# Arcveil 内部版本 v56.4：GUI 独立缩放与交互细节

日期：2026-10-08。基线：v56.3 `18fd713`；保留已应用的 Windows GUI 动画补丁。

## 修改

- Interface 新增 Element size，60–150%，默认 100%，独立调整 GUI 的文字、控件和间距。
  不改变四档共享缩放，不改变窗口外部尺寸、HUD 或其他窗口的大小。
- 窗口宽、高范围均为 40–150%；同时更新控制器、持久化、Agent 解析与回传边界。
  小窗口自动限制内容缩放，窄布局使用分类下拉菜单、堆叠 Hotbar 行与滚动视口。
- 独立 GUI_ELEMENT_SCALE / CHANGED / APPLIED 消息保持 FEATURE_STATE_V3 字段位置不变；
  renderer dirty mailbox 防止旧快照覆盖正在编辑的值。
- 下拉框采用圆角字段、统一预览、旋转箭头、悬停背景、选中标记与焦点边框。
  使用 [Dear ImGui 官方 BeginCombo 方案](https://github.com/ocornut/imgui/issues/1658)，
  保留原生鼠标、键盘导航和弹出窗口行为，没有增加外部 UI 依赖。
- GUI 边框使用几何抗锯齿，缩放动画补偿 fringe 与侧栏描边厚度；描边内缩避免裁剪。
- 导航文字预留完整 hover 位移后确定字号，并保留小数位置，使逐字流光移动时保持字距。
- 开关行增加水平内边距、文字与开关间隔；长标签换行，主开关扩大布局空间。
- About / README 标记内部版本 v56.4，并更新相关模块与 IPC 契约文档。

## 验证

| 检查 | 结果 |
| --- | --- |
| Release 控制器、Agent、renderer/controller tests、JVM harness 构建 | 退出码 0 |
| McOverlayRendererTests，真实 WGL | 5,599 checks，0 failures |
| 下拉框鼠标选择、键盘 Down/Enter、Esc、点击外部关闭及 stack 平衡 | 通过 |
| 深浅主题、全部页面、四档共享缩放、打开与关闭动画 | 通过 |
| 元素大小 60/100/150%，外部窗口尺寸/HUD 缩放不变 | 通过 |
| 窗口宽高 40%，主要页面/深浅主题/三种元素大小下 child 视口与 AA 策略 | 通过 |
| 逐字流光与普通文字的小数位移顶点/字距稳定 | 通过 |
| ControllerResponsivenessTests：配置恢复、持久化、非法消息、40% 布局回声 | 通过 |
| 私有 JVM + 生产 Agent WGL，unified / split-thread | 均通过 renderer readiness、设置 ACK、40% 布局及有序 detach |
| 新 IPC 的边界值 60/150、默认 100 与非法 59/151/额外字段/非数字 | 通过 |
| 便携包移除 Qt/MinGW 开发路径后的 renderer/controller tests | 均通过；renderer 5,599 checks，0 failures |
| 便携包 Arcveil --smoke-test | 退出码 0，stderr 为空 |
| 包内 Java 21 runtime | java -version 退出码 0 |
| git diff --check | 通过 |

检查了 Interface、Smart Hotbar、40% 窄窗口以及打开动画截图。
顶点测试证明各字母整体位移和字距稳定；截图与几何策略检查不能替代真实游戏中的主观动画验收。
本轮未运行真实 Minecraft/Lunar 客户端，也未重新执行真实输入法验收。
MC_OVERLAY_UI_TESTS=OFF，--smoke-test 仅验证控制器启动和 QML 加载。

## 测试包

Arcveil_v56.4.zip 及解压目录 Arcveil_v56.4/ 包含本次应用、Agent、Qt/QML、MinGW、
Java 21 attach runtime、映射、工具与许可证。Java runtime 复用已验证的 v56.2 包。
直接启动 Arcveil.exe。Run-GUI-test.cmd 提供可选 renderer/controller 回归检查。
diagnostics/ 包含截图、测试日志和本轮 unified/split-thread JVM 验证记录。
