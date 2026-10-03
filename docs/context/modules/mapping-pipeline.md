# mapping-pipeline

Start here: P/mapping/analyzer/Analyzer.h, P/agent/bindings/MappingPack.h.
P/ means Mc_Injector-master. This module owns pack interchange, standalone
analysis, JSONL events and pre-injection mapping validation; Gameplay consumes
only its existing BindingCache.

## v55.2 entry points

- MappingAnalyzer inspect: offline snapshot inspection or --pid / --java live
  attach through the existing AttachHelper and a separate MappingProbe.dll.
- MappingProbe::Agent_OnAttach: dedicated JVMTI environment, no render/game hooks,
  no retransformation request, no registry writes. Gets installed bytecodes and
  constant pools, fields, methods, superclasses, interfaces and defining loaders.
- Each class is read twice; changed data aborts. This is not a JVM-wide atomic
  snapshot. Before injection the later service must recapture and compare.
- inspectSnapshot: loader-group canonicalization, SHA-256 content fingerprint;
  PID/process creation identity/request UUID tracked separately. Ambiguous loader
  groups fail closed. No selection solely by a class name across loaders.
- validate without --snapshot reports schema-only, injectionReady=false. Runtime
  member validation and automatic resolution are described under v55.3 below.
- diff: exact symbol changes and pack digest/metadata changes.

Capture transport now uses chunked JSONL (v55.6) without an aggregate file-size
ceiling: 64 KiB chunks, 512 KiB frames, 48 MiB per object, 16 MiB per JVMTI
binary buffer. Legacy whole-JSON offline input remains bounded at 64 MiB; packs
at 2 MiB. Probe output is published by rename; JSON tool output uses QSaveFile. Tool stdout is JSONL
with eventVersion=1, event and event-specific fields; nonzero exit indicates
failure. Agent uses no Qt; Analyzer links Qt Core.

Tests: MappingAnalyzerTests, Run-MappingCapture.ps1 (private transformed JVM).
Lunar production compatibility requires a real Lunar run; this fixture proves
only installed-bytecode capture through standard JVMTI. Lack of capabilities or
Attach support fails closed and is not hidden with disk/JAR fallback.

JVMTI reference: https://docs.oracle.com/en/java/javase/17/docs/specs/jvmti.html#GetConstantPool

## v55.3 resolver / live validation

`validate --pack FILE --snapshot FILE` checks required core bindings using
contracts-v1.json: defining-loader scope, hierarchy, exact descriptors and
static/instance modifiers. Authored optional mappings retain the Agent's
existing capability behavior. `resolve` additionally requires *every nonempty
automatically mapped symbol* to pass final live validation before export.

`resolve --pack FILE --reference VERIFIED_SNAPSHOT --snapshot TARGET --out
CANDIDATE [--write-pack OUTPUT]` compares class hierarchy, descriptor/field/method
structure, normalized installed bytecodes and incoming class/member references.
Names have weight zero. Every symbol has evidence, confidence, threshold and
margin. Confidence 0.99 is a conservative rule-based acceptance tier, not a
calibrated statistical probability; threshold is 0.98. A unique exact eligible
match has margin 1; ambiguous/unresolved matches have confidence/margin zero.
No bytecode-less class is accepted without cross-reference evidence. Unsupported
dynamic bootstrap data fails closed rather than being guessed.

A verified reference snapshot is required for automatic migration. Without one,
the tool writes a diagnostic candidate and exits 4; it never writes --write-pack.
The first known runtime can bootstrap a reference after an authored pack passes
live validation. Structural fixtures prove rename recovery and rejection paths,
not arbitrary Lunar-version coverage. Template contracts have 257 logical keys;
three currently unconsumed values remain explicit authored/reserved data.

Tests: Test-Resolver.py exercises the whole schema with renamed classes/members,
ambiguity, absent reference, and wrong optional object descriptor. Bytecode
normalization resolves constant-pool indices, excludes member names/debug data,
normalizes branch destinations, and retains typed reference/call graph edges.

## v55.4 Injector boundary and cache

`P/src/MappingService.h` is the QObject API: prepare / cancel / rollback,
busy/status/fingerprint/logPath, ready(pack,digest), failed(reason), eventReceived.
OverlayManager remains Validating until ready; only then does it start IPC and
the existing Attach / NativeLoader main-Agent path. Cancellation invalidates the
operation generation. Child process output is JSONL with bounded memory and a
32-event / 5 ms drain budget; no child-process waits run on the Qt GUI thread.

Preflight: installed snapshot → fingerprint cache → authored pack live validation
→ on failure Analyzer resolve using last verified snapshot → independent candidate
validation → second snapshot / PID creation-time check → atomic cache promotion
→ normal injection. No reference or ambiguous evidence means no injection.
Known authored dependencies may be loaded without initialization through an
already-loaded anchor's defining loader, matching the old resolver's loadClass
behavior. Launch hints are matched in probe memory; only family/confidence is
returned. Raw JVM properties, command lines and paths are never persisted.

AgentOptions mapping (hex UTF-8 absolute path) + mappingHash (SHA-256) are a startup
boundary only. MappingPack hashes and parses the same bounded byte buffer. It
cannot silently fall back when the provided pack/hash is bad. GameBindings
constructs its registry with these options before resolve/freeze. Existing
registration methods remain intact; there is no live-registry update IPC.

Cache lives in AppLocalDataLocation/mapping-cache-v1. Immutable objects hold pack,
snapshot and digests; index.json atomically points at candidate/verified/previous.
QLockFile serializes publication. Failed/interrupted candidates never replace a
verified entry. Rollback swaps verified/previous for the last fingerprint and
only affects the next injection, which rechecks the runtime. Cache entries are
bound to the contract digest and analyzer revision. Runs retain JSONL/snapshots
for diagnosis. Mapping Console is a later consumer of these events.

The probe supports standard Attach (including Java 8 tools.jar) and the existing
NativeLoader fallback. Its fallback worker copies options before returning and
attaches its own daemon JNI thread; it never starts the main Agent or hooks.
No read protocol can freeze a third-party JVM across separate processes: the
second snapshot detects observed change; main Agent still performs normal JNI
resolution and immutable-cache publication afterward.

## Stage 5 — developer console and distribution
MappingEventModel retains 2,000 structured JSONL event rows; MappingService exposes events/progress/status/fingerprint/logPath. MappingConsole is opt-in from Settings or Ctrl+Shift+M. Rollback applies next injection; cancellation routes through OverlayManager. Standalone CLI usage and limitations are in `P/mapping/README.md`, installed to docs/mapping. Internal version v55.9.

## v55.6 capture diagnostics gate
Start at `P/mapping/analyzer/CaptureClient.h` and `P/mapping/SnapshotStream.h`.
`inspect --pid PID --lite` (alias inspect-lite) captures metadata only, automatically
selects the target runtime unless --java is supplied, and writes a streamed class
index with a separate metadata fingerprint domain. It does not call resolve.
CAPTURE_PATH exposes both helper outcomes; SNAPSHOT_STATS exposes actual sizes and
precisely scoped limits. No registry/Agent startup boundary changes in this stage.
The real Lunar lite gate passed on 2026-09-28; see the v55.7 stage below.
Tests: SnapshotStreamTests (>64 MiB aggregate, chunk Unicode/order/truncation/limits),
Test-CaptureDiagnostics.py, Run-MappingCapture.ps1 -Lite [-DisableAttach].

## v55.7 two-stage capture and Lunar SRG
`inspect --pid --pack` and both MappingService capture passes use lite →
selectDetailCandidates → inspect-detail. `select` and `inspect-detail` are also
standalone commands; select consumes a lite snapshot plus an optional verified
reference. The existing normalized-bytecode/call-graph acceptance algorithm is
unchanged. Metadata matches only expand the detail scope; they never grant mapping
confidence. Selected detail binds PID/creation time, loader instance and per-class
metadata digests. Scope metadata is part of its fingerprint.

ProbeProtocol.h defines tagged native startup I/O failures; CAPTURE_PATH includes
Win32 errors and the last probe phase. Requests travel through a bounded temporary
JSON file, keeping Attach arguments short. The worker owns a copy. Live fallback
works when jdk.attach is missing or Attach is disabled. Snapshot cache digests now
stream with no aggregate 64 MiB cap; analyzer proof revision is 4.

Default pack v2 preserves v1 and adds Lunar SRG using existing authored SRG symbols.
Default paths in Agent and controller move together; v1 remains installed. Real
Lunar 1.8.9 v2.22.42-2639 passed two-stage capture, required-member validation and
Agent ready/renderer active with clean detach. Test evidence and exact limits are
in P/tests/mapping/V55_7_VALIDATION.md. Tests add Test-TwoStage.py,
Run-MappingCapture.ps1 -Detail [-DisableAttach] and opt-in McOverlayLiveMappingSmoke.

The request-file protocol uses MappingProbe-v2.dll. Its versioned filename prevents
an older resident Probe DLL from being mistaken for the current protocol.


## v55.8 progress event consumption (historical)

Start at `P/src/MappingProgressController.h`, `MappingProgressModel.h`, then
`MappingServiceProgress.cpp` for event adaptation. QML receives three filter
models and reference/step properties. OverlayManager.mappingAttachRequested opens
the window; MappingService session-start resets late-cancelled presentation state
without reopening a hidden window. No controller action reaches Agent freeze or
MappingService.cancel. `stopMatching` stops presentation retry/progress scheduling;
mandatory preflight and final result delivery continue. No new matcher/capture
loop is introduced in this visualization-only stage.

Structured JSONL events: session-start, step(index/state/message), reference,
snapshot-update(fingerprint/classes), symbol-queued(symbol/logicalName/required),
symbol-started, symbol-progress, symbol-retry, symbol-failed, symbol-matched
(runtimeName/verified/confidence/evidence), complete(scope=mapping/source).
Analyzer subcommand complete is not whole-pipeline completion. Raw `symbol`
acceptance remains provisional; only the service's post-recheck receipt completes
rows. Existing dictionary validation now reports found aliases as runtimeMapping;
new cache proofs store optional symbol receipts, including automatic evidence.
Older verified cache proofs retain required-only display with explicit alias-set
provenance. Receipt read errors degrade the display, never the injection decision.

## v55.9 service-level dynamic generations

Start at `P/src/MappingService.h` / `MappingServiceDynamic.cpp`, then
`P/mapping/analyzer/Resolver.h`. Service public API adds matchingEnabled,
stopMatching(), resumeMatching(). Stop pauses future watch/retry only; cancel()
is whole-attach cancellation. Existing ready/failed and Agent freeze boundaries
remain unchanged. main.cpp wires progress-controller stop/resume request signals
to the service; closing the QML window only hides it.

prepare → initial lite/select/detail → exact cache or authored validation →
incremental resolve. An incomplete result persists accepted/unresolved state and
waits. QTimer schedules serialized lite capture with 2.5–30 s backoff; changed
relevant scopes debounce 750 ms. Watch lite does not hydrate authored classes.
Unchanged lite fingerprints skip detail; changed global fingerprints with unchanged
relevant class metadata/loader instances also skip detail. Selected detail then
revalidates retained proofs and resolves unresolved/affected symbols only.

Analyzer `select --allow-empty` supports an empty waiting scope and emits a
relevantFingerprint. `resolve --incremental [--state FILE]` emits stateVersion=1,
accepted bindings with bindingProof/confidence/evidence, unresolved and a provisional
draft pack. State binds source pack/contracts/reference, dictionary, PID and process
creation time. Proofs use normalized bytecode and references plus descriptor/access
metadata; enumeration order is immaterial. Incremental state cannot --write-pack.
Normal resolve's acceptance criteria and complete export remain unchanged.

Required completion → full independent live validation → final selected snapshot
and PID/creation/fingerprint check → verified upgrade/cache promotion/ready.
Final drift becomes a new generation and rechecks affected bindings, never a fatal
failure. ProbeProtocol::RuntimeChanged carries transient class/method/loader drift
as structured retryable failure and Analyzer exit 5; MappingProbe-v3.dll prevents
reuse of a resident v2 protocol. Non-drift capture failures retry three times before
capture is declared unavailable. No dynamic Agent registry replacement.

Events add snapshot-watch/unchanged/changed, retry-scheduled/started,
symbol-provisional-match/revalidated/invalidated and waiting-for-runtime-change.
Provisional symbol-matched rows migrate immediately but cannot authorize injection.
The model's isVerified role gates success; retained and verified upgrades do not
restart migration. Only service events retry/invalidate rows, not QML snapshot logic.

Tests: DynamicMappingServiceTests (100-symbol external subprocess through production
service), Test-Incremental.py (real Analyzer), MappingServiceTests,
MappingProgressTests, ControllerUiSmoke and existing capture/agent regressions.
Evidence and limitations: P/tests/mapping/V55_9_VALIDATION.md.
