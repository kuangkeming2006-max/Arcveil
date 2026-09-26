# qml-ui

返回 [INDEX](../INDEX.md)；依赖见 [DEPENDENCIES](../DEPENDENCIES.md)。`P/` = `Mc_Injector-master/`。

## 边界与入口

控制器进程中的 Qt Quick UI；import `McOverlay 1.0` 消费 C++ singleton。
[main.qml](../../../Mc_Injector-master/qml/main.qml) 是主窗口；
[Startup.qml](../../../Mc_Injector-master/qml/Startup.qml) 是 splash。
两者由 StartupLoader 通过 qrc URL 加载；QML 不调用 Agent DLL、JNI 或 ImGui。
游戏内菜单属于 [renderer](renderer.md)，设置一致性经 controller + IPC 完成。

## 现有组件

- 页面/容器：ConfigPage、PlayerStatusPage、WheelPage、PageWheelHandler。
- 业务组件：ProcessCard、HypixelStatsCard、SkinAvatar、TextGuiPreview、ScanIndicator。
- 通用控件：MaterialButton、RippleEffect、MaterialTextField、KeyCaptureButton。
- `OverlaySurface.qml` 虽存在，但未列入当前工程 qml_resources；不要把它作为活跃 overlay 边界。

## QML 文件导航（2026-09-26 已拆分）

| 文件 | 责任与显式契约 |
| --- | --- |
| main.qml | 根窗口、主题/route/session/autoRefresh、持久化 timer、服务 Connections、七页 StackLayout、退出确认 |
| WindowChrome.qml | 原隐藏 chrome 和八个 resize hit area；host 指向原窗口，hit area visual parent 仍是窗口 contentItem |
| NavigationRail.qml | host 只读输入；route/browse/return/clearSelection/paneWidth 信号回根，原 objectName 保留 |
| ProcessScannerPage.qml | host、workspaceWidth；attachRequested(pid) |
| ConfigWorkspacePage.qml | 原 ConfigPage 与即时创建的 LegacyFeatureControls |
| LegacyFeatureControls.qml | 原隐藏/禁用 WheelPage，保留所有绑定/Connections；四个 game 状态格式 helper |
| SessionDashboardPage.qml | host；browse/attach/detach 信号 |
| HypixelPage.qml / AboutPage.qml | 原页面与 singleton，About 测试标识保留 |
| SettingsPage.qml | host；autoRefreshRequested 信号回根 |
| PlayerStatusPage.qml | 原有独立页面，未再次拆分 |
| SessionActionBar.qml | host；attachRequested(pid) |
| InjectionSnackbar.qml | 原 Popup 与 timer；open/close 与 closeTimer 显式入口 |
| AttachDialog.qml | 原 Popup；host、confirmAttach(pid)，由根保持交互关闭/附加成功/导航的原顺序 |

所有新增组件位于同一 `P/qml/` 目录并入 qml_resources。host 是本轮机械提取的显式桥，不依赖跨文件的 app/兄弟 id。导航和底栏对兄弟的 anchors 由 main.qml 实例设置。
旧面板仍为 visible:false/enabled:false，未改 Loader 或页面实例化时机；pageEnterAnimation 仍作用 workspace。

## 公共与测试契约

- C++ StartupLoader 写根 `startupReady`；Lifecycle.record/requestExit 可由 UI 调用。
- OverlayManager 的 state/attached/busy、feature properties、Q_INVOKABLE 和 NOTIFY 都是声明式边界。
- ProcessScanner 提供 model roles 与 selectedPid/refreshing/scanProgress、refresh/selectProcess。
- AppSettings 为 darkTheme、window size、navigation width、auto refresh 等持久化入口。
- 测试观察根 `activeRoute` 与 objectName：navigation_<route>、scanRefreshButton、attachDialog、aboutBuildLabel。提取组件不应改变这些对象的可发现性。
- popup 动画中断后必须释放 modal 输入；保留视觉 parent、z、focus 和 open/close 语义。
- 新 QML 文件无法隐式看到 main.qml 的 id；显式 props/signal 或临时 host 契约见 [拆分方案](../REFACTOR_PLAN.md)。

## 构建与验证

工程 CMake 用 `qt_add_resources` 显式列 QML，PREFIX 为 /，路径仍是 qrc:/qml/main.qml。新增组件需要显式入资源清单，不修改 URI。
`MC_OVERLAY_UI_TESTS=ON` + Arcveil --smoke-test 启用 ControllerUiSmoke 点击/弹窗/refresh/about 场景；
未开选项只覆盖启动。
SkinAndWheelTests 运行自己的 SkinPreview/WheelPreview，并不覆盖全部 main.qml。
2026-09-26：Skin/Wheel 1048 checks、0 failures；UI smoke 保留导航、modal、refresh、主题检查，并新增七页路由加载与截图。运行日志无 QML ReferenceError/TypeError/绑定错误；深浅 About 图与拆前对照检查。

按需读取：页面任务只读目标页面符号边界和其 component；属性不明确时读对应 C++ header。只有信号/绑定行为无法解释时，才读 backend setter/slot 局部实现。
