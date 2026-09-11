# Catfishing 当前框架项目地图

更新时间：2026-09-09
文档状态：当前源码事实与 Frontend 实施中边界地图。
范围：面向后续程序员和 AI 代码审查，说明当前框架从哪里读、运行时真相由谁持有、哪些资料只能作为背景来源。本文不替代 GDD、数值表或验收报告。

## 文档位置

本目录描述当前 `Source/Catfishing` 已落地的框架。若它与交接或调研文档冲突，以真实源码、配置、资产和最新构建证据为准。

- 产品规则先读 `Knowledge/Design/`。
- 当前架构先读本目录和真实源码；`Docs/Architecture/项目技术方案.md` 与接线溯源只作为背景材料，凡与当前源码冲突都以本目录为准。
- 当前代码事实先读本文和本目录其他文件。
- 构建、运行和验收证据看 `.codex/docs/testing-report.md`、`.codex/docs/acceptance-report.md`、`.codex/docs/review-report.md`。
- `Knowledge/Design/` 是设计真值（2026-09-11 起）：正文直接改这里、走 git；内容表 CSV 仍在飞书填、定稿拉进来；`_feishu/` 是飞书非正式页的只读快照。动手前读该目录 README。
- 旧的 `Knowledge/GDD/` 快照与 `Knowledge/Feishu/` 镜像都已删除；遇到同名或改名文档以 `Knowledge/Design/README.md` 为准。
- `Docs/gap-analysis/2026-09-09/` 是飞书镜像和代码的对表审计材料；总览看 `SUMMARY.md`，待讨论项看 `讨论清单.md`。在属主确认前，它不等同于产品需求、工程进度或代码修改指令。

交接文档中关于“空模板”的描述不能用于判断当前工程是否已实现框架。

## 当前源码入口

项目保持单 Runtime 模块：`Source/Catfishing/Catfishing.Build.cs`。模块内部按领域目录组织，目录不是 UE 子模块，也不是 DLL 边界。

| 目录 | 当前责任 | 主要入口 |
|---|---|---|
| `Logging/` | 项目日志分类。 | `CatLog.h`、`Catfishing.cpp` |
| `Framework/Core/` | 跨领域 DTO、Result、Run/Profile 合同。 | `CatRunContracts.h`、`CatProfileContracts.h`、`CatDomainCommandTypes.h` |
| `Framework/Game/` | UE 游戏框架宿主、玩家准入、Run 写口、PlayerController RPC。 | `CatFrontendGameMode.*`、`CatfishingGameModeBase.*`、`CatfishingGameState.*`、`CatfishingPlayerState.*`、`CatfishingPlayerController.*`；`CatGameplayTypes.h` 仅作共享 Gameplay 类型入口 |
| `Online/` | Session、邀请、Frontend 房间停留、显式 Host 开始、异步预载、旅行与失败收口。 | `CatOnlineSubsystem.h/.cpp`；新流程已落源码，未获 runtime 证据 |
| `Save/` | 独立于 Profile 的世界槽索引、异步读写、库存内容和角色位置恢复。 | `CatSaveSubsystem.h/.cpp`、`CatRunSaveGame.h`；槽框架及领域导出/恢复接口已落源码，读写与恢复结果未验证 |
| `UI/Frontend/` | Frontend Root、页面流程和 Save/Room/Settings 三个只读 Model。 | `UCatFrontendRootWidget`、`UCatFrontendPageController`、`UCatFrontendSaveModel`、`UCatFrontendRoomModel`、`UCatFrontendSettingsModel` |
| `UI/` | LocalPlayer UI 生命周期、WBP 配置和局内只读展示。 | `UCatLocalPlayerUISubsystem`、`UCatUISettings`、`UCatSurvivalWidget`；Frontend 接线已落源码，全局加载遮罩由 LocalPlayer UI 管 |
| `Character/` | 猫身体 Actor、ASC Owner/Avatar 和身体宿主组件装配。 | `ACatCharacter` |
| `AbilitySystem/` | Ability/AttributeSet/能力配置、Ability 输入路由和正式 GameplayEffect 写口；输入 Ability 按领域放在 `Fishing/InputAbilities/` 等子目录。 | `UCatAbilitySystemComponent`、`UCatAbilityInputBindingComponent`、`UCatSurvivalAttributeSet`、`UCatGE_PoisonDelta`、`UCatAbilitySettings` |
| `Condition/` | Wet/Downed/Recovery 等离散身体状态。 | `UCatConditionComponent` |
| `Equipment/` | 钓具选择读模型、Fishing 使用冻结、绑定鱼竿磨损和失败预算。 | `UCatEquipmentComponent`、`UCatEquipmentDefinition` |
| `Run/` | StateTree 节点、Run 配置和供品结算写口。 | `CatRunStateTreeNodes.*`、`CatRunSettings.*` |
| `Environment/` | 水域查询、WaterRegion、窝料聚鱼与环境配置。 | `UCatWaterQuerySubsystem`、`ACatWaterRegion` |
| `Fishing/` | 钓鱼会话、阶段推进、搏斗协作、近岸抢抄、失败预算。 | `UCatFishingService`、`ACatFishingSession` |
| `FishContainers/` | 鱼实例、容器快照、鱼护/鱼缸事务、吃鱼、售鱼和偷鱼 escrow。 | `UCatFishContainerService`、`UCatContainerReplicationComponent`、`ACatFishGuardActor`、`ACatFishTankActor` |
| `Inventory/` | 正式道具 Entry+Instance、背包/公共库存和库存槽位事务。 | `UCatInventoryComponent`（`FCatInventoryEntry` 与 Use 回执）、`CatInventoryItemDefinition`、`CatInventoryItemInstance`、`CatBackPackComponent`、`UCatInventoryStatics`、`UCatInventorySettings` |
| `Collection/` | 捕获事实到 Grant、CapturePlan 投递、Grant ACK。 | `UCatRunImprintService` |
| `Profile/` | LocalPlayer SaveGame、Grant Journal、装备选择、相册隐藏。 | `UCatProfileSubsystem`、`UCatProfileSaveGame` |
| `Social/` | 求助、恶作剧、保护牌、偷鱼协议权限。 | `UCatSocialService`、`ACatProtectionSignActor` |
| `Camp/` | 固定营地、救援落点、休息、鱼缸转移、篝火回看。 | `ACatCampHubActor` |
| `Data/` | 鱼表 DataAsset 和目录设置。 | `UCatFishDefinition`、`UCatFishCatalogSettings` |

## 运行时真相

当前框架的核心规则是：每类状态只有一个写入者，其他系统通过 Command/Result、只读快照或复制结果消费。

| 真相 | 写入者 | 消费者 |
|---|---|---|
| Session、World、Transport、ActiveOperation 四份联机事实 | `UCatOnlineSubsystem` | UI、PlayerController、GameMode teardown |
| Frontend 当前页、选中世界槽与确认状态 | `UCatFrontendPageController` | `UCatFrontendRootWidget` 和四个业务子 WBP |
| 世界存档槽、库存内容与角色位置 | `UCatSaveSubsystem` / `UCatRunSaveGame` | `UCatFrontendSaveModel`、房主开始游戏链；不写入 Profile |
| Frontend 房间、好友、成员、邀请码与地图加载投影 | `UCatOnlineSubsystem` | `UCatFrontendRoomModel`、房间页、全局 Loading 遮罩 |
| Frontend 设置草稿 | `UCatFrontendSettingsModel` | 设置页；应用后由正式 GameUserSettings、音频和输入来源持有生效值 |
| Run Phase、额度、夜晚 ready、Host exit 等局事实 | `ACatfishingGameModeBase` | `ACatfishingGameState`、StateTree、Online |
| 公开 Run/Environment/Help 快照 | `ACatfishingGameState` | UI、本地表现、客户端只读逻辑 |
| 稳定身份、普通夜 ready、公开鱼图鉴摘要 | `ACatfishingPlayerState` | GameMode、Social、UI |
| 猫身体、ASC、Poison/FishingStrength/FightStamina、Condition、钓具读模型入口 | `ACatCharacter` 及其组件 | UI、Fishing、Condition、Equipment |
| 单次钓鱼会话阶段、参与者、鱼运行态、抢抄终态 | `ACatFishingSession` | FishingService、FishContainers、Collection、客户端复制 |
| 鱼实例、容器数组、转移、吃鱼、售鱼、偷鱼 escrow | `UCatFishContainerService` | Fishing、Social、ShopEconomy、Run 供品结算、Container 复制组件 |
| 供品结算结果、每日目标和世界进度 | `ACatfishingGameModeBase` 与 Run ASC | `ACatfishingGameState`、StateTree、Online teardown |
| CapturePlan、GrantDelivery、ImprintDelivery | `UCatRunImprintService` | PlayerController、Profile、Run settlement |
| 本地永久档案和 Grant Journal | `UCatProfileSubsystem` | PlayerController ACK、UI、Public collection 发布 |
| 求助、恶作剧、保护牌和偷鱼社交协议 | `UCatSocialService` | GameState、FishContainers、PlayerController |

## 最短阅读顺序

审查 Frontend Root、页面流转与 WBP 拼装：

1. `Docs/Development/主界面重构设计笔记.md`
2. `Docs/Development/主界面子技术文档.md`
3. `Docs/Development/UI_WBP拼装接口清单.md`
4. `Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h/.cpp`
5. `Source/Catfishing/UI/Frontend/CatFrontendPageController.h/.cpp`
6. `Source/Catfishing/UI/Frontend/CatFrontendSaveModel.h/.cpp`
7. `Source/Catfishing/UI/Frontend/CatFrontendRoomModel.h/.cpp`
8. `Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h/.cpp`
9. `Source/Catfishing/UI/CatLocalPlayerUISubsystem.h/.cpp`

世界槽继续读 `Source/Catfishing/Save/CatSaveSubsystem.h/.cpp`；资产生成继续读 `Source/CatfishingEditor/UI/CatFrontendWidgetAuthoringLibrary.h/.cpp` 与 `Scripts/create_frontend_assets.py`。源码和生成入口均已落地，正式 Root `/Game/UI/Frontend/WBP_CatFrontendRoot` 及配套 `.uasset` 尚未生成，完整构建与 runtime 未通过验证。

审查联机、旅行或回前台：

1. `Source/Catfishing/Online/CatOnlineTypes.h`
2. `Source/Catfishing/Online/CatOnlineSubsystem.h/.cpp`
3. `Source/Catfishing/UI/CatLocalPlayerUISubsystem.h/.cpp`
4. `Source/Catfishing/Framework/Game/CatfishingPlayerController.h/.cpp`

审查入局、身份、Run 与 Host 退出：

1. `Source/Catfishing/Framework/Core/CatRunContracts.h`
2. `Source/Catfishing/Framework/Game/CatfishingGameModeBase.h/.cpp`
3. `Source/Catfishing/AbilitySystem/Executions/CatRunSettleOfferingExecutionCalculation.h/.cpp`
4. `Source/Catfishing/Online/CatOnlineSubsystem.h/.cpp`

审查 Character、GAS、状态和 UI：

1. `Source/Catfishing/Character/CatCharacter.h/.cpp`
2. `Source/Catfishing/AbilitySystem/Input/CatAbilityInputBindingComponent.h/.cpp`
3. `Source/Catfishing/AbilitySystem/Attributes/CatSurvivalAttributeSet.h/.cpp`
4. `Source/Catfishing/Condition/CatConditionComponent.h/.cpp`
5. `Source/Catfishing/Equipment/CatEquipmentComponent.h/.cpp`
6. `Source/Catfishing/UI/CatLocalPlayerUISubsystem.h/.cpp`

审查钓鱼、鱼实例和捕获结算：

1. `Source/Catfishing/Data/CatFishDefinition.h/.cpp`
2. `Source/Catfishing/Environment/CatWaterRegion.h/.cpp`
3. `Source/Catfishing/Fishing/CatFishingService.h/.cpp`
4. `Source/Catfishing/Fishing/CatFishingSession.h/.cpp`
5. `Source/Catfishing/FishContainers/CatFishContainerService.h/.cpp`
6. `Source/Catfishing/Collection/CatRunImprintService.h/.cpp`

审查 Profile、图鉴和印记投递：

1. `Source/Catfishing/Framework/Core/CatProfileContracts.h`
2. `Source/Catfishing/Collection/CatImprintTypes.h`
3. `Source/Catfishing/Collection/CatRunImprintService.h/.cpp`
4. `Source/Catfishing/Profile/CatProfileSubsystem.h/.cpp`
5. `Source/Catfishing/Profile/CatProfileSaveGame.h`

审查 FishContainers 与正式库存/装备边界：

1. `Source/Catfishing/FishContainers/CatFishContainerTypes.h`
2. `Source/Catfishing/FishContainers/CatFishContainerService.h/.cpp`
3. `Source/Catfishing/Inventory/CatInventoryComponent.h/.cpp`（`FCatInventoryEntry`、Use 回执、库存导出/恢复）
4. `Source/Catfishing/Inventory/CatInventoryItemDefinition.h/.cpp`、`CatInventoryItemInstance.h/.cpp`
5. `Source/Catfishing/Inventory/CatBackPackComponent.h/.cpp`（角色随身背包的 Inventory 归属）
6. `Source/Catfishing/Inventory/CatInventoryStatics.h/.cpp`
7. `Source/Catfishing/Equipment/CatEquipmentLoadoutSnapshot.h`
8. `Source/Catfishing/Equipment/CatEquipmentComponent.h/.cpp`
9. `Source/Catfishing/Equipment/CatEquipmentDefinition.h/.cpp`

## 核心链路

Frontend 正式结构是 Root + PageController + 3 Model。`UCatLocalPlayerUISubsystem` 负责 LocalPlayer 生命周期，创建目标 Root `/Game/UI/Frontend/WBP_CatFrontendRoot`，并在 Start、Leave 或地图旅行等待阶段创建全局 Loading 遮罩 `/Game/UI/Frontend/WBP_CatFrontendLoading`；遮罩阶段和百分比只读取 `UCatOnlineSubsystem` 的 `FCatOnlineSnapshot`，其中地图包百分比来自引擎异步加载进度。`UCatFrontendPageController` 只管“首页 -> 存档 -> 房间”，`UCatFrontendSaveModel`、`UCatFrontendRoomModel`、`UCatFrontendSettingsModel` 分别投影 Save、Online 和设置事实。Root 下装四个业务子 WBP（Menu、SaveList、Room、Settings）与三个动态行资产（SaveSlotRow、RoomFriendRow、RoomPlayerSlot），全局 Loading WBP 不嵌入 Root，连同 Root 共九个资产。

世界 Save 与 Profile 是两套不同持久化边界。`UCatSaveSubsystem` / `UCatRunSaveGame` 承担世界槽、库存内容和角色位置；`UCatProfileSubsystem` / `UCatProfileSaveGame` 承担 Grant Journal、图鉴、解锁和装备选择。Frontend 存档列表只能读 SaveModel，不得从 Profile 拼装世界槽摘要。

Save 源码已接 `AsyncLoadGameFromSlot`、`AsyncSaveGameToSlot`，并通过 `ACatCampInventoryActor`、`UCatInventoryComponent`、`UCatFishContainerService` 的受控接口导出/恢复正式库存和鱼容器；`UCatEquipmentComponent` 只导出/恢复钓具选择读模型。角色位置随玩家恢复链处理。领域写入者仍归各自系统，Save 负责持久化协调；这条链尚无实际恢复成功证据。

Frontend 到玩法地图的 Session 与旅行事实仍由 `UCatOnlineSubsystem` 持有。创建房间成功后必须停在 Frontend；只有房主显式点击“开始游戏”才开始真实异步预载和旅行。设置固定为游戏、画面、声音、控制四类，背景同时预留静态与动态承载；“加入队伍”和控制细项当前都只保留入口，不推导未确定行为。不要新增 ConsoleCommand、OpenLevel 或 Widget 旁路。

设置边界补充（2026-09-07 人工决定）：仅麦克风选择与语音输入模式允许暂不可用。`UCatFrontendSettingsModel` 对这两项返回不可用，Root 与资产生成器保留设置行和真实禁用原因，不持久化无效偏好；现有 Steam 语音缺少设备选择及输入模式接线，其他设置要求未放宽。

`UCatLocalPlayerUISubsystem::RefreshFrontendForCurrentController()` 和 `RemoveFrontendRoot()` 已接 Root、Controller、三 Model 的装配与解绑；正式 WBP 缺失时保持加载失败，不创建原生 fallback。源码接线存在不等于正式主界面已经生效。

玩家进入 Lake 后，`ACatfishingGameModeBase` 通过 `PreLogin`、`PostLogin`、`HandleStartingNewPlayer_Implementation` 维护 Reserved/Active 身份记录，并生成 `ACatCharacter`。`ACatfishingPlayerState` 只承载身份、ready 和公开摘要，不承载 ASC、正式库存、鱼容器或 Profile。

`ACatCharacter` 是猫身体宿主。ASC、`UCatSurvivalAttributeSet`、`UCatConditionComponent` 和 `UCatEquipmentComponent` 挂在 Character 上，但目录归属按系统本身判断：AttributeSet 属于 `AbilitySystem/`，Condition 属于 `Condition/`，Equipment 属于 `Equipment/`。个人鱼护是独立箱子式容器对象，由 FishContainers 注册和正式鱼护宿主承接。

`UCatFishingService` 负责创建和定位钓鱼会话；真正的阶段和抢抄终态在 `ACatFishingSession`。搏斗阶段可以登记协作者，近岸抢抄只允许首个合法提交者通过 FishContainers 的容器提交链取得实物鱼。

`UCatFishContainerService` 不是通用道具系统。它是鱼实例和鱼容器事务的服务器写口，管理个人鱼护、共享鱼缸、转移、吃鱼、售鱼和偷鱼 escrow。装备、窝料和草药等物品实例和数量归 `Inventory/`；`Equipment/` 只保存钓具选择读模型、Fishing 使用冻结、绑定鱼竿磨损和失败预算。

`UCatRunImprintService` 把已提交的领域事实转成 CapturePlan 和 Profile Grant。CapturePlan 是成像任务，Grant 是永久授予内容，Grant ACK 是客户端 durable 后的回执；三者不能互相替代。

## 配置与资产入口

`CatUISettings` 已把 `FrontendRootWidgetClass` 指向 `/Game/UI/Frontend/WBP_CatFrontendRoot.WBP_CatFrontendRoot_C`，`Config/DefaultGame.ini` 已精确加入 `/Game/UI/Frontend` 和 `/Game/Audio/Settings` 的 Cook 目录。`Source/CatfishingEditor/UI/CatFrontendWidgetAuthoringLibrary.h/.cpp` 与 `Scripts/create_frontend_assets.py` 已提供 9 个 WBP、6 个音频资产（1 个 SoundMix + 5 个 SoundClass）的生成入口；对应 `.uasset` 尚未生成，完整构建未通过，runtime 未验证。

当前默认地图在 `Config/DefaultEngine.ini` 指向 `/Game/Catfishing/Maps/Frontend`；Online 源码将创建房间与开始旅行分开，显式 Host 开始后才异步预载并进入 `CatOnlineSettings.GameplayMap`，当前测试值为 `/Game/NaturePackage/Maps/Showcase2`。Online 的 `Lake`/`TravelingToLake` 枚举运行含义是“联机玩法地图”。Frontend、Lake 与 Showcase2 都在打包地图白名单中，因此切回正式 Lake 只需修改一行 `GameplayMap` 配置，无需改 C++ 或重新维护 Cook 列表。
Showcase2 当前包含一个 `BP_CampHUB_C` 实例 `Showcase_CampHub`，位置为 `(4760, 22310, 417)`，它承担唯一营地出生点语义。地图里可以暂留普通 `PlayerStart`，但 `ACatfishingGameModeBase` 运行时只接受唯一的 `ACatCampHubActor`，不会把普通 `PlayerStart` 当作玩家出生点。

Build 依赖在 `Source/Catfishing/Catfishing.Build.cs`。当前是单 Runtime 模块，公开依赖包括 `GameplayAbilities`、`GameplayTags`、`NetCore`、`OnlineSubsystem` 和 `StateTreeModule`；实现侧依赖包括 `GameplayStateTreeModule`、`EnhancedInput` 和 `OnlineSubsystemUtils`。

Steam 当前使用开发测试 AppId 480，并已接入 `SteamDevAppId`、`bInitServerOnClient`、SteamSockets 插件及 `GameNetDriver`/连接类配置；本机 Win64 游戏模式冒烟已确认 Client API、Game Server API 与 OSS Steam 初始化成功。SteamSockets 的实际监听/连接驱动和真实双账号回调时序仍须双机验证，不能由单进程启动日志推断为已通过。

## 审查基线

本轮 Frontend 导航的事实来源是 `Docs/Development/主界面重构设计笔记.md`、`Docs/Development/主界面子技术文档.md`、本轮人工决策与主线程交接，以及上述 Runtime 源码、Editor 生成器和配置。`.codex/state/frontend-online-context.json` 的改动前入口、创建即旅行和缺少 Save 域接口记录属于改动前基线，不覆盖已变更源码。

`Saved/Logs/FrontendIntegrationBuild.log`（Build1）与 `Saved/Logs/FrontendIntegrationBuild2.log`（Build2）均为失败记录。主线程交接说明：Build1 的真实 C++ 错误已部分修正，Build2 的 `generated.h` 错配待源码静态冻结后强制 UHT 重编；该安排尚无成功结果。生成器、软路径、Cook 或源码存在均不能替代资产生成与运行证据，当前不能声明正式交付或完成。

后续 AI 审查时，先判断改动属于哪个系统，再读对应入口。不要按“谁调用它”或“它挂在哪个 Actor 上”来移动文件或评估职责。

审查结论必须区分三类事实：

- **源码事实**：当前 `Source/`、`Config/`、`Content/` 中存在并可构建的内容。
- **架构事实**：技术方案和接线文档已经定下的边界。
- **产品事实**：GDD 和用户裁决确定的玩法规则。

资料里的拟定名、空模板状态或候选方案不能压过当前源码事实；当前源码里的临时 gate、未配置默认值或诊断入口也不能被当成最终产品裁决。
