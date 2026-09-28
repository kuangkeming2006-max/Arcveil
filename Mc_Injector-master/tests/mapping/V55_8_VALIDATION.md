# v55.8 Mapping progress validation — 2026-09-28

Scope: independent Qt/QML window and structured state consumption on the v55.7
pipeline. Stage 1 commit: a7fd400. No matching score/threshold/selection algorithm,
Agent registry/freeze, renderer, JNI or injection authorization changes.

## Verified

- MappingProgressTests: 19 checks, 0 failures. Covers automatic open notification,
  pending → active → completed dwell/migration, unverified result rejection,
  snapshot-change retries of unfinished rows only, stable completed rows, repeated
  snapshots, stop, required completion, stale timers and late session reset.
- MappingServiceTests: 18 checks, 0 failures. Connects the production service to
  the new controller with a fake analyzer subprocess. Stops the progress controller
  while the service is busy and still gets ready; command ordering remains
  inspect/validate/inspect or inspect/inspect on cache hit. No provisional matches
  become verified before final recheck. Missing reference and verified cache source
  are asserted. Existing cancellation/failure/stream bounds remain covered.
- ControllerUiSmoke: 40 phases, exit 0, for development and extracted package.
  Exercises actual attachToProcess(0), so auto-open is tested without injecting any
  process. Covers native window visibility, active/settling/completed lists, stop
  without OverlayManager state changes, closed-window completion, reopen and both
  themes. No QML ReferenceError/TypeError/binding errors in the logs.
- Mapping pack parity 44; cache/boundary 22; Analyzer inventory 8; snapshot stream 7;
  provider tests all passed. Full-schema structural resolver fixtures, two-stage
  scoping and capture failure diagnostics passed. RuntimeMapping receipt assertions
  confirm accepted field/method rows contain actually found names.
- Agent policy 194; aim 68,551; knockback 17; navigation 43,271 checks passed.
  BedWars and controller responsiveness passed. Media shutdown test: received=1,
  lateSignals=0, childExited=1.

## Package

Arcveil-v55.8.zip: 51,921,003 bytes.
SHA-256: 3C99729162334E3B95E8A002B819BDCFC87BBEF4CB154D3DD1AFF52486CFE818.
Build and extracted Arcveil.exe SHA-256:
BF0AAB4E7979BD169BD4B1C70A42CFEE6AD6B13F16C5F4EDFB82622B5F1E9CF0.
Qt/QML and Analyzer dependencies deployed; package extracted to Arcveil-v55.8.
Packaged Analyzer inspection and UI smoke passed with only Windows directories in
PATH. Screenshots/logs are in build-mapping-pipeline/ui-v55.8[-extracted].

## Limits and stop semantics

No new real Lunar injection was needed for this UI stage; v55.7's live Lunar
results remain recorded separately. This iteration does not add autonomous
snapshot polling or restart a failed preflight. Snapshot updates requeue unfinished
presentation state. Stop disables subsequent progress/retry consumption and row
scanning; it does not kill the mandatory Analyzer process or change its existing
preflight chain. Completed results and final receipts remain consumable. Thus it
is not a new independently cancellable background structural matcher. Final
fingerprint drift still fails closed, as required by preserving mapping behavior.
