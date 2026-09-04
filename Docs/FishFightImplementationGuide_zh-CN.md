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
      3. FCatFishingFightSimulator：合并运动与卷线意图，计算约束修正、努力距离、体力和鱼竿磨损增量
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

主猫仍持竿、体力恰好为零且没有助手实际贡献合力时，`ShouldEscapeExhaustedCat` 接管本步为持续外冲。Runner 保存 StateTree 当前请求意图而不另建计时器，把实际发布意图覆盖为 `StrugglingOutward`；Steering 不再抽休息、假动作或低体力内游，平滑转向远离猫身体的方向，保留真实岸线反馈的短期水内避让。助手出力、接力者恢复正体力、离竿或鱼已力竭后交回原树意图与按键状态，不能因为某次树转到平静就提前停止拖拽。

力竭外冲速度为 `max(鱼平静游速, 鱼挣扎游速) × ExhaustedCatEscapeSpeedMultiplier`，正式平衡资产默认倍率 2，已有资产通过新字段默认值获得配置。此时锁线，不允许右键放线回体；猫承担沿线约束位移，牵引目标速度允许达到外冲速度，避免小鱼质量把拖动压到近乎静止。鱼端修正上限同时覆盖外冲步幅，避免高速造成线长误差持续累积。这是用户要求的拖落水玩法规则，不是对鱼力量或猫质量做第二次修改；角色仍通过原有移动与碰撞落位，不瞬移或强行越过障碍。

拖拽期间鱼无自由游动耗体，不新增竿磨损，不积累强度过载，也不以最大鱼距提前逃脱；已有真实坏竿仍按原终局处理。猫脚点达到 35 cm 危险水深并持续 0.2 秒后，Condition 的真实水深查询触发 `CatInWater`，停止 Runner 并复用现有落水表现。运行日志筛选 `LogCatFishing/Event=fishing_exhausted_cat_escape_changed`、`fishing_drag_water_entered`，结合既有牵引、终局和角色水深日志核对。动画资产仍使用项目现有绑定，不能把自动化的水深到达当作画面验收。

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

活鱼不会因为自己的游动直接冲上陆地。若鱼只是自行撞岸，Runner 用水域查询的最近岸点与入水方向阻止继续向陆地的法向位移，同时保留本步真实的朝水内位移与沿岸切向滑动，并由 `RedirectFromWaterBoundary()` 调整游向。即使活鱼已被拖到烘焙轮廓外、真实岸面前的间隙，松开拖行后仍能逐步游回；岸线容差带内的小步回水也不能被最近岸点覆盖。入水与切向合成后的步幅不超过原始候选位移，不借边界投影瞬移回湖；真实拖拽候选不经过这个防自游出水分支。Development 日志 `fishing_shore_recovery` 记录接触/结束及限频采样，可按 `SessionId` 对比 `CandidateWaterwardCm`、`ResolvedWaterwardCm`、`CatAction` 和 `LineLengthCm`，区分岸线校正与鱼线牵制；转向失败记 `fishing_shore_recovery_rejected`。

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

鱼的个体重量由服务器选择流程冻结，乘平衡资产的 `StrengthPerKilogram` 得到基础力量，再应用完美中鱼的力量倍率。猫的基础 `FishingStrength / StrengthPerKilogram` 是当前玩法等效质量；主位和辅助位只要有正体力，可用力量就取 ASC 当前 `FishingStrength` 原值，不按剩余体力比例衰减。体力恰好归零才停止主动出力，恢复正体力后恢复完整力量；辅助位仍须按住左键才贡献合力。质量不随体力降低。`CharacterMovement.Mass` 不作为这套分配公式的猫质量输入。

1. 鱼直接以性格资产的平静/挣扎游速乘固定步长生成水平候选位移；没有由鱼力量积分得到自由游速，也没有持久鱼速度状态。
2. 活鱼阶段猫端收线速度取 `min(ReelSpeedCentimetersPerSecond, 有效猫力量 × AccelerationPerStrength × DriveResponseSeconds)`；鱼力竭后的免耗体收尾直接使用 `ReelSpeedCentimetersPerSecond`，猫体力为零也能继续收回。两种阶段都要求有操作手且按住左键，按剩余可收长度生成请求并直接缩短 `L_paid`，保留垂直距离下限及同一端点/地形约束。模拟器把 `ActualReelDistanceCentimeters` 设为请求距离，没有按鱼线负载求卷线器停转。
3. 猫的移动输入形成端点候选位移，与鱼候选和已放线长一起生成约束误差。只有鱼的向外对抗加速度高于猫时，其差值才按双方质量分配猫端牵引；猫更强或鱼力竭时，修正主要落到鱼端。相等力量下猫端牵引为零是这套公式的性质，不代表已经完成地面摩擦与合力求解。
4. `ACatFishingRodActor::ApplyCarrierConstraint()` 在 CharacterMovement 完成移动后平滑追赶目标，补足向鱼速度并限制远离方向速度；这不是共同拉力在角色运动中的积分。

`bStalemate` 在求得候选修正位置后按径向位移阈值生成，只供诊断，不锁位置。`bStrongConfrontation` 则由向外比例阈值与持续时间确认，仍参与过载断线：只有它成立且 `max(鱼向外力量, 收线时猫有效力量) × NormalizedTension >= RodStrength` 才报告 `StrengthOverload` 并以 `LineBroken` 结束本场。鱼竿耐久耗尽走 `RodBroken` 真损坏分支，不要求强对抗；磨损按 `LineLoad` 缩放。不要把“强对抗仅用于表现”写成当前事实。

### 当前鱼竿耐久归属

耐久只有 `Equipment` 中绑定 `RodItemInstanceId` 的一份实例事实。新会话读取它的实际剩余值；模拟器每个固定步算出磨损增量，由权威装备入口写回同一实例，Session 的 `RodDurabilityRemaining` 只用于复制这一剩余值。辅助位、主位换人和无人值守不会改变受损鱼竿实例；取消、主动切线、收杆和下一次抛竿也不会退还既有磨损。不存在每场补满的独立鱼线耐久池。

耐久归零时，装备实例和对应场景鱼竿进入损坏状态，不能再次抛竿。强度过载的 `LineBroken` 只结束本场，但已经写入的鱼竿磨损仍保留。既有维修领域入口处理同一装备实例，购买获得另一件新实例；本轮不把维修领域代码的存在视为正式玩家维修交互已验收。`MaximumRodDurability` 是新鱼竿和维修的上限，不是会话启动值。起始竿定义基线仍为 150，唯一配置脚本为 `Scripts/configure_starter_rod_tuning.py`，本轮不重平衡资产。

### 当前体力公式

猫的做功与持续支撑现在分别结算，鱼保留独立的对抗努力模型。本轮只替换体力观察量与价格，不改变鱼线约束、卷线长度求解、猫端牵引、鱼游速或有向转矩积分。体力变化会改变进入力竭的时间，但相同输入与力量下的物理求解保持原路径。

`FCatFishingFightWorkModel::ComputeCatWorkDrain()` 使用 `标准做功量 × 对应单位单价 × 动作倍率 × (CatUnloadedWorkMultiplier + 自身归一化负载 × CatLoadStaminaMultiplier)`。线性做功量为 `StrengthPerKilogram × 已完成的主动沿线位移`；移动仍从 CharacterMovement 的已接受加速度判断主动意图，并只使用身体速度，实际量封顶于对应主动意图。收线使用原求解器给出的本步卷线量，目前仍等于请求量；本轮不加入负载限速。转杆做功量为 `StrengthPerKilogram × ∫max(0,角速度·主动转矩方向) × 主动转矩/自身容量 dt`，使用独立弧度单价。反向被拖不计正功，鱼助力时也只按猫实际施力比例计费。猫不再使用鱼的 Base/StruggleDrainMultiplier，实际负载变化本身已参与结算。这是标准强度下的玩法做功量，并非完整牛顿约束或机械焦耳模型。

转杆另记录 `ExertionSquaredSeconds=∫(主动转矩/自身容量)²dt`，不再以最大转速和一米参考力臂生成虚拟计费距离。转矩积分与角速度计算本身保持原样；`Epoch`、累计积分时长与 `FCatFishingRodEffortSampler` 仍将每帧观察量分配到固定步，补步不重复消费，换人或新搏斗清除旧积压。原意图/实际弧长字段及对应日志已移除，用力时间和正功弧度分别有明确单位。

共享沿线支撑为 `CatSupportStaminaPerSecond × dt × 猫沿线归一化负载² × CatHoldStaminaMultiplier`，仅在锁线受载时结算。主位转杆支撑候选为 `CatSupportStaminaPerSecond × ExertionSquaredSeconds × CatRodStaminaMultiplier`。总支撑取两者较高值：共享沿线支撑由实际合力者按力量分担，超出共享支撑的转杆部分只由主位承担。移动、收线、转杆正功费用另行相加，不能抵扣支撑；因此微小操作与不可支付的个人费用均不能免掉助手的支撑费用。主位力竭时不产生移动/转杆费用，辅助不能赋予主位免费转杆能力。

新字段通过正式平衡资产默认值接入既有资产：`CatRodStaminaCostPerStrengthRadian=0.03`、`CatUnloadedWorkMultiplier=0.15`、`CatSupportStaminaPerSecond=2.0`，均可独立配置非负有限值。猫线性做功单价、动作和负载倍率继续读取已有资产值。新字段无需重置整份资产，用户既有调参保留。

正常遛鱼按住右键时，主位按 `SlackStaminaRegenPerSecond` 持续恢复至上限，同时关闭全部猫端与鱼端耗体；移动、转杆、助手合力、是否真正解除约束、是否达到最大线长均不影响此规则。左右键同时按住时右键优先，松右键后恢复仍按住的左键收线；旧输入序号仍被拒绝。无人值守放线不会恢复旧操作手体力。零体力已经进入强制拖拽时，仍锁线且不能通过右键回体退出，沿用前述拖落水规则。鱼力竭后关闭所有猫端正向扣费，继续允许收鱼和右键回体。

猫沿线负载为 `约束张力 × clamp(鱼向外力量/猫有效合力,0,1)`；转杆负载另按鱼端力矩与主位转矩容量计算。鱼的负载为 `约束张力 × max(鱼向外对齐度,0) × clamp(猫有效合力/鱼力量,0,1)`，解除约束时均归零。鱼自身游动不扣体力，正常右键回体期间也不扣体；其余时段只有自身沿线努力受到猫端约束时才按 `标准努力强度 × 有效努力距离 × 鱼消耗系数 × 阶段倍率 × 鱼归一化对抗负载 × FishLoadStaminaMultiplier` 结算。单猫体力归零且没有助手出力时，鱼对抗负载为零，不会继续靠游动耗尽；有正体力且按住拉线的助手仍可提供合力，使鱼在非右键、绷线向外对抗时耗体。被拖往意图反方向的位移不计主动完成量，完全被动拖动也不会凭空增加鱼的主动努力。

鱼仍由 `FCatFishingFightWorkModel::ComputeDrain()` 计算 `Realized=min(实际完成距离,主动意图距离)`、`Blocked=max(主动意图距离-Realized,0)`，以 `Realized + Blocked × IsometricEffortMultiplier` 得到有效对抗努力；该受阻距离模型现在只服务鱼，资产字段显示名同步为“鱼受阻努力折算倍率”。鱼调用时 `BaseEffortMultiplier=0`，自由游动没有基础费用；`FishLoadStaminaMultiplier=0` 可关闭鱼对抗耗体。猫负载倍率为零只关闭实际做功的负载附加部分，基础操作与时间支撑仍由各自参数控制。关闭猫全部费用需要关闭线性单价、转杆单价与每秒支撑三项。

`create_fishing_fight_balance_asset.py` 只对新资产写默认值，已有资产只校验并记录实际配置，非法旧资产明确拒绝。运行证据使用 `LogCatFishing/fishing_effort_configured` 的 `Model=CatActualWorkAndTimedSupport` 与三项新参数；每秒 `fishing_coupled_work_sample` 的 `RodWorkDrain`、`RodSupportExtraDrain`、`HoldDrain` 分别表示转杆正功、超出共享部分的转杆支撑和共享支撑，`RodDrain` 等于前两者之和。旋转观察量用 `RodExertionSquaredSeconds/RodPositiveWorkRadians` 核对。客户端仍用 `fishing_cat_stamina_received/fishing_fish_stamina_received` 检查复制到达，按 SessionId/PlayerId 关联，单端日志不能证明联机验收。

### 当前鱼竿旋转

`FCatFishingRodResistanceModel::Evaluate()` 用 `鱼力量 × LineLoad × NormalizedTension × 玩法杆长（米）` 估计最大鱼端转矩。`StepRotation()` 先对 `牵引方向 × 最大鱼端转矩` 这个有向负载作跨帧指数插值，再将 `cross(杆方向, 平滑后的有向负载)` 与猫朝瞄准方向的有限转矩相加，通过阻尼得到角速度并推进实际杆朝向。大小和方向一起平滑，避免鱼游向或 20 Hz 松绷线切换直接造成角速度阶跃；不对欧拉角、视觉 Mesh 单独造第二份姿态，也不增加容易过冲的旋转惯量。鱼线端点继续读取这份权威实际姿态。

`HeldRodFishPullSmoothingSeconds=0.15` 是有向负载的响应时间常数（约 0.15 秒完成 63% 的变化），与猫端瞄准响应 `HeldRodAngularResistanceResponseSeconds=0.08` 分开。亚步使用中点负载、保存终点历史，保持不同帧率下响应一致；只影响过渡，不改变恒定负载下的平衡角。历史不会随猫端移动 `bActive` 或一次零负载目标清空；会话清约束、力竭清驱动力、换持有者或落地时清空，避免旧鱼负载带到新状态。`fishing_rod_rotation_resistance_sample` 保留原始 `MaximumFishTorque/PullAxis`，另记 `AppliedFishPull/FishPullSmoothingSeconds`，可按同一 `RodActorId` 与约束日志对照。

`TorqueBalanced` 只用于诊断；没有全方向零速锁定，也没有阻力角度上限或锁定半径。鱼线转矩的输入仍是上述负载估计，尚未接入共同约束反力。

### 鱼线为什么会垂、什么时候会绷紧

模拟不再把“鱼到竿尖的距离”直接当作线长，而是分别记录：

- `L_paid`：已经放出去的实际线长；左键只负责主动收短，右键按住时只允许鱼向外游动被动带出更多线。
- `D`：竿尖到鱼的直线距离，由鱼的实际位置决定。
- `Slack=max(L_paid-D, 0)`：余线；客户端按平滑已放线长与端点距离生成等弧长下垂曲线，余线越多弧度越大。
- `TensionCentimeters`：双方候选端点距离超过 `L_paid` 的约束误差。体力、牵引和杆转矩按前述各自公式读取它或其归一化值；鱼竿磨损按实际 `LineLoad` 缩放，不用几何张力替代向外负载。

因此不按任何键时，`L_paid` 保持不变：鱼向内游会自然产生余线，鱼向外游先吃掉余线，碰到线端才绷紧并让猫产生等长保持消耗。正常按住右键相当于打开线杯，它本身不会制造余线；鱼向外游到线端后，`L_paid` 才随鱼距增长，鱼静止或向内游时不会增加。只要还没到 `L_max` 且候选距离未被最大线长截断，松线状态解除约束；整根线被带完后继续外冲会重新形成张力，但正常右键的回体和双方免耗体继续有效。零体力强制拖拽依旧优先锁线。曲线网格只在各客户端本地生成，网络仍只复制上述标量和端点 Actor。客户端保留 60Hz 锚点与已放线长平滑，再由 FCatFishingLineCurve 计算弧长匹配的下垂形状，用一个细管网格绘制；没有粒子速度、重力积分或惯性回弹。接近垂直/重合的端点连续加入小幅侧向开口，避免折返叠线或方向翻转。曲线不提供碰撞或约束反力，服务器线长、张力与牵引系统不读取显示网格。

鱼线表现调参统一位于项目设置 `Catfishing Fishing Presentation → FishingLine`（`Config/DefaultGame.ini` 的同名配置节）：`FishingLineCurveSegments=64` 控制曲线采样精度，`FishingLineWidthCentimeters=1.25` 控制粗细，原有端点/线长插值速度继续有效。旧 Cable 段数、物理子步、求解次数、重力与 Slack 重力插值参数已删除。正式鱼钩蓝图由 `Scripts/migrate_fishing_line_curve.py` 编译重存，保持可视锚点和 Mesh 资源；Development 日志改用 `LogCatFishing/Event=fishing_line_curve_configured`（含 Renderer、Session、CastAttempt、Segments、WidthCm、PaidLengthCm、CurveLengthCm），非法几何只在进入失败状态时记录 `fishing_line_curve_rejected`，不会逐帧刷屏。

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

本步确实产生正的鱼对抗耗体、且扣除后剩余体力不高于平衡资产的 `FishExhaustionThreshold` 时，服务器把尾数吸附到 0 并进入 `ExhaustedReel`；该值是绝对体力阈值，不是百分比。零耗体步骤不能仅因剩余值已低于阈值就吸附归零，鱼自由游动或关闭鱼耗体系数时仍保留原有体力。排查时以权威日志的剩余体力与转换事件为准，不能用 HUD 四舍五入后的显示代替实际值。

鱼体力清空的同一服务器帧保留 Encounter 当前位置，并把 `AutoHauling` 复制给所有客户端驱动侧翻。左键按住状态和输入序号跨阶段保留；主位与辅助位在 `ExhaustedReel` 新按下/松开左键仍写入原 Runner，主位右键按下/释放也继续有效。鱼随后仍由原 Runner 的线长约束逐步靠近竿尖 XY；该收尾免耗体且不再受猫剩余搏斗力量限制，阶段切换不自动补满猫体力，按右键仍按配置逐步回体。只有确认落在真实干地、并进入竿尖水平完成距离后才在鱼当前位置生成 Pickup；水面上的鱼不会仅因靠近竿尖就交接。确认上岸也会直接清空鱼体力并进入同一力竭生命周期。这些收尾条件是当前玩法规则，不能作为“所有结果已由物理产生”的证据。

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
| 钓组承载、鱼竿耐久上限、杆长与装备侧磨损参数 | 当前鱼竿引用的 `UCatEquipmentDefinition`；运行时 ID 见 `fishing_fight_started` 的 `RodDefinition`，默认起始 ID 为 `StarterRodT1` |
| 鱼竿当前剩余耐久与损坏状态 | `Equipment` 中绑定 `RodItemInstanceId` 的实例；Session 只复制同一值，不按定义上限补满 |
| 全局努力距离、恢复、磨损和约束系数 | `/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default`；INI 仅保留 `FightBalanceDefinition` 引用 |
| 持竿最大角速度、响应时间与身体俯仰范围 | `UCatFishingSettings` 的 `HeldRod*` 字段 |

## 实测时怎么判断卡在哪层

过滤 `LogCatFishing`，优先按 `SessionId` 串联，再用 `RodActorId` 对齐鱼竿日志；跨场磨损和换人接力必须继续对齐同一个 `RodItemInstanceId`：

1. 用 `fishing_fight_started` 确认当前鱼、竿、平衡资产 ID 与实际参数。
2. 用 `fishing_fish_exhausted` 区分 `Cause=StaminaDepleted` 和 `Cause=ShoreLanding`；显示体力为零不等于已发生权威转换。
3. 收线/拖动问题对照 `fishing_coupled_work_sample`、`fishing_constraint_sample` 的操作、收线量和端点修正，以及 `fishing_carrier_smoothing_sample` 的目标/应用速度。杆回转看 `fishing_rod_rotation_resistance_sample`。
   右键回体在 `fishing_effort_configured` 中记录 `SlackRecoveryMode=RightButtonExceptExhaustedDrag` 和实际恢复速度；每秒工作采样用 `SlackRecovery`、`CatRecovery`、四项 `Drain` 和 `FishStaminaDrain` 核对。有回体时 `GroupStaminaDrain` 为负，上限处为零；`fishing_exhausted_cat_escape_changed` 仍用于辨认优先锁线的力竭拖拽。
4. 上岸与交接看 `fishing_fish_beached`、`exhausted_fish_pickup_spawned`；生成被拒看 `exhausted_fish_pickup_rejected` 的 `Reason`。生成前必须同时确认真实干地和竿尖水平距离。
5. 耐久对照 `LogCatEquipment` 的 `equipment_rod_wear_applied/rejected`、`equipment_rod_durability_replicated`，以及 Session 镜像与 `ROD Durability` 面板。磨损按 `SessionId + RodItemInstanceId + WearSequence` 关联，跨场继续追踪同一实例；维修看 `equipment_rod_repair_result`。区分强度过载的 `LineBroken` 和鱼竿耐久归零的 `RodBroken`，必须证明下一场沿用剩余耐久，不能用新实例满耐久代替跨场验证。

Win64 Development 包无需 `-log` 也应将事件写入 `<打包根目录>/Catfishing/Saved/Logs`。联机验收分别核对房主与客户端的新日志；源码核对或 Automation 通过只提供 contract/runtime_behavior 证据，不能替代 presentation_delivery 验收。
