# Arcveil 内部版本 v54 验证记录

日期：2026-09-26。基线：fd480a8（main 的模块拆分）。

## 修改

1. 攻击可用性开启时，目标筛选按眼睛到碰撞箱的距离判断，避免 interior aim point 超过 3 格导致漏锁。实际攻击仍独立检查 3.0 格射线及遮挡。
2. movement/sprint 接管必须存在实际 Silent Lock 且开启 control adaptation；单纯按住左键、无目标、释放或丢失锁定时使用原版输入。
3. 移动补偿选择最接近原意图的原版八方向按键，保留每轴潜行/物品使用/travel 减速，不再产生连续 analog 轴；保持同一物理 tick 的提交一致性。
4. Smart Hotbar 在原版 consumed-key 阶段本地选择快捷栏槽位，交给原版 controller 正常同步手持物品，不提前调用 syncCurrentPlayItem。主背包交换保留动作释放与 neutral/resume movement packet 门控。手动切槽优先于旧自动补货请求。
5. TSF Begin/Update 结束 transition，第一次 live Update 同步读取；保留 m_reading re-entry guard。IMM composition 同步查询候选 list 0，candidate notify 不再 PostMessage 延迟查询，语言切换仍 reset-only。
6. 添加要求的 IME_MSG / IMM_COMP / IMM_CAND / TSF_* 临时日志。CandidateListElement ABI、ActivateEx flag、*show=TRUE 和候选词绘制代码不变。

## 已通过

| 验证 | 结果 |
| --- | --- |
| Release Agent + 控制器 + 测试目标构建 | 通过；现有第三方 ImGui conversion warnings |
| McOverlayAimControlTests | 70,321 checks, 0 failures |
| McOverlayRegressionPolicyTests | 201 checks, 0 failures |
| McOverlayRendererTests（真实 WGL） | 4,372 checks, 0 failures |
| McOverlayLogicalPipelineHookTests（JDK 21, -Xcheck:jni） | 156 checks, 0 failures |
| 私有 JVM + WGL，同线程 / 分线程 | 均通过，正常卸载 |
| 独立包 Arcveil.exe --smoke-test | 去掉开发工具 PATH 后退出码 0 |

TSF fixture 特别覆盖 reset -> Begin -> 首次 Update，以及 reset -> 首次 Update（没有 Begin、未等 posted settle），验证候选立即进入快照、nested Update 不重入、迟到 settle 不清空候选。

## 真实中文输入验收：未通过

运行 McOverlayImeLiveTests（原生 EDIT）和 McOverlayImeLiveTests --raw（游戏式 HWND），使用本机已安装的微软拼音，完成英文 -> 中文切换并以 SendInput 输入 nihao。仅在焦点确实属于测试窗口时发送输入。

两种窗口均观察到：

```text
LIVE_IME profile=微软拼音 enabled=1 active=1
LIVE_IME activation=0x00000000 layout=0000000008040804
IMM_COMP bytes=12
IMM_CAND required=0 count=0 selection=0
LIVE_IME typed=nihao complete=1 overlayImm=0 overlayTsf=0
```

有 TSF_BEGIN/TSF_END，但未收到候选 TSF_UPDATE。因此没有真实的 IMM_CAND count>0 或 TSF_GET_COUNT count>0，不能宣称满足用户的候选词验收要求。当前补丁只修复已指定的生命周期和同步读取问题，没有扩大到 COM ABI、ActivateEx flag 或 renderer 修改。

包内 diagnostics/v54-ime-live.log 与 v54-ime-raw.log 保存完整记录。Run-IME-test.cmd 可重复运行真实原生窗口测试，失败返回非零。产品日志仍使用既有 OutputDebugString 通道；该测试将同一生产代码的日志输出到文件。

## 验证边界

未在实际 Minecraft/Lunar 世界或服务端验证本轮游戏行为。纯策略、源码边界和私有 JVM 测试不能证明所有客户端映射或服务端发包检查均通过。快捷栏修复通过不主动发快捷栏同步包来保留原版顺序，背包交换门控未移除。
