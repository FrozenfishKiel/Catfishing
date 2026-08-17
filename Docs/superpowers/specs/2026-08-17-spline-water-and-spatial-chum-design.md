# 样条水域与空间窝料场设计

- 日期：2026-08-17
- 目标工程：`D:\develop\Catfishing`
- 状态：对话设计已确认，等待书面规格复核
- 取代范围：原可玩竖切规格中“AABB WaterRegion、整条河共享 ChumPool、StateTree 提供 NearShore 世界坐标”的相关设计

## 1. 背景

当前原型用一个 `ACatWaterRegion` 的世界 AABB 表示整段水域，并在 Region 上维护一份共享 `ChumPool`。该模型不能表达不规则河岸、岛屿、禁钓洞、猫在岸外与鱼在岸内的双向近岸关系，也会让相距很远的窝料互相影响。

新需求固定如下：

- 河流边界由关卡设计师在编辑器中配置，游戏运行期间不变化。
- 美术水面和地形尚未确定，玩法不能依赖最终水面模型或碰撞。
- 一个 WaterRegion 第一版近似水平；明显存在水位落差时拆成多个 Region。
- 每次投放窝料形成独立圆形影响区。
- 多个窝料影响区重合时贡献叠加，但最终吸引概率具有边际递减。
- 需要同时判断水域内外、猫在岸外的交互距离，以及鱼在水内的 NearShore 距离。
- 一个 Region 支持多个 `Include` 闭合轮廓以及可选的 `Exclude` 闭合轮廓。
- 玩家仅在打窝或瞄准时临时看到影响范围；表现覆层不能成为玩法真相。

现在正式抛竿、局部窝料选鱼、鱼运动与 NearShore 尚未接通，因此应在继续 Fishing Phase B 前完成这次空间模型替换。

## 2. 目标与非目标

### 2.1 目标

- 用闭合样条配置不规则水域和岸线。
- 在运行期提供确定、只读、服务器权威的二维多边形查询。
- 统一返回内外分类、有符号岸距、最近岸点、朝水方向和水面点。
- 用独立空间 Field 取代整条河共享 `ChumPool`。
- 在查询位置叠加窝料三轴贡献，并与鱼饵职责分离。
- 保留可选 Blueprint 表现入口，支持临时水面、边界预览、圆环和 Niagara。
- 保持 Geometry、窝料 Field、Fishing Session、StateTree 和表现层各自只有一份可写真相。
- 为凹多边形、洞、自交拒绝、事务幂等和三端网络行为建立自动化门禁。

### 2.2 非目标

- 第一版不支持一局进行中编辑岸线。
- 第一版不支持一条 Region 内连续变化的水面高程、瀑布或上下重叠水层。
- 第一版不模拟水深、流速、河床或浮力。
- 第一版不把 UE Water Plugin、Landscape 或最终水面 Mesh 设为强依赖。
- 第一版不要求精确生成“圆形窝料场与水域多边形的布尔交集 Mesh”。
- 第一版不让每个窝料 Field 成为权威复制 Actor。
- 不允许客户端覆层碰撞、Decal 或 Niagara 参与服务器裁决。

## 3. 方案裁决

采用“权威多边形查询与可选表现覆层分离”的混合方案。

```text
闭合边界样条
    ↓ 编辑期采样、校验和烘焙
只读二维多边形缓存（唯一几何真相）
    ├─ 水域内外
    ├─ 有符号岸距
    ├─ 最近岸点和朝水方向
    ├─ 抛竿、打窝、鱼运动、NearShore、Scoop
    └─ 可选生成无碰撞表现覆层
```

不采用“生成带碰撞的不规则覆层并把它当作玩法区域”的方案，原因如下：

- 凹多边形和洞需要可靠三角化，薄面碰撞不等于水域体积。
- 生成碰撞会污染 Navigation、Trace 语义和烹饪流程。
- 最终水面美术替换会被迫改动玩法几何。
- 表现 Actor 丢失、未加载或降级时不应改变服务器结果。

外包 AABB 仍然保留，但只用于空间粗筛，不再具有最终判定权。

## 4. 唯一真相与职责

| 单元 | 唯一拥有的事实 | 明确禁止 |
|---|---|---|
| `ACatWaterRegion` | Region 身份、水平面配置、Boundary 引用、已烘焙 Geometry Cache 和 Geometry Revision | 不持有共享 ChumPool；不把视觉 Mesh 当边界 |
| `ACatWaterBoundarySplineActor` | 单个 Include 或 Exclude 闭合样条的编辑输入 | 不回答玩法查询；不持有窝料或鱼 |
| `UCatWaterQuerySubsystem` | Region 注册索引和唯一公开只读空间查询入口 | 不修改边界、装备、Session 或 Field |
| `UCatChumFieldSubsystem` | 全部权威 Field、生命周期、预算预留、幂等结果和空间索引 | 不选择鱼；不持有客户端表现 Actor |
| `UCatChumFieldReplicationComponent` | 挂在正式 GameState 上，复制面向客户端的轻量 FastArray 表现 DTO | 不成为 Field 权威写口 |
| Fishing Session | 当前 Attempt、Phase、冻结的选择输入和终态 | 不复制边界多边形；不持有第二份 Field 集合 |
| StateTree | 长流程拓扑和有界等待 | 不填写世界岸点；不做几何、耗材或 Field 结算 |
| 表现 Actor / Niagara | 水面、边界、圆环、气泡和鱼影 | 无 Gameplay Collision、无 Nav 影响、无权威裁决 |

## 5. 关卡编辑模型

### 5.1 WaterRegion

`ACatWaterRegion` 表示一个稳定的水体与生态身份，至少配置：

```text
RegionId
WaterSurfaceZ
WaterPointVerticalToleranceCm
BankHeightToleranceCm
BoundaryToleranceCm
MaxLandingCorrectionCm
MinimumWaterInsetCm
BoundaryActors[]
```

第一版允许 Actor 的 Yaw，但要求 Pitch 和 Roll 为零。水面是该 Region 局部 XY 平面上的常高平面。明显存在水位落差时，关卡作者创建新的 RegionId。

### 5.2 BoundarySplineActor

每个 `ACatWaterBoundarySplineActor` 持有一条 `USplineComponent`，并配置：

```text
BoundaryId
Operation = Include | Exclude
ClosedLoop = true
OwningRegion
```

采用独立 Boundary Actor，而不是在一个 Region 中硬编码多个 Spline Component，理由是：

- 多个 Include、岛屿和禁钓洞可以独立选择、隐藏和编辑。
- Boundary 数量不会改变 `ACatWaterRegion` 的组件结构。
- Region 继续作为生态和版本身份，Boundary 只作为几何输入。

Region 使用显式 `BoundaryActors[]` 引用，不通过名称或全世界扫描猜测归属。Boundary 的反向 `OwningRegion` 只用于编辑器校验，二者必须一致。

### 5.3 拓扑约束

- 至少存在一个 Include。
- 每条轮廓至少包含 3 个去重后的有效顶点。
- 单条轮廓不得自交。
- Include 之间不得相交或相互覆盖；允许互相分离。
- Exclude 必须完整位于且只位于一个 Include 内。
- Exclude 之间不得相交、覆盖或嵌套。
- 不同 WaterRegion 若同时通过高度容差且二维有效区域重叠，查询结果为 Ambiguous，并在编辑器校验中报错。
- 分叉河流应画成一个合法凹轮廓或拆成多个不相交 Include，不允许用自交闭环表达。

有效水域定义为：

```text
InsideWater(P) = P 位于任意 Include 内，并且不位于任何 Exclude 内
```

岛屿和禁钓洞的边界同样属于岸线，参与最近岸点和 NearShore 计算。

## 6. 编辑期烘焙与 Geometry Revision

### 6.1 采样

样条是编辑输入，运行时查询使用烘焙后的量化折线。采样同时满足：

- 单段最大长度不超过 `MaxSampleSegmentLengthCm`。
- 曲线相对弦的最大偏差不超过 `MaxChordErrorCm`。
- 相邻量化点重复时去重。
- 首尾闭合点只存一份，闭合边由缓存隐式补齐。

默认建议值为：

```text
MaxSampleSegmentLengthCm = 100
MaxChordErrorCm = 5
BoundaryToleranceCm = 2
```

这些是 Authoring Settings，不进入玩法 DataAsset。修改它们会产生新的 Geometry Revision。

### 6.2 缓存

私有 `FCatWaterGeometryCache` 至少包含：

```text
RegionId
GeometryRevision
WorldToPlane / PlaneToWorld
IncludePolygons[]
ExcludePolygons[]
Bounds2D
WaterSurfaceZ
Vertical tolerances
Sampling settings
```

缓存不通过公共 Snapshot 复制给 Session。服务器和客户端从同一关卡烘焙数据加载；客户端副本仅用于预览，服务器副本才具有授权意义。

### 6.3 Revision 生成

`GeometryRevision` 是相等性令牌，不用于大小排序。编辑期烘焙器对以下数据按稳定顺序量化并生成非零正 `int64` 摘要：

- RegionId。
- 按 BoundaryId 排序后的 Operation 和顶点数组。
- 水面高度与全部容差。
- 采样设置。

运行期读取烘焙缓存，不重新从浮点样条猜测另一个版本。编辑器保存或 Cook 前若样条源与烘焙摘要不一致，验证失败并要求重新烘焙。

## 7. 水域空间查询

### 7.1 Handle

公开几何身份统一使用：

```text
FCatWaterRegionHandle
- RegionId
- GeometryRevision
```

调用者不得长期持有 Boundary Actor、Spline Component 或缓存指针。

### 7.2 查询结果

`FCatWaterSpatialResult` 至少返回：

```text
WaterRegionHandle
Containment = Outside | Boundary | Inside
SignedDistanceToShoreCm
NearestShoreWorldPoint
WaterwardDirection
WaterSurfaceWorldPoint
WaterSurfaceNormal
VerticalDeltaCm
```

统一约定：

- `SignedDistanceToShoreCm > 0` 表示有效水域内部。
- `SignedDistanceToShoreCm < 0` 表示水域外部或 Exclude 内部。
- `abs(SignedDistance) <= BoundaryToleranceCm` 返回 `Boundary` 和零距离。
- `WaterwardDirection` 永远指向有效水域。外岸向 Include 内部，岛屿岸向 Exclude 外部。

### 7.3 QuerySubsystem API

`UCatWaterQuerySubsystem` 是唯一公共查询入口，正式提供：

```text
QueryWaterPoint(WorldPoint, ExpectedHandle)
QueryShoreRelation(WorldPoint, OptionalRegionId)
ResolveRayToWater(WorldRay, ExpectedHandle)
FindRegionById(RegionId)
```

查询顺序：

1. 通过 Region 注册索引和 Bounds2D 粗筛候选。
2. 检查水面高度或指定查询模式的高度容差。
3. 做 Include/Exclude 点包含测试。
4. 遍历候选边界线段，求最近岸点和有符号距离。
5. 多 Region 同时合法时返回 `AmbiguousRegion`，不猜优先级。

Region 在 BeginPlay 注册、EndPlay 注销；不再由每个查询遍历 World 中全部 Actor。

多个线段到查询点距离相同时，按 `BoundaryId`、再按烘焙 Segment Index 决定最近岸点，保证服务器重放得到相同方向。

### 7.4 岸边玩法语义

- 鱼进入 `NearShore`：鱼 Actor 的服务器 Transform 位于水内，且 `0 < SignedDistance <= NearShoreWidthCm`。
- 猫成为岸边交互候选：猫位于岸外，且 `-BankInteractionWidthCm <= SignedDistance < 0`。
- 猫是否能抛竿或抄鱼，还必须通过服务器地面 Trace、坡度、角色状态、距离和 LOS 校验。
- 二维岸距不会把桥面、悬崖或地下空间自动判成可站立位置。

## 8. 抛竿与打窝落点

客户端只提交候选输入。服务器执行：

```text
客户端准星射线在本地解析出的候选世界点
→ 服务器把候选 XY 重新投影到 Region 水平面
→ 验证 Expected RegionId + GeometryRevision
→ Include/Exclude 判定
→ 验证 canonical RodTip / 角色到落点的距离
→ 验证 LOS 与玩法 Gate
→ 返回服务器修正落水点
```

若候选点只在 `MaxLandingCorrectionCm` 内越过边界，服务器可以把它投影到最近岸点并沿 `WaterwardDirection` 推入 `MinimumWaterInsetCm`。超过容差、落入岛屿或禁钓洞时直接拒绝。

表现覆层或临时 Trace 的命中永远只是候选。命令只携带 `ClientCandidateWorldPoint`；服务器还要用 canonical RodTip、服务器 ControlRotation 的允许夹角、距离和 LOS 复核，不读取客户端碰撞结果作为最终事实。

## 9. 空间窝料 Field

### 9.1 权威状态

每次正式投放创建一条 `FCatChumFieldState`：

```text
FieldId
RegionId
ChumDefinitionId
CenterWorldPoint
RadiusCm
Contribution: FCatChumVector
StartServerTime
ExpireServerTime
Source = Player | NaturalEvent
OwnerStableNetId（仅审计需要时保留）
```

一个 Placement 命令创建一个 Field。`Quantity > 1` 时保持 Radius 和 Duration 不变，使用 `BaseContribution × Quantity` 作为本 Field 的基础三轴，并原子消耗对应数量；不生成多个位置相同的权威 Field。

Field 不包含 Tick 状态。是否生效由服务器当前时间与不可变 Start/Expire 时间计算。

### 9.2 DataAsset 配置

窝料继续使用 `UCatEquipmentDefinition` 作为库存与消耗身份；Category 为 Chum 的定义内嵌 `FCatChumInfluenceSpec`，至少提供：

```text
ChumDefinitionId
RadiusCm > 0
DurationSeconds > 0
BaseContribution 三轴非负且至少一轴为正
DistanceFalloffCurve
TimeFalloffCurve
MaximumQuantityPerPlacement
PresentationClass 或 PresentationId（可选）
```

曲线输入统一归一化到 `[0,1]`，输出必须为有限非负值。非法定义 fail-closed。

### 9.3 局部采样

`SampleChumAtPoint(P, RegionHandle, ServerTime)`：

1. 验证 `P` 仍位于指定 Region 的有效水域。
2. 只读取同 Region、未过期且半径覆盖 `P` 的 Field。
3. 按 FieldId 稳定排序，避免浮点累加顺序在重放时漂移。
4. 对每个 Field 计算：

```text
Contribution(P)
= BaseContribution
 × DistanceFalloff(Distance / Radius)
 × TimeFalloff(Age / Duration)
```

5. 返回：

```text
FCatChumSample
- RegionHandle
- ChumFieldSetRevision
- SampleServerTime
- EffectiveChumVector
- ContributingFieldCount
- ContributingFieldIds（仅服务器诊断或测试）
```

靠岸 Field 的圆形可以延伸到陆地，但陆地点永远不会通过第 1 步，因此不产生效果。无需计算圆与水域的几何并集。

### 9.4 饱和吸引

原始三轴仍然线性叠加。鱼种对窝料的吸引得分由鱼定义的非负偏好向量与局部样本计算，再通过配置化饱和曲线：

```text
RawAffinity = Dot(EffectiveChumVector, FishChumPreference)
ChumModifier = SaturationCurve(RawAffinity)
```

`SaturationCurve` 由 Fish Selector 的统一设置持有，必须单调不减且具有上限；鱼种定义只保存三轴偏好向量。这样可以防止同一点堆放大量窝料把稀有鱼概率推到必然命中，同时避免每个鱼种维护一条难以比较的曲线。

鱼饵不进入三轴向量：

- 窝料决定某个空间位置吸引哪些鱼。
- 鱼饵决定候选鱼是否愿意咬钩及其权重修正。
- Region、时段、天气和稀有度 Gate 继续独立生效。

## 10. Field 版本、预算与索引

- `ChumFieldSetRevision` 只在 Field 创建、显式移除或清理过期记录时递增。
- 时间衰减不每帧推进任何 Revision。
- Placement 命令不携带 Expected `ChumFieldSetRevision`；远处玩家新增 Field 不应让当前玩家无意义冲突。
- 若存在 `MaxActiveFieldsPerRegion` 或总贡献预算，Subsystem 使用私有 `BudgetRevision` 做预留和提交；它不冒充几何版本。
- Field 查询先按 Region 分桶。第一版可以在桶内线性筛选；公开 API 与存储隔离，数量增长时可替换为空间 Hash 而不改 Fishing 消费者。
- 过期效果在 `ExpireServerTime` 立即为零，清理可以使用低频批处理。清理时才移除记录并推进 FieldSetRevision。

## 11. 打窝事务

正式命令：

```text
FCatPlaceChumCommand
- RequestId
- ExpectedWaterRegionHandle
- ExpectedEquipmentRevision
- ChumDefinitionId
- Quantity
- ClientCandidateWorldPoint
```

身份从 owning PlayerController / PlayerState 派生，不允许客户端自报 StableNetId。

成功结果：

```text
FCatPlaceChumResult
- RequestId
- bCommitted / Error
- FieldId
- WaterRegionHandle
- ServerCorrectedCenter
- StartServerTime / ExpireServerTime
- EquipmentRevision
- ChumFieldSetRevision
```

服务器事务顺序：

```text
验证命令身份与重放
→ 解析并验证落点
→ 预留 Equipment 数量与 Field 预算
→ 创建不可见 Pending Field，并取得保证可激活的 Field Commit Token
→ 提交 Equipment 消耗
→ 以 Commit Token 执行不会再失败的内存状态切换，发布 Active Field
→ 缓存首次终态结果
```

Equipment 提交之前的任一步失败都释放预留并移除 Pending Field。Field Subsystem 只有在完成容量、ID 和索引预留后才返回 Commit Token，因此 Equipment 已提交后的 Active 切换不得再执行可失败验证。Field 只有在耗材提交成功后才进入查询和复制列表。同一身份和 RequestId 的重放返回首次终态，不重复消耗或创建 Field。

旧 `ServerContributeChum(ACatWaterRegion*)`、`ExpectedAggregationRevision` 和 Region 共享 `FCatAggregationResult.ChumPool` 不再是正式协议。

## 12. 与 Fishing 的集成

### 12.1 Session 创建

正式 BeginCast 使用服务器修正后的落水点查询 Region，不再使用 Character 脚下位置。Attempt 冻结：

```text
FishingSessionId
CastAttemptId
WaterRegionHandle
ServerCorrectedLandingWorldPoint
Rod / Bait / Equipment revisions
```

Session 不复制边界顶点、Bounds 或共享 ChumPool。

### 12.2 鱼种选择

鱼不在 Session 创建时立即选择。服务器产生正式咬口时：

1. 在权威 Hook/落点位置采样当前 `FCatChumSample`。
2. 冻结本次样本、鱼饵定义、Region、时段、天气和确定性随机种子。
3. 执行鱼种 Gate 和权重计算。
4. 选择结果写入 Attempt，后续 Field 变化不改变已咬钩的鱼。

这保证命令重放和网络延迟不会重新抽鱼。

### 12.3 NearShore 与 Scoop

- FishEncounter Actor Transform 是鱼位置唯一真相。
- Motion / AutoHaul 每个权威步通过 `UCatWaterQuerySubsystem` 校验 Region Handle。
- 进入 NearShore 前查询鱼当前位置，必须在水内且岸距不超过配置阈值。
- StateTree 只请求进入 NearShore，不再携带可编辑世界坐标。
- Scoop 时重新读取 FishEncounter Transform、岸距、GeometryRevision、猫岸外关系、Reach 和 LOS。
- Geometry Handle 失配或 Actor 丢失时 Session 直接 Invalidated，不降级到旧 AABB。

## 13. 自然聚鱼

自然事件若继续产生聚鱼效果，必须提供：

```text
RegionId
CenterWorldPoint 或确定的位置 Provider
Radius
Duration
Contribution
Stable Event Identity
```

它通过与玩家窝料相同的 Field 创建入口，只把 Source 标记为 `NaturalEvent`。没有明确空间来源的“整条河增加 ChumPool”命令被拒绝，不保留旁路。

## 14. 表现层

### 14.1 WaterRegionPresentationActor

可选 Blueprint 表现 Actor 接收只读的边界采样数据和 GeometryRevision，可实现：

- 临时透明水面。
- 编辑器填充和岸线调试。
- 瞄准时可钓区域高亮。

约束：

- 默认 `NoCollision`。
- `SetCanEverAffectNavigation(false)`。
- 不复制权威状态。
- 不向 QuerySubsystem 写回几何。
- 被删除或加载失败时不影响玩法查询。

如果需要填充凹多边形与洞，表现层可以三角化烘焙轮廓，但三角化结果不参与服务器 Contains 或岸距计算。

### 14.2 ChumFieldPresentationActor

权威 Subsystem 通过正式 GameState 上的 `UCatChumFieldReplicationComponent` FastArray 发布轻量 DTO。客户端根据 DTO 创建或复用表现 Actor/Niagara：

```text
FieldId
PresentationId
Center
Radius
Start / Expire time
可公开的强度档位
```

玩家进入打窝/瞄准状态时临时显示圆形范围。第一版允许圆形视觉跨到岸上；玩法效果仍按水域查询裁剪。若以后需要精确视觉裁剪，可以从相同 Geometry Cache 生成材质 Mask 或表现 Mesh，不改权威模型。

## 15. 失败与诊断

以下情况 fail-closed，并产生结构化错误和编辑器诊断：

- RegionId、BoundaryId 缺失或重复。
- 无 Include、未闭合、少于 3 点、非有限点、自交。
- Include/Exclude 拓扑不合法。
- 水平面、容差或采样设置非法。
- 源样条与烘焙 GeometryRevision 不一致。
- 多 Region 查询歧义。
- 过期 Geometry Handle。
- 落点在陆地、岛屿、禁钓洞或超出高度容差。
- Chum Definition、Quantity、半径、持续时间、曲线或三轴非法。
- Equipment 预留、Field 预算或最终耗材提交失败。

运行期不自动修复自交，不自动选择重叠 Region，不退回 AABB，也不通过最近 Actor 猜 Region。

## 16. 网络模型

- 静态 Boundary Spline 和烘焙 Geometry Cache 不进行运行时属性复制。
- 客户端本地几何只服务瞄准和表现，所有命令由服务器重查。
- Field 权威状态位于服务器 Subsystem。
- 只复制表现所需的 Field DTO，不复制私有预算、幂等缓存或鱼种选择输入。
- Fishing 命令结果继续通过 owning PlayerController 的 Reliable Client Result 通道返回。
- 第一轮网络门禁必须包含 Dedicated/Listen Server、拥有客户端和非拥有客户端，验证 Field 结果不会广播给无关玩家。

如果未来必须运行期修改岸线，应另立设计：复制版本化控制点、完整验证后原子切换缓存。该能力不在本规格范围内。

## 17. 现有代码迁移表

| 当前位置 | 迁移方向 |
|---|---|
| `Environment/CatWaterTypes.h` | 用 Region Handle、Spatial Result、Chum Field/Sample DTO 替换 AABB/共享池语义 |
| `Environment/CatWaterRegion.h/.cpp` | AABB 降为缓存粗筛；增加 Boundary 引用、烘焙缓存和几何验证；删除共享 ChumPool 写口 |
| 新增 `Environment/CatWaterBoundarySplineActor.*` | 单个 Include/Exclude 的编辑输入 |
| `Environment/CatWaterQuerySubsystem.*` | 注册 Region；提供点、岸线和射线查询；不再逐查询遍历 World |
| 新增 `Environment/CatChumFieldSubsystem.*` | Field 真相、预算、幂等、过期和局部采样 |
| 新增 `Environment/CatChumFieldReplicationComponent.*` 并挂到正式 GameState | FastArray 表现 DTO |
| `Equipment/CatEquipmentDefinition.*` | 为 Category=Chum 增加 `FCatChumInfluenceSpec`：半径、持续时间、曲线、数量和表现配置 |
| `Fishing/Integration/CatFishingCommandTypes.h` | `ContributeChum` 改为空间 Placement 命令与 Field 结果 |
| `Framework/Game/CatGameplayTypes.*` | 移除 WaterRegion Actor 客户端参数和“角色脚下 Region”逻辑；增量转发正式命令 |
| `Fishing/CatFishingTypes.h` | Attempt 只冻结 Region Handle 与服务器落点 |
| `Fishing/CatFishingService.*` | 从服务器落点创建 Session；正式咬口时采样 Chum + Bait |
| `Fishing/CatFishingSession.*` | 移除 NearShore AABB；从 Fish Actor Transform 查询岸距 |
| `Fishing/CatFishingStateTreeNodes.*` | 移除可编辑 NearShore 世界坐标，只保留阶段意图 |
| `Environment/CatEnvironmentSettings.*` | 自然聚鱼要求显式空间 Field 输入 |
| Fish Catalog / Selector | 加入局部 ChumSample、Bait 和饱和曲线权重 |

当前 Content 和 Config 中尚未发现正式 WaterRegion、StateTree 或 Chum DataAsset 的已序列化依赖，因此这次主动破坏 Phase A 原型协议不需要迁移既有正式资产。用户已有 Content、Config 和 Blueprint 工作不在本规格写入范围内。

## 18. 测试与验收

### 18.1 几何单元测试

- 默认 Region fail-closed。
- 合法凸、凹 Include 的内外判定。
- 一个与多个 Exclude 的洞判定。
- 自交、未闭合、重复 ID、非法点和错误拓扑拒绝。
- Boundary 容差和零距离分类。
- 外岸与岛屿岸的最近点、距离和 WaterwardDirection。
- 高度容差、两个高度错开的 Region 和同高度歧义。
- Bounds 粗筛不会改变最终多边形结果。
- 相同烘焙输入产生相同非零 GeometryRevision，任一配置变化产生不同 Revision。

### 18.2 Chum 单元测试

- 单 Field 中心、边缘和范围外采样。
- 两个 Field 重叠稳定相加。
- 不同 Region 永不叠加。
- Exclude 和陆地点不产生贡献。
- 距离、时间衰减以及过期立即归零。
- FieldId 排序保证确定累加。
- 饱和曲线单调、有上限且不会把稀有鱼推到必然命中。
- 清理过期记录只推进一次 FieldSetRevision。
- 远处新增 Field 不使 Placement 产生无关 RevisionConflict。

### 18.3 命令与事务测试

- 客户端伪造 Region、Revision、落点和 Actor 指针被拒绝。
- 小容差岸边修正和明显越界拒绝。
- Equipment 预留失败不创建 Field。
- Field 预算失败不消耗窝料。
- 耗材提交失败不发布 Pending Field。
- 同 RequestId 重放不重复扣除或创建 Field。
- 同身份不同 RequestId 可以创建不同 Field。

### 18.4 Fishing 集成测试

- 抛竿用服务器落水点，不用 Character 脚下位置。
- 真咬时冻结 ChumSample + Bait，后续 Field 变化不重抽鱼。
- 鱼在水内 NearShore 带、猫在岸外交互带时才允许 Scoop 候选。
- StateTree 无法提交世界坐标绕过几何。
- Geometry Revision 失配、Fish Actor 丢失和跨 Region 移动使 Session Invalidated。
- 移除所有表现 Actor 后权威结果不变化。

### 18.5 网络测试

- Dedicated Server、拥有客户端、非拥有客户端的 Placement Result 路由。
- Field FastArray 新增、过期和移除不重复表现。
- Listen Server 本地表现与远端客户端使用相同 DTO。
- 无关客户端不能看到 owner-only 失败原因或 Equipment Revision。

## 19. 编辑器工作流

代码与自动化通过后，关卡作者按以下顺序操作：

1. 放置一个 `ACatWaterRegion`，配置 RegionId 和水平面。
2. 放置一个或多个 `ACatWaterBoundarySplineActor`，选择 Include/Exclude，并加入 Region 的显式数组。
3. 闭合并编辑样条。
4. 执行 Validate/Bake，解决所有拓扑错误并生成 GeometryRevision。
5. 可选指定 WaterRegion Presentation Blueprint。
6. 创建 Chum Definition，填写半径、持续时间、三轴和衰减曲线。
7. 可选指定 Chum Presentation Blueprint/Niagara。
8. 在测试图验证水内、水外、岛屿岸、外岸、打窝重叠和临时显示。

不要求关卡作者创建碰撞覆层，也不要求把最终水面 Mesh 对齐成玩法真相。

## 20. 实施顺序约束

后续实施计划必须按以下依赖顺序拆分：

1. Geometry DTO、Boundary Actor、烘焙校验和纯查询测试。
2. WaterRegion 与 QuerySubsystem 切换到 Geometry Cache。
3. Chum Definition、Field Subsystem、预算/幂等和采样测试。
4. Placement 命令与 Equipment 原子协调。
5. Fishing BeginCast、正式咬口选择和 NearShore 接入。
6. Field FastArray 与 Blueprint 表现入口。
7. 编辑器资产、地图配置和三端网络验收。

旧 AABB 与共享 ChumPool 只能作为迁移期间的短暂编译接缝；新路径通过测试后必须删除，不能长期以双模式存在。
