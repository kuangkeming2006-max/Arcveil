# v56.5 Cache HIT → Attach：真实 Lunar 验收

基于 PR #6 `389a472`，2026-10-09 在同一台 Windows 主机、真实 Lunar
1.8.9 `(c814507/dev)` / Forge、Zulu JRE 17.0.18 上测试。构建为 **Debug**，
MinGW 13.1 / Qt 6.10.1。最终使用 MappingProbe-v13。QML 仅更新版本标签，
structural matcher 的阈值没有修改。

以下三项均实际加载 Agent，等待 exact JNI binding、Lunar profile、游戏状态及
renderer readiness，再分离 Agent。界面是客户端主菜单，状态 `no_player`；
这证明真实类绑定及渲染就绪，不等同于全部游戏内功能验收。每种最终场景一次，
不宣称统计分位数或所有机器的性能保证。

## 优化前：先记录耗时，再改路径

PR #6 的历史原始日志 `build/final-lunar.log`，事务
`78d356ac-d13a-43d8-8998-5e69abb0c565`，总耗时 **49194 ms**：

| 阶段 | 毫秒 |
|---|---:|
| 初始准备 | 566 |
| lite capture | 3659 |
| family / cache lookup / selection | 4159 |
| selected detail | 14867 |
| full live validation | 8670 |
| 第二次 lite / selection / detail final check | 16895 |
| verified → Agent active | 378 |

原调用链：`prepare → lite → identify/cache → select → detail → validate →
final lite → select → detail → native loader → Agent`。Cache HIT 只跳过 resolver。
两次 detail 各约 70 类、2.62 MB；历史日志没有真实 JVMTI 计数，不能补写为 0。

本轮先只加入计时与计数、保留原调用链，重新构建并在真实 Lunar 上测试：
同 PID 第二次 Attach **27008 ms**，Active **26978 ms**，命中为 true，
autoResolveCalls=0、detailCaptureCalls=2。此时 Analyzer 8 次、helper 5 次，
Probe JVMTI 604072 次，transport 6899830 B，JVM 返回 bytecode 536608 B、
constant pool 3631184 B。旧 Agent 尚无 JNI readiness 门槛及计数。

| 阶段 | 原路径实测 ms | 最终同 PID ms | 最终重启 ms |
|---|---:|---:|---:|
| lite / 紧凑 required check | 1782 | 2782 | 3313 |
| cache lookup / selection | 1648 | 8 | 1 |
| selected detail | 8009 | 0 | 0 |
| validation | 4283 | 896 | 841 |
| final check | 1621 + 951 + 8049 | 0 | 0 |
| native Agent loader | 53 | 55 | 79 |
| Agent ready | 149（旧握手） | 374（含最终绑定） | 410（含最终绑定） |

首次输出上述原路径分布之后才实施快速路径。旧路径最大瓶颈确实是两次 detail，
其次是完整 Analyzer validation；并非 native loader。

## 最终三场景结果

连续使用同一磁盘缓存：空缓存启动 PID 30632，完成 Attach / Detach，再次 Attach；
关闭客户端、通过 Lunar 启动器重启为 PID 4392，使用原缓存 Attach。

| 场景 | cacheHit | autoResolveCalls | detailCaptureCalls | totalAttachMs | Active ms | 对 49194 ms 的改善 |
|---|---|---:|---:|---:|---:|---:|
| 全新客户端、空缓存 | false | 0 | 3 | 63576 | 63486 | 不适用于缓存命中比较 |
| 同 PID 再次 Attach | true | 0 | 0 | 4670 | 4472 | 90.51% |
| 相同客户端重启后 Attach | true | 0 | 0 | 5073 | 4978 | 89.69% |

空缓存走 authored-pack bootstrap 和完整 capture / validation；已知客户端无需
automatic resolve，因此冷启动 autoResolveCalls 也为 0。冷路径仍慢，本次没有
将它包装成性能成功。缓存命中相对本轮同配置的 27008 ms 对照，分别减少
82.71% / 81.22%。历史 49.2 秒与本轮 27.0 秒有环境差异，两个基线均保留。

`ATTACH_PERFORMANCE.totalAttachMs` 在进入 Active 时采样；
`LUNAR_ACCEPTANCE.totalAttachMs` 还等待 profile / game state / renderer 均可用，
因此采用更保守的后者作为最终验收。stageMs 是累计墙钟耗时；Agent ready 包含
loader，JNI binding 是其中子阶段，不能将这些重叠阶段直接相加。

| 计数 / 字节 | 冷启动 | 同 PID | 重启 |
|---|---:|---:|---:|
| Analyzer 子进程 | 13 | 2 | 2 |
| helper 子进程（含 Agent loader） | 8 | 2 | 3 |
| 子进程合计 | 21 | 4 | 5 |
| JVMTI 调用 | 1143510 | 294226 | 292979 |
| Agent resolver JNI 调用 | 30094 | 30099 | 29958 |
| transport bytes | 16022334 | 469956 | 469953 |
| JVM 返回 bytecode bytes（双读合计） | 1585870 | 234624 | 234624 |
| JVM 返回 constant-pool bytes（双读合计） | 9182622 | 1056854 | 1056854 |

计数范围是 Probe capture/warmup/dispose 与 Agent binding resolver；不包含正常
逐帧渲染、其他 Agent 工作线程和分离过程。JNI 计数范围为
GameBindingsResolve.cpp 中的 resolver 调用，不声称是整个 JVM 的调用总数。
SNAPSHOT_STATS 同时输出 JVMTI 分操作计数，CAPTURE_HELPER 记录每次子进程耗时。
重启多一次 helper 是新进程首次尝试标准 Attach，确认不支持后切换 NativeLoader；
后续同 PID/start 会复用 transport memo。

## 命中路径与保留的验证

最终调用链为：

`AttachTransaction → integrity-checked preferred verified pack →
inspect --lite --binding-check --identity-lite --required-only → validate-cache →
PID/start + pack SHA256 final check → native Agent loader → authenticated HELLO →
exact JNI binding / BINDING_READY + RENDERER_READY → Active`。

命中只有 2 个 Analyzer 进程：一次紧凑检查，一次 exact member / proof validation。
没有 select、inspect-detail、完整候选结构解析及第二次 final snapshot。
final check 仍做进程身份和 pack 摘要比较，本次毫秒分辨率下为 0 ms。

同 PID 也保留一次紧凑双读，因为 PID/start 相同不能排除 retransformation、类增载
或 loader 变化。此次只对直接使用的 18 个类获取内容摘要，其继承关系闭包检查完整
字段/方法元数据；捕获共 72 类（包含检测标记），portable proof 覆盖 70 类。
两次 reads 发生在同一个 Probe 请求内部，用于拒绝捕获中变化的类。
bytecode / constant pool 在 JVM 内计算摘要，序列化到控制器的对应字节量均为 0；
表中的非零字节是 JVMTI 实际返回的内存总量，没有通过日志口径隐藏这些调用。
这一步同 PID 2782 ms、重启 3313 ms，有当前运行时验证价值。

verified pack 提供精确类名、字段/方法名和 descriptor。Analyzer 验证 owner、
descriptor、static/instance、继承关系、defining-loader 分区和 installed proof。
跨进程 proof 不绑定旧 loader 实例；当前 capture 提供新的 anchor/type/instance，
Agent 使用此 pin 重新定位真实 defining loader 并执行 JNI lookup。重名或冲突拒绝。

Lunar 的 Mixin annotation session UUID、关联 synthetic lambda nonce 会跨启动变化；
JVM 也可能以不同顺序追加生成的 overpass 常量。proof 仅归一化已识别的启动 nonce，
保留全部 pool 内容、每条指令实际引用的索引/值及完整原始方法字节。
未知或截断指令保留 indexed raw pool，保守拒绝漂移。业务字符串、非 synthetic 名称、
descriptor、字段和代码变化均有负向回归覆盖；原始 exact member lookup 不做归一化。
这项规则仅用于 cache proof，不改变 structural matcher 的评分或阈值。

仅缓存缺失、轻量检查失败或内容/结构变化进入原完整 capture / automatic matching。
失败不会把缓存候选当成 verified；lastVerified 只是候选排序提示。

PR #6 旧缓存没有 bindingIdentity：先校验其完整 source 的 SHA256，离线计算 portable
proof 并核对原 mappingIdentity/metadataIdentity，再做当前紧凑检查，最后原子发布新 proof。
额外真实测试把本次真实 Lunar full source 复制为 PR #6 proof 格式（移除新字段）：
首次升级 **10162 ms**，其中离线 proof **5136 ms**，cacheHit=true、autoResolveCalls=0、
detailCaptureCalls=0，随后成功 CACHE_PROMOTE。此为兼容格式迁移测试，不冒称原始 PR #6
历史缓存的原样重放；完整回归另覆盖旧 proof 升级和 source 身份不符拒绝。

## Fail-closed 与事务回归

真实 Lunar 的负向测试在 verified 后故意翻转 loader instance，Agent 返回
`RUNTIME_BINDING_FAILED exact-JNI-binding-rejected`，控制器 `active=false`、
`rendererActive=false`。HELLO 和 RENDERER_READY 不能单独授权 Active。
同 PID JNI binding 46 ms、重启 78 ms，计入真实验收，没有删除这一步。

保持原 AttachTransaction 的 generation/owner/cancel/processStart 和原子缓存发布；
新增可观察计时事件之后再检查 owner，两个 loader 启动前再次核对 PID/start。
旧事务的结果不能启动 Agent。取消、supersede、暂停/恢复、关闭进度窗、缓存内容变更、
legacy 原子升级均通过实际 Analyzer 算法的事务回归。

辅助回归：Analyzer 24 项、mapping boundary/cache 32 项、transaction/cache 21 项、
MappingService 18 项、dynamic service 69 项；controller IPC/readiness/异步扫描回归通过；
resolver、two-stage、incremental 脚本通过。fixture 只用于正确性，不用于上述性能结论。

## 证据与复现

提交的 `evidence/v56_5-lunar.jsonl` 保存真实收据、每阶段时间、Probe 分操作统计、
基线和负向结果，未包含 JVM bytecode 或完整 snapshot。原始日志保留在本机
`build-cache-attach-debug/`，其 SHA256 和构建三件套 SHA256 一并写入证据。

最终原始日志：`acceptance-final-cold-same-lunar.log`、
`acceptance-final-restart-lunar.log`；负向为 `acceptance-v13-wrong-loader-lunar.log`；
旧格式迁移为 `acceptance-legacy-lunar.log`。探索中两次重启被安全拒绝的记录也保留，
它们未计作通过；用来定位 nonce 和 generated-pool 顺序差异。

构建目标 `McOverlayLiveMappingSmoke` 后，在真实 Lunar 主菜单设置：

```powershell
$env:ARCVEIL_LIVE_CACHE = '<同一个空缓存绝对目录>'
$env:ARCVEIL_LIVE_CYCLES = '2'
$env:ARCVEIL_LIVE_SCENARIO = 'cold-and-same-pid'
& '<build>/McOverlayLiveMappingSmoke.exe' <真实LunarPID> *> cold-and-same.log
# 通过启动器关闭/重启同版本 Lunar，保持上面的磁盘缓存：
$env:ARCVEIL_LIVE_CYCLES = '1'
$env:ARCVEIL_LIVE_SCENARIO = 'restart'
& '<build>/McOverlayLiveMappingSmoke.exe' <重启后的PID> *> restart.log
python Mc_Injector-master/tests/mapping/Verify-LunarAcceptance.py cold-and-same.log restart.log
```

Verifier 检查同 PID/start 与重启身份关系、cacheHit/resolve/detail 计数、真实 Probe 统计、
JNI readiness、Lunar profile 和 renderer，并输出与 49194 ms 的比较及日志 SHA256。
负向设置 `ARCVEIL_LIVE_REJECT_LOADER=1`，在真实 JVM 上验证错误 loader 不进入 Active。

交付 ZIP 及解压目录使用相同 Debug 构建，包含 Qt/QML/平台插件、MinGW runtime、
Agent、Analyzer、Probe、NativeLoader、attach helper、mapping packs、contracts 和许可证。
独立 Analyzer schema 检查通过；清除开发 Qt PATH 后，打包 Arcveil 的 D3D11
`--smoke-test` 返回 0。没有将 Debug 验收换算成未经实测的 Release 数字。
