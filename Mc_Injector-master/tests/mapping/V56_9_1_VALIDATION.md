# v56.9.1 — Vanilla fast path and conditional Mapping recovery

PR #10 branch: `codex/v56.9-minecraft-1.20.1`; baseline e24b300.
Remote main checked during this task: 764d4b8 (v56.6). PR remains Draft.
The v56.7/v56.8 architecture and authored recovery already on this branch remain.

## Actual regressions and changes

Historical audit: [V56_9_FAST_PATH_AUDIT](../../../docs/context/V56_9_FAST_PATH_AUDIT.md).
The original 1.8.9 Vanilla run validated Lunar Notch because Unknown admitted
all providers; its final Unknown receipt was then rejected. Modern Lave was a
DataFixer name collision, incorrectly counted as a second Minecraft version.
Modern Forge inherited 1.8.9 from a selected cached pack and entered pointless
watch/retry. Opening the progress window at every Attach hid these distinctions.

| Before | v56.9.1 |
| --- | --- |
| Preferred Cache/class-name hits precede actual version authority | Minimal scoped Probe-v14 runtime evidence first |
| Unknown may select high-priority Lunar | Unknown stays Unknown; Vanilla symbols are only a diagnostic candidate |
| Every Attach opens Mapping UI | Only explicit automatic fallback or diagnosis opens it |
| Vanilla enters detail/Cache/structural machinery | Installed exact authored pack, metadata validation twice, Verified Context |
| Cache failure can contaminate identity/baseline | Actual environment is immutable; Cache failure revalidates installed authored baseline |
| Cached pack version appears to be runtime version | Independent exact version controls Adapter/contract; unsupported versions fail |

MappingDictionary data has not changed in this fix. Vanilla Notch already existed
and remains reused. Modern 52-key audited dictionary, readonly Adapter and
capabilities from v56.9 remain unchanged; no duplicate feature modules were added.
The new abstraction is one receipt on the existing AttachTransaction, plus
explicit environment/authored-proof Analyzer operations. Cache code proof domain,
confidence 0.98, provisional export gate and JNI ownership rules remain enforced.

Runtime version evidence uses audited stable APIs: 1.8.9 RealmsSharedConstants
VERSION_STRING and modern loader resource version.json id. Brand comes from
ClientBrandRetriever. Resource input is bounded at 64 KiB; reads are repeated;
conflicting/malformed/oversized evidence fails closed. Evidence classes must be
defined by the game loader. A different loader that delegates version.json to its
parent cannot acquire version authority without its own independent game witness.
No raw property values, launcher commands or resource contents are persisted.

## Executed verification

Windows x64, MinGW 13.1, Qt 6.10.1; Release controller, Agent, Analyzer, Probe-v14
and relevant executable test targets built. Tests are explicit executables/scripts;
an empty ctest run is not counted.

| Evidence class | Executed checks |
| --- | --- |
| Mock | MappingService 27; dynamic service 92; MappingProgress 33; GameApi 43 |
| Synthetic offline metadata / process-bound capture | Production Analyzer Transaction/cache 31, including actual QML window close, Cancel/Retry isolation, compact hit, stale proof -> authored success without popup, failed authored -> automatic popup once, old proof upgrade |
| Offline regression | Mapping pack parity 67; Provider tests; Analyzer 24; boundary/cache 32; Snapshot stream 7; source policy 201; full 257-key Resolver, VersionAdapters, TwoStage, Incremental scripts |
| Unchanged real Minecraft Snapshot replay | RuntimeDetection 70 checks total, of which 9 directly replay three unchanged historical runs with SHA256 preservation; existing Lunar/Forge replay validates 70 hierarchy classes / 548 fields / 3124 methods |
| Owned synthetic Java 8 and Java 17 JVMs | RuntimeEvidence 56 each, including parent resource delegation/colliding loaders, positive Vanilla, Forge brand, Unknown, unsupported version and malformed/conflicting evidence |
| Owned synthetic modern game on Java 17 | Production Probe/Analyzer 41; production MappingService 9; production JNI bindings 32; metadata-only capture confirms zero bytecode/constant-pool bytes |
| Real JVM hook / WGL infrastructure, synthetic objects | Logical pipeline 156 each on Java 8 and Java 17; real WGL core renderer 13 and renderer regression 5599; full Agent unified/split-thread core-context smoke receives BINDING_READY + RENDERER_READY and DETACH_COMPLETE |
| Controller responsiveness | Five-second async scan retained 252 UI heartbeats; bounded IPC, split messages and teardown passed |
| Actual Qt/QML controller UI | D3D11 smoke passed all40 phases, including silent Cache success, fallback opens once, close preserves state, duplicate fallback does not reopen, manual reopen and light/dark screenshots |

Javac --release8 emits expected obsolete-target warnings. Runtime fixture JVMs
use -Xcheck:jni; no checked JNI warnings/errors were observed. None of these tests
are presented as a running Minecraft client test.

The first UI run exposed a stale v56.6 test assertion, which was corrected to
BuildInfo.internalVersion before the40-phase passing run. Software-mode Quick3D
is not supported by Qt; the final UI evidence uses the normal D3D11 backend.

## Client matrix and limits

| Client | Recognition / selected Mapping | Automatic / popup | Agent / Renderer evidence |
| --- | --- | --- | --- |
| 1.8.9 Vanilla | Independent Java8 evidence + exact Vanilla Notch metadata; Unknown never grants Ready | Success: no / no; mismatch: no / diagnosis | Fast service path covered by Mock + offline exact metadata; real game Ready not tested |
| 1.8.9 Forge | Synthetic independent family/version; dedicated SRG; existing recorded Forge metadata regression | Existing mapping/Cache success silent; only failure starts structural fallback | No new running Forge Agent/Renderer test |
| 1.8.9 Lunar | Dedicated MCP/Notch/SRG selection remains; process-bound Cache/authored/recovery tests and recorded SRG metadata | Cache/authored recovery no popup; real resolver failure path opens once | No new running Lunar Agent/Renderer test |
| 1.20.1 Vanilla | Independent exact1.20.1/Vanilla + modern Notch required metadata and final recheck | no / no | Production service + JNI + full Agent/WGL Ready on owned synthetic game JVM; real game pending |
| 1.20.1 Forge | Independent exact1.20.1/Forge; no trusted dedicated Mapping | no structural retry / Unsupported diagnosis | Refuses Agent loading; no false1.8.9 Ready |
| Unknown / unsupported version | Evidence absence or unsupported exact version is terminal | No guessing or endless watch | No Ready |

The three historical real snapshots did not contain the new independent evidence.
Their replay deliberately does not invent it or authorize injection. A fresh
Probe-v14 capture in each running client is required for the final game acceptance.
No running Minecraft JVM was available during this execution.
Standalone full validation also separates valid member metadata from
injectionReady: absent independent runtime version evidence cannot produce Ready
or a fully validated structural export. Compact Cache validation requires it too.

## Next real-client acceptance

Launch the extracted Arcveil build and a supported client, enter a world, Attach,
then Detach and retry. For Vanilla confirm no automatic Mapping window, two
authored receipts, matching PID/start/loader, actual version/family/Notch in
Verified Runtime Context, and both BINDING_READY/RENDERER_READY. Repeat 1.8.9
Forge/Lunar with a valid Cache and after invalidating only the isolated test Cache;
authored recovery must remain silent. A genuine authored mismatch must keep the
Attach pending in modified clients without accepting provisional data. Modern
Forge must report Unsupported1.20.1 and stop without legacy watch or Agent load.
Keep per-transaction logs; closing Mapping UI must not cancel verification.

Modern gameplay interactions, network packets, inventory/bed scanning, world
projection and ESP rendering are still outside the readonly1.20.1 capabilities.
Forge needs audited exact mapping and API evidence. 1.20.2–1.20.6 need separate
dictionary/API audits, exact allowlist entries and version-specific JVM fixtures;
they are not aliases of1.20.1. No1.12.2/1.20.4/1.21 support was added.
