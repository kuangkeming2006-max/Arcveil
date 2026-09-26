# game-bindings

返回 [INDEX](../INDEX.md)；依赖见 [DEPENDENCIES](../DEPENDENCIES.md)。`P/` = `Mc_Injector-master/`。

## 责任与最小入口

目标 JVM 内的游戏适配层：映射发现、JNI ID 缓存、取样与游戏逻辑、床扫描、逻辑 hook 的绑定 owner。
先读 [GameBindings.h](../../../Mc_Injector-master/agent/bindings/GameBindings.h) 的数据结构和 public 段；
映射问题才读 [MappingProvider.h](../../../Mc_Injector-master/agent/bindings/MappingProvider.h)。

| 接口 | 消费者 / 语义 |
| --- | --- |
| GameSnapshot、WorldCameraSnapshot、EntityMarker、BedMarker、PlayerIdentity、轨迹结构 | runtime/renderer 使用的值类型快照，不携带可任意跨线程操作的 JNI 所有权 |
| GameplaySettings、updateGameplay | runtime 投影功能设置并执行绑定逻辑 |
| runResolver/registerMappingDictionary/markResolverUnavailable | 解析线程与注册阶段；启动后 freeze |
| runBedScanner/requestBedRescan | 后台扫描与原子/event 重扫请求 |
| sample/sampleCamera/sampleBow/snapshot | 渲染路径取样；snapshot 返回值副本 |
| setInputCaptured/maintainInputReleased/gameScreenOpen | 游戏与 overlay 输入所有权协调 |
| serializeLogicalPacket、silentAvailable/silentAttackAvailable、aimTargetId/aimAttackTargetId | runtime/逻辑钩子的能力、目标和包边界 |
| enqueueDebugChatLine/enqueueWarningChatLine/publishDebugChat | 排队和在正确 JNI 上下文发布本地聊天 |
| release/abandon | 正常 JNI 清理与 VM 不可用时停止访问 |

## 内部边界

MappingProvider 是纯字典/环境/registry：客户端 family 检测、候选映射、schema 校验、注册冻结。
BedWarsState 是语言无关的 sidebar、队伍/羊毛/皮革颜色与 threat 分类。
AimControl、SilentLockCoordinator、TrajectoryMath、SafeWalkPolicy、SmartHotbarPolicy、KnockbackEvidence 各自提供策略或数学/证据结构。
Live*Transform / LiveInteractionObserver 及 MethodWeaver/Native*BridgeBytes 由 [jvm-hooks](jvm-hooks.md) 描述机制；游戏含义和回调决策仍由 GameBindings 所有。

## 实现文件导航（2026-09-26 已拆分）

所有文件位于 `P/agent/bindings/`，仍实现同一个 GameBindings 类。

| 文件 | 职责 / 入口符号 |
| --- | --- |
| GameBindings.cpp | 构造/析构、clearException、deleteGlobalRefs/release/abandon |
| GameBindingsCache.internal.h | private nested BindingCache 完整定义，仅 binding 实现使用 |
| GameBindingsResolve.cpp | registerMappingDictionary、runResolver、环境探测、类查找、resolveProfile/resolve |
| GameBindingsSnapshot.cpp | snapshot、sampleCamera、sampleBow、sample |
| GameBindingsInput.cpp | LWJGL 键鼠、focus、setInputCaptured、maintainInputReleased、gameScreenOpen |
| GameBindingsGameplay.cpp | updateGameplay，内部顺序与完整函数体保留 |
| GameBindingsHotbar.cpp | onItemUse、consumeSmartHotbarPress、暂停/恢复移动、processSmartHotbarRequests |
| GameBindingsImpulse.cpp | suppressKnownImpulse |
| GameBindingsBeds.cpp | runBedScanner/requestBedRescan |
| GameBindingsDiagnostics.cpp | 聊天队列与 publishDebugChat |
| GameBindingsLogical.cpp | logical packet/combat/interaction；唯一 synthetic attack TLS |
| GameBindingsMovement.cpp | movement/sprint/heading/jump；两组唯一 TLS |
| GameBindingsFreeLook.cpp | perspective、endFreeLook、相机旋转/角度 |

resolveProfile、updateGameplay、sample 保持整函数；函数内重构不属于本轮。

## 不变量

- resolve 发布后 BindingCache 与持有的 mapping profile 对读取者保持不可变，直到 callback drain 后释放。不能因为拆分暴露 cache setter。
- JNIEnv 属于线程；global refs 有单一释放路径，local frame/refs 不能随 helper 提取跨出原生命周期。
- snapshot 由 SwapBuffers 渲染线程所有；床 scanner 持有自己的 chunk 状态，经发布缓存交接。
- 逻辑 hook 的 PRE/POST、packet publication、combat geometry 与渲染插值有各自用途；文件迁移不能挪动执行时机。
- 私有 thread_local movement/jump/reentry 状态必须唯一，不能放入各 cpp 自己的匿名空间副本。
- public header 当前包含多种 transform/策略头；renderer 只需要 snapshot 契约，不应沿传递 include 阅读这些实现。
- SmartHotbarPolicy 被 controller 使用是纯值校验例外。现有 Java 版本号源文件与 Bytes header 不由本轮重新生成或选择替换。

## 测试与缺口

已有 MappingProviderTests、BedWarsStateTests、AimControlTests、KnockbackTests、NavigationTrajectoryTests、LogicalPipelineHookTests。
RegressionPolicyTests 按职责文件读取源码，以各完整函数为范围检查顺序，不拼接文件制造顺序。
纯策略/fixture 测试不覆盖实际 Minecraft 客户端映射；无映射 OpenGL smoke 不能证明全部游戏功能正确。
2026-09-26：Agent 构建和上述七个相关测试通过；66 个原成员函数逐字核对为唯一归属。


## v54 行为修复（2026-09-26）

- 开启攻击可用性时，TargetSelector 以 eye-to-hitbox 距离筛选；瞄准点仍用于角度，攻击射线仍独立验证 reach/遮挡。
- movement 与 sprint 接管需要实际 Silent Lock 和 control adaptation；MovementIntentResolver 输出原版八方向及输入自带的每轴减速，物理 tick 仍由消费者提交。
- Smart Hotbar 的 consumed-key callback 现在可以直接改选中 hotbar slot，不调用 syncCurrentPlayItem/windowClick；原版 controller 在正常路径同步手持物品。主背包交换仍排队到 input PRE，并保留动作释放、neutral/resume movement packet 边界。
- 手动切槽会使旧槽位的自动补货请求失效。onItemUse 仍只观察与排队。
