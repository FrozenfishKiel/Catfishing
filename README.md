# Catfishing

Catfishing 是一个基于 Unreal Engine 5.8 的联机钓鱼与营地协作项目。当前工程重点不是先堆完整玩法，而是先建立一套能继续承载后续功能开发的框架：单 Runtime 模块、明确的领域目录、服务器权威的事务入口、失败时 fail-closed 的配置门禁，以及可以被 UE Automation 重复验证的模块合同和第一条跨模块纵向切片。

当前代码已经具备 Frontend/Lake 基础地图、Online/Travel 状态框架、角色与 AbilitySystem 骨架、局流程与 StateTree 适配点、钓鱼/物品/图鉴/档案/营地/社交等领域服务的程序合同。正式数值、输入资产、鱼表、装备表、两棵 StateTree、Steam 双账号联机和完整产品体验仍需要后续补齐与人工验收。

## 文档目录怎么读

| 目录 | 存放内容 | 什么时候看 |
|---|---|---|
| `Docs/Development/` | 当前开发交接、自动化测试方案等“正在执行的工程说明”。 | 接手任务、判断当前阶段目标、补测试或核验测试边界时先看。 |
| `Knowledge/Development/` | 框架接线说明、项目调研结论等稳定技术背景。 | 不确定某个系统为什么这样接线，或需要追溯阶段 A–G 的架构依据时看。 |
| `Knowledge/Framework/` | 项目地图、规则、术语、决策、已知问题。 | 新程序员入场、整理模块边界、判断文件该放哪里、确认团队统一语言时看。 |
| `Knowledge/GDD/` | 正式玩法系统分册，包括钓鱼、联机、营地、装备、图鉴、猫状态等产品规则。 | 写具体玩法逻辑、数值、交互规则和产品验收用例前必须看。 |
| `.codex/docs/` | AI 开发形成的测试报告、人工验收报告、程序员 Review 报告。 | 想知道“这版到底验证过什么、没验证什么、程序员从哪里审查”时看。 |
| `Config/` | UE 默认地图、Online、输入和项目配置。 | 修改启动地图、平台服务、输入类或项目级开关时看。 |
| `Source/Catfishing/` | 单 Runtime 模块的 C++ 实现和各系统 `Tests/`。 | 日常代码开发和 Review 的主入口。 |
| `Content/Catfishing/Maps/` | 当前仅包含 `Frontend` 与 `Lake` 两张基础地图。 | 验证默认启动、旅行链或未来摆放场景 Actor 时看。 |
| `Scripts/` | 地图生成和阶段 A 验证辅助脚本。 | 维护验证工具时才看；普通功能开发不应依赖脚本绕过正式入口。 |

版本管理口径：`.codex/docs/` 和 `Knowledge/` 属于项目长期知识，应提交；`.codex/state/`、`Saved/`、`Intermediate/`、构建产物和本地缓存不提交。`Scripts/` 不是日常功能提交的默认范围，只有脚本本身成为稳定工程工具时才单独审查后提交。

## 模块概要

当前项目保持一个 Unreal Runtime 模块：`Source/Catfishing`。模块内部按领域目录拆分，不按“谁调用它”或“挂在哪个 Actor 上”归类。

| 模块目录 | 作用 | 主要内容 |
|---|---|---|
| `AbilitySystem/` | 角色能力、属性和临时诊断 Ability。 | `UCatSurvivalAttributeSet`、Ability 设置、诊断 Ability。 |
| `Character/` | 角色本体、Pawn/Character 生命周期和输入桥。 | `ACatCharacter`，只放角色自身职责。 |
| `Condition/` | 猫的身体状态域。 | 湿身、倒地、恢复、救援相关状态组件与类型。 |
| `Equipment/` | 装备定义、装备状态和局内消耗。 | 鱼竿、鱼饵、窝料、耐久和装备快照。 |
| `Fishing/` | 钓鱼会话、阶段入口、协作和抢抄事务。 | `UCatFishingService`、`ACatFishingSession`、Fishing StateTree 节点。 |
| `Items/` | 容器、鱼实例、事务、FastArray 复制边界。 | 鱼护/共享缸、捕获提交、转移、消耗、预留/提交/取消。 |
| `Collection/` | 本轮图鉴/印记计划和 durable grant。 | `UCatRunImprintService`、CapturePlan、FishRecorded Grant。 |
| `Profile/` | 本地档案、持久化和跨局收藏合同。 | Profile 设置、Grant ACK、SaveGame 接缝。 |
| `Run/` | 一局流程、阶段、献祭和 StateTree 适配。 | Run contracts、Run StateTree 节点、献祭协调器。 |
| `Environment/` | 环境、天气、时间、水域和聚鱼查询。 | Environment provider、水域区域、水域查询子系统。 |
| `Camp/` | 营地、共享缸、休息、救援和篝火协作。 | `ACatCampHubActor`、营地设置与协作入口。 |
| `Social/` | 玩家间交互协议。 | 偷鱼、恶作剧、求助、防骚扰牌和 Social service。 |
| `Online/` | Session、邀请、旅行、网络失败和四事实快照。 | `UCatOnlineSubsystem`、Online settings/types。 |
| `UI/` | LocalPlayer 级 UI 生命周期、原生 Widget 和只读 View DTO。 | Travel Widget、Survival Widget、LocalPlayer UI Subsystem。 |
| `Data/` | 鱼定义、鱼目录和数据选择规则。 | FishDefinition、FishCatalog settings。 |
| `Framework/Core/` | 跨系统共享的轻量合同。 | Run、Environment、HelpSignal 等公开 DTO。 |
| `Framework/Game/` | UE 游戏框架宿主。 | GameMode、GameState、PlayerState、PlayerController。 |
| `Logging/` | 项目日志分类。 | `LogCatfishing`、`LogCatOnline` 等日志声明。 |

每个模块如果需要自动化测试，都在自己的 `Tests/` 子目录里放测试文件。测试应验证公开行为和正式入口，不读取生产私有缓存，也不添加测试后门。

## 程序员怎么读代码

建议按下面顺序读，不要从随机 cpp 开始硬啃：

1. 先读本文，了解目录和边界。
2. 读 `Knowledge/Framework/PROJECT_MAP.md`、`RULES.md`、`TERMS.md`，统一项目地图、文件归属和术语。
3. 读 `Docs/Development/自动化测试方案.md`，确认当前测试护栏保护了哪些行为。
4. 读 `.codex/docs/testing-report.md`，确认当前已经验证过什么、还没验证什么。
5. 读 `Source/Catfishing/Catfishing.Build.cs`，确认模块依赖和 include 根。
6. 按目标系统进入对应目录，例如 Online 先看 `Online/CatOnlineSubsystem.*`，钓鱼先看 `Fishing/CatFishingService.*` 和 `Fishing/CatFishingSession.*`。
7. 读对应 `Tests/`，用测试理解公开接口的行为合同。

如果要补实际玩法，不要绕过当前正式入口。例如钓鱼捕获必须通过 Fishing/Items/Collection/Profile 的公开链路推进；Online 旅行必须收敛到 `UCatOnlineSubsystem`；Run/Fishing 的正式流程拓扑应由 StateTree 资产拥有，C++ 只提供权威原语。

## 项目怎么运行

### 基础环境

- Unreal Engine：`D:\UE_5.8`
- 项目文件：`Catfishing.uproject`
- 默认地图：`/Game/Catfishing/Maps/Frontend`
- 当前基础地图：`Frontend`、`Lake`

### 构建

在项目根目录运行：

```powershell
& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' CatfishingEditor Win64 Development -Project='D:\UnreaProjects\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReload

& 'D:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Catfishing Win64 Development -Project='D:\UnreaProjects\Catfishing\Catfishing.uproject' -WaitMutex -NoHotReload
```

### 自动化测试

当前完整自动化批次是 64 条测试：63 条 `Catfishing.Unit.*` 和 1 条 `Catfishing.Slice.*`。

```powershell
New-Item -ItemType Directory -Force -Path 'Saved/Automation/user-acceptance-01/Report' | Out-Null

& 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\UnreaProjects\Catfishing\Catfishing.uproject' -unattended -nop4 -nosplash -nullrhi -DDC-ForceMemoryCache '-ExecCmds=Automation RunTests Catfishing;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=D:\UnreaProjects\Catfishing\Saved\Automation\user-acceptance-01\Report' '-abslog=D:\UnreaProjects\Catfishing\Saved\Automation\user-acceptance-01\Automation.log'
```

验收时读取：

```powershell
$json = Get-Content -Raw 'Saved/Automation/user-acceptance-01/Report/index.json' | ConvertFrom-Json
$json.succeeded
$json.succeededWithWarnings
$json.failed
$json.notRun
$json.inProcess
$json.tests.Count
```

当前期望：

```text
succeeded = 64
succeededWithWarnings = 0
failed = 0
notRun = 0
inProcess = 0
tests.Count = 64
```

### 打开项目

可以用 UE 5.8 打开 `Catfishing.uproject`。当前默认进入 Frontend。阶段 A 的基础旅行链已经有无窗口验证证据；但 B–G 的正式产品体验还需要补齐输入资产、两棵 StateTree、鱼/装备 DataAsset、水域和营地场景 Actor 后再做人工 PIE/Steam 验收。

## 当前验收边界

当前自动化可以证明：

- 模块公开合同能被 UE Automation 发现并运行。
- Settings/Definition 默认 fail-closed，不会伪造未裁决产品行为。
- Items、Collection、Condition、Equipment、Environment、Run、Social 等关键事务具备幂等、Revision、所有权和关门边界。
- Items→Collection 的第一条跨模块纵向切片可以通过公开接口组合。

当前自动化不能证明：

- Steam 双账号 Create/Find/Join/Invite/掉线/重连。
- 可见 PIE 中真实 UMG 按钮、焦点、手柄/键鼠输入。
- 正式 `ST_RunFlow`、`ST_FishingSession` 的资产拓扑。
- Fishing→Items→Collection→Profile 完整链。
- Run→Sacrifice→Items 完整链。
- Character→AbilitySystem→Condition→UI 正向体验链。
- 多客户端复制、FastArray 客户端收敛和真实网络时序。

这些内容不是“已经通过但没写”，而是明确留给后续纵向切片、Functional Test、Steam 双端测试和人工验收。
