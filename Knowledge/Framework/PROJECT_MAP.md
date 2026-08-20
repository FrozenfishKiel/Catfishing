# Catfishing 当前框架项目地图

更新时间：2026-08-20
文档状态：当前源码事实地图（文件级）。
范围：给后续程序员和 AI 检索使用。说明每个功能域拥有什么、包含哪些文件、被谁调用、配置和资产挂在哪、跨域链路怎么走。本文不替代飞书 GDD、数值表或验收报告，也不描述实现进度。

## 怎么用这份地图

这份地图存在的目的，是让你**不必把整个项目读进上下文**。正常用法是三步：

1. 在「按任务找入口」里定位你要改的东西属于哪个域。
2. 只读那个域的小节，拿到文件清单、关键类型、真相归属和上下游。
3. 需要跨域时再看「跨模块运行链路」，确认改动会穿过哪些边界。

判断某个功能实现到什么程度，只能看 `Source/Catfishing` 真实源码、构建输出和 `Saved/Automation/` 的新鲜结果。任何文档（包括本文）都不是实现进度的证据。文末「当前 fail-closed 与断链」记录的是本轮实际读到的接线状态，它同样会过期，用之前先复核源码。

其他入口：

| 要什么                                     | 去哪                                                                    |
| ------------------------------------------ | ----------------------------------------------------------------------- |
| 产品规则、玩法数值、策划裁决               | 飞书知识库「小猫钓鱼」，用 `feishu-context` 按需读；本地不保存副本      |
| 项目本地约定、目录归属规则、需求事实源边界 | `AGENTS.md`                                                             |
| 问题分析与根因标准                         | `.harness/PROBLEM_ANALYSIS_STANDARD.md`                                 |
| 历史教训                                   | `.harness/CODING_LESSONS.md`                                            |
| Harness 机器入口与必读上下文               | `.harness/harness.json`                                                 |
| 当前开发范围、工作包、完成标准             | `Docs/Development/项目开发工作计划.md`                                  |
| 详细技术来源                               | `Docs/Architecture/项目技术方案.md`（其中的阶段性描述不得覆盖真实代码） |

本文是 `Knowledge/` 下当前唯一的文档。过去的 `RULES.md` / `DECISIONS.md` / `KNOWN_ISSUES.md` / `TERMS.md` / `FRAMEWORK_WIRING.md` / `PROJECT_RESEARCH.md` / `Requirements/PROJECT_MAP.md` 均已删除，看到旧引用不必去找。它们承载过的内容，凡属当前源码事实的都已并入本文（真相归属、跨域链路、容易误判的地方在「当前 fail-closed 与断链」与「审查基线」两节），凡属产品规则的一律回飞书。

构建与运行证据看 `Saved/Automation/<run-id>/` 下的真实 Automation 报告与日志，以及 `.codex/state/current-harness.json` 记录的本轮验收证据。`.codex/docs/` 只在 Harness 明确要求产出测试、验收或 Review 文档时才有内容；没有对应文档时不要假设证据存在。

**一条持续生效的范围约束**：当前开发把 `Fishing/**` 内部状态机、Fishing StateTree 和钓鱼专用 UI/Input/表现视为外部纵向边界，不在外围任务里顺手修改。外围领域只通过版本化的 Fishing Boundary 提供生产 Adapter。需要动 Fishing 内部时必须有明确包含该改动的独立任务。你可以按需读 Fishing 源码——只是不要在别的任务里改它。

## 按任务找入口

| 你要做的事                         | 先读哪个域                     | 通常还会牵动                           |
| ---------------------------------- | ------------------------------ | -------------------------------------- |
| 组局、邀请、加入、退出、旅行失败   | `Online/`                      | `UI/`、`Framework/Game/`               |
| 进入 Lake 的准入、重连、Host 退出  | `Framework/Game/`              | `Online/`、`Run/`                      |
| 一局的天数、额度、入夜、结算       | `Run/` + `Framework/Game/`     | `Environment/`                         |
| 猫的属性、倒地、淋湿、恢复         | `Condition/`、`AbilitySystem/` | `Character/`、`UI/`                    |
| 竿漂饵窝料、耐久、断竿、团队装备库 | `Equipment/`                   | `Data/`、`ShopEconomy/`                |
| 抛竿、咬钩、搏斗、抢抄             | `Fishing/`                     | `Integration/Fishing/`、`Environment/` |
| 鱼实例、鱼护、鱼缸、转移、吃鱼     | `Items/`                       | `Camp/`、`Social/`、`Collection/`      |
| 图鉴、印记、成像计划、跨局授予     | `Collection/`、`Profile/`      | `Items/`、`Framework/Game/`            |
| 偷鱼、求助、恶作剧、保护牌         | `Social/`                      | `Items/`、`ShopEconomy/`               |
| 卖鱼、买竿、团队钱包、商店库存     | `ShopEconomy/`                 | `Items/`、`Equipment/`                 |
| 水域、天气、昼夜、窝点             | `Environment/`                 | `Run/`、`Fishing/`                     |
| 鱼表、装备表、内容校验             | `Data/`                        | `Equipment/`、`Fishing/`               |
| 营地、休息、救援、鱼缸转移、篝火   | `Camp/`                        | `Items/`、`Condition/`、`Collection/`  |
| HUD、会话界面、只读展示            | `UI/`                          | 各领域快照                             |
| 输入映射                           | `Input/`                       | `Character/`                           |
| 跨域 DTO、命令外壳、幂等模板       | `Framework/Core/`              | 全域                                   |

改动落到哪个目录，按**类型自身属于哪个系统**判断，不按"它服务谁""挂在哪个 Actor 上""当前谁调用它"。判定细则见 `AGENTS.md` 的「源码目录分类」。

## 模块索引

项目是单 Runtime 模块 `Source/Catfishing`，目录是领域划分，不是 UE 子模块，也不是 DLL 边界。`Catfishing.Build.cs` 把模块根注册为 `PublicIncludePaths`，所以模块内 include 一律写根相对路径（`Items/CatItemTypes.h`）。

每个域下的 `Tests/` 子目录本文不展开，只在需要说明"某入口只有测试调用"时提到。

### Source/Catfishing/（模块根）

只放模块入口和构建合同，没有业务类型。

| 文件                  | 职责                                                                                     |
| --------------------- | ---------------------------------------------------------------------------------------- |
| `Catfishing.cpp`      | `IMPLEMENT_PRIMARY_GAME_MODULE`，并集中 `DEFINE_LOG_CATEGORY` 全部 10 个日志分类的定义体 |
| `Catfishing.h`        | 只 include `CoreMinimal.h` 的空占位头，除 `Catfishing.cpp` 外无人包含                    |
| `Catfishing.Build.cs` | 模块依赖声明与公开 include 根                                                            |

模块根开始堆业务类时，应在验收前整理出去。

### Source/Catfishing/Logging/

只有日志分类的**声明**，没有包装函数或格式化助手；调用点直接 `UE_LOG` 写真实字段。

| 文件       | 职责                                                                                                                                                                                                       |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatLog.h` | 声明 10 条领域分类：`LogCatfishing`、`LogCatOnline`、`LogCatRun`、`LogCatEnvironment`、`LogCatFishing`、`LogCatItems`、`LogCatProfile`、`LogCatUI`、`LogCatSocial`、`LogCatCharacter`，等级均为 `Log, All` |

要按域筛日志时从这里挑分类名。

### Source/Catfishing/Framework/Core/

跨领域的**纯合同层**：命令外壳、结构化错误、幂等语义、公开 DTO、跨域协议。全目录只有头文件，没有 .cpp，不持有任何运行时状态。它对任何领域目录零依赖——这是所有领域都能反向依赖它的前提。

| 文件                            | 职责                                                                                                                                                                                                                                         |
| ------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatDomainCommandTypes.h`       | 全域共用的命令外壳：三态策略门、13 种结构化错误、带 `RequestId/ExpectedRevision/StableNetId` 的命令上下文与终态头；并提供两个共享实现 `MarkCommandReplayed()` 和模板 `CatQueryTerminalReplay()`（按终态键 + 载荷签名判定首次/重放/载荷冲突） |
| `CatRunContracts.h`             | Run 的公开语义与 DTO：7 个阶段、终局原因、转移原因、Run 命令与错误、Environment 的天气/时段轴、`FCatRunPublicState`（GameState 复制的组合公开事实）、teardown 三态协议，以及只读接口 `ICatEnvironmentProvider`                               |
| `CatProfileContracts.h`         | 永久授予与本地图鉴的持久结构：`FCatProfileGrant`、Journal 两阶段条目、鱼图鉴记录（钓起轨与抄获轨并列）、本地印记索引条目                                                                                                                     |
| `CatSacrificeContracts.h`       | 献祭跨 Items/Run 的短协议：8 个单向阶段（`ItemsCommitted` 后不可逆）、命令与可重放结果                                                                                                                                                       |
| `CatFishingBoundaryContracts.h` | Fishing 外围事务的信封与身份：事务三态、20 余种 Boundary 错误、8 类 operation、9 类 Receipt、五种 opaque ID 包装、SHA-256 载荷哈希、请求/结果公共头。**只有信封和身份，具体业务型在 `Fishing/CatFishingTypes.h`**                            |

`CatQueryTerminalReplay` 是全项目幂等的公共骨架，Camp、Condition、Equipment、Environment、Items、Social、Fishing、Framework/Game 都在用它。改它等于改所有域的重放语义。

### Source/Catfishing/Framework/Game/

全部 UE 游戏框架宿主的项目实现。它拥有 Run 聚合本身、身份准入注册表和 Run 命令幂等缓存；不拥有任何领域算法。

| 文件                                | 职责                                                        |
| ----------------------------------- | ----------------------------------------------------------- |
| `CatFrontendGameMode.h/.cpp`        | 前台菜单模式，约 40 行                                      |
| `CatfishingGameMode.h/.cpp`         | Lake 服务器权威根，约 1840 行，仍是本目录最大的一组文件      |
| `CatfishingGameState.h/.cpp`        | 6 份公开复制快照的出口，约 310 行                            |
| `CatfishingPlayerState.h/.cpp`      | 个人 ready / 公开图鉴 / 装备解锁清单，约 185 行              |
| `CatfishingPlayerController.h/.cpp` | 30+ 条玩家意图 RPC 的转发口，约 1240 行                      |

这五组文件此前挤在一个 `CatGameplayTypes.h/.cpp` 里（约 3100 行），且那个头 include 了 8~10 个领域类型头，
19 个 .cpp 全都包含它、没有任何 .h 包含它——任何一个领域 DTO 改动都要重编这 19 个 TU。
拆开后每个头只 include 自己声明里真正出现的类型，调用方按需包含具体宿主，伞头已删除。

五个宿主：

- `ACatFrontendGameMode` — 前台菜单模式，`DefaultPawnClass` 置空，不碰 Online、不旅行。
- `ACatfishingGameModeBase` — Lake 服务器权威根。持有准入注册表、Run 聚合与 Revision、StateTree 组件与 Environment provider、白天截止与时段分界计时器、夜间 ready 资格与确认集合、Run 命令终态缓存、Host exit ACK 等待集合。构造时钉死 `ACatCharacter` / `ACatfishingPlayerController` / `ACatfishingGameState` / `ACatfishingPlayerState`。
- `ACatfishingGameState` — 6 份 `ReplicatedUsing` 公开快照的唯一出口：Run 公开状态、求助信号、叼鱼列表、Social 权限、团队经济、团队装备库。
- `ACatfishingPlayerState` — 只有个人 ready、公开鱼图鉴摘要与服务器持有的装备解锁清单（owning client 从 Profile 上报、服务器校验后复制，只给 Equipment 装配 gate 读），不承载 ASC、物品或 Profile。
- `ACatfishingPlayerController` — owning-client 网络适配层，30+ 条 Server/Client RPC 的转发口，外加本 Controller 的窝料与团队装备取用终态缓存。绝大多数玩法命令的真实入口都在这里，要找"某个玩法怎么从客户端进来的"就搜这个类的 `Server*_Implementation`；`.cpp` 里按 Run / Profile / Fishing / 献祭 / Camp / Equipment+Items / Social / Shop / Condition / Environment / HostExit / 团队装备库分段。

GameMode 的五条 Run 写口：`EnterRunPhaseFromStateTree`（唯一阶段写入口）、`SubmitCommittedQuotaContributionFromCoordinator`（唯一额度/世界进度写入口）、`SubmitNextDayReady`、`CompleteSettlementFromCoordinator`、`RequestRunTeardown`。所有写入统一经 `RefreshEnvironmentAndPublish()` 出到 GameState，每次实质写入 `Revision` +1。

`CanAcceptGameplayCommand` / `CanForwardGameplayCommand` 是准入注册表对外的只读 gate，几乎所有玩法 RPC 都先过它。唯一刻意绕过的是 `ServerRequestKickPlayer`（teardown 与结算夜房主仍应能踢人）。

### Source/Catfishing/Online/

拥有联机会话生命周期这一整块事实：平台 NamedSession、两地图旅行、网络失败补偿、会话成员顺序与房主身份。不拥有玩家准入记录（在 GameMode），不拥有任何 UI。

| 文件                        | 职责                                                                                                                                                                                                            |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatOnlineTypes.h`          | 四条互不代替的联机事实轴各自的枚举、22 项结构化错误码、给 UI 用的 opaque 句柄（`FGuid` 包装，平台 SessionId 永不出子系统）、公开摘要 DTO、合成只读快照 `FCatOnlineSnapshot`、同步提交回执、UI 能表达的 5 种意图 |
| `CatOnlineSettings.h/.cpp`  | 全部 Online 产品裁决位，默认一律是 `Undecided`/0/-1 这类未裁哨兵；`TryGet*` 把未裁翻译成 fail-closed 的 false。**注意它继承 `UObject` 不是 `UDeveloperSettings`，所以不出现在项目设置面板**                     |
| `CatOnlineSubsystem.h/.cpp` | GameInstance 级深模块，约 1470 行，Create/Find/Join/AcceptInvite/Leave/RemoteHostExit 六个入口的唯一实现者                                                                                                      |

**四份事实各自的持有者与收口点**：

| 事实            | 成员                                                                | 收口点                                                                                                                           |
| --------------- | ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Session         | `SessionState` + `SessionRole` + `OperationRole`                    | 四个 OSS 完成回调置终态；`OperationRole` 单独存在是因为 Destroy 后 `SessionRole` 被清空，但回前台还需知道当初是 Host 还是 Client |
| World           | `WorldState` + `ExpectedPackage`                                    | `HandlePostLoadMap` 是唯一"到达"确认点；`HandleTravelFailure` 也重写                                                             |
| Transport       | `TransportState`                                                    | 三个 travel 入口写 `TravelQueued`，PostLoadMap 收敛，两个失败回调写 `Failed`                                                     |
| ActiveOperation | `ActiveOperation` + `ActiveRequestId` + `OperationEpoch` + 超时句柄 | `BeginOperation` 是并发闸，`FinishOperationSuccess/Failure` 是唯二终态出口                                                       |

`OperationEpoch` 单调递增，所有平台回调与超时计时器都携带提交时的 epoch，不匹配就只记 `online_callback_ignored` 后返回——排查"回调乱序"类问题从这里入手。

Frontend 到 Lake 的唯一正式入口就在这个子系统。不要新增 ConsoleCommand、OpenLevel 或 Widget 旁路。

### Source/Catfishing/UI/

LocalPlayer 层的 MVC 协调与两个原生白盒 View。不写任何玩法状态，不直接碰 OSS 或旅行 API。

| 文件                               | 职责                                                                                                                                                               |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `CatLocalPlayerUISubsystem.h/.cpp` | 每个 LocalPlayer 唯一的协调器。持有三个 Widget（Travel / Survival / CommandPanel）的所有权、5 个宿主弱引用和 11 个配对解绑句柄。所有事件回调都丢弃载荷、整份重读，防止 UI 从增量事件拼出自己的私有真相 |
| `CatSurvivalWidget.h/.cpp`         | 定义只读投影 DTO `FCatSurvivalViewState`（三个属性浮点 + Condition/Equipment/Run/Help 四份完整快照）与渲染它的 Widget                                              |
| `CatTravelWidget.h/.cpp`           | 原生白盒会话界面：状态文本 + 五个按钮，点击时广播 `(Action, FGuid)`，按钮可用性完全由 Online 快照推导                                                              |
| `CatUISettings.h/.cpp`             | 只有一个 `bEnableLakeStatusView` 开关                                                                                                                              |

两个 Widget 都是 `WidgetTree::ConstructWidget` 手搓的纯 C++ 白盒，没有任何 UMG 资产接线。

模块内没有任何文件 include `UI/` 下的头——UI 是纯下游，入口全部来自引擎生命周期和它自己订阅的广播。所以改 UI 不会波及领域层，反过来改领域快照要检查这里的订阅点。

### Source/Catfishing/Input/

只做 EnhancedInput MappingContext 的装配，不定义 InputAction、不绑定玩法命令（玩法输入绑定在 Character 侧）。

| 文件                                  | 职责                                                                                                                                           |
| ------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatInputSettings.h/.cpp`             | 总开关 + MappingContext 配置数组，按 Priority → Layer → 路径三级稳定排序                                                                       |
| `CatLocalPlayerInputSubsystem.h/.cpp` | 记录"本子系统添加过哪些 MappingContext、加到了哪个 EnhancedInput 实例"，只为精确成对移除；`Deinitialize` 只移自己加的，不调 `ClearAllMappings` |

`bEnableGlobalInputContexts=False` 且 Content 里没有输入资产，因此这两个类当前在运行时必然无副作用。

### Source/Catfishing/Character/

只放猫的玩法身体本体。它拥有组件装配、ASC ActorInfo 生命周期和"身体失效时先收口协议"的顺序；不拥有任何状态数值。

| 文件                  | 职责                                                                                                                                                                        |
| --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatCharacter.h/.cpp` | 构造并持有身体上的全部子对象；在四个时机幂等刷新 ASC ActorInfo；authority 侧一次性授予诊断 Ability、写三项初始属性、注册个人鱼护、触发 starter 三件套；拆卸时按固定顺序收口 |

Character 上实际装配的子对象只有五个：`AbilitySystemComponent`（引擎原类，未派生）、`SurvivalAttributes`（构造期即交给 ASC 持有）、`PersonalFishGuard`（`UCatContainerReplicationComponent`）、`ConditionComponent`、`EquipmentComponent`。**没有**摄像机、SpringArm、交互组件或 Fishing 组件——钓鱼和社交是 WorldSubsystem，不是 Character 组件。

ASC 的 Owner 和 Avatar **都是 Character 自己**，不是 PlayerState。统一收在私有的 `InitializeAbilityActorInfo()`，调用点四处：`BeginPlay`、`PossessedBy`、`OnRep_Controller`、`PawnClientRestart`。配置 gate 关闭时是主动 `ClearActorInfo()`，不是不做——避免引擎在组件注册期自动建立的 ActorInfo 让未裁 runtime 偷跑。

拆卸顺序有语义要求：先通知 Fishing/Social 收口 → 移自有 MappingContext → `CancelAllAbilities` → `ClearActorInfo`。

`PersonalFishGuardId` 是本 Character 一局个人鱼护 ID 的唯一权威，只在 authority 首次生成，不复制。Camp、PlayerController、Fishing Boundary 都读它。

### Source/Catfishing/AbilitySystem/

GAS 那一层的项目定制：局内属性集、ASC 运行/复制策略配置、一个诊断 Ability。不拥有 ASC 实例本身（实例在 Character 构造）。

| 文件                             | 职责                                                                                                                                             |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `CatSurvivalAttributeSet.h/.cpp` | 定义并复制三个局内属性，RepNotify 里只做标准收敛，不夹带阈值、表现或死亡判定                                                                     |
| `CatAbilitySettings.h/.cpp`      | ASC 运行总闸与复制策略；`IsRuntimeEnabled()` 要求总开关为真且策略显式为 `Full`（`Undecided` 不隐式升级）；诊断增量在 Shipping 下硬编译为永远关闭 |
| `CatStageCTestAbility.h/.cpp`    | `ServerOnly` 的诊断 Ability，激活时按配置增量改 Poison（`DiagnosticPoisonDelta`）；无 Tag / Cost / Cooldown / Cue                                 |

生存属性是**三项**：

| 属性              | 含义                                                         |
| ----------------- | ------------------------------------------------------------ |
| `FishingStrength` | 搏斗中的猫力量                                               |
| `FightStamina`    | 单次搏斗内的短周期体力                                       |
| `Poison`          | 中毒累积，**倒地的唯一来源**                                 |

`Hunger`（饥饿，2026-08-18 删）和 `Fatigue`（疲惫数值，2026-08-20 删）都已按飞书猫咪状态册删除：饥饿是废弃概念，疲惫被拍定为**纯演出**（无数值条、无惩罚，演出档位本身尚未实现）。类注释、Character 注释、`TryGetInitialAttributes` 签名与 `GetLifetimeReplicatedProps` 全部一致地只有三项。看到旧文档写"四项/五项生存属性"的，以源码为准。

三项属性的 authority 写入者只有三处：Character 的初值、Condition 的加毒/清毒、诊断 Ability 的 Poison 增量。

### Source/Catfishing/Condition/

拥有猫身体的**离散**状态（湿 / 倒地 / 最近恢复方式）及其复制读模型。数值本身存在 ASC 里，本目录只读写它们。

| 文件                           | 职责                                                                                                                                                             |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatConditionTypes.h`          | 复制到客户端的 `FCatConditionSnapshot` 与 `ECatRecoveryMode`。`Herb` 枚举值被刻意保留为空位——删掉会让 `CarriedToCamp` 的整数值前移，导致跨版本客户端读错恢复方式 |
| `CatConditionSettings.h/.cpp`  | 只剩总 gate 与 `PoisonDownedThreshold`。`HasDownedThresholds()` 是三条恢复路径和吃鱼结算共用的准入；恢复路径没有自己的数值配置                                     |
| `CatConditionComponent.h/.cpp` | 唯一裁决 Wet/Downed/Recovery 的 authority 组件                                                                                                                   |

三条恢复入口 `RequestFieldSelfRecovery` / `RequestCampRest` / `CompleteCarryToCamp` 统一走 `ApplyRecovery`：Poison **直接清零**——三条路径在需求上都定义为"解除倒地"，部分削减会被下一次裁决立刻判回倒地。野外自救的网络入口是 `ACatfishingPlayerController::ServerRequestFieldSelfRecovery`（白盒面板 `FieldSelfRecovery` 按钮，仅倒地时可点）。

吃鱼路径连**失败**结果也永久缓存进幂等表：实物鱼已被 Items 不可逆移除，重试必须换新 RequestId。这是这个组件最容易被误改的地方。

首次进入倒地时本组件会主动调 `UCatFishingService::TerminateSessionsForCharacter`。

### Source/Catfishing/Equipment/

拥有两类东西的局内真相：挂在单只猫身上的功能型装配（竿/饵/漂 + 耗材栈 + 竿耐久），以及全队共有的团队装备库实例表。不拥有跨局解锁与 Profile 选择，不拥有钱和订单，没有等级/词条/战力概念。

| 文件                             | 职责                                                                                                                                                                                                                |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatEquipmentTypes.h`            | 装备类别、失败预算惩罚、单猫装配快照、一局耗材栈、团队装备库实例/快照/命令/结果。`Herb`、`Driftwood`、`DamageRod` 三个枚举值是保留的死值                                                                            |
| `CatEquipmentDefinition.h/.cpp`  | 单条装备 DataAsset。`IsRuntimeDefinitionReady()` 按类别 fail-closed：Rod 必须非消耗且耐久上限 > 0，Bait 不论普通/特殊都必须是一局消耗品（飞书 §3.4，`bSpecialBait` 只区分身份），Chum 必须是消耗品且三轴合法，Herb/Driftwood 直接拒绝进运行目录 |
| `CatEquipmentSettings.h/.cpp`    | 装备目录本体（软引用清单）+ 整表校验 + Blake3 内容摘要 + starter 三件套配置 + 随身耗材上限 `RunConsumableStackCapacity`（只约束 Grant，0 = 不设限）。`FindRuntimeDefinition()` 每次调用都先跑整表校验，重复稳定 ID 直接返回 nullptr |
| `CatEquipmentComponent.h/.cpp`   | 单猫装配聚合。装配、从团队装备库取用装配（`EquipFromTeamLibraryFromAuthority`，只换对应槽位、不查 RequiredUnlockId）、耗材五入口（Grant/Consume/Reserve/CommitReserved/ReleaseReservation）、竿耐久扣减、失败预算提交 |
| `CatTeamEquipmentLibrary.h/.cpp` | 服务器 WorldSubsystem，一局的团队装备库。两条写口：`GrantFromShopOrder`（入库，订单去重刻意排在版本前提**之前**——交付重试拿到的一定是入库前的旧版本，否则订单会永远卡在待交付）、`TakeInstance`（按实例取走，被取走的记到 `TakenInstanceById` 供订单重放找回执） |

竿耐久的字段与入口：定义侧上限是 `MaximumRodDurability`，运行时是快照里的 `RodDurability` + `bRodBroken`，装配时置满。**唯一扣减入口**是 `CommitFightRodDurabilityFromAuthority`。

耗材的 `Reserve` 只占位，不改公开 Snapshot 也不推 Revision，靠 `CountActiveConsumableReservations` 保证并发不重复占同一份库存——投窝的三段式（预留→提交→释放）依赖这个语义。

### Source/Catfishing/Environment/

拥有"哪里是水""哪里有窝""今天什么天气/时段"三类局内环境事实。Run 快照是它的只读输入，环境侧从不回写 Run。

| 文件                                      | 职责                                                                                                                                                                                           |
| ----------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatWaterTypes.h`                         | 环境域跨系统 DTO 的单一定义处：窝料三轴向量 `FCatChumVector`、投窝命令/结果、窝点快照、水域查询请求/结果/错误码。被 Data、Equipment、Fishing、Framework 反向 include，实际是全模块的环境词汇表 |
| `CatWaterRegion.h/.cpp`                   | 关卡放置的中性水域 Actor。只有 AABB 半尺寸、中心偏移、稳定 RegionId 和手填 RegionRevision；不复制、不持窝料；服务器按 0.25 秒节拍 Tick，把站在 AABB 里的猫置 Wet（落水全湿，只写真实变化）                                                                           |
| `CatWaterQuerySubsystem.h/.cpp`           | 无状态 WorldSubsystem，每次查询现扫 World 里的 `ACatWaterRegion`，**要求唯一命中**，第二次命中直接判 `AmbiguousRegion`。不缓存、不建索引                                                       |
| `CatChumSpotSubsystem.h/.cpp`             | 本局所有窝点的唯一服务器权威：窝点数组、集合 Revision、全局衰减累加器、投窝幂等缓存。唯一生产读端是 Fishing Boundary 的 Cast（按落点查窝点算咬钩间隔）                                          |
| `CatEnvironmentSettings.h/.cpp`           | 环境总 gate、天气权重与调度种子、晨/暮分界比例、全部窝料数值、自然事件配置，外加一组纯计算方法：调度槽解析（含时段）、按槽天气抽样、自然事件选择、种子解析、下一个分界求解、咬钩间隔公式。文件头部的 `CatEnvironmentEvents` 命名空间是六类自然事件 ID 的唯一定义处 |
| `CatConfiguredEnvironmentProvider.h/.cpp` | `ICatEnvironmentProvider` 的正式实现，作为 GameMode 的 DefaultSubobject 存在。每次现算，不缓存：把当前局内时钟折成调度槽，再依次取天气、上一槽天气（彩虹用）和聚鱼时刻是否在冷却，然后选出本槽的自然事件                                                          |

窝料池在 `UCatChumSpotSubsystem` 的私有 `Spots` 数组里，窝点没有稳定 ID，一律按坐标解析；命中判定只比 XY 不比 Z（漂和落点高度随水面与抛投弧线浮动），多个相交取圆心最近。

天气与自然事件走同一套“调度槽”：一天切成晨/昼/暮/夜四个槽（前三个用已有的晨末/暮初分界，夜晚整段一个槽），槽号 = DayIndex×4 + 段序号。
每个槽用 (RunId 派生的种子, 槽号) 重建一条 `FRandomStream` 抽一次天气，因此它是纯函数：同一局同一槽反复求值结果恒等，不同局序列不同，测试只需推进局内时钟就能复现。
调度不自己拿计时器：重算时机完全靠 GameMode 已有的阶段进入与时段分界计时器，所以它也不会在夜晚或结算夜被唤醒。
六类自然事件共用快照里的**一个** `ActiveEventId`，同槽多条满足时按固定优先级取第一条（森林湖鱼群 > 彩虹 > 月光湖面 > 萤火虫 > 晚霞 > 鸟群蝴蝶，工程暂定 D-26）。
事件的副作用在 GameMode 的 `ApplyEnvironmentEventSideEffects()`，**只在事件 ID 真的换了时触发一次**：森林湖鱼群去投窝，会抛印记的五类提交印记候选。

"聚鱼时刻"是一个机制两个触发源，只有一本账：玩家投窝与森林湖自然涌现都走 `ContributeChum`，
任何一次 committed 投窝都重置 `IsAggregationMomentOnCooldown` 读的那个共享冷却，因此自然事件不会叠在玩家刚投好的窝上；
玩家投窝本身不查这个谓词（给玩家投窝加 cd 是产品改动，飞书未裁，D-25）。它也因此取代了旧的"Day+Event 去重键"，不再存第二份账。

写口只有三个：`ContributeChum`（玩家与自然事件共用，唯一投窝写口）、`ApplyOneDecayStep`（Tick 驱动，**刻意不推 Revision**——衰减是确定性全局过程、投料是纯累加，两者可交换，推了会把"读快照后跨过一次衰减再投料"误判成陈旧写入）、`ResetRuntimeChumFromAuthority`（局末清空）。客户端因 `ShouldCreateSubsystem` 根本不创建这个子系统，不存在第二份池。

水域查询有两条**语义不同**的路径：`QueryWaterRegion` 用于解析区域身份（要求唯一命中，重叠即歧义），Fishing Boundary 在 Cast 时用它；`IsChumDropReachable`（file-static，在 `CatfishingPlayerController.cpp` 里）用于投窝可达性，只要求存在一片水域同时包含落点和投掷者，重叠不算歧义。

### Source/Catfishing/Data/

拥有"哪些正式内容资产构成本次可运行内容包"这件事。不拥有装备资产定义（在 `Equipment/`），运行时不扫描 Content 目录也不回读飞书。

| 文件                                        | 职责                                                                                                                                                                                                                       |
| ------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatDataCatalogTypes.h`                     | 目录校验的公共词汇：问题码、单条问题、来源戳、单目录结果、多目录报告。被 `Equipment/CatEquipmentSettings.h` 复用，所以它是 Fish 与 Equipment 共享的校验契约                                                                |
| `CatFishDefinition.h/.cpp`                  | 单条鱼的 DataAsset：稳定 ID、体型档、献祭增减量、成像事件 ID、出没 RegionIds、窝料归属、重量区间、最低协作人数、鱼力量与搏斗体力、偏好特殊饵、食用安全与中毒值、可否入缸。给出"能进捕获链"和"能进吃鱼链"两个独立 readiness |
| `CatFishCatalogSettings.h/.cpp`             | 鱼目录：Schema 版本、DataRevision、飞书来源戳、软引用清单；按 ID 解析、确定性 Encounter 抽取、整表校验、Blake3 内容摘要。选鱼的请求/结果 DTO 也在这里                                                                      |
| `CatDataCatalogValidation.h/.cpp`           | 静态类 `FCatDataCatalogValidator`：把 Fish 与 Equipment 当一个内容包一起校验，先校 Equipment，再把同一个 Equipment 目录传给 Fish 做特殊饵引用校验                                                                          |
| `CatDataCatalogValidationCommandlet.h/.cpp` | CI 入口 UCommandlet，成功打 ContentHash 返回 0，失败逐条打结构化 issue 返回 1                                                                                                                                              |

**索引方式**：`Definitions` 是软引用数组，ini 里写完整对象路径，运行时逐条 `LoadSynchronous()` 后按稳定 ID 排序（选鱼与 hash 共用这一条顺序）。**没有任何按目录扫描、AssetRegistry 查询或 PrimaryAssetType 注册**——一个资产不写进 ini 就完全不存在于运行时。稳定身份是 `FishDefinitionId` 这个 FName，不是资产路径；内容摘要只哈希字段值不写路径，所以移动资产文件不改变 ContentHash。

跑内容校验：

```
D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\UnreaProjects\Catfishing\Catfishing.uproject -run=CatDataCatalogValidation -Unattended -NullRHI
```

它不接受任何参数。GameMode 的 `StartPlay` 也会拿同一个 validator 做启动硬 gate，失败直接 `FailRunStartup("DataCatalogInvalid")`。

### Source/Catfishing/Fishing/

拥有"一次钓鱼长流程"的服务器运行态本身。不拥有鱼表抽取、水域查询、幂等 Journal 和 encounter 冻结（都在 `Integration/Fishing/`），也不拥有实物鱼、图鉴 Grant、装备扣减。

| 文件                              | 职责                                                                                                                                                                                                                             |
| --------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatFishingTypes.h`               | Fishing 域对外可见的全部数据形状：公开阶段枚举、复制用 Snapshot（含搏斗公开字段 D/L/L_max、鱼状态、鱼体力、挣扎、钓手意图、终局、完美中鱼）、抢抄命令/结果，以及 Fishing↔Boundary 之间往返的 Start/Cast/Bait/Fight 请求与 typed result |
| `CatFishingFightModel.h/.cpp`     | 遛鱼/搏斗的**纯规则层**（无 Actor/World）：飞书 4.3 判定表 `JudgeOutwardPull`（①→④ 取等从严）、4.4 消耗战、D/L 速率表、两状态按段推进与 P(向外) 修正、垂死挣扎、完美中鱼削减、体重档游速系数。常量全部抄飞书并注明出处；会话只是它的宿主，测试直接驱动它 |
| `CatFishingStateTreeEvents.h/.cpp`| `Cat.Fishing.HookSet` 事件 Tag：钓手在真咬期第一次"拖"＝提竿，会话发它让 ST_FishingSession 从 TrueBiteWindow 转进 HookedFight（不等 10 秒延时边）                                                                        |
| `CatFishingService.h/.cpp`        | authority-only WorldSubsystem。持有 SessionId→Session、SessionId→钓手、钓手→当前唯一活跃会话三张表和两张 Start 终态缓存。它是"同一玩家不能并行开第二局"这条规则的持有者，也是 RPC 转发点和 teardown 收口点，**本身不写任何阶段** |
| `CatFishingSession.h/.cpp`        | 一次钓鱼的 Actor 宿主。持有唯一复制字段 `FCatFishingSessionSnapshot`、服务器私有的参与者集合、Boundary 冻结下来的鱼重/水域/AttemptId、会话初始化时从钓手装备冻结的竿强度/L_max、搏斗运行态（`FightParams/FightState/FightRandom`）、近岸权威目标点、assist/scoop 两套幂等缓存。**会话阶段、搏斗终局与捕获终态的唯一写入者**；`ApplyFightExchangeFromStateTree(DeltaSeconds)` 每帧推进搏斗并把猫体力写 ASC、竿磨损按秒提交 Equipment；`SubmitFightIntent` 接拖/松意图并在真咬期判完美中鱼 |
| `CatFishingSettings.h/.cpp`       | fail-closed 闸：总 gate、StateTree 软引用、真咬窗口秒、近岸验证 gate、抢抄 reach、终态复制窗口。任一 Unset 就阻止会话创建；D₀ 已不在这里，改为按当前鱼漂现算落点后由 `FCatFishingEncounterSpec.InitialFishDistanceMeters` 携带（D-19 已退休）                                                                 |
| `CatFishingStateTreeNodes.h/.cpp` | 5 个 StateTree Task 的 C++ 实现体：EnterPhase/Wait/FailureBudget 关 Tick、只做 `Cast` 出 Session 再调一次对应写口；`FCatFishingBiteIntervalWaitTask` 在 Probe 里累计时间等满会话冻结的咬钩间隔；`FCatFishingFightExchangeTask` 在 HookedFight 里每帧调会话推进口，按终局返回 Succeeded（碾压/翻肚/遛到岸边→NearShore）或 Failed（断竿/拖下水→Terminated） |

阶段只在三处被写：`EnterPhaseFromStateTree`、`RequestScoop`（提交成功后直接写 Resolved）、`TerminateSession`。

五个 Task 里真正推进阶段的只有 `FCatFishingEnterPhaseTask` 一个，其余是副作用节点（失败预算）、等待节点或逐帧驱动节点：`FCatFishingWaitTask` 是纯占位，`FCatFishingBiteIntervalWaitTask` 等满 `ACatFishingSession::GetBiteIntervalSeconds()`（Cast 时按落点窝料冻结进 EncounterSpec 的 T_actual）后让 Probe 状态 Succeeded，`FCatFishingFightExchangeTask` 每帧把 DeltaTime 交给会话让 HookedFight 按飞书规则打到终局。旧的"重试耗尽→剪影"节点已随重试制废除删除（2026-08-20）。**转移拓扑完全不在 C++**，由 StateTree 资产编排：当前 ST_FishingSession 是 Probe →(间隔等满) TrueBiteWindow →(HookSet 事件 或 10 秒) HookedFight →(搏斗成功终局) NearShore →(30 秒无人抄) Terminated，任一状态 Failed 由 Root 兜底进 Terminated（8 条转移）。

搏斗的时间推进：`HookedFight` 状态的 `FCatFishingFightExchangeTask::Tick` 每帧调用 `ACatFishingSession::ApplyFightExchangeFromStateTree(DeltaTime)`（StateTreeComponent 的 Tick 频率，即每帧）；会话读猫体力（ASC）与竿耐久（Equipment）交给 `CatFishingFightModel::Step`，猫体力增量当帧写回 ASC，竿磨损累计每满 1 秒批量提交一次 `CommitFightRodDurabilityFromAuthority`；Snapshot 里的搏斗公开字段每帧拷贝但不递增 Revision，只有终局才 `++Revision` 并 `ForceNetUpdate`。玩家输入：`ACatCharacter` 绑定 `IA_FishingPull`（左键）/`IA_FishingRelease`（右键）的 Started/Completed → `ACatfishingPlayerController::ServerSetFishingFightIntent` → `UCatFishingService::SubmitFightIntent`（按钓手身份找活跃会话）→ `ACatFishingSession::SubmitFightIntent`。鱼表缺食性/稀有度、漂缺射程时的工程暂定值见 `Docs/Development/工程自补决策记录.md` D-16~D-22。

抢抄的不可逆提交点是 `UCatItemsService::CommitCapture`。它成功之后才记钓起轨与抄获轨、才建成像计划；两条 Grant 的预检刻意排在提交之前。

### Source/Catfishing/Integration/Fishing/

拥有 Fishing 与其他域之间的事务协调层：幂等 Journal、canonical PayloadHash、Fight cursor 顺序账、Start/Cast 时刻的 encounter 冻结。全部状态是 run-local，不承诺跨进程持久恢复。

| 文件                                 | 职责                                                                                                                                                                                                                              |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatFishingBoundarySubsystem.h/.cpp` | authority-only WorldSubsystem，Boundary 的唯一公开门面。持有 Journal、FightCursorLedger 和四张 run-local 表。**它是唯一真正去查水域、遍历在线玩家算参战能力、调鱼表抽鱼的地方**                                                   |
| `CatFishingOperationJournal.h/.cpp`  | 纯 C++ 类。幂等键是 `OperationKind\|AttemptId\|RequestId`，Principal 和 PayloadHash **刻意不进 key**——这样身份漂移能被检出为 `InvalidIdentity`、语义漂移能被检出为 `PayloadMismatch`，而不是悄悄命中另一个槽                      |
| `CatFishingFightCursorLedger.h/.cpp` | 纯 C++ 类，自带独立 Journal。管搏斗资源帧的严格按序（必须 Last+1）与"Capture 前封存后不许补帧"                                                                                                                                    |
| `CatFishingBoundaryHash.h/.cpp`      | 无状态 helper，自带 one-shot SHA-256。按固定顺序把 SchemaVersion、OperationKind、AttemptId、PrincipalId、ExpectedRevision 和长度前缀的业务 payload 编码后哈希。**RequestId / OperationId / ReceiptId / 时间戳明确排除在输入之外** |

Journal 的写入规则很硬：`CommitResult` 只允许 Pending→终态一次，拒绝降级、拒绝二次覆盖、拒绝"Committed 带非 None Error"和"Rejected 带 None Error"这两种非法组合。

一次钓鱼的真实调用顺序（生产路径）：

```
PlayerController::ServerStartFishingSession
  → CatFishingService::StartFishingSession
      → BoundarySubsystem::Start          (AttemptId 在这里首次生成)
      → BoundarySubsystem::CastAccepted   (查水域 → 算参战能力 → 抽鱼 → 冻结 EncounterSpec)
      → SpawnActor<ACatFishingSession> + InitializeSession
  → StateTree 资产驱动 EnterPhaseFromStateTree 推进阶段
  → PlayerController::ServerRequestScoop
      → Session::RequestScoop
          → ItemsService::CommitCapture   (不可逆提交点)
          → RunImprintService 记两轨 + 建成像计划
```

`InitializeSession` 有一个很长的 fail-closed 条件链（authority、settings ready、StateTree 可加载、鱼定义 ready、ID 与 EncounterSpec 一致、AttemptId 有效、内容版本非空、鱼重与体力正有限、水域 ID/Revision 有效、ItemsService 存在）。会话建不起来时先查这一串。

### Source/Catfishing/Items/

拥有"局内实物鱼"这一份服务器真相：容器数组、容量、鱼实例生命周期、各类持有锁及其幂等终态，并把已提交结果发布成只读复制快照。**不是泛道具系统**——功能装备在 `Equipment/`，永久解锁在 `Profile/`，捕获授予在 `Collection/`。

| 文件                                      | 职责                                                                                                                                                   |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `CatItemTypes.h`                          | 容器与鱼的全部领域 DTO，以及捕获/转移/偷取/进食/售卖/预留六类命令与结果。`FCatFishInstance::OwnerStableNetId` 故意不加 UPROPERTY，因而不会复制到客户端 |
| `CatItemsService.h/.cpp`                  | 唯一权威写口。持有容器记录、`ReservationByFish`（一条鱼同时只能被一个持有占用的唯一锁）、售卖冻结、偷鱼 escrow，以及四套幂等缓存                       |
| `CatItemsSettings.h/.cpp`                 | 容器种类到显式容量的映射；未知种类或非正配置返回 0，使事务返回 `PolicyUndecided` 而不是默认无限容量                                                    |
| `CatContainerReplicationComponent.h/.cpp` | 容器的只读网络出口。只有 `SetSnapshotFromAuthority` 一个入口且拒绝非 authority 调用，**没有任何领域写口**                                              |
| `CatFishTankActor.h/.cpp`                 | 共享鱼缸的复制宿主。authority BeginPlay 时生成一局稳定 TankContainerId 并向 Items 注册                                                                 |

公有事务入口：

| 场景          | 方法                                                                                                |
| ------------- | --------------------------------------------------------------------------------------------------- |
| 容器注册/注销 | `RegisterContainer` / `UnregisterContainer`（个人鱼护由 Character 调，共享鱼缸由 FishTankActor 调） |
| 首次捕获写入  | `CommitCapture` — **只允许写个人鱼护**                                                              |
| 跨容器转移    | `TransferOwnedFish` — 源必须是调用者本人的鱼护，源与目标两个 Revision 同时匹配                      |
| 吃鱼          | `ConsumeFish` — 鱼护要求鱼主人是调用者，共享鱼缸不做主人校验                                        |
| 献祭          | `ReserveFish` → `CancelFishReservation` / `CommitFishReservation`                                   |
| 售卖          | `PrepareFishSale` → `CancelPreparedFishSale` / `CommitPreparedFishSale`                             |
| 只读          | `TryGetContainerSnapshot` / `TryGetContainerHost`                                                   |
| Teardown      | `CloseCommandsAndCancelReservations`                                                                |

献祭与售卖共用同一套持有内核，但持有目的分别是 `"Sacrifice"` 和 `"Sale"`，是互不相通的键空间。

**偷鱼 escrow 全部是 private，只对 `friend class UCatSocialService` 开放**，主键是 Social 分配的 `TheftProtocolId` 而不是客户端 RequestId。escrow 期间源容器保留返还槽位，且这个预留计入所有容量检查。

服务与复制组件是单向关系：服务写完调 `SetSnapshotFromAuthority`，组件失效时只是不发布，**绝不回滚已提交的服务器事务**。`UnregisterContainer` 按精确弱引用比对解绑，迟到的旧 Actor 不能删掉同 ID 的新注册。

### Source/Catfishing/Collection/

拥有"把已提交领域事实转成永久 Grant，并把成像投递与 durable ACK 分成两条独立跟踪链"这件事。不拥有玩家永久档案（在 Profile 的 SaveGame），不产生图片。

| 文件                                     | 职责                                                                                                                                                              |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatImprintTypes.h`                      | 候选、成像计划、两类投递记录、两套阶段枚举，以及结算夜归档的结构化阻塞原因                                                                                        |
| `CatRunImprintService.h/.cpp`            | 一局服务器协调器。持有候选表、CapturePlan 投递表 + `CandidateId\|Recipient` 去重索引、Grant 投递表、两套 Grant 幂等索引（钓起轨/抄获轨）、RunId→相册 ID 映射；剪影 Grant 当前没有生产入口（供奉推进解锁待实现） |
| `CatImprintSettings.h/.cpp`              | 印记触发事件白名单与单局候选上限。空清单和 0 上限都是 fail-closed，没有"未配置即放行"的分支                                                                       |
| `CatImprintMediaTransportService.h/.cpp` | 一局服务器媒体字节传输：Manifest、分块、逐收件人 ACK cursor                                                                                                       |
| `CatImprintMediaSettings.h/.cpp`         | 媒体链路的安全边界：总开关、单份最大字节、块大小/块数上限、MIME 白名单                                                                                            |
| `CatImprintMediaTypes.h`                 | 媒体链路的 DTO                                                                                                                                                    |

**四个容易混淆的概念在代码里的落点**：

| 概念            | 类型                                                                                        | 谁产生                                               | 谁消费                                                                 | 落盘吗                                        |
| --------------- | ------------------------------------------------------------------------------------------- | ---------------------------------------------------- | ---------------------------------------------------------------------- | --------------------------------------------- |
| CapturePlan     | `FCatCapturePlan`                                                                           | `CreateCapturePlansForParticipants`（ID 在此生成）   | Client RPC → `ProfileSubsystem::ReceiveCapturePlan` → 广播给外部成像桥 | **不落盘**，服务器内存里活到 World 销毁       |
| Grant           | `FCatProfileGrant`（定义在 `Framework/Core/`）                                              | `EnqueueGrant`，三条路径：钓起、抄获、成像成功（剪影待供奉推进接入） | Client RPC → `ProfileSubsystem::ApplyGrant`                            | 客户端 SaveGame 的 Journal + AppliedGrantIds  |
| Grant ACK       | 没有独立结构，是投递记录的 `Stage == Acknowledged`                                          | 客户端 durable 后回 `ServerAcknowledgeProfileGrant`  | `AcknowledgeGrant`                                                     | 服务器只在内存；durable 证据在客户端 SaveGame |
| ImprintDelivery | 拆成 `FCatImprintCaptureDeliveryRecord` 与 `FCatGrantDeliveryRecord` 两条**互不借用**的记录 | 均由本服务创建                                       | 登录重投、teardown 收口                                                | 都不落盘                                      |

三者不能互相替代：CapturePlan 是成像任务，Grant 是永久授予内容，Grant ACK 是客户端 durable 后的回执。

### Source/Catfishing/Profile/

拥有每个 LocalPlayer 的本地永久档案，以及"什么时候允许向服务器 ACK"这一判断。不接触服务器实物容器，不生成或保存图片字节。

| 文件                         | 职责                                                                                                                                         |
| ---------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatProfileSaveGame.h`       | SaveGame 数据合同：schema 版本、Grant Journal、已应用 GrantId 集合、鱼图鉴、相册索引、每局封面映射、解锁 ID、按槽位的装备选择                |
| `CatProfileSubsystem.h/.cpp` | 档案的唯一持有者与写入者。实现 Grant 的「Pending 落盘 → 合并 → Complete 落盘」两阶段、相册只读投影、装备选择、印记隐藏、外部成像桥的计划广播 |
| `CatProfileSettings.h/.cpp`  | 持久化与外部成像桥的显式 gate（默认 fail-closed）+ 跨局相册容量上限                                                                          |

写盘只有一个私有函数 `SaveCurrentProfile()`，调用它的五处是：初始化建空档案、`ApplyGrant` 追加 Pending 后、`CompletePendingGrant` 合并后、`SetEquipmentSelection`、`SetImprintHidden`。后四处失败都有明确回滚（内存回滚或从磁盘重载）。`Deinitialize` **不做隐式保存**。

`bAckAllowed` 只有在 Complete 阶段的第二次落盘成功后才为 true——这是"服务器相信照片已经收好"的唯一依据，改动这条路径要格外小心。

槽位名是 `SaveSlotBaseName_ControllerId`。SaveGame 的 schema 版本不匹配时一律保持不可写，**没有迁移规则**。

合并语义里有一条容易踩：钓起轨与抄获轨共用一条按鱼种的图鉴记录，但抄获轨只 `++ScoopedCount`，不推进 State、不参与个人最佳、不计入 EncounterCount。

### Source/Catfishing/Run/

拥有一局的配置裁决、StateTree 可用的 C++ 节点与事件 Tag，以及献祭这条跨领域短协议的顺序推进。**Run 相位状态本身不在这里**——相位、Revision、DayIndex、ready 集合全在 `ACatfishingGameModeBase`。

| 文件                             | 职责                                                                                                                                                                                              |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CatRunSettings.h/.cpp`          | RunFlow 总开关、`ST_RunFlow` 软引用、白天秒数、额度策略（固定值或按天×清晨人数的显式曲线表）、夜间新加入/重连准入、成功结算策略与最终天序号。曲线档**不插值不外推**，缺条目或有重复键就整表判非法 |
| `CatRunStateTreeEvents.h/.cpp`   | 四个 GameplayTag 常量：`Cat.Run.DayElapsed` / `QuotaFailed` / `AllEligibleReady` / `SettlementComplete`，是 GameMode 向 StateTree 发事件时唯一允许用的集合                                        |
| `CatRunStateTreeNodes.h/.cpp`    | 三个节点：进入某 Phase 的 Task、原地等事件的 Task、按上次转移原因分边的 Condition。都只通过 `Context.GetOwner()` 调 GameMode，自身不保存状态、不选目标 State                                      |
| `CatSacrificeCoordinator.h/.cpp` | 献祭协议的唯一顺序推进者，也是 Host teardown 时按序关闭各域的收口点                                                                                                                               |

献祭的真实推进顺序是 `Received → Reserved → RunAccepted → ItemsCommitted → RunApplied → Completed`，另有 `Cancelled` / `Failed` 两个提前退出态，且**只可能发生在 Items commit 之前**。commit 之后鱼不可逆消失，只向前补齐，绝不还鱼。

`PrepareForRunTeardown` 的关闭顺序是语义要求不是习惯：关献祭门 → 逐条协议收口 → 关 Fishing → **关 Social（必须趁 Items 与 ShopEconomy 都还开着，因为未收口的偷鱼售出要靠 ShopEconomy 入账）** → 才关 Items 与 ShopEconomy。任一步失败返回 false，Online 侧不会继续 DestroySession。

### Source/Catfishing/Social/

拥有一局内玩家对玩家的三类互动的服务器裁决与协议状态，外加防骚扰牌这道共用护栏。不拥有鱼实体（在 Items 的 escrow）、不拥有救援与倒地状态、不拥有钱包。

| 文件                            | 职责                                                                                                                                                                                       |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `CatSocialTypes.h`              | 求助信号种类与快照、共享缸追回策略、偷鱼命令/结果、公开"叼鱼的猫"快照、运行期 Social 权限快照                                                                                              |
| `CatSocialSettings.h/.cpp`      | 六个 ready gate 与其数值。**偷取/恶作剧的权限项在这里只是开局默认值**，运行期不再参与裁决                                                                                                  |
| `CatProtectionSignActor.h/.cpp` | 复制的保护牌 Actor。`ProtectsAgainst()` 做精确 PlayerState 比对 + 球形距离，不按队伍或名字推断                                                                                             |
| `CatSocialService.h/.cpp`       | 约 1000 行，一局服务器 Social 的全部可写状态：活跃偷鱼协议表与"每个小偷至多一条"的反查索引、派生的公开叼鱼列表、运行期权限快照、局主身份、求助冷却表、两套幂等缓存、每人唯一牌子的弱引用表 |

四类协议的入口：

| 协议   | 入口                                                                                 | 权限判定的关键位置                                                                                                                                                                     |
| ------ | ------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 求助   | `RequestManualHelp`；巨鱼系统提示走独立的 `BroadcastGiantFishingPrompt`              | 没有权限枚举参与，**求助不受局主开关影响**；手动求助有冷却，系统提示不写冷却表                                                                                                         |
| 恶作剧 | `RequestMischief`                                                                    | `RuntimePolicy.MischiefPermission` 最先拦，然后是参数 ready、双方未倒地、距离，最后才查保护牌                                                                                          |
| 保护牌 | `PlaceProtectionSign`                                                                | **刻意不读 `MischiefPermission`**——否则关掉恶作剧会顺带让人放不了挡偷窃的牌。每人唯一，重复放牌是移动同一个 Actor 而不是叠加保护区                                                     |
| 偷鱼   | `BeginTheft` / `CatchTheft` / `SellStolenFish` / 超时回调 `HandleTheftWindowExpired` | `BeginTheft` 里保护牌判定**刻意排在 `Items::BeginFishTheft` 之前**，拒绝时 Items 一次都没被调用；`CatchTheft` **刻意不读 `TheftPermission`**——局主中途关偷取只该停新偷，不能把赃物锁死 |

`RuntimePolicy` 是本局权限的唯一运行期真相：`Initialize` 从 Settings 抄一次，此后只有 `SetSocialPolicy` 能改，Settings 不再被这两条路径读取。

公开叼鱼列表由 `RefreshStolenFishCarriers()` **整体重建**（不增量维护），所有会改变活跃协议集合的路径都汇到这一个函数，它末尾负责推给 GameState。

偷鱼售出是本目录最长的跨域链：估价（**刻意排在 Items 冻结之前**，估不出价时鱼一次都没被冻过）→ Items 冻结 → 校验 → 钱包入账 → Items drain。回退边界只有一条：钱包入账成功之前失败一律解冻，之后一律不解冻。

### Source/Catfishing/ShopEconomy/

拥有团队公款余额、商店库存、交易账本、订单交付状态和收鱼价体重轴五份经济事实。不拥有装备实例、不拥有鱼实体、不做权限判断、不接触 Controller。

| 文件                             | 职责                                                                                                                                                               |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `CatShopEconomyTypes.h/.cpp`     | 全部经济类型 + 目录项与档位表的自校验（拦没填价格的哨兵、拦标了每日进货却没给量、拦无限库存与每日进货同时为真）                                                    |
| `CatShopEconomySettings.h/.cpp`  | 总开关、起始公款（-1 哨兵区分"没裁过"与"裁定 0 元"）、售鱼最小金额、显式商店目录、免费普通饵与保底竿两个 EntryId、收鱼价策略与体重档位表；估价整表校验每次调用都做 |
| `CatShopEconomyService.h/.cpp`   | 约 640 行。持有钱包、库存、账本、幂等缓存、开局冻结的档位表、商店天序号                                                                                            |
| `CatShopOrderCoordinator.h/.cpp` | **无任何自有状态**，只做三条链的顺序推进：购买链、免费自取链、玩家自有容器售鱼链。购买/免费自取按账本 `EntryKind` 分流：装备进团队装备库，耗材进买家 Pawn 的 Equipment 耗材栈（RPC 层传入组件，订单 RequestId 当回执） |

订单即账本条目，交付状态是它的一个字段而不是第二份状态。钱包余额只有三处写：售鱼入账、目录交易扣款（价格为 0 时不推 Revision，所以免费自取不推进钱包版本）、初始化。

购买链跨 ShopEconomy + Equipment：付款 → 判断订单是否成立 → 已 Delivered 的订单只找回回执（装备类找实物、找不到报 `DependencyUnavailable` 而**不重造**；耗材类直接 AlreadyResolved）→ 按 Kind 交付（装备入库 / 耗材授予 1 份到买家耗材栈）→ 确认交付。交付或确认失败时**刻意不退款、不回滚库存**；耗材订单没有收货人 Equipment 时停在待交付等同 RequestId 重试。

商店不自己数天：`AdvanceShopDay` 由 GameMode 在换日副作用里按 Run 的 DayIndex 调用，且只接受严格更大的天。

赃物售出**绕过 Coordinator** 直接用 Service（走 Social 自己的 escrow 协议），这是有意的分流，不是遗漏。

### Source/Catfishing/Camp/

拥有 Lake 中那个唯一固定营地的交互边界，以及四条以营地为前提的命令的受理与幂等。它**不拥有被这些命令修改的任何状态**：解除倒地写在 Condition、鱼的归属写在 Items、成像计划写在 Collection。不支持建造、装饰、成长或搬迁。

| 文件                     | 职责                                                                                                                                   |
| ------------------------ | -------------------------------------------------------------------------------------------------------------------------------------- |
| `CatCampSettings.h/.cpp` | 只有三样：营地总 gate、统一交互半径、结算夜篝火封面事件 ID。`IsRuntimeReady()` 要求半径是有限正值，所以半径 0 不是合法配置而是未裁状态 |
| `CatCampHubActor.h/.cpp` | 关卡放置的营地宿主。持有营地根节点、固定救援落点、关卡显式关联的鱼缸引用，以及四张命令幂等缓存（休息、救援、入缸、篝火）               |

四条命令全部先重放再执行，且都把真正的状态写入委派出去：休息与救援 → `ConditionComponent`，入缸 → `ItemsService::TransferOwnedFish`，篝火 → `RunImprintService` + 一个只启动可跳过表现的广播。

营地范围判定（`ResolveCharacterInCamp`）要求 authority + settings ready + 距离在半径内，**客户端不能自报位置**。

`UCatCampSettings` 还被两处外部读：PlayerController 的吃鱼链（从共享鱼缸取鱼时复用同一半径）和 `RunImprintService`（用封面事件 ID 决定结算归档要不要等篝火封面计划）。

## 运行时真相

核心规则：每类状态只有一个写入者，其他系统通过 Command/Result、只读快照或复制结果消费。

| 真相                                                       | 唯一写入者                                                                      | 主要消费者                                            |
| ---------------------------------------------------------- | ------------------------------------------------------------------------------- | ----------------------------------------------------- |
| Session / World / Transport / ActiveOperation 四份联机事实 | `UCatOnlineSubsystem`                                                           | UI、PlayerController、GameMode teardown               |
| Run 聚合（相位、天数、额度、世界进度、终局原因、Revision） | `ACatfishingGameModeBase`                                                       | GameState 复制、Run StateTree、Sacrifice、Fishing、UI |
| StableNetId 准入注册表、重连与踢人记录                     | `ACatfishingGameModeBase`                                                       | 所有玩法命令的准入 gate（服务器私有，不复制）         |
| 6 份公开复制快照                                           | `ACatfishingGameState`（写口分属 GameMode 与 SocialService）                    | UI、客户端只读逻辑                                    |
| 个人 ready、公开鱼图鉴摘要                                 | `ACatfishingPlayerState`                                                        | GameMode、UI                                          |
| 三项生存属性                                               | `UCatSurvivalAttributeSet`（写入者：Character 初值 / Condition / 诊断 Ability） | Condition、Fishing、Boundary、UI                      |
| Wet / Downed / RecoveryMode                                | `UCatConditionComponent`                                                        | Camp、Fishing、Social、UI                             |
| 单猫装配、竿耐久、一局耗材                                 | `UCatEquipmentComponent`                                                        | Fishing、Boundary、PlayerController、UI               |
| 团队装备库实例表                                           | `UCatTeamEquipmentLibrary`                                                      | ShopOrderCoordinator、GameState 复制                  |
| 个人鱼护 ID                                                | `ACatCharacter`                                                                 | Camp、PlayerController、Boundary                      |
| 共享鱼缸容器 ID                                            | `ACatFishTankActor`                                                             | Camp、Items                                           |
| 鱼实例、容器数组、预留、售卖冻结、偷鱼 escrow              | `UCatItemsService`                                                              | Fishing、Social、Sacrifice、Shop、复制组件            |
| 窝点集合与三轴池                                           | `UCatChumSpotSubsystem`                                                         | 环境查询（客户端不创建这个子系统）                    |
| 水域几何                                                   | `ACatWaterRegion`（编辑期配置，运行时无人写；它自己在服务器按节拍写 Condition 的 Wet） | `UCatWaterQuerySubsystem`、投窝可达性判定             |
| 单次钓鱼阶段、参与者、抢抄终态                             | `ACatFishingSession`                                                            | FishingService、复制到客户端                          |
| 某钓手当前唯一活跃会话                                     | `UCatFishingService`                                                            | 只有它自己                                            |
| AttemptId 生命周期与 EncounterSpec                         | `UCatFishingBoundarySubsystem`                                                  | FishingService                                        |
| OperationId 分配与 Result Inbox                            | `FCatFishingOperationJournal`                                                   | Boundary、FightCursorLedger                           |
| 献祭协议阶段                                               | `UCatSacrificeCoordinator`                                                      | 只有它自己（对外只返回值拷贝）                        |
| CapturePlan / Grant / ACK 三条投递链                       | `UCatRunImprintService`                                                         | PlayerController、GameMode 的 Host exit 与结算归档门  |
| 本地永久档案与 Grant Journal                               | `UCatProfileSubsystem`                                                          | PlayerController ACK、公开图鉴摘要发布                |
| 偷鱼协议、公开叼鱼列表、运行期 Social 权限                 | `UCatSocialService`                                                             | GameState、Items、PlayerController                    |
| 团队钱包、库存、账本、商店天序号                           | `UCatShopEconomyService`                                                        | ShopOrderCoordinator、SocialService、GameState 快照   |
| 四条营地命令的幂等终态                                     | `ACatCampHubActor`                                                              | 只有它自己                                            |

## 跨模块运行链路

要定位跨域问题时，先在这里确认改动会穿过哪些边界。

**组局到进入 Lake**

```
CatTravelWidget（意图）
  → CatLocalPlayerUISubsystem::HandleActionRequested
  → CatOnlineSubsystem::Request*（Create/Find/Join/AcceptInvite/Leave）
  → OSS 回调 → ServerTravel / ClientTravel
  → HandlePostLoadMap 收口
  → ACatfishingGameModeBase::PreLogin / PostLogin（准入注册表）
  → 生成 ACatCharacter
```

Travel API 返回成功**不等于**到达，只有 PostLoadMap / TravelFailure / NetworkFailure 收口后才更新终态。

**一局的推进**

```
ST_RunFlow 资产
  → FCatRunEnterPhaseTask → GameMode::EnterRunPhaseFromStateTree
  → RefreshEnvironmentAndPublish → GameState 复制
GameMode 白天计时器到点 → 发 DayElapsed
翻天确认点 → 按额度成败发 AllEligibleReady / QuotaFailed
  → FCatRunResultReasonCondition 在同一 Tag 下分边
```

**抛竿到入账（最长的一条）**

```
ServerStartFishingSession
  → FishingService::StartFishingSession
      → BoundarySubsystem::Start        生成 AttemptId
      → BoundarySubsystem::CastAccepted 查水域(Environment) → 抽鱼(Data) → 冻结 EncounterSpec
      → Spawn ACatFishingSession
  → StateTree 推进阶段
  → ServerRequestScoop → Session::RequestScoop
      → ItemsService::CommitCapture     ← 不可逆提交点，鱼在这里成为实物
      → RunImprintService 记钓起轨 + 抄获轨，建 CapturePlan
          → ClientReceiveProfileGrant → ProfileSubsystem::ApplyGrant
              → SaveGame 两阶段落盘 → bAckAllowed
          → ServerAcknowledgeProfileGrant → RunImprintService::AcknowledgeGrant
```

**鱼的几个去向**

```
个人鱼护 ──TransferOwnedFish──→ 共享鱼缸（经 CampHubActor）
        ──ConsumeFish────────→ Condition 加毒或不变，鱼消失
        ──ReserveFish/Commit─→ SacrificeCoordinator → GameMode 额度写口
        ──PrepareFishSale────→ ShopOrderCoordinator → 钱包入账 → drain
被偷走 ──BeginFishTheft─────→ Social escrow → 返还 / 吃掉 / 卖掉
```

**Host 退出**

```
CatOnlineSubsystem::RequestLeave(Host)
  → GameMode::RequestRunTeardown
      → SacrificeCoordinator::PrepareForRunTeardown
          → Fishing 关 → Social 关 → Items 关 → ShopEconomy 关（顺序有语义）
      → 等待远端 DestroySession ACK 与最终 Grant ACK（有界，超时只推进不伪造）
  → DestroySession → BeginTravelToFrontend → PostLoadMap 收口
```

## Config 与资产入口

改配置前先在这里定位段名，不要全文搜 ini。

### 各模块的 Settings 段

全部在 `Config/DefaultGame.ini`，段名一律是 `[/Script/Catfishing.<SettingsClass>]`。

| 模块             | 段名                      | 当前关键项                                                                                                                                                               |
| ---------------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Online/`        | `CatOnlineSettings`       | `SessionAccess=FriendsOnly`、`SessionPublicConnectionLimit=8`、`SessionOperationTimeoutSeconds=30`、`HostExitAckTimeoutSeconds=10`、`ReconnectRecordTtlSeconds=60`       |
| `Run/`           | `CatRunSettings`          | `bEnableRunRuntime=True`、`DayLengthSeconds=1200`、`QuotaTarget=3`、`FinalDayIndex=10`、`RunFlowStateTree=/Game/Catfishing/StateTree/ST_RunFlow`（由 `-run=CatBuildStateTreeAssets` 生成） |
| `Fishing/`       | `CatFishingSettings`      | `bEnableFishingRuntime=True`（注：旧的 `InitialFishDistanceMeters` 已删，D₀ 改由鱼漂射程决定）、`FishingSessionStateTree=…/ST_FishingSession`（v0 骨架：Probe 等满会话冻结的咬钩间隔，再 10/5 秒延时边推到 NearShore，30 秒可抄窗；近岸目标由服务器按钓手位置取水域 AABB 最近点，D-12）、`ScoopReachCentimeters=100`（飞书已裁）、`TrueBiteWindowSeconds=10`（暂定）、`TerminalReplicationWindowSeconds=5`（工程值） |
| `Environment/`   | `CatEnvironmentSettings`  | `bEnableEnvironmentRuntime=True`、`ConfiguredWeather=Unknown`（不钉死天气，交给按槽调度；人工验收时可临时改成 Rain/Fog）、晨昏分段比例、天气权重 0.6/0.25/0.15（暂定 D-24）、`WeatherScheduleSeed=0`（按 RunId 派生）、`NaturalAggregationRegionId=ForestLake`、`NaturalSchoolEmergenceChance=0.25` 与 `AggregationMomentCooldownSeconds=300`（暂定 D-25）。窝料数值仍走 C++ 默认 |
| `Data/`          | `CatFishCatalogSettings`  | `DataRevision=548`、飞书来源戳、12 条 `+Definitions`                                                                                                                     |
| `Equipment/`     | `CatEquipmentSettings`    | `DataRevision=656`（655→656：三条漂补射程/精准度并改路线名）、17 条 `+Definitions`（四条普通饵 08-20 起 `bRunConsumable=true`）、starter 三件套 Id、`RunConsumableStackCapacity=5`（飞书暂定，D-13）、`RodFailureDurabilityLoss=0`             |
| `AbilitySystem/` | `CatAbilitySettings`      | `bEnableCharacterAbilityRuntime=True`、`ReplicationPolicy=Full`、三项初值（Poison 为 0，FishingStrength=50，FightStamina=100）、`bEnableDiagnosticAbility=False`         |
| `Condition/`     | `CatConditionSettings`    | `PoisonDownedThreshold=100`；恢复路径没有数值配置                                                                                                                         |
| `Items/`         | `CatItemsSettings`        | `PersonalGuardCapacity=4`、`SharedFishTankCapacity=12`                                                                                                                   |
| `Camp/`          | `CatCampSettings`         | `bEnableCampRuntime=True`、`InteractionRadiusCentimeters=300`、`CampfireCoverEventId=None`                                                                               |
| `ShopEconomy/`   | `CatShopEconomySettings`  | `StartingTeamWalletBalance=10`、4 条 `+CatalogEntries`（2 级竿、免费虫虫饵、免费保底竿、暂定价窝料 D-14）、`FishPurchasePricePolicy=Unset`                                                   |
| `Social/`        | `CatSocialSettings`       | 四类玩法各自的开关、距离、冷却；`SharedTankRecoveryPolicy=OriginalOwner`；`TheftCaughtImprintEventId=TheftCaught`（与印记准入名单逐字一致）                                                  |
| `Profile/`       | `CatProfileSettings`      | `SaveSlotBaseName=CatProfile`、`bEnableExternalImprintCaptureBridge=True`、`MaxLocalAlbumImprints=0`                                                                     |
| `Collection/`    | `CatImprintSettings`      | `+AllowedImprintEventIds=TheftCaught`（唯一已拍定触发，D-07）、`MaxRunImprintCandidates=8`（工程暂定，D-15）                                                                               |
| `Collection/`    | `CatImprintMediaSettings` | 4 MiB / 64 KiB / 128 块、白名单 PNG+JPEG                                                                                                                                 |
| `UI/`            | `CatUISettings`           | `bEnableLakeStatusView=True`                                                                                                                                             |
| `Input/`         | `CatInputSettings`        | `bEnableGlobalInputContexts=False`                                                                                                                                       |

两个坑：

- `CatProfileSettings` 在 `DefaultGame.ini` 里出现了**两段**（键不重叠所以当前不冲突，UE 会合并）。改 Profile 配置时两处都要看，别只改到一处。
- （已解除）`CatFishingSettings` 早已有 ini 段；上面那一行是旧事实。

`UCatOnlineSettings` 继承的是 `UObject` 而不是 `UDeveloperSettings`，因此它不出现在编辑器的项目设置面板里，只能改 ini。

### 引擎侧配置（`Config/DefaultEngine.ini`）

| 关注点   | 段与项                                                                                                                                         |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| 默认地图 | `GameMapsSettings`：Editor/GameDefaultMap = Frontend，`ServerDefaultMap` = Lake                                                                |
| 网络驱动 | `Engine.GameEngine` 的 `GameNetDriver` = `SteamNetDriver`，回退 `IpNetDriver`；`OnlineSubsystemSteam.SteamNetDriver` 指定 `SteamNetConnection` |
| Steam    | `DefaultPlatformService=Steam`、`bEnabled=true`、`SteamDevAppId/SteamAppId=480`（开发占位）、`bInitServerOnClient=true`                        |
| 渲染     | Substrate、VSM、Lumen、RayTracing 全开，DX12 + SM6                                                                                             |

`Config/DefaultInput.ini` 目前只有引擎模板的 AxisConfig，没有项目自定义输入。

### 资产入口

| 路径                                         | 内容                                                           |
| -------------------------------------------- | -------------------------------------------------------------- |
| `Content/Catfishing/Maps/Frontend.umap`      | 前台菜单地图。World Settings 挂 `ACatFrontendGameMode`         |
| `Content/Catfishing/Maps/Lake.umap`          | 一局玩法地图。World Settings 挂 `ACatfishingGameModeBase`      |
| `Content/Catfishing/Data/Fish/*.uasset`      | 12 个 `UCatFishDefinition`，由 `CatFishCatalogSettings` 索引   |
| `Content/Catfishing/Data/Equipment/*.uasset` | 17 个 `UCatEquipmentDefinition`（竿 2 / 漂 3 / 饵 8 / 窝料 4） |

**GameMode 挂在地图的 World Settings 里，不在任何 ini**。全文搜 `GlobalDefaultGameMode` 是搜不到的，要确认得读 `.umap`。

`Content/` 下**没有任何 StateTree 资产**，也没有玩法蓝图——GameMode、Character、Widget 全是 C++ 直接生效。`Lake.umap` 里目前只放了 `StageA_PlayerStart` 和一个名为 `StageA_Ground` 的 Cube，**没有放置任何 `ACatWaterRegion`、`ACatCampHubActor` 或 `ACatFishTankActor`**。

`Content/Developers/` 下只有开发者 Collections，没有玩法资产。

### 构建

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development 'D:\UnreaProjects\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReload
```

模块依赖在 `Source/Catfishing/Catfishing.Build.cs`：

- Public：`Core`、`CoreUObject`、`Engine`、`DeveloperSettings`、`UMG`、`GameplayAbilities`、`GameplayTags`、`NetCore`、`OnlineSubsystem`、`StateTreeModule`
- Private：`InputCore`、`GameplayTasks`、`GameplayStateTreeModule`、`EnhancedInput`、`Slate`、`SlateCore`、`OnlineSubsystemUtils`

启用插件（`Catfishing.uproject`）：EnhancedInput、GameplayAbilities、StateTree、GameplayStateTree、OnlineSubsystemSteam；PythonScriptPlugin 与 ModelingToolsEditorMode 仅 Editor。

## 当前 fail-closed 与断链

下面这些是 2026-08-19 逐文件读源码与配置得到的接线状态，**它会过期**——动手前用源码复核，不要把本节当结论引用。列在这里是为了避免"以为坏了其实是没配"和"以为通了其实没接"这两类误判。

**显式 fail-closed（ini 里有注释说明是有意为之，等飞书裁决）**

| 现象                                                  | 位置                                                                                      |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| 售鱼整条不可达                                        | `FishPurchasePricePolicy=Unset` 且档位表为空                                              |
| 印记候选 → CapturePlan → Imprint Grant → 相册整条不通 | `AllowedImprintEventIds` 空清单 + `MaxRunImprintCandidates=0` + `MaxLocalAlbumImprints=0` |
| 每日进货不会发生                                      | 目录里没有任何 `bDailyRestock=True` 条目                                                  |
| 环境事件拍不出印记                                    | 五类"抛印记"事件的候选提交链路已接，但 `AllowedImprintEventIds` 仍只放行 `TheftCaught`（需策划裁决，D-26） |
| 输入无副作用                                          | `bEnableGlobalInputContexts=False` 且 Content 无输入资产                                  |

在这些位置补一个"暂定值"，会让服务器按编造的数值真实入账或真实发授予。要改先拿到飞书裁决。

**配置缺失导致的不可达（ini 里没有对应说明，性质待确认）**

- （已解除）`CatFishingSettings` 段与 `RunFlowStateTree` 已配置，两棵 StateTree 资产由 `Source/CatfishingEditor/StateTree/CatBuildStateTreeAssetsCommandlet.cpp` 生成到 `Content/Catfishing/StateTree/`；但 `ST_FishingSession` 仍是只靠延时推进阶段的 v0 骨架：Probe→TrueBiteWindow→HookedFight→NearShore（停 30 秒等抢抄，超时 Terminated），近岸目标是"钓手面前水域 AABB 最近点"的 v0 近似（D-12）；没有玩家输入事件、搏斗交换和失败预算。
- Social 的 `TheftSaleShopAnchorTag`、`TheftSaleShopRangeCentimeters`、`TheftConsumeVictimEscapeDistanceCentimeters` 未配 → 偷鱼的"卖掉"和"吃掉"两个终态走不到，窗口到期只会原样返还。
- `Lake.umap` 未放置水域、营地、鱼缸 Actor → 水域查询返回 `RegionNotFound`，投窝可达性判定为 false，营地与入缸命令无宿主。

**已实现但没有生产调用方**

- 竿耐久唯一扣减入口 `CommitFightRodDurabilityFromAuthority` 零生产调用；上游 `ApplyFightExchangeFromStateTree` 校验通过后直接返回 `PolicyUndecided`。当前竿耐久只在装配时被写满一次，永远不会下降。
- Boundary 的 Bait / Fight / Terminal 三条链只有测试在调；`Terminal` 没有对应的 Subsystem 方法，**捕获事务是 Session 直接对 Items + Collection 发起的，绕过 Boundary**。9 种 ReceiptKind 里只有两种有发行点。
- `UCatImprintMediaTransportService` 全部入口零生产调用。
- `UCatProfileSubsystem` 的相册与装备选择读写入口（`GetRunAlbumSummaries` 等六个）只有测试调用，UI 没接。
- `ACatCampHubActor::IsControllerInCamp` 公有但零调用方（可能是删除修竿代码后的悬空入口）。
- `FCatFishingSessionSnapshot` 复制出去后没有任何 C++ 读者，表现侧未接。
- `UCatFishingBoundarySubsystem::BaitAccepted` 零生产调用方：普通饵已是一局消耗品（D-13），但"每次咬钩扣 1 份饵"的真实扣减仍没接进钓鱼流程，`BaitAccepted` 对普通饵也还只发语义 Receipt。

**已知的重复与残留**

- `TryGetFightCapability` 在 `UCatFishingService` 和 `UCatFishingBoundarySubsystem` 里有**两份逐行相同的实现**。只改一处会静默分叉。
- `ACatfishingPlayerState::HasServerAuthorizedEquipmentUnlock` 现在只认 owning client 上报、服务器校验后持有的解锁清单（D-04 已执行），但项目里**没有任何生产入口产出 Unlock Grant**，所以 Profile 选择路径上的非 starter 装备仍然装不上；买来的装备走团队装备库取用路径（`ServerTakeTeamEquipment`），不受此影响。
- `ECatEquipmentKind` 的 `Herb`/`Driftwood`、`ECatFishingFailurePenalty` 的 `DamageRod`、`ECatRecoveryMode` 的 `Herb` 都是保留死值。`ECatRecoveryMode::Herb` 特别注意：删掉它会让 `CarriedToCamp` 的整数值前移，导致跨版本客户端读错恢复方式。
- `CatEquipmentSettings::DriftwoodDefinitionId` 一旦被填就必然让整个目录 fail-closed（校验要求它指向运行目录内的 Driftwood 定义，但 Driftwood 被无条件拒绝进运行目录）。当前填 `None`，不触发。
- Host 离开后不选新房主：`UnregisterSessionMember` 在被注销者是房主时只清空房主身份。飞书要求自动移交给最早加入者，本地工作计划明写不实现 Host Migration——这条冲突当前未裁决，依赖"局内始终有房主"会踩空。

## 审查基线

先判断改动属于哪个系统，再读对应入口。不要按"谁调用它"或"它挂在哪个 Actor 上"来移动文件或评估职责。

审查结论必须区分三类事实：

- **源码事实**：当前 `Source/`、`Config/`、`Content/` 中存在并可构建的内容。
- **架构事实**：`AGENTS.md`、`Docs/Development/项目开发工作计划.md` 与本文已经定下的边界。
- **产品事实**：飞书「小猫钓鱼」当前 GDD 确定的玩法规则。本轮用户指令若与飞书冲突，应显式处理，不得静默写成本地第三套口径。

旧文档里的拟定名、空模板状态或候选方案不能压过当前源码事实；当前源码里的临时 gate、未配置默认值或诊断 Ability 也不能被当成最终产品裁决。

几条从本轮采集里能直接复用的判读经验：

- 看到 `Undecided` / `Unset` / 0 / -1 这类哨兵，先假定它是"没裁过"而不是"裁定为零"，再去 ini 注释里找说明。
- 看到某个入口只有 `Tests/` 在调，不要据此判断它是死代码，也不要据此判断它已接线——先确认预期的上游是谁。
- 幂等缓存里存**失败**结果的地方（比如吃鱼）通常有不可逆副作用在先，改这类缓存前要先弄清副作用边界。
- 跨域链上标了"刻意排在 X 之前/之后"的顺序，基本都是为了控制不可逆点的位置，调整顺序等于改变失败时的回退边界。
