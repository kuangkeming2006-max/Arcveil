#pragma once
#include <algorithm>
#include <cstdint>
namespace mcoverlay {
// Upper 24 bits of the existing preferences word. A zero blur code means an
// old configuration: use the new default instead of silently disabling blur.
struct UiPreferences {
    int blur = 65;
    int imeX = -1; // 0..255, normalized to the available drag area
    int imeY = -1;
};
inline std::uint32_t packUiPreferences(UiPreferences p) noexcept
{
    const auto blur = static_cast<std::uint32_t>(std::clamp(p.blur, 0, 100) + 1);
    if (p.imeX < 0 || p.imeY < 0) return blur << 8U;
    return (blur << 8U) | 0x80000000U |
        (static_cast<std::uint32_t>(std::clamp(p.imeX, 0, 255)) << 15U) |
        (static_cast<std::uint32_t>(std::clamp(p.imeY, 0, 255)) << 23U);
}
inline UiPreferences unpackUiPreferences(std::uint32_t bits) noexcept
{
    UiPreferences p;
    const int code = static_cast<int>((bits >> 8U) & 127U);
    p.blur = code == 0 ? 65 : std::clamp(code - 1, 0, 100);
    if (bits & 0x80000000U) {
        p.imeX = static_cast<int>((bits >> 15U) & 255U);
        p.imeY = static_cast<int>((bits >> 23U) & 255U);
    }
    return p;
}
}
