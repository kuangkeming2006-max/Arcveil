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


} // namespace mcoverlay
