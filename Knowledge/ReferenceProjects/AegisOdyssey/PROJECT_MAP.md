# AegisOdyssey 参考项目地图

更新时间：2026-09-10

文档状态：当前项目的 `reference_context` 地图。本文只说明 `D:\UnreaProjects\AegisOdyssey` 作为外部参考项目时的入口、主链、可参考边界和误读风险，不把 AegisOdyssey 的架构规则提升为 Catfishing 当前规则。

范围：覆盖 AegisOdyssey 的 UE 项目入口、源码模块、配置、插件、项目自带知识库、主要运行时链路和适合 Catfishing 检索的参考方向。本文不覆盖 `.uasset` / `.umap` 二进制资产内部字段，不证明 AegisOdyssey 本身可构建、可运行或已完成正式联机交付。

## 事实来源

- `D:\UnreaProjects\AegisOdyssey\AegisOdyssey.uproject`
- `D:\UnreaProjects\AegisOdyssey\Config\DefaultEngine.ini`
- `D:\UnreaProjects\AegisOdyssey\Config\DefaultGame.ini`
- `D:\UnreaProjects\AegisOdyssey\Config\DefaultInput.ini`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey.Target.cs`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdysseyEditor.Target.cs`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\AegisOdyssey.Build.cs`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\GameModes\*.h/.cpp`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\System\*.h/.cpp`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Character\*.h/.cpp`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Player\*.h/.cpp`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\AbilitySystem\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Input\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Inventory\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Equipment\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Interaction\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Harvest\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Crafting\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\SkillSystem\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\UI\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\StateTree\**`
- `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Character\Enemies\AI\**`
- `D:\UnreaProjects\AegisOdyssey\Plugins\GameFeatures\AOGameCore\AOGameCore.uplugin`
- `D:\UnreaProjects\AegisOdyssey\Plugins\CommonUser\Source\CommonUser\Public\CommonSessionSubsystem.h`
- `D:\UnreaProjects\AegisOdyssey\Plugins\CommonUser\Source\CommonUser\Public\CommonUserSubsystem.h`
- `D:\UnreaProjects\AegisOdyssey\Docs\Knowledge\*\PROJECT_MAP.md`
- Catfishing 当前仓库内已保留本参考地图；源码层没有直接 include、配置或运行时依赖 AegisOdyssey。

## 地图定位

AegisOdyssey 位于当前仓库旁侧：

```text
D:\UnreaProjects\AegisOdyssey
```

它是 UE 5.6 项目，主模块为 `AegisOdyssey`。从项目 README、`AegisOdyssey.uproject` 和源码目录看，它是基于 `CommonGame/CommonUI + GAS + StateTree + GameFeatures + MVVM` 的第三人称动作 RPG 项目，重点系统包括角色、战斗、库存、装备、交互、采集、制造、技能和 AI。

当前 Catfishing 源码没有直接依赖 AegisOdyssey。使用它时应把它当成 `reference_context`：可借鉴模块拆分、运行时真相层、投影层、输入和 UI 观察链，但不能把 AegisOdyssey 的路径、类名、配置或内容资产当成 Catfishing 当前事实。

AegisOdyssey 自己已经有 `Docs/Knowledge`，其中多个系统存在 `PROJECT_MAP.md`、`DECISIONS.md` 和 `KNOWN_ISSUES.md`。本文件不复制那套知识库，只提供 Catfishing 侧检索入口和跨项目参考边界。

## AegisOdyssey 项目入口

| 入口 | 路径 | 当前事实 |
| --- | --- | --- |
| UE 项目 | `AegisOdyssey.uproject` | `EngineAssociation=5.6`，Runtime 模块是 `AegisOdyssey`。 |
| Game Target | `Source/AegisOdyssey.Target.cs` | 只声明 `TargetType.Game`，模块为 `AegisOdyssey`。 |
| Editor Target | `Source/AegisOdysseyEditor.Target.cs` | 只声明 `TargetType.Editor`，模块为 `AegisOdyssey`。 |
| 主模块规则 | `Source/AegisOdyssey/AegisOdyssey.Build.cs` | 依赖 `ModularGameplay`、`GameplayAbilities`、`GameFeatures`、`EnhancedInput`、`CommonUI`、`CommonUser`、`CommonGame`、`UIExtension`、`StateTree`、`AIModule`、`MotionWarping`、`Niagara` 等。 |
| 默认地图 | `Config/DefaultEngine.ini` | `GameDefaultMap=/Game/Levels/TestMap.TestMap`，`EditorStartupMap=/Game/Levels/TestMap.TestMap`。 |
| 默认 GameMode | `Config/DefaultEngine.ini` | `GlobalDefaultGameMode=/Game/Games/Mode/BP_AOMainGameMode.BP_AOMainGameMode_C`。 |
| GameInstance | `Config/DefaultEngine.ini` | `GameInstanceClass=/Game/BP_AOGameInstance.BP_AOGameInstance_C`。 |
| Engine 类接线 | `Config/DefaultEngine.ini` | `WorldSettingsClassName=/Script/AegisOdyssey.AOWorldSettings`，`GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient`，`LocalPlayerClassName=/Script/AegisOdyssey.AOLocalPlayer`，`AssetManagerClassName=/Script/AegisOdyssey.AOAssetManager`。 |
| AssetManager 配置 | `Config/DefaultGame.ini` / `Config/DefaultEngine.ini` | 两处都出现 `[/Script/AegisOdyssey.AOAssetManager]`，且 `AOGameData` 路径不完全一致；接用前要以运行时实际读取结果核对。 |
| GameFeature 插件 | `Plugins/GameFeatures/AOGameCore/AOGameCore.uplugin` | `ExplicitlyLoaded=true`，`BuiltInInitialFeatureState=Registered`，内容入口是 `Plugins/GameFeatures/AOGameCore/Content/AOGameCore.uasset`。 |
| 项目知识库 | `Docs/Knowledge` | 已按系统沉淀 `GameplayFramework`、`CombatSystem`、`InventoryEquipment`、`InteractionSystem`、`HarvestSystem`、`CraftingSystem`、`SkillSystem`、`AI`、`StateTreeAI`、`MultiplayerSystem` 等地图。 |

## 运行时链路锚点

### 项目启动与 Experience

优先入口：

- `Source/AegisOdyssey/GameModes/AOGameMode.h`
- `Source/AegisOdyssey/GameModes/AOGameMode.cpp`
- `Source/AegisOdyssey/GameModes/AOGameState.h`
- `Source/AegisOdyssey/GameModes/AOGameState.cpp`
- `Source/AegisOdyssey/GameModes/AOWorldSettings.h`
- `Source/AegisOdyssey/GameModes/AOExperienceDefinition.h`
- `Source/AegisOdyssey/GameModes/AOExperienceManagerComponent.h`
- `Source/AegisOdyssey/GameModes/AOExperienceManagerComponent.cpp`

当前主线是：

```text
DefaultEngine.ini
-> BP_AOMainGameMode / AAOGameMode
-> AAOGameState 创建 ExperienceManager / PlayerSpawning / AIBotCreation 组件
-> AAOGameMode::InitGame 下一帧 HandleGameInitialize
-> AOWorldSettings 提供默认 Experience
-> UAOExperienceManagerComponent::SetCurrentExperience
-> Experience 加载 GameFeatureNames
-> Experience Actions 激活
-> PawnData / Ability / Input / UI 接入角色
```

`AAOGameMode` 的构造函数明确设置 `AAOGameState`、`AAOPlayerController`、`AAOPlayerState`、`AAOCharacter` 和 `AAOHUD`。`GetPawnDataForController()` 先看 `AAOPlayerState::PawnData`，再看当前 Experience 的 `DefaultPawnData`。这条链和 Lyra 风格接近，但 AegisOdyssey 的 ASC 实体不在 PlayerState 上，而是作为组件挂在 `AAOCharacter`。

### AssetManager、GameData 与 PawnData

优先入口：

- `Source/AegisOdyssey/System/AOAssetManager.h`
- `Source/AegisOdyssey/System/AOAssetManager.cpp`
- `Source/AegisOdyssey/System/AOGameData.h`
- `Source/AegisOdyssey/Character/AOPawnData.h`
- `Content/Games/GameData/DA_AOGameData.uasset`
- `Content/Games/PawnData/DA_PawnData.uasset`

`UAOAssetManager` 是项目资产底座，配置类来自 `DefaultEngine.ini` 的 `AssetManagerClassName`。`UAOGameData` 当前主要保留全局物品总表入口和通用 GameplayEffect 软引用。`UAOPawnData` 是角色运行时配置核心，集中声明 `PawnClass`、`AbilitySets`、`DefaultAbility`、`InputConfig`、`DefaultCameraMode`、`Actions`、等级/属性表和 `CraftingRecipeDataTable`。

需要注意：`DefaultGame.ini` 中 `AOGameData=/Game/Games/GameData/DA_AOGameData.DA_AOGameData`，`DefaultEngine.ini` 中还有 `AOGameData=/Game/GameData/AOGameData.AOGameData`。这属于参考项目自身的配置双口径，后续引用时必须以运行时加载证据判断生效路径。

### 角色、PawnData 与 ASC

优先入口：

- `Source/AegisOdyssey/Character/AOCharacter.h`
- `Source/AegisOdyssey/Character/AOCharacter.cpp`
- `Source/AegisOdyssey/Character/AOExtPawnComponent.h`
- `Source/AegisOdyssey/Character/AOExtPawnComponent.cpp`
- `Source/AegisOdyssey/Character/AOHeroComponent.h`
- `Source/AegisOdyssey/Character/AOHeroComponent.cpp`
- `Source/AegisOdyssey/Player/AOPlayerState.h`
- `Source/AegisOdyssey/Player/AOPlayerState.cpp`
- `Source/AegisOdyssey/Player/AOPlayerController.h`

`AAOCharacter` 是主要运行时挂载点，组件包括：

- `UAOAbilitySystem`
- `UAOExtPawnComponent`
- `UAOHeroComponent`
- `UAOCameraComponent`
- `UAOBackPackComponent`
- `UAOQuickBarComponent`
- `UAOWeaponManagerComponent`
- `UAOSkillComponent`
- `UAOSkillSlotInventoryComponent`
- `UAOCraftingComponent`
- `UAOFormalEquipmentManagerComponent`
- `UAOFormalEquipmentSlotInventoryComponent`
- `UAOInteractionSessionComponent` 由 `AAOPlayerController` 持有
- `UAOAIDecisionComponent`
- `UAOPersistentStateTagComponent`
- `UMotionWarpingComponent`

`UAOExtPawnComponent` 负责 Pawn 初始化状态链、复制 `DefaultPawnData`、授予 PawnData 中的 AbilitySets，并在能力系统数据和角色等级就绪后刷新基础属性。`UAOHeroComponent` 负责本地输入初始化、输入事件桥接、相机模式回查和把输入送入 ASC。

`AAOPlayerState` 复制 `PawnData`，并把等级、经验、属性点接口桥接到当前控制的 `AAOCharacter`。从当前源码看，`AAOCharacter` 持有 ASC 组件；`UAOHeroComponent` 初始化时把 PlayerState 作为 ASC owner actor，把 Pawn 作为 avatar。不能把它误读成 Lyra 那种 PlayerState 直接持有 ASC 的结构。

### GAS、战斗与消息

优先入口：

- `Source/AegisOdyssey/AOAbilitySystemGlobals.h`
- `Source/AegisOdyssey/AOAbilitySystemGlobals.cpp`
- `Source/AegisOdyssey/AOAbilityTypes.h`
- `Source/AegisOdyssey/AOCombatResultMessage.h`
- `Source/AegisOdyssey/AOCombatMessageSubsystem.h`
- `Source/AegisOdyssey/AbilitySystem/AOAbilitySystem.h`
- `Source/AegisOdyssey/AbilitySystem/AOAbilitySet.h`
- `Source/AegisOdyssey/AbilitySystem/Abilities/AOGameplayAbility.h`
- `Source/AegisOdyssey/AbilitySystem/Attributes/Core/AOHealthAttributeSet.h`
- `Source/AegisOdyssey/AbilitySystem/Attributes/Combat/AOCombatAttributeSet.h`
- `Source/AegisOdyssey/Character/AOCharacterCombatManagerComponent.h`
- `Source/AegisOdyssey/ExecCal/ExecCal_Damage.cpp`
- `Source/AegisOdyssey/Combat/Cue/*`

项目通过 `DefaultGame.ini` 注册 `AbilitySystemGlobalsClassName=/Script/AegisOdyssey.AOAbilitySystemGlobals`，并在 `AOAbilityTypes` 中提供自定义 EffectContext。Aegis 自己的 `Docs/Knowledge/GameplayFramework/PROJECT_MAP.md` 把正式链路归纳为：

```text
AOAbilitySystemGlobals
-> FAOGameplayEffectContext
-> AOCharacterCombatManagerComponent 写入攻击/武器/命中上下文
-> ExecCal_Damage 计算与补写
-> AOHealthAttributeSet 整理统一战斗消息
-> CombatMessageSubsystem / HUD / FloatingText / WorldHealthBar 消费
```

这条链适合参考“命中采集分散、最终结算统一”的做法。Catfishing 若参考它，应只借鉴上下文收口和消息消费边界，不直接迁移攻击标签、装备类型或属性数值体系。

### 输入与可改键

优先入口：

- `Config/DefaultInput.ini`
- `Source/AegisOdyssey/Input/AOInputConfig.h`
- `Source/AegisOdyssey/Input/AOEnhancedInputComponent.h`
- `Source/AegisOdyssey/Input/AOInputUserSetting.h`
- `Source/AegisOdyssey/Character/AOHeroComponent.h`
- `Source/AegisOdyssey/GameFeatures/GF_AddInputMapping.h`

`DefaultInput.ini` 指定 `DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput`，`DefaultInputComponentClass=/Script/AegisOdyssey.AOEnhancedInputComponent`，并启用 Enhanced Input User Settings。`UAOHeroComponent::InitializePlayerInputInternal()` 从 `PawnData->InputConfig` 读取输入配置，加载 `DefaultInputMappings`，绑定 Ability 输入、移动和视角输入。

这条链最有价值的参考点是：输入标签只是底层桥接语义，技能、战斗或 AI 不应把高层业务判断塞回 Hero 输入层。AegisOdyssey 已经把 `InjectAbilityInputCommand()` 和技能组件的直接执行入口分开，便于 AI / StateTree 复用。

### Inventory、Equipment 与 Items

优先入口：

- `Source/AegisOdyssey/Inventory/AOInventoryStatics.h`
- `Source/AegisOdyssey/Inventory/AOInventoryComponent.h`
- `Source/AegisOdyssey/Inventory/AOBackPackComponent.h`
- `Source/AegisOdyssey/Inventory/AOInventoryItemDefinition.h`
- `Source/AegisOdyssey/Inventory/AOInventoryItemInstance.h`
- `Source/AegisOdyssey/Inventory/Fragments/*`
- `Source/AegisOdyssey/Equipment/AOQuickBarComponent.h`
- `Source/AegisOdyssey/Equipment/AOWeaponManagerComponent.h`
- `Source/AegisOdyssey/Equipment/Formal/AOFormalEquipmentManagerComponent.h`
- `Source/AegisOdyssey/Equipment/Formal/AOFormalEquipmentSlotInventoryComponent.h`
- `Source/AegisOdyssey/Items/*`
- `Docs/Knowledge/InventoryEquipment/PROJECT_MAP.md`

AegisOdyssey 的库存装备链不是一个万能背包，而是几条并列但互相投影的链：

```text
统一入包 -> BackPack
库存右键使用 -> 消耗品或装备请求
QuickBar -> 武器/快捷使用激活态
FormalEquipmentSlotInventory -> FormalEquipmentManager -> ASC 授予
World Item Actor -> Inventory Item Instance / Definition
```

`UAOInventoryStatics::TryAddInventoryBatchToActor()` 是统一入包入口。`UAOInventoryComponent` 用 FastArray 保存槽位和实例，`UAOBackPackComponent` 是当前玩家主接收容器。正式装备栏的运行时真相在 `UAOFormalEquipmentManagerComponent`，槽位库存外壳只给 UI 和库存交换链使用。

这条链对 Catfishing 的 Equipment / Shop / Items / Tank 方向有参考价值：世界对象、背包、快捷栏、长期装备和 UI 投影应保持各自真相边界，不要用一个组件承担所有物品语义。

### Interaction、Container 与会话

优先入口：

- `Source/AegisOdyssey/Interaction/Abilities/AOGameplayAbility_Interact.h`
- `Source/AegisOdyssey/Interaction/InteractableTarget.h`
- `Source/AegisOdyssey/Interaction/InteractionOption.h`
- `Source/AegisOdyssey/Interaction/AOInteractionSessionComponent.h`
- `Source/AegisOdyssey/Interaction/Session/AOInteractionSessionModel.h`
- `Source/AegisOdyssey/Interaction/Session/AOContainerInteractionSessionModel.h`
- `Source/AegisOdyssey/Interaction/Containers/AOChest.h`
- `Source/AegisOdyssey/Interaction/Containers/AOContainerInventoryComponent.h`
- `Source/AegisOdyssey/UI/Common/Inventory/AOContainerUI.h`
- `Docs/Knowledge/InteractionSystem/PROJECT_MAP.md`

当前正式链路可以压成：

```text
Interact Ability
-> IInteractableTarget / InteractionOption
-> Object-side CanExecuteInteraction / ExecuteInteraction
-> AOInteractionSessionComponent
-> AOInteractionSessionModel / AOContainerInteractionSessionModel
-> Object State / Inventory
-> OwnerOnly Session Snapshot
-> UI
```

`AAOChest` 是当前容器样板：它既是世界 Actor，也是 `IInteractableTarget` 和 `IInventoryInterface` 提供者。`UAOInteractionSessionComponent` 持有当前会话和 OwnerOnly 复制状态，并统一处理当前交互对象的 mutation 请求。UI 只消费会话模型和快照，不直接成为容器真相层。

这条链对 Catfishing 的世界鱼容器、箱子、营地交互和“当前 E 命中对象提供的容器”很有参考价值。接用时应保留对象侧真相、会话层快照、UI 观察三层，不要让 Widget 直接拥有世界对象状态。

### Harvest、动作窗口与统一结算

优先入口：

- `Source/AegisOdyssey/Harvest/StateTree/STT_PlayHarvest.h`
- `Source/AegisOdyssey/Harvest/Abilities/GA_Harvest.h`
- `Source/AegisOdyssey/Animation/NotifyState/AOHarvestWindow.h`
- `Source/AegisOdyssey/Harvest/Core/AOHarvestTypes.h`
- `Source/AegisOdyssey/Harvest/System/AOHarvestResolver.h`
- `Source/AegisOdyssey/Harvest/Core/AOHarvestableComponent.h`
- `Source/AegisOdyssey/Harvest/Definition/AOHarvestableDefinition.h`
- `Source/AegisOdyssey/Harvest/Definition/AOHarvestToolDefinition.h`
- `Source/AegisOdyssey/Harvest/Items/AOHarvestToolInstance.h`
- `Source/AegisOdyssey/Harvest/Nodes/Tree/AOHarvestableTree.h`
- `Docs/Knowledge/HarvestSystem/PROJECT_MAP.md`

AegisOdyssey 的采集主链是：

```text
StateTree
-> STT_PlayHarvest
-> GA_Harvest
-> AOHarvestWindow
-> Tool Socket Trace
-> AOHarvestResolver
-> AOHarvestableComponent
-> AOInventoryStatics
-> BackPack
```

这条链的关键价值不是“树和矿怎么实现”，而是它把动作发起、命中窗、工具轨迹、服务端重判定、奖励结算、节点 depleted/respawn 生命周期分开了。Catfishing 的 Fishing 行为可以参考这种“动作窗口进入正式结算”的分层方式，但钓鱼的水域、鱼、鱼护和捕获结果仍应按 Catfishing 自己的领域模型收口。

### Crafting

优先入口：

- `Source/AegisOdyssey/Crafting/Components/AOCraftingComponent.h`
- `Source/AegisOdyssey/Crafting/Data/AOCraftingRecipeTypes.h`
- `Source/AegisOdyssey/Crafting/Data/AOCraftingObservationTypes.h`
- `Source/AegisOdyssey/UI/ViewModel/MVVM_Crafting.h`
- `Source/AegisOdyssey/UI/Widgets/Crafting/*`
- `Docs/Knowledge/CraftingSystem/PROJECT_MAP.md`

AegisOdyssey 的制造链以 `PawnData -> CraftingComponent -> ObservationData -> MVVM -> Widget` 为主。`UAOPawnData` 持有 `CraftingRecipeDataTable`，`UAOCraftingComponent` 是运行时真相层，UI/MVVM 只消费观察结果。

这条链可参考“配方来源、运行时队列、观察数据和 UI 消费分离”的做法。当前 Catfishing 若没有制造系统，不应为未来可能性提前迁移这套结构。

### SkillSystem

优先入口：

- `Source/AegisOdyssey/SkillSystem/Core/AOSkillDefinition.h`
- `Source/AegisOdyssey/SkillSystem/Core/AOSkillInstance.h`
- `Source/AegisOdyssey/SkillSystem/Components/AOSkillComponent.h`
- `Source/AegisOdyssey/SkillSystem/Components/AOSkillSlotInventoryComponent.h`
- `Source/AegisOdyssey/SkillSystem/Core/AOSkillGameplayAbility.h`
- `Source/AegisOdyssey/SkillSystem/Core/AOSkillExecutionDefinition.h`
- `Source/AegisOdyssey/SkillSystem/Execution/Definitions/*`
- `Source/AegisOdyssey/SkillSystem/Execution/AbilityBases/*`
- `Source/AegisOdyssey/SkillSystem/Abilities/*`
- `Source/AegisOdyssey/Inventory/Fragments/AOFragment_SkillSource.h`
- `Docs/Knowledge/SkillSystem/PROJECT_MAP.md`

当前技能主链是：

```text
SkillDefinition
-> SkillSource
-> SkillInstance
-> SkillSlot
-> SkillComponent
-> ASC
-> SkillGameplayAbility
-> ExecutionDefinition
```

`UAOSkillComponent` 是运行时总入口，负责技能实例、槽位装配、ASC 授予、输入订阅和 UI 观察快照。`UAOSkillSlotInventoryComponent` 是库存投影适配层，不是第二套技能真相。`UAOSkillGameplayAbility` 从 `SourceObject` 反查实例、定义和执行对象。

这条链适合参考“定义、来源、实例、槽位、执行”分层。Catfishing 若以后给装备、鱼饵、鱼竿或角色成长接技能/Ability，不应直接复制 Aegis 的技能系统，而应先判断是否真的需要实例层和槽位层。

### UI、CommonUI 与 MVVM

优先入口：

- `Source/AegisOdyssey/UI/AOHUD.h`
- `Source/AegisOdyssey/UI/AOHUDViewModelComponent.h`
- `Source/AegisOdyssey/UI/SubSystem/AOUIManagerSubSystem.h`
- `Source/AegisOdyssey/UI/AOHUDLayout.h`
- `Source/AegisOdyssey/UI/ViewModel/*`
- `Source/AegisOdyssey/UI/Widgets/Inventory/*`
- `Source/AegisOdyssey/UI/Widgets/FormalEquipment/*`
- `Source/AegisOdyssey/UI/Widgets/Skill/*`
- `Source/AegisOdyssey/UI/Common/Inventory/*`
- `Plugins\CommonGame\Source\Public\GameUIManagerSubsystem.h`
- `Plugins\CommonGame\Source\Public\PrimaryGameLayout.h`
- `Plugins\UIExtension\Source\Public\UIExtensionSystem.h`
- `Content/Games/UI/BP_AOGameUIPolicy.uasset`
- `Content/Games/UI/MainUI/WBP_GameMainHUD.uasset`

`UAOUIManagerSubSystem` 继承 `UGameUIManagerSubsystem`，默认 UI Policy 来自 `DefaultGame.ini` 的 `DefaultUIPolicyClass=/Game/Games/UI/BP_AoGameUIPolicy.BP_AOGameUIPolicy_C`。`AAOHUD` 持有 `UAOHUDViewModelComponent`，后者集中创建和桥接 HUD 子 ViewModel，包括战斗资源、本地战斗状态、战斗反馈、目标血条、制造、物品提示和 AI 调试。

这条链对 Catfishing 的 UIReach 模块有参考价值：UI 应观察 ViewModel / ViewState，不直接持有玩法真相；HUD 层可以做本地过滤和展示桥接，但不应重判战斗、库存或交互真相。

### AI、StateTree 与调试

优先入口：

- `Source/AegisOdyssey/StateTree/AOStateTreeComponentBase.h`
- `Source/AegisOdyssey/StateTree/CombatStateTree/AOCombatStateTree.h`
- `Source/AegisOdyssey/StateTree/AI/Enemies/AOAILogicStateTreeComponentBase.h`
- `Source/AegisOdyssey/Character/Enemies/AI/Decision/AOAIDecisionComponent.h`
- `Source/AegisOdyssey/Character/Enemies/AI/Decision/AOAIDecisionTypes.h`
- `Source/AegisOdyssey/Character/Enemies/AI/Decision/AOAIDecisionProfile.h`
- `Source/AegisOdyssey/Character/Enemies/AI/StateTree/Evaluator/STE_UpdateCurrentTarget.cpp`
- `Source/AegisOdyssey/Character/Enemies/AI/StateTree/Evaluator/STE_UpdateCombatDecision.cpp`
- `Source/AegisOdyssey/Character/Enemies/AI/StateTree/Tasks/STT_CommitAIDecisionIntent.cpp`
- `Source/AegisOdyssey/Player/AAOAIPlayerBotController.h`
- `Source/AegisOdyssey/System/AOGlobalConsoleCommands.cpp`
- `Docs/Knowledge/AI/PROJECT_MAP.md`
- `Docs/Knowledge/StateTreeAI/PROJECT_MAP.md`

AI 知识库把当前结构拆成事实层、决策层和执行层。`UAOAIDecisionComponent` 持有运行时决策状态，StateTree Evaluator 更新目标/距离/意图分数，Condition 和 Task 消费结果并回写执行状态。调试入口包括 `AegisOdyssey.AI.SetDecisionTreeEnabled` 和 `AegisOdyssey.AI.SetDebugPanelEnabled`。

这条链对 Catfishing 可作为“AI 事实和 StateTree 行为分离”的参考。除非 Catfishing 明确采用 StateTree AI，否则不要把 Aegis 的 AI 决策 profile 和意图标签搬进当前项目。

### Online、CommonUser 与 Dedicated Server 边界

优先入口：

- `Docs/Knowledge/MultiplayerSystem/PROJECT_MAP.md`
- `Plugins/CommonUser/Source/CommonUser/Public/CommonSessionSubsystem.h`
- `Plugins/CommonUser/Source/CommonUser/Public/CommonUserSubsystem.h`
- `Plugins\CommonGame\Source\Public\CommonGameInstance.h`
- `Source/AegisOdyssey/System/AOGameInstance.h`
- `Source/AegisOdyssey/System/SubSystem/AOGameInstanceSubsystem.h`
- `Source/AegisOdyssey/AegisOdyssey.Build.cs`
- `Source/AegisOdyssey.Target.cs`
- `Source/AegisOdysseyEditor.Target.cs`

AegisOdyssey 携带 `CommonUser` / `CommonSessionSubsystem` / `CommonGameInstance` 等联机底座，但当前没有发现 `AegisOdysseyServer.Target.cs`。`Docs/Knowledge/MultiplayerSystem/PROJECT_MAP.md` 也明确把 Dedicated Server 工程基线列为未立住的方向。主模块 `Build.cs` 里 `OnlineSubsystem` 仍是注释状态，且 Private 依赖包含 `UnrealEd`，这对正式 Server 构建是高风险入口。

Catfishing 若参考 Aegis 的联机方向，只能参考“不要绕开 CommonUser/CommonSession 另造会话底座”和“联机成功后进入 GameMode/Experience 世界主链”这类设计判断，不能把 Aegis 当成已完成正式多人或 Dedicated Server 样板。

## Catfishing 可参考口径

| Catfishing 方向 | 可参考的 AegisOdyssey 入口 | 参考边界 |
| --- | --- | --- |
| `Equipment / Shop` | `Inventory`、`Equipment`、`Items`、`AOInventoryStatics`、`AOFormalEquipmentManagerComponent` | 参考统一入包、快捷栏、长期装备、库存投影分层；不要迁移 Aegis 的装备槽、物品 ID 或属性体系。 |
| `Items / Tank / Camp` | `Interaction`、`AOInteractionSessionComponent`、`AOContainerInteractionSessionModel`、`AOChest` | 参考对象侧真相、会话快照、OwnerOnly 同步和 UI 消费边界；不要让 Catfishing 的鱼护/箱子直接变成 Aegis 容器类。 |
| `FishingPlayerEntry` | `AOHeroComponent`、`AOEnhancedInputComponent`、`AOInputConfig` | 参考输入标签和 Ability 输入桥接；Catfishing 的 Fishing 命令链、E 交互和正式输入资产仍以本项目为准。 |
| `UIReach` | `AOUIManagerSubSystem`、`AOHUDViewModelComponent`、`UI/ViewModel`、`UI/Widgets` | 参考 CommonUI + MVVM 观察边界；Catfishing 已裁定的 UI Subsystem、只读 View DTO 和正式 WBP 口径不被 Aegis 覆盖。 |
| `CharacterGrowthCondition` | `AOCharacter`、`AOExtPawnComponent`、`AOPawnData`、`AbilitySystem/Attributes` | 参考角色承载 ASC、PawnData 驱动能力和属性刷新；Catfishing 的身体状态、Condition、吃鱼成长和恢复规则仍按本项目领域模型。 |
| `RunEnvironmentSocial` | `GameModes`、`ExperienceManager`、`StateTree`、`AI` | 可参考世界主链和 AI 事实/决策/执行分层；不要把 Aegis 的动作 RPG 战斗 AI 直接套到 Catfishing 的 Run/Environment/Social。 |
| `DataWorldProfileAlbum` | `AOGameData`、`AOPawnData`、`InventoryItemDefinition`、`SkillDefinition` | 可参考全局数据、Pawn 数据、定义/实例分离；Aegis 没有确认的正式 Save/Profile 主链，不能作为 Profile 持久化样板。 |
| `FrontendOnline` | `CommonUser`、`CommonSessionSubsystem`、`CommonGameInstance`、`MultiplayerSystem` 文档 | 只参考会话底座和 DS 风险拆分；Aegis 没有 Server Target，不能证明正式 Steam 联机闭环。 |
| 项目知识库 | `Docs/Knowledge/*/PROJECT_MAP.md` | 可参考“按系统建立地图/决策/已知问题”的文档结构；不要复制其历史文档或任务清单。 |

## 最短阅读顺序

1. `D:\UnreaProjects\AegisOdyssey\README.md`
2. `D:\UnreaProjects\AegisOdyssey\AegisOdyssey.uproject`
3. `D:\UnreaProjects\AegisOdyssey\Config\DefaultEngine.ini`
4. `D:\UnreaProjects\AegisOdyssey\Config\DefaultGame.ini`
5. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\AegisOdyssey.Build.cs`
6. `D:\UnreaProjects\AegisOdyssey\Docs\Knowledge\GameplayFramework\PROJECT_MAP.md`
7. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\GameModes\AOGameMode.*`
8. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\GameModes\AOExperienceManagerComponent.*`
9. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Character\AOCharacter.*`
10. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Character\AOExtPawnComponent.*`
11. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Character\AOHeroComponent.*`
12. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Character\AOPawnData.*`
13. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\AbilitySystem\AOAbilitySystem.*`
14. `D:\UnreaProjects\AegisOdyssey\Source\AegisOdyssey\Input\AOInputConfig.*`
15. 按任务进入 `Docs\Knowledge\InventoryEquipment`、`InteractionSystem`、`HarvestSystem`、`CraftingSystem`、`SkillSystem`、`AI`、`StateTreeAI`、`MultiplayerSystem`。

## 容易误读的点

1. AegisOdyssey 不是 Catfishing 的依赖。当前 Catfishing 仓库没有搜到 AegisOdyssey 直接引用，不能把 Aegis 路径或类名写进 Catfishing 运行事实。
2. AegisOdyssey 有 Lyra 风格结构，但不是原版 Lyra。`AOExperienceDefinition`、`AOPawnData`、`AOAbilitySet`、`GameFeature` 等概念可类比 Lyra，具体实现和职责已经有 Aegis 自己的改动。
3. `AAOCharacter` 持有 ASC 组件，`AAOPlayerState` 复制 `PawnData` 并桥接角色等级；不要误读成 PlayerState 直接持有 ASC。
4. GameFeature 插件存在不等于当前世界已经激活。`AOGameCore` 是 `Registered` 且 `ExplicitlyLoaded`，实际激活仍要看 Experience 的 `GameFeatureNames` 和加载链。
5. `DefaultGame.ini` 和 `DefaultEngine.ini` 中 `AOAssetManager` 配置有路径双口径。接用任何数据路径前必须运行时核对。
6. AegisOdyssey 自带 `Docs/Knowledge`，但知识文档不能覆盖源码和配置事实。发现冲突时优先回到真实代码、配置和资产引用。
7. `.uasset` / `.umap` 只在本图中作为路径锚点，本文没有解析其内部字段、蓝图继承、组件实例或引用关系。
8. 多人联机方向不能当成完成样板：当前没有发现 `AegisOdysseyServer.Target.cs`，`Build.cs` 还带 `UnrealEd` 依赖，`OnlineSubsystem` 在主模块规则中仍是注释状态。
9. AegisOdyssey 的 Harvest、Skill、Crafting 很适合参考分层方式，但 Catfishing 的钓鱼、鱼类、鱼护、营地和长期进度仍需要按本项目领域模型重新命名和收口。
10. `Docs/Knowledge/ReferenceStudies` 里有 Lyra、GASP、动画等参考研究；它们是 Aegis 的参考项目上下文，不应经由 Aegis 间接覆盖 Catfishing 的 Lyra 地图或 UE 系统源码事实。

## 维护规则

- 本地图只维护 AegisOdyssey 作为 Catfishing 外部参考项目的导航和边界，不写 Aegis 任务进度、历史聊天或一次性排查记录。
- AegisOdyssey 源码、配置、插件或 `Docs/Knowledge` 发生实际变化后，再更新本图；不要靠记忆补写。
- 新增可参考方向时，先写清对应 Catfishing 模块、Aegis 入口和不可照搬的边界。
- 若要深入某个系统，优先在 Aegis 自己的 `Docs/Knowledge/<System>/PROJECT_MAP.md` 继续读，再回源码核对。
- 若发现 Aegis 的文档和源码冲突，本文只记录“冲突存在”和可核对路径，不替 Aegis 裁决最终真相。
- 不把 Aegis 的 `.uasset` 内部字段写成已验证事实，除非后续通过编辑器、资产导出或专用工具重新核验。
