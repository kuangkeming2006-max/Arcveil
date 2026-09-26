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
thread_local bool g_syntheticLogicalAttack=false;
} // namespace

bool GameBindings::silentAvailable() const noexcept
{
    const auto* c=m_cache.get();
    const bool packetReady = m_silentRotationHook.ready() && c &&
        c->networkPacketClass && c->movementPacketClass &&
        c->positionPacketClass && c->lookPacketClass &&
        c->positionLookPacketClass && c->addToSendQueue &&
        c->movementPacketConstructor && c->positionPacketConstructor &&
        c->lookPacketConstructor &&
        c->positionLookPacketConstructor && c->packetYaw && c->packetPitch &&
        c->packetOnGround &&
        std::none_of(c->packetPosition.begin(),c->packetPosition.end(),
                     [](jfieldID value){ return value==nullptr; });
    // This public capability describes the feature's defining promise: can
    // logical yaw/pitch reach the outgoing C03/C05/C06 stream while the camera
    // stays untouched? Movement compensation and click redirection have their
    // own optional hooks and must never suppress target acquisition or packet
    // rotation on a transformed client.
    return packetReady;
}

bool GameBindings::silentAttackAvailable() const noexcept
{
    const auto* c=m_cache.get();
    // These JNI bindings are sufficient for the single scheduler-owned
    // game-thread dispatch path.  Held-left and lower attack retransforms only
    // arbitrate vanilla call sites when a client exposes them; making either
    // optional hook part of this capability used to disable Silent Lock
    // completely on otherwise supported clients.
    return c && c->attackEntity && c->getEntityById && c->swingItem &&
        c->playerControllerField && c->playerField && c->worldField;
}

void GameBindings::deactivateSilentOutput() noexcept
{
    m_logicalController.deactivate();
    m_sprintFeatureEnabled.store(false,std::memory_order_release);
    m_sprintOwner.store(SprintOwner::Vanilla,std::memory_order_release);
    m_silentRotationHook.setEnabled(m_logicalController.restoring()||
        m_logicalController.packetContinuityRequired());
    m_logicalMovementHook.setEnabled(false);
    m_logicalJumpHook.setEnabled(false);
    m_headingHook.setEnabled(false);
    m_attackOwnershipHook.setEnabled(false);
    m_logicalInteractionHook.setEnabled(false);
}

void GameBindings::refreshAttackAtPublication(JNIEnv* env) noexcept
{
    const auto pending=m_logicalController.pendingAttack();
    if(pending.kind==silent::InteractionCommandKind::None) return;
    const auto* c=m_cache.get();
    if(!c||env->PushLocalFrame(24)<0) {clearException(env);return;}
    struct Locals {JNIEnv* env;~Locals(){env->PopLocalFrame(nullptr);}} locals{env};
    const auto cancel=[&](const char* const reason) noexcept {
        clearException(env);
        (void)m_logicalController.cancelPendingAttack(pending,reason);
    };
    jobject mc=c->minecraftInstanceField
        ? env->GetStaticObjectField(c->minecraftClass,c->minecraftInstanceField)
        : env->CallStaticObjectMethod(c->minecraftClass,c->getMinecraft);
    if(!mc||env->ExceptionCheck()) {cancel("minecraft_unavailable");return;}
    if(env->CallBooleanMethod(mc,c->isMainThread)!=JNI_TRUE||env->ExceptionCheck()) {
        clearException(env);return;
    }
    jobject player=env->GetObjectField(mc,c->playerField);
    jobject world=env->GetObjectField(mc,c->worldField);
    if(!player||!world||env->ExceptionCheck()) {cancel("world_unavailable");return;}
    jobject target=env->CallObjectMethod(world,c->getEntityById,pending.entityId);
    if(!target||env->ExceptionCheck()) {
        cancel("entity_lookup_failed");return;
    }
    // Selection has already established player identity using the live world
    // player list. Some offline-server player wrappers do not satisfy the
    // optional cached EntityPlayer class test even though their entity id is
    // present in that list. Require only the living-entity methods used below.
    if(env->IsInstanceOf(target,c->livingClass)!=JNI_TRUE||env->ExceptionCheck()) {
        cancel("entity_not_living");return;
    }
    if(c->getHealth) {
        const float health=env->CallFloatMethod(target,c->getHealth);
        if(env->ExceptionCheck()||!std::isfinite(health)||health<=0.0F) {
            cancel("dead");return;
        }
    }
    silent::Vec3 eye{};silent::Bounds bounds{};
    if(!readCombatEye(env,player,eye)||!readCombatBounds(env,target,bounds)) {
        cancel("bounds_unavailable");return;
    }
    const double nearestX=std::clamp(eye.x,bounds.minX,bounds.maxX);
    const double nearestY=std::clamp(eye.y,bounds.minY,bounds.maxY);
    const double nearestZ=std::clamp(eye.z,bounds.minZ,bounds.maxZ);
    const double preAimDistance=std::hypot(
        std::hypot(nearestX-eye.x,nearestZ-eye.z),nearestY-eye.y);
    if(!std::isfinite(preAimDistance)||preAimDistance>pending.preAimReach+1.0e-5) {
        cancel("outside_preaim_range");return;
    }
    const auto trace=[&](const silent::Vec3 direction,const double reach) noexcept {
        silent::LogicalFramePlan ray{};
        ray.rayOrigin=eye;ray.rayDirection=direction;ray.rayLimit=reach;
        return traceLogicalBlock(env,world,ray);
    };
    const auto direction=silent::RayTraceCoordinator::direction(pending.committedRotation);
    const double distance=silent::RayTraceCoordinator::intersect(eye,direction,bounds,pending.reach);
    if(std::isfinite(distance)&&distance>=0.0) {
        const auto block=pending.enforceAvailability?trace(direction,pending.reach):silent::BlockRayHit{};
        if(!pending.enforceAvailability||(block.querySucceeded&&
           (block.distance<0.0||block.distance>distance+1.0e-5))) {
            (void)m_logicalController.revisePendingAttack(
                pending,pending.committedRotation,true);
            return;
        }
    }
    const auto point=silent::chooseCombatAimPoint(eye,bounds,pending.committedRotation,
        pending.reach,pending.enforceAvailability,trace);
    const double dx=point.point.x-eye.x,dy=point.point.y-eye.y,dz=point.point.z-eye.z;
    constexpr double degrees=180.0/3.14159265358979323846;
    const aim::Angles rotation{std::atan2(dz,dx)*degrees-90.0,
        -std::atan2(dy,std::hypot(dx,dz))*degrees};
    (void)m_logicalController.revisePendingAttack(pending,rotation,point.available);
}

jobject GameBindings::serializeLogicalPacket(JNIEnv* env,jobject packet) noexcept
{
    if(!env || !packet) return packet;
    observeDigPacket(env,packet);
    const auto* c=m_cache.get();
    if(!c || !c->movementPacketClass || !c->lookPacketClass ||
       !c->positionPacketClass || !c->positionLookPacketClass ||
       !c->movementPacketConstructor || !c->positionPacketConstructor ||
       !c->lookPacketConstructor || !c->positionLookPacketConstructor ||
       !c->packetOnGround || !c->packetYaw || !c->packetPitch ||
       std::any_of(c->packetPosition.begin(),c->packetPosition.end(),
                   [](jfieldID value){ return value==nullptr; })) return packet;
    if(!env->IsInstanceOf(packet,c->movementPacketClass) ||
       env->ExceptionCheck()==JNI_TRUE) { clearException(env); return packet; }
    const jboolean onGround=env->GetBooleanField(packet,c->packetOnGround);
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return packet; }
    // A transformed client may subclass C04/C05/C06. Exact-class comparison
    // mislabels such packets as Ground, then a rotation rewrite can discard
    // their position payload and violate the 1.8 position-reminder cadence.
    const bool exactPositionLook=env->IsInstanceOf(packet,c->positionLookPacketClass)==JNI_TRUE;
    const bool exactPosition=env->IsInstanceOf(packet,c->positionPacketClass)==JNI_TRUE;
    const bool exactLook=env->IsInstanceOf(packet,c->lookPacketClass)==JNI_TRUE;
    jclass packetClass=env->GetObjectClass(packet);
    const bool exactBase=packetClass&&
        env->IsSameObject(packetClass,c->movementPacketClass)==JNI_TRUE;
    if(packetClass) env->DeleteLocalRef(packetClass);
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return packet; }
    const bool hasPosition=exactPosition || exactPositionLook;
    const bool hasRotation=exactLook || exactPositionLook;
    const silent::PacketKind original=exactPositionLook ? silent::PacketKind::PositionLook :
        exactPosition ? silent::PacketKind::Position : exactLook ? silent::PacketKind::Look :
        exactBase ? silent::PacketKind::Ground : silent::PacketKind::Unknown;
    // Unknown movement derivatives must pass through intact; do not guess
    // their payload shape when an adapter injects a custom packet subclass.
    if(original==silent::PacketKind::Unknown) return packet;
    aim::Angles originalRotation{};
    bool originalRotationValid=false;
    if(hasRotation) {
        originalRotation={env->GetFloatField(packet,c->packetYaw),
                          env->GetFloatField(packet,c->packetPitch)};
        originalRotationValid=env->ExceptionCheck()==JNI_FALSE&&
            std::isfinite(originalRotation.yaw)&&
            std::isfinite(originalRotation.pitch);
        if(env->ExceptionCheck()==JNI_TRUE) {
            clearException(env);
            return packet;
        }
    }
    const auto record=[&](jobject value,silent::PacketKind finalKind,
                          const bool rotation,const bool restore,
                          const bool vanillaHandoff=false) noexcept {
        double x=0,y=0,z=0; float rawYaw=0,rawPitch=0;
        if(hasPosition) {
            x=env->GetDoubleField(value,c->packetPosition[0]);
            y=env->GetDoubleField(value,c->packetPosition[1]);
            z=env->GetDoubleField(value,c->packetPosition[2]);
        }
        if(rotation) {
            rawYaw=env->GetFloatField(value,c->packetYaw); rawPitch=env->GetFloatField(value,c->packetPitch);
        }
        if(env->ExceptionCheck()) {clearException(env);return;}
        m_movementPacketSerial.fetch_add(1U,std::memory_order_release);
        const auto before=m_logicalController.latest();
        const auto noPositionRun=hasPosition?0U:
            m_noPositionPacketRun.fetch_add(1U,std::memory_order_relaxed)+1U;
        if(hasPosition) m_noPositionPacketRun.store(0U,std::memory_order_relaxed);
        char detail[320]{};
        std::snprintf(detail,sizeof(detail),"originalType=%u finalType=%u hasPosition=%d hasRotation=%d noPositionRun=%u pos=(%.8f,%.8f,%.8f) yaw=%.6f pitch=%.6f",
            static_cast<unsigned>(original),static_cast<unsigned>(finalKind),hasPosition,rotation,
            noPositionRun,x,y,z,rawYaw,rawPitch);
        m_logicalController.debug().event("PACKET",before,detail);
        if(rotation) m_logicalController.acknowledgePacket({rawYaw,rawPitch},original,finalKind,hasPosition,true);
        if(restore) {
            m_waitingVanillaResume=true;
            m_logicalController.debug().event("RESTORE_PACKET",m_logicalController.latest(),detail,true);
        }
        if(vanillaHandoff) {
            m_waitingVanillaResume=false;
            m_logicalController.debug().event("VANILLA_ROTATION_RESUME",m_logicalController.latest(),detail,true);
        }
    };
    // Without adapted movement, sample current geometry at publication too.
    // The controller refuses to rewrite an already committed physics snapshot:
    // SCA refreshes before its first jump/moveFlying consumer instead. Never
    // emit an extra movement packet or attack during movement POST.
    refreshAttackAtPublication(env);
    const silent::PacketSerializationPlan plan=
        m_logicalController.packetPlan(hasPosition,hasRotation,
            originalRotation,originalRotationValid);
    if(plan.mutation==silent::PacketMutation::Pass) {
        record(packet,original,hasRotation,false);
        if(plan.restoring&&!hasRotation) {
            m_logicalController.acknowledgeSuppressedPacket(
                original,original,hasPosition);
            m_waitingVanillaResume=true;
            m_logicalController.debug().event("RESTORE_PACKET",
                m_logicalController.latest(),"duplicate rotation suppressed",true);
        }
        return packet;
    }
    const float yaw=static_cast<float>(plan.rotation.yaw);
    const float pitch=static_cast<float>(plan.rotation.pitch);
    if(!std::isfinite(yaw) || !std::isfinite(pitch)) return packet;
    jobject replacement=nullptr;
    if(plan.mutation==silent::PacketMutation::RemoveRotation) {
        if(hasPosition) {
            const jdouble x=env->GetDoubleField(packet,c->packetPosition[0]);
            const jdouble y=env->GetDoubleField(packet,c->packetPosition[1]);
            const jdouble z=env->GetDoubleField(packet,c->packetPosition[2]);
            if(env->ExceptionCheck()==JNI_FALSE)
                replacement=env->NewObject(c->positionPacketClass,
                    c->positionPacketConstructor,x,y,z,onGround);
        } else {
            replacement=env->NewObject(c->movementPacketClass,
                c->movementPacketConstructor,onGround);
        }
        if(env->ExceptionCheck()==JNI_TRUE || !replacement) {
            clearException(env); return packet;
        }
        const silent::PacketKind replacementKind=hasPosition
            ? silent::PacketKind::Position : silent::PacketKind::Ground;
        record(replacement,replacementKind,false,false);
        m_logicalController.acknowledgeSuppressedPacket(
            original,replacementKind,hasPosition);
        if(plan.restoring) {
            m_waitingVanillaResume=true;
            m_logicalController.debug().event("RESTORE_PACKET",
                m_logicalController.latest(),"duplicate rotation suppressed",true);
        }
        return replacement;
    }
    if(hasPosition) {
        const jdouble x=env->GetDoubleField(packet,c->packetPosition[0]);
        const jdouble y=env->GetDoubleField(packet,c->packetPosition[1]);
        const jdouble z=env->GetDoubleField(packet,c->packetPosition[2]);
        if(env->ExceptionCheck()==JNI_FALSE)
            replacement=env->NewObject(c->positionLookPacketClass,
                c->positionLookPacketConstructor,x,y,z,yaw,
                std::clamp(pitch,-90.0F,90.0F),onGround);
    } else {
        replacement=env->NewObject(c->lookPacketClass,c->lookPacketConstructor,
            yaw,std::clamp(pitch,-90.0F,90.0F),onGround);
    }
    if(env->ExceptionCheck()==JNI_TRUE || !replacement) {
        clearException(env);
        return packet;
    }
    const silent::PacketKind replacementKind=hasPosition
        ? silent::PacketKind::PositionLook : silent::PacketKind::Look;
    record(replacement,replacementKind,true,plan.restoring,
           plan.vanillaHandoff);
    return replacement;
}

bool GameBindings::readCombatEye(JNIEnv* env,jobject player,
                                 silent::Vec3& eye) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!player||!c||!c->getEyeHeight) return false;
    eye={env->GetDoubleField(player,c->positionX),
         env->GetDoubleField(player,c->positionY),
         env->GetDoubleField(player,c->positionZ)};
    eye.y+=env->CallFloatMethod(player,c->getEyeHeight);
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return false;}
    return std::isfinite(eye.x)&&std::isfinite(eye.y)&&std::isfinite(eye.z);
}

bool GameBindings::readCombatBounds(JNIEnv* env,jobject entity,
                                    silent::Bounds& bounds) noexcept
{
    const auto* c=m_cache.get();
    if(!env||!entity||!c||!c->getBounds) return false;
    jobject box=env->CallObjectMethod(entity,c->getBounds);
    if(!box||env->ExceptionCheck()==JNI_TRUE) {
        if(box) env->DeleteLocalRef(box);
        clearException(env);return false;
    }
    bounds={env->GetDoubleField(box,c->minX),env->GetDoubleField(box,c->minY),
        env->GetDoubleField(box,c->minZ),env->GetDoubleField(box,c->maxX),
        env->GetDoubleField(box,c->maxY),env->GetDoubleField(box,c->maxZ)};
    env->DeleteLocalRef(box);
    if(env->ExceptionCheck()==JNI_TRUE) {clearException(env);return false;}
    return std::isfinite(bounds.minX)&&std::isfinite(bounds.minY)&&
        std::isfinite(bounds.minZ)&&std::isfinite(bounds.maxX)&&
        std::isfinite(bounds.maxY)&&std::isfinite(bounds.maxZ)&&
        bounds.maxX>bounds.minX&&bounds.maxY>bounds.minY&&bounds.maxZ>bounds.minZ;
}

silent::BlockRayHit GameBindings::traceLogicalBlock(
    JNIEnv* env,jobject world,const silent::LogicalFramePlan& plan) noexcept
{
    silent::BlockRayHit result{};
    const auto* c=m_cache.get();
    if(!env || !world || !c || !c->rayTraceBlocks || !c->rayVectorClass ||
       !c->rayVectorConstructor || !c->hitVector ||
       std::any_of(c->vectorFields.begin(),c->vectorFields.end(),
                   [](jfieldID value){return value==nullptr;})) return result;
    // Adaptive sampling may issue several rays per target. Scope every JNI
    // local to one query instead of retaining hundreds until the frame ends.
    if(env->PushLocalFrame(12)<0) {clearException(env);return result;}
    struct RayLocals {JNIEnv* env;~RayLocals(){env->PopLocalFrame(nullptr);}} locals{env};
    const double reach=std::min(3.0,std::max(0.0,plan.rayLimit));
    jobject from=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,
        plan.rayOrigin.x,plan.rayOrigin.y,plan.rayOrigin.z);
    jobject to=env->NewObject(c->rayVectorClass,c->rayVectorConstructor,
        plan.rayOrigin.x+plan.rayDirection.x*reach,
        plan.rayOrigin.y+plan.rayDirection.y*reach,
        plan.rayOrigin.z+plan.rayDirection.z*reach);
    jobject hit=from&&to&&env->ExceptionCheck()==JNI_FALSE
        ? env->CallObjectMethod(world,c->rayTraceBlocks,from,to,
                                JNI_FALSE,JNI_TRUE,JNI_FALSE) : nullptr;
    if(env->ExceptionCheck()==JNI_TRUE) { clearException(env); return result; }
    result.querySucceeded=true;
    if(!hit) return result;
    jobject point=env->GetObjectField(hit,c->hitVector);
    jobject position=c->rayBlockPos ? env->GetObjectField(hit,c->rayBlockPos) : nullptr;
    jobject facing=c->raySideHit ? env->GetObjectField(hit,c->raySideHit) : nullptr;
    if(point && env->ExceptionCheck()==JNI_FALSE) {
        const double x=env->GetDoubleField(point,c->vectorFields[0]);
        const double y=env->GetDoubleField(point,c->vectorFields[1]);
        const double z=env->GetDoubleField(point,c->vectorFields[2]);
        if(env->ExceptionCheck()==JNI_FALSE)
            result.distance=std::hypot(
                std::hypot(x-plan.rayOrigin.x,y-plan.rayOrigin.y),
                z-plan.rayOrigin.z);
    }
    const bool blockMetadataReady=c->facingIndex &&
        std::none_of(c->blockPosCoordinates.begin(),c->blockPosCoordinates.end(),
                     [](jmethodID value){return value==nullptr;});
    if(blockMetadataReady && position && facing && env->ExceptionCheck()==JNI_FALSE) {
        result.target.x=env->CallIntMethod(position,c->blockPosCoordinates[0]);
        result.target.y=env->CallIntMethod(position,c->blockPosCoordinates[1]);
        result.target.z=env->CallIntMethod(position,c->blockPosCoordinates[2]);
        result.target.face=env->CallIntMethod(facing,c->facingIndex);
        result.target.valid=env->ExceptionCheck()==JNI_FALSE;
    }
    if(env->ExceptionCheck()==JNI_TRUE) {
        clearException(env); return {};
    }
    return result;
}

bool GameBindings::executeLogicalInteraction(
    JNIEnv* env,jobject minecraft,
    const silent::InteractionCommand& command) noexcept
{
    if(command.kind==silent::InteractionCommandKind::None) return true;
    const auto* c=m_cache.get();
    const auto fail=[&](const char* reason) noexcept {
        if(command.kind==silent::InteractionCommandKind::AttackEntity) {
            // attackDispatched is the single authoritative completion record;
            // logging here as well used to emit two ATTACK_FAILED rows for one
            // intent and made dispatch counts ambiguous.
            m_logicalController.attackDispatched(command,false,reason);
        }
        if(env) clearException(env);
        return false;
    };
    if(!env || !minecraft || !c) return fail("bindings_unavailable");
    jobject player=env->GetObjectField(minecraft,c->playerField);
    jobject world=env->GetObjectField(minecraft,c->worldField);
    jobject controller=env->GetObjectField(minecraft,c->playerControllerField);
    if(!player || !world || !controller || env->ExceptionCheck()==JNI_TRUE) {
        return fail("interaction_context_unavailable");
    }
    if(command.kind==silent::InteractionCommandKind::AttackEntity) {
        jobject target=env->CallObjectMethod(
            world,c->getEntityById,static_cast<jint>(command.entityId));
        if(!target||env->ExceptionCheck()==JNI_TRUE)
            return fail("logical_target_unavailable");
        if(env->IsInstanceOf(target,c->livingClass)!=JNI_TRUE)
            return fail("logical_target_replaced");
        const float targetHealth=env->CallFloatMethod(target,c->getHealth);
        if(env->ExceptionCheck()==JNI_TRUE||!std::isfinite(targetHealth)||targetHealth<=0.0F)
            return fail("logical_target_dead");
        silent::Vec3 eye{};
        silent::Bounds bounds{};
        if(!readCombatEye(env,player,eye)||!readCombatBounds(env,target,bounds))
            return fail("physics_geometry_unavailable");
        const auto direction=silent::RayTraceCoordinator::direction(
            command.committedRotation);
        const double hit=silent::RayTraceCoordinator::intersect(
            eye,direction,bounds,command.reach);
        // Validate the rotation that was actually published. A new point must
        // start a new transaction and get its own publication; never silently
        // substitute a fresh angle inside an already confirmed attack.
        if(!std::isfinite(hit)||hit<0.0)
            return fail("physics_ray_stale");
        if(command.enforceAvailability) {
            silent::LogicalFramePlan ray{};
            ray.rayOrigin=eye;ray.rayDirection=direction;ray.rayLimit=command.reach;
            const auto block=traceLogicalBlock(env,world,ray);
            if(!block.querySucceeded||
               (block.distance>=0.0&&block.distance<=hit+1.0e-5))
                return fail("physics_ray_occluded");
        }
        // Preserve the vanilla 1.8.9 click transaction: the client publishes
        // the arm swing first, then PlayerControllerMP sends the attack.  The
        // PRE dispatch boundary and frozen rotation transaction remain owned
        // by LogicalStateController; only this internal call order is restored.
        env->CallVoidMethod(player,c->swingItem);
        if(env->ExceptionCheck()==JNI_TRUE)
            return fail("swing_jni_exception");
        g_syntheticLogicalAttack=true;
        env->CallVoidMethod(controller,c->attackEntity,player,target);
        g_syntheticLogicalAttack=false;
        if(env->ExceptionCheck()==JNI_TRUE)
            return fail("attack_entity_jni_exception");
        m_logicalController.attackDispatched(command,true);
    } else if(command.kind==silent::InteractionCommandKind::ResetBlock) {
        env->CallVoidMethod(controller,c->resetBlockRemoving);
    } else {
        jobject position=env->NewObject(c->blockPosClass,c->blockPosConstructor,
            command.block.x,command.block.y,command.block.z);
        jobject facing=position ? env->CallStaticObjectMethod(
            c->enumFacingClass,c->getFacingByIndex,command.block.face) : nullptr;
        if(!position || !facing || env->ExceptionCheck()==JNI_TRUE) {
            clearException(env); return false;
        }
        const jboolean accepted=command.kind==
                silent::InteractionCommandKind::StartBlock
            ? env->CallBooleanMethod(controller,c->clickBlock,position,facing)
            : env->CallBooleanMethod(controller,c->onPlayerDamageBlock,position,facing);
        if(command.kind==silent::InteractionCommandKind::StartBlock &&
           env->ExceptionCheck()==JNI_FALSE && accepted!=JNI_TRUE)
            m_logicalController.blockRejected();
        if(env->ExceptionCheck()==JNI_FALSE &&
           (command.kind==silent::InteractionCommandKind::StartBlock ||
            accepted==JNI_TRUE))
            env->CallVoidMethod(player,c->swingItem);
        if(command.kind==silent::InteractionCommandKind::ContinueBlock &&
           env->ExceptionCheck()==JNI_FALSE &&
           env->CallBooleanMethod(world,c->isAirBlock,position)==JNI_TRUE)
            m_logicalController.blockFinished();
    }
    const bool succeeded=env->ExceptionCheck()!=JNI_TRUE;
    if(!succeeded&&command.kind==silent::InteractionCommandKind::AttackEntity)
        return fail("interaction_jni_exception");
    clearException(env);
    return succeeded;
}

jobject GameBindings::arbitrateLogicalAttack(
    JNIEnv* env,jobject originalTarget) noexcept
{
    if(!env||!originalTarget||g_syntheticLogicalAttack||
       !m_logicalController.active()) return originalTarget;
    // Vanilla/lower attackEntity is an ownership guard only. It must never
    // consume a prepared scheduler intent because not every caller is the
    // stable input/PRE boundary. The held-input hook emits the sole synthetic
    // dispatch; that call bypasses this guard via g_syntheticLogicalAttack.
    return nullptr;
}

bool GameBindings::consumeLogicalInteraction(
    JNIEnv* env,jobject minecraft,const LiveInteractionTransform::Entry entry,
    const bool heldDown) noexcept
{
    const auto* c=m_cache.get();
    // This transformed per-tick input entry is the clean boundary shared by
    // Smart Hotbar and combat. Processing the queue here keeps inventory
    // mutation out of key/right-click hooks and out of render-driven update.
    (void)processSmartHotbarRequests(env,minecraft);
    const bool down=entry==LiveInteractionTransform::Entry::Click || heldDown;
    if(!observeLogicalCamera(env,minecraft,down)) return false;
    // Input/PRE may run before heading PRE. Both boundaries honor the same
    // active Silent Lock owner; holding attack without a lock stays vanilla.
    if(env&&minecraft&&c&&syncSprintOwner()==SprintOwner::SilentCombat&&
       c->setSprinting&&c->playerField) {
        jobject player=env->GetObjectField(minecraft,c->playerField);
        if(player&&!env->ExceptionCheck())
            env->CallVoidMethod(player,c->setSprinting,JNI_FALSE);
        clearException(env);
    }
    m_logicalController.debug().event(entry==LiveInteractionTransform::Entry::Click
        ? "CLICK_PULSE" : "HELD_PULSE",
        m_logicalController.latest());
    if(m_logicalController.routeManualInput(down)) {
        m_silentRotationHook.setEnabled(m_logicalController.restoring()||
            m_logicalController.packetContinuityRequired()||
            m_logicalController.debug().enabled());
        // Original click / held-left owns block damage. A combat action already
        // emitted in this real tick defers digging until the next native tick.
        return !m_logicalController.manualBlockInputAllowed();
    }
    if(!m_logicalController.active()) return false;
    if(env->PushLocalFrame(32)<0) { clearException(env); return true; }
    const auto interactionTick=m_logicalController.latest().interactionTick;
    m_logicalController.beginInteractionPre(interactionTick);
    // Revalidate before consuming the CPS intent. If movement invalidated its
    // published ray, retain the same intent/target and require a fresh publish
    // rather than spending this click on a guaranteed failed dispatch.
    refreshAttackAtPublication(env);
    silent::InteractionCommand command=
        m_logicalController.clickAtInteractionPre(interactionTick);
    if(command.kind==silent::InteractionCommandKind::None)
        command=m_logicalController.held(heldDown);
    (void)executeLogicalInteraction(env,minecraft,command);
    env->PopLocalFrame(nullptr);
    // Once active, InteractionCoordinator is the sole source for both Java
    // entry points.  Even a deliberate no-op is consumed so vanilla cannot
    // produce an entity attack and block-damage event in the same logical tick.
    return true;
}

bool GameBindings::observeLogicalCamera(JNIEnv* env,jobject minecraft,bool down) noexcept
{
    const auto* c=m_cache.get();
    if(!env || !minecraft || !c || !c->playerField || !c->rotationYaw ||
       !c->rotationPitch ||
       env->PushLocalFrame(16)<0) { clearException(env); return false; }
    const auto failed=[&] {clearException(env);env->PopLocalFrame(nullptr);return false;};
    jobject player=env->GetObjectField(minecraft,c->playerField);
    jobject hit=c->cameraMouseOver
        ? env->GetObjectField(minecraft,c->cameraMouseOver) : nullptr;
    silent::BlockTarget block{}; int id=-1;
    if(hit && c->cameraHitEntity && !env->ExceptionCheck()) {
        jobject entity=env->GetObjectField(hit,c->cameraHitEntity);
        if(entity && c->getEntityId) id=env->CallIntMethod(entity,c->getEntityId);
        if(env->ExceptionCheck()) return failed();
        int hitKind=-1;
        const bool typedHit=c->cameraHitType && c->enumOrdinal;
        if(typedHit) {
            jobject type=env->GetObjectField(hit,c->cameraHitType);
            hitKind=type ? env->CallIntMethod(type,c->enumOrdinal) : -1;
            if(env->ExceptionCheck()) return failed();
        }
        jobject position=c->rayBlockPos ? env->GetObjectField(hit,c->rayBlockPos) : nullptr;
        jobject face=c->raySideHit ? env->GetObjectField(hit,c->raySideHit) : nullptr;
        // When typeOfHit is available, a MISS carrying a BlockPos is rejected.
        // On transformed Lunar descriptors, fall back to the unambiguous
        // position + face + no-entity shape so camera observation remains live.
        const bool blockHit=typedHit ? hitKind==1 : position && face && !entity;
        const bool blockMetadataReady=c->facingIndex &&
            std::none_of(c->blockPosCoordinates.begin(),c->blockPosCoordinates.end(),
                         [](jmethodID value){return value==nullptr;});
        if(blockMetadataReady && blockHit && position && face && !entity &&
           !env->ExceptionCheck()) {
            block.x=env->CallIntMethod(position,c->blockPosCoordinates[0]);
            if(env->ExceptionCheck()) return failed();
            block.y=env->CallIntMethod(position,c->blockPosCoordinates[1]);
            if(env->ExceptionCheck()) return failed();
            block.z=env->CallIntMethod(position,c->blockPosCoordinates[2]);
            if(env->ExceptionCheck()) return failed();
            block.face=env->CallIntMethod(face,c->facingIndex); block.valid=true;
        }
    }
    if(player && !env->ExceptionCheck()) {
        const aim::Angles camera{env->GetFloatField(player,c->rotationYaw),env->GetFloatField(player,c->rotationPitch)};
        const auto tick=c->entityTicks
            ? static_cast<std::uint64_t>(env->GetIntField(player,c->entityTicks))
            : 0U;
        const bool sneaking=c->isSneaking && env->CallBooleanMethod(player,c->isSneaking)==JNI_TRUE;
        const bool rightDown=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
        if(!env->ExceptionCheck()) m_logicalController.observeCameraInput(
            camera,block,id,down,tick,sneaking,rightDown);
    }
    const bool ok=player && !env->ExceptionCheck();
    clearException(env); env->PopLocalFrame(nullptr); return ok;
}

void GameBindings::observeActualInteraction(JNIEnv* env,LiveInteractionObserver::Event event,jobject argument) noexcept
{
    const auto* c=m_cache.get(); if(!c || !env) return;
    char detail[180]{};
    const char* name="RESET_BLOCK";
    if(event==LiveInteractionObserver::Event::Attack) {
        const int id=argument ? env->CallIntMethod(argument,c->getEntityId) : -1;
        if(env->ExceptionCheck()) {clearException(env);return;}
        m_lastAttackEntryEntity.store(id,std::memory_order_release);
        m_lastAttackEntryTick.store(::GetTickCount64(),std::memory_order_release);
        // This observer is composed at method entry and therefore sees the
        // vanilla/original argument before LiveAttackTransform substitutes the
        // committed target. The lower ownership hook records final target and
        // dispatch count; treating this entry argument as final would create a
        // false mismatch exactly when substitution is working.
        std::snprintf(detail,sizeof(detail),"entryEntity=%d arbitrationReady=%d",
            id,m_attackOwnershipHook.ready()?1:0);
        name=m_attackOwnershipHook.ready()?"ATTACK_ENTRY":"ACTUAL_ATTACK";
    } else if(argument) {
        const int x=env->CallIntMethod(argument,c->blockPosCoordinates[0]);
        if(env->ExceptionCheck()) {clearException(env);return;}
        const int y=env->CallIntMethod(argument,c->blockPosCoordinates[1]);
        if(env->ExceptionCheck()) {clearException(env);return;}
        const int z=env->CallIntMethod(argument,c->blockPosCoordinates[2]);
        if(env->ExceptionCheck()) {clearException(env);return;}
        std::snprintf(detail,sizeof(detail),"block=(%d,%d,%d)",x,y,z);
        name=event==LiveInteractionObserver::Event::StartBlock ? "CLICK_BLOCK_ENTRY" : "CONTINUE_DIGGING";
    }
    if(!env->ExceptionCheck()) m_logicalController.debug().event(name,m_logicalController.latest(),detail,
        event==LiveInteractionObserver::Event::Attack);
    clearException(env);
}

void GameBindings::observeDigPacket(JNIEnv* env,jobject packet) noexcept
{
    const auto* c=m_cache.get();
    if(!c || !c->diggingPacketClass || !c->diggingAction || !c->diggingPosition || !c->enumOrdinal ||
       !env->IsInstanceOf(packet,c->diggingPacketClass)) return;
    if(env->PushLocalFrame(8)<0) { clearException(env); return; }
    const auto done=[&] {clearException(env);env->PopLocalFrame(nullptr);};
    jobject action=env->CallObjectMethod(packet,c->diggingAction);
    if(env->ExceptionCheck()) {done();return;}
    jobject position=env->CallObjectMethod(packet,c->diggingPosition);
    if(action && position && !env->ExceptionCheck()) {
        const int kind=env->CallIntMethod(action,c->enumOrdinal);
        if(env->ExceptionCheck()) {done();return;}
        const int x=env->CallIntMethod(position,c->blockPosCoordinates[0]);
        if(env->ExceptionCheck()) {done();return;}
        const int y=env->CallIntMethod(position,c->blockPosCoordinates[1]);
        if(env->ExceptionCheck()) {done();return;}
        const int z=env->CallIntMethod(position,c->blockPosCoordinates[2]);
        if(kind>=0 && kind<=2 && !env->ExceptionCheck()) {
            const char* names[]{"START_DIGGING","ABORT_DIGGING","STOP_DIGGING"};
            char detail[100]{}; std::snprintf(detail,sizeof(detail),"block=(%d,%d,%d) source=C07",x,y,z);
            m_logicalController.debug().event(names[kind],m_logicalController.latest(),detail,true);
            if(kind==1 || kind==2) m_logicalController.manualBlockEnded();
        }
    }
    clearException(env); env->PopLocalFrame(nullptr);
}


} // namespace mcoverlay
