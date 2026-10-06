# renderer

返回 [INDEX](../INDEX.md)；依赖见 [DEPENDENCIES](../DEPENDENCIES.md)。`P/` = `Mc_Injector-master/`。

## 责任与入口

Agent 进程中的 ImGui/OpenGL2 UI，负责游戏内投影、Click GUI、HUD、媒体/IME/toast 和输入桥。
入口 [overlay_renderer.h](../../../Mc_Injector-master/agent/overlay_renderer.h)；
它不是控制器 QML 页面，也不是游戏 JNI 调用层。

runtime 提供 GameSnapshot、FeatureSettings、Hypixel/PlayerStats/Blacklist/Media snapshot，
调用 render(HDC, snapshot, interactive)，之后消费 consume* 回传动作。
输入与 UI 修改留在原 renderer 状态，外部不会直接写 m_ 字段；测试 friend 是例外。

## public 契约

- render 的 bool 返回值表示某个 HWND/HGLRC generation 首次成功初始化/绘制，不是一般“帧成功”。
- initialized/ownsCurrentContext 与 shutdownWithCurrentContext/abandonAfterHookDisabled 表达 GL context 生命周期。
- setFeatureSettings 与 consumeFeatureSettings；setMenuHotkey/setGuiScaleIndex 及 consume 改动。
- setGuiTypography/consumeGuiTypographyChange：独立字号/字重设置；dirty 保护先发布再合并外部快照。
- set*Snapshot 与 consumeHypixelQuery/consumeBlacklistAction/consumeMediaAction/consumeMediaSettings。
- consumeClickGuiToggle/consumeBedRescanRequest/setGameScreenOpen。
- shieldAttacker 只从 UI 选择和 snapshot 计算标识值，runtime 将其纳入游戏设置；不执行攻击或 JNI。

## 文件与内部职责（2026-09-26 已拆分）

所有文件位于 `P/agent/`，仍实现原 OverlayRenderer；仅新增 private 绘制方法。

| 文件 | 入口 / 责任 |
| --- | --- |
| overlay_renderer.cpp | render 帧生命周期、热键门控、按原序调用绘制块、最终提交 |
| overlay_renderer_state.cpp | set*/consume* mailbox 与 dirty/ack 合并 |
| overlay_renderer_backend.cpp | 构造/清理、initialize、context、字体纹理、backdrop/blur；唯一 STB 实现与嵌入图像数组 |
| overlay_renderer_input.cpp | fallback polling、Win32/IME/TSF、WndProc handler |
| overlay_renderer_ime.cpp / _media.cpp / _toasts.cpp | IME、媒体、提示与床威胁 |
| overlay_renderer_draw.cpp | renderer_detail 下唯一绘图、颜色、投影、格式 helper |
| overlay_renderer_internal.h | OverlayInputState、private RenderFrameContext、helper 声明与值类型 |
| overlay_renderer_world.cpp | renderWorldOverlay |
| overlay_renderer_clickgui.cpp | Windows 适配器；保留原 renderer 状态/dirty mailbox、热键桥和本帧主题回传 |
| ui/UiModel.h | 无平台依赖的 FeatureSettings、媒体、统计、黑名单值类型；virtual-key 整数保留原 wire 编码 |
| ui/GuiDesignState.h | 无 ImGui 依赖的分类、页面记忆、控件动效状态；OverlayRenderer public header 只依赖值类型 |
| ui/GuiTypography.h | 字号 14–24、字重 400/600/700 校验与原子打包；Click GUI 分辨率适配策略 |
| ui/ClickGui.h / .cpp | 完整共享 Click GUI，顶部六分类独立标签、分类内功能侧栏、全局搜索、24 个设置页 |
| ui/AnimatedWidgets.h / .cpp | 每个 GUI 独立持有的 hover/press/value/focus 动效；开关、滑块、菜单、按钮、色彩与输入框 |
| ui/NavigationLabel.h | 以原 FeatureNavigation::glyphGlow 绘制已启用侧栏功能的逐字往返流光；适配字重、字号与 reduced motion |
| overlay_renderer_blacklist.cpp | renderBlacklistAddDialog、renderBlacklistPanel |
| overlay_renderer_textgui.cpp / _stats.cpp | renderTextGui、renderPlayerStatsPanel |

RenderFrameContext 只在一帧调用栈上生存，引用当前 snapshot/ImGui IO，传递 delta、uiScale、门控和颜色；不持有异步状态或 JNI 引用。
WndProcHook 仍属 hook 模块。gaussian_blur、tsf_candidates、UiColors/UiMotion/UiPreferences、FeatureNavigation/MediaHotkeyEdges 仍是原支撑组件。
内部头不供 runtime/controller 使用；资源数据不重复实例化。

## 生命周期/绘制约束

必须在拥有原 HGLRC 的线程清理 GL backend。切换到新 context 时不能删除旧 context 的纹理编号；原实现有保留/放弃路径。
WndProc restore 超时不能销毁尚可能被回调使用的 input/ImGui 状态。
ImGui Win32 backend 仅在兼容线程拓扑下被 WndProc 直接调用；split-thread 通过独立桥与 polling。

拖动或编辑的 dirty 状态应先回传，不被旧 IPC snapshot 覆盖。
绘制顺序、ImGui ID、Begin/End、Push/Pop、draw-list 顶点范围和后处理时机属于行为。
字体/纹理加载与 STB_IMAGE_IMPLEMENTATION 不得在拆文件后重复定义。

## 测试和上下文

McOverlayRendererTests 直接编译 renderer、wndproc、blur、tsf、AgentLog 与嵌入资源，以 OverlayRendererTestAccess 访问私有状态。
新 renderer cpp 必须同时入 Agent 和该测试目标，不能只更新 DLL。
OpenGlJvmSmoke 的 unified/split modes 验证实际 hooked 帧与 teardown；NavigationTrajectoryTests 验证部分纯算法。
2026-09-26：拆前/拆后 WGL 测试均为 4370 checks、0 failures；同线程/分线程 JVM smoke 通过。检查了 28 张前后截图及主要场景总览；动画和媒体图受采样时间影响，并非逐像素等价证明。

只改 HUD 时读目标块和 helper 声明；只有输入/context 问题才读 hook 实现。renderer 对 game-bindings 默认仅需 snapshot 类型，不能将 BindingCache 或 JNI 操作移入绘图层。


## 2026-10-02 Windows 游戏内 GUI 设计

`ui/ClickGui.cpp` 实现 Windows 游戏内 GUI。`ClickGuiRefs`
绑定原 renderer 的 settings、UI 状态和 dirty 标记；`ClickGuiHost` 只桥接按键捕获、
平台键名、toast 和瞬态 blur，不调用 JNI。只读 `ClickGuiSnapshot` 限定到当前页面
需要的能力和玩家标识，Windows 适配器每帧投影既有 GameSnapshot。

顶部标签记住各分类的最后设置页。侧栏点击名称打开设置、尾部状态点启停，右键仍打开
设置；页首保留主开关。搜索仍跨全部分类、支持已有中文关键字并排除两个 retired 页面。
页首显示 Arcveil / 分类 / 当前功能路径；底部装饰文字和左下主题控件已移除，主题在
Interface 的 THEME 设置区切换。控件焦点边框在控件边界内绘制，避免 child clip 裁切。
原设置页滚动、输入捕获、dirty/ack 合并和安全 interlock 留在原路径。

Interface 的 Typography 提供独立字号与字重；默认 18 epx / Semibold。共享 view 根据
framebuffer 高度补偿密度并按视口收敛布局，2560×1600 / M 下正文约 36px；HUD 继续
使用原字体。Windows 主体用 Segoe UI Regular/Semibold/Bold 三种字重。
字体按实际大小由 ImGui 1.92 动态 atlas 光栅化；副标题可换行，slider 为长标签留出
额外高度。字体编辑走 GUI_TYPOGRAPHY 独立消息，不改变 FEATURE_STATE_V3 的位置字段。
新增 shared cpp 已同步加入 Agent 和 McOverlayRendererTests 构建目标。

## v54 IME 生命周期验证入口

BeginUIElement/UpdateUIElement 的 live callback 结束 layout transition；Update 在 callback 内读取，保留 m_reading guard。WM_IME_COMPOSITION 同步尝试 IMM list 0；candidate notify 在原消息内读取，WM_INPUTLANGCHANGE 仍 reset-only。ABI、ActivateEx flag 和绘制路径不变，系统候选 UI 保持可见。

McOverlayImeLiveTests 是显式运行的真实 Windows TIP 测试，激活已安装中文输入法并向自己的前台窗口输入 nihao；可加 --raw 测试游戏式 HWND。诊断写 stdout，返回 0 仅表示实际候选已进入 overlay 输入快照；fixture 测试不能替代这个验收。v54 本机微软拼音的两种窗口均返回 1：composition 非空，IMM candidate count=0，未收到 candidate Update；此项未通过。完整记录见 P/tests/V54_VALIDATION.md。

## v56.2 IME 候选协商与渲染回退

### 故障原因与提醒（2026-10-06）

旧实现始终返回 `*show=TRUE`，允许输入法自行显示候选窗口；按
[TSF 协商契约](https://learn.microsoft.com/en-us/windows/win32/tsf/uiless-mode-overview)，
此时输入法可以不发送 `UpdateUIElement`。本机微软拼音确实未发送候选更新，
IMM 备用通道的候选列表也为空，因此虽然有拼音组合串，overlay 仍拿不到候选词。
修复要点是对支持的候选接口返回 `FALSE`，请求输入法持续提供候选更新。

排查时同时检查候选更新、候选数量和实际显示；组合串非空不能证明候选通路正常，
单张窗口截图也可能漏掉系统候选窗。用户于 2026-10-06 确认 IME 模块工作正常。

### 当前处理与验证

`TsfCandidates::BeginUIElement` 只探测候选接口，不读取尚未完成的候选内容。
支持的 candidate element 返回 `*show=FALSE` 请求 UI-less Update；未知 UI 返回 TRUE。
内容继续在 live Update 同步读取，保留 COM ABI、窗口线程、重入与 generation 检查。

`ImeCandidates::generation` 随候选生命周期变化；renderer 仅在实际生成候选绘制命令并
提交 OpenGL 后调用 `candidatesDrawn(generation)`，位置预览不算确认。窗口线程通过
`onWindowTimer` 处理仅属于 TSF helper 的 100ms timer：超过 500ms 未提交当前 generation
的候选帧时恢复 native UI。旧帧确认不能维持新候选的隐藏状态。读取失败清空旧快照并回退；
End、语言切换、禁用和卸载取消 timer、恢复并释放 retained native element。
Show(TRUE) 引发的回调受 restoration guard 保护，不重新发布旧候选。

`McOverlayImeLiveTests` 仍是显式运行的真实 TIP 测试。`--render` 使用生产 WGL renderer；
`--split` 将 OpenGL 放到独立线程；`--stall` 验证暂停渲染后归还 native UI 的请求成功；
`--output <dir>` 保存实际候选截图；`--hold` 保留窗口 45 秒供观察。返回 0 还要求
空格提交“你好”且 TSF 候选清空；render 模式要求提交过候选帧和请求的截图成功。
v56.2 本机微软拼音 native EDIT、raw、WGL unified/split 与 stalled renderer 均通过。
真实 Minecraft 客户端并未运行；WGL 窗口验证使用真实输入法与生产 IME/绘制路径。
系统候选窗口可以在测试 HWND 截图范围外；旧 element 在 Show(TRUE) 后可能结束。
`priorElementShown` 仅表示旧接口状态，不能据此否定用户看到的候选窗口；
`stalledRendererNativeRestoreRequested` 验证恢复请求 HRESULT，未自动验证完整系统 UI。
