# v56.9.2 — tiered Snapshot verification

Executed 2026-10-18, Windows x64, Qt 6.10.1 / MinGW 13.1 Release.
PR #10 remains Draft. This extends v56.9.1 without restoring a direct-Agent
Attach bypass. The installed 1.8.9 and 1.20.1 MappingDictionary data, adapters
and capability ceilings are unchanged.

## Problem and final behavior

Three recorded production failures built a 45913-byte request against the
unchanged 32768-byte request-file limit. Each identity catalog contained eleven
historical cache packs plus two installed packs. Expanding their dictionaries
into repeated `liteClasses`, `classHints` and warmup sets made environment
detection depend on historical mapping volume, before any Snapshot was captured.
See [source audit](../../../docs/context/V56_9_2_SNAPSHOT_REVIEW.md).

The normal path remains Runtime Detection -> Mapping Validation -> transaction
Verified Runtime Context -> VersionAdapter -> normal Agent/Renderer startup.

| Tier | Request scope and authority |
| --- | --- |
| Detection | Four audited environment anchors, six fixed family class hints, five fixed launch hints, independent version/brand evidence and defining loader. No API members, hierarchy, warmup, bytecode or constant pools. It cannot authorize Ready. |
| Vanilla Validation | One installed dictionary for the exact detected Minecraft version; required API class/member metadata and hierarchy, pinned to the detected loader. Two independent captures/validations. No code/pool capture, Automatic Resolver or normal automatic Mapping window. A first or final real mismatch diagnoses and fails. |
| Modded Validation | Cache -> installed dedicated authored dictionary -> explicit Vanilla base / same-version compatible dictionary. One deduplicated dictionary per capture. Fresh compact installed-code proof is retained for Cache and modded authored validation; code/pool bytes are hashed inside the JVM rather than serialized. |
| Structural | Only after existing mapping candidates actually fail in a verified supported modified-client environment. Broad lite inventory, selected detail and Automatic Resolver retain confidence, required-binding and independent final-check gates. |

`Cache::knownPacks()` is no longer called to generate independent detection.
The identify catalog contains only installed supported packs; the Probe request
planner never reads `--identity-packs`. Class names, anchors and anchor/class
warmup groups are deduplicated. Cache lookup/reference discovery deduplicates
immutable revisions and restricts references to the exact detected version;
dedicated family priority is preserved. No arbitrary history truncation was added.
Reading/ranking a Cache after detection can still scale with its entries; the
bounded independent request planner and Probe capture scope no longer do.

`CAPTURE_REQUEST` reports actual UTF-8 request bytes, unchanged configured limit,
per-section sizes, tier, deduplicated scope counts and transport before size
validation. `SNAPSHOT_STATS` reports emitted classes/members, warmup attempts,
JVMTI/code/pool totals and Probe duration. A deterministic request-size overflow
now fails once with observed/configured sizes instead of retrying unchanged input.

The standalone full-inspect second capture retains fixed environment anchors
when its selected-detail request no longer contains a pack. This does not expand
the selected API detail scope. The exact Forge brand alias `fml,forge` is
accepted; unknown extra tokens remain unsupported. The alias is backed by
[Forge 1.8.9 source](https://github.com/MinecraftForge/MinecraftForge/blob/1.8.9/src/main/java/net/minecraftforge/fml/common/FMLCommonHandler.java#L474).

## Files and abstractions

- CaptureClient.h/.cpp: pure `planCaptureRequest` and `captureRequestStats`, four
  explicit capture tiers and pinned loader arguments. Analyzer CLI exposes
  `inspect --plan-capture` for offline reproducible request diagnostics.
- MappingProbe.cpp: tier-aware collection, pinned warmup/capture, counters and
  canonical Forge brand handling. Final artifact is **MappingProbe-v16.dll**;
  a new resident filename avoids reusing an older loaded Probe implementation.
- MappingService.h/.cpp/MappingServiceDynamic.cpp: bounded detection, scoped
  authored candidate queue, two independent validations, delayed structural
  fallback and terminal deterministic-overflow handling.
- Resolver.cpp: loader-scoped family evidence, modded authored compact proof,
  exact version/family boundaries and separate metadata-proof domain.
- MappingCache.h/.cpp: revision deduplication and exact-version references.
- BuildInfo.h / CMakeLists: v56.9.2, Probe-v16 and CacheStressTests target.
- Tests: Cache/request stress, runtime boundaries, service/transaction/fallback
  and modern production-Probe regressions. Context navigation/dependencies updated.

No business module was copied, no new wrapper inheritance was introduced, and
no Mapping confidence, descriptor, reference ownership or Ready gate was relaxed.
Metadata-only authored success is intentionally not promoted as a structural
Cache reference. Existing fully verified structural Cache proofs remain usable
only after fresh live verification.

## Measured stages

Request sizes below include real output paths/request IDs/loader receipts.
`requested / captured` distinguishes deduplicated API names from emitted classes,
which can include required hierarchy classes. Probe time excludes helper startup,
transport and Analyzer normalization. None is a Renderer readiness time.

| Evidence / tier | Captures | Bytes each | Requested / captured classes | Fields / methods | Warmup | Probe time each | Resolver / window trigger |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Real Minecraft 1.8.9 Vanilla, normal service Detection | 1 | 1262 | 4 / 2 | 0 / 0 | 0 | 47 ms | 0 / 0 |
| Same real process, Vanilla Validation | 2 | 1500 | 18 / 27 | 472 / 1275 | 18 | 156, 156 ms | 0 / 0 |
| Final Probe-v16, synthetic 1.20.1 on Java 17, Detection | 2 | 1288 | 4 / 1 | 0 / 0 | 0 | 31, 31 ms | 0 / 0 |
| Same synthetic modern service, Vanilla Validation | 4 | 1456 | 12 / 12 | 14 / 36 | 12 | 31, 32, 32, 31 ms | 0 / 0 |
| Real/synthetic normal Vanilla, Modded Validation | 0 | Not requested | 0 | 0 | 0 | Not executed | 0 / 0 |
| Real/synthetic normal Vanilla, Structural | 0 | Not requested | 0 | 0 | 0 | Not executed | 0 / 0 |

All collected Vanilla/Detection rows have zero serialized and JVMTI-returned
bytecode/constant-pool bytes. The production modern service reached
MappingVerified twice in **3230 / 3528 ms**. It does not execute Agent or Renderer.
Final machine-readable metrics: `build-main-release/tier-metrics-modern.json`,
also included in the delivery package.

The real normal-service capture/validation stages, including tool/transport work,
were: Detection 1536 ms, identify/cache lookup 449 ms, first Vanilla capture
1144 ms, first validation 1355 ms, second capture 1209 ms, final validation
1458 ms. Independent standalone real detection additionally measured 1208 B,
4 requested / 2 captured, zero members/warmup/code/pool, Probe 62 ms and helper
1107 ms. These are separate requests, not a claim that request sizes are identical
at every path length.

### Real Agent / Renderer outcome

The real process was PID 30512, Java 21, independently verified Minecraft
1.8.9 Vanilla with its actual AppClassLoader. The normal path completed
MappingVerified, normal Agent load (670 ms), authenticated Agent handshake
(1270 ms) and JNI binding readiness (16 ms). The OpenGL presentation hook was
installed but received no usable render frame/context during the ten-minute
acceptance window. The harness then detached cleanly and restored hooks.

**Agent/JNI Ready: observed. Renderer Ready / Active: not established.**
`LIVE_RESULT success=0` is retained as a partial/incomplete game acceptance result,
not converted into a passing end-to-end test. Resolver calls and automatic
Mapping-window triggers were both zero; physical desktop-window visibility was
not instrumented. Local evidence: `.research/tier-real-live-smoke.log` and the
isolated `build-main-release/tier-real-vanilla-cache/runs` transaction.

This real attempt used the initial v56.9.2 Probe-v15 candidate. Final Probe-v16
adds the canonical Forge alias and retains the measured Vanilla tier behavior;
final v16 regression uses owned Java 8/17 JVMs. No final-v16 real Renderer pass
is claimed.

## History stress and unexecuted tier budgets

The offline pure-planner test generates 1200 unique historical dictionaries over
three versions and four families, including unreadable/malformed catalog paths.
Detection is **1029 B, 4 requested anchors, zero warmup** with no catalog, 1200
packs, absent catalog or malformed catalog. Final measured CLI wall times were
103.892 / 105.993 / 102.547 / 98.205 ms; process startup is included, no JVM is
attached. These are planning estimates, not actual game transport timings.

| Offline plan | Bytes | Requested classes | Warmup classes | CLI plan time | Runtime / readiness |
| --- | --- | --- | --- | --- | --- |
| Selected cached modded dictionary | 2393 | 18 | 18 | 72.789 ms | No JVM/Ready |
| Entire legacy authored pack, Modded Validation diagnostic plan | 2620 | 36 | 36 | 68.627 ms | Not the production single-dictionary queue |
| Entire legacy authored pack, Vanilla diagnostic plan | 2621 | 36 | 36 | 99.997 ms | Not the production single-dictionary queue |
| Modern authored pack, Vanilla diagnostic plan | 1062 | 12 | 12 | 66.778 ms | Not actual path-bearing transport size |
| Structural inventory plan | 3326 | Inventory, not a fixed class budget | 55 | 56.895 ms | No Structural JVM capture executed in this stress test |

The planner test passes **118 assertions**, including exact UTF-8 accounting,
deduplication, candidate scope and rejection of a genuinely oversized but
schema-valid 80-dictionary request at the unchanged 32768-byte limit.
CacheStressTests creates **1200 objects / 1350 index entries** and verifies
**1129 unique known packs / 94 exact-family/version candidates**, corruption,
contract mismatch and reference isolation: **23 checks, zero failures**.
Its recorded **56167 ms** includes fixture construction and all Cache operations;
it is not a Detection benchmark.

## Executed regression and evidence classes

| Evidence class | Actual result in this iteration |
| --- | --- |
| Release build | Controller, Agent, Analyzer, Probe-v16, attach tools and relevant test targets built; complete default build and install passed |
| Mock transport | MappingService **34**, Dynamic **92**, Progress **33** checks, zero failures; includes overflow single-attempt, first/final Vanilla mismatch, missing version/Unknown, no normal popup and actual fallback once |
| Real algorithms, synthetic offline capture transport | Transaction/cache **32** checks, zero failures: cache hit, stale proof -> authored/compatible validation, no false structural promotion, descriptor refusal, cancellation, pause/resume, real QML progress-window close |
| Offline Cache/request stress | Cache **23**, request planner **118** checks passed |
| Offline Analyzer/boundaries/stream | **24 / 32 / 7** checks passed; full Resolver, VersionAdapters, TwoStage and Incremental scripts passed |
| Runtime detection | **107** assertions passed; **9** directly replay three unchanged historical real Snapshots with SHA256 preservation; remaining assertions are synthetic |
| Recorded Lunar/Forge metadata | Existing 70 hierarchy classes / 548 fields / 3124 methods authored SRG positive replay and descriptor/static/loader/missing/duplicate negatives passed; no new live game Attach claim |
| Owned synthetic Java 8u51 and Java 17.0.8, Probe-v16 | RuntimeEvidence **68 each**, including canonical Forge brand, unknown extra-token refusal, parent-resource delegation, colliding loaders, version conflicts and malformed resources; checked JNI enabled |
| Owned synthetic modern game on Java 17, Probe-v16 | ModernPipeline **44** and production MappingService **18** checks passed; no Agent/Renderer in these two tests |
| Actual Qt/QML controller UI | Normal D3D11 smoke **40 phases passed**, including silent success/fallback visibility and close/reopen behavior; package startup is separately checked |
| Fresh real Minecraft | Partial 1.8.9 Vanilla result above; Renderer acceptance incomplete and clean detach observed |

Earlier v56.9.1 JNI/WGL/renderer smoke remains historical evidence for unchanged
Agent/rendering code and is not relabeled as a new full game test in this release.
Tests are explicit executables/scripts, not a claim that an empty `ctest` passed.

## Client boundaries and remaining acceptance

| Client | Current boundary | New real game evidence |
| --- | --- | --- |
| 1.8.9 Vanilla | Exact installed Notch dictionary, metadata twice, no resolver/popup on success | Mapping, Agent and JNI observed; Renderer/Active incomplete |
| 1.8.9 Forge | Independent exact version + Forge brand/loader evidence, dedicated SRG first; Cache/authored/compatible/structural order preserved | Offline + owned synthetic evidence; fresh real Forge not run |
| 1.8.9 Lunar | Same-family dedicated mapping priority, fresh Cache/installed proof and silent authored recovery | Recorded metadata + synthetic transaction tests; fresh real Lunar not run |
| 1.20.1 Vanilla | Exact audited modern dictionary and existing readonly adapter/capabilities | Final production preflight on owned synthetic Java17 only; real game not run |
| 1.20.1 Forge | Correctly recognized as 1.20.1 Forge, explicitly Unsupported; no legacy fallback, structural guessing or Agent load | Synthetic boundary tests only |
| Unknown or unsupported version | Terminal diagnosis, no guessed members, provisional Ready or unbounded retry | Synthetic negative cases |

Finish real acceptance with a visible actively drawing game window, enter a world,
Attach, confirm both BINDING_READY and RENDERER_READY/Active, then Detach/Retry.
Record request/Probe stats per transaction. Repeat 1.8.9 Forge/Lunar with isolated
valid Cache and stale-proof Cache, plus real 1.20.1 Vanilla. Modern Forge needs
an audited family-specific dictionary and API/runtime proof before enabling it.
1.20.2–1.20.6 still require exact dictionaries, API audits, allowlist changes and
independent fixtures; they are not aliases of 1.20.1. No 1.12.2 work was added.
