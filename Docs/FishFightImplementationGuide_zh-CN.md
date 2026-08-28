# 鱼运动与遛鱼逻辑实现导读

这份文档只解释“鱼上钩以后为什么会游、力量怎么算、体力归零以后怎样收鱼”。源码内可以搜索 `[FishLogic`，按 1～5 顺序阅读。

## 一条完整调用链

```text
玩家左/右键
  → Fishing Ability / CommandComponent：把输入请求送到服务器
  → ACatFishingSession：验证当前会话和阶段
  → UCatFishingFightRunner：服务器每 0.05 秒推进一次
      1. ST_FishFight：决定当前是发力还是平静，并把 MotionIntent 交给 Runner
      2. FCatFishSteeringModel：结合鱼体力决定向内/向外，再平滑转弯
      3. FCatFishingFightSimulator：计算鱼线松弛/张力、夹角、位移、体力和竿磨损
      4. ACatFishEncounterActor：应用服务器位置并复制表现状态
  → 鱼体力耗尽
      5. ACatFishingSession::HandleExhaustedReelStep：侧翻并收至竿尖表面投影，再生成 Pickup
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

每隔 `DirectionRetargetDurationRangeSeconds` 才抽一次目标方向。目标方向由四部分混合：

```text
目标方向 = 状态锚方向 × 方向偏置
         + 随机方向 × 剩余随机量
         + 左右切线 × 横向偏置
         + 小概率假动作
```

抽到目标后不会立刻转过去，而是每秒最多转 `MaximumTurnRateDegreesPerSecond`。这叫“相关随机”：相邻帧彼此相关，所以轨迹生动但连续；逐帧重新随机则是白噪声，会表现成原地抖动。

本项目把“向内”定义为：目标方向落在“鱼 → 竿尖”方向左右各 `InwardConeHalfAngleDegrees` 的扇区内，默认是 ±60°。每次需要重新选择目标时，先按鱼当前体力算向内概率：

```text
疲劳度 = 1 - 当前鱼体力 / 初始鱼体力
向内概率 = Lerp(满体力向内概率, 力竭向内概率, 疲劳度 ^ 曲线指数)
```

当前测试鱼满体力有 25% 的平静方向会朝内，接近力竭时提高到 80%，指数为 1.1；发力游速从 150cm/s 降到 120cm/s，同时降低正面外冲偏置并提高横向偏置。这样高体力鱼仍会主动拉开战线，但会更常横切和短暂回头，不要求玩家长期一直松开线杯。发力状态仍以外冲为主，只把 `FeintProbability` 比例的选择用于向内假动作。

活鱼不能直接游上陆地。水域查询发现候选位置越过岸线时，会给出岸线内侧安全点；活鱼运动会把这次修正拆成“向水内法向 + 沿岸切向”，丢掉可能较大的法向安全距离跳变，只保留不超过本步速度的沿岸滑动。同时 Runner 调用 `RedirectFromWaterBoundary()`，把当前方向沿岸线法线反射回水里，并至少维持一个换向周期。这样碰岸当帧可以沿岸移动，后续再平滑游离，不会产生突兀的回弹。

玩家按住左键仍然可以把活鱼收到岸边，但活鱼不会因此直接穿过岸线成为物品：此时可以抄网；或者继续对抗直至鱼体力耗尽，再由 `ExhaustedReel` 收至竿尖的表面投影点。这个投影不做 WaterRegion 内外校验；Z 在力竭瞬间取水面与竿尖下方地面中较高者。

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

- 同方向向外游：夹角 0°，`LineLoad=1`，鱼力全部压在线上。
- 斜向游：例如 60°，指数为 1 时 `LineLoad=0.5`。
- 横向游：90°，`LineLoad=0`，玩家获得完整收线窗口。
- 朝竿游：点积为负，钳制为 0，不制造反方向的假拉力。

`LineLoad` 控制鱼线磨损、牵引效率和强对抗资格。玩家的左键已经进入带载牵引时，猫和鱼一定都会消耗体力，不会因为鱼正朝竿尖游或横切而归零：平静期使用 `InwardPull* × BaseDrainMultiplier`，挣扎期使用 `Stalemate* × StruggleDrainMultiplier`，且鱼性格要求挣扎倍率高于平静倍率。玩家没有主动拉、只有鱼自己把锁线绷紧时，双方消耗才继续按 `LineLoad` 缩放。重大判定仍要求负载超过鱼性格阈值并持续确认时间，避免方向刚好扫过阈值一帧就断线或落水。

鱼先按自己的水平游向和游速生成自由候选位置，锁线只阻止候选点继续越过线端。带载左键随后叠加一个不超过本步有效收线距离的水平牵引；鱼没有实际靠近到请求距离时，`L_paid` 以最终鱼距回填，表示卷线受阻，不会把几厘米收线通过三维球面投影放大成吸附或漂移。

### 鱼线为什么会垂、什么时候会绷紧

模拟不再把“鱼到竿尖的距离”直接当作线长，而是分别记录：

- `L_paid`：已经放出去的实际线长；左键请求收短但不得短于牵引后的真实鱼距，右键按住时只允许鱼向外游动被动带出更多线。
- `D`：竿尖到鱼的直线距离，由鱼的实际位置决定。
- `Slack=max(L_paid-D, 0)`：余线；大于 0 时 Cable 平滑增加本地重力，形成垂坠。
- `Tension`：鱼本步本想游到线端之外的超出量；线会限制鱼的位置，并产生鱼/猫体力消耗和竿磨损。

因此不按任何键时，`L_paid` 保持不变：鱼向内游会自然产生余线，鱼向外游先吃掉余线，碰到线端才绷紧。按住右键相当于打开线杯，它本身不会制造余线；鱼向外游到线端后，`L_paid` 才随鱼距增长，鱼静止或向内游时不会增加。只要还没到 `L_max`，松线状态不限制鱼的径向运动；整根线被带完后继续外冲才重新形成张力。Cable 粒子只在各客户端本地模拟，网络只复制上述几个紧凑标量和端点 Actor，不逐粒子同步。客户端用 60Hz 平滑锚点追赶 Hook 的权威/复制位置，并连续插值 CableLength 与 Slack 重力；Cable 使用固定子步和固定求解次数，避免收线时出现 20Hz 压缩阶跃与刚度模式跳变。

## FishLogic 4：为什么多人看到一致

入口：`ACatFishEncounterActor::ApplyFightStepFromAuthority()`。

随机流、方向、公式和水域修正只在服务器执行。服务器最终写入：

- Actor Transform：通过 UE Movement Replication 同步。
- `FishLineAlignment`：鱼方向和鱼线方向的点积。
- `NormalizedLineLoad`：0～1 的实际线负载。
- `bStrongConfrontation`：是否已经进入强对抗。
- `MotionIntent`：发力、休息或力竭侧翻收近。

客户端不再各自随机一次，否则同一条鱼会在每台机器上向不同方向游。AnimBP/UI 只消费这些复制后的事实。

## FishLogic 5：体力归零、侧翻与收近

相关入口：

- `FCatFishingFightSimulator::Step()`：判定 `FishExhausted`。
- `ACatFishingSession::BeginExhaustedReelFromAuthority()`：从搏斗切到力竭阶段，立刻发布侧翻状态，冻结竿尖表面投影，并保留左键的按住状态。
- `ACatFishingSession::HandleExhaustedReelStep()`：每个固定步向冻结的竿尖表面投影移动，绝不超过该点。
- `ACatFishingSession::SpawnExhaustedFishPickupFromAuthority()`：到达投影后在原位置生成所有玩家都可拾取的鱼。

此前调试 HUD 只显示整数百分比，极低的小数体力可能被显示成 `0%`。现在调试值保留一位小数；同时加入 `FishExhaustionThreshold=0.5`：结算后绝对体力不高于 0.5 时，服务器直接将尾数吸附到真正的 0，并立刻进入 `ExhaustedReel`，避免尾数阶段拖得过久。

鱼体力清空或力量碾压的同一服务器帧会保留 Encounter 的当前世界位置，并把 `AutoHauling` 复制给所有客户端驱动 `VisualRoot` 侧翻 90°；结局帧不会再把鱼或 D 直接归零到竿尖。松开左键只停止位移，不会让鱼重新立起。若耗尽时左键正按住，按住状态会跨阶段保留；否则再次按左键才开始移动。鱼随后按固定步逐步靠近、最多到达“竿尖 XY + max(水面 Z, 竿尖下方地面 Z)”，Encounter 隐藏并在原地生成可拾取 Actor。目标 XY 不做岸线限制，目标 Z 只在力竭瞬间查询一次，因此交接时不会二次跳位。

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
| 鱼力量和总搏斗体力 | 当前从正式鱼库选中的 `UCatFishDefinition` |
| 猫力量和体力 | `DefaultGame.ini` / 后续猫定义资产 |
| 收近速度和耗尽吸附阈值 | `UCatFishingSettings` / `DefaultGame.ini` |
| 张力表现达到满值的响应范围 | `TensionResponseRangeCentimeters`（Project Settings） |
| 竿力量、耐久和高张力磨损 | `DA_Rod_Basic` |

## 实测时怎么判断卡在哪层

1. UI 体力到 0 后查看日志是否出现 `Event=exhausted_reel_started`。
2. 出现 started 但鱼不动：检查 `Reeling=true`；false 表示服务器没有收到当前左键按住状态。
3. 鱼移动但没有变为物品：检查是否出现 `Event=exhausted_fish_reached_rod_tip_projection`。
4. 出现 reached_rod_tip_projection 但没有鱼物品：继续查看 `Exhausted fish pickup spawn failed` 和 Items 容器/世界物品配置。
