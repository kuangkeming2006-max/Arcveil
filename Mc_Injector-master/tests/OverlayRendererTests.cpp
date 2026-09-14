#include "overlay_renderer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <QImage>
#include <QDir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace mcoverlay {
struct OverlayRendererTestAccess {
    static ImGuiContext* context(OverlayRenderer& r) { return r.m_imguiContext; }
    static void prepare(OverlayRenderer& r, int page, int scale, bool light) {
        r.m_clickGuiPage = page;
        r.m_clickGuiProgress = r.m_clickGuiPageProgress = 1;
        r.m_clickGuiVelocity = 0;
        r.m_clickGuiNavPosition = 0;
        r.m_guiScaleIndex = scale;
        r.m_animatedGuiScale = 1.0F + 0.25F * static_cast<float>(scale);
        r.m_clickGuiThemeProgress = light ? 1.0F : 0.0F;
        r.m_features.clickGuiLightTheme = light;
        r.m_features.clickGuiHeightPercent = 80;
        r.m_features.clickGuiBlur = 65;
        r.m_clickGuiX = r.m_clickGuiY = 24;
        r.m_blacklist.panelEnabled = r.m_blacklist.showWithClickGui = false;
    }
    static void imePreview(OverlayRenderer& r) {
        r.m_features.fullscreenImeFixEnabled = true;
        r.m_imePositionEditing = true;
        r.m_imePanelProgress = 1;
        r.m_clickGuiProgress = r.m_clickGuiVelocity = 0;
    }
    static void freeLookPreview(OverlayRenderer& r) {
        r.m_features.freeLookEnabled=true;
        r.m_features.featureHotkeys[17U]=VK_LMENU;
        r.m_toggleAnimation[54]=1.0F;
        // The test selects pages directly instead of clicking the navigation
        // row, so place the rail where a real click would already have left it.
        r.m_navigationScroll.current=240.0F;
        r.m_navigationScroll.target=240.0F;
    }
    static void predictionOnly(OverlayRenderer& r) {
        r.m_imePositionEditing = false;
        r.m_features.fullscreenImeFixEnabled = false;
        r.m_features.knockbackPredictionEnabled = true;
        r.m_features.bowPredictionEnabled = false;
        r.m_blacklist.panelEnabled = r.m_blacklist.showWithClickGui = false;
        r.m_clickGuiProgress = r.m_clickGuiVelocity = 0;
        r.m_imePanelProgress = 0;
        r.m_blacklistPanelProgress = r.m_blacklistPanelVelocity = 0;
    }
    static bool predictionActive(OverlayRenderer& r) {
        return std::any_of(r.m_knockbackVisuals.begin(), r.m_knockbackVisuals.end(),
            [](const auto& visual) { return visual.active; });
    }
    static bool mediaGlyph(OverlayRenderer& r, ImWchar codepoint) {
        return r.m_mediaFont != nullptr && r.m_mediaFont->IsGlyphInFont(codepoint);
    }
    static bool mediaKeycapsReady(OverlayRenderer& r) {
        return r.m_mediaKeycapTexture != 0U && r.m_mediaKeycapAtlasXml != nullptr &&
            r.m_mediaKeycapAtlasXmlSize > 1000U;
    }
    static void aimMode(OverlayRenderer& r,bool lock) {
        r.m_features.aimLockOnMode=lock;
        if(!lock) r.m_features.aimSilentLock=false;
    }
    static void aimSection(OverlayRenderer& r,std::size_t index,bool open,
                           float phase,float bodyHeight) {
        r.m_aimSectionOpen[index]=open;
        r.m_aimSectionProgress[index]=phase;
        r.m_aimSectionBodyHeight[index]=bodyHeight;
    }
    static float aimSectionPhase(OverlayRenderer& r,std::size_t index) {
        return r.m_aimSectionProgress[index];
    }
    static float aimSectionBodyHeight(OverlayRenderer& r,std::size_t index) {
        return r.m_aimSectionBodyHeight[index];
    }
    static void media(OverlayRenderer& r,const char* title,const char* cover,bool available=true) {
        r.m_mediaSettings.enabled=true; r.m_mediaSettings.opacity=70;
        r.m_mediaSettings.previousHotkey='O'; r.m_mediaSettings.toggleHotkey=VK_END;
        r.m_mediaSettings.nextHotkey=VK_NEXT;
        r.m_media.available=available; r.m_media.playing=true;
        std::snprintf(r.m_media.title.data(),r.m_media.title.size(),"%s",title);
        std::snprintf(r.m_media.artist.data(),r.m_media.artist.size(),"MIMI");
        std::snprintf(r.m_media.coverPath.data(),r.m_media.coverPath.size(),"%s",cover);
        r.m_media.durationMs=0;
        for(std::size_t i=0;i<r.m_media.spectrum.size();++i)
            r.m_media.spectrum[i]=0.4F+0.25F*std::sin(static_cast<float>(i));
        r.m_mediaPanelProgress=1; r.m_mediaPanelVelocity=0;
    }
    static float mediaProgress(OverlayRenderer& r) {return r.m_mediaTrackProgress;}
    static void settleTrack(OverlayRenderer& r) {r.m_mediaTrackProgress=0.8F;r.m_mediaTrackVelocity=0;}
    static void stableMedia(OverlayRenderer& r) {
        r.m_mediaTrackProgress=1;r.m_mediaTrackVelocity=0;
        r.m_mediaPlayMorph=1;r.m_mediaPlayMorphVelocity=0;
    }
    static void hudOnly(OverlayRenderer& r) {r.m_clickGuiProgress=r.m_clickGuiVelocity=0;}
    static int selectedPage(OverlayRenderer& r) {return r.m_clickGuiPage;}
    static void noMedia(OverlayRenderer& r) {r.m_mediaSettings.enabled=false;r.m_mediaPanelProgress=0;}
    static void search(OverlayRenderer& r,const char* text) {
        std::snprintf(r.m_featureSearch.data(),r.m_featureSearch.size(),"%s",text);
        r.m_searchIslandOpen=text[0]!='\0';
        r.m_searchIslandProgress=text[0]!='\0'?1.0F:0.0F;
        r.m_searchTransitionFrom=r.m_searchIslandProgress;
        r.m_searchTransitionTarget=r.m_searchIslandProgress;
        r.m_searchTransitionStartedAt=ImGui::GetTime()-1.0;
        r.m_searchLoadingStartedAt=ImGui::GetTime();
        r.m_navigationScroll={};
    }
    static void bowOnly(OverlayRenderer& r) {
        predictionOnly(r);
        r.m_features.knockbackPredictionEnabled=false;
        r.m_features.bowPredictionEnabled=true;
        r.m_features.entityEspEnabled=false;
        r.m_features.nametagEnabled=false;
        r.m_features.bedEspEnabled=false;
        r.m_features.aimAssistEnabled=false;
        r.m_knockbackVisuals={};
    }
    template<class Check> static void synchronization(OverlayRenderer& r, Check check) {
        auto previous = r.m_features;
        r.m_imePositionEditing = true;
        r.m_features.imePanelX = 191; r.m_features.imePanelY = 123;
        // Runtime publishes its pre-drag snapshot every frame, including after
        // mouse-up. Preview coordinates must survive until Done/Save.
        r.m_imeDragging = true;
        r.setFeatureSettings(previous);
        r.m_imeDragging = false;
        for (int i=0;i<10;++i) r.setFeatureSettings(previous);
        check(r.m_features.imePanelX==191 && r.m_features.imePanelY==123,
              "IME preview survives stale settings after mouse release");
        r.m_imePositionEditing = false;
        r.m_featureSettingsDirty = true;
        FeatureSettings saved;
        check(r.consumeFeatureSettings(saved) && saved.imePanelX==191 && saved.imePanelY==123,
              "IME Done publishes the dragged position");
        r.setFeatureSettings(saved);
        check(r.m_features.imePanelX==191,"IME position remains after runtime echo");

        auto old = std::make_unique<BlacklistOverlaySnapshot>();
        r.setBlacklistSnapshot(*old);
        r.m_blacklist.panelX=212; r.m_blacklist.panelY=137;
        r.m_blacklist.panelWidth=140; r.m_blacklist.panelHeight=180;
        r.m_blacklistAction.type=BlacklistAction::Type::Layout;
        r.m_blacklistActionDirty=true;
        BlacklistAction action;
        check(r.consumeBlacklistAction(action), "layout action published");
        old->count=1;
        r.setBlacklistSnapshot(*old);
        check(r.m_blacklist.panelX==212 && r.m_blacklist.panelHeight==180 && r.m_blacklist.count==1,
              "stale IPC layout cannot undo a drag; new entries still merge");
        old->panelX=212; old->panelY=137; old->panelWidth=140; old->panelHeight=180;
        r.setBlacklistSnapshot(*old);
        check(!r.m_blacklistLayoutPendingUntil,"matching layout echo acknowledges drag");
        old->panelX=400;
        r.setBlacklistSnapshot(*old);
        check(r.m_blacklist.panelX==400,"controller edits work after drag acknowledgement");
        r.m_blacklist.collapsed=true;
        r.m_blacklist.contentScale=165;
        r.m_blacklistAction.type=BlacklistAction::Type::Settings;
        r.m_blacklistAction.contentScale=r.m_blacklist.contentScale;
        r.m_blacklistActionDirty=true;
        check(r.consumeBlacklistAction(action), "settings action published");
        check(action.contentScale==165, "blacklist content scale is published");
        r.setBlacklistSnapshot(*old);
        check(r.m_blacklist.collapsed && r.m_blacklist.contentScale==165,
              "blacklist settings survive stale IPC echo");
        old->collapsed=true; old->contentScale=165;
        r.setBlacklistSnapshot(*old);
        check(!r.m_blacklistSettingsPendingUntil,"collapse echo acknowledged");
        r.m_blacklist = {};
    }
    static void blacklist(OverlayRenderer& r, unsigned count, bool collapsed, bool light) {
        r.m_imePositionEditing = false;
        r.m_blacklist.panelEnabled = true;
        r.m_blacklist.showWithClickGui = true;
        r.m_blacklist.collapsed = collapsed;
        r.m_blacklist.panelWidth = 110;
        r.m_blacklist.panelHeight = 120;
        r.m_blacklist.panelX = 20;
        r.m_blacklist.panelY = 20;
        r.m_blacklist.panelColor = light ? 0xF9F9FF : 0x111218;
        r.m_blacklist.panelOpacity = 96;
        r.m_blacklist.contentScale = 150;
        r.m_blacklistPanelProgress = 1;
        r.m_blacklistPanelVelocity = 0;
        r.m_blacklist.count = count;
        for (unsigned i=0;i<count;++i) {
            auto& entry=r.m_blacklist.entries[i];
            std::snprintf(entry.key.data(),entry.key.size(),"uuid-%u",i);
            std::snprintf(entry.name.data(),entry.name.size(),"PlayerName_%04u",i);
            std::snprintf(entry.reason.data(),entry.reason.size(),"%s", i%2 ? "Repeated harassment in chat; keep this note for later review." : "Suspicious behaviour");
            entry.nick = i%3==0; entry.addedAt=1788170400000;
        }
    }
    static void blacklistModal(OverlayRenderer& r, GameSnapshot& snapshot) {
        r.m_blacklist.panelEnabled = false;
        r.m_blacklist.showWithClickGui = false;
        r.m_blacklist.allowIdOnlyNicks = true;
        r.m_blacklist.presetCount = 3;
        std::snprintf(r.m_blacklist.presets[0].data(),
            r.m_blacklist.presets[0].size(),"Suspicious behaviour");
        std::snprintf(r.m_blacklist.presets[1].data(),
            r.m_blacklist.presets[1].size(),"Chat harassment");
        std::snprintf(r.m_blacklist.presets[2].data(),
            r.m_blacklist.presets[2].size(),"Review later");
        snapshot.playerCount = 3;
        std::snprintf(snapshot.players[0].name.data(),
            snapshot.players[0].name.size(),"RecentPlayer");
        std::snprintf(snapshot.players[0].uuid.data(),
            snapshot.players[0].uuid.size(),"25f61a15-93b6-4f4e-bf88-6d83335ed114");
        std::snprintf(snapshot.players[1].name.data(),
            snapshot.players[1].name.size(),"BedwarsNick");
        snapshot.players[1].uuid[0] = '\0';
        std::snprintf(snapshot.players[2].name.data(),
            snapshot.players[2].name.size(),"LongNamePlayer16");
        std::snprintf(snapshot.players[2].uuid.data(),
            snapshot.players[2].uuid.size(),"44aa27dd-2cb7-40bd-a98d-bb686925132c");
        r.m_blacklistSelectedPlayer = 0;
        std::snprintf(r.m_blacklistReasonInput.data(),
            r.m_blacklistReasonInput.size(),"Suspicious behaviour observed in the last match.");
        r.m_blacklistWarnOnEncounter = true;
        r.m_blacklistIdOnlyNick = false;
        r.m_blacklistAddOpen = true;
        r.m_blacklistAddProgress = 1.0F;
        r.m_blacklistAddVelocity = 0.0F;
    }
};
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
    };
    WNDCLASSW wc{};
    wc.style = CS_OWNDC; wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"McOverlayRendererTest";
    RegisterClassW(&wc);
    // Offscreen, nonactivating test drawable. The production renderer correctly
    // skips WS_VISIBLE=false windows; never take focus from the user's game.
    HWND window = CreateWindowW(wc.lpszClassName, L"Renderer regression", WS_POPUP,
        -30000, -30000, 1600, 1200, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 2;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format); format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32;
    format.cDepthBits = 24; format.cStencilBits = 8;
    int pixelFormat = ChoosePixelFormat(dc, &format);
    if (!pixelFormat || !SetPixelFormat(dc, pixelFormat, &format)) return 3;
    HGLRC context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) return 4;
    std::printf("GL: %s\n", glGetString(GL_VERSION));
    const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    QDir().mkpath(output);
    const auto saveFrame = [&](const char* filename) {
        QImage frame(1600, 1200, QImage::Format_RGBA8888);
        glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, 1600, 1200, GL_RGBA, GL_UNSIGNED_BYTE, frame.bits());
        check(frame.flipped(Qt::Vertical).save(output + "/" + filename), "save regression image");
    };

    // Symmetric bright square must convolve symmetrically on both axes.
    // Also assert preservation of game depth/scissor/viewport state.
    GLuint source = 0;
    glGenTextures(1, &source); glBindTexture(GL_TEXTURE_2D, source);
    constexpr int n = 128;
    std::vector<unsigned char> pixels(n*n*4, 0);
    for (int y=0; y<n; ++y) for (int x=0; x<n; ++x) {
        auto p = static_cast<std::size_t>((y*n+x)*4);
        pixels[p+3] = 255;
        if (x>=48 && x<80 && y>=48 && y<80)
            pixels[p] = pixels[p+1] = pixels[p+2] = 255;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,n,n,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    mcoverlay::GaussianBlur blur;
    glViewport(7, 9, 300, 200); glEnable(GL_DEPTH_TEST); glEnable(GL_SCISSOR_TEST);
    glScissor(5, 6, 7, 8);
    check(blur.draw(source,n,n,3.0F,1.0F), "Gaussian shader/FBO draws successfully");
    GLint viewport[4]{}; glGetIntegerv(GL_VIEWPORT,viewport);
    check(viewport[0]==7 && viewport[1]==9 && viewport[2]==300 && viewport[3]==200,
        "Gaussian restores viewport");
    check(glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_SCISSOR_TEST), "Gaussian restores enable state");
    glReadBuffer(GL_BACK); glReadPixels(0,0,n,n,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    int asymmetry = 0;
    for (int y=16; y<112; ++y) for (int x=16; x<112; ++x)
        asymmetry = std::max(asymmetry, std::abs(int(pixels[(y*n+x)*4])-int(pixels[(x*n+y)*4])));
    check(asymmetry<=2, "Gaussian X/Y convolution is symmetric");
    check(pixels[(64*n+42)*4]>2 && pixels[(64*n+64)*4]>50, "Gaussian spreads actual pixels, not no-op");
    check(glGetError()==GL_NO_ERROR,"Gaussian leaves no GL error");
    blur.release(); glDeleteTextures(1,&source);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);

    auto renderer = std::make_unique<mcoverlay::OverlayRenderer>();
    auto snapshot = std::make_unique<mcoverlay::GameSnapshot>();
    snapshot->gameScreenOpen = false;
    auto frame = [&](bool open) {
        glViewport(0,0,1600,1200);
        glClearColor(0.18F,0.27F,0.34F,1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        (void)renderer->render(dc,*snapshot,open);
        auto* gui = mcoverlay::OverlayRendererTestAccess::context(*renderer);
        check(renderer->initialized() && gui, "renderer initializes");
        if (!gui) return;
        ImGui::SetCurrentContext(gui);
        check(gui->ColorStack.Size==0,
              "renderer balances the ImGui color stack every frame");
        check(gui->StyleVarStack.Size==0,
              "renderer balances the ImGui style stack every frame");
        check(gui->ColorStack.Size==0 && gui->StyleVarStack.Size==0, "all ImGui style stacks balanced");
        check(ImGui::GetDrawData() && ImGui::GetDrawData()->Valid, "frame draw data valid");
        check(glGetError()==GL_NO_ERROR,"frame GL state valid");
    };
    frame(false);
    check(mcoverlay::OverlayRendererTestAccess::mediaGlyph(*renderer, 0x4E2D),
          "Now Playing font contains Chinese glyphs");
    check(mcoverlay::OverlayRendererTestAccess::mediaGlyph(*renderer, 0x3042),
          "Now Playing font contains Japanese glyphs");
    check(mcoverlay::OverlayRendererTestAccess::mediaKeycapsReady(*renderer),
          "Kenney keyboard atlas and metadata load from embedded resources");
    mcoverlay::OverlayRendererTestAccess::synchronization(*renderer, check);
    {
        mcoverlay::MediaHotkeyEdges keys;
        check(!keys.update(0,'K',false,true),"media hotkey primes released state");
        check(keys.update(0,'K',true,true),"custom media hotkey dispatches without WndProc delivery");
        check(!keys.update(0,'K',true,true),"held media hotkey never repeats");
        check(!keys.update(0,'K',false,false),"chat/rebinding blocks media action");
        check(!keys.update(0,'K',true,false),"blocked key press is observed but not dispatched");
        check(!keys.update(0,'K',true,true),"closing chat while holding key creates no stale action");
        check(!keys.update(0,'J',true,true),"changing binding while held creates no action");
        (void)keys.update(0,'J',false,true);
        check(keys.update(0,'J',true,true),"new binding works on next physical edge");
        (void)keys.update(0,VK_MEDIA_PLAY_PAUSE,false,true);
        check(!keys.update(0,VK_MEDIA_PLAY_PAUSE,true,true),"native media key is not double-toggled");
        check(!keys.update(0,0,true,true),"None binding is inert");
        QImage art(64,64,QImage::Format_RGBA8888); art.fill(QColor(160,94,230));
        const QByteArray cover=(output+"/media-test-cover.png").toLocal8Bit();
        check(art.save(QString::fromLocal8Bit(cover)),"create deterministic media texture fixture");
        auto& access=mcoverlay::OverlayRendererTestAccess::media;
        access(*renderer,"Song A","",true); frame(false);
        access(*renderer,"\xE3\x81\xA0\xE3\x81\x8D\xE3\x81\x97\xE3\x82\x81\xE3\x82\x8B\xE3\x81\xBE\xE3\x81\xA7\xE3\x80\x82","",true); frame(false);
        check(mcoverlay::OverlayRendererTestAccess::mediaProgress(*renderer)<0.5F,
              "one new title starts a track transition");
        mcoverlay::OverlayRendererTestAccess::settleTrack(*renderer);
        access(*renderer,"\xE3\x81\xA0\xE3\x81\x8D\xE3\x81\x97\xE3\x82\x81\xE3\x82\x8B\xE3\x81\xBE\xE3\x81\xA7\xE3\x80\x82",cover.constData(),true);
        frame(false);
        check(mcoverlay::OverlayRendererTestAccess::mediaProgress(*renderer)>=0.8F,
              "late cover does not replay the song-change animation");
        for(int i=0;i<35;++i) frame(false);
        mcoverlay::OverlayRendererTestAccess::stableMedia(*renderer);
        frame(false);
        saveFrame("now-playing-v42.png");
        mcoverlay::OverlayRendererTestAccess::noMedia(*renderer);
    }
    mcoverlay::SmoothScroll scroll;
    scroll.update(0,500,-1,20,1.0F/60,false);
    check(scroll.current>0 && scroll.current<80,"wheel begins smooth motion, not a jump");
    for(int i=0;i<90;++i) scroll.update(scroll.current,500,0,20,1.0F/60,false);
    check(std::abs(scroll.current-80)<0.2F,"scroll converges to exact destination");
    scroll.update(220,500,0,20,1.0F/60,true);
    check(scroll.current==220 && scroll.target==220,"scrollbar drag overrides animation directly");
    scroll.update(scroll.current,30,0,20,1.0F/60,false);
    check(scroll.current<=30 && scroll.target<=30,"scroll clamps after shrinking content");
    scroll.update(6,30,1,20,1.0F/60,false);
    check(scroll.target==0.0F,"upper partial line snaps to the true top endpoint");
    scroll.update(24,30,-1,20,1.0F/60,false);
    check(scroll.target==30.0F,"lower partial line snaps to the true bottom endpoint");
    // Reproduce Dear ImGui's pixel-quantized feedback. The old implementation
    // restarted interpolation from round(current) every frame and could become
    // permanently stuck 1-4 px away from either endpoint.
    mcoverlay::SmoothScroll quantizedTop;
    quantizedTop.update(2.0F,30.0F,1.0F,20.0F,1.0F/60.0F,false);
    for(int i=0;i<120;++i)
        quantizedTop.update(std::round(quantizedTop.current),30.0F,0.0F,20.0F,
                            1.0F/60.0F,false);
    check(quantizedTop.current==0.0F,
          "pixel-quantized feedback still reaches the exact top endpoint");
    mcoverlay::SmoothScroll quantizedBottom;
    quantizedBottom.update(28.0F,30.0F,-1.0F,20.0F,1.0F/60.0F,false);
    for(int i=0;i<120;++i)
        quantizedBottom.update(std::round(quantizedBottom.current),30.0F,0.0F,20.0F,
                               1.0F/60.0F,false);
    check(quantizedBottom.current==30.0F,
          "pixel-quantized feedback still reaches the exact bottom endpoint");
    float collapsePhase=1.0F;
    float previousHeight=mcoverlay::CollapsibleMotion::eased(collapsePhase);
    bool strictlyMoving=true;
    for(int frameIndex=0;frameIndex<17;++frameIndex) {
        collapsePhase=mcoverlay::CollapsibleMotion::advance(
            collapsePhase,false,1.0F/60.0F);
        const float height=mcoverlay::CollapsibleMotion::eased(collapsePhase);
        strictlyMoving=strictlyMoving&&height<previousHeight;
        previousHeight=height;
    }
    check(collapsePhase==0.0F&&strictlyMoving,
          "accordion collapse moves every frame and reaches an exact endpoint");
    const float hoverIn=mcoverlay::approachExponential(0.0F,1.0F,13.0F,0.02F);
    const float hoverOut=mcoverlay::approachExponential(1.0F,0.0F,13.0F,0.02F);
    check(std::abs(hoverIn-(1.0F-hoverOut))<1.0e-6F,
          "search hover acquire and recovery use equal response speed");
    check(mcoverlay::SearchActivationMotion::scale(0.070F)<0.95F,
          "search click visibly compresses before activation");
    check(mcoverlay::SearchActivationMotion::expansion(0.075F)==0.0F &&
          mcoverlay::SearchActivationMotion::expansion(0.30F)<1.0F&&
          mcoverlay::SearchActivationMotion::expansion(0.50F)==1.0F,
          "search expands monotonically without a post-open rebound");
    for (int theme=0;theme<2;++theme) for(int size=0;size<4;++size) for(int page=0;page<23;++page) {
        mcoverlay::OverlayRendererTestAccess::prepare(*renderer,page,size,theme!=0);
        if(page==22) mcoverlay::OverlayRendererTestAccess::freeLookPreview(*renderer);
        frame(true);
        // Content metrics are finalized at the next Begin, so sample twice.
        frame(true);
        if(page==15 || page==17)
            check(mcoverlay::OverlayRendererTestAccess::selectedPage(*renderer)==0,
                  "retired pages cannot be rendered from stale resident state");
        ImGuiWindow* rail=nullptr;
        if (auto* gui = ImGui::GetCurrentContext())
            for(auto* w:gui->Windows)
                if(std::strstr(w->Name,"##navigationScroll")) rail=w;
        check(rail && rail->ScrollMax.y>0, "every page/size has independent scrollable rail");
        if (page==0 && size==0) saveFrame(theme ? "gui-light.png" : "gui-dark.png");
        if (page==22 && size==0 && theme==0) saveFrame("freelook-v40.png");
    }
    mcoverlay::OverlayRendererTestAccess::prepare(*renderer,8,0,false);
    mcoverlay::OverlayRendererTestAccess::aimMode(*renderer,false);
    frame(true);
    mcoverlay::OverlayRendererTestAccess::aimMode(*renderer,true);
    frame(true);
    check(ImGui::GetCurrentContext()->ColorStack.Size==0,
          "Smooth Aim and Lock On selected states keep the color stack balanced");
    ImGuiWindow* aimSettings=nullptr;
    for(auto* windowEntry:ImGui::GetCurrentContext()->Windows)
        if(std::strstr(windowEntry->Name,"##settingsPage")) aimSettings=windowEntry;
    check(aimSettings&&aimSettings->ScrollbarY,
          "Aim Assist reserves a stable scrollbar rail while content changes");
    mcoverlay::OverlayRendererTestAccess::aimSection(
        *renderer,0U,false,0.8F,123.0F);
    frame(true);
    check(std::abs(mcoverlay::OverlayRendererTestAccess::aimSectionBodyHeight(
              *renderer,0U)-123.0F)<0.01F,
          "a collapsing Aim Assist section preserves its measured body height");
    mcoverlay::OverlayRendererTestAccess::search(*renderer,"silent");
    for(int animationFrame=0;animationFrame<28;++animationFrame) frame(true);
    saveFrame("gui-search-silent.png");
    auto* island=ImGui::FindWindowByName("##FeatureSearchIsland");
    auto* mainGui=ImGui::FindWindowByName("##McOverlayClickGui");
    check(island && island->Active && mainGui &&
          island->Pos.y>=mainGui->Pos.y+mainGui->Size.y,
          "search island is visible below GUI, not covering its controls");
    auto* rail=ImGui::FindWindowByName("##McOverlayClickGui/##navigationScroll_0DE478A2");
    for(auto* w:ImGui::GetCurrentContext()->Windows)
        if(std::strstr(w->Name,"##navigationScroll")) rail=w;
    check(rail && rail->ScrollMax.y<1,"single search result removes obsolete scroll extent");
    mcoverlay::OverlayRendererTestAccess::search(*renderer,"no feature matches this");
    frame(true); frame(true);
    check(rail && rail->ScrollMax.y<1,"empty search does not extend parent bounds");
    mcoverlay::OverlayRendererTestAccess::search(*renderer,"");
    frame(true); frame(true);
    check(rail && rail->ScrollMax.y>0,"clearing search restores full navigation");
    for (int theme=0;theme<2;++theme) for(int size=0;size<4;++size) {
        mcoverlay::OverlayRendererTestAccess::prepare(*renderer,11,size,theme!=0);
        for (int variant=0;variant<3;++variant) {
            mcoverlay::OverlayRendererTestAccess::blacklist(*renderer,variant==0 ? 0 : 8,variant==2,theme!=0);
            mcoverlay::OverlayRendererTestAccess::hudOnly(*renderer);
            frame(false); frame(false);
            if(size==0) {
                char file[64]; std::snprintf(file,sizeof(file),"blacklist-%s-%d.png",theme?"light":"dark",variant);
                saveFrame(file);
            }
        }
    }
    mcoverlay::OverlayRendererTestAccess::prepare(*renderer,11,0,false);
    mcoverlay::OverlayRendererTestAccess::blacklistModal(*renderer,*snapshot);
    frame(true); frame(true);
    saveFrame("blacklist-add-modal-v42.png");
    mcoverlay::OverlayRendererTestAccess::prepare(*renderer,18,0,false);
    mcoverlay::OverlayRendererTestAccess::imePreview(*renderer);
    frame(true); frame(true); saveFrame("ime-position-preview.png");
    // Exercise the actual projection/draw path, not just the detection policy.
    // Synthetic evidence is kept in this private offscreen drawable; no JVM or
    // live player is required. A landed path must animate, linger, then expire.
    mcoverlay::OverlayRendererTestAccess::predictionOnly(*renderer);
    snapshot->state = mcoverlay::GameSnapshot::State::Ready;
    snapshot->camera.valid = true;
    snapshot->camera.modelView = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    snapshot->camera.projection = {0.5F,0,0,0, 0,0.5F,0,0, 0,0,-0.1F,0, 0,0,0,1};
    snapshot->camera.viewport = {0,0,1600,1200};
    snapshot->entitySampleGeneration = 1;
    snapshot->knockbackTrajectoryCount = 1;
    auto& trajectory = snapshot->knockbackTrajectories[0];
    trajectory.entityId = 42;
    trajectory.startBounds = {-1.3,-0.8,-4.3, -0.7,1.0,-3.7};
    trajectory.points[0] = {-1,-0.8,-4};
    trajectory.points[1] = {0,-0.2,-4};
    trajectory.points[2] = {1,-0.8,-4};
    trajectory.pointCount = 3;
    trajectory.landed = true;
    frame(false);
    check(mcoverlay::OverlayRendererTestAccess::predictionActive(*renderer),
          "confirmed trajectory creates a renderer animation");
    auto* background = ImGui::GetBackgroundDrawList();
    check(background->VtxBuffer.Size>0, "prediction emits projected box geometry");
    const ImVec2 firstVertex = background->VtxBuffer.empty() ? ImVec2{} : background->VtxBuffer[0].pos;
    ImGui::GetCurrentContext()->Time += 0.15;
    frame(false);
    background = ImGui::GetBackgroundDrawList();
    check(background->VtxBuffer.Size>0 &&
          (std::abs(background->VtxBuffer[0].pos.x-firstVertex.x)>1 ||
           std::abs(background->VtxBuffer[0].pos.y-firstVertex.y)>1),
          "prediction advances between frames without a new JNI snapshot");
    saveFrame("knockback-animation.png");
    ImGui::GetCurrentContext()->Time += 0.5;
    frame(false);
    check(mcoverlay::OverlayRendererTestAccess::predictionActive(*renderer) &&
          ImGui::GetBackgroundDrawList()->VtxBuffer.Size>0, "landing box lingers");
    ImGui::GetCurrentContext()->Time += 3;
    frame(false);
    check(!mcoverlay::OverlayRendererTestAccess::predictionActive(*renderer),
          "landing animation expires without restarting from a stale snapshot");
    mcoverlay::OverlayRendererTestAccess::bowOnly(*renderer);
    snapshot->bowTrajectory.active=true;
    snapshot->bowTrajectory.pointCount=3;
    snapshot->bowTrajectory.points[0]={-1,0,-4};
    snapshot->bowTrajectory.points[1]={-.5,.2,-4};
    snapshot->bowTrajectory.points[2]={0,0,-4};
    frame(false);
    background=ImGui::GetBackgroundDrawList();
    check(background->VtxBuffer.Size>0,"bow path emits geometry");
    const float before=background->VtxBuffer.empty()?0:background->VtxBuffer[0].pos.x;
    for(int i=0;i<3;++i) snapshot->bowTrajectory.points[i].x+=1;
    frame(false);
    background=ImGui::GetBackgroundDrawList();
    check(background->VtxBuffer.Size>0 && std::abs(background->VtxBuffer[0].pos.x-before-400)<.1F,
          "bow follows new aim on the very next frame without easing");
    snapshot->bowTrajectory.hasImpact=true;
    snapshot->bowTrajectory.impactLiving=true;
    snapshot->bowTrajectory.impactPlayer=false;
    snapshot->bowTrajectory.impact=snapshot->bowTrajectory.points[2];
    frame(false); saveFrame("bow-mob-impact.png");
    renderer->shutdownWithCurrentContext();
    renderer.reset();
    wglMakeCurrent(nullptr,nullptr); wglDeleteContext(context);
    ReleaseDC(window,dc); DestroyWindow(window);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
