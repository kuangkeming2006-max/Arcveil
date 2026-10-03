#pragma once
#include "ClickGui.h"
#include <imgui.h>

namespace mcoverlay::ui {
// Each GUI owns its motion store, independent of the ImGui context.
namespace widgets {
void begin(ClickGuiDesignState& state, float scale) noexcept;
ControlMotion& motion(ImGuiID id, bool hovered, bool active=false) noexcept;
bool Button(const char* label, ImVec2 size=ImVec2(0,0)) noexcept;
bool SmallButton(const char* label) noexcept;
bool Checkbox(const char* label, bool* value) noexcept;
bool SliderInt(const char* label, int* value, int min, int max,
               const char* format="%d", ImGuiSliderFlags flags=0) noexcept;
bool Combo(const char* label, int* current, const char* const items[], int count,
           int height=-1) noexcept;
bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags=0) noexcept;
bool Selectable(const char* label, bool selected=false, ImGuiSelectableFlags flags=0,
                ImVec2 size=ImVec2(0,0)) noexcept;
bool ColorEdit3(const char* label, float color[3], ImGuiColorEditFlags flags=0) noexcept;
bool InputText(const char* label, char* buffer, std::size_t length,
               ImGuiInputTextFlags flags=0) noexcept;
void animatePopups() noexcept;
}
bool animatedToggle(const char* label, bool& value, float& animation, float scale) noexcept;
} // namespace mcoverlay::ui
