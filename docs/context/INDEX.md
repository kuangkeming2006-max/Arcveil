# 渐进式上下文入口

2026-09-26 已完成五个大型文件的机械拆分并逐阶段构建/测试。以下“模块”是阅读与职责边界，不表示已经建立独立库。

## 基线与路径

- 分析基线：2026-09-25，Git `5877207`；拆分完成：2026-09-26。
- Git 根目录是当前仓库；实际 CMake 工程位于 [Mc_Injector-master](../../Mc_Injector-master/CMakeLists.txt)。本文档集放在仓库根目录的 `docs/context/`。
- 本文档中的简写 `P/` 表示 `Mc_Injector-master/`；符号名比行号更稳定，历史行号只用于计划对照；当前导航以模块页的文件族和符号为准。
- 工作区原有未跟踪文件 `P/agent/bindings/NativeHotbarBridge_v49.java`、`P/tests/V48_VALIDATION.md`、`V49_VALIDATION.md`、`V50_VALIDATION.md` 不作为已纳入构建或测试通过的证据。
- 模块边界由原分析和拆后代码核对；验证范围及局限见各模块页和 REFACTOR_FOLLOWUPS.md。

## 阅读顺序

1. 先用下表选择一个模块，只读它的模块文档。
2. 需要跨模块修改时，查 [DEPENDENCIES.md](DEPENDENCIES.md) 中对应方向和接口；不要因为 include 可达就默认读取整条依赖链。
3. 先读入口 header / protocol / QML property / signal；只对当前问题涉及的符号查看实现附近。
4. 需要拆分五个大文件时才读 [REFACTOR_PLAN.md](REFACTOR_PLAN.md) 对应章节。
5. 只有接口不足以判断职责、线程或生命周期时，才读取依赖模块的特定实现区段；记录读取理由。不要整文件输出、不复制函数实现进文档。

## 按任务路由

| 问题 / 改动 | 先读 | 最小代码入口 |
| --- | --- | --- |
| 进程扫描、附加/分离、会话状态、配置保存 | [controller](modules/controller.md) | `P/src/OverlayManager.h`、`ProcessScanner.h` |
| 页面、主题、导航、弹窗、Qt 属性绑定 | [qml-ui](modules/qml-ui.md) | `P/qml/main.qml` 的路由/目标页面、`P/src/main.cpp` 的注册区 |
| Agent 启停、线程、帧调度、设置桥接 | [agent-runtime](modules/agent-runtime.md) | `P/agent/src/AgentRuntime.h` |
| 游戏映射、快照、床扫描、游戏逻辑 | [game-bindings](modules/game-bindings.md) | `P/agent/bindings/GameBindings.h`、`MappingProvider.h` |
| 游戏内 ImGui、HUD、输入、IME、OpenGL 资源 | [renderer](modules/renderer.md) | `P/agent/overlay_renderer.h` |
| JVM 导出、线程附着、Win32/OpenGL detour、Java 变换 | [jvm-hooks](modules/jvm-hooks.md) | `P/agent/jvm.h`、`opengl_hook.h`、`wndproc_hook.h`、对应 `Live*Transform.h` |
| 消息、握手、序列化、同步、断线 | [ipc](modules/ipc.md) | `ControlProtocol.h` + 两端解析/发送符号 |
| 构建、打包、测试入口 | 本页下方，然后目标模块“测试” | 工程、agent、tests 三处 `CMakeLists.txt` |

## 进程与构建边界

`MinecraftOverlayManager`（输出名 `Arcveil`）拥有 Qt/QML、网络服务、设置与命名管道服务端。
`McOverlayAgent` DLL 在目标 JVM 进程内运行，拥有 JNI/JVMTI、游戏快照、ImGui、OpenGL 和 Win32 输入钩子。
两者通过文本 IPC 同步，不共享 C++ 对象或 Qt 实例。

| 构建目标 | 作用与约束 |
| --- | --- |
| `MinecraftOverlayManager` | Qt6 Core/Gui/Qml/Quick/QuickControls2/Quick3D/Network/Concurrent；C++20 |
| `McOverlayAgent` | Windows x64；JNI/JVMTI 仅头文件，不链接 `jvm.lib`；MinHook、ImGui OpenGL2 |
| `mc_overlay_attach_helper` | 编译 Java AttachHelper JAR，调用 `VirtualMachine.loadAgentPath` |
| `McOverlayNativeLoader` | 独立可执行文件，普通 DLL 加载后调用 `McOverlay_Start` |
| `mc_windows_media_bridge` | Windows C# 媒体辅助进程；由控制器媒体服务使用 |
| `mc_overlay_runtime_files` | 把 DLL 和两个加载工具复制到控制器旁的 `agent/`、`tools/` |
| `mc_agent_assets` | 嵌入资源 OBJECT 目标，Agent 和渲染测试都使用 |

根工程明确列出 QML resource 文件；不是自动收集目录。现有 `qml/OverlaySurface.qml` 不在该清单中，不能仅凭文件存在认定它是正在运行的渲染入口。

## 当前需要记住的结构事实

- 控制器 QML 和 Agent ImGui 是两套 UI。两者都能改变设置，通过 IPC 回传；没有 QML → Agent renderer 的直接调用。
- `AgentRuntime` 是进程内协调者；OpenGL 帧回调、控制线程、解析线程、床扫描线程和 telemetry 线程职责不同。
- `overlay_renderer.h` 包含 `GameBindings.h`，主要为快照类型；这会间接带入 binding 内部的 transform 头文件，是现有 include 耦合。
- 控制器直接 include `agent/bindings/SmartHotbarPolicy.h` 的纯值校验接口，是跨目录例外，不代表控制器可以操作 JNI binding。
- 协议 header 只列基础协议，扩展消息以两端代码为事实来源。
- 测试既有行为测试，也有读源码文本的规则测试；后者对文件搬迁敏感。

## 验证与维护

本次实际完成 Agent/控制器构建、现有可执行测试、私有 JVM smoke 与 UI/WGL 截图检查；未在真实 Minecraft 客户端验证。
后续代码改动按模块文档选择测试；测试目标通常 `EXCLUDE_FROM_ALL`，目前没有 `add_test` 注册，不能把普通构建或空的 `ctest` 当作测试已通过。

更新规则：接口变化更新模块文档和依赖表；文件迁移更新机械拆分表与测试源码路径；消息变化同时核对两端；QML 提取更新资源清单与对象契约。文档保留导航和约束，具体实现始终以源码为准。


完成记录见 [REFACTOR_PLAN](REFACTOR_PLAN.md)，后续架构议题见 [REFACTOR_FOLLOWUPS](REFACTOR_FOLLOWUPS.md)。
