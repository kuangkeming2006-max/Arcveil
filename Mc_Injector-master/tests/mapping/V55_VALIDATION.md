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

## v55.2

- MappingAnalyzer / MappingProbe Release builds passed.
- Real private JVM Attach test passed after a premain class transformer changed
  MappingCaptureSubject; captured installed constant pool contains the changed
  value and offline inspect preserves the fingerprint.
- Analyzer inventory/tamper/schema/diff tests: 8 checks, 0 failures.
- All v55.1 mapping and Agent regression suites rerun and passed unchanged.
- Real Lunar has not been exercised; unsupported Attach/capabilities fail closed.

## v55.3

Original worktree Release Analyzer build passed. Full-schema (257 keys) renamed
class/member fixture passed; missing reference, ambiguous classes and wrong
optional object descriptor were rejected, preserving the final-pack output.
MappingProvider, pack parity (38), policy (194), aim (68,551), BedWars, knockback
(17), navigation (43,271), private JVM logical hook (156) regressions all passed.
Confidence is a rule-based exact-evidence tier, not a measured probability.
Real Lunar remains untested; unrecognized/unsupported structures fail closed.
