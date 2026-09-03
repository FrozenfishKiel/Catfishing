# 钓鱼 MVP 蓝图接线指南

本文面向：C++ 权威链已经写完之后，需要在 UE Editor 里把「打窝、钓鱼、抄鱼」跑通的人。内容分三层：

1. **本轮已经在 C++ 补上的东西**（你不用再写代码，但要知道它们现在能做什么）
2. **纯资产/配置工作**（DataAsset、StateTree、`DefaultGame.ini`，不涉及蓝图节点图）
3. **必须手写蓝图节点图的地方**（这是本文重点，逐个给出输入、节点链和陷阱）

---

## 0. 先看清楚：五个原生 Ability 的真实状态

项目在 `AbilitySystem/CatFishingAbilities.h` 里已经写好五个 `UGameplayAbility` 原生子类，`UCatAbilitySet::IsRuntimeReady()` 强制要求它们对应的五个 InputTag 必须齐全。它们各自的完成度不同，**这是本文最重要的一张表**，决定了你还要不要为对应功能另写蓝图：

| InputTag | Ability 类 | 服务器命令 | 状态 |
|---|---|---|---|
| `Cat.Input.Fishing.RodInteract` | `UCatGA_FishingRodInteract` | OperateRod | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Primary`（按下） | `UCatGA_FishingPrimaryAction` | RequestHook / 搏斗中收线 | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Primary`（松开） | 同上 `InputReleased` | PrimaryReleased / 停止收线 | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Cancel` | `UCatGA_FishingCancel` | CancelFishing | ✅ 一直可用 |
| `Cat.Input.Fishing.Scoop` | `UCatGA_FishingScoop` | RequestScoop | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Chum` | `UCatGA_FishingChum` | PlaceChum（**无payload**） | ⚠️ **占位符，永远会被服务器拒绝** |

**关键结论**：抓竿互动、提竿/收线、取消、抢抄这四个动作，装好 GAS 资产、绑好输入键位之后**不需要再写任何蓝图逻辑**，直接能跑。真正需要你写蓝图节点图的，是下面这几件事：

- **PlaceRod（放竿）**：完全没有原生 Ability，五个 Ability 里没有它
- **BeginCast（抛竿）**：同上
- **PlaceChum（打窝）**：`Cat.Input.Fishing.Chum` 这个键位必须绑（否则 AbilitySet 校验不过），但它背后的 `UCatGA_FishingChum` 只会发一个空命令，永远失败——**真正的打窝要另开一条路**，不走这个键位对应的 Ability
- **ConfigureEquipment（首次装配鱼竿/饵/浮漂）**：没有 Ability，且这是钓鱼链路最上游的前置条件

这四个是本文第 3 部分的重点。

---

## 1. 本轮 C++ 改动清单（供你确认代码已同步）

| 文件 | 改动 |
|---|---|
| `Fishing/Integration/CatFishingCommandComponent.cpp` | `HandleAbilityCommandFromAuthority` 新增 OperateRod（服务器自动找“我部署的竿”）、搏斗中 Primary 按下=收线/松开=停止收线、Scoop（服务器找范围内已上钩鱼并直接嘴叼）三条分支 |
| `Framework/Game/CatGameplayTypes.h` | `ServerConfigureEquipment` 加 `BlueprintCallable`，蓝图现在能直接调用它提交装备定义和实例 ID |
| `Equipment/CatEquipmentComponent.h` | `GetSnapshot()` 加 `BlueprintPure`，蓝图能读当前 `Revision`、装备 DefinitionId 和对应 ItemInstanceId |
| `Character/CatCharacter.h` | `GetEquipmentComponent()` / `GetConditionComponent()` 加 `BlueprintPure` |
| `Environment/CatWaterRegion.h` | `GetWaterRegionHandle()` / `HasValidBakedGeometry()` 加 `BlueprintPure`，蓝图能从关卡里放置的湖 Actor 直接拿到抛竿/打窝要用的 `FCatWaterRegionHandle` |
| `Fishing/Actors/CatFishingHookActor.h/.cpp` | 浮漂现在会在权威落点用有界轮询计时器（不是 Tick）确认落水，`Phase` 会真正走到 `Landed`，蓝图的 `BP_OnHookPresentationChanged` 能收到正确通知 |

如果你在自己机器上拉了最新代码却发现这些函数没有 `BlueprintCallable`/`BlueprintPure`，说明改动没同步，先确认代码状态再继续。

**一个已知的小瑕疵，不影响功能，先告诉你**：`ScheduleWaitingProbeFromStateTree`（等口阶段）里第一次采样窝料浓度时，用的是钩子刚生成时的位置（鱼竿竿尖），而不是最终落水点，因为这一步比 `BeginAuthoritativeFlight` 先跑。这只影响**第一口的咬钩延迟计算精度**，不影响后续选鱼（选鱼发生在 Probe 事件触发时，那时钩子已经落水）。如果你们后续要精确调打窝手感，这是一个可以优化的点，但不阻塞 MVP。

---

## 2. 纯资产/配置工作（没有蓝图节点图，但必须做）

### 2.1 DataAsset 清单

**已经全部创建并填好数值**（本轮用 Python 批量生成，两个 `IsRuntimeReady()` 校验规则已逐条比对通过）：

| 资产 | 路径 | 关键内容 |
|---|---|---|
| `DA_CatAbilityInputConfig` | `/Game/Data/Abilities/` | `AbilityInputActions` 保存钓鱼 GAS 映射；`NativeInputActions` 额外保存 `IA_Interact` → `Cat.Input.Interact` |
| `DA_CatAbilitySet_Default` | `/Game/Data/Abilities/` | 5 个原生 Ability 类；Primary=`WhileInputActive`，其余 `OnInputTriggered` |
| `Equip_ScoopNet_Starter` | `/Game/Catfishing/Data/Equipment/` | 正式目录抄网定义 `StarterScoopNet`；当前由开发期开关默认发放，正式获取接入后关闭该开关 |
| `Fish_*` | `/Game/Catfishing/Data/Fish/` | 16 条正式鱼定义；Showcase2 已使用 `RegionId=River`，按生态条件与连续挑战度从该目录选择 |
| `Bite_*` / `Fight_*` | `/Game/Catfishing/Data/Fish/` | 正式咬钩与搏斗性格；由选中的 `Fish_*` 稳定 ID 解析 |
| `Curve_ChumDistanceFalloff` / `Curve_ChumTimeFalloff` | `/Game/Data/Curves/` | 1→0 线性衰减 |

两个最容易踩的校验坑（生成时已规避，改数值时注意别破坏）：

- **Gameplay Tag 前缀是 `Cat.`** —— `CatFishingAbilityTags.cpp` 里真实定义是 `Cat.Input.Fishing.Primary`，不是 `Input.Fishing.Primary`
- **Chum 的两条 Curve 不能为空，且 `v(0)` 必须 > 0** —— `BakeCurve` 拒绝空曲线，`FCatChumFalloffTable::IsRuntimeReady()` 额外要求首采样点为正值

### 2.2 `Config/DefaultGame.ini`

**已经写好并落盘了**，8 个 section 全部指向 2.1 节创建好的资产。只剩两处 StateTree 软引用需要你建完资产后补上：

```ini
[/Script/Catfishing.CatRunSettings]
RunFlowStateTree=/Game/.../ST_RunFlow.ST_RunFlow                        ; ← 待补

[/Script/Catfishing.CatFishingSettings]
FishingSessionStateTree=/Game/.../ST_FishingSession.ST_FishingSession   ; ← 待补
```

已填好的 section：`CatEnvironmentSettings`、`CatEquipmentSettings`、`CatFishCatalogSettings`、`CatItemsSettings`、`CatAbilitySettings`、`CatFishingPresentationSettings`、`CatFishingSettings`、`CatRunSettings`。

**改完 ini 必须重启 Editor** —— `UDeveloperSettings` 只在启动时读一次配置，运行中改 ini 不会热加载（本轮验证过：写完 ini 后在运行中的编辑器里读 CDO，全部还是默认值）。

任何一项没填，对应链路会在 `IsRuntimeReady()` 直接 fail-closed，不会弹窗报错，只在日志里安静拒绝命令——排查时先查这里。

### 2.3 关卡里的湖（前置于一切）

1. 放 `ACatWaterRegion`（或其蓝图子类 `BP_CatWaterRegion`），设置 `WaterSurfaceZ`
2. 放若干 `ACatWaterBoundarySplineActor` 圈出湖岸线，**每条样条 Actor 自己身上的 `OwningRegion` 必须手动指回这个 Region**（最容易漏的一步）
3. Region 的 `BoundaryActors` 数组里把这些样条 Actor 都拖进去
4. 点 `BakeGeometry`（Details 面板按钮）——它只写内部数据，**视口不会有任何变化**，去 Output Log 确认 `BakeGeometry Successful`，并看 `GeometryRevision` 属性从 0 变成非零
5. 进 PIE 运行起来看 Output Log 有没有 `StaleGeometry` 报错

### 2.4 StateTree 拓扑（简要重述，细节见前文对话）

- **ST_RunFlow**（Context = `ACatfishingGameModeBase`）：`DayActive → NormalNight/FailureNight → Ending → Ended`，事件只有 `Cat.Run.QuotaReached/QuotaFailed/AllEligibleReady/SettlementComplete`
- **ST_FishingSession**（Context = `ACatFishingSession`）：`Waiting → Probe → HookedFight → ExhaustedReelHold`；鱼体力耗尽时发送 `Cat.Fishing.FishExhausted`，叶子的 `Enter Phase(ExhaustedReel)` 只切生命周期，同一个 Runner 继续运行。**树永远不能自然结束**，`Resolved`/`Terminated` 只能由 C++ 写。

---

## 3. 必须手写的蓝图节点图

这四件事全部走 `UCatFishingCommandComponent` 上现成的 `BlueprintCallable` 函数，**不需要新建 GameplayAbility**，挂在 Character 或 PlayerController 蓝图的一个普通 Enhanced Input 绑定上就行（和上面五个 GAS Ability 走的是两条不同的输入通道，互不干扰）。

拿命令组件的通用第一步：

```
Get Controller (Cast to ACatfishingPlayerController)
  → Get Fishing Command Component   ← 已是 BlueprintPure
```

### 3.1 PlaceRod（放竿）

触发时机：玩家手上没有已部署的竿，按下"放竿"键。

```
Get Player Character → Get Equipment Component → Get Snapshot   ← Revision
Make FCatPlaceRodCommand
    RequestId = Make Guid (New Guid)
    ExpectedEquipmentRevision = Snapshot.Revision
→ FishingCommandComponent.Submit Place Rod (Command)
```

- 结果通过 `OnResultReceived`（`BlueprintAssignable` 委托）回调，或用 `TryGetResult(RequestId, OutResult)` 轮询
- 成功后 `FCatFishingCommandResult` 里的 `RodActorId` / `RodActorRevision` / `EquipmentRevision` **要缓存下来**，BeginCast 要用
- 失败常见原因：站的地面坡度不够平（服务器要求法线 Z ≥ 0.7）、站在水里（会判 `InvalidWaterTarget`）、已经有一根部署中的竿（`ActiveSessionExists`）

### 3.2 OperateRod（走近操作）

**不需要写蓝图**——`UCatGA_FishingRodInteract` 已经原生实现，只要 `Cat.Input.Fishing.RodInteract` 绑好键位、Character 站在 `StandAnchor` 250cm 范围内按键即可，服务器会自动找到"我部署的那根竿"并把角色吸附过去。

### 3.3 BeginCast（抛竿）

触发时机：玩家已经是竿的 Operator（`OperateRod` 成功之后），瞄准水面按下"抛竿确认"键。

```
Line Trace（从摄像机沿准星方向），命中点作为 CandidateWorldPoint
Get 关卡里放置的 ACatWaterRegion 引用 → Get Water Region Handle   ← 现在是 BlueprintPure
Get Player Character → Get Equipment Component → Get Snapshot   ← Revision/RodDefinitionId 等

Make FCatBeginCastCommand
    RequestId = New Guid
    RodActorId = （PlaceRod 结果缓存的值）
    ExpectedEquipmentRevision = Snapshot.Revision
    ExpectedRodActorRevision = （PlaceRod/OperateRod 结果缓存的 RodActorRevision）
    ClientCandidateWorldPoint = Trace 命中点
    ExpectedWaterRegionHandle = Region.GetWaterRegionHandle()
→ FishingCommandComponent.Submit Begin Cast (Command)
```

- 服务器会**用自己的 `ResolveCandidatePointToWater` 修正落点**，客户端给的只是"建议"，不用做客户端预校验
- 服务器另外校验：距离 ≤ `min(竿最大线长, 浮漂最大抛距)`；准星方向和落点方向夹角 ≤ 约 60°（`Dot ≥ 0.5`）；视线无遮挡
- 结果 `FCatBeginCastResult` 通过 `TryGetBeginCastResult(RequestId, OutResult)` 拿，里面有服务器修正后的 `ServerCorrectedLandingWorldPoint` 和 `Command.FishingSessionId`——**这个 SessionId 只用于你自己 UI 显示，不需要传给后续任何命令**，因为 Hook/Reel/Cancel/Scoop 全部由服务器通过 `TryGetActiveSessionForController` 自动定位你的活跃会话

### 3.4 RequestHook / 收线 / Cancel

**不需要写蓝图**——三个正式原生 Ability 已经覆盖：

- `Cat.Input.Fishing.Primary` 按住不放：`TrueBiteWindow` 阶段=提竿判定，`HookedFight` 阶段=持续收线（哪个阶段由服务器读当前 Session Phase 决定，蓝图端不用关心）
- 松开：停止收线（或阶段外的无害 no-op）
- `Cat.Input.Fishing.Cancel`：随时取消当前会话
- `Cat.Input.Fishing.Scoop`：鱼上钩后即可使用，不读取鱼的剩余体力；服务器范围校验成功后直接进入与岸上死鱼按 E 相同的嘴叼状态，不在钓鱼会话里指定鱼护

### 3.5 PlaceChum（打窝）—— 单独走一条路，不挂在 `Cat.Input.Fishing.Chum` 键位对应的 Ability 上

`UCatGA_FishingChum` 只是为了让 `UCatAbilitySet::IsRuntimeReady()` 校验通过而存在的占位符（它发的是一个不带载荷的命令，服务器永远会拒绝）。**真正的打窝逻辑要单独绑一个输入**（比如做一个"打窝"UI 按钮，或者另一个准星确认键），直接调 payload 版本的函数：

```
Line Trace 拿目标水面点
Get 关卡 ACatWaterRegion → Get Water Region Handle
Get Player Character → Get Equipment Component → Get Snapshot   ← Revision

Make FCatPlaceChumCommand
    RequestId = New Guid
    ExpectedWaterRegionHandle = Region.GetWaterRegionHandle()
    ExpectedEquipmentRevision = Snapshot.Revision
    ChumDefinitionId = （玩家当前选择的窝料 ID，需要你自己的库存/选择 UI 提供）
    Quantity = 1（或 UI 里选的数量）
    ClientCandidateWorldPoint = Trace 命中点
→ FishingCommandComponent.Submit Place Chum (Command)
```

- 结果用 `TryGetPlaceChumResult(RequestId, OutResult)` 拿
- 窝料本身需要先进玩家的统一库存格；正式来源走商店订单或服务器权威授予链路，临时调试也应调用同一套库存授予接口，避免再加客户端直连发放入口。

### 3.6 ConfigureEquipment（首次装配）—— 必须最先做，否则后面全部走不通

`FCatEquipmentLoadoutSnapshot` 初始是空的（`RodDefinitionId`/`BaitDefinitionId`/`FloatDefinitionId` 全是 `NAME_None`），`PlaceRod`/`BeginCast` 都会因为 `Kind` 校验失败而拒绝。玩家进图后第一件事必须是装配：

```
Get Player Character → Get Equipment Component → Get Snapshot   ← 初次是 Revision=0

Controller.Server Configure Equipment(
    RequestId = New Guid,
    ExpectedRevision = Snapshot.Revision,   // 首次是 0
    RodDefinitionId = "你的Rod DataAsset的稳定ID",
    BaitDefinitionId = "...",
    FloatDefinitionId = "..."
)
```

- 这是个 `Server, Reliable` RPC，没有直接的成功/失败回调结构体传回客户端——**成功与否要靠 `UCatEquipmentComponent` 的 `OnSnapshotChanged`（复制驱动）或直接监听 `Get Snapshot` 的 `Revision` 是否变化来判断**
- 建议做法：进图 BeginPlay 时（或一个"装备"菜单确认按钮）调用一次，然后在 Character/PlayerState 的 Tick 或 Snapshot 变化事件里检查 `RodDefinitionId != NAME_None` 作为"已装配完成"的信号，再解锁"放竿"按钮
- 只在库存为空（`Snapshot.RodDefinitionId.IsNone()`）时允许调用；重复调用会因为"同一套新 Request 只读取既有耐久"规则被拒绝换装

---

## 4. 端到端测试清单

按顺序验证，每一步在 Output Log 过滤 `LogCatRun`/`LogCatFishing`/`LogCatEquipment` 相关前缀：

1. PIE 启动，确认 `Event=run_phase_entered ... Phase=DayActive`
2. 调 `ConfigureEquipment`，确认 Equipment `Revision` 从 0 变 1
3. 按放竿键，确认 `PlaceRod` 结果 `bCommitted=true`，世界里出现 Rod Actor
4. 走近竿，按互动键（`RodInteract`），确认角色被吸附到 `StandAnchor`
5. 瞄水面按抛竿确认键，确认 `Event=fishing_phase_entered ... Phase=Waiting`，浮漂飞出去后 `Phase` 最终变成 `Landed`（Hook 的 `BP_OnHookPresentationChanged` 应该收到一次带 `Landed` 的回调）
6. 确认浮漂先慢浮至少 `MinimumBiteDelaySeconds`（当前 5 秒），再快速抖动 `BiteWarningSeconds`（当前 3 秒），然后下沉并进入 `Phase=TrueBiteWindow`
7. 窗口内按住 Primary，确认提竿成功进 `HookedFight`
8. 鱼仍有体力时先收到抄网射线范围内按 `F`，确认鱼直接挂到猫嘴上；也可继续把鱼力竭后回收，确认岸上生成可交互的死鱼 Actor
9. F 抄中的鱼应已处于嘴叼状态；力竭落地鱼则先按 `E` 叼起。两条路线都确认随身背包没有新增鱼，再对目标地面鱼护按 `E`，确认只写入该鱼护
10. 单独测打窝：调用 `SubmitPlaceChum`，确认 `TryGetPlaceChumResult` 返回 `bCommitted=true`，且第 6 步的等待时间因为窝料明显缩短

任何一步卡住，先看对应阶段在本文第 0/1 节里是"开箱可用"还是"需要你自己接线"，再去查 Config/DataAsset 校验链（第 2 节）。
