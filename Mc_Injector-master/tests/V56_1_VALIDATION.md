# v56.1 main integration validation

Validated on Windows x64 on 2026-10-04.

## Integration

- Merge `ed89a2a` (v56 Windows GUI, including the `7b4744f` v54 fixes) into
  main `cdc3b27` (v55.9 dynamic mapping service).
- Keep the mapping preflight/progress lifecycle, GUI typography IPC and gameplay
  fixes. No feature implementation was discarded to resolve conflicts.
- Resolve About/README/UI-smoke version conflicts as internal build v56.1.
- Retain both mapping/media and IME test targets in `tests/CMakeLists.txt`.
- Retain both mapping and v54 behavior notes in the dependency/binding documents.
- Existing uncommitted `AGENTS.md` edits and unrelated untracked files are outside
  the merge commit.

## Results

| Validation | Result |
| --- | --- |
| Release controller, Agent, mapping tools and selected tests | Build passed |
| Debug controller with complete UI smoke enabled | Build passed |
| WGL renderer regression | 4,376 checks, 0 failures |
| Controller responsiveness/config/protocol tests | Passed |
| Aim-control policies | 70,321 checks, 0 failures |
| Regression policies | 201 checks, 0 failures |
| Mapping-pack parity | 44 checks, 0 failures |
| Mapping boundary/cache tests | 22 checks, 0 failures |
| Mapping progress model/controller | 24 checks, 0 failures |
| Dynamic mapping service | 69 checks, 0 failures |
| Logical pipeline private JVM (`-Xcheck:jni`) | 156 checks, 0 failures |
| Complete controller UI smoke, including mapping windows | Exit 0 in the dependency-bundled test directory |
| Private JVM/WGL unified and split modes | Passed, including typography ACKs and detach |
| Extracted Release package startup | Exit 0 with development Qt/JDK paths removed |
| Packaged `tools/MappingAnalyzer.exe --help` | Exit 0 with system-only PATH |

The first attempt to start the Debug UI directly from the build directory lacked
a runtime dependency. The complete UI test was subsequently run successfully
from a fresh dependency-bundled directory. MappingAnalyzer receives its own Qt
Core and MinGW libraries under `tools/`, so it also starts independently of the
controller's current working directory.

## Distribution

- ZIP: repository root `Arcveil_v56.1.zip`, 72,639,731 bytes.
- Extracted entry point: `Arcveil_v56.1/Arcveil.exe`.
- ZIP SHA256: `C9D24F792AA65796EF44C894B586B416B66E13356353415F0BE8A211D28FD8EF`.
- Includes Qt/QML plugins, MinGW libraries, Agent DLL, v1/v2 mapping packs,
  MappingAnalyzer, MappingProbe-v3, contract schema, AttachHelper, NativeLoader,
  WindowsMediaBridge, Java 21.0.10 Attach runtime, documentation and licenses.

Real Minecraft/Lunar gameplay and live Chinese IME candidate integration were
not revalidated in this integration run. Historical validation records remain.
