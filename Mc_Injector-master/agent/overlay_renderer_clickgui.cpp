#include "overlay_renderer_internal.h"
#include "ui/ClickGui.h"
#include <algorithm>
#include <array>
namespace mcoverlay {
void OverlayRenderer::renderClickGui(RenderFrameContext& frame) noexcept {
    std::array<ui::ClickGuiPlayer, GameSnapshot::MaxEntityMarkers> players{};
    const auto count=std::min<std::size_t>(frame.snapshot.entityMarkerCount,players.size());
    for(std::size_t i=0;i<count;++i) {
        const auto& e=frame.snapshot.entityMarkers[i];
        players[i]={e.entityId,e.player,e.playerName,e.displayName,e.uuid};
    }
    const auto& s=frame.snapshot;
    ui::ClickGuiSnapshot snapshot{s.hypixelServer,s.integratedSinglePlayer,
        s.silentAimAvailable,s.knockbackHurtAvailable,s.knockbackDamageEvents,
        s.knockbackImpulseEvents,s.knockbackConfirmedEvents,s.worldGeneration,
        static_cast<std::uint32_t>(count),{players.data(),count}};
    ui::ClickGuiFrame portable{snapshot,frame.interactive,frame.io,frame.uiScale,frame.delta,frame.guiEase};
    portable.typefaces={m_fonts[0],m_semiboldFont,m_boldFonts[0]};
    ui::ClickGuiRefs refs{
        m_aimSectionBodyHeight,
        m_aimSectionOpen,
        m_aimSectionProgress,
        m_aimSectionVelocity,
        m_bedRescanPending,
        m_blacklist,
        m_blacklistAction,
        m_blacklistActionDirty,
        m_blacklistAddOpen,
        m_boldFonts,
        m_clickGuiNavEnabled,
        m_clickGuiNavHover,
        m_clickGuiNavPosition,
        m_clickGuiNavSelection,
        m_clickGuiPage,
        m_clickGuiPageProgress,
        m_clickGuiProgress,
        m_clickGuiThemeProgress,
        m_clickGuiX,
        m_clickGuiY,
        m_featureSearch,
        m_featureSettingsDirty,
        m_features,
        m_guiScaleDirty,
        m_guiScaleIndex,
        m_hotkeyCaptureArmed,
        m_hotkeyCaptureCooldownFrames,
        m_hotkeyCaptureTarget,
        m_hypixelInput,
        m_hypixelQuery,
        m_hypixelQueryPending,
        m_imePositionEditing,
        m_mediaSettings,
        m_mediaSettingsDirty,
        m_menuHotkey,
        m_menuHotkeyDirty,
        m_navigationScroll,
        m_previousClickGuiPage,
        m_searchActivationStartedAt,
        m_searchClearProgress,
        m_searchFocusRequested,
        m_searchHoverProgress,
        m_searchInputActive,
        m_searchIslandOpen,
        m_searchIslandProgress,
        m_searchLoadingStartedAt,
        m_searchTransitionFrom,
        m_searchTransitionStartedAt,
        m_searchTransitionTarget,
        m_settingsScroll,
        m_shieldSelectedId,
        m_shieldSelectedUuid,
        m_shieldWorld,
        m_toggleAnimation,
        m_waitingForHotkey,
        m_guiDesign
    };
    ui::ClickGuiHost host;
    host.captureEnabled=[this](bool enabled) {m_inputState->captureHotkey.store(enabled,std::memory_order_release);};
    host.consumeCapturedHotkey=[this]() {return m_inputState->capturedHotkey.exchange(0U,std::memory_order_acq_rel);};
    host.mouseHeld=[] {return (::GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0 ||
        (::GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0 || (::GetAsyncKeyState(VK_MBUTTON)&0x8000)!=0 ||
        (::GetAsyncKeyState(VK_XBUTTON1)&0x8000)!=0 || (::GetAsyncKeyState(VK_XBUTTON2)&0x8000)!=0;};
    host.menuHotkeyChanged=[this](unsigned key) {setMenuHotkey(key);};
    host.keyName=[](unsigned key) {return renderer_detail::hotkeyName(key);};
    host.message=[this](const char* text,bool success) {enqueueMessage(text,success);};
    host.featureToasts=[this](const FeatureSettings& before,const FeatureSettings& after) {enqueueFeatureToasts(before,after);};
    host.transientBlur=[](ImDrawList* draw,float amount,float scale) {renderer_detail::appendTransientSoftBlur(draw,amount,scale);};
    ui::renderClickGui(refs,portable,host);
    frame.theme=portable.theme;
    frame.guiSurface=portable.guiSurface; frame.guiRail=portable.guiRail;
    frame.guiText=portable.guiText; frame.guiMuted=portable.guiMuted; frame.guiFrame=portable.guiFrame;
    frame.guiScrollbarTrack=portable.guiScrollbarTrack; frame.guiScrollbarGrab=portable.guiScrollbarGrab;
    frame.guiAccent=portable.guiAccent; frame.guiSelected=portable.guiSelected;
}
} // namespace mcoverlay
