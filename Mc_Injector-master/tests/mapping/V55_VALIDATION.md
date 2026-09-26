# v55.1 validation

Baseline: main fd480a8. External pack migration only; no automatic mapper.

- Release Agent DLL build: passed (MinGW 13.1, Qt 6.10.1, JDK 21 headers).
- MappingProviderTests: passed.
- MappingPackTests: 38 checks, 0 failures; all four original dictionaries / 247 fields match frozen export digests.
- RegressionPolicyTests: 194 checks, 0 failures.
- AimControlTests: 68,551 checks, 0 failures.
- LogicalPipelineHookTests: private JDK 21 JVM, all three Java fixtures, 156 checks, 0 failures.
- BedWarsStateTests: passed. KnockbackTests: 17 checks, 0 failures.
- NavigationTrajectoryTests: 43,271 checks, 0 failures.

The first hook invocation lacked two fixture classes and failed; after compiling
all existing fixtures the full suite passed. No real Minecraft/Lunar runtime
compatibility claim is made from these tests.
