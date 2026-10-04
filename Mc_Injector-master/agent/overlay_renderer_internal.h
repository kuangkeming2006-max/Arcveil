#pragma once

#include "overlay_renderer.h"
#include "tsf_candidates.h"
#include <imgui.h>
#include <new>

namespace mcoverlay {

struct OverlayInputState final {
    TsfCandidates* tsf = new (std::nothrow) TsfCandidates;
    ~OverlayInputState() { if (tsf) tsf->Release(); }
    std::atomic<bool> imeEnabled{false};
    std::atomic<bool> interactive{false};
    std::atomic<bool> gameScreenOpen{true};
    std::atomic<bool> composingInput{false};
    std::atomic<bool> clickGuiToggle{false};
    std::atomic<unsigned> menuHotkey{VK_OEM_7};
    std::atomic<bool> acceptImGuiMessages{false};
    std::atomic<bool> windowProcedureAvailable{false};
    // Calling the ImGui Win32 backend is only legal when WndProc and OpenGL
    // share a thread. A Lunar split-thread window still installs the hook for
    // suppression/hotkeys, while the render thread feeds ImGui by polling.
    std::atomic<bool> directImGuiWndProc{false};
    std::atomic<bool> captureHotkey{false};
    std::atomic<unsigned> capturedHotkey{0U};
    std::atomic<int> mediaPreviousHotkey{VK_MEDIA_PREV_TRACK};
    std::atomic<int> mediaToggleHotkey{VK_MEDIA_PLAY_PAUSE};
    std::atomic<int> mediaNextHotkey{VK_MEDIA_NEXT_TRACK};
    std::atomic<std::uint8_t> mediaAction{0U};
    // One immutable native cursor is retained for the whole overlay session.
    // Changing HCURSOR shapes every render frame is both visually wrong for
    // custom/Lunar cursors and creates a WM_SETCURSOR race while dragging.
    std::atomic<HCURSOR> sessionCursor{nullptr};
    std::atomic<HWND> window{nullptr};
    std::atomic<ImGuiContext*> imguiContext{nullptr};
    bool fallbackPrimed = false;
    bool menuKeyDown = false;
    bool escapeKeyDown = false;
    bool mouseDown[5]{};
    bool captureKeysPrimed = false;
    std::array<bool, 256U> keyDown{};
    // Legacy IMM messages are delivered on the game window thread, which can
    // differ from Lunar's OpenGL presentation thread. Fixed buffers plus this
    // lock make the handoff allocation-free and race-free.
    SRWLOCK imeLock = SRWLOCK_INIT;
    std::array<wchar_t, 80U> imeName{};
    std::array<wchar_t, 128U> imeComposition{};
    std::array<std::array<wchar_t, 64U>, 9U> imeCandidates{};
    std::uint32_t imeCandidateCount = 0U;
    std::uint32_t imeCandidateSelection = 0U;
    bool imeComposing = false;
    std::atomic<std::uint64_t> imeRevision{0U};
};

struct OverlayRenderer::RenderFrameContext final {
    const GameSnapshot& snapshot;
    bool interactive;
    ImGuiIO& io;
    float uiScale;
    float delta;
    bool gameplayHotkeysAllowed;
    float guiEase;
    float theme = 0.0F;
    ImVec4 guiSurface{};
    ImVec4 guiRail{};
    ImVec4 guiText{};
    ImVec4 guiMuted{};
    ImVec4 guiFrame{};
    ImVec4 guiScrollbarTrack{};
    ImVec4 guiScrollbarGrab{};
    ImVec4 guiAccent{};
    ImVec4 guiSelected{};
};

namespace renderer_detail {
ImVec4 mixColor(const ImVec4& dark, const ImVec4& light, float amount) noexcept;

struct ScreenPoint final {
    float x = 0.0F;
    float y = 0.0F;
    double clipX = 0.0;
    double clipY = 0.0;
    double clipW = 0.0;
    bool finiteClip = false;
    bool visible = false;
};
struct PrestigeStyle final {
    ImU32 color = IM_COL32(170, 170, 170, 255);
    bool master = false;
};
struct KenneyPromptTile final {
    ImVec2 uv0{};
    ImVec2 uv1{};
    int width = 0;
    int height = 0;
    bool valid = false;
};

void appendTransientSoftBlur(ImDrawList* const drawList,const float amount,
                             const float uiScale) noexcept;
void beginSmoothChild(const char* id, ImVec2 size, SmoothScroll& scroll,
                      float delta, ImGuiWindowFlags extra = 0) noexcept;
void endSmoothChild(SmoothScroll& scroll,const float delta) noexcept;
UINT imeShutdownMessage() noexcept;
void stopTsf(HWND window, OverlayInputState* input) noexcept;
void clearImeComposition(OverlayInputState& input) noexcept;
void updateImeState(OverlayInputState& input, const HWND window,
                    const ImeMessageAction action,const LPARAM lParam) noexcept;
void advancePresentationSpring(float& value, float& velocity,
                               const float target, const float delta) noexcept;
ScreenPoint projectPoint(const WorldCameraSnapshot& camera,
                         const ImVec2 displaySize,
                         const double x, const double y, const double z) noexcept;
void drawProjectedBox(ImDrawList* const drawList,
                      const WorldCameraSnapshot& camera,
                      const ImVec2 displaySize,
                      const AxisAlignedBox& box,
                      const ImU32 color,
                      const char* const label,
                      const bool filled = false) noexcept;
ImU32 packedRgbColor(const std::uint32_t rgb, const int alpha = 255) noexcept;
ImU32 contrastingTextColor(const std::uint32_t rgb) noexcept;
std::array<float, 3U> unpackRgb(const std::uint32_t rgb) noexcept;
std::uint32_t packRgb(const std::array<float, 3U>& color) noexcept;
PrestigeStyle bedWarsPrestigeStyle(const int stars) noexcept;
bool clipProjectedLine(const ScreenPoint& firstPoint,
                       const ScreenPoint& secondPoint,
                       const WorldCameraSnapshot& camera,
                       const ImVec2 displaySize,
                       ImVec2& first, ImVec2& second) noexcept;
bool projectedBoxBounds(const WorldCameraSnapshot& camera,
                        const ImVec2 displaySize,
                        const AxisAlignedBox& box,
                        ImVec2& minimum,
                        ImVec2& maximum) noexcept;
const char* protectionRoman(const std::uint8_t level) noexcept;
void drawPrestigeStar(ImDrawList* const drawList, const ImVec2 center,
                      const float radius, const ImU32 color,
                      const bool master) noexcept;
void formatCompactCount(char* const output, const std::size_t capacity,
                        const std::int64_t value) noexcept;
void drawRoundedTriangle(ImDrawList* const drawList,
                         const ImVec2 a, const ImVec2 b, const ImVec2 c,
                         const float radius, const ImU32 color) noexcept;
bool isMouseMessage(const UINT message) noexcept;
bool isKeyboardMessage(const UINT message) noexcept;
bool animatedToggle(const char* const label, bool& value, float& animation,
                    const float uiScale) noexcept;
const char* hypixelStateText(const HypixelOverlaySnapshot::State state) noexcept;
float guiScaleForIndex(const int index) noexcept;
const char* hotkeyName(const unsigned virtualKey) noexcept;
const char* kenneyPromptName(const unsigned virtualKey) noexcept;
KenneyPromptTile kenneyPromptTile(const char* const xmlData,
                                  const std::size_t xmlSize,
                                  const unsigned virtualKey) noexcept;
bool rawModuleResource(HMODULE module, const int identifier,
                       const unsigned char*& data,
                       std::size_t& size) noexcept;
float cubicBezierProgress(const float position, const float x1,
                          const float y1, const float x2,
                          const float y2) noexcept;
float smootherStep(float value) noexcept;
ImVec4 teamColor(const char code) noexcept;
ImU32 defenseBlockColor(const std::uint16_t blockId,
                        const std::uint8_t metadata) noexcept;
void drawInventoryBlockIcon(ImDrawList* const draw, const ImVec2 center,
                            const float size, const std::uint16_t blockId,
                            const std::uint8_t metadata,
                            const unsigned* textures) noexcept;

} // namespace renderer_detail
} // namespace mcoverlay
