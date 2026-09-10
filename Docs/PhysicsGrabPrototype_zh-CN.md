# 物理抓握原型使用说明

## 2026-09-10：起停响应与镜头独立朝向

用户要求起步/停步不漂移，普通侧移时猫转向移动方向，Controller继续独立控制镜头。基线19aa1d9，工作区仅用户Cat_Skeleton.uasset修改（SHA256=3AF77F901B4FD35EAF79A7133230BB8964A1F2E886064010F680A40C3DE18008）；上轮196项195通过，既有StarterRod耐久150/500差异保留。本轮主工程构建/运行与正式资产渲染已完成，具体结果及未验收范围见表后。证据根目录`Saved/Automation/MovementResponse-20260910`。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 自主移动计算 | `Source/Catfishing/Character/Physics/CatPhysicalBodyComponent.cpp::UpdatePhysicalMovement`由PrePhysics Tick调用；原普通电机VelocityError/0.22、上限450cm/s² | 起停慢、指数拖尾；目标普通无载移动快速达到目标/停住，同时保留真实碰撞及外力 | 原地替换为按实际物理步消除速度误差的有限驱动力（沿Chaos子步总时长上限与NetworkDeltaTimeScale，非子步才用MaxPhysicsDeltaTime），普通加/减速上限6000cm/s²；钓鱼仍沿原FishingMotorMaxForce上限、站定弹性/零预算契约，不直接清除刚体速度 | 先服务器公式→运行轨迹→联机观察 | 60/120Hz、0.12秒卡顿、100/300cm/s起停距离、真实推拉、零预算、鱼力与费用；力单位kg·cm/s²不改 | 正式/原型×60/120Hz×100/300cm/s×有无120ms首帧卡顿共16组通过；正式正常100cm/s松键距离0.50–0.57cm、300cm/s为6.56–6.62cm，100ms后水平速度<0.9cm/s；有限力仍可被外力拖动 |
| 已连接身体的自主力 | `Body::UpdatePhysicalMovement`与`Grab::GetGripState/GetGripTarget`；Report-Initial无鱼对拉中助手新电机把主控反拖193cm | 快速空载电机不能同时抬高朋友持续抓拉能力；目标普通自由行走快起停，已经抓住或被抓住继续原物理拉力 | 新增Body只读连接查询，含自身实际握点和他人指向本Actor的握点；非钓鱼真实连接恢复原0.22s/450cm/s²电机，主控原FishingMotorMaxForce仍单独权威；无新握点、成员或费用状态 | 实际Grip关系→每步选择唯一电机→原关节解算；松握自然恢复快速自由移动 | 原无鱼对拉方向/关节间隙、直接抓猫和被抓、最后释放恢复响应；不修改失败测试门槛 | Report-Final原无鱼对拉与抓猫用例通过；助手+X输入仍被主控向-X拉动约170cm；原关节/目标/GripId/力量上限保持。连接状态实际消费两种响应，不是无消费者旧分支 |
| 朝向目标/状态 | 同函数原Forward恒等于ViewInput水平朝向；`SetViewIntent`由Character::FaceRotation和Controller调用 | 原地镜头拖着猫转，侧移仍看镜头；目标普通走路面向MoveInput，停止维持最后朝向 | 维护唯一物理朝向目标；无瞄准普通移动读世界MoveInput，伸手/握竿仍按ViewInput瞄准；180度采用有符号角差避免叉积为0；转矩仍是唯一身体朝向写口 | 目标选择→有限朝向转矩；初始化/传送重置目标，离地/倒地原规则保留 | A/D/S/斜向移动、原地镜头90度、回头180度、伸手抓猫、主控鼠标/跳跃；不新增Controller旋转写口 | 原生侧移90度、后退180度、停步转镜头、主动伸手朝向均通过；正式实际D键服务器BodyYaw=89.932、ControlYaw=0，客户端身体与实际默认相机分离断言通过 |
| 输入/网络/退出 | `Framework/Game/CatfishingPlayerController.cpp::Move/StopMove`→Body::SetMoveIntent→ServerSetInput；PostPhysics Snapshot→客户端插值 | 原30Hz轮询增加起停延迟；输入和结果仍须单一服务器裁决 | 起停/换向边沿立即复用现有RPC，30Hz重发保留；边沿后物理快照及时发布，保留ControlEpoch/Sequence/超时与ClearControlIntent入口；收紧客户端跟随响应，不实施预测/回滚 | 请求→原裁决→物理结果→快照；失焦不增加第二清理路径 | 正式IP双端真实IMC按键/松键/失焦；默认Log记录输入边沿与快照，不在Tick刷屏 | 起停即时复用RPC并在下一物理结果快照ForceNetUpdate；微小模拟输入起停也发送。输入Sequence/ControlEpoch沿原门，客户端插值响应由20/s收紧到60/s；正式按键/松键/Flush与原抓握/镜头回归通过，未新增预测 |
| 表现与相关测试 | `CatCharacterMovementComponent::RefreshPhysicalObservation`→ABP/`PhysicsPrototypeVisual::RefreshVisualPose`→四足IK/手CCD；正式网络测试原要求原地转镜头身体也转 | 现有消费者继续读真实速度/身体姿态；旧测试绑定的镜头跟身假设需替换 | 不改动画/IK/镜头生产逻辑；原测试先验证镜头独立，再主动伸手转向原目标并继续真实抓拉；新增原生起停与普通朝向专项，正式双端验证 | 物理接收→已有消费者→更新过时断言 | 正式BP姿态、控制器视角、抓握HUD、轻道具和搏鱼组合回归；不只靠编译验收 | 正式渲染Report-RenderedFinal 5/5通过；默认相机方向断言通过后，用独立观察镜头截图完整猫的停步/侧移姿态，测量区间无截图阻塞。未改ABP、IK、骨架或镜头生产源码 |
| 配置/资产/存档/资源/Cook | 正式`/Game/Character/BP_CatCharacter`继承原生Body；`CatCharacter::BeginPlay`读取原CMC速度/跳跃/重力；原型Pawn直接Initialize；正式IMC/WBP与Cook入口不改 | 移动响应在唯一Body调整；旧CMC只作原配置/动画观察消费者，不重新启用 | 保留移动速度/跳跃/重力配置入口，无资产或存档迁移；库存/费用/Session写口不涉及修改；不删除未确认二进制引用 | 原生接线→正式BP加载→哈希核对 | Blueprint实际运行确认新路径；Skeleton保持hash；钓鱼原力量/账单与零体力测试 | 正式BP与原生共享Body已实际运行，速度/跳跃/重力仍读原配置；用户Skeleton hash不变。原搏鱼/费用/零预算回归通过，无BP/WBP/IMC/存档/资源/Cook入口修改；未运行新Cook/包 |
| 跳跃首帧/截图测时 | `Body::UpdatePhysicalMovement`在排队Jump AddImpulse后、首个Chaos步前以旧Velocity.Z≤0判断结束分离；Rendered日志空手首帧Grounded=1且顶点154.74cm，持竿124.56cm；正式网络截图调用阻塞导致计时段增加一慢帧 | 跳跃冲量尚未消费不能判为下落；截图不能人为污染起停比较 | 首次PostPhysics跳跃发布前禁止清除bJumpSeparating；新增真实120ms首帧专项；普通走停计时先完成，再用观察镜头独立截图，不放宽原起停距离门槛 | 请求排队→唯一物理步→首次快照→原正常下坠/支撑；定量计时→渲染观察 | 保持420cm/s/重力/支撑系数，首帧非Grounded、原75–100cm跳高；原跳跃和轻道具渲染再验证 | 独立检查点88a1ce7；120ms首帧跳高84.453cm且不接地，正常落地恢复；最终渲染空手/持竿顶点124.709/124.539cm。截图已移出起停计时，未放宽原距离或跳高断言 |
| 文档/日志/交付 | 本页与`Docs/Development/需求对齐差距清单.md`；Body默认Log及现有自动化构建入口 | 此次局部修复不代表整个角色/联机模块完成 | 按contract/runtime_behavior/presentation_delivery记录最终结果、删除本范围旧错误说明，独立中文检查点；无新业务账本 | 实施→验证→最终diff→提交 | Editor/Game Development、正式运行日志/截图；未Cook/新包/高延迟完整预测明确列缺口 | 主工程BuildEditor-HitchGuard与BuildGame-HitchGuard均成功；197项组合196通过，末轮5项专项/渲染全通过。结果同步唯一差距入口；正式地图真人手感/高延迟预测/新包默认日志仍未验收 |


本轮证据分层（均相对项目根目录）：

- **contract**：`Saved/Automation/MovementResponse-20260910/BuildEditor-HitchGuard.log`、`BuildGame-HitchGuard.log`均为实际主工程Win64 Development成功；最终源码与模块摘要见同目录`MainSourceManifest-Delivery.json`、`MainBinaryHashes-Delivery.json`。
- **runtime_behavior**：`Report-Final/index.json` 197项中196通过（186 clean、10带既有警告），唯一失败仍为StarterRod耐久150/500。完整相关抓推、轻道具、身体、IK、正式搏鱼60/120Hz/卡顿、主控费用与网络协议通过。最后小幅输入边沿发送衔接和跳跃首帧保护后，`Report-RenderedFinal/index.json`再验新增跳跃、原三刚体跳跃、起停朝向、正式输入抓拉和轻道具网络五项，5/5通过（2 clean、3带既有ABP启动警告）。早期Report-Initial的助手拉力变化、Report-Rendered的截图测时污染/跳跃额外抬升均已保留为失败证据，不能替代最终报告。
- **presentation_delivery**：已查看`Saved/Automation/MovementResponse-20260910/Images/20260910-060155-formal-stopped.png`、`20260910-060156-formal-side-walk.png`；实际正式BP猫、Controller、默认镜头方向与真实按键链均经服务器/客户端核对。截图采用计时完成后的观察镜头，未修改正式相机资产。最终实际按键松开后服务器移动2.444cm、客户端追齐6.235cm，0.25秒观察时服务器水平速度0.560cm/s、两端位置差<1cm；该距离含输入/网络/插值延迟，不应描述为网络端零延迟。正式Lake地图真人手感、高延迟/丢包下整套运动预测、新Cook/Development包无-log双端落盘尚未验收，本轮不关闭角色/Fishing/Delivery模块。

操作结果：普通自由移动快速起停，猫面向世界移动方向，停止保持最后身体朝向；只转镜头不会改变该朝向。主动伸手和主控持竿仍面向独立瞄准方向；已建立抓握或被抓住时沿用原有限物理拉扯响应，受到朋友/鱼拉动不会被强制清速。正常100cm/s松键后约0.5cm，300cm/s约6.6cm；120ms卡顿帧下分别约6.1/17.4cm，无持续缓动或倒冲。加速度/阻尼限值改变的是自由移动响应，不改变FishingStrength、主控力量预算、跳速或重力配置。

默认Development日志分类`LogCatPhysicsGrab`，新增`physics_body_movement_requested/accepted/snapshot`，保留`physics_body_input_rejected/timeout`、`physics_body_control_changed`和跳跃事件；`Tests-Final.log`与`Tests-RenderedFinal.log`按World/NetMode、BodyId、ControlEpoch、InputSequence关联请求、服务器接受和物理后快照，BodyYaw/ViewYaw展示视角分离。新包默认落盘尚未实测。无源码/资产删除；旧普通全时镜头朝向和自由行走0.22s路径已替换，0.22s仅作为真实抓握连接的现役拉扯响应保留，其消费者由原抓拉测试确认。

## 2026-09-10：四足落脚 IK 与步幅匹配

正式 `BP_CatCharacter` 和原型 Pawn 共用 `UCatPhysicsPrototypeVisualComponent` 的最终姿势通道：原动画 → 四足步幅/地面求解 → 抓握前爪 CCD。`FCatQuadrupedLocomotion` 只读物理身体、动画及场景碰撞，修改可见 PoseableMesh 的腿部旋转和有限骨盆偏移。身体、手球、碰撞、受力、费用与网络裁决仍由原有物理/抓握系统负责。

当前 Cat 骨架的 LF/RF/LB/RB 四条腿各有三段骨骼，使用从原姿势开始的 FABRIK 保持骨长。运行时读取压缩动画中支撑脚后移的轨迹，标定 Stand/Walk/Run 三个现有原地素材；按实际混合权重、素材时间推进量和可见模型缩放计算参考速度。步幅比例为相对支撑面的身体速度除以参考速度，默认限制在 0.6–1.6。原型的私有 Walk 播放器也使用该标定速度，移除原先固定 100 cm/s 的参考值；正式 ABP、Montage、动画通知和跳跃根骨补偿保持原通道。

每只脚独立探测地面并调整脚底朝向；支撑阶段保存支撑组件/骨骼的局部落脚点，抬脚、支撑变化或超距时解除。地面姿势不会把脚强行锁在无法到达的位置。前爪伸出时让出该脚控制，随后由现有抓握求解器处理；离地、身体不可行走、翻倒、非步行动作和明显滑动时淡出。复位或大幅位置变化清理历史足点。水平移动平台的速度另行采样，不把平台位移算进自主步幅。

组件的 `Catfishing|Locomotion / LocomotionSettings` 可配置开关、步幅范围及偏移界限。`MaxFootOffsetCm=8`、`MaxPelvisOffsetCm=3`、`MaxPlantDriftCm=5` 均为**未缩放的模型厘米**，运行时乘可见模型缩放一次；`BlendSeconds=0.12` 为秒。关闭 `bEnabled` 淡出落脚及步幅姿势修正，原型的素材速度标定仍保留。超出腿长、步幅范围或地形修正范围时允许残余误差，不拉长骨骼或移动真实身体。当前只支持已审计的 Cat 骨链和上述三个原地素材；其他动作保留原动画。

本次接入基线为物理检查点 `6b04247` 和独立旧预算清理检查点 `6358679`。此前在 `Saved/Validation/LocomotionIK-20260910` 已完成不含 IK 的 Editor 构建；用户 `Cat_Skeleton.uasset` 修改保留，不保存任何动画/Blueprint/骨架资产。下表是本轮执行与审查材料，后续模块状态仍归需求对齐差距清单。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 最终动画消费者 | `Source/Catfishing/Character/Physics/CatPhysicsPrototypeVisualComponent::RefreshVisualPose`；正式 `ACatCharacter::BeginPlay` 与原型 `BeginPlay` 调用 `InitializeVisual` | 原动画复制后只做抓握 CCD；新增四足地面及步幅修正，保留原源姿势/根骨补偿 | 新 `Character/Animation/CatQuadrupedLocomotion::Apply` 接于 ReachAlpha 更新后、手 CCD 前；原 ABP 不变 | 动画和 PostPhysics 已完成后求解，抓握最后处理 | 正式 BP 实例、原型实例、骨长/根骨/动画源不变及双端消费者测试 | `FormalBlueprintKeepsAnimationAndPhysicsAuthority`、原型滑动测试及正式双端测试通过；见下方最终报告 |
| 速度与步幅 | Visual `UpdateBaseAnimation` 原 `WalkReferenceSpeed=100`；正式 ABP 引用 `/Game/Animalia/Cat/WalkToRun` 的 Stand_00-IP、Loco_Walk-IP、Loco_Run-IP | 旧值未按素材标定；新值区分模型 cm/s、世界 cm/s、播放倍率，三者只换算一次 | `GetReferenceSpeedMeshCmS` 标定原型播放率；`ReadAnimation` 读取正式真实混合/播放速度；`Apply` 调整步幅 | 先校准素材，再匹配剩余速度差，不改变正式 Montage 或动画推进 | 多速度实测原动画与修正后支撑脚位移；实际 ABP 图检查 | 固定参考常量已删除；Walk 标定 32.655 模型 cm/s。原型三档支撑脚滑动减少约 69%–79%，正式三档约 64%–98%；这些是连续落脚样本的比较，不是全动画零滑动承诺 |
| 地面、脚底和骨盆 | 新 `Apply/SolveFoot`，消费身体 `GetCollisionObjectType/GetCollisionResponseToChannels` 对应命中及 Cat LF/RF/LB/RB 骨链；与并行交互碰撞修复的物理支撑查询一致 | 原脚掌可能穿地/悬空；新增限距落脚、坡面脚底朝向、有限骨盆偏移；Visibility-only 交互盒不能成为脚下支撑 | 可见骨骼旋转及 RigPelvis 局部平移；RigRoot/Actor 不写 | 支撑局部点随支撑移动，超距或抬脚解除，再保长求解；不以交互射线代替身体碰撞约定 | 台阶、坡面、移动支撑、交互盒穿过；物理位姿/速度与动画源不变 | `TerrainPosePreservesPhysicsAndBoneLengths` 通过：3 cm 台阶抬脚 3.016 cm，支撑上移 1 cm 时脚随动 1.002 cm；12°坡面脚底朝向、交互射线仍可命中但不抬脚、骨长及物理状态不变均通过 |
| 权威、复制与退出 | 只读 `UCatPhysicalBodyComponent` 的速度、输入、支撑、可行走状态、ResetEpoch；手 ReachAlpha 来自已有 Grab | 不添加第二份运动状态或 RPC；身体控制失效时必须让出姿势控制 | `Apply` 淡出/清理，Visual `DestroyVisualComponents` 调用 `Reset` | 先消费物理快照；抓握后解；销毁清理弱引用及足点 | 实际跳跃/复位/抓握、身体禁用、Listen/Client 与原物理回归 | `GrabJumpResetAndDisableReleaseFootControl` 与原有抓握/跳跃/翻身/静态支撑回归通过；服务器继续模拟、客户端只读快照；未增加 RPC 或物理写入口 |
| 配置、资产及持久化 | Visual 原生 `LocomotionSettings`；正式 `/Game/Character/BP_CatCharacter` → `/Game/Animalia/Cat/ABP_Cat` → WalkToRun | 新参数默认开启，单位见上文；无新资产保存、Cook 入口或复制字段 | 保留现有骨架/动画/Blueprint 与加载引用；审计图节点确认地面 IK 唯一负责方 | 不删除二进制资产或兼容入口；资产审计先于最终验收 | 骨架 hash、ABP 实际图、正式运行类、Game 构建 | 实际 ABP 节点及三个 BlendSpace 素材审计通过，没有另一套地面/步幅求解节点；Editor/Game 构建通过，用户骨架 hash 不变。UI/WBP、存档、资源扣费不涉及；无旧兼容入口需要删除 |
| 诊断及验证入口 | 新 `LogCatLocomotion`；既有 `Build/Automation/verify_physics_grab_prototype.ps1`；新 Runtime/Editor `Character/Animation/Tests` | 需要 Development 默认日志解释何时修正/退出、真实步幅及足端误差 | `locomotion_clip_calibrated`、`locomotion_pose_sample`、设置/骨架拒绝事件；正常采样至多每秒，状态切换即时 | 日志按 BodyId、World、NetMode、Authority、LocalRole 对照两端，不建立玩法状态 | `Catfishing.Locomotion` 专项，定向旧手/跳跃/物理回归；真实客户端截图 | 26 项组合回归全部通过（23 clean、3 带原 ABP 启动 warning）；另两项正式网络渲染通过，已检查站立/行走画面。完整层级边界见下文 |

验证入口：`Build/Automation/verify_physics_grab_prototype.ps1 -Mode BuildEditor -RunName LocomotionIK-20260910`，随后 `-Mode Automation -RunName LocomotionIK-20260910 -Filter Catfishing.Locomotion`。加 `-Render -Filter Catfishing.Locomotion.Network` 获取正式客户端画面。试玩当前已编译原型可执行 `Scripts/launch_physics_grab_prototype.ps1 -RunName LocomotionIK-20260910`；正式工程已写入源码，但当前打开的旧编辑器需要重新编译并重启后加载。

最终 `contract`：`Saved/Automation/LocomotionIK-20260910/BuildEditorFreshPose.log`、`BuildGameFinal.log` 均成功；七个交付源码与冻结副本的 SHA256 记录在 `IKSourceManifest.json`。增量验证期间一次手工复制保留了早于旧 obj 的时间戳，导致执行旧地面筛选，已明确刷新副本时间并重编求解器；以之后报告为准，不能使用早期失败报告宣称该过滤已生效。标准验证脚本已有内容变化刷新时间戳保护，本轮未改该脚本。

最终 `runtime_behavior`：同目录 `Report-20260910-111703-781/index.json` 为 26/26 通过、0 failed、0 notRun；六项 `Catfishing.Locomotion` 专项全部通过。正式 100/200/300 cm/s 请求档实际速度约 93.77/187.57/281.93 cm/s，原型 25/60/100 档约 22.73/54.58/91.24 cm/s，未为了凑目标速度修改物理驱动。连续落脚样本中，正式客户端滑动总距离由 224.583 cm 降至 71.661 cm；累计值来自多脚/多帧比较。真实日志 `Automation-20260910-111703-781.log` 按 `LogCatLocomotion` 和 `BodyId` 对照房主/客户端。3 项 warning 来自原有 `ABP_Cat` 启动除零，前置物理验证日志 `Saved/Automation/PhysicalGrabIntegration-20260909/Automation-MainFormalJumpRender-20260909-174257.log` 也有此项；未在本轮修改该二进制动画蓝图。

`presentation_delivery`：`Report-20260910-112126-962/index.json` 的正式步幅与原正式跳跃两项真实网络渲染均通过（均带上述原 ABP warning），日志为 `Automation-20260910-112126-962.log`。已检查 `Saved/Validation/LocomotionIK-20260910/Saved/Automation/Locomotion/Images/20260910-032146-FormalStandingIK.png` 与 `20260910-032148-FormalWalkingIK.png`。尚未完成正式地图真人多人手感、长时间低帧率/高延迟观察、新 Cook/打包及打包双端不加 `-log` 的默认落盘验收；这次局部交付不关闭 Character/Growth/Condition 或 Delivery 模块。并行轻道具玩法不在本轮冻结副本内，其新增支撑忽略策略由该物理任务后续同时接入身体与 IK 两处查询。

## 物理原型说明

这是独立的身体与抓握试验场，使用当前猫模型、动画和程序前爪姿势验证刚体接触、双向传力、固定支点承重与释放。2026-09-09 起，身体驱动已抽到 `UCatPhysicalBodyComponent`，由原型 Pawn 和正式 `ACatCharacter` 共享；正式抓握合作接入范围与当前验证结果见 [钓鱼架构](FishingArchitecture_zh-CN.md)。下文早期检查点中的“正式路径不变”只描述当时的原型隔离范围。

## 入口

地图：`/Game/Catfishing/Prototypes/PhysicsGrabPrototype`。地图绑定 `ACatPhysicsPrototypeGameMode`，运行时生成两只猫、动态竿、箱体、地面、墙和平台。原型竿仅是物理道具，还没有钩、线、鱼或正式钓鱼会话。

本项目编辑器可能仍加载旧 DLL，所以验证与试手使用独立工程输出：

```powershell
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode Prepare
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode BuildEditor
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode CreateMap
pwsh -File Scripts/launch_physics_grab_prototype.ps1
```

启动脚本只在 `Saved/Validation/PhysicsGrabPrototype-20260909` 内配置本地 IP 运输。它不会关闭正在使用的编辑器，也不会修改正式项目的 Steam 运输配置。试验场不是正式主菜单的入口。

两人本地试验分别执行：

```powershell
pwsh -File Scripts/launch_physics_grab_prototype.ps1 -Mode Host
pwsh -File Scripts/launch_physics_grab_prototype.ps1 -Mode Client
```

默认端口 7779。第二台电脑使用同一构建，`-Mode Client -Address <房主局域网地址>`。这里验证的是物理原型的 IP 联机，不代表 Steam 房间、旅行和正式多人游戏已验收。

## 操作

| 输入 | 行为 |
| --- | --- |
| WASD | 相对视角移动；移动通过有限的身体驱动力实现 |
| 鼠标 | 转向、抬头或低头，决定伸爪方向 |
| 按住左 / 右键 | 伸出对应前爪，真实手球接近碰撞表面后建立抓握 |
| 松开对应按键 | 立即请求释放该手的约束 |
| Space | 有脚下支撑时跳跃 |
| R | 先解除自己的及别人抓住自己的连接，再回到出生点 |
| Tab | 单机切换猫；未控制的另一只猫仍参加物理模拟 |
| F1 | 切换抓点辅助显示：青色伸手、绿色已抓住、黄色接触点 |

当前猫约 30 cm 高，前肢骨长约 19.6 cm。需要靠近再伸手；低头可抓地，转向身旁的猫可抓住它。臂展限制使用厘米，刚体力使用 kg·cm/s²。正式合作钓鱼为各猫设置独立 motor 预算，ASC 的牛顿力量乘100后施加，CMC 的旧 Mass 不作为刚体质量来源。

## 2026-09-10：轻道具与正式持竿

原型动态箱/竿与正式鱼竿共用轻道具策略：任意手抓住时自身重力0，最后松手后25%重力轻落、线阻尼2和角阻尼4；有鱼力时恢复原阻尼且不截断速度。实体碰撞仍存在，轻道具碰猫只纠正道具，猫之间仍可双向推抓。动态轻道具不作脚底或起身支撑，固定地形保持原行为。

正式主控用R取放本人的竿，并保留原持竿偏移、鼠标瞄准及搏鱼转竿。主控持竿采用受控姿态，鱼线力只写入主控身体一次；助手抓猫或抓竿后通过真实约束拉动，不按R加入、不共享体力或收费，也不自动接任。抓受控竿的约束连接到主控身体对应握点，主控释放后同一GripId改接自由竿；助手还抓着时竿仍无自重。原型的R依旧是复位，不是正式鱼竿命令。实现盘点与验证见`FishingArchitecture_zh-CN.md`本轮轻道具一节。

## 运行边界

- 身体是一个受控刚体，两只前爪各是球刚体。肩驱动和接触约束传递双方反作用力；脚点支撑与有限姿态驱动辅助站稳，近地侧躺/倒扣可通过物理转矩恢复站立。它还不是完整多关节主动布娃娃，没有正式多阶段起身动画或爬回平台动作。
- 当前模型和动画只作为表现源，起跳、空中与落地使用已有 JumpX 片段，程序 CCD 在身体动画和过渡混合之后让前爪追随物理手，保留骨段长度。原模型、ABP 和原 Physics Asset 不需要保存修改。
- 服务器唯一模拟身体、手爪、道具与抓握。客户端提交输入并插值权威快照，没有实现完整物理预测和回滚。延迟手感需要单独评估。
- 抓握快照包含独立 GripId、版本、目标 Actor / Component、骨骼和目标局部接触点。松手、失焦、切换控制、目标销毁、断线和复位都走明确的解除入口。
- 正式项目已接入真实身体和抓握；鱼竿名册只保留唯一主控，原虚拟共同速度/固定队形与辅助费用不再生效。原型和局部验证不关闭 Fishing、角色或 Delivery 整模块。

## 验证与日志

```powershell
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode Automation
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode Automation -Render -Filter Catfishing.PhysicsGrabPrototype.Network
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode BuildGame
```

报告在 `Saved/Automation/PhysicsGrabPrototype-20260909`，隔离工程的默认游戏日志在 `Saved/Validation/PhysicsGrabPrototype-20260909/Saved/Logs`。启动脚本不需要 `-log` 窗口。

主要过滤词：`LogCatPhysicsGrab`、`physics_grip_created`、`physics_grip_observed`、`physics_grip_released`、`physics_body_motion`、`physics_prototype_visual_ready`、`physics_prototype_animation_changed`。用 `GripId` 关联同一抓握，用 `BodyId` 关联身体；分别核对房主与客户端的 World / NetMode 和状态。

倒地恢复另查 `physics_body_ground_recovery_changed`，其中 `Active` 表示近地翻身电机是否启用，不表示已有脚下支撑或允许跳跃。

测试分层：类型与骨长约束属于 `contract`；真实 World/Chaos 与 Listen 客户端 RPC 测试属于 `runtime_behavior`；渲染截图与实际操作属于原型的可视验证，不等于正式模型、正式动画或打包联机的 `presentation_delivery` 完成。实际结果与持续缺口归入 `Docs/Development/需求对齐差距清单.md`。

## 原型创建时的影响与交接（历史检查点）

修改前已在对话完成职责盘点。正式编辑器的既有 PIE 使用原 DLL，本轮构建使用隔离输出。实施期间观察到 `Cat_Skeleton.uasset`、`Config/DefaultGame.ini` 和 Fishing 源码/测试的并行修改，本轮没有纳入这些变更；交付时钓鱼变更已由其他任务提交，骨架修改仍保留。首次专项测试尚未运行；实现后的首轮夹具失败保留在报告中，不能当成既有游戏缺陷。

下表的源码路径均相对 `Source/Catfishing/`；这是本轮变更的技术交接材料，模块状态仍只记录在需求对齐差距清单。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 正式角色与钓鱼入口 | `Character/CatCharacter.*`、`Framework/Game/CatfishingPlayerController.*`、`Fishing/Actors/CatFishingRodActor.*`；正式 GameMode/BP 及操竿名册消费这些入口 | 正式 CMC 移动及按键加入继续运行；本轮目标为独立可试玩的物理抓握 | 保留正式路径；独立 `Framework/Game/PhysicsPrototype/CatPhysicsPrototypeGameMode` 选择原型 Pawn/Controller/HUD | 先准备原型接收方，再绑定独立地图；本轮不切换正式生产入口 | 不删除仍有消费者的 CMC、旧操作位和资源权威；正式钓鱼回归未运行 | 原型未调用操竿名册、费用或存档；正式路径是仍在使用的消费者，不是待删废案 |
| 身体、移动和支撑 | 新 `Character/Physics/CatPhysicsPrototypePawn`，由原型 GameMode `RestartPlayer` 创建/占有；Tick 调用 `UpdatePhysicalMovement` | 4 kg 身体、两只 0.12 kg 手球参与 Chaos；输入 X 前进/Y 横移，100 cm/s，有限驱动力；四脚点辅助站稳 | 新增身体与手球、肩驱动、跳跃及复位；不沿用正式角色力量字段 | 先创建真实刚体，再配置 Hand/Body 约束及抓握组件 | 互推、墙碰撞、下落及真实位移；不能仅靠坐标赋值证明物理 | `RealBodyFallsPushesAndStopsAtWall` 通过；墙前中心 x=136.997 cm，身体半长 13 cm，墙面 x=150 cm |
| 抓取与双向力 | 新 `Interaction/Grab/CatPhysicsGrabComponent`；Controller → Pawn `SetGrabInput` → 组件 RPC → `UpdateHand/TryLatch` | 按住持续伸手，手球接近表面 1 cm 内建立约束；18 cm 伸手长度，600 N 接触断力；松手释放 | 同一约束路径支持角色、动态物体及静态表面；目标记录 Actor/Component/Bone/局部点与独立 GripId | 肩驱动先可用，接触约束随后创建；仅服务器建约束 | 抓猫位移、抓地、静态承重、释放后下落、动态竿位移 | `RealGripTransfersForceAndCleansUp`、`GroundGripAndStaticContactBearBodyWeight`、`ReachingGripsAndMovesThePhysicalRod` 通过；动态竿移动 49.675 cm |
| 当前模型表现 | 新 `Character/Physics/CatPhysicsPrototypeVisualComponent`；Pawn 初始化并传入物理手/伸手状态，PostPhysics 复制动画并解前爪 CCD | 继续使用 `/Game/Animalia/Cat/Meshes/Cat` 和 Stand/Walk；伸出的前爪追随真实手球，不要求现有 ABP 有 IK | 运行时隐藏动画源 + 可见 PoseableMesh；三骨段保长，松手混回动画 | 刚体更新后消费手位置；客户端速度读 Pawn 的权威快照 | 视觉手与物理手距离、骨长、实际渲染；旧 ABP/PhysicsAsset 的完整二进制引用未确认，因此不修改或删除 | `ExistingCatPoseFollowsHandWithoutStretchingBones` 通过，稳态误差 0.291 cm；正式模型/ABP/PhysicsAsset 无本轮保存修改 |
| 输入与屏幕消费者 | 新 `Framework/Game/PhysicsPrototype/CatPhysicsPrototypePlayerController` 绑定原生键；对应 HUD 每帧只读 Grab 状态 | 左/右键各控制一只手，WASD 任意水平拉动，鼠标决定伸手方向；R 复位，Tab 单机切猫，F1 辅助线 | 原型 HUD 显示抓握状态；无新增正式 InputAction/WBP/DataAsset 绑定 | 先注册 Pawn/组件接口，再绑定 Controller 与 HUD | 松键、失焦、切换控制是否留下约束；抓握标记是否对应真实状态 | `FocusLossReleasesBothHands` 通过；正式 UI Subsystem 仍消费 `ACatCharacter`，独立 APawn 不进入该 UI 路径 |
| 权威与复制 | Pawn `Snapshot/ControlEpoch`；Grab `LeftGrip/RightGrip/InputEpoch`；Prop `Configuration/BodyTransform` | 服务器模拟唯一权威；身体/手/道具 30 Hz 快照，客户端插值；抓握 RPC 带 epoch/sequence | 原型不启用会让客户端重新模拟的原生 Movement 复制；客户端手球独立插值；未实现预测/回滚 | 服务器先裁决接触，再复制状态和位置；目标 Actor 必须可解析 | owning-client RPC、抓握状态、真实拉动、释放、目标退出两端核对 | `ListenClientGripForceReleaseAndTargetExit` 最终通过；真实客户端拉动 21.675 cm，第二次抓取后目标销毁，两端清理 |
| 失败与退出 | Grab `ReleaseHand/ReleaseAllFromAuthority/ReleaseTargetFromAuthority`；Pawn `ReleaseConnections/UnPossessed/EndPlay/ResetFromAuthority`；GameMode `Logout` | 松键、目标失效、超力/超距、复位、失焦和失去控制有统一解除入口；复位先解除别人抓住自己的连接 | 解除权威约束后复制释放；保留末次 GripId 关联释放日志 | 先清理连接，再重置刚体/更换控制；输入世代拒绝过时控制请求 | 实际约束是否解除，而非只清一个布尔值 | 真实抓取、双手失焦及目标销毁测试通过；实际 Logout/UnPossessed、旧 ControlEpoch 拒绝、延迟/丢包尚未单独运行验证，不从 Destroy 用例推导通过 |
| 场景和资产 | 新 `/Game/Catfishing/Prototypes/PhysicsGrabPrototype` 绑定原型 GameMode；GameMode `EnsurePrototypeArena` 生成几何和动态道具 | 两猫、物理竿、箱体、台阶、墙及平台；竿是抓取道具，没有正式钓鱼状态 | `Scripts/create_physics_grab_prototype_map.py` 只保存新地图；原生 Prop 复制尺寸/姿态 | 原生类构建完成后生成地图；灯光属于地图，碰撞几何属于服务器 | 原生地图绑定、客户端静态碰撞/姿态、原资产保存范围 | 生成与重载验证见本轮 CreateMap 日志；脚本不调用 SaveAllDirtyPackages |
| 灯光与道具材质 | 原型 Controller `EnsureLocalLighting`；Prop `ApplyConfiguration` 消费颜色配置 | 首次截图发现空地图主补方向光同优先级警告、道具仍用棋盘材质；目标为抓点清楚可见且配置颜色生效 | 补光注册前设主 1 / 补 0，已有地图灯则不生成；显式绑定 `/Engine/BasicShapes/BasicShapeMaterial`，缓存 MID 并核对 Color 参数 | 先修材质/灯光接收方，再由渲染测试加载实际新地图复核 | 检查警告与真实道具颜色；不更改基础材质资产 | 最终截图显示彩色场地与两猫抓握接触点，无旧多日光争用警告；实际地图加载成功 |
| 构建、配置与持久化 | `Catfishing.Build.cs` 增加 PhysicsCore；`Build/Automation/verify_physics_grab_prototype.ps1` 隔离构建；启动脚本只写隔离工程 Config | 正式 `DefaultEngine.ini` 的默认地图/GameMode 与 `DefaultGame.ini` MapsToCook 无本轮切换；试验场不写库存、ASC、账单或存档 | 新启动脚本提供本地 IP 试验；正式 Cook 白名单不加入原型 | 构建 → 地图 → 自动化/渲染 → 试玩；若需包体须另用明确 Cook 地图入口 | Editor/Game 编译、脚本解析、默认日志；不能把 Game 编译当成打包通过 | 证据见下述交付验证；资源迁移、费用、持久化写入不涉及；Cook/打包未运行 |
| 检查与日志 | 新 Runtime 测试及 `Source/CatfishingEditor/Character/Physics/Tests/CatPhysicsGrabPrototypeNetworkTests.cpp`；原型各日志分类 | 检查真实 Chaos、当前模型与 Listen 客户端；默认日志落盘；高频运动限频 | 保留请求、裁决、复制、解除及失败事件；测试结果与文档说明同一原型范围 | 每次源码变化后重建相应模块；报告保留失败与最终结果 | contract/runtime_behavior/presentation_delivery 分层核对 | 最终专项报告、构建和截图见下述交付验证；正式模块整体状态不关闭 |

## 交付验证

`contract`：最终 Editor 构建 `Saved/Automation/PhysicsGrabPrototype-20260909/BuildEditor-20260909-114611-586.log`、Game Win64 Development 构建 `BuildGame-20260909-114846-427.log` 均成功。隔离构建显式使用 `-ForceHeaderGeneration`，避免复制保留时间戳时复用行号过期的 UHT 宏；本轮源码与隔离副本逐个哈希一致。地图生成成功见 `CreateMap-20260909-113040-620.log`；随后渲染进程独立加载了该地图。

`runtime_behavior`：核心专项报告 `Report-20260909-114726-291/index.json` 为 8/8 Success，0 warning/failed/notRun；对应 `Automation-20260909-114726-291.log`。覆盖互推到墙、真实抓猫传力、抓地及静态承重、动态竿、当前模型骨长和前爪、失焦释放与 Listen 客户端 RPC。补充的 `RightHandSupportsAfterLeftReleaseAndResetClearsIncomingGrip` 证明双手同时抓墙后可单独松左手，由右手继续承重（身体 z=-0.898 cm），并在独立有地面场景中验证：双手仍抓墙、另猫同时抓自己时，R 解除自身左右及对方的入向连接。

原型 `presentation_delivery`：`Report-20260909-114801-453/index.json` 的实际地图侧视渲染网络用例通过，真实拉动 21.901 cm；有一条引擎 `r.MotionVectorSimulation` render-thread flag 警告，无抓握用例失败。图片 `Saved/Validation/PhysicsGrabPrototype-20260909/Saved/Automation/PhysicsGrabPrototype/Images/20260909-034821-gripped-and-pulling.png` 已目视检查：两猫、伸爪接触、彩色场地和中文 HUD 可见，旧多日光争用警告已消失。侧视相机仅供测试截图；玩家默认使用第三人称操作镜头。

2026-09-09 11:50 通过启动脚本打开 Solo 独立窗口，未使用 `-log` 或 `-abslog`。实际默认文件为 `D:/develop/Catfishing/Saved/Validation/PhysicsGrabPrototype-20260909/Saved/Logs/Catfishing.log`，已确认新的 `physics_prototype_arena_ready`、两个 `physics_prototype_visual_ready`、`physics_body_started` 及抓握创建/释放事件；启动与操作日志快照留在证据目录 `Solo-20260909-115036.log`。这证明单机编辑器 Game 窗口落盘；Cook、打包后双进程联机及延迟手感仍未验收，不等同于正式表现交付完成。

## 试玩反馈修复：跳跃表现

触发：原型 Space 已给身体施加跳跃冲量，但 `RefreshVisualPose` 只按水平速度选择 Idle/Walk，所以模型不播跳跃。原型首轮 8 项测试没有覆盖这一消费者。修改前仅骨架资产有并行改动，本轮继续保留且不保存原动画资产。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 起落输入与生命周期 | 原型 Controller Space → Pawn `RequestJump` → 物理状态与 `Snapshot` → Visual | 跳力仍为 220 cm/s 增量、禁支撑 0.25 s；初生/Reset 原有支撑布尔暂时无效 | 增加 `bSupportSampleReady` 快照有效位与只读 ResetEpoch 查询，不复制动画枚举 | 先准备可辨识的新样本，再由 Visual 消费；Reset 清除样本有效性 | 初生不误播落地、空中 Reset、客户端接收时序 | 真实 World 与 Listen 客户端跳跃/Reset 测试通过 |
| 动画与抓爪 | Visual 原 Idle/Walk 二态，PostPhysics 复制动画再 CCD | 起跳/空中/落地有对应姿势，原伸爪状态仍生效 | 复用 `JumpX_Start-IP/Loop-IP/End-IP`；起跳窗口 0.12 s、落地 0.18 s、姿态过渡 0.08 s；缓存 CCD 前的已混合姿势供重跳打断 | 基础选态 → 动画推进 → 根锁与混合 → CCD；离地可打断落地，真实落地可打断起跳 | 三态实际动画资产、空中 CCD、骨长、落地再跳 | 两项 Runtime 及 owning-client 网络测试通过；短离地保留 0.12 s 确认窗口，兼容支撑先变化、冲量后消费 |
| 根骨与资产 | JumpX 三段同 Cat_Skeleton；Start/Loop/End 分别 0.583/1.083/0.833 s，RootMotion 关闭 | 渲染发现 Loop/End 的 RigRoot 仍额外上移约 51/48 cm，使模型叠加刚体高度 | 仅在原型跳跃 Poseable 副本中，将根骨完整锁为参考变换，等价 RefPose RootLock；不写共享动画资产 | CopyPose 后先锁根，混合后缓存基础姿势，再解前爪 | 可见根骨每帧等于参考根，原距离镜头截图复核 | 根锁断言通过；原二态分支与 `bUsingWalkAnimation` 已由统一状态选择替换，无第二个动画播放入口 |
| 配置、正式消费者与交付 | 原型使用说明、专项脚本、正式 CMC/ABP 与现有地图配置 | 操作、正式钓鱼与持久化不变；无新增 WBP/DataAsset 或地图迁移 | 更新本说明与唯一进度入口；日志增加 `physics_prototype_animation_changed` | 验证新消费者后刷新独立试玩 DLL | Editor/Game、原抓握回归、运行时事件、实际渲染 | 已构建并运行；原 ABP/PhysicsAsset 仍有正式消费者，不删除；正式 Cook/打包未运行 |

最终集成验证：`Saved/Automation/PhysicsGrabPrototype-20260909/BuildEditor-20260909-121928-221.log` 与 `BuildGame-20260909-122003-953.log` 成功；`Report-20260909-121959-622/index.json` 为 13/13 clean（含同期倒地修复回归）。跳跃专项为 `PhysicalJumpPlaysAuthoredPhasesAndKeepsPawIK`、`JumpLandingCanBeInterruptedAndResetDoesNotFakeLanding`、`ListenClientJumpAnimationAndReset`。运行测试证明真实身体离地、两端三段动画资产、落地后重跳、空中重置、根骨对齐与 CCD；不把仅 `IsPlaying` 当作跳跃通过证据。

`presentation_delivery` 的原型渲染复核为 `Report-20260909-122034-046/index.json`，网络用例通过，仅保留引擎 `r.MotionVectorSimulation` 警告。三图在隔离工程 `Saved/Automation/PhysicsGrabPrototype/Images/20260909-042053-jump-{takeoff,airborne,landing}.png`，已目视猫留在原镜头内、空中姿态可见。完整变换日志显示源 RigRoot 额外 Z 约 51/49 cm，而可见根与参考根均为零位置、零旋转、单位缩放。截图保留默认运动模糊，落地图是阶段首帧；不能用单张图证明完整落地动作或正式美术验收。

## 试玩反馈修复：倒地恢复

触发：猫侧躺或完全倒扣后不能自行站起。原因是原扶正叉积在 180° 时为零；同时 `Body.Up.Z <= 0.35` 禁止脚点支撑，电机落入弱空中分支，100 rad/s² 的旧上限不足以克服侧躺身体接地边缘的重力转矩。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 翻身判定与转矩 | Pawn `UpdatePhysicalMovement` 的脚支撑门和扶正误差 | 翻倒无法进入站立支撑；空中弱电机不够翻身 | 以旋转 Box 的垂直厚度加 1 cm 向下查找法线 Z≥0.55 的近地接触，独立选择恢复电机；180° 时采用身体纵轴确定翻滚方向 | 接触成立才用角度 PD（300/s²、50/s、角加速度上限 650 rad/s²）；朝向恢复后回到原脚点支撑与电机 | 正负侧躺、倒扣、连续物理解算、稳定性；禁止直接 SetRotation/Teleport 修正 | 60/120 Hz × 90°/-90°/180° 六组均通过，最终 UpZ≈1、站立高度≈19.964 cm，ResetEpoch 未变化 |
| 操作、悬挂与网络 | 原 `bGrounded` 跳跃门、墙面抓握、服务器物理和快照复制 | 新近地接触仅可用于翻身，不能让抓墙悬空角色获得跳跃资格 | 保留脚支撑/跳力/普通站姿/空中驱动、抓握约束与复制入口；近地恢复不写 `bGrounded` | 物理翻身 → 正常支撑 → 原移动/跳跃请求 | 恢复后真移动/真跳跃；悬挂后请求跳跃再实际 Tick，不能只即时读延迟生效前的速度 | 恢复后实际行走约 92 cm、跳起约 20 cm；无地面墙面悬挂拒绝空跳，松手仍下落 |
| 清理、日志与交付 | `ResetFromAuthority`、恢复电机选择状态、`Tests/CatPhysicsPrototypeRecoveryTests.cpp` | 复位不能残留恢复选择；不引入另一套角色运动路径 | 复位清除电机选择，状态变化默认落盘；原脚支撑保留为恢复后的消费者 | 更新说明/唯一进度入口，增量构建与局部集成回归 | 无配置/资产/持久化写入；官方 CMC/ABP 不涉及，Cook/打包未运行 | `InvertedAndSideLyingBodiesRecoverThroughGroundContact` 与 `WallSuspensionDoesNotBecomeGroundedRecoveryOrJump` 均通过；不存在需要删除的第二套旧入口 |

此修复的 `contract/runtime_behavior` 证据同上最终 Editor/Game 构建及 `Report-20260909-121959-622/index.json` 13/13 clean。地面六种姿态、恢复后控制与悬挂边界由真实 Chaos World 证明；没有以跳跃截图替代完整倒地恢复的真人/联网美术验收，模块仍保持原未完成边界。
