# 五个大文件的机械拆分方案

**状态：五个文件的本轮机械拆分已于 2026-09-26 完成。** 已按用户要求依次执行 GameBindings → renderer → AgentRuntime → OverlayManager → QML，并逐阶段构建/测试。下表路径均已落地；原行号为拆前历史锚点，不再用作当前源码导航，当前入口见各模块文档。GameBindings 三个巨型函数的内部提取仍是独立后续批次。renderer 完成整函数迁移及六个 render 连续块提取。`P/` 表示 `Mc_Injector-master/`。

## 机械拆分的共同规则

- 第一阶段只移动完整成员函数/辅助定义，保持类名、public 声明、ABI/导出、Qt 元对象、参数默认值、协议字段顺序及版本、资源 URL 不变。不新建服务层、不改数据模型。
- 保持原有成员字段、锁、原子操作及 memory order、thread_local 对象、回调 owner、线程创建/退出与资源清理顺序。不能用“每文件一份 static 状态”替代原共享状态。
- 原文件保留 facade/调度与生命周期；每个定义只编译一次。实现文件不能互相 include；私有共享声明放 internal header，非模板定义留一个 cpp；模板必要时放内部头。
- C++ 成员函数移到另一个 cpp 仍可访问同一类 private 成员，无需新增 public accessor 或 friend。测试现有 friend 不扩大。
- QML 拆分会改变词法作用域；必须显式传入依赖，不能指望新文件还能访问原来的 `id: app` 或兄弟 id。保持所有对象的创建时机，第一阶段不改 Loader 懒加载。
- 巨型函数内部的提取是第二阶段。每次提取一个连续职责块，显式列出输入/输出与早退传播；不把旧函数的 return 误改成只返回 helper。
- 不删除隐藏/禁用代码、不修改 defaults、不“顺便”统一重复 codec、不重命名协议消息、不更新依赖版本。

## 1. GameBindings.cpp（8,106 行）

入口：[GameBindings.h](../../Mc_Injector-master/agent/bindings/GameBindings.h)。
当前至少混合了映射解析、JNI 引用所有权、输入协调、游戏更新、快照取样、床缓存、逻辑钩子回调和本地诊断。
关键大函数：`resolveProfile` 652–2184，`updateGameplay` 2768–4380，`sample` 6416–7787。
这三个函数先整段迁移，不在同一次搬迁中改写其内部流程。

### 第一阶段：完整定义的唯一归属

所有实施路径位于 `P/agent/bindings/`。

| 目标文件 | 原符号 / 职责 | private helper 与共享状态处理 |
| --- | --- | --- |
| 保留 `GameBindings.cpp` | 构造/析构；`clearException`；`deleteGlobalRefs/release/abandon`（7788 起） | 保留 JNI/钩子清理顺序；所有新成员实现仍使用现有类字段 |
| `GameBindingsCache.internal.h` | 当前 cpp 48–281 的 `GameBindings::BindingCache` 完整定义 | 仍是 private nested type；不改 GameBindings.h 中前置声明；只有需解引用 cache 的 binding 实现包含它，不供 renderer/runtime 使用 |
| `GameBindingsResolve.cpp` | `registerMappingDictionary/runResolver/markResolverUnavailable/probeEnvironmentHints/findMinecraftClass/loadWithClassLoader/resolveProfile/resolve` | `lookupRequired` 模板、`LocalReferenceSet` 一并迁移并保留匿名命名空间；registry freeze、cache release-store 不变 |
| `GameBindingsSnapshot.cpp` | `snapshot/sampleCamera/sampleBow/sample` | sample 内 roster、队伍/世界状态、实体/床标记、轨迹采样的 lambda 和局部量整体保留；render-owned snapshot 仍由原类持有 |
| `GameBindingsInput.cpp` | `ensureLwjglMouseBindings/ensureLwjglKeyboardBindings/queryLwjglKeyDown/queryMinecraftBindingDown/queryLwjglMouseGrabbed/setLwjglMouseGrabbed/setInputCaptured/maintainInputReleased/gameScreenOpen` | LWJGL fallback 与 Minecraft focus 分支不合并；不能逐帧释放 Win32 capture |
| `GameBindingsGameplay.cpp` | `updateGameplay` 原样 | 函数内 hook 安装 lambda、功能 gating、prepare、movement/scaffold 顺序原样保留；用到的纯策略 include 随函数迁移 |
| `GameBindingsHotbar.cpp` | `onItemUse/consumeSmartHotbarPress/setHotbarMovementPaused/restoreHotbarMovement/processSmartHotbarRequests` | 私有排队/暂停成员仍留类中；这组函数按原相对顺序排列以便源码规则测试重新定位 |
| `GameBindingsImpulse.cpp` | `suppressKnownImpulse` | 真实伤害源判断及本地功能限制不改；KnockbackEvidence 状态归原类 |
| `GameBindingsBeds.cpp` | `runBedScanner/requestBedRescan`（4764–5193） | scanner 内 chunk 集合/缓存与 lambda 留在 scanner 线程；发布缓存、重扫 event 不复制 |
| `GameBindingsDiagnostics.cpp` | `enqueueDebugChatLine/enqueueWarningChatLine/publishDebugChat`（4381–4698） | 文本队列、限长、generation、JNI 本地聊天入口保持；只由既定线程实际发送聊天 |
| `GameBindingsLogical.cpp` | `silentAvailable/silentAttackAvailable/deactivateSilentOutput/refreshAttackAtPublication/serializeLogicalPacket`；`readCombatEye/readCombatBounds/traceLogicalBlock/executeLogicalInteraction/arbitrateLogicalAttack/consumeLogicalInteraction/observeLogicalCamera/observeActualInteraction/observeDigPacket` | `g_syntheticLogicalAttack` 唯一 thread_local 定义迁到此文件；重入保护与 PRE/publication 的职责不能混并 |
| `GameBindingsMovement.cpp` | `beginLogicalMovement/logicalMovementForward/endLogicalMovement/arbitrateLogicalSprint/syncSprintOwner/beginLogicalHeading/endLogicalHeading/beginLogicalJump/endLogicalJump` | `LogicalMovementHookContext/g_logicalMovementHook`、`LogicalJumpHookContext/g_logicalJumpHook` 两组类型与 TLS 整体迁移，不放进头文件生成多份 |
| `GameBindingsFreeLook.cpp` | `setFreeLookPerspective/endFreeLook/rotateFreeLookCamera/freeLookCameraAngle` | FreeLook 成员、原先恢复条件和异常清理不变 |

不迁移已经独立的 MappingProvider、BedWarsState、纯策略头、Live*Transform、MethodWeaver、Java 源码与 Bytes header。编译代码消费 Bytes header；不要在本次机械拆分顺带重新生成桥接字节码。

**保持不变的 public API：** GameBindings.h 的全部 public 声明，包括映射注册、resolver/scanner、snapshot/sample、输入接口、updateGameplay、逻辑包接口、能力查询、诊断、release/abandon；所有快照类型与容量常量。私有 BindingCache 仍不向外暴露。

### 后续函数内提取（独立批次）

- `resolveProfile`：按核心映射、可选 scoreboard/装备、移动/交互、包与相机能力分段。保留一个私有解析上下文持有 candidate、localReferences、能力标志；每段明确是否“必需失败”或“可选缺失”。现有 optional 功能不能因提取而变成整体解析失败条件。
- `updateGameplay`：先识别当前 early-return gate，再依次提取 FreeLook/输入准备、hook activation、combat preparation、movement/scaffold；不能把跨段的 JNI local reference 或上一帧状态改为临时值。
- `sample`：可按本地玩家与世界、sidebar/TAB roster、实体/装备、床合并、预测发布提取；worldGeneration/rosterGeneration 的推进与最终发布只保留一个 owner。
- 这些后续 helper 属于原类 private 方法或内部上下文；它们不是本阶段新增 public API。没有完成每个局部量的读写清单前，不宣称巨型函数已经可以安全拆成独立服务。

### CMake 与测试

在 `P/agent/CMakeLists.txt` 的 `McOverlayAgent` 源列表加入上述新 cpp 和内部头，原 GameBindings.cpp 保留。include root 与链接库不变，不新增 Qt 或 jvm.lib。

已有相关测试：
`McOverlayMappingProviderTests`（registry/schema/freeze）、
`McOverlayBedWarsStateTests`、
`McOverlayAimControlTests`（包括 SmartHotbar 策略）、
`McOverlayKnockbackTests`、
`McOverlayNavigationTrajectoryTests`、
`McOverlayLogicalPipelineHookTests`（变换+Java fixture）、
`McOverlayRegressionPolicyTests`。
它们不等于在真实 Minecraft 上完整执行 GameBindings。
OpenGL/JVM smoke 的无映射路径只能验证解析不可用时仍能上报和清理。

**必须调整的测试路径：** RegressionPolicyTests.cpp 从单个 GameBindings.cpp 字符串跨函数定位。将其输入改为迁移后各职责文件，并用函数范围作为边界；尤其 interaction、gameplay preparation/scaffold、hotbar、packet、heading/movement、offline player 判定。不能仅盲目拼接新文件制造人为顺序，也不能删除这些检查使测试“通过”。已调整测试：逐职责文件读取，再以完整函数范围检查，不拼接文件。

## 2. overlay_renderer.cpp（7,766 行）

入口：[overlay_renderer.h](../../Mc_Injector-master/agent/overlay_renderer.h)。
职责包含输入桥、IME、GL/字体/纹理生命周期、设置 mailbox、投影绘制、Click GUI、各 HUD 和 toast。
`render` 位于 3205–7575，约占文件一半以上，仅迁移其他成员函数仍会留下巨型 render。

### 第一阶段：已存在函数的迁移

目标目录为 `P/agent/`；采用 `overlay_renderer_*.cpp` 命名以便沿用目录布局。

| 新文件 / 保留文件 | 函数与职责 | private helper 归属 |
| --- | --- | --- |
| 保留 `overlay_renderer.cpp` | `render`、绘图主体和仅用于该主体的 helper | 第一阶段不改 render 的语句顺序、ImGui ID、样式栈、draw-list 提交 |
| `overlay_renderer_state.cpp` | `setFeatureSettings/consumeFeatureSettings`、`setHypixelSnapshot/setPlayerStatsSnapshot/setBlacklistSnapshot/setMediaSnapshot`、`setMediaSettings/consumeMediaSettings`、`consumeMediaAction/consumeBlacklistAction/consumeHypixelQuery`；`consumeClickGuiToggle/setMenuHotkey/consumeMenuHotkeyChange/setGuiScaleIndex/consumeGuiScaleChange/consumeBedRescanRequest/setGameScreenOpen` | 保留 dirty/ack 合并逻辑，避免旧 IPC snapshot 覆盖拖动中的布局；不改字段所有权 |
| `overlay_renderer_backend.cpp` | 构造/析构；`initialize/ownsCurrentContext/shutdownWithCurrentContext/abandonForContextChange/abandonAfterWndProcDrainTimeout/abandonAfterHookDisabled/applyGuiScaleStyle/captureBackdropTexture/renderInventoryBlur` | `rawModuleResource`、资源解析/初始化段；`STB_IMAGE_IMPLEMENTATION` 只保留一个 cpp 定义，所有 stbi 调用方只见声明；`bed_png.h/block_textures.h` 等按实际使用迁移，避免大数组重复实例 |
| `overlay_renderer_input.cpp` | `pollFallbackInput/handleWindowMessage/onWindowMessage` | `isMouseMessage/isKeyboardMessage`、IME Windows 消息处理 `imeShutdownMessage/stopTsf/clearImeComposition/imeCandidateRefreshMessage/updateImeState` |
| `overlay_renderer_ime.cpp` | `renderImeOverlay` | 渲染侧候选卡片局部 helper；不把 WndProc/TSF 线程操作迁到 render helper |
| `overlay_renderer_media.cpp` | `renderMediaOverlay` | 媒体布局/频谱/封面/按键提示的局部 lambda；共享 keycap/纹理 helper 经内部声明访问 |
| `overlay_renderer_toasts.cpp` | `enqueueToast/enqueueMessage/enqueueFeatureToasts/updateBedThreatAlerts/renderToasts` | 威胁身份缓存、动画状态仍是原类字段；复用公共绘图 helper 时只搬声明 |
| `overlay_renderer_internal.h` | 当前 cpp 中 `OverlayInputState` 完整定义；跨文件私有 helper 声明、必要小型 POD 类型 | 不公开给 runtime/controller，不把对象变成全局变量；WndProc 超时后 input bridge 独立保留的设计不变 |
| `overlay_renderer_draw.cpp` | 文件级纯绘图/格式 helper 的单一定义 | `advancePresentationSpring/packedRgbColor/contrastingTextColor/unpackRgb/packRgb/guiScaleForIndex/hotkeyName`、KenneyPrompt 类型/映射与下列绘图 helper 一并迁移；在 internal header 的 `mcoverlay::renderer_detail` 中声明，绝不变成 public API |

`projectPoint/clipProjectedLine/drawProjectedBox/projectedBoxBounds`、`ScreenPoint`、`PrestigeStyle/bedWarsPrestigeStyle/protectionRoman/drawPrestigeStar/formatCompactCount/drawRoundedTriangle`、`defenseBlockColor/drawInventoryBlockIcon/teamColor`、`animatedToggle/beginSmoothChild/endSmoothChild/appendTransientSoftBlur`、`cubicBezierProgress/smootherStep/hypixelStateText` 全部归 `overlay_renderer_draw.cpp`，类型与所需声明放 internal header。即使某 helper 暂无调用，也只搬迁、不删除。第二阶段可再随其唯一职责迁移，但不保留重复定义。

**保持不变的 public API：** OverlayRenderer 的全部 public 方法、FeatureSettings、各 overlay snapshot、MediaAction/BlacklistAction 与值语义；runtime 仍只通过 render/set*/consume*/清理接口交互。`OverlayRendererTestAccess` 现有 friend 保留。

### 第二阶段：render 内连续职责块

下面新私有成员名为建议名，并非现有符号。首先把 render 本帧局部量整理成明确参数或无所有权的 `RenderFrameContext`（delta、uiScale、interactive、snapshot、ImGui IO/绘制目标、当帧颜色/进度）；它只活到本帧结束，不存 JNI 引用或新增异步状态。

| 新文件 / private helper | 当前锚点 | 必须传递/保留的状态 |
| --- | --- | --- |
| 原 `overlay_renderer.cpp` 的 render | 3205–3380 的 HWND/HGLRC 校验、初始化、输入、NewFrame；结尾 Render/RenderDrawData | 返回值 newlyInitialized；前台/热键门控、delta、字体切换、安全早退；ImGui 帧生命周期只在这里 |
| `overlay_renderer_world.cpp::renderWorldOverlay` | 3381 开始的 Ready+camera 分支，到 4036 的 Click GUI 标题前 | snapshot/camera、uiScale、delta、feature flags、预测动画成员；投影/box/轨迹 helper 整体迁移 |
| `overlay_renderer_clickgui.cpp::renderClickGui` | 4036–5997 左右，两栏 GUI、搜索、页面、主题、子 draw-list 后处理 | GUI 位置/尺寸/选择页、搜索、focus、scroll、dirty flags；Begin/End、Push/Pop、顶点变换和 diffusion 作为完整作用域搬迁 |
| `overlay_renderer_blacklist.cpp::renderBlacklistAddDialog` | “Adding a recent player”到 “Text GUI”前，约 5999–6322 | modal 的局部 draw-list 范围、编辑值、actionDirty；不能改变 modal 层级/动画/输入阻挡 |
| `overlay_renderer_textgui.cpp::renderTextGui` | 6323–6554 的 text-only HUD | glyph 动画成员、features/dirty 标记、drag hit target、delta/uiScale |
| `overlay_renderer_stats.cpp::renderPlayerStatsPanel` | 6555 起 statsHotkeyDown 到 7010 | gameplayHotkeysAllowed、snapshot/roster、统计快照、layout dirty/ack、透明度和整面板变换 |
| `overlay_renderer_blacklist.cpp::renderBlacklistPanel` | 7011 的 cumulative roster 注释到面板关闭块（约 7522 前） | world/match、UUID/名匹配缓存、面板拖动/折叠/动作；保持生成 toast 的时点 |
| 原 render 帧尾 | 黑名单块后 feature-change 检测、media/toasts、鼠标/弹窗层级、最终 Render | 保持实际顺序；IME 当前在 world 绘制前（3377），media/toasts 在最终提交前（7561–7562） |

不得跨函数拆开一个 ImGui Begin/End 或 Push/Pop 配对；`appendTransientSoftBlur` 必须在目标 window/child 结束后运行。只迁移，不改帧中同一状态被观察或消费的时刻。各块边界按实际大括号校验，近似行号不能用作剪切指令。

### CMake 与测试

- `P/agent/CMakeLists.txt::McOverlayAgent` 加全部 renderer 新 cpp。
- `P/tests/CMakeLists.txt::McOverlayRendererTests` **同步添加同一批 renderer cpp**；该目标不通过链接 DLL 获得实现。保留 wndproc/blur/tsf/AgentLog/资源 object 和现有系统库。
- 本阶段不更改 `mc_agent_imgui` 版本、OpenGL2 backend 或资源编译目标。
- `McOverlayRendererTests` 覆盖真实 WGL 绘制及内部状态：缩放、主题、页面/滚动/动画、输入法、媒体/按键资源、预测、黑名单等；迁移后运行并检查输出图。
- `Run-OpenGlJvmSmoke.ps1` 的 unified 和 `-SplitThreads` 模式覆盖 callback、输入拓扑和 detach；`McOverlayNavigationTrajectoryTests` 覆盖共享导航/轨迹纯计算，不是 renderer 全部行为测试。
- 当前缺少完整逐帧等价测试；第二阶段每块需要补充对应场景的拆前/拆后对照，不把测试可启动当作视觉等价。

## 3. AgentRuntime.cpp（2,954 行）

入口：[AgentRuntime.h](../../Mc_Injector-master/agent/src/AgentRuntime.h)。
协调生命周期、worker/resolver/scanner/telemetry、协议编解码、帧调度和多来源设置。
`telemetryMain` 741–1147、`handleControlLine` 1566–2368、`beforeSwapBuffers` 2442–2870 是主要职责块。

| 目标文件（P/agent/src/） | 迁移符号 | private helper / 状态 |
| --- | --- | --- |
| 保留 `AgentRuntime.cpp` | 构造/析构、`start/stop/launch/requestStop/join/workerEntry/workerMain/shutdownGraphics` | `g_runtimeMutex/g_runtime` 唯一定义留这里；生命周期不因迁移而拆成多 owner |
| `AgentRuntimeWorkers.cpp` | `launchResolver/resolverEntry/resolverMain/joinResolver`；`launchBedScanner/bedScannerEntry/bedScannerMain/joinBedScanner` | JNIEnv 只在各 worker 的 ScopedThreadEnv 中获得，不跨线程搬运 |
| `AgentRuntimeTelemetry.cpp` | `launchTelemetry/telemetryEntry/telemetryMain/joinTelemetry/queueTelemetry/queueStateChanged/queueHypixelQuery/queueMenuHotkeyChanged/queueGuiScaleChanged/queueMediaSettingsChanged/queueMediaAction/queueBlacklistAction/queueRendererReady` | `FixedLine/appendPercentEncoded/formatGameState/snapshotStateToken/unixMillisecondsNow` 留此消费侧；mailbox/revision/event 仍在类中 |
| `AgentRuntimeFeatures.cpp` | `queueFeatureChanged`；编码 helpers 的单一定义 | `packFeatures/packExtraFeatures/packAimOptions/packFeatureHotkeys/packFeatureHotkeysExtra/unpackFeatures` 经 `AgentRuntimeFeatures.internal.h` 声明，供 control/telemetry/frame 使用；不改 bit 位或默认值 |
| `AgentRuntimeControl.cpp` | `sendHello/sendHandshake/handleControlLine` 原样 | `parseFlag/percentDecode`；解析 stream、字段限制、APPLIED 与 ERROR 回复顺序不变 |
| `AgentRuntimeFrame.cpp` | `frameEntry/beforeSwapBuffers` | `CallbackGuard/SrwExclusiveGuard` 随唯一使用方迁移；帧回调只发布 mailbox，格式化和管道 I/O 仍留 telemetry |

全部成员字段与 private 方法声明保留 AgentRuntime.h；public 仍只有静态 `start/stop`，C 导出仍在 jvm.cpp，不改 def 文件。
运行时包含的 renderer snapshot/FeatureSettings 类型先不搬走，不在此次迁移顺便引入 shared schema。

CMake：仅向 `P/agent/CMakeLists.txt::McOverlayAgent` 增加这些 cpp/私有头；不改 worker 数、链接库、导出或打包。
相关已有验证：三种启动/渲染 smoke 脚本、`McOverlayOpenGlJvmSmoke` 的 unified/split 模式、controller responsiveness 的接收/断线侧；没有独立 AgentRuntime codec 单元测试。
未来应补消息黄金样例/边界长度和双向 feature round-trip；已有 smoke 只覆盖核心生命周期及少量消息，不覆盖整个扩展协议。

## 4. OverlayManager.cpp（3,244 行）

入口：[OverlayManager.h](../../Mc_Injector-master/src/OverlayManager.h)。
同时处理 QObject 状态、进程探测/加载、异步会话、服务端 IPC、设置持久化、业务消息适配。
`processAgentLine` 1783–2514 是最大消息分派块；`loadFeatureSettings/flushFeatureSettings/sendFeatureSnapshot` 包含大量重复字段投影，但本轮只规划搬迁。

| 目标文件（P/src/） | 函数 / 职责 | private helper / 状态 |
| --- | --- | --- |
| 保留 `OverlayManager.cpp` | 构造/析构；`attached/busy/attachToProcess/detach/beginDetach/completeDetach/finalizeDetachedState/startPendingAttach`；`handleAttachFinished/handleAttachError/handleAgentDisconnected/monitorTarget/closeSessionTransport`；`setState/setStatusMessage/setRenderer/clearError/setErrorState/fail` | QObject、timers、process、server/socket 所有权不变；attach/detach timeout 常量留此处 |
| `OverlayManagerProcess.cpp` | `locateAgentDll/locateAttachHelper/locateNativeLoader/locateJavaRuntime/targetExecutablePath/targetWindowTitle/targetArchitectureSupported/targetProcessIsRunning/targetHasLoadedJvm/targetHasLoadedOverlayAgent/isRecoverableJvmAttachFailure/startNativeLoaderFallback` | `firstExistingFile/modularRuntimeContainsAttach/ScopedHandle/WindowSearch/findTargetWindow/nativeWindowTitle` |
| `OverlayManagerSettings.cpp` | `setOverlayEnabled/setInteractive`；从 `setEspEnabled` 到 `setGuiScaleIndex` 的所有设置 setter（含 MC_OVERLAY_BOOL_SETTER 生成项）；`textGuiModules` | setter 的 signal、store、send 顺序原样；宏定义及全部调用一起迁移，不散落不同 TU |
| `OverlayManagerConfig.cpp` | `setConfigAutoSave/saveConfig/applyConfig/removeConfig/loadFeatureSettings/storeFeatureSettings/flushFeatureSettings` | `validConfigName/settingsGroupHasValues/copySettingsGroup`；QSettings key/group/default、批量加载 guard 与延迟写入保留 |
| `OverlayManagerTransport.cpp` | `acceptAgentConnection/readAgentMessages/writeAgentCommand` | `kMaximumAgentMessageBytes/kAgentReadChunkBytes`；读取 budget、queued continuation、source socket 身份检查不变；会话关闭仍由 facade 控制 |
| `OverlayManagerProtocol.cpp` | `processAgentLine/sendStateSnapshot/sendFeatureSnapshot/sendBindSnapshot/sendGuiScaleSnapshot/refreshBedCache` | 握手认证、字段解析、FEATURE_STATE 兼容分支整体保留；不要顺带“修正”未被 controller 消费的 ACK |
| `OverlayManagerServices.cpp` | `publishHypixelResult/publishPlayerStats/publishPlayerStatsError/sendBlacklistCommand/publishMediaState/publishMediaSpectrum/sendMediaSettings` | 输出编码、黑名单消息 allowlist、UTF-8 限长；main.cpp 的服务信号连线不变 |
| `OverlayManagerGameState.cpp` | `resetGameState/refreshGameStateFreshness` | `kGameStateStaleAfterMilliseconds`；游戏状态属性通知与超时行为不变 |
| `OverlayManagerCodec.internal.h/.cpp` | 需跨多个单元共享的值 helper | `validSmartHotbarConfig/normalizedRgbColor/decodeProtocolToken/encodeProtocolToken/boundedUtf8` 移到内部 namespace；单一 cpp 定义，不复制、不扩成 public QObject |

**保持不变的 public API：** OverlayManager.h 的 State/Q_ENUM、所有 Q_PROPERTY 名称/类型/READ/WRITE/NOTIFY、Q_INVOKABLE、slots、signals；Qt singleton 名 `OverlayManager`；原有测试 friend。所有 m_ 字段仍在原类；本阶段无新 QObject，避免改变线程亲和性和连接顺序。

**CMake：** 工程 `CMakeLists.txt::MinecraftOverlayManager` 和 `tests/CMakeLists.txt::McOverlayControllerResponsivenessTests` 同时加入全部新 OverlayManager cpp（包括 codec）。原 OverlayManager.h 继续由 AUTOMOC 处理一次。不将 Agent DLL 链接进控制器。

**已有测试：** `McOverlayControllerResponsivenessTests` 覆盖异步接收、会话与响应性；`MC_OVERLAY_UI_TESTS=ON` 的 controller smoke 验证 QML 属性/弹窗/导航；HotkeyCaptureTests 覆盖相邻输入接口；Attach/NativeLoader/OpenGL smoke 验证工具和 Agent，但这些脚本使用测试管道控制器，不等价于自动覆盖整个 OverlayManager。
**缺口：** 全量 QSettings/Config round-trip、各协议版本字段等价、所有 signal 次数未被全面覆盖。后续搬迁需加入针对受影响路径的验证，而非把普通编译当作行为证明。

## 5. main.qml（3,094 行）

入口：[main.qml](../../Mc_Injector-master/qml/main.qml)。
现有根窗口兼有主题、导航状态机、退出/窗口持久化、进程扫描反馈、七页 StackLayout、底栏、snackbar、attach 弹窗。
不是所有 3,094 行都是可见页面：config 下的旧 WheelPage 明确 `visible:false/enabled:false`，仍然实例化。

### 新文件、属性和 signal 边界

全部新 QML 放在 `P/qml/` 同目录，保持现有相对资源引用。

| 目标文件 | 迁移范围 / 函数 | 需要的显式输入与输出 |
| --- | --- | --- |
| 保留 `main.qml` | ApplicationWindow、`id: app`；`startupReady/activeRoute/autoRefresh`；根主题属性、route 模型与 `pageIndexForRoute`；退出确认、窗口/导航尺寸持久化；两个服务 Connections 与扫描 timer；`openAttachDialog/browseProcesses/returnToSession/clearScannerSelection` | 维持 root contract，统一控制会话导航；第一阶段不把 app 状态变成多个局部副本 |
| `WindowChrome.qml` | windowChrome（403 起）与八个 resize hit area | 显式 `window` 引用及主题；systemMove/resize/最小化等调用原窗口。即使 chromeHeight=0 也不删 |
| `NavigationRail.qml` | navigationRail（546–1021），resize handle | navigationItems、activeRoute、paneWidth/目标状态/主题；发出 `routeRequested/browseRequested/returnRequested/paneWidthRequested`，由根维护 AppSettings 保存逻辑；保留 navigation_<route> objectName |
| `ProcessScannerPage.qml` | StackLayout 第 0 页（1060–1248） | selection/session/scanning 相关根属性、主题；`attachRequested(pid)`；ProcessScanner 仍为同一 singleton；保留 `scanRefreshButton` |
| `ConfigWorkspacePage.qml` + `LegacyFeatureControls.qml` | 第 1 页（1251–2008）；前者包装现有 ConfigPage 和后者，后者接收隐藏 WheelPage | 主题；不复制既有 ConfigPage 实现；旧面板保留 visible/enabled=false、同样实例化时机、内部 Connections；`gameMetric/mappingStateLabel/gameStateSummary/sampleTimeLabel` 的实际调用均在旧面板，四函数随它迁移 |
| `SessionDashboardPage.qml` | 第 2 页（2011–2310），含 TextGuiPreview | injection phase、target/session/主题；发出 browse/attach/detach 意图；游戏格式 helper 属于隐藏旧面板，不误迁入此页 |
| `HypixelPage.qml` | 第 3 页（2313–2406），含现有 HypixelStatsCard | 主题与原 singleton；保留原网络查询操作，不引入 Agent 直接访问 |
| 保持 `PlayerStatusPage.qml` | 第 4 页现有组件（2409–2414） | 原属性绑定不变，无需再次拆分 |
| `AboutPage.qml` | 第 5 页（2417–2476） | 主题；保留 aboutBuildLabel/aboutBrandGlyph、构建文字 |
| `SettingsPage.qml` | 第 6 页（2479–2770） | 主题、autoRefresh 值 + 修改 signal；AppSettings/ApiKeys/OverlayManager/HotkeyCapture 原契约。`menuHotkeyLabel` 实际供隐藏旧面板和 snackbar 使用，留根显式传入；`menuHotkeyIndex` 当前无调用，也留根，不在机械拆分中删掉 |
| `SessionActionBar.qml` | actionBar（2776 起） | 当前选中进程/会话/主题；`attachRequested(pid)`，不复制 pendingProcess |
| `InjectionSnackbar.qml` | snackbarCloseTimer 与 injectionSnackbar（2836–2960 左右） | 文本/主题/动作输入，保持 open/close 入口；由根 onStateChanged 调用；Active 才是完成提示 |
| `AttachDialog.qml` | attachDialog（2962 到文件尾附近） | pendingProcess、主题、injection 状态；`confirmAttach(pid)`，根连接 OverlayManager.attachToProcess；保留 `objectName: "attachDialog"`、open/close、enter/exit 和 Lifecycle.record |

这是组件边界提议，不是为每个组件新增 C++ backend。第一阶段可用单一显式 `required property var host` 传入只读根状态/调用入口，以机械替换原 app 引用；可写操作优先用 signal 回到根，不能赋值给绑定属性造成 binding 被移除。之后再细化 props 属于单独改进。

### 必须保留的 QML 契约

- import `McOverlay 1.0` 与所有 singleton 名不变；`qrc:/qml/main.qml`、Startup.qml 和根 `startupReady` 不变。
- route → index 不变：scanner=0、config=1、main=2、hypixel=3、player=4、about=5、settings=6；未知 route 仍到 scanner。
- StackLayout 仍即时实例化七个页；pageEnterAnimation 的 target 仍为 workspace wrapper。
- `navigation_<route>`、`scanRefreshButton`、`attachDialog`、`aboutBuildLabel` 的 objectName 与窗口下可发现性不变。
- popup 的 overlay parent、z、modal、focus、Esc/关闭策略、动画中断后输入释放不变。拆文件后须检查视觉 parent 与 QObject parent，而非只看画面。
- 内部 id 不能跨组件访问；原根 Connections 通过组件实例公开的 open/close/属性/信号控制，所有原绑定都逐项对照。
- 深浅主题、Settings 保存节流、扫描完成后等待间隔、session error 仍保留目标信息等行为不变。

**CMake：** 仅向工程 `qt_add_resources(MinecraftOverlayManager "qml_resources" PREFIX "/" FILES ...)` 加入所有新 QML；保留 main.qml 和既有文件。不改成 qt_add_qml_module、不新增 import URI、不遗漏隐藏面板资源。

**已有测试：** `MC_OVERLAY_UI_TESTS=ON` 编译后以 `Arcveil --smoke-test` 运行 ControllerUiSmoke，覆盖导航、attach modal 阻挡/反复开关、refresh、about 深浅主题；未启用该选项时启动 smoke 不等于 UI 点击测试。
`McOverlaySkinAndWheelTests` 只验证 Skin/Wheel 相关已有组件；
`McOverlayHotkeyCaptureTests` 验证 C++ 输入捕获。尚无 main.qml 全页面的自动视觉快照等价保障；每提取一页需对照原页的主题、滚动、焦点、属性通知和隐藏页加载情况。

## 完成记录与验证边界

- 按用户指定的五阶段顺序完成。GameBindings.h、AgentRuntime.h、OverlayManager.h 的类契约不变；OverlayRenderer.h 只增加 private 帧上下文/方法。
- 两端 CMake 源列表同步；renderer internal header 与 codec header 不形成新 public API。十二个 QML 组件显式资源打包，七页与隐藏旧面板仍即时创建。
- 每阶段构建及相关测试通过；GameBindings 66、renderer 原有非 render 成员 39、runtime 37、controller 116 个完整函数逐字唯一性核对。宏族整体迁移。
- WGL 前后 4370 checks、JVM fixture 156 checks、Skin/Wheel 1048 checks 均零失败；纯策略、映射、响应性、热键、规则测试通过。Attach、NativeLoader、OpenGL unified/split smoke 通过。
- UI smoke 扩充七页加载截图，保留原 modal/导航/扫描/主题断言。检查 WGL 和 QML 图；动态动画不是逐像素确定性基线。测试输出位于现有 build-main-debug 下 refactor-* 日志/图目录，不属于源代码或交付依赖。
- 剩余架构与验证缺口见 [REFACTOR_FOLLOWUPS](REFACTOR_FOLLOWUPS.md)。

输入 helper 的唯一实现仍在 overlay_renderer_input.cpp；rawModuleResource 在 backend，Kenney prompt helper 在 draw；共享声明在 internal header，默认参数只在声明处。颜色 mixColor 由原局部 lambda 机械提为 draw helper，供 Click GUI 与弹窗共用。

最终复核：`build-main-debug` 完整默认构建及所有 opt-in 测试目标构建成功；十一项可执行测试全部 exit 0。四个文件族共 40 个 cpp 均在各所属目标中恰好注册一次，需直接编译实现的测试目标也同步注册；十二个新增 QML 均在生成的 qrc 输出中。最终链接无遗漏定义/duplicate symbol，TLS 与 STB 实现各保持唯一；暂存新增文件后检测到迁移原文中的既有行尾空白及文件末尾空行，本轮按机械搬迁保留。源码、测试和上下文文档随后按用户要求提交；生成的 build 和任务开始时的无关未跟踪文件保留在本地。
