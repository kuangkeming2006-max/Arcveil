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
bool parseFlag(std::string_view token, bool& result) noexcept
{
    unsigned parsed = 0U;
    const auto conversion = std::from_chars(token.data(), token.data() + token.size(), parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != token.data() + token.size() ||
        parsed > 1U) {
        return false;
    }
    result = parsed != 0U;
    return true;
}

template<std::size_t Capacity>
bool percentDecode(const std::string_view source,
                   std::array<char, Capacity>& destination) noexcept
{
    destination.fill('\0');
    if (source == "-") return true;
    const auto hexValue = [](const char character) noexcept -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        return -1;
    };
    std::size_t output = 0U;
    for (std::size_t index = 0U; index < source.size();) {
        if (output + 1U >= Capacity) return false;
        unsigned char byte = static_cast<unsigned char>(source[index++]);
        if (byte == '%') {
            if (index + 1U >= source.size()) return false;
            const int high = hexValue(source[index++]);
            const int low = hexValue(source[index++]);
            if (high < 0 || low < 0) return false;
            byte = static_cast<unsigned char>((high << 4) | low);
        }
        if (byte == 0U || byte == '\r' || byte == '\n') return false;
        destination[output++] = static_cast<char>(byte);
    }
    destination[output] = '\0';
    return true;
}

}

bool AgentRuntime::sendHello() noexcept
{
    std::string hello = "HELLO " + std::to_string(m_options.protocol) + " " +
                        std::to_string(::GetCurrentProcessId()) + " " + m_options.token;
    return m_ipc->sendLine(hello);
}

bool AgentRuntime::sendHandshake() noexcept
{
    if (!sendHello()) return false;
    if (!m_ipc->sendLine("HOOK_READY OpenGL")) {
        return false;
    }
    m_handshakeSent.store(true, std::memory_order_release);
    if (m_telemetryEvent != nullptr) {
        ::SetEvent(m_telemetryEvent);
    }
    return true;
}

bool AgentRuntime::handleControlLine(const std::string_view line) noexcept
{
    std::istringstream stream{std::string(line)};
    std::string command;
    stream >> command;
    if (command == "STATE") {
        std::string visibleToken;
        std::string interactiveToken;
        std::string trailing;
        bool visible = false;
        bool interactive = false;
        if (!(stream >> visibleToken >> interactiveToken) || (stream >> trailing) ||
            !parseFlag(visibleToken, visible) || !parseFlag(interactiveToken, interactive)) {
            (void)m_ipc->sendLine("ERROR BAD_STATE expected-STATE-visible-interactive");
            return true;
        }
        if (interactive) visible = true;
        m_visible.store(visible, std::memory_order_release);
        m_interactive.store(interactive, std::memory_order_release);
        // This is a protocol acknowledgement, not a user-facing progress
        // message. Keeping it out of STATUS prevents the controller's friendly
        // "waiting for renderer" text from being overwritten by state-applied.
        (void)m_ipc->sendLine(std::string("STATE_APPLIED ") +
                              (visible ? "1 " : "0 ") +
                              (interactive ? "1" : "0"));
        return true;
    }
    if (command == "AIM_OPTIONS") {
        unsigned value=0; std::string trailing;
        if(!(stream>>value) || value>0x7FFFFFFFU || (stream>>trailing)) {
            (void)m_ipc->sendLine("ERROR BAD_AIM_OPTIONS expected-v32-packed-options");
            return true;
        }
        m_aimOptions.store(value,std::memory_order_release);
        return true;
    }
    if(command=="AIM_ATTACK_CPS") {
        int value=0; std::string trailing;
        if(!(stream>>value)||value<1||value>20||(stream>>trailing)) {
            (void)m_ipc->sendLine("ERROR BAD_AIM_ATTACK_CPS expected-1-to-20");
            return true;
        }
        m_aimAttackCps.store(value,std::memory_order_release);
        return true;
    }
    if(command=="SMART_HOTBAR") {
        std::uint32_t packed=0U; std::string trailing;
        if(!(stream>>packed)||(stream>>trailing)||!hotbar::validPacked(packed)) {
            (void)m_ipc->sendLine(
                "ERROR BAD_SMART_HOTBAR expected-packed-v1-config");
            return true;
        }
        m_smartHotbarConfig.store(packed,std::memory_order_release);
        return true;
    }
    if (command == "FEATURE_STATE" || command == "FEATURE_STATE_V2" ||
        command == "FEATURE_STATE_V3") {
        std::array<std::string, 32U> tokens{};
        std::string trailing;
        FeatureSettings settings{};
        std::array<bool, 32U> values{};
        int defenseRadius = 0;
        int threatRadius = 0;
        int bedHotkey = 0;
        int panelOpacity = 0;
        int hypixelHotkey = 0;
        int hypixelOpacity = 0;
        int hypixelScale = 0;
        int hypixelX = 0;
        int hypixelY = 0;
        int clickGuiTheme = 0;
        std::uint32_t playerColor = 0U;
        std::uint32_t bedColor = 0U;
        std::uint32_t panelColor = 0U;
        std::uint32_t hypixelColor = 0U;
        int hypixelHeight = 0;
        int nametagOpacity = 0;
        std::uint32_t nametagColor = 0U;
        std::uint32_t accentColor = 0U;
        int hypixelFontIndex = 0;
        int nametagRange = 0;
        int nametagSizeIndex = 0;
        std::uint32_t hypixelRailColor = 0U;
        int hypixelRailOpacity = 0;
        int safewalkReleaseDelayMs = 0;
        int safewalkEdgeSensitivity = 0;
        int safewalkMinimumPitch = 0;
        int safewalkHotkey = 0;
        int flySpeedPercent = 0;
        int aimSlowdownPercent = 0;
        int aimSpeedPercent = 0;
        std::uint32_t textGuiColor = 0U;
        int textGuiX = 0;
        int textGuiY = 0;
        int bhopAirSpeedPercent = 0;
        std::uint64_t hotkeysPackedA = 0U;
        std::uint64_t hotkeysPackedB = 0U;
        std::uint32_t hotkeysPackedC = 0xA400U;
        std::string fireballEnabledToken;
        std::string fireballFilledToken;
        std::string longJumpEnabledToken;
        int longJumpSpeedPercent = 0;
        std::uint32_t fireballEspColor = 0U;
        int aimMinimumDistance = 0;
        int aimMaximumDistance = 0;
        int aimFovDegrees = 0;
        int clickGuiWidthPercent = 0;
        int clickGuiHeightPercent = 0;
        int clickGuiOpacity = 0;
        std::uint32_t extraBits = 0U;
        int textGuiAlignment = 0;
        int localMobReach = 0;
        int localAttackDelayMs = 0;
        int localVelocityPercent = 0;
        bool featureTokensRead = true;
        for (std::string& token : tokens) {
            if (!(stream >> token)) {
                featureTokensRead = false;
                break;
            }
        }
        if (!featureTokensRead || !(stream
               >> defenseRadius >> threatRadius >> bedHotkey >> panelOpacity
               >> hypixelHotkey >> hypixelOpacity
               >> hypixelScale >> hypixelX >> hypixelY >> clickGuiTheme
               >> playerColor >> bedColor >> panelColor >> hypixelColor
               >> hypixelHeight >> nametagOpacity >> nametagColor >> accentColor
               >> hypixelFontIndex >> nametagRange >> nametagSizeIndex
               >> hypixelRailColor >> hypixelRailOpacity
               >> safewalkReleaseDelayMs >> safewalkEdgeSensitivity
               >> safewalkMinimumPitch >> safewalkHotkey >> flySpeedPercent
               >> aimSlowdownPercent >> aimSpeedPercent >> textGuiColor
               >> textGuiX >> textGuiY >> bhopAirSpeedPercent
               >> hotkeysPackedA >> hotkeysPackedB) ||
            (command=="FEATURE_STATE_V3"&&!(stream>>hotkeysPackedC)) ||
            !(stream >> fireballEnabledToken >> fireballFilledToken
               >> longJumpEnabledToken >> longJumpSpeedPercent
               >> fireballEspColor >> aimMinimumDistance
               >> aimMaximumDistance >> aimFovDegrees
               >> clickGuiWidthPercent >> clickGuiHeightPercent
               >> clickGuiOpacity >> extraBits >> textGuiAlignment
               >> localMobReach >> localAttackDelayMs
               >> localVelocityPercent) ||
            (stream >> trailing)) {
            (void)m_ipc->sendLine("ERROR BAD_FEATURE_STATE expected-thirty-two-flags-and-layout");
            return true;
        }

        for (std::size_t index = 0U; index < tokens.size(); ++index) {
            if (!parseFlag(tokens[index], values[index])) {
                (void)m_ipc->sendLine("ERROR BAD_FEATURE_STATE expected-thirty-two-flags-and-layout");
                return true;
            }
        }
        if (defenseRadius < 3 || defenseRadius > 10 ||
            threatRadius < 3 || threatRadius > 32 ||
            (bedHotkey != 0 && bedHotkey < 8) || bedHotkey > 254 ||
            panelOpacity < 0 || panelOpacity > 100 ||
            (hypixelHotkey != 0 && hypixelHotkey < 8) || hypixelHotkey > 254 ||
            hypixelOpacity < 0 || hypixelOpacity > 100 ||
            hypixelScale < 70 || hypixelScale > 160 ||
            hypixelHeight < 60 || hypixelHeight > 400 ||
            hypixelX < -1 || hypixelX > 1000 ||
            hypixelY < -1 || hypixelY > 1000 ||
            clickGuiTheme < 0 || clickGuiTheme > 1 ||
            playerColor > 0xFFFFFFU || bedColor > 0xFFFFFFU ||
            panelColor > 0xFFFFFFU || hypixelColor > 0xFFFFFFU ||
            nametagOpacity < 10 || nametagOpacity > 100 ||
            nametagColor > 0xFFFFFFU || accentColor > 0xFFFFFFU ||
            hypixelRailColor > 0xFFFFFFU ||
            hypixelRailOpacity < 0 || hypixelRailOpacity > 100 ||
            safewalkReleaseDelayMs < 0 || safewalkReleaseDelayMs > 750 ||
            safewalkEdgeSensitivity < 0 || safewalkEdgeSensitivity > 100 ||
            safewalkMinimumPitch < -90 || safewalkMinimumPitch > 90 ||
            (safewalkHotkey != 0 && safewalkHotkey < 8) || safewalkHotkey > 254 ||
            flySpeedPercent < 10 || flySpeedPercent > 500 ||
            aimSlowdownPercent < 5 || aimSlowdownPercent > 95 ||
            aimSpeedPercent < 1 || aimSpeedPercent > 100 ||
            aimMinimumDistance < 0 || aimMinimumDistance > 64 ||
            aimMaximumDistance < std::max(1, aimMinimumDistance) ||
            aimMaximumDistance > 128 ||
            aimFovDegrees < 1 || aimFovDegrees > 360 ||
            clickGuiWidthPercent < 80 || clickGuiWidthPercent > 150 ||
            clickGuiHeightPercent < 80 || clickGuiHeightPercent > 150 ||
            clickGuiOpacity < 35 || clickGuiOpacity > 100 ||
            textGuiColor > 0xFFFFFFU || textGuiX < -1 || textGuiX > 1000 ||
            textGuiY < -1 || textGuiY > 1000 ||
            bhopAirSpeedPercent < 10 || bhopAirSpeedPercent > 300 ||
            hypixelFontIndex < 0 || hypixelFontIndex > 3 ||
            nametagRange < 4 || nametagRange > 128 ||
            nametagSizeIndex < 0 || nametagSizeIndex > 3 ||
            textGuiAlignment < 0 || textGuiAlignment > 2 ||
            localMobReach < 3 || localMobReach > 10 ||
            localAttackDelayMs < 100 || localAttackDelayMs > 1500 ||
            localVelocityPercent < 0 || localVelocityPercent > 100) {
            (void)m_ipc->sendLine("ERROR BAD_FEATURE_STATE invalid-radius-bind-opacity-or-color");
            return true;
        }
        bool fireballEnabled = false;
        bool fireballFilled = false;
        bool longJumpEnabled = false;
        if (!parseFlag(fireballEnabledToken, fireballEnabled) ||
            !parseFlag(fireballFilledToken, fireballFilled) ||
            !parseFlag(longJumpEnabledToken, longJumpEnabled) ||
            longJumpSpeedPercent < 25 || longJumpSpeedPercent > 250 ||
            fireballEspColor > 0xFFFFFFU) {
            (void)m_ipc->sendLine("ERROR BAD_FEATURE_STATE invalid-local-feature");
            return true;
        }
        settings.espEnabled = values[0];
        settings.entityEspEnabled = values[1];
        settings.bedEspEnabled = values[2];
        settings.labelsEnabled = values[3];
        settings.hypixelPanelEnabled = values[4];
        settings.bedThreatAlertsEnabled = values[5];
        settings.bedDefensePanelEnabled = values[6];
        settings.entityEspPlayersOnly = values[7];
        settings.bedAutoRefreshEnabled = values[8];
        settings.bedEspFilled = values[9];
        settings.debugChatEnabled = values[10];
        settings.showOwnBedDefenseInfo = values[11];
        settings.showTeammateBoxes = values[12];
        settings.bedDefenseHoldToShow = values[13];
        settings.bedDefensePerspectiveScale = values[14];
        settings.hypixelPanelHoldToShow = values[15];
        settings.nametagEnabled = values[16];
        settings.nametagSidePlacement = values[17];
        settings.enemyItemIndicatorsEnabled = values[18];
        settings.showTeammateNametags = values[19];
        settings.nametagNearbyEnemiesOnly = values[20];
        settings.nametagTeamPulse = values[21];
        settings.showTeammateArrows = values[22];
        settings.safewalkEnabled = values[23];
        settings.scaffoldEnabled = values[24];
        settings.flyEnabled = values[25];
        settings.bhopEnabled = values[26];
        settings.bhopAutoJump = values[27];
        settings.aimAssistEnabled = values[28];
        // Legacy slot 29 meant sensitivity slowdown, never hard-lock. Only a
        // versioned packet can request the new mode; old clients fail softly.
        settings.aimLockOnMode = command != "FEATURE_STATE" && values[29];
        settings.textGuiEnabled = values[30];
        settings.allowHypixelMovement = values[31];
        settings.bedDefenseRadius = defenseRadius;
        settings.bedThreatRadius = threatRadius;
        settings.bedDefenseHotkey = bedHotkey;
        settings.bedDefensePanelOpacity = panelOpacity;
        settings.hypixelPanelHotkey = hypixelHotkey;
        settings.hypixelPanelOpacity = hypixelOpacity;
        settings.hypixelPanelScale = hypixelScale;
        settings.hypixelPanelHeight = hypixelHeight;
        settings.hypixelPanelX = hypixelX;
        settings.hypixelPanelY = hypixelY;
        settings.clickGuiLightTheme = clickGuiTheme != 0;
        settings.playerEspColor = playerColor;
        settings.bedEspColor = bedColor;
        settings.bedDefensePanelColor = panelColor;
        settings.hypixelPanelColor = hypixelColor;
        settings.nametagPanelOpacity = nametagOpacity;
        settings.nametagPanelColor = nametagColor;
        settings.clickGuiAccentColor = accentColor;
        settings.hypixelPanelFontIndex = hypixelFontIndex;
        settings.nametagRange = nametagRange;
        settings.nametagSizeIndex = nametagSizeIndex;
        settings.hypixelRailColor = hypixelRailColor;
        settings.hypixelRailOpacity = hypixelRailOpacity;
        settings.safewalkReleaseDelayMs = safewalkReleaseDelayMs;
        settings.safewalkEdgeSensitivity = safewalkEdgeSensitivity;
        settings.safewalkMinimumPitch = safewalkMinimumPitch;
        settings.safewalkHotkey = safewalkHotkey;
        settings.flySpeedPercent = flySpeedPercent;
        settings.aimSlowdownPercent = aimSlowdownPercent;
        settings.aimSpeedPercent = aimSpeedPercent;
        settings.textGuiColor = textGuiColor;
        settings.textGuiX = textGuiX;
        settings.textGuiY = textGuiY;
        settings.bhopAirSpeedPercent = bhopAirSpeedPercent;
        for (std::size_t index = 0U; index < 16U; ++index) {
            const std::uint64_t packed = index < 8U ? hotkeysPackedA : hotkeysPackedB;
            const std::size_t offset = index < 8U ? index : index - 8U;
            const int key = static_cast<int>((packed >> (offset * 8U)) & 0xFFU);
            if ((key > 0 && key < 8) || key > 254) {
                (void)m_ipc->sendLine("ERROR BAD_FEATURE_STATE invalid-feature-hotkey");
                return true;
            }
            settings.featureHotkeys[index] = key;
        }
        const int extraHotkey16=static_cast<int>(hotkeysPackedC&0xFFU);
        const int extraHotkey17=static_cast<int>((hotkeysPackedC>>8U)&0xFFU);
        if((extraHotkey16>0&&extraHotkey16<8)||extraHotkey16>254||
           (extraHotkey17>0&&extraHotkey17<8)||extraHotkey17>254||
           hotkeysPackedC>0x7FFFFU) {
            (void)m_ipc->sendLine("ERROR BAD_FEATURE_STATE invalid-extra-feature-hotkey");
            return true;
        }
        settings.featureHotkeys[16U]=extraHotkey16;
        settings.featureHotkeys[17U]=extraHotkey17;
        settings.freeLookEnabled=(hotkeysPackedC&0x10000U)!=0U;
        settings.attackShieldEnabled=(hotkeysPackedC&0x20000U)!=0U;
        settings.attackShieldWildcard=(hotkeysPackedC&0x40000U)!=0U;
        if (settings.featureHotkeys[4U] == 0)
            settings.featureHotkeys[4U] = safewalkHotkey;
        settings.safewalkHotkey = settings.featureHotkeys[4U];
        settings.fireballEspEnabled = fireballEnabled;
        settings.fireballEspFilled = fireballFilled;
        settings.longJumpEnabled = longJumpEnabled;
        settings.longJumpSpeedPercent = longJumpSpeedPercent;
        settings.fireballEspColor = fireballEspColor;
        settings.aimMinimumDistance = aimMinimumDistance;
        settings.aimMaximumDistance = aimMaximumDistance;
        settings.aimFovDegrees = aimFovDegrees;
        settings.clickGuiWidthPercent = clickGuiWidthPercent;
        settings.clickGuiHeightPercent = clickGuiHeightPercent;
        settings.clickGuiOpacity = clickGuiOpacity;
        settings.aimNearestPriority = (extraBits & 0x01U) != 0U;
        settings.textGuiVerticalLine = (extraBits & 0x02U) != 0U;
        settings.knockbackPredictionEnabled = (extraBits & 0x04U) != 0U;
        settings.bowPredictionEnabled = (extraBits & 0x08U) != 0U;
        settings.localMobAuraEnabled = (extraBits & 0x10U) != 0U;
        settings.localVelocityEnabled = (extraBits & 0x20U) != 0U;
        settings.scaffoldSameLayerOnly = (extraBits & 0x40U) != 0U;
        settings.fullscreenImeFixEnabled = (extraBits & 0x80U) != 0U;
        const auto ui = unpackUiPreferences(extraBits);
        settings.clickGuiBlur = ui.blur;
        settings.imePanelX = ui.imeX;
        settings.imePanelY = ui.imeY;
        settings.textGuiAlignment = textGuiAlignment;
        settings.localMobReach = localMobReach;
        settings.localAttackDelayMs = localAttackDelayMs;
        settings.localVelocityPercent = localVelocityPercent;
        m_featureBits.store(packFeatures(settings), std::memory_order_release);
        m_bedDefenseRadius.store(defenseRadius, std::memory_order_release);
        m_bedThreatRadius.store(threatRadius, std::memory_order_release);
        m_bedDefenseHotkey.store(bedHotkey, std::memory_order_release);
        m_bedDefensePanelOpacity.store(panelOpacity, std::memory_order_release);
        m_hypixelPanelHotkey.store(hypixelHotkey, std::memory_order_release);
        m_hypixelPanelOpacity.store(hypixelOpacity, std::memory_order_release);
        m_hypixelPanelScale.store(hypixelScale, std::memory_order_release);
        m_hypixelPanelHeight.store(hypixelHeight, std::memory_order_release);
        m_hypixelPanelX.store(hypixelX, std::memory_order_release);
        m_hypixelPanelY.store(hypixelY, std::memory_order_release);
        m_clickGuiLightTheme.store(clickGuiTheme != 0, std::memory_order_release);
        m_playerEspColor.store(playerColor, std::memory_order_release);
        m_bedEspColor.store(bedColor, std::memory_order_release);
        m_bedDefensePanelColor.store(panelColor, std::memory_order_release);
        m_hypixelPanelColor.store(hypixelColor, std::memory_order_release);
        m_nametagPanelOpacity.store(nametagOpacity, std::memory_order_release);
        m_nametagPanelColor.store(nametagColor, std::memory_order_release);
        m_clickGuiAccentColor.store(accentColor, std::memory_order_release);
        m_hypixelRailColor.store(hypixelRailColor, std::memory_order_release);
        m_hypixelRailOpacity.store(hypixelRailOpacity, std::memory_order_release);
        m_hypixelPanelFontIndex.store(hypixelFontIndex, std::memory_order_release);
        m_nametagRange.store(nametagRange, std::memory_order_release);
        m_nametagSizeIndex.store(nametagSizeIndex, std::memory_order_release);
        m_safewalkReleaseDelayMs.store(safewalkReleaseDelayMs,
                                       std::memory_order_release);
        m_safewalkEdgeSensitivity.store(safewalkEdgeSensitivity,
                                         std::memory_order_release);
        m_safewalkMinimumPitch.store(safewalkMinimumPitch,
                                     std::memory_order_release);
        m_safewalkHotkey.store(safewalkHotkey, std::memory_order_release);
        m_flySpeedPercent.store(flySpeedPercent, std::memory_order_release);
        m_aimSlowdownPercent.store(aimSlowdownPercent,
                                   std::memory_order_release);
        m_aimSpeedPercent.store(aimSpeedPercent, std::memory_order_release);
        m_textGuiColor.store(textGuiColor, std::memory_order_release);
        m_textGuiX.store(textGuiX, std::memory_order_release);
        m_textGuiY.store(textGuiY, std::memory_order_release);
        m_bhopAirSpeedPercent.store(bhopAirSpeedPercent,
                                    std::memory_order_release);
        m_featureHotkeysPackedA.store(packFeatureHotkeys(settings, 0U),
                                      std::memory_order_release);
        m_featureHotkeysPackedB.store(packFeatureHotkeys(settings, 8U),
                                      std::memory_order_release);
        m_featureHotkeysPackedC.store(packFeatureHotkeysExtra(settings),
                                      std::memory_order_release);
        m_fireballEspEnabled.store(fireballEnabled, std::memory_order_release);
        m_fireballEspFilled.store(fireballFilled, std::memory_order_release);
        m_longJumpEnabled.store(longJumpEnabled, std::memory_order_release);
        m_longJumpSpeedPercent.store(longJumpSpeedPercent,
                                     std::memory_order_release);
        m_fireballEspColor.store(fireballEspColor, std::memory_order_release);
        m_aimMinimumDistance.store(aimMinimumDistance, std::memory_order_release);
        m_aimMaximumDistance.store(aimMaximumDistance, std::memory_order_release);
        m_aimFovDegrees.store(aimFovDegrees, std::memory_order_release);
        m_clickGuiWidthPercent.store(clickGuiWidthPercent, std::memory_order_release);
        m_clickGuiHeightPercent.store(clickGuiHeightPercent, std::memory_order_release);
        m_clickGuiOpacity.store(clickGuiOpacity, std::memory_order_release);
        m_featureExtraBits.store(extraBits, std::memory_order_release);
        m_textGuiAlignment.store(textGuiAlignment, std::memory_order_release);
        m_localMobReach.store(localMobReach, std::memory_order_release);
        m_localAttackDelayMs.store(localAttackDelayMs, std::memory_order_release);
        m_localVelocityPercent.store(localVelocityPercent, std::memory_order_release);
        (void)m_ipc->sendLine("FEATURE_STATE_APPLIED");
        return true;
    }
    if (command == "BIND") {
        unsigned virtualKey = 0U;
        std::string trailing;
        if (!(stream >> virtualKey) || (stream >> trailing) ||
            (virtualKey != 0U && virtualKey < 8U) || virtualKey > 254U) {
            (void)m_ipc->sendLine("ERROR BAD_BIND expected-virtual-key-8-254");
            return true;
        }
        m_menuHotkey.store(virtualKey, std::memory_order_release);
        (void)m_ipc->sendLine(std::string("BIND_APPLIED ") +
                              std::to_string(virtualKey));
        return true;
    }
    if (command == "GUI_SCALE") {
        int index = -1;
        std::string trailing;
        if (!(stream >> index) || (stream >> trailing) || index < 0 || index > 3) {
            (void)m_ipc->sendLine("ERROR BAD_GUI_SCALE expected-index-0-3");
            return true;
        }
        m_guiScaleIndex.store(index, std::memory_order_release);
        (void)m_ipc->sendLine(std::string("GUI_SCALE_APPLIED ") + std::to_string(index));
        return true;
    }
    if (command == "MEDIA_STATE") {
        int available = 0;
        int playing = 0;
        std::string titleToken;
        std::string artistToken;
        std::string sourceToken;
        std::string coverToken;
        std::string trailing;
        MediaPlaybackSnapshot snapshot{};
        if (!(stream >> available >> playing >> titleToken >> artistToken >>
              sourceToken >> coverToken >> snapshot.positionMs >> snapshot.durationMs) ||
            (stream >> trailing) || available < 0 || available > 1 ||
            playing < 0 || playing > 1 || snapshot.positionMs < 0 ||
            snapshot.durationMs < 0 || !percentDecode(titleToken, snapshot.title) ||
            !percentDecode(artistToken, snapshot.artist) ||
            !percentDecode(sourceToken, snapshot.source) ||
            !percentDecode(coverToken, snapshot.coverPath)) {
            // Media integration is optional and must not affect the overlay
            // session if a shell provider emits an unexpected payload.
            (void)m_ipc->sendLine("STATUS media-state-rejected");
            return true;
        }
        snapshot.available = available != 0;
        snapshot.playing = playing != 0;
        snapshot.receivedAtMs = static_cast<std::uint64_t>(::GetTickCount64());
        if (snapshot.durationMs > 0)
            snapshot.positionMs = std::min(snapshot.positionMs, snapshot.durationMs);
        ::AcquireSRWLockExclusive(&m_mediaLock);
        // Metadata/timeline events and WASAPI frames are independent streams.
        // Preserve the latest audio bands when a STATE update arrives so the
        // spectrum does not flash to zero on every playback-position event.
        snapshot.spectrum = m_mediaSnapshot.spectrum;
        m_mediaSnapshot = snapshot;
        ::ReleaseSRWLockExclusive(&m_mediaLock);
        return true;
    }
    if (command == "MEDIA_SPECTRUM") {
        std::string encoded;
        std::string trailing;
        if (!(stream >> encoded) || (stream >> trailing) || encoded.size() > 80U)
            return true;
        std::replace(encoded.begin(), encoded.end(), ',', ' ');
        std::istringstream bands(encoded);
        std::array<float, 10U> parsed{};
        int value = 0;
        for (float& band : parsed) {
            if (!(bands >> value) || value < 0 || value > 1000) return true;
            band = static_cast<float>(value) / 1000.0F;
        }
        if (bands >> value) return true;
        ::AcquireSRWLockExclusive(&m_mediaLock);
        m_mediaSnapshot.spectrum = parsed;
        ::ReleaseSRWLockExclusive(&m_mediaLock);
        return true;
    }
    if (command == "MEDIA_SETTINGS") {
        int enabled = 0;
        std::uint32_t color = 0U;
        std::string trailing;
        MediaOverlaySettings settings{};
        if (!(stream >> enabled >> settings.opacity >> settings.previousHotkey >>
              settings.toggleHotkey >> settings.nextHotkey >> color >>
              settings.panelX >> settings.panelY) ||
            enabled < 0 || enabled > 1 || settings.opacity < 20 ||
            settings.opacity > 100 || settings.previousHotkey < 0 ||
            settings.previousHotkey > 254 || settings.toggleHotkey < 0 ||
            settings.toggleHotkey > 254 || settings.nextHotkey < 0 ||
            settings.nextHotkey > 254 || color > 0xFFFFFFU ||
            settings.panelX < -1 || settings.panelX > 1000 ||
            settings.panelY < -1 || settings.panelY > 1000) {
            (void)m_ipc->sendLine("STATUS media-settings-rejected");
            return true;
        }
        settings.enabled = enabled != 0;
        settings.panelColor = color;
        stream >> std::ws;
        if (!stream.eof() && (!(stream >> settings.spectrumOpacity) ||
            settings.spectrumOpacity < 0 || settings.spectrumOpacity > 100)) {
            (void)m_ipc->sendLine("STATUS media-settings-rejected");
            return true;
        }
        stream >> std::ws;
        if (!stream.eof()) {
            int wireScale = settings.scalePercent;
            if (!(stream >> wireScale) || (stream >> trailing)) {
                (void)m_ipc->sendLine("STATUS media-settings-rejected");
                return true;
            }
            if (wireScale >= 0 && wireScale <= 3) {
                static constexpr std::array<int, 4U> legacyScales{
                    42, 52, 68, 84};
                settings.scalePercent = legacyScales[
                    static_cast<std::size_t>(wireScale)];
            } else if (wireScale >= 35 && wireScale <= 100) {
                settings.scalePercent = wireScale;
            } else {
                (void)m_ipc->sendLine("STATUS media-settings-rejected");
                return true;
            }
        }
        ::AcquireSRWLockExclusive(&m_mediaLock);
        m_mediaSettings = settings;
        ::ReleaseSRWLockExclusive(&m_mediaLock);
        (void)m_ipc->sendLine("MEDIA_SETTINGS_APPLIED");
        return true;
    }
    if (command == "BED_RESCAN") {
        std::string trailing;
        if (stream >> trailing) {
            (void)m_ipc->sendLine("ERROR BAD_BED_RESCAN expected-no-arguments");
            return true;
        }
        m_bindings->requestBedRescan();
        (void)m_ipc->sendLine("BED_RESCAN_ACCEPTED");
        return true;
    }
    if (command == "BLACKLIST_RESET") {
        std::string trailing;
        if (stream >> trailing) return true;
        ::AcquireSRWLockExclusive(&m_blacklistLock);
        m_blacklistSnapshot = {};
        ::ReleaseSRWLockExclusive(&m_blacklistLock);
        m_blacklistSyncInProgress = true;
        return true;
    }
    if (command == "BLACKLIST_SETTINGS") {
        std::string enabledToken, alertsToken, idOnlyToken, showToken,
                    collapsedToken, trailing;
        int opacity = 0, x = -1, y = -1, width = 100, height = 100,
            contentScale = 100;
        std::uint32_t color = 0U;
        bool enabled = false, alerts = false, allowIdOnly = false;
        bool showWithClickGui = false, collapsed = false;
        if (!(stream >> enabledToken >> alertsToken >> idOnlyToken >> showToken
                     >> collapsedToken >> opacity >> color
                     >> x >> y >> width >> height >> contentScale) ||
            (stream >> trailing) ||
            !parseFlag(enabledToken, enabled) || !parseFlag(alertsToken, alerts) ||
            !parseFlag(idOnlyToken, allowIdOnly) ||
            !parseFlag(showToken, showWithClickGui) ||
            !parseFlag(collapsedToken, collapsed) || opacity < 0 || opacity > 100 ||
            color > 0xFFFFFFU || x < -1 || x > 1000 || y < -1 || y > 1000 ||
            width < 60 || width > 180 || height < 60 || height > 300 ||
            contentScale < 80 || contentScale > 200) return true;
        ::AcquireSRWLockExclusive(&m_blacklistLock);
        m_blacklistSnapshot.panelEnabled = enabled;
        m_blacklistSnapshot.matchAlertsEnabled = alerts;
        m_blacklistSnapshot.allowIdOnlyNicks = allowIdOnly;
        m_blacklistSnapshot.showWithClickGui = showWithClickGui;
        m_blacklistSnapshot.collapsed = collapsed;
        m_blacklistSnapshot.panelOpacity = opacity;
        m_blacklistSnapshot.contentScale = contentScale;
        m_blacklistSnapshot.panelColor = color;
        m_blacklistSnapshot.panelX = x;
        m_blacklistSnapshot.panelY = y;
        m_blacklistSnapshot.panelWidth = width;
        m_blacklistSnapshot.panelHeight = height;
        ::ReleaseSRWLockExclusive(&m_blacklistLock);
        if (!m_blacklistSyncInProgress)
            m_blacklistRevision.fetch_add(1U, std::memory_order_release);
        return true;
    }
    if (command == "BLACKLIST_PRESET") {
        std::string valueToken, trailing;
        std::array<char, 81U> value{};
        if (!(stream >> valueToken) || (stream >> trailing) ||
            !percentDecode(valueToken, value) || value[0U] == '\0') return true;
        ::AcquireSRWLockExclusive(&m_blacklistLock);
        if (m_blacklistSnapshot.presetCount < m_blacklistSnapshot.presets.size())
            m_blacklistSnapshot.presets[m_blacklistSnapshot.presetCount++] = value;
        ::ReleaseSRWLockExclusive(&m_blacklistLock);
        if (!m_blacklistSyncInProgress)
            m_blacklistRevision.fetch_add(1U, std::memory_order_release);
        return true;
    }
    if (command == "BLACKLIST_ENTRY") {
        std::string keyToken, uuidToken, nameToken, reasonToken, nickToken,
                    idOnlyToken, warningToken, faceToken, trailing;
        std::int64_t addedAt = 0;
        BlacklistEntry entry{};
        bool nick = false, idOnly = false, warning = false;
        if (!(stream >> keyToken >> uuidToken >> nameToken >> reasonToken >> addedAt
                     >> nickToken >> idOnlyToken >> warningToken >> faceToken) ||
            (stream >> trailing) || !parseFlag(nickToken, nick) ||
            !parseFlag(idOnlyToken, idOnly) || !parseFlag(warningToken, warning) ||
            !percentDecode(keyToken, entry.key) ||
            !percentDecode(uuidToken, entry.uuid) ||
            !percentDecode(nameToken, entry.name) ||
            !percentDecode(reasonToken, entry.reason) ||
            !percentDecode(faceToken, entry.facePath) || entry.key[0U] == '\0' ||
            entry.name[0U] == '\0') return true;
        entry.addedAt = addedAt;
        entry.nick = nick;
        entry.idOnly = idOnly;
        entry.warnOnEncounter = warning;
        ::AcquireSRWLockExclusive(&m_blacklistLock);
        std::uint32_t index = m_blacklistSnapshot.count;
        for (std::uint32_t candidate = 0U;
             candidate < m_blacklistSnapshot.count; ++candidate) {
            if (::_stricmp(m_blacklistSnapshot.entries[candidate].key.data(),
                           entry.key.data()) == 0) {
                index = candidate;
                break;
            }
        }
        if (index < m_blacklistSnapshot.entries.size()) {
            m_blacklistSnapshot.entries[index] = entry;
            if (index == m_blacklistSnapshot.count) ++m_blacklistSnapshot.count;
        }
        ::ReleaseSRWLockExclusive(&m_blacklistLock);
        if (!m_blacklistSyncInProgress)
            m_blacklistRevision.fetch_add(1U, std::memory_order_release);
        return true;
    }
    if (command == "BLACKLIST_REMOVE" || command == "BLACKLIST_WARNING") {
        std::string keyToken, valueToken, trailing;
        std::array<char, 50U> key{};
        const bool warningCommand = command == "BLACKLIST_WARNING";
        bool warning = false;
        if (!(stream >> keyToken) || !percentDecode(keyToken, key) ||
            (warningCommand && (!(stream >> valueToken) ||
                                !parseFlag(valueToken, warning))) ||
            (stream >> trailing)) return true;
        ::AcquireSRWLockExclusive(&m_blacklistLock);
        for (std::uint32_t index = 0U; index < m_blacklistSnapshot.count; ++index) {
            if (::_stricmp(m_blacklistSnapshot.entries[index].key.data(), key.data()) != 0)
                continue;
            if (warningCommand) {
                m_blacklistSnapshot.entries[index].warnOnEncounter = warning;
            } else {
                for (std::uint32_t move = index + 1U;
                     move < m_blacklistSnapshot.count; ++move) {
                    m_blacklistSnapshot.entries[move - 1U] =
                        m_blacklistSnapshot.entries[move];
                }
                --m_blacklistSnapshot.count;
                m_blacklistSnapshot.entries[m_blacklistSnapshot.count] = {};
            }
            break;
        }
        ::ReleaseSRWLockExclusive(&m_blacklistLock);
        m_blacklistRevision.fetch_add(1U, std::memory_order_release);
        return true;
    }
    if (command == "BLACKLIST_SYNC_END") {
        std::string trailing;
        if (!(stream >> trailing)) {
            m_blacklistSyncInProgress = false;
            m_blacklistRevision.fetch_add(1U, std::memory_order_release);
        }
        return true;
    }
    if (command == "STATS") {
        std::string playerToken;
        std::string teamToken;
        std::string trailing;
        PlayerStatsEntry entry{};
        if (!(stream >> playerToken >> teamToken >> entry.stars >> entry.fkdr >>
              entry.wlr >> entry.bblr >> entry.wins >> entry.finalKills >>
              entry.bedsBroken >> entry.winStreak >> entry.level) ||
            (stream >> trailing) || !std::isfinite(entry.fkdr) ||
            !std::isfinite(entry.wlr) || !std::isfinite(entry.bblr) ||
            entry.stars < 0 || entry.stars > 100000 || entry.fkdr < 0.0 ||
            entry.fkdr > 1000000.0 || entry.wlr < 0.0 || entry.wlr > 1000000.0 ||
            entry.bblr < 0.0 || entry.bblr > 1000000.0 || entry.wins < 0 ||
            entry.finalKills < 0 || entry.bedsBroken < 0 ||
            entry.winStreak < 0 || entry.winStreak > 1000000 ||
            entry.level < 0 || entry.level > 100000 ||
            !percentDecode(playerToken, entry.name) ||
            !percentDecode(teamToken, entry.teamPrefix)) {
            (void)m_ipc->sendLine("ERROR BAD_STATS invalid-payload");
            return true;
        }
        const std::string_view playerName(entry.name.data());
        const bool validName = !playerName.empty() && playerName.size() <= 16U &&
            std::all_of(playerName.begin(), playerName.end(), [](const char character) noexcept {
                return (character >= 'A' && character <= 'Z') ||
                       (character >= 'a' && character <= 'z') ||
                       (character >= '0' && character <= '9') || character == '_';
            });
        const bool validTeam = static_cast<unsigned char>(entry.teamPrefix[0U]) == 0xC2U &&
            static_cast<unsigned char>(entry.teamPrefix[1U]) == 0xA7U &&
            (((entry.teamPrefix[2U] >= '0' && entry.teamPrefix[2U] <= '9') ||
              (entry.teamPrefix[2U] >= 'a' && entry.teamPrefix[2U] <= 'f'))) &&
            entry.teamPrefix[3U] == '\0';
        if (!validName || !validTeam) {
            (void)m_ipc->sendLine("ERROR BAD_STATS invalid-player-or-team");
            return true;
        }
        ::AcquireSRWLockExclusive(&m_playerStatsLock);
        if (!m_playerStats.contains(std::string(playerName)) && m_playerStats.size() >= 256U) {
            m_playerStats.erase(m_playerStats.begin());
        }
        m_playerStats[std::string(playerName)] = entry;
        ::ReleaseSRWLockExclusive(&m_playerStatsLock);
        m_bindings->enqueueDebugChatLine(
            "stats_ready player=" + std::string(playerName) +
            " stars=" + std::to_string(entry.stars) +
            " fkdr=" + std::to_string(entry.fkdr) +
            " wlr=" + std::to_string(entry.wlr) +
            " beds=" + std::to_string(entry.bedsBroken) +
            " level=" + std::to_string(entry.level));
        return true;
    }
    if (command == "STATS_ERROR") {
        std::string playerToken;
        std::string reasonToken;
        std::string trailing;
        PlayerStatsEntry entry{};
        if (!(stream >> playerToken >> reasonToken) || (stream >> trailing) ||
            !percentDecode(playerToken, entry.name) ||
            !percentDecode(reasonToken, entry.status)) {
            (void)m_ipc->sendLine("ERROR BAD_STATS_ERROR invalid-payload");
            return true;
        }
        const std::string_view playerName(entry.name.data());
        const bool validName = !playerName.empty() && playerName.size() <= 16U &&
            std::all_of(playerName.begin(), playerName.end(), [](const char character) noexcept {
                return (character >= 'A' && character <= 'Z') ||
                       (character >= 'a' && character <= 'z') ||
                       (character >= '0' && character <= '9') || character == '_';
            });
        if (!validName || entry.status[0U] == '\0') {
            (void)m_ipc->sendLine("ERROR BAD_STATS_ERROR invalid-player-or-reason");
            return true;
        }
        entry.failed = true;
        ::AcquireSRWLockExclusive(&m_playerStatsLock);
        if (!m_playerStats.contains(std::string(playerName)) &&
            m_playerStats.size() >= 256U) {
            m_playerStats.erase(m_playerStats.begin());
        }
        m_playerStats[std::string(playerName)] = entry;
        ::ReleaseSRWLockExclusive(&m_playerStatsLock);
        m_bindings->enqueueDebugChatLine(
            "stats_error player=" + std::string(playerName) +
            " reason=" + std::string(entry.status.data()));
        return true;
    }
    if (command == "HYPIXEL_RESULT") {
        int state = 0;
        std::string uuidToken;
        std::string nameToken;
        std::string statusToken;
        HypixelOverlaySnapshot snapshot{};
        if (!(stream >> state >> uuidToken >> nameToken >> snapshot.wins >> snapshot.losses
              >> snapshot.finalKills >> snapshot.finalDeaths >> snapshot.bedsBroken
              >> snapshot.bedsLost >> snapshot.winRate >> snapshot.fkdr >> statusToken) ||
            state < 0 || state > 3 || !std::isfinite(snapshot.winRate) ||
            !std::isfinite(snapshot.fkdr) ||
            !percentDecode(uuidToken, snapshot.uuid) ||
            !percentDecode(nameToken, snapshot.displayName) ||
            !percentDecode(statusToken, snapshot.status)) {
            // Hypixel is an optional UI data source.  Never terminate an
            // otherwise healthy overlay session because one response was
            // truncated or malformed.  Keep a bounded error snapshot and let
            // the next valid Controller update replace it.
            HypixelOverlaySnapshot rejected{};
            rejected.state = HypixelOverlaySnapshot::State::Error;
            constexpr std::string_view reason = "Statistics response was rejected";
            std::copy(reason.begin(), reason.end(), rejected.status.begin());
            ::AcquireSRWLockExclusive(&m_hypixelLock);
            m_hypixelSnapshot = rejected;
            ::ReleaseSRWLockExclusive(&m_hypixelLock);
            (void)m_ipc->sendLine("STATUS hypixel-result-rejected");
            return true;
        }
        snapshot.state = static_cast<HypixelOverlaySnapshot::State>(state);
        ::AcquireSRWLockExclusive(&m_hypixelLock);
        m_hypixelSnapshot = snapshot;
        ::ReleaseSRWLockExclusive(&m_hypixelLock);
        return true;
    }
    if (command == "DETACH") {
        m_visible.store(false, std::memory_order_release);
        m_interactive.store(false, std::memory_order_release);
        m_detachRequested.store(true, std::memory_order_release);
        (void)m_ipc->sendLine("STATUS detaching");
        return false;
    }
    (void)m_ipc->sendLine("ERROR UNKNOWN_COMMAND unsupported-command");
    return true;
}

} // namespace mcoverlay
