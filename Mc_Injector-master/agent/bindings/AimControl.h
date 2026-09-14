#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mcoverlay::aim {

enum class Mode { Smooth, LockOn };
struct Angles { double yaw = 0, pitch = 0; };
inline double wrap(double angle) noexcept { return std::remainder(angle, 360.0); }

// Entity.setAngles applies the same delta to current AND previous rotations.
// Writing current alone makes orientCamera replay our frame update with the
// 20 Hz partial-tick sawtooth. Preserve the user's existing interpolation;
// do not overwrite previous with current (which would erase their input).
inline Angles shiftedPrevious(Angles previous, Angles current, Angles output) noexcept {
    return {previous.yaw + wrap(output.yaw - current.yaw),
            previous.pitch + output.pitch - current.pitch};
}

// One producer (AgentRuntime) supplies this phase to both ESP and aiming.
// A partialTicks wrap with the same entity sample is a missing sample, not
// movement back to previousPosition. Extrapolation is bounded to two ticks.
class RenderClock final {
public:
    double update(std::uint64_t generation, std::uint64_t world, float partial) noexcept {
        partial = std::isfinite(partial) ? std::clamp(partial, 0.0F, 1.0F) : 0;
        if (generation != m_generation || world != m_world) m_missing = 0;
        else if (partial + 0.20F < m_partial) m_missing = std::min(2U, m_missing + 1);
        m_generation = generation; m_world = world; m_partial = partial;
        return static_cast<double>(partial) + m_missing;
    }
private:
    std::uint64_t m_generation = 0, m_world = 0;
    unsigned m_missing = 0;
    float m_partial = 0;
};

class ExactLockOutput final {
public:
    static Angles apply(Angles current, Angles desired) noexcept {
        return {current.yaw + wrap(desired.yaw - current.yaw),
                std::clamp(desired.pitch, -90.0, 90.0)};
    }
};

// A different controller, not a slower hard-lock. Angular velocity and
// acceleration are bounded in degrees/second and degrees/second^2. Residual
// mouse counts survive sub-count frames but are cleared on mode/target reset.
class SmoothMouseOutput final {
public:
    void reset(Angles current) noexcept {
        m_filtered = current; m_yaw = {}; m_pitch = {};
    }
    Angles apply(Angles current, Angles desired, double seconds,
                 double sensitivity, int speedPercent) noexcept {
        if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(sensitivity) ||
            !std::isfinite(desired.yaw) || !std::isfinite(desired.pitch)) return current;
        const double dt = std::min(seconds, 0.025); // Hitches never accumulate a large turn.
        const double speed = std::clamp(speedPercent, 1, 100) / 100.0;
        const double filter = -std::expm1(-(14.0 + 16.0 * speed) * dt);
        m_filtered.yaw += wrap(desired.yaw - m_filtered.yaw) * filter;
        m_filtered.pitch += (desired.pitch - m_filtered.pitch) * filter;
        const double f = std::clamp(sensitivity, 0.0, 1.0) * 0.6 + 0.2;
        const double quantum = f * f * f * 8.0 * 0.15; // Minecraft 1.8.9 mouse pipeline.
        return {current.yaw + step(m_yaw, wrap(m_filtered.yaw-current.yaw), dt, speed, quantum),
                std::clamp(current.pitch + step(m_pitch, m_filtered.pitch-current.pitch,
                                              dt, speed, quantum), -90.0, 90.0)};
    }
private:
    struct Axis { double velocity = 0, remainder = 0, lastError = 0; };
    static double step(Axis& axis, double error, double dt, double speed, double quantum) noexcept {
        if (error * axis.lastError < 0) { axis.velocity = 0; axis.remainder = 0; }
        axis.lastError = error;
        const double maxSpeed = 8.0 + 352.0 * speed * speed;
        const double acceleration = 60.0 + 1440.0 * speed * speed;
        // Braking-distance limit avoids oscillating across a stationary target.
        const double brakingSpeed = std::sqrt(2.0 * acceleration * std::abs(error));
        const double desiredVelocity = std::clamp(error * (2.0 + 12.0*speed*speed),
            -std::min(maxSpeed, brakingSpeed), std::min(maxSpeed, brakingSpeed));
        axis.velocity += std::clamp(desiredVelocity-axis.velocity, -acceleration*dt, acceleration*dt);
        double delta = axis.velocity * dt;
        if (delta * error <= 0) delta = 0;
        if (std::abs(delta) > std::abs(error)) delta = error;
        axis.remainder += delta / quantum;
        double counts = std::trunc(axis.remainder);
        // A quantized count may not cross the aim point. This is a quiet
        // sub-count residual, not the old alternating +/- snap at AABB edges.
        const double maxCounts = std::floor(std::abs(error) / quantum);
        counts = std::clamp(counts, -maxCounts, maxCounts);
        axis.remainder = std::clamp(axis.remainder - counts, -1.0, 1.0);
        return counts * quantum;
    }
    Angles m_filtered;
    Axis m_yaw, m_pitch;
};

} // namespace mcoverlay::aim
