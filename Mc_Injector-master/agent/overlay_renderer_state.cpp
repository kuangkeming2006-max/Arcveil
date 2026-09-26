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

bool OverlayRenderer::consumeClickGuiToggle() noexcept
{
    pollFallbackInput();
    return m_inputState != nullptr &&
           m_inputState->clickGuiToggle.exchange(false, std::memory_order_acq_rel);
}

void OverlayRenderer::setFeatureSettings(const FeatureSettings& settings) noexcept
{
    // Do not overwrite a just-clicked ImGui value before AgentRuntime has
    // consumed it and published the corresponding atomic bitset.
    if (!m_featureSettingsDirty &&
        !m_statsPanelDragging && !m_statsPanelResizing) {
        // The IME editor holds an uncommitted preview until Done is clicked.
        // Merge unrelated settings, but never replace its local coordinates
        // with the previous runtime snapshot between mouse-up and Save.
        FeatureSettings incoming = settings;
        // Retired experimental modules remain wire-compatible for older
        // controllers but are neither presented nor allowed to run.
        incoming.longJumpEnabled = false;
        incoming.localMobAuraEnabled = false;
        if (m_imePositionEditing) {
            incoming.imePanelX = m_features.imePanelX;
            incoming.imePanelY = m_features.imePanelY;
        }
        if (m_featureSnapshotInitialized && m_features != incoming) {
            enqueueFeatureToasts(m_features, incoming);
        }
        m_features = incoming;
        if(!m_featureSnapshotInitialized||m_clickGuiProgress<=0.008F)
            m_clickGuiThemeProgress=incoming.clickGuiLightTheme?1.0F:0.0F;
        m_featureSnapshotInitialized = true;
    }
}

bool OverlayRenderer::consumeFeatureSettings(FeatureSettings& settings) noexcept
{
    if (!m_featureSettingsDirty) return false;
    settings = m_features;
    m_featureSettingsDirty = false;
    return true;
}

void OverlayRenderer::setHypixelSnapshot(const HypixelOverlaySnapshot& snapshot) noexcept
{
    m_hypixel = snapshot;
}

void OverlayRenderer::setPlayerStatsSnapshot(
    const PlayerStatsOverlaySnapshot& snapshot) noexcept
{
    m_playerStats = snapshot;
}

void OverlayRenderer::setBlacklistSnapshot(
    const BlacklistOverlaySnapshot& snapshot) noexcept
{
    // Layout changes created by an active drag are first returned to the
    // Controller. Do not let an older pipe snapshot pull the panel backward
    // before that acknowledgement arrives.
    const auto now = ::GetTickCount64();
    if (snapshot.panelX == m_blacklist.panelX && snapshot.panelY == m_blacklist.panelY &&
        snapshot.panelWidth == m_blacklist.panelWidth && snapshot.panelHeight == m_blacklist.panelHeight)
        m_blacklistLayoutPendingUntil = 0U;
    if (snapshot.panelEnabled == m_blacklist.panelEnabled &&
        snapshot.matchAlertsEnabled == m_blacklist.matchAlertsEnabled &&
        snapshot.allowIdOnlyNicks == m_blacklist.allowIdOnlyNicks &&
        snapshot.showWithClickGui == m_blacklist.showWithClickGui &&
        snapshot.collapsed == m_blacklist.collapsed &&
        snapshot.panelOpacity == m_blacklist.panelOpacity &&
        snapshot.contentScale == m_blacklist.contentScale &&
        snapshot.panelColor == m_blacklist.panelColor)
        m_blacklistSettingsPendingUntil = 0U;
    // Continue accepting new entries/avatars during a drag. Only the locally
    // edited fields wait for an IPC echo, bounded in case the pipe disconnects.
    // Copy only editable metadata, not the 128 avatar/record payloads.
    const int localX = m_blacklist.panelX, localY = m_blacklist.panelY;
    const int localWidth = m_blacklist.panelWidth, localHeight = m_blacklist.panelHeight;
    const bool localEnabled = m_blacklist.panelEnabled, localAlerts = m_blacklist.matchAlertsEnabled;
    const bool localNicks = m_blacklist.allowIdOnlyNicks, localGui = m_blacklist.showWithClickGui;
    const bool localCollapsed = m_blacklist.collapsed;
    const int localOpacity = m_blacklist.panelOpacity;
    const int localContentScale = m_blacklist.contentScale;
    const auto localColor = m_blacklist.panelColor;
    m_blacklist = snapshot;
    if (m_blacklistPanelDragging || m_blacklistPanelResizing || m_blacklistPanelTransformDirty ||
        m_blacklistLayoutPendingUntil > now ||
        (m_blacklistActionDirty && m_blacklistAction.type == BlacklistAction::Type::Layout)) {
        m_blacklist.panelX = localX;
        m_blacklist.panelY = localY;
        m_blacklist.panelWidth = localWidth;
        m_blacklist.panelHeight = localHeight;
    }
    if (m_blacklistSettingsPendingUntil > now ||
        (m_blacklistActionDirty && m_blacklistAction.type == BlacklistAction::Type::Settings)) {
        m_blacklist.panelEnabled = localEnabled;
        m_blacklist.matchAlertsEnabled = localAlerts;
        m_blacklist.allowIdOnlyNicks = localNicks;
        m_blacklist.showWithClickGui = localGui;
        m_blacklist.collapsed = localCollapsed;
        m_blacklist.panelOpacity = localOpacity;
        m_blacklist.contentScale = localContentScale;
        m_blacklist.panelColor = localColor;
    }
}

void OverlayRenderer::setMediaSnapshot(
    const MediaPlaybackSnapshot& snapshot) noexcept
{
    m_media = snapshot;
}

void OverlayRenderer::setMediaSettings(
    const MediaOverlaySettings& settings) noexcept
{
    // A drag or an in-GUI edit is first sent back to the Controller. Keep the
    // local value until that message has been consumed so a 500 ms media-state
    // refresh cannot snap the island back to its previous layout.
    if (!m_mediaSettingsDirty && !m_mediaDragging) {
        m_mediaSettings = settings;
    }
    if(m_inputState) {
        m_inputState->mediaPreviousHotkey.store(settings.previousHotkey,
                                                std::memory_order_release);
        m_inputState->mediaToggleHotkey.store(settings.toggleHotkey,
                                              std::memory_order_release);
        m_inputState->mediaNextHotkey.store(settings.nextHotkey,
                                            std::memory_order_release);
    }
}

bool OverlayRenderer::consumeMediaSettings(
    MediaOverlaySettings& settings) noexcept
{
    if (!m_mediaSettingsDirty) return false;
    settings = m_mediaSettings;
    m_mediaSettingsDirty = false;
    return true;
}

MediaAction OverlayRenderer::consumeMediaAction() noexcept
{
    return std::exchange(m_mediaAction, MediaAction::None);
}

bool OverlayRenderer::consumeBlacklistAction(BlacklistAction& action) noexcept
{
    if (!m_blacklistActionDirty) return false;
    action = m_blacklistAction;
    if (action.type == BlacklistAction::Type::Layout)
        m_blacklistLayoutPendingUntil = ::GetTickCount64() + 5000U;
    if (action.type == BlacklistAction::Type::Settings)
        m_blacklistSettingsPendingUntil = ::GetTickCount64() + 5000U;
    m_blacklistAction = {};
    m_blacklistActionDirty = false;
    return true;
}

bool OverlayRenderer::consumeHypixelQuery(std::array<char, 17U>& playerId) noexcept
{
    if (!m_hypixelQueryPending) return false;
    playerId = m_hypixelQuery;
    m_hypixelQueryPending = false;
    return true;
}

void OverlayRenderer::setMenuHotkey(const unsigned virtualKey) noexcept
{
    if ((virtualKey != 0U && virtualKey < 8U) || virtualKey > 254U) return;
    if (m_menuHotkey == virtualKey && m_inputState != nullptr &&
        m_inputState->menuHotkey.load(std::memory_order_acquire) == virtualKey) {
        return;
    }
    m_menuHotkey = virtualKey;
    if (m_inputState != nullptr) {
        m_inputState->menuHotkey.store(virtualKey, std::memory_order_release);
        m_inputState->fallbackPrimed = false;
    }
}

bool OverlayRenderer::consumeMenuHotkeyChange(unsigned& virtualKey) noexcept
{
    if (!m_menuHotkeyDirty) return false;
    virtualKey = m_menuHotkey;
    m_menuHotkeyDirty = false;
    return true;
}

void OverlayRenderer::setGuiScaleIndex(const int index) noexcept
{
    if (m_guiScaleDirty) return;
    m_guiScaleIndex = std::clamp(index, 0, 3);
}

bool OverlayRenderer::consumeGuiScaleChange(int& index) noexcept
{
    if (!m_guiScaleDirty) return false;
    index = std::clamp(m_guiScaleIndex, 0, 3);
    m_guiScaleDirty = false;
    return true;
}

bool OverlayRenderer::consumeBedRescanRequest() noexcept
{
    return std::exchange(m_bedRescanPending, false);
}

void OverlayRenderer::setGameScreenOpen(const bool open) noexcept
{
    if (m_inputState != nullptr)
        m_inputState->gameScreenOpen.store(open, std::memory_order_release);
}


} // namespace mcoverlay
