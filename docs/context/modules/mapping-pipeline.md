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
