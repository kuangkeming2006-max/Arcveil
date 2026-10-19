# v56.9.5 — Silent Aim Force Target

> Superseded for ESP behavior: this version incorrectly narrowed Player ESP through
> a new aiming-validity guard. Its prior policy/UI tests did not establish parity
> with pre-Force ESP. v56.9.6 removes that guard and adds actual historical-renderer
> comparisons; see V56_9_6_VALIDATION.md. The original record below is historical.

## Before / after

| Policy | Player identity qualification | Team exclusion | Geometry and attack |
| --- | --- | --- | --- |
| Normal (default) | Exactly the prior confirmedPlayer OR (matchActive AND colouredPlayer) OR (!hypixelServer AND player) | Prior Hypixel recognized-match teammate predicate | Existing selector, FOV/min/max, physics Hitbox, reach/ray/block/PRE validation |
| Force (Silent Aim only) | Same valid player Hitbox predicate as Player ESP; no TAB, UUID, nickname or match requirement | None | Same existing geometry and final interaction pipeline |

Force is stored independently and never changes Smooth or visible Lock On. Both
binaries in the package must be used together. Old/missing settings select Normal.
A player-shaped NPC shown by the shared Player ESP classification is admitted by
Force, subject to all remaining geometry and interaction checks.

## Actual ESP audit and implementation

- `GameBindingsSnapshot.cpp::sample` / `readEntityMarker`: marker.player comes
  from actual player class or exact object membership in world.playerEntities.
  UUID/name TAB matching sets confirmedPlayer separately. No display-name, colour
  or string is used to infer a Hitbox. `sampleModern` also uses actual player
  class; modern gameplay remains unsupported by the existing capability gate.
- `overlay_renderer_world.cpp::renderWorldOverlay`: player Hitboxes did not use
  confirmedPlayer, TAB or match recognition for identity. Team visibility and
  projection were display concerns; nametags have a separate confirmed gate.
- `AimTargetPolicy.h`: single typed identity policy, exact Normal truth table,
  shared valid player Hitbox predicate for Force and Player ESP. Adds explicit
  nonself/nonnegative ID, finite positive health, reliable finite current/previous
  positions and finite positive-volume bounds. Player ESP is also hardened to
  exclude invalid/dead player boxes; this does not change Normal aim qualification.
- `GameBindingsGameplay.cpp`: chooses effective policy only for Silent Aim; retains
  live world lookup, combat-eye, live positive Hitbox and old aim-point/ray logic.
  Force verifies live living-class compatibility before getHealth (safe with stale
  snapshot / entity-ID reuse), life and nonself. No new playerClass gate excludes
  the snapshot's exact-world-player-object fallback.
- `SilentLockCoordinator.h`: filter change enters existing context cancellation:
  selector, smoother, target grace, sequential cooldown, movement ownership, CPS
  pending attack and packet rotation proof cannot survive a policy change.
- `TARGET_DIAG`: targetFilter, markerPlayer/confirmedPlayer/matchActive/teamColor,
  identityEligible/forceAllowed, lookup, bounds, distance/FOV/attack availability,
  reason, final selected candidate plus existing ray/attack/commit state.
- `CANDIDATE_SCAN`: per-marker rows every five seconds plus summary counts. Identity,
  invalid entity/Hitbox, lookup/bounds, FOV/range and ray/reach failures are distinct.
  Debug-off path does not emit these rows. Existing final attack events remain.

## Settings and changed files

- UI: `agent/ui/{UiModel.h,ClickGui.cpp}` — Target Filter Normal/Force under Silent
  Control, persistent selection, disabled outside Silent Lock, precise description.
- Core: `agent/bindings/{AimTargetPolicy.h,GameBindings.h,GameBindingsGameplay.cpp,
  SilentLockCoordinator.h}`, `agent/overlay_renderer_world.cpp`.
- Pipeline: `agent/src/AgentRuntime{.h,Control.cpp,Features.cpp,Frame.cpp,Telemetry.cpp}`,
  `agent/include/mcoverlay/ControlProtocol.h`; independent AIM_TARGET_FILTER,
  AIM_TARGET_FILTER_CHANGED and AIM_TARGET_FILTER_APPLIED with strict0/1. Existing
  FEATURE_STATE position fields and other Aim options are unchanged.
- Controller: `src/OverlayManager{.h,Settings.cpp,Config.cpp,Protocol.cpp}` — Qt
  property, strict setter, QSettings/named profiles; absent/invalid loads Normal.
- Version: `src/BuildInfo.h` — v56.9.5-force-target.
- Tests: new `tests/AimTargetPolicyTests.cpp`, `tests/CMakeLists.txt`, existing
  ControllerResponsivenessTests, OverlayRendererTests and RegressionPolicyTests.
- Context: game-bindings/renderer/controller/agent-runtime/ipc modules and dependency
  boundary documentation. No Java bridge, mapping symbol or entity IPC change.

## Validation on 2026-10-19

Release Controller and Agent build passed (Qt6.10.1 / MinGW13.1 x64).

| Executed test | Result |
| --- | --- |
| McOverlayAimTargetPolicyTests | 1,472 checks, 0 failures |
| McOverlayAimControlTests | 70,346 checks, 0 failures |
| McOverlayRegressionPolicyTests | 201 checks, 0 failures |
| McOverlayRendererTests (actual NVIDIA OpenGL4.6) | 5,954 checks, 0 failures |
| McOverlayControllerResponsivenessTests | Passed, including real local IPC and saved profiles |
| Packaged Arcveil --smoke-test with developer directories removed from PATH | Exit0 |
| git diff --check | Passed |

Acceptance1: exhaustive independent old Normal identity truth table including
unknown team values. Acceptance2–6: valid ESP player eligibility, unconfirmed/nick/
UUID-name mismatch/unrecognized games/teammates/player-shaped NPCs, and nonplayer
metadata spoofing. Acceptance7: self, dead, nonfinite position/health and empty/
inverted/nonfinite Hitboxes. Acceptance8: Force candidates pass through unchanged
FOV, minimum/maximum distance, block occlusion, legal reach and final-ray pipeline.
Acceptance9: both switch directions with active published CPS intents, absent or
still-valid target; stale attack proof and revisions cannot dispatch.
Acceptance10: default/strict IPC, actual outgoing pipe frames, named/legacy profile
and QSettings restore, UI dirty/echo plus Normal restoration. Dark/light/disabled
screenshots visually checked; descriptions fit and Target Filter remains visible.

## Limits and real-game acceptance still needed

No real Minecraft gameplay was exercised in this change. Policy and transaction
fixtures do not prove current client mappings, world/TAB sampling, ray callbacks,
input hooks or server-accepted attacks. Live JNI final revalidation was preserved,
not exercised by these pure policy tests. Packaged startup smoke checks the
Controller; WGL tests check production rendering with synthetic snapshots.

On a supported legacy client, compare the same nicked/unconfirmed/same-team player
in Normal and Force with Player ESP enabled. Record TARGET_DIAG and CANDIDATE_SCAN
with effective targetFilter; Force should report forceAllowed=1/identityEligible=1
for each valid player marker. A candidate may still be rejected by live bounds,
FOV, minimum/maximum distance, obstruction or attack reach. Change Force→Normal and
back while holding attack and verify no previous-policy intent dispatches. Include
NPCs displayed as players, mobs, dead players, behind-wall/out-of-range targets,
reconnect and restored configuration. ESP/team display toggles and screen projection
do not restrict Force's underlying entity classification. Existing1.20.1 inventory/
gameplay capability limitations remain.
