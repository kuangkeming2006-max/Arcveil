#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mcoverlay::prediction {
struct Velocity { double x = 0, y = 0, z = 0; };
struct HurtSample {
    float health = 0;
    int hurtTime = -1; // -1 means this optional mapping is unavailable.
    bool onGround = true;
    bool groundKnown = false;
    Velocity velocity{};
};
struct EvidenceResult {
    bool damage = false, impulse = false, triggered = false;
};

struct VelocityConfidence final {
    bool confident=false;
    double directionAgreement=0.0;
    double speedRatio=0.0;
    Velocity residual{};
};

// A remote player's motion fields and interpolated position are independent
// observations. Do not extrapolate until the next 20 TPS displacement agrees
// with the candidate impulse. The returned residual advances the measured
// displacement through one vanilla airborne drag/gravity step because the
// measured displacement has already been consumed by the current position.
inline VelocityConfidence confirmVelocity(const Velocity impulse,
    const Velocity measured,const std::uint64_t elapsedMilliseconds) noexcept {
    VelocityConfidence result;
    if(elapsedMilliseconds<35U||elapsedMilliseconds>90U||
       !std::isfinite(impulse.x)||!std::isfinite(impulse.y)||
       !std::isfinite(impulse.z)||!std::isfinite(measured.x)||
       !std::isfinite(measured.y)||!std::isfinite(measured.z)) return result;
    const double impulseHorizontal=std::hypot(impulse.x,impulse.z);
    const double measuredHorizontal=std::hypot(measured.x,measured.z);
    if(impulseHorizontal<0.04||impulseHorizontal>2.5||
       measuredHorizontal<0.035||measuredHorizontal>1.8||
       std::abs(measured.y)>1.25) return result;
    result.directionAgreement=(impulse.x*measured.x+impulse.z*measured.z)/
        (impulseHorizontal*measuredHorizontal);
    result.speedRatio=measuredHorizontal/impulseHorizontal;
    result.confident=result.directionAgreement>=0.82&&
        result.speedRatio>=0.38&&result.speedRatio<=1.18&&
        measured.y>-0.18;
    if(result.confident) result.residual={measured.x*0.91,
        (measured.y-0.08)*0.98,measured.z*0.91};
    return result;
}

// Damage metadata/status and movement can arrive in different network ticks.
// Correlate two independent edges for at most 250 ms, in either arrival order.
// A swing, ordinary jump, or stationary hurt animation is insufficient alone.
class KnockbackEvidence final {
public:
    EvidenceResult update(const HurtSample& sample, std::uint64_t now) noexcept {
        EvidenceResult result;
        const auto speed = [](Velocity v) { return std::hypot(v.x, v.z); };
        const bool valid = sample.groundKnown && std::isfinite(sample.health) &&
            sample.health > 0 && std::isfinite(sample.velocity.x) &&
            std::isfinite(sample.velocity.y) && std::isfinite(sample.velocity.z);
        if (!valid || !m_valid || now <= m_last || now - m_last > 250) {
            m_previous = sample; m_last = now; m_valid = valid;
            m_hasDamage = m_hasImpulse = false;
            return result;
        }
        result.damage = sample.health + 0.01F < m_previous.health ||
            (sample.hurtTime >= 0 && m_previous.hurtTime >= 0 &&
             sample.hurtTime > m_previous.hurtTime + 1);
        const double horizontal = speed(sample.velocity);
        const double change = std::hypot(sample.velocity.x - m_previous.velocity.x,
                                        sample.velocity.z - m_previous.velocity.z);
        result.impulse = !sample.onGround && sample.velocity.y > 0.015 &&
            horizontal >= 0.04 &&
            (change >= 0.035 || horizontal > speed(m_previous.velocity) + 0.035) &&
            (m_previous.onGround || sample.velocity.y > m_previous.velocity.y + 0.025);
        if (result.damage) { m_damageAt = now; m_hasDamage = true; }
        if (result.impulse) { m_impulseAt = now; m_hasImpulse = true; }
        if (m_hasDamage && now - m_damageAt > 250) m_hasDamage = false;
        if (m_hasImpulse && now - m_impulseAt > 250) m_hasImpulse = false;
        result.triggered = m_hasDamage && m_hasImpulse && !sample.onGround &&
            horizontal >= 0.025;
        if (result.triggered || sample.onGround) {
            // Landing invalidates an old impulse, but preserve a just-received
            // damage edge while waiting for the next movement update.
            m_hasImpulse = false;
            if (result.triggered) m_hasDamage = false;
        }
        m_previous = sample; m_last = now;
        return result;
    }
private:
    HurtSample m_previous{};
    std::uint64_t m_last = 0, m_damageAt = 0, m_impulseAt = 0;
    bool m_valid = false, m_hasDamage = false, m_hasImpulse = false;
};
} // namespace mcoverlay::prediction
