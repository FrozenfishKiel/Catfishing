# 物理抓握原型使用说明

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

## 运行边界

- 身体是一个受控刚体，两只前爪各是球刚体。肩驱动和接触约束传递双方反作用力；脚点支撑与有限姿态驱动辅助站稳，近地侧躺/倒扣可通过物理转矩恢复站立。它还不是完整多关节主动布娃娃，没有正式多阶段起身动画或爬回平台动作。
- 当前模型和动画只作为表现源，起跳、空中与落地使用已有 JumpX 片段，程序 CCD 在身体动画和过渡混合之后让前爪追随物理手，保留骨段长度。原模型、ABP 和原 Physics Asset 不需要保存修改。
- 服务器唯一模拟身体、手爪、道具与抓握。客户端提交输入并插值权威快照，没有实现完整物理预测和回滚。延迟手感需要单独评估。
- 抓握快照包含独立 GripId、版本、目标 Actor / Component、骨骼和目标局部接触点。松手、失焦、切换控制、目标销毁、断线和复位都走明确的解除入口。
- 原型的抓握关系没有投影到正式鱼竿名册；旧共同速度、固定队形、鱼线张力和账单尚未迁移。试验通过不能据此关闭 Fishing 或角色相关模块。

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

## 本轮影响与交接

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
