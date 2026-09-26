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

bool OverlayRenderer::render(HDC const deviceContext,
                             const GameSnapshot& snapshot,
                             const bool interactive) noexcept
{
    if (m_permanentlyDisabled) {
        return false;
    }
    const HGLRC context = ::wglGetCurrentContext();
    const HWND window = ::WindowFromDC(deviceContext);
    if (context == nullptr || window == nullptr || ::IsWindowVisible(window) == FALSE) {
        return false;
    }
    RECT client{};
    if (::GetClientRect(window, &client) == FALSE ||
        client.right - client.left < 480 || client.bottom - client.top < 270) {
        return false;
    }

    bool newlyInitialized = false;
    if (m_initialized && (m_window != window || m_glContext != context)) {
        if (m_glContext == context) {
            shutdownWithCurrentContext();
        } else {
            abandonForContextChange();
        }
    }
    if (!m_initialized) {
        if (!initialize(window, context)) {
            return false;
        }
        newlyInitialized = true;
    }

    if (m_inputState == nullptr) {
        return false;
    }
    m_inputState->interactive.store(interactive, std::memory_order_release);
    const bool imeWasEnabled=m_inputState->imeEnabled.exchange(
        m_features.fullscreenImeFixEnabled,std::memory_order_acq_rel);
    if(imeWasEnabled&&!m_features.fullscreenImeFixEnabled)
        clearImeComposition(*m_inputState);
    if (!interactive) {
        if (m_imePositionEditing) m_featureSettingsDirty = true;
        m_imePositionEditing = m_imeDragging = false;
        m_inputState->captureHotkey.store(false, std::memory_order_release);
        m_inputState->sessionCursor.store(nullptr, std::memory_order_release);
        m_waitingForHotkey = false;
        m_hotkeyCaptureTarget = 0;
        m_statsPanelDragging = false;
        m_statsPanelResizing = false;
    }
    ImGui::SetCurrentContext(m_imguiContext);
    ImGuiIO& io = ImGui::GetIO();
    m_guiScaleIndex = std::clamp(m_guiScaleIndex, 0, 3);
    const float targetScale = guiScaleForIndex(m_guiScaleIndex);
    const float scaleDelta = std::clamp(io.DeltaTime, 0.0F, 0.05F);
    m_animatedGuiScale += (targetScale - m_animatedGuiScale) *
                          (1.0F - std::exp(-11.0F * scaleDelta));
    if (std::abs(targetScale - m_animatedGuiScale) < 0.001F)
        m_animatedGuiScale = targetScale;
    // Rebuild from immutable constants before NewFrame. This produces a
    // smooth size transition without ever compounding ScaleAllSizes values.
    applyGuiScaleStyle(m_animatedGuiScale, m_guiScaleIndex);
    const float uiScale = m_animatedGuiScale;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (interactive && !m_cursorSessionActive) {
        CURSORINFO cursorInfo{};
        cursorInfo.cbSize = sizeof(cursorInfo);
        HCURSOR cursor = nullptr;
        if (::GetCursorInfo(&cursorInfo) != FALSE &&
            (cursorInfo.flags & CURSOR_SHOWING) != 0U) {
            cursor = cursorInfo.hCursor;
        }
        if (cursor == nullptr) {
            cursor = reinterpret_cast<HCURSOR>(::GetClassLongPtrW(
                window, GCLP_HCURSOR));
        }
        if (cursor == nullptr) cursor = ::LoadCursorW(nullptr, IDC_ARROW);
        m_inputState->sessionCursor.store(cursor, std::memory_order_release);
        m_cursorSessionActive = true;
    } else if (!interactive && m_cursorSessionActive) {
        m_cursorSessionActive = false;
        m_inputState->sessionCursor.store(nullptr, std::memory_order_release);
    }

    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    m_backdropCapturedThisFrame = false;

    // Feature hotkeys are sampled only while Minecraft owns foreground focus.
    // While unfocused we mirror physical state into the edge latch, so a key
    // used in another application cannot fire immediately on refocus.
    const bool gameForeground = ::GetForegroundWindow() == window;
    const bool gameplayHotkeysAllowed = gameForeground && !snapshot.gameScreenOpen &&
        !m_inputState->composingInput.load(std::memory_order_acquire);
    const FeatureSettings featuresBeforeHotkeys = m_features;
    bool hotkeyFeatureChanged = false;
    for (std::size_t index = 0U; index < m_features.featureHotkeys.size(); ++index) {
        const int key = m_features.featureHotkeys[index];
        const bool down = key >= 8 && key <= 254 &&
            (::GetAsyncKeyState(key) & 0x8000) != 0;
        bool toggledFeature = false;
        if (gameplayHotkeysAllowed && !interactive &&
            down && !m_featureHotkeyWasDown[index]) {
            switch (index) {
            case 0: m_features.entityEspEnabled = !m_features.entityEspEnabled; toggledFeature=true; break;
            case 1: m_features.bedEspEnabled = !m_features.bedEspEnabled; toggledFeature=true; break;
            case 2: m_features.nametagEnabled = !m_features.nametagEnabled; toggledFeature=true; break;
            case 3: m_features.bedThreatAlertsEnabled =
                        !m_features.bedThreatAlertsEnabled; toggledFeature=true; break;
            case 4: m_features.safewalkEnabled = !m_features.safewalkEnabled; toggledFeature=true; break;
            case 5: m_features.scaffoldEnabled = !m_features.scaffoldEnabled; toggledFeature=true; break;
            case 6: m_features.flyEnabled = !m_features.flyEnabled; toggledFeature=true; break;
            case 7: m_features.bhopEnabled = !m_features.bhopEnabled; toggledFeature=true; break;
            case 8: m_features.aimAssistEnabled = !m_features.aimAssistEnabled; toggledFeature=true; break;
            case 9: m_features.hypixelPanelEnabled =
                        !m_features.hypixelPanelEnabled; toggledFeature=true; break;
            case 10: m_features.debugChatEnabled = !m_features.debugChatEnabled; toggledFeature=true; break;
            case 11:
                m_blacklist.panelEnabled = !m_blacklist.panelEnabled;
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
                enqueueToast("Blacklist", m_blacklist.panelEnabled);
                break;
            case 12: m_features.textGuiEnabled = !m_features.textGuiEnabled; break;
            case 13: m_features.fireballEspEnabled =
                         !m_features.fireballEspEnabled; break;
            case 14: break;
            case 15: m_features.bedBreakerEnabled =
                          !m_features.bedBreakerEnabled; break;
            case 16: m_features.localVelocityEnabled =
                          !m_features.localVelocityEnabled; break;
            // FreeLook (index 17) is intentionally hold-only. GameBindings
            // samples its physical state and never converts the press to a
            // persistent enabled/disabled edge here.
            case 17: break;
            default: break;
            }
            if (index >= 12U && index <= 16U) toggledFeature = true;
            if (index != 11U && toggledFeature) hotkeyFeatureChanged = true;
        }
        m_featureHotkeyWasDown[index] = down;
    }
    if (hotkeyFeatureChanged) {
        m_features.safewalkHotkey = m_features.featureHotkeys[4U];
        m_featureSettingsDirty = true;
        enqueueFeatureToasts(featuresBeforeHotkeys, m_features);
    }

    const float targetGui = interactive && !m_imePositionEditing ? 1.0F : 0.0F;
    const float delta = std::clamp(io.DeltaTime, 0.0F, 0.10F);
    // A lightly under-damped spring takes roughly half a second to settle. It
    // is slower than the previous exponential lerp but still responsive, and
    // its single small overshoot provides the requested lightweight rebound.
    // Exact under-damped oscillator integration. At normal frame rates this is
    // visually equivalent to the previous spring, while uneven/slow title-menu
    // frames no longer quantize the motion into Euler steps.
    advancePresentationSpring(m_clickGuiProgress, m_clickGuiVelocity,
                              targetGui, delta);
    const float guiLinear = std::clamp(m_clickGuiProgress, 0.0F, 1.0F);
    const float guiEase = guiLinear * guiLinear * (3.0F - 2.0F * guiLinear);
    renderInventoryBlur(std::clamp(guiEase * 1.28F, 0.0F, 1.0F));
    renderImeOverlay(delta, uiScale);

    updateBedThreatAlerts(snapshot);

    RenderFrameContext frame{snapshot, interactive, io, uiScale, delta,
                             gameplayHotkeysAllowed, guiEase};
    renderWorldOverlay(frame);

    renderClickGui(frame);

    renderBlacklistAddDialog(frame);

    renderTextGui(frame);

    renderPlayerStatsPanel(frame);

    renderBlacklistPanel(frame);

    renderMediaOverlay(delta, uiScale, interactive);
    renderToasts(delta, uiScale);

    // Never render a software cursor. Windows remains the only cursor owner,
    // preserving the exact system/game DPI-scaled pointer size and avoiding a
    // second sprite that can race the title-screen cursor.
    io.MouseDrawCursor = false;
    if(m_blacklistAddProgress>0.005F&&ImGui::GetCurrentContext()->OpenPopupStack.empty()) {
        if(ImGuiWindow* editor=ImGui::FindWindowByName("##BlacklistAddDialog"))
            ImGui::BringWindowToDisplayFront(editor);
    }
    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    return newlyInitialized;
}


} // namespace mcoverlay
