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

移动重放只恢复该移动步保存的牵引方向、加速度、支撑减速度、速度上限、有效上下文和来源 ID。它不调用 Runner、ASC、装备、随机数或捕获事务。服务器仍只接受自己的受力裁决；客户端没有上传自报力量或张力的权利。连续牵引上下文有效期间不合并 SavedMove，包括暂时零牵引的减速阶段，避免改掉原碰撞积分步长；这会增加该阶段的移动记录/网络开销，需要打包联机测量。

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

力与阻力共同决定到达目标速度的过程，小鱼自由游速仍渐近性格资产值；换向不会瞬间反转已有惯性。Runner 保存求解输出 `ResolvedFishVelocityCentimetersPerSecond`，并反馈地形及 Encounter 实际落位与候选的差异。历史位置纠偏不再写入下一步惯性，也不计入鱼主动做功或主动上岸牵引。鱼力竭时清除游动速度，继续沿用既定的无自主漂游收尾规则。

线约束采用单向拉力。竿尖到鱼水面的高差为 h，已放线长为 L，可行水平半径为 `sqrt(max(L²-h², 0))`。先从旧状态分离历史位置误差，再寻找使双方同一步末端点满足线长的非负张力。鱼端包含隐式水阻和惯性；猫端包含实际沿线速度、质量、有限支撑、牵引限速与胶囊探测允许的移动距离；手持杆的预测旋转也由同一个候选张力驱动。猫端预测采用不超过 1/120 s 的积分，与现有 CMC 的非反向支撑和速度上限规则一致：

```text
mobility_cm/N = 100 × dt² / m_effective
fish_end(T) = corrected_start + dt × v_free - T × mobility × horizontal_line_fraction × horizontal_axis
rod_aim(T) = existing_StepRotation(readonly_current_state, torque_from_T, dt)
rod_end(T) = actual_holder + rotate(rod_aim(T), calibrated_tip_offset) + predicted_carrier_displacement(T)
T_N = smallest nonnegative tension that satisfies the end-of-step line constraint
```

固定端是该约束的特例，使用解析解；可移动端使用有界求根。接近纯竖直时力臂有数值下限。历史位置误差按 `MaximumFishConstraintCorrectionSpeedCentimetersPerSecond` 回收，不再全部折算成新拉力；不可满足的高差仍保留。该配置仍以 cm/s 为单位，保留猫端牵引速度上限用途。该层不求鱼的垂直浮力，也不求猫与地面的法向力或静/动摩擦系数；地面阻挡由 CMC 执行，沿线支撑能力仍由猫力量及既有杆杠杆规则给出。

`ACatFishingRodActor::GetRotationPredictionFromAuthority` 为实际刷新和预测提供同一份输入构造：真实姿态、请求朝向、已有滤波历史、现有阻尼参数、持有人位置及正式握把/竿尖标定。模拟器调用既有 `FCatFishingRodResistanceModel::StepRotation` 试算本步末竿尖，不写实际姿态、滤波历史或努力累计量。没有旋转快照的纯数值夹具仍可提供竿尖相对身体速度；正式持竿 Runner 提供旋转快照。仅外推上一份负载产生的竿尖速度不足以解决重鱼反复卸力，因此不能把这条夹具输入当作正式手持杆方案。

左键产生 `ReelSpeedCentimetersPerSecond × dt` 的请求。模拟器用同一个约束函数寻找不超过猫可用支撑/卷线力的缩短量：负载超过出力时停转，有余力才实际缩短。`RequestedReelDistanceCentimeters` 与 `ActualReelDistanceCentimeters` 明确分开。力竭鱼使用独立的 `ExhaustedReelForceNewtons`（默认 200 N），保持猫零体力也能免耗体回收，仍不超过配置收线速度。

猫端不使用玩家期望速度冒充已完成位移。Runner 从真实端点、实际速度和 CMC 胶囊查询构造预测输入；`CarrierTravelLimitCentimeters` 为负一表示固定端、零表示无法向鱼移动、正值表示本步向鱼移动的保守上限，默认负一。预测不移动 Actor、不触发重叠、不计费；真实移动仍由 CMC 完成，下一步重新读取实际端点。最终沿线净力为 `T × 最终线方向水平比例 - 猫可用支撑力`，正负值分别输出互斥的加速度和支撑减速度（cm/s²），减速度不会把静止猫推离鱼。

地形未改变鱼候选落点时，Runner 保留同一步末约束求出的张力，避免又用“鱼新位置 + 猫尚未执行完的旧位置”清零。地形确实改变候选并产生松线时，仍撤销负载。`ConstraintRodEnd` 是受碰撞上限约束的预测观察值，不是已经执行的角色位置；角色主动移动、滑墙、台阶或移动障碍可能使实际落位与预测不同，不能将该模型当作 Chaos 内同一物理步的完整刚体接触求解。

Rod 只转交输入；`Character/CatCharacterMovementComponent::CalcVelocity` 在原移动积分中衔接沿线加速/减速，无输入时替换沿线的普通急刹，有输入时仍保留引擎完成的主动加速并提供牵引速度下限，其余轴和碰撞/滑动由 CMC 处理。`bUseContinuousTraction` 明确表示活鱼、持竿、有主位且未终局的上下文，暂时零牵引仍保持连续减速；它与原 `CarrierConstraintState.bActive` 的正牵引含义分开。非反射的移动输入 `FCatExternalTractionInput.bActive` 表示该来源参与本移动步，因而也包括减速阶段。鱼力竭、终局、离竿、换人和清约束后退出；减速度默认 0、上下文默认 false，启动首帧的原转矩初始化仍使用默认值，首个固定步才发布实际受力。

行走期间 `PerformMovement` 临时将 CMC 最大子步压到不超过 1/120 s，并为本帧（预算至 0.25 s）保留足够迭代数；实时移动和 SavedMove 重放使用同一设置，返回后恢复原设置。该方式复用引擎支持子步的移动模式，不在外层重复执行资源或运动回调。极小正加速度也必须发布非零速度上限，避免近似平衡时被零上限瞬间刹停。旧 Rod Tick 补速度、质量份额分配位移、背离方向速度硬截断均已退出生产链。

`NormalizedLineLoad = pow(max(dot(鱼努力方向, 水平向外方向), 0), AngleStrengthExponent)` 继续供鱼表现和既有方向性磨损规则使用，不能冒充真实张力。`LineTensionNewtons` 是力；`NormalizedTension = clamp(T / DisplayTensionNewtons, 0, 1)` 仅是表现刻度。`TensionCentimeters/ConstraintErrorCentimeters` 仍表示几何误差。强对抗、僵持标记只观察结果，不锁位置、不裁决断线。

为使每个固定步都能从落盘数据复核，`FCatFightStepResult::Trace` 保存本次纯求解的中间量，但不作为下一步输入，也不写 ASC、装备或 Actor。保留方向、力与质量换算、收线力上限、实际张力和最终带符号加速度。旧 `FullCorrectionCm` 替换为含义明确的 `ExistingPositionErrorCm`；约束采样新增 `PositionCorrectionCm`、`CarrierTravelLimitCm`、`ConstraintRodEnd` 和 `RodRotationPredicted`，区分历史误差修正、碰撞上限和本步受力预测，并确认正式杆旋转已参与约束。`ResolvedFishVelocityCmS` 现在记录受力积分并经地形反馈后的速度，已排除历史位置纠偏。

Development 权威日志 `Event=fishing_simulation_trace` 默认按约 1 秒和终局额外输出一次，包含上述中间量、猫移动/收线/转杆做功单位、共享支撑负载、鱼的实际/受阻/等效努力距离、原始/封顶鱼体力费用、猫体力前后值、方向性磨损、`InputAccepted/FinalizeAccepted` 与终局名。它不会在 `FCatFishingFightSimulator` 内直接写日志，保证测试仍是无副作用纯函数；非法输入会在 `fishing_fight_step_rejected` 中写出 `RejectReason`（配置、状态、竿约束、鱼方向或最终结果）。要复盘单步时，以 `SessionId + RodActorId` 关联 `fishing_simulation_trace`、`fishing_constraint_sample`、`fishing_surface_tow` 和资源写回事件。

### 最终费用和耐久

以下费用规则使用上文当前求解结果；文末其他日期/阶段的衔接表是历史证据，不代表仍在运行旧求解公式。

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

有向负载仍使用 0.15 s 指数平滑。2026-09-07 的实机反馈和 `Saved/Logs/Catfishing.log` 显示：不动鼠标也有摆动，权威张力在约 0.05～0.15 s 内多次松绷切换，计算转速一度约 339.8°/s。已有滤波后仍会出现大幅快速转动；竿尖又参与下一固定步的线约束，因此本次在同一积分器增加受载粘性阻尼，限制这条反馈链的转动响应，不再叠加独立滤波组件。

`HeldRodLoadedAngularDampingRatio` 来自 `DefaultGame.ini` 的 `[/Script/Catfishing.CatFishingSettings]`，原生默认和正式配置均为 3，无量纲。设平滑后的鱼负载大小为 P，原转矩尺度为 `S = max(猫转矩容量, P, 数值下限)`，本亚步阻尼倍率为 `1 + 3 × P/S`；原净转矩对应的角速度除以此倍率后，继续使用原角速度上限。鱼负载达到或超过猫容量时倍率为 4，计算转速上限由 360°/s 降到 90°/s；负载很小则连续接近原响应，完全空载严格保持原响应。配置为 0 时禁用追加阻尼，仍走同一个公式和原负载滤波，无第二套运行实现。

猫、鱼净转矩统一减缓，不改变静态力量平衡、方向或单位。受载时玩家主动调杆也会减缓，趋近平衡所需时间更长，这是本次明确的手感变化；实际做功依旧由最终积分转角观察，单价及支付入口不变，支撑持续时间可能随运动过程变化。默认倍率仍需用户复测手感后调节。没有新增角速度历史、复制字段、资产迁移或退出清理状态。

杆负载使用地形后的线方向；松线、上岸力竭、终局均不发布旧鱼转矩，现有负载历史继续渐退。实际竿尖、Actor Transform、握把/镜头和努力采样继续消费同一积分结果。该层仍没有独立鱼竿转动惯量，不是完整刚体角动力学，也不声称已经解决所有猫端牵引及网络纠正抖动。

## FishLogic 4：网络与移动重放

服务器决定鱼状态、固定随机流、线长、费用和最终 Transform。拥有客户端接收 Rod 约束用于本地移动；模拟代理使用引擎角色移动复制。`FCatSavedMove::SetMoveFor` 保存每次移动使用的约束，`PrepMoveFor` 为纠正重放恢复它，重放结束恢复读取最新复制输入，旧鱼负载不会覆盖实时输入。换持有人、离竿、清约束和来源销毁会卸载实时牵引。

Rod 的约束快照同时保存 `ConstraintHolderPlayerState`，复制乱序时只能作用于快照对应的持有人；服务器在换主位、离竿、坏竿和收起时立即卸载旧牵引，不能等下一次表现 Tick 才清理。服务器发布和拥有客户端 OnRep 收到暂时零牵引时，保留对应 CMC Tick 前置关系；只有实际解绑或来源不匹配时移除，使竿尖在拉动与减速阶段都采样当帧碰撞后的角色位置。

这保证受力输入参与客户端历史移动重放，不代表已经实现整场物理回滚、服务器按客户端时间戳回溯鱼状态或零延迟网络一致性。仍须在延迟/丢包条件下检查服务器纠正频率、主辅换人、坡面与正式双端手感。自动化碰撞/回放测试属于受控 runtime_behavior，不能替代真人联机验收。

## FishLogic 5：上岸、力竭与收近

原 `ResolveFishSurfaceFromAuthority` 继续解析真实水面、岸线间隙及阻挡坡面。活鱼只有实际收线或身体向岸位移形成有效拖拽时才能上岸力竭，纯甩杆仍不能借少量卷线误触发。鱼干继续使用同一路径；只有真实干地和拾取距离条件同时成立才进入 Pickup。地面暂缺或重新入水会撤回干地资格，不创建第二条捕获路径。

动画、WBP、拾取和持久化入口保持现状，本轮没有新增正式表现资产或 Cook 入口。

## 参数、兼容载荷与诊断

正式数值仍从 `/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default` 唯一读取，`DefaultGame.ini` 保留原软引用。`Scripts/create_fishing_fight_balance_asset.py` 验证并保存显式迁移版本，已有调参不重置。新默认值为每点力量 1 N、单猫质量 5 kg、力竭回收辅助力 200 N、零体力拖行辅助加速度 300 cm/s²、满表现张力 50 N。

以下旧字段没有新模型运行读取，但保留序列化/只读蓝图身份：平衡资产的 `AccelerationPerStrength`、`DriveResponseSeconds`、`TensionResponseRangeCentimeters`、`MinimumCarrierAwaySpeedMultiplier`，以及 Rod/Snapshot 的两个旧背离速度倍率字段（当前恒为 1）。项目蓝图图表引用由 `CatFishingForceMigrationTests.cpp` 审计；旧 `/Game/UI/WBP_CatLakeReach` 的父类 `CatLakeReachWidget` 缺失，部分子对象无法完整加载，故不能宣称所有资产消费者已确认。删除条件是先在编辑器恢复或正式迁移该旧 WBP，核对其属性绑定与外部调用，再移除兼容载荷。没有保留第二套旧模拟器或速度写口。

开发包默认落盘日志分类 `LogCatFishing`：

- `fishing_fight_started`：`StrengthResolution=CommonLineForce`、`ForcePerStrengthN`、`MassMode=IndependentCatBodyMass`。
- `fishing_behavior_phase_entered`：StateTree 请求的平静/反抗阶段、前一阶段、已抽取的持续秒数、双方体力、会话/鱼竿 ID；不额外调用随机数。
- `fishing_constraint_sample`：共同 `LineTensionN`、几何误差、最终转矩、`CarrierAcceleration`、`CarrierBrakingDeceleration`（均 cm/s²）和 `ContinuousCarrierTraction`；详细模式每固定步附加 `StepId/Frame/WorldTime/WorldGapSeconds`、实际/请求阶段、期望/目标游向、边界避让时间、鱼的前位置/速度与地形解析后速度、竿尖及角色速度/输入。
- `fishing_simulation_trace`：固定步的几何、方向负载、力/质量换算、隐式移动质量、收线二分的力上限、所需/实际张力、鱼/猫费用、磨损和终局；按约 1 秒及终局输出，避免无条件刷屏。
- `fishing_coupled_work_sample`：请求/实际收线及各项费用；最终结算失败看 `fishing_final_work_rejected`。
- `fishing_carrier_movement_sample`：RodActorId、角色、前后速度、实际碰撞位移、输入加速度、牵引方向/上限、`Frame/WorldTime/DeltaSeconds/Replay`、NetMode/LocalRole、`AccelerationCmS2/BrakingDecelerationCmS2`；`Active` 包含减速阶段，退出帧仍保留最后来源 ID。实时与重放分别限频，重放日志不改变实时采样计时器。替代旧 `fishing_carrier_smoothing_sample`。
- `fishing_rod_rotation_resistance_sample`：原始/平滑负载、阻尼倍率、转速、控制器意图、实际姿态和努力 Epoch；附加本帧 `DeltaYaw/DeltaPitch`、身体位置/速度、竿尖位置/速度、`ConstraintAgeSeconds` 与 `Integrated`，区分正常积分和初始化姿态。
- `fishing_carrier_constraint_received`：约束快照持有人、当前持有人、是否成功绑定移动组件、拉力/减速/转矩、接收时观察的鱼竿姿态、握把及本机帧/世界时间；这是复制回调的观察事实，不把未绑定快照记为成功应用。
- 原 `fishing_surface_tow`、`fishing_fish_beached`、`fishing_drag_water_entered`、装备磨损及捕获日志继续沿用。

Win64 Development 包应在不加 `-log` 时写入 `<打包根目录>/Catfishing/Saved/Logs`。本轮尚未重新打包、采集新房主/客户端双端日志或验收正式画面，不将代码/受控运行通过写成 presentation_delivery 完成。

## 平静/反抗运动日志衔接核对（2026-09-07）

用户复测反馈平静阶段顺滑、反抗阶段左右转动有停顿，要求先增加详细日志。工作开始时存在模拟器 Trace 与鱼竿弯曲表现的并行修改；Trace 在本轮期间形成 `2fc4acb`，本轮沿用它的每秒公式诊断，新增的是阶段和运动时间信息。弯曲组件、Hook/Session、表现设置、资产和对应测试不归本轮提交，保留并行工作。

`cat.Fishing.MotionLog` 在 Development 默认 1，无需打开屏幕调试或附加 `-log`。详细模式每个战斗固定步（当前 20 Hz）记录一条约束样本，包括松线；实际运动、旋转及接收样本按不超过 60 Hz 的时间间隔限频，原有关键状态变化额外保留。控制台设为 0 时恢复原约 1 Hz 运动/约束采样并关闭重放详细采样，阶段切换仍落盘；Shipping 不启用详细模式。记录停止条件沿用真实搏斗/牵引生命周期，没有后台文件写入器或新的玩法历史。高密度文本日志会增加磁盘与格式化开销，排查后可关闭。

分析时先按 `SessionId + RodActorId` 定位 `fishing_behavior_phase_entered` 的 `CalmOrInward/StrugglingOutward`，再关联同一鱼竿的约束、角色和旋转样本。这三条链的鱼竿 ID 统一为带连字符格式；`Frame/WorldTime` 是本进程/本 World 的时间，不能将客户端与服务器数值直接相减当网络延迟。`WorldGapSeconds` 是相邻成功到达日志阶段的固定步之间的世界时间差，第一步为 0；同帧多步且间隔为 0 可用于发现计时器追赶。`ResolvedFishVelocityCmS` 表示地形解析后的候选速度，实际提交失败仍以原失败/终局事件为准。反抗阶段先比较 `SteeringTarget/DesiredFishDirection` 与 `LineTensionN`，再看 `HolderVelocityCmS`、`VelocityBefore/Velocity` 和 `DeltaYaw/NetTorque/AppliedFishPull`，最后核对约束接收/重放时间。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 阶段入口 | `Source/Catfishing/Fishing/Simulation/CatFishingFightRunner.cpp::BeginBehaviorStateFromStateTree`，由原 StateTree 调用 | 缺少平静/反抗切换与持续时长 | 原入口读取已抽取的持续时间并记录双方体力、前后阶段、ID | 沿用原状态和随机流，计算完成后记录 | 日志消耗随机数或改变阶段时长 | 真实入口开关对照测试通过，时长序列和随机流末状态相同；阶段日志落盘 |
| 固定步/转向/张力 | 同文件 `HandleFixedStep` → Steering/Simulator/地形解析 → Rod | 既有公式日志 1 Hz，短暂方向/负载/时序变化可能漏掉 | 扩展原 `fishing_constraint_sample`；默认逐固定步记录最终候选结果 | 不改变生产调用顺序；编号和时间仅为诊断 | 相同帧追赶、不涉及物理公式、资源或终局更改 | 编译和完整 Fishing 回归无新增失败；正式搏斗固定步密集日志待用户复测采集 |
| 旋转消费者 | `Source/Catfishing/Fishing/Actors/CatFishingRodActor.cpp::RefreshHeldTransformFromAuthority` → 原 Actor/握把/竿尖消费者 | 原 1 Hz 样本可能漏掉短暂停顿 | 扩展原旋转事件，本帧角度差、身体/竿尖运动、约束年龄 | 已有旋转结果产生后只读观察 | 日志改变真实姿态，或无限逐帧写入 | 120 FPS 受控 World 中验证采样密度与上限，开关前后实际姿态相同 |
| 移动与复制 | `Character/CatCharacterMovementComponent.{h,cpp}::PerformMovement`；Rod 的 `OnRep_CarrierConstraintState` | 缺前后速度/重放/接收时序，退出时原来源被清空 | 扩展原移动事件，新增限频接收事件；ID 统一，退出保留仅用于诊断的来源 | 原复制/运动绑定完成后观察，复制载荷和 SavedMove 不变 | 旧快照被误称应用、日志恢复旧牵引、退出后刷屏 | 真实接收回调记录绑定结果；开关前后实际移动相同；退出停止详细采样且 ID 保留；原移动重放回归通过 |
| 开关及生命周期 | `Source/Catfishing/Fishing/Debug/CatFishingMotionDiagnostics.{h,cpp}` → Runner/Rod/Movement | 各层缺少共同的采样控制 | 新增同模块普通辅助函数及 `cat.Fishing.MotionLog`，Development 默认 1 | 各消费者读取同一开关，无 UObject/复制/存档状态 | 关闭无效、采样计时器干扰实时与重放 | 关闭恢复低频、开启增加采样、结束停止的行为测试通过 |
| 配置/资产/资源/打包 | 原 `DefaultGame.ini`、正式 FightBalance/StateTree、Rod BP/WBP、ASC/Equipment 与 Cook 入口 | 本轮不涉及参数、资产、脚本、费用和持久化变更；未知二进制绑定未重新确认 | 保留接口与反射字段；不改名、不删除资产；复用 LogCatFishing 默认落盘 | 无迁移依赖 | 并行表现资产不能混入日志提交；正式双端需后证 | Editor/Game Development 构建成功；并行内容保留；旧 WBP 与六个兼容字段的删除条件不变 |
| 测试/文档/残留 | `Fishing/Tests/CatFishingDiagnosticLogTests.cpp`；本指南、差距清单、`Docs/FishingArchitecture_zh-CN.md` | 旧采样说明与架构转矩公式已过时 | 复用原测试文件；更新采样说明与当前共同张力口径 | 先源码/日志核对，再更新说明 | 仅编译不能证明日志落盘或表现正确 | 新日志实际写入文件；本轮无并行滤波/模拟器实现，旧 1 Hz 是同事件的低密度模式；公式 Trace 保持独立用途 |

contract：`Saved/Automation/FishingPhysics/BuildMotionLoggingEditor.log`、`BuildMotionLoggingGame.log` 的 Editor/Game Win64 Development 构建成功。最终 `MotionLoggingFinalReport/index.json` 为 136 项：132 clean、3 警告、1 既有初级竿 500/150 耐久失败，0 notRun；没有本轮新增失败。早期 DebugGame 报告的日志捕获断言失败来自测试未处理 UE 异步日志派发，已在测试中同步排空并使用线程安全捕获；不把早期报告算作通过，也没有为通过测试改变生产日志写入时序。

runtime_behavior：`DetailedMotionLogsPreservePhysicsAndPhaseRandomness` 通过；开关前后阶段时长、随机流末状态、实际角色位移及杆姿态一致，详细采样限频、约束接收字段、退出停采样及来源 ID 通过。`MotionLoggingFinalTests.log` 新进程加载常用 Development DLL，阶段/旋转/接收/移动新字段真实落盘。测试覆盖受控 World 与真实阶段入口，正式战斗的逐固定步采样仍需接下来的用户测试确认；并行弯曲表现尚不由本项宣称交付。

presentation_delivery：尚未重新打包、没有新房主/客户端双端日志或反抗阶段手感验收；此次提供定位信息，不宣称已修复用户刚反馈的剩余停顿。重新打开编辑器可直接测试平静与反抗阶段；编辑器日志在项目 `Saved/Logs/Catfishing.log`，打包后应在 `<打包根目录>/Catfishing/Saved/Logs`，测试后记录发生停顿的大致时间便于定位。

## 被鱼拖动时的持续抖动衔接核对（2026-09-07）

修改前为 `0cd7866`，工作区干净；上一轮 129 项基线为 125 clean、3 警告、1 既有初级竿 500/150 耐久失败。用户复测确认阻尼减轻抖动，但身体被鱼拖动时仍反复回正。原生产代码与受控复现确认：仅发布正牵引导致张力过平衡点时切回普通急刹，零牵引发布和 OnRep 又撤销 CMC Tick 前置关系；连续积分实施中还定位并移除了极小正加速度对应零速度上限的容差冲突。修改前先在对话列出影响对照，以下为交付核对。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 固定步与受力计算 | `Source/Catfishing/Fishing/Simulation/CatFishingFightRunner.cpp::HandleFixedStep` → `CatFishingFightSimulator.cpp::FinalizeResolvedStep` | 原只发布正牵引；目标为共同张力下连续加速/支撑减速。N、kg、cm/s²、猫力量和固定步契约保持 | 原结果结构新增减速度及上下文；正牵引字段含义不变，修正微小正值的速度上限 | 先接收方，再结果及 Runner 唯一生产调用 | 平衡附近启停、松线、鱼力竭/终局、费用 | 已衔接；20/60/120 FPS 实际地面稳定拖行；完整 Simulation 回归通过 |
| 角色移动与重放 | `Source/Catfishing/Character/CatCharacterMovementComponent.{h,cpp}::PerformMovement/CalcVelocity`、`FCatSavedMove`；Rod 发布输入 | 零牵引切回普通急刹 → 本来源连续减速；保留碰撞、主动输入与单一移动写口 | 在现有输入新增 cm/s² 减速度，沿线积分保持非负停点；临时收紧 CMC 子步，复用整个输入的 SavedMove 保存/恢复 | 先移动接收方，再 Rod；清理后恢复原制动 | 静止倒滑、退出滑动、顶墙、重放读到实时新力、低 FPS、普通子步设置泄漏 | 扩展 `TractionUsesCollisionAndReplaysSavedForceWithoutLiveOverwrite` 通过；新地面用例验证三档 FPS 和设置恢复 |
| Rod 复制、绑定与退出 | `Source/Catfishing/Fishing/Actors/CatFishingRodActor.{h,cpp}::SetCarrierConstraintFromAuthority/OnRep_CarrierConstraintState/PublishCarrierConstraintToMovement/ClearCarrierMovementBinding` | 原零牵引撤销 CMC 前置 Tick，下一帧重绑；目标保持身体先移动、竿尖后采样 | 删除两处按正牵引开关解绑的分支；新增反射字段 `PullBrakingDecelerationCentimetersPerSecondSquared=0` 与 `bUseContinuousTraction=false` | 移动接口准备后切生产者；原换人、坏竿、收起、离竿即时清理保留 | 乱序复制套错持有人、旧力残留、交替帧采到旧位置 | 扩展 Handoff 用例验证零牵引发布及 OnRep 不拆前置关系、真实换人会拆；新 World Tick 用例验证实际握把位置，均通过 |
| 费用、状态与权威 | `CatFishingFightRunner.cpp::Start/HandleFixedStep/Stop` → ASC、Encounter、Session/Equipment | 原启动先发布转矩意图、首固定步实际受力；资源和终局只由服务器提交 | 保留启动/停止时序、原扣费/磨损/捕获写口；仅附加最终运动输出 | 最终地形解析及费用求值后，沿原调用发布输入 | 重放双扣、提前终局、力竭拖水/回收行为 | 未新增任何资源写口；Fishing Service/Simulation、危险水深和岸坡回归通过 |
| 旋转、朝向、相机与 UI | `CatFishingRodActor.cpp::RefreshHeldTransformFromAuthority` → `Framework/Game/CatGameplayTypes.cpp::ACatfishingPlayerController::RefreshFishingFacingMode`、`Fishing/Presentation/CatFishingCameraComponent.cpp::TryGetCameraView` → `GetGripWorldTransform` | 沿用上一轮阻尼 3 与负载滤波 0.15 s，不再调慢手感；改正其端点输入时序 | 保留唯一实际姿态，调整生产该端点的绑定时序 | CMC 连续移动 → Rod 当帧位置 → 原朝向/相机消费者 | 反复回正、鼠标响应、稳定平衡、努力 Epoch | Camera/RodEffort/Service 回归无新增失败；正式画面仍待用户复测，不能用自动化代替 |
| 配置、资产、脚本、持久化、Cook | `Config/DefaultGame.ini` 的 `[/Script/Catfishing.CatFishingSettings]`；`/Game/Blueprint/Actors/BP_CatFishingRodActor`、`/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default`；`Scripts/create_fishing_fight_balance_asset.py` | 不涉及资产/存档迁移、配置调参、生成脚本及新增 Cook 入口；新增状态按原 Rod 复制，旧反射身份保持 | 保留资产；扩展原约束结构，未新建滤波组件或可选旧运动模式 | C++ 接收方编译后由原生默认值提供新增字段 | 结构加载、正式角色组件、未知二进制绑定 | 原资产契约/蓝图审计运行；旧 WBP 父类缺失仍未确认其全部消费者，六个兼容字段按原条件暂留，未删除资产 |
| 诊断、测试与文档清理 | `CatFishingFightRunner.cpp` 与 `CatCharacterMovementComponent.cpp` 的 `LogCatFishing`；`Fishing/Tests/CatFishingForceIntegrationTests.cpp`；本指南和唯一差距清单 | 原诊断只有正牵引；增加减速和上下文可区分真正退出。旧正牵引口径需同步 | 扩展原限频事件，复用已有测试文件；更新计算、重放及采样时序描述 | 对照旧复现后验证新代码，最后核对调用与 diff | 新增高频刷屏、重复实现、把试验报告当交付 | 默认落盘新字段已验证；旧断续牵引分支与零上限冲突已替换，不留第二份实现。最终证据见下文 |

contract：`Saved/Automation/FishingPhysics/BuildTractionContinuityEditor.log`、`BuildTractionContinuityGame.log` 的 Editor/Game Win64 Development 构建成功；`TractionContinuityDevelopmentReport/index.json` 最终 131 项为 127 clean、3 警告、1 既有初级竿耐久失败，0 notRun。独立 DebugGame 的 `TractionContinuityFinalDebugReport` 结果相同。未修改既有耐久资产或降低其断言。

上述证据对应本次牵引修复的构建。最终构建后发现 `CatFishingFightSimulator.{h,cpp}` 出现另一项诊断 Trace/拒绝原因扩展，提交时只纳入本轮受力计算及输出字段的差异；并行扩展原样保留，不由本轮测试结果背书。

runtime_behavior：旧实现隔离高度夹具 `TractionContinuityBaselineReport` 复现恒定鱼力下速度反复归零，且零牵引 Tick 绑定断言失败。最终改用真实地面 Walking，覆盖引擎实际摩擦、碰撞和子步；恒定鱼推力 75 N、鱼 3 kg、猫 5 kg、支撑 50 N，运行 20 s 后取末 5 s，20/60/120 FPS 均为 50 N 张力与 25 cm/s 匀速（报告保留四位小数）。另一个真实 World Tick 测试交替发布拉动/减速 120 帧，握把始终跟随当帧碰撞后的身体；真实移动重放、退出恢复普通制动、静止不倒滑、换人和乱序回执保护均通过。`TractionContinuityDevelopmentTests.log` 可检索新减速字段、`fishing_carrier_movement_sample` 与 `fishing_constraint_sample`。

验证边界：前期 `TractionContinuityDebugReport` 和 `TractionContinuityRecheckReport` 是未收口的试验报告，不能当作最终通过。UE `PhysFlying` 不使用 Walking 的子步循环，20 FPS 的 Flying 隔离夹具仍有振荡；已检索的 Character/Fishing/Condition 生产源码未主动设置 Flying，该夹具不能用于声称地面通过，也不能被地面通过掩盖为通用运动已稳定。更大鱼猫质量比、其他移动模式及网络延迟下的完整耦合稳定性未完成验收。

presentation_delivery：编辑器退出后已完成常用 Development DLL 的正式构建和新进程加载回归，可重新打开项目复测。期间请求过 Live Coding，但编辑器在编译期间正常退出，热更新结果不作为交付证据。未重新打包、未采集本轮房主/客户端双端日志，正式场景“不动鼠标且被鱼拖动”的回正抖动仍待实机确认；连续上下文扩大了禁止合并 SavedMove 的时间范围，子步增加了移动碰撞开销，尚需打包联机测量。Fishing 模块仍未整体验收。

## 手持杆抖动衔接核对（2026-09-07，前一阶段）

修改前为 `a4f968a`，工作区干净。上一轮 128 项基线为 124 clean、3 警告、1 既有耐久失败。用户明确反馈中鱼后即使不动鼠标也抖；日志确认权威负载跳变，尚未将该现象唯一归因于某个网络或物理环节。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 张力入口 | `Source/Catfishing/Fishing/Simulation/CatFishingFightRunner.cpp::ResolveFishSurfaceFromAuthority` → `RodResistanceModel::Evaluate` → `Rod::SetCarrierConstraintFromAuthority` | 保留牛顿张力、StrengthMeters 转矩、地形后线方向、固定步和服务器权威 | 保留同一生产者，不在 Runner 另建平滑张力或付费入口 | 接收方模型就绪后接 Settings | 力竭、松线、共同张力与终局 | 原链路未修改；Fishing 全回归无新增失败 |
| 旋转积分 | `Source/Catfishing/Fishing/Simulation/CatFishingRodResistanceModel.{h,cpp}::StepRotation` → `Rod::RefreshHeldTransformFromAuthority` | 原 0.15 s 负载滤波保留；受载净转矩的速度响应减缓，静态平衡不变 | 原公式追加无量纲阻尼，默认 3；不另建组件或旋转状态 | 先纯模型对比，再接实际 Actor | 固定视角/20 Hz 负载切换、20/60/120 FPS、静态平衡、卸载 | 新 `LoadedRodDampingReducesFixedAimJitterWithoutChangingBalance` 通过；摆幅分别下降约 84%/76%/75% |
| 配置及生命周期 | `Source/Catfishing/Fishing/CatFishingSettings.h::HeldRodLoadedAngularDampingRatio`、`Config/DefaultGame.ini` 的 `[/Script/Catfishing.CatFishingSettings]` → `CatFishingRodActor.cpp::RefreshHeldTransformFromAuthority` | 新默认 3，单位为倍率；空载 360°/s、0.08 s 瞄准响应保持，非法值拒绝 | 原 Actor 读取/校验配置并传入模型；不新增历史或复制字段 | 模型后接入；换人/清约束沿用原流程 | 配置实际接入、Epoch、重启会话与卸载 | 受控 Actor 从真实 Settings 获取倍率，验证实际 Transform、原平衡角和 Epoch；日志输出 Ratio=3、Multiplier=4 |
| 资源及实际运动消费者 | `StepRotation::CatPositiveWorkRadians/ExertionSquaredSeconds` → Runner 单次结算；`Rod::GetRodTipWorldTransform` → Runner/Hook；`Fishing/Presentation/CatFishingCameraComponent.cpp::TryGetCameraView` → `Rod::GetGripWorldTransform` | 不改价格、单位和写入权威；受载动作更慢，实际做功和支撑持续时间据此变化 | 保留同一姿态与努力采样入口，不滤波最终 Mesh 或镜头事实 | 先真实姿态，再核对既有消费者 | 努力、相机、身体朝向、松线和恢复 | RodEffort/Camera/Service 回归通过；原平衡精度保留，测试等待阶段改为覆盖更慢的受载收敛 |
| 资产、持久化和打包 | `/Game/Blueprint/Actors/BP_CatFishingRodActor`；`/Game/Catfishing/Data/Fishing/DA_FishingFightBalance_Default`；既有 Cook 和资产生成脚本 | 不涉及资产迁移、存档结构、生成脚本或新增 Cook 入口；二进制内部消费者本轮未重新确认 | 不删除或改名反射接口、资产字段；新增原生 Settings 参数 | 原有资产消费 Actor Transform；风险项继续保留 | 构建与资产契约；真人联机/正式画面需另证 | 本轮无资产改动；旧 WBP 与六个兼容字段的既有不确定性保持，没有新增废弃入口 |
| 测试与诊断 | `CatFishingSimulationTests.cpp`、`CatFishingRodEffortTests.cpp`、`CatFishingCameraTests.cpp`、`CatFishingServiceTests.cpp`；Rod 旋转采样事件 | 比较两条使用相同负载滤波的路径；稳定后取样，不把慢收敛计入摆幅 | 复用原测试文件和日志分类；同一模型系数 0 作为对照 | 原精度/幅度断言不变，先充分等待平衡再测；最后核对 diff | 原有行为、Development 日志和运行模块身份 | 最终 Development 129 项：125 clean、3 警告、1 既有耐久失败；新代码实际加载路径由日志确认 |

contract：`Saved/Automation/FishingPhysics/BuildRodDampingEditor.log` 与 `BuildRodDampingGame.log` 的 Editor/Game Win64 Development 构建成功；最终报告 `RodDampingDevelopmentReport/index.json` 为 129 项（125 clean、3 警告、1 既有初级竿 500/150 耐久基线失败，0 notRun）。`RodDampingDevelopmentTests.log` 明确加载新 `UnrealEditor-Catfishing.dll` 并输出新增阻尼字段；覆盖猫与鱼同向转动也不能越过受载转速上限。独立 DebugGame 构建和 `RodDampingFinalReport` 曾用于编辑器打开期间的阶段验证；初次普通启动器仍加载旧模块的 `RodDampingInitialReport` 只能作为旧版本复核；`RodDampingDebugReport` 是首次未收口的试验报告。最终交付以 Development 报告为准。

runtime_behavior：受控 World 中实际 Rod/Controller 的配置接入、受载 Transform、相机/朝向消费者和努力生命周期通过；日志见最终测试日志的 `fishing_rod_rotation_resistance_sample`。纯模型以固定视角、相同 0.15 s 滤波和每 50 ms 松绷切换进行对比：120 FPS 摆幅 0.5109° → 0.1259°，角速度均方根 11.7774°/s → 2.9014°/s；60/20 FPS 的摆幅也分别下降约 76%/84%。这些是受控输入的比较，不是实机全链路摆幅保证。

presentation_delivery：尚未在正式场景重试或重新打包联机验收。用户已保存并关闭编辑器，解除 Live Coding 构建锁，常规 Editor Development 模块已构建并通过加载验证；重新打开项目可复测。不能将受控摆幅降低比例当作正式场景或房主/客户端画面验收。

## 共同张力衔接核对（2026-09-07）

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

## 短线拖动期间的联合端点预测（2026-09-07，当前实现）

实机 `Saved/Logs/Catfishing.log` 的会话 `7C5598E1-40E5-C0DC-B336-F09C732B66AC` 显示，5.535 kg 鱼、约 188 cm 线长且鼠标不动时，共同张力在约 369 N 和 0 N 间跳变，弯曲消费者按收到的零力回直。问题在上游：鱼端先相对旧竿尖求约束，角色和转杆稍后移动端点；历史位置纠偏又混入鱼的下一步速度。只预测身体仍会在 15 kg 鱼上复发，最终需让同一个候选张力驱动鱼、身体和既有转杆模型。

开始时 HEAD 为 `1ea3ed6`，`CatFishingEffortTests.cpp` 已有工作区标记但无文本 diff，未纳入本次提交。期间窝料/咬钩改动由并行工作提交为 `6deefb6`，本轮未覆盖其配置、Session、资产或文档。修改前 `CoupledBaselineReport` 有四项既有失败：初级竿耐久 500/150、两项坏竿收纳夹具无法授予两次鱼饵、咬钩窗口配置；咬钩失败随并行修改消失，不归因于本修复。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 入口、状态与积分 | `Source/Catfishing/Fishing/Simulation/CatFishingFightRunner.cpp::HandleFixedStep` → `CatFishingFightSimulator.cpp::Step` → Runner 状态写回 | 20 Hz 调度保留；历史纠偏不再等同于物理速度 | 原求解入口替换；新增非反射结果速度及位置纠偏量，单位 cm/s、cm | 接收新结果后切换 Runner 和测试状态反馈 | 惯性、过长线、松线、有限收线、非法输入 | 已替换；JointMotion、Simulation、ExhaustedCatEscape 回归通过 |
| 身体预测、碰撞与重放 | Runner 从 `Character/CatCharacterMovementComponent::GetExternalTractionTravelLimit` 取输入；Rod 仍发布 CMC 牵引 | 固定身体端 → 按实际速度及碰撞上限预测；默认 -1 固定端，0 阻挡，正数为 cm | 新增只读胶囊查询；实际碰撞、移动和 SavedMove 入口保留 | 先可移动范围，再求共同张力，最后原 CMC 执行 | 顶墙不得预支位移、查询无副作用、退出和回放不变 | 真实墙体、CMC Walking 与 SavedMove 用例通过；滑墙/台阶的预测误差仍由下步真实端点反馈 |
| 鱼竿旋转及生命周期 | `CatFishingRodActor::GetRotationPredictionFromAuthority` 同时供 Simulator 和 `RefreshHeldTransformFromAuthority` 使用 | 只按旧负载竿尖速度外推 → 候选张力试算同一旋转模型 | 提取共享只读输入构造；复用 `CatFishingRodResistanceModel::StepRotation` | 先快照，再试算，实际 Tick 才更新姿态/滤波/努力 | 不得重复转杆、扣费、消费努力；换人初始化不变 | 正式标定 5.535/15 kg、20/60/120 FPS 无周期卸力；姿态、滤波历史和努力累计量只读断言通过 |
| 水面、岸坡与杆负载 | `CatFishingFightRunner::ResolveFishSurfaceFromAuthority` → `CatFishingRodResistanceModel::Evaluate` | 不改变候选的水面解析不能用异步端点清零；真实地形造成松线仍卸力 | 原位置衔接；地形差值只反馈真实碰撞速度；历史纠偏不算主动上岸 | 同步求解 → 地形 → 最终转矩 | 未变落点保持共同张力；跨岸、松线、鱼力竭清力 | `CatFishingSurfaceTests.cpp` 真实水域张力/速度/转矩交接及既有岸坡用例通过 |
| 费用、权威与复制 | `FinalizeResolvedStep` → Runner ASC/装备 → Session/Encounter/Hook；Rod 约束复制 | 输入输出单位和单次写口保留；历史纠偏不计鱼主动功；预测不收费 | 保留原资源/终局权威，只有求解结果生产方式变更 | 地形最终结果后单次结算 | 双重扣费、助手分摊、终局旧力、远端回执 | Effort/Participant/Session/退出契约通过；真人网络及双端落盘未验证 |
| 表现、配置、资产及 Cook | Hook `ActualLineTensionNewtons` → 现有杆弯曲；正式 `/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1` 标定；现有 balance/settings | 继续消费同一 N 张力；0.15 s 杆滤波、阻尼、线长等默认值保留 | 不涉及资产修改/迁移、持久化新写口、生成脚本或 Cook 入口变更；不新增插件 | 保留反射字段/包路径后验证原消费者 | 正式标定通过 LoadObject 读取；完整二进制消费者未确认，不删除兼容字段 | RodBend/资产加载回归通过；正式 WBP/动画、重打包和真人观感未验证 |
| 日志、测试及文档 | 原 `fishing_constraint_sample/fishing_simulation_trace`；Fishing Tests；本文和唯一进度入口 | 新旧口径区分纠偏、物理速度、碰撞预测及杆预测；限频策略不变 | 替换旧 Trace 字段，新增 `RodRotationPredicted` 等观察值 | 随接收方切换后检查 Development 构建与报告 | 日志不能更改时序/随机/玩法；打包无 -log 落盘尚待验证 | Editor/Game Development 通过；最终报告见下文，未宣称完整正式战斗日志验收 |
| 旧路径及残留 | 同一 Simulator/Runner 旧位置转速度公式、旧 `FullConstraintCorrectionCentimeters`、旧测试步长参数 | 不并挂两个生产求解器 | 删除旧公式/字段/无用参数；临时旧版对照仅留测试报告，其源文件已移除 | 新路径验证后核对引用与 diff | 保留锚定端解析解为同一约束的固定端特例；无旋转快照数值夹具是明确消费者 | 无旧求解器源码或临时 include 残留；此前六个序列化兼容字段因旧 WBP 引用未完全确认仍暂留，非本轮新增 |

contract：最终 `BuildCoupledDelivery2Editor.log`、`BuildCoupledDelivery2Game.log` 均成功；`CoupledDelivery2Report/index.json` 完整 Fishing 142 项为 135 clean、4 警告、3 既有失败，0 notRun。原耐久与两项坏竿收纳夹具失败保留，没有新增失败。报告和落盘测试日志位于 `Saved/Automation/FishingPhysics/`。中间 `CoupledDeliveryReport` 的新增失败来自地形夹具遗漏必需步长/收线参数，已补齐并在最终报告重验，不能引用该中间报告为通过证据。

runtime_behavior：旧求解器对照 `FormalBaselineReport` 用正式杆标定，在 60/120 FPS 下末 5 s 各出现 84 个卸力固定步，张力约 0–331 N、身体速度 0–160 cm/s。最终夹具持续外冲 6 s，观察 2–6 s：5.535 kg 的三个帧率均约 49.626–49.676 N、53.628–53.882 cm/s；15 kg 约 49.026–49.122 N、133.994–134.396 cm/s，均零卸力步。它覆盖实际 CMC Walking、Rod/Controller 和正式挂点；地形交接另经真实水域用例验证，并非完整真人 Session 录制。最新日志 `CoupledDelivery2Tests.log`；正式复测可按 `LogCatFishing/Event=fishing_constraint_sample` 的 `SessionId/StepId` 检索 `RodRotationPredicted=true`、`LineTensionN`、`PositionCorrectionCm`、`ConstraintRodEnd`，对照移动和杆旋转事件。

presentation_delivery：未重新打包，未完成正式地图鼠标操作、反抗/平静全程、低于 20 FPS/卡顿追赶、移动障碍及真人房主/客户端杆弯曲验收，也未验证新包无 `-log` 双端落盘。当前是保留 CMC 的联合预测修复，不是所有实体在 Chaos 中同时提交的刚体求解器；Fishing 模块仍未整体验收。

### 引擎现成能力与自定义边界

依据本机 UE 5.8 源码核查：`Engine/Private/Components/CharacterMovementComponent.cpp` 在胶囊模拟刚体物理时跳过普通角色移动；`PhysicsConstraintComponent.h` 的约束对象为 PrimitiveComponent；`RootMotionSource.cpp` 的 ConstantForce 最终生成的是位移/速度型 Root Motion；`CableComponent.cpp` 求解绳粒子而未把端点反力接回本项目 CMC。不能仅因类名含 Force 或 Constraint 就直接替换 N 张力或角色动力学。

| 能力 | 当前情况 | 本轮判断 |
| --- | --- | --- |
| 角色移动、碰撞、滑动、网络纠正 | 已使用 CMC；自定义牵引接入 CalcVelocity/SavedMove | 属于引擎扩展，继续复用；不能另加 Actor Tick 位移写口 |
| 鱼动力学、绳长和共同张力 | 主要由 Simulator 自定义，与 Chaos 约束职责重叠最多 | 如双方都改为刚体，可评估 Physics Constraint 和物理子步；对当前 CMC 角色不是即插即用替换 |
| 受载转杆 | 自定义转矩/操控响应模型 | 本轮预测复用同一模型；未来全刚体方案才考虑角度约束/驱动替换，不能同时保留两个姿态权威 |
| 绳子和杆弯曲表现 | 表现独立消费现有端点和张力 | Cable Component 可服务视觉绳段，但不能单独修复本次共同力不连续 |
| 阶段、体力、耐久、收线玩法和终局 | 项目规则，由 StateTree/ASC/Session 等现有基础设施承载 | 仍需业务代码，不属于物理引擎会自动提供的玩法 |

因此不以未经统计的代码行百分比声称“多少在造轮子”。可替代程度最高的是基础动力学和约束，但是否更好取决于是否接受角色整体物理架构迁移。本轮选择保留 CMC，把自定义范围收在鱼线耦合与玩法；没有新增 Chaos/Cable/Mover 插件、另一个网络移动框架或假张力保底。参考：[Physics Constraints](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-constraints-in-unreal-engine)、[CMC 网络移动](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine)。
