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
- validate currently reports schema-only, injectionReady=false. Runtime member
  validation and automatic resolution belong to v55.3.
- diff: exact symbol changes and pack digest/metadata changes.

Snapshots are bounded at 48 MiB capture / 64 MiB input; packs at 2 MiB. Probe
output is published by rename; tool output uses QSaveFile. Tool stdout is JSONL
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
