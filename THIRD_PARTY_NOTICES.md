# Third-party notices

Arcveil's original source is licensed under Apache-2.0. The following
third-party components and assets retain their own licenses.

| Component | Version/use | License | License copy |
| --- | --- | --- | --- |
| Qt | Qt 6 dynamic libraries used by the desktop controller | LGPL-3.0-only or Qt commercial terms | [`third_party/qt/LICENSE.LGPLv3`](third_party/qt/LICENSE.LGPLv3), [`third_party/qt/LICENSE.GPLv3`](third_party/qt/LICENSE.GPLv3) |
| MinHook | 1.3.4, fetched at configure time and linked into the native agent | BSD-2-Clause; bundled HDE portions carry the notice in the same file | [`third_party/minhook/LICENSE.txt`](third_party/minhook/LICENSE.txt) |
| Dear ImGui | 1.92.9, fetched at configure time and linked into the native agent | MIT | [`third_party/dear-imgui/LICENSE.txt`](third_party/dear-imgui/LICENSE.txt) |
| stb_image | 2.30, vendored as `agent/stb_image.h` | MIT or public domain; Arcveil uses the MIT option for redistribution | [`third_party/stb/LICENSE.txt`](third_party/stb/LICENSE.txt) |
| Kenney Input Prompts | 1.5A keyboard-and-mouse atlas embedded in the agent | CC0-1.0 | [`third_party/kenney-input-prompts/LICENSE.txt`](third_party/kenney-input-prompts/LICENSE.txt) |

## Qt

The desktop application dynamically links replaceable Qt 6 libraries from the
Qt open-source distribution. Qt source releases are available from
<https://download.qt.io/official_releases/qt/>. Redistributed Qt binaries must
remain accompanied by the applicable corresponding Qt license material and notices.

## Microsoft Build of OpenJDK

Release packages may include a `jlink` runtime image made from Microsoft Build
of OpenJDK 21. The image is not stored in this source repository. Its original
module-specific licenses and notices remain under `runtime/legal` in any package
that contains it.

## Platform APIs and trademarks

The Windows Global System Media Transport Controls bridge uses Windows SDK and
.NET Framework APIs; it does not vendor a third-party media SDK. Minecraft,
Microsoft, Mojang Studios, Lunar Client, Hypixel, Spotify and other product names are
the property of their respective owners. Their mention identifies compatibility
only and does not imply endorsement.
