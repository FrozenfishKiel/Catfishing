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
│       On Event  Cat.Run.QuotaFailed   →  FailureSettlementNight
│
├── NormalNight
│     Tasks Completion: All        ← ⚠️
│     Tasks: [Cat Run Enter Phase (Phase=NormalNight, Reason=QuotaReached)]
│            [Cat Run Wait For Event]
│     Transitions:
│       On Event  Cat.Run.AllEligibleReady  →  SuccessSettlementNight（需 Cat Run Success Settlement Eligible）
│       On Event  Cat.Run.AllEligibleReady  →  DayActive
│
└── FailureSettlementNight
      Tasks Completion: All        ← ⚠️
      Tasks: [Cat Run Enter Phase (Phase=FailureSettlementNight, Reason=QuotaFailed)]
             [Cat Run Wait For Event]
      Transitions:
        On Event  Cat.Run.SettlementComplete  →  Ending
└── SuccessSettlementNight
      Tasks Completion: All
      Tasks: [Cat Run Enter Phase (Phase=SuccessSettlementNight, Reason=AllEligibleReady)]
             [Cat Run Wait For Event]
      Transitions:
        On Event  Cat.Run.SettlementComplete  →  Ending
```

**说明与注意事项**

- ⚠️ **每个多 Task 的 State 都必须把 `Tasks Completion` 从默认的 `Any` 改成 `All`。** 引擎默认 `Any` 的含义是「任何一个 Task 完成，State 就完成」—— `Cat Run Enter Phase` 一返回 Succeeded，State 立刻退出，`Wait For Event` 白搭。改成 `All` 后要所有 Task 都完成才算完成，而 `Wait For Event` 永远 Running，State 就被钉住了。详见 [StateTree 教程第 4 节](StateTreeTutorial_zh-CN.md#4-️-state-什么时候算完成最大的坑)。
- Task 顺序有意义：`Enter Phase` 必须排在 `Wait For Event` 之前。
- **起始状态由排列顺序决定**：树启动时选中 Root 的第一个子状态，所以 `DayActive` 必须排第一。
- `NormalNight` 收到 `Cat.Run.AllEligibleReady` 时要有两条边：第一条挂 `Cat Run Success Settlement Eligible` 去 `SuccessSettlementNight`，第二条无条件回 `DayActive` 翻下一天。
- `DayActive` 会启动 `DayLengthSeconds` 倒计时，到期额度不够就发 `QuotaFailed` 进入失败结算夜。测试期间可以改 ini 里的值后重启，也可以在开发期用 `cat.RunEnvironmentSocial.DayLength <秒数>` 临时改当前白天。

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
| ExhaustedReel | 鱼体力耗尽或被猫端牵引越岸后由事件驱动 StateTree 进入；同一 Runner 继续按双端约束收近，水内贴水面、越岸后逐步贴地，且不再扣猫体力 |
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
| **R** | `IA_PutDownFishingRod` | **鱼竿一键三态**：已在操作容器→离开 / 公共交互锚点附近且容器有容量→追加加入 / 否则→放自己的竿 | ✅ C++ 已实现，服务器按事实自动分派 |
| **E** | `IA_Interact` | 准星交互/拾取；本地只选择 Current Target，真正拾取由服务器复核距离、视线和物品状态 | ✅ C++ 已实现，走 Native InputTag 而不是 Gameplay Ability |
| **左键** | `IA_LMB` | 无会话→**长按预览抛物线（不蓄力）松手抛竿**；真咬窗→**提竿**（1 秒内=完美）；遛鱼→**按住拖** | 提竿/拖 ✅ C++；**抛竿预览+提交走蓝图**（5.2） |
| **右键** | `IA_RMB` | 遛鱼时**按住松开线杯**（L_max 内确实自由出线时按配置恢复体力；线放尽重新绷紧后停止恢复） | ✅ C++（`UCatGA_FishingSlack`） |
| **Q** | `IA_BaitSpot` | **长按蓄力打窝**：抛物线越蓄越远，松手投出 | `UCatGA_FishingChum` → Pressed/Released → 服务器投放（5.3）；预览可接蓝图，不再另接投放 |
| **F** | `IA_CatchFish` | 抢抄 | ✅ C++ |
| **X** | `IA_CancelFishing` | 取消当前会话 | ✅ C++ |

`DA_CatAbilityInputConfig.AbilityInputActions` 是 **6 条**（5 个核心 + `Cat.Input.Fishing.Slack` → `IA_RMB`）；`DA_CatAbilitySet_Default` 相应 6 个 Ability。另有 `NativeInputActions`：`Cat.Input.Interact` → `IA_Interact`，它不授予第 7 个 Ability。

> Q 已由 GAS 的 `UCatGA_FishingChum` 处理蓄力按下和松开，正常投放会收到明确的成功/失败回执。若旧蓝图仍独立绑定 Q 并提交 `PlaceChum`，需在编辑器检查该图并迁移至 5.3 的单一投放入口，避免重复投放；不要用 UI 过滤失败来掩盖重复请求。本文不把未审计的二进制蓝图视为已经迁移。

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

多人占位口径：所有玩家都从同一个公共交互锚点按 R 加入，服务器把 PlayerState 追加到紧凑容器 `OperatorPlayerStates`。`[0]` 是当前主位，抛竿、提竿和右键线杯由它驱动；HookedFight 中左键立即提交收线/协作发力意图，不再蓄力或衰减。总做功按有效力量占比分摊到各自 ASC；每只猫以基础 `FishingStrength / StrengthPerKilogram` 得到等效质量，换算系数来自 `DA_FishingFightBalance_Default`，鱼使用本次实际重量。只有鱼的沿线加速度高于猫端合力时，差值才会牵引主猫。

会话跟随鱼竿而不是角色：按 R 离开不会直接结束会话。`HookedFight/ExhaustedReel` 离竿后进入无人值守 FreeSpool；下一位玩家使用自己的 ASC、当前力量、体力和输入序号接管同一个 Runner，不补满体力，也没有从 0 蓄力过程。接管后的玩法质量按该玩家基础力量和统一系数重算，不读取角色组件默认质量。

---

### 5.2 抛竿 —— 已由左键 C++ 接管，**不用做蓝图**（预览可选）

规格 3.1：点哪落哪、无蓄力。现在的实现：

- **左键按下**（无会话时）：服务器无副作用，回执 `RequestHook Committed=true`（= 开始瞄准）
- **左键松开**：服务器用你的**准星射线 ∩ 水面**算落点，自动填 RodActorId / Revision / Equipment Revision / WaterRegion Handle，走 `BeginCast` 全部校验（射程 ≤ min(竿线长, 漂抛距)、夹角 ≤ 60°、视线无遮挡）
- 前置：已按 R 进入鱼竿主操作位；没在操作时松开左键会被**静默忽略**（不刷日志）

结果日志：`Event=begin_cast_result Committed=true Landing=...` 或 `Committed=false Error=CastOutOfRange/InvalidWaterTarget/...`

**可选的客户端预览蓝图**（纯表现，不影响判定）：按住左键期间每帧调
先用 `UCatFishingAimLibrary::TryGetLocalCastViewRay` 取得鼠标射线（隐藏鼠标时取镜头准星），再调用 `ResolveCastAimPoint(Origin, Direction.Rotation()) → Landing`。松开时上传同一射线，服务器验证来源、朝向并重新求水面交点，随后校验遮挡与装备射程。不要使用角色眼睛位置替代第三人称镜头。当前浮漂上限：羽毛 10 米、毛线球 15 米、铃铛 20 米；有效距离取浮漂上限与鱼竿线长的较小值，入门竿线长为 15 米。

---

提竿成功上鱼后，主持杆玩家自动切到第一人称拿杆视角。镜头逐帧平滑追随实际杆；角力让杆停转时视角逐渐稳定，鼠标继续表达施力方向，反向回转或卸力后镜头开始跟随。前后左右移动以实际杆为准，镜头有短暂的跟随过渡。搏斗结束、离杆、断杆或失去角色控制后恢复原相机；协作者与旁观者的视角不切换。镜头构图可在项目设置 `Catfishing Fishing Presentation → Camera` 调整握把偏移和 FOV；`Fight Camera Follow Response Seconds` 默认 0.08 秒，越大越柔和、跟随延迟越明显。

### 5.3 打窝 —— 已由 Q 键 C++ 接管，**不用做蓝图**（蓄力预览可选）

规格 3.1 打窝：蓄力抛掷、抛物线预览。现在的实现：

- **Q 按下**：服务器记时刻
- **Q 松开**：服务器按按住时长算 `ChargeAlpha = clamp(held / ChumChargeMaxSeconds)`，初速 `Lerp(Min, Max, Alpha)`，用引擎 `PredictProjectilePath` 得到落点，选一份足量窝料实例（优先 `StarterChumDefinitionId` 对应实例），把 `ChumItemInstanceId` 交给 `PlaceChum` 做全部校验（射程/夹角/视线/库存/水域）
- 参数在 `Project Settings → Catfishing Fishing → Chum|Throw`：`ChumChargeMaxSeconds=1.5`、`Min/MaxSpeed=600/1400`、`Elevation=35°`、`ThrowQuantity=1`

结果日志：`Event=chum_throw Held=.. Alpha=.. Landing=.. ChumItem=..` + `Event=place_chum_result Committed=...`

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
         RodDefinitionId      = "StarterRodT1"
         BaitDefinitionId     = "BugBait"
         FloatDefinitionId    = "FeatherFloat"
         ScoopNetDefinitionId = "StarterScoopNet"     ← 第 4 个参数；当前临时测试路径在新角色占有时发一把，选择仍要求已有库存实例
```

**怎么知道成功了**：先准备好对应钓具的库存实例，再调用 `Server, Reliable` RPC。监听装备组件快照变化，核对 Rod/Bait/Float/ScoopNet 的定义与实例 ID 是否为本次选择；版本应与请求前的快照比较，不能把 `Revision=1` 当作装配成功，因为临时发网本身也会推进库存版本。相同装配的幂等请求可能不推进版本，应以当前选择实例为准。

> `RequestScoop` 仍要求服务器装备快照里存在有效 `ScoopNet`。当前启用独立临时测试开关 `bAutoGrantStarterScoopNet=True`：单人、房主和加入玩家在新 Character 被占有时，由服务器补齐一把正式 `StarterScoopNet` 并自动选中，占一个背包格；已有抄网不重复增加，同一角色重占有也不会再次补发。手工装配若把抄网参数留空仍会清除选择，应带上这份已有库存实例。测试时新开局后检查背包抄网及 F 的射程；日志用 `equipment_starter_scoop_completed` 和 `equipment_scoop_selection_replicated`，按 `ScoopNetItemInstanceId` 对照服务器与客户端。商店获取接通后关闭开关并删除临时来源，清理位置见 `FishingArchitecture_zh-CN.md` §2.5。

---

## 步骤 6：端到端测试

按顺序做，每步在 Output Log 里过滤对应关键字确认。

| # | 操作 | 期望日志 / 现象 |
|---|---|---|
| 1 | PIE 启动 | `Event=run_started` 且 `Event=run_phase_entered ... Phase=DayActive` |
| 2 | （自动）装配 | Equipment `Revision` 从 0 → 1，`RodDefinitionId = Rod_Basic` |
| 3 | 在任意合法地面第一次按 R | 世界里出现无人操作的 Rod Actor并播放放杆表现；角色不吸附、不锁移动 |
| 3.1 | 放置者再次按 R | 放置者进入右侧主位并开始操作，`OperatorPlayerStates.Num=1` |
| 4 | 主位仍有人时，第二个玩家走近同一个公共交互锚点按 R | 第二人追加为编号 1；两端都看到 `OperatorPlayerStates.Num=2` |
| 4.1 | 编号 0 的玩家按 R 离开 | 原编号 1 自动变为 0、按新编号重新站位并接管当前会话；若容器为空，搏斗才进入无人值守松线 |
| 5 | 瞄水面按住再松开左键 | `Event=fishing_phase_entered ... Phase=Waiting`，浮漂飞出去 |
| 6 | 等浮漂落水 | Hook 的 `BP_OnHookPresentationChanged` 收到 `Phase=Landed` |
| 7 | 等咬钩 | `Phase=Probe` → 紧接着 `Phase=TrueBiteWindow`，鱼 Actor 生成 |
| 8 | 3 秒内按住左键 | `Phase=HookedFight` |
| 9 | 持续按住左键收线 | Snapshot 里 `NormalizedFishStamina` 下降 |
| 10 | 鱼被收到面前（**搏斗中就可以**） | debug 里鱼身上的圈从红变绿 = 现在按 F 抄得到 |
| 11 | 对着鱼按 F | 不论鱼剩余体力，范围合法即直接变成嘴叼世界鱼；失败按同一 `RequestId` 查看 `scoop_target_* → scoop_rejected → fishing_scoop_terminal → fishing_command_result` |
| 12 | 或者等鱼翻肚，或用绷紧鱼线把鱼拖过岸线 | `Phase=ExhaustedReel`；日志出现 `fishing_fish_exhausted Cause=StaminaDepleted/ShoreLanding`，仍可按 F 抄，也可继续按住左键把鱼干沿地面拖近 |
| 13 | 鱼落到岸上后准星对准并按 E | 服务器只允许一个玩家成功叼起；随后再对具体地面鱼护按 E 才入箱 |

鱼生成时的大小由服务器随机重量决定：每个 `FishPresentation_*` 用自己的 `MeshReferenceWeightKilograms` 定义
`Scale=1` 的参考重量，再按体积关系取立方根并裁在本鱼的 `Minimum/MaximumUniformScale`。水中鱼和岸上拾取鱼
共用同一个复制缩放值；只在鱼种库直接引用的表现资产中调参数，不要在蓝图里再次随机 Scale。

**抄网范围**（详见 `FishingArchitecture_zh-CN.md` §2.5）：猫沿 `Character Actor Forward` 面朝正前方发一条水平线段，不读取 `Controller/Camera` 朝向；自由转动镜头不会改变挥网方向。线段与挂在鱼身上的圆相交即够得着，**纯俯视投影不看俯仰角**；高度差另由 `MaximumScoopVerticalDeltaCentimeters`（默认 250）卡上限。线段长 = `min(ini 的 ScoopReachCentimeters, 抄网 DA 的同名字段)`，圆半径 = 鱼 DA 的 `ScoopTargetRadiusCentimeters`（**为 0 则永远抄不到**）。

**咬钩要等多久**：默认 BugBait 下，无窝/一份新窝中心/五份新窝重叠中心的平均等待分别为 20/14/6 秒（落水到真咬，包含 1.5 秒预警）。单竿仍随机，慢浮至少 3 秒，总等待最多 40 秒。调参使用 ini 的 `NoChumMeanBiteDelaySeconds`、`SingleChumMeanBiteDelaySeconds`、`FullChumMeanBiteDelaySeconds` 并重启；均值必须大于慢浮加预警、按浓度非递增且小于总上限。

**日志过滤关键字**：`LogCatRun`、`LogCatFishing`、`LogCatEquipment`。多人差异先比较 `IsLocalController`、`NetMode`、`PawnLocation`；抄网站位再比较 `CenterWater*`、`FootWater*`、`GroundWater*` 的 `Error`、`Containment`、`VerticalDeltaCm` 与 `SignedShoreDistanceCm`。`FootWater`/`GroundWater` 是诊断对照，不代表当前服务器改成用脚底判定。

---

## 已知缺口

### ~~1. 抢抄需要 ScoopNet，但装配接口传不进去~~ ✅ 已修

`ServerConfigureEquipment` 现在提交 Rod/Bait/Float/ScoopNet 四个 DefinitionId，并可同时提交对应的四个 ItemInstanceId。库存 UI 应从当前格子带上实例 ID；旧调用没带实例 ID 时，服务器仍会按 DefinitionId 兼容解析一份可用实例。当前临时测试来源只在新角色占有时补齐 `"StarterScoopNet"`；装配 RPC 自身不发物品，没有已有库存实例时抄网选择仍拒绝。

### ~~2. 打窝需要窝料库存，但没有发放入口~~ ✅ 已修

数量型物品不再提供客户端直连发放 RPC；调试和正式链路都应通过服务器权威入口把物品写入统一库存格。

> 没有直接给组件方法加 `BlueprintCallable`，而是走 Controller RPC 转发 —— 和 `ServerConfigureEquipment` / `ServerRepairRodAtCamp` 保持一致的权限边界，避免任何蓝图都能直接摸到域写入口。

### 3. 窝料类型差异仍未配置

`UCatGA_FishingChum` 已负责 Q 按下/松开，服务器从库存选择窝料并调用 `PlaceChum`，不是占位能力。当前四种正式窝料的三轴贡献完全相同；Q 还会优先使用足量 BugChum。不同类型的筛选测试需要先确认实际投放的 Definition，再在测试资产上准备不同的三轴配方。正式配方调参尚未完成，详见下方测试方案。

---

### 抛竿飞行与首次等待

鱼钩按服务器冻结的抛物线连续飞到准确落点，客户端从复制的起始时间重建当前飞行位置。落水后恢复普通移动复制，不再朝水面直射后强制吸附。

`ScheduleWaitingProbeFromStateTree` 首次采样使用冻结的落水点，首次预警与咬钩计时包含剩余飞行时间。Development 落盘日志分类为 `LogCatFishing`，可用 `cast_aim_request`、`cast_range_rejected`、`cast_flight_started`、`cast_flight_landed`、`cast_flight_received`、`begin_cast_received` 对照请求、Session 和 CastAttempt；端到端验收需要同时检查房主与客户端日志。

---

## 窝料效果测试（2026-09-07）

本节区分当前代码事实、理论预测和待执行实验；不会把理论数值当成游戏实测。

### 当前到底有什么效果

正式四种 `Equip_Chum_{Bug,FermentedGrain,FruitFragrance,HolyLight}` 的影响半径基础值都为 300 cm，时长现为 180 s。全局 `InfluenceAreaMultiplier=8` 使实际半径约 8.485 m、直径约 16.971 m；相比原面积倍率 2，直径翻倍、覆盖面积变成四倍。中心初始贡献不因扩大面积而稀释，时间曲线按新时长拉长，因此固定实际距离/经过秒数上的贡献也可能增大。

当前四种配方都是 `(腥=1, 香=0.5, 酵=0.2)`。换名字不会换效果；圣光窝料也没有绕过巨鱼人数、挑战度、水域门的逻辑。

默认 BugBait 的咬钩频率倍率与最小等待倍率都为 1。按 `ACatFishingSession::ScheduleWaitingProbeFromStateTree`，令抛竿调度时冻结落点的三轴有效贡献总和为 C：

```text
目标均值 M(C)：(0,20秒)、(1.7,14秒)、(8.5,6秒) 之间分段线性插值；C>=8.5 饱和
先校准频率 λ，使 4.5 + (1-exp(-35.5λ))/λ = M(C)
落水至真咬 T = 3 + min(-ln(1-U)/λ, 35.5) + 1.5 秒，U 为随机数
鱼饵随后乘到 λ 和三秒慢浮下限；1.5秒预警和40秒上限保持
```

| 调度时的采样条件 | C | 理论平均等待 |
| --- | --- | --- |
| 无窝料 | 0 | 20 s |
| 一份新窝料中心 | 1.7 | 14 s |
| 两份新窝料完全重叠中心 | 3.4 | 12 s |
| 三份新窝料完全重叠中心 | 5.1 | 10 s |
| 四份新窝料完全重叠中心 | 6.8 | 8 s |
| 五份新窝料完全重叠中心及更高浓度 | ≥8.5 | 6 s |

这里的新窝料中心是理想条件，实际投放、瞄准耗时会使贡献开始衰减。单份比无窝平均少 6 秒（30%），五份比单份平均少 8 秒（约57%）。满窝指有效贡献达到 8.5，不是库存、同时投放数量或水域预算上限。新模型由 `Fishing/Simulation/CatFishingBiteTimingModel` 校准截顶后的真实均值，不能用 `1/M` 直接代替频率；它替换了旧的 `2-exp(-C)` 提速模型。

选鱼则在有效提竿时重新调用 `SampleChumAtPoint`：先按水域、挑战度、人数等筛候选并选择挑战档，再在该档内乘上窝料权重和鱼饵权重。正式曲线为 `1+2x`，`x=Affinity/(Affinity+10)`。一份新窝料对偏腥、偏香、偏酵三组鱼的权重倍率分别约为 1.197、1.119、1.077；偏腥相对偏酵的额外优势只有约 11.2%，不是增加 11.2 个百分点。最终鱼种占比还受基础权重、鱼饵、个体重量和挑战档影响。

### 先固定实验条件

- 使用同一地图和水域、同一准确落点、同一鱼饵/装备、同一猫力量和体力、同一在场人数；统计期间不吃鱼升级。时段/天气固定，并记录实际 gate 开关。
- 排除自然窝点和其他玩家打窝：`ConfiguredWeather=Rain` 可触发 `RainBloom` 的自然 BugChum；“我没按 Q”不等于落点没有窝。先检查 `natural_chum_terminal` 和圆环，在独立测试环境隔离该事件。
- 按 `chum_throw` 中的 `Chum` 和 `chum_field_prepared` 中的 `Definition` 确认真正投了什么。Q 优先用足量 BugChum；测试其他类型时让测试库存只含被测类型，或用已有显式 `SubmitPlaceChum` 携带对应实例/Definition。不要只看背包选中格。
- 每轮先打窝，再抛竿。等待调度只采样一次，中途补窝不会让已经排定的本次咬钩提前。过期回归也要在过期后重新抛竿，不能拿到期前已排好的计时器判断失效。
- 每轮控制窝料年龄和叠加数量。不能连续向同一个旧窝补一份，再把每次都标成“一份新窝料”；必须隔离旧场。手工测试可重进测试关卡，自动测试应每轮重新创建 World/场并使用指定服务器时间。

### A. 提速与衰减

先每组做 10 次操作检查，再用每组至少 60～100 次独立首次咬钩做初步统计。交错执行 A/B，或自动化使用同一组随机种子配对比较，避免时段、疲劳、随机序列恰好偏向某组。每竿仍存在随机波动，不能用三五竿断言均值是否达标。

| 组别 | 布置 | 观察和判据 |
| --- | --- | --- |
| A0 无窝 | 全新场，没有人工/自然窝覆盖落点 | 平均约20秒；建立均值、中位数、P90 和40秒截顶占比基线 |
| A1 单份中心 | 一份窝，固定投放至抛竿间隔，钩落中心 | 平均值应比 A0 低；核对下降幅度及置信区间，不要求每一竿更快 |
| A5 五份中心 | 五份同种窝近同时完全重叠，记录各自年龄 | 新窝理想均值6秒；手工顺次投放有年龄差，以日志有效贡献对应的均值为准 |
| A距离 | 单份，落点分别为中心、0.5R、0.99R、1.01R | 同年龄下浓度随距离下降；1.01R 重新抛竿应回到无窝基线。R≈8.485 m，边缘误差需用实际落点核对 |
| A时间 | 单份，在年龄 0、90、179、180、185 s 采样/重抛 | 曲线倍率约 1、0.6、接近 0、0、0；180 s 已无贡献，默认清理最多约晚 5 s，网络显示另有传输延迟 |
| A补窝时序 | 无窝抛竿后再补窝；与先补窝后抛竿对照 | 当前前者不重算本轮等待，后者才用于检验提速 |

时间指标统一为“落水成功 → 首次 `TrueBiteWindow`”，不要从按下抛竿、进入 Waiting、开始预警计时，也不要包含遛鱼/抄鱼。每轮计时后取消并开启新试验；漏按造成的第二次机会另记，不能与首次等待混合。配对自动化记录每个种子的差值和均值置信区间；一般随机人工样本用两组均值差的置信区间，不拿单竿输赢作判据。

### B. 筛选鱼类

先做内容前置检查：**现在直接拿四种正式窝料互相比，理论结果就应该相同**。如果同种子、同状态、同数量/年龄/位置下换类型，结果发生系统性差异，应先排查实际投放类型和测试污染。

要验证筛选算法本身，在独立测试夹具中准备总量相同的三种配方，例如腥 `(1.7,0,0)`、香 `(0,1.7,0)`、酵 `(0,0,1.7)`；这些是测试刺激，不是本轮写入的正式平衡值。先构造同一挑战档内、基础权重/体重/鱼饵倍率均相同、只在偏好上不同的候选，验证喜腥/喜香/喜酵鱼的**归一化概率**确实随配方切换。再使用正式鱼库复验，确保目标鱼没有被水域、人数或挑战度提前排除。

正式鱼库可观察：偏香组 `LittleColorFish/PetalFish/WindbellFish`，偏酵组 `LittleSilverFish/Loach/StinkyFish`；偏腥组包含 `SaltedFish/PufferFish/Blackfish`，但必须先核对其在该玩家状态下的候选资格和挑战档，不能把不同档强行当等权样本。

每组先做约 300 次选鱼作分布初筛；当前效应很小，定量确认建议自动化每组 5,000 次以上并报告各鱼种/偏好组占比、差值和 95% 置信区间，必要时增加样本。批量测试应直接调用生产 `UCatFishCatalogSettings::SelectRuntimeDefinition`，使用一组共同种子，固定其余上下文；不要只重写一份公式来证明生产选择器有效。

统计“成功提竿时选中的鱼”，不统计“最后抓到的鱼”。以 `fishing_fish_selection_resolved` 的 `Selected=true/FishId` 为准，失败选鱼单列。`NormalizedProbability` 是**所选挑战档内部**概率，不能当全鱼库概率；实测总占比须按所有成功选鱼统计。圣光窝料不会凭名字获得巨鱼特许。

### 日志和记录格式

Development 包无需加 `-log` 即应写入 `<打包根目录>/Catfishing/Saved/Logs`；编辑器在项目 `Saved/Logs`。正式联机验收分别保存房主和客户端的新日志。本轮尚未执行打包双端实验。

| 事件/字段 | 用途 |
| --- | --- |
| `chum_throw`、`chum_field_prepared`、`place_chum_result` | 实际 Definition、数量、落点、FieldId、半径、起止时间及提交是否成功；prepared 不能单独证明已提交 |
| `cast_flight_landed` 的 `Succeeded=1`、`Session` | 房主落水时间与实际落点；统一按服务器日志计算等待 |
| `fishing_phase_entered` 的 `Phase=TrueBiteWindow`、`SessionId` | 同 Session 第一次真咬时间；枚举文本可能包含类型前缀 |
| `fishing_bite_scheduled` 的 `SessionId/CastAttemptId/Opportunity/Seed` | 关联本轮三轴贡献、总贡献、落点、采样时间、目标/鱼饵后均值、频率、WaitSeconds、飞行时间和两个期限 |
| `fishing_bobber_mode_observed` | 房主与客户端各自观察到的模式变化、服务器模式起始时间；按同 SessionId 核对 Calm→BiteWarning→Sunk |
| `fishing_bite_schedule_rejected`、`fishing_bite_chum_sample_failed` | 调度拒绝原因或采样失败后按无窝回退；失败样本单独记录，不混作有效零浓度 |
| `fishing_fish_selection_resolved` | 提竿抽到的 FishId、有效候选数、档内概率、当时贡献场数量 |
| `natural_chum_terminal` | 识别自然窝料污染 |

建议每行一轮：`Group, Trial, SessionId, Seed(自动化), ChumDefinition, Quantity, DistanceCm, ChumAgeAtScheduleSec, LandedTime, FirstTrueBiteTime, WaitSec, FishId, SelectionSucceeded, ChumFieldsAtHook, Notes`。按试验条件额外记录水域、猫力量/体力、人数、鱼饵、天气和时段。

`fishing_bite_scheduled` 默认 Log 等级落盘，完整输出三轴采样和最终 `RatePerSecond`；按 `Opportunity=1` 对应首次等待。`ExpectedMeanSeconds` 是该次采样条件的分布均值，`WaitSeconds` 是实际抽出的等待时长，两者不能混淆。不能只凭 `ChumFields>0` 推断有效浓度大于零，因为半径边缘的衰减可以为零。

### 三层验收与迁移入口

- `contract`：四种正式资产时长 180 s、配置倍率 8、其他影响字段保持；原有面积倍率自动化检查属于此层。范围/时长迁移入口是 `Scripts/update_chum_duration.py`，只接受旧值 60 或新值 180，多次执行不重复加 120；带 `-VerifyChumTuning` 时只读取验证。
- `runtime_behavior`：生产采样的距离/年龄/重叠/到期边界、首次等待对照、正式选鱼分布、客户端投放只扣一次，以及双方看到同一场的起止事实。新进程加载资产不能替代这些行为证据。
- `presentation_delivery`：实际地图上约 16.97 m 的圆环与有效覆盖吻合、180 s 后正确消失、两端一致；正式 WBP 和整体模块交付仍按对应模块验收。

迁移和只读重载示例（先关闭会保存这些资产的编辑器实例，避免旧内存值覆盖新文件）：

```powershell
& D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe D:/develop/Catfishing/Catfishing.uproject -run=pythonscript -script=D:/develop/Catfishing/Scripts/update_chum_duration.py -unattended -nop4 -NullRHI -nosound -DDC=InstalledNoZenLocalFallback
& D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe D:/develop/Catfishing/Catfishing.uproject -run=pythonscript -script=D:/develop/Catfishing/Scripts/update_chum_duration.py -VerifyChumTuning -unattended -nop4 -NullRHI -nosound -DDC=InstalledNoZenLocalFallback
```

两次均应输出四行 `CHUM_TUNING_VERIFIED` 和 `CHUM_TUNING_PASS`，第二次 Mode 为 VerifyOnly。实际引擎/项目位置不同可替换路径。

### 范围/时长调整的影响盘点（先前检查点）

修改前已在对话列出下表；当前补记实际结果。基线为四种正式窝料 300 cm 基础半径、60 s、同配方，配置面积倍率 2；工作区原有鱼竿/角色/鱼钩/会话等并行改动未纳入本次修改。证据根目录为 `Saved/Automation/ChumTuning`，本轮不新建人工进度入口。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 输入/扣量 | `Source/Catfishing/AbilitySystem/Fishing/InputAbilities/CatFishingChumAbility.cpp` → `Fishing/Integration/CatFishingCommandComponent.cpp::ThrowChumFromChargeOnAuthority` → `Environment/CatChumPlacementService.cpp::PlaceChum` | Q 蓄力、BugChum 优先、权威事务扣一份不变 | 保留源码入口；纠正文档中的占位说法 | 原调用链直接读取新配置/资产 | 重复请求/失败扣量需游戏实测；不涉及新增持久化 | 源码未改；`BP_GA_Chum` 的注册表引用包含 `/Game/Data/Abilities/DA_CatAbilitySet_Default`，未删除能力资产 |
| 半径 | `Config/DefaultGame.ini` 的 `CatChumFieldSettings.InfluenceAreaMultiplier` → `Environment/CatChumFieldSubsystem.cpp::PrepareField` | 面积倍率 2→8；直径约 8.49→16.97 m，基础半径仍 300 cm | 原配置改值；沿用 sqrt 换算和一次性缩放 | 接收端已有半径字段，无结构迁移 | 单位混淆；配置重载及已有面积换算 Automation | `VerifyConsumers.log` 读取倍率 8，四种实际半径 848.528137 cm；`Contract/index.json` 1/1 Success |
| 时长/生命周期 | `/Game/Catfishing/Data/Equipment/Equip_Chum_{Bug,FermentedGrain,FruitFragrance,HolyLight}` 的 `ChumInfluence.DurationSeconds` → `PrepareField/ActivatePreparedFieldDeferred` | 60→180 s，按新生命周期归一化时间衰减 | 原字段迁移；`Scripts/update_chum_duration.py` 检查旧值并防止重复加时 | 全量预检→保存四资产→新进程只读重载 | 原配方/曲线/数量/表现引用保留 | `Migrate.log` 保存通过；`VerifyConsumers.log` 四资产 180 s 重载通过 |
| 自然窝与清理 | `Framework/Game/CatGameplayTypes.cpp::SubmitNaturalChumFieldIfConfigured` 引用 BugChum；`Environment/CatChumFieldSubsystem.cpp::CleanupExpiredFields/Deinitialize` | 同样扩大/延长；到期停止采样、默认 5 s 清理不变 | 保留共用实现 | 使用同一资产和到期时间 | 自然场污染基线、清理与采样时间差 | 源码链已核对；实际 180 s 到期和双端消失尚未验证 |
| 提速/选鱼 | `Fishing/CatFishingSession.cpp::ScheduleWaitingProbeFromStateTree/ResolveHookSelectionFromAuthority` → `Data/CatFishCatalogSettings.cpp::SelectRuntimeDefinition` | 算法不改；区分三轴影响值与尚未实现的鱼量密度 | 原实现保留，增加本节实验设计并更新 `Docs/FishingCoreFlow_zh-CN.md` | 先独立测有无窝，再以不同配方测选鱼 | 调度快照、挑战档、鱼饵偏好、抓获幸存偏差 | 资产审计确认同配方；数值为理论预测。正式配方差异及统计实测未完成，挂原模块 |
| 复制/表现/日志 | `Environment/CatChumFieldReplicationComponent.cpp::ReconcileFieldFromAuthority` → `Environment/Presentation/CatChumFieldPresentationActor.cpp::ApplyPublicState`；既有 `chum_field_prepared/place_chum_result` | 半径、起止秒数共用服务器事实；事件不改 | 保留字段、日志及 BP 回调，不增加第二套状态 | 复制新半径/起止值后重建圆环 | 正式 WBP/BP 外部消费者未完整审计；打包双端表现未验证 | 源码投影已核对；无新网络格式或 C++ 改动；实测留待本节方案 |
| 配置/目录/Cook/旧路径 | `CatEquipmentSettings.Definitions`、`CatShopEconomySettings.DefaultShopCatalogTable`；`Scripts/verify_equipment_shop_runtime.py`；两份现有 Fishing 指南 | 正式四资产原路径继续生效，清除文档旧 Q 入口/已实现鱼群模型的误导 | 复用原定义/商店校验函数；不新增 Cook 路径 | 参数迁移后核对消费者及最终 diff | `/Game/Data/Equipment/DA_Chum_Basic` 未在正式目录；注册表直接引用为空，但未做加载全部地图/BP 的引用确认 | `VerifyConsumers.log` 中 Definitions=4、ShopRows=4 PASS。旧 Basic 二进制暂留原路径且仍为旧值，待编辑器完整引用审计后安全删除，不作为调参入口；完整商店检查因既有 PlayerStart 断言失败，见 `VerifySaved.log` |

该范围/时长检查点未重编译、未 Cook/打包，仅调整资产和配置。资产重载与 Automation 仅属于 `contract`，不代替上述 `runtime_behavior` 或 `presentation_delivery` 待验项。

### 20/14/6秒等待调整的影响盘点与交付核对

本次在先前范围/时长检查点后实施。修改前 `Fishing.Settings` 4/4 通过（`Saved/Automation/ChumBiteTiming/BaselineReport/index.json`）；资产注册表扫描376个 Blueprint，未发现 `CatFishingSettings` 派生资产（`AssetAudit.log`）。旧频率字段未暴露 Blueprint 读取，源码/配置入口已核查；未修改正式 WBP/BP 的资产结构。工作区抄网、角色移动、搏斗模拟器与相关测试属于并行改动，本次不纳入提交。下表源码路径均相对 `Source/Catfishing/`，证据相对 `Saved/Automation/ChumBiteTiming/`。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 等待入口与状态 | `Fishing/CatFishingStateTreeNodes.cpp::FCatFishingScheduleWaitingProbeTask` → `CatFishingSession.cpp::ScheduleWaitingProbeFromStateTree` | 保留首次抛竿/漏按重试各一次调度、服务器权威、机会种子与阶段顺序 | 原入口调用新 `Fishing/Simulation/CatFishingBiteTimingModel` | 先准备模型与配置，再切换唯一生产入口 | 冻结落点、剩余飞行时间、重试种子 | `FishingReport/index.json` 的 `WorldFieldsDriveFormalStateTreeAndBobber` 通过；真实正式树及 Hook BP 跑通三组等待/重试 |
| 核心计算 | 原 Session 内联 `2-exp(-C)` 与截顶指数采样 | 原单份后过早饱和；改为有效贡献0/1.7/8.5对应20/14/6秒均值；三秒慢浮+1.5秒预警、40秒截顶 | 新模型分段线性目标、数值校准截顶分布；保留每次一个随机数，无第二份玩法状态 | 模型输出分布→Session采样→原两个计时器 | 均值、连续性、单调性、饱和、非法值、鱼饵单位 | 三项模型测试通过；0/1/5份各十万次均值20.062694/14.033983/6.006305秒；旧内联公式删除 |
| 配置与单位 | `Fishing/CatFishingSettings.h/.cpp`；`Config/DefaultGame.ini` 的 `/Script/Catfishing.CatFishingSettings` | 旧基础频率0.2/s改为三个 MeanBiteDelaySeconds；慢浮5→3秒、上限15→40秒，预警1.5秒不变 | 新增两个贡献锚点和三个均值；`TryGetBiteTimingParameters` 统一校验 | 同步切换读取方与 ini，移除旧频率属性/键 | 非法/不可达均值拒绝；保留其他 Settings 契约 | `Fishing.Settings` 4/4通过；原默认值依赖测试显式指定五秒夹具；源码/配置/脚本无旧字段消费者 |
| 窝料与鱼饵 | `Environment/CatChumFieldSubsystem.cpp::SampleChumAtPoint`；正式 `Equip_Chum_*` 和 `Equip_Bait_Bug` | 保留半径、180秒时长、三轴、距离/时间衰减；鱼饵仍缩放频率与慢浮下限 | 模型先校准中性频率，再施加原倍率；采样仍只在机会开始时进行 | 真实场提供贡献→模型→调度 | 中心叠加、范围内边缘、90秒衰减、180秒到期 | World测试验证真实0/1/5场、八米处贡献、90秒变淡和180秒归零；首次调度用冻结落点，无新资产或迁移脚本 |
| 选鱼、库存、持久化 | `CatFishingSession.cpp::ResolveHookSelectionFromAuthority` → `Data/CatFishCatalogSettings.cpp::SelectRuntimeDefinition`；原 Equipment 预约/扣量 | 提竿时才选鱼、原配方和扣量契约不变；不涉及新持久化写入 | 保留原链路，新模型仅输出时间分布 | 先等口，合法提竿仍走唯一选鱼/扣饵入口 | 不能提前选鱼；正式鱼种占比另验 | 源码调用链保持；World测试在真咬窗口仍无鱼身份/Actor。四配方同值和正式选鱼分布验收缺口保留 |
| 复制、浮漂、日志 | Session计时器→`Fishing/Actors/CatFishingHookActor.cpp::SetBobberPresentationModeFromAuthority/DispatchPresentationChanged`→原复制表现 | 保留完整预警与下沉，客户端不重算咬钩随机数 | 增加一次性 `fishing_bite_scheduled` 和状态变化 `fishing_bobber_mode_observed`；复用原复制字段 | 同一分布结果写日志，原 OnRep/权威通知共用模式日志 | 双端时钟/可见表现；不逐帧刷日志 | `FishingTests.log` 默认落盘有浓度、种子、时间与稳定SessionId；World验证1.5秒预警和Sunk。打包双端/UI视觉实测未完成 |
| 失败与退出 | `CatFishingSession.cpp::FinalizeSession/EndPlay` 与调度器失败分支 | 保留退出停计时器；采样失败继续按无窝回退且明确报警 | 原清理路径保留，新增两个失败事件和Reason/Error | 不增计时器、复制字段或资源写口 | 非法参数、到期重试、取消清理 | 模型非法输入测试及World取消后的三个计时器清理通过；失败事件分支已核查，未声称双端故障注入通过 |
| 文档、脚本、Cook、检查 | `Fishing/Tests/CatFishingSettingsTests.cpp`；本指南、CoreFlow、Architecture、BlueprintSetup、`Docs/StateTreeTutorial_zh-CN.md` 故障表和唯一差距清单 | 旧5/15秒与频率调参说明失效 | 同轮改正说明；新增模型与World回归；不涉及新增Cook/打包入口 | 编译→定向测试→Fishing回归→独立检查点 | 编译/自动化不等同正式资产和双端交付 | `EditorBuildFinal.log`、`GameBuildFinal.log` 均Succeeded；`FishingReport/index.json` 共141项，137通过、4失败，详见下段；未Cook/打包 |

三层结论：`contract` 的四项新测试及四项 Settings 测试全部通过，Editor/Game Win64 Development 编译通过；`runtime_behavior` 已验证受控World中真实窝料场到正式StateTree/Hook的等待、预警、下沉、重试和取消；尚未验证玩家输入/库存完整事务、正式鱼种统计及打包联机链路。`presentation_delivery` 的实际地图圆环、客户端浮漂、正式WBP/UI及整套模块仍未验收，本局部检查点不关闭模块。

完整Fishing回归的四项失败为：`Assets.StarterRodPreservesMaximumDurabilityBaseline`（测试要求150，资产返回500）、两个 `Service.BrokenRodPack*`（`grants two bait uses` 失败）、`Simulation.EqualStrengthNaturallyStalematesWithoutCarrierPull`（预期0，实际160）。这些资产/库存/搏斗用例与代码未在本次修改；未单独重建旧版本复验其基线，因此保留完整失败证据，不将全套回归描述为通过。四项通过但带警告的测试中，新World测试的三条警告来自既有取消终态日志等级。

## 当前完成度

### ✅ 已完成

**C++**（已编译验证通过，`Result: Succeeded`）

| 文件 | 改动 |
|---|---|
| `CatFishingCommandComponent.cpp` | `HandleAbilityCommandFromAuthority` 新增 OperateRod / 搏斗收线 / Scoop 三条分支 |
| `CatGameplayTypes.h` | `ServerConfigureEquipment` 加 `BlueprintCallable`，并支持随 DefinitionId 提交 ItemInstanceId |
| `CatEquipmentComponent.h` | `GetSnapshot()` 加 `BlueprintPure`，快照包含当前选择的实例 ID |
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
CatFishEncounterActor（原生运行类；鱼种库直连 Mesh/ABP）
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

- 本节只记录 2026-08-19 的 PIE 检查点。该检查点的运动方案已被替换；当前运动、收放线入口与诊断按 [鱼运动与遛鱼逻辑实现导读](FishFightImplementationGuide_zh-CN.md) 阅读，不能据本节重建旧判定表。
- `DA_CatAbilityInputConfig` / `DA_CatAbilitySet_Default` 各 6 条（含 Slack）；PIE 中猫 `FishingStrength=50`、Chum runtime=True、规格系数已加载
- `UCatFishingViewBridge` 已暴露蓝图（`CreateFishingViewBridge` / `FindFishingSessionForPlayerState` / `BindSession` / `OnViewStateChangedBP`），`ACatFishingSession::GetReplicatedSnapshot()` 可读
- 窝点表现类可配：`[CatFishingPresentationSettings] ChumFieldPresentationClass=`

### ⬜ 待办（你）

1. 表现：Rod/Hook 蓝图继续实现 `BP_On*PresentationChanged`；鱼 Mesh/骨骼/AnimBP/动画只在 `Fish_* → FishPresentation_*` 直连资产中维护；新建 `BP_CatChumFieldPresentation`（父类 `CatChumFieldPresentationActor`）并把类路径写进 ini
2. `BP_CatFishingController`：5.4 ConfigureEquipment（4 参数）→ `Server Grant Run Consumable` 发窝料 → 5.1 接 E 键结果缓存 → 5.2 左键长按预览+抛竿 → 5.3 Q 蓄力+打窝
3. （可选）HUD：用 ViewBridge 订阅会话状态

### ⬜ 待办（我）

- ~~C++ 调试可视化~~ ✅ 已完成：`cat.Fishing.Debug 0/1/2`（默认 0；0=世界标记全关，1=全量，2=只留抄网射线+鱼圈+鱼线+阶段提示）；右上角当前鱼种 ID 与鱼/竿/猫体力、耐久和力量面板由独立的 `cat.Fishing.Stats 0/1` 控制（默认 1，自动显示，可手动关闭）。鱼竿统一显示跨场累计的 `ROD Durability`，没有独立鱼线耐久条；新一场不会补满，耐久归零后坏竿不能再抛，维修正式交互的验收仍以模块进度入口为准。
- 浮漂弹道修正（现在飞行轨迹落不到目标点会"瞬移"）
- 规格后续：抄网道具化/概率/硬直/无网拾取/翻肚 30s；窝料接入水域面积/鱼量账本、平均分布、共享重叠收敛曲线与面积容量上限；浮漂级计时器读取所在面积单元聚鱼总量；浮漂精准偏移；入夜停咬

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
