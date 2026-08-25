# Catfishing 项目本地约定

本文件补充全局 `AGENTS.md`，适用于 `D:\UnreaProjects\Catfishing`。

## 源码目录分类

- 新增或迁移 C++ 类型前，先判断类型本身隶属于哪个系统，再决定目录；不要按“它服务谁”“它挂在哪个 Actor 上”“当前谁调用它”来归类。
- `Character/` 只放角色本体、身体生命周期、Pawn/Character 直接职责。挂在角色身上的系统组件不因此自动属于 `Character/`。
- Gameplay Ability、AbilitySet、AttributeSet、Ability 诊断类和 Ability 配置属于 `AbilitySystem/`。例如 `UCatSurvivalAttributeSet` 归 AbilitySystem，而不是 Character。
- 条件、倒地、潮湿、恢复等状态域属于 `Condition/`，即使它们由角色组件承载。
- UI Subsystem、Widget、UI 配置和只读 View DTO 属于 `UI/`。
- Online Session、旅行、邀请、联机策略与对应类型属于 `Online/`。
- GameMode、GameState、PlayerState、PlayerController 等 UE 游戏框架宿主属于 `Framework/Game/`；不要继续把这类“项目大父类”留在模块根目录。
- 模块根目录只保留模块入口、Build 配置和确实跨系统且没有更稳定归属的极少数文件。发现根目录开始堆业务类时，应在验收前整理。

整理目录时必须同步完成文件移动、include 路径修正、构建验证和必要文档路径更新；不要留下“能编译但导航混乱”的半整理状态。

## Harness 与进度入口

- `Docs/Development/需求对齐差距清单.md` 是本项目唯一人工进度入口；`.codex/state/current-harness.json` 与 `.codex/state/current-graph.json` 只保存机器执行状态，不另建业务进度清单。
- Harness 中声明为 `single_atomic_module` 的模块只能维护一个模块级状态。内部实现切面、Agent 节点和验证步骤不得拆成可独立关闭的业务事项；只有整套端到端验收同时成立后才能把模块标为完成。
- 多个分流任务并行写同一工作区时，各模块必须使用 `.codex/state/<module>-harness.json`、`<module>-graph.json` 和 `<module>-context.json` 隔离机器状态，并在 `module_delivery` 中声明对应路径；这些文件不是新的人工进度账本。
- 本项目测试 Agent 必须把证据分成 `contract`、`runtime_behavior`、`presentation_delivery` 三层。Automation、FullLoop、静态盘点或编译绿灯只能证明代码契约或运行链路，不能关闭正式资产、正式 WBP/UI、整场 Run/Environment/Social 行为验收或 Delivery readiness；这些高层缺口必须继续挂在对应模块级状态下。
