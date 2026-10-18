# v56.9.3 — Lunar runtime recovery and legacy feature bindings

Executed 2026-10-18, Windows x64, Release, Qt 6.10.1 / MinGW 13.1.
Baseline: PR #10 `4a84cbe` (v56.9.2); PR remains Draft.
Detailed source/history evidence: [root causes](../../../docs/context/V56_9_3_ROOT_CAUSES.md).

## Root causes and changes

1. Real Lunar's `version.json` id is
   `1.8.9-forge1.8.9-11.15.1.2318-1.8.9`, while its exact same-loader Realms
   `VERSION_STRING` is `1.8.9`. The unusable resource diagnostic masked the valid
   legacy source. Probe-v17 preserves per-source attempts and accepts the latter;
   conflicting exact sources still reject.
2. Its observed brand is `lunarclient:v2.23.4-2642,fml,forge`. Sanitized
   `modified` previously became Unknown after version recovery. The audited
   release syntax now also requires a strong Lunar marker in that exact loader.
   The Lunar release never determines Minecraft version.
3. After identity recovery, an absent Notch namespace returned a completed
   zero-class validation scope. A typed, PID/start-bound candidate-scope miss
   advances the initial authored candidate instead of aborting before valid SRG.
   Empty Detection, incomplete transport and failed final validation still fail.
4. The official 1.8.9 Notch PlayerControllerMP sync mapping was `j()V`, but the
   actual method is `n()V`; `j()` returns boolean. The shared optional predicate
   disables both Scaffold and Smart Hotbar when this lookup fails. The defect is
   already in v54, predating required-only Snapshots. Both Notch dictionaries in
   default-v1/v2 are corrected; legacy audit revision invalidates old 1.8.9 Cache
   proofs once. Modern sub-contract and capability ceilings are unchanged.

Unknown version recovery is bounded: one extra Detection capture, known modified
family, one verified candidate loader and at most two audited versions. Legacy
semantic evidence requires the exact audited Realms class in that defining
loader, protocol 47/ticks 20 and exactly one matching semantic String value.
Candidate declarations, old Cache, titles and process names never authorize
identity. Missing/conflicting evidence remains unsupported. Final capture repeats
the independent evidence before Verified Runtime Context and normal Agent Ready.

## Actual Minecraft observations

Two repaired sessions on the user's real Lunar 1.8.9 process, PID 56528,
Zulu 17.0.18, selected **Lunar SRG**, authenticated Agent, verified exact JNI,
observed Renderer Ready/Active and detached normally. Default features remained
off and no game input was sent. This is Attach acceptance, not gameplay acceptance.

| Capture in first successful session | Request bytes | Requested / captured classes | Fields / methods | Probe duration |
| --- | ---: | ---: | ---: | ---: |
| Detection | 1263 | 4 / 2 (34069 loaded) | 0 / 0 | 63 ms |
| Authored MCP validation, mismatch | 2801 | 18 / 70 | 548 / 3127 | 547 ms |
| Authored Notch scope absent | 1594 | 18 / 0 | 0 / 0 | 187 ms |
| Authored SRG validation | 2801 | 18 / 70 | 548 / 3127 | 578 ms |
| Independent final SRG validation | 2801 | 18 / 70 | 548 / 3127 | 562 ms |

Automatic Resolver **0**, Structural/detail captures **0**, automatic Mapping
window triggers **0**, Cache hit **false**. All transported bytecode/constant-pool
bytes are zero. Each successful Modded Validation nevertheless reads and hashes
234690 bytecode / 1055824 constant-pool bytes inside JVMTI for compact installed
proof; zero transported detail must not be described as zero JVMTI work.
Hierarchy expansion explains 70 captured classes from 18 explicit requests.
Request-file cap remains 32768 bytes.

First session: Active at about 19610 ms, final acceptance receipt 19704 ms;
Agent Ready 403 ms, JNI 47 ms, native main-Agent loading 148 ms. Initial Standard
Attach was unavailable; NativeLoader succeeded. Its approximately 11-second
standard-attempt latency contributes to total time and remains an optimization
opportunity, not a mapping failure.
Second session: Active at 19300 ms, then a five-second read-only observation;
receipt 24589 ms, Agent Ready 359 ms / JNI 31 ms. It reported bindingMask=15,
missingBindings=0, hookMask=0, updateCalls=31, FeatureDisabled and zero actions.
This proves observed optional Lunar predicates, not enabled feature hooks.

Evidence: `.research/v56.9.3-real-lunar-attach2.log`,
`.research/v56.9.3-real-lunar-observe.log`, and the preceding read-only
`build-main-release/v56.9.3-real-lunar-detection.jsonl`. The final Probe/Agent
sources and SRG data were used; the later Notch-only correction and legacy Cache
audit revision were not followed by another real game session.
The user cannot provide further render-frame/world validation this round.

The unchanged recorded real Vanilla 1.8.9 v56.9.2 run used Detection 1265 bytes,
2 captured / 8322 loaded classes, then two 1503-byte Vanilla Validation captures,
18 requested / 27 captured classes, 94/78 ms. It reached Mapping/Agent/Renderer
Ready, total 9239 ms, Automatic Resolver/detail/window triggers zero. These are
historical measurements, not a new final-build Vanilla Attach or feature test.

| Environment | Mapping Ready | Agent/JNI Ready | Renderer Ready | Scaffold / Smart Hotbar |
| --- | --- | --- | --- | --- |
| Real Lunar 1.8.9, two repaired sessions | Observed | Observed | Observed during Attach | Disabled; no behavior acceptance |
| Real Vanilla 1.8.9, historical v56.9.2 session | Recorded | Recorded | Recorded Active | User reports both ineffective; no corrected-build world test |
| Final Vanilla 1.8.9 official classes | Artifact/JNI audit passes | Only owned-JVM member lookup | Not exercised | Binding defect corrected; behavior unverified |
| Forge 1.8.9 | Synthetic identity/recovery boundary tests | No fresh real game test | No fresh real game test | Unverified |
| Vanilla 1.20.1 | Owned synthetic JVM passes | Owned synthetic full-Agent test passes | Owned WGL core fixture passes | Legacy modules deliberately unsupported |
| Forge 1.20.1 | Correctly identified but API unsupported | Refused | Not applicable | Unsupported |

## Automated validation and evidence classes

All listed successful runs exited zero. Original failing evidence is retained;
it is not relabeled as a passing test.

| Test | Result | Actual scope |
| --- | --- | --- |
| Final Release build and install | Pass | Controller, Agent, Analyzer, Probe-v17 and deployment data |
| Test-LegacyArtifact.py | 257 rows / 193 configured class-member checks, no misses; two wrong-descriptor negatives | Offline official client JAR, SHA-1 `3870888a6c3d349d3771a3e9d16c9bf5e076b908`; two empty optional aliases inactive |
| LegacyJniTests | 12 / 0 failures | Owned Java17 JVM, official 1.8.9 classes + libraries; correct sync/right-click/windowClick and rejection of historical/wrong signatures; no Minecraft instance |
| MappingPack / MappingProvider | 69 checks / provider pass | Historical parity with explicit asserted `j` to `n` audit exception; v1/v2 consistency |
| MappingService | 43 / 0 failures | Mock subprocesses; recovery once, missing/wrong version-family-loader rejection, empty candidate scope, silent success; bounded current-owner persisted receipts |
| TransactionCache | 32 / 0 failures | Production Analyzer over synthetic installed-code snapshots, Cache/authored/Resolver trigger and independent final proof |
| DynamicMappingService | 92 / 0 failures | Mock dynamic state machine, cancellation/rollback/change tracking |
| RuntimeEvidence, Java8 and Java17 | 170 each | Owned synthetic JVMs; missing/oversized/unusable resource, exact source conflict, inaccessible/wrong-loader legacy, bounded semantic recovery and negatives |
| RuntimeDetection | 165, including 14 assertions on four unchanged recorded Snapshots | Remaining metadata synthetic; Vanilla/Forge/Lunar/version/namespace/loader boundaries |
| ModernPipeline, final rerun | 50 | Owned synthetic Java17; typed absent candidate scope, exact descriptors, Cache/family/version boundaries |
| ModernMappingService, final rerun | 18 / 0 failures | Owned synthetic Java17; two silent Mapping transactions, no Agent in this service test |
| Request pressure | 118 | Offline planner, 1200 historical packs; Detection remains 1029 bytes / 4 anchors / zero warmup, independent of history |
| Mapping boundary / GameApi | 32 / 43, zero failures | Pack/hash/cache/options boundary / mocked JNI facade |
| ModernJvm / gameplay policy | 39 / 201, zero failures | Owned Java17 JNI / source policy and lifecycle guards |
| Controller IPC | Pass | Config/runtime receipts, auth/current-owner gate, malformed/oversized rejection, exact uint64, heartbeat responsiveness |
| Packaged Controller UI | 40 phases | Actual Qt/QML clicks/modal transitions with bundled dependencies and isolated preferences; Controller D3D11, not Minecraft rendering |
| Resolver / Incremental / TwoStage / VersionAdapters | Pass | Offline synthetic full-schema rename, unsupported API, descriptor, loader and provisional-proof refusal |
| Full Agent + OpenGL core | Pass | Owned Java17 synthetic game objects; Agent/Renderer Ready, clean Detach, modern mask=8/missing=0/reason UnsupportedVersion, no invented actions |

Final modern service metrics (`build-main-release/tier-metrics-modern.json`):
Detection 1288 bytes, 4 requested / 1 captured, 31/16 ms. Vanilla Validation
1456 bytes, 12/12 classes, 14 fields / 36 methods, first/final 16/16 ms and
16/32 ms. Two Mapping transactions take 3231/3169 ms including helper startup;
Automatic Resolver **0**, Mapping-window triggers **0**, transported and JVMTI
bytecode/pool **0**. This is a synthetic API, not real Minecraft 1.20.1.

Cache is attempted only after independent actual identity. A hit must match
version/family/contract/loader/current installed proof and final recheck. Changed
proof or old legacy audit domain misses; authored revalidation is tried next.
Actual repaired Lunar used authored SRG with no Cache hit. Automatic Resolver
needs failed existing candidates in a supported modified environment, Structural
evidence and a compatible verified reference. No reference/ambiguous evidence
cannot export Ready. Those Resolver/Cache recovery conditions passed synthetic
regressions; they were not artificially triggered in the user's game.

## Functional diagnostic acceptance still required

Use the final extracted package, real Vanilla 1.8.9, a visible continuously
drawing game, first singleplayer and then the user's server. Preserve the
transaction JSONL and Controller log. Authenticated receipts are persisted as
nested `AGENT_GAMEPLAY_DIAGNOSTIC` events in the existing Attach run's
`events.jsonl`, so double-click launches retain the same evidence without a debug
console. Persistence separately checks the current transaction/PID and refuses
oversized receipts; it cannot authorize Ready. Keep these steps separate:

1. UI switches show intended Scaffold/Smart Hotbar settings.
2. `SCAFFOLD_CONFIG_APPLIED` / `SMART_HOTBAR_APPLIED` confirm Agent config receipt.
3. `AGENT_GAMEPLAY_DIAGNOSTIC` has observed binding bits (`bindingMask & 12 == 12`),
   Scaffold/Hotbar predicates and no missing dependency groups. A zero before
   observation is inconclusive. Existing JNI errors remain errors.
4. Relevant consumed-key/item-use/movement hook bits become available after
   enabling the function; installed hooks alone do not prove callbacks.
5. In-world, foreground, mouse captured, GUI closed and eligible inventory/input
   conditions produce actual update/callback/action or placement-attempt counts.
   Record `lastBlockReason` when they do not. No guards were removed or bypassed.
6. Check visible placement/refill/slot behavior and server acceptance separately.
   `scaffoldAccepted` is a true local JNI result; `hotbarActions` is a successful
   local mapped operation. Neither is a server acknowledgment.

The 1 Hz best-effort diagnostic can combine short-lived states. Its dependency
mask describes unavailable groups, including IDs skipped after an earlier lookup
failure; it is not an exact individual-symbol error list. Actual gameplay,
long-running game lifecycle, real Forge and real 1.20.1 remain unverified here.
