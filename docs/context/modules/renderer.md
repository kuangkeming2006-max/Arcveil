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
| overlay_renderer_clickgui.cpp | renderClickGui，产出本帧主题颜色供弹窗使用 |
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


## v54 IME 生命周期验证入口

BeginUIElement/UpdateUIElement 的 live callback 结束 layout transition；Update 在 callback 内读取，保留 m_reading guard。WM_IME_COMPOSITION 同步尝试 IMM list 0；candidate notify 在原消息内读取，WM_INPUTLANGCHANGE 仍 reset-only。ABI、ActivateEx flag 和绘制路径不变，系统候选 UI 保持可见。

McOverlayImeLiveTests 是显式运行的真实 Windows TIP 测试，激活已安装中文输入法并向自己的前台窗口输入 nihao；可加 --raw 测试游戏式 HWND。诊断写 stdout，返回 0 仅表示实际候选已进入 overlay 输入快照；fixture 测试不能替代这个验收。v54 本机微软拼音的两种窗口均返回 1：composition 非空，IMM candidate count=0，未收到 candidate Update；此项未通过。完整记录见 P/tests/V54_VALIDATION.md。
