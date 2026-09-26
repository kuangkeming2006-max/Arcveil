# Versioned mapping packs — v55.1

`packs/default-v1.json` migrates the four dictionaries from main `fd480a8`.
The Agent loads `mappings/default-v1.json` beside its own DLL, never from the
working directory. Distribution and install targets copy this data alongside
the Agent. Missing, malformed, duplicate or unsupported packs fail closed.

`schemaVersion` versions the stable logical keys in `MappingSymbols.inc`;
`packVersion` versions mapping data. Provider priority, family, detection and
ordered dictionary/alias lists retain their original semantics. Every schema
key must be present; optional values are explicitly empty. Packs are limited to
2 MiB, 32 providers and 16 dictionaries per provider. Unknown keys are rejected.
Actual names and descriptors belong in packs, not Gameplay C++.

The loader parses and validates the complete pack before registration.
`MappingRegistry` and `GameBindings::registerMappingDictionary()` remain the
registration boundary. `resolve()` freezes the registry once and never reloads
it. Pack validation in this phase checks schema and descriptors, not the
existence or semantics of live JVM members.

## Verification

Build and execute `McOverlayMappingProviderTests` and
`McOverlayMappingPackTests`; ordinary CTest is not wired to these targets.
`tests/mapping/legacy-parity.json` contains frozen canonical FNV-1a-64 digests
from the unmodified C++ dictionaries on `fd480a8`, covering all 247 original
fields per dictionary, metadata and detection order. These digests are regression
oracles, not security hashes. Ten additional fields replace resolver-local
aliases and namespace-dependent descriptors. No automatic mapper is included
in this commit.
