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
