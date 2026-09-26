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

namespace renderer_detail {

ImVec4 mixColor(const ImVec4& dark, const ImVec4& light, float amount) noexcept
{
        return ImVec4(dark.x + (light.x - dark.x) * amount,
                      dark.y + (light.y - dark.y) * amount,
                      dark.z + (light.z - dark.z) * amount,
                      dark.w + (light.w - dark.w) * amount);
    }


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
            // Solid surfaces must not be composited five times: translucent
            // dark cards otherwise flash darker while the blur taps disappear.
            const ImVec2 white=ImGui::GetDrawListSharedData()->TexUvWhitePixel;
            const bool solid=std::abs(vertex.uv.x-white.x)<1.0e-6F&&
                std::abs(vertex.uv.y-white.y)<1.0e-6F;
            const unsigned blurredAlpha=solid?0U:static_cast<unsigned>(std::lround(
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
                      float delta, ImGuiWindowFlags extra) noexcept
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

void drawProjectedBox(ImDrawList* const drawList,
                      const WorldCameraSnapshot& camera,
                      const ImVec2 displaySize,
                      const AxisAlignedBox& box,
                      const ImU32 color,
                      const char* const label,
                      const bool filled) noexcept
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

ImU32 packedRgbColor(const std::uint32_t rgb, const int alpha) noexcept
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
    const ImVec4 onColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
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
    const ImVec4 onKnob = lightSurface ? ImVec4(0.99F,0.985F,1.0F,1.0F)
        : ImVec4(0.10F,0.09F,0.14F,1.0F);
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

} // namespace renderer_detail

using namespace renderer_detail;


} // namespace mcoverlay
