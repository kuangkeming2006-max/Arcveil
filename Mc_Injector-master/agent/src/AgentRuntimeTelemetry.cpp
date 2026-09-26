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

namespace {
const char* snapshotStateToken(const GameSnapshot::State state) noexcept
{
    switch (state) {
    case GameSnapshot::State::Resolving: return "resolving";
    case GameSnapshot::State::Unsupported: return "unsupported";
    case GameSnapshot::State::WaitingForGameThread: return "waiting_for_game_thread";
    case GameSnapshot::State::NoPlayer: return "no_player";
    case GameSnapshot::State::Ready: return "ready";
    case GameSnapshot::State::JniError: return "jni_error";
    }
    return "unknown";
}

template<std::size_t Capacity>
class FixedLine final {
public:
    [[nodiscard]] bool append(const std::string_view value) noexcept
    {
        if (value.size() > Capacity - m_size) return false;
        std::memcpy(m_data.data() + m_size, value.data(), value.size());
        m_size += value.size();
        return true;
    }

    [[nodiscard]] bool append(const char value) noexcept
    {
        if (m_size == Capacity) return false;
        m_data[m_size++] = value;
        return true;
    }

    template<typename Integer>
    [[nodiscard]] bool appendInteger(const Integer value) noexcept
    {
        static_assert(std::is_integral_v<Integer>);
        const auto result = std::to_chars(m_data.data() + m_size,
                                          m_data.data() + Capacity, value);
        if (result.ec != std::errc{}) return false;
        m_size = static_cast<std::size_t>(result.ptr - m_data.data());
        return true;
    }

    [[nodiscard]] bool appendDouble(const double value) noexcept
    {
        const auto result = std::to_chars(m_data.data() + m_size,
                                          m_data.data() + Capacity, value,
                                          std::chars_format::general, 9);
        if (result.ec != std::errc{}) return false;
        m_size = static_cast<std::size_t>(result.ptr - m_data.data());
        return true;
    }

    [[nodiscard]] std::string_view view() const noexcept
    {
        return {m_data.data(), m_size};
    }

private:
    std::array<char, Capacity> m_data{};
    std::size_t m_size = 0U;
};

template<std::size_t Capacity>
[[nodiscard]] bool appendPercentEncoded(FixedLine<Capacity>& destination,
                                        const char* const value) noexcept
{
    if (value == nullptr || *value == '\0') return destination.append("-");
    constexpr char kHex[] = "0123456789ABCDEF";
    for (const unsigned char byte : std::string_view(value)) {
        const bool unreserved = (byte >= 'A' && byte <= 'Z') ||
                                (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') ||
                                byte == '-' || byte == '_' ||
                                byte == '.' || byte == '~';
        if (unreserved) {
            if (!destination.append(static_cast<char>(byte))) return false;
        } else {
            if (!destination.append('%') ||
                !destination.append(kHex[(byte >> 4U) & 0x0FU]) ||
                !destination.append(kHex[byte & 0x0FU])) return false;
        }
    }
    return true;
}

std::uint64_t unixMillisecondsNow() noexcept
{
    FILETIME time{};
    ::GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    constexpr std::uint64_t kWindowsToUnixEpoch100ns = 116'444'736'000'000'000ULL;
    return ticks.QuadPart >= kWindowsToUnixEpoch100ns
        ? (ticks.QuadPart - kWindowsToUnixEpoch100ns) / 10'000ULL
        : 0U;
}

template<std::size_t Capacity, typename Message>
[[nodiscard]] bool formatGameState(
    FixedLine<Capacity>& line,
    const Message& message) noexcept
{
    const auto token = snapshotStateToken(
        static_cast<GameSnapshot::State>(message.state));
    return line.append("GAME_STATE 1 ") && line.appendInteger(message.sequence) &&
           line.append(' ') && line.appendInteger(message.unixMilliseconds) &&
           line.append(' ') && line.appendInteger(message.valid ? 1 : 0) &&
           line.append(' ') && line.appendDouble(message.health) &&
           line.append(' ') && line.appendDouble(message.maxHealth) &&
           line.append(' ') && line.appendInteger(message.entityId) &&
           line.append(' ') && line.appendDouble(message.x) &&
           line.append(' ') && line.appendDouble(message.y) &&
           line.append(' ') && line.appendDouble(message.z) &&
           line.append(' ') && line.appendInteger(message.loadedEntities) &&
           line.append(' ') && line.appendInteger(message.bedCount) &&
           line.append(' ') && appendPercentEncoded(line, message.mapping.data()) &&
           line.append(' ') && appendPercentEncoded(line, token);
}

}

bool AgentRuntime::launchTelemetry() noexcept
{
    if (m_telemetryEvent == nullptr) {
        log::error("Could not create the telemetry wake event.");
        return false;
    }
    unsigned threadId = 0U;
    m_telemetry = reinterpret_cast<HANDLE>(::_beginthreadex(
        nullptr, 0U, &AgentRuntime::telemetryEntry, this, 0U, &threadId));
    if (m_telemetry == nullptr) {
        log::error("Could not create the IPC telemetry thread.");
        return false;
    }
    return true;
}

unsigned __stdcall AgentRuntime::telemetryEntry(void* const context) noexcept
{
    static_cast<AgentRuntime*>(context)->telemetryMain();
    return 0U;
}

void AgentRuntime::telemetryMain() noexcept
{
    const HANDLE events[2]{m_stopEvent, m_telemetryEvent};
    std::uint64_t sentGameStateRevision = 0U;
    std::uint32_t sentStateChangedRevision = 0U;
    std::uint32_t sentFeatureChangedRevision = 0U;
    std::uint32_t sentBindChangedRevision = 0U;
    std::uint32_t sentGuiScaleChangedRevision = 0U;
    std::uint32_t sentMediaSettingsChangedRevision = 0U;
    std::uint32_t sentMediaActionRevision = 0U;
    std::uint64_t sentHypixelQueryRevision = 0U;
    std::uint64_t sentPlayerRosterGeneration = 0U;
    std::uint32_t sentBlacklistActionRevision = 0U;
    while (m_stopEvent != nullptr && m_telemetryEvent != nullptr) {
        const DWORD wait = ::WaitForMultipleObjects(2U, events, FALSE, 250U);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) {
            return;
        }
        if (!m_handshakeSent.load(std::memory_order_acquire)) {
            continue;
        }

        // Readiness and hotkey state originate in SwapBuffers but pipe writes
        // never do. Failed writes retain their revision and are retried on the
        // next wake/timeout; multiple producer updates intentionally coalesce.
        if (m_rendererReadyQueued.load(std::memory_order_acquire) &&
            !m_rendererReadySent.load(std::memory_order_acquire) &&
            m_ipc->sendLine("RENDERER_READY OpenGL")) {
            m_rendererReadySent.store(true, std::memory_order_release);
        }

        const std::uint32_t stateRevision =
            m_stateChangedRevision.load(std::memory_order_acquire);
        if (stateRevision != sentStateChangedRevision) {
            const std::uint8_t state = m_stateChangedBits.load(std::memory_order_acquire);
            std::array<char, 18U> stateLine{};
            constexpr std::string_view prefix{"STATE_CHANGED "};
            std::memcpy(stateLine.data(), prefix.data(), prefix.size());
            stateLine[prefix.size()] = (state & 0x01U) != 0U ? '1' : '0';
            stateLine[prefix.size() + 1U] = ' ';
            stateLine[prefix.size() + 2U] = (state & 0x02U) != 0U ? '1' : '0';
            const std::string_view message(stateLine.data(), prefix.size() + 3U);
            if (m_ipc->sendLine(message)) {
                sentStateChangedRevision = stateRevision;
            }
        }

        const std::uint32_t featureRevision =
            m_featureChangedRevision.load(std::memory_order_acquire);
        if (featureRevision != sentFeatureChangedRevision) {
            const FeatureSettings settings = unpackFeatures(
                m_featureChangedBits.load(std::memory_order_acquire),
                m_featureChangedBedRadius.load(std::memory_order_acquire),
                m_featureChangedThreatRadius.load(std::memory_order_acquire),
                m_featureChangedBedHotkey.load(std::memory_order_acquire),
                m_featureChangedPanelOpacity.load(std::memory_order_acquire),
                m_featureChangedHypixelHotkey.load(std::memory_order_acquire),
                m_featureChangedHypixelOpacity.load(std::memory_order_acquire),
                m_featureChangedHypixelScale.load(std::memory_order_acquire),
                m_featureChangedHypixelX.load(std::memory_order_acquire),
                m_featureChangedHypixelY.load(std::memory_order_acquire),
                m_featureChangedClickGuiLightTheme.load(std::memory_order_acquire),
                m_featureChangedPlayerColor.load(std::memory_order_acquire),
                m_featureChangedBedColor.load(std::memory_order_acquire),
                m_featureChangedPanelColor.load(std::memory_order_acquire),
                m_featureChangedHypixelColor.load(std::memory_order_acquire),
                m_featureChangedHypixelHeight.load(std::memory_order_acquire),
                m_featureChangedNametagOpacity.load(std::memory_order_acquire),
                m_featureChangedNametagColor.load(std::memory_order_acquire),
                m_featureChangedAccentColor.load(std::memory_order_acquire),
                m_featureChangedHypixelFontIndex.load(std::memory_order_acquire),
                m_featureChangedNametagRange.load(std::memory_order_acquire),
                m_featureChangedNametagSizeIndex.load(std::memory_order_acquire),
                m_featureChangedHypixelRailColor.load(std::memory_order_acquire),
                m_featureChangedHypixelRailOpacity.load(std::memory_order_acquire),
                m_featureChangedSafewalkReleaseDelayMs.load(
                    std::memory_order_acquire),
                m_featureChangedSafewalkEdgeSensitivity.load(std::memory_order_acquire),
                m_featureChangedSafewalkMinimumPitch.load(std::memory_order_acquire),
                m_featureChangedSafewalkHotkey.load(std::memory_order_acquire),
                m_featureChangedFlySpeedPercent.load(std::memory_order_acquire),
                m_featureChangedAimSlowdownPercent.load(std::memory_order_acquire),
                m_featureChangedAimSpeedPercent.load(std::memory_order_acquire),
                m_featureChangedTextGuiColor.load(std::memory_order_acquire),
                m_featureChangedTextGuiX.load(std::memory_order_acquire),
                m_featureChangedTextGuiY.load(std::memory_order_acquire),
                m_featureChangedBhopAirSpeedPercent.load(std::memory_order_acquire),
                m_featureChangedHotkeysPackedA.load(std::memory_order_acquire),
                m_featureChangedHotkeysPackedB.load(std::memory_order_acquire),
                m_featureChangedFireballEspEnabled.load(std::memory_order_acquire),
                m_featureChangedFireballEspFilled.load(std::memory_order_acquire),
                m_featureChangedLongJumpEnabled.load(std::memory_order_acquire),
                m_featureChangedLongJumpSpeedPercent.load(std::memory_order_acquire),
                m_featureChangedFireballEspColor.load(std::memory_order_acquire),
                m_featureChangedAimMinimumDistance.load(std::memory_order_acquire),
                m_featureChangedAimMaximumDistance.load(std::memory_order_acquire),
                m_featureChangedAimFovDegrees.load(std::memory_order_acquire),
                m_featureChangedClickGuiWidthPercent.load(std::memory_order_acquire),
                m_featureChangedClickGuiHeightPercent.load(std::memory_order_acquire),
                m_featureChangedClickGuiOpacity.load(std::memory_order_acquire),
                m_featureChangedExtraBits.load(std::memory_order_acquire),
                m_featureChangedTextGuiAlignment.load(std::memory_order_acquire),
                m_featureChangedLocalMobReach.load(std::memory_order_acquire),
                m_featureChangedLocalAttackDelayMs.load(std::memory_order_acquire),
                m_featureChangedLocalVelocityPercent.load(std::memory_order_acquire),
                m_featureChangedHotkeysPackedC.load(std::memory_order_acquire));
            FixedLine<960U> line;
            if (line.append("FEATURE_STATE_CHANGED_V3 ") &&
                line.appendInteger(settings.espEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.entityEspEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedEspEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.labelsEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedThreatAlertsEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedDefensePanelEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.entityEspPlayersOnly ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedAutoRefreshEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedEspFilled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.debugChatEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.showOwnBedDefenseInfo ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.showTeammateBoxes ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedDefenseHoldToShow ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedDefensePerspectiveScale ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelHoldToShow ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.nametagEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.nametagSidePlacement ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.enemyItemIndicatorsEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.showTeammateNametags ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.nametagNearbyEnemiesOnly ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.nametagTeamPulse ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.showTeammateArrows ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.safewalkEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.scaffoldEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.flyEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bhopEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bhopAutoJump ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.aimAssistEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.aimLockOnMode ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.textGuiEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.allowHypixelMovement ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.bedDefenseRadius) && line.append(' ') &&
                line.appendInteger(settings.bedThreatRadius) && line.append(' ') &&
                line.appendInteger(settings.bedDefenseHotkey) && line.append(' ') &&
                line.appendInteger(settings.bedDefensePanelOpacity) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelHotkey) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelOpacity) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelScale) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelX) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelY) && line.append(' ') &&
                line.appendInteger(settings.clickGuiLightTheme ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.playerEspColor) && line.append(' ') &&
                line.appendInteger(settings.bedEspColor) && line.append(' ') &&
                line.appendInteger(settings.bedDefensePanelColor) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelColor) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelHeight) && line.append(' ') &&
                line.appendInteger(settings.nametagPanelOpacity) && line.append(' ') &&
                line.appendInteger(settings.nametagPanelColor) && line.append(' ') &&
                line.appendInteger(settings.clickGuiAccentColor) && line.append(' ') &&
                line.appendInteger(settings.hypixelPanelFontIndex) && line.append(' ') &&
                line.appendInteger(settings.nametagRange) && line.append(' ') &&
                line.appendInteger(settings.nametagSizeIndex) && line.append(' ') &&
                line.appendInteger(settings.hypixelRailColor) && line.append(' ') &&
                line.appendInteger(settings.hypixelRailOpacity) && line.append(' ') &&
                line.appendInteger(settings.safewalkReleaseDelayMs) && line.append(' ') &&
                line.appendInteger(settings.safewalkEdgeSensitivity) && line.append(' ') &&
                line.appendInteger(settings.safewalkMinimumPitch) && line.append(' ') &&
                line.appendInteger(settings.safewalkHotkey) && line.append(' ') &&
                line.appendInteger(settings.flySpeedPercent) && line.append(' ') &&
                line.appendInteger(settings.aimSlowdownPercent) && line.append(' ') &&
                line.appendInteger(settings.aimSpeedPercent) && line.append(' ') &&
                line.appendInteger(settings.textGuiColor) && line.append(' ') &&
                line.appendInteger(settings.textGuiX) && line.append(' ') &&
                line.appendInteger(settings.textGuiY) && line.append(' ') &&
                line.appendInteger(settings.bhopAirSpeedPercent) && line.append(' ') &&
                line.appendInteger(packFeatureHotkeys(settings, 0U)) && line.append(' ') &&
                line.appendInteger(packFeatureHotkeys(settings, 8U)) && line.append(' ') &&
                line.appendInteger(packFeatureHotkeysExtra(settings)) && line.append(' ') &&
                line.appendInteger(settings.fireballEspEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.fireballEspFilled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.longJumpEnabled ? 1 : 0) && line.append(' ') &&
                line.appendInteger(settings.longJumpSpeedPercent) && line.append(' ') &&
                line.appendInteger(settings.fireballEspColor) && line.append(' ') &&
                line.appendInteger(settings.aimMinimumDistance) && line.append(' ') &&
                line.appendInteger(settings.aimMaximumDistance) && line.append(' ') &&
                line.appendInteger(settings.aimFovDegrees) && line.append(' ') &&
                line.appendInteger(settings.clickGuiWidthPercent) && line.append(' ') &&
                line.appendInteger(settings.clickGuiHeightPercent) && line.append(' ') &&
                line.appendInteger(settings.clickGuiOpacity) && line.append(' ') &&
                line.appendInteger(packExtraFeatures(settings)) && line.append(' ') &&
                line.appendInteger(settings.textGuiAlignment) && line.append(' ') &&
                line.appendInteger(settings.localMobReach) && line.append(' ') &&
                line.appendInteger(settings.localAttackDelayMs) && line.append(' ') &&
                line.appendInteger(settings.localVelocityPercent) &&
                m_ipc->sendLine(line.view())) {
                FixedLine<48U> options;
                if(options.append("AIM_OPTIONS_CHANGED ") &&
                   options.appendInteger(m_featureChangedAimOptions.load(std::memory_order_acquire)) &&
                   m_ipc->sendLine(options.view())) {
                    FixedLine<48U> cps;
                    if(cps.append("AIM_ATTACK_CPS_CHANGED ")&&
                       cps.appendInteger(m_featureChangedAimAttackCps.load(
                           std::memory_order_acquire))&&m_ipc->sendLine(cps.view())) {
                        FixedLine<48U> smartHotbar;
                        if(smartHotbar.append("SMART_HOTBAR_CHANGED ")&&
                           smartHotbar.appendInteger(
                               m_featureChangedSmartHotbarConfig.load(
                                   std::memory_order_acquire))&&
                           m_ipc->sendLine(smartHotbar.view()))
                            sentFeatureChangedRevision=featureRevision;
                    }
                }
            }
        }

        const std::uint32_t bindRevision =
            m_bindChangedRevision.load(std::memory_order_acquire);
        if (bindRevision != sentBindChangedRevision) {
            FixedLine<48U> line;
            if (line.append("BIND_CHANGED ") &&
                line.appendInteger(m_bindChangedKey.load(std::memory_order_acquire)) &&
                m_ipc->sendLine(line.view())) {
                sentBindChangedRevision = bindRevision;
            }
        }

        const std::uint32_t guiScaleRevision =
            m_guiScaleChangedRevision.load(std::memory_order_acquire);
        if (guiScaleRevision != sentGuiScaleChangedRevision) {
            FixedLine<48U> line;
            if (line.append("GUI_SCALE_CHANGED ") &&
                line.appendInteger(m_guiScaleChangedIndex.load(std::memory_order_acquire)) &&
                m_ipc->sendLine(line.view())) {
                sentGuiScaleChangedRevision = guiScaleRevision;
            }
        }

        const std::uint32_t mediaSettingsRevision =
            m_mediaSettingsChangedRevision.load(std::memory_order_acquire);
        if (mediaSettingsRevision != sentMediaSettingsChangedRevision) {
            const std::uint32_t bits =
                m_mediaSettingsChangedBits.load(std::memory_order_acquire);
            FixedLine<192U> line;
            if (line.append("MEDIA_SETTINGS_CHANGED ") &&
                line.appendInteger((bits & 0x100U) != 0U ? 1 : 0) && line.append(' ') &&
                line.appendInteger(bits & 0xFFU) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedPrevious.load(
                    std::memory_order_acquire)) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedToggle.load(
                    std::memory_order_acquire)) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedNext.load(
                    std::memory_order_acquire)) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedColor.load(
                    std::memory_order_acquire)) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedX.load(
                    std::memory_order_acquire)) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedY.load(
                    std::memory_order_acquire)) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedSpectrum.load(
                    std::memory_order_acquire)) && line.append(' ') &&
                line.appendInteger(m_mediaSettingsChangedScale.load(
                    std::memory_order_acquire)) && m_ipc->sendLine(line.view())) {
                sentMediaSettingsChangedRevision = mediaSettingsRevision;
            }
        }

        const std::uint32_t mediaActionRevision =
            m_mediaActionRevision.load(std::memory_order_acquire);
        if (mediaActionRevision != sentMediaActionRevision) {
            const MediaAction action = static_cast<MediaAction>(
                m_mediaAction.load(std::memory_order_acquire));
            const char* token = action == MediaAction::Previous ? "PREVIOUS" :
                action == MediaAction::Toggle ? "TOGGLE" :
                action == MediaAction::Next ? "NEXT" : nullptr;
            if (token != nullptr && m_ipc->sendLine(
                    std::string("MEDIA_ACTION ") + token)) {
                sentMediaActionRevision = mediaActionRevision;
            }
        }

        const std::uint32_t blacklistActionRevision =
            m_blacklistActionRevision.load(std::memory_order_acquire);
        if (blacklistActionRevision != sentBlacklistActionRevision) {
            BlacklistAction action{};
            ::AcquireSRWLockShared(&m_blacklistActionLock);
            action = m_blacklistAction;
            ::ReleaseSRWLockShared(&m_blacklistActionLock);
            FixedLine<1024U> actionLine;
            bool formatted = false;
            switch (action.type) {
            case BlacklistAction::Type::Add:
                formatted = actionLine.append("BLACKLIST_ADD ") &&
                    appendPercentEncoded(actionLine, action.name.data()) && actionLine.append(' ') &&
                    appendPercentEncoded(actionLine, action.uuid.data()) && actionLine.append(' ') &&
                    appendPercentEncoded(actionLine, action.reason.data()) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.idOnlyNick ? 1 : 0) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.warnOnEncounter ? 1 : 0);
                break;
            case BlacklistAction::Type::Remove:
                formatted = actionLine.append("BLACKLIST_REMOVE ") &&
                    appendPercentEncoded(actionLine, action.key.data());
                break;
            case BlacklistAction::Type::Warning:
                formatted = actionLine.append("BLACKLIST_WARNING ") &&
                    appendPercentEncoded(actionLine, action.key.data()) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.warnOnEncounter ? 1 : 0);
                break;
            case BlacklistAction::Type::Layout:
                formatted = actionLine.append("BLACKLIST_LAYOUT ") &&
                    actionLine.appendInteger(action.x) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.y) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.width) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.height);
                break;
            case BlacklistAction::Type::Settings:
                formatted = actionLine.append("BLACKLIST_SETTINGS_CHANGED ") &&
                    actionLine.appendInteger(action.panelEnabled ? 1 : 0) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.matchAlertsEnabled ? 1 : 0) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.allowIdOnlyNicks ? 1 : 0) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.showWithClickGui ? 1 : 0) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.collapsed ? 1 : 0) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.panelOpacity) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.contentScale) && actionLine.append(' ') &&
                    actionLine.appendInteger(action.panelColor);
                break;
            case BlacklistAction::Type::None: break;
            }
            if (formatted && m_ipc->sendLine(actionLine.view()))
                sentBlacklistActionRevision = blacklistActionRevision;
        }

        GameStateMessage gameState{};
        std::uint64_t gameStateRevision = 0U;
        ::AcquireSRWLockShared(&m_telemetryLock);
        gameState = m_telemetryMailbox.gameState;
        gameStateRevision = m_telemetryMailbox.gameStateRevision;
        const auto hypixelQuery = m_telemetryMailbox.hypixelQuery;
        const std::uint64_t hypixelQueryRevision =
            m_telemetryMailbox.hypixelQueryRevision;
        const auto players = m_telemetryMailbox.players;
        const std::uint32_t playerCount = m_telemetryMailbox.playerCount;
        const std::uint64_t playerRosterGeneration =
            m_telemetryMailbox.playerRosterGeneration;
        const auto localPlayerName = m_telemetryMailbox.localPlayerName;
        const bool matchActive = m_telemetryMailbox.matchActive;
        ::ReleaseSRWLockShared(&m_telemetryLock);
        if (gameStateRevision != 0U && gameStateRevision != sentGameStateRevision) {
            FixedLine<768U> line;
            if (formatGameState(line, gameState) && m_ipc->sendLine(line.view())) {
                sentGameStateRevision = gameStateRevision;
            }
        }
        if (hypixelQueryRevision != 0U &&
            hypixelQueryRevision != sentHypixelQueryRevision) {
            FixedLine<80U> line;
            if (line.append("HYPIXEL_QUERY ") &&
                appendPercentEncoded(line, hypixelQuery.data()) &&
                m_ipc->sendLine(line.view())) {
                sentHypixelQueryRevision = hypixelQueryRevision;
            }
        }
        if (playerRosterGeneration != 0U &&
            playerRosterGeneration != sentPlayerRosterGeneration) {
            FixedLine<48U> matchLine;
            bool allSent = matchLine.append("MATCH_STATE ") &&
                matchLine.appendInteger(matchActive ? 1 : 0) &&
                m_ipc->sendLine(matchLine.view());
            if (allSent && localPlayerName[0U] != '\0') {
                FixedLine<64U> playerLine;
                allSent = playerLine.append("PLAYER_STATUS ") &&
                    appendPercentEncoded(playerLine, localPlayerName.data()) &&
                    m_ipc->sendLine(playerLine.view());
            }
            for (std::uint32_t index = 0U; allSent && matchActive &&
                 index < playerCount; ++index) {
                const char teamColor = players[index].teamColor;
                if (!((teamColor >= '0' && teamColor <= '9') ||
                      (teamColor >= 'a' && teamColor <= 'f'))) {
                    continue;
                }
                const char teamPrefix[4]{
                    static_cast<char>(0xC2), static_cast<char>(0xA7),
                    teamColor, '\0'};
                FixedLine<160U> line;
                if (!line.append("PLAYER_FOUND ") ||
                    !appendPercentEncoded(line, players[index].name.data()) ||
                    !line.append(' ') || !appendPercentEncoded(line, teamPrefix) ||
                    !line.append(' ') ||
                    !appendPercentEncoded(line, players[index].uuid.data()) ||
                    !m_ipc->sendLine(line.view())) {
                    allSent = false;
                    break;
                }
            }
            if (allSent && matchActive && playerCount > 0U) {
                m_bindings->enqueueDebugChatLine(
                    "stats_query=dispatched players=" + std::to_string(playerCount));
            }
            if (!matchActive) {
                ::AcquireSRWLockExclusive(&m_playerStatsLock);
                m_playerStats.clear();
                ::ReleaseSRWLockExclusive(&m_playerStatsLock);
            }
            if (allSent) sentPlayerRosterGeneration = playerRosterGeneration;
        }
    }
}

void AgentRuntime::joinTelemetry() noexcept
{
    if (m_telemetry != nullptr && ::GetCurrentThreadId() != ::GetThreadId(m_telemetry)) {
        ::WaitForSingleObject(m_telemetry, INFINITE);
    }
}

void AgentRuntime::queueTelemetry(const GameSnapshot& snapshot,
                                  const std::uint64_t tickMilliseconds) noexcept
{
    constexpr std::uint64_t kTelemetryIntervalMs = 100U;
    if (m_lastTelemetryTick != 0U &&
        tickMilliseconds - m_lastTelemetryTick < kTelemetryIntervalMs) {
        return;
    }
    GameStateMessage message{};
    message.valid = snapshot.state == GameSnapshot::State::Ready;
    message.state = static_cast<std::uint8_t>(snapshot.state);
    message.unixMilliseconds = unixMillisecondsNow();
    const auto finiteOrZero = [](const double value) noexcept {
        return std::isfinite(value) ? value : 0.0;
    };
    if (message.valid) {
        message.health = finiteOrZero(snapshot.health);
        message.maxHealth = finiteOrZero(snapshot.maxHealth);
        message.entityId = snapshot.entityId;
        message.x = finiteOrZero(snapshot.x);
        message.y = finiteOrZero(snapshot.y);
        message.z = finiteOrZero(snapshot.z);
        message.loadedEntities = snapshot.loadedEntities;
        message.bedCount = snapshot.bedCount;
    }
    const char* const mapping = snapshot.mapping != nullptr ? snapshot.mapping : "";
    std::size_t mappingLength = 0U;
    while (mapping[mappingLength] != '\0' &&
           mappingLength + 1U < message.mapping.size()) {
        message.mapping[mappingLength] = mapping[mappingLength];
        ++mappingLength;
    }
    message.mapping[mappingLength] = '\0';

    if (::TryAcquireSRWLockExclusive(&m_telemetryLock) == FALSE) {
        return;
    }
    message.sequence = ++m_telemetrySequence;
    m_telemetryMailbox.gameState = message;
    ++m_telemetryMailbox.gameStateRevision;
    if (snapshot.playerRosterGeneration != 0U &&
        snapshot.playerRosterGeneration != m_telemetryMailbox.playerRosterGeneration) {
        m_telemetryMailbox.players = snapshot.players;
        m_telemetryMailbox.playerCount = snapshot.playerCount;
        m_telemetryMailbox.localPlayerName = snapshot.localPlayerName;
        m_telemetryMailbox.matchActive = snapshot.matchActive;
        m_telemetryMailbox.playerRosterGeneration = snapshot.playerRosterGeneration;
    }
    ::ReleaseSRWLockExclusive(&m_telemetryLock);
    m_lastTelemetryTick = tickMilliseconds;
    if (m_telemetryEvent != nullptr) {
        ::SetEvent(m_telemetryEvent);
    }
}

void AgentRuntime::queueStateChanged(const bool visible, const bool interactive) noexcept
{
    const std::uint8_t state = static_cast<std::uint8_t>(
        (visible ? 0x01U : 0U) | (interactive ? 0x02U : 0U));
    m_stateChangedBits.store(state, std::memory_order_relaxed);
    m_stateChangedRevision.fetch_add(1U, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

void AgentRuntime::queueHypixelQuery(const std::array<char, 17U>& playerId) noexcept
{
    // Explicit button clicks must not be dropped if telemetry is copying its
    // tiny mailbox at the same instant. Regular game samples remain try-lock
    // based; this one-shot path may wait only for that bounded copy.
    ::AcquireSRWLockExclusive(&m_telemetryLock);
    m_telemetryMailbox.hypixelQuery = playerId;
    ++m_telemetryMailbox.hypixelQueryRevision;
    ::ReleaseSRWLockExclusive(&m_telemetryLock);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

void AgentRuntime::queueMenuHotkeyChanged(const unsigned virtualKey) noexcept
{
    m_menuHotkey.store(virtualKey, std::memory_order_release);
    m_bindChangedKey.store(virtualKey, std::memory_order_relaxed);
    m_bindChangedRevision.fetch_add(1U, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

void AgentRuntime::queueGuiScaleChanged(const int index) noexcept
{
    const int bounded = std::clamp(index, 0, 3);
    m_guiScaleIndex.store(bounded, std::memory_order_release);
    m_guiScaleChangedIndex.store(bounded, std::memory_order_relaxed);
    m_guiScaleChangedRevision.fetch_add(1U, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

void AgentRuntime::queueMediaSettingsChanged(
    const MediaOverlaySettings& settings) noexcept
{
    const std::uint32_t bits = static_cast<std::uint32_t>(
        (settings.enabled ? 0x100U : 0U) |
        static_cast<unsigned>(std::clamp(settings.opacity, 20, 100)));
    m_mediaSettingsChangedBits.store(bits, std::memory_order_relaxed);
    m_mediaSettingsChangedPrevious.store(
        std::clamp(settings.previousHotkey, 0, 254), std::memory_order_relaxed);
    m_mediaSettingsChangedToggle.store(
        std::clamp(settings.toggleHotkey, 0, 254), std::memory_order_relaxed);
    m_mediaSettingsChangedNext.store(
        std::clamp(settings.nextHotkey, 0, 254), std::memory_order_relaxed);
    m_mediaSettingsChangedColor.store(
        settings.panelColor & 0xFFFFFFU, std::memory_order_relaxed);
    m_mediaSettingsChangedX.store(
        std::clamp(settings.panelX, -1, 1000), std::memory_order_relaxed);
    m_mediaSettingsChangedY.store(
        std::clamp(settings.panelY, -1, 1000), std::memory_order_relaxed);
    m_mediaSettingsChangedSpectrum.store(
        std::clamp(settings.spectrumOpacity, 0, 100), std::memory_order_relaxed);
    m_mediaSettingsChangedScale.store(
        std::clamp(settings.scalePercent, 35, 100), std::memory_order_relaxed);
    m_mediaSettingsChangedRevision.fetch_add(1U, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

void AgentRuntime::queueMediaAction(const MediaAction action) noexcept
{
    if (action == MediaAction::None) return;
    m_mediaAction.store(static_cast<std::uint8_t>(action), std::memory_order_relaxed);
    m_mediaActionRevision.fetch_add(1U, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

void AgentRuntime::queueBlacklistAction(const BlacklistAction& action) noexcept
{
    ::AcquireSRWLockExclusive(&m_blacklistActionLock);
    m_blacklistAction = action;
    ::ReleaseSRWLockExclusive(&m_blacklistActionLock);
    m_blacklistActionRevision.fetch_add(1U, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

void AgentRuntime::queueRendererReady() noexcept
{
    m_rendererReadyQueued.store(true, std::memory_order_release);
    if (m_telemetryEvent != nullptr) ::SetEvent(m_telemetryEvent);
}

} // namespace mcoverlay
