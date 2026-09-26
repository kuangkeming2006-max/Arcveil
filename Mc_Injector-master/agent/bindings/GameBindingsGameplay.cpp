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

bool GameBindings::updateGameplay(JNIEnv* const env,
                                  const GameplaySettings& requested,
                                  const GameSnapshot& snapshot,
                                  const std::uint64_t tickMilliseconds) noexcept
{
    m_logicalController.debug().configure(requested.silentFileDebug,requested.silentChatDebug);
    if(!m_logicalController.debug().enabled() && m_interactionObserver.ready())
        m_interactionObserver.setEnabled(false);
    if (env == nullptr) return false;
    if(!requested.aimAssist||!requested.aimSilentLock||
       !requested.silentControlAdaptation) {
        m_sprintFeatureEnabled.store(false,std::memory_order_release);
        m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
    }

    BindingCache* const cache =
        m_resolutionPhase.load(std::memory_order_acquire) == ResolutionPhase::Resolved
        ? m_cache.get() : nullptr;
    if(m_hotbarPausePhase!=HotbarPausePhase::None&&
       (!requested.smartHotbar||!cache||gameScreenOpen(env)))
        restoreHotbarMovement(env);
    // These two diagnostics are intentionally impossible to activate on a
    // remote server. The guard is duplicated here (below the UI/runtime
    // guard) so a malformed IPC frame still cannot broaden their scope.
    const bool localWorld = snapshot.integratedSinglePlayer &&
        !snapshot.hypixelServer;
    const bool localMobAuraRequested = requested.localMobAura && localWorld;
    const bool localVelocityRequested = requested.localVelocity && localWorld;
    const bool shield=requested.shieldAttackerId==-2||
        (localWorld&&requested.shieldAttackerId>=0);
    m_shieldAttacker.store(shield?requested.shieldAttackerId:-1,std::memory_order_release);
    m_shieldLocalPlayer.store(shield?snapshot.entityId:-1,std::memory_order_release);
    m_impulseHook.setEnabled(shield);
    m_velocityHook.setEnabled(shield&&requested.shieldAttackerId==-2);
    const bool aimCapability = cache != nullptr &&
        cache->minecraftClass != nullptr && cache->isMainThread != nullptr &&
        cache->playerField != nullptr && cache->gameSettingsField != nullptr &&
        cache->mouseSensitivity != nullptr && cache->rotationYaw != nullptr &&
        cache->rotationPitch != nullptr && cache->previousRotationYaw != nullptr &&
        cache->previousRotationPitch != nullptr;
    const bool freeLookCapability=aimCapability&&
        cache->entityRendererClass!=nullptr&&cache->thirdPersonView!=nullptr&&
        cache->updateCameraAndRender!=nullptr&&cache->orientCamera!=nullptr&&
        cache->setAngles!=nullptr&&cache->profile!=nullptr;
    const int freeLookHotkey=std::clamp(requested.freeLookHotkey,0,254);
    const bool freeLookHeld=freeLookHotkey>=8&&freeLookHotkey<=254&&
        (::GetAsyncKeyState(freeLookHotkey)&0x8000)!=0;
    const bool freeLookEnableEdge=requested.freeLookConfigured&&
        (!m_freeLookObservationInitialized||!m_freeLookUiRequestedObserved);
    const bool freeLookCapabilityRecovered=freeLookCapability&&
        m_freeLookObservationInitialized&&!m_freeLookCapabilityObserved;
    if(freeLookEnableEdge||freeLookCapabilityRecovered) {
        m_freeLookHookAttemptCount=0U;
        m_freeLookHookRetryLatched=false;
        m_nextFreeLookHookAttemptTick=0U;
        m_freeLookDiagnostics.event("HOOK_RETRY_RESET",
            freeLookEnableEdge?"reason=explicit-enable":"reason=capability-recovered");
    } else if(!requested.freeLookConfigured) {
        m_freeLookHookAttemptCount=0U;
        m_freeLookHookRetryLatched=false;
        m_nextFreeLookHookAttemptTick=0U;
    }
    m_freeLookDiagnostics.setVerbose(requested.silentFileDebug);
    m_freeLookHotkey.store(freeLookHotkey,
                           std::memory_order_release);
    if(!m_freeLookObservationInitialized||
       requested.freeLookConfigured!=m_freeLookUiRequestedObserved||
       requested.freeLook!=m_freeLookRuntimeRequestedObserved||
       requested.freeLookGuiOpen!=m_freeLookGuiObserved||
       requested.freeLookForeground!=m_freeLookForegroundObserved||
       freeLookCapability!=m_freeLookCapabilityObserved||
       m_freeLookHook.ready()!=m_freeLookReadyObserved||
       freeLookHeld!=m_freeLookHeldObserved) {
        char detail[320]{};
        std::snprintf(detail,sizeof(detail),
            "uiRequested=%d runtimeRequested=%d hotkey=%d held=%d foreground=%d guiOpen=%d capability=%d ready=%d",
            requested.freeLookConfigured?1:0,requested.freeLook?1:0,
            freeLookHotkey,freeLookHeld?1:0,
            requested.freeLookForeground?1:0,requested.freeLookGuiOpen?1:0,
            freeLookCapability?1:0,m_freeLookHook.ready()?1:0);
        m_freeLookDiagnostics.event("REQUEST_STATE",detail);
        if(!m_freeLookObservationInitialized||
           freeLookCapability!=m_freeLookCapabilityObserved) {
            char capability[512]{};
            std::snprintf(capability,sizeof(capability),
                "resolved=%d profile=%s minecraft=%d mainThread=%d player=%d settings=%d yaw=%d pitch=%d prevYaw=%d prevPitch=%d renderer=%d perspective=%d update=%d orient=%d setAngles=%d terrain=%d",
                freeLookCapability?1:0,
                cache&&cache->profile?cache->profile->label.c_str():"unresolved",
                cache&&cache->minecraftClass?1:0,cache&&cache->isMainThread?1:0,
                cache&&cache->playerField?1:0,cache&&cache->gameSettingsField?1:0,
                cache&&cache->rotationYaw?1:0,cache&&cache->rotationPitch?1:0,
                cache&&cache->previousRotationYaw?1:0,
                cache&&cache->previousRotationPitch?1:0,
                cache&&cache->entityRendererClass?1:0,
                cache&&cache->thirdPersonView?1:0,
                cache&&cache->updateCameraAndRender?1:0,
                cache&&cache->orientCamera?1:0,cache&&cache->setAngles?1:0,
                cache&&cache->setupTerrain?1:0);
            m_freeLookDiagnostics.event("CAPABILITY",capability);
        }
        m_freeLookObservationInitialized=true;
        m_freeLookUiRequestedObserved=requested.freeLookConfigured;
        m_freeLookRuntimeRequestedObserved=requested.freeLook;
        m_freeLookGuiObserved=requested.freeLookGuiOpen;
        m_freeLookForegroundObserved=requested.freeLookForeground;
        m_freeLookCapabilityObserved=freeLookCapability;
        m_freeLookReadyObserved=m_freeLookHook.ready();
        m_freeLookHeldObserved=freeLookHeld;
    }
    if(requested.freeLook&&freeLookCapability&&!m_freeLookHook.ready()&&
       !m_freeLookHookRetryLatched&&
       tickMilliseconds>=m_nextFreeLookHookAttemptTick) {
        const std::uint64_t attemptStarted=::GetTickCount64();
        ++m_freeLookHookAttemptCount;
        char retry[192]{};
        std::snprintf(retry,sizeof(retry),"tickMs=%llu attempt=%u profile=%s",
            static_cast<unsigned long long>(tickMilliseconds),
            static_cast<unsigned>(m_freeLookHookAttemptCount),
            cache->profile->label.c_str());
        m_freeLookDiagnostics.event("HOOK_INSTALL_BEGIN",retry);
        std::string entityOwner=cache->profile->entityName;
        std::replace(entityOwner.begin(),entityOwner.end(),'.','/');
        const std::array<const char*,4> fields{
            cache->profile->rotationYawField.c_str(),
            cache->profile->rotationPitchField.c_str(),
            cache->profile->previousRotationYawField.c_str(),
            cache->profile->previousRotationPitchField.c_str()};
        const bool installed=m_freeLookHook.install(m_vm,cache->updateCameraAndRender,
            cache->orientCamera,cache->setAngles,cache->setupTerrain,
            entityOwner.c_str(),
            cache->profile->setAngles.c_str(),fields,this,
            [](void* owner,JNIEnv* jni,jobject entity,jfloat yaw,
               jfloat pitch) noexcept {
                static_cast<GameBindings*>(owner)->rotateFreeLookCamera(
                    jni,entity,yaw,pitch);
            },
            [](void* owner,JNIEnv* jni,jobject entity,
               LiveFreeLookTransform::Angle angle) noexcept -> jfloat {
                return static_cast<GameBindings*>(owner)->freeLookCameraAngle(
                    jni,entity,angle);
            },
            [](void* owner,const char* event,const char* detail) noexcept {
                static_cast<GameBindings*>(owner)->m_freeLookDiagnostics.event(
                    event?event:"HOOK_EVENT",detail?detail:"");
            });
        const std::uint64_t duration=::GetTickCount64()-attemptStarted;
        char result[224]{};
        std::snprintf(result,sizeof(result),
            "attempt=%u success=%d terrainReady=%d durationMs=%llu lastJvmtiError=%d",
            static_cast<unsigned>(m_freeLookHookAttemptCount),installed?1:0,
            m_freeLookHook.terrainReady()?1:0,
            static_cast<unsigned long long>(duration),m_freeLookHook.lastError());
        m_freeLookDiagnostics.event("HOOK_INSTALL_END",result);
        if(m_freeLookHook.ready()) {
            m_freeLookHookAttemptCount=0U;
            m_nextFreeLookHookAttemptTick=0U;
        } else if(m_freeLookHookAttemptCount>=3U) {
            m_freeLookHookRetryLatched=true;
            m_freeLookDiagnostics.event("HOOK_RETRY_LATCHED",
                "retry requires explicit re-enable or capability recovery");
        } else {
            const std::uint64_t backoff=1500ULL<<
                static_cast<unsigned>(m_freeLookHookAttemptCount-1U);
            m_nextFreeLookHookAttemptTick=tickMilliseconds+backoff;
        }
        if(m_freeLookHook.ready()!=m_freeLookReadyObserved) {
            char ready[96]{};
            std::snprintf(ready,sizeof(ready),"ready=%d lastJvmtiError=%d",
                m_freeLookHook.ready()?1:0,m_freeLookHook.lastError());
            m_freeLookDiagnostics.event("READY_CHANGE",ready);
            m_freeLookReadyObserved=m_freeLookHook.ready();
        }
    }
    const bool freeLookRequested=requested.freeLook&&freeLookCapability&&
        m_freeLookHook.ready();
    m_freeLookRequested.store(freeLookRequested,std::memory_order_release);
    const bool freeLookNotReady=requested.freeLookConfigured&&
        !m_freeLookHook.ready();
    if(freeLookNotReady&&!m_freeLookNotReadyObserved)
        m_freeLookDiagnostics.event("REQUEST_NOT_READY",
            freeLookCapability?"hook install incomplete":"mapping capability incomplete");
    m_freeLookNotReadyObserved=freeLookNotReady;
    if(!freeLookRequested&&m_freeLookActive) {
        const char* reason=!requested.freeLookConfigured?"disabled":
            requested.freeLookGuiOpen?"gui-open":
            !requested.freeLookForeground?"window-unfocused":
            !freeLookCapability?"capability-lost":"hook-not-ready";
        endFreeLook(env,reason,true);
    }
    const bool safewalkCapability = cache != nullptr &&
        cache->gameSettingsClass != nullptr && cache->keyBindingClass != nullptr &&
        cache->gameSettingsField != nullptr && cache->keyBindSneakField != nullptr &&
        cache->getKeyCode != nullptr && cache->setKeyBindState != nullptr &&
        cache->rotationPitch != nullptr && cache->isAirBlock != nullptr;
    const bool logicalMovementCapability = cache != nullptr &&
        cache->moveFlying != nullptr && cache->isSprinting != nullptr &&
        cache->setSprinting != nullptr &&
        cache->jump != nullptr &&
        cache->getEntityId != nullptr && cache->getKeyCode != nullptr &&
        cache->onGround != nullptr && cache->entityTicks != nullptr &&
        std::all_of(cache->movementInputFields.begin(),
                    cache->movementInputFields.end(),
                    [](jfieldID field) { return field != nullptr; }) &&
        std::all_of(cache->movementKeyFields.begin(),
                    cache->movementKeyFields.begin() + 4,
                    [](jfieldID field) { return field != nullptr; }) &&
        std::all_of(cache->motionFields.begin(), cache->motionFields.end(),
                    [](jfieldID field) { return field != nullptr; });

    const auto setSneakState = [&](const int keyCode, const bool down) noexcept {
        if (!safewalkCapability || keyCode <= 0) return false;
        env->CallStaticVoidMethod(cache->keyBindingClass,
                                  cache->setKeyBindState,
                                  static_cast<jint>(keyCode),
                                  down ? JNI_TRUE : JNI_FALSE);
        const bool succeeded = env->ExceptionCheck() != JNI_TRUE;
        clearException(env);
        return succeeded;
    };
    const auto releaseForcedSneak = [&]() noexcept {
        if (!m_safewalkSneakForced) return true;
        bool physicalDown = false;
        (void)queryLwjglKeyDown(env, m_safewalkSneakKeyCode, physicalDown);
        const bool released = setSneakState(m_safewalkSneakKeyCode,
                                            physicalDown);
        m_safewalkSneakForced = false;
        m_safewalkSneakKeyCode = 0;
        m_safewalkSupportMask = 0U;
        m_safewalkReleaseAt = 0U;
        return released;
    };
    const auto releaseForcedSprint = [&]() noexcept {
        if(!m_sprintKeyForced) return;
        bool physicalDown=false;
        (void)queryLwjglKeyDown(env,m_sprintKeyCode,physicalDown);
        if(cache->keyBindingClass&&cache->setKeyBindState&&m_sprintKeyCode>0)
            env->CallStaticVoidMethod(cache->keyBindingClass,
                cache->setKeyBindState,m_sprintKeyCode,
                physicalDown?JNI_TRUE:JNI_FALSE);
        clearException(env);
        m_sprintKeyForced=false;m_sprintKeyCode=0;
    };

    const bool movementCapability = safewalkCapability &&
        std::all_of(cache->movementKeyFields.begin(), cache->movementKeyFields.end(),
                    [](jfieldID field) { return field != nullptr; }) &&
        std::all_of(cache->motionFields.begin(), cache->motionFields.end(),
                    [](jfieldID field) { return field != nullptr; }) &&
        cache->mouseSensitivity != nullptr && cache->rotationYaw != nullptr &&
        cache->onGround != nullptr && cache->jump != nullptr;
    const bool placementCapability = movementCapability &&
        cache->playerControllerClass != nullptr && cache->itemBlockClass != nullptr &&
        cache->enumFacingClass != nullptr && cache->vec3Class != nullptr &&
        cache->inventoryField != nullptr && cache->onPlayerRightClick != nullptr &&
        cache->currentItem != nullptr && cache->mainInventory != nullptr &&
        cache->getItem != nullptr && cache->getBlockFromItem != nullptr &&
        cache->getIdFromBlock != nullptr && cache->getFacingByIndex != nullptr &&
        cache->vec3Constructor != nullptr && cache->playerControllerField != nullptr &&
        cache->syncCurrentPlayItem != nullptr;
    const bool bedBreakerCapability = cache != nullptr &&
        cache->playerControllerClass != nullptr && cache->itemStackClass != nullptr &&
        cache->enumFacingClass != nullptr && cache->playerControllerField != nullptr &&
        cache->inventoryField != nullptr && cache->currentItem != nullptr &&
        cache->mainInventory != nullptr && cache->getItem != nullptr &&
        cache->getFacingByIndex != nullptr && cache->clickBlock != nullptr &&
        cache->onPlayerDamageBlock != nullptr &&
        cache->resetBlockRemoving != nullptr &&
        cache->getBlockReachDistance != nullptr && cache->getStrVsBlock != nullptr &&
        cache->blockPosConstructor != nullptr && cache->getBlockState != nullptr &&
        cache->getBlock != nullptr && cache->isAirBlock != nullptr;
    const bool smartHotbarCapability=aimCapability&&cache!=nullptr&&
        cache->keyBindingClass!=nullptr&&cache->keyBindsHotbar!=nullptr&&
        cache->keyBindingIsPressed!=nullptr&&cache->getKeyCode!=nullptr&&cache->inventoryField!=nullptr&&
        cache->currentItem!=nullptr&&cache->mainInventory!=nullptr&&
        cache->getItem!=nullptr&&cache->itemSwordClass!=nullptr&&
        cache->itemBlockClass!=nullptr&&cache->playerControllerField!=nullptr&&
        cache->windowClick!=nullptr&&cache->syncCurrentPlayItem!=nullptr;
    const bool smartHotbarRequested=requested.smartHotbar&&
        smartHotbarCapability;
    m_smartHotbarConfig.store(hotbar::pack(smartHotbarRequested,
        requested.smartHotbarActions),std::memory_order_release);
    m_smartHotbarHook.setEnabled(smartHotbarRequested);
    m_refillEnabled.store(smartHotbarRequested&&requested.smartHotbarRefill,
        std::memory_order_release);
    m_itemUseHook.setEnabled(smartHotbarRequested&&requested.smartHotbarRefill);
    if(!smartHotbarRequested) {
        restoreHotbarMovement(env);
        m_smartHotbarRequest.store(0,std::memory_order_release);
        m_smartHotbarRefillRequest.store(0,std::memory_order_release);
        m_refillSlot=-1;
    }
    const bool bedBreakerRequested = requested.bedBreaker && localWorld;
    const bool movementRequested = requested.safewalk || requested.scaffold ||
        requested.fly || requested.bhop || requested.longJump ||
        localMobAuraRequested || localVelocityRequested || requested.forceSprint || shield;
    // A pending logical restore/reset is work in its own right.  Keep this
    // frame alive even after the feature toggle turns off so the authoritative
    // controller can flush the real Minecraft state before the hooks stand
    // down.
    const bool anyRequested = movementRequested || requested.aimAssist ||
        requested.silentFileDebug || requested.silentChatDebug ||
        bedBreakerRequested || m_bedBreakerTargetValid ||
        m_logicalController.requiresDrain() || freeLookRequested ||
        m_freeLookActive || smartHotbarRequested;
    if ((!anyRequested && !m_aimSensitivityModified) ||
        (!aimCapability && !movementCapability && !bedBreakerCapability &&
         !freeLookCapability && !smartHotbarCapability)) {
        if(!aimCapability||!anyRequested) deactivateSilentOutput();
        (void)releaseForcedSneak();
        releaseForcedSprint();
        m_scaffoldPlatformYValid = false;
        m_lastLocalHealth = -1.0F;
        m_lastLocalEntityId = -1;
        return false;
    }
    if (env->PushLocalFrame(96) < 0) {
        clearException(env);
        (void)releaseForcedSneak();
        releaseForcedSprint();
        return false;
    }
    const auto finish = [&](const bool result) noexcept {
        env->PopLocalFrame(nullptr);
        return result;
    };
    const auto fail = [&]() noexcept {
        deactivateSilentOutput();
        clearException(env);
        restoreHotbarMovement(env);
        m_sprintFeatureEnabled.store(false,std::memory_order_release);
        m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
        (void)releaseForcedSneak();
        releaseForcedSprint();
        m_logicalController.deactivate();
        m_bedBreakerTargetValid = false;
        return finish(false);
    };

    jobject minecraft = cache->minecraftInstanceField != nullptr
        ? env->GetStaticObjectField(cache->minecraftClass,
                                    cache->minecraftInstanceField)
        : env->CallStaticObjectMethod(cache->minecraftClass,
                                      cache->getMinecraft);
    if (env->ExceptionCheck() == JNI_TRUE || minecraft == nullptr) return fail();
    const jboolean mainThread = env->CallBooleanMethod(minecraft,
                                                       cache->isMainThread);
    if (env->ExceptionCheck() == JNI_TRUE || mainThread != JNI_TRUE) return fail();

    jobject player = env->GetObjectField(minecraft, cache->playerField);
    jobject settings = env->GetObjectField(minecraft, cache->gameSettingsField);
    jobject world = env->GetObjectField(minecraft, cache->worldField);
    if (env->ExceptionCheck() == JNI_TRUE || player == nullptr ||
        settings == nullptr || world == nullptr) return fail();
    // Ask vanilla's input path to sprint. Writing setSprinting(true) from
    // moveFlying occurs after the movement-speed decision and produces a
    // client/server mismatch, especially on diagonal input.
    if(requested.forceSprint&&cache->keyBindSprintField&&
       cache->setKeyBindState&&cache->getKeyCode) {
        jobject sprintBinding=env->GetObjectField(settings,cache->keyBindSprintField);
        jobject forwardBinding=cache->movementKeyFields[0]
            ?env->GetObjectField(settings,cache->movementKeyFields[0]):nullptr;
        if(sprintBinding&&forwardBinding&&!env->ExceptionCheck()) {
            const int sprintCode=env->CallIntMethod(sprintBinding,cache->getKeyCode);
            const int forwardCode=env->CallIntMethod(forwardBinding,cache->getKeyCode);
            bool forwardDown=false;
            if(!env->ExceptionCheck()&&
               queryMinecraftBindingDown(env,forwardCode,forwardDown)) {
                const bool silentHeld=requested.aimAssist&&requested.aimSilentLock&&
                    (::GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
                const bool desired=forwardDown&&!silentHeld&&
                    m_hotbarPausePhase==HotbarPausePhase::None;
                if(desired&&sprintCode>0) {
                    if(m_sprintKeyForced&&m_sprintKeyCode!=sprintCode)
                        releaseForcedSprint();
                    env->CallStaticVoidMethod(cache->keyBindingClass,
                        cache->setKeyBindState,sprintCode,JNI_TRUE);
                    if(!env->ExceptionCheck()) {
                        m_sprintKeyForced=true;m_sprintKeyCode=sprintCode;
                    }
                } else releaseForcedSprint();
            }
        }
        clearException(env);
    } else releaseForcedSprint();
    const jfloat pitch = env->GetFloatField(player, cache->rotationPitch);
    if(shield&&cache->knockBack&&(!m_impulseHook.ready()||
       (requested.shieldAttackerId==-2&&!m_velocityHook.ready()))&&tickMilliseconds>=m_nextImpulseHookAttempt) {
        m_nextImpulseHookAttempt=tickMilliseconds+5000U;
        (void)m_impulseHook.install(m_vm,cache->knockBack,this,
            [](void* owner,JNIEnv* jni,jobject victim,jobject source) noexcept {
                return static_cast<GameBindings*>(owner)->suppressKnownImpulse(jni,victim,source);
            });
        m_impulseHook.setEnabled(true);
        if(requested.shieldAttackerId==-2&&cache->handleEntityVelocity&&cache->velocityEntityId) {
            (void)m_velocityHook.install(m_vm,cache->handleEntityVelocity,this,
                [](void* owner,JNIEnv* jni,jobject,jobject packet) noexcept {
                    auto* bindings=static_cast<GameBindings*>(owner);
                    const auto* resolved=bindings->m_cache.get();
                    if(!packet||!resolved||bindings->m_shieldAttacker.load(std::memory_order_acquire)!=-2) return false;
                    const int id=jni->CallIntMethod(packet,resolved->velocityEntityId);
                    if(jni->ExceptionCheck()) {bindings->clearException(jni);return false;}
                    return id==bindings->m_shieldLocalPlayer.load(std::memory_order_acquire);
                });
            m_velocityHook.setEnabled(true);
        }
    }
    const jfloat yaw = env->GetFloatField(player, cache->rotationYaw);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();

    if(smartHotbarRequested&&(!m_smartHotbarHook.ready()||
       (requested.smartHotbarRefill&&!m_itemUseHook.ready()))&&
       cache->keyBindingIsPressed&&tickMilliseconds>=m_nextSmartHotbarHookAttemptTick) {
        m_nextSmartHotbarHookAttemptTick=tickMilliseconds+5000U;
        (void)m_smartHotbarHook.install(m_vm,cache->keyBindingIsPressed,this,
            [](void* owner,JNIEnv* jni,jobject binding) noexcept {
                return static_cast<GameBindings*>(owner)->consumeSmartHotbarPress(jni,binding);
            });
        m_smartHotbarHook.setEnabled(true);
        if(requested.smartHotbarRefill&&cache->rightClickMouse) {
            (void)m_itemUseHook.install(m_vm,cache->rightClickMouse,this,
                [](void* owner,JNIEnv* jni,jobject mc,bool entering) noexcept {
                    return static_cast<GameBindings*>(owner)->onItemUse(jni,mc,entering);
                });
            m_itemUseHook.setEnabled(true);
        }
    }

    silent::HeldItemPolicy heldItemPolicy=silent::HeldItemPolicy::Other;
    if(cache->getEquipmentInSlot&&cache->getItem) {
        jobject heldStack=env->CallObjectMethod(player,cache->getEquipmentInSlot,0);
        jobject heldItem=!env->ExceptionCheck()&&heldStack
            ?env->CallObjectMethod(heldStack,cache->getItem):nullptr;
        if(!env->ExceptionCheck()&&heldItem) {
            if(cache->itemBlockClass&&env->IsInstanceOf(
                    heldItem,cache->itemBlockClass)==JNI_TRUE) {
                heldItemPolicy=silent::HeldItemPolicy::BlockItem;
            } else if(cache->itemClass&&cache->getIdFromItem) {
                const jint id=env->CallStaticIntMethod(
                    cache->itemClass,cache->getIdFromItem,heldItem);
                // Legacy 1.8.9 IDs: pickaxes, axes and shears. These tools
                // deliberately give the camera block priority on left-click.
                constexpr std::array<int,11U> miningTools{{
                    257,258,270,271,274,275,278,279,285,286,359}};
                if(!env->ExceptionCheck()&&std::find(miningTools.begin(),
                        miningTools.end(),static_cast<int>(id))!=miningTools.end())
                    heldItemPolicy=silent::HeldItemPolicy::MiningTool;
            }
        }
        clearException(env);
        if(heldItem) env->DeleteLocalRef(heldItem);
        if(heldStack) env->DeleteLocalRef(heldStack);
    }

    if (localVelocityRequested && movementCapability) {
        const jfloat localHealth = env->CallFloatMethod(player, cache->getHealth);
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        if (m_lastLocalEntityId == snapshot.entityId &&
            m_lastLocalHealth >= 0.0F &&
            localHealth + 0.01F < m_lastLocalHealth) {
            const unsigned probability=static_cast<unsigned>(std::clamp(
                requested.localVelocityProbability,0,100));
            const unsigned roll=static_cast<unsigned>((tickMilliseconds ^
                (static_cast<std::uint64_t>(snapshot.entityId)*0x9E3779B97F4A7C15ULL))%100ULL);
            const double horizontalScale = static_cast<double>(std::clamp(
                requested.localVelocityPercent, 0, 100)) / 100.0;
            const double verticalScale=static_cast<double>(std::clamp(
                requested.localVelocityVerticalPercent,0,100))/100.0;
            for (std::size_t axis=0;axis<cache->motionFields.size();++axis) {
                const jfieldID field=cache->motionFields[axis];
                const jdouble motion = env->GetDoubleField(player, field);
                if(roll<probability)
                    env->SetDoubleField(player,field,motion*
                        (axis==1U ? verticalScale : horizontalScale));
            }
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
        }
        m_lastLocalHealth = localHealth;
        m_lastLocalEntityId = snapshot.entityId;
    } else {
        m_lastLocalHealth = -1.0F;
        m_lastLocalEntityId = snapshot.entityId;
    }

    // Silent Lock is a Lock On output mode even if an older persisted config
    // contains the contradictory Smooth+Silent combination.  Do not let a UI
    // state mismatch silently disable the authoritative logical pipeline.
    const aim::Mode mode = (requested.aimLockOnMode || requested.aimSilentLock)
        ? aim::Mode::LockOn : aim::Mode::Smooth;
    const bool wantsSilent = requested.aimAssist && requested.aimSilentLock;
    if (wantsSilent || requested.forceSprint || smartHotbarRequested ||
        m_logicalController.debug().enabled()) {
        if (!m_silentRotationHook.ready() &&
            tickMilliseconds >= m_nextSilentRotationHookAttemptTick) {
            m_nextSilentRotationHookAttemptTick = tickMilliseconds + 5000U;
            if (cache->addToSendQueue && cache->profile) {
                std::string packetInternal = cache->profile->networkPacketName;
                std::replace(packetInternal.begin(), packetInternal.end(), '.', '/');
                (void)m_silentRotationHook.install(
                    m_vm, cache->addToSendQueue, packetInternal.c_str(), this,
                    [](void* owner, JNIEnv* jni, jobject packet) noexcept -> jobject {
                        return static_cast<GameBindings*>(owner)->serializeLogicalPacket(jni, packet);
                    });
            }
        }
        if ((!m_logicalMovementHook.ready()||!m_logicalJumpHook.ready()) &&
            logicalMovementCapability &&
            tickMilliseconds >= m_nextLogicalMovementHookAttemptTick) {
            m_nextLogicalMovementHookAttemptTick = tickMilliseconds + 5000U;
            if(!m_logicalMovementHook.ready()) {
                (void)m_logicalMovementHook.install(
                    m_vm, cache->moveFlying,cache->setSprinting,this,
                    [](void* owner, JNIEnv* jni, jobject entity, jfloat strafe,
                       jfloat forward) noexcept -> jfloat {
                        return static_cast<GameBindings*>(owner)->beginLogicalMovement(
                            jni, entity, strafe, forward);
                    },
                    [](void* owner, JNIEnv* jni, jobject entity,
                       jfloat fallback) noexcept -> jfloat {
                        return static_cast<GameBindings*>(owner)->logicalMovementForward(
                            jni, entity, fallback);
                    },
                    [](void* owner, JNIEnv* jni, jobject entity) noexcept {
                        static_cast<GameBindings*>(owner)->endLogicalMovement(jni, entity);
                    },
                    [](void* owner,JNIEnv* jni,jobject entity,
                       bool sprintRequested) noexcept -> bool {
                        return static_cast<GameBindings*>(owner)->arbitrateLogicalSprint(
                            jni,entity,sprintRequested);
                    });
            }
            if(!m_logicalJumpHook.ready()) {
                (void)m_logicalJumpHook.install(
                    m_vm,cache->jump,this,
                    [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                        static_cast<GameBindings*>(owner)->beginLogicalJump(jni,entity);
                    },
                    [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                        static_cast<GameBindings*>(owner)->endLogicalJump(jni,entity);
                    });
            }
        }
        if(wantsSilent&&requested.silentControlAdaptation&&
           !m_headingHook.ready()&&cache->moveEntityWithHeading&&
           tickMilliseconds>=m_nextHeadingHookAttemptTick) {
            m_nextHeadingHookAttemptTick=tickMilliseconds+5000U;
            const bool installed=m_headingHook.install(
                m_vm,cache->moveEntityWithHeading,this,
                [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                    static_cast<GameBindings*>(owner)->beginLogicalHeading(jni,entity);
                },
                [](void* owner,JNIEnv* jni,jobject entity) noexcept {
                    static_cast<GameBindings*>(owner)->endLogicalHeading(jni,entity);
                });
            char detail[96]{};
            std::snprintf(detail,sizeof(detail),"ready=%d jvmtiError=%d",
                installed?1:0,m_headingHook.lastError());
            m_logicalController.debug().event("HEADING_HOOK",
                m_logicalController.latest(),detail,true);
            log::info(installed?"Heading PRE/POST hook installed.":
                "Heading PRE/POST hook unavailable; SprintOwner remains Vanilla.");
        }
        if(wantsSilent&&requested.silentControlAdaptation&&
           !cache->moveEntityWithHeading&&!m_headingMappingMissingLogged) {
            m_headingMappingMissingLogged=true;
            m_logicalController.debug().event("HEADING_HOOK",
                m_logicalController.latest(),
                "ready=0 reason=moveEntityWithHeading_mapping_missing",true);
            log::info("Heading PRE/POST mapping unavailable; SprintOwner remains Vanilla.");
        }
        if (!m_logicalInteractionHook.ready() && cache->sendClickBlock &&
            tickMilliseconds >= m_nextLogicalInteractionHookAttemptTick) {
            m_nextLogicalInteractionHookAttemptTick = tickMilliseconds + 5000U;
            // Lunar's transformed clickMouse is not a reliable retransformation
            // target. Hook the per-tick held-left entry and let it consume only
            // intents already released by the fixed-CPS monotonic scheduler.
            (void)m_logicalInteractionHook.install(
                m_vm, nullptr, cache->sendClickBlock, this,
                [](void* owner, JNIEnv* jni, jobject mc,
                   LiveInteractionTransform::Entry entry, bool down) noexcept {
                    return static_cast<GameBindings*>(owner)->consumeLogicalInteraction(
                        jni, mc, entry, down);
                });
        }
        if(!m_attackOwnershipHook.ready()&&cache->attackEntity&&
           tickMilliseconds>=m_nextAttackOwnershipHookAttemptTick) {
            m_nextAttackOwnershipHookAttemptTick=tickMilliseconds+5000U;
            (void)m_attackOwnershipHook.install(m_vm,cache->attackEntity,this,
                [](void* owner,JNIEnv* jni,jobject original) noexcept -> jobject {
                    return static_cast<GameBindings*>(owner)->arbitrateLogicalAttack(
                        jni,original);
                });
        }
    }

    silent::MovementInput physicalMovement{};
    if(m_logicalController.debug().enabled() && !m_interactionObserver.ready() &&
       tickMilliseconds>=m_nextInteractionObserverAttempt) {
        m_nextInteractionObserverAttempt=tickMilliseconds+5000;
        const bool installed=m_interactionObserver.install(m_vm,
            {cache->attackEntity,cache->clickBlock,cache->onPlayerDamageBlock,cache->resetBlockRemoving},
            this,[](void* owner,JNIEnv* jni,LiveInteractionObserver::Event event,jobject argument) noexcept {
                static_cast<GameBindings*>(owner)->observeActualInteraction(jni,event,argument);
            });
        char detail[80]{}; std::snprintf(detail,sizeof(detail),"ready=%d jvmtiError=%d",installed,m_interactionObserver.error());
        m_logicalController.debug().event("ACTUAL_HOOK",m_logicalController.latest(),detail,true);
    } else if(m_interactionObserver.ready()) {
        m_interactionObserver.setEnabled(m_logicalController.debug().enabled());
    }
    silent::Vec3 currentVelocity{};
    bool logicalOnGround = false;
    bool logicalSprinting = false;
    std::uint64_t logicalPhysicsTick = 0U;
    if (logicalMovementCapability) {
        std::array<bool, 4U> keys{};
        for (std::size_t index = 0U; index < keys.size(); ++index) {
            jobject binding = env->GetObjectField(settings,
                                                  cache->movementKeyFields[index]);
            if (!binding || env->ExceptionCheck() == JNI_TRUE) return fail();
            const jint code = env->CallIntMethod(binding, cache->getKeyCode);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            (void)queryLwjglKeyDown(env, code, keys[index]);
        }
        physicalMovement = {keys[0], keys[1], keys[2], keys[3]};
        currentVelocity = {
            env->GetDoubleField(player, cache->motionFields[0]),
            env->GetDoubleField(player, cache->motionFields[1]),
            env->GetDoubleField(player, cache->motionFields[2])};
        logicalOnGround = env->GetBooleanField(player, cache->onGround) == JNI_TRUE;
        logicalSprinting = env->CallBooleanMethod(player, cache->isSprinting) == JNI_TRUE;
        logicalPhysicsTick=cache->entityTicks
            ? static_cast<std::uint64_t>(env->GetIntField(player,cache->entityTicks))
            : 0U;
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
    }

    std::array<silent::TargetCandidate, GameSnapshot::MaxEntityMarkers> candidates{};
    std::size_t candidateCount = 0U;
    std::uint32_t playerMarkers = 0U;
    std::uint32_t tabConfirmedMarkers = 0U;
    std::uint32_t colouredPlayerMarkers = 0U;
    std::uint32_t teammateMarkers = 0U;
    silent::Vec3 combatEye{};
    const bool combatEyeReady=readCombatEye(env,player,combatEye);
    const bool leftHeld=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    (void)observeLogicalCamera(env,minecraft,leftHeld);
    const auto previousCombat=m_logicalController.latest();
    const auto observedAttackTick=m_lastAttackEntryTick.load(std::memory_order_acquire);
    const int observedAttackId=m_lastAttackEntryEntity.load(std::memory_order_acquire);
    const int diagnosticId=previousCombat.cameraMouseOverEntityId>=0
        ? previousCombat.cameraMouseOverEntityId
        : (observedAttackId>=0&&tickMilliseconds>=observedAttackTick&&
           tickMilliseconds-observedAttackTick<=1500U ? observedAttackId:-1);
    struct TargetDiagnostic final {
        int id=-1;
        bool markerFound=false,markerPlayer=false,confirmedPlayer=false;
        bool livePlayer=false;
        float health=-1.0F;
        bool self=false,teammate=false,lookup=false,boundsReady=false;
        bool candidateAdded=false,withinFov=false,withinPreAim=false;
        bool attackAvailable=false;
        double aimPointDistance=-1.0,aabbEntryDistance=-1.0,angle=-1.0;
    } targetDiag{};
    targetDiag.id=diagnosticId;
    const double configuredMaximum=std::clamp(requested.aimMaximumDistance,
        std::max(1,requested.aimMinimumDistance),128);
    const double preAimRange=requested.aimAttackViability
        ?std::min(3.5,configuredMaximum):configuredMaximum;
    const double attackReach=requested.aimAttackViability
        ?std::min(3.0,configuredMaximum):configuredMaximum;
    const auto identityHash = [](const EntityMarker& marker) noexcept {
        std::uint64_t value = 1469598103934665603ULL;
        const auto append = [&](const auto& text) noexcept {
            for (const char character : text) {
                if (!character) break;
                value ^= static_cast<unsigned char>(character);
                value *= 1099511628211ULL;
            }
        };
        if (marker.uuid[0]) append(marker.uuid);
        else append(marker.playerName);
        return value;
    };
    for (std::uint32_t index = 0U;
         index < std::min<std::uint32_t>(snapshot.entityMarkerCount,
             static_cast<std::uint32_t>(snapshot.entityMarkers.size())); ++index) {
        const EntityMarker& entity = snapshot.entityMarkers[index];
        const bool diagnostic=entity.entityId==diagnosticId;
        if(diagnostic) {
            targetDiag.markerFound=true;
            targetDiag.markerPlayer=entity.player;
            targetDiag.confirmedPlayer=entity.confirmedPlayer;
            targetDiag.health=entity.health;
            targetDiag.self=entity.entityId==snapshot.entityId;
        }
        if (entity.player) ++playerMarkers;
        if (entity.confirmedPlayer) ++tabConfirmedMarkers;
        const bool colouredPlayer = entity.player && entity.teamColor != 'u' &&
            entity.teamColor != '\0';
        if (colouredPlayer) ++colouredPlayerMarkers;
        if (colouredPlayer && snapshot.ownTeam != 'u' &&
            entity.teamColor == snapshot.ownTeam) ++teammateMarkers;
        // Hypixel may expose a nicked player's world-entity alias that differs
        // from the chosen TAB nickname.  TAB/UUID confirmation remains the
        // strongest signal, while a real player entity wearing a recognised
        // Bed Wars team colour is the safe in-match fallback. Shop NPCs and
        // opening robots have no recognised team colour and stay excluded.
        const bool validPlayer = entity.confirmedPlayer ||
            (snapshot.matchActive && colouredPlayer) ||
            (!snapshot.hypixelServer && entity.player);
        // Outside Hypixel Bed Wars, scoreboard/text colours are commonly ranks
        // or chat decoration rather than team identity (especially on
        // offline-mode servers). Applying the Bed Wars team filter globally
        // made only same-colour players mysteriously untargetable.
        const bool confirmedTeammate=silent::isConfirmedCombatTeammate(
            snapshot.hypixelServer,snapshot.matchActive,
            snapshot.ownTeam,entity.teamColor);
        if(diagnostic) targetDiag.teammate=confirmedTeammate;
        const bool eligible = validPlayer && entity.entityId != snapshot.entityId &&
            std::isfinite(entity.health) && entity.health > 0.0F &&
            !confirmedTeammate;
        if (!eligible || candidateCount >= candidates.size()) continue;
        if(!combatEyeReady||!cache->getEntityById) continue;
        jobject liveTarget=env->CallObjectMethod(world,cache->getEntityById,
                                                entity.entityId);
        if(diagnostic) {
            targetDiag.lookup=liveTarget&&env->ExceptionCheck()==JNI_FALSE;
            targetDiag.livePlayer=targetDiag.lookup&&cache->playerClass&&
                env->IsInstanceOf(liveTarget,cache->playerClass)==JNI_TRUE;
        }
        silent::Bounds bounds{};
        const bool boundsReady=liveTarget&&env->ExceptionCheck()==JNI_FALSE&&
            readCombatBounds(env,liveTarget,bounds);
        if(diagnostic) targetDiag.boundsReady=boundsReady;
        if(liveTarget) env->DeleteLocalRef(liveTarget);
        if(env->ExceptionCheck()==JNI_TRUE) clearException(env);
        if(!boundsReady) continue;
        const auto reference=previousCombat.candidateTargetId==entity.entityId
            ? previousCombat.logical : aim::Angles{yaw,pitch};
        const auto aimPoint=silent::chooseCombatAimPoint(combatEye,bounds,
            reference,attackReach,requested.aimAttackViability,
            [&](const silent::Vec3 direction,const double limit) noexcept {
                silent::LogicalFramePlan ray{};
                ray.rayOrigin=combatEye;ray.rayDirection=direction;ray.rayLimit=limit;
                return traceLogicalBlock(env,world,ray);
            });
        if(diagnostic) {
            const double dx=aimPoint.point.x-combatEye.x;
            const double dy=aimPoint.point.y-combatEye.y;
            const double dz=aimPoint.point.z-combatEye.z;
            const double horizontal=std::hypot(dx,dz);
            targetDiag.aimPointDistance=std::hypot(horizontal,dy);
            if(targetDiag.aimPointDistance>1.0e-6&&
               std::isfinite(targetDiag.aimPointDistance)) {
                const silent::Vec3 direction{dx/targetDiag.aimPointDistance,
                    dy/targetDiag.aimPointDistance,dz/targetDiag.aimPointDistance};
                targetDiag.aabbEntryDistance=silent::RayTraceCoordinator::intersect(
                    combatEye,direction,bounds,std::max(preAimRange,attackReach));
                constexpr double degrees=180.0/3.14159265358979323846;
                const aim::Angles desired{std::atan2(dz,dx)*degrees-90.0,
                    -std::atan2(dy,horizontal)*degrees};
                targetDiag.angle=std::hypot(aim::wrap(desired.yaw-yaw),desired.pitch-pitch);
                targetDiag.withinFov=targetDiag.angle<=
                    std::clamp(requested.aimFovDegrees,1,360)*0.5+1.0e-5;
                const silent::TargetCandidate distanceCandidate{entity.entityId,0,aimPoint.point,bounds};
                targetDiag.withinPreAim=silent::targetSelectionDistance(
                    distanceCandidate,combatEye,requested.aimAttackViability)<=preAimRange+1.0e-5;
            }
            targetDiag.attackAvailable=!requested.aimAttackViability||aimPoint.available;
            targetDiag.candidateAdded=true;
        }
        candidates[candidateCount++] = {
            entity.entityId,identityHash(entity),aimPoint.point,bounds,true,
            requested.aimSequentialTargets&&entity.hurtTime>0,
            !requested.aimAttackViability||aimPoint.available};
    }

    // If the camera/vanilla attack path can resolve an entity that was absent
    // from the marker pipeline, probe it directly so TARGET_DIAG identifies
    // marker classification versus world lookup/bounds failures.
    if(diagnosticId>=0&&!targetDiag.lookup&&cache->getEntityById) {
        jobject entity=env->CallObjectMethod(world,cache->getEntityById,diagnosticId);
        if(env->ExceptionCheck()==JNI_TRUE) clearException(env);
        else if(entity) {
            targetDiag.lookup=true;
            targetDiag.livePlayer=cache->playerClass&&
                env->IsInstanceOf(entity,cache->playerClass)==JNI_TRUE;
            silent::Bounds bounds{};
            targetDiag.boundsReady=readCombatBounds(env,entity,bounds);
            if(cache->livingClass&&cache->getHealth&&
               env->IsInstanceOf(entity,cache->livingClass)==JNI_TRUE) {
                targetDiag.health=env->CallFloatMethod(entity,cache->getHealth);
                if(env->ExceptionCheck()==JNI_TRUE) clearException(env);
            }
            env->DeleteLocalRef(entity);
        }
    }

    const bool canAim = requested.aimAssist && aimCapability && combatEyeReady &&
        snapshot.state == GameSnapshot::State::Ready && snapshot.health > 0.0F &&
        std::isfinite(yaw) && std::isfinite(pitch);
    const bool sprintFeature=canAim&&wantsSilent&&requested.silentControlAdaptation&&
        m_headingHook.ready()&&m_logicalMovementHook.ready()&&
        m_logicalJumpHook.ready();
    m_sprintFeatureEnabled.store(sprintFeature,std::memory_order_release);
    const jfloat sensitivity = env->GetFloatField(settings, cache->mouseSensitivity);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();
    const bool silentRotationReady = silentAvailable();
    const bool silentAttackBindingsReady=silentAttackAvailable();
    const silent::RuntimeCapabilities silentCapabilities{
        silentRotationReady,silentAttackBindingsReady,
        m_logicalInteractionHook.ready(),m_attackOwnershipHook.ready()};
    const auto monotonicMicroseconds=static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    silent::LogicalFrameInput logicalInput{};
    logicalInput.tick = tickMilliseconds;
    logicalInput.physicsTick = logicalPhysicsTick;
    logicalInput.worldGeneration = snapshot.worldGeneration;
    logicalInput.localEntityId = snapshot.entityId;
    // Silent combat is activated only when both packet publication and its
    // stable input/PRE dispatch boundary exist. Falling back to vanilla attack
    // while a hidden rotation is active would attack a camera-ray target.
    logicalInput.enabled = canAim&&(!wantsSilent||
        silentCapabilities.attackSchedulerReady());
    logicalInput.silent = wantsSilent;
    logicalInput.mode = mode;
    logicalInput.nearestPriority = requested.aimNearestPriority;
    logicalInput.enforceAttackAvailability = requested.aimAttackViability;
    // Movement ownership is an explicit user policy. It is deliberately
    // independent from ray/attack availability so losing an executable hit can
    // never cause a one-tick movement or sprint direction pulse.
    logicalInput.coordinateMovement = requested.silentControlAdaptation;
    logicalInput.sequentialTargets = requested.aimSequentialTargets;
    logicalInput.leftMouseDown = leftHeld;
    logicalInput.heldItemPolicy = requested.silentControlAdaptation
        ? heldItemPolicy : silent::HeldItemPolicy::Other;
    logicalInput.minimumDistance = std::clamp(requested.aimMinimumDistance, 0, 64);
    // With availability checking enabled, 3.5 m is acquisition/pre-aim only;
    // current physics-space reach and occlusion remain a separate 3.0 m gate.
    logicalInput.maximumDistance=requested.aimAttackViability
        ? preAimRange:configuredMaximum;
    logicalInput.attackReach=requested.aimAttackViability
        ? attackReach:configuredMaximum;
    logicalInput.fovDegrees = std::clamp(requested.aimFovDegrees, 1, 360);
    logicalInput.aimSpeedPercent = requested.aimSpeedPercent;
    logicalInput.mouseSensitivity = sensitivity;
    logicalInput.camera = {yaw, pitch};
    logicalInput.eye = combatEye;
    logicalInput.physicalMovement = physicalMovement;
    logicalInput.currentVelocity = currentVelocity;
    logicalInput.sprinting = logicalSprinting;
    logicalInput.onGround = logicalOnGround;
    logicalInput.candidates = {candidates.data(), candidateCount};
    // Schedule and bind against this SAME physics snapshot. Never leave a new
    // intent waiting for the next rendered frame to choose its target/rotation.
    m_logicalController.updateAttackClock(leftHeld,
        canAim&&wantsSilent&&silentCapabilities.attackSchedulerReady(),
        monotonicMicroseconds,requested.aimAttackCps);
    const silent::LogicalFramePlan logicalPlan =
        m_logicalController.advance(logicalInput,
            [&](const silent::LogicalFramePlan& pending) noexcept {
                return traceLogicalBlock(env,world,pending);
            });
    (void)syncSprintOwner();

    if(m_logicalController.debug().enabled()&&diagnosticId>=0) {
        const bool cameraMissing=previousCombat.cameraMouseOverEntityId>=0&&
            logicalPlan.candidateTargetId<0;
        const bool force=cameraMissing&&(!m_targetDiagMissingLatched||
            m_lastTargetDiagMissingId!=diagnosticId);
        if(!cameraMissing) {
            m_targetDiagMissingLatched=false;
            m_lastTargetDiagMissingId=-1;
        } else if(force) {
            m_targetDiagMissingLatched=true;
            m_lastTargetDiagMissingId=diagnosticId;
        }
        if(force||tickMilliseconds>=m_nextTargetDiagTick) {
            m_nextTargetDiagTick=tickMilliseconds+2000U;
            const char* reject="none";
            if(!targetDiag.markerFound&&targetDiag.livePlayer)
                reject="marker_missing";
            else if(!targetDiag.markerPlayer) reject="not_player";
            else if(targetDiag.self) reject="self";
            else if(!std::isfinite(targetDiag.health)||targetDiag.health<=0.0F)
                reject="dead";
            else if(targetDiag.teammate) reject="teammate";
            else if(!targetDiag.lookup) reject="entity_lookup_failed";
            else if(!targetDiag.boundsReady) reject="bounds_unavailable";
            else if(!targetDiag.markerFound) reject="selector_not_chosen";
            else if(!targetDiag.withinPreAim) reject="outside_preaim_range";
            else if(!targetDiag.withinFov) reject="outside_fov";
            else if(requested.aimAttackViability&&!targetDiag.attackAvailable)
                reject=targetDiag.aabbEntryDistance<0.0||
                    targetDiag.aabbEntryDistance>attackReach+1.0e-5
                    ?"outside_attack_reach":"occluded";
            else if(logicalPlan.candidateTargetId!=diagnosticId)
                reject="selector_not_chosen";
            char detail[1024]{};
            std::snprintf(detail,sizeof(detail),
                "id=%d markerFound=%d markerPlayer=%d livePlayer=%d confirmedPlayer=%d health=%.2f self=%d teammate=%d getEntityById=%d boundsReady=%d candidateAdded=%d aimPointDistance=%.3f aabbEntryDistance=%.3f angle=%.2f withinFov=%d withinPreAimRange=%d attackAvailable=%d reject=%s players=%u candidateCount=%zu cameraEntity=%d selectedCandidate=%d",
                diagnosticId,targetDiag.markerFound?1:0,targetDiag.markerPlayer?1:0,
                targetDiag.livePlayer?1:0,targetDiag.confirmedPlayer?1:0,targetDiag.health,
                targetDiag.self?1:0,targetDiag.teammate?1:0,
                targetDiag.lookup?1:0,targetDiag.boundsReady?1:0,
                targetDiag.candidateAdded?1:0,targetDiag.aimPointDistance,
                targetDiag.aabbEntryDistance,targetDiag.angle,
                targetDiag.withinFov?1:0,targetDiag.withinPreAim?1:0,
                targetDiag.attackAvailable?1:0,reject,playerMarkers,candidateCount,
                previousCombat.cameraMouseOverEntityId,logicalPlan.candidateTargetId);
            m_logicalController.debug().event("TARGET_DIAG",
                m_logicalController.latest(),detail,force);
        }
    }

    if (m_logicalController.debug().enabled() &&
        tickMilliseconds >= m_nextAimCandidateDebugTick) {
        m_nextAimCandidateDebugTick = tickMilliseconds + 5000U;
        char detail[320]{};
        std::snprintf(detail, sizeof(detail),
            "markers=%u players=%u tabConfirmed=%u coloured=%u teammates=%u eligible=%zu match=%d own=%c packetReady=%d movementReady=%d interactionReady=%d requestedMode=%s effectiveMode=%s",
            snapshot.entityMarkerCount, playerMarkers, tabConfirmedMarkers,
            colouredPlayerMarkers, teammateMarkers, candidateCount,
            snapshot.matchActive ? 1 : 0,
            snapshot.ownTeam ? snapshot.ownTeam : 'u',
            silentRotationReady ? 1 : 0,
            (m_logicalMovementHook.ready()&&m_logicalJumpHook.ready()) ? 1 : 0,
            silentAttackAvailable() ? 1 : 0,
            requested.aimLockOnMode ? "lock" : "smooth",
            mode == aim::Mode::LockOn ? "lock" : "smooth");
        m_logicalController.debug().event("CANDIDATE_SCAN",
            m_logicalController.latest(), detail, candidateCount == 0U);
    }

    if (logicalPlan.interactionTransition.kind !=
            silent::InteractionCommandKind::None &&
        !executeLogicalInteraction(env, minecraft,
                                   logicalPlan.interactionTransition)) {
        m_logicalController.deferInteraction(
            logicalPlan.interactionTransition);
        return fail();
    }

    // Render/update owns preparation only. A rotation-confirmed AttackIntent is
    // deliberately left pending for the transformed Minecraft input/PRE
    // boundary; dispatching here can run after this tick's movement POST.

    if (logicalPlan.writeVisibleRotation) {
        const aim::Angles previous{
            env->GetFloatField(player, cache->previousRotationYaw),
            env->GetFloatField(player, cache->previousRotationPitch)};
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        const aim::Angles shifted = aim::shiftedPrevious(
            previous, {yaw, pitch}, logicalPlan.visibleRotation);
        env->SetFloatField(player, cache->previousRotationYaw,
                           static_cast<jfloat>(shifted.yaw));
        env->SetFloatField(player, cache->previousRotationPitch,
                           static_cast<jfloat>(shifted.pitch));
        env->SetFloatField(player, cache->rotationYaw,
                           static_cast<jfloat>(logicalPlan.visibleRotation.yaw));
        env->SetFloatField(player, cache->rotationPitch,
                           static_cast<jfloat>(logicalPlan.visibleRotation.pitch));
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
    }
    const bool logicalMovementReady=m_logicalMovementHook.ready()&&
        m_logicalJumpHook.ready();
    // Hook ownership is feature-scoped. The physics consumer decides whether
    // the current target requires a transform or a vanilla passthrough.
    const bool logicalMovementEnabled=canAim&&wantsSilent&&
        requested.silentControlAdaptation&&logicalMovementReady;
    m_logicalMovementHook.setEnabled(logicalMovementEnabled);
    m_logicalJumpHook.setEnabled(logicalMovementEnabled);
    m_headingHook.setEnabled(sprintFeature);
    const bool logicalAttackEnabled=logicalPlan.silentActive&&
        silentCapabilities.attackSchedulerReady();
    m_logicalInteractionHook.setEnabled(
        (logicalAttackEnabled&&silentCapabilities.heldArbitrationReady())||
        smartHotbarRequested);
    m_attackOwnershipHook.setEnabled(logicalAttackEnabled&&
        silentCapabilities.ownershipArbitrationReady());
    m_silentRotationHook.setEnabled(
        (logicalPlan.silentActive && silentRotationReady) ||
        m_logicalController.restoring() ||
        m_logicalController.packetContinuityRequired() ||
        m_logicalController.debug().enabled() ||
        smartHotbarRequested);

    // Aim-only operation intentionally stops here. The remainder reads block
    // support, movement keys and inventory/controller mappings.
    if ((!movementRequested || !movementCapability) &&
        ((!bedBreakerRequested && !m_bedBreakerTargetValid) ||
         !bedBreakerCapability)) {
        (void)releaseForcedSneak();
        m_scaffoldPlatformYValid = false;
        m_lastGameplayTick = tickMilliseconds;
        return finish((requested.aimAssist && aimCapability)||freeLookRequested);
    }

    if (bedBreakerRequested && bedBreakerCapability &&
        tickMilliseconds - m_lastBedBreakerTick >= 45U) {
        m_lastBedBreakerTick = tickMilliseconds;
        jobject controller = env->GetObjectField(minecraft,
                                                  cache->playerControllerField);
        jobject inventory = env->GetObjectField(player, cache->inventoryField);
        const jfloat mappedReach = controller == nullptr ? 0.0F :
            env->CallFloatMethod(controller, cache->getBlockReachDistance);
        if (env->ExceptionCheck() == JNI_TRUE || controller == nullptr ||
            inventory == nullptr || !std::isfinite(mappedReach)) return fail();
        const double reach = std::clamp(static_cast<double>(mappedReach), 2.0, 8.0);
        const double breakerEyeX = snapshot.x;
        const double breakerEyeY = snapshot.y + 1.62;
        const double breakerEyeZ = snapshot.z;

        struct BlockCoordinate final { int x=0,y=0,z=0; };
        struct PathChoice final {
            BlockCoordinate target{};
            int solidCount = std::numeric_limits<int>::max();
            double length = std::numeric_limits<double>::max();
            bool valid = false;
        } bestPath;
        const auto isOwnBed = [&](const BedMarker& bed) noexcept {
            if (!snapshot.ownBedKnown) return false;
            return bed.y == snapshot.ownBedY &&
                ((bed.x == snapshot.ownBedX && bed.z == snapshot.ownBedZ) ||
                 (bed.footX == snapshot.ownBedX && bed.footZ == snapshot.ownBedZ));
        };
        const auto sameCoordinate = [](const BlockCoordinate& a,
                                       const BlockCoordinate& b) noexcept {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        const auto blockKind = [&](const BlockCoordinate coordinate,
                                   bool& air, bool& bed) noexcept {
            jobject positionObject = env->NewObject(cache->blockPosClass,
                cache->blockPosConstructor, coordinate.x, coordinate.y, coordinate.z);
            if (positionObject == nullptr || env->ExceptionCheck() == JNI_TRUE) {
                clearException(env); air = false; bed = false; return false;
            }
            const jboolean empty = env->CallBooleanMethod(world,
                cache->isAirBlock, positionObject);
            if (env->ExceptionCheck() == JNI_TRUE) {
                clearException(env); env->DeleteLocalRef(positionObject);
                air = false; bed = false; return false;
            }
            air = empty == JNI_TRUE;
            bed = false;
            if (!air) {
                jobject state = env->CallObjectMethod(world, cache->getBlockState,
                                                       positionObject);
                jobject block = state == nullptr ? nullptr :
                    env->CallObjectMethod(state, cache->getBlock);
                if (env->ExceptionCheck() == JNI_TRUE) clearException(env);
                else bed = block != nullptr &&
                    env->IsInstanceOf(block, cache->bedClass) == JNI_TRUE;
                if (block != nullptr) env->DeleteLocalRef(block);
                if (state != nullptr) env->DeleteLocalRef(state);
            }
            env->DeleteLocalRef(positionObject);
            return true;
        };

        constexpr std::array<std::array<double, 3U>, 5U> offsets{{
            {{0.0,0.44,0.0}}, {{0.30,0.44,0.0}}, {{-0.30,0.44,0.0}},
            {{0.0,0.44,0.30}}, {{0.0,0.44,-0.30}}}};
        for (std::uint32_t bedIndex = 0U;
             bedIndex < std::min(snapshot.bedMarkerCount,
                 static_cast<std::uint32_t>(snapshot.bedMarkers.size())); ++bedIndex) {
            const BedMarker& bedMarker = snapshot.bedMarkers[bedIndex];
            if (isOwnBed(bedMarker)) continue;
            const std::array<BlockCoordinate, 2U> halves{{
                {bedMarker.x, bedMarker.y, bedMarker.z},
                {bedMarker.footX, bedMarker.y, bedMarker.footZ}}};
            for (const BlockCoordinate half : halves) {
                for (const auto& offset : offsets) {
                    const double goalX = half.x + 0.5 + offset[0];
                    const double goalY = half.y + offset[1];
                    const double goalZ = half.z + 0.5 + offset[2];
                    const double dx = goalX-breakerEyeX;
                    const double dy = goalY-breakerEyeY;
                    const double dz = goalZ-breakerEyeZ;
                    const double length = std::sqrt(dx*dx+dy*dy+dz*dz);
                    if (!std::isfinite(length) || length > reach || length < 0.2) continue;
                    const int samples = std::clamp(
                        static_cast<int>(std::ceil(length / 0.16)), 2, 64);
                    BlockCoordinate previous{std::numeric_limits<int>::min(),0,0};
                    BlockCoordinate firstSolid{};
                    int solids = 0;
                    bool reachedBed = false;
                    bool validRay = true;
                    for (int sampleIndex = 1; sampleIndex <= samples; ++sampleIndex) {
                        const double t = static_cast<double>(sampleIndex) /
                                         static_cast<double>(samples);
                        const BlockCoordinate coordinate{
                            static_cast<int>(std::floor(breakerEyeX + dx*t)),
                            static_cast<int>(std::floor(breakerEyeY + dy*t)),
                            static_cast<int>(std::floor(breakerEyeZ + dz*t))};
                        if (sameCoordinate(coordinate, previous)) continue;
                        previous = coordinate;
                        bool air = false, isBedBlock = false;
                        if (!blockKind(coordinate, air, isBedBlock)) {
                            validRay = false; break;
                        }
                        if (air) continue;
                        if (solids == 0) firstSolid = coordinate;
                        ++solids;
                        if (isBedBlock) { reachedBed = true; break; }
                    }
                    if (!validRay || !reachedBed || solids <= 0) continue;
                    if (!bestPath.valid || solids < bestPath.solidCount ||
                        (solids == bestPath.solidCount && length < bestPath.length)) {
                        bestPath = {firstSolid, solids, length, true};
                    }
                }
            }
        }

        if (!bestPath.valid) {
            if (m_bedBreakerTargetValid) {
                env->CallVoidMethod(controller, cache->resetBlockRemoving);
                clearException(env);
                m_bedBreakerTargetValid = false;
            }
        } else {
            jobject targetPosition = env->NewObject(cache->blockPosClass,
                cache->blockPosConstructor, bestPath.target.x,
                bestPath.target.y, bestPath.target.z);
            jobject targetState = targetPosition == nullptr ? nullptr :
                env->CallObjectMethod(world, cache->getBlockState, targetPosition);
            jobject targetBlock = targetState == nullptr ? nullptr :
                env->CallObjectMethod(targetState, cache->getBlock);
            jobjectArray hotbar = static_cast<jobjectArray>(env->GetObjectField(
                inventory, cache->mainInventory));
            if (env->ExceptionCheck() == JNI_TRUE || targetPosition == nullptr ||
                targetBlock == nullptr || hotbar == nullptr) return fail();
            int selectedSlot = std::clamp(static_cast<int>(
                env->GetIntField(inventory, cache->currentItem)), 0, 8);
            float bestStrength = -1.0F;
            const jsize hotbarLength = std::min<jsize>(env->GetArrayLength(hotbar), 9);
            for (jsize slot = 0; slot < hotbarLength; ++slot) {
                jobject stack = env->GetObjectArrayElement(hotbar, slot);
                if (stack == nullptr) continue;
                const jfloat strength = env->CallFloatMethod(
                    stack, cache->getStrVsBlock, targetBlock);
                if (env->ExceptionCheck() == JNI_TRUE) return fail();
                if (std::isfinite(strength) && strength > bestStrength) {
                    bestStrength = strength; selectedSlot = static_cast<int>(slot);
                }
                env->DeleteLocalRef(stack);
            }
            env->SetIntField(inventory, cache->currentItem, selectedSlot);
            const double centreX = bestPath.target.x + 0.5;
            const double centreY = bestPath.target.y + 0.5;
            const double centreZ = bestPath.target.z + 0.5;
            const double faceX = breakerEyeX-centreX;
            const double faceY = breakerEyeY-centreY;
            const double faceZ = breakerEyeZ-centreZ;
            int facingIndex = 1;
            if (std::abs(faceY) >= std::abs(faceX) &&
                std::abs(faceY) >= std::abs(faceZ)) facingIndex = faceY >= 0 ? 1 : 0;
            else if (std::abs(faceX) >= std::abs(faceZ)) facingIndex = faceX >= 0 ? 5 : 4;
            else facingIndex = faceZ >= 0 ? 3 : 2;
            jobject facing = env->CallStaticObjectMethod(cache->enumFacingClass,
                cache->getFacingByIndex, facingIndex);
            if (env->ExceptionCheck() == JNI_TRUE || facing == nullptr) return fail();
            const bool sameTarget = m_bedBreakerTargetValid &&
                m_bedBreakerTargetX == bestPath.target.x &&
                m_bedBreakerTargetY == bestPath.target.y &&
                m_bedBreakerTargetZ == bestPath.target.z;
            if (!sameTarget) {
                if (m_bedBreakerTargetValid)
                    env->CallVoidMethod(controller, cache->resetBlockRemoving);
                env->CallBooleanMethod(controller, cache->clickBlock,
                                       targetPosition, facing);
                m_bedBreakerTargetX = bestPath.target.x;
                m_bedBreakerTargetY = bestPath.target.y;
                m_bedBreakerTargetZ = bestPath.target.z;
                m_bedBreakerTargetValid = true;
            } else {
                env->CallBooleanMethod(controller, cache->onPlayerDamageBlock,
                                       targetPosition, facing);
            }
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
        }
    } else if (m_bedBreakerTargetValid && bedBreakerCapability) {
        jobject controller = env->GetObjectField(minecraft,
                                                  cache->playerControllerField);
        if (controller != nullptr)
            env->CallVoidMethod(controller, cache->resetBlockRemoving);
        clearException(env);
        m_bedBreakerTargetValid = false;
    }

    if (!movementRequested || !movementCapability) {
        (void)releaseForcedSneak();
        m_scaffoldPlatformYValid = false;
        m_lastGameplayTick = tickMilliseconds;
        return finish(bedBreakerRequested && bedBreakerCapability);
    }

    if (localMobAuraRequested && cache->playerControllerField != nullptr &&
        cache->hostileClass != nullptr &&
        cache->attackEntity != nullptr &&
        tickMilliseconds - m_lastLocalAttackTick >=
            static_cast<std::uint64_t>(std::clamp(
                requested.localAttackDelayMs, 100, 1500))) {
        const EntityMarker* nearest = nullptr;
        const double reach = static_cast<double>(std::clamp(
            requested.localMobReach, 3, 10));
        for (std::uint32_t index = 0U;
             index < snapshot.entityMarkerCount; ++index) {
            const EntityMarker& marker = snapshot.entityMarkers[index];
            if (!marker.hostile || marker.player || marker.health <= 0.0F ||
                marker.distance > reach) continue;
            if (nearest == nullptr || marker.distance < nearest->distance)
                nearest = &marker;
        }
        if (nearest != nullptr) {
            jobject loaded = cache->loadedEntitiesField != nullptr
                ? env->GetObjectField(world, cache->loadedEntitiesField)
                : env->CallObjectMethod(world, cache->getLoadedEntities);
            jobjectArray entities = loaded == nullptr ? nullptr :
                static_cast<jobjectArray>(env->CallObjectMethod(
                    loaded, cache->listToArray));
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            jobject targetObject = nullptr;
            if (entities != nullptr) {
                const jsize count = std::min<jsize>(
                    env->GetArrayLength(entities), 512);
                for (jsize index = 0; index < count; ++index) {
                    jobject candidate = env->GetObjectArrayElement(entities, index);
                    if (candidate == nullptr) continue;
                    const bool hostile = env->IsInstanceOf(
                        candidate, cache->hostileClass) == JNI_TRUE;
                    const jint id = hostile ? env->CallIntMethod(
                        candidate, cache->getEntityId) : -1;
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    if (hostile && id == nearest->entityId &&
                        env->IsInstanceOf(candidate, cache->playerClass) != JNI_TRUE) {
                        targetObject = candidate;
                        break;
                    }
                    env->DeleteLocalRef(candidate);
                }
            }
            if (targetObject != nullptr) {
                jobject controller = env->GetObjectField(
                    minecraft, cache->playerControllerField);
                if (env->ExceptionCheck() == JNI_TRUE || controller == nullptr)
                    return fail();
                env->CallVoidMethod(controller, cache->attackEntity,
                                    player, targetObject);
                if (env->ExceptionCheck() == JNI_TRUE) return fail();
                m_lastLocalAttackTick = tickMilliseconds;
            }
        }
    }
    jobject sneakBinding = env->GetObjectField(settings, cache->keyBindSneakField);
    if (env->ExceptionCheck() == JNI_TRUE || sneakBinding == nullptr) return fail();
    const jint keyCode = env->CallIntMethod(sneakBinding, cache->getKeyCode);
    jobject bounds = env->CallObjectMethod(player, cache->getBounds);
    if (env->ExceptionCheck() == JNI_TRUE || keyCode <= 0 || bounds == nullptr)
        return fail();

    const double minX = env->GetDoubleField(bounds, cache->minX);
    const double minY = env->GetDoubleField(bounds, cache->minY);
    const double minZ = env->GetDoubleField(bounds, cache->minZ);
    const double maxX = env->GetDoubleField(bounds, cache->maxX);
    const double maxZ = env->GetDoubleField(bounds, cache->maxZ);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();

    // Read physical movement once and reuse it for edge prediction, movement
    // modules and scaffold targeting. This mirrors Minecraft's movement-input
    // stage and avoids one-frame disagreement between those systems.
    std::array<bool, 6U> input{}; // forward, back, left, right, jump, sneak
    if (movementCapability) {
        for (std::size_t index = 0U; index < 5U; ++index) {
            jobject binding = env->GetObjectField(settings,
                cache->movementKeyFields[index]);
            if (env->ExceptionCheck() == JNI_TRUE || binding == nullptr) return fail();
            const jint code = env->CallIntMethod(binding, cache->getKeyCode);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            (void)queryLwjglKeyDown(env, code, input[index]);
        }
        (void)queryLwjglKeyDown(env, keyCode, input[5U]);
    }
    const bool onGround = movementCapability &&
        env->GetBooleanField(player, cache->onGround) == JNI_TRUE;
    if (env->ExceptionCheck() == JNI_TRUE) return fail();
    const double forward = (input[0U] ? 1.0 : 0.0) - (input[1U] ? 1.0 : 0.0);
    // Minecraft's positive moveStrafing direction is left. The previous
    // right-minus-left expression inverted A and D for every movement module.
    const double strafe = (input[2U] ? 1.0 : 0.0) - (input[3U] ? 1.0 : 0.0);
    const double magnitude = std::hypot(forward, strafe);
    const double normalizedForward = magnitude > 0.001 ? forward / magnitude : 0.0;
    const double normalizedStrafe = magnitude > 0.001 ? strafe / magnitude : 0.0;
    constexpr double pi = 3.14159265358979323846;
    const double radians = static_cast<double>(yaw) * pi / 180.0;
    const double directionX = -std::sin(radians) * normalizedForward +
                              std::cos(radians) * normalizedStrafe;
    const double directionZ =  std::cos(radians) * normalizedForward +
                              std::sin(radians) * normalizedStrafe;

    // Use the motion that Minecraft actually calculated for this tick.  Key
    // intent alone is insufficient while airborne (or after sprinting over a
    // diagonal edge), because inertia can carry the player somewhere that no
    // currently pressed key points at.
    const double actualMotionX = env->GetDoubleField(
        player, cache->motionFields[0U]);
    const double actualMotionY = env->GetDoubleField(
        player, cache->motionFields[1U]);
    const double actualMotionZ = env->GetDoubleField(
        player, cache->motionFields[2U]);
    if (env->ExceptionCheck() == JNI_TRUE) return fail();

    bool collisionQueryFailed = false;
    const auto hasSupport = [&](double dx, double dz, double inset) noexcept {
        if (cache->getCollidingBoxes == nullptr || cache->aabbConstructor == nullptr) {
            collisionQueryFailed = true;
            return false;
        }
        // Query real collision geometry, not "non-air": grass/water are not
        // support, whereas slabs/stairs have partial-height collision shapes.
        jobject probe = env->NewObject(cache->aabbClass, cache->aabbConstructor,
            minX + dx + inset, minY - 0.60, minZ + dz + inset,
            maxX + dx - inset, minY + 0.001, maxZ + dz - inset);
        jobject collisions = probe != nullptr && !env->ExceptionCheck()
            ? env->CallObjectMethod(world, cache->getCollidingBoxes, player, probe)
            : nullptr;
        const jint count = collisions != nullptr && !env->ExceptionCheck()
            ? env->CallIntMethod(collisions, cache->listSize) : 0;
        if (env->ExceptionCheck() || probe == nullptr || collisions == nullptr) {
            collisionQueryFailed = true;
            clearException(env);
        }
        if (collisions != nullptr) env->DeleteLocalRef(collisions);
        if (probe != nullptr) env->DeleteLocalRef(probe);
        return count > 0;
    };
    const bool guardActive = requested.safewalk && onGround && !requested.fly &&
        !input[4U]; // jumping deliberately suspends vanilla ledge protection
    const bool atEdge = guardActive && safewalk::needsSneak(
        requested.safewalkEdgeSensitivity, actualMotionX, actualMotionZ,
        directionX, directionZ, hasSupport);
    if (collisionQueryFailed) return fail();
    const bool supportRestored = guardActive && !atEdge;
    const bool pitchAllowsSafewalk = pitch >= static_cast<float>(std::clamp(
        requested.safewalkMinimumPitch, -90, 90));
    m_safewalkSupportMask = atEdge ? 0x0FU : 0U;

    if (m_safewalkSneakForced && supportRestored &&
        m_safewalkReleaseAt == 0U) {
        m_safewalkReleaseAt = tickMilliseconds + static_cast<std::uint64_t>(
            std::clamp(requested.safewalkReleaseDelayMs, 0, 750));
    }
    if (m_safewalkSneakForced && atEdge) m_safewalkReleaseAt = 0U;
    if (m_safewalkSneakForced && m_safewalkReleaseAt != 0U &&
        tickMilliseconds >= m_safewalkReleaseAt) {
        const bool released = releaseForcedSneak();
        if (!movementCapability) return finish(released);
    }

    if (!guardActive) {
        (void)releaseForcedSneak();
    } else if (!m_safewalkSneakForced && atEdge && pitchAllowsSafewalk) {
        if (setSneakState(keyCode, true)) {
            m_safewalkSneakForced = true;
            m_safewalkSneakKeyCode = keyCode;
            m_safewalkReleaseAt = 0U;
        } else return fail();
    }
    if (m_safewalkSneakForced && !pitchAllowsSafewalk) {
        const bool released = releaseForcedSneak();
        if (!movementCapability) return finish(released);
    }
    // LWJGL/KeyBinding updates may replace the synthetic state between frames.
    // Reassert it while owned; release restores the user's physical Shift key.
    if (m_safewalkSneakForced) (void)setSneakState(keyCode, true);

    if (!movementCapability) return finish(m_safewalkSneakForced);

    if (requested.fly) {
        const double speed = 0.34 * static_cast<double>(std::clamp(
            requested.flySpeedPercent, 10, 500)) / 100.0;
        env->SetDoubleField(player, cache->motionFields[0U], directionX * speed);
        env->SetDoubleField(player, cache->motionFields[2U], directionZ * speed);
        const double vertical = (input[4U] ? speed : 0.0) -
                                (input[5U] ? speed : 0.0);
        env->SetDoubleField(player, cache->motionFields[1U], vertical);
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
    } else if (requested.bhop && magnitude > 0.001) {
        const double airSpeed = 0.30 * static_cast<double>(std::clamp(
            requested.bhopAirSpeedPercent, 10, 300)) / 100.0;
        if (onGround && requested.bhopAutoJump) {
            env->CallVoidMethod(player, cache->jump);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
            // EntityLivingBase.jump applies the vanilla sprint impulse. Clamp
            // only excess speed on the landing/jump frame so Auto Jump cannot
            // create a one-tick boost, while preserving slower player motion.
            const double jumpX = env->GetDoubleField(player, cache->motionFields[0U]);
            const double jumpZ = env->GetDoubleField(player, cache->motionFields[2U]);
            const double jumpHorizontal = std::hypot(jumpX, jumpZ);
            if (jumpHorizontal > airSpeed && jumpHorizontal > 0.0001) {
                const double scale = airSpeed / jumpHorizontal;
                env->SetDoubleField(player, cache->motionFields[0U], jumpX * scale);
                env->SetDoubleField(player, cache->motionFields[2U], jumpZ * scale);
            }
        } else if (!onGround) {
            env->SetDoubleField(player, cache->motionFields[0U], directionX * airSpeed);
            env->SetDoubleField(player, cache->motionFields[2U], directionZ * airSpeed);
            if (env->ExceptionCheck() == JNI_TRUE) return fail();
        }
    } else if (requested.longJump && onGround && magnitude > 0.001 &&
               tickMilliseconds - m_lastLongJumpTick >= 650U) {
        const double speed = 0.72 * static_cast<double>(std::clamp(
            requested.longJumpSpeedPercent, 25, 250)) / 100.0;
        env->SetDoubleField(player, cache->motionFields[0U], directionX * speed);
        env->SetDoubleField(player, cache->motionFields[1U], 0.42);
        env->SetDoubleField(player, cache->motionFields[2U], directionZ * speed);
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        m_lastLongJumpTick = tickMilliseconds;
    }

    if (requested.scaffold && placementCapability &&
        tickMilliseconds - m_lastScaffoldPlacementTick >= 35U) {
        const int supportLayer = static_cast<int>(std::floor(minY - 0.06));
        if (!m_scaffoldPlatformYValid || onGround) {
            m_scaffoldPlatformY = supportLayer;
            m_scaffoldPlatformYValid = true;
        }
        jobject inventory = env->GetObjectField(player, cache->inventoryField);
        jobject controller = env->GetObjectField(minecraft,
            cache->playerControllerField);
        jobjectArray hotbar = inventory == nullptr ? nullptr :
            static_cast<jobjectArray>(env->GetObjectField(inventory,
                cache->mainInventory));
        if (env->ExceptionCheck() == JNI_TRUE) return fail();
        auto allowedBlock = [](const int id) noexcept {
            if (id == 12 || id == 13) return false; // sand / gravel fall
            switch (id) {
            case 1: case 4: case 5: case 24: case 35: case 45:
            case 87: case 98: case 121: case 159: return true;
            default: return false;
            }
        };
        int selectedSlot = -1;
        jobject selectedStack = nullptr;
        if (hotbar != nullptr && controller != nullptr) {
            const jsize length = std::min<jsize>(9, env->GetArrayLength(hotbar));
            const int current = std::clamp(
                static_cast<int>(env->GetIntField(inventory, cache->currentItem)),
                0, std::max(0, static_cast<int>(length) - 1));
            for (jsize pass = 0; pass < length; ++pass) {
                const int slot = pass == 0 ? current :
                    (static_cast<int>(pass) <= current
                        ? static_cast<int>(pass) - 1 : static_cast<int>(pass));
                jobject stack = env->GetObjectArrayElement(hotbar, slot);
                if (stack == nullptr) continue;
                jobject itemObject = env->CallObjectMethod(stack, cache->getItem);
                if (env->ExceptionCheck() == JNI_TRUE) return fail();
                if (itemObject != nullptr && env->IsInstanceOf(
                        itemObject, cache->itemBlockClass) == JNI_TRUE) {
                    jobject blockObject = env->CallObjectMethod(
                        itemObject, cache->getBlockFromItem);
                    const jint blockId = blockObject == nullptr ? -1 :
                        env->CallStaticIntMethod(cache->blockClass,
                            cache->getIdFromBlock, blockObject);
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    if (allowedBlock(blockId)) {
                        selectedSlot = slot;
                        selectedStack = stack;
                        break;
                    }
                }
            }
        }
        if (selectedSlot >= 0 && selectedStack != nullptr) {
            const double centerX = (minX + maxX) * 0.5;
            const double centerZ = (minZ + maxZ) * 0.5;
            std::array<std::array<int, 3U>, 32U> targets{};
            std::size_t targetCount = 0U;
            const auto addTargetAt = [&](const double x, const int layer,
                                         const double z) noexcept {
                const std::array<int, 3U> candidate{
                    static_cast<int>(std::floor(x)), layer,
                    static_cast<int>(std::floor(z))};
                for (std::size_t i = 0; i < targetCount; ++i)
                    if (targets[i] == candidate) return;
                if (targetCount < targets.size()) targets[targetCount++] = candidate;
            };
            // Start with the current footprint, then integrate the player's
            // real velocity through several vanilla-like air ticks. This keeps
            // diagonal sprint jumps covered even after the player releases or
            // changes a movement key mid-air.
            constexpr double cornerInset = 0.025;
            const double halfWidthX = std::max(0.0,
                (maxX - minX) * 0.5 - cornerInset);
            const double halfWidthZ = std::max(0.0,
                (maxZ - minZ) * 0.5 - cornerInset);
            const auto addFootprintAt = [&](const double x, const int layer,
                                            const double z) noexcept {
                addTargetAt(x, layer, z);
                addTargetAt(x - halfWidthX, layer, z - halfWidthZ);
                addTargetAt(x - halfWidthX, layer, z + halfWidthZ);
                addTargetAt(x + halfWidthX, layer, z - halfWidthZ);
                addTargetAt(x + halfWidthX, layer, z + halfWidthZ);
            };
            // Safety layer one: repair the cells immediately beneath the live
            // collision footprint, even with no movement key held. This is the
            // path that catches residual sprint/jump inertia and vertical jumps.
            if (!requested.scaffoldSameLayerOnly ||
                supportLayer == m_scaffoldPlatformY) {
                addFootprintAt(centerX, supportLayer, centerZ);
            }
            if (supportLayer != m_scaffoldPlatformY)
                addFootprintAt(centerX, m_scaffoldPlatformY, centerZ);

            double simulatedX = centerX;
            double simulatedY = minY;
            double simulatedZ = centerZ;
            double simulatedMotionX = actualMotionX;
            double simulatedMotionY = actualMotionY;
            double simulatedMotionZ = actualMotionZ;
            if (std::hypot(simulatedMotionX, simulatedMotionZ) < 0.012 &&
                magnitude > 0.001) {
                simulatedMotionX = directionX * 0.10;
                simulatedMotionZ = directionZ * 0.10;
            }
            for (int predictionTick = 0; predictionTick < 6; ++predictionTick) {
                simulatedX += simulatedMotionX;
                simulatedY += simulatedMotionY;
                simulatedZ += simulatedMotionZ;
                // Safety layer two: predict from actual motion, then add input
                // acceleration only as a secondary correction.
                addFootprintAt(simulatedX, m_scaffoldPlatformY, simulatedZ);

                // 1.8.x EntityLivingBase air motion approximation. Input is a
                // small acceleration/fallback; existing inertia remains the
                // dominant signal and therefore also covers jump momentum.
                if (magnitude > 0.001) {
                    simulatedMotionX += directionX * 0.012;
                    simulatedMotionZ += directionZ * 0.012;
                }
                simulatedMotionX *= 0.91;
                simulatedMotionZ *= 0.91;
                simulatedMotionY = (simulatedMotionY - 0.08) * 0.98;
                if (simulatedY <= static_cast<double>(m_scaffoldPlatformY) +
                                  1.02 && predictionTick >= 1) break;
            }
            constexpr std::array<std::array<int, 4U>, 5U> neighbours{{
                {{0,-1,0,1}}, {{0,0,-1,3}}, {{0,0,1,2}},
                {{-1,0,0,5}}, {{1,0,0,4}}}};
            bool placed = false;
            for (std::size_t targetIndex = 0U; targetIndex < targetCount; ++targetIndex) {
                const auto& target = targets[targetIndex];
                jobject targetPos = env->NewObject(cache->blockPosClass,
                    cache->blockPosConstructor, target[0U], target[1U], target[2U]);
                if (targetPos == nullptr || env->ExceptionCheck() == JNI_TRUE) return fail();
                if (env->CallBooleanMethod(world, cache->isAirBlock, targetPos) != JNI_TRUE) {
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    continue;
                }
                for (const auto& side : neighbours) {
                    jobject neighbour = env->NewObject(cache->blockPosClass,
                        cache->blockPosConstructor, target[0U] + side[0U],
                        target[1U] + side[1U], target[2U] + side[2U]);
                    if (neighbour == nullptr || env->ExceptionCheck() == JNI_TRUE) return fail();
                    const jboolean neighbourAir = env->CallBooleanMethod(
                        world, cache->isAirBlock, neighbour);
                    if (env->ExceptionCheck() == JNI_TRUE) return fail();
                    if (neighbourAir == JNI_TRUE) continue;
                    jobject face = env->CallStaticObjectMethod(cache->enumFacingClass,
                        cache->getFacingByIndex, side[3U]);
                    jobject hit = env->NewObject(cache->vec3Class,
                        cache->vec3Constructor, target[0U] + 0.5,
                        target[1U] + 0.5, target[2U] + 0.5);
                    if (face == nullptr || hit == nullptr ||
                        env->ExceptionCheck() == JNI_TRUE) return fail();
                    // Borrow the slot only for this placement attempt, even
                    // if placement is rejected or throws a JNI exception.
                    const jint previousSlot=env->GetIntField(inventory,cache->currentItem);
                    if(env->ExceptionCheck()) return fail();
                    env->SetIntField(inventory, cache->currentItem, selectedSlot);
                    if(env->ExceptionCheck()) return fail();
                    // Make the server-visible sequence explicit: held block,
                    // placement, original held slot. Some clients override the
                    // right-click method and do not perform vanilla's sync.
                    env->CallVoidMethod(controller,cache->syncCurrentPlayItem);
                    const jboolean accepted = env->ExceptionCheck() ? JNI_FALSE : env->CallBooleanMethod(controller,
                        cache->onPlayerRightClick, player, world, selectedStack,
                        neighbour, face, hit);
                    const bool placementFailed=env->ExceptionCheck()==JNI_TRUE;
                    clearException(env);
                    env->SetIntField(inventory,cache->currentItem,previousSlot);
                    if(!env->ExceptionCheck())
                        env->CallVoidMethod(controller,cache->syncCurrentPlayItem);
                    if(placementFailed||env->ExceptionCheck()) return fail();
                    if (accepted == JNI_TRUE) {
                        m_lastScaffoldPlacementTick = tickMilliseconds;
                        placed = true;
                        break;
                    }
                }
                if (placed) break;
            }
        }
    } else if (!requested.scaffold) {
        m_scaffoldPlatformYValid = false;
    }

    m_lastGameplayTick = tickMilliseconds;
    return finish(m_safewalkSneakForced || requested.scaffold || requested.fly ||
                  requested.bhop || requested.aimAssist || requested.longJump ||
                  localMobAuraRequested || localVelocityRequested ||
                  smartHotbarRequested);
}


} // namespace mcoverlay
