# Codex 上下文路由规则

## 按任务加载上下文

1. 每个任务首先读取 [docs/context/INDEX.md](docs/context/INDEX.md)，确定 owning module（职责所属模块）。
2. 确定模块后，只读取对应的 `docs/context/modules/<module>.md`；不要在任务开始时读取所有模块文档。
3. 从模块文档的 `Start here`（现有文档中的“入口 / 最小入口”）进入源码，按当前任务缩小读取范围。
4. 默认不扫描整个仓库、不无差别读取全部源码；禁止仅为“熟悉项目”而读取所有模块。
5. generated、third-party、assets、release notes、screenshots 默认不读取，除非任务明确涉及。

## 跨模块排查

1. 遇到跨模块问题时，再读取 [docs/context/DEPENDENCIES.md](docs/context/DEPENDENCIES.md)，沿实际 dependency edge 逐跳排查，不预先展开全部依赖。
2. 进入相邻模块时，先读其模块说明和 public interface：header、protocol、Qt property/signal/slot、public data structures。
3. 只有 public interface 无法解释问题时，才读取相邻模块实现。读取前先确定具体 symbol，仅定位其对应实现及必要的局部上下文。

## 文档维护

- 源码与 context 文档不一致时，以源码为准；修改完成后更新对应文档。
- 修改改变 module ownership、public interface、cross-module dependency 或 IPC/interface boundary 时，更新受影响的 context 文档。
- 纯内部实现变化不需要无意义更新架构文档。本文件只维护导航规则，不复制模块文件列表、接口列表或依赖图。

## 大型模块化重构

- 仅在实施当前计划中的模块化重构时，读取 [docs/context/REFACTOR_PLAN.md](docs/context/REFACTOR_PLAN.md) 的对应章节。
- 按计划逐模块实施，不一次加载所有大型源码文件。
- 每完成一个模块拆分，先 build 并运行对应测试；验证通过后再进入下一个模块。受阻时记录原因，不把未验证当作通过。
- 优先机械拆分，不改变可观察行为和 public API。
