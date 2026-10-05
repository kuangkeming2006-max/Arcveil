# Arcveil 内部版本 v56.2：IME 候选修复

日期：2026-10-05。基线：main `2065c41`（v56.1）。

## 故障与证据

用真实 Windows 微软拼音向现有 `McOverlayImeLiveTests` 输入 `nihao`。
原生 EDIT 基线退出码 1：组合串非空、`IMM_CAND required=0`，没有候选 Update，
最终 `overlayImm=0 overlayTsf=0`。日志保留在包内 `diagnostics/ime-baseline-edit.log`。

根因是 BeginUIElement 始终返回 `*show=TRUE`，允许 TIP 只绘制自己的窗口而不发送
UpdateUIElement。微软拼音实际选择此路径，IMM 兼容候选也为空。对照实验请求
UI-less 更新后，首次 Update 返回 531 个候选；输入完整 `nihao` 返回 94 个，当前页 9 个。
依据：[Microsoft UILess Mode Overview](https://learn.microsoft.com/en-us/windows/win32/tsf/uiless-mode-overview)。

## 修复行为

- Begin 仅检查候选接口，支持的候选返回 FALSE；未知 UI 保持 native 显示。
  内容在 Update 同步读取，不在 Begin 读取未完成内容，不延迟使用旧 element ID。
- 仅实际绘制并提交 OpenGL 的候选帧确认其 generation；预览和旧帧不能确认新候选。
- HWND 线程用 100ms timer 检查：500ms 没有当前候选帧即恢复 native UI。
  时钟采样与渲染线程并发时防止无符号差值下溢；窗口消息须正常被处理。
- 读取失败清空旧候选并回退；空列表、End、语言切换、禁用与卸载归还 native
  element/timer。Show(TRUE) 的重入回调不会重新发布已清空的候选。
- 关于页面及导航显示内部版本 v56.2；保留 v56.1 的映射、GUI 与游戏功能。

## 验证结果

| 检查 | 结果 |
| --- | --- |
| 完整 Release 构建：Arcveil、Agent、映射与加载工具 | 退出码 0 |
| McOverlayRendererTests，真实 WGL | 4,384 checks，0 failures |
| McOverlayRegressionPolicyTests | 201 checks，0 failures |
| 真实微软拼音，native EDIT | 9 个候选，提交“你好”，候选清空，退出码 0 |
| 真实微软拼音，raw 游戏式 HWND | 同上，退出码 0 |
| 真实微软拼音，生产 WGL renderer / 同线程 | 实际候选截图与绘制帧，提交和清理通过 |
| 真实微软拼音，生产 WGL renderer / 分线程 | 9 个候选，持续提交候选帧，提交和清理通过 |
| 暂停 renderer 800ms | 归还 native UI 的 Show(TRUE) 请求成功，仍能提交“你好”，退出码 0 |

Fixture 同时覆盖未知 UI、Begin 不读候选内容、首次 live Update、nested Update、
layout reset 的 Show 重入、旧 generation、未绘制/暂停回退、读取失败后的旧候选清理、
空列表、End 和 shutdown。

## 可复现入口与包

`McOverlayImeLiveTests` 是 opt-in 目标，不会由普通构建或空 ctest 自动运行。

```text
McOverlayImeLiveTests.exe
McOverlayImeLiveTests.exe --raw
McOverlayImeLiveTests.exe --render --output diagnostics
McOverlayImeLiveTests.exe --split --stall --output diagnostics
```

需本机已安装并启用中文 TIP、桌面已解锁，测试窗口在前台；测试仅向自己的窗口发送
按键。`--hold` 在提交前保留候选 45 秒，便于观察；失去焦点时不会向其他窗口输入。

便携目录 `Arcveil_v56.2` / ZIP `Arcveil_v56.2.zip` 包含 Qt/QML 和 MinGW 运行依赖、
Java 21 attach runtime、Agent、映射和辅助工具。`Arcveil.exe` 直接启动；
`Run-IME-test.cmd` 运行真实分线程 WGL 验收，日志与截图写入 `diagnostics`。
打包及解压后均去掉开发工具 PATH，检查启动、工具与 IME 测试。

## 验证范围

本轮未运行真实 Minecraft/Lunar 客户端。测试使用真实 Windows 输入法、游戏式
HWND、生产 WndProc/IME/renderer 和实际 OpenGL；不把 fixture 或组合文本当作候选
显示证据。其他厂商 TIP 未在本机验证，未知 UI 保留系统显示。
用户已确认实际窗口能看到候选词。系统候选 UI 可能位于测试窗口截图范围外；
Show(TRUE) 后旧 element 可能结束，因此旧接口的 IsShown 值不能证明系统 UI 没有显示。
自动 stall 检查验证恢复请求成功和输入仍可提交，不把它冒充完整系统窗口的可视验收。
