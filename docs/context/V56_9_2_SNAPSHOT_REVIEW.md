# v56.9.2 Snapshot capture review

Review date: 2026-10-18 (Asia/Tokyo). This note separates the recorded real-client
failure from the owned synthetic JVM measurements produced during this change.
The implementation keeps Snapshot validation, Verified Runtime Context and normal
Agent/Renderer readiness; no historical direct-Attach bypass is proposed.

## Recorded production failure

The local `mapping-cache-v1/runs` records contain three unchanged v56.9.1 failures:

| Run ID | Local time | PID | Catalog paths | Request bytes / unchanged limit | Recorded capture attempts |
| --- | --- | --- | --- | --- | --- |
| `9e0a57f6-0c7c-4a36-9065-47cf283e8424` | 2026-10-15 20:12 | 36920 | 13 | 45913 / 32768 | 1224, 267, 255 ms |
| `9cd2b5c3-d7c5-4f77-8960-cf741916f347` | 2026-10-15 20:12 | 36920 | 13 | 45913 / 32768 | 350, 411, 360 ms |
| `4bca5d29-81de-4d29-abdf-d231547a9d52` | 2026-10-18 03:43 | 30512 | 13 | 45913 / 32768 | 1369, 358, 368 ms |

Each catalog contains eleven historical cache-pack paths and the two installed
authored packs. Each run ends with
`snapshot size limit: layer=file limit=probe-request-json observed=45913 configured=32768`.
There are no Snapshot files, CAPTURE_PATH results, AUTO_RESOLVE events or
mapping-fallback events in these runs. The size rejection occurs while building
the Analyzer request, before Probe transport. Thus no game classes were captured
and MappingVerified/Agent Ready/Renderer Ready were not reached. These records
cannot independently establish the Minecraft version or client family. The lack
of a mapping-fallback event proves no automatic window trigger in these logs;
window visibility itself was not recorded.

The durations above are complete failed service capture attempts, including tool
startup, rather than successful Probe capture times. Retrying the identical
oversized deterministic request cannot resolve the failure. No real-client
success is inferred from a later synthetic test.

## Why history changed an environment request

The v56.9.1 `Cache::knownPacks()` traversed every verified index entry and returned
every valid historical pack for the contract. `writeIdentityCatalog()` added both
installed packs and deduplicated paths. Neither operation bounded the number of
historical symbol sets used for detection. `CaptureClient` then added its active
`--pack` again and expanded every catalog dictionary into `liteClasses` and
`classHints`. Those arrays were not deduplicated; only `runtimeAnchors` were.
Consequently a request that only needed independent environment evidence included
all historical API names and repeated family hints. This is a request-generation
problem, not evidence that the 32 KiB request-file protocol is insufficient.

The tier boundary should keep environment detection independent of the cache:

| Tier | Required evidence | Excluded work |
| --- | --- | --- |
| Detection | Audited version/brand receipts and exact defining-loader anchors | API members, hierarchy, warmup, cache symbol expansion, bytecode and constant pools |
| Vanilla Validation | Exact selected authored dictionary's required API metadata, hierarchy and independent environment receipt | Structural matching, installed-bytecode cache proof, unrelated versions/families |
| Modded Validation | Compatible current cache proof or selected authored/compatible mapping, with exact current loader and environment | Unrelated cache history and broad structural inventory |
| Structural | Selected detail and required matching evidence after existing mappings fail | Success-path Vanilla capture and unsupported modern Forge guessing |

## Detection safety boundary

`Resolver::identifyRuntime()` already distinguishes authoritative runtime evidence
from diagnostic API-member candidates. A verified exact version may select its
authored API even when the Detection Snapshot intentionally contains zero fields
and methods. This does not authorize those members: the later required-member
validation and fresh independent final validation remain mandatory.

Detection must retain an anchor record in the same defining-loader group as its
runtime receipt. Removing all class records would discard the evidence anchor.
Multiple independent loader/version scopes must remain ambiguous; a shared class
name must not select one arbitrarily. A dictionary's declared version or high
provider priority cannot supply missing runtime evidence. Positive Vanilla brand
is still required; unknown brand and unsupported versions stay unsupported.

Family class hints are scoped observations, not permission to relabel a selected
mapping. The global JVM launch hints are weaker independent context, while the
exact anchor/loader receipt must survive every validation stage. Parent resource
delegation alone cannot establish a child's game version. In particular the
modern `Lave;` collision must not reopen the old cross-version ambiguity.

## Verification artifacts and limits

`ModernMappingServiceTests` owns a Java 17 fixture process and runs the production
MappingService, Analyzer and Probe twice. Its `tier-metrics-modern.json` stores
each request's byte count, requested class count, Probe class/member counts,
bytecode/constant-pool byte totals, Probe durations and service stage durations.
It checks one Detection and two independent Vanilla Validation captures per
transaction, the unchanged 32768-byte limit, no history catalog expansion, no
structural resolver or automatic window trigger, and zero code/pool capture.

Metric interpretation is explicit: `CAPTURE_REQUEST.requestBytes` is the actual
serialized request, including its output path, request UUID and any current
loader receipt, immediately before the unchanged file-size check. It is not a
path-free planning estimate. Its `sections` values measure individual serialized
JSON values; their sum excludes object keys/punctuation and is not the total.
`requestedClassCount` counts deduplicated requested API names;
`SNAPSHOT_STATS.capturedClassCount` counts emitted classes and may additionally
include required hierarchy metadata. `loadedClassCount` is the initial JVM
inventory, not the capture scope. Probe `durationMs` measures in-process request
handling/capture before final serialization; service `STAGE_PERFORMANCE` also
includes tool startup, transport, file processing and normalization. Neither
duration is an Agent/Renderer readiness time.

This service test proves MappingVerified only. It does not load the rendering
Agent, and explicitly records Agent Ready/Renderer Ready as not exercised.
Owned full-Agent JVM smoke, unchanged recorded Snapshot replay, Mock transport
tests and fresh real Minecraft acceptance must be reported as separate evidence.
The executed measurements below are scoped to their actual evidence class and
do not imply a successful full Agent attachment inside Minecraft.

## Executed tier measurements

The enhanced RuntimeDetection script passed **107 checks** against the final production
Analyzer, including the final canonical Forge-brand regression. Nine assertions
replayed three unchanged historical Minecraft metadata
Snapshots and verified input SHA256 preservation; the remainder used synthetic
offline metadata. The added cases include foreign-loader family markers, same
loader modified-client evidence, empty-member Detection, Main-only detection,
namespace boundaries and refusal to authorize missing required API members.
The final additions accept only the audited legacy Forge `fml,forge` brand and
reject an unknown extra brand token, even when independent Forge hints exist.
The canonical alias comes from the default token list in
[Forge 1.8.9 FMLCommonHandler.getModName](https://github.com/MinecraftForge/MinecraftForge/blob/1.8.9/src/main/java/net/minecraftforge/fml/common/FMLCommonHandler.java#L474);
arbitrary comma-separated strings or substring matches are not accepted.

The production-service test passed **18 checks**, owning a Java 17 process with
synthetic Minecraft objects. Two complete preflights reached MappingVerified in
3230/3528 ms on final Probe-v16. Actual transport request and Probe measurements were:

| Evidence / tier | Captures | Actual request bytes each | Requested / captured classes each | Fields / methods each | Probe duration each | Automatic / window trigger | Agent / Renderer Ready |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Synthetic modern / Detection | 2 | 1288 | 4 / 1 | 0 / 0 | 31, 31 ms | 0 / 0 | Not exercised |
| Synthetic modern / Vanilla Validation | 4 | 1456 | 12 / 12 | 14 / 36 | 31, 32, 32, 31 ms | 0 / 0 | Not exercised |
| Fresh real 1.8.9 Vanilla / Detection | 1 | 1208 | 4 / 2 | 0 / 0 | 62 ms | Standalone inspection; no resolver or UI service | Not exercised |

All rows above have zero serialized bytecode/constant-pool bytes and zero
JVMTI-returned bytecode/constant-pool bytes. The live Detection helper duration was
1107 ms; it is separate from the 62 ms in-process Probe duration. Request paths
and loader receipts explain small request-size differences between standalone
inspection and the full service. No request-size ceiling was raised.

The fresh real run inspected PID 30512 using Probe-v15 and independently confirmed
`1.8.9`, `Vanilla` and the current AppClassLoader instance. It used
RealmsSharedConstants/brand evidence rather than treating `Lave;` as version proof.
It produced `build-main-release/tier-real-detection.jsonl` and
`tier-real-identity.json`; its local event log is `.research/tier-real-detection.log`.
This is a real in-process **Detection-only** test. It does not validate all
required Mapping members or claim Agent/Renderer Ready. The subsequent normal
service/Agent attempt is recorded separately below.

## Fresh real normal-service attempt — partial readiness and clean detach

The same real Minecraft PID 30512 runs Java 21 and independently reports Minecraft
1.8.9 Vanilla. The normal MappingService path completed Detection and both exact
Vanilla Notch validations, then published MappingVerified and loaded the normal
Agent. `.research/tier-real-live-smoke.log` records these actual capture stages:

| Tier | Captures | Actual request bytes each | Requested / captured classes each | Fields / methods each | Warmup classes | Probe duration each |
| --- | --- | --- | --- | --- | --- | --- |
| Detection | 1 | 1262 | 4 / 2 | 0 / 0 | 0 | 47 ms |
| Vanilla Validation | 2 | 1500 | 18 / 27 | 472 / 1275 | 18 per capture | 156, 156 ms |

All three captures returned and transported zero bytecode/constant-pool bytes.
The extra emitted Vanilla classes are required hierarchy metadata, not another
version's Mapping. No Automatic Resolver or automatic Mapping-window trigger
appears in the service log. This event evidence is separate from testing actual
desktop window visibility.

The log reports JNI binding readiness in **16 ms** and authenticated Agent
handshake readiness in **1270 ms**. The installed OpenGL presentation hook waited
ten minutes for a render context/first frame, then the attempt timed out and
detached cleanly. **Renderer Ready and Active were not established**. This is a
partial real-client result, not a successful complete Attach.

The real attempt used candidate **Probe-v15**. The final package uses
**Probe-v16**, which additionally accepts the explicitly audited Forge 1.8.9
`fml,forge` brand and prevents reuse of the earlier resident Probe. The Vanilla
capture logic is unchanged, but these real measurements must still be identified
as candidate-Probe measurements; no final-package real Renderer success is claimed.

Fresh real 1.20.1, Forge/Lunar, Modded Validation and Structural acceptance remain
separate work; their absence cannot be filled with synthetic measurements or
this real Vanilla 1.8.9 result.
