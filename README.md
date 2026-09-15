# Arcveil

Arcveil is a Windows x64 desktop controller and native in-process overlay for
Minecraft 1.8.9. The controller uses C++20, Qt 6 and QML; the native agent uses
JVMTI/JNI, Dear ImGui and the game's LWJGL 2 OpenGL context.

The project does not install a mod JAR or add one to Minecraft's classpath. It
first uses the supported JVM Attach API. When runtime attach is disabled, an
explicit Windows loader fallback can load the same ordinary DLL and invoke its
exported initialization entry point. The fallback does not manually map,
unlink or conceal the module.

> [!WARNING]
> Arcveil changes client behavior. Movement, combat and automation features may
> violate a server's rules and can result in a ban. Use them only in
> single-player, private test environments, or where you have permission. This
> project contains no anti-cheat bypass.

## Current feature set

### Controller and in-game interface

- Java process discovery, architecture validation, authenticated per-session
  IPC, structured attach errors and graceful session switching.
- A Qt Quick controller with persistent settings, light/dark appearance,
  responsive navigation and a 3D Minecraft skin preview.
- An in-game Click GUI rendered in the active Minecraft OpenGL context, so it
  survives borderless fullscreen and F11 window/context recreation.
- Animated page transitions and collapsible Aim Assist sections, stable smooth
  scrolling, searchable feature navigation, four crisp GUI scale presets,
  theme/accent controls and fullscreen IME support.
- A configurable Click GUI key plus per-feature hotkeys synchronized between
  the controller and the injected agent.

### Combat and interaction

- **Aim Assist** with separate Smooth Aim, Lock On and Silent Lock output
  modes; distance/FOV filters, nearest-target priority, optional attack
  viability checks and sequential target selection.
- **Silent Lock** activates only while the left mouse button is held. Its fixed
  1-20 CPS scheduler creates one numbered attack intent per deadline, confirms
  the required network rotation before dispatch, and uses one final entity
  attack path without catch-up bursts.
- **Silent Control Adaptation** optionally aligns movement, jump and sprint
  arbitration with the same committed logical yaw while keeping the core
  Silent Lock attack clock independent.
- **Smart Hotbar** lets each logical hotbar slot select a sword or blocks from
  the hotbar/main inventory. Triggers follow Minecraft's own remapped hotbar
  bindings, including keyboard and mouse bindings, rather than hard-coded
  number keys.
- **Bed Breaker** for local/test environments and configurable local velocity
  response controls.

### Movement

- SafeWalk based on the player's real next-tick support footprint and
  Minecraft's own sneak binding.
- Scaffold, Flight and Bunny Hop local/testing tools with visible risk warnings
  and a Hypixel safety interlock.

### Visuals, HUD and player tools

- Player ESP, Bed ESP and bed-defense material cards.
- Nametags, teammate presentation controls and visible held-item indicators.
- Fireball ESP plus bow-impact and knockback trajectory visualizations.
- FreeLook with independent camera render/input hooks and expanded terrain
  preparation so looking behind the player does not reveal unloaded-looking
  gaps caused by view-only chunk preparation.
- Bed alerts, Hypixel Bed Wars statistics, a searchable blacklist with recent
  players, configurable module list, and a Now Playing card with artwork,
  multilingual metadata, spectrum animation and media hotkeys.

## Architecture

```text
Arcveil/
├─ LICENSE
├─ NOTICE
├─ THIRD_PARTY_NOTICES.md
├─ third_party/                  dependency license copies
└─ Mc_Injector-master/
   ├─ src/                       Qt controller and services
   ├─ qml/                       desktop controller UI
   ├─ agent/                     native JVMTI/JNI + OpenGL agent
   ├─ attach-helper/             supported JVM Attach helper
   ├─ native-loader/             transparent Windows fallback loader
   ├─ media-helper/              Windows media-session bridge
   └─ tests/                     isolated native, JVM and UI regressions
```

The agent hooks `gdi32!SwapBuffers` (and the optional
`opengl32!wglSwapLayerBuffers` path) and renders through Dear ImGui's OpenGL2
backend. A generation-aware renderer owns HWND/HGLRC changes, WndProc chaining,
input capture and context rebuilds.

Minecraft mappings are resolved once on a dedicated JVM-attached worker. The
resolver supports Forge/SRG and vanilla/Lunar 1.8.9 profiles, publishes an
immutable JNI cache only after validation, and fails closed for unsupported
transformed clients. Rendering can remain available even when game bindings
are unsupported.

Gameplay telemetry uses bounded immutable snapshots. Java object references
remain local to a sample; only validated classes, method IDs and field IDs are
cached. Network work, skin downloads, media polling and named-pipe writes stay
outside the render callback.

## Requirements

- Windows 10 or 11, x64
- CMake 3.24+
- Ninja or another Windows CMake generator
- Qt 6.9+ with Core, Gui, Qml, Quick, QuickControls2, Quick3D, Network and
  Concurrent (Qt Test is used by regression targets)
- A full x64 JDK: JDK 8 with `tools.jar`, or JDK 9+ with `jdk.attach`
- An x64 Minecraft 1.8.9 JVM

The first configure can fetch the pinned MinHook and Dear ImGui sources into
the build tree. For offline/reproducible builds, set
`MC_AGENT_MINHOOK_SOURCE_DIR` and `MC_AGENT_IMGUI_SOURCE_DIR` to vetted local
checkouts and disable `MC_OVERLAY_FETCH_AGENT_DEPENDENCIES`.

## Build

From the repository root with Qt MinGW:

```powershell
cmake -S .\Mc_Injector-master -B .\build-native-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH="D:/Qt/6.10.1/mingw_64" `
  -DCMAKE_C_COMPILER="D:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe"

cmake --build .\build-native-debug --parallel
```

The build tree contains:

```text
build-native-debug/
├─ MinecraftOverlayManager.exe
├─ agent/McOverlayAgent.dll
├─ tools/McOverlayAttachHelper.jar
├─ tools/McOverlayNativeLoader.exe
└─ tools/WindowsMediaBridge.exe
```

## Hypixel API key

Enter the key for your registered Hypixel developer application in
**Settings → Hypixel API key**. Arcveil encrypts it with Windows DPAPI for the
current account and never exposes the clear text as a QML property or writes
it to logs. A non-persistent development override is also supported:

```powershell
$env:HYPIXEL_API_KEY = 'your-registered-application-key'
.\build-native-debug\MinecraftOverlayManager.exe
```

Requests use bounded responses, timeouts, rate-limit reporting and in-memory
caching. Automatic roster lookups start only for a confirmed Bed Wars match.

## Validation

The test suite includes pure state-machine tests, an isolated JVM/JVMTI
fixture, off-screen OpenGL/ImGui rendering checks, controller lifecycle and hotkey tests,
and owned attach/loader smoke targets. The smoke scripts only attach to Java
processes they start themselves.

Representative commands after building:

```powershell
.\build-native-debug\McOverlayAimControlTests.exe
.\build-native-debug\McOverlayRendererTests.exe .\render-output

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\Mc_Injector-master\tests\Run-AgentAttachSmoke.ps1 `
  -AgentDll .\build-native-debug\agent\McOverlayAgent.dll `
  -AttachHelperJar .\build-native-debug\attach-helper\McOverlayAttachHelper.jar `
  -JavaHome 'C:\Program Files\Microsoft\jdk-21.0.10.7-hotspot'
```

## Runtime notes

- Controller and target must run as the same Windows user and integrity level.
- A 32-bit or ARM64 JVM is rejected before attach.
- `-XX:+DisableAttachMechanism` prevents runtime Attach. The transparent loader
  fallback may still be blocked by process mitigation or security software;
  start-time `-agentpath:` remains the JVM-supported alternative.
- Detach disables input, rendering, hooks and IPC, but intentionally leaves the
  agent DLL resident until JVM exit to avoid unloading while callbacks run.
- Restart the target JVM before testing a newly built agent DLL.

## License

Arcveil's original source is licensed under the
[Apache License 2.0](LICENSE). Third-party components and assets retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[`third_party/`](third_party/).

Arcveil is not affiliated with Mojang Studios, Microsoft, Lunar Client,
Hypixel, Spotify, or the maintainers of the listed third-party libraries.
