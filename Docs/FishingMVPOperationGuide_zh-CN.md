# 钓鱼 MVP 落地操作手册

面向：在 `Showcase2` 地图上把「放竿 → 抛竿 → 咬钩 → 搏斗 → 抄鱼」和「打窝」跑通。

本文只写**还没做的事**，按执行顺序排列。已经完成的部分见文末「当前完成度」。

---

## 目录

- [步骤 0：重启 Editor 并验证配置生效](#步骤-0重启-editor-并验证配置生效)
- [步骤 1：创建 ST_RunFlow](#步骤-1创建-st_runflow)
- [步骤 2：创建 ST_FishingSession](#步骤-2创建-st_fishingsession)
- [步骤 3：把两个 StateTree 路径写进 ini](#步骤-3把两个-statetree-路径写进-ini)
- [步骤 4：决定按键分配](#步骤-4决定按键分配)
- [步骤 5：四张蓝图节点图](#步骤-5四张蓝图节点图)
- [步骤 6：端到端测试](#步骤-6端到端测试)
- [已知缺口](#已知缺口)
- [当前完成度](#当前完成度)

---

## 步骤 0：重启 Editor 并验证配置生效

`Config/DefaultGame.ini` 已经写好，但 `UDeveloperSettings` **只在编辑器启动时读一次配置**，运行中改 ini 不会热加载（已实测：写完 ini 后在运行中的编辑器里读 CDO，全部还是默认值）。

1. 关闭 UE Editor，重新打开 `Catfishing.uproject`
2. 打开 **Edit → Project Settings**，左侧应该能看到这几项，逐个确认值已经进去：

| Project Settings 里的名字 | 确认什么 |
|---|---|
| Catfishing Fishing | `Enable Fishing Runtime` 已勾选，`Scoop Reach Centimeters` = 200 |
| Catfishing Character Abilities | `Default Ability Set` / `Ability Input Config` 指向 `/Game/Data/Abilities/` 下两个资产 |
| Catfishing Equipment | `Profile Loadout Trust Policy` = Enabled，`Definitions` 有 5 条 |
| Catfishing Fish Catalog | `Definitions` 有 1 条 |
| Catfishing Run | `Enable Run Runtime` 已勾选，`Player Scaling Policy` = Fixed Quota Target |
| Catfishing Environment | `Enable Environment Runtime` 已勾选，`Configured Weather` = Clear |
| Catfishing Fishing Presentation | 三个 Actor Class 指向 `/Game/Blueprint/Actors/` 下三个 BP |

如果某项还是默认值（未勾选 / None / 0），说明那一段 ini 没被读到 —— 检查 section 名拼写，别继续往下走。

---

## 步骤 1：创建 ST_RunFlow

> 📘 **没用过 StateTree？** 先读 **[StateTree 从零到能用](StateTreeTutorial_zh-CN.md)** —— 那里讲清了 State / Task / Transition / Schema 的概念、编辑器怎么操作、以及一个**引擎默认值导致的致命坑**（`Tasks Completion`）。本节只是操作清单，教程里有同样两棵树的逐步图解版。

**为什么必须先做这个**：`UCatFishingService::BeginCast` 第一道校验是 `GameMode->CanAcceptGameplayCommand()`，而 `bRunCommandsOpen` 默认是 `false`，只有 `ST_RunFlow` 进入 `DayActive` 阶段才会打开。没有这棵树，抛竿永远返回 `CommandsClosed`。

### 1.1 新建资产

1. Content Browser 进入 `/Game/Data/StateTrees`（没有就新建文件夹）
2. 右键 → **Artificial Intelligence → State Tree**
3. 弹出 Schema 选择器 → 选 **StateTree Component Schema**
4. 命名 `ST_RunFlow`

### 1.2 设置 Context Actor

打开 `ST_RunFlow`，在 **Schema** 设置里把 **Context Actor Class** 设为 **`CatfishingGameModeBase`**。

> 这一步不能漏。所有 Task 内部都是 `Cast<ACatfishingGameModeBase>(Context.GetOwner())`，Context Actor Class 不对，Cast 失败，节点直接返回 Failed。

### 1.3 节点说明

在 Task/Condition 选择器里搜 `Cat Run` 能找到三个节点（分类 `Catfishing|Run`）：

| 节点 | 类型 | 参数 |
|---|---|---|
| `Cat Run Enter Phase` | Task | `Phase`（ECatRunPhase）、`Reason`（ECatRunTransitionReason） |
| `Cat Run Wait For Event` | Task | 无参数，进入后保持 Running |
| `Cat Run Result Reason` | Condition | `ExpectedReason` |

C++ 只会发这 4 个事件：

```
Cat.Run.QuotaReached
Cat.Run.QuotaFailed
Cat.Run.AllEligibleReady
Cat.Run.SettlementComplete
```

### 1.4 MVP 最小拓扑

```
Root
├── DayActive                      ← 第一个子状态 = 起始状态
│     Tasks Completion: All        ← ⚠️ 必须改，默认 Any 会让 State 秒退
│     Tasks: [Cat Run Enter Phase (Phase=DayActive, Reason=None)]
│            [Cat Run Wait For Event]
│     Transitions:
│       On Event  Cat.Run.QuotaReached  →  NormalNight
│       On Event  Cat.Run.QuotaFailed   →  FailureNight
│
├── NormalNight
│     Tasks Completion: All        ← ⚠️
│     Tasks: [Cat Run Enter Phase (Phase=NormalNight, Reason=QuotaReached)]
│            [Cat Run Wait For Event]
│     Transitions:
│       On Event  Cat.Run.AllEligibleReady  →  DayActive
│
└── FailureNight
      Tasks Completion: All        ← ⚠️
      Tasks: [Cat Run Enter Phase (Phase=FailureSettlementNight, Reason=QuotaFailed)]
             [Cat Run Wait For Event]
      Transitions:
        On Event  Cat.Run.SettlementComplete  →  DayActive
```

**说明与注意事项**

- ⚠️ **每个多 Task 的 State 都必须把 `Tasks Completion` 从默认的 `Any` 改成 `All`。** 引擎默认 `Any` 的含义是「任何一个 Task 完成，State 就完成」—— `Cat Run Enter Phase` 一返回 Succeeded，State 立刻退出，`Wait For Event` 白搭。改成 `All` 后要所有 Task 都完成才算完成，而 `Wait For Event` 永远 Running，State 就被钉住了。详见 [StateTree 教程第 4 节](StateTreeTutorial_zh-CN.md#4-️-state-什么时候算完成最大的坑)。
- Task 顺序有意义：`Enter Phase` 必须排在 `Wait For Event` 之前。
- **起始状态由排列顺序决定**：树启动时选中 Root 的第一个子状态，所以 `DayActive` 必须排第一。
- **不要接 `SuccessSettlementNight`**：`EnterRunPhaseFromStateTree` 里有硬校验，`SuccessSettlementPolicy != Enabled` 时进这个阶段会直接返回 `PolicyUndecided`（我在 ini 里没启用这条策略）。
- MVP 只要 `DayActive` 一个状态能进去就够钓鱼了，`NormalNight`/`FailureNight` 是为了让白天到期后不卡死。嫌麻烦可以先只做 `DayActive`。
- 进入 `DayActive` 会启动一个 `DayLengthSeconds`（ini 里配的 600 秒）的倒计时，到期额度不够就发 `QuotaFailed`。测试期间如果嫌 10 分钟太短，改 ini 里的 `DayLengthSeconds` 再重启。

---

## 步骤 2：创建 ST_FishingSession

### 2.1 新建资产

同样是 **Artificial Intelligence → State Tree**，Schema 选 **StateTree Component Schema**，命名 `ST_FishingSession`。

**Context Actor Class 设为 `CatFishingSession`。**

### 2.2 先理解：谁在推进阶段

这是这棵树最反直觉的地方 —— **大部分阶段是 C++ 自己写的，StateTree 只负责起头和等**：

| 阶段 | 谁写的 |
|---|---|
| Waiting | `Cat Fishing Schedule Waiting Probe` 节点内部自己 EnterPhase |
| Probe | StateTree 的 `Cat Fishing Enter Phase` 节点 |
| TrueBiteWindow | `Cat Fishing Open True Bite Window` 节点打开通用响应窗；此时没有鱼 Actor |
| HookedFight | 真咬窗内收到左键后，`RequestHook` 才选鱼、生成 Actor、扣饵并 EnterPhase |
| ExhaustedReel | 鱼体力耗尽或力量碾压后，Session 保留鱼的死亡瞬间位置、停止搏斗 Runner 并进入；随后仅在持续左键时有限速收近 |
| Resolved / Terminated | `FinalizeSession()`，**StateTree 禁止进入** |

### 2.3 节点说明

搜 `Cat Fishing` 能找到这些（分类 `Catfishing|Fishing`）：

| 节点 | 类型 | MVP 用不用 |
|---|---|---|
| `Cat Fishing Schedule Waiting Probe` | Task | ✅ 用 |
| `Cat Fishing Enter Phase` | Task（参数 `Phase`） | ✅ 用 |
| `Cat Fishing Open True Bite Window` | Task | ✅ 用 |
| `Cat Fishing Open True Bite Window (Legacy Node)` | Task | ❌ 仅用于加载旧资产 |
| `Cat Fishing Start Fight Runner` | Task | ✅ 用 |
| `Cat Fishing Wait For Fight Runner` | Task | ✅ 用 |
| `Cat Fishing Wait` | Task | ✅ 用 |
| `Cat Fishing Phase Is` | Condition（参数 `ExpectedPhase`） | 可选 |
| `Cat Fishing Fight Exchange` | Task | ❌ 不用（见下） |
| `Cat Fishing Commit Failure Budget` | Task | ❌ 不用 |
| `Cat Fishing Resolve Retry Exhausted Escape` | Task | ❌ 不用 |

> `Cat Fishing Fight Exchange` 在当前 Runner 驱动的搏斗模型下不能用 —— 它的实现开头就检查 `FightRunner->IsRunning()`，运行中会直接拒绝。搏斗数值全在 `UCatFishingFightRunner` 里跑。

C++ 实际只发这 5 个事件（头文件里声明了 16 个，其余 11 个**没有任何发送点**，别在资产里等它们）：

```
Cat.Fishing.Event.ProbeTriggered    ← 咬钩计时器到期
Cat.Fishing.Event.HookAccepted      ← 真咬窗口内成功提竿
Cat.Fishing.Event.WindowExpired     ← 真咬窗口超时
Cat.Fishing.Event.EarlyHook         ← 过早提竿（空竿）
Cat.Fishing.Event.Interrupted       ← 主动取消
```

### 2.4 MVP 拓扑

```
Root
├── Waiting                                    ← 树启动后的第一个状态
│     Tasks Completion: All                    ← ⚠️
│     Tasks: [Cat Fishing Schedule Waiting Probe]
│            [Cat Fishing Wait]
│     Transitions:
│       On Event  Cat.Fishing.Event.ProbeTriggered  →  Probe
│
├── Probe
│     Tasks Completion: All                    ← ⚠️
│     Tasks: [Cat Fishing Enter Phase (Phase = Probe)]
│            [Cat Fishing Open True Bite Window]
│            [Cat Fishing Wait]
│     Transitions:
│       On Event  Cat.Fishing.Event.HookAccepted  →  HookedFight
│       On Event  Cat.Fishing.Event.WindowExpired →  Waiting
│
├── HookedFight
│     Tasks Completion: All                    ← ⚠️ 这里尤其关键
│     Tasks: [Cat Fishing Start Fight Runner]
│            [Cat Fishing Wait For Fight Runner]
│     Transitions:
│       On State Succeeded  →  ExhaustedReelHold
│
└── ExhaustedReelHold
      Tasks: [Cat Fishing Wait]              （只有一个 Task，Completion 不用管）
      （无转移，等 C++ 的继续收线落地或抢抄事务结算）
```

### 2.5 六条必须守住的规则

1. ⚠️ **多 Task 的 State 必须把 `Tasks Completion` 改成 `All`。** 引擎默认是 `Any`（任一 Task 完成即 State 完成）。`HookedFight` 尤其致命：默认设置下 `Start Fight Runner` 一 Succeeded 就立刻跳走，搏斗根本没机会跑。另一种改法（取消 Task 的 `Considered For Completion` 勾选）**不要用** —— 被排除的 Task 连 Failed 都不会传播，出问题极难排查。

2. **树永远不能自然结束。** `EnterPhaseFromStateTree` 等写口都要求 `StateTreeComponent->IsRunning()`。任何叶子状态都必须停在 `Cat Fishing Wait` 上，让树保持 Running。转移 Target 也绝不能选 `Tree Succeeded` / `Tree Failed`。

3. **`Cat Fishing Enter Phase` 的 Phase 不能选 `Resolved` 或 `Terminated`。** 选了会直接返回 `AlreadyResolved` → Failed。终态只能由 C++ 写。

4. **`ExhaustedReelHold` 状态不要再放 `Enter Phase`。** C++ 在搏斗 Runner 结束时已经 EnterPhase(ExhaustedReel) 过了，重复进入只会白白递增 Revision。旧资产中的叶子状态即使仍命名为 `NearShore` 也能兼容运行，但建议改名避免误解。

5. **`WindowExpired` 必须接回 `Waiting`。** 漏按只关闭这一轮响应窗，不释放竿、线或饵料预约；Waiting 重入后会清空窗口并重新调度。`EarlyHook` / `Interrupted` 才由 C++ 直接终止并停树，不需要资产终态。

6. **`Cat Fishing Start Fight Runner` 是幂等的。** `RequestHook` 在发 `HookAccepted` 之前就已经启动了 Runner，这个节点检测到已在运行会直接返回 Succeeded，不会重复启动。

---

## 步骤 3：把两个 StateTree 路径写进 ini

打开 `Config/DefaultGame.ini`，在对应 section 下加两行（路径按你实际存放位置改）：

```ini
[/Script/Catfishing.CatRunSettings]
RunFlowStateTree=/Game/Data/StateTrees/ST_RunFlow.ST_RunFlow

[/Script/Catfishing.CatFishingSettings]
FishingSessionStateTree=/Game/Data/StateTrees/ST_FishingSession.ST_FishingSession
```

**改完再重启一次 Editor。**

> `UCatFishingSettings::IsRuntimeReady()` 里有 `!FishingSessionStateTree.IsNull()` 这一条，这两行不填，整个钓鱼链依然是 fail-closed 状态。

---

## 步骤 4：按键分配（鱼竿与通用交互分离）

钓鱼 Ability 继续复用原有 InputAction；通用拾取交互新增一个 `IA_Interact`。可以执行《InteractionInputPythonSetup_zh-CN.md》中的脚本自动完成接线：

| 键 | InputAction | 用途 | 走哪条路 |
|---|---|---|---|
| **R** | `IA_PutDownFishingRod` | **鱼竿一键三态**：已占任意竿位→离开 / 附近鱼竿还有空位→加入 / 否则→放自己的竿。第一人吸附右主位，第二人吸附左辅助位 | ✅ C++ 已实现，服务器按事实自动分派 |
| **E** | `IA_Interact` | 准星交互/拾取；本地只选择 Current Target，真正拾取由服务器复核距离、视线和物品状态 | ✅ C++ 已实现，走 Native InputTag 而不是 Gameplay Ability |
| **左键** | `IA_LMB` | 无会话→**长按预览抛物线（不蓄力）松手抛竿**；真咬窗→**提竿**（1 秒内=完美）；遛鱼→**按住拖** | 提竿/拖 ✅ C++；**抛竿预览+提交走蓝图**（5.2） |
| **右键** | `IA_RMB` | 遛鱼时**按住松开线杯**（鱼在 L_max 内自由带线，发力期喘气回体力 +1.5/s） | ✅ C++（`UCatGA_FishingSlack`） |
| **Q** | `IA_BaitSpot` | **长按蓄力打窝**：抛物线越蓄越远，松手投出 | 蓄力预览+提交走蓝图（5.3）；同键上的占位符 Chum Ability 会同时发一条无害的空命令 |
| **F** | `IA_CatchFish` | 抢抄 | ✅ C++ |
| **X** | `IA_CancelFishing` | 取消当前会话 | ✅ C++ |

`DA_CatAbilityInputConfig.AbilityInputActions` 是 **6 条**（5 个核心 + `Cat.Input.Fishing.Slack` → `IA_RMB`）；`DA_CatAbilitySet_Default` 相应 6 个 Ability。另有 `NativeInputActions`：`Cat.Input.Interact` → `IA_Interact`，它不授予第 7 个 Ability。

> 左键与 Q 上，GAS Ability 和你的蓝图绑定会**同时触发**（同一个 IA 两条独立绑定）。无会话时按左键，GAS 的 `RequestHook` 会拿到一条 `DependencyUnavailable` 回执，无害；按 Q 时占位符 Chum Ability 同理。UI 若监听 `OnResultReceived` 弹失败提示，请按 `CommandType` 过滤这两种。

---

## 步骤 5：四张蓝图节点图

这四件事**不需要新建 GameplayAbility**，全部走 `UCatFishingCommandComponent` 上现成的 `BlueprintCallable` 函数。建议全部挂在 **`BP_CatFishingController`** 里（它已经是 `ACatfishingPlayerController` 子类，能直接拿到命令组件）。

在 `BP_CatFishingController` 的 `Event Graph` 里，先用 Enhanced Input 事件节点接住按键（`EnhancedInputAction IA_xxx`），再接下面的链。

**通用第一步**（每张图都要）：

```
Get Fishing Command Component      ← BlueprintPure，Controller 自己身上就有
```

**通用状态缓存**：在 Controller 蓝图里加几个变量存放上一步的结果：

| 变量名 | 类型 | 用途 |
|---|---|---|
| `CachedRodActorId` | Guid | PlaceRod 返回，BeginCast 要用 |
| `CachedRodActorRevision` | Integer64 | 同上 |
| `PendingRequestId` | Guid | 用来轮询结果 |
| `LevelWaterRegion` | Actor Object Reference (`BP_CatWaterRegion`) | 关卡里那个湖，BeginCast/PlaceChum 要用 |

`LevelWaterRegion` 可以在 `BeginPlay` 里用 `Get Actor Of Class (BP_CatWaterRegion)` 拿一次存起来。

---

### 5.1 放竿 / 操作 / 离开 —— 已由 R 键 C++ 三态接管，**不用做蓝图**

按 R 服务器自动分派 PlaceRod / OperateRod / LeaveRod。第一次 R 只部署空杆并播放放杆表现，第二次 R 才进入右侧主位，第三次 R 离开；不能把部署和使用合并。架杆不再要求靠近岸线，只要角色前方有坡度合法的实体地面即可；抛竿阶段仍受有效水域和射程限制。你只需要**接结果并缓存**：

```
Get Fishing Command Component → Bind Event to On Result Received
  │
  └─ 回调 (Result: FCatFishingCommandResult)
        Branch: Result.CommandType == PlaceRod 或 OperateRod，且 Result.bCommitted
          → CachedRodActorId       = Result.RodActorId
          → CachedRodActorRevision = Result.RodActorRevision
```

BeginCast 要用这两个值做乐观锁；OperateRod 成功后 `RodActorRevision` 会变，所以**每次 R 成功都要刷新缓存**。

**常见失败原因**（放竿）：`InvalidPayload`=前方太斜/没实体地面；`DependencyUnavailable`=还没装配（5.4）。`InvalidWaterTarget/CastOutOfRange` 现在只属于抛竿阶段。

多人占位口径：`OperatorPlayerStates[0]` 是右侧主位，当前只有主位能抛竿/提竿/收线/松线；`[1]` 是左侧辅助位，先完成同步站位。搏斗规则层已预留两项并按“主操作猫力量 + 第二只猫力量”计算猫总体力量，但第二项运行时仍为 0；第二只猫怎样加入、何时生效、体力如何分摊和输入如何配合仍是 `TODO(CooperativeFishing)`。主位退出时左位自动晋升，数组长度立刻从 2 变 1，不保留旧双人模式。当前参数在 `Project Settings → Catfishing Fishing → Rod|Operators`：槽位数 2，左右间距 140cm。

会话跟随鱼竿而不是角色：按 R 离开任何阶段都只释放竿位与持续输入，不会直接结束鱼竿上的会话。多人各自部署鱼竿后，同一玩家可以在第一根竿抛线、离开，再进入第二根空竿抛线；左键与 HUD 始终只路由当前主操作鱼竿。等待/试探/真咬阶段允许其他玩家接力；搏斗阶段的跨玩家资源迁移仍未接入，但原钓手可以重新回到该竿继续。

---

### 5.2 抛竿 —— 已由左键 C++ 接管，**不用做蓝图**（预览可选）

规格 3.1：点哪落哪、无蓄力。现在的实现：

- **左键按下**（无会话时）：服务器无副作用，回执 `RequestHook Committed=true`（= 开始瞄准）
- **左键松开**：服务器用你的**准星射线 ∩ 水面**算落点，自动填 RodActorId / Revision / Equipment Revision / WaterRegion Handle，走 `BeginCast` 全部校验（射程 ≤ min(竿线长, 漂抛距)、夹角 ≤ 60°、视线无遮挡）
- 前置：已按 R 进入鱼竿主操作位；没在操作时松开左键会被**静默忽略**（不刷日志）

结果日志：`Event=begin_cast_result Committed=true Landing=...` 或 `Committed=false Error=CastOutOfRange/InvalidWaterTarget/...`

**可选的客户端预览蓝图**（纯表现，不影响判定）：按住左键期间每帧调
`UCatFishingAimLibrary::ResolveCastAimPoint(ViewLocation, ViewRotation) → Landing`，用 `Draw Debug Sphere` 画落点即可。这个函数和服务器用的是**同一份**，预览点 = 真实落点。

---

### 5.3 打窝 —— 已由 Q 键 C++ 接管，**不用做蓝图**（蓄力预览可选）

规格 3.1 打窝：蓄力抛掷、抛物线预览。现在的实现：

- **Q 按下**：服务器记时刻
- **Q 松开**：服务器按按住时长算 `ChargeAlpha = clamp(held / ChumChargeMaxSeconds)`，初速 `Lerp(Min, Max, Alpha)`，用引擎 `PredictProjectilePath` 得到落点，选一份可用窝料（优先 `StarterChumDefinitionId`），走 `PlaceChum` 全部校验（射程/夹角/视线/库存/水域）
- 参数在 `Project Settings → Catfishing Fishing → Chum|Throw`：`ChumChargeMaxSeconds=1.5`、`Min/MaxSpeed=600/1400`、`Elevation=35°`、`ThrowQuantity=1`

结果日志：`Event=chum_throw Held=.. Alpha=.. Landing=..` + `Event=place_chum_result Committed=...`

**可选的客户端蓄力预览蓝图**：Q 按下记 `PressTime`，按住期间每帧：
```
Alpha   = ChargeAlphaFromHeldSeconds(Now - PressTime)                         ← UCatFishingAimLibrary
PredictChumThrow(CharacterLocation, ControlRotation, Alpha) → Path, Landing, bHitWater
Draw Debug Line 逐段画 Path；Draw Debug Sphere 画 Landing（bHitWater 绿 / 否则红）
```
同样是服务器同一份数学，预览线 = 真实弹道。

---

### 5.4 ConfigureEquipment（首次装配）—— 必须最先执行

**这是整条钓鱼链最上游的前置条件。** `FCatEquipmentLoadoutSnapshot` 初始是空的（三个 ID 都是 `None`），`PlaceRod`/`BeginCast` 会因为 Kind 校验失败直接拒绝。

最简单做法：在 `BP_CatFishingController` 的 `BeginPlay` 里自动装配一次。

```
Event BeginPlay
  │
  ├─ (只在本地控制的 Controller 上执行：Is Local Controller ?)
  │
  ├─ Get Player Character → Get Equipment Component → Get Snapshot
  │      → Branch: Snapshot.RodDefinitionId == None ?     ← 只在没装配时执行
  │
  ├─ New Guid
  │
  └─ Server Configure Equipment              ← BlueprintCallable（本轮新加的）
         RequestId            = New Guid
         ExpectedRevision     = Snapshot.Revision     ← 首次是 0
         RodDefinitionId      = "Rod_Basic"
         BaitDefinitionId     = "Bait_Basic"
         FloatDefinitionId    = "Float_Basic"
         ScoopNetDefinitionId = "StarterScoopNet"     ← 第 4 个参数；当前开发配置会另行默认发放并选中
```

**怎么知道成功了**：这是个 `Server, Reliable` RPC，没有回执结构体。判断方式是**轮询 `Get Snapshot` 的 `Revision` 是否从 0 变成 1**，或者监听装备组件的复制变化。建议在 UI 上显示当前 `RodDefinitionId`，非 `None` 就说明装配好了。

> `RequestScoop` 仍要求服务器装备快照里存在有效 `ScoopNet`。当前 `bAutoGrantStarterScoopNet=True` 会在服务器首次占有时给每名玩家发放并选中 `StarterScoopNet`，所以手工装配留空也能继续测试抄网；正式获取方式接入后关闭该临时开关，届时必须通过商店/奖励获得并选择抄网。

---

## 步骤 6：端到端测试

按顺序做，每步在 Output Log 里过滤对应关键字确认。

| # | 操作 | 期望日志 / 现象 |
|---|---|---|
| 1 | PIE 启动 | `Event=run_started` 且 `Event=run_phase_entered ... Phase=DayActive` |
| 2 | （自动）装配 | Equipment `Revision` 从 0 → 1，`RodDefinitionId = Rod_Basic` |
| 3 | 在任意合法地面第一次按 R | 世界里出现无人操作的 Rod Actor并播放放杆表现；角色不吸附、不锁移动 |
| 3.1 | 放置者再次按 R | 放置者进入右侧主位并开始操作，`OperatorPlayerStates.Num=1` |
| 4 | 第二个玩家走近同一根竿按 R | 第二人被吸附到左侧辅助位；两端都看到 `OperatorPlayerStates.Num=2` |
| 4.1 | 主位玩家按 R 离开 | 左侧玩家晋升到右主位；占位人数立即回到 1，之后可继续按单人逻辑操作 |
| 5 | 瞄水面按住再松开左键 | `Event=fishing_phase_entered ... Phase=Waiting`，浮漂飞出去 |
| 6 | 等浮漂落水 | Hook 的 `BP_OnHookPresentationChanged` 收到 `Phase=Landed` |
| 7 | 等咬钩 | `Phase=Probe` → 紧接着 `Phase=TrueBiteWindow`，鱼 Actor 生成 |
| 8 | 3 秒内按住左键 | `Phase=HookedFight` |
| 9 | 持续按住左键收线 | Snapshot 里 `NormalizedFishStamina` 下降 |
| 10 | 鱼被收到面前（**搏斗中就可以**） | debug 里鱼身上的圈从红变绿 = 现在按 F 抄得到 |
| 11 | 对着鱼按 F | 不论鱼剩余体力，范围合法即直接变成嘴叼世界鱼；失败看 `Event=scoop_rejected` 的逐项谓词 |
| 12 | 或者等鱼翻肚 | `Phase=ExhaustedReel`；仍可按 F 抄，也可继续按住左键把鱼拖上岸 |
| 13 | 鱼落到岸上后准星对准并按 E | 服务器只允许一个玩家成功叼起；随后再对具体地面鱼护按 E 才入箱 |

鱼生成时的大小由服务器随机重量决定：`1kg` 对应当前 Mesh 的 `Scale=1`，重量按体积关系取立方根换算并裁在
`0.75~1.75`。水中鱼和岸上拾取鱼共用同一个复制缩放值；若要调观感，到项目设置
`Catfishing Fishing Presentation > FishScale` 修改参考重量与上下限，不要在蓝图里再次随机 Scale。

**抄网范围**（详见 `FishingArchitecture_zh-CN.md` §2.5）：猫向正前方发一条水平线段，与挂在鱼身上的圆相交即够得着，**纯俯视投影不看俯仰角**；高度差另由 `MaximumScoopVerticalDeltaCentimeters`（默认 250）卡上限。线段长 = `min(ini 的 ScoopReachCentimeters, 抄网 DA 的同名字段)`，圆半径 = 鱼 DA 的 `ScoopTargetRadiusCentimeters`（**为 0 则永远抄不到**）。

**咬钩要等多久**：`BaseBiteRatePerSecond=0.2` + 泊松分布，clamp 在 `[2, 15]` 秒。嫌慢就把 ini 里 `BaseBiteRatePerSecond` 调大（比如 2.0）再重启。

**日志过滤关键字**：`LogCatRun`、`LogCatFishing`、`LogCatEquipment`

---

## 已知缺口

### ~~1. 抢抄需要 ScoopNet，但装配接口传不进去~~ ✅ 已修

`ServerConfigureEquipment` 已加第 4 个参数 `ScoopNetDefinitionId` 并往下传给 `ConfigureLoadoutFromAuthority`。当前正式目录定义填 `"StarterScoopNet"`；开发期默认发放开启时可留空沿用服务器已选中的抄网。

### ~~2. 打窝需要窝料库存，但没有发放入口~~ ✅ 已修

数量型物品不再提供客户端直连发放 RPC；调试和正式链路都应通过服务器权威入口把物品写入统一库存格。

> 没有直接给组件方法加 `BlueprintCallable`，而是走 Controller RPC 转发 —— 和 `ServerConfigureEquipment` / `ServerRepairRodAtCamp` 保持一致的权限边界，避免任何蓝图都能直接摸到域写入口。

### 3. `IA_BaitSpot`(Q) 那个 Chum Ability 是占位符

`UCatGA_FishingChum` 发的是一个**不带载荷**的命令（没有目标点、没有窝料 ID、没有数量），服务器 `HandleAbilityCommandFromAuthority` 里没有 `PlaceChum` 分支，必然落到 `DependencyUnavailable`。

它存在的唯一原因是 `UCatAbilitySet::IsRuntimeReady()` 强制要求 5 个 InputTag 齐全。**不要试图修它** —— 打窝本质上需要客户端提供瞄准点和窝料选择，走步骤 5.3 的独立蓝图路径才是对的。Q 键留着当占位就行。

---

### 一个不阻塞的小瑕疵

`ScheduleWaitingProbeFromStateTree` 里第一次采样窝料浓度时，用的是钩子**刚生成时**的位置（鱼竿竿尖），而不是最终落水点 —— 因为这一步比 `BeginAuthoritativeFlight` 先跑。

只影响第一口的咬钩延迟精度，不影响选鱼（选鱼发生在 Probe 事件触发时，那时钩子已经落水）。后续要精调打窝手感时再处理。

---

## 当前完成度

### ✅ 已完成

**C++**（已编译验证通过，`Result: Succeeded`）

| 文件 | 改动 |
|---|---|
| `CatFishingCommandComponent.cpp` | `HandleAbilityCommandFromAuthority` 新增 OperateRod / 搏斗收线 / Scoop 三条分支 |
| `CatGameplayTypes.h` | `ServerConfigureEquipment` 加 `BlueprintCallable` |
| `CatEquipmentComponent.h` | `GetSnapshot()` 加 `BlueprintPure` |
| `CatCharacter.h` | `GetEquipmentComponent()` / `GetConditionComponent()` 加 `BlueprintPure` |
| `CatWaterRegion.h` | `GetWaterRegionHandle()` / `HasValidBakedGeometry()` 加 `BlueprintPure` |
| `CatFishingHookActor.h/.cpp` | 浮漂落水确认（有界轮询计时器，非 Tick），`Phase` 能走到 `Landed` |

**关键资产**（已存盘）

```
/Game/Data/Abilities/   DA_CatAbilityInputConfig, DA_CatAbilitySet_Default
/Game/Catfishing/Data/Equipment/   Equip_Rod_StarterT1, Equip_Bait_Bug, Equip_Float_Feather,
                                   Equip_ScoopNet_Starter, Equip_Chum_Bug（以及其他正式目录定义）
/Game/Catfishing/Data/Fish/  正式 Fish_*, Bite_*, Fight_*
/Game/Data/Fish/             未注册历史测试二进制；不得再作为运行鱼库入口
/Game/Data/Curves/      Curve_ChumDistanceFalloff, Curve_ChumTimeFalloff
```

**配置**：`Config/DefaultGame.ini` 已注册正式鱼、咬钩性格与搏斗性格软引用；旧测试资产不再注册。

**关卡**：`Showcase2` 的唯一 `BP_CatWaterRegion` 已迁移为 `RegionId=River` 并重新烘焙、保存、重载验证；当前 `GeometryRevision=776404699334229561`，`HasValidBakedGeometry()=True`。该水域现在直接匹配正式 16 鱼库中的 `River` 鱼种。

**框架蓝图**（全部已存在且父类正确）

```
BP_CatFishingGamemode  → ACatfishingGameModeBase   （已设为地图 GameMode Override）
BP_CatCharacter        → ACatCharacter
BP_CatFishingController→ ACatfishingPlayerController
BP_CatFishingRodActor  → ACatFishingRodActor
BP_CatFishingHookActor → ACatFishingHookActor
BP_CatFishEncounterActor → ACatFishEncounterActor
BP_CatWaterRegion      → ACatWaterRegion
CatWaterBoundarySplineActor → ACatWaterBoundarySplineActor
```

**输入**：`IMC_InputContext` 已映射全部 6 个钓鱼相关按键；PlayerController 的 GAS 输入绑定链（`Started`→Pressed / `Completed`→Released + `PostProcessInput`）完整

### ✅ 已完成（续）

- `ST_RunFlow` / `ST_FishingSession` 两棵树已建好，结构核对通过：Schema、Context Actor、`Tasks Completion=All`、Task 顺序与参数、转移链路全部正确
- 两条 StateTree 路径已写进 `Config/DefaultGame.ini`
- C++ 缺口 1、2 已补；数量型物品改走统一库存格，旧直连发放入口已移除

### ✅ PIE 已验证通过（2026-08-18）

一次 PIE 实跑，确认下列链路全部工作：

```
Event=run_started       RunId=67F080D2... StateTree=ST_RunFlow
Event=run_phase_entered Day=1 Phase=ECatRunPhase::DayActive Deadline=600.000
```

- `ST_RunFlow` 启动并进入 `DayActive` → `bRunCommandsOpen=true`，钓鱼命令门已打开
- 两个 StateTree 软引用都能正确解析
- `BP_CatCharacter` / `BP_CatFishingController` 正常生成
- **五项初始属性从 ini 注入成功**：`Hunger=100 Fatigue=0 Poison=0 FishingStrength=10 FightStamina=100`
  → 这证明 `IsFishingRuntimeReady()` 为 true，即 AbilitySet 和 InputConfig 两个资产都通过了严格校验，Ability 已授予、输入已绑定
- Equipment Loadout 仍为空（`Revision=0`）—— 符合预期，等 ConfigureEquipment 蓝图

### ✅ 遛鱼按规格重写（2026-08-19）并 PIE 验证

- `Step()` = 规格 4.3 判定表 + 4.4 消耗战；右键松开线杯（`UCatGA_FishingSlack`）；完美中鱼接线；E 键三态放/操作/离开
- `DA_CatAbilityInputConfig` / `DA_CatAbilitySet_Default` 各 6 条（含 Slack）；PIE 中猫 `FishingStrength=50`、Chum runtime=True、规格系数已加载
- `UCatFishingViewBridge` 已暴露蓝图（`CreateFishingViewBridge` / `FindFishingSessionForPlayerState` / `BindSession` / `OnViewStateChangedBP`），`ACatFishingSession::GetReplicatedSnapshot()` 可读
- 窝点表现类可配：`[CatFishingPresentationSettings] ChumFieldPresentationClass=`

### ⬜ 待办（你）

1. 表现蓝图：`BP_CatFishEncounterActor` / `BP_CatFishingRodActor` / `BP_CatFishingHookActor` 挂 Mesh 并实现 `BP_On*PresentationChanged`；新建 `BP_CatChumFieldPresentation`（父类 `CatChumFieldPresentationActor`）并把类路径写进 ini
2. `BP_CatFishingController`：5.4 ConfigureEquipment（4 参数）→ `Server Grant Run Consumable` 发窝料 → 5.1 接 E 键结果缓存 → 5.2 左键长按预览+抛竿 → 5.3 Q 蓄力+打窝
3. （可选）HUD：用 ViewBridge 订阅会话状态
4. （可选）`ST_RunFlow` 补夜晚循环

### ⬜ 待办（我）

- ~~C++ 调试可视化~~ ✅ 已完成：`cat.Fishing.Debug 0/1/2`（默认 0；0=世界标记全关，1=全量，2=只留抄网射线+鱼圈+鱼线+阶段提示）；右上角当前鱼种 ID 与鱼/竿/猫体力、耐久和力量面板由独立的 `cat.Fishing.Stats 0/1` 控制（默认 1）
- 浮漂弹道修正（现在飞行轨迹落不到目标点会"瞬移"）
- 规格后续：抄网道具化/概率/硬直/无网拾取/翻肚 30s；窝料并池与 30s×0.9；`T_base/(1+Total/K)` 浮漂级计时器；浮漂精准偏移；入夜停咬

### ⚠️ StateTree 资产损坏事故记录

`ST_RunFlow` 第一版曾因一条**畸形转移**（`Trigger=OnEvent` 但 Event Tag 为空、Target 指向 `Root`）导致编译产物损坏：节点表的 `InstanceTemplateIndex` 与实例数据容器对不上，加载时在 `UStateTree::PostLoad()` 断言崩溃（`InstancedStructContainer.h:124`）。资产已重建，坏文件保留为 `ST_RunFlow.uasset.corrupt.bak`。

**防止复发**：
- 转移绝不能留空的 Event Tag —— 要么填完整，要么整条删掉
- `GotoState` 的 Target 不要指向 `Root`
- **保存前先点 Compile 并确认 Compiler Results 无报错**，编译失败时不要保存

---

## 附：BakeGeometry 的坑

如果以后改了样条或 Region 参数需要重新烘焙：

**点 BakeGeometry 不会有任何视觉反馈**，这是设计如此。它只写 Actor 内部的 `BakedGeometry` / `GeometryRevision`，不生成 Mesh、不刷新视口。湖面表现 Mesh（`ACatWaterRegionPresentationActor`）只在 `BeginPlay` 才 spawn，而且代码里显式 `SetWaterPreviewVisible(false)`。

**验证方式**：看 Details 面板里的 `GeometryRevision` 是否从 0 变成非零。

**已知陷阱**：地图刚加载完时，如果后台还在异步编译 Mesh / 重算物理（Output Log 在刷 `recomputing physics on load`），Blueprint Actor 可能被重建，把烘焙结果冲掉。**等编辑器完全空闲再点**，烤完记得存盘（Ctrl+S）。

**烘焙失败的常见原因**（`BuildCurrentGeometryInput` 里的硬校验，任一不满足就整体失败且视口无提示）：

1. Region Actor 的 Transform 必须**只有 Yaw 旋转**（Pitch/Roll 必须为 0）、**缩放必须是 (1,1,1)**
2. 每条 Boundary 样条 Actor 身上的 `OwningRegion` 必须指回该 Region
3. `BoundaryActors` 数组里不能有空指针
4. 样条控制点太少或自交，导致自适应采样超过 20 层递归深度
