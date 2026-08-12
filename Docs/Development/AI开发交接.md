# Catfishing AI 开发交接

更新时间：2026-08-11

文档状态：可进入开发；产品架构已经敲定，工程实现尚未开始。

文档范围：这是交给下一位开发 AI 的单一入口。它说明项目要做什么、前期已经完成什么、当前工程真实处于什么状态，以及接下来按什么顺序实施和验收。它不替代正式 GDD、技术方案或长期接线规则。

## 1. 接手后先建立正确认识

一句话结论：**现在不是继续讨论架构，也不是补某个孤立玩法；现在要按照已经敲定的完整架构，从空 UE 模板开始搭建第一条可运行、可联机、可继续扩展的项目框架。**

实现顺序可以分阶段，但完整目标不是 MVP。阶段只用于控制接线风险和验证顺序，不表示删减最终系统。

### 必读顺序与权威边界

接手后按下面顺序阅读，不要从会议纪要或候选机制稿反推现行方案。

| 顺序 | 文档 | 用途 |
|---|---|---|
| 1 | 本文 | 明确当前任务、工程状态和下一步实施顺序。 |
| 2 | [项目技术方案](../Architecture/项目技术方案.md) | 实现蓝图；查看模块边界、UE 宿主、生命周期、插件、验证矩阵和 R-01～R-32 追踪。 |
| 3 | [FRAMEWORK_WIRING](../../Knowledge/Development/FRAMEWORK_WIRING.md) | 长期稳定的接线规则；确认状态真相、唯一写入口、禁止旁路和重连/清理边界。 |
| 4 | [项目调研基线](../../Knowledge/Development/PROJECT_RESEARCH.md) | 当前工程、环境、构建和资源缺失情况。 |
| 5 | [GDD 系统分册](../../Knowledge/GDD/GDD%20系统分册.md) | 产品规则入口；先看权威顺序，再进入当前功能所属的正式分册。 |
| 6 | [项目架构讨论纪要](../Architecture/项目架构讨论纪要.md) | 只在需要理解“为什么这样决定、哪些旧方案已经撤回”时阅读。 |

发生冲突时分两条线判断：

- 产品规则：用户最新明确裁决高于正式 GDD；正式属主分册高于机制细案和候选鱼表。
- 工程事实：真实源码、配置、资产和新鲜运行证据高于任何文档中的“当前状态”；技术方案和 `FRAMEWORK_WIRING.md` 约束尚未实现的目标接线。

技术方案中的事实等级必须继续使用：`R` 是正式需求，`D` 是已敲定架构，`E` 是已核实的引擎/工程事实，`P` 是拟定实现，`V` 是必须通过原型验证的接缝，`O` 是不能擅自拍板的策划或专项设计项。

## 2. 我们要开发什么

Catfishing 是一款 Windows PC + Steam 的 1～8 人合作/轻对抗湖畔钓鱼游戏。玩家扮演猫，在同一局里钓鱼、协作、争抢渔获、照顾身体状态、回营处理鱼，并通过图鉴和印记保存跨局记忆。

单人和多人使用同一套局结构。单人不是删减玩法的特殊模式，但生成和协作门槛必须保证单人可以完成核心循环，不能刷出必须依靠不存在队友才能处理的鱼。

### 一局的主循环

1. 玩家在 `Frontend` 创建或加入 Steam 会话，进入房主的 `Lake` Listen Server。
2. 白天在湖畔任意合法水边准备装备、投窝料、抛竿、等待咬钩、搏斗和近岸抢抄。
3. 巨鱼可以在搏斗阶段由其他玩家协作；近岸抄鱼不是合作结算，鱼归第一个合法成功抄中的玩家。
4. 实物鱼先进入个人鱼护，再可转入营地共享鱼缸，用于献祭、进食、观赏或受规则限制的偷取。
5. 全队通过献鱼完成当日额度。成功进入普通夜晚；额度失败也必须进入失败结算夜，仍然点火、看照片，然后结束本局。
6. 夜晚没有倒计时。在场玩家共同确认后翻天，继续下一日；达到本局结束条件后进行局末清理。
7. 实物鱼、营地、耗材、猫状态、天气和局内进度随局清空；个人图鉴、印记和已经裁定的解锁跟玩家跨局保留。

钓鱼、渔获、图鉴和印记是四条相关但不能混成一条的数据链。钓到鱼产生局内实物；捕获事实由 Collection/Imprint 幂等归约；成像任务与永久授予分别投递；拥有者客户端最终把 `FProfileGrant` 写入本地档案。服务器不能宣称已经原子写入远端玩家的 `USaveGame`。

### 已确认的产品硬边界

- 平台是 Windows PC + Steam。
- 联机采用 Listen Server，服务器权威；房主退出结束本局，不做 Host Migration。
- 地图只规划 `Frontend` 和 `Lake`。没有 Lobby Map，不做无缝旅行；昼夜全部发生在同一个 `Lake` World。
- 夜晚不钓鱼、不计时，也不使用现实世界时间或离线时间。
- 营地是固定据点，不做建造、经营或搬迁系统。
- 不做 CommonUI、Steam Cloud、持久房间/MMO、公会、排行榜、烹饪和强制任务。
- 经验、等级、成长收益、具体解锁形式仍是 `O`，既不能默认实现，也不能当成永久禁项。
- 缺少正式美术不能阻塞开发。使用白盒、白模和简单占位资源，但表现层接口要允许后续替换。

## 3. 前期已经完成什么

前期工作完成的是产品理解、工程调研和架构设计，不是玩法代码。

| 已完成事项 | 当前产物与证据 |
|---|---|
| GDD 整理与权威分层 | `Knowledge/GDD/` 已包含系统总册和正式分册；下位机制细案、候选鱼表与正式规则的边界已经标明。 |
| 工程与环境调研 | [PROJECT_RESEARCH.md](../../Knowledge/Development/PROJECT_RESEARCH.md) 已记录 UE 5.8.1、构建工具链、空模板状态和资源缺口。 |
| 产品冲突裁决 | 已确认“巨鱼只在搏斗阶段协作、首个合法抄中者得鱼”；旧“双人抄网”不再采用。 |
| 项目架构 | 已确定单 Runtime 模块化单体、UE 宿主、唯一写入者、生命周期、复制和持久化边界。 |
| 技术选型 | GAS + 服务器 StateTree、Enhanced Input、原生 UMG MVC、OnlineSubsystemSteam。 |
| 架构审计 | 需求、架构内聚和 UE 5.8 可实施性三路审计已完成；独立 Review 和最终静态验证均无剩余 finding。 |
| 长期接线规则 | `FRAMEWORK_WIRING.md` 已同步献祭唯一入口、捕获/印记/Grant 分型、重连和清理规则。 |

这些成果的作用是让开发不再“走一步看一步”。下一位 AI 不需要重新发明项目架构，也不能把会议纪要中的撤回方案重新接回工程。

## 4. 当前工程真实状态

当前工程仍是一个可构建的 UE 空模板。

- 引擎：`D:/UE_5.8`，UE 5.8.1。
- 项目：`Catfishing.uproject` 只声明 `Catfishing` Runtime 模块和 Editor-only `ModelingToolsEditorMode`。
- 模块依赖：`Source/Catfishing/Catfishing.Build.cs` 只有 `Core`、`CoreUObject`、`Engine`、`InputCore`。
- C++：只有默认模块入口和空 `MyClass`；不存在 GameMode、GameState、PlayerState、PlayerController、Character、Subsystem、ASC、RPC 或复制实现。
- 资产：`Content/` 当前为 0 个文件；没有 `Frontend`、`Lake`、Input Action、Mapping Context、Widget、StateTree、Gameplay Ability 或 Data Asset。
- 配置：`DefaultEngine.ini` 仍把默认地图指向引擎模板 `OpenWorld`；没有 Steam Session、NetDriver 或项目 GameMode 配置。
- 输入：`DefaultInput.ini` 已指定 Enhanced Input 的默认类，但没有项目输入资产和玩法绑定，因此 Enhanced Input 尚未接线。
- UI：`DefaultGame.ini` 有 CommonUI 模板残留，但项目没有 CommonUI 代码或资产；正式架构明确不采用 CommonUI。

2026-08-11 已重新执行：

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\UnreaProjects\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReload
```

结果为 `Succeeded`，Target 为 `up to date`，总耗时 2.04 秒。这个证据只证明空模板基线可构建，不证明任何新框架、Editor 启动、Cook、Package 或多人行为已经通过。

### 工作区保护

接手时必须先运行 `git status --short`。当前工作区已有用户或前序任务留下的变动：`.gitignore` 修改、`.codex/state` 和两个 `.slnx` 的删除状态，以及尚未跟踪的 `Docs/`、`Knowledge/`。不要重置、恢复、删除、移动或覆盖这些内容，也不要把它们误归因于新的开发任务。

本文和现有文档当前也属于未跟踪文件。除非用户明确要求提交或暂存，不要擅自执行 `git add`、commit 或清理操作。

## 5. 已敲定的工程架构

项目保持一个 `Source/Catfishing` Runtime 模块，在模块内部按领域建立目录。不要把它提前拆成多个 Runtime 模块，也不要先创建一批没有运行用途的空类和转发接口。目录随真实纵向链逐步落地。

统一执行链如下：

```text
Enhanced Input / UMG View
→ PlayerController、UI Controller 或 Ability 形成领域 Command
→ 服务器校验身份、权限、阶段、RequestId、Revision
→ StateTree 编排长流程，或领域服务执行短事务
→ 唯一真相宿主提交 Result
→ GameState / PlayerState / Character ASC / 领域 Actor 复制必要状态
→ Gameplay Cue、动画、音效和 UMG View 消费结果
→ Collection/Imprint 归约捕获与印记事实
→ 客户端本地 Journal FProfileGrant，持久化完成后 ACK
```

### 状态真相和唯一写入者

| 状态 | 唯一宿主/写入者 | 不能放在哪里 |
|---|---|---|
| 局阶段、局时钟、额度、翻天和局级社交策略 | `ACatfishingGameModeBase` / RunFlow（拟定） | UI、PlayerState、客户端 StateTree |
| 公开局快照 | `ACatfishingGameState`（拟定） | 客户端可读但不可写 |
| 稳定身份、在场、翻天确认和重连索引 | `ACatfishingPlayerState`（拟定） | 不放 ASC、身体状态或物品容器 |
| 猫身体、动作和 GAS 状态 | `ACatCharacter` 的 Character-owned ASC（拟定） | 不放 PlayerState |
| 单次钓鱼阶段、参与者和唯一结算键 | `AFishingSession`（拟定） | 不允许 Ability、鱼 Actor 或 UI 各自结算 |
| 实物鱼与容器 | Items Aggregate / `ItemTransaction` | FastArray、Widget、StateTree Task 不能直接写 |
| 献祭跨 Items/Run 的提交进度 | 一局级 Run-owned `USacrificeCoordinator`（拟定） | 不依附 PlayerController、Ability 或临时请求对象 |
| 图鉴/印记候选、CapturePlan 和 Grant 归约 | Collection/Imprint / `URunImprintService`（拟定） | Social 不裁决印记，捕获事务不直产永久 Grant |
| 本地永久档案、相册、隐藏标记和 Grant Journal | `UCatProfileSubsystem`（拟定，本地） | 服务器 Actor、PlayerState 或公共复制状态 |

`RequestId` 负责幂等，`Revision` 负责拒绝陈旧视图。所有共享、竞争和不可逆写入都必须经过服务器 Command/Result；客户端预测、UI Pending、动画结束或 FastArray 更新都不是提交真相。

### GAS、StateTree、UI 与 Social 的边界

- GAS 负责单猫动作、Attribute、Effect、Tag、取消/互斥和 Gameplay Cue。它不负责整局流程、多人 FishingSession 或容器事务。
- StateTree 只在服务器运行，用于 `ST_RunFlow`、`ST_FishingSession` 和确有必要的鱼行为长流程。偷取、转移、捕获终态、献祭和印记候选等短事务不另建 StateTree。
- UMG 使用原生 MVC。View 接受玩家 UI 操作，Controller 转成领域命令，Model 是只读查询/通知；Widget 不保存或修改玩法真相。
- `Social` 负责恶作剧/偷取意图、求助广播和多人互动权限。它不拥有救援玩法本身：倒地、搬运、喂药和恢复归 `Character`；钓鱼过程中的协作救援归 `Fishing`；Social 只决定能否广播、能否互动以及规则是否允许。

C++ 持有权威校验、事务、复制、ID/Revision、Subsystem 和 StateTree/GAS 的项目级 Task/接口。蓝图与资产负责 Widget 布局、StateTree 资产组合、Cue、动画、VFX、音频、资源接线和可调数据；蓝图不能绕过正式 Command/Result 写领域状态。

## 6. 接下来准备怎么开发

下面是完整架构的实施顺序，不是产品范围裁剪。每一阶段都要产生可运行纵向链和新鲜验证证据，不能只增加空目录、空类或未接线资产。

| 阶段 | 目标 | 主要验证 |
|---|---|---|
| A. 工程基线与启动链 | 插件/Build/ini、最小 Core 契约、Frontend/Lake、核心 UE 宿主、日志与退出清理 | 冷编译、Editor 启动、Frontend→Lake→Frontend，无旧 World 引用 |
| B. Steam 会话与玩家装配 | Create/Find/Join/Destroy、Listen Server、StableNetId、PlayerState/Controller/Character | 两进程/双账号 Host、Join、Leave、Host 退出和 Session 清理 |
| C. Character GAS、输入与 UI | Character-owned ASC、基础 Attribute/Ability、Enhanced Input、LocalPlayer UMG MVC | 服务端/拥有客户端初始化、输入激活 Ability、UI/Input 跨 World 重绑 |
| D. RunFlow 与环境骨架 | 昼夜、额度、失败结算夜、翻天、局时钟、环境快照 | 技术方案 V4；失败不能直接清理，夜晚无计时 |
| E. Fishing 与 Items 纵向链 | WaterQuery、窝料、咬钩、搏斗、协作、首抄、鱼护/鱼缸、献祭协调 | V5～V7；同帧只有一个捕获终态，无重复鱼和半提交献祭 |
| F. Profile、图鉴与印记 | CaptureCommittedResult、CapturePlan、两类投递记录、FProfileGrant、Journal/ACK | V8～V9；成像与授予分型，重放不重复，重启可读取 |
| G. 完整领域扩展 | 猫状态、营地、装备、社交、偷鱼、自然事件、鱼表和正式表现 | R-01～R-32 对应验收及 1/2/4/8 人网络矩阵 |

### 下一位 AI 立即执行的第一个开发包

从 **阶段 A：工程基线与启动链** 开始。不要先写完整钓鱼，也不要重新讨论 GAS、StateTree 或 MVC 是否采用。

这个开发包应按以下内部顺序完成：

1. 读取当前有效的全局/项目 `AGENTS.md`、注释标准和 UE 开发规则，先保护现有工作区。
2. 按技术方案 §6.2 接入项目必需插件和 Runtime 直接依赖，清理无效 CommonUI 残留。SteamSockets 仍是 V1 候选，不能照抄旧 `OnlineSubsystemSteam.SteamNetDriver` 配置并宣称定案。
3. 在 `Source/Catfishing` 内先落地第一条链真正需要的 `Framework/Core`、`Online`、`Run`、`Character`、`UI` 等代码；未被当前链使用的领域暂不创建空壳。
4. 创建最小共享 ID、Command/Result、错误码和日志分类。只创建当前 Host/Travel/装配链会使用的类型，不先做“通用框架大全”。
5. 创建并接线拟定宿主：Online GameInstanceSubsystem、LocalPlayer UI Manager、GameModeBase、GameState、PlayerState、PlayerController 和 Character。每个类必须承担真实初始化、查询、旅行或装配职责，不能只是为了目录好看。
6. 创建项目 `Frontend` 与 `Lake` 地图并设置默认入口。资产必须通过 Unreal Editor 或可靠的资产生成路径创建，禁止把 `.umap` 当文本文件伪造。若当前执行环境不能创建资产，应完成可独立验证的 C++/配置部分并明确报告资产阻塞，不能声称启动链完成。
7. 建立最小 Frontend→Lake→Frontend 生命周期。正式 Online 尚未完成的部分可以分小步接入，但最终入口必须收敛到 `UCatOnlineSubsystem` 的异步 Create/Join/Destroy 链，不能留下第二条永久的旁路旅行逻辑。
8. 为每个初始化、旅行、Controller/Character 装配和退出阶段增加结构化日志，让日志可以回答当前 World、NetMode、StableNetId、Session 状态和失败原因。
9. 运行删减审查：删除没有真实调用者的空类、转发方法、接口、状态和配置；保留的每个边界必须对应现有生命周期、平台隔离或后续紧邻阶段的真实需求。

阶段 A 的完成条件：

- 修改后的 `CatfishingEditor Win64 Development` 可以冷编译，而不是只显示旧 Target up to date。
- Unreal Editor 可以启动项目，默认进入项目 `Frontend`，不再依赖引擎模板地图。
- 可以通过正式入口进入 `Lake`，服务端/本地端能观察到正确 GameMode、GameState、PlayerController 和 Character 装配。
- 可以从 `Lake` 回到自己的 `Frontend`，旧 World、Widget、Delegate、Mapping Context 和 Pending 状态没有残留。
- 日志能区分成功、Pending、失败和重复请求，不把旅行请求发出等同于旅行成功。
- 没有 Lobby Map、CommonUI、多个 Runtime 模块、全局事件总线、万能 Item 基类或空 StateTree。
- 有文件级改动说明、运行链说明、构建/启动证据、未验证项和下一阶段入口。

如果只完成源码和配置但没有实际创建/打开地图、启动 Editor 或观察运行日志，只能报告“阶段 A 部分完成”，不能用编译成功代替完整验收。

## 7. 开发时必须守住的规则

### 不要擅自改变的决定

- 不把 ASC 改回 PlayerState。
- 不增加 Lobby Map、无缝旅行或 Host Migration。
- 不让客户端运行权威 StateTree。
- 不让 UI、Ability、PlayerController、世界交互或 StateTree Task 直接写容器、额度、图鉴或档案。
- 不让捕获事务直接生成远端已落盘结论；捕获事实、CapturePlan、Grant 投递和本地 Journal 必须分开。
- 不让外部调用者直调 Items Sacrifice；只能提交 Run-owned `SacrificeCommand`。
- 不把 Social 写成救援状态宿主，也不让 Social 裁决印记候选。
- 不为了扩展性预建没有真实变化隔离价值的接口、组件、事件链和模块。

### 遇到开放项时怎么处理

数值、鱼表、成长、恶作剧频率、求助范围、照片格式和容量等 `O` 项通常不阻塞框架。需要它们才能运行时，使用集中、可调、明确标注的原型默认值，并在交付中列出，不要把默认值写成最终策划结论。

Steam 运输、GAS `Mixed`、重连时序、FastArray 并发、图片故障恢复等 `V` 项必须建立最小原型和观察证据。验证前可以提出候选实现，不能把候选写成“UE 已保证”或“项目已经通过”。

只有下列情况需要先停下来向用户确认：必须改变已敲定架构、必须裁决会直接影响当前实现的数据/权限/持久化语义、需要破坏性修改现有资产或工作区，或者现有引擎/平台证据与技术方案发生实质冲突。

### 每次 Coding 交付的证据要求

- 开始前声明本轮代码注释目标；新增或修改的每个属性、方法声明和方法实现都按当前 `COMMENT_STANDARD.md` 写有语义的中文注释。
- 先运行项目真实构建，再运行与本轮链路匹配的 Editor、PIE、Standalone 或打包验证；验证强度必须和完成声明一致。
- 完成前执行 comment review、删减审查、独立 code review 和最终 verification；发现问题回到实现修复后重新审查完整 diff。
- 不用旧空模板构建记录证明新增代码正确，不用单机 PIE 证明 Steam 双账号联机，不用客户端显示成功证明服务器事务或本地档案已经提交。
- 若某项无法验证，准确写出缺少的工具、资产、账号或运行条件，并把任务状态标成部分完成或 blocked，而不是降低验收标准。

## 8. 当前开放风险和后续交接格式

技术方案 §8.2 已列出 V1～V11。近期风险集中在 Steam Session/NetDriver、Character-owned ASC 生命周期、StateTree 与 GAS 的 Result 接缝、并发抢抄和容器 Revision、献祭跨 Aggregate 恢复、CapturePlan/Grant 两类投递、本地图片/SaveGame 崩溃窗口，以及普通玩家 StableNetId 重连。

无 Steam Cloud、无外部后端是当前明确边界。房主进程直接消失后，服务器内存中的献祭协调进度和尚未送达远端的 Grant 无法保证恢复；不要为掩盖这个边界擅自建设跨主机授权账本。

每个开发包结束时，下一份交接必须至少回答：

1. 本轮目标和非目标是什么。
2. 修改了哪些真实文件、资产、配置和符号。
3. 玩家输入如何经过 Command、服务器宿主、Result、复制和表现走完整条链。
4. 状态真相在哪里，谁是唯一写入者，生命周期何时结束。
5. 运行了哪些构建、测试和人工验证，观察到了什么。
6. 哪些 V/O 项仍未验证或未裁决。
7. 下一阶段从哪个真实入口继续，不能只写“继续完善”。

接手后的首要任务不是产出更多设计文档，而是依据本文和正式技术方案开始阶段 A，并用真实工程证据证明第一条启动链已经建立。
