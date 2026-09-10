# 钓鱼 MVP 蓝图接线指南

本文面向：C++ 权威链已经写完之后，需要在 UE Editor 里把「打窝、钓鱼、抄鱼」跑通的人。内容分三层：

1. **本轮已经在 C++ 补上的东西**（你不用再写代码，但要知道它们现在能做什么）
2. **纯资产/配置工作**（DataAsset、StateTree、`DefaultGame.ini`，不涉及蓝图节点图）
3. **必须手写蓝图节点图的地方**（这是本文重点，逐个给出输入、节点链和陷阱）

---

## 0. 先看清楚：六个钓鱼输入 Ability 的真实状态

项目在 `AbilitySystem/Fishing/InputAbilities/` 和 `AbilitySystem/Fishing/CatFishingGameplayAbility.h` 中定义了六个钓鱼输入 `UGameplayAbility` 原生子类，`UCatAbilitySet::IsRuntimeReady()` 强制要求对应的六个 Fishing InputTag 必须齐全。它们各自的完成度不同，**这是本文最重要的一张表**，决定了你还要不要为对应功能另写蓝图：

| InputTag | Ability 类 | 服务器命令 | 状态 |
|---|---|---|---|
| `Cat.Input.Fishing.RodInteract` | `UCatGA_FishingRodInteract` | OperateRod | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Primary`（按下） | `UCatGA_FishingPrimaryAction` | RequestHook / 搏斗中收线 | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Primary`（松开） | 同上 `InputReleased` | PrimaryReleased / 停止收线 | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Cancel` | `UCatGA_FishingCancel` | CancelFishing | ✅ 一直可用 |
| `Cat.Input.Fishing.Scoop` | `UCatGA_FishingScoop` | RequestScoop | ✅ 本轮已补，**开箱可用** |
| `Cat.Input.Fishing.Chum` | `UCatGA_FishingChum` | ChumPressed / ChumReleased → PlaceChum | ✅ Q 蓄力打窝已由 C++ 接管 |
| `Cat.Input.Fishing.Slack` | `UCatGA_FishingSlack` | 松开线杯 | ✅ 遛鱼时可用 |

正式键鼠入口已经由 `CatAbilityInputBindingComponent`、原生 Ability 和 `CatFishingCommandComponent` 接通。R取竿/放竿、主控左键抛钩/提竿/收线、右键放线，以及旁人的双爪抓握都不需要再绑定一套蓝图输入。下文 payload 节点仅供自定义 UI 使用；和现有键位重复提交会产生两条请求。

装备选择仍使用原有 Equipment/Inventory 权威入口；表现蓝图消费结果、快照和抓握状态。
---

## 1. 本轮 C++ 改动清单（供你确认代码已同步）

| 文件 | 改动 |
|---|---|
| `Fishing/Integration/CatFishingCommandComponent.cpp` | `HandleAbilityCommandFromAuthority` 按唯一主控裁决：R放下当前竿；已抓回本人的竿时R恢复主控，否则从库存取竿；Primary收放边沿只由主控进入会话 |
| `Framework/Game/CatfishingPlayerController.h` | `ServerConfigureEquipment` 加 `BlueprintCallable`，蓝图现在能直接调用它提交装备定义和实例 ID |
| `Equipment/CatEquipmentComponent.h` | `GetSnapshot()` 加 `BlueprintPure`，蓝图能读当前 `Revision`、装备 DefinitionId 和对应 ItemInstanceId |
| `Character/CatCharacter.h` | `GetEquipmentComponent()` / `GetConditionComponent()` 加 `BlueprintPure` |
| `Environment/CatWaterRegion.h` | `GetWaterRegionHandle()` / `HasValidBakedGeometry()` 加 `BlueprintPure`，蓝图能从关卡里放置的湖 Actor 直接拿到抛竿/打窝要用的 `FCatWaterRegionHandle` |
| `Fishing/Actors/CatFishingHookActor.h/.cpp` | 服务器冻结抛物线并复制给客户端；双方按服务器时间更新，到达精确落点后一次性进入 `Landed`，不再使用高度轮询或 ProjectileMovement |

如果你在自己机器上拉了最新代码却发现这些函数没有 `BlueprintCallable`/`BlueprintPure`，说明改动没同步，先确认代码状态再继续。

`ScheduleWaitingProbeFromStateTree` 首次采样使用服务器冻结的水面落点，等待时间包含剩余飞行时间，因此慢浮等待与咬钩预警从落水后计算。

---

## 2. 纯资产/配置工作（没有蓝图节点图，但必须做）

### 2.1 DataAsset 清单

**已经全部创建并填好数值**（本轮用 Python 批量生成，两个 `IsRuntimeReady()` 校验规则已逐条比对通过）：

| 资产 | 路径 | 关键内容 |
|---|---|---|
| `DA_CatAbilityInputConfig` | `/Game/Data/Abilities/` | `AbilityInputActions` 保存钓鱼 GAS 映射；`NativeInputActions` 额外保存 `IA_Interact` → `Cat.Input.Interact` |
| `DA_CatAbilitySet_Default` | `/Game/Data/Abilities/` | 6 个 Fishing Ability + 6 个无输入 BodyAction 专用 Ability；Primary=`WhileInputActive`，其余按各自触发策略配置 |
| `Equip_ScoopNet_Starter` | `/Game/Catfishing/Data/Equipment/` | 正式目录抄网定义 `StarterScoopNet`；当前不默认发放，商店/奖励来源接入前暂时没有获取渠道 |
| `Fish_*` | `/Game/Catfishing/Data/Fish/` | 16 条正式鱼定义；Showcase2 已使用 `RegionId=River`，按生态条件与连续挑战度从该目录选择 |
| `Bite_*` / `Fight_*` | `/Game/Catfishing/Data/Fish/` | 正式咬钩与搏斗性格；由选中的 `Fish_*` 稳定 ID 解析 |
| `Curve_ChumDistanceFalloff` / `Curve_ChumTimeFalloff` | `/Game/Data/Curves/` | 1→0 线性衰减 |

两个最容易踩的校验坑（生成时已规避，改数值时注意别破坏）：

- **Gameplay Tag 前缀是 `Cat.`** —— `CatFishingAbilityTags.cpp` 里真实定义是 `Cat.Input.Fishing.Primary`，不是 `Input.Fishing.Primary`
- **Chum 的两条 Curve 不能为空，且 `v(0)` 必须 > 0** —— `BakeCurve` 拒绝空曲线，`FCatChumFalloffTable::IsRuntimeReady()` 额外要求首采样点为正值

### 2.2 `Config/DefaultGame.ini`

**已经写好并落盘了**，8 个 section 基本都指向正式资产；RunFlow 已经使用项目内固定资产路径：

```ini
[/Script/Catfishing.CatRunSettings]
RunFlowStateTree=/Game/Data/StateTrees/ST_RunFlow.ST_RunFlow

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

- **ST_RunFlow**（Context = `ACatfishingGameModeBase`）：`DayActive → NormalNight/FailureSettlementNight/SuccessSettlementNight → Ending → Ended`，事件只有 `Cat.Run.QuotaReached/QuotaFailed/AllEligibleReady/SettlementComplete`；`NormalNight` 的成功结算分支必须挂 `Cat Run Success Settlement Eligible`
- **ST_FishingSession**（Context = `ACatFishingSession`）：`Waiting → Probe → HookedFight → ExhaustedReelHold`；鱼体力耗尽时发送 `Cat.Fishing.FishExhausted`，叶子的 `Enter Phase(ExhaustedReel)` 只切生命周期，同一个 Runner 继续运行。**树永远不能自然结束**，`Resolved`/`Terminated` 只能由 C++ 写。

---

## 3. 自定义 UI 的命令节点参考

下面的 payload 调用用于自定义 UI 或专用工具，不要在正式 R、左键、右键上重复绑定。普通键鼠操作继续走原生输入组件，只有当前主控可以提交钓鱼控制。

拿命令组件的通用第一步：

```
Get Controller (Cast to ACatfishingPlayerController)
  → Get Fishing Command Component   ← 已是 BlueprintPure
```

### 3.1 PlaceRod（取出并持握鱼竿）

触发时机：玩家没有占用鱼竿、本人场上不足两根竿且背包还有未使用的鱼竿实例，按 R。成功后直接进入主位持握，无需再次调用 OperateRod。地上和手持合计最多两根，同时最多操作一根；第二根必须是背包中原有的另一物品实例。

```
Get Player Character → Get Equipment Component → Get Snapshot   ← Revision
Make FCatPlaceRodCommand
    RequestId = Make Guid (New Guid)
    ExpectedEquipmentRevision = Snapshot.Revision
    ExpectedInventoryRevision = Inventory.GetInventoryRevision()
→ FishingCommandComponent.Submit Place Rod (Command)
```

- 结果通过 `OnResultReceived`（`BlueprintAssignable` 委托）回调，或用 `TryGetResult(RequestId, OutResult)` 轮询
- 成功后 `FCatFishingCommandResult` 里的 `RodActorId` / `RodActorRevision` / `EquipmentRevision` **要缓存下来**，BeginCast 要用
- 失败常见原因：角色前方没有实体地面或地面太斜（`InvalidPayload`，法线 Z 必须 ≥ 0.7）、本人场上已有两根竿（`RodDeploymentLimitReached`）、已占用另一根竿、没有可用库存实例或持握依赖无效。水域合法性在抛竿时检查。

### 3.2 OperateRod（走近操作）

**不需要写蓝图**——`UCatGA_FishingRodInteract` 负责取出自己的竿与释放当前连接。首次 R 成功后建立实际手部持握；拿起地上的竿或协助其他玩家，改为走近后按住左键或右键伸出对应爪，抓竿或抓住已连到竿的队友。鱼竿和身体通过真实约束传力，不按 StandAnchor 摆放角色。

要部署第二根，先放下第一根，再按R取出本人另一可用实例。R不加入他人会话；放下后先伸爪抓回自己的原竿，再按R恢复主控。实际抓住任意竿或猫，只建立物理连接。旁人既不抛钩、收放线，也不获得物品归属或收纳权限；跨竿抓握不合并Session。主控放下后不会由旁人自动接任。

X 优先处理当前操作竿；空手时只选择 250cm 内本人无人占位的竿。有活动会话先按原阶段走取消或切线裁决，无活动会话再离位并收纳。收纳请求按具体 `RodActorId` 定位，服务器另验归属；目前不能把别人的地面竿收进自己背包。以后开放时，需要先完成原使用记录到接收方库存的原子迁移和失败回滚，不能只删除归属检查。

### 3.3 BeginCast（抛竿）

触发时机：玩家已经直接持握竿并成为主 Operator（本人通过明确取竿操作取得主控后），瞄准水面按住并松开左键。辅助的左右键用于持续抓握，不提交主位抛钩命令。

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

### 3.5 PlaceChum（打窝）—— 普通 Q 已由 Ability 接管，自定义目标点才走 payload

`UCatGA_FishingChum` 现在只是输入壳：按下提交 `ChumPressed` 开始计时，松开提交 `ChumReleased`，服务器按蓄力时长计算落点、从正式库存选择窝料并交给 `PlaceChum` 扣量。下面这个 payload 版本只给自定义 UI 使用，例如玩家要点选目标水面、指定某格窝料或指定数量：

```
Line Trace 拿目标水面点
Get 关卡 ACatWaterRegion → Get Water Region Handle
Get Player Character → Get Inventory Component → Get Inventory Revision   ← 正式库存版本

Make FCatPlaceChumCommand
    RequestId = New Guid
    ExpectedWaterRegionHandle = Region.GetWaterRegionHandle()
    ExpectedEquipmentRevision = InventoryRevision（字段名保留旧协议；正式角色必须传库存版本）
    ChumItemInstanceId = （玩家当前选择的窝料库存格 ItemInstanceId）
    ChumDefinitionId = （可选；服务器会按 ChumItemInstanceId 复核并覆盖为真实定义）
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
- 配置只选择本人真实持有的装备实例，不能通过重复配置重置耐久或生成另一根竿。部署备用竿继续走 R 的原生 `PlaceRod` 链路，背包必须已有第二根未使用的实例；不要在蓝图里另外复制 Actor 或重建装备快照。

---

## 4. 端到端测试清单

按顺序验证，每一步在 Output Log 过滤 `LogCatRun`/`LogCatFishing`/`LogCatEquipment` 相关前缀：

1. PIE 启动，确认 `Event=run_phase_entered ... Phase=DayActive`
2. 调 `ConfigureEquipment`，确认 Equipment `Revision` 从 0 变 1
3. 第一次按 R，确认 `PlaceRod` 结果 `bCommitted=true`，鱼竿直接拿在手上，本人已为主 Operator
4. 再按R放下，伸爪抓回同一根竿后按R恢复主控；确认旁人抓竿不进入会话、不自动接任，角色不被摆到固定站位
5. 瞄水面按抛竿确认键，确认 `Event=fishing_phase_entered ... Phase=Waiting`，浮漂飞出去后 `Phase` 最终变成 `Landed`（Hook 的 `BP_OnHookPresentationChanged` 应该收到一次带 `Landed` 的回调）
6. 确认默认鱼饵下浮漂先慢浮至少 `MinimumBiteDelaySeconds`（当前 3 秒），再快速抖动 `BiteWarningSeconds`（当前 1.5 秒），然后下沉并进入 `Phase=TrueBiteWindow`；无窝/单份新窝中心/五份重叠新窝中心的平均总等待为20/14/6秒。
7. 窗口内按住 Primary，确认提竿成功进 `HookedFight`
8. 鱼仍有体力时先收到抄网射线范围内按 `F`，确认鱼直接挂到猫嘴上；也可继续把鱼力竭后回收，确认岸上生成可交互的死鱼 Actor
9. F 抄中的鱼应已处于嘴叼状态；力竭落地鱼则先按 `E` 叼起。两条路线都确认随身背包没有新增鱼，再对目标地面鱼护按 `E`，确认只写入该鱼护
10. 单独测打窝：调用 `SubmitPlaceChum`，确认 `TryGetPlaceChumResult` 返回 `bCommitted=true`，且第 6 步的等待时间因为窝料明显缩短

双竿专项回归：背包准备两根独立鱼竿实例；取出第一根后放下并离开交互范围，再取出第二根。确认场上合计两根、同时只有一根被本人操作、第三根部署被拒绝；切换两根竿后分别开会话，检查 HUD 跟随当前主位、实例耐久独立写回。X 收起其中一根不得改变另一根的 Actor、会话或使用记录；库存满时收纳失败应恢复原竿。正式地图的房主与客户端都需核对这一整条流程，以及地面/手持姿态和落盘回执。

任何一步卡住，先看对应阶段在本文第 0/1 节里是"开箱可用"还是"需要你自己接线"，再去查 Config/DataAsset 校验链（第 2 节）。
