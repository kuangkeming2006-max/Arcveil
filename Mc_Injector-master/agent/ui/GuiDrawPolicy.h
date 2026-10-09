#pragma once
#include <imgui.h>

namespace mcoverlay::ui {
inline void configureGuiDrawList(ImDrawList* draw, float presentationScale=1.F) noexcept {
    // Text must move as a continuous surface rather than snapping each glyph
    // independently. Geometry AA avoids scaling the baked one-pixel line UVs.
    draw->Flags |= ImDrawListFlags_TextNoPixelSnap;
    draw->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    // The subsequent gather/scatter transform keeps a one-screen-pixel fringe.
    draw->_FringeScale = 1.F / presentationScale;
}
} // namespace mcoverlay::ui
