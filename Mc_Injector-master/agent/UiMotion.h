#pragma once

#include <algorithm>
#include <cmath>

namespace mcoverlay {

// Frame-rate independent first-order response. Using one rate for both target
// directions makes hover recovery exactly as quick as hover acquisition.
inline float approachExponential(const float current,const float target,
                                 const float rate,const float seconds) noexcept
{
    return current+(target-current)*(1.0F-std::exp(-std::max(0.0F,rate)*
        std::clamp(seconds,0.0F,0.05F)));
}

// Unit-step response of an under-damped second-order spring. Response is the
// approximate oscillation period and dampingFraction is the usual fraction of
// critical damping used by platform UI spring APIs.
inline float dampedSpringStep(const float seconds,const float response,
                              const float dampingFraction) noexcept
{
    if(seconds<=0.0F) return 0.0F;
    constexpr float tau=6.28318530717958647692F;
    const float omega=tau/std::max(0.05F,response);
    const float damping=std::clamp(dampingFraction,0.01F,0.999F);
    const float root=std::sqrt(std::max(0.0001F,1.0F-damping*damping));
    const float damped=omega*root;
    const float envelope=std::exp(-damping*omega*seconds);
    return 1.0F-envelope*(std::cos(damped*seconds)+
        (damping/root)*std::sin(damped*seconds));
}

struct SearchActivationMotion final {
    static constexpr float PressSeconds=0.075F;
    static constexpr float PressScale=0.94F;
    static constexpr float ReleaseSeconds=0.135F;
    static constexpr float ExpandSeconds=0.360F;

    [[nodiscard]] static float scale(const float elapsed) noexcept
    {
        if(elapsed<=0.0F) return 1.0F;
        if(elapsed<PressSeconds) {
            const float x=std::clamp(elapsed/PressSeconds,0.0F,1.0F);
            const float eased=x*x*(3.0F-2.0F*x);
            return 1.0F+(PressScale-1.0F)*eased;
        }
        const float x=std::clamp((elapsed-PressSeconds)/ReleaseSeconds,0.0F,1.0F);
        const float eased=x*x*(3.0F-2.0F*x);
        return PressScale+(1.0F-PressScale)*eased;
    }

    [[nodiscard]] static float expansion(const float elapsed) noexcept
    {
        if(elapsed<=PressSeconds) return 0.0F;
        const float x=std::clamp((elapsed-PressSeconds)/ExpandSeconds,0.0F,1.0F);
        // Monotonic ease-out: the click still compresses first, but the field
        // never overshoots after it has reached its expanded width.
        return 1.0F-std::pow(1.0F-x,3.0F);
    }
};

// A finite phase avoids the asymptotic tail (and final one-frame snap) of an
// exponential layout animation. Every consumer derives height, opacity and
// corner/icon motion from the same eased value.
struct CollapsibleMotion final {
    static constexpr float DurationSeconds=0.28F;

    [[nodiscard]] static float advance(const float phase,const bool open,
                                       const float seconds) noexcept {
        const float direction=open?1.0F:-1.0F;
        return std::clamp(phase+direction*
            std::clamp(seconds,0.0F,0.05F)/DurationSeconds,0.0F,1.0F);
    }
    [[nodiscard]] static float eased(const float phase) noexcept {
        const float value=std::clamp(phase,0.0F,1.0F);
        return value*value*(3.0F-2.0F*value);
    }
};

// Separate wheel destination from the displayed offset. Repeated wheel events
// accumulate without restarting an animation, and scrollbar drags stay direct.
struct SmoothScroll final {
    float current = 0.0F;
    float target = 0.0F;
    float scrollbarAlpha = 0.0F;
    float scrollbarIdleSeconds = 10.0F;
    float lastMaximum = 0.0F;
    bool initialized = false;
    bool maximumInitialized = false;

    void update(float actual, float maximum, float wheel, float lineHeight,
                float seconds, bool dragging) noexcept
    {
        maximum = std::max(0.0F, maximum);
        if (!initialized) {
            current = target = std::clamp(actual, 0.0F, maximum);
            initialized = true;
        }
        const float edgeBand=std::max(1.0F,lineHeight*0.75F);
        // Dynamic accordion height changes move the true scroll maximum every
        // frame.  When the user is already at the bottom, shift both ends by
        // the same delta so the viewport remains bottom-anchored continuously
        // instead of clamping late and hitching on the final collapse frame.
        if(maximumInitialized&&!dragging) {
            const bool bottomAnchored=lastMaximum>0.0F&&
                (lastMaximum-target<=edgeBand||
                 lastMaximum-current<=edgeBand);
            if(bottomAnchored) {
                const float maximumDelta=maximum-lastMaximum;
                current+=maximumDelta;
                target+=maximumDelta;
            }
        }
        lastMaximum=maximum;
        maximumInitialized=true;
        current = std::clamp(current, 0.0F, maximum);
        target = std::clamp(target, 0.0F, maximum);
        if (dragging) {
            // A real scrollbar drag is authoritative and intentionally bypasses
            // interpolation. Normal wheel motion must keep its own continuous
            // accumulator: Dear ImGui rounds the applied scroll to pixels, and
            // feeding that rounded value back into the lerp creates a 1-4 px
            // quantization dead-zone at both endpoints.
            current = target = std::clamp(actual, 0.0F, maximum);
        }
        target = std::clamp(target - wheel * lineHeight * 4.0F, 0.0F, maximum);
        // Snap the last partial line to the true ImGui endpoints. Fractional
        // DPI/font metrics otherwise leave roughly half a glyph clipped even
        // though another wheel notch has already been consumed.
        if(wheel>0.001F&&target<edgeBand) target=0.0F;
        if(wheel<-0.001F&&maximum-target<edgeBand) target=maximum;
        const float blend = 1.0F - std::exp(-16.0F * std::clamp(seconds, 0.0F, 0.05F));
        if (!dragging) {
            current = std::clamp(current + (target - current) * blend, 0.0F, maximum);
            if (std::abs(target - current) < 0.15F) current = target;
        }

        const bool active = maximum > 0.0F &&
            (std::abs(wheel) > 0.001F || dragging || std::abs(target - current) > 0.2F);
        if (active) scrollbarIdleSeconds = 0.0F;
        else scrollbarIdleSeconds += std::clamp(seconds, 0.0F, 0.05F);
        const float wanted = maximum > 0.0F && scrollbarIdleSeconds < 0.72F ? 1.0F : 0.0F;
        const float fadeBlend = 1.0F - std::exp(-10.0F * std::clamp(seconds, 0.0F, 0.05F));
        scrollbarAlpha += (wanted - scrollbarAlpha) * fadeBlend;
        if (std::abs(wanted - scrollbarAlpha) < 0.01F) scrollbarAlpha = wanted;
    }
};

} // namespace mcoverlay
