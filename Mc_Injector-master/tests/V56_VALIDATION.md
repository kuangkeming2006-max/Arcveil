# v56 Windows GUI patch validation

Validated on Windows x64 on 2026-10-03, starting from `7b4744f`.

## Changes

- Applied all 33 files from `Arcveil-Windows-GUI.patch`. Merged the renderer
  documentation manually to retain the existing v54 IME validation record.
- Added six category tabs, a scoped feature sidebar, shared animated widgets,
  and independently persisted GUI font size/weight with two-way IPC.
- Set the About page and README internal build number to v56, updated its UI
  smoke expectation, and added live Agent typography ACK checks to the private
  JVM/WGL smoke script.

Input patch SHA256:
`F407AA83678A2A148D1D8DD2376710CFB7E323EDB018FBE48DD32C996EE2F97A`.

## Results

| Validation | Result |
| --- | --- |
| Release controller, Agent and loading tools | Build passed |
| Debug controller and renderer | Build passed |
| Release renderer/WGL regression | 4,376 checks, 0 failures |
| Debug renderer/WGL regression (assertions enabled) | 4,376 checks, 0 failures |
| Controller responsiveness/config/protocol tests | Passed, including typography config restore and invalid-message rejection |
| Navigation/trajectory tests | 43,271 checks, 0 failures |
| Regression policy tests | 201 checks, 0 failures |
| Private JVM + WGL, unified threads | Passed: rendering, input, typography ACKs, detach |
| Private JVM + WGL, split threads | Passed: rendering, typography ACKs, detach |
| Full controller UI smoke | Exit 0; route/theme/dialog/refresh/About checks passed |
| Extracted Release package startup | Exit 0 with Qt/JDK development paths removed from the environment |
| ZIP extraction integrity | All 1,483 packaged files matched staging SHA256 hashes |

Renderer screenshots were generated in `render-v56-release/` and
`render-v56-debug/`. The light/dark primary GUI screenshots were inspected.
Controller screenshots are in `render-v56-controller/`.

Build environment: Qt 6.10.1, MinGW 13.1, CMake/Ninja, Microsoft OpenJDK
21.0.10, NVIDIA OpenGL 4.6. Existing third-party conversion warnings remain;
there were no compilation errors. Tests were explicitly executed because the
project's test executables are excluded from normal builds.

## Test package

- Archive: repository root `Arcveil_v56.zip` (66,344,900 bytes).
- Extracted runnable entry point: `Arcveil_v56/Arcveil.exe`.
- Includes Qt/QML plugins, MinGW libraries, Agent DLL, AttachHelper JAR,
  NativeLoader, WindowsMediaBridge, licenses and a trimmed Java runtime
  containing `java.base`, `jdk.attach`, and `jdk.internal.jvmstat`.
- Archive SHA256: `7D6DD0825CDA2AB55A98FC05304F2FE9F9C8C0FB6EFA50F5ED14BB656386D944`.

The private JVM smoke used the packaged Java runtime and Agent DLL. Real
Minecraft behavior and the previously unresolved live Chinese IME candidate
integration were not verified in this run.
