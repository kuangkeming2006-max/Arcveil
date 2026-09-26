# controller

返回 [INDEX](../INDEX.md)；跨界先查 [DEPENDENCIES](../DEPENDENCIES.md)。`P/` = `Mc_Injector-master/`。

## 边界与最小入口

运行在控制器进程，构建目标 `MinecraftOverlayManager`，输出 Arcveil。
负责进程模型、加载/分离会话、管道服务端、设置持久化、网络/皮肤/黑名单/系统媒体服务，以及把这些服务注册给 QML。
不拥有目标 JVM 的 JNI 对象或 OpenGL 资源。

先读 [OverlayManager.h](../../../Mc_Injector-master/src/OverlayManager.h)、
[ProcessScanner.h](../../../Mc_Injector-master/src/ProcessScanner.h)；
服务连线只读 [main.cpp](../../../Mc_Injector-master/src/main.cpp) 的服务初始化/注册区域。
启动问题另读 StartupLoader.h、ApplicationLifecycle.h。

## 文件职责

| 文件族 | 职责 / 对外契约 |
| --- | --- |
| OverlayManager | QObject 会话状态机；attach/detach；feature/config；QLocalServer/socket；协议消息与属性/信号适配 |
| ProcessScanner | QAbstractListModel、扫描结果、selectedPid、refreshing、scanProgress；异步扫描 |
| StartupLoader / ApplicationLifecycle | splash/main 加载、startupReady、退出与记录 |
| AppSettings / ApiKeyStore | UI 设置、API key 保存与配置变化通知 |
| HypixelApiClient / PlayerStatsService | 手工玩家查询与发现玩家队列查询；网络结果交给 OverlayManager |
| BlacklistService | 黑名单持久化、身份观测、与 Agent 同步 |
| SkinProfileService / SkinCuboidGeometry / SkinImage | 皮肤数据与 Qt Quick3D 几何/图像处理 |
| HotkeyCaptureService | Qt/native 事件过滤的一次性按键捕获 |
| MediaSessionService / media-helper | 媒体状态、频谱、上一首/下一首/播放切换 |

## Qt/QML 和服务调用

`main.cpp` 注册 `McOverlay 1.0` 的 ProcessScanner、OverlayManager、HotkeyCapture、HypixelApi、ApiKeys、AppSettings、SkinProfile、Blacklist；
SkinCuboidGeometry 为可实例化 QML 类型。Lifecycle 是 context property，PlayerStatsService 与 MediaSessionService 不在该 singleton 注册列表中。

典型链：QML attachToProcess → OverlayManager 启动 helper → HELLO 验证 → WaitingForOpenGL/Active 通知 UI。
Agent PLAYER_FOUND → playerFound → PlayerStatsService.enqueuePlayer → statsReady → publishPlayerStats → STATS。
Agent 黑名单动作 → blacklist* 信号 → BlacklistService → commandReady → sendBlacklistCommand。
这些服务由 composition root 的 signal/slot 连接，不是 OverlayManager include 每个网络服务实现。

## OverlayManager 实现文件导航（2026-09-26 已拆分）

目录 `P/src/`；QObject 类、Q_PROPERTY、signals/slots、测试 friend 均不变。

| 文件 | 职责 |
| --- | --- |
| OverlayManager.cpp | 构造/析构、会话 attach/detach、失败/状态、监控、关闭 transport |
| OverlayManagerProcess.cpp | 路径/JVM/进程检查、native loader fallback、ScopedHandle/window helper |
| OverlayManagerSettings.cpp | 所有 feature setters、完整 BOOL_SETTER 宏族、textGuiModules |
| OverlayManagerConfig.cpp | 配置与 QSettings load/store/flush；唯一 validConfigName 实现 |
| OverlayManagerTransport.cpp | accept/read/write、读取预算与 queued continuation |
| OverlayManagerProtocol.cpp | processAgentLine、快照/绑定/缩放/床重扫命令 |
| OverlayManagerServices.cpp | Hypixel、stats、blacklist、media 编码发送 |
| OverlayManagerGameState.cpp | resetGameState/refreshGameStateFreshness |
| OverlayManagerCodec.internal.h/.cpp | overlay_detail 的颜色、UTF-8、协议 token、Hotbar 校验；头同时声明供构造函数使用的配置名校验 |

主程序与 ControllerResponsivenessTests 同时编译全部实现；OverlayManager.h 仍由 AUTOMOC 处理。

## 生命周期约束

保留 QObject 线程亲和性和 signal 次数/顺序；不能把异步 detach/attach 改为阻塞等待。
readAgentMessages 有每轮消息数/字节数/时间预算和后续调度，避免 Qt 事件循环被大量 telemetry 占满。
GAME_STATE 有会话序列及 stale 判断；重连不能延用旧会话认证状态。
feature 属性、持久化字段与 Agent feature 位/位置字段是重复表示，改一边时必须按 IPC 文档核对另一边。

现有 include 例外是 OverlayManagerCodec.internal.cpp → SmartHotbarPolicy.h 的 validPacked 纯函数；不意味着控制器可以读 BindingCache 或调用 JNI。

## 测试与按需下钻

- `McOverlayControllerResponsivenessTests` 直接编译 OverlayManager 文件族、ProcessScanner.cpp；保持 friend 和新增 cpp 列表同步。
- `McOverlayHotkeyCaptureTests`；`MC_OVERLAY_UI_TESTS=ON` 的 controller smoke。
- 工具/Agent 三种 smoke 是启动链邻接验证，脚本不使用生产 OverlayManager 实现全部控制流程。
- 全设置/协议版本 round-trip 是覆盖缺口，不宣称已测试。
- 普通 UI 任务停在 QObject public 声明；只有消息/生命周期变更才读对应 cpp 区段。网络 API、密钥存储和辅助加载器内部不自动纳入上下文。


2026-09-26：控制器构建、响应性、HotkeyCapture 和 UI 点击/弹窗/主题 smoke 通过；116 个原成员函数体唯一且保持不变。
