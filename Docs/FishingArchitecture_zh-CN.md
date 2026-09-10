# 钓鱼核心架构（技术文档）

阅读对象：需要理解/修改钓鱼玩法逻辑的人。运动链说明于 2026-09-04 按源码核对，当前细节统一见 [鱼运动与遛鱼逻辑实现导读](FishFightImplementationGuide_zh-CN.md)；本页负责系统关系与入口导航。
配套文档：蓝图配置见 [FishingBlueprintSetupGuide_zh-CN.md](FishingBlueprintSetupGuide_zh-CN.md)；规格口径见 [FishingCoreFlow_zh-CN.md](FishingCoreFlow_zh-CN.md)。

## 2026-09-10：删除旧钓鱼失败预算

用户明确要求安全删除旧重试/失败预算方案。删除前扫描 1,887 个项目及插件资产包，旧类型/字段的 ASCII 与 UTF-16 序列化名称均零引用；UE 加载并读取 3 个 StateTree 的任务、条件、转换、全局任务及参数，其中 `/Game/Data/StateTrees/ST_FishingSession` 未接旧任务。资产保持原文件，不生成替代树。检查脚本为 `Scripts/audit_fishing_failure_removal.py`，证据位于 `Saved/Automation/FishingFailureRemoval/Before/Audit.json`。

本次移除 Session 的两个旧入口、两项缓存，StateTree 的两个旧 Task 及实例参数，Equipment 的额外失败惩罚写口/枚举/结果/缓存和默认 0 的 `RodFailureDurabilityLoss`，以及 Collection 的旧重试耗尽剪影生成入口/去重表/退出清理。正常 Fishing Use 扣饵、实例耐久磨损、维修、服务器终局、复制回执及其 Development 日志不变；UI/WBP、资产生成器、Cook 入口和持久化 schema 不变。旧 `fishing_silhouette_committed` 日志随其唯一旧生产者移除。Profile 的 `FishSilhouette` 枚举及既有存档/Journal/ACK/显示消费者保留；没有新增上钩或空竿揭剪影触发。

实施基线为物理集成检查点 `6b04247`；先前并行源码已由所属任务独立提交，本轮仅恢复自身清理补丁。用户已有 `Content/Animalia/Cat/Meshes/Cat_Skeleton.uasset` 修改未改写、未提交。删除前局部基线 13/13 通过（1 项含测试主动取消会话的既有 warning），不把它当作本轮失败。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 旧入口/状态/终局 | `Fishing/CatFishingStateTreeNodes.*` 的 FailureBudget/ResolveRetryExhausted 两任务调用 `CatFishingSession.*` 同名旧入口；正式生成器无对应任务 | 删除会话额外惩罚和重试逃鱼终局；正常模拟 Escaped、Cancelled、RodBroken 时序不变 | 删除两任务、实例参数、两接口和会话幂等缓存，保留 `FinalizeSession` | 先 UE 读取现存树、确认无消费者，再删除反射类型 | 编译及正式树咬钩 Automation；不凭纯文本搜索裁定资产安全 | 已删除，前后审计 3 个 StateTree 无旧节点；Editor/Game Development 编译通过 |
| 权威资源写入/回执 | `Equipment/CatEquipmentComponent.*` 的 CommitFishingFailure 原扣 1 份特殊饵或固定耐久；Types/Settings 声明专属结果、惩罚枚举、默认 0 的 RodFailureDurabilityLoss | 移除额外扣费；普通/特殊饵正常预留、提交、释放，以及绑定实例磨损和维修不变 | 删除唯一旧写口、恢复时缓存清空、结果/枚举/缓存/设置字段；保留 `BeginFishingUse/CommitFishingBaitDeferred/ReleaseFishingUse/ApplyFishingRodWear` | 审计 Blueprint/DataAsset 序列化引用后删除；无双写过渡 | 现有 BaitRestock、MultipleRods、RodDurability 回归；数量仍为份、耐久单位不变 | 旧生产符号无源码/插件/配置消费者；局部回归结果见下文 |
| 剪影/持久化/退出 | `Collection/CatRunImprintService.*` 的 RecordRetryExhaustedSilhouette 原只被旧终局调用；私有去重表在 Deinitialize 清空；Profile/Controller/UI 消费 FishSilhouette | 移除未接入的旧生产者，保留既有记录、稳定枚举值、Journal/ACK 和图鉴显示；不新增上钩/空竿触发 | 删除旧服务方法、专属去重表和退出清理；只更正 `CatProfileContracts.h` 注释 | 先确认旧生产入口不可达；保留 Profile schema 和合并函数原实现 | `Collection/Tests/CatFishingArchiveCompatibilityTests.cpp` 验证捕获 Grant 幂等、待 ACK 状态及剪影 SaveGame/Journal 往返 | 服务无旧入口，Profile 实现和存档 schema 无 diff；新增回归结果见下文，设计新触发仍未完成 |
| 资产/配置/脚本/Cook/表现 | `DefaultGame.ini` 绑定 `/Game/Data/StateTrees/ST_FishingSession`；`CatFishStateTreeAuthoringLibrary` 生成正式树；全部项目/插件包由审计脚本读取 | 无需改资产、默认运行配置、Cook 入口、WBP/UI 或动画；旧类型删除后原树应照常加载 | 新增只读 `Scripts/audit_fishing_failure_removal.py`，扫描序列化名称并由 UE 读取所有项目 StateTree 拓扑 | 旧二进制审计 → 删除及构建 → 新二进制再审计；不重建替代资产 | 前后 Audit.json、树文件 SHA-256 对比和实际咬钩树回归 | 两次均 1,887 包/3 树通过，零旧引用/零审计错误，树文件哈希完全相同；UI/动画变更不涉及 |
| 测试/日志/文档 | `CatFishingStateTreeNodesTests` 旧参数断言；`Docs/Architecture`、本页及 09-09 差距审计表描述旧链 | 删除旧断言与旧生产者日志，纠正“任务类存在即实际接入”的误判；保留真实扣饵/磨损/终局诊断 | 更新同职责链 9 份现有文档及唯一进度入口，保留新剪影触发缺口；不新增业务账本 | 对照最终 diff 与日志；测试不代替正式图鉴/整场交付 | 过滤 `equipment_fishing_bait_committed`、`equipment_rod_wear_applied`、`fishing_session_terminated`；旧生产日志已移除 | 旧检查/文档入口已更正；本轮未运行新包无 `-log` 双端日志和真人画面验收 |

验证结果：`contract`：Editor 与 Game Win64 Development 构建均 `Succeeded`（`Saved/Automation/FishingFailureRemoval/EditorBuild.log`、`GameBuild.log`）；局部 Automation 15/15 成功，14 clean、1 项含与基线相同的 3 条测试主动取消会话 warning，0 failed/notRun（`FinalTests/index.json`、`FinalTests.log`）。新增两项覆盖捕获 Grant 幂等/待 ACK 状态与剪影 SaveGame/Journal 内存序列化往返，不冒充真实玩家存档落盘验收。`runtime_behavior`：`Before/Audit.json` 与新二进制 `After/Audit.json` 均 1,887 包/3 树通过，树包 SHA-256 不变；现有真实 World 咬钩状态树、饵料预留/提交/释放、跨会话耐久和维修回归通过。日志见同目录 `After.log` 和 `FinalTests.log`。`presentation_delivery`：本轮未做正式图鉴画面、双端真人联机或 Cook/打包、新包无 `-log` 双端落盘验收；不以局部检查关闭图鉴或 Fishing 模块。

## 2026-09-09：正式物理抓握接入审查

本轮目标按用户最新确认修正：把物理试验场的身体和抓握接入正式角色。只有主控进入钓鱼会话，其他人抓住竿或猫后，以普通身体移动和真实约束传力；抓握不产生会话成员、共享费用或自动接任。R 保留取出/放下自己鱼竿的功能，取消 R 加入他人会话。角色仍是 `ACatCharacter`，ASC、Condition、Inventory、Equipment、会话和费用仍由原系统拥有。这里是变更审查材料；业务模块是否完成仍只见 `Development/需求对齐差距清单.md`。

修改前基线：HEAD `c491786`，工作区仅 `Content/Animalia/Cat/Meshes/Cat_Skeleton.uasset` 有并行改动，本轮不得覆盖。历史钓鱼191项中190通过、1项初级竿耐久150/500既有失败，原型13项通过；本轮接入构建/运行/表现验证尚未运行。只读正式资产审计见 `Saved/Automation/PhysicsGrabProductionAudit-20260909/AssetAudit.json`：猫仍用 Cat/ABP_Cat；杆网格 NoCollision；HUD无固定R加入文案。审计旧验证副本报告 FrontendRoot 缺 LoadingPage；当前源码已无此绑定，先归为旧模块与新资产不匹配，须用新构建复核，不能据此认定正式项目已有故障。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 身体/生命周期 | `Character/CatCharacter` 宿主ASC等组件；`CatCharacterMovementComponent`实际移动；`Physics/CatPhysicsPrototypePawn`独立刚体 | 保留Character身份；服务器刚体代替CMC实际位移 | 共享`CatPhysicalBodyComponent`供正式/原型使用；停CMC积分与原移动复制 | 身体接收方→输入→查询消费者 | 互推/双爪/跳跃/倒地/网络；正式420cm/s起跳、重力1，N到kg·cm/s²须×100 | 共享身体接入已实现；`Report-Main-20260910-095837` 的 Body/Prototype 互推、墙碰撞、真实双爪、倒地接地恢复与生命周期回归通过；`Report-FormalClient-Prototype-20260910-100725` 正式双端抓人拖动25.028cm及失焦释放通过，正式跳跃420cm/s/重力1由095916及095959报告验证；完整预测/回滚未实现 |
| 输入 | `CatAbilityInputBindingComponent`→ASC，`IMC_InputContext`左右键Primary/Slack、R RodInteract | 空手/辅助伸爪，主位收放线；R不加入别人 | 按下记录路由归属，释放/取消/Pawn切换返回原路 | 先抓握接收方后切输入 | 换主、松手、菜单/失焦，不残留输入 | 已接通原IMC的Grab/ASC按下归属与原接收方释放；Four-StanceYield-103353和SlackAim-StanceYield-103519实际联机通过（带初始化警告），菜单/失焦/切Pawn路径已有回归。104519 Owner通过；追加104823/104910/104958三次18.45–22.4秒等待均真实退过旧岸地后沿并完成GAS提竿收线，同GripId保持 |
| 成员图 | Grab真实约束；`FishingService::OperateRod/LeaveRod`→Rod名单→Session接力 | 一次按键占位改为持续连通关系；猫/竿为节点，地面不入图 | 抓握前校验容量/跨竿，Service唯一reconcile；双手与环路去重；主位须直连竿 | 校验→约束→成员事务→Runner冻结 | 双手松一只/断中间链/四人/双竿/倒地/销毁 | 已撤销该未交付方案，按下表单主控入口替换；真实约束不推导会话成员，无成员图运行入口。 |
| 竿与鱼力 | `Rod::RefreshHeldTransformFromAuthority`写组均值姿态；杆BP SceneRoot/NoCollision；Runner旧净加速度已扣猫力 | 真实竿双向约束，三维张力只作用竿尖一次 | 新物理竿组件接收`LineTensionNewtons×100`；锚点读真实姿态；停旧组位移写口 | 物理竿→持握→Runner输出 | 地面/侧拉/碰撞/松手落地，不能重复鱼力矩 | 已以PhysicalRod真实刚体、持握约束与唯一竿尖线力替换组均值姿态/净猫力写口；Report-Target-20260910-102832的实际20N冲量、重鱼8秒、小鱼10秒及正式4case均通过，N到UE力仅乘100一次；正式竿速278–279cm/s，抓地可阻挡拖动。打包与多人画面验收另列缺口。 |
| 数值/费用 | Runner→GroupModel/Simulator→CMC共同运动；ASC单一费用提交 | 停虚拟猫端位移预测、队形纠正/组内碰撞忽略；各猫自身motor | 显式真实竿端点输入；个人力量/体力限制motor，保留助手0.5、费用/线杯/资源权威 | 实际运动读取→单次结算→发布力 | 相反方向、零体力、断链当步、固定步费用和最新鼠标停止契约 | 本行辅助系数方案已撤销，改为下表核心计算/费用的单主控本人账本；无共享费用或辅助倍率运行路径。 |
| 动画/镜头 | ABP_Cat、Character GetMesh/Montage、`CatFishingCameraComponent` | 保留正式动画源，增加可见Poseable爪IK与物理状态 | 同一视觉组件支持ABP源；OwnerNoSee指向最终可见Mesh | 身体样本→ABP→IK→相机 | 跳跃/蒙太奇/远程Tick/第一人称；隐藏挂件引用尚待核对 | `Report-Main-20260910-095837` Camera两项及原型姿态/爪IK回归通过；`Report-FormalJump-Render-20260910-095959` 正式ABP三阶段及blend-out通过，已查看Land和恢复站立截图；最新转杆镜头见`Report-SlackAim-StanceYield-20260910-103519`。Montage与隐藏挂件专项未运行，打包表现未验收 |
| HUD | WBP_CatHUD/InteractionPrompt实查无固定R加入文本，HUD Model消费Fishing View | 增加抓握/松手/主辅提示 | 现有正式View DTO与WBP控件、原生成脚本同步 | View→WBP→实际显示 | 不新建第二套HUD；FrontendRoot既有错误单独记录 | 正式WBP已迁移，HUD-AfterCleanup-101442六项全部clean，验证原背包按钮/本人ASC体力/左右爪/松手文本及退出清理；正式1280×720客户端图已可读。HUD无辅助会话或共享账显示；多人真人画面与打包默认落盘尚未验收 |
| 身体状态/存档/退出 | Condition脚点；Camp GetDefaultHalfHeight/TeleportTo；SaveSubsystem恢复Transform；UI停止CMC；GameMode退出托管 | 胶囊尺寸不再代表物理身体；瞬移须同步三刚体 | Character统一脚点/高度、Body传送先清双向连接；只停止自主输入；Downed仍由Condition裁决 | 状态消费者→清理→传送；保留存档schema、无抓握持久化 | 落水/救援/保存恢复/菜单/UnPossess/Destroy | `Report-Main-20260910-095837` 的 CharacterTeleportReleasesIncomingGripAndMovesThreeBodies、ConditionMeasuresPhysicalFeetAtWaterThreshold、SaveRestoreMovesAllBodiesOnceAndPreservesSavedInventory通过；输入失焦两端释放另由100725正式客户端报告验证。三刚体恢复、双向连接清理与库存保存契约成立；Condition用例1条既有警告，整场救援/撤离表现未验收 |
| 网络/日志 | 原型输入epoch/序号/可靠抓握、正式名单版本/命令回执 | 唯一服务器模拟，客户端只消费身体/爪/成员事实 | 共享快照、控制失效域，GripId/BodyId/RodActorId/SessionId关联日志 | 先接收再生产 | 迟到/乱序/超时/断线；Development默认落盘 | 服务器权威Body/Grip与主控复制已衔接；100725正式客户端抓握/释放两端revision一致；`Report-Four-StanceYield-20260910-103353` 四端主控1→0、辅助不进Session、不自动接任及伪造命令拒绝通过。PIE落盘日志可用BodyId/GripId/RodActorId/SessionId关联两端事实；客户端仍仅插值，Development包无-log双端落盘未验收 |
| 配置/资产/旧路径/验收 | 正式BP/IMC/DA、HUD脚本、PhysicsGrab验证脚本、Fishing/Character测试与本页相关文档 | 生产共享身体；试验场继续作为实验入口 | 核对正式资源引用与Cook依赖；迁移过期队形测试/文档；未确认二进制兼容入口不硬删 | 实现→构建→运行→正式表现→diff清理 | contract、runtime_behavior、presentation_delivery分层；Cook/包无证据不得宣称通过 | 共享身体和唯一主控已进入正式BP/IMC链；子步配置统一，旧组运动/分账C++与HUD生成入口已清理。100BP/1536节点/3562引脚只读审计0加载失败，正式HUD6/6通过；已确认序列化兼容消费者见下方清理边界，Cook/新包尚未验收。 |
| 隔离构建 | `Build/Automation/verify_physics_grab_prototype.ps1::Sync-ValidationSource`复制Source/Config，独立Binaries/Intermediate | 保留旧时间戳可能导致已变头文件早于obj，旧副本文件也不能参与新构建 | 仅清理隔离Source/Config的过期文件；按内容hash复制并刷新变化文件时间；`NoHotReloadFromIDE`只用于隔离构建 | 先修脚本，再完整重编当前项目源码 | 不遍历共享Content；不关闭用户Editor；核对模块hash与SourceManifest | 脚本检查点f469c62保留；当前共享工程Editor 104402、Game 104601 Win64 Development均成功；SourceManifest-20260910-104402.json及同戳ModuleHashes归档。IK/失败预算并行任务暂存自身增量后冻结，本快照只含物理集成。 |
| 客户端HUD装配 | PC `OnRep_Pawn`→`CatLocalPlayerUISubsystem::AttachPlayerLakeUI`→`CatInteractionPageController::Bind` | 旧增量模块在客户端装配崩溃，疑似头文件布局与obj不一致，尚未证明生产逻辑错误 | 先全源重编复验；若仍崩溃再检查装配重入/对象生命周期 | 构建一致性先行 | 正式客户端与四人冷启动加入；无持久化/配置迁移 | 正式Main DLL单主控双端抓猫/失焦通过；Report-FormalClient-SingleOperator-173113和FormalRender-SingleOperator-173113，含ABP初始化除零/启动陈旧输入警告；未改UI装配逻辑，正式模型尺寸表现另列缺口 |
| HUD资产生成 | `Scripts/migrate_physical_grab_hud.py`→Editor `CatFrontendWidgetAuthoringLibrary::CompileAndSaveHUDWidgetBlueprint`→`/Game/UI/HUD/WBP_CatHUD` | 新建控件需要同步蓝图变量GUID后编译保存；正式文件锁已解除 | 复用现有CompileRegisterAndSaveWidget并限包路径，使用当前Main DLL生成并重载正式HUD | 接收控件→生成→重载→实际HUD消费 | 不改Skeleton/原前端资产；核对受保护文件hash | 正式HUD回填成功，SHA256=33B2E74D4D7900B04ADBF8BE28DA8DD2C9E0B5FFB4064951DC3BEFD9284A2549；HUDSingleOperatorMigrationAndReadonlyAssets.json，Report-HUD-SingleOperator-173113为6/6无警告；1280×720正式Slate图可读抓握/本人控件 |
| 联网转杆回归 | `Editor/Fishing/Tests/CatFishingSlackAimNetworkTests.cpp::FRestore/FVerify`旧CMC/虚拟力/角度预测夹具 | 保留启停、超时、stroke基准、拒绝客户端权威写、镜头连续性；不保留旧虚拟平衡公式 | 正式GameMode准入、真实站地/抓竿、ControlObservation读取 | 身体与物理竿→测试→真实两端 | 丢包超过鼠标超时但短于身体超时，并断言抓握仍在；无资产/存档写入 | `Report-SlackAim-StanceYield-20260910-103519` 使用102757模块通过；真实UI/PC输入、鼠标停止、丢包超时保留握点、重新stroke和相机连续性全部成立；server/request yaw85.533度一致，client/camera84.897度，握点误差0.775度，镜头最大单步6.234度；0错、1初始旧epoch警告 |
| 联网转杆输入准备 | `CatFishingSlackAimNetworkTests.cpp::FVerify` 在正式 UI 绑定前建握；`Automation-SlackAim-Physical-20260909-182925.log` 中初始菜单绑定后 Flush 到达服务器，握点与 Holder 被清理；`Automation-SlackAim-Formal-20260910-100430.log` 又证明夹具每帧主动 Refresh 会不断重绑菜单 Action | 正式 PC tick、UI Flush 与 75 秒上限不变；先只读观察同 Pawn ACK、库存模型/控制器、正式菜单 Action 绑定及真实输入帧，再建立主控握点，观察自身不重建绑定 | 测试只读取 `UCatLocalPlayerUISubsystem` 和 `UEnhancedInputComponent::GetActionEventBindings`，移除每帧 UI Refresh；正常装配继续由 PC 生命周期发起；`CatfishingEditor.Build.cs` 增加私有 EnhancedInput 依赖 | 先生产 UI/输入装配完成，再身体姿态与 epoch 复制，最后实际握点、主控和鼠标协议 | 保留真实服务器释放与 RPC、启停/超时/重启、镜头断言；依赖仅 Editor 测试，不进入 Shipping Game；无资产、资源或存档写入 | 只读准备门已修正并验证；`Report-SlackAim-StanceYield-20260910-103519` 观察正式UI菜单绑定及3个真实输入帧后建立握持，不主动Refresh、不关闭PC或吞Flush；后续超时/恢复协议通过。100430准备门自激失败报告保留；最新0错、1初始旧epoch诊断警告 |

### 用户澄清后的替换盘点（修改前）

上表“成员图、辅助力量系数、共同账单、自动接任”是澄清前正在实施的方案，尚未交付。以下行取代它们；身体、存档、动画与真实鱼线力的回归契约不变。工作区保留全部本轮未提交改动和用户 Skeleton 并行改动；最近隔离构建在删除旧运动字段的中间快照失败，消费者已迁移但尚未重新编译。此前正式双客户端抓猫/松手通过；四人旧会话测试失败，不能作为新语义验收。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 主控入口/状态 | `Fishing/CatFishingService::PlaceRod/OperateRod/ReconcilePhysicalGripMembership`；`Rod::SetPhysicalOperatorsFromAuthority`被Service调用，`PhysicalRod::TickComponent`每帧触发图同步 | 当前未交付实现把连通猫投影为最多4人成员并自动接任；目标只有显式取竿的主控为0或1操作人，纯抓握不授予控制或物品所有权 | Service显式取得/释放唯一主控；删除图成员推导与自动晋升；旧反射数组仅投影0或1，原控制世代/命令幂等保留 | 先唯一主控写口→取竿/退出→删除图写口 | 主控R取竿/放下、普通抓握、主控退出无接任；BeginCast与资源写入仍唯一 | 已改PlaceRod/OperateRod显式原owner授权，CommitPrimaryHold在事务成功后保留真实握点，失败撤销新授权；纯抓握不授予主控且退出无晋升。102832真实约束/双竿测试与Report-Four-StanceYield-20260910-103353四端1→0、伪造控制拒绝通过；最终104519及三次OwnerLongWait专项通过，后者实际越过旧岸地边界且正常提竿收线。 |
| 通用抓握/真实传力 | `PhysicalRod::ValidateGrip/UpdateMotorBudgets/UpdateMouseControl`；Grab创建真实约束前调用竿校验；当前按Operator数组分配motor/鼠标反力 | 目标辅助猫始终普通移动，不受会话辅助系数、人数容量或跨竿会话规则约束；侧向、反向、抓猫链都由Chaos决定 | 删除会话容量/跨竿成员门与辅助motor改写；鼠标力矩预算与反力只归主控；保留接触距离、权限/物理有效性校验与单次竿尖鱼力 | 主控状态→物理receiver→抓握校验 | 四人真实约束、抓两杆不串会话、不转归属、松手断力；力N×100、力矩kg·cm²/s²不换义 | 已删除容量/跨竿会话门、辅助motor与鼠标反力分摊，仅主控接本人预算；102832的RealConstraintsDoNotGrantControlAndIsolateSessionLoads覆盖双手、环路、第五只猫、直抓竿、两竿隔离及退出无接任。真实重鱼/小鱼/静态握持均通过；103353四端真实约束与主控隔离通过，未宣称客户端预测或打包表现已完成。 |
| 核心计算/费用 | `Session::RefreshOperatorMembershipFromAuthority/Refresh...View`→`Runner::RefreshParticipants/Freeze...`→`GroupModel`→各ASC | 当前冻结辅助ASC并汇总力量/分账；目标只冻结主控ASC与身体样本，收放线/转杆/主控用力仅结算本人；辅助不登记会话、不扣钓鱼账 | 改单主控计算/付费，清理无消费者的分账与辅助公式；辅助助力只通过竿尖真实位置/速度进入鱼线求解 | 先单人计算接收方→Runner→Session视图→删除旧纯C++模型专属逻辑 | 主控零体力、放线恢复、鱼力竭、终局清理、8秒实际鱼线/刚体耦合；辅助不改变会话账单集合 | 已用OperatorState与OperatorWorkModel替换GroupModel分账，Runner只读取并单次提交主控ASC，移动/支撑/收放线/转杆归本人；物理助手不加入账本。Report-Target-20260910-102832正式4case均8秒160步/160次ASC写入，取消后无新增费用/磨损；原小鱼零体力300/0及抓地对照全部通过，全房选鱼资格保留。 |
| 输入/UI/表现 | Binding按PressedRoutes锁Grab/ASC；PC查主控；HUD消费Fishing View；相机查`IsPrimaryOperator` | 辅助抓竿不再变主控，因此仍用鼠标伸爪；主控R取竿后鼠标钓鱼，HUD不显示辅助身份/共享体力 | 保留正式IMC/DA和主控镜头；更新只读HUD、既有WBP生成默认文案与测试，无新增抓握键资产 | 主控判定→输入/HUD消费者→暂存HUD生成/正式回填 | 抓竿/抓猫时无钓鱼HUD接管；菜单/失焦不误抛竿；正式客户端Slate图 | 原IMC/DA与PressedRoutes已接通唯一主控分流；正式HUD六项clean、Four和SlackAim最新双端输入回执通过，镜头仍只由主控驱动。WBP hash保持33B2E74D…，没有新增抓握键资产；OwnerR长等待夹具最新复测修正仍待验证，不据UI通过代替它 |
| 网络/退出/资源 | Service生命周期清理→Session无人值守；Snapshot/Rod数组及ControlEpoch复制；Equipment与资源托管原入口 | 辅助进出不刷新会话成员、输入世代或设备版本；主控退出不自动转让，会话沿既有无人值守规则；物品归属与存档schema不变 | 保留可靠抓握、身体快照和主控复制；移除接力事务；托管/终局只按实际绑定原实例处理 | 状态入口→退出清理→复制日志→多人测试 | 各端SessionId/Operator/Owner一致；辅助请求控制被拒；Development文件日志；无新持久化/资产迁移 | 已移除主控自动接力；Report-Four-StanceYield-20260910-103353证明真实R退出后辅助连接保留而操作人数变0、ControlEpoch同步；095837 OwnedRod生命周期/多Session独立资源回归通过。Equipment原精确实例与存档schema保留；新包双端默认日志未验收。 |
| 测试/配置/资产/文档 | `GroupNetworkTests`4人接任、`ParticipantStrengthTests`辅助系数、`GroupModelTests`共享账；Settings/Balance旧槽位/辅助字段；正式WBP及BP读数组状态未全审计 | 新验收为多身体物理合作、单人钓鱼会话；源码无引用不证明二进制无引用 | 替换旧业务断言并删除已确认无消费者纯C++旧模型；反射多槽/Helper字段可弃用留0并列不确定资产消费者；同步现有文档/脚本 | 新入口可编译→contract→runtime→presentation→审查引用后清理 | 受保护资产hash保持；Cook仍由既有BP/DA引用链；未审计反射消费者标未确认，不硬删；正式HUD文件锁仍未解除 | 旧GroupModel/分账/借竿操控专属测试已替换或删除；Settings辅助/多槽字段和旧站位查询经100BP完整重载审计后安全清理，保留具名组件与兼容数组的已确认BP/TestMap序列化消费者。HUD已回填，引用/哈希证据见LegacyPhysicalFishingBlueprintConsumersAfterCleanup-20260910-101421.json；最终Editor104402/Game104601成功，104519整组183项仅原StarterRod耐久既有失败。 |
| 旧交换节点/选鱼人数 | `Session::ResolveFightExchangeFromStateTree`旧反射多人扣款；`SelectFish`→`Service::BuildFightCapabilitySnapshot`→`FrozenSelectionContext.ActivePlayerCount/Combined*`用于全场抽鱼资格 | 前者与Runner重复权威应停用；后者是全场人数/挑战门，不能因Session只有主控而改成单人抽鱼 | 旧反射交换入口保留可加载身份但拒绝执行并记录；保留全场抽鱼上下文的既有含义；`CatFishingSessionTests`更新旧交换断言 | 先停重复写口，再核选鱼独立契约 | StateTree旧节点无法扣款；多人房间原鱼种资格不变；未审计二进制节点不删除反射类型 | 已停止旧交换扣款，LegacyExchangeCannotBillAndRefreshKeepsControlIdentity在Report-Main-20260910-095837通过；SelectFish仍调用BuildFightCapabilitySnapshot冻结全场人数/挑战值，未改选鱼规则；反射入口保留加载身份且明确拒绝。 |
| 主控恢复域/渔获归属 | `Session::SuspendOperator/FinalizeSession/EndPlay`→ASC恢复；`SpawnScoopedFishPickup/SpawnExhaustedFishPickup`原读当前钓手或参与集合 | 当前钓手在无人值守时清空，但捕获归属必须继续记原主控；任何允许放下的阶段都须解除旧体力恢复引用 | `PrepareSession`冻结唯一`CatchFisherStableNetId`用于捕获；`StaminaOwner`在所有Suspend阶段清空，恢复同一主控不补满体力；存档schema与物品归属不变 | 分离捕获身份与输入身份→退出清理→Pickup/终局测试 | 主控放下后他人抄鱼仍保留原钓手归属，物理帮助者无会话奖励身份；旧Session不能恢复别场主控体力 | 已分离CatchFisherStableNetId与当前FisherStableNetId，Suspend/Finalize/EndPlay只清本场恢复域；095837的OwnedRodTransactionsSurviveCancellationAndOwnerDeparture及Session抄鱼/终局回归通过，抓握助手未加入捕获身份集合。 |
| 鱼线与真实端点稳定耦合 | `PhysicalRod`施加竿尖力→Chaos身体/抓握→`Runner`采样→`FightSimulator`固定步张力求解；当前鱼端把采样竿尖当无限质量固定点 | 60/120Hz实际8秒30kg鱼回归出现25.6万/30万N峰值，竿速失控并断握；目标同一张力同时考虑鱼端与真实持握端响应，不再把历史线误差全交给轻竿 | 物理接收方采样竿尖速度/加速度及连通刚体质量、惯量响应，再在Simulator隐式求解共同张力；机械连通只提供kg、cm/s、点逆质量，不生成会话成员或费用；仍只在竿尖AddForce一次，不预测写身体位置 | 先端点响应数据→固定步求解→同一力输出→持续Chaos回归；静态锚点保持零逆质量契约 | 60/120Hz重鱼8秒和零体力小鱼10秒；量测峰值/相邻张力变化、线误差、真实竿速、抓握和身体位移，不放宽失败阈值掩盖发散；网络/资产/存档无新增状态入口 | 已接实际端点位置/速度/差分加速度及Locked点响应，隐式张力经有限力段只提交竿尖一次；PrePhysics调度与有限站定支撑共同闭合。102832 native60/120Hz重鱼8秒峰张力124.648/124.572N、线误差11.360/12.210cm、竿速244.152/247.494cm/s且不断握；正式4case与小鱼300/0/抓地原门槛全部通过，未放宽阈值。 |
| 正式身体与可见脚点 | `CatPhysicalBodyComponent`身体支撑高度→`CatPhysicsPrototypeVisualComponent`同步正式GetMesh姿态→ABP_Cat/可见Poseable | 正式1280×720客户端截图可读HUD，但两只猫腿埋地、伸爪不明显；目标保留正式动画根运动含义并让可见脚点与真实地面一致 | 先测身体/源Mesh/可见骨骼坐标，再修正共享视觉与正式身体的坐标衔接；不改用户Skeleton或用镜头裁剪掩盖 | 坐标证据→必要实现修正→正式走跳落地和伸爪截图 | 正式BP跳跃状态与可见根/脚高度、倒地恢复、双端抓握；资产hash保持，存档schema与费用不涉及 | 正式GeometryScale=2的身体26×10×14cm、站高40cm、手球半径3.6cm已与模型衔接；`Report-FormalClient-Geometry-180338` 对应`20260909-101112-formal-client-grip.png`已查看爪接触和落地；`Report-FormalJump-Render-20260910-095959`及`20260910-020056-formal-jump-grounded-locomotion.png`确认双猫四脚落地，Skeleton hash保持；完整正式场景表现未验收 |
| 显式目标竿命令拒绝 | `CatFishingCommandComponent::HandleAbilityCommandFromAuthority`先查`FindRodOperatedBy`再验证ControlRodActorId；四人测试辅助猫无操作竿时跳过目标身份门 | 普通助手不再拥有操作竿，显式伪造别人的竿ID仍须在写入held/sequence前拒绝，不能落入无竿分支 | 按命令携带的目标竿先核当前主控/归属/世代，拒绝返回NotFisher并保留原输入状态；不改变R取出本人竿入口 | 目标裁决→持续输入写入→Session操作；单一RPC权威不增加复制写口 | 原主控正常钩/收放、辅助伪造ID/迟到epoch、拒绝后held/sequence不污染；正式4端回执 | 已在held/sequence写入前按显式RodActorId校验当前主控与世代，非主控返回NotFisher；095837 CommandComponent及Report-Four-StanceYield-20260910-103353真实辅助伪造RPC拒绝通过，原主控输入保持。 |
| 静态检查说明 | `Scripts/verify_character_growth_condition.ps1::Invoke-CharacterGrowthConditionStatic`核对GameMode→Service退出调用，错误提示仍称shared session member | 退出方法仍有效，但说明应为释放唯一主控并保留无人值守Session | 仅更正断言说明，保留已有条件和模块原子约束；并运行Character、Equipment、UI现有Static入口 | 源码入口→静态核对→保留运行验收边界 | 不改变检查结果、玩法、资源、网络、资产或Cook默认配置；原地图配置门禁差异仍按既有失败记录 | 已更正为唯一主控/无人值守；20260910 CharacterGrowthCondition、EquipmentShop、UIReach Static均exit0（StaticResults-20260910.json），未改地图配置或放宽检查。 |
| 近处抓握的手臂驱动 | `CatPhysicsGrabComponent::UpdateHand`未抓住时Sweep截到接触点，建约束后Desired改为完整臂展；Four停手阶段主控周期翻倒，辅助仍持续移动 | 近处约束建成后目标突然从接触距离跳至最大臂展，产生未经新增移动输入的持续推挤；目标为抓住前后连续、移动/转向仍能真实施力 | 测量握住前后肩点/接触点/drive目标与反力，按实际抓取距离衔接持续手臂驱动；有关瞬态距离在松手、传送和换目标时重置，不进入Session/费用/存档 | 接触建握→持续drive→松手清理→正式4端停手/拖动 | 保留真实约束、伸爪搜索范围与WASD任意方向作用；近处抓猫/竿不突然推满臂展、静态挂点/双手/松手正常；不放宽停手稳定与网络收敛断言 | 已用实际接触距离衔接HeldReachDistanceCm，并在松手、换目标、传送重置；`Report-Target-20260910-102832` NearGripPreservesContactReachAndClearsItsDomain在60/120Hz通过，同Grip显式接管与清理也通过。`Report-Four-StanceYield-20260910-103353`保留6秒/3cm每秒/5cm/连续0.3秒原门槛并通过真实四端停手、抓人链和移动 |
| 权威竿锚点采样时序 | `Rod::GetRodTipWorldTransform/GetGripWorldTransform`读Scene代理；`PhysicalRod::RefreshObservedPose`在PrePhysics更新；Runner Timer在物理步后读点速度 | 消除getter对Scene代理刷新顺序的依赖；Rod Actor实际也在PostPhysics刷新，UE Timer在其后，不能把正常Timer旧一帧写为已证实根因；权威位置/速度从同一刚体直接读取 | 权威getter直接由`PhysicalRod::GetObservedActorTransform`与规范局部锚点计算，客户端继续读复制代理；保持只读不写竿姿态 | 真实刚体→权威锚点→Runner和鼠标消费者；随后验证Scene/客户端表现 | 固定步之后直接读取位置一致、getter无副作用、60/120Hz正式Runner持续张力与侧拉；原规范锚点单位/资源/存档/资产保持 | 权威锚点/点速度已由同一当前刚体只读取得，客户端保留复制代理。102832真实约束测试在不刷新Scene代理时核对位置、点速度和getter无副作用；正式4case MaximumTipReadErrorCm均0。UE LevelTick证据仍为PostPhysics先于Timer，已移除Timer生产调度，未把正常Timer旧一帧误列为根因。 |
| 铰接竿尖响应 | `PhysicalRod::PopulateEndpointResponse`把肩部Limited/软驱动、抓点线性Locked且Angular Free的全部机械连通体合成一块刚体；Grab.cpp32/Body.cpp136与UE ConstraintInstance::GetRefFrame证明各自真实自由度 | 刚性并质量低估杆/爪局部旋转响应，静态机械连通也不等于竿尖固定；60Hz仍有ReachLimit和峰速越界 | 以各真实BodyInstance的质量/惯量和Locked约束Jacobian计算杆尖响应，保留自由角运动；Limited肩驱动由实际加速度观测反馈，不虚拟焊接；冗余约束采用数值正则化的 Cholesky 求解 | 接收方响应→同一Simulator→真实线力一次提交；不改body/手质量、断握与速度门槛 | 单点静态约束保留切向自由度、Limited连地不强制零响应；60/120Hz真实重鱼与抓地10秒阻挡；无资源/Session成员/资产迁移 | 已替换整体焊接质量，按真实质量/惯量及Locked Jacobian经数值正则化Cholesky求解；Limited肩部不冒充硬锁。102832真实静态握把球铰轴向响应近0而切向大于0.1、跨Limited抓地不强制竿尖零响应，以及重鱼/小鱼实际约束均通过；肩部活动限位近似未采用，原失败由有限站定支撑修复后消除。 |
| 同帧补步与实际线冲量 | `Runner::Start`普通循环Timer；UE TimerManager.h127默认非once-per-frame、cpp1235/1248会追赶；Receiver相同worldtime时把最新发布但未施加的力当上步力 | 同帧多固定步推进鱼及费用，但竿只保留末力；实际加速度与被扣除力不属同时间窗 | 每次固定步发布有限时长力段；PrePhysics按实际物理时长消费并累计已施加/待施加冲量；观测器以已施加冲量除实际时间窗，补步期间冻结该观测；待施加力段参与只读竿端响应预测，不写身体/竿pose | 真实约束响应→力段接收方→Runner发布步长→Observer与Simulator补步预测→0.12s真实Runner | 不丢固定步时间/费用、不覆盖中间冲量、清理/过期按Session域撤销待施加段；normal与hitch两帧率8秒及冲量守恒；无第二费用或虚拟主控 | 已接收有限力段并核对生命周期累计冲量；单独队列仍有Timer相位问题，已由下一行PrePhysics唯一调度补齐；101029正式4case每项160步/160ASC、discard=0、冲量账差约0、未来queue小于0.05秒 |
| 物理帧内唯一搏斗调度（2026-09-10） | `Runner::Start/Stop` Timer在UE `LevelTick.cpp` 的PostPhysics之后执行；`PhysicalRod::TickComponent`已在PrePhysics施力。`Automation-Main-20260910-095837.log`正常60/120Hz竿速278/275cm/s且存活，首个0.12秒帧后queue约0.15秒、发生discard并断握 | Timer产生的补步力错过该物理帧，未消费时间形成持续相位延迟；不能扩大queue寿命掩盖。目标同一物理帧先推进累计0.05秒鱼步，再消费队列，费用仍每固定步一次 | Runner在原竿组件注册唯一PrePhysics帧委托并累加dt；删除Timer调度与专属handle；Receiver当前主控Controller/Grab tick先决关系确保输入与松握先行。Stop/销毁解除委托、清时间余量；原行为树成功启动后才启调度 | 主控输入与Body→Grab→竿Receiver刷新握持→Runner固定步/StateTree/单次费用→本帧竿尖力→PostPhysics实际冲量观测；无人值守继续同一调度 | FormalRunner 60/120正常及7次0.12帧、原步数/费用/抓握/速度/线误差门槛不变；停调度改验证解绑及实际后续Tick无新增步；native重鱼误差45/47cm另须回归；无资源/资产/Cook/复制schema变更 | 已删除Timer运行入口；最终104519四组60/120Hz正常及各7次0.12秒帧均8秒160步/160ASC，竿速278–279cm/s、线误差不超10.115cm、运行discard=0。取消及真实Rod Destroy均解绑；销毁后Session终态、资源释放、下一真实Tick仍160步。 |
| 调度宿主真实销毁验收 | `PhysicalRod::EndPlay`→`PhysicsReceiverUnavailable`→`Runner::HandlePhysicsReceiverUnavailable/Stop`→`Session::HandleFightRunnerFailureFromAuthority`→原终局和Equipment预留释放；正式Runner测试已实际跑满4case | 原Timer可在竿销毁后察觉依赖失败，组件时钟消失时需同步通知；不能仅证明主动Cancel停调度 | 在120Hz+7hitch用例完成原8秒/冲量/费用/速度等断言后，真实`Rod->Destroy()`；断言Session失效终局、Runner解绑、握点释放、预留解除、后续WorldTick无新增步。其余三case继续实际Cancel及旧载荷fence回归 | 已有四case物理成立→最后一case销毁宿主→唯一原终局→真实后续Tick | 不人工设置bRunning、不访问已销毁竿/Receiver、不改前8秒门槛；无配置/资产/Cook或存档变更，破坏性只在隔离Automation World夹具 | 修改前调用链已核，补运行测试待统一构建及执行 |
| 短杆端点加速度预测对照（2026-09-10） | `FightSimulator::Step`把`PopulateEndpointResponse`过去0.05秒竿尖差分加速度外推为未来恒加速度；`Automation-Target-20260910-101029.log`正式4case调度通过，但native60cm杆在3.33/3.39秒峰速1094/1121cm/s、伸长45/47cm，加速度约9000cm/s²，发生于4秒主动移动前 | 差分包含肩部/杆摆动、碰撞及当前几何变化，不能等同未来持续外力；需区分时序修复与预测数值反馈 | 仅源码对照关闭历史差分加速度的未来外推，保留真实位置/速度、已施加力、Locked Jacobian与队列冲量；不加生产开关，不改质量/驱动/鱼力量或验收门槛，依据短集合结果选择并清实验路径 | 记录当前manifest→唯一预测变化→正式4case/native重鱼/小鱼300对0及抓地→比较证据后决定最终预测 | 观察native是否去除过冲，同时正式hitch、稳态拖力及真实地面阻挡必须保持；无资源、Session身份、资产、存档或复制变更 | 101650实验未通过：正式4case与小鱼仍通过，但native峰速恶化至8082/9164cm/s且断握；已恢复原差分预测，无实验开关或旁路残留。101958 manifest与恢复后的Simulator SHA256一致；此对照不构成修复成果 |
| 肩部球面限位诊断排查 | `Body::ConfigureArm`三轴Limited、19.3cm×GeometryScale、硬球面；Chaos`PBDJointSolverGaussSeidel.cpp::ApplySphericalPositionConstraint`只解越界径向行。`Automation-Target-20260910-102006.log` native3.10–3.35s肩手距19.35–20.10cm，超过19.3cm与120Hz有效0.00625cm容差，而`PopulateEndpointResponse`仍只计竿+爪0.47kg | 球面限位是实际物理反力，但更早的2.8–3.0s尚未碰限位、T=0时身体已向旧脚点加速至452cm/s，限位越界发生在人工回冲之后 | 本轮不增加Limited径向近似；保留原Locked响应，把因果更直接的站定锚点屈服作为修复接收方。临时径向代码在构建前全部撤除，没有实验开关或双求解路径残留 | 先还原唯一Solver→有限脚支撑修复→原正式/native/小鱼回归 | 不把XYZ焊接，不用5cm ContactDistance扩大硬限制；质量/力量/门槛不变；无Session/资源/资产/配置写入 | 已完成排查；Simulator hash与101958 manifest一致，PhysicalRod逐段恢复原Locked逻辑（补丁改变部分换行，需用新manifest归档）；未运行的径向方案不算实现或验证成果 |
| 已停用的CMC载荷参数 | `Rod::SetCarrierConstraintFromAuthority`非反射C++方法；Runner仅给旧加速度/速度传0，Camera/SlackAim/RodEffort/Service等测试也只构造战斗观察 | 真实鱼力已由PhysicalRod接收，旧无效参数仍暗示能控制CMC，不能以反射兼容为由保留此纯C++写口 | 改`SetFightConstraintObservationFromAuthority`，只接线方向、归一化张力、误差cm、战斗标志和只读转矩；原反射状态/读取类型按实际资产审计保留身份并标明废弃移动字段 | 新观察写口→Runner→全部C++测试调用→移除旧签名；不增第二写口 | 编译及Camera/SlackAim/时序/权限回归；相同观察数值/世代转换保持，旧加速度参数确无行为，不改任何真实力；无配置/资源/存档迁移 | 已删除纯C++旧SetCarrierConstraintFromAuthority签名，Runner及全部C++测试改用SetFightConstraintObservationFromAuthority，只写战斗观察；不传无效速度/加速度，不重复施力。095837 Camera两项、102832真实Runner/端点及103519联网转杆通过；无UFUNCTION直接调用，反射旧字段/具名组件按独立资产审计行列明保留条件。 |
| 正式跳跃根位移叠加 | `/Game/Animalia/Cat/ABP_Cat`的Main States Jump/Fall Loop/Land→`PhysicsPrototypeVisual::RefreshVisualPose`复制到可见Pose；真实身体已负责起落 | Render首个Land身体Z42.34但源RigRoot本地Z50.98，正式2倍缩放后可见根Z104.29，动画再次把整猫抬离地面 | 保留GetMesh/ABP及Montage源；只在正式跳跃状态及其过渡权重存在时，将最终可见RigRoot竖直平移还原参考值，保留根旋转/水平和四肢姿势；非跳跃与Montage不改 | 身体竖直位移权威→动画姿态→可见根补偿→爪IK | 正式双端Jump/Fall/Land及blend-out的可见根/脚点和截图；源骨骼姿势不写回、正式资产/Skeleton hash保持；无网络/资源/存档变更 | 正式跳跃与blend-out期间仅补偿最终可见根Z，保留源ABP及其51.032cm根位移；`Report-FormalJump-Null-20260910-095916`和`Report-FormalJump-Render-20260910-095959`双方三阶段7/7，可见根误差0，render起跳高度84.779/83.947cm。已查看`20260910-020054-formal-jump-Land.png`及020056恢复站立图；源资产/Skeleton未改，Montage专项未运行 |
| Session域只读查找与批量终止 | `Service::FindActiveSessionByRod`→`CompactSessions`；`TerminateAllSessionsAndReleaseOperators`遍历Sessions→Session终止→Runner.Stop→Rod.Clear；`FinalizeSession::PublishSnapshot`同步广播给ViewBridge C++/BP消费者，可回入`FindSession/TryGetActiveSessionForController`清索引 | 新的力域查询不应删map；批量终局在开放回调期间也不能继续持有map迭代器 | 查找改const并移除内部Compact；批量入口先复制弱Session引用再逐个调用原终局入口，生命周期清理仍保留 | 纯查接收方→力域校验→稳定批量遍历→终态回查压缩回归 | 旧Session不能清新力；两Session广播重入压缩由Service路由测试覆盖，真实Runner停止由FormalRunner用例独立覆盖，不把两项合称“两实际Runner批量退出”；无新资源/复制/资产/配置写入；默认HUD是否同步回查未确认，但公开广播允许该消费者 | 已改为纯查与稳定弱数组遍历；Report-Main-20260910-095837中Service.SessionSurvivesLeaveAndInputRoutesByCurrentRod通过，两个终态同步回查压缩仍各终结一次、清空索引并释放主控；FormalRunner实际Stop与force fence另由101029运行通过 |
| 长时物理回归夹具生命期 | `CatFishingPhysicalRodTests::TickConnectedWorld`遍历Cats裸指针；前阶段Destroy辅助猫后仍留数组，增加静态挂点场景使运行跨过GC周期 | 物理回归在第120Hz的小鱼阶段因已回收辅助猫的IsValid检查崩溃，未完成项不能作为通过 | Destroy后立即移除已退出辅助猫，再推进后续单猫阶段；不改生产生命周期 | 退出夹具→清测试持有列表→重跑完整回归 | 覆盖长时60/120Hz所有小鱼对照；无生产数值/网络/资源/资产变化 | 已将被销毁辅助猫从夹具裸指针列表移除；095837完整集合与102832长时真实World测试均无IndexToObject崩溃，未修改生产GC或对象寿命。 |
| 正式物理子步时间 | `Config/DefaultEngine.ini`未覆盖PhysicsSettings；引擎`PhysicsSettings`默认关闭子步、最大物理dt为1/30秒，`ChaosScene`据此裁剪长帧；Runner Timer仍补足鱼的固定步 | 0.12秒卡顿会让鱼推进约0.1秒而刚体只推进约0.033秒，且60/120Hz关节求解分辨率不同 | 正式`[/Script/Engine.PhysicsSettings]`启用`bSubstepping=True`、`MaxSubstepDeltaTime=1/120秒`、`MaxSubsteps=16`；最多覆盖约0.133秒，Receiver按实际物理dt消费已发布力段 | 配置时间接收方→实际冲量观测/力队列→正式Runner追赶→身体/跳跃/碰撞/四人网络 | 覆盖60/120Hz及每秒0.12秒帧；质量、重力、起跳420cm/s、移动/断握阈值保持；全场动态物体受影响，Game构建/实际配置加载核对，未测整场资产性能和打包行为明确保留 | 已设置bSubstepping=true、MaxSubstepDeltaTime=1/120秒、MaxSubsteps=16；102832正式60/120Hz正常及7次0.12秒帧均完整8秒160步，queue无discard；正式Jump/网络也读取同一配置。 |
| 蓝图操作任务说明 | `Docs/蓝图任务清单.md`任务1第4条R表现与后文试玩步骤仍描述加入他人竿、再次R直接拿起 | 当前原生入口只授予本人主控，恢复地面原竿需先抓回；旧说明会引导制作第二套占位表现 | 更正R与双爪来源、恢复主控操作；不修改文档所指BP事件/资产 | 原生命令事实→表现文案→试玩步骤 | 文本与Command/Service调用核对；不涉及计算、保存、复制或Cook变更 | 已同步蓝图任务清单、配置指南、MVP操作说明与ActorBlueprintHooks，均为本人R取放、鼠标抓回后R接管、旁人持续抓握；对照Command/Service调用核对，保留现有BP事件与资产，无第二输入入口 |
| 同手R接管回归的资源前置 | `CatPhysicalInputRouteTests::RunTest`→`Equipment::GrantEquipmentFromAuthority`为真实抛钩准备BugBait；该入口明确拒绝`bRunConsumable` | 新回归夹具误将消耗品当装备发放，尚未运行即能确定会被InvalidPayload拒绝；正式库存事务保持不变 | 浮标继续装备发放，鱼饵改用现有`GrantInventoryQuantityFromAuthority`数量1；继续验证同GripId鼠标抓回→R接管→松键→真实抛竿/咬钩/收线 | 正式库存前置→原生命令/GAS→真实Session计时器→强清理 | 不弱化原抓握、输入或受力门槛；资源与权威入口不新增；资产/配置/保存不涉及 | 最终104519完整真实输入/资源链Success：同GripId鼠标→R显式持握、旧松键保留、GAS抛钩/飞行/咬钩/提竿/收线、强清理。等待6.267秒、支撑射线命中；1条测试World清理后托管Actor缺上下文警告，非clean。 |
| 蓝图钩子与调参说明 | `Docs/FishingActorBlueprintHooks_zh-CN.md`§1/3.2仍描述GroupModel/CMC共同运动；`Docs/DataAsset字段含义.md`§1.1仍宣称虚拟5kg和共享费用 | 当前生效链为独立Chaos身体/持续约束与唯一主控费用，旧说明会引导资产作者重建已停用行为 | 同步只读竿尖/握点、0或1主控投影、独立抓握及本人调参；具体兼容字段按扩展资产审计标明保留条件 | 原生真实调用证据→资产制作/数值文档；资产本身不重写 | 文本与Body/PhysicalRod/Runner/Balance调用核对；不涉及新输入输出、存档、复制写口或Cook变更 | 当前文档已衔接实际身体/唯一主控/本人调参；清理后100BP独立重载0失败，HUD6/6clean（Report-HUD-AfterCleanup-20260910-101442）；这证明正式控件绑定/文字，不代替多人实际画面与打包验收 |
| 已停用的辅助参数和站位查询 | `Balance::HelperStrengthMultiplier/CatBodyMassKilograms`仅定义与SettingsTests读取；Settings槽位数/间距→`TryGetRodOperatorLayout`→Rod旧站位getter/左右参考组件，测试与Editor MultiplayerAudit读旧getter | 48词扫描1887包与100BP/1536K2节点/3562pin确认上述字段/getter无资产消费者；右/左/中心参考组件在`BP_CatFishingRodActor`与`TestMap`仍序列化，不能硬删 | 删除2个无用Balance字段及专属断言、槽位配置/方法和3个旧站位getter；EditorAudit只核实际竿尖/握把；保留具名组件和装备Stand标定，左右参考保持原±70cm历史姿态，不再提供可编辑多人配置 | 完成只读审计→原生/测试消费者迁移→删除无消费字段/配置→独立构建与正式资产重载 | 辅助抓握不受容量/倍率影响，原竿物理与资源不改；保留组件须经编辑器迁移BP和TestMap后才能删除；Cook/存档无新入口 | 已删除无消费字段/入口并迁移测试；100906 Editor与Settings/Actors十项通过，清理后100BP独立重载0失败（LegacyPhysicalFishingBlueprintConsumersAfterCleanup-20260910-101421.json），5保护资产hash一致；具名组件仍按已确认BP/TestMap消费者保留，删除条件为编辑器迁移并独立重载 |
| 同手接管抛竿视点夹具 | `CatPhysicalInputRouteTests`先Possess再SetPlayer，而相机未像现有CameraTests显式绑定ViewTarget；真实AimLibrary从CameraManager取射线后失败 | 095717构建已证明同GripId接管/松键和消耗品前置通过，失败停在水面求交，尚未证明生产抛竿故障 | 按正式本地角色绑定视点并通过真实相机朝向水域内点；记录ViewTarget、原点、方向及求交结果，继续经过原瞄准合法性/库存/Session门 | 本地控制器视点前置→真实射线与水域求交→GAS正常release抛竿 | 不绕过水域或BeginCast gate，不改正式Aim计算、配置或资产；本地夹具不代替联机抛竿验收 | 已建立明确ViewTarget并保留真实相机射线；101546模块日志确认ViewTarget=Cat、眼高81.122cm、目标900cm，早先失败实因为下行水域高度前置；Report-Target-20260910-101650中完整GAS操作通过（1条退出World上下文警告） |
| 输入回归水域的眼高容差 | `AimLibrary::ResolveCastAimPoint`先FindNearestWaterRegion→`WaterQuery::QueryNearestShoreForPreview`→`Geometry::QueryPoint`，使用BankHeightToleranceCm校验视点高度 | 100906诊断证明实际视点Z81.122cm、方向指向水面900cm且ViewTarget正确；夹具容差50cm先排除了候选水域，并非R抓握或相机计算错误 | 测试烘焙输入改用`Scripts/create_stage_a_maps.py`正式地图生成值250cm，记录nearest与直接ray各自的结果；正式代码/地图和校验规则不变 | 合法水域配置前置→候选选择→直接射线求交→原GAS抛竿链 | 不放宽抓握/费用/物理门槛；继续使用真实水域gate，配置只在测试烘焙输入；无资产、复制或存档写入 | 已用正式250cm配置修正；101546模块的Automation-Target-20260910-101650.log中nearest/directRay均成功且错误None，真实抛竿落点900cm，完整输入链Success；唯一警告在测试World终止后销毁资源托管Actor，不是抛竿/抓握拒绝 |
| 输入长等待的真实岸地支撑 | `CatPhysicalInputRouteTests`原立方体地板X范围[-300,300]cm；真实咬钩调度允许40秒等待，原生1倍猫抓200cm正式地面竿会产生可观察被动位移 | 102757模块随机等待28.4517秒，身体越过后沿-300cm后掉出夹具；提竿时Z=-39681cm使竿尖到水面的最短距离超过线长，权威初始化正确拒绝。101546只等待7.127秒，不能据先前成功认定长等待通过 | 岸地只向后扩至-5000cm、左右扩至±5000cm；前沿300与水域起点400保持；记录真实等待时长/位移/最小高度/支撑，提竿前增加地面射线和同GripId未断断言 | 完整岸地前置→原随机等待与物理步→实际支撑/握持观察→GAS提竿与收线 | 不冻结身体、不压缩随机窗口、不在水下铺地、不放宽原行为；本输入夹具的空载后滑不作为正式尺寸手感稳定证据，正式2倍猫+200cm竿稳定性另有回归 | 岸地扩建后104519用例通过，实际随机等待6.267秒、位移107.027cm、MinimumZ=5.162cm、ShoreSupport=1、原GripId保留；独立Report-OwnerLongWait-20260910-104823-1、104910-2、104958-3均通过，等待18.45–22.4秒、身体X为-407至-438cm，真实跨过原-300cm掉落触发边界且保持支撑/握点/正常提竿收线；没有声称跑满28.45或40秒；只改夹具，空载姿态/手感仍需正式场景验收。 |

---

### 站定支撑受强拖后的屈服范围（2026-09-10，修改前）

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 持竿站定的身体驱动 | `Character/Physics/CatPhysicalBodyComponent::UpdatePhysicalMovement`使用首次`FishingHoldLocation`，`PhysicalRod::UpdatePrimaryMotorBudget`给本人50N预算；native诊断2.8–3.0秒鱼线力为0，身体仍以约1000cm/s²向旧脚点加速，速度255→452cm/s | 已被强拖数百厘米后仍储存原世界站点，松线会自动高速冲回并再次绷线；保留弱力站稳、本人预算、正常方向输入与真实刚体运动 | 沿既有刚度`Force/10cm`，把支撑点的可恢复偏移限制在原10cm弹性范围，超出部分由支撑点跟随身体屈服；不写身体位置或速度，不增加力量/鱼参数 | 本人预算→实际身体位移→有限支撑目标→真实施力→鱼线反馈；输入/倒地/传送已有支撑清理不变 | 60/120Hz弱力站稳、超过预算后真实拖动、撤力在新位置附近停稳且不回冲旧点；native短竿重鱼/小鱼与正式四组长竿、Four联网回归 | 有限10cm可恢复偏移已实现；`Report-Target-20260910-102832` 60/120Hz支撑屈服真实回归通过：外拖228.461/226.003cm，撤力后仅返回新支撑点10cm内、最终速度0；native30kg鱼8秒峰速约244/247cm每秒且保留抓握，正式四case均160步/160次ASC通过。102757模块`Report-Four-StanceYield-20260910-103353`与`Report-SlackAim-StanceYield-20260910-103519`独占联机通过；未改物体位置/速度或费用权威 |

### 本轮检查点的交付证据（2026-09-10）

本次把正式角色切换到共享物理身体和持续抓握；只有原竿拥有者通过 R 显式取得主控并进入 Session，鼠标执行原抛钩/提竿/收放线操作。其他玩家按住左右鼠标抓竿、猫或场景，再通过普通移动传力，无会话成员、辅助倍率、共享费用或自动接任。R 放竿不拆别人的连接。正式起跳仍为420cm/s、重力倍率1；倒地/脚点、存档恢复与镜头读取同一身体。原型继续保留为试验入口。

`contract`：共享工作区 Editor `BuildEditor-Main-20260910-104402.log`、Game `BuildGame-Main-20260910-104601.log` Win64 Development 均成功。`SourceManifest-20260910-104402.json` 与 `ModuleHashes-20260910-104402.json`冻结实际源码和两个Editor模块；相较已完成Four/Slack的102757快照，只更改Owner输入岸地及FormalRunner销毁两个测试文件，生产源码行为一致；提交前另清两处空行的行尾Tab，token序列逐文件完全相同，前后hash见CheckpointWhitespaceCleanup.json，未因此重复编译/行为测试。CharacterGrowthCondition、EquipmentShop、UIReach三个Static均exit0，两份修改的Python脚本语法通过。原FishingPlayerEntry静态脚本仍要求Lake而当前配置为Showcase2，未改用户地图配置，不把该入口记为通过。

`runtime_behavior`：证据根目录 `Saved/Automation/PhysicalGrabIntegration-20260909/`，最终 `Report-Main-20260910-104519/index.json` 共183项：176 clean、6带警告通过、1既有失败、0未运行；唯一失败仍是StarterRod测试150与正式资产500。包含真实碰撞/抓握/释放/传送/存档、本人ASC账、资源归属、旧交换拒绝、单主控退出、鼠标/GAS及持续受力。正式竿四组60/120Hz与每秒一次0.12秒卡顿均完成8秒160步和160次ASC写入，线误差不超过10.115cm，竿峰速不超过279.252cm/s，运行冲量账差和discard为0。最后一组实际Destroy竿后Session Invalidated、双委托解绑、握点与资源锁释放，下一真实WorldTick仍160步。native短竿、零体力小鱼和有限站定支撑同时通过原门槛。带警告项目保留水面查询、ABP初始化、取消通知和测试World清理诊断，没有把它们写成clean。

真实联机为 `Report-Four-StanceYield-20260910-103353` 和 `Report-SlackAim-StanceYield-20260910-103519`：四端辅助传力但ASC不记钓鱼账，原主控R退出后操作人1→0、epoch1→2、辅助握点保留；伪造主控请求被拒。双端鼠标停止/丢包超时/新stroke恢复及镜头连续性通过。正式客户端抓猫与失焦使用100725报告；正式Jump/Fall/Land及可见根补偿使用095916/095959报告，双猫跳升约85cm，源ABP根Z约51cm未被改资产，可见根重复偏移为0。网络证据使用PIE真实NetDriver与RPC，Four夹具关闭PC tick以隔离身体/抓握协议；Slack保留真实PC/UI输入，不能把Four描述为四端完整EnhancedInput试玩。

`presentation_delivery`：正式HUD重载/实例6项通过；已查看正式模型抓爪、落地和跳跃截图，`Saved/Automation/PhysicalGrab/Images/20260910-020056-formal-jump-grounded-locomotion.png`确认双猫四脚落地。尚未运行本轮Cook/打包、真人四端整场操作、Montage/隐藏挂件专项、严重延迟下的手感或Development新包无`-log`双端默认落盘；不关闭Character/Fishing模块。身体目前是主刚体与两只物理手，客户端为权威快照插值；完整多关节主动布娃娃、预测/回滚及最终动画未实现。并行脚步IK与旧失败预算删除另有任务负责，未混入此源码快照或检查点。

清理结果：无消费者的GroupModel、共享分账/自动接任路径、组队形运动、旧辅助HUD生成器、辅助倍率/多槽配置和旧站位getter已删除。保留的具名Stand/LeftStand/RightStand组件在`/Game/Blueprint/Actors/BP_CatFishingRodActor`与`/Game/Catfishing/Maps/TestMap`仍有序列化消费者；OperatorPlayerStates/OperatorMemberships在Rod BP的Break节点有未连接但仍序列化的引脚，运行值只为0或1。DA_FishingFightBalance_Default仍保存AccelerationPerStrength、DriveResponseSeconds、MinimumCarrierAwaySpeedMultiplier身份，它们不参加物理求解；删除条件为对应BP/地图/DA编辑器迁移并独立重载。审计100BP、1536节点、3562引脚0加载失败，报告`LegacyPhysicalFishingBlueprintConsumersAfterCleanup-20260910-101421.json`。正式HUD SHA256为33B2E74D4D7900B04ADBF8BE28DA8DD2C9E0B5FFB4064951DC3BEFD9284A2549；用户Cat_Skeleton SHA256仍为3AF77F901B4FD35EAF79A7133230BB8964A1F2E886064010F680A40C3DE18008，原样保留且不提交。

日志使用`LogCatPhysicsGrab/physics_grip_*、physics_body_hold_anchor_yielded`及`LogCatFishing/fishing_primary_*、fishing_fight_scheduler_*、fishing_physical_line_load、fishing_fight_runner_stopped`，以BodyId/GripId/RodActorId/SessionId/ControlEpoch跨World/NetMode关联；本轮实际文件为上述证据目录中的`Automation-Main-20260910-104519.log`、Four及Slack各自日志。它们是Editor/PIE落盘证据，不替代新包房主与客户端两份默认日志。

## 1. 一张图看全貌

```
【输入层】玩家按键
   R / E / 左键 / 右键 / Q / F / X
        │  Enhanced Input（IMC_InputContext）
        ▼
【输入语义层】DA_CatAbilityInputConfig
   ├─ Native：E → IA_Interact → Cat.Input.Interact → Current Target → 服务器交互 RPC
   ├─ 空手/辅助：左键/右键 → 左爪/右爪持续抓握 → 服务器约束
   └─ Ability：R/Q/F/X，以及主位左键/右键 → Fishing InputTag → ASC
        ▼
【GAS 层】6 个原生 GameplayAbility（可被蓝图子类化，仅承载"输入边沿 + 本地表现钩子"）
   UCatGA_FishingRodInteract / PrimaryAction / Slack / Chum / Scoop / Cancel
        │  Submit*()：把按下/松开变成一条带 RequestId 的命令
        ▼
【命令层】UCatFishingCommandComponent（挂在 PlayerController 上，唯一 RPC 边界）
   HandleAbilityCommandFromAuthority()：服务器按"当前事实"分派语义
   ├─ R   → 已主控=释放本人连接 / 已抓住本人的竿=恢复主控 / 否则=PlaceRod（取出自己的竿）
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

### 2.0 单主控钓鱼与多人抓握

本节描述当前正在接入的目标，验证状态见页首审查表。主控通过 R 取出自己的鱼竿并建立实际持握，使用鼠标抛竿、提竿、收放线和转向。R 放下只释放主控自己的连接；旁人仍抓着竿或主控时，相关真实约束继续存在。普通抓握不会授予主控身份，也不会转移 `OwnerPlayerState`、原竿实例或收纳权限。

只有主控进入 Session。旁人按住左右键伸出对应爪，可以抓竿、猫或场景接触点，再用普通身体移动沿任意方向施力。抓两根竿也只建立物理连接，不把两场会话合并；双手、环路、人数都不产生会话成员。`OperatorPlayerState` 表示唯一主控，历史 `OperatorPlayerStates` 如因二进制消费者暂留，只允许0或1项，不能成为另一套成员入口。

`PlaceRod` 的地面坡度和部署额度保留：前方150cm可站立实体地面，法线Z≥0.7，每人最多两根部署实例，同时只主控一根。取出无需靠近水域；`BeginCast` 仍校验准星水域、射程、前向夹角、视线和原装备资源。收纳仍按具体 `RodActorId` 及物品归属校验；抓到别人的竿不能抛钩或收进自己的背包。

`UCatPhysicalBodyComponent` 统一拥有真实身体、两只爪、站立支撑和移动，正式 `ACatCharacter` 保留GAS、Condition、Inventory、Equipment及动画宿主。正式CMC只提供BP运动配置与ABP观察接口，停止积分位移与旧SavedMove；默认起跳420cm/s、重力倍率1、行走100cm/s，冲刺读原Controller配置。倒地由Condition停用自主驱动，仍允许别人抓住身体拖救；传送统一清理双向约束并重置三刚体。

`UCatFishingPhysicalRodComponent` 的刚体拥有竿姿态，握把、竿尖、视觉与镜头读取实际变换。主控的鼠标力矩只由本人ASC限制，反力只回本人身体；旁人不向控制器贡献虚拟力量，而是靠真实身体与约束对竿产生作用。主控移动和站定支撑共用自身钓鱼力量预算；旁人保持普通身体运动规则，不因抓上竿而切换到钓鱼费用或辅助系数。

鱼端保留0.05秒固定步与水面求解。Simulator读取真实竿尖位置和速度，最终鱼线力在竿尖施加一次（N转换为kg·cm/s²乘100），再经约束传到各个身体；CMC、组均值或旧杆预测都不再写身体与竿的位置。耦合稳定性必须由实际持续鱼力与Chaos联合运行验证，不能用静态力接收或编译成功替代。

Runner只冻结主控ASC、输入及身体运动样本，并在最终求解后提交本人一次账单。收线、转杆与去重支撑按原单主体规则结算，个人移动样本按厘米和秒消费，不把传送或被动位移算主动进展。有效放线恢复主控体力、满线不恢复、鱼力竭免正向费用保持。辅助玩家的ASC不注册到Session，不均分费用、不合并体力、不借余额；HUD只显示本人已有属性。

主控归零仍保留控制权但没有主动力量。强制外冲只由主控零体力、持握和鱼状态决定；其他玩家可以通过真实约束抵抗或拖救，不以会话身份开关鱼的行为。主控恢复正体力、离竿或鱼力竭时退出强制外冲。主控危险落水或退出时进入既有无人值守处理，旁人不会自动接任。

主控取得与释放仍推进控制世代，隔离迟到输入；辅助抓握不修改该世代、设备版本或Session身份。当前角色网络由服务器模拟与快照复制，客户端插值不等于完整预测回滚。打开菜单/失焦取消未提交瞄准并清持续输入，不误抛竿。

一轮结束仅清本场载荷与结算，健康竿上的持续抓握保留。破竿、收纳、竿销毁才清理其相关约束。无人值守仍属于原Session的既有规则；旧Session后置EndPlay不能清掉新Session的载荷。Equipment、原实例磨损、精确预留和终局裁决继续使用单一权威入口，不因为旁人参与物理接触而换资源宿主或复制物品。存档schema不变，不保存临时抓握关系。

历史反射字段和兼容资源入口须以源码、配置及必要资产引用审计决定删除；未确认的二进制消费者保留原因与删除条件在页首记录。以下日期章节仅保留旧实现的审查证据，不能据其中的四人成员、共同账单或接力规则新建运行路径。

#### 借竿抛钩修复影响核对（2026-09-08，历史检查点）

以下保留借竿修复当时的验证证据；其中“资源宿主退出即释放整场”已被后续四人接力与资源托管替代，当前规则以上文和 2.0.2 为准。

修改前基线为 `01b8b75`，工作区只有用户未跟踪的 `Scripts/Art/`、`SourceArt/`，本轮保留。原借竿审计中本人竿对照成功、借竿预留失败；证据在 `Saved/Automation/BorrowedRod-20260908/BaselineReport/index.json`。下表只覆盖本次资源归属修复，不关闭 Fishing 或 Equipment / Shop 模块。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 输入与权威抛钩 | `Fishing/Integration/CatFishingCommandComponent.cpp::BeginCastFromViewOnAuthority/ServerSubmitBeginCast` → `Fishing/CatFishingService.cpp::BeginCast` | 输入已按正在操作的竿路由，旧预留仍只查操作者库存；借竿返回依赖失败 | 保留输入/RPC，在 Service 用部署 Instigator 冻结真实竿宿主，重查占有关系和既有 gate | 先准备跨宿主预留，再切换 Service；保持 RequestId 缓存和失败回执 | 实际 Place/Leave/Operate/Begin、Hook 飞行落水、预留后失败与广播中 UnPossess | 已衔接；Service 的 5 个正式资源场景通过，见下方 FinalReport |
| 装备事实与资源写入 | `Equipment/CatEquipmentComponent.h/.cpp::BeginFishingUse/ApplyFishingRodWear/ReleaseFishingUse`；Session 调用这些入口 | 原竿/饵/漂都在同组件；目标为竿归原主，饵/漂归抛钩者 | 协调记录冻结 RodEquipment；原主 UseRecord 保存会话锁。磨损仍以累计值差额写准确实例，不改耐久单位、饵数量与消费时机 | 双方预检 → 静默扣饵/锁竿/增版 → 通知；先闭合记录再退款通知，禁止重复扣退 | 双版本冲突、重放、换选择、双方回调重入、同主双竿回归 | 已替换本地竿查找；6 项 BorrowedRod 装备回归通过，见下方 FinalReport |
| 通用库存转移与收竿 | `CatEquipmentComponent::ReadInventoryTransferEndpoint` → `Equipment/Inventory/CatInventoryTransferService.cpp`；Service Pack → UnUse | 旧 ActiveUse 锁只扫描本组件协调记录，不能识别借出的竿 | 改读原竿 UseRecord 唯一锁；转移事务与 OwnerPlayerState 收纳权限保留 | 先建立锁，再开放借竿 Begin；Release 后才允许转移 | 活动竿转移拒绝，取消后释放，原有库存转移与满包收竿回归 | 已移除旧本地记录扫描路径；InventoryTransfer 与 BrokenRodPack 回归通过；未新增拾取/扔出玩家入口 |
| 退出与接力 | `Character/CatCharacter.cpp::UnPossessed/EndPlay` → Service Terminate → `CatFishingSession::InvolvesCharacter`；Equipment DestroyComponent/EndPlay/OnComponentDestroyed | 接力后仅查现任参与者会漏掉原抛钩者与竿主；未 BeginPlay 销毁不进 EndPlay | 关联两个冻结资源宿主；组件在基类 DestroyComponent 前完成一次性清理并防止通知重入销毁，EndPlay/OnComponentDestroyed 复用同一 Release | Character 通知先于组件销毁；预留通知时 Session 未注册，Service 另做重入校验 | 接力后两个资源宿主分别退出、预留中 UnPossess、未 BeginPlay 销毁协调者 | 已衔接正常角色退出和组件协调记录释放；直接销毁竿主组件只验证后续 Release 可退款，未宣称立即终止 Session |
| UI、复制与资产 | `UI/CatFishingViewBridge.cpp` 消费 Session/当前操作竿；正式 `CatFishStateTreeAuthoringLibrary` 不接额外失败任务 | EquipmentRevision 保持抛钩者库存语义；额外失败惩罚未接正式流程 | 保留正式 Snapshot/回执与 RodEquipmentRevision；2026-09-10 审计后移除旧失败预算接口和任务 | 正式 Rod/Hook BP 与 Session 树继续加载；无资产迁移、存档或 Cook 修改 | `audit_fishing_failure_removal.py` 扫描项目/插件包并加载全部项目 StateTree；双端表现另行验收 | 旧接口保留条件已由资产审计解除；删除结果见本页 2026-09-10 清理说明 |
| 诊断、测试、文档 | `LogCatEquipment/equipment_rod_session_*`、`LogCatFishing/begin_cast_*`；Equipment/Fishing 新 BorrowedRod 测试、Editor MultiplayerAudit；本页和 Blueprint 指南 | 旧日志无法分辨两份装备归属，旧审计暴露失败 | 增加 SessionId、原竿实例/宿主和双方版本；迁移原借竿审计、保留自有竿对照；旧指南缺口口径改正 | 静态与构建后跑正式资源调用链；进度只记 `Docs/Development/需求对齐差距清单.md` | contract / runtime_behavior / presentation_delivery 分层记录 | 最终 54 项通过；默认日志已在 FinalTests.log 落盘；指南与唯一进度文档已更正 |

本轮验证：`contract` 为 EquipmentShop Static PASS 和 Editor/Game Win64 Development 构建成功；`runtime_behavior` 为 54/54 Success（53 clean、1 条既有无人接管 RunnerTransition=false 警告、0 failed/notRun）。证据根目录 `Saved/Automation/BorrowedRod-20260908/`：`FinalReport/index.json`、`FinalTests.log`、`BuildEditorDelivery.log`、`BuildGameDelivery.log`、`StaticDelivery.log`。6 项新装备回归、Service 的 5 个正式资源场景、原借竿审计及原双竿/转移/收竿回归均通过；`equipment_fishing_shutdown_completed` 实际记录未 BeginPlay 销毁后 UnreleasedSessions=0。首轮仅依赖销毁回调的兜底未通过新增测试，具体回调跳过点未确认；最终改在 DestroyComponent 的 Super 前释放并验证重入安全，未删除或放宽失败断言。

构建/运行位于 `Saved/Validation/BorrowedRod-20260908`，11 个修改源文件与共享工作区 SHA256 相同（`SourceManifest.json`）。这保留了当前开启 Live Coding 的用户编辑器现场；当前编辑器尚未加载此修复。`presentation_delivery` 未运行 Cook/打包、正式 WBP/真人双端借竿或无 `-log` 双端默认日志验收，仍需保存退出编辑器后构建共享项目并实测；单世界正式 BP 飞行不替代双端表现证据。

### 2.0.2 四人移动合力的影响与验证（2026-09-08，历史检查点）

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

### 2.0.4 入竿即保持队形的影响与验证（2026-09-08，历史检查点）

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
| 配置、生成脚本与Cook | `Config/DefaultGame.ini[/Script/Catfishing.CatFishingSettings]` 的4槽/FightBalance/Session树；`Scripts/create_fishing_session_state_tree.py`、`create_fishing_fight_balance_asset.py`、`configure_formal_rod_anchor_baseline.py`、`migrate_physical_grab_hud.py`（原名cooperative，仅供历史定位） | 继续使用同正式系数、标定与树；140cm仍仅兼容站位查询 | 不涉及资产迁移、配置调参、脚本改写、新资源或新增Cook目录；本轮仅原生运行字段扩展 | 现有软引用/生成路径不切换；构建验证结构加载 | 正式树/平衡/角色/竿资产加载；未Cook不得记打包通过 | 不涉及配置/脚本/资产/Cook入口写入；共享Editor/Game Development构建通过，正式树/竿/角色/HUD运行加载通过；初级竿150/500仍是既有测试失败，Cook/打包未运行 |
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

搏斗中的实际杆姿态独立于控制器施力意图，由 `FCatFishingRodResistanceModel::StepRotation` 按有阻尼的净转矩积分。鼠标活动时，猫朝请求方向施加不超过当前力量的转矩，接近目标时连续减小；鱼端最大转矩由 `最终共同线张力 N / ForcePerStrengthNewtons × 玩法杆长 m` 换算为既有 StrengthMeters 单位，再沿最终牵引方向滤波，与杆方向叉乘形成有向回复转矩。净转矩改变角加速度，既有角速度经阻尼衰减；鼠标启停、改变鱼力、猫力或线方向后每帧重新求解，不存在硬角度锁或解锁状态，也不使用原来的全方向零速倍率。保持原有身体俯仰范围与最大角速度；响应时间、等效惯性、受载阻尼及不超过 1/240 秒的亚步共同决定动态响应。服务器复制实际 Actor 姿态，Development 日志 `fishing_rod_rotation_resistance_sample` 记录请求/实际朝向、本帧转角、净转矩、负载历史和仅用于观察的 `TorqueBalanced`；`fishing_constraint_sample` 记录共同张力、阶段、游向与固定步时序。详细采样开关及字段见 [实现导读](FishFightImplementationGuide_zh-CN.md) 的“平静/反抗运动日志衔接核对”。
`FCatFishSteeringModel` 用独立服务器随机流产生平滑目标游向；相同种子与固定步长得到相同方向序列，客户端不自行随机。

搏斗中 `UpdateRotation → CommandComponent` 按当前鼠标增量决定是否主动转杆。鼠标停止即关闭主动转矩并丢弃本段未完成目标；新移动段从实际握把重新开始，不再存在首次右键前追赶ControlRotation的分支。启停可靠发送并与30 Hz持续快照共用有序输入域；150 ms无有效新输入时权威撤力，停止心跳不能补回旧鼠标目标。右键首次按下仍由 `CommandComponent → Session` 完整验证后重设目标，迟到按键不得激活旧活动段；释放、重复通知和回执不恢复旧目标。角速度、鱼负载、已发生努力、实际姿态和镜头平滑保留，身体支撑与左键收线独立。输入顺序、限位及验证边界见 [实现导读](FishFightImplementationGuide_zh-CN.md) 的“鼠标停止即撤掉主动转杆”。

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

### 显式 R 持竿与持续鼠标抓握的来源交接（2026-09-09）

修改前基线：首次 R 从库存取竿可使用空闲手，已验证鼠标抓人另一手不会释放持竿手；但“鼠标抓回本人地上鱼竿 → R → 松鼠标”仍把同一约束当普通抓握释放。当前用户 `Cat_Skeleton.uasset` 并行修改保持原样。本单元修改前回归未运行，不以首次取竿用例替代抓回用例。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 抓握来源及权威状态 | `Interaction/Grab/CatPhysicsGrabComponent.{h,cpp}` 的 `ApplyGrabInput`、`FCatPhysicsGripState`；Ability 输入组件向它发送左右手按下/松开 | 现有松开无条件拆约束。目标在服务器把已存在且目标匹配的真实约束交给显式 R 持握，保持 `GripId`、接触点、力学参数；普通辅助仍必须持续按住 | 新增仅服务器 `RetainGripFromAuthority(bool,UPrimitiveComponent*)` 和复制 `bExplicitHold`；松鼠标仅结束持续输入，不拆显式持握 | 先实现接收方，再切 R 成功事务；客户端无授予显式持握 RPC | `contract` 验证目标匹配与无客户端授权口；`runtime_behavior` 验证同 ID、松鼠标承重、辅助松手立即断开 | 已增加服务器目标匹配的来源交接，同GripId仅增加revision并复制bExplicitHold；101650真实GAS链和103353四端普通松键不拆R持握、辅助松键即断通过；最终104519及三次OwnerLongWait均通过，同GripId和强清理契约成立。 |
| R 取回交易与失败回滚 | `CatFishingService::OperateRod`/放置入口调用 `CatFishingPhysicalRodComponent::BeginPrimaryHold`，随后写本人主控、恢复 Session；`ReleasePrimaryHold` 被 R 放下及退出调用 | 已握时 Begin 直接成功，原鼠标来源未交接；若过早增加显式持握，后续 Resume 失败会留下锁存 | Begin 仅保证真实物理接触；仅最终交易提交后把同一接触转为显式持握。失败保留原鼠标来源；R 放下仍显式拆本人约束 | 抓握 API 就绪后接 Service 最终成功点；不重建 Grip，不改变 immutable owner 或单主控 Session | 实际抓回 → R → 松鼠标 → 抛钩/收线；拒绝/恢复失败不能变成持久持握；重复 R 退出真实松竿 | 已将CommitPrimaryHold置于Place/Operate成功尾部；Resume拒绝不调用Retain，最终Commit失败撤销本次新主控并刷新Session。101650同GripId抓回→R→松键→GAS抛钩/收线通过，103353四端普通松键保留、R显式放下通过；102832输入夹具掉出小地板已修正，104519及三次OwnerLongWait均通过；后者实际越过原-300cm边界而仍有地面支撑。拒绝/最终提交失败分支已核调用链，未逐分支故障注入实跑；不新增成员、辅助费用或自动接任 |
| 按下接收方及表现 | `CatAbilityInputBindingComponent::PressedRoutes` 保留按下时 Grab/ASC 接收方；`CatHUDModel` 从本地身体与竿主控读左右手提示 | 原接收方正确收到迟到松键，但抓握层不认识显式持握；目标继续正常发送该松键，由权威来源决定是否拆约束 | 保留路由；正式 HUD 依据真实持握状态显示“持竿（R 放竿）”，持续辅助仍显示“松键释放” | 不新增键位、IMC、AbilitySet 或临时 HUD；接收方先准备后验证现有消费者 | 真实 GAS 激活、正常释放抛钩/收线及菜单取消不提交；左右手都覆盖；画面层本单元尚未验证 | 按下路由与迟到释放保留，HUD按真实来源显示持竿或松键释放；HUD-AfterCleanup-101442六项clean通过，四端普通松键/R放竿路径通过。新版R来源文案的独立Slate截图未运行，不以控件契约代替画面验收。 |
| 生命周期、网络与清理 | Grab `ReleaseHand/All/Target`、Body `ClearControlIntent`/超时、目标销毁及约束断裂调用链 | 显式持握只改变普通松鼠标；失焦、菜单、倒地、传送、超时、销毁仍必须真实断开并收爪 | 所有强制释放清 `bExplicitHold`，不自动重抓；复制来源标志及 revision，记录授权交接与失败原因 | 与 R 事务成功点同时验收，不靠吞一次客户端 release 掩盖跨 Actor RPC 顺序 | `runtime_behavior` 验证真约束释放与状态复制；迟到松键不能清已提交显式握持，强制清理不遗留来源 | 所有强释放统一清来源，无自动重抓；100725双端Flush、095837传送/生命周期、103353四端主控R退出及辅助保留通过，BodyId/GripId/Revision日志可串联授权与释放；新包双端默认落盘未验收。 |
| 四端主控放竿验证入口 | `CatFishingGroupNetworkTests.cpp::FVerify` Stage5 原调用左右 `SetGrabInput(false)`；正式 `CatFishingCommandComponent::SubmitRodInteract` R 持竿分支调用 `Service::LeaveRod` | 普通松鼠标应保留已提交的显式握持；旧夹具把松鼠标视为 R 放竿，会在 Stage6 等待不存在的退出 | Stage5 先核普通松键保持主控和原 GripId，再走正式 `SubmitRodInteract` 及关联回执，由 R 路由构造最新 revision 并调用 `LeaveRod`；继续核本人约束释放、辅助约束和 Session 无自动接任 | 来源交接已编译→更新测试消费方→统一重构建→四端独占复验 | 原6秒/3cm每秒/5cm/连续0.3秒收敛门及实际 RPC、费用、归属断言不变；仅测试入口，配置/资产/Cook/存档不涉及 | `Report-Four-RRoute-20260910-101209` 全链路通过；0 错、32 既有 ABP 初始化警告，原收敛门保持；R 回执后四端主控 1→0、epoch 1→2，辅助握点仍在且无自动接任。首轮 100121 的 Stage6 超时保留为旧测试输入不匹配证据 |
| 配置、资源、资产、构建与测试 | `AbilitySystem/Tests/CatPhysicalInputRouteTests.cpp`、Grab 实物测试、正式网络测试及本页；现有 `WBP_CatHUD` 消费只读 View | 无运动单位/默认值或持久化装备账变化；Grip 来源是临时复制状态，不写存档 | 扩现有真实 World/GAS 回归并保留原首次 R/辅助/清理用例；配置、Cook、保存资源和资产生成脚本不涉及 | 统一新 DLL 编译后执行专项；不写用户 Skeleton，不启动或关闭用户 PIE | 分层记录编译/contract、真实物理/GAS/network、正式 HUD 画面；未验证层明确保留 | 抓握来源不写存档/装备账，原资产无新增字段绑定；最终Editor104402/Game104601成功，104519 Owner/RodDestroy和整组回归通过（仅保留初级竿耐久既有失败）；103353/103519联机通过，正式HUD6项clean，打包/真人画面未验收。 |

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

当前数值由正式角色ASC、装备实例和鱼定义读取，不从旧示例快照回填。每根竿只有0或1名主控，物理帮助者不占会话位置。身体质量4kg、竿体0.35kg；正式猫几何按现有2倍模型尺寸匹配，起跳仍为420cm/s。`DefaultEngine.ini`的物理子步按1/120秒、最多16步运行；不按站位间距摆放角色。
开发便利开关：整套 `bAutoConfigureStarterLoadout=False`；独立临时测试开关 `bAutoGrantStarterScoopNet=True` 只为新玩家角色补齐一把抄网并选中，商店获取接通后删除这条路径。

## 6. 已知待办（都在契约后面，不影响表现层）

- 咬钩公式改版：读取所在面积单元的聚鱼总量、浮漂级计时器、总量变化时比例折算；正式总量→等待时长曲线待裁
- 窝料改版：水域面积/鱼总量/鱼种库存账本、鱼种平均分布、互斥面积单元、共享重叠收敛曲线、守恒重分配与面积容量上限
- 抄网规格版：概率/硬直/无网拾取/翻肚 30s 苏醒（会新增 Phase/Intent 枚举值→表现层届时"补分支"）
- 浮漂精准偏移、入夜停咬、拽尾巴救援(W3)、巨鱼协作表现输入
- 多人按本页2026-09-09正式物理接入方案执行：只有主控的操作与本人费用进入Session，旁人通过真实抓握传力。低体力强制换人、自动接任、共享账单和辅助系数都不属于当前规则。正式HUD显示本人属性和手部状态；受力、网络和交付边界按顶部审查表记录。旧StateTree交换节点不执行费用，不能作为第二条调参或结算入口。
