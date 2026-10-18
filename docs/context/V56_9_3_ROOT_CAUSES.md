# v56.9.3 evidence review — Lunar identity and reported legacy feature regression

Investigation and fixes on 2026-10-18 (Asia/Tokyo), baseline `4a84cbe`
(v56.9.2). Original observations below are retained as historical evidence;
the subsequent sections distinguish repaired real Attach, official-artifact/JNI
checks and gameplay that remains unverified.

## Fresh Lunar failure

The latest matching real run is
`068eed5d-71ec-4e1e-9bd0-25d754046947`, PID **56528**, at approximately
17:13 JST. At inspection the process still existed as `javaw`, started 17:09:55,
using Lunar's Zulu **17.0.18** runtime. The initial read-only log review did not
attach to it; the subsequent Probe and Agent observations are recorded below.

| Recorded fact | Result |
| --- | --- |
| Build | `v56.9.2-tiered-snapshots` |
| Capture tier / actual request | Detection / 1265 bytes |
| JVM inventory / emitted classes | 33855 / 2 |
| API fields / methods / warmup | 0 / 0 / 0 |
| Serialized and JVMTI code / constant-pool bytes | All zero |
| Probe time / complete service capture stage | 78 ms / 1065 ms |
| Standard Attach | Explicitly unsupported, helper exit 10, 417 ms |
| NativeLoader | Success, 199 ms |
| Identity result | Lunar; actual version absent; `versionVerified=false` |
| Selected dictionary / namespace / runtime binding | Empty / Unknown / null |

The resulting `index-1.jsonl` is 5081 bytes. Both emitted class stubs,
`net.minecraft.client.Minecraft` and `net.minecraft.client.main.Main`, belong to
the same Lunar genesis defining-loader instance. Five loader receipts appear
because scoped Lunar/Forge class-hint evidence also registers other loaders.
Both independent environment receipts report:

```json
{
  "brand": "modified",
  "brandSource": "ClientBrandRetriever.getClientModName",
  "version": "",
  "versionSource": "",
  "diagnostic": "unsupported runtime version resource id"
}
```

The offline identity lists diagnostic 1.8.9 Lunar MCP/SRG API candidates with
`criticalApiMatched=false`, not verified mappings. The actual version remains
empty; the pack's declared 1.8.9 was correctly not promoted to runtime authority.
The service stops before Cache validation, authored validation, structural
resolution or Agent loading. This is an identity-evidence failure, not a request
size, NativeLoader or Mapping-confidence failure.

The same failure appears in earlier run
`3a9f6148-a962-451e-a90f-bad487eaaf2d`, PID 14308: 34351 loaded classes,
two captured classes, 1265-byte request, 62 ms Probe capture, then the same
unsupported identity rejection.

## What the original Snapshot could not establish

In the baseline Probe, `readRuntimeEvidence` reads both resource and legacy
sources, but checks `resource.diagnostic` first. Any nonempty resource diagnostic
prevents the result from using the already-read legacy value. The current
Snapshot therefore **does not prove** that RealmsSharedConstants is missing,
renamed, from the wrong loader or unreadable. A valid legacy value could also have
been masked by the unsupported resource id. The raw version resource id and
individual legacy read outcome were not persisted and cannot be reconstructed
from these files.

Likewise `brand="modified"` is a sanitizer placeholder for an unaudited brand
string, not the original string or a verified Lunar brand alias. The original
value is unknown. Baseline `Resolver::detectedFamily` reads brand only after a
version scope becomes independently verified. With no version it currently
identifies Lunar from separate marker/launch evidence. If the version were
restored without addressing this boundary, the placeholder would enter the
unknown-brand rejection branch and turn the family into Unknown. A version-only
fix therefore cannot by itself demonstrate Lunar compatibility.

A safe repair must distinguish a rejected/non-version resource from a genuine
conflict between trusted exact runtime sources, preserve exact loader ownership,
and retain rejection of unsupported versions and unknown Forge brand token lists.
It must not use the diagnostic Mapping candidate or cached pack version as the
missing actual version.

## Fresh Probe-v17 observation confirms both identity boundaries

A subsequent read-only Probe-v17 capture of the same live PID 56528 is saved in
`build-main-release/v56.9.3-real-lunar-detection.jsonl`, with transport events in
`.research/v56.9.3-real-lunar-detection.log`. It includes separate source attempts,
resolving the uncertainty in the original v56.9.2 record:

| Source | Observed value | Result |
| --- | --- | --- |
| Loader `version.json` id | `1.8.9-forge1.8.9-11.15.1.2318-1.8.9` | Not an exact semantic Minecraft version; unavailable as version authority |
| Same-defining-loader Realms `VERSION_STRING` | `1.8.9` | Successfully read exact runtime version |
| ClientBrandRetriever | `lunarclient:v2.23.4-2642,fml,forge` | Preserved in `brandObserved`; sanitized brand remains `modified` |

Both Minecraft and Main receipts agree in the same game defining loader. The
capture now selects the canonical Realms source, records
`unusable version.json ignored; unsupported runtime version resource id`, and
returns exact version **1.8.9**. This confirms the actual regression: a rejected
resource id had masked a valid independent legacy version. The repair must not
strip or guess a Minecraft version from that resource id.

The fresh Detection request was 1217 bytes, with 34054 loaded / two emitted
classes, 78 ms Probe time, zero API members/warmup and zero code/pool bytes.
This client did not require the bounded renamed-field candidate scan: the
canonical field was present and readable. Renamed-field recovery remains a
separately tested fallback, not an explanation substituted for this run.

The observed `v2.23.4-2642` denotes the Lunar client release, not Minecraft.
Family acceptance requires the narrowly audited release syntax plus independent
strong Lunar marker evidence in the same game loader; an unknown brand suffix,
foreign-loader marker or brand string alone cannot supply identity. At this
checkpoint the fresh capture proves version evidence only; it does not establish
full Mapping, Agent or Renderer readiness on the repaired path.

## Fresh Vanilla evidence and feature limits

Real run `2c9d9411-b544-46ba-a7c9-cca7028dab9d`, PID **50528**, at 16:46 JST
did reach **Active** on v56.9.2. It recorded one 1265-byte Detection capture
(8322 loaded / two emitted classes), followed by two 1503-byte Vanilla Validation
captures (18 requested / 27 emitted classes, 94/78 ms). The normal Agent path
recorded Agent readiness at 916 ms, Active at 2793 ms and total Attach at 9239 ms;
`TRANSACTION_COMMIT` explicitly reports authenticated Agent, exact JNI bindings and
active renderer. Automatic Resolver and selected-detail calls were both zero.

The matching persistent diagnostic
`diagnostics/freelook-20261018-164634-50528.log` records:

- `profile=Vanilla obfuscated`, `mainThread=1`, FreeLook `capability=1`;
- render/input/terrain transforms succeeded and combined readiness became true;
- foreground and GUI state changes were observed during the session.

This confirms some real JNI/game-thread/hook behavior, but it does **not** prove
Scaffold placement or Smart Hotbar consumed-key/refill behavior. No dedicated
Scaffold/Smart Hotbar log was found for that PID. Generic `AgentLog` writes only
to OutputDebugString, so these controller files cannot recover an unrecorded
capability-disable/JNI warning. The required-only Mapping Snapshot intentionally
omits optional classes; its optional `symbol-failed` rows cannot be treated as
evidence that the later Agent's full dictionary failed JNI resolution.

The successful run's `validated-pack.json` contains all 257 symbols of
`minecraft-1.8.9-vanilla-notch`, equal to the source dictionary's full symbol
object. Required-only validation did not trim optional symbol data before Agent
loading. This rules out that specific proposed regression mechanism; it does
not rule out an independent runtime capability or feature-callback failure.

The user-reported feature failure remains a real regression report requiring
targeted runtime evidence. Neither successful Attach nor the absence of a
persistent feature error proves the feature is working.

## Historical baseline: what was actually verified

| Commit / version | Evidence | Scope and limit |
| --- | --- | --- |
| `9546a77`, v56.5, 2026-10-09 | Real Lunar 1.8.9/Forge, Zulu17; cold, same-PID Cache hit and restarted-client Cache hit all reached exact JNI/profile/renderer readiness and detached | Client main menu, `no_player`; the document explicitly excludes complete gameplay acceptance |
| `83a061d`, v55.7, 2026-09-28 | Real Lunar 1.8.9 v2.22.42-2639; two actual PIDs, including a singleplayer-world Attach, SRG mapping and renderer ready | Proves those Mapping/Attach sessions; no explicit Scaffold/Smart Hotbar behavior acceptance |
| `7b4744f`, v54, 2026-09-26 | Smart Hotbar timing changes and policy/private-JVM regressions | Validation document explicitly says real Minecraft/Lunar gameplay was not tested |
| v51 validation | Automated movement/hotbar/JVM policy checks | Explicitly lists live Smart Hotbar refill while moving/neutral as remaining acceptance |

Sources are `tests/mapping/V56_5_VALIDATION.md`,
`tests/mapping/V55_7_VALIDATION.md`, `tests/V54_VALIDATION.md` and
`tests/V51_VALIDATION.md`. The v56.5 main-menu result is the most recent explicit
historical real Lunar Attach baseline located in the tracked validation records.
No tracked record reviewed here establishes a most-recent fully working real
Vanilla Scaffold/Smart Hotbar gameplay baseline. Mock, policy and synthetic JVM
checks must not be substituted for that missing evidence.


## Binding resolver and capability audit

`GameBindingsResolve.cpp::resolveProfile` obtains the defining ClassLoader of
its verified Minecraft anchor. Both its core `loadClass` lambda and optional
`loadFeatureClass` lambda call `loadWithClassLoader`, which invokes that loader's
`ClassLoader.loadClass(binaryName)` explicitly. Optional ItemStack, Item,
InventoryPlayer, ItemSword, GameSettings, KeyBinding, PlayerControllerMP,
ItemBlock, EnumFacing and Vec3 all use this path. They are not limited to the
Probe's loaded-class inventory and are not discovered with an attached native
thread's default `FindClass`. The resolver's `FindClass` calls are for Java
platform types such as ClassLoader, UUID, List, Collection and Enum.

The optional JNI methods and fields are subsequently resolved in the Agent
through its existing `GetMethodID` / `GetFieldID` calls. Removing optional Probe
warmup therefore does not by itself remove the Agent's explicit dependency
loading and lookup. It can change cold class-loading/initialization timing; that
remaining hypothesis requires an actual failed Agent lookup or callback receipt,
which the existing Vanilla records do not contain. No claim is made that all
runtime loading failures are ruled out.

A complete symbol comparison of the installed Vanilla Notch and Lunar Notch
dictionaries found no symbol differences. Selecting the confirmed Vanilla
provider therefore did not substitute different optional member names. The
source diff from main `764d4b8` to v56.9.2 leaves the legacy feature predicates,
placement sequence and hotbar guards intact; relevant changes are the borrowed
GameApi accessors and the exact VersionAdapter gate. The Legacy18 adapter still
advertises the original 63 capability bits, including inventory and gameplay.
The array descriptor/accessors preserve the 1.8.9 ItemStack[] representation.
These structural source checks alone did not identify the faulty member name.
The later official-artifact and JNI audit below identifies a concrete shared
capability failure; gameplay acceptance remains separate.

v56.9.3 adds the bounded `GameplayDiagnostics` value facade rather than guessing
a member or changing a game guard. It distinguishes published core bindings from
optional predicates actually observed during an update; unavailable dependency
groups are reported separately and may include IDs skipped by an earlier failed
lookup. Hook readiness, real consumed-key callbacks, successful mapped local
hotbar operations and placement JNI attempts/true local returns are separate
observations. None proves server acknowledgment or grants Ready. The existing
201 source-policy regressions passed against the updated source, retaining the
slot/sync/placement/restore sequence and neutral/action-release hotbar guards.

## Official 1.8.9 audit: shared Scaffold / Smart Hotbar dependency is wrong

The user confirms that both functions have no effect in singleplayer and on
servers. The full Vanilla dictionary is delivered, but its optional member
`syncCurrentPlayItem` was **`j` with descriptor `()V`**. In the official 1.8.9
client class `bda` (PlayerControllerMP), **`j()Z` exists and `j()V` does not**.
The correct sync member is **`n()V`**, SRG `func_78750_j`.

Evidence:

- The local official client JAR SHA-1 is
  `3870888a6c3d349d3771a3e9d16c9bf5e076b908`.
- The independently retained MCP joined.srg names
  `bda/n ()V -> PlayerControllerMP/func_78750_j ()V`; stable_22 methods.csv
  names `func_78750_j` as `syncCurrentPlayItem`.
- `GameBindingsResolve.cpp::resolveProfile` performs the actual `GetMethodID`
  lookup with `()V`. A null result makes placement capability unavailable;
  Smart Hotbar also depends on that same placement capability. Optional failure
  does not invalidate core Mapping/Agent Ready, explaining how Attach can succeed
  while both modules stay inactive.
- The wrong Notch name is already present in commit `7b4744f` (v54), before JSON
  pack extraction and required-only Snapshot work. There is no tracked real
  Vanilla gameplay baseline proving when the user-visible regression began.

Both Vanilla Notch and Lunar Notch dictionaries in default-v1/v2 now use `n`.
MCP/SRG names, descriptor, capability guards and operation order are unchanged.
The legacy contract records `authoredMappingAuditRevision`, invalidating old
1.8.9 Cache proof domains once; modern 1.20.1's contract remains unchanged.
MappingPackTests retain the historical golden comparison for all other rows and
explicitly assert this audited exception rather than silently rewriting history.

`Test-LegacyArtifact.py` checks all 257 symbol rows, including 193 configured
class/member checks, against the hash-pinned JAR (two intentionally empty
optional aliases are inactive). It also rejects the old/wrong descriptors.
`LegacyJniTests` passes 12 real JNI checks in an owned Java 17 JVM loading those
official classes and their local libraries: correct sync/right-click/window-click
methods resolve; historical `j()V` and incorrect `n()Z` fail. This JVM creates no
Minecraft instance, attaches no Agent and renders no game. These results prove
the binding correction, **not successful placement/refill in a real world**.

## Real Lunar: candidate namespace miss must not abort remaining mappings

The first repaired full Attach reached verified Lunar 1.8.9 identity. Its MCP
candidate failed actual metadata validation, then its Notch candidate captured
zero API classes because that namespace was absent. The baseline classified
the completed empty candidate response as a generic capture/transport error,
aborting before the valid SRG candidate.

`CaptureScopeUnavailable` now distinguishes only a **complete validation-tier**
zero-class response; Analyzer emits exit 6 / `candidate-scope-unavailable` with
PID/start identity. MappingService advances an initial authored candidate only
when that receipt matches the current target. Empty Detection, incomplete
responses, unrelated PID/start and failed final validation remain fatal. It does
not treat empty data as a mapping pass or retry another transport unnecessarily.

Two actual sessions then passed on the same Lunar/Zulu17 process using SRG:
`.research/v56.9.3-real-lunar-attach2.log` and
`.research/v56.9.3-real-lunar-observe.log`. Both recorded verified Mapping,
authenticated Agent, exact JNI, Renderer Ready/Active and normal Detach; no
Automatic Resolver, Structural capture or automatic Mapping window occurred.
The first took 19610 ms; the second 19300 ms until Active and then observed for
five seconds. The Probe-v17/DLL and SRG data in these sessions equal the final
sources; the later Notch-only correction and legacy proof-domain invalidation
were not retested in a real game session.

The read-only observation reported bindingMask=15/missingBindings=0, hookMask=0,
31 update calls, feature-disabled reason and zero actions. Scaffold and Smart
Hotbar stayed disabled, so this establishes available Lunar optional binding
predicates, not installed feature hooks or gameplay. User availability does not
permit further frame/world testing in this round. The later Controller-only
addition persists authenticated diagnostic receipts to the existing Attach
`events.jsonl`; it passed 43 service checks and Controller IPC regression, but
was not followed by another real game Attach.
