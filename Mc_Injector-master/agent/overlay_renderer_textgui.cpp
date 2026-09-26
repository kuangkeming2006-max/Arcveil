#include "overlay_renderer_internal.h"
#include "UiColors.h"
#include "FeatureNavigation.h"
#include "assets/KenneyInputPromptsResource.h"
#include <span>

#include "src/AgentLog.h"
#include "tsf_candidates.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_win32.h>

#include <gl/GL.h>
#include <imm.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <functional>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "stb_image.h"

namespace mcoverlay {

using namespace renderer_detail;

void OverlayRenderer::renderTextGui(RenderFrameContext& frame) noexcept
{
    const auto& interactive = frame.interactive;
    auto& io = frame.io;
    const auto& uiScale = frame.uiScale;
    const auto& delta = frame.delta;

    // Text GUI is deliberately a text-only HUD: no window surface is drawn.
    // A single invisible hit target owns dragging while the Click GUI is open,
    // avoiding competing per-row hover/cursor state.
    if (m_features.textGuiEnabled) {
        struct TextModule { const char* name; const char* mode; bool enabled; };
        const char* const aimMode=m_features.aimSilentLock ? "Silent Lock"
            : m_features.aimLockOnMode ? "Lock On" : "Smooth";
        const std::array<TextModule, 22U> modules{{
            {"Player ESP", "", m_features.entityEspEnabled},
            {"Bed ESP", "", m_features.bedEspEnabled},
            {"Nametag", "", m_features.nametagEnabled},
            {"Bed Alert", "", m_features.bedThreatAlertsEnabled},
            {"Safewalk", "", m_features.safewalkEnabled},
            {"Scaffold", m_features.scaffoldSameLayerOnly ? "Same Layer" : "Dynamic", m_features.scaffoldEnabled},
            {"Fly", "", m_features.flyEnabled},
            {"BHop", m_features.bhopAutoJump ? "Auto Jump" : "Manual", m_features.bhopEnabled},
            {"Aim Assist", aimMode, m_features.aimAssistEnabled},
            {"Smart Hotbar", "Minecraft Keys", m_features.smartHotbarEnabled},
            {"Bed Breaker", "", m_features.bedBreakerEnabled},
            {"Player Stats", "", m_features.hypixelPanelEnabled},
            {"Debug", "", m_features.debugChatEnabled},
            {"Blacklist", "", m_blacklist.panelEnabled},
            {"Fireball ESP", "", m_features.fireballEspEnabled},
            {"Knockback Prediction", "", m_features.knockbackPredictionEnabled},
            {"Bow Prediction", "", m_features.bowPredictionEnabled},
            {"Velocity", "", m_features.localVelocityEnabled},
            {"FreeLook", "Hold", m_features.freeLookEnabled},
            {"Now Playing", "", m_mediaSettings.enabled},
            {"Sprint", "", m_features.sprintEnabled},
            {"Attack Shield", "", m_features.attackShieldEnabled}}};
        static_assert(modules.size()==std::tuple_size_v<decltype(m_textGuiModuleProgress)>);
        static_assert(modules.size()==std::tuple_size_v<decltype(m_textGuiModuleVelocity)>);
        static_assert(modules.size()==std::tuple_size_v<decltype(m_textGuiGlyphBrightness)>);
        static_assert(modules.size()==std::tuple_size_v<decltype(m_textGuiGlyphTargets)>);
        ImFont* const textGuiFont = m_boldFonts[static_cast<std::size_t>(
            std::clamp(m_guiScaleIndex, 0, 3))] != nullptr
            ? m_boldFonts[static_cast<std::size_t>(
                std::clamp(m_guiScaleIndex, 0, 3))] : ImGui::GetFont();
        const float textGuiFontSize = ImGui::GetFontSize() * 1.20F;
        const auto measureText = [&](const char* const value) noexcept {
            return textGuiFont->CalcTextSizeA(textGuiFontSize, 100000.0F,
                                              0.0F, value);
        };
        const std::uint64_t glyphClock = static_cast<std::uint64_t>(
            ImGui::GetTime() * 1000.0);
        if (!m_textGuiGlyphsInitialized ||
            glyphClock >= m_textGuiNextShuffleTick) {
            for (std::size_t moduleIndex = 0U;
                 moduleIndex < modules.size(); ++moduleIndex) {
                const std::size_t length = std::min<std::size_t>(
                    std::strlen(modules[moduleIndex].name), 32U);
                const auto previousTargets = m_textGuiGlyphTargets[moduleIndex];
                std::array<std::uint8_t, 32U> order{};
                for (std::size_t character = 0U; character < length; ++character) {
                    order[character] = static_cast<std::uint8_t>(character);
                    m_textGuiGlyphTargets[moduleIndex][character] = 0.50F;
                }
                std::uint32_t randomState = static_cast<std::uint32_t>(
                    glyphClock ^ (moduleIndex + 1U) * 0x9E3779B9U);
                for (std::size_t remaining = length; remaining > 1U; --remaining) {
                    randomState = randomState * 1664525U + 1013904223U;
                    const std::size_t swapIndex = randomState % remaining;
                    std::swap(order[remaining - 1U], order[swapIndex]);
                }
                for (std::size_t bright = 0U; bright < (length + 1U) / 2U;
                     ++bright) {
                    m_textGuiGlyphTargets[moduleIndex][order[bright]] = 1.0F;
                }
                // A random shuffle can repeat a whole short label. Guarantee
                // a visible change by exchanging two different visible glyphs.
                bool visibleChange = false;
                for (std::size_t c = 0; c < length; ++c)
                    visibleChange |= modules[moduleIndex].name[c] != ' ' &&
                        previousTargets[c] != m_textGuiGlyphTargets[moduleIndex][c];
                if (!visibleChange && length > 1) {
                    std::size_t bright = length, dim = length;
                    for (std::size_t c = 0; c < length; ++c) {
                        if (modules[moduleIndex].name[c] == ' ') continue;
                        if (m_textGuiGlyphTargets[moduleIndex][c] > 0.75F) bright = c;
                        else dim = c;
                    }
                    if (bright < length && dim < length)
                        std::swap(m_textGuiGlyphTargets[moduleIndex][bright],
                                  m_textGuiGlyphTargets[moduleIndex][dim]);
                }
            }
            if (!m_textGuiGlyphsInitialized) {
                m_textGuiGlyphBrightness = m_textGuiGlyphTargets;
                m_textGuiGlyphsInitialized = true;
            }
            // Recompose the half-bright mask at a calm cadence. Brightness is
            // interpolated below, so individual glyphs never flash abruptly.
            m_textGuiNextShuffleTick = glyphClock + 2400U;
        }
        // Targets change every 2.4 s; the slower continuous transition has
        // time to settle and remains frame-rate independent.
        const float glyphBlend = 1.0F - std::exp(-1.8F * delta);
        for (std::size_t moduleIndex = 0U; moduleIndex < modules.size(); ++moduleIndex)
            for (std::size_t character = 0U; character < 32U; ++character)
                m_textGuiGlyphBrightness[moduleIndex][character] +=
                    (m_textGuiGlyphTargets[moduleIndex][character] -
                     m_textGuiGlyphBrightness[moduleIndex][character]) * glyphBlend;

        float visibleRows = 0.0F;
        float maximumTextWidth = 0.0F;
        for (std::size_t index = 0U; index < modules.size(); ++index) {
            advancePresentationSpring(m_textGuiModuleProgress[index],
                                      m_textGuiModuleVelocity[index],
                                      modules[index].enabled ? 1.0F : 0.0F, delta);
            const float progress = std::clamp(
                m_textGuiModuleProgress[index], 0.0F, 1.0F);
            if (progress <= 0.004F) continue;
            visibleRows += progress;
            const float modeWidth=m_features.textGuiShowModes && modules[index].mode[0]
                ? measureText(modules[index].mode).x+9.0F*uiScale : 0.0F;
            maximumTextWidth = std::max(maximumTextWidth,
                measureText(modules[index].name).x+modeWidth);
        }
        if (visibleRows > 0.004F) {
            const float width = maximumTextWidth +
                (m_features.textGuiVerticalLine ? 28.0F : 20.0F) * uiScale;
            const float lineHeight = textGuiFontSize + 5.0F * uiScale;
            const float height = lineHeight * visibleRows +
                                 8.0F * uiScale;
            const float defaultX = std::max(5.0F, io.DisplaySize.x - width - 16.0F);
            const float defaultY = std::max(5.0F, io.DisplaySize.y * 0.18F);
            float textX = m_features.textGuiX < 0 ? defaultX
                : io.DisplaySize.x * static_cast<float>(m_features.textGuiX) / 1000.0F;
            float textY = m_features.textGuiY < 0 ? defaultY
                : io.DisplaySize.y * static_cast<float>(m_features.textGuiY) / 1000.0F;
            textX = std::clamp(textX, 4.0F,
                std::max(4.0F, io.DisplaySize.x - width - 4.0F));
            textY = std::clamp(textY, 4.0F,
                std::max(4.0F, io.DisplaySize.y - height - 4.0F));
            ImGui::SetNextWindowPos(ImVec2(textX, textY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0F);
            ImGuiWindowFlags textFlags = ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoScrollbar;
            if (!interactive) textFlags |= ImGuiWindowFlags_NoInputs;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (ImGui::Begin("##TextGuiHud", nullptr, textFlags)) {
                ImGui::InvisibleButton("##TextGuiDrag", ImVec2(width, height));
                if (interactive && ImGui::IsItemActive() &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
                    textX = std::clamp(textX + io.MouseDelta.x, 4.0F,
                        std::max(4.0F, io.DisplaySize.x - width - 4.0F));
                    textY = std::clamp(textY + io.MouseDelta.y, 4.0F,
                        std::max(4.0F, io.DisplaySize.y - height - 4.0F));
                    m_features.textGuiX = std::clamp(static_cast<int>(std::lround(
                        textX / std::max(1.0F, io.DisplaySize.x) * 1000.0F)), 0, 1000);
                    m_features.textGuiY = std::clamp(static_cast<int>(std::lround(
                        textY / std::max(1.0F, io.DisplaySize.y) * 1000.0F)), 0, 1000);
                    m_featureSettingsDirty = true;
                }
                ImDrawList* const textDraw = ImGui::GetWindowDrawList();
                const std::array<float, 3U> baseRgb =
                    unpackRgb(m_features.textGuiColor);
                const ImVec4 base(baseRgb[0U], baseRgb[1U], baseRgb[2U], 1.0F);
                float rowY = textY + 4.0F * uiScale;
                if (m_features.textGuiVerticalLine) {
                    const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(base.x, base.y, base.z, 0.82F));
                    textDraw->AddRectFilled(
                        ImVec2(textX + 3.0F * uiScale, textY + 3.0F * uiScale),
                        ImVec2(textX + 6.0F * uiScale,
                               textY + height - 3.0F * uiScale),
                        lineColor, 1.5F * uiScale);
                }
                for (std::size_t moduleIndex = 0U;
                     moduleIndex < modules.size(); ++moduleIndex) {
                    const TextModule& module = modules[moduleIndex];
                    const float progress = std::clamp(
                        m_textGuiModuleProgress[moduleIndex], 0.0F, 1.0F);
                    if (progress <= 0.004F) continue;
                    const float eased = progress * progress * (3.0F - 2.0F * progress);
                    const ImVec2 nameSize = measureText(module.name);
                    const float modeWidth=m_features.textGuiShowModes && module.mode[0]
                        ? measureText(module.mode).x+9.0F*uiScale : 0.0F;
                    const ImVec2 textSize(nameSize.x+modeWidth,nameSize.y);
                    const float contentLeft = textX +
                        (m_features.textGuiVerticalLine ? 11.0F : 4.0F) * uiScale;
                    const float contentRight = textX + width - 4.0F * uiScale;
                    float glyphX = contentLeft;
                    if (m_features.textGuiAlignment == 1)
                        glyphX = (contentLeft + contentRight - textSize.x) * 0.5F;
                    else if (m_features.textGuiAlignment == 2)
                        glyphX = contentRight - textSize.x;
                    const float entryDirection = m_features.textGuiAlignment == 0
                        ? -1.0F : 1.0F;
                    glyphX += entryDirection * (1.0F - eased) * 18.0F * uiScale;
                    const float glyphY = rowY + (1.0F - eased) * 4.0F * uiScale;
                    // Half of the glyphs are bright and half are 50% dimmed.
                    // A continuously regenerated mask flows through a smooth
                    // exponential transition, producing a restrained optical
                    // shimmer rather than a scrolling-lyrics effect.
                    for (std::size_t characterIndex = 0U;
                         module.name[characterIndex] != '\0'; ++characterIndex) {
                        char glyph[2]{module.name[characterIndex], '\0'};
                        const float glyphWidth = measureText(glyph).x;
                        const float brightness = m_textGuiGlyphBrightness[
                            moduleIndex][std::min<std::size_t>(characterIndex, 31U)];
                        textDraw->AddText(textGuiFont, textGuiFontSize,
                            ImVec2(glyphX + 1.0F, glyphY + 1.4F),
                            IM_COL32(0, 0, 0,
                                static_cast<int>(145.0F * eased * brightness)), glyph);
                        const ImVec4 glyphColor(
                            std::clamp(base.x * 0.58F + 0.42F, 0.0F, 1.0F),
                            std::clamp(base.y * 0.58F + 0.42F, 0.0F, 1.0F),
                            std::clamp(base.z * 0.58F + 0.42F, 0.0F, 1.0F),
                            eased * brightness);
                        textDraw->AddText(textGuiFont, textGuiFontSize,
                            ImVec2(glyphX, glyphY),
                            ImGui::ColorConvertFloat4ToU32(glyphColor), glyph);
                        glyphX += glyphWidth;
                    }
                    if(modeWidth>0.0F) {
                        glyphX+=9.0F*uiScale;
                        textDraw->AddText(textGuiFont,textGuiFontSize,
                            ImVec2(glyphX,glyphY),IM_COL32(238,240,246,
                                static_cast<int>(145.0F*eased)),module.mode);
                    }
                    rowY += lineHeight * progress;
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }
    }

}

} // namespace mcoverlay
