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

namespace {
struct LogicalMovementHookContext final {
    jint entityId = -1;
    jfloat originalYaw = 0.0F;
    jfloat mappedForward = 0.0F;
    bool sprintChanged = false;
    bool applied = false;
};
thread_local LogicalMovementHookContext g_logicalMovementHook{};
struct LogicalJumpHookContext final {
    jint entityId=-1;
    jfloat originalYaw=0.0F;
    bool sprintChanged=false;
    bool applied=false;
};
thread_local LogicalJumpHookContext g_logicalJumpHook{};
} // namespace

jfloat GameBindings::beginLogicalMovement(JNIEnv* env,jobject entity,
                                          const jfloat strafe,
                                          const jfloat forward) noexcept
{
    g_logicalMovementHook={};
    g_logicalMovementHook.mappedForward=forward;
    if(!env || !entity) return strafe;
    const auto* c=m_cache.get();
    if(!c || !c->getEntityId || !c->rotationYaw) return strafe;
    const jint entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return strafe; }
    if(entityId!=m_logicalController.localPlayerId()) return strafe;
    const auto tick=c->entityTicks ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks)) : 0U;
    if(env->ExceptionCheck()) { clearException(env); return strafe; }
    // The values arriving here are the inputs Minecraft will use in this exact
    // moveFlying invocation.  Resolving from them (instead of a render-frame
    // key snapshot) makes MovementCoordinator the authoritative source for the
    // current movement computation, including low-TPS/high-FPS timing gaps.
    m_logicalController.beginPhysicsTick(tick);
    refreshAttackAtPublication(env);
    const silent::MovementCommand command=m_logicalController.movementCommand(
        static_cast<double>(strafe),static_cast<double>(forward),tick);
    if(c->isSprinting&&c->motionFields[0]&&c->motionFields[2]) {
        const bool sprinting=env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
        const double x=env->GetDoubleField(entity,c->motionFields[0]);
        const double z=env->GetDoubleField(entity,c->motionFields[2]);
        if(!env->ExceptionCheck()) {
            char detail[180]{};
            std::snprintf(detail,sizeof(detail),
                "tick=%llu sprinting=%d owner=%u motion=(%.7f,%.7f)",
                static_cast<unsigned long long>(tick),sprinting?1:0,
                static_cast<unsigned>(m_sprintOwner.load(std::memory_order_acquire)),x,z);
            m_logicalController.debug().event("MOVE_FLYING",
                m_logicalController.latest(),detail);
        }
        clearException(env);
    }
    if(!command.enabled || entityId!=m_logicalController.localPlayerId()) return strafe;
    g_logicalMovementHook.entityId=entityId;
    g_logicalMovementHook.originalYaw=env->GetFloatField(entity,c->rotationYaw);
    g_logicalMovementHook.mappedForward=static_cast<jfloat>(
        command.forward);
    env->SetFloatField(entity,c->rotationYaw,
                       static_cast<jfloat>(command.logicalRotation.yaw));
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        env->SetFloatField(entity,c->rotationYaw,
                           g_logicalMovementHook.originalYaw);
        clearException(env); g_logicalMovementHook={};
        g_logicalMovementHook.mappedForward=forward; return strafe;
    }
    g_logicalMovementHook.applied=true;
    return static_cast<jfloat>(command.strafe);
}

jfloat GameBindings::logicalMovementForward(JNIEnv*,jobject,
                                            const jfloat fallback) noexcept
{
    return g_logicalMovementHook.applied
        ? g_logicalMovementHook.mappedForward : fallback;
}

void GameBindings::endLogicalMovement(JNIEnv* env,jobject entity) noexcept
{
    if(!env || !entity) return;
    const auto* c=m_cache.get();
    if(c && c->getEntityId) {
        const jint entityId=env->CallIntMethod(entity,c->getEntityId);
        const auto tick=c->entityTicks&&env->ExceptionCheck()==JNI_FALSE
            ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks))
            : 0U;
        if(env->ExceptionCheck()==JNI_FALSE&&
           entityId==m_logicalController.localPlayerId())
            m_logicalController.endMovementPhase(tick);
        if(g_logicalMovementHook.applied&&c->rotationYaw&&
           env->ExceptionCheck()==JNI_FALSE&&
           entityId==g_logicalMovementHook.entityId) {
            env->SetFloatField(entity,c->rotationYaw,
                               g_logicalMovementHook.originalYaw);
        }
    }
    clearException(env);
    g_logicalMovementHook={};
}

bool GameBindings::arbitrateLogicalSprint(JNIEnv* env,jobject entity,
                                          const bool requested) noexcept
{
    if(!env||!entity) return requested;
    const auto* c=m_cache.get();
    if(!c||!c->getEntityId) return requested;
    const jint entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        return requested;
    }
    if(entityId!=m_logicalController.localPlayerId()) return requested;
    const auto tick=c->entityTicks
        ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks))
        : 0U;
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        return requested;
    }
    const SprintOwner owner=syncSprintOwner();
    const bool allowed=requested&&owner!=SprintOwner::SilentCombat;
    char detail[136]{};
    std::snprintf(detail,sizeof(detail),
        "tick=%llu requested=%d owner=%u veto=%d result=%d",
        static_cast<unsigned long long>(tick),requested?1:0,
        static_cast<unsigned>(owner),requested&&!allowed?1:0,allowed?1:0);
    m_logicalController.debug().event("SPRINT_SET",
        m_logicalController.latest(),detail,requested&&!allowed);
    return allowed;
}

GameBindings::SprintOwner GameBindings::syncSprintOwner() noexcept
{
    const bool leftHeld=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    const SprintOwner desired=m_sprintFeatureEnabled.load(std::memory_order_acquire)&&
        leftHeld&&m_logicalController.active()
            ?SprintOwner::SilentCombat:SprintOwner::Vanilla;
    const SprintOwner previous=m_sprintOwner.exchange(desired,std::memory_order_acq_rel);
    if(previous!=desired) {
        char detail[80]{};
        std::snprintf(detail,sizeof(detail),"old=%u new=%u leftHeld=%d",
            static_cast<unsigned>(previous),static_cast<unsigned>(desired),leftHeld?1:0);
        m_logicalController.debug().event("SPRINT_OWNER",
            m_logicalController.latest(),detail,true);
    }
    return desired;
}

void GameBindings::beginLogicalHeading(JNIEnv* env,jobject entity) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c||!c->getEntityId||!c->isSprinting||
       !c->setSprinting) return;
    const int entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()||entityId!=m_logicalController.localPlayerId()) {
        clearException(env);return;
    }
    const auto tick=c->entityTicks
        ?static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks)):0U;
    const SprintOwner owner=syncSprintOwner();
    const bool tracing=m_logicalController.debug().enabled();
    const bool before=tracing&&
        env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    if(env->ExceptionCheck()) {clearException(env);return;}
    if(owner==SprintOwner::SilentCombat)
        env->CallVoidMethod(entity,c->setSprinting,JNI_FALSE);
    if(env->ExceptionCheck()) {clearException(env);return;}
    if(!tracing) return;
    const bool after=env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    const float speed=c->getAIMoveSpeed
        ?env->CallFloatMethod(entity,c->getAIMoveSpeed):0.0F;
    if(env->ExceptionCheck()) {clearException(env);return;}
    char detail[160]{};
    std::snprintf(detail,sizeof(detail),
        "tick=%llu owner=%u sca=%d actualBefore=%d actualAfter=%d",
        static_cast<unsigned long long>(tick),static_cast<unsigned>(owner),
        m_sprintFeatureEnabled.load(std::memory_order_acquire)?1:0,
        before?1:0,after?1:0);
    m_logicalController.debug().event("SPRINT_PRE",
        m_logicalController.latest(),detail);
    std::snprintf(detail,sizeof(detail),
        "tick=%llu sprinting=%d moveSpeed=%.7f",
        static_cast<unsigned long long>(tick),after?1:0,speed);
    m_logicalController.debug().event("MOVE_HEADING_PRE",
        m_logicalController.latest(),detail);
}

void GameBindings::endLogicalHeading(JNIEnv* env,jobject entity) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c||!c->getEntityId||!c->isSprinting||
       !c->motionFields[0]||!c->motionFields[2]) return;
    const int entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()||entityId!=m_logicalController.localPlayerId()) {
        clearException(env);return;
    }
    const auto tick=c->entityTicks
        ?static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks)):0U;
    const bool sprinting=env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    const double x=env->GetDoubleField(entity,c->motionFields[0]);
    const double z=env->GetDoubleField(entity,c->motionFields[2]);
    if(env->ExceptionCheck()) {clearException(env);return;}
    char detail[170]{};
    std::snprintf(detail,sizeof(detail),
        "tick=%llu sprinting=%d owner=%u motion=(%.7f,%.7f)",
        static_cast<unsigned long long>(tick),sprinting?1:0,
        static_cast<unsigned>(m_sprintOwner.load(std::memory_order_acquire)),x,z);
    m_logicalController.debug().event("MOVE_HEADING_POST",
        m_logicalController.latest(),detail);
}

void GameBindings::beginLogicalJump(JNIEnv* env,jobject entity) noexcept
{
    g_logicalJumpHook={};
    if(!env||!entity) return;
    const auto* c=m_cache.get();
    if(!c||!c->getEntityId||!c->rotationYaw||!c->isSprinting||
       !c->setSprinting||
       std::any_of(c->movementInputFields.begin(),c->movementInputFields.end(),
                   [](jfieldID field){return field==nullptr;})) return;
    const jint entityId=env->CallIntMethod(entity,c->getEntityId);
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return;}
    if(entityId!=m_logicalController.localPlayerId()) return;
    const bool physicalSprinting=
        env->CallBooleanMethod(entity,c->isSprinting)==JNI_TRUE;
    // EntityLivingBase scales both action-state axes by 0.98F between jump()
    // and moveFlying(). Snapshot the current fields now and use the exact
    // values the subsequent movement hook will receive.
    constexpr jfloat VanillaTravelScale=0.98F;
    const jfloat physicalStrafe=
        env->GetFloatField(entity,c->movementInputFields[0])*VanillaTravelScale;
    const jfloat physicalForward=
        env->GetFloatField(entity,c->movementInputFields[1])*VanillaTravelScale;
    const auto tick=c->entityTicks
        ? static_cast<std::uint64_t>(env->GetIntField(entity,c->entityTicks))
        : 0U;
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return;}
    m_logicalController.beginPhysicsTick(tick);
    refreshAttackAtPublication(env);
    const silent::MovementCommand command=m_logicalController.jumpCommand(
        physicalStrafe,physicalForward,physicalSprinting,tick);
    if(!command.enabled) return;
    g_logicalJumpHook.entityId=entityId;
    g_logicalJumpHook.originalYaw=env->GetFloatField(entity,c->rotationYaw);
    env->SetFloatField(entity,c->rotationYaw,
                       static_cast<jfloat>(command.logicalRotation.yaw));
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env);
        env->SetFloatField(entity,c->rotationYaw,g_logicalJumpHook.originalYaw);
        clearException(env);g_logicalJumpHook={};return;
    }
    g_logicalJumpHook.applied=true;
}

void GameBindings::endLogicalJump(JNIEnv* env,jobject entity) noexcept
{
    if(!env||!entity||!g_logicalJumpHook.applied) return;
    const auto* c=m_cache.get();
    if(c&&c->getEntityId&&c->rotationYaw) {
        const jint entityId=env->CallIntMethod(entity,c->getEntityId);
        if(env->ExceptionCheck()==JNI_FALSE&&
           entityId==g_logicalJumpHook.entityId) {
            env->SetFloatField(entity,c->rotationYaw,
                               g_logicalJumpHook.originalYaw);
        }
    }
    clearException(env);
    g_logicalJumpHook={};
}


} // namespace mcoverlay
