# v56.6 PR integration validation

2026-10-09, Windows x64, Debug / MinGW 13.1 / Qt 6.10.1.
Integrates PR #5 (`b95d122`), #6 (`389a472`) and #7 (`9546a77`).

## Resulting behavior

- Preserve independent GUI element sizing, 40–150% window dimensions, compact
  layouts, rounded keyboard-accessible dropdowns and the gather/scatter animation.
- Preserve Attach transaction ownership/cancellation, family-scoped verified cache,
  compact installed-evidence checks and pinned-loader JNI binding readiness.
- Resolve three overlapping version/README conflicts by retaining both feature
  descriptions and displaying internal build v56.6 in About, navigation and Mapping.
  Keep the controller UI smoke's expected build label in sync.
- Correct the dependency document's current compact-capture boundary to Probe-v13,
  matching the CMake output and packaged DLL. No further protocol change is added.

## Checks performed on the combined sources

| Check | Result |
|---|---|
| Full application, Agent, Analyzer, Probe, helpers and explicit test-target build | passed |
| Controller responsiveness | bounded/split IPC, stale transaction gate, binding + renderer readiness, GUI configuration persistence/protocol, async scanning passed |
| Navigation/trajectory | 43,271 checks, 0 failures |
| Real WGL renderer | 5,599 checks, 0 failures |
| Analyzer inventory / installed proof | 24 checks, 0 failures |
| Mapping boundaries / cache | 32 checks, 0 failures |
| MappingService | 18 checks, 0 failures |
| Mapping progress | 26 checks, 0 failures |
| Snapshot stream | 7 checks, 0 failures |
| Dynamic MappingService | 69 checks, 0 failures |
| Transaction / cache with production Analyzer fixtures | 21 checks, 0 failures |
| Resolver / two-stage / incremental Python regressions | all passed |
| Private JVM + production Agent + WGL, unified and split threads | both passed, including GUI_ELEMENT_SCALE valid/invalid commands and drained detach |
| Dependency-packaged application, development Qt/MinGW removed from PATH | D3D11 40-phase controller UI smoke returned 0 |
| Packaged Analyzer schema validation | passed; injectionReady=false as required for schema-only validation |
| Packaged Java | Java 21.0.10, java.base + jdk.attach + jdk.internal.jvmstat present |

Renderer output includes compact dark/light layouts and independent element scales
60/100/150. The compact Attack Shield screenshot was visually inspected.

Initial controller and boundary runs could not publish files under the sandboxed
system temporary directory. The same binaries passed with TEMP and TMP set to
`.research/v56.6-temp` inside the writable workspace; no source change was needed.
This environment failure is not recorded as a product regression or a passing run.

## Reproduction

Use `build-cache-attach-debug` configured with `MC_OVERLAY_UI_TESTS=ON`.
Prepend Qt 6.10.1 and MinGW runtime directories to PATH for development tests,
and use a writable TEMP/TMP directory. Build these explicit test targets in addition
to `all` (plain ctest does not execute these tests):

```text
McOverlayControllerResponsivenessTests McOverlayRendererTests
McOverlayNavigationTrajectoryTests McOverlayOpenGlJvmSmoke
MappingAnalyzerTests MappingBoundaryTests MappingServiceTests MappingProgressTests
SnapshotStreamTests DynamicMappingServiceTests TransactionCacheTests
```

Run `tests/mapping/Test-Resolver.py <build>` before the two-stage and incremental
scripts. Its generated `resolver-fixtures/reference.json` is the Analyzer inventory
input; TransactionCacheTests uses FixtureMappingAnalyzer, that fixture directory
and `mapping/contracts-v1.json`. MappingServiceTests uses FakeMappingAnalyzer,
default-v2.json and the contracts. MappingBoundaryTests uses default-v1.json.
Run `tests/Run-OpenGlJvmSmoke.ps1` with the built harness/DLL and Java 21, once
without and once with `-SplitThreads`.

Local verification logs are `.research/v56.6-*.log`; renderer captures are
`render-v56.6/`. The runnable package is `Arcveil_v56.6-integration/Arcveil.exe`,
with archive `Arcveil_v56.6-integration.zip`. It includes Qt/QML/plugins, MinGW
runtime, Java attach runtime, Agent, Analyzer, Probe-v13, helpers, mappings,
contracts and licenses. The application/Agent are Debug builds; Qt's installed
runtime and plugins are the release Qt binaries selected by windeployqt.

## Scope

This integration run did not attach to a real Minecraft/Lunar process, repeat
the earlier Lunar performance measurements, or re-run live Microsoft Pinyin.
PR #7's original real Lunar receipts and limitations remain in
`tests/mapping/V56_5_VALIDATION.md` and `tests/mapping/evidence/v56_5-lunar.jsonl`.
Private JVM fixture tests establish protocol/rendering behavior, not real-client
mapping or in-world feature coverage.
