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
class CallbackGuard final {
public:
    CallbackGuard(std::atomic<unsigned>& count, HANDLE idleEvent) noexcept
        : m_count(count), m_idleEvent(idleEvent)
    {
        if (m_count.fetch_add(1U, std::memory_order_acq_rel) == 0U) {
            ::ResetEvent(m_idleEvent);
        }
    }

    ~CallbackGuard()
    {
        if (m_count.fetch_sub(1U, std::memory_order_acq_rel) == 1U) {
            ::SetEvent(m_idleEvent);
        }
    }

private:
    std::atomic<unsigned>& m_count;
    HANDLE m_idleEvent;
};

class SrwExclusiveGuard final {
public:
    explicit SrwExclusiveGuard(SRWLOCK& lock) noexcept : m_lock(&lock) {}
    ~SrwExclusiveGuard() { ::ReleaseSRWLockExclusive(m_lock); }

    SrwExclusiveGuard(const SrwExclusiveGuard&) = delete;
    SrwExclusiveGuard& operator=(const SrwExclusiveGuard&) = delete;

private:
    SRWLOCK* m_lock;
};

}

void AgentRuntime::frameEntry(void* const context, HDC const deviceContext) noexcept
{
    // A hook must never unwind through opengl32/gdi32. Runtime-owned work uses
    // fixed storage and nothrow Win32 primitives; this final boundary also
    // contains any unexpected third-party/backend exception.
    try {
        static_cast<AgentRuntime*>(context)->beforeSwapBuffers(deviceContext);
    } catch (...) {
        static_cast<AgentRuntime*>(context)->m_frameFaulted.store(
            true, std::memory_order_release);
    }
}

void AgentRuntime::beforeSwapBuffers(HDC const deviceContext)
{
    CallbackGuard guard(m_activeCallbacks, m_callbacksIdleEvent);
    if (::TryAcquireSRWLockExclusive(&m_renderLock) == FALSE) {
        return;
    }
    SrwExclusiveGuard renderGuard(m_renderLock);

    // Vanilla LWJGL normally calls SwapBuffers on a Java-owned thread, but
    // Lunar can present from a native helper thread. Attach that long-lived
    // renderer once as a daemon instead of doing an Attach/Detach pair every
    // frame. This is required for Mouse.setGrabbed(false) to actually execute
    // on Lunar while the Click GUI is open.
    JNIEnv* env = nullptr;
    if (m_vm != nullptr) {
        const jint environmentResult = m_vm->GetEnv(
            reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (environmentResult == JNI_EDETACHED) {
            JavaVMAttachArgs arguments{};
            arguments.version = JNI_VERSION_1_6;
            arguments.name = const_cast<char*>("McOverlayRender");
            if (m_vm->AttachCurrentThreadAsDaemon(
                    reinterpret_cast<void**>(&env), &arguments) == JNI_OK) {
                m_renderThreadAttachedByAgent = true;
                m_renderJvmThreadId = ::GetCurrentThreadId();
                log::info("Attached the native OpenGL presentation thread to the JVM.");
            } else {
                env = nullptr;
            }
        } else if (environmentResult != JNI_OK) {
            env = nullptr;
        }
    }

    if (m_shutdownRequested.load(std::memory_order_acquire)) {
        if (m_renderCleanupCompleted.load(std::memory_order_acquire)) {
            ::SetEvent(m_rendererStoppedEvent);
            return;
        }
        if (m_renderer->initialized() && !m_renderer->ownsCurrentContext()) {
            return;
        }
        bool expected = false;
        if (!m_renderCleanupStarted.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            return;
        }
        if (m_gameInputReleased && env != nullptr) {
            (void)m_bindings->setInputCaptured(env, false);
            m_gameInputReleased = false;
        }
        // This callback is the only place where the renderer's original HGLRC
        // is guaranteed current. JNI globals are released later by the worker,
        // after hooks and all frame callbacks have drained.
        if (m_renderer->initialized()) {
            m_renderer->shutdownWithCurrentContext();
        }
        if (m_renderThreadAttachedByAgent && m_vm != nullptr &&
            m_renderJvmThreadId == ::GetCurrentThreadId()) {
            (void)m_vm->DetachCurrentThread();
            m_renderThreadAttachedByAgent = false;
            m_renderJvmThreadId = 0U;
            env = nullptr;
        }
        m_renderCleanupCompleted.store(true, std::memory_order_release);
        ::SetEvent(m_rendererStoppedEvent);
        return;
    }

    m_renderer->setMenuHotkey(m_menuHotkey.load(std::memory_order_acquire));
    const bool gameScreenOpen = m_bindings->gameScreenOpen(env);
    // Menus without a world (title screen/test harness) still allow opening
    // ClickGUI. In-world screens, especially chat, own their keyboard input.
    m_renderer->setGameScreenOpen(gameScreenOpen &&
        m_bindings->hasPlayerSnapshot());
    if (m_renderer->consumeClickGuiToggle()) {
        const bool next = !m_interactive.load(std::memory_order_acquire);
        m_interactive.store(next, std::memory_order_release);
        if (next) m_visible.store(true, std::memory_order_release);
        queueStateChanged(m_visible.load(std::memory_order_acquire), next);
    }

    const std::uint64_t tickMilliseconds =
        static_cast<std::uint64_t>(::GetTickCount64());
    FeatureSettings activeFeatures = unpackFeatures(
        m_featureBits.load(std::memory_order_acquire),
        m_bedDefenseRadius.load(std::memory_order_acquire),
        m_bedThreatRadius.load(std::memory_order_acquire),
        m_bedDefenseHotkey.load(std::memory_order_acquire),
        m_bedDefensePanelOpacity.load(std::memory_order_acquire),
        m_hypixelPanelHotkey.load(std::memory_order_acquire),
        m_hypixelPanelOpacity.load(std::memory_order_acquire),
        m_hypixelPanelScale.load(std::memory_order_acquire),
        m_hypixelPanelX.load(std::memory_order_acquire),
        m_hypixelPanelY.load(std::memory_order_acquire),
        m_clickGuiLightTheme.load(std::memory_order_acquire),
        m_playerEspColor.load(std::memory_order_acquire),
        m_bedEspColor.load(std::memory_order_acquire),
        m_bedDefensePanelColor.load(std::memory_order_acquire),
        m_hypixelPanelColor.load(std::memory_order_acquire),
        m_hypixelPanelHeight.load(std::memory_order_acquire),
        m_nametagPanelOpacity.load(std::memory_order_acquire),
        m_nametagPanelColor.load(std::memory_order_acquire),
        m_clickGuiAccentColor.load(std::memory_order_acquire),
        m_hypixelPanelFontIndex.load(std::memory_order_acquire),
        m_nametagRange.load(std::memory_order_acquire),
        m_nametagSizeIndex.load(std::memory_order_acquire),
        m_hypixelRailColor.load(std::memory_order_acquire),
        m_hypixelRailOpacity.load(std::memory_order_acquire),
        m_safewalkReleaseDelayMs.load(std::memory_order_acquire),
        m_safewalkEdgeSensitivity.load(std::memory_order_acquire),
        m_safewalkMinimumPitch.load(std::memory_order_acquire),
        m_safewalkHotkey.load(std::memory_order_acquire),
        m_flySpeedPercent.load(std::memory_order_acquire),
        m_aimSlowdownPercent.load(std::memory_order_acquire),
        m_aimSpeedPercent.load(std::memory_order_acquire),
        m_textGuiColor.load(std::memory_order_acquire),
        m_textGuiX.load(std::memory_order_acquire),
        m_textGuiY.load(std::memory_order_acquire),
        m_bhopAirSpeedPercent.load(std::memory_order_acquire),
        m_featureHotkeysPackedA.load(std::memory_order_acquire),
        m_featureHotkeysPackedB.load(std::memory_order_acquire),
        m_fireballEspEnabled.load(std::memory_order_acquire),
        m_fireballEspFilled.load(std::memory_order_acquire),
        m_longJumpEnabled.load(std::memory_order_acquire),
        m_longJumpSpeedPercent.load(std::memory_order_acquire),
        m_fireballEspColor.load(std::memory_order_acquire),
        m_aimMinimumDistance.load(std::memory_order_acquire),
        m_aimMaximumDistance.load(std::memory_order_acquire),
        m_aimFovDegrees.load(std::memory_order_acquire),
        m_clickGuiWidthPercent.load(std::memory_order_acquire),
        m_clickGuiHeightPercent.load(std::memory_order_acquire),
        m_clickGuiOpacity.load(std::memory_order_acquire),
        m_featureExtraBits.load(std::memory_order_acquire),
        m_textGuiAlignment.load(std::memory_order_acquire),
        m_localMobReach.load(std::memory_order_acquire),
        m_localAttackDelayMs.load(std::memory_order_acquire),
        m_localVelocityPercent.load(std::memory_order_acquire),
        m_featureHotkeysPackedC.load(std::memory_order_acquire));
    const std::uint32_t smartHotbarConfig=m_smartHotbarConfig.load(
        std::memory_order_acquire);
    activeFeatures.smartHotbarEnabled=hotbar::enabled(smartHotbarConfig);
    activeFeatures.smartHotbarActions=hotbar::unpack(smartHotbarConfig);
    activeFeatures.smartHotbarRefill=(smartHotbarConfig&hotbar::Refill)!=0U;
    activeFeatures.sprintEnabled=(smartHotbarConfig&hotbar::Sprint)!=0U;
    activeFeatures.nametagAlways=(smartHotbarConfig&hotbar::AllNames)!=0U;
    const bool interactiveNow = m_interactive.load(std::memory_order_acquire);
    const unsigned aimOptions=m_aimOptions.load(std::memory_order_acquire);
    activeFeatures.aimSilentLock=(aimOptions&1U)!=0;
    activeFeatures.silentFileDebug=(aimOptions&0x08000000U)!=0;
    activeFeatures.silentChatDebug=(aimOptions&0x10000000U)!=0;
    activeFeatures.aimScannerEnabled=(aimOptions&2U)!=0;
    activeFeatures.bedBreakerEnabled=(aimOptions&4U)!=0;
    activeFeatures.aimAttackViability=(aimOptions&8U)!=0;
    activeFeatures.aimSequentialTargets=(aimOptions&0x20000000U)!=0;
    activeFeatures.silentControlAdaptation=(aimOptions&0x40000000U)!=0;
    activeFeatures.textGuiShowModes=(aimOptions&16U)!=0;
    activeFeatures.localVelocityProbability=static_cast<int>((aimOptions>>5U)&0x7FU);
    activeFeatures.localVelocityVerticalPercent=static_cast<int>((aimOptions>>12U)&0x7FU);
    activeFeatures.featureHotkeys[16U]=static_cast<int>((aimOptions>>19U)&0xFFU);
    activeFeatures.aimAttackCps=std::clamp(
        m_aimAttackCps.load(std::memory_order_acquire),1,20);
    activeFeatures.longJumpEnabled=false;
    activeFeatures.localMobAuraEnabled=false;
    if (env != nullptr) {
        if (interactiveNow && !m_gameInputReleased) {
            if (m_bindings->setInputCaptured(env, true)) {
                m_gameInputReleased = true;
                m_lastInputFocusReleaseTick = tickMilliseconds;
            }
        } else if (interactiveNow) {
            // Lunar and other transformed clients can re-enable relative input
            // every game tick. Keep the public LWJGL grab released each frame,
            // and reassert Minecraft.inGameHasFocus at a bounded 10 Hz when a
            // verified mapping is available.
            (void)m_bindings->maintainInputReleased(env);
            if (tickMilliseconds - m_lastInputFocusReleaseTick >= 100U) {
                (void)m_bindings->setInputCaptured(env, true);
                m_lastInputFocusReleaseTick = tickMilliseconds;
            }
        } else if (m_gameInputReleased && m_bindings->setInputCaptured(env, false)) {
            m_gameInputReleased = false;
            m_lastInputFocusReleaseTick = 0U;
        }

    }

    // Renderer readiness must not depend on Minecraft class mappings. Lunar
    // and other transformed clients may never match the supported 1.8.9 names,
    // but a valid HWND/HGLRC is still sufficient to initialize and display
    // ImGui. GetLoadedClasses and ID lookup live exclusively on m_resolver;
    // this callback only copies atomic progress and samples an already-published
    // immutable cache.
    if (env != nullptr) {
        (void)m_bindings->sample(env, tickMilliseconds);
        // ActiveRenderInfo changes with every camera transform. Keep this out
        // of the 10 Hz telemetry sampler so rotation, FOV and view bobbing are
        // reflected by the very same frame that is about to be presented.
        m_bindings->sampleCamera(env);
    }
    GameSnapshot snapshot = m_bindings->snapshot(tickMilliseconds);
    snapshot.gameScreenOpen = gameScreenOpen;
    snapshot.entityRenderTick = m_entityRenderClock.update(snapshot.entitySampleGeneration,
        snapshot.worldGeneration, snapshot.camera.partialTicks);

    // Server guard is evaluated from currentServerData.serverIP and therefore
    // remains active in lobbies and during respawn. The explicit override is
    // persisted separately and never inferred from a feature hotkey.
    if (snapshot.hypixelServer && !activeFeatures.allowHypixelMovement &&
        (activeFeatures.aimAssistEnabled || activeFeatures.scaffoldEnabled || activeFeatures.flyEnabled ||
         activeFeatures.bhopEnabled)) {
        activeFeatures.aimAssistEnabled = false;
        activeFeatures.scaffoldEnabled = false;
        activeFeatures.flyEnabled = false;
        activeFeatures.bhopEnabled = false;
        queueFeatureChanged(activeFeatures);
        m_bindings->enqueueDebugChatLine(
            "[Server Guard] Aim Assist/Fly/BHop/Scaffold were disabled on Hypixel.");
    }

    // New local diagnostics are fail-closed. A warning cannot be used as an
    // override: only an integrated single-player server may execute them.
    if (!snapshot.integratedSinglePlayer &&
        (activeFeatures.longJumpEnabled || activeFeatures.fireballEspEnabled ||
         activeFeatures.localMobAuraEnabled ||
         activeFeatures.localVelocityEnabled || activeFeatures.bedBreakerEnabled)) {
        activeFeatures.longJumpEnabled = false;
        activeFeatures.fireballEspEnabled = false;
        activeFeatures.localMobAuraEnabled = false;
        activeFeatures.localVelocityEnabled = false;
        activeFeatures.bedBreakerEnabled = false;
        queueFeatureChanged(activeFeatures);
        m_bindings->enqueueDebugChatLine(
            "[Local Guard] Local diagnostics require an integrated single-player world.");
    }

    if (env != nullptr) {
        GameplaySettings gameplay{};
        const bool gameForeground =
            ::GetForegroundWindow() == ::WindowFromDC(deviceContext);
        const bool gameplayInput = !interactiveNow && !gameScreenOpen &&
            gameForeground;
        gameplay.safewalk = activeFeatures.safewalkEnabled && gameplayInput;
        gameplay.scaffold = activeFeatures.scaffoldEnabled && gameplayInput;
        gameplay.scaffoldSameLayerOnly = activeFeatures.scaffoldSameLayerOnly;
        gameplay.fly = activeFeatures.flyEnabled && gameplayInput;
        gameplay.bhop = activeFeatures.bhopEnabled && gameplayInput;
        gameplay.bhopAutoJump = activeFeatures.bhopAutoJump;
        gameplay.freeLookConfigured = activeFeatures.freeLookEnabled;
        gameplay.freeLookGuiOpen = interactiveNow || gameScreenOpen;
        gameplay.freeLookForeground = gameForeground;
        gameplay.freeLook = activeFeatures.freeLookEnabled && gameplayInput;
        gameplay.freeLookHotkey = activeFeatures.featureHotkeys[17U];
        gameplay.smartHotbar=activeFeatures.smartHotbarEnabled&&gameplayInput;
        gameplay.smartHotbarActions=activeFeatures.smartHotbarActions;
        gameplay.smartHotbarRefill=activeFeatures.smartHotbarRefill;
        gameplay.forceSprint=activeFeatures.sprintEnabled&&gameplayInput;
        gameplay.shieldAttackerId=gameplayInput?m_renderer->shieldAttacker(snapshot):-1;
        gameplay.aimAssist = activeFeatures.aimAssistEnabled && gameplayInput;
        gameplay.longJump = activeFeatures.longJumpEnabled && gameplayInput &&
                            snapshot.integratedSinglePlayer;
        gameplay.aimLockOnMode = activeFeatures.aimLockOnMode;
        gameplay.aimSilentLock = activeFeatures.aimSilentLock;
        gameplay.silentFileDebug = activeFeatures.silentFileDebug;
        gameplay.silentChatDebug = activeFeatures.silentChatDebug;
        gameplay.aimAttackViability = activeFeatures.aimAttackViability;
        gameplay.silentControlAdaptation = activeFeatures.silentControlAdaptation;
        gameplay.aimSequentialTargets = activeFeatures.aimSequentialTargets;
        gameplay.aimNearestPriority = activeFeatures.aimNearestPriority;
        gameplay.bedBreaker = activeFeatures.bedBreakerEnabled && gameplayInput &&
            snapshot.integratedSinglePlayer;
        gameplay.localMobAura = activeFeatures.localMobAuraEnabled &&
            gameplayInput && snapshot.integratedSinglePlayer;
        gameplay.localVelocity = activeFeatures.localVelocityEnabled &&
            gameplayInput && snapshot.integratedSinglePlayer;
        gameplay.safewalkReleaseDelayMs = activeFeatures.safewalkReleaseDelayMs;
        gameplay.safewalkEdgeSensitivity = activeFeatures.safewalkEdgeSensitivity;
        gameplay.safewalkMinimumPitch = activeFeatures.safewalkMinimumPitch;
        gameplay.flySpeedPercent = activeFeatures.flySpeedPercent;
        gameplay.bhopAirSpeedPercent = activeFeatures.bhopAirSpeedPercent;
        gameplay.aimSlowdownPercent = activeFeatures.aimSlowdownPercent;
        gameplay.aimSpeedPercent = activeFeatures.aimSpeedPercent;
        gameplay.aimAttackCps = activeFeatures.aimAttackCps;
        gameplay.aimMinimumDistance = activeFeatures.aimMinimumDistance;
        gameplay.aimMaximumDistance = activeFeatures.aimMaximumDistance;
        gameplay.aimFovDegrees = activeFeatures.aimFovDegrees;
        gameplay.longJumpSpeedPercent = activeFeatures.longJumpSpeedPercent;
        gameplay.localMobReach = activeFeatures.localMobReach;
        gameplay.localAttackDelayMs = activeFeatures.localAttackDelayMs;
        gameplay.localVelocityPercent = activeFeatures.localVelocityPercent;
        gameplay.localVelocityProbability = activeFeatures.localVelocityProbability;
        gameplay.localVelocityVerticalPercent =
            activeFeatures.localVelocityVerticalPercent;
        // Sample the view that produced this frame before Aim Assist writes
        // the next frame's angles. This avoids a one-frame path/camera mismatch.
        m_bindings->sampleBow(env, snapshot,
            activeFeatures.bowPredictionEnabled && gameplayInput);
        (void)m_bindings->updateGameplay(env, gameplay, snapshot,
                                         tickMilliseconds);
        snapshot.aimTargetEntityId=m_bindings->aimTargetId();
        snapshot.aimAttackTargetEntityId=m_bindings->aimAttackTargetId();
        snapshot.silentAimAvailable=m_bindings->silentAvailable();
    }
    const std::uint32_t currentBlacklistRevision =
        m_blacklistRevision.load(std::memory_order_acquire);
    if (currentBlacklistRevision != m_runtimeBlacklistRevision) {
        ::AcquireSRWLockShared(&m_blacklistLock);
        m_runtimeBlacklistSnapshot = m_blacklistSnapshot;
        ::ReleaseSRWLockShared(&m_blacklistLock);
        m_runtimeBlacklistRevision = currentBlacklistRevision;
    }
    if (!snapshot.matchActive) {
        m_blacklistChatWarnedCount = 0U;
    } else if (m_runtimeBlacklistSnapshot.matchAlertsEnabled) {
        for (std::uint32_t entryIndex = 0U;
             entryIndex < m_runtimeBlacklistSnapshot.count; ++entryIndex) {
            const BlacklistEntry& entry =
                m_runtimeBlacklistSnapshot.entries[entryIndex];
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
            for (std::uint32_t index = 0U;
                 index < m_blacklistChatWarnedCount; ++index) {
                if (::_stricmp(m_blacklistChatWarnedKeys[index].data(),
                               entry.key.data()) == 0) {
                    alreadyWarned = true;
                    break;
                }
            }
            if (alreadyWarned) continue;
            if (m_blacklistChatWarnedCount <
                m_blacklistChatWarnedKeys.size()) {
                auto& key = m_blacklistChatWarnedKeys[
                    m_blacklistChatWarnedCount++];
                std::snprintf(key.data(), key.size(), "%s", entry.key.data());
            }
            m_bindings->enqueueWarningChatLine(entry.name.data(),
                                                entry.reason.data());
        }
    }
    if (env != nullptr)
        m_bindings->publishDebugChat(env, activeFeatures.debugChatEnabled);
    if (m_visible.load(std::memory_order_acquire)) {
        m_renderer->setGuiScaleIndex(m_guiScaleIndex.load(std::memory_order_acquire));
        m_renderer->setFeatureSettings(activeFeatures);
        ::AcquireSRWLockShared(&m_hypixelLock);
        const HypixelOverlaySnapshot hypixel = m_hypixelSnapshot;
        ::ReleaseSRWLockShared(&m_hypixelLock);
        m_renderer->setHypixelSnapshot(hypixel);
        ::AcquireSRWLockShared(&m_mediaLock);
        const MediaPlaybackSnapshot media = m_mediaSnapshot;
        const MediaOverlaySettings mediaSettings = m_mediaSettings;
        ::ReleaseSRWLockShared(&m_mediaLock);
        m_renderer->setMediaSnapshot(media);
        m_renderer->setMediaSettings(mediaSettings);
        PlayerStatsOverlaySnapshot playerStats{};
        ::AcquireSRWLockShared(&m_playerStatsLock);
        for (const auto& [name, stats] : m_playerStats) {
            (void)name;
            if (playerStats.count >= playerStats.entries.size()) break;
            playerStats.entries[playerStats.count++] = stats;
        }
        ::ReleaseSRWLockShared(&m_playerStatsLock);
        m_renderer->setPlayerStatsSnapshot(playerStats);
        const std::uint32_t blacklistRevision =
            m_blacklistRevision.load(std::memory_order_acquire);
        if (blacklistRevision != m_renderBlacklistRevision) {
            BlacklistOverlaySnapshot blacklist{};
            ::AcquireSRWLockShared(&m_blacklistLock);
            blacklist = m_blacklistSnapshot;
            ::ReleaseSRWLockShared(&m_blacklistLock);
            m_renderer->setBlacklistSnapshot(blacklist);
            m_renderBlacklistRevision = blacklistRevision;
        }
        (void)m_renderer->render(deviceContext, snapshot,
                                 m_interactive.load(std::memory_order_acquire));
        FeatureSettings changedFeatures{};
        if (m_renderer->consumeFeatureSettings(changedFeatures)) {
            queueFeatureChanged(changedFeatures);
        }
        std::array<char, 17U> query{};
        if (m_renderer->consumeHypixelQuery(query)) {
            queueHypixelQuery(query);
        }
        BlacklistAction blacklistAction{};
        if (m_renderer->consumeBlacklistAction(blacklistAction))
            queueBlacklistAction(blacklistAction);
        unsigned changedHotkey = 0U;
        if (m_renderer->consumeMenuHotkeyChange(changedHotkey)) {
            queueMenuHotkeyChanged(changedHotkey);
        }
        int changedScale = 0;
        if (m_renderer->consumeGuiScaleChange(changedScale)) {
            queueGuiScaleChanged(changedScale);
        }
        MediaOverlaySettings changedMediaSettings{};
        if (m_renderer->consumeMediaSettings(changedMediaSettings)) {
            ::AcquireSRWLockExclusive(&m_mediaLock);
            m_mediaSettings = changedMediaSettings;
            ::ReleaseSRWLockExclusive(&m_mediaLock);
            queueMediaSettingsChanged(changedMediaSettings);
        }
        // None is the renderer's empty slot, not a transport command. Writing
        // it every frame used to race the telemetry thread and overwrite a
        // real Previous/Next/Toggle before that action reached the Controller.
        const MediaAction mediaAction = m_renderer->consumeMediaAction();
        if (mediaAction != MediaAction::None) queueMediaAction(mediaAction);
        if (m_renderer->consumeBedRescanRequest()) {
            m_bindings->requestBedRescan();
        }
        if (m_renderer->initialized() && m_handshakeSent.load(std::memory_order_acquire) &&
            !m_rendererReadySent.load(std::memory_order_acquire)) {
            queueRendererReady();
        }
    }

    queueTelemetry(snapshot, tickMilliseconds);
}

} // namespace mcoverlay
