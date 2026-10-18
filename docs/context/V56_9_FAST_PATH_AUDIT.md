# v56.9 原版快速路径审计

审计基线：PR #10，`e24b300`；审计日期：2026-10-13。本文先记录修改前的源码和已保存真实运行资料，再列实施边界。后半部分是计划，不代表已经通过真实 Minecraft 验收。

## 历史流程与保留项

| 基线 | 实际流程与差异 |
| --- | --- |
| v54，`7b4744f` | 控制器直接加载主 Agent；`GameBindings::resolve()` 单次发现 anchor，按注册表候选逐个 `resolveProfile()`，成功后发布不可变 `BindingCache`。尚无独立 MappingService / Cache preflight，也没有 Service 的 Unknown family 最终拒绝。内置 Vanilla Notch 与 Lunar Notch 的名称本来就相同；未知 family 允许所有 provider，Lunar priority 1000 高于 Vanilla 100。能够读到成员不等于准确确认客户端身份。 |
| v56.4，`389a472` | `AttachTransaction` 统一所有权；先采集、识别 family，再尝试 family/version scoped Cache，未命中则 authored validation，必要时自动结构解析，再独立验证与最终采集复查。`finalize()` 已明确拒绝 Unknown family。Cache 失败设置 `m_forceAutomatic`，会跳过 authored 重验。 |
| v56.5 / v56.6，`9546a77` / `764d4b8` | Cache 命中改成 compact required binding capture + `validate-cache` + 进程/文件最终检查，避免全量结构扫描；新鲜 `runtimeBinding` 将 loader pin 传给 Agent，Agent `BINDING_READY` 与 Renderer Ready 才允许 Active。保留了前述 Cache 失效直接 automatic 的缺陷。 |
| PR #8，`bc6fe25` | 引入三个独立类型、VersionAdapter 与能力边界；当时只支持 1.8.9。API 支持检查基于 pack 的声明版本，不能作为实际游戏版本的证据。 |
| PR #9，`1f6479e` | Cache 失效改成 `m_skipCacheCandidates`，`choosePack()` 总是先重验 authored，仅失败后再 automatic。该修复必须保留。Resolver 加入缺少运行时类等具体诊断，并保留原匹配阈值。 |
| PR #10，`95add77` / `e24b300` | 新增 1.20.1 mapping / adapter / 版本化 contract；`identify --identity-packs` 只要已加载类签名命中某 pack anchor 就将该 pack 的 gameVersion 放入版本集合，多个版本直接报 ambiguity。该逻辑暴露了不同版本 obfuscated 类名碰撞；没有独立的实际版本依据。 |

旧的 `MappingRegistry::candidatesForAnchor()` 在这些基线没有改变核心规则：Unknown 时不按 family 过滤；已知 family 时过滤；provider priority 仍决定候选先后。问题不能概括成 PR #10 调低验证阈值或改变了原版字典；实际是身份判断、验证候选、最终授权三处规则不一致。

另一个旧机制是 `MappingRegistry::observeClassSignature()` 给“当前注册 pack 中唯一 family 的 anchor”增加 50 分；现代 Vanilla pack 还为 `Lenn;` 配置了 10 分 ExactClassSignature detection。注册表不完整时，唯一候选不能证明客户端身份。本轮已移除 anchor 身份加分和 Vanilla 命名模式身份推断，并将 Unknown 候选限制为 Vanilla 成员数据，保持 environment Unknown。`McOverlayMappingProviderTests` 的 Release 构建与执行通过，覆盖共享 Notch 名称、现代单 Vanilla pack、外部单 Lunar anchor 和既有 Forge/Lunar 正向选择。

## 三种真实回归

以下资料来自用户本机已保存运行，未把新造测试字段写入原始资料。资料目录不包含本文打印的原始 JVM 属性或命令行。它们是“真实 Snapshot 离线回放”证据，不能替代修复后真实 Minecraft 内运行。

根目录：`C:/Users/kkm/AppData/Local/Overlay Studio/MinecraftOverlayManager/mapping-cache-v1/runs/`。

### 1.8.9 Vanilla

- Run：`beef1e76-6c07-410a-acd9-f5ad9cf6776d`，PID 18660。
- 保存有 `index-1.jsonl`、`index-2.jsonl`、`detail-1.jsonl`、`detail-2.jsonl`、`identity.json`、`validation.json` 和 `events.jsonl`。
- 没有 Forge/Lunar launch evidence。真正的 `Lave;` 主类有 96 fields / 106 methods，定义于 AppClassLoader。
- `identify` 输出 family Unknown、version 1.8.9。`validateRuntime()` 在 Unknown 时允许所有 provider 并优先尝试 Lunar，因此 `validation.json` 选中 `minecraft-1.8.9-lunar-notch`，`valid=true`、`injectionReady=true`。
- 验证 receipt 的 `identity.family` 仍为 Unknown。`mappingIdentity()` 构造 runtimeBinding 时却要求 dictionary.family 与 detected family 严格相等，因而 `runtimeBinding=null`。`MappingService::finalize()` 最终拒绝 Unknown，日志第 842 行记录 `stable mapping identity/family unavailable`。

根因是 `Resolver::detectedFamily()`、`validateRuntime()`、`mappingIdentity()` 和 `MappingService::finalize()` 的授权条件相互矛盾。不能通过把 Unknown 直接赋值 Vanilla，或把成员匹配到的 Lunar 字典当作 Lunar 身份来修复。

### 1.20.1 Vanilla

- Runs：`36ff0828-0084-434a-813e-1db334c3b63a`、`8060c377-d765-41fb-933c-d9153797cc73`，均为 PID 24396。
- 两次都有完整 `index-1.jsonl`；`events.jsonl` 第 282 行为 `ambiguous Minecraft API versions in live class loaders`。
- 同一 AppClassLoader 内实际 Minecraft 主类是 `Lenn;`，125 fields / 223 methods。
- 同一 loader 同时加载了 `Lave;`，但它只有 0 fields / 3 methods，super 为 `Lauz;`，方法使用 `com/mojang/datafixers/Typed`、`com/mojang/serialization/Dynamic`。这是现代游戏中的 DataFixer 类，并非旧版 Minecraft 主类。
- `main.cpp::identify` 只检查 `c.name == d.symbols.minecraftSignature`，所以同时加入 1.8.9 与 1.20.1，尚未检查 required members / descriptors / hierarchy / loader 就报错。

根因是将 anchor 名称命中当成版本确认；当前旧字典的 `Lave;` 和现代游戏的普通混淆类天然可能重名。应使用实际版本依据和同一 loader 内的完整必要 API 元数据筛选，不能仅改变 provider priority 来掩盖。

### 1.20.1 Forge

- Run：`fb9c53cb-053c-42c5-8275-26c16c26eba5`，PID 36012。
- 保存有 `index-1.jsonl` 及多个 watch indices、`detail-1.jsonl`、`detail-2.jsonl`、`identity.json`、`candidate.json` 与完整 events。
- launchEvidence Forge 190；`Lnet/minecraft/client/Minecraft;` 定义于 `cpw/mods/modlauncher/TransformingClassLoader`，super 为 `net/minecraft/util/thread/ReentrantBlockableEventLoop`，126 fields / 233 methods。字段为现代 `f_*`，方法为 `m_*`；同 loader 有现代 `SharedConstants` / `WorldVersion`。
- identify 选中了旧 cache object `81269819-e646-4a4f-9c53-5d7d29fd7123/pack.json`，输出 Forge / 1.8.9，`identityComplete=false`、`identityClasses=0`。
- 再次尝试 default-v2 的 1.8.9 Forge SRG，失败后无可信 reference 进入 incremental/watch。后续 unchanged Snapshot 不断触发 retry；即使 family 正确，version 已经从所选旧 pack 错误继承。

根因同样是“候选 pack.gameVersion”被当作实际版本。1.20.1 Forge 尚没有经过审计的专用字典，应在可信版本/家族检测后明确 Unsupported，不能落到 1.8.9 API，也不能把缺少现代支持解释为旧版缺类而持续监听。

## UI 的实际问题

`OverlayManager::attachToProcess()` 在调用 MappingService 前就发出 `mappingAttachRequested`；`src/main.cpp` 将它直接连接到 `MappingProgressController::begin()`。因此任何 Attach（包括 Cache 命中和 authored 正常验证）都会打开自动 Mapping 窗口。窗口不是当前结构解析确实已启动的证据。

修复应复用现有 Controller / QML，只把自动展示的触发条件改成 Service 明确进入 automatic fallback 或需要展示诊断。窗口隐藏/关闭仍不得改变 AttachTransaction 所有权；Cancel 与停止未来自动重试继续保持原有不同语义。

## 计划中的流程

1. 最小 Runtime Snapshot 收集 version / brand / family 的独立候选依据，并附 defining loader；类名命中只建立候选，不授予版本、family 或注入权限。
2. 按确切版本、family、namespace 选择候选。验证成员可以证明兼容 API，不能替代客户端身份依据。Unknown 保持 Unknown，未经独立证据确认不能自动选 Lunar 身份。
3. 已支持 Vanilla 优先完整必要元数据的确定性 authored 快速验证。成功后继续独立 live validation / final check，建立一致的 Verified Runtime Context；不进入结构解析、不打开自动 Mapping 窗口。
4. Forge/Lunar 优先兼容 Cache；失效后保留 PR #9 authored revalidation，相关专用字典全部失败后才允许 Automatic Structural Resolver。共享 Vanilla 名称数据时仍保留原客户端身份。
5. Cache / authored / automatic 的最终 receipt 汇入同一 Runtime Context，绑定实际版本、family、namespace、pack/dictionary、验证依据和当前 loader，避免散落成员互相覆盖。
6. 真正 Vanilla authored mismatch 显示失败符号与 descriptor 诊断。缺少可信 modern Forge mapping 直接 Unsupported。恢复层级按条件执行，不调低 confidence、不接受 provisional、不跳过独立 live / final 校验。
7. 最后由现有 VersionAdapter / Capabilities → GameApi → Agent JNI publication → BINDING_READY + Renderer Ready 完成 Attach；取消与重试保持事务隔离。

实施及验收结果应由后续变更补充；本审计没有启动、附加或修改任何真实 Minecraft 进程。
