# 钓鱼核心架构（技术文档）

版本对应：2026-08-25 代码状态。阅读对象：需要理解/修改钓鱼玩法逻辑的人。
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
- `0` 号槽是右侧主位；当前单人钓鱼命令只接受该玩家。
- `1` 号槽是左侧辅助位；当前只完成站位和网络占用，合力数值/输入仍是 `TODO(CooperativeFishing)`。
- 主位离开时数组压紧，原 1 号位晋升为 0 号位并移动到右侧；人数从数组长度即时推导，没有可能遗留的 `bTwoPlayer`。
- 槽位算法按右/左成对向外扩展，配置上限当前为 2、代码有界预留到 8，后续增加人数不需要更换复制结构。
- `OperatorPlayerState` 只保留为 `OperatorPlayerStates[0]` 的兼容快捷字段；蓝图若要判断双人必须读取数组长度。

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
- 咬钩加速：`ScheduleWaitingProbe` 在钩子位置采样三轴总量 → `BiteRate ×= 1+(1-e^-Total)`
- 选鱼偏好：三轴采样 · 鱼的 ChumPreference 点积 → 饱和曲线 → 权重放大（最多 ×3）
- 待改为规格版：圆形并池 / 30s×0.9 离散衰减 / T_base/(1+Total/K)（见 §6 待办）

### 2.3 会话阶段（谁在推进——最反直觉的部分）

| 阶段 | 写入者 |
|---|---|
| Waiting | StateTree 节点 `ScheduleWaitingProbe` **内部自己** EnterPhase，并按泊松抽咬钩延迟起计时器 |
| Probe | StateTree 的 `EnterPhase` 节点（ProbeTriggered 事件转移后） |
| TrueBiteWindow | StateTree 的 `OpenTrueBiteWindow` 节点打开通用响应窗；只让浮漂下沉，不选鱼、不生成 Actor、不扣饵 |
| HookedFight | 真咬窗内收到左键后，`RequestHook` 冻结选鱼上下文、选鱼、生成 Actor、扣饵并启动搏斗 |
| ExhaustedReel | 鱼体力耗尽或猫形成绝对力量优势后，由 Session 停止搏斗 Runner 并进入 |
| Resolved/Terminated | `FinalizeSession()` —— StateTree **禁止**进入终态，且它会停树 |

浮漂正式表现由 `ACatFishingHookActor` 驱动，不依赖 `cat.Fishing.Debug`：Waiting 先保证至少 `MinimumBiteDelaySeconds`（当前 5 秒）的小幅慢浮，再叠加服务器随机安静等待；真咬前 `BiteWarningSeconds`（当前 1.5 秒）只把 Hook 的复制模式切为 `BiteWarning`，此时提前提竿仍是空钩；进入 `TrueBiteWindow` 时切为 `Sunk` 猛然下沉。若响应窗内没有左键，StateTree 走 `WindowExpired → Waiting`，保留鱼竿、鱼线和饵料预约并开始新一轮；每轮使用新的确定性服务器随机种子。`MaximumBiteDelaySeconds`（当前 15 秒）是每轮慢浮开始到下沉的总上限。网络只复制模式和服务器起始时间，各客户端本地计算连续位移，因此不会逐帧复制 Transform。

StateTree（`ST_FishingSession`）只有 4 个状态、3 条事件转移 + 1 条完成转移，逻辑极薄。其中 `WindowExpired` 是 `Probe → Waiting` 的循环边；`EarlyHook` / `Interrupted` 仍由 C++ 直接收敛终态并停树。

### 2.4 遛鱼（规格 4.3/4.4 判定表）

`FCatFishingFightSimulator::Step()`：纯静态无副作用函数（有单元测试），每 0.05s 由 Runner 调一次。

三方力量（冻结进 Config）：猫力=ASC FishingStrength ／ 鱼力=鱼种 FishStrength（完美中鱼×0.8）／ 竿强=RodDefinition.FishingStrength。
`FCatFishSteeringModel` 用独立服务器随机流产生平滑目标游向；相同种子与固定步长得到相同方向序列，客户端不自行随机。

鱼自己的高层行为由 Encounter 上的 `ST_FishFight` 控制：默认在 `StrugglingOutward` 与 `CalmOrInward` 两个状态间循环。StateTree Task 只把意图和持续时间交给 Runner，不写 Transform、不扣体力，也不直接修改鱼线。未来增加“低体力蓄力冲刺”时，可以在树上增加状态和条件，同时仍复用同一套服务器模拟器。

```
LineOutward = normalize(鱼位置 - 竿尖位置)（水平面）
Alignment   = dot(鱼当前游向, LineOutward)，范围[-1,1]
LineLoad    = pow(max(Alignment, 0), AngleStrengthExponent)，范围[0,1]

鱼仍在平静 ⇄ 挣扎之间定时交替，但每段内部可平滑转向、横切、绕竿和假动作；上钩瞬间=挣扎。
每次重选方向先根据鱼体力计算向内概率；朝竿尖 ±60° 属于向内。当前测试鱼满体力为 25%，接近力竭为 80%，中间按指数 1.1 的性格曲线插值；发力期仍只把其中 `FeintProbability` 比例当作向内假动作。
LineLoad 控制鱼线磨损、牵引效率和强对抗资格：正对外冲满载，斜向按夹角衰减，横向/向内不制造正面鱼线力量。带载左键的猫/鱼体力消耗不再乘 LineLoad；平静期使用 InwardPull 系数×BaseDrainMultiplier，挣扎期使用 Stalemate 系数×StruggleDrainMultiplier，确保拖线始终双方做功且挣扎档更高。只有鱼自己绷紧锁线、玩家没有主动拉时，双方消耗才按 LineLoad 缩放。
鱼先按自己的水平游向/游速自由移动，锁线只截住越线部分；左键按 (1-LineLoad) 得到有限牵引位移，并按实际到达的鱼距结算 L_paid，不再用缩短后的三维球面重建位置。

挣扎 + 拖(或线放尽被绷紧) + LineLoad 连续达到性格阈值/确认时间：进入强对抗并按序裁决
   ① 钓组承载 ≤ min(猫力,鱼力) → 鱼线瞬断（LineBroken，鱼逃；鱼竿保持可用）
   ② 鱼力 ≥ 猫力            → 猫被拖下水（CatInWater，鱼逃）
   ③ 猫力 ≥ 鱼力×2          → 绝对碾压（结束搏斗→ExhaustedReel）
   ④ 其余 = 僵持消耗战：竿-=鱼力×0.1×LineLoad · 带载拖时鱼-=猫力×0.08×StruggleDrainMultiplier · 猫-=鱼力×0.12×StruggleDrainMultiplier /s
向外游 + 松线(右键)：在 L_max 内不限制鱼，L_paid 只随鱼实际外游被动增长；猫体力 +1.5/s（封顶）

L_paid = 已放出的线长（左键主动收短；右键只允许鱼外游时被动带线）
D      = 竿尖到鱼的直线距离
Slack  = max(L_paid-D, 0)：有余线时 Cable 本地垂坠
Tension= 鱼试图超过线端的距离：无输入向外冲也会绷线并消耗资源

归零优先级：鱼体力(翻肚→ExhaustedReel) → 猫体力(拖下水) → 本场鱼线耐久(断线)
完美中鱼：真咬后 1s 内提竿（服务器时间戳）→ 鱼力/鱼体力/初始线长按性格模板折减，bPerfectHook 复制
```

全局消耗系数在 `Project Settings → Catfishing Fishing → Fight|Spec`；转向、随机倾向、夹角曲线和强对抗阈值在鱼的 `DA_Fight_*` 性格资产。

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
- **圆半径** = 鱼定义的 `ScoopTargetRadiusCentimeters`（圆心随鱼移动）。**为 0 时一律拒绝抢抄**（fail-closed）
- 射线**不带俯仰**：鱼在水下看不清，逼玩家瞄准深度会变成盲操作；而且现实里站高一点更好捞，3D 判定反而会让站得高的人够不着。高度只由 `MaximumScoopVerticalDeltaCentimeters` 单独卡上限
- 每次服务器接收的真实挥网尝试都会消费 `ScoopCooldownSeconds`（当前 3 秒）：GAS 的 `Cat.Cooldown.Fishing.Scoop` 提供本地预测与 UI 剩余时间，`UCatFishingCommandComponent` 的每玩家服务器闸门负责拒绝绕过 Ability 的重复 RPC；挥空同样消费，其他玩家的冷却互不影响

**开放阶段：`HookedFight` + `NearShore` + `ExhaustedReel`。** 鱼身上的圈**一直存在**，鱼的剩余体力完全不参与抄网判定——满体力鱼只要已经上钩并进入射线范围也能直接抄走。更早的阶段不开放：鱼还没被提上钩，抄它等于绕过提竿机制。

其余谓词：抄手在岸上（Outside 水域）+ 地面坡度 ≤ `MaximumScoopGroundSlopeDegrees` + 视线不被遮挡 + 装了 ScoopNet。
当前开发配置通过 `bAutoGrantStarterScoopNet=True` 在服务器首次占有时给每名玩家本人库存发一份并自动选中正式目录定义 `StarterScoopNet`（`Equip_ScoopNet_Starter`）；这是临时可玩性开关，不是正式获取规则。商店或奖励来源接入后关闭它。
**不再要求"鱼在近岸带内"**——射线∩圆已是唯一范围口径，再叠一层离岸距离会出现"debug 圈画成绿色但服务器拒绝"的表现/判定打架。旧 `NearShoreWidthCentimeters` 只作为配置兼容字段保留，不再推进会话阶段。

首个合法 F 会生成一个 `ACatFishPickupActor`，并立即调用与岸上死鱼按 E 相同的嘴叼交接；此时鱼仍是世界 Actor，不进入背包或鱼护。玩家之后对具体地面鱼护按 E，才由 Items 执行唯一容器提交与图鉴归档。拒绝时打 `Event=scoop_rejected`，逐项列出谓词并附带实测水平距离/高度差/射程/半径，能直接分辨是"没对准"、"太远"还是"站太高"。

鱼体力耗尽后还有第二条正式收尾路线：服务器立即复制 `AutoHauling`，各端据此让鱼侧翻；继续按住左键时，服务器在 `ExhaustedReel` 中把鱼逐步收向“竿尖 XY + max(水面 Z, 竿尖下方地面 Z)”的冻结投影，最多到该点。目标 XY 不受 WaterRegion 轮廓限制；岸地高于水面时使用地面高度，避免鱼埋进岸坡。到点后原地生成复制的 `ACatFishPickupActor`。所有玩家都能以准星锁定并按 E 请求拾取，服务器复核距离、视线和物品状态，首个合法请求获胜。抄网与岸上拾取从这里开始共用同一条“嘴叼世界鱼 → 对具体鱼护 E → Items 唯一提交”链；Session Outcome 分别为 `Caught` 与 `Landed`。

---

## 3. 表现层契约（五条稳定接缝）

表现只订阅，永不写回。逻辑公式怎么改，只要字段**语义**不变，表现层零改动（遛鱼公式整个重写已验证）。

| 接缝 | 内容 | 用途 |
|---|---|---|
| Ability 钩子 | `BP_OnLocalInputActivated / Released`（本地端、提交前、不带结果） | 挥网/甩杆/提竿抬手等"成败都播"的即时动作 |
| Snapshot/ViewBridge | Phase / bReeling / bSlacking / bPerfectHook / NormalizedFishStamina / FishMotionIntent / Outcome | AnimBP 状态机、HUD、结果演出 |
| 表现 Actor 事件 | Rod/Hook/Fish 的 `BP_On*PresentationChanged` + `BP_Play*Event` | Mesh/皮肤/阶段外观切换 |
| 窝点公开态 | 中心/半径/过期时间（`GameState.ChumFieldReplication`） | 窝点光环表现（BP 类经 `ChumFieldPresentationClass` 配置） |
| AimLibrary | `ResolveCastAimPoint / PredictChumThrow / ChargeAlphaFromHeldSeconds` | 预览与服务器**同一份数学**，所见即所得 |

反向纪律（唯一红线）：表现事件里不发命令；Montage 完成 / AnimNotify 不作为任何玩法提交条件。

鱼的体重与视觉大小使用同一条服务器事实链：鱼种选出后，服务器在定义的重量区间内冻结 `WeightKilograms`，再按
`Scale = clamp(cuberoot(Weight / ReferenceWeight), MinScale, MaxScale)` 计算一次 `VisualScale`。水中
`FishEncounterActor` 与水面 `FishPickupActor` 都复制这个标量，并分别只缩放 `VisualRoot` / `FishMesh`；Actor 根节点、
抄网圆、拾取 Sphere、鱼线与岸线判定不随 Mesh 大小变化。这样多人尺寸一致，收鱼交接也不会产生大小跳变。

---

## 4. 调试可视化

`UCatFishingDebugSubsystem` 的世界调试标记由 CVar `cat.Fishing.Debug` 控制（默认 0，需要调试时执行 `cat.Fishing.Debug 1`）：
青色湖边界 / 抛竿瞄准绿球 / 窝点绿圈+剩余秒 /
钩子蓝球 / 鱼球（红=发力·绿=累了）+ 鱼线 / 近岸翡翠圈 / 屏幕阶段提示（线长·拖放·完美）。

右上角三方数值面板使用独立 CVar `cat.Fishing.Stats`，默认 1：鱼显示当前/上限体力与有效力量（含完美中鱼折减），竿显示当前本场鱼线或装备耐久、上限与钓组力量，猫显示 ASC 当前/上限搏斗体力与钓鱼力量。执行 `cat.Fishing.Stats 0` 可单独关闭；它不会修改 `cat.Fishing.Debug`，后者保持默认关闭，开启世界调试也不会改变数值面板开关。

Q 蓄力黄色抛物线与落点球是玩法瞄准反馈，不属于上述两类调试信息；它继续由 `cat.Fishing.ChumPreview` 独立控制并默认开启。
命令链每条回执有结构化日志：过滤 `LogCatFishing`，失败为 Warning 且带 Error 枚举。

## 5. 关键资产与配置

```
Content/Data/Abilities/   DA_CatAbilitySet_Default(6条) · DA_CatAbilityInputConfig(6条)
Content/Data/Equipment/   DA_Rod/Bait/Float/ScoopNet/Chum_Basic
Content/Data/Fish/        DA_Fish_Test01 · DA_Bite_Test01 · DA_Fight_Test01
Content/Data/Curves/      Curve_ChumSaturation(1→3) · Curve_ChumDistance/TimeFalloff(1→0)
Content/Data/StateTrees/  ST_RunFlow · ST_FishingSession · ST_FishFight
Config/DefaultGame.ini    10 个 section（改后必须重启 Editor；软引用资产必须真实落盘）
```

当前共用鱼 Mesh 的体重缩放在 `Catfishing Fishing Presentation > FishScale` 配置：参考重量 `1kg`、最小缩放
`0.75`、最大缩放 `1.75`。以后若每个鱼种拥有独立 Mesh，应把这三个参数迁入对应鱼种表现 DA，而不是让客户端
按鱼名猜比例。

数值快照：猫力50 体力100 ／ 竿强60 耐久70 线长1500 ／ 鱼力40 体力50 ／ 真咬窗3s 完美窗1s ／ 近岸100cm ／ 鱼竿操作位2个、左右间距140cm。
开发便利开关：整套 `bAutoConfigureStarterLoadout=False`；仅 `bAutoGrantStarterScoopNet=True` 临时默认发一份抄网，正式商店/奖励获取接入后关闭。

## 6. 已知待办（都在契约后面，不影响表现层）

- 浮漂弹道解（飞行轨迹落不到目标点，靠落水轮询吸附，视觉有"瞬移"）
- 咬钩公式规格版：T_base/(1+Total/K)、浮漂级计时器、比例折算
- 窝料规格版：圆形并池、30s×0.9 离散衰减、去设计上限
- 抄网规格版：概率/硬直/无网拾取/翻肚 30s 苏醒（会新增 Phase/Intent 枚举值→表现层届时"补分支"）
- 浮漂精准偏移、入夜停咬、拽尾巴救援(W3)、巨鱼协作输入
- `TODO(CooperativeFishing)`：把鱼竿当前占位数组接入 Session 参与集合、合力/体力结算与多输入仲裁；必须每次从当前数组重建，不能缓存单/双人模式
