#include "../agent/bindings/KnockbackEvidence.h"
#include <cstdio>
int main() {
    using namespace mcoverlay::prediction;
    int checks = 0, failed = 0;
    auto check = [&](bool value, const char* description) {
        ++checks; if (!value) { ++failed; std::printf("FAIL %s\n", description); }
    };
    HurtSample ground{20, 0, true, true, {0,0,0}};
    HurtSample hurt = ground; hurt.hurtTime = 10;
    HurtSample air{20, 9, false, true, {.22,.30,0}};
    KnockbackEvidence ordered;
    check(!ordered.update(ground,1000).triggered,"first observation cannot trigger");
    check(!ordered.update(hurt,1050).triggered,"hurt without movement cannot trigger");
    check(ordered.update(air,1100).triggered,"hurt then airborne next tick triggers with unchanged health");
    air.hurtTime=8; air.velocity={.20,.21,0};
    check(!ordered.update(air,1150).triggered,"same damage never repeats");
    KnockbackEvidence reverse;
    reverse.update(ground,1000);
    HurtSample moving{20,0,false,true,{.22,.3,0}};
    check(!reverse.update(moving,1050).triggered,"movement first waits for confirmation");
    moving.hurtTime=10; moving.velocity={.20,.21,0};
    check(reverse.update(moving,1100).triggered,"movement then hurt status is also correlated");
    KnockbackEvidence jump;
    jump.update(ground,1000);
    moving.hurtTime=0;
    check(!jump.update(moving,1050).triggered,"ordinary jump/empty swing has no hurt evidence");
    KnockbackEvidence stationary;
    stationary.update(ground,1000);
    check(!stationary.update(hurt,1050).triggered,"grounded damage alone has no prediction");
    KnockbackEvidence timeout;
    timeout.update(ground,1000); timeout.update(hurt,1050);
    for (unsigned t=1100;t<=1400;t+=50) { hurt.hurtTime--; timeout.update(hurt,t); }
    moving.hurtTime=0;
    check(!timeout.update(moving,1450).triggered,"expired hurt cannot combine with a later jump");
    KnockbackEvidence missing;
    ground.groundKnown=false; missing.update(ground,1000);
    moving.groundKnown=false; moving.hurtTime=10;
    check(!missing.update(moving,1050).triggered,"unknown ground mapping fails closed");
    ground.groundKnown=true; ground.hurtTime=-1;
    KnockbackEvidence healthFallback;
    healthFallback.update(ground,1000);
    moving={18,-1,false,true,{.2,.3,0}};
    check(healthFallback.update(moving,1050).triggered,"actual health drop works without hurt mapping");
    KnockbackEvidence gap;
    gap.update(ground,1000);
    check(!gap.update(moving,2000).triggered,"unload/teleport gap resets evidence");
    const auto coherent=confirmVelocity({.40,.36,.10},{.32,.24,.08},50);
    check(coherent.confident&&coherent.directionAgreement>0.99,
          "next-tick displacement confirms a coherent impulse");
    check(std::hypot(coherent.residual.x,coherent.residual.z)<
          std::hypot(.32,.08),"confirmed residual advances one horizontal drag step");
    check(!confirmVelocity({.40,.36,0},{-.25,.20,0},50).confident,
          "opposite displacement rejects an apparent impulse");
    check(!confirmVelocity({.40,.36,0},{.32,.20,0},140).confident,
          "stale interpolation cannot confirm a trajectory");
    check(!confirmVelocity({.10,.30,0},{1.20,.20,0},50).confident,
          "teleport-sized speed disagreement fails closed");
    std::printf("%d checks, %d failures\n",checks,failed);
    return failed ? 1 : 0;
}
