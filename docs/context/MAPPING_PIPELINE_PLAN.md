# Mapping pipeline 迁移计划

基线：origin/main fd480a8，独立工作区 mapping-pipeline。上一轮 v54 不包含在本次重构。

## 原数据流与 freeze

AgentRuntime 构造 GameBindings → MappingRegistry 注册默认 provider → resolverMain 使用独立 daemon JNIEnv → GameBindings::resolve 首先 freeze → probeEnvironmentHints → 一次 JVMTI GetLoadedClasses 与 defining loader 证据 → exact-anchor/family/priority 选字典 → resolveProfile 查找 JNI IDs → release store 发布不可变 BindingCache。失败清理 global refs；不重试、不在渲染线程扫描、不在 freeze 后热换 registry。

原本具体数据在 MappingProvider.cpp 的四份字典（Forge SRG、Vanilla Notch、Lunar MCP、Lunar Notch），以及 GameBindingsResolve.cpp 的 ordered alias 与 namespace 推断。MappingDictionary 的 247 个已有 symbol 字段是本次保持的逻辑键；附加 10 个字段承载原 resolver 数据。注册 API 与 Gameplay 的 BindingCache 消费方式保留。

## 依赖方向

共享 schema/pack 库 → Agent registry、独立 Analyzer；JVM probe → 运行时元数据/字节码采集；Injector MappingService → Analyzer 子进程、cache、注入前协调；Mapping Console → MappingService JSONL 事件。Gameplay 不读取 pack、不辨认实际混淆名、不参与自动匹配。

## 分阶段提交与门槛

1. v55.1 external versioned pack + strict loader + parity。仅数据迁移，无自动 mapper。以未修改 C++ 导出的全部字段摘要为 oracle；测试 missing/corrupt/unknown-schema/duplicate/late-registration；部署 pack 到 DLL 旁，禁止 cwd 搜索与编译符号 fallback。
2. v55.2 Analyzer inspect/validate/diff、JSONL 协议、运行中 JVM 采集。区分已安装字节码与 retransformation 输入，不把磁盘 jar 当作 transformed class。指纹绑定 schema、loader、运行实例与类内容。
3. v55.3 自动 resolve。结构/hierarchy/descriptor/normalized bytecode/call graph 为主要 evidence，名字弱证据；逐 symbol confidence/ambiguity margin；不足阈值只产生失败候选，不写 verified。
4. v55.4 MappingService 注入前接入与 candidate/verified/previous、原子晋级、rollback、并发/取消/过期处理。registry freeze 前选定；IPC 不支持热替换。
5. v55.5 默认隐藏的 Qt/QML Mapping Console、独立工具操作、端到端验证与打包。

每阶段先执行现有 mapping/Agent regression 与相关新测试，再提交到 Git/GitHub，最后才进入下一阶段。未知 Lunar 版本必须 fail closed，fixture 不代替真实 Lunar 验证。
