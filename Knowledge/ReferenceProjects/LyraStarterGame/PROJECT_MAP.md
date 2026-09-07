# LyraStarterGame 参考项目地图

更新时间：2026-09-07
文档状态：当前 `reference_context` 地图。本文只记录本地 Lyra 项目的源码、配置、插件描述和可见资产路径事实，不把 Lyra 规则写成 Catfishing 当前实现事实。
范围：面向后续 Catfishing 开发、架构复核和代码审查，说明本地 Lyra 从哪里开始读、哪些链路值得参考、哪些部分不能直接套用。本文不覆盖 Lyra `.uasset` 内部字段、蓝图图表、运行结果或构建结论。

## 事实来源

- `D:\UnreaProjects\LyraStarterGame\LyraStarterGame.uproject`
- `D:\UnreaProjects\LyraStarterGame\Config\DefaultEngine.ini`
- `D:\UnreaProjects\LyraStarterGame\Config\DefaultGame.ini`
- `D:\UnreaProjects\LyraStarterGame\Source\LyraGame\LyraGame.Build.cs`
- `D:\UnreaProjects\LyraStarterGame\Source\LyraGame.Target.cs`
- `D:\UnreaProjects\LyraStarterGame\Source\LyraGame\`
- `D:\UnreaProjects\LyraStarterGame\Plugins\GameFeatures\`
- Catfishing 引用核对：`Source/Catfishing/Input/CatInputConfig.h`、`Source/Catfishing/Framework/Game/CatfishingGameState.h/.cpp`、`Docs/DataAsset字段含义.md`、`Docs/Architecture/属性系统重构子技术方案.md`、`Docs/Architecture/项目架构讨论纪要.md`

## 地图定位

Lyra 根目录是 `D:\UnreaProjects\LyraStarterGame`，与当前项目 `D:\UnreaProjects\Catfishing-verify-01` 同级。当前搜索没有发现 Catfishing 直接 include Lyra 源码、依赖 Lyra 模块或引用 Lyra 插件模块的文本证据；已看到的关系是“参考 Lyra 风格/口径”的本地实现说明。

Catfishing 的当前源码事实仍由 `Knowledge/Framework/PROJECT_MAP.md` 负责。阅读 Lyra 后形成的任何实现判断，都必须回到 Catfishing 的 `Source/`、`Config/`、`Content/` 和当前架构文档核对，不能直接把 Lyra 的宿主、字段、插件或资产链路当作本项目事实。

## Lyra 项目入口

| 入口 | 当前事实 | 主要用途 |
| --- | --- | --- |
| `LyraStarterGame.uproject` | EngineAssociation 为 `5.8`；声明 `LyraGame` Runtime 模块和 `LyraEditor` Editor 模块；启用 GameplayAbilities、GameFeatures、ModularGameplay、EnhancedInput、CommonUI、CommonGame、CommonUser、GameplayMessageRouter、OnlineSubsystemSteam、SteamSockets 等插件。 | 判断 Lyra 依赖的引擎和插件表面。 |
| `Source/LyraGame/LyraGame.Build.cs` | `LyraGame` 是主 Runtime 模块，公开依赖 GAS、GameplayTags、ModularGameplay、GameFeatures、ReplicationGraph、CommonLoadingScreen 等，私有依赖 EnhancedInput、CommonUI、CommonGame、CommonUser、GameplayMessageRuntime 等。 | 判断 Lyra 的 C++ 模块边界和依赖来源。 |
| `Source/LyraGame.Target.cs` | `LyraGameTarget.ApplySharedLyraTargetSettings` 处理共享 Target 设置，并扫描 `Plugins/GameFeatures` 下的插件描述来决定构建启用策略。 | 判断 GameFeature 插件不是普通静态模块开关。 |
| `Config/DefaultEngine.ini` | 指定 `AssetManagerClassName=/Script/LyraGame.LyraAssetManager`；默认地图为 `/Game/System/FrontEnd/Maps/L_LyraFrontEnd.L_LyraFrontEnd`；默认 GameMode 蓝图为 `/Game/B_LyraGameMode.B_LyraGameMode_C`。 | 判断启动、地图和全局类替换入口。 |
| `Config/DefaultGame.ini` | 指定 `GameFeaturesManagerClassName=/Script/LyraGame.LyraGameFeaturePolicy`；配置 `LyraGameDataPath`、`DefaultPawnData` 和 Primary Asset 扫描类型。 | 判断 Asset Manager、Experience、GameFeatureData 和 PawnData 的发现方式。 |
| `Plugins/GameFeatures/` | 包含 `ShooterCore`、`ShooterExplorer`、`ShooterMaps`、`ShooterTests`、`TopDownArena`。已核对的 GameFeature `.uplugin` 均为 `ExplicitlyLoaded=true`、`EnabledByDefault=false`。 | 判断玩法插件是被 Experience/策略显式激活的参考链路。 |

## 运行时链路锚点

### Experience 与 GameMode

`ALyraGameMode` 是 Lyra 玩法进入后的服务器框架入口。它在 `InitGame` 后延迟到下一帧执行 `HandleMatchAssignmentIfNotExpectingOne`，按匹配分配、URL、开发设置、命令行、World Settings、专服和默认配置等顺序确定 `FPrimaryAssetId`，再通过 `OnMatchAssignmentGiven` 交给 GameState 上的 `ULyraExperienceManagerComponent`。

关键阅读入口：

- `Source/LyraGame/GameModes/LyraGameMode.h/.cpp`
- `Source/LyraGame/GameModes/LyraExperienceManagerComponent.h/.cpp`
- `Source/LyraGame/GameModes/LyraExperienceDefinition.h/.cpp`
- `Source/LyraGame/GameModes/LyraExperienceActionSet.h/.cpp`
- `Source/LyraGame/GameModes/LyraUserFacingExperienceDefinition.h/.cpp`
- `Source/LyraGame/GameModes/LyraWorldSettings.h/.cpp`

`ULyraExperienceManagerComponent` 持有当前 Experience，负责加载 Experience 本身、ActionSet、Raw Asset 和 GameFeature 插件。`ULyraExperienceDefinition` 是数据资产定义，字段包括 `GameFeaturesToEnable`、`DefaultPawnData`、`Actions` 和 `ActionSets`。

### Pawn、PlayerState 与 GAS

Lyra 的玩家 ASC 关键宿主是 `ALyraPlayerState`。它构造 `ULyraAbilitySystemComponent`，并持有 `ULyraHealthSet` 与 `ULyraCombatSet`；Experience 加载后，`ALyraPlayerState::SetPawnData` 从 PawnData 授予 AbilitySet。

`ULyraPawnData` 实际位于 `Source/LyraGame/Character/LyraPawnData.h`，不是 `GameModes/`。它把 `PawnClass`、`AbilitySets`、`TagRelationshipMapping`、`InputConfig` 和 `DefaultCameraMode` 聚合成一份 Pawn 定义。`ALyraGameMode::SpawnDefaultPawnAtTransform_Implementation` 生成 Pawn 后，把 PawnData 写入 `ULyraPawnExtensionComponent`。

关键阅读入口：

- `Source/LyraGame/Player/LyraPlayerState.h/.cpp`
- `Source/LyraGame/Character/LyraPawnData.h`
- `Source/LyraGame/Character/LyraPawnExtensionComponent.h/.cpp`
- `Source/LyraGame/Character/LyraHeroComponent.h/.cpp`
- `Source/LyraGame/Character/LyraCharacter.h/.cpp`
- `Source/LyraGame/AbilitySystem/LyraAbilitySystemComponent.h/.cpp`
- `Source/LyraGame/AbilitySystem/LyraAbilitySet.h/.cpp`
- `Source/LyraGame/AbilitySystem/Attributes/LyraHealthSet.h/.cpp`
- `Source/LyraGame/AbilitySystem/Attributes/LyraCombatSet.h/.cpp`
- `Source/LyraGame/AbilitySystem/Executions/LyraDamageExecution.h/.cpp`
- `Source/LyraGame/AbilitySystem/Executions/LyraHealExecution.h/.cpp`

Lyra 的 GameState 也实现 `IAbilitySystemInterface`，`ALyraGameState` 持有一个面向全局玩法事件的 `ULyraAbilitySystemComponent`，主要用于 game-wide 能力系统能力和 GameplayCue 相关场景。参考 Catfishing 的 Run ASC 时，读它的宿主模式即可，不要把 Lyra 的所有比赛状态模型照搬过来。

### Input

Lyra 使用“设备输入 -> InputTag -> Native 或 Ability 消费者”的结构。`ULyraInputConfig` 区分 `NativeInputActions` 与 `AbilityInputActions`；`ULyraInputComponent` 根据 InputConfig 绑定原生动作和 Ability 动作；`ULyraHeroComponent` 在玩家 Pawn 初始化后把 InputTag pressed/released 转交给 `ULyraAbilitySystemComponent`。

关键阅读入口：

- `Source/LyraGame/Input/LyraInputConfig.h/.cpp`
- `Source/LyraGame/Input/LyraInputComponent.h/.cpp`
- `Source/LyraGame/Character/LyraHeroComponent.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_AddInputBinding.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_AddInputContextMapping.h/.cpp`

资产锚点只记录路径存在，不代表内部字段已核验：

- `Content/Input/InputData_Hero.uasset`
- `Content/Input/InputData_SimplePawn.uasset`
- `Content/Input/Mappings/IMC_Default.uasset`
- `Plugins/GameFeatures/ShooterCore/Content/Input/Actions/InputData_ShooterGame_AddOns.uasset`
- `Plugins/GameFeatures/ShooterCore/Content/Input/Mappings/IMC_ShooterGame.uasset`

### GameFeature 动态授予

Lyra 的 GameFeature 插件不是“目录存在就运行”。已核对的 `ShooterCore`、`ShooterMaps`、`TopDownArena` 描述文件都显式加载、默认不启用；Experience 或 ActionSet 收集到插件 URL 后，`ULyraExperienceManagerComponent` 调用 `UGameFeaturesSubsystem::LoadAndActivateGameFeaturePlugin` 激活。

关键阅读入口：

- `Source/LyraGame/GameFeatures/LyraGameFeaturePolicy.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_WorldActionBase.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_AddAbilities.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_AddInputBinding.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_AddInputContextMapping.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_AddWidget.h/.cpp`
- `Plugins/GameFeatures/ShooterCore/ShooterCore.uplugin`
- `Plugins/GameFeatures/ShooterMaps/ShooterMaps.uplugin`
- `Plugins/GameFeatures/TopDownArena/TopDownArena.uplugin`

代表性内容锚点：

- `Content/System/FrontEnd/B_LyraFrontEnd_Experience.uasset`
- `Content/System/Experiences/B_LyraDefaultExperience.uasset`
- `Content/System/Playlists/DA_Frontend.uasset`
- `Plugins/GameFeatures/ShooterCore/Content/Experiences/B_ShooterGame_Elimination.uasset`
- `Plugins/GameFeatures/ShooterCore/Content/Experiences/LAS_ShooterGame_SharedInput.uasset`
- `Plugins/GameFeatures/ShooterCore/Content/Experiences/LAS_ShooterGame_StandardComponents.uasset`
- `Plugins/GameFeatures/ShooterCore/Content/Experiences/LAS_ShooterGame_StandardHUD.uasset`
- `Plugins/GameFeatures/TopDownArena/Content/System/Experiences/B_TopDownArenaExperience.uasset`

### UI

Lyra 的 UI 栈依赖 CommonUI/CommonGame。`ULyraUIManagerSubsystem` 继承 `UGameUIManagerSubsystem`，CommonGame 插件中的 `UPrimaryGameLayout` 负责按层推入 `UCommonActivatableWidget`。GameFeature 可通过 `GameFeatureAction_AddWidget` 把 HUD 或页面挂到指定 UI 层。

关键阅读入口：

- `Source/LyraGame/UI/Subsystem/LyraUIManagerSubsystem.h/.cpp`
- `Source/LyraGame/UI/LyraHUD.h/.cpp`
- `Source/LyraGame/UI/LyraHUDLayout.h/.cpp`
- `Source/LyraGame/UI/Frontend/LyraFrontendStateComponent.h/.cpp`
- `Source/LyraGame/GameFeatures/GameFeatureAction_AddWidget.h/.cpp`
- `Plugins/CommonGame/Source/Public/PrimaryGameLayout.h`
- `Plugins/CommonGame/Source/Public/CommonUIExtensions.h`
- `Plugins/UIExtension/Source/UIExtension.Build.cs`

Catfishing 当前架构明确“不采用 CommonUI”，使用原生 UMG 和项目自有小型 UI 组织层。因此这里适合参考“每个本地玩家有 UI 管理者、Root Layout 提供层、玩法插件/功能向层提交 UI”的结构思想，不应直接引入 Lyra 的 CommonUI 类型或 Widget 栈。

### 消息、Online 与 Session

Lyra 使用 `GameplayMessageRouter` 做弱耦合通知，消息以 GameplayTag Channel 加 USTRUCT payload 分发。它适合参考“广播者和监听者不直接互相引用”的事件边界，但 Catfishing 需要先判断现有服务、复制 DTO 和 owning-client RPC 是否已经足够。

关键阅读入口：

- `Plugins/GameplayMessageRouter/Source/GameplayMessageRuntime/Public/GameFramework/GameplayMessageSubsystem.h`
- `Source/LyraGame/Messages/LyraVerbMessage.h`
- `Source/LyraGame/Messages/LyraVerbMessageHelpers.h/.cpp`
- `Source/LyraGame/Messages/LyraVerbMessageReplication.h/.cpp`
- `Source/LyraGame/Messages/GameplayMessageProcessor.h/.cpp`
- `Source/LyraGame/System/LyraGameInstance.h/.cpp`
- `Source/LyraGame/System/LyraGameSession.h/.cpp`
- `Source/LyraGame/UI/Frontend/LyraFrontendStateComponent.h/.cpp`

Lyra 的 Online 表面涉及 `CoreOnline`、`CommonUser`、`CommonGame`、`OnlineFramework`、`OnlineServicesNull`、`OnlineServicesOSSAdapter`、`OnlineSubsystemSteam` 和 `SteamSockets`。Catfishing 当前已经有自己的 `UCatOnlineSubsystem`、Frontend/Lake 两图策略和 Steam Session 边界，不能用 Lyra 的前端流程替换当前 Online 真相。

## Catfishing 已采用的 Lyra 参考口径

当前项目只看到文本级参考，没有看到直接依赖 Lyra 模块的证据。

| Catfishing 位置 | 已采用的参考口径 | 当前边界 |
| --- | --- | --- |
| `Source/Catfishing/Input/CatInputConfig.h`、`Docs/DataAsset字段含义.md` | 设备输入映射到稳定 `InputTag`，再由 Native 或 Ability 消费者处理。 | 只参考输入分层，不引入 Lyra 的完整 CommonUI/Input 设置体系。 |
| `Source/Catfishing/Framework/Game/CatfishingGameState.h/.cpp` | GameState 可以拥有一个 ASC，Owner/Avatar 都绑定为 GameState。 | Catfishing 的 Run ASC 只承载全队 Run 数值；Run 阶段、Revision、Ready、偷鱼协议、容器数组仍归领域状态。 |
| `Docs/Architecture/属性系统重构子技术方案.md` | HealthSet 的真实属性与 Meta Attribute / ExecutionCalculation 思路可参考。 | Catfishing 当前决定是 Character-owned ASC，不把身体属性迁到 PlayerState。 |
| `Docs/Architecture/项目架构讨论纪要.md` | GAS + StateTree + Enhanced Input 是当前框架方向之一。 | 文档明确不采用 CommonUI；PlayerState 只承担稳定身份和连接期事实。 |

## 最短阅读顺序

1. 先读 `LyraStarterGame.uproject`，确认模块和启用插件。
2. 再读 `Config/DefaultEngine.ini` 与 `Config/DefaultGame.ini`，确认 AssetManager、默认地图、Experience、GameFeature 策略和 Primary Asset 扫描。
3. 读 `Source/LyraGame/LyraGame.Build.cs` 与 `Source/LyraGame.Target.cs`，确认 C++ 依赖和 GameFeature 构建策略。
4. 读 `Source/LyraGame/System/LyraAssetManager.h/.cpp`，确认全局数据和 PawnData 的加载方式。
5. 读 `Source/LyraGame/GameModes/LyraGameMode.cpp` 与 `Source/LyraGame/GameModes/LyraExperienceManagerComponent.cpp`，确认 Experience 如何被选择、加载和激活。
6. 读 `Source/LyraGame/GameModes/LyraExperienceDefinition.h`、`Source/LyraGame/GameModes/LyraExperienceActionSet.h` 和 `Source/LyraGame/Character/LyraPawnData.h`，确认 Experience、ActionSet、PawnData 的数据职责。
7. 读 `Source/LyraGame/Player/LyraPlayerState.h/.cpp`、`Source/LyraGame/Character/LyraPawnExtensionComponent.h/.cpp` 和 `Source/LyraGame/Character/LyraHeroComponent.h/.cpp`，确认 ASC Owner/Avatar、Pawn 初始化和输入绑定时序。
8. 读 `Source/LyraGame/AbilitySystem/LyraAbilitySet.h/.cpp` 与 `Source/LyraGame/GameFeatures/GameFeatureAction_AddAbilities.h/.cpp`，确认 Ability、Effect、AttributeSet 的授予和撤销路径。
9. 读 `Source/LyraGame/Input/LyraInputConfig.h/.cpp` 与 `Source/LyraGame/Input/LyraInputComponent.h/.cpp`，确认 InputTag 路由。
10. 读 `Source/LyraGame/UI/Subsystem/LyraUIManagerSubsystem.h/.cpp`、`Plugins/CommonGame/Source/Public/PrimaryGameLayout.h` 和 `Source/LyraGame/GameFeatures/GameFeatureAction_AddWidget.h/.cpp`，确认 UI 层和 GameFeature UI 注入。
11. 读 `Plugins/GameplayMessageRouter/Source/GameplayMessageRuntime/Public/GameFramework/GameplayMessageSubsystem.h` 与 `Source/LyraGame/Messages/`，确认 GameplayMessage 的低耦合通知模型。

## 容易误读的点

- Lyra 是参考项目，不是 Catfishing 的上游基类库；没有直接 include 或模块依赖证据时，不要把 Lyra 符号写进 Catfishing 方案。
- `Plugins/GameFeatures` 下的插件描述存在，不等于运行时已激活；Experience 加载链路才是激活入口。
- Lyra 的玩家 ASC 在 `ALyraPlayerState`，Catfishing 当前身体 ASC 在 `ACatCharacter`，这是项目需求不同导致的宿主选择，不是需要自动“向 Lyra 对齐”的差异。
- Lyra 的 `ULyraPawnData` 文件在 `Source/LyraGame/Character/`，不要按 Experience 语义误找进 `GameModes/`。
- Lyra 使用 CommonUI/CommonGame；Catfishing 当前架构明确不采用 CommonUI，最多参考本地玩家 UI 生命周期、层级和只读展示边界。
- 本文没有打开 `.uasset` 内部字段。资产路径只能证明锚点存在，不能证明其蓝图 Action 列表、WBP 层级或具体配置值。

## 维护规则

当 Lyra 项目升级、移动目录或 Catfishing 开始直接复用 Lyra 插件/模块时，先重新核对 `LyraStarterGame.uproject`、`Config/DefaultGame.ini`、`Source/LyraGame.Target.cs`、`Source/LyraGame/` 和 `Plugins/GameFeatures/`，再更新本文。更新时仍保持 Lyra 为 `reference_context`；只有回到 Catfishing 当前源码、配置、资产和架构决策核实后，才允许把某条参考转写为 Catfishing 项目事实。
