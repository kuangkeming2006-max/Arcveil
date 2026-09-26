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

void OverlayRenderer::renderPlayerStatsPanel(RenderFrameContext& frame) noexcept
{
    const auto& snapshot = frame.snapshot;
    const auto& interactive = frame.interactive;
    auto& io = frame.io;
    const auto& delta = frame.delta;
    const auto& gameplayHotkeysAllowed = frame.gameplayHotkeysAllowed;

    const bool statsHotkeyDown = gameplayHotkeysAllowed &&
        (::GetAsyncKeyState(m_features.hypixelPanelHotkey) & 0x8000) != 0;
    const bool statsPanelTarget = m_features.hypixelPanelEnabled &&
        snapshot.matchActive && snapshot.playerCount > 0U &&
        (!m_features.hypixelPanelHoldToShow || statsHotkeyDown || interactive);
    advancePresentationSpring(m_statsPanelProgress, m_statsPanelVelocity,
                              statsPanelTarget ? 1.0F : 0.0F, delta);

    if (m_statsPanelProgress > 0.005F) {
            const float panelLinear = std::clamp(m_statsPanelProgress, 0.0F, 1.0F);
            const float panelEase = panelLinear * panelLinear *
                                    (3.0F - 2.0F * panelLinear);
            const float panelPresentationScale =
                1.26F - 0.26F * m_statsPanelProgress;
            const float panelWidthScale = static_cast<float>(std::clamp(
                m_features.hypixelPanelScale, 70, 160)) / 100.0F;
            const float panelHeightScale = static_cast<float>(std::clamp(
                m_features.hypixelPanelHeight, 60, 400)) / 100.0F;
            const float panelWidth = std::clamp(
                448.0F * panelWidthScale, 420.0F,
                std::max(420.0F, io.DisplaySize.x * 0.92F));
            const float panelHeight = std::clamp(
                230.0F * panelHeightScale, 138.0F,
                std::max(138.0F, io.DisplaySize.y - 8.0F));
            const float defaultPanelX = std::max(6.0F,
                io.DisplaySize.x - panelWidth - 12.0F);
            const float storedPanelX = m_features.hypixelPanelX < 0
                ? defaultPanelX
                : io.DisplaySize.x * static_cast<float>(m_features.hypixelPanelX) / 1000.0F;
            const float storedPanelY = m_features.hypixelPanelY < 0
                ? 12.0F
                : io.DisplaySize.y * static_cast<float>(m_features.hypixelPanelY) / 1000.0F;
            const float targetPanelX = std::clamp(storedPanelX, 4.0F,
                std::max(4.0F, io.DisplaySize.x - panelWidth - 4.0F));
            const float targetPanelY = std::clamp(storedPanelY, 4.0F,
                std::max(4.0F, io.DisplaySize.y - panelHeight - 4.0F));
            ImGui::SetNextWindowPos(ImVec2(targetPanelX, targetPanelY),
                                    ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panelEase);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                ImVec2(0.0F, 0.0F));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGuiWindowFlags boardFlags = ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;
            if (!interactive) boardFlags |= ImGuiWindowFlags_NoInputs;
            ImDrawList* statsCardDraw = nullptr;
            ImDrawList* statsRowsDraw = nullptr;
            int statsCardVertexStart = 0;
            int statsCardVertexEnd = 0;
            int statsRowsVertexStart = 0;
            int statsRowsVertexEnd = 0;
            if (ImGui::Begin("##LiveBedWarsPlayers", nullptr, boardFlags)) {
                ImDrawList* const cardDraw = ImGui::GetWindowDrawList();
                statsCardDraw = cardDraw;
                statsCardVertexStart = cardDraw->VtxBuffer.Size;
                const ImVec2 cardMin = ImGui::GetWindowPos();
                const ImVec2 cardMax(cardMin.x + ImGui::GetWindowWidth(),
                                     cardMin.y + ImGui::GetWindowHeight());
                cardDraw->AddRectFilled(
                    ImVec2(cardMin.x + 3.0F, cardMin.y + 5.0F),
                    ImVec2(cardMax.x + 3.0F, cardMax.y + 5.0F),
                    IM_COL32(0, 0, 0, 72), 12.0F);
                const int statsPanelAlpha = static_cast<int>(std::lround(
                    std::clamp(m_features.hypixelPanelOpacity, 0, 100) * 2.55));
                const bool whitePanel = m_features.hypixelPanelColor == 0xFFFFFFU;
                const ImU32 primaryText = whitePanel
                    ? IM_COL32(14, 14, 18, 255) : IM_COL32(248, 248, 250, 255);
                const ImU32 secondaryText = whitePanel
                    ? IM_COL32(55, 55, 62, 255) : IM_COL32(210, 210, 218, 255);
                if (statsPanelAlpha < 250) {
                    captureBackdropTexture();
                    if (m_blurTexture != 0U) {
                        // 13-tap separable-Gaussian approximation. The former
                        // five equal acrylic samples preserved hard edges;
                        // weighted centre/axis/diagonal taps produce a softer
                        // Gaussian backdrop without allocating another FBO.
                        struct BlurTap final { ImVec2 offset; int alpha; };
                        constexpr std::array<BlurTap, 13U> blurTaps{{
                            {ImVec2(0, 0), 54},
                            {ImVec2(-2, 0), 40}, {ImVec2(2, 0), 40},
                            {ImVec2(0, -2), 40}, {ImVec2(0, 2), 40},
                            {ImVec2(-2, -2), 26}, {ImVec2(2, -2), 26},
                            {ImVec2(-2, 2), 26}, {ImVec2(2, 2), 26},
                            {ImVec2(-5, 0), 17}, {ImVec2(5, 0), 17},
                            {ImVec2(0, -5), 17}, {ImVec2(0, 5), 17}}};
                        const float exitBlurSpread = 1.0F +
                            (1.0F - panelLinear) * 2.8F;
                        for (const BlurTap& tap : blurTaps) {
                            const float left = std::clamp(cardMin.x +
                                                          tap.offset.x * exitBlurSpread,
                                                          0.0F, io.DisplaySize.x);
                            const float top = std::clamp(cardMin.y +
                                                         tap.offset.y * exitBlurSpread,
                                                         0.0F, io.DisplaySize.y);
                            const float right = std::clamp(cardMax.x +
                                                           tap.offset.x * exitBlurSpread,
                                                           0.0F, io.DisplaySize.x);
                            const float bottom = std::clamp(cardMax.y +
                                                            tap.offset.y * exitBlurSpread,
                                                            0.0F, io.DisplaySize.y);
                            cardDraw->AddImageRounded(
                                reinterpret_cast<ImTextureID>(
                                    static_cast<std::uintptr_t>(m_blurTexture)),
                                cardMin, cardMax,
                                ImVec2(left / io.DisplaySize.x,
                                       1.0F - top / io.DisplaySize.y),
                                ImVec2(right / io.DisplaySize.x,
                                       1.0F - bottom / io.DisplaySize.y),
                                IM_COL32(255, 255, 255, static_cast<int>(
                                    std::lround(static_cast<float>(tap.alpha) * panelEase))),
                                12.0F);
                        }
                    }
                }
                cardDraw->AddRectFilled(cardMin, cardMax,
                    packedRgbColor(whitePanel ? 0xFFFFFFU : 0x000000U, statsPanelAlpha),
                    12.0F);
                cardDraw->AddRect(cardMin, cardMax,
                    whitePanel ? IM_COL32(0, 0, 0, 52) : IM_COL32(255, 255, 255, 48),
                    12.0F, 0, 1.0F);
                const int statsRailAlpha = static_cast<int>(std::lround(
                    std::clamp(m_features.hypixelRailOpacity, 0, 100) * 2.55));
                cardDraw->AddRectFilled(
                    cardMin, ImVec2(cardMin.x + 24.0F, cardMax.y),
                    packedRgbColor(m_features.hypixelRailColor, statsRailAlpha),
                    12.0F,
                    ImDrawFlags_RoundCornersLeft);
                const std::size_t statsFontIndex = static_cast<std::size_t>(
                    std::clamp(m_features.hypixelPanelFontIndex, 0, 3));
                ImFont* const panelFont = m_fonts[statsFontIndex] != nullptr
                    ? m_fonts[statsFontIndex] : ImGui::GetFont();
                ImFont* const panelBold = m_boldFonts[statsFontIndex] != nullptr
                    ? m_boldFonts[statsFontIndex] : panelFont;
                // Independent geometry: resizing the card never stretches the
                // rasterized font or row pitch. This keeps every row legible at
                // all GUI size presets and prevents baseline overlap.
                const float panelFontSize = panelFont->LegacySize;
                const float panelBoldSize = panelBold->LegacySize;
                const float rowHeight = std::ceil(std::max(20.0F,
                                                          panelFontSize + 5.0F));
                constexpr float railWidth = 24.0F;
                const float headerHeight = std::ceil(panelBoldSize + 11.0F);
                if (interactive && panelLinear > 0.985F) {
                    const ImVec2 resizeMin(cardMax.x - 22.0F,
                                           cardMax.y - 22.0F);
                    const bool resizeHovered = ImGui::IsMouseHoveringRect(
                        resizeMin, cardMax, false);
                    const bool headerHovered = ImGui::IsMouseHoveringRect(
                        cardMin,
                        ImVec2(cardMax.x - 22.0F,
                               cardMin.y + headerHeight), false);
                    const bool railHovered = ImGui::IsMouseHoveringRect(
                        cardMin,
                        ImVec2(cardMin.x + railWidth, cardMax.y), false);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (resizeHovered) {
                            m_statsPanelResizing = true;
                            m_statsPanelDragging = false;
                            m_statsPanelResizeStartX = io.MousePos.x;
                            m_statsPanelResizeStartY = io.MousePos.y;
                            m_statsPanelResizeStartScale = m_features.hypixelPanelScale;
                            m_statsPanelResizeStartHeight = m_features.hypixelPanelHeight;
                        } else if (headerHovered || railHovered) {
                            m_statsPanelDragging = true;
                            m_statsPanelResizing = false;
                            m_statsPanelDragStartMouseX = io.MousePos.x;
                            m_statsPanelDragStartMouseY = io.MousePos.y;
                            m_statsPanelDragStartPanelX = targetPanelX;
                            m_statsPanelDragStartPanelY = targetPanelY;
                        }
                    }
                    if (m_statsPanelResizing && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        const float dragX = io.MousePos.x - m_statsPanelResizeStartX;
                        const float dragY = io.MousePos.y - m_statsPanelResizeStartY;
                        const int resizedWidth = std::clamp(
                            m_statsPanelResizeStartScale + static_cast<int>(std::lround(
                                dragX * 100.0F / 448.0F)), 70, 160);
                        const int resizedHeight = std::clamp(
                            m_statsPanelResizeStartHeight + static_cast<int>(std::lround(
                                dragY * 100.0F / 230.0F)), 60, 400);
                        if (resizedWidth != m_features.hypixelPanelScale ||
                            resizedHeight != m_features.hypixelPanelHeight) {
                            m_features.hypixelPanelScale = resizedWidth;
                            m_features.hypixelPanelHeight = resizedHeight;
                            m_statsPanelTransformDirty = true;
                        }
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    } else if (m_statsPanelDragging &&
                               ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        // Calculate from the immutable press origin. Accumulating
                        // MouseDelta into a normalized/rounded position caused
                        // quantization feedback, lag and cursor-shape flicker.
                        const float movedX = m_statsPanelDragStartPanelX +
                            (io.MousePos.x - m_statsPanelDragStartMouseX);
                        const float movedY = m_statsPanelDragStartPanelY +
                            (io.MousePos.y - m_statsPanelDragStartMouseY);
                        m_features.hypixelPanelX = std::clamp(static_cast<int>(std::lround(
                            movedX / std::max(1.0F, io.DisplaySize.x) * 1000.0F)), 0, 1000);
                        m_features.hypixelPanelY = std::clamp(static_cast<int>(std::lround(
                            movedY / std::max(1.0F, io.DisplaySize.y) * 1000.0F)), 0, 1000);
                        m_statsPanelTransformDirty = true;
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    } else if (resizeHovered) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    } else if (headerHovered || railHovered) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    }
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                        (m_statsPanelDragging || m_statsPanelResizing)) {
                        if (m_statsPanelTransformDirty) m_featureSettingsDirty = true;
                        m_statsPanelTransformDirty = false;
                        m_statsPanelDragging = false;
                        m_statsPanelResizing = false;
                    }
                }
                constexpr std::array<char, 5U> verticalLabel{'S', 'T', 'A', 'T', 'S'};
                const float labelAdvance = std::max(13.0F, panelBoldSize * 0.84F);
                const float labelHeight = panelBoldSize +
                    labelAdvance * static_cast<float>(verticalLabel.size() - 1U);
                const float labelStartY = cardMin.y +
                    std::max(0.0F, (panelHeight - labelHeight) * 0.5F);
                for (std::size_t letter = 0U; letter < verticalLabel.size(); ++letter) {
                    const char text[2]{verticalLabel[letter], '\0'};
                    const ImVec2 textSize = panelBold->CalcTextSizeA(
                        panelBoldSize, FLT_MAX, 0.0F, text);
                    cardDraw->AddText(panelBold, panelBoldSize,
                        ImVec2(cardMin.x + (railWidth - textSize.x) * 0.5F,
                               labelStartY + static_cast<float>(letter) * labelAdvance),
                        contrastingTextColor(m_features.hypixelRailColor), text);
                }
                const ImVec2 columnHeader(cardMin.x + railWidth + 4.0F,
                                          cardMin.y + 6.0F);
                const float contentWidth = std::max(360.0F, panelWidth - railWidth - 8.0F);
                const std::array<float, 8U> columns{{
                    4.0F, contentWidth * 0.335F, contentWidth * 0.445F,
                    contentWidth * 0.555F, contentWidth * 0.655F,
                    contentWidth * 0.755F, contentWidth * 0.865F,
                    contentWidth * 0.955F}};
                const ImU32 headerColor = primaryText;
                const auto header = [&](const float x, const char* text) noexcept {
                    cardDraw->AddText(panelBold, panelBoldSize,
                        ImVec2(columnHeader.x + x, columnHeader.y),
                        headerColor, text);
                };
                constexpr std::array<const char*, 8U> headers{{
                    "PLAYER", "STAR", "FKDR", "WLR", "BBLR", "FINALS", "WINS", "WS"}};
                for (std::size_t column = 0U; column < headers.size(); ++column)
                    header(columns[column], headers[column]);
                ImGui::SetCursorScreenPos(ImVec2(columnHeader.x,
                                                  cardMin.y + headerHeight));
                std::array<std::uint32_t, GameSnapshot::MaxDiscoveredPlayers> order{};
                for (std::uint32_t index = 0U; index < snapshot.playerCount; ++index) {
                    order[index] = index;
                }
                const auto teamRank = [](const char code) noexcept {
                    constexpr std::array<char, 8U> orderCodes{
                        'c', '9', 'a', 'e', 'b', 'f', 'd', '7'};
                    const auto found = std::find(orderCodes.begin(), orderCodes.end(), code);
                    return found == orderCodes.end()
                        ? 8 : static_cast<int>(found - orderCodes.begin());
                };
                const auto teamLetter = [](const char code) noexcept {
                    switch (code) {
                    case 'c': return 'R';
                    case '9': return 'B';
                    case 'a': return 'G';
                    case 'e': return 'Y';
                    case 'b': return 'A';
                    case 'f': return 'W';
                    case 'd': return 'P';
                    case '7': return 'S';
                    default: return '?';
                    }
                };
                std::sort(order.begin(), order.begin() + snapshot.playerCount,
                          [&](const std::uint32_t first, const std::uint32_t second) noexcept {
                              const PlayerIdentity& a = snapshot.players[first];
                              const PlayerIdentity& b = snapshot.players[second];
                              const int ar = teamRank(a.teamColor);
                              const int br = teamRank(b.teamColor);
                              return ar != br ? ar < br
                                  : std::strcmp(a.name.data(), b.name.data()) < 0;
                          });
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
                ImGui::BeginChild("##liveRosterRows",
                    ImVec2(panelWidth - railWidth - 8.0F,
                           panelHeight - headerHeight - 4.0F),
                    false, ImGuiWindowFlags_NoBackground);
                statsRowsDraw = ImGui::GetWindowDrawList();
                statsRowsVertexStart = statsRowsDraw != nullptr
                    ? statsRowsDraw->VtxBuffer.Size : 0;
                for (std::uint32_t ordered = 0U; ordered < snapshot.playerCount; ++ordered) {
                    const PlayerIdentity& identity = snapshot.players[order[ordered]];
                    const char code = identity.teamColor;
                    const PlayerStatsEntry* stats = nullptr;
                    for (std::uint32_t statsIndex = 0U;
                         statsIndex < m_playerStats.count; ++statsIndex) {
                        if (std::strcmp(m_playerStats.entries[statsIndex].name.data(),
                                        identity.name.data()) == 0) {
                            stats = &m_playerStats.entries[statsIndex];
                            break;
                        }
                    }
                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const ImVec2 size(ImGui::GetContentRegionAvail().x,
                                      rowHeight);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const int rowAlpha = (ordered & 1U) != 0U ? 18 : 10;
                    drawList->AddRectFilled(pos,
                        ImVec2(pos.x + size.x, pos.y + size.y),
                        whitePanel ? IM_COL32(0, 0, 0, rowAlpha)
                                   : IM_COL32(255, 255, 255, rowAlpha),
                        0.0F);
                    const float textY = pos.y + (rowHeight - panelFontSize) * 0.5F;
                    const char teamTag[4]{'[', teamLetter(code), ']', '\0'};
                    drawList->AddText(panelBold, panelBoldSize,
                        ImVec2(pos.x + columns[0U], textY),
                        ImGui::ColorConvertFloat4ToU32(teamColor(code)), teamTag);
                    const ImVec4 nameClip(
                        pos.x + 30.0F, pos.y,
                        pos.x + columns[1U] - 5.0F, pos.y + size.y);
                    drawList->AddText(panelFont, panelFontSize,
                        ImVec2(pos.x + 30.0F, textY), primaryText,
                        identity.name.data(), nullptr, 0.0F, &nameClip);
                    if (stats == nullptr) {
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[1U], textY),
                            secondaryText, "Querying...");
                    } else if (stats->failed) {
                        const bool suspectedNick = std::strcmp(
                            stats->status.data(), "unavailable") == 0;
                        drawList->AddText(panelBold, panelBoldSize,
                            ImVec2(pos.x + columns[1U], textY),
                            suspectedNick ? IM_COL32(224, 159, 255, 255)
                                          : IM_COL32(255, 116, 127, 255),
                            suspectedNick ? "SUSPECTED NICK" : "UNAVAILABLE");
                    } else {
                        char starsText[24]{};
                        char fkdrText[16]{};
                        char wlrText[16]{};
                        char bblrText[16]{};
                        char finalsText[16]{};
                        char winsText[16]{};
                        char winStreakText[12]{};
                        const PrestigeStyle prestige = bedWarsPrestigeStyle(stats->stars);
                        std::snprintf(starsText, sizeof(starsText), "%d", stats->stars);
                        std::snprintf(fkdrText, sizeof(fkdrText), "%.2f", stats->fkdr);
                        std::snprintf(wlrText, sizeof(wlrText), "%.2f", stats->wlr);
                        std::snprintf(bblrText, sizeof(bblrText), "%.2f", stats->bblr);
                        formatCompactCount(finalsText, sizeof(finalsText), stats->finalKills);
                        formatCompactCount(winsText, sizeof(winsText), stats->wins);
                        std::snprintf(winStreakText, sizeof(winStreakText), "%d", stats->winStreak);
                        const ImU32 fkdrColor = stats->fkdr >= 3.0
                            ? IM_COL32(89, 235, 122, 255)
                            : (stats->fkdr >= 1.5 ? IM_COL32(255, 214, 82, 255)
                                                  : IM_COL32(255, 97, 107, 255));
                        drawList->AddText(panelBold, panelBoldSize,
                            ImVec2(pos.x + columns[1U], textY),
                            prestige.color, starsText);
                        const ImVec2 starNumberSize = panelBold->CalcTextSizeA(
                            panelBoldSize, FLT_MAX, 0.0F, starsText);
                        drawPrestigeStar(drawList,
                            ImVec2(pos.x + columns[1U] + starNumberSize.x + 6.0F,
                                   pos.y + size.y * 0.5F),
                            4.1F, prestige.color, prestige.master);
                        drawList->AddText(panelBold, panelBoldSize,
                            ImVec2(pos.x + columns[2U], textY), fkdrColor, fkdrText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[3U], textY),
                            stats->wlr >= 1.0 ? IM_COL32(57, 190, 112, 255) : secondaryText,
                            wlrText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[4U], textY),
                            stats->bblr >= 1.5 ? IM_COL32(54, 164, 219, 255) : secondaryText,
                            bblrText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[5U], textY),
                            stats->finalKills >= 5000 ? IM_COL32(224, 151, 35, 255) : secondaryText,
                            finalsText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[6U], textY), secondaryText, winsText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[7U], textY),
                            stats->winStreak >= 10 ? IM_COL32(238, 83, 70, 255) : secondaryText,
                            winStreakText);
                    }
                    ImGui::Dummy(size);
                }
                statsRowsVertexEnd = statsRowsDraw != nullptr
                    ? statsRowsDraw->VtxBuffer.Size : statsRowsVertexStart;
                ImGui::EndChild();
                ImGui::PopStyleVar();
                if (interactive) {
                    const ImU32 gripColor = whitePanel
                        ? IM_COL32(20, 20, 24, 155) : IM_COL32(255, 255, 255, 160);
                    for (int gripLine = 0; gripLine < 3; ++gripLine) {
                        const float inset = 4.0F + static_cast<float>(gripLine) * 4.0F;
                        cardDraw->AddLine(
                            ImVec2(cardMax.x - inset, cardMax.y - 2.0F),
                            ImVec2(cardMax.x - 2.0F, cardMax.y - inset),
                            gripColor, 1.0F);
                    }
                }
                statsCardVertexEnd = cardDraw->VtxBuffer.Size;
            }
            ImGui::End();
            ImGui::PopStyleVar(3);

            // Use the same whole-surface spotlight transform as the Click GUI.
            // Geometry is laid out at its final coordinates and all vertices
            // gather/scatter around the panel centre as one flat layer.
            const ImVec2 statsCenter(targetPanelX + panelWidth * 0.5F,
                                     targetPanelY + panelHeight * 0.5F);
            const auto transformStats = [&](ImDrawList* const drawList,
                                            int begin, int end) noexcept {
                if (drawList == nullptr) return;
                begin = std::clamp(begin, 0, drawList->VtxBuffer.Size);
                end = std::clamp(end, begin, drawList->VtxBuffer.Size);
                for (int vertexIndex = begin; vertexIndex < end; ++vertexIndex) {
                    ImDrawVert& vertex = drawList->VtxBuffer[vertexIndex];
                    if (std::abs(panelPresentationScale - 1.0F) >= 0.0001F) {
                        vertex.pos.x = statsCenter.x +
                            (vertex.pos.x - statsCenter.x) * panelPresentationScale;
                        vertex.pos.y = statsCenter.y +
                            (vertex.pos.y - statsCenter.y) * panelPresentationScale;
                    }
                    const unsigned alpha = static_cast<unsigned>(vertex.col >> 24U);
                    const unsigned faded = static_cast<unsigned>(std::clamp(
                        std::lround(static_cast<float>(alpha) * panelEase),
                        0L, 255L));
                    vertex.col = (vertex.col & 0x00FFFFFFU) | (faded << 24U);
                }
                if (std::abs(panelPresentationScale - 1.0F) >= 0.0001F) {
                    for (ImDrawCmd& command : drawList->CmdBuffer) {
                        command.ClipRect.x = statsCenter.x +
                            (command.ClipRect.x - statsCenter.x) * panelPresentationScale;
                        command.ClipRect.y = statsCenter.y +
                            (command.ClipRect.y - statsCenter.y) * panelPresentationScale;
                        command.ClipRect.z = statsCenter.x +
                            (command.ClipRect.z - statsCenter.x) * panelPresentationScale;
                        command.ClipRect.w = statsCenter.y +
                            (command.ClipRect.w - statsCenter.y) * panelPresentationScale;
                    }
                }
            };
            transformStats(statsCardDraw, statsCardVertexStart,
                           statsCardVertexEnd);
            if (statsRowsDraw != statsCardDraw)
                transformStats(statsRowsDraw, statsRowsVertexStart,
                               statsRowsVertexEnd);
    }

}

} // namespace mcoverlay
