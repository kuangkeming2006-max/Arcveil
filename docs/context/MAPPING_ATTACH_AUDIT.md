# Mapping / Attach transaction + cache audit (v56.4)

Baseline: main@ea8a961. Audit recorded before implementation.

## Existing state machine, as implemented

OverlayManager owns Detached -> Validating -> StartingIpc -> LaunchingAttachHelper
-> WaitingForAgent -> WaitingForOpenGL -> Active. Explicit detach/process switch
enters Detaching, sends DETACH to an authenticated Agent, waits for DETACH_COMPLETE
(or the 2.5-second deadline), then waits for the helper exit and reaches Detached.
Failure closes transport and enters Error with the target retained for diagnostics.

MappingService owns an independent generation: prepare cancels the previous job,
initial lite -> select -> detail -> exact fingerprint cache lookup. Cache hit skips
live validate and performs another lite/select/detail; equal fingerprint finalizes.
Miss validates the authored pack; failure enters incremental resolve. Partial results
persist accepted/unresolved proofs and watch lite with 2.5--30-second backoff. Relevant
metadata changes select detail and revalidate/resolve. Required completion validates
the draft, recaptures, checks processStart/fingerprint, promotes, then emits ready.
Final drift starts another generation. Stop pauses future retries, preserving the
current job; cancel invalidates service generation and kills its Analyzer process.

ProcessScanner.selectedPid is independent model state. main.qml clearScannerSelection
calls OverlayManager.detach during injectionInProgress, but only clears scanner
selection when Active. A scanner refresh can clear a vanished selection. Closing the
MappingProgressWindow hides it; no cancellation edge exists.

## Findings

- Reusable cache is keyed by volatile detail fingerprint, including captureScope /
  liteFingerprint and defining-loader keys derived from loaded class sets.
- Lookup follows both expensive captures. Hit relies on fingerprint equality rather
  than an independently validated cached pack in the current process.
- Cache.reference reads global lastVerified; family boundaries are absent.
- Analyzer output generation is guarded inside MappingService, but ready/failed do
  not carry ownership to OverlayManager. Selection, mapping and loading lifetimes
  have no shared transaction identity.
- Pause leaves the attach pending, but there is no explicit paused transaction state.
- Standard Attach failure is rediscovered by every fresh Analyzer process.
- Stable ambiguity and absent runtime classes share the same unlimited polling loop.
- Incremental select expands the complete reference set even for a few unresolved keys.

## Required boundaries

One AttachTransaction owns pid/processStart, transactionId, generation and state from
Attach to cancellation/detach. Scanner selection is browsing only. Cache candidates
require family/version/contracts/analyzer evidence, stable structural identity and
current live validation. No historical runtime snapshot authorizes injection.
Cache promotion publishes immutable objects through a single atomic index update.
Reference preference is exact identity, family/version, family, explicit base, none.

Validation results and real-client evidence are recorded separately in
Mc_Injector-master/tests/mapping/V56_4_VALIDATION.md. Test fixtures are not evidence
of a production Lunar automatic-resolve pass.
## Implemented state machine

Idle -> Selected (Attach click creates owner) -> MappingFastPath.
Fast candidate -> scoped live validation -> MappingVerified -> LoadingAgent -> Active.
Miss/failed cached evidence -> MappingResolving; incomplete missing runtime classes
remain pending and retry; stable ambiguity or user pause -> MappingPaused.
Resume preserves the UUID and provisional results and returns to MappingResolving.
Only independent validation + final recheck can reach MappingVerified and publish.
Cancel during Mapping/Loading invalidates the owner -> Cancelling -> Detached.
Active Detach invalidates ownership, sends authenticated DETACH, drains/acknowledges
or bounds teardown -> Detached. Failure -> Failed with no injection authorization.
Switching Attach creates a new owner; it starts after cancelling/tearing down the old.
Scanner Selected/clear/refresh never represents an Attach transaction. Window hide
and presentation timers never control ownership.
