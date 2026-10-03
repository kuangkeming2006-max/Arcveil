#pragma once
#include "UiModel.h"
#include "GuiDesignState.h"
#include "../UiMotion.h"
#include <imgui.h>
#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mcoverlay::ui {
// Value-state layout for the GUI. The Windows adapter binds references to
// its existing renderer state, preserving dirty mailboxes and IPC ownership.
struct ClickGuiState {
    std::array<float, 4U> m_aimSectionBodyHeight{};
    std::array<bool, 4U> m_aimSectionOpen{{true, true, true, false}};
    std::array<float, 4U> m_aimSectionProgress{{1.0F, 1.0F, 1.0F, 0.0F}};
    std::array<float, 4U> m_aimSectionVelocity{};
    bool m_bedRescanPending = false;
    BlacklistOverlaySnapshot m_blacklist{};
    BlacklistAction m_blacklistAction{};
    bool m_blacklistActionDirty = false;
    bool m_blacklistAddOpen = false;
    std::array<ImFont*, 4U> m_boldFonts{};
    std::array<float,26U> m_clickGuiNavEnabled{};
    std::array<float, 26U> m_clickGuiNavHover{};
    float m_clickGuiNavPosition = 0.0F;
    std::array<float,26U> m_clickGuiNavSelection{};
    int m_clickGuiPage = 0;
    float m_clickGuiPageProgress = 1.0F;
    float m_clickGuiProgress = 0.0F;
    float m_clickGuiThemeProgress = 0.0F;
    float m_clickGuiX = -9999.0F;
    float m_clickGuiY = 18.0F;
    std::array<char, 96U> m_featureSearch{};
    bool m_featureSettingsDirty = false;
    FeatureSettings m_features{};
    bool m_guiScaleDirty = false;
    int m_guiScaleIndex = 1;
    bool m_hotkeyCaptureArmed = false;
    int m_hotkeyCaptureCooldownFrames = 0;
    int m_hotkeyCaptureTarget = 0;
    std::array<char, 17U> m_hypixelInput{};
    std::array<char, 17U> m_hypixelQuery{};
    bool m_hypixelQueryPending = false;
    bool m_imePositionEditing = false;
    MediaOverlaySettings m_mediaSettings{};
    bool m_mediaSettingsDirty = false;
    unsigned m_menuHotkey = 0xDE;
    bool m_menuHotkeyDirty = false;
    SmoothScroll m_navigationScroll;
    int m_previousClickGuiPage = 0;
    double m_searchActivationStartedAt = 0.0;
    float m_searchClearProgress = 0.0F;
    bool m_searchFocusRequested = false;
    float m_searchHoverProgress = 0.0F;
    bool m_searchInputActive = false;
    bool m_searchIslandOpen = false;
    float m_searchIslandProgress = 0.0F;
    double m_searchLoadingStartedAt = 0.0;
    float m_searchTransitionFrom = 0.0F;
    double m_searchTransitionStartedAt = 0.0;
    float m_searchTransitionTarget = 0.0F;
    std::array<SmoothScroll, 26U> m_settingsScroll{};
    int m_shieldSelectedId=-1;
    std::array<char,37U> m_shieldSelectedUuid{};
    std::uint64_t m_shieldWorld=0U;
    float m_toggleAnimation[64]{};
    bool m_waitingForHotkey = false;
    ClickGuiDesignState m_guiDesign;
};

struct ClickGuiRefs {
    decltype(ClickGuiState::m_aimSectionBodyHeight)& m_aimSectionBodyHeight;
    decltype(ClickGuiState::m_aimSectionOpen)& m_aimSectionOpen;
    decltype(ClickGuiState::m_aimSectionProgress)& m_aimSectionProgress;
    decltype(ClickGuiState::m_aimSectionVelocity)& m_aimSectionVelocity;
    decltype(ClickGuiState::m_bedRescanPending)& m_bedRescanPending;
    decltype(ClickGuiState::m_blacklist)& m_blacklist;
    decltype(ClickGuiState::m_blacklistAction)& m_blacklistAction;
    decltype(ClickGuiState::m_blacklistActionDirty)& m_blacklistActionDirty;
    decltype(ClickGuiState::m_blacklistAddOpen)& m_blacklistAddOpen;
    decltype(ClickGuiState::m_boldFonts)& m_boldFonts;
    decltype(ClickGuiState::m_clickGuiNavEnabled)& m_clickGuiNavEnabled;
    decltype(ClickGuiState::m_clickGuiNavHover)& m_clickGuiNavHover;
    decltype(ClickGuiState::m_clickGuiNavPosition)& m_clickGuiNavPosition;
    decltype(ClickGuiState::m_clickGuiNavSelection)& m_clickGuiNavSelection;
    decltype(ClickGuiState::m_clickGuiPage)& m_clickGuiPage;
    decltype(ClickGuiState::m_clickGuiPageProgress)& m_clickGuiPageProgress;
    decltype(ClickGuiState::m_clickGuiProgress)& m_clickGuiProgress;
    decltype(ClickGuiState::m_clickGuiThemeProgress)& m_clickGuiThemeProgress;
    decltype(ClickGuiState::m_clickGuiX)& m_clickGuiX;
    decltype(ClickGuiState::m_clickGuiY)& m_clickGuiY;
    decltype(ClickGuiState::m_featureSearch)& m_featureSearch;
    decltype(ClickGuiState::m_featureSettingsDirty)& m_featureSettingsDirty;
    decltype(ClickGuiState::m_features)& m_features;
    decltype(ClickGuiState::m_guiScaleDirty)& m_guiScaleDirty;
    decltype(ClickGuiState::m_guiScaleIndex)& m_guiScaleIndex;
    decltype(ClickGuiState::m_hotkeyCaptureArmed)& m_hotkeyCaptureArmed;
    decltype(ClickGuiState::m_hotkeyCaptureCooldownFrames)& m_hotkeyCaptureCooldownFrames;
    decltype(ClickGuiState::m_hotkeyCaptureTarget)& m_hotkeyCaptureTarget;
    decltype(ClickGuiState::m_hypixelInput)& m_hypixelInput;
    decltype(ClickGuiState::m_hypixelQuery)& m_hypixelQuery;
    decltype(ClickGuiState::m_hypixelQueryPending)& m_hypixelQueryPending;
    decltype(ClickGuiState::m_imePositionEditing)& m_imePositionEditing;
    decltype(ClickGuiState::m_mediaSettings)& m_mediaSettings;
    decltype(ClickGuiState::m_mediaSettingsDirty)& m_mediaSettingsDirty;
    decltype(ClickGuiState::m_menuHotkey)& m_menuHotkey;
    decltype(ClickGuiState::m_menuHotkeyDirty)& m_menuHotkeyDirty;
    decltype(ClickGuiState::m_navigationScroll)& m_navigationScroll;
    decltype(ClickGuiState::m_previousClickGuiPage)& m_previousClickGuiPage;
    decltype(ClickGuiState::m_searchActivationStartedAt)& m_searchActivationStartedAt;
    decltype(ClickGuiState::m_searchClearProgress)& m_searchClearProgress;
    decltype(ClickGuiState::m_searchFocusRequested)& m_searchFocusRequested;
    decltype(ClickGuiState::m_searchHoverProgress)& m_searchHoverProgress;
    decltype(ClickGuiState::m_searchInputActive)& m_searchInputActive;
    decltype(ClickGuiState::m_searchIslandOpen)& m_searchIslandOpen;
    decltype(ClickGuiState::m_searchIslandProgress)& m_searchIslandProgress;
    decltype(ClickGuiState::m_searchLoadingStartedAt)& m_searchLoadingStartedAt;
    decltype(ClickGuiState::m_searchTransitionFrom)& m_searchTransitionFrom;
    decltype(ClickGuiState::m_searchTransitionStartedAt)& m_searchTransitionStartedAt;
    decltype(ClickGuiState::m_searchTransitionTarget)& m_searchTransitionTarget;
    decltype(ClickGuiState::m_settingsScroll)& m_settingsScroll;
    decltype(ClickGuiState::m_shieldSelectedId)& m_shieldSelectedId;
    decltype(ClickGuiState::m_shieldSelectedUuid)& m_shieldSelectedUuid;
    decltype(ClickGuiState::m_shieldWorld)& m_shieldWorld;
    decltype(ClickGuiState::m_toggleAnimation)& m_toggleAnimation;
    decltype(ClickGuiState::m_waitingForHotkey)& m_waitingForHotkey;
    ClickGuiDesignState& m_guiDesign;
};
inline ClickGuiRefs bind(ClickGuiState& state) noexcept {
    return {state.m_aimSectionBodyHeight, state.m_aimSectionOpen, state.m_aimSectionProgress, state.m_aimSectionVelocity, state.m_bedRescanPending, state.m_blacklist, state.m_blacklistAction, state.m_blacklistActionDirty, state.m_blacklistAddOpen, state.m_boldFonts, state.m_clickGuiNavEnabled, state.m_clickGuiNavHover, state.m_clickGuiNavPosition, state.m_clickGuiNavSelection, state.m_clickGuiPage, state.m_clickGuiPageProgress, state.m_clickGuiProgress, state.m_clickGuiThemeProgress, state.m_clickGuiX, state.m_clickGuiY, state.m_featureSearch, state.m_featureSettingsDirty, state.m_features, state.m_guiScaleDirty, state.m_guiScaleIndex, state.m_hotkeyCaptureArmed, state.m_hotkeyCaptureCooldownFrames, state.m_hotkeyCaptureTarget, state.m_hypixelInput, state.m_hypixelQuery, state.m_hypixelQueryPending, state.m_imePositionEditing, state.m_mediaSettings, state.m_mediaSettingsDirty, state.m_menuHotkey, state.m_menuHotkeyDirty, state.m_navigationScroll, state.m_previousClickGuiPage, state.m_searchActivationStartedAt, state.m_searchClearProgress, state.m_searchFocusRequested, state.m_searchHoverProgress, state.m_searchInputActive, state.m_searchIslandOpen, state.m_searchIslandProgress, state.m_searchLoadingStartedAt, state.m_searchTransitionFrom, state.m_searchTransitionStartedAt, state.m_searchTransitionTarget, state.m_settingsScroll, state.m_shieldSelectedId, state.m_shieldSelectedUuid, state.m_shieldWorld, state.m_toggleAnimation, state.m_waitingForHotkey, state.m_guiDesign};
}

struct ClickGuiPlayer {
    int entityId=-1;
    bool player=false;
    std::array<char,17> playerName{};
    std::array<char,65> displayName{};
    std::array<char,37> uuid{};
};
struct ClickGuiSnapshot {
    bool hypixelServer=false, integratedSinglePlayer=false;
    bool silentAimAvailable=false, knockbackHurtAvailable=false;
    std::uint32_t knockbackDamageEvents=0, knockbackImpulseEvents=0, knockbackConfirmedEvents=0;
    std::uint64_t worldGeneration=0;
    std::uint32_t entityMarkerCount=0;
    std::span<const ClickGuiPlayer> entityMarkers;
};
struct ClickGuiFrame {
    const ClickGuiSnapshot& snapshot;
    bool interactive;
    ImGuiIO& io;
    float uiScale, delta, guiEase;
    float theme=0;
    ImVec4 guiSurface{}, guiRail{}, guiText{}, guiMuted{}, guiFrame{},
        guiScrollbarTrack{}, guiScrollbarGrab{}, guiAccent{}, guiSelected{};
    // Regular, Semibold, Bold. Host fonts stay independent of GUI typography.
    std::array<ImFont*,3> typefaces{};
};
struct ClickGuiHost {
    std::function<void(bool)> captureEnabled;
    std::function<unsigned()> consumeCapturedHotkey;
    std::function<bool()> mouseHeld;
    std::function<void(unsigned)> menuHotkeyChanged;
    std::function<const char*(unsigned)> keyName;
    std::function<void(const char*,bool)> message;
    std::function<void(const FeatureSettings&,const FeatureSettings&)> featureToasts;
    std::function<void(ImDrawList*,float,float)> transientBlur;
};
void renderClickGui(ClickGuiRefs state, ClickGuiFrame& frame, const ClickGuiHost& host) noexcept;
} // namespace mcoverlay::ui
