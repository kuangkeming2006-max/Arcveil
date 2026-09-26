# 模块依赖与上下文边界

先读 [INDEX](INDEX.md)。`P/` 表示工程目录 `Mc_Injector-master/`。

## 如何理解箭头与“允许读取内部”

`A → B` 表示 A 使用 B 的接口。回调箭头单独列出，不等同于新增静态 include。
“public interface”在本文中指模块消费契约，不仅指 CMake 的 PUBLIC include 或 C++ 的 public 关键字。

默认只读 B 的入口声明和模块文档。表中“否”表示处理 A 的普通任务不需要读 B 内部；若本轮任务明确涉及 B、接口与实现不一致、或生命周期问题无法由声明回答，可以定点读 B 的相关符号并说明原因。它不是文件访问权限禁令。不能将文件名可见、头文件中有 inline 实现或传递 include 当成扩大上下文的理由。

## 跨进程、UI 与构建依赖

| A → B | 为什么需要 | 使用的 public interface | 具体入口 / 证据 | 允许 A 读取 B 内部？ |
| --- | --- | --- | --- | --- |
| qml-ui → controller 会话 | 启停、显示状态、编辑设置 | `OverlayManager` singleton；`attachToProcess/detach/refreshBedCache/saveConfig/applyConfig/removeConfig`；`state/attached/busy/targetPid/gameState*/feature properties` | `P/src/OverlayManager.h` 的 Q_PROPERTY、Q_INVOKABLE、slots、NOTIFY；`P/src/main.cpp` 注册 `McOverlay 1.0` | 否；先读元对象声明 |
| qml-ui → controller 进程模型 | 扫描、选择、卡片显示 | `ProcessScanner` 的 model roles、`selectedPid/refreshing/scanProgress`，`refresh/selectProcess/processForPid` | `P/src/ProcessScanner.h`；`qml/ProcessCard.qml` 的 `selectRequested/attachRequested` | 否 |
| qml-ui → controller 服务 | 主题、窗口设置、API 查询、皮肤、黑名单、热键捕获 | `AppSettings`、`ApiKeys`、`HypixelApi`、`SkinProfile`、`Blacklist`、`HotkeyCapture`；`SkinCuboidGeometry` | `P/src/main.cpp:148–164` 的注册；同名服务 header（ApiKeys 对应 ApiKeyStore） | 否；按实际属性只读一个服务 header |
| controller → qml-ui | 启动窗口、标记启动完成 | `qrc:/qml/Startup.qml`、`qrc:/qml/main.qml`；根对象 `startupReady`，splash 的 `phaseText/completed/failed` | `P/src/StartupLoader.cpp::loadMain/showMain/fail`；QML 根属性 | 否；启动任务可读根对象绑定区 |
| qml-ui → controller lifecycle | 退出和生命周期事件记录 | context property `Lifecycle` 的 `requestExit/record` | `P/src/ApplicationLifecycle.h`、`main.cpp` 的 `setContextProperty` | 否 |
| controller → ipc → agent-runtime | 控制注入后的会话和同步设置/网络结果 | 换行文本命令 `STATE/DETACH/FEATURE_STATE_V3` 等；并非 DLL 函数调用 | `OverlayManager::writeAgentCommand/send*Snapshot` → `IpcClient::run` 的 LineHandler → `AgentRuntime::handleControlLine` | 普通任务否；修改消息时允许对读两端对应分支 |
| agent-runtime → ipc → controller | 上报 readiness、快照、Agent UI 修改、查询/媒体动作 | `HELLO/HOOK_READY/RENDERER_READY/GAME_STATE/*_CHANGED` 等 | `AgentRuntime::sendHandshake/telemetryMain` → `IpcClient::sendLine` → `OverlayManager::readAgentMessages/processAgentLine` | 同上；完整消息族见 [ipc](modules/ipc.md) |
| controller → attach-helper | 常规 JVM Attach 启动 | CLI 参数 PID、DLL、options；退出码/标准错误；`loadAgentPath` | `OverlayManager::attachToProcess/handleAttachFinished`；`P/attach-helper/src/com/mcoverlay/attach/AttachHelper.java` | 否；仅启动失败诊断读 CLI/错误分支 |
| controller → native-loader | Attach 可恢复失败的后备路径 | CLI PID、DLL、options；退出码 | `OverlayManager::startNativeLoaderFallback`；`P/native-loader/main.cpp` | 否 |
| attach-helper / native-loader → jvm-hooks | 进入目标 JVM 内 Agent | `Agent_OnAttach` / `McOverlay_Start`；正常加载也支持 `Agent_OnLoad` | `P/agent/McOverlayAgent.def` 与 `jvm.cpp` 的 C 导出 | 否；只依赖导出与 options |
| controller 构建 → Agent/工具构建 | 保证可部署布局 | CMake target 依赖和文件输出，不是链接 Agent ABI | 工程 `CMakeLists.txt::mc_overlay_runtime_files`；`agent/McOverlayAgent.dll`、`tools/McOverlayAttachHelper.jar`、`tools/McOverlayNativeLoader.exe` | 否；读 CMake 即可 |
| MediaSessionService → media-helper | 系统媒体状态、频谱和播放动作 | 辅助进程命令/输出；最终桥接 `stateChanged/spectrumChanged`、`previous/next/togglePlayback` | `P/src/MediaSessionService.h/.cpp`、`P/media-helper/WindowsMediaBridge.cs` | 否；仅媒体进程协议问题定点读双方 |

## Agent 内部依赖

| A → B | 为什么需要 | 使用的 public interface | 具体 header / callback | 允许 A 读取 B 内部？ |
| --- | --- | --- | --- | --- |
| jvm-hooks bootstrap → agent-runtime | JVM 导出委托运行时启停 | `AgentRuntime::start(JavaVM*, options)`、`stop(vmUnloading)` | `jvm.cpp` include `src/AgentRuntime.h` | 否 |
| agent-runtime → jvm-hooks JVM 适配 | 获取 VM/JVMTI、线程局部 JNIEnv | `jvm::resolveJavaVm/resolveJvmti/ScopedThreadEnv` | `P/agent/jvm.h` | 否；线程附着/卸载故障可定点查实现 |
| agent-runtime → jvm-hooks OpenGL | 安装帧回调并有序停止 | `OpenGlHook::install/disable/waitForIdle/remove`、`FrameCallback` | `P/agent/opengl_hook.h` | 否 |
| jvm-hooks OpenGL → agent-runtime | SwapBuffers 提交前通知 | 注册的 `frameEntry(void*, HDC)` → `beforeSwapBuffers` | FrameCallback；hook 只持 context 指针，未 include AgentRuntime | 否；不得根据 context 读取 runtime 私有成员 |
| agent-runtime → game-bindings | 解析、取样、输入协调、游戏逻辑、释放 | `runResolver/runBedScanner/sample/sampleCamera/snapshot/updateGameplay/sampleBow`；`setInputCaptured/maintainInputReleased`；`release/abandon` 等 | `P/agent/bindings/GameBindings.h` 的 public 方法与 `GameSnapshot/GameplaySettings` | 否；不得读取 BindingCache |
| agent-runtime → renderer | 传快照、画帧、消费 UI 动作 | `OverlayRenderer::render`、`set*Snapshot/setFeatureSettings`、`consume*`、`shieldAttacker`、清理接口 | `P/agent/overlay_renderer.h`；runtime header 也直接包含该头 | 否 |
| renderer → game-bindings 数据契约 | 投影实体、床、轨迹、玩家状态 | `GameSnapshot/WorldCameraSnapshot/EntityMarker/BedMarker/PlayerIdentity` | `overlay_renderer.h` include `bindings/GameBindings.h` | 否；仅值类型，renderer 不持 GameBindings 实例、不调用 JNI |
| game-bindings → jvm-hooks 逻辑变换 | Java 方法回调到绑定逻辑 | `Live*Transform::install/ready/setEnabled/stop/abandon`（按具体类提供的接口）；`LiveInteractionObserver` 回调 | `P/agent/bindings/Live*Transform.h`、`LiveInteractionObserver.h`，经 GameBindings.h 引入 | 否；变换签名/回调时序问题才读对应头中的实现 |
| jvm-hooks 逻辑变换 → game-bindings | 在游戏输入/移动/包发布/相机边界执行绑定逻辑 | install 时提供的 owner+回调；例如 `beginLogicalMovement/consumeLogicalInteraction/serializeLogicalPacket` | `GameBindings::updateGameplay` 注册的 lambda；`Live*Transform` 的 JNI dispatch | 否；这些是 callback contract，并非把 GameBindings private 方法变成 public |
| renderer → jvm-hooks WndProc | 拦截窗口输入、恢复并排空回调 | `WndProcHook::install/restore/installed`、Handler | `P/agent/wndproc_hook.h` | 否；`procedure` 虽 public，但只为内部链比较，不是业务入口 |
| jvm-hooks WndProc → renderer | 将 Win32 事件投递到独立 input bridge | `handleWindowMessage/onWindowMessage` callback，`OverlayInputState` context | `OverlayRenderer::initialize` 安装 Handler | 否；不能在 hook 中解引用 renderer 私有状态 |
| agent-runtime → ipc transport | 连接、发消息、取消阻塞读取 | `IpcClient::run/sendLine/cancel` | `P/agent/ipc_client.h` | 否；限长和断线问题才读 readLoop |
| ipc transport → agent-runtime | 传连接、收到一行、关闭、断线事件 | EventHandler / LineHandler；handler false 结束控制循环 | `IpcClient::run` 与 `AgentRuntime::workerMain` | 否；transport 不 include runtime |
| renderer → renderer 支撑组件 | blur、IME、颜色/动效/导航、媒体键沿 | GaussianBlur、TSF helper、UiColors/UiMotion/UiPreferences/FeatureNavigation/MediaHotkeyEdges | `P/agent/gaussian_blur.h`、`tsf_candidates.h` 和对应纯值头 | 同模块可按职责查；不要顺带读取 stb_image 等第三方实现 |
| Agent 各模块 → logging/options | 诊断和启动参数 | `log::info/error` 等、`AgentOptions` | `P/agent/src/AgentLog.h`、`AgentOptions.h` | 否；本轮视作 runtime 的支撑接口 |

## 现有跨目录例外与模块内服务关系

| A → B | 原因 | 使用的接口与入口 | 内部读取策略 |
| --- | --- | --- | --- |
| controller → game-bindings 下的纯策略 | 验证 Smart Hotbar 打包值 | `mcoverlay::hotbar::validPacked`，`P/agent/bindings/SmartHotbarPolicy.h`；`OverlayManagerCodec.internal.cpp` | 允许读此纯值契约头；不允许据此扩展到 GameBindings.cpp/JNI。未来可另议 shared-contracts，机械拆分阶段不搬路径 |
| agent-runtime → 同一纯策略 | 校验、打包与投影 `SMART_HOTBAR` | 同上，`AgentRuntimeControl.cpp`、`AgentRuntimeFeatures.cpp`、`AgentRuntimeFrame.cpp` | 同上 |
| game-bindings → MappingProvider / BedWarsState / 策略 | 映射字典冻结、队伍与 sidebar 解析、轨迹/瞄准纯计算 | `MappingRegistry/MappingDictionary`，`bedwars` 函数；`AimControl/TrajectoryMath/SafeWalkPolicy/SmartHotbarPolicy/KnockbackEvidence` | 同模块，按需读对应 header；不默认读全部策略 |
| HypixelApiClient / PlayerStatsService → ApiKeyStore | 取 API key 与配置更新 | `P/src/ApiKeyStore.h`，`changed` 信号；main.cpp 连接 reloadConfiguration | 否；key 存储实现无需带入 UI/IPC 上下文 |
| controller composition → PlayerStatsService | 发现玩家、比赛状态驱动查询，再回传结果 | `playerFound→enqueuePlayer`、`matchStateChanged→setMatchActive`；`statsReady/statsFailed→publishPlayerStats/publishPlayerStatsError` | `P/src/main.cpp:79–108` 和两个 header；不读 HTTP 实现 |
| controller composition → BlacklistService | 玩家身份、会话同步和 Agent UI 修改 | `playerIdentityFound→observePlayer`、`agentSessionReady→synchronizeAgent`；`commandReady→sendBlacklistCommand`；`blacklist*Requested/*Changed` | `P/src/main.cpp:83–98`；仅信号/slot 契约，消息变更再读 codec 区 |
| controller composition → HypixelApiClient | Agent 请求查询、结果回传 | `hypixelQueryRequested`；`stateChanged/statsChanged/statusMessageChanged/errorMessageChanged` → publish lambda → `publishHypixelResult` | `P/src/main.cpp:79–129`；lambda 是组合层，不是 OverlayManager 直接依赖网络实现 |
| controller composition → MediaSessionService | 双向媒体状态/动作 | `stateChanged/spectrumChanged→publishMediaState/publishMediaSpectrum`；`mediaPreviousRequested/mediaNextRequested/mediaToggleRequested` | `P/src/main.cpp:129–140`；普通 UI 任务不读取 C# helper |

## 测试依赖例外

`McOverlayControllerResponsivenessTests` 直接编译 OverlayManager 与 ProcessScanner，并以 friend 访问会话内部；
`McOverlayRendererTests` 直接编译 renderer、WndProc、blur、TSF、AgentLog，并用 `OverlayRendererTestAccess` friend 访问 renderer。
这是受限测试接口，不应扩散到生产模块。

`RegressionPolicyTests.cpp` 读取 GameBindingsLogical/Gameplay/Hotbar/Movement/Snapshot.cpp 的函数范围与 tsf_candidates.cpp 的源码片段；
`LogicalPipelineHookTests` 直接使用 transform 头与 Java fixture；
`ControllerUiSmoke.h` 使用根属性、objectName 和真实 UI 点击。这些依赖已随本次文件迁移更新，不能仅看 C++ include 图。

## 边界结论

当前边界主要靠类接口、进程隔离和开发约定维持；Agent 的 PRIVATE include 目录覆盖 src、agent 根与 bindings，编译器并未强制上述逻辑模块隔离。
jvm bootstrap 与 runtime 有双向源码依赖，属于启停入口与 VM 适配两个方向；callback 形成的反向运行时边不能误判为相互 include。
优先保持既有接口和生命周期；建立独立库、缩小公共 header 或统一协议 schema 都属于后续架构改动，不能混入机械搬迁。


## 2026-09-26 拆后核对

跨模块依赖、IPC wire protocol、导出和 public 类接口未变。源码入口迁移如下：

- controller 管道读写 → OverlayManagerTransport.cpp；业务协议 → OverlayManagerProtocol.cpp/Services.cpp；持久化 → Config.cpp。
- runtime 控制协议 → AgentRuntimeControl.cpp；发送/格式化 → Telemetry.cpp；feature 编码 → Features.cpp；帧调用 → Frame.cpp。
- binding hook 注册仍是 GameBindingsGameplay.cpp::updateGameplay；TLS 分别唯一归 Logical.cpp/Movement.cpp。
- renderer 后端资源 → overlay_renderer_backend.cpp；输入 → _input.cpp；绘图 → _draw.cpp 与各 private render 块。内部头不供 runtime/controller 消费。
- QML 组件通过显式 host 读取根状态、信号写回；根仍连接同一套 controller singletons。未建立新的跨进程通道。
- 两个直接编译产品实现的测试目标均已同步全部对应 source；十二个新增 QML 全部入资源清单。

### v54 callback 行为边界补充

jvm-hooks 的 hotbar consumed-key callback → game-bindings：可在该按键阶段本地选择 hotbar currentItem，包同步由原版 controller 负责；背包 windowClick 仍留在 input PRE。callback 签名、Java bridge 和 IPC 均未改动。
