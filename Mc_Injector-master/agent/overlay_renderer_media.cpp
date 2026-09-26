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

void OverlayRenderer::renderMediaOverlay(const float deltaSeconds,
                                         const float uiScale,
                                         const bool interactive) noexcept
{
    ImGuiIO& io=ImGui::GetIO();
    const bool targetVisible=m_mediaSettings.enabled && m_media.available &&
        m_media.title[0U]!='\0';
    advancePresentationSpring(m_mediaPanelProgress,m_mediaPanelVelocity,
                              targetVisible?1.0F:0.0F,deltaSeconds);

    const auto requestAction=[&](const MediaAction action) noexcept {
        if(action==MediaAction::None) return;
        m_mediaAction=action;
        m_mediaSlideDirection=action==MediaAction::Previous?1:-1;
        log::info(action==MediaAction::Previous?"Now Playing action queued: previous":
            action==MediaAction::Toggle?"Now Playing action queued: toggle":
            "Now Playing action queued: next");
    };
    if(m_inputState) {
        const auto queued=static_cast<MediaAction>(
            m_inputState->mediaAction.exchange(0U,std::memory_order_acq_rel));
        requestAction(queued);
    }

    const bool foreground=m_window && ::GetForegroundWindow()==m_window;
    const bool hotkeysAllowed=foreground && !interactive && m_inputState &&
        !m_inputState->gameScreenOpen.load(std::memory_order_acquire) &&
        !m_inputState->composingInput.load(std::memory_order_acquire);
    const std::array<int,3U> keys{{m_mediaSettings.previousHotkey,
                                  m_mediaSettings.toggleHotkey,
                                  m_mediaSettings.nextHotkey}};
    constexpr std::array<MediaAction,3U> actions{{MediaAction::Previous,
                                                  MediaAction::Toggle,
                                                  MediaAction::Next}};
    for(std::size_t index=0;index<keys.size();++index) {
        const int key=keys[index];
        const bool down=key>=8 && key<=254 &&
            (::GetAsyncKeyState(key)&0x8000)!=0;
        // LWJGL may consume WM_KEYDOWN before our subclass. One physical-edge
        // reader owns custom transport keys; WndProc only suppresses delivery.
        if(m_mediaKeyEdges.update(index,key,down,m_mediaSettings.enabled && hotkeysAllowed))
            requestAction(actions[index]);
    }

    const bool trackChanged=m_media.available && m_media.title[0] &&
        std::strcmp(m_mediaLoadedTitle.data(),m_media.title.data())!=0;
    const bool coverChanged=m_media.available && m_media.title[0] &&
        std::strcmp(m_mediaLoadedCoverPath.data(),
                                        m_media.coverPath.data())!=0;
    if(trackChanged) {
        m_mediaElapsedFallbackMs=std::max<std::int64_t>(0,m_media.positionMs);
        m_mediaElapsedClockTick=static_cast<std::uint64_t>(::GetTickCount64());
        if(m_mediaPreviousCoverTexture) {
            const GLuint stale=static_cast<GLuint>(m_mediaPreviousCoverTexture);
            ::glDeleteTextures(1,&stale);
            m_mediaPreviousCoverTexture=0U;
        }
        const bool hadTrack=m_mediaLoadedTitle[0U]!='\0';
        if(hadTrack) {
            m_mediaPreviousCoverTexture=m_mediaCoverTexture;
            m_mediaCoverTexture=0U;
            std::snprintf(m_mediaPreviousTitle.data(),m_mediaPreviousTitle.size(),
                          "%s",m_mediaLoadedTitle.data());
            std::snprintf(m_mediaPreviousArtist.data(),m_mediaPreviousArtist.size(),
                          "%s",m_mediaLoadedArtist.data());
            m_mediaTrackProgress=0.0F;
            m_mediaTrackVelocity=0.0F;
        } else {
            if(m_mediaCoverTexture) {
                const GLuint stale=static_cast<GLuint>(m_mediaCoverTexture);
                ::glDeleteTextures(1,&stale);
                m_mediaCoverTexture=0U;
            }
            m_mediaTrackProgress=1.0F;
        }
    }
    if(trackChanged || coverChanged) {
        if(m_mediaCoverTexture) {
            const GLuint stale=m_mediaCoverTexture;
            ::glDeleteTextures(1,&stale);
            m_mediaCoverTexture=0U;
        }
        m_mediaCoverWidth=m_mediaCoverHeight=0;
        std::snprintf(m_mediaLoadedCoverPath.data(),m_mediaLoadedCoverPath.size(),
                      "%s",m_media.coverPath.data());
        std::snprintf(m_mediaLoadedTitle.data(),m_mediaLoadedTitle.size(),
                      "%s",m_media.title.data());
        const char* incomingArtist=m_media.artist[0U]?m_media.artist.data():
            m_media.source.data();
        std::snprintf(m_mediaLoadedArtist.data(),m_mediaLoadedArtist.size(),
                      "%s",incomingArtist);
        if(m_media.coverPath[0U]) {
            int channels=0;
            unsigned char* pixels=stbi_load(m_media.coverPath.data(),
                &m_mediaCoverWidth,&m_mediaCoverHeight,&channels,4);
            if(pixels && m_mediaCoverWidth>0 && m_mediaCoverHeight>0) {
                double red=0,green=0,blue=0,weight=0;
                const std::size_t count=static_cast<std::size_t>(
                    m_mediaCoverWidth)*static_cast<std::size_t>(m_mediaCoverHeight);
                const std::size_t stride=std::max<std::size_t>(1U,count/4096U);
                for(std::size_t pixel=0;pixel<count;pixel+=stride) {
                    const auto* p=pixels+pixel*4U;
                    const double maximum=std::max({p[0],p[1],p[2]})/255.0;
                    const double minimum=std::min({p[0],p[1],p[2]})/255.0;
                    const double saturation=maximum-minimum;
                    const double luminance=(p[0]*0.2126+p[1]*0.7152+p[2]*0.0722)/255.0;
                    const double sampleWeight=0.18+saturation*1.8+
                        (1.0-std::abs(luminance-0.56))*0.35;
                    red+=p[0]*sampleWeight;
                    green+=p[1]*sampleWeight;
                    blue+=p[2]*sampleWeight;
                    weight+=255.0*sampleWeight;
                }
                if(weight>0.0) {
                    m_mediaAccent[0]=static_cast<float>(std::clamp(red/weight,0.0,1.0));
                    m_mediaAccent[1]=static_cast<float>(std::clamp(green/weight,0.0,1.0));
                    m_mediaAccent[2]=static_cast<float>(std::clamp(blue/weight,0.0,1.0));
                    const float peak=std::max({m_mediaAccent[0],m_mediaAccent[1],
                                               m_mediaAccent[2],0.001F});
                    for(float& component:m_mediaAccent)
                        component=std::clamp(component/peak*0.92F+0.08F,0.08F,1.0F);
                }
                GLint previousTexture=0;
                ::glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);
                ::glGenTextures(1,&m_mediaCoverTexture);
                ::glBindTexture(GL_TEXTURE_2D,m_mediaCoverTexture);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
                ::glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,m_mediaCoverWidth,
                    m_mediaCoverHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                ::glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previousTexture));
            }
            stbi_image_free(pixels);
        }
    }

    advancePresentationSpring(m_mediaTrackProgress,m_mediaTrackVelocity,
                              1.0F,deltaSeconds);
    if(m_mediaTrackProgress>=0.999F && m_mediaPreviousTitle[0]) {
        if(m_mediaPreviousCoverTexture) {
            const GLuint stale=static_cast<GLuint>(m_mediaPreviousCoverTexture);
            ::glDeleteTextures(1,&stale);
        }
        m_mediaPreviousCoverTexture=0U;
        m_mediaPreviousTitle.fill('\0');
        m_mediaPreviousArtist.fill('\0');
        // A spontaneous track completion is a forward transition. Do not let
        // the direction from an old Previous click leak into the next track.
        m_mediaSlideDirection=-1;
    }
    advancePresentationSpring(m_mediaPlayMorph,m_mediaPlayMorphVelocity,
                              m_media.playing?1.0F:0.0F,deltaSeconds);

    if(m_mediaPanelProgress<=0.004F) return;
    const float linear=std::clamp(m_mediaPanelProgress,0.0F,1.0F);
    const float eased=linear*linear*(3.0F-2.0F*linear);
    const float presentationScale=1.14F-0.14F*m_mediaPanelProgress;
    ImFont* const titleFont=m_mediaFont ? m_mediaFont :
        (m_boldFonts[static_cast<std::size_t>(std::clamp(m_guiScaleIndex,0,3))]?
         m_boldFonts[static_cast<std::size_t>(std::clamp(m_guiScaleIndex,0,3))]:
         ImGui::GetFont());
    ImFont* const bodyFont=m_mediaFont ? m_mediaFont :
        (m_fonts[static_cast<std::size_t>(std::clamp(m_guiScaleIndex,0,3))]?
         m_fonts[static_cast<std::size_t>(std::clamp(m_guiScaleIndex,0,3))]:
         ImGui::GetFont());
    // Reference HTML design space: 720 x 184. The control column remains at
    // the v38 position, while the right edge follows it inward so the 20-unit
    // outer gaps now match the album-art side.
    // used before the large HTML preview pass; every preset scales the entire
    // card uniformly so artwork, keycaps and typography keep their ratios.
    const float requestedScale=uiScale*static_cast<float>(std::clamp(
        m_mediaSettings.scalePercent,35,100))/100.0F;
    const float cardScale=std::max(0.25F,std::min(
        requestedScale,(io.DisplaySize.x-16.0F)/720.0F));
    // The compact card should not make metadata read like micro-copy.  Round
    // the requested sizes to whole display pixels so the dynamic font bake is
    // sampled one-to-one instead of through a permanently fractional scale.
    const float titleSize=std::max(15.0F,std::round(28.0F*cardScale));
    const float bodySize=std::max(11.0F,std::round(20.0F*cardScale));
    const float targetWidth=720.0F*cardScale;
    if(m_mediaAnimatedWidth<=1.0F) m_mediaAnimatedWidth=targetWidth;
    const float widthBlend=1.0F-std::exp(-9.5F*std::max(0.0F,deltaSeconds));
    m_mediaAnimatedWidth+=(targetWidth-m_mediaAnimatedWidth)*widthBlend;
    const float panelWidth=std::clamp(m_mediaAnimatedWidth,
        std::min(280.0F*cardScale,io.DisplaySize.x-8.0F),
        std::max(8.0F,io.DisplaySize.x-8.0F));
    const float panelHeight=184.0F*cardScale;
    const float defaultX=(io.DisplaySize.x-panelWidth)*0.5F;
    const float defaultY=14.0F*cardScale;
    float panelX=m_mediaSettings.panelX<0?defaultX:
        io.DisplaySize.x*static_cast<float>(m_mediaSettings.panelX)/1000.0F;
    float panelY=m_mediaSettings.panelY<0?defaultY:
        io.DisplaySize.y*static_cast<float>(m_mediaSettings.panelY)/1000.0F;
    panelX=std::clamp(panelX,4.0F,std::max(4.0F,io.DisplaySize.x-panelWidth-4.0F));
    panelY=std::clamp(panelY,4.0F,std::max(4.0F,io.DisplaySize.y-panelHeight-4.0F));
    const ImVec2 minimum(panelX,panelY),maximum(panelX+panelWidth,panelY+panelHeight);
    const ImVec2 centre((minimum.x+maximum.x)*0.5F,(minimum.y+maximum.y)*0.5F);

    // Modern transport hierarchy: the glyph is the action and sits directly
    // on the card; only the binding hint is a tactile keycap. The three rows
    // share the album cover's exact top/bottom bounds without a parent capsule.
    const float controlColumnWidth=78.0F*cardScale;
    const float controlX=panelX+panelWidth-20.0F*cardScale-controlColumnWidth;
    const float controlHeight=32.0F*cardScale;
    const std::array<float,3U> controlTops{{panelY+20.0F*cardScale,
        panelY+64.0F*cardScale,panelY+108.0F*cardScale}};
    const std::array<float,3U> buttonYs{{
        controlTops[0]+controlHeight*0.5F,
        controlTops[1]+controlHeight*0.5F,
        controlTops[2]+controlHeight*0.5F}};
    const float buttonX=controlX+14.5F*cardScale;
    const float keycapLeft=controlX+35.0F*cardScale;
    const float keycapRight=controlX+controlColumnWidth;
    std::array<bool,3U> controlHovered{};
    std::array<bool,3U> controlPressed{};
    if(interactive && linear>0.985F && !ImGui::GetTopMostPopupModal()) {
        // ForegroundDrawList has no input surface. Register a real window so
        // controls outside the main GUI own their clicks, including overlaps.
        ImGui::SetNextWindowPos(minimum);
        ImGui::SetNextWindowSize(ImVec2(panelWidth,panelHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::Begin("##NowPlayingInput",nullptr,ImGuiWindowFlags_NoDecoration|
            ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|
            ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNav|
            ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        for(std::size_t index=0;index<controlHovered.size();++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetCursorScreenPos(ImVec2(controlX,controlTops[index]));
            if(ImGui::InvisibleButton("##transport",ImVec2(controlColumnWidth,controlHeight)))
                requestAction(actions[index]);
            controlHovered[index]=ImGui::IsItemHovered();
            controlPressed[index]=ImGui::IsItemActive();
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(minimum);
        ImGui::InvisibleButton("##dragMedia",ImVec2(controlX-panelX-8.0F*cardScale,panelHeight));
        if(ImGui::IsItemActivated()) {
            m_mediaDragging=true;
            m_mediaDragOffsetX=io.MousePos.x-panelX;
            m_mediaDragOffsetY=io.MousePos.y-panelY;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_mediaDragging=false;
        if(m_mediaDragging) {
            panelX=std::clamp(io.MousePos.x-m_mediaDragOffsetX,4.0F,
                std::max(4.0F,io.DisplaySize.x-panelWidth-4.0F));
            panelY=std::clamp(io.MousePos.y-m_mediaDragOffsetY,4.0F,
                std::max(4.0F,io.DisplaySize.y-panelHeight-4.0F));
            m_mediaSettings.panelX=std::clamp(static_cast<int>(std::lround(
                panelX/std::max(1.0F,io.DisplaySize.x)*1000.0F)),0,1000);
            m_mediaSettings.panelY=std::clamp(static_cast<int>(std::lround(
                panelY/std::max(1.0F,io.DisplaySize.y)*1000.0F)),0,1000);
            m_mediaSettingsDirty=true;
        }
    } else m_mediaDragging=false;

    ImDrawList* const draw=ImGui::GetForegroundDrawList();
    const auto transform=[&](const ImVec2 point) noexcept {
        return ImVec2(centre.x+(point.x-centre.x)*presentationScale,
                      centre.y+(point.y-centre.y)*presentationScale);
    };
    const auto transformText=[&](const ImVec2 point) noexcept {
        ImVec2 result=transform(point);
        result.x=std::round(result.x);
        result.y=std::round(result.y);
        return result;
    };
    const ImVec2 shownMinimum=transform(ImVec2(panelX,panelY));
    const ImVec2 shownMaximum=transform(ImVec2(panelX+panelWidth,panelY+panelHeight));
    const float rounding=34.0F*cardScale*presentationScale;
    const int opacity=static_cast<int>(std::lround(
        static_cast<float>(std::clamp(m_mediaSettings.opacity,20,100))*
        2.55F*eased));
    if(opacity<250) {
        captureBackdropTexture();
        if(m_blurTexture) {
            struct Tap { ImVec2 offset; int alpha; };
            constexpr std::array<Tap,13U> taps{{
                {{0,0},54},{{-2,0},40},{{2,0},40},{{0,-2},40},{{0,2},40},
                {{-2,-2},26},{{2,-2},26},{{-2,2},26},{{2,2},26},
                {{-5,0},17},{{5,0},17},{{0,-5},17},{{0,5},17}}};
            for(const Tap& tap:taps) {
                const float left=std::clamp(panelX+tap.offset.x*uiScale,0.0F,io.DisplaySize.x);
                const float top=std::clamp(panelY+tap.offset.y*uiScale,0.0F,io.DisplaySize.y);
                const float right=std::clamp(panelX+panelWidth+tap.offset.x*uiScale,0.0F,io.DisplaySize.x);
                const float bottom=std::clamp(panelY+panelHeight+tap.offset.y*uiScale,0.0F,io.DisplaySize.y);
                draw->AddImageRounded(reinterpret_cast<ImTextureID>(
                    static_cast<std::uintptr_t>(m_blurTexture)),shownMinimum,shownMaximum,
                    ImVec2(left/io.DisplaySize.x,1.0F-top/io.DisplaySize.y),
                    ImVec2(right/io.DisplaySize.x,1.0F-bottom/io.DisplaySize.y),
                    IM_COL32(255,255,255,static_cast<int>(tap.alpha*eased)),rounding);
            }
        }
    }
    draw->AddRectFilled(shownMinimum,shownMaximum,
        packedRgbColor(m_mediaSettings.panelColor,opacity),rounding);

    // The helper publishes ten real WASAPI loopback FFT bands. Expand them
    // with Catmull-Rom interpolation only; no synthetic sine/noise is added.
    // The resulting Spotify-style waveform is part of the card background.
    const float rise=1.0F-std::exp(-20.0F*std::max(0.0F,deltaSeconds));
    const float fall=1.0F-std::exp(-(m_media.playing?7.0F:2.1F)*
                                  std::max(0.0F,deltaSeconds));
    for(std::size_t band=0;band<m_mediaSpectrumDisplay.size();++band) {
        const float target=m_media.playing?
            std::clamp(m_media.spectrum[band],0.0F,1.0F):0.0F;
        float& shown=m_mediaSpectrumDisplay[band];
        shown+=(target-shown)*(target>shown?rise:fall);
    }
    constexpr int visualBars=48;
    const float spectrumBottom=panelY+panelHeight-7.0F*cardScale;
    const float spectrumLeft=panelX+20.0F*cardScale;
    const float spectrumRight=keycapRight;
    const float barStep=(spectrumRight-spectrumLeft)/
        static_cast<float>(visualBars);
    const float spectrumSetting=static_cast<float>(std::clamp(
        m_mediaSettings.spectrumOpacity,0,100))/100.0F;
    for(int visual=0;visual<visualBars;++visual) {
        // Mirror the ten physical bands around the centre. Low/mid cumulative
        // energy owns the middle and higher frequencies taper toward both
        // edges, which reads as one coherent waveform instead of a left-to-
        // right analyser.
        const float unit=(static_cast<float>(visual)+0.5F)/
            static_cast<float>(visualBars);
        const float distance=std::abs(unit*2.0F-1.0F);
        const float sample=distance*9.0F;
        const int i1=std::clamp(static_cast<int>(std::floor(sample)),0,9);
        const int i0=std::max(0,i1-1),i2=std::min(9,i1+1),i3=std::min(9,i1+2);
        const float t=sample-static_cast<float>(i1);
        const float p0=m_mediaSpectrumDisplay[static_cast<std::size_t>(i0)];
        const float p1=m_mediaSpectrumDisplay[static_cast<std::size_t>(i1)];
        const float p2=m_mediaSpectrumDisplay[static_cast<std::size_t>(i2)];
        const float p3=m_mediaSpectrumDisplay[static_cast<std::size_t>(i3)];
        const float local=std::clamp(0.5F*((2.0F*p1)+(-p0+p2)*t+
            (2.0F*p0-5.0F*p1+4.0F*p2-p3)*t*t+
            (-p0+3.0F*p1-3.0F*p2+p3)*t*t*t),0.0F,1.0F);
        float cumulative=0.0F;
        for(int band=0;band<=i2;++band)
            cumulative+=m_mediaSpectrumDisplay[static_cast<std::size_t>(band)];
        cumulative/=static_cast<float>(i2+1);
        const float activity=std::clamp((local*0.72F+cumulative*0.28F)*
            (1.0F-0.48F*distance),0.0F,1.0F);
        const float lifted=std::log1p(activity*5.0F)/std::log(6.0F);
        const float height=(2.0F+lifted*64.0F)*cardScale;
        const float x=spectrumLeft+barStep*static_cast<float>(visual);
        const int alpha=static_cast<int>((72.0F+178.0F*lifted)*eased*
            spectrumSetting);
        draw->AddRectFilled(transform(ImVec2(x,spectrumBottom-height)),
            transform(ImVec2(x+barStep*0.56F,spectrumBottom)),
            IM_COL32(static_cast<int>(m_mediaAccent[0]*255.0F),
                static_cast<int>(m_mediaAccent[1]*255.0F),
                static_cast<int>(m_mediaAccent[2]*255.0F),alpha),
            std::max(1.0F,barStep*0.28F*presentationScale));
    }
    draw->AddRect(shownMinimum,shownMaximum,
        IM_COL32(255,255,255,static_cast<int>(66.0F*eased)),rounding,0,
        std::max(1.0F,cardScale*presentationScale));

    const float textLeft=panelX+164.0F*cardScale;
    const float textRight=panelX+556.0F*cardScale;
    const float trackProgress=std::clamp(m_mediaTrackProgress,0.0F,1.0F);
    const float trackEase=trackProgress*trackProgress*(3.0F-2.0F*trackProgress);
    draw->PushClipRect(shownMinimum,shownMaximum,true);
    const auto drawTrack=[&](const unsigned texture,const char* title,
                             const char* artist,const float offset,
                             const float alpha) noexcept {
        if(alpha<=0.002F || !title || !title[0]) return;
        const ImVec2 artMin=transform(ImVec2(panelX+20.0F*cardScale+offset,
                                             panelY+20.0F*cardScale));
        const ImVec2 artMax=transform(ImVec2(panelX+140.0F*cardScale+offset,
                                             panelY+140.0F*cardScale));
        draw->AddRectFilled(ImVec2(artMin.x-1.0F*cardScale,
                                   artMin.y+4.0F*cardScale),
                            ImVec2(artMax.x+1.0F*cardScale,
                                   artMax.y+8.0F*cardScale),
                            IM_COL32(0,0,0,static_cast<int>(26.0F*eased*alpha)),
                            25.0F*cardScale*presentationScale);
        if(texture) draw->AddImageRounded(reinterpret_cast<ImTextureID>(
            static_cast<std::uintptr_t>(texture)),artMin,artMax,ImVec2(0,0),ImVec2(1,1),
            IM_COL32(255,255,255,static_cast<int>(255.0F*eased*alpha)),
            25.0F*cardScale*presentationScale);
        else draw->AddRectFilled(artMin,artMax,
            IM_COL32(255,255,255,static_cast<int>(245.0F*eased*alpha)),
            25.0F*cardScale*presentationScale);
        const ImVec2 titleMin=transformText(ImVec2(textLeft+offset,panelY+23.0F*cardScale));
        const ImVec2 titleMax=transform(ImVec2(textRight+offset,panelY+54.0F*cardScale));
        draw->PushClipRect(titleMin,titleMax,true);
        draw->AddText(titleFont,titleSize*presentationScale,titleMin,
            IM_COL32(250,250,253,static_cast<int>(255.0F*eased*alpha)),title);
        draw->PopClipRect();
        const ImVec2 artistMin=transformText(ImVec2(textLeft+offset,panelY+62.0F*cardScale));
        const ImVec2 artistMax=transform(ImVec2(textRight+offset,panelY+83.0F*cardScale));
        draw->PushClipRect(artistMin,artistMax,true);
        draw->AddText(bodyFont,bodySize*presentationScale,artistMin,
            IM_COL32(255,255,255,static_cast<int>(158.0F*eased*alpha)),
            artist&&artist[0]?artist:"Windows Media");
        draw->PopClipRect();
    };
    if(trackProgress<0.999F && m_mediaPreviousTitle[0]) {
        drawTrack(m_mediaPreviousCoverTexture,m_mediaPreviousTitle.data(),
                  m_mediaPreviousArtist.data(),
                  static_cast<float>(m_mediaSlideDirection)*panelWidth*trackEase,
                  1.0F-trackEase);
    }
    drawTrack(m_mediaCoverTexture,m_media.title.data(),
              m_media.artist[0]?m_media.artist.data():m_media.source.data(),
              -static_cast<float>(m_mediaSlideDirection)*panelWidth*(1.0F-trackEase),
              trackEase);
    draw->PopClipRect();

    const std::uint64_t elapsedNow=static_cast<std::uint64_t>(::GetTickCount64());
    if(m_mediaElapsedClockTick==0U) m_mediaElapsedClockTick=elapsedNow;
    const std::uint64_t elapsedDelta=elapsedNow>=m_mediaElapsedClockTick?
        std::min<std::uint64_t>(elapsedNow-m_mediaElapsedClockTick,1000U):0U;
    if(m_media.durationMs<=1000) {
        if(m_media.positionMs>0 && std::abs(
            m_media.positionMs-m_mediaElapsedFallbackMs)>2500)
            m_mediaElapsedFallbackMs=m_media.positionMs;
        if(m_media.playing)
            m_mediaElapsedFallbackMs+=static_cast<std::int64_t>(elapsedDelta);
    }
    m_mediaElapsedClockTick=elapsedNow;
    const std::int64_t extrapolated=m_media.durationMs<=1000?
        m_mediaElapsedFallbackMs:
        (m_media.playing && m_media.receivedAtMs?
            m_media.positionMs+static_cast<std::int64_t>(elapsedNow-
                m_media.receivedAtMs):m_media.positionMs);
    const bool knownDuration=m_media.durationMs>1000;
    const float progress=knownDuration?std::clamp(
        static_cast<float>(std::min(extrapolated,m_media.durationMs))/
        static_cast<float>(m_media.durationMs),0.0F,1.0F):0.0F;
    const ImVec2 progressMin=transform(ImVec2(textLeft,panelY+112.0F*cardScale));
    const ImVec2 progressMax=transform(ImVec2(textRight,panelY+117.0F*cardScale));
    draw->AddRectFilled(progressMin,progressMax,
        IM_COL32(255,255,255,static_cast<int>(51.0F*eased)),2.5F*cardScale);
    if(knownDuration) {
        draw->AddRectFilled(progressMin,
            ImVec2(progressMin.x+(progressMax.x-progressMin.x)*progress,progressMax.y),
            IM_COL32(255,255,255,static_cast<int>(194.0F*eased)),
            2.5F*cardScale);
    } else {
        const float breath=0.62F+0.28F*std::sin(static_cast<float>(ImGui::GetTime())*2.2F);
        draw->AddRectFilled(progressMin,progressMax,
            IM_COL32(255,255,255,static_cast<int>(194.0F*eased*breath)),
            2.5F*cardScale);
    }

    const auto formatTime=[](const std::int64_t milliseconds,
                             std::array<char,16U>& output) noexcept {
        const std::int64_t seconds=std::max<std::int64_t>(0,milliseconds/1000);
        std::snprintf(output.data(),output.size(),"%lld:%02lld",
            static_cast<long long>(seconds/60),
            static_cast<long long>(seconds%60));
    };
    std::array<char,16U> elapsedText{},durationText{};
    formatTime(extrapolated,elapsedText);
    if(knownDuration) formatTime(m_media.durationMs,durationText);
    else std::snprintf(durationText.data(),durationText.size(),"--:--");
    const float timeSize=std::max(10.0F,std::round(
        15.0F*cardScale*presentationScale));
    const ImVec2 elapsedPosition=transformText(ImVec2(textLeft,panelY+123.0F*cardScale));
    const ImVec2 durationSize=bodyFont->CalcTextSizeA(timeSize,FLT_MAX,0.0F,
                                                       durationText.data());
    const ImVec2 durationPosition=transformText(ImVec2(textRight,panelY+123.0F*cardScale));
    const ImU32 timeColor=IM_COL32(255,255,255,static_cast<int>(142.0F*eased));
    draw->AddText(bodyFont,timeSize,elapsedPosition,timeColor,elapsedText.data());
    draw->AddText(bodyFont,timeSize,
        ImVec2(durationPosition.x-durationSize.x,durationPosition.y),timeColor,
        durationText.data());

    const ImU32 controlColor=IM_COL32(250,250,253,
        static_cast<int>(248.0F*eased));
    const ImDrawListFlags savedDrawFlags=draw->Flags;
    draw->Flags|=ImDrawListFlags_AntiAliasedFill|ImDrawListFlags_AntiAliasedLines;
    for(std::size_t index=0;index<controlHovered.size();++index) {
        const int alpha=controlPressed[index]?72:controlHovered[index]?42:12;
        const ImVec2 rowMin=transform(ImVec2(controlX,controlTops[index]));
        const ImVec2 rowMax=transform(ImVec2(controlX+controlColumnWidth,
            controlTops[index]+controlHeight));
        draw->AddRectFilled(rowMin,rowMax,IM_COL32(255,255,255,
            static_cast<int>(alpha*eased)),10.0F*cardScale*presentationScale);
    }
    const auto drawSkip=[&](const float y,const bool next) noexcept {
        const ImVec2 c=transform(ImVec2(buttonX,y));
        const float direction=next?1.0F:-1.0F;
        const float s=cardScale*presentationScale;
        const ImVec2 triangle[3]{{c.x-direction*5*s,c.y-7*s},
                                 {c.x-direction*5*s,c.y+7*s},
                                 {c.x+direction*5*s,c.y}};
        draw->AddTriangleFilled(triangle[0],triangle[1],triangle[2],controlColor);
        draw->AddRectFilled(ImVec2(c.x+direction*7*s-s,c.y-7*s),
                      ImVec2(c.x+direction*7*s+s,c.y+7*s),controlColor,s);
    };
    drawSkip(buttonYs[0],false); drawSkip(buttonYs[2],true);
    const ImVec2 playCentre=transform(ImVec2(buttonX,buttonYs[1]));
    const float controlScale=cardScale*presentationScale;
    const float pauseAlpha=std::clamp(m_mediaPlayMorph,0.0F,1.0F);
    const float playAlpha=1.0F-pauseAlpha;
    if(pauseAlpha>0.002F) {
        const ImU32 color=IM_COL32(250,250,253,
            static_cast<int>(245.0F*eased*pauseAlpha));
        draw->AddRectFilled(ImVec2(playCentre.x-5.5F*controlScale,
            playCentre.y-7*controlScale),ImVec2(playCentre.x-1.5F*controlScale,
            playCentre.y+7*controlScale),color,1.5F*controlScale);
        draw->AddRectFilled(ImVec2(playCentre.x+1.5F*controlScale,
            playCentre.y-7*controlScale),ImVec2(playCentre.x+5.5F*controlScale,
            playCentre.y+7*controlScale),color,1.5F*controlScale);
    }
    if(playAlpha>0.002F) draw->AddTriangleFilled(
        ImVec2(playCentre.x-5*controlScale,playCentre.y-8*controlScale),
        ImVec2(playCentre.x-5*controlScale,playCentre.y+8*controlScale),
        ImVec2(playCentre.x+8*controlScale,playCentre.y),
        IM_COL32(250,250,253,static_cast<int>(245.0F*eased*playAlpha)));

    // Every binding hint owns the same square footprint. Kenney's atlas mixes
    // narrow letter tiles and wide named-key tiles; drawing their source aspect
    // directly made otherwise equivalent controls look randomly larger.
    std::array<std::array<char,64U>,3U> shortcutLabels{};
    for(std::size_t index=0;index<keys.size();++index)
        std::snprintf(shortcutLabels[index].data(),shortcutLabels[index].size(),
                      "%s",hotkeyName(static_cast<unsigned>(std::max(0,keys[index]))));
    for(std::size_t index=0;index<buttonYs.size();++index) {
        const unsigned virtualKey=static_cast<unsigned>(std::max(0,keys[index]));
        const KenneyPromptTile tile=kenneyPromptTile(m_mediaKeycapAtlasXml,
            m_mediaKeycapAtlasXmlSize,virtualKey);
        const float promptHeight=28.0F*cardScale;
        const float promptWidth=28.0F*cardScale;
        const float keycapCentre=(keycapLeft+keycapRight)*0.5F;
        const ImVec2 promptMin=transform(ImVec2(keycapCentre-promptWidth*0.5F,
            buttonYs[index]-promptHeight*0.5F));
        const ImVec2 promptMax=transform(ImVec2(keycapCentre+promptWidth*0.5F,
            buttonYs[index]+promptHeight*0.5F));
        if(tile.valid && m_mediaKeycapTexture!=0U) {
            draw->AddImage(reinterpret_cast<ImTextureID>(
                static_cast<std::uintptr_t>(m_mediaKeycapTexture)),
                promptMin,promptMax,tile.uv0,tile.uv1,
                IM_COL32(255,255,255,static_cast<int>(242.0F*eased)));
        } else {
            draw->AddRectFilled(promptMin,promptMax,
                IM_COL32(255,255,255,static_cast<int>(230.0F*eased)),
                5.0F*cardScale*presentationScale);
            const float available=promptMax.x-promptMin.x-6.0F*cardScale;
            const float natural=bodyFont->CalcTextSizeA(
                9.0F*cardScale*presentationScale,FLT_MAX,0.0F,
                shortcutLabels[index].data()).x;
            const float labelSize=9.0F*cardScale*presentationScale*
                std::min(1.0F,available/std::max(1.0F,natural));
            const ImVec2 size=bodyFont->CalcTextSizeA(labelSize,FLT_MAX,0.0F,
                shortcutLabels[index].data());
            draw->AddText(bodyFont,labelSize,
                ImVec2((promptMin.x+promptMax.x-size.x)*0.5F,
                       (promptMin.y+promptMax.y-size.y)*0.5F),
                IM_COL32(55,55,55,static_cast<int>(120.0F*eased)),
                shortcutLabels[index].data());
        }
    }
    draw->Flags=savedDrawFlags;
}


} // namespace mcoverlay
