#include "GameBindingsCache.internal.h"
#include "SmartHotbarPolicy.h"
#include "TrajectoryMath.h"
#include "SafeWalkPolicy.h"

#include "src/AgentLog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcoverlay {

void GameBindings::enqueueDebugChatLine(const std::string_view line) noexcept
{
    if (line.empty()) return;
    std::array<char, DebugLineCapacity> copy{};
    std::size_t length = 0U;
    for (const char character : line) {
        if (length + 1U >= copy.size()) break;
        if (character == '\r' || character == '\n' || character == '\t') continue;
        copy[length++] = character;
    }
    if (length == 0U) return;

    ::AcquireSRWLockExclusive(&m_debugQueueLock);
    std::uint32_t target = 0U;
    if (m_debugQueueCount < DebugQueueCapacity) {
        target = (m_debugQueueHead + m_debugQueueCount) % DebugQueueCapacity;
        ++m_debugQueueCount;
    } else {
        target = m_debugQueueHead;
        m_debugQueueHead = (m_debugQueueHead + 1U) % DebugQueueCapacity;
    }
    m_debugQueue[target] = copy;
    ::ReleaseSRWLockExclusive(&m_debugQueueLock);
}

void GameBindings::enqueueWarningChatLine(
    const std::string_view playerName,
    const std::string_view reason) noexcept
{
    if (playerName.empty()) return;
    WarningChatLine line{};
    const auto copyClean = [](const std::string_view source, char* const target,
                              const std::size_t capacity) noexcept {
        std::size_t length = 0U;
        for (const char character : source) {
            if (length + 1U >= capacity) break;
            if (character == '\r' || character == '\n' || character == '\t' ||
                character == '\0') continue;
            target[length++] = character;
        }
    };
    copyClean(playerName, line.playerName.data(), line.playerName.size());
    copyClean(reason, line.reason.data(), line.reason.size());
    if (line.playerName[0U] == '\0') return;

    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    std::uint32_t target = 0U;
    if (m_warningQueueCount < WarningQueueCapacity) {
        target = (m_warningQueueHead + m_warningQueueCount) % WarningQueueCapacity;
        ++m_warningQueueCount;
    } else {
        target = m_warningQueueHead;
        m_warningQueueHead = (m_warningQueueHead + 1U) % WarningQueueCapacity;
    }
    m_warningQueue[target] = line;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);
}

void GameBindings::publishDebugChat(JNIEnv* const env, const bool enabled) noexcept
{
    std::uint32_t warningCount = 0U;
    ::AcquireSRWLockShared(&m_warningQueueLock);
    warningCount = m_warningQueueCount;
    ::ReleaseSRWLockShared(&m_warningQueueLock);
    if (!enabled) {
        // Re-enabling the option should print the current state even if the
        // roster itself did not change while the option was disabled.
        m_debugRosterGeneration = 0U;
        m_debugBedOwnershipGeneration = 0U;
        m_debugMatchProbeGeneration = 0U;
        ::AcquireSRWLockExclusive(&m_debugQueueLock);
        m_debugQueueHead = 0U;
        m_debugQueueCount = 0U;
        ::ReleaseSRWLockExclusive(&m_debugQueueLock);
    }
    std::uint32_t pendingCount = 0U;
    ::AcquireSRWLockShared(&m_debugQueueLock);
    pendingCount = m_debugQueueCount;
    ::ReleaseSRWLockShared(&m_debugQueueLock);
    const bool probeChanged = enabled && m_matchProbeGeneration !=
        m_debugMatchProbeGeneration;
    const bool rosterChanged = enabled && m_snapshot.matchActive &&
        m_snapshot.playerRosterGeneration != 0U &&
        m_snapshot.playerRosterGeneration != m_debugRosterGeneration;
    const bool bedChanged = enabled && m_snapshot.matchActive &&
        m_bedOwnershipGeneration !=
        m_debugBedOwnershipGeneration;
    if (env == nullptr ||
        (!probeChanged && !rosterChanged && !bedChanged && pendingCount == 0U &&
         warningCount == 0U && !m_logicalController.debug().hasChat()) ||
        m_resolutionPhase.load(std::memory_order_acquire) != ResolutionPhase::Resolved ||
        m_cache == nullptr) {
        return;
    }

    BindingCache* const cache = m_cache.get();
    if (cache->chatTextClass == nullptr || cache->chatTextConstructor == nullptr ||
        cache->addChatMessage == nullptr || env->PushLocalFrame(192) != JNI_OK) {
        clearException(env);
        return;
    }

    jobject minecraft = cache->minecraftInstanceField != nullptr
        ? env->GetStaticObjectField(cache->minecraftClass, cache->minecraftInstanceField)
        : env->CallStaticObjectMethod(cache->minecraftClass, cache->getMinecraft);
    jobject player = minecraft == nullptr ? nullptr :
        env->GetObjectField(minecraft, cache->playerField);
    if (env->ExceptionCheck() == JNI_TRUE || player == nullptr) {
        clearException(env);
        env->PopLocalFrame(nullptr);
        return;
    }

    // Consume generations only after a usable local chat endpoint and player
    // object exist. This avoids silently discarding the exact diagnostics that
    // are needed when a transformed client temporarily exposes an incomplete
    // game state.
    for(int i=0;i<8;++i) {
        std::string text;
        if(!m_logicalController.debug().popChat(text)) break;
        text="\xC2\xA7" "b"+text;
        jstring string=env->NewStringUTF(text.c_str());
        jobject component=string ? env->NewObject(cache->chatTextClass,cache->chatTextConstructor,string) : nullptr;
        if(component && !env->ExceptionCheck()) env->CallVoidMethod(player,cache->addChatMessage,component);
        if(component) env->DeleteLocalRef(component);
        if(string) env->DeleteLocalRef(string);
        clearException(env);
    }
    if (enabled) {
        m_debugMatchProbeGeneration = m_matchProbeGeneration;
        m_debugRosterGeneration = m_snapshot.playerRosterGeneration;
        m_debugBedOwnershipGeneration = m_bedOwnershipGeneration;
    }
    std::array<std::array<char, DebugLineCapacity>, DebugQueueCapacity> queued{};
    ::AcquireSRWLockExclusive(&m_debugQueueLock);
    const std::uint32_t queuedCount = m_debugQueueCount;
    for (std::uint32_t index = 0U; index < queuedCount; ++index) {
        queued[index] = m_debugQueue[
            (m_debugQueueHead + index) % DebugQueueCapacity];
    }
    m_debugQueueHead = 0U;
    m_debugQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_debugQueueLock);
    std::array<WarningChatLine, WarningQueueCapacity> warnings{};
    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    const std::uint32_t queuedWarningCount = m_warningQueueCount;
    for (std::uint32_t index = 0U; index < queuedWarningCount; ++index) {
        warnings[index] = m_warningQueue[
            (m_warningQueueHead + index) % WarningQueueCapacity];
    }
    m_warningQueueHead = 0U;
    m_warningQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);

    // Each letter is deliberately assigned a different legacy chat color.
    // This component is added straight to EntityPlayerSP and never reaches a
    // network handler, so the message remains visible only to this client.
    constexpr std::string_view prefix{
        "[\xC2\xA7" "bD\xC2\xA7" "de\xC2\xA7" "ab\xC2\xA7" "eu\xC2\xA7" "6g\xC2\xA7" "r] "};
    auto addLine = [&](const std::string& body) noexcept {
        const std::string line = std::string(prefix) + body;
        jstring text = env->NewStringUTF(line.c_str());
        jobject component = text == nullptr ? nullptr :
            env->NewObject(cache->chatTextClass, cache->chatTextConstructor, text);
        if (component != nullptr && env->ExceptionCheck() != JNI_TRUE) {
            env->CallVoidMethod(player, cache->addChatMessage, component);
        }
        clearException(env);
    };
    const auto jsonEscape = [](const char* const source) {
        std::string escaped;
        if (source == nullptr) return escaped;
        escaped.reserve(std::strlen(source) + 8U);
        for (const unsigned char character : std::string_view(source)) {
            switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character >= 0x20U) escaped.push_back(
                    static_cast<char>(character));
                break;
            }
        }
        return escaped;
    };
    auto addWarning = [&](const WarningChatLine& warning) noexcept {
        const std::string name = jsonEscape(warning.playerName.data());
        const std::string reason = jsonEscape(warning.reason.data());
        if (cache->chatSerializerClass != nullptr &&
            cache->parseChatJson != nullptr) {
            const std::string command = "/wdr " + name;
            const std::string json =
                "{\"text\":\"\",\"clickEvent\":{\"action\":\"suggest_command\","
                "\"value\":\"" + command + "\"},\"extra\":["
                "{\"text\":\"[WARNING]\",\"color\":\"red\",\"bold\":true},"
                "{\"text\":\" Blacklisted player \"},"
                "{\"text\":\"" + name + "\",\"color\":\"gold\",\"bold\":true},"
                "{\"text\":\" — " + reason +
                " (click to prepare /wdr)\",\"color\":\"yellow\"}]}";
            jstring text = env->NewStringUTF(json.c_str());
            jobject component = text == nullptr ? nullptr :
                env->CallStaticObjectMethod(cache->chatSerializerClass,
                                            cache->parseChatJson, text);
            if (component != nullptr && env->ExceptionCheck() != JNI_TRUE) {
                env->CallVoidMethod(player, cache->addChatMessage, component);
                clearException(env);
                return;
            }
            clearException(env);
        }
        std::string fallback =
            "\xC2\xA7" "c\xC2\xA7" "l[WARNING]\xC2\xA7" "r Blacklisted player ";
        fallback += warning.playerName.data();
        fallback += " - ";
        fallback += warning.reason.data();
        fallback += " \xC2\xA7" "e(/wdr ";
        fallback += warning.playerName.data();
        fallback += ')';
        jstring text = env->NewStringUTF(fallback.c_str());
        jobject component = text == nullptr ? nullptr :
            env->NewObject(cache->chatTextClass, cache->chatTextConstructor, text);
        if (component != nullptr && env->ExceptionCheck() != JNI_TRUE)
            env->CallVoidMethod(player, cache->addChatMessage, component);
        clearException(env);
    };

    if (probeChanged) {
        const auto teamText = [](const char team) noexcept {
            return team == 'u' ? std::string("unknown") : std::string(1U, team);
        };
        std::string body = "match_probe active=";
        body += m_snapshot.matchActive ? "true" : "false";
        body += " sidebar=" + std::to_string(m_matchProbe.sidebarAvailable ? 1 : 0);
        body += " tab=" + std::to_string(m_matchProbe.tabAvailable ? 1 : 0);
        body += " lines=" + std::to_string(m_matchProbe.sidebarLines);
        body += " teams=" + std::to_string(m_matchProbe.sidebarTeams);
        body += " you=" + std::to_string(m_matchProbe.sidebarYouRows);
        body += " roster_tags=" + std::to_string(m_matchProbe.rosterTaggedPlayers);
        body += " roster_players=" + std::to_string(m_matchProbe.rosterPlayers);
        body += " roster_teams=" + std::to_string(m_matchProbe.rosterTeams);
        body += " roster_own=" + teamText(m_matchProbe.rosterOwnTeam);
        body += " armor_own=" + teamText(m_matchProbe.localArmorTeam);
        body += " armor_teams=" + std::to_string(m_matchProbe.armorTeams);
        body += " evidence=";
        if (m_matchProbe.sidebarEvidence) body += 'S';
        if (m_matchProbe.rosterEvidence) body += 'R';
        if (m_matchProbe.armorEvidence) body += 'A';
        if (!m_matchProbe.sidebarEvidence && !m_matchProbe.rosterEvidence &&
            !m_matchProbe.armorEvidence) body += '-';
        body += " stable=" + std::to_string(m_matchProbe.stableCount);
        body += " beds=" + std::to_string(m_snapshot.bedMarkerCount);
        addLine(body);
    }

    if (rosterChanged) {
        std::string matchLine = "game_started=true own_team=";
        if (m_snapshot.ownTeam == 'u') {
            matchLine += "unknown";
        } else {
            matchLine += "\xC2\xA7";
            matchLine.push_back(m_snapshot.ownTeam);
            matchLine.push_back(m_snapshot.ownTeam);
            matchLine += "\xC2\xA7r";
        }
        addLine(matchLine);

        for (std::uint32_t index = 0U; index < m_snapshot.playerCount; ++index) {
            const PlayerIdentity& identity = m_snapshot.players[index];
            if (identity.name[0U] == '\0') continue;
            const bool teammate = m_snapshot.ownTeam != 'u' &&
                identity.teamColor == m_snapshot.ownTeam;
            std::string body = "player=";
            if (identity.teamColor != 'u') {
                body += "\xC2\xA7";
                body.push_back(identity.teamColor);
            }
            body += identity.name.data();
            body += "\xC2\xA7r team=";
            if (identity.teamColor == 'u') {
                body += "unknown";
            } else {
                body += "\xC2\xA7";
                body.push_back(identity.teamColor);
                body.push_back(identity.teamColor);
                body += "\xC2\xA7r";
            }
            body += " teammate=";
            body += teammate ? "true" : "false";
            addLine(body);
        }
    }
    if (bedChanged) {
        std::string body = "own_bed=";
        if (!m_snapshot.ownBedKnown) {
            body += "unknown";
        } else {
            body += "true pos=" + std::to_string(m_snapshot.ownBedX) + "," +
                std::to_string(m_snapshot.ownBedY) + "," +
                std::to_string(m_snapshot.ownBedZ) + " source=";
            body += m_snapshot.ownBedSource == GameSnapshot::OwnBedSource::TeamWool
                ? "team_wool" : "match_spawn";
        }
        addLine(body);
    }
    for (std::uint32_t index = 0U; index < queuedCount; ++index) {
        if (queued[index][0U] != '\0') addLine(queued[index].data());
    }
    for (std::uint32_t index = 0U; index < queuedWarningCount; ++index) {
        if (warnings[index].playerName[0U] != '\0') addWarning(warnings[index]);
    }
    env->PopLocalFrame(nullptr);
}


} // namespace mcoverlay
