#include "../agent/bindings/SilentLockCoordinator.h"

#include <cstdio>

namespace {

using mcoverlay::aim::AimTargetFilter;
using namespace mcoverlay::silent;

int checks=0,failures=0;
void check(const bool ok,const char* const description) {
    ++checks;
    if(!ok) {++failures;std::printf("FAIL %s\n",description);}
}

constexpr auto clearTrace=[](const LogicalFramePlan&) noexcept {
    return BlockRayHit{true,-1,{}};
};

struct Fixture final {
    LogicalStateController controller{};
    TargetCandidate target{42,42U,{0,1.62,2.0},
        {-.3,0,1.7,.3,1.9,2.3},true,false,true};
    LogicalFrameInput input{};
    std::uint64_t now=1000000U;

    explicit Fixture(const AimTargetFilter filter) {
        controller.reset();
        input.tick=1000;input.physicsTick=20;input.worldGeneration=1;
        input.localEntityId=1;input.enabled=input.silent=input.leftMouseDown=true;
        input.mode=mcoverlay::aim::Mode::LockOn;input.targetFilter=filter;
        input.maximumDistance=3.5;input.attackReach=3;input.fovDegrees=90;
        input.eye={0,1.62,0};input.candidates={&target,1};
        sample();
    }
    void sample() {
        ++input.tick;++input.physicsTick;
        (void)controller.advance(input,clearTrace);
    }
    InteractionCommand arm() {
        now+=100000U;
        controller.updateAttackClock(true,true,now,10);
        sample();
        return controller.pendingAttack();
    }
    void publish() {
        const auto packet=controller.packetPlan(false,false);
        controller.acknowledgePacket(packet.rotation,PacketKind::Ground,
            PacketKind::Look,false,true);
    }
    InteractionCommand dispatch() {
        publish();
        controller.beginInteractionPre(input.physicsTick);
        return controller.clickAtInteractionPre(input.physicsTick);
    }
    void setFilter(const AimTargetFilter filter) {
        input.targetFilter=filter;
        sample();
    }
};

void frozenPolicyAndProof() {
    for(const auto filter:{AimTargetFilter::Normal,AimTargetFilter::Force}) {
        Fixture fixture{filter};
        const auto pending=fixture.arm();
        check(pending.kind==InteractionCommandKind::AttackEntity&&
            pending.targetFilter==filter,"pending target captures its source policy");
        check(!fixture.controller.validateAttackTransaction(pending),
            "a pending intent is not an executable attack");
        check(fixture.controller.click().kind==InteractionCommandKind::None,
            "policy propagation retains exact rotation publication requirement");

        auto changed=pending;
        changed.targetFilter=filter==AimTargetFilter::Normal
            ?AimTargetFilter::Force:AimTargetFilter::Normal;
        check(!fixture.controller.revisePendingAttack(changed,
            pending.committedRotation,true),"a refresh cannot substitute policy");
        check(!fixture.controller.cancelPendingAttack(changed),
            "a mismatched policy cannot cancel another transaction");
        check(fixture.controller.pendingAttack().intentId==pending.intentId,
            "rejected policy substitution preserves the original pending intent");

        const auto command=fixture.dispatch();
        check(command.kind==InteractionCommandKind::AttackEntity&&
            command.targetFilter==filter&&command.intentId==pending.intentId&&
            command.rotationEpoch==pending.rotationEpoch,
            "dispatch preserves the source policy and exact target/rotation proof");
        check(fixture.controller.validateAttackTransaction(command),
            "current published target transaction is executable");
        auto invalid=command;invalid.targetFilter=changed.targetFilter;
        check(!fixture.controller.validateAttackTransaction(invalid),
            "final execution rejects a substituted target policy");
        invalid=command;++invalid.intentId;
        check(!fixture.controller.validateAttackTransaction(invalid),
            "final execution rejects a different intent");
        invalid=command;++invalid.rotationEpoch;
        check(!fixture.controller.validateAttackTransaction(invalid),
            "final execution rejects a different rotation epoch");
        invalid=command;++invalid.entityId;
        check(!fixture.controller.validateAttackTransaction(invalid),
            "final execution rejects a different target");
        invalid=command;invalid.committedRotation.yaw+=1;
        check(!fixture.controller.validateAttackTransaction(invalid),
            "final execution rejects a rotation without publication proof");
        fixture.controller.attackDispatched(command,true);
        check(fixture.controller.latest().attackDispatchCount==1&&
            !fixture.controller.validateAttackTransaction(command),
            "completed transaction cannot execute again");
        fixture.controller.attackDispatched(command,true);
        check(fixture.controller.latest().attackDispatchCount==1,
            "duplicate completion cannot count a second attack");
    }
}

void filterSwitchPendingAndInFlight() {
    for(const auto before:{AimTargetFilter::Normal,AimTargetFilter::Force}) {
        const auto after=before==AimTargetFilter::Normal
            ?AimTargetFilter::Force:AimTargetFilter::Normal;
        for(const bool inFlight:{false,true}) {
            Fixture fixture{before};
            auto old=fixture.arm();
            if(inFlight) old=fixture.dispatch();
            fixture.setFilter(after);
            check(fixture.controller.pendingAttack().kind==InteractionCommandKind::None,
                "switching target filter clears pending or dispatched attack requirement");
            check(!fixture.controller.validateAttackTransaction(old)&&
                !fixture.controller.revisePendingAttack(old,old.committedRotation,true)&&
                !fixture.controller.cancelPendingAttack(old),
                "old source policy cannot validate refresh or cancel after mode switch");

            // Returning to the same enum value must not revive its old proof.
            fixture.setFilter(before);
            check(!fixture.controller.validateAttackTransaction(old),
                "switching away and back cannot revive an old attack");
            const auto current=fixture.arm();
            check(current.intentId!=0&&current.intentId!=old.intentId&&
                current.targetFilter==before,
                "reacquisition after filter switches uses a fresh monotonic intent");
            const auto command=fixture.dispatch();
            check(fixture.controller.validateAttackTransaction(command),
                "new same-policy transaction executes after filter switches");
            fixture.controller.attackDispatched(old,true);
            check(fixture.controller.latest().attackDispatchCount==0&&
                fixture.controller.latest().committedAttackTargetId==command.entityId&&
                fixture.controller.validateAttackTransaction(command),
                "stale completion cannot clear or count against the newer transaction");
            fixture.controller.attackDispatched(command,true);
            check(fixture.controller.latest().attackDispatchCount==1,
                "only the current filter transaction completes");
        }
    }
}

void publicationRefreshAndRelease() {
    Fixture fixture{AimTargetFilter::Force};
    const auto old=fixture.arm();
    check(fixture.controller.revisePendingAttack(old,{0,1},true),
        "current pending Force intent can refresh its rotation");
    const auto refreshed=fixture.controller.pendingAttack();
    check(refreshed.intentId==old.intentId&&refreshed.rotationEpoch>old.rotationEpoch&&
        refreshed.targetFilter==AimTargetFilter::Force,
        "rotation refresh retains policy and renews its exact publication epoch");
    check(!fixture.controller.revisePendingAttack(old,old.committedRotation,true)&&
        !fixture.controller.cancelPendingAttack(old),
        "stale pre-refresh proof cannot revise or cancel the replacement");
    const auto command=fixture.dispatch();
    check(fixture.controller.validateAttackTransaction(command),
        "refreshed rotation becomes executable only with current proof");
    fixture.controller.observeCameraInput({}, {},-1,false,fixture.input.physicsTick);
    check(!fixture.controller.validateAttackTransaction(command),
        "release still invalidates an already returned attack command");
    fixture.controller.attackDispatched(command,true);
    check(fixture.controller.latest().attackDispatchCount==0,
        "released stale completion cannot manufacture a successful dispatch");
}

} // namespace

int main() {
    frozenPolicyAndProof();
    filterSwitchPendingAndInFlight();
    publicationRefreshAndRelease();
    std::printf("Attack policy transaction checks: %d, failures: %d\n",checks,failures);
    return failures?1:0;
}
