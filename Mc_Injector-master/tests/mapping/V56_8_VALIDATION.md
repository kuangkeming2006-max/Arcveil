# v56.8 Lunar + Forge automatic Mapping recovery

日期：2026-10-12（Asia/Tokyo）。内部版本：`v56.8-lunar-forge-mapping`。
基线：v56.7 `c89de8d`。这是 Mapping 恢复修复，没有放宽自动匹配接受标准。

## 实际日志与根因

用户所述 19,611 类 / 402 detail 对应 run `44f6dbc8-d97c-4b51-a312-bfcee8c388ed`。
本机最新 run `417e8b4b-4498-4a76-89e9-734c9589de09` 为 21,078 / 422；两次均为
41 provisional、216 unresolved、42 required unresolved，之后 STABLE_AMBIGUITY。
原始日志和完整 installed snapshots 保留在本机 mapping cache，未上传仓库。

1. NativeLoader 捕获成功；原 402 detail = 371 个 metadata structural 候选 + 31 个
   hierarchy 依赖，含参考的全部 163 类及全部 55 个逻辑映射类。详细数据有 1,144 字段、
   5,565 方法、268,530 字节 installed bytecode、1,933,581 字节 constant pool。
   非 abstract/native 方法均有 bytecode；双读统计恰为两倍，未触发 buffer/object 限额。
2. 基础类并未漏采。Minecraft 为 120 字段 / 419 方法 / 7 接口；Entity 为 84 / 339 / 6；
   World 为 48 / 311 / 3，与参考数量相同。SRG 类名、成员 descriptor 和 defining-loader
   关系仍可通过 authored exact validation。Mixin 会话生成的方法名称有变化；不能将所有
   installed proof 变化都归结为 nonce，也不能仅由 fingerprint 不同推断映射名称失效。
3. 结构迁移严格拒绝了基础类：Minecraft、Entity、World 分别有 58、4、11 个方法需要
   当前捕获未包含的 BootstrapMethods 属性。installed bytecode 已捕获，但它不足以证明
   invokedynamic bootstrap 等价，因此整类 `supported=false`。AABB 本身结构与引用图一致，
   仍因 Lunar 注入接口未获独立唯一对应被 hierarchy 校验拒绝。参考 163 类中 162 类的
   normalized structure 相同；初始 86 个对应经 hierarchy 检查剩 55 个。
   未发现 namespace 或跨快照 loaderKey 错比较导致基础类失败的证据。
4. 关键程序错误：`fallbackFromCache` 设置 `m_forceAutomatic`，`choosePack` 随即跳过
   当前 authored pack 的完整运行时验证，直接尝试严格迁移。**同一份真实 402 快照和
   最新 422 快照使用 default-v2 直接 validate 均退出 0，Lunar SRG 字典 valid=true、
   injectionReady=true。** 旧 installed-content cache proof 失效不代表该字典失效。
5. 216 个 unresolved 都归类 stable-ambiguity，没有 missing-runtime-class，故暂停是既有
   策略，并非 capture 挂死。最新日志没有 Resume，只见随后 Cancel。旧 Resume 会绕过
   fingerprint skip，却仍可复用旧 detail，无法看到 metadata 不变的 installed-bytecode 更新。

## 修复

- `m_forceAutomatic` 改为 `m_skipCacheCandidates`，只阻止已失败的 cache 候选循环。
  缓存失效后重新捕获 selected detail，先完整验证当前 authored pack，失败才进入迁移。
- 成功仍须独立完整运行时校验、fresh final lite/select/detail、PID/processStart 与 fingerprint
  一致，再原子发布 cache proof/ready。新恢复事件带 generation/cancel 保护。
- Resume 立即发起 fresh Refresh，禁用旧 detail 复用；在途结果、可重试 capture drift 均不会
  吞掉刷新请求。证据仍不足时继续阻止注入并再次暂停。
- Resolver 明确报告 bootstrap/structure/incoming-reference/loader/hierarchy 的拒绝理由。
  confidence、margin、provisional、required 和最终验证规则均不变。
- UI 与 JSONL 版本来自同一 BuildInfo，UI v56.8；事件 v56.8-lunar-forge-mapping。

## 版本核实

v56.7 分发的 Controller/Agent/Analyzer/Probe hash 与其 SHA256SUMS 一致，BUILD-INFO
标明 c89de8d Release。该提交的三处 UI 文案确实仍为 v56.6，这是显示遗漏。
但是本次最新日志 CAPTURE_PATH 中 javaRuntime 和 NativeLoader program 也明确位于
`Arcveil_v56.6-integration`，该目录自身 manifest/hash 对应 56ec30f Debug，与 v56.7
确为不同二进制。不能仅凭目录名称或截图断定当时运行 v56.7；日志证明实际使用了旧目录工具。
本轮没有正在运行的旧 Arcveil 进程，无法反查当日 Controller 的进程映像。

## 最终运行结果

Release Controller/Analyzer/Agent/Probe 构建成功。不是用空 ctest 或仅构建作测试证据。

| 检查 | 结果 / 范围 |
| --- | --- |
| 真实完整 402 / 422 snapshots 回放 | 两份均 Lunar SRG valid/injectionReady=true；生产 Analyzer |
| Test-LunarForge.py | 真实 70 类 / 548 字段 / 3,124 方法 / 62 接口元数据；正例及 required descriptor/static/loader/缺失成员/重复 anchor 五负例通过 |
| TransactionCacheTests | 25 checks, 0 failures；真实 Analyzer/Resolver/validator + 替代 capture，含 stale content -> authored full validation -> fresh final detail -> new proof，和未验证 required 不 ready/promote |
| DynamicMappingServiceTests | 92 checks, 0 failures；含 Resume 新证据、歧义不变、在途 Resume、Refresh capture drift |
| MappingServiceTests | 19 checks, 0 failures；含恢复事件同步取消后不继续旧事务 |
| MappingProgressTests | 26 checks, 0 failures |
| MappingAnalyzerTests | 24 checks, 0 failures |
| MappingBoundaryTests | 32 checks, 0 failures |
| Test-Resolver.py | 257 keys 重命名、缺 reference、歧义、错误 descriptor、unsupported bootstrap 与 hierarchy 拒绝及诊断通过 |
| Test-VersionAdapters.py / Test-TwoStage.py / Test-Incremental.py | 全部通过 |
| McOverlayControllerResponsivenessTests | passed，5,060ms / 10 polls / 253 GUI heartbeats |
| 独立分发包启动 | Windows 平台 + --smoke-test --software-renderer，隔离设置，PATH 仅 package 与系统目录；退出 0 |
| git diff --check | 通过 |

真实 metadata fixture 不含 bytecode/constant pool 和进程身份，因此它只证明 exact member/
loader validation，不能证明结构自动迁移或 live injection。完整快照回放使用本机实际内容，
但仍是离线验证。本轮没有正在运行的游戏 JVM，未重新进行真实 Lunar Agent 加载、renderer
ready 或游戏操作测试。没有把这些未运行项目记为通过。

## 复现

Qt/MinGW runtime 放入 PATH，TEMP/TMP 使用 workspace 内目录。依次运行：

```
python Mc_Injector-master/tests/mapping/Test-Resolver.py build-main-release
python Mc_Injector-master/tests/mapping/Prepare-TransactionFixtures.py build-main-release
python Mc_Injector-master/tests/mapping/Test-VersionAdapters.py build-main-release
python Mc_Injector-master/tests/mapping/Test-TwoStage.py build-main-release
python Mc_Injector-master/tests/mapping/Test-Incremental.py build-main-release
python Mc_Injector-master/tests/mapping/Test-LunarForge.py build-main-release
```

事务测试参数仍是绝对 FixtureMappingAnalyzer.exe、resolver-fixtures、contracts-v1.json；
Dynamic 测试传入 FakeDynamicMappingAnalyzer.exe。相关可执行测试均显式运行。

## 分发

`package-v56.8-lunar-forge-mapping` 从最终 Release 安装并用 windeployqt 部署 Qt/MinGW。
Java runtime 使用此前 v56.7 包的 Microsoft JDK 21.0.10 jlink（java.base、jdk.internal.jvmstat、
jdk.attach）。同一完整包压缩为 zip 并再次解压到 `Arcveil_v56.8-lunar-forge-mapping`。
准确 commit、逐文件 SHA256 和启动入口见包内 BUILD-INFO.json、SHA256SUMS.json、START-HERE.txt。
