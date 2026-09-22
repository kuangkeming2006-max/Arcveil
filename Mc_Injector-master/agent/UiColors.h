#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace mcoverlay::ui {
using Rgb=std::array<float,3>;
inline float luminance(const Rgb& rgb) noexcept {
    const auto linear=[](float v){return v<=0.04045F?v/12.92F:
        std::pow((v+0.055F)/1.055F,2.4F);};
    return 0.2126F*linear(rgb[0])+0.7152F*linear(rgb[1])+0.0722F*linear(rgb[2]);
}
inline float contrast(const Rgb& a,const Rgb& b) noexcept {
    const float x=luminance(a),y=luminance(b);
    return (std::max(x,y)+0.05F)/(std::min(x,y)+0.05F);
}
inline Rgb readableAccent(Rgb accent,const Rgb& surface,const bool light) noexcept {
    const Rgb original=accent;
    for(int step=0;step<=100;++step) {
        const float t=static_cast<float>(step)*0.01F;
        for(std::size_t i=0;i<3;++i) accent[i]=original[i]*(1-t)+(light?0.0F:t);
        if(contrast(accent,surface)>=4.6F) break;
    }
    return accent;
}
}
