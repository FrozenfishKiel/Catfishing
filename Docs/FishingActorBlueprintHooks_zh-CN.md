# Fishing Actor 蓝图扩展点操作说明

本文用于 Stage C 的 UE Editor 表现资产制作。它只说明如何从阶段 A 已编译的原生 Actor 派生蓝图并接入 Mesh、皮肤、动画和音画表现，不授予任何玩法权威写入口。

## 1. 创建前检查

1. 使用已通过编译的 `CatfishingEditor` 打开工程。
2. 在 Content Browser 进入当前运行配置使用的 `/Game/Blueprint/Actors`。
3. 通过 **Blueprint Class → All Classes** 选择下文指定的原生父类；不要从 Demo 的 Character、Controller、GameMode、FishingComponent 或 Session 派生。
4. 三个原生父类均禁止蓝图 Actor Tick；原生 Rod Tick 负责权威握持姿态与无载共同速度发布，原生 RodBend 组件负责本地变形。蓝图不得用 Tick 模拟 Session 阶段、成员队形、鱼体力、鱼竿耐久或权威位移；身体移动仍由CMC执行。
5. 三个 Actor 都复制 Actor movement，并以空间相关方式复制；蓝图表现事件标记为 Cosmetic，只在收到本地原生调用时执行，不会自动向其他网络端广播。

当前运行配置使用以下入口：

- `/Game/Blueprint/Actors/BP_CatFishingRodActor`
- `/Game/Blueprint/Actors/BP_CatFishingHookActor`
- `/Script/Catfishing.CatFishEncounterActor`（鱼外观由鱼种库直接解析，不再使用全局 FishEncounter 蓝图）

## 2. 共同的权威边界

蓝图只负责表现，可以：

- 在允许扩展的 `VisualRoot` 子树添加 Static Mesh、Skeletal Mesh、Niagara、Audio 和其他纯表现组件；FishEncounter 的鱼体 Mesh 是原生锁定组件，不得另加平行鱼体 Mesh。
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

## 3. Rod：`BP_CatFishingRodActor`

### 3.1 父类与组件挂载

- 原生父类：`/Script/Catfishing.CatFishingRodActor`
- C++ 类：`ACatFishingRodActor`
- 蓝图资产：`/Game/Blueprint/Actors/BP_CatFishingRodActor`
- Mesh、材质、AnimBP、Niagara 和 Audio 组件只能添加到可编辑的 `VisualRoot` 下。
- `SceneRoot`、`RodTipAnchor`、`StandAnchor`、`RightStandAnchor`、`LeftStandAnchor`、`GripAnchor` 是原生锁定组件，不得移动、替换、重挂或用 Construction Script 改写。

### 3.2 canonical anchors 的只读权威边界

Rod 的 canonical 中心锚与当前左右站位参考组件都位于 `SceneRoot` 直属层级：

- `RodTipAnchor`：权威抛竿原点与鱼线起点。
- `StandAnchor`：公共操作交互点与兼容站位查询的 canonical 中心。
- `RightStandAnchor` / `LeftStandAnchor`：兼容站位的编辑器参考组件；1–4人从入竿起通过共同运动根与个人偏移保持位置，不按编号传送角色。
- `GripAnchor`：权威握持/IK 目标。

蓝图只能调用以下 Blueprint Pure 值 getter：

- `GetRodTipWorldTransform()`
- `GetStandWorldTransform()`
- `GetOperatorStandWorldTransform(SlotIndex)`
- `GetOperatorCount()` / `GetOperatorSlotIndex(PlayerState)` / `IsPrimaryOperator(PlayerState)`
- `GetGripWorldTransform()`

getter 返回的是原生 private canonical local transform 与 Actor Transform 的组合值，不读取蓝图可见组件的临时相对变换。服务器通过 `ConfigureCanonicalAnchorsFromAuthority` 设置装备定义提供的三个锚点；当前握把由 `GripCanonicalLocalTransform` 初始复制，竿尖/站位在客户端的重建缺口仍以多人审计记录为准。弯曲与鱼线显示使用独立的 `RodTipMarker`，不依赖该缺口，也不能据此宣称已修复权威锚点复制。皮肤、Mesh Socket、AnimBP、Montage 和 Construction Script 永远不能反向修改 canonical anchors。

加入时记录的是成员当下的相对位置；无Session、钩子飞行、Waiting、Probe、TrueBiteWindow都已使用组移动。无载时Rod只读个人ASC与同一GroupModel，发布经过全员最小胶囊sweep距离约束的共同水平速度，CMC保留重力和真实碰撞；不能由BP把成员Attach到竿上、按槽位SetActorLocation或再加一份个人移动。退出者恢复自由移动，仍占竿者在一轮终局后继续无载组移动。鱼力竭仍保留搏斗域，仅清鱼驱动力；不要凭零张力或鱼力竭自行解除队形、切相机或重置Aim域。

同竿成员的移动碰撞忽略由原生CMC按名单登记和恢复，BP不要全局关闭胶囊碰撞或覆盖其他系统的忽略项。无载探测允许地面角色经CMC跨过可StepUp的低台阶，真正墙体仍限制整组；坡面高度差和接触容差是正常身体结果，不应通过BP逐帧改Transform把全员锁在相同高度。

如果皮肤 Mesh 的 Socket 与 canonical 值存在视觉偏差，应调整 `VisualRoot` 子树、皮肤专用相对变换或 Attachment Socket 映射；不得移动 canonical anchor 来“对齐外观”。本轮不改这些资产、反射钩子或Cook入口，历史图审计不代替本轮正式表现验证；验证状态统一见 [钓鱼架构2.0.4](FishingArchitecture_zh-CN.md#204-入竿即保持队形的影响与验证2026-09-08)。

### 3.3 现有静态鱼竿的受力弯曲

正式蓝图的 `StaticMesh` 标记为 `RodBendSource`，引用 `/Game/Catfishing/Fishing/Presentation/SM_Rod_BendSource`。此资产是现有 `SM_Fishing_Rods_01a` 的 CPU 可读副本；原素材与材质继续保留，没有新增外部美术依赖。`Scripts/configure_rod_bend_visual.py` 生成副本、关闭副本 Nanite、保留网格 Transform/材质，并把 `RodTipMarker` 校准到实际网格最顶端截面中心。原标记取包围盒顶面中心，受绕线轮宽度影响偏离真实竿尖约 5.46cm；迁移只修正表现标记，绝不改装备定义的 canonical anchors。

`UCatRodBendComponent` 在本地复制该网格的各材质段、顶点色与最多四套 UV，一次性补充分段后按横向线负载变形；成功后隐藏源组件自身，保留其子组件与原碰撞合同。握把/绕线轮处保持刚性，前方竿身按弧长采样弯曲。`FCatRodBendCurve` 的输入长度为模型局部厘米、方向性弯曲量为弧度；不参与玩法力学。源网格不可 CPU 读取或表现标记缺失时保留原外观并输出 Warning。

`ACatFishingSession` 将最终固定步的 `LineTensionNewtons` 传入现有 Hook 的 `SetFishingLinePresentationFromAuthority`，与线长/松绷状态一起复制。新增实际张力默认 0，单位 N，与既有 `NormalizedTension` 的 0–1 显示归一化含义分开。组件从 Hook 位置和原始表现竿尖计算拉力方向，取垂直竿轴的分量；不能使用猫/鱼相抵后的净转矩，否则僵持时会错误回直。插地与手持共用该路径；无张力、断竿、收杆、Owner 丢失或 Hook 销毁时平滑卸力，专用服务器不创建变形网格。销毁后的诊断仍保留原 SessionId 以便关联。

鱼线在采样前刷新弯曲，并连接变形后的 `RodTipMarker`；视觉弧长补偿端点移动，保留原有余线预算。组件绝不写入 `GetRodTipWorldTransform()`、实际放线长度、鱼/猫受力、耐久或终局。视觉缓冲与弹性物理解算是不同职责，本功能不增加玩法弹簧。

调参入口为 Project Settings → Catfishing Fishing Presentation → RodBend（配置节 `[/Script/Catfishing.CatFishingPresentationSettings]`）：

| 参数 | 默认值与含义 |
| --- | --- |
| `RodBendAxisLocal` | `(0,0,1)`，当前模型的纵向轴 |
| `RodBendRigidFraction` | `0.25`，源模型下部保留的刚性比例 |
| `RodBendReferenceForceNewtons` | `50 N`，横向负载达到此值时取最大弯曲角的一半 |
| `RodBendMaximumAngleDegrees` | `75°`，纯表现上限，不是断竿阈值 |
| `RodBendResponseSeconds` | `0.15 s`，加载与回直的指数响应时间 |

诊断分类为 `LogCatFishing`，检索 `fishing_rod_bend_ready/bound/sample/rejected`；正常加载/卸载与每两秒受载采样默认落盘，带 `RodActorId/SessionId/NetMode/Authority/LocalRole/HolderPlayerId/LineTensionN/BendRadians`。基础回归为 `Catfishing.Unit.Fishing.Presentation.RodBend`，真实本机联机为 `Catfishing.Editor.Fishing.RodBendListenClient`，离屏画面导出为 `Catfishing.Editor.Fishing.RodBendRender`（使用 `-RenderOffscreen`，不能使用 `-nullrhi`）。截图只证明受控场景中的素材表现，不能代替正式地图/Steam 双机/打包双端验收。

本轮衔接核对（2026-09-07）：修改前工作区已有牵引、运动诊断与对应文档的并行改动，保留原样，不归入弯曲提交。已有钓鱼回归失败为起始竿耐久资产 500 与测试预期 150 不一致；本轮未改该资产或断言。下表源码位置均相对 `Source/`，证据均位于 `Saved/Automation/RodBend/`。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 负载入口与复制 | `Catfishing/Fishing/CatFishingSession.cpp::HandleFightRunnerStepFromAuthority` 调用 `Actors/CatFishingHookActor.cpp::SetFishingLinePresentationFromAuthority`；Hook 的 `PresentationState` 已注册复制 | 原有归一化张力只作 0–1 显示；新增最终实际张力 N，默认 0；服务器仍唯一写入，松线强制 0 | 扩展 `FCatFishingHookPresentationState::LineTensionNewtons`，保持旧调用默认参数 | 先接收字段，再由 Session 发布最终固定步值 | 不得使用净转矩或客户端自报力；验证真实复制和拒绝客户端写入 | 已接通；`NetworkRenderReport/index.json` 的 Listen/Client 用例通过，双方收到 100 N |
| 组件生命周期 | `Actors/CatFishingRodActor.cpp` 原生 `VisualRoot`；Hook 的 `RefreshFishingLineAttachment` 解析 Owner | 原为刚性模型；新增本地变形，不要求当前持竿人存在 | 原生 `Presentation/CatRodBendComponent`，弱引用 Hook；专用服务器跳过网格 | 组件准备后由现有鱼线绑定路径关联 Hook | 插地/手持共用；松线、坏竿、收起、Owner 丢失、Hook 销毁卸力；退出恢复源组件 | 已接通；正式蓝图受控 World 与真实联网 Hook 销毁回直通过；坏竿/收起分支仅完成调用链核对，未单独运行验收 |
| 核心计算 | `Presentation/CatRodBendCurve.cpp::TargetBend/Initialize/DeformPosition` 被组件读取 | 新增横向力到弯角的纯表现映射；局部 cm、角度 rad；固定握把、保持中心线弧长 | 48 段曲线、一次性轴向细分、指数响应；不改变模拟器 | 先计算曲线，再用同一变形映射更新顶点与标记 | 多方向、轴向力、握把刚性、弧长、15–240 FPS 回直 | 已实现；`FishingReport/index.json` 两项 RodBend 测试通过 |
| 正式资产与迁移 | `/Game/Blueprint/Actors/BP_CatFishingRodActor` 原引用 `/Game/Boatyard/VOL5_Dockyard/Meshes/LP/SM_Fishing_Rods_01a` | 原网格不可在 Cook 后读取 CPU 顶点；没有骨骼；仍复用已有造型、UV 与材质 | `Scripts/configure_rod_bend_visual.py::main` 生成 `/Game/Catfishing/Fishing/Presentation/SM_Rod_BendSource`，开启 CPUAccess、关闭副本 Nanite并添加源组件标签 | 先保存可读副本，再切换正式 BP；原素材保持不变 | 不覆盖其他素材消费者；原资产外部/二进制引用未完全确认，保留上游素材供复用，不删除 | 迁移、正式 BP 加载、材质与实际网格变形通过；见 `Migration.log`、`AssetAuditFull.log` |
| 视觉竿尖与鱼线 | 同一 BP 的 `RodTipMarker` → Hook 的 `UpdateFishingLinePresentation` → `CatFishingLineCurveComponent::UpdateCurve` | 旧标记按包围盒顶面中心偏离真实尖端约 5.46 cm；弯曲后需跟随端点且不凭空多出余线 | 标记校准至实际最高截面中心；采样前刷新弯曲；视觉线长按端点距离差补偿 | 先迁移标记，再同步网格/标记，最后鱼线采样 | 权威放线长度和 `GetRodTipWorldTransform` 不变；实测绷紧线不产生额外松弛 | 已接通；`TipAudit.log`、正式 World 测试和 `Images/01-rest.png`、`02-loaded.png`、`03-released.png` |
| 配置与默认值 | `Presentation/CatFishingPresentationSettings.h`、`[/Script/Catfishing.CatFishingPresentationSettings]` | 新增 +Z、刚性比例 0.25、50 N、75°、0.15 s；不复用旧强度/耐久参数 | 新增独立 RodBend 配置；既有玩法默认值保留 | 数据含义先明确，再由组件读取 | 非法参数拒绝或归零；CPU 数据缺失时保留原外观 | 已实现并经构建；参数说明见上表；未新增 DataAsset/WBP 配置 |
| 权威、副作用与清理 | Rod canonical getters、Session/Runner、Equipment 仍为原入口；组件只改视觉 Mesh/Marker | 不写装备、存档、费用、终局、鱼猫受力或权威锚点；无新增持久化 | 保留现有入口；只新增只读表现消费；EndPlay 清网格/弱引用 | 避免把弯曲反馈到同一负载方向和物理求解 | 验证 canonical 不变、松线清力、Hook 销毁卸力 | 已核对；两项 RodBend 单元/运行用例与 Listen/Client 通过；资源扣费/存档不涉及 |
| 日志与消费者 | `CatRodBendComponent::LogVisualEvent`；显示消费者为正式竿 Mesh 与鱼线，现有 HUD 继续读取旧显示标量 | 新增默认落盘事件；同场 SessionId 在 Hook 销毁后继续保留；受载每两秒限频 | `LogCatFishing` Display/Warning；无新增 WBP、Montage 或动画资产 | 按加载→绑定→受载→卸力关联日志 | 只记录观察事实；无 Tick 无条件刷屏 | `NetworkRender.log` 包含 NetMode 2/3 双端事件；正式 Win64 包无 `-log` 双端落盘尚未运行 |
| Cook、构建与交付 | 正式 BP 硬引用新增副本，沿用现有 Cook 入口；新增 `Tests/CatRodBendTests.cpp` 与 Editor 网络/截图用例 | 不新增 Cook 列表；源码契约、运行行为、正式场景交付分层 | 修正本指南旧 BP 路径、蓝图 Tick 与 canonical 说明；无并行旧弯曲实现需删除 | 资产保存后构建、回归、联网、截图核验 | 编译/受控画面不等于正式场景或打包验收 | Editor/Game Development 构建成功；Fishing 132 clean＋3 警告、1 既有失败、0 notRun；网络/截图 2/2 通过；Cook/打包、正式地图手持观感、Steam 双机未运行，继续挂在 Fishing 模块 |

分层结论：`contract` 通过本轮构建和新增公式/资产契约检查，完整回归保留上述既有失败；`runtime_behavior` 通过正式 BP 受控 World 与实际 Listen/Client 复制、松线和 Hook 销毁测试；`presentation_delivery` 只有受控离屏画面已检查，正式地图、真人手持/插地体验及打包双端尚未验收，不能据此关闭 Fishing 模块。实际日志路径为 `D:/develop/Catfishing/Saved/Automation/RodBend/FishingTests.log`、`NetworkRender.log`；尚无本轮打包房主/客户端日志。

### 3.4 复制状态

`PresentationState` 是 private `ReplicatedUsing` 的只读副本，包含：

- `RodActorId`
- `RodActorRevision`
- `RodDefinitionId`
- `RodSkinDefinitionId`
- `OwnerPlayerState`
- `OperatorPlayerState`
- `OperatorPlayerStates`（紧凑有序数组；0=主位，其余为只控制移动的辅助；默认最多4人，单/多人状态只看当前数组长度，身份不能用下标表示）
- `bDeployed`
- `bBroken`

Rod Actor 的权威 Transform 由 replicated movement 单独复制。耐久不在 Actor 状态中；正式耐久属于 Equipment，`bBroken` 只表现服务器已提交的破损结果。

### 3.5 Blueprint events

`BP_ApplyRodSkin(RodSkinDefinitionId)`

- 触发时机：权威身份初始化或客户端 `PresentationState` RepNotify 到达后，在通用状态变化事件之前调用。
- 如果初始化/RepNotify 发生在 BeginPlay 前，原生代码会合并 pending 状态，在 BeginPlay 后只派发一次。
- 输入只有稳定 Skin ID。Stage C 由 Presentation Registry 将 ID 解析为 Mesh、材质、Animation Set、VFX/SFX；蓝图不得从 Skin 反推强度、线长、耐久或 anchor。

`BP_OnRodPresentationChanged(Previous, Current)`

- 触发时机：与上项相同，并固定在 `BP_ApplyRodSkin` 之后调用。
- 用途：比较只读 Previous/Current，更新部署、操作人、破损和皮肤的本地表现。
- 阶段 A 只保证初始化通知；后续权威变化必须继续由原生状态入口和 RepNotify 驱wo
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

## 4. Hook/Bobber：`BP_CatFishingHookActor`

### 4.1 父类与组件挂载

- 原生父类：`/Script/Catfishing.CatFishingHookActor`
- C++ 类：`ACatFishingHookActor`
- 蓝图资产：`/Game/Blueprint/Actors/BP_CatFishingHookActor`
- 组件层级为 `SceneRoot → VisualRoot → HookVisualAnchor / BobberVisualAnchor / BaitVisualAnchor`。
- Hook、Bobber、Bait 的 Mesh、材质、局部动画、VFX 和 SFX 放在对应 visual anchor 下；阶段 A 建议表现 Mesh 使用 NoCollision。

鱼线显示使用原生 `FishingLineCurve`（`UCatFishingLineCurveComponent`），一个无碰撞的程序化细管网格按端点与已放线长生成下垂曲线，不再运行 Cable 粒子模拟。项目设置 `Catfishing Fishing Presentation → FishingLine` 的 `FishingLineCurveSegments=64` 控制采样精度，`FishingLineWidthCentimeters=1.25` 控制粗细，端点与线长插值速度保留；重力、物理子步、刚度与求解次数已从配置删除。`Scripts/migrate_fishing_line_curve.py` 只编译重存正式 Hook，并检查旧 Cable 不再实例化、可视锚点和 Mesh 资源不变。材质和可见性通过新曲线组件管理，蓝图不得重新添加会驱动物理的鱼线入口。

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

## 5. FishEncounter：鱼种库直连表现

### 5.1 父类与组件挂载

- 原生父类：`/Script/Catfishing.CatFishEncounterActor`
- C++ 类：`ACatFishEncounterActor`
- 运行类：`/Script/Catfishing.CatFishEncounterActor`
- 原生组件层级：`SceneRoot → VisualRoot → FishMesh`。`FishMesh` 关闭碰撞，只消费鱼种库解析出的外观。
- 不再创建“所有鱼共用一个 Mesh/AnimBP”的 FishEncounter 蓝图，也不得在蓝图 Construction Script 中按 `FishDefinitionId` 维护第二张资源映射。

### 5.2 Transform 与权威边界

FishEncounter 没有提供给蓝图修改的 canonical authority anchor。鱼的服务器 Actor Transform 是世界位置的唯一真相，并由 replicated movement 复制。每种鱼的局部轴向、位置、侧翻角和基础比例统一配置在自己的 `FishPresentation_*`；不得用 Set Actor Transform/Location/Rotation、Root Motion 或 Timeline 推动权威 Actor。

鱼体力、力量、捕获状态和 Outcome 不在 Fish Actor 中。行为树只能产生运动意图，不能结算体力或捕获。

### 5.3 复制状态

`PresentationState` 是 private `ReplicatedUsing` 的只读副本，字段白名单只有：

- `FishingSessionId`
- `CastAttemptId`
- `FishDefinitionId`
- `MotionIntent`
- `IntendedSwimSpeedCentimetersPerSecond`：行为意图在线长/岸线约束前选择的自由游速；鱼被挡住不动时仍保持冲刺值
- `CurrentLineLength`
- `FishLineAlignment`：鱼游向与鱼线向外方向点积，`-1` 朝竿尖、`0` 横向、`1` 正对外冲
- `NormalizedLineLoad`：性格幂曲线处理后的服务器受力比例，`0~1`
- `bStrongConfrontation`：服务器已确认进入强对抗；可直接驱动 AnimBP/Montage/VFX/SFX

Fish 世界位置不在该结构中；它只来自 Actor replicated movement。结构中不得新增 stamina、outcome、capture、item、equipment durability 或第二份 position/target transform。

### 5.4 Blueprint events

`BP_OnFishPresentationChanged(Previous, Current)`

- 触发时机：权威身份初始化或客户端 RepNotify 后；BeginPlay 前的变化会合并为 BeginPlay 后的一次派发。
- 用途：补充材质参数、VFX/SFX 等不属于基础鱼体资源选择的纯表现；Mesh、骨骼、AnimBP 与基础动画已由原生代码沿鱼种库直接应用，不允许在事件里重新按 ID 选择一遍。
- 表现切换不得写鱼体力、Session Phase、Outcome、捕获或 Actor Transform。

`BP_PlayFishPresentationEvent(EventTag)`

- 触发时机：仅当后续原生表现桥显式调用时触发；阶段 A 没有自动网络调用点。
- 用途：播放上钩、挣扎、近岸、脱钩或上鱼的 Montage、VFX/SFX。
- Montage 完成和 AnimNotify 不能生成物品、提交捕获或决定终态。

### 5.5 Base AnimBP 与每鱼覆盖

原生类 `UCatFishAnimInstance` 负责从 `ACatFishEncounterActor` 读取复制状态，并输出经过本地平滑的 `SwimPlayRate`；它不读取 `Get Velocity`，因此鱼被线端或岸线挡住时不会错误降成待机。这套速率逻辑只维护一次。

#### 5.5.1 资产链

```text
Fish_<Species> (UCatFishDefinition)
  └─ PresentationDefinition
       └─ FishPresentation_<Species> (UCatFishPresentationDefinition)
            ├─ SkeletalMesh（自身持有 Skeleton）
            ├─ AnimInstanceClass → ABP_Fish_<Species>
            ├─ Calm / Struggle / Exhausted / Landed Animation
            └─ 水中 / 落地 / 嘴叼 Transform 与重量缩放参数

ABPT_CatFishBase（无 Target Skeleton 的 AnimBP Template）
  └─ ABP_Fish_<Species>（绑定本鱼 Skeleton，只覆盖三个 Sequence Player 资源）
```

`ABPT_CatFishBase` 保存 `Calm / Struggle / AutoHauling` 状态机、转换条件以及 `SwimPlayRate` 接线；每鱼子 ABP 不复制 Event Graph 或速率公式。`FishPresentation_*` 是 `UDataAsset`，不能作为独立鱼目录枚举；运行时只允许从 `Fish_*::PresentationDefinition` 进入。

死鱼贴地由 `CatFishGrounding` 统一处理：Runner 复制已确认的干地接触和坡面法线，Encounter 与 Pickup 按鱼体包围盒、侧翻姿态和冻结重量缩放计算世界 Z 抬升，使鱼身完整位于接触坡面上方。Actor 根仍是权威接触点，进入水面时清除地面专用抬升。不要再给 `LandedMeshRelativeTransform` 添加固定 15cm 的贴地补偿；原生默认值和资产生成器已改为无固定补偿。16 份正式资产均继承该默认值，重新加载核对为零，无需改写二进制；`Scripts/migrate_fish_ground_offsets.py` 可核对资产并清理显式保存的旧 15cm 值。拾取后 FishMesh 使用独立世界比例，猫嘴骨骼的缩放不会改变鱼的大小；掉落时清理继承的根缩放并重新定位地表。诊断过滤 `fish_ground_presentation`、`fish_pickup_grounded`、`fish_pickup_ground_received`、`fish_pickup_dropped` 与 `fish_pickup_ground_query_failed`。

#### 5.5.2 新增或换鱼资源

1. 在对应 `FishPresentation_*` 配置 Mesh、四类动画和三种局部 Transform。
2. 新建继承 `ABPT_CatFishBase` 的 `ABP_Fish_*`，Target Skeleton 设为该 Mesh 的 Skeleton。
3. 在子 ABP 的 Asset Override Editor 中，仅覆盖 `Calm / Struggle / AutoHauling` 三个 Sequence Player。
4. 把子 ABP Generated Class 写回同一 `FishPresentation_*`，最后只在 `Fish_*::PresentationDefinition` 直接引用该表现资产。
5. 运行 `Catfishing.Unit.Fishing.Assets.FormalFishPresentationsUseCatalogOwnedAnimBlueprintChildren`；它会检查直连关系、Skeleton 兼容性、Base 继承和覆盖动画一致性。

Base 状态机口径固定为：

```text
Calm       -> Struggle    : MotionIntent == StrugglingOutward
Struggle   -> Calm        : MotionIntent == CalmOrInward
Calm       -> AutoHauling : MotionIntent == AutoHauling
Struggle   -> AutoHauling : MotionIntent == AutoHauling
```

转换 Blend Duration 为 `0.15s`。`Calm` 与 `Struggle` 的 Sequence Player 共用 `SwimPlayRate`；`AutoHauling` 播放力竭动画且不返回活鱼状态，之后由 Pickup 接管。

`NormalizedLineLoad`、`FishLineAlignment`、`bStrongConfrontation` 只用于之后增加身体弯曲、水花、受力抖动或爆发表现，不要乘进基础 `SwimPlayRate`，否则鱼横向冲刺或被线挡住时又会错误减弱。

子 ABP 不在 Event Graph 获取 Pawn、计算位置差或重复设置速率变量。`None` 与 `AutoHauling` 时 `SwimPlayRate` 返回中性 `1.0`。

### 5.6 FishEncounter 资产验收

- 允许的七类表现工作已覆盖：Fish Mesh、材质、由 Fish Definition 选择的表现皮肤、AnimBP、局部 Montage、VFX、SFX。
- Mesh 由原生 `FishMesh` 承载；AnimBP 与基础动画均能沿 `FishDefinition → PresentationDefinition` 直接追溯。
- 每个子 ABP 都继承同一个无骨骼模板并绑定自己的 Skeleton，Base 状态机与速率逻辑没有被复制。
- 蓝图根据只读 `MotionIntent` 做局部动画，并只消费原生 `SwimPlayRate`；不保存第二份运动/体力/Outcome 状态。
- 蓝图图表中没有 Session、Equipment、Items 写入，也没有 Actor Transform 写入。

## 6. 阶段边界与网络验收说明

原生 Actor 的组件拓扑、复制注册、authority-only 身份初始化、只读 presentation state 和 cosmetic events 已有自动化；16 套正式鱼表现资产也已创建并通过直连/Skeleton/Base 继承合同。具体朝向、尺寸和动画节奏仍需在正式 Lake 目测调参。

阶段 A 对 Fishing Command Component 只完成 owning-client reliable RPC 的静态契约、原生 Controller 挂载和单 World 邮箱语义。真实的远程 owner-only 私有路由仍是 Phase B 的阻断测试：旧 RPC/Ability 接入通道后，必须使用 server + owning client + non-owning client 三端网络测试，证明 owning client 恰好收到一次、non-owning client 完全收不到，并证明 Listen Server 本地请求只广播一次。在这些测试通过前，不得宣称 owner-only 远程传输或网络隐私验收完成。
