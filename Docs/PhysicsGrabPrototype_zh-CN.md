# 物理抓握原型使用说明


## 2026-09-11：普通身体互推与斜坡分离

本轮基线 `2878dd6`。源码、资产均无已有未提交改动；根目录一份未跟踪的裁决同步文档保留。本轮未修改二进制资产或输入配置。用户反馈不伸手无法推动朋友、坡面推挤卡住；现有日志 `Saved/Logs/Catfishing.log` 的 `model_contact_push` 已观察到 20–34cm 重叠。最近个人体力接入把身体触碰也当成满力量主动站稳；原模型接触只提供重叠后推力，没有实体分离纠正。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 身体接触与防穿透 | `Source/Catfishing/Character/CatCharacterMovementComponent.cpp::UpdatePeerPushContacts`→`Source/Catfishing/Interaction/CatModelContactComponent.cpp::FindPeerContact`；模型角色互相忽略胶囊 | 仅重叠后加力，缺少实体分离；角色会穿入 | 保留真实模型表面，接触力查询使用3cm沿双方地面高差方向的近接触余量来跨过小幅动画/分离间隙，实际穿入纠正仍使用零余量；移动完成后沿 CMC 地形扫掠纠正穿入；一对角色按逆质量分配原有电机反力，避免把推者驱动力全部抵消 | 原模型查询→CMC接收→快照 | 两正式猫不伸手互推、反向运动、贴墙；远距不产生空气墙 | `Report-20260911-142656-025` 定向24/24通过，含24组模型/地形场景；联机结果见下文 |
| 普通接触与抓握支撑 | `Source/Catfishing/Character/Physics/CatPhysicalBodyComponent.cpp::CaptureDriveSample/SetExternalForceFromAuthority`→地面积分与个人努力结算 | 空闲身体触碰也请求全力量站稳 | 外力源增加只供内部使用的身体接触标记，移动/预测样本内显式 `bPassiveBodyContact`。仅纯身体触碰且无手抓/鱼竿/其他载荷时，空闲者被动让开；实际移动仍采用本人力量及费用，其他外力支撑保持 | 外力标记→移动样本→积分及努力位移 | 被动移动不收费；主动对抗及抓握/持竿支撑照旧，耗尽不恢复隐藏电机；贴墙时反向去穿透不得被算成成功前进抵扣体力 | 24/24定向通过，被推者保持60体力、双方松键0.5秒内速度<3cm/s且位移<15cm；不涉及存档或网络字段迁移 |
| 斜坡/退出/复制 | 模型 MTD→`ResolveModelPeerPenetration`→`MoveAlongFloor/SafeMoveUpdatedComponent`→原 `PublishPostPhysicsSnapshot`；原外力弱引用清理 | 倾斜法线简单水平投影，几何穿入没有按坡面清理 | 形状重叠决定是否接触，直立角色间使用根位置水平轴稳定推挤方向，避免逐骨骼法线跳变导向侧滑；沿该轴估算分离量，纠正经原胶囊扫掠/坡面/台阶，仍禁止把猫当落脚点；纠正记入 `TotalMotionCorrection`；正向/侧向几何纠正不充当主动前进，阻止穿入的反向纠正保留为未完成意图，避免贴墙免费出力 | 地形扫掠→位置样本/费用→原复制 | 上下坡、横坡、60/120Hz/卡顿；贴墙不穿墙；目标退出仍原清理 | `Report-20260911-142656-025` 定向24/24通过，含24组模型/地形场景；联机结果见下文 |
| 已抓连双方与松键 | `Source/Catfishing/Interaction/Grab/CatPhysicsGrabComponent.h` 原 `TractionReceiver`→实际接收身体；模型查询/CMC接触及 `CatLightPropNetworkTests` 的只读接收方检查 | 新实体分离会和已有抓握约束争抢位置；纯身体接触松键后也须及时刹停 | 将只读 `GetTractionReceiverForDiagnostics` 统一命名为生产 `GetTractionReceiver`（仅一项旧C++测试消费者，同步迁移，无蓝图入口）；已抓连双方沿用原接触法线/反力且不追加位置分离；普通接触样本区分是否仍有主动推者，全部松键时有限刹停且不产生被动支撑费用 | 只读接收方→模型识别连线→CMC选择唯一解算规则；原抓点/复制/清理不改 | 同环境旧基线三客户端通过，新版本曾失败，必须恢复；普通互推松键、相等力量对抗、力竭守恒 | `Report-20260911-142724-843` 三客户端与身体互推均通过；保留与基线相同的落地抓点复制失败，详见下文 |
| 资产/配置/表现/打包 | `/Game/Character/BP_CatCharacter`、`BP_CuteCatCharacter`→各自现有PhysicsAsset及最终姿态；动画/抓点消费者不改 | 需保持修复后的真实尺寸和原抓点 | 不改PhysicsAsset、骨架、脚部IK、WBP、输入、默认力量/重力/跳跃；无资产生成/Cook入口迁移、库存持久化写入 | 原资产直接运行 | 运行加载两种正式蓝图、手抓与跳跃；二进制隐藏图未逐个审计，不删除兼容入口 | `Report-20260911-142656-025` 定向24/24通过，含24组模型/地形场景；联机结果见下文 |
| 测试/日志/文档 | `Interaction/Tests/CatModelContactTests`、`Character/Tests/CatUprightMovementTests`、`AbilitySystem/Tests/CatPhysicalEffortTests`及 `Editor/Character/Physics/Tests/CatPhysicalCharacterNetworkTests.cpp::FVerify`（客户端W→权威身体互推→双端位置→原抓握） | 旧体力测试要求空闲同力量者完全顶住，不符合最新互推要求 | 改为双方主动反向用力时僵持；新增同力量空闲者地形推挤，不放宽费用或远距离接触要求；`model_contact_resolved`默认Log限频落盘 | 基线失败→修复复验→Editor/Game→独立提交 | contract/runtime_behavior/presentation_delivery分别留证；新包/正式地图真人体验不能以自动化代替 | 定向24/24及客户端互推通过；费用/搏鱼最终复验与主工程构建在下文记实，不删除既有失败 |


本轮证据（持续更新）：

- 基线 `Report-20260911-140405-825/index.json`：两正式蓝图、0/±25°、60/120Hz 共12场景，被推者位移全部为0，空闲者却发生体力扣除。该行为来自 `dcfd1dc` 接入的主动支撑语义，不是输入映射删除。
- `contract/runtime_behavior`：`Saved/Automation/PeerPush-20260911/Report-20260911-141745-318/index.json` 79/79通过（72 clean、7带警告）；包括24组正式模型坡面/20°横坡/100ms卡顿帧/墙体场景。未贴墙场景被推者位移129–287cm；贴墙终点由原CMC胶囊阻挡；被推者体力均保持60。直接测量Chaos形状MTD，最大几何穿入5.939cm；它与倾斜表面所需的水平纠正距离是不同量，测试分别记录，不放宽8cm的几何重叠上限。抓握耗尽拒绝、旧状态树/非模拟刚体警告及负向输入诊断仍在报告中，不称为零警告。
- 客户端真实W按键互推、抓人、焦点释放、独立相机转向和起停：`Report-20260911-141820-042/index.json` 中 `FormalClientViewGripForceAndFocusRelease` 通过。双端被推位移108.402cm，采样位置差0cm。此组合曾发现已抓连双方重复纠正的位置争用，现已修复，最新结果见下一条。
- `presentation_delivery`：已检查隔离客户端图片 `Saved/Validation/PeerPush-20260911/Saved/Automation/MovementResponse-20260910/Images/20260911-061916-formal-body-push-no-reach.png`，两只正式原猫直立且未伸手；这是受控场景，不能替代当前Showcase2地图真人手感、新Cook、DedicatedServer或Development包双端无-log落盘验收。角色模块保持未关闭。

- 最终互推/CMC/个人体力/物理身体定向：`Report-20260911-142656-025/index.json` 24/24通过（23 clean、1条力竭拒绝诊断），新增双方松键及时停止验证。
- 联机对照：隔离副本恢复本轮修改前的已合并提交 `2878dd6` 的6份生产源码，`Report-20260911-142133-394/index.json` 中三客户端通过，抓竿组合因 `direct friend grip remains replicated after landing` 失败。修复重复分离后的 `Report-20260911-142724-843/index.json`：三客户端、客户端W互推/抓人/焦点释放均通过，抓竿组合仅保留同一条既有落地复制失败；抓竿反拉手点间距1.406cm（旧基线2.413cm），局部锚点漂移0，握点反拉已通过。最终身体互推双端位移110.083cm，位置差0cm。该既有复制缺口不在本次普通互推中扩大修复，也不将抓握组合标为整体通过。
- 开发日志：`LogCatPhysicsGrab` 的 `model_contact_push` 与 `model_contact_resolved` 默认Log、每角色至多每秒采样，携带World/NetMode/Authority/LocalRole及BodyId。几何字段明确为 `HorizontalSeparationEstimateCm`，避免将沿坡面水平分离估值误称为原始MTD深度；实际校验日志记录 `MaxShapePenetrationCm`。服务端与客户端观察见上述联机报告和对应 `Automation-*.log`。

- 最终原搏鱼回归：`Report-20260911-142946-870/index.json` 63/63通过（57 clean、6条既有诊断警告），没有改动主钓身份、个人费用、R取放、原搏鱼计算或抓点传力的业务入口。
- 构建：隔离 Editor Development 完整编译/链接成功（`BuildEditor-20260911-142648-752.log`）；正式工程 Game Development 成功（`BuildGame-Main-Final.log`）；正式 Editor 源码预编译成功（`BuildEditor-Main-NoLink.log`）。随后经用户明确授权，进程35672正常退出，主Editor完整链接（`BuildEditor-Main-Linked.log`）及最终Game构建（`BuildGame-Main-LinkedFinal.log`）成功。已重新打开原 `/Game/NaturePackage/Maps/Showcase2`；新进程2896加载主工程 `UnrealEditor-Catfishing.dll`，SHA256与路径记录于 `MainEditorReload.json`。七份玩法源码与已测试隔离副本逐字节一致，见 `FinalGameplaySourceComparison.json`；实际Showcase2试玩的新 `model_contact_push/model_contact_resolved` 事件见 `Showcase2-LiveContact.log`。
- 清理：原 `GetTractionReceiverForDiagnostics` 没有剩余C++引用，唯一旧测试消费者已经迁移；实际原生无模型测试仍使用胶囊回退，已抓连角色仍使用原软接触/握点约束，均为确认在用的消费者，保留对应分支。不修改或删除PhysicsAsset、输入、脚步IK、WBP、资产生成/Cook入口、持久化写口；保留用户根目录未跟踪文档。

- 追加贴墙费用修复：初版统一剔除几何纠正，会把被身体阻挡的前进当成成功进度。`Report-20260911-143614-912` 六个贴墙场景复现一秒仅扣0.057–0.067体力。现在反向纠正保留为受阻意图，正向/侧向纠正仍不制造主动进度；`Report-20260911-143725-229/index.json` 模型/个人体力/搏鱼71/71通过（64 clean、7条既有诊断），对应一秒扣1.943–2.132，符合2体力/米未完成意图的配置及实际小幅位移。未改力值、速度、抓握几何或扣费写口；此追加仅影响普通模型接触的费用进度，已抓连双方没有额外位置纠正。主工程最终两目标构建均包含该修复。


2026-09-11 当前正式猫的辅助地面驱动已接入本人 ASC 力量和意图缺失体力结算。抓握/钓鱼负载下松方向键仍有限站稳；仅身体触碰时，空闲者被动让开且不产生主动支撑费用。耗尽时主动出力为零，外部负载下禁止恢复。接触主动反力受各猫力量预算限制，30 N 只保留在被动碰撞部分；模型/骨骼数量不叠力。详见 [当前猫鱼/猫猫框架与验证](FishFightImplementationGuide_zh-CN.md)。下方带日期的原型和迁移说明为历史版本。

## 2026-09-10：按最终模型姿态抓握、互推及 CuteCat 空气墙修复

正式猫保留 CMC 胶囊处理地面、墙、台阶和直立移动。`UCatModelContactComponent` 在最终可见骨骼姿态之后更新 PhysicsAsset 查询形状；抓人使用这些形状上的局部接触点，普通猫使用原有 8 个身体，CuteCat 使用修正后的 4 个凸包。猫对猫胶囊扫掠互相忽略，服务器按真实形状重叠计算一次水平接触力，继续使用原 30 N 上限，不按骨骼数量叠加力量。客户端按相同组件名和骨骼重建接触面，GripId、世界厘米单位、伸手长度、抓握 RPC、退出清理不变。无模型的原生测试角色仍走原胶囊/方盒路径；这些是已确认消费者，因此保留该回退，不将其用于正常载入的两种正式猫。

本轮接入曾引入 CuteCat 空气墙：其原物理资产自动生成的四个胶囊半径、长度均为 0.505 骨骼局部厘米，骨骼世界缩放为 200，导致半径达到 101 世界厘米。首次测试仅验证 CuteCat 建体、跟骨骼与跳跃，遗漏实际接触距离；用户截图和隔离尺寸审计确认了这个错误。绿色框来自 UE `HUD.cpp` 的 `GetActorBounds(true)` 再扩张 10%，虽然框是调试显示，过大的形状确实参与了本轮新增互推，不能以关闭显示作为修复。

修复保留骨骼缩放与动画，按 LOD0 主权重顶点及最近已有物理祖先生成四个凸包，替换 `/Game/Characters/CuteCat/Meshes/SK_CuteCat_PhysicsAsset` 中过大的胶囊；骨名、资产路径、约束不变。编辑器入口 `UCatModelContactAuthoringLibrary::RefitCuteCatContacts(false)` 在临时对象预览，`true` 验证后保存，遇到已有脏资产拒绝覆盖。没有加入运行时拟合、模型硬编码缩放补偿或另一套接触力入口。引擎基础形状拟合的 0.5 局部厘米下限不适用于此导入单位；直接凸包 cooking 使用原蒙皮顶点，避免这一限制。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 入口与生命周期 | `Character/CatCharacter::BeginPlay` 初始化 `PhysicalVisual` | 模型原本无抓握表面 | 增加 `Interaction/CatModelContactComponent`；最终姿态后跟骨，退出释放抓握、移除忽略关系 | 可见姿态→模型形状→查询消费者 | 建体、骨骼单独移动、目标销毁 | 两正式 Blueprint 均运行验证；保留模型原有 NoCollision，使用专门查询形状 |
| 地形及接触力 | `CatCharacterMovementComponent::InitCollisionParams/UpdatePeerPushContacts` | 原胶囊间距驱动互推 | 正式模型形状重叠驱动一次成对水平力；胶囊继续负责地形 | 双方形状就绪后忽略对方胶囊 | 分离不推、碰触才推、站立与跳跃 | 原猫 30 cm 开始接触；CuteCat 62 cm，与渲染网格宽 62.637 cm 相符；CuteCat 72.637 cm 间距不位移 |
| 抓点、复制及清理 | `CatPhysicsGrabComponent::IsReachSurface`、既有 `TryLatch/GetGripWorldLocation/ReleaseTargetFromAuthority` | 原方盒和手球截取抓点 | 正式猫排除旧代理，接入稳定命名的模型组件；保留原 RPC/GripId | 单一抓握权威先验证再发布；目标退出立即释放 | 真正伸手、保留骨骼局部点、双方回执、焦点退出 | 双模型运行测试通过；正式客户端抓人链已验证，不修改权限或伸手距离 |
| CuteCat 资产与生成 | `SK_CuteCat` 默认 PhysicsAsset；`CatModelContactAuthoringLibrary::RefitCuteCatContacts` | 原 4 个最小胶囊经 200 倍缩放过大 | 原路径替换为蒙皮凸包，保留 4 个骨骼身份 | 临时拟合→尺寸核对→保存→重新载入验证 | 形状 cooking、实际世界范围 | `SaveContacts.log` 记录 Saved；前后资产与审计位于本轮证据目录 |
| 姿态与表现 | `CatPhysicsPrototypeVisualComponent::InitializeVisual`、现有 AnimationSource | DedicatedServer 原跳过可见姿态计算 | 权威无渲染端也计算接触所需最终骨骼；IK/动画规则不变 | 姿态先于接触 Tick | 行走、跳跃、抓握骨点 | 原猫/CuteCat 动画联机消费者通过；DedicatedServer 单独运行尚未验证 |
| 联机测试消费者 | `CatPhysicalCharacterNetworkTests`、`CatLightPropNetworkTests` | 原夹具瞄准旧方盒中心 | 改为侧面站位和实际模型表面，保留真实输入与原结果断言 | 形状就绪后发客户端输入 | 抓握、牵拉、HUD、失焦释放、抓人带起 | 最终报告结果见下；没有放宽物理力或臂长 |
| 配置、持久化、Cook、其他资产 | 模型默认 PhysicsAsset 引用；库存/Session/UI 均沿原调用链 | 不需要改库存、费用、存档、WBP、地图或 Cook 配置 | 只保存 CuteCat PhysicsAsset；编辑器拟合依赖只在 Editor 模块 | 现有模型资产引用带入打包 | 配置引用、编译、Cook/包内双端日志 | 无新增业务配置/资源扣费；本轮未运行 Cook 和新包验证，二进制外部消费者未穷举，原路径与骨骼接口均保留 |

基线保留用户 Skeleton、鱼配置、CuteCat ABP/材质、GameMode 修改；鱼竿与脚步的并行检查点未合入本次暂存内容。旧胶囊抓推仅在无模型的已确认测试/原型消费者保留；正式猫的旧方盒仍作为身体组件宿主供其他系统读取，但已从模型抓握入口排除，没有删除未确认的 Blueprint 引用。UI/动画/库存和多人钓鱼规则不因碰撞修复改变。

证据根目录：`Saved/Automation/ModelContacts-20260910`。`contract`：主工程 `BuildEditor-Main-Final.log` 成功；本轮 8 个生产源码 SHA256 与 `Saved/Automation/RodPark-20260910/MainGameSourceManifest.json` 的已成功 Game Development 构建一致。资产保存见 `SaveContacts.log`；修复前 `Automation-20260910-181803-126.log` 尺寸审计记录各接触体约 100–147 cm 半尺寸，修复后审计和真实重叠测试记录新范围。`runtime_behavior`：模型抓取与独立骨骼移动、销毁释放、地形跳跃、真实互推均按两种猫验证；最终报告另见下。`presentation_delivery`：已查看最终渲染轮次的 CuteCat 实体抓握截图 `Saved/Validation/ModelContacts-20260910/Saved/Automation/Locomotion/Images/20260910-103239-CuteReachSolidContact.png`；静态截图不能证明正式地图动态手感。重新打开编辑器即可载入保存的资产；新 Cook、Development 包无 `-log` 双端落盘、正式地图真人联机验收未完成，模块不关闭。

Development 默认日志分类为 `LogCatPhysicsGrab`，过滤 `model_contact_ready`、`model_contact_body_rejected`、`model_contact_unavailable`、`model_contact_push`、`physics_reach_surface`、`physics_grip_observed`。前者记录 World/NetMode/Authority/LocalRole、Actor、Mesh/PhysicsAsset/身体数量；接触力日志携带双方 BodyId、穿透厘米和力 N，最多每秒采样一次。新包最终应分别核查 `<打包根目录>/Catfishing/Saved/Logs` 房主/客户端日志，本轮编辑器证据不能替代它。

最终组合证据 `Saved/Automation/ModelContacts-20260910/Report-20260910-184539-830/index.json`（2026-09-10 18:46）：三项 `Catfishing.ModelContacts.Runtime` 全部通过，覆盖两种猫；`FormalClientViewGripForceAndFocusRelease` 通过。`LightProps.FormalRodReleaseAndSharedPull` 未通过：实际双方牵拉和房主/客户端短暂离地成立，但手球与移动抓点前轮最大偏差约 7 cm，最终轮次达到 12.799 cm，超过既有 5 cm 断言；抓胸部跳跃过程中实际臂距 64.699 cm 超过既有 64 cm 释放上限，发生 `ReachLimit`，因此落地后的持续持有断言失败。没有放宽阈值、延长手臂或禁用释放来消除这些失败。这两项是剩余未完成行为，不能把本次已验证的碰撞体积修复视为整个模型交互闭环完成。原固定方盒夹具已切到真实表面，剩余失败必须继续从手部更新时序与骨骼接触点运动分析；正式地图用户验证和新包验收同样保留。

## 2026-09-10：脚部锁定释放保持连续

CuteCat 移动抽动已复现为锁脚修正被一帧清零：`FCatQuadrupedLocomotion::Apply` 在偏移超过 `MaxPlantDriftCm` 或抬脚阶段退出锁定后，立即从落点返回动画/步幅目标。现每脚保存 `PlantOffsetWorld`，锁定期仍按原支撑物局部落点计算，释放期只衰减残留锁定修正；原动画继续正常推进，不平滑整个脚的世界轨迹。新落点继承未完成的过渡修正，`ClearPlants/Reset`、无地面、跳跃/失效及伸手优先分支清理状态。原8/3/5模型厘米上限、0.12秒混合时间及一次可见缩放不变；不改变关节长度、骨骼缩放、CMC/物理权威、RPC或资产。默认 `LogCatLocomotion::locomotion_pose_sample` 增加世界厘米 `ReleaseOffsetCm`，沿用原限频采样。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 动画消费者与计算 | `Source/Catfishing/Character/Physics/CatPhysicsPrototypeVisualComponent::RefreshVisualPose`→`Character/Animation/CatQuadrupedLocomotion::Apply`；两种正式BP共用 | 原锁脚释放瞬跳；原动画→脚部→伸手时序保持 | 在原求解器替换瞬时清除，保留PlantOffsetWorld渐退；未增加第二套IK | 先复现→替换原释放→实际ABP与CMC运行 | CuteCat 100/200/300cm/s、旧原型三速度；静止移动根模拟连续失锁 | 最终真实CuteCat三速度通过；100cm/s释放修正4.8571→0.5265模型cm；原三速度减滑回归通过 |
| 生命周期/支撑 | `FFoot/ClearPlants/Reset/Apply`；Body只读接地、ResetEpoch；前脚ReachAlpha排除 | 支撑物局部锁点、脚距/骨盆上限与退出契约保持 | 新过渡状态随既有清理；只衰减失锁修正，不平滑稳定落点 | 跳跃/传送/禁用/伸手→退出；有效落地再获取新支撑 | 移动平台、台阶、坡面法线、骨长与缩放、左右手 | 174202报告的TerrainPose/GrabJumpReset/骨长与实际抓握均通过；30/60/120Hz两骨架释放步长均<2模型cm |
| 物理/网络/UI/持久化 | Body/CMC唯一移动权威；Visual只写最终骨骼，Hand CCD最后运行 | 无新增力、碰撞、复制、存档、扣费或会话状态 | 保留原入口；不涉及配置、WBP、资产生成迁移或Cook入口修改 | 动画后读取物理结果，再输出可见姿势 | 正式Listen/Client脚点与现有抓人抓杆 | CuteCat客户端305个连续支撑样本，原动画累计滑脚196.773cm、求解后5.833cm；旧猫双端335.913→91.008cm。握点、跳跃、Condition均通过 |
| 资产/日志/测试/旧入口 | `/Game/Character/BP_CuteCatCharacter`与`BP_CatCharacter`、公共模板；`CatFootContinuityTests.cpp`、`CatLocomotionNetworkTests.cpp`；本页与唯一差距清单 | 二进制资产与Rig配置不改；旧直接清除公式由连续性回归替代 | 实际载入两骨架；新增ReleaseOffsetCm诊断；旧代码路径已替换 | 独立源码摘要→Editor/Game→运行→截图→本表 | 不把编译或图片当作全地形/全速动作验收 | 基线/修复/最终报告及OwnedSourceManifest.json保留；原模块状态不关闭，无已确认无消费者的旧IK另行残留 |

分层证据：`Saved/Automation/FootReachStability-20260910/BuildEditor-Fix1.log`、`BuildEditor-FinalTests.log`及`BuildGame-Fix1.log`成功（contract）；`Report-20260910-174202-562/index.json`中33项通过，1个新增测试越出地板的夹具失败已修正，随后`Report-20260910-175446-412/index.json`最终4/4通过（runtime_behavior）。只有既有动画启动警告，无新增生产失败；两份报告合并覆盖36个不同测试的最终通过状态。CuteCat 60Hz固定姿势释放时单帧脚位移从5.0901降到0.7916模型cm，30Hz为1.4609、120Hz为0.4052；未放宽原骨长或脚点范围。

presentation_delivery：已查看真实客户端`Images/20260910-095526-CuteWalkingIK.png`及同轮两张伸手截图，画面中脚部正常接触且手接实体。截图不是动态连续性证明，动态结论来自逐帧测量。300cm/s下原有目标可达误差仍约6.89模型cm，本轮未以伸长腿骨掩盖它，不能据此宣称高速/大高差的全身补偿完成；原地转身换步、复杂地形站姿、正式地图真人手感与新打包双端落盘仍未验收。并行ModelContact/架竿接缝保留，由对应任务组合验证。

## 2026-09-10：伸手穿过交互范围、接触实体表面

基线 `62db64a`：`CatPhysicsGrabComponent::UpdateHand` 与 CMC 的 `RefreshKinematicHands` 都使用 Visibility 单次扫掠，商人/容器的 QueryOnly 交互范围先截短手部目标；随后 `TryLatch` 即使拒绝该范围，也无法找回后面的实体。现两处统一调用 `TraceReachSurface`：对象扫掠后由 `IsReachSurface` 选择最近的有效实体，跳过 UI 范围，并允许接触不阻挡 Visibility 的物理表面。最终抓握使用同一校验，QueryOnly 例外只限真实身体/手代理和 CMC 胶囊，不扩展到同 Actor 的交互组件。手球半径、伸手长度、世界厘米/GeometryScale、服务器 GripId 和原释放/回执不变。

`CatShopKioskActor`、`CatFishGuardActor`、`CatFishTankActor::InteractionCollision` 与 `CatInteractionSettings::TargetingTraceChannel` 保留；UI/WBP、库存、费用、Session、存档及 Cook 配置不涉及修改。两处旧 Visibility 抓取入口已移除，未增加第二条手驱动或抓握权威。`LogCatPhysicsGrab` 默认记录 `Event=physics_reach_surface`、World/NetMode/Authority/LocalRole、BodyId/Hand/GripId、实体和忽略数，仅在变化时且每手最多每 0.25 秒记录。精确七列影响盘点与处理结果保留在本任务对话中。

验证根目录 `Saved/Automation/FootReachStability-20260910`：修复前 `Report-20260910-173915-217/index.json` 9项中3项预期失败，包含实际 Kiosk + 容器查询盒挡手。修复后 `Report-20260910-174202-562/index.json` 34项中33通过，实体在交互范围后仍可双手抓住，Visibility 仍可命中原 UI 范围；唯一失败为新增300cm/s跑步夹具超出地板，已修正其持续时间，未修改生产运动参数。`BuildEditor-Fix1.log` 与 `BuildGame-Fix1.log` 均成功。最终 `Report-20260910-175446-412/index.json` 4/4通过（2 clean、2既有动画启动警告）：新 CuteCat 双端范围穿透/实体抓取/GripId回执/双手释放、原猫双端滑脚、修正后的CuteCat三速度、正式抓人/抓竿牵拉。该报告对应 `Automation-20260910-175446-412.log`；两端 `physics_reach_surface` 与 `physics_grip_observed` 已按World/NetMode核对。已查看真实客户端 `20260910-095531-CuteReachThroughInteraction.png`、`20260910-095532-CuteReachSolidContact.png`，手部与蓝色实体接触清楚；截图相机以脚和手为观察区，头顶部分出画，不能作为全身模型比例验收。

已保留用户 Skeleton、CuteCat ABP/材质、鱼配置和 GameMode 资产修改；本任务没有保存二进制资产。并行架竿 Parked 与 ModelContact 接缝由对应任务实现、验证并提交，独立隔离证据尚未包含它们。已列构建属于 contract，实际抓取属于 runtime_behavior；正式地图真人手感、新Cook/Development包以及不带-log的打包双端独立日志尚未验收，不据局部测试关闭模块。

## 2026-09-10：正式鱼竿 R 取放与架竿禁抓

正式鱼竿当前取放改为 R 拿出、R 原位架住、附近 R 拾回。架竿固定位置与角度、不自由下落，也不允许伸手抓取；放竿解除该竿全部手抓点。只有主控持竿时才允许抓竿协助，抓人/互推/短时带起保持。其他原型动态道具仍按25%重力轻落。实现影响表及本轮验证见[取放与架竿修复](FishingArchitecture_zh-CN.md)。以下各日期章节保留对应历史证据，早期自由落竿/助手脱手后抓竿描述不再是当前规则。


## 2026-09-10：抓握传递短时跳跃牵拉

用户追加确认：抓着朋友跳跃时，可以把对方短暂带离地面。基线`7d98c8c`；原CMC/搏鱼恢复的独立Editor、Game及主工程Editor构建成功，组合渲染212项中211通过、1项已确认的既有StarterRod耐久500/150失败。并行骨架/模型修改全部保留。新跳跃抓握已通过下述构建与运行回归；并行模型、骨架和动画源码不计入本次提交。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 起跳入口与生命周期 | `Character/Physics/CatPhysicalBodyComponent::RequestJump` 经已有ServerRequestJump/Epoch和DoJump权威裁决 | 原正常420cm/s起跳，角色抓握Z分量全丢弃 | 真正起跳成功后开放0.35秒垂直牵拉窗口，末0.08秒渐退；原起跳速度/重力保持。失效、传送、离手清旧力 | 成功起跳→握点力窗口→唯一CMC积分 | 空中重复按跳、乱序请求、释放重抓/传送不产生旧力 | 已接入0.35秒窗口/末0.08秒渐退；成功DoJump才开启，外部带起不续窗；释放、传送、目标退出的真实世界清力测试通过（161447报告）。 |
| 抓握双向传力 | `Interaction/Grab/CatPhysicsGrabComponent::ApplyTraction/ResolveConstraintTarget`，Body外力表按每手Contact为唯一来源 | 已有650弹性/24阻尼/每手10000UE限力，仅水平作用猫 | 双端CMC且任一端有起跳窗口时，让同一真实握点力的Z分量渐退后作用双方；抓主控杆仍解析到主控身体，非角色/固定表面不提供垂直悬挂力 | 双端/窗口确认→现有握点公式→相反力 | 抓人/抓杆跳起、后退、双手及环路不重复力；轻道具不压人 | 已接入；单手、双手及两猫互抓共4握点×60/120Hz×正常/120ms共12场，双方Z力和为0，朋友被带起21.30–24.56cm，落地后原握点继续后退拖动；逐手释放不丢其他握点。 |
| 双手慢帧稳定性 | `Grab::ApplyTraction`首次实际120ms双手测试将朋友弹起148/181cm，单手与常规帧正常；旧650/24力按帧初速度冻结 | 120Hz CMC细分不能修复整帧不更新的弹簧/阻尼力 | 垂直抓握采用包含本帧相对位移的隐式弹簧阻尼，按同一角色对的有效握点数计算共同质量响应；水平公式和每手独立来源保持 | 先只读统计双方有效握点，再解该握点平均垂直力，最后原CMC消费 | 保留原失败证据；双手120ms不弹飞；首轮0.25秒窗口在两猫错峰落地时导致剩余手越距，延长至0.35秒完成短跳回拉，仍末0.08秒渐退；逐手释放不丢另手、60/120Hz可比 | 已替换垂直力的整帧显式算法；161447报告12场全部通过，双手120ms不再出现148/181cm弹飞，既有32cm原生越距阈值未放宽。160700/160900失败报告保留。 |
| CMC垂直接收 | `Character/CatCharacterMovementComponent::AdvanceFromAuthority/CalcVelocity/NewFallVelocity`；Body::ExternalForces | CMC原过滤Z；PhysFalling会恢复CalcVelocity前的Z速度，不能在该函数重复加垂直力 | 外力记录标注允许垂直抓握；向上合力超过重力时进入Falling，垂直力只经NewFallVelocity一次积分；停止窗口后只剩正常重力，保持直立 | 标注外力→离地→原CMC下坠/落地 | 被抓者短暂离地、起跳者受反力、120ms/60/120Hz、无持续悬挂 | NewFallVelocity唯一积分垂直力，胶囊保持UpZ=1；常规跳跃75–100cm契约与固定表面不能悬挂回归通过。窗口结束只剩原重力，双方落地、无残余Z力。 |
| 预测与搏鱼衔接 | `CMC::CaptureMotionPrediction/AdvanceMotionPrediction`→`PhysicalRod::PopulateCMCEndpointPrediction` | 历史防抖预测必须跟上新增垂直抓握，不能让实际上移与预测分离 | 冻结同一允许垂直外力和地面离开门槛；空中改用与CMC一致的中点位移积分，地面提交的鱼线Z仍沿原队列过滤，空中鱼线Z由NewFallVelocity一次消费；鱼线仍唯一冲量队列、原主控费用和会话不变 | CMC规则→只读候选→原Simulator/Runner | 同竿助手跳起、主控跳跃、双主体历史短线不回归 | 地面离开/空中鱼线力×1/120、1/60、120ms的预测与真实CMC位置最大误差0.000005cm；历史短线6场继续无周期卸力；215项组合仅原耐久差异，无新增费用/会话/持竿失败。 |
| 网络/表现/清理 | Body Snapshot与客户端实际离地/速度驱动原Visual/ABP；Grab::ReleaseHand/EndPlay | 不新增第二套跳跃RPC或复制载荷 | 原服务器快照复制带起结果、原动画消费离地；默认日志记录垂直力窗口和离地。退出移除同一每手外力，窗口不递归传播 | 单一裁决→快照→现有消费者 | 客户端发起跳跃，服务器/客户端观察两猫；不生成吊挂链 | 161447正式Listen+Client实际RPC抓人、跳跃、释放及快照验证通过。抓主控杆的朋友服务器/客户端升高19.495/19.367cm；直接抓猫升高21.391/21.147cm，均观察Falling并落地。主控人数仍1、助手不接任。 |
| 测试、资产与文档 | 新增`Character/Tests/CatGrabJumpTests.cpp`；现有`Source/CatfishingEditor/Interaction/Grab/Tests/CatLightPropNetworkTests.cpp::FVerify`网络夹具；本页/唯一差距清单 | 新增行为需要实际轨迹及网络证据 | 原生与正式模型实测抓人跳跃/后退/双手/窗口结束；不改BP/动画/存档/输入配置或Cook入口，正式资产引用沿既有路径 | 局部→网络→受影响搏鱼→构建/提交 | contract/runtime_behavior/presentation_delivery分层；并行新模型仍独立验收 | 独立Editor/Game成功，161133组合215项中214通过、1既有StarterRod耐久失败；最后新增互抓4握点及观察镜头修正后161447专项3/3通过。正式截图已查看，本页及唯一差距清单同步；无资产保存、无输入/Cook/存档迁移。 |


本轮分层证据（根目录`Saved/Automation/UprightCMC-20260910`）：

- **contract**：`BuildEditor-GrabJumpDelivery.log`与`BuildGame-GrabJumpDelivery.log`均成功；生产源码与隔离副本摘要见`GrabJumpSourceManifest.json`。主工程Editor同样构建成功，记录于`BuildEditor-MainGrabJump.log`，该构建包含并行Rig源码，不属于本独立提交的源码证据。
- **runtime_behavior**：`Report-20260910-161133-433/index.json`为215项、214通过（191 clean、23 warning）、1既有耐久失败；之后仅补互抓4握点测试和观察镜头，`Report-20260910-161447-342/index.json`最终3/3通过（2 clean、1 warning）。实际日志对应`Automation-20260910-161133-433.log`与`Automation-20260910-161447-342.log`。默认`LogCatPhysicsGrab`记录`physics_body_jump`、`physics_body_grip_lift`、`physics_body_jump_snapshot`、`physics_body_snapshot_observed`及`physics_grip_traction`；以World/NetMode、BodyId、GripId、ControlEpoch关联，不依赖Verbose。`GetJumpTractionWeight`是服务器窗口，客户端只消费姿态/速度/接地快照。
- **presentation_delivery**：已人工查看实际正式猫的`Images/20260910-081519-formal-held-rod-grab-jump.png`与`Images/20260910-081525-formal-friend-grab-jump.png`，均来自真实客户端视口。初版直接抓猫截图未入镜，已在跳跃RPC前定位观察相机并重跑；未更改生产相机或动画。当前临时模型的原骨长/前爪表现保留；正式地图真人手感、互联网延迟/丢包下预测、新Cook/Development包不带-log的房主/客户端独立落盘尚未验收，仍挂原模块，不据本次局部回归关闭Fishing/Delivery。

交付行为：成功起跳后，角色之间的实际握点力可以短暂带起另一只猫，并反过来限制起跳者。抓主钓手握住的鱼竿也通过原接收方传力；静态场景和自由道具不会提供悬挂支撑。没有新增远端Jump命令、鱼会话成员、体力叠加或倒地身体模拟。原APawn试验场仍有已确认的Chaos消费者，因此保留；正式CMC不再消费它的自由翻倒/支撑代码。窗口默认0.35秒、渐退0.08秒属于本次交互参数，420cm/s跳速、重力和原角色力量预算均未改变。

## 2026-09-10：直立 CMC 与抓推迁移（检查点7d98c8c）

用户确认不需要悬挂、串联吊挂，正式角色保持直立，倒地由状态和动画控制。基线608b736；已有用户Skeleton修改保持SHA256=3AF77F901B4FD35EAF79A7133230BB8964A1F2E886064010F680A40C3DE18008。基线197项196通过，唯一既有StarterRod耐久150/500；首轮组合210项中207通过，当时剩余为四端旧位置夹具、需Render的RodBendRender及既有耐久差异；前两项已处理，最终212项211通过。期间出现并行CuteCat导入提交ffa23a5及16个Fish资产修改，均不属于本轮、不撤销或提交。开始时编辑器PID10024正在打开工程，使用Saved/Validation/UprightCMC-20260910隔离构建，不关闭或保存用户编辑器。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 正式身体和移动 | Source/Catfishing/Character/CatCharacter.cpp::BeginPlay、ConfigureCharacterMovementAuthority；旧Body::UpdatePhysicalMovement驱动刚体 | CMC原仅观察，身体可翻；目标胶囊直立、CMC扫掠/步行/跳跃 | 恢复胶囊根；CMC::AdvanceFromAuthority由Body PostPhysics唯一调用；普通速度/朝向契约保持，Pitch/Roll为0 | CMC接收→正式入口→快照；诊断APawn原型仍消费原Chaos运动，不在正式路径启用 | 正式BP/原生角色60/120Hz/120ms，侧撞不翻、坡面台阶、跳跃落地 | 已接入；原生/正式BP×60/120Hz的CMC真实起跳约89.99cm、胶囊UpZ=1。历史防抖追加验证见FishFightImplementationGuide顶部 |
| 抓点与推拉 | Interaction/Grab/CatPhysicsGrabComponent::UpdateHand/RefreshContact；原肩/接触关节驱动3刚体 | 正式手无需自由刚体；抓握仍须双向 | 正式保存局部握点和视角局部抓握偏移，650kg/s²弹性、24kg/s阻尼、每手10000kg·cm/s²限力，双方相反力；CMC接收水平牵拉，道具接收3D力。ReachLimit保持；无悬挂支撑 | 握点权威→每手独立力源→移动和道具，释放即清双方力源 | 抓猫/竿/固定表面、多手退出、销毁、越距，无收费或成员状态 | 已接入；四端真实抓猫/抓杆/逐手退出通过（Report-20260910-151605-781），手到握点保持<5cm；局部接触点未漂移。角色不悬挂测试通过 |
| 接触与轻道具 | CMC::UpdatePeerPushContacts/InitCollisionParams/IsWalkable；LightPropSubsystem现有接触策略 | 胶囊扫掠不得被道具顶起，普通角色仍能互推 | 胶囊加入原轻道具接触过滤；CMC查询忽略轻道具；Box保留抓取实体且忽略Pawn通道。相邻胶囊每对一次有限水平推力（上限3000kg·cm/s²）；手球忽略Pawn扫掠，轻道具注册/撤销同步胶囊移动忽略表 | 胶囊几何→查询与推力→道具 | 头顶释放、贴墙/贴地、被推拖动及轻落 | 已接入；两猫互推48.735cm，轻道具头顶/横向/手球接触与持竿释放通过；同杆相向移动实际传力且均直立。截图已人工查看 |
| 捕鱼端点/载荷 | Fishing/Integration/CatFishingPhysicalRodComponent::PopulateEndpointResponse/GetPointVelocity/TickComponent→Body；FightRunner消费原端点结果 | CMC身体不能再按IsSimulatingPhysics拒绝端点或直接AddForce | 有限4kg平移质量、无身体转动惯量响应，接地竖直响应0；原鱼线冲量队列一次交CMC，随后按用户要求恢复历史共同候选预测与不反向支撑；CMC增加0.8N被动地面阻力（独立于主控力量预算），对齐旧触地手球摩擦的玩法表现，避免零预算小鱼拖行无阻力；原转竿/搏鱼/预算/费用保留 | 端点反馈→冲量接收→原搏鱼回归 | 单人/多人、60/120Hz/卡顿、零预算、冲量与ASC写入一次 | 已接入；原30kg/0.04kg及零体力60/120Hz回归通过；用户追加要求后改为CMC候选运动/转杆联合预测和120Hz分配力，详见FishFightImplementationGuide恢复表 |
| 倒地动画 | Condition::OnSnapshotChanged；新增Condition/CatConditionPresentationComponent；Character只创建宿主 | 原停支撑倒地；目标原Condition状态控制行动、动画控制躺下/起身 | 只消费bDowned，使用现有Stand→Sitting→Lying及反向动画，经正式DefaultSlot输出；不新增倒地判定或改变资源写口 | 状态→动作清理→表现；身体支撑始终保留 | 下毒倒地/恢复、双端姿势、动画中断及资产加载 | 已接入；正式ABP实际躺下头高48.69→26.01cm，胶囊Z42.15cm不变；服务器/客户端倒地和起身回归及渲染通过，截图072016/072024 |
| 网络/清理/存档 | Body的ServerSetInput/ControlEpoch/Snapshot，Controller::Flush/StopMove，Save::TeleportBodyFromAuthority | 保留现有服务器模拟与客户端跟随；不让CMC默认Tick/ServerMove同时写位置 | 同一快照发布CMC结果与手点；传送清输入、握点、外力和CMC累计力；未增加预测/回滚或存档字段 | 单一移动写口→旧调用方回归 | 客户端实际按键、释放、失焦、退出、传送/存档恢复，默认落盘日志 | 已接入；服务器输入权威、四端控制与逐手状态复制通过。仍是服务器快照跟随，未实现CMC客户端预测/重放；新包双端落盘未验收 |
| 资产/动画/配置 | /Game/Character/BP_CatCharacter及/Game/Animalia/Cat/ABP_Cat，Visual→QuadrupedLocomotion；Rider只读CDO证据 | BP原胶囊局部scale2、radius15.5285、halfheight17.0681；Mesh局部Z=-20，速度100/跳速420/重力1/台阶45 | 保留Mesh/相机世界变换；胶囊改为13×GeometryScale半径、20×GeometryScale半高，对齐现有脚底；原骨架/IK算法/输入资产不改。旧组件保留因BP序列化和APawn诊断消费者；新动画为现有硬引用，无资产保存 | 原生接线→正式BP载入→画面；Cook入口不变，新引用需核查 | 正式模型比例/爪点/镜头/动作；骨架hash，Game构建；新包未运行 | 旧正式BP/原生模型与动画消费者已验证；保护骨架hash未变；未保存资产。并行CuteCat/公共动画模板不属于本提交；新Cook/打包未运行 |
| 测试/日志/文档 | Character/Physics/Tests、Editor/Character/Physics/Tests、Fishing/Tests及Build/Automation/verify_physics_grab_prototype.ps1；本页/差距清单 | 旧正式刚体断言需迁移，含Character/Animation/Tests与Editor/Character/Animation/Tests的正式IK权威断言；Editor/Fishing/Tests/CatFishingGroupNetworkTests的四端抓取夹具按胶囊和露出竿身放置，不能靠未抓住的手球反作用自动搬动身体；玩法契约不能随意放宽 | 替换已过时的模拟/翻滚契约，增加直立CMC/不悬挂和真实抓推回归。原关节间隙5cm迁移为实际手到握点间隙5cm；局部接触点漂移仍<0.1cm，肩部弹性误差另记。圆胶囊侧碰可能绕开偏置手球，逐步核查实体间距及偏转，不再用旧方盒平面回弹代替不穿透；默认日志记录新接收端及牵拉 | 隔离构建→运行→正式渲染→最终diff/独立提交 | contract/runtime_behavior/presentation_delivery分别记录；库存/存档格式/扣费入口不涉及修改 | 初始210项207通过，四端夹具及Render要求已在151605和151113报告定向补过；已检查正式躺下/双猫拉竿截图。最终154540报告212项211通过，唯一既有StarterRod150/500差异保留；独立Editor/Game及主工程Editor均通过 |

## 2026-09-10：起停响应与镜头独立朝向

用户要求起步/停步不漂移，普通侧移时猫转向移动方向，Controller继续独立控制镜头。基线19aa1d9，工作区仅用户Cat_Skeleton.uasset修改（SHA256=3AF77F901B4FD35EAF79A7133230BB8964A1F2E886064010F680A40C3DE18008）；上轮196项195通过，既有StarterRod耐久150/500差异保留。本轮主工程构建/运行与正式资产渲染已完成，具体结果及未验收范围见表后。证据根目录`Saved/Automation/MovementResponse-20260910`。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 自主移动计算 | `Source/Catfishing/Character/Physics/CatPhysicalBodyComponent.cpp::UpdatePhysicalMovement`由PrePhysics Tick调用；原普通电机VelocityError/0.22、上限450cm/s² | 起停慢、指数拖尾；目标普通无载移动快速达到目标/停住，同时保留真实碰撞及外力 | 原地替换为按实际物理步消除速度误差的有限驱动力（沿Chaos子步总时长上限与NetworkDeltaTimeScale，非子步才用MaxPhysicsDeltaTime），普通加/减速上限6000cm/s²；钓鱼仍沿原FishingMotorMaxForce上限、站定弹性/零预算契约，不直接清除刚体速度 | 先服务器公式→运行轨迹→联机观察 | 60/120Hz、0.12秒卡顿、100/300cm/s起停距离、真实推拉、零预算、鱼力与费用；力单位kg·cm/s²不改 | 正式/原型×60/120Hz×100/300cm/s×有无120ms首帧卡顿共16组通过；正式正常100cm/s松键距离0.50–0.57cm、300cm/s为6.56–6.62cm，100ms后水平速度<0.9cm/s；有限力仍可被外力拖动 |
| 已连接身体的自主力 | `Body::UpdatePhysicalMovement`与`Grab::GetGripState/GetGripTarget`；Report-Initial无鱼对拉中助手新电机把主控反拖193cm | 快速空载电机不能同时抬高朋友持续抓拉能力；目标普通自由行走快起停，已经抓住或被抓住继续原物理拉力 | 新增Body只读连接查询，含自身实际握点和他人指向本Actor的握点；非钓鱼真实连接恢复原0.22s/450cm/s²电机，主控原FishingMotorMaxForce仍单独权威；无新握点、成员或费用状态 | 实际Grip关系→每步选择唯一电机→原关节解算；松握自然恢复快速自由移动 | 原无鱼对拉方向/关节间隙、直接抓猫和被抓、最后释放恢复响应；不修改失败测试门槛 | Report-Final原无鱼对拉与抓猫用例通过；助手+X输入仍被主控向-X拉动约170cm；原关节/目标/GripId/力量上限保持。连接状态实际消费两种响应，不是无消费者旧分支 |
| 朝向目标/状态 | 同函数原Forward恒等于ViewInput水平朝向；`SetViewIntent`由Character::FaceRotation和Controller调用 | 原地镜头拖着猫转，侧移仍看镜头；目标普通走路面向MoveInput，停止维持最后朝向 | 维护唯一物理朝向目标；无瞄准普通移动读世界MoveInput，伸手/握竿仍按ViewInput瞄准；180度采用有符号角差避免叉积为0；转矩仍是唯一身体朝向写口 | 目标选择→有限朝向转矩；初始化/传送重置目标，离地/倒地原规则保留 | A/D/S/斜向移动、原地镜头90度、回头180度、伸手抓猫、主控鼠标/跳跃；不新增Controller旋转写口 | 原生侧移90度、后退180度、停步转镜头、主动伸手朝向均通过；正式实际D键服务器BodyYaw=89.932、ControlYaw=0，客户端身体与实际默认相机分离断言通过 |
| 输入/网络/退出 | `Framework/Game/CatfishingPlayerController.cpp::Move/StopMove`→Body::SetMoveIntent→ServerSetInput；PostPhysics Snapshot→客户端插值 | 原30Hz轮询增加起停延迟；输入和结果仍须单一服务器裁决 | 起停/换向边沿立即复用现有RPC，30Hz重发保留；边沿后物理快照及时发布，保留ControlEpoch/Sequence/超时与ClearControlIntent入口；收紧客户端跟随响应，不实施预测/回滚 | 请求→原裁决→物理结果→快照；失焦不增加第二清理路径 | 正式IP双端真实IMC按键/松键/失焦；默认Log记录输入边沿与快照，不在Tick刷屏 | 起停即时复用RPC并在下一物理结果快照ForceNetUpdate；微小模拟输入起停也发送。输入Sequence/ControlEpoch沿原门，客户端插值响应由20/s收紧到60/s；正式按键/松键/Flush与原抓握/镜头回归通过，未新增预测 |
| 表现与相关测试 | `CatCharacterMovementComponent::RefreshPhysicalObservation`→ABP/`PhysicsPrototypeVisual::RefreshVisualPose`→四足IK/手CCD；正式网络测试原要求原地转镜头身体也转 | 现有消费者继续读真实速度/身体姿态；旧测试绑定的镜头跟身假设需替换 | 不改动画/IK/镜头生产逻辑；原测试先验证镜头独立，再主动伸手转向原目标并继续真实抓拉；新增原生起停与普通朝向专项，正式双端验证 | 物理接收→已有消费者→更新过时断言 | 正式BP姿态、控制器视角、抓握HUD、轻道具和搏鱼组合回归；不只靠编译验收 | 正式渲染Report-RenderedFinal 5/5通过；默认相机方向断言通过后，用独立观察镜头截图完整猫的停步/侧移姿态，测量区间无截图阻塞。未改ABP、IK、骨架或镜头生产源码 |
| 配置/资产/存档/资源/Cook | 正式`/Game/Character/BP_CatCharacter`继承原生Body；`CatCharacter::BeginPlay`读取原CMC速度/跳跃/重力；原型Pawn直接Initialize；正式IMC/WBP与Cook入口不改 | 移动响应在唯一Body调整；旧CMC只作原配置/动画观察消费者，不重新启用 | 保留移动速度/跳跃/重力配置入口，无资产或存档迁移；库存/费用/Session写口不涉及修改；不删除未确认二进制引用 | 原生接线→正式BP加载→哈希核对 | Blueprint实际运行确认新路径；Skeleton保持hash；钓鱼原力量/账单与零体力测试 | 正式BP与原生共享Body已实际运行，速度/跳跃/重力仍读原配置；用户Skeleton hash不变。原搏鱼/费用/零预算回归通过，无BP/WBP/IMC/存档/资源/Cook入口修改；未运行新Cook/包 |
| 跳跃首帧/截图测时 | `Body::UpdatePhysicalMovement`在排队Jump AddImpulse后、首个Chaos步前以旧Velocity.Z≤0判断结束分离；Rendered日志空手首帧Grounded=1且顶点154.74cm，持竿124.56cm；正式网络截图调用阻塞导致计时段增加一慢帧 | 跳跃冲量尚未消费不能判为下落；截图不能人为污染起停比较 | 首次PostPhysics跳跃发布前禁止清除bJumpSeparating；新增真实120ms首帧专项；普通走停计时先完成，再用观察镜头独立截图，不放宽原起停距离门槛 | 请求排队→唯一物理步→首次快照→原正常下坠/支撑；定量计时→渲染观察 | 保持420cm/s/重力/支撑系数，首帧非Grounded、原75–100cm跳高；原跳跃和轻道具渲染再验证 | 独立检查点88a1ce7；120ms首帧跳高84.453cm且不接地，正常落地恢复；最终渲染空手/持竿顶点124.709/124.539cm。截图已移出起停计时，未放宽原距离或跳高断言 |
| 文档/日志/交付 | 本页与`Docs/Development/需求对齐差距清单.md`；Body默认Log及现有自动化构建入口 | 此次局部修复不代表整个角色/联机模块完成 | 按contract/runtime_behavior/presentation_delivery记录最终结果、删除本范围旧错误说明，独立中文检查点；无新业务账本 | 实施→验证→最终diff→提交 | Editor/Game Development、正式运行日志/截图；未Cook/新包/高延迟完整预测明确列缺口 | 主工程BuildEditor-HitchGuard与BuildGame-HitchGuard均成功；197项组合196通过，末轮5项专项/渲染全通过。结果同步唯一差距入口；正式地图真人手感/高延迟预测/新包默认日志仍未验收 |


本轮证据分层（均相对项目根目录）：

- **contract**：`Saved/Automation/MovementResponse-20260910/BuildEditor-HitchGuard.log`、`BuildGame-HitchGuard.log`均为实际主工程Win64 Development成功；最终源码与模块摘要见同目录`MainSourceManifest-Delivery.json`、`MainBinaryHashes-Delivery.json`。
- **runtime_behavior**：`Report-Final/index.json` 197项中196通过（186 clean、10带既有警告），唯一失败仍为StarterRod耐久150/500。完整相关抓推、轻道具、身体、IK、正式搏鱼60/120Hz/卡顿、主控费用与网络协议通过。最后小幅输入边沿发送衔接和跳跃首帧保护后，`Report-RenderedFinal/index.json`再验新增跳跃、原三刚体跳跃、起停朝向、正式输入抓拉和轻道具网络五项，5/5通过（2 clean、3带既有ABP启动警告）。早期Report-Initial的助手拉力变化、Report-Rendered的截图测时污染/跳跃额外抬升均已保留为失败证据，不能替代最终报告。
- **presentation_delivery**：已查看`Saved/Automation/MovementResponse-20260910/Images/20260910-060155-formal-stopped.png`、`20260910-060156-formal-side-walk.png`；实际正式BP猫、Controller、默认镜头方向与真实按键链均经服务器/客户端核对。截图采用计时完成后的观察镜头，未修改正式相机资产。最终实际按键松开后服务器移动2.444cm、客户端追齐6.235cm，0.25秒观察时服务器水平速度0.560cm/s、两端位置差<1cm；该距离含输入/网络/插值延迟，不应描述为网络端零延迟。正式Lake地图真人手感、高延迟/丢包下整套运动预测、新Cook/Development包无-log双端落盘尚未验收，本轮不关闭角色/Fishing/Delivery模块。

操作结果：普通自由移动快速起停，猫面向世界移动方向，停止保持最后身体朝向；只转镜头不会改变该朝向。主动伸手和主控持竿仍面向独立瞄准方向；已建立抓握或被抓住时沿用原有限物理拉扯响应，受到朋友/鱼拉动不会被强制清速。正常100cm/s松键后约0.5cm，300cm/s约6.6cm；120ms卡顿帧下分别约6.1/17.4cm，无持续缓动或倒冲。加速度/阻尼限值改变的是自由移动响应，不改变FishingStrength、主控力量预算、跳速或重力配置。

默认Development日志分类`LogCatPhysicsGrab`，新增`physics_body_movement_requested/accepted/snapshot`，保留`physics_body_input_rejected/timeout`、`physics_body_control_changed`和跳跃事件；`Tests-Final.log`与`Tests-RenderedFinal.log`按World/NetMode、BodyId、ControlEpoch、InputSequence关联请求、服务器接受和物理后快照，BodyYaw/ViewYaw展示视角分离。新包默认落盘尚未实测。无源码/资产删除；旧普通全时镜头朝向和自由行走0.22s路径已替换，0.22s仅作为真实抓握连接的现役拉扯响应保留，其消费者由原抓拉测试确认。

## 2026-09-10：四足落脚 IK 与步幅匹配

正式 `BP_CatCharacter` 和原型 Pawn 共用 `UCatPhysicsPrototypeVisualComponent` 的最终姿势通道：原动画 → 四足步幅/地面求解 → 抓握前爪 CCD。`FCatQuadrupedLocomotion` 只读物理身体、动画及场景碰撞，修改可见 PoseableMesh 的腿部旋转和有限骨盆偏移。身体、手球、碰撞、受力、费用与网络裁决仍由原有物理/抓握系统负责。

当前 Cat 骨架的 LF/RF/LB/RB 四条腿各有三段骨骼，使用从原姿势开始的 FABRIK 保持骨长。运行时读取压缩动画中支撑脚后移的轨迹，标定 Stand/Walk/Run 三个现有原地素材；按实际混合权重、素材时间推进量和可见模型缩放计算参考速度。步幅比例为相对支撑面的身体速度除以参考速度，默认限制在 0.6–1.6。原型的私有 Walk 播放器也使用该标定速度，移除原先固定 100 cm/s 的参考值；正式 ABP、Montage、动画通知和跳跃根骨补偿保持原通道。

每只脚独立探测地面并调整脚底朝向；支撑阶段保存支撑组件/骨骼的局部落脚点，抬脚、支撑变化或超距时解除。地面姿势不会把脚强行锁在无法到达的位置。前爪伸出时让出该脚控制，随后由现有抓握求解器处理；离地、身体不可行走、翻倒、非步行动作和明显滑动时淡出。复位或大幅位置变化清理历史足点。水平移动平台的速度另行采样，不把平台位移算进自主步幅。

组件的 `Catfishing|Locomotion / LocomotionSettings` 可配置开关、步幅范围及偏移界限。`MaxFootOffsetCm=8`、`MaxPelvisOffsetCm=3`、`MaxPlantDriftCm=5` 均为**未缩放的模型厘米**，运行时乘可见模型缩放一次；`BlendSeconds=0.12` 为秒。关闭 `bEnabled` 淡出落脚及步幅姿势修正，原型的素材速度标定仍保留。超出腿长、步幅范围或地形修正范围时允许残余误差，不拉长骨骼或移动真实身体。当前只支持已审计的 Cat 骨链和上述三个原地素材；其他动作保留原动画。

本次接入基线为物理检查点 `6b04247` 和独立旧预算清理检查点 `6358679`。此前在 `Saved/Validation/LocomotionIK-20260910` 已完成不含 IK 的 Editor 构建；用户 `Cat_Skeleton.uasset` 修改保留，不保存任何动画/Blueprint/骨架资产。下表是本轮执行与审查材料，后续模块状态仍归需求对齐差距清单。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 最终动画消费者 | `Source/Catfishing/Character/Physics/CatPhysicsPrototypeVisualComponent::RefreshVisualPose`；正式 `ACatCharacter::BeginPlay` 与原型 `BeginPlay` 调用 `InitializeVisual` | 原动画复制后只做抓握 CCD；新增四足地面及步幅修正，保留原源姿势/根骨补偿 | 新 `Character/Animation/CatQuadrupedLocomotion::Apply` 接于 ReachAlpha 更新后、手 CCD 前；原 ABP 不变 | 动画和 PostPhysics 已完成后求解，抓握最后处理 | 正式 BP 实例、原型实例、骨长/根骨/动画源不变及双端消费者测试 | `FormalBlueprintKeepsAnimationAndPhysicsAuthority`、原型滑动测试及正式双端测试通过；见下方最终报告 |
| 速度与步幅 | Visual `UpdateBaseAnimation` 原 `WalkReferenceSpeed=100`；正式 ABP 引用 `/Game/Animalia/Cat/WalkToRun` 的 Stand_00-IP、Loco_Walk-IP、Loco_Run-IP | 旧值未按素材标定；新值区分模型 cm/s、世界 cm/s、播放倍率，三者只换算一次 | `GetReferenceSpeedMeshCmS` 标定原型播放率；`ReadAnimation` 读取正式真实混合/播放速度；`Apply` 调整步幅 | 先校准素材，再匹配剩余速度差，不改变正式 Montage 或动画推进 | 多速度实测原动画与修正后支撑脚位移；实际 ABP 图检查 | 固定参考常量已删除；Walk 标定 32.655 模型 cm/s。原型三档支撑脚滑动减少约 69%–79%，正式三档约 64%–98%；这些是连续落脚样本的比较，不是全动画零滑动承诺 |
| 地面、脚底和骨盆 | 新 `Apply/SolveFoot`，消费身体 `GetCollisionObjectType/GetCollisionResponseToChannels` 对应命中及 Cat LF/RF/LB/RB 骨链；与并行交互碰撞修复的物理支撑查询一致 | 原脚掌可能穿地/悬空；新增限距落脚、坡面脚底朝向、有限骨盆偏移；Visibility-only 交互盒不能成为脚下支撑 | 可见骨骼旋转及 RigPelvis 局部平移；RigRoot/Actor 不写 | 支撑局部点随支撑移动，超距或抬脚解除，再保长求解；不以交互射线代替身体碰撞约定 | 台阶、坡面、移动支撑、交互盒穿过；物理位姿/速度与动画源不变 | `TerrainPosePreservesPhysicsAndBoneLengths` 通过：3 cm 台阶抬脚 3.016 cm，支撑上移 1 cm 时脚随动 1.002 cm；12°坡面脚底朝向、交互射线仍可命中但不抬脚、骨长及物理状态不变均通过 |
| 权威、复制与退出 | 只读 `UCatPhysicalBodyComponent` 的速度、输入、支撑、可行走状态、ResetEpoch；手 ReachAlpha 来自已有 Grab | 不添加第二份运动状态或 RPC；身体控制失效时必须让出姿势控制 | `Apply` 淡出/清理，Visual `DestroyVisualComponents` 调用 `Reset` | 先消费物理快照；抓握后解；销毁清理弱引用及足点 | 实际跳跃/复位/抓握、身体禁用、Listen/Client 与原物理回归 | `GrabJumpResetAndDisableReleaseFootControl` 与原有抓握/跳跃/翻身/静态支撑回归通过；服务器继续模拟、客户端只读快照；未增加 RPC 或物理写入口 |
| 配置、资产及持久化 | Visual 原生 `LocomotionSettings`；正式 `/Game/Character/BP_CatCharacter` → `/Game/Animalia/Cat/ABP_Cat` → WalkToRun | 新参数默认开启，单位见上文；无新资产保存、Cook 入口或复制字段 | 保留现有骨架/动画/Blueprint 与加载引用；审计图节点确认地面 IK 唯一负责方 | 不删除二进制资产或兼容入口；资产审计先于最终验收 | 骨架 hash、ABP 实际图、正式运行类、Game 构建 | 实际 ABP 节点及三个 BlendSpace 素材审计通过，没有另一套地面/步幅求解节点；Editor/Game 构建通过，用户骨架 hash 不变。UI/WBP、存档、资源扣费不涉及；无旧兼容入口需要删除 |
| 诊断及验证入口 | 新 `LogCatLocomotion`；既有 `Build/Automation/verify_physics_grab_prototype.ps1`；新 Runtime/Editor `Character/Animation/Tests` | 需要 Development 默认日志解释何时修正/退出、真实步幅及足端误差 | `locomotion_clip_calibrated`、`locomotion_pose_sample`、设置/骨架拒绝事件；正常采样至多每秒，状态切换即时 | 日志按 BodyId、World、NetMode、Authority、LocalRole 对照两端，不建立玩法状态 | `Catfishing.Locomotion` 专项，定向旧手/跳跃/物理回归；真实客户端截图 | 26 项组合回归全部通过（23 clean、3 带原 ABP 启动 warning）；另两项正式网络渲染通过，已检查站立/行走画面。完整层级边界见下文 |

验证入口：`Build/Automation/verify_physics_grab_prototype.ps1 -Mode BuildEditor -RunName LocomotionIK-20260910`，随后 `-Mode Automation -RunName LocomotionIK-20260910 -Filter Catfishing.Locomotion`。加 `-Render -Filter Catfishing.Locomotion.Network` 获取正式客户端画面。试玩当前已编译原型可执行 `Scripts/launch_physics_grab_prototype.ps1 -RunName LocomotionIK-20260910`；正式工程已写入源码，但当前打开的旧编辑器需要重新编译并重启后加载。

最终 `contract`：`Saved/Automation/LocomotionIK-20260910/BuildEditorFreshPose.log`、`BuildGameFinal.log` 均成功；七个交付源码与冻结副本的 SHA256 记录在 `IKSourceManifest.json`。增量验证期间一次手工复制保留了早于旧 obj 的时间戳，导致执行旧地面筛选，已明确刷新副本时间并重编求解器；以之后报告为准，不能使用早期失败报告宣称该过滤已生效。标准验证脚本已有内容变化刷新时间戳保护，本轮未改该脚本。

最终 `runtime_behavior`：同目录 `Report-20260910-111703-781/index.json` 为 26/26 通过、0 failed、0 notRun；六项 `Catfishing.Locomotion` 专项全部通过。正式 100/200/300 cm/s 请求档实际速度约 93.77/187.57/281.93 cm/s，原型 25/60/100 档约 22.73/54.58/91.24 cm/s，未为了凑目标速度修改物理驱动。连续落脚样本中，正式客户端滑动总距离由 224.583 cm 降至 71.661 cm；累计值来自多脚/多帧比较。真实日志 `Automation-20260910-111703-781.log` 按 `LogCatLocomotion` 和 `BodyId` 对照房主/客户端。3 项 warning 来自原有 `ABP_Cat` 启动除零，前置物理验证日志 `Saved/Automation/PhysicalGrabIntegration-20260909/Automation-MainFormalJumpRender-20260909-174257.log` 也有此项；未在本轮修改该二进制动画蓝图。

`presentation_delivery`：`Report-20260910-112126-962/index.json` 的正式步幅与原正式跳跃两项真实网络渲染均通过（均带上述原 ABP warning），日志为 `Automation-20260910-112126-962.log`。已检查 `Saved/Validation/LocomotionIK-20260910/Saved/Automation/Locomotion/Images/20260910-032146-FormalStandingIK.png` 与 `20260910-032148-FormalWalkingIK.png`。尚未完成正式地图真人多人手感、长时间低帧率/高延迟观察、新 Cook/打包及打包双端不加 `-log` 的默认落盘验收；这次局部交付不关闭 Character/Growth/Condition 或 Delivery 模块。并行轻道具玩法不在本轮冻结副本内，其新增支撑忽略策略由该物理任务后续同时接入身体与 IK 两处查询。

## 物理原型说明

这是独立的身体与抓握试验场，使用当前猫模型、动画和程序前爪姿势验证刚体接触、双向传力、固定支点承重与释放。2026-09-09 起，身体驱动已抽到 `UCatPhysicalBodyComponent`，由原型 Pawn 和正式 `ACatCharacter` 共享；正式抓握合作接入范围与当前验证结果见 [钓鱼架构](FishingArchitecture_zh-CN.md)。下文早期检查点中的“正式路径不变”只描述当时的原型隔离范围。

## 入口

地图：`/Game/Catfishing/Prototypes/PhysicsGrabPrototype`。地图绑定 `ACatPhysicsPrototypeGameMode`，运行时生成两只猫、动态竿、箱体、地面、墙和平台。原型竿仅是物理道具，还没有钩、线、鱼或正式钓鱼会话。

本项目编辑器可能仍加载旧 DLL，所以验证与试手使用独立工程输出：

```powershell
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode Prepare
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode BuildEditor
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode CreateMap
pwsh -File Scripts/launch_physics_grab_prototype.ps1
```

启动脚本只在 `Saved/Validation/PhysicsGrabPrototype-20260909` 内配置本地 IP 运输。它不会关闭正在使用的编辑器，也不会修改正式项目的 Steam 运输配置。试验场不是正式主菜单的入口。

两人本地试验分别执行：

```powershell
pwsh -File Scripts/launch_physics_grab_prototype.ps1 -Mode Host
pwsh -File Scripts/launch_physics_grab_prototype.ps1 -Mode Client
```

默认端口 7779。第二台电脑使用同一构建，`-Mode Client -Address <房主局域网地址>`。这里验证的是物理原型的 IP 联机，不代表 Steam 房间、旅行和正式多人游戏已验收。

## 操作

| 输入 | 行为 |
| --- | --- |
| WASD | 相对视角移动；移动通过有限的身体驱动力实现 |
| 鼠标 | 转向、抬头或低头，决定伸爪方向 |
| 按住左 / 右键 | 伸出对应前爪，真实手球接近碰撞表面后建立抓握 |
| 松开对应按键 | 立即请求释放该手的约束 |
| Space | 有脚下支撑时跳跃 |
| R | 先解除自己的及别人抓住自己的连接，再回到出生点 |
| Tab | 单机切换猫；未控制的另一只猫仍参加物理模拟 |
| F1 | 切换抓点辅助显示：青色伸手、绿色已抓住、黄色接触点 |

当前猫约 30 cm 高，前肢骨长约 19.6 cm。需要靠近再伸手；低头可抓地，转向身旁的猫可抓住它。臂展限制使用厘米，刚体力使用 kg·cm/s²。正式合作钓鱼为各猫设置独立 motor 预算，ASC 的牛顿力量乘100后施加，CMC 的旧 Mass 不作为刚体质量来源。

## 2026-09-10：轻道具与正式持竿

原型动态箱/竿与正式鱼竿共用轻道具策略：任意手抓住时自身重力0，最后松手后25%重力轻落、线阻尼2和角阻尼4；有鱼力时恢复原阻尼且不截断速度。实体碰撞仍存在，轻道具碰猫只纠正道具，猫之间仍可双向推抓。动态轻道具不作脚底或起身支撑，固定地形保持原行为。

正式主控用R取放本人的竿，并保留原持竿偏移、鼠标瞄准及搏鱼转竿。主控持竿采用受控姿态，鱼线力只写入主控身体一次；助手抓猫或抓竿后通过真实约束拉动，不按R加入、不共享体力或收费，也不自动接任。抓受控竿的约束连接到主控身体对应握点，主控释放后同一GripId改接自由竿；助手还抓着时竿仍无自重。原型的R依旧是复位，不是正式鱼竿命令。实现盘点与验证见`FishingArchitecture_zh-CN.md`本轮轻道具一节。

## 运行边界

- 身体是一个受控刚体，两只前爪各是球刚体。肩驱动和接触约束传递双方反作用力；脚点支撑与有限姿态驱动辅助站稳，近地侧躺/倒扣可通过物理转矩恢复站立。它还不是完整多关节主动布娃娃，没有正式多阶段起身动画或爬回平台动作。
- 当前模型和动画只作为表现源，起跳、空中与落地使用已有 JumpX 片段，程序 CCD 在身体动画和过渡混合之后让前爪追随物理手，保留骨段长度。原模型、ABP 和原 Physics Asset 不需要保存修改。
- 服务器唯一模拟身体、手爪、道具与抓握。客户端提交输入并插值权威快照，没有实现完整物理预测和回滚。延迟手感需要单独评估。
- 抓握快照包含独立 GripId、版本、目标 Actor / Component、骨骼和目标局部接触点。松手、失焦、切换控制、目标销毁、断线和复位都走明确的解除入口。
- 正式项目已接入真实身体和抓握；鱼竿名册只保留唯一主控，原虚拟共同速度/固定队形与辅助费用不再生效。原型和局部验证不关闭 Fishing、角色或 Delivery 整模块。

## 验证与日志

```powershell
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode Automation
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode Automation -Render -Filter Catfishing.PhysicsGrabPrototype.Network
pwsh -File Build/Automation/verify_physics_grab_prototype.ps1 -Mode BuildGame
```

报告在 `Saved/Automation/PhysicsGrabPrototype-20260909`，隔离工程的默认游戏日志在 `Saved/Validation/PhysicsGrabPrototype-20260909/Saved/Logs`。启动脚本不需要 `-log` 窗口。

主要过滤词：`LogCatPhysicsGrab`、`physics_grip_created`、`physics_grip_observed`、`physics_grip_released`、`physics_body_motion`、`physics_prototype_visual_ready`、`physics_prototype_animation_changed`。用 `GripId` 关联同一抓握，用 `BodyId` 关联身体；分别核对房主与客户端的 World / NetMode 和状态。

倒地恢复另查 `physics_body_ground_recovery_changed`，其中 `Active` 表示近地翻身电机是否启用，不表示已有脚下支撑或允许跳跃。

测试分层：类型与骨长约束属于 `contract`；真实 World/Chaos 与 Listen 客户端 RPC 测试属于 `runtime_behavior`；渲染截图与实际操作属于原型的可视验证，不等于正式模型、正式动画或打包联机的 `presentation_delivery` 完成。实际结果与持续缺口归入 `Docs/Development/需求对齐差距清单.md`。

## 原型创建时的影响与交接（历史检查点）

修改前已在对话完成职责盘点。正式编辑器的既有 PIE 使用原 DLL，本轮构建使用隔离输出。实施期间观察到 `Cat_Skeleton.uasset`、`Config/DefaultGame.ini` 和 Fishing 源码/测试的并行修改，本轮没有纳入这些变更；交付时钓鱼变更已由其他任务提交，骨架修改仍保留。首次专项测试尚未运行；实现后的首轮夹具失败保留在报告中，不能当成既有游戏缺陷。

下表的源码路径均相对 `Source/Catfishing/`；这是本轮变更的技术交接材料，模块状态仍只记录在需求对齐差距清单。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 正式角色与钓鱼入口 | `Character/CatCharacter.*`、`Framework/Game/CatfishingPlayerController.*`、`Fishing/Actors/CatFishingRodActor.*`；正式 GameMode/BP 及操竿名册消费这些入口 | 正式 CMC 移动及按键加入继续运行；本轮目标为独立可试玩的物理抓握 | 保留正式路径；独立 `Framework/Game/PhysicsPrototype/CatPhysicsPrototypeGameMode` 选择原型 Pawn/Controller/HUD | 先准备原型接收方，再绑定独立地图；本轮不切换正式生产入口 | 不删除仍有消费者的 CMC、旧操作位和资源权威；正式钓鱼回归未运行 | 原型未调用操竿名册、费用或存档；正式路径是仍在使用的消费者，不是待删废案 |
| 身体、移动和支撑 | 新 `Character/Physics/CatPhysicsPrototypePawn`，由原型 GameMode `RestartPlayer` 创建/占有；Tick 调用 `UpdatePhysicalMovement` | 4 kg 身体、两只 0.12 kg 手球参与 Chaos；输入 X 前进/Y 横移，100 cm/s，有限驱动力；四脚点辅助站稳 | 新增身体与手球、肩驱动、跳跃及复位；不沿用正式角色力量字段 | 先创建真实刚体，再配置 Hand/Body 约束及抓握组件 | 互推、墙碰撞、下落及真实位移；不能仅靠坐标赋值证明物理 | `RealBodyFallsPushesAndStopsAtWall` 通过；墙前中心 x=136.997 cm，身体半长 13 cm，墙面 x=150 cm |
| 抓取与双向力 | 新 `Interaction/Grab/CatPhysicsGrabComponent`；Controller → Pawn `SetGrabInput` → 组件 RPC → `UpdateHand/TryLatch` | 按住持续伸手，手球接近表面 1 cm 内建立约束；18 cm 伸手长度，600 N 接触断力；松手释放 | 同一约束路径支持角色、动态物体及静态表面；目标记录 Actor/Component/Bone/局部点与独立 GripId | 肩驱动先可用，接触约束随后创建；仅服务器建约束 | 抓猫位移、抓地、静态承重、释放后下落、动态竿位移 | `RealGripTransfersForceAndCleansUp`、`GroundGripAndStaticContactBearBodyWeight`、`ReachingGripsAndMovesThePhysicalRod` 通过；动态竿移动 49.675 cm |
| 当前模型表现 | 新 `Character/Physics/CatPhysicsPrototypeVisualComponent`；Pawn 初始化并传入物理手/伸手状态，PostPhysics 复制动画并解前爪 CCD | 继续使用 `/Game/Animalia/Cat/Meshes/Cat` 和 Stand/Walk；伸出的前爪追随真实手球，不要求现有 ABP 有 IK | 运行时隐藏动画源 + 可见 PoseableMesh；三骨段保长，松手混回动画 | 刚体更新后消费手位置；客户端速度读 Pawn 的权威快照 | 视觉手与物理手距离、骨长、实际渲染；旧 ABP/PhysicsAsset 的完整二进制引用未确认，因此不修改或删除 | `ExistingCatPoseFollowsHandWithoutStretchingBones` 通过，稳态误差 0.291 cm；正式模型/ABP/PhysicsAsset 无本轮保存修改 |
| 输入与屏幕消费者 | 新 `Framework/Game/PhysicsPrototype/CatPhysicsPrototypePlayerController` 绑定原生键；对应 HUD 每帧只读 Grab 状态 | 左/右键各控制一只手，WASD 任意水平拉动，鼠标决定伸手方向；R 复位，Tab 单机切猫，F1 辅助线 | 原型 HUD 显示抓握状态；无新增正式 InputAction/WBP/DataAsset 绑定 | 先注册 Pawn/组件接口，再绑定 Controller 与 HUD | 松键、失焦、切换控制是否留下约束；抓握标记是否对应真实状态 | `FocusLossReleasesBothHands` 通过；正式 UI Subsystem 仍消费 `ACatCharacter`，独立 APawn 不进入该 UI 路径 |
| 权威与复制 | Pawn `Snapshot/ControlEpoch`；Grab `LeftGrip/RightGrip/InputEpoch`；Prop `Configuration/BodyTransform` | 服务器模拟唯一权威；身体/手/道具 30 Hz 快照，客户端插值；抓握 RPC 带 epoch/sequence | 原型不启用会让客户端重新模拟的原生 Movement 复制；客户端手球独立插值；未实现预测/回滚 | 服务器先裁决接触，再复制状态和位置；目标 Actor 必须可解析 | owning-client RPC、抓握状态、真实拉动、释放、目标退出两端核对 | `ListenClientGripForceReleaseAndTargetExit` 最终通过；真实客户端拉动 21.675 cm，第二次抓取后目标销毁，两端清理 |
| 失败与退出 | Grab `ReleaseHand/ReleaseAllFromAuthority/ReleaseTargetFromAuthority`；Pawn `ReleaseConnections/UnPossessed/EndPlay/ResetFromAuthority`；GameMode `Logout` | 松键、目标失效、超力/超距、复位、失焦和失去控制有统一解除入口；复位先解除别人抓住自己的连接 | 解除权威约束后复制释放；保留末次 GripId 关联释放日志 | 先清理连接，再重置刚体/更换控制；输入世代拒绝过时控制请求 | 实际约束是否解除，而非只清一个布尔值 | 真实抓取、双手失焦及目标销毁测试通过；实际 Logout/UnPossessed、旧 ControlEpoch 拒绝、延迟/丢包尚未单独运行验证，不从 Destroy 用例推导通过 |
| 场景和资产 | 新 `/Game/Catfishing/Prototypes/PhysicsGrabPrototype` 绑定原型 GameMode；GameMode `EnsurePrototypeArena` 生成几何和动态道具 | 两猫、物理竿、箱体、台阶、墙及平台；竿是抓取道具，没有正式钓鱼状态 | `Scripts/create_physics_grab_prototype_map.py` 只保存新地图；原生 Prop 复制尺寸/姿态 | 原生类构建完成后生成地图；灯光属于地图，碰撞几何属于服务器 | 原生地图绑定、客户端静态碰撞/姿态、原资产保存范围 | 生成与重载验证见本轮 CreateMap 日志；脚本不调用 SaveAllDirtyPackages |
| 灯光与道具材质 | 原型 Controller `EnsureLocalLighting`；Prop `ApplyConfiguration` 消费颜色配置 | 首次截图发现空地图主补方向光同优先级警告、道具仍用棋盘材质；目标为抓点清楚可见且配置颜色生效 | 补光注册前设主 1 / 补 0，已有地图灯则不生成；显式绑定 `/Engine/BasicShapes/BasicShapeMaterial`，缓存 MID 并核对 Color 参数 | 先修材质/灯光接收方，再由渲染测试加载实际新地图复核 | 检查警告与真实道具颜色；不更改基础材质资产 | 最终截图显示彩色场地与两猫抓握接触点，无旧多日光争用警告；实际地图加载成功 |
| 构建、配置与持久化 | `Catfishing.Build.cs` 增加 PhysicsCore；`Build/Automation/verify_physics_grab_prototype.ps1` 隔离构建；启动脚本只写隔离工程 Config | 正式 `DefaultEngine.ini` 的默认地图/GameMode 与 `DefaultGame.ini` MapsToCook 无本轮切换；试验场不写库存、ASC、账单或存档 | 新启动脚本提供本地 IP 试验；正式 Cook 白名单不加入原型 | 构建 → 地图 → 自动化/渲染 → 试玩；若需包体须另用明确 Cook 地图入口 | Editor/Game 编译、脚本解析、默认日志；不能把 Game 编译当成打包通过 | 证据见下述交付验证；资源迁移、费用、持久化写入不涉及；Cook/打包未运行 |
| 检查与日志 | 新 Runtime 测试及 `Source/CatfishingEditor/Character/Physics/Tests/CatPhysicsGrabPrototypeNetworkTests.cpp`；原型各日志分类 | 检查真实 Chaos、当前模型与 Listen 客户端；默认日志落盘；高频运动限频 | 保留请求、裁决、复制、解除及失败事件；测试结果与文档说明同一原型范围 | 每次源码变化后重建相应模块；报告保留失败与最终结果 | contract/runtime_behavior/presentation_delivery 分层核对 | 最终专项报告、构建和截图见下述交付验证；正式模块整体状态不关闭 |

## 交付验证

`contract`：最终 Editor 构建 `Saved/Automation/PhysicsGrabPrototype-20260909/BuildEditor-20260909-114611-586.log`、Game Win64 Development 构建 `BuildGame-20260909-114846-427.log` 均成功。隔离构建显式使用 `-ForceHeaderGeneration`，避免复制保留时间戳时复用行号过期的 UHT 宏；本轮源码与隔离副本逐个哈希一致。地图生成成功见 `CreateMap-20260909-113040-620.log`；随后渲染进程独立加载了该地图。

`runtime_behavior`：核心专项报告 `Report-20260909-114726-291/index.json` 为 8/8 Success，0 warning/failed/notRun；对应 `Automation-20260909-114726-291.log`。覆盖互推到墙、真实抓猫传力、抓地及静态承重、动态竿、当前模型骨长和前爪、失焦释放与 Listen 客户端 RPC。补充的 `RightHandSupportsAfterLeftReleaseAndResetClearsIncomingGrip` 证明双手同时抓墙后可单独松左手，由右手继续承重（身体 z=-0.898 cm），并在独立有地面场景中验证：双手仍抓墙、另猫同时抓自己时，R 解除自身左右及对方的入向连接。

原型 `presentation_delivery`：`Report-20260909-114801-453/index.json` 的实际地图侧视渲染网络用例通过，真实拉动 21.901 cm；有一条引擎 `r.MotionVectorSimulation` render-thread flag 警告，无抓握用例失败。图片 `Saved/Validation/PhysicsGrabPrototype-20260909/Saved/Automation/PhysicsGrabPrototype/Images/20260909-034821-gripped-and-pulling.png` 已目视检查：两猫、伸爪接触、彩色场地和中文 HUD 可见，旧多日光争用警告已消失。侧视相机仅供测试截图；玩家默认使用第三人称操作镜头。

2026-09-09 11:50 通过启动脚本打开 Solo 独立窗口，未使用 `-log` 或 `-abslog`。实际默认文件为 `D:/develop/Catfishing/Saved/Validation/PhysicsGrabPrototype-20260909/Saved/Logs/Catfishing.log`，已确认新的 `physics_prototype_arena_ready`、两个 `physics_prototype_visual_ready`、`physics_body_started` 及抓握创建/释放事件；启动与操作日志快照留在证据目录 `Solo-20260909-115036.log`。这证明单机编辑器 Game 窗口落盘；Cook、打包后双进程联机及延迟手感仍未验收，不等同于正式表现交付完成。

## 试玩反馈修复：跳跃表现

触发：原型 Space 已给身体施加跳跃冲量，但 `RefreshVisualPose` 只按水平速度选择 Idle/Walk，所以模型不播跳跃。原型首轮 8 项测试没有覆盖这一消费者。修改前仅骨架资产有并行改动，本轮继续保留且不保存原动画资产。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 起落输入与生命周期 | 原型 Controller Space → Pawn `RequestJump` → 物理状态与 `Snapshot` → Visual | 跳力仍为 220 cm/s 增量、禁支撑 0.25 s；初生/Reset 原有支撑布尔暂时无效 | 增加 `bSupportSampleReady` 快照有效位与只读 ResetEpoch 查询，不复制动画枚举 | 先准备可辨识的新样本，再由 Visual 消费；Reset 清除样本有效性 | 初生不误播落地、空中 Reset、客户端接收时序 | 真实 World 与 Listen 客户端跳跃/Reset 测试通过 |
| 动画与抓爪 | Visual 原 Idle/Walk 二态，PostPhysics 复制动画再 CCD | 起跳/空中/落地有对应姿势，原伸爪状态仍生效 | 复用 `JumpX_Start-IP/Loop-IP/End-IP`；起跳窗口 0.12 s、落地 0.18 s、姿态过渡 0.08 s；缓存 CCD 前的已混合姿势供重跳打断 | 基础选态 → 动画推进 → 根锁与混合 → CCD；离地可打断落地，真实落地可打断起跳 | 三态实际动画资产、空中 CCD、骨长、落地再跳 | 两项 Runtime 及 owning-client 网络测试通过；短离地保留 0.12 s 确认窗口，兼容支撑先变化、冲量后消费 |
| 根骨与资产 | JumpX 三段同 Cat_Skeleton；Start/Loop/End 分别 0.583/1.083/0.833 s，RootMotion 关闭 | 渲染发现 Loop/End 的 RigRoot 仍额外上移约 51/48 cm，使模型叠加刚体高度 | 仅在原型跳跃 Poseable 副本中，将根骨完整锁为参考变换，等价 RefPose RootLock；不写共享动画资产 | CopyPose 后先锁根，混合后缓存基础姿势，再解前爪 | 可见根骨每帧等于参考根，原距离镜头截图复核 | 根锁断言通过；原二态分支与 `bUsingWalkAnimation` 已由统一状态选择替换，无第二个动画播放入口 |
| 配置、正式消费者与交付 | 原型使用说明、专项脚本、正式 CMC/ABP 与现有地图配置 | 操作、正式钓鱼与持久化不变；无新增 WBP/DataAsset 或地图迁移 | 更新本说明与唯一进度入口；日志增加 `physics_prototype_animation_changed` | 验证新消费者后刷新独立试玩 DLL | Editor/Game、原抓握回归、运行时事件、实际渲染 | 已构建并运行；原 ABP/PhysicsAsset 仍有正式消费者，不删除；正式 Cook/打包未运行 |

最终集成验证：`Saved/Automation/PhysicsGrabPrototype-20260909/BuildEditor-20260909-121928-221.log` 与 `BuildGame-20260909-122003-953.log` 成功；`Report-20260909-121959-622/index.json` 为 13/13 clean（含同期倒地修复回归）。跳跃专项为 `PhysicalJumpPlaysAuthoredPhasesAndKeepsPawIK`、`JumpLandingCanBeInterruptedAndResetDoesNotFakeLanding`、`ListenClientJumpAnimationAndReset`。运行测试证明真实身体离地、两端三段动画资产、落地后重跳、空中重置、根骨对齐与 CCD；不把仅 `IsPlaying` 当作跳跃通过证据。

`presentation_delivery` 的原型渲染复核为 `Report-20260909-122034-046/index.json`，网络用例通过，仅保留引擎 `r.MotionVectorSimulation` 警告。三图在隔离工程 `Saved/Automation/PhysicsGrabPrototype/Images/20260909-042053-jump-{takeoff,airborne,landing}.png`，已目视猫留在原镜头内、空中姿态可见。完整变换日志显示源 RigRoot 额外 Z 约 51/49 cm，而可见根与参考根均为零位置、零旋转、单位缩放。截图保留默认运动模糊，落地图是阶段首帧；不能用单张图证明完整落地动作或正式美术验收。

## 试玩反馈修复：倒地恢复

触发：猫侧躺或完全倒扣后不能自行站起。原因是原扶正叉积在 180° 时为零；同时 `Body.Up.Z <= 0.35` 禁止脚点支撑，电机落入弱空中分支，100 rad/s² 的旧上限不足以克服侧躺身体接地边缘的重力转矩。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 翻身判定与转矩 | Pawn `UpdatePhysicalMovement` 的脚支撑门和扶正误差 | 翻倒无法进入站立支撑；空中弱电机不够翻身 | 以旋转 Box 的垂直厚度加 1 cm 向下查找法线 Z≥0.55 的近地接触，独立选择恢复电机；180° 时采用身体纵轴确定翻滚方向 | 接触成立才用角度 PD（300/s²、50/s、角加速度上限 650 rad/s²）；朝向恢复后回到原脚点支撑与电机 | 正负侧躺、倒扣、连续物理解算、稳定性；禁止直接 SetRotation/Teleport 修正 | 60/120 Hz × 90°/-90°/180° 六组均通过，最终 UpZ≈1、站立高度≈19.964 cm，ResetEpoch 未变化 |
| 操作、悬挂与网络 | 原 `bGrounded` 跳跃门、墙面抓握、服务器物理和快照复制 | 新近地接触仅可用于翻身，不能让抓墙悬空角色获得跳跃资格 | 保留脚支撑/跳力/普通站姿/空中驱动、抓握约束与复制入口；近地恢复不写 `bGrounded` | 物理翻身 → 正常支撑 → 原移动/跳跃请求 | 恢复后真移动/真跳跃；悬挂后请求跳跃再实际 Tick，不能只即时读延迟生效前的速度 | 恢复后实际行走约 92 cm、跳起约 20 cm；无地面墙面悬挂拒绝空跳，松手仍下落 |
| 清理、日志与交付 | `ResetFromAuthority`、恢复电机选择状态、`Tests/CatPhysicsPrototypeRecoveryTests.cpp` | 复位不能残留恢复选择；不引入另一套角色运动路径 | 复位清除电机选择，状态变化默认落盘；原脚支撑保留为恢复后的消费者 | 更新说明/唯一进度入口，增量构建与局部集成回归 | 无配置/资产/持久化写入；官方 CMC/ABP 不涉及，Cook/打包未运行 | `InvertedAndSideLyingBodiesRecoverThroughGroundContact` 与 `WallSuspensionDoesNotBecomeGroundedRecoveryOrJump` 均通过；不存在需要删除的第二套旧入口 |

此修复的 `contract/runtime_behavior` 证据同上最终 Editor/Game 构建及 `Report-20260909-121959-622/index.json` 13/13 clean。地面六种姿态、恢复后控制与悬挂边界由真实 Chaos World 证明；没有以跳跃截图替代完整倒地恢复的真人/联网美术验收，模块仍保持原未完成边界。
