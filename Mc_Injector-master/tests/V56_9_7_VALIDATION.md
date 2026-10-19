# v56.9.7 — Force target health independence

## Scope and behavior

Force target qualification no longer consumes marker.health or target getHealth().
NaN, positive/negative infinity, zero, negative and normal target health all retain
candidate eligibility when the entity/player/self/position/AABB rules pass. Normal
keeps its exact previous marker health and publication/execution getHealth gates.
Local-player health/state and unrelated movement/game features are unchanged.

- `AimTargetPolicy.h`: remove only Force's finite/positive health predicate.
- `GameBindingsGameplay.cpp`: Force rechecks current-world target lookup, nonself,
  actual player class or exact world-player-list object membership. No remote health
  getter is called in its validation or diagnostic fallback. Normal keeps its
  prior snapshot-only health qualification. Live positive AABB, eye/aimpoint/FOV/
  min/max range, ray/block and attack availability paths remain.
- `CombatTargetValidation.h`: small shared production JNI function for Force's
  current player identity. It receives no health binding, uses no names/TAB/team
  inference, bounds fallback traversal, and releases its list/element local refs.
- `GameBindingsLogical.cpp::refreshAttackAtPublication`: frozen pending.targetFilter
  chooses validation. Force current-player validation skips health entirely;
  Normal retains its prior optional-getHealth expression. Geometry/selection
  reach, actual ray, adaptive point refresh and revision proof remain.
- `executeLogicalInteraction`: frozen command.targetFilter chooses Force current
  player or original Normal health validation. Existing living-class, combat-eye,
  real AABB, published rotation, reach, block trace, swing-before-attack and final
  target checks remain. Transaction proof is checked at entry, after geometry,
  and after swing JNI before actual attackEntity.

`InteractionCommand.targetFilter` is captured when intent/target/rotation proof is
bound, not read from mutable UI at final execution. Pending/click, revision,
cancellation and completion match the frozen filter plus intent/epoch/target and
current effective policy. Applied Normal↔Force transitions invalidate old commands;
switching back does not resurrect them. Late/duplicate completion cannot clear or
count a newer transaction. No SRW lock is held across JNI calls.

No UI, Controller, IPC format, entity collection, ESP world renderer or SmartHotbar
implementation changes. World renderer Git blob remains
72a56f0b43dfc79a595838acbc82fc04d259d631, identical to pre-Force863b92b.

## Diagnostics without inferring unknown health as death

TARGET_DIAG uses stage=selection/publication/execute, effective or frozen targetFilter,
healthIgnored, current player lookup/identity reason, AABB, FOV/min/max/range/ray
facts and intent/rotation proof. Force never emits a target-health dead rejection.
Observed snapshot health may still be printed; it does not authorize or reject.
Actual ray queries distinguish unavailable queries from actual block occlusion;
rayQuerySucceeded in sampling means at least one real query succeeded. Publication
that cannot produce an executable point reports attack_point_unavailable or actual
ray query failure. Execution reports stale transaction at the precise entry,
geometry or swing boundary, as well as lookup/type/bounds/published-ray/block failures.
Existing attack completion events retain final success/failure information.

No real failing player trace was supplied or reproduced this round. These fields
permit inspecting which stage rejects a remaining client-specific target rather
than speculating that every failure is caused by health.

## Executed validation on2026-10-19

Release Controller and Agent build passed with Qt6.10.1 / MinGW13.1 x64.

| Test | Result |
| --- | --- |
| McOverlayAimTargetPolicyTests | 2,202 checks,0 failures |
| McOverlayAttackPolicyTransactionTests | 64 checks,0 failures |
| McOverlayCombatTargetValidationTests | 3,536 checks,0 failures |
| McOverlayAimControlTests | 70,346 checks,0 failures |
| McOverlayRegressionPolicyTests | 208 checks,0 failures |
| McOverlayRendererTests | 5,954 checks,0 failures |
| Historical production WGL Renderer oracle | 2,279 checks,0 failures |
| Current production WGL Renderer parity | 2,605 checks,0 failures;324/324 old outputs identical |
| Frozen v56.9.5 negative control | Expected exit1;172 detected failures |
| Packaged startup without developer DLL paths | Exit0 |
| git diff --check | Passed |

Health tests exercise NaN,±Infinity,0,-1,normal/max finite values, and a ten-step
post-attack health sequence: normal→NaN→0→Infinity→normal→negative→negativeInfinity→
0→NaN→normal. The real LogicalStateController repeatedly acquires, schedules,
publishes, validates and acknowledges attacks, alternating continuously-held
attacks and explicit release/reacquisition. Normal's historical truth table and
finite-positive health behavior remain independently asserted.

Transaction tests cover both switch directions while pending and after a command
is handed out, away-and-back transitions, policy substitution, stale revisions,
cancellations and completion callbacks, fresh intent/rotation proof, duplicate
completion and release. A new command never inherits a previous policy's proof.

JNI tests call the production Force validator through JNINativeInterface function-
table doubles. They spy on CallFloatMethodV, including an intentionally throwing
getHealth implementation, and assert ZERO target-health reads. Tests cover
player-class and exact-world-list identity/aliases, nonplayers, self, missing refs/
bindings, identity exceptions, scoped-ref cleanup, bounded lists and12 repeated
three-stage validations while health changes. Source-call-site regression guards
verify the actual Gameplay/publication/execution branches use the validator and
frozen policy, preserve Normal health expressions and retain final geometry.

ESP parity compares actual submitted geometry, player Hitbox ID sets and complete
GPU framebuffer pixels against the verbatim pre-Force world renderer.324 observed
rows still match. The known-bad renderer now also freezes its original v56.9.5
predicate body from Git, so changing the CURRENT Force health policy cannot
accidentally change the negative oracle. No production ESP code was modified.

## Files and practical limits

Production: AimTargetPolicy.h, new CombatTargetValidation.h,
GameBindingsGameplay.cpp, GameBindingsLogical.cpp, SilentLockCoordinator.h,
src/BuildInfo.h. Tests: AimTargetPolicyTests.cpp, new AttackPolicyTransactionTests.cpp,
new CombatTargetValidationTests.cpp, RegressionPolicyTests.cpp, tests/CMakeLists.txt.
Context docs update the internal policy/transaction contract; no unrelated refactor.

JNI function-table doubles and call-site guards do NOT execute complete
GameBindings final methods inside a live JVM; the logical scheduler tests simulate
successful dispatch acknowledgement. WGL tests run production rendering on
synthetic snapshots. No actual minigame, client mapping, live final JNI attack or
server acceptance was verified here. Test unknown/NaN-health players in a supported
legacy client, repeated attacks/releases and policy changes; inspect TARGET_DIAG
by stage/intent when a target still fails. Existing1.20.1 gameplay limitations remain.
