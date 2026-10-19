# v56.9.4 — Smart Hotbar priority lists

## Changes

- Adds Ender pearl, Bow and TNT alongside Sword, Blocks, Shears, Pickaxe and Axe.
- Every Minecraft hotbar key has an ordered list of up to eight unique categories.
  The first available category wins, even when it is in the main inventory and a
  lower-priority category is already held. Within a category, held/hotbar/inventory
  preference remains. An empty list or no matching inventory item preserves the
  normal slot fallback.
- Per-key + menu, category editing, up/down sorting and removal; animated card
  height and rank transitions, shared hover/press/popup motion, reduced-motion
  support and stacked controls in narrow windows.
- All four legacy pressure plate IDs (70,72,147,148) are excluded from Blocks and
  automatic block refill. TNT is a separate category, also excluded from refill.
- v3 settings tokens roundtrip across Controller/Agent and QSettings, retaining
  v1/v2 migration and existing sprint/name/refill preferences. Ship both binaries
  together. No Java bridge or mapping symbol changes.

## Validation (2026-10-19)

- Release application and Agent built with Qt 6.10.1 / MinGW 13.1.
- McOverlayAimControlTests: 70,346 checks, 0 failures. Includes all-eight-category
  roundtrip on all nine keys, v1/v2 migration, malformed/duplicate/overflow rejection,
  priority-before-location, missing-item fallback and pressure plate classification.
- McOverlayRegressionPolicyTests: 201 checks, 0 failures.
- McOverlayControllerResponsivenessTests: passed. Includes authenticated v3 change
  handling, persisted token reload, invalid-list rejection, legacy receipt behavior
  and bounded transport/responsive scanning. Five-second scan: 10 polls / 252 UI
  heartbeats (one measured run).
- McOverlayRendererTests: 5,889 checks, 0 failures on NVIDIA OpenGL 4.6. Includes
  dark/light, four GUI scales, compact layouts and multi-item priority preview.
  Dark/light priority screenshots were visually inspected.
- Packaged Arcveil --smoke-test exits 0 with developer tool directories removed
  from PATH. Qt, compiler runtimes and Java Attach runtime are bundled.
- git diff --check passed.

## Actual limits and manual acceptance

No real Minecraft gameplay was exercised in this change. Rendering fixtures do
not prove real hotbar callback/mapped inventory behavior. Modern 1.20.1 gameplay
inventory support is still unavailable, as before.

In a supported legacy client, bind Pearl > Bow > TNT to a key using +, move the
priorities with up/down, and verify the chosen item as higher-priority stacks
are removed. Try a high-priority stack in the main inventory while holding a
lower-priority item. Remove all entries to restore vanilla selection. Restart
Arcveil to check persistence. Verify stone/wood/heavy/light pressure plates never
satisfy Blocks/refill, while ordinary building blocks do. Verify rebinding the
Minecraft hotbar key and depletion refill still work. Inventory transfers retain
the existing action-release and neutral-movement boundaries.
