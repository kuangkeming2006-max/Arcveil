# v56.9.6 — restore pre-Force Player ESP behavior

## Error and narrow repair

The pre-Force PR11 baseline is863b92b33e2c757d272c0045b2c8ffed620c5ac9. The Force
commit86ab03a incorrectly inserted this before the original player rendering path:

```cpp
if(entity.player&&!aim::playerEspHitboxEligible(entity,snapshot.entityId)) continue;
```

It applied Silent Aim's self/health/position/ID/AABB restrictions to Player ESP,
including downstream Nametag/team-box/arrow handling. The requirement is one-way:
ESP keeps its old behavior; only Silent Aim Force broadens player identity.

The three added lines are removed. `overlay_renderer_world.cpp` has an EMPTY diff
against863b92b, with identical Git blob72a56f0b43dfc79a595838acbc82fc04d259d631.
GameBindingsSnapshot.cpp, GameBindingsModern.cpp, renderer draw helpers and final
GameBindingsLogical.cpp attack validation are also unchanged against that baseline.
No entity collection, EntityMarker layout, visibility option, Nametag or teammate
render setting is changed by this correction.

The misleading `playerEspHitboxEligible` helper is renamed to aim-only
`forceTargetCandidateEligible`. It never controls ESP. Force identity still uses
the existing snapshot player classification (actual player class / exact world
player-object membership), ignoring confirmedPlayer, TAB/UUID/name match, game/match
recognition and team colours. Self, alive/valid entity, finite position, positive
real AABB and the existing FOV/range/block/ray/final-attack checks remain in the
Silent Aim pipeline. Normal's exact historical predicate is unchanged. Diagnostics
no longer label an aim-only validity decision as `espHitboxEligible`.

## Actual historical Renderer comparison (not a policy-derived oracle)

New opt-in WGL tests link the identical test harness/common production renderer
objects against three historical/current world renderer translation units:

1. Historical: verbatim863b92b `overlay_renderer_world.cpp`, extracted from Git.
2. Repaired: current production `overlay_renderer_world.cpp`.
3. Negative control: verbatim86ab03a world source, with only a test compilation macro
   mapping the old helper spelling to the renamed equivalent function. This
   reproduces the erroneous global guard without touching production source.

All run production `renderWorldOverlay`, production ImGui/OpenGL2 submission and
actual GPU framebuffer readback. The historical executable writes its observations
first; the repaired executable compares against those outputs, not a new Force
predicate or a rewritten renderer implementation. Each fixture measures actual
Hitbox vertices and nonblack pixels with Nametag/arrows isolated, then compares
complete output including labels/Nametags/team display. Actual ID sets are derived
from those per-entity draw observations. Shapes, indices, vertex colours, clip
commands and complete framebuffer pixels are hashed. UV allocation/process
addresses are excluded only from canonical geometry hashing; full submitted pixels
are compared independently. Frame timing is deterministic and dynamic fonts settle
before capture; no pixel tolerance or weakened set expectation is used.

16 entity cases ×9 display setting modes ×Normal/Force, plus mixed snapshots and
actual drawn-player ID lists produce324 observations and306 PNGs per implementation.
Cases include valid players, TAB mismatch/nickname/unknown game, same-colour armor
and roster teammates, nonplayers/fireballs, distance-dependent labels, and old ESP
renderable but aim-ineligible death/health/ID/degenerate/inverted-box snapshots.
Some adversarial snapshots deliberately bypass normal sampling invariants to prove
that aim validity cannot change renderer output. Sampling itself remains untouched.
NaN-health cases disable Nametag drawing to avoid undefined historical healthbar
geometry; their real Hitbox output is still tested. Team-arrow geometry is compared;
the single-fixture arrow anchor is offscreen, so no visible-arrow screenshot claim.

| Run | Actual WGL checks | Result |
| --- | ---: | --- |
| Pre-Force baseline | 2,279 | 0 failures |
| Repaired current | 2,605 | 0 failures,324/324 historical observations identical |
| Known v56.9.5 regression control | 2,605 | Exit1 and172 expected failures |

Negative control has158 differing historical-output rows plus14 positive-draw
assertion failures. In the boxes scenario the historical/repaired output contains
14 supplied player Hitboxes, while the regression control contains7, dropping the
seven aim-ineligible fixture types under BOTH Normal and Force. These are rendering
outputs, not an aim helper's reconstructed expected set.

Historical and repaired results.tsv SHA256 are identical:
28fec075874e3460de91ccbbbf3263665f23f01942153120eee284ad1d033d6d.

Representative death/zero-width baseline and repaired PNGs were visually inspected:
red box/line in both; the known-regression images are black. The healthy nickname,
TAB-mismatch and team variants retain their original rendering. All nine settings
modes' Normal/Force drawn-ID sets, geometry and pixels are identical.

## Other validation on2026-10-19

- Release Controller and Agent built (Qt6.10.1 / MinGW13.1 x64).
- McOverlayAimTargetPolicyTests:2,127 checks,0 failures. Independent historical
  Normal truth table plus self/dead/NaN/invalid geometry stage checks; Force accepts
  valid player identities rejected by Normal for TAB/nick/game/team metadata,
  while existing geometry, ray/reach, final transaction and filter-switch checks
  remain. Old-renderer player classification expectations are independent of the
  new Force safety predicate.
- McOverlayAimControlTests:70,346 checks,0 failures.
- McOverlayRegressionPolicyTests:201 checks,0 failures.
- McOverlayRendererTests:5,954 checks,0 failures.
- Dependency-bundled Arcveil --smoke-test with developer directories removed from
  PATH:exit0. Packaged Agent/Controller hashes match Release build outputs.
- git diff --check passed.

## Reproduce and inspect

```powershell
cmake -S Mc_Injector-master -B build-main-release -DMC_BUILD_ESP_RENDERER_PARITY_TESTS=ON
cmake --build build-main-release --target McOverlayEspRendererHistoricalParity
cmake --build build-main-release --target McOverlayEspRendererRegressedTests
```

Run McOverlayEspRendererRegressedTests with output directory and historical baseline
directory; expect exit1. Git history containing the pinned baseline/regression
commits is required; configuration fails if those sources cannot be read, rather
than substituting current code. Runtime needs the normal Qt/MinGW DLL search path.
Evidence is `build-main-release/tests/esp-renderer-parity/{baseline,repaired,
known-regression}`: renderer.txt, results.tsv, summary.txt and actual frame PNGs.
The test package includes these outputs under `renderer-evidence/`.

Changed production files are ONLY overlay_renderer_world.cpp (remove guard),
AimTargetPolicy.h (aim-only helper name/comments), GameBindingsGameplay.cpp
(helper call/diagnostic wording) and BuildInfo.h (v56.9.6-esp-parity). Test files:
AimTargetPolicyTests.cpp, new EspRendererParityTests.cpp and tests/CMakeLists.txt.
Context/validation documentation corrects the previous two-way interpretation.
No unrelated module refactoring, new targeting algorithm, IPC or setting changes.

## Limits

The historical/current comparison runs real NVIDIA WGL production rendering code
on supplied deterministic GameSnapshots. It is NOT a running Minecraft/client or
server test. No real-game entity sampling, nicked player session, live final JNI
attack execution or server acceptance was exercised this round. The unchanged
sampling/final attack source and focused policy/transaction regressions support
the scope; they do not claim real-client integration success. Existing1.20.1
gameplay capability limits remain.
