#include "AgentRuntime.h"

#include "AgentLog.h"
#include "bindings/GameBindings.h"
#include "bindings/SmartHotbarPolicy.h"
#include "ipc_client.h"
#include "jvm.h"
#include "opengl_hook.h"
#include "overlay_renderer.h"

#include <process.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>

#include "AgentRuntimeFeatures.internal.h"

namespace mcoverlay {
using namespace runtime_detail;

namespace runtime_detail {
std::uint32_t packFeatures(const FeatureSettings& settings) noexcept
{
    return static_cast<std::uint32_t>((settings.espEnabled ? 0x01U : 0U) |
        (settings.entityEspEnabled ? 0x02U : 0U) |
        (settings.bedEspEnabled ? 0x04U : 0U) |
        (settings.labelsEnabled ? 0x08U : 0U) |
        (settings.hypixelPanelEnabled ? 0x10U : 0U) |
        (settings.bedThreatAlertsEnabled ? 0x20U : 0U) |
        (settings.bedDefensePanelEnabled ? 0x40U : 0U) |
        (settings.entityEspPlayersOnly ? 0x80U : 0U) |
        (settings.bedAutoRefreshEnabled ? 0x100U : 0U) |
        (settings.bedEspFilled ? 0x200U : 0U) |
        (settings.debugChatEnabled ? 0x400U : 0U) |
        (settings.showOwnBedDefenseInfo ? 0x800U : 0U) |
        (settings.showTeammateBoxes ? 0x1000U : 0U) |
        (settings.bedDefenseHoldToShow ? 0x2000U : 0U) |
        (settings.bedDefensePerspectiveScale ? 0x4000U : 0U) |
        (settings.hypixelPanelHoldToShow ? 0x8000U : 0U) |
        (settings.nametagEnabled ? 0x10000U : 0U) |
        (settings.nametagSidePlacement ? 0x20000U : 0U) |
        (settings.enemyItemIndicatorsEnabled ? 0x40000U : 0U) |
        (settings.showTeammateNametags ? 0x80000U : 0U) |
        (settings.nametagNearbyEnemiesOnly ? 0x100000U : 0U) |
        (settings.nametagTeamPulse ? 0x200000U : 0U) |
        (settings.showTeammateArrows ? 0x400000U : 0U) |
        (settings.safewalkEnabled ? 0x800000U : 0U) |
        (settings.scaffoldEnabled ? 0x1000000U : 0U) |
        (settings.flyEnabled ? 0x2000000U : 0U) |
        (settings.bhopEnabled ? 0x4000000U : 0U) |
        (settings.bhopAutoJump ? 0x8000000U : 0U) |
        (settings.aimAssistEnabled ? 0x10000000U : 0U) |
        (settings.aimLockOnMode ? 0x20000000U : 0U) |
        (settings.textGuiEnabled ? 0x40000000U : 0U) |
        (settings.allowHypixelMovement ? 0x80000000U : 0U));
}

std::uint32_t packExtraFeatures(const FeatureSettings& settings) noexcept
{
    return static_cast<std::uint32_t>(
        (settings.aimNearestPriority ? 0x01U : 0U) |
        (settings.textGuiVerticalLine ? 0x02U : 0U) |
        (settings.knockbackPredictionEnabled ? 0x04U : 0U) |
        (settings.bowPredictionEnabled ? 0x08U : 0U) |
        (settings.localMobAuraEnabled ? 0x10U : 0U) |
        (settings.localVelocityEnabled ? 0x20U : 0U) |
        (settings.scaffoldSameLayerOnly ? 0x40U : 0U) |
        (settings.fullscreenImeFixEnabled ? 0x80U : 0U)) |
        packUiPreferences({settings.clickGuiBlur, settings.imePanelX, settings.imePanelY});
}

std::uint32_t packAimOptions(const FeatureSettings& settings) noexcept
{
    return (settings.aimSilentLock ? 0x01U : 0U) |
        (settings.aimScannerEnabled ? 0x02U : 0U) |
        (settings.bedBreakerEnabled ? 0x04U : 0U) |
        (settings.aimAttackViability ? 0x08U : 0U) |
        (settings.textGuiShowModes ? 0x10U : 0U) |
        (settings.silentControlAdaptation ? 0x40000000U : 0U) |
        (settings.aimSequentialTargets ? 0x20000000U : 0U) |
        (settings.silentFileDebug ? 0x08000000U : 0U) |
        (settings.silentChatDebug ? 0x10000000U : 0U) |
        (static_cast<std::uint32_t>(std::clamp(
            settings.localVelocityProbability,0,100)) << 5U) |
        (static_cast<std::uint32_t>(std::clamp(
            settings.localVelocityVerticalPercent,0,100)) << 12U) |
        (static_cast<std::uint32_t>(std::clamp(
            settings.featureHotkeys[16U],0,254)) << 19U);
}

std::uint64_t packFeatureHotkeys(const FeatureSettings& settings,
                                 const std::size_t first) noexcept
{
    std::uint64_t packed = 0U;
    for (std::size_t offset = 0U; offset < 8U; ++offset) {
        const std::size_t index = first + offset;
        if (index >= settings.featureHotkeys.size()) break;
        const auto key = static_cast<std::uint64_t>(std::clamp(
            settings.featureHotkeys[index], 0, 254));
        packed |= key << (offset * 8U);
    }
    return packed;
}

std::uint32_t packFeatureHotkeysExtra(const FeatureSettings& settings) noexcept
{
    return static_cast<std::uint32_t>(
        std::clamp(settings.featureHotkeys[16U],0,254)) |
        (static_cast<std::uint32_t>(
            std::clamp(settings.featureHotkeys[17U],0,254))<<8U) |
        (settings.freeLookEnabled ? 0x10000U : 0U) |
        (settings.attackShieldEnabled ? 0x20000U : 0U) |
        (settings.attackShieldWildcard ? 0x40000U : 0U);
}

FeatureSettings unpackFeatures(const std::uint32_t bits,
                               const int defenseRadius,
                               const int threatRadius,
                               const int bedHotkey,
                                const int panelOpacity,
                                const int hypixelHotkey,
                                const int hypixelOpacity,
                                const int hypixelScale,
                                const int hypixelX,
                                const int hypixelY,
                                const bool clickGuiLightTheme,
                                const std::uint32_t playerColor,
                                const std::uint32_t bedColor,
                                const std::uint32_t panelColor,
                                const std::uint32_t hypixelColor,
                                const int hypixelHeight,
                                const int nametagOpacity,
                                const std::uint32_t nametagColor,
                                const std::uint32_t accentColor,
                                const int hypixelFontIndex,
                                const int nametagRange,
                                const int nametagSizeIndex,
                                const std::uint32_t hypixelRailColor,
                                const int hypixelRailOpacity,
                                const int safewalkReleaseDelayMs,
                                const int safewalkEdgeSensitivity,
                                const int safewalkMinimumPitch,
                                const int safewalkHotkey,
                                const int flySpeedPercent,
                                const int aimSlowdownPercent,
                                const int aimSpeedPercent,
                                const std::uint32_t textGuiColor,
                                const int textGuiX,
                                const int textGuiY,
                                const int bhopAirSpeedPercent,
                                const std::uint64_t hotkeysPackedA,
                                const std::uint64_t hotkeysPackedB,
                                const bool fireballEspEnabled,
                                const bool fireballEspFilled,
                                const bool longJumpEnabled,
                                const int longJumpSpeedPercent,
                                const std::uint32_t fireballEspColor,
                                const int aimMinimumDistance,
                                const int aimMaximumDistance,
                                const int aimFovDegrees,
                                const int clickGuiWidthPercent,
                                const int clickGuiHeightPercent,
                                const int clickGuiOpacity,
                                const std::uint32_t extraBits,
                                const int textGuiAlignment,
                                const int localMobReach,
                                const int localAttackDelayMs,
                                const int localVelocityPercent,
                                const std::uint32_t hotkeysPackedC) noexcept
{
    FeatureSettings s;
    s.espEnabled = (bits & 0x01U) != 0U;
    s.entityEspEnabled = (bits & 0x02U) != 0U;
    s.bedEspEnabled = (bits & 0x04U) != 0U;
    s.labelsEnabled = (bits & 0x08U) != 0U;
    s.hypixelPanelEnabled = (bits & 0x10U) != 0U;
    s.bedThreatAlertsEnabled = (bits & 0x20U) != 0U;
    s.bedDefensePanelEnabled = (bits & 0x40U) != 0U;
    s.entityEspPlayersOnly = (bits & 0x80U) != 0U;
    s.bedAutoRefreshEnabled = (bits & 0x100U) != 0U;
    s.bedEspFilled = (bits & 0x200U) != 0U;
    s.debugChatEnabled = (bits & 0x400U) != 0U;
    s.showOwnBedDefenseInfo = (bits & 0x800U) != 0U;
    s.showTeammateBoxes = (bits & 0x1000U) != 0U;
    s.bedDefenseHoldToShow = (bits & 0x2000U) != 0U;
    s.bedDefensePerspectiveScale = (bits & 0x4000U) != 0U;
    s.hypixelPanelHoldToShow = (bits & 0x8000U) != 0U;
    s.nametagEnabled = (bits & 0x10000U) != 0U;
    s.nametagSidePlacement = (bits & 0x20000U) != 0U;
    s.enemyItemIndicatorsEnabled = (bits & 0x40000U) != 0U;
    s.showTeammateNametags = (bits & 0x80000U) != 0U;
    s.nametagNearbyEnemiesOnly = (bits & 0x100000U) != 0U;
    s.nametagTeamPulse = (bits & 0x200000U) != 0U;
    s.showTeammateArrows = (bits & 0x400000U) != 0U;
    s.safewalkEnabled = (bits & 0x800000U) != 0U;
    s.scaffoldEnabled = (bits & 0x1000000U) != 0U;
    s.flyEnabled = (bits & 0x2000000U) != 0U;
    s.bhopEnabled = (bits & 0x4000000U) != 0U;
    s.bhopAutoJump = (bits & 0x8000000U) != 0U;
    s.aimAssistEnabled = (bits & 0x10000000U) != 0U;
    s.aimLockOnMode = (bits & 0x20000000U) != 0U;
    s.textGuiEnabled = (bits & 0x40000000U) != 0U;
    s.allowHypixelMovement = (bits & 0x80000000U) != 0U;
    s.bedDefenseRadius = std::clamp(defenseRadius, 3, 10);
    s.bedThreatRadius = std::clamp(threatRadius, 3, 32);
    s.bedDefenseHotkey = std::clamp(bedHotkey, 0, 254);
    s.bedDefensePanelOpacity = std::clamp(panelOpacity, 0, 100);
    s.hypixelPanelHotkey = std::clamp(hypixelHotkey, 0, 254);
    s.hypixelPanelOpacity = std::clamp(hypixelOpacity, 0, 100);
    s.hypixelPanelScale = std::clamp(hypixelScale, 70, 160);
    s.hypixelPanelHeight = std::clamp(hypixelHeight, 60, 400);
    s.hypixelPanelX = std::clamp(hypixelX, -1, 1000);
    s.hypixelPanelY = std::clamp(hypixelY, -1, 1000);
    s.clickGuiLightTheme = clickGuiLightTheme;
    s.playerEspColor = playerColor & 0xFFFFFFU;
    s.bedEspColor = bedColor & 0xFFFFFFU;
    s.bedDefensePanelColor = panelColor & 0xFFFFFFU;
    s.hypixelPanelColor = hypixelColor & 0xFFFFFFU;
    s.nametagPanelOpacity = std::clamp(nametagOpacity, 10, 100);
    s.nametagPanelColor = nametagColor & 0xFFFFFFU;
    s.clickGuiAccentColor = accentColor & 0xFFFFFFU;
    s.hypixelPanelFontIndex = std::clamp(hypixelFontIndex, 0, 3);
    s.nametagRange = std::clamp(nametagRange, 4, 128);
    s.nametagSizeIndex = std::clamp(nametagSizeIndex, 0, 3);
    s.hypixelRailColor = hypixelRailColor & 0xFFFFFFU;
    s.hypixelRailOpacity = std::clamp(hypixelRailOpacity, 0, 100);
    s.safewalkReleaseDelayMs = std::clamp(safewalkReleaseDelayMs, 0, 750);
    s.safewalkEdgeSensitivity = std::clamp(safewalkEdgeSensitivity, 0, 100);
    s.safewalkMinimumPitch = std::clamp(safewalkMinimumPitch, -90, 90);
    s.safewalkHotkey = std::clamp(safewalkHotkey, 0, 254);
    s.flySpeedPercent = std::clamp(flySpeedPercent, 10, 500);
    s.aimSlowdownPercent = std::clamp(aimSlowdownPercent, 5, 95);
    s.aimSpeedPercent = std::clamp(aimSpeedPercent, 1, 100);
    s.textGuiColor = textGuiColor & 0xFFFFFFU;
    s.textGuiX = std::clamp(textGuiX, -1, 1000);
    s.textGuiY = std::clamp(textGuiY, -1, 1000);
    s.bhopAirSpeedPercent = std::clamp(bhopAirSpeedPercent, 10, 300);
    for (std::size_t index = 0U; index < 16U; ++index) {
        const std::uint64_t packed=index<8U?hotkeysPackedA:hotkeysPackedB;
        const std::size_t offset=index<8U?index:index-8U;
        s.featureHotkeys[index]=static_cast<int>(
            (packed>>(offset*8U))&0xFFU);
    }
    s.featureHotkeys[16U]=static_cast<int>(hotkeysPackedC&0xFFU);
    s.featureHotkeys[17U]=static_cast<int>((hotkeysPackedC>>8U)&0xFFU);
    s.freeLookEnabled=(hotkeysPackedC&0x10000U)!=0U;
    s.attackShieldEnabled=(hotkeysPackedC&0x20000U)!=0U;
    s.attackShieldWildcard=(hotkeysPackedC&0x40000U)!=0U;
    // Preserve the legacy Safewalk binding as a migration source. New builds
    // keep both fields synchronized, while old settings remain usable.
    if (s.featureHotkeys[4U] == 0) s.featureHotkeys[4U] = s.safewalkHotkey;
    else s.safewalkHotkey = s.featureHotkeys[4U];
    s.fireballEspEnabled = fireballEspEnabled;
    s.fireballEspFilled = fireballEspFilled;
    s.longJumpEnabled = longJumpEnabled;
    s.longJumpSpeedPercent = std::clamp(longJumpSpeedPercent, 25, 250);
    s.fireballEspColor = fireballEspColor & 0xFFFFFFU;
    s.aimMinimumDistance = std::clamp(aimMinimumDistance, 0, 64);
    s.aimMaximumDistance = std::clamp(aimMaximumDistance,
                                      std::max(1, s.aimMinimumDistance), 128);
    s.aimFovDegrees = std::clamp(aimFovDegrees, 1, 360);
    s.clickGuiWidthPercent = std::clamp(clickGuiWidthPercent, 80, 150);
    s.clickGuiHeightPercent = std::clamp(clickGuiHeightPercent, 80, 150);
    s.clickGuiOpacity = std::clamp(clickGuiOpacity, 35, 100);
    s.aimNearestPriority = (extraBits & 0x01U) != 0U;
    s.textGuiVerticalLine = (extraBits & 0x02U) != 0U;
    s.knockbackPredictionEnabled = (extraBits & 0x04U) != 0U;
    s.bowPredictionEnabled = (extraBits & 0x08U) != 0U;
    s.localMobAuraEnabled = (extraBits & 0x10U) != 0U;
    s.localVelocityEnabled = (extraBits & 0x20U) != 0U;
    s.scaffoldSameLayerOnly = (extraBits & 0x40U) != 0U;
    s.fullscreenImeFixEnabled = (extraBits & 0x80U) != 0U;
    const auto ui = unpackUiPreferences(extraBits);
    s.clickGuiBlur = ui.blur;
    s.imePanelX = ui.imeX;
    s.imePanelY = ui.imeY;
    s.textGuiAlignment = std::clamp(textGuiAlignment, 0, 2);
    s.localMobReach = std::clamp(localMobReach, 3, 10);
    s.localAttackDelayMs = std::clamp(localAttackDelayMs, 100, 1500);
    s.localVelocityPercent = std::clamp(localVelocityPercent, 0, 100);
    return s;
}

}

void AgentRuntime::queueFeatureChanged(const FeatureSettings& settings) noexcept
{
    const unsigned aimOptions=packAimOptions(settings);
    const std::uint32_t smartHotbarConfig=hotbar::pack(
        settings.smartHotbarEnabled,settings.smartHotbarActions,
        settings.smartHotbarRefill,settings.sprintEnabled,settings.nametagAlways);
    m_aimOptions.store(aimOptions,std::memory_order_release);
    m_featureChangedAimOptions.store(aimOptions,std::memory_order_relaxed);
    const std::uint32_t bits = packFeatures(settings);
    m_featureBits.store(bits, std::memory_order_release);
    m_bedDefenseRadius.store(std::clamp(settings.bedDefenseRadius, 3, 10),
                             std::memory_order_release);
    m_bedThreatRadius.store(std::clamp(settings.bedThreatRadius, 3, 32),
                            std::memory_order_release);
    m_bedDefenseHotkey.store(std::clamp(settings.bedDefenseHotkey, 0, 254),
                             std::memory_order_release);
    m_bedDefensePanelOpacity.store(std::clamp(settings.bedDefensePanelOpacity, 0, 100),
                                   std::memory_order_release);
    m_hypixelPanelHotkey.store(std::clamp(settings.hypixelPanelHotkey, 0, 254),
                               std::memory_order_release);
    m_hypixelPanelOpacity.store(std::clamp(settings.hypixelPanelOpacity, 0, 100),
                                std::memory_order_release);
    m_hypixelPanelScale.store(std::clamp(settings.hypixelPanelScale, 70, 160),
                              std::memory_order_release);
    m_hypixelPanelHeight.store(std::clamp(settings.hypixelPanelHeight, 60, 400),
                               std::memory_order_release);
    m_hypixelPanelX.store(std::clamp(settings.hypixelPanelX, -1, 1000),
                          std::memory_order_release);
    m_hypixelPanelY.store(std::clamp(settings.hypixelPanelY, -1, 1000),
                          std::memory_order_release);
    m_clickGuiLightTheme.store(settings.clickGuiLightTheme,
                               std::memory_order_release);
    m_playerEspColor.store(settings.playerEspColor & 0xFFFFFFU,
                           std::memory_order_release);
    m_bedEspColor.store(settings.bedEspColor & 0xFFFFFFU,
                        std::memory_order_release);
    m_bedDefensePanelColor.store(settings.bedDefensePanelColor & 0xFFFFFFU,
                                 std::memory_order_release);
    m_hypixelPanelColor.store(settings.hypixelPanelColor & 0xFFFFFFU,
                              std::memory_order_release);
    m_nametagPanelOpacity.store(std::clamp(settings.nametagPanelOpacity, 10, 100),
                                std::memory_order_release);
    m_nametagPanelColor.store(settings.nametagPanelColor & 0xFFFFFFU,
                              std::memory_order_release);
    m_clickGuiAccentColor.store(settings.clickGuiAccentColor & 0xFFFFFFU,
                                std::memory_order_release);
    m_hypixelRailColor.store(settings.hypixelRailColor & 0xFFFFFFU,
                             std::memory_order_release);
    m_hypixelRailOpacity.store(std::clamp(settings.hypixelRailOpacity, 0, 100),
                               std::memory_order_release);
    m_hypixelPanelFontIndex.store(std::clamp(settings.hypixelPanelFontIndex, 0, 3),
                                  std::memory_order_release);
    m_nametagRange.store(std::clamp(settings.nametagRange, 4, 128),
                         std::memory_order_release);
    m_nametagSizeIndex.store(std::clamp(settings.nametagSizeIndex, 0, 3),
                             std::memory_order_release);
    m_safewalkReleaseDelayMs.store(
        std::clamp(settings.safewalkReleaseDelayMs, 0, 750),
        std::memory_order_release);
    m_safewalkEdgeSensitivity.store(
        std::clamp(settings.safewalkEdgeSensitivity, 0, 100),
        std::memory_order_release);
    m_safewalkMinimumPitch.store(
        std::clamp(settings.safewalkMinimumPitch, -90, 90),
        std::memory_order_release);
    m_safewalkHotkey.store(std::clamp(settings.safewalkHotkey, 0, 254),
                            std::memory_order_release);
    m_flySpeedPercent.store(std::clamp(settings.flySpeedPercent, 10, 500),
                            std::memory_order_release);
    m_bhopAirSpeedPercent.store(
        std::clamp(settings.bhopAirSpeedPercent, 10, 300),
        std::memory_order_release);
    m_featureHotkeysPackedA.store(packFeatureHotkeys(settings, 0U),
                                  std::memory_order_release);
    m_featureHotkeysPackedB.store(packFeatureHotkeys(settings, 8U),
                                  std::memory_order_release);
    m_featureHotkeysPackedC.store(packFeatureHotkeysExtra(settings),
                                  std::memory_order_release);
    m_fireballEspEnabled.store(settings.fireballEspEnabled, std::memory_order_release);
    m_fireballEspFilled.store(settings.fireballEspFilled, std::memory_order_release);
    m_longJumpEnabled.store(settings.longJumpEnabled, std::memory_order_release);
    m_longJumpSpeedPercent.store(std::clamp(settings.longJumpSpeedPercent, 25, 250),
                                 std::memory_order_release);
    m_fireballEspColor.store(settings.fireballEspColor & 0xFFFFFFU,
                             std::memory_order_release);
    m_aimSlowdownPercent.store(std::clamp(settings.aimSlowdownPercent, 5, 95),
                               std::memory_order_release);
    m_aimSpeedPercent.store(std::clamp(settings.aimSpeedPercent, 1, 100),
                            std::memory_order_release);
    m_aimMinimumDistance.store(std::clamp(settings.aimMinimumDistance, 0, 64),
                               std::memory_order_release);
    m_aimMaximumDistance.store(std::clamp(settings.aimMaximumDistance,
        std::max(1, settings.aimMinimumDistance), 128), std::memory_order_release);
    m_aimFovDegrees.store(std::clamp(settings.aimFovDegrees, 1, 360),
                          std::memory_order_release);
    m_aimAttackCps.store(std::clamp(settings.aimAttackCps,1,20),
                         std::memory_order_release);
    m_clickGuiWidthPercent.store(std::clamp(settings.clickGuiWidthPercent, 80, 150),
                                 std::memory_order_release);
    m_clickGuiHeightPercent.store(std::clamp(settings.clickGuiHeightPercent, 80, 150),
                                  std::memory_order_release);
    m_clickGuiOpacity.store(std::clamp(settings.clickGuiOpacity, 35, 100),
                            std::memory_order_release);
    m_featureExtraBits.store(packExtraFeatures(settings), std::memory_order_release);
    m_smartHotbarConfig.store(smartHotbarConfig,std::memory_order_release);
    m_textGuiAlignment.store(std::clamp(settings.textGuiAlignment, 0, 2),
                             std::memory_order_release);
    m_localMobReach.store(std::clamp(settings.localMobReach, 3, 10),
                          std::memory_order_release);
    m_localAttackDelayMs.store(std::clamp(settings.localAttackDelayMs, 100, 1500),
                               std::memory_order_release);
    m_localVelocityPercent.store(std::clamp(settings.localVelocityPercent, 0, 100),
                                 std::memory_order_release);
    m_textGuiColor.store(settings.textGuiColor & 0xFFFFFFU,
                         std::memory_order_release);
    m_textGuiX.store(std::clamp(settings.textGuiX, -1, 1000),
                     std::memory_order_release);
    m_textGuiY.store(std::clamp(settings.textGuiY, -1, 1000),
                     std::memory_order_release);
    m_featureChangedBits.store(bits, std::memory_order_relaxed);
    m_featureChangedBedRadius.store(std::clamp(settings.bedDefenseRadius, 3, 10),
                                    std::memory_order_relaxed);
    m_featureChangedThreatRadius.store(std::clamp(settings.bedThreatRadius, 3, 32),
                                       std::memory_order_relaxed);
    m_featureChangedBedHotkey.store(std::clamp(settings.bedDefenseHotkey, 0, 254),
                                    std::memory_order_relaxed);
    m_featureChangedPanelOpacity.store(
        std::clamp(settings.bedDefensePanelOpacity, 0, 100),
        std::memory_order_relaxed);
    m_featureChangedHypixelHotkey.store(
        std::clamp(settings.hypixelPanelHotkey, 0, 254), std::memory_order_relaxed);
    m_featureChangedHypixelOpacity.store(
        std::clamp(settings.hypixelPanelOpacity, 0, 100), std::memory_order_relaxed);
    m_featureChangedHypixelScale.store(
        std::clamp(settings.hypixelPanelScale, 70, 160), std::memory_order_relaxed);
    m_featureChangedHypixelHeight.store(
        std::clamp(settings.hypixelPanelHeight, 60, 400), std::memory_order_relaxed);
    m_featureChangedHypixelX.store(
        std::clamp(settings.hypixelPanelX, -1, 1000), std::memory_order_relaxed);
    m_featureChangedHypixelY.store(
        std::clamp(settings.hypixelPanelY, -1, 1000), std::memory_order_relaxed);
    m_featureChangedClickGuiLightTheme.store(settings.clickGuiLightTheme,
                                              std::memory_order_relaxed);
    m_featureChangedPlayerColor.store(settings.playerEspColor & 0xFFFFFFU,
                                      std::memory_order_relaxed);
    m_featureChangedBedColor.store(settings.bedEspColor & 0xFFFFFFU,
                                   std::memory_order_relaxed);
    m_featureChangedPanelColor.store(settings.bedDefensePanelColor & 0xFFFFFFU,
                                     std::memory_order_relaxed);
    m_featureChangedHypixelColor.store(settings.hypixelPanelColor & 0xFFFFFFU,
                                       std::memory_order_relaxed);
    m_featureChangedNametagOpacity.store(
        std::clamp(settings.nametagPanelOpacity, 10, 100), std::memory_order_relaxed);
    m_featureChangedNametagColor.store(settings.nametagPanelColor & 0xFFFFFFU,
                                       std::memory_order_relaxed);
    m_featureChangedAccentColor.store(settings.clickGuiAccentColor & 0xFFFFFFU,
                                      std::memory_order_relaxed);
    m_featureChangedHypixelRailColor.store(settings.hypixelRailColor & 0xFFFFFFU,
                                           std::memory_order_relaxed);
    m_featureChangedHypixelRailOpacity.store(
        std::clamp(settings.hypixelRailOpacity, 0, 100),
        std::memory_order_relaxed);
    m_featureChangedHypixelFontIndex.store(
        std::clamp(settings.hypixelPanelFontIndex, 0, 3), std::memory_order_relaxed);
    m_featureChangedNametagRange.store(
        std::clamp(settings.nametagRange, 4, 128), std::memory_order_relaxed);
    m_featureChangedNametagSizeIndex.store(
        std::clamp(settings.nametagSizeIndex, 0, 3), std::memory_order_relaxed);
    m_featureChangedSafewalkReleaseDelayMs.store(
        std::clamp(settings.safewalkReleaseDelayMs, 0, 750),
        std::memory_order_relaxed);
    m_featureChangedSafewalkEdgeSensitivity.store(
        std::clamp(settings.safewalkEdgeSensitivity, 0, 100),
        std::memory_order_relaxed);
    m_featureChangedSafewalkMinimumPitch.store(
        std::clamp(settings.safewalkMinimumPitch, -90, 90),
        std::memory_order_relaxed);
    m_featureChangedSafewalkHotkey.store(
        std::clamp(settings.safewalkHotkey, 0, 254),
        std::memory_order_relaxed);
    m_featureChangedFlySpeedPercent.store(
        std::clamp(settings.flySpeedPercent, 10, 500),
        std::memory_order_relaxed);
    m_featureChangedBhopAirSpeedPercent.store(
        std::clamp(settings.bhopAirSpeedPercent, 10, 300),
        std::memory_order_relaxed);
    m_featureChangedHotkeysPackedA.store(packFeatureHotkeys(settings, 0U),
                                         std::memory_order_relaxed);
    m_featureChangedHotkeysPackedB.store(packFeatureHotkeys(settings, 8U),
                                         std::memory_order_relaxed);
    m_featureChangedHotkeysPackedC.store(packFeatureHotkeysExtra(settings),
                                         std::memory_order_relaxed);
    m_featureChangedFireballEspEnabled.store(settings.fireballEspEnabled,
                                              std::memory_order_relaxed);
    m_featureChangedFireballEspFilled.store(settings.fireballEspFilled,
                                             std::memory_order_relaxed);
    m_featureChangedLongJumpEnabled.store(settings.longJumpEnabled,
                                           std::memory_order_relaxed);
    m_featureChangedLongJumpSpeedPercent.store(
        std::clamp(settings.longJumpSpeedPercent, 25, 250),
        std::memory_order_relaxed);
    m_featureChangedFireballEspColor.store(settings.fireballEspColor & 0xFFFFFFU,
                                            std::memory_order_relaxed);
    m_featureChangedAimSlowdownPercent.store(
        std::clamp(settings.aimSlowdownPercent, 5, 95),
        std::memory_order_relaxed);
    m_featureChangedAimSpeedPercent.store(
        std::clamp(settings.aimSpeedPercent, 1, 100),
        std::memory_order_relaxed);
    m_featureChangedAimMinimumDistance.store(
        std::clamp(settings.aimMinimumDistance, 0, 64), std::memory_order_relaxed);
    m_featureChangedAimMaximumDistance.store(std::clamp(
        settings.aimMaximumDistance, std::max(1, settings.aimMinimumDistance), 128),
        std::memory_order_relaxed);
    m_featureChangedAimFovDegrees.store(
        std::clamp(settings.aimFovDegrees, 1, 360), std::memory_order_relaxed);
    m_featureChangedAimAttackCps.store(
        std::clamp(settings.aimAttackCps,1,20),std::memory_order_relaxed);
    m_featureChangedClickGuiWidthPercent.store(
        std::clamp(settings.clickGuiWidthPercent, 80, 150), std::memory_order_relaxed);
    m_featureChangedClickGuiHeightPercent.store(
        std::clamp(settings.clickGuiHeightPercent, 80, 150), std::memory_order_relaxed);
    m_featureChangedClickGuiOpacity.store(
        std::clamp(settings.clickGuiOpacity, 35, 100), std::memory_order_relaxed);
    m_featureChangedExtraBits.store(packExtraFeatures(settings),
                                    std::memory_order_relaxed);
    m_featureChangedSmartHotbarConfig.store(
        smartHotbarConfig,std::memory_order_relaxed);
    m_featureChangedTextGuiAlignment.store(
        std::clamp(settings.textGuiAlignment, 0, 2), std::memory_order_relaxed);
    m_featureChangedLocalMobReach.store(
        std::clamp(settings.localMobReach, 3, 10), std::memory_order_relaxed);
    m_featureChangedLocalAttackDelayMs.store(
        std::clamp(settings.localAttackDelayMs, 100, 1500),
        std::memory_order_relaxed);
    m_featureChangedLocalVelocityPercent.store(
        std::clamp(settings.localVelocityPercent, 0, 100),
        std::memory_order_relaxed);
    m_featureChangedTextGuiColor.store(settings.textGuiColor & 0xFFFFFFU,
                                       std::memory_order_relaxed);
    m_featureChangedTextGuiX.store(std::clamp(settings.textGuiX, -1, 1000),
                                   std::memory_order_relaxed);
    m_featureChangedTextGuiY.store(std::clamp(settings.textGuiY, -1, 1000),
                                   std::memory_order_relaxed);
    m_featureChangedRevision.fetch_add(1U, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

} // namespace mcoverlay
