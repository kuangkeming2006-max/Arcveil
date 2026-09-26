# jvm-hooks

返回 [INDEX](../INDEX.md)；依赖见 [DEPENDENCIES](../DEPENDENCIES.md)。`P/` = `Mc_Injector-master/`。

## 这是一个逻辑模块

本模块横跨 `P/agent/` 和 `P/agent/bindings/`，没有独立 CMake 库边界（MinHook 第三方目标除外）。
负责进入 JVM、取得线程环境、拦截宿主调用和安全回调；不拥有控制器页面或业务网络请求。
不要把不同类型的 hook 当作同一清理协议。

## 三类入口

| 机制 | 契约 | 实现导航 |
| --- | --- | --- |
| JVM bootstrap / native export | Agent_OnLoad、Agent_OnAttach、Agent_OnUnload、McOverlay_Start；AgentRuntime.start/stop | [jvm.cpp](../../../Mc_Injector-master/agent/jvm.cpp)、[McOverlayAgent.def](../../../Mc_Injector-master/agent/McOverlayAgent.def) |
| VM 环境 | resolveJavaVm、resolveJvmti、ScopedThreadEnv | [jvm.h](../../../Mc_Injector-master/agent/jvm.h) |
| OpenGL detour | OpenGlHook.install(FrameCallback, context)、disable/waitForIdle/remove | [opengl_hook.h](../../../Mc_Injector-master/agent/opengl_hook.h)、对应 cpp |
| Win32 window subclass | WndProcHook.install(Handler, context)、restore/installed | [wndproc_hook.h](../../../Mc_Injector-master/agent/wndproc_hook.h)、对应 cpp |
| Java 方法变换 | 各 Live*Transform 的 install/ready/setEnabled/stop/abandon（具体签名以对应头为准）；注册的 JNI callback | `P/agent/bindings/Live*Transform.h`、`LiveInteractionObserver.h` |

## 调用与所有权

jvm.cpp 的导出把启动委托给 bootstrap/runtime；DllMain 不执行 JVM 初始化或 hook 工作。
resolveJavaVm 优先使用 JVM 正式传入的 VM，普通 DLL 入口才需要查找已有 JVM。
ScopedThreadEnv 只对自己附着的线程承担 detach，JNIEnv 不能缓存给其他线程。

OpenGlHook 在 SwapBuffers/SwapLayerBuffers 路径调用 FrameCallback → AgentRuntime.frameEntry。
active detour 计数与 dispatch lock 确保卸载时不会在指针已被选中但计数未增加的窗口释放对象。
WndProcHook 用独立 dispatch state；restore 会停止新分派、在所属窗口线程恢复链并排空旧回调。超时保留 inert state 是既有行为，不能在机械重构中改为强制 delete。

## Java 变换边界

- LiveMovementTransform、LiveJumpTransform、LiveHeadingTransform：movement/jump/heading/sprint 等方法边界。
- LiveInteractionTransform、LiveAttackTransform、LiveInteractionObserver：交互 PRE/POST、攻击仲裁/观察。
- LivePacketTransform：包发布边界。
- LiveFreeLookTransform：相机边界。
- LiveHotbarTransform、LiveImpulseTransform：按键/item-use/impulse 等适配。
- 对应 MethodWeaver 与 Native*BridgeBytes 提供 class 变换/桥接；Native*Bridge_v*.java 为桥接源码。

GameBindings::updateGameplay 注册带 owner 的回调并进行业务 gating；callback 又调用 GameBindings 的 private 方法。
这是显式 callback 契约，变换类不应 include GameBindings.cpp 或探测其 BindingCache。
构建清单没有把这些 Java 源逐个列为独立 Agent C++ target source；应先查 Bytes header 的实际 include，不能按最高文件版本号臆测生效桥接。

## 构建、测试与读取策略

Agent 为 Windows x64，仅取 JNI/JVMTI 头，不链接 jvm.lib。
MinHook v1.3.4、Dear ImGui v1.92.9 的版本是当前 CMake 声明；机械拆分不更新它们。
导出与 bootstrap 用 AttachSmoke/NativeLoaderSmoke；帧与 drain 用 OpenGlJvmSmoke（unified/split）；Java 变换用 McOverlayLogicalPipelineHookTests 和 Java fixtures。
这些 fixture 不等于所有客户环境/映射通过。

只读所需 hook 的 header 和注册点；排查 stop/drain/异常时才下钻 cpp 或 header 中相应实现。一般 controller/QML 任务不需读取 MethodWeaver 或桥接字节数组。

