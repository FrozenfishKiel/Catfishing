# 鱼运动与遛鱼逻辑实现导读

本指南解释当前鱼、线、杆、猫的生产调用链，核对日期：2026-09-07。进度和验收缺口只维护在 [需求对齐差距清单](Development/需求对齐差距清单.md)。

当前使用共同鱼线张力、有限出力收线和持久鱼速度。猫端由 CharacterMovement 执行真实移动与碰撞，鱼端使用服务器固定步；这是交错推进的约束模型，尚不是完整的三维刚体/接触摩擦求解器。鱼仍在水面平面上求解，再由现有地形入口解析岸线和坡面。不可满足的竿尖高差保留几何误差，不凭空抬鱼或放线。

## 一条完整调用链

```text
玩家左/右键 → Ability/CommandComponent → Session 验证和权威状态机
  → FightRunner 固定步（当前 0.05 s）
      StateTree 决定发力/平静，Steering 产生连续游向
      Simulator::Step 积分鱼速度、求线张力、按出力确定实际收线
      ResolveFishSurfaceFromAuthority 解析水面/岸线/地面
      Simulator::FinalizeResolvedStep 从最终落点和线长重算费用、终局与猫端反力
      RodResistanceModel 从共同张力和最终线方向计算杆转矩
      Rod 发布约束输入 → CatCharacterMovementComponent 速度积分、碰撞、SavedMove 重放
      Runner 单次支付 ASC → Encounter 应用位置并复制 → Session 写同一装备实例磨损
  → 力竭/上岸后仍由同一 Runner 拖到真实干地，进入原 Pickup/捕获入口
```

移动重放只恢复该移动步保存的牵引方向、加速度、速度上限和来源 ID。它不调用 Runner、ASC、装备、随机数或捕获事务。服务器仍只接受自己的受力裁决；客户端没有上传自报力量或张力的权利。受力期间不合并 SavedMove，避免改掉原碰撞积分步长；这会增加该阶段的移动记录/网络开销，需要打包联机测量。

## FishLogic 1：发力与休息节奏

入口：`ST_FishFight` → `FCatFishBehaviorStateTask` → `UCatFishingFightRunner::BeginBehaviorStateFromStateTree()`。

鱼在两个大状态间切换：

- `StrugglingOutward`：发力期，通常更倾向远离鱼竿。
- `CalmOrInward`：休息期，通常更倾向靠近鱼竿。

持续时间从鱼性格 DataAsset 的区间中抽取。鱼体力较低时休息期会变长。这里仅决定“情绪/意图”，不决定世界方向。

主猫仍持竿、体力恰好为零且没有助手实际贡献合力时，`ShouldEscapeExhaustedCat` 接管本步为持续外冲。Runner 保存 StateTree 当前请求意图而不另建计时器，把实际发布意图覆盖为 `StrugglingOutward`；Steering 不再抽休息、假动作或低体力内游，平滑转向远离猫身体的方向，保留真实岸线反馈的短期水内避让。助手出力、接力者恢复正体力、离竿或鱼已力竭后交回原树意图与按键状态，不能因为某次树转到平静就提前停止拖拽。

力竭外冲速度为 `max(鱼平静游速, 鱼挣扎游速) × ExhaustedCatEscapeSpeedMultiplier`，正式平衡资产默认倍率 2，已有资产通过新字段默认值获得配置。此时锁线，不允许右键放线回体。鱼推力额外获得 `猫系统质量 × ExhaustedCatTowAccelerationCentimetersPerSecondSquared / 100` 牛顿的玩法辅助力；该力仍通过同一鱼线张力传到猫，猫速度逐渐增加，上限为外冲游速。默认辅助加速度为 300 cm/s²。角色由 CMC 碰撞落位，障碍阻挡后不预支猫位移；这是显式的拖落水玩法政策，并非小鱼自身力量突然增长。

拖拽期间鱼无自由游动耗体，不新增竿磨损，也不以最大鱼距提前逃脱；已有真实坏竿仍按原终局处理。猫脚点达到 35 cm 危险水深并持续 0.2 秒后，Condition 的真实水深查询触发 `CatInWater`，停止 Runner 并复用现有落水表现。运行日志筛选 `LogCatFishing/Event=fishing_exhausted_cat_escape_changed`、`fishing_drag_water_entered`，结合既有牵引、终局和角色水深日志核对。动画资产仍使用项目现有绑定，不能把自动化的水深到达当作画面验收。

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

## FishLogic 3：共同张力、惯性与有限收线

代码入口为 `Fishing/Simulation/CatFishingFightSimulator.{h,cpp}`。世界坐标、速度与加速度使用 cm、cm/s、cm/s²；质量使用 kg，力使用 N。`ForcePerStrengthNewtons` 默认 1 N/力量，不能把已废弃的“每点力量 5 cm/s²”数值搬过来。

鱼基础力量仍由冻结重量乘 `StrengthPerKilogram` 生成，再应用既有中鱼倍率。猫质量改为独立的 `CatBodyMassKilograms`（默认每猫 5 kg），助手按住出力键时加入系统质量。正体力保持完整力量、恰好零体力停止出力的规则不变。力量成长不再同时让猫变重；CMC 的引擎推挤 Mass 不参与该参数。

鱼保留 `FishVelocityCentimetersPerSecond`。设主动推力 F、质量 m、目标自由游速 u，采用线性水阻 `d = F/u`（u 换算为 m/s，阻力分母有数值下限；目标游速为零时关闭主动推力），通过隐式积分更新：

```text
m_effective = m + dt × d
v_free_cm/s = (m × v_previous_cm/s + 100 × dt × F_N) / m_effective
x_free_cm   = x_actual_cm + dt × v_free_cm/s
```

力与阻力共同决定到达目标速度的过程，小鱼自由游速仍渐近性格资产值；换向不会瞬间反转已有惯性。Runner 只把 Encounter 和地形实际执行后的位移写回下步速度。鱼力竭时清除游动速度，继续沿用既定的无自主漂游收尾规则。

线约束采用单向拉力。竿尖到鱼水面的高差为 h，已放线长为 L，可行水平半径为 `L × sqrt(max(1 - (h/L)², 0))`。先求鱼候选位置超出这个半径的水平距离，再按修正速度预算收近；不会把三维超长厘米数直接当水平位移。张力由实际水平约束冲量和有限步长平均力臂反算：

```text
mobility_cm/N = 100 × dt² / m_effective
T_N = actual_horizontal_correction_cm / (mobility_cm/N × average_horizontal_line_fraction)
```

接近纯竖直时力臂有数值下限。普通帧靠修正预算避免瞬移，超出线长的不可行高差仍作为误差输出。该层不求鱼的垂直浮力，也不求猫与地面的法向力或静/动摩擦系数；地面阻挡由 CMC 执行，沿线支撑能力仍由猫力量及既有杆杠杆规则给出。

左键产生 `ReelSpeedCentimetersPerSecond × dt` 的请求。模拟器用同一个约束函数寻找不超过猫可用支撑/卷线力的缩短量：负载超过出力时停转，有余力才实际缩短。`RequestedReelDistanceCentimeters` 与 `ActualReelDistanceCentimeters` 明确分开。力竭鱼使用独立的 `ExhaustedReelForceNewtons`（默认 200 N），保持猫零体力也能免耗体回收，仍不超过配置收线速度。

猫端不使用移动意图构造虚拟竿尖。它只读取 Rod 当前真实端点，最终水平反力为 `max(T × 最终线方向水平比例 - 猫可用支撑力, 0)`，再除以猫质量换成加速度。Rod 只转交输入；`Character/CatCharacterMovementComponent` 在速度积分阶段应用它，由后续 CMC 碰撞/滑动生成位置。旧 Rod Tick 补速度、质量份额分配位移、背离方向速度硬截断均已退出生产链。

`NormalizedLineLoad = pow(max(dot(鱼努力方向, 水平向外方向), 0), AngleStrengthExponent)` 继续供鱼表现和既有方向性磨损规则使用，不能冒充真实张力。`LineTensionNewtons` 是力；`NormalizedTension = clamp(T / DisplayTensionNewtons, 0, 1)` 仅是表现刻度。`TensionCentimeters/ConstraintErrorCentimeters` 仍表示几何误差。强对抗、僵持标记只观察结果，不锁位置、不裁决断线。

### 最终费用和耐久

`FinalizeResolvedStep` 从输入状态和最终落点重算，可重复调用但不会累计费用或写资源。Runner 地形解析后调用一次，随后仍由原 ASC/Equipment 权威入口支付。冻结本步 `FishEffortDirection`，防止岸线反馈修改下步 Steering 时反写本步努力方向。

猫保留原玩法标准做功价格，未把单价冒充焦耳价格：

- 移动和收线按 `StrengthPerKilogram × 完成的主动厘米数` 计价，移动意图只用于识别主动做功，不能凭受阻输入收费；收线按实际完成量收费。
- 转杆按独立的正功弧度单价计费，转矩积分的 Epoch 与累计时长继续防止换人或补步重复消费。
- 共享支撑按 `CatSupportStaminaPerSecond × dt × 自身相对负载²`，转杆只补超过共享支撑的部分。停转没有收线正功，仍可能有持竿支撑费用。
- 鱼只支付自身对抗努力，自由游动免耗体；负载取共同张力相对自身推力，实际主动位移不得把被拖向反方向算成主动做功。
- 正常右键恢复猫体力并免除双方费用；零体力强制拖拽优先。鱼已力竭后回收仍免猫耗体。

耐久继续只有 Equipment 中绑定 `RodItemInstanceId` 的一份实例事实，Session 只复制镜像。方向性磨损公式保留，最终解除张力后不按临时负载收费；坏竿优先于同期鱼力竭。取消、换人、收杆、开新会话都不恢复已磨损耐久。初级竿当前正式资产为 500，既有测试仍要求 150；本轮不重新平衡或覆盖该用户资产。

### 鱼竿旋转

`CatFishingRodResistanceModel::Evaluate` 读取同一 `LineTensionNewtons`，不再乘一次鱼力量、游向负载和表现张力。为保持现有旋转参数及复制字段的单位，它将牛顿数除以 `ForcePerStrengthNewtons`，再乘配置杆长（m）得到 `StrengthMeters` 转矩；字段含义没有改为牛顿米。

现有有向阻尼转矩积分、负载平滑、实际竿尖跟随和主动转杆用力采样保持。杆负载使用地形后的线方向；松线、上岸力竭、终局均不发布旧鱼转矩。该层仍没有独立鱼竿转动惯量，不是完整刚体角动力学。

## FishLogic 4：网络与移动重放

服务器决定鱼状态、固定随机流、线长、费用和最终 Transform。拥有客户端接收 Rod 约束用于本地移动；模拟代理使用引擎角色移动复制。`FCatSavedMove::SetMoveFor` 保存每次移动使用的约束，`PrepMoveFor` 为纠正重放恢复它，重放结束恢复读取最新复制输入，旧鱼负载不会覆盖实时输入。换持有人、离竿、清约束和来源销毁会卸载实时牵引。

Rod 的约束快照同时保存 `ConstraintHolderPlayerState`，复制乱序时只能作用于快照对应的持有人；服务器在换主位、离竿、坏竿和收起时立即卸载旧牵引，不能等下一次表现 Tick 才清理。竿尖采样仍排在角色碰撞移动之后。

这保证受力输入参与客户端历史移动重放，不代表已经实现整场物理回滚、服务器按客户端时间戳回溯鱼状态或零延迟网络一致性。仍须在延迟/丢包条件下检查服务器纠正频率、主辅换人、坡面与正式双端手感。自动化碰撞/回放测试属于受控 runtime_behavior，不能替代真人联机验收。

## FishLogic 5：上岸、力竭与收近

原 `ResolveFishSurfaceFromAuthority` 继续解析真实水面、岸线间隙及阻挡坡面。活鱼只有实际收线或身体向岸位移形成有效拖拽时才能上岸力竭，纯甩杆仍不能借少量卷线误触发。鱼干继续使用同一路径；只有真实干地和拾取距离条件同时成立才进入 Pickup。地面暂缺或重新入水会撤回干地资格，不创建第二条捕获路径。

动画、WBP、拾取和持久化入口保持现状，本轮没有新增正式表现资产或 Cook 入口。

## 参数、兼容载荷与诊断

正式数值仍从 `/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default` 唯一读取，`DefaultGame.ini` 保留原软引用。`Scripts/create_fishing_fight_balance_asset.py` 验证并保存显式迁移版本，已有调参不重置。新默认值为每点力量 1 N、单猫质量 5 kg、力竭回收辅助力 200 N、零体力拖行辅助加速度 300 cm/s²、满表现张力 50 N。

以下旧字段没有新模型运行读取，但保留序列化/只读蓝图身份：平衡资产的 `AccelerationPerStrength`、`DriveResponseSeconds`、`TensionResponseRangeCentimeters`、`MinimumCarrierAwaySpeedMultiplier`，以及 Rod/Snapshot 的两个旧背离速度倍率字段（当前恒为 1）。项目蓝图图表引用由 `CatFishingForceMigrationTests.cpp` 审计；旧 `/Game/UI/WBP_CatLakeReach` 的父类 `CatLakeReachWidget` 缺失，部分子对象无法完整加载，故不能宣称所有资产消费者已确认。删除条件是先在编辑器恢复或正式迁移该旧 WBP，核对其属性绑定与外部调用，再移除兼容载荷。没有保留第二套旧模拟器或速度写口。

开发包默认落盘日志分类 `LogCatFishing`：

- `fishing_fight_started`：`StrengthResolution=CommonLineForce`、`ForcePerStrengthN`、`MassMode=IndependentCatBodyMass`。
- `fishing_constraint_sample`：共同 `LineTensionN`、几何误差、最终转矩和牵引加速度。
- `fishing_coupled_work_sample`：请求/实际收线及各项费用；最终结算失败看 `fishing_final_work_rejected`。
- `fishing_carrier_movement_sample`：RodActorId、角色、速度、实际碰撞位移、NetMode/LocalRole；替代旧 `fishing_carrier_smoothing_sample`。
- 原 `fishing_surface_tow`、`fishing_fish_beached`、`fishing_drag_water_entered`、装备磨损及捕获日志继续沿用。

Win64 Development 包应在不加 `-log` 时写入 `<打包根目录>/Catfishing/Saved/Logs`。本轮尚未重新打包、采集新房主/客户端双端日志或验收正式画面，不将代码/受控运行通过写成 presentation_delivery 完成。

## 本轮衔接核对（2026-09-07）

修改前为 `8930c90`，工作区干净；用户此前配置/地图/资产改动保存在 `881fa15`，本轮不覆盖。验证基线为 GeometryReport 的 122 项：119 clean、2 警告、1 既有耐久失败。本表是实现与审查对照，模块进度仍只在需求对齐差距清单维护。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 鱼惯性、鱼线及收线 | `Source/Catfishing/Fishing/Simulation/CatFishingFightSimulator::Step`，Runner 固定步调用 | 固定候选游速、直接缩线 → 持久速度、N/kg 积分及有限出力收线 | 原位置替换；移除旧力量差份额及加速度响应时间 | 先纯模型，再接正式 Runner | 小鱼、强鱼、零游速、惯性、不同步长、高差 | 已接入；CommonForce/FishVelocity/PlanarConstraint 等用例通过 |
| 猫真实端点及速度归属 | `CatFishingFightRunner::HandleFixedStep` 读 Rod/Encounter；`ResolveFishSurfaceFromAuthority` 写最终结果 | 不预支未通过碰撞的移动意图；鱼速度来自实际落点 | 删除意图端点位移；保持竿尖在 CMC 之后采样 | 模型接收真实端点后切换 | 顶墙、后退、连续鱼线误差、岸坡 | 已接入；BlockedCarrierIntent 和真实碰撞/岸坡回归通过 |
| 移动与重放 | `Character/CatCharacter` 默认子对象 → `CatCharacterMovementComponent`；Rod 发布输入 | Rod Tick 补速度 → CMC 有限加速度和 SavedMove 原输入重放 | 新增原生移动组件，删除 Rod 旧速度写口和硬限速入口 | 先接收方，再切换默认组件、Rod、调用方 | 碰撞、纠正回放、重复应用、普通移动 | 已接入；TractionUsesCollisionAndReplaysSavedForce 通过，正式角色 BP CDO 使用新组件 |
| 换人和退出 | `CatFishingRodActor::CommitAuthoritativeMutation`、约束复制回调、EndPlay | 原约束可能跨持有人沿用 → 持有人关联校验、服务器同步卸力 | 给复制快照绑定 `ConstraintHolderPlayerState`；旧移动来源清理 | 清旧角色，再允许新受力快照 | BeginPlay 前换人、旧快照乱序、最后一人离开 | 已接入；CarrierHandoffRejectsOldHolderConstraintAndClearsImmediately 通过 |
| 杆转矩与地形 | Runner 最终 Motion → `CatFishingRodResistanceModel::Evaluate` | 二次估算鱼力量 → 同一张力沿最终线方向作用 | 替换转矩输入，保留 StrengthMeters 原单位及转杆积分 | 最终地形/终局后发布 | 松线、上岸和力竭残留负载 | 已接入；RodRotationResistance、真实岸坡零负载回归通过 |
| 体力、耐久与终局 | `FinalizeResolvedStep` → Runner ASC → Session/Equipment；Encounter 复制 | 请求距离收费 → 最终完成距离；地形改动后幂等重算 | 原业务写口保留；没有新的资源、存档或捕获入口 | 最终结果形成后单次支付 | 停转支撑、助手分摊、右键回体、重复回放、坏竿优先 | 已衔接；完整 Effort/Participant/Session 回归通过；初级竿耐久既有失败保留 |
| 参数、资产与脚本 | `Config/CatFishingFightBalanceDefinition` → Session；正式 DA；`Scripts/create_fishing_fight_balance_asset.py` | 新力单位不能复用旧加速度数值；已有调参保持 | 迁移版本 1，新参数独立默认值；旧运行读取删除 | 编译接收方后迁移/再验证正式资产 | 默认值、可运行门槛、重复执行是否改写 | 运行迁移已完成；两次脚本成功，第二次资产哈希不变；六个序列化兼容字段暂留，删除未完成 |
| 蓝图、UI、动画及 Cook | `Source/CatfishingEditor/Fishing/Tests/CatFishingForceMigrationTests.cpp`；正式 `BP_CatCharacter`、Encounter/View、原资产软引用 | 图表引用不能代替完整二进制资产核对 | 只读扫描 90 蓝图/170 图表；保持旧 UI/动画与 Cook 包路径 | 先审计，再决定能否删字段 | 旧 WBP 父类缺失、属性绑定无法完整确认 | 六字段在已加载图表无引用；一个旧 WBP 未完整加载，因此资产清理未完成；正式画面未验收 |
| 日志、测试与文档 | `LogCatFishing`、Fishing Tests、本文及唯一进度入口 | 旧速度平滑诊断/旧公式 → 共同张力、真实位移、最终费用 | 删除旧日志入口并更新说明；审计仅在 Editor 模块 | 随调用链替换并最终核对 diff | Development 是否可编译、诊断是否落盘 | Editor/Game Development 通过；ForceDeliveryReport 为 124 clean + 3 警告 + 1 既有失败，0 notRun；新包双端日志尚未验收 |

最终证据位于 `Saved/Automation/FishingPhysics/`：`BuildForceEditor.log`、`BuildForceGame.log`、`ForceDeliveryReport/index.json`、`ForceDeliveryTests.log`、`ForceAssetMigration.log`、`ForceAssetRevalidation.log`。新增诊断/测试不构成第二套运行实现。未新增持久化事务、正式 WBP/动画资产、资源生成目录或 Cook 入口。完整三维接触物理和正式联机交付仍是明确未完成边界。
