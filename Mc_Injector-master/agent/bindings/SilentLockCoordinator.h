#pragma once

#include "AimControl.h"
#include "SilentDebugRecorder.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace mcoverlay::silent {

// Rotation publication can stand alone, but scheduled attacks require the
// transformed input/PRE boundary. Render-driven updateGameplay() is allowed to
// prepare an intent only; it must never dispatch attackEntity() after movement
// POST in the same Minecraft tick.
struct RuntimeCapabilities final {
    bool rotationOutput=false;
    bool attackBindings=false;
    bool heldInputHook=false;
    bool attackOwnershipHook=false;

    [[nodiscard]] constexpr bool logicalOutputReady() const noexcept {
        return rotationOutput;
    }
    [[nodiscard]] constexpr bool attackSchedulerReady() const noexcept {
        return rotationOutput&&attackBindings&&heldInputHook;
    }
    [[nodiscard]] constexpr bool heldArbitrationReady() const noexcept {
        return attackBindings&&heldInputHook;
    }
    [[nodiscard]] constexpr bool ownershipArbitrationReady() const noexcept {
        return attackBindings&&attackOwnershipHook;
    }
};

// Team colours are authoritative target exclusions only in a confirmed
// Hypixel match. Generic/offline-mode scoreboards frequently use the same
// formatting byte for ranks, groups or chat decoration.
[[nodiscard]] inline constexpr bool isConfirmedCombatTeammate(
    const bool hypixelServer,const bool matchActive,
    const char ownTeam,const char entityTeam) noexcept {
    return hypixelServer&&matchActive&&ownTeam!='u'&&entityTeam==ownTeam;
}

enum class RotationOwner : std::uint8_t { Camera, AimAssist, Restore };
enum class InteractionOwner : std::uint8_t { None, SilentCombat, ManualBlock, BedBreaker, Scaffold };
enum class HeldItemPolicy : std::uint8_t { Other, MiningTool, BlockItem };
enum class InteractionMode : std::uint8_t { None, Entity, Block };
enum class PacketKind : std::uint8_t { Unknown, Ground, Position, Look, PositionLook };
enum class InteractionEvent : std::uint8_t {
    None, Attack, StartDigging, ContinueDigging, FinishedDigging, AbortDigging
};
enum class InteractionCommandKind : std::uint8_t {
    None, AttackEntity, StartBlock, ContinueBlock, ResetBlock
};
enum class PacketMutation : std::uint8_t { Pass, InjectRotation, RemoveRotation };

struct Vec3 final { double x=0.0, y=0.0, z=0.0; };
struct Bounds final {
    double minX=0.0, minY=0.0, minZ=0.0;
    double maxX=0.0, maxY=0.0, maxZ=0.0;
};
struct BlockTarget final {
    int x=0, y=0, z=0, face=0;
    bool valid=false;
    [[nodiscard]] bool sameBlock(const BlockTarget& other) const noexcept {
        return valid && other.valid && x==other.x && y==other.y && z==other.z;
    }
};
struct BlockRayHit final {
    bool querySucceeded=false;
    double distance=-1.0;
    BlockTarget target{};
};

struct MovementInput final {
    bool forward=false, back=false, left=false, right=false;
};
struct WorldMovementIntent final {
    double x=0.0, z=0.0;
    double magnitude=0.0;
    bool active=false;
};
struct ResolvedMovementIntent final {
    WorldMovementIntent world{};
    double logicalForward=0.0;
    double logicalStrafe=0.0;
};
struct LogicalMovementState final {
    std::uint64_t physicsTick=0U;
    std::uint64_t snapshotVersion=0U;
    aim::Angles cameraRotation{};
    aim::Angles logicalRotation{};
    double physicalForward=0.0;
    double physicalStrafe=0.0;
    double forward=0.0;
    double strafe=0.0;
    WorldMovementIntent worldIntent{};
    Vec3 currentVelocity{};
    Vec3 intendedVelocity{};
    bool physicalSprinting=false;
    bool sprinting=false;
    bool sprintSuppressed=false;
    bool onGround=false;
    bool controlsMinecraftMovement=false;
};

struct TargetCandidate final {
    int entityId=-1;
    std::uint64_t identity=0U;
    Vec3 aimPoint{};
    Bounds bounds{};
    bool eligible=false;
    bool coolingDown=false;
    bool attackAvailable=true;
};
struct TargetSelection final {
    int entityId=-1;
    std::uint64_t identity=0U;
    aim::Angles desired{};
    double distance=-1.0;
    bool valid=false;
    bool attackReady=true;
    // A recently attacked target remains fully attackable. This flag only
    // asks sequential selection to prefer another valid target when one exists.
    bool switchPreferred=false;
};
struct LogicalFrameInput final {
    std::uint64_t tick=0U;
    std::uint64_t physicsTick=0U;
    std::uint64_t worldGeneration=0U;
    int localEntityId=-1;
    bool enabled=false;
    bool silent=false;
    aim::Mode mode=aim::Mode::Smooth;
    bool nearestPriority=false;
    bool enforceAttackAvailability=true;
    bool coordinateMovement=true;
    bool sequentialTargets=false;
    // SilentCombat is an interaction transaction, not a passive aim mode.
    // Candidate scanning may continue while released, but rotation, movement,
    // sprint and attack ownership require a physically held left button.
    bool leftMouseDown=false;
    HeldItemPolicy heldItemPolicy=HeldItemPolicy::Other;
    double minimumDistance=0.0;
    double maximumDistance=6.0;
    // Acquisition may deliberately lead vanilla reach (3.5 m pre-aim versus
    // 3.0 m attack).  Never reuse the acquisition radius as the attack gate.
    double attackReach=3.0;
    double fovDegrees=90.0;
    int aimSpeedPercent=50;
    double mouseSensitivity=0.5;
    aim::Angles camera{};
    Vec3 eye{};
    MovementInput physicalMovement{};
    Vec3 currentVelocity{};
    bool sprinting=false;
    bool onGround=false;
    std::span<const TargetCandidate> candidates{};
};
struct InteractionCommand final {
    InteractionCommandKind kind=InteractionCommandKind::None;
    int entityId=-1;
    BlockTarget block{};
    std::uint64_t logicalTick=0U;
    std::uint64_t intentId=0U;
    std::uint64_t rotationEpoch=0U;
    aim::Angles committedRotation{};
    double reach=3.0;
    double preAimReach=3.5;
    bool enforceAvailability=true;
};
struct LogicalFramePlan final {
    bool aimActive=false;
    bool silentActive=false;
    bool writeVisibleRotation=false;
    aim::Angles visibleRotation{};
    aim::Angles logicalRotation{};
    aim::Angles renderDesiredRotation{};
    Vec3 rayOrigin{};
    Vec3 rayDirection{};
    double rayLimit=0.0;
    int candidateTargetId=-1;
    LogicalMovementState movement{};
    InteractionCommand interactionTransition{};
};
struct MovementCommand final {
    bool enabled=false;
    double physicalStrafe=0.0;
    double physicalForward=0.0;
    double strafe=0.0;
    double forward=0.0;
    aim::Angles cameraRotation{};
    aim::Angles logicalRotation{};
    std::uint64_t logicalTick=0U;
    std::uint64_t snapshotVersion=0U;
    bool physicalSprinting=false;
    bool sprinting=false;
};
struct PacketSerializationPlan final {
    PacketMutation mutation=PacketMutation::Pass;
    aim::Angles rotation{};
    bool restoring=false;
    bool vanillaHandoff=false;
};

struct LogicalTickState final {
    std::uint64_t tick=0U;
    std::uint64_t interactionTick=0U;
    std::uint64_t inputEventId=0U;
    std::uint64_t attackIntentId=0U;
    std::uint64_t attackDispatchCount=0U;
    std::uint64_t rotationEpoch=0U;
    std::uint64_t publishedRotationEpoch=0U;
    std::uint64_t requiredAttackRotationEpoch=0U;
    InteractionOwner interactionOwner=InteractionOwner::None;
    BlockTarget cameraBlock{};
    int cameraMouseOverEntityId=-1;
    bool leftMouseDown=false, rightMouseDown=false;
    bool sneaking=false, restorePending=false;
    aim::Angles camera{};
    aim::Angles renderDesired{};
    aim::Angles logical{};
    aim::Angles previousLogical{};
    aim::Angles networkRotation{};
    aim::Angles lastReported{};
    RotationOwner rotationOwner=RotationOwner::Camera;
    LogicalMovementState movement{};
    int candidateTargetId=-1;
    int rayFirstHitEntityId=-1;
    int attackTargetId=-1;
    int committedAttackTargetId=-1;
    BlockTarget rayBlock{};
    InteractionMode interaction=InteractionMode::None;
    PacketKind originalPacket=PacketKind::Unknown;
    PacketKind replacementPacket=PacketKind::Unknown;
    bool hasPosition=false;
    bool hasRotation=false;
    InteractionEvent event=InteractionEvent::None;
};

class RotationManager final {
public:
    void reset(const aim::Angles camera={}) noexcept {
        m_camera=camera; m_logical=camera; m_previousLogical=camera;
        m_lastReported={}; m_lastReportedValid=false;
        m_valid=std::isfinite(camera.yaw)&&std::isfinite(camera.pitch);
        m_active=false; m_restorePending=false; m_handoffPending=false;
        m_branchManaged=false;
        m_owner=RotationOwner::Camera;
    }
    void observeCamera(const aim::Angles camera) noexcept {
        if(!std::isfinite(camera.yaw)||!std::isfinite(camera.pitch)) return;
        m_camera={camera.yaw,std::clamp(camera.pitch,-90.0,90.0)};
        if(!m_valid) reset(m_camera);
    }
    aim::Angles acquire(const aim::Angles desired) noexcept {
        if(!m_valid) reset(m_camera);
        if(!std::isfinite(desired.yaw)||!std::isfinite(desired.pitch)) return m_logical;
        m_previousLogical=m_logical;
        m_logical.yaw+=aim::wrap(desired.yaw-m_logical.yaw);
        m_logical.pitch=std::clamp(desired.pitch,-90.0,90.0);
        m_active=true; m_restorePending=false; m_handoffPending=false;
        m_owner=RotationOwner::AimAssist;
        return m_logical;
    }
    void deactivate() noexcept {
        if(m_active) {
            m_active=false; m_restorePending=true; m_handoffPending=false;
            m_owner=RotationOwner::Restore;
        }
    }
    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] bool restoring() const noexcept {
        return !m_active&&(m_restorePending||m_handoffPending);
    }
    [[nodiscard]] bool restorePacketPending() const noexcept {
        return !m_active&&m_restorePending;
    }
    [[nodiscard]] bool handoffPending() const noexcept {
        return !m_active&&m_handoffPending;
    }
    [[nodiscard]] bool branchManaged() const noexcept {
        return m_branchManaged;
    }
    [[nodiscard]] RotationOwner owner() const noexcept { return m_owner; }
    [[nodiscard]] aim::Angles camera() const noexcept { return m_camera; }
    [[nodiscard]] aim::Angles logical() const noexcept { return m_logical; }
    [[nodiscard]] aim::Angles previousLogical() const noexcept { return m_previousLogical; }
    [[nodiscard]] aim::Angles lastReported() const noexcept { return m_lastReported; }
    [[nodiscard]] bool lastReportedValid() const noexcept { return m_lastReportedValid; }
    [[nodiscard]] aim::Angles networkRotation() const noexcept {
        aim::Angles desired=m_active ? m_logical : m_camera;
        const aim::Angles anchor=m_lastReportedValid ? m_lastReported : m_logical;
        desired.yaw=anchor.yaw+aim::wrap(desired.yaw-anchor.yaw);
        desired.pitch=std::clamp(desired.pitch,-90.0,90.0);
        return desired;
    }
    [[nodiscard]] aim::Angles cameraNetworkRotation() const noexcept {
        aim::Angles desired=m_camera;
        const aim::Angles anchor=m_lastReportedValid?m_lastReported:m_camera;
        desired.yaw=anchor.yaw+aim::wrap(desired.yaw-anchor.yaw);
        desired.pitch=std::clamp(desired.pitch,-90.0,90.0);
        return desired;
    }
    [[nodiscard]] aim::Angles continuousVanillaRotation(
        const aim::Angles vanilla) const noexcept {
        if(!std::isfinite(vanilla.yaw)||!std::isfinite(vanilla.pitch))
            return cameraNetworkRotation();
        const aim::Angles anchor=m_lastReportedValid?m_lastReported:m_camera;
        return {anchor.yaw+aim::wrap(vanilla.yaw-anchor.yaw),
                std::clamp(vanilla.pitch,-90.0,90.0)};
    }
    void acknowledgeReported(const aim::Angles reported) noexcept {
        if(!std::isfinite(reported.yaw)||!std::isfinite(reported.pitch)) return;
        if(m_lastReportedValid)
            m_lastReported.yaw+=aim::wrap(reported.yaw-m_lastReported.yaw);
        else m_lastReported.yaw=reported.yaw;
        m_lastReported.pitch=std::clamp(reported.pitch,-90.0,90.0);
        m_lastReportedValid=true;
    }
    void beginVanillaHandoff() noexcept {
        if(!m_restorePending) return;
        m_restorePending=false; m_handoffPending=true;
        m_owner=RotationOwner::Restore;
    }
    void finishVanillaHandoff() noexcept {
        m_restorePending=false; m_handoffPending=false;
        // EntityPlayerSP keeps its own raw camera-yaw branch. Rewriting only
        // the first vanilla Look would let a later mouse movement jump back by
        // a whole turn, so normalize subsequent vanilla rotations as well.
        m_branchManaged=true;
        m_owner=RotationOwner::Camera;
        const aim::Angles restored=m_lastReportedValid?m_lastReported:networkRotation();
        m_logical=restored; m_previousLogical=restored;
    }
private:
    aim::Angles m_camera{},m_logical{},m_previousLogical{},m_lastReported{};
    bool m_lastReportedValid=false,m_valid=false,m_active=false;
    bool m_restorePending=false,m_handoffPending=false,m_branchManaged=false;
    RotationOwner m_owner=RotationOwner::Camera;
};

class MovementIntentResolver final {
public:
    [[nodiscard]] ResolvedMovementIntent resolveAxes(
        const double physicalForward,const double physicalStrafe,
        const double cameraYaw,const double logicalYaw,
        const bool sprinting=false,const std::uint64_t tick=0U) const noexcept {
        ResolvedMovementIntent result;
        const double magnitude=std::hypot(physicalForward,physicalStrafe);
        result.world.magnitude=magnitude;
        if(magnitude<=1.0e-6) return result;
        constexpr double radians=3.14159265358979323846/180.0;
        const double camera=cameraYaw*radians;
        result.world.x=-std::sin(camera)*physicalForward+
                        std::cos(camera)*physicalStrafe;
        result.world.z= std::cos(camera)*physicalForward+
                        std::sin(camera)*physicalStrafe;
        result.world.active=true;
        const double logical=logicalYaw*radians;
        const double forward=-std::sin(logical)*result.world.x+std::cos(logical)*result.world.z;
        const double strafe=std::cos(logical)*result.world.x+std::sin(logical)*result.world.z;
        // Preserve the exact inverse-rotated vector. Quantising it to one of
        // eight WASD sectors changes world-space motion and creates prediction
        // drift at large camera/silent-yaw deltas.
        static_cast<void>(sprinting); static_cast<void>(tick);
        result.logicalForward=forward;
        result.logicalStrafe=strafe;
        return result;
    }
    [[nodiscard]] ResolvedMovementIntent resolve(
        const MovementInput input,const double cameraYaw,
        const double logicalYaw,const bool sprinting=false,
        const std::uint64_t tick=0U) const noexcept {
        const double forward=(input.forward?1.0:0.0)-(input.back?1.0:0.0);
        const double strafe=(input.left?1.0:0.0)-(input.right?1.0:0.0);
        return resolveAxes(forward,strafe,cameraYaw,logicalYaw,sprinting,tick);
    }
};

class SprintCoordinator final {
public:
    [[nodiscard]] bool coordinate(const bool physicalSprinting,
                                  const ResolvedMovementIntent& resolved) const noexcept {
        // Do not manufacture sprint.  A vanilla sprint may continue only while
        // the final logical input still contains a full-strength forward
        // component; side/back remaps naturally release it.
        return physicalSprinting&&resolved.world.active&&
               resolved.logicalForward>=0.8;
    }
};

class MovementCoordinator final {
public:
    [[nodiscard]] LogicalMovementState coordinate(
        const MovementInput input,const aim::Angles camera,const aim::Angles logical,
        const Vec3 current,const bool sprinting,const bool onGround,
        const std::uint64_t tick=0U) const noexcept {
        LogicalMovementState result;
        result.physicalForward=(input.forward?1.0:0.0)-(input.back?1.0:0.0);
        result.physicalStrafe=(input.left?1.0:0.0)-(input.right?1.0:0.0);
        const ResolvedMovementIntent resolved=m_resolver.resolve(
            input,camera.yaw,logical.yaw,sprinting,tick);
        result.forward=resolved.logicalForward;
        result.strafe=resolved.logicalStrafe;
        result.worldIntent=resolved.world;
        result.currentVelocity=current;
        result.physicalSprinting=sprinting;
        result.sprinting=m_sprint.coordinate(sprinting,resolved);
        result.sprintSuppressed=sprinting&&!result.sprinting;
        result.onGround=onGround;
        const double speed=std::hypot(current.x,current.z);
        result.intendedVelocity={resolved.world.x*speed,current.y,
                                 resolved.world.z*speed};
        result.controlsMinecraftMovement=resolved.world.active;
        return result;
    }
    [[nodiscard]] LogicalMovementState coordinateAxes(
        const double physicalForward,const double physicalStrafe,
        const aim::Angles camera,const aim::Angles logical,
        const Vec3 current,const bool sprinting,const bool onGround,
        const std::uint64_t tick=0U) const noexcept {
        LogicalMovementState result;
        result.physicalForward=physicalForward;
        result.physicalStrafe=physicalStrafe;
        const ResolvedMovementIntent resolved=m_resolver.resolveAxes(
            physicalForward,physicalStrafe,camera.yaw,logical.yaw,sprinting,tick);
        result.forward=resolved.logicalForward;
        result.strafe=resolved.logicalStrafe;
        result.worldIntent=resolved.world;
        result.currentVelocity=current;
        result.physicalSprinting=sprinting;
        result.sprinting=m_sprint.coordinate(sprinting,resolved);
        result.sprintSuppressed=sprinting&&!result.sprinting;
        result.onGround=onGround;
        const double speed=std::hypot(current.x,current.z);
        result.intendedVelocity={resolved.world.x*speed,current.y,
                                 resolved.world.z*speed};
        result.controlsMinecraftMovement=resolved.world.active;
        return result;
    }
private:
    MovementIntentResolver m_resolver;
    SprintCoordinator m_sprint;
};

class TargetSelector final {
public:
    void reset() noexcept { m_entity=-1; m_identity=0U; }
    [[nodiscard]] TargetSelection select(
        const std::span<const TargetCandidate> candidates,const Vec3 eye,
        const aim::Angles camera,const double minimumDistance,
        const double maximumDistance,const double acquireAngle,
        const double releaseAngle,const bool nearestPriority,
        const bool retainLock,const bool requireAttackable=false) noexcept {
        constexpr double degrees=180.0/3.14159265358979323846;
        TargetSelection best{},retained{},passive{},passiveRetained{};
        double bestScore=std::numeric_limits<double>::infinity();
        double passiveScore=std::numeric_limits<double>::infinity();
        for(const TargetCandidate& candidate:candidates) {
            if(!candidate.eligible) continue;
            const double dx=candidate.aimPoint.x-eye.x;
            const double dy=candidate.aimPoint.y-eye.y;
            const double dz=candidate.aimPoint.z-eye.z;
            const double horizontal=std::hypot(dx,dz);
            const double distance=std::hypot(horizontal,dy);
            if(!std::isfinite(distance)||horizontal<0.05||
               distance<minimumDistance||distance>maximumDistance) continue;
            const aim::Angles desired{std::atan2(dz,dx)*degrees-90.0,
                                      -std::atan2(dy,horizontal)*degrees};
            const double angle=std::hypot(aim::wrap(desired.yaw-camera.yaw),
                                          desired.pitch-camera.pitch);
            const bool same=candidate.entityId==m_entity &&
                (!m_identity||candidate.identity==m_identity);
            const TargetSelection choice{candidate.entityId,candidate.identity,desired,distance,
                true,!requireAttackable||candidate.attackAvailable,candidate.coolingDown};
            const bool passiveOnly=requireAttackable&&!candidate.attackAvailable;
            if(same&&angle<=releaseAngle) {
                if(passiveOnly) passiveRetained=choice;
                else retained=choice;
            }
            if(angle>acquireAngle) continue;
            double score=nearestPriority ? distance+angle*0.001
                                         : angle+distance*0.025;
            if(candidate.coolingDown) score+=10000.0;
            if(same) score*=0.65;
            if(passiveOnly) {
                if(score<passiveScore) {passiveScore=score;passive=choice;}
                continue;
            }
            if(score<bestScore) {
                bestScore=score;
                best=choice;
            }
        }
        if(retainLock&&retained.valid&&!retained.switchPreferred) best=retained;
        if(!best.valid&&requireAttackable) {
            // No attackable target: passive aim may still own/publish rotation,
            // but cannot bind an attack. Never let an unavailable retained lock
            // or sequential-switch preference outrank an attackable enemy.
            best=retainLock&&passiveRetained.valid&&!passiveRetained.switchPreferred
                ? passiveRetained:passive;
        }
        if(best.valid) { m_entity=best.entityId; m_identity=best.identity; }
        // Lock-on owns a short target-loss grace in LogicalStateController.
        // Keep the selector identity during that grace; a confirmed release
        // explicitly resets it after the timeout.
        else if(!retainLock) reset();
        return best;
    }
private:
    int m_entity=-1;
    std::uint64_t m_identity=0U;
};

class AimController final {
public:
    void reset(const aim::Angles camera={}) noexcept {
        m_smooth.reset(camera); m_lastTick=0U; m_target=-1;
        m_mode=aim::Mode::Smooth;
    }
    [[nodiscard]] aim::Angles advance(
        const aim::Angles camera,const aim::Angles desired,const aim::Mode mode,
        const int target,const std::uint64_t tick,const double sensitivity,
        const int speedPercent) noexcept {
        const bool changed=target!=m_target||mode!=m_mode;
        double dt=m_lastTick&&tick>m_lastTick
            ? static_cast<double>(tick-m_lastTick)/1000.0 : 1.0/240.0;
        if(changed||dt>0.10) { m_smooth.reset(camera); dt=1.0/240.0; }
        m_target=target; m_mode=mode; m_lastTick=tick;
        return mode==aim::Mode::LockOn
            ? aim::ExactLockOutput::apply(camera,desired)
            : m_smooth.apply(camera,desired,dt,sensitivity,speedPercent);
    }
private:
    aim::SmoothMouseOutput m_smooth;
    std::uint64_t m_lastTick=0U;
    int m_target=-1;
    aim::Mode m_mode=aim::Mode::Smooth;
};

class RayTraceCoordinator final {
public:
    [[nodiscard]] static Vec3 direction(const aim::Angles angles) noexcept {
        constexpr double r=3.14159265358979323846/180.0;
        const double yaw=angles.yaw*r,pitch=angles.pitch*r;
        const double horizontal=std::cos(pitch);
        return {-std::sin(yaw)*horizontal,-std::sin(pitch),
                 std::cos(yaw)*horizontal};
    }
    [[nodiscard]] static double intersect(const Vec3 origin,const Vec3 direction,
                                          const Bounds& box,const double limit) noexcept {
        double nearDistance=0.0,farDistance=limit;
        const auto axis=[&](const double o,const double d,const double minimum,
                            const double maximum) noexcept {
            if(std::abs(d)<1.0e-9) return o>=minimum&&o<=maximum;
            double a=(minimum-o)/d,b=(maximum-o)/d;
            if(a>b) std::swap(a,b);
            nearDistance=std::max(nearDistance,a);
            farDistance=std::min(farDistance,b);
            return nearDistance<=farDistance;
        };
        if(!axis(origin.x,direction.x,box.minX,box.maxX)||
           !axis(origin.y,direction.y,box.minY,box.maxY)||
           !axis(origin.z,direction.z,box.minZ,box.maxZ)) return -1.0;
        return nearDistance>=0.0&&nearDistance<=limit?nearDistance:-1.0;
    }
    struct EntityHit final { int first=-1; double firstDistance=-1.0; double candidateDistance=-1.0; };
    [[nodiscard]] EntityHit traceEntities(
        const std::span<const TargetCandidate> candidates,const int candidate,
        const Vec3 origin,const Vec3 ray,const double limit) const noexcept {
        EntityHit result;
        double nearest=std::numeric_limits<double>::infinity();
        for(const TargetCandidate& entity:candidates) {
            if(!entity.eligible) continue;
            const double hit=intersect(origin,ray,entity.bounds,limit);
            if(hit<0.0) continue;
            if(entity.entityId==candidate) result.candidateDistance=hit;
            if(hit<nearest) { nearest=hit; result.first=entity.entityId; result.firstDistance=hit; }
        }
        return result;
    }
    [[nodiscard]] static int attackTarget(
        const int candidate,const EntityHit entity,const BlockRayHit block,
        const bool enforceVanilla) noexcept {
        if(candidate<0) return -1;
        if(!enforceVanilla) return candidate;
        if(!block.querySucceeded||candidate!=entity.first||
           entity.candidateDistance<0.0||entity.candidateDistance>3.0+1.0e-6)
            return -1;
        if(block.distance>=0.0&&entity.candidateDistance+1.0e-5>=block.distance)
            return -1;
        return candidate;
    }
};

// Pure physics-space geometry, shared by preparation and PRE validation.
// Visibility failure never removes the candidate or its rotation ownership.
struct CombatAimPoint final { Vec3 point{}; bool available=false; };
template<class TraceBlock>
[[nodiscard]] CombatAimPoint chooseCombatAimPoint(const Vec3 eye,
    const Bounds& box,const aim::Angles reference,const double reach,
    const bool enforceAvailability,TraceBlock&& traceBlock) noexcept {
    const double cx=(box.minX+box.maxX)*0.5;
    const double cz=(box.minZ+box.maxZ)*0.5;
    const double height=box.maxY-box.minY;
    CombatAimPoint result{{cx,box.minY+height*0.55,cz},false};
    if(!std::isfinite(height)||height<=0.0||box.maxX<=box.minX||
       box.maxZ<=box.minZ||reach<=0.0) return result;
    const double insetX=(box.maxX-box.minX)*0.08;
    const double insetZ=(box.maxZ-box.minZ)*0.08;
    // The point nearest the eye handles the three-block edge, while a grid
    // across the actual box finds exposed chest/side/leg areas under cover.
    double best=std::numeric_limits<double>::infinity();
    const auto consider=[&](const Vec3 point) noexcept {
        const double dx=point.x-eye.x,dy=point.y-eye.y,dz=point.z-eye.z;
        const double length=std::hypot(std::hypot(dx,dz),dy);
        if(!std::isfinite(length)||length<1.0e-6) return;
        const Vec3 direction{dx/length,dy/length,dz/length};
        const double hit=RayTraceCoordinator::intersect(eye,direction,box,reach);
        if(hit<0.0) return;
        constexpr double degrees=180.0/3.14159265358979323846;
        const aim::Angles angle{std::atan2(dz,dx)*degrees-90.0,
            -std::atan2(dy,std::hypot(dx,dz))*degrees};
        // A minimal-turn ray alone hugs the nearest edge and becomes invalid
        // on the next strafe. Prefer interior points while still searching the
        // entire visible AABB when cover hides the centre.
        const double edgePenalty=120.0*(std::abs(point.x-cx)/(box.maxX-box.minX)+
            std::abs(point.z-cz)/(box.maxZ-box.minZ))+
            30.0*std::abs((point.y-box.minY)/height-0.55);
        const double score=std::hypot(aim::wrap(angle.yaw-reference.yaw),
            angle.pitch-reference.pitch)+length*0.01+edgePenalty;
        if(score>=best) return;
        if(enforceAvailability) {
            const BlockRayHit block=traceBlock(direction,reach);
            if(!block.querySucceeded||
               (block.distance>=0.0&&block.distance<=hit+1.0e-5)) return;
        }
        best=score;result={point,true};
    };
    consider(result.point);
    consider({std::clamp(eye.x,box.minX+insetX,box.maxX-insetX),
        std::clamp(eye.y,box.minY+height*0.08,box.maxY-height*0.08),
        std::clamp(eye.z,box.minZ+insetZ,box.maxZ-insetZ)});
    for(const double y:{0.18,0.38,0.58,0.78,0.92})
        for(const double x:{0.12,0.5,0.88})
            for(const double z:{0.12,0.5,0.88})
                consider({box.minX+(box.maxX-box.minX)*x,
                    box.minY+height*y,box.minZ+(box.maxZ-box.minZ)*z});
    return result;
}

class InteractionCoordinator final {
public:
    void reset() noexcept { m_candidate=m_firstHit=m_attack=-1; m_rayBlock={}; m_activeBlock={}; m_mode=InteractionMode::None; m_event=InteractionEvent::None; m_eventTick=0U; }
    void updateRay(const int candidate,const int firstHit,const int attack,
                   const BlockTarget block) noexcept {
        m_candidate=candidate; m_firstHit=firstHit; m_attack=attack; m_rayBlock=block;
    }
    [[nodiscard]] InteractionCommand click(const std::uint64_t tick) noexcept {
        if(m_attack>=0) {
            if(m_mode==InteractionMode::Block) return resetBlock(tick,false);
            m_mode=InteractionMode::Entity; m_event=InteractionEvent::Attack; m_eventTick=tick;
            return {InteractionCommandKind::AttackEntity,m_attack,{},tick};
        }
        if(m_mode==InteractionMode::Block) return resetBlock(tick,false);
        return {};
    }
    [[nodiscard]] InteractionCommand held(const bool down,const std::uint64_t tick) noexcept {
        if(!down) return m_mode==InteractionMode::Block ? resetBlock(tick,false)
                                                        : InteractionCommand{};
        if(m_eventTick==tick&&m_event!=InteractionEvent::None) return {};
        if(m_attack>=0) return m_mode==InteractionMode::Block ? resetBlock(tick,false)
                                                              : InteractionCommand{};
        return m_mode==InteractionMode::Block ? resetBlock(tick,false)
                                              : InteractionCommand{};
    }
    void blockFinished(const std::uint64_t tick) noexcept {
        m_mode=InteractionMode::None; m_activeBlock={};
        m_event=InteractionEvent::FinishedDigging; m_eventTick=tick;
    }
    void blockRejected(const std::uint64_t tick) noexcept {
        m_mode=InteractionMode::None; m_activeBlock={};
        m_event=InteractionEvent::AbortDigging; m_eventTick=tick;
    }
    [[nodiscard]] InteractionCommand cancel(const std::uint64_t tick) noexcept {
        if(m_mode==InteractionMode::Block) return resetBlock(tick,false);
        m_mode=InteractionMode::None; m_activeBlock={};
        m_event=InteractionEvent::None; m_eventTick=0U;
        return {};
    }
    [[nodiscard]] int attackTarget() const noexcept { return m_attack; }
    [[nodiscard]] InteractionMode mode() const noexcept { return m_mode; }
    [[nodiscard]] InteractionEvent event() const noexcept { return m_event; }
private:
    [[nodiscard]] InteractionCommand resetBlock(
        const std::uint64_t tick,const bool finished) noexcept {
        const BlockTarget old=m_activeBlock;
        m_mode=InteractionMode::None; m_activeBlock={};
        m_event=finished?InteractionEvent::FinishedDigging:InteractionEvent::AbortDigging;
        m_eventTick=tick;
        return {InteractionCommandKind::ResetBlock,-1,old,tick};
    }
    int m_candidate=-1,m_firstHit=-1,m_attack=-1;
    BlockTarget m_rayBlock{},m_activeBlock{};
    InteractionMode m_mode=InteractionMode::None;
    InteractionEvent m_event=InteractionEvent::None;
    std::uint64_t m_eventTick=0U;
};

class PacketObserver final {
public:
    static constexpr std::size_t Capacity=128U;
    void publish(const LogicalTickState& state) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        m_value=state;
        ReleaseSRWLockExclusive(&m_lock);
    }
    [[nodiscard]] LogicalTickState latest() const noexcept {
        AcquireSRWLockShared(&m_lock);
        const LogicalTickState result=m_value;
        ReleaseSRWLockShared(&m_lock);
        return result;
    }
private:
    mutable SRWLOCK m_lock=SRWLOCK_INIT;
    LogicalTickState m_value{};
};

// One monotonic hold clock owns every synthetic combat intent. It deliberately
// never catches up: after a late pulse the next deadline is measured from that
// pulse, so a stalled client cannot release a burst of queued attacks.
class FixedCpsAttackScheduler final {
public:
    struct Pulse final {
        std::uint64_t intentId=0U;
        std::uint64_t deadlineMicroseconds=0U;
        std::uint64_t cancelledIntentId=0U;
    };
    [[nodiscard]] Pulse update(const bool held,const bool blocked,
                               const std::uint64_t nowMicroseconds,
                               const int requestedCps) noexcept {
        Pulse result;
        if(!held||blocked) {
            result.cancelledIntentId=m_pendingIntentId;
            m_pendingIntentId=0U; m_armed=false; m_nextDeadline=0U;
            return result;
        }
        if(m_pendingIntentId) {
            result.intentId=m_pendingIntentId;
            result.deadlineMicroseconds=m_pendingDeadline;
            return result;
        }
        const int cps=std::clamp(requestedCps,1,20);
        const std::uint64_t interval=1000000ULL/static_cast<std::uint64_t>(cps);
        if(!m_armed) {m_armed=true;m_nextDeadline=nowMicroseconds;}
        if(nowMicroseconds<m_nextDeadline) return result;
        m_pendingIntentId=++m_nextIntentId;
        m_pendingDeadline=m_nextDeadline;
        // No catch-up burst: late delivery starts one fresh interval now.
        m_nextDeadline=nowMicroseconds+interval;
        result.intentId=m_pendingIntentId;
        result.deadlineMicroseconds=m_pendingDeadline;
        return result;
    }
    [[nodiscard]] std::uint64_t pending() const noexcept {
        return m_pendingIntentId;
    }
    [[nodiscard]] std::uint64_t consume() noexcept {
        const std::uint64_t value=m_pendingIntentId;
        m_pendingIntentId=0U; m_pendingDeadline=0U;
        return value;
    }
    [[nodiscard]] std::uint64_t reset() noexcept {
        const std::uint64_t value=m_pendingIntentId;
        m_pendingIntentId=0U; m_pendingDeadline=0U;
        m_armed=false; m_nextDeadline=0U;
        return value;
    }
private:
    std::uint64_t m_nextIntentId=0U;
    std::uint64_t m_nextDeadline=0U;
    std::uint64_t m_pendingIntentId=0U;
    std::uint64_t m_pendingDeadline=0U;
    bool m_armed=false;
};

class LogicalStateController final {
public:
    LogicalStateController() noexcept = default;
    SilentDebugRecorder& debug() noexcept { return m_debug; }
    void reset(const aim::Angles camera={}) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        m_selector.reset(); m_aim.reset(camera); m_rotation.reset(camera);
        m_interaction.reset(); m_pendingInteraction={}; m_interactionTick=1U;
        m_manualBlock=false; m_nativeTickKnown=false;
        m_targetGraceActive=false;
        m_lastCombatTick=~std::uint64_t{0};
        m_lastMovementTick=~std::uint64_t{0}; m_movementCommand={};
        m_committedMovementTick=~std::uint64_t{0};
        m_committedMovement={}; m_movementSnapshotVersion=0U;
        m_physicsPhaseTick=~std::uint64_t{0}; m_movementPostReached=false;
        m_interactionPreOpen=false;
        m_sprintDecisionTick=~std::uint64_t{0};
        m_sprintDecisionOwned=false; m_sprintDecisionAllowed=true;
        m_sprintIntentRequested=false; m_sprintSuppressedUntilRelease=false;
        m_lastTargetSeenTick=0U; m_lastRotationReleaseTick=~std::uint64_t{0};
        m_recentAttacks={}; m_outputReady=false; m_targetAttackReady=false;
        m_cameraResyncPending=false;
        (void)m_attackClock.reset();
        m_rotationEpoch=0U; m_publishedRotationEpoch=0U;
        clearAttackRequirementLocked();
        m_nextInputEventId=0U;
        m_state={}; m_state.camera=camera;
        m_state.logical=camera; m_state.renderDesired=camera;
        m_world=0U; m_localPlayer=-1; m_silent=false;
        m_heldItemPolicy=HeldItemPolicy::Other;
        m_coordinateMovement=true;
        publishLocked();
        ReleaseSRWLockExclusive(&m_lock);
    }
    template<class BlockTracer>
    [[nodiscard]] LogicalFramePlan advance(
        const LogicalFrameInput& input,BlockTracer&& traceBlock) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        LogicalFramePlan plan=advanceLocked(input);
        const BlockRayHit block=plan.silentActive?traceBlock(plan):BlockRayHit{};
        finalizeRayLocked(block);
        // Target, rotation, movement, ray, availability and interaction owner
        // become visible together.  Input/packet threads block on m_lock while
        // the JNI block trace is in flight and can observe only the complete
        // previous or complete next logical tick.
        publishLocked();
        ReleaseSRWLockExclusive(&m_lock);
        return plan;
    }
    void updateAttackClock(const bool held,const bool combatEnabled,
                           const std::uint64_t nowMicroseconds,
                           const int cps) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const bool miningBlock=m_heldItemPolicy==HeldItemPolicy::MiningTool&&
            m_state.cameraBlock.valid&&m_state.cameraMouseOverEntityId<0;
        const bool placement=m_heldItemPolicy==HeldItemPolicy::BlockItem&&
            m_state.rightMouseDown;
        const bool enabled=m_silent&&combatEnabled;
        const auto pulse=m_attackClock.update(held,!enabled||miningBlock||placement,
                                              nowMicroseconds,cps);
        if(pulse.cancelledIntentId) {
            clearAttackRequirementLocked(pulse.cancelledIntentId);
            char detail[144]{};
            std::snprintf(detail,sizeof(detail),"intent=%llu reason=%s",
                static_cast<unsigned long long>(pulse.cancelledIntentId),
                !held?"release":!enabled?"combat_inactive":
                    miningBlock?"mining_priority":"placement");
            m_debug.event("ATTACK_REJECT",m_state,detail,true);
        }
        if(pulse.intentId&&pulse.intentId!=m_state.attackIntentId) {
            clearAttackRequirementLocked();
            m_state.inputEventId=pulse.intentId;
            m_state.attackIntentId=pulse.intentId;
            char detail[160]{};
            std::snprintf(detail,sizeof(detail),
                "intent=%llu deadlineUs=%llu cps=%d",
                static_cast<unsigned long long>(pulse.intentId),
                static_cast<unsigned long long>(pulse.deadlineMicroseconds),
                std::clamp(cps,1,20));
            m_debug.event("ATTACK_SCHEDULE",m_state,detail,true);
        }
        publishLocked();
        ReleaseSRWLockExclusive(&m_lock);
    }
private:
    [[nodiscard]] LogicalFramePlan advanceLocked(
        const LogicalFrameInput& input) noexcept {
        m_outputReady=false;
        m_sequentialTargets=input.sequentialTargets;
        LogicalFramePlan plan;
        plan.interactionTransition=m_pendingInteraction;
        m_pendingInteraction={};
        const auto acceptTransition=[&](const InteractionCommand command) noexcept {
            if(command.kind==InteractionCommandKind::None) return;
            if(plan.interactionTransition.kind==InteractionCommandKind::None)
                plan.interactionTransition=command;
            else
                m_pendingInteraction=command;
        };
        const bool contextChanged=m_world!=input.worldGeneration||
                                   m_localPlayer!=input.localEntityId||
                                   m_silent!=input.silent;
        const bool movementContextChanged=contextChanged||
            m_coordinateMovement!=input.coordinateMovement;
        if(contextChanged) {
            m_manualBlock=false; m_lastMovementTick=~std::uint64_t{0};
            m_targetGraceActive=false;
            if(m_world!=input.worldGeneration||m_localPlayer!=input.localEntityId)
                m_recentAttacks={};
            acceptTransition(m_interaction.cancel(m_interactionTick));
            m_selector.reset(); m_aim.reset(input.camera);
            if(m_world!=input.worldGeneration||m_localPlayer!=input.localEntityId||
               (input.silent&&!m_rotation.restoring())) m_rotation.reset(input.camera);
        }
        if(movementContextChanged) {
            m_committedMovementTick=~std::uint64_t{0};
            m_committedMovement={};
        }
        m_world=input.worldGeneration; m_localPlayer=input.localEntityId; m_silent=input.silent;
        m_heldItemPolicy=input.heldItemPolicy;
        m_coordinateMovement=input.coordinateMovement;
        m_rotation.observeCamera(input.camera);
        plan.rayOrigin=input.eye; plan.rayLimit=std::max(0.0,input.maximumDistance);
        const bool releaseRequested=!input.enabled||!input.silent||
            input.mode!=aim::Mode::LockOn||!input.leftMouseDown||
            m_manualBlock||contextChanged;
        if(releaseRequested) {
            // The render sample is also an authoritative release boundary.  Do
            // not rely solely on a later held-input callback: no packet,
            // movement consumer or click may retain a transaction after LMB is
            // observed up here.
            const std::uint64_t cancelled=m_attackClock.reset();
            clearAttackRequirementLocked(cancelled);
            m_sprintDecisionTick=~std::uint64_t{0};
            m_sprintDecisionOwned=false;
            m_sprintDecisionAllowed=true;
            m_sprintIntentRequested=false;
            m_sprintSuppressedUntilRelease=false;
        }
        const bool attackTransactionActive=attackTransactionActiveLocked();
        const double acquire=std::clamp(input.fovDegrees,1.0,360.0)*0.5;
        std::array<TargetCandidate,256U> adjusted{};
        std::span<const TargetCandidate> candidates=input.candidates;
        if(input.sequentialTargets) {
            const std::size_t count=std::min(adjusted.size(),input.candidates.size());
            for(std::size_t index=0;index<count;++index) {
                adjusted[index]=input.candidates[index];
                adjusted[index].coolingDown=adjusted[index].coolingDown ||
                    recentlyAttackedLocked(adjusted[index].entityId,input.tick);
            }
            candidates={adjusted.data(),count};
        }
        const TargetSelection selected=!attackTransactionActive&&input.enabled&&
            !m_manualBlock
            ? m_selector.select(candidates,input.eye,input.camera,
                std::max(0.0,input.minimumDistance),std::max(1.0,input.maximumDistance),
                acquire,std::min(180.0,acquire+10.0),input.nearestPriority,
                input.mode==aim::Mode::LockOn,input.enforceAttackAvailability)
            : TargetSelection{};
        const bool silentCombatRequested=input.silent&&
            input.mode==aim::Mode::LockOn&&input.leftMouseDown;
        const bool explicitRelease=releaseRequested;
        const bool targetGrace=!selected.valid&&!explicitRelease&&
            m_rotation.active()&&m_state.candidateTargetId>=0&&
            input.tick>=m_lastTargetSeenTick&&
            input.tick-m_lastTargetSeenTick<=TargetLossGraceMilliseconds;
        const bool blockItemKeepsCamera=silentCombatRequested&&
            input.heldItemPolicy==HeldItemPolicy::BlockItem&&
            m_attackClock.pending()==0U;
        if(input.silent&&!silentCombatRequested) {
            // Passive scanning is intentionally retained for fast acquisition,
            // but a released left button immediately returns every local and
            // network-facing owner to Camera/Vanilla.
            m_targetGraceActive=false;
            m_targetAttackReady=false;
            if(m_rotation.active()) m_lastRotationReleaseTick=input.tick;
            m_rotation.deactivate();
            acceptTransition(m_interaction.cancel(m_interactionTick));
            m_interaction.updateRay(-1,-1,-1,{});
            plan.aimActive=selected.valid;
            plan.candidateTargetId=selected.valid?selected.entityId:-1;
            plan.renderDesiredRotation=selected.valid?selected.desired:input.camera;
            plan.logicalRotation=input.camera;
        } else if(attackTransactionActive) {
            // One intent owns one immutable target/rotation pair.  AimAssist
            // must not advance to R1/R2 while this intent is waiting for R0 to
            // be observed at the packet serialization boundary.
            m_targetGraceActive=false;
            m_targetAttackReady=m_attackRequiredAvailable;
            plan.aimActive=true;
            plan.silentActive=true;
            plan.candidateTargetId=m_attackRequiredTargetId;
            plan.renderDesiredRotation=m_attackRequiredRotation;
            plan.logicalRotation=m_rotation.acquire(m_attackRequiredRotation);
        } else if(blockItemKeepsCamera) {
            m_targetGraceActive=false;
            m_targetAttackReady=false;
            if(m_rotation.active()) m_lastRotationReleaseTick=input.tick;
            m_rotation.deactivate();
            acceptTransition(m_interaction.cancel(m_interactionTick));
            m_interaction.updateRay(-1,-1,-1,{});
            plan.aimActive=selected.valid;
            plan.candidateTargetId=selected.valid?selected.entityId:-1;
            plan.renderDesiredRotation=selected.valid?selected.desired:input.camera;
            plan.logicalRotation=input.camera;
        } else if(!selected.valid && !targetGrace) {
            m_targetGraceActive=false;
            m_targetAttackReady=false;
            if(m_rotation.active()) m_lastRotationReleaseTick=input.tick;
            m_selector.reset(); m_aim.reset(input.camera); m_rotation.deactivate();
            acceptTransition(m_interaction.cancel(m_interactionTick));
            m_interaction.updateRay(-1,-1,-1,{});
            plan.logicalRotation=input.camera; plan.renderDesiredRotation=input.camera;
        } else if(targetGrace) {
            // A single incomplete render/backend sample must not create a
            // Silent -> Restore -> Silent ownership pulse.  Keep the previous
            // target/rotation owner briefly, but publish no executable hit.
            m_targetAttackReady=false;
            m_targetGraceActive=true;
            plan.aimActive=true; plan.silentActive=true;
            plan.candidateTargetId=m_state.candidateTargetId;
            plan.renderDesiredRotation=m_state.renderDesired;
            plan.logicalRotation=m_rotation.logical();
        } else {
            m_targetGraceActive=false;
            m_lastTargetSeenTick=input.tick;
            m_targetAttackReady=selected.attackReady;
            plan.aimActive=true; plan.candidateTargetId=selected.entityId;
            plan.renderDesiredRotation=selected.desired;
            const aim::Angles aimed=m_aim.advance(input.camera,selected.desired,
                input.mode,selected.entityId,input.tick,input.mouseSensitivity,
                input.aimSpeedPercent);
            const bool sameTickReacquire=m_rotation.restoring()&&
                input.tick<=m_lastRotationReleaseTick;
            if(silentCombatRequested&&!sameTickReacquire) {
                plan.silentActive=true; plan.logicalRotation=m_rotation.acquire(aimed);
            } else {
                if(!sameTickReacquire) {
                    if(m_rotation.active()) m_lastRotationReleaseTick=input.tick;
                    m_rotation.deactivate();
                    plan.visibleRotation=aimed; plan.writeVisibleRotation=true;
                    acceptTransition(m_interaction.cancel(m_interactionTick));
                }
                plan.logicalRotation=sameTickReacquire?input.camera:aimed;
            }
        }
        // Publish only the rotation/velocity metadata here. The real
        // moveFlying/jump consumer owns the exact axes and commits movement
        // once for its own physics tick, eliminating render-frame N/N+1 drift.
        plan.movement={};
        plan.movement.physicsTick=input.physicsTick;
        plan.movement.snapshotVersion=m_movementSnapshotVersion;
        plan.movement.cameraRotation=input.camera;
        plan.movement.logicalRotation=plan.logicalRotation;
        plan.movement.physicalForward=
            (input.physicalMovement.forward?1.0:0.0)-
            (input.physicalMovement.back?1.0:0.0);
        plan.movement.physicalStrafe=
            (input.physicalMovement.left?1.0:0.0)-
            (input.physicalMovement.right?1.0:0.0);
        plan.movement.forward=plan.movement.physicalForward;
        plan.movement.strafe=plan.movement.physicalStrafe;
        plan.movement.currentVelocity=input.currentVelocity;
        plan.movement.physicalSprinting=input.sprinting;
        plan.movement.sprinting=input.sprinting;
        plan.movement.onGround=input.onGround;
        if(m_committedMovementTick==input.physicsTick)
            plan.movement=m_committedMovement;
        plan.rayDirection=RayTraceCoordinator::direction(plan.logicalRotation);
        m_entityHit=attackTransactionActive
            ? RayTraceCoordinator::EntityHit{
                m_attackRequiredTargetId,0.0,0.0}
            : plan.silentActive
            ? m_rayTrace.traceEntities(candidates,plan.candidateTargetId,
                plan.rayOrigin,plan.rayDirection,plan.rayLimit)
            : RayTraceCoordinator::EntityHit{};
        if(plan.silentActive) {
            const bool rotationChanged=!m_rotationEpoch||
                m_state.candidateTargetId!=plan.candidateTargetId||
                std::abs(aim::wrap(plan.logicalRotation.yaw-m_state.logical.yaw))>0.0005||
                std::abs(plan.logicalRotation.pitch-m_state.logical.pitch)>0.0005;
            if(rotationChanged) ++m_rotationEpoch;
        }
        m_enforceAttack=input.enforceAttackAvailability;
        m_attackReach=std::max(0.0,input.attackReach);
        m_preAimReach=std::max(m_attackReach,std::max(0.0,input.maximumDistance));
        m_state.tick=input.tick; m_state.interactionTick=m_interactionTick;
        m_state.leftMouseDown=input.leftMouseDown;
        m_state.camera=input.camera;
        m_state.renderDesired=plan.renderDesiredRotation; m_state.logical=plan.logicalRotation;
        m_state.previousLogical=m_rotation.previousLogical();
        m_state.networkRotation=attackTransactionActive
            ? m_attackRequiredRotation:m_rotation.networkRotation();
        m_state.lastReported=m_rotation.lastReported();
        m_state.rotationEpoch=m_rotationEpoch;
        m_state.publishedRotationEpoch=m_publishedRotationEpoch;
        m_state.requiredAttackRotationEpoch=m_attackRequiredRotationEpoch;
        m_state.rotationOwner=m_rotation.owner(); m_state.movement=plan.movement;
        m_state.candidateTargetId=plan.candidateTargetId;
        m_state.rayFirstHitEntityId=m_entityHit.first;
        m_state.attackTargetId=-1; m_state.rayBlock={};
        m_state.interaction=m_interaction.mode(); m_state.event=InteractionEvent::None;
        return plan;
    }
    void finalizeRayLocked(const BlockRayHit block) noexcept {
        if(attackTransactionActiveLocked()) {
            // A pre-aim transaction owns its target/rotation while it waits,
            // but temporary lack of reach/visibility is not an executable hit
            // and must not consume the CPS intent.
            m_outputReady=m_rotation.active();
            m_targetAttackReady=m_attackRequiredAvailable;
            m_interaction.updateRay(m_attackRequiredTargetId,
                m_attackRequiredTargetId,
                m_attackRequiredAvailable?m_attackRequiredTargetId:-1,{});
            m_state.candidateTargetId=m_attackRequiredTargetId;
            m_state.rayFirstHitEntityId=m_attackRequiredTargetId;
            m_state.attackTargetId=m_attackRequiredAvailable
                ?m_attackRequiredTargetId:-1;
            m_state.rayBlock={};
            m_state.interaction=m_interaction.mode();
            m_state.event=InteractionEvent::None;
            return;
        }
        const int attack=m_rotation.active()&&m_targetAttackReady
            ? RayTraceCoordinator::attackTarget(m_state.candidateTargetId,m_entityHit,
                                                block,m_enforceAttack)
            : -1;
        // Attack availability answers only whether this intent may dispatch.
        // Rotation and movement remain owned by the same SilentCombat state
        // for the complete physical hold, even when the ray is temporarily
        // blocked or outside vanilla reach.
        m_outputReady=m_rotation.active();
        if(m_outputReady) m_cameraResyncPending=false;
        else if(m_rotation.active()&&m_rotation.lastReportedValid()) {
            const aim::Angles camera=m_rotation.cameraNetworkRotation();
            const aim::Angles reported=m_rotation.lastReported();
            m_cameraResyncPending=std::abs(camera.yaw-reported.yaw)>0.0005||
                std::abs(camera.pitch-reported.pitch)>0.0005;
        }
        m_interaction.updateRay(m_state.candidateTargetId,m_entityHit.first,
                                attack,block.target);
        m_state.rayFirstHitEntityId=m_entityHit.first; m_state.attackTargetId=attack;
        m_state.rayBlock=block.target; m_state.interaction=m_interaction.mode();
        m_state.event=InteractionEvent::None;
        const std::uint64_t pendingIntent=m_attackClock.pending();
        if(pendingIntent&&m_state.candidateTargetId>=0&&m_rotation.active()&&
           m_attackRequiredIntentId!=pendingIntent) {
            m_attackRequiredIntentId=pendingIntent;
            m_attackRequiredTargetId=m_state.candidateTargetId;
            m_attackRequiredRotation=m_state.networkRotation;
            m_attackRequiredRotationEpoch=m_rotationEpoch;
            m_attackRequiredAvailable=attack>=0;
            m_state.requiredAttackRotationEpoch=m_attackRequiredRotationEpoch;
        } else if(pendingIntent&&m_state.candidateTargetId<0) {
            const std::uint64_t rejected=m_attackClock.consume();
            clearAttackRequirementLocked(rejected);
            char detail[112]{};
            std::snprintf(detail,sizeof(detail),"intent=%llu reason=no_target",
                static_cast<unsigned long long>(rejected));
            m_debug.event("ATTACK_REJECT",m_state,detail,true);
        }
    }
public:
    [[nodiscard]] MovementCommand movementCommand(
        const double physicalStrafe,const double physicalForward,
        const std::uint64_t minecraftTick=0U) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const auto tick=minecraftTick ? minecraftTick : m_interactionTick;
        beginPhysicsTickLocked(tick);
        const MovementCommand command=movementCommandLocked(
            physicalStrafe,physicalForward,tick);
        m_state.interactionTick=tick;
        m_debug.event("MOVE_INPUT",m_state);
        publishLocked();
        ReleaseSRWLockExclusive(&m_lock); return command;
    }
    [[nodiscard]] MovementCommand jumpCommand(
        const double physicalStrafe,const double physicalForward,
        const bool physicalSprinting,
        const std::uint64_t minecraftTick=0U) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const auto tick=minecraftTick?minecraftTick:m_interactionTick;
        beginPhysicsTickLocked(tick);
        MovementCommand command=movementCommandLocked(
            physicalStrafe,physicalForward,tick,physicalSprinting,true);
        m_state.interactionTick=tick;
        m_debug.event("JUMP_INPUT",m_state);
        publishLocked();
        ReleaseSRWLockExclusive(&m_lock); return command;
    }
    void beginPhysicsTick(const std::uint64_t minecraftTick) noexcept {
        if(!minecraftTick) return;
        AcquireSRWLockExclusive(&m_lock);
        beginPhysicsTickLocked(minecraftTick);
        ReleaseSRWLockExclusive(&m_lock);
    }
    void endMovementPhase(const std::uint64_t minecraftTick) noexcept {
        if(!minecraftTick) return;
        AcquireSRWLockExclusive(&m_lock);
        if(m_physicsPhaseTick!=minecraftTick)
            m_physicsPhaseTick=minecraftTick;
        m_movementPostReached=true;
        m_interactionPreOpen=false;
        ReleaseSRWLockExclusive(&m_lock);
    }
    void beginInteractionPre(const std::uint64_t minecraftTick=0U) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        if(minecraftTick) {
            m_interactionTick=minecraftTick;
            m_nativeTickKnown=true;
            m_physicsPhaseTick=minecraftTick;
        }
        // This method is called only from the transformed Minecraft input
        // entry, which is structurally before the upcoming world movement.
        // ticksExisted may still equal the preceding movement POST here, so the
        // explicit boundary—not a numeric tick comparison—opens dispatch.
        m_movementPostReached=false;
        m_interactionPreOpen=true;
        ReleaseSRWLockExclusive(&m_lock);
    }
    [[nodiscard]] bool arbitrateSprint(
        const bool requested,const std::uint64_t minecraftTick=0U) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        // This hook can precede moveFlying while the entity axes still belong
        // to the previous physics tick.  Record the caller's intent, but never
        // derive a movement decision here.  If the current tick's consumer has
        // already committed a decision, every later caller obeys that one.
        m_sprintIntentRequested=requested;
        const bool silentOwnsSprint=m_rotation.active()&&
            m_state.leftMouseDown;
        // Silent aiming owns sprint independently of movement adaptation.
        // This is a policy veto, not a decision derived from stale entity axes.
        bool allowed=!silentOwnsSprint;
        if(silentOwnsSprint&&m_sprintDecisionOwned&&
           (!minecraftTick||m_sprintDecisionTick==minecraftTick)) {
            allowed=m_sprintDecisionAllowed;
            if(requested&&!allowed) m_sprintSuppressedUntilRelease=true;
        }
        const bool result=requested&&(!silentOwnsSprint||
            (allowed&&!m_sprintSuppressedUntilRelease));
        ReleaseSRWLockExclusive(&m_lock);
        return result;
    }
    // Raw Minecraft observations only. This never overwrites objectMouseOver.
    void observeCameraInput(const aim::Angles camera,const BlockTarget block,
        const int entity,const bool down,const std::uint64_t tick,
        const bool sneaking=false,const bool rightDown=false) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        m_rotation.observeCamera(camera); m_state.camera=camera;
        m_state.cameraBlock=block; m_state.cameraMouseOverEntityId=entity;
        m_state.leftMouseDown=down; m_state.rightMouseDown=rightDown;
        m_state.sneaking=sneaking;
        if(tick) {m_interactionTick=tick;m_nativeTickKnown=true;}
        if(!down) {
            // The transformed input entry observes release earlier than the
            // next render sample. End SilentCombat here so no packet, movement
            // or scheduled click can retain ownership for an extra frame.
            const auto cancelled=m_attackClock.update(false,false,0U,1);
            clearAttackRequirementLocked(cancelled.cancelledIntentId);
            m_sprintDecisionTick=~std::uint64_t{0};
            m_sprintDecisionOwned=false; m_sprintDecisionAllowed=true;
            m_sprintIntentRequested=false;
            m_sprintSuppressedUntilRelease=false;
            m_targetGraceActive=false;
            m_targetAttackReady=false;
            m_outputReady=false;
            if(m_rotation.active()) {
                m_lastRotationReleaseTick=m_state.tick;
                m_rotation.deactivate();
                const InteractionCommand transition=
                    m_interaction.cancel(m_interactionTick);
                if(transition.kind!=InteractionCommandKind::None&&
                   m_pendingInteraction.kind==InteractionCommandKind::None)
                    m_pendingInteraction=transition;
                m_interaction.updateRay(-1,-1,-1,{});
                m_state.logical=camera;
            }
        }
        if(m_manualBlock && (!down || !block.valid || entity>=0)) {
            m_manualBlock=false; m_interaction.blockRejected(m_interactionTick);
        }
        m_state.networkRotation=m_rotation.networkRotation();
        updateInteractionStateLocked(); ReleaseSRWLockExclusive(&m_lock);
    }
    // Called at both actual Java input entries. A block under the CAMERA, not
    // a block under the combat ray, is the only way into ManualBlock.
    [[nodiscard]] bool routeManualInput(const bool down) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        if(!m_coordinateMovement) {
            // Tool/block ownership is part of Silent Control Adaptation. With
            // SCA disabled, an active SilentCombat transaction is never
            // displaced by camera-block heuristics.
            m_manualBlock=false;
            updateInteractionStateLocked();
            ReleaseSRWLockExclusive(&m_lock);
            return false;
        }
        const bool executableCombat=m_rotation.active()&&
            m_state.attackTargetId>=0;
        const bool manualBlockPriority=m_heldItemPolicy==HeldItemPolicy::MiningTool;
        if(down && m_state.cameraBlock.valid &&
           m_state.cameraMouseOverEntityId<0 &&
           (manualBlockPriority||!executableCombat)) {
            m_manualBlock=true;
            const std::uint64_t cancelled=m_attackClock.reset();
            clearAttackRequirementLocked(cancelled);
            (void)m_interaction.cancel(m_interactionTick);
            m_interaction.updateRay(-1,-1,-1,{});
            if(m_rotation.active()) m_lastRotationReleaseTick=m_state.tick;
            m_rotation.deactivate(); m_selector.reset(); m_aim.reset(m_state.camera);
            m_state.candidateTargetId=m_state.rayFirstHitEntityId=m_state.attackTargetId=-1;
            m_state.logical=m_state.camera;
            m_state.networkRotation=m_rotation.networkRotation();
        } else if(!down || !m_state.cameraBlock.valid) m_manualBlock=false;
        const bool passthrough=m_manualBlock;
        updateInteractionStateLocked(); ReleaseSRWLockExclusive(&m_lock);
        return passthrough;
    }
    void manualBlockEnded() noexcept {
        AcquireSRWLockExclusive(&m_lock);
        m_manualBlock=false; m_interaction.blockFinished(m_interactionTick);
        updateInteractionStateLocked(); ReleaseSRWLockExclusive(&m_lock);
    }
    [[nodiscard]] bool manualBlockInputAllowed() const noexcept {
        AcquireSRWLockShared(&m_lock);
        const bool allowed=m_lastCombatTick!=m_interactionTick;
        ReleaseSRWLockShared(&m_lock);return allowed;
    }
    [[nodiscard]] InteractionCommand click() noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const InteractionCommand command=clickLocked();
        ReleaseSRWLockExclusive(&m_lock);
        return command;
    }
    [[nodiscard]] InteractionCommand clickAtInteractionPre(
        const std::uint64_t minecraftTick=0U) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const auto tick=minecraftTick?minecraftTick:m_interactionTick;
        // Only beginInteractionPre(), reached by the transformed input entry,
        // may open this gate. Render/update and movement POST consumers leave
        // the complete target+rotation transaction pending.
        if(!m_interactionPreOpen||m_movementPostReached) {
            const std::uint64_t eventId=m_attackClock.pending();
            if(eventId&&m_lastWaitPreIntent!=eventId) {
                m_lastWaitPreIntent=eventId;
                char detail[144]{};
                std::snprintf(detail,sizeof(detail),
                    "intent=%llu inputTick=%llu postTick=%llu",
                    static_cast<unsigned long long>(eventId),
                    static_cast<unsigned long long>(tick),
                    static_cast<unsigned long long>(m_physicsPhaseTick));
                m_debug.event("ATTACK_WAIT_PRE",m_state,detail,true);
            }
            ReleaseSRWLockExclusive(&m_lock);
            return {};
        }
        m_interactionPreOpen=false;
        if(tick) {m_interactionTick=tick;m_nativeTickKnown=true;}
        const InteractionCommand command=clickLocked();
        ReleaseSRWLockExclusive(&m_lock);
        return command;
    }
private:
    [[nodiscard]] InteractionCommand clickLocked() noexcept {
        const std::uint64_t eventId=m_attackClock.pending();
        if(!eventId) return {};
        const bool requirementReady=m_attackRequiredIntentId==eventId&&
            m_attackRequiredTargetId>=0;
        if(requirementReady&&!m_attackRequiredAvailable) {
            if(m_lastWaitAvailabilityIntent!=eventId) {
                m_lastWaitAvailabilityIntent=eventId;
                char detail[128]{};
                std::snprintf(detail,sizeof(detail),
                    "intent=%llu target=%d state=ARMED_WAIT_AVAILABILITY",
                    static_cast<unsigned long long>(eventId),
                    m_attackRequiredTargetId);
                m_debug.event("ATTACK_WAIT_AVAILABILITY",m_state,detail,true);
            }
            return {};
        }
        const bool published=requirementReady&&
            m_publishedRotationEpoch>=m_attackRequiredRotationEpoch&&
            m_rotation.lastReportedValid()&&
            std::abs(aim::wrap(m_rotation.lastReported().yaw-
                m_attackRequiredRotation.yaw))<0.0005&&
            std::abs(m_rotation.lastReported().pitch-
                m_attackRequiredRotation.pitch)<0.0005;
        if(!published) {
            if(requirementReady&&m_lastWaitRotationIntent!=eventId) {
                m_lastWaitRotationIntent=eventId;
                char detail[176]{};
                std::snprintf(detail,sizeof(detail),
                    "intent=%llu requiredEpoch=%llu publishedEpoch=%llu",
                    static_cast<unsigned long long>(eventId),
                    static_cast<unsigned long long>(m_attackRequiredRotationEpoch),
                    static_cast<unsigned long long>(m_publishedRotationEpoch));
                m_debug.event("ATTACK_WAIT_ROTATION",m_state,detail,true);
            }
            return {};
        }
        (void)m_attackClock.consume();
        m_state.inputEventId=eventId;
        m_interaction.updateRay(m_attackRequiredTargetId,
            m_attackRequiredTargetId,m_attackRequiredTargetId,{});
        InteractionCommand command=m_rotation.active()
            ? m_interaction.click(m_interactionTick) : InteractionCommand{};
        if(command.kind==InteractionCommandKind::AttackEntity) {
            command.intentId=eventId;
            command.rotationEpoch=m_attackRequiredRotationEpoch;
            command.committedRotation=m_attackRequiredRotation;
            command.reach=m_enforceAttack?std::min(3.0,m_attackReach):m_attackReach;
            command.enforceAvailability=m_enforceAttack;
            m_state.attackIntentId=eventId;
            m_state.committedAttackTargetId=command.entityId;
            m_attackDispatchInFlight=true;
            m_lastCombatTick=m_interactionTick;
            if(m_sequentialTargets) rememberAttackLocked(command.entityId,m_state.tick);
        } else {
            char detail[112]{};
            std::snprintf(detail,sizeof(detail),"intent=%llu reason=no_target",
                static_cast<unsigned long long>(eventId));
            m_debug.event("ATTACK_REJECT",m_state,detail,true);
            clearAttackRequirementLocked(eventId);
        }
        updateInteractionStateLocked(); return command;
    }
public:
    void attackDispatched(const InteractionCommand& command,const bool succeeded,
                          const char* const failureReason=nullptr) noexcept {
        if(command.kind!=InteractionCommandKind::AttackEntity) return;
        AcquireSRWLockExclusive(&m_lock);
        if(succeeded) ++m_state.attackDispatchCount;
        if(m_state.attackIntentId==command.intentId)
            m_state.committedAttackTargetId=-1;
        char detail[160]{};
        std::snprintf(detail,sizeof(detail),
            "intent=%llu target=%d success=%d dispatchCount=%llu reason=%s",
            static_cast<unsigned long long>(command.intentId),command.entityId,
            succeeded?1:0,
            static_cast<unsigned long long>(m_state.attackDispatchCount),
            !succeeded&&failureReason ? failureReason : "none");
        m_debug.event(succeeded?"ATTACK_DISPATCH":"ATTACK_FAILED",
            m_state,detail,true);
        clearAttackRequirementLocked(command.intentId);
        updateInteractionStateLocked();
        ReleaseSRWLockExclusive(&m_lock);
    }
    [[nodiscard]] InteractionCommand held(const bool down) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const InteractionCommand command=m_rotation.active()
            ? m_interaction.held(down,m_interactionTick) : InteractionCommand{};
        // Real Minecraft ticks, not render frames or multiple held callbacks,
        // define the interaction epoch. Keep the fallback for isolated callers.
        if(!m_nativeTickKnown) ++m_interactionTick;
        updateInteractionStateLocked(); ReleaseSRWLockExclusive(&m_lock); return command;
    }
    void blockFinished() noexcept {
        AcquireSRWLockExclusive(&m_lock);
        m_interaction.blockFinished(m_interactionTick);
        updateInteractionStateLocked(); ReleaseSRWLockExclusive(&m_lock);
    }
    void blockRejected() noexcept {
        AcquireSRWLockExclusive(&m_lock);
        m_interaction.blockRejected(m_interactionTick);
        updateInteractionStateLocked(); ReleaseSRWLockExclusive(&m_lock);
    }
    void deferInteraction(const InteractionCommand command) noexcept {
        if(command.kind==InteractionCommandKind::None) return;
        AcquireSRWLockExclusive(&m_lock);
        if(m_pendingInteraction.kind==InteractionCommandKind::None)
            m_pendingInteraction=command;
        publishLocked();
        ReleaseSRWLockExclusive(&m_lock);
    }
    [[nodiscard]] InteractionCommand pendingAttack() const noexcept {
        AcquireSRWLockShared(&m_lock);
        InteractionCommand result{};
        if(attackTransactionActiveLocked()&&!m_attackDispatchInFlight&&
           m_state.leftMouseDown&&m_rotation.active()) {
            result.kind=InteractionCommandKind::AttackEntity;
            result.entityId=m_attackRequiredTargetId;
            result.intentId=m_attackRequiredIntentId;
            result.rotationEpoch=m_attackRequiredRotationEpoch;
            result.committedRotation=m_attackRequiredRotation;
            result.reach=m_enforceAttack?std::min(3.0,m_attackReach):m_attackReach;
            result.preAimReach=m_preAimReach;
            result.enforceAvailability=m_enforceAttack;
        }
        ReleaseSRWLockShared(&m_lock);
        return result;
    }
    // Refresh before the first physics consumer; publication may refresh only
    // if there is no adapted movement commit to contradict. Every replacement
    // needs fresh publication proof and keeps the same CPS intent/target.
    bool revisePendingAttack(const InteractionCommand& expected,
                             const aim::Angles rotation,const bool available) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        bool matches=expected.intentId!=0U&&
            m_attackRequiredIntentId==expected.intentId&&
            m_attackRequiredRotationEpoch==expected.rotationEpoch&&
            m_attackRequiredTargetId==expected.entityId&&
            !m_attackDispatchInFlight&&m_state.leftMouseDown&&m_rotation.active();
        if(matches) {
            const bool frozenMovement=!m_interactionPreOpen&&m_coordinateMovement&&
                m_committedMovementTick==m_physicsPhaseTick&&
                m_committedMovement.controlsMinecraftMovement;
            const bool changesCommitted=frozenMovement&&
                (std::abs(aim::wrap(rotation.yaw-m_committedMovement.logicalRotation.yaw))>0.0005||
                 std::abs(rotation.pitch-m_committedMovement.logicalRotation.pitch)>0.0005);
            if(changesCommitted||!std::isfinite(rotation.yaw)||!std::isfinite(rotation.pitch)) {
                // Even when a committed movement snapshot prevents adopting
                // the new angle mid-tick, the old ray is no longer an attack
                // permission. Wait for the next PRE publication.
                m_attackRequiredAvailable=false;
                m_targetAttackReady=false;
                m_state.attackTargetId=-1;
                publishLocked();
                matches=false;
            }
        }
        if(matches) {
            const bool rotationChanged=
                std::abs(aim::wrap(rotation.yaw-m_attackRequiredRotation.yaw))>0.0005||
                std::abs(rotation.pitch-m_attackRequiredRotation.pitch)>0.0005;
            if(rotationChanged) {
                (void)m_rotation.acquire(rotation);
                m_attackRequiredRotation=m_rotation.networkRotation();
                m_attackRequiredRotationEpoch=++m_rotationEpoch;
                m_state.logical=m_rotation.logical();
                m_state.networkRotation=m_attackRequiredRotation;
                m_state.requiredAttackRotationEpoch=m_attackRequiredRotationEpoch;
            }
            const bool availabilityChanged=m_attackRequiredAvailable!=available;
            m_attackRequiredAvailable=available;
            m_targetAttackReady=available;
            m_state.attackTargetId=available?m_attackRequiredTargetId:-1;
            if(available) {
                m_lastWaitAvailabilityIntent=0U;
                if(availabilityChanged||rotationChanged)
                    m_debug.event("ATTACK_GEOMETRY_READY",m_state,
                        rotationChanged?"rotation=refreshed":"rotation=published",true);
            } else if(m_lastWaitAvailabilityIntent!=expected.intentId) {
                m_lastWaitAvailabilityIntent=expected.intentId;
                m_debug.event("ATTACK_WAIT_AVAILABILITY",m_state,
                    rotationChanged?"rotation=refreshed":"rotation=published",true);
            }
            publishLocked();
        }
        ReleaseSRWLockExclusive(&m_lock);
        return matches;
    }
    bool cancelPendingAttack(const InteractionCommand& expected,
                             const char* const reason="target_invalid") noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const bool matches=expected.intentId!=0U&&
            m_attackRequiredIntentId==expected.intentId&&
            m_attackRequiredRotationEpoch==expected.rotationEpoch&&
            m_attackRequiredTargetId==expected.entityId&&!m_attackDispatchInFlight;
        if(matches) {
            (void)m_attackClock.consume();
            char detail[160]{};
            std::snprintf(detail,sizeof(detail),"intent=%llu target=%d reason=%s",
                static_cast<unsigned long long>(expected.intentId),expected.entityId,
                reason?reason:"target_invalid");
            m_debug.event("ATTACK_CANCELLED",m_state,detail,true);
            clearAttackRequirementLocked(expected.intentId);
            publishLocked();
        }
        ReleaseSRWLockExclusive(&m_lock);
        return matches;
    }
    [[nodiscard]] PacketSerializationPlan packetPlan(
        const bool hasPosition,const bool hasRotation,
        const aim::Angles originalRotation={},
        const bool originalRotationValid=false) const noexcept {
        static_cast<void>(hasPosition);
        AcquireSRWLockShared(&m_lock);
        PacketSerializationPlan result;
        const bool vanillaHandoff=m_rotation.handoffPending()&&hasRotation&&
            originalRotationValid;
        const bool vanillaContinuity=m_rotation.owner()==RotationOwner::Camera&&
            m_rotation.branchManaged()&&hasRotation&&originalRotationValid;
        const bool attackTransaction=attackTransactionActiveLocked();
        if(m_rotation.active()||
           m_rotation.restorePacketPending()||m_cameraResyncPending||
           vanillaHandoff||vanillaContinuity) {
            result.rotation=(vanillaHandoff||vanillaContinuity)
                ? m_rotation.continuousVanillaRotation(originalRotation)
                : (m_cameraResyncPending||m_rotation.restorePacketPending())
                    ? m_rotation.cameraNetworkRotation()
                    : attackTransaction
                        ? m_attackRequiredRotation:m_state.networkRotation;
            result.restoring=m_rotation.restorePacketPending()||
                m_cameraResyncPending;
            result.vanillaHandoff=vanillaHandoff;
            // Duplicate suppression applies to every managed owner: Silent,
            // camera resync, restore and the vanilla handoff packet.
            const bool unchanged=m_rotation.lastReportedValid()&&
                std::abs(result.rotation.yaw-m_rotation.lastReported().yaw)<0.0005&&
                std::abs(result.rotation.pitch-m_rotation.lastReported().pitch)<0.0005;
            result.mutation=unchanged
                ? (hasRotation?PacketMutation::RemoveRotation:PacketMutation::Pass)
                : PacketMutation::InjectRotation;
        }
        ReleaseSRWLockShared(&m_lock); return result;
    }
    void acknowledgePacket(const aim::Angles reported,const PacketKind original,
                           const PacketKind replacement,const bool hasPosition,
                           const bool hasRotation) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        const bool restorePacket=m_rotation.restorePacketPending();
        const bool vanillaHandoff=m_rotation.handoffPending();
        m_rotation.acknowledgeReported(reported);
        if(m_attackRequiredIntentId&&
           std::abs(aim::wrap(reported.yaw-m_attackRequiredRotation.yaw))<0.0005&&
           std::abs(reported.pitch-m_attackRequiredRotation.pitch)<0.0005) {
            m_publishedRotationEpoch=std::max(
                m_publishedRotationEpoch,m_attackRequiredRotationEpoch);
        } else if(m_rotation.active()) {
            m_publishedRotationEpoch=m_rotationEpoch;
        }
        m_cameraResyncPending=false;
        m_state.lastReported=m_rotation.lastReported(); m_state.originalPacket=original;
        m_state.replacementPacket=replacement; m_state.hasPosition=hasPosition;
        m_state.hasRotation=hasRotation;
        m_state.publishedRotationEpoch=m_publishedRotationEpoch;
        if(restorePacket) m_rotation.beginVanillaHandoff();
        else if(vanillaHandoff) m_rotation.finishVanillaHandoff();
        m_state.logical=m_rotation.logical();
        m_state.previousLogical=m_rotation.previousLogical();
        m_state.networkRotation=m_rotation.networkRotation();
        m_state.rotationOwner=m_rotation.owner(); publishLocked();
        ReleaseSRWLockExclusive(&m_lock);
    }
    void acknowledgeSuppressedPacket(
        const PacketKind original,const PacketKind replacement,
        const bool hasPosition) noexcept {
        AcquireSRWLockExclusive(&m_lock);
        // An unchanged restore needs no Look packet, but it still advances to
        // the vanilla-handoff phase.  An unchanged first vanilla Look remains
        // suppressed and keeps ownership until a real camera change arrives.
        if(m_rotation.restorePacketPending()) m_rotation.beginVanillaHandoff();
        if(m_rotation.active()&&m_rotation.lastReportedValid()&&
           m_attackRequiredIntentId&&
           std::abs(aim::wrap(m_rotation.lastReported().yaw-
               m_attackRequiredRotation.yaw))<0.0005&&
           std::abs(m_rotation.lastReported().pitch-
               m_attackRequiredRotation.pitch)<0.0005)
            m_publishedRotationEpoch=std::max(
                m_publishedRotationEpoch,m_attackRequiredRotationEpoch);
        m_cameraResyncPending=false;
        m_state.lastReported=m_rotation.lastReported();
        m_state.originalPacket=original;
        m_state.replacementPacket=replacement;
        m_state.hasPosition=hasPosition;
        m_state.hasRotation=false;
        m_state.publishedRotationEpoch=m_publishedRotationEpoch;
        m_state.logical=m_rotation.logical();
        m_state.previousLogical=m_rotation.previousLogical();
        m_state.networkRotation=m_rotation.networkRotation();
        m_state.rotationOwner=m_rotation.owner();
        publishLocked();
        ReleaseSRWLockExclusive(&m_lock);
    }
    void deactivate() noexcept {
        AcquireSRWLockExclusive(&m_lock);
        if(m_rotation.active()) m_lastRotationReleaseTick=m_state.tick;
        m_rotation.deactivate();
        m_outputReady=false; m_targetAttackReady=false;
        m_targetGraceActive=false;
        m_cameraResyncPending=false;
        m_manualBlock=false; m_lastMovementTick=~std::uint64_t{0};
        m_physicsPhaseTick=~std::uint64_t{0}; m_movementPostReached=false;
        m_interactionPreOpen=false;
        m_sprintDecisionTick=~std::uint64_t{0};
        m_sprintDecisionOwned=false; m_sprintDecisionAllowed=true;
        m_sprintIntentRequested=false; m_sprintSuppressedUntilRelease=false;
        (void)m_attackClock.reset();
        clearAttackRequirementLocked();
        m_selector.reset(); m_aim.reset(m_rotation.camera());
        const InteractionCommand transition=m_interaction.cancel(m_interactionTick);
        if(transition.kind!=InteractionCommandKind::None)
            m_pendingInteraction=transition;
        m_interaction.updateRay(-1,-1,-1,{});
        m_state.candidateTargetId=m_state.rayFirstHitEntityId=m_state.attackTargetId=-1;
        m_state.rayBlock={}; m_state.networkRotation=m_rotation.networkRotation();
        m_state.interaction=m_interaction.mode(); m_state.event=m_interaction.event();
        m_state.rotationOwner=m_rotation.owner(); publishLocked();
        ReleaseSRWLockExclusive(&m_lock);
    }
    [[nodiscard]] bool requiresDrain() const noexcept {
        AcquireSRWLockShared(&m_lock);
        const bool value=m_rotation.active()||m_rotation.restoring()||
            m_cameraResyncPending||
            m_pendingInteraction.kind!=InteractionCommandKind::None;
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] bool active() const noexcept {
        AcquireSRWLockShared(&m_lock); const bool value=m_rotation.active();
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] bool restoring() const noexcept {
        AcquireSRWLockShared(&m_lock); const bool value=m_rotation.restoring();
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] bool packetContinuityRequired() const noexcept {
        AcquireSRWLockShared(&m_lock);
        const bool value=m_rotation.branchManaged();
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] int targetId() const noexcept {
        AcquireSRWLockShared(&m_lock); const int value=m_state.candidateTargetId;
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] int attackTargetId() const noexcept {
        AcquireSRWLockShared(&m_lock); const int value=m_state.attackTargetId;
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] bool outputReady() const noexcept {
        AcquireSRWLockShared(&m_lock); const bool value=m_outputReady;
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] int localPlayerId() const noexcept {
        AcquireSRWLockShared(&m_lock); const int value=m_localPlayer;
        ReleaseSRWLockShared(&m_lock); return value;
    }
    [[nodiscard]] LogicalTickState latest() const noexcept { return m_observer.latest(); }
private:
    struct RecentAttack final { int entityId=-1; std::uint64_t readyAt=0U; };
    static constexpr std::uint64_t TargetLossGraceMilliseconds=50U;
    [[nodiscard]] bool attackTransactionActiveLocked() const noexcept {
        return m_attackRequiredIntentId!=0U&&
            (m_attackRequiredIntentId==m_attackClock.pending()||
             m_attackDispatchInFlight)&&
            m_attackRequiredTargetId>=0;
    }
    void clearAttackRequirementLocked(
        const std::uint64_t intentId=0U) noexcept {
        if(intentId&&m_attackRequiredIntentId!=intentId) return;
        m_attackRequiredIntentId=0U;
        m_attackRequiredTargetId=-1;
        m_attackRequiredRotation={};
        m_attackRequiredRotationEpoch=0U;
        m_attackRequiredAvailable=false;
        m_lastWaitRotationIntent=0U;
        m_lastWaitAvailabilityIntent=0U;
        m_lastWaitPreIntent=0U;
        m_attackDispatchInFlight=false;
        m_state.requiredAttackRotationEpoch=0U;
    }
    [[nodiscard]] bool recentlyAttackedLocked(
        const int entityId,const std::uint64_t tick) const noexcept {
        return std::any_of(m_recentAttacks.begin(),m_recentAttacks.end(),
            [&](const RecentAttack& entry) noexcept {
                return entry.entityId==entityId&&tick<entry.readyAt;
            });
    }
    void rememberAttackLocked(const int entityId,const std::uint64_t tick) noexcept {
        RecentAttack* slot=&m_recentAttacks[0];
        for(RecentAttack& entry:m_recentAttacks) {
            if(entry.entityId==entityId) {slot=&entry;break;}
            if(entry.entityId<0||entry.readyAt<=tick||entry.readyAt<slot->readyAt)
                slot=&entry;
        }
        *slot={entityId,tick+520U};
    }
    [[nodiscard]] MovementCommand movementCommandLocked(
        const double physicalStrafe,const double physicalForward,
        const std::uint64_t tick,const bool observedSprinting=false,
        const bool sprintObserved=false) noexcept {
        const bool physicalSprinting=sprintObserved
            ? observedSprinting:m_state.movement.physicalSprinting;
        if(m_committedMovementTick!=tick) {
            m_committedMovement=m_movement.coordinateAxes(
                physicalForward,physicalStrafe,m_state.camera,m_state.logical,
                m_state.movement.currentVelocity,physicalSprinting,
                m_state.movement.onGround,tick);
            m_committedMovement.physicsTick=tick;
            m_committedMovement.snapshotVersion=++m_movementSnapshotVersion;
            m_committedMovement.cameraRotation=m_state.camera;
            m_committedMovement.logicalRotation=m_state.logical;
            m_committedMovementTick=tick;
            m_state.movement=m_committedMovement;
            char detail[180]{};
            std::snprintf(detail,sizeof(detail),
                "consumer=PHYSICS snapshot=%llu tick=%llu axes=(%.4f,%.4f)",
                static_cast<unsigned long long>(m_movementSnapshotVersion),
                static_cast<unsigned long long>(tick),physicalForward,physicalStrafe);
            m_debug.event("MOVE_COMMIT",m_state,detail);
        }
        // A render-frame publication may occur between two consumers in the
        // same Minecraft physics tick. Re-publish the already committed
        // movement snapshot so state observers and every consumer retain the
        // identical logical yaw/version for that tick.
        m_state.movement=m_committedMovement;
        const bool silentOwnsSprint=commitSprintDecisionLocked(
            tick,physicalSprinting);
        MovementCommand command;
        command.enabled=m_rotation.active()&&m_coordinateMovement&&
                        m_committedMovement.controlsMinecraftMovement;
        command.physicalStrafe=physicalStrafe;
        command.physicalForward=physicalForward;
        command.cameraRotation=m_committedMovement.cameraRotation;
        command.strafe=m_committedMovement.strafe;
        command.forward=m_committedMovement.forward;
        command.logicalRotation=m_committedMovement.logicalRotation;
        command.logicalTick=tick;
        command.snapshotVersion=m_committedMovement.snapshotVersion;
        command.physicalSprinting=physicalSprinting;
        // Veto only. A stale render snapshot can never manufacture sprint;
        // an observed vanilla sprint may only be suppressed by remapped input.
        command.sprinting=physicalSprinting&&
            (silentOwnsSprint?m_sprintDecisionAllowed:
                              m_committedMovement.sprinting);
        m_lastMovementTick=tick; m_movementCommand=command;
        return command;
    }
    void beginPhysicsTickLocked(const std::uint64_t tick) noexcept {
        m_interactionPreOpen=false;
        if(!tick||m_physicsPhaseTick==tick) return;
        m_physicsPhaseTick=tick;
        m_movementPostReached=false;
        // The next setSprinting/movement consumer freezes the new tick's
        // decision. It may be called before moveFlying commits its snapshot.
        m_sprintDecisionTick=~std::uint64_t{0};
        m_sprintDecisionOwned=false;
        m_sprintDecisionAllowed=true;
    }
    [[nodiscard]] bool commitSprintDecisionLocked(
        const std::uint64_t tick,const bool physicalSprinting) noexcept {
        const bool owns=m_rotation.active()&&
            m_state.leftMouseDown;
        if(!owns) return false;
        if(m_sprintDecisionOwned&&m_sprintDecisionTick==tick) return true;
        // Only an actual jump/moveFlying consumer reaches this point, after
        // movementCommandLocked has committed the current tick's snapshot.
        // No render sample or setSprinting call may preview/recompute it.
        static_cast<void>(physicalSprinting);
        m_sprintSuppressedUntilRelease=true;
        m_sprintDecisionTick=tick;
        m_sprintDecisionOwned=true;
        m_sprintDecisionAllowed=false;
        return true;
    }
    void updateInteractionStateLocked() noexcept {
        m_state.interactionTick=m_interactionTick;
        m_state.attackTargetId=m_interaction.attackTarget();
        m_state.interaction=m_interaction.mode(); m_state.event=m_interaction.event();
        publishLocked();
    }
    void publishLocked() noexcept {
        m_state.interaction=m_manualBlock ? InteractionMode::Block : m_interaction.mode();
        m_state.interactionOwner=m_manualBlock ? InteractionOwner::ManualBlock :
            m_rotation.active()&&m_state.leftMouseDown
                ? InteractionOwner::SilentCombat :
            InteractionOwner::None;
        m_state.rotationOwner=m_rotation.owner();
        m_state.restorePending=m_rotation.restoring();
        if(m_debug.enabled()) {
            const auto old=m_observer.latest();
            if(old.interactionOwner!=m_state.interactionOwner) {
                char detail[100]{};
                std::snprintf(detail,sizeof(detail),"old=%s new=%s",
                    SilentDebugRecorder::owner(static_cast<unsigned>(old.interactionOwner)),
                    SilentDebugRecorder::owner(static_cast<unsigned>(m_state.interactionOwner)));
                m_debug.event("OWNER_CHANGE",m_state,detail,true);
                if(m_state.interactionOwner==InteractionOwner::ManualBlock)
                    m_debug.event("MANUAL_BLOCK_BEGIN",m_state,"",true);
                if(old.interactionOwner==InteractionOwner::ManualBlock)
                    m_debug.event("MANUAL_BLOCK_END",m_state,"",true);
                if(m_state.interactionOwner==InteractionOwner::SilentCombat)
                    m_debug.event("SILENT_ACQUIRE",m_state,"",true);
                if(old.interactionOwner==InteractionOwner::SilentCombat)
                    m_debug.event("SILENT_RELEASE",m_state,"",true);
            }
            if(!old.restorePending && m_state.restorePending)
                m_debug.event("RESTORE_BEGIN",m_state,"",true);
            if(m_state.event==InteractionEvent::Attack &&
               (old.event!=m_state.event || old.interactionTick!=m_state.interactionTick))
                m_debug.event("LOGICAL_ATTACK",m_state,"",true);
            if(m_lastDebugTick!=m_state.interactionTick) {
                m_lastDebugTick=m_state.interactionTick;
                m_debug.event("ROTATION_PUBLISH",m_state);
            }
        }
        m_observer.publish(m_state);
    }
    mutable SRWLOCK m_lock=SRWLOCK_INIT;
    TargetSelector m_selector;
    AimController m_aim;
    RotationManager m_rotation;
    MovementCoordinator m_movement;
    RayTraceCoordinator m_rayTrace;
    InteractionCoordinator m_interaction;
    FixedCpsAttackScheduler m_attackClock;
    PacketObserver m_observer;
    SilentDebugRecorder m_debug;
    std::uint64_t m_lastDebugTick=~std::uint64_t{0};
    LogicalTickState m_state{};
    RayTraceCoordinator::EntityHit m_entityHit{};
    InteractionCommand m_pendingInteraction{};
    std::uint64_t m_interactionTick=1U;
    std::uint64_t m_lastMovementTick=~std::uint64_t{0};
    std::uint64_t m_nextInputEventId=0U;
    std::uint64_t m_committedMovementTick=~std::uint64_t{0};
    std::uint64_t m_movementSnapshotVersion=0U;
    std::uint64_t m_physicsPhaseTick=~std::uint64_t{0};
    std::uint64_t m_sprintDecisionTick=~std::uint64_t{0};
    std::uint64_t m_lastCombatTick=~std::uint64_t{0};
    std::uint64_t m_lastTargetSeenTick=0U;
    std::uint64_t m_lastRotationReleaseTick=~std::uint64_t{0};
    std::uint64_t m_rotationEpoch=0U;
    std::uint64_t m_publishedRotationEpoch=0U;
    std::uint64_t m_attackRequiredIntentId=0U;
    std::uint64_t m_attackRequiredRotationEpoch=0U;
    std::uint64_t m_lastWaitRotationIntent=0U;
    std::uint64_t m_lastWaitAvailabilityIntent=0U;
    std::uint64_t m_lastWaitPreIntent=0U;
    int m_attackRequiredTargetId=-1;
    aim::Angles m_attackRequiredRotation{};
    MovementCommand m_movementCommand{};
    LogicalMovementState m_committedMovement{};
    std::array<RecentAttack,16U> m_recentAttacks{};
    bool m_manualBlock=false,m_nativeTickKnown=false,m_targetGraceActive=false;
    std::uint64_t m_world=0U;
    int m_localPlayer=-1;
    bool m_silent=false,m_enforceAttack=true,m_sequentialTargets=false;
    double m_attackReach=3.0;
    double m_preAimReach=3.5;
    HeldItemPolicy m_heldItemPolicy=HeldItemPolicy::Other;
    bool m_coordinateMovement=true;
    bool m_movementPostReached=false;
    bool m_interactionPreOpen=false;
    bool m_sprintDecisionOwned=false;
    bool m_sprintDecisionAllowed=true;
    bool m_sprintIntentRequested=false;
    bool m_sprintSuppressedUntilRelease=false;
    bool m_attackDispatchInFlight=false;
    bool m_attackRequiredAvailable=false;
    bool m_outputReady=false,m_targetAttackReady=false,m_cameraResyncPending=false;
};

} // namespace mcoverlay::silent
