# v51 validation and live acceptance

## Change audit

- AimAssist separates a 3.5-block pre-aim acquisition range from the current
  physics-space 3.0-block attack gate when availability checking is enabled.
  A pending CPS intent binds immediately to one target/rotation, waits through
  temporary unavailability without being consumed as `no_target`, and dispatches
  at the next valid input/PRE boundary only after that rotation was published.
  Revalidation cancels a genuinely missing, dead or out-of-pre-aim target.
- `TARGET_DIAG` reports camera/attack-entry entity classification, world lookup,
  physics bounds, reach, visibility, FOV and selector outcome. Missing selected
  candidates force one diagnostic; ordinary output is rate-limited.
- Movement correction retains continuous inverse-yaw forward/strafe values
  rather than quantizing them to eight keyboard directions.
- Smart Hotbar key/use hooks only enqueue requests. Hotbar selection occurs at
  input/PRE; main-inventory transfers wait until there is no held movement key,
  meaningful horizontal velocity, sprint, attack or use action. A shortcut with
  no matching item falls back to its normal slot.
- TSF input-language reset restores any native candidate UI previously hidden
  by the overlay, invalidates old composition/candidate state, and defers
  settling until after the transition. Reused `UpdateUIElement` IDs can then
  refresh the overlay. The corrected COM interface ABI is unchanged.
- Attack Shield's enabled and wildcard (`*`) selections are persisted with
  the other feature settings.

The v43 AimAssist/FreeLook brief and fixed-CPS repair guide were reread after
editing. This release preserves their shared invariants: held-left ownership,
SCA independent of attack scheduling, availability gating dispatch only,
publish-before-attack, one PRE dispatch path and consumer-derived movement.
Where the older guides conflict, the user's later transaction, sprint and
pre-aim instructions take precedence. FreeLook hooks were not changed here.

## Automated verification

- Debug and Release builds completed. Remaining compiler warnings are in
  third-party ImGui/STB code and existing unrelated rendering code.
- AimControl: 68,548 checks, zero failures.
- RegressionPolicy: 175 checks, zero failures.
- LogicalPipeline JVM: 154 checks, zero failures.
- Renderer/IME: 4,366 checks, zero failures.
- Navigation/trajectory: 43,271 checks, zero failures.
- Mapping provider, hotkey capture, controller responsiveness, skin/wheel and
  knockback tests passed.

## Live acceptance still required

Automated fixtures cannot reproduce a particular server, Lunar client or
Chinese TIP. In a permitted test world, verify rapid movement toward a target
from just outside 3.0 blocks, sustained held-left CPS, immediate release,
pre-aim occlusion, Smart Hotbar refill while moving and while neutral, repeated
Chinese input-language switches, and persistence of Attack Shield `*` after
restart. Capture `TARGET_DIAG` when a visible PvP player is not selected; its
`markerFound`, `markerPlayer`, `livePlayer` and `reject` fields distinguish
snapshot omission from eligibility or geometry rejection.
