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

## 需求事实源

- 策划案、GDD 和产品规则的唯一持久事实源是飞书知识库「小猫钓鱼」；本地技术文档、历史调研和代码实现都不能反向覆盖飞书需求。
- 本地只保留 `Knowledge/Requirements/PROJECT_MAP.md` 作为需求入口地图，不再保存 `Knowledge/GDD/` 的策划案副本，也不得重新建立本地 GDD 镜像。
- 任务需要产品规则时，先按入口地图使用 `feishu-context`，以 `user` 身份按需读取当前飞书文档，并在交接中记录所用节点、读取时间和仍未读到的范围。
- 飞书暂时不可读时必须暴露权限或认证缺口，不得回退到旧本地策划案、技术方案中的派生结论或自行猜测需求。

## 唯一禁读材料

- Agent 应按具体任务需要主动读取飞书、源码、配置、资产、测试、引擎资料、项目知识和其他技术文档；本节不规定固定阅读顺序，也不限制其他上下文来源。
- 唯一排除项是 `Docs/Architecture/项目完整蓝图与开发分工.md`。它只供两位人类程序员协调责任；除非用户明确要求维护或审查该文档，Agent 不得读取、检索、引用、总结它，也不得把它加入 Context Pack、Review Packet、任务 prompt 或 handoff。

## 项目问题分析 Harness

- 项目 Harness 的机器入口是 `.harness/harness.json`；所有 Coding 任务启动 Harness 时必须加载其中的 `required_context`。
- Bug、行为异常、回归、失败测试和修改既有行为的任务，进入实现前必须按 `.harness/PROBLEM_ANALYSIS_STANDARD.md` 形成诊断记录，分开观察事实、工程推断和未知项，并给出真实运行链与断点证据。
- 没有确认断点时只允许继续调查，不得用扩大架构、增加旁路、隐藏症状或测试替身代替根因修复。
- Review 与验证必须检查真实产品入口、最小充分改动、证据强度和防复发载体；历史案例只能提供分析线索，不能机械复用修法或测试矩阵。

## 存量审查（不能用绿灯顶替）

- 构建、自动化、冒烟全绿只证明"想到过的行为没退化"，不证明"没有缺陷"。声称一个阶段性任务完成前，审查必须作为**独立一步**执行并留下 findings 清单，不得用验证通过替代。依据与事故记录见 `.harness/CODING_LESSONS.md`。
- 审查不能只看本轮 diff。至少按下列五个可机械搜索的存量维度盘一遍，每个维度给出结论与证据：
  1. **零非测试调用方的公开符号**——重点标注带写副作用的、以及在 `Config/*.ini` 里被显式启用的；
  2. **跨聚合写序列无回滚**——同一函数依次修改两个及以上状态所有者，较早一步不可逆、较晚一步失败只记日志；
  3. **门开着但域关了**——领域写口开关（`CloseCommands` / `bCommandsOpen`）与玩法准入门（`bRunCommandsOpen`）在某些 Run 相位下不一致；
  4. **高频路径上的昂贵操作**——循环体或每帧/每次动作路径里做同步加载、整表校验、哈希、全场 `TActorIterator` 扫描；
  5. **幂等结构配对完整性**——终态缓存与 payload 签名是否存在只写一半、只清一半、签名漏字段。
- 删减审查除按轮跑本轮 diff 外，还必须定期全仓跑一次；只量自己 diff 的删减审查在定义上发现不了存量的未接线实现。
- 设计类与跨领域缺陷优先使用**独立判断面**（外部 Review 或独立子 Agent），不得用主 Agent 自审作为唯一判断面。
