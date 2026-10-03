# v55.9 Dynamic MappingService validation — 2026-10-03

Scope: real pre-injection watch/retry, incremental Analyzer state, provisional UI
and final-generation verification. Existing matching thresholds/evidence and
Agent registry/freeze stay unchanged. No live-registry replacement is added.

## Verified

- DynamicMappingServiceTests: 69 checks, 0 failures. Uses the production QObject
  service with an external scripted Analyzer process and 100 required symbols.
  A accepts 70; unchanged B skips detail; an unrelated global change also skips
  detail. C starts only 30/revalidates 70 and completes 90; D starts 10/revalidates
  90 and completes 100. Only full validation and final recheck emit ready/cache
  promotion. Authored bootstrap selects the default target pack even when a
  different verified reference pack exists. Final drift E starts 1 affected/revalidates 99 without failed().
- Stop/resume scenario asserts no further lite/detail/resolve subprocess calls
  while stopped, busy/attach state retained, 70 provisional rows saved and no
  success. Resume completes; an already-completed row upgrades without migrating
  again. Whole-operation cancellation suppresses stale timers/ready.
- Full validation rejection of a nonempty automatic binding prevents promotion
  even with an injectionReady receipt; the service invalidates it, waits and
  retries the next generation. Typed recoverable capture drift returns to waiting;
  persistently unavailable capture fails once after bounded recovery.
- MappingServiceTests: 18 checks, 0 failures. Existing authored/cache injection
  gate, late cancellation, subprocess/event-stream handling and event-loop
  responsiveness pass with explicit lite/select/detail command ordering.
- MappingProgressTests: 24 checks, 0 failures. Provisional migration does not
  grant success; revalidation/verified upgrade preserves rows; invalidation
  prevents stale timers restoring a binding. Snapshot observation alone does not
  invent retries. Automatic open, stop and verified required completion pass.
- Real Analyzer Test-Incremental.py passes all 257 accepted bindings, identical
  and reordered-enumeration retention, affected installed-bytecode invalidation,
  process-bound state rejection, unrelated-class scope hash stability and the
  provisional --write-pack prohibition. Existing full-schema Test-Resolver.py
  and Test-TwoStage.py pass renames, ambiguity, missing reference, incorrect
  descriptors and loader-scoped selected capture rejection.
- Current MappingProbe-v3 passes private transformed-JVM selected-detail capture
  through both standard Attach and disabled-Attach NativeLoader fallback. Lite
  has no bytecode/constant pools; detail includes the installed transformed class
  and cross references. Metadata drift returns recoverable Analyzer exit 5; stale-process requests are rejected
  without stale success. Test-CaptureDiagnostics.py passes separate helper/fallback
  reasons, Java runtime, stderr, exit code and unavailable statistics.
- Mapping pack parity 44; provider passed; boundary/cache 22; Analyzer inventory
  8 each for packs v1/v2; snapshot stream 7 checks, all zero failures.
- Agent policy 194; aim 68,551; knockback 17; navigation 43,271 checks, zero failures.
  BedWars state and controller responsiveness pass. Media shutdown:
  received=1, lateSignals=0, childExited=1.
- Development ControllerUiSmoke exits 0 after all 40 phases, with automatic Attach
  window open, provisional/verified states, stop without injection-state change,
  closed-window updates, reopen and both themes. The provisional label is visible
  in the inspected migrating screenshot.

## Package

Arcveil-v55.9.zip: 51,958,292 bytes.
SHA-256: 5F2A94F4AE554A74BCA99CD1C949CC501C545976D9FD585E9495AF2B557E04D0.
Build and extracted Arcveil.exe SHA-256:
335CA1DEC9C9E111377B84706F1A29A5D7255326B00C8D2D01DEAC373F7A7AAB.
Qt/QML and standalone Analyzer dependencies are deployed; the package is extracted
as Arcveil-v55.9. Extracted Analyzer inspection and all 40 UI smoke phases exit 0
with only Windows system directories in PATH. Provisional migration and verified
success screenshots were inspected. No QML binding/ReferenceError/TypeError appears
in the smoke outputs. Local build/log evidence is under build-mapping-pipeline:
v55.9-dynamic-test.log, v55.9-regressions.log, v55.9-capture-standard.log,
v55.9-capture-fallback.log, ui-v55.9/ and ui-v55.9-extracted/.

## Limits

This iteration was verified with the production service/external deterministic
fixture, the real Analyzer's structural fixtures, and real private transformed
JVM capture. It does not claim a new v55.9 injection into a running Lunar client;
v55.7's successful Lunar run is recorded separately. Unknown runtimes still need
an authored pack that validates or a verified structural reference. Ambiguous
matches remain unresolved. Confidence 0.99 is the existing conservative rule
acceptance tier, not a calibrated probability.

Lite polling uses GetLoadedClasses with metadata only, default 2.5–30 second
backoff and serialized jobs; it is not a JVM class-load event stream. Metadata
changes detect runtime/class-set changes. A bytecode-only change is observed on
selected detail or the mandatory final full fingerprint check. Capture double
reads/final recheck cannot make a third-party JVM-wide snapshot atomic. The Agent
still performs its normal resolve/freeze checks after the preflight gate.
