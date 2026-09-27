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

## v55.4

- Release controller + Agent + Analyzer + Probe builds passed.
- Mapping boundary/cache tests: 18 checks, 0 failures.
- MappingService async orchestration: 10 checks, 0 failures (includes event flood,
  cache hit ordering, final fingerprint change, failed analysis and cancellation).
- Real transformed private JVM capture passed through standard Attach and with
  DisableAttachMechanism via NativeLoader fallback.
- Whole-schema structural resolver regression passed.
- Existing mapping/Agent suites rerun: provider, 38 parity, 194 policy, 68,551 aim,
  BedWars, 17 knockback, 43,271 navigation, 156 private JVM hook checks passed.
- Controller responsiveness passed (253 UI heartbeats during 5-second scan).
- Test doubles exercise preflight success; they are not real Lunar evidence.
