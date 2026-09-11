# DataAsset 字段说明手册

对应代码状态：2026-09-04；2026-09-11 更新身体意图耗体及相关搏斗字段。给配数值/建资产的人看：每个 DataAsset 类型的字段含义、校验规则、注册方法。

## 0. 所有 DataAsset 共同的规矩

1. **必须注册才生效**：代码从不扫描目录，只认 `Config/DefaultGame.ini` 里显式列出的资产（各类型注册键见下表）。新建 DA 忘了注册 = 游戏里等于不存在。
2. **改 ini 必须重启 Editor**（DeveloperSettings 只在启动读取）；改 DA 资产本身在下一次会话/播种时生效，不需要重启。
3. **`bEnableRuntimeDefinition` 显式启用 gate**：大部分定义类默认关闭，防止占位资产被正式流程采用。建完资产记得勾上。
4. **ID 唯一**：同类型清单里出现两个相同 ID → 查询返回空（fail-closed），相关功能整个失效，不是"随便选一个"。
5. **校验是整体的**：任一必填字段不合法，整个定义被判"未就绪"而被跳过（症状往往是 InvalidPayload / DependencyUnavailable / No eligible fish 这类下游错误，日志过滤 `LogCat` 前缀可定位）。
6. **PIE 运行中保存会被静默拒绝**（返回成功但不落盘）——停 PIE 再保存。

| 类型 | 注册位置（DefaultGame.ini 的 section / 键） | 现有资产 |
|---|---|---|
| 猫种类 CatCharacterDefinition | `[CatAbilitySettings]` `+CharacterDefinitions=` | `/Game/Catfishing/Data/Character/Cat_Default` |
| 装备 CatEquipmentDefinition | `[CatInventorySettings]` `+Definitions=(DefinitionId=...,ItemDefinition=...)` | `/Game/Catfishing/Data/Equipment/Equip_*` |
| 鱼种 CatFishDefinition | `[CatFishCatalogSettings]` `+Definitions=` | `/Game/Catfishing/Data/Fish/Fish_*`；Showcase2 的 `River` 水域直接使用正式目录 |
| 咬钩性格 CatBitePersonalityDefinition | `[CatFishingSettings]` `+BitePersonalities=` | `/Game/Catfishing/Data/Fish/Bite_*` |
| 搏斗性格 CatFightPersonalityDefinition | `[CatFishingSettings]` `+FightPersonalities=` | `/Game/Catfishing/Data/Fish/Fight_*` |
| 搏斗平衡 CatFishingFightBalanceDefinition | `[CatFishingSettings]` `FightBalanceDefinition=` | `/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default` |
| AbilitySet / InputConfig | `[CatAbilitySettings]` `DefaultAbilitySet=` / `AbilityInputConfig=` | DA_CatAbilitySet_Default 等 |
| 曲线 CurveFloat | 被上述 DA/设置按字段引用 | Curve_ChumSaturation 等 3 条 |

---

## 1. 猫种类：`UCatCharacterDefinition`（DA_Cat_*）

按种类差异化猫的初始属性与搏斗数值。角色蓝图 Details 里把 `Cat Definition Id` 填成某个 DA 的 ID 即选用该种类；留 **None** 则先使用 `CatAbilitySettings.DefaultCharacterDefinitionId`，该配置也留空时才回退全局三项初值。数值只在**属性播种**和**搏斗开始**两个时刻被冻结，运行中改资产不影响进行中的搏斗。当前正式默认资产是 `/Game/Catfishing/Data/Character/Cat_Default`，稳定 ID 为 `DefaultCat`。

| 字段 | 含义 | 校验 |
|---|---|---|
| CatDefinitionId | 种类稳定 ID，角色用它选种类 | 必填、清单内唯一 |
| DisplayName | 表现用显示名 | 不参与数值裁决 |
| InitialPoison | 初始中毒值 | ≥0 |
| FishingStrength | **猫力量**（正体力期间完整生效，归零停止主动出力；不按剩余体力比例衰减） | >0 |
| FightStaminaMaximum | **猫搏斗体力上限**（规格 4.3 消耗与松线喘息回复的基线） | >0 |
| bEnableRuntimeDefinition | 显式启用 gate | 必须 True |

⚠️ 角色指定了 ID 但定义缺失/未就绪时**不会悄悄换成全局值**——属性播种直接失败并打 `initial_attributes_unresolved` Warning，钓鱼链整体不可用，方便第一时间发现配错。

### 1.1 全局搏斗平衡：`UCatFishingFightBalanceDefinition`

正式资产是 `/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default`，稳定 ID 为 `DefaultFishingFightBalance`。在 Content Browser 打开后，Details 面板按“力量与运动 / 体力 / 鱼线与张力”显示中文字段名；修改会从下一次选鱼/搏斗开始生效，已经开始的搏斗继续使用冻结快照。

| 编辑器字段 | C++ 字段 | 当前值 | 含义 |
|---|---|---:|---|
| 每公斤力量 | StrengthPerKilogram | 10 | 实际鱼重→鱼力量；也用于猫基础力量反推等效质量 |
| 每点力量推力（牛顿） | ForcePerStrengthNewtons | 1 N | 把玩法力量换算成共同线张力求解使用的力 |
| 单猫系统质量 | CatBodyMassKilograms | 5 kg | 猫端有限加速度和线约束求解使用的质量 |
| 力竭鱼回收辅助力（牛顿） | ExhaustedReelForceNewtons | 200 N | 鱼力竭后收近阶段使用的辅助回收力 |
| 猫力竭拖行辅助加速度 | ExhaustedCatTowAccelerationCentimetersPerSecondSquared | 300 cm/s² | 主猫零体力被拖行时的沿线辅助加速度 |
| 满表现张力（牛顿） | DisplayTensionNewtons | 50 N | UI 和竿体弯曲表现的张力归一化基准 |
| 收线速度 | ReelSpeedCentimetersPerSecond | 80 cm/s | 左键收线意图速度上限 |
| 猫力竭后鱼外冲速度倍率 | ExhaustedCatEscapeSpeedMultiplier | 2 | 主位体力为零且没有助手实际出力时，按鱼两档游速中的较大值乘此倍率持续外冲；有限值且至少为 1 |
| 猫做功体力消耗系数 | CatStaminaCostPerStrengthCentimeter | 默认 0.002 | 收线每标准力量·cm 已完成正功的单价；身体意图缺失使用下述独立配置 |
| 猫转杆每标准转矩弧度体力系数 | CatRodStaminaCostPerStrengthRadian | 默认 0.03 | 真实转角按主位主动转矩比例加权后计价，不使用最大转速虚拟弧长 |
| 猫无负载动作成本倍率 | CatUnloadedWorkMultiplier | 默认 0.15 | 猫实际做功的基础价格，与负载价格相加 |
| 猫满用力每秒支撑耗体 | CatSupportStaminaPerSecond | 默认 2/s | 支撑按用力/负载比例平方和持续时间结算；共享支撑与转杆支撑取较高者 |
| 鱼每米未完成意图耗体 | FishStaminaPerUnfulfilledMeter | 默认 5/3 点/m | 沿主动意图未完成米数的独立价格；倒退增加缺失，侧移不抵扣，无额外张力/角度倍率 |
| 旧鱼每厘米价格 | FishStaminaCostPerStrengthCentimeter | 已停用 | 仅保留资产字段身份，不参与现行计算，也不换算新价格 |
| 猫移动体力倍率 | CatMovementStaminaMultiplier | 默认 1 | 主控身体意图缺失每米价格的无量纲倍率；不以张力或鱼线方向额外门控；零意图无此项费用 |
| 猫收线体力倍率 | CatReelStaminaMultiplier | 默认 1 | 原求解器本步卷线量的正功费用 |
| 猫转杆体力倍率 | CatRodStaminaMultiplier | 默认 1 | 主位实际转杆正功及其时间支撑的倍率 |
| 猫持竿体力倍率 | CatHoldStaminaMultiplier | 默认 1 | 共享沿线支撑费用倍率；实际做功费用与支撑费用分别计算 |
| 猫负载体力倍率 | CatLoadStaminaMultiplier | 默认 1 | 猫实际做功乘 `(无负载动作倍率 + 自身归一化负载 × 本倍率)` |
| 旧鱼负载体力倍率 | FishLoadStaminaMultiplier | 已停用 | 仅保留资产字段身份，不参与意图缺失耗体 |
| 旧鱼受阻努力折算倍率 | IsometricEffortMultiplier | 已停用 | 仅保留资产字段身份；现行缺失距离不另乘受阻倍率 |
| 放线体力恢复速度 | SlackStaminaRegenPerSecond | 3/s | 正常右键且身体/杆完全卸载时主控的恢复速度；任一抓握/推挤/冲量负载包括相互抵消均阻止恢复；零体力强制拖拽除外 |
| 鱼力竭吸附阈值 | FishExhaustionThreshold | 0.5 | 本步产生正的鱼对抗耗体后，剩余绝对体力不高于该值才吸附归零；零耗体不触发 |
| 低体力休息触发比例/时长倍率 | LowStaminaRestThreshold/Multiplier | 0.5 / 1.5 | 低体力鱼延长平静期 |
| 逃脱松线余量 | EscapeSlackCentimeters | 100 cm | 无人持竿时超过最大线长后的逃脱余量 |
| 僵持鱼竿磨损系数 | StalemateRodWearPerFishStrength | 0.1 | 按鱼沿线向外负载连续缩放的鱼竿磨损，写回同一装备实例；几何张力不能替代方向负载 |
| 持竿最低杠杆倍率 | HeldRodMinimumLeverageMultiplier | 0.4 | 竿身偏线时保留的最低有效力量 |
| 最大约束修正速度 | MaximumFishConstraintCorrectionSpeedCentimetersPerSecond | 160 cm/s | 鱼端修正及猫端牵引目标的安全上限 |

`DefaultGame.ini` 只保存 `FightBalanceDefinition` 资产引用，上述数值全部来自正式资产。资产缺失、未勾“启用正式运行”或任一字段非法时，Fishing runtime 保持 fail-closed。

现行费用分为身体意图缺失、收线/转杆正功、主控持竿支撑。身体受阻按未完成米数收费，完成进展不收此费；收线按实际完成量，转杆按真实主动转角，主控支撑仍按相对用力平方和时间去重。`CatLoadStaminaMultiplier` 只作用于原收线/转杆正功的负载部分，不能关闭身体意图缺失费。鱼使用独立米价，不读取旧每厘米/每秒价或负载倍率。正常放线保留原鱼/主控免耗，恢复还须身体与杆完全卸载；辅助始终按自身身体状态结算。配置身份及创建脚本保持，已有合法资产不自动覆盖。

### 1.2 身体意图与恢复：`UCatPhysicalEffortSettings`

这是 Project Settings 的 Config=Game 设置，节名 `[/Script/Catfishing.CatPhysicalEffortSettings]`，不是新的 DataAsset；使用独立默认，不迁用旧做功数值。

| 字段 | 默认与单位 | 含义 |
| --- | --- | --- |
| StaminaPerUnfulfilledMeter | 2 点/m | 身体沿意图缺失距离的价格；主控另乘原移动倍率 |
| SupportReferenceSpeedCmS | 100 cm/s | 辅助满用力站稳的等效意图速度，与走路速度独立 |
| RecoveryDelaySeconds | 2 s | 辅助完全卸载且不主动用力的连续等待；重新受力清零 |
| RecoveryPerSecond | 5 点/s | 辅助等待结束后的本人恢复速度 |
| ExhaustionResumeRatio | 0.2 | 辅助耗尽后恢复到最大体力的此比例，才开放辅助出力/抓握 |

价格、速度、等待均需有限且非负，恢复比例在 (0,1]。辅助耗尽时主动地面力量为零，完全卸载之前不恢复；主控仍使用上述原放线恢复入口。实际公式、网络权威和验证见 `FishFightImplementationGuide_zh-CN.md` 顶部。

## 2. 装备/道具：`UCatEquipmentDefinition` 与正式库存目录

`Equip_*` 资产可复用为库存定义。通用身份仍由 `UCatEquipmentDefinition` 保存，五组专属静态能力改由 Fragments 表达：鱼竿用 `UCatEquipmentFragment_Rod`，鱼饵用 `..._Bait`，鱼漂用 `..._Float`，抄网用 `..._Scoop`，窝料用 `..._Chum`。运行入口只查自己需要的片段并校验其 `IsRuntimeReady()`，不再通过其他领域字段的零值推测物品用途。

**共同字段**：`EquipmentDefinitionId`(唯一 ID，也是库存目录 ID) · `LoadoutSlotId`(Rod/Bait/Float/ScoopNet 四个钓具选择槽位物品必填，非选择型道具不填) · `RequiredUnlockId`(解锁门槛,None=不设) · `UseActorClass`(部署型物品 Use 到世界时生成的 Actor 类；鱼竿填 BP_CatFishingRodActor，其他部署物品填自己的 Actor) · `bRunConsumable`(是否一局内数量物：普通/特殊鱼饵、窝料和片段型耗材为 True，工具和部署型物品为 False) · `FunctionalRouteId`(装备运行目录必填,常规填 Route_Standard；只进入库存目录的片段型物品不靠它表达用途) · `bEnableRuntimeDefinition`(gate) · `PreferredInstanceType`（可显式指定 `UCatEquipmentInventoryItemInstance` 的子类；不填时使用默认装备实例，填入非装备实例子类会使定义不就绪）。

**Rod（鱼竿）Fragment：`UCatEquipmentFragment_Rod`**：
| 字段 | 含义 | 规格对应 |
|---|---|---|
| MaximumRodDurability | **鱼竿耐久上限**；新鱼竿使用该上限，同一装备实例的剩余耐久跨场累计，归零即损坏；重新抛竿不恢复 | `StarterRodT1` 当前基线 150；其他档以正式资产为准 |
| MaximumLineLengthCentimeters | 线长上限 cm | 放尽绷紧强制按拖判定 |
| BaseDurabilityWearPerSecond / HighTensionWearMultiplier | 基础磨损/绷紧磨损倍率 | ≥0 / ≥1 |
| RodTipLocal/StandLocal/GripLocalTransform | 竿尖(抛竿原点+鱼线起点)/操作站位/握持 三个权威锚点 | 表现蓝图只读不写 |

**Bait（鱼饵）Fragment：`UCatEquipmentFragment_Bait`**：`bSpecialBait`（仅饵料的特殊身份标记） · `BiteRateMultiplier`(>0,咬钩率倍率) · `MinimumBiteDelayMultiplier`(>0,最短咬钩延迟倍率)
**Float（浮漂）Fragment：`UCatEquipmentFragment_Float`**：`MaximumCastDistanceCentimeters`(>0,最大抛竿距离) · `CastErrorStandardDeviationCentimeters` / `MaximumCastErrorRadiusCentimeters`(落点误差 σ/上限,σ≤上限) · `BiteSignalStability`(0~1,咬钩信号稳定度)

当前正式射程：羽毛 1000 cm、毛线球 1500 cm、铃铛 2000 cm。实际可抛距离取浮漂射程与鱼竿 `MaximumLineLengthCentimeters` 的较小值，并从竿尖量至落点；入门竿线长 1500 cm。原 300/500/700 cm 配置由 `Scripts/update_fishing_cast_ranges.py` 定向迁移，其他装备字段保留。
**ScoopNet（抄网）Fragment：`UCatEquipmentFragment_Scoop`**：`ScoopReachCentimeters`(>0,**抄手沿 Character 面朝正前方发射的水平线段长度**,语义="网杆多长")。方向取 `Character Actor Forward`，不读取 `Controller/Camera` 朝向。与鱼定义里的 `ScoopTargetRadiusCentimeters`(圆半径)配对构成抄网判定：**俯视投影下线段∩圆**即够得着。实际生效长度取 `min(本值, UCatFishingSettings::ScoopReachCentimeters)`——全局那个是上限闸门。高度差另由 `UCatFishingSettings::MaximumScoopVerticalDeltaCentimeters` 单独限制,判定本身完全不看俯仰角。`StarterScoopNet` 只是正式库存目录中的抄网定义；装配和抄取必须引用已经进入正式库存的实例。
**Chum（窝料）Fragment：`UCatEquipmentFragment_Chum`**：`bRunConsumable` 必须 True，核心在片段的 `ChumInfluence` 结构：

| ChumInfluence 字段 | 含义 | 校验 |
|---|---|---|
| RadiusCentimeters | 窝点半径 cm | >0 |
| DurationSeconds | 窝点持续秒 | >0 |
| BaseContribution (Fishy/Fragrant/Fermented) | 腥/香/酵三轴基础贡献量,与鱼的 ChumPreference 点积决定诱鱼偏好 | 三轴合法 |
| DistanceFalloffCurve | 距离衰减曲线(输入0=中心→1=边缘,输出≥0,v(0)>0) | 必填 |
| TimeFalloffCurve | 时间衰减曲线(输入0=刚投→1=到期) | 必填 |
| MaximumQuantityPerPlacement | 单次投放最多消耗份数 | >0 |
| PresentationId / PresentationClass | 表现语义 ID / 表现 Actor 类 | 可空 |

**Herb（草药恢复）**：草药属于 `CatInventorySettings` 正式库存目录，并在定义 `Fragments` 里添加 `UCatHerbRecoveryItemFragment`；施药入口由目标 `UCatConditionComponent::UseHerbOnCharacterFromAuthority` 执行，它只按正式库存实例和这个片段复核能否扣量，恢复数值与距离来自 `CatConditionSettings`。

## 3. 鱼种：`UCatFishDefinition`（DA_Fish_*）

| 字段组 | 字段 | 含义 |
|---|---|---|
| 身份 | FishDefinitionId | 唯一 ID,图鉴/日志/实物鱼都引用它 |
| 表现 | PresentationDefinition | **唯一表现入口**，直接引用本鱼 `FishPresentation_*`；水中 Encounter、落地 Pickup 和嘴叼状态都沿这条引用解析，不得按 FishDefinitionId 再建 Mesh/ABP 映射表 |
| 体型 | BodyClass | Standard=单人可搏 / Giant=可多人协作；抄网成功后世界鱼由首个合法抄手叼走；不能 Unknown |
| 出没 | RegionIds / TimeOfDay / Weather | 可出现的水域 ID/时段(夜晚永不进选择器)/天气;**空数组=未配置=不出现** |
| 稀有 | RarityTierId / SpawnWeight | 稀有度轴 ID / 选择正权重(稀有度由数据表达,代码无硬编码档位) |
| 体重 | Minimum/MaximumWeightKilograms | 服务器在区间内抽取真实重量;min≤max |
| 搏斗 | FishStrength | 二进制资产读取字段；运行时忽略，重存资产后可逐步清空 |
| 搏斗 | FishFightStamina | **鱼搏斗体力**(短周期,与稀有度独立);>0 |
| 搏斗 | MinimumFightParticipants | 需要的协作人数;单人局过滤 >1 的定义 |
| 抄网 | **ScoopTargetRadiusCentimeters** | **这条鱼的可捞圆圈半径 cm**,圆心随鱼移动;抄手向正前方发射长度=抄网 ScoopReach 的水平线段,与圆相交即够得着。语义="这条鱼有多好捞"——小鱼小圈、巨鱼大圈以降低多人抢抄难度。**必须 >0,为 0 时服务器一律拒绝抢抄** |
| 性格 | BitePersonalityId / FightPersonalityId | 引用下面两类模板的 ID |
| 偏好 | ChumPreference (三轴) | 与窝点三轴点积→经饱和曲线→选择权重放大(封顶 MaximumChumModifier) |
| 偏好 | BaitWeightMultipliers | 特定鱼饵 ID→权重倍率;普通饵不用列 |
| 食用 | FoodSafety / EatingExperience / PoisonIncrease | Safe/Toxic 结论 + 吃后体验/增毒量(Safe 必须 0 毒) |
| 其他 | OfferingPoints / CaptureImprintEventId / bTankDisplayEligible | 供品点数 / 捕获成像事件 / 可否入展示鱼缸 |
| gate | bEnableRuntimeDefinition | 必须 True |

`FishPresentation_*` 是普通 `UCatFishPresentationDefinition` DataAsset，不单独注册到鱼目录。它配置本鱼的 `SkeletalMesh`、继承 `UCatFishAnimInstance` 的子 AnimBP、Calm/Struggle/Exhausted/Landed 四类动画、参考重量与缩放范围，以及 Encounter/Landed/Carried 三套 Mesh 局部 Transform。Mesh 自身持有 Skeleton；子 AnimBP 和四类动画必须与该 Skeleton 兼容。所有子 AnimBP 继承无 Target Skeleton 的 `ABPT_CatFishBase`，只覆盖三个 Sequence Player，美术资源变化不会复制游速公式与状态机。

首轮正式鱼的近似美术映射如下；这是可替换的内容选择，不是运行时代码分支：

| 正式鱼 | 资源包物种 | 正式鱼 | 资源包物种 |
|---|---|---|---|
| RiverPattern | koi | LittleSilver | mackerel |
| LittleColor | clownfish | ForestLongtail | barracuda |
| SilvermoonTrout | trout | LakeGiantShadow | arapaima |
| Petal | discus_v2 | Windbell | butterfly_fish |
| Salted | atlantic_cod | Stinky | carp |
| Blackfish | black_redeye_fish | Loach | oreochromis |
| EstuaryBass | peacock_bass | Puffer | frontosa |
| ElectricEel | electric_catfish | Pike | pike |

鱼种没有固定“低级/中级”战斗标签。服务器为每个候选鱼种按本次机会种子和稳定鱼 ID 独立抽取个体重量，令 `FishStrength=WeightKilograms×StrengthPerKilogram`；其中换算系数来自当前正式搏斗平衡资产。该重量和力量一旦选中便冻结，选择、搏斗和 HUD 共同读取这份冻结结果。令力量比 `S=FishStrength/玩家合计力量`、体力比 `T=FishFightStamina/玩家合计搏斗体力`，目录按 `max(S, 2ST/(S+T))` 计算当前上下文里的连续挑战度：力量比是危险下限，力量/体力调和均值只在两项都足够时抬高挑战度，避免力量极低但体力很高的鱼被错误归入势均力敌带。`≤ ComfortChallengeMaximumRatio` 为轻松带，之后到 `MatchedChallengeMaximumRatio` 为势均力敌带，再到 `MaximumChallengeRatio` 为高风险带；超过安全上限才不进入池。系统先按三条 `*ChallengeBandWeight` 在当前有候选的难度带之间抽取，再用 `SpawnWeight × 窝料倍率 × 鱼饵倍率 × 连续挑战倍率` 在带内选鱼。某个目标带没有鱼时会在其余有候选的带之间重新归一化；只有生态条件、协作人数或安全上限后确实没有鱼才会空钩。

## 4. 咬钩性格：`UCatBitePersonalityDefinition`（DA_Bite_*）

| 字段 | 含义 |
|---|---|
| BitePersonalityId | 唯一 ID,被 FishDefinition.BitePersonalityId 引用 |
| ProbeDurationSeconds | 试探期时长(浮漂轻点,提竿=空钩) |
| TrueBiteWindowSeconds | 真咬窗时长(黑漂,此窗内提竿=中鱼,超时=脱钩) |
| PerfectHookWindowSeconds | **完美提竿窗**(真咬开始后 X 秒内提竿=完美中鱼,规格 4.1 默认 1s) |
| PerfectFishStrengthMultiplier | 完美中鱼时鱼力量折减(0~1,规格 0.8) |
| PerfectFishStaminaMultiplier | 完美中鱼时鱼体力折减(0~1,规格 0.85) |
| PerfectInitialLineLengthMultiplier | 完美中鱼时初始线长折减(0~1,鱼更近) |

## 5. 搏斗性格：`UCatFightPersonalityDefinition`（DA_Fight_*）

| 字段 | 含义 |
|---|---|
| FightPersonalityId | 唯一 ID,被 FishDefinition.FightPersonalityId 引用 |
| CalmDurationRangeSeconds | 顺从期(向内游)时长区间,服务器每段随机抽;鱼体力<50% 后休息期 ×1.5 |
| StruggleDurationRangeSeconds | 挣扎期(向外游)时长区间;上钩瞬间必从挣扎开始 |
| CalmMovementSpeedCentimetersPerSecond | 顺从期游速(向内) |
| StruggleMovementSpeedCentimetersPerSecond | 挣扎期游速(向外) |
| BaseDrainMultiplier / StruggleDrainMultiplier | 该鱼种体力消耗基础/挣扎倍率(在规格系数之上再乘) |
| DirectionRetargetDurationRangeSeconds | 每段目标游向持续时间；到期才重新随机，不是每帧随机 |
| MaximumTurnRateDegreesPerSecond | 当前游向追向目标游向的最大角速度，控制鱼转弯灵活度 |
| StruggleOutwardDirectionBias | 挣扎时偏向鱼线外向的程度；越高越常正面对抗 |
| CalmInwardDirectionBias | 平静时偏向竿尖方向的程度；越高越容易出现安全收线窗口 |
| LateralMovementBias | 横向绕竿/切线运动倾向 |
| FeintProbability | 挣扎阶段先选一次反向目标的概率，用于假动作 |
| FullStaminaInwardProbability | 满体力重选方向时进入“朝竿尖扇区”的概率；低值可防止高体力鱼过早贴岸 |
| ExhaustedInwardProbability | 接近力竭时的向内概率；必须 ≥ 满体力值 |
| InwardProbabilityExponent | `pow(1-体力比例, 指数)` 的曲线；>1 表示低体力后才明显增加向内概率 |
| InwardConeHalfAngleDegrees | 朝竿尖方向左右各多少度算向内；默认 60°，完整扇区 120° |
| StrongConfrontationAlignmentThreshold | 夹角投影达到多少才算强对抗；体力/磨损在阈值以下仍连续按 `LineLoad` 投影计算，张力不把低方向负载抬成满负载 |
| StrongConfrontationConfirmationSeconds | 强对抗角度需要连续保持多久才发布表现标记，不裁决终局；落水由 Condition 水深判定 |
| AngleStrengthExponent | 对 `max(cos夹角,0)` 做幂变换；1=线性，越大则斜向力量衰减越快 |

## 6. GAS 资产：`UCatAbilitySet` / `UCatAbilityInputConfig`

**AbilitySet**（DA_CatAbilitySet_Default,每行一条 GrantedAbilities）:
- `Ability`:GameplayAbility 类——**换成蓝图子类就在这里换**(BP_GA_*)
- `InputTag`:绑定的输入 Tag(Cat.Input.Fishing.Primary 等,前缀必须 Cat.)
- `Level`:授予等级(当前恒 1)
- `ActivationPolicy`:OnInputTriggered=按一下激活一次 / **WhileInputActive=按住期间保持激活(左键/右键/Q 三个按住型必须用它,否则收不到 InputReleased)** / OnGranted=授予即激活
- `InitialEffect`:授予时附带的 GameplayEffect(可空)

**InputConfig**（DA_CatAbilityInputConfig）分两组：

- `AbilityInputActions`：`InputAction` ↔ Fishing `InputTag`，用于把输入送进 ASC/GAS；须覆盖 6 个 Fishing InputTag：RodInteract、Primary、Slack、Cancel、Scoop、Chum。
- `NativeInputActions`：不需要 Gameplay Ability 的意图映射。当前为 `IA_Interact` ↔ `Cat.Input.Interact`，由 PlayerController 分发给交互组件。

这里采用 Lyra 风格的“设备输入 → InputTag → 消费者”：改 `E/R` 键位只动 IMC，玩法代码仍按稳定 Tag 工作。

## 7. 曲线资产（CurveFloat）

| 曲线 | 引用处 | 输入→输出 | 校验(不满足→整条链失效) |
|---|---|---|---|
| Curve_ChumSaturation | CatFishCatalogSettings.ChumSaturationCurve | 归一化窝料亲和度 0→1 映射到权重倍率 | **v(0) 必须恰=1.0** 且单调不减,终值≤MaximumChumModifier |
| Curve_ChumDistanceFalloff | 各窝料 `UCatEquipmentFragment_Chum` 的 ChumInfluence | 0=窝点中心→1=边缘 的浓度衰减 | 全程 ≥0 且 **v(0)>0** |
| Curve_ChumTimeFalloff | 同上 | 0=刚投放→1=到期 的浓度衰减 | 同上 |

## 8. 常见配置事故速查

| 症状 | 多半是 |
|---|---|
| starter 装配/发窝料失败 InvalidPayload | 对应 DA 未注册 / bEnableRuntimeDefinition 没勾 / 字段组合不满足当前入口需要的真实用途 |
| No eligible fish | 鱼的 Region/TimeOfDay/Weather 不匹配或空数组；协作人数不足；全部鱼超过 MaximumChallengeRatio；或 CatFishCatalogSettings 的窝料曲线/连续挑战参数未配、非法 |
| 打窝 EquipmentUnavailable | 背包没窝料(上一条的下游);或窝料 Fragment 的 ChumInfluence 缺曲线 |
| 提竿后搏斗数值全 0 | 猫种类 ID 配错(看 initial_attributes_unresolved 日志) / DA_Bite/DA_Fight 未注册 |
| 新 DA 配好了不生效 | ini 没加注册行,或加了没重启 Editor |
