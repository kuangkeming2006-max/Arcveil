#include "../agent/bindings/AimControl.h"
#include "../agent/bindings/SilentLockCoordinator.h"
#include "../agent/bindings/SmartHotbarPolicy.h"
#include <cstdio>
#include <cmath>

int main()
{
    using namespace mcoverlay::aim;
    int checks=0, failed=0;
    auto check=[&](bool ok,const char* message) {
        ++checks; if(!ok) { ++failed; std::printf("FAIL %s\n",message); }
    };
    {
        using namespace mcoverlay::silent;
        const Vec3 eye{0,1.62,0};
        const Bounds box{-0.3,0,2.6,0.3,1.8,3.2};
        const auto open=[](Vec3,double){BlockRayHit b{};b.querySucceeded=true;return b;};
        const auto coveredHead=[](Vec3 direction,double){
            BlockRayHit b{};b.querySucceeded=true;
            // A slab obscures rays above chest height halfway to the target.
            if(1.62+direction.y*(1.3/direction.z)>1.48) b.distance=1.3;
            return b;
        };
        const auto chest=chooseCombatAimPoint(eye,box,{0,0},3,true,coveredHead);
        check(chest.available&&chest.point.y<1.34,
            "head cover still finds a reachable exposed body point");
        const auto blocked=[](Vec3,double){BlockRayHit b{};b.querySucceeded=true;b.distance=.2;return b;};
        const auto hidden=chooseCombatAimPoint(eye,box,{0,0},3,true,blocked);
        check(!hidden.available&&std::isfinite(hidden.point.y),
            "fully blocked target keeps finite aim fallback without attack availability");
        int traces=0;
        auto unchecked=chooseCombatAimPoint(eye,box,{0,0},3,false,
            [&](Vec3,double){++traces;return BlockRayHit{};});
        check(unchecked.available&&traces==0,"availability OFF never invokes strict block gate");
        const Bounds edge{-0.3,0,2.999,0.3,1.8,3.599};
        check(chooseCombatAimPoint(eye,edge,{0,0},3,true,open).available,
            "three-block edge uses box entry instead of center distance");
        const Bounds outsideReach{-0.3,0,3.001,0.3,1.8,3.601};
        check(!chooseCombatAimPoint(eye,outsideReach,{0,0},3,true,open).available,
            "geometry outside legal reach is unavailable");
        const auto ray=RayTraceCoordinator::direction({0,0});
        check(RayTraceCoordinator::intersect(eye,ray,box,3)>=0&&
            RayTraceCoordinator::intersect({0,2.2,0},ray,box,3)<0,
            "jump rejects previously published rotation against current physics eye");
        check(RayTraceCoordinator::intersect(eye,ray,{1,0,2.6,1.6,1.8,3.2},3)<0,
            "target movement invalidates old ray without rewriting its transaction");
    }
    {
        using mcoverlay::silent::RuntimeCapabilities;
        constexpr RuntimeCapabilities noOptionalHooks{true,true,false,false};
        static_assert(noOptionalHooks.logicalOutputReady());
        static_assert(!noOptionalHooks.attackSchedulerReady());
        static_assert(!noOptionalHooks.heldArbitrationReady());
        static_assert(!noOptionalHooks.ownershipArbitrationReady());
        check(noOptionalHooks.logicalOutputReady()&&
              !noOptionalHooks.attackSchedulerReady(),
              "rotation remains available but render update cannot dispatch attacks");
        constexpr RuntimeCapabilities stablePreBoundary{true,true,true,false};
        check(stablePreBoundary.attackSchedulerReady()&&
              stablePreBoundary.heldArbitrationReady(),
              "attack scheduling requires the transformed input PRE boundary");
        constexpr RuntimeCapabilities missingAttackBindings{true,false,true,true};
        check(missingAttackBindings.logicalOutputReady()&&
              !missingAttackBindings.attackSchedulerReady(),
              "packet rotation remains available when attack bindings are absent");
    }
    check(!mcoverlay::silent::isConfirmedCombatTeammate(
              false,true,'a','a')&&
          !mcoverlay::silent::isConfirmedCombatTeammate(
              true,false,'a','a')&&
          mcoverlay::silent::isConfirmedCombatTeammate(
              true,true,'a','a'),
          "team-colour exclusion is scoped to confirmed Hypixel matches");
    RenderClock clock;
    check(std::abs(clock.update(1,1,0.95F)-0.95)<1e-5,"initial render phase");
    check(std::abs(clock.update(1,1,0.05F)-1.05)<1e-5,"stale sample advances across partial-tick wrap");
    check(std::abs(clock.update(2,1,0.10F)-0.10)<1e-5,"new sample resets missing phase");
    check(std::abs(clock.update(2,2,0.05F)-0.05)<1e-5,"world change clears extrapolation");
    auto exact=ExactLockOutput::apply({179,0},{-179,20});
    check(exact.yaw==181 && exact.pitch==20,"exact lock wraps yaw without a 358-degree turn");
    {
        using namespace mcoverlay::silent;
        const Vec3 ray=RayTraceCoordinator::direction({0,0});
        check(std::abs(ray.x)<1e-9 && std::abs(ray.y)<1e-9 &&
              std::abs(ray.z-1.0)<1e-9,"logical ray follows yaw/pitch");
        const double hit=RayTraceCoordinator::intersect(
            {0,1.5,0},ray,{-0.3,0,2.0,0.3,1.8,2.6},3.0);
        check(std::abs(hit-2.0)<1e-9,"logical ray selects nearest AABB face");
        const RayTraceCoordinator::EntityHit verified{7,2.0,2.0};
        check(RayTraceCoordinator::attackTarget(7,verified,{true,-1.0,{}},true)==7,
              "vanilla attack accepts first unobstructed logical hit");
        check(RayTraceCoordinator::attackTarget(8,verified,{true,-1.0,{}},true)<0 &&
              RayTraceCoordinator::attackTarget(7,{7,3.01,3.01},{true,-1.0,{}},true)<0 &&
              RayTraceCoordinator::attackTarget(7,verified,{true,1.5,{}},true)<0,
              "vanilla attack rejects wrong target, excess reach and blocks");
        InteractionCoordinator interaction;
        interaction.updateRay(7,7,7,{});
        const auto attack=interaction.click(10);
        check(attack.kind==InteractionCommandKind::AttackEntity &&
              attack.entityId==7 && interaction.attackTarget()==7 &&
              interaction.event()==InteractionEvent::Attack,
              "interaction uses the verified ray target");
        interaction.reset();
        const BlockTarget block{1,2,3,4,true};
        interaction.updateRay(-1,-1,-1,block);
        check(interaction.click(20).kind==InteractionCommandKind::None,
              "combat ray block cannot implicitly start digging");
        check(interaction.held(true,20).kind==InteractionCommandKind::None,
              "one logical tick cannot emit click and held actions together");
        check(interaction.held(true,21).kind==InteractionCommandKind::None,
              "held combat input cannot damage a logical-ray block");
        interaction.blockFinished(21);
        check(interaction.event()==InteractionEvent::FinishedDigging &&
              interaction.mode()==InteractionMode::None,
              "completed block damage closes the same interaction lifecycle");
        MovementCoordinator movement;
        const auto state=movement.coordinate({true,false,true,false},{0,0},{0,0},
            {0.0,0.0,0.2},false,true);
        check(state.forward>0.70 && state.strafe>0.70 &&
              state.intendedVelocity.x>0.0 && state.intendedVelocity.z>0.0,
              "logical movement preserves Minecraft left-strafe sign");
        const auto remapped=movement.coordinate(
            {true,false,false,false},{0,0},{90,0},{0.0,0.0,0.2},false,true);
        const double radians=remapped.worldIntent.active ? 90.0*3.14159265358979323846/180.0 : 0.0;
        const double appliedX=-std::sin(radians)*remapped.forward+
                               std::cos(radians)*remapped.strafe;
        const double appliedZ= std::cos(radians)*remapped.forward+
                               std::sin(radians)*remapped.strafe;
        check(appliedZ>0.9 && std::abs(appliedX)<0.1,
              "cardinal remap follows camera/WASD intent");
        for(double yaw=-720.0;yaw<=720.0;yaw+=2.5) {
            for(double modifier:{1.0,0.98,0.294,0.196}) {
                const auto chosen=movement.coordinateAxes(modifier,0,{19,0},{yaw,0},{0,.1,.2},false,false);
                const double r=yaw*3.14159265358979323846/180.0;
                const double x=-std::sin(r)*chosen.forward+std::cos(r)*chosen.strafe;
                const double z=std::cos(r)*chosen.forward+std::sin(r)*chosen.strafe;
                const auto legalAxis=[&](double axis) {
                    return std::abs(axis)<1e-9||std::abs(std::abs(axis)-modifier)<1e-9;
                };
                check(legalAxis(chosen.forward)&&legalAxis(chosen.strafe),
                      "each remapped axis is a vanilla key value with the inherited slowdown");
                const double cosine=(x*chosen.worldIntent.x+z*chosen.worldIntent.z)/
                    (std::hypot(x,z)*std::hypot(chosen.worldIntent.x,chosen.worldIntent.z));
                check(cosine>=std::cos(22.5*3.14159265358979323846/180.0)-1e-9,
                      "selected vanilla direction stays within half a WASD sector of intent");
                check(!chosen.sprinting && !chosen.onGround,"native sprint state is not rewritten");
            }
        }
        for(std::size_t sector=0;sector<8U;++sector) {
            const double yaw=static_cast<double>(sector)*45.0;
            const auto sprint=movement.coordinateAxes(1.0,0.0,{0,0},{yaw,0},
                {0,.1,.2},true,false,sector);
            const double r=yaw*3.14159265358979323846/180.0;
            check(std::abs(sprint.forward-std::round(std::cos(r)*1.01))<1e-9&&
                  std::abs(sprint.strafe-std::round(std::sin(r)*1.01))<1e-9,
                  "sector-aligned movement uses discrete vanilla axes");
            check(sprint.sprinting==(sprint.forward>=0.8),
                  "logical sprint follows the chosen vanilla forward key");
            const double x=-std::sin(r)*sprint.forward+
                           std::cos(r)*sprint.strafe;
            const double z= std::cos(r)*sprint.forward+
                           std::sin(r)*sprint.strafe;
            check(z>0.7&&std::abs(x)<0.01,
                  "sprint state never changes authoritative world-space intent");
        }
        const auto suppressed=movement.coordinateAxes(1.0,0.0,{0,0},{90,0},
            {0,.1,.2},true,false,100);
        check(suppressed.physicalSprinting&&!suppressed.sprinting&&
              suppressed.sprintSuppressed,
              "an incompatible remap suppresses an observed physical sprint");
        const auto notManufactured=movement.coordinateAxes(1.0,0.0,{0,0},{0,0},
            {0,.1,.2},false,false,101);
        check(!notManufactured.physicalSprinting&&!notManufactured.sprinting&&
              !notManufactured.sprintSuppressed,
              "a compatible remap never manufactures sprint for a walking player");
    }
    {
        using namespace mcoverlay::silent;
        RotationManager rotation;
        rotation.reset({179.0,0.0});
        double previousPacket=179.0;
        bool havePacket=false;
        // Render at 240 Hz while vanilla emits one movement packet every
        // twelve frames (20 TPS).  Continuity is asserted between packets,
        // not merely between render snapshots.
        for(int frame=0;frame<480;++frame) {
            const double desired=179.0+static_cast<double>(frame)*1.75;
            (void)rotation.acquire({desired,5.0*std::sin(frame/40.0)});
            if(frame%12!=0) continue;
            const auto packet=rotation.networkRotation();
            if(havePacket)
                check(std::abs(packet.yaw-previousPacket)<=180.0,
                      "20 TPS packet yaw remains on one continuous branch");
            rotation.acknowledgeReported(packet);
            previousPacket=packet.yaw; havePacket=true;
        }
        rotation.observeCamera({-175.0,3.0});
        rotation.deactivate();
        const auto restore=rotation.networkRotation();
        check(std::abs(restore.yaw-previousPacket)<=180.0,
              "restore packet is continuous with the last logical packet");
        rotation.acknowledgeReported(restore);
        rotation.beginVanillaHandoff();
        check(rotation.restoring()&&rotation.handoffPending(),
              "restore acknowledgement retains ownership through vanilla handoff");
        const auto vanilla=rotation.continuousVanillaRotation({-175.0,3.0});
        check(std::abs(vanilla.yaw-restore.yaw)<=180.0,
              "first vanilla rotation stays on the last reported yaw branch");
        rotation.acknowledgeReported(vanilla);
        rotation.finishVanillaHandoff();
        check(!rotation.active()&&!rotation.restoring(),
              "first changed vanilla rotation completes logical ownership handoff");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate firstTarget{
            81,0x81U,{0,1.62,2.0},{-0.3,0.0,1.7,0.3,1.9,2.3},true};
        const TargetCandidate secondTarget{
            82,0x82U,{2.0,1.62,0},{1.7,0.0,-0.3,2.3,1.9,0.3},true};
        LogicalFrameInput input{};
        input.tick=5000;input.physicsTick=10;input.worldGeneration=13;
        input.localEntityId=1;
        input.enabled=true;input.silent=true;input.mode=Mode::LockOn;
        input.leftMouseDown=true;
        input.maximumDistance=6;input.fovDegrees=360;
        input.camera={0,0};input.eye={0,1.62,0};
        input.physicalMovement.forward=true;
        input.candidates={&firstTarget,1};
        const auto clearTrace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        (void)controller.advance(input,clearTrace);
        check(controller.latest().attackTargetId==81,
              "initial logical tick commits a complete verified target");

        const auto publishedBeforeTrace=controller.latest();
        input.tick=5010;input.physicsTick=11;input.candidates={&secondTarget,1};
        (void)controller.advance(input,clearTrace);
        const auto committed=controller.latest();
        check(publishedBeforeTrace.candidateTargetId==81&&
              publishedBeforeTrace.attackTargetId==81&&
              committed.candidateTargetId==82&&committed.attackTargetId==82,
              "a complete trace replaces target and attack state in one publication");

        input.tick=5020;input.physicsTick=12;input.candidates={};
        const auto grace=controller.advance(input,clearTrace);
        check(grace.silentActive&&controller.active()&&
              controller.latest().attackTargetId<0,
              "one incomplete target sample keeps ownership but exposes no attack");
        controller.observeCameraInput({0,0},{3,4,5,2,true},-1,true,3);
        check(controller.routeManualInput(true)&&!controller.active(),
              "camera block owns a real input even during target-loss grace");
        controller.observeCameraInput({0,0},{},-1,false,4);
        check(!controller.routeManualInput(false),
              "manual input releases cleanly during target-loss grace");
        input.tick=5021;input.physicsTick=13;input.candidates={&secondTarget,1};
        check(controller.advance(input,clearTrace).silentActive&&controller.active(),
              "a fresh target sample reacquires after manual input releases");
        input.tick=5030;input.physicsTick=14;input.candidates={};
        check(controller.advance(input,clearTrace).silentActive,
              "target loss grace retains the reacquired rotation owner");
        input.tick=5081;input.physicsTick=15;
        const auto released=controller.advance(input,clearTrace);
        check(!released.silentActive&&!controller.active()&&controller.restoring(),
              "real target loss begins one stable restore lifecycle");
        input.candidates={&secondTarget,1};
        const auto sameTick=controller.advance(input,clearTrace);
        check(!sameTick.silentActive&&!controller.active(),
              "same logical tick cannot release and reacquire rotation ownership");
        input.tick=5082;input.physicsTick=16;
        check(controller.advance(input,clearTrace).silentActive&&controller.active(),
              "a later complete tick can reacquire after the stable release");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate target{42,0x42U,{0,1.62,2.0},
                                     {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        LogicalFrameInput input{};
        input.tick=1000; input.physicsTick=20;
        input.worldGeneration=9; input.localEntityId=1;
        input.enabled=true; input.silent=true; input.mode=Mode::LockOn;
        input.leftMouseDown=true;
        input.maximumDistance=6.0; input.fovDegrees=90.0;
        input.camera={0,0}; input.eye={0,1.62,0};
        input.physicalMovement.forward=true;
        input.currentVelocity={0,0,0.2}; input.sprinting=true; input.onGround=true;
        input.candidates={&target,1};
        const auto clearTrace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        const auto plan=controller.advance(input,clearTrace);
        check(plan.silentActive&&plan.candidateTargetId==42,
              "pure AimAssist plus Silent Lock enters authoritative frame pipeline");
        const auto movement=controller.movementCommand(0.0,1.0,20);
        check(movement.enabled&&movement.logicalTick==20,
              "pure Silent Lock publishes an executable movement command");
        controller.updateAttackClock(true,true,1000000U,10);
        input.tick=1001;
        (void)controller.advance(input,clearTrace);
        check(controller.click().kind==InteractionCommandKind::None,
              "attack waits until its exact logical rotation is published");
        const auto firstPacket=controller.packetPlan(false,false);
        check(firstPacket.mutation==PacketMutation::InjectRotation,
              "pending attack first requests its logical Look rotation");
        controller.acknowledgePacket(firstPacket.rotation,PacketKind::Ground,
                                     PacketKind::Look,false,true);
        const auto click=controller.click();
        check(click.kind==InteractionCommandKind::AttackEntity&&click.entityId==42,
              "published logical ray decides authoritative entity interaction");
        check(controller.latest().committedAttackTargetId==42&&
              controller.latest().requiredAttackRotationEpoch==click.rotationEpoch,
              "target and published rotation stay frozen through final dispatch");
        controller.attackDispatched(click,true);
        check(controller.latest().committedAttackTargetId<0&&
              controller.latest().requiredAttackRotationEpoch==0U,
              "completed dispatch releases the attack transaction");
        controller.updateAttackClock(true,true,1100000U,10);
        input.tick=1002;
        (void)controller.advance(input,clearTrace);
        const auto repeatedClick=controller.click();
        check(repeatedClick.kind==InteractionCommandKind::AttackEntity&&
              repeatedClick.intentId!=click.intentId,
              "two real clicks in one native tick retain distinct attack intents");
        controller.attackDispatched(repeatedClick,true);
        check(controller.latest().attackDispatchCount==2,
              "each committed attack intent records exactly one dispatch");
        const auto snapshot=controller.latest();
        check(snapshot.movement.controlsMinecraftMovement&&
              snapshot.attackTargetId==42&&
              snapshot.event==InteractionEvent::Attack,
              "one logical tick links movement, raytrace, interaction and packet state");
        check(controller.held(true).kind==InteractionCommandKind::None,
              "actual held entry closes the attack client tick without a block action");
        const auto packet=controller.packetPlan(false,false);
        check(packet.mutation==PacketMutation::Pass,
              "already-published logical rotation is not emitted twice");

        // Ordinary items keep an executable combat intent; the camera block is
        // only a fallback when that intent cannot commit.
        input.tick=1008; input.physicsTick=21;
        input.heldItemPolicy=HeldItemPolicy::Other;
        (void)controller.advance(input,clearTrace);
        controller.observeCameraInput({0,0},{4,5,6,2,true},-1,true,20,true);
        check(!controller.routeManualInput(true) && controller.active(),
              "ordinary held item keeps an executable combat click authoritative");
        controller.observeCameraInput({0,0},{},-1,false,21);
        check(!controller.routeManualInput(false),
              "ordinary-item release leaves no manual block owner");
        input.heldItemPolicy=HeldItemPolicy::MiningTool;
        input.tick=1012; input.physicsTick=22;
        check(controller.advance(input,clearTrace).silentActive,
              "combat remains available before a mining-tool gesture");
        controller.observeCameraInput({0,0},{4,5,6,2,true},-1,true,20,true);
        check(controller.routeManualInput(true)&&!controller.active(),
              "mining tool gives a valid camera block priority over combat");
        controller.observeCameraInput({0,0},{},-1,false,21);
        check(!controller.routeManualInput(false),
              "mining-tool gesture releases manual ownership cleanly");

        input.tick=1016; input.physicsTick=23;
        (void)controller.advance(input,[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,0.5,{4,5,6,2,true}};
        });
        check(controller.click().kind==InteractionCommandKind::None,
              "blocked attack never becomes implicit digging");
        controller.observeCameraInput({0,60},{4,5,6,2,true},-1,true,21,true);
        check(controller.routeManualInput(true) && !controller.active(),
              "camera block input releases combat rotation before vanilla dig");
        input.tick=1020;
        check(!controller.advance(input,clearTrace).silentActive &&
            controller.latest().interactionOwner==InteractionOwner::ManualBlock,
            "render frames cannot steal ownership during a held manual dig");
        check(!controller.movementCommand(0,0.294,21).enabled,
            "manual dig keeps vanilla movement source");
        controller.observeCameraInput({0,0},{},-1,false,22);
        check(!controller.routeManualInput(false),"release ends manual ownership");
        input.tick=1024; input.physicsTick=24;
        check(controller.advance(input,clearTrace).silentActive,
              "combat re-acquires only after manual release");
        const auto stable=controller.movementCommand(0,0.98,24);
        input.tick=1028; input.camera={20,0};
        (void)controller.advance(input,clearTrace);
        const auto sameTick=controller.movementCommand(0.98,0,24);
        check(sameTick.physicalStrafe==0.98&&sameTick.physicalForward==0.0&&
              sameTick.enabled&&sameTick.snapshotVersion==stable.snapshotVersion&&
              sameTick.strafe==stable.strafe&&sameTick.forward==stable.forward,
              "same physics tick reuses the first consumer-owned movement commit");
        check(stable.logicalRotation.yaw==sameTick.logicalRotation.yaw&&
              stable.cameraRotation.yaw==sameTick.cameraRotation.yaw,
              "same Minecraft tick retains one camera/logical yaw snapshot");
        input.tick=1030; input.physicsTick=25;
        (void)controller.advance(input,clearTrace);
        const auto jumpSnapshot=controller.jumpCommand(0.0,0.98,true,25);
        const auto moveAfterJump=controller.movementCommand(0.0,0.98,25);
        check(jumpSnapshot.physicalStrafe==moveAfterJump.physicalStrafe&&
              jumpSnapshot.physicalForward==moveAfterJump.physicalForward&&
              jumpSnapshot.strafe==moveAfterJump.strafe&&
              jumpSnapshot.forward==moveAfterJump.forward&&
              jumpSnapshot.sprinting==moveAfterJump.sprinting,
              "jump and moveFlying share the current physics-tick snapshot");
        controller.deactivate();
        input.tick=1032; input.physicsTick=26;
        input.enabled=false; input.silent=false;
        input.candidates={};
        const auto drain=controller.advance(input,clearTrace);
        check(drain.interactionTransition.kind==
                  InteractionCommandKind::None,
              "shutdown does not reset a block dig it does not own");
        const auto restorePacket=controller.packetPlan(false,false);
        check(restorePacket.mutation==PacketMutation::InjectRotation&&
              restorePacket.restoring,
              "feature shutdown serializes one continuous camera restore");
        controller.acknowledgePacket(restorePacket.rotation,PacketKind::Ground,
                                     PacketKind::Look,false,true);
        check(controller.requiresDrain(),
              "restore acknowledgement keeps the vanilla handoff managed");
        const auto equivalent=controller.packetPlan(false,true,{20,0},true);
        check(equivalent.mutation==PacketMutation::RemoveRotation&&
              equivalent.vanillaHandoff,
              "equivalent first vanilla rotation is suppressed without ending handoff");
        controller.acknowledgeSuppressedPacket(PacketKind::Look,
                                               PacketKind::Ground,false);
        check(controller.requiresDrain(),
              "suppressed equivalent vanilla yaw cannot prematurely release ownership");
        const auto resumed=controller.packetPlan(false,true,{21,0},true);
        check(resumed.mutation==PacketMutation::InjectRotation&&
              resumed.vanillaHandoff&&std::abs(resumed.rotation.yaw-21.0)<1e-9,
              "changed vanilla yaw resumes on the continuous reported branch");
        controller.acknowledgePacket(resumed.rotation,PacketKind::Look,
                                     PacketKind::Look,false,true);
        check(!controller.requiresDrain(),
              "changed vanilla acknowledgement fully drains rotation ownership");
        check(controller.packetContinuityRequired(),
              "camera packets remain branch-normalized after handoff");
        const auto delayedEquivalent=controller.packetPlan(
            false,true,{381,0},true);
        check(delayedEquivalent.mutation==PacketMutation::RemoveRotation,
              "later modulo-equivalent vanilla Look is suppressed too");
        controller.acknowledgeSuppressedPacket(PacketKind::Look,
                                               PacketKind::Ground,false);
        const auto laterChange=controller.packetPlan(false,true,{382,0},true);
        check(laterChange.mutation==PacketMutation::InjectRotation&&
              std::abs(laterChange.rotation.yaw-22.0)<1e-9,
              "later vanilla mouse movement stays on the reported yaw branch");
        controller.acknowledgePacket(laterChange.rotation,PacketKind::Look,
                                     PacketKind::Look,false,true);
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate target{91,0x91U,{0,1.62,2.0},
            {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        LogicalFrameInput input{};
        input.tick=2000;input.physicsTick=40;input.worldGeneration=5;
        input.localEntityId=1;input.enabled=true;input.silent=true;
        input.leftMouseDown=true;
        input.mode=Mode::LockOn;input.maximumDistance=6;input.fovDegrees=90;
        input.camera={0,0};input.eye={0,1.62,0};
        input.physicalMovement.forward=true;input.coordinateMovement=false;
        input.candidates={&target,1};
        const auto plan=controller.advance(input,[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        });
        const auto movement=controller.movementCommand(0,1,40);
        const auto jump=controller.jumpCommand(0,1,false,40);
        check(plan.silentActive&&!plan.movement.controlsMinecraftMovement&&
              !movement.enabled&&!jump.enabled,
              "control adaptation OFF leaves vanilla movement and jump untouched");
        controller.observeCameraInput({0,0},{1,2,3,2,true},-1,true,40);
        check(!controller.routeManualInput(true)&&controller.active(),
              "control adaptation OFF does not apply tool/block ownership rules");
        controller.updateAttackClock(true,true,2000000U,10);
        input.tick=2001;
        (void)controller.advance(input,[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        });
        const auto packet=controller.packetPlan(false,false);
        controller.acknowledgePacket(packet.rotation,PacketKind::Ground,
                                     PacketKind::Look,false,true);
        check(controller.click().kind==InteractionCommandKind::AttackEntity,
              "control adaptation OFF does not gate core silent attack");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate target{92,0x92U,{0,1.62,2.0},
            {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        LogicalFrameInput input{};
        input.tick=2100;input.physicsTick=41;input.worldGeneration=6;
        input.localEntityId=1;input.enabled=true;input.silent=true;
        input.leftMouseDown=true;
        input.mode=Mode::LockOn;input.maximumDistance=6;input.fovDegrees=90;
        input.camera={0,0};input.eye={0,1.62,0};
        input.physicalMovement.forward=true;input.coordinateMovement=true;
        input.enforceAttackAvailability=true;input.candidates={&target,1};
        (void)controller.advance(input,[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,0.5,{1,2,3,2,true}};
        });
        check(controller.outputReady()&&controller.latest().attackTargetId<0&&
              controller.movementCommand(0,1,41).enabled&&
              controller.jumpCommand(0,1,false,41).enabled,
              "attack availability gates only dispatch, not rotation or adaptation");
        check(controller.packetPlan(false,false).mutation==
                  PacketMutation::InjectRotation,
              "blocked attack viability still publishes silent rotation");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate targetA{98,0x98U,{0,1.62,2.0},
            {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        const TargetCandidate targetB{99,0x99U,{2.0,1.62,0.0},
            {1.7,0.0,-0.3,2.3,1.9,0.3},true};
        LogicalFrameInput input{};
        input.tick=3000;input.physicsTick=44;input.worldGeneration=13;
        input.localEntityId=1;input.enabled=true;input.silent=true;
        input.leftMouseDown=true;input.mode=Mode::LockOn;
        input.maximumDistance=6;input.fovDegrees=360;
        input.camera={0,0};input.eye={0,1.62,0};input.candidates={&targetA,1};
        const auto clearTrace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        (void)controller.advance(input,clearTrace);
        controller.updateAttackClock(true,true,3000000U,10);
        input.tick=3001;
        const auto bound=controller.advance(input,clearTrace);
        const auto rotationR0=controller.packetPlan(false,false);
        check(bound.candidateTargetId==98&&
              rotationR0.mutation==PacketMutation::InjectRotation,
              "attack transaction binds target A and rotation R0");
        check(controller.click().kind==InteractionCommandKind::None,
              "bound attack waits until its exact rotation is published");

        input.candidates={&targetB,1};
        bool pinned=true;
        for(int frame=0;frame<5;++frame) {
            input.tick=3002+static_cast<std::uint64_t>(frame);
            const auto waiting=controller.advance(input,clearTrace);
            const auto packet=controller.packetPlan(false,false);
            pinned=pinned&&waiting.candidateTargetId==98&&
                std::abs(wrap(packet.rotation.yaw-rotationR0.rotation.yaw))<
                    0.0005&&
                std::abs(packet.rotation.pitch-rotationR0.rotation.pitch)<0.0005&&
                controller.click().kind==InteractionCommandKind::None;
        }
        check(pinned,
              "pending transaction cannot drift from A/R0 to newer AimAssist rotations");
        const auto publishedR0=controller.packetPlan(false,false);
        controller.acknowledgePacket(publishedR0.rotation,PacketKind::Ground,
                                     PacketKind::Look,false,true);
        const auto attackA=controller.click();
        check(attackA.kind==InteractionCommandKind::AttackEntity&&
              attackA.entityId==98,
              "publishing R0 releases exactly the target A attack");

        controller.updateAttackClock(true,true,3100000U,10);
        input.tick=3010;
        const auto next=controller.advance(input,clearTrace);
        const auto rotationR1=controller.packetPlan(false,false);
        check(next.candidateTargetId==99&&
              std::abs(wrap(rotationR1.rotation.yaw-
                            rotationR0.rotation.yaw))>1.0,
              "next attack recalculates a fresh target B and rotation R1");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate target{93,0x93U,{0,1.62,2.0},
            {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        LogicalFrameInput input{};
        input.tick=2200;input.physicsTick=42;input.worldGeneration=7;
        input.localEntityId=1;input.enabled=true;input.silent=true;
        input.mode=Mode::LockOn;input.maximumDistance=6;input.fovDegrees=90;
        input.camera={0,0};input.eye={0,1.62,0};input.candidates={&target,1};
        input.heldItemPolicy=HeldItemPolicy::BlockItem;
        controller.observeCameraInput({0,0},{},-1,false,42);
        const auto idle=controller.advance(input,[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        });
        check(!idle.silentActive&&!controller.active()&&idle.candidateTargetId==93,
              "held block keeps camera rotation while idle but retains target knowledge");
        controller.observeCameraInput({0,0},{},-1,true,43);
        controller.updateAttackClock(true,true,2201000U,10);
        input.leftMouseDown=true;
        input.tick=2201;input.physicsTick=43;
        check(controller.advance(input,[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        }).silentActive,
              "held block temporarily acquires logical rotation for a real left click");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const std::array<TargetCandidate,2> targets{{
            {71,0x71U,{0,1.62,2.0},{-0.3,0.0,1.7,0.3,1.9,2.3},true,false},
            {72,0x72U,{1.7,1.62,1.7},{1.4,0.0,1.4,2.0,1.9,2.0},true,false}
        }};
        LogicalFrameInput input{};
        input.tick=4000; input.worldGeneration=12; input.localEntityId=1;
        input.enabled=true; input.silent=true; input.mode=Mode::LockOn;
        input.leftMouseDown=true;
        input.sequentialTargets=true; input.maximumDistance=6.0;
        input.fovDegrees=360.0; input.camera={0,0}; input.eye={0,1.62,0};
        input.candidates=targets;
        const auto clearTrace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        const auto first=controller.advance(input,clearTrace);
        check(first.candidateTargetId==71 && controller.attackTargetId()==71,
              "sequential Silent Lock starts with the best ready target");
        controller.updateAttackClock(true,true,4000000U,10);
        input.tick=4001;
        (void)controller.advance(input,clearTrace);
        const auto firstPacket=controller.packetPlan(false,false);
        controller.acknowledgePacket(firstPacket.rotation,PacketKind::Ground,
                                     PacketKind::Look,false,true);
        const auto firstAttack=controller.click();
        check(firstAttack.kind==InteractionCommandKind::AttackEntity &&
              firstAttack.entityId==71,
              "sequential attack records the first logical target");

        input.tick=4016;
        controller.observeCameraInput({0,0},{},-1,false,2);
        const auto second=controller.advance(input,clearTrace);
        check(second.candidateTargetId==72 && controller.attackTargetId()==72,
              "a cooling target yields to the next attackable target");
        controller.updateAttackClock(true,true,4100000U,10);
        input.tick=4017;
        (void)controller.advance(input,clearTrace);
        const auto secondPacket=controller.packetPlan(false,false);
        if(secondPacket.mutation==PacketMutation::InjectRotation)
            controller.acknowledgePacket(secondPacket.rotation,PacketKind::Ground,
                                         PacketKind::Look,false,true);
        check(controller.click().entityId==72,
              "left click consumes the newly selected sequential target");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate target{94,0x94U,{0,1.62,2.0},
            {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        LogicalFrameInput input{};
        input.tick=5000;input.physicsTick=50;input.worldGeneration=8;
        input.localEntityId=1;input.enabled=true;input.silent=true;
        input.mode=Mode::LockOn;input.leftMouseDown=true;
        input.maximumDistance=6;input.fovDegrees=90;
        input.camera={0,0};input.eye={0,1.62,0};input.candidates={&target,1};
        const auto trace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        check(controller.advance(input,trace).silentActive,
              "held left mouse grants SilentCombat rotation ownership");
        controller.updateAttackClock(true,true,5000000U,10);
        input.tick=5001;
        (void)controller.advance(input,trace);
        check(controller.click().kind==InteractionCommandKind::None,
              "bound transaction remains pending before rotation publish");
        controller.observeCameraInput({0,0},{},-1,false,50);
        check(!controller.active()&&
              controller.click().kind==InteractionCommandKind::None,
              "physical release entry cancels SilentCombat before the next frame");
        check(!controller.movementCommand(0,1,50).enabled&&
              controller.arbitrateSprint(true),
              "physical release returns movement and sprint to vanilla immediately");
        input.leftMouseDown=false;input.tick=5002;
        const auto released=controller.advance(input,trace);
        controller.updateAttackClock(false,true,5001000U,10);
        check(!released.silentActive&&!controller.active()&&
              controller.latest().interactionOwner==InteractionOwner::None,
              "left-mouse release immediately returns every owner to Camera");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate target{95,0x95U,{0,1.62,2.0},
            {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        LogicalFrameInput input{};
        input.tick=5500;input.physicsTick=55;input.worldGeneration=8;
        input.localEntityId=1;input.enabled=true;input.silent=true;
        input.mode=Mode::LockOn;input.leftMouseDown=true;
        input.maximumDistance=6;input.fovDegrees=90;
        input.camera={0,0};input.eye={0,1.62,0};input.candidates={&target,1};
        const auto trace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        (void)controller.advance(input,trace);
        controller.updateAttackClock(true,true,5500000U,10);
        input.tick=5501;
        (void)controller.advance(input,trace);
        const auto packet=controller.packetPlan(false,false);
        if(packet.mutation==PacketMutation::InjectRotation)
            controller.acknowledgePacket(packet.rotation,PacketKind::Ground,
                                         PacketKind::Look,false,true);
        controller.beginPhysicsTick(55);
        controller.endMovementPhase(55);
        check(controller.clickAtInteractionPre(55).kind==
                  InteractionCommandKind::None,
              "rotation-ready attack remains pending after movement POST");
        controller.beginInteractionPre(55);
        const auto attack=controller.clickAtInteractionPre(55);
        check(attack.kind==InteractionCommandKind::AttackEntity&&
              attack.entityId==95,
              "pending transaction dispatches at the next stable PRE boundary");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        const TargetCandidate forwardTarget{96,0x96U,{0,1.62,2.0},
            {-0.3,0.0,1.7,0.3,1.9,2.3},true};
        const TargetCandidate sideTarget{97,0x97U,{2.0,1.62,0},
            {1.7,0.0,-0.3,2.3,1.9,0.3},true};
        LogicalFrameInput input{};
        input.tick=6000;input.physicsTick=60;input.worldGeneration=9;
        input.localEntityId=1;input.enabled=true;input.silent=true;
        input.mode=Mode::LockOn;input.leftMouseDown=true;
        input.coordinateMovement=true;input.maximumDistance=6;
        input.fovDegrees=360;input.camera={0,0};input.eye={0,1.62,0};
        input.physicalMovement.forward=true;input.sprinting=false;
        input.candidates={&forwardTarget,1};
        const auto trace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        (void)controller.advance(input,trace);
        const auto walking=controller.movementCommand(0,1,60);
        check(walking.enabled&&!walking.sprinting&&
              !controller.arbitrateSprint(true)&&
              !controller.arbitrateSprint(false),
              "silent aiming vetoes Lunar sprint even when forward movement is compatible");
        input.tick=6001;input.physicsTick=61;input.candidates={&sideTarget,1};
        (void)controller.advance(input,trace);
        check(!controller.arbitrateSprint(true,61),
              "pre-consumer sprint obeys silent policy without reading stale axes");
        const auto remapped=controller.movementCommand(0,1,61);
        check(!remapped.sprinting&&!controller.arbitrateSprint(true,61),
              "current movement snapshot commits the immutable sprint veto");
        input.tick=6002;input.physicsTick=62;input.candidates={&forwardTarget,1};
        (void)controller.advance(input,trace);
        (void)controller.movementCommand(0,1,62);
        check(!controller.arbitrateSprint(true,62),
              "incompatible sprint suppression remains latched while LMB is held");
        controller.observeCameraInput({0,0},{},-1,false,61);
        check(controller.arbitrateSprint(true),
              "mouse release returns sprint arbitration to vanilla immediately");
    }
    {
        using namespace mcoverlay::hotbar;
        std::array<int,SlotCount> actions{};
        actions[0]=static_cast<int>(Action::Sword);
        actions[3]=static_cast<int>(Action::Blocks);
        const std::uint32_t packed=pack(true,actions);
        check(validPacked(packed)&&enabled(packed)&&unpack(packed)==actions,
              "smart hotbar configuration has an exact bounded roundtrip");
        check(!validPacked(packed|(7U<<(1U+2U*3U))),
              "smart hotbar rejects reserved action values");
        check(validPacked(1U|(2U<<7U))&&unpack(1U|(2U<<7U))[3]==2,
              "v49 two-bit hotbar settings remain readable without shifting assignments");
        actions[1]=3;actions[5]=4;actions[8]=5;
        const auto expanded=pack(true,actions,true,true,true);
        check(validPacked(expanded)&&unpack(expanded)==actions&&
              (expanded&(Refill|Sprint|AllNames))==(Refill|Sprint|AllNames),
              "v50 categories and preferences roundtrip without bit overlap");
        for(int id:{257,270,274,278,285}) check(toolKind(id)==ItemKind::Pickaxe,"every vanilla pickaxe material matches");
        for(int id:{258,271,275,279,286}) check(toolKind(id)==ItemKind::Axe,"every vanilla axe material matches");
        check(toolKind(359)==ItemKind::Shears&&toolKind(1)==ItemKind::Other,"shears classify without treating unrelated items as tools");
        std::array<ItemKind,36U> inventory{};
        inventory[7]=ItemKind::Sword;
        inventory[18]=ItemKind::Blocks;
        check(selectSource(inventory,2,static_cast<int>(Action::Sword))==7,
              "smart hotbar prefers a matching hotbar item without swapping");
        check(selectSource(inventory,2,static_cast<int>(Action::Blocks))==18,
              "smart hotbar falls back to a matching main-inventory stack");
        inventory[4]=ItemKind::Blocks;
        check(selectRefillSource(inventory,2)==4,
              "block refill prioritizes a ready hotbar stack over inventory");
        inventory[18]=ItemKind::Empty;
        check(selectRefillSource(inventory,2)==4,
              "block refill can still use a ready hotbar stack");
        inventory[4]=ItemKind::Empty;
        inventory[18]=ItemKind::Blocks;
        check(selectRefillSource(inventory,2)==18,
              "block refill falls back to a main-inventory stack");
        inventory[2]=ItemKind::Sword;
        check(selectSource(inventory,2,static_cast<int>(Action::Sword))==2,
              "smart hotbar keeps an already-held matching item selected");
    }
    {
        using namespace mcoverlay::silent;
        MovementIntentResolver resolver;
        const auto sector=resolver.resolveAxes(1.0,0.0,0.0,22.0,false,1);
        check(sector.logicalForward==1.0&&sector.logicalStrafe==0.0,
              "between-sector movement selects a real key direction, never analog input");
        // Independent vanilla moveFlying oracle: each result must equal one
        // possible WASD acceleration, including slowed diagonal input.
        for(double modifier:{.98,.294,.196,.0588})
            for(int pf=-1;pf<=1;++pf) for(int ps=-1;ps<=1;++ps)
                for(double yaw=-180;yaw<=180;yaw+=7.5) {
                    const auto result=resolver.resolveAxes(pf*modifier,ps*modifier,13,yaw);
                    const auto accelerate=[&](double f,double st) {
                        const double scale=.1/std::max(1.0,std::hypot(f,st));
                        const double r=yaw*3.14159265358979323846/180.0;
                        return std::array<double,2>{(-std::sin(r)*f+std::cos(r)*st)*scale,
                            (std::cos(r)*f+std::sin(r)*st)*scale};
                    };
                    const auto actual=accelerate(result.logicalForward,result.logicalStrafe);
                    bool legal=false;
                    for(int f=-1;f<=1;++f) for(int st=-1;st<=1;++st) {
                        const auto expected=accelerate(f*modifier,st*modifier);
                        legal=legal||(std::abs(actual[0]-expected[0])<1e-12&&
                                      std::abs(actual[1]-expected[1])<1e-12);
                    }
                    check(legal,"movement acceleration belongs to the vanilla WASD set");
                }
        for(const int cps:{1,7,20}) {
            FixedCpsAttackScheduler clock;
            int dispatched=0;
            for(std::uint64_t now=0;now<=5000000U;now+=2000U) {
                const auto pulse=clock.update(true,false,now,cps);
                if(pulse.intentId&&clock.consume()) ++dispatched;
            }
            check(std::abs(dispatched-(cps*5+1))<=1,
                "fixed-CPS clock stays within one intent over five seconds");
        }
        FixedCpsAttackScheduler clock;
        const auto first=clock.update(true,false,1000U,20);
        const auto held=clock.update(true,false,50000U,20);
        check(first.intentId!=0U&&held.intentId==first.intentId,
            "a pending deadline owns one stable unique intent");
        const auto cancelled=clock.update(true,true,51000U,20);
        check(cancelled.cancelledIntentId==first.intentId&&clock.pending()==0U,
            "placement or mining priority classifies and cancels a pending intent");
    }
    {
        using namespace mcoverlay::silent;
        LogicalStateController controller;
        controller.reset({0,0});
        TargetCandidate preAim{73,73U,{0,1.62,3.3},
            {-.3,0,3.0,.3,1.9,3.6},true,false,false};
        LogicalFrameInput input{};
        input.tick=7000;input.physicsTick=70;input.worldGeneration=11;
        input.localEntityId=1;input.enabled=input.silent=input.leftMouseDown=true;
        input.mode=Mode::LockOn;input.maximumDistance=3.5;input.attackReach=3.0;
        input.enforceAttackAvailability=true;input.fovDegrees=360;
        input.eye={0,1.62,0};input.candidates={&preAim,1};
        const auto clear=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        (void)controller.advance(input,clear);
        controller.updateAttackClock(true,true,7000000U,20);
        (void)controller.advance(input,clear);
        const auto armed=controller.pendingAttack();
        check(armed.intentId!=0&&armed.entityId==73,
            "pending CPS intent arms on a legitimate pre-aim target");
        const auto publish=controller.packetPlan(true,false);
        controller.acknowledgePacket(publish.rotation,PacketKind::Position,
            PacketKind::PositionLook,true,true);
        controller.beginInteractionPre(70);
        check(controller.clickAtInteractionPre(70).kind==InteractionCommandKind::None&&
              controller.pendingAttack().intentId==armed.intentId,
            "temporary lack of 3.0 m availability neither rejects nor consumes the intent");
        check(controller.revisePendingAttack(controller.pendingAttack(),
                  armed.committedRotation,true),
            "current physics geometry can promote the armed intent to READY");
        controller.beginInteractionPre(71);
        const auto attack=controller.clickAtInteractionPre(71);
        check(attack.kind==InteractionCommandKind::AttackEntity&&attack.entityId==73&&
              attack.intentId==armed.intentId,
            "an already-published pre-aim rotation dispatches immediately when availability opens");
    }
    for (double phase : {0.0, 0.25, 0.5, 0.999}) {
        const Angles before{178,12}, current{179,15}, output{181,16};
        const auto previous=shiftedPrevious(before,current,output);
        const double renderedBefore=before.yaw+(current.yaw-before.yaw)*phase;
        const double renderedAfter=previous.yaw+(output.yaw-previous.yaw)*phase;
        check(std::abs(renderedAfter-renderedBefore-2)<1e-9,
              "camera delta is independent of 20Hz interpolation phase");
        check(std::abs((previous.pitch+(output.pitch-previous.pitch)*phase)-
                       (before.pitch+(current.pitch-before.pitch)*phase)-1)<1e-9,
              "pitch preserves existing mouse interpolation");
    }
    for (int hz : {60,144,240}) {
        SmoothMouseOutput smooth;
        smooth.reset({0,0});
        Angles angle{}, previous{};
        for (int frame=0; frame<hz*8; ++frame) {
            const double time=double(frame)/hz;
            const Angles desired{20*std::sin(time), 7*std::sin(time*0.7)};
            if (int(time*20)!=int((time-1.0/hz)*20)) previous=angle;
            const auto output=smooth.apply(angle,desired,1.0/hz,0.5,70);
            previous=shiftedPrevious(previous,angle,output);
            check(std::abs(output.yaw-angle.yaw)<2.0,"moving target has no snap on reversals");
            check(std::abs(previous.yaw-output.yaw)<1e-8,"no tick-reset sawtooth after paired output");
            angle=output;
        }
    }
    for(int hz : {60,144,240}) for(int speed : {1,35,100}) {
        SmoothMouseOutput output;
        output.reset({0,0});
        Angles angle{};
        const double dt=1.0/hz;
        angle=output.apply(angle,{20,10},dt,0.5,speed);
        check(angle.yaw<20 && angle.pitch<10,"Smooth at 100% is not a hard lock");
        double previous=angle.yaw;
        for(int frame=0;frame<hz*20;++frame) {
            angle=output.apply(angle,{20,10},dt,0.5,speed);
            check(angle.yaw>=previous-1e-6 && angle.yaw<=20.00001,"settling does not overshoot or oscillate");
            check(std::isfinite(angle.pitch),"finite pitch");
            previous=angle.yaw;
        }
        output.reset({-45,0}); // Mode/target changes reset residual mouse counts.
        angle=output.apply({-45,0},{-45,0},dt,0.5,speed);
        check(std::abs(angle.yaw+45)<1e-9,"no old residual kick after reset");
    }
    {
        using namespace mcoverlay::silent;
        const auto clearTrace=[](const LogicalFramePlan&) noexcept {
            return BlockRayHit{true,-1.0,{}};
        };
        for(bool adaptation:{false,true}) for(int step=0;step<40;++step) {
            LogicalStateController controller;
            controller.reset({0,0});
            TargetCandidate target{42,42U,{0,1.62,2.5},{-.3,0,2.2,.3,1.9,2.8},true};
            LogicalFrameInput input{};
            input.tick=1000;input.physicsTick=20;input.worldGeneration=1;input.localEntityId=1;
            input.enabled=input.silent=input.leftMouseDown=true;
            input.coordinateMovement=adaptation;input.mode=Mode::LockOn;
            input.maximumDistance=3;input.fovDegrees=360;
            input.eye={0,1.62,0};input.candidates={&target,1};
            (void)controller.advance(input,clearTrace);
            controller.updateAttackClock(true,true,1000000U,10);
            (void)controller.advance(input,clearTrace);
            const auto old=controller.pendingAttack();
            check(old.intentId!=0,"moving transaction binds immediately");
            const Vec3 currentEye{.4*std::cos(step*.31),1.62+.5*std::sin(step*.43),.1};
            const auto point=chooseCombatAimPoint(currentEye,target.bounds,old.committedRotation,3,true,
                [](Vec3,double) noexcept {return BlockRayHit{true,-1,{}};});
            const double dx=point.point.x-currentEye.x,dy=point.point.y-currentEye.y,dz=point.point.z-currentEye.z;
            const Angles angle{std::atan2(dz,dx)*180.0/3.14159265358979323846-90,
                -std::atan2(dy,std::hypot(dx,dz))*180.0/3.14159265358979323846};
            check(point.available&&controller.revisePendingAttack(old,angle,true),
                "publication refresh accepts current strafe/jump geometry independent of SCA");
            const auto revised=controller.pendingAttack();
            check(revised.intentId==old.intentId&&revised.entityId==old.entityId&&
                revised.rotationEpoch>old.rotationEpoch,"revision preserves CPS intent and target, replaces publication proof");
            check(!controller.revisePendingAttack(old,{0,0},true),"stale revision cannot overwrite a newer transaction");
            if(adaptation) {
                const auto move=controller.movementCommand(0,.98,20);
                check(move.enabled&&std::abs(wrap(move.logicalRotation.yaw-angle.yaw))<.0005,
                    "movement consumer commits the refreshed transaction yaw");
                check(!controller.revisePendingAttack(revised,{angle.yaw+5,angle.pitch},true),
                    "publication cannot change the yaw already consumed by movement in this tick");
                const auto jump=controller.jumpCommand(0,.98,true,20);
                check(jump.snapshotVersion==move.snapshotVersion&&
                    jump.logicalRotation.yaw==move.logicalRotation.yaw,
                    "jump, sprint and packet retain the immutable physics decision after rejected revision");
            }
            controller.acknowledgePacket(old.committedRotation,PacketKind::Look,PacketKind::Look,false,true);
            controller.beginInteractionPre(20);
            check(controller.clickAtInteractionPre(20).kind==InteractionCommandKind::None,
                "old published angle cannot release revised attack");
            check(controller.arbitrateSprint(true)==!adaptation,
                "silent sprint veto follows the movement adaptation setting");
            if(adaptation) {
                controller.endMovementPhase(20);
                controller.beginInteractionPre(20);
                check(controller.revisePendingAttack(revised,{angle.yaw+.1,angle.pitch},true),
                    "new PRE may refresh geometry after the previous immutable movement consumer");
                check(controller.pendingAttack().intentId==old.intentId&&
                    controller.pendingAttack().entityId==old.entityId&&
                    controller.clickAtInteractionPre(20).kind==InteractionCommandKind::None,
                    "PRE refresh retains target and intent but requires fresh publication");
            }
            const auto packet=controller.packetPlan(true,false);
            controller.acknowledgePacket(packet.rotation,PacketKind::Position,PacketKind::PositionLook,true,true);
            controller.endMovementPhase(20);
            check(controller.clickAtInteractionPre(20).kind==InteractionCommandKind::None,
                "refresh at publication never dispatches in POST");
            controller.beginInteractionPre(20);
            const auto attack=controller.clickAtInteractionPre(20);
            check(attack.entityId==42&&attack.kind==InteractionCommandKind::AttackEntity&&
                RayTraceCoordinator::intersect(currentEye,RayTraceCoordinator::direction(attack.committedRotation),
                    target.bounds,3)>=0,"next PRE dispatches the published current-physics hit without movement starvation");
            controller.attackDispatched(attack,true);
            check(controller.pendingAttack().intentId==0,"completed attack releases its revised transaction");
            input.leftMouseDown=false;
            (void)controller.advance(input,clearTrace);
            check(!controller.revisePendingAttack(revised,angle,true),"LMB release cannot be resurrected by late publication");
        }
    }
    {
        using namespace mcoverlay::silent;
        for(bool nearest:{false,true}) for(bool retain:{false,true}) {
            TargetSelector selector;
            std::array<TargetCandidate,2> candidates{{
                {1,1,{0,1.62,1.0},{-.3,0,.7,.3,1.9,1.3},true,false,false},
                {2,2,{.8,1.62,2.2},{.5,0,1.9,1.1,1.9,2.5},true,true,true}}};
            const auto pick=[&](bool strict) {return selector.select(candidates,{0,1.62,0},
                {0,0},0,6,180,180,nearest,retain,strict);};
            check(pick(false).entityId==1,"availability OFF keeps normal range/angle priority");
            const auto available=pick(true);
            check(available.entityId==2&&available.attackReady,
                "availability ON chooses reachable enemy over unavailable retained/closer target");
            candidates[1].attackAvailable=false;
            check(!pick(true).attackReady,
                "no attackable enemy leaves passive rotation only, never an attack selection");
            candidates[0].attackAvailable=true;
            check(pick(true).entityId==1&&pick(true).attackReady,
                "newly exposed hitbox becomes eligible immediately");
        }
    }
    {
        using namespace mcoverlay::silent;
        // A reachable front face with the preferred interior aim point >3m.
        TargetCandidate edge{81,81,{0,1.62,3.25},{-.3,0,2.95,.3,1.9,3.55},true,false,true};
        TargetSelector selector;
        const auto pick=selector.select({&edge,1},{0,1.62,0},{0,0},0,3,180,180,true,false,true);
        check(pick.valid&&pick.attackReady&&std::abs(pick.distance-2.95)<1e-9,
              "three-block selection admits a reachable hitbox with a deeper aim point");
        edge.bounds.minZ=3.001;
        check(!selector.select({&edge,1},{0,1.62,0},{0,0},0,3,180,180,true,false,true).valid,
              "hitbox beyond configured range remains excluded");
        edge.bounds.minZ=2.95;
        check(!selector.select({&edge,1},{0,1.62,0},{0,0},0,3,180,180,true,false,false).valid,
              "availability off retains legacy point distance semantics");
        LogicalStateController controller;
        LogicalFrameInput input{};
        input.worldGeneration=1;input.localEntityId=1;input.tick=1000;input.physicsTick=1;
        input.enabled=input.silent=input.leftMouseDown=input.coordinateMovement=true;
        input.mode=Mode::LockOn;input.maximumDistance=3;input.attackReach=3;
        input.eye={0,1.62,0};input.fovDegrees=360;
        auto clear=[](const LogicalFramePlan&){return BlockRayHit{true,-1,{}};};
        controller.reset({0,0});
        (void)controller.advance(input,clear);
        const auto idle=controller.movementCommand(.294,0,1);
        check(!idle.enabled&&idle.strafe==.294&&idle.snapshotVersion==0&&
              controller.arbitrateSprint(true,1),
              "held attack without a target leaves movement and sprint vanilla");
        input.candidates={&edge,1};input.tick++;
        const auto lock=controller.advance(input,clear);
        check(lock.silentActive&&controller.movementCommand(0,.98,2).enabled,
              "movement activates when an actual silent target is acquired");
        input.candidates={};input.tick+=1000;
        (void)controller.advance(input,clear);
        check(!controller.movementCommand(.98,0,3).enabled&&controller.arbitrateSprint(true,3),
              "target loss releases movement and sprint while attack stays held");
    }
    std::printf("%d checks, %d failures\n",checks,failed);
    return failed ? 1 : 0;
}
