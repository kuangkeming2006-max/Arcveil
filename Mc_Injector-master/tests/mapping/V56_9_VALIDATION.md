# v56.9 validation — 2026-10-13

Windows x64, MinGW 13.1, Qt 6.10.1. Release Controller/Agent/Analyzer/Probe and
explicit test targets built; assert-dependent policy tests used Debug. No empty
ctest result is counted as verification. Official 1.20.1 artifacts were downloaded
and SHA-1 checked; no real Minecraft 1.20.1 process was started or attached.

## Actual results

| Test | Result / evidence level |
| --- | --- |
| Generate-ModernPack.py | 52 logical keys / 37 exact official JAR member descriptor/static/hierarchy checks; offline only |
| McOverlayGameApiTests | 43 checks, 0 failures; mock JNI and exact Adapter/capability allowlist |
| McOverlayModernJvmTests | 32 checks, 0 failures; real Microsoft Java 17.0.8 with synthetic game objects, -Xcheck:jni, required IDs, runtime version mismatch, world sampling, Player classification, input, NaN, unload, release |
| Test-ModernPipeline.py | 27 checks; production Probe/Analyzer Attach to owned Java 17 JVM, full/compact validation, forged proof/wrong descriptor/malformed pack/unsupported minor versions/legacy contract refusal, modern catalog selection; Forge evidence negative case uses modified offline snapshot |
| ModernMappingServiceTests | 8 checks, 0 failures; production service/Probe/Analyzer, owned Java 17, isolated Cache, cold modern selection from legacy default, proof promotion, fresh repeat validation, loader pin, no checked-JNI warnings |
| Modern full Agent + WGL | Java 17 core context, unified and split window/render threads: HELLO, HOOK_READY, BINDING_READY, RENDERER_READY, GUI updates, Ready game snapshot, DETACH_COMPLETE. Production Agent, synthetic game; not a real Minecraft session |
| McOverlayCoreProfileRendererTests | 13 checks, 0 failures; real WGL 3.2 core context, production GL3 renderer, submitted geometry, no GL errors, cleanup |
| McOverlayRendererTests | 5,599 checks, 0 failures; existing real WGL compatibility renderer, GL 4.6 NVIDIA 596.49 |
| Legacy full Agent + WGL | Oracle Java 8u51 owned JVM compatibility context, GUI/input/drained detach passed; no Minecraft binding claim |
| LogicalPipelineHookTests | 156 checks, 0 failures **each** on real Oracle Java 8u51 and Microsoft Java 17.0.8; synthetic Java 8 bytecode fixture |
| MappingProvider / MappingPackTests | passed / 67 checks, 0 failures; legacy dictionary parity |
| MappingAnalyzerTests / MappingBoundaryTests | 24 / 32 checks, 0 failures |
| MappingServiceTests | 22 checks, 0 failures; fake subprocess, includes unchanged legacy digest, separate modern digest, edited sub-contract invalidation |
| DynamicMappingServiceTests / TransactionCacheTests | 92 / 25 checks, 0 failures; fake transport / production Analyzer with substituted capture; includes v56.8 recovery and unsupported API rejection |
| Test-Resolver / VersionAdapters / TwoStage / Incremental | all passed; synthetic snapshots, original confidence/proof/provisional/loader boundaries |
| Test-LunarForge | recorded 70-class / 548-field / 3,124-method hierarchy metadata positive and descriptor/static/loader/missing/duplicate-anchor negatives passed; offline, not fresh game Attach |
| Debug AimControl / BedWarsState / Knockback / NavigationTrajectory | 70,321 checks / assertion-based passed / 17 checks / 43,271 checks; all zero failures |
| RegressionPolicyTests / ControllerResponsivenessTests | 201 source-policy checks passed / 5,001ms async scan, 250 UI heartbeats, bounded IPC and teardown passed |
| Distribution startup / tools | Release dependency package, isolated settings/TEMP, PATH limited to package + Windows: Arcveil --smoke-test --software-renderer exited 0. Packaged Analyzer also ran with system-only PATH from another directory; schema valid, injectionReady=false. Bundled helper runtime Java 21.0.10; target fixture JVM Java 17 remains independent |

## Reproduction of new tests

Run in a built Windows checkout with Qt/MinGW runtime available and writable TEMP.
Replace absolute JDK/Java paths with the local installation. Tests own their JVMs;
do not supply a game PID.

```powershell
python Mc_Injector-master/tests/mapping/Prepare-ModernFixture.py build-main-release/modern-fixture
& "$jdk/bin/javac.exe" --release 17 -d build-main-release/modern-fixture '@build-main-release/modern-fixture/sources.txt'
& build-main-release/McOverlayModernJvmTests.exe "$java17/bin/server/jvm.dll" build-main-release/modern-fixture Mc_Injector-master/mapping/packs/vanilla-1.20.1.json
python Mc_Injector-master/tests/mapping/Test-ModernPipeline.py build-main-release "$java17/bin/java.exe" build-main-release/modern-fixture
& build-main-release/ModernMappingServiceTests.exe "$pwd/build-main-release" "$java17/bin/java.exe" "$pwd/build-main-release/modern-fixture" "$pwd/Mc_Injector-master"
& build-main-release/McOverlayCoreProfileRendererTests.exe
& Mc_Injector-master/tests/Run-OpenGlJvmSmoke.ps1 -HarnessExe build-main-release/McOverlayOpenGlJvmSmoke.exe -AgentDll build-main-release/agent/McOverlayAgent.dll -JavaHome $java17 -FixturePath build-main-release/modern-fixture -MappingPack Mc_Injector-master/mapping/packs/vanilla-1.20.1.json -CoreProfile
# Repeat the final command with -SplitThreads.
```

## Limits / required next validation

Real 1.20.1 Vanilla menu/world/main-thread/mouse/driver behavior and real 1.8.9
Vanilla/Forge/Lunar gameplay were **not** revalidated. No Forge 1.20.1 mapping is
shipped. No modern world projection/ESP rendering, AimAssist, attack, inventory,
BlockState/bed scanning, movement/network/tick transform capability is enabled.
Unsupported neighboring versions cannot use the 1.20.1 Adapter or bypass actual
runtime version verification. A wholly unknown anchor may produce missing-mapping
watch diagnostics; catalog identity is a candidate, not an independent semantic
version detector. See docs/context/MINECRAFT_1_20_REVIEW.md for real-game steps.

The packaged START-HERE and BUILD-INFO record startup/package checks separately.
