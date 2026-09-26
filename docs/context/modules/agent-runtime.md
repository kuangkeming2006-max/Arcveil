# agent-runtime

返回 [INDEX](../INDEX.md)；依赖见 [DEPENDENCIES](../DEPENDENCIES.md)。`P/` = `Mc_Injector-master/`。

## 责任与入口

目标 JVM 进程内的协调层，编入 McOverlayAgent。
[AgentRuntime.h](../../../Mc_Injector-master/agent/src/AgentRuntime.h) 的 public 入口只有静态 `start(JavaVM*, options)` 与 `stop(vmUnloading)`。
持有 IpcClient、OpenGlHook、OverlayRenderer、GameBindings 及其同步状态；不是 Qt 对象。

支撑接口：AgentOptions.h 解析/持有启动参数，AgentLog.h 记录诊断。jvm.cpp 负责导出与 bootstrap；runtime 负责本次会话，不把两者混为同一个线程生命周期。

## 线程和数据所有权

| 执行位置 | 函数 / 职责 | 边界 |
| --- | --- | --- |
| bootstrap / start-stop 调用者 | start/stop、g_runtimeMutex/g_runtime | 运行时实例发布、同会话判断与替换 |
| control worker | workerMain、handleControlLine | hook 安装、管道读循环、接收设置、安排关闭 |
| resolver | resolverMain → runResolver | 自己的 JNIEnv；映射发现和冻结发布 |
| bed scanner | bedScannerMain → runBedScanner | 自己的 JNIEnv；chunk/床缓存 |
| telemetry | telemetryMain | 格式化、管道发送、revision/最新快照消费 |
| 外部 SwapBuffers 线程 | frameEntry → beforeSwapBuffers | 当前 GL context；采样/游戏逻辑/画帧；定长 mailbox 发布 |
| Win32 窗口线程 | 通过 renderer/WndProc bridge | 可能不同于 SwapBuffers 线程，不能共享线程局部 JNIEnv |

m_renderLock、active callback 计数与 idle event、telemetry mailbox lock、服务 snapshot lock 解决不同问题；不能为了文件拆分合并或更换这些锁。
queueTelemetry 的发布采用非阻塞尝试获取锁；样本可以被后续最新值替代，帧回调不执行字符串格式化或 named-pipe I/O。

## 调用流

启动：jvm 导出 → AgentRuntime.start → workerMain → OpenGlHook.install → resolver/scanner/telemetry 启动 → IPC handshake。
hook 安装失败也有专门管道错误上报路径，不能因提取 worker 代码而丢失。

帧：frameEntry 的异常/回调保护 → beforeSwapBuffers → input/focus 协调 → GameBindings.sample/sampleCamera/snapshot → gameplay/updateGameplay → 各服务快照读取 → renderer.set*/render → consume* UI 动作 → queue*。
`shieldAttacker` 返回渲染 UI 选择值供 gameplay 使用；renderer 本身不执行游戏 JNI。

结束：请求停止 → 原 context 拥有者的渲染清理/确认 → hook 禁用和排空/移除 → bindings.release 或 VM 不可用时 abandon。
错误/超时可能保留惰性资源；DETACH_COMPLETE 仅在原逻辑认定清理成功时发送，不能提前发。

## 实现文件导航（2026-09-26 已拆分）

目录 `P/agent/src/`；全部字段和 public API 仍属于 AgentRuntime。

| 文件 | 责任 |
| --- | --- |
| AgentRuntime.cpp | 构造/析构、start/stop、主 worker、shutdownGraphics；唯一 g_runtimeMutex/g_runtime |
| AgentRuntimeWorkers.cpp | resolver 与 bed scanner 的启动、入口、执行、join |
| AgentRuntimeTelemetry.cpp | telemetry 线程、mailbox queue、FixedLine/percent encoding/格式化 |
| AgentRuntimeFeatures.cpp | queueFeatureChanged 和六个 pack/unpack helper 的唯一实现 |
| AgentRuntimeFeatures.internal.h | runtime_detail 内部 codec 声明，默认参数只在这里定义 |
| AgentRuntimeControl.cpp | sendHello/sendHandshake、handleControlLine、parseFlag/percentDecode |
| AgentRuntimeFrame.cpp | frameEntry/beforeSwapBuffers 及 CallbackGuard/SrwExclusiveGuard |

37 个原成员函数体保持不变；线程数量、锁、memory order、mailbox 与 IPC 字段不变。

## 验证和上下文界限

读取 GameBindings/OverlayRenderer/OpenGlHook 时默认止于 public 声明；
只有跨线程清理或 callback 时序问题才读对应实现。
AgentRuntime.h 当前直接包含 overlay_renderer.h，传递引入 binding/transform 大量声明；这不表示所有传递内容都需加载。

已有验证为 Attach、NativeLoader、OpenGL/JVM smoke（含 split threads），以及相邻 renderer/controller 测试。
没有独立全面的 runtime command codec 测试，协议版本/feature round-trip 是后续验证缺口。2026-09-26 构建、Attach、NativeLoader（含驻留重附加）、OpenGL unified/split smoke 均通过。

