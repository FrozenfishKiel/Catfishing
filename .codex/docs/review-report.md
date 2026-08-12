# Catfishing A–G 程序员 Review 报告

> **文档状态：待程序员审查**  
> **事实截止时间：2026-08-12 02:21 CST（Asia/Shanghai）**  
> **审查结论边界：** 独立代码审查已完整覆盖当前 A–G 的 103 个 Source 文件、4 个 Config、4 个 Scripts 和 2 个地图资产元数据，旧 F01–F15 均已关闭，最终 `code_findings: none / pass`。本报告仍保持“待程序员审查”，因为它是交给程序员复核改动、风险和产品接线的入口；B–G runtime、人工 PIE 和双账号 Steam 未验证。

## 1. 文档状态

- **程序员 Review 状态：** 待程序员审查。
- **独立代码审查状态：** pass；无当前 open code finding。
- **注释语义审查状态：** CMT01–CMT22 已闭环；CMT16–CMT22 包含 6 处 header 与 1 处 cpp 注释准确性修正。修正后 Editor/Game 同源重构建通过。
- **运行验证状态：** 阶段 A 无窗口 Null OSS 正常/负路径通过；B–G 正常玩法未运行。
- **产品验收状态：** 待人工验收，详见 `.codex/docs/acceptance-report.md`。

## 2. 事实来源

- 原始阶段目标：`Docs/Development/AI开发交接.md`。只取 A–G 范围、架构优先级和产品边界，不采用其中已经过时的“空模板”现状。
- 架构/接线/产品事实：技术方案作为架构，`Knowledge/Development/FRAMEWORK_WIRING.md` 作为接线说明，正式 GDD 作为产品规则；源码事实优先于文档描述。
- 当前代码：`Source/` 103 文件，约 683 KB 文本代码/配置/脚本；完整读取而非抽样。
- 当前配置与资产：`Config/` 4 文件、`Scripts/` 4 文件、Content 仅 Frontend/Lake 两张地图。
- 独立 Review：覆盖 UHT/UE 5.8 API、模块依赖、Online 同步重入/迟到回调/Destroy 补偿、四事实、GI 过滤、teardown/ACK timeout、StateTree 拓扑、FastArray、身份/准入、跨聚合事务、durable grant、Social、统一 gameplay gate、复制/安全和最小性；F01–F15 经过多轮 closure re-review 后最终 pass。
- 最终构建：`Saved/Verification/BG_StaticBuild_20260812_011156/build_CatfishingEditor_final_header_167_179_206.log`、`build_Catfishing_final_header_167_179_206.log`。
- 最终静态汇总：同目录 `final_header_167_179_206_static_summary.log`；Editor 7/7、11.218 s、exit 0，Game 6/6、15.313 s、exit 0，Python AST 4/4，diff-check 0，114 个保护文件 delta 0，无残留进程。
- 最终运行：同目录 `runtime_safe_null_travel_after_wait_end_pie_fix.log`、`runtime_safe_null_missing_lake_failure_after_wait_end_pie_fix.log`。运行日志生成于最后的纯注释修正前；最终重链的 Editor/Game PE `.text` size/hash 与 runtime 副本完全一致。
- 运行门禁：同目录 `runtime_bg_gate_matrix.md`。
- 保护项：`.codex/state`、`.gitignore`、`.slnx`、`Docs/Knowledge` 的既有用户状态排除归因，未作为本轮实现改动审查；本 documentation 节点只新增 `.codex/docs/` 下三份报告。

## 3. 改动总览

本轮把 UE 空白模板扩展为一个**单 Runtime 模块**内的 A–G 服务器权威玩法骨架。核心设计不是“把所有玩法写死”，而是先落稳定合同、所有权、幂等终态和产品门禁：

1. `UCatOnlineSubsystem` 是唯一 OSS/旅行所有者，公开 `Session / World / Role / Transport` 四事实快照。
2. `ACatfishingGameModeBase` 是 Run 权威入口；Run 和 Fishing 的唯一流程拓扑由正式 StateTree 资产提供，缺资产时 fail-closed。
3. `ACatCharacter` 拥有 ASC/属性/状态/装备，PlayerController 只做客户端命令桥和 owning-client 交付。
4. Fishing、Items、Sacrifice、Imprint、Profile 通过 RequestId、StableNetId、revision、terminal cache、reservation/journal 切开跨聚合副作用。
5. 巨鱼协作仅属于 HookedFight；NearShore 不采用旧“双人抄网”，由首个通过权威距离与 revision 校验的 scooper 唯一得到鱼。
6. Profile grant 先 durable journal 再 ACK；CapturePlan 使用全参与者两阶段 batch，避免部分成功。
7. 所有未决数值、权限、复制策略、输入和场景资产保持 unset/disabled/undecided，不用默认 0 值伪造产品体验。

构建与阶段 A 回归已经有新鲜证据；B–G 的代码合同已审查，但正式资产不存在，不能从 Review 报告推导出“玩法已跑通”。

## 4. 真实文件结构与代码地图

以下是当前审查面，不包含保护项和 `Saved/` 生成证据：

```text
Catfishing.uproject
Config/
  DefaultEditor.ini
  DefaultEngine.ini
  DefaultGame.ini
  DefaultInput.ini
Content/Catfishing/Maps/
  Frontend.umap
  Lake.umap
Scripts/
  create_stage_a_maps.py
  verify_stage_a_map.py
  verify_stage_a_travel.py
  verify_stage_a_travel_failure.py
Source/
  Catfishing.Target.cs
  CatfishingEditor.Target.cs
  Catfishing/
    Catfishing.Build.cs
    Catfishing.cpp / Catfishing.h / CatLog.h
    CatOnlineSubsystem.cpp / .h
    CatOnlineTypes.h / CatOnlineSettings.cpp / .h
    CatGameplayTypes.cpp / .h
    CatCharacter.cpp / .h
    CatAbilitySettings.cpp / .h
    CatStageCTestAbility.cpp / .h
    CatSurvivalAttributeSet.cpp / .h
    CatLocalPlayerUISubsystem.cpp / .h
    CatTravelWidget.cpp / .h
    CatSurvivalWidget.cpp / .h
    CatUISettings.cpp / .h
    Framework/Core/
      CatDomainCommandTypes.h
      CatRunContracts.h
      CatProfileContracts.h
      CatSacrificeContracts.h
    Run/
      CatRunSettings.cpp / .h
      CatRunStateTreeEvents.cpp / .h
      CatRunStateTreeNodes.cpp / .h
      CatSacrificeCoordinator.cpp / .h
    Environment/
      CatEnvironmentSettings.cpp / .h
      CatConfiguredEnvironmentProvider.cpp / .h
      CatWaterQuerySubsystem.cpp / .h
      CatWaterRegion.cpp / .h
      CatWaterTypes.h
    Fishing/
      CatFishingSettings.cpp / .h
      CatFishingService.cpp / .h
      CatFishingSession.cpp / .h
      CatFishingStateTreeNodes.cpp / .h
      CatFishingTypes.h
    Items/
      CatItemsSettings.cpp / .h
      CatItemsService.cpp / .h
      CatContainerReplicationComponent.cpp / .h
      CatFishTankActor.cpp / .h
      CatItemTypes.h
    Data/
      CatFishCatalogSettings.cpp / .h
      CatFishDefinition.cpp / .h
    Collection/
      CatImprintTypes.h
      CatRunImprintService.cpp / .h
    Profile/
      CatProfileSettings.cpp / .h
      CatProfileSubsystem.cpp / .h
      CatProfileSaveGame.h
    Equipment/
      CatEquipmentSettings.cpp / .h
      CatEquipmentDefinition.cpp / .h
      CatEquipmentComponent.cpp / .h
      CatEquipmentTypes.h
    Character/
      CatConditionSettings.cpp / .h
      CatConditionComponent.cpp / .h
      CatConditionTypes.h
    Camp/
      CatCampSettings.cpp / .h
      CatCampHubActor.cpp / .h
    Social/
      CatSocialSettings.cpp / .h
      CatSocialService.cpp / .h
      CatProtectionSignActor.cpp / .h
      CatSocialTypes.h
```

代码地图按所有权划分：

- **进程/平台生命周期：** `CatOnlineSubsystem`。
- **局内权威与复制入口：** `CatGameplayTypes` 中的 GameMode/GameState/PlayerState/PlayerController。
- **角色聚合：** `CatCharacter` + ASC/AttributeSet + Equipment/Condition。
- **世界级玩法服务：** Run、Environment、Fishing、Items、Imprint、Social。
- **客户端本地持久与 UI：** `CatProfileSubsystem`、`CatLocalPlayerUISubsystem`。
- **跨域 DTO/错误：** `Framework/Core` 和各域 `*Types.h`。
- **产品事实：** `*Settings`、Fish/Equipment DataAsset、StateTree 和场景 Actor；当前多数尚未装配。

## 5. 变更矩阵

| ID | 阶段/层 | 主要文件与锚点 | 改动职责 | 主要影响 | 已有证据 | 主要风险 |
|---|---|---|---|---|---|---|
| M-01 | 工程基线 | `Catfishing.uproject:3`、`Catfishing.Build.cs:10` | UE 5.8、GAS/StateTree/Steam/Python 插件与单模块依赖 | UHT、Target、运行插件 | Editor/Game 构建通过 | IncludeOrder 仍为 5.6 兼容模式 |
| M-02 | A 地图/启动 | `Config/DefaultEngine.ini:4`、Frontend/Lake | 默认 Frontend 与两地图 GameMode | 冷启动和旅行目标 | 主资产查询/Frontend 无窗口启动 | 地图仅为工程基线，无 B–G 场景装配 |
| M-03 | A/B Online | `CatOnlineSubsystem.cpp:187` `RequestCreateSession`、`:408` `RequestLeave`、`:874` `HandlePostLoadMap` | 单一 OSS/Travel 所有者、四事实、epoch、补偿和 teardown | 所有联机入口 | Null OSS 两轮与缺图负路径通过 | Steam/远端/ACK timeout 未实测 |
| M-04 | A UI | `CatLocalPlayerUISubsystem.cpp:123`、`CatTravelWidget.cpp:80` | 本地玩家 UI 创建/销毁和 Online 快照展示 | Frontend 诊断、Widget 生命周期 | Widget 5/5、EndPIE 清理 | 无可见 PIE 可读性证据 |
| M-05 | B/D 网关 | `CatGameplayTypes.cpp:119` `PreLogin`、`:310` `CanAcceptGameplayCommand` | 身份准入、统一 gameplay gate、RPC 桥 | 所有服务器命令 | 完整 code review + 构建 | 双账号/恶意 RPC 未运行 |
| M-06 | C 角色/GAS | `CatCharacter.cpp:190` `InitializeAbilityActorInfo`、`:207` `ApplyInitialAttributesOnce` | Character-owned ASC、属性、输入生命周期 | 角色能力/属性 | 构建 + code review | 正式复制策略/输入/数值未定 |
| M-07 | C/G 状态 | `Condition/CatConditionComponent.cpp:192` `ApplyRecovery` | 湿身/倒地/恢复的权威快照和幂等终态 | 救援、草药、进食、营地 | code review | 距离/多人复制未运行 |
| M-08 | D Run | `CatGameplayTypes.cpp:397` `EnterRunPhaseFromStateTree`、`:925` `RequestRunTeardown` | Run phase、命令 cache、公开复制和 teardown | 日循环/结算/Host 退出 | code review；缺配置 fail-closed | 缺 `ST_RunFlow`，正常路径未跑 |
| M-09 | D StateTree | `Run/CatRunStateTreeNodes.cpp:15` | Task/Condition 只调用 GameMode 合同 | 保证唯一流程拓扑 | UHT/构建 | 正式 StateTree 资产缺失 |
| M-10 | D Environment | `Environment/CatConfiguredEnvironmentProvider.cpp:7`、`CatWaterRegion.cpp:42` | 配置环境快照、水域聚合与查询 | 天气、鱼表筛选、自然贡献 | Lake fail-closed 日志 | 无正式水域/天气 Actor |
| M-11 | E Fishing | `Fishing/CatFishingService.cpp:36`、`CatFishingSession.cpp:314` | 单活动会话、能力快照、HookedFight 协作、首抄 capture | 捕鱼主链 | code review | 正式 StateTree/鱼资产缺失 |
| M-12 | E Items | `Items/CatItemsService.cpp:92` `CommitCapture`、`:334` `ReserveFish` | 容器所有权/revision、唯一鱼、转移/偷取/献祭 reservation | 物品一致性 | code review | 容量为 0，运行未测 |
| M-13 | E Replication | `Items/CatContainerReplicationComponent.cpp:25` | Authority snapshot→FastArray→OnRep snapshot | 鱼袋/共享缸客户端视图 | 构建 + code review | 无真实多客户端 delta 证据 |
| M-14 | E Sacrifice | `Run/CatSacrificeCoordinator.cpp:26` | Reserve→Run contribution→Commit/Cancel 补偿 | 跨 Items/Run 事务 | code review | 部分失败未运行注入 |
| M-15 | F Imprint | `Collection/CatRunImprintService.cpp:141`、`:262` | 全参与者两阶段 CapturePlan、成像 terminal、Grant 投递/ACK | 本轮印记和退出交付 | F15 closure review | 外部成像桥未接 |
| M-16 | F Profile | `Profile/CatProfileSubsystem.cpp:70` `ApplyGrant`、`:335` `SaveCurrentProfile` | durable journal、幂等 Grant、收藏/装备选择持久化 | 跨局数据 | code review | persistence 默认关闭，恢复未跑 |
| M-17 | G Equipment | `Equipment/CatEquipmentComponent.cpp:30`、`:248` | 解锁证明、局内耐久/消耗品和营地维修 | 角色装备状态 | code review | definitions/trust policy 未装配 |
| M-18 | G Camp | `Camp/CatCampHubActor.cpp:43`、`:132` | 救援、休息、共享缸、营火全员计划 | 营地协作 | F15 closure review | Lake 无 Camp Actor，范围为 0 |
| M-19 | G Social | `Social/CatSocialService.cpp:61`、`:245` | TheftProtocolId、权威距离、恶作剧/求助/保护牌和 teardown | 玩家交互协议 | F01–F15 closure review | Social gate/范围/冷却未配置 |
| M-20 | 验证脚本 | `Scripts/verify_stage_a_travel.py:26`、`:126`；failure `:28`、`:77` | UE GUID 规范化、两轮状态机、负路径、EndPIE 顺序 | 可重复阶段 A 证据 | 两脚本最终 PASS | 仅 Null OSS，无 B–G harness |

## 6. 主要文件单卡

### 6.1 `Source/Catfishing/Catfishing.Build.cs:4` — `Catfishing`

- **模块/层：** 构建合同 / 单 Runtime 模块。
- **改动类型：** 模块依赖与 include 根接线。
- **改了什么：** 在 `:10` 登记唯一模块根，公开 Core/Engine/DeveloperSettings/UMG/GAS/Tags/NetCore/OSS/StateTree，私有 EnhancedInput/Slate/OSSUtils 等实现依赖。
- **为什么：** 公共 UHT 头直接暴露这些引擎类型，并使用 `Items/...` 等模块根相对路径。
- **怎么做：** 不拆新模块，不引入 Editor 依赖；public/private 依赖按头文件暴露面区分。
- **输入：** 所有公开头的类型引用与 Runtime cpp include。
- **输出：** UBT/UHT 可解析的模块图。
- **状态读取：** `Target` 与模块规则。
- **状态写入：** include path 和依赖数组。
- **调用者：** UBT。
- **消费者：** Editor/Game Target 和全部源码。
- **风险：** 根 include 提高跨目录可见性；新增公共头时仍应直接 include，不能依赖偶然传递。
- **建议先看：** `:7-20`，再对照任一公共头的模块类型。

### 6.2 `Source/Catfishing/Online/CatOnlineSubsystem.cpp:29` — `UCatOnlineSubsystem`

- **模块/层：** GameInstance 生命周期 / Online 与 Travel。
- **改动类型：** 新增核心子系统。
- **改了什么：** 唯一持有 OSS delegates、operation epoch、RequestId、四事实快照、旅行失败/网络失败补偿和 Host teardown。
- **为什么：** 避免 Session manager、Travel manager 和 UI 分别写平台事实，尤其要处理同步 Null OSS 回调和迟到回调。
- **怎么做：** 六个公开入口先执行 pending gate；`BeginOperation` 建 epoch；回调按 epoch 过滤；Host leave 先 `RequestRunTeardown`，再 DestroySession；`HandlePostLoadMap` 按地图目标收敛 World/Transport。
- **输入：** `UCatOnlineSettings`、OSS session interface、地图加载/失败 delegates、Run teardown 结果。
- **输出：** `FCatOnlineResult` 与 `FCatOnlineSnapshot` 广播、Server/Client travel。
- **状态读取：** 当前 snapshot、active request/epoch、search/invite handles、session settings。
- **状态写入：** Session/World/Role/Transport/Operation、last error、delegate handles、pending failure-after-destroy。
- **调用者：** `UCatLocalPlayerUISubsystem`、Travel Widget、GameMode host-exit 路径、Python verification。
- **消费者：** 本地 UI、自动化脚本、所有旅行相关玩法。
- **风险：** Steam 异步/迟到回调、DestroySession 失败、远端 Grant ACK timeout 仍缺平台运行证据。
- **建议先看：** `:111` `BeginOperation`，`:187` Create，`:408` Leave，`:449` teardown，`:664-822` callbacks，`:874` map load，`:935` travel failure。

### 6.3 `Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp:20` — `UCatLocalPlayerUISubsystem`

- **模块/层：** LocalPlayer / UI 生命周期。
- **改动类型：** 新增 UI 所有者。
- **改了什么：** 订阅 Online/Run 快照，为当前 Controller 创建原生 Travel/Survival Widget，并在 map/player/deinitialize 时成对移除。
- **为什么：** Widget 不应持有 OSS delegate，也不应跨旅行残留旧 World 对象。
- **怎么做：** `CreateWidgetForCurrentController` 在 `:123` 去重创建；`:147` 开始销毁；Online 命令由 subsystem 转发。
- **输入：** LocalPlayer/Controller、Online snapshot、UI settings、GameState。
- **输出：** 一个 Travel Widget 和按 gate 可选的 Survival Widget。
- **状态读取：** 当前 World、Controller、Widget weak references、配置 gate。
- **状态写入：** delegate handles、Widget 引用和 viewport 生命周期。
- **调用者：** LocalPlayer subsystem 生命周期、地图加载回调。
- **消费者：** 本地玩家。
- **风险：** split-screen/多 LocalPlayer 未验证；可见 UI 焦点和可读性未人工检查。
- **建议先看：** `:20-65` delegate 生命周期，`:123-153` Travel Widget，`:195-282` Survival Widget。

### 6.4 `Source/Catfishing/UI/CatTravelWidget.cpp:11` — `UCatTravelWidget`

- **模块/层：** UMG 原生诊断 UI。
- **改动类型：** 新增无蓝图依赖 Widget。
- **改了什么：** 构建五个 Online 按钮与状态文本，`Configure` 根据四事实/operation/search/invite 状态刷新可用性。
- **为什么：** 阶段 A 在正式 UI 资产缺失时仍需要可观察、可点击的工程入口。
- **怎么做：** `NativeOnInitialized` 在 `:11-48` 构建 WidgetTree 与五个按钮；`NativeConstruct` 在 `:51-64` 对每个点击 delegate 执行 Remove/Add 配对；`NativeDestruct` 在 `:67-76` 解绑五个 delegate、清空临时 opaque handle，再释放 Slate 生命周期。按钮只广播意图，业务逻辑留在 LocalPlayer/Online subsystem。
- **输入：** `FCatOnlineSnapshot` 与按钮点击。
- **输出：** Host/Find/Join/Invite/Leave 事件和可见状态。
- **状态读取：** snapshot 的四事实、结果数、邀请数、错误。
- **状态写入：** 文本与按钮 enabled 状态；不写 Online 状态。
- **调用者：** `UCatLocalPlayerUISubsystem`。
- **消费者：** 本地玩家和阶段 A 自动化的 Widget 数检查。
- **风险：** 工程诊断 UI 不等于最终产品 UI；未做本地化/手柄焦点验收。
- **建议先看：** `:11-48` 初始化树，`:51-76` 绑定/解绑，`:80` `Configure`。

### 6.5 `Source/Catfishing/Framework/Game/CatGameplayTypes.cpp:43` — GameMode/GameState/PlayerState/PlayerController

- **模块/层：** Gameplay Framework 权威总线。
- **改动类型：** 扩展核心 gameplay 类和 RPC 桥。
- **改了什么：** Frontend/Lake startup、PreLogin/PostLogin、统一 gameplay gate、Run phase/命令/teardown、公开复制状态，以及 Fishing/Camp/Equipment/Social 等 Server RPC。
- **为什么：** 需要一个稳定的网络边界把客户端意图变成带身份、RequestId 和 revision 的服务器命令。
- **怎么做：** GameMode 保存权威 Run state/cache；GameState/PlayerState 只复制公开 DTO；PlayerController 先过 `CanForwardGameplayCommand` 再调用域服务；owning Client RPC 承接 Grant/CapturePlan/TheftResult。
- **输入：** Online 准入、StateTree 事件、各 Server RPC、环境/域服务结果。
- **输出：** `FCatRunPublicState`、命令终态、Grant/Plan/社交结果与 host-exit ACK。
- **状态读取：** active controller、StableNetId、Run phase/revision、ready eligibility、settings gates。
- **状态写入：** Run public state、command cache、ready/collection replicated state、teardown timers/ACK 集合。
- **调用者：** 引擎 Gameplay Framework、PlayerController RPC、Run StateTree、Online subsystem。
- **消费者：** 所有域服务、客户端 UI与 Profile。
- **风险：** 文件很大但承担真实框架聚合边界；未来拆分前必须保持唯一 Run/身份/teardown 所有权，不能按函数长度机械拆 manager。
- **建议先看：** `:119-335` 准入/gate，`:397-507` phase，`:925-1086` teardown，`:1267-1836` RPC 边界。

### 6.6 `Source/Catfishing/Character/CatCharacter.cpp:23` — `ACatCharacter`

- **模块/层：** Pawn / 角色聚合根。
- **改动类型：** 新增 Character-owned GAS 与组件生命周期。
- **改了什么：** 拥有 ASC、SurvivalAttributeSet、Equipment、Condition 和个人鱼袋复制组件；在 Possess/OnRep/PawnClientRestart 初始化 actor info 与输入。
- **为什么：** 角色状态需要服务器权威且能在占有变化、重生和客户端重启时正确配对。
- **怎么做：** `InitializeAbilityActorInfo` 幂等设置 owner/avatar；初始属性和测试能力各有一次性 gate；输入 MappingContext 由拥有客户端添加/移除。
- **输入：** Controller/PlayerState、Ability settings、Enhanced Input local player。
- **输出：** 可复制角色能力/属性/装备/状态和个人容器注册。
- **状态读取：** authority/local control、ASC actor info、配置与 grant handles。
- **状态写入：** actor info、初始 attributes、ability handle、mapping context、个人容器注册状态。
- **调用者：** UE Pawn 生命周期。
- **消费者：** Fishing、Camp、Social、UI。
- **风险：** 正式输入资产与 GAS replication policy 未决；重生/断线重连未实测。
- **建议先看：** `:58-135` 生命周期，`:155-190` 容器/会话清理，`:190-278` GAS/输入。

### 6.7 `Source/Catfishing/Run/CatRunStateTreeNodes.cpp:15` — Run Task/Condition

- **模块/层：** StateTree 适配层。
- **改动类型：** 新增薄节点。
- **改了什么：** Task 调 `EnterRunPhaseFromStateTree`，Condition 只判断最后 transition reason。
- **为什么：** 正式 StateTree 保持唯一拓扑，C++ 只提供可验证原语，不再暗藏第二套 FSM。
- **怎么做：** 从 context actor 解析 GameMode，返回 Running/Succeeded/Failed，不在节点内写额外状态。
- **输入：** phase、reason、StateTree execution context。
- **输出：** Run transition result / condition boolean。
- **状态读取：** GameMode 最后结果。
- **状态写入：** 仅通过 GameMode 合同写 Run state。
- **调用者：** 正式 `ST_RunFlow`。
- **消费者：** GameMode、GameState 复制状态。
- **风险：** 当前没有 `ST_RunFlow`，只能审查节点合同，不能审查资产拓扑。
- **建议先看：** `:15-43` Task，再看 `:45` Condition。

### 6.8 `Source/Catfishing/Environment/CatConfiguredEnvironmentProvider.cpp:7` — `EvaluateEnvironment`

- **模块/层：** Environment Provider。
- **改动类型：** 新增配置驱动 Provider。
- **改了什么：** 按 Run phase/revision 生成环境快照，缺 gate/天气/配置时返回失败。
- **为什么：** Run 不应自己猜天气，Fishing 也不应从客户端环境推断鱼表。
- **怎么做：** 读取 `UCatEnvironmentSettings`，输出显式 result；没有 fallback 天气。
- **输入：** `FCatRunPhaseSnapshot`、Run revision、Environment settings。
- **输出：** `FCatEnvironmentResult`。
- **状态读取：** gate、天气/事件配置。
- **状态写入：** 无持久状态；构造结果 DTO。
- **调用者：** `ACatfishingGameModeBase::RefreshEnvironmentAndPublish`。
- **消费者：** Run public state、Water/Fishing 筛选。
- **风险：** 当前 gate 关闭且 Weather Unknown；正常输出没有 runtime 证据。
- **建议先看：** 文件全文，再看 `CatEnvironmentSettings.h:11`。

### 6.9 `Source/Catfishing/Environment/CatWaterRegion.cpp:42` — `ContributeAggregation`

- **模块/层：** 场景水域 Actor / 聚合入口。
- **改动类型：** 新增权威空间域。
- **改了什么：** 水域配置/范围判断、自然或玩家贡献、revision 和环境快照。
- **为什么：** 鱼类选择和聚合必须由明确场景 Region 提供，不能以客户端坐标或全局默认水域替代。
- **怎么做：** 校验 Region/runtime config、StableNetId、RequestId 和 ExpectedRevision 后提交；terminal cache 阻止重复贡献。
- **输入：** 世界位置、`FCatAggregationCommand`、Water/Environment settings。
- **输出：** `FCatAggregationResult` 与 Region snapshot。
- **状态读取：** region bounds、当前 aggregation/revision、commands gate。
- **状态写入：** aggregation、revision、terminal cache。
- **调用者：** GameMode 自然聚合和 PlayerController chum RPC。
- **消费者：** Environment/Fishing。
- **风险：** Lake 没有正式 WaterRegion Actor，距离/鱼表行为未运行。
- **建议先看：** `:11` 配置判断、`:19` 空间判断、`:42` 聚合。

### 6.10 `Source/Catfishing/Fishing/CatFishingService.cpp:36` — `StartFishingSession`

- **模块/层：** WorldSubsystem / Fishing 会话目录。
- **改动类型：** 新增会话入口与单活跃所有权。
- **改了什么：** 对每个钓手缓存 start 终态，阻止并发会话，生成 Session Actor，转发 assist/scoop，并在终态/角色离开/teardown 释放。
- **为什么：** Session Actor 管单场流程，Service 管跨 Session 唯一性和生命周期，两者职责不同。
- **怎么做：** 用 StableNetId+RequestId 缓存；`ActiveSessionByFisher` 绑定钓手；`CompactSessions` 清无效/终态引用；能力快照从权威 Character 读取。
- **输入：** Controller、RequestId、Fish catalog、Water query、Fishing settings。
- **输出：** `FCatFishingStartResult`、SessionId、session actor。
- **状态读取：** active session map、start cache、commands gate、角色能力。
- **状态写入：** active map、session registry、terminal cache。
- **调用者：** PlayerController Fishing RPC、Run teardown、Character EndPlay。
- **消费者：** `ACatFishingSession`、Items/Imprint/Social。
- **风险：** 正式 StateTree/鱼表/水域缺失；真实多人 simultaneous start 未运行。
- **建议先看：** `:36-151` start，`:167` scoop，`:184-231` teardown/compact。

### 6.11 `Source/Catfishing/Fishing/CatFishingSession.cpp:314` — `RequestScoop`

- **模块/层：** Replicated Actor / 单次 Fishing 聚合。
- **改动类型：** 新增捕获 compare-and-commit 主链。
- **改了什么：** 验证 NearShore、revision、scooper 能力与权威距离；提交唯一 Fish；为普通/巨鱼整理参与者并触发可选印记。
- **为什么：** NearShore 的并发竞争、鱼实物和 CapturePlan 必须有确定先后；旧“双人抄网”不合法。
- **怎么做：** terminal cache 先重放；`bCaptureResolved` 和 Items 的 `CaptureByFishingSession` 双重唯一；Items commit 成功后记录 FishRecorded，再可选创建全员 CapturePlan，最后发布 Resolved 并延迟销毁。
- **输入：** scoop command、Fishing snapshot、Character 位置/能力、Guard revision、FishDefinition、Water snapshot。
- **输出：** `FCatScoopResult`、唯一 fish instance、FishRecorded grant、可选 CapturePlan。
- **状态读取：** phase/revision、near-shore target、fight participants、settings reach、Items/Imprint readiness。
- **状态写入：** scoop terminal cache、capture-resolved、session snapshot/revision、terminal destroy timer。
- **调用者：** `UCatFishingService::RequestScoop`。
- **消费者：** Items、Imprint、GameState/客户端。
- **风险：** Items commit 后若 archive/plan 失败只记录 error 日志，实物不会回滚；这是“实物必需、印记可选”的既定边界，程序员应确认符合产品预期。
- **建议先看：** `:314-475`，尤其 `:337` 唯一性、`:386-445` 巨鱼参与者/计划、`:456-464` 终态。

### 6.12 `Source/Catfishing/Items/CatItemsService.cpp:92` — `CommitCapture`

- **模块/层：** WorldSubsystem / Items 权威聚合。
- **改动类型：** 新增容器与鱼事务服务。
- **改了什么：** 注册容器、唯一 capture、原子转移、消费/偷取、献祭 reservation/commit/cancel、teardown 收口。
- **为什么：** 鱼实例不能由 Fishing、Camp、Social 各自直接改数组；所有权、容量和 revision 必须集中校验。
- **怎么做：** terminal key 由 StableNetId+操作+RequestId 构成；`CaptureByFishingSession` 保证一场只生成一鱼；转移先同时校验再一起改两个 snapshot；献祭先锁鱼再提交或取消。
- **输入：** 各域 command DTO、容器组件、authority actor、settings capacity。
- **输出：** command terminal、容器 snapshot/revision、reservation 结果。
- **状态读取：** Containers、owner/kind/capacity/revision、terminal caches、reservations。
- **状态写入：** fish arrays、revisions、capture map、reservation/terminal cache，并发布复制组件。
- **调用者：** FishingSession、SacrificeCoordinator、Camp、Condition、Social。
- **消费者：** FastArray 组件、各玩法域、客户端容器视图。
- **风险：** 高扇出文件；任何新调用者都必须走已有两阶段/terminal 合同，禁止直接改 snapshot。
- **建议先看：** `:31-91` 注册，`:92-178` capture，`:179-333` transfer，`:334-486` sacrifice，`:487-614` theft/consume，`:615` teardown。

### 6.13 `Source/Catfishing/Items/CatContainerReplicationComponent.cpp:25` — `SetSnapshotFromAuthority`

- **模块/层：** ActorComponent / 容器复制。
- **改动类型：** 新增 FastArray 适配层。
- **改了什么：** 服务器 snapshot 转 FastArray entries 与 metadata，客户端 replication callback 重建只读 snapshot。
- **为什么：** 容器鱼列表需要 delta replication，但业务域仍应使用明确 snapshot/revision。
- **怎么做：** authority-only 写入，标记 array/items dirty；metadata OnRep 与 PostReplicatedReceive 统一重建。
- **输入：** `FCatContainerSnapshot`。
- **输出：** replicated fish list、客户端 snapshot change delegate。
- **状态读取：** 旧 entries 与 metadata。
- **状态写入：** FastArray、ContainerId/kind/revision、rebuilt snapshot。
- **调用者：** `UCatItemsService::PublishContainer`。
- **消费者：** Character 个人鱼袋、FishTank/客户端 UI。
- **风险：** 尚无真实两客户端增删/重排 delta 运行证据。
- **建议先看：** `:25-82` authority 写，`:83-115` client rebuild。

### 6.14 `Source/Catfishing/Run/CatSacrificeCoordinator.cpp:26` — `RequestSacrifice`

- **模块/层：** WorldSubsystem / 跨 Items–Run 协调器。
- **改动类型：** 新增两阶段事务协调。
- **改了什么：** 对献祭命令做 terminal replay，先 Reserve Fish，再提交 Run contribution，随后 Commit reservation；失败时 Cancel。
- **为什么：** 单次调用跨两个聚合，不能出现“鱼没了但配额没加”或反向重复。
- **怎么做：** protocol key 固定 StableNetId+RequestId；记录 in-flight protocol 和每一步结果；teardown 关闭入口并取消未完成 reservation。
- **输入：** Controller、`FCatSacrificeCommand`、Items 与 GameMode。
- **输出：** `FCatSacrificeResult` 和稳定 terminal cache。
- **状态读取：** commands gate、terminal/in-flight map、Run revision、reservation。
- **状态写入：** protocol state、terminal cache、Items reservation、Run contribution。
- **调用者：** PlayerController Sacrifice RPC、Run teardown。
- **消费者：** Run quota、Items 容器、客户端结果。
- **风险：** 没有运行时部分失败注入；Review 应重点核对 Commit/Cancel 顺序和重入。
- **建议先看：** `:26-96` 主流程，`:97-140` teardown。

### 6.15 `Source/Catfishing/Collection/CatRunImprintService.cpp:141` — `CreateCapturePlansForParticipants`

- **模块/层：** WorldSubsystem / 本轮印记与可靠交付。
- **改动类型：** 新增两阶段 batch 与 durable delivery 状态机。
- **改了什么：** 候选、全员 CapturePlan、成像 terminal、Grant delivery/ACK、teardown readiness 和 settlement archive readiness。
- **为什么：** Campfire/巨鱼等多参与者事件不能边创建边 RPC，后半失败会留下部分玩家计划。
- **怎么做：** 第一阶段完整校验/去重并在临时数组构造所有 record；第二阶段一次写入 maps；确认全量可回读后冻结 `OutPlans`，再逐 ID 投递，避免同步 Client RPC 重入使 TMap 引用失效。
- **输入：** CandidateId、recipient StableNetIds、campfire flag、Controller result/ACK。
- **输出：** 全批 `FCatCapturePlan`、durable `FCatProfileGrant`、readiness 状态。
- **状态读取：** Candidates、plan/delivery maps、AlbumByRun、commands gate、active controllers。
- **状态写入：** capture deliveries、grant deliveries、terminal phase/ACK、archive 状态。
- **调用者：** FishingSession、Campfire、Social caught theft、PlayerController RPC、Run teardown。
- **消费者：** owning Client Profile bridge、Host exit/settlement。
- **风险：** 外部桥未运行；离线重投与 ACK timeout 仅代码审查。
- **建议先看：** `:141-258` batch，`:262-352` terminal/ACK，`:353-456` reconnect/teardown/archive，`:498-530` delivery。

### 6.16 `Source/Catfishing/Profile/CatProfileSubsystem.cpp:70` — `ApplyGrant`

- **模块/层：** LocalPlayerSubsystem / 本地 durable Profile。
- **改动类型：** 新增 SaveGame journal 与本地集合。
- **改了什么：** 加载/迁移 Profile、应用 durable Grant、接 CapturePlan 外部桥、保存装备选择/收藏与公开投影。
- **为什么：** owning client 的跨局数据不能直接由 replicated PlayerState 当持久真相；ACK 只应在本地 durable 后允许。
- **怎么做：** 校验 Grant；已 applied 直接允许 ACK；新 Grant 先写 `GrantJournal(Pending)` 并 Save，再 `CompletePendingGrant` 合并/保存；失败保留或回滚到可重试状态。
- **输入：** owning Client RPC 的 Grant/CapturePlan、Profile settings、SaveSlot、Fish/Equipment definitions。
- **输出：** `FCatProfileApplyResult`、ACK 资格、收藏/选择快照与 capture bridge delegate。
- **状态读取：** CurrentProfile、schema version、AppliedGrantIds、GrantJournal、catalog/unlock。
- **状态写入：** SaveGame journal、收藏/装备选择、applied IDs、persistence-ready。
- **调用者：** `ACatfishingPlayerController::ClientReceiveProfileGrant` 与 UI/本地系统。
- **消费者：** Server ACK、PlayerState 公开收藏、装备装配、外部成像桥。
- **风险：** persistence 默认关闭；真实落盘、损坏恢复、崩溃窗口和平台文件路径未运行。
- **建议先看：** `:12-69` 初始化/重载，`:70-109` Grant，`:123-236` 本地命令，`:237-367` merge/save/reload。

### 6.17 `Source/Catfishing/Equipment/CatEquipmentComponent.cpp:30` — `ConfigureLoadoutFromAuthority`

- **模块/层：** Character ActorComponent / 装备聚合。
- **改动类型：** 新增复制装备与消耗品状态。
- **改了什么：** 配装、局内 consumable grant/consume、耐久和 Camp repair，全部带 request/revision 与 unlock 证明。
- **为什么：** Profile 只保存跨局选择，局内数量/耐久必须由角色权威组件持有。
- **怎么做：** 对 definition、slot、unlock、revision、Camp 条件 fail-closed；terminal cache 重放重复命令；发布 replicated snapshot。
- **输入：** Profile 选择、EquipmentDefinition、PlayerState unlock、Camp/command context。
- **输出：** equipment snapshot 与 command terminal。
- **状态读取：** current loadout/revision、run inventory/durability、settings trust policy。
- **状态写入：** replicated snapshot、terminal caches。
- **调用者：** PlayerController、Camp repair、Run/角色初始化。
- **消费者：** Character、Fishing capability、UI。
- **风险：** definitions 为空、unlock trust policy 未决；团队可达/多人装配未运行。
- **建议先看：** `:30-96` loadout，`:97-247` consumable/durability，`:248-323` repair/publish。

### 6.18 `Source/Catfishing/Condition/CatConditionComponent.cpp:192` — `ApplyRecovery`

- **模块/层：** Character ActorComponent / 生存状态。
- **改动类型：** 新增湿身、倒地和恢复状态机。
- **改了什么：** 自救、营地休息、草药、搬运救援、进食后的权威恢复与公开 snapshot。
- **为什么：** 状态变化需要统一 revision/幂等处理，Social/Fishing 也要查询“角色是否仍可参与”。
- **怎么做：** public entry 校验 Controller/距离/消耗已提交等前置，再进入 `ApplyRecovery`；`EvaluateDownedFromAttributes` 从服务器属性导出 downed 状态。
- **输入：** RequestId、RecoveryMode、Character/Controller、已提交的 Items/Equipment 结果、属性。
- **输出：** `FCatDomainCommandResult` 与 replicated condition snapshot。
- **状态读取：** wet/downed/revision、attribute values、terminal cache。
- **状态写入：** condition snapshot/revision 与 terminal cache。
- **调用者：** PlayerController、Camp、Items consume、Equipment herb。
- **消费者：** Social/Fishing eligibility、Survival UI。
- **风险：** 恢复公式和正式阈值未配置；救援/草药距离未多人运行验证。
- **建议先看：** `:70-185` 各入口，`:192-260` 核心转换。

### 6.19 `Source/Catfishing/Camp/CatCampHubActor.cpp:132` — `RequestCampfirePlayback`

- **模块/层：** 场景 Actor / 营地聚合入口。
- **改动类型：** 新增营地协作命令。
- **改了什么：** Rest、救援、共享缸转移和 settlement campfire；campfire 在广播前给所有在场玩家建立计划。
- **为什么：** 营地范围是权威场景事实，多人回放不能出现部分计划成功。
- **怎么做：** `ResolveCharacterInCamp` 统一范围 gate；campfire 收集 GameState 全部玩家，任一不在 Camp 则失败；调用 Imprint 两阶段 batch 成功后才广播。
- **输入：** Controller/Character、RequestId、GameState players、Camp settings、Items/Imprint。
- **输出：** terminal result、状态恢复/鱼转移、Campfire delegate。
- **状态读取：** Camp range、settlement phase、player presence、terminal cache。
- **状态写入：** campfire terminal cache；其他副作用委托对应聚合完成。
- **调用者：** PlayerController Camp RPC。
- **消费者：** Condition、Items、Imprint、场景表现层。
- **风险：** 当前 Lake 无 CampHub/SharedTank 正式装配；“所有 active players”与掉线准入需产品场景实测。
- **建议先看：** `:28-131` rest/rescue/tank，`:132-204` campfire，`:208` 范围判断。

### 6.20 `Source/Catfishing/Social/CatSocialService.cpp:61` — `BeginTheft`

- **模块/层：** WorldSubsystem / Social 协议。
- **改动类型：** 新增偷鱼、恶作剧、求助、保护牌与 teardown。
- **改了什么：** TheftProtocolId 端到端返回到 Client RPC，服务器验证角色/容器/authority actor 距离；Catch/timeout 只终结一次；其他 social 命令复用权威 eligibility。
- **为什么：** 社交玩法涉及对他人容器和角色的副作用，不能相信客户端目标、距离或协议 ID。
- **怎么做：** BeginTheft 先解析 StableNetId 和 source authority context，再生成协议并交给 Items 锁鱼；active map/timer 管窗口；Client 得到同一 ProtocolId 后才能 Catch；teardown 关闭并解析所有活动协议。
- **输入：** Controller、Theft/Mischief/Help/Sign command、Items container context、Social settings。
- **输出：** `FCatTheftResult`、help snapshot、imprint candidate、terminal results。
- **状态读取：** character social activity、距离、source owner/kind、active theft、cooldown/protection。
- **状态写入：** active theft maps/timers、terminal caches、help/protection state、Items theft protocol。
- **调用者：** PlayerController Social RPC、Fishing giant prompt、Run teardown、Character EndPlay。
- **消费者：** Items、Imprint、GameState/clients。
- **风险：** 正式权限/范围/冷却均未配置；网络延迟下 Catch/timeout 竞态未实跑。
- **建议先看：** `:61-244` theft，`:245-436` mischief/help/sign，`:437-577` prompt/cancel/timeout/caught imprint，`:610` eligibility。

### 6.21 `Scripts/verify_stage_a_travel.py:26` 与 `verify_stage_a_travel_failure.py:28`

- **模块/层：** Harness / UE Python runtime verifier。
- **改动类型：** 新增可重复阶段 A 正负路径脚本。
- **改了什么：** `FGuid.to_string()` 规范化、Slate pre-tick 有界状态机、四事实/epoch/pending/Widget 断言、EndPIE 后清理；负脚本验证缺 Lake 补偿和新 RequestId。
- **为什么：** Travel 会重建 World/UObject，阻塞式脚本和持有旧 subsystem 引用会产生假失败或挂起。
- **怎么做：** context 只保存标量/回调句柄，每 tick 重枚举唯一非 CDO Online；先处理 timeout/EndPIE，再访问 UObject；所有 terminal 只报告一次。
- **输入：** UE Python API、Online snapshot、PIE 生命周期。
- **输出：** `STAGE_A_TRAVEL_PASS/FAIL`、`STAGE_A_TRAVEL_FAILURE_PASS/FAIL` 和自动退出。
- **状态读取：** stage/deadline/round/epoch、snapshot、Widget count。
- **状态写入：** 脚本 context、PIE start/end、Online public commands。
- **调用者：** `UnrealEditor-Cmd -ExecCmds=py ...`。
- **消费者：** verification 与 testing report。
- **风险：** 仅覆盖 Null OSS 安全副本；同步完成不代表 Steam 异步行为。
- **建议先看：** 正常脚本 `:26`、`:126-208`；负脚本 `:28`、`:77-154`。

## 7. 代码工作流

### 7.1 Host Create → Lake → Leave → Frontend

**对应改动：** M-03、M-04、M-08；**对应文件卡：** 6.2、6.3、6.4、6.5。

```text
TravelWidget 点击
  → LocalPlayerUISubsystem 转发意图
  → UCatOnlineSubsystem::RequestCreateSession
      → pending gate
      → BeginOperation(Create)：生成 RequestId + operation epoch
      → OSS CreateSession
      → HandleCreateSessionComplete(epoch 校验)
      → BeginHostTravelToLake
      → HandlePostLoadMap(Lake)：Host / Lake / Connected / None
  → RequestLeave
      → BeginHostRunTeardown
      → GameMode::RequestRunTeardown
      → 各域关闭命令、补偿事务、交付 Grant、等待 ACK/timeout
      → BeginDestroySession
      → HandleDestroySessionComplete(epoch 校验)
      → BeginTravelToFrontend
      → HandlePostLoadMap(Frontend)：NoSession / Frontend / Idle / None
```

关键审查点：六个 public entry 必须先于业务状态校验执行 pending gate；每个 OSS callback 都带捕获 epoch；Destroy 失败不能跳过明确终态；Frontend map load 必须把 Transport 重置为 Idle。最终 Null OSS 两轮回归已覆盖同步回调重入和这四点，Steam 异步尚未覆盖。

### 7.2 Client Find / Join / Invite

**对应改动：** M-03、M-04、M-05；**对应文件卡：** 6.2、6.3、6.4、6.5。

```text
UI Find
  → RequestFindSessions
  → OSS callback 将真实 SearchResult 留在 subsystem，仅向 UI 暴露 opaque handle + summary
UI Join/Invite
  → handle 查回受管 SearchResult
  → RequestJoinInternal + epoch
  → ResolveConnectString
  → ClientTravel Lake
  → map load 收敛 Client / Lake / Connected
```

句柄隔离防止蓝图/客户端长期持有平台对象；Session settings 兼容性由 `HasCompatibleSessionSettings` 检查。此流程只有编译和完整代码审查，没有 Steam 运行证据。

### 7.3 Run phase 与命令

**对应改动：** M-05、M-08、M-09、M-10；**对应文件卡：** 6.5、6.7、6.8、6.9。

```text
正式 ST_RunFlow
  → FCatRunEnterPhaseTask
  → GameMode::EnterRunPhaseFromStateTree
      → 检查 authority、RunId、StateTree 运行状态、目标 Phase 配置
      → 写 RunPublicState.Phase + Revision
      → GameState authority publish / replicate

客户端 Run 命令
  → PlayerController Server RPC
  → CanForwardGameplayCommand / CanAcceptGameplayCommand
  → FillServerCommandIdentity(StableNetId)
  → RequestId terminal replay + ExpectedRevision
  → 修改 quota/ready/settlement
  → 发布新 revision
```

Run StateTree 拥有合法前驱与 transition reason 的流程拓扑，GameMode 负责 authority、RunId、StateTree 运行状态、目标 Phase 配置和公开状态写入；当前缺正式 `ST_RunFlow`，不得用 C++ fallback 补一个平行 FSM。

### 7.4 Fishing → Items → Imprint

**对应改动：** M-11、M-12、M-13、M-15；**对应文件卡：** 6.10、6.11、6.12、6.13、6.15。

```text
ServerStartFishingSession
  → FishingService：StableNetId + RequestId cache / 单 active session
  → ACatFishingSession 初始化正式 StateTree、鱼定义、水域与能力快照
  → HookedFight：允许合法参与者 assist；巨鱼能力使用会话快照
  → NearShore：任何合法 scooper 都可竞争
  → RequestScoop：phase + revision + authority distance + guard capacity
  → ItemsService::CommitCapture：FishingSessionId 唯一 compare-and-commit
  → 首个成功者唯一获得 fish instance
  → Imprint::RecordCommittedCapture（实物永久事实）
  → 若 FishDefinition 配置事件：建立全参与者候选和两阶段 CapturePlan
  → Session 发布 Resolved、停止 StateTree、有界延迟销毁
  → FishingService compact 并释放钓手 active slot
```

必须维持的产品规则：巨鱼多人仅在 HookedFight 协作；NearShore 没有“双人抄网”条件；参与者的 CapturePlan 不改变鱼的唯一 owner。

### 7.5 Sacrifice 跨聚合事务

**对应改动：** M-12、M-14；**对应文件卡：** 6.12、6.14。

```text
PlayerController ServerRequestSacrifice
  → SacrificeCoordinator terminal replay
  → ItemsService::ReserveFish（锁鱼与返回槽位）
  → GameMode 提交 quota contribution
      → 失败：Items CancelFishReservation
      → 成功：Items CommitFishReservation
  → 写单一 protocol terminal
```

Teardown 会关闭新命令并取消未完成 reservation。程序员应重点逆向检查每个 early return 是否落 terminal，以及 Run 已提交但 Items commit 失败时的既定补偿/告警边界。

### 7.6 CapturePlan → 成像 terminal → durable Grant → ACK

**对应改动：** M-15、M-16；**对应文件卡：** 6.15、6.16。

```text
域服务提交 ImprintCandidate
  → CreateCapturePlansForParticipants
      1. 校验所有 recipient / 去重 / 生成临时 records
      2. 一次提交 Album + plan maps
      3. 全量回读并冻结 OutPlans
      4. 按 PlanId 重取 record 后再发 Client RPC
  → owning client Profile::ReceiveCapturePlan
  → 外部桥上报成功或失败 terminal
  → server ReportCaptureResult
      → 先写 capture terminal
      → 成功则创建 Grant delivery record
      → ClientReceiveProfileGrant
  → Profile::ApplyGrant
      → GrantJournal(Pending) Save
      → merge collection / AppliedGrantIds Save
      → 返回 AckAllowed
  → ServerAcknowledgeProfileGrant
  → Imprint 标 ACK，teardown/archive readiness 前进
```

这里的同步 Client RPC 可能立刻回入服务器，所以服务不能跨 RPC 持有 `TMap` 元素引用；两阶段 batch 和 journal 都是实际正确性边界，不是为了抽象而抽象。

### 7.7 Campfire 全员 batch

**对应改动：** M-15、M-18；**对应文件卡：** 6.15、6.19。

```text
ServerRequestCampfirePlayback
  → CampHub ResolveCharacterInCamp(requester)
  → 若结算夜且配置 cover event：
      → 枚举 GameState PlayerArray
      → 每名 active player 必须解析为 Camp 内角色
      → SubmitImprintCandidate(all-present)
      → CreateCapturePlansForParticipants(完整名单, campfire=true)
  → 全批成功后才 Broadcast playback
  → 写 terminal cache
```

任何参与者缺失、离营、计划不可创建都会在广播前失败，避免“动画播了但只有部分玩家有计划”。

### 7.8 Theft protocol

**对应改动：** M-12、M-15、M-19；**对应文件卡：** 6.12、6.15、6.20。

```text
ServerBeginTheft(command)
  → Social 解析 Thief StableNetId/Character
  → Items 查询 source container snapshot + authority actor
  → 服务器计算角色到 authority actor 的距离
  → 生成 TheftProtocolId
  → Items BeginFishTheft 锁定/移出鱼
  → Social 保存 active protocol + timer
  → ClientReceiveTheftResult(同一 TheftProtocolId)
  → 受害者 ServerCatchTheft(TheftProtocolId) 或 timer timeout
  → 恰好一个终态，归还/转移鱼并可选生成 caught imprint
```

Client 只能回传服务器签发的 ProtocolId，不能指定终态鱼或伪造距离。Character EndPlay/Run teardown 都会解析活动协议。

## 8. 建议阅读顺序

### 第一遍：先看稳定边界

1. `Catfishing.Build.cs:7-20`：模块和 Runtime/Editor 边界。
2. `Framework/Core/CatDomainCommandTypes.h:52`、`:70`：统一 command/result。
3. `CatOnlineTypes.h:248`：Online 四事实快照。
4. `Framework/Core/CatRunContracts.h:218`：Run 公开复制状态。
5. `Items/CatItemTypes.h:21` 与 `Framework/Core/CatProfileContracts.h:54`：鱼实例和 durable grant。

### 第二遍：沿所有权主线

1. `CatOnlineSubsystem.cpp:29` → `CatGameplayTypes.cpp:119`：平台事实、准入、统一 gate。
2. `CatCharacter.cpp:58` → Equipment/Condition：角色聚合生命周期。
3. `CatGameplayTypes.cpp:397` → Run StateTree nodes → Environment Provider：局流程。
4. `CatFishingService.cpp:36` → `CatFishingSession.cpp:314` → `CatItemsService.cpp:92`：捕获主链。
5. `CatRunImprintService.cpp:141` → `CatProfileSubsystem.cpp:70`：计划、交付、持久化。
6. Camp/Social：跨玩家调用方。

### 第三遍：按风险逆向

1. 从 `RequestRunTeardown:925` 逆向看每个域的 close/compensation/readiness。
2. 从 `CommitCapture:92` 反查所有鱼生成入口，确认没有旁路。
3. 从 `CreateCapturePlansForParticipants:141` 反查全部调用者，确认没有退回单人逐个提交。
4. 从 PlayerController Server RPC 段 `:1267-1836` 检查每个命令是否统一经过 gameplay gate 和服务器身份重建。
5. 最后看两个 Python 脚本和最终日志，区分“已验证”与“只审查”。

## 9. 风险清单

| 严重度 | 风险 | 当前保护 | 剩余验证/决定 |
|---|---|---|---|
| 高 | B–G 正式资产/配置缺失，正常产品路径不可运行 | 所有入口 fail-closed，无 fallback 伪成功 | 产品/设计补齐 settings、两棵 StateTree、DataAsset、输入和场景 Actor 后全量 runtime |
| 高 | Steam 异步、双账号、邀请、重连、Host 退出未实测 | epoch、opaque handle、准入、teardown/ACK timeout 已代码审查；Null OSS 同步路径通过 | 双账号 Steam 两端日志，含中途加入/掉线/迟到回调 |
| 高 | 跨域事务在运行时部分失败 | terminal cache、reservation、batch、journal、teardown 补偿 | 注入 Items/Run/Profile/外部桥失败，检查无重复/半提交 |
| 高 | Profile durable 语义未落盘实测 | SaveGame schema、pending journal、AppliedGrantIds、ACK gate | 重启/崩溃/损坏/重复 Grant/容量与原子文件策略 |
| 中 | FastArray 和公开复制只有静态审查 | authority-only publisher、revision、PostReplicatedReceive | 两客户端增删/转移/重排/断线重连 delta 测试 |
| 中 | Social timer 与网络竞态 | ProtocolId、active map、terminal cache、teardown resolve | Catch 与 timeout 同帧、角色离开、共享缸恢复策略测试 |
| 中 | `CatGameplayTypes.cpp` 扇出高 | 它是 GameMode/GameState/PlayerState/PlayerController 的真实框架边界；没有新增 manager | 程序员重点检查 RPC→域服务路由，未来只有出现独立所有权时再拆 |
| 中 | 可见 UI/输入未验收 | 原生工程 UI 生命周期有 headless 证据；产品输入 gate 关闭 | 可见 PIE、手柄/键鼠焦点、重生/占有、多 LocalPlayer |
| 低 | Target 使用 `EngineIncludeOrderVersion.Unreal5_6` 兼容顺序 | UE 5.8 Editor/Game 当前构建通过 | 单独升级 include order 并全量重建，避免和玩法改动混做 |
| 低 | 大量新文件在空模板基线中尚未形成稳定 Git 历史 | 完整审查面和文件地图已记录；保护项排除归因 | 程序员提交前按真实归属分组 review/stage，勿吞并用户文档和 state |

## 10. 设计取舍

### 10.1 单 Runtime 模块而非按域拆模块

当前 103 个文件仍处于一个产品和同一 Runtime 生命周期内。拆成多模块会引入导出宏、循环依赖、Build.cs 和加载顺序成本，却没有独立发布/复用边界。保留目录级领域边界，并用 `Framework/Core` 公开最小 DTO，是当前更小的充分方案。

### 10.2 Online subsystem 而非 Session/Travel manager 链

Session、Travel、epoch、delegate 和四事实必须由同一对象配对。拆 manager 会增加同步回调重入和 teardown 交接点，因此保持 `UCatOnlineSubsystem` 为唯一平台所有者。

### 10.3 StateTree 拥有拓扑，C++ 只提供原语

Run/Fishing 的正式流程由产品资产决定。C++ fallback 会形成第二套真相，资产缺失时 fail-closed 更诚实。代价是当前 B–G 不能“先凑合玩”，但不会把假拓扑写进长期代码。

### 10.4 公开 snapshot 与 command/result，而非通用事件总线

复制、幂等和权限要求稳定 DTO；通用事件总线会隐藏调用者和副作用。当前直接调用路径较长，但可沿 RequestId/StableNetId/revision 追踪。

### 10.5 两阶段状态是必要复杂度

- Items reservation 隔离献祭跨聚合部分失败。
- CapturePlan batch 隔离多接收者部分成功和同步 RPC 重入。
- Profile journal 隔离本地落盘与 Server ACK。
- Online epoch 隔离迟到 callback。
- FastArray 隔离服务器 snapshot 与客户端 delta replication。

这些状态均对应已存在的失败窗口；不能在没有等价保证时内联删除。

### 10.6 实物捕获必需、印记可选

FishDefinition 没有 CaptureImprintEvent 时仍允许实物鱼进入容器；有事件时才预检/生成计划。Items commit 后归档失败只记录严重错误，不回滚实物。程序员需要确认这是产品真正想要的“实物优先”策略；若未来要求强原子，修复必须设计跨 Items/Profile 的 durable outbox，而不是简单删除鱼。

## 11. 影响范围

- **构建：** `.uproject` 插件、Runtime Build.cs 依赖、Editor/Game Target 都受影响。
- **启动与地图：** 默认 Frontend；Lake startup 依赖正式 Run/Environment assets。
- **网络：** PreLogin/PostLogin、PlayerController RPC、GameState/PlayerState/ActorComponent replication、OSS session/travel 全部进入新合同。
- **存档：** 新增本地 Profile SaveGame schema、Grant journal、收藏与装备选择；默认关闭，启用前需正式策略。
- **玩法：** Run、Fishing、Items、Sacrifice、Imprint、Equipment、Condition、Camp、Social 都有新的服务器权威入口。
- **客户端：** 本地 UI、Profile、CapturePlan bridge；不会把他人 durable Profile 复制给客户端。
- **资产制作：** 需要两棵 StateTree、Fish/Equipment DataAsset、Input assets、WaterRegion/Camp/Tank/Sign 场景装配。
- **测试：** 新增地图创建/核查脚本和阶段 A 正负旅行 verifier；目前没有 B–G functional harness。
- **安全：** 统一 gameplay gate、StableNetId 重建、ExpectedRevision、权威距离与 unlock 证明降低客户端伪造面；仍需恶意 RPC 运行测试。

## 12. 相关但未改的内容

- 没有新增第二个 StableId 体系；继续以 `PlayerState.UniqueId`/OSS 身份为稳定键。
- 没有引入 CommonUI、Dedicated Server、Lobby、Seamless Travel、Host Migration、云存档或 MMO 后端。
- 没有创建 Session manager、Travel manager、通用事件总线、万能 Item 基类或未来平台适配器。
- 没有为缺失产品资产创建占位 StateTree/DataAsset，也没有写入猜测的数值、键位、权限或图片格式。
- 没有把旧“双人抄网”接回 NearShore。
- `.codex/state`、`.gitignore`、`.slnx`、`Docs/Knowledge` 的既有用户状态未由本实现节点修改或归因；程序员提交时必须继续保护。
- `Content` 只有 Frontend/Lake 工程地图；没有声称 Lake 已完成营地、水域、鱼群或玩法关卡制作。

## 13. 必要复杂度与删减审查

### 已明确删减/未引入

- 未拆多 Runtime 模块。
- 未增加 Session/Travel manager 或 Online 包装层。
- 未增加第二套身份、通用命令总线或万能 Item。
- StateTree node 保持薄适配，不复制流程状态机。
- UI 直接绑定现有 subsystem delegate，不新增 ViewModel 框架。
- 各域直接调用明确 contract，不为未来平台/玩法预留空 adapter。

### 保留结构的具体理由

| 结构 | 若删除会丢失的保证 |
|---|---|
| Online `operation epoch` | 迟到/同步 callback 无法与当前 request 隔离 |
| 四事实 snapshot | Session、地图、角色、传输会再次混成单一模糊状态 |
| Run/Social/Fishing terminal cache | 重试和重复 RPC 会重复副作用 |
| Items `CaptureByFishingSession` | 不同 RequestId 可为同一会话生成第二条鱼 |
| Sacrifice reservation | 跨 Items/Run 部分失败无法补偿 |
| Imprint delivery maps + ACK | 离线/重连/Host exit 时 Grant 可丢失 |
| 两阶段 CapturePlan batch | 多参与者只拿到半批计划；同步 RPC 可使引用失效 |
| Profile GrantJournal | ACK 可能早于 durable merge，崩溃后丢奖励 |
| FastArray adapter | 每次复制整容器或让业务依赖复制内部结构 |
| 配置 gate | 默认 0/None 会被误当正式产品值 |

删减结论：未发现可以在保持同等正确性、清晰性和验证边界的前提下继续删除的新增 manager/type/state。最大的文件是 Gameplay Framework 聚合边界，不建议以“变短”为目的机械拆分；若后续出现独立生命周期或可测试边界，再做有证据的拆分。

## 14. 修复入口

| 问题类别 | 首个入口 | 继续追踪 | 修复后必须重跑 |
|---|---|---|---|
| Create/Join/Leave 状态错误、迟到回调 | `CatOnlineSubsystem.cpp:111` `BeginOperation` | `:664-822` callbacks、`:874` map load、`:935` failures | 两个阶段 A 脚本 + Editor/Game 构建 + Steam 双端相关场景 |
| Host teardown/Grant ACK 卡住 | `CatGameplayTypes.cpp:925` `RequestRunTeardown` | `:1026-1086` ACK/timeout、`CatOnlineSubsystem.cpp:449` | Host leave 正常/timeout/远端断开 |
| 准入或 RPC 越权 | `CatGameplayTypes.cpp:119` `PreLogin`、`:310` gate | PlayerController `:1267-1836`、各域 identity rebuild | 双账号 + 恶意参数 + voluntary leave/reconnect |
| Run phase/结算错误 | `CatGameplayTypes.cpp:397` | Run settings/events/nodes、`:729-910` ready/environment/startup | 正式 `ST_RunFlow` 全拓扑和 illegal transition |
| 环境/水域错误 | `CatConfiguredEnvironmentProvider.cpp:7` | `CatWaterRegion.cpp:11/19/42`、WaterQuery | 正式天气、水域边界与多客户端 snapshot |
| 同角色多 Fishing/终态后锁死 | `CatFishingService.cpp:36` | `:184-231` terminate/compact、Session `:479-586` | start retry、角色离开、session lifespan |
| 重复鱼或 scoop 归属错误 | `CatFishingSession.cpp:314` | `CatItemsService.cpp:92` | 普通/巨鱼并发 NearShore；确认首个合法者唯一得鱼 |
| 容器不同步/超容/转移半提交 | `CatItemsService.cpp:31` / `:179` | Replication component `:25`、settings capacity | 双客户端 FastArray、满容量、revision conflict |
| 献祭丢鱼或重复贡献 | `CatSacrificeCoordinator.cpp:26` | Items reservation `:334` 起 | Reserve/Run/Commit/Cancel 各步骤失败注入 |
| 部分 CapturePlan | `CatRunImprintService.cpp:141` | Fishing `:438`、Camp `:193`、Social caught-imprint | 1/2/4/8 人 batch、离线 recipient、同步 RPC 重入 |
| Grant 重复/ACK 过早 | `CatProfileSubsystem.cpp:70` | `:237-367` merge/save/reload、Imprint `:321` ACK | 崩溃/重启/损坏/重复 grant/ACK 丢失 |
| 装备未解锁或远程维修 | `CatEquipmentComponent.cpp:30` / `:248` | Equipment settings/definitions、PlayerState unlock | 未解锁/伪 revision/营地内外 |
| 远程救援/草药/共享缸 | `CatConditionComponent.cpp:107-185`、`CatCampHubActor.cpp:43-131` | Controller RPC `:1492-1630` | 服务器距离边界、不同团队/角色、重复请求 |
| 偷鱼/Catch 竞态 | `CatSocialService.cpp:61` | `:150-244` protocol、`:483` timeout、Items theft | Catch vs timeout、角色退出、shared tank policy |
| 自动化假失败/挂起 | 两脚本 `_on_slate_pre_tick` | GUID key 与 `WAIT_END_PIE` 分支 | 正常/缺 Lake 两脚本、进程残留和主文件 hash |

## 15. 待程序员审查与人工断点

### 15.1 程序员 Review 断点

建议程序员不要逐文件平铺阅读，按以下断点签字：

1. **Online 所有权断点：** 确认没有其他类直接写 Session/Travel/四事实；检查 public pending gate、epoch、Destroy 补偿和 Frontend Transport 收敛。
2. **身份/准入断点：** 确认所有 Server RPC 都从 Controller/PlayerState 重建 StableNetId，并统一经过 gameplay gate；检查 PreLogin/PostLogin/reconnect 事实。
3. **StateTree 唯一拓扑断点：** 确认 Run/Fishing C++ 没有资产缺失 fallback FSM；正式资产接入时只调用现有 Task/contract。
4. **唯一鱼断点：** 从所有 `FCatFishInstance` 创建点反查，只允许 Items capture commit 产生实物；NearShore 只允许首个合法 compare-and-commit。
5. **跨聚合断点：** 检查 Sacrifice reservation、Imprint batch、Profile journal 的每个 early return、teardown 和重入。
6. **复制/隐私断点：** 检查 FastArray、GameState/PlayerState 只公开必要 DTO；Profile durable 私有数据不复制给其他玩家。
7. **Social 安全断点：** 检查 TheftProtocolId、authority actor 距离、能力/团队资格和 timer teardown。
8. **最小性断点：** 确认没有为了缩短大文件再引入无独立生命周期的 manager；新增产品功能优先扩展现有域合同。
9. **证据断点：** 对照 `testing-report.md`，不要把 Null OSS、headless Editor 或代码 Review 写成 Steam/B–G/人工 PIE 通过。

### 15.2 旧 finding 闭环

- 初轮 F01–F10：在后续完整 diff re-review 中均为 closed，无回归。
- R2-F11 SaveGame 嵌套、R2-F12 同步 RPC、R2-F13 lifespan、R2-F14 救援/草药/共享缸距离：均 closed。
- F05 全参与者计划、F09 统一能力/团队可达：完整调用面复核 closed。
- R3-F15 CapturePlan 两阶段 batch：Fishing、Camp、Social 与单人接口全部改用同一 batch 合同，部分成功根因已消除，closed。
- 机械 UHT/include/const-ref 修复与最终 Online pending/Frontend transport/script 顺序修复均经过完整面 re-review，未发现行为回归。
- 当前 `code_findings: none`。这句话只表示当前代码审查没有 finding，不代表 Harness 或产品验收完整。

### 15.3 人工/产品断点

- 当前 `DefaultGame.ini` 没有 B–G 正式 settings；如果程序员 Review 需要运行正常路径，应在产品负责人提供配置后另开验证轮次，不能在 Review 中猜值。
- 缺 `ST_RunFlow`、`ST_FishingSession`、Fish/Equipment DataAsset、InputAction/MappingContext、WaterRegion/Camp Actor 时，B–G 产品体验验收必须中止。
- 双账号 Steam、可见 PIE、完整日循环、普通/巨鱼、NearShore 竞态、持久化重启和 Social 竞态均应按 `.codex/docs/acceptance-report.md` 留证。

### 15.4 程序员签字建议

程序员完成上述九个 Review 断点后，可以把“代码审查”单独标记为通过；只有补齐资产/配置并取得人工与 Steam 证据后，才能推进“产品验收”。当前建议结论保持：**代码独立审查 pass，程序员 Review 待完成，B–G 产品运行待验收。**
