#include "overlay_renderer.h"
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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "bed_png.h"
#include "block_textures.h"

// Dear ImGui intentionally keeps this declaration inside `#if 0` in the
// backend header so including it does not force windows.h on every consumer.
// This translation unit already includes Win32 types through our headers.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

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

namespace {

// Short-lived draw-list diffusion used only while a surface is entering or
// leaving. Four low-alpha offset copies soften text, icons and custom geometry
// together without introducing another framebuffer/FBO lifetime into Lunar's
// OpenGL context. The source remains authoritative, so hit testing and layout
// are untouched and the extra work exists only during the transition.
void appendTransientSoftBlur(ImDrawList* const drawList,const float amount,
                             const float uiScale) noexcept
{
    if(drawList==nullptr) return;
    const float blur=std::clamp(amount,0.0F,1.0F);
    if(blur<0.015F) return;

    const int sourceVertexCount=drawList->VtxBuffer.Size;
    const int sourceIndexCount=drawList->IdxBuffer.Size;
    const int sourceCommandCount=drawList->CmdBuffer.Size;
    if(sourceVertexCount<=0||sourceIndexCount<=0||sourceCommandCount<=0) return;

    // The project uses imgui_impl_opengl2. That backend intentionally does not
    // advertise/support RendererHasVtxOffset, so never manufacture commands
    // that rely on a non-zero VtxOffset. Duplicate indices as absolute indices
    // instead, and gracefully skip/reduce the diffusion when a 16-bit
    // ImDrawIdx buffer would overflow.
    for(int i=0;i<sourceCommandCount;++i) {
        if(drawList->CmdBuffer[i].VtxOffset!=0U) return;
    }

    unsigned maxSourceIndex=0U;
    for(int i=0;i<sourceIndexCount;++i)
        maxSourceIndex=std::max(maxSourceIndex,
            static_cast<unsigned>(drawList->IdxBuffer[i]));

    const unsigned maxDrawIndex=static_cast<unsigned>(
        std::numeric_limits<ImDrawIdx>::max());
    int tapCount=0;
    for(int candidate=1;candidate<=4;++candidate) {
        const std::uint64_t vertexBase=static_cast<std::uint64_t>(sourceVertexCount)*
            static_cast<std::uint64_t>(candidate);
        if(vertexBase+maxSourceIndex>maxDrawIndex) break;
        tapCount=candidate;
    }
    if(tapCount<=0) return;

    const float spread=(0.45F+1.85F*blur)*std::max(0.5F,uiScale);
    const float ghostAlpha=0.075F+0.11F*blur;
    const std::array<ImVec2,4U> offsets{{
        ImVec2(-spread,0.0F),ImVec2(spread,0.0F),
        ImVec2(0.0F,-spread),ImVec2(0.0F,spread)}};
    constexpr ImU32 alphaMask=static_cast<ImU32>(0xFFU)<<IM_COL32_A_SHIFT;

    drawList->VtxBuffer.reserve(sourceVertexCount*(1+tapCount));
    drawList->IdxBuffer.reserve(sourceIndexCount*(1+tapCount));
    drawList->CmdBuffer.reserve(sourceCommandCount*(1+tapCount));
    for(int tap=0;tap<tapCount;++tap) {
        const ImVec2 offset=offsets[static_cast<std::size_t>(tap)];
        const unsigned vertexBase=static_cast<unsigned>(drawList->VtxBuffer.Size);
        const unsigned indexBase=static_cast<unsigned>(drawList->IdxBuffer.Size);

        for(int i=0;i<sourceVertexCount;++i) {
            ImDrawVert vertex=drawList->VtxBuffer[i];
            vertex.pos.x+=offset.x;
            vertex.pos.y+=offset.y;
            const unsigned sourceAlpha=(vertex.col>>IM_COL32_A_SHIFT)&0xFFU;
            const unsigned blurredAlpha=static_cast<unsigned>(std::lround(
                static_cast<float>(sourceAlpha)*ghostAlpha));
            vertex.col=(vertex.col&~alphaMask)|
                ((static_cast<ImU32>(std::min(blurredAlpha,255U)))<<IM_COL32_A_SHIFT);
            drawList->VtxBuffer.push_back(vertex);
        }

        for(int i=0;i<sourceIndexCount;++i) {
            const unsigned absolute=vertexBase+
                static_cast<unsigned>(drawList->IdxBuffer[i]);
            drawList->IdxBuffer.push_back(static_cast<ImDrawIdx>(absolute));
        }

        for(int i=0;i<sourceCommandCount;++i) {
            const ImDrawCmd& source=drawList->CmdBuffer[i];
            if(source.ElemCount==0||source.UserCallback!=nullptr) continue;
            ImDrawCmd command=source;
            command.IdxOffset=indexBase+source.IdxOffset;
            command.VtxOffset=0U;
            command.ClipRect.x+=offset.x;
            command.ClipRect.y+=offset.y;
            command.ClipRect.z+=offset.x;
            command.ClipRect.w+=offset.y;
            drawList->CmdBuffer.push_back(command);
        }
    }
    // Direct vector growth bypasses PrimReserve(), so Dear ImGui's cached
    // write cursors still point at the pre-reserve buffers. Every call site
    // invokes this only after its window/child has finished emitting normal
    // geometry; publish the final buffer ends before AddDrawListToDrawData()
    // validates and submits the list.
    drawList->_VtxWritePtr=drawList->VtxBuffer.Data+drawList->VtxBuffer.Size;
    drawList->_IdxWritePtr=drawList->IdxBuffer.Data+drawList->IdxBuffer.Size;
    drawList->_VtxCurrentIdx=static_cast<unsigned int>(drawList->VtxBuffer.Size);
}

void beginSmoothChild(const char* id, ImVec2 size, SmoothScroll& scroll,
                      float delta, ImGuiWindowFlags extra = 0) noexcept
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

UINT imeShutdownMessage() noexcept
{
    static const UINT id = RegisterWindowMessageW(L"McOverlay.IME.Stop.v1");
    return id;
}
void stopTsf(HWND window, OverlayInputState* input) noexcept
{
    if (!input || !input->tsf) return;
    input->imeEnabled.store(false, std::memory_order_release);
    DWORD_PTR result = 0;
    // No renderer lock is acquired by the recipient. On a failed/hung HWND
    // the ref-counted COM sink stays alive rather than leaving a dangling callback.
    if (IsWindow(window)) SendMessageTimeoutW(window, imeShutdownMessage(), 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result);
}

void clearImeComposition(OverlayInputState& input) noexcept
{
    ::AcquireSRWLockExclusive(&input.imeLock);
    input.imeComposition={};
    input.imeCandidates={};
    input.imeCandidateCount=0U;
    input.imeCandidateSelection=0U;
    input.imeComposing=false;
    ::ReleaseSRWLockExclusive(&input.imeLock);
    input.imeRevision.fetch_add(1U,std::memory_order_release);
    input.composingInput.store(false,std::memory_order_release);
}

void updateImeState(OverlayInputState& input, const HWND window,
                    const ImeMessageAction action,const LPARAM lParam) noexcept
{
    if(action==ImeMessageAction::Ignore) return;
    if(action==ImeMessageAction::ResetComposition) {
        clearImeComposition(input);
        return;
    }
    std::array<wchar_t, 80U> name{};
    std::array<wchar_t, 128U> composition{};
    std::array<std::array<wchar_t, 64U>, 9U> candidates{};
    std::uint32_t candidateCount = 0U;
    std::uint32_t candidateSelection = 0U;
    bool composing = false;

    DWORD processId = 0U;
    const DWORD threadId = ::GetWindowThreadProcessId(window, &processId);
    const HKL layout = action == ImeMessageAction::ResetLayout
        ? reinterpret_cast<HKL>(lParam) : ::GetKeyboardLayout(threadId);
    if (layout != nullptr) {
        const UINT described = ::ImmGetDescriptionW(
            layout, name.data(), static_cast<UINT>(name.size()));
        if (described == 0U) {
            const LANGID language = LOWORD(reinterpret_cast<ULONG_PTR>(layout));
            (void)::GetLocaleInfoW(MAKELCID(language, SORT_DEFAULT),
                LOCALE_SLOCALIZEDDISPLAYNAME, name.data(),
                static_cast<int>(name.size()));
        }
    }

    // Layout changes are reset-only. The original WndProc has not completed
    // its input-context transition yet, so touching HIMC here can observe or
    // retain the old composition/candidate list.
    HIMC const ime = action==ImeMessageAction::QueryComposition
        ? ::ImmGetContext(window) : nullptr;
    if (ime != nullptr) {
        const LONG bytes = ::ImmGetCompositionStringW(
            ime, GCS_COMPSTR, composition.data(),
            static_cast<DWORD>((composition.size() - 1U) * sizeof(wchar_t)));
        if (bytes > 0) {
            composition[std::min<std::size_t>(
                static_cast<std::size_t>(bytes) / sizeof(wchar_t),
                composition.size() - 1U)] = L'\0';
            composing = true;
        }

        alignas(CANDIDATELIST) std::array<unsigned char, 8192U> candidateBytes{};
        const DWORD required = ::ImmGetCandidateListW(ime, 0U, nullptr, 0U);
        if (required >= sizeof(CANDIDATELIST) &&
            required <= candidateBytes.size()) {
            auto* const list = reinterpret_cast<CANDIDATELIST*>(
                candidateBytes.data());
            if (::ImmGetCandidateListW(ime, 0U, list,
                    static_cast<DWORD>(candidateBytes.size())) > 0U) {
                const DWORD pageStart = std::min(list->dwPageStart, list->dwCount);
                const DWORD pageCount = std::min<DWORD>(
                    std::min(list->dwPageSize, list->dwCount - pageStart),
                    static_cast<DWORD>(candidates.size()));
                for (DWORD index = 0U; index < pageCount; ++index) {
                    const DWORD sourceIndex = pageStart + index;
                    if (offsetof(CANDIDATELIST, dwOffset) +
                        (static_cast<std::size_t>(sourceIndex) + 1U) *
                            sizeof(DWORD) > required) continue;
                    const DWORD offset = list->dwOffset[sourceIndex];
                    if (offset >= required) continue;
                    const auto* const source = reinterpret_cast<const wchar_t*>(
                        candidateBytes.data() + offset);
                    std::size_t length = 0U;
                    const std::size_t availableCharacters =
                        (required - offset) / sizeof(wchar_t);
                    while (length + 1U < candidates[index].size() &&
                           length < availableCharacters &&
                           source[length] != L'\0') {
                        candidates[index][length] = source[length];
                        ++length;
                    }
                    ++candidateCount;
                }
                if (list->dwSelection >= pageStart &&
                    list->dwSelection < pageStart + pageCount) {
                    candidateSelection = list->dwSelection - pageStart;
                }
                composing = composing || candidateCount != 0U;
            }
        }
        ::ImmReleaseContext(window, ime);
    }

    ::AcquireSRWLockExclusive(&input.imeLock);
    input.imeName = name;
    input.imeComposition = composition;
    input.imeCandidates = candidates;
    input.imeCandidateCount = candidateCount;
    input.imeCandidateSelection = candidateSelection;
    input.imeComposing = composing;
    ::ReleaseSRWLockExclusive(&input.imeLock);
    input.imeRevision.fetch_add(1U, std::memory_order_release);
    input.composingInput.store(composing, std::memory_order_release);
}

void advancePresentationSpring(float& value, float& velocity,
                               const float target, const float delta) noexcept
{
    constexpr float stiffness = 70.0F;
    constexpr float damping = 12.5F;
    constexpr float halfDamping = damping * 0.5F;
    constexpr float dampedFrequency = std::sqrt(
        stiffness - halfDamping * halfDamping);
    const float displacement = value - target;
    const float secondary = (velocity + halfDamping * displacement) /
                            dampedFrequency;
    const float decay = std::exp(-halfDamping * delta);
    const float cosine = std::cos(dampedFrequency * delta);
    const float sine = std::sin(dampedFrequency * delta);
    const float evolved = displacement * cosine + secondary * sine;
    value = target + decay * evolved;
    velocity = decay *
        (-halfDamping * evolved - displacement * dampedFrequency * sine +
         secondary * dampedFrequency * cosine);
    value = std::clamp(value, -0.045F, 1.055F);
    if (std::abs(target - value) < 0.0005F && std::abs(velocity) < 0.005F) {
        value = target;
        velocity = 0.0F;
    }
}

struct ScreenPoint final {
    float x = 0.0F;
    float y = 0.0F;
    double clipX = 0.0;
    double clipY = 0.0;
    double clipW = 0.0;
    bool finiteClip = false;
    bool visible = false;
};

ScreenPoint projectPoint(const WorldCameraSnapshot& camera,
                         const ImVec2 displaySize,
                         const double x, const double y, const double z) noexcept
{
    const auto multiply = [](const std::array<float, 16U>& matrix,
                             const std::array<double, 4U>& input) noexcept {
        std::array<double, 4U> output{};
        for (std::size_t row = 0U; row < 4U; ++row) {
            output[row] = matrix[row] * input[0U] +
                          matrix[4U + row] * input[1U] +
                          matrix[8U + row] * input[2U] +
                          matrix[12U + row] * input[3U];
        }
        return output;
    };
    // Minecraft 1.8.9 renders world geometry relative to RenderManager's
    // renderPos*. ActiveRenderInfo's MODELVIEW/PROJECTION buffers contain the
    // real current 3D camera transform (yaw/pitch, FOV, hurt effect and view
    // bobbing), but intentionally do not contain the large world translation.
    const auto eye = multiply(camera.modelView,
                              {x - camera.renderX, y - camera.renderY,
                               z - camera.renderZ, 1.0});
    const auto clip = multiply(camera.projection, eye);
    ScreenPoint result;
    result.clipX = clip[0U];
    result.clipY = clip[1U];
    result.clipW = clip[3U];
    result.finiteClip = std::isfinite(clip[0U]) && std::isfinite(clip[1U]) &&
                        std::isfinite(clip[3U]);
    if (!result.finiteClip || clip[3U] <= 0.001) return result;
    const double ndcX = clip[0U] / clip[3U];
    const double ndcY = clip[1U] / clip[3U];
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) return {};
    const float viewportX = static_cast<float>(camera.viewport[0U]);
    const float viewportY = static_cast<float>(camera.viewport[1U]);
    const float viewportWidth = static_cast<float>(camera.viewport[2U]);
    const float viewportHeight = static_cast<float>(camera.viewport[3U]);
    result.x = viewportX + static_cast<float>((ndcX + 1.0) * 0.5) * viewportWidth;
    const float openGlY = viewportY + static_cast<float>((ndcY + 1.0) * 0.5) * viewportHeight;
    result.y = displaySize.y - openGlY;
    result.visible = result.x > -viewportWidth && result.x < displaySize.x + viewportWidth &&
                     result.y > -viewportHeight && result.y < displaySize.y + viewportHeight;
    return result;
}

bool clipProjectedLine(const ScreenPoint& firstPoint,
                       const ScreenPoint& secondPoint,
                       const WorldCameraSnapshot& camera,
                       ImVec2 displaySize,
                       ImVec2& first, ImVec2& second) noexcept;

void drawProjectedBox(ImDrawList* const drawList,
                      const WorldCameraSnapshot& camera,
                      const ImVec2 displaySize,
                      const AxisAlignedBox& box,
                      const ImU32 color,
                      const char* const label,
                      const bool filled = false) noexcept
{
    if (drawList == nullptr || !camera.valid) return;
    const std::array<std::array<double, 3U>, 8U> corners{{
        {{box.minX, box.minY, box.minZ}}, {{box.maxX, box.minY, box.minZ}},
        {{box.maxX, box.minY, box.maxZ}}, {{box.minX, box.minY, box.maxZ}},
        {{box.minX, box.maxY, box.minZ}}, {{box.maxX, box.maxY, box.minZ}},
        {{box.maxX, box.maxY, box.maxZ}}, {{box.minX, box.maxY, box.maxZ}}}};
    std::array<ScreenPoint, 8U> projected{};
    bool anyInFront = false;
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        projected[index] = projectPoint(camera, displaySize,
                                        corners[index][0U], corners[index][1U], corners[index][2U]);
        anyInFront = anyInFront || (projected[index].finiteClip &&
                                     projected[index].clipW > 0.001);
    }
    if (!anyInFront) return;
    if (filled) {
        constexpr std::array<std::array<std::uint8_t, 4U>, 6U> faces{{
            {{0, 1, 2, 3}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}},
            {{1, 2, 6, 5}}, {{2, 3, 7, 6}}, {{3, 0, 4, 7}}}};
        const ImU32 fillColor = (color & ~IM_COL32_A_MASK) |
                                (42U << IM_COL32_A_SHIFT);
        for (const auto& face : faces) {
            std::array<ImVec2, 4U> points{};
            bool complete = true;
            for (std::size_t point = 0U; point < face.size(); ++point) {
                const ScreenPoint& projectedPoint = projected[face[point]];
                if (!projectedPoint.visible) {
                    complete = false;
                    break;
                }
                points[point] = ImVec2(projectedPoint.x, projectedPoint.y);
            }
            if (complete) drawList->AddConvexPolyFilled(points.data(), 4, fillColor);
        }
    }
    constexpr std::array<std::array<std::uint8_t, 2U>, 12U> edges{{
        {{0,1}}, {{1,2}}, {{2,3}}, {{3,0}}, {{4,5}}, {{5,6}},
        {{6,7}}, {{7,4}}, {{0,4}}, {{1,5}}, {{2,6}}, {{3,7}}}};
    for (const auto& edge : edges) {
        const ScreenPoint& first = projected[edge[0U]];
        const ScreenPoint& second = projected[edge[1U]];
        ImVec2 clippedFirst{};
        ImVec2 clippedSecond{};
        if (clipProjectedLine(first, second, camera, displaySize,
                              clippedFirst, clippedSecond))
            drawList->AddLine(clippedFirst, clippedSecond, color, 1.8F);
    }
    if (label != nullptr && label[0] != '\0') {
        float left = displaySize.x;
        float top = displaySize.y;
        bool found = false;
        for (const ScreenPoint& point : projected) {
            if (!point.visible) continue;
            left = std::min(left, point.x);
            top = std::min(top, point.y);
            found = true;
        }
        if (found) {
            const ImVec2 size = ImGui::CalcTextSize(label);
            const ImVec2 start(left, std::max(2.0F, top - size.y - 5.0F));
            drawList->AddRectFilled(ImVec2(start.x - 4.0F, start.y - 2.0F),
                                    ImVec2(start.x + size.x + 4.0F, start.y + size.y + 2.0F),
                                    IM_COL32(18, 16, 22, 190), 4.0F);
            drawList->AddText(start, color, label);
        }
    }
}

ImU32 packedRgbColor(const std::uint32_t rgb, const int alpha = 255) noexcept
{
    return IM_COL32(static_cast<int>((rgb >> 16U) & 0xFFU),
                    static_cast<int>((rgb >> 8U) & 0xFFU),
                    static_cast<int>(rgb & 0xFFU), alpha);
}

ImU32 contrastingTextColor(const std::uint32_t rgb) noexcept
{
    const int red = static_cast<int>((rgb >> 16U) & 0xFFU);
    const int green = static_cast<int>((rgb >> 8U) & 0xFFU);
    const int blue = static_cast<int>(rgb & 0xFFU);
    const int luminance = red * 299 + green * 587 + blue * 114;
    return luminance >= 150000 ? IM_COL32(18, 18, 22, 255)
                               : IM_COL32(255, 255, 255, 255);
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

struct PrestigeStyle final {
    ImU32 color = IM_COL32(170, 170, 170, 255);
    bool master = false;
};

PrestigeStyle bedWarsPrestigeStyle(const int stars) noexcept
{
    // Hypixel's classic 100-star prestige cadence. At 1000+ the icon changes
    // to the master-prestige star; later tiers continue rotating the familiar
    // palette so the compact row remains readable without reproducing chat's
    // multi-glyph rainbow formatter.
    const int prestige = std::max(0, stars) / 100;
    constexpr std::array<ImU32, 10U> colors{{
        IM_COL32(170, 170, 170, 255), IM_COL32(245, 245, 245, 255),
        IM_COL32(255, 190, 58, 255),  IM_COL32(83, 220, 238, 255),
        IM_COL32(72, 204, 112, 255),  IM_COL32(48, 190, 190, 255),
        IM_COL32(236, 76, 86, 255),   IM_COL32(241, 110, 196, 255),
        IM_COL32(91, 137, 255, 255),  IM_COL32(177, 106, 235, 255)}};
    PrestigeStyle result;
    result.color = colors[static_cast<std::size_t>(prestige % 10)];
    result.master = prestige >= 10;
    return result;
}

bool clipProjectedLine(const ScreenPoint& firstPoint,
                       const ScreenPoint& secondPoint,
                       const WorldCameraSnapshot& camera,
                       const ImVec2 displaySize,
                       ImVec2& first, ImVec2& second) noexcept
{
    if (!firstPoint.finiteClip || !secondPoint.finiteClip) return false;
    constexpr double nearW = 0.001;
    double ax = firstPoint.clipX, ay = firstPoint.clipY, aw = firstPoint.clipW;
    double bx = secondPoint.clipX, by = secondPoint.clipY, bw = secondPoint.clipW;
    if (aw <= nearW && bw <= nearW) return false;
    const auto clipToNear = [](double& x, double& y, double& w,
                               const double otherX, const double otherY,
                               const double otherW) noexcept {
        if (w > nearW) return;
        const double denominator = otherW - w;
        if (std::abs(denominator) < 1.0e-12) return;
        const double amount = std::clamp((nearW - w) / denominator, 0.0, 1.0);
        x += (otherX - x) * amount;
        y += (otherY - y) * amount;
        w += (otherW - w) * amount;
    };
    clipToNear(ax, ay, aw, bx, by, bw);
    clipToNear(bx, by, bw, ax, ay, aw);
    if (aw <= 0.0 || bw <= 0.0) return false;
    const float viewportX = static_cast<float>(camera.viewport[0U]);
    const float viewportY = static_cast<float>(camera.viewport[1U]);
    const float viewportWidth = static_cast<float>(camera.viewport[2U]);
    const float viewportHeight = static_cast<float>(camera.viewport[3U]);
    const auto toScreen = [&](const double x, const double y,
                              const double w) noexcept {
        const float screenX = viewportX + static_cast<float>((x / w + 1.0) * 0.5) *
            viewportWidth;
        const float openGlY = viewportY + static_cast<float>((y / w + 1.0) * 0.5) *
            viewportHeight;
        return ImVec2(screenX, displaySize.y - openGlY);
    };
    first = toScreen(ax, ay, aw);
    second = toScreen(bx, by, bw);
    if (!std::isfinite(first.x) || !std::isfinite(first.y) ||
        !std::isfinite(second.x) || !std::isfinite(second.y)) return false;

    const float minX = 1.0F, minY = 1.0F;
    const float maxX = std::max(minX, displaySize.x - 1.0F);
    const float maxY = std::max(minY, displaySize.y - 1.0F);
    const float dx = second.x - first.x;
    const float dy = second.y - first.y;
    const std::array<float, 4U> p{{-dx, dx, -dy, dy}};
    const std::array<float, 4U> q{{first.x - minX, maxX - first.x,
                                  first.y - minY, maxY - first.y}};
    float enter = 0.0F, leave = 1.0F;
    for (std::size_t index = 0U; index < p.size(); ++index) {
        if (std::abs(p[index]) < 1.0e-7F) {
            if (q[index] < 0.0F) return false;
            continue;
        }
        const float ratio = q[index] / p[index];
        if (p[index] < 0.0F) enter = std::max(enter, ratio);
        else leave = std::min(leave, ratio);
        if (enter > leave) return false;
    }
    const ImVec2 originalFirst = first;
    first = ImVec2(originalFirst.x + dx * enter,
                   originalFirst.y + dy * enter);
    second = ImVec2(originalFirst.x + dx * leave,
                    originalFirst.y + dy * leave);
    return true;
}

bool projectedBoxBounds(const WorldCameraSnapshot& camera,
                        const ImVec2 displaySize,
                        const AxisAlignedBox& box,
                        ImVec2& minimum,
                        ImVec2& maximum) noexcept
{
    const std::array<std::array<double, 3U>, 8U> corners{{
        {{box.minX, box.minY, box.minZ}}, {{box.maxX, box.minY, box.minZ}},
        {{box.maxX, box.minY, box.maxZ}}, {{box.minX, box.minY, box.maxZ}},
        {{box.minX, box.maxY, box.minZ}}, {{box.maxX, box.maxY, box.minZ}},
        {{box.maxX, box.maxY, box.maxZ}}, {{box.minX, box.maxY, box.maxZ}}}};
    minimum = displaySize;
    maximum = ImVec2(0.0F, 0.0F);
    bool found = false;
    for (const auto& corner : corners) {
        const ScreenPoint point = projectPoint(
            camera, displaySize, corner[0U], corner[1U], corner[2U]);
        if (!point.visible) continue;
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        found = true;
    }
    return found;
}

const char* protectionRoman(const std::uint8_t level) noexcept
{
    constexpr std::array<const char*, 6U> labels{{"", "I", "II", "III", "IV", "V"}};
    return level < labels.size() ? labels[level] : "V+";
}

void drawPrestigeStar(ImDrawList* const drawList, const ImVec2 center,
                      const float radius, const ImU32 color,
                      const bool master) noexcept
{
    if (drawList == nullptr || radius <= 0.0F) return;
    std::array<ImVec2, 10U> points{};
    constexpr float pi = 3.14159265358979323846F;
    for (std::size_t index = 0U; index < points.size(); ++index) {
        const float angle = -pi * 0.5F + static_cast<float>(index) * pi / 5.0F;
        const float pointRadius = (index % 2U) == 0U ? radius : radius * 0.44F;
        points[index] = ImVec2(center.x + std::cos(angle) * pointRadius,
                               center.y + std::sin(angle) * pointRadius);
    }
    drawList->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
    if (master) {
        const ImU32 ring = IM_COL32(255, 255, 255, 220);
        drawList->AddPolyline(points.data(), static_cast<int>(points.size()), ring,
                              ImDrawFlags_Closed, std::max(1.0F, radius * 0.18F));
        drawList->AddCircle(center, radius * 1.20F, color, 20,
                            std::max(1.0F, radius * 0.14F));
    }
}

void formatCompactCount(char* const output, const std::size_t capacity,
                        const std::int64_t value) noexcept
{
    if (output == nullptr || capacity == 0U) return;
    if (value >= 1'000'000)
        std::snprintf(output, capacity, "%.1fM", static_cast<double>(value) / 1'000'000.0);
    else if (value >= 1'000)
        std::snprintf(output, capacity, "%.1fk", static_cast<double>(value) / 1'000.0);
    else
        std::snprintf(output, capacity, "%lld", static_cast<long long>(value));
}

void drawRoundedTriangle(ImDrawList* const drawList,
                         const ImVec2 a, const ImVec2 b, const ImVec2 c,
                         const float radius, const ImU32 color) noexcept
{
    if (drawList == nullptr) return;
    const auto toward = [](const ImVec2 from, const ImVec2 to,
                           const float distance) noexcept {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.001F) return from;
        const float amount = std::min(distance / length, 0.45F);
        return ImVec2(from.x + dx * amount, from.y + dy * amount);
    };
    const ImVec2 aToB = toward(a, b, radius);
    const ImVec2 bFromA = toward(b, a, radius);
    const ImVec2 bToC = toward(b, c, radius);
    const ImVec2 cFromB = toward(c, b, radius);
    const ImVec2 cToA = toward(c, a, radius);
    const ImVec2 aFromC = toward(a, c, radius);
    drawList->PathClear();
    drawList->PathLineTo(aToB);
    drawList->PathLineTo(bFromA);
    drawList->PathBezierQuadraticCurveTo(b, bToC);
    drawList->PathLineTo(cFromB);
    drawList->PathBezierQuadraticCurveTo(c, cToA);
    drawList->PathLineTo(aFromC);
    drawList->PathBezierQuadraticCurveTo(a, aToB);
    drawList->PathFillConvex(color);
}

bool isMouseMessage(const UINT message) noexcept
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           message == WM_NCMOUSEMOVE || message == WM_NCLBUTTONDOWN ||
           message == WM_NCLBUTTONUP || message == WM_NCRBUTTONDOWN ||
           message == WM_NCRBUTTONUP || message == WM_SETCURSOR ||
           message == WM_INPUT;
}

bool isKeyboardMessage(const UINT message) noexcept
{
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           message == WM_CHAR || message == WM_SYSCHAR;
}

bool animatedToggle(const char* const label, bool& value, float& animation,
                    const float uiScale) noexcept
{
    ImGui::PushID(label);
    const float height = 22.0F * uiScale;
    const float width = 42.0F * uiScale;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##toggle", ImVec2(width, height));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        value = !value;
        changed = true;
    }
    const float target = value ? 1.0F : 0.0F;
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0F, 0.05F);
    animation += (target - animation) * (1.0F - std::exp(-14.0F * dt));
    const float eased = animation * animation * (3.0F - 2.0F * animation);
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec4 offColor = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 onColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const bool lightSurface = textColor.x + textColor.y + textColor.z < 1.5F;
    const ImVec4 mixedColor(
        offColor.x + (onColor.x - offColor.x) * eased,
        offColor.y + (onColor.y - offColor.y) * eased,
        offColor.z + (onColor.z - offColor.z) * eased, 1.0F);
    const ImU32 track = ImGui::GetColorU32(mixedColor);
    draw->AddRectFilled(start, ImVec2(start.x + width, start.y + height),
                        track, height * 0.5F);
    if (!value && animation < 0.5F) {
        const ImVec4 outline = lightSurface
            ? ImVec4(0.47F, 0.44F, 0.50F, 0.72F)
            : ImVec4(0.70F, 0.66F, 0.76F, 0.55F);
        draw->AddRect(start, ImVec2(start.x + width, start.y + height),
                      ImGui::GetColorU32(outline), height * 0.5F, 0,
                      1.0F * uiScale);
    }
    const float knobX = start.x + 11.0F * uiScale +
                        eased * (width - 22.0F * uiScale);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        draw->AddCircleFilled(ImVec2(knobX, start.y + height * 0.5F),
                              10.0F * uiScale,
                              ImGui::GetColorU32(ImVec4(
                                  onColor.x, onColor.y, onColor.z, 0.15F)));
    }
    const ImVec4 offKnob = lightSurface
        ? ImVec4(0.36F, 0.34F, 0.39F, 1.0F)
        : ImVec4(0.76F, 0.72F, 0.80F, 1.0F);
    const ImVec4 onKnob = ImVec4(0.99F, 0.985F, 1.0F, 1.0F);
    const ImVec4 knobColor(
        offKnob.x + (onKnob.x - offKnob.x) * eased,
        offKnob.y + (onKnob.y - offKnob.y) * eased,
        offKnob.z + (onKnob.z - offKnob.z) * eased, 1.0F);
    draw->AddCircleFilled(ImVec2(knobX, start.y + height * 0.5F),
                          (hovered ? 8.4F : 8.0F) * uiScale,
                          ImGui::GetColorU32(knobColor));
    ImGui::SameLine(0.0F, 12.0F * uiScale);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

const char* hypixelStateText(const HypixelOverlaySnapshot::State state) noexcept
{
    switch (state) {
    case HypixelOverlaySnapshot::State::Idle: return "Idle";
    case HypixelOverlaySnapshot::State::Loading: return "Loading";
    case HypixelOverlaySnapshot::State::Ready: return "Ready";
    case HypixelOverlaySnapshot::State::Error: return "Error";
    }
    return "Unknown";
}

float guiScaleForIndex(const int index) noexcept
{
    constexpr std::array<float, 4U> scales{1.0F, 1.25F, 1.5F, 1.75F};
    return scales[static_cast<std::size_t>(std::clamp(index, 0, 3))];
}

const char* hotkeyName(const unsigned virtualKey) noexcept
{
    static thread_local std::array<char, 64U> name{};
    switch (virtualKey) {
    case 0U: return "None";
    case VK_OEM_7: return "Apostrophe";
    case VK_INSERT: return "Insert";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_F8: return "F8";
    case VK_F9: return "F9";
    case VK_F10: return "F10";
    case VK_F11: return "F11";
    case VK_F12: return "F12";
    case VK_MEDIA_PREV_TRACK: return "Media Previous";
    case VK_MEDIA_PLAY_PAUSE: return "Media Play / Pause";
    case VK_MEDIA_NEXT_TRACK: return "Media Next";
    default: break;
    }
    name.fill('\0');
    UINT scan = ::MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == VK_UP ||
        virtualKey == VK_DOWN || virtualKey == VK_PRIOR || virtualKey == VK_NEXT ||
        virtualKey == VK_END || virtualKey == VK_HOME || virtualKey == VK_INSERT ||
        virtualKey == VK_DELETE || virtualKey == VK_DIVIDE || virtualKey == VK_NUMLOCK) {
        scan |= 0x100U;
    }
    const LONG parameter = static_cast<LONG>(scan << 16U);
    if (::GetKeyNameTextA(parameter, name.data(),
                          static_cast<int>(name.size())) > 0) return name.data();
    std::snprintf(name.data(), name.size(), "VK 0x%02X", virtualKey);
    return name.data();
}

struct KenneyPromptTile final {
    ImVec2 uv0{};
    ImVec2 uv1{};
    int width = 0;
    int height = 0;
    bool valid = false;
};

const char* kenneyPromptName(const unsigned virtualKey) noexcept
{
    static thread_local std::array<char, 40U> name{};
    name.fill('\0');
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        std::snprintf(name.data(), name.size(), "keyboard_%c",
                      static_cast<char>(std::tolower(static_cast<int>(virtualKey))));
        return name.data();
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        std::snprintf(name.data(), name.size(), "keyboard_%c",
                      static_cast<char>(virtualKey));
        return name.data();
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F12) {
        std::snprintf(name.data(), name.size(), "keyboard_f%u",
                      virtualKey - VK_F1 + 1U);
        return name.data();
    }
    switch (virtualKey) {
    case VK_BACK: return "keyboard_backspace";
    case VK_TAB: return "keyboard_tab";
    case VK_RETURN: return "keyboard_enter";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "keyboard_shift";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "keyboard_ctrl";
    case VK_MENU: case VK_LMENU: case VK_RMENU: return "keyboard_alt";
    case VK_PAUSE: return "keyboard_pause";
    case VK_CAPITAL: return "keyboard_capslock";
    case VK_ESCAPE: return "keyboard_escape";
    case VK_SPACE: return "keyboard_space";
    case VK_PRIOR: return "keyboard_page_up";
    case VK_NEXT: return "keyboard_page_down";
    case VK_END: return "keyboard_end";
    case VK_HOME: return "keyboard_home";
    case VK_LEFT: return "keyboard_arrow_left";
    case VK_UP: return "keyboard_arrow_up";
    case VK_RIGHT: return "keyboard_arrow_right";
    case VK_DOWN: return "keyboard_arrow_down";
    case VK_INSERT: return "keyboard_insert";
    case VK_DELETE: return "keyboard_delete";
    case VK_NUMLOCK: return "keyboard_numlock";
    case VK_OEM_1: return "keyboard_semicolon";
    case VK_OEM_PLUS: return "keyboard_plus";
    case VK_OEM_COMMA: return "keyboard_comma";
    case VK_OEM_MINUS: return "keyboard_minus";
    case VK_OEM_PERIOD: return "keyboard_period";
    case VK_OEM_7: return "keyboard_apostrophe";
    case VK_LBUTTON: return "mouse_left";
    case VK_RBUTTON: return "mouse_right";
    case VK_MBUTTON: return "mouse_scroll";
    default: return nullptr;
    }
}

KenneyPromptTile kenneyPromptTile(const char* const xmlData,
                                  const std::size_t xmlSize,
                                  const unsigned virtualKey) noexcept
{
    const char* const sprite = kenneyPromptName(virtualKey);
    if (!xmlData || xmlSize == 0U || !sprite) return {};
    const std::string_view xml(xmlData, xmlSize);
    std::array<char, 72U> token{};
    std::snprintf(token.data(), token.size(), "name=\"%s\"", sprite);
    const std::size_t start = xml.find(token.data());
    if (start == std::string_view::npos) return {};
    const std::size_t end = xml.find("/>", start);
    if (end == std::string_view::npos) return {};
    const auto attribute = [&](const char* key, int& value) noexcept {
        std::array<char, 20U> marker{};
        std::snprintf(marker.data(), marker.size(), " %s=\"", key);
        const std::size_t position = xml.find(marker.data(), start);
        if (position == std::string_view::npos || position >= end) return false;
        std::size_t digit = position + std::strlen(marker.data());
        if (digit >= end || xml[digit] < '0' || xml[digit] > '9') return false;
        value = 0;
        while (digit < end && xml[digit] >= '0' && xml[digit] <= '9') {
            value = value * 10 + (xml[digit] - '0');
            ++digit;
        }
        return true;
    };
    int x=0,y=0,width=0,height=0;
    if (!attribute("x",x) || !attribute("y",y) ||
        !attribute("width",width) || !attribute("height",height) ||
        width <= 0 || height <= 0) return {};
    constexpr float atlasWidth = 1088.0F;
    constexpr float atlasHeight = 1024.0F;
    const float openGlY=atlasHeight-static_cast<float>(y+height);
    return {ImVec2(static_cast<float>(x)/atlasWidth,
                   openGlY/atlasHeight),
            ImVec2(static_cast<float>(x+width)/atlasWidth,
                   (openGlY+static_cast<float>(height))/atlasHeight),
            width,height,true};
}

bool rawModuleResource(HMODULE module, const int identifier,
                       const unsigned char*& data,
                       std::size_t& size) noexcept
{
    data = nullptr;
    size = 0U;
    if (!module) return false;
    const HRSRC resource = ::FindResourceW(module, MAKEINTRESOURCEW(identifier),
                                           RT_RCDATA);
    if (!resource) return false;
    const HGLOBAL loaded = ::LoadResource(module, resource);
    if (!loaded) return false;
    const DWORD bytes = ::SizeofResource(module, resource);
    const void* const pointer = ::LockResource(loaded);
    if (!pointer || bytes == 0U) return false;
    data = static_cast<const unsigned char*>(pointer);
    size = static_cast<std::size_t>(bytes);
    return true;
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

ImVec4 teamColor(const char code) noexcept
{
    switch (code) {
    case '0': return ImVec4(0.12F, 0.12F, 0.14F, 1.0F);
    case '1': return ImVec4(0.20F, 0.28F, 0.75F, 1.0F);
    case '2': return ImVec4(0.18F, 0.68F, 0.32F, 1.0F);
    case '3': return ImVec4(0.16F, 0.70F, 0.72F, 1.0F);
    case '4': return ImVec4(0.72F, 0.22F, 0.25F, 1.0F);
    case '5': return ImVec4(0.58F, 0.28F, 0.76F, 1.0F);
    case '6': return ImVec4(0.95F, 0.63F, 0.18F, 1.0F);
    case '7': return ImVec4(0.68F, 0.68F, 0.72F, 1.0F);
    case '8': return ImVec4(0.34F, 0.34F, 0.38F, 1.0F);
    case '9': return ImVec4(0.38F, 0.52F, 1.0F, 1.0F);
    case 'a': return ImVec4(0.42F, 0.92F, 0.48F, 1.0F);
    case 'b': return ImVec4(0.38F, 0.90F, 0.95F, 1.0F);
    case 'c': return ImVec4(1.0F, 0.38F, 0.42F, 1.0F);
    case 'd': return ImVec4(0.92F, 0.48F, 0.95F, 1.0F);
    case 'e': return ImVec4(1.0F, 0.90F, 0.34F, 1.0F);
    default: return ImVec4(0.92F, 0.92F, 0.95F, 1.0F);
    }
}

ImU32 defenseBlockColor(const std::uint16_t blockId,
                        const std::uint8_t metadata) noexcept
{
    if (blockId == 35U || blockId == 95U || blockId == 159U) {
        constexpr std::array<ImU32, 16U> dyeColors{
            IM_COL32(225, 225, 225, 255), IM_COL32(216, 122, 45, 255),
            IM_COL32(178, 80, 188, 255), IM_COL32(102, 145, 205, 255),
            IM_COL32(198, 184, 54, 255), IM_COL32(89, 167, 53, 255),
            IM_COL32(217, 132, 153, 255), IM_COL32(66, 66, 66, 255),
            IM_COL32(152, 152, 152, 255), IM_COL32(44, 119, 146, 255),
            IM_COL32(127, 63, 178, 255), IM_COL32(46, 64, 154, 255),
            IM_COL32(105, 66, 40, 255), IM_COL32(72, 118, 42, 255),
            IM_COL32(154, 52, 48, 255), IM_COL32(28, 28, 33, 255)};
        return dyeColors[metadata & 0xFU];
    }
    switch (blockId) {
    case 1U: return IM_COL32(125, 127, 130, 255);
    case 4U: return IM_COL32(105, 107, 110, 255);
    case 5U: return IM_COL32(167, 125, 72, 255);
    case 17U: return IM_COL32(118, 86, 49, 255);
    case 20U: return IM_COL32(154, 210, 218, 220);
    case 24U: return IM_COL32(214, 199, 139, 255);
    case 45U: return IM_COL32(151, 75, 67, 255);
    case 49U: return IM_COL32(37, 25, 50, 255);
    case 121U: return IM_COL32(218, 221, 143, 255);
    default: return IM_COL32(142, 137, 151, 255);
    }
}

void drawInventoryBlockIcon(ImDrawList* const draw, const ImVec2 center,
                            const float size, const std::uint16_t blockId,
                            const std::uint8_t metadata,
                            const unsigned* textures) noexcept
{
    if (draw == nullptr) return;
    
    // Map block ID to index in textures array
    int texIndex = -1;
    switch (blockId) {
    case 35: texIndex = 0; break; // Wool
    case 5:  texIndex = 1; break; // Planks
    case 159: texIndex = 2; break; // Terracotta / Hardened clay
    case 20: 
    case 95: texIndex = 3; break; // Glass / Stained Glass
    case 121: texIndex = 4; break; // End Stone
    case 49: texIndex = 5; break; // Obsidian
    }

    if (texIndex >= 0 && textures[texIndex] != 0U) {
        // Draw 2D PNG icon
        const float half = size * 0.5F;
        draw->AddImage(
            reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(textures[texIndex])),
            ImVec2(center.x - half, center.y - half),
            ImVec2(center.x + half, center.y + half)
        );
        return;
    }

    // Fallback to 3D solid color cube if texture is missing or unknown block
    const ImU32 base = defenseBlockColor(blockId, metadata);
    const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(base);
    const auto shaded = [&](const float multiplier) noexcept {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            std::clamp(rgba.x * multiplier, 0.0F, 1.0F),
            std::clamp(rgba.y * multiplier, 0.0F, 1.0F),
            std::clamp(rgba.z * multiplier, 0.0F, 1.0F), rgba.w));
    };
    const float half = size * 0.5F;
    const float quarter = size * 0.24F;
    const ImVec2 top[4]{
        {center.x, center.y - half}, {center.x + half, center.y - quarter},
        {center.x, center.y}, {center.x - half, center.y - quarter}};
    const ImVec2 left[4]{
        {center.x - half, center.y - quarter}, {center.x, center.y},
        {center.x, center.y + half}, {center.x - half, center.y + quarter}};
    const ImVec2 right[4]{
        {center.x, center.y}, {center.x + half, center.y - quarter},
        {center.x + half, center.y + quarter}, {center.x, center.y + half}};
    draw->AddConvexPolyFilled(top, 4, shaded(1.0F));
    draw->AddConvexPolyFilled(left, 4, shaded(0.7F));
    draw->AddConvexPolyFilled(right, 4, shaded(0.5F));
    draw->AddPolyline(top, 4, IM_COL32(255, 255, 255, 65), ImDrawFlags_Closed, 1.0F);
}

} // namespace

OverlayRenderer::OverlayRenderer()
    : m_inputState(new (std::nothrow) OverlayInputState)
{
}

OverlayRenderer::~OverlayRenderer()
{
    abandonAfterHookDisabled();
    delete m_inputState;
    m_inputState = nullptr;
}

bool OverlayRenderer::consumeClickGuiToggle() noexcept
{
    pollFallbackInput();
    return m_inputState != nullptr &&
           m_inputState->clickGuiToggle.exchange(false, std::memory_order_acq_rel);
}

void OverlayRenderer::setFeatureSettings(const FeatureSettings& settings) noexcept
{
    // Do not overwrite a just-clicked ImGui value before AgentRuntime has
    // consumed it and published the corresponding atomic bitset.
    if (!m_featureSettingsDirty &&
        !m_statsPanelDragging && !m_statsPanelResizing) {
        // The IME editor holds an uncommitted preview until Done is clicked.
        // Merge unrelated settings, but never replace its local coordinates
        // with the previous runtime snapshot between mouse-up and Save.
        FeatureSettings incoming = settings;
        // Retired experimental modules remain wire-compatible for older
        // controllers but are neither presented nor allowed to run.
        incoming.longJumpEnabled = false;
        incoming.localMobAuraEnabled = false;
        if (m_imePositionEditing) {
            incoming.imePanelX = m_features.imePanelX;
            incoming.imePanelY = m_features.imePanelY;
        }
        if (m_featureSnapshotInitialized && m_features != incoming) {
            enqueueFeatureToasts(m_features, incoming);
        }
        m_features = incoming;
        m_featureSnapshotInitialized = true;
    }
}

bool OverlayRenderer::consumeFeatureSettings(FeatureSettings& settings) noexcept
{
    if (!m_featureSettingsDirty) return false;
    settings = m_features;
    m_featureSettingsDirty = false;
    return true;
}

void OverlayRenderer::setHypixelSnapshot(const HypixelOverlaySnapshot& snapshot) noexcept
{
    m_hypixel = snapshot;
}

void OverlayRenderer::setPlayerStatsSnapshot(
    const PlayerStatsOverlaySnapshot& snapshot) noexcept
{
    m_playerStats = snapshot;
}

void OverlayRenderer::setBlacklistSnapshot(
    const BlacklistOverlaySnapshot& snapshot) noexcept
{
    // Layout changes created by an active drag are first returned to the
    // Controller. Do not let an older pipe snapshot pull the panel backward
    // before that acknowledgement arrives.
    const auto now = ::GetTickCount64();
    if (snapshot.panelX == m_blacklist.panelX && snapshot.panelY == m_blacklist.panelY &&
        snapshot.panelWidth == m_blacklist.panelWidth && snapshot.panelHeight == m_blacklist.panelHeight)
        m_blacklistLayoutPendingUntil = 0U;
    if (snapshot.panelEnabled == m_blacklist.panelEnabled &&
        snapshot.matchAlertsEnabled == m_blacklist.matchAlertsEnabled &&
        snapshot.allowIdOnlyNicks == m_blacklist.allowIdOnlyNicks &&
        snapshot.showWithClickGui == m_blacklist.showWithClickGui &&
        snapshot.collapsed == m_blacklist.collapsed &&
        snapshot.panelOpacity == m_blacklist.panelOpacity &&
        snapshot.contentScale == m_blacklist.contentScale &&
        snapshot.panelColor == m_blacklist.panelColor)
        m_blacklistSettingsPendingUntil = 0U;
    // Continue accepting new entries/avatars during a drag. Only the locally
    // edited fields wait for an IPC echo, bounded in case the pipe disconnects.
    // Copy only editable metadata, not the 128 avatar/record payloads.
    const int localX = m_blacklist.panelX, localY = m_blacklist.panelY;
    const int localWidth = m_blacklist.panelWidth, localHeight = m_blacklist.panelHeight;
    const bool localEnabled = m_blacklist.panelEnabled, localAlerts = m_blacklist.matchAlertsEnabled;
    const bool localNicks = m_blacklist.allowIdOnlyNicks, localGui = m_blacklist.showWithClickGui;
    const bool localCollapsed = m_blacklist.collapsed;
    const int localOpacity = m_blacklist.panelOpacity;
    const int localContentScale = m_blacklist.contentScale;
    const auto localColor = m_blacklist.panelColor;
    m_blacklist = snapshot;
    if (m_blacklistPanelDragging || m_blacklistPanelResizing || m_blacklistPanelTransformDirty ||
        m_blacklistLayoutPendingUntil > now ||
        (m_blacklistActionDirty && m_blacklistAction.type == BlacklistAction::Type::Layout)) {
        m_blacklist.panelX = localX;
        m_blacklist.panelY = localY;
        m_blacklist.panelWidth = localWidth;
        m_blacklist.panelHeight = localHeight;
    }
    if (m_blacklistSettingsPendingUntil > now ||
        (m_blacklistActionDirty && m_blacklistAction.type == BlacklistAction::Type::Settings)) {
        m_blacklist.panelEnabled = localEnabled;
        m_blacklist.matchAlertsEnabled = localAlerts;
        m_blacklist.allowIdOnlyNicks = localNicks;
        m_blacklist.showWithClickGui = localGui;
        m_blacklist.collapsed = localCollapsed;
        m_blacklist.panelOpacity = localOpacity;
        m_blacklist.contentScale = localContentScale;
        m_blacklist.panelColor = localColor;
    }
}

void OverlayRenderer::setMediaSnapshot(
    const MediaPlaybackSnapshot& snapshot) noexcept
{
    m_media = snapshot;
}

void OverlayRenderer::setMediaSettings(
    const MediaOverlaySettings& settings) noexcept
{
    // A drag or an in-GUI edit is first sent back to the Controller. Keep the
    // local value until that message has been consumed so a 500 ms media-state
    // refresh cannot snap the island back to its previous layout.
    if (!m_mediaSettingsDirty && !m_mediaDragging) {
        m_mediaSettings = settings;
    }
    if(m_inputState) {
        m_inputState->mediaPreviousHotkey.store(settings.previousHotkey,
                                                std::memory_order_release);
        m_inputState->mediaToggleHotkey.store(settings.toggleHotkey,
                                              std::memory_order_release);
        m_inputState->mediaNextHotkey.store(settings.nextHotkey,
                                            std::memory_order_release);
    }
}

bool OverlayRenderer::consumeMediaSettings(
    MediaOverlaySettings& settings) noexcept
{
    if (!m_mediaSettingsDirty) return false;
    settings = m_mediaSettings;
    m_mediaSettingsDirty = false;
    return true;
}

MediaAction OverlayRenderer::consumeMediaAction() noexcept
{
    return std::exchange(m_mediaAction, MediaAction::None);
}

bool OverlayRenderer::consumeBlacklistAction(BlacklistAction& action) noexcept
{
    if (!m_blacklistActionDirty) return false;
    action = m_blacklistAction;
    if (action.type == BlacklistAction::Type::Layout)
        m_blacklistLayoutPendingUntil = ::GetTickCount64() + 5000U;
    if (action.type == BlacklistAction::Type::Settings)
        m_blacklistSettingsPendingUntil = ::GetTickCount64() + 5000U;
    m_blacklistAction = {};
    m_blacklistActionDirty = false;
    return true;
}

bool OverlayRenderer::consumeHypixelQuery(std::array<char, 17U>& playerId) noexcept
{
    if (!m_hypixelQueryPending) return false;
    playerId = m_hypixelQuery;
    m_hypixelQueryPending = false;
    return true;
}

void OverlayRenderer::setMenuHotkey(const unsigned virtualKey) noexcept
{
    if ((virtualKey != 0U && virtualKey < 8U) || virtualKey > 254U) return;
    if (m_menuHotkey == virtualKey && m_inputState != nullptr &&
        m_inputState->menuHotkey.load(std::memory_order_acquire) == virtualKey) {
        return;
    }
    m_menuHotkey = virtualKey;
    if (m_inputState != nullptr) {
        m_inputState->menuHotkey.store(virtualKey, std::memory_order_release);
        m_inputState->fallbackPrimed = false;
    }
}

bool OverlayRenderer::consumeMenuHotkeyChange(unsigned& virtualKey) noexcept
{
    if (!m_menuHotkeyDirty) return false;
    virtualKey = m_menuHotkey;
    m_menuHotkeyDirty = false;
    return true;
}

void OverlayRenderer::setGuiScaleIndex(const int index) noexcept
{
    if (m_guiScaleDirty) return;
    m_guiScaleIndex = std::clamp(index, 0, 3);
}

bool OverlayRenderer::consumeGuiScaleChange(int& index) noexcept
{
    if (!m_guiScaleDirty) return false;
    index = std::clamp(m_guiScaleIndex, 0, 3);
    m_guiScaleDirty = false;
    return true;
}

bool OverlayRenderer::consumeBedRescanRequest() noexcept
{
    return std::exchange(m_bedRescanPending, false);
}

void OverlayRenderer::pollFallbackInput() noexcept
{
    OverlayInputState* const input = m_inputState;
    if (input == nullptr ||
        input->directImGuiWndProc.load(std::memory_order_acquire) ||
        !input->acceptImGuiMessages.load(std::memory_order_acquire)) {
        return;
    }

    const HWND window = input->window.load(std::memory_order_acquire);
    if (window == nullptr || ::GetForegroundWindow() != window) {
        ImGuiContext* const context = input->imguiContext.load(std::memory_order_acquire);
        if (context != nullptr) {
            ImGui::SetCurrentContext(context);
            ImGuiIO& io = ImGui::GetIO();
            for (int button = 0; button < 5; ++button) {
                if (input->mouseDown[button]) {
                    io.AddMouseButtonEvent(button, false);
                    input->mouseDown[button] = false;
                }
            }
        }
        input->fallbackPrimed = false;
        input->captureKeysPrimed = false;
        return;
    }

    const unsigned menuHotkey = input->menuHotkey.load(std::memory_order_acquire);
    const bool menuKeyDown = (::GetAsyncKeyState(static_cast<int>(menuHotkey)) & 0x8000) != 0;
    const bool escapeKeyDown = (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const bool mouseDown[5] = {
        (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0,
    };

    if (!input->fallbackPrimed) {
        // Prime from the current physical state so attaching while a hotkey is
        // already held does not synthesize a toggle edge.
        input->fallbackPrimed = true;
        input->menuKeyDown = menuKeyDown;
        input->escapeKeyDown = escapeKeyDown;
        ImGuiContext* const context = input->imguiContext.load(std::memory_order_acquire);
        if (context != nullptr) {
            ImGui::SetCurrentContext(context);
            ImGuiIO& io = ImGui::GetIO();
            POINT cursor{};
            if (::GetCursorPos(&cursor) != FALSE &&
                ::ScreenToClient(window, &cursor) != FALSE) {
                io.AddMousePosEvent(static_cast<float>(cursor.x),
                                    static_cast<float>(cursor.y));
            }
            for (int button = 0; button < 5; ++button) {
                io.AddMouseButtonEvent(button, mouseDown[button]);
            }
        }
        for (int button = 0; button < 5; ++button) {
            input->mouseDown[button] = mouseDown[button];
        }
        return;
    }

    bool capturedThisFrame = false;
    if (input->captureHotkey.load(std::memory_order_acquire)) {
        if (!input->captureKeysPrimed) {
            for (unsigned key = 8U; key <= 254U; ++key) {
                input->keyDown[key] =
                    (::GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
            }
            input->captureKeysPrimed = true;
        } else {
            for (unsigned key = 8U; key <= 254U; ++key) {
                const bool down =
                    (::GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
                const bool rising = down && !input->keyDown[key];
                input->keyDown[key] = down;
                if (!rising || key == VK_LBUTTON || key == VK_RBUTTON ||
                    key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2) {
                    continue;
                }
                input->capturedHotkey.store(key, std::memory_order_release);
                input->captureHotkey.store(false, std::memory_order_release);
                input->captureKeysPrimed = false;
                capturedThisFrame = true;
                break;
            }
        }
    } else {
        input->captureKeysPrimed = false;
    }
    if (!capturedThisFrame) {
        if (menuKeyDown && !input->menuKeyDown &&
            !input->composingInput.load(std::memory_order_acquire) &&
            (!input->gameScreenOpen.load(std::memory_order_acquire) ||
             input->interactive.load(std::memory_order_acquire))) {
            input->clickGuiToggle.store(true, std::memory_order_release);
        }
        if (escapeKeyDown && !input->escapeKeyDown &&
            input->interactive.load(std::memory_order_acquire)) {
            input->clickGuiToggle.store(true, std::memory_order_release);
        }
    }
    input->menuKeyDown = menuKeyDown;
    input->escapeKeyDown = escapeKeyDown;

    ImGuiContext* const context = input->imguiContext.load(std::memory_order_acquire);
    if (context != nullptr) {
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO();
        POINT cursor{};
        if (::GetCursorPos(&cursor) != FALSE && ::ScreenToClient(window, &cursor) != FALSE) {
            io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
        }
        for (int button = 0; button < 5; ++button) {
            if (input->mouseDown[button] != mouseDown[button]) {
                io.AddMouseButtonEvent(button, mouseDown[button]);
            }
        }
    }
    for (int button = 0; button < 5; ++button) {
        input->mouseDown[button] = mouseDown[button];
    }
}

void OverlayRenderer::setGameScreenOpen(const bool open) noexcept
{
    if (m_inputState != nullptr)
        m_inputState->gameScreenOpen.store(open, std::memory_order_release);
}

bool OverlayRenderer::ownsCurrentContext() const noexcept
{
    return m_initialized && m_glContext != nullptr &&
           m_glContext == ::wglGetCurrentContext();
}

void OverlayRenderer::applyGuiScaleStyle(const float scale, const int fontIndex) noexcept
{
    ImGuiStyle fresh{};
    ImGui::StyleColorsDark(&fresh);
    fresh.WindowRounding = 16.0F * scale;
    fresh.ChildRounding = 12.0F * scale;
    fresh.FrameRounding = 9.0F * scale;
    fresh.PopupRounding = 12.0F * scale;
    fresh.WindowPadding = ImVec2(17.0F * scale, 15.0F * scale);
    fresh.ItemSpacing = ImVec2(10.0F * scale, 8.0F * scale);
    fresh.ItemInnerSpacing = ImVec2(7.0F * scale, 5.0F * scale);
    fresh.WindowBorderSize = 0.0F;
    fresh.ChildBorderSize = 0.0F;
    fresh.PopupBorderSize = 0.0F;
    fresh.FrameBorderSize = 0.0F;
    fresh.ScrollbarRounding = 12.0F * scale;
    fresh.GrabRounding = 9.0F * scale;
    fresh.Colors[ImGuiCol_WindowBg] = ImVec4(0.055F, 0.050F, 0.072F, 0.88F);
    fresh.Colors[ImGuiCol_ChildBg] = ImVec4(0.090F, 0.082F, 0.112F, 0.82F);
    fresh.Colors[ImGuiCol_PopupBg] = ImVec4(0.075F, 0.068F, 0.095F, 0.96F);
    fresh.Colors[ImGuiCol_Header] = ImVec4(0.39F, 0.31F, 0.68F, 0.85F);
    fresh.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.50F, 0.39F, 0.82F, 0.90F);
    fresh.Colors[ImGuiCol_Button] = ImVec4(0.42F, 0.33F, 0.75F, 0.92F);
    fresh.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.52F, 0.41F, 0.88F, 1.0F);
    fresh.Colors[ImGuiCol_ButtonActive] = ImVec4(0.61F, 0.48F, 0.96F, 1.0F);
    fresh.Colors[ImGuiCol_FrameBg] = ImVec4(0.15F, 0.14F, 0.19F, 0.90F);
    fresh.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21F, 0.19F, 0.28F, 0.96F);
    fresh.Colors[ImGuiCol_Separator] = ImVec4(0.40F, 0.36F, 0.48F, 0.36F);
    ImGui::GetStyle() = fresh;
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 1.0F;
    const int selectedFont = std::clamp(fontIndex, 0, 3);
    if (m_fonts[static_cast<std::size_t>(selectedFont)] != nullptr) {
        io.FontDefault = m_fonts[static_cast<std::size_t>(selectedFont)];
    }
    m_appliedGuiScaleIndex = selectedFont;
}

void OverlayRenderer::enqueueToast(const char* const label, const bool enabled) noexcept
{
    if (label == nullptr || label[0] == '\0') return;
    char message[96]{};
    std::snprintf(message, sizeof(message), "%s %s", label,
                  enabled ? "enabled" : "disabled");
    enqueueMessage(message, enabled);
}

void OverlayRenderer::enqueueMessage(const char* const message,
                                     const bool positive) noexcept
{
    if (message == nullptr || message[0] == '\0') return;
    Toast* selected = nullptr;
    for (Toast& toast : m_toasts) {
        if (!toast.active) { selected = &toast; break; }
        if (selected == nullptr || toast.sequence < selected->sequence) selected = &toast;
    }
    if (selected == nullptr) return;
    *selected = {};
    std::snprintf(selected->label.data(), selected->label.size(), "%s", message);
    selected->enabled = positive;
    selected->active = true;
    selected->sequence = ++m_toastSequence;
}

void OverlayRenderer::enqueueFeatureToasts(const FeatureSettings& before,
                                           const FeatureSettings& after) noexcept
{
    if (before.espEnabled != after.espEnabled)
        enqueueToast("ESP master", after.espEnabled);
    if (before.entityEspEnabled != after.entityEspEnabled)
        enqueueToast("Living hitboxes", after.entityEspEnabled);
    if (before.bedEspEnabled != after.bedEspEnabled)
        enqueueToast("Bed ESP", after.bedEspEnabled);
    if (before.labelsEnabled != after.labelsEnabled)
        enqueueToast("World labels", after.labelsEnabled);
    if (before.hypixelPanelEnabled != after.hypixelPanelEnabled)
        enqueueToast("Player statistics", after.hypixelPanelEnabled);
    if (before.bedThreatAlertsEnabled != after.bedThreatAlertsEnabled)
        enqueueToast("Bed threat alerts", after.bedThreatAlertsEnabled);
    if (before.bedDefensePanelEnabled != after.bedDefensePanelEnabled)
        enqueueToast("Bed defense panel", after.bedDefensePanelEnabled);
    const auto movementWarning = [&](const char* feature, const bool wasEnabled,
                                     const bool enabled) noexcept {
        if (wasEnabled == enabled) return;
        if (enabled) {
            char warning[96]{};
            std::snprintf(warning, sizeof(warning),
                "WARNING: %s can cause a server ban. Use only offline.", feature);
            enqueueMessage(warning, false);
        } else enqueueToast(feature, false);
    };
    movementWarning("Scaffold", before.scaffoldEnabled, after.scaffoldEnabled);
    movementWarning("Fly", before.flyEnabled, after.flyEnabled);
    movementWarning("BHop", before.bhopEnabled, after.bhopEnabled);
    if (before.safewalkEnabled != after.safewalkEnabled)
        enqueueToast("Safewalk", after.safewalkEnabled);
    if (before.aimAssistEnabled != after.aimAssistEnabled)
        enqueueToast("Aim Assist", after.aimAssistEnabled);
    if (before.bedBreakerEnabled != after.bedBreakerEnabled)
        enqueueToast("Bed Breaker", after.bedBreakerEnabled);
    if (before.fireballEspEnabled != after.fireballEspEnabled)
        enqueueToast("Fireball ESP", after.fireballEspEnabled);
    if (before.textGuiEnabled != after.textGuiEnabled)
        enqueueToast("Text GUI", after.textGuiEnabled);
    if (before.knockbackPredictionEnabled != after.knockbackPredictionEnabled)
        enqueueToast("Knockback Prediction", after.knockbackPredictionEnabled);
    if (before.bowPredictionEnabled != after.bowPredictionEnabled)
        enqueueToast("Bow Prediction", after.bowPredictionEnabled);
    if (before.localVelocityEnabled != after.localVelocityEnabled)
        enqueueToast("Local Velocity", after.localVelocityEnabled);
    if (before.freeLookEnabled != after.freeLookEnabled)
        enqueueToast("FreeLook", after.freeLookEnabled);
    if(before.smartHotbarEnabled!=after.smartHotbarEnabled)
        enqueueToast("Smart Hotbar",after.smartHotbarEnabled);
    if (before.fullscreenImeFixEnabled != after.fullscreenImeFixEnabled)
        enqueueToast("Fullscreen IME", after.fullscreenImeFixEnabled);
}

void OverlayRenderer::updateBedThreatAlerts(const GameSnapshot& snapshot) noexcept
{
    const std::uint64_t now = static_cast<std::uint64_t>(::GetTickCount64());
    if (!m_features.bedThreatAlertsEnabled ||
        snapshot.state != GameSnapshot::State::Ready ||
        !snapshot.matchActive || snapshot.ownTeam == 'u' ||
        !snapshot.ownBedKnown) {
        for (ThreatContact& contact : m_threatContacts) contact.inside = false;
        return;
    }

    const double enterDistance = static_cast<double>(
        std::clamp(m_features.bedThreatRadius, 3, 32));
    const double leaveDistance = enterDistance + 1.25;

    // Ownership is intentionally persistent for the lifetime of the world,
    // whereas the high-performance bed scanner cache may be briefly empty
    // until a chunk refresh completes. Threat distance must therefore use the
    // locked own-bed coordinates directly and only borrow the paired foot
    // coordinate when the current marker happens to be available.
    int ownBedFootX = snapshot.ownBedX;
    int ownBedFootZ = snapshot.ownBedZ;
    for (std::uint32_t bedIndex = 0U; bedIndex < snapshot.bedMarkerCount; ++bedIndex) {
        const BedMarker& marker = snapshot.bedMarkers[bedIndex];
        if (marker.x == snapshot.ownBedX && marker.y == snapshot.ownBedY &&
            marker.z == snapshot.ownBedZ) {
            ownBedFootX = marker.footX;
            ownBedFootZ = marker.footZ;
            break;
        }
    }

    auto checkEntity = [&](const EntityMarker& entity) {
        if (!entity.player) return;

        // The live entity name/team metadata may disappear for a few frames
        // while an enemy becomes invisible (some transformed clients rebuild
        // their NetworkPlayerInfo wrapper at that point).  The ESP box does
        // not depend on that metadata, so requiring a fresh TAB-name match
        // here made the box remain visible while the bed alert silently
        // dropped the same entity.  Reuse an identity only when it belongs to
        // this exact entity id and this world's locked own bed.  A non-empty,
        // different live name rejects the cache to guard against entity-id
        // reuse.
        ThreatContact* identityContact = nullptr;
        for (ThreatContact& candidate : m_threatContacts) {
            if (candidate.entityId != entity.entityId ||
                candidate.bedX != snapshot.ownBedX ||
                candidate.bedY != snapshot.ownBedY ||
                candidate.bedZ != snapshot.ownBedZ ||
                candidate.teamColor == 'u') {
                continue;
            }
            const bool uuidMatch = entity.uuid[0U] != '\0' &&
                candidate.uuid[0U] != '\0' &&
                std::strcmp(entity.uuid.data(), candidate.uuid.data()) == 0;
            const bool liveNameMissing = entity.playerName[0U] == '\0';
            const bool cachedNameMissing = candidate.playerName[0U] == '\0';
            const bool sameName = !liveNameMissing && !cachedNameMissing &&
                ::_stricmp(entity.playerName.data(),
                           candidate.playerName.data()) == 0;
            if (uuidMatch || liveNameMissing || cachedNameMissing || sameName) {
                identityContact = &candidate;
                break;
            }
        }

        bool persistentRosterKnown = false;
        bool persistentRosterTeammate = false;
        char persistentRosterTeam = 'u';
        std::array<char, 17U> persistentRosterName{};
        if (entity.playerName[0U] != '\0' || entity.uuid[0U] != '\0') {
            for (std::uint32_t index = 0U; index < snapshot.playerCount; ++index) {
                const PlayerIdentity& identity = snapshot.players[index];
                const bool uuidMatch = entity.uuid[0U] != '\0' &&
                    identity.uuid[0U] != '\0' &&
                    std::strcmp(identity.uuid.data(), entity.uuid.data()) == 0;
                const bool nameMatch = entity.playerName[0U] != '\0' &&
                    ::_stricmp(identity.name.data(), entity.playerName.data()) == 0;
                if (uuidMatch || nameMatch) {
                    persistentRosterKnown = true;
                    persistentRosterTeammate = identity.teamColor == snapshot.ownTeam;
                    persistentRosterTeam = identity.teamColor;
                    persistentRosterName = identity.name;
                    break;
                }
            }
        }
        if (!persistentRosterKnown && identityContact != nullptr) {
            persistentRosterKnown = true;
            persistentRosterTeam = identityContact->teamColor;
            persistentRosterTeammate =
                identityContact->teamColor == snapshot.ownTeam;
            persistentRosterName = identityContact->playerName;
        }
        // Threats and API lookups share the same monotonic, colour-validated
        // TAB roster. Uncoloured lobby/start NPCs are never admitted, while a
        // real player remains known through death/respawn TAB gaps.
        if (!persistentRosterKnown) return;
        // The monotonic TAB roster is authoritative once a player has been
        // admitted.  Invisibility removes armour, so live armour colour must
        // never be required to keep a confirmed enemy classified as a threat.
        // This also keeps respawning teammates excluded while their armour is
        // temporarily absent.
        if (persistentRosterTeammate) return;
        if (entity.entityId == snapshot.entityId) return; // Ignore local player

        {
            const double dx = std::min(
                std::abs(entity.currentX - (snapshot.ownBedX + 0.5)),
                std::abs(entity.currentX - (ownBedFootX + 0.5)));
            const double dz = std::min(
                std::abs(entity.currentZ - (snapshot.ownBedZ + 0.5)),
                std::abs(entity.currentZ - (ownBedFootZ + 0.5)));
            const double dy = std::abs(
                entity.currentY - static_cast<double>(snapshot.ownBedY));
            const double distance = std::sqrt(dx * dx + dz * dz + dy * dy);

            ThreatContact* contact = identityContact;
            ThreatContact* oldest = &m_threatContacts.front();
            if (contact == nullptr) {
                for (ThreatContact& candidate : m_threatContacts) {
                    if (candidate.entityId == entity.entityId &&
                        candidate.bedX == snapshot.ownBedX &&
                        candidate.bedY == snapshot.ownBedY &&
                        candidate.bedZ == snapshot.ownBedZ) {
                        contact = &candidate;
                        break;
                    }
                    if (candidate.entityId < 0 ||
                        candidate.lastSeenTick < oldest->lastSeenTick) oldest = &candidate;
                }
            }
            if (contact == nullptr) {
                contact = oldest;
                *contact = {};
                contact->entityId = entity.entityId;
                contact->bedX = snapshot.ownBedX;
                contact->bedY = snapshot.ownBedY;
                contact->bedZ = snapshot.ownBedZ;
            }
            contact->lastSeenTick = now;
            contact->distance = distance;
            contact->teamColor = persistentRosterTeam;
            contact->uuid = entity.uuid;
            if (persistentRosterName[0U] != '\0')
                contact->playerName = persistentRosterName;
            else if (entity.playerName[0U] != '\0')
                contact->playerName = entity.playerName;
            contact->invisible = entity.invisible;
            if (entity.skinTextureId != 0U)
                contact->skinTextureId = entity.skinTextureId;
            if (distance > leaveDistance) contact->inside = false;
            else if (distance <= enterDistance) contact->inside = true;
        }
    };

    for (std::uint32_t entityIndex = 0U;
         entityIndex < snapshot.entityMarkerCount; ++entityIndex) {
        const EntityMarker& entity = snapshot.entityMarkers[entityIndex];
        if (!entity.player) continue;
        checkEntity(entity);
    }
    for (ThreatContact& contact : m_threatContacts) {
        if (contact.entityId >= 0 && now - contact.lastSeenTick > 500U) contact.inside = false;
    }
}

void OverlayRenderer::renderToasts(const float deltaSeconds, const float uiScale) noexcept
{
    constexpr float lifetime = 3.4F;
    constexpr float transition = 0.32F;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = 326.0F * uiScale;
    const float height = 68.0F * uiScale;
    const float gap = 10.0F * uiScale;
    std::array<const ThreatContact*, 64U> threats{};
    std::size_t threatCount = 0U;
    for (ThreatContact& contact : m_threatContacts) {
        const float target = contact.inside && contact.entityId >= 0 ? 1.0F : 0.0F;
        contact.presentation += (target - contact.presentation) *
            (1.0F - std::exp(-13.0F * std::clamp(deltaSeconds, 0.0F, 0.05F)));
        if (contact.presentation > 0.005F && contact.entityId >= 0)
            threats[threatCount++] = &contact;
    }
    std::sort(threats.begin(), threats.begin() + threatCount,
              [](const ThreatContact* first, const ThreatContact* second) {
                  return first->distance < second->distance;
              });
    std::array<Toast*, 6U> ordered{};
    std::size_t count = 0U;
    for (Toast& toast : m_toasts) {
        if (!toast.active) continue;
        toast.age += std::clamp(deltaSeconds, 0.0F, 0.05F);
        if (toast.age >= lifetime) { toast.active = false; continue; }
        ordered[count++] = &toast;
    }
    std::sort(ordered.begin(), ordered.begin() + count,
              [](const Toast* a, const Toast* b) { return a->sequence > b->sequence; });
    ImDrawList* const draw = ImGui::GetForegroundDrawList();
    for (std::size_t index = 0U; index < threatCount; ++index) {
        const ThreatContact& contact = *threats[index];
        const float targetX = display.x - width - 22.0F * uiScale;
        const float slideLinear = std::clamp(contact.presentation, 0.0F, 1.0F);
        const float slideEase = 1.0F - std::pow(1.0F - slideLinear, 3.0F);
        const float outsideX = display.x + 12.0F * uiScale;
        const float x = outsideX + (targetX - outsideX) * slideEase;
        const float y = display.y - 24.0F * uiScale - height -
                        static_cast<float>(index) * (height + gap);
        const ImVec2 min(x, y), max(x + width, y + height);
        draw->AddRectFilled(ImVec2(min.x + 4.0F * uiScale, min.y + 7.0F * uiScale),
                            ImVec2(max.x + 4.0F * uiScale, max.y + 7.0F * uiScale),
                            IM_COL32(0, 0, 0, 75), 15.0F * uiScale);
        draw->AddRectFilled(min, max, IM_COL32(36, 24, 30, 246), 15.0F * uiScale);
        const ImU32 accent = IM_COL32(255, 92, 104, 255);
        const float warningPulse = 0.5F + 0.5F * static_cast<float>(
            std::sin(ImGui::GetTime() * 6.2));
        const int warningAlpha = static_cast<int>(120.0F + warningPulse * 125.0F);
        const float warningThickness = (1.4F + warningPulse * 1.4F) * uiScale;
        draw->AddRect(ImVec2(min.x - 2.0F * uiScale, min.y - 2.0F * uiScale),
                      ImVec2(max.x + 2.0F * uiScale, max.y + 2.0F * uiScale),
                      IM_COL32(255, 58, 76, warningAlpha), 17.0F * uiScale, 0,
                      warningThickness);
        draw->AddRect(ImVec2(min.x - 4.0F * uiScale, min.y - 4.0F * uiScale),
                      ImVec2(max.x + 4.0F * uiScale, max.y + 4.0F * uiScale),
                      IM_COL32(255, 58, 76,
                          static_cast<int>(warningAlpha * 0.28F)),
                      19.0F * uiScale, 0, 1.0F * uiScale);
        draw->AddCircleFilled(ImVec2(min.x + 20.0F * uiScale, min.y + 18.0F * uiScale),
                              10.0F * uiScale, accent);
        const ImVec2 exclamationSize = ImGui::CalcTextSize("!");
        draw->AddText(ImVec2(min.x + 20.0F * uiScale - exclamationSize.x * 0.5F,
                             min.y + 18.0F * uiScale - exclamationSize.y * 0.5F),
                      IM_COL32(255, 255, 255, 255), "!");
        const char* const name = contact.playerName[0U] == '\0'
            ? "Unknown player" : contact.playerName.data();
        const ImU32 nameColor = ImGui::ColorConvertFloat4ToU32(teamColor(contact.teamColor));
        // Minecraft's TextureManager owns this texture in the exact OpenGL
        // context used by the hook. Draw the 8x8 face and hat UV regions from
        // the already-loaded 64x64 skin; no controller download or per-frame
        // upload is necessary.
        const ImVec2 avatarMin(min.x + 39.0F * uiScale, min.y + 14.0F * uiScale);
        const ImVec2 avatarMax(avatarMin.x + 38.0F * uiScale,
                              avatarMin.y + 38.0F * uiScale);
        draw->AddRectFilled(avatarMin, avatarMax, IM_COL32(20, 18, 24, 255),
                            8.0F * uiScale);
        if (contact.skinTextureId != 0U) {
            const ImTextureID skin = reinterpret_cast<ImTextureID>(
                static_cast<std::uintptr_t>(contact.skinTextureId));
            draw->AddImage(skin, avatarMin, avatarMax,
                           ImVec2(8.0F / 64.0F, 8.0F / 64.0F),
                           ImVec2(16.0F / 64.0F, 16.0F / 64.0F));
            draw->AddImage(skin, avatarMin, avatarMax,
                           ImVec2(40.0F / 64.0F, 8.0F / 64.0F),
                           ImVec2(48.0F / 64.0F, 16.0F / 64.0F));
        } else {
            ImVec4 avatarTint = teamColor(contact.teamColor);
            avatarTint.w = 0.34F;
            draw->AddRectFilled(avatarMin, avatarMax,
                                ImGui::ColorConvertFloat4ToU32(avatarTint),
                                8.0F * uiScale);
        }
        draw->AddRect(avatarMin, avatarMax, nameColor, 10.0F * uiScale, 0,
                      1.5F * uiScale);
        const ImVec2 textPos(min.x + 88.0F * uiScale, min.y + 10.0F * uiScale);
        ImFont* const warningFont = m_fonts[static_cast<std::size_t>(
            std::clamp(m_appliedGuiScaleIndex, 0, 3))];
        if (warningFont != nullptr) {
            draw->AddText(warningFont, warningFont->LegacySize * 1.18F,
                          textPos, nameColor, name);
        } else {
            draw->AddText(textPos, nameColor, name);
        }
        char distanceLabel[64]{};
        std::snprintf(distanceLabel, sizeof(distanceLabel),
                      contact.invisible
                          ? "INVIS  |  Enemy near bed  %.1fm / %dm"
                          : "Enemy near bed  %.1fm / %dm",
                      contact.distance,
                      std::clamp(m_features.bedThreatRadius, 3, 32));
        draw->AddText(ImVec2(textPos.x, textPos.y + 28.0F * uiScale),
                      contact.invisible ? IM_COL32(255, 91, 108, 255)
                                        : IM_COL32(235, 224, 232, 255),
                      distanceLabel);
    }
    for (std::size_t index = 0U; index < count; ++index) {
        Toast& toast = *ordered[index];
        const float enter = std::clamp(toast.age / transition, 0.0F, 1.0F);
        const float exit = std::clamp((lifetime - toast.age) / transition, 0.0F, 1.0F);
        const float progress = std::min(enter, exit);
        const float eased = 1.0F - std::pow(1.0F - progress, 3.0F);
        const float targetX = display.x - width - 22.0F * uiScale;
        const float x = display.x + 12.0F * uiScale +
                        (targetX - display.x - 12.0F * uiScale) * eased;
        const float y = display.y - 24.0F * uiScale - height -
                        static_cast<float>(index + threatCount) * (height + gap);
        const ImVec2 min(x, y), max(x + width, y + height);
        draw->AddRectFilled(ImVec2(min.x + 4.0F * uiScale, min.y + 7.0F * uiScale),
                            ImVec2(max.x + 4.0F * uiScale, max.y + 7.0F * uiScale),
                            IM_COL32(0, 0, 0, static_cast<int>(75.0F * eased)), 15.0F * uiScale);
        draw->AddRectFilled(min, max, IM_COL32(28, 25, 36, static_cast<int>(242.0F * eased)),
                            15.0F * uiScale);
        const ImU32 accent = toast.enabled ? IM_COL32(87, 220, 126, 255)
                                           : IM_COL32(255, 105, 115, 255);
        draw->AddRectFilled(min, ImVec2(min.x + 5.0F * uiScale, max.y), accent,
                            15.0F * uiScale, ImDrawFlags_RoundCornersLeft);
        draw->AddCircleFilled(ImVec2(min.x + 29.0F * uiScale, min.y + height * 0.5F),
                              10.0F * uiScale, accent);
        draw->AddText(ImVec2(min.x + 50.0F * uiScale, min.y + 19.0F * uiScale),
                      IM_COL32(245, 240, 252, static_cast<int>(255.0F * eased)),
                      toast.label.data());
    }
}

bool OverlayRenderer::initialize(HWND const window, HGLRC const context) noexcept
{
    if (m_permanentlyDisabled) {
        return false;
    }
    if (m_inputState == nullptr) {
        m_inputState = new (std::nothrow) OverlayInputState;
        if (m_inputState == nullptr) {
            return false;
        }
    }

    IMGUI_CHECKVERSION();
    m_imguiContext = ImGui::CreateContext();
    if (m_imguiContext == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(m_imguiContext);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // FontGlobalScale enlarges a single bitmap and becomes visibly blurry.
    // Build four independently rasterized Segoe UI sizes into one atlas and
    // switch the default font only at the safe pre-NewFrame boundary.
    char windowsDirectory[MAX_PATH]{};
    const UINT windowsLength = ::GetWindowsDirectoryA(windowsDirectory, MAX_PATH);
    std::array<char, MAX_PATH> fontPath{};
    std::array<char, MAX_PATH> boldFontPath{};
    if (windowsLength > 0U && windowsLength + 20U < fontPath.size()) {
        std::snprintf(fontPath.data(), fontPath.size(), "%s\\Fonts\\segoeui.ttf",
                      windowsDirectory);
        std::snprintf(boldFontPath.data(), boldFontPath.size(),
                      "%s\\Fonts\\segoeuib.ttf", windowsDirectory);
    }
    constexpr std::array<float, 4U> fontSizes{15.0F, 19.0F, 23.0F, 27.0F};
    for (std::size_t index = 0U; index < m_fonts.size(); ++index) {
        ImFontConfig fontConfig{};
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;
        if (fontPath[0U] != '\0') {
            m_fonts[index] = io.Fonts->AddFontFromFileTTF(
                fontPath.data(), fontSizes[index], &fontConfig,
                io.Fonts->GetGlyphRangesDefault());
        }
        if (m_fonts[index] == nullptr) {
            fontConfig.SizePixels = fontSizes[index];
            m_fonts[index] = io.Fonts->AddFontDefault(&fontConfig);
        }
        ImFontConfig boldConfig{};
        boldConfig.OversampleH = 3;
        boldConfig.OversampleV = 2;
        boldConfig.PixelSnapH = false;
        if (boldFontPath[0U] != '\0') {
            m_boldFonts[index] = io.Fonts->AddFontFromFileTTF(
                boldFontPath.data(), fontSizes[index], &boldConfig,
                io.Fonts->GetGlyphRangesDefault());
        }
        if (m_boldFonts[index] == nullptr) m_boldFonts[index] = m_fonts[index];
    }
    // One dedicated CJK font is enough for the transient IME card. Building
    // four full CJK atlases would waste substantial memory inside the game.
    std::array<char, MAX_PATH> imeFontPath{};
    if (windowsLength > 0U && windowsLength + 18U < imeFontPath.size()) {
        std::snprintf(imeFontPath.data(), imeFontPath.size(),
                      "%s\\Fonts\\msyh.ttc", windowsDirectory);
        if (::GetFileAttributesA(imeFontPath.data()) == INVALID_FILE_ATTRIBUTES) {
            std::snprintf(imeFontPath.data(), imeFontPath.size(),
                          "%s\\Fonts\\msjh.ttc", windowsDirectory);
        }
    }
    if (imeFontPath[0U] != '\0' &&
        ::GetFileAttributesA(imeFontPath.data()) != INVALID_FILE_ATTRIBUTES) {
        ImFontConfig imeConfig{};
        imeConfig.OversampleH = 2;
        imeConfig.OversampleV = 2;
        imeConfig.PixelSnapH = false;
        m_imeFont = io.Fonts->AddFontFromFileTTF(
            imeFontPath.data(), 22.0F, &imeConfig,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }

    // GSMTC strings are UTF-8 all the way through the controller protocol;
    // the old squares/question marks were caused by the Latin-only Segoe UI
    // atlas. Keep one media-only CJK face and merge Japanese kana/kanji into
    // it. The GUI continues to use the four compact Latin atlases above.
    std::array<char, MAX_PATH> mediaChinesePath{};
    std::array<char, MAX_PATH> mediaJapanesePath{};
    if (windowsLength > 0U && windowsLength + 24U < mediaChinesePath.size()) {
        std::snprintf(mediaChinesePath.data(), mediaChinesePath.size(),
                      "%s\\Fonts\\msyh.ttc", windowsDirectory);
        std::snprintf(mediaJapanesePath.data(), mediaJapanesePath.size(),
                      "%s\\Fonts\\YuGothM.ttc", windowsDirectory);
        if (::GetFileAttributesA(mediaJapanesePath.data()) == INVALID_FILE_ATTRIBUTES) {
            std::snprintf(mediaJapanesePath.data(), mediaJapanesePath.size(),
                          "%s\\Fonts\\msgothic.ttc", windowsDirectory);
        }
    }
    if (mediaChinesePath[0U] != '\0' &&
        ::GetFileAttributesA(mediaChinesePath.data()) != INVALID_FILE_ATTRIBUTES) {
        ImFontConfig mediaConfig{};
        // Small HUD text benefits noticeably from horizontal oversampling and
        // a slight coverage lift.  Keep vertical oversampling at one (ImGui
        // does not use sub-pixel Y placement) so the full CJK atlas stays
        // reasonably small inside the target JVM.
        mediaConfig.OversampleH = 2;
        mediaConfig.OversampleV = 1;
        mediaConfig.PixelSnapH = false;
        mediaConfig.RasterizerMultiply = 1.12F;
        m_mediaFont = io.Fonts->AddFontFromFileTTF(
            mediaChinesePath.data(), 20.0F, &mediaConfig,
            io.Fonts->GetGlyphRangesChineseFull());
        if (m_mediaFont != nullptr && mediaJapanesePath[0U] != '\0' &&
            ::GetFileAttributesA(mediaJapanesePath.data()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig japaneseConfig{};
            japaneseConfig.MergeMode = true;
            japaneseConfig.DstFont = m_mediaFont;
            japaneseConfig.OversampleH = 2;
            japaneseConfig.OversampleV = 1;
            japaneseConfig.PixelSnapH = false;
            japaneseConfig.RasterizerMultiply = 1.12F;
            (void)io.Fonts->AddFontFromFileTTF(
                mediaJapanesePath.data(), 20.0F, &japaneseConfig,
                io.Fonts->GetGlyphRangesJapanese());
        }
    }

    m_appliedGuiScaleIndex = -1;
    m_animatedGuiScale = guiScaleForIndex(m_guiScaleIndex);
    applyGuiScaleStyle(m_animatedGuiScale, m_guiScaleIndex);

    if (!ImGui_ImplWin32_Init(window)) {
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext = nullptr;
        m_fonts = {};
        m_boldFonts = {};
        m_imeFont = nullptr;
        m_mediaFont = nullptr;
        return false;
    }
    if (!ImGui_ImplOpenGL2_Init()) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext = nullptr;
        m_fonts = {};
        m_boldFonts = {};
        m_imeFont = nullptr;
        m_mediaFont = nullptr;
        return false;
    }

    m_inputState->imguiContext.store(m_imguiContext, std::memory_order_release);
    m_inputState->window.store(window, std::memory_order_release);
    updateImeState(*m_inputState, window, ImeMessageAction::ResetLayout,
        reinterpret_cast<LPARAM>(::GetKeyboardLayout(
            ::GetWindowThreadProcessId(window, nullptr))));
    m_inputState->acceptImGuiMessages.store(true, std::memory_order_release);
    m_inputState->fallbackPrimed = false;
    DWORD windowProcessId = 0U;
    const DWORD windowThreadId = ::GetWindowThreadProcessId(window, &windowProcessId);
    m_inputState->directImGuiWndProc.store(
        windowThreadId != 0U && windowThreadId == ::GetCurrentThreadId(),
        std::memory_order_release);
    // Minecraft/LWJGL remains the cursor style owner. The overlay only keeps
    // the cursor visible while interactive; it never swaps the game's native
    // pointer for ImGui's smaller default arrow or resize sprites.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    const bool windowProcedureInstalled = m_windowProcedure.install(
        window, &OverlayRenderer::handleWindowMessage, m_inputState);
    m_inputState->windowProcedureAvailable.store(
        windowProcedureInstalled, std::memory_order_release);
    if (!windowProcedureInstalled && !m_wndProcFallbackLogged) {
        log::info("WndProc input hook unavailable; using non-blocking async input fallback.");
        m_wndProcFallbackLogged = true;
    }

    int bedWidth = 0, bedHeight = 0, bedChannels = 0;
    unsigned char* bedPixels = stbi_load_from_memory(BED_PNG_DATA, sizeof(BED_PNG_DATA), &bedWidth, &bedHeight, &bedChannels, 4);
    if (bedPixels) {
        GLint lastTexture = 0;
        ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
        ::glGenTextures(1, &m_bedTexture);
        ::glBindTexture(GL_TEXTURE_2D, m_bedTexture);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bedWidth, bedHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, bedPixels);
        stbi_image_free(bedPixels);

        auto loadBlockTexture = [](const unsigned char* data, std::size_t size) -> unsigned {
            int w = 0, h = 0, c = 0;
            unsigned char* pixels = stbi_load_from_memory(data, size, &w, &h, &c, 4);
            if (!pixels) return 0U;
            unsigned tex = 0U;
            ::glGenTextures(1, &tex);
            ::glBindTexture(GL_TEXTURE_2D, tex);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            stbi_image_free(pixels);
            return tex;
        };
        m_blockTextures[0] = loadBlockTexture(BLOCK_WOOL_PNG, sizeof(BLOCK_WOOL_PNG));
        m_blockTextures[1] = loadBlockTexture(BLOCK_PLANKS_OAK_PNG, sizeof(BLOCK_PLANKS_OAK_PNG));
        m_blockTextures[2] = loadBlockTexture(BLOCK_HARDENED_CLAY_PNG, sizeof(BLOCK_HARDENED_CLAY_PNG));
        m_blockTextures[3] = loadBlockTexture(BLOCK_GLASS_PNG, sizeof(BLOCK_GLASS_PNG));
        m_blockTextures[4] = loadBlockTexture(BLOCK_END_STONE_PNG, sizeof(BLOCK_END_STONE_PNG));
        m_blockTextures[5] = loadBlockTexture(BLOCK_OBSIDIAN_PNG, sizeof(BLOCK_OBSIDIAN_PNG));

        ::glBindTexture(GL_TEXTURE_2D, lastTexture);
    }

    // Kenney's CC0 Input Prompts atlas is embedded in the DLL so an injected
    // agent never depends on Minecraft's working directory or loose files.
    HMODULE agentModule = nullptr;
    if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&OverlayRenderer::handleWindowMessage),
            &agentModule)) {
        const unsigned char* atlasBytes = nullptr;
        std::size_t atlasByteCount = 0U;
        const unsigned char* atlasXml = nullptr;
        std::size_t atlasXmlBytes = 0U;
        if (rawModuleResource(agentModule, MCOVERLAY_KENNEY_INPUT_PROMPTS_XML,
                              atlasXml, atlasXmlBytes)) {
            m_mediaKeycapAtlasXml = reinterpret_cast<const char*>(atlasXml);
            m_mediaKeycapAtlasXmlSize = atlasXmlBytes;
        }
        if (rawModuleResource(agentModule, MCOVERLAY_KENNEY_INPUT_PROMPTS_PNG,
                              atlasBytes, atlasByteCount)) {
            int atlasWidth=0,atlasHeight=0,channels=0;
            unsigned char* atlasPixels=stbi_load_from_memory(atlasBytes,
                static_cast<int>(atlasByteCount),&atlasWidth,&atlasHeight,&channels,4);
            if (atlasPixels && atlasWidth>0 && atlasHeight>0) {
                GLint previousTexture=0;
                ::glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);
                ::glGenTextures(1,&m_mediaKeycapTexture);
                ::glBindTexture(GL_TEXTURE_2D,m_mediaKeycapTexture);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
                ::glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,atlasWidth,atlasHeight,0,
                               GL_RGBA,GL_UNSIGNED_BYTE,atlasPixels);
                ::glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previousTexture));
            }
            stbi_image_free(atlasPixels);
        }
    }

    m_window = window;
    m_glContext = context;
    m_initialized = true;
    return true;
}

void OverlayRenderer::shutdownWithCurrentContext() noexcept
{
    if (!m_initialized || m_imguiContext == nullptr) {
        return;
    }
    stopTsf(m_window, m_inputState);
    m_gaussianBlur.release();
    if (m_inputState != nullptr) {
        m_inputState->acceptImGuiMessages.store(false, std::memory_order_release);
        m_inputState->interactive.store(false, std::memory_order_release);
        m_inputState->windowProcedureAvailable.store(false, std::memory_order_release);
        m_inputState->directImGuiWndProc.store(false, std::memory_order_release);
        m_inputState->captureHotkey.store(false, std::memory_order_release);
        m_inputState->window.store(nullptr, std::memory_order_release);
    }
    if (!m_windowProcedure.restore()) {
        abandonAfterWndProcDrainTimeout();
        return;
    }
    if (m_inputState != nullptr) {
        m_inputState->imguiContext.store(nullptr, std::memory_order_release);
    }
    ImGui::SetCurrentContext(m_imguiContext);
    if (m_blurTexture != 0U) {
        const GLuint texture = static_cast<GLuint>(m_blurTexture);
        ::glDeleteTextures(1, &texture);
        m_blurTexture = 0U;
        m_blurWidth = 0;
        m_blurHeight = 0;
    }
    if (m_bedTexture != 0U) {
        const GLuint texture = static_cast<GLuint>(m_bedTexture);
        ::glDeleteTextures(1, &texture);
        m_bedTexture = 0U;
    }
    if (m_mediaCoverTexture != 0U) {
        const GLuint texture = static_cast<GLuint>(m_mediaCoverTexture);
        ::glDeleteTextures(1, &texture);
        m_mediaCoverTexture = 0U;
    }
    if(m_mediaPreviousCoverTexture!=0U) {
        const GLuint texture=static_cast<GLuint>(m_mediaPreviousCoverTexture);
        ::glDeleteTextures(1,&texture);
        m_mediaPreviousCoverTexture=0U;
    }
    if(m_mediaKeycapTexture!=0U) {
        const GLuint texture=static_cast<GLuint>(m_mediaKeycapTexture);
        ::glDeleteTextures(1,&texture);
        m_mediaKeycapTexture=0U;
    }
    for (unsigned& tex : m_blockTextures) {
        if (tex != 0U) {
            const GLuint t = static_cast<GLuint>(tex);
            ::glDeleteTextures(1, &t);
            tex = 0U;
        }
    }
    for (BlacklistTexture& cached : m_blacklistTextures) {
        if (cached.texture != 0U) {
            const GLuint texture = static_cast<GLuint>(cached.texture);
            ::glDeleteTextures(1, &texture);
        }
        cached = {};
    }
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(m_imguiContext);
    m_imguiContext = nullptr;
    m_fonts = {};
    m_boldFonts = {};
    m_imeFont = nullptr;
    m_mediaFont = nullptr;
    m_blurTexture = 0U;
    m_bedTexture = 0U;
    m_mediaCoverTexture = 0U;
    m_mediaPreviousCoverTexture = 0U;
    m_mediaKeycapTexture = 0U;
    m_mediaKeycapAtlasXml = nullptr;
    m_mediaKeycapAtlasXmlSize = 0U;
    m_mediaLoadedCoverPath = {};
    m_mediaLoadedTitle = {};
    m_mediaLoadedArtist = {};
    m_mediaPreviousTitle = {};
    m_mediaPreviousArtist = {};
    m_blurWidth = 0;
    m_blurHeight = 0;
    m_blacklistTextures = {};
    m_cursorSessionActive = false;
    m_window = nullptr;
    m_glContext = nullptr;
    m_initialized = false;
}

void OverlayRenderer::abandonForContextChange() noexcept
{
    stopTsf(m_window, m_inputState);
    m_gaussianBlur.abandon();
    if (m_imguiContext != nullptr) {
        if (m_inputState != nullptr) {
            m_inputState->acceptImGuiMessages.store(false, std::memory_order_release);
            m_inputState->interactive.store(false, std::memory_order_release);
            m_inputState->windowProcedureAvailable.store(false, std::memory_order_release);
            m_inputState->directImGuiWndProc.store(false, std::memory_order_release);
            m_inputState->captureHotkey.store(false, std::memory_order_release);
            m_inputState->window.store(nullptr, std::memory_order_release);
        }
        if (!m_windowProcedure.restore()) {
            abandonAfterWndProcDrainTimeout();
            return;
        }
        if (m_inputState != nullptr) {
            m_inputState->imguiContext.store(nullptr, std::memory_order_release);
        }
        // The old HGLRC is not current. Neither ImGui_ImplOpenGL2_Shutdown nor
        // DestroyContext is legal here: the former would delete driver objects
        // in the new context, while the latter asserts because renderer backend
        // data is still attached. Retain this small, unreachable generation;
        // initialize() creates a fresh context for the new HGLRC.
        log::error("Retaining an ImGui generation whose OpenGL context is no longer current.");
    }
    m_imguiContext = nullptr;
    m_fonts = {};
    m_boldFonts = {};
    m_imeFont = nullptr;
    m_mediaFont = nullptr;
    // The old HGLRC is unavailable, so its texture cannot be deleted here.
    // Drop the name to prevent a later context generation from deleting an
    // unrelated object which happens to reuse the same GLuint value.
    m_blurTexture = 0U;
    m_bedTexture = 0U;
    m_mediaCoverTexture = 0U;
    m_mediaPreviousCoverTexture = 0U;
    m_mediaKeycapTexture = 0U;
    m_mediaKeycapAtlasXml = nullptr;
    m_mediaKeycapAtlasXmlSize = 0U;
    m_mediaLoadedCoverPath = {};
    m_mediaLoadedTitle = {};
    m_mediaLoadedArtist = {};
    m_mediaPreviousTitle = {};
    m_mediaPreviousArtist = {};
    m_blurWidth = 0;
    m_blurHeight = 0;
    m_blacklistTextures = {};
    m_cursorSessionActive = false;
    m_window = nullptr;
    m_glContext = nullptr;
    m_initialized = false;
}

void OverlayRenderer::abandonAfterWndProcDrainTimeout() noexcept
{
    // A callback selected the old handler before restore() unpublished it and
    // did not finish within the bounded drain. Destroying the ImGui context or
    // input bridge would race that callback. Both are intentionally retained;
    // no new handler can acquire them because WndProcHook is already inert.
    log::error("Retaining ImGui state after a WndProc drain timeout.");
    m_inputState = nullptr;
    m_imguiContext = nullptr;
    m_fonts = {};
    m_boldFonts = {};
    m_imeFont = nullptr;
    m_mediaFont = nullptr;
    m_blacklistTextures = {};
    m_mediaCoverTexture = 0U;
    m_mediaPreviousCoverTexture = 0U;
    m_mediaKeycapTexture = 0U;
    m_mediaKeycapAtlasXml = nullptr;
    m_mediaKeycapAtlasXmlSize = 0U;
    m_mediaLoadedCoverPath = {};
    m_mediaLoadedTitle = {};
    m_mediaLoadedArtist = {};
    m_mediaPreviousTitle = {};
    m_mediaPreviousArtist = {};
    m_cursorSessionActive = false;
    m_window = nullptr;
    m_glContext = nullptr;
    m_initialized = false;
    m_permanentlyDisabled = true;
}

void OverlayRenderer::abandonAfterHookDisabled() noexcept
{
    abandonForContextChange();
}

void OverlayRenderer::captureBackdropTexture() noexcept
{
    const ImGuiIO& io = ImGui::GetIO();
    const int width = static_cast<int>(io.DisplaySize.x);
    const int height = static_cast<int>(io.DisplaySize.y);
    if (width < 2 || height < 2 || m_backdropCapturedThisFrame) return;

    ::glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (m_blurTexture == 0U) {
        GLuint texture = 0U;
        ::glGenTextures(1, &texture);
        m_blurTexture = texture;
    }
    ::glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_blurTexture));
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    if (width != m_blurWidth || height != m_blurHeight) {
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                       GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        m_blurWidth = width;
        m_blurHeight = height;
    }
    ::glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    ::glPopAttrib();
    m_backdropCapturedThisFrame = true;
}

void OverlayRenderer::renderInventoryBlur(const float strength) noexcept
{
    if (strength <= 0.01F || m_features.clickGuiBlur <= 0) return;
    const ImGuiIO& io = ImGui::GetIO();
    captureBackdropTexture();
    const float amount = static_cast<float>(m_features.clickGuiBlur) / 100.0F;
    (void)m_gaussianBlur.draw(m_blurTexture,
        static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y),
        0.5F + 5.5F * amount, strength);
}

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
    const auto insideControl=[&](const std::size_t index) noexcept {
        return io.MousePos.x>=controlX &&
            io.MousePos.x<=controlX+controlColumnWidth &&
            io.MousePos.y>=controlTops[index] &&
            io.MousePos.y<=controlTops[index]+controlHeight;
    };
    std::array<bool,3U> controlHovered{};
    std::array<bool,3U> controlPressed{};
    if(interactive&&linear>0.985F) {
        for(std::size_t index=0;index<controlHovered.size();++index) {
            controlHovered[index]=insideControl(index);
            controlPressed[index]=controlHovered[index]&&
                ImGui::IsMouseDown(ImGuiMouseButton_Left);
        }
    }
    if(interactive && linear>0.985F) {
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if(insideControl(0U)) requestAction(MediaAction::Previous);
            else if(insideControl(1U)) requestAction(MediaAction::Toggle);
            else if(insideControl(2U)) requestAction(MediaAction::Next);
            else if(ImGui::IsMouseHoveringRect(minimum,maximum,false)) {
                m_mediaDragging=true;
                m_mediaDragOffsetX=io.MousePos.x-panelX;
                m_mediaDragOffsetY=io.MousePos.y-panelY;
            }
        }
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
    } else if(!interactive) m_mediaDragging=false;

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

void OverlayRenderer::renderImeOverlay(const float deltaSeconds,
                                       const float uiScale) noexcept
{
    if (m_inputState == nullptr) return;
    std::array<wchar_t, 80U> name{};
    std::array<wchar_t, 128U> composition{};
    std::array<std::array<wchar_t, 64U>, 9U> candidates{};
    std::uint32_t candidateCount = 0U;
    std::uint32_t candidateSelection = 0U;
    bool composing = false;
    ::AcquireSRWLockShared(&m_inputState->imeLock);
    name = m_inputState->imeName;
    composition = m_inputState->imeComposition;
    candidates = m_inputState->imeCandidates;
    candidateCount = m_inputState->imeCandidateCount;
    candidateSelection = m_inputState->imeCandidateSelection;
    composing = m_inputState->imeComposing;
    ::ReleaseSRWLockShared(&m_inputState->imeLock);
    if (m_inputState->tsf != nullptr) {
        const ImeCandidates tsf = m_inputState->tsf->snapshot();
        if (tsf.active) {
            candidates = tsf.words;
            candidateCount = tsf.count;
            candidateSelection = tsf.selected;
            composing = true;
        }
    }
    if (m_imePositionEditing) {
        std::wcscpy(name.data(), L"输入法面板 · 拖动调整位置");
        std::wcscpy(composition.data(), L"ni'hao");
        std::wcscpy(candidates[0].data(), L"你好");
        std::wcscpy(candidates[1].data(), L"你号");
        std::wcscpy(candidates[2].data(), L"拟好");
        candidateCount = 3;
        candidateSelection = 0;
    }

    const std::uint64_t revision = m_inputState->imeRevision.load(
        std::memory_order_acquire);
    const std::uint64_t now = ::GetTickCount64();
    if (revision != m_lastImeRevision) {
        m_lastImeRevision = revision;
        m_lastImeActivityTick = now;
    }
    const bool recentlyChanged = m_lastImeActivityTick != 0U &&
        now - m_lastImeActivityTick < 2200U;
    const bool visible = m_imePositionEditing || (m_features.fullscreenImeFixEnabled &&
        (composing || recentlyChanged));
    const float target = visible ? 1.0F : 0.0F;
    m_imePanelProgress += (target - m_imePanelProgress) *
        (1.0F - std::exp(-12.0F * std::clamp(deltaSeconds, 0.0F, 0.05F)));
    if (m_imePanelProgress < 0.002F) return;

    const auto toUtf8 = [](const wchar_t* const source,
                           char* const destination,
                           const int capacity) noexcept {
        destination[0] = '\0';
        if (source == nullptr || source[0] == L'\0') return;
        (void)::WideCharToMultiByte(CP_UTF8, 0, source, -1,
            destination, capacity, nullptr, nullptr);
        destination[capacity - 1] = '\0';
    };
    std::array<char, 240U> nameUtf8{};
    std::array<char, 384U> compositionUtf8{};
    std::array<std::array<char, 192U>, 9U> candidateUtf8{};
    toUtf8(name.data(), nameUtf8.data(), static_cast<int>(nameUtf8.size()));
    toUtf8(composition.data(), compositionUtf8.data(),
           static_cast<int>(compositionUtf8.size()));
    for (std::size_t index = 0U; index < candidateUtf8.size(); ++index) {
        toUtf8(candidates[index].data(), candidateUtf8[index].data(),
               static_cast<int>(candidateUtf8[index].size()));
    }

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* const draw = ImGui::GetForegroundDrawList();
    const float eased = m_imePanelProgress * m_imePanelProgress *
        (3.0F - 2.0F * m_imePanelProgress);
    const float width = std::min(660.0F * uiScale,
                                 io.DisplaySize.x - 24.0F * uiScale);
    const float rowHeight = candidateCount == 0U ? 0.0F : 35.0F * uiScale;
    const float height = (compositionUtf8[0U] != '\0' ? 92.0F : 66.0F) *
        uiScale + rowHeight;
    const float availableX = std::max(1.0F, io.DisplaySize.x - width - 12.0F);
    const float availableY = std::max(1.0F, io.DisplaySize.y - height - 70.0F * uiScale);
    const float targetX = m_features.imePanelX < 0 ? (io.DisplaySize.x - width) * 0.5F
        : 6.0F + availableX * static_cast<float>(m_features.imePanelX) / 255.0F;
    const float targetY = m_features.imePanelY < 0 ? 18.0F * uiScale
        : 6.0F + availableY * static_cast<float>(m_features.imePanelY) / 255.0F;
    ImVec2 minimum(targetX, targetY - 24.0F * uiScale * (1.0F - eased));
    if (m_imePositionEditing) {
        if (!m_imeDragging) { m_imeEditX = targetX; m_imeEditY = targetY; }
        if (ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(
                minimum, ImVec2(minimum.x + width, minimum.y + height), false))
            m_imeDragging = true;
        if (!ImGui::IsMouseDown(0)) m_imeDragging = false;
        if (m_imeDragging) {
            m_imeEditX = std::clamp(m_imeEditX + io.MouseDelta.x, 6.0F, 6.0F + availableX);
            m_imeEditY = std::clamp(m_imeEditY + io.MouseDelta.y, 6.0F, 6.0F + availableY);
            m_features.imePanelX = static_cast<int>(std::lround((m_imeEditX - 6.0F) / availableX * 255.0F));
            m_features.imePanelY = static_cast<int>(std::lround((m_imeEditY - 6.0F) / availableY * 255.0F));
        }
        minimum = ImVec2(m_imeEditX, m_imeEditY);
        ImGui::SetNextWindowPos(ImVec2(minimum.x, minimum.y + height + 8.0F * uiScale));
        ImGui::SetNextWindowSize(ImVec2(width, 44.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F * uiScale, 6.0F * uiScale));
        ImGui::Begin("##ImePositionTools", nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
        if (ImGui::Button("Done / Save position", ImVec2(210.0F * uiScale, 32.0F * uiScale))) {
            m_imePositionEditing = false;
            m_imeDragging = false;
            m_featureSettingsDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset position", ImVec2(150.0F * uiScale, 32.0F * uiScale))) {
            m_features.imePanelX = m_features.imePanelY = -1;
            m_featureSettingsDirty = true;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    const ImVec2 maximum(minimum.x + width, minimum.y + height);
    const int alpha = static_cast<int>(238.0F * eased);
    draw->AddRectFilled(ImVec2(minimum.x - 5.0F * uiScale,
                              minimum.y + 8.0F * uiScale),
                        ImVec2(maximum.x + 5.0F * uiScale,
                              maximum.y + 12.0F * uiScale),
                        IM_COL32(0, 0, 0, static_cast<int>(54.0F * eased)),
                        20.0F * uiScale);
    draw->AddRectFilled(minimum, maximum,
        IM_COL32(20, 21, 28, alpha), 18.0F * uiScale);
    draw->AddRectFilled(minimum,
        ImVec2(minimum.x + 5.0F * uiScale, maximum.y),
        IM_COL32(114, 224, 210, static_cast<int>(255.0F * eased)),
        18.0F * uiScale, ImDrawFlags_RoundCornersLeft);
    ImFont* const font = m_imeFont != nullptr ? m_imeFont : ImGui::GetFont();
    const float fontSize = (m_imeFont != nullptr ? 22.0F : ImGui::GetFontSize()) *
        uiScale;
    draw->AddText(font, fontSize * 0.70F,
        ImVec2(minimum.x + 22.0F * uiScale, minimum.y + 12.0F * uiScale),
        IM_COL32(174, 180, 194, static_cast<int>(255.0F * eased)),
        nameUtf8[0U] != '\0' ? nameUtf8.data() : "Input method");
    if (compositionUtf8[0U] != '\0') {
        draw->AddText(font, fontSize,
            ImVec2(minimum.x + 22.0F * uiScale,
                   minimum.y + 34.0F * uiScale),
            IM_COL32(248, 249, 252, static_cast<int>(255.0F * eased)),
            compositionUtf8.data());
    }
    if (candidateCount != 0U) {
        const float top = maximum.y - rowHeight;
        const float cellWidth = (width - 28.0F * uiScale) /
            static_cast<float>(candidateCount);
        for (std::uint32_t index = 0U; index < candidateCount; ++index) {
            const float left = minimum.x + 14.0F * uiScale +
                cellWidth * static_cast<float>(index);
            if (index == candidateSelection) {
                draw->AddRectFilled(ImVec2(left, top + 2.0F * uiScale),
                    ImVec2(left + cellWidth - 4.0F * uiScale,
                           maximum.y - 5.0F * uiScale),
                    IM_COL32(55, 184, 170, static_cast<int>(105.0F * eased)),
                    9.0F * uiScale);
            }
            char numbered[224]{};
            std::snprintf(numbered, sizeof(numbered), "%u %s",
                index + 1U, candidateUtf8[index].data());
            draw->PushClipRect(ImVec2(left, top),
                ImVec2(left + cellWidth - 4.0F * uiScale, maximum.y), true);
            draw->AddText(font, fontSize * 0.76F,
                ImVec2(left + 7.0F * uiScale, top + 8.0F * uiScale),
                IM_COL32(236, 239, 244, static_cast<int>(255.0F * eased)),
                numbered);
            draw->PopClipRect();
        }
    }
}

bool OverlayRenderer::render(HDC const deviceContext,
                             const GameSnapshot& snapshot,
                             const bool interactive) noexcept
{
    if (m_permanentlyDisabled) {
        return false;
    }
    const HGLRC context = ::wglGetCurrentContext();
    const HWND window = ::WindowFromDC(deviceContext);
    if (context == nullptr || window == nullptr || ::IsWindowVisible(window) == FALSE) {
        return false;
    }
    RECT client{};
    if (::GetClientRect(window, &client) == FALSE ||
        client.right - client.left < 480 || client.bottom - client.top < 270) {
        return false;
    }

    bool newlyInitialized = false;
    if (m_initialized && (m_window != window || m_glContext != context)) {
        if (m_glContext == context) {
            shutdownWithCurrentContext();
        } else {
            abandonForContextChange();
        }
    }
    if (!m_initialized) {
        if (!initialize(window, context)) {
            return false;
        }
        newlyInitialized = true;
    }

    if (m_inputState == nullptr) {
        return false;
    }
    m_inputState->interactive.store(interactive, std::memory_order_release);
    const bool imeWasEnabled=m_inputState->imeEnabled.exchange(
        m_features.fullscreenImeFixEnabled,std::memory_order_acq_rel);
    if(imeWasEnabled&&!m_features.fullscreenImeFixEnabled)
        clearImeComposition(*m_inputState);
    if (!interactive) {
        if (m_imePositionEditing) m_featureSettingsDirty = true;
        m_imePositionEditing = m_imeDragging = false;
        m_inputState->captureHotkey.store(false, std::memory_order_release);
        m_inputState->sessionCursor.store(nullptr, std::memory_order_release);
        m_waitingForHotkey = false;
        m_hotkeyCaptureTarget = 0;
        m_statsPanelDragging = false;
        m_statsPanelResizing = false;
    }
    ImGui::SetCurrentContext(m_imguiContext);
    ImGuiIO& io = ImGui::GetIO();
    m_guiScaleIndex = std::clamp(m_guiScaleIndex, 0, 3);
    const float targetScale = guiScaleForIndex(m_guiScaleIndex);
    const float scaleDelta = std::clamp(io.DeltaTime, 0.0F, 0.05F);
    m_animatedGuiScale += (targetScale - m_animatedGuiScale) *
                          (1.0F - std::exp(-11.0F * scaleDelta));
    if (std::abs(targetScale - m_animatedGuiScale) < 0.001F)
        m_animatedGuiScale = targetScale;
    // Rebuild from immutable constants before NewFrame. This produces a
    // smooth size transition without ever compounding ScaleAllSizes values.
    applyGuiScaleStyle(m_animatedGuiScale, m_guiScaleIndex);
    const float uiScale = m_animatedGuiScale;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (interactive && !m_cursorSessionActive) {
        CURSORINFO cursorInfo{};
        cursorInfo.cbSize = sizeof(cursorInfo);
        HCURSOR cursor = nullptr;
        if (::GetCursorInfo(&cursorInfo) != FALSE &&
            (cursorInfo.flags & CURSOR_SHOWING) != 0U) {
            cursor = cursorInfo.hCursor;
        }
        if (cursor == nullptr) {
            cursor = reinterpret_cast<HCURSOR>(::GetClassLongPtrW(
                window, GCLP_HCURSOR));
        }
        if (cursor == nullptr) cursor = ::LoadCursorW(nullptr, IDC_ARROW);
        m_inputState->sessionCursor.store(cursor, std::memory_order_release);
        m_cursorSessionActive = true;
    } else if (!interactive && m_cursorSessionActive) {
        m_cursorSessionActive = false;
        m_inputState->sessionCursor.store(nullptr, std::memory_order_release);
    }

    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    m_backdropCapturedThisFrame = false;

    // Feature hotkeys are sampled only while Minecraft owns foreground focus.
    // While unfocused we mirror physical state into the edge latch, so a key
    // used in another application cannot fire immediately on refocus.
    const bool gameForeground = ::GetForegroundWindow() == window;
    const bool gameplayHotkeysAllowed = gameForeground && !snapshot.gameScreenOpen &&
        !m_inputState->composingInput.load(std::memory_order_acquire);
    const FeatureSettings featuresBeforeHotkeys = m_features;
    bool hotkeyFeatureChanged = false;
    for (std::size_t index = 0U; index < m_features.featureHotkeys.size(); ++index) {
        const int key = m_features.featureHotkeys[index];
        const bool down = key >= 8 && key <= 254 &&
            (::GetAsyncKeyState(key) & 0x8000) != 0;
        bool toggledFeature = false;
        if (gameplayHotkeysAllowed && !interactive &&
            down && !m_featureHotkeyWasDown[index]) {
            switch (index) {
            case 0: m_features.entityEspEnabled = !m_features.entityEspEnabled; toggledFeature=true; break;
            case 1: m_features.bedEspEnabled = !m_features.bedEspEnabled; toggledFeature=true; break;
            case 2: m_features.nametagEnabled = !m_features.nametagEnabled; toggledFeature=true; break;
            case 3: m_features.bedThreatAlertsEnabled =
                        !m_features.bedThreatAlertsEnabled; toggledFeature=true; break;
            case 4: m_features.safewalkEnabled = !m_features.safewalkEnabled; toggledFeature=true; break;
            case 5: m_features.scaffoldEnabled = !m_features.scaffoldEnabled; toggledFeature=true; break;
            case 6: m_features.flyEnabled = !m_features.flyEnabled; toggledFeature=true; break;
            case 7: m_features.bhopEnabled = !m_features.bhopEnabled; toggledFeature=true; break;
            case 8: m_features.aimAssistEnabled = !m_features.aimAssistEnabled; toggledFeature=true; break;
            case 9: m_features.hypixelPanelEnabled =
                        !m_features.hypixelPanelEnabled; toggledFeature=true; break;
            case 10: m_features.debugChatEnabled = !m_features.debugChatEnabled; toggledFeature=true; break;
            case 11:
                m_blacklist.panelEnabled = !m_blacklist.panelEnabled;
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
                enqueueToast("Blacklist", m_blacklist.panelEnabled);
                break;
            case 12: m_features.textGuiEnabled = !m_features.textGuiEnabled; break;
            case 13: m_features.fireballEspEnabled =
                         !m_features.fireballEspEnabled; break;
            case 14: break;
            case 15: m_features.bedBreakerEnabled =
                          !m_features.bedBreakerEnabled; break;
            case 16: m_features.localVelocityEnabled =
                          !m_features.localVelocityEnabled; break;
            // FreeLook (index 17) is intentionally hold-only. GameBindings
            // samples its physical state and never converts the press to a
            // persistent enabled/disabled edge here.
            case 17: break;
            default: break;
            }
            if (index >= 12U && index <= 16U) toggledFeature = true;
            if (index != 11U && toggledFeature) hotkeyFeatureChanged = true;
        }
        m_featureHotkeyWasDown[index] = down;
    }
    if (hotkeyFeatureChanged) {
        m_features.safewalkHotkey = m_features.featureHotkeys[4U];
        m_featureSettingsDirty = true;
        enqueueFeatureToasts(featuresBeforeHotkeys, m_features);
    }

    const float targetGui = interactive && !m_imePositionEditing ? 1.0F : 0.0F;
    const float delta = std::clamp(io.DeltaTime, 0.0F, 0.10F);
    // A lightly under-damped spring takes roughly half a second to settle. It
    // is slower than the previous exponential lerp but still responsive, and
    // its single small overshoot provides the requested lightweight rebound.
    // Exact under-damped oscillator integration. At normal frame rates this is
    // visually equivalent to the previous spring, while uneven/slow title-menu
    // frames no longer quantize the motion into Euler steps.
    advancePresentationSpring(m_clickGuiProgress, m_clickGuiVelocity,
                              targetGui, delta);
    const float guiLinear = std::clamp(m_clickGuiProgress, 0.0F, 1.0F);
    const float guiEase = guiLinear * guiLinear * (3.0F - 2.0F * guiLinear);
    renderInventoryBlur(std::clamp(guiEase * 1.28F, 0.0F, 1.0F));
    renderImeOverlay(delta, uiScale);

    updateBedThreatAlerts(snapshot);

    if (snapshot.state == GameSnapshot::State::Ready && snapshot.camera.valid) {
        ImDrawList* const background = ImGui::GetBackgroundDrawList();
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        if (m_features.bedEspEnabled) {
            const bool defenseHotkeyDown = gameplayHotkeysAllowed &&
                (::GetAsyncKeyState(std::clamp(m_features.bedDefenseHotkey, 8, 254)) &
                 0x8000) != 0;
            const bool defensePanelsVisible = m_features.bedDefensePanelEnabled &&
                (!m_features.bedDefenseHoldToShow || defenseHotkeyDown);
            if (m_features.bedAutoRefreshEnabled) {
                const double now = ImGui::GetTime();
                if (now - m_lastBedRefreshTime >= 2.0) {
                    m_bedRescanPending = true;
                    m_lastBedRefreshTime = now;
                }
            }
            for (std::uint32_t index = 0U; index < snapshot.bedMarkerCount; ++index) {
                const BedMarker& bed = snapshot.bedMarkers[index];
                const AxisAlignedBox bedBox{
                    static_cast<double>(std::min(bed.x, bed.footX)),
                    static_cast<double>(bed.y),
                    static_cast<double>(std::min(bed.z, bed.footZ)),
                    static_cast<double>(std::max(bed.x, bed.footX) + 1),
                    static_cast<double>(bed.y) + 0.5625,
                    static_cast<double>(std::max(bed.z, bed.footZ) + 1)};
                char label[64]{};
                if (m_features.labelsEnabled) {
                    std::snprintf(label, sizeof(label), "Bed  %d %d %d", bed.x, bed.y, bed.z);
                }
                
                const ImU32 boxColor = packedRgbColor(m_features.bedEspColor);
                const bool ownBed = snapshot.ownBedKnown &&
                    bed.x == snapshot.ownBedX && bed.y == snapshot.ownBedY &&
                    bed.z == snapshot.ownBedZ;
                
                drawProjectedBox(background, snapshot.camera, displaySize, bedBox,
                                 boxColor, label, m_features.bedEspFilled);

                const bool defenseOwnershipVisible =
                    m_features.showOwnBedDefenseInfo || !snapshot.matchActive ||
                    !snapshot.ownBedKnown || !ownBed;
                if (defensePanelsVisible && bed.defenseCount > 0U &&
                    defenseOwnershipVisible) {
                    const int radius = std::clamp(m_features.bedDefenseRadius, 3, 10);
                    std::array<std::uint16_t, BedMarker::MaxDefenseBlocks> totals{};
                    std::uint32_t visibleBlocks = 0U;
                    for (std::size_t material = 0U; material < bed.defenseCount; ++material) {
                        unsigned total = 0U;
                        for (int ring = 1; ring <= radius; ++ring) {
                            total += bed.defense[material].ringCounts[
                                static_cast<std::size_t>(ring)];
                        }
                        totals[material] = static_cast<std::uint16_t>(
                            std::min<unsigned>(total, UINT16_MAX));
                        if (total != 0U) ++visibleBlocks;
                    }
                    const ScreenPoint anchor = projectPoint(
                        snapshot.camera, displaySize,
                        (static_cast<double>(bed.x + bed.footX) + 1.0) * 0.5,
                        static_cast<double>(bed.y) + 1.65,
                        (static_cast<double>(bed.z + bed.footZ) + 1.0) * 0.5);
                    if (anchor.visible && visibleBlocks > 0U) {
                        const double dx = (static_cast<double>(bed.x + bed.footX) + 1.0) *
                            0.5 - snapshot.x;
                        const double dy = static_cast<double>(bed.y) - snapshot.y;
                        const double dz = (static_cast<double>(bed.z + bed.footZ) + 1.0) *
                            0.5 - snapshot.z;
                        const double bedDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
                        const float distanceScale = m_features.bedDefensePerspectiveScale
                            ? std::clamp(static_cast<float>(14.0 / (bedDistance + 4.0)),
                                         0.55F, 1.40F)
                            : 1.0F;
                        const float panelScale = uiScale * distanceScale;
                        const unsigned columns = std::min<std::uint32_t>(visibleBlocks, 6U);
                        const unsigned rows = (visibleBlocks + 5U) / 6U;
                        const float cell = 45.0F * panelScale;
                        const float panelWidth = 20.0F * panelScale +
                            static_cast<float>(columns) * cell;
                        const float panelHeight = 24.0F * panelScale +
                            static_cast<float>(rows) * 49.0F * panelScale;
                        float left = anchor.x - panelWidth * 0.5F;
                        left = std::clamp(left, 8.0F, std::max(8.0F,
                            displaySize.x - panelWidth - 8.0F));
                        const float top = std::clamp(anchor.y - panelHeight - 10.0F * panelScale,
                                                     8.0F, displaySize.y - panelHeight - 8.0F);
                        const ImVec2 panelMin(left, top);
                        const ImVec2 panelMax(left + panelWidth, top + panelHeight);
                        background->AddRectFilled(
                            ImVec2(panelMin.x + 3.0F * panelScale,
                                   panelMin.y + 5.0F * panelScale),
                            ImVec2(panelMax.x + 3.0F * panelScale,
                                   panelMax.y + 5.0F * panelScale),
                             IM_COL32(0, 0, 0, 85), 12.0F * panelScale);
                        const int panelAlpha = static_cast<int>(std::lround(
                            std::clamp(m_features.bedDefensePanelOpacity, 0, 100) * 2.55));
                        if (panelAlpha < 250) {
                            captureBackdropTexture();
                            if (m_blurTexture != 0U) {
                                constexpr std::array<ImVec2, 5U> blurOffsets{{
                                    ImVec2(-3.0F, 0.0F), ImVec2(3.0F, 0.0F),
                                    ImVec2(0.0F, -3.0F), ImVec2(0.0F, 3.0F),
                                    ImVec2(0.0F, 0.0F)}};
                                for (const ImVec2 offset : blurOffsets) {
                                    const float sourceLeft = std::clamp(
                                        panelMin.x + offset.x * panelScale,
                                        0.0F, displaySize.x);
                                    const float sourceTop = std::clamp(
                                        panelMin.y + offset.y * panelScale,
                                        0.0F, displaySize.y);
                                    const float sourceRight = std::clamp(
                                        panelMax.x + offset.x * panelScale,
                                        0.0F, displaySize.x);
                                    const float sourceBottom = std::clamp(
                                        panelMax.y + offset.y * panelScale,
                                        0.0F, displaySize.y);
                                    background->AddImageRounded(
                                        reinterpret_cast<ImTextureID>(
                                            static_cast<std::uintptr_t>(m_blurTexture)),
                                        panelMin, panelMax,
                                        ImVec2(sourceLeft / displaySize.x,
                                               1.0F - sourceTop / displaySize.y),
                                        ImVec2(sourceRight / displaySize.x,
                                               1.0F - sourceBottom / displaySize.y),
                                        IM_COL32(255, 255, 255, 48),
                                        12.0F * panelScale);
                                }
                            }
                        }
                        background->AddRectFilled(
                            panelMin, panelMax,
                            packedRgbColor(m_features.bedDefensePanelColor, panelAlpha),
                            12.0F * panelScale);
                        background->AddRect(
                            panelMin, panelMax, IM_COL32(255, 255, 255, 38),
                            12.0F * panelScale, 0, 1.0F * panelScale);
                        
                        // Draw 2D vanilla bed item icon
                        const float bedIconSize = 18.0F * panelScale;
                        const ImVec2 bPos(left + 8.0F * panelScale, top + 4.0F * panelScale);
                        if (m_bedTexture != 0U) {
                            background->AddImage(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(m_bedTexture)),
                                                 bPos, ImVec2(bPos.x + bedIconSize, bPos.y + bedIconSize));
                        }

                        char radiusLabel[24]{};
                        std::snprintf(radiusLabel, sizeof(radiusLabel),
                                      "Bed");
                        background->AddText(
                            ImVec2(left + 30.0F * panelScale,
                                   top + 6.0F * panelScale),
                            IM_COL32(221, 210, 241, 235), radiusLabel);
                        unsigned visibleIndex = 0U;
                        for (std::size_t material = 0U; material < bed.defenseCount;
                             ++material) {
                            if (totals[material] == 0U) continue;
                            const unsigned column = visibleIndex % 6U;
                            const unsigned row = visibleIndex / 6U;
                            const float cellX = left + 10.0F * panelScale +
                                (static_cast<float>(column) + 0.5F) * cell;
                            const float cellY = top + 37.0F * panelScale +
                                static_cast<float>(row) * 49.0F * panelScale;
                            drawInventoryBlockIcon(background, ImVec2(cellX, cellY),
                                                   23.0F * panelScale,
                                                   bed.defense[material].blockId,
                                                   bed.defense[material].metadata,
                                                   m_blockTextures);
                            char countLabel[16]{};
                            std::snprintf(countLabel, sizeof(countLabel), "x%u",
                                          static_cast<unsigned>(totals[material]));
                            background->AddText(
                                ImVec2(cellX - 12.0F * panelScale,
                                       cellY + 14.0F * panelScale),
                                IM_COL32(248, 244, 252, 245), countLabel);
                            ++visibleIndex;
                        }
                    }
                }
            }
        }
        if (m_features.entityEspEnabled || m_features.nametagEnabled ||
            (m_features.fireballEspEnabled && snapshot.integratedSinglePlayer)) {
            const double renderTick = snapshot.entityRenderTick;
            for (std::uint32_t index = 0U; index < snapshot.entityMarkerCount; ++index) {
                const EntityMarker& entity = snapshot.entityMarkers[index];
                if (entity.fireball) {
                    if (m_features.fireballEspEnabled &&
                        snapshot.integratedSinglePlayer) {
                        const double renderX = entity.previousX +
                            (entity.currentX - entity.previousX) * renderTick;
                        const double renderY = entity.previousY +
                            (entity.currentY - entity.previousY) * renderTick;
                        const double renderZ = entity.previousZ +
                            (entity.currentZ - entity.previousZ) * renderTick;
                        // EntityLargeFireball's collision box is visually much
                        // larger than its core. A compact fixed cube protects
                        // visibility while still tracking the projectile.
                        constexpr double halfExtent = 0.24;
                        const AxisAlignedBox fireballBox{
                            renderX - halfExtent, renderY - halfExtent,
                            renderZ - halfExtent, renderX + halfExtent,
                            renderY + halfExtent, renderZ + halfExtent};
                        drawProjectedBox(background, snapshot.camera, displaySize,
                            fireballBox, packedRgbColor(m_features.fireballEspColor),
                            m_features.labelsEnabled ? "Fireball" : "",
                            m_features.fireballEspFilled);
                    }
                    continue;
                }
                if (m_features.entityEspPlayersOnly && !entity.player) continue;
                const double renderX = entity.previousX +
                    (entity.currentX - entity.previousX) * renderTick;
                const double renderY = entity.previousY +
                    (entity.currentY - entity.previousY) * renderTick;
                const double renderZ = entity.previousZ +
                    (entity.currentZ - entity.previousZ) * renderTick;
                const double offsetX = renderX - entity.currentX;
                const double offsetY = renderY - entity.currentY;
                const double offsetZ = renderZ - entity.currentZ;
                const AxisAlignedBox interpolated{
                    entity.bounds.minX + offsetX, entity.bounds.minY + offsetY,
                    entity.bounds.minZ + offsetZ, entity.bounds.maxX + offsetX,
                    entity.bounds.maxY + offsetY, entity.bounds.maxZ + offsetZ};
                char label[64]{};
                if (m_features.entityEspEnabled && m_features.labelsEnabled &&
                    !(entity.player && m_features.nametagEnabled)) {
                    if (entity.player && entity.playerName[0U] != '\0') {
                        std::snprintf(label, sizeof(label), "%s  %.1fm",
                                      entity.playerName.data(), entity.distance);
                    } else {
                        std::snprintf(label, sizeof(label), "Entity #%d  %.1fm",
                                      entity.entityId, entity.distance);
                    }
                }
                
                const ImU32 entityColor = entity.player
                    ? packedRgbColor(m_features.playerEspColor)
                    : IM_COL32(255, 168, 74, 255);
                // Restore the original live armour-colour classification as
                // the primary signal. Roster metadata is retained only as a
                // fallback for a frame where armour JNI data is unavailable.
                const bool armorTeammate = entity.armorTeam != 'u' &&
                    entity.armorTeam == snapshot.ownTeam;
                const bool rosterTeammate = entity.teamColor != 'u' &&
                    entity.teamColor == snapshot.ownTeam;
                const bool isTeammate = snapshot.matchActive && entity.player &&
                    snapshot.ownTeam != 'u' && (armorTeammate || rosterTeammate);
                if (m_features.entityEspEnabled &&
                    (!isTeammate || m_features.showTeammateBoxes)) {
                    drawProjectedBox(background, snapshot.camera, displaySize, interpolated,
                                     entityColor, label);
                }
                if (m_features.entityEspEnabled && isTeammate &&
                    m_features.showTeammateArrows) {
                    const double centerX = (interpolated.minX + interpolated.maxX) * 0.5;
                    const double centerZ = (interpolated.minZ + interpolated.maxZ) * 0.5;
                    const double topY = interpolated.maxY + 0.95;
                    const ScreenPoint pt = projectPoint(snapshot.camera, displaySize, centerX, topY, centerZ);
                    if (pt.visible) {
                        const char visualTeam = entity.armorTeam != 'u'
                            ? entity.armorTeam : entity.teamColor;
                        const ImVec4 teamAccentVector = teamColor(visualTeam);
                        const ImU32 teamAccent = ImGui::ColorConvertFloat4ToU32(
                            teamAccentVector);
                        const float halfWidth = 11.0F * uiScale;
                        const float height = halfWidth * 1.7320508F;
                        const ImVec2 tip(pt.x, pt.y);
                        const ImVec2 left(pt.x - halfWidth, pt.y - height);
                        const ImVec2 right(pt.x + halfWidth, pt.y - height);
                        drawRoundedTriangle(background,
                            ImVec2(tip.x + 1.5F * uiScale, tip.y + 3.0F * uiScale),
                            ImVec2(left.x + 1.5F * uiScale, left.y + 3.0F * uiScale),
                            ImVec2(right.x + 1.5F * uiScale, right.y + 3.0F * uiScale),
                            4.6F * uiScale, IM_COL32(0, 0, 0, 92));
                        // The inner triangle is a similarity transform around
                        // the equilateral triangle's centroid. Unlike manually
                        // moving only its tip, this is exactly equivalent to
                        // offsetting all three edges inward by the same amount,
                        // so the white shell has uniform thickness everywhere.
                        drawRoundedTriangle(background, tip, left, right,
                                            4.6F * uiScale,
                                            IM_COL32(255, 255, 255, 245));
                        const float borderThickness = 2.8F * uiScale;
                        const float inradius = height / 3.0F;
                        const float innerScale = std::clamp(
                            1.0F - borderThickness / inradius, 0.25F, 0.90F);
                        const ImVec2 centroid(pt.x, pt.y - height * (2.0F / 3.0F));
                        const auto insetVertex = [&](const ImVec2 vertex) noexcept {
                            return ImVec2(
                                centroid.x + (vertex.x - centroid.x) * innerScale,
                                centroid.y + (vertex.y - centroid.y) * innerScale);
                        };
                        drawRoundedTriangle(
                            background, insetVertex(tip), insetVertex(left),
                            insetVertex(right), 3.2F * uiScale, teamAccent);
                    }
                }
                const bool nametagAllowed = entity.player && entity.confirmedPlayer &&
                    m_features.nametagEnabled && entity.playerName[0U] != '\0' &&
                    (!isTeammate || m_features.showTeammateNametags) &&
                    (isTeammate || !m_features.nametagNearbyEnemiesOnly ||
                     entity.distance <= static_cast<double>(m_features.nametagRange));
                if (nametagAllowed &&
                    entity.playerName[0U] != '\0') {
                    ImVec2 bodyMin{}, bodyMax{};
                    if (projectedBoxBounds(snapshot.camera, displaySize, interpolated,
                                           bodyMin, bodyMax)) {
                        constexpr std::array<float, 4U> sizePresets{{
                            0.86F, 1.0F, 1.16F, 1.34F}};
                        const float tagScale = std::clamp(uiScale, 0.88F, 1.34F) *
                            sizePresets[static_cast<std::size_t>(
                                std::clamp(m_features.nametagSizeIndex, 0, 3))];
                        const int potion = entity.heldItemDamage & 0x3FFF;
                        const bool knownSpecial = entity.heldItemId == 388 ||
                            entity.heldItemId == 264 || entity.heldItemId == 46 ||
                            entity.heldItemId == 385 ||
                            (entity.heldItemId == 373 &&
                             (potion == 8206 || potion == 8270));
                        const bool enemy = snapshot.ownTeam != 'u' && !isTeammate;
                        const bool showSpecial = m_features.enemyItemIndicatorsEnabled &&
                            enemy && knownSpecial;
                        const float width = 184.0F * tagScale;
                        const float height = (showSpecial ? 62.0F : 48.0F) * tagScale;
                        float x = (bodyMin.x + bodyMax.x - width) * 0.5F;
                        float y = bodyMin.y - height - 8.0F * tagScale;
                        if (m_features.nametagSidePlacement) {
                            const bool placeRight = (bodyMin.x + bodyMax.x) * 0.5F <
                                                    displaySize.x * 0.5F;
                            x = placeRight ? bodyMax.x + 9.0F * tagScale
                                           : bodyMin.x - width - 9.0F * tagScale;
                            y = (bodyMin.y + bodyMax.y - height) * 0.5F;
                        }
                        x = std::clamp(x, 4.0F,
                            std::max(4.0F, displaySize.x - width - 4.0F));
                        y = std::clamp(y, 4.0F,
                            std::max(4.0F, displaySize.y - height - 4.0F));
                        const ImVec2 minimum(x, y), maximum(x + width, y + height);
                        background->AddRectFilled(
                            ImVec2(x + 2.0F * tagScale, y + 4.0F * tagScale),
                            ImVec2(maximum.x + 2.0F * tagScale,
                                   maximum.y + 4.0F * tagScale),
                            IM_COL32(0, 0, 0, 74), 11.0F * tagScale);
                        background->AddRectFilled(minimum, maximum,
                            packedRgbColor(m_features.nametagPanelColor,
                                static_cast<int>(std::lround(static_cast<float>(
                                    std::clamp(m_features.nametagPanelOpacity,
                                               10, 100)) * 2.55F))),
                            11.0F * tagScale);
                        background->AddRect(minimum, maximum,
                            IM_COL32(255, 255, 255, 34), 11.0F * tagScale);
                        if (enemy && m_features.nametagTeamPulse) {
                            const float pulse = 0.5F + 0.5F * static_cast<float>(
                                std::sin(ImGui::GetTime() * 5.8));
                            ImVec4 pulseColor = teamColor(entity.teamColor != 'u'
                                ? entity.teamColor : entity.armorTeam);
                            pulseColor.w = 0.42F + pulse * 0.50F;
                            background->AddRect(
                                ImVec2(minimum.x - (1.0F + pulse) * tagScale,
                                       minimum.y - (1.0F + pulse) * tagScale),
                                ImVec2(maximum.x + (1.0F + pulse) * tagScale,
                                       maximum.y + (1.0F + pulse) * tagScale),
                                ImGui::ColorConvertFloat4ToU32(pulseColor),
                                12.0F * tagScale, 0,
                                (1.2F + 1.3F * pulse) * tagScale);
                        }

                        const ImVec2 faceMin(x + 8.0F * tagScale,
                                             y + 7.0F * tagScale);
                        const ImVec2 faceMax(faceMin.x + 34.0F * tagScale,
                                             faceMin.y + 34.0F * tagScale);
                        background->AddRectFilled(faceMin, faceMax,
                            IM_COL32(24, 23, 29, 255), 7.0F * tagScale);
                        if (entity.skinTextureId != 0U) {
                            const ImTextureID skin = reinterpret_cast<ImTextureID>(
                                static_cast<std::uintptr_t>(entity.skinTextureId));
                            background->AddImage(skin, faceMin, faceMax,
                                ImVec2(8.0F / 64.0F, 8.0F / 64.0F),
                                ImVec2(16.0F / 64.0F, 16.0F / 64.0F));
                            background->AddImage(skin, faceMin, faceMax,
                                ImVec2(40.0F / 64.0F, 8.0F / 64.0F),
                                ImVec2(48.0F / 64.0F, 16.0F / 64.0F));
                        }
                        background->AddRect(faceMin, faceMax,
                            IM_COL32(255, 255, 255, 42), 7.0F * tagScale);

                        NametagAnimation* animation = nullptr;
                        NametagAnimation* oldest = &m_nametagAnimations.front();
                        const std::uint64_t now = static_cast<std::uint64_t>(::GetTickCount64());
                        for (NametagAnimation& candidate : m_nametagAnimations) {
                            if (candidate.entityId == entity.entityId) {
                                animation = &candidate;
                                break;
                            }
                            if (candidate.entityId < 0 ||
                                candidate.lastSeenTick < oldest->lastSeenTick) oldest = &candidate;
                        }
                        if (animation == nullptr) {
                            animation = oldest;
                            *animation = {};
                            animation->entityId = entity.entityId;
                            animation->displayedHealth = entity.health;
                        }
                        animation->lastSeenTick = now;
                        animation->displayedHealth +=
                            (entity.health - animation->displayedHealth) *
                            (1.0F - std::exp(-11.0F * delta));
                        const float ratio = std::clamp(animation->displayedHealth /
                            std::max(1.0F, entity.maxHealth), 0.0F, 1.0F);
                        const std::size_t nametagFontIndex = static_cast<std::size_t>(
                            std::clamp(m_features.nametagSizeIndex, 0, 3));
                        ImFont* const font = m_boldFonts[nametagFontIndex] != nullptr
                            ? m_boldFonts[nametagFontIndex] : ImGui::GetFont();
                        const float fontSize = font->LegacySize;
                        const ImU32 nameColor = ImGui::ColorConvertFloat4ToU32(
                            teamColor(entity.teamColor != 'u'
                                ? entity.teamColor : entity.armorTeam));
                        background->AddText(font, fontSize,
                            ImVec2(faceMax.x + 8.0F * tagScale,
                                   y + 7.0F * tagScale), nameColor,
                            entity.playerName.data());
                        if (entity.protectionLevel > 0U) {
                            char protection[16]{};
                            std::snprintf(protection, sizeof(protection), "Prot %s",
                                          protectionRoman(entity.protectionLevel));
                            const ImVec2 size = font->CalcTextSizeA(
                                fontSize * 0.76F, FLT_MAX, 0.0F, protection);
                            background->AddText(font, fontSize * 0.76F,
                                ImVec2(maximum.x - size.x - 7.0F * tagScale,
                                       y + 8.0F * tagScale),
                                IM_COL32(196, 176, 255, 245), protection);
                        }
                        char healthText[16]{};
                        std::snprintf(healthText, sizeof(healthText), "%.1f",
                                      animation->displayedHealth);
                        const float healthFontSize = fontSize * 0.76F;
                        const ImVec2 healthTextSize = font->CalcTextSizeA(
                            healthFontSize, FLT_MAX, 0.0F, healthText);
                        const ImVec2 barMin(faceMax.x + 8.0F * tagScale,
                                            y + 29.0F * tagScale);
                        const ImVec2 barMax(maximum.x - 13.0F * tagScale -
                                                healthTextSize.x,
                                            barMin.y + 7.0F * tagScale);
                        background->AddRectFilled(barMin, barMax,
                            IM_COL32(255, 255, 255, 28), 3.5F * tagScale);
                        const ImU32 hpColor = ratio > 0.60F ? IM_COL32(78, 220, 121, 255)
                            : (ratio > 0.30F ? IM_COL32(255, 190, 62, 255)
                                             : IM_COL32(255, 76, 92, 255));
                        background->AddRectFilled(barMin,
                            ImVec2(barMin.x + (barMax.x - barMin.x) * ratio, barMax.y),
                            hpColor, 3.5F * tagScale);
                        background->AddText(font, healthFontSize,
                            ImVec2(maximum.x - 7.0F * tagScale - healthTextSize.x,
                                   barMin.y - (healthTextSize.y -
                                               (barMax.y - barMin.y)) * 0.5F),
                            hpColor, healthText);
                        if (showSpecial) {
                            const char* item = entity.heldItemId == 388 ? "EMERALD"
                                : entity.heldItemId == 264 ? "DIAMOND"
                                : entity.heldItemId == 46 ? "TNT"
                                : entity.heldItemId == 385 ? "FIREBALL" : "INVIS";
                            char itemText[32]{};
                            std::snprintf(itemText, sizeof(itemText), "%s x%u", item,
                                static_cast<unsigned>(std::max<std::uint8_t>(
                                    1U, entity.heldItemCount)));
                            background->AddText(font, fontSize * 0.76F,
                                ImVec2(faceMax.x + 8.0F * tagScale,
                                       y + 44.0F * tagScale),
                                entity.heldItemId == 373
                                    ? IM_COL32(255, 93, 113, 255)
                                    : IM_COL32(105, 218, 240, 255), itemText);
                        }
                    }
                }
            }
        }

        // Knockback prediction is visual-only and is fed by the bounded 20 Hz
        // snapshot. A detected impulse starts one animation; subsequent samples
        // refine its path without restarting it, then the landing box lingers.
        const double trajectoryNow = ImGui::GetTime();
        if (m_features.knockbackPredictionEnabled &&
            snapshot.entitySampleGeneration != m_lastKnockbackGeneration) {
            m_lastKnockbackGeneration = snapshot.entitySampleGeneration;
            for (std::uint8_t predictionIndex = 0U;
                 predictionIndex < snapshot.knockbackTrajectoryCount;
                 ++predictionIndex) {
                const KnockbackTrajectory& prediction =
                    snapshot.knockbackTrajectories[predictionIndex];
                KnockbackVisual* visual = nullptr;
                KnockbackVisual* oldest = &m_knockbackVisuals.front();
                for (KnockbackVisual& candidate : m_knockbackVisuals) {
                    if (candidate.active &&
                        candidate.trajectory.entityId == prediction.entityId) {
                        visual = &candidate;
                        break;
                    }
                    if (!candidate.active) oldest = &candidate;
                    else if (candidate.updatedAt < oldest->updatedAt) oldest = &candidate;
                }
                if (visual == nullptr) {
                    visual = oldest;
                    *visual = {};
                    visual->startedAt = trajectoryNow;
                    visual->active = true;
                    visual->trajectory = prediction;
                } else if (trajectoryNow - visual->updatedAt > 0.42) {
                    // A later impulse on the same entity is a new event.
                    visual->startedAt = trajectoryNow;
                    visual->trajectory = prediction;
                }
                // Keep the first trajectory immutable throughout one impulse.
                // Re-basing it to the entity's newer mid-flight position while
                // preserving animation time would make the box jump forward.
                visual->updatedAt = trajectoryNow;
            }
        }
        for (KnockbackVisual& visual : m_knockbackVisuals) {
            if (!m_features.knockbackPredictionEnabled) {
                visual.active = false;
                continue;
            }
            if (!visual.active || visual.trajectory.pointCount < 2U) continue;
            const KnockbackTrajectory& prediction = visual.trajectory;
            const float duration = std::max(0.28F,
                static_cast<float>(prediction.pointCount - 1U) * 0.045F);
            const float age = static_cast<float>(trajectoryNow - visual.startedAt);
            if (age > duration + 2.7F) {
                visual.active = false;
                continue;
            }
            const ImU32 trajectoryColor = IM_COL32(255, 184, 72, 220);
            const std::size_t visiblePoint = std::min<std::size_t>(
                prediction.pointCount - 1U,
                static_cast<std::size_t>(std::floor(std::clamp(
                    age / duration, 0.0F, 1.0F) *
                    static_cast<float>(prediction.pointCount - 1U))));
            for (std::size_t point = 1U; point <= visiblePoint; ++point) {
                const ScreenPoint first = projectPoint(snapshot.camera, displaySize,
                    prediction.points[point - 1U].x,
                    prediction.points[point - 1U].y + 0.9,
                    prediction.points[point - 1U].z);
                const ScreenPoint second = projectPoint(snapshot.camera, displaySize,
                    prediction.points[point].x,
                    prediction.points[point].y + 0.9,
                    prediction.points[point].z);
                if (first.visible && second.visible)
                    background->AddLine(ImVec2(first.x, first.y),
                        ImVec2(second.x, second.y), trajectoryColor, 2.0F);
            }
            const auto translatedBox = [&](const WorldPoint& point) noexcept {
                const WorldPoint& origin = prediction.points[0U];
                const double offsetX = point.x - origin.x;
                const double offsetY = point.y - origin.y;
                const double offsetZ = point.z - origin.z;
                return AxisAlignedBox{
                    prediction.startBounds.minX + offsetX,
                    prediction.startBounds.minY + offsetY,
                    prediction.startBounds.minZ + offsetZ,
                    prediction.startBounds.maxX + offsetX,
                    prediction.startBounds.maxY + offsetY,
                    prediction.startBounds.maxZ + offsetZ};
            };
            if (age < duration) {
                const float exactIndex = std::clamp(age / duration, 0.0F, 1.0F) *
                    static_cast<float>(prediction.pointCount - 1U);
                const std::size_t lower = std::min<std::size_t>(
                    static_cast<std::size_t>(std::floor(exactIndex)),
                    prediction.pointCount - 1U);
                const std::size_t upper = std::min<std::size_t>(
                    lower + 1U, prediction.pointCount - 1U);
                const double blend = static_cast<double>(exactIndex -
                    static_cast<float>(lower));
                const WorldPoint animated{
                    prediction.points[lower].x +
                        (prediction.points[upper].x - prediction.points[lower].x) * blend,
                    prediction.points[lower].y +
                        (prediction.points[upper].y - prediction.points[lower].y) * blend,
                    prediction.points[lower].z +
                        (prediction.points[upper].z - prediction.points[lower].z) * blend};
                drawProjectedBox(background, snapshot.camera, displaySize,
                    translatedBox(animated), trajectoryColor, "", true);
            } else if (prediction.landed) {
                const float fade = std::clamp(
                    1.0F - (age - duration) / 2.7F, 0.0F, 1.0F);
                drawProjectedBox(background, snapshot.camera, displaySize,
                    translatedBox(prediction.points[
                        prediction.pointCount - 1U]),
                    IM_COL32(255, 184, 72,
                        static_cast<int>(220.0F * fade)), "", true);
            }
        }

        if(m_features.aimAssistEnabled && m_features.aimSilentLock &&
           m_features.aimScannerEnabled && snapshot.aimTargetEntityId>=0) {
            for(std::uint32_t index=0;index<snapshot.entityMarkerCount;++index) {
                const auto& marker=snapshot.entityMarkers[index];
                if(marker.entityId!=snapshot.aimTargetEntityId || marker.health<=0) continue;
                auto box=marker.bounds;
                const double t=snapshot.entityRenderTick;
                const double dx=marker.previousX+(marker.currentX-marker.previousX)*t-marker.currentX;
                const double dy=marker.previousY+(marker.currentY-marker.previousY)*t-marker.currentY;
                const double dz=marker.previousZ+(marker.currentZ-marker.previousZ)*t-marker.currentZ;
                box.minX+=dx; box.maxX+=dx; box.minY+=dy; box.maxY+=dy;
                box.minZ+=dz; box.maxZ+=dz;
                // The stable outline identifies the selected target. The
                // animated scan plane is reserved for an actually attackable
                // target when vanilla reach/occlusion checking is enabled.
                drawProjectedBox(background,snapshot.camera,displaySize,box,
                    IM_COL32(120,240,255,150),"",false);
                const bool attackable=!m_features.aimAttackViability ||
                    snapshot.aimAttackTargetEntityId==snapshot.aimTargetEntityId;
                if(attackable) {
                    AxisAlignedBox scanBox=box;
                    const double scan=(std::sin(ImGui::GetTime()*3.4)+1)*0.5;
                    scanBox.minY=box.minY+(box.maxY-box.minY)*scan;
                    scanBox.maxY=scanBox.minY+0.035;
                    drawProjectedBox(background,snapshot.camera,displaySize,scanBox,
                        IM_COL32(120,240,255,225),"",true);
                }
                break;
            }
        }
        // The bow path is recomputed every frame from the exact 1.8.9
        // charge/drag/gravity constants. The terminal marker turns red only
        // when the swept segment first intersects any living entity AABB.
        if (m_features.bowPredictionEnabled && snapshot.bowTrajectory.active &&
            snapshot.bowTrajectory.pointCount >= 2U) {
            const BowTrajectory& bow = snapshot.bowTrajectory;
            for (std::size_t point = 1U; point < bow.pointCount; ++point) {
                const ScreenPoint first = projectPoint(snapshot.camera, displaySize,
                    bow.points[point - 1U].x, bow.points[point - 1U].y,
                    bow.points[point - 1U].z);
                const ScreenPoint second = projectPoint(snapshot.camera, displaySize,
                    bow.points[point].x, bow.points[point].y, bow.points[point].z);
                if (first.visible && second.visible)
                    background->AddLine(ImVec2(first.x, first.y),
                        ImVec2(second.x, second.y), IM_COL32(92, 220, 255, 225),
                        2.0F);
            }
            if (bow.hasImpact) {
                constexpr double impactHalf = 0.16;
                const AxisAlignedBox impactBox{
                    bow.impact.x - impactHalf, bow.impact.y - impactHalf,
                    bow.impact.z - impactHalf, bow.impact.x + impactHalf,
                    bow.impact.y + impactHalf, bow.impact.z + impactHalf};
                const ImU32 impactColor = bow.impactLiving
                    ? IM_COL32(255, 70, 86, 255)
                    : IM_COL32(92, 220, 255, 255);
                drawProjectedBox(background, snapshot.camera, displaySize,
                    impactBox, impactColor,
                    bow.impactPlayer ? "PLAYER IMPACT" : bow.impactLiving ? "ENTITY IMPACT" : "IMPACT", true);
            }
        }
    }

    // Modern two-pane Click GUI. The left rail exposes stable feature pages;
    // each page owns its top-right master switch and all related detail
    // settings. Categories are labels, never collapsing containers, so every
    // function remains one click away.
    const float baseGuiWidth = 820.0F * static_cast<float>(std::clamp(
        m_features.clickGuiWidthPercent, 80, 150)) / 100.0F;
    // Keep the navigation rail usable at the smallest height while still
    // allowing generous vertical expansion on larger displays.
    const float requestedGuiHeight = std::max(620.0F, 650.0F * static_cast<float>(
        std::clamp(m_features.clickGuiHeightPercent, 80, 150)) / 100.0F);
    const float baseGuiHeight = std::max(220.0F, std::min(requestedGuiHeight,
        (io.DisplaySize.y - 78.0F * uiScale) / uiScale));
    constexpr float baseRailWidth = 198.0F;
    const float guiWidth = baseGuiWidth * uiScale;
    const float guiHeight = baseGuiHeight * uiScale;
    if (m_clickGuiX < -9000.0F) {
        m_clickGuiX = std::max(8.0F, (io.DisplaySize.x - guiWidth) * 0.5F);
        m_clickGuiY = std::max(8.0F, (io.DisplaySize.y - guiHeight) * 0.16F);
    }
    m_clickGuiY = std::clamp(m_clickGuiY, 4.0F,
        std::max(4.0F, io.DisplaySize.y - guiHeight - 70.0F * uiScale));
    m_clickGuiThemeProgress +=
        ((m_features.clickGuiLightTheme ? 1.0F : 0.0F) - m_clickGuiThemeProgress) *
        (1.0F - std::exp(-12.0F * delta));
    const auto mixColor = [](const ImVec4& dark, const ImVec4& light,
                             const float amount) noexcept {
        return ImVec4(dark.x + (light.x - dark.x) * amount,
                      dark.y + (light.y - dark.y) * amount,
                      dark.z + (light.z - dark.z) * amount,
                      dark.w + (light.w - dark.w) * amount);
    };
    const float theme = std::clamp(m_clickGuiThemeProgress, 0.0F, 1.0F);
    ImVec4 guiSurface = mixColor(ImVec4(0.075F, 0.068F, 0.096F, 0.98F),
                                ImVec4(0.965F, 0.956F, 0.975F, 0.98F), theme);
    ImVec4 guiRail = mixColor(ImVec4(0.055F, 0.050F, 0.073F, 1.0F),
                             ImVec4(0.918F, 0.906F, 0.938F, 1.0F), theme);
    const float guiOpacity = static_cast<float>(std::clamp(
        m_features.clickGuiOpacity, 35, 100)) / 100.0F;
    guiSurface.w = guiOpacity;
    guiRail.w = std::min(1.0F, guiOpacity + 0.03F);
    const ImVec4 guiText = mixColor(ImVec4(0.94F, 0.92F, 0.98F, 1.0F),
                                   ImVec4(0.10F, 0.09F, 0.13F, 1.0F), theme);
    const ImVec4 guiMuted = mixColor(ImVec4(0.59F, 0.56F, 0.65F, 1.0F),
                                    ImVec4(0.39F, 0.36F, 0.44F, 1.0F), theme);
    const ImVec4 guiFrame = mixColor(ImVec4(0.14F, 0.13F, 0.18F, 1.0F),
                                    ImVec4(0.88F, 0.86F, 0.90F, 1.0F), theme);
    const ImVec4 guiScrollbarTrack = mixColor(
        ImVec4(0.065F, 0.058F, 0.082F, 0.72F),
        ImVec4(0.70F, 0.70F, 0.72F, 0.82F), theme);
    const ImVec4 guiScrollbarGrab = mixColor(
        ImVec4(1.0F, 1.0F, 1.0F, 0.72F),
        ImVec4(1.0F, 1.0F, 1.0F, 0.96F), theme);
    const std::array<float, 3U> accentChannels = unpackRgb(
        m_features.clickGuiAccentColor);
    const ImVec4 guiAccent(accentChannels[0U], accentChannels[1U],
                           accentChannels[2U], 1.0F);

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
            ((!interactive||m_blacklistAddOpen)?ImGuiWindowFlags_NoInputs:0);
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
                              ImVec4(guiAccent.x, guiAccent.y, guiAccent.z, 0.76F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, guiAccent);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, guiScrollbarTrack);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, guiScrollbarGrab);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                              ImVec4(1, 1, 1, 0.90F));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1, 1, 1, 1));
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
            windowDraw->AddRectFilled(
                windowPosition,
                ImVec2(windowPosition.x + baseRailWidth * uiScale,
                       windowPosition.y + windowSize.y),
                fadedGuiColor(guiRail), 20.0F * uiScale,
                ImDrawFlags_RoundCornersLeft);
            windowDraw->AddLine(
                ImVec2(windowPosition.x + baseRailWidth * uiScale,
                       windowPosition.y + 18.0F * uiScale),
                ImVec2(windowPosition.x + baseRailWidth * uiScale,
                       windowPosition.y + windowSize.y - 18.0F * uiScale),
                fadedGuiColor(mixColor(
                    ImVec4(1, 1, 1, 0.08F), ImVec4(0, 0, 0, 0.10F), theme)));

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

            ImFont* const boldFont = m_boldFonts[static_cast<std::size_t>(
                std::clamp(m_guiScaleIndex, 0, 3))] != nullptr
                ? m_boldFonts[static_cast<std::size_t>(
                    std::clamp(m_guiScaleIndex, 0, 3))] : ImGui::GetFont();
            windowDraw->AddText(boldFont, ImGui::GetFontSize(),
                                ImVec2(windowPosition.x + 20.0F * uiScale,
                                       windowPosition.y + 17.0F * uiScale),
                                fadedGuiColor(guiText), "MC OVERLAY");
            windowDraw->AddText(ImVec2(windowPosition.x + 20.0F * uiScale,
                                       windowPosition.y + 38.0F * uiScale),
                                fadedGuiColor(guiMuted),
                                "Native workspace");

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
            const auto filtered = navigation::filter(m_featureSearch.data());
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
                default: return false;
                }
            };
            ImGui::SetCursorScreenPos(ImVec2(windowPosition.x + 6.0F * uiScale,
                windowPosition.y + 64.0F * uiScale));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            beginSmoothChild("##navigationScroll",
                ImVec2((baseRailWidth - 12.0F) * uiScale,
                       (baseGuiHeight - 112.0F) * uiScale), m_navigationScroll, delta);
            ImDrawList* const navDraw = ImGui::GetWindowDrawList();
            const ImVec2 navOrigin = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            float contentY = 0, targetNavY = 0;
            bool selectedVisible=false;
            for (const auto& row : rows) {
                if (row.page == m_clickGuiPage) { targetNavY = contentY; selectedVisible=true; }
                contentY += row.page < 0 ? 28.0F : 32.0F;
            }
            if (m_clickGuiNavPosition <= 0.0F) m_clickGuiNavPosition = targetNavY;
            m_clickGuiNavPosition += (targetNavY - m_clickGuiNavPosition) *
                (1.0F - std::exp(-15.0F * delta));
            const ImVec2 selectedMin(navOrigin.x + 2.0F * uiScale,
                navOrigin.y + m_clickGuiNavPosition * uiScale);
            if(selectedVisible) {
            navDraw->AddRectFilled(selectedMin,
                ImVec2(navOrigin.x + rowWidth, selectedMin.y + 30.0F * uiScale),
                fadedGuiColor(ImVec4(guiAccent.x, guiAccent.y, guiAccent.z, 0.18F)), 9.0F * uiScale);
            navDraw->AddRectFilled(ImVec2(selectedMin.x + 2.0F * uiScale, selectedMin.y + 7.0F * uiScale),
                ImVec2(selectedMin.x + 5.0F * uiScale, selectedMin.y + 23.0F * uiScale),
                fadedGuiColor(guiAccent), 1.5F * uiScale);
            }
            contentY = 0;
            for (const auto& row : rows) {
                const ImVec2 position(navOrigin.x + 2.0F * uiScale,
                    navOrigin.y + contentY * uiScale);
                if (row.page < 0) {
                    navDraw->AddText(boldFont, ImGui::GetFontSize() * 0.72F,
                        ImVec2(position.x + 12.0F * uiScale, position.y + 9.0F * uiScale),
                        fadedGuiColor(guiAccent), row.label);
                    contentY += 28.0F;
                    continue;
                }
                ImGui::SetCursorScreenPos(position);
                ImGui::PushID(row.page);
                if (ImGui::InvisibleButton("##nav", ImVec2(rowWidth - 2.0F * uiScale, 30.0F * uiScale)) &&
                    m_clickGuiPage != row.page) {
                    m_previousClickGuiPage = m_clickGuiPage;
                    m_clickGuiPage = row.page;
                    m_clickGuiPageProgress = 0.0F;
                }
                const bool selected = row.page == m_clickGuiPage;
                float& hover = m_clickGuiNavHover[static_cast<std::size_t>(row.page)];
                hover += ((ImGui::IsItemHovered() && !selected ? 1.0F : 0.0F) - hover) *
                    (1.0F - std::exp(-18.0F * delta));
                if (selected) hover = 0.0F;
                if (hover > 0.005F) navDraw->AddRectFilled(position,
                    ImVec2(navOrigin.x + rowWidth, position.y + 30.0F * uiScale),
                    fadedGuiColor(ImVec4(guiAccent.x, guiAccent.y, guiAccent.z, 0.08F * hover)), 9.0F * uiScale);
                ImFont* font=selected ? boldFont : ImGui::GetFont();
                ImVec2 textPosition(position.x + 14.0F * uiScale,
                    position.y + (30.0F * uiScale-ImGui::GetFontSize())*0.5F);
                const std::size_t length=std::strlen(row.label);
                for(std::size_t glyph=0;glyph<length;++glyph) {
                    const float glow=enabledPage(row.page)
                        ? navigation::glyphGlow(glyph,length,ImGui::GetTime()) : 0;
                    const ImVec4 color=mixColor(selected ? guiText : guiMuted,ImVec4(1,1,1,1),glow);
                    // A subtle shadow retains contrast for white glyphs on the
                    // light theme. The same colour is used for the WHOLE glyph.
                    if(theme>0.1F && glow>0.01F)
                        navDraw->AddText(font,ImGui::GetFontSize(),
                            ImVec2(textPosition.x+0.7F,textPosition.y+0.7F),
                            fadedGuiColor(ImVec4(0,0,0,theme*glow*0.65F)),row.label+glyph,row.label+glyph+1);
                    navDraw->AddText(font,ImGui::GetFontSize(),textPosition,
                        fadedGuiColor(color),row.label+glyph,row.label+glyph+1);
                    textPosition.x+=font->CalcTextSizeA(ImGui::GetFontSize(),1000,0,
                        row.label+glyph,row.label+glyph+1).x;
                }
                ImGui::PopID();
                contentY += 32.0F;
            }
            if(rows.empty()) { ImGui::TextDisabled("No matching features"); contentY=32; }
            ImGui::SetCursorScreenPos(ImVec2(navOrigin.x,
                navOrigin.y+(contentY+7.0F)*uiScale));
            ImGui::Dummy(ImVec2(1.0F,1.0F));
            endSmoothChild(m_navigationScroll,delta);
            ImGui::PopStyleVar();

            // Bottom-left sun/moon control is vector drawn, so it remains crisp
            // and does not depend on an icon font.
            const ImVec2 themeButtonMin(windowPosition.x + 18.0F * uiScale,
                                        windowPosition.y + (baseGuiHeight - 39.0F) * uiScale);
            ImGui::SetCursorScreenPos(themeButtonMin);
            if (ImGui::InvisibleButton("##themeToggle",
                                       ImVec2(28.0F * uiScale, 26.0F * uiScale))) {
                m_features.clickGuiLightTheme = !m_features.clickGuiLightTheme;
                changed = true;
            }
            const ImVec2 iconCenter(themeButtonMin.x + 13.0F * uiScale,
                                    themeButtonMin.y + 13.0F * uiScale);
            const ImU32 iconColor = fadedGuiColor(guiText);
            if (m_features.clickGuiLightTheme) {
                windowDraw->AddCircleFilled(iconCenter, 3.5F * uiScale, iconColor, 20);
                for (int ray = 0; ray < 8; ++ray) {
                    const float angle = static_cast<float>(ray) * 3.14159265F / 4.0F;
                    windowDraw->AddLine(
                        ImVec2(iconCenter.x + std::cos(angle) * 6.0F * uiScale,
                               iconCenter.y + std::sin(angle) * 6.0F * uiScale),
                        ImVec2(iconCenter.x + std::cos(angle) * 8.0F * uiScale,
                               iconCenter.y + std::sin(angle) * 8.0F * uiScale),
                        iconColor, 1.25F * uiScale);
                }
            } else {
                windowDraw->AddCircleFilled(iconCenter, 6.5F * uiScale, iconColor, 24);
                windowDraw->AddCircleFilled(
                    ImVec2(iconCenter.x + 3.0F * uiScale,
                           iconCenter.y - 2.0F * uiScale),
                    5.7F * uiScale, fadedGuiColor(guiRail), 24);
            }

            constexpr std::array<const char*, 24U> pageTitles{{
                "Player ESP", "Bed ESP", "Nametags", "Bed Alerts",
                "SafeWalk", "Scaffold", "Flight", "Bunny Hop", "Aim Assist",
                "Player Stats", "Diagnostics", "Blacklist", "Module List", "Interface",
                "Fireball ESP", "", "Trajectories", "",
                "Input Method", "Now Playing", "Bed Breaker", "Velocity", "FreeLook",
                "Smart Hotbar"}};
            constexpr std::array<const char*, 24U> pageDescriptions{{
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
                "Category shortcuts that follow Minecraft's own hotbar bindings"}};
            const int page = std::clamp(m_clickGuiPage, 0, 23);
            const float contentX = windowPosition.x + (baseRailWidth + 22.0F) * uiScale;
            windowDraw->AddText(boldFont, ImGui::GetFontSize() * 1.16F,
                ImVec2(contentX, windowPosition.y + 17.0F * uiScale),
                fadedGuiColor(guiText),
                pageTitles[static_cast<std::size_t>(page)]);
            windowDraw->AddText(
                ImVec2(contentX, windowPosition.y + 42.0F * uiScale),
                fadedGuiColor(guiMuted),
                pageDescriptions[static_cast<std::size_t>(page)]);
            windowDraw->AddLine(
                ImVec2(contentX, windowPosition.y + 66.0F * uiScale),
                ImVec2(windowPosition.x + (baseGuiWidth - 20.0F) * uiScale,
                       windowPosition.y + 66.0F * uiScale),
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
            default: break;
            }
            if (pageMaster != nullptr && pageMasterAnimation != nullptr) {
                ImGui::SetCursorScreenPos(ImVec2(
                    windowPosition.x + (baseGuiWidth - 126.0F) * uiScale,
                    windowPosition.y + 20.0F * uiScale));
                const bool masterChanged = animatedToggle(
                    "Enabled", *pageMaster, *pageMasterAnimation, uiScale);
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
                windowDraw->AddText(boldFont, ImGui::GetFontSize() * 0.78F,
                    ImVec2(windowPosition.x + (baseGuiWidth - 79.0F) * uiScale,
                           windowPosition.y + 27.0F * uiScale),
                    fadedGuiColor(guiAccent), "SYSTEM");
            }

            auto beginHotkeyCapture = [&](const int target) noexcept {
                m_waitingForHotkey = true;
                m_hotkeyCaptureTarget = target;
                m_hotkeyCaptureCooldownFrames = 2;
                m_hotkeyCaptureArmed = false;
                m_inputState->captureHotkey.store(false, std::memory_order_release);
                m_inputState->capturedHotkey.store(0U, std::memory_order_release);
            };
            if (m_waitingForHotkey) {
                unsigned captured = m_inputState->capturedHotkey.exchange(
                    0U, std::memory_order_acq_rel);
                if (captured != 0U) {
                    {
                        if(captured==VK_ESCAPE) captured=0U;
                        if (m_hotkeyCaptureTarget == 1) {
                            setMenuHotkey(captured);
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
                    m_inputState->captureHotkey.store(false, std::memory_order_release);
                } else {
                    const bool mouseHeld = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                        (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                        (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
                        (::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
                        (::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
                    if (mouseHeld) {
                        m_hotkeyCaptureArmed = false;
                        m_hotkeyCaptureCooldownFrames = 2;
                    } else if (m_hotkeyCaptureCooldownFrames > 0) {
                        --m_hotkeyCaptureCooldownFrames;
                    } else if (!m_hotkeyCaptureArmed) {
                        m_hotkeyCaptureArmed = true;
                        m_inputState->captureHotkey.store(true, std::memory_order_release);
                    }
                }
            }

            m_clickGuiPageProgress += (1.0F - m_clickGuiPageProgress) *
                (1.0F - std::exp(-14.0F * delta));
            const float pageEase = std::clamp(m_clickGuiPageProgress, 0.0F, 1.0F);
            const ImVec2 childPos(contentX + (1.0F - pageEase) * 14.0F * uiScale,
                                  windowPosition.y + 79.0F * uiScale);
            ImGui::SetCursorScreenPos(childPos);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pageEase);
            ImGui::PushID(page); // Each page retains its own scroll position.
            beginSmoothChild("##settingsPage",
                ImVec2((baseGuiWidth - baseRailWidth - 38.0F) * uiScale,
                       (baseGuiHeight - 96.0F) * uiScale),
                m_settingsScroll[static_cast<std::size_t>(page)], delta,
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
            const auto sectionTitle = [&](const char* text) noexcept {
                ImGui::Spacing();
                ImGui::PushFont(boldFont);
                ImGui::TextColored(guiAccent, "%s", text);
                ImGui::PopFont();
                ImGui::Spacing();
            };
            const auto hotkeyControl = [&](const char* label, const int target,
                                           const unsigned key) noexcept {
                ImGui::TextDisabled("%s", label);
                ImGui::SameLine(0.0F, 14.0F * uiScale);
                const char* buttonText = m_waitingForHotkey &&
                    m_hotkeyCaptureTarget == target ? "Press a key..." : hotkeyName(key);
                if (ImGui::Button(buttonText, ImVec2(126.0F * uiScale, 0.0F)))
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
                if (ImGui::ColorEdit3("Player box color", playerColor.data(),
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
                if (ImGui::ColorEdit3("Bed box color", bedColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.bedEspColor = packRgb(bedColor);
                    changed = true;
                }
                if (ImGui::Button("Refresh loaded beds now",
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
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiAccent);
                    char label[4]{};
                    std::snprintf(label, sizeof(label), "%d", radius);
                    if (ImGui::Button(label, ImVec2(38.0F * uiScale, 0.0F)) && !selected) {
                        m_features.bedDefenseRadius = radius;
                        changed = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                std::array<float, 3U> cardColor = unpackRgb(
                    m_features.bedDefensePanelColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (ImGui::ColorEdit3("Card background", cardColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.bedDefensePanelColor = packRgb(cardColor);
                    changed = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= ImGui::SliderInt("Card opacity",
                    &m_features.bedDefensePanelOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
            } else if (page == 2) {
                sectionTitle("PLAYER FILTER");
                changed |= animatedToggle("Show teammate nametags",
                    m_features.showTeammateNametags, m_toggleAnimation[19], uiScale);
                changed |= animatedToggle("Only nearby enemies",
                    m_features.nametagNearbyEnemiesOnly, m_toggleAnimation[20], uiScale);
                if (m_features.nametagNearbyEnemiesOnly) {
                    ImGui::SetNextItemWidth(320.0F * uiScale);
                    changed |= ImGui::SliderInt("Enemy range", &m_features.nametagRange,
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
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiAccent);
                    if (ImGui::Button(nametagSizes[static_cast<std::size_t>(sizeIndex)],
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
                if (ImGui::ColorEdit3("Nametag background", nametagColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.nametagPanelColor = packRgb(nametagColor);
                    changed = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= ImGui::SliderInt("Nametag opacity",
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
                changed |= ImGui::SliderInt("Warning range",
                    &m_features.bedThreatRadius, 3, 32, "%d blocks",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::Spacing();
                ImGui::TextDisabled("The warning remains visible while a tracked enemy stays inside the range.");
            } else if (page == 4) {
                sectionTitle("EDGE ASSIST");
                ImGui::TextWrapped(
                    "Uses Minecraft's native inset-AABB ledge rule and the player's real next-tick motion, then holds the normal sneak key state.");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Edge timing",
                    &m_features.safewalkEdgeSensitivity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Minimum look pitch",
                    &m_features.safewalkMinimumPitch, -90, 90, "%d deg",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Stand delay after placement",
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
                ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.34F, 1.0F),
                    "WARNING: Do not use this on a server. It can cause a ban.");
                ImGui::TextDisabled("Placement still requires a reachable solid neighbour.");
            } else if (page == 6) {
                sectionTitle("FLIGHT SPEED");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Speed", &m_features.flySpeedPercent,
                    10, 500, "%d%%", ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.34F, 1.0F),
                    "WARNING: Do not use this on a server. It can cause a ban.");
                ImGui::TextDisabled("This is local motion control and contains no server-correction bypass.");
            } else if (page == 7) {
                sectionTitle("AIR CONTROL");
                changed |= animatedToggle("Auto-jump on landing",
                    m_features.bhopAutoJump, m_toggleAnimation[32], uiScale);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Air speed",
                    &m_features.bhopAirSpeedPercent, 10, 300, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextColored(ImVec4(1.0F, 0.42F, 0.34F, 1.0F),
                    "WARNING: Do not use this on a server. It can cause a ban.");
                ImGui::TextDisabled("Airborne horizontal velocity follows current movement input.");
            } else if (page == 8) {
                bool lockOn=m_features.aimLockOnMode;
                const auto collapsible=[&](const std::size_t index,
                    const char* id,const char* title,const char* summary,
                    const float fallbackBodyHeight,auto&& body) noexcept {
                    const float bodyGap=8.0F*uiScale;
                    const float bodyPaddingX=19.0F*uiScale;
                    const float bodyPaddingY=13.0F*uiScale;
                    const float sectionGap=14.0F*uiScale;
                    const float storedBodyHeight=m_aimSectionBodyHeight[index]>1.0F
                        ? m_aimSectionBodyHeight[index]*uiScale
                        : (fallbackBodyHeight+20.0F)*uiScale;
                    const float fullBodyHeight=std::max(36.0F*uiScale,storedBodyHeight);
                    const ImVec2 headerMin=ImGui::GetCursorScreenPos();
                    const float headerWidth=ImGui::GetContentRegionAvail().x;
                    const ImVec2 headerSize(headerWidth,52.0F*uiScale);
                    const float parentItemSpacing=ImGui::GetStyle().ItemSpacing.y;
                    ImGui::PushID(id);
                    ImGui::InvisibleButton("##header",headerSize);
                    const bool hovered=ImGui::IsItemHovered();
                    if(ImGui::IsItemClicked())
                        m_aimSectionOpen[index]=!m_aimSectionOpen[index];
                    // Apply the click before advancing. Otherwise the click
                    // frame repeats the fully-open layout and collapse appears
                    // to hitch before its first visible step.
                    const float direction=m_aimSectionOpen[index]?1.0F:-1.0F;
                    m_aimSectionProgress[index]=CollapsibleMotion::advance(
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
                        ImGui::ColorConvertFloat4ToU32(outerSurface),cardRounding);
                    draw->AddRect(headerMin,outerMax,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(
                            guiAccent.x,guiAccent.y,guiAccent.z,
                            0.20F+0.16F*progress)),cardRounding,0,
                        std::max(1.0F,uiScale));
                    const ImVec4 surface=mixColor(guiFrame,guiAccent,
                        hovered?0.27F:(0.13F+0.06F*progress));
                    draw->AddRectFilled(headerMin,
                        ImVec2(headerMin.x+headerSize.x,headerMin.y+headerSize.y),
                        ImGui::ColorConvertFloat4ToU32(surface),cardRounding,
                        ImDrawFlags_RoundCornersAll);
                    draw->AddLine(ImVec2(headerMin.x+12.0F*uiScale,
                        headerMin.y+1.0F*uiScale),
                        ImVec2(headerMin.x+headerSize.x-12.0F*uiScale,
                        headerMin.y+1.0F*uiScale),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(
                            guiAccent.x,guiAccent.y,guiAccent.z,0.32F)),
                        std::max(1.0F,uiScale));
                    draw->AddText(boldFont,ImGui::GetFontSize()*0.88F,
                        ImVec2(headerMin.x+18.0F*uiScale,
                               headerMin.y+9.0F*uiScale),
                        ImGui::ColorConvertFloat4ToU32(guiText),title);
                    draw->AddText(ImVec2(headerMin.x+18.0F*uiScale,
                        headerMin.y+31.0F*uiScale),
                        ImGui::ColorConvertFloat4ToU32(guiMuted),summary);
                    const ImVec2 centre(headerMin.x+headerSize.x-22.0F*uiScale,
                        headerMin.y+headerSize.y*0.5F);
                    const float p=progress;
                    const ImVec2 a(centre.x+(-4.0F*(1.0F-p)-5.0F*p)*uiScale,
                        centre.y+(-5.0F*(1.0F-p)-2.0F*p)*uiScale);
                    const ImVec2 b(centre.x,centre.y+(0.0F*(1.0F-p)+4.0F*p)*uiScale);
                    const ImVec2 c(centre.x+(4.0F*(1.0F-p)+5.0F*p)*uiScale,
                        centre.y+(5.0F*(1.0F-p)-2.0F*p)*uiScale);
                    draw->AddLine(a,b,ImGui::ColorConvertFloat4ToU32(guiMuted),
                        1.7F*uiScale);
                    draw->AddLine(b,c,ImGui::ColorConvertFloat4ToU32(guiMuted),
                        1.7F*uiScale);

                    {
                        ImGuiWindow* const parentWindow=ImGui::GetCurrentWindow();
                        const float parentCursorMaxBefore=parentWindow->DC.CursorMaxPos.y;
                        const ImVec2 bodyStart(headerMin.x,
                            headerMin.y+headerSize.y+bodyGap);
                        ImGui::SetCursorScreenPos(bodyStart);
                        // Parent ClickGUI alpha is applied once to the completed
                        // draw lists. This local alpha only represents accordion
                        // openness and therefore composes without overriding the
                        // parent's close animation.
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,progress*progress);
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
                        if(phase>=0.999F||m_aimSectionBodyHeight[index]<=1.0F)
                            m_aimSectionBodyHeight[index]=measuredPixels/
                                std::max(0.01F,uiScale);
                        ImGui::EndChild();
                        if(spotlightEase>0.985F)
                            appendTransientSoftBlur(bodyDraw,1.0F-progress,uiScale);
                        if(progress<0.985F) ImGui::EndDisabled();
                        ImGui::PopStyleColor();
                        ImGui::PopStyleVar(2);
                        // The full-height body keeps its contents and text
                        // metrics stable while clipping reveals them. Rebase
                        // the parent layout to the exact shared animated edge
                        // so ScrollMax and all following cards move continuously.
                        parentWindow->DC.CursorMaxPos.y=std::max(
                            parentCursorMaxBefore,outerMax.y);
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
                    if(smoothSelected) ImGui::PushStyleColor(ImGuiCol_Button,guiAccent);
                    if(ImGui::Button("Smooth Aim",ImVec2(modeButtonWidth,0))&&lockOn) {
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
                    if(lockSelected) ImGui::PushStyleColor(ImGuiCol_Button,guiAccent);
                    if(ImGui::Button("Lock On",ImVec2(modeButtonWidth,0))&&!lockOn) {
                        m_features.aimLockOnMode=true;lockOn=true;changed=true;
                    }
                    if(lockSelected) ImGui::PopStyleColor();
                    ImGui::BeginDisabled(!lockOn);
                    changed|=animatedToggle("Silent Lock · keep camera free",
                        m_features.aimSilentLock,m_toggleAnimation[46],uiScale);
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Silent Lock redirects only real left-click attack intents; it never auto-attacks.");
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
                    changed|=ImGui::SliderInt("Hold attack rate",
                        &m_features.aimAttackCps,1,20,"%d CPS",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::EndDisabled();
                    if(m_features.aimSilentLock&&!snapshot.silentAimAvailable)
                        ImGui::TextColored(ImVec4(1,.65F,.25F,1),
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
                    changed|=ImGui::SliderInt("Minimum distance",
                        &m_features.aimMinimumDistance,0,
                        std::max(0,m_features.aimMaximumDistance-1),"%d blocks",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=ImGui::SliderInt("Maximum distance",
                        &m_features.aimMaximumDistance,
                        std::max(1,m_features.aimMinimumDistance),128,"%d blocks",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=ImGui::SliderInt("Field of view",
                        &m_features.aimFovDegrees,1,360,"%d deg",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::TextDisabled("Only validated enemy-player candidates are eligible.");
                });

                collapsible(3U,"aimResponse","RESPONSE & DIAGNOSTICS",
                    lockOn?"Exact tracking":"Smoothed response",115.0F,[&] {
                    ImGui::BeginDisabled(lockOn);
                    ImGui::SetNextItemWidth(320.0F*uiScale);
                    changed|=ImGui::SliderInt("Smooth speed",
                        &m_features.aimSpeedPercent,1,100,"%d%%",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::EndDisabled();
                    changed|=ImGui::Checkbox("Silent file diagnostics",
                        &m_features.silentFileDebug);
                    changed|=ImGui::Checkbox("Silent chat diagnostics",
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
                if (black) ImGui::PushStyleColor(ImGuiCol_Button, guiAccent);
                if (ImGui::Button("Black", ImVec2(104.0F * uiScale, 0.0F)) && !black) {
                    m_features.hypixelPanelColor = 0x000000U;
                    changed = true;
                }
                if (black) ImGui::PopStyleColor();
                ImGui::SameLine();
                if (!black) ImGui::PushStyleColor(ImGuiCol_Button, guiAccent);
                if (ImGui::Button("White", ImVec2(104.0F * uiScale, 0.0F)) && black) {
                    m_features.hypixelPanelColor = 0xFFFFFFU;
                    changed = true;
                }
                if (!black) ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= ImGui::SliderInt("Panel opacity",
                    &m_features.hypixelPanelOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                std::array<float, 3U> statsRailColor = unpackRgb(
                    m_features.hypixelRailColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (ImGui::ColorEdit3("STATS rail", statsRailColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.hypixelRailColor = packRgb(statsRailColor);
                    changed = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                changed |= ImGui::SliderInt("Rail opacity",
                    &m_features.hypixelRailOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextDisabled("Font size");
                constexpr std::array<const char*, 4U> statsFonts{{"S", "M", "L", "XL"}};
                for (int fontIndex = 0; fontIndex < 4; ++fontIndex) {
                    if (fontIndex != 0) ImGui::SameLine();
                    const bool selected = fontIndex == m_features.hypixelPanelFontIndex;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiAccent);
                    if (ImGui::Button(statsFonts[static_cast<std::size_t>(fontIndex)],
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
                if (ImGui::Button("Reset position & size")) {
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
                if (ImGui::Button("Query", ImVec2(90.0F * uiScale, 0.0F))) {
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
                if (ImGui::ColorEdit3("Panel background", blacklistColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_blacklist.panelColor = packRgb(blacklistColor);
                    blacklistSettingsChanged = true;
                }
                ImGui::SetNextItemWidth(300.0F * uiScale);
                blacklistSettingsChanged |= ImGui::SliderInt(
                    "Panel opacity", &m_blacklist.panelOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(300.0F * uiScale);
                blacklistSettingsChanged |= ImGui::SliderInt(
                    "Panel content size", &m_blacklist.contentScale, 80, 200,
                    "%d%%", ImGuiSliderFlags_AlwaysClamp);
                if (blacklistSettingsChanged) publishBlacklistSettings();

                sectionTitle("ADD RECENT PLAYER");
                if (ImGui::Button("+ Add player",
                                  ImVec2(130.0F * uiScale, 0.0F))) {
                    m_blacklistAddOpen = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Opens a separate animated dialog.");

                sectionTitle("SAVED PLAYERS");
                ImGui::BeginChild("##blacklistEntries", ImVec2(0.0F, 190.0F * uiScale),
                                  false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                for (std::uint32_t index = 0U; index < m_blacklist.count; ++index) {
                    const BlacklistEntry& entry = m_blacklist.entries[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::PushFont(boldFont);
                    ImGui::TextUnformatted(entry.name.data());
                    ImGui::PopFont();
                    if (entry.nick) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.84F, 0.48F, 1.0F, 1.0F), "NICK");
                    }
                    ImGui::TextWrapped("%s", entry.reason.data());
                    bool warning = entry.warnOnEncounter;
                    if (ImGui::Checkbox("Encounter warning", &warning)) {
                        m_blacklistAction = {};
                        m_blacklistAction.type = BlacklistAction::Type::Warning;
                        std::snprintf(m_blacklistAction.key.data(),
                            m_blacklistAction.key.size(), "%s", entry.key.data());
                        m_blacklistAction.warnOnEncounter = warning;
                        m_blacklistActionDirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete")) {
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
            } else if (page == 12) {
                sectionTitle("ENABLED MODULE LIST");
                ImGui::TextWrapped(
                    "Shows enabled modules as animated text without a background. Drag the list while the Click GUI is open.");
                std::array<float, 3U> textColor = unpackRgb(m_features.textGuiColor);
                ImGui::SetNextItemWidth(220.0F * uiScale);
                if (ImGui::ColorEdit3("Flow base color", textColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.textGuiColor = packRgb(textColor);
                    changed = true;
                }
                if (ImGui::Button("Reset text position",
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
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiAccent);
                    if (ImGui::Button(alignLabels[static_cast<std::size_t>(alignment)],
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
                if (ImGui::ColorEdit3("Fireball color", fireballColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.fireballEspColor = packRgb(fireballColor);
                    changed = true;
                }
                ImGui::TextColored(ImVec4(1.0F, 0.58F, 0.30F, 1.0F),
                    "LOCAL WORLD ONLY: automatically disabled on every multiplayer server.");
            } else if (page == 15) {
                sectionTitle("JUMP IMPULSE");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Horizontal speed",
                    &m_features.longJumpSpeedPercent, 25, 250, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextWrapped(
                    "Applies a forward jump impulse at the next grounded movement step, with a bounded cooldown.");
                ImGui::TextColored(ImVec4(1.0F, 0.58F, 0.30F, 1.0F),
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
                changed |= ImGui::SliderInt("Local reach",
                    &m_features.localMobReach, 3, 10, "%d blocks",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Attack interval",
                    &m_features.localAttackDelayMs, 100, 1500, "%d ms",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextColored(ImVec4(1.0F, 0.58F, 0.30F, 1.0F),
                    "INTEGRATED SINGLE-PLAYER ONLY: Agent hard-disables this page elsewhere.");
                ImGui::TextWrapped(
                    "The attack helper accepts only non-player hostile candidates; it never targets players.");
            } else if (page == 18) {
                sectionTitle("WINDOWS INPUT METHOD BRIDGE");
                if (ImGui::Button("Adjust panel position", ImVec2(260.0F * uiScale, 0)))
                    m_imePositionEditing = true;
                ImGui::TextWrapped(
                    "Mirrors the active Windows input method, live composition text and the current candidate page into the OpenGL frame. This keeps candidates visible in exclusive fullscreen without synthesizing input.");
                ImGui::Spacing();
                ImGui::TextDisabled(
                    "The original IME remains the text owner. The overlay only observes WM_IME messages and never commits or replaces characters.");
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.42F, 0.84F, 0.78F, 1.0F),
                    "Candidate card appears at the top-center while composing and briefly after an input-method switch.");
            } else if (page == 19) {
                bool mediaChanged = false;
                sectionTitle("NOW PLAYING SURFACE");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                mediaChanged |= ImGui::SliderInt("Card scale",
                    &m_mediaSettings.scalePercent, 35, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextDisabled(
                    "Scale is continuous and applies uniformly to artwork, type, spectrum and controls.");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                mediaChanged |= ImGui::SliderInt("Card opacity",
                    &m_mediaSettings.opacity, 20, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                mediaChanged |= ImGui::SliderInt("Spectrum opacity",
                    &m_mediaSettings.spectrumOpacity, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                std::array<float, 3U> mediaColor = unpackRgb(
                    m_mediaSettings.panelColor);
                ImGui::SetNextItemWidth(240.0F * uiScale);
                if (ImGui::ColorEdit3("Card color", mediaColor.data(),
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_DisplayRGB)) {
                    m_mediaSettings.panelColor = packRgb(mediaColor);
                    mediaChanged = true;
                }
                if (ImGui::Button("Reset card position",
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
                ImGui::TextColored(ImVec4(1.0F, 0.58F, 0.30F, 1.0F),
                    "INTEGRATED SINGLE-PLAYER ONLY: automatically disabled on multiplayer servers.");
            } else if (page == 21) {
                sectionTitle("RESPONSE PROFILE");
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Probability",
                    &m_features.localVelocityProbability,0,100,"%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Horizontal retained",
                    &m_features.localVelocityPercent,0,100,"%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Vertical retained",
                    &m_features.localVelocityVerticalPercent,0,100,"%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextDisabled("100%% keeps vanilla knockback; 0%% removes that component.");
                ImGui::TextColored(ImVec4(1.0F,0.58F,0.30F,1.0F),
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
                constexpr std::array<const char*,3U> actionLabels{{
                    "Normal slot","Sword","Blocks"}};
                const float gap=10.0F*uiScale;
                const float available=ImGui::GetContentRegionAvail().x;
                const float cardWidth=std::max(120.0F*uiScale,
                    (available-gap*2.0F)/3.0F);
                for(std::size_t slot=0;slot<m_features.smartHotbarActions.size();
                    ++slot) {
                    if(slot%3U!=0U) ImGui::SameLine(0.0F,gap);
                    ImGui::PushID(static_cast<int>(slot));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,12.0F*uiScale);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(14.0F*uiScale,12.0F*uiScale));
                    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        mixColor(ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.095F),
                                 ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.065F),theme));
                    ImGui::BeginChild("##smartHotbarCard",
                        ImVec2(cardWidth,94.0F*uiScale),true,
                        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
                    ImGui::PushFont(boldFont);
                    ImGui::Text("HOTBAR %u",static_cast<unsigned>(slot+1U));
                    ImGui::PopFont();
                    ImGui::TextDisabled("Minecraft binding");
                    int& action=m_features.smartHotbarActions[slot];
                    action=std::clamp(action,0,2);
                    ImGui::SetNextItemWidth(-1.0F);
                    if(ImGui::Combo("##category",&action,actionLabels.data(),
                                    static_cast<int>(actionLabels.size())))
                        changed=true;
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar(2);
                    ImGui::PopID();
                    if(slot%3U==2U&&slot+1U<m_features.smartHotbarActions.size())
                        ImGui::Spacing();
                }
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_TextDisabled,guiMuted);
                ImGui::TextWrapped(
                    "If the category is already in the hotbar it is selected directly. Otherwise the first matching main-inventory stack is swapped into the pressed logical slot.");
                ImGui::PopStyleColor();
            } else {
                sectionTitle("INTERFACE SIZE");
                constexpr std::array<const char*, 4U> sizeLabels{"S", "M", "L", "XL"};
                for (int sizeIndex = 0; sizeIndex < 4; ++sizeIndex) {
                    if (sizeIndex != 0) ImGui::SameLine();
                    const bool selected = m_guiScaleIndex == sizeIndex;
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, guiAccent);
                    if (ImGui::Button(sizeLabels[static_cast<std::size_t>(sizeIndex)],
                        ImVec2(64.0F * uiScale, 0.0F)) && !selected) {
                        m_guiScaleIndex = sizeIndex;
                        m_guiScaleDirty = true;
                    }
                    if (selected) ImGui::PopStyleColor();
                }
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Window width",
                    &m_features.clickGuiWidthPercent, 80, 150, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Window height",
                    &m_features.clickGuiHeightPercent, 80, 150, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Gaussian background blur",
                    &m_features.clickGuiBlur, 0, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetNextItemWidth(320.0F * uiScale);
                changed |= ImGui::SliderInt("Surface opacity",
                    &m_features.clickGuiOpacity, 35, 100, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp);
                sectionTitle("INPUT");
                hotkeyControl("Open Click GUI", 1, m_menuHotkey);
                ImGui::TextDisabled("ESC closes the GUI and restores Minecraft mouse capture.");
                sectionTitle("THEME");
                ImGui::TextWrapped("Use the sun/moon button at the bottom-left to switch between the light and dark interface.");
                std::array<float, 3U> accentColor = unpackRgb(
                    m_features.clickGuiAccentColor);
                ImGui::SetNextItemWidth(240.0F * uiScale);
                if (ImGui::ColorEdit3("Theme accent", accentColor.data(),
                        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB)) {
                    m_features.clickGuiAccentColor = packRgb(accentColor);
                    changed = true;
                }
                sectionTitle("SERVER SAFETY");
                changed |= animatedToggle(
                    "Allow movement modules on Hypixel",
                    m_features.allowHypixelMovement,
                    m_toggleAnimation[34], uiScale);
                ImGui::TextColored(ImVec4(1.0F, 0.34F, 0.28F, 1.0F),
                    "DANGER: Fly, BHop and Scaffold can cause a server ban.");
                ImGui::TextWrapped(
                    "By default these three modules are force-disabled whenever the current server address is Hypixel. Enable this exception only if you explicitly accept that risk.");
            }
            // Preserve a full baseline below the final control so exact
            // bottom snapping never clips half of its label.
            ImGui::Dummy(ImVec2(1.0F,7.0F*uiScale));
            endSmoothChild(m_settingsScroll[static_cast<std::size_t>(page)],delta);
            ImGui::PopID();
            ImGui::PopStyleVar();

            // The renderer applies the same guard as AgentRuntime so a click
            // cannot leave a high-risk module enabled for even one rendered
            // frame before the next runtime snapshot arrives.
            if (snapshot.hypixelServer && !m_features.allowHypixelMovement &&
                (m_features.scaffoldEnabled || m_features.flyEnabled ||
                 m_features.bhopEnabled)) {
                if (m_features.scaffoldEnabled && !featuresBefore.scaffoldEnabled)
                    enqueueMessage(
                        "WARNING: Scaffold can cause a server ban. Use only offline.",
                        false);
                if (m_features.flyEnabled && !featuresBefore.flyEnabled)
                    enqueueMessage(
                        "WARNING: Fly can cause a server ban. Use only offline.",
                        false);
                if (m_features.bhopEnabled && !featuresBefore.bhopEnabled)
                    enqueueMessage(
                        "WARNING: BHop can cause a server ban. Use only offline.",
                        false);
                m_features.scaffoldEnabled = false;
                m_features.flyEnabled = false;
                m_features.bhopEnabled = false;
                enqueueMessage(
                    "BLOCKED: movement module disabled on Hypixel. See Interface / Server Safety.",
                    false);
                changed = true;
            }
            m_featureSettingsDirty = m_featureSettingsDirty || changed;
            if (changed) enqueueFeatureToasts(featuresBefore, m_features);
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
            appendTransientSoftBlur(clickGuiDrawForDiffusion,
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
                    appendTransientSoftBlur(child->DrawList,
                        1.0F-spotlightEase,uiScale);
                }
            }
        }
        ImGui::PopStyleColor(15);
        ImGui::PopStyleVar(5);
    }

    // Adding a recent player is intentionally a separate modal surface. It no
    // longer expands the Blacklist settings page and therefore cannot disturb
    // that page's scroll position or make the form feel visually mixed with
    // persistent settings.
    if (!interactive) m_blacklistAddOpen = false;
    advancePresentationSpring(m_blacklistAddProgress,
                              m_blacklistAddVelocity,
                              m_blacklistAddOpen ? 1.0F : 0.0F, delta);
    if (m_blacklistAddProgress > 0.005F) {
        const float modalProgress = std::clamp(m_blacklistAddProgress, 0.0F, 1.0F);
        const float modalEase = modalProgress * modalProgress *
                                (3.0F - 2.0F * modalProgress);
        const float modalScale = 1.12F - 0.12F * m_blacklistAddProgress;
        const ImVec2 modalSize(480.0F * uiScale, 438.0F * uiScale);
        const ImVec2 modalPosition(
            std::max(6.0F, (io.DisplaySize.x - modalSize.x) * 0.5F),
            std::max(6.0F, (io.DisplaySize.y - modalSize.y) * 0.5F));
        const ImVec2 modalCenter(modalPosition.x + modalSize.x * 0.5F,
                                 modalPosition.y + modalSize.y * 0.5F);
        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("##BlacklistAddModalBlocker", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav)) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(0.0F, 0.0F), io.DisplaySize,
                IM_COL32(5, 4, 9, static_cast<int>(82.0F * modalEase)));
            ImGui::InvisibleButton("##blacklistModalOutside", io.DisplaySize);
            if (interactive && modalProgress > 0.985F &&
                ImGui::IsItemClicked(ImGuiMouseButton_Left))
                m_blacklistAddOpen = false;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::SetNextWindowPos(modalPosition, ImGuiCond_Always);
        ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(22.0F * uiScale, 20.0F * uiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0F * uiScale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,ImVec2(0.5F,0.5F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, guiSurface);
        ImGui::PushStyleColor(ImGuiCol_Text, guiText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, guiMuted);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, guiFrame);
        ImGui::PushStyleColor(ImGuiCol_Button, guiFrame);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(guiAccent.x, guiAccent.y, guiAccent.z, 0.76F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, guiAccent);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, guiAccent);
        ImGuiWindowFlags modalFlags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (!interactive || modalProgress < 0.985F)
            modalFlags |= ImGuiWindowFlags_NoInputs;
        ImDrawList* modalDraw = nullptr;
        ImGuiWindow* modalRoot = nullptr;
        int modalVertexEnd = 0;
        if (ImGui::Begin("##BlacklistAddDialog", nullptr, modalFlags)) {
            modalDraw = ImGui::GetWindowDrawList();
            modalRoot=ImGui::GetCurrentWindow();
            ImFont* const modalBold = m_boldFonts[static_cast<std::size_t>(
                std::clamp(m_guiScaleIndex, 0, 3))] != nullptr
                ? m_boldFonts[static_cast<std::size_t>(
                    std::clamp(m_guiScaleIndex, 0, 3))] : ImGui::GetFont();
            ImGui::PushFont(modalBold);
            ImGui::TextUnformatted("Add to blacklist");
            ImGui::PopFont();
            ImGui::SameLine(modalSize.x - 63.0F * uiScale);
            if (ImGui::Button("X", ImVec2(30.0F * uiScale,
                                           28.0F * uiScale)))
                m_blacklistAddOpen = false;
            ImGui::TextDisabled("Create a clear record from a player observed in this session.");
            ImGui::Dummy(ImVec2(1.0F,8.0F*uiScale));
            ImGui::PushFont(modalBold);
            ImGui::TextColored(guiAccent,"RECENT PLAYER");
            ImGui::PopFont();
            const char* preview = "Select a recent player";
            if (m_blacklistSelectedPlayer >= 0 &&
                static_cast<std::uint32_t>(m_blacklistSelectedPlayer) <
                    snapshot.playerCount) {
                preview = snapshot.players[static_cast<std::size_t>(
                    m_blacklistSelectedPlayer)].name.data();
            }
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize,1.0F*uiScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.0F*uiScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                ImVec2(7.0F*uiScale,7.0F*uiScale));
            ImGui::PushStyleColor(ImGuiCol_PopupBg,guiRail);
            ImGui::PushStyleColor(ImGuiCol_Border,
                ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.64F));
            ImGui::PushStyleColor(ImGuiCol_Header,
                ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.28F));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.48F));
            ImGui::PushStyleColor(ImGuiCol_FrameBg,mixColor(guiFrame,guiAccent,0.10F));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                mixColor(guiFrame,guiAccent,0.18F));
            if (ImGui::BeginCombo("##blacklistRecentModal", preview)) {
                for (std::uint32_t index = 0U; index < snapshot.playerCount; ++index) {
                    const PlayerIdentity& identity = snapshot.players[index];
                    const bool selected = static_cast<int>(index) ==
                                          m_blacklistSelectedPlayer;
                    char label[72]{};
                    std::snprintf(label, sizeof(label), "%s%s",
                        identity.name.data(), identity.uuid[0U] == '\0'
                            ? "   NICK / ID ONLY" : "   UUID LINKED");
                    if (ImGui::Selectable(label, selected,
                            ImGuiSelectableFlags_None,ImVec2(0,30.0F*uiScale))) {
                        m_blacklistSelectedPlayer = static_cast<int>(index);
                        m_blacklistIdOnlyNick = identity.uuid[0U] == '\0';
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleColor(6);
            ImGui::PopStyleVar(3);

            const bool selectionValid = m_blacklistSelectedPlayer >= 0 &&
                static_cast<std::uint32_t>(m_blacklistSelectedPlayer) <
                    snapshot.playerCount;
            if(selectionValid) {
                const PlayerIdentity& selected=snapshot.players[
                    static_cast<std::size_t>(m_blacklistSelectedPlayer)];
                const ImVec2 identityMin=ImGui::GetCursorScreenPos();
                const ImVec2 identityMax(identityMin.x+ImGui::GetContentRegionAvail().x,
                    identityMin.y+42.0F*uiScale);
                ImGui::GetWindowDrawList()->AddRectFilled(identityMin,identityMax,
                    ImGui::ColorConvertFloat4ToU32(guiFrame),9.0F*uiScale);
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(identityMin.x+20.0F*uiScale,identityMin.y+21.0F*uiScale),
                    11.0F*uiScale,ImGui::ColorConvertFloat4ToU32(
                        ImVec4(guiAccent.x,guiAccent.y,guiAccent.z,0.72F)),20);
                ImGui::GetWindowDrawList()->AddText(modalBold,ImGui::GetFontSize(),
                    ImVec2(identityMin.x+39.0F*uiScale,identityMin.y+6.0F*uiScale),
                    ImGui::ColorConvertFloat4ToU32(guiText),selected.name.data());
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(identityMin.x+39.0F*uiScale,identityMin.y+23.0F*uiScale),
                    ImGui::ColorConvertFloat4ToU32(guiMuted),
                    selected.uuid[0U]?"Identity linked by UUID":
                        "No UUID available; this may be a nick");
                ImGui::Dummy(ImVec2(1.0F,47.0F*uiScale));
            } else ImGui::Dummy(ImVec2(1.0F,5.0F*uiScale));

            ImGui::PushFont(modalBold);
            ImGui::TextColored(guiAccent,"NOTE");
            ImGui::PopFont();
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextMultiline("##blacklistReasonModal",
                m_blacklistReasonInput.data(),m_blacklistReasonInput.size(),
                ImVec2(-1.0F,62.0F*uiScale));
            if (m_blacklist.presetCount > 0U) {
                ImGui::TextDisabled("Quick notes");
                for (std::uint32_t index = 0U;
                     index < m_blacklist.presetCount; ++index) {
                    const float buttonWidth=ImGui::CalcTextSize(
                        m_blacklist.presets[index].data()).x+18.0F*uiScale;
                    if(index!=0U && ImGui::GetCursorPosX()+buttonWidth<
                        ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::SmallButton(m_blacklist.presets[index].data()))
                        std::snprintf(m_blacklistReasonInput.data(),
                            m_blacklistReasonInput.size(), "%s",
                            m_blacklist.presets[index].data());
                    ImGui::PopID();
                }
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg,mixColor(guiRail,guiSurface,0.35F));
            ImGui::BeginChild("##blacklistOptions",ImVec2(-1.0F,48.0F*uiScale),
                false,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
            const auto optionCard=[&](const char* label,bool& value,
                                      const float width) noexcept {
                const ImVec2 cardMin=ImGui::GetCursorScreenPos();
                const ImVec2 cardSize(width,38.0F*uiScale);
                ImGui::PushID(label);
                ImGui::InvisibleButton("##option",cardSize);
                if(ImGui::IsItemClicked()) value=!value;
                const bool hovered=ImGui::IsItemHovered();
                ImDrawList* const optionDraw=ImGui::GetWindowDrawList();
                const ImVec4 cardColor=mixColor(guiFrame,guiAccent,
                    value?0.20F:(hovered?0.10F:0.035F));
                optionDraw->AddRectFilled(cardMin,
                    ImVec2(cardMin.x+cardSize.x,cardMin.y+cardSize.y),
                    ImGui::ColorConvertFloat4ToU32(cardColor),9.0F*uiScale);
                optionDraw->AddRect(cardMin,
                    ImVec2(cardMin.x+cardSize.x,cardMin.y+cardSize.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(guiAccent.x,
                        guiAccent.y,guiAccent.z,value?0.62F:0.24F)),9.0F*uiScale,
                    0,std::max(1.0F,uiScale));
                const ImVec2 checkMin(cardMin.x+10.0F*uiScale,
                    cardMin.y+11.0F*uiScale);
                const ImVec2 checkMax(checkMin.x+16.0F*uiScale,
                    checkMin.y+16.0F*uiScale);
                optionDraw->AddRectFilled(checkMin,checkMax,
                    ImGui::ColorConvertFloat4ToU32(value?guiAccent:
                        mixColor(guiSurface,guiText,0.10F)),4.0F*uiScale);
                optionDraw->AddRect(checkMin,checkMax,
                    ImGui::ColorConvertFloat4ToU32(value?guiAccent:
                        ImVec4(guiText.x,guiText.y,guiText.z,0.52F)),4.0F*uiScale,
                    0,std::max(1.0F,uiScale));
                if(value) {
                    const ImU32 checkColor=IM_COL32(255,255,255,245);
                    optionDraw->AddLine(ImVec2(checkMin.x+3.5F*uiScale,
                        checkMin.y+8.3F*uiScale),ImVec2(checkMin.x+7.0F*uiScale,
                        checkMin.y+12.0F*uiScale),checkColor,1.8F*uiScale);
                    optionDraw->AddLine(ImVec2(checkMin.x+7.0F*uiScale,
                        checkMin.y+12.0F*uiScale),ImVec2(checkMin.x+13.0F*uiScale,
                        checkMin.y+4.3F*uiScale),checkColor,1.8F*uiScale);
                }
                const ImVec2 textSize=ImGui::CalcTextSize(label);
                optionDraw->AddText(ImVec2(checkMax.x+9.0F*uiScale,
                    cardMin.y+(cardSize.y-textSize.y)*0.5F),
                    ImGui::ColorConvertFloat4ToU32(guiText),label);
                ImGui::PopID();
            };
            ImGui::SetCursorPos(ImVec2(5.0F*uiScale,5.0F*uiScale));
            const float optionGap=7.0F*uiScale;
            const float available=ImGui::GetContentRegionAvail().x-5.0F*uiScale;
            const bool twoOptions=m_blacklist.allowIdOnlyNicks;
            const float optionWidth=twoOptions?(available-optionGap)*0.5F:available;
            optionCard("Warn on encounter",m_blacklistWarnOnEncounter,optionWidth);
            if(twoOptions) {
                ImGui::SameLine(0.0F,optionGap);
                optionCard("Allow ID-only nick",m_blacklistIdOnlyNick,optionWidth);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(modalSize.y - 58.0F * uiScale);
            const float footerWidth=ImGui::GetContentRegionAvail().x;
            if (ImGui::Button("Cancel", ImVec2((footerWidth-10.0F*uiScale)*0.38F,
                    34.0F * uiScale)))
                m_blacklistAddOpen = false;
            ImGui::SameLine();
            if (!selectionValid) ImGui::BeginDisabled();
            if (ImGui::Button("Add player",
                    ImVec2((footerWidth-10.0F*uiScale)*0.62F,34.0F*uiScale)) &&
                selectionValid) {
                const PlayerIdentity& identity = snapshot.players[
                    static_cast<std::size_t>(m_blacklistSelectedPlayer)];
                m_blacklistAction = {};
                m_blacklistAction.type = BlacklistAction::Type::Add;
                std::snprintf(m_blacklistAction.name.data(),
                    m_blacklistAction.name.size(), "%s", identity.name.data());
                std::snprintf(m_blacklistAction.uuid.data(),
                    m_blacklistAction.uuid.size(), "%s", identity.uuid.data());
                std::snprintf(m_blacklistAction.reason.data(),
                    m_blacklistAction.reason.size(), "%s",
                    m_blacklistReasonInput[0U] == '\0'
                        ? "No reason supplied" : m_blacklistReasonInput.data());
                m_blacklistAction.idOnlyNick = m_blacklistIdOnlyNick;
                m_blacklistAction.warnOnEncounter = m_blacklistWarnOnEncounter;
                m_blacklistActionDirty = true;
                m_blacklistAddOpen = false;
                m_blacklistSelectedPlayer = -1;
                m_blacklistReasonInput.fill('\0');
            }
            if (!selectionValid) ImGui::EndDisabled();
            // SetCursorPosY deliberately extends to the modal footer. Submit a
            // final bounded item so Dear ImGui can validate the content extent.
            ImGui::Dummy(ImVec2(1.0F, 1.0F));
            modalVertexEnd = modalDraw->VtxBuffer.Size;
        }
        ImGui::End();
        ImGui::PopStyleColor(8);
        ImGui::PopStyleVar(5);
        if (modalDraw != nullptr) {
            modalVertexEnd = std::clamp(modalVertexEnd, 0,
                                        modalDraw->VtxBuffer.Size);
            const bool scaleModal=std::abs(modalScale-1.0F)>0.0001F;
            const float alphaScale=std::clamp(modalEase,0.0F,1.0F);
            constexpr ImU32 alphaMask=static_cast<ImU32>(0xFFU)<<IM_COL32_A_SHIFT;
            const auto transformModalVertices=[&](ImDrawList* const list,
                                                  int begin,int end) noexcept {
                if(list==nullptr) return;
                begin=std::clamp(begin,0,list->VtxBuffer.Size);
                end=std::clamp(end,begin,list->VtxBuffer.Size);
                for(int vertexIndex=begin;vertexIndex<end;++vertexIndex) {
                    ImDrawVert& vertex=list->VtxBuffer[vertexIndex];
                    if(scaleModal) {
                        vertex.pos.x=modalCenter.x+
                            (vertex.pos.x-modalCenter.x)*modalScale;
                        vertex.pos.y=modalCenter.y+
                            (vertex.pos.y-modalCenter.y)*modalScale;
                    }
                    const unsigned sourceAlpha=(vertex.col>>IM_COL32_A_SHIFT)&0xFFU;
                    const unsigned fadedAlpha=static_cast<unsigned>(std::lround(
                        static_cast<float>(sourceAlpha)*alphaScale));
                    vertex.col=(vertex.col&~alphaMask)|
                        ((static_cast<ImU32>(std::min(fadedAlpha,255U)))<<IM_COL32_A_SHIFT);
                }
            };
            const auto transformModalClips=[&](ImDrawList* const list) noexcept {
                if(list==nullptr||!scaleModal) return;
                for(ImDrawCmd& command:list->CmdBuffer) {
                    command.ClipRect.x=modalCenter.x+
                        (command.ClipRect.x-modalCenter.x)*modalScale;
                    command.ClipRect.y=modalCenter.y+
                        (command.ClipRect.y-modalCenter.y)*modalScale;
                    command.ClipRect.z=modalCenter.x+
                        (command.ClipRect.z-modalCenter.x)*modalScale;
                    command.ClipRect.w=modalCenter.y+
                        (command.ClipRect.w-modalCenter.y)*modalScale;
                }
            };
            transformModalVertices(modalDraw,0,modalVertexEnd);
            transformModalClips(modalDraw);
            appendTransientSoftBlur(modalDraw,1.0F-modalEase,uiScale);
            if(modalRoot!=nullptr) {
                ImGuiContext& imguiState=*ImGui::GetCurrentContext();
                for(ImGuiWindow* child:imguiState.Windows) {
                    if(child==nullptr||child==modalRoot||!child->Active) continue;
                    const bool ownedByModal=child->RootWindow==modalRoot ||
                        child->RootWindowPopupTree==modalRoot;
                    if(!ownedByModal) continue;
                    transformModalVertices(child->DrawList,0,
                        child->DrawList->VtxBuffer.Size);
                    transformModalClips(child->DrawList);
                    appendTransientSoftBlur(child->DrawList,
                        1.0F-modalEase,uiScale);
                }
            }
        }
    }

    // Text GUI is deliberately a text-only HUD: no window surface is drawn.
    // A single invisible hit target owns dragging while the Click GUI is open,
    // avoiding competing per-row hover/cursor state.
    if (m_features.textGuiEnabled) {
        struct TextModule { const char* name; const char* mode; bool enabled; };
        const char* const aimMode=m_features.aimSilentLock ? "Silent Lock"
            : m_features.aimLockOnMode ? "Lock On" : "Smooth";
        const std::array<TextModule, 20U> modules{{
            {"Player ESP", "", m_features.entityEspEnabled},
            {"Bed ESP", "", m_features.bedEspEnabled},
            {"Nametag", "", m_features.nametagEnabled},
            {"Bed Alert", "", m_features.bedThreatAlertsEnabled},
            {"Safewalk", "", m_features.safewalkEnabled},
            {"Scaffold", m_features.scaffoldSameLayerOnly ? "Same Layer" : "Dynamic", m_features.scaffoldEnabled},
            {"Fly", "", m_features.flyEnabled},
            {"BHop", m_features.bhopAutoJump ? "Auto Jump" : "Manual", m_features.bhopEnabled},
            {"Aim Assist", aimMode, m_features.aimAssistEnabled},
            {"Smart Hotbar", "Minecraft Keys", m_features.smartHotbarEnabled},
            {"Bed Breaker", "", m_features.bedBreakerEnabled},
            {"Player Stats", "", m_features.hypixelPanelEnabled},
            {"Debug", "", m_features.debugChatEnabled},
            {"Blacklist", "", m_blacklist.panelEnabled},
            {"Fireball ESP", "", m_features.fireballEspEnabled},
            {"Knockback Prediction", "", m_features.knockbackPredictionEnabled},
            {"Bow Prediction", "", m_features.bowPredictionEnabled},
            {"Velocity", "", m_features.localVelocityEnabled},
            {"FreeLook", "Hold", m_features.freeLookEnabled},
            {"Now Playing", "", m_mediaSettings.enabled}}};
        ImFont* const textGuiFont = m_boldFonts[static_cast<std::size_t>(
            std::clamp(m_guiScaleIndex, 0, 3))] != nullptr
            ? m_boldFonts[static_cast<std::size_t>(
                std::clamp(m_guiScaleIndex, 0, 3))] : ImGui::GetFont();
        const float textGuiFontSize = ImGui::GetFontSize() * 1.20F;
        const auto measureText = [&](const char* const value) noexcept {
            return textGuiFont->CalcTextSizeA(textGuiFontSize, 100000.0F,
                                              0.0F, value);
        };
        const std::uint64_t glyphClock = static_cast<std::uint64_t>(
            ImGui::GetTime() * 1000.0);
        if (!m_textGuiGlyphsInitialized ||
            glyphClock >= m_textGuiNextShuffleTick) {
            for (std::size_t moduleIndex = 0U;
                 moduleIndex < modules.size(); ++moduleIndex) {
                const std::size_t length = std::min<std::size_t>(
                    std::strlen(modules[moduleIndex].name), 32U);
                const auto previousTargets = m_textGuiGlyphTargets[moduleIndex];
                std::array<std::uint8_t, 32U> order{};
                for (std::size_t character = 0U; character < length; ++character) {
                    order[character] = static_cast<std::uint8_t>(character);
                    m_textGuiGlyphTargets[moduleIndex][character] = 0.50F;
                }
                std::uint32_t randomState = static_cast<std::uint32_t>(
                    glyphClock ^ (moduleIndex + 1U) * 0x9E3779B9U);
                for (std::size_t remaining = length; remaining > 1U; --remaining) {
                    randomState = randomState * 1664525U + 1013904223U;
                    const std::size_t swapIndex = randomState % remaining;
                    std::swap(order[remaining - 1U], order[swapIndex]);
                }
                for (std::size_t bright = 0U; bright < (length + 1U) / 2U;
                     ++bright) {
                    m_textGuiGlyphTargets[moduleIndex][order[bright]] = 1.0F;
                }
                // A random shuffle can repeat a whole short label. Guarantee
                // a visible change by exchanging two different visible glyphs.
                bool visibleChange = false;
                for (std::size_t c = 0; c < length; ++c)
                    visibleChange |= modules[moduleIndex].name[c] != ' ' &&
                        previousTargets[c] != m_textGuiGlyphTargets[moduleIndex][c];
                if (!visibleChange && length > 1) {
                    std::size_t bright = length, dim = length;
                    for (std::size_t c = 0; c < length; ++c) {
                        if (modules[moduleIndex].name[c] == ' ') continue;
                        if (m_textGuiGlyphTargets[moduleIndex][c] > 0.75F) bright = c;
                        else dim = c;
                    }
                    if (bright < length && dim < length)
                        std::swap(m_textGuiGlyphTargets[moduleIndex][bright],
                                  m_textGuiGlyphTargets[moduleIndex][dim]);
                }
            }
            if (!m_textGuiGlyphsInitialized) {
                m_textGuiGlyphBrightness = m_textGuiGlyphTargets;
                m_textGuiGlyphsInitialized = true;
            }
            // Recompose the half-bright mask at a calm cadence. Brightness is
            // interpolated below, so individual glyphs never flash abruptly.
            m_textGuiNextShuffleTick = glyphClock + 2400U;
        }
        // Targets change every 2.4 s; the slower continuous transition has
        // time to settle and remains frame-rate independent.
        const float glyphBlend = 1.0F - std::exp(-1.8F * delta);
        for (std::size_t moduleIndex = 0U; moduleIndex < modules.size(); ++moduleIndex)
            for (std::size_t character = 0U; character < 32U; ++character)
                m_textGuiGlyphBrightness[moduleIndex][character] +=
                    (m_textGuiGlyphTargets[moduleIndex][character] -
                     m_textGuiGlyphBrightness[moduleIndex][character]) * glyphBlend;

        float visibleRows = 0.0F;
        float maximumTextWidth = 0.0F;
        for (std::size_t index = 0U; index < modules.size(); ++index) {
            advancePresentationSpring(m_textGuiModuleProgress[index],
                                      m_textGuiModuleVelocity[index],
                                      modules[index].enabled ? 1.0F : 0.0F, delta);
            const float progress = std::clamp(
                m_textGuiModuleProgress[index], 0.0F, 1.0F);
            if (progress <= 0.004F) continue;
            visibleRows += progress;
            const float modeWidth=m_features.textGuiShowModes && modules[index].mode[0]
                ? measureText(modules[index].mode).x+9.0F*uiScale : 0.0F;
            maximumTextWidth = std::max(maximumTextWidth,
                measureText(modules[index].name).x+modeWidth);
        }
        if (visibleRows > 0.004F) {
            const float width = maximumTextWidth +
                (m_features.textGuiVerticalLine ? 28.0F : 20.0F) * uiScale;
            const float lineHeight = textGuiFontSize + 5.0F * uiScale;
            const float height = lineHeight * visibleRows +
                                 8.0F * uiScale;
            const float defaultX = std::max(5.0F, io.DisplaySize.x - width - 16.0F);
            const float defaultY = std::max(5.0F, io.DisplaySize.y * 0.18F);
            float textX = m_features.textGuiX < 0 ? defaultX
                : io.DisplaySize.x * static_cast<float>(m_features.textGuiX) / 1000.0F;
            float textY = m_features.textGuiY < 0 ? defaultY
                : io.DisplaySize.y * static_cast<float>(m_features.textGuiY) / 1000.0F;
            textX = std::clamp(textX, 4.0F,
                std::max(4.0F, io.DisplaySize.x - width - 4.0F));
            textY = std::clamp(textY, 4.0F,
                std::max(4.0F, io.DisplaySize.y - height - 4.0F));
            ImGui::SetNextWindowPos(ImVec2(textX, textY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0F);
            ImGuiWindowFlags textFlags = ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoScrollbar;
            if (!interactive) textFlags |= ImGuiWindowFlags_NoInputs;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (ImGui::Begin("##TextGuiHud", nullptr, textFlags)) {
                ImGui::InvisibleButton("##TextGuiDrag", ImVec2(width, height));
                if (interactive && ImGui::IsItemActive() &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
                    textX = std::clamp(textX + io.MouseDelta.x, 4.0F,
                        std::max(4.0F, io.DisplaySize.x - width - 4.0F));
                    textY = std::clamp(textY + io.MouseDelta.y, 4.0F,
                        std::max(4.0F, io.DisplaySize.y - height - 4.0F));
                    m_features.textGuiX = std::clamp(static_cast<int>(std::lround(
                        textX / std::max(1.0F, io.DisplaySize.x) * 1000.0F)), 0, 1000);
                    m_features.textGuiY = std::clamp(static_cast<int>(std::lround(
                        textY / std::max(1.0F, io.DisplaySize.y) * 1000.0F)), 0, 1000);
                    m_featureSettingsDirty = true;
                }
                ImDrawList* const textDraw = ImGui::GetWindowDrawList();
                const std::array<float, 3U> baseRgb =
                    unpackRgb(m_features.textGuiColor);
                const ImVec4 base(baseRgb[0U], baseRgb[1U], baseRgb[2U], 1.0F);
                float rowY = textY + 4.0F * uiScale;
                if (m_features.textGuiVerticalLine) {
                    const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(base.x, base.y, base.z, 0.82F));
                    textDraw->AddRectFilled(
                        ImVec2(textX + 3.0F * uiScale, textY + 3.0F * uiScale),
                        ImVec2(textX + 6.0F * uiScale,
                               textY + height - 3.0F * uiScale),
                        lineColor, 1.5F * uiScale);
                }
                for (std::size_t moduleIndex = 0U;
                     moduleIndex < modules.size(); ++moduleIndex) {
                    const TextModule& module = modules[moduleIndex];
                    const float progress = std::clamp(
                        m_textGuiModuleProgress[moduleIndex], 0.0F, 1.0F);
                    if (progress <= 0.004F) continue;
                    const float eased = progress * progress * (3.0F - 2.0F * progress);
                    const ImVec2 nameSize = measureText(module.name);
                    const float modeWidth=m_features.textGuiShowModes && module.mode[0]
                        ? measureText(module.mode).x+9.0F*uiScale : 0.0F;
                    const ImVec2 textSize(nameSize.x+modeWidth,nameSize.y);
                    const float contentLeft = textX +
                        (m_features.textGuiVerticalLine ? 11.0F : 4.0F) * uiScale;
                    const float contentRight = textX + width - 4.0F * uiScale;
                    float glyphX = contentLeft;
                    if (m_features.textGuiAlignment == 1)
                        glyphX = (contentLeft + contentRight - textSize.x) * 0.5F;
                    else if (m_features.textGuiAlignment == 2)
                        glyphX = contentRight - textSize.x;
                    const float entryDirection = m_features.textGuiAlignment == 0
                        ? -1.0F : 1.0F;
                    glyphX += entryDirection * (1.0F - eased) * 18.0F * uiScale;
                    const float glyphY = rowY + (1.0F - eased) * 4.0F * uiScale;
                    // Half of the glyphs are bright and half are 50% dimmed.
                    // A continuously regenerated mask flows through a smooth
                    // exponential transition, producing a restrained optical
                    // shimmer rather than a scrolling-lyrics effect.
                    for (std::size_t characterIndex = 0U;
                         module.name[characterIndex] != '\0'; ++characterIndex) {
                        char glyph[2]{module.name[characterIndex], '\0'};
                        const float glyphWidth = measureText(glyph).x;
                        const float brightness = m_textGuiGlyphBrightness[
                            moduleIndex][std::min<std::size_t>(characterIndex, 31U)];
                        textDraw->AddText(textGuiFont, textGuiFontSize,
                            ImVec2(glyphX + 1.0F, glyphY + 1.4F),
                            IM_COL32(0, 0, 0,
                                static_cast<int>(145.0F * eased * brightness)), glyph);
                        const ImVec4 glyphColor(
                            std::clamp(base.x * 0.58F + 0.42F, 0.0F, 1.0F),
                            std::clamp(base.y * 0.58F + 0.42F, 0.0F, 1.0F),
                            std::clamp(base.z * 0.58F + 0.42F, 0.0F, 1.0F),
                            eased * brightness);
                        textDraw->AddText(textGuiFont, textGuiFontSize,
                            ImVec2(glyphX, glyphY),
                            ImGui::ColorConvertFloat4ToU32(glyphColor), glyph);
                        glyphX += glyphWidth;
                    }
                    if(modeWidth>0.0F) {
                        glyphX+=9.0F*uiScale;
                        textDraw->AddText(textGuiFont,textGuiFontSize,
                            ImVec2(glyphX,glyphY),IM_COL32(238,240,246,
                                static_cast<int>(145.0F*eased)),module.mode);
                    }
                    rowY += lineHeight * progress;
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }
    }

    const bool statsHotkeyDown = gameplayHotkeysAllowed &&
        (::GetAsyncKeyState(m_features.hypixelPanelHotkey) & 0x8000) != 0;
    const bool statsPanelTarget = m_features.hypixelPanelEnabled &&
        snapshot.matchActive && snapshot.playerCount > 0U &&
        (!m_features.hypixelPanelHoldToShow || statsHotkeyDown || interactive);
    advancePresentationSpring(m_statsPanelProgress, m_statsPanelVelocity,
                              statsPanelTarget ? 1.0F : 0.0F, delta);

    if (m_statsPanelProgress > 0.005F) {
            const float panelLinear = std::clamp(m_statsPanelProgress, 0.0F, 1.0F);
            const float panelEase = panelLinear * panelLinear *
                                    (3.0F - 2.0F * panelLinear);
            const float panelPresentationScale =
                1.26F - 0.26F * m_statsPanelProgress;
            const float panelWidthScale = static_cast<float>(std::clamp(
                m_features.hypixelPanelScale, 70, 160)) / 100.0F;
            const float panelHeightScale = static_cast<float>(std::clamp(
                m_features.hypixelPanelHeight, 60, 400)) / 100.0F;
            const float panelWidth = std::clamp(
                448.0F * panelWidthScale, 420.0F,
                std::max(420.0F, io.DisplaySize.x * 0.92F));
            const float panelHeight = std::clamp(
                230.0F * panelHeightScale, 138.0F,
                std::max(138.0F, io.DisplaySize.y - 8.0F));
            const float defaultPanelX = std::max(6.0F,
                io.DisplaySize.x - panelWidth - 12.0F);
            const float storedPanelX = m_features.hypixelPanelX < 0
                ? defaultPanelX
                : io.DisplaySize.x * static_cast<float>(m_features.hypixelPanelX) / 1000.0F;
            const float storedPanelY = m_features.hypixelPanelY < 0
                ? 12.0F
                : io.DisplaySize.y * static_cast<float>(m_features.hypixelPanelY) / 1000.0F;
            const float targetPanelX = std::clamp(storedPanelX, 4.0F,
                std::max(4.0F, io.DisplaySize.x - panelWidth - 4.0F));
            const float targetPanelY = std::clamp(storedPanelY, 4.0F,
                std::max(4.0F, io.DisplaySize.y - panelHeight - 4.0F));
            ImGui::SetNextWindowPos(ImVec2(targetPanelX, targetPanelY),
                                    ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0F);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panelEase);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                ImVec2(0.0F, 0.0F));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
            ImGuiWindowFlags boardFlags = ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;
            if (!interactive) boardFlags |= ImGuiWindowFlags_NoInputs;
            ImDrawList* statsCardDraw = nullptr;
            ImDrawList* statsRowsDraw = nullptr;
            int statsCardVertexStart = 0;
            int statsCardVertexEnd = 0;
            int statsRowsVertexStart = 0;
            int statsRowsVertexEnd = 0;
            if (ImGui::Begin("##LiveBedWarsPlayers", nullptr, boardFlags)) {
                ImDrawList* const cardDraw = ImGui::GetWindowDrawList();
                statsCardDraw = cardDraw;
                statsCardVertexStart = cardDraw->VtxBuffer.Size;
                const ImVec2 cardMin = ImGui::GetWindowPos();
                const ImVec2 cardMax(cardMin.x + ImGui::GetWindowWidth(),
                                     cardMin.y + ImGui::GetWindowHeight());
                cardDraw->AddRectFilled(
                    ImVec2(cardMin.x + 3.0F, cardMin.y + 5.0F),
                    ImVec2(cardMax.x + 3.0F, cardMax.y + 5.0F),
                    IM_COL32(0, 0, 0, 72), 12.0F);
                const int statsPanelAlpha = static_cast<int>(std::lround(
                    std::clamp(m_features.hypixelPanelOpacity, 0, 100) * 2.55));
                const bool whitePanel = m_features.hypixelPanelColor == 0xFFFFFFU;
                const ImU32 primaryText = whitePanel
                    ? IM_COL32(14, 14, 18, 255) : IM_COL32(248, 248, 250, 255);
                const ImU32 secondaryText = whitePanel
                    ? IM_COL32(55, 55, 62, 255) : IM_COL32(210, 210, 218, 255);
                if (statsPanelAlpha < 250) {
                    captureBackdropTexture();
                    if (m_blurTexture != 0U) {
                        // 13-tap separable-Gaussian approximation. The former
                        // five equal acrylic samples preserved hard edges;
                        // weighted centre/axis/diagonal taps produce a softer
                        // Gaussian backdrop without allocating another FBO.
                        struct BlurTap final { ImVec2 offset; int alpha; };
                        constexpr std::array<BlurTap, 13U> blurTaps{{
                            {ImVec2(0, 0), 54},
                            {ImVec2(-2, 0), 40}, {ImVec2(2, 0), 40},
                            {ImVec2(0, -2), 40}, {ImVec2(0, 2), 40},
                            {ImVec2(-2, -2), 26}, {ImVec2(2, -2), 26},
                            {ImVec2(-2, 2), 26}, {ImVec2(2, 2), 26},
                            {ImVec2(-5, 0), 17}, {ImVec2(5, 0), 17},
                            {ImVec2(0, -5), 17}, {ImVec2(0, 5), 17}}};
                        const float exitBlurSpread = 1.0F +
                            (1.0F - panelLinear) * 2.8F;
                        for (const BlurTap& tap : blurTaps) {
                            const float left = std::clamp(cardMin.x +
                                                          tap.offset.x * exitBlurSpread,
                                                          0.0F, io.DisplaySize.x);
                            const float top = std::clamp(cardMin.y +
                                                         tap.offset.y * exitBlurSpread,
                                                         0.0F, io.DisplaySize.y);
                            const float right = std::clamp(cardMax.x +
                                                           tap.offset.x * exitBlurSpread,
                                                           0.0F, io.DisplaySize.x);
                            const float bottom = std::clamp(cardMax.y +
                                                            tap.offset.y * exitBlurSpread,
                                                            0.0F, io.DisplaySize.y);
                            cardDraw->AddImageRounded(
                                reinterpret_cast<ImTextureID>(
                                    static_cast<std::uintptr_t>(m_blurTexture)),
                                cardMin, cardMax,
                                ImVec2(left / io.DisplaySize.x,
                                       1.0F - top / io.DisplaySize.y),
                                ImVec2(right / io.DisplaySize.x,
                                       1.0F - bottom / io.DisplaySize.y),
                                IM_COL32(255, 255, 255, static_cast<int>(
                                    std::lround(static_cast<float>(tap.alpha) * panelEase))),
                                12.0F);
                        }
                    }
                }
                cardDraw->AddRectFilled(cardMin, cardMax,
                    packedRgbColor(whitePanel ? 0xFFFFFFU : 0x000000U, statsPanelAlpha),
                    12.0F);
                cardDraw->AddRect(cardMin, cardMax,
                    whitePanel ? IM_COL32(0, 0, 0, 52) : IM_COL32(255, 255, 255, 48),
                    12.0F, 0, 1.0F);
                const int statsRailAlpha = static_cast<int>(std::lround(
                    std::clamp(m_features.hypixelRailOpacity, 0, 100) * 2.55));
                cardDraw->AddRectFilled(
                    cardMin, ImVec2(cardMin.x + 24.0F, cardMax.y),
                    packedRgbColor(m_features.hypixelRailColor, statsRailAlpha),
                    12.0F,
                    ImDrawFlags_RoundCornersLeft);
                const std::size_t statsFontIndex = static_cast<std::size_t>(
                    std::clamp(m_features.hypixelPanelFontIndex, 0, 3));
                ImFont* const panelFont = m_fonts[statsFontIndex] != nullptr
                    ? m_fonts[statsFontIndex] : ImGui::GetFont();
                ImFont* const panelBold = m_boldFonts[statsFontIndex] != nullptr
                    ? m_boldFonts[statsFontIndex] : panelFont;
                // Independent geometry: resizing the card never stretches the
                // rasterized font or row pitch. This keeps every row legible at
                // all GUI size presets and prevents baseline overlap.
                const float panelFontSize = panelFont->LegacySize;
                const float panelBoldSize = panelBold->LegacySize;
                const float rowHeight = std::ceil(std::max(20.0F,
                                                          panelFontSize + 5.0F));
                constexpr float railWidth = 24.0F;
                const float headerHeight = std::ceil(panelBoldSize + 11.0F);
                if (interactive && panelLinear > 0.985F) {
                    const ImVec2 resizeMin(cardMax.x - 22.0F,
                                           cardMax.y - 22.0F);
                    const bool resizeHovered = ImGui::IsMouseHoveringRect(
                        resizeMin, cardMax, false);
                    const bool headerHovered = ImGui::IsMouseHoveringRect(
                        cardMin,
                        ImVec2(cardMax.x - 22.0F,
                               cardMin.y + headerHeight), false);
                    const bool railHovered = ImGui::IsMouseHoveringRect(
                        cardMin,
                        ImVec2(cardMin.x + railWidth, cardMax.y), false);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (resizeHovered) {
                            m_statsPanelResizing = true;
                            m_statsPanelDragging = false;
                            m_statsPanelResizeStartX = io.MousePos.x;
                            m_statsPanelResizeStartY = io.MousePos.y;
                            m_statsPanelResizeStartScale = m_features.hypixelPanelScale;
                            m_statsPanelResizeStartHeight = m_features.hypixelPanelHeight;
                        } else if (headerHovered || railHovered) {
                            m_statsPanelDragging = true;
                            m_statsPanelResizing = false;
                            m_statsPanelDragStartMouseX = io.MousePos.x;
                            m_statsPanelDragStartMouseY = io.MousePos.y;
                            m_statsPanelDragStartPanelX = targetPanelX;
                            m_statsPanelDragStartPanelY = targetPanelY;
                        }
                    }
                    if (m_statsPanelResizing && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        const float dragX = io.MousePos.x - m_statsPanelResizeStartX;
                        const float dragY = io.MousePos.y - m_statsPanelResizeStartY;
                        const int resizedWidth = std::clamp(
                            m_statsPanelResizeStartScale + static_cast<int>(std::lround(
                                dragX * 100.0F / 448.0F)), 70, 160);
                        const int resizedHeight = std::clamp(
                            m_statsPanelResizeStartHeight + static_cast<int>(std::lround(
                                dragY * 100.0F / 230.0F)), 60, 400);
                        if (resizedWidth != m_features.hypixelPanelScale ||
                            resizedHeight != m_features.hypixelPanelHeight) {
                            m_features.hypixelPanelScale = resizedWidth;
                            m_features.hypixelPanelHeight = resizedHeight;
                            m_statsPanelTransformDirty = true;
                        }
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    } else if (m_statsPanelDragging &&
                               ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        // Calculate from the immutable press origin. Accumulating
                        // MouseDelta into a normalized/rounded position caused
                        // quantization feedback, lag and cursor-shape flicker.
                        const float movedX = m_statsPanelDragStartPanelX +
                            (io.MousePos.x - m_statsPanelDragStartMouseX);
                        const float movedY = m_statsPanelDragStartPanelY +
                            (io.MousePos.y - m_statsPanelDragStartMouseY);
                        m_features.hypixelPanelX = std::clamp(static_cast<int>(std::lround(
                            movedX / std::max(1.0F, io.DisplaySize.x) * 1000.0F)), 0, 1000);
                        m_features.hypixelPanelY = std::clamp(static_cast<int>(std::lround(
                            movedY / std::max(1.0F, io.DisplaySize.y) * 1000.0F)), 0, 1000);
                        m_statsPanelTransformDirty = true;
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    } else if (resizeHovered) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    } else if (headerHovered || railHovered) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    }
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                        (m_statsPanelDragging || m_statsPanelResizing)) {
                        if (m_statsPanelTransformDirty) m_featureSettingsDirty = true;
                        m_statsPanelTransformDirty = false;
                        m_statsPanelDragging = false;
                        m_statsPanelResizing = false;
                    }
                }
                constexpr std::array<char, 5U> verticalLabel{'S', 'T', 'A', 'T', 'S'};
                const float labelAdvance = std::max(13.0F, panelBoldSize * 0.84F);
                const float labelHeight = panelBoldSize +
                    labelAdvance * static_cast<float>(verticalLabel.size() - 1U);
                const float labelStartY = cardMin.y +
                    std::max(0.0F, (panelHeight - labelHeight) * 0.5F);
                for (std::size_t letter = 0U; letter < verticalLabel.size(); ++letter) {
                    const char text[2]{verticalLabel[letter], '\0'};
                    const ImVec2 textSize = panelBold->CalcTextSizeA(
                        panelBoldSize, FLT_MAX, 0.0F, text);
                    cardDraw->AddText(panelBold, panelBoldSize,
                        ImVec2(cardMin.x + (railWidth - textSize.x) * 0.5F,
                               labelStartY + static_cast<float>(letter) * labelAdvance),
                        contrastingTextColor(m_features.hypixelRailColor), text);
                }
                const ImVec2 columnHeader(cardMin.x + railWidth + 4.0F,
                                          cardMin.y + 6.0F);
                const float contentWidth = std::max(360.0F, panelWidth - railWidth - 8.0F);
                const std::array<float, 8U> columns{{
                    4.0F, contentWidth * 0.335F, contentWidth * 0.445F,
                    contentWidth * 0.555F, contentWidth * 0.655F,
                    contentWidth * 0.755F, contentWidth * 0.865F,
                    contentWidth * 0.955F}};
                const ImU32 headerColor = primaryText;
                const auto header = [&](const float x, const char* text) noexcept {
                    cardDraw->AddText(panelBold, panelBoldSize,
                        ImVec2(columnHeader.x + x, columnHeader.y),
                        headerColor, text);
                };
                constexpr std::array<const char*, 8U> headers{{
                    "PLAYER", "STAR", "FKDR", "WLR", "BBLR", "FINALS", "WINS", "WS"}};
                for (std::size_t column = 0U; column < headers.size(); ++column)
                    header(columns[column], headers[column]);
                ImGui::SetCursorScreenPos(ImVec2(columnHeader.x,
                                                  cardMin.y + headerHeight));
                std::array<std::uint32_t, GameSnapshot::MaxDiscoveredPlayers> order{};
                for (std::uint32_t index = 0U; index < snapshot.playerCount; ++index) {
                    order[index] = index;
                }
                const auto teamRank = [](const char code) noexcept {
                    constexpr std::array<char, 8U> orderCodes{
                        'c', '9', 'a', 'e', 'b', 'f', 'd', '7'};
                    const auto found = std::find(orderCodes.begin(), orderCodes.end(), code);
                    return found == orderCodes.end()
                        ? 8 : static_cast<int>(found - orderCodes.begin());
                };
                const auto teamLetter = [](const char code) noexcept {
                    switch (code) {
                    case 'c': return 'R';
                    case '9': return 'B';
                    case 'a': return 'G';
                    case 'e': return 'Y';
                    case 'b': return 'A';
                    case 'f': return 'W';
                    case 'd': return 'P';
                    case '7': return 'S';
                    default: return '?';
                    }
                };
                std::sort(order.begin(), order.begin() + snapshot.playerCount,
                          [&](const std::uint32_t first, const std::uint32_t second) noexcept {
                              const PlayerIdentity& a = snapshot.players[first];
                              const PlayerIdentity& b = snapshot.players[second];
                              const int ar = teamRank(a.teamColor);
                              const int br = teamRank(b.teamColor);
                              return ar != br ? ar < br
                                  : std::strcmp(a.name.data(), b.name.data()) < 0;
                          });
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
                ImGui::BeginChild("##liveRosterRows",
                    ImVec2(panelWidth - railWidth - 8.0F,
                           panelHeight - headerHeight - 4.0F),
                    false, ImGuiWindowFlags_NoBackground);
                statsRowsDraw = ImGui::GetWindowDrawList();
                statsRowsVertexStart = statsRowsDraw != nullptr
                    ? statsRowsDraw->VtxBuffer.Size : 0;
                for (std::uint32_t ordered = 0U; ordered < snapshot.playerCount; ++ordered) {
                    const PlayerIdentity& identity = snapshot.players[order[ordered]];
                    const char code = identity.teamColor;
                    const PlayerStatsEntry* stats = nullptr;
                    for (std::uint32_t statsIndex = 0U;
                         statsIndex < m_playerStats.count; ++statsIndex) {
                        if (std::strcmp(m_playerStats.entries[statsIndex].name.data(),
                                        identity.name.data()) == 0) {
                            stats = &m_playerStats.entries[statsIndex];
                            break;
                        }
                    }
                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const ImVec2 size(ImGui::GetContentRegionAvail().x,
                                      rowHeight);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const int rowAlpha = (ordered & 1U) != 0U ? 18 : 10;
                    drawList->AddRectFilled(pos,
                        ImVec2(pos.x + size.x, pos.y + size.y),
                        whitePanel ? IM_COL32(0, 0, 0, rowAlpha)
                                   : IM_COL32(255, 255, 255, rowAlpha),
                        0.0F);
                    const float textY = pos.y + (rowHeight - panelFontSize) * 0.5F;
                    const char teamTag[4]{'[', teamLetter(code), ']', '\0'};
                    drawList->AddText(panelBold, panelBoldSize,
                        ImVec2(pos.x + columns[0U], textY),
                        ImGui::ColorConvertFloat4ToU32(teamColor(code)), teamTag);
                    const ImVec4 nameClip(
                        pos.x + 30.0F, pos.y,
                        pos.x + columns[1U] - 5.0F, pos.y + size.y);
                    drawList->AddText(panelFont, panelFontSize,
                        ImVec2(pos.x + 30.0F, textY), primaryText,
                        identity.name.data(), nullptr, 0.0F, &nameClip);
                    if (stats == nullptr) {
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[1U], textY),
                            secondaryText, "Querying...");
                    } else if (stats->failed) {
                        const bool suspectedNick = std::strcmp(
                            stats->status.data(), "unavailable") == 0;
                        drawList->AddText(panelBold, panelBoldSize,
                            ImVec2(pos.x + columns[1U], textY),
                            suspectedNick ? IM_COL32(224, 159, 255, 255)
                                          : IM_COL32(255, 116, 127, 255),
                            suspectedNick ? "SUSPECTED NICK" : "UNAVAILABLE");
                    } else {
                        char starsText[24]{};
                        char fkdrText[16]{};
                        char wlrText[16]{};
                        char bblrText[16]{};
                        char finalsText[16]{};
                        char winsText[16]{};
                        char winStreakText[12]{};
                        const PrestigeStyle prestige = bedWarsPrestigeStyle(stats->stars);
                        std::snprintf(starsText, sizeof(starsText), "%d", stats->stars);
                        std::snprintf(fkdrText, sizeof(fkdrText), "%.2f", stats->fkdr);
                        std::snprintf(wlrText, sizeof(wlrText), "%.2f", stats->wlr);
                        std::snprintf(bblrText, sizeof(bblrText), "%.2f", stats->bblr);
                        formatCompactCount(finalsText, sizeof(finalsText), stats->finalKills);
                        formatCompactCount(winsText, sizeof(winsText), stats->wins);
                        std::snprintf(winStreakText, sizeof(winStreakText), "%d", stats->winStreak);
                        const ImU32 fkdrColor = stats->fkdr >= 3.0
                            ? IM_COL32(89, 235, 122, 255)
                            : (stats->fkdr >= 1.5 ? IM_COL32(255, 214, 82, 255)
                                                  : IM_COL32(255, 97, 107, 255));
                        drawList->AddText(panelBold, panelBoldSize,
                            ImVec2(pos.x + columns[1U], textY),
                            prestige.color, starsText);
                        const ImVec2 starNumberSize = panelBold->CalcTextSizeA(
                            panelBoldSize, FLT_MAX, 0.0F, starsText);
                        drawPrestigeStar(drawList,
                            ImVec2(pos.x + columns[1U] + starNumberSize.x + 6.0F,
                                   pos.y + size.y * 0.5F),
                            4.1F, prestige.color, prestige.master);
                        drawList->AddText(panelBold, panelBoldSize,
                            ImVec2(pos.x + columns[2U], textY), fkdrColor, fkdrText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[3U], textY),
                            stats->wlr >= 1.0 ? IM_COL32(57, 190, 112, 255) : secondaryText,
                            wlrText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[4U], textY),
                            stats->bblr >= 1.5 ? IM_COL32(54, 164, 219, 255) : secondaryText,
                            bblrText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[5U], textY),
                            stats->finalKills >= 5000 ? IM_COL32(224, 151, 35, 255) : secondaryText,
                            finalsText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[6U], textY), secondaryText, winsText);
                        drawList->AddText(panelFont, panelFontSize,
                            ImVec2(pos.x + columns[7U], textY),
                            stats->winStreak >= 10 ? IM_COL32(238, 83, 70, 255) : secondaryText,
                            winStreakText);
                    }
                    ImGui::Dummy(size);
                }
                statsRowsVertexEnd = statsRowsDraw != nullptr
                    ? statsRowsDraw->VtxBuffer.Size : statsRowsVertexStart;
                ImGui::EndChild();
                ImGui::PopStyleVar();
                if (interactive) {
                    const ImU32 gripColor = whitePanel
                        ? IM_COL32(20, 20, 24, 155) : IM_COL32(255, 255, 255, 160);
                    for (int gripLine = 0; gripLine < 3; ++gripLine) {
                        const float inset = 4.0F + static_cast<float>(gripLine) * 4.0F;
                        cardDraw->AddLine(
                            ImVec2(cardMax.x - inset, cardMax.y - 2.0F),
                            ImVec2(cardMax.x - 2.0F, cardMax.y - inset),
                            gripColor, 1.0F);
                    }
                }
                statsCardVertexEnd = cardDraw->VtxBuffer.Size;
            }
            ImGui::End();
            ImGui::PopStyleVar(3);

            // Use the same whole-surface spotlight transform as the Click GUI.
            // Geometry is laid out at its final coordinates and all vertices
            // gather/scatter around the panel centre as one flat layer.
            const ImVec2 statsCenter(targetPanelX + panelWidth * 0.5F,
                                     targetPanelY + panelHeight * 0.5F);
            const auto transformStats = [&](ImDrawList* const drawList,
                                            int begin, int end) noexcept {
                if (drawList == nullptr) return;
                begin = std::clamp(begin, 0, drawList->VtxBuffer.Size);
                end = std::clamp(end, begin, drawList->VtxBuffer.Size);
                for (int vertexIndex = begin; vertexIndex < end; ++vertexIndex) {
                    ImDrawVert& vertex = drawList->VtxBuffer[vertexIndex];
                    if (std::abs(panelPresentationScale - 1.0F) >= 0.0001F) {
                        vertex.pos.x = statsCenter.x +
                            (vertex.pos.x - statsCenter.x) * panelPresentationScale;
                        vertex.pos.y = statsCenter.y +
                            (vertex.pos.y - statsCenter.y) * panelPresentationScale;
                    }
                    const unsigned alpha = static_cast<unsigned>(vertex.col >> 24U);
                    const unsigned faded = static_cast<unsigned>(std::clamp(
                        std::lround(static_cast<float>(alpha) * panelEase),
                        0L, 255L));
                    vertex.col = (vertex.col & 0x00FFFFFFU) | (faded << 24U);
                }
                if (std::abs(panelPresentationScale - 1.0F) >= 0.0001F) {
                    for (ImDrawCmd& command : drawList->CmdBuffer) {
                        command.ClipRect.x = statsCenter.x +
                            (command.ClipRect.x - statsCenter.x) * panelPresentationScale;
                        command.ClipRect.y = statsCenter.y +
                            (command.ClipRect.y - statsCenter.y) * panelPresentationScale;
                        command.ClipRect.z = statsCenter.x +
                            (command.ClipRect.z - statsCenter.x) * panelPresentationScale;
                        command.ClipRect.w = statsCenter.y +
                            (command.ClipRect.w - statsCenter.y) * panelPresentationScale;
                    }
                }
            };
            transformStats(statsCardDraw, statsCardVertexStart,
                           statsCardVertexEnd);
            if (statsRowsDraw != statsCardDraw)
                transformStats(statsRowsDraw, statsRowsVertexStart,
                               statsRowsVertexEnd);
    }

    // A blacklist encounter is admitted only through the cumulative,
    // colour-validated TAB roster. This excludes lobby/shop bots and keeps a
    // UUID match stable across respawn gaps and later name changes.
    if (!snapshot.matchActive) {
        m_blacklistWarnedCount = 0U;
        m_blacklistMatchWasActive = false;
    } else {
        m_blacklistMatchWasActive = true;
        if (m_blacklist.matchAlertsEnabled) {
            for (std::uint32_t entryIndex = 0U;
                 entryIndex < m_blacklist.count; ++entryIndex) {
                const BlacklistEntry& entry = m_blacklist.entries[entryIndex];
                if (!entry.warnOnEncounter) continue;
                bool encountered = false;
                for (std::uint32_t playerIndex = 0U;
                     playerIndex < snapshot.playerCount; ++playerIndex) {
                    const PlayerIdentity& player = snapshot.players[playerIndex];
                    encountered = entry.idOnly
                        ? ::_stricmp(entry.name.data(), player.name.data()) == 0
                        : entry.uuid[0U] != '\0' && player.uuid[0U] != '\0' &&
                          ::_stricmp(entry.uuid.data(), player.uuid.data()) == 0;
                    if (encountered) break;
                }
                if (!encountered) continue;
                bool alreadyWarned = false;
                for (std::uint32_t warned = 0U;
                     warned < m_blacklistWarnedCount; ++warned) {
                    if (::_stricmp(m_blacklistWarnedKeys[warned].data(),
                                   entry.key.data()) == 0) {
                        alreadyWarned = true;
                        break;
                    }
                }
                if (alreadyWarned) continue;
                if (m_blacklistWarnedCount < m_blacklistWarnedKeys.size()) {
                    std::snprintf(m_blacklistWarnedKeys[m_blacklistWarnedCount].data(),
                        m_blacklistWarnedKeys[m_blacklistWarnedCount].size(), "%s",
                        entry.key.data());
                    ++m_blacklistWarnedCount;
                }
                char message[52]{};
                std::snprintf(message, sizeof(message), "WARNING: %s is blacklisted",
                              entry.name.data());
                enqueueMessage(message, false);
            }
        }
    }
    // showWithClickGui applies to the whole Click GUI session rather than only
    // the Blacklist settings page, so navigation never unexpectedly dismisses
    // the panel while the user is reviewing another category.
    const bool blacklistPanelTarget = m_blacklist.panelEnabled ||
        (m_blacklist.showWithClickGui && interactive);
    advancePresentationSpring(m_blacklistPanelProgress,
                              m_blacklistPanelVelocity,
                              blacklistPanelTarget ? 1.0F : 0.0F, delta);
    if (m_blacklistPanelProgress > 0.005F) {
        const float presentation = std::clamp(m_blacklistPanelProgress, 0.0F, 1.0F);
        const float panelAlphaEase = presentation * presentation *
                                     (3.0F - 2.0F * presentation);
        const float panelScale = 1.26F - 0.26F * m_blacklistPanelProgress;
        const float contentScale = static_cast<float>(std::clamp(
            m_blacklist.contentScale, 80, 200)) / 100.0F;
        const auto content = [contentScale](const float value) noexcept {
            return value * contentScale;
        };
        const float requestedWidth = std::clamp(440.0F *
            static_cast<float>(m_blacklist.panelWidth) / 100.0F * contentScale,
            content(260.0F), std::max(content(260.0F), io.DisplaySize.x * 0.82F));
        const float expandedHeight = std::clamp(550.0F *
            static_cast<float>(m_blacklist.panelHeight) / 100.0F * contentScale,
            content(240.0F), std::max(content(240.0F), io.DisplaySize.y - 8.0F));
        const float requestedHeight = m_blacklist.collapsed ? content(64.0F) : expandedHeight;
        const float storedX = m_blacklist.panelX < 0 ? 18.0F
            : io.DisplaySize.x * static_cast<float>(m_blacklist.panelX) / 1000.0F;
        const float storedY = m_blacklist.panelY < 0
            ? std::max(8.0F, (io.DisplaySize.y - expandedHeight) * 0.5F)
            : io.DisplaySize.y * static_cast<float>(m_blacklist.panelY) / 1000.0F;
        const float panelX = std::clamp(storedX, 4.0F,
            std::max(4.0F, io.DisplaySize.x - content(260.0F) - 4.0F));
        const float panelY = std::clamp(storedY, 4.0F,
            std::max(4.0F, io.DisplaySize.y - content(240.0F) - 4.0F));
        // Resizing is anchored to the upper-left. If the lower/right edge
        // reaches the viewport, cap the effective size instead of moving the
        // saved top-left in the opposite direction.
        const float width = std::clamp(requestedWidth, content(260.0F),
            std::max(content(260.0F), io.DisplaySize.x - panelX - 4.0F));
        const float minimumHeight = m_blacklist.collapsed ? content(64.0F) : content(240.0F);
        const float height = std::clamp(requestedHeight, minimumHeight,
            std::max(minimumHeight, io.DisplaySize.y - panelY - 4.0F));
        ImGui::SetNextWindowPos(ImVec2(panelX, panelY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, panelAlphaEase);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        // Match the main GUI's selected font preset. Panel content scaling owns
        // geometry only; applying it again through SetWindowFontScale made text
        // and controls grow twice and caused the card proportions to drift.
        const std::size_t blacklistFontIndex=static_cast<std::size_t>(
            std::clamp(m_guiScaleIndex,0,3));
        ImFont* const listFont = m_fonts[blacklistFontIndex]
            ? m_fonts[blacklistFontIndex] : ImGui::GetFont();
        ImFont* const listBold = m_boldFonts[blacklistFontIndex]
            ? m_boldFonts[blacklistFontIndex] : listFont;
        constexpr std::array<float,4U> blacklistFontSizes{{15.0F,19.0F,23.0F,27.0F}};
        const float blacklistFontSize=blacklistFontSizes[blacklistFontIndex];
        ImGui::PushFont(listFont);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(content(10.0F), content(7.0F)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, content(10.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, content(10.0F));
        const auto panelRgb = unpackRgb(m_blacklist.panelColor);
        const bool lightPanel = panelRgb[0] * 0.2126F + panelRgb[1] * 0.7152F + panelRgb[2] * 0.0722F > 0.6F;
        const ImU32 ink = lightPanel ? IM_COL32(24,26,32,255) : IM_COL32(245,246,250,255);
        const ImU32 muted = lightPanel ? IM_COL32(82,86,96,255) : IM_COL32(171,180,197,255);
        const ImU32 cardSurface = lightPanel ? IM_COL32(0,0,0,13) : IM_COL32(255,255,255,16);
        ImGui::PushStyleColor(ImGuiCol_Text, ink);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, cardSurface);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, lightPanel ? IM_COL32(68,72,86,140) : IM_COL32(196,202,218,140));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
        if (!interactive) flags |= ImGuiWindowFlags_NoInputs;
        ImDrawList* panelDraw = nullptr;
        ImDrawList* entriesDraw = nullptr;
        int panelBegin = 0, panelEnd = 0, entriesBegin = 0, entriesEnd = 0;
        if (ImGui::Begin("##BlacklistPanel", nullptr, flags)) {
            panelDraw = ImGui::GetWindowDrawList();
            panelBegin = panelDraw->VtxBuffer.Size;
            const ImVec2 minimum = ImGui::GetWindowPos();
            const ImVec2 maximum(minimum.x + width, minimum.y + height);
            const int surfaceAlpha = static_cast<int>(std::lround(
                std::clamp(m_blacklist.panelOpacity, 0, 100) * 2.55));
            if (surfaceAlpha < 250) {
                captureBackdropTexture();
                if (m_blurTexture != 0U) {
                    constexpr std::array<ImVec2, 9U> taps{{
                        ImVec2(0,0), ImVec2(-3,0), ImVec2(3,0), ImVec2(0,-3),
                        ImVec2(0,3), ImVec2(-5,-5), ImVec2(5,-5),
                        ImVec2(-5,5), ImVec2(5,5)}};
                    const float exitBlurSpread = 1.0F +
                        (1.0F - presentation) * 2.8F;
                    for (const ImVec2 tap : taps) {
                        const float left = std::clamp(minimum.x +
                            tap.x * exitBlurSpread, 0.0F, io.DisplaySize.x);
                        const float top = std::clamp(minimum.y +
                            tap.y * exitBlurSpread, 0.0F, io.DisplaySize.y);
                        const float right = std::clamp(maximum.x +
                            tap.x * exitBlurSpread, 0.0F, io.DisplaySize.x);
                        const float bottom = std::clamp(maximum.y +
                            tap.y * exitBlurSpread, 0.0F, io.DisplaySize.y);
                        panelDraw->AddImageRounded(reinterpret_cast<ImTextureID>(
                            static_cast<std::uintptr_t>(m_blurTexture)), minimum, maximum,
                            ImVec2(left / io.DisplaySize.x, 1.0F - top / io.DisplaySize.y),
                            ImVec2(right / io.DisplaySize.x, 1.0F - bottom / io.DisplaySize.y),
                            IM_COL32(255,255,255,static_cast<int>(
                                std::lround(36.0F * panelAlphaEase))), content(18.0F));
                    }
                }
            }
            // No offset shadow inside the window's rectangular clip. It is
            // truncated at the lower/right edges and produces square corners
            // on both collapsed and expanded cards. The rounded surface and
            // subtle outline provide separation without extending the clip.
            panelDraw->AddRectFilled(minimum, maximum,
                packedRgbColor(m_blacklist.panelColor, surfaceAlpha), content(18.0F));
            panelDraw->AddRect(minimum, maximum, IM_COL32(255,255,255,42),
                               content(18.0F), 0, std::max(1.0F,contentScale));
            ImFont* const bold = listBold;
            panelDraw->AddText(bold, blacklistFontSize * 1.12F,
                ImVec2(minimum.x + content(16.0F), minimum.y + content(11.0F)),
                ink, "Blacklist");
            char countLabel[48]{};
            std::snprintf(countLabel, sizeof(countLabel), "%u saved players", m_blacklist.count);
            panelDraw->AddText(listFont, blacklistFontSize * 0.72F,
                ImVec2(minimum.x + content(16.0F), minimum.y + content(38.0F)),
                muted, countLabel);

            const ImVec2 collapseMin(maximum.x - content(48.0F),
                                     minimum.y + content(13.0F));
            const ImVec2 collapseMax(maximum.x - content(10.0F),
                                     minimum.y + content(51.0F));
            panelDraw->AddRectFilled(collapseMin, collapseMax,
                IM_COL32(255,255,255,18), content(11.0F));
            const float chevronY = (collapseMin.y + collapseMax.y) * 0.5F;
            const float chevronDirection = m_blacklist.collapsed ? -1.0F : 1.0F;
            panelDraw->AddLine(
                ImVec2(collapseMin.x + content(11.0F), chevronY - content(4.0F) * chevronDirection),
                ImVec2((collapseMin.x + collapseMax.x) * 0.5F,
                       chevronY + content(4.0F) * chevronDirection),
                ink, content(2.0F));
            panelDraw->AddLine(
                ImVec2((collapseMin.x + collapseMax.x) * 0.5F,
                       chevronY + content(4.0F) * chevronDirection),
                ImVec2(collapseMax.x - content(11.0F), chevronY - content(4.0F) * chevronDirection),
                ink, content(2.0F));

            if (interactive && presentation > 0.985F) {
                const ImVec2 resizeMin(maximum.x - content(24.0F),
                                       maximum.y - content(24.0F));
                const bool panelHovered = ImGui::IsWindowHovered();
                const bool collapseHovered = panelHovered && ImGui::IsMouseHoveringRect(
                    collapseMin, collapseMax, false);
                const bool resizeHovered = panelHovered && !m_blacklist.collapsed &&
                    ImGui::IsMouseHoveringRect(resizeMin, maximum, false);
                const bool headerHovered = panelHovered && ImGui::IsMouseHoveringRect(
                    minimum, ImVec2(collapseMin.x - content(4.0F),
                                    minimum.y + content(64.0F)), false);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (collapseHovered) {
                        m_blacklist.collapsed = !m_blacklist.collapsed;
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
                    } else if (resizeHovered) {
                        m_blacklistPanelResizing = true;
                        m_blacklistPanelDragging = false;
                        // The default Y is vertically centred and therefore
                        // depends on height. Materialise the current top-left
                        // position before resizing so dragging the lower-right
                        // grip never moves the top edge in the opposite
                        // direction.
                        if (m_blacklist.panelX < 0) {
                            m_blacklist.panelX = std::clamp(
                                static_cast<int>(std::lround(panelX /
                                    std::max(1.0F, io.DisplaySize.x) * 1000.0F)),
                                0, 1000);
                        }
                        if (m_blacklist.panelY < 0) {
                            m_blacklist.panelY = std::clamp(
                                static_cast<int>(std::lround(panelY /
                                    std::max(1.0F, io.DisplaySize.y) * 1000.0F)),
                                0, 1000);
                        }
                        m_blacklistResizeStartMouseX = io.MousePos.x;
                        m_blacklistResizeStartMouseY = io.MousePos.y;
                        m_blacklistResizeStartWidth = m_blacklist.panelWidth;
                        m_blacklistResizeStartHeight = m_blacklist.panelHeight;
                    } else if (headerHovered) {
                        m_blacklistPanelDragging = true;
                        m_blacklistPanelResizing = false;
                        m_blacklistDragStartMouseX = io.MousePos.x;
                        m_blacklistDragStartMouseY = io.MousePos.y;
                        m_blacklistDragStartPanelX = panelX;
                        m_blacklistDragStartPanelY = panelY;
                    }
                }
                if (m_blacklistPanelDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const float x = m_blacklistDragStartPanelX +
                        io.MousePos.x - m_blacklistDragStartMouseX;
                    const float y = m_blacklistDragStartPanelY +
                        io.MousePos.y - m_blacklistDragStartMouseY;
                    m_blacklist.panelX = std::clamp(static_cast<int>(std::lround(
                        x / std::max(1.0F, io.DisplaySize.x) * 1000.0F)), 0, 1000);
                    m_blacklist.panelY = std::clamp(static_cast<int>(std::lround(
                        y / std::max(1.0F, io.DisplaySize.y) * 1000.0F)), 0, 1000);
                    m_blacklistPanelTransformDirty = true;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                } else if (m_blacklistPanelResizing &&
                           ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_blacklist.panelWidth = std::clamp(
                        m_blacklistResizeStartWidth + static_cast<int>(std::lround(
                            (io.MousePos.x - m_blacklistResizeStartMouseX) * 100.0F /
                            content(440.0F))),
                        60, 180);
                    m_blacklist.panelHeight = std::clamp(
                        m_blacklistResizeStartHeight + static_cast<int>(std::lround(
                            (io.MousePos.y - m_blacklistResizeStartMouseY) * 100.0F /
                            content(550.0F))),
                        60, 300);
                    m_blacklistPanelTransformDirty = true;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                } else if (resizeHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                else if (headerHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                    (m_blacklistPanelDragging || m_blacklistPanelResizing)) {
                    if (m_blacklistPanelTransformDirty) {
                        m_blacklistAction = {};
                        m_blacklistAction.type = BlacklistAction::Type::Layout;
                        m_blacklistAction.x = m_blacklist.panelX;
                        m_blacklistAction.y = m_blacklist.panelY;
                        m_blacklistAction.width = m_blacklist.panelWidth;
                        m_blacklistAction.height = m_blacklist.panelHeight;
                        m_blacklistActionDirty = true;
                    }
                    m_blacklistPanelTransformDirty = false;
                    m_blacklistPanelDragging = false;
                    m_blacklistPanelResizing = false;
                }
            }

            if (!m_blacklist.collapsed) {
                ImGui::SetCursorScreenPos(ImVec2(minimum.x + content(12.0F),
                                                  minimum.y + content(68.0F)));
                ImGui::SetNextItemWidth(width - content(24.0F));
                if (ImGui::InputTextWithHint("##BlacklistSearch", "Search name or reason...",
                                            m_blacklistSearch.data(), m_blacklistSearch.size()))
                    m_blacklistScroll = {};
                ImGui::SetCursorScreenPos(ImVec2(minimum.x + content(12.0F),
                                                  minimum.y + content(110.0F)));
                beginSmoothChild("##BlacklistCards",
                                 ImVec2(width - content(24.0F),
                                        height - content(168.0F)),
                                 m_blacklistScroll, delta, ImGuiWindowFlags_NoBackground);
                entriesDraw = ImGui::GetWindowDrawList();
                entriesBegin = entriesDraw->VtxBuffer.Size;
                const auto contains = [](const char* haystack, const char* needle) {
                    return std::search(haystack, haystack + std::strlen(haystack),
                        needle, needle + std::strlen(needle), [](unsigned char a, unsigned char b) {
                            return std::tolower(a) == std::tolower(b);
                        }) != haystack + std::strlen(haystack);
                };
                unsigned shown = 0;
                for (std::uint32_t index = 0; index < std::min<std::uint32_t>(
                         m_blacklist.count, static_cast<std::uint32_t>(m_blacklist.entries.size())); ++index) {
                    const BlacklistEntry& entry = m_blacklist.entries[index];
                    if (m_blacklistSearch[0] && !contains(entry.name.data(), m_blacklistSearch.data()) &&
                        !contains(entry.reason.data(), m_blacklistSearch.data())) continue;
                    ++shown;
                    ImGui::PushID(entry.key.data());
                    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                    const float cardWidth = ImGui::GetContentRegionAvail().x;
                    const char* reason = entry.reason[0] ? entry.reason.data() : "No reason provided";
                    const float reasonSize=blacklistFontSize*0.92F;
                    const float nameSize=blacklistFontSize*1.03F;
                    const float metaSize=blacklistFontSize*0.68F;
                    const float reasonHeight = std::clamp(listFont->CalcTextSizeA(
                        reasonSize, FLT_MAX,
                        std::max(content(80.0F), cardWidth - content(24.0F)), reason).y,
                        reasonSize, reasonSize*3.0F);
                    const ImVec2 cardMax(cardMin.x + cardWidth,
                                         cardMin.y + content(90.0F) + reasonHeight);
                    entriesDraw->AddRectFilled(cardMin, cardMax, cardSurface,
                                               content(12.0F));
                unsigned faceTexture = 0U;
                if (entry.facePath[0U] != '\0') {
                    BlacklistTexture* slot = nullptr;
                    for (BlacklistTexture& cached : m_blacklistTextures) {
                        if (cached.path[0U] != '\0' &&
                            std::strcmp(cached.path.data(), entry.facePath.data()) == 0) {
                            slot = &cached; break;
                        }
                        if (slot == nullptr && cached.path[0U] == '\0') slot = &cached;
                    }
                    if (slot != nullptr && slot->path[0U] == '\0') {
                        std::snprintf(slot->path.data(), slot->path.size(), "%s",
                                      entry.facePath.data());
                        int imageWidth = 0, imageHeight = 0, channels = 0;
                        unsigned char* pixels = stbi_load(entry.facePath.data(),
                            &imageWidth, &imageHeight, &channels, 4);
                        if (pixels != nullptr && imageWidth > 0 && imageHeight > 0) {
                            GLint lastTexture = 0;
                            ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
                            ::glGenTextures(1, &slot->texture);
                            ::glBindTexture(GL_TEXTURE_2D, slot->texture);
                            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                            ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth,
                                imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                            ::glBindTexture(GL_TEXTURE_2D,
                                            static_cast<GLuint>(lastTexture));
                        }
                        stbi_image_free(pixels);
                    }
                    if (slot != nullptr) faceTexture = slot->texture;
                }

                    const ImVec2 avatarMin(cardMin.x + content(12.0F),
                                           cardMin.y + content(12.0F));
                    const ImVec2 avatarMax(avatarMin.x + content(36.0F),
                                           avatarMin.y + content(36.0F));
                    entriesDraw->AddRectFilled(avatarMin, avatarMax,
                                               IM_COL32(87,83,112,180), content(8.0F));
                    if (faceTexture) entriesDraw->AddImageRounded(
                        reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(faceTexture)),
                        avatarMin, avatarMax, ImVec2(0,0), ImVec2(1,1),
                        IM_COL32_WHITE, content(8.0F));
                    else {
                        // Neutral placeholder until the real skin file arrives.
                        entriesDraw->AddCircle(ImVec2(avatarMin.x+content(18.0F),
                            avatarMin.y+content(13.0F)), content(5.0F), muted, 16,
                            content(1.5F));
                        entriesDraw->AddBezierQuadratic(
                            ImVec2(avatarMin.x+content(8.0F),avatarMin.y+content(29.0F)),
                            ImVec2(avatarMin.x+content(18.0F),avatarMin.y+content(14.0F)),
                            ImVec2(avatarMin.x+content(28.0F),avatarMin.y+content(29.0F)),
                            muted,content(1.5F));
                    }
                    const ImVec4 nameClip(cardMin.x + content(60.0F),
                        cardMin.y + content(10.0F),cardMax.x-content(12.0F),
                        cardMin.y + content(37.0F));
                    entriesDraw->AddText(bold,nameSize,
                        ImVec2(cardMin.x+content(60.0F),cardMin.y+content(10.0F)),
                                         ink, entry.name.data(), nullptr, 0, &nameClip);
                    entriesDraw->AddText(listFont,metaSize,
                        ImVec2(cardMin.x+content(60.0F),cardMin.y+content(37.0F)),
                        entry.nick ? IM_COL32(193,122,225,255) : muted,
                        entry.nick ? "NICK / ID ONLY" : "UUID LINKED");
                    const ImVec4 reasonClip(cardMin.x+content(12.0F),
                        cardMin.y+content(58.0F),cardMax.x-content(12.0F),
                        cardMin.y+content(58.0F)+reasonHeight);
                    entriesDraw->AddText(listFont,reasonSize,
                        ImVec2(cardMin.x+content(12.0F),cardMin.y+content(58.0F)),
                        ink,reason,nullptr,cardWidth-content(24.0F),&reasonClip);
                    std::time_t seconds = static_cast<std::time_t>(entry.addedAt / 1000);
                    std::tm local{};
                    char date[24]{};
                    if (::_localtime64_s(&local, &seconds) == 0)
                        std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &local);
                    entriesDraw->AddText(listFont,metaSize,
                        ImVec2(cardMin.x+content(12.0F),cardMax.y-content(24.0F)),
                                         muted, date);
                    // Two-step removal: the first press arms this record only.
                    // Stable UUID keys keep the confirmation attached after filtering.
                    const bool confirming = std::strcmp(m_blacklistDeleteKey.data(), entry.key.data()) == 0;
                    ImGui::SetCursorScreenPos(ImVec2(cardMax.x-content(66.0F),
                                                      cardMax.y-content(30.0F)));
                    if (ImGui::InvisibleButton("##remove",
                                               ImVec2(content(56.0F),content(24.0F))) && interactive) {
                        if (!confirming) std::snprintf(m_blacklistDeleteKey.data(),
                            m_blacklistDeleteKey.size(), "%s", entry.key.data());
                        else {
                            m_blacklistAction = {};
                            m_blacklistAction.type = BlacklistAction::Type::Remove;
                            m_blacklistAction.key = entry.key;
                            m_blacklistActionDirty = true;
                            m_blacklistDeleteKey = {};
                        }
                    }
                    const bool hovered = ImGui::IsItemHovered();
                    if (hovered || confirming)
                        entriesDraw->AddRectFilled(
                            ImVec2(cardMax.x-content(66.0F),cardMax.y-content(30.0F)),
                            ImVec2(cardMax.x-content(10.0F),cardMax.y-content(6.0F)),
                            IM_COL32(236,83,108,30),content(7.0F));
                    const char* removeText = confirming ? "Confirm" : "Remove";
                    const float removeWidth=listFont->CalcTextSizeA(
                        content(12.0F),FLT_MAX,0,removeText).x;
                    entriesDraw->AddText(listFont,metaSize,
                        ImVec2(cardMax.x-content(38.0F)-removeWidth*0.5F,
                               cardMax.y-content(24.0F)),
                        lightPanel ? IM_COL32(166,32,60,255) : IM_COL32(255,153,167,255), removeText);
                    if (hovered) ImGui::SetTooltip(confirming ? "Click again to remove this record" : "Remove saved record");
                    ImGui::SetCursorScreenPos(ImVec2(cardMin.x,
                                                      cardMax.y+content(8.0F)));
                    ImGui::Dummy(ImVec2(cardWidth,content(1.0F)));
                    ImGui::PopID();
                }
                if (!shown) {
                    ImGui::Dummy(ImVec2(content(1.0F),content(14.0F)));
                    ImGui::PushStyleColor(ImGuiCol_Text, muted);
                    ImGui::TextWrapped(m_blacklistSearch[0] ? "No matching players. Try another name or reason."
                        : "No saved players yet. Add someone from your recent encounters below.");
                    ImGui::PopStyleColor();
                }
                ImGui::Dummy(ImVec2(1.0F,7.0F*uiScale));
                entriesEnd = entriesDraw->VtxBuffer.Size;
                endSmoothChild(m_blacklistScroll,delta);
                const ImVec2 addMin(minimum.x+content(12.0F),
                                    maximum.y-content(48.0F));
                const ImVec2 addSize(width-content(44.0F),content(36.0F));
                ImGui::SetCursorScreenPos(addMin);
                if (ImGui::InvisibleButton("##BlacklistAddPlayer", addSize) && interactive)
                    m_blacklistAddOpen = true;
                const bool addHovered = interactive && ImGui::IsItemHovered();
                panelDraw->AddRectFilled(addMin, ImVec2(addMin.x + addSize.x, addMin.y + addSize.y),
                    packedRgbColor(m_features.clickGuiAccentColor, addHovered ? 92 : 58),
                    content(11.0F));
                const char* addText = "Add player";
                const float addTextSize=blacklistFontSize*0.92F;
                const float textWidth=bold->CalcTextSizeA(
                    addTextSize,FLT_MAX,0,addText).x;
                const float labelLeft=addMin.x+
                    (addSize.x-textWidth-content(23.0F))*0.5F;
                const ImVec2 plus(labelLeft+content(6.0F),
                                  addMin.y+content(18.0F));
                panelDraw->AddLine(ImVec2(plus.x-content(5.0F),plus.y),
                    ImVec2(plus.x+content(5.0F),plus.y),ink,content(1.8F));
                panelDraw->AddLine(ImVec2(plus.x,plus.y-content(5.0F)),
                    ImVec2(plus.x,plus.y+content(5.0F)),ink,content(1.8F));
                panelDraw->AddText(bold,addTextSize,
                    ImVec2(labelLeft+content(23.0F),addMin.y+content(10.0F)),ink,addText);
                panelDraw->AddLine(ImVec2(maximum.x-content(18.0F),maximum.y-content(9.0F)),
                    ImVec2(maximum.x-content(9.0F),maximum.y-content(18.0F)),muted,content(1.5F));
                panelDraw->AddLine(ImVec2(maximum.x-content(12.0F),maximum.y-content(9.0F)),
                    ImVec2(maximum.x-content(9.0F),maximum.y-content(12.0F)),muted,content(1.5F));
            }
            panelEnd = panelDraw->VtxBuffer.Size;
        }
        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(4);
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
        const ImVec2 center(panelX + width * 0.5F, panelY + height * 0.5F);
        const auto transform = [&](ImDrawList* drawList, int begin, int end) noexcept {
            if (drawList == nullptr) return;
            begin = std::clamp(begin, 0, drawList->VtxBuffer.Size);
            end = std::clamp(end, begin, drawList->VtxBuffer.Size);
            for (int vertex = begin; vertex < end; ++vertex) {
                ImDrawVert& drawVertex = drawList->VtxBuffer[vertex];
                if (std::abs(panelScale - 1.0F) >= 0.0001F) {
                    drawVertex.pos.x = center.x +
                        (drawVertex.pos.x - center.x) * panelScale;
                    drawVertex.pos.y = center.y +
                        (drawVertex.pos.y - center.y) * panelScale;
                }
                const unsigned alpha = static_cast<unsigned>(drawVertex.col >> 24U);
                const unsigned faded = static_cast<unsigned>(std::clamp(
                    std::lround(static_cast<float>(alpha) * panelAlphaEase),
                    0L, 255L));
                drawVertex.col = (drawVertex.col & 0x00FFFFFFU) |
                    (faded << 24U);
            }
            if (std::abs(panelScale - 1.0F) >= 0.0001F) {
                for (ImDrawCmd& command : drawList->CmdBuffer) {
                    command.ClipRect.x = center.x +
                        (command.ClipRect.x - center.x) * panelScale;
                    command.ClipRect.y = center.y +
                        (command.ClipRect.y - center.y) * panelScale;
                    command.ClipRect.z = center.x +
                        (command.ClipRect.z - center.x) * panelScale;
                    command.ClipRect.w = center.y +
                        (command.ClipRect.w - center.y) * panelScale;
                }
            }
        };
        transform(panelDraw, panelBegin, panelEnd);
        if (entriesDraw != panelDraw) transform(entriesDraw, entriesBegin, entriesEnd);
    }

    renderMediaOverlay(delta, uiScale, interactive);
    renderToasts(delta, uiScale);

    // Never render a software cursor. Windows remains the only cursor owner,
    // preserving the exact system/game DPI-scaled pointer size and avoiding a
    // second sprite that can race the title-screen cursor.
    io.MouseDrawCursor = false;
    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    return newlyInitialized;
}

LRESULT OverlayRenderer::handleWindowMessage(void* const context,
                                             HWND const window,
                                             const UINT message,
                                             const WPARAM wParam,
                                             const LPARAM lParam,
                                             bool& handled) noexcept
{
    return onWindowMessage(*static_cast<OverlayInputState*>(context),
                           window, message, wParam, lParam, handled);
}

LRESULT OverlayRenderer::onWindowMessage(OverlayInputState& input,
                                         HWND const window,
                                         const UINT message,
                                         const WPARAM wParam,
                                         const LPARAM lParam,
                                         bool& handled) noexcept
{
    if (message == imeShutdownMessage() || message == WM_NCDESTROY) {
        if (input.tsf) input.tsf->shutdownOnWindowThread();
        if (message == imeShutdownMessage()) { handled = true; return 0; }
    } else if (input.tsf) {
        input.tsf->enableOnWindowThread(input.imeEnabled.load(std::memory_order_acquire));
    }
    const ImeMessageAction imeAction=classifyImeMessage(message,wParam,
        input.imeEnabled.load(std::memory_order_acquire));
    if(imeAction!=ImeMessageAction::Ignore)
        updateImeState(input,window,imeAction,lParam);
    const bool firstKeyDown = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
                              (lParam & (1LL << 30)) == 0;
    if (firstKeyDown) {
        const UINT scanCode = static_cast<UINT>((lParam >> 16) & 0xFF);
        const UINT extendedScan = scanCode | ((lParam & (1LL << 24)) != 0 ? 0xE000U : 0U);
        const UINT resolvedKey = ::MapVirtualKeyW(extendedScan, MAPVK_VSC_TO_VK_EX);
        const unsigned eventKey = resolvedKey != 0U
            ? resolvedKey : static_cast<unsigned>(wParam);
        if (input.captureHotkey.exchange(false, std::memory_order_acq_rel)) {
            if (eventKey >= 8U && eventKey <= 254U &&
                eventKey != VK_LBUTTON && eventKey != VK_RBUTTON &&
                eventKey != VK_MBUTTON && eventKey != VK_XBUTTON1 &&
                eventKey != VK_XBUTTON2) {
                input.capturedHotkey.store(eventKey, std::memory_order_release);
            }
            handled = true;
            return 1;
        }
        if(!input.interactive.load(std::memory_order_acquire) &&
           !input.gameScreenOpen.load(std::memory_order_acquire) &&
           !input.composingInput.load(std::memory_order_acquire)) {
            const auto matches=[&](const int key) noexcept {
                return key>=8 && key<=254 &&
                    (static_cast<unsigned>(wParam)==static_cast<unsigned>(key) ||
                     eventKey==static_cast<unsigned>(key));
            };
            MediaAction action=MediaAction::None;
            if(matches(input.mediaPreviousHotkey.load(std::memory_order_acquire)))
                action=MediaAction::Previous;
            else if(matches(input.mediaToggleHotkey.load(std::memory_order_acquire)))
                action=MediaAction::Toggle;
            else if(matches(input.mediaNextHotkey.load(std::memory_order_acquire)))
                action=MediaAction::Next;
            if(action!=MediaAction::None) {
                const int key=action==MediaAction::Previous
                    ? input.mediaPreviousHotkey.load(std::memory_order_acquire)
                    : action==MediaAction::Toggle
                        ? input.mediaToggleHotkey.load(std::memory_order_acquire)
                        : input.mediaNextHotkey.load(std::memory_order_acquire);
                // Native transport keys are already consumed by Windows. A
                // custom key must be forwarded once through the helper.
                if(key!=VK_MEDIA_PREV_TRACK && key!=VK_MEDIA_PLAY_PAUSE &&
                   key!=VK_MEDIA_NEXT_TRACK) {
                    // Dispatch is owned by the physical-edge reader, not by
                    // both WndProc and render polling (which doubled actions).
                    // A configured ordinary key belongs to the media binding;
                    // do not also deliver it to Minecraft's gameplay input.
                    handled=true;
                    return 1;
                }
            }
        }
        if (wParam == VK_ESCAPE && input.interactive.load(std::memory_order_acquire)) {
            if (::GetCapture() == window) ::ReleaseCapture();
            input.clickGuiToggle.store(true, std::memory_order_release);
            handled = true;
            return 1;
        }
        const unsigned configured = input.menuHotkey.load(std::memory_order_acquire);
        if ((static_cast<unsigned>(wParam) == configured || resolvedKey == configured) &&
            !input.composingInput.load(std::memory_order_acquire) &&
            (!input.gameScreenOpen.load(std::memory_order_acquire) ||
             input.interactive.load(std::memory_order_acquire))) {
            if (input.interactive.load(std::memory_order_acquire) &&
                ::GetCapture() == window) {
                ::ReleaseCapture();
            }
            input.clickGuiToggle.store(true, std::memory_order_release);
            handled = true;
            return 1;
        }
    }
    ImGuiContext* const context = input.imguiContext.load(std::memory_order_acquire);
    const bool directBackend =
        input.directImGuiWndProc.load(std::memory_order_acquire);
    if (input.acceptImGuiMessages.load(std::memory_order_acquire) &&
        input.interactive.load(std::memory_order_acquire) && context != nullptr &&
        directBackend) {
        ImGui::SetCurrentContext(context);
        (void)ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    }
    // When the Click GUI is open Minecraft must not receive any relative/raw
    // movement or gameplay key, even if ImGui currently has no hovered item.
    // WM_INPUT is cleaned up through DefWindowProc but never forwarded into the
    // client WndProc. This is what prevents Lunar's camera from rotating.
    const bool clientCursorMessage = message == WM_SETCURSOR &&
        LOWORD(lParam) == HTCLIENT;
    const bool overlayMouseMessage = isMouseMessage(message) &&
        (message != WM_SETCURSOR || clientCursorMessage);
    const bool interactiveNow = input.interactive.load(std::memory_order_acquire);
    if (interactiveNow && !directBackend) {
        // The official backend performs this exact capture transition on its
        // owning thread. Lunar's split presentation thread cannot call that
        // backend from WndProc, so mirror only the Win32 capture portion here.
        // This keeps drag delivery continuous when the pointer crosses a card
        // or the game client boundary; position still comes from one absolute
        // GetCursorPos source on the render thread.
        if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
            message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) {
            if (::GetCapture() == nullptr) ::SetCapture(window);
        } else if (message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
                   message == WM_MBUTTONUP || message == WM_XBUTTONUP) {
            const bool anyButtonDown =
                (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
            if (!anyButtonDown && ::GetCapture() == window) ::ReleaseCapture();
        }
    }
    if (clientCursorMessage && interactiveNow) {
        // Preserve the exact cursor Minecraft exposed when the GUI opened.
        // Search fields intentionally have no cursor override, including no
        // I-beam and no per-mouse-move SetCursor race on Lunar.
        if (HCURSOR const cursor=input.sessionCursor.load(std::memory_order_acquire);
            cursor!=nullptr && ::GetCursor()!=cursor) {
            ::SetCursor(cursor);
        }
        handled=true;
        return TRUE;
    }
    if (interactiveNow &&
        (overlayMouseMessage || isKeyboardMessage(message))) {
        handled = true;
        return message == WM_INPUT
            ? ::DefWindowProcW(window, message, wParam, lParam) : 1;
    }
    handled = false;
    return 0;
}

} // namespace mcoverlay
