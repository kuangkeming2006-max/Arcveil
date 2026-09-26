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

GameBindings::GameBindings(JavaVM* const vm, jvmtiEnv* const jvmti) noexcept
    : m_vm(vm), m_jvmti(jvmti)
{
    // Auto-reset wakeup: normal chunk diffing remains asleep for 500 ms, but a
    // user refresh interrupts that wait immediately without introducing a
    // high-frequency polling loop.
    m_bedRescanEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

GameBindings::~GameBindings()
{
    if (m_bedRescanEvent != nullptr) {
        ::CloseHandle(m_bedRescanEvent);
        m_bedRescanEvent = nullptr;
    }
}

void GameBindings::clearException(JNIEnv* const env) const noexcept
{
    if (env != nullptr && env->ExceptionCheck() == JNI_TRUE) {
        env->ExceptionClear();
    }
}

void GameBindings::deleteGlobalRefs(JNIEnv* const env, BindingCache& cache) noexcept
{
    if (env == nullptr) {
        return;
    }
    if (cache.minecraftClass != nullptr) env->DeleteGlobalRef(cache.minecraftClass);
    if (cache.rayVectorClass != nullptr) env->DeleteGlobalRef(cache.rayVectorClass);
    if (cache.rayHitClass != nullptr) env->DeleteGlobalRef(cache.rayHitClass);
    if (cache.diggingPacketClass != nullptr) env->DeleteGlobalRef(cache.diggingPacketClass);
    if (cache.networkPacketClass != nullptr) env->DeleteGlobalRef(cache.networkPacketClass);
    if (cache.movementPacketClass != nullptr) env->DeleteGlobalRef(cache.movementPacketClass);
    if (cache.positionPacketClass != nullptr) env->DeleteGlobalRef(cache.positionPacketClass);
    if (cache.lookPacketClass != nullptr) env->DeleteGlobalRef(cache.lookPacketClass);
    if (cache.positionLookPacketClass != nullptr) env->DeleteGlobalRef(cache.positionLookPacketClass);
    if (cache.packetNetHandlerClass != nullptr) env->DeleteGlobalRef(cache.packetNetHandlerClass);
    cache.rayVectorClass=nullptr; cache.rayHitClass=nullptr;
    cache.networkPacketClass=nullptr; cache.movementPacketClass=nullptr;
    cache.positionPacketClass=nullptr; cache.lookPacketClass=nullptr;
    cache.positionLookPacketClass=nullptr; cache.packetNetHandlerClass=nullptr;
    if (cache.playerClass != nullptr) env->DeleteGlobalRef(cache.playerClass);
    if (cache.livingClass != nullptr) env->DeleteGlobalRef(cache.livingClass);
    if (cache.hostileClass != nullptr) env->DeleteGlobalRef(cache.hostileClass);
    if (cache.entityClass != nullptr) env->DeleteGlobalRef(cache.entityClass);
    if (cache.fireballClass != nullptr) env->DeleteGlobalRef(cache.fireballClass);
    if (cache.aabbClass != nullptr) env->DeleteGlobalRef(cache.aabbClass);
    if (cache.worldClass != nullptr) env->DeleteGlobalRef(cache.worldClass);
    if (cache.worldClientClass != nullptr) env->DeleteGlobalRef(cache.worldClientClass);
    if (cache.stateClass != nullptr) env->DeleteGlobalRef(cache.stateClass);
    if (cache.blockClass != nullptr) env->DeleteGlobalRef(cache.blockClass);
    if (cache.blockPosClass != nullptr) env->DeleteGlobalRef(cache.blockPosClass);
    if (cache.bedClass != nullptr) env->DeleteGlobalRef(cache.bedClass);
    if (cache.chunkProviderClass != nullptr) env->DeleteGlobalRef(cache.chunkProviderClass);
    if (cache.chunkClass != nullptr) env->DeleteGlobalRef(cache.chunkClass);
    if (cache.storageClass != nullptr) env->DeleteGlobalRef(cache.storageClass);
    if (cache.activeRenderInfoClass != nullptr) env->DeleteGlobalRef(cache.activeRenderInfoClass);
    if (cache.renderManagerClass != nullptr) env->DeleteGlobalRef(cache.renderManagerClass);
    if (cache.timerClass != nullptr) env->DeleteGlobalRef(cache.timerClass);
    if (cache.gameSettingsClass != nullptr)
        env->DeleteGlobalRef(cache.gameSettingsClass);
    if (cache.entityRendererClass != nullptr)
        env->DeleteGlobalRef(cache.entityRendererClass);
    if (cache.renderGlobalClass != nullptr)
        env->DeleteGlobalRef(cache.renderGlobalClass);
    if (cache.keyBindingClass != nullptr)
        env->DeleteGlobalRef(cache.keyBindingClass);
    if (cache.playerControllerClass != nullptr)
        env->DeleteGlobalRef(cache.playerControllerClass);
    if (cache.serverDataClass != nullptr)
        env->DeleteGlobalRef(cache.serverDataClass);
    if (cache.itemBlockClass != nullptr)
        env->DeleteGlobalRef(cache.itemBlockClass);
    if (cache.enumFacingClass != nullptr)
        env->DeleteGlobalRef(cache.enumFacingClass);
    if (cache.vec3Class != nullptr)
        env->DeleteGlobalRef(cache.vec3Class);
    if (cache.chatComponentClass != nullptr) env->DeleteGlobalRef(cache.chatComponentClass);
    if (cache.chatTextClass != nullptr) env->DeleteGlobalRef(cache.chatTextClass);
    if (cache.chatSerializerClass != nullptr)
        env->DeleteGlobalRef(cache.chatSerializerClass);
    if (cache.scoreboardClass != nullptr) env->DeleteGlobalRef(cache.scoreboardClass);
    if (cache.scoreObjectiveClass != nullptr) env->DeleteGlobalRef(cache.scoreObjectiveClass);
    if (cache.scoreClass != nullptr) env->DeleteGlobalRef(cache.scoreClass);
    if (cache.scorePlayerTeamClass != nullptr) env->DeleteGlobalRef(cache.scorePlayerTeamClass);
    if (cache.netHandlerClass != nullptr) env->DeleteGlobalRef(cache.netHandlerClass);
    if (cache.networkPlayerInfoClass != nullptr) env->DeleteGlobalRef(cache.networkPlayerInfoClass);
    if (cache.gameProfileClass != nullptr) env->DeleteGlobalRef(cache.gameProfileClass);
    if (cache.itemStackClass != nullptr) env->DeleteGlobalRef(cache.itemStackClass);
    if (cache.itemClass != nullptr) env->DeleteGlobalRef(cache.itemClass);
    if (cache.itemArmorClass != nullptr) env->DeleteGlobalRef(cache.itemArmorClass);
    if (cache.itemSwordClass != nullptr) env->DeleteGlobalRef(cache.itemSwordClass);
    if (cache.inventoryPlayerClass != nullptr) env->DeleteGlobalRef(cache.inventoryPlayerClass);
    if (cache.enchantmentHelperClass != nullptr)
        env->DeleteGlobalRef(cache.enchantmentHelperClass);
    if (cache.abstractClientPlayerClass != nullptr)
        env->DeleteGlobalRef(cache.abstractClientPlayerClass);
    if (cache.resourceLocationClass != nullptr)
        env->DeleteGlobalRef(cache.resourceLocationClass);
    if (cache.textureManagerClass != nullptr)
        env->DeleteGlobalRef(cache.textureManagerClass);
    if (cache.textureObjectClass != nullptr)
        env->DeleteGlobalRef(cache.textureObjectClass);
    if (cache.uuidClass != nullptr) env->DeleteGlobalRef(cache.uuidClass);
    if (cache.renderManagerObject != nullptr) env->DeleteGlobalRef(cache.renderManagerObject);
    if (cache.timerObject != nullptr) env->DeleteGlobalRef(cache.timerObject);
    if (cache.modelViewBuffer != nullptr) env->DeleteGlobalRef(cache.modelViewBuffer);
    if (cache.projectionBuffer != nullptr) env->DeleteGlobalRef(cache.projectionBuffer);
    if (cache.viewportBuffer != nullptr) env->DeleteGlobalRef(cache.viewportBuffer);

    cache.minecraftClass = nullptr;
    cache.playerClass = nullptr;
    cache.livingClass = nullptr;
    cache.hostileClass = nullptr;
    cache.entityClass = nullptr;
    cache.fireballClass = nullptr;
    cache.aabbClass = nullptr;
    cache.worldClass = nullptr;
    cache.worldClientClass = nullptr;
    cache.stateClass = nullptr;
    cache.blockClass = nullptr;
    cache.blockPosClass = nullptr;
    cache.bedClass = nullptr;
    cache.chunkProviderClass = nullptr;
    cache.chunkClass = nullptr;
    cache.storageClass = nullptr;
    cache.activeRenderInfoClass = nullptr;
    cache.renderManagerClass = nullptr;
    cache.timerClass = nullptr;
    cache.gameSettingsClass = nullptr;
    cache.entityRendererClass = nullptr;
    cache.renderGlobalClass = nullptr;
    cache.keyBindingClass = nullptr;
    cache.playerControllerClass = nullptr;
    cache.serverDataClass = nullptr;
    cache.itemBlockClass = nullptr;
    cache.enumFacingClass = nullptr;
    cache.vec3Class = nullptr;
    cache.chatComponentClass = nullptr;
    cache.chatTextClass = nullptr;
    cache.chatSerializerClass = nullptr;
    cache.scoreboardClass = nullptr;
    cache.scoreObjectiveClass = nullptr;
    cache.scoreClass = nullptr;
    cache.scorePlayerTeamClass = nullptr;
    cache.netHandlerClass = nullptr;
    cache.networkPlayerInfoClass = nullptr;
    cache.gameProfileClass = nullptr;
    cache.itemStackClass = nullptr;
    cache.itemClass = nullptr;
    cache.itemArmorClass = nullptr;
    cache.itemSwordClass = nullptr;
    cache.inventoryPlayerClass = nullptr;
    cache.enchantmentHelperClass = nullptr;
    cache.abstractClientPlayerClass = nullptr;
    cache.resourceLocationClass = nullptr;
    cache.textureManagerClass = nullptr;
    cache.textureObjectClass = nullptr;
    cache.uuidClass = nullptr;
    cache.renderManagerObject = nullptr;
    cache.timerObject = nullptr;
    cache.modelViewBuffer = nullptr;
    cache.projectionBuffer = nullptr;
    cache.viewportBuffer = nullptr;
}

void GameBindings::release(JNIEnv* const env) noexcept
{
    // Drain an authoritative block reset while the bindings and transformed
    // entry points are still valid.  Stopping the hooks first would clear only
    // native bookkeeping and could leave PlayerControllerMP mid-dig.
    if (env != nullptr &&
        (m_logicalController.requiresDrain() || m_safewalkSneakForced ||
         m_sprintKeyForced ||
         m_aimSensitivityModified)) {
        (void)updateGameplay(env, GameplaySettings{}, m_snapshot, 0U);
    }
    m_freeLookRequested.store(false,std::memory_order_release);
    endFreeLook(env,"detach",true);
    m_freeLookHook.stop();
    deactivateSilentOutput();
    m_attackOwnershipHook.stop();
    m_interactionObserver.stop();
    m_logicalInteractionHook.stop();
    m_logicalJumpHook.stop();
    m_headingHook.stop();
    m_logicalMovementHook.stop();
    m_smartHotbarHook.stop();
    restoreHotbarMovement(env);
    m_itemUseHook.stop();
    m_impulseHook.stop();
    m_velocityHook.stop();
    m_smartHotbarConfig.store(0U,std::memory_order_release);
    m_smartHotbarRequest.store(0,std::memory_order_release);
    m_smartHotbarRefillRequest.store(0,std::memory_order_release);
    m_refillSlot=-1;
    m_silentRotationHook.stop();
    m_logicalController.reset();
    // AgentRuntime guarantees the resolver has joined and all other frame
    // callbacks have drained before this method can destroy published globals.
    m_resolutionPhase.store(ResolutionPhase::Stopped, std::memory_order_release);
    if (env != nullptr && m_cache != nullptr) {
        deleteGlobalRefs(env, *m_cache);
    }
    if (env != nullptr && m_lastWorld != nullptr) {
        env->DeleteWeakGlobalRef(m_lastWorld);
    }
    m_lastWorld = nullptr;
    m_cache.reset();
    m_snapshot = {};
    m_mappingAttempt.store(0U, std::memory_order_relaxed);
    m_retryAtMilliseconds.store(0U, std::memory_order_relaxed);
    m_lastSample = 0U;
    m_entitySampleGeneration = 0U;
    m_lastPlayerScan = 0U;
    m_playerRosterGeneration = 0U;
    m_debugRosterGeneration = 0U;
    m_bedOwnershipGeneration = 0U;
    m_debugBedOwnershipGeneration = 0U;
    m_matchProbe = {};
    m_matchProbeGeneration = 0U;
    m_debugMatchProbeGeneration = 0U;
    m_sidebarCandidateTeam = bedwars::Team::Unknown;
    m_sidebarStableCount = 0U;
    m_sidebarMissingCount = 0U;
    m_matchAnchorValid = false;
    m_lockedOwnBedKnown = false;
    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
    ::AcquireSRWLockExclusive(&m_debugQueueLock);
    m_debugQueue = {};
    m_debugQueueHead = 0U;
    m_debugQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_debugQueueLock);
    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    m_warningQueue = {};
    m_warningQueueHead = 0U;
    m_warningQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);
    m_bedRescanRequested.store(false, std::memory_order_relaxed);
    ::AcquireSRWLockExclusive(&m_bedCacheLock);
    m_publishedBedCache = {};
    ::ReleaseSRWLockExclusive(&m_bedCacheLock);
    if (env != nullptr && m_lwjglMouseClass != nullptr) {
        env->DeleteGlobalRef(m_lwjglMouseClass);
    }
    m_lwjglMouseClass = nullptr;
    m_lwjglIsButtonDown = nullptr;
    m_lwjglSetGrabbed = nullptr;
    m_lwjglIsGrabbed = nullptr;
    if (env != nullptr && m_lwjglKeyboardClass != nullptr) {
        env->DeleteGlobalRef(m_lwjglKeyboardClass);
    }
    m_lwjglKeyboardClass = nullptr;
    m_lwjglIsKeyDown = nullptr;
    m_overlayInputSessionActive = false;
    m_inputGrabStateKnown = false;
    m_inputWasGrabbed = true;
    m_safewalkSneakForced = false;
    m_sprintKeyForced = false;
    m_sprintKeyCode = 0;
    m_safewalkSneakKeyCode = 0;
    m_safewalkSupportMask = 0U;
    m_safewalkReleaseAt = 0U;
    m_nextFreeLookHookAttemptTick = 0U;
    m_freeLookHookAttemptCount=0U;
    m_freeLookHookRetryLatched=false;
    m_freeLookObservationInitialized=false;
    m_nextAttackOwnershipHookAttemptTick = 0U;
    m_lastScaffoldPlacementTick = 0U;
    m_lastGameplayTick = 0U;
    m_lastBedBreakerTick = 0U;
    m_bedBreakerTargetValid = false;
    m_originalMouseSensitivity = 0.5F;
    m_aimSensitivityModified = false;
    m_freeLookDiagnostics.stop("release");
}

void GameBindings::abandon() noexcept
{
    m_freeLookRequested.store(false,std::memory_order_relaxed);
    endFreeLook(nullptr,"detach-no-jni",true);
    m_freeLookHook.abandon();
    m_freeLookEntity=nullptr;
    m_freeLookPerspectiveSaved=false;
    m_freeLookActive=false;
    m_interactionObserver.abandon();
    m_attackOwnershipHook.abandon();
    m_logicalInteractionHook.abandon();
    m_logicalJumpHook.abandon();
    m_headingHook.abandon();
    m_logicalMovementHook.abandon();
    m_sprintFeatureEnabled.store(false,std::memory_order_release);
    m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
    m_smartHotbarHook.abandon();
    m_itemUseHook.abandon();
    m_impulseHook.abandon();
    m_velocityHook.abandon();
    m_smartHotbarConfig.store(0U,std::memory_order_release);
    m_silentRotationHook.abandon();
    m_logicalController.reset();
    // Used only when the JVM is already shutting down and no JNIEnv can be
    // obtained. The VM owns and releases its reference table at process exit.
    m_resolutionPhase.store(ResolutionPhase::Stopped, std::memory_order_release);
    m_cache.reset();
    m_lastWorld = nullptr;
    m_matchAnchorValid = false;
    m_lockedOwnBedKnown = false;
    m_lockedOwnBedSource = GameSnapshot::OwnBedSource::Unknown;
    ::AcquireSRWLockExclusive(&m_warningQueueLock);
    m_warningQueue = {};
    m_warningQueueHead = 0U;
    m_warningQueueCount = 0U;
    ::ReleaseSRWLockExclusive(&m_warningQueueLock);
    m_lwjglMouseClass = nullptr;
    m_lwjglSetGrabbed = nullptr;
    m_lwjglIsGrabbed = nullptr;
    m_lwjglKeyboardClass = nullptr;
    m_lwjglIsKeyDown = nullptr;
    m_overlayInputSessionActive = false;
    m_inputGrabStateKnown = false;
    m_inputWasGrabbed = true;
    m_safewalkSneakForced = false;
    m_safewalkSneakKeyCode = 0;
    m_safewalkSupportMask = 0U;
    m_safewalkReleaseAt = 0U;
    m_nextFreeLookHookAttemptTick = 0U;
    m_freeLookHookAttemptCount=0U;
    m_freeLookHookRetryLatched=false;
    m_freeLookObservationInitialized=false;
    m_nextAttackOwnershipHookAttemptTick = 0U;
    m_lastScaffoldPlacementTick = 0U;
    m_lastGameplayTick = 0U;
    m_lastBedBreakerTick = 0U;
    m_bedBreakerTargetValid = false;
    m_originalMouseSensitivity = 0.5F;
    m_aimSensitivityModified = false;
    m_freeLookDiagnostics.stop("abandon");
}


} // namespace mcoverlay
