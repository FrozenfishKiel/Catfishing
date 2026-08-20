# 钓鱼核心架构（技术文档）

版本对应：2026-08-19 代码状态。阅读对象：需要理解/修改钓鱼玩法逻辑的人。
配套文档：蓝图任务步骤见《BlueprintTaskGuide_zh-CN.md》；规格口径见《FishingCoreFlow_zh-CN.md》。

---

## 1. 一张图看全貌

```
【输入层】玩家按键
   E / 左键 / 右键 / Q / F / X
        │  Enhanced Input（IMC_InputContext）
        ▼
【GAS 层】6 个原生 GameplayAbility（可被蓝图子类化，仅承载"输入边沿 + 本地表现钩子"）
   UCatGA_FishingRodInteract / PrimaryAction / Slack / Chum / Scoop / Cancel
        │  Submit*()：把按下/松开变成一条带 RequestId 的命令
        ▼
【命令层】UCatFishingCommandComponent（挂在 PlayerController 上，唯一 RPC 边界）
   HandleAbilityCommandFromAuthority()：服务器按"当前事实"分派语义
   ├─ E   → 没竿=PlaceRod / 没人操作=OperateRod / 自己在操作=LeaveRod
   ├─ 左键→ 无会话按下=记录瞄准 / 无会话松开=BeginCast（视线∩水面）
   │        有会话按下=提竿(TrueBite) 或 拖(HookedFight) / 松开=停拖
   ├─ 右键→ HookedFight 中 按住=放线 / 松开=停放
   ├─ Q   → 按下记时刻 / 松开=按时长算蓄力 → 弹道预测落点 → PlaceChum
   ├─ F   → RequestScoop（服务器自动填抄手自己的鱼护 ID）
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
        ▼
【表现层】只订阅，永不写回（单向依赖）
   Snapshot/ViewBridge、表现 Actor 的 BP_On* 事件、Ability 的 BP_OnLocalInput* 钩子
```

**核心原则**：严格服务器权威。客户端只提交"意图"（带 RequestId 幂等 + ExpectedRevision 乐观锁），
所有事实由服务器重建——现在连瞄准点、竿 ID、装备版本都是服务器自己算的，客户端命令基本零载荷。

---

## 2. 关键子系统

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
| TrueBiteWindow | `ResolveTrueBiteSelection` 节点内部**直接写**（选鱼+生成鱼 Actor+扣饵在此一次完成） |
| HookedFight | `RequestHook` 命令内部（提竿成功） |
| NearShore | 遛鱼 Runner 结束后 C++ 内部 |
| Resolved/Terminated | `FinalizeSession()` —— StateTree **禁止**进入终态，且它会停树 |

StateTree（`ST_FishingSession`）只有 4 个状态、2 条事件转移 + 1 条完成转移，逻辑极薄。
C++ 实际只发 5 个事件：ProbeTriggered / HookAccepted / WindowExpired / EarlyHook / Interrupted（后三个发完即停树，树收不到）。

### 2.4 遛鱼（规格 4.3/4.4 判定表）

`FCatFishingFightSimulator::Step()`：纯静态无副作用函数（有单元测试），每 0.05s 由 Runner 调一次。

三方力量（冻结进 Config）：猫力=ASC FishingStrength ／ 鱼力=鱼种 FishStrength（完美中鱼×0.8）／ 竿强=RodDefinition.FishingStrength

```
鱼 向内游(顺从) ⇄ 向外游(挣扎)，定时交替；上钩瞬间=向外游；鱼体力<50% 休息期×1.5

向内游 + 拖(左键)：D 大幅拉近；猫 -= 鱼力×0.15/s；鱼 -= 猫力×0.08/s   ← 拖永远双方消耗
向内游 + 不按　　：D 缓慢拉近；无消耗
向外游 + 拖(或线放尽被绷紧)：按序判定，先命中先生效，取等从严
   ① 竿强 ≤ min(猫力,鱼力)  → 瞬断（断竿，鱼逃）
   ② 鱼力 ≥ 猫力            → 猫被拖下水（CatInWater，鱼逃）
   ③ 猫力 ≥ 鱼力×2          → 绝对碾压（D 归零甩上岸→NearShore）
   ④ 其余 = 僵持消耗战：竿-=鱼力×0.1 · 鱼-=猫力×0.08 · 猫-=鱼力×0.12 /s
向外游 + 放线(右键)：D 变远；猫体力 +1.5/s（封顶）——喘气窗口

归零优先级：鱼体力(翻肚→贴岸→NearShore) → 猫体力(拖下水) → 竿耐久(断竿)
完美中鱼：真咬后 1s 内提竿（服务器时间戳）→ 鱼力/鱼体力/初始线长按性格模板折减，bPerfectHook 复制
```

所有系数在 `Project Settings → Catfishing Fishing → Fight|Spec`，默认即规格快照。

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

**开放阶段：`HookedFight` + `NearShore`。** 鱼身上的圈**一直存在**，不是"体力清零才能抄"——搏斗中把鱼收到射线够得着的位置就能直接抄上来（提前结束搏斗的主动选择，也给多人抢抄拉长窗口）。更早的阶段不开放：鱼还没被提上钩，抄它等于绕过提竿机制。

其余谓词：抄手在岸上（Outside 水域）+ 地面坡度 ≤ `MaximumScoopGroundSlopeDegrees` + 视线不被遮挡 + 装了 ScoopNet。
**不再要求"鱼在近岸带内"**——射线∩圆已是唯一范围口径，再叠一层离岸距离会出现"debug 圈画成绿色但服务器拒绝"的表现/判定打架。`NearShoreWidthCentimeters` 现在只管"何时进入 NearShore 阶段"。

首个合法 F 走 Items 唯一捕获事务（鱼归抄手，图鉴首钓归钓手的两轨制在 Items 侧）。拒绝时打 `Event=scoop_rejected`，逐项列出谓词并附带实测水平距离/高度差/射程/半径，能直接分辨是"没对准"、"太远"还是"站太高"。

待改为规格版：道具化概率 / 3s 硬直 / 无网翻肚拖上岸拾取 / 30s 苏醒（§6）。

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

---

## 4. 调试可视化

`UCatFishingDebugSubsystem`，CVar `cat.Fishing.Debug`（默认 1）：
青色湖边界 / 抛竿瞄准绿球 / Q 蓄力黄色抛物线+落点球 / 窝点绿圈+剩余秒 /
钩子蓝球 / 鱼球（红=发力·绿=累了）+ 鱼线 / 近岸翡翠圈 / 屏幕状态条（阶段提示+猫体力·鱼体力%·线长·拖放·完美）。
命令链每条回执有结构化日志：过滤 `LogCatFishing`，失败为 Warning 且带 Error 枚举。

## 5. 关键资产与配置

```
Content/Data/Abilities/   DA_CatAbilitySet_Default(6条) · DA_CatAbilityInputConfig(6条)
Content/Data/Equipment/   DA_Rod/Bait/Float/ScoopNet/Chum_Basic
Content/Data/Fish/        DA_Fish_Test01 · DA_Bite_Test01 · DA_Fight_Test01
Content/Data/Curves/      Curve_ChumSaturation(1→3) · Curve_ChumDistance/TimeFalloff(1→0)
Content/Data/StateTrees/  ST_RunFlow · ST_FishingSession
Config/DefaultGame.ini    10 个 section（改后必须重启 Editor；软引用资产必须真实落盘）
```

数值快照：猫力50 体力100 ／ 竿强60 耐久70 线长1500 ／ 鱼力40 体力50 ／ 真咬窗3s 完美窗1s ／ 近岸100cm ／ 放竿岸带400cm。
开发便利开关：`bAutoConfigureStarterLoadout`（自动装配+发5窝料，正式选装 UI 完成后关闭）。

## 6. 已知待办（都在契约后面，不影响表现层）

- 浮漂弹道解（飞行轨迹落不到目标点，靠落水轮询吸附，视觉有"瞬移"）
- 咬钩公式规格版：T_base/(1+Total/K)、浮漂级计时器、比例折算
- 窝料规格版：圆形并池、30s×0.9 离散衰减、去设计上限
- 抄网规格版：概率/硬直/无网拾取/翻肚 30s 苏醒（会新增 Phase/Intent 枚举值→表现层届时"补分支"）
- 浮漂精准偏移、入夜停咬、拽尾巴救援(W3)、巨鱼协作输入
