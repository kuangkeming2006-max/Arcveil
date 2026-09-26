# ipc

返回 [INDEX](../INDEX.md)；箭头与读取许可见 [DEPENDENCIES](../DEPENDENCIES.md)。`P/` = `Mc_Injector-master/`。

## 所有权与入口

Controller 用 QLocalServer/QLocalSocket 拥有命名管道服务端；Agent 用 Win32 IpcClient 连接。
协议是换行文本，不共享 C++ ABI 或 QObject。UTF-8 文本字段按具体消息使用 percent encoding，`-` 用作空 token 的约定出现在相应 codec 中，不应泛化到每个字段。

按需阅读顺序：
[ControlProtocol.h](../../../Mc_Injector-master/agent/include/mcoverlay/ControlProtocol.h) →
[IpcClient.h](../../../Mc_Injector-master/agent/ipc_client.h) →
目标消息在 `OverlayManager::processAgentLine/send*Snapshot` 与 `AgentRuntime::handleControlLine/telemetryMain` 的对应分支。
**ControlProtocol.h 只有基础消息说明，不是完整 schema。** Controller 没有 include 该 header；两端存在常量/字段映射重复。

## 核心生命周期

`HELLO 1 <pid> <32-hex-token>` 验证协议版本、目标 PID 和会话 token；控制器 processAgentLine 的认证门之后才处理业务消息。
HOOK_READY 表示 hook 安装，RENDERER_READY 表示真正的 renderer readiness，二者不是同一个里程碑。
STATE/STATE_APPLIED、STATE_CHANGED 同步 visible/interactive。
DETACH 触发已有有序清理，成功后 DETACH_COMPLETE；断管道会隐藏 overlay，DLL 可以继续驻留，不通过 FreeLibrary 自行卸载。

## 实际消息族与两端锚点

下表列消息名和消费路径，不复制序列化实现；字段细节改动必须对读两端。

| 方向 | 消息族 | 发送入口 → 接收入口 |
| --- | --- | --- |
| Controller → Agent | STATE、DETACH | sendStateSnapshot/beginDetach → handleControlLine |
| Controller → Agent | FEATURE_STATE / _V2 / _V3（当前发送 V3） | sendFeatureSnapshot → handleControlLine 的兼容分支 |
| Controller → Agent | AIM_OPTIONS、AIM_ATTACK_CPS、SMART_HOTBAR | sendFeatureSnapshot → 同名 command；packed 值用 SmartHotbarPolicy 校验 |
| Controller → Agent | BIND、GUI_SCALE、BED_RESCAN | sendBindSnapshot/sendGuiScaleSnapshot/refreshBedCache → 同名 command |
| Controller → Agent | HYPIXEL_RESULT、STATS、STATS_ERROR | publishHypixelResult/publishPlayerStats/publishPlayerStatsError → 同名 command |
| Controller → Agent | MEDIA_STATE、MEDIA_SPECTRUM、MEDIA_SETTINGS | publishMediaState/publishMediaSpectrum/sendMediaSettings → 同名 command |
| Controller → Agent | BLACKLIST_RESET、SETTINGS、PRESET、ENTRY、REMOVE、WARNING、SYNC_END（均带 BLACKLIST_ 前缀） | BlacklistService.commandReady → OverlayManager.sendBlacklistCommand → handleControlLine |
| Agent → Controller | HELLO、HOOK_READY | sendHello/sendHandshake → processAgentLine |
| Agent → Controller | RENDERER_READY、STATE_CHANGED | queueRendererReady/queueStateChanged → telemetryMain → processAgentLine |
| Agent → Controller | FEATURE_STATE_CHANGED / _V2 / _V3（当前发 V3）、AIM_OPTIONS_CHANGED、AIM_ATTACK_CPS_CHANGED、SMART_HOTBAR_CHANGED | queueFeatureChanged → telemetryMain → processAgentLine |
| Agent → Controller | BIND_CHANGED、GUI_SCALE_CHANGED | 对应 queue* → telemetryMain → processAgentLine |
| Agent → Controller | GAME_STATE | queueTelemetry → formatGameState/telemetryMain → processAgentLine |
| Agent → Controller | PLAYER_FOUND、PLAYER_STATUS、MATCH_STATE、HYPIXEL_QUERY | mailbox/queueHypixelQuery → telemetryMain → processAgentLine → controller signals |
| Agent → Controller | MEDIA_ACTION、MEDIA_SETTINGS_CHANGED | queueMediaAction/queueMediaSettingsChanged → telemetryMain → processAgentLine |
| Agent → Controller | BLACKLIST_ADD、REMOVE、WARNING、LAYOUT、SETTINGS_CHANGED（均带 BLACKLIST_ 前缀） | queueBlacklistAction → telemetryMain → processAgentLine → BlacklistService slots |
| Agent → Controller | STATUS、ERROR、DETACH_COMPLETE | control/worker 路径 → processAgentLine 的状态/错误处理 |

Agent 还发送 STATE_APPLIED、FEATURE_STATE_APPLIED、BIND_APPLIED、GUI_SCALE_APPLIED、MEDIA_SETTINGS_APPLIED、BED_RESCAN_ACCEPTED 等 ACK。
当前 controller 的主要分派没有为上述每个 ACK 建独立状态迁移；部分由 smoke 脚本验证。
不能仅因协议注释有某个消息就认定 controller 会处理它，未来搬迁也不顺带改变 ACK 消费行为。

GAME_STATE v1 是 15 个空格分隔字段：消息名、版本、sequence、unix-ms、valid、health、max-health、entity-id、x/y/z、loaded-entities、bed-count、mapping、state。
valid=0 时仍需占位数字。controller 校验递增序列、数值与 freshness，session 重置时也要重置序列/认证。
FEATURE_STATE 家族是较长的位置字段协议；V1/V2/V3 与额外 AIM/Hotbar 命令不是可任意排序的字典，禁止只改一端字段位置。

## 分帧、限长与线程

- ControlProtocol.kMaximumLineBytes = 1024；IpcClient.sendLine 和 readLoop 都用此边界。readLoop 还在分帧过程中检查累计 pending；批量到达行为应以该实现为准，不把它描述成无限流缓冲。
- Controller 自己的 kMaximumAgentMessageBytes 为 64 KiB；readAgentMessages 还有 4096 字节块与每轮 64 行 / 64 KiB / 约 3 ms 预算。两端限额不对称，本轮不统一。
- 管道 control worker 解析命令；telemetry 线程格式化和发送上报。IpcClient 写互斥用于多发送来源。
- renderer/runtime 帧回调只发布定长 mailbox / atomic revision；不能把 sendLine 或复杂格式化搬回 SwapBuffers。
- blacklist 同步有 RESET 到 SYNC_END 的边界；布局修改与旧 snapshot 的回声存在 dirty/ack 合并，不是每条收到就覆盖当前拖动值。

## 测试和未来缺口

现有三种 smoke 验证核心握手、STATE/DETACH；OpenGL smoke 还验证 GAME_STATE 无映射帧与 renderer readiness。
ControllerResponsivenessTests 验证生产接收路径及事件循环相关行为。
没有完整的双端协议 schema/黄金样例测试覆盖所有扩展消息、编码、版本兼容及边界长度。后续拆 codec 时应补这些验证，不在本轮修改产品行为。

IPC 是跨模块契约，没有一个现成独立 ipc 库囊括所有业务 codec。
普通 UI/renderer 改动读本页即可；消息变更才读取两端对应分支和服务编码入口，不需要加载整个 runtime/controller 大文件。

2026-09-26 导航更新：控制器接收/快照在 OverlayManagerProtocol.cpp，服务消息在 Services.cpp，读写在 Transport.cpp，公共 token helper 在 Codec.internal.cpp；Agent 接收在 AgentRuntimeControl.cpp，发送在 Telemetry.cpp，feature 编码在 Features.cpp。消息格式与字段顺序未改，生命周期 smoke 和控制器响应性测试已运行通过。
