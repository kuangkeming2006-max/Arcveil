# v55.7 validation — 2026-09-28

## Delivered behavior

- Normal preflight and final recheck use metadata-only lite capture, candidate scope
  selection, and selected CP/bytecode/cross-reference capture. No aggregate snapshot
  limit was raised; streamed transport and bounded frames/objects remain.
- Lite avoids class-version queries rejected by certain Lunar transformed classes.
  Identical classloader content groups retain distinct runtime identities; hash
  collisions fail closed. No matcher acceptance/ranking algorithm was changed.
- Short Attach options reference a bounded request file. MappingProbe-v2.dll copies
  the request before its worker starts. CAPTURE_PATH preserves both helper results,
  stdout/stderr/runtime, last probe phase and tagged Win32 file errors.
- External default-v2.json retains v1 dictionaries and adds a separate authored Lunar
  SRG dictionary. Agent registration/freeze and Gameplay logical names are unchanged.
- Cache proof revision 4; streamed file hashing has no aggregate snapshot cap.
- Packaging exposed an existing MediaSessionService destructor bug: QProcess could
  emit callbacks after cached fields were destroyed. Disconnecting callbacks before
  teardown and waiting for the killed child fixes the reproduced heap corruption.

## Real Lunar evidence

Runtime: Lunar 1.8.9 v2.22.42-2639, Zulu 17.0.18, Windows x64.
PID 18036 passed capture, validation, authenticated Agent handshake and renderer
activation. After the user restarted Lunar, PID 13172 passed again in a singleplayer
world. Both sessions detached their hooks after the test.

On PID 13172 the stable successful run captured:

| Stage | Loaded classes | Captured classes | Constant pool bytes | Bytecode bytes | Wire bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Lite | 33,410 | 22,407 | 0 | 0 | 46,129,070 |
| Selected detail | 33,411 | 155 | 1,536,002 | 264,409 | 4,640,435 |

The final recheck captured the same 22,407/155 classes and fingerprint
`09bc4b814d33a7de5b203f88d53cc819f65c4c57b44c034446a1256f495b0fd0`.
Result: `success=1 profile=Lunar 1.8.9 SRG mappingState=ready renderer=1`.
An earlier attempt detected newly loaded classes and stopped before injection;
retry after loading settled succeeded. This rejection was not bypassed.

The inventory includes prepared non-bootstrap object classes. Arrays, bootstrap
classes and unprepared classes are not exported; loadedClassCount covers the full
JVMTI enumeration. Per-class double reads and a final recheck are not a JVM-wide
atomic snapshot. This evidence does not establish compatibility with every Lunar
version or automatically resolve arbitrary future obfuscations.

The opt-in live test isolates settings and puts its cache under the build directory.
An earlier Qt test cache path was unavailable to the target (Win32 3); file failures
now report their actual stage instead of being mistaken for a capture timeout.
Probe/Agent DLLs can remain resident after hooks detach. Restart Lunar before using
an upgrade from a different package directory; module identity guards remain active.

## Regression results

- Mapping pack parity: 44 checks; provider tests passed.
- Mapping boundaries/cache: 22 checks, including >64 MiB streaming hash and rollback.
- MappingService: 14 checks; Analyzer inventory: 8 checks each with v1 and v2 packs.
- Snapshot transport: 7 checks, including aggregate size, Unicode, order and truncation.
- Full 257-symbol structural fixture: rename recovery, confidence/evidence, missing
  reference, ambiguous matches and wrong optional descriptors all passed.
- Two-stage fixtures: scoped candidates, structural reference, stable loader IDs,
  separate equal-content loaders, collisions and lite-injection guards passed.
- Private transformed JVM: standard Attach and disabled-Attach fallback passed lite
  and selected-detail capture. Detail captured one transformed class. Metadata drift,
  stale PID identity and missing output directory were rejected with diagnostics.
- Agent regression: policy 194, aim 68,551, knockback 17, navigation 43,271 and real
  private-JVM logical pipeline 156 checks passed; BedWars and controller responsiveness
  tests passed.
- Media shutdown regression: pre-fix exit 0xC0000374; fixed result
  `received=1 lateSignals=0 childExited=1`.
- Packaged Analyzer runs with only the Windows system directories in PATH. Final
  packaged UI passed all 33 phases and exited 0, including About/Mapping Console.

Local evidence: build-mapping-pipeline/lunar-13172-live-injection-v557-r2.log,
v557-regression-summary.log, v557-live-detail-fixture.log,
v557-live-detail-fallback.log, v557-live-lite*.log, media-shutdown-before.log,
media-shutdown-after.log, v557-fixed-ui-err.log and ui-package-v55.7-fixed/.
