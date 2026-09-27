# Capture diagnostics v55.6 — first verification gate

Implemented CAPTURE_PATH with independent standardAttach/fallback exit codes,
stderr/stdout, selected Java runtime, timeout/start status and probe failure details.
SNAPSHOT_STATS includes all requested counts/byte sizes and exact named limits.
The former 48 MiB aggregate class JSON limit is removed via class-by-class chunked
JSONL transport, not by increasing a fixed snapshot ceiling. Per-buffer/object/frame
limits remain independently enforced and reported.

inspect --pid PID --lite / inspect-lite captures metadata only, automatically chooses
the target runtime when --java is absent, and emits a metadata fingerprint and class
index. No mapping resolve or main Agent starts. Lite snapshots explicitly cannot be
passed to validateRuntime/resolveMappings as detailed evidence.

Passed on 2026-09-27:
- SnapshotStreamTests: 7 checks, including >64 MiB aggregate snapshot, Unicode chunk
  boundaries, truncation/order rejection and distinct frame/object limit diagnostics.
- Private transformed JDK 21: standard Attach and disabled-Attach native fallback,
  each with two identical lite fingerprints, constantPoolBytes=0 and bytecodeBytes=0;
  runtime auto-selection and streamed offline roundtrip passed.
- Full transformed capture compatibility and Analyzer inventory: 8 checks.
- Capture diagnostics: independent launch failure details, unavailable statistics,
  and refusal to validate/resolve lite snapshots passed.
- Existing structural resolver fixture suite passed without changing its matching
  algorithm; 257-symbol rename/ambiguity/missing-reference/optional descriptor tests.
- Mapping pack parity 38, provider behavior, mapping boundaries 19, service 14.
- Agent policy 194, aim 68,551, knockback 17, navigation 43,271 and BedWars state.
- Release runtime built, dependencies deployed, ZIP extracted. Packaged Analyzer lite
  roundtrip and full controller UI smoke (33 phases) passed with Qt/MinGW removed
  from PATH. Existing WindowsMediaBridge shutdown warning remains unrelated.

Artifact: Arcveil-v55.6.zip, 50,343,632 bytes.
SHA256: 80520249669a84068f6cd5e5df5968ebeef07c38873cc46542f423ce4035d20c.
Logs and screenshots are in the primary workspace build-mapping-pipeline directory.

NOT completed: real Lunar lite verification, candidate class selection from the lite
index, inspect-detail and pre-injection two-stage integration. PID 19432 was observed
but exited before diagnostics ran; later Java/javaw process checks returned none.
User was asked to restart Lunar and keep it running. Per the requested gate, matcher
work is paused at that missing live verification, not represented as a Lunar pass.
Existing full inspect remains available for compatibility during this first stage.
