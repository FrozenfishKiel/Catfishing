# DataAsset 字段说明手册

对应代码状态：2026-09-04。给配数值/建资产的人看：每个 DataAsset 类型的字段含义、校验规则、注册方法。

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
| 每公斤力量 | StrengthPerKilogram | 10 | 冻结实际鱼重→鱼力量；猫系统质量独立配置，不随力量成长 |
| 每点力量推力 | ForcePerStrengthNewtons | 默认 1 N/力量 | 正常满力推力基准；鱼实际主动推力再乘连续出力 u，质量另参与积分 |
| 单猫系统质量 | CatBodyMassKilograms | 默认 5 kg | 按实际参与的猫组合系统质量，与鱼力量和成长独立 |
| 旧每点力量加速度 | AccelerationPerStrength | 仅旧载荷 | 已停用，不能拿旧 5 cm/s² 数值套用新牛顿换算 |
| 旧猫端驱动力响应时间 | DriveResponseSeconds | 仅旧载荷 | 当前使用共同张力和质量积分，不参与运行 |
| 收线速度 | ReelSpeedCentimetersPerSecond | 80 cm/s | 左键收线意图速度上限 |
| 猫力竭后鱼外冲速度倍率 | ExhaustedCatEscapeSpeedMultiplier | 2 | 主位体力为零且没有助手实际出力时，按人格满出力参考游速乘此倍率持续外冲；有限值且至少为 1 |
| 猫做功体力消耗系数 | CatStaminaCostPerStrengthCentimeter | 默认 0.002 | 猫移动/收线每标准力量·cm 已完成正功的单价 |
| 猫转杆每标准转矩弧度体力系数 | CatRodStaminaCostPerStrengthRadian | 默认 0.03 | 真实转角按主位主动转矩比例加权后计价，不使用最大转速虚拟弧长 |
| 猫无负载动作成本倍率 | CatUnloadedWorkMultiplier | 默认 0.15 | 猫实际做功的基础价格，与负载价格相加 |
| 猫满用力每秒支撑耗体 | CatSupportStaminaPerSecond | 默认 2/s | 支撑按用力/负载比例平方和持续时间结算；共享支撑与转杆支撑取较高者 |
| 鱼每米未完成意图耗体 | FishStaminaPerUnfulfilledMeter | 新原生默认 5/3 点/m（约 1.666667） | 沿本步鱼主动朝向，将期望位移减去最终实际位移的投影，负值取 0；厘米转米后乘本价格，不再乘 u²、鱼线夹角或张力比例 |
| 旧鱼每秒对抗耗体 | FishEffortStaminaPerSecond | 仅旧载荷，旧默认 3 点/s | 已标 Deprecated，不参与费用或运行校验，不换算为新每米价格；全 Content/外部 Blueprint 字段消费者未完成审计前保留反射身份 |
| 旧鱼每厘米体力价格 | FishStaminaCostPerStrengthCentimeter | 仅旧载荷 | 不再运行，旧力量乘厘米单价不得直接作为新意图缺失位移单价 |
| 猫移动体力倍率 | CatMovementStaminaMultiplier | 默认 1 | 绷线时主动远离鱼的身体移动费用；被动位移不计 |
| 猫收线体力倍率 | CatReelStaminaMultiplier | 默认 1 | 原求解器本步卷线量的正功费用 |
| 猫转杆体力倍率 | CatRodStaminaMultiplier | 默认 1 | 主位实际转杆正功及其时间支撑的倍率 |
| 猫持竿体力倍率 | CatHoldStaminaMultiplier | 默认 1 | 共享沿线支撑费用倍率；实际做功费用不再抵扣支撑 |
| 猫负载体力倍率 | CatLoadStaminaMultiplier | 默认 1 | 猫实际做功乘 `(无负载动作倍率 + 自身归一化负载 × 本倍率)` |
| 旧鱼负载体力倍率 | FishLoadStaminaMultiplier | 仅旧载荷 | 不再叠加鱼费用；关闭鱼耗体改设 FishStaminaPerUnfulfilledMeter=0 |
| 旧鱼受阻努力折算倍率 | IsometricEffortMultiplier | 仅旧载荷 | 不参与当前意图缺失位移计算，不把旧等效努力倍率叠加到新每米价格 |
| 放线体力恢复速度 | SlackStaminaRegenPerSecond | 3/s | 正常右键且线杯还有可放容量时猫的恢复速度；满线恢复正常对抗计费，零体力强制拖拽除外 |
| 鱼力竭吸附阈值 | FishExhaustionThreshold | 0.5 | 本步产生正的鱼对抗耗体后，剩余绝对体力不高于该值才吸附归零；零耗体不触发 |
| 旧低体力休息触发比例/时长倍率 | LowStaminaRestThreshold/Multiplier | 仅旧载荷 | 运行已迁入人格 AdaptiveSteeringConfig 的体力阈值和行为时长倍率 |
| 满表现张力 | DisplayTensionNewtons | 默认 50 N | 仅将真实张力归一化供表现，不产生玩法张力 |
| 旧满张力响应距离 | TensionResponseRangeCentimeters | 仅旧载荷 | 不再由几何误差换算玩法张力 |
| 逃脱松线余量 | EscapeSlackCentimeters | 100 cm | 无人持竿时超过最大线长后的逃脱余量 |
| 僵持鱼竿磨损系数 | StalemateRodWearPerFishStrength | 0.1 | 按鱼沿线向外负载连续缩放的鱼竿磨损，写回同一装备实例；几何张力不能替代方向负载 |
| 持竿最低杠杆倍率 | HeldRodMinimumLeverageMultiplier | 0.4 | 竿身偏线时保留的最低有效力量 |
| 最大约束修正速度 | MaximumFishConstraintCorrectionSpeedCentimetersPerSecond | 160 cm/s | 鱼端修正及猫端牵引目标的安全上限 |
| 旧背离鱼方向最低速度倍率 | MinimumCarrierAwaySpeedMultiplier | 仅旧载荷 | CMC 现按共同张力/支撑积分，不再硬乘后退速度 |

`DefaultGame.ini` 只保存 `FightBalanceDefinition` 资产引用，不再保存上述数值；C++ 也不提供可偷偷生效的第二套回退。资产缺失、未勾“启用正式运行”或任一现行字段非法时，Fishing runtime 保持 fail-closed。

上述现行费用与倍率均允许非负有限值。猫实际做功与支撑分开，阶段倍率不再额外放大猫费用；正功量只来自已完成的主动身体移动、本步卷线与归一化主动转矩加权转角，受阻时只承担时间支撑。猫负载倍率为 0 只关闭实际做功的负载附加部分；完全关闭猫费用需要关闭线性单价、转杆单价及支撑费。

鱼当前按 `D_cm=max(0, desiredSpeed_cm/s(u)×dt_s-dot(finalActualDelta_cm, heading))` 计算未完成意图位移，再按 `FishStaminaPerUnfulfilledMeter×D_cm/100` 计费。意图为零时费用为零；达到期望速度的自由游动也为零，但起步、转弯或被拉回导致实际进展小于意图时会产生费用，不要求有张力或特定鱼线夹角。结算仍要求有持有人、猫有可用合力、鱼未力竭且仍有体力，并排除有效右键放线恢复和强制力竭拖拽。满线时右键不属于有效放线恢复。

每米价格默认 `5/3` 是新模型的独立标定：180 cm/s 满出力且完全受阻时为 3 点/s，不是旧每秒价的单位换算，也不读取旧每厘米价格或受阻倍率。修改资产后下一场搏斗生效；创建脚本只初始化新资产，已有合法的新价格保留，非法资产报错而不自动覆盖。`migrate_fish_adaptive_behavior.py` 的 `-AuditFishUnfulfilledStamina` 只读审计，`-ApplyFishUnfulfilledStamina` 只保存已知指纹的 Balance 并保护其余 55 个鱼相关包。

本轮已在完整构建后的两个独立 DebugGame 进程中完成保存与重载。`Saved/Automation/FishIntentStamina-20260908/AssetVerification.json` 的 15 项资产检查通过：新价格为 `1.6666666666666667` 点/m，其余 Balance 运行参数、旧载荷、55 个保护包指纹和正式引用不变。保存后的 Balance SHA256 仍为 `4042ca6ea1ae526e1cccf8c763d85d14b7555b85b3b2014b9ac5e7e89dca5d1b`；新价格等于原生默认，成功保存没有产生不同的包字节，独立重载由新原生类读出该值，不能据此声称产生了资产二进制改动。保存与重载原始报告分别为该目录的 `MigrationRetry/Migration.json` 和 `FreshReload/Audit.json`。首轮因文件占用保存失败的记录仍保留，失败后也已核对全部 56 包未变；上述资产检查不替代真人手感或打包联机验收。

## 2. 装备/道具：`UCatEquipmentDefinition`（正式目录 `Equip_*`）

一个类覆盖装备和道具，靠 `Kind` 区分；**每个 Kind 只看自己那组字段，其余必须保持默认 0/false**（校验会因"竿字段出现在鱼饵上"这类越界而判未就绪）。

**共同字段**：`EquipmentDefinitionId`(唯一ID) · `Kind`(种类,不能 Unknown) · `LoadoutSlotId`(Rod/Bait/Float/ScoopNet 四种钓鱼选择物必填，非选择型道具不填) · `RequiredUnlockId`(解锁门槛,None=不设) · `UseActorClass`(部署型物品 Use 到世界时生成的 Actor 类；鱼竿填 BP_CatFishingRodActor，其他部署物品填自己的 Actor) · `UseInventoryEffect`(统一 Use 成功后的库存影响：Auto 兼容旧部署资产并让 Chum/Herb 默认扣数量、None 表示无通用 Use、HoldInstanceUntilUnUse 表示部署到 UnUse 前占用实例、ConsumeQuantity 表示按请求数量消耗) · `bRunConsumable`(是否一局内耗材：普通/特殊鱼饵、窝料、草药等数量型运行消耗物为 True，工具和部署型物品为 False) · `bSpecialBait`(特殊鱼饵标记,与 bRunConsumable 同真同假仅限 Bait) · `FunctionalRouteId`(功能路由,必填,常规填 Route_Standard) · `bEnableRuntimeDefinition`(gate)

**Rod（鱼竿）**：
| 字段 | 含义 | 规格对应 |
|---|---|---|
| MaximumRodDurability | **鱼竿耐久上限**；新鱼竿或维修使用该上限，同一装备实例的剩余耐久跨场累计，归零即损坏；重新抛竿不恢复 | `StarterRodT1` 当前基线 150；其他档以正式资产为准 |
| FishingStrength | 已停用的旧鱼竿承载字段，仅保留资产/蓝图读取兼容 | 不再编辑、校验或参与搏斗；不要用它调断线阈值 |
| MaximumLineLengthCentimeters | 线长上限 cm | 放尽绷紧强制按拖判定 |
| BaseDurabilityWearPerSecond / HighTensionWearMultiplier | 满出力基础磨损/绷紧磨损倍率；前者冻结为 FishFullEffortRodWearPerSecond，按实际u²缩放，不按动画挣扎标签收费 | ≥0 / ≥1 |
| RodTipLocal/StandLocal/GripLocalTransform | 竿尖(抛竿原点+鱼线起点)/操作站位/握持 三个权威锚点 | 表现蓝图只读不写 |

**Bait（鱼饵）**：`BiteRateMultiplier`(>0,咬钩率倍率) · `MinimumBiteDelayMultiplier`(>0,最短咬钩延迟倍率)
**Float（浮漂）**：`MaximumCastDistanceCentimeters`(>0,最大抛竿距离) · `CastErrorStandardDeviation/MaximumCastErrorRadiusCentimeters`(落点误差σ/上限,σ≤上限) · `BiteSignalStability`(0~1,咬钩信号稳定度)

当前正式射程：羽毛 1000 cm、毛线球 1500 cm、铃铛 2000 cm。实际可抛距离取浮漂射程与鱼竿 `MaximumLineLengthCentimeters` 的较小值，并从竿尖量至落点；入门竿线长 1500 cm。原 300/500/700 cm 配置由 `Scripts/update_fishing_cast_ranges.py` 定向迁移，其他装备字段保留。
**ScoopNet（抄网）**：`ScoopReachCentimeters`(>0,**抄手沿 Character 面朝正前方发射的水平线段长度**,语义="网杆多长")。方向取 `Character Actor Forward`，不读取 `Controller/Camera` 朝向。与鱼定义里的 `ScoopTargetRadiusCentimeters`(圆半径)配对构成抄网判定：**俯视投影下线段∩圆**即够得着。实际生效长度取 `min(本值, UCatFishingSettings::ScoopReachCentimeters)`——全局那个是上限闸门。高度差另由 `UCatFishingSettings::MaximumScoopVerticalDeltaCentimeters` 单独限制,判定本身完全不看俯仰角。当前独立临时开关 `bAutoGrantStarterScoopNet=True` 使用正式定义 `StarterScoopNet`，由服务器在玩家占有新角色时补齐一把并自动选中，占一个背包格；已有抄网或同一角色已处理过时不重复发。商店获取接通后删除临时来源，射程和捕获判定保持不变。
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
| 搏斗 | FishStrength | 旧二进制资产兼容字段；运行时忽略，重存资产后可逐步清空 |
| 搏斗 | FishFightStamina | **鱼搏斗体力**(短周期,与稀有度独立);>0 |
| 搏斗 | MinimumFightParticipants | 需要的协作人数;单人局过滤 >1 的定义 |
| 抄网 | **ScoopTargetRadiusCentimeters** | **这条鱼的可捞圆圈半径 cm**,圆心随鱼移动;抄手向正前方发射长度=抄网 ScoopReach 的水平线段,与圆相交即够得着。语义="这条鱼有多好捞"——小鱼小圈、巨鱼大圈以降低多人抢抄难度。**必须 >0,为 0 时服务器一律拒绝抢抄** |
| 性格 | BitePersonalityId / FightPersonalityId | 引用下面两类模板的 ID |
| 偏好 | ChumPreference (三轴) | 与窝点三轴点积→经饱和曲线→选择权重放大(封顶 MaximumChumModifier) |
| 偏好 | BaitWeightMultipliers | 特定鱼饵 ID→权重倍率;普通饵不用列 |
| 食用 | FoodSafety / EatingExperience / PoisonIncrease | Safe/Toxic 结论 + 吃后体验/增毒量(Safe 必须 0 毒) |
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

鱼种没有固定“低级/中级”战斗标签。服务器为每个候选鱼种按本次机会种子和稳定鱼 ID 独立抽取个体重量，令 `FishStrength=WeightKilograms×StrengthPerKilogram`；其中换算系数来自当前正式搏斗平衡资产。该重量和力量一旦选中便冻结，选择、搏斗和 HUD 不再分别重抽或读取旧静态字段。令力量比 `S=FishStrength/玩家合计力量`、体力比 `T=FishFightStamina/玩家合计搏斗体力`，目录按 `max(S, 2ST/(S+T))` 计算当前上下文里的连续挑战度：力量比是危险下限，力量/体力调和均值只在两项都足够时抬高挑战度，避免力量极低但体力很高的鱼被错误归入势均力敌带。`≤ ComfortChallengeMaximumRatio` 为轻松带，之后到 `MatchedChallengeMaximumRatio` 为势均力敌带，再到 `MaximumChallengeRatio` 为高风险带；超过安全上限才不进入池。系统先按三条 `*ChallengeBandWeight` 在当前有候选的难度带之间抽取，再用 `SpawnWeight × 窝料倍率 × 鱼饵倍率 × 连续挑战倍率` 在带内选鱼。某个目标带没有鱼时会在其余有候选的带之间重新归一化；只有生态条件、协作人数或安全上限后确实没有鱼才会空钩。

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

## 5. 搏斗性格：`UCatFightPersonalityDefinition`（Fight_*）

以下为当前连续出力性格字段；正式四性格和鱼树已在 2026-09-08 完成迁移并经独立进程重载：四性格版本 1、满出力速度 110/140/180/240 cm/s。本轮改鱼耗体不修改这些行为资产；全局每米价格见上文 Balance，交付证据见 [鱼运动实现导读](FishFightImplementationGuide_zh-CN.md)。运行不再用两档游速、阶段费用或疲劳向内概率。

| 字段 | 含义 |
|---|---|
| FightPersonalityId | 稳定ID，被 FishDefinition.FightPersonalityId 引用；16正式鱼共用4份性格 |
| AdaptiveMotionVersion | 0=旧序列化格式，1=新格式；只迁移0，不覆盖版本1的调参或以旧值修补非法新参数 |
| FullEffortMovementSpeedCentimetersPerSecond | 满出力参考自由游速 cm/s，必须为正；校准固定水阻，实际推进随u变化。旧资产取原两档速度最大值，不把速度比例换成u |
| AdaptiveSteeringConfig | 新的连续行为配置，Session 开始搏斗时整体冻结；下列带此前缀的字段均属于此结构 |
| AdaptiveSteeringConfig.OutwardEffortRange / LateralEffortRange / EaseOffEffortRange | 外冲/横切/缓游目标出力区间，默认0.8～1 / 0.75～0.95 / 0.3～0.45，无量纲[0,1]；横切仍主动抗线 |
| AdaptiveSteeringConfig.EffortRisePerSecond / EffortFallPerSecond | 实际出力升/降速率，默认0.8 / 0.6比例每秒；切状态保留当前u |
| AdaptiveSteeringConfig.OutwardDurationRangeSeconds / LateralDurationRangeSeconds / EaseOffDurationRangeSeconds | 新结构默认最长区间2～4 / 1.5～3 / 1.25～2秒；四正式性格缓游区间见鱼运动文档。版本0载荷迁移仍保留原秒数，已知正式旧资产另经显式调参迁移 |
| AdaptiveSteeringConfig.MinimumBehaviorDurationSeconds | 受阻改道前的最短承诺，默认1.25秒；采样的局部/整轮时长不短于它。整轮恢复优先，允许越过局部承诺 |
| AdaptiveSteeringConfig.ActiveBoutDurationRangeSeconds | 连续外冲/横切的整轮最长时限，默认6～10秒；跨改道累计，缓游结束才重新采样，避免无限反复冲刺 |
| AdaptiveSteeringConfig.RetargetDurationRangeSeconds | 有限偏角重新采样间隔，默认0.6～1.4秒；不逐步随机换侧 |
| AdaptiveSteeringConfig.MaximumTurnRateDegreesPerSecond | 实际游向的最大水平转速，默认120°/秒；旧人格迁移保留原角速度 |
| AdaptiveSteeringConfig.OutwardAngularSpreadDegrees | 外冲随机偏角半宽，默认25°；旧扇区只按角度几何迁入，旧内游概率退出 |
| AdaptiveSteeringConfig.LateralOutwardBias / EaseOffInwardBias | 横切向外系数/缓游向内混合权重，默认0.9 / 0；横切目标为normalize(切向+0.9×向外)，默认缓游只横游，不主动帮收线 |
| AdaptiveSteeringConfig.LowStaminaRatio | 低体力阈值，默认0.3；影响入态时长采样，不直接减弱正常最大力量，也不让正式树立即退出外冲 |
| AdaptiveSteeringConfig.LowStaminaActiveDurationMultiplier / LowStaminaEaseOffDurationMultiplier | 低体力进入强动作时局部时长及新一轮对抗时限×0.7，进入缓游时×1.5；已采样整轮时限不在途中重抽，缓游不恢复鱼体力 |
| AdaptiveSteeringConfig.BlockedLoadThreshold / BlockedProgressFraction | 受阻需真实承载，默认平滑负载≥0.2且主动方向实际速度低于期望自由游速的0.4 |
| AdaptiveSteeringConfig.BlockedConfirmationSeconds / LoadSmoothingSeconds | 受阻确认时长/负载平滑时间常数，默认0.35 / 0.15秒；只用上一完整物理步反馈 |
| StrongConfrontationAlignmentThreshold / StrongConfrontationConfirmationSeconds | 强对抗表现阈值/确认时长，不选择终局，不代替鱼费用G |
| AngleStrengthExponent | 方向性负载/磨损/表现的夹角指数，不衰减整份主动推进；新的鱼耗体G独立使用沿线正投影 |

旧 `CalmDurationRangeSeconds/StruggleDurationRangeSeconds`、两档 `*MovementSpeedCentimetersPerSecond`、重选/角速度/扇区字段作为版本0迁移载荷暂留；`BaseDrainMultiplier/StruggleDrainMultiplier` 和旧向内概率/假动作字段不再进入运行链。本轮四份正式性格均已版本1；其他旧包重载迁移及全Content类型/外部Blueprint字段引用尚未全部确认，不能仅凭源码无读取删除反射身份。迁移入口为 `Scripts/migrate_fish_adaptive_behavior.py`，版本1使用上表新字段调参。

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
