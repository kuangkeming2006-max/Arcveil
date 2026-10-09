#include "ClickGui.h"
#include "AnimatedWidgets.h"
#include "NavigationLabel.h"
#include "GuiDrawPolicy.h"
#include "../UiColors.h"
#include "../FeatureNavigation.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
namespace mcoverlay::ui {
namespace {
ImVec4 mixColor(const ImVec4& dark, const ImVec4& light, float amount) noexcept
{
        return ImVec4(dark.x + (light.x - dark.x) * amount,
                      dark.y + (light.y - dark.y) * amount,
                      dark.z + (light.z - dark.z) * amount,
                      dark.w + (light.w - dark.w) * amount);
    }

void beginSmoothChild(const char* id, ImVec2 size, SmoothScroll& scroll,
                      float delta, ImGuiWindowFlags extra=0) noexcept
{
    static_cast<void>(delta);
    // Apply before BeginChild so content, clipping and hit testing share the
    // same scroll origin. Never transform text vertices after layout to fake it.
    if (scroll.initialized) ImGui::SetNextWindowScroll(ImVec2(-1.0F, scroll.current));
    const auto fadedScrollbar = [&](const ImGuiCol color) noexcept {
        ImVec4 value = ImGui::GetStyleColorVec4(color);
        value.w *= std::clamp(scroll.scrollbarAlpha, 0.0F, 1.0F);
        return value;
    };
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,
                          fadedScrollbar(ImGuiCol_ScrollbarBg));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
                          fadedScrollbar(ImGuiCol_ScrollbarGrab));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                          fadedScrollbar(ImGuiCol_ScrollbarGrabHovered));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
                          fadedScrollbar(ImGuiCol_ScrollbarGrabActive));
    ImGui::BeginChild(id, size, false,
                      extra | ImGuiWindowFlags_NoScrollWithMouse);
    configureGuiDrawList(ImGui::GetWindowDrawList());
    ImGui::PopStyleColor(4);
}

void endSmoothChild(SmoothScroll& scroll,const float delta) noexcept
{
    const ImGuiIO& io = ImGui::GetIO();
    // Nested Aim Assist/Blacklist children are deliberately non-scrolling;
    // route their wheel input to this owning page instead of dropping it.
    const float wheel = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_ChildWindows|ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
        ? io.MouseWheel : 0.0F;
    const float actualScroll=ImGui::GetScrollY();
    // Only the vertical scrollbar itself is authoritative. Treating any left
    // mouse press as a drag resets smooth scrolling when the user clicks an
    // unrelated button while the page is still settling. imgui_internal.h is
    // already pinned to the project's Dear ImGui version, so compare ActiveId
    // with the real scrollbar ID instead of guessing from cursor deltas.
    ImGuiWindow* const scrollWindow=ImGui::GetCurrentWindow();
    const bool scrollbarDragging=scrollWindow!=nullptr && scrollWindow->ScrollbarY &&
        ImGui::GetActiveID()==ImGui::GetWindowScrollbarID(scrollWindow,ImGuiAxis_Y);
    // CursorMaxPos contains this frame's completed layout whereas ContentSize
    // and ScrollMax.y still describe the previous Begin().  Taking the larger
    // of old and new heights keeps a collapsing bottom card artificially tall
    // for one frame, then drops the scroll limit in a single step.  Use only
    // the completed current-frame layout so SmoothScroll can follow every
    // accordion-height delta continuously.
    const float liveContentHeight=scrollWindow!=nullptr
        ? std::max(0.0F,scrollWindow->DC.CursorMaxPos.y-
            scrollWindow->DC.CursorStartPos.y)
        : 0.0F;
    const float liveMaximum=scrollWindow!=nullptr
        ? std::max(0.0F,liveContentHeight+
            scrollWindow->WindowPadding.y*2.0F-scrollWindow->InnerRect.GetHeight())
        : ImGui::GetScrollMaxY();
    scroll.update(actualScroll,liveMaximum,wheel,
                  ImGui::GetFontSize(), delta, scrollbarDragging);
    ImGui::EndChild();
}

std::array<float, 3U> unpackRgb(const std::uint32_t rgb) noexcept
{
    return {static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F,
            static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F,
            static_cast<float>(rgb & 0xFFU) / 255.0F};
}

std::uint32_t packRgb(const std::array<float, 3U>& color) noexcept
{
    const auto channel = [](const float value) noexcept {
        return static_cast<std::uint32_t>(std::lround(
            std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return (channel(color[0U]) << 16U) |
           (channel(color[1U]) << 8U) | channel(color[2U]);
}

float cubicBezierProgress(const float position, const float x1,
                          const float y1, const float x2,
                          const float y2) noexcept
{
    const auto coordinate=[](const float t,const float first,
                             const float second) noexcept {
        const float u=1.0F-t;
        return 3.0F*u*u*t*first+3.0F*u*t*t*second+t*t*t;
    };
    const float x=std::clamp(position,0.0F,1.0F);
    float lower=0.0F,upper=1.0F;
    for(int iteration=0;iteration<14;++iteration) {
        const float middle=(lower+upper)*0.5F;
        if(coordinate(middle,x1,x2)<x) lower=middle;
        else upper=middle;
    }
    return coordinate((lower+upper)*0.5F,y1,y2);
}

float smootherStep(float value) noexcept
{
    value=std::clamp(value,0.0F,1.0F);
    return value*value*value*(value*(value*6.0F-15.0F)+10.0F);
}

const char* portableHotkeyName(unsigned key) noexcept {
    static thread_local char name[40];
    if(!key) return "Unbound";
    if(key==0xDE) return "Apostrophe";
    if(key==0xA4) return "Left Alt";
    if(key==9) return "Tab";
    if(key==0xB0) return "Media Next";
    if(key==0xB1) return "Media Previous";
    if(key==0xB3) return "Play / Pause";
    if(key>=0x70 && key<=0x87) std::snprintf(name,sizeof(name),"F%u",key-0x6F);
    else if((key>='A'&&key<='Z')||(key>='0'&&key<='9')) std::snprintf(name,sizeof(name),"%c",int(key));
    else std::snprintf(name,sizeof(name),"Key 0x%02X",key);
    return name;
}

}
void renderClickGui(ClickGuiRefs refs, ClickGuiFrame& frame, const ClickGuiHost& host) noexcept
{
    auto& m_aimSectionBodyHeight = refs.m_aimSectionBodyHeight;
    auto& m_aimSectionOpen = refs.m_aimSectionOpen;
    auto& m_aimSectionProgress = refs.m_aimSectionProgress;
    auto& m_aimSectionVelocity = refs.m_aimSectionVelocity;
    auto& m_bedRescanPending = refs.m_bedRescanPending;
    auto& m_blacklist = refs.m_blacklist;
    auto& m_blacklistAction = refs.m_blacklistAction;
    auto& m_blacklistActionDirty = refs.m_blacklistActionDirty;
    auto& m_blacklistAddOpen = refs.m_blacklistAddOpen;
    auto& m_boldFonts = refs.m_boldFonts;
    auto& m_clickGuiNavEnabled = refs.m_clickGuiNavEnabled;
    auto& m_clickGuiNavHover = refs.m_clickGuiNavHover;
    auto& m_clickGuiNavPosition = refs.m_clickGuiNavPosition;
    auto& m_clickGuiNavSelection = refs.m_clickGuiNavSelection;
    auto& m_clickGuiPage = refs.m_clickGuiPage;
    auto& m_clickGuiPageProgress = refs.m_clickGuiPageProgress;
    auto& m_clickGuiProgress = refs.m_clickGuiProgress;
    auto& m_clickGuiThemeProgress = refs.m_clickGuiThemeProgress;
    auto& m_clickGuiX = refs.m_clickGuiX;
    auto& m_clickGuiY = refs.m_clickGuiY;
    auto& m_featureSearch = refs.m_featureSearch;
    auto& m_featureSettingsDirty = refs.m_featureSettingsDirty;
    auto& m_features = refs.m_features;
    auto& m_guiScaleDirty = refs.m_guiScaleDirty;
    auto& m_guiScaleIndex = refs.m_guiScaleIndex;
    auto& m_hotkeyCaptureArmed = refs.m_hotkeyCaptureArmed;
    auto& m_hotkeyCaptureCooldownFrames = refs.m_hotkeyCaptureCooldownFrames;
    auto& m_hotkeyCaptureTarget = refs.m_hotkeyCaptureTarget;
    auto& m_hypixelInput = refs.m_hypixelInput;
    auto& m_hypixelQuery = refs.m_hypixelQuery;
    auto& m_hypixelQueryPending = refs.m_hypixelQueryPending;
    auto& m_imePositionEditing = refs.m_imePositionEditing;
    auto& m_mediaSettings = refs.m_mediaSettings;
    auto& m_mediaSettingsDirty = refs.m_mediaSettingsDirty;
    auto& m_menuHotkey = refs.m_menuHotkey;
    auto& m_menuHotkeyDirty = refs.m_menuHotkeyDirty;
    auto& m_navigationScroll = refs.m_navigationScroll;
    auto& m_previousClickGuiPage = refs.m_previousClickGuiPage;
    auto& m_searchActivationStartedAt = refs.m_searchActivationStartedAt;
    auto& m_searchClearProgress = refs.m_searchClearProgress;
    auto& m_searchFocusRequested = refs.m_searchFocusRequested;
    auto& m_searchHoverProgress = refs.m_searchHoverProgress;
    auto& m_searchInputActive = refs.m_searchInputActive;
    auto& m_searchIslandOpen = refs.m_searchIslandOpen;
    auto& m_searchIslandProgress = refs.m_searchIslandProgress;
    auto& m_searchLoadingStartedAt = refs.m_searchLoadingStartedAt;
    auto& m_searchTransitionFrom = refs.m_searchTransitionFrom;
    auto& m_searchTransitionStartedAt = refs.m_searchTransitionStartedAt;
    auto& m_searchTransitionTarget = refs.m_searchTransitionTarget;
    auto& m_settingsScroll = refs.m_settingsScroll;
    auto& m_shieldSelectedId = refs.m_shieldSelectedId;
    auto& m_shieldSelectedUuid = refs.m_shieldSelectedUuid;
    auto& m_shieldWorld = refs.m_shieldWorld;
    auto& m_toggleAnimation = refs.m_toggleAnimation;
    auto& m_waitingForHotkey = refs.m_waitingForHotkey;
    auto& design = refs.m_guiDesign;

    const auto& snapshot = frame.snapshot;
    const auto& interactive = frame.interactive;
    auto& io = frame.io;
    const float windowScale = clickGuiDisplayScale(frame.uiScale,io.DisplaySize.x,io.DisplaySize.y);
    // Window dimensions and other renderer surfaces retain their existing
    // scale. This preference controls only the contents of the Click GUI.
    const float guiWidth=std::min(920.F*windowScale*std::clamp(m_features.clickGuiWidthPercent,40,150)/100.F,
        std::max(1.F,io.DisplaySize.x-40.F*windowScale));
    const float guiHeight=std::min(650.F*windowScale*std::clamp(m_features.clickGuiHeightPercent,40,150)/100.F,
        std::max(1.F,io.DisplaySize.y-78.F*windowScale));
    const float requestedElementScale=windowScale*normalizeGuiElementScale(design.elementScale)/100.F;
    const float uiScale=std::min(requestedElementScale,std::min(guiWidth/460.F,guiHeight/330.F));
    const auto& delta = frame.delta;
    const auto& guiEase = frame.guiEase;
    design.typography=normalizeTypography(design.typography);
    const auto typeface=[&](int index) {
        return frame.typefaces[index] ? frame.typefaces[index] : ImGui::GetFont();
    };
    const int bodyWeight=design.typography.weight==400?0:(design.typography.weight==600?1:2);
    // Bake at the actual display size, rather than enlarging an old atlas.
    // Integer sizes bound the atlas cache during animated layout transitions.
    const float bodySize=std::round(design.typography.size*uiScale);
    ImGui::PushFont(typeface(bodyWeight),bodySize);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10*uiScale,10*uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8*uiScale,6*uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,6*uiScale);
    widgets::begin(design,uiScale);
    if(navigation::categoryForPage(m_clickGuiPage)<0) {
        m_clickGuiPage=m_previousClickGuiPage=0;
        design.pageElapsed=.38F;
    }
    const int owningCategory=navigation::categoryForPage(m_clickGuiPage);
    if(design.category!=owningCategory) {
        design.category=owningCategory;
        design.rememberedPage[owningCategory]=m_clickGuiPage;
    }

    // Detached category tabs own the top bar. The feature rail is scoped to
    // one category; global search temporarily spans all six categories.
    const float baseGuiWidth=guiWidth/uiScale;
    const float baseGuiHeight=guiHeight/uiScale;
    const bool compactWidth=baseGuiWidth<700.F;
    const bool compactHeight=baseGuiHeight<430.F;
    const float baseRailWidth=compactWidth?160.F:194.F;
    constexpr float headerHeight = 116.0F;
    if (m_clickGuiX < -9000.0F) {
        m_clickGuiX = std::max(8.0F, (io.DisplaySize.x - guiWidth) * 0.5F);
        m_clickGuiY = std::max(8.0F, (io.DisplaySize.y - guiHeight) * 0.16F);
    }
    m_clickGuiX=std::clamp(m_clickGuiX,4.F,std::max(4.F,io.DisplaySize.x-guiWidth-4.F));
    m_clickGuiY = std::clamp(m_clickGuiY, 4.0F,
        std::max(4.0F, io.DisplaySize.y - guiHeight - 70.0F * uiScale));
    m_clickGuiThemeProgress = design.reducedMotion ? (m_features.clickGuiLightTheme?1.F:0.F) :
        approachExponential(m_clickGuiThemeProgress,m_features.clickGuiLightTheme?1.F:0.F,12,delta);
    const float theme = std::clamp(m_clickGuiThemeProgress, 0.0F, 1.0F);
    ImVec4 guiSurface = mixColor(ImVec4(0.052F, 0.058F, 0.079F, 0.98F),
                                ImVec4(0.969F, 0.972F, 0.984F, 0.98F), theme);
    ImVec4 guiRail = mixColor(ImVec4(0.043F, 0.047F, 0.064F, 1.0F),
                             ImVec4(0.935F, 0.941F, 0.965F, 1.0F), theme);
    const float guiOpacity = static_cast<float>(std::clamp(
        m_features.clickGuiOpacity, 35, 100)) / 100.0F;
    guiSurface.w = guiOpacity;
    guiRail.w = std::min(1.0F, guiOpacity + 0.03F);
    const ImVec4 guiText = mixColor(ImVec4(0.94F, 0.92F, 0.98F, 1.0F),
                                   ImVec4(0.10F, 0.09F, 0.13F, 1.0F), theme);
    const ImVec4 guiMuted = mixColor(ImVec4(0.65F, 0.69F, 0.77F, 1.0F),
                                    ImVec4(0.33F, 0.37F, 0.46F, 1.0F), theme);
    const ImVec4 guiFrame = mixColor(ImVec4(0.093F, 0.104F, 0.136F, 1.0F),
                                    ImVec4(0.877F, 0.890F, 0.926F, 1.0F), theme);
    const ImVec4 guiScrollbarTrack = mixColor(
        ImVec4(0.065F, 0.058F, 0.082F, 0.72F),
        ImVec4(0.86F, 0.85F, 0.89F, 0.82F), theme);
    const ImVec4 guiScrollbarGrab = mixColor(
        ImVec4(1.0F, 1.0F, 1.0F, 0.72F),
        ImVec4(0.43F, 0.40F, 0.49F, 0.85F), theme);
    const std::array<float, 3U> accentChannels = unpackRgb(
        m_features.clickGuiAccentColor);
    const auto darkAccent=ui::readableAccent(accentChannels,{0.14F,0.13F,0.18F},false);
    const auto lightAccent=ui::readableAccent(accentChannels,{0.965F,0.956F,0.975F},true);
    const ImVec4 guiAccent=mixColor(
        ImVec4(darkAccent[0],darkAccent[1],darkAccent[2],1),
        ImVec4(lightAccent[0],lightAccent[1],lightAccent[2],1),theme);
    const ImVec4 guiSelected=mixColor(guiFrame,guiAccent,0.22F);
    const ImVec4 guiDanger=mixColor(ImVec4(1,.48F,.43F,1),ImVec4(.70F,.12F,.16F,1),theme);
    const ImVec4 guiWarning=mixColor(ImVec4(1,.70F,.38F,1),ImVec4(.53F,.29F,.06F,1),theme);
    const ImVec4 guiSuccess=mixColor(ImVec4(.42F,.84F,.78F,1),ImVec4(.07F,.40F,.34F,1),theme);

    // One-to-one port of google_dynamic_island_search.html. The island keeps
    // Minecraft's session cursor unchanged; ImGui never requests an I-beam,
    // hand, or navigation outline for this control.
    if(m_clickGuiProgress<=0.008F || m_imePositionEditing) {
        m_searchInputActive=false;
        if(m_featureSearch[0U]=='\0') m_searchIslandOpen=false;
    }
    if(m_clickGuiProgress>0.008F && !m_imePositionEditing) {
        const float collapsedWidth=220.0F*uiScale;
        const float expandedWidth=std::min(620.0F*uiScale,
            io.DisplaySize.x-40.0F*uiScale);
        const float height=58.0F*uiScale;
        const ImVec2 centre(io.DisplaySize.x*0.5F,
            io.DisplaySize.y-14.0F*uiScale-height*0.5F);
        const double now=ImGui::GetTime();

        const float previewProgress=std::clamp(m_searchIslandProgress,0.0F,1.0F);
        const float previewWidth=collapsedWidth+(expandedWidth-collapsedWidth)*
            previewProgress+14.0F*uiScale*m_searchHoverProgress*(1.0F-previewProgress);
        const bool hovered=std::abs(io.MousePos.x-centre.x)<=previewWidth*0.5F &&
            std::abs(io.MousePos.y-centre.y)<=height*0.5F;
        const float hoverTarget=interactive && hovered && !m_searchIslandOpen?1.0F:0.0F;
        // Acquisition and recovery deliberately share the same time constant.
        m_searchHoverProgress=approachExponential(
            m_searchHoverProgress,hoverTarget,13.0F,delta);
        m_searchHoverProgress=std::clamp(m_searchHoverProgress,0.0F,1.0F);
        const float hoverEase=1.0F-std::pow(1.0F-m_searchHoverProgress,3.0F);
        if(interactive && !m_blacklistAddOpen &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if(hovered) {
                if(!m_searchIslandOpen) {
                    m_searchFocusRequested=true;
                    m_searchLoadingStartedAt=now;
                    m_searchActivationStartedAt=now;
                }
                m_searchIslandOpen=true;
            } else if(m_searchIslandOpen && m_featureSearch[0U]=='\0') {
                m_searchIslandOpen=false;
            }
        }
        if(m_featureSearch[0U]!='\0') m_searchIslandOpen=true;
        const bool expandTarget=m_searchIslandOpen || m_searchInputActive ||
            m_featureSearch[0U]!='\0';
        const float requested=expandTarget?1.0F:0.0F;
        if(requested!=m_searchTransitionTarget) {
            m_searchTransitionFrom=m_searchIslandProgress;
            m_searchTransitionTarget=requested;
            m_searchTransitionStartedAt=now;
            if(requested>0.5F) m_searchLoadingStartedAt=now;
        }
        if(m_searchTransitionStartedAt<=0.0)
            m_searchTransitionStartedAt=now-0.620;
        const float elapsed=static_cast<float>(now-m_searchTransitionStartedAt);
        const bool clickActivation=requested>0.5F&&
            m_searchActivationStartedAt>0.0&&
            std::abs(m_searchActivationStartedAt-m_searchTransitionStartedAt)<0.002;
        const float transitionTime=std::clamp(elapsed/0.620F,0.0F,1.0F);
        const float transitionEase=clickActivation
            ? SearchActivationMotion::expansion(elapsed)
            : cubicBezierProgress(transitionTime,0.16F,1.0F,0.30F,1.0F);
        m_searchIslandProgress=m_searchTransitionFrom+
            (m_searchTransitionTarget-m_searchTransitionFrom)*transitionEase;

        const float activationScale=clickActivation
            ? SearchActivationMotion::scale(elapsed) : 1.0F;
        const float width=std::min(io.DisplaySize.x-16.0F*uiScale,
            (collapsedWidth+(expandedWidth-collapsedWidth)*
            std::clamp(m_searchIslandProgress,0.0F,1.045F)+
            14.0F*uiScale*hoverEase*(1.0F-std::clamp(
                m_searchIslandProgress,0.0F,1.0F)))*activationScale);
        const ImVec2 minimum(centre.x-width*0.5F,centre.y-height*0.5F);
        const ImVec2 maximum(minimum.x+width,minimum.y+height);
        ImGui::SetNextWindowPos(minimum,ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width,height),ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,guiEase);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,height*0.5F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(0,
            std::max(0.0F,(height-ImGui::GetFontSize())*0.5F)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,1,1,1));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled,ImVec4(1,1,1,0.42F));
        ImGui::PushStyleColor(ImGuiCol_NavCursor,ImVec4(0,0,0,0));
        const auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollWithMouse|
            ImGuiWindowFlags_NoNavFocus|
            (!interactive?ImGuiWindowFlags_NoInputs:0);
        if(ImGui::Begin("##FeatureSearchIsland",nullptr,flags)) {
            ImDrawList* const draw=ImGui::GetWindowDrawList();
            // Flat material surface: no shadow and no cursor override. Hover
            // is expressed through a nonlinear width/tonal lift only.
            const int surface=static_cast<int>(9.0F+8.0F*hoverEase*
                (1.0F-std::clamp(m_searchIslandProgress,0.0F,1.0F)));
            draw->AddRectFilled(minimum,maximum,
                IM_COL32(surface,surface,surface,static_cast<int>(255.0F*guiEase)),
                height*0.5F);
            if(hoverEase>0.002F && m_searchIslandProgress<0.08F)
                draw->AddRect(minimum,maximum,
                    IM_COL32(static_cast<int>(guiAccent.x*255.0F),
                        static_cast<int>(guiAccent.y*255.0F),
                        static_cast<int>(guiAccent.z*255.0F),
                        static_cast<int>(92.0F*hoverEase*guiEase)),
                    height*0.5F,0,std::max(1.0F,1.25F*uiScale));

            // Exact 20 px icon box with a 11 px ring and 7 px handle.
            const ImVec2 iconOrigin(minimum.x+20.0F*uiScale,
                                    centre.y-10.0F*uiScale);
            const ImU32 white=IM_COL32(255,255,255,
                static_cast<int>(235.0F*guiEase));
            draw->AddCircle(ImVec2(iconOrigin.x+6.5F*uiScale,
                                   iconOrigin.y+6.5F*uiScale),
                            6.5F*uiScale,white,24,2.0F*uiScale);
            draw->AddLine(ImVec2(iconOrigin.x+12.0F*uiScale,
                                 iconOrigin.y+14.0F*uiScale),
                          ImVec2(iconOrigin.x+17.0F*uiScale,
                                 iconOrigin.y+19.0F*uiScale),
                          white,2.0F*uiScale);

            const float inputOpacity=expandTarget?
                std::clamp(static_cast<float>((now-m_searchTransitionStartedAt)/0.180),
                           0.0F,1.0F):
                1.0F-std::clamp(static_cast<float>(
                    (now-m_searchTransitionStartedAt)/0.180),0.0F,1.0F);
            if(!expandTarget) {
                const char* const label="Search";
                const float labelSize=14.0F*uiScale;
                const ImVec2 labelMeasure=ImGui::GetFont()->CalcTextSizeA(
                    labelSize,FLT_MAX,0.0F,label);
                draw->AddText(ImGui::GetFont(),labelSize,
                    ImVec2(minimum.x+52.0F*uiScale,
                           centre.y-labelMeasure.y*0.5F),
                    IM_COL32(255,255,255,static_cast<int>(209.0F*guiEase)),label);
                m_searchInputActive=false;
                ImGui::SetCursorScreenPos(minimum);
                (void)ImGui::InvisibleButton("##openFeatureSearch",
                                              ImVec2(width,height));
            } else if(m_searchIslandProgress>0.08F) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha,guiEase*inputOpacity);
                ImGui::SetCursorPos(ImVec2(52.0F*uiScale,0.0F));
                ImGui::SetNextItemWidth(std::max(20.0F,width-108.0F*uiScale));
                if(m_searchFocusRequested &&
                   now-m_searchTransitionStartedAt>=0.180) {
                    ImGui::SetKeyboardFocusHere();
                    m_searchFocusRequested=false;
                }
                if(ImGui::InputTextWithHint("##featureSearch","Search features...",
                        m_featureSearch.data(),m_featureSearch.size())) {
                    m_navigationScroll={};
                    m_clickGuiNavPosition=0;
                }
                m_searchInputActive=ImGui::IsItemActive();
                ImGui::PopStyleVar();

                const bool hasText=m_featureSearch[0U]!='\0';
                const float clearRate=hasText?10.0F:8.0F;
                const float clearTarget=hasText?1.0F:0.0F;
                m_searchClearProgress+=(clearTarget-m_searchClearProgress)*
                    (1.0F-std::exp(-clearRate*std::max(0.0F,delta)));
                m_searchClearProgress=std::clamp(m_searchClearProgress,0.0F,1.0F);
                if(m_searchClearProgress>0.01F) {
                    const float clearScale=0.7F+0.3F*m_searchClearProgress;
                    const ImVec2 clearCentre(maximum.x-34.0F*uiScale,centre.y);
                    const float clearRadius=14.0F*uiScale*clearScale;
                    draw->AddCircleFilled(clearCentre,clearRadius,
                        IM_COL32(255,255,255,static_cast<int>(
                            25.5F*guiEase*m_searchClearProgress)),32);
                    const ImU32 clearColor=IM_COL32(255,255,255,static_cast<int>(
                        191.0F*guiEase*m_searchClearProgress));
                    draw->AddLine(ImVec2(clearCentre.x-4.0F*uiScale,
                                         clearCentre.y-4.0F*uiScale),
                                  ImVec2(clearCentre.x+4.0F*uiScale,
                                         clearCentre.y+4.0F*uiScale),
                                  clearColor,1.6F*uiScale);
                    draw->AddLine(ImVec2(clearCentre.x+4.0F*uiScale,
                                         clearCentre.y-4.0F*uiScale),
                                  ImVec2(clearCentre.x-4.0F*uiScale,
                                         clearCentre.y+4.0F*uiScale),
                                  clearColor,1.6F*uiScale);
                    if(hasText) {
                        ImGui::SetCursorScreenPos(
                            ImVec2(clearCentre.x-14.0F*uiScale,
                                   clearCentre.y-14.0F*uiScale));
                        if(ImGui::InvisibleButton("##clearFeatureSearch",
                                                  ImVec2(28.0F*uiScale,
                                                         28.0F*uiScale))) {
                            m_featureSearch.fill('\0');
                            m_navigationScroll={};
                            m_clickGuiNavPosition=0;
                            m_searchFocusRequested=true;
                        }
                    }
                }
            }

            // Exact HTML elastic Material rail: one white bar, 1450 ms cycle.
            const float trackAlpha=expandTarget?
                std::clamp(static_cast<float>(
                    (now-m_searchTransitionStartedAt)/0.180),0.0F,1.0F):
                0.0F;
            if(trackAlpha>0.001F) {
                const float trackLeft=minimum.x+17.0F*uiScale;
                const float trackRight=maximum.x-17.0F*uiScale;
                const float trackWidth=std::max(1.0F,trackRight-trackLeft);
                const float phase=static_cast<float>(std::fmod(std::max(0.0,
                    now-m_searchLoadingStartedAt),1.450)/1.450);
                const float travel=smootherStep(phase);
                const float centrePosition=-0.10F+(1.08F+0.10F)*travel;
                float stretch=1.0F;
                if(phase<0.32F) stretch=smootherStep(phase/0.32F);
                else if(phase>=0.58F)
                    stretch=1.0F-smootherStep((phase-0.58F)/0.42F);
                const float length=0.055F+(0.46F-0.055F)*stretch;
                const float lead=std::sin(phase*3.14159265F)*0.055F;
                const float left=centrePosition-length*0.5F+lead;
                const float x0=trackLeft+left*trackWidth;
                const float x1=x0+length*trackWidth;
                draw->PushClipRect(ImVec2(trackLeft,maximum.y-2.5F*uiScale),
                                   ImVec2(trackRight,maximum.y),true);
                draw->AddRectFilled(
                    ImVec2(x0,maximum.y-2.5F*uiScale),ImVec2(x1,maximum.y),
                    IM_COL32(255,255,255,static_cast<int>(
                        245.0F*guiEase*trackAlpha)),1.25F*uiScale);
                draw->PopClipRect();
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(5);
    }

    if (m_clickGuiProgress > 0.008F) {
        // iPadOS Spotlight-inspired materialization: the surface forms around
        // its centre with a short decelerating scale/lensing motion and fully
        // reversible opacity. Keeping every child inside this one transformed
        // parent also prevents the navigation rail from surviving a close.
        // Opacity/blur use the same temporal envelope as geometry. The former
        // fourth-power decelerate reached near-opaque too early and made an
        // otherwise longer motion still look abrupt.
        const float spotlightEase = guiEase;
        // Drive geometry directly from the spring, including its overshoot.
        // A second eased/sine motion would rebound a fraction of a beat later.
        // Opening gathers inward from 126%; closing scatters outward again.
        // Keep the original spring-driven trajectory and its small overshoot.
        const float spotlightScale = 1.26F - 0.26F * m_clickGuiProgress;
        const ImVec2 guiCenter(m_clickGuiX + guiWidth * 0.5F,
                               m_clickGuiY + guiHeight * 0.5F);
        // Layout always uses the final rectangle. Once ImGui has generated the
        // complete draw lists, they are transformed around guiCenter as one
        // flat image. This removes every top-left layout origin from the visual
        // path and makes all four directions perfectly symmetric.
        ImGui::SetNextWindowPos(ImVec2(m_clickGuiX, m_clickGuiY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(guiWidth, guiHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0F * uiScale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,ImVec2(0.5F,0.5F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, guiSurface);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, guiText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, guiMuted);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, guiFrame);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                              mixColor(ImVec4(0.19F, 0.17F, 0.24F, 1.0F),
                                       ImVec4(0.82F, 0.79F, 0.85F, 1.0F), theme));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, guiAccent);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, guiAccent);
        ImGui::PushStyleColor(ImGuiCol_Button, guiFrame);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              mixColor(guiFrame,guiAccent,0.16F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, guiSelected);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, mixColor(guiSurface,guiFrame,0.12F));
        ImGui::PushStyleColor(ImGuiCol_Border, mixColor(guiFrame,guiMuted,0.30F));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, guiSelected);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, guiAccent);
        ImGui::PushStyleColor(ImGuiCol_Header, guiSelected);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, mixColor(guiFrame,guiAccent,0.16F));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, guiSelected);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, guiScrollbarTrack);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, guiScrollbarGrab);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                              mixColor(guiScrollbarGrab,guiAccent,0.35F));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, guiAccent);
        const ImGuiWindowFlags clickFlags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            (m_imePositionEditing ? ImGuiWindowFlags_NoInputs : 0);
        ImGuiWindow* clickGuiRootForDiffusion=nullptr;
        ImDrawList* clickGuiDrawForDiffusion=nullptr;
        if (ImGui::Begin("##McOverlayClickGui", nullptr, clickFlags)) {
            const FeatureSettings featuresBefore = m_features;
            bool changed = false;
            // ESP master is intentionally retired. Individual Player/Bed ESP
            // pages are the authoritative switches.
            if (!m_features.espEnabled) {
                m_features.espEnabled = true;
                changed = true;
            }

            const ImVec2 windowPosition = ImGui::GetWindowPos();
            const ImVec2 windowSize = ImGui::GetWindowSize();
            ImDrawList* const windowDraw = ImGui::GetWindowDrawList();
            configureGuiDrawList(windowDraw,spotlightScale);
            const int parentContentVertexStart = 0;
            ImDrawList* const backgroundDraw = ImGui::GetBackgroundDrawList();
            const int shadowVertexStart = backgroundDraw->VtxBuffer.Size;
            // Parent presentation alpha is applied once to the completed root
            // and every child draw list below. Doing it post-layout keeps raw
            // custom draw calls and regular ImGui widgets on the same fade path.
            const auto fadedGuiColor = [&](const ImVec4 color) noexcept {
                return ImGui::ColorConvertFloat4ToU32(color);
            };
            windowDraw->PushClipRect(windowPosition,
                ImVec2(windowPosition.x + windowSize.x,
                       windowPosition.y + windowSize.y), true);
            // renderInventoryBlur() has already composited a true Gaussian
            // backdrop into the game framebuffer. Do not paste m_blurTexture
            // here: that texture is the *unblurred* capture and used to sharpen
            // the game back through the translucent ClickGUI surface.
            for (int shadow = 4; shadow >= 1; --shadow) {
                const float spread = static_cast<float>(shadow) * 4.0F * uiScale;
                backgroundDraw->AddRectFilled(
                    ImVec2(windowPosition.x - spread,
                           windowPosition.y - spread + 7.0F * uiScale),
                    ImVec2(windowPosition.x + windowSize.x + spread,
                           windowPosition.y + windowSize.y + spread + 7.0F * uiScale),
                    IM_COL32(4, 3, 8, 14),
                    20.0F * uiScale + spread);
            }
            const ImVec4 hairline=mixColor(ImVec4(1,1,1,.07F),ImVec4(.10F,.15F,.30F,.10F),theme);
            windowDraw->AddRectFilled(
                ImVec2(windowPosition.x,windowPosition.y+headerHeight*uiScale),
                ImVec2(windowPosition.x+baseRailWidth*uiScale,windowPosition.y+windowSize.y),
                fadedGuiColor(guiRail),20*uiScale,ImDrawFlags_RoundCornersBottomLeft);
            windowDraw->AddLine(ImVec2(windowPosition.x,windowPosition.y+headerHeight*uiScale),
                ImVec2(windowPosition.x+windowSize.x,windowPosition.y+headerHeight*uiScale),fadedGuiColor(hairline));
            windowDraw->AddLine(ImVec2(windowPosition.x+baseRailWidth*uiScale,windowPosition.y+headerHeight*uiScale),
                ImVec2(windowPosition.x+baseRailWidth*uiScale,windowPosition.y+windowSize.y),fadedGuiColor(hairline));
            windowDraw->AddRect(windowPosition,ImVec2(windowPosition.x+windowSize.x,windowPosition.y+windowSize.y),
                fadedGuiColor(hairline),20*uiScale,0,uiScale);

            // Header drag surface spans both panes without stealing controls.
            ImGui::SetCursorScreenPos(ImVec2(windowPosition.x + 14.0F * uiScale,
                                              windowPosition.y + 8.0F * uiScale));
            ImGui::InvisibleButton("##clickGuiDrag",
                                   ImVec2((baseGuiWidth - 150.0F) * uiScale,
                                          34.0F * uiScale));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                m_clickGuiX += io.MouseDelta.x;
                m_clickGuiY += io.MouseDelta.y;
                const float maxX = std::max(4.0F, io.DisplaySize.x - guiWidth - 4.0F);
                const float maxY = std::max(4.0F, io.DisplaySize.y - guiHeight - 4.0F);
                m_clickGuiX = std::clamp(m_clickGuiX, 4.0F, maxX);
                m_clickGuiY = std::clamp(m_clickGuiY, 4.0F, maxY);
            }

            ImFont* const legacyBoldFont = m_boldFonts[static_cast<std::size_t>(
                std::clamp(m_guiScaleIndex, 0, 3))] != nullptr
                ? m_boldFonts[static_cast<std::size_t>(
                    std::clamp(m_guiScaleIndex, 0, 3))] : ImGui::GetFont();
            ImFont* const boldFont=frame.typefaces[bodyWeight==2?2:1]
                ? frame.typefaces[bodyWeight==2?2:1] : legacyBoldFont;
            // A small vector mark keeps the chrome independent of raster assets.
            const ImVec2 mark(windowPosition.x+31*uiScale,windowPosition.y+29*uiScale);
            const ImU32 ink=fadedGuiColor(guiAccent);
            windowDraw->AddLine(ImVec2(mark.x-10*uiScale,mark.y+9*uiScale),ImVec2(mark.x,mark.y-10*uiScale),ink,2.3F*uiScale);
            windowDraw->AddLine(ImVec2(mark.x,mark.y-10*uiScale),ImVec2(mark.x+10*uiScale,mark.y+9*uiScale),ink,2.3F*uiScale);
            windowDraw->AddLine(ImVec2(mark.x-5*uiScale,mark.y+1*uiScale),ImVec2(mark.x+5*uiScale,mark.y+1*uiScale),ink,2.3F*uiScale);
            windowDraw->AddText(boldFont,ImGui::GetFontSize()*1.05F,
                ImVec2(windowPosition.x+54*uiScale,windowPosition.y+19*uiScale),fadedGuiColor(guiText),"ARCVEIL");
            const float brandWidth=boldFont->CalcTextSizeA(ImGui::GetFontSize()*1.05F,1000,0,"ARCVEIL").x;
            const char* currentPage="Interface";
            for(const auto& row:navigation::rows)
                if(row.page==m_clickGuiPage) {currentPage=row.label;break;}
            const int currentCategory=std::max(0,navigation::categoryForPage(m_clickGuiPage));
            const float pathFont=ImGui::GetFontSize()*.84F;
            ImVec2 pathPosition(windowPosition.x+68*uiScale+brandWidth,windowPosition.y+22*uiScale);
            const auto pathPart=[&](const char* label,ImFont* font,const ImVec4& color) {
                windowDraw->AddText(font,pathFont,pathPosition,fadedGuiColor(color),label);
                pathPosition.x+=font->CalcTextSizeA(pathFont,10000,0,label).x;
            };
            if(!compactWidth) {
                pathPart("/  ",ImGui::GetFont(),guiMuted);
                pathPart(navigation::categories[currentCategory],ImGui::GetFont(),guiMuted);
                pathPart("  /  ",ImGui::GetFont(),guiMuted);
                pathPart(currentPage,boldFont,guiText);
            }
            windowDraw->AddLine(ImVec2(windowPosition.x+22*uiScale,windowPosition.y+55*uiScale),
                ImVec2(windowPosition.x+(baseGuiWidth-24)*uiScale,windowPosition.y+55*uiScale),fadedGuiColor(hairline));

            if(design.category<0) design.category=std::max(0,navigation::categoryForPage(m_clickGuiPage));
            const auto selectPage=[&](int next) noexcept {
                if(m_clickGuiPage==next) return;
                m_previousClickGuiPage=m_clickGuiPage; m_clickGuiPage=next;
                m_clickGuiPageProgress=0; design.pageElapsed=0;
                const int category=navigation::categoryForPage(next);
                if(category>=0) {design.category=category;design.rememberedPage[category]=next;}
            };
            const float tabGap=8*uiScale;
            const float tabWidth=((baseGuiWidth-44)*uiScale-5*tabGap)/6;
            if(compactWidth) {
                ImGui::SetCursorScreenPos(ImVec2(windowPosition.x+22*uiScale,windowPosition.y+66*uiScale));
                ImGui::SetNextItemWidth((baseGuiWidth-44)*uiScale);
                int category=design.category;
                if(widgets::Combo("##categorySwitcher",&category,navigation::categories.data(),6)) {
                    design.category=category;design.railElapsed=0;m_navigationScroll={};
                    selectPage(design.rememberedPage[category]);
                }
            } else for(int category=0;category<6;++category) {
                const ImVec2 pos(windowPosition.x+22*uiScale+category*(tabWidth+tabGap),windowPosition.y+70*uiScale);
                ImGui::SetCursorScreenPos(pos);ImGui::PushID(category+400);
                const auto id=ImGui::GetID("category");
                if(ImGui::InvisibleButton("category",ImVec2(tabWidth,33*uiScale))) {
                    if(design.category!=category) {
                        design.category=category;design.railElapsed=0;m_navigationScroll={};
                        selectPage(design.rememberedPage[category]);
                    }
                }
                const bool selected=category==design.category;
                auto& motion=widgets::motion(id,ImGui::IsItemHovered(),ImGui::IsItemActive());
                float& selection=design.categorySelection[category];
                selection=design.reducedMotion?(selected?1.F:0.F):approachExponential(selection,selected?1.F:0.F,19,delta);
                const float lift=(-motion.hover+motion.press*1.5F)*uiScale;
                const ImVec2 a(pos.x,pos.y+lift),b(pos.x+tabWidth,pos.y+33*uiScale+lift);
                ImVec4 bg=mixColor(guiSurface,guiFrame,.45F+motion.hover*.35F);
                bg=mixColor(bg,guiAccent,selection*.18F);
                windowDraw->AddRectFilled(a,b,fadedGuiColor(bg),8*uiScale);
                windowDraw->AddRect(a,b,fadedGuiColor(mixColor(hairline,ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,.6F),selection*.65F+motion.hover*.15F)),8*uiScale,0,uiScale);
                float fontSize=ImGui::GetFontSize()*.90F;
                const auto label=navigation::categories[category];
                const float naturalWidth=boldFont->CalcTextSizeA(fontSize,1000,0,label).x;
                fontSize*=std::min(1.F,(tabWidth-14*uiScale)/std::max(1.F,naturalWidth));
                fontSize=std::floor(fontSize);
                const auto size=boldFont->CalcTextSizeA(fontSize,1000,0,label);
                windowDraw->AddText(boldFont,fontSize,ImVec2(a.x+(tabWidth-size.x)*.5F,a.y+(33*uiScale-size.y)*.5F),
                    fadedGuiColor(mixColor(guiMuted,guiText,std::max(selection,motion.hover*.7F))),label);
                ImGui::PopID();
            }

            // A clipped child owns scrolling. Row height is independent of
            // window height: resizing changes the viewport, never row spacing.
            // Retired pages may still be selected in a resident Agent that was
            // upgraded in-place. Redirect them before any title, master toggle
            // or page body can be rendered.
            if (m_clickGuiPage == 15 || m_clickGuiPage == 17) {
                m_clickGuiPage = 0;
                m_previousClickGuiPage = 0;
                m_clickGuiPageProgress = 1.0F;
            }
            const auto filtered = navigation::filterCategory(design.category,m_featureSearch.data());
            const std::span<const navigation::Row> rows(filtered.rows.data(),filtered.count);
            const auto enabledPage = [&](int page) {
                switch(page) {
                case 0: return m_features.entityEspEnabled; case 1: return m_features.bedEspEnabled;
                case 2: return m_features.nametagEnabled; case 3: return m_features.bedThreatAlertsEnabled;
                case 4: return m_features.safewalkEnabled; case 5: return m_features.scaffoldEnabled;
                case 6: return m_features.flyEnabled; case 7: return m_features.bhopEnabled;
                case 8: return m_features.aimAssistEnabled; case 9: return m_features.hypixelPanelEnabled;
                case 10: return m_features.debugChatEnabled; case 11: return m_blacklist.showWithClickGui;
                case 12: return m_features.textGuiEnabled; case 14: return m_features.fireballEspEnabled;
                case 16: return m_features.bowPredictionEnabled || m_features.knockbackPredictionEnabled;
                case 18: return m_features.fullscreenImeFixEnabled;
                case 19: return m_mediaSettings.enabled;
                case 20: return m_features.bedBreakerEnabled;
                case 21: return m_features.localVelocityEnabled;
                case 22: return m_features.freeLookEnabled;
                case 23: return m_features.smartHotbarEnabled;
                case 24: return m_features.sprintEnabled;
                case 25: return m_features.attackShieldEnabled;
                default: return false;
                }
            };
            ImGui::SetCursorScreenPos(ImVec2(windowPosition.x + 12.0F * uiScale,
                windowPosition.y + 162.0F * uiScale));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            beginSmoothChild("##navigationScroll",
                ImVec2((baseRailWidth - 24.0F) * uiScale,
                       (baseGuiHeight - 180.0F) * uiScale), m_navigationScroll, delta);
            ImDrawList* const navDraw = ImGui::GetWindowDrawList();
            configureGuiDrawList(navDraw,spotlightScale);
            const ImVec2 navOrigin = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            windowDraw->AddText(boldFont,ImGui::GetFontSize()*.82F,
                ImVec2(windowPosition.x+26*uiScale,windowPosition.y+138*uiScale),fadedGuiColor(guiMuted),
                m_featureSearch[0]?"SEARCH RESULTS":navigation::categories[design.category]);
            design.railElapsed=std::min(1.F,design.railElapsed+delta);
            float contentY=0;
            for (const auto& row : rows) {
                const float reveal=design.reducedMotion?1.F:cubicBezierProgress(std::clamp(
                    (design.railElapsed-contentY/44*.018F)/.26F,0.F,1.F),.16F,1.F,.3F,1.F);
                const ImVec2 position(navOrigin.x + (2.0F-7.0F*(1-reveal))*uiScale,
                    navOrigin.y + contentY * uiScale);
                if (row.page < 0) {
                    navDraw->AddText(boldFont, ImGui::GetFontSize() * 0.82F,
                        ImVec2(position.x + 12.0F * uiScale, position.y + 9.0F * uiScale),
                        fadedGuiColor(guiAccent), row.label);
                    contentY += 28.0F;
                    continue;
                }
                ImGui::SetCursorScreenPos(position);
                ImGui::PushID(row.page);
                ImGui::InvisibleButton("##nav", ImVec2(rowWidth - 2.0F * uiScale, 38.0F * uiScale),
                    ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
                const bool rowClicked=ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool statusClicked=rowClicked&&io.MousePos.x>position.x+rowWidth-28*uiScale;
                if(statusClicked) {
                    bool* toggle=nullptr;
                    switch(row.page) {
                    case 0:toggle=&m_features.entityEspEnabled;break;
                    case 1:toggle=&m_features.bedEspEnabled;break;
                    case 2:toggle=&m_features.nametagEnabled;break;
                    case 3:toggle=&m_features.bedThreatAlertsEnabled;break;
                    case 4:toggle=&m_features.safewalkEnabled;break;
                    case 5:toggle=&m_features.scaffoldEnabled;break;
                    case 6:toggle=&m_features.flyEnabled;break;
                    case 7:toggle=&m_features.bhopEnabled;break;
                    case 8:toggle=&m_features.aimAssistEnabled;break;
                    case 9:toggle=&m_features.hypixelPanelEnabled;break;
                    case 10:toggle=&m_features.debugChatEnabled;break;
                    case 11:
                        m_blacklist.showWithClickGui=!m_blacklist.showWithClickGui;
                        m_blacklistAction={};
                        m_blacklistAction.type=BlacklistAction::Type::Settings;
                        m_blacklistAction.panelEnabled=m_blacklist.panelEnabled;
                        m_blacklistAction.matchAlertsEnabled=m_blacklist.matchAlertsEnabled;
                        m_blacklistAction.allowIdOnlyNicks=m_blacklist.allowIdOnlyNicks;
                        m_blacklistAction.showWithClickGui=m_blacklist.showWithClickGui;
                        m_blacklistAction.collapsed=m_blacklist.collapsed;
                        m_blacklistAction.panelOpacity=m_blacklist.panelOpacity;
                        m_blacklistAction.contentScale=m_blacklist.contentScale;
                        m_blacklistAction.panelColor=m_blacklist.panelColor;
                        m_blacklistActionDirty=true;
                        break;
                    case 12:toggle=&m_features.textGuiEnabled;break;
                    case 14:toggle=&m_features.fireballEspEnabled;break;
                    case 16: {
                        const bool enabled=!(m_features.bowPredictionEnabled||m_features.knockbackPredictionEnabled);
                        m_features.bowPredictionEnabled=enabled;
                        m_features.knockbackPredictionEnabled=enabled;
                        changed=true;
                        break;
                    }
                    case 18:toggle=&m_features.fullscreenImeFixEnabled;break;
                    case 19:m_mediaSettings.enabled=!m_mediaSettings.enabled;m_mediaSettingsDirty=true;break;
                    case 20:toggle=&m_features.bedBreakerEnabled;break;
                    case 21:toggle=&m_features.localVelocityEnabled;break;
                    case 22:toggle=&m_features.freeLookEnabled;break;
                    case 23:toggle=&m_features.smartHotbarEnabled;break;
                    case 24:toggle=&m_features.sprintEnabled;break;
                    case 25:toggle=&m_features.attackShieldEnabled;break;
                    }
                    if(toggle) {*toggle=!*toggle;changed=true;}
                }
                if((rowClicked&&!statusClicked)||ImGui::IsItemClicked(ImGuiMouseButton_Right)) selectPage(row.page);
                const bool selected = row.page == m_clickGuiPage;
                // Selection dissolves in place instead of a highlight sliding
                // through unrelated pages. The trailing marker shares the fade.
                float& selection=m_clickGuiNavSelection[static_cast<std::size_t>(row.page)];
                selection+=((selected?1.0F:0.0F)-selection)*(1.0F-std::exp(-20.0F*delta));
                const bool enabled=enabledPage(row.page);
                const ImVec2 rowEnd(navOrigin.x+rowWidth-2.F*uiScale,position.y+38.0F*uiScale);
                const ImVec4 enabledSurface=mixColor(guiRail,guiFrame,.46F);
                float& enabledFade=m_clickGuiNavEnabled[static_cast<std::size_t>(row.page)];
                enabledFade=approachExponential(enabledFade,enabled?1.0F:0.0F,15.0F,delta);
                if(enabledFade>0.002F) navDraw->AddRectFilled(position,rowEnd,
                    fadedGuiColor(ImVec4(enabledSurface.x,enabledSurface.y,enabledSurface.z,enabledFade)),7.0F*uiScale);
                if(selection>0.002F) {
                    navDraw->AddRectFilled(position,rowEnd,
                        fadedGuiColor(ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,
                            selection*(enabled?0.20F:0.12F))),7.0F*uiScale);
                    const float inset=1.F*uiScale+navDraw->_FringeScale;
                    navDraw->AddRect(ImVec2(position.x+inset,position.y+inset),
                        ImVec2(rowEnd.x-inset,rowEnd.y-inset),
                        fadedGuiColor(ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,selection*.35F)),
                        std::max(0.F,7.F*uiScale-inset),0,uiScale/spotlightScale);
                }
                const float statusX=rowEnd.x-14*uiScale;
                const auto statusColor=mixColor(mixColor(guiFrame,guiMuted,.45F),guiSuccess,enabledFade);
                navDraw->AddCircleFilled(ImVec2(statusX,position.y+19*uiScale),3*uiScale,fadedGuiColor(statusColor),20);
                float& hover = m_clickGuiNavHover[static_cast<std::size_t>(row.page)];
                hover += ((ImGui::IsItemHovered() ? 1.0F : 0.0F) - hover) *
                    (1.0F - std::exp(-18.0F * delta));
                if (hover > 0.005F) navDraw->AddRectFilled(position,
                    rowEnd,
                    fadedGuiColor(ImVec4(guiAccent.x, guiAccent.y, guiAccent.z, 0.08F * hover)), 9.0F * uiScale);
                ImFont* font=selected ? boldFont : ImGui::GetFont();
                ImVec2 textPosition(position.x + 16.0F * uiScale,
                    position.y + (38.0F * uiScale-ImGui::GetFontSize())*0.5F);
                float navigationFont=ImGui::GetFontSize()*.96F;
                const float navigationWidth=font->CalcTextSizeA(navigationFont,1000,0,row.label).x;
                // Reserve the complete hover travel before fitting the font;
                // hovering must never select a different baked font size.
                navigationFont*=std::min(1.F,(statusX-12*uiScale-textPosition.x)/std::max(1.F,navigationWidth));
                navigationFont=std::floor(navigationFont);
                textPosition.x+=hover*2*uiScale;
                const ImVec4 labelColor=mixColor(guiMuted,guiText,std::max(selection,hover*.75F));
                const ImVec4 flowColor=mixColor(ImVec4(1,1,1,1),ImVec4(.26F,.16F,.54F,1),theme);
                drawNavigationLabel(navDraw,font,navigationFont,textPosition,row.label,
                    labelColor,flowColor,enabled,design.reducedMotion,ImGui::GetTime());
                ImGui::PopID();
                contentY += 44.0F;
            }
            if(rows.empty()) { ImGui::TextDisabled("No matching features"); contentY=32; }
            ImGui::SetCursorScreenPos(ImVec2(navOrigin.x,
                navOrigin.y+(contentY+7.0F)*uiScale));
            ImGui::Dummy(ImVec2(1.0F,1.0F));
            endSmoothChild(m_navigationScroll,delta);
            ImGui::PopStyleVar();

            constexpr std::array<const char*, 26U> pageTitles{{
                "Player ESP", "Bed ESP", "Nametags", "Bed Alerts",
                "SafeWalk", "Scaffold", "Flight", "Bunny Hop", "Aim Assist",
                "Player Stats", "Diagnostics", "Blacklist", "Module List", "Interface",
                "Fireball ESP", "", "Trajectories", "",
                "Input Method", "Now Playing", "Bed Breaker", "Velocity", "FreeLook",
                "Smart Hotbar", "Sprint", "Attack Shield"}};
            constexpr std::array<const char*, 26U> pageDescriptions{{
                "Player outlines and teammate presentation",
                "Bed geometry and defense material card",
                "Confirmed-player identity and live health cards",
                "Persistent own-bed proximity warning",
                "Edge-aware crouch assistance and release timing",
                "Predictive hotbar block placement",
                "Local movement flight controls",
                "Air momentum and landing jump controls",
                "Separate Smooth Aim and exact Lock On controllers",
                "Automatic TAB roster statistics",
                "Local diagnostics visible only to you",
                "UUID-based player records and encounter warnings",
                "Draggable enabled-feature list",
                "Appearance, scale and input binding",
                "Compact local-world ghast fireball boxes",
                "",
                "Knockback evidence and frame-synchronous bow impacts",
                "",
                "Windows IME status and candidate overlay for fullscreen",
                "Windows media transport, artwork and playback controls",
                "Visible local-world bed path and automatic tool selection",
                "Probability and independent horizontal/vertical knockback response",
                "Hold-to-look camera orbit without rotating your player",
                "Category shortcuts that follow Minecraft's own hotbar bindings",
                "Automatic sprint with Silent Lock priority",
                "Source-aware impulse suppression, with an optional wildcard"}};
            const int page = std::clamp(m_clickGuiPage, 0, 25);
            const float contentX = windowPosition.x + (baseRailWidth + 22.0F) * uiScale;
            const float headingWidth=std::max(20.F,(baseGuiWidth-baseRailWidth-(compactWidth?130.F:210.F))*uiScale);
            const char* pageTitle=pageTitles[static_cast<std::size_t>(page)];
            const float titleSize=ImGui::GetFontSize()*(compactHeight?1.18F:1.52F);
            const float fittedTitle=titleSize*std::min(1.F,headingWidth/
                std::max(1.F,boldFont->CalcTextSizeA(titleSize,10000,0,pageTitle).x));
            windowDraw->AddText(boldFont, fittedTitle,
                ImVec2(contentX, windowPosition.y + (compactHeight?132.F:135.F) * uiScale),
                fadedGuiColor(guiText),
                pageTitle);
            const float descriptionFont=ImGui::GetFontSize()*.92F;
            const float descriptionWidth=(baseGuiWidth-baseRailWidth-44)*uiScale;
            const char* description=pageDescriptions[static_cast<std::size_t>(page)];
            const float descriptionHeight=ImGui::GetFont()->CalcTextSizeA(
                descriptionFont,10000,descriptionWidth,description).y;
            const float bodyTop=compactHeight?188.F:std::max(220.F,173.F+descriptionHeight/uiScale+23.F);
            if(!compactHeight) windowDraw->AddText(ImGui::GetFont(),descriptionFont,
                ImVec2(contentX, windowPosition.y + 173.0F * uiScale),
                fadedGuiColor(guiMuted),description,nullptr,descriptionWidth);
            windowDraw->AddLine(
                ImVec2(contentX, windowPosition.y + (bodyTop-16.F) * uiScale),
                ImVec2(windowPosition.x + (baseGuiWidth - 20.0F) * uiScale,
                       windowPosition.y + (bodyTop-16.F) * uiScale),
                fadedGuiColor(mixColor(
                    ImVec4(1, 1, 1, 0.09F), ImVec4(0, 0, 0, 0.10F), theme)));

            bool* pageMaster = nullptr;
            float* pageMasterAnimation = nullptr;
            switch (page) {
            case 0: pageMaster = &m_features.entityEspEnabled;
                    pageMasterAnimation = &m_toggleAnimation[1]; break;
            case 1: pageMaster = &m_features.bedEspEnabled;
                    pageMasterAnimation = &m_toggleAnimation[2]; break;
            case 2: pageMaster = &m_features.nametagEnabled;
                    pageMasterAnimation = &m_toggleAnimation[16]; break;
            case 3: pageMaster = &m_features.bedThreatAlertsEnabled;
                    pageMasterAnimation = &m_toggleAnimation[5]; break;
            case 4: pageMaster = &m_features.safewalkEnabled;
                    pageMasterAnimation = &m_toggleAnimation[26]; break;
            case 5: pageMaster = &m_features.scaffoldEnabled;
                    pageMasterAnimation = &m_toggleAnimation[27]; break;
            case 6: pageMaster = &m_features.flyEnabled;
                    pageMasterAnimation = &m_toggleAnimation[28]; break;
            case 7: pageMaster = &m_features.bhopEnabled;
                    pageMasterAnimation = &m_toggleAnimation[29]; break;
            case 8: pageMaster = &m_features.aimAssistEnabled;
                    pageMasterAnimation = &m_toggleAnimation[30]; break;
            case 9: pageMaster = &m_features.hypixelPanelEnabled;
                    pageMasterAnimation = &m_toggleAnimation[4]; break;
            case 10: pageMaster = &m_features.debugChatEnabled;
                    pageMasterAnimation = &m_toggleAnimation[11]; break;
            case 11: pageMaster = &m_blacklist.showWithClickGui;
                     pageMasterAnimation = &m_toggleAnimation[33]; break;
            case 12: pageMaster = &m_features.textGuiEnabled;
                     pageMasterAnimation = &m_toggleAnimation[31]; break;
            case 14: pageMaster = &m_features.fireballEspEnabled;
                     pageMasterAnimation = &m_toggleAnimation[34]; break;
            case 18: pageMaster = &m_features.fullscreenImeFixEnabled;
                     pageMasterAnimation = &m_toggleAnimation[44]; break;
            case 19: pageMaster = &m_mediaSettings.enabled;
                     pageMasterAnimation = &m_toggleAnimation[48]; break;
            case 20: pageMaster = &m_features.bedBreakerEnabled;
                     pageMasterAnimation = &m_toggleAnimation[49]; break;
            case 21: pageMaster = &m_features.localVelocityEnabled;
                     pageMasterAnimation = &m_toggleAnimation[50]; break;
            case 22: pageMaster = &m_features.freeLookEnabled;
                     pageMasterAnimation = &m_toggleAnimation[54]; break;
            case 23: pageMaster=&m_features.smartHotbarEnabled;
                     pageMasterAnimation=&m_toggleAnimation[55]; break;
            case 24: pageMaster=&m_features.sprintEnabled;
                     pageMasterAnimation=&m_toggleAnimation[58]; break;
            case 25: pageMaster=&m_features.attackShieldEnabled;
                     pageMasterAnimation=&m_toggleAnimation[59]; break;
            default: break;
            }
            if (pageMaster != nullptr && pageMasterAnimation != nullptr) {
                ImGui::SetCursorScreenPos(ImVec2(
                    windowPosition.x + (baseGuiWidth - (compactWidth?91.F:170.F)) * uiScale,
                    windowPosition.y + 133.0F * uiScale));
                const bool masterChanged = animatedToggle(
                    compactWidth?"##Enabled":"Enabled", *pageMaster, *pageMasterAnimation, uiScale);
                if (page == 11 && masterChanged) {
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
                } else if (page == 19) {
                    if (masterChanged) m_mediaSettingsDirty = true;
                } else {
                    changed |= masterChanged;
                }
            } else {
                if(!compactWidth)
                windowDraw->AddText(boldFont, ImGui::GetFontSize() * 0.84F,
                    ImVec2(windowPosition.x + (baseGuiWidth - 79.0F) * uiScale,
                           windowPosition.y + 148.0F * uiScale),
                    fadedGuiColor(guiAccent), "SYSTEM");
            }

            auto beginHotkeyCapture = [&](const int target) noexcept {
                m_waitingForHotkey = true;
                m_hotkeyCaptureTarget = target;
                m_hotkeyCaptureCooldownFrames = 2;
                m_hotkeyCaptureArmed = false;
                if(host.captureEnabled) host.captureEnabled(false);
                if(host.consumeCapturedHotkey) host.consumeCapturedHotkey();
            };
            if (m_waitingForHotkey) {
                unsigned captured = (host.consumeCapturedHotkey ? host.consumeCapturedHotkey() : 0U);
                if (captured != 0U) {
                    {
                        if(captured==0x1B) captured=0U;
                        if (m_hotkeyCaptureTarget == 1) {
                            m_menuHotkey = captured;
                            if(host.menuHotkeyChanged) host.menuHotkeyChanged(captured);
                            m_menuHotkeyDirty = true;
                        } else if (m_hotkeyCaptureTarget == 2) {
                            m_features.bedDefenseHotkey = static_cast<int>(captured);
                            changed = true;
                        } else if (m_hotkeyCaptureTarget == 3) {
                            m_features.hypixelPanelHotkey = static_cast<int>(captured);
                            changed = true;
                        } else if (m_hotkeyCaptureTarget == 4) {
                            m_features.safewalkHotkey = static_cast<int>(captured);
                            changed = true;
                        } else if (m_hotkeyCaptureTarget == 5) {
                            m_mediaSettings.previousHotkey = static_cast<int>(captured);
                            m_mediaSettingsDirty = true;
                        } else if (m_hotkeyCaptureTarget == 6) {
                            m_mediaSettings.toggleHotkey = static_cast<int>(captured);
                            m_mediaSettingsDirty = true;
                        } else if (m_hotkeyCaptureTarget == 7) {
                            m_mediaSettings.nextHotkey = static_cast<int>(captured);
                            m_mediaSettingsDirty = true;
                        } else if (m_hotkeyCaptureTarget >= 100 &&
                                   m_hotkeyCaptureTarget < 118) {
                            const std::size_t featureIndex = static_cast<std::size_t>(
                                m_hotkeyCaptureTarget - 100);
                            for (int& configured : m_features.featureHotkeys)
                                if (configured == static_cast<int>(captured)) configured = 0;
                            m_features.featureHotkeys[featureIndex] =
                                static_cast<int>(captured);
                            if (featureIndex == 4U)
                                m_features.safewalkHotkey = static_cast<int>(captured);
                            changed = true;
                        }
                    }
                    m_waitingForHotkey = false;
                    m_hotkeyCaptureTarget = 0;
                    m_hotkeyCaptureArmed = false;
                    if(host.captureEnabled) host.captureEnabled(false);
                } else {
                    const bool mouseHeld = host.mouseHeld ? host.mouseHeld() : ImGui::IsAnyMouseDown();
                    if (mouseHeld) {
                        m_hotkeyCaptureArmed = false;
                        m_hotkeyCaptureCooldownFrames = 2;
                    } else if (m_hotkeyCaptureCooldownFrames > 0) {
                        --m_hotkeyCaptureCooldownFrames;
                    } else if (!m_hotkeyCaptureArmed) {
                        m_hotkeyCaptureArmed = true;
                        if(host.captureEnabled) host.captureEnabled(true);
                    }
                }
            }

            design.pageElapsed=std::min(.38F,design.pageElapsed+delta);
            m_clickGuiPageProgress=design.reducedMotion?1.F:cubicBezierProgress(design.pageElapsed/.38F,.16F,1.F,.3F,1.F);
            const float pageEase=std::clamp(m_clickGuiPageProgress,0.F,1.F);
            const ImVec2 childPos(contentX,
                windowPosition.y + (bodyTop+12.0F*(1.0F-pageEase)) * uiScale);
            ImGui::SetCursorScreenPos(childPos);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pageEase);
            ImGui::PushID(page); // Each page retains its own scroll position.
            beginSmoothChild("##settingsPage",
                ImVec2((baseGuiWidth - baseRailWidth - 38.0F) * uiScale,
                       std::max(20.F,baseGuiHeight-bodyTop-25.F) * uiScale),
                m_settingsScroll[static_cast<std::size_t>(page)], delta,
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
            configureGuiDrawList(ImGui::GetWindowDrawList(),spotlightScale);
            ImGui::PushTextWrapPos(0.F);
            const auto sectionTitle = [&](const char* text) noexcept {
                ImGui::Spacing();
                ImGui::PushFont(boldFont,bodySize);
                ImGui::TextColored(guiAccent, "%s", text);
                ImGui::PopFont();
                ImGui::Spacing();
            };
            const auto hotkeyControl = [&](const char* label, const int target,
                                           const unsigned key) noexcept {
                ImGui::TextDisabled("%s", label);
                ImGui::SameLine(0.0F, 14.0F * uiScale);
                const char* buttonText = m_waitingForHotkey &&
                    m_hotkeyCaptureTarget == target ? "Press a key..." : (host.keyName ? host.keyName(key) : portableHotkeyName(key));
                if (widgets::Button(buttonText, ImVec2(126.0F * uiScale, 0.0F)))
                    beginHotkeyCapture(target);
            };

            const int featureHotkeyIndex = page <= 12 ? page
                : (page == 14 ? 13 : (page == 15 ? 14 :
                   (page == 20 ? 15 : (page == 21 ? 16 :
                   (page == 22 ? 17 : -1)))));
            if (featureHotkeyIndex >= 0) {
                hotkeyControl(page == 22 ? "Hold hotkey" : "Feature hotkey",
                    100 + featureHotkeyIndex,
                    static_cast<unsigned>(std::max(0,
                        m_features.featureHotkeys[static_cast<std::size_t>(
                            featureHotkeyIndex)])));
                ImGui::Spacing();
            }

            if (page == 0) {
                sectionTitle("TARGETS");
                changed |= animatedToggle("Players only",
                    m_features.entityEspPlayersOnly, m_toggleAnimation[8], uiScale);
                changed |= animatedToggle("Show teammate boxes",
                    m_features.showTeammateBoxes, m_toggleAnimation[12], uiScale);
                changed |= animatedToggle("Show teammate arrows",
                    m_features.showTeammateArrows, m_toggleAnimation[25], uiScale);
                changed |= animatedToggle("World labels",
                    m_features.labelsEnabled, m_toggleAnimation[3], uiScale);
                sectionTitle("APPEARANCE");
                std::array<float, 3U> playerColor = unpackRgb(m_features.playerEspColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("Player box color", playerColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.playerEspColor = packRgb(playerColor);
                    changed = true;
                }
            } else if (page == 1) {
                sectionTitle("BED GEOMETRY");
                changed |= animatedToggle("Automatic bed refresh",
                    m_features.bedAutoRefreshEnabled, m_toggleAnimation[7], uiScale);
                changed |= animatedToggle("Semi-transparent box fill",
                    m_features.bedEspFilled, m_toggleAnimation[9], uiScale);
                std::array<float, 3U> bedColor = unpackRgb(m_features.bedEspColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("Bed box color", bedColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.bedEspColor = packRgb(bedColor);
                    changed = true;
                }
                if (widgets::Button("Refresh loaded beds now",
                                  ImVec2(210.0F * uiScale, 0.0F)))
                    m_bedRescanPending = true;
                sectionTitle("DEFENSE MATERIAL CARD");
                changed |= animatedToggle("Show defense material card",
                    m_features.bedDefensePanelEnabled, m_toggleAnimation[6], uiScale);
                changed |= animatedToggle("Include own bed",
                    m_features.showOwnBedDefenseInfo, m_toggleAnimation[10], uiScale);
                changed |= animatedToggle("Hold key to show",
                    m_features.bedDefenseHoldToShow, m_toggleAnimation[13], uiScale);
                hotkeyControl("Hold bind", 2,
                    static_cast<unsigned>(m_features.bedDefenseHotkey));
                changed |= animatedToggle("Distance-scaled card",
                    m_features.bedDefensePerspectiveScale, m_toggleAnimation[14], uiScale);
                ImGui::TextDisabled("Block radius (bed level and above)");
                for (int radius = 3; radius <= 10; ++radius) {
                    if (radius != 3) ImGui::SameLine();
                    ImGui::PushID(radius);
                    const bool selected = radius == m_features.bedDefenseRadius;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiSelected);
                    char label[4]{};
                    std::snprintf(label, sizeof(label), "%d", radius);
                    if (widgets::Button(label, ImVec2(38.0F * uiScale, 0.0F)) && !selected) {
                        m_features.bedDefenseRadius = radius;
                        changed = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                std::array<float, 3U> cardColor = unpackRgb(
                    m_features.bedDefensePanelColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("Card background", cardColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.bedDefensePanelColor = packRgb(cardColor);
                    changed = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= widgets::SliderInt("Card opacity",
                    &m_features.bedDefensePanelOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
            } else if (page == 2) {
                sectionTitle("PLAYER FILTER");
                changed |= animatedToggle("Show teammate nametags",
                    m_features.showTeammateNametags, m_toggleAnimation[19], uiScale);
                changed |= animatedToggle("Show on all servers and in lobbies",
                    m_features.nametagAlways,m_toggleAnimation[57],uiScale);
                ImGui::TextWrapped("Uses the server's player name and UUID, including offline-mode identities. No online-account lookup is required.");
                changed |= animatedToggle("Only nearby enemies",
                    m_features.nametagNearbyEnemiesOnly, m_toggleAnimation[20], uiScale);
                if (m_features.nametagNearbyEnemiesOnly) {
                    ImGui::SetNextItemWidth(320.0F * uiScale);
                    changed |= widgets::SliderInt("Enemy range", &m_features.nametagRange,
                        4, 128, "%d blocks", ImGuiSliderFlags_AlwaysClamp);
                }
                ImGui::TextDisabled("Only colour-validated TAB players are shown; shop NPCs are excluded.");
                sectionTitle("LAYOUT & SIZE");
                changed |= animatedToggle("Smart side placement",
                    m_features.nametagSidePlacement, m_toggleAnimation[17], uiScale);
                changed |= animatedToggle("Enemy team pulse",
                    m_features.nametagTeamPulse, m_toggleAnimation[21], uiScale);
                constexpr std::array<const char*, 4U> nametagSizes{{"S", "M", "L", "XL"}};
                ImGui::TextDisabled("Card size");
                for (int sizeIndex = 0; sizeIndex < 4; ++sizeIndex) {
                    if (sizeIndex != 0) ImGui::SameLine();
                    const bool selected = sizeIndex == m_features.nametagSizeIndex;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiSelected);
                    if (widgets::Button(nametagSizes[static_cast<std::size_t>(sizeIndex)],
                        ImVec2(62.0F * uiScale, 0.0F)) && !selected) {
                        m_features.nametagSizeIndex = sizeIndex;
                        changed = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                }
                sectionTitle("SURFACE");
                std::array<float, 3U> nametagColor = unpackRgb(
                    m_features.nametagPanelColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("Nametag background", nametagColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.nametagPanelColor = packRgb(nametagColor);
                    changed = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= widgets::SliderInt("Nametag opacity",
                    &m_features.nametagPanelOpacity, 10, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                sectionTitle("OBSERVED EQUIPMENT");
                changed |= animatedToggle("Show held special item",
                    m_features.enemyItemIndicatorsEnabled, m_toggleAnimation[18], uiScale);
                ImGui::TextWrapped("Minecraft servers do not send another player's private inventory. Fireballs, TNT, diamonds, emeralds and invisibility potions can only be reported while visibly held.");
            } else if (page == 3) {
                sectionTitle("THREAT RULE");
                ImGui::TextWrapped("Track confirmed enemy roster members around your locked own bed.");
                ImGui::Spacing();
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Warning range",
                    &m_features.bedThreatRadius, 3, 32, "%d blocks",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::Spacing();
                ImGui::TextDisabled("The warning remains visible while a tracked enemy stays inside the range.");
            } else if (page == 4) {
                sectionTitle("EDGE ASSIST");
                ImGui::TextWrapped(
                    "Uses Minecraft's native inset-AABB ledge rule and the player's real next-tick motion, then holds the normal sneak key state.");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Edge timing",
                    &m_features.safewalkEdgeSensitivity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Minimum look pitch",
                    &m_features.safewalkMinimumPitch, -90, 90, "%d deg",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Stand delay after placement",
                    &m_features.safewalkReleaseDelayMs, 0, 750, "%d ms",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextDisabled(
                    "0%% = earlier guard; 100%% = last safe body margin. Neither waits until the live hitbox is unsupported.");
            } else if (page == 5) {
                sectionTitle("AUTOMATIC PLACEMENT");
                ImGui::TextWrapped(
                    "Places a real hotbar block below/predictively ahead of your movement. Wool, planks, sandstone and stable building blocks are accepted; sand and gravel are always excluded.");
                ImGui::Spacing();
                changed |= animatedToggle("Keep placement on takeoff layer",
                    m_features.scaffoldSameLayerOnly,
                    m_toggleAnimation[43], uiScale);
                ImGui::TextDisabled(
                    "When enabled, jumping bridges forward on the takeoff layer instead of towering upward.");
                ImGui::Spacing();
                ImGui::TextColored(guiDanger,
                    "WARNING: Do not use this on a server. It can cause a ban.");
                ImGui::TextDisabled("Placement still requires a reachable solid neighbour.");
            } else if (page == 6) {
                sectionTitle("FLIGHT SPEED");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Speed", &m_features.flySpeedPercent,
                    10, 500, "%d%%", ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextColored(guiDanger,
                    "WARNING: Do not use this on a server. It can cause a ban.");
                ImGui::TextDisabled("This is local motion control and contains no server-correction bypass.");
            } else if (page == 7) {
                sectionTitle("AIR CONTROL");
                changed |= animatedToggle("Auto-jump on landing",
                    m_features.bhopAutoJump, m_toggleAnimation[32], uiScale);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Air speed",
                    &m_features.bhopAirSpeedPercent, 10, 300, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextColored(guiDanger,
                    "WARNING: Do not use this on a server. It can cause a ban.");
                ImGui::TextDisabled("Airborne horizontal velocity follows current movement input.");
            } else if (page == 8) {
                bool lockOn=m_features.aimLockOnMode;
                const auto collapsible=[&](const std::size_t index,
                    const char* id,const char* title,const char* summary,
                    const float fallbackBodyHeight,auto&& body) noexcept {
                    const float bodyGap=8.0F*uiScale;
                    const float bodyPaddingX=24.0F*uiScale;
                    const float bodyPaddingY=17.0F*uiScale;
                    const float sectionGap=14.0F*uiScale;
                    const float storedBodyHeight=m_aimSectionBodyHeight[index]>1.0F
                        ? m_aimSectionBodyHeight[index]*uiScale
                        : (fallbackBodyHeight+20.0F)*uiScale;
                    const float fullBodyHeight=std::max(36.0F*uiScale,storedBodyHeight);
                    const ImVec2 headerMin=ImGui::GetCursorScreenPos();
                    const float headerWidth=ImGui::GetContentRegionAvail().x;
                    const ImVec2 headerSize(headerWidth,60.0F*uiScale);
                    const float parentItemSpacing=ImGui::GetStyle().ItemSpacing.y;
                    ImGui::PushID(id);
                    ImGui::InvisibleButton("##header",headerSize);
                    const bool hovered=ImGui::IsItemHovered();
                    auto& headerMotion=widgets::motion(ImGui::GetID("##header"),hovered,ImGui::IsItemActive());
                    if(ImGui::IsItemClicked())
                        m_aimSectionOpen[index]=!m_aimSectionOpen[index];
                    // Apply the click before advancing. Otherwise the click
                    // frame repeats the fully-open layout and collapse appears
                    // to hitch before its first visible step.
                    const float direction=m_aimSectionOpen[index]?1.0F:-1.0F;
                    m_aimSectionProgress[index]=design.reducedMotion?(m_aimSectionOpen[index]?1.F:0.F):CollapsibleMotion::advance(
                        m_aimSectionProgress[index],m_aimSectionOpen[index],delta);
                    m_aimSectionVelocity[index]=direction/
                        CollapsibleMotion::DurationSeconds;
                    const float phase=std::clamp(
                        m_aimSectionProgress[index],0.0F,1.0F);
                    const float progress=CollapsibleMotion::eased(phase);
                    // InvisibleButton is a normal ImGui item and therefore adds
                    // ItemSpacing.y. The accordion owns its vertical geometry,
                    // so remove that hidden fixed gap before applying animation.
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY()-parentItemSpacing);

                    ImDrawList* const draw=ImGui::GetWindowDrawList();
                    const float bodyVisible=(bodyGap+fullBodyHeight)*progress;
                    const ImVec2 outerMax(headerMin.x+headerSize.x,
                        headerMin.y+headerSize.y+bodyVisible);
                    constexpr float baseCardRounding=11.0F;
                    const float cardRounding=baseCardRounding*uiScale;
                    const ImVec4 outerSurface=mixColor(guiSurface,guiFrame,0.46F);
                    draw->AddRectFilled(headerMin,outerMax,
                        ImGui::GetColorU32(outerSurface),cardRounding);
                    draw->AddRect(headerMin,outerMax,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(
                            guiAccent.x,guiAccent.y,guiAccent.z,
                            0.20F+0.16F*progress)),cardRounding,0,
                        std::max(1.0F,uiScale));
                    const ImVec4 surface=mixColor(guiFrame,guiAccent,
                        0.08F+0.09F*progress+0.09F*headerMotion.hover+0.03F*headerMotion.press);
                    draw->AddRectFilled(headerMin,
                        ImVec2(headerMin.x+headerSize.x,headerMin.y+headerSize.y),
                        ImGui::GetColorU32(surface),cardRounding,
                        ImDrawFlags_RoundCornersAll);
                    draw->AddLine(ImVec2(headerMin.x+12.0F*uiScale,
                        headerMin.y+1.0F*uiScale),
                        ImVec2(headerMin.x+headerSize.x-12.0F*uiScale,
                        headerMin.y+1.0F*uiScale),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(
                            guiAccent.x,guiAccent.y,guiAccent.z,0.32F)),
                        std::max(1.0F,uiScale));
                    draw->AddText(boldFont,ImGui::GetFontSize(),
                        ImVec2(headerMin.x+24.0F*uiScale,
                               headerMin.y+12.0F*uiScale),
                        ImGui::GetColorU32(guiText),title);
                    draw->AddText(ImVec2(headerMin.x+24.0F*uiScale,
                        headerMin.y+35.0F*uiScale),
                        ImGui::GetColorU32(guiMuted),summary);
                    const ImVec2 centre(headerMin.x+headerSize.x-22.0F*uiScale,
                        headerMin.y+headerSize.y*0.5F);
                    const float p=progress;
                    const ImVec2 a(centre.x+(-4.0F*(1.0F-p)-5.0F*p)*uiScale,
                        centre.y+(-5.0F*(1.0F-p)-2.0F*p)*uiScale);
                    const ImVec2 b(centre.x,centre.y+(0.0F*(1.0F-p)+4.0F*p)*uiScale);
                    const ImVec2 c(centre.x+(4.0F*(1.0F-p)+5.0F*p)*uiScale,
                        centre.y+(5.0F*(1.0F-p)-2.0F*p)*uiScale);
                    draw->AddLine(a,b,ImGui::GetColorU32(guiMuted),
                        1.7F*uiScale);
                    draw->AddLine(b,c,ImGui::GetColorU32(guiMuted),
                        1.7F*uiScale);

                    {
                        ImGuiWindow* const parentWindow=ImGui::GetCurrentWindow();
                        const float parentCursorMaxBefore=parentWindow->DC.CursorMaxPos.y;
                        const float parentIdealMaxBefore=parentWindow->DC.IdealMaxPos.y;
                        const ImVec2 bodyStart(headerMin.x,
                            headerMin.y+headerSize.y+bodyGap);
                        ImGui::SetCursorScreenPos(bodyStart);
                        // Parent ClickGUI alpha is applied once to the completed
                        // draw lists. This local alpha only represents accordion
                        // openness and therefore composes without overriding the
                        // parent's close animation.
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,pageEase*progress*progress);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(bodyPaddingX,bodyPaddingY));
                        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));
                        if(progress<0.985F) ImGui::BeginDisabled();
                        ImGui::BeginChild("##body",ImVec2(0.0F,fullBodyHeight),
                            ImGuiChildFlags_AlwaysUseWindowPadding,
                            ImGuiWindowFlags_NoScrollbar|
                            ImGuiWindowFlags_NoScrollWithMouse|
                            (progress<0.985F?ImGuiWindowFlags_NoInputs:0));
                        ImDrawList* const bodyDraw=ImGui::GetWindowDrawList();
                        configureGuiDrawList(bodyDraw,spotlightScale);
                        bodyDraw->PushClipRect(
                            ImVec2(headerMin.x,headerMin.y+headerSize.y),
                            ImVec2(headerMin.x+headerSize.x,
                                headerMin.y+headerSize.y+bodyVisible),true);
                        // Description strings are allowed to wrap within the
                        // padded body instead of overrunning the right border.
                        ImGui::PushTextWrapPos(0.0F);
                        body();
                        ImGui::PopTextWrapPos();
                        bodyDraw->PopClipRect();
                        const float trailingSpacing=ImGui::GetStyle().ItemSpacing.y;
                        const float measuredPixels=std::max(36.0F*uiScale,
                            ImGui::GetCursorPosY()-trailingSpacing+bodyPaddingY);
                        // A clipped child can report transient, pixel-rounded
                        // cursor metrics while it is collapsing.  Preserve the
                        // last fully-expanded natural height so ScrollMax and
                        // every following card remain stable during animation.
                        // Offscreen children skip widget layout. Their cursor
                        // then reports padding only, not natural content size.
                        // Never replace a measured height with that placeholder.
                        if(!ImGui::GetCurrentWindow()->SkipItems&&
                           (phase>=0.999F||m_aimSectionBodyHeight[index]<=1.0F)) {
                            const float measured=measuredPixels/std::max(0.01F,uiScale);
                            if(std::abs(measured-m_aimSectionBodyHeight[index])>1.0F)
                                m_aimSectionBodyHeight[index]=measured;
                        }
                        ImGui::EndChild();
                        if(spotlightEase>0.985F)
                            if(host.transientBlur) host.transientBlur(bodyDraw,1.0F-progress,uiScale);
                        if(progress<0.985F) ImGui::EndDisabled();
                        ImGui::PopStyleColor();
                        ImGui::PopStyleVar(2);
                        // The full-height body keeps its contents and text
                        // metrics stable while clipping reveals them. Rebase
                        // the parent layout to the exact shared animated edge
                        // so ScrollMax and all following cards move continuously.
                        parentWindow->DC.CursorMaxPos.y=std::max(
                            parentCursorMaxBefore,outerMax.y);
                        parentWindow->DC.IdealMaxPos.y=std::max(parentIdealMaxBefore,outerMax.y);
                        ImGui::SetCursorScreenPos(ImVec2(headerMin.x,outerMax.y));
                    }
                    ImGui::PopID();
                    // Dummy adds ItemSpacing itself; compensate so the gap
                    // between cards is an exact, intentional 12 base pixels.
                    ImGui::Dummy(ImVec2(1.0F,
                        std::max(0.0F,sectionGap-parentItemSpacing)));
                };

                const char* modeSummary=m_features.aimSilentLock?"Lock On · Silent":
                    lockOn?"Lock On · Camera":"Smooth Aim · Camera";
                collapsible(0U,"aimMode","MODE & OUTPUT",modeSummary,82.0F,[&] {
                    const float modeGap=10.0F*uiScale;
                    const float modeButtonWidth=std::max(96.0F*uiScale,
                        (ImGui::GetContentRegionAvail().x-modeGap)*0.5F);
                    const bool smoothSelected=!lockOn;
                    if(smoothSelected) ImGui::PushStyleColor(ImGuiCol_Button,guiSelected);
                    if(widgets::Button("Smooth Aim",ImVec2(modeButtonWidth,0))&&lockOn) {
                        m_features.aimLockOnMode=false;
                        m_features.aimSilentLock=false;
                        lockOn=false; changed=true;
                    }
                    if(smoothSelected) ImGui::PopStyleColor();
                    ImGui::SameLine(0.0F,modeGap);
                    // Pair the style stack with the state captured before this
                    // button can mutate lockOn.  Reading the new state in the
                    // Pop branch caused PopStyleColor() underflow when switching
                    // from Smooth Aim to Lock On.
                    const bool lockSelected=lockOn;
                    if(lockSelected) ImGui::PushStyleColor(ImGuiCol_Button,guiSelected);
                    if(widgets::Button("Lock On",ImVec2(modeButtonWidth,0))&&!lockOn) {
                        m_features.aimLockOnMode=true;lockOn=true;changed=true;
                    }
                    if(lockSelected) ImGui::PopStyleColor();
                    ImGui::BeginDisabled(!lockOn);
                    changed|=animatedToggle("Silent Lock · keep camera free",
                        m_features.aimSilentLock,m_toggleAnimation[46],uiScale);
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Hold left click for fixed-CPS attacks. Releasing returns control immediately.");
                });
                if(m_features.aimSilentLock&&!m_features.aimLockOnMode) {
                    m_features.aimLockOnMode=true;lockOn=true;changed=true;
                }

                const char* controlSummary=m_features.silentControlAdaptation?
                    "Movement adaptation on":"Vanilla local controls";
                collapsible(1U,"aimSilent","SILENT CONTROL",controlSummary,
                    216.0F,[&] {
                    ImGui::BeginDisabled(!m_features.aimSilentLock);
                    changed|=animatedToggle("Silent Control Adaptation",
                        m_features.silentControlAdaptation,m_toggleAnimation[55],uiScale);
                    ImGui::TextDisabled("Keeps world movement, jump and sprint aligned with the committed logical yaw.");
                    changed|=animatedToggle("Locked target scanner",
                        m_features.aimScannerEnabled,m_toggleAnimation[47],uiScale);
                    changed|=animatedToggle("Check attack availability",
                        m_features.aimAttackViability,m_toggleAnimation[51],uiScale);
                    ImGui::TextDisabled("This validates attack commitment only; it never controls movement adaptation.");
                    changed|=animatedToggle("Sequential multi-target",
                        m_features.aimSequentialTargets,m_toggleAnimation[53],uiScale);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=widgets::SliderInt("Hold attack rate",
                        &m_features.aimAttackCps,1,20,"%d CPS",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::EndDisabled();
                    if(m_features.aimSilentLock&&!snapshot.silentAimAvailable)
                        ImGui::TextColored(guiWarning,
                            "Silent rotation bindings are unavailable in this client.");
                });

                char targetSummary[80]{};
                std::snprintf(targetSummary,sizeof(targetSummary),"%d–%d blocks · %d° FOV",
                    m_features.aimMinimumDistance,m_features.aimMaximumDistance,
                    m_features.aimFovDegrees);
                collapsible(2U,"aimTarget","TARGET SELECTION",targetSummary,
                    150.0F,[&] {
                    changed|=animatedToggle("Prioritize nearest target",
                        m_features.aimNearestPriority,m_toggleAnimation[37],uiScale);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=widgets::SliderInt("Minimum distance",
                        &m_features.aimMinimumDistance,0,
                        std::max(0,m_features.aimMaximumDistance-1),"%d blocks",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=widgets::SliderInt("Maximum distance",
                        &m_features.aimMaximumDistance,
                        std::max(1,m_features.aimMinimumDistance),128,"%d blocks",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=widgets::SliderInt("Field of view",
                        &m_features.aimFovDegrees,1,360,"%d deg",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::TextDisabled("Only validated enemy-player candidates are eligible.");
                });

                collapsible(3U,"aimResponse","RESPONSE & DIAGNOSTICS",
                    lockOn?"Exact tracking":"Smoothed response",115.0F,[&] {
                    ImGui::BeginDisabled(lockOn);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=widgets::SliderInt("Smooth speed",
                        &m_features.aimSpeedPercent,1,100,"%d%%",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::EndDisabled();
                    changed|=widgets::Checkbox("Silent file diagnostics",
                        &m_features.silentFileDebug);
                    changed|=widgets::Checkbox("Silent chat diagnostics",
                        &m_features.silentChatDebug);
                    ImGui::TextDisabled("Diagnostics observe final ownership and dispatch; they do not alter input.");
                });
            } else if (page == 9) {
                sectionTitle("VISIBILITY");
                changed |= animatedToggle("Hold key to show roster",
                    m_features.hypixelPanelHoldToShow, m_toggleAnimation[15], uiScale);
                hotkeyControl("Panel bind", 3,
                    static_cast<unsigned>(m_features.hypixelPanelHotkey));
                sectionTitle("CARD SURFACE");
                ImGui::TextDisabled("Background");
                const bool black = m_features.hypixelPanelColor != 0xFFFFFFU;
                if (black) ImGui::PushStyleColor(ImGuiCol_Button, guiSelected);
                if (widgets::Button("Black", ImVec2(104.0F * uiScale, 0.0F)) && !black) {
                    m_features.hypixelPanelColor = 0x000000U;
                    changed = true;
                }
                if (black) ImGui::PopStyleColor();
                ImGui::SameLine();
                if (!black) ImGui::PushStyleColor(ImGuiCol_Button, guiSelected);
                if (widgets::Button("White", ImVec2(104.0F * uiScale, 0.0F)) && black) {
                    m_features.hypixelPanelColor = 0xFFFFFFU;
                    changed = true;
                }
                if (!black) ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= widgets::SliderInt("Panel opacity",
                    &m_features.hypixelPanelOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                std::array<float, 3U> statsRailColor = unpackRgb(
                    m_features.hypixelRailColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("STATS rail", statsRailColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.hypixelRailColor = packRgb(statsRailColor);
                    changed = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= widgets::SliderInt("Rail opacity",
                    &m_features.hypixelRailOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextDisabled("Font size");
                constexpr std::array<const char*, 4U> statsFonts{{"S", "M", "L", "XL"}};
                for (int fontIndex = 0; fontIndex < 4; ++fontIndex) {
                    if (fontIndex != 0) ImGui::SameLine();
                    const bool selected = fontIndex == m_features.hypixelPanelFontIndex;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiSelected);
                    if (widgets::Button(statsFonts[static_cast<std::size_t>(fontIndex)],
                        ImVec2(62.0F * uiScale, 0.0F)) && !selected) {
                        m_features.hypixelPanelFontIndex = fontIndex;
                        changed = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                }
                ImGui::Text("Panel size: %d%% W  /  %d%% H",
                            m_features.hypixelPanelScale,
                            m_features.hypixelPanelHeight);
                ImGui::SameLine(0.0F, 16.0F * uiScale);
                if (widgets::Button("Reset position & size")) {
                    m_features.hypixelPanelScale = 100;
                    m_features.hypixelPanelHeight = 100;
                    m_features.hypixelPanelX = -1;
                    m_features.hypixelPanelY = -1;
                    changed = true;
                }
                ImGui::TextDisabled("Open the GUI, drag the column header to move, or drag the lower-right handle to resize.");
                sectionTitle("MANUAL LOOKUP");
                ImGui::SetNextItemWidth(280.0F * uiScale);
                ImGui::InputTextWithHint("##hypixelPlayer", "Minecraft player name",
                    m_hypixelInput.data(), m_hypixelInput.size());
                ImGui::SameLine();
                if (widgets::Button("Query", ImVec2(90.0F * uiScale, 0.0F))) {
                    const std::string_view playerId(m_hypixelInput.data());
                    const bool valid = !playerId.empty() && playerId.size() <= 16U &&
                        std::all_of(playerId.begin(), playerId.end(), [](const char c) noexcept {
                            return (c >= 'A' && c <= 'Z') ||
                                   (c >= 'a' && c <= 'z') ||
                                   (c >= '0' && c <= '9') || c == '_';
                        });
                    if (valid) {
                        m_hypixelQuery.fill('\0');
                        std::copy(playerId.begin(), playerId.end(), m_hypixelQuery.begin());
                        m_hypixelQueryPending = true;
                    }
                }
            } else if (page == 10) {
                sectionTitle("LOCAL CHAT");
                ImGui::TextWrapped("Match probes, roster teams, teammate decisions, own-bed ownership and automatic statistics requests are written only to your local chat.");
                ImGui::Spacing();
                ImGui::TextDisabled("Disable the page switch above for a clean normal session.");
            } else if (page == 11) {
                const auto publishBlacklistSettings = [&]() noexcept {
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
                };
                sectionTitle("ENCOUNTER POLICY");
                bool blacklistSettingsChanged = false;
                blacklistSettingsChanged |= animatedToggle(
                    "Warn once when a match starts", m_blacklist.matchAlertsEnabled,
                    m_toggleAnimation[23], uiScale);
                blacklistSettingsChanged |= animatedToggle(
                    "Allow ID-only records for nicks", m_blacklist.allowIdOnlyNicks,
                    m_toggleAnimation[24], uiScale);
                sectionTitle("PANEL SURFACE");
                blacklistSettingsChanged |= animatedToggle(
                    "Show panel in game", m_blacklist.panelEnabled,
                    m_toggleAnimation[22], uiScale);
                ImGui::TextDisabled(
                    "The page master controls whether the panel stays visible with Click GUI.");
                std::array<float, 3U> blacklistColor = unpackRgb(m_blacklist.panelColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("Panel background", blacklistColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_blacklist.panelColor = packRgb(blacklistColor);
                    blacklistSettingsChanged = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                blacklistSettingsChanged |= widgets::SliderInt(
                    "Panel opacity", &m_blacklist.panelOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(300.0F * uiScale);
                blacklistSettingsChanged |= widgets::SliderInt(
                    "Panel content size", &m_blacklist.contentScale, 80, 200,
                    "%d%%", ImGuiSliderFlags_AlwaysClamp);
                if (blacklistSettingsChanged) publishBlacklistSettings();

                sectionTitle("ADD RECENT PLAYER");
                if (widgets::Button("+ Add player",
                                  ImVec2(138.0F * uiScale, 0.0F))) {
                    m_blacklistAddOpen = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Opens a separate animated dialog.");

                sectionTitle("SAVED PLAYERS");
                ImGui::BeginChild("##blacklistEntries", ImVec2(0.0F, 190.0F * uiScale),
                                  false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                configureGuiDrawList(ImGui::GetWindowDrawList(),spotlightScale);
                for (std::uint32_t index = 0U; index < m_blacklist.count; ++index) {
                    const BlacklistEntry& entry = m_blacklist.entries[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::PushFont(boldFont,bodySize);
                    ImGui::TextUnformatted(entry.name.data());
                    ImGui::PopFont();
                    if (entry.nick) {
                        ImGui::SameLine();
                        ImGui::TextColored(guiAccent, "NICK");
                    }
                    ImGui::TextWrapped("%s", entry.reason.data());
                    bool warning = entry.warnOnEncounter;
                    if (widgets::Checkbox("Encounter warning", &warning)) {
                        m_blacklistAction = {};
                        m_blacklistAction.type = BlacklistAction::Type::Warning;
                        std::snprintf(m_blacklistAction.key.data(),
                            m_blacklistAction.key.size(), "%s", entry.key.data());
                        m_blacklistAction.warnOnEncounter = warning;
                        m_blacklistActionDirty = true;
                    }
                    ImGui::SameLine();
                    if (widgets::SmallButton("Delete")) {
                        m_blacklistAction = {};
                        m_blacklistAction.type = BlacklistAction::Type::Remove;
                        std::snprintf(m_blacklistAction.key.data(),
                            m_blacklistAction.key.size(), "%s", entry.key.data());
                        m_blacklistActionDirty = true;
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();
            } else if (page == 25) {
                sectionTitle("ATTACKER SOURCE");
                const int selected=([&]() noexcept {
                    if(m_features.attackShieldWildcard) return -2;
                    if(m_shieldWorld != snapshot.worldGeneration) return -1;
                    for(const auto& e:snapshot.entityMarkers)
                        if(e.player && e.entityId==m_shieldSelectedId && e.uuid==m_shieldSelectedUuid) return e.entityId;
                    return -1;
                })();
                const char* preview=selected==-2?"*  All sources (no verification)":"Choose a player in this world";
                for(std::uint32_t i=0;i<snapshot.entityMarkerCount;++i)
                    if(snapshot.entityMarkers[i].entityId==selected)
                        preview=snapshot.entityMarkers[i].displayName[0]
                            ? snapshot.entityMarkers[i].displayName.data():snapshot.entityMarkers[i].playerName.data();
                ImGui::SetNextItemWidth(-1.0F);
                if(widgets::BeginCombo("##shieldPlayer",preview)) {
                    if(widgets::Selectable("*  All sources (no verification)",selected==-2)) {
                        m_shieldSelectedId=-2;m_shieldWorld=snapshot.worldGeneration;
                        m_shieldSelectedUuid={};
                        m_features.attackShieldWildcard=true;
                        changed=true;
                    }
                    for(std::uint32_t i=0;i<snapshot.entityMarkerCount;++i) {
                        const auto& entity=snapshot.entityMarkers[i];
                        if(!entity.player||(!entity.playerName[0]&&!entity.displayName[0])||!entity.uuid[0]) continue;
                        ImGui::PushID(entity.entityId);
                        if(widgets::Selectable(entity.displayName[0]?entity.displayName.data():entity.playerName.data(),entity.entityId==selected)) {
                            m_shieldSelectedId=entity.entityId;
                            m_shieldSelectedUuid=entity.uuid;
                            m_shieldWorld=snapshot.worldGeneration;
                            m_features.attackShieldWildcard=false;
                            changed=true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Spacing();
                ImGui::TextWrapped("Blocks a selected player's directly attributed knockback before it changes your velocity. No velocity rollback, nearby-player guessing, or damage cancellation.");
                ImGui::PushTextWrapPos(0.0F);
                if(selected==-2) ImGui::TextColored(guiDanger,
                    "Wildcard also blocks non-attack entity-velocity updates to you.");
                else ImGui::TextColored(guiWarning,snapshot.integratedSinglePlayer
                    ? "Direct source hooks are available in this integrated world."
                    : "Remote server velocity packets do not identify the attacker: those impulses are not blocked.");
                ImGui::PopTextWrapPos();
                ImGui::TextWrapped(selected==-2
                    ? "The wildcard choice is saved with your Arcveil settings."
                    : "A specific player remains session-only and tied to this world's entity ID and UUID. Additional unattributed impulses remain untouched.");
            } else if (page == 24) {
                sectionTitle("SPRINT OWNERSHIP");
                ImGui::TextWrapped("Automatically sprint while moving forward. Silent Lock always has priority: sprint stays off throughout silent aiming, including when SCA is disabled.");
                ImGui::TextDisabled("Release attack to return sprint control to this module.");
            } else if (page == 12) {
                sectionTitle("ENABLED MODULE LIST");
                ImGui::TextWrapped(
                    "Shows enabled modules as animated text without a background. Drag the list while the Click GUI is open.");
                std::array<float, 3U> textColor = unpackRgb(m_features.textGuiColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("Flow base color", textColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.textGuiColor = packRgb(textColor);
                    changed = true;
                }
                if (widgets::Button("Reset text position",
                                  ImVec2(180.0F * uiScale, 0.0F))) {
                    m_features.textGuiX = -1;
                    m_features.textGuiY = -1;
                    changed = true;
                }
                sectionTitle("LAYOUT");
                changed |= animatedToggle("Left accent line",
                    m_features.textGuiVerticalLine, m_toggleAnimation[38], uiScale);
                changed |= animatedToggle("Show module modes",
                    m_features.textGuiShowModes,m_toggleAnimation[52],uiScale);
                ImGui::TextDisabled("Text alignment");
                constexpr std::array<const char*, 3U> alignLabels{{
                    "Left", "Center", "Right"}};
                for (int alignment = 0; alignment < 3; ++alignment) {
                    if (alignment != 0) ImGui::SameLine();
                    const bool selected = m_features.textGuiAlignment == alignment;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiSelected);
                    if (widgets::Button(alignLabels[static_cast<std::size_t>(alignment)],
                        ImVec2(88.0F * uiScale, 0.0F)) && !selected) {
                        m_features.textGuiAlignment = alignment;
                        changed = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                }
            } else if (page == 14) {
                sectionTitle("PROJECTILE BOX");
                ImGui::TextWrapped(
                    "Tracks EntityFireball instances in an integrated single-player world. The compact box is intentionally smaller than the large-fireball collision volume.");
                changed |= animatedToggle("Semi-transparent fill",
                    m_features.fireballEspFilled, m_toggleAnimation[36], uiScale);
                std::array<float, 3U> fireballColor = unpackRgb(
                    m_features.fireballEspColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (widgets::ColorEdit3("Fireball color", fireballColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.fireballEspColor = packRgb(fireballColor);
                    changed = true;
                }
                ImGui::TextColored(guiWarning,
                    "LOCAL WORLD ONLY: automatically disabled on every multiplayer server.");
            } else if (page == 15) {
                sectionTitle("JUMP IMPULSE");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Horizontal speed",
                    &m_features.longJumpSpeedPercent, 25, 250, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextWrapped(
                    "Applies a forward jump impulse at the next grounded movement step, with a bounded cooldown.");
                ImGui::TextColored(guiWarning,
                    "LOCAL WORLD ONLY: automatically disabled on every multiplayer server.");
            } else if (page == 16) {
                sectionTitle("TRAJECTORY SOURCES");
                changed |= animatedToggle("Knockback prediction",
                    m_features.knockbackPredictionEnabled,
                    m_toggleAnimation[39], uiScale);
                ImGui::TextDisabled(
                    "Matches hurt status/health loss with an airborne impulse within 250 ms.");
                ImGui::TextDisabled("Hurt mapping: %s | hurt %u / impulse %u / confirmed %u",
                    snapshot.knockbackHurtAvailable ? "ready" : "health fallback",
                    snapshot.knockbackDamageEvents, snapshot.knockbackImpulseEvents,
                    snapshot.knockbackConfirmedEvents);
                changed |= animatedToggle("Bow prediction",
                    m_features.bowPredictionEnabled,
                    m_toggleAnimation[40], uiScale);
                ImGui::TextDisabled(
                    "20 TPS physics samples are blended every rendered frame.");
            } else if (page == 17) {
                sectionTitle("HOSTILE MOB LAB");
                changed |= animatedToggle("Auto-attack hostile mobs",
                    m_features.localMobAuraEnabled,
                    m_toggleAnimation[41], uiScale);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Local reach",
                    &m_features.localMobReach, 3, 10, "%d blocks",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Attack interval",
                    &m_features.localAttackDelayMs, 100, 1500, "%d ms",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextColored(guiWarning,
                    "INTEGRATED SINGLE-PLAYER ONLY: Agent hard-disables this page elsewhere.");
                ImGui::TextWrapped(
                    "The attack helper accepts only non-player hostile candidates; it never targets players.");
            } else if (page == 18) {
                sectionTitle("WINDOWS INPUT METHOD BRIDGE");
                if (widgets::Button("Adjust panel position", ImVec2(260.0F * uiScale, 0)))
                    m_imePositionEditing = true;
                ImGui::TextWrapped(
                    "Mirrors the active Windows input method, live composition text and the current candidate page into the OpenGL frame. This keeps candidates visible in exclusive fullscreen without synthesizing input.");
                ImGui::Spacing();
                ImGui::TextDisabled(
                    "The original IME remains the text owner. The overlay only observes WM_IME messages and never commits or replaces characters.");
                ImGui::Spacing();
                ImGui::TextColored(guiSuccess,
                    "Candidate card appears at the top-center while composing and briefly after an input-method switch.");
            } else if (page == 19) {
                bool mediaChanged = false;
                sectionTitle("NOW PLAYING SURFACE");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                mediaChanged |= widgets::SliderInt("Card scale",
                    &m_mediaSettings.scalePercent, 35, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextDisabled(
                    "Scale is continuous and applies uniformly to artwork, type, spectrum and controls.");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                mediaChanged |= widgets::SliderInt("Card opacity",
                    &m_mediaSettings.opacity, 20, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                mediaChanged |= widgets::SliderInt("Spectrum opacity",
                    &m_mediaSettings.spectrumOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                std::array<float, 3U> mediaColor = unpackRgb(
                    m_mediaSettings.panelColor);
                ImGui::SetNextItemWidth(240.0F * uiScale);
                if (widgets::ColorEdit3("Card color", mediaColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_mediaSettings.panelColor = packRgb(mediaColor);
                    mediaChanged = true;
                }
                if (widgets::Button("Reset card position",
                                  ImVec2(220.0F * uiScale, 0.0F))) {
                    m_mediaSettings.panelX = -1;
                    m_mediaSettings.panelY = -1;
                    mediaChanged = true;
                }
                ImGui::TextDisabled(
                    "Transparent cards use the captured frame behind the panel for a soft Gaussian surface.");
                sectionTitle("PLAYBACK SHORTCUTS");
                hotkeyControl("Previous", 5,
                    static_cast<unsigned>(m_mediaSettings.previousHotkey));
                hotkeyControl("Play / pause", 6,
                    static_cast<unsigned>(m_mediaSettings.toggleHotkey));
                hotkeyControl("Next", 7,
                    static_cast<unsigned>(m_mediaSettings.nextHotkey));
                ImGui::TextDisabled(
                    "The card itself is draggable and its three controls are clickable while this GUI is open.");
                if (mediaChanged) m_mediaSettingsDirty = true;
            } else if (page == 20) {
                sectionTitle("LOCAL BED PATH");
                ImGui::TextWrapped(
                    "Selects the fastest hotbar tool and mines the first visible block on the shortest sampled path to a non-owned bed.");
                ImGui::TextWrapped(
                    "Line of sight and normal controller reach are mandatory. Hidden blocks and seam/through-wall hits are never synthesized.");
                ImGui::Spacing();
                ImGui::TextColored(guiWarning,
                    "INTEGRATED SINGLE-PLAYER ONLY: automatically disabled on multiplayer servers.");
            } else if (page == 21) {
                sectionTitle("RESPONSE PROFILE");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Probability",
                    &m_features.localVelocityProbability,0,100,"%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Horizontal retained",
                    &m_features.localVelocityPercent,0,100,"%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Vertical retained",
                    &m_features.localVelocityVerticalPercent,0,100,"%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextDisabled("100%% keeps vanilla knockback; 0%% removes that component.");
                ImGui::TextColored(guiWarning,
                    "INTEGRATED SINGLE-PLAYER ONLY: automatically disabled on multiplayer servers.");
            } else if (page == 22) {
                sectionTitle("CAMERA CONTROL");
                ImGui::TextWrapped(
                    "Hold the configured key and move the mouse to look around independently. Your player yaw and pitch remain unchanged.");
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "FreeLook temporarily switches to rear third-person view, keeps that perspective while held, then restores the exact previous camera perspective when released.");
                ImGui::TextDisabled(
                    "The camera is limited to vanilla pitch bounds. Release the key before rebinding it.");
            } else if(page==23) {
                sectionTitle("CATEGORY SHORTCUTS");
                ImGui::TextWrapped(
                    "Assign a category to each logical hotbar slot. Arcveil reads Minecraft's live Hotbar 1-9 bindings, so a slot rebound to Q or a mouse button follows that binding automatically.");
                ImGui::Spacing();
                changed |= animatedToggle("Refill depleted blocks",m_features.smartHotbarRefill,
                    m_toggleAnimation[56],uiScale);
                constexpr std::array<const char*,6U> actionLabels{{
                    "Normal slot","Sword","Blocks","Shears","Pickaxe","Axe"}};
                const float available=ImGui::GetContentRegionAvail().x;
                const float inset=18.0F*uiScale;
                const bool stacked=available<440.F*uiScale;
                const float rowHeight=(stacked?126.F:78.F)*uiScale;
                const float comboWidth=stacked?available-inset*2:std::min(194.0F*uiScale,available*0.43F);
                for(std::size_t slot=0;slot<m_features.smartHotbarActions.size();++slot) {
                    ImGui::PushID(static_cast<int>(slot));
                    const ImVec2 start=ImGui::GetCursorScreenPos();
                    const ImVec2 end(start.x+available,start.y+rowHeight);
                    ImDrawList* draw=ImGui::GetWindowDrawList();
                    draw->AddRectFilled(start,end,fadedGuiColor(mixColor(guiSurface,guiFrame,.52F)),12*uiScale);
                    draw->AddRect(start,end,fadedGuiColor(mixColor(guiFrame,guiMuted,.15F)),12*uiScale);
                    const ImVec2 badge(start.x+inset,start.y+23*uiScale);
                    draw->AddRectFilled(badge,ImVec2(badge.x+32*uiScale,badge.y+32*uiScale),
                        fadedGuiColor(mixColor(guiFrame,guiAccent,.13F)),8*uiScale);
                    char number[4]{};
                    std::snprintf(number,sizeof(number),"%u",static_cast<unsigned>(slot+1U));
                    const ImVec2 numberSize=ImGui::CalcTextSize(number);
                    draw->AddText(ImVec2(badge.x+(32*uiScale-numberSize.x)*.5F,
                        badge.y+(32*uiScale-numberSize.y)*.5F),fadedGuiColor(guiText),number);
                    const float labelX=badge.x+44*uiScale;
                    char label[24]{};
                    std::snprintf(label,sizeof(label),"Hotbar %u",static_cast<unsigned>(slot+1U));
                    draw->AddText(boldFont,ImGui::GetFontSize(),ImVec2(labelX,start.y+18*uiScale),
                        fadedGuiColor(guiText),label);
                    draw->AddText(ImVec2(labelX,start.y+43*uiScale),fadedGuiColor(guiMuted),"Minecraft key");
                    ImGui::SetCursorScreenPos(ImVec2(end.x-inset-comboWidth,
                        stacked?start.y+76*uiScale:start.y+(rowHeight-ImGui::GetFrameHeight())*.5F));
                    int& action=m_features.smartHotbarActions[slot];
                    action=std::clamp(action,0,5);
                    ImGui::SetNextItemWidth(comboWidth);
                    if(widgets::Combo("##category",&action,actionLabels.data(),
                                    static_cast<int>(actionLabels.size()))) changed=true;
                    ImGui::SetCursorScreenPos(start);
                    ImGui::Dummy(ImVec2(available,rowHeight+4*uiScale));
                    ImGui::PopID();
                }
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_TextDisabled,guiMuted);
                ImGui::TextWrapped(
                    "If the category is already in the hotbar it is selected directly. Otherwise the first matching main-inventory stack is swapped into the pressed logical slot.");
                ImGui::PopStyleColor();
            } else {
                sectionTitle("GUI ELEMENT SIZE");
                ImGui::SetNextItemWidth(360*uiScale);
                if(widgets::SliderInt("Element size",&design.elementScale,
                    minimumGuiElementScale,maximumGuiElementScale,"%d%%",
                    ImGuiSliderFlags_AlwaysClamp)) design.elementScaleDirty=true;
                ImGui::TextWrapped("Changes GUI text, controls and spacing. HUDs and other windows keep their size. Small windows fit the elements automatically.");
                if(widgets::Button("Restore element size")) {
                    design.elementScale=100;design.elementScaleDirty=true;
                }
                sectionTitle("TYPOGRAPHY");
                ImGui::SetNextItemWidth(360*uiScale);
                if(widgets::SliderInt("Font size",&design.typography.size,
                    GuiTypography::minimumSize,GuiTypography::maximumSize,"%d px",
                    ImGuiSliderFlags_AlwaysClamp)) design.typographyDirty=true;
                ImGui::TextDisabled("Font weight");
                constexpr std::array<const char*,3> weightLabels{"Regular","Semibold","Bold"};
                constexpr std::array<int,3> weights{400,600,700};
                const float weightButtonWidth=std::min(116.F*uiScale,(ImGui::GetContentRegionAvail().x-20*uiScale)/3);
                for(int i=0;i<3;++i) {
                    if(i) ImGui::SameLine();
                    const bool selected=design.typography.weight==weights[i];
                    if(selected) ImGui::PushStyleColor(ImGuiCol_Button,guiSelected);
                    if(widgets::Button(weightLabels[i],ImVec2(weightButtonWidth,0)) && !selected) {
                        design.typography.weight=weights[i];design.typographyDirty=true;
                    }
                    if(selected) ImGui::PopStyleColor();
                }
                ImGui::Spacing();
                ImGui::TextWrapped("Aa  Clear labels, readable details.");
                ImGui::TextDisabled("18 px / Semibold recommended for a 16-inch 2560 x 1600 display.");
                if(widgets::Button("Restore recommended typography")) {
                    design.typography={};design.typographyDirty=true;
                }
                sectionTitle("INTERFACE SIZE");
                constexpr std::array<const char*, 4U> sizeLabels{"S", "M", "L", "XL"};
                for (int sizeIndex = 0; sizeIndex < 4; ++sizeIndex) {
                    if (sizeIndex != 0) ImGui::SameLine();
                    const bool selected = m_guiScaleIndex == sizeIndex;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiSelected);
                    if (widgets::Button(sizeLabels[static_cast<std::size_t>(sizeIndex)],
                        ImVec2(64.0F * uiScale, 0.0F)) && !selected) {
                        m_guiScaleIndex = sizeIndex;
                        m_guiScaleDirty = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                }
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Window width",
                    &m_features.clickGuiWidthPercent, 40, 150, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Window height",
                    &m_features.clickGuiHeightPercent, 40, 150, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Gaussian background blur",
                    &m_features.clickGuiBlur, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= widgets::SliderInt("Surface opacity",
                    &m_features.clickGuiOpacity, 35, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                sectionTitle("INPUT");
                hotkeyControl("Open Click GUI", 1, m_menuHotkey);
                ImGui::TextDisabled("ESC closes the GUI and restores Minecraft mouse capture.");
                sectionTitle("THEME");
                constexpr std::array<const char*,2> themeLabels{"Dark","Light"};
                for(int themeIndex=0;themeIndex<2;++themeIndex) {
                    if(themeIndex) ImGui::SameLine();
                    const bool selected=m_features.clickGuiLightTheme==(themeIndex==1);
                    if(selected) ImGui::PushStyleColor(ImGuiCol_Button,guiSelected);
                    if(widgets::Button(themeLabels[themeIndex],ImVec2(116*uiScale,0)) && !selected) {
                        m_features.clickGuiLightTheme=themeIndex==1;changed=true;
                    }
                    if(selected) ImGui::PopStyleColor();
                }
                std::array<float, 3U> accentColor = unpackRgb(
                    m_features.clickGuiAccentColor);
                ImGui::SetNextItemWidth(240.0F * uiScale);
                if (widgets::ColorEdit3("Theme accent", accentColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.clickGuiAccentColor = packRgb(accentColor);
                    changed = true;
                }
                sectionTitle("SERVER SAFETY");
                changed |= animatedToggle(
                    "Allow restricted modules on Hypixel",
                    m_features.allowHypixelMovement,
                    m_toggleAnimation[34], uiScale);
                ImGui::TextColored(guiDanger,
                    "DANGER: Aim Assist, Fly, BHop and Scaffold can cause a server ban.");
                ImGui::TextWrapped(
                    "By default these four modules are force-disabled whenever the current server address is Hypixel. Enable this exception only if you explicitly accept that risk.");
            }
            // Preserve a full baseline below the final control so exact
            // bottom snapping never clips half of its label.
            ImGui::Dummy(ImVec2(1.0F,7.0F*uiScale));
            ImGui::PopTextWrapPos();
            endSmoothChild(m_settingsScroll[static_cast<std::size_t>(page)],delta);
            ImGui::PopID();
            ImGui::PopStyleVar();

            // The renderer applies the same guard as AgentRuntime so a click
            // cannot leave a high-risk module enabled for even one rendered
            // frame before the next runtime snapshot arrives.
            if (snapshot.hypixelServer && !m_features.allowHypixelMovement &&
                (m_features.aimAssistEnabled || m_features.scaffoldEnabled || m_features.flyEnabled ||
                 m_features.bhopEnabled)) {
                if (m_features.scaffoldEnabled && !featuresBefore.scaffoldEnabled)
                    if(host.message) host.message(
                        "WARNING: Scaffold can cause a server ban. Use only offline.",
                        false);
                if (m_features.flyEnabled && !featuresBefore.flyEnabled)
                    if(host.message) host.message(
                        "WARNING: Fly can cause a server ban. Use only offline.",
                        false);
                if (m_features.bhopEnabled && !featuresBefore.bhopEnabled)
                    if(host.message) host.message(
                        "WARNING: BHop can cause a server ban. Use only offline.",
                        false);
                m_features.aimAssistEnabled = false;
                m_features.scaffoldEnabled = false;
                m_features.flyEnabled = false;
                m_features.bhopEnabled = false;
                if(host.message) host.message(
                    "BLOCKED: restricted module disabled on Hypixel. See Interface / Server Safety.",
                    false);
                changed = true;
            }
            m_featureSettingsDirty = m_featureSettingsDirty || changed;
            if (changed && host.featureToasts) host.featureToasts(featuresBefore, m_features);
            widgets::animatePopups();
            windowDraw->PopClipRect();

            // Treat all generated GUI content as one flat layer. Scaling its
            // vertices once around the window centre makes the rail, header and
            // controls gather/scatter radially instead of appearing to travel
            // from the top-left. This is O(number of GUI vertices), requires no
            // per-widget animation state and leaves the final layout untouched.
            const float contentScale = spotlightScale;
            const ImVec2 contentCenter(windowPosition.x + windowSize.x * 0.5F,
                                       windowPosition.y + windowSize.y * 0.5F);
            const auto transformContent = [&](ImDrawList* const drawList,
                                              int begin, int end,
                                              const bool transformClips) noexcept {
                if (drawList == nullptr) return;
                begin = std::clamp(begin, 0, drawList->VtxBuffer.Size);
                end = std::clamp(end, begin, drawList->VtxBuffer.Size);
                const bool scaleGeometry=std::abs(contentScale-1.0F)>=0.0001F;
                const float alphaScale=std::clamp(spotlightEase,0.0F,1.0F);
                constexpr ImU32 alphaMask=static_cast<ImU32>(0xFFU)<<IM_COL32_A_SHIFT;
                for (int vertexIndex = begin; vertexIndex < end; ++vertexIndex) {
                    ImDrawVert& vertex=drawList->VtxBuffer[vertexIndex];
                    if(scaleGeometry) {
                        vertex.pos.x = contentCenter.x +
                            (vertex.pos.x - contentCenter.x) * contentScale;
                        vertex.pos.y = contentCenter.y +
                            (vertex.pos.y - contentCenter.y) * contentScale;
                    }
                    const unsigned sourceAlpha=(vertex.col>>IM_COL32_A_SHIFT)&0xFFU;
                    const unsigned fadedAlpha=static_cast<unsigned>(std::lround(
                        static_cast<float>(sourceAlpha)*alphaScale));
                    vertex.col=(vertex.col&~alphaMask)|
                        ((static_cast<ImU32>(std::min(fadedAlpha,255U)))<<IM_COL32_A_SHIFT);
                }
                if (transformClips && scaleGeometry) {
                    for (ImDrawCmd& command : drawList->CmdBuffer) {
                        command.ClipRect.x = contentCenter.x +
                            (command.ClipRect.x - contentCenter.x) * contentScale;
                        command.ClipRect.y = contentCenter.y +
                            (command.ClipRect.y - contentCenter.y) * contentScale;
                        command.ClipRect.z = contentCenter.x +
                            (command.ClipRect.z - contentCenter.x) * contentScale;
                        command.ClipRect.w = contentCenter.y +
                            (command.ClipRect.w - contentCenter.y) * contentScale;
                    }
                }
            };
            const int parentContentVertexEnd = windowDraw->VtxBuffer.Size;
            transformContent(windowDraw, parentContentVertexStart,
                             parentContentVertexEnd, true);
            // Every BeginChild owns a distinct draw list. Transform every
            // active descendant of this ClickGUI root so nested Aim content
            // and option labels close with the same radial/fade animation as
            // the parent instead of lingering for an extra frame.
            ImGuiContext& imguiState=*ImGui::GetCurrentContext();
            ImGuiWindow* const rootWindow=ImGui::GetCurrentWindow();
            clickGuiRootForDiffusion=rootWindow;
            clickGuiDrawForDiffusion=windowDraw;
            for(ImGuiWindow* child:imguiState.Windows) {
                if(child==nullptr||child==rootWindow||!child->Active) continue;
                const bool ownedByClickGui=child->RootWindow==rootWindow ||
                    child->RootWindowPopupTree==rootWindow;
                if(!ownedByClickGui) continue;
                transformContent(child->DrawList,0,
                    child->DrawList->VtxBuffer.Size,true);
            }
            transformContent(backgroundDraw, shadowVertexStart,
                             backgroundDraw->VtxBuffer.Size, false);
        }
        ImGui::End();
        // Diffusion appends raw draw commands/indices. Do it only after the
        // root window has ended so Dear ImGui will not append more geometry to
        // the same list with stale internal write cursors. Child lists have
        // already ended by this point as well.
        if(clickGuiDrawForDiffusion!=nullptr) {
            if(host.transientBlur) host.transientBlur(clickGuiDrawForDiffusion,
                1.0F-spotlightEase,uiScale);
            if(clickGuiRootForDiffusion!=nullptr) {
                ImGuiContext& imguiState=*ImGui::GetCurrentContext();
                for(ImGuiWindow* child:imguiState.Windows) {
                    if(child==nullptr||child==clickGuiRootForDiffusion||
                       !child->Active) continue;
                    const bool ownedByClickGui=
                        child->RootWindow==clickGuiRootForDiffusion ||
                        child->RootWindowPopupTree==clickGuiRootForDiffusion;
                    if(!ownedByClickGui) continue;
                    if(host.transientBlur) host.transientBlur(child->DrawList,
                        1.0F-spotlightEase,uiScale);
                }
            }
        }
        ImGui::PopStyleColor(22);
        ImGui::PopStyleVar(5);
    }

    // Adding a recent player is intentionally a separate modal surface. It no
    frame.theme = theme;
    frame.guiSurface = guiSurface;
    frame.guiRail = guiRail;
    frame.guiText = guiText;
    frame.guiMuted = guiMuted;
    frame.guiFrame = guiFrame;
    frame.guiScrollbarTrack = guiScrollbarTrack;
    frame.guiScrollbarGrab = guiScrollbarGrab;
    frame.guiAccent = guiAccent;
    frame.guiSelected = guiSelected;
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
}


} // namespace mcoverlay::ui
