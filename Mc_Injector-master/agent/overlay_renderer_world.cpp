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

void OverlayRenderer::renderWorldOverlay(RenderFrameContext& frame) noexcept
{
    const auto& snapshot = frame.snapshot;
    const auto& uiScale = frame.uiScale;
    const auto& delta = frame.delta;
    const auto& gameplayHotkeysAllowed = frame.gameplayHotkeysAllowed;

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
                const char* shownName=entity.displayName[0]?entity.displayName.data():entity.playerName.data();
                const bool nametagAllowed = entity.player &&
                    (entity.confirmedPlayer || m_features.nametagAlways) &&
                    m_features.nametagEnabled && shownName[0] != '\0' &&
                    (!isTeammate || m_features.showTeammateNametags) &&
                    (isTeammate || !m_features.nametagNearbyEnemiesOnly ||
                     entity.distance <= static_cast<double>(m_features.nametagRange));
                if (nametagAllowed) {
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
                            shownName);
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

}

} // namespace mcoverlay
