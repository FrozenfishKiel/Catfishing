# Catfishing

Catfishing 是一个基于 Unreal Engine 5.8 的联机钓鱼与营地协作项目。当前工程重点不是先堆完整玩法，而是先建立一套能继续承载后续功能开发的框架：单 Runtime 模块、明确的领域目录、服务器权威的事务入口、失败时 fail-closed 的配置门禁，以及可以被 UE Automation 重复验证的模块合同和第一条跨模块纵向切片。

当前代码已经具备 Frontend/Lake 基础地图、Online/Travel 状态框架、角色与 AbilitySystem 骨架、局流程与 StateTree 适配点、钓鱼/鱼容器/正式库存/图鉴/档案/营地/社交等领域服务的程序合同。正式数值、输入资产、鱼表、装备表、Steam 双账号联机和完整产品体验仍需要后续补齐与人工验收（三棵 StateTree 资产已在 `Content/Data/StateTrees/`，2026-09-15 核）。

## 先看模块：当前代码分成哪些系统

先把最容易混淆的地方说清楚：工程层面目前只有一个 Unreal Runtime 模块，名字叫 `Catfishing`；下面这些“模块”指的是 `Source/Catfishing/` 里的业务系统目录。也就是说，程序员平时找代码时看的是这些目录，而不是去找多个 UE Runtime Module。

`Source/Catfishing/` 下现有 26 个系统目录（2026-09-15 数），下表只列其中 18 个；Growth、Input、Interaction、Items、Physics、Save、Settings、ShopEconomy 八个尚未入表，先看目录本身：

| 分组 | 模块目录 | 它负责什么 | 你什么时候会看它 |
|---|---|---|---|
| 框架宿主 | `Framework/Game/` | UE 游戏框架宿主，放 GameMode、GameState、PlayerState、PlayerController。 | 查玩家进入局、生成角色、局内宿主日志、服务器权限入口时看。 |
| 框架宿主 | `Framework/Core/` | 跨系统共享的轻量合同，放 Run、Environment、HelpSignal 这类 DTO。 | 多个系统都要引用同一份只读事实或命令结构时看。 |
| 联机与旅行 | `Online/` | Session、邀请、Frontend/Lake 旅行、网络失败和联机快照。 | 改 Host、Find、Join、Leave、掉线恢复、Steam 接线时看。 |
| UI | `UI/` | LocalPlayer 级 UI 生命周期、原生 Widget、只读 View DTO。 | 改界面展示、按钮意图转发、HUD 数据读取、跨地图 UI 清理时看。 |
| 角色身体 | `Character/` | 角色本体、Pawn/Character 生命周期、输入桥。 | 改 `ACatCharacter` 自己的身体生命周期、Possess/UnPossess、输入绑定时看。 |
| 角色身体 | `AbilitySystem/` | GAS 能力、属性集、Ability 诊断类和 Ability 配置。 | 改属性、Ability、ASC 初始化、能力激活和属性复制时看。 |
| 角色身体 | `Condition/` | 猫的身体状态域，例如湿身、倒地、恢复、救援状态。 | 改身体状态判定、倒地恢复、救援事实、状态 Revision 时看。 |
| 局流程 | `Run/` | 一局流程、阶段切换、供品结算、Run StateTree 适配点。 | 改开局/结算/供品目标/局阶段，或接正式 `ST_RunFlow` 时看。 |
| 核心玩法 | `Fishing/` | 钓鱼会话、阶段入口、协作搏斗、近岸抢抄事务。 | 改抛竿、搏斗、鱼池选择、捕获结算、`ST_FishingSession` 接线时看。 |
| 核心玩法 | `FishContainers/` | 鱼实例、鱼护/共享缸容器、鱼事务和 FastArray 复制边界。 | 改捕获提交、鱼转移、吃鱼、售鱼、偷鱼 escrow 或鱼容器复制时看。 |
| 核心玩法 | `Inventory/` | 正式道具实例、背包/公共库存、库存槽位事务。 | 改装备/消耗品进入库存、库存移动、存档恢复或 UI 背包数据时看。 |
| 核心玩法 | `Collection/` | 本轮图鉴/印记计划和 durable grant。 | 改捕获后记录图鉴、多人印记候选、Grant ACK 时看。 |
| 核心玩法 | `Profile/` | 本地档案、持久化和跨局收藏合同。 | 改 SaveGame、档案 slot、跨局收藏落盘、Profile 与图鉴桥接时看。 |
| 数据与配置 | `Data/` | 鱼定义、鱼目录和数据选择规则。 | 改鱼表结构、鱼种运行时可用性、巨鱼筛选和鱼池查询时看。 |
| 数据与配置 | `Equipment/` | 装备定义、装备状态和局内消耗。 | 改鱼竿、鱼饵、窝料、耐久、装备快照和装备门禁时看。 |
| 场景环境 | `Environment/` | 环境、天气、时间、水域和聚鱼查询。 | 改水域区域、时间/天气、自然聚鱼、环境快照时看。 |
| 场景环境 | `Camp/` | 营地、共享缸、休息、救援和篝火协作。 | 改营地交互、共享缸、救援送回营地、篝火回看入口时看。 |
| 玩家交互 | `Social/` | 玩家间交互协议。 | 改偷鱼、恶作剧、求助、防骚扰牌和玩家间权限校验时看。 |
| 基础设施 | `Logging/` | 项目日志分类声明。 | 新增跨系统日志分类，或核对 `LogCatfishing` / `LogCatOnline` 时看。 |

每个系统如果需要自动化测试，就在自己的 `Tests/` 子目录里放测试文件。例如联机系统的测试在 `Source/Catfishing/Online/Tests/`。没有稳定测试入口的系统不要保留空目录或失效路径。

目录归属按“类本身属于哪个系统”判断，不按“它服务谁”判断。比如 `UCatSurvivalAttributeSet` 属于 `AbilitySystem/`，不是 `Character/`；GameMode、PlayerState 这类项目大父类属于 `Framework/Game/`，不是模块根目录。

## 文档目录怎么读

| 目录 | 存放内容 | 什么时候看 |
|---|---|---|
| `Docs/Development/` | 当前开发交接、实现报告、决策台账等“正在执行的工程说明”。 | 接手任务、判断当前阶段目标、补测试或核验测试边界时先看。 |
| `Knowledge/Development/` | 框架阅读入口（21 行；2026-09-10 起早期接线细节已删）。 | 进框架前按它的读取顺序走；当前工程事实先看 `Knowledge/Framework/` 和真实源码。 |
| `Docs/gap-analysis/` | 设计对代码的对表报告，四轮按日期分目录（09-09、09-09-1321、09-11、09-13），每轮 `RUN.json` 记上一轮为基线；**最新基线是 `2026-09-13/`**。 | 讨论差异时看该轮 `SUMMARY.md` 与 `回填清单.md`；它不是需求入口，也不是进度清单。 |
| `Docs/design-reviews/` | 设计侧过程材料：各轮评审报告、删除前备份、整合稿，按 `<日期-主题>/` 分文件夹（2026-09-15 从设计项目并入）。 | 查「这条裁决当时评了什么、原稿长什么样」时看；不是需求入口，里面的数字不当现行值。 |
| `Docs/sim/` | 数值模拟脚本与 08-16／08-19 两轮报告（纯 Python 标准库）。 | 改参数前复跑；报告数字是当时的模拟结果，现行值看 `Knowledge/Design/数值模拟与参数记录.md`。 |
| `Knowledge/Framework/` | 项目地图、规则、术语、决策、已知问题。 | 新程序员入场、整理模块边界、判断文件该放哪里、确认团队统一语言时看。 |
| `Knowledge/Design/` | **设计真值**（2026-09-11 起）：GDD 九册、愿景、裁决账本、参数页与内容表。飞书降级为草稿／点子／同步页。 | 写具体玩法逻辑、数值、交互规则和产品验收用例前必须看。 |
| `.codex/docs/` | 目前只有 `items-inventory-review.md`（2026-09-10 快照，库存链此后已大改，见其页头声明）；早先说的测试报告、验收报告、Review 报告三份不存在。 | 测试与验收实况看 `Docs/Development/` 最新一份交接说明。 |
| `Config/` | UE 默认地图、Online、输入和项目配置。 | 修改启动地图、平台服务、输入类或项目级开关时看。 |
| `Source/Catfishing/` | 单 Runtime 模块的 C++ 实现和各系统 `Tests/`。 | 日常代码开发和 Review 的主入口。 |
| `Content/Catfishing/Maps/` | `Frontend`、`Lake` 两张基础地图，另有 `TestMap`；玩法默认地图由 `Config/DefaultGame.ini` 的 GameplayMap 指定（当前 Showcase2）。 | 验证默认启动、旅行链或未来摆放场景 Actor 时看。 |
| `Scripts/` | 资产创建／迁移脚本、设计清单刷新脚本与少量 verify 脚本；2026-09-10 起整批 `verify_*.ps1` 已删（可检查面收窄，不是实现被删）。 | 维护验证工具时才看；普通功能开发不应依赖脚本绕过正式入口。 |

版本管理口径：`.codex/docs/` 和 `Knowledge/` 属于项目长期知识，应提交；`.codex/state/`、`Saved/`、`Intermediate/`、构建产物和本地缓存不提交。`Scripts/` 不是日常功能提交的默认范围，只有脚本本身成为稳定工程工具时才单独审查后提交。

## 程序员怎么读代码

建议按下面顺序读，不要从随机 cpp 开始硬啃：

1. 先读本文，了解目录和边界。
2. 读 `Knowledge/Framework/PROJECT_MAP.md`、`RULES.md`、`TERMS.md`，统一项目地图、文件归属和术语。
3. 读 `Docs/Development/` 最新一份交接说明的〈测试〉节（现为 `2026-09-15-批7改动交接给两位程序.md`），确认当前测试护栏保护了哪些行为。
4. 读 `.codex/docs/testing-report.md`，确认当前已经验证过什么、还没验证什么。
5. 读 `Source/Catfishing/Catfishing.Build.cs`，确认模块依赖和 include 根。
6. 按目标系统进入对应目录，例如 Online 先看 `Online/CatOnlineSubsystem.*`，钓鱼先看 `Fishing/CatFishingService.*` 和 `Fishing/CatFishingSession.*`。
7. 读对应 `Tests/`，用测试理解公开接口的行为合同。

如果要补实际玩法，不要绕过当前正式入口。例如钓鱼捕获必须通过 Fishing/FishContainers/Collection/Profile 的公开链路推进；正式库存动作走 `Inventory/` 的事务入口；Online 旅行必须收敛到 `UCatOnlineSubsystem`；Run/Fishing 的正式流程拓扑应由 StateTree 资产拥有，C++ 只提供权威原语。

## 项目怎么运行

### 基础环境

- Unreal Engine：5.8（版本号见 `Catfishing.uproject` 的 `EngineAssociation`）。
  **引擎装在哪台机器上都不一样，本仓库任何文档都不写死它的绝对路径**——下面的命令统一用
  `$env:UE_ROOT` 指代引擎安装目录，每个新终端先设一次：

  ```powershell
  $env:UE_ROOT = '<你机器上 UE 5.8 的安装目录>'   # 该目录下应有 Engine\Build\BatchFiles\Build.bat
  ```

  别指望自动发现：注册表 `HKLM:\SOFTWARE\EpicGames\Unreal Engine` 只登记 Launcher 装的版本，
  本机那里有 4.27／5.3～5.6 而**没有 5.8**。
- 项目文件：`Catfishing.uproject`
- 默认地图：`/Game/Catfishing/Maps/Frontend`
- 当前基础地图：`Frontend`、`Lake`

### 构建

在项目根目录运行：

```powershell
& "$env:UE_ROOT\Engine\Build\BatchFiles\Build.bat" CatfishingEditor Win64 Development -Project="$PWD\Catfishing.uproject" -WaitMutex -NoHotReload

& "$env:UE_ROOT\Engine\Build\BatchFiles\Build.bat" Catfishing Win64 Development -Project="$PWD\Catfishing.uproject" -WaitMutex -NoHotReload
```

### 自动化测试

本节的条数与红项清单是 2026-09-12 的快照，最新统计见 `Docs/Development/2026-09-15-批7改动交接给两位程序.md`（228 条、220 通过、8 失败）。当时批次是 88 条 `Catfishing.Unit.*`。**`Catfishing.Slice.*` 套件已不存在**（源码里已无该命名空间），
下面的口径全部按 `Catfishing.Unit` 写。

日常跑：

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\Catfishing.uproject" -ExecCmds="Automation RunTests Catfishing.Unit; Quit" -unattended -nopause -nosplash -NullRHI -log
```

结果在 `Saved/Logs/Catfishing.log`，数两个标记：

```powershell
(Select-String -Path 'Saved/Logs/Catfishing.log' -Pattern 'Result=\{Success\}' -AllMatches).Matches.Count
(Select-String -Path 'Saved/Logs/Catfishing.log' -Pattern 'Result=\{Fail\}' -AllMatches).Matches.Count
```

**09-12 时是 81 通过 / 7 失败。** 当时的 7 条红项（09-13、09-14 已全部修掉；现在仍红的 8 条见 09-15 交接）：

```text
Catfishing.Unit.Fishing.BiteTiming.WorldFieldsDriveFormalStateTreeAndBobber
Catfishing.Unit.Fishing.Effort.ActualWorkAndSupportPreservePricingIndependenceUnderFiniteReel
Catfishing.Unit.Fishing.Effort.ExhaustionAndReleasedSlackRespectEffortBoundaries
Catfishing.Unit.Fishing.Effort.LineLimitStopsRecoveryOnArrivalAndUsesFinalPaidLength
Catfishing.Unit.Fishing.Runner.OperatorSamplesAndSingleASCSettlementRemainConservative
Catfishing.Unit.Fishing.Runner.PrimaryOperatorStrengthAndCostsIgnorePhysicalHelpers
Catfishing.Unit.Inventory.EquipmentItemPickupRejectsFullBagAndPreventsReentrantDoubleGrant
```

**改代码前先建基线**：`git stash -u` 回到对比点跑一遍，回来再跑一遍，比失败集的差集。
不然分不清「你弄红的」和「本来就红的」。这 7 条的成因与修法见
`Docs/Development/2026-09-13-长期红项排查报告.md`（09-12 说明第七节是最早的版本）。

需要完整报告（`index.json`）时：

```powershell
New-Item -ItemType Directory -Force -Path 'Saved/Automation/user-acceptance-01/Report' | Out-Null

& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\Catfishing.uproject" -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache '-ExecCmds=Automation RunTests Catfishing.Unit;Quit' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$PWD\Saved\Automation\user-acceptance-01\Report" "-abslog=$PWD\Saved\Automation\user-acceptance-01\Automation.log"
```

```powershell
$json = Get-Content -Raw 'Saved/Automation/user-acceptance-01/Report/index.json' | ConvertFrom-Json
$json.succeeded; $json.failed; $json.notRun; $json.tests.Count
```

### 打开项目

可以用 UE 5.8 打开 `Catfishing.uproject`。当前默认进入 Frontend。阶段 A 的基础旅行链已经有无窗口验证证据；但 B–G 的正式产品体验还需要补齐输入资产、两棵 StateTree、鱼/装备 DataAsset、水域和营地场景 Actor 后再做人工 PIE/Steam 验收。

## 当前验收边界

当前自动化可以证明：

- 模块公开合同能被 UE Automation 发现并运行。
- Settings/Definition 默认 fail-closed，不会伪造未裁决产品行为。
- FishContainers、Inventory、Collection、Condition、Equipment、Environment、Run、Social 等关键事务具备幂等、Revision、所有权和关门边界。
- FishContainers→Collection 的第一条跨模块纵向切片可以通过公开接口组合。

当前自动化不能证明：

- Steam 双账号 Create/Find/Join/Invite/掉线/重连。
- 可见 PIE 中真实 UMG 按钮、焦点、手柄/键鼠输入。
- 正式 `ST_RunFlow`、`ST_FishingSession` 的资产拓扑。
- Fishing→FishContainers→Collection→Profile 完整链。
- Run→供品结算→FishContainers 完整链。
- Character→AbilitySystem→Condition→UI 正向体验链。
- 多客户端复制、FastArray 客户端收敛和真实网络时序。

这些内容不是“已经通过但没写”，而是明确留给后续纵向切片、Functional Test、Steam 双端测试和人工验收。
