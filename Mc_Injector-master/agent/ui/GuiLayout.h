#pragma once
#include <algorithm>

namespace mcoverlay::ui {
inline constexpr int minimumGuiElementScale = 60;
inline constexpr int maximumGuiElementScale = 150;
constexpr bool validGuiElementScale(int percent) noexcept {
    return percent >= minimumGuiElementScale && percent <= maximumGuiElementScale;
}
constexpr int normalizeGuiElementScale(int percent) noexcept {
    return std::clamp(percent, minimumGuiElementScale, maximumGuiElementScale);
}
} // namespace mcoverlay::ui
