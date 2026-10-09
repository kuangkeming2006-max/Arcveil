# Mapping pipeline — internal v55.9

The controller validates mappings before loading the main Agent. Gameplay uses logical
symbols; MappingRegistry remains the final registration boundary and freezes once.
Default packs are external, versioned JSON. Missing or invalid packs fail closed.

## Developer console
Open Settings → Open Mapping Console, or Ctrl+Shift+M. It is hidden during normal
injection. Events show capture fingerprints, selected packs, symbols, confidence,
evidence, failures and validation. The view retains 2,000 rows; complete JSONL logs
are in the displayed run directory. Clear view does not delete logs.
Rollback previous selects the previous verified revision for the next injection.
It never modifies a running Agent registry. Cancel check cancels preflight.

## Standalone analyzer
Run from the extracted package root (PowerShell). Supply absolute paths for outputs.
For Java 9+, use a JDK java.exe matching the target architecture:

```powershell
.\tools\MappingAnalyzer.exe inspect --pid 1234 --java "C:/JDK/bin/java.exe" --native-loader "$PWD/tools/McOverlayNativeLoader.exe" --pack "$PWD/agent/mappings/default-v2.json" --out "$PWD/reference.json"
.\tools\MappingAnalyzer.exe validate --pack "$PWD/agent/mappings/default-v2.json" --snapshot "$PWD/reference.json" --out "$PWD/validation.json"
.\tools\MappingAnalyzer.exe inspect --snapshot "$PWD/reference.json" --out "$PWD/normalized.json"
.\tools\MappingAnalyzer.exe resolve --pack "$PWD/agent/mappings/default-v2.json" --reference "$PWD/reference.json" --snapshot "$PWD/new-runtime.json" --out "$PWD/candidate.json" --write-pack "$PWD/new-pack.json"
.\tools\MappingAnalyzer.exe diff --before "$PWD/agent/mappings/default-v2.json" --after "$PWD/new-pack.json" --out "$PWD/diff.json"
```

Capture new-runtime.json from the target JVM using inspect. A reference must come
from a runtime where the supplied pack validates. For Java 8 add --tools-jar with
that JDK's lib/tools.jar. Helper JAR, probe DLL and contracts default beside the tool.
All commands emit eventVersion 1 JSONL. validate without a snapshot checks syntax
only and reports injectionReady=false. Exit 3 is failed live validation; exit 4 is
incomplete resolution; exit 5 is recoverable capture drift. --write-pack is written
only after complete validation and is prohibited for incremental provisional state.

Matching uses hierarchy, descriptors, member structure, normalized installed bytecode
and call/reference graphs. Names have zero ranking weight. Exact unique structural
matches receive 0.99 against threshold 0.98; these are rule-based confidence tiers,
not calibrated probabilities. Ambiguity, unsupported instructions, missing reference
or failed required contracts stop injection; there is no guessing fallback.

The probe reads installed JVM method bytecodes/constant pools, not original JAR files.
Per-class double reads and a second complete fingerprint before injection detect
changes, but cannot make the whole JVM snapshot atomic. A bootstrap pack/reference
is still required for unfamiliar runtimes; this is not a universal deobfuscator.

## Cache and validation
AppLocalDataLocation/mapping-cache-v1 contains run candidates and immutable verified
objects. An atomic index publishes candidate → verified and retains previous.
Proofs bind pack, snapshot and contract digests to the analyzer revision. Corruption,
process reuse, changed fingerprints and incomplete candidates are rejected.

Regression coverage includes legacy pack parity, synthetic full-schema renaming and
ambiguity, private JVM transformed bytecode capture (standard and disabled Attach),
cache/Agent option boundaries, cancellation, event-loop responsiveness and UI smoke.
Real Lunar 1.8.9 (v2.22.42-2639, Zulu 17.0.18) passed lite capture, selected detail,
live SRG validation and a complete Agent/renderer handshake on 2026-09-28. This
is evidence for that runtime, not every Lunar version. Unsupported runtimes fail closed.

## v55.6 capture diagnostics (lite verification gate)

Before trying an unfamiliar Lunar runtime, keep the client on its main menu and run:

```powershell
.\tools\MappingAnalyzer.exe inspect --pid 1234 --lite
```

`inspect-lite --pid 1234` is an alias. `--java` is optional: the analyzer queries the
target process executable and selects java.exe in the same directory. Override it
with a matching JDK when needed. `--out` defaults to snapshot-1234-lite.jsonl in the
current directory. This command only captures metadata and emits a domain-separated
metadata fingerprint and class index. It never invokes mapping resolution or loads
the main Agent. Lite requests no bytecode/constant-pool JVMTI capabilities and reads
no constant pools or bytecodes. Without --pack it performs no dependency warmup.

`CAPTURE_PATH` events contain separate standardAttach/fallback objects: selected
javaRuntime/program, started/timedOut, exitCode, exitStatus, stdout, stderr, status,
and captureFailure/reason when applicable. Fallback has a separate UUID-scoped
output path, so an earlier Attach error cannot masquerade as a fallback result.
`SNAPSHOT_STATS` contains loadedClassCount, capturedClassCount, classMetadataBytes,
constantPoolBytes, bytecodeBytes, totalBytes, configuredLimit, largestClass and
largestClassBytes. Metadata/largest class sizes are serialized JSON bytes; constant
pool/bytecode sizes are decoded JVM bytes; totalBytes is the exact capture wire file
size, including framing. Unavailable statistics are null, never fake zero counts.

Transport is JSONL with 64 KiB payload chunks, sequence/identity checks and a required
completion footer. It streams one class at a time; no 48/64 MiB aggregate capture
file ceiling remains. Independent protections are 512 KiB per frame, 48 MiB per
serialized object and 16 MiB per JVMTI binary buffer. Limit errors report limitLayer,
limitName, observedBytes and configuredLimit, including class/method identity for
JVM buffers. Truncated/partial files are never published as successful snapshots.
Legacy whole-JSON offline inputs retain their explicitly named 64 MiB reader limit;
lite output uses the streaming format. Neither lite fingerprints nor lite snapshots
are verified bytecode mappings and must not authorize an injection cache hit.

## v55.7 two-stage preflight

`inspect --pid PID --pack PACK --out SNAPSHOT` now performs lite capture, selects
candidate classes, and captures only those classes' constant pools and installed
method bytecodes. It exports cross references with a completeness flag; unsupported
bytecode remains ineligible for automatic acceptance. It retains `SNAPSHOT.lite.jsonl`
and `SNAPSHOT.candidates.json` for diagnosis. Supply `--reference VERIFIED_SNAPSHOT`
when selecting unfamiliar renamed classes structurally. Authored names request a
capture scope only; the separate live validator still decides acceptance.

Stages can also run independently:

```powershell
.\tools\MappingAnalyzer.exe inspect --pid 1234 --lite --pack "$PWD/agent/mappings/default-v2.json" --out "$PWD/index.jsonl"
.\tools\MappingAnalyzer.exe select --pack "$PWD/agent/mappings/default-v2.json" --snapshot "$PWD/index.jsonl" --out "$PWD/selection.json"
.\tools\MappingAnalyzer.exe inspect-detail --pid 1234 --candidates "$PWD/selection.json" --out "$PWD/detail.jsonl"
```

Selections bind the PID, process creation time, lite fingerprint, defining-loader
instance and metadata digest. Changed/missing classes, loader collisions, mismatched
metadata or incomplete results fail closed. Content-identical loaders retain separate
process-scoped identities; they are never merged. Lite avoids class-version queries,
which some transformed Lunar classes reject despite supporting metadata enumeration.

`default-v2.json` retains all four v1 dictionaries unchanged and adds an authored Lunar
SRG dictionary from the existing Forge SRG symbol data. v1 remains shipped; Agent and
Injector defaults select v2. No Gameplay symbols or registry freeze behavior changed.

Preflight and its final recheck both use this pipeline. Snapshot cache hashes stream
without an aggregate size ceiling; pack/metadata JSON bounds remain. Analyzer cache
revision 4 invalidates older proofs. Legacy full inspect without a pack remains an
explicit diagnostic compatibility path, outside normal injection.

Long capture requests are stored in a bounded temporary JSON file; Attach carries a
short file reference, and the native worker takes its own copy. Native startup verifies
that the target can create diagnostics. CAPTURE_PATH adds `lastProbeStatus` and tagged
`captureFailure.stage/win32Error/reason` for request reads and output writes, rather
than waiting a minute when a path is unavailable. Failed normalization retains the
raw capture for inspection; partial files never authorize injection.

The opt-in `McOverlayLiveMappingSmoke PID [UNIQUE_PROBE_PATH]` test uses production
OverlayManager, isolated preferences/cache under the build directory, checks ready
mapping plus renderer activation, and then detaches. The optional unique probe path
supports testing while an older DLL is resident. Its cache directory is printed and
retained. It is not part of normal application startup.

v55.7 shipped protocol v2 as `MappingProbe-v2.dll`, so a resident v1 probe could not be
accidentally reused after an application upgrade. The NativeLoader's module-identity
checks remain enabled.


## v55.9 Dynamic matching and progress window

Attach opens an independent window with Cache / Snapshot / Resolve / Complete
steps, reference provenance, and active/completed/pending symbol lists. Close
hides the window; reopen from Settings or Ctrl+Shift+P. The developer console
remains available independently (Ctrl+Shift+M).

MappingService owns the actual watch/retry lifecycle. Incomplete automatic
resolution persists unique accepted bindings and an unresolved set in the run's
matching-state.json, then waits. Lite-only capture starts after 2.5 seconds and
backs off to 30 seconds for unchanged snapshots. Jobs are serialized on QProcess;
changed relevant scopes debounce for 750 ms before selected detail capture.
Unchanged lite fingerprints skip selection/detail; unrelated class changes skip
detail after selection compares a loader-instance-bound relevant fingerprint.
Watch requests do not hydrate authored classes or request bytecode/constant pools.

On a new selected generation, Analyzer revalidates accepted bindings using live
lookup and normalized structural/bytecode/reference proofs. Still-valid results
remain stable; only unresolved or invalidated symbols start matching again.
Acceptance criteria, confidence tiers and the existing structural matcher remain
unchanged. Without a verified reference, the service waits without guessing.

New unique matches emit symbol-started → symbol-progress → symbol-matched and
symbol-provisional-match. They migrate to completed with an explicit
provisional / awaiting-final-validation label. Provisional results cannot grant
injection readiness or be exported through --write-pack. Required completion
triggers independent full live validation, a new selected detail capture, final
fingerprint and PID/creation-time checks, then cache promotion and ready().
Final fingerprint drift starts another generation and rechecks affected bindings;
it does not emit failed(). Typed capture drift (exit 5) also returns to waiting.
Other capture failures get bounded recovery; a target exit or persistently
unavailable capture is fatal. Cache candidate/verified/previous and rollback
semantics remain intact. Agent registry/freeze and injection startup are unchanged.

Stop Matching stops future watch/retry and preserves accepted results. It neither
kills an in-flight job nor cancels attach, changes injection state, detaches or
rolls back the Agent. That generation may finish validation and ready; if a new
generation is needed it pauses. Resume Matching starts a fresh lite check. Cancel
Attach remains the separate whole-operation cancellation action.

The window consumes structured events only, including snapshot-watch,
snapshot-unchanged, snapshot-changed, retry-scheduled, retry-started,
symbol-provisional-match, symbol-revalidated, symbol-invalidated and
waiting-for-runtime-change. Successful provisional rows keep their position and
migration timers during revalidation. Final verification upgrades them in place.
QML contains no retry or matching business logic.

Standalone incremental usage (absolute output paths):

```powershell
.\tools\MappingAnalyzer.exe select --allow-empty --pack "$PWD/agent/mappings/default-v2.json" --reference "$PWD/reference.json" --snapshot "$PWD/index.jsonl" --out "$PWD/selection.json"
.\tools\MappingAnalyzer.exe resolve --incremental --pack "$PWD/agent/mappings/default-v2.json" --reference "$PWD/reference.json" --snapshot "$PWD/detail.jsonl" --out "$PWD/state.json"
.\tools\MappingAnalyzer.exe resolve --incremental --pack "$PWD/agent/mappings/default-v2.json" --reference "$PWD/reference.json" --snapshot "$PWD/next-detail.jsonl" --state "$PWD/state.json" --out "$PWD/next-state.json"
```

Incremental state binds the source pack/contracts/reference, dictionary and target
PID/creation time. Its pack is a provisional draft; use independent validate and
fresh capture before promotion. Normal standalone resolve --write-pack retains
its existing complete-validation behavior. MappingProbe-v3.dll distinguishes
recoverable runtime-drift output from older resident Probe versions.

Tests: DynamicMappingServiceTests drives the production service and an external
100-symbol analyzer fixture through 70 → 90 → 100, unchanged/irrelevant updates,
stop/resume, final drift, cancellation and capture recovery. Test-Incremental.py
exercises the real Analyzer's retention/invalidation and export gate.
MappingProgressTests and ControllerUiSmoke cover provisional migration, verified
upgrade, automatic open, stop, close/reopen and both themes. Exact evidence and
limits are in tests/mapping/V55_9_VALIDATION.md.


## v56.4 transaction/cache audit

Attach owns a UUID + PID/creation-time transaction through mapping, loading and
Active. Cancellation suppresses old ready/cache/loading/UI results. Reusable cache
v2 uses stable required installed-class structure and family/contracts/schema/
analyzer evidence; runtime fingerprints are only transaction drift receipts.
A verified family cache candidate always gets required-only live validation and
final recheck, with no automatic resolve on a compatible second Attach.

Reference priority: stable identity, same family/version, same family, explicit
Vanilla base/version, none. Cross-client lastVerified is never a strong reference.
Pause Automatic Matching stops future retries and preserves the pending owner;
Clear Selection is browsing, Cancel Attach cancels preflight/loading, Active Detach
uses normal Agent DETACH. Closing progress only hides presentation.

The current Probe filename is MappingProbe-v8.dll. NativeLoader preference is
memoized by PID/creation time. Known required classes can be linked without target
initialization for cold-start validation; changed detail is reused only within its
original transaction. Stable ambiguity pauses polling; missing runtime classes use
at most 5-second watch backoff. Console records transaction, cache, reference and
capture transport decisions.

Acceptance commands (from a configured build): run Test-Resolver.py, then
Prepare-TransactionFixtures.py; TransactionCacheTests takes FixtureMappingAnalyzer,
resolver-fixtures and contracts-v1.json. McOverlayLiveMappingSmoke PID performs two
real attach/detach cycles in a persistent fresh cache and requires cycle 2 to have
cacheHits > 0 and autoResolveCalls == 0. Main-menu no_player/no_world is acceptable
only after the Agent publishes a mapping profile and renderer Active. See
../tests/mapping/V56_4_VALIDATION.md for evidence and remaining coverage limits.
