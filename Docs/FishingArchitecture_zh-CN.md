# 钓鱼核心架构（技术文档）

阅读对象：需要理解/修改钓鱼玩法逻辑的人。运动链说明于 2026-09-04 按源码核对，当前细节统一见 [鱼运动与遛鱼逻辑实现导读](FishFightImplementationGuide_zh-CN.md)；本页负责系统关系与入口导航。
配套文档：蓝图任务步骤见《BlueprintTaskGuide_zh-CN.md》；规格口径见《FishingCoreFlow_zh-CN.md》。

---

## 1. 一张图看全貌

```
【输入层】玩家按键
   R / E / 左键 / 右键 / Q / F / X
        │  Enhanced Input（IMC_InputContext）
        ▼
【输入语义层】DA_CatAbilityInputConfig
   ├─ Native：E → IA_Interact → Cat.Input.Interact → Current Target → 服务器交互 RPC
   └─ Ability：R/左键/右键/Q/F/X → Fishing InputTag → ASC
        ▼
【GAS 层】6 个原生 GameplayAbility（可被蓝图子类化，仅承载"输入边沿 + 本地表现钩子"）
   UCatGA_FishingRodInteract / PrimaryAction / Slack / Chum / Scoop / Cancel
        │  Submit*()：把按下/松开变成一条带 RequestId 的命令
        ▼
【命令层】UCatFishingCommandComponent（挂在 PlayerController 上，唯一 RPC 边界）
   HandleAbilityCommandFromAuthority()：服务器按"当前事实"分派语义
   ├─ R   → 已占位=LeaveRod / 附近竿有空位=OperateRod / 否则=PlaceRod
   ├─ 左键→ 无会话按下=记录瞄准 / 无会话松开=BeginCast（视线∩水面）
   │        有会话按下=提竿(TrueBite) 或 拖(HookedFight) / 松开=停拖
   ├─ 右键→ HookedFight 中 按住=松开线杯 / 松开=锁住当前线长
   ├─ Q   → 按下记时刻 / 松开=按时长算蓄力 → 弹道预测落点 → PlaceChum
   ├─ F   → RequestScoop（服务器找范围内已上钩的鱼；成功直接变成抄手嘴叼世界鱼）
   └─ X   → 有会话=Cancel（咬钩前零损失）/ 无会话=收竿（操作中先 Leave 再 Pack）
        ▼
【服务层】UCatFishingService（World Subsystem，只在服务器存在）
   PlaceRod/OperateRod/LeaveRod/PackRod/BeginCast/RequestScoop/SubmitFightAssist
   持有：每人一根部署竿的 Registry、每人一个活跃会话的槽位、BeginCast 幂等缓存
        ▼
【会话层】ACatFishingSession（一次钓鱼长流程的宿主 Actor）
   ├─ UStateTreeComponent（ST_FishingSession）  ← 只拥有"阶段拓扑"
   ├─ UCatFishingFightRunner                    ← 拥有遛鱼数值（0.05s 固定步进）
   └─ FCatFishingSessionSnapshot（ReplicatedUsing）← 唯一复制出口，只读
        │
        └─ ACatFishEncounterActor
             └─ UStateTreeComponent（ST_FishFight）← 只选发力/平静意图；仅服务器运行
        ▼
【表现层】只订阅，永不写回（单向依赖）
   Snapshot/ViewBridge、表现 Actor 的 BP_On* 事件、Ability 的 BP_OnLocalInput* 钩子
```

**核心原则**：严格服务器权威。客户端只提交"意图"（带 RequestId 幂等 + ExpectedRevision 乐观锁），
所有事实由服务器重建——现在连瞄准点、竿 ID、装备版本都是服务器自己算的，客户端命令基本零载荷。

---

## 2. 关键子系统

### 2.0 鱼竿放置、共享与左右操作位

`PlaceRod` 现在只检查角色前方 150cm 是否存在可站立实体地面（地面法线 Z≥0.7），不再要求放置点位于水域样条外侧或距岸 4m 内。因此营地、远岸和测试区都可以先架杆。**架杆自由不等于抛线无限**：`BeginCast` 仍要求准星命中有效水域，并同时满足 `min(鱼竿最大线长, 浮漂最大抛距)`、前向夹角与无遮挡视线。

鱼竿公开状态以紧凑数组 `OperatorPlayerStates` 表示占位，服务器复制给所有客户端：

- 第一次 R 的 `PlaceRod` 只把鱼竿部署为空杆，不占槽、不锁角色移动；第二次 R 才进入操作位，保证放杆动画和使用动作是两个独立复制跃迁。
- 鱼竿只有一个公共 R 交互锚点；能否加入只看这个锚点与容器剩余容量，不会因为下一个人的编号改用另一套交互位置或射线。
- `OperatorPlayerStates` 是唯一紧凑容器：加入时追加到末尾并取得 `0、1、2...` 编号，任意成员离开后更高编号全部依次减一。
- `0` 号是当前主位；抛竿、提竿和右键线杯只由它驱动。HookedFight 中所有编号都可用左键提交即时发力意图，0 号离开后新的 0 号立即接管。
- 每次容器压紧后，服务器按新编号重排所有剩余角色；站位算法按右/左成对向外扩展，配置上限当前为 2、代码有界预留到 8，增加第三、第四人不需要新增专用槽位分支或交互锚点。
- HookedFight 固定步每次从该容器重建参与集合：主位提供移动/线杯意图，按住左键的辅助位提供协作力量和质量；统一做功后按有效力量占比分别从各自 ASC 支付体力。
- `OperatorPlayerState` 只保留为 `OperatorPlayerStates[0]` 的兼容快捷字段；蓝图若要判断双人必须读取数组长度。
- 活动会话唯一性属于鱼竿，不属于玩家：一根竿最多绑定一个未终态 `FishingSession`，同一玩家可以在多人部署的多根竿之间依次抛线。
- 按 R 离开只释放操作位，不写 `Escaped` 或 `Terminated`；`HookedFight` 会立刻进入无人值守松线，鱼按实际外游带线，到 `L_max` 后只按真实负载消耗本场鱼线耐久，不借用离开玩家的力量/体力。下一位玩家占据主位时，Session 与 Runner 会原子迁移到其 ASC、力量、体力和输入序号域；HookedFight 左键按本人所占鱼竿路由，其他主位命令与 HUD 按当前主操作鱼竿路由。
- 原始抛竿者的 Equipment 以 `FishingSessionId` 隔离多份鱼饵预留；一场结束只释放自己的预留，不会误释放其他鱼竿会话。

另一个容易混淆的身份是 `OwnerPlayerState`：它代表谁部署/谁能最终收走鱼竿，并不限制谁能占位。服务器按公开 `RodActorId` 找全场鱼竿；接管别人鱼竿后，抛竿也按“当前主操作位”查竿，不再误查“自己部署的竿”。

### 2.1 水域（样条烘焙 → 只读缓存）

- 作者态：`BP_CatWaterRegion` + 若干 `CatWaterBoundarySplineActor`（闭合样条，Include/Exclude）
- 编辑器里点 `BakeGeometry`：样条自适应采样 → 2D 多边形集 `FCatWaterGeometryCache` 存进 Actor
- 运行时：`UCatWaterQuerySubsystem` 纯读缓存回答一切空间问题——
  点在不在水里、到岸距离、射线∩水面、落点修正（`ResolveCandidatePointToWater`，
  岸上 `MaxLandingCorrectionCm` 内自动拉回水里）
- 所有查询要求 `FCatWaterRegionHandle`（RegionId+GeometryRevision）：几何重烘焙后旧 Handle 失效（StaleGeometry）
- ⚠️ 历史坑：蓝图 compile-on-load 曾把烘焙清掉（已修：空 Property 的编辑回调不再作废烘焙）

### 2.2 窝点（空间场）

- `UCatChumFieldSubsystem`（服务器）：投放建场（中心/半径/三轴腥香酵/时间衰减曲线）
- 公开态复制：`GameState → UCatChumFieldReplicationComponent → FCatChumFieldPublicItem[]`
- 咬钩加速：`ScheduleWaitingProbe` 在服务器冻结的落水点采样三轴总量 → `BiteRate ×= 1+(1-e^-Total)`；首次计时包含剩余飞行时间，避免鱼钩仍在空中就进入预警。
- 选鱼偏好：三轴采样 · 鱼的 ChumPreference 点积 → 饱和曲线 → 权重放大（最多 ×3）
- 上述三条是当前实现，不是新版目标；待改为水域面积/鱼量账本、平均分布、重叠区共享收敛曲线、守恒重分配与面积容量上限（见 `Docs/Architecture/项目技术方案.md` §7.1.1 和本文 §6）

### 2.3 会话阶段（谁在推进——最反直觉的部分）

| 阶段 | 写入者 |
|---|---|
| Waiting | StateTree 节点 `ScheduleWaitingProbe` **内部自己** EnterPhase，并按泊松抽咬钩延迟起计时器 |
| Probe | StateTree 的 `EnterPhase` 节点（ProbeTriggered 事件转移后） |
| TrueBiteWindow | StateTree 的 `OpenTrueBiteWindow` 节点打开通用响应窗；只让浮漂下沉，不选鱼、不生成 Actor、不扣饵 |
| HookedFight | 真咬窗内收到左键后，`RequestHook` 冻结选鱼上下文、选鱼、生成 Actor、扣饵并启动搏斗 |
| ExhaustedReel | 鱼体力归零或被猫端牵引越岸后发送 `FishExhausted` 事件；同一个 Runner 继续双端运动约束，但关闭鱼 AI 与猫端体力扣费 |
| Resolved/Terminated | `FinalizeSession()` —— StateTree **禁止**进入终态，且它会停树 |

浮漂正式表现由 `ACatFishingHookActor` 驱动，不依赖 `cat.Fishing.Debug`：Waiting 先保证至少 `MinimumBiteDelaySeconds`（当前 5 秒）的小幅慢浮，再叠加服务器随机安静等待；真咬前 `BiteWarningSeconds`（当前 1.5 秒）只把 Hook 的复制模式切为 `BiteWarning`，此时提前提竿仍是空钩；进入 `TrueBiteWindow` 时切为 `Sunk` 猛然下沉。若响应窗内没有左键，StateTree 走 `WindowExpired → Waiting`，保留鱼竿、鱼线和饵料预约并开始新一轮；每轮使用新的确定性服务器随机种子。`MaximumBiteDelaySeconds`（当前 15 秒）是每轮慢浮开始到下沉的总上限。网络只复制模式和服务器起始时间，各客户端本地计算连续位移，因此不会逐帧复制 Transform。

StateTree（`ST_FishingSession`）保持薄编排。其中 `FishExhausted` 是 `HookedFight → ExhaustedReelHold` 的显式事件边；`EarlyHook` / `Interrupted` 仍由 C++ 直接收敛终态并停树。

### 2.4 遛鱼（当前运动链与模块边界）

`FCatFishingFightSimulator::Step()`：纯静态无副作用函数（有单元测试），每 0.05s 由 Runner 调一次。

当前模型由鱼的游速候选、已放线长约束、力量差分配和玩法裁决组成，尚未完成鱼、线、杆、猫共同反力驱动的物理系统。鱼/猫质量与力量来源、收线公式、体力和断线条件只维护在 [实现导读](FishFightImplementationGuide_zh-CN.md)，避免在多个入口复制不同版本的公式。

主猫持竿时，PlayerController 在每帧视角旋转完成后把猫身水平朝向同步到 `ControlRotation.Yaw`，并临时关闭面向移动/ControllerDesiredRotation 两条覆盖通道。猫身和移动基准共用控制 Yaw，向后输入不会让猫掉头；离竿或换 Pawn 时恢复角色原有转向配置。是否持竿仍只读鱼竿 `HolderPlayerState`，Controller 不保存平行业务状态。

搏斗中的实际杆姿态独立于镜头意图，由 `FCatFishingRodResistanceModel::StepRotation` 按有阻尼的净转矩积分。猫朝请求视角施加不超过当前力量的转矩，接近目标时连续减小；鱼线施加 `cross(杆方向, 牵引方向) × 鱼力量 × LineLoad × Tension × 玩法杆长` 的有向回复转矩。净转矩抵消时自然停转；回看、改变鱼力、猫力或线方向后每帧重新求解，不存在硬角度锁或解锁状态，也不使用原来的全方向零速倍率。保持原有身体俯仰范围与最大角速度；响应时间控制阻尼，1/120 秒亚步降低帧率差异。服务器复制实际 Actor 姿态，Development 日志 `fishing_rod_rotation_resistance_sample` 记录请求/实际朝向、角速度、净转矩与仅用于观察的 `TorqueBalanced`；`fishing_constraint_sample` 记录最大鱼转矩。
`FCatFishSteeringModel` 用独立服务器随机流产生平滑目标游向；相同种子与固定步长得到相同方向序列，客户端不自行随机。

鱼自己的高层行为由 Encounter 上的 `ST_FishFight` 控制：默认在 `StrugglingOutward` 与 `CalmOrInward` 两个状态间循环。StateTree Task 只把意图和持续时间交给 Runner，不写 Transform、不扣体力，也不直接修改鱼线。未来增加“低体力蓄力冲刺”时，可以在树上增加状态和条件，同时仍复用同一套服务器模拟器。

Runner 将模拟器的候选结果交给水域/地面解析，再由 Encounter 应用并复制鱼的位置；Rod 消费猫端目标速度与杆转矩输入。Cable 只表现端点和余线，不向服务器提供约束反力。`bStalemate` 与 `TorqueBalanced` 只观察结果；`bStrongConfrontation` 仍参与过载断线，不能按只读表现标签处理。

鱼体力归零或确认被猫端牵引上岸后，Session 发布 `FishExhausted` 进入 `ExhaustedReel`；同一 Runner 保留运动约束，但停止鱼主动运动和猫端正向扣费。当前上岸清空体力和力竭后零猫消耗都是玩法特例，物理改造尚未替换这些分支。猫危险入水由 Condition 的脚点浸没查询确认。

全局搏斗系数来自 `DA_FishingFightBalance_Default`；鱼的游速、方向概率和阶段倍率来自当前鱼种性格，杆长、承载与本场鱼线耐久来自当前装备定义。`UCatFishingSettings` 保留资产软引用、固定步与持竿姿态等技术设置。具体字段和诊断过滤词见实现导读，不再从旧 `Fight|Spec` 设置页或测试鱼快照推断现行参数。

### 2.5 抄网（当前实现）

**范围判定 = 俯视投影下的「线段 ∩ 圆」**（`UCatFishingAimLibrary::DoesScoopRayReachFish`，服务器裁决与 debug 绘制调同一个函数）：

```
俯视（唯一判定平面）                侧视（唯一垂直约束）
猫 ●━━━━━━━━▶ 线段长 = 抄网射程      猫 ●
             ╭───╮                      │ ΔZ ≤ MaximumScoopVerticalDeltaCentimeters ?
             │🐟 │ 半径 = 鱼的可捞圈    ~~~🐟~~~ 水面
             ╰───╯
```

- **线段长度** = `min(UCatFishingSettings::ScoopReachCentimeters, 抄网 DA 的 ScoopReachCentimeters)`——全局那个是上限闸门，两个都得调才生效
- **线段方向** = 抄手 `Character` 的水平面朝方向（`Actor Forward`），不读取 `Controller/Camera` 朝向；自由转动镜头不会改变挥网方向
- **圆半径** = 鱼定义的 `ScoopTargetRadiusCentimeters`（圆心随鱼移动）。**为 0 时一律拒绝抢抄**（fail-closed）
- 射线**不带俯仰**：鱼在水下看不清，逼玩家瞄准深度会变成盲操作；而且现实里站高一点更好捞，3D 判定反而会让站得高的人够不着。高度只由 `MaximumScoopVerticalDeltaCentimeters` 单独卡上限
- 每次服务器接收的真实挥网尝试都会消费 `ScoopCooldownSeconds`（当前 3 秒）：GAS 的 `Cat.Cooldown.Fishing.Scoop` 提供本地预测与 UI 剩余时间，`UCatFishingCommandComponent` 的每玩家服务器闸门负责拒绝绕过 Ability 的重复 RPC；挥空同样消费，其他玩家的冷却互不影响

**开放阶段：`HookedFight` + `NearShore` + `ExhaustedReel`。** 鱼身上的圈**一直存在**，鱼的剩余体力完全不参与抄网判定——满体力鱼只要已经上钩并进入射线范围也能直接抄走。更早的阶段不开放：鱼还没被提上钩，抄它等于绕过提竿机制。

其余谓词：抄手在岸上（Outside 水域）+ 地面坡度 ≤ `MaximumScoopGroundSlopeDegrees` + 视线不被遮挡 + 装了 ScoopNet。
当前默认配置不再发放或自动选中 `StarterScoopNet`；临时默认抄网发放配置和 Character 启动分支已删除。`Equip_ScoopNet_Starter` 仍是正式目录定义，但在商店或奖励来源接入前，玩家暂时没有默认获取渠道。
**不再要求"鱼在近岸带内"**——射线∩圆已是唯一范围口径，再叠一层离岸距离会出现"debug 圈画成绿色但服务器拒绝"的表现/判定打架。`NearShoreWidthCentimeters` 仅用于外部 StateTree 请求进入 NearShore 时校验真实鱼位置，不参与抢抄距离或自动推进会话阶段。

首个合法 F 会生成一个 `ACatFishPickupActor`，并立即调用与岸上死鱼按 E 相同的嘴叼交接；此时鱼仍是世界 Actor，不进入背包或鱼护。玩家之后对具体地面鱼护按 E，才由 Items 执行唯一容器提交与图鉴归档。一次 F 用同一个 `RequestId` 串联 `scoop_target_selected`（或 `scoop_target_selection_failed`）、`scoop_rejected`、`fishing_scoop_terminal` 与最终 `fishing_command_result`。拒绝日志除逐项谓词和距离/高度/射程外，还同时保留角色中心、胶囊足底和地面命中点三组 WaterQuery 的错误枚举、Inside/Boundary/Outside、Region/几何版本、垂直差和带符号岸距；后两组只用于诊断，不改变当前以角色中心为准的权威规则。由此可以区分“角色中心高度超差”“脚下在水域内/边界”“没对准”“太远”“地面或视线不合法”。

鱼进入 `ExhaustedReel` 后还有第二条正式收尾路线：服务器立即复制 `AutoHauling`，各端据此让鱼侧翻；同一个约束继续负责收线/持竿者平移的拖动。力竭鱼的游向为零，到达竿尖正下方也属于合法状态，不再走活鱼的非零水平游向校验。未确认真实干地前保持水面高度；若烘焙水域轮廓已结束但地表射线仍命中水面，真实拖拽继续保留候选 XY 并逐步重查，不弹回水域内缩点。只有命中高于水面的真实表面才锁定 `Beached`，此后每个固定步按当前 XY 调用 `FCatWorldSurfaceResolver` 更新权威地面高度。干地鱼进入竿尖的水平 `LandingCompletionDistanceToRodCentimeters` 后，原地生成复制的 `ACatFishPickupActor`；不使用握把距离，交接帧松开左键也不会阻止生成。所有玩家都能以准星锁定并按 E 请求拾取，服务器复核距离、视线和物品状态，首个合法请求获胜。抄网与岸上拾取从这里开始共用同一条“嘴叼世界鱼 → 对具体鱼护 E → Items 唯一提交”链；Session Outcome 分别为 `Caught` 与 `Landed`。关键日志为 `fishing_fish_exhausted`、`fishing_beaching_deferred Result=ContinueSurfaceTow`、`fishing_fish_beached` 和 `exhausted_fish_pickup_spawned LandingTarget=RodTip`。

---

## 3. 表现层契约（五条稳定接缝）

表现只订阅，永不写回。逻辑公式怎么改，只要字段**语义**不变，表现层零改动（遛鱼公式整个重写已验证）。

| 接缝 | 内容 | 用途 |
|---|---|---|
| Ability 钩子 | `BP_OnLocalInputActivated / Released`（本地端、提交前、不带结果） | 挥网/甩杆/提竿抬手等"成败都播"的即时动作 |
| Snapshot/ViewBridge | Phase / bReeling / bSlacking / bPerfectHook / NormalizedFishStamina / FishMotionIntent / Outcome | AnimBP 状态机、HUD、结果演出 |
| 表现 Actor 事件 | Rod/Hook/Fish 的 `BP_On*PresentationChanged` + `BP_Play*Event` | 阶段外观与附加音画；鱼基础 Mesh/AnimBP 由鱼种库直连，不在事件内按 ID 重选 |
| 窝点公开态 | 中心/半径/过期时间（`GameState.ChumFieldReplication`） | 窝点光环表现（BP 类经 `ChumFieldPresentationClass` 配置） |
| AimLibrary | `ResolveCastAimPoint / PredictChumThrow / ChargeAlphaFromHeldSeconds` | 预览与服务器**同一份数学**，所见即所得 |

反向纪律（唯一红线）：表现事件里不发命令；Montage 完成 / AnimNotify 不作为任何玩法提交条件。

鱼的体重、力量与视觉大小使用同一条服务器事实链：服务器先为每个候选鱼种按稳定随机流抽取个体 `WeightKilograms`，以 `Weight × StrengthPerKilogram` 计算挑战度和本场基础力量；选中后不再重抽。完美中鱼只在该基础力量上乘性格倍率。视觉再按
`Scale = clamp(cuberoot(Weight / ReferenceWeight), MinScale, MaxScale)` 计算一次 `VisualScale`。水中
`FishEncounterActor` 与水面 `FishPickupActor` 都复制这个标量，并只缩放各自的 `FishMesh`；Actor 根节点、
抄网圆、拾取 Sphere、鱼线与岸线判定不随 Mesh 大小变化。这样多人尺寸一致，收鱼交接也不会产生大小跳变。

---

## 4. 调试可视化

`UCatFishingDebugSubsystem` 的世界调试标记由 CVar `cat.Fishing.Debug` 控制（默认 0，需要调试时执行 `cat.Fishing.Debug 1`）：
青色湖边界 / 抛竿瞄准绿球 / 窝点绿圈+剩余秒 /
钩子蓝球 / 鱼球（红=发力·绿=累了）+ 鱼线 / 近岸翡翠圈 / 屏幕阶段提示（线长·拖放·完美）。

右上角三方数值面板使用独立 CVar `cat.Fishing.Stats`，默认 0：第一行显示当前复制快照的稳定 `FishDefinitionId`（无鱼时为 `--`），鱼数值行显示当前/上限体力与有效力量（含完美中鱼折减），竿显示当前本场鱼线或装备耐久、上限与钓组力量，猫显示 ASC 当前/上限搏斗体力与钓鱼力量。执行 `cat.Fishing.Stats 1` 可在排查时单独开启；它不会修改 `cat.Fishing.Debug`，后者保持默认关闭，开启世界调试也不会改变数值面板开关。

Q 蓄力黄色抛物线与落点球是玩法瞄准反馈，不属于上述两类调试信息；它继续由 `cat.Fishing.ChumPreview` 独立控制并默认开启。
命令链每条回执有结构化日志：过滤 `LogCatFishing`，失败为 Warning 且带 Error 枚举。命令/抛竿/打窝回执统一附带 Controller、PlayerState、脱敏 StableNetId、`IsLocalController`、NetMode、Pawn 权威位置/Role 与控制朝向；会话终态附带鱼、竿尖、钩、Encounter 和操作者上下文。高频物理诊断按状态变化或每秒限频输出，异常单步和终局边沿不被限频吞掉；不逐帧刷屏。原始 StableNetId 只有 `StableNetIdExposure=Enabled` 时才允许出现。

## 5. 关键资产与配置

```
Content/Data/Abilities/   DA_CatAbilitySet_Default(6条) · DA_CatAbilityInputConfig(6条)
Content/Data/Equipment/   DA_Rod/Bait/Float/ScoopNet/Chum_Basic
Content/Catfishing/Data/Fish/  正式 Fish_* · Bite_* · Fight_* · Presentation/FishPresentation_*
Content/Catfishing/Fishing/Animation/Fish/  无骨骼 ABPT_CatFishBase · 每鱼 ABP_Fish_*
Content/Data/Fish/             未注册的历史测试二进制（不得作为运行入口，待编辑器引用审计后清理）
Content/Data/Curves/      Curve_ChumSaturation(1→3) · Curve_ChumDistance/TimeFalloff(1→0)
Content/Data/StateTrees/  ST_RunFlow · ST_FishingSession · ST_FishFight
Config/DefaultGame.ini    10 个 section（改后必须重启 Editor；软引用资产必须真实落盘）
```

鱼表现的唯一入口是 `Fish_*::PresentationDefinition`。每个 `FishPresentation_*` 保存 Mesh、子 AnimBP、四类动画、
参考重量、最小/最大缩放和三种局部 Transform；运行时没有按鱼名猜 Mesh/比例的平行配置。所有子 ABP 继承同一个
无 Target Skeleton 的 `ABPT_CatFishBase`，播放速率与状态机只维护一次。

数值快照：猫力50 体力100 ／ 竿强60 耐久70 线长1500 ／ 鱼力40 体力50 ／ 真咬窗3s 完美窗1s ／ 近岸100cm ／ 鱼竿操作位2个、左右间距140cm。
开发便利开关：整套 `bAutoConfigureStarterLoadout=False`；临时默认抄网发放已经删除，默认进游戏不会创建或选中抄网。

## 6. 已知待办（都在契约后面，不影响表现层）

- 浮漂弹道解（飞行轨迹落不到目标点，靠落水轮询吸附，视觉有"瞬移"）
- 咬钩公式改版：读取所在面积单元的聚鱼总量、浮漂级计时器、总量变化时比例折算；正式总量→等待时长曲线待裁
- 窝料改版：水域面积/鱼总量/鱼种库存账本、鱼种平均分布、互斥面积单元、共享重叠收敛曲线、守恒重分配与面积容量上限
- 抄网规格版：概率/硬直/无网拾取/翻肚 30s 苏醒（会新增 Phase/Intent 枚举值→表现层届时"补分支"）
- 浮漂精准偏移、入夜停咬、拽尾巴救援(W3)、巨鱼协作表现输入
- 多人实时力量与独立体力已接入常规 FightRunner；仍待接的是低体力换人广播/超时、虚脱双倍恢复与 50% 再入门槛，以及正式多人力量/体力 HUD。巨鱼旧 StateTree 交换仍保留兼容参与摘要，不得与常规固定步重复扣体力。
