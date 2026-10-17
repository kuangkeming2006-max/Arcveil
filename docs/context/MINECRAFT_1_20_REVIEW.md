# Arcveil v56.9：1.20.1 审查与增量实施

## 基线和范围

2026-10-13 核实 origin/main 为 `764d4b8`（v56.6），尚未包含 v56.7、v56.8。
独立分支从该 main 建立，先 cherry-pick 两个前置提交，再实现本轮；Draft PR
因此包含这两个前置提交。没有把已有功能复制成另一套程序。1.12.2 不在范围内。

职责入口：game-bindings → mapping-pipeline → agent-runtime/jvm-hooks/renderer。
本轮依据当前 GameBindings、MappingRegistry、MappingService 和 AttachTransaction 源码，
延续已有的轻量接口；没有新增完整 M* 包装层或替代 Registry/Cache。

## 源码确认的差异与处理

| 边界 | 原有实现及实际问题 | 本轮处理 / 复用限制 |
| --- | --- | --- |
| 版本、family、namespace | GameVersion 分离三个维度，但 Adapter 仅允许 exact 1.8.9；gameVersion 原为 pack 声明，不能独立证明实际 JVM 版本 | 增加 exact 1.20.1 allowlist；catalog 的已加载 anchor 仅选候选版本。Agent 另调用 SharedConstants.getCurrentVersion().getName()，必须精确为 1.20.1 才发布绑定 |
| Mapping | default-v2 的 257 keys、ordered aliases 和 required 契约包含大量 1.8.9 结构，不能改名后整体套用现代类 | 保留 legacy pack/contract 原数据；新 Vanilla pack 52 keys，现代独立契约；版本契约 bundle 在原 Resolver 内分派 |
| JVM / Attach | agent/jvm.cpp 动态解析已有 JVM；JNI_VERSION_1_6、JVMTI_VERSION_1_2、daemon attachment，并不要求目标 Java 8 | 保留机制。实际 Java 8/17 私有 JVM、Java 17 Probe Attach 和完整 Agent 验证通过。官方 1.20.1 元数据要求 Java 17；没有在真实游戏执行注入 |
| ClassLoader | v56.5 的 anchor + defining-loader type/instance pin 已由当前 capture receipt 绑定，Cache 不能提供当前 loader authority | 继续使用；现代核心类通过同一个 Minecraft defining loader 加载，并校验实际 defining loader、继承关系和每个 required ID。JVMTI 批量类查询前预留 JNI local capacity，避免 Java 17 checked-JNI 警告 |
| Entity | legacy 直接位置/朝向字段、List，现代公开 getX/Y/Z、getYRot/XRot；生命周期 isRemoved/isAlive | 新 GameBindingsModern.cpp 封装调用结构；仍输出同一个 GameSnapshot/EntityMarker，不修改 AimAssist/ESP 等模块算法 |
| Player / Level | LocalPlayer 与远程 Player 基类不同；ClientLevel.entitiesForRendering 返回 Iterable，不能按 legacy java.util.List 访问 | 映射两种玩家类；有界 Iterator 遍历 4,096 项，过滤自身/移除/死亡实体，遵循原 marker 上限，截断显式标记；校验 JNI 对象类型、有限数值和 AABB 顺序 |
| 名称 / 碰撞箱 | Component 与 legacy IChatComponent 的成员名称/descriptor 不同；AABB 类型不同 | 名称和 descriptor 来自新字典/契约；现代 adapter 读取 Component 字符串和六个 AABB double，不把名称分支写进模块 |
| 输入 | legacy LWJGL2 Mouse/Keyboard 静态接口和 focus 字段不适用于 GLFW MouseHandler | MouseCapture capability；现代 releaseMouse/grabMouse/isMouseGrabbed，主线程操作，仅恢复原先抓取且没有游戏 Screen 的鼠标。键盘移动接管、攻击/交互未实现 |
| Tick / 网络 / 交互 | GameBindingsGameplay 安装的 movement/combat/input/packet transforms 依赖 1.8.9 方法和字节码；MultiPlayerGameMode、InteractionHand、现代交互返回类型不同 | 通过 LegacyGameplay capability 在入口阻止这些路径进入现代对象；现代未安装 Java tick/render/network transforms。仍在现有 WGL 帧回调中、确认游戏主线程后采样 |
| 网络包 | 旧 C03/C04/C05/C06 与现代 ServerboundMovePlayerPacket 的 Pos/Rot/PosRot/StatusOnly 层级不同；连接与交互 API 也改变 | 不仅是成员重命名。未来须实现现代 adapter 的交互/事件与独立字节码验证，本轮关闭相关能力 |
| BlockState / 床扫描 | 旧 ExtendedBlockStorage/char[] 假设不能用于现代 LevelChunkSection/PalettedContainer<BlockState> | BedScanning 关闭，原 scanner 已有 capability gate。现代 Level 基础绑定已完成，BlockState/区块读取尚未实现 |
| 渲染 | WGL SwapBuffers Hook 可复用；OpenGL2 renderer 和 legacy FloatBuffer 矩阵不可用于现代 core context | 按实际 GL context 选择原 OpenGL2 或 ImGui OpenGL3；core 下停用使用已移除 GL API 的 blur。WorldProjection 关闭，现代 camera/JOML/projection 未实现，所以不宣称 ESP 世界投影或 AimAssist 可用。D3D11 是控制器 Qt 路径，不承担 Minecraft 的现代渲染 |

## 分阶段实施及当前结果

1. **最小初始化链**：官方产物校验 → 52-key authored dictionary → catalog 选择 → 原
   Probe/Resolver 完整验证 → 原 final capture/cache proof → 新鲜 loader pin → Agent
   独立 exact-version 和 required JNI 校验 → BINDING_READY + RENDERER_READY。
   这些生产部件已分别在私有 Java 17 synthetic game JVM 中运行，MappingService 冷启动及
   Cache 复验通过。它们不是一次真实 Minecraft/控制器注入会话的证据。
2. **基础只读数据及必要输入**：Minecraft/LocalPlayer/Player/Level/Entity、坐标、前一帧位置、
   朝向、血量、名字、AABB、实体枚举、世界 generation、GUI mouse capture 已实现。
   Modern120Adapter 只声明 EntitySnapshots、MouseCapture；InventoryAccess、BedScanning、
   LegacyGameplay、WorldProjection 均关闭。绑定 label 和 Agent 日志明确说明只读范围。
3. **Forge**：尚未实现，family evidence 独立于 pack provider 检测；识别为 Forge/Lunar
   的 1.20.1 不能使用 Vanilla mapping 获得 Ready。先真实 Vanilla 验证，再收集 Forge
   的运行时 namespace/loader/required bindings；必要时添加字典并复用 Modern120 API。

对尚无映射的运行时，catalog 的 anchor 候选并不是通用的语义版本探针；完全陌生 anchor
可能停留在 missing-mapping/watch 诊断，不能可靠报告该客户端的具体版本号。
这不会授权 Ready。已声明但无 Adapter 的版本明确拒绝；冒充 1.20.1 的邻近版本
即使成员兼容，也被 Agent 的实际版本检查拒绝。未来可增加独立版本资源证据，不能拿
Java major、客户端品牌或 anchor 名称代替独立运行时 API 验证。

## Mapping 来源与可复现性

Vanilla 1.20.1 运行时为混淆名称；官方 Mojang mappings 把语义名称对应到该产物的
混淆类/成员。字典以项目现有 `Notch` namespace 表示这种运行时命名，并非 Mojang named
development runtime。没有根据网络文章猜名字，也没有把 Vanilla Mojang 名称当作 Forge SRG。

`P/mapping/tools/Generate-ModernPack.py` 不联网，要求官方文件 SHA-1 精确匹配，并检查
实际 classfile 的字段/方法 descriptor、static modifier 和继承查找。结果为 52 keys、
37 个具体成员校验；provenance JSON 包含哈希、URL、语义成员和离线审计。
只保存选用的映射事实；完整 client.jar/client.txt 留在本地 ignored research，不随程序分发。
官方产物校验不是运行时验证，也不会设置 injectionReady。

- [官方版本 manifest](https://piston-meta.mojang.com/mc/game/version_manifest_v2.json)
- [1.20.1 官方 client mappings](https://piston-data.mojang.com/v1/objects/6c48521eed01fe2e8ecdadbd5ae348415f3c47da/client.txt)
- [1.20.1 官方 client JAR](https://piston-data.mojang.com/v1/objects/0c3ec587af28e5a785c0b4a7b8a30f9a8f78f838/client.jar)
- [GLFW OpenGL context 兼容说明](https://www.glfw.org/docs/latest/compat_guide.html)

## Cache / 生命周期不变量

- v56.8 authored revalidation 恢复逻辑、优先级、confidence/margin、provisional、最终
  runtime/PID/start continuity 均保留。未修改 MappingCache 持久化格式。
- bundle 只选择子契约；旧 1.8.9 canonical analyzer identity 不变。Controller 对语义一致的
  子契约继续使用原 standalone 原始字节摘要；编辑或丢失子契约/standalone 会失效旧 proof。
- 不因 Cache 存在跳过 fresh compact/full validation；现代绑定还额外检查 actual version。
- Immutable BindingCache release/acquire publication、callback drain、single-owner global ref
  cleanup 保留；新增 class global refs 进入同一 cleanup。borrowed GameApi 无新所有权。
- 不改 IPC、AttachTransaction、resident Probe-v13 请求格式。完整 Agent smoke 使用测试
  JVM 内生成的 pin；另有生产 Probe/Analyzer/MappingService 测试验证 pin 来源。

## 真实环境验收步骤与后续版本

1. 使用官方 Java 17 启动 **Vanilla 1.20.1**，先主菜单附加；保存 Controller mapping JSONL、
   Agent 日志和 runtimeBinding。确认实际版本、fresh validation、两个 READY 才进入 Active。
2. 进入单人世界，检查玩家坐标/血量、远程玩家及其他实体、AABB、世界切换/退出；开启/关闭
   GUI，确认鼠标释放/恢复；重复 Attach/Detach，检查引用、线程和渲染状态。
3. 冷 Cache、热 Cache、错误 descriptor、缺 required member、邻近版本误标、普通/替代
   transport 均验收；误标、缺失或 unsupported API 必须阻止 Ready。
4. 回测 1.8.9 Vanilla/Forge/Lunar 的实际游戏与功能，本轮只有自动化与已有元数据回放。
5. 完成 Vanilla 后再加 1.20.1 Forge；不要只删除 family guard。

1.20.2–1.20.6 仍须逐产物生成可信 mapping、审核 API/Java/网络变化、补充 exact allowlist
及跨版本负例，再独立进行运行时验证；能共享时复用 Modern120Adapter，不能仅改 gameVersion。
现代投影、inventory/BlockState、交互/网络 hooks 应各自实现并验证后再开放 capability。
本轮不预建空的未来 Adapter。具体实际测试记录见 `P/tests/mapping/V56_9_VALIDATION.md`。
