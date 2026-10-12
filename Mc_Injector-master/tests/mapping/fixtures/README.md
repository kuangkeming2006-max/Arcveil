`lunar-1.8.9-forge-required-metadata.json.gz` records the actual required binding
and complete hierarchy metadata from the 2026-10-10 Lunar Client 1.8.9 with Forge
capture that selected 402 of 19,611 indexed classes. Its source snapshot SHA-256
is `0858951e82706a910a5fba7b90cbd56bdec9bce4d81a520af901817d5d7c5f3b`.

The source selected detail contains all 55 mapped class names from the verified
reference. This fixture retains the 70 classes needed by required bindings and
their hierarchy, including 548 fields, 3,124 methods and 62 interface edges.
Exact names, descriptors, access flags and defining-loader relationships remain;
the runtime PID, request, paths and loader instance identity are removed.

Bytecodes and constant pools are deliberately omitted. `Test-LunarForge.py`
constructs an offline test envelope with empty content placeholders to exercise
the production authored member validator. It cannot establish structural
resolver acceptance, installed-content equality, or successful live injection.
The original complete snapshots remain local diagnostic artifacts.

Run `python Mc_Injector-master/tests/mapping/Test-LunarForge.py BUILD_DIRECTORY`
with Qt and the compiler runtime on `PATH`.
