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

void OverlayRenderer::renderBlacklistAddDialog(RenderFrameContext& frame) noexcept
{
    const auto& snapshot = frame.snapshot;
    const auto& interactive = frame.interactive;
    auto& io = frame.io;
    const auto& uiScale = frame.uiScale;
    const auto& delta = frame.delta;
    const auto& theme = frame.theme;
    const auto& guiSurface = frame.guiSurface;
    const auto& guiRail = frame.guiRail;
    const auto& guiText = frame.guiText;
    const auto& guiMuted = frame.guiMuted;
    const auto& guiFrame = frame.guiFrame;
    const auto& guiScrollbarTrack = frame.guiScrollbarTrack;
    const auto& guiScrollbarGrab = frame.guiScrollbarGrab;
    const auto& guiAccent = frame.guiAccent;
    const auto& guiSelected = frame.guiSelected;

    // longer expands the Blacklist settings page and therefore cannot disturb
    // that page's scroll position or make the form feel visually mixed with
    // persistent settings.
    if (!interactive) m_blacklistAddOpen = false;
    advancePresentationSpring(m_blacklistAddProgress,
                              m_blacklistAddVelocity,
                              m_blacklistAddOpen ? 1.0F : 0.0F, delta);
    if (m_blacklistAddProgress > 0.005F) {
        const float modalProgress = std::clamp(m_blacklistAddProgress, 0.0F, 1.0F);
        const float modalEase = modalProgress * modalProgress *
                                (3.0F - 2.0F * modalProgress);
        const float modalScale = 1.12F - 0.12F * m_blacklistAddProgress;
        const ImVec2 modalSize(480.0F * uiScale,
            std::min(480.0F*uiScale,std::max(240.0F,io.DisplaySize.y-12.0F)));
        const ImVec2 modalPosition(
            std::max(6.0F, (io.DisplaySize.x - modalSize.x) * 0.5F),
            std::max(6.0F, (io.DisplaySize.y - modalSize.y) * 0.5F));
        ImVec2 modalCenter(modalPosition.x + modalSize.x * 0.5F,
                                 modalPosition.y + modalSize.y * 0.5F);
        // Independent floating editor, not a page child or fullscreen blocker.
        ImGui::SetNextWindowPos(modalPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(22.0F * uiScale, 20.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0F * uiScale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,ImVec2(0.5F,0.5F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, guiSurface);
        ImGui::PushStyleColor(ImGuiCol_Text, guiText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, guiMuted);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, guiFrame);
        ImGui::PushStyleColor(ImGuiCol_Button, guiFrame);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            mixColor(guiFrame,guiAccent,0.16F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, guiSelected);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, guiAccent);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,mixColor(guiFrame,guiAccent,0.12F));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,guiSelected);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,guiScrollbarTrack);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,guiScrollbarGrab);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,guiAccent);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,guiAccent);
        ImGuiWindowFlags modalFlags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
        if (!interactive || modalProgress < 0.985F)
            modalFlags |= ImGuiWindowFlags_NoInputs;
        ImDrawList* modalDraw = nullptr;
        ImGuiWindow* modalRoot = nullptr;
        int modalVertexEnd = 0;
        if (ImGui::Begin("##BlacklistAddDialog", nullptr, modalFlags)) {
            modalDraw = ImGui::GetWindowDrawList();
            modalRoot=ImGui::GetCurrentWindow();
            modalCenter=ImVec2(modalRoot->Pos.x+modalSize.x*.5F,
                               modalRoot->Pos.y+modalSize.y*.5F);
            ImFont* const modalBold = m_boldFonts[static_cast<std::size_t>(
                std::clamp(m_guiScaleIndex, 0, 3))] != nullptr
                ? m_boldFonts[static_cast<std::size_t>(
                    std::clamp(m_guiScaleIndex, 0, 3))] : ImGui::GetFont();
            ImGui::PushFont(modalBold);
            ImGui::TextUnformatted("Add to blacklist");
            ImGui::PopFont();
            ImGui::SameLine(modalSize.x - 63.0F * uiScale);
            if (ImGui::Button("X", ImVec2(30.0F * uiScale,
                                           28.0F * uiScale)))
                m_blacklistAddOpen = false;
            ImGui::TextDisabled("Create a clear record from a player observed in this session.");
            ImGui::Dummy(ImVec2(1.0F,8.0F*uiScale));
            ImGui::PushFont(modalBold);
            ImGui::TextColored(guiAccent,"RECENT PLAYER");
            ImGui::PopFont();
            const char* preview = "Select a recent player";
            if (m_blacklistSelectedPlayer >= 0 &&
                static_cast<std::uint32_t>(m_blacklistSelectedPlayer) <
                    snapshot.playerCount) {
                preview = snapshot.players[static_cast<std::size_t>(
                    m_blacklistSelectedPlayer)].name.data();
            }
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize,1.0F*uiScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.0F*uiScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                ImVec2(7.0F*uiScale,7.0F*uiScale));
            ImGui::PushStyleColor(ImGuiCol_PopupBg,guiRail);
            ImGui::PushStyleColor(ImGuiCol_Border,
                ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.64F));
            ImGui::PushStyleColor(ImGuiCol_Header,
                ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.28F));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.48F));
            ImGui::PushStyleColor(ImGuiCol_FrameBg,mixColor(guiFrame,guiAccent,0.10F));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                mixColor(guiFrame,guiAccent,0.18F));
            if (ImGui::BeginCombo("##blacklistRecentModal", preview)) {
                for (std::uint32_t index = 0U; index < snapshot.playerCount; ++index) {
                    const PlayerIdentity& identity = snapshot.players[index];
                    const bool selected = static_cast<int>(index) ==
                                          m_blacklistSelectedPlayer;
                    char label[72]{};
                    std::snprintf(label, sizeof(label), "%s%s",
                        identity.name.data(), identity.uuid[0U] == '\0'
                            ? "   NICK / ID ONLY" : "   UUID LINKED");
                    if (ImGui::Selectable(label, selected,
                            ImGuiSelectableFlags_None,ImVec2(0,30.0F*uiScale))) {
                        m_blacklistSelectedPlayer = static_cast<int>(index);
                        m_blacklistIdOnlyNick = identity.uuid[0U] == '\0';
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleColor(6);
            ImGui::PopStyleVar(3);

            const bool selectionValid = m_blacklistSelectedPlayer >= 0 &&
                static_cast<std::uint32_t>(m_blacklistSelectedPlayer) <
                    snapshot.playerCount;
            if(selectionValid) {
                const PlayerIdentity& selected=snapshot.players[
                    static_cast<std::size_t>(m_blacklistSelectedPlayer)];
                const ImVec2 identityMin=ImGui::GetCursorScreenPos();
                const ImVec2 identityMax(identityMin.x+ImGui::GetContentRegionAvail().x,
                    identityMin.y+42.0F*uiScale);
                ImGui::GetWindowDrawList()->AddRectFilled(identityMin,identityMax,
                    ImGui::ColorConvertFloat4ToU32(guiFrame),9.0F*uiScale);
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(identityMin.x+20.0F*uiScale,identityMin.y+21.0F*uiScale),
                    11.0F*uiScale,ImGui::ColorConvertFloat4ToU32(
                        ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.72F)),20);
                ImGui::GetWindowDrawList()->AddText(modalBold,ImGui::GetFontSize(),
                    ImVec2(identityMin.x+39.0F*uiScale,identityMin.y+6.0F*uiScale),
                    ImGui::ColorConvertFloat4ToU32(guiText),selected.name.data());
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(identityMin.x+39.0F*uiScale,identityMin.y+23.0F*uiScale),
                    ImGui::ColorConvertFloat4ToU32(guiMuted),
                    selected.uuid[0U]?"Identity linked by UUID":
                        "No UUID available; this may be a nick");
                ImGui::Dummy(ImVec2(1.0F,47.0F*uiScale));
            } else ImGui::Dummy(ImVec2(1.0F,5.0F*uiScale));

            ImGui::PushFont(modalBold);
            ImGui::TextColored(guiAccent,"NOTE");
            ImGui::PopFont();
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextMultiline("##blacklistReasonModal",
                m_blacklistReasonInput.data(),m_blacklistReasonInput.size(),
                ImVec2(-1.0F,62.0F*uiScale));
            if (m_blacklist.presetCount > 0U) {
                ImGui::TextDisabled("Quick notes");
                for (std::uint32_t index = 0U;
                     index < m_blacklist.presetCount; ++index) {
                    const float buttonWidth=ImGui::CalcTextSize(
                        m_blacklist.presets[index].data()).x+18.0F*uiScale;
                    if(index!=0U && ImGui::GetCursorPosX()+buttonWidth<
                        ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::SmallButton(m_blacklist.presets[index].data()))
                        std::snprintf(m_blacklistReasonInput.data(),
                            m_blacklistReasonInput.size(), "%s",
                            m_blacklist.presets[index].data());
                    ImGui::PopID();
                }
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg,mixColor(guiRail,guiSurface,0.35F));
            ImGui::BeginChild("##blacklistOptions",ImVec2(-1.0F,48.0F*uiScale),
                false,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
            const auto optionCard=[&](const char* label,bool& value,
                                      const float width) noexcept {
                const ImVec2 cardMin=ImGui::GetCursorScreenPos();
                const ImVec2 cardSize(width,38.0F*uiScale);
                ImGui::PushID(label);
                ImGui::InvisibleButton("##option",cardSize);
                if(ImGui::IsItemClicked()) value=!value;
                const bool hovered=ImGui::IsItemHovered();
                ImDrawList* const optionDraw=ImGui::GetWindowDrawList();
                float* optionPhase=ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("phase"),value?1.0F:0.0F);
                *optionPhase=approachExponential(*optionPhase,value?1.0F:0.0F,18.0F,delta);
                const ImVec4 cardColor=mixColor(guiFrame,guiAccent,
                    0.035F+0.165F*(*optionPhase)+(hovered?0.04F:0.0F));
                optionDraw->AddRectFilled(cardMin,
                    ImVec2(cardMin.x+cardSize.x,cardMin.y+cardSize.y),
                    ImGui::ColorConvertFloat4ToU32(cardColor),9.0F*uiScale);
                optionDraw->AddRect(cardMin,
                    ImVec2(cardMin.x+cardSize.x,cardMin.y+cardSize.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(guiAccent.x,
                        guiAccent.y,guiAccent.z,value?0.62F:0.24F)),9.0F*uiScale,
                    0,std::max(1.0F,uiScale));
                const ImVec2 checkMin(cardMin.x+10.0F*uiScale,
                    cardMin.y+11.0F*uiScale);
                const ImVec2 checkMax(checkMin.x+16.0F*uiScale,
                    checkMin.y+16.0F*uiScale);
                optionDraw->AddRectFilled(checkMin,checkMax,
                    ImGui::ColorConvertFloat4ToU32(mixColor(
                        mixColor(guiSurface,guiText,0.10F),guiAccent,*optionPhase)),4.0F*uiScale);
                optionDraw->AddRect(checkMin,checkMax,
                    ImGui::ColorConvertFloat4ToU32(value?guiAccent:
                        ImVec4(guiText.x,guiText.y,guiText.z,0.52F)),4.0F*uiScale,
                    0,std::max(1.0F,uiScale));
                if(*optionPhase>0.01F) {
                    const ImU32 checkColor=ImGui::ColorConvertFloat4ToU32(
                        mixColor(ImVec4(.10F,.09F,.14F,*optionPhase),ImVec4(1,1,1,*optionPhase),theme));
                    optionDraw->AddLine(ImVec2(checkMin.x+3.5F*uiScale,
                        checkMin.y+8.3F*uiScale),ImVec2(checkMin.x+7.0F*uiScale,
                        checkMin.y+12.0F*uiScale),checkColor,1.8F*uiScale);
                    optionDraw->AddLine(ImVec2(checkMin.x+7.0F*uiScale,
                        checkMin.y+12.0F*uiScale),ImVec2(checkMin.x+13.0F*uiScale,
                        checkMin.y+4.3F*uiScale),checkColor,1.8F*uiScale);
                }
                const ImVec2 textSize=ImGui::CalcTextSize(label);
                optionDraw->AddText(ImVec2(checkMax.x+9.0F*uiScale,
                    cardMin.y+(cardSize.y-textSize.y)*0.5F),
                    ImGui::ColorConvertFloat4ToU32(guiText),label);
                ImGui::PopID();
            };
            ImGui::SetCursorPos(ImVec2(5.0F*uiScale,5.0F*uiScale));
            const float optionGap=7.0F*uiScale;
            const float available=ImGui::GetContentRegionAvail().x-5.0F*uiScale;
            const bool twoOptions=m_blacklist.allowIdOnlyNicks;
            const float optionWidth=twoOptions?(available-optionGap)*0.5F:available;
            optionCard("Warn on encounter",m_blacklistWarnOnEncounter,optionWidth);
            if(twoOptions) {
                ImGui::SameLine(0.0F,optionGap);
                optionCard("Allow ID-only nick",m_blacklistIdOnlyNick,optionWidth);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY()+12.0F*uiScale,
                                         modalSize.y-70.0F*uiScale));
            const float footerWidth=ImGui::GetContentRegionAvail().x;
            if (ImGui::Button("Cancel", ImVec2((footerWidth-10.0F*uiScale)*0.38F,
                    34.0F * uiScale)))
                m_blacklistAddOpen = false;
            ImGui::SameLine();
            if (!selectionValid) ImGui::BeginDisabled();
            if (ImGui::Button("Add player",
                    ImVec2((footerWidth-10.0F*uiScale)*0.62F,34.0F*uiScale)) &&
                selectionValid) {
                const PlayerIdentity& identity = snapshot.players[
                    static_cast<std::size_t>(m_blacklistSelectedPlayer)];
                m_blacklistAction = {};
                m_blacklistAction.type = BlacklistAction::Type::Add;
                std::snprintf(m_blacklistAction.name.data(),
                    m_blacklistAction.name.size(), "%s", identity.name.data());
                std::snprintf(m_blacklistAction.uuid.data(),
                    m_blacklistAction.uuid.size(), "%s", identity.uuid.data());
                std::snprintf(m_blacklistAction.reason.data(),
                    m_blacklistAction.reason.size(), "%s",
                    m_blacklistReasonInput[0U] == '\0'
                        ? "No reason supplied" : m_blacklistReasonInput.data());
                m_blacklistAction.idOnlyNick = m_blacklistIdOnlyNick;
                m_blacklistAction.warnOnEncounter = m_blacklistWarnOnEncounter;
                m_blacklistActionDirty = true;
                m_blacklistAddOpen = false;
                m_blacklistSelectedPlayer = -1;
                m_blacklistReasonInput.fill('\0');
            }
            if (!selectionValid) ImGui::EndDisabled();
            // SetCursorPosY deliberately extends to the modal footer. Submit a
            // final bounded item so Dear ImGui can validate the content extent.
            ImGui::Dummy(ImVec2(1.0F, 1.0F));
            modalVertexEnd = modalDraw->VtxBuffer.Size;
        }
        ImGui::End();
        ImGui::PopStyleColor(14);
        ImGui::PopStyleVar(5);
        if (modalDraw != nullptr) {
            modalVertexEnd = std::clamp(modalVertexEnd, 0,
                                        modalDraw->VtxBuffer.Size);
            const bool scaleModal=std::abs(modalScale-1.0F)>0.0001F;
            const float alphaScale=std::clamp(modalEase,0.0F,1.0F);
            constexpr ImU32 alphaMask=static_cast<ImU32>(0xFFU)<<IM_COL32_A_SHIFT;
            const auto transformModalVertices=[&](ImDrawList* const list,
                                                  int begin,int end) noexcept {
                if(list==nullptr) return;
                begin=std::clamp(begin,0,list->VtxBuffer.Size);
                end=std::clamp(end,begin,list->VtxBuffer.Size);
                for(int vertexIndex=begin;vertexIndex<end;++vertexIndex) {
                    ImDrawVert& vertex=list->VtxBuffer[vertexIndex];
                    if(scaleModal) {
                        vertex.pos.x=modalCenter.x+
                            (vertex.pos.x-modalCenter.x)*modalScale;
                        vertex.pos.y=modalCenter.y+
                            (vertex.pos.y-modalCenter.y)*modalScale;
                    }
                    const unsigned sourceAlpha=(vertex.col>>IM_COL32_A_SHIFT)&0xFFU;
                    const unsigned fadedAlpha=static_cast<unsigned>(std::lround(
                        static_cast<float>(sourceAlpha)*alphaScale));
                    vertex.col=(vertex.col&~alphaMask)|
                        ((static_cast<ImU32>(std::min(fadedAlpha,255U)))<<IM_COL32_A_SHIFT);
                }
            };
            const auto transformModalClips=[&](ImDrawList* const list) noexcept {
                if(list==nullptr||!scaleModal) return;
                for(ImDrawCmd& command:list->CmdBuffer) {
                    command.ClipRect.x=modalCenter.x+
                        (command.ClipRect.x-modalCenter.x)*modalScale;
                    command.ClipRect.y=modalCenter.y+
                        (command.ClipRect.y-modalCenter.y)*modalScale;
                    command.ClipRect.z=modalCenter.x+
                        (command.ClipRect.z-modalCenter.x)*modalScale;
                    command.ClipRect.w=modalCenter.y+
                        (command.ClipRect.w-modalCenter.y)*modalScale;
                }
            };
            transformModalVertices(modalDraw,0,modalVertexEnd);
            transformModalClips(modalDraw);
            appendTransientSoftBlur(modalDraw,1.0F-modalEase,uiScale);
            if(modalRoot!=nullptr) {
                ImGuiContext& imguiState=*ImGui::GetCurrentContext();
                for(ImGuiWindow* child:imguiState.Windows) {
                    if(child==nullptr||child==modalRoot||!child->Active) continue;
                    const bool ownedByModal=child->RootWindow==modalRoot ||
                        child->RootWindowPopupTree==modalRoot;
                    if(!ownedByModal) continue;
                    transformModalVertices(child->DrawList,0,
                        child->DrawList->VtxBuffer.Size);
                    transformModalClips(child->DrawList);
                    appendTransientSoftBlur(child->DrawList,
                        1.0F-modalEase,uiScale);
                }
            }
        }
    }

}

void OverlayRenderer::renderBlacklistPanel(RenderFrameContext& frame) noexcept
{
    const auto& snapshot = frame.snapshot;
    const auto& interactive = frame.interactive;
    auto& io = frame.io;
    const auto& uiScale = frame.uiScale;
    const auto& delta = frame.delta;

    // A blacklist encounter is admitted only through the cumulative,
    // colour-validated TAB roster. This excludes lobby/shop bots and keeps a
    // UUID match stable across respawn gaps and later name changes.
    if (!snapshot.matchActive) {
        m_blacklistWarnedCount = 0U;
        m_blacklistMatchWasActive = false;
    } else {
        m_blacklistMatchWasActive = true;
        if (m_blacklist.matchAlertsEnabled) {
            for (std::uint32_t entryIndex = 0U;
                 entryIndex < m_blacklist.count; ++entryIndex) {
                const BlacklistEntry& entry = m_blacklist.entries[entryIndex];
                if (!entry.warnOnEncounter) continue;
                bool encountered = false;
                for (std::uint32_t playerIndex = 0U;
                     playerIndex < snapshot.playerCount; ++playerIndex) {
                    const PlayerIdentity& player = snapshot.players[playerIndex];
                    encountered = entry.idOnly
                        ? ::_stricmp(entry.name.data(), player.name.data()) == 0
                        : entry.uuid[0U] != '\0' && player.uuid[0U] != '\0' &&
                          ::_stricmp(entry.uuid.data(), player.uuid.data()) == 0;
                    if (encountered) break;
                }
                if (!encountered) continue;
                bool alreadyWarned = false;
                for (std::uint32_t warned = 0U;
                     warned < m_blacklistWarnedCount; ++warned) {
                    if (::_stricmp(m_blacklistWarnedKeys[warned].data(),
                                   entry.key.data()) == 0) {
                        alreadyWarned = true;
                        break;
                    }
                }
                if (alreadyWarned) continue;
                if (m_blacklistWarnedCount < m_blacklistWarnedKeys.size()) {
                    std::snprintf(m_blacklistWarnedKeys[m_blacklistWarnedCount].data(),
                        m_blacklistWarnedKeys[m_blacklistWarnedCount].size(), "%s",
                        entry.key.data());
                    ++m_blacklistWarnedCount;
                }
                char message[52]{};
                std::snprintf(message, sizeof(message), "WARNING: %s is blacklisted",
                              entry.name.data());
                enqueueMessage(message, false);
            }
        }
    }
    // showWithClickGui applies to the whole Click GUI session rather than only
    // the Blacklist settings page, so navigation never unexpectedly dismisses
    // the panel while the user is reviewing another category.
    const bool blacklistPanelTarget = m_blacklist.panelEnabled ||
        (m_blacklist.showWithClickGui && interactive);
    advancePresentationSpring(m_blacklistPanelProgress,
                              m_blacklistPanelVelocity,
                              blacklistPanelTarget ? 1.0F : 0.0F, delta);
    if (m_blacklistPanelProgress > 0.005F) {
        const float presentation = std::clamp(m_blacklistPanelProgress, 0.0F, 1.0F);
        const float panelAlphaEase = presentation * presentation *
                                     (3.0F - 2.0F * presentation);
        const float panelScale = 1.26F - 0.26F * m_blacklistPanelProgress;
        const float contentScale = static_cast<float>(std::clamp(
            m_blacklist.contentScale, 80, 200)) / 100.0F;
        const auto content = [contentScale](const float value) noexcept {
            return value * contentScale;
        };
        const float requestedWidth = std::clamp(440.0F *
            static_cast<float>(m_blacklist.panelWidth) / 100.0F * contentScale,
            content(260.0F), std::max(content(260.0F), io.DisplaySize.x * 0.82F));
        const float expandedHeight = std::clamp(550.0F *
            static_cast<float>(m_blacklist.panelHeight) / 100.0F * contentScale,
            content(240.0F), std::max(content(240.0F), io.DisplaySize.y - 8.0F));
        const float requestedHeight = m_blacklist.collapsed ? content(64.0F) : expandedHeight;
        const float storedX = m_blacklist.panelX < 0 ? 18.0F
            : io.DisplaySize.x * static_cast<float>(m_blacklist.panelX) / 1000.0F;
        const float storedY = m_blacklist.panelY < 0
            ? std::max(8.0F, (io.DisplaySize.y - expandedHeight) * 0.5F)
            : io.DisplaySize.y * static_cast<float>(m_blacklist.panelY) / 1000.0F;
        const float panelX = std::clamp(storedX, 4.0F,
            std::max(4.0F, io.DisplaySize.x - content(260.0F) - 4.0F));
        const float panelY = std::clamp(storedY, 4.0F,
            std::max(4.0F, io.DisplaySize.y - content(240.0F) - 4.0F));
        // Resizing is anchored to the upper-left. If the lower/right edge
        // reaches the viewport, cap the effective size instead of moving the
        // saved top-left in the opposite direction.
        const float width = std::clamp(requestedWidth, content(260.0F),
            std::max(content(260.0F), io.DisplaySize.x - panelX - 4.0F));
        const float minimumHeight = m_blacklist.collapsed ? content(64.0F) : content(240.0F);
        const float height = std::clamp(requestedHeight, minimumHeight,
            std::max(minimumHeight, io.DisplaySize.y - panelY - 4.0F));
        ImGui::SetNextWindowPos(ImVec2(panelX, panelY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panelAlphaEase);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        // Match the main GUI's selected font preset. Panel content scaling owns
        // geometry only; applying it again through SetWindowFontScale made text
        // and controls grow twice and caused the card proportions to drift.
        const std::size_t blacklistFontIndex=static_cast<std::size_t>(
            std::clamp(m_guiScaleIndex,0,3));
        ImFont* const listFont = m_fonts[blacklistFontIndex]
            ? m_fonts[blacklistFontIndex] : ImGui::GetFont();
        ImFont* const listBold = m_boldFonts[blacklistFontIndex]
            ? m_boldFonts[blacklistFontIndex] : listFont;
        constexpr std::array<float,4U> blacklistFontSizes{{15.0F,19.0F,23.0F,27.0F}};
        const float blacklistFontSize=blacklistFontSizes[blacklistFontIndex];
        ImGui::PushFont(listFont);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(content(10.0F), content(7.0F)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, content(10.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, content(10.0F));
        const auto panelRgb = unpackRgb(m_blacklist.panelColor);
        const bool lightPanel = panelRgb[0] * 0.2126F + panelRgb[1] * 0.7152F + panelRgb[2] * 0.0722F > 0.6F;
        const ImU32 ink = lightPanel ? IM_COL32(24,26,32,255) : IM_COL32(245,246,250,255);
        const ImU32 muted = lightPanel ? IM_COL32(82,86,96,255) : IM_COL32(171,180,197,255);
        const ImU32 cardSurface = lightPanel ? IM_COL32(0,0,0,13) : IM_COL32(255,255,255,16);
        ImGui::PushStyleColor(ImGuiCol_Text, ink);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, cardSurface);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, lightPanel ? IM_COL32(68,72,86,140) : IM_COL32(196,202,218,140));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
        if (!interactive) flags |= ImGuiWindowFlags_NoInputs;
        ImDrawList* panelDraw = nullptr;
        ImDrawList* entriesDraw = nullptr;
        int panelBegin = 0, panelEnd = 0, entriesBegin = 0, entriesEnd = 0;
        if (ImGui::Begin("##BlacklistPanel", nullptr, flags)) {
            panelDraw = ImGui::GetWindowDrawList();
            panelBegin = panelDraw->VtxBuffer.Size;
            const ImVec2 minimum = ImGui::GetWindowPos();
            const ImVec2 maximum(minimum.x + width, minimum.y + height);
            const int surfaceAlpha = static_cast<int>(std::lround(
                std::clamp(m_blacklist.panelOpacity, 0, 100) * 2.55));
            if (surfaceAlpha < 250) {
                captureBackdropTexture();
                if (m_blurTexture != 0U) {
                    constexpr std::array<ImVec2, 9U> taps{{
                        ImVec2(0,0), ImVec2(-3,0), ImVec2(3,0), ImVec2(0,-3),
                        ImVec2(0,3), ImVec2(-5,-5), ImVec2(5,-5),
                        ImVec2(-5,5), ImVec2(5,5)}};
                    const float exitBlurSpread = 1.0F +
                        (1.0F - presentation) * 2.8F;
                    for (const ImVec2 tap : taps) {
                        const float left = std::clamp(minimum.x +
                            tap.x * exitBlurSpread, 0.0F, io.DisplaySize.x);
                        const float top = std::clamp(minimum.y +
                            tap.y * exitBlurSpread, 0.0F, io.DisplaySize.y);
                        const float right = std::clamp(maximum.x +
                            tap.x * exitBlurSpread, 0.0F, io.DisplaySize.x);
                        const float bottom = std::clamp(maximum.y +
                            tap.y * exitBlurSpread, 0.0F, io.DisplaySize.y);
                        panelDraw->AddImageRounded(reinterpret_cast<ImTextureID>(
                            static_cast<std::uintptr_t>(m_blurTexture)), minimum, maximum,
                            ImVec2(left / io.DisplaySize.x, 1.0F - top / io.DisplaySize.y),
                            ImVec2(right / io.DisplaySize.x, 1.0F - bottom / io.DisplaySize.y),
                            IM_COL32(255,255,255,static_cast<int>(
                                std::lround(36.0F * panelAlphaEase))), content(18.0F));
                    }
                }
            }
            // No offset shadow inside the window's rectangular clip. It is
            // truncated at the lower/right edges and produces square corners
            // on both collapsed and expanded cards. The rounded surface and
            // subtle outline provide separation without extending the clip.
            panelDraw->AddRectFilled(minimum, maximum,
                packedRgbColor(m_blacklist.panelColor, surfaceAlpha), content(18.0F));
            panelDraw->AddRect(minimum, maximum, IM_COL32(255,255,255,42),
                               content(18.0F), 0, std::max(1.0F,contentScale));
            ImFont* const bold = listBold;
            panelDraw->AddText(bold, blacklistFontSize * 1.12F,
                ImVec2(minimum.x + content(16.0F), minimum.y + content(11.0F)),
                ink, "Blacklist");
            char countLabel[48]{};
            std::snprintf(countLabel, sizeof(countLabel), "%u saved players", m_blacklist.count);
            panelDraw->AddText(listFont, blacklistFontSize * 0.72F,
                ImVec2(minimum.x + content(16.0F), minimum.y + content(38.0F)),
                muted, countLabel);

            const ImVec2 collapseMin(maximum.x - content(48.0F),
                                     minimum.y + content(13.0F));
            const ImVec2 collapseMax(maximum.x - content(10.0F),
                                     minimum.y + content(51.0F));
            panelDraw->AddRectFilled(collapseMin, collapseMax,
                IM_COL32(255,255,255,18), content(11.0F));
            const float chevronY = (collapseMin.y + collapseMax.y) * 0.5F;
            const float chevronDirection = m_blacklist.collapsed ? -1.0F : 1.0F;
            panelDraw->AddLine(
                ImVec2(collapseMin.x + content(11.0F), chevronY - content(4.0F) * chevronDirection),
                ImVec2((collapseMin.x + collapseMax.x) * 0.5F,
                       chevronY + content(4.0F) * chevronDirection),
                ink, content(2.0F));
            panelDraw->AddLine(
                ImVec2((collapseMin.x + collapseMax.x) * 0.5F,
                       chevronY + content(4.0F) * chevronDirection),
                ImVec2(collapseMax.x - content(11.0F), chevronY - content(4.0F) * chevronDirection),
                ink, content(2.0F));

            if (interactive && presentation > 0.985F) {
                const ImVec2 resizeMin(maximum.x - content(24.0F),
                                       maximum.y - content(24.0F));
                const bool panelHovered = ImGui::IsWindowHovered();
                const bool collapseHovered = panelHovered && ImGui::IsMouseHoveringRect(
                    collapseMin, collapseMax, false);
                const bool resizeHovered = panelHovered && !m_blacklist.collapsed &&
                    ImGui::IsMouseHoveringRect(resizeMin, maximum, false);
                const bool headerHovered = panelHovered && ImGui::IsMouseHoveringRect(
                    minimum, ImVec2(collapseMin.x - content(4.0F),
                                    minimum.y + content(64.0F)), false);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (collapseHovered) {
                        m_blacklist.collapsed = !m_blacklist.collapsed;
                        m_blacklistAction = {};
                        m_blacklistAction.type = BlacklistAction::Type::Settings;
                        m_blacklistAction.panelEnabled = m_blacklist.panelEnabled;
                        m_blacklistAction.matchAlertsEnabled = m_blacklist.matchAlertsEnabled;
                        m_blacklistAction.allowIdOnlyNicks = m_blacklist.allowIdOnlyNicks;
                        m_blacklistAction.showWithClickGui = m_blacklist.showWithClickGui;
                        m_blacklistAction.collapsed = m_blacklist.collapsed;
                        m_blacklistAction.panelOpacity = m_blacklist.panelOpacity;
                        m_blacklistAction.contentScale = m_blacklist.contentScale;
                        m_blacklistAction.panelColor = m_blacklist.panelColor;
                        m_blacklistActionDirty = true;
                    } else if (resizeHovered) {
                        m_blacklistPanelResizing = true;
                        m_blacklistPanelDragging = false;
                        // The default Y is vertically centred and therefore
                        // depends on height. Materialise the current top-left
                        // position before resizing so dragging the lower-right
                        // grip never moves the top edge in the opposite
                        // direction.
                        if (m_blacklist.panelX < 0) {
                            m_blacklist.panelX = std::clamp(
                                static_cast<int>(std::lround(panelX /
                                    std::max(1.0F, io.DisplaySize.x) * 1000.0F)),
                                0, 1000);
                        }
                        if (m_blacklist.panelY < 0) {
                            m_blacklist.panelY = std::clamp(
                                static_cast<int>(std::lround(panelY /
                                    std::max(1.0F, io.DisplaySize.y) * 1000.0F)),
                                0, 1000);
                        }
                        m_blacklistResizeStartMouseX = io.MousePos.x;
                        m_blacklistResizeStartMouseY = io.MousePos.y;
                        m_blacklistResizeStartWidth = m_blacklist.panelWidth;
                        m_blacklistResizeStartHeight = m_blacklist.panelHeight;
                    } else if (headerHovered) {
                        m_blacklistPanelDragging = true;
                        m_blacklistPanelResizing = false;
                        m_blacklistDragStartMouseX = io.MousePos.x;
                        m_blacklistDragStartMouseY = io.MousePos.y;
                        m_blacklistDragStartPanelX = panelX;
                        m_blacklistDragStartPanelY = panelY;
                    }
                }
                if (m_blacklistPanelDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const float x = m_blacklistDragStartPanelX +
                        io.MousePos.x - m_blacklistDragStartMouseX;
                    const float y = m_blacklistDragStartPanelY +
                        io.MousePos.y - m_blacklistDragStartMouseY;
                    m_blacklist.panelX = std::clamp(static_cast<int>(std::lround(
                        x / std::max(1.0F, io.DisplaySize.x) * 1000.0F)), 0, 1000);
                    m_blacklist.panelY = std::clamp(static_cast<int>(std::lround(
                        y / std::max(1.0F, io.DisplaySize.y) * 1000.0F)), 0, 1000);
                    m_blacklistPanelTransformDirty = true;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                } else if (m_blacklistPanelResizing &&
                           ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_blacklist.panelWidth = std::clamp(
                        m_blacklistResizeStartWidth + static_cast<int>(std::lround(
                            (io.MousePos.x - m_blacklistResizeStartMouseX) * 100.0F /
                            content(440.0F))),
                        60, 180);
                    m_blacklist.panelHeight = std::clamp(
                        m_blacklistResizeStartHeight + static_cast<int>(std::lround(
                            (io.MousePos.y - m_blacklistResizeStartMouseY) * 100.0F /
                            content(550.0F))),
                        60, 300);
                    m_blacklistPanelTransformDirty = true;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                } else if (resizeHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                else if (headerHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                    (m_blacklistPanelDragging || m_blacklistPanelResizing)) {
                    if (m_blacklistPanelTransformDirty) {
                        m_blacklistAction = {};
                        m_blacklistAction.type = BlacklistAction::Type::Layout;
                        m_blacklistAction.x = m_blacklist.panelX;
                        m_blacklistAction.y = m_blacklist.panelY;
                        m_blacklistAction.width = m_blacklist.panelWidth;
                        m_blacklistAction.height = m_blacklist.panelHeight;
                        m_blacklistActionDirty = true;
                    }
                    m_blacklistPanelTransformDirty = false;
                    m_blacklistPanelDragging = false;
                    m_blacklistPanelResizing = false;
                }
            }

            if (!m_blacklist.collapsed) {
                ImGui::SetCursorScreenPos(ImVec2(minimum.x + content(12.0F),
                                                  minimum.y + content(68.0F)));
                ImGui::SetNextItemWidth(width - content(24.0F));
                if (ImGui::InputTextWithHint("##BlacklistSearch", "Search name or reason...",
                                            m_blacklistSearch.data(), m_blacklistSearch.size()))
                    m_blacklistScroll = {};
                ImGui::SetCursorScreenPos(ImVec2(minimum.x + content(12.0F),
                                                  minimum.y + content(110.0F)));
                beginSmoothChild("##BlacklistCards",
                                 ImVec2(width - content(24.0F),
                                        height - content(168.0F)),
                                 m_blacklistScroll, delta, ImGuiWindowFlags_NoBackground);
                entriesDraw = ImGui::GetWindowDrawList();
                entriesBegin = entriesDraw->VtxBuffer.Size;
                const auto contains = [](const char* haystack, const char* needle) {
                    return std::search(haystack, haystack + std::strlen(haystack),
                        needle, needle + std::strlen(needle), [](unsigned char a, unsigned char b) {
                            return std::tolower(a) == std::tolower(b);
                        }) != haystack + std::strlen(haystack);
                };
                unsigned shown = 0;
                for (std::uint32_t index = 0; index < std::min<std::uint32_t>(
                         m_blacklist.count, static_cast<std::uint32_t>(m_blacklist.entries.size())); ++index) {
                    const BlacklistEntry& entry = m_blacklist.entries[index];
                    if (m_blacklistSearch[0] && !contains(entry.name.data(), m_blacklistSearch.data()) &&
                        !contains(entry.reason.data(), m_blacklistSearch.data())) continue;
                    ++shown;
                    ImGui::PushID(entry.key.data());
                    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                    const float cardWidth = ImGui::GetContentRegionAvail().x;
                    const char* reason = entry.reason[0] ? entry.reason.data() : "No reason provided";
                    const float reasonSize=blacklistFontSize*0.92F;
                    const float nameSize=blacklistFontSize*1.03F;
                    const float metaSize=blacklistFontSize*0.78F;
                    const float reasonHeight = std::clamp(listFont->CalcTextSizeA(
                        reasonSize, FLT_MAX,
                        std::max(content(80.0F), cardWidth - content(24.0F)), reason).y,
                        reasonSize, reasonSize*3.0F);
                    const ImVec2 cardMax(cardMin.x + cardWidth,
                                         cardMin.y + content(101.0F) + reasonHeight);
                    entriesDraw->AddRectFilled(cardMin, cardMax, cardSurface,
                                               content(12.0F));
                    entriesDraw->AddRect(cardMin,cardMax,
                        packedRgbColor(m_features.clickGuiAccentColor,lightPanel?48:65),content(12.0F));
                    entriesDraw->AddRectFilled(cardMin,
                        ImVec2(cardMax.x,cardMin.y+content(52.0F)),
                        packedRgbColor(m_features.clickGuiAccentColor,lightPanel?16:25),
                        content(12.0F),ImDrawFlags_RoundCornersTop);
                unsigned faceTexture = 0U;
                if (entry.facePath[0U] != '\0') {
                    BlacklistTexture* slot = nullptr;
                    for (BlacklistTexture& cached : m_blacklistTextures) {
                        if (cached.path[0U] != '\0' &&
                            std::strcmp(cached.path.data(), entry.facePath.data()) == 0) {
                            slot = &cached; break;
                        }
                        if (slot == nullptr && cached.path[0U] == '\0') slot = &cached;
                    }
                    if (slot != nullptr && slot->path[0U] == '\0') {
                        std::snprintf(slot->path.data(), slot->path.size(), "%s",
                                      entry.facePath.data());
                        int imageWidth = 0, imageHeight = 0, channels = 0;
                        unsigned char* pixels = stbi_load(entry.facePath.data(),
                            &imageWidth, &imageHeight, &channels, 4);
                        if (pixels != nullptr && imageWidth > 0 && imageHeight > 0) {
                            GLint lastTexture = 0;
                            ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
                            ::glGenTextures(1, &slot->texture);
                            ::glBindTexture(GL_TEXTURE_2D, slot->texture);
                            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                            ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth,
                                imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                            ::glBindTexture(GL_TEXTURE_2D,
                                            static_cast<GLuint>(lastTexture));
                        }
                        stbi_image_free(pixels);
                    }
                    if (slot != nullptr) faceTexture = slot->texture;
                }

                    const ImVec2 avatarMin(cardMin.x + content(12.0F),
                                           cardMin.y + content(12.0F));
                    const ImVec2 avatarMax(avatarMin.x + content(36.0F),
                                           avatarMin.y + content(36.0F));
                    entriesDraw->AddRectFilled(avatarMin, avatarMax,
                                               IM_COL32(87,83,112,180), content(8.0F));
                    if (faceTexture) entriesDraw->AddImageRounded(
                        reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(faceTexture)),
                        avatarMin, avatarMax, ImVec2(0,0), ImVec2(1,1),
                        IM_COL32_WHITE, content(8.0F));
                    else {
                        // Neutral placeholder until the real skin file arrives.
                        entriesDraw->AddCircle(ImVec2(avatarMin.x+content(18.0F),
                            avatarMin.y+content(13.0F)), content(5.0F), muted, 16,
                            content(1.5F));
                        entriesDraw->AddBezierQuadratic(
                            ImVec2(avatarMin.x+content(8.0F),avatarMin.y+content(29.0F)),
                            ImVec2(avatarMin.x+content(18.0F),avatarMin.y+content(14.0F)),
                            ImVec2(avatarMin.x+content(28.0F),avatarMin.y+content(29.0F)),
                            muted,content(1.5F));
                    }
                    const ImVec4 nameClip(cardMin.x + content(60.0F),
                        cardMin.y + content(10.0F),cardMax.x-content(12.0F),
                        cardMin.y + content(37.0F));
                    entriesDraw->AddText(bold,nameSize,
                        ImVec2(cardMin.x+content(60.0F),cardMin.y+content(10.0F)),
                                         ink, entry.name.data(), nullptr, 0, &nameClip);
                    entriesDraw->AddText(listFont,metaSize,
                        ImVec2(cardMin.x+content(60.0F),cardMin.y+content(37.0F)),
                        entry.nick ? (lightPanel?IM_COL32(117,58,153,255):IM_COL32(193,122,225,255)) : muted,
                        entry.nick ? "NICK / ID ONLY" : "UUID LINKED");
                    const ImVec4 reasonClip(cardMin.x+content(12.0F),
                        cardMin.y+content(58.0F),cardMax.x-content(12.0F),
                        cardMin.y+content(58.0F)+reasonHeight);
                    entriesDraw->AddText(listFont,reasonSize,
                        ImVec2(cardMin.x+content(12.0F),cardMin.y+content(58.0F)),
                        ink,reason,nullptr,cardWidth-content(24.0F),&reasonClip);
                    std::time_t seconds = static_cast<std::time_t>(entry.addedAt / 1000);
                    std::tm local{};
                    char date[24]{};
                    if (::_localtime64_s(&local, &seconds) == 0)
                        std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &local);
                    entriesDraw->AddLine(ImVec2(cardMin.x+content(12.0F),cardMax.y-content(35.0F)),
                        ImVec2(cardMax.x-content(12.0F),cardMax.y-content(35.0F)),
                        lightPanel?IM_COL32(0,0,0,18):IM_COL32(255,255,255,20));
                    entriesDraw->AddText(listFont,metaSize,
                        ImVec2(cardMin.x+content(12.0F),cardMax.y-content(18.0F)-metaSize*.5F),
                                         muted, date);
                    // Two-step removal: the first press arms this record only.
                    // Stable UUID keys keep the confirmation attached after filtering.
                    const bool confirming = std::strcmp(m_blacklistDeleteKey.data(), entry.key.data()) == 0;
                    ImGui::SetCursorScreenPos(ImVec2(cardMax.x-content(66.0F),
                                                      cardMax.y-content(30.0F)));
                    if (ImGui::InvisibleButton("##remove",
                                               ImVec2(content(56.0F),content(24.0F))) && interactive) {
                        if (!confirming) std::snprintf(m_blacklistDeleteKey.data(),
                            m_blacklistDeleteKey.size(), "%s", entry.key.data());
                        else {
                            m_blacklistAction = {};
                            m_blacklistAction.type = BlacklistAction::Type::Remove;
                            m_blacklistAction.key = entry.key;
                            m_blacklistActionDirty = true;
                            m_blacklistDeleteKey = {};
                        }
                    }
                    const bool hovered = ImGui::IsItemHovered();
                    if (hovered || confirming)
                        entriesDraw->AddRectFilled(
                            ImVec2(cardMax.x-content(66.0F),cardMax.y-content(30.0F)),
                            ImVec2(cardMax.x-content(10.0F),cardMax.y-content(6.0F)),
                            IM_COL32(236,83,108,30),content(7.0F));
                    const char* removeText = confirming ? "Confirm" : "Remove";
                    const float removeWidth=listFont->CalcTextSizeA(
                        metaSize,FLT_MAX,0,removeText).x;
                    entriesDraw->AddText(listFont,metaSize,
                        ImVec2(cardMax.x-content(38.0F)-removeWidth*0.5F,
                               cardMax.y-content(18.0F)-metaSize*.5F),
                        lightPanel ? IM_COL32(166,32,60,255) : IM_COL32(255,153,167,255), removeText);
                    if (hovered) ImGui::SetTooltip(confirming ? "Click again to remove this record" : "Remove saved record");
                    ImGui::SetCursorScreenPos(ImVec2(cardMin.x,
                                                      cardMax.y+content(8.0F)));
                    ImGui::Dummy(ImVec2(cardWidth,content(1.0F)));
                    ImGui::PopID();
                }
                if (!shown) {
                    ImGui::Dummy(ImVec2(content(1.0F),content(14.0F)));
                    ImGui::PushStyleColor(ImGuiCol_Text, muted);
                    ImGui::TextWrapped(m_blacklistSearch[0] ? "No matching players. Try another name or reason."
                        : "No saved players yet. Add someone from your recent encounters below.");
                    ImGui::PopStyleColor();
                }
                ImGui::Dummy(ImVec2(1.0F,7.0F*uiScale));
                entriesEnd = entriesDraw->VtxBuffer.Size;
                endSmoothChild(m_blacklistScroll,delta);
                const ImVec2 addMin(minimum.x+content(12.0F),
                                    maximum.y-content(48.0F));
                const ImVec2 addSize(width-content(44.0F),content(36.0F));
                ImGui::SetCursorScreenPos(addMin);
                if (ImGui::InvisibleButton("##BlacklistAddPlayer", addSize) && interactive)
                    m_blacklistAddOpen = true;
                const bool addHovered = interactive && ImGui::IsItemHovered();
                panelDraw->AddRectFilled(addMin, ImVec2(addMin.x + addSize.x, addMin.y + addSize.y),
                    packedRgbColor(m_features.clickGuiAccentColor, addHovered ? 92 : 58),
                    content(11.0F));
                const char* addText = "Add player";
                const float addTextSize=blacklistFontSize*0.92F;
                const float textWidth=bold->CalcTextSizeA(
                    addTextSize,FLT_MAX,0,addText).x;
                const float labelLeft=addMin.x+
                    (addSize.x-textWidth-content(23.0F))*0.5F;
                const ImVec2 plus(labelLeft+content(6.0F),
                                  addMin.y+content(18.0F));
                panelDraw->AddLine(ImVec2(plus.x-content(5.0F),plus.y),
                    ImVec2(plus.x+content(5.0F),plus.y),ink,content(1.8F));
                panelDraw->AddLine(ImVec2(plus.x,plus.y-content(5.0F)),
                    ImVec2(plus.x,plus.y+content(5.0F)),ink,content(1.8F));
                panelDraw->AddText(bold,addTextSize,
                    ImVec2(labelLeft+content(23.0F),addMin.y+content(10.0F)),ink,addText);
                panelDraw->AddLine(ImVec2(maximum.x-content(18.0F),maximum.y-content(9.0F)),
                    ImVec2(maximum.x-content(9.0F),maximum.y-content(18.0F)),muted,content(1.5F));
                panelDraw->AddLine(ImVec2(maximum.x-content(12.0F),maximum.y-content(9.0F)),
                    ImVec2(maximum.x-content(9.0F),maximum.y-content(12.0F)),muted,content(1.5F));
            }
            panelEnd = panelDraw->VtxBuffer.Size;
        }
        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(4);
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        const ImVec2 center(panelX + width * 0.5F, panelY + height * 0.5F);
        const auto transform = [&](ImDrawList* drawList, int begin, int end) noexcept {
            if (drawList == nullptr) return;
            begin = std::clamp(begin, 0, drawList->VtxBuffer.Size);
            end = std::clamp(end, begin, drawList->VtxBuffer.Size);
            for (int vertex = begin; vertex < end; ++vertex) {
                ImDrawVert& drawVertex = drawList->VtxBuffer[vertex];
                if (std::abs(panelScale - 1.0F) >= 0.0001F) {
                    drawVertex.pos.x = center.x +
                        (drawVertex.pos.x - center.x) * panelScale;
                    drawVertex.pos.y = center.y +
                        (drawVertex.pos.y - center.y) * panelScale;
                }
                const unsigned alpha = static_cast<unsigned>(drawVertex.col >> 24U);
                const unsigned faded = static_cast<unsigned>(std::clamp(
                    std::lround(static_cast<float>(alpha) * panelAlphaEase),
                    0L, 255L));
                drawVertex.col = (drawVertex.col & 0x00FFFFFFU) |
                    (faded << 24U);
            }
            if (std::abs(panelScale - 1.0F) >= 0.0001F) {
                for (ImDrawCmd& command : drawList->CmdBuffer) {
                    command.ClipRect.x = center.x +
                        (command.ClipRect.x - center.x) * panelScale;
                    command.ClipRect.y = center.y +
                        (command.ClipRect.y - center.y) * panelScale;
                    command.ClipRect.z = center.x +
                        (command.ClipRect.z - center.x) * panelScale;
                    command.ClipRect.w = center.y +
                        (command.ClipRect.w - center.y) * panelScale;
                }
            }
        };
        transform(panelDraw, panelBegin, panelEnd);
        if (entriesDraw != panelDraw) transform(entriesDraw, entriesBegin, entriesEnd);
    }

}

} // namespace mcoverlay
