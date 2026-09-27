# Internal v55.5 validation — 2026-09-27

Baseline origin/main fd480a8; branch codex/mapping-pipeline. v54 work is separate.
Stage commits: v55.1 0bd49d9, v55.2 1a82ce8, v55.3 40f5903, v55.4 b82a2ee;
v55.5 is the commit containing this report. Release MinGW 13.1 / Qt 6.10.1 / JDK 21.

Final results (all passed):
- MappingProvider legacy behavior; MappingPack parity 38 checks, including all 247 original fields across four dictionaries.
- Analyzer inventory 8 checks; full 257-symbol synthetic structural rename, ambiguity, absent reference and incorrect optional descriptor rejection.
- MappingBoundary 19 checks: digest verification, frozen registration, option validation, candidate/verified/previous, rollback, corruption, locking and malformed revision paths.
- MappingService 13 checks: preflight ordering, cache hits, fingerprint changes, failed resolution, cancellation generations, JSONL flood responsiveness, bounded UI model and progress.
- Agent policy 194; aim 68,551; knockback 17; navigation/trajectory 43,271; BedWars state passed.
- Private JVM logical pipeline 156 checks. Installed transformed bytecodes captured with both standard Attach and disabled Attach/native fallback; modified constant pool verified.
- Controller responsiveness: five-second scan, 252 UI heartbeats.
- Controller UI smoke passed phase 33: navigation/modal behavior, hidden Mapping Console, Settings open, dark/light themes and close. Screenshots visually checked.
- Full Release runtime built; installed with Qt/QML and compiler runtime dependencies, including a standalone tools directory.
- ZIP extracted into a fresh directory. Packaged Analyzer schema validation and full packaged UI smoke passed with Qt/MinGW removed from PATH.

Artifacts: Arcveil-v55.5.zip (50,255,103 bytes), extracted Arcveil-v55.5/Arcveil.exe.
ZIP SHA256: 051356d09e0c1ba491b83816e14fc73308259b1410374433092028cd7929a687.
Build logs and screenshots: build-mapping-pipeline/*-final.log, ui-v55/, ui-package-v55/ in the primary local workspace.

Limits: no running Minecraft/Lunar client was available, so real Lunar mapping/injection
is not claimed. Structural confidence is rule-based, not statistical calibration.
Unknown/ambiguous runtimes require a valid reference and fail closed otherwise.
The existing WindowsMediaBridge shutdown QProcess warning remains in smoke logs;
no QML errors occurred in successful runs. Probe capture is per-class stable plus a
final fingerprint recheck, not an atomic snapshot of the entire JVM.
