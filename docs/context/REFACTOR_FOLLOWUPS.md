# 机械拆分后的独立后续议题

2026-09-26。本轮不改变算法、线程、JNI/JVMTI 或 IPC。以下需要单独设计与验证，不能通过继续搬文件宣称已解决。

| 议题 | 后续需要决定 / 验证 |
| --- | --- |
| 共享值契约与 include 耦合 | renderer/runtime 通过 GameBindings.h 传递引入 transform 头；controller 的 SmartHotbarPolicy 仍在 agent 目录。若建立 shared contracts，需先定义所有权和构建边界。 |
| 双向设置/协议 schema | Controller 属性、QSettings、runtime bit/字段重复。先补齐 V1/V2/V3、额外消息、UTF-8 限长和 feature round-trip 黄金样例，再设计统一 schema；不能改变现有 ACK 消费。 |
| GameBindings 巨型函数 | resolveProfile/updateGameplay/sample 只整函数迁移。内部提取需建立局部读写/JNI reference/early-return 清单，保留可选映射容错与发布时点。 |
| 生命周期与超时资源保留 | context 变更、WndProc drain timeout、VM 卸载路径仍由原 owner 管理。若拆成服务，需要单独证明排空与资源有效期。 |
| QML host 契约 | 本轮 host 显式桥保持行为；细化为强类型主题/会话接口须避免绑定移除和页面创建时机改变。 |
| 验证覆盖 | 未在真实 Minecraft/Lunar 客户端执行完整映射与游戏功能；现有 private JVM smoke 只证明受测路径。动画截图需要可控时钟才适合逐帧等价；配置全量 round-trip 和 signal 次数尚未完整覆盖。 |
| 媒体辅助进程退出 | 扩展 UI smoke 退出时观察到 QProcess destroyed while WindowsMediaBridge still running 警告；本轮未改该生命周期，也未据此判定其根因。另行检查 MediaSessionService 与 bridge 的异步退出协议。 |

当前各模块仍是阅读/职责边界，不是已建立的独立库。后续任务从 INDEX 对应模块进入，按 DEPENDENCIES 的实际边排查。
