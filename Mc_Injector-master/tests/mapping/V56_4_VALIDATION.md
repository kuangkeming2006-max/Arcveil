# v56.4 Mapping / Attach acceptance

Baseline: main@ea8a961; isolated branch codex/attach-transaction-cache-v56.4.
Internal version v56.4. Audit of the original state machine was written first in
docs/context/MAPPING_ATTACH_AUDIT.md.

## Production Lunar observation

Windows x64, Lunar Client 1.8.9 v2.23.1-2641, main menu, PID 23700.
McOverlayLiveMappingSmoke used an initially empty persistent cache and executed
normal OverlayManager Attach -> Agent authenticated/OpenGL Active -> DETACH ->
second Attach -> Active -> DETACH. No cached receipt was fabricated or seeded.
The first Attach passed authored SRG live validation and atomically promoted it.
The second had CACHE_LOOKUP hit=true, scoped live validation, final runtime/process
recheck and Agent load with **autoResolveCalls=0, cacheHits=1**. Agent published
"Lunar 1.8.9 SRG" on both cycles, main-menu state no_player, renderer=1.

Committed receipt: V56_4_LUNAR_RECEIPT.json. Raw local log:
build/final-lunar.log in the attached worktree. Second transaction:
78d356ac-d13a-43d8-8998-5e69abb0c565; identity:
a600c6407657da1e50c95439ad5602e8a291c8c60bfa21c9dd9fed52842b35f8.

Observed Debug build durations: a run before the final policy/ownership checks took
63.3 s / 45.0 s; the final run alongside regression checks took 103.9 s / 49.2 s to Active. Cache detail
captures 70 required/hierarchy classes (2.62 MB each independent pass), versus
163 initial authored candidates (4.63 MB), in a JVM with 29,208 loaded classes.
Cheap lite captures 166 classes, 0.825 MB. Two independent detail passes remain a
material cost; this is not an instant-attach claim. Canonical sort now serializes
each item once, and installed bytecode normalization parses each class pool once.
An earlier unoptimized successful run took about 107/63 s. These are observations,
not a controlled benchmark. Standard Attach explicitly reported unsupported once;
later captures and both Agent loads used the remembered NativeLoader preference.

## Automated coverage

TransactionCacheTests: 18 checks, 0 failures. Only capture transport is substituted;
identify/select/resolve/validate and persistent Cache are the production implementations.
Prepare-TransactionFixtures validates original and updated full-schema references.

| Case | Acceptance evidence |
| --- | --- |
| A | renamed Lunar fixture: miss -> real automatic resolve -> validate -> atomic promote -> ready; separate controller gates Agent loading |
| B | same build: hit -> required scope live validation/final check; automatic resolve calls == 0; also observed on production Lunar |
| C | fresh loader instance, changed count/order and extra unrelated class preserve stable identity/cache hit |
| D | Lunar verified last entry cannot become Badlion reference |
| E | cancel in-flight capture invalidates owner, stops capture/watch, no ready/promotion; controller rejects cancelled ready before Agent load |
| F | A cancelled, B has a new UUID and succeeds; controller rejects an old ready result |
| G | actual QML MappingProgressWindow show/close during pending capture preserves owner/matching and completes |
| H | incomplete mapping pauses in MappingPaused, retains Attach/results, no new retries; Resume continues same owner, including pause before any watch timer is armed |
| I | controller Active owner/PID preserved while scanner selects A/B/clears; normal Detach exercised in real Lunar |
| J | installed constants change while metadata stays the same: cached identity rejected -> real automatic resolve -> new atomic publication |

Additional passed checks: MappingService 18; DynamicMappingService 69;
Mapping boundaries/cache 27; progress 26; SnapshotStream 7; Analyzer inventory 8;
MappingProvider; ControllerResponsiveness (bounded IPC, teardown, async scan,
old/cancelled mapping result rejection and Active selection independence).
Cache policy checks additionally prove nearest same-family ranking, exact identity
priority, cheap candidate discovery and corrupt structural-reference fallthrough.
Test-Resolver.py, Test-TwoStage.py and Test-Incremental.py passed.
The packaged 40-phase controller UI smoke passed with an isolated test preferences
folder and no Qt installation on PATH (offscreen/software renderer).

## Scope and limits

A/C/D/E/F/G/H/I/J use fixtures and controlled controller states where appropriate;
no production Badlion or actual client restart/update was performed. The live Lunar
first run used a compatible authored pack; the automatic-resolution branch is proven
by the full-schema renamed fixture, not claimed as a production Lunar auto-resolve.
Main-menu acceptance does not prove all gameplay features/world interactions.
No hooks or registry mutations occur during capture; main Agent startup/freeze and
normal authenticated DETACH remain the injection boundary.

## Reproduce

Build targets MappingAnalyzer, FixtureMappingAnalyzer, TransactionCacheTests,
MappingServiceTests, DynamicMappingServiceTests and McOverlayLiveMappingSmoke.
Run Test-Resolver.py BUILD, then Prepare-TransactionFixtures.py BUILD.
Run TransactionCacheTests FixtureMappingAnalyzer.exe BUILD/resolver-fixtures
P/mapping/contracts-v1.json (QT_QPA_PLATFORM=offscreen for headless QML).
Run McOverlayLiveMappingSmoke PID for two real Attach cycles. The executable fails
unless cycle 2 records a cache hit and zero AUTO_RESOLVE calls. Test timeout is
10 minutes and waits for a published Agent mapping profile plus renderer Active;
no_player/no_world explicitly permit a client left at its main menu.
