# DataAsset 字段说明手册

对应代码状态：2026-08-19。给配数值/建资产的人看：每个 DataAsset 类型的字段含义、校验规则、注册方法。

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
| 装备 CatEquipmentDefinition | `[CatEquipmentSettings]` `+Definitions=` | `/Game/Catfishing/Data/Equipment/Equip_*` |
| 鱼种 CatFishDefinition | `[CatFishCatalogSettings]` `+Definitions=` | `/Game/Catfishing/Data/Fish/Fish_*`；Showcase2 的 `River` 水域直接使用正式目录 |
| 咬钩性格 CatBitePersonalityDefinition | `[CatFishingSettings]` `+BitePersonalities=` | `/Game/Catfishing/Data/Fish/Bite_*` |
| 搏斗性格 CatFightPersonalityDefinition | `[CatFishingSettings]` `+FightPersonalities=` | `/Game/Catfishing/Data/Fish/Fight_*` |
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
| FishingStrength | **猫力量**（规格 4.2：与鱼力量、竿强度三方比较） | >0 |
| FightStaminaMaximum | **猫搏斗体力上限**（规格 4.3 消耗与松线喘息回复的基线） | >0 |
| bEnableRuntimeDefinition | 显式启用 gate | 必须 True |

⚠️ 角色指定了 ID 但定义缺失/未就绪时**不会悄悄换成全局值**——属性播种直接失败并打 `initial_attributes_unresolved` Warning，钓鱼链整体不可用，方便第一时间发现配错。

## 2. 装备：`UCatEquipmentDefinition`（正式目录 `Equip_Rod/Bait/Float/ScoopNet/Chum_*`）

一个类覆盖五种装备，靠 `Kind` 区分；**每个 Kind 只看自己那组字段，其余必须保持默认 0/false**（校验会因"竿字段出现在鱼饵上"这类越界而判未就绪）。

**共同字段**：`EquipmentDefinitionId`(唯一ID) · `Kind`(种类,不能 Unknown) · `LoadoutSlotId`(Rod/Bait/Float/ScoopNet 四种必填,窝料不填) · `RequiredUnlockId`(解锁门槛,None=不设) · `bRunConsumable`(是否一局内耗材:窝料 True、特殊饵 True、其他 False) · `bSpecialBait`(特殊鱼饵标记,与 bRunConsumable 同真同假仅限 Bait) · `FunctionalRouteId`(功能路由,必填,常规填 Route_Standard) · `bEnableRuntimeDefinition`(gate)

**Rod（鱼竿）**：
| 字段 | 含义 | 规格对应 |
|---|---|---|
| MaximumRodDurability | **本场鱼线耐久上限**（字段名为兼容资产保留；每次新会话重置，不会写坏鱼竿） | 40/70/120 档 |
| FishingStrength | 钓组承载强度（静态） | 25/60/130 档，判定①断线比较用 |
| MaximumLineLengthCentimeters | 线长上限 cm | 放尽绷紧强制按拖判定 |
| BaseDurabilityWearPerSecond / HighTensionWearMultiplier | 基础磨损/绷紧磨损倍率 | ≥0 / ≥1 |
| RodTipLocal/StandLocal/GripLocalTransform | 竿尖(抛竿原点+鱼线起点)/操作站位/握持 三个权威锚点 | 表现蓝图只读不写 |

**Bait（鱼饵）**：`BiteRateMultiplier`(>0,咬钩率倍率) · `MinimumBiteDelayMultiplier`(>0,最短咬钩延迟倍率)
**Float（浮漂）**：`MaximumCastDistanceCentimeters`(>0,最大抛竿距离) · `CastErrorStandardDeviation/MaximumCastErrorRadiusCentimeters`(落点误差σ/上限,σ≤上限) · `BiteSignalStability`(0~1,咬钩信号稳定度)
**ScoopNet（抄网）**：`ScoopReachCentimeters`(>0,**抄手沿 Character 面朝正前方发射的水平线段长度**,语义="网杆多长")。方向取 `Character Actor Forward`，不读取 `Controller/Camera` 朝向。与鱼定义里的 `ScoopTargetRadiusCentimeters`(圆半径)配对构成抄网判定：**俯视投影下线段∩圆**即够得着。实际生效长度取 `min(本值, UCatFishingSettings::ScoopReachCentimeters)`——全局那个是上限闸门。高度差另由 `UCatFishingSettings::MaximumScoopVerticalDeltaCentimeters` 单独限制,判定本身完全不看俯仰角。当前开发期 `bAutoGrantStarterScoopNet=True` 会给每名玩家库存临时发一份并选中正式目录定义 `StarterScoopNet`；正式商店/奖励获取就绪后关闭该开关。
**Chum（窝料）**：`bRunConsumable` 必须 True，核心在 `ChumInfluence` 结构：

| ChumInfluence 字段 | 含义 | 校验 |
|---|---|---|
| RadiusCentimeters | 窝点半径 cm | >0 |
| DurationSeconds | 窝点持续秒 | >0 |
| BaseContribution (Fishy/Fragrant/Fermented) | 腥/香/酵三轴基础贡献量,与鱼的 ChumPreference 点积决定诱鱼偏好 | 三轴合法 |
| DistanceFalloffCurve | 距离衰减曲线(输入0=中心→1=边缘,输出≥0,v(0)>0) | 必填 |
| TimeFalloffCurve | 时间衰减曲线(输入0=刚投→1=到期) | 必填 |
| MaximumQuantityPerPlacement | 单次投放最多消耗份数 | >0 |
| PresentationId / PresentationClass | 表现语义 ID / 表现 Actor 类 | 可空 |

## 3. 鱼种：`UCatFishDefinition`（DA_Fish_*）

| 字段组 | 字段 | 含义 |
|---|---|---|
| 身份 | FishDefinitionId | 唯一 ID,图鉴/日志/实物鱼都引用它 |
| 表现 | PresentationDefinition | **唯一表现入口**，直接引用本鱼 `FishPresentation_*`；水中 Encounter、落地 Pickup 和嘴叼状态都沿这条引用解析，不得按 FishDefinitionId 再建 Mesh/ABP 映射表 |
| 体型 | BodyClass | Standard=单人可搏 / Giant=可多人协作；抄网成功后世界鱼由首个合法抄手叼走；不能 Unknown |
| 出没 | RegionIds / TimeOfDay / Weather | 可出现的水域 ID/时段(夜晚永不进选择器)/天气;**空数组=未配置=不出现** |
| 稀有 | RarityTierId / SpawnWeight | 稀有度轴 ID / 选择正权重(稀有度由数据表达,代码无硬编码档位) |
| 体重 | Minimum/MaximumWeightKilograms | 服务器在区间内抽取真实重量;min≤max |
| 搏斗 | FishStrength | **鱼力量**(规格 4.2,与猫力/竿强比较);>0 |
| 搏斗 | FishFightStamina | **鱼搏斗体力**(短周期,与稀有度独立);>0 |
| 搏斗 | MinimumFightParticipants | 需要的协作人数;单人局过滤 >1 的定义 |
| 抄网 | **ScoopTargetRadiusCentimeters** | **这条鱼的可捞圆圈半径 cm**,圆心随鱼移动;抄手向正前方发射长度=抄网 ScoopReach 的水平线段,与圆相交即够得着。语义="这条鱼有多好捞"——小鱼小圈、巨鱼大圈以降低多人抢抄难度。**必须 >0,为 0 时服务器一律拒绝抢抄** |
| 性格 | BitePersonalityId / FightPersonalityId | 引用下面两类模板的 ID |
| 偏好 | ChumPreference (三轴) | 与窝点三轴点积→经饱和曲线→选择权重放大(封顶 MaximumChumModifier) |
| 偏好 | BaitWeightMultipliers | 特定鱼饵 ID→权重倍率;普通饵不用列 |
| 食用 | FoodSafety / HungerRelief / PoisonIncrease | Safe/Toxic 结论 + 吃后减饥/增毒量(Safe 必须 0 毒) |
| 其他 | SacrificeContribution / CaptureImprintEventId / bTankDisplayEligible | 献祭额度 / 捕获成像事件 / 可否入展示鱼缸 |
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

鱼种没有固定“低级/中级”战斗标签。令力量比 `S=FishStrength/玩家合计力量`、体力比 `T=FishFightStamina/玩家合计搏斗体力`，目录按 `max(S, 2ST/(S+T))` 计算当前上下文里的连续挑战度：力量比是危险下限，力量/体力调和均值只在两项都足够时抬高挑战度，避免“体力高但力量极低、进入搏斗又被 2 倍力量规则秒杀”的鱼占据势均力敌带。`≤ ComfortChallengeMaximumRatio` 为轻松带，之后到 `MatchedChallengeMaximumRatio` 为势均力敌带，再到 `MaximumChallengeRatio` 为高风险带；超过安全上限才不进入池。系统先按三条 `*ChallengeBandWeight` 在当前有候选的难度带之间抽取，再用 `SpawnWeight × 窝料倍率 × 鱼饵倍率 × 连续挑战倍率` 在带内选鱼。某个目标带没有鱼时会在其余有候选的带之间重新归一化；只有生态条件、协作人数或安全上限后确实没有鱼才会空钩。

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
| StrongConfrontationAlignmentThreshold | 夹角投影达到多少才算强对抗；体力/磨损在阈值以下仍连续按投影计算 |
| StrongConfrontationConfirmationSeconds | 强对抗角度需要连续保持多久，才允许触发断线/落水/碾压 |
| AngleStrengthExponent | 对 `max(cos夹角,0)` 做幂变换；1=线性，越大则斜向力量衰减越快 |

## 6. GAS 资产：`UCatAbilitySet` / `UCatAbilityInputConfig`

**AbilitySet**（DA_CatAbilitySet_Default,每行一条 GrantedAbilities）:
- `Ability`:GameplayAbility 类——**换成蓝图子类就在这里换**(BP_GA_*)
- `InputTag`:绑定的输入 Tag(Cat.Input.Fishing.Primary 等,前缀必须 Cat.)
- `Level`:授予等级(当前恒 1)
- `ActivationPolicy`:OnInputTriggered=按一下激活一次 / **WhileInputActive=按住期间保持激活(左键/右键/Q 三个按住型必须用它,否则收不到 InputReleased)** / OnGranted=授予即激活
- `InitialEffect`:授予时附带的 GameplayEffect(可空)

**InputConfig**（DA_CatAbilityInputConfig）分两组：

- `AbilityInputActions`：`InputAction` ↔ Fishing `InputTag`，用于把输入送进 ASC/GAS；须覆盖五个核心 Fishing Tag。
- `NativeInputActions`：不需要 Gameplay Ability 的意图映射。当前为 `IA_Interact` ↔ `Cat.Input.Interact`，由 PlayerController 分发给交互组件。

这里采用 Lyra 风格的“设备输入 → InputTag → 消费者”：改 `E/R` 键位只动 IMC，玩法代码仍按稳定 Tag 工作。

## 7. 曲线资产（CurveFloat）

| 曲线 | 引用处 | 输入→输出 | 校验(不满足→整条链失效) |
|---|---|---|---|
| Curve_ChumSaturation | CatFishCatalogSettings.ChumSaturationCurve | 归一化窝料亲和度 0→1 映射到权重倍率 | **v(0) 必须恰=1.0** 且单调不减,终值≤MaximumChumModifier |
| Curve_ChumDistanceFalloff | 各窝料 DA 的 ChumInfluence | 0=窝点中心→1=边缘 的浓度衰减 | 全程 ≥0 且 **v(0)>0** |
| Curve_ChumTimeFalloff | 同上 | 0=刚投放→1=到期 的浓度衰减 | 同上 |

## 8. 常见配置事故速查

| 症状 | 多半是 |
|---|---|
| starter 装配/发窝料失败 InvalidPayload | 对应 DA 未注册 / bEnableRuntimeDefinition 没勾 / 某字段越了 Kind 的界 |
| No eligible fish | 鱼的 Region/TimeOfDay/Weather 不匹配或空数组；协作人数不足；全部鱼超过 MaximumChallengeRatio；或 CatFishCatalogSettings 的窝料曲线/连续挑战参数未配、非法 |
| 打窝 EquipmentUnavailable | 背包没窝料(上一条的下游);或窝料 DA 的 ChumInfluence 缺曲线 |
| 提竿后搏斗数值全 0 | 猫种类 ID 配错(看 initial_attributes_unresolved 日志) / DA_Bite/DA_Fight 未注册 |
| 新 DA 配好了不生效 | ini 没加注册行,或加了没重启 Editor |
