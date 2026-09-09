# 钓鱼核心架构（技术文档）

阅读对象：需要理解/修改钓鱼玩法逻辑的人。运动链说明于 2026-09-04 按源码核对，当前细节统一见 [鱼运动与遛鱼逻辑实现导读](FishFightImplementationGuide_zh-CN.md)；本页负责系统关系与入口导航。
配套文档：蓝图配置见 [FishingBlueprintSetupGuide_zh-CN.md](FishingBlueprintSetupGuide_zh-CN.md)；规格口径见 [FishingCoreFlow_zh-CN.md](FishingCoreFlow_zh-CN.md)。

---

## 1. 一张图看全貌

```
【输入层】玩家按键
   R / E / 左键 / 右键 / Q / F / X
        │  Enhanced Input（IMC_InputContext）
        ▼
【输入语义层】DA_CatAbilityInputConfig
   ├─ Native：E → IA_Interact → Cat.Input.Interact → Current Target → 服务器交互 RPC
   └─ Ability：R/左键/右键/Q/F/X → Fishing InputTag → ASC
        ▼
【GAS 层】6 个原生 GameplayAbility（可被蓝图子类化，仅承载"输入边沿 + 本地表现钩子"）
   UCatGA_FishingRodInteract / PrimaryAction / Slack / Chum / Scoop / Cancel
        │  Submit*()：把按下/松开变成一条带 RequestId 的命令
        ▼
【命令层】UCatFishingCommandComponent（挂在 PlayerController 上，唯一 RPC 边界）
   HandleAbilityCommandFromAuthority()：服务器按"当前事实"分派语义
   ├─ R   → 已占位=LeaveRod / 附近竿有空位=OperateRod / 否则=PlaceRod
   ├─ 左键→ 无会话按下=记录瞄准 / 无会话松开=BeginCast（视线∩水面）
   │        有会话按下=提竿(TrueBite) 或 拖(HookedFight) / 松开=停拖
   ├─ 右键→ HookedFight 中 按住=松开线杯 / 松开=锁住当前线长
   ├─ Q   → 按下记时刻 / 松开=按时长算蓄力 → 弹道预测落点 → PlaceChum
   ├─ F   → RequestScoop（服务器找范围内已上钩的鱼；成功直接变成抄手嘴叼世界鱼）
   └─ X   → 优先当前操作竿；空手只找 250cm 内本人无人占位竿；有会话=取消或切线 / 无会话=Leave 后 Pack
        ▼
【服务层】UCatFishingService（World Subsystem，只在服务器存在）
   PlaceRod/OperateRod/LeaveRod/PackRod/BeginCast/RequestScoop/SubmitFightAssist
   持有：按 RodActorId 登记的全场竿 Registry、每人最多两根部署额度、每根竿一个活跃会话、BeginCast 幂等缓存
        ▼
【会话层】ACatFishingSession（一次钓鱼长流程的宿主 Actor）
   ├─ UStateTreeComponent（ST_FishingSession）  ← 只拥有"阶段拓扑"
   ├─ UCatFishingFightRunner                    ← 拥有遛鱼数值（0.05s 固定步进）
   └─ FCatFishingSessionSnapshot（ReplicatedUsing）← 唯一复制出口，只读
        │
        └─ ACatFishEncounterActor
             └─ UStateTreeComponent（ST_FishFight）← 外冲/横切/缓游；Runner固定步触发，仅服务器运行
        ▼
【表现层】只订阅，永不写回（单向依赖）
   Snapshot/ViewBridge、表现 Actor 的 BP_On* 事件、Ability 的 BP_OnLocalInput* 钩子
```

**核心原则**：严格服务器权威。客户端只提交"意图"（带 RequestId 幂等 + ExpectedRevision 乐观锁），
所有事实由服务器重建——现在连瞄准点、竿 ID、装备版本都是服务器自己算的，客户端命令基本零载荷。

---

## 2. 关键子系统

### 2.0 鱼竿放置、共享与多人操作

`PlaceRod` 现在只检查角色前方 150cm 是否存在可站立实体地面（地面法线 Z≥0.7），不再要求放置点位于水域样条外侧或距岸 4m 内。因此营地、远岸和测试区都可以先架杆。**架杆自由不等于抛线无限**：`BeginCast` 仍要求准星命中有效水域，并同时满足 `min(鱼竿最大线长, 浮漂最大抛距)`、前向夹角与无遮挡视线。

鱼竿公开状态以 `OperatorPlayerStates` 保存唯一有序成员名单，默认容量 `MaximumRodOperatorSlots=4`。部署数量仍是每人最多两根；每人同时最多操作一根。空手 R 优先加入公共锚点 250 cm 内有空位的鱼竿，否则取出本人现有库存实例，首次取出直接持握。再按 R 离开。加入不瞬移角色。

名单首位负责收线、放线和转向；其余成员只提交自己的移动。任意离开后名单压紧，最早仍可操作的成员接任。个人 `MembershipEpoch`、整组 `RosterVersion` 与主控 `ControlEpoch` 分别隔离重入、名单变更与接力后的旧输入/运动快照；下标只是显示顺序，不能作为身份或角色位置。角色的体力为零不自动失去主控权，也不产生力量；倒地、离竿和断线走同一个 Service 移除入口。

HookedFight 每个固定步冻结成员、ASC、个人体力/上限和 CMC 已接受的移动意图。`FCatFishingGroupModel` 使用同一套 N 人向量计算：主位系数 1，辅助默认 `HelperStrengthMultiplier=0.5`（来自正式 FightBalance，范围 0–1）；有正体力即贡献完整角色系数力量，零体力贡献为零。站定沿远离鱼方向支撑；移动与站定分享同一个力量预算，反向移动抵消，侧向移动改变合力方向。队伍速度不随人数相乘。鱼的行为、体力公式、正式 StateTree、最大线长放线规则保持原契约。

鱼竿握把以共同运动根为位置基准，加入时立即记录成员当下的相对站位并绑定组移动；从尚无 Session、钩子飞行、Waiting、Probe 到 TrueBiteWindow 都保持队形。单人也走同一个组入口。入退和换主只重定偏移基准，不瞬移剩余角色或重置鱼、线、耐久；退出者恢复自己的普通移动，仍在组内的人继续共同移动。旧左右 StandAnchor 只保留编辑器参考/兼容查询用途，运行时不按压紧下标重排身体。

尚未开始搏斗或一轮终局后仍持竿的无载阶段，`Rod::UpdateUnloadedGroupMotionFromAuthority` 只读每人的 ASC `FishingStrength/FightStamina/MaxFightStamina`、CMC 已接受的移动意图和速度上限，用同一个 `FCatFishingGroupModel` 与同一 `HelperStrengthMultiplier` 求主动移动目标。Rod 将目标积分成全组共同速度（cm/s），取全员胶囊 sweep 可行距离的最小值限制本步位移，再发布 `GroupMotionState.UnloadedVelocity`。CMC 消费这份共同水平速度，保留自身重力、Walking/Falling、碰撞和滑动；不再叠加个人行走加速。无载阶段不启动 Runner，不支付搏斗移动、持竿、转杆或收线账单。进入搏斗后，现有 Runner 接管鱼线受力与共同运动发布，仍由 CMC 执行身体移动；搏斗中暂时零鱼力或鱼力竭不属于无载模式。

同竿成员通过 `CMC::SetFishingGroupCollisionPeers` 彼此忽略移动碰撞，避免紧靠入组时互相卡住；离组时仅恢复本系统新增的忽略，不覆盖其他系统原有设置。无载距离查询遵守该名单；角色在地面遇到可StepUp的低台阶时，以 `MaxStepHeight` 抬高胶囊做可行性探测，再交给CMC真正跨越，实际墙体仍按全员最小距离限制组移动。搏斗距离查询默认保留原算法。队形是共同水平运动目标，仍允许接触容差、坡面高度与真实碰撞产生的位置差，不是将角色刚体锁定或瞬移到同一高度。

体力账本仍是每人的 ASC：共同收线、转杆与去重后的沿线支撑只生成一次账单，按仍能出力的成员均分，余额不足者付到零后由其他有余额成员补齐。个人主动移动与受阻用力另记本人，不能用队友体力透支自己的余额。HUD 总体力只读求和 `ΣCurrent / ΣMaximum`，入退只加减本人现有值，不补满、转移或重新分配体力。有效放线时全组按各人上限恢复；满线不恢复；鱼力竭保持免正向费用。固定步边界内普通入退命令可重试，生命周期强制退出排到步末，防止属性通知改变本步付款集合。

一根竿只有一个未终态 Session。主位离开且有人接替时继续同一场；最后一人离开才进入无人值守松线，到最大线长后继续按真实负载磨损原竿，不扣离开者体力。新主位使用自己的输入序号域，初始左右键均释放，必须重新提交当前控制世代的输入。取消、收获或其他终局只结束本场鱼载荷和结算，仍占用健康鱼竿的成员回到无载组移动；破竿、收起、全部离开或 Rod 销毁才完全解绑。鱼力竭仍属于同一场搏斗：立即清鱼驱动力与鱼转矩，保留组目标、`bFightActive` 和原 `AimInputEpoch`，下一固定步继续原力竭收线规则。Runner 停止幂等，旧终态 Session 后续 EndPlay 不得撤销同竿新一场的载荷。

`OwnerPlayerState`/部署时稳定归属 ID 代表原竿归属与收纳权限，接力不转让物品。BeginCast 按当前主位和实际部署的 UseRecord 冻结原竿装备宿主，鱼饵/鱼漂仍来自抛钩者。跨宿主 Begin 先预检并静默提交精确预留和锁，再发布通知；`EquipmentRevision` 保持抛钩者语义，原竿宿主版本另记。磨损只写 `RodItemInstanceId`，退出不改扣新主位选中的另一把竿。

资源宿主失去占有不再强制终止其他人的钓鱼；`UnPossessed/EndPlay` 在 PlayerState 与 ASC 清除前先经 GameMode 协調 Service 移除本人，再把尚在场原竿的正式 held UObject、UseRecord 和未结束的 FishingUseRecord 转入当前 World 的服务器 `Equipment/CatFishingResourceCustodian`。原记录移除，正式实例只变更运行时归属，不复制为第二件物品；Coordinator/Session 引用在通知前重绑。后置 Save 导出只接收原 Equipment 仍持有的物品，退役不能销毁已经托管、队友仍在使用的竿。普通背包不随之复制，不授予接力者所有权，也不实现整背包断线恢复、重连领取或离线托管物跨存档重载；当前 World/Run 关闭时统一清理。准备失败和真正终局仍由单一 Release 退还未消费预留并解锁。

历史 `CommitFailureBudgetFromStateTree` 入口仍保留供未完成引用审计的资产兼容；正式 Session 树生成器不使用它，Equipment 继续拒绝对活动会话/部署实例走“当前选择”失败预算。借竿磨损不绕过此 gate，而是始终使用绑定实例的 `ApplyFishingRodWear`。

X 优先处理当前操作竿；本人没有操作竿时，只寻找公共交互锚点 250cm 内本人无人占位的竿，避免收错另一根远处鱼竿。有活动会话仍按原阶段走取消或切线裁决，无活动会话才进入离位与收纳。`PackRod` 按具体 `RodActorId` 查找并独立验证 `OwnerPlayerState`，当前不允许把别人的竿收进自己背包。`Equipment::UnUse` 已通过 `UCatInventoryTransferService` 将 `ActiveUse` 归还自身 `Stored`；同一通道也支持原生服务器把完整实例转给另一库存，保留耐久、校验容量并处理重放。未来开放他人收竿时，仍需把权限、世界竿收起/失败恢复、注册表解除和目标背包接到这笔事务；不能只放开 Owner 校验。通道契约见 `Docs/Architecture/商店库存与营地公共仓库子技术方案.md`。

#### 借竿抛钩修复影响核对（2026-09-08，历史检查点）

以下保留借竿修复当时的验证证据；其中“资源宿主退出即释放整场”已被后续四人接力与资源托管替代，当前规则以上文和 2.0.2 为准。

修改前基线为 `01b8b75`，工作区只有用户未跟踪的 `Scripts/Art/`、`SourceArt/`，本轮保留。原借竿审计中本人竿对照成功、借竿预留失败；证据在 `Saved/Automation/BorrowedRod-20260908/BaselineReport/index.json`。下表只覆盖本次资源归属修复，不关闭 Fishing 或 Equipment / Shop 模块。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 输入与权威抛钩 | `Fishing/Integration/CatFishingCommandComponent.cpp::BeginCastFromViewOnAuthority/ServerSubmitBeginCast` → `Fishing/CatFishingService.cpp::BeginCast` | 输入已按正在操作的竿路由，旧预留仍只查操作者库存；借竿返回依赖失败 | 保留输入/RPC，在 Service 用部署 Instigator 冻结真实竿宿主，重查占有关系和既有 gate | 先准备跨宿主预留，再切换 Service；保持 RequestId 缓存和失败回执 | 实际 Place/Leave/Operate/Begin、Hook 飞行落水、预留后失败与广播中 UnPossess | 已衔接；Service 的 5 个正式资源场景通过，见下方 FinalReport |
| 装备事实与资源写入 | `Equipment/CatEquipmentComponent.h/.cpp::BeginFishingUse/ApplyFishingRodWear/ReleaseFishingUse`；Session 调用这些入口 | 原竿/饵/漂都在同组件；目标为竿归原主，饵/漂归抛钩者 | 协调记录冻结 RodEquipment；原主 UseRecord 保存会话锁。磨损仍以累计值差额写准确实例，不改耐久单位、饵数量与消费时机 | 双方预检 → 静默扣饵/锁竿/增版 → 通知；先闭合记录再退款通知，禁止重复扣退 | 双版本冲突、重放、换选择、双方回调重入、同主双竿回归 | 已替换本地竿查找；6 项 BorrowedRod 装备回归通过，见下方 FinalReport |
| 通用库存转移与收竿 | `CatEquipmentComponent::ReadInventoryTransferEndpoint` → `Equipment/Inventory/CatInventoryTransferService.cpp`；Service Pack → UnUse | 旧 ActiveUse 锁只扫描本组件协调记录，不能识别借出的竿 | 改读原竿 UseRecord 唯一锁；转移事务与 OwnerPlayerState 收纳权限保留 | 先建立锁，再开放借竿 Begin；Release 后才允许转移 | 活动竿转移拒绝，取消后释放，原有库存转移与满包收竿回归 | 已移除旧本地记录扫描路径；InventoryTransfer 与 BrokenRodPack 回归通过；未新增拾取/扔出玩家入口 |
| 退出与接力 | `Character/CatCharacter.cpp::UnPossessed/EndPlay` → Service Terminate → `CatFishingSession::InvolvesCharacter`；Equipment DestroyComponent/EndPlay/OnComponentDestroyed | 接力后仅查现任参与者会漏掉原抛钩者与竿主；未 BeginPlay 销毁不进 EndPlay | 关联两个冻结资源宿主；组件在基类 DestroyComponent 前完成一次性清理并防止通知重入销毁，EndPlay/OnComponentDestroyed 复用同一 Release | Character 通知先于组件销毁；预留通知时 Session 未注册，Service 另做重入校验 | 接力后两个资源宿主分别退出、预留中 UnPossess、未 BeginPlay 销毁协调者 | 已衔接正常角色退出和组件协调记录释放；直接销毁竿主组件只验证后续 Release 可退款，未宣称立即终止 Session |
| UI、复制、资产与旧任务 | `UI/CatFishingViewBridge.cpp` 消费 Session/当前操作竿；`CatFishingSession::CommitFailureBudgetFromStateTree` → Equipment gate；`CatFishStateTreeAuthoringLibrary.cpp` 正式生成器不接失败预算任务 | 不改公开字段或 Blueprint 签名；EquipmentRevision 始终为抛钩者库存，不能换成竿主版本 | 保留 Snapshot/回执入口，日志增加 RodEquipmentRevision；保留未完成二进制引用审计的失败预算兼容入口及拒绝 gate | 原正式 Rod/Hook BP 与 Session 树继续加载；无资产迁移、配置默认值、存档或 Cook 入口修改 | 正式 BP 本地飞行/会话验证；WBP 图内引用未确认，需编辑器审计及双端实操 | 新逻辑不删除反射入口或二进制资产；正式双端表现/打包落盘未验收 |
| 诊断、测试、文档 | `LogCatEquipment/equipment_rod_session_*`、`LogCatFishing/begin_cast_*`；Equipment/Fishing 新 BorrowedRod 测试、Editor MultiplayerAudit；本页和 Blueprint 指南 | 旧日志无法分辨两份装备归属，旧审计暴露失败 | 增加 SessionId、原竿实例/宿主和双方版本；迁移原借竿审计、保留自有竿对照；旧指南缺口口径改正 | 静态与构建后跑正式资源调用链；进度只记 `Docs/Development/需求对齐差距清单.md` | contract / runtime_behavior / presentation_delivery 分层记录 | 最终 54 项通过；默认日志已在 FinalTests.log 落盘；指南与唯一进度文档已更正 |

本轮验证：`contract` 为 EquipmentShop Static PASS 和 Editor/Game Win64 Development 构建成功；`runtime_behavior` 为 54/54 Success（53 clean、1 条既有无人接管 RunnerTransition=false 警告、0 failed/notRun）。证据根目录 `Saved/Automation/BorrowedRod-20260908/`：`FinalReport/index.json`、`FinalTests.log`、`BuildEditorDelivery.log`、`BuildGameDelivery.log`、`StaticDelivery.log`。6 项新装备回归、Service 的 5 个正式资源场景、原借竿审计及原双竿/转移/收竿回归均通过；`equipment_fishing_shutdown_completed` 实际记录未 BeginPlay 销毁后 UnreleasedSessions=0。首轮仅依赖销毁回调的兜底未通过新增测试，具体回调跳过点未确认；最终改在 DestroyComponent 的 Super 前释放并验证重入安全，未删除或放宽失败断言。

构建/运行位于 `Saved/Validation/BorrowedRod-20260908`，11 个修改源文件与共享工作区 SHA256 相同（`SourceManifest.json`）。这保留了当前开启 Live Coding 的用户编辑器现场；当前编辑器尚未加载此修复。`presentation_delivery` 未运行 Cook/打包、正式 WBP/真人双端借竿或无 `-log` 双端默认日志验收，仍需保存退出编辑器后构建共享项目并实测；单世界正式 BP 飞行不替代双端表现证据。

### 2.0.2 四人移动合力的影响与验证（2026-09-08）

本节保留四人合力检查点的历史实现与测试证据；入竿绑定及清载荷后的当前行为以2.0.4为准，不回改历史报告数字。

修改前基线：HEAD `f767a13`；仅已有未跟踪 `Scripts/Art/`、`SourceArt/`，原样保留。隔离源副本完成 Editor Development 构建；Fishing/UI/Equipment 199 项中 198 通过（含 5 项警告）、1 项既有失败，初级竿资产 500 与测试要求 150 不一致。报告 `Saved/Automation/CooperativeFishing-20260908/BaselineReport/index.json`；基线源码来自 HEAD，Content 使用正式目录引用，HUD 迁移并行发生，不能把该源码基线称为旧 HUD 资产基线。

执行期间外部任务独立提交 `b675838`（胖猫造型与基础骨骼动画，21 个 `Scripts/Art/` / `SourceArt/Characters/CreamCat/` 文件）。该提交不含本轮运行源码，完整保留；本轮不修改或重复提交这些美术文件。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 入口、权限、世代 | `Source/Catfishing/Fishing/Integration/CatFishingCommandComponent::ApplyInputEdge` → Service/Session；`CatFishingRodActor::CommitAuthoritativeMutation` | 辅助左键发力、槽号压紧；目标主控独占1/2、所有人移动且身份不随下标改变 | 保留R/收放入口，增加ControlEpoch/个人Epoch/名单版本，拒绝过时主控 | 先Rod元数据，再命令校验，最后Runner消费 | 四人入退、旧按键、重入；Service/SlackAim/网络测试 | 已接线；Integrated8Report 的 Actors/Service/CommandComponent/SlackAim 与四端 GroupListenThreeClients 全部通过，真实RPC拒绝旧ControlEpoch |
| 成员生命周期 | Service `RemoveOperatorAndReconcileSession` → Session接力；Character UnPossessed/EndPlay、Condition倒地 | 主位离开不能让幸存成员整场失败 | 统一候选遍历，允许零体力主位；最后一人才无人值守；固定步内退出延迟到步末 | 清旧输入→迁移主控→发布名单；不重置鱼线和磨损 | 两个原资源宿主退出、四人→零人、通知重入 | 真实 BorrowedRod 11 场景与 GroupRunnerIntegration 通过：零体力接力、四到零人、UnPossess/Destroy、倒地与逐人危险落水、扣体通知中销毁后步末移除 |
| 力量与核心计算 | Runner `UpdateParticipantIntentAndProperties` → GroupModel → Simulator `Step/FinalizeResolvedStep` | 旧辅助按键/全量叠加与主位独付；目标单预算方向合力和个人账本 | 新纯N人模型；主1/辅0.5，无人数专用分支；有符号支撑只进入一次物理求解 | 冻结集合→方向聚合→求解→最终地形→费用 | 同/反/侧向、微量体力、零主位、多人费用守恒 | GroupModel 7 项、GroupSimulation、ParticipantStrength 和真实四 ASC Runner 通过；保留鱼意图耗体、满线与鱼力竭旧行为回归 |
| 身体、碰撞、网络移动 | Rod组根/组快照 → `Character/CatCharacterMovementComponent::CalcVelocity/PerformMovement`；SavedMove | 旧主位独走；成员加入不能瞬移或重复叠加自身走路推力 | 共同根与偏移，单次合力驱动每个身体；独立碰撞队形修正，跨世代不重放旧力 | 约束和组快照同版本后绑定CMC；结束立即清理 | 修正增速、名单变化跳位、120Hz/低帧率、障碍和客户端复制 | 5项GroupMovement、短线/交接/日志回归及四端真实CMC移动通过；临时World改用UE稳定网络名称和原生StaticMesh地面后，保留原收敛/位移断言通过 |
| 体力与终局副作用 | Runner `ApplyGroupStaminaChanges` → `UCatAbilitySystemComponent::ApplyFishingStaminaDelta`；Session唯一磨损/终局 | 总体力是sum，只能各付各账；保留有效放线/满线/鱼力竭规则 | 冻结每人的ASC与上限；每人每步至多一次写；共同费用均分，个人移动独付 | 最终求解后一次结算，世代变更步末执行 | 零余额、未付尾额、加入不补满、共享/个人支撑重复 | 四 ASC 守恒、重复结算拒绝、零主位、费用通知销毁、有效放线/满线/力竭回归通过；实际写入由同一个 ApplyGroupStaminaChanges 完成 |
| 原竿资源与持久化 | `Equipment/CatEquipmentComponent::ReleaseFishingUsesForShutdown` → 新 `CatFishingResourceCustodian`；Session冻结CastEquipment | 旧宿主销毁释放全场；目标当前World保留原竿及本场预留 | 原精确记录移入权威托管，删除原记录并重绑协调者；普通背包/Profile存档不涉及 | 静默迁移→重绑全部引用→通知→销毁原宿主 | 原竿与抛钩者分别Destroy、通知重入、重复Commit/Wear | 真实资源宿主 Destroy/EndPlay 与借竿预留场景通过，原实例/协调记录精确迁移；普通背包和 Profile 未改；跨 World 恢复不涉及 |
| 配置、正式资产、生成入口 | `Config/DefaultGame.ini [/Script/Catfishing.CatFishingSettings] MaximumRodOperatorSlots`；FightBalance DA；`Scripts/create_fishing_fight_balance_asset.py` | 默认2→4；新增独立无单位辅助系数0.5，不挪用旧字段单位 | Config与类默认同步；生成脚本只给新资产初始化，保留已有调参 | Native默认→DA读取→Session构造配置 | 正式DA加载与非法范围；Cook仍沿已有MapsToCook，未改打包入口 | Settings 与正式 DA 加载回归通过，新字段默认0.5；配置仅改2→4；Python语法通过；Cook/打包未运行 |
| UI/动画/Blueprint | Session snapshot → `UI/CatFishingViewBridge` → HUDModel/Widget；`/Game/UI/HUD/WBP_CatHUD` | 旧桥只主位且正式WBP缺体力控件 | 辅助绑定同会话，加入total max字段；正式WBP补控件保留按钮；0.2s调和复制顺序 | 先DTO/绑定再正式控件；旧个人字段含义不变 | 正式WBP实例验证；Rod/Character主图已导出无瞬移调用，Rod剩余Montage生成回调未确认 | 正式 WBP 幂等迁移、实际实例控件和3项HUD回归通过；SHA/备份见UI拼装文档；5张Rod Montage生成回调图仍未确认，兼容事件/StandAnchor保留 |
| Editor 联机验证构建入口 | `Source/CatfishingEditor/CatfishingEditor.Build.cs` → `Fishing/Tests/CatFishingGroupNetworkTests.cpp`；实际 ASC / Region / BoundarySpline | 新四端测试跨模块读取 GAS、创建实际烘焙水域，原模块缺显式 GAS 依赖 | 增加私有 GameplayAbilities 依赖，使用导出 Region/BoundarySpline → BakeGeometry；不导出纯内部几何 API | 先构建依赖，再真实 PIE 四端运行 | 链接与真实成员 RPC、CMC、主控世代复制 | BuildIntegrated8完整链接通过，GroupListenThreeClients成功；空鱼、碰撞与临时World引用的早期失败报告保留，不计通过证据 |
| 日志、检查、文档与旧口径 | `LogCatFishing/LogCatEquipment`；Unit/Editor tests；本页、实现导读、UI拼装清单 | 旧辅助按键/主独付/2位说明会误导 | 清旧帮助者扣费入口，更新费用断言；结构化Session/Rod/Player/Step日志；保留未核实二进制兼容入口 | 清已确认旧调用后跑受影响测试；不扩大全项目重构 | 默认落盘、原有静态脚本与三层证据 | 默认事件已在Integrated8Tests.log落盘，PIE房主/客户端按World和NetMode关联；Equipment Static通过，原FishingEntry/UI静态gate仍失败（见下文）；真人与打包双端未验收 |
| 低帧率移动账单 | Runner 成员采样 → GroupModel 个人费用；Timer 可同帧执行多个固定步 | 原单步吃掉全部位置差，追赶步误判受阻 | Runner 按成员保存厘米位移/秒时间及接受意图，逐步消费；公式不变 | 采样→分配本步进展→一次结算；成员世代变化清旧样本 | 真实 Runner 比较一次0.1秒与两次0.05秒采样；被动拖移/瞬移不成为主动进展 | 真实四CMC/四ASC回归通过：一次0.1秒与两次0.05秒运动和扣费一致，剩余样本消费守恒，瞬移不冒充进展，无输入被动移动不收费 |
| 操作指南与规格消费者 | `Docs/FishingCoreFlow_zh-CN.md` 遛鱼；`Docs/FishingMVPOperationGuide_zh-CN.md` 多人占位；本页蓝图导航 | 仍写辅助左键、力量占比分摊、任意离开无人值守、落水整场终局 | 改为当前合力/独立账本/幸存接力，修复已失效的蓝图指南链接 | 依据已衔接的Service/Runner口径改文档，不另建业务账本 | 定向旧关键词和实际入口核对 | CoreFlow/MVP操作指南已改为合力、均分共同账单、个人移动独付与幸存接力；已失效BlueprintTaskGuide导航改为实际SetupGuide |

本轮最终证据根目录为 `Saved/Automation/CooperativeFishing-20260908/`。`contract`：`BuildIntegrated8.log` 与 `BuildGameFinal.log` 分别为 Editor/Game Win64 Development 完整构建成功；`SourceManifest.json` 核对58个源码/配置文件与隔离项目完全一致，正式HUD SHA为 `6696c60fd0ac67473efcf385422d30b13444f95178c6d46d9cfe105f9dc53bba`。没有关闭或热替换用户编辑器，也没有更改外部美术检查点。

`runtime_behavior`：`Integrated8Report/index.json` 共217项，211 clean、5 warning、1 failed、0 notRun，即216通过；唯一失败仍是修改前的 `StarterRodPreservesMaximumDurabilityBaseline`（测试150、正式资产500），保留原资产与断言。新增及迁移后的Unit和既有职责链回归通过：方向合力/个人账本、真实四ASC与CMC、低帧率样本守恒、5项组移动/复制世代、3项HUD；扩展BorrowedRod包含11个独立World场景，覆盖真实Destroy/UnPossess、个人危险落水/倒地以及扣体通知销毁后的步末移除和原资源托管。

真实 `GroupListenThreeClients` 使用一个Listen World和3个客户端，通过拥有者RPC加入、实际移动输入与CMC、客户端/服务器位置收敛、4→3成员复制、最早辅助补位、旧控制世代输入拒绝、总体力DTO与原竿实例锁。最终会话 `CD4C5D4C40AF2F6A1D886AACEA99B8AF`，竿 `BCABD2A2480961C7FA3EEE9BBB9C80DC`，57次受控短搏斗采样，竿/鱼分别移动9.933/32.998cm，ControlEpoch从1到2，3名剩余成员的复制体力176.901/180。原空鱼、互穿站位和临时World网络寻址失败均通过修正场景解决，未放宽移动、收敛、费用或接力断言。

默认落盘证据为 `Integrated8Tests.log`，过滤 `LogCatFishing` 的 `fishing_group_stamina_settled`、`fishing_group_budget_exhausted`、`fishing_rod_operator_left`、`fishing_control_input_rejected`、`fishing_resource_*`，以及 `LogCatUI/ui_hud_fishing_*`。PIE多World日志共存于同一文件，以SessionId/RodActorId/PlayerId和World/NetMode区分双方；它不是新打包房主与客户端两份默认日志的替代证明。

`presentation_delivery`：正式WBP真实实例和复制DTO已验证，尚未进行Cook/打包、真人4端整场手感、正式地图联机画面及不加`-log`的新包双端落盘验收。当前用户Editor仍加载旧DLL；保存退出后须在共享项目构建Development再体验。Fishing/UI/Equipment模块级缺口继续保留，不能以本轮受控短搏斗关闭整套模块。

本轮静态边界：`EquipmentStatic.log` 为 PASS；`FishingEntryStaticFinal.log` 被脚本固定要求 GameplayMap=Lake 拦截，当前配置为 Showcase2；`UIStaticFinal.log` 被缺少 `/Game/UI/Collection` 显式 Cook 目录拦截。这两处配置本轮未改，仅 `MaximumRodOperatorSlots` 从2改为4，不把上述脚本计为通过，也不扩展到地图或图鉴模块改造。三个变更 Python 脚本及 UI PowerShell 脚本语法通过。

兼容残留：正式 Rod/Character 主图已导出核对，未见按旧StandAnchor瞬移身体调用；Rod 的5张Montage生成回调图未完成导出，2026-09-08 末次 RiderLink health 为 disconnected。因此不删除反射事件、兼容站位查询及未确认二进制消费者的旧 StateTree 交换入口；后续须编辑器完成图/引用审计并迁移消费者后再删。`ApplyHelperStaminaChanges`、`GetPrimaryCatStaminaDrain`、旧 Character 整场终止调用与旧辅助按键运行路径已移除，无第二套生产体力账本。

### 2.0.3 合入上游架构时保留本地钓鱼玩法（2026-09-08）

本节记录已完成合并检查点的历史证据；下述Integrated2是2.0.4的修改前基线，不是新前战组移动的验证结果。

本次合并固定本地 `b3eb453` 与上游 `4380d03`，共同基线 `7daa9d5`；本地备份分支 `codex/pre-upstream-fishing-20260908`。修改前工作区、暂存区均干净。历史基线 `Saved/Automation/CooperativeFishing-20260908/Integrated8Report/index.json` 为 217 项中 216 项通过（211 clean、5 warning），既有失败为 `StarterRodPreservesMaximumDurabilityBaseline` 要求 150 而正式资产为 500；本轮修改前未重新构建/运行，历史报告不作为本轮通过证据。

合并契约：上游负责拆分后的框架宿主、正式 Inventory 实例与 AS 的个人属性；钓鱼核心以本地为准。保留每人两根竿、同竿默认四人、首次 R 直接持握、借竿使用自己的钓组、零体力可占主位但不出力、向量合力与 CMC 身体移动、共同账单均分与个人移动独付、入退不转移或补满体力、同一 Runner 无缝接力和力竭收近。固定步 0.05 秒，长度/速度仍使用厘米及厘米每秒，力使用牛顿；鱼主动意图缺失距离耗体、自适应三状态/连续出力、满线不回体及原竿绝对磨损公式保持本地语义。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 输入/宿主 | `Framework/Game/CatGameplayTypes.cpp` 旧 Controller → `Fishing/Integration/CatFishingCommandComponent`；上游拆为 `CatfishingPlayerController` | 拆分时遗漏的转杆增量、持竿朝向/移动基准/跳跃抑制必须保留 | 迁入新 Controller；保留本地 R/右键/主辅权限；切线确认表现继续由服务器裁决 | 先新宿主与组件，再命令参数 | 首次持竿/转杆/旧输入/切线回归 | 已迁入新 Controller，UpdateRotation 保持公开；Editor/Game构建、Camera/Service/首次持竿与 SlackAimListenClient 通过 |
| AS/生命周期 | `AbilitySystem/Attributes/CatSurvivalAttributeSet`；Character `UnPossessed/EndPlay` → GameMode 协调 → Service | 上限从配置查询迁至 `MaxFightStamina`；UE UnPossess 后 PlayerState 已清，不能依赖后置通知释放成员 | 在身份和 ASC 有效时先释放成员并托管；后置保存读取最终个人库存；微量正体力扣费不以 NearlyZero 丢弃 | 属性就绪→前置清理→后置 Capture | 零体力、上限变化、倒地/销毁/断线、扣费通知重入 | 真实ASC微量扣费、ActorInfo重建、动态上限、零体力接力与 BorrowedRod 生命周期回归通过；清理在身份丢失前执行 |
| 模拟/会话 | `CatFishingSession` → `CatFishingFightRunner` → Simulator/GroupModel/SteeringModel → CMC | 保留全部本地公式、输入输出/单位、固定步和终局时序；只替换上限读源 | Session/Runner 保留本地核心；不恢复上游独立力竭计时器/接力补满/旧行为参数 | 先 AS 与资源接口，再会话消费 | 鱼行为、几何/力学、四人费用、接力连续性 | CoreGameplayIntegrity 核对26个核心文件，25个与本地完全相同，Runner仅上限接缝不同；模拟/合力/鱼行为与四人网络通过 |
| 库存/托管/保存 | `Equipment::BeginFishingUse/ApplyFishingRodWear/UnUse`、Camp、TransferService；上游 Inventory/Save | 旧 Snapshot 写库存被正式实例取代；借竿宿主、RequestId/版本、精确物品与耐久不得漂移 | 正式 Inventory 负责物品持有/批次提交；Custodian 持有迁移来的同一部署 UObject；Equipment 保留选择/会话锁 | 正式事务接收方→本地多竿消费者→保存/归还 | 双端原子性、幂等、失败回滚、全包与跨宿主归还、退出保存不复制托管物品 | FormalObjectIdentityAndAtomicObservers、借竿磨损、耗尽补饵、开局前仓库入库通过；真实托管后Export/Retire不销毁在用竿通过；离线托管跨存档未实现 |
| 复制/诊断/HUD | Session/Equipment RepNotify、Controller 回执 → `HUDModel/HUDWidget` → `/Game/UI/HUD/WBP_CatHUD`；`CatFishingDebugSubsystem::DrawFishingStats/DrawCastAimPoint` | 保留总体力/人数与本地默认日志；诊断面板必须读本场鱼力量、部署竿剩余耐久和实际相机瞄准射线，不能退回资产力量/Pawn视点；个人 AS 上限和正式库存版本进入读模型 | 单次权威结算；总值只读求和；恢复被自动合并覆盖的诊断/瞄准消费者；日志保持 SessionId/RequestId/原竿实例可关联 | 提交完成→冻结结果→通知/复制 | 真实 Listen+3 clients、HUD、Camera/Service与耐久日志 | 三项真实网络与4项HUD回归通过；体力接收/叠层注册/保存退役事件见Integrated2Tests.log；打包双端默认落盘未验收 |
| 资产/配置/Cook | `DefaultGame.ini` 的 `CatFishingSettings`、`CatOnlineSettings.GameplayMap`、`CatRunSettings.DayLengthSeconds`、Packaging；`ST_FishingSession/ST_FishFight`；平衡/性格资产；HUD/鱼迁移脚本 | 保留4槽与本地物理资产；上游默认地图改Lake、白天改60秒会改变本地钓鱼环境，本次保持Showcase2及99999秒；接入上游正式菜单/输入/库存资产 | 保留本地玩法默认值；原生接线后只读加载/编译正式资产；核对生成器/软引用，不靠文本证明二进制无引用 | 核心接口→资产加载→Editor/Game Development构建 | 状态树/鱼竿/角色BP/WBP与输入资产；未跑Cook不得记通过 | 保留本地玩法配置；15资产只读审计、8个BP/WBP内存编译通过且审计前后哈希相同；两StateTree加载及真实运行通过；未Cook/打包 |
| 测试/文档/清理 | Fishing/Equipment/UI/Editor tests；既有 `Scripts/verify_*`；本页与唯一差距清单 | 旧测试只写 Current、旧宿主/配置读取失效；保留玩法断言，纠正现行文档旧口径 | 先播种 Max 再 Current，增加AS上限/零体力回归；删除已替换原生路径，保留未确认BP兼容入口 | 测试适配→构建→受影响回归→逐项diff复核 | contract/runtime_behavior/presentation_delivery分别记结果 | 最终227项中226成功、1既有耐久断言失败；4项Static及脚本语法通过；旧宿主/脚本引用已迁移，必要兼容及模块级缺口见下文 |

本次证据归档：`Saved/Automation/UpstreamFishingMerge-20260908/`。

- `contract`：`BuildEditor3.log` 与 `BuildGameFinal.log` 均为 Win64 Development 完整链接成功；EquipmentShop、ItemsTankSacrificeCamp、CharacterGrowthCondition、UIReach 的 Static 均退出0，4个变更 PowerShell 与 UI Runtime Python 语法通过。`CoreGameplayIntegrity.log` 记录26个核心文件的本地 blob 对照；Runner只改ASC上限读源、合法性检查与逐步刷新，其他25个公式/行为/CMC/接口文件完全不变。两根正式竿、猫种定义、FightBalance、两StateTree及HUD共7个保护资产的当前SHA256均等于本地提交的LFS oid。
- `runtime_behavior`：`Integrated2Report/index.json` 共227项，220 clean、6 warning、1 failed、0 notRun。唯一失败仍是 `StarterRodPreservesMaximumDurabilityBaseline` 断言150与正式资产500不一致，本次未改断言或数值。首轮两个新增库存失败均已修复并在最终报告通过。新测试覆盖正式物品UObject身份、两端完整提交后的观察与幂等重入、ASC微量支付/动态上限/零体力接力、托管后导出不重复原竿且保存退役不破坏同Session。`GroupListenThreeClients`、`SlackAimListenClient`、`FishBehaviorListenClientSnapshots` 都成功；后两项仍有临时PIE关卡NetGUID警告，其他警告包含主动退出与临时World清理诊断，不计为clean。
- 资产只读 `contract`：`FormalFishingAssetsAuditFinal.log` 读取15个资产，0失败、2项StateTree受保护数据读取限制；8个角色/竿/Controller/GameMode/HUD/库存BP与WBP内存编译成功。正式猫力量50、体力上限60；AbilitySet六钓鱼与六BodyAction类齐全且InitialEffect全空；正式IMC含全部六钓鱼输入。`FormalFishingAssetsAuditHashIntegrity.json` 确認15资产审计前后完全相同，未保存资产。StateTree真实执行由上述运行回归证明，Python加载不代替图审计。
- `presentation_delivery`：未Cook/打包，未进行真人四端整场手感、正式界面全流程或打包房主/客户端无`-log`默认落盘验收。实际新日志在 `Integrated2Tests.log`，可按 `fishing_group_stamina_settled`、`fishing_cat_stamina_received`、`fishing_resource_*`、`inventory_held_resources_transferred`、`persistence_departure_deployment_retired`、`fishing_stats_overlay_registered` 关联服务器和客户端World；同进程PIE多World不等于打包双端证据。

清理与边界：旧 `CatGameplayTypes.cpp` 已移除，本地Controller的转向/移动/朝向/禁跳迁入新宿主，6行聚合头仍供尚存原生include使用；旧AS上限配置查询已无生产消费者。保留未完成全量BP引用审计的历史反射事件、站位查询及旧 `IMC_Lake`，后者不是已核对的正式Controller入口，必须先完成其他二进制消费者审计再删除。UIReach Static随架构迁移恢复通过，但其Automation/Runtime仍要求上游已删除的6项高层测试，不能以现存HUD测试替代；FishingEntry Static仍因本次保留Showcase2而不满足固定Lake门禁。世界内托管与退出保存防重复已验证，离线托管资源跨存档重载仍未实现/验收；这些缺口归唯一差距清单对应模块，不关闭任何模块级交付。

### 2.0.4 入竿即保持队形的影响与验证（2026-09-08）

修改前基线为 `8b4c10a`，工作区与暂存区干净。已有历史报告 `Saved/Automation/UpstreamFishingMerge-20260908/Integrated2Report/index.json` 共227项：226成功（220 clean、6 warning）、1失败；唯一失败仍为初级竿断言150与正式资产500不一致。本轮不修改该资产或断言。本轮于2026-09-09完成共享项目构建及受影响运行回归，结果如下；历史报告不作为本轮通过证据。

本轮修复的差异是：原实现虽在入竿时记录偏移，但全员 CMC 绑定依赖搏斗开始，前战自由移动会让按成员均值计算的竿根漂移。现在组移动归属于操作名单的生命周期，鱼驱动力归属于当前搏斗；主辅权限、力量系数、厘米/秒单位、个人账本、装备归属和 Session 阶段拓扑保持原契约。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 入竿与成员生命周期 | `Source/Catfishing/Fishing/CatFishingService.cpp::PlaceRod/OperateRod/RemoveOperatorAndReconcileSession` → `Actors/CatFishingRodActor.cpp::InitializeAuthoritativeIdentity/PrepareOperatorMemberships/CommitAuthoritativeMutation` | 原偏移已记录，尚未搏斗时成员仍自由走；目标加入返回前即绑定组移动，退出只释放本人 | Rod 按现有名单及各人当下位置建立组根/偏移；单人同入口，入退保持组根连续 | 先组移动接收方，再初始化/名单提交发布；Service 距离、容量、版本和接力资格不变 | 1–4人、同向/反向、战前接力、退出者自由移动、加入不传送 | 已接入；FinalReport 的 JoinedGroupMovesTogetherBeforeSessionWithoutFishCosts、GroupRosterKeepsAnchorAndRejectsStaleReplication 及真实 GroupListenThreeClients 均通过，加入即时绑定、主位离开不跳位、全部离开后真实个人移动通过 |
| 无载共同速度 | `CatFishingRodActor::Tick/UpdateUnloadedGroupMotionFromAuthority` → `Simulation/CatFishingGroupModel::ComputeForces`；ASC与CMC提供只读输入 | 原无前战合力；目标复用同N人模型、同主1/辅默认0.5系数，只取主动移动目标 | 读取 `FishingStrength/FightStamina/MaxFightStamina` 和已接受移动；Rod 将目标积分成共同cm/s速度，按全员最小sweep距离限制 | 先合法属性/系数求目标，再一次积分和碰撞上限，最后复制 | 零体力、动态Max、反向抵消、单人不叠速度、一人顶墙全组限位；不写ASC | 已接入；四人真实 CMC 的反向协作、不同初速度及战前体力不变通过；GroupModel/ParticipantStrength 保留既有力量与余额契约；无载无 ASC 写口 |
| CMC、复制与重放 | `Character/CatCharacterMovementComponent.cpp::CalcVelocity/PerformMovement`；Rod `PublishCarrierConstraintToMovement/OnRep_*` | 原组模式只对应搏斗，旧快照可能误恢复前后模式；目标保留水平共同运动及自身垂直物理 | 扩展 `GroupMotionState.UnloadedVelocity/bUnloadedMovement` 与对应 CMC 输入；SavedMove 域包含模式，拒绝跨名单/控制/成员/Aim域重放 | 新字段默认零/false；同域数据齐全才执行，等待期间不偷用旧鱼力 | 前战、入战、停止乱序复制，重力/落地/碰撞、移动重放；真实Listen+3 clients | 已接入；新模式 SavedMove、旧名单/旧鱼载荷拒绝、Falling 首步不散且继续下落均通过；真实四端战前移动、客户端停稳收敛与相对偏移均在5cm内 |
| 同组碰撞与台阶 | Rod绑定名单 → `CatCharacterMovementComponent::SetFishingGroupCollisionPeers`；`GetExternalTractionTravelLimit(..., bAllowStepUp=false)` 被无载组求解和搏斗查询调用 | 原紧靠成员会互相阻挡，无载水平探测会把可跨低台阶当墙；目标消除组内自阻挡并保留真实地形运动 | 仅登记/恢复本系统新增的 `IgnoreActorWhenMoving`；sweep尊重忽略名单；无载grounded分支允许按MaxStepHeight抬高胶囊探测可StepUp台阶，默认搏斗查询不变 | 先同步名单忽略，再做共同最小距离查询，最后由CMC真实移动/StepUp | 紧靠入组、离组恢复、不覆盖其他系统ignore、低台阶可跨、真墙限组、坡面高度与接触容差 | 已接入；UnloadedGroupRespectsPeersWallsStepsAndExitCollision 的三个独立真实世界通过：胶囊直径+1cm同行、墙只挡一人则整组停止、低台阶实际StepUp；离队恢复真实阻挡并保留外部ignore |
| 飞行、等待和咬钩 | `Service::BeginCast` → `Hook::BeginAuthoritativeFlight`、`Session::ScheduleWaitingProbeFromStateTree/OpenTrueBiteWindowFromStateTree`；正式Session树 | 原这些阶段没有Runner，不能靠开启搏斗来绑定队伍 | 原生Rod维持无载移动，覆盖无Session、Hook飞行、Waiting/Probe/TrueBiteWindow；不新增阶段或改选鱼时机 | 入竿绑定先于抛钩；咬钩成功后现有Runner接管发布 | 等待与漏真咬重试不停组、没有鱼却开启受力相机、预留/饵消费时机变化 | 已接入；GroupListenThreeClients 通过无Session实际移动、BeginCast返回与Waiting/TrueBiteWindow绑定/偏移；正式Hook飞行落水和原状态树回归通过。漏真咬重试后的人工移动手感未单独实操 |
| 力竭与终局清理 | `Session::EnterPhaseFromStateTree/FinalizeSession/EndPlay` → `Runner::SetFishExhaustedFromAuthority/Stop` → Rod载荷写口 | 原Clear把鱼力和组移动一起拆除，旧Session重复Stop可能清新场 | 力竭保留fight与Aim域，仅清鱼力/鱼转矩；终局转无载组；Stop在回调前关闭运行态，重复调用无副作用；grounded保持完全清理 | 鱼载荷发布成功后提交力竭State；正常终态结算/资源释放仍单一入口 | 力竭当帧及下一步无无载空档；取消后新场已启动再Destroy旧Session | 已接入；BorrowedRod第12个真实World通过两场正式钓鱼与旧Session真实Destroy，保留新场完整CMC载荷/鱼/原竿锁；力竭当帧及下一固定步保留fight/Aim域并清鱼力；FinalTests.log含对应停止/验证事件 |
| 资源、体力与持久化 | Runner `ApplyGroupStaminaChanges` → ASC；Session → Equipment `ReleaseFishingUse`；Service→Custodian | 无载移动不应生成搏斗账单；终局后名单仍在不等于装备会话锁仍在 | 无载只读属性；保留现有搏斗费用、终态恢复、原竿/饵/漂锁与托管写口，不新增存档字段 | 先运动模式切换，再沿原单次结算收尾 | 前战持续移动体力不扣、旧Session不释放新场资源锁、费用回归 | 资源生产写口保留；Fishing/Equipment运行回归通过，含战前四人余额不变、取消/重抛的精确新场资源锁及既有费用/托管场景；未新增存档字段 |
| 相机、输入与表现资产 | `CatFishingCameraComponent::FindFightRodHeldBy`、Command的AimInputEpoch、Controller朝向；`/Game/Blueprint/Actors/BP_CatFishingRodActor`、`/Game/Character/BP_CatCharacter`、`/Game/UI/HUD/WBP_CatHUD` | `bFightActive` 仍代表搏斗，不能借它表示已入组；旧按槽位传送说明误导 | 保留现有相机/转杆/主辅权限与反射事件；文档明确成员偏移从加入起生效，表现不写身体Transform | 原生复制先衔接；BP/WBP继续消费旧公开入口，无资产写入 | 前战正常瞄准、入战转向阻力、HUD阶段与总体力；二进制隐藏图消费者未重新确认 | Camera、SlackAim、RodEffort、Service和现有HUD/正式WBP实例回归通过；未写资产或删除反射入口。二进制隐藏图本轮未重新导出，真人正式地图/打包表现仍未验收 |
| 配置、生成脚本与Cook | `Config/DefaultGame.ini[/Script/Catfishing.CatFishingSettings]` 的4槽/FightBalance/Session树；`Scripts/create_fishing_session_state_tree.py`、`create_fishing_fight_balance_asset.py`、`configure_formal_rod_anchor_baseline.py`、`migrate_cooperative_fishing_hud.py` | 继续使用同正式系数、标定与树；140cm仍仅兼容站位查询 | 不涉及资产迁移、配置调参、脚本改写、新资源或新增Cook目录；本轮仅原生运行字段扩展 | 现有软引用/生成路径不切换；构建验证结构加载 | 正式树/平衡/角色/竿资产加载；未Cook不得记打包通过 | 不涉及配置/脚本/资产/Cook入口写入；共享Editor/Game Development构建通过，正式树/竿/角色/HUD运行加载通过；初级竿150/500仍是既有测试失败，Cook/打包未运行 |
| 测试、诊断、旧路径与文档 | `CatFishingGroupMovementTests.cpp`、`CatFishingBorrowedRodTests.cpp`、Editor `CatFishingGroupNetworkTests.cpp`；本页、实现导读、BP钩子与MVP操作指南 | 原测试把Clear等同退出组；原生holder-only fallback和私有 `CarrierMovement` 已不适用 | 移除无消费者的原生fallback/私有绑定；迁移清载荷断言，补无Session移动、旧Session销毁及力竭连续性；改正按新编号重站位说明 | 接收方/生产调用完成后清旧路径，再跑contract/runtime_behavior | `JoinedGroupMovesTogetherBeforeSessionWithoutFishCosts`、BorrowedRod与真实四端；默认日志关联RodId/成员世代 | 已删除无生产消费者的holder-only fallback及私有CarrierMovement；原公开反射接口保留。FinalReport为221成功（含6项warning）、1既有失败、0未运行；本页及三份指南已同步，具体三层证据见下方 |

本轮证据根目录为 `Saved/Automation/PreFightFormation-20260908/`。`contract`：共享项目的 `BuildSharedEditor.log`、`BuildSharedGame.log` 均为 Win64 Development 完整构建成功；`SourceManifest.json` 记录10个受影响源码文件SHA256，`git diff --check`通过。中断前使用隔离副本，续作时确认编辑器已关闭后构建共享项目，下一次正常启动会加载本轮DLL。

`runtime_behavior`：共享项目 `FinalReport/index.json` 共222项，221成功（215 clean、6 warning）、1失败、0未运行；唯一失败是基线已有初级竿150/500不一致，未修改资产或断言。新无Session/Falling/碰撞回归、12个BorrowedRod真实场景、四端GroupListenThreeClients及原松线/鱼行为联机回归均通过。首轮 `IntegratedReport` 的两处新夹具失败保留：普通移动需经MoveAutonomous初始化模拟输入倍率；新场需等待真实首个完整组求解后才能验证旧场销毁不会拆除它。修正实际入口与等待条件后保留原行为断言通过。6项warning涉及旧PIE临时World网络名称、测试取消/水深/无人接管及托管Actor销毁时World上下文，不能把它们计作clean。

`presentation_delivery`：尚未完成正式地图真人四端、打包双端默认落盘或Cook验收。本轮未写正式BP/WBP/状态树资产；二进制隐藏图和兼容反射入口仍按上表的未确认边界保留。实际运行日志为 `D:/develop/Catfishing/Saved/Automation/PreFightFormation-20260908/FinalTests.log`，包含PIE房主/三个客户端World观察；同进程日志不能替代打包房主与客户端各自落盘。`LogCatFishing` 的 `fishing_group_movement_binding` 带 `Unloaded`、名单和成员世代；结合 `fishing_group_unloaded_solve_rejected`、`fishing_fight_runner_stopped`、`fishing_exhausted_constraint_rejected` 与现有Session/资源事件复核模式交接。只读复用模型不代表无载阶段新增体力收费，测试通过也不能替代正式派对手感验收。持续缺口仍只归入 `Docs/Development/需求对齐差距清单.md`。


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
- 咬钩加速：`ScheduleWaitingProbe` 在服务器冻结的落水点采样三轴总量 → `BiteRate ×= 1+(1-e^-Total)`；首次计时包含剩余飞行时间，避免鱼钩仍在空中就进入预警。
- 选鱼偏好：三轴采样 · 鱼的 ChumPreference 点积 → 饱和曲线 → 权重放大（最多 ×3）
- 上述三条是当前实现，不是新版目标；待改为水域面积/鱼量账本、平均分布、重叠区共享收敛曲线、守恒重分配与面积容量上限（见 `Docs/Architecture/项目技术方案.md` §7.1.1 和本文 §6）

### 2.3 会话阶段（谁在推进——最反直觉的部分）

| 阶段 | 写入者 |
|---|---|
| Waiting | StateTree 节点 `ScheduleWaitingProbe` **内部自己** EnterPhase，并按泊松抽咬钩延迟起计时器 |
| Probe | StateTree 的 `EnterPhase` 节点（ProbeTriggered 事件转移后） |
| TrueBiteWindow | StateTree 的 `OpenTrueBiteWindow` 节点打开通用响应窗；只让浮漂下沉，不选鱼、不生成 Actor、不扣饵 |
| HookedFight | 真咬窗内收到左键后，`RequestHook` 冻结选鱼上下文、选鱼、生成 Actor、扣饵并启动搏斗 |
| ExhaustedReel | 鱼体力归零或被猫端牵引越岸后发送 `FishExhausted` 事件；同一个 Runner 继续双端运动约束，但关闭鱼 AI 与猫端体力扣费 |
| Resolved/Terminated | `FinalizeSession()` —— StateTree **禁止**进入终态，且它会停树 |

浮漂正式表现由 `ACatFishingHookActor` 驱动，不依赖 `cat.Fishing.Debug`：Waiting 先保证至少 `MinimumBiteDelaySeconds`（当前 3 秒）的小幅慢浮，再叠加服务器随机安静等待；真咬前 `BiteWarningSeconds`（当前 1.5 秒）只把 Hook 的复制模式切为 `BiteWarning`，此时提前提竿仍是空钩；进入 `TrueBiteWindow` 时切为 `Sunk` 猛然下沉。若响应窗内没有左键，StateTree 走 `WindowExpired → Waiting`，保留鱼竿、鱼线和饵料预约并开始新一轮；每轮使用新的确定性服务器随机种子。`MaximumBiteDelaySeconds`（当前 40 秒）是每轮慢浮开始到下沉的总上限。网络只复制模式和服务器起始时间，各客户端本地计算连续位移，因此不会逐帧复制 Transform。

StateTree（`ST_FishingSession`）保持薄编排。其中 `FishExhausted` 是 `HookedFight → ExhaustedReelHold` 的显式事件边；`EarlyHook` / `Interrupted` 仍由 C++ 直接收敛终态并停树。

### 2.4 遛鱼（当前运动链与模块边界）

`FCatFishingFightSimulator::Step()`：纯静态无副作用函数（有单元测试），每 0.05s 由 Runner 调一次。

当前源码用连续主动推力、持久鱼速度、固定正常水阻与共同鱼线张力求解，收线受真实出力限制；CMC 执行猫端碰撞移动，仍不是完整三维刚体/接触求解器。鱼/猫质量与力量来源、收线公式、体力和断线条件只维护在 [实现导读](FishFightImplementationGuide_zh-CN.md)，避免在多个入口复制不同版本的公式。

主猫持竿等鱼时，猫身与移动基准使用 `ControlRotation.Yaw`。提竿成功进入搏斗后，本人切换到第一人称持杆视角：`UCatFishingCameraComponent` 以实际握把为目标，按 `CalcCamera` 的帧时间平滑位置和四元数朝向，再组合镜头偏移。`FightCameraFollowResponseSeconds=0.08` 为本地跟随响应时间；房主与客户端都使用这条路径，没有新姿态的帧也继续过渡，首帧直接建立当前握把基线。身体与移动仍使用实际杆 Yaw，镜头在短暂跟随后与其对齐。角力使杆自然停转时镜头收敛到实际方向；鼠标仍提供施力意图，回转或卸力后镜头立即开始追随。组件不关闭 Look 输入，也不反写杆姿态或角力状态。协作者和旁观者保留原镜头；搏斗结束、离杆、断杆、失去占有或切换观战目标后清除插值历史、恢复原相机与本人身体可见性，并从最后可见方向继续。持杆查询仍只读权威 Registry 或复制的操作位，原 Controller 私有重复查询已移到该组件共用。

镜头偏移与 FOV 在 `Catfishing Fishing Presentation` 的 `Camera` 分类配置；默认镜头位于握把后方 35cm、左侧 16cm、上方 16cm，让杆位于画面右下。握把标定随 Rod 初始复制发送，客户端组合该标定与服务器复制的实际 Actor 姿态，不用未初始化的本地握把。`LogCatFishing` 的 `fishing_fight_camera` 记录进入、每秒一次的实际/请求朝向和恢复，`fishing_rod_grip_received` 记录客户端收到的标定；用 `RodActorId` 与角力日志交叉检索。

搏斗中的实际杆姿态独立于控制器施力意图，由 `FCatFishingRodResistanceModel::StepRotation` 按有阻尼的净转矩积分。猫朝请求方向施加不超过当前力量的转矩，接近目标时连续减小；鱼端最大转矩由 `最终共同线张力 N / ForcePerStrengthNewtons × 玩法杆长 m` 换算为既有 StrengthMeters 单位，再沿最终牵引方向滤波，与杆方向叉乘形成有向回复转矩。净转矩抵消时自然停转；回看、改变鱼力、猫力或线方向后每帧重新求解，不存在硬角度锁或解锁状态，也不使用原来的全方向零速倍率。保持原有身体俯仰范围与最大角速度；响应时间、受载阻尼及 1/120 秒亚步共同决定动态响应。服务器复制实际 Actor 姿态，Development 日志 `fishing_rod_rotation_resistance_sample` 记录请求/实际朝向、本帧转角、净转矩、负载历史和仅用于观察的 `TorqueBalanced`；`fishing_constraint_sample` 记录共同张力、阶段、游向与固定步时序。详细采样开关及字段见 [实现导读](FishFightImplementationGuide_zh-CN.md) 的“平静/反抗运动日志衔接核对”。
`FCatFishSteeringModel` 用独立服务器随机流产生平滑目标游向；相同种子与固定步长得到相同方向序列，客户端不自行随机。

右键放线首次按下时，`CommandComponent → Session` 在验证完整输入后，以当前权威握把朝向重设转杆目标。此后目标只消费 `UpdateRotation` 采集的新鼠标增量，`ControlRotation` 的旧目标和迟到的 CMC 控制角不再驱动杆；尚未重设的搏斗保持原控制器目标契约。松开右键、重复按下通知和命令回执都不再重设方向，仍按住的左键照常恢复收线。实际姿态、负载滤波、努力累计和镜头平滑均连续保留。输入顺序、限位及验证边界见 [实现导读](FishFightImplementationGuide_zh-CN.md) 的“右键放线时重设转向意图”。

鱼高层行为由 Encounter 上的 `ST_FishFight` 控制：当前生成器含 `OutwardRush/LateralArc/EaseOff` 三叶，按固定步更新的持续受阻、体力和时长条件转移。Task 只提交行为，Runner 唯一推进反馈/时钟并手动Tick树，然后连续执行方向和实际出力u；不增加 AIController，也不让树写 Transform、ASC、鱼线或装备。旧 `MotionIntent` 仅投影正式三动画，物理、鱼费用和基础磨损不读它。四性格/正式树已迁移并独立重载，Editor/Game构建和受控行为/复制回归已完成；真人及新打包验收仍未完成，分层证据见实现导读。

Runner 将模拟器的候选结果交给水域/地面解析，再由 Encounter 应用并复制鱼的位置；Rod 消费猫端目标速度与杆转矩输入。鱼线曲线网格只表现端点和余线，不运行粒子物理，也不向服务器提供约束反力。`bStalemate`、`TorqueBalanced` 与 `bStrongConfrontation` 均只观察和表现结果；力量差由现有约束处理，已取消强对抗过载即断线的终局分支。

鱼体力归零或确认被猫端牵引上岸后，Session 发布 `FishExhausted` 进入 `ExhaustedReel`；同一 Runner 保留运动约束，但停止鱼主动运动和猫端正向扣费。当前上岸清空体力和力竭后零猫消耗都是玩法特例，物理改造尚未替换这些分支。猫危险入水由 Condition 的脚点浸没查询确认。

全局搏斗系数来自 `DA_FishingFightBalance_Default`，鱼费用为沿主动意图未完成的米数乘 `FishStaminaPerUnfulfilledMeter`（默认5/3点/米），实际进展使用最终位移扣除历史纠偏后的主动方向投影，反拖不封顶，零意图免耗；满出力参考游速与 `AdaptiveSteeringConfig` 来自鱼种性格，旧方向概率/阶段倍率退出运行。杆长、满出力基础磨损和鱼竿耐久上限来自当前装备定义。当前剩余耐久只属于绑定 `RodItemInstanceId` 的装备实例，每个固定步的磨损写回该实例，Session 只复制同一值；新会话、切线、换人和收杆不恢复耐久。力量超过旧承载值不再结束本场；耐久归零以 `RodBroken` 写入真实损坏并拒绝再次抛竿。`UCatFishingSettings` 保留资产软引用、固定步与持竿姿态等技术设置。具体字段和诊断过滤词见实现导读，不再从旧 `Fight|Spec` 设置页或测试鱼快照推断现行参数。

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
- **线段方向** = 抄手 `Character` 的水平面朝方向（`Actor Forward`），不读取 `Controller/Camera` 朝向；自由转动镜头不会改变挥网方向
- **圆半径** = 鱼定义的 `ScoopTargetRadiusCentimeters`（圆心随鱼移动）。**为 0 时一律拒绝抢抄**（fail-closed）
- 射线**不带俯仰**：鱼在水下看不清，逼玩家瞄准深度会变成盲操作；而且现实里站高一点更好捞，3D 判定反而会让站得高的人够不着。高度只由 `MaximumScoopVerticalDeltaCentimeters` 单独卡上限
- 每次服务器接收的真实挥网尝试都会消费 `ScoopCooldownSeconds`（当前 3 秒）：GAS 的 `Cat.Cooldown.Fishing.Scoop` 提供本地预测与 UI 剩余时间，`UCatFishingCommandComponent` 的每玩家服务器闸门负责拒绝绕过 Ability 的重复 RPC；挥空同样消费，其他玩家的冷却互不影响

**开放阶段：`HookedFight` + `NearShore` + `ExhaustedReel`。** 鱼身上的圈**一直存在**，鱼的剩余体力完全不参与抄网判定——满体力鱼只要已经上钩并进入射线范围也能直接抄走。更早的阶段不开放：鱼还没被提上钩，抄它等于绕过提竿机制。

其余谓词：抄手在岸上（Outside 水域）+ 地面坡度 ≤ `MaximumScoopGroundSlopeDegrees` + 视线不被遮挡 + 装了 ScoopNet。
当前测试配置启用独立的 `bAutoGrantStarterScoopNet=True`，`StarterScoopNetDefinitionId=StarterScoopNet`。每个玩家占有新 Character 后，由 Equipment 的 `GrantStarterScoopNetIfConfigured` 通过正式库存事务补齐一把抄网并自动选中，占一个背包格；已持有任一完整抄网时复用已有装备，同一 Character 成功处理后不因重复占有或移出背包再次补发。新角色/新世界使用新库存，重新执行一次。定义无效或库存满时明确拒绝并记录日志，不覆盖已有物品。整套 `bAutoConfigureStarterLoadout` 仍关闭。这是商店接通前的临时测试来源，正式抄网资产为 `/Game/Catfishing/Data/Equipment/Equip_ScoopNet_Starter`；后续先关闭开关，再删除临时发放入口、占有调用、一次性记录及专项发放测试/脚本断言，保留正式资产、普通入库和抄网捕获链。
**不再要求"鱼在近岸带内"**——射线∩圆已是唯一范围口径，再叠一层离岸距离会出现"debug 圈画成绿色但服务器拒绝"的表现/判定打架。`NearShoreWidthCentimeters` 仅用于外部 StateTree 请求进入 NearShore 时校验真实鱼位置，不参与抢抄距离或自动推进会话阶段。

首个合法 F 会生成一个 `ACatFishPickupActor`，并立即调用与岸上死鱼按 E 相同的嘴叼交接；此时鱼仍是世界 Actor，不进入背包或鱼护。玩家之后对具体地面鱼护按 E，才由 Items 执行唯一容器提交与图鉴归档。一次 F 用同一个 `RequestId` 串联 `scoop_target_selected`（或 `scoop_target_selection_failed`）、`scoop_rejected`、`fishing_scoop_terminal` 与最终 `fishing_command_result`。拒绝日志除逐项谓词和距离/高度/射程外，还同时保留角色中心、胶囊足底和地面命中点三组 WaterQuery 的错误枚举、Inside/Boundary/Outside、Region/几何版本、垂直差和带符号岸距；后两组只用于诊断，不改变当前以角色中心为准的权威规则。由此可以区分“角色中心高度超差”“脚下在水域内/边界”“没对准”“太远”“地面或视线不合法”。

鱼进入 `ExhaustedReel` 后还有第二条正式收尾路线：服务器立即复制 `AutoHauling`，各端据此让鱼侧翻；同一个约束继续负责收线/持竿者平移的拖动。力竭鱼的游向为零，到达竿尖正下方也属于合法状态，不再走活鱼的非零水平游向校验。未确认真实干地前保持水面高度；若烘焙水域轮廓已结束但地表射线仍命中水面，真实拖拽继续保留候选 XY 并逐步重查，不弹回水域内缩点。只有命中高于水面的真实表面才锁定 `Beached`，此后每个固定步按当前 XY 调用 `FCatWorldSurfaceResolver` 更新权威地面高度。干地鱼进入竿尖的水平 `LandingCompletionDistanceToRodCentimeters` 后，原地生成复制的 `ACatFishPickupActor`；不使用握把距离，交接帧松开左键也不会阻止生成。所有玩家都能以准星锁定并按 E 请求拾取，服务器复核距离、视线和物品状态，首个合法请求获胜。抄网与岸上拾取从这里开始共用同一条“嘴叼世界鱼 → 对具体鱼护 E → Items 唯一提交”链；Session Outcome 分别为 `Caught` 与 `Landed`。关键日志为 `fishing_fish_exhausted`、`fishing_beaching_deferred Result=ContinueSurfaceTow`、`fishing_fish_beached` 和 `exhausted_fish_pickup_spawned LandingTarget=RodTip`。

---

## 3. 表现层契约（五条稳定接缝）

表现只订阅，永不写回。逻辑公式怎么改，只要字段**语义**不变，表现层零改动（遛鱼公式整个重写已验证）。

| 接缝 | 内容 | 用途 |
|---|---|---|
| Ability 钩子 | `BP_OnLocalInputActivated / Released`（本地端、提交前、不带结果） | 挥网/甩杆/提竿抬手等"成败都播"的即时动作 |
| Snapshot/ViewBridge | Phase / bReeling / bSlacking / bPerfectHook / NormalizedFishStamina / FishMotionIntent / Outcome | AnimBP 状态机、HUD、结果演出 |
| 表现 Actor 事件 | Rod/Hook/Fish 的 `BP_On*PresentationChanged` + `BP_Play*Event` | 阶段外观与附加音画；鱼基础 Mesh/AnimBP 由鱼种库直连，不在事件内按 ID 重选 |
| 窝点公开态 | 中心/半径/过期时间（`GameState.ChumFieldReplication`） | 窝点光环表现（BP 类经 `ChumFieldPresentationClass` 配置） |
| AimLibrary | `ResolveCastAimPoint / PredictChumThrow / ChargeAlphaFromHeldSeconds` | 预览与服务器**同一份数学**，所见即所得 |

反向纪律（唯一红线）：表现事件里不发命令；Montage 完成 / AnimNotify 不作为任何玩法提交条件。

鱼的体重、力量与视觉大小使用同一条服务器事实链：服务器先为每个候选鱼种按稳定随机流抽取个体 `WeightKilograms`，以 `Weight × StrengthPerKilogram` 计算挑战度和本场基础力量；选中后不再重抽。完美中鱼只在该基础力量上乘性格倍率。视觉再按
`Scale = clamp(cuberoot(Weight / ReferenceWeight), MinScale, MaxScale)` 计算一次 `VisualScale`。水中
`FishEncounterActor` 与水面 `FishPickupActor` 都复制这个标量，并只缩放各自的 `FishMesh`；Actor 根节点、
抄网圆、拾取 Sphere、鱼线与岸线判定不随 Mesh 大小变化。这样多人尺寸一致，收鱼交接也不会产生大小跳变。

---

## 4. 调试可视化

`UCatFishingDebugSubsystem` 的世界调试标记由 CVar `cat.Fishing.Debug` 控制（默认 0，需要调试时执行 `cat.Fishing.Debug 1`）：
青色湖边界 / 抛竿瞄准绿球 / 窝点绿圈+剩余秒 /
钩子蓝球 / 鱼球（红=发力·绿=累了）+ 鱼线 / 近岸翡翠圈 / 屏幕阶段提示（线长·拖放·完美）。

右上角三方数值面板使用独立 CVar `cat.Fishing.Stats`，默认 1：第一行显示当前复制快照的稳定 `FishDefinitionId`（无鱼时为 `--`），鱼数值行显示当前/上限体力与有效力量（含完美中鱼折减），鱼竿统一显示 `ROD Durability` 当前剩余/上限与钓组力量，猫显示 ASC 当前/上限搏斗体力与钓鱼力量。鱼竿战内读取 Session 对同一装备实例的镜像，战外读取 Equipment；未知当前值显示 `--`，不以定义上限假定满耐久。无需手动开启，执行 `cat.Fishing.Stats 0` 可单独隐藏；它不会修改 `cat.Fishing.Debug`，后者保持默认关闭，开启世界调试也不会改变数值面板开关。Development 落盘日志中的 `LogCatFishing/Event=fishing_stats_overlay_registered` 记录当前 World、面板开关和绘制回调注册结果；注册成功不等同于画面已经人工验收。

Q 蓄力黄色抛物线与落点球是玩法瞄准反馈，不属于上述两类调试信息；它继续由 `cat.Fishing.ChumPreview` 独立控制并默认开启。
命令链每条回执有结构化日志：过滤 `LogCatFishing`，失败为 Warning 且带 Error 枚举。命令/抛竿/打窝回执统一附带 Controller、PlayerState、脱敏 StableNetId、`IsLocalController`、NetMode、Pawn 权威位置/Role 与控制朝向；会话终态附带鱼、竿尖、钩、Encounter 和操作者上下文。高频物理诊断按状态变化或每秒限频输出，异常单步和终局边沿不被限频吞掉；不逐帧刷屏。原始 StableNetId 只有 `StableNetIdExposure=Enabled` 时才允许出现。

## 5. 关键资产与配置

```
Content/Data/Abilities/   DA_CatAbilitySet_Default(6条) · DA_CatAbilityInputConfig(6条)
Content/Data/Equipment/   DA_Rod/Bait/Float/ScoopNet/Chum_Basic
Content/Catfishing/Data/Fish/  正式 Fish_* · Bite_* · Fight_* · Presentation/FishPresentation_*
Content/Catfishing/Fishing/Animation/Fish/  无骨骼 ABPT_CatFishBase · 每鱼 ABP_Fish_*
Content/Data/Fish/             未注册的历史测试二进制（不得作为运行入口，待编辑器引用审计后清理）
Content/Data/Curves/      Curve_ChumSaturation(1→3) · Curve_ChumDistance/TimeFalloff(1→0)
Content/Data/StateTrees/  ST_RunFlow · ST_FishingSession · ST_FishFight
Config/DefaultGame.ini    10 个 section（改后必须重启 Editor；软引用资产必须真实落盘）
```

鱼表现的唯一入口是 `Fish_*::PresentationDefinition`。每个 `FishPresentation_*` 保存 Mesh、子 AnimBP、四类动画、
参考重量、最小/最大缩放和三种局部 Transform；运行时没有按鱼名猜 Mesh/比例的平行配置。所有子 ABP 继承同一个
无 Target Skeleton 的 `ABPT_CatFishBase`，播放速率与状态机只维护一次。

数值快照：猫力50 体力100 ／ 竿强60 耐久70 线长1500 ／ 鱼力40 体力50 ／ 真咬窗3s 完美窗1s ／ 近岸100cm ／ 鱼竿操作位默认4个；140 cm 仍为兼容站位参考间距，不驱动加入瞬移。
开发便利开关：整套 `bAutoConfigureStarterLoadout=False`；独立临时测试开关 `bAutoGrantStarterScoopNet=True` 只为新玩家角色补齐一把抄网并选中，商店获取接通后删除这条路径。

## 6. 已知待办（都在契约后面，不影响表现层）

- 浮漂弹道解（飞行轨迹落不到目标点，靠落水轮询吸附，视觉有"瞬移"）
- 咬钩公式改版：读取所在面积单元的聚鱼总量、浮漂级计时器、总量变化时比例折算；正式总量→等待时长曲线待裁
- 窝料改版：水域面积/鱼总量/鱼种库存账本、鱼种平均分布、互斥面积单元、共享重叠收敛曲线、守恒重分配与面积容量上限
- 抄网规格版：概率/硬直/无网拾取/翻肚 30s 苏醒（会新增 Phase/Intent 枚举值→表现层届时"补分支"）
- 浮漂精准偏移、入夜停咬、拽尾巴救援(W3)、巨鱼协作表现输入
- 多人采用本页 2.0 的移动合力与个人账本方案；低体力强制换人、虚脱双倍恢复和 50% 再入门槛不属于当前已确认规则。正式多人 HUD 已接线，交付验收状态见下表。`FCatFishingFightExchangeTask` 是历史 StateTree 交换入口：源码仅保留对应节点调用，正式资产生成器只接入 FightRunner，Runner 运行期间 Session 拒绝旧交换入口。目前没有已确认的巨鱼或其他运行消费者；由于二进制资产引用尚未完全核实，暂留待编辑器引用审计，不能作为常规耗体调参路径或已确认的巨鱼兼容方案。
