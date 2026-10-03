#pragma once
#include <algorithm>

namespace mcoverlay::ui {
// Effective pixels, independent of the S/M/L/XL layout preference.
struct GuiTypography {
    static constexpr int minimumSize=14, maximumSize=24;
    int size=18;
    int weight=600;
    constexpr bool operator==(const GuiTypography&) const = default;
};
constexpr bool validTypography(int size,int weight) noexcept {
    return size>=GuiTypography::minimumSize && size<=GuiTypography::maximumSize &&
        (weight==400 || weight==600 || weight==700);
}
constexpr GuiTypography normalizeTypography(GuiTypography value) noexcept {
    value.size=std::clamp(value.size,GuiTypography::minimumSize,GuiTypography::maximumSize);
    if(value.weight!=400 && value.weight!=600 && value.weight!=700) value.weight=600;
    return value;
}
// A single atomic carries both settings, so a frame cannot mix revisions.
constexpr int packTypography(GuiTypography value) noexcept {
    value=normalizeTypography(value);
    return (value.weight<<8)|value.size;
}
constexpr GuiTypography unpackTypography(int packed) noexcept {
    return normalizeTypography({packed&255,packed>>8});
}
inline float clickGuiDisplayScale(float baseScale,float width,float height) noexcept {
    // Minecraft supplies framebuffer pixels rather than Windows effective
    // pixels. 1600px receives 160% density compensation; smaller viewports fit.
    const float density=std::clamp(height/1000.F,1.F,1.8F);
    const float fit=std::max(.65F,std::min(width/1020.F,height/790.F));
    return std::min(baseScale*density,fit);
}
} // namespace mcoverlay::ui
