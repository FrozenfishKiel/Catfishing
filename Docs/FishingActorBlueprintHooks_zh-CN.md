# Fishing Actor 蓝图扩展点操作说明

本文用于 Stage C 的 UE Editor 表现资产制作。它只说明如何从阶段 A 已编译的原生 Actor 派生蓝图并接入 Mesh、皮肤、动画和音画表现，不授予任何玩法权威写入口。

## 1. 创建前检查

1. 使用已通过编译的 `CatfishingEditor` 打开工程。
2. 在 Content Browser 创建或进入 `/Game/Catfishing/Fishing/Actors`。
3. 通过 **Blueprint Class → All Classes** 选择下文指定的原生父类；不要从 Demo 的 Character、Controller、GameMode、FishingComponent 或 Session 派生。
4. 三个原生父类均禁止 Actor Tick，蓝图不得用 Tick 模拟 Session 阶段、鱼体力、鱼竿耐久或权威位移。
5. 三个 Actor 都复制 Actor movement，并以空间相关方式复制；蓝图表现事件标记为 Cosmetic，只在收到本地原生调用时执行，不会自动向其他网络端广播。

Stage C 建议使用以下正式资产路径：

- `/Game/Catfishing/Fishing/Actors/BP_FishingRod`
- `/Game/Catfishing/Fishing/Actors/BP_FishingHook`
- `/Game/Catfishing/Fishing/Actors/BP_FishEncounter`

## 2. 共同的权威边界

蓝图只负责表现，可以：

- 在 `VisualRoot` 子树添加 Static Mesh、Skeletal Mesh、Niagara、Audio 和其他纯表现组件。
- 配置 Mesh、材质实例、材质参数、皮肤外观、AnimBP、Montage、VFX 和 SFX。
- 根据 `BP_On...PresentationChanged` 的只读 Previous/Current 状态切换本地表现。
- 根据原生代码传入的 Event Tag 播放一次性 Montage、VFX 或 SFX。

蓝图不得：

- 创建、推进、终止或重开 Fishing Session，也不得保存第二份可写 Phase 或 Outcome。
- 扣除或恢复 Equipment 的鱼饵、鱼竿耐久、装配、预留或 Revision。
- 生成实物鱼、修改鱼护、调用 Items 捕获提交或发放奖励。
- 把 AnimNotify、Montage 完成、VFX、SFX 或资源加载成功作为扣饵、断杆、上鱼、捕获或终态的提交条件。
- 调用继承自 Actor 的 Transform 写接口移动权威 Actor。局部造型偏移只能发生在 `VisualRoot` 子树。
- 添加客户端自报的 Session、Equipment、Items 或 Transform 写入口，也不得在蓝图中发送替代原生命令的 RPC。

资源缺失时应保留默认外观并记录表现错误；不得回滚已经复制的稳定 ID 或改变玩法事实。

## 3. Rod：`BP_FishingRod`

### 3.1 父类与组件挂载

- 原生父类：`/Script/Catfishing.CatFishingRodActor`
- C++ 类：`ACatFishingRodActor`
- 蓝图资产：`/Game/Catfishing/Fishing/Actors/BP_FishingRod`
- Mesh、材质、AnimBP、Niagara 和 Audio 组件只能添加到可编辑的 `VisualRoot` 下。
- `SceneRoot`、`RodTipAnchor`、`StandAnchor`、`GripAnchor` 是原生锁定组件，不得移动、替换、重挂或用 Construction Script 改写。

### 3.2 canonical anchors 的只读权威边界

Rod 有三个位于 `SceneRoot` 直属层级的 canonical anchors：

- `RodTipAnchor`：权威抛竿原点与鱼线起点。
- `StandAnchor`：权威操作站位。
- `GripAnchor`：权威握持/IK 目标。

蓝图只能调用以下 Blueprint Pure 值 getter：

- `GetRodTipWorldTransform()`
- `GetStandWorldTransform()`
- `GetGripWorldTransform()`

getter 返回的是原生 private canonical local transform 与 Actor Transform 的组合值，不读取蓝图可见组件的临时相对变换。阶段 A 的三个 canonical local transform 固定为 Identity；Stage C 必须由 Rod 功能定义提供最终权威值及客户端重建/复制来源。皮肤、Mesh Socket、AnimBP、Montage 和 Construction Script 永远不能反向修改 canonical anchors。

如果皮肤 Mesh 的 Socket 与 canonical 值存在视觉偏差，应调整 `VisualRoot` 子树、皮肤专用相对变换或 Attachment Socket 映射；不得移动 canonical anchor 来“对齐外观”。

### 3.3 复制状态

`PresentationState` 是 private `ReplicatedUsing` 的只读副本，包含：

- `RodActorId`
- `RodActorRevision`
- `RodDefinitionId`
- `RodSkinDefinitionId`
- `OwnerPlayerState`
- `OperatorPlayerState`
- `bDeployed`
- `bBroken`

Rod Actor 的权威 Transform 由 replicated movement 单独复制。耐久不在 Actor 状态中；正式耐久属于 Equipment，`bBroken` 只表现服务器已提交的破损结果。

### 3.4 Blueprint events

`BP_ApplyRodSkin(RodSkinDefinitionId)`

- 触发时机：权威身份初始化或客户端 `PresentationState` RepNotify 到达后，在通用状态变化事件之前调用。
- 如果初始化/RepNotify 发生在 BeginPlay 前，原生代码会合并 pending 状态，在 BeginPlay 后只派发一次。
- 输入只有稳定 Skin ID。Stage C 由 Presentation Registry 将 ID 解析为 Mesh、材质、Animation Set、VFX/SFX；蓝图不得从 Skin 反推强度、线长、耐久或 anchor。

`BP_OnRodPresentationChanged(Previous, Current)`

- 触发时机：与上项相同，并固定在 `BP_ApplyRodSkin` 之后调用。
- 用途：比较只读 Previous/Current，更新部署、操作人、破损和皮肤的本地表现。
- 阶段 A 只保证初始化通知；后续权威变化必须继续由原生状态入口和 RepNotify 驱动。

`BP_PlayRodPresentationEvent(EventTag)`

- 触发时机：仅当后续原生表现桥显式调用时触发；阶段 A 没有自动网络调用点。
- 用途：播放抛竿、收杆、断杆、空钩等一次性 Montage、VFX 或 SFX。
- Event Tag 只选择表现，不能提交耐久、Session Outcome 或物品变化。

### 3.5 Rod 资产验收

- 允许的七类表现工作已覆盖：Rod Static/Skeletal Mesh、材质、稳定 ID 驱动的皮肤、AnimBP、局部 Montage、VFX、SFX。
- 所有可见 Mesh 都在 `VisualRoot` 子树。
- 切换皮肤只改变 Mesh、材质、AnimBP/Animation Set、VFX/SFX 和允许的局部表现变换。
- 三个 canonical getter 在切换皮肤或移动 `VisualRoot` 后保持相同权威结果。
- 蓝图图表中没有 Session、Equipment、Items 写入，也没有 Set Actor Transform/Location/Rotation。

## 4. Hook/Bobber：`BP_FishingHook`

### 4.1 父类与组件挂载

- 原生父类：`/Script/Catfishing.CatFishingHookActor`
- C++ 类：`ACatFishingHookActor`
- 蓝图资产：`/Game/Catfishing/Fishing/Actors/BP_FishingHook`
- 组件层级为 `SceneRoot → VisualRoot → HookVisualAnchor / BobberVisualAnchor / BaitVisualAnchor`。
- Hook、Bobber、Bait 的 Mesh、材质、局部动画、VFX 和 SFX 放在对应 visual anchor 下；阶段 A 建议表现 Mesh 使用 NoCollision。

### 4.2 anchors 与权威边界

`HookVisualAnchor`、`BobberVisualAnchor`、`BaitVisualAnchor` 都是纯表现附着点，不是 canonical authority anchors。蓝图可以为外观调整它们的局部布局，但服务器落点、命中、水域合法性和抛物线判定不得读取这些表现变换。

Hook Actor 的权威世界位置来自原生 Actor Transform 与 replicated movement。蓝图不得通过 Set Actor Transform/Location/Rotation 驱动飞行、落水或失败，也不得用表现碰撞直接宣布落点。

### 4.3 复制状态

`PresentationState` 是 private `ReplicatedUsing` 的只读副本，包含：

- `FishingSessionId`
- `CastAttemptId`
- `Phase`：`Unconfigured`、`CastFlight`、`Landed`、`Failed`

阶段 A 首次合法初始化把 `Phase` 设为 `CastFlight`。Session 阶段、鱼饵库存、捕获和最终 Outcome 不属于 Hook 状态。

### 4.4 Blueprint events

`BP_OnHookPresentationChanged(Previous, Current)`

- 触发时机：权威身份初始化或客户端 RepNotify 后；BeginPlay 前的多次变化会合并，在 BeginPlay 后派发一次。
- 用途：根据 Hook 表现 Phase 切换飞行、漂浮、落地或失败外观，更新 Hook/Bobber/Bait Mesh、材质、AnimBP、局部 Montage、VFX/SFX。
- 只读状态变化不能替代原生落点确认或 Session 领域事件。

`BP_PlayHookPresentationEvent(EventTag)`

- 触发时机：仅当后续原生表现桥显式调用时触发；阶段 A 没有自动网络调用点。
- 用途：播放入水水花、漂动提示、收回或失败反馈。
- AnimNotify 和碰撞回调只能影响本地表现，不能扣饵、推进 Session 或写权威 Transform。

### 4.5 Hook 资产验收

- 允许的七类表现工作已覆盖：Hook/Bobber/Bait Mesh、材质与表现皮肤变体、Skeletal Mesh 的 AnimBP、局部 Montage、VFX、SFX；这些变体不能新增权威字段。
- 三类 Mesh 位于正确的 visual anchor 子树，且不承担权威碰撞。
- 蓝图只消费只读 Session/Attempt 身份与表现 Phase。
- 蓝图图表中没有 Session、Equipment、Items 写入，也没有 Actor Transform 写入。

## 5. FishEncounter：`BP_FishEncounter`

### 5.1 父类与组件挂载

- 原生父类：`/Script/Catfishing.CatFishEncounterActor`
- C++ 类：`ACatFishEncounterActor`
- 蓝图资产：`/Game/Catfishing/Fishing/Actors/BP_FishEncounter`
- Skeletal Mesh、Static Mesh、AnimBP、材质、Niagara 和 Audio 组件只能添加到 `VisualRoot` 下。

### 5.2 Transform 与权威边界

FishEncounter 没有提供给蓝图修改的 canonical authority anchor。鱼的服务器 Actor Transform 是世界位置的唯一真相，并由 replicated movement 复制。蓝图只允许在 `VisualRoot` 子树做游动、挣扎、转身或上岸的局部动画偏移；不得用 Set Actor Transform/Location/Rotation、Root Motion 或 Timeline 推动权威 Actor。

鱼体力、力量、捕获状态和 Outcome 不在 Fish Actor 中。未来 Fish 行为树若接入，也只能产生运动意图，不能结算体力或捕获。

### 5.3 复制状态

`PresentationState` 是 private `ReplicatedUsing` 的只读副本，字段白名单只有：

- `FishingSessionId`
- `CastAttemptId`
- `FishDefinitionId`
- `MotionIntent`
- `CurrentLineLength`

Fish 世界位置不在该结构中；它只来自 Actor replicated movement。结构中不得新增 stamina、outcome、capture、item、equipment durability 或第二份 position/target transform。

### 5.4 Blueprint events

`BP_OnFishPresentationChanged(Previous, Current)`

- 触发时机：权威身份初始化或客户端 RepNotify 后；BeginPlay 前的变化会合并为 BeginPlay 后的一次派发。
- 用途：按 `FishDefinitionId` 选择 Mesh/材质/AnimBP，按 `MotionIntent` 和 `CurrentLineLength` 切换局部游动、平静、向内或向外挣扎表现。
- 表现切换不得写鱼体力、Session Phase、Outcome、捕获或 Actor Transform。

`BP_PlayFishPresentationEvent(EventTag)`

- 触发时机：仅当后续原生表现桥显式调用时触发；阶段 A 没有自动网络调用点。
- 用途：播放上钩、挣扎、近岸、脱钩或上鱼的 Montage、VFX/SFX。
- Montage 完成和 AnimNotify 不能生成物品、提交捕获或决定终态。

### 5.5 FishEncounter 资产验收

- 允许的七类表现工作已覆盖：Fish Mesh、材质、由 Fish Definition 选择的表现皮肤、AnimBP、局部 Montage、VFX、SFX。
- Mesh、AnimBP、材质、VFX/SFX 全在 `VisualRoot` 子树。
- 蓝图根据只读 `MotionIntent` 做局部动画，不保存第二份运动/体力/Outcome 状态。
- 蓝图图表中没有 Session、Equipment、Items 写入，也没有 Actor Transform 写入。

## 6. 阶段边界与网络验收说明

阶段 A 已验证三个原生父类的 Blueprintable 元数据、组件拓扑、复制注册、authority-only 身份初始化、只读 presentation state 和 cosmetic events；本文不表示 Stage C 资产已经创建或保存。

阶段 A 对 Fishing Command Component 只完成 owning-client reliable RPC 的静态契约、原生 Controller 挂载和单 World 邮箱语义。真实的远程 owner-only 私有路由仍是 Phase B 的阻断测试：旧 RPC/Ability 接入通道后，必须使用 server + owning client + non-owning client 三端网络测试，证明 owning client 恰好收到一次、non-owning client 完全收不到，并证明 Listen Server 本地请求只广播一次。在这些测试通过前，不得宣称 owner-only 远程传输或网络隐私验收完成。
