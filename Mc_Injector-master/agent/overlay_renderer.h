#pragma once
#include "MediaHotkeyEdges.h"

#include "bindings/GameBindings.h"
#include "wndproc_hook.h"
#include "UiPreferences.h"
#include "UiMotion.h"
#include "ui/UiModel.h"
#include "ui/GuiDesignState.h"
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
    void setGuiTypography(ui::GuiTypography value) noexcept;
    [[nodiscard]] bool consumeGuiTypographyChange(ui::GuiTypography& value) noexcept;
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
    ImFont* m_semiboldFont=nullptr;
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
    ui::ClickGuiDesignState m_guiDesign;
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
