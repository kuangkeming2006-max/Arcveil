# Mapping pipeline — internal v55.5

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
.\tools\MappingAnalyzer.exe inspect --pid 1234 --java "C:/JDK/bin/java.exe" --native-loader "$PWD/tools/McOverlayNativeLoader.exe" --pack "$PWD/agent/mappings/default-v1.json" --out "$PWD/reference.json"
.\tools\MappingAnalyzer.exe validate --pack "$PWD/agent/mappings/default-v1.json" --snapshot "$PWD/reference.json" --out "$PWD/validation.json"
.\tools\MappingAnalyzer.exe inspect --snapshot "$PWD/reference.json" --out "$PWD/normalized.json"
.\tools\MappingAnalyzer.exe resolve --pack "$PWD/agent/mappings/default-v1.json" --reference "$PWD/reference.json" --snapshot "$PWD/new-runtime.json" --out "$PWD/candidate.json" --write-pack "$PWD/new-pack.json"
.\tools\MappingAnalyzer.exe diff --before "$PWD/agent/mappings/default-v1.json" --after "$PWD/new-pack.json" --out "$PWD/diff.json"
```

Capture new-runtime.json from the target JVM using inspect. A reference must come
from a runtime where the supplied pack validates. For Java 8 add --tools-jar with
that JDK's lib/tools.jar. Helper JAR, probe DLL and contracts default beside the tool.
All commands emit eventVersion 1 JSONL. validate without a snapshot checks syntax
only and reports injectionReady=false. Exit 3 is failed live validation; exit 4 is
incomplete resolution. --write-pack is written only after complete validation.

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
A real Lunar client was unavailable during development; private JVM results do not
establish compatibility with every Lunar version. Unsupported runtimes fail closed.

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

The candidate-class selection and inspect-detail integration are a separate next
stage, gated on successful repeatable real Lunar lite capture. Existing full inspect
is retained for compatibility while that gate is pending; this diagnostic change
must not be presented as completion of the two-stage pre-injection pipeline.
