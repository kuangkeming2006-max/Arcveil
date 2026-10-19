#include "../agent/bindings/AimTargetPolicy.h"
#include "../agent/bindings/SilentLockCoordinator.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using mcoverlay::aim::AimTargetFilter;
using namespace mcoverlay::silent;

// The production policy consumes the value-only EntityMarker contract. These
// fixtures intentionally need neither JNI nor a particular Minecraft version.
struct Marker final {
    Bounds bounds{-.3,0,2.2,.3,1.9,2.8};
    double previousX=0, previousY=0, previousZ=2.5;
    double currentX=0, currentY=0, currentZ=2.5;
    int entityId=42;
    float health=20;
    bool player=true, fireball=false, confirmedPlayer=false;
    char teamColor='u';
    std::array<char,17> playerName{};
    std::array<char,65> displayName{};
    std::array<char,37> uuid{};
};

int checks=0, failures=0;

void check(const bool ok,const char* const description) {
    ++checks;
    if(!ok) {++failures;std::printf("FAIL %s\n",description);}
}

bool forceEligible(const Marker& marker) {
    return mcoverlay::aim::targetCandidateEligible(
        AimTargetFilter::Force,marker,1,true,false,'c');
}

TargetCandidate forcedCandidate(const Marker& marker) {
    return {marker.entityId,static_cast<std::uint64_t>(marker.entityId),
        {marker.currentX,1.62,marker.currentZ},marker.bounds,
        forceEligible(marker),false,true};
}

LogicalFrameInput forceInput(const TargetCandidate& target) {
    LogicalFrameInput input{};
    input.tick=1000;input.physicsTick=20;input.worldGeneration=1;
    input.localEntityId=1;input.enabled=input.silent=input.leftMouseDown=true;
    input.coordinateMovement=true;input.mode=mcoverlay::aim::Mode::LockOn;
    input.targetFilter=AimTargetFilter::Force;
    input.maximumDistance=3.5;input.attackReach=3;input.fovDegrees=360;
    input.enforceAttackAvailability=true;input.eye={0,1.62,0};
    input.candidates={&target,1};
    return input;
}

constexpr auto clearTrace=[](const LogicalFramePlan&) noexcept {
    return BlockRayHit{true,-1,{}};
};

void normalCompatibility() {
    using namespace mcoverlay::aim;
    // This is the pre-change expression, including its intentionally looser
    // confirmed-player branch and exact historical unknown-colour handling.
    for(bool hypixel:{false,true}) for(bool match:{false,true})
    for(bool player:{false,true}) for(bool confirmed:{false,true})
    for(bool fireball:{false,true}) for(char team:{'u','\0','c','9'})
    for(char own:{'u','\0','c','9'}) {
        Marker marker{};marker.player=player;marker.confirmedPlayer=confirmed;
        marker.fireball=fireball;marker.teamColor=team;
        const bool coloured=player&&team!='u'&&team!='\0';
        const bool validPlayer=confirmed||(match&&coloured)||(!hypixel&&player);
        const bool teammate=hypixel&&match&&own!='u'&&team==own;
        const bool expected=validPlayer&&!teammate;
        check(targetIdentityEligible(AimTargetFilter::Normal,marker,
            hypixel,match,own)==expected,
            "Normal preserves the exact existing identity and team expression");
        check(targetCandidateEligible(AimTargetFilter::Normal,marker,1,
            hypixel,match,own)==expected,
            "Normal preserves existing candidate qualification");
    }
    Marker marker{};marker.confirmedPlayer=true;
    marker.bounds.maxX=marker.bounds.minX;
    check(targetCandidateEligible(AimTargetFilter::Normal,marker,1,true,false,'u'),
        "Normal leaves live combat-bounds validation in its existing stage");
    check(!forceEligible(marker),"Force requires an effective collision box");
}

void forceIdentityAndHitboxes() {
    using namespace mcoverlay::aim;
    for(bool hypixel:{false,true}) for(bool match:{false,true})
    for(bool confirmed:{false,true}) for(char team:{'u','\0','c','9'})
    for(char own:{'u','\0','c','9'}) {
        Marker marker{};marker.confirmedPlayer=confirmed;marker.teamColor=team;
        check(playerEspHitboxEligible(marker,1),"fixture has a displayable player Hitbox");
        check(targetIdentityEligible(AimTargetFilter::Force,marker,hypixel,match,own),
            "Force ignores TAB confirmation, match recognition and team metadata");
        check(targetCandidateEligible(AimTargetFilter::Force,marker,1,hypixel,match,own),
            "every valid Player ESP Hitbox has Force candidate qualification");
    }

    Marker nickname{};
    nickname.playerName={'E','n','t','i','t','y','A','l','i','a','s'};
    nickname.displayName={'N','i','c','k','n','a','m','e'};
    nickname.uuid={'m','i','s','m','a','t','c','h'};
    check(!targetIdentityEligible(AimTargetFilter::Normal,nickname,true,false,'c')&&
        forceEligible(nickname),
        "unconfirmed nickname and mismatched UUID remain eligible in an unknown Hypixel game");
    nickname.playerName={};nickname.displayName={};nickname.uuid={};
    check(forceEligible(nickname),"Force does not require a readable name or UUID");
    nickname.teamColor='c';
    check(!targetCandidateEligible(AimTargetFilter::Normal,nickname,1,true,true,'c')&&
        targetCandidateEligible(AimTargetFilter::Force,nickname,1,true,true,'c'),
        "same-colour teammates remain eligible only when Force is requested");
    check(forceEligible(nickname),
        "a player-shaped NPC drawn by Player ESP uses that same eligibility without a TAB join");

    Marker mob{};mob.player=false;mob.confirmedPlayer=true;mob.teamColor='c';
    mob.playerName={'P','l','a','y','e','r'};
    check(!playerEspHitboxEligible(mob,1)&&!forceEligible(mob),
        "a living mob cannot gain player eligibility from a name, colour or roster metadata");
    Marker projectile{};projectile.fireball=true;
    check(!forceEligible(projectile),"projectile Hitboxes never qualify as Player ESP Hitboxes");

    Marker invalid{};
    invalid.entityId=1;check(!forceEligible(invalid),"Force excludes the local entity");
    invalid.entityId=-1;check(!forceEligible(invalid),"Force excludes an invalid entity ID");
    invalid=Marker{};invalid.health=0;check(!forceEligible(invalid),"Force excludes dead entities");
    invalid.health=-1;check(!forceEligible(invalid),"Force excludes invalid negative health");
    invalid.health=std::numeric_limits<float>::quiet_NaN();
    check(!forceEligible(invalid),"Force excludes unreadable health");
    invalid.health=std::numeric_limits<float>::infinity();
    check(!forceEligible(invalid),"Force excludes non-finite health");

    constexpr double Marker::* positions[]={&Marker::currentX,&Marker::currentY,
        &Marker::currentZ,&Marker::previousX,&Marker::previousY,&Marker::previousZ};
    for(auto member:positions) for(double bad:{std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()}) {
        invalid=Marker{};invalid.*member=bad;
        check(!forceEligible(invalid),"Force excludes unreliable current or interpolated positions");
    }
    constexpr double Bounds::* coordinates[]={&Bounds::minX,&Bounds::minY,
        &Bounds::minZ,&Bounds::maxX,&Bounds::maxY,&Bounds::maxZ};
    for(auto member:coordinates) {
        invalid=Marker{};invalid.bounds.*member=std::numeric_limits<double>::quiet_NaN();
        check(!forceEligible(invalid),"Force excludes each non-finite Hitbox coordinate");
    }
    for(unsigned axis=0;axis<3;++axis) for(bool inverted:{false,true}) {
        invalid=Marker{};
        invalid.bounds.*coordinates[axis+3]=invalid.bounds.*coordinates[axis]-(inverted?1:0);
        check(!forceEligible(invalid),"Force excludes collapsed and inverted collision boxes on every axis");
    }
}

void unchangedGeometryAndInteraction() {
    using namespace mcoverlay::aim;
    const Vec3 eye{0,1.62,0};
    Marker marker{};
    auto candidate=forcedCandidate(marker);
    check(candidate.eligible,"unconfirmed Force player enters the shared selector");
    TargetSelector selector;
    auto select=[&](double minimum,double maximum,double fov) {
        selector.reset();
        return selector.select({&candidate,1},eye,{0,0},minimum,maximum,
            fov*.5,fov*.5,true,false,true);
    };
    check(select(0,3.5,90).valid,"Force uses the existing reachable forward candidate");
    check(!select(2.4,3.5,90).valid,"Force respects minimum eye-to-Hitbox distance");
    check(!select(0,2.1,90).valid,"Force respects maximum eye-to-Hitbox distance");

    marker.currentX=marker.previousX=2.5;marker.currentZ=marker.previousZ=0;
    marker.bounds={2.2,0,-.3,2.8,1.9,.3};candidate=forcedCandidate(marker);
    check(candidate.eligible&&!select(0,3.5,90).valid,
        "Force identity qualification cannot bypass the configured FOV");

    marker=Marker{};candidate=forcedCandidate(marker);
    const auto blocked=[](Vec3,double) {return BlockRayHit{true,.2,{}};};
    const auto failed=[](Vec3,double) {return BlockRayHit{};};
    for(bool failure:{false,true}) {
        const auto point=chooseCombatAimPoint(eye,candidate.bounds,{0,0},3,true,
            [&](Vec3 direction,double limit) {return failure?failed(direction,limit):blocked(direction,limit);});
        candidate.aimPoint=point.point;candidate.attackAvailable=point.available;
        const auto chosen=select(0,3.5,360);
        check(candidate.eligible&&chosen.valid&&!chosen.attackReady,
            "Force blocked or failed rays retain only passive pre-aim, never an attack-ready selection");
        LogicalStateController controller;controller.reset({0,0});
        auto input=forceInput(candidate);
        const auto plan=controller.advance(input,[&](const LogicalFramePlan&) {
            return failure?BlockRayHit{}:BlockRayHit{true,.2,{}};
        });
        check(plan.candidateTargetId==marker.entityId&&controller.attackTargetId()<0,
            "Force candidate still passes through the shared block-ray and final attack checks");
    }

    marker=Marker{};marker.currentZ=marker.previousZ=3.3;
    marker.bounds.minZ=3.01;marker.bounds.maxZ=3.6;
    candidate=forcedCandidate(marker);
    const auto beyond=chooseCombatAimPoint(eye,candidate.bounds,{0,0},3,true,
        [](Vec3,double) {return BlockRayHit{true,-1,{}};});
    candidate.aimPoint=beyond.point;candidate.attackAvailable=beyond.available;
    check(select(0,3.5,360).valid&&!select(0,3.5,360).attackReady,
        "Force can pre-aim inside selection range without gaining attack reach");
    LogicalStateController beyondController;beyondController.reset({0,0});
    const auto beyondPlan=beyondController.advance(forceInput(candidate),clearTrace);
    check(beyondPlan.candidateTargetId==marker.entityId&&beyondController.attackTargetId()<0,
        "the final ray does not attack a Force target beyond legal reach");

    RayTraceCoordinator ray;
    marker=Marker{};candidate=forcedCandidate(marker);
    const auto hit=ray.traceEntities({&candidate,1},candidate.entityId,eye,{0,0,1},3);
    check(RayTraceCoordinator::attackTarget(candidate.entityId,hit,{true,-1,{}},true)==marker.entityId,
        "a clear reachable Force Hitbox passes the unchanged final validation");
    check(RayTraceCoordinator::attackTarget(candidate.entityId,hit,{true,1,{}},true)<0&&
        RayTraceCoordinator::attackTarget(candidate.entityId,hit,{},true)<0,
        "final validation rejects both block obstruction and unavailable ray queries");
    auto intercepted=hit;intercepted.first=99;
    check(RayTraceCoordinator::attackTarget(candidate.entityId,intercepted,{true,-1,{}},true)<0,
        "Force cannot attack through a different entity hit first by the ray");
}

void filterChangeReleasesTransactions() {
    using namespace mcoverlay::aim;
    for(bool remainsEligible:{false,true}) {
        Marker marker{};marker.confirmedPlayer=remainsEligible;
        const auto candidate=forcedCandidate(marker);
        LogicalStateController controller;controller.reset({0,0});
        auto input=forceInput(candidate);
        (void)controller.advance(input,clearTrace);
        controller.updateAttackClock(true,true,1000000,10);
        (void)controller.advance(input,clearTrace);
        const auto armed=controller.pendingAttack();
        check(armed.intentId!=0&&armed.entityId==marker.entityId,
            "Force owns a real pending CPS attack before switching policy");
        const auto publication=controller.packetPlan(true,false);
        controller.acknowledgePacket(publication.rotation,PacketKind::Position,
            PacketKind::PositionLook,true,true);

        input.targetFilter=AimTargetFilter::Normal;++input.tick;++input.physicsTick;
        if(!remainsEligible) input.candidates={};
        const auto changed=controller.advance(input,clearTrace);
        check(controller.pendingAttack().intentId==0,
            "Force to Normal cancels the previous filter's immutable attack intent");
        check(!controller.revisePendingAttack(armed,armed.committedRotation,true),
            "a stale Force attack revision cannot restore a cancelled transaction");
        if(!remainsEligible) {
            check(changed.candidateTargetId<0&&controller.attackTargetId()<0&&!changed.silentActive,
                "Normal never retains the old unconfirmed target through target-loss grace");
            check(!controller.movementCommand(.2,.8,input.physicsTick).enabled,
                "Normal releases movement ownership when the Force target becomes ineligible");
        } else {
            controller.updateAttackClock(true,true,1100000,10);
            ++input.tick;++input.physicsTick;
            (void)controller.advance(input,clearTrace);
            const auto rearmed=controller.pendingAttack();
            check(rearmed.intentId!=0&&rearmed.intentId!=armed.intentId,
                "a still-eligible Normal target requires a new CPS intent after the switch");
        }

        input.targetFilter=AimTargetFilter::Force;input.candidates={&candidate,1};
        ++input.tick;++input.physicsTick;
        const auto forcedAgain=controller.advance(input,clearTrace);
        check(controller.pendingAttack().intentId==0&&forcedAgain.candidateTargetId==marker.entityId,
            "Normal to Force reacquires with a fresh target state and no old attack intent");
    }
}

} // namespace

int main() {
    using namespace mcoverlay::aim;
    check(validTargetFilter(0)&&validTargetFilter(1)&&!validTargetFilter(-1)&&
        !validTargetFilter(2),"only the two documented target filter values are valid");
    normalCompatibility();
    forceIdentityAndHitboxes();
    unchangedGeometryAndInteraction();
    filterChangeReleasesTransactions();
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
