#include "AnimatedWidgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mcoverlay::ui {
namespace {
thread_local ClickGuiDesignState* design=nullptr;
thread_local float unit=1;
ImVec2 add(ImVec2 p,float x,float y) { return {p.x+x,p.y+y}; }
ImVec4 mix(ImVec4 a,ImVec4 b,float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
}
ImU32 color(ImVec4 c) { return ImGui::GetColorU32(c); }
ImVec4 accent() { return ImGui::GetStyleColorVec4(ImGuiCol_CheckMark); }
ImVec4 surface() { return ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); }
ImVec4 text() { return ImGui::GetStyleColorVec4(ImGuiCol_Text); }
void labelText(ImDrawList* draw,ImVec2 at,const char* label,ImU32 col) {
    draw->AddText(at,col,label,ImGui::FindRenderedTextEnd(label));
}
float advance(float from,float to,float rate) {
    return design->reducedMotion ? to : approachExponential(from,to,rate,ImGui::GetIO().DeltaTime);
}
void focusRing(ImDrawList* draw,ImVec2 a,ImVec2 b,float focus,float rounding) {
    if(focus<.001F) return;
    auto c=accent(); c.w=.6F*focus;
    // The control may sit against a scrolling child's clip edge. Keep the
    // entire stroke and its antialias fringe inside its allocated rectangle.
    const float inset=1.5F*unit+.5F;
    draw->AddRect(add(a,inset,inset),add(b,-inset,-inset),color(c),
        std::max(0.F,rounding-inset),0,1.4F*unit);
}
}
namespace widgets {
void begin(ClickGuiDesignState& state,float scale) noexcept { design=&state;unit=scale; }
ControlMotion& motion(ImGuiID id,bool hovered,bool active) noexcept {
    auto& m=design->controls[id];
    m.hover=advance(m.hover,hovered?1.F:0.F,15);
    m.press=advance(m.press,active?1.F:0.F,24);
    const bool focused=ImGui::GetCurrentContext()->NavId==id;
    m.focus=advance(m.focus,focused?1.F:0.F,18);
    return m;
}
bool Button(const char* label,ImVec2 size) noexcept {
    const auto p=ImGui::GetCursorScreenPos();
    const auto t=ImGui::CalcTextSize(label,nullptr,true);
    if(size.x<=0) size.x=t.x+28*unit;
    if(size.y<=0) size.y=34*unit;
    size.x=std::min(size.x,std::max(1.F,ImGui::GetContentRegionAvail().x));
    const auto id=ImGui::GetID(label);
    const bool changed=ImGui::InvisibleButton(label,size,ImGuiButtonFlags_EnableNav);
    auto& m=motion(id,ImGui::IsItemHovered(),ImGui::IsItemActive());
    const float dy=(-m.hover+2*m.press)*unit;
    const ImVec2 a=add(p,0,dy), b=add(a,size.x,size.y);
    auto* draw=ImGui::GetWindowDrawList();
    auto base=ImGui::GetStyleColorVec4(ImGuiCol_Button);
    auto bg=mix(base,accent(),m.hover*.14F+m.press*.12F);
    draw->AddRectFilled(a,b,color(bg),9*unit);
    auto border=mix(surface(),accent(),m.hover*.35F);border.w=.65F;
    draw->AddRect(a,b,color(border),9*unit,0,unit);
    labelText(draw,add(a,(size.x-t.x)*.5F,(size.y-t.y)*.5F),label,color(text()));
    focusRing(draw,a,b,m.focus,9*unit);
    return changed;
}
bool SmallButton(const char* label) noexcept {return Button(label,{0,26*unit});}
bool Checkbox(const char* label,bool* value) noexcept {
    const auto p=ImGui::GetCursorScreenPos();
    const auto t=ImGui::CalcTextSize(label,nullptr,true);
    const auto id=ImGui::GetID(label);
    const bool changed=ImGui::InvisibleButton(label,{t.x+30*unit,28*unit},ImGuiButtonFlags_EnableNav);
    if(changed)*value=!*value;
    auto& m=motion(id,ImGui::IsItemHovered(),ImGui::IsItemActive());
    if(!m.initialized){m.value=*value?1.F:0.F;m.initialized=true;}
    m.value=advance(m.value,*value?1.F:0.F,19);
    auto* draw=ImGui::GetWindowDrawList();
    const auto a=add(p,0,4*unit),b=add(a,20*unit,20*unit);
    draw->AddRectFilled(a,b,color(mix(surface(),accent(),m.value*.85F+m.hover*.1F)),5*unit);
    auto border=accent();border.w=.2F+.4F*m.hover;
    draw->AddRect(a,b,color(border),5*unit,0,unit);
    auto check=text();check.w=m.value;
    ImGui::RenderCheckMark(draw,add(a,4*unit,4*unit),color(check),12*unit);
    labelText(draw,add(p,30*unit,(28*unit-t.y)*.5F),label,color(text()));
    focusRing(draw,a,b,m.focus,5*unit);
    return changed;
}
bool SliderInt(const char* label,int* value,int minimum,int maximum,const char* format,ImGuiSliderFlags flags) noexcept {
    const auto p=ImGui::GetCursorScreenPos();
    const float w=std::min(ImGui::CalcItemWidth(),std::max(1.F,ImGui::GetContentRegionAvail().x));
    auto* draw=ImGui::GetWindowDrawList();
    char display[64];std::snprintf(display,sizeof(display),format,*value);
    const auto valueSize=ImGui::CalcTextSize(display);
    const float labelWidth=std::max(40*unit,w-valueSize.x-30*unit);
    const char* labelEnd=ImGui::FindRenderedTextEnd(label);
    const float labelHeight=ImGui::GetFont()->CalcTextSizeA(
        ImGui::GetFontSize(),10000,labelWidth,label,labelEnd).y;
    const float barOffset=std::max(20*unit,labelHeight)+6*unit;
    const float barHeight=ImGui::GetFontSize()+8*unit;
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),p,color(text()),label,labelEnd,labelWidth);
    auto valueColor=accent();valueColor.w=.9F;
    draw->AddRectFilled(add(p,w-valueSize.x-16*unit,-2*unit),add(p,w,ImGui::GetFontSize()+2*unit),color(mix(surface(),accent(),.12F)),5*unit);
    draw->AddText(add(p,w-valueSize.x-8*unit,0),color(valueColor),display);
    ImGui::SetCursorScreenPos(add(p,0,barOffset));
    ImGui::SetNextItemWidth(w);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{0,4*unit});
    for(auto c:{ImGuiCol_FrameBg,ImGuiCol_FrameBgHovered,ImGuiCol_FrameBgActive,
                ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive,ImGuiCol_Text})
        ImGui::PushStyleColor(c,{0,0,0,0});
    ImGui::PushID(label);
    const auto id=ImGui::GetID("##value");
    const bool changed=ImGui::SliderInt("##value",value,minimum,maximum,"",flags);
    const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    ImGui::PopID();ImGui::PopStyleColor(6);ImGui::PopStyleVar();
    auto& m=motion(id,hovered,active);
    const float ratio=maximum>minimum?float(*value-minimum)/float(maximum-minimum):0;
    if(!m.initialized){m.value=ratio;m.initialized=true;}
    m.value=advance(m.value,std::clamp(ratio,0.F,1.F),active?40:18);
    const float y=p.y+barOffset+barHeight*.5F;
    const float r=(5.5F+m.hover*1.5F-m.press*.5F)*unit;
    const float x=p.x+7*unit+m.value*std::max(0.F,w-14*unit);
    const auto a=ImVec2(p.x,y-2.5F*unit), b=ImVec2(p.x+w,y+2.5F*unit);
    draw->AddRectFilled(a,b,color(mix(surface(),accent(),m.hover*.10F)),3*unit);
    draw->AddRectFilled(a,{x,b.y},color(accent()),3*unit);
    auto glow=accent();glow.w=.12F*m.hover;
    draw->AddCircleFilled({x,y},11*unit,color(glow),28);
    draw->AddCircleFilled({x,y+unit},r+unit,color({0,0,0,.25F}),28);
    draw->AddCircleFilled({x,y},r,color(text()),28);
    focusRing(draw,add(p,-unit,barOffset),add(p,w,barOffset+barHeight),m.focus,6*unit);
    ImGui::SetCursorScreenPos(p);ImGui::Dummy({w,barOffset+barHeight+6*unit});
    return changed;
}
bool BeginCombo(const char* label,const char* preview,ImGuiComboFlags flags) noexcept {
    const auto id=ImGui::GetID(label);
    auto& m=design->controls[id];
    const auto p=ImGui::GetCursorScreenPos();
    const float width=std::min(ImGui::CalcItemWidth(),std::max(1.F,ImGui::GetContentRegionAvail().x));
    const auto mouse=ImGui::GetIO().MousePos;
    const bool hovered=mouse.x>=p.x && mouse.x<=p.x+width && mouse.y>=p.y && mouse.y<=p.y+34*unit;
    m=motion(id,hovered,ImGui::IsPopupOpen(id,ImGuiPopupFlags_None));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{12*unit,8*unit});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,8*unit);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,10*unit);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,mix(surface(),accent(),m.hover*.1F));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,mix(surface(),accent(),m.hover*.1F));
    ImGui::PushStyleColor(ImGuiCol_Button,mix(surface(),accent(),m.hover*.15F));
    const bool open=ImGui::BeginCombo(label,preview,flags);
    ImGui::PopStyleColor(3);ImGui::PopStyleVar(3);
    return open;
}
bool Selectable(const char* label,bool selected,ImGuiSelectableFlags flags,ImVec2 size) noexcept {
    const auto id=ImGui::GetID(label);
    auto& m=design->controls[id];
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,mix(surface(),accent(),m.hover*.20F));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,mix(surface(),accent(),.24F));
    if(size.y<=0)size.y=28*unit;
    const bool pressed=ImGui::Selectable(label,selected,flags,size);
    motion(id,ImGui::IsItemHovered(),ImGui::IsItemActive());
    ImGui::PopStyleColor(2);
    return pressed;
}
bool Combo(const char* label,int* current,const char* const items[],int count,int height) noexcept {
    static_cast<void>(height);
    bool changed=false;
    if(BeginCombo(label,*current>=0&&*current<count?items[*current]:"Choose")) {
        for(int i=0;i<count;++i) {
            ImGui::PushID(i);
            if(Selectable(items[i],i==*current)){*current=i;changed=true;}
            if(i==*current)ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}
bool ColorEdit3(const char* label,float c[3],ImGuiColorEditFlags flags) noexcept {
    const auto id=ImGui::GetID(label);
    auto& m=design->controls[id];
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,8*unit);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{8*unit,7*unit});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,mix(surface(),accent(),m.hover*.12F));
    const bool changed=ImGui::ColorEdit3(label,c,flags);
    motion(id,ImGui::IsItemHovered(),ImGui::IsItemActive());
    ImGui::PopStyleColor();ImGui::PopStyleVar(2);
    return changed;
}
bool InputText(const char* label,char* buffer,std::size_t length,ImGuiInputTextFlags flags) noexcept {
    const auto id=ImGui::GetID(label);
    auto& m=design->controls[id];
    ImGui::PushStyleColor(ImGuiCol_FrameBg,mix(surface(),accent(),.06F*m.hover+.06F*m.press));
    const bool changed=ImGui::InputText(label,buffer,length,flags);
    motion(id,ImGui::IsItemHovered(),ImGui::IsItemActive());
    ImGui::PopStyleColor();
    return changed;
}
void animatePopups() noexcept {
    // Final-layout hit targets stay stable. The real popup draw data fades
    // after EndCombo, including nested color-picker windows.
    for(auto* window:ImGui::GetCurrentContext()->Windows) {
        if(!window->Active || !(window->Flags&ImGuiWindowFlags_Popup))continue;
        auto& age=design->popupAges[window->ID];
        age=window->Appearing?0.F:std::min(.18F,age+ImGui::GetIO().DeltaTime);
        const float t=design->reducedMotion?1.F:std::clamp(age/.18F,0.F,1.F);
        const float alpha=1-std::pow(1-t,3);
        constexpr ImU32 mask=ImU32(255)<<IM_COL32_A_SHIFT;
        for(auto& vertex:window->DrawList->VtxBuffer) {
            const auto value=unsigned(std::round(float((vertex.col>>IM_COL32_A_SHIFT)&255)*alpha));
            vertex.col=(vertex.col&~mask)|(ImU32(value)<<IM_COL32_A_SHIFT);
        }
    }
}
}

bool animatedToggle(const char* label,bool& value,float& animation,float scale) noexcept {
    unit=scale;
    const auto p=ImGui::GetCursorScreenPos();
    const float w=std::strcmp(label,"Enabled")==0?110*unit:std::max(1.F,ImGui::GetContentRegionAvail().x);
    const float h=36*unit;
    const auto id=ImGui::GetID(label);
    const bool changed=ImGui::InvisibleButton(label,{w,h},ImGuiButtonFlags_EnableNav);
    if(changed)value=!value;
    auto& m=widgets::motion(id,ImGui::IsItemHovered(),ImGui::IsItemActive());
    if(!m.initialized){m.value=value?1.F:0.F;m.initialized=true;}
    m.value=advance(m.value,value?1.F:0.F,19);
    animation=m.value;
    const float t=m.value*m.value*(3-2*m.value);
    auto* draw=ImGui::GetWindowDrawList();
    auto row=accent();row.w=.035F*m.hover;
    draw->AddRectFilled(p,add(p,w,h),color(row),8*unit);
    labelText(draw,add(p,0,(h-ImGui::GetFontSize())*.5F),label,color(text()));
    const ImVec2 track=add(p,w-43*unit,7*unit),end=add(track,43*unit,22*unit);
    auto bg=mix(surface(),accent(),t);
    bg=mix(bg,text(),m.hover*.06F);
    draw->AddRectFilled(track,end,color(bg),11*unit);
    auto outline=mix(surface(),accent(),.3F*m.hover);outline.w=1-t;
    draw->AddRect(track,end,color(outline),11*unit,0,unit);
    const float knobX=track.x+11*unit+21*unit*t;
    const float stretch=m.press*1.7F*unit;
    const ImVec2 knobMin{knobX-7*unit-stretch,track.y+4*unit},knobMax{knobX+7*unit+stretch,track.y+18*unit};
    auto knob=mix(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),ImVec4(1,1,1,1),t);
    draw->AddRectFilled(add(knobMin,0,unit),add(knobMax,0,unit),color({0,0,0,.15F}),7*unit);
    draw->AddRectFilled(knobMin,knobMax,color(knob),7*unit);
    focusRing(draw,track,end,m.focus,11*unit);
    return changed;
}
} // namespace mcoverlay::ui
