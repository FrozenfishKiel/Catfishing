# 鱼运动与遛鱼逻辑实现导读

这份文档是鱼、线、杆、猫运动实现的阅读入口，解释当前源码怎样计算运动、负载和收尾。核对日期：2026-09-04。源码内可以搜索 `[FishLogic`，按 1～5 顺序阅读。

当前实现是“候选位移与线长约束 + 力量差分配 + 玩法裁决”的混合模型，尚未完成共同拉力驱动的物理改造。文中“意图速度”“负载”“做功”均按下述实际公式解释，不能据名称推断已经存在统一动力学。

## 阅读边界与后续改造

用户要求：人为设定物理参数与施力意图，由同一套受力框架产生位移、僵持和拖动；结果标签只供观察和表现。以下是设计方向，尚未作为整套系统实现：先统一物理量与鱼线反力，再一起接通鱼和有限出力的卷线器，随后接入猫的移动/地面支撑与鱼竿转矩，最后替换状态驱动的断线、上岸和体力特例，并做整链验收。

这些是一个运动系统内部的实施顺序，不是可独立关闭的业务事项。模块状态和验证证据只维护在 [需求对齐差距清单](Development/需求对齐差距清单.md)。本指南下文只描述当前实现；每次替换旧路径时应直接更新相应正文，同轮核对源码引用、配置、资产生成脚本、测试入口和其他文档。无消费者的旧方案及错误说明应删除，历史过程由 Git 保留；仍有资产或兼容消费者时明确写出消费者和迁移限制。

## 一条完整调用链

```text
玩家左/右键
  → Fishing Ability / CommandComponent：把输入请求送到服务器
  → ACatFishingSession：验证当前会话和阶段
  → UCatFishingFightRunner：服务器每 0.05 秒推进一次
      1. ST_FishFight：决定当前是发力还是平静，并把 MotionIntent 交给 Runner
      2. FCatFishSteeringModel：结合鱼体力决定向内/向外，再平滑转弯
      3. FCatFishingFightSimulator：合并运动与卷线意图，计算约束修正、努力距离、体力和本场鱼线磨损
         Runner 再解析真实水面/岸线/地面，发布猫端牵引目标与鱼竿转矩输入
      4. ACatFishEncounterActor：应用服务器位置并复制表现状态
  → 鱼体力耗尽
      5. FishExhausted 事件切换 Session StateTree；同一 Runner 停止鱼主动游动，拖到真实干地且接近竿尖后生成 Pickup
```

可以用 Aura 课程里的分层来类比：

- Ability/CommandComponent 类似输入到 Ability 的入口，只表达“玩家想收线/松开线杯”。
- Session 类似服务器权威的玩法状态机，决定当前请求能不能执行、什么时候切阶段。
- StateTree 类似 Aura 里“由 Ability 状态决定接下来允许执行什么”：只控制发力/平静的高层顺序，不负责数值公式。
- Runner 类似一个固定频率执行的服务器任务，负责组织 StateTree 意图、转向和模拟步骤，不在里面堆公式。
- Simulator 类似纯公式层。它不读取 Actor、World 或网络，因此同一输入必定得到同一结果，方便测试。
- EncounterActor 类似表现载体。服务器写入结果，客户端通过属性复制和移动复制看到同一条鱼。
- DataAsset/DeveloperSettings 类似 Ability 配置和 GE 数值配置，修改行为不需要重写流程。

## FishLogic 1：发力与休息节奏

入口：`ST_FishFight` → `FCatFishBehaviorStateTask` → `UCatFishingFightRunner::BeginBehaviorStateFromStateTree()`。

鱼在两个大状态间切换：

- `StrugglingOutward`：发力期，通常更倾向远离鱼竿。
- `CalmOrInward`：休息期，通常更倾向靠近鱼竿。

持续时间从鱼性格 DataAsset 的区间中抽取。鱼体力较低时休息期会变长。这里仅决定“情绪/意图”，不决定世界方向。

为什么不把所有公式都写进 StateTree Task：StateTree 适合让你直观看到“先发力、再平静、以后可插入蓄力冲刺”，但逐帧位移、鱼线和体力结算仍需要固定步长、纯 C++ 和单元测试。Task 只向 Runner 申请一个意图和持续时间，所以策划改树时不会绕过服务器权威规则。

## FishLogic 2：平滑随机方向

入口：`FCatFishSteeringModel::Step()`。

在 `DirectionRetargetDurationRangeSeconds` 到期或运动意图切换时抽取目标方向，按以下步骤选择：

```text
体力比例 → 向内概率（挣扎期再乘 FeintProbability）
         → 抽取向内/向外锚方向
         → 用方向偏置与 LateralMovementBias 调整扇区内随机偏角
         → 保存目标方向和下一次重选时间
```

抽到目标后不会立刻转过去，而是每秒最多转 `MaximumTurnRateDegreesPerSecond`。这叫“相关随机”：相邻帧彼此相关，所以轨迹生动但连续；逐帧重新随机则是白噪声，会表现成原地抖动。

本项目把“向内”定义为：目标方向落在“鱼 → 竿尖”方向左右各 `InwardConeHalfAngleDegrees` 的扇区内，默认是 ±60°。每次需要重新选择目标时，先按鱼当前体力算向内概率：

```text
疲劳度 = 1 - 当前鱼体力 / 初始鱼体力
向内概率 = Lerp(满体力向内概率, 力竭向内概率, 疲劳度 ^ 曲线指数)
```

概率、指数和两档游速以当前鱼种引用的 `UCatFightPersonalityDefinition` 为准，不以测试鱼旧数值作为全鱼种基线。发力状态仍以外冲为主，只把 `FeintProbability` 比例的向内概率用于假动作。

活鱼不会因为自己的游动直接冲上陆地。若鱼只是自行撞岸，Runner 用水域查询的最近岸点与入水方向消除出水位移，保留本步沿岸切向滑动，并由 `RedirectFromWaterBoundary()` 平滑游回水里；真实拖拽候选不经过这个防自游出水分支。

猫端沿绷紧鱼线把鱼拖向岸上时，活鱼与鱼干共用 `ResolveFishSurfaceFromAuthority`：保留线约束求出的候选位移，水域只提供水面与岸向，不再用抛竿内缩点或初始落点包围盒挡住拖行。活鱼要有真实收线、按住收线时的剩余约束拖拽或猫端向岸平移；横向调杆不取消拖拽，主导向岸位移的纯甩杆仍不能让活鱼瞬间力竭。力竭鱼没有自主游动，直接随同一鱼线的端点约束拖行，不再套用活鱼的防误力竭门槛。烘焙轮廓与真实岸面有间隙时继续贴水面前进；即使岸面位于轮廓内，只要实际接触高于水面的干地也可上岸。首次地面高度不能被后续水面结果覆盖，高低坡面逐步重查；重新入水会撤销干地拾取资格并继续拖动，不把地面暂缺判为会话失效。活鱼首次接触干地仍按当前玩法进入 `ExhaustedReel/AutoHauling` 并清空体力，鱼干仍不扣猫体力，这些并非完整共同物理求解。干地鱼进入竿尖水平完成距离后原地生成 Pickup，松开左键仍能交接并按 E 拾取。诊断过滤 `fishing_surface_tow`、`fishing_fish_beached`、`fishing_surface_resolve_rejected`。

## FishLogic 3：夹角怎样变成力量

入口：`FCatFishingFightSimulator::Step()`。

先构造两个水平单位向量：

- `LineOutward`：竿尖指向鱼。
- `FishDirection`：鱼本步准备游动的方向。

然后计算：

```text
Alignment = dot(FishDirection, LineOutward) = cos(夹角)
LineLoad  = pow(max(Alignment, 0), AngleStrengthExponent)
```

- 同方向向外游：夹角 0°，`LineLoad=1`，向外负载比例为满值；是否绷线仍需检查线长。
- 斜向游：例如 60°，指数为 1 时 `LineLoad=0.5`。
- 横向游：90°，`LineLoad=0`；当前鱼向外负载估计为零，但不能据此断言线必然松弛。
- 朝竿游：点积为负，钳制为 0，不制造反方向的假拉力。

`LineLoad` 是鱼主动游向的向外投影比例，不是以力为单位的线张力。`TensionCentimeters` 是候选端点超过已放线长的距离；`NormalizedTension` 是该误差除以 `TensionResponseRangeCentimeters` 后钳制到 0～1 的值。当前猫端牵引、杆转矩与断线负载仍分别使用这些代理量，尚未共用一份约束反力。

当前运动计算顺序：

鱼的个体重量由服务器选择流程冻结，乘平衡资产的 `StrengthPerKilogram` 得到基础力量，再应用完美中鱼的力量倍率。猫的基础 `FishingStrength / StrengthPerKilogram` 是当前玩法等效质量；猫的可用力量随体力降低，质量不随体力降低。`CharacterMovement.Mass` 不作为这套分配公式的猫质量输入。

1. 鱼直接以性格资产的平静/挣扎游速乘固定步长生成水平候选位移；没有由鱼力量积分得到自由游速，也没有持久鱼速度状态。
2. 猫端收线速度取 `min(ReelSpeedCentimetersPerSecond, 有效猫力量 × AccelerationPerStrength × DriveResponseSeconds)`，按剩余可收长度生成请求并直接缩短 `L_paid`。模拟器把 `ActualReelDistanceCentimeters` 设为请求距离，没有按鱼线负载求卷线器停转。
3. 猫的移动输入形成端点候选位移，与鱼候选和已放线长一起生成约束误差。只有鱼的向外对抗加速度高于猫时，其差值才按双方质量分配猫端牵引；猫更强或鱼力竭时，修正主要落到鱼端。相等力量下猫端牵引为零是这套公式的性质，不代表已经完成地面摩擦与合力求解。
4. `ACatFishingRodActor::ApplyCarrierConstraint()` 在 CharacterMovement 完成移动后平滑追赶目标，补足向鱼速度并限制远离方向速度；这不是共同拉力在角色运动中的积分。

`bStalemate` 在求得候选修正位置后按径向位移阈值生成，只供诊断，不锁位置。`bStrongConfrontation` 则由向外比例阈值与持续时间确认，仍参与过载断线：只有它成立且 `max(鱼向外力量, 收线时猫有效力量) × NormalizedTension >= RodStrength` 才报告 `StrengthOverload`。耐久耗尽另有 `DurabilityDepleted` 分支，不要求强对抗；磨损按 `LineLoad` 缩放。不要把“强对抗仅用于表现”写成当前事实。

### 当前体力公式

`FCatFishingFightWorkModel::ComputeDrain()` 计算 `Realized=min(实际沿线距离, 意图沿线距离)`、`Blocked=max(意图沿线距离-Realized,0)`，再以 `Realized + Blocked × IsometricEffortMultiplier` 得到有效努力距离。猫和鱼都乘同一个 `StrengthPerKilogram` 标准努力强度、各自消耗系数及性格阶段倍率；不是分别乘各自绝对力量，也不是从统一拉力积分出的机械功。

Locked 绷紧时，鱼的向外意图形成猫的等长保持意图，所以零位移仍可消耗体力；收线或主动后退已给出猫端意图时不重复叠加保持距离。FreeSpool 在 `L_max` 内确实解除约束时恢复主猫体力，到最大线长重新绷紧后停止恢复。鱼力竭后直接关闭所有猫端正向体力扣费，这是仍然生效的玩法特例。

### 当前鱼竿旋转

`FCatFishingRodResistanceModel::Evaluate()` 用 `鱼力量 × LineLoad × NormalizedTension × 玩法杆长（米）` 估计最大鱼端转矩。`StepRotation()` 将猫朝瞄准方向的有限转矩与 `cross(杆方向, 牵引方向) × 最大鱼端转矩` 相加，通过阻尼得到角速度并推进实际杆朝向。`TorqueBalanced` 只用于诊断；没有全方向零速锁定。鱼线转矩的输入仍是上述负载估计，尚未接入共同约束反力。

### 鱼线为什么会垂、什么时候会绷紧

模拟不再把“鱼到竿尖的距离”直接当作线长，而是分别记录：

- `L_paid`：已经放出去的实际线长；左键只负责主动收短，右键按住时只允许鱼向外游动被动带出更多线。
- `D`：竿尖到鱼的直线距离，由鱼的实际位置决定。
- `Slack=max(L_paid-D, 0)`：余线；大于 0 时 Cable 平滑增加本地重力，形成垂坠。
- `TensionCentimeters`：双方候选端点距离超过 `L_paid` 的约束误差。体力、牵引、杆转矩和本场鱼线磨损按前述各自公式读取它或其归一化值。

因此不按任何键时，`L_paid` 保持不变：鱼向内游会自然产生余线，鱼向外游先吃掉余线，碰到线端才绷紧并让猫产生等长保持消耗。按住右键相当于打开线杯，它本身不会制造余线；鱼向外游到线端后，`L_paid` 才随鱼距增长，鱼静止或向内游时不会增加。只要还没到 `L_max` 且候选距离未被最大线长截断，松线状态解除约束并让猫恢复体力；整根线被带完后继续外冲会重新形成张力并停止恢复。Cable 粒子只在各客户端本地模拟，网络只复制上述几个紧凑标量和端点 Actor，不逐粒子同步。客户端用 60Hz 平滑锚点追赶 Hook 的权威/复制位置，并连续插值 CableLength 与 Slack 重力；Cable 使用固定子步和固定求解次数，避免收线时出现 20Hz 压缩阶跃与刚度模式跳变。

## FishLogic 4：为什么多人看到一致

入口：`ACatFishEncounterActor::ApplyFightStepFromAuthority()`。

随机流、方向、公式和水域修正只在服务器执行。服务器最终写入：

- Actor Transform：通过 UE Movement Replication 同步。
- `FishLineAlignment`：鱼方向和鱼线方向的点积。
- `NormalizedLineLoad`：0～1 的鱼游向向外负载比例，不是实际拉力。
- `bStrongConfrontation`：是否已经进入强对抗。
- `MotionIntent`：发力、休息或力竭侧翻收近。

客户端不再各自随机一次，否则同一条鱼会在每台机器上向不同方向游。AnimBP/UI 只消费这些复制后的事实。

## FishLogic 5：体力归零、侧翻与收近

相关入口：

- `FCatFishingFightSimulator::Step()`：判定 `FishExhausted`。
- `Cat.Fishing.FishExhausted`：把 Session StateTree 从 `HookedFight` 切到 `ExhaustedReelHold`。
- `UCatFishingFightRunner::SetFishExhaustedFromAuthority()`：关闭鱼行为树并改为 `AutoHauling`，但不停止固定步。
- `FCatFishingFightSimulator::Step()`：继续用同一个双体约束处理收线与鱼的位置。
- `ACatFishingSession::SpawnExhaustedFishPickupFromAuthority()`：到达投影后在原位置生成所有玩家都可拾取的鱼。

鱼体力结算后不高于平衡资产的 `FishExhaustionThreshold` 时，服务器把尾数吸附到 0 并进入 `ExhaustedReel`；该值是绝对体力阈值，不是百分比。排查时以权威日志的剩余体力与转换事件为准，不能用 HUD 四舍五入后的显示代替实际值。

鱼体力清空的同一服务器帧保留 Encounter 当前位置，并把 `AutoHauling` 复制给所有客户端驱动侧翻。左键按住状态和输入序号跨阶段保留；鱼随后仍由原 Runner 的线长约束逐步靠近竿尖 XY。只有确认落在真实干地、并进入竿尖水平完成距离后才在鱼当前位置生成 Pickup；水面上的鱼不会仅因靠近竿尖就交接。确认上岸也会直接清空鱼体力并进入同一力竭生命周期。这些收尾条件是当前玩法规则，不能作为“所有结果已由物理产生”的证据。

## 常用调参位置

| 想改变什么 | 位置 |
|---|---|
| 发力/休息持续时间 | 正式 `/Game/Catfishing/Data/Fish/Fight_*` 性格的 Calm/Struggle Duration |
| 满体力/力竭时向内概率 | `FullStaminaInwardProbability` / `ExhaustedInwardProbability` |
| 向内扇区和概率增长曲线 | `InwardConeHalfAngleDegrees` / `InwardProbabilityExponent` |
| 多久换一次目标方向 | `DirectionRetargetDurationRangeSeconds` |
| 转弯灵活程度 | `MaximumTurnRateDegreesPerSecond` |
| 爱向外冲还是横切 | `StruggleOutwardDirectionBias` / `LateralMovementBias` |
| 假装回头的概率 | `FeintProbability` |
| 多大夹角算强对抗 | `StrongConfrontationAlignmentThreshold` |
| 强对抗要持续多久 | `StrongConfrontationConfirmationSeconds` |
| 鱼重量范围和总搏斗体力 | 当前从正式鱼库选中的 `UCatFishDefinition` |
| 平静/挣扎自由游速与体力阶段倍率 | 当前鱼种性格的 `CalmMovementSpeedCentimetersPerSecond` / `StruggleMovementSpeedCentimetersPerSecond`、`BaseDrainMultiplier` / `StruggleDrainMultiplier` |
| 体重→力量、力量→加速度 | `DA_FishingFightBalance_Default` 的“每公斤力量 / 每点力量加速度 / 驱动力响应时间” |
| 猫力量和体力 | `Cat_Default`（或当前角色绑定的 `UCatCharacterDefinition`） |
| 收近速度和耗尽吸附阈值 | `DA_FishingFightBalance_Default` 的“收线速度 / 鱼力竭吸附阈值” |
| 张力表现达到满值的响应范围 | `DA_FishingFightBalance_Default` 的“满张力响应距离” |
| 钓组承载、本场鱼线耐久、杆长与装备侧磨损参数 | 当前鱼竿引用的 `UCatEquipmentDefinition`；运行时 ID 见 `fishing_fight_started` 的 `RodDefinition`，默认起始 ID 为 `StarterRodT1` |
| 全局努力距离、恢复、磨损和约束系数 | `/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default`；INI 仅保留 `FightBalanceDefinition` 引用 |
| 持竿最大角速度、响应时间与身体俯仰范围 | `UCatFishingSettings` 的 `HeldRod*` 字段 |

## 实测时怎么判断卡在哪层

过滤 `LogCatFishing`，优先按 `SessionId` 串联，再用 `RodActorId` 对齐鱼竿日志：

1. 用 `fishing_fight_started` 确认当前鱼、竿、平衡资产 ID 与实际参数。
2. 用 `fishing_fish_exhausted` 区分 `Cause=StaminaDepleted` 和 `Cause=ShoreLanding`；显示体力为零不等于已发生权威转换。
3. 收线/拖动问题对照 `fishing_coupled_work_sample`、`fishing_constraint_sample` 的操作、收线量和端点修正，以及 `fishing_carrier_smoothing_sample` 的目标/应用速度。杆回转看 `fishing_rod_rotation_resistance_sample`。
4. 上岸与交接看 `fishing_fish_beached`、`exhausted_fish_pickup_spawned`；生成被拒看 `exhausted_fish_pickup_rejected` 的 `Reason`。生成前必须同时确认真实干地和竿尖水平距离。

Win64 Development 包无需 `-log` 也应将事件写入 `<打包根目录>/Catfishing/Saved/Logs`。联机验收分别核对房主与客户端的新日志；源码核对或 Automation 通过只提供 contract/runtime_behavior 证据，不能替代 presentation_delivery 验收。
