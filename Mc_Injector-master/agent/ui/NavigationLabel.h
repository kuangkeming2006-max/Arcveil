#pragma once

#include "../FeatureNavigation.h"
#include "GuiDrawPolicy.h"
#include <imgui.h>
#include <algorithm>
#include <limits>
#include <string_view>

namespace mcoverlay::ui {
namespace navigation_label_detail {

// Keep a UTF-8 character intact when applying a glyph's single highlight color.
// All current navigation labels are ASCII, matching the original byte loop.
inline std::size_t nextCharacter(std::string_view text, std::size_t offset) noexcept
{
    const auto first = static_cast<unsigned char>(text[offset]);
    const std::size_t length = first < 0x80U ? 1U :
        first >= 0xC2U && first <= 0xDFU ? 2U :
        first >= 0xE0U && first <= 0xEFU ? 3U :
        first >= 0xF0U && first <= 0xF4U ? 4U : 1U;
    if (offset + length > text.size()) return offset + 1U;
    for (std::size_t byte = 1U; byte < length; ++byte)
        if ((static_cast<unsigned char>(text[offset + byte]) & 0xC0U) != 0x80U)
            return offset + 1U;
    return offset + length;
}

} // namespace navigation_label_detail

// Restore the original enabled-feature flow: a whole-glyph highlight travels
// back and forth using FeatureNavigation::glyphGlow's unchanged timing/shape.
// The caller supplies theme colors (near-white on dark, deep purple on light).
// Opacity is local only; keep it at 1 when the root draw data applies its fade.
inline float drawNavigationLabel(ImDrawList* drawList, ImFont* font,
    float fontSize, ImVec2 position, std::string_view label,
    const ImVec4& baseColor, const ImVec4& flowHighlightColor,
    bool enabled, bool reducedMotion, double time, float opacity = 1.0F)
{
    if (drawList == nullptr || font == nullptr || fontSize <= 0.0F || label.empty())
        return 0.0F;

    const float alpha = std::clamp(opacity, 0.0F, 1.0F);
    const auto encodeColor = [alpha](ImVec4 color) {
        color.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32(color);
    };
    const char* const begin = label.data();
    const char* const end = begin + label.size();
    // Every glyph retains its fractional origin during hover translation.
    const auto previousFlags=drawList->Flags;
    drawList->Flags |= ImDrawListFlags_TextNoPixelSnap;
    constexpr float unlimitedWidth = std::numeric_limits<float>::max();
    if (!enabled || reducedMotion) {
        drawList->AddText(font, fontSize, position, encodeColor(baseColor), begin, end);
        drawList->Flags=previousFlags;
        return font->CalcTextSizeA(fontSize, unlimitedWidth, 0.0F, begin, end).x;
    }

    std::size_t count = 0U;
    for (std::size_t offset = 0U; offset < label.size(); ++count)
        offset = navigation_label_detail::nextCharacter(label, offset);

    const float originX = position.x;
    std::size_t glyph = 0U;
    for (std::size_t offset = 0U; offset < label.size(); ++glyph) {
        const std::size_t next = navigation_label_detail::nextCharacter(label, offset);
        const float glow = navigation::glyphGlow(glyph, count, time);
        const ImVec4 color(
            baseColor.x + (flowHighlightColor.x - baseColor.x) * glow,
            baseColor.y + (flowHighlightColor.y - baseColor.y) * glow,
            baseColor.z + (flowHighlightColor.z - baseColor.z) * glow,
            baseColor.w + (flowHighlightColor.w - baseColor.w) * glow);
        const char* const characterBegin = begin + offset;
        const char* const characterEnd = begin + next;
        drawList->AddText(font, fontSize, position, encodeColor(color),
            characterBegin, characterEnd);
        position.x += font->CalcTextSizeA(fontSize, unlimitedWidth, 0.0F,
            characterBegin, characterEnd).x;
        offset = next;
    }
    drawList->Flags=previousFlags;
    return position.x - originX;
}

} // namespace mcoverlay::ui
