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

void OverlayRenderer::renderImeOverlay(const float deltaSeconds,
                                       const float uiScale) noexcept
{
    if (m_inputState == nullptr) return;
    std::array<wchar_t, 80U> name{};
    std::array<wchar_t, 128U> composition{};
    std::array<std::array<wchar_t, 64U>, 9U> candidates{};
    std::uint32_t candidateCount = 0U;
    std::uint32_t candidateSelection = 0U;
    bool composing = false;
    ::AcquireSRWLockShared(&m_inputState->imeLock);
    name = m_inputState->imeName;
    composition = m_inputState->imeComposition;
    candidates = m_inputState->imeCandidates;
    candidateCount = m_inputState->imeCandidateCount;
    candidateSelection = m_inputState->imeCandidateSelection;
    composing = m_inputState->imeComposing;
    ::ReleaseSRWLockShared(&m_inputState->imeLock);
    if (m_inputState->tsf != nullptr) {
        const ImeCandidates tsf = m_inputState->tsf->snapshot();
        if (tsf.active && tsf.count > 0U) {
            candidates = tsf.words;
            candidateCount = tsf.count;
            candidateSelection = tsf.selected;
            composing = true;
        }
    }
    if (m_imePositionEditing) {
        std::wcscpy(name.data(), L"输入法面板 · 拖动调整位置");
        std::wcscpy(composition.data(), L"ni'hao");
        std::wcscpy(candidates[0].data(), L"你好");
        std::wcscpy(candidates[1].data(), L"你号");
        std::wcscpy(candidates[2].data(), L"拟好");
        candidateCount = 3;
        candidateSelection = 0;
    }

    const std::uint64_t revision = m_inputState->imeRevision.load(
        std::memory_order_acquire);
    const std::uint64_t now = ::GetTickCount64();
    if (revision != m_lastImeRevision) {
        m_lastImeRevision = revision;
        m_lastImeActivityTick = now;
    }
    const bool recentlyChanged = m_lastImeActivityTick != 0U &&
        now - m_lastImeActivityTick < 2200U;
    const bool visible = m_imePositionEditing || (m_features.fullscreenImeFixEnabled &&
        (composing || recentlyChanged));
    const float target = visible ? 1.0F : 0.0F;
    m_imePanelProgress += (target - m_imePanelProgress) *
        (1.0F - std::exp(-12.0F * std::clamp(deltaSeconds, 0.0F, 0.05F)));
    if (m_imePanelProgress < 0.002F) return;

    const auto toUtf8 = [](const wchar_t* const source,
                           char* const destination,
                           const int capacity) noexcept {
        destination[0] = '\0';
        if (source == nullptr || source[0] == L'\0') return;
        (void)::WideCharToMultiByte(CP_UTF8, 0, source, -1,
            destination, capacity, nullptr, nullptr);
        destination[capacity - 1] = '\0';
    };
    std::array<char, 240U> nameUtf8{};
    std::array<char, 384U> compositionUtf8{};
    std::array<std::array<char, 192U>, 9U> candidateUtf8{};
    toUtf8(name.data(), nameUtf8.data(), static_cast<int>(nameUtf8.size()));
    toUtf8(composition.data(), compositionUtf8.data(),
           static_cast<int>(compositionUtf8.size()));
    for (std::size_t index = 0U; index < candidateUtf8.size(); ++index) {
        toUtf8(candidates[index].data(), candidateUtf8[index].data(),
               static_cast<int>(candidateUtf8[index].size()));
    }

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* const draw = ImGui::GetForegroundDrawList();
    const float eased = m_imePanelProgress * m_imePanelProgress *
        (3.0F - 2.0F * m_imePanelProgress);
    const float width = std::min(660.0F * uiScale,
                                 io.DisplaySize.x - 24.0F * uiScale);
    const float rowHeight = candidateCount == 0U ? 0.0F : 35.0F * uiScale;
    const float height = (compositionUtf8[0U] != '\0' ? 92.0F : 66.0F) *
        uiScale + rowHeight;
    const float availableX = std::max(1.0F, io.DisplaySize.x - width - 12.0F);
    const float availableY = std::max(1.0F, io.DisplaySize.y - height - 70.0F * uiScale);
    const float targetX = m_features.imePanelX < 0 ? (io.DisplaySize.x - width) * 0.5F
        : 6.0F + availableX * static_cast<float>(m_features.imePanelX) / 255.0F;
    const float targetY = m_features.imePanelY < 0 ? 18.0F * uiScale
        : 6.0F + availableY * static_cast<float>(m_features.imePanelY) / 255.0F;
    ImVec2 minimum(targetX, targetY - 24.0F * uiScale * (1.0F - eased));
    if (m_imePositionEditing) {
        if (!m_imeDragging) { m_imeEditX = targetX; m_imeEditY = targetY; }
        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(
                minimum, ImVec2(minimum.x + width, minimum.y + height), false))
            m_imeDragging = true;
        if (!ImGui::IsMouseDown(0)) m_imeDragging = false;
        if (m_imeDragging) {
            m_imeEditX = std::clamp(m_imeEditX + io.MouseDelta.x, 6.0F, 6.0F + availableX);
            m_imeEditY = std::clamp(m_imeEditY + io.MouseDelta.y, 6.0F, 6.0F + availableY);
            m_features.imePanelX = static_cast<int>(std::lround((m_imeEditX - 6.0F) / availableX * 255.0F));
            m_features.imePanelY = static_cast<int>(std::lround((m_imeEditY - 6.0F) / availableY * 255.0F));
        }
        minimum = ImVec2(m_imeEditX, m_imeEditY);
        ImGui::SetNextWindowPos(ImVec2(minimum.x, minimum.y + height + 8.0F * uiScale));
        ImGui::SetNextWindowSize(ImVec2(width, 44.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F * uiScale, 6.0F * uiScale));
        ImGui::Begin("##ImePositionTools", nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
        if (ImGui::Button("Done / Save position", ImVec2(210.0F * uiScale, 32.0F * uiScale))) {
            m_imePositionEditing = false;
            m_imeDragging = false;
            m_featureSettingsDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset position", ImVec2(150.0F * uiScale, 32.0F * uiScale))) {
            m_features.imePanelX = m_features.imePanelY = -1;
            m_featureSettingsDirty = true;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    const ImVec2 maximum(minimum.x + width, minimum.y + height);
    const int alpha = static_cast<int>(238.0F * eased);
    draw->AddRectFilled(ImVec2(minimum.x - 5.0F * uiScale,
                              minimum.y + 8.0F * uiScale),
                        ImVec2(maximum.x + 5.0F * uiScale,
                              maximum.y + 12.0F * uiScale),
                        IM_COL32(0, 0, 0, static_cast<int>(54.0F * eased)),
                        20.0F * uiScale);
    draw->AddRectFilled(minimum, maximum,
        IM_COL32(20, 21, 28, alpha), 18.0F * uiScale);
    draw->AddRectFilled(minimum,
        ImVec2(minimum.x + 5.0F * uiScale, maximum.y),
        IM_COL32(114, 224, 210, static_cast<int>(255.0F * eased)),
        18.0F * uiScale, ImDrawFlags_RoundCornersLeft);
    ImFont* const font = m_imeFont != nullptr ? m_imeFont : ImGui::GetFont();
    const float fontSize = (m_imeFont != nullptr ? 22.0F : ImGui::GetFontSize()) *
        uiScale;
    draw->AddText(font, fontSize * 0.70F,
        ImVec2(minimum.x + 22.0F * uiScale, minimum.y + 12.0F * uiScale),
        IM_COL32(174, 180, 194, static_cast<int>(255.0F * eased)),
        nameUtf8[0U] != '\0' ? nameUtf8.data() : "Input method");
    if (compositionUtf8[0U] != '\0') {
        draw->AddText(font, fontSize,
            ImVec2(minimum.x + 22.0F * uiScale,
                   minimum.y + 34.0F * uiScale),
            IM_COL32(248, 249, 252, static_cast<int>(255.0F * eased)),
            compositionUtf8.data());
    }
    if (candidateCount != 0U) {
        const float top = maximum.y - rowHeight;
        const float cellWidth = (width - 28.0F * uiScale) /
            static_cast<float>(candidateCount);
        for (std::uint32_t index = 0U; index < candidateCount; ++index) {
            const float left = minimum.x + 14.0F * uiScale +
                cellWidth * static_cast<float>(index);
            if (index == candidateSelection) {
                draw->AddRectFilled(ImVec2(left, top + 2.0F * uiScale),
                    ImVec2(left + cellWidth - 4.0F * uiScale,
                           maximum.y - 5.0F * uiScale),
                    IM_COL32(55, 184, 170, static_cast<int>(105.0F * eased)),
                    9.0F * uiScale);
            }
            char numbered[224]{};
            std::snprintf(numbered, sizeof(numbered), "%u %s",
                index + 1U, candidateUtf8[index].data());
            draw->PushClipRect(ImVec2(left, top),
                ImVec2(left + cellWidth - 4.0F * uiScale, maximum.y), true);
            draw->AddText(font, fontSize * 0.76F,
                ImVec2(left + 7.0F * uiScale, top + 8.0F * uiScale),
                IM_COL32(236, 239, 244, static_cast<int>(255.0F * eased)),
                numbered);
            draw->PopClipRect();
        }
    }
}


} // namespace mcoverlay
