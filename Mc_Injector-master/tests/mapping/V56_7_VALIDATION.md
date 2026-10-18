# v56.7 多版本适配基础 — 实际验证记录

2026-10-10，Windows x64，MinGW 13.1，Qt 6.10.1，Microsoft JDK 21.0.10。
基线 main 764d4b8；本轮实现与源码审查见 `docs/context/MULTIVERSION_REVIEW.md`。

## 构建

Release：MinecraftOverlayManager/Arcveil、McOverlayAgent、MappingAnalyzer、MappingProbe-v13、AttachHelper、NativeLoader 以及下列测试目标构建成功。
Debug：BedWarsState、AimControl、Knockback、NavigationTrajectory 测试目标构建成功，使用 Debug 保留 assert，未把 Release 的空断言执行当作充分验证。
普通构建并不等于运行测试；未使用空 ctest 结果作证。

## 实际通过

| 测试 | 结果 | 证明范围 |
| --- | --- | --- |
| McOverlayGameApiTests | 31 checks，0 failures | mock JNI；版本解析、exact allowlist、capability、singleton/entity-list accessor、统一 inventory slot 与 JNI 调用数量 |
| McOverlayMappingProviderTests | passed | 原 family evidence、候选选择、优先级、动态注册与 freeze 等 |
| McOverlayMappingPackTests | 67 checks，0 failures | schema-1 legacy 摘要 parity、schema-2 roundtrip、五份字典元数据与符号保持、版本一致性、缺文件诊断 |
| MappingAnalyzerTests | 24 checks，0 failures | installed proof/快照与 schema/diff 等，用 synthetic raw.json 和 default-v2.json |
| MappingBoundaryTests | 32 checks，0 failures | schema-1 pack/hash、Agent options、cache integrity/promotion/rollback/reference/lock；参数为 default-v1.json |
| Test-Resolver.py | passed | 全 schema 257 keys 的合成类/成员重命名恢复；缺 reference、歧义、错误可选 descriptor 拒绝 |
| Test-VersionAdapters.py | passed | schema-only 不授权；错误版本的 full/cache/resolve/incremental 拒绝；schema-2 自动恢复标 Custom；错误核心 descriptor 返回 playerField 诊断 |
| Test-TwoStage.py | passed | 合成两阶段 scope、loader、reference、lite guard 与冲突拒绝 |
| Test-Incremental.py | passed | 合成增量保留/失效、进程绑定、scope 和 provisional export gate |
| TransactionCacheTests | 22 checks，0 failures | 真实生产 Analyzer/Resolver/validator + 替代 capture transport；A–K cache/事务回归及 L 不支持版本终止、不 ready/promote/retry |
| McOverlayControllerResponsivenessTests | passed | bounded IPC、split lines、teardown、异步进程扫描；最终运行 5,053ms，10 polls，252 UI heartbeats |
| McOverlayRegressionPolicyTests | 201 checks，0 failures | 现有源码顺序/边界规则；不是 Minecraft 行为验证 |
| McOverlayAimControlTests (Debug) | 70,321 checks，0 failures | 纯策略数学 |
| McOverlayBedWarsStateTests (Debug) | exit 0，assert 启用 | sidebar/team 纯值逻辑 |
| McOverlayKnockbackTests (Debug) | 17 checks，0 failures | 纯证据策略 |
| McOverlayNavigationTrajectoryTests (Debug) | 43,271 checks，0 failures | 导航/轨迹策略 |
| McOverlayLogicalPipelineHookTests | 156 checks，0 failures | 真实私有 JDK 21 JVM、fixture Java classes、-Xcheck:jni；hook/restore/detach，不是真实 Minecraft |
| Run-OpenGlJvmSmoke.ps1 | PASS，DETACH_COMPLETE | 私有 JVM + WGL 的 Agent 加载、ImGui、WndProc、IPC 和 callback drain；unified thread 模式；明确报告 unsupported client mappings，未建立游戏绑定 |

Transaction L 首次揭示缺失 anchor 会先进入 missing-class watch。补充 MappingService 冷启动 API 门槛后，最终 22 项均通过，并实际输出 `Unsupported Minecraft API version: no verified Version Adapter: 1.12.2`。

## 可复现入口

测试工作临时目录使用 workspace 内 `build-main-release/v56.7-test-tmp`，将 TEMP/TMP 指向该目录；Qt 测试需要 Qt/MinGW runtime PATH。Transaction 使用 `QT_QPA_PLATFORM=offscreen`。

先运行 Test-Resolver.py，再运行 Test-VersionAdapters.py、Test-TwoStage.py、Test-Incremental.py、Prepare-TransactionFixtures.py；传入 `build-main-release`。然后：

```
TransactionCacheTests.exe <absolute FixtureMappingAnalyzer.exe> <absolute resolver-fixtures> <absolute contracts-v1.json>
MappingAnalyzerTests.exe <raw.json> <default-v2.json>
MappingBoundaryTests.exe <absolute default-v1.json>
McOverlayLogicalPipelineHookTests.exe <JDK/bin/server/jvm.dll> <compiled Java fixture directory>
```

Java fixtures 是 tests/LogicalPipelineFixture.java、AssistFeaturesFixture.java、HotbarBindingFixture.java。Run-OpenGlJvmSmoke.ps1 的 HarnessExe、AgentDll、JavaHome 使用当前构建的绝对路径。

## 未验证与限制

- 本轮没有附加真实 Minecraft。1.8.9 Vanilla/Forge/Lunar/Badlion 的真实游戏内操作、渲染、输入与冷/热缓存 Attach 仍未重新验证；字典 parity 与 fixture 回归不能替代这些测试。
- 1.12.2 及其他版本尚不支持；测试中的 1.12.2 只是故意错误的 pack 标签，用于验证拒绝路径。
- 新统一 API 的 JNI 操作使用 mock 检查；私有 JVM hook/WGL 测试没有真实 Minecraft BindingCache，不能证明 mapped 对象的所有权和游戏调用在实际客户端已通过。
- Snapshot 装备/护甲、packed block section、交互、render/input 和 Java hook 的剩余结构假设已记入审查文档，没有宣称完全消除。
- 未运行真实 Lunar acceptance、外部客户端新版兼容验证或全仓所有无关 UI/媒体测试。

## 分发

`package-v56.7-multiversion` 从 Release 安装，windeployqt 补齐 Qt/MinGW；runtime 沿用上一已分发包的 Microsoft JDK 21.0.10 jlink 模块 java.base/jdk.internal.jvmstat/jdk.attach。
已在仅包含 package 与 Windows 系统目录的 PATH 下执行 Arcveil --smoke-test --software-renderer（exit 0），验证独立依赖部署。配置目录隔离到本次 workspace；没有使用用户游戏 PID。
最终压缩包、解压目录、commit 与 SHA256 以包内 BUILD-INFO.json、SHA256SUMS.json 为准。
