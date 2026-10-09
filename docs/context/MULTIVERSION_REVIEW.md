# Arcveil 多版本兼容审查与最小增量 — 内部 v56.7

日期：2026-10-10。源码是事实来源；本轮没有宣称已支持 1.12.2。

## 审查结论

现有 GameBindings 已经是 runtime/renderer 使用的游戏门面，GameSnapshot 是 ESP 等消费者的值类型边界；AimControl、TrajectoryMath 等策略不依赖 Minecraft 名称。没有必要另建一套模块包装类或复制 AimAssist/ESP。

实际问题在 binding 内部：MappingDictionary 原先没有 Minecraft 版本或 namespace 身份；pack 的 gameVersion 没有传给 Agent 字典，也没有用于拒绝不支持的 API。GameBindingsResolve 无论 pack 声明哪个版本，都按照 1.8.9 结构生成大量 JNI descriptor。GameBindingsGameplay/Hotbar 直接把主背包解释为 ItemStack[]，多个 binding 文件重复选择静态字段/方法获取 singleton，Snapshot/Gameplay 重复选择实体列表的字段/方法。

### 当前检测、选择和验证的真实边界

- `MappingProvider.cpp::ClientEnvironment` 使用 loaded-class signature 与安全 launch hint 的置信度，取各 family 最大证据。相同置信度的既有优先次序为 Vanilla → Forge → Lunar → Badlion，后者获胜；不改动这套规则。
- `GameBindingsResolve.cpp::findMinecraftClass` 只有一次 loaded-class 表扫描，匹配确切 anchor，并检查 Controller 传入的 defining-loader type/instance。`MappingRegistry::freeze` 仍按 provider priority 降序；`candidatesForAnchor` 仍按 family 与确切 anchor 过滤。无 family 证据时仍尝试同 anchor 的既有候选。
- 原先 namespace 隐含在选中的字典和其中的 ordered aliases；不存在独立可信的 namespace 探针。本轮 schema-2 明确记录 authored namespace；schema-1 保留 Unknown，绝不根据 family 或 ID 猜测。自动结构迁移后的 schema-2 字典标为 Custom。
- 原先没有独立、可信的 Minecraft 语义版本探测。Analyzer 的 minecraftVersion 来自 pack.gameVersion，Cache 也使用此声明。现在显式解析为 MinecraftVersion 并传入每份字典；它仍是**声明的目标版本**，不是从 family 推导的运行时事实。
- `MappingDictionary::validate` 验证身份、核心/可选类名与 signature 配对、必需 accessor 的互斥、符号合法性等。Analyzer `dictionaryValidation` 另按 contracts-v1 验证实际类层次、精确 descriptor、staticness 和 loader；schema-only 不授予 injectionReady。
- `GameBindingsResolve::resolveProfile` 最后仍通过实际 JNI 查找 ID；可选映射失败仍按旧规则禁用对应功能。全部成功后再一次性发布 immutable BindingCache，release/acquire 和 callback drain 不变。

### 哪些属于 Mapping，哪些属于 API 结构

Mapping 继续拥有类名、成员名、类型 signature、ordered alias 和已显式提供的 descriptor（如 setupTerrain）。静态 singleton 字段/方法、实体列表字段/方法是已有字典表达的 accessor 差异，不根据 namespace 写版本分支。

以下属于 API/行为假设，单纯重命名不能解决：

| 源码入口 | 现存结构/行为假设 | 本轮处理 |
| --- | --- | --- |
| GameBindingsResolve / Gameplay / Hotbar | ItemStack[] 主背包及 slot 访问 | descriptor 形状归 VersionAdapter；统一 InventoryView 的 size/at 隔离数组读取 |
| GameBindingsSnapshot | 实体 XYZ double、yaw/pitch float、AABB、相机矩阵缓冲、装备槽位与护甲数组 | 继续使用既有 ID；仅公共对象/实体列表读取收口，其他假设列为下一步 |
| GameBindingsBeds | ExtendedBlockStorage、char[] packed block 数据、数字 block ID | 用 BedScanning capability 明确限制；尚未抽取新存储实现 |
| GameBindingsGameplay / Logical / Movement / FreeLook | 老式 right-click/windowClick 参数与返回值、movement packet、物理 tick 和 Java hook 签名 | 保留验证过的 1.8.9 实现，整体受 Adapter allowlist 保护；不把这些假装成通用接口 |
| AimControl / TrajectoryMath / renderer world drawing | 统一值类型、几何与策略 | 无需复制或修改版本逻辑 |

参考本地 `Vape-v4.21-main.zip` recovered 源码中的 `mapping/mappings/MWorld.java`、`MPlayerControllerMP.java`、`MMinecraft.java` 和 `MappingProfileSnapshotRegistry.java`：版本差异不仅是成员名，也包括调用参数、返回类型与访问结构。本轮只借鉴把调用形状差异集中隔离的思想，没有引入其 M* 包装体系，也没有把 recovered 字符串当作已验证的 Arcveil 映射。

## 本轮引入的边界

```
GameBindings public API / GameSnapshot / GameplaySettings（保持）
    → BindingCache::gameApi()（非 owning、无分配的局部访问视图）
    → VersionAdapter + VersionCapabilities（不可变、按 API 结构组织）
    → MappingDictionary / MappingRegistry / 原 JNI ID resolver
    → JNI / JVMTI
```

- `GameVersion.h`：独立 MinecraftVersion 与 MappingNamespace；ClientFamily 沿用 MappingProvider。
- `VersionAdapter.h`：一个 Legacy18 API policy；只接受确切 1.8.9。以后经验证可让多个版本复用同一个 policy，不能用版本范围推测兼容性。
- `GameApi.internal.h`：统一 singleton、player、world、entities 与主背包读取。它不缓存对象、不创建/删除 global refs、不清异常、不管理 local frame，也不保留 JNIEnv。InventoryView 是借用的局部视图，不能跨线程/帧保存。
- `GameBindings::versionCapabilities()` 返回 API 支持上限；可选 JNI ID 和实际 hook readiness 仍会进一步限制功能，不能把 capability 当作功能已就绪。
- Agent 在任何 profile 专用 JNI lookup 前拒绝无 Adapter 的版本。Analyzer full/cache/resolve 路径同样拒绝；MappingService 冷启动在进入缺类 watch 前拒绝不支持的声明，收到明确 unsupportedApi receipt 也终止，不无限重试。

## Mapping pack 与缓存兼容

`default-v1.json` 不变，schema-1 仍可读，字典 namespace 为 Unknown。`default-v2.json` 更新为 schema-2，仅新增明确 namespace 元数据；原有五份字典的符号、检测证据、provider priority、alias 顺序均保留。pack-level gameVersion 适用于本 pack 所有字典；序列化拒绝字典版本与 pack 不一致。

新版本可以先作为字典数据被解析/注册；这不授予 API 支持。没有验证过的 Adapter 时禁止 injectionReady。缺文件、hash 不符、错误 signature 和不支持的 API 保留具体诊断。

AttachTransaction 的 PID/start、generation、状态与握手提交边界未改。MappingProbe-v13 的捕获协议、双读、loader 证明没有改。MappingCache 的 pack/snapshot/contract digest、analyzer revision、promotion/rollback 和实时复验机制没有改。schema-2 元数据自然改变 pack digest，且 schemaVersion 已参与 identity；旧 schema-1 的验证过缓存仍须经过现在的 API 门槛和原有 compact live validation。cache hint 永远不是授权。以后改变 API descriptor/hook 合同时必须同步版本化 contracts/proof revision，不能只改 Adapter 标签。

## 文件变更范围

| 文件组 | 内容 |
| --- | --- |
| agent/bindings/GameVersion.h、VersionAdapter.h、GameApi.internal.h（新增） | 身份、API policy、统一读取视图 |
| GameBindings.h/.cpp、GameBindingsCache.internal.h、GameBindingsResolve.cpp | capability 契约、视图、Adapter 选择与诊断 |
| GameBindingsSnapshot / Gameplay / Hotbar / Beds / Input / FreeLook / Logical / Diagnostics | 复用对象访问；主背包读取；scanner capability |
| MappingProvider.h/.cpp、MappingPack.cpp、mapping/packs/default-v2.json | 版本传播、schema-1/2、namespace、载入错误 |
| mapping/analyzer/Analyzer.cpp、Resolver.cpp | API 门槛、namespace diff/remap、明确不支持 receipt |
| src/MappingService.cpp、MappingServiceDynamic.cpp | 冷启动及 receipt 的终止诊断 |
| agent/CMakeLists.txt、GameApiTests.cpp、MappingProviderTests.cpp、tests/mapping/MappingPackTests.cpp、Test-VersionAdapters.py、TransactionCacheTests.cpp | 新接口测试及现有回归扩展 |
| context 模块页、DEPENDENCIES、mapping/README、验证记录 | 新公共边界和实际验证范围 |

## 加入 1.12.2 还需要什么

1. 提供来自真实目标运行时并验证的 MappingDictionary/pack，明确版本、family 和 namespace，保留 defining-loader 与 live proof；不能重贴 1.8.9 标签或猜成员名。
2. 逐项验证公共 API 与 contracts。主背包集合、交互参数/返回值、block section 表示、渲染/输入和 hook/packet 边界必须按目标源码/运行时审查。可以复用一致的基础操作，只补实际不同的操作。
3. 为确有不同的操作补 Adapter shape/实现、descriptor 合同和对应 capability；尚未实现的 scanner/hook 等 capability 必须关闭，同时确保缺失的可选功能不会被核心绑定错误地要求。
4. 当前 resolver 仍把床扫描核心类等作为 required，这些要求也须按 capability 分组；本轮没有把完整 1.12.2 仅渲染路径假装成已经可用。
5. 经过模拟、私有 JVM、真实 Vanilla/Forge/Lunar 等目标验证后，才加入版本 allowlist。AimAssist/ESP 的策略和快照消费不应复制。

这是一轮可复用访问边界和拒绝门槛的增量，不是全部版本假设已经隔离完毕的声明。实际验证见 `Mc_Injector-master/tests/mapping/V56_7_VALIDATION.md`。
