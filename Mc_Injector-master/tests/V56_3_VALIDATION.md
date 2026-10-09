# Arcveil 内部版本 v56.3：Windows GUI 动画补丁

日期：2026-10-06。基线：main `ea8a961`（v56.2）。

## 修改

- 原样应用 `Arcveil-Windows-GUI-Animation-Fix.patch` 的唯一源码改动：
  `spotlightScale = 1.26F - 0.26F * m_clickGuiProgress`。
- 打开时从 126% 向中心收拢，关闭时向外散开；继续直接使用现有弹簧进度及其小幅超调。
- About、导航说明及 README 标记内部版本 v56.3。
- 未改变 public interface、IPC 或模块边界。

## 验证

| 检查 | 结果 |
| --- | --- |
| 补丁 `git apply --check` | 通过，无冲突 |
| 完整 Release 构建（控制器、Agent、映射和加载工具） | 退出码 0 |
| McOverlayRendererTests，真实 WGL | 4,384 checks，0 failures |
| McOverlayNavigationTrajectoryTests | 43,271 checks，0 failures |
| 移除 Qt/MinGW 开发路径后的便携包渲染与导航测试 | 同上，退出码 0 |
| 便携包 Arcveil --smoke-test（启动与 QML 加载） | 退出码 0，stderr 为空 |
| 包内 Java 21 runtime | `java -version` 退出码 0 |
| git diff --check | 通过 |

检查了深浅主题的打开动画截图。现有 renderer 测试遍历打开过程及全部页面、
主题和四种缩放档位；单张截图不能证明完整动画轨迹，关闭方向由补丁公式确定。
本轮没有运行真实 Minecraft/Lunar 客户端，也没有重新执行真实输入法验收。

## 测试包

`Arcveil_v56.3.zip` 及其解压目录 `Arcveil_v56.3/` 包含 Qt/QML、MinGW 依赖、
Java 21 attach runtime、Agent、映射、加载工具及许可证。
Java runtime 复用已验证的 v56.2 便携包；本次应用及 Agent 使用当前构建产物。

直接启动 `Arcveil.exe`。`Run-GUI-test.cmd` 运行现有 WGL renderer 和导航测试，
日志及截图保存在 `diagnostics/`。本轮构建启用 `MC_OVERLAY_UI_TESTS=OFF`，
因此 `--smoke-test` 只验证启动和 QML 加载，不代表完整控制器交互测试。
