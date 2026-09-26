#pragma once
#include "MediaHotkeyEdges.h"

#include "bindings/GameBindings.h"
#include "wndproc_hook.h"
#include "UiPreferences.h"
#include "UiMotion.h"
#include "gaussian_blur.h"

#include <windows.h>
#include <imm.h>

#include <atomic>
#include <array>
#include <cstdint>

struct ImGuiContext;
struct ImFont;

namespace mcoverlay {

struct OverlayInputState;

enum class ImeMessageAction : std::uint8_t {
    Ignore,
    ResetLayout,
    ResetComposition,
    QueryComposition,
    QueryCandidates,
    ClearCandidates
};

[[nodiscard]] constexpr ImeMessageAction classifyImeMessage(
    const UINT message,const WPARAM wParam,const bool fullscreenImeEnabled) noexcept
{
    // WM_INPUTLANGCHANGE is delivered while the window's HIMC is changing.
    // It is a layout/name reset boundary even when the fullscreen overlay is
    // disabled, never a safe point at which to probe composition state.
    if(message==WM_INPUTLANGCHANGE) return ImeMessageAction::ResetLayout;
    if(!fullscreenImeEnabled) return ImeMessageAction::Ignore;
    if(message==WM_IME_ENDCOMPOSITION)
        return ImeMessageAction::ResetComposition;
    if(message==WM_IME_STARTCOMPOSITION||message==WM_IME_COMPOSITION)
        return ImeMessageAction::QueryComposition;
    if(message==WM_IME_NOTIFY&&
       (wParam==IMN_OPENCANDIDATE||wParam==IMN_CHANGECANDIDATE))
        return ImeMessageAction::QueryCandidates;
    if(message==WM_IME_NOTIFY&&wParam==IMN_CLOSECANDIDATE)
        return ImeMessageAction::ClearCandidates;
    return ImeMessageAction::Ignore;
}

struct FeatureSettings final {
    static constexpr std::size_t FeatureHotkeyCount = 18U;
    bool espEnabled = true;
    bool entityEspEnabled = true;
    bool entityEspPlayersOnly = false;
    bool bedEspEnabled = true;
    bool bedAutoRefreshEnabled = false;
    bool labelsEnabled = true;
    bool hypixelPanelEnabled = true;
    bool bedThreatAlertsEnabled = true;
    bool bedDefensePanelEnabled = true;
    bool bedEspFilled = false;
    bool debugChatEnabled = true;
    bool showOwnBedDefenseInfo = true;
    bool showTeammateBoxes = true;
    bool bedDefenseHoldToShow = true;
    bool bedDefensePerspectiveScale = false;
    bool hypixelPanelHoldToShow = true;
    bool clickGuiLightTheme = false;
    bool nametagEnabled = true;
    bool nametagSidePlacement = false;
    bool enemyItemIndicatorsEnabled = true;
    bool showTeammateNametags = true;
    bool nametagNearbyEnemiesOnly = false;
    bool nametagTeamPulse = true;
    bool showTeammateArrows = true;
    bool safewalkEnabled = false;
    bool scaffoldEnabled = false;
    bool scaffoldSameLayerOnly = true;
    bool flyEnabled = false;
    bool bhopEnabled = false;
    bool bhopAutoJump = true;
    bool aimAssistEnabled = false;
    bool aimLockOnMode = false;
    bool aimSilentLock = false;
    bool silentFileDebug = false;
    bool silentChatDebug = false;
    bool aimScannerEnabled = true;
    bool aimAttackViability = true;
    // Independent opt-in for remapping vanilla movement/jump/sprint while
    // Silent Lock owns the logical rotation. Attack-ray policy must never
    // implicitly enable or disable this behaviour.
    bool silentControlAdaptation = false;
    // Rotates through attack-ready targets instead of waiting on one target's
    // hurt/click cooldown. One physical click still emits at most one attack.
    bool aimSequentialTargets = false;
    // Integrated-single-player only. The binding layer repeats this guard.
    bool bedBreakerEnabled = false;
    bool aimNearestPriority = true;
    bool textGuiEnabled = false;
    bool textGuiVerticalLine = true;
    bool textGuiShowModes = false;
    bool knockbackPredictionEnabled = false;
    bool bowPredictionEnabled = false;
    // These controls are enforced again inside GameBindings. They can never
    // execute unless Minecraft owns an integrated single-player server, and
    // the attack helper only accepts a living non-player hostile candidate.
    bool localMobAuraEnabled = false;
    bool localVelocityEnabled = false;
    bool fullscreenImeFixEnabled = false;
    bool fireballEspEnabled = false;
    bool fireballEspFilled = true;
    bool longJumpEnabled = false;
    bool freeLookEnabled = false;
    bool smartHotbarEnabled = false;
    bool smartHotbarRefill = false;
    bool sprintEnabled = false;
    bool nametagAlways = false;
    bool attackShieldEnabled = false;
    // The wildcard is safe to persist because it has no world/entity identity.
    // Specific UUID/entity selections remain tied to the current session.
    bool attackShieldWildcard = false;
    // High-risk movement helpers fail closed on Hypixel. This explicit,
    // persisted opt-in is intentionally separate from each feature switch so
    // an accidental hotkey press can never silently override the server guard.
    bool allowHypixelMovement = false;
    int bedDefenseRadius = 6;
    int bedThreatRadius = 8;
    int bedDefenseHotkey = VK_LMENU;
    int bedDefensePanelOpacity = 78;
    int hypixelPanelHotkey = VK_TAB;
    int hypixelPanelOpacity = 76;
    // The statistics card has an independent scale and normalized top-left
    // position so it remains usable when the Minecraft resolution changes.
    int hypixelPanelScale = 100;
    // Width and height are intentionally independent.  The legacy "scale"
    // value now controls width only; glyphs/row pitch remain fixed and crisp.
    int hypixelPanelHeight = 100;
    int hypixelPanelX = -1; // -1 = default right aligned; otherwise 0..1000
    int hypixelPanelY = -1; // -1 = default top aligned; otherwise 0..1000
    // Independent font preset (15/19/23/28 px). Panel resizing intentionally
    // does not scale glyphs, so rows remain readable and never overlap.
    int hypixelPanelFontIndex = 1;
    int nametagRange = 32;
    int nametagSizeIndex = 1;
    int safewalkReleaseDelayMs = 120;
    int safewalkEdgeSensitivity = 55;
    int safewalkMinimumPitch = -5;
    int safewalkHotkey = VK_F8;
    int flySpeedPercent = 100;
    int bhopAirSpeedPercent = 100;
    int longJumpSpeedPercent = 100;
    int aimSlowdownPercent = 45; // Reserved legacy wire slot; no sensitivity modification.
    int aimSpeedPercent = 35;
    int aimMinimumDistance = 0;
    int aimMaximumDistance = 16;
    int aimFovDegrees = 90;
    int aimAttackCps = 10;
    int textGuiAlignment = 2; // 0=left, 1=center, 2=right
    int localMobReach = 4;
    int localAttackDelayMs = 500;
    int localVelocityPercent = 100;
    int localVelocityProbability = 100;
    int localVelocityVerticalPercent = 100;
    int clickGuiWidthPercent = 100;
    int clickGuiHeightPercent = 100;
    int clickGuiOpacity = 96;
    int clickGuiBlur = 65;
    int imePanelX = -1;
    int imePanelY = -1;
    int textGuiX = -1;
    int textGuiY = -1;
    // Stored as 0xRRGGBB so the value is renderer-independent and can travel
    // through the text IPC protocol without floating-point round trips.
    std::uint32_t playerEspColor = 0xFF3B30U;
    std::uint32_t bedEspColor = 0xFF5C68U;
    std::uint32_t bedDefensePanelColor = 0x191621U;
    std::uint32_t hypixelPanelColor = 0x000000U;
    std::uint32_t hypixelRailColor = 0x825DE8U;
    std::uint32_t nametagPanelColor = 0x101218U;
    std::uint32_t clickGuiAccentColor = 0x825DE8U;
    std::uint32_t textGuiColor = 0x7EE7FFU;
    std::uint32_t fireballEspColor = 0xFF9D3DU;
    int nametagPanelOpacity = 82;
    int hypixelRailOpacity = 100;
    // Page master hotkeys in navigation order, excluding Interface. Zero is
    // deliberately "Unbound"; configured keys are persisted by Controller.
    std::array<int, FeatureHotkeyCount> featureHotkeys{};
    // Each logical Minecraft hotbar binding can become a category shortcut:
    // 0=None, 1=Sword, 2=Blocks. The physical key remains owned by Minecraft.
    std::array<int, 9U> smartHotbarActions{};

    [[nodiscard]] bool operator==(const FeatureSettings&) const noexcept = default;
};

struct HypixelOverlaySnapshot final {
    enum class State : std::uint8_t { Idle, Loading, Ready, Error };
    State state = State::Idle;
    std::array<char, 40U> uuid{};
    std::array<char, 48U> displayName{};
    std::array<char, 160U> status{};
    std::int64_t wins = 0;
    std::int64_t losses = 0;
    std::int64_t finalKills = 0;
    std::int64_t finalDeaths = 0;
    std::int64_t bedsBroken = 0;
    std::int64_t bedsLost = 0;
    double winRate = 0.0;
    double fkdr = 0.0;
};

struct PlayerStatsEntry final {
    std::array<char, 17U> name{};
    std::array<char, 5U> teamPrefix{};
    std::array<char, 97U> status{};
    std::int32_t stars = 0;
    double fkdr = 0.0;
    double wlr = 0.0;
    double bblr = 0.0;
    std::int64_t wins = 0;
    std::int64_t finalKills = 0;
    std::int64_t bedsBroken = 0;
    std::int32_t winStreak = 0;
    std::int32_t level = 0;
    bool failed = false;
};

struct PlayerStatsOverlaySnapshot final {
    static constexpr std::size_t Capacity = 64U;
    std::array<PlayerStatsEntry, Capacity> entries{};
    std::uint32_t count = 0U;
};

struct BlacklistEntry final {
    std::array<char, 50U> key{};
    std::array<char, 37U> uuid{};
    std::array<char, 17U> name{};
    std::array<char, 161U> reason{};
    std::array<char, 260U> facePath{};
    std::int64_t addedAt = 0;
    bool nick = false;
    bool idOnly = false;
    bool warnOnEncounter = true;
};

struct BlacklistOverlaySnapshot final {
    static constexpr std::size_t Capacity = 128U;
    static constexpr std::size_t PresetCapacity = 8U;
    std::array<BlacklistEntry, Capacity> entries{};
    std::uint32_t count = 0U;
    std::array<std::array<char, 81U>, PresetCapacity> presets{};
    std::uint32_t presetCount = 0U;
    bool panelEnabled = true;
    bool matchAlertsEnabled = true;
    bool allowIdOnlyNicks = true;
    bool showWithClickGui = true;
    bool collapsed = false;
    int panelOpacity = 82;
    int contentScale = 100;
    std::uint32_t panelColor = 0x111218U;
    int panelX = -1;
    int panelY = -1;
    int panelWidth = 100;
    int panelHeight = 100;
};

struct BlacklistAction final {
    enum class Type : std::uint8_t { None, Add, Remove, Warning, Layout, Settings };
    Type type = Type::None;
    std::array<char, 50U> key{};
    std::array<char, 37U> uuid{};
    std::array<char, 17U> name{};
    std::array<char, 161U> reason{};
    bool idOnlyNick = false;
    bool warnOnEncounter = true;
    int x = -1;
    int y = -1;
    int width = 100;
    int height = 100;
    bool panelEnabled = true;
    bool matchAlertsEnabled = true;
    bool allowIdOnlyNicks = true;
    bool showWithClickGui = true;
    bool collapsed = false;
    int panelOpacity = 82;
    int contentScale = 100;
    std::uint32_t panelColor = 0x111218U;
};

struct MediaOverlaySettings final {
    bool enabled = true;
    int opacity = 58;
    int spectrumOpacity = 100;
    // Continuous whole-card scale. Every internal coordinate is derived from
    // the same design-space multiplier so typography and controls stay aligned.
    int scalePercent = 52;
    int previousHotkey = VK_MEDIA_PREV_TRACK;
    int toggleHotkey = VK_MEDIA_PLAY_PAUSE;
    int nextHotkey = VK_MEDIA_NEXT_TRACK;
    std::uint32_t panelColor = 0x857F82U;
    int panelX = -1;
    int panelY = -1;
    [[nodiscard]] bool operator==(const MediaOverlaySettings&) const noexcept = default;
};

struct MediaPlaybackSnapshot final {
    bool available = false;
    bool playing = false;
    std::array<char, 192U> title{};
    std::array<char, 160U> artist{};
    std::array<char, 160U> source{};
    std::array<char, 512U> coverPath{};
    std::int64_t positionMs = 0;
    std::int64_t durationMs = 0;
    std::uint64_t receivedAtMs = 0U;
    std::array<float, 10U> spectrum{};
};

enum class MediaAction : std::uint8_t { None, Previous, Toggle, Next };

class OverlayRenderer final {
public:
    OverlayRenderer();
    ~OverlayRenderer();

    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    // Returns true exactly once after each successful HWND/HGLRC generation.
    [[nodiscard]] bool render(HDC deviceContext,
                              const GameSnapshot& snapshot,
                              bool interactive) noexcept;
    void shutdownWithCurrentContext() noexcept;
    void abandonAfterHookDisabled() noexcept;

    [[nodiscard]] bool initialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool ownsCurrentContext() const noexcept;
    [[nodiscard]] bool consumeClickGuiToggle() noexcept;
    void setFeatureSettings(const FeatureSettings& settings) noexcept;
    [[nodiscard]] bool consumeFeatureSettings(FeatureSettings& settings) noexcept;
    [[nodiscard]] int shieldAttacker(const GameSnapshot& snapshot, bool selectionOnly=false) const noexcept {
        if(!selectionOnly&&!m_features.attackShieldEnabled) return -1;
        if(m_features.attackShieldWildcard) return -2;
        if(m_shieldWorld!=snapshot.worldGeneration) return -1;
        for(std::uint32_t i=0;i<snapshot.entityMarkerCount;++i) {
            const auto& entity=snapshot.entityMarkers[i];
            if(entity.player&&entity.entityId==m_shieldSelectedId&&
               entity.uuid==m_shieldSelectedUuid) return entity.entityId;
        }
        return -1;
    }
    void setHypixelSnapshot(const HypixelOverlaySnapshot& snapshot) noexcept;
    void setPlayerStatsSnapshot(const PlayerStatsOverlaySnapshot& snapshot) noexcept;
    void setBlacklistSnapshot(const BlacklistOverlaySnapshot& snapshot) noexcept;
    void setMediaSnapshot(const MediaPlaybackSnapshot& snapshot) noexcept;
    void setMediaSettings(const MediaOverlaySettings& settings) noexcept;
    [[nodiscard]] bool consumeMediaSettings(MediaOverlaySettings& settings) noexcept;
    [[nodiscard]] MediaAction consumeMediaAction() noexcept;
    [[nodiscard]] bool consumeBlacklistAction(BlacklistAction& action) noexcept;
    [[nodiscard]] bool consumeHypixelQuery(std::array<char, 17U>& playerId) noexcept;
    void setMenuHotkey(unsigned virtualKey) noexcept;
    [[nodiscard]] bool consumeMenuHotkeyChange(unsigned& virtualKey) noexcept;
    void setGuiScaleIndex(int index) noexcept;
    [[nodiscard]] bool consumeGuiScaleChange(int& index) noexcept;
    [[nodiscard]] bool consumeBedRescanRequest() noexcept;
    void setGameScreenOpen(bool open) noexcept;

private:
    friend struct OverlayRendererTestAccess;
    struct RenderFrameContext;
    void renderWorldOverlay(RenderFrameContext& frame) noexcept;
    void renderClickGui(RenderFrameContext& frame) noexcept;
    void renderBlacklistAddDialog(RenderFrameContext& frame) noexcept;
    void renderTextGui(RenderFrameContext& frame) noexcept;
    void renderPlayerStatsPanel(RenderFrameContext& frame) noexcept;
    void renderBlacklistPanel(RenderFrameContext& frame) noexcept;

    [[nodiscard]] bool initialize(HWND window, HGLRC context) noexcept;
    void pollFallbackInput() noexcept;
    void captureBackdropTexture() noexcept;
    void renderInventoryBlur(float strength) noexcept;
    void renderImeOverlay(float deltaSeconds, float uiScale) noexcept;
    void renderMediaOverlay(float deltaSeconds, float uiScale, bool interactive) noexcept;
    void applyGuiScaleStyle(float scale, int fontIndex) noexcept;
    void enqueueFeatureToasts(const FeatureSettings& before,
                              const FeatureSettings& after) noexcept;
    void enqueueToast(const char* label, bool enabled) noexcept;
    void enqueueMessage(const char* message, bool positive) noexcept;
    void updateBedThreatAlerts(const GameSnapshot& snapshot) noexcept;
    void renderToasts(float deltaSeconds, float uiScale) noexcept;
    void abandonForContextChange() noexcept;
    void abandonAfterWndProcDrainTimeout() noexcept;
    static LRESULT handleWindowMessage(void* context,
                                       HWND window,
                                       UINT message,
                                       WPARAM wParam,
                                       LPARAM lParam,
                                       bool& handled) noexcept;
    [[nodiscard]] static LRESULT onWindowMessage(OverlayInputState& input,
                                                 HWND window,
                                                 UINT message,
                                                 WPARAM wParam,
                                                 LPARAM lParam,
                                                 bool& handled) noexcept;

    HWND m_window = nullptr;
    HGLRC m_glContext = nullptr;
    WndProcHook m_windowProcedure;
    ImGuiContext* m_imguiContext = nullptr;
    // Kept separate from OverlayRenderer so a bounded WndProc drain timeout
    // can intentionally retain this tiny bridge without retaining/dereferencing
    // a destroyed OverlayRenderer object.
    OverlayInputState* m_inputState = nullptr;
    bool m_initialized = false;
    bool m_permanentlyDisabled = false;
    bool m_wndProcFallbackLogged = false;
    bool m_cursorSessionActive = false;
    FeatureSettings m_features{};
    HypixelOverlaySnapshot m_hypixel{};
    PlayerStatsOverlaySnapshot m_playerStats{};
    BlacklistOverlaySnapshot m_blacklist{};
    MediaPlaybackSnapshot m_media{};
    MediaOverlaySettings m_mediaSettings{};
    MediaHotkeyEdges m_mediaKeyEdges;
    BlacklistAction m_blacklistAction{};
    std::uint64_t m_blacklistLayoutPendingUntil = 0U;
    std::uint64_t m_blacklistSettingsPendingUntil = 0U;
    std::array<char, 81U> m_blacklistSearch{};
    std::array<char, 50U> m_blacklistDeleteKey{};
    SmoothScroll m_navigationScroll;
    std::array<SmoothScroll, 26U> m_settingsScroll{};
    SmoothScroll m_blacklistScroll;
    bool m_blacklistActionDirty = false;
    bool m_featureSettingsDirty = false;
    int m_shieldSelectedId=-1;
    std::uint64_t m_shieldWorld=0U;
    std::array<char,37U> m_shieldSelectedUuid{};
    bool m_hypixelQueryPending = false;
    std::array<char, 17U> m_hypixelQuery{};
    std::array<char, 17U> m_hypixelInput{};
    float m_clickGuiProgress = 0.0F;
    float m_clickGuiVelocity = 0.0F;
    float m_statsPanelProgress = 0.0F;
    float m_statsPanelVelocity = 0.0F;
    float m_blacklistPanelProgress = 0.0F;
    float m_blacklistPanelVelocity = 0.0F;
    float m_toggleAnimation[64]{};
    float m_clickGuiX = -9999.0F;
    float m_clickGuiY = 18.0F;
    double m_lastBedRefreshTime = 0.0;
    int m_guiScaleIndex = 1;
    int m_appliedGuiScaleIndex = -1;
    float m_animatedGuiScale = 1.25F;
    std::array<ImFont*, 4U> m_fonts{};
    std::array<ImFont*, 4U> m_boldFonts{};
    ImFont* m_imeFont = nullptr;
    // A single, card-local font atlas entry covers Chinese and Japanese media
    // metadata. It is never installed as ImGui's default GUI font.
    ImFont* m_mediaFont = nullptr;
    unsigned m_menuHotkey = VK_OEM_7;
    bool m_menuHotkeyDirty = false;
    bool m_guiScaleDirty = false;
    bool m_bedRescanPending = false;
    bool m_waitingForHotkey = false;
    int m_hotkeyCaptureCooldownFrames = 0;
    bool m_hotkeyCaptureArmed = false;
    int m_hotkeyCaptureTarget = 0; // 1=menu, 2=bed, 3=stats, 4=safewalk, 5..7=media
    int m_clickGuiPage = 0;
    int m_previousClickGuiPage = 0;
    float m_clickGuiPageProgress = 1.0F;
    float m_clickGuiNavPosition = 0.0F;
    std::array<char, 96U> m_featureSearch{};
    float m_searchIslandProgress = 0.0F;
    float m_searchTransitionFrom = 0.0F;
    float m_searchTransitionTarget = 0.0F;
    double m_searchTransitionStartedAt = 0.0;
    double m_searchActivationStartedAt = 0.0;
    double m_searchLoadingStartedAt = 0.0;
    float m_searchClearProgress = 0.0F;
    float m_searchHoverProgress = 0.0F;
    bool m_searchInputActive = false;
    bool m_searchIslandOpen = false;
    bool m_searchFocusRequested = false;
    std::array<float, 26U> m_clickGuiNavHover{};
    bool m_mediaSettingsDirty = false;
    MediaAction m_mediaAction = MediaAction::None;
    float m_mediaPanelProgress = 0.0F;
    float m_mediaPanelVelocity = 0.0F;
    bool m_mediaDragging = false;
    float m_mediaDragOffsetX = 0.0F;
    float m_mediaDragOffsetY = 0.0F;
    std::array<bool, 3U> m_mediaHotkeyWasDown{};
    unsigned m_mediaCoverTexture = 0U;
    unsigned m_mediaPreviousCoverTexture = 0U;
    unsigned m_mediaKeycapTexture = 0U;
    const char* m_mediaKeycapAtlasXml = nullptr;
    std::size_t m_mediaKeycapAtlasXmlSize = 0U;
    int m_mediaCoverWidth = 0;
    int m_mediaCoverHeight = 0;
    std::array<char, 512U> m_mediaLoadedCoverPath{};
    std::array<char, 192U> m_mediaLoadedTitle{};
    std::array<char, 160U> m_mediaLoadedArtist{};
    std::array<char, 192U> m_mediaPreviousTitle{};
    std::array<char, 160U> m_mediaPreviousArtist{};
    std::array<float, 10U> m_mediaSpectrumDisplay{};
    std::array<float, 3U> m_mediaAccent{{0.36F,0.70F,1.0F}};
    float m_mediaTrackProgress = 1.0F;
    float m_mediaTrackVelocity = 0.0F;
    float m_mediaAnimatedWidth = 0.0F;
    float m_mediaWidthVelocity = 0.0F;
    float m_mediaPlayMorph = 0.0F;
    float m_mediaPlayMorphVelocity = 0.0F;
    std::array<bool, 4U> m_aimSectionOpen{{true, true, true, false}};
    std::array<float, 4U> m_aimSectionProgress{{1.0F, 1.0F, 1.0F, 0.0F}};
    std::array<float, 4U> m_aimSectionVelocity{};
    // Natural expanded body heights are measured from real ImGui content.
    // Keeping them separate from animation progress prevents fixed-height
    // cards from clipping wrapped text or leaving text exactly on the border.
    std::array<float, 4U> m_aimSectionBodyHeight{};
    std::uint64_t m_mediaElapsedClockTick = 0U;
    std::int64_t m_mediaElapsedFallbackMs = 0;
    int m_mediaSlideDirection = -1;
    std::uint64_t m_lastImeRevision = 0U;
    std::uint64_t m_lastImeActivityTick = 0U;
    float m_imePanelProgress = 0.0F;
    bool m_imePositionEditing = false;
    bool m_imeDragging = false;
    float m_imeEditX = 0.0F;
    float m_imeEditY = 0.0F;
    float m_clickGuiThemeProgress = 0.0F;
    std::array<float,26U> m_clickGuiNavSelection{};
    std::array<float,26U> m_clickGuiNavEnabled{};
    bool m_statsPanelTransformDirty = false;
    bool m_statsPanelDragging = false;
    bool m_statsPanelResizing = false;
    float m_statsPanelResizeStartX = 0.0F;
    float m_statsPanelResizeStartY = 0.0F;
    int m_statsPanelResizeStartScale = 100;
    int m_statsPanelResizeStartHeight = 100;
    float m_statsPanelDragStartMouseX = 0.0F;
    float m_statsPanelDragStartMouseY = 0.0F;
    float m_statsPanelDragStartPanelX = 0.0F;
    float m_statsPanelDragStartPanelY = 0.0F;
    bool m_blacklistAddOpen = false;
    float m_blacklistAddProgress = 0.0F;
    float m_blacklistAddVelocity = 0.0F;
    int m_blacklistSelectedPlayer = -1;
    std::array<char, 161U> m_blacklistReasonInput{};
    bool m_blacklistIdOnlyNick = false;
    bool m_blacklistWarnOnEncounter = true;
    bool m_blacklistPanelDragging = false;
    bool m_blacklistPanelResizing = false;
    bool m_blacklistPanelTransformDirty = false;
    float m_blacklistDragStartMouseX = 0.0F;
    float m_blacklistDragStartMouseY = 0.0F;
    float m_blacklistDragStartPanelX = 0.0F;
    float m_blacklistDragStartPanelY = 0.0F;
    float m_blacklistResizeStartMouseX = 0.0F;
    float m_blacklistResizeStartMouseY = 0.0F;
    int m_blacklistResizeStartWidth = 100;
    int m_blacklistResizeStartHeight = 100;
    bool m_safewalkHotkeyWasDown = false;
    std::array<bool, FeatureSettings::FeatureHotkeyCount> m_featureHotkeyWasDown{};
    std::array<float, 22U> m_textGuiModuleProgress{};
    std::array<float, 22U> m_textGuiModuleVelocity{};
    std::array<std::array<float, 32U>, 22U> m_textGuiGlyphBrightness{};
    std::array<std::array<float, 32U>, 22U> m_textGuiGlyphTargets{};
    std::uint64_t m_textGuiNextShuffleTick = 0U;
    bool m_textGuiGlyphsInitialized = false;
    bool m_scaffoldBlockedNoticeShown = false;
    bool m_textGuiDragging = false;
    float m_textGuiDragOffsetX = 0.0F;
    float m_textGuiDragOffsetY = 0.0F;
    struct BlacklistTexture final {
        std::array<char, 260U> path{};
        unsigned texture = 0U;
    };
    std::array<BlacklistTexture, 128U> m_blacklistTextures{};
    bool m_blacklistMatchWasActive = false;
    std::array<std::array<char, 50U>, 128U> m_blacklistWarnedKeys{};
    std::uint32_t m_blacklistWarnedCount = 0U;
    bool m_featureSnapshotInitialized = false;
    struct Toast final {
        std::array<char, 96U> label{};
        float age = 0.0F;
        std::uint64_t sequence = 0U;
        bool enabled = false;
        bool active = false;
    };
    std::array<Toast, 6U> m_toasts{};
    std::uint64_t m_toastSequence = 0U;
    struct ThreatContact final {
        jint entityId = -1;
        std::array<char, 17U> playerName{};
        std::array<char, 37U> uuid{};
        char teamColor = 'u';
        int bedX = 0;
        int bedY = 0;
        int bedZ = 0;
        double distance = 0.0;
        std::uint64_t lastSeenTick = 0U;
        bool inside = false;
        bool invisible = false;
        std::uint32_t skinTextureId = 0U;
        float presentation = 0.0F;
    };
    std::array<ThreatContact, 64U> m_threatContacts{};
    struct NametagAnimation final {
        jint entityId = -1;
        float displayedHealth = 0.0F;
        std::uint64_t lastSeenTick = 0U;
    };
    std::array<NametagAnimation, GameSnapshot::MaxEntityMarkers> m_nametagAnimations{};
    struct KnockbackVisual final {
        KnockbackTrajectory trajectory{};
        double startedAt = 0.0;
        double updatedAt = 0.0;
        bool active = false;
    };
    std::array<KnockbackVisual, GameSnapshot::MaxKnockbackTrajectories>
        m_knockbackVisuals{};
    std::uint64_t m_lastKnockbackGeneration = 0U;
    std::uint64_t m_lastEntitySampleGeneration = 0U;
    float m_lastEntityPartialTicks = 0.0F;
    unsigned m_missedEntityTicks = 0U;
    unsigned m_blurTexture = 0U;
    GaussianBlur m_gaussianBlur;
    unsigned m_bedTexture = 0U;
    unsigned m_blockTextures[6]{};
    int m_blurWidth = 0;
    int m_blurHeight = 0;
    bool m_backdropCapturedThisFrame = false;

};

} // namespace mcoverlay
