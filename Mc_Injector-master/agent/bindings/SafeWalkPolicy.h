#pragma once
#include <algorithm>
#include <cmath>

namespace mcoverlay::safewalk {
// A positive inset asks for more support BEFORE the physical 0.6m body leaves
// a ledge. The former slider inverted this relationship. Keep a 2cm minimum
// overlap even at 100%; sensitivity never disables the collision check.
inline double insetForSensitivity(int sensitivity) noexcept
{
    return 0.27 - 0.25 * std::clamp(sensitivity, 0, 100) / 100.0;
}
inline double predictedMotion(double residual, double intent) noexcept
{
    // At SwapBuffers, grounded motion has already been damped by vanilla.
    // Add one tick of input acceleration; tiny residual motion is NOT the
    // entire next movement. Preserve inertia when keys are released.
    return std::clamp(residual + intent * 0.10, -1.0, 1.0);
}
template<class HasSupport>
bool needsSneak(int sensitivity, double motionX, double motionZ,
               double intentX, double intentZ, HasSupport&& supported)
{
    const double inset = insetForSensitivity(sensitivity);
    return !supported(0.0, 0.0, inset) ||
           !supported(predictedMotion(motionX, intentX),
                      predictedMotion(motionZ, intentZ), inset);
}
}
