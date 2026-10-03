#pragma once
#include "GuiTypography.h"
#include <array>
#include <cstdint>
#include <unordered_map>

namespace mcoverlay::ui {
struct ControlMotion {
    float hover=0,press=0,value=0,focus=0;
    bool initialized=false;
};
// Keep the renderer's public header independent of ImGui implementation types.
// ImGuiID is an unsigned 32-bit value; only the shared view interprets it.
struct ClickGuiDesignState {
    GuiTypography typography;
    bool typographyDirty=false;
    int category=-1;
    std::array<int,6> rememberedPage{{8,4,0,3,11,13}};
    std::array<float,6> categorySelection{};
    std::unordered_map<std::uint32_t,ControlMotion> controls;
    float pageElapsed=.45F,railElapsed=.35F;
    std::unordered_map<std::uint32_t,float> popupAges;
    bool reducedMotion=false;
};
} // namespace mcoverlay::ui
