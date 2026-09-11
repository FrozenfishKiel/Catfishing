# 角色换装与公共动画模板

## 2026-09-11 猫之间推拉的一次性受力表现（试用）

`ACatCharacter` 创建 `UCatForceReactionComponent`（`Character/Animation/`），原猫与 CuteCat 的角色子蓝图启用它。服务器在 `TG_PostPhysics`、身体移动 Tick 之后采样本帧已提交的猫之间抓握/身体推动力；不会在 `ApplyTraction` 每帧先清再写的中间状态触发。抓杆、鱼线、重力、纯竖直抓跳不触发水平受力 Montage。玩法移动、抓握、体力账本、Condition 权威均不改变。

一次受力的规则：水平载荷达到 **5 N** 时锁定本次方向并多播一次；持续受力、方向反转、Montage 播完都不重新触发。载荷降到 **2 N** 以下（含等于）并连续 **0.2 秒** 后才能再次触发。值在角色 `ForceReaction` 组件 Defaults 中配置；底层力仍为 kg·cm/s²，采样只除以 100 转成 N，不修改原力。载荷取「水平合力大小、最大单来源水平力」的较大值，因此两侧拉力抵消不会被视作卸载；方向优先取水平合力，几乎完全抵消时取最大来源方向。

方向描述身体向哪里反应，不描述攻击者在哪边。以身体前向和实际受力做点乘/叉乘，选择 `Forward / Backward / Left / Right`；对方在正前方拉猫时选 Forward，推猫时选 Backward。抓握已经给双方相反的力，表现层不得再给“拉”额外取反。每个角色当前的一次连续载荷只允许一次事件，多手或多人新增来源不会绕过门控。

原始 Animalia 受击素材是头/胸/骨盆 × 左右 × 轻重，并非现成的纯四向动画。此次试用片段使用 `Hit_ChestL_Heavy-IP` 的冲击/回弹时间曲线驱动定向骨盆偏移和倾斜，保留首帧站姿、子骨骼长度及根骨位置；随后通过现有 `RTG_AnimaliaToCuteCat` 重定向并修正 CuteCat 的根骨/骨盆单位。它们是**派生的四向试用动作**，不应描述为四段原本就有的成品动画。原猫全部旧动画与断线受击 Montage 保留，其既有消费者仍有效。

正式资产为 `/Game/Animalia/Cat/Animations/ForceReaction/AS_Force{Forward,Backward,Left,Right}`、同目录 `AM_Force*`，以及 `/Game/Characters/CuteCat/Animation/Retargeted/` 下对应的八个资产。Montage 使用公共模板的单一 `DefaultSlot`，单段、播放一次、无 Root Motion，混入 0.08 秒、混出 0.15 秒。CuteCat 用现有 `PhysicalVisual.AnimationOverrides` 映射原动作身份，播放仍走 `ACatCharacter::PlayAnimMontage`，四足和抓握 IK 保持原消费者。

服务器处于不可行动/倒地表现时消费并抑制本次起始事件；各接收端若仍在倒地/起身或已有 Montage，则跳过本次播放，不排队补播、不在持续受力时重试。这样不会打断抛竿、救援、断线或上一次尚未结束的反应。可靠多播携带事件序号，客户端只接收事件，不独立检测力。组件销毁只停止自己记录的 Montage；握点释放、目标销毁仍沿原力源清理路径卸载。

生成入口：构建 Editor 后，在空闲 UE Python commandlet 中运行 `Scripts/Art/create_force_reaction_assets.py`。默认保留已有动作；确需重生成本功能的四段源片段和四段目标片段时设置 `CAT_FORCE_REACTION_REBUILD=1`，不要用该选项覆盖用户手工调整过的动作。脚本不会重建公共 ABP、旧重定向器或其他角色资产。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 入口与源 | `Interaction/Grab/CatPhysicsGrabComponent::ApplyTraction`、`Character/CatCharacterMovementComponent::UpdatePeerPushContacts` → `CatPhysicalBodyComponent::SetExternalForceFromAuthority` | 原先只传力，无一次性动画；保留全部原力公式/单位 | 新 `bCharacterInteraction` 标记，身体推动沿原 `bBodyContact` 纳入；新只读采样返回 N 和方向 | 原力先写完，再采样 | 实际抓猫双向传力、释放清理、无关力与竖直力过滤 | 已接入；`CharacterForceSourcesAndCleanup` 验证真实 `GripFromAuthority`→`ApplyTraction` |
| 门控与方向 | 新 `Character/Animation/CatForceReactionComponent::{TickComponent,FCatForceReactionGate}` ← Body PostMovement Tick prerequisite | 连续检测只产生起始事件；无新玩法状态 | 5/2 N、0.2秒迟滞门控；身体坐标下点乘/叉乘 | 固定帧提交之后，恢复行动不绕过已消费载荷 | 30/60/120 Hz、短断帧、持续/反向受力、重新卸力 | 已接入；`OnceUntilStableUnload` 覆盖；实际方向另由资产姿势检查 |
| 原素材与派生动作 | `/Game/Animalia/Cat/Animations/InPlace/Hit_ChestL_Heavy-IP` → `CatForceReactionAuthoring.cpp::CreateForceReactionSourceClips` | 素材没有纯四向；不能按 L/R 名称猜偏移 | 从源冲击曲线生成四向骨盆反应，再由现有 IK Retargeter 导出 CuteCat | 源片段→重定向→单位修正→Montage | 朝向、骨长、首末姿态、Root Motion | 已生成16个资产；四向实际姿势与画面验证见下方证据 |
| 资源消费与 Cook 引用 | `/Game/Character/BP_CatCharacter`、`BP_CuteCatCharacter` 的 `ForceReaction.DirectionalMontages`；CuteCat `PhysicalVisual.AnimationOverrides` → 新 Montage → Sequence → Skeleton | 新表现配置使用既有公共 Slot 和换皮映射 | `FinalizeForceReactionAssets` 配置两个具体子类；抽象基类和公共 ABP 不改 | 先资源，后硬引用，最后启用两个子类 | 重载后 CDO、Skeleton、唯一非循环 Slot；Cook未运行 | 已绑定；`SkinMontagesAndActualPoseDirections` 实查重载消费者；无新 WBP/DataAsset/ini字段 |
| 网络、冲突与清理 | `UCatForceReactionComponent::MulticastReact` → `ACatCharacter::PlayAnimMontage` → 可见 `PhysicalVisual`；Condition原表现状态 | 每端只播放一次；不争抢已有动作 | 服务器事件序号/可靠多播；忙碌或不可行动时消费后跳过；EndPlay仅清自身表现 | 确认开始→各端消费；原抓握负责力源释放 | 房主/拥有端/旁观端、实际 Montage 和最终骨骼；忙碌与恢复不补播 | `OnsetOnlyAcrossThreeEndpoints`、`FormalCuteCatFourDirectionsAndBusyMontage` 验证 |
| 日志、旧路径及非涉及项 | `LogCatCharacter` 的 `force_reaction_*`；原 `DefaultGame.ini` 的 `LineBrokenMontage` 仍由 Fishing 使用 | 新事件与既有断线事件分离，无双重玩法入口 | 日志带 World/NetMode/Authority/LocalRole/Actor/BodyId/EventId/Direction/LoadN/Result | 静态diff→资产重载→Editor/Game→运行 | 新包无需-log落盘未验收；UI、库存、存档、费用不涉及 | 原动画/终局消费者保留；本轮错误的派生动作已同包替换，空Slot已修复；无另一套生产触发入口 |

本轮工作区基线只有用户未跟踪文档 `裁决同步 · 程序（工程待办）.md`，未纳入改动。修改前 `Saved/Automation/ForceReaction-Baseline-20260911/Report/index.json` 的角色族4项通过（2项带警告）；原始素材采样见 `Saved/ForceReaction/hit_poses.json`。首次工具存在空Slot与姿势方向问题，已用行为/姿势失败证据定位后修正，早期 `Report` / `FinalReport` 不作为交付绿灯。

`contract`：最终 Editor Development 构建 `Saved/ForceReaction/BuildDelivery.log` 和 Game Win64 Development 构建 `BuildGame.log` 均成功。`DeliveryAuditFinal.log` / `DeliveryAudit.json` 核查18个包：两种猫到四个 Montage、Sequence 的硬引用链成立，重复生成 Changed=0；只有本功能16个新资产及两个角色子蓝图修改，无临时重定向副本残留。

`runtime_behavior`：`Saved/ForceReaction/VerifiedReport/index.json` 的18项全部通过（15 clean、3项带警告，0 failed/notRun），范围为 ForceReaction、CharacterVariants、Locomotion。包含真实抓握力来源/清理、30/60/120Hz门控、两骨架8段实际姿势方向、正式CuteCat四向播放/忙碌跳过、房主/拥有端/旁观端一次性事件与可见骨骼消费，以及既有跳跃、倒地恢复和四足/抓握步态回归。网络事件测试使用隔离的权威力源，真实抓握入口由另一个运行测试覆盖。三项警告分别是引擎 `r.MotionVectorSimulation` 渲染线程提示及测试传送后旧 ControlEpoch 输入被正常拒绝；没有 `force_reaction_playback_failed`。

`presentation_delivery`：已查看 `Saved/ForceReaction/Screenshots/{Idle,Forward,Backward,Left,Right}.png` 的受控正式 CuteCat 渲染，身体比例及整体朝向保持，动作呈现对应方向的偏移/倾斜。源动作时间曲线派生的动作协调性仍需正式地图多人试玩；新 Cook、Win64 Development 新包房主/客户端无 `-log` 落盘验收未运行，不关闭所属模块。

本轮服务器与两个客户端的实际日志合并在 `D:/develop/Catfishing/Saved/ForceReaction/VerifiedTests.log`，按 `LogCatCharacter`、`force_reaction_started`、`force_reaction_observed`、`force_reaction_rearmed` 和 `BodyId` / `EventId` / `NetMode` 关联；三端成功不是用单端日志推断。打包后仍使用项目标准 `<打包根目录>/Catfishing/Saved/Logs`，未硬编码路径。

交付核对时出现 `Source/Catfishing/Online/CatOnlineSubsystem.{h,cpp}` 并行修改，保留且不纳入本功能检查点；上述已执行构建/回归不代表对随后并行修改的重新验证。

## 资产入口

| 用途 | 资产包路径 |
| --- | --- |
| 共用角色基类（抽象类） | `/Game/Character/BP_CatCharacterBase` |
| 原猫角色 | `/Game/Character/BP_CatCharacter` |
| CuteCat 角色 | `/Game/Character/BP_CuteCatCharacter` |
| 不绑定骨架的 Animation Blueprint Template | `/Game/Character/Animation/ABPT_CatCharacterBase` |
| 原猫动画子类 | `/Game/Animalia/Cat/ABP_Cat` |
| CuteCat 动画子类 | `/Game/Characters/CuteCat/Animation/ABP_CuteCat` |
| CuteCat 步态 BlendSpace | `/Game/Characters/CuteCat/Animation/BS_CuteCat_WalkToRun` |
| 动作重定向器 | `/Game/Characters/CuteCat/Rig/RTG_AnimaliaToCuteCat` |

本轮支持在编辑器中选择角色类。角色游戏逻辑仍由 `ACatCharacter` 及原有系统组件负责；两个角色子蓝图继承同一套镜头、事件与玩法入口。基类不作为可生成角色使用。

打开 `/Game/Game/BP_CatFishingGamemode`，在 Class Defaults 中把 `Default Pawn Class` 设为 `BP_CatCharacter` 或 `BP_CuteCatCharacter`，编译保存后重新运行。Showcase2、Lake、TestMap 都引用该 GameMode。场景中另有手工摆放且自动占有的 Pawn 时，也要替换那个实例，避免绕过 GameMode 的默认生成入口。

## 扩展一个角色

1. 导入独立 Skeletal Mesh、Skeleton、Physics Asset 和材质；不同骨架不要强行指定原猫 Skeleton。
2. 从 `BP_CatCharacterBase` 创建角色子蓝图，在继承的 Mesh 上配置模型、相对变换及动画类。
3. 从 `ABPT_CatCharacterBase` 创建绑定新骨架的动画子类。在 Asset Override Editor 中替换 Idle、WalkToRun、Lean、Jump Start、Fall Loop、Land 共六个动画播放器的资产。
4. 在角色的 `PhysicalVisual` 组件上配置 `Rig Settings`：四条直接父子相连的腿骨链，顺序是前左、前右、后左、后右；骨盆共同祖先；模型前向；站立/移动动画；跳跃状态机、状态名和根骨。每条骨链允许 3–16 节，CuteCat 使用 5 节。
5. 在 `Animation Overrides` 中配置全局玩法动画到新骨架动作的映射。键使用原动作完整对象路径，值使用新骨架的 Animation Asset。原动作身份仍用于开始和停止，网络事件及服务器裁决保持原有路径。
6. 配置倒地表现组件的五段动作，并在新 Skeleton 上提供 `Mouth` Socket。检查嘴叼鱼、镜头、碰撞胶囊和四足接地的实际画面。

公共模板保留 `Locomotion` / `Main States` 两个状态机以及 `DefaultSlot`。新增共用状态、过渡和参数时修改模板；资产替换放在对应动画子类。模板初始化时对 Delta Seconds 除数设下限，避免编辑器预览或首次更新出现除零。

四足地形、步幅和前爪抓握 IK 由 `UCatPhysicsPrototypeVisualComponent` 与 `FCatQuadrupedLocomotion` 统一处理；添加角色时无需再叠加一套 Foot Placement / FABRIK / Control Rig 地形求解。源码中的距离为厘米，步态标定为模型空间厘米/秒，IK 不修改物理移动权威或骨骼缩放。

CuteCat 的默认花色为 Calico。在 Mesh 的材质槽中成对替换 `M_CuteCat_<花色>` 与 `M_CuteCat_<花色>_Fur`，可选 Calico、ClassicTabby、SolidGrey、Tuxedo。第二槽保留毛发透明遮罩材质。

毛片的两套 UV 分工不同：UV0 对应贴图顶部的白色毛丝，供 Alpha → Opacity Mask；UV1 对应毛根在身体贴图上的位置，供 RGB → Base Color。四套 `_Fur` 材质都使用两个独立 Texture Sample，不能让颜色和遮罩共用 UV0，否则全身毛丝会发白。普通皮肤材质仍使用 UV0。

## 生产与回归工具

`Scripts/Art/prepare_cute_cat.py` 在副本中处理源 FBX 的顶层骨架名称；`import_cute_cat.py` 导入模型、21 段源动画及四套花色。`create_cute_cat_retarget.py` 生成缺失的跳跃、倒地及玩法动作；原生 idle / walk / run 用作 CuteCat 步态。

既有白毛材质可设置环境变量 `CUTE_CAT_REPAIR_FUR=1` 后运行 `import_cute_cat.py`：只为旧图添加 UV1 颜色采样，保留原遮罩、粗糙度与其他材质参数；正确图重复执行不保存。导入验证分别检查颜色和遮罩通道，并只将 `Meshes` 下的原生片段计入 21 段数量，避免后续重定向动作造成误报。

`create_character_family.py` 调用 Editor-only 的 `UCatCharacterVariantAuthoringLibrary::CreateCharacterFamily` 做一次性迁移，并在验证工程的 `Saved/CharacterFamilyBackup` 中备份原资产。工具拒绝覆盖已有角色族；不要把该脚本作为每次启动或反复重导的入口。迁移既有资产应在其未被其他编辑器占用时执行。

之后运行 `finalize_character_family.py`：它恢复 CuteCat 根骨 100 倍缩放，单独转换重定向骨盆的位置单位，保留其余骨骼的原始局部间距；同时把三个转向附加姿势处理为仅旋转、零位移增量，其中 Neutral 为附加单位姿势。工具也能修复早期仅恢复根缩放却压缩子骨骼间距的资产，重复运行不再改写正确数据。鱼竿蓝图的原猫专用 Cast / 直接 Mesh Montage 播放会迁移为 `ACatCharacter::PlayAnimMontage`。原 FBX、原生 idle/walk/run、原猫骨架和原动画数据不因此改写。

定向 Automation 入口为 `Catfishing.CharacterVariants`，并应回归 `Catfishing.Locomotion` 及原有抓握/跳跃测试。运行真实渲染验证时增加 `-CatVariantScreenshots`，输出位于该工程 `Saved/CharacterVariantScreenshots`。Automation 通过不替代正式地图、联网画面或打包体验验收。

Development 落盘诊断可检索 `LogCatCharacter` 的 `physics_prototype_visual_ready`、`physics_prototype_visual_init_failed`、`character_animation_rejected`，以及 `LogCatLocomotion` 的 `locomotion_clip_calibrated`。日志跟随工程/打包版本的标准 `Saved/Logs` 位置，不依赖 `-log` 控制台参数。

## 本轮迁移核对

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 编辑器角色选择 | `BP_CatFishingGamemode.DefaultPawnClass` 引用原 `BP_CatCharacter`；Showcase2 / Lake / TestMap 引用该 GameMode | 原来只有原猫；新增同级 CuteCat 子类，默认角色保持原猫 | 原路径保留，新增 `BP_CatCharacterBase` 和 `BP_CuteCatCharacter` | 先创建抽象基类，再迁移原猫、配置新猫 | 类继承、模型/动画绑定、相机组件重复 | 已接入；两个子类直接继承基类，原猫局部 SCS 无孤立 Camera；包重载审查与 Contract 通过 |
| 共用动画图 | `/Game/Animalia/Cat/ABP_Cat` 原有 Locomotion / Main States / DefaultSlot | 独立图改为无 Skeleton 模板；6 个实际播放器由子类覆盖；参数、状态名和时序保留 | 图移到 `ABPT_CatCharacterBase`，原 ABP 路径变为子类 | 模板编译后再迁移子类 | 跳跃三态、Slot、初始化除零、图是否仍可编辑 | 原事件图和 18 个动画图已重载检查；删除两个未连接播放器；Delta Seconds 分母保护；两端跳跃与动作测试通过 |
| 四足与抓握 IK | `CatQuadrupedLocomotion::{Initialize,GetProfile,SolveFoot}`、`CatPhysicsPrototypeVisualComponent::{InitializeVisual,SolveHandReach}` | 原硬编码 4 节改为配置 3–16 节；CuteCat 为 5 节；模型空间 cm、速度 cm/s 和物理权威不变 | 新 `FCatCharacterRigSettings`；唯一原生姿势修正链保留 | 先解算器支持可变骨链，再绑定新资产 | 骨长、缩放、地面探测、抓爪、移动与 Reset 清理 | 两种角色骨长/缩放有限性与抓爪通过；CuteCat 行走 180 帧四足均找到地面；旧步态/Reset 回归通过 |
| 重定向单位 | CuteCat FBX 根骨 `Cat_Root` 为 100 倍，IK Retargeter 导出根为 1 倍 | 直接混合会异常偏移；转换回原 Skeleton 的局部坐标表示 | `NormalizeCuteCatRetargetedAnimations` 转换生成的动作数据，保留组件空间位置/旋转 | 先导出，再统一表示，最后运行混合验证 | 根骨、骨盆、跳跃与倾斜叠加 | 19 个动作序列已转换；原生 idle/walk/run 保持原数据；修正后两种角色均实际跳起约 90 cm，无运行姿势错误 |
| 全局玩法 Montage | `ACatCharacter::{PlayAnimMontage,StopAnimMontage}`；DefaultGame.ini BodyAction / Fishing 表现配置 | 配置仍传原动作身份；新角色解析为对应骨架动作，原服务器裁决/事件不变 | `PhysicalVisual.AnimationOverrides`；不匹配骨架明确拒绝并落盘 | 先生成映射，再使用原播停入口 | 抛竿/交互、广播、原动作身份停止 | 共 34 条映射；双角色播停、真实鱼竿回调、双端广播测试通过；`character_animation_resolved` 记录实际解析结果 |
| 鱼竿蓝图消费者 | `/Game/Blueprint/Actors/BP_CatFishingRodActor` 被两套正式竿装备和 TestMap 引用 | 原 Cast 原猫 + Mesh 直接播放改为 Cast `ACatCharacter` + `PlayAnimMontage`；bDeployed 边沿条件不变 | 同包内迁移；移除旧临时角色变量及对应 Get/Set/Mesh 节点 | 补齐 Attack_Left 重定向后再迁移 | 原 Montage 回调是否被使用、拿放竿两角色兼容 | 原异步回调均未连接；已迁移为单一播放入口，实际 Blueprint 回调在两种角色上通过 |
| 倒地与恢复 | `CatConditionPresentationComponent::PoseClips` → `PlayPhase` | 原五段顺序、复制与恢复生命周期保留；数组开放默认值编辑 | 子类保存自己骨架的五段动作 | 重定向后配置子类 CDO | 服务器倒地、客户端躺下、恢复直立与移动 | 真实 Condition 权威请求和复制通过；两端到达 DownedPose 并恢复 Locomotion；新猫头部实际下降并恢复 |
| 模型、附着和持久化 | `/Game/Characters/CuteCat`、Skeleton `Mouth`；材质沿用导入的四套花色 | 新骨架独立，Mouth 抵消导入根缩放；旧猫 Skeleton 不改 | 新 Mesh/Skeleton/动作依赖；仅迁移明确资产包 | 备份后生成、重载验证，再按哈希接回主工程 | 资源引用、嘴叼鱼外观、保存覆盖 | Mesh/骨架/材质/Socket 绑定通过；固定视角待机/走路/跳跃/动作截图已检查；真实嘴叼鱼对齐仍需正式地图人工验收 |
| 配置、权威、UI、Cook 与清理 | 原 GameMode、Condition/GAS、移动组件及 DefaultGame.ini 仍为原入口 | 未改变网络状态、库存/存档、UI、权限或业务写入；无第二份玩法状态 | 不涉及 UI/存档字段迁移；新子类选择后由 GameMode 的硬引用进入 Cook | 删除已替换的本地图、固定骨链和旧竿 Cast；保留仍有消费者的原猫资产 | Development 编译、完整定向回归、正式打包范围 | Editor 和 Game 编译通过；未重做 Cook、正式地图人工体验或打包双端验收，模块级交付状态保持未关闭 |

`contract`：隔离 Editor 构建与主工程 Editor 构建成功；Game Win64 Development 成功（`Saved/CharacterVariants/BuildGame.log`）。主工程 Editor 的组合构建日志为 `Saved/Automation/UprightCMC-20260910/BuildEditor-MainGrabJump.log`，包含本轮资产工具与测试。

`runtime_behavior`：主工程 `Saved/CharacterVariants/MainFinalReport/index.json` 的 10 项定向回归全部通过，9 clean、1 warning、0 failed、0 notRun。警告来自原猫跳跃网络用例在重置控制 Epoch 后拒绝一条迟到输入，不是动画失败。日志 `Saved/CharacterVariants/MainFinalTests.log` 同时包含服务器和客户端事件。早期隔离组合回归的旧图检查曾使用复制前编译缓存；强制更新该测试目标后，单项和主工程组合回归均通过。

`presentation_delivery`：固定视角截图在隔离工程 `Saved/Validation/CharacterVariants/Saved/CharacterVariantScreenshots`。图像可证明已绑定模型和实际动作，不能替代正式地图真人观感、嘴叼鱼精确对齐、全部花色或 Cook 后的双端验证。

## 2026-09-10 走跑缩小修复

用户反馈推翻了上文初版“无运行姿势错误”的结论：初版测试只证明 IK 保留动画输入，未检查输入本身的骨骼比例。原生待机、走路、跑步不需要重定向，也未修改；问题来自叠加在步态上的旧猫转向层，以及同源的跳跃/动作片段。

本地 UE 5.8 `IKRetargetProcessor.cpp::FResolvedRetargetPoseSet::AddOrUpdateRetargetPose` 去除局部和全局缩放后，只重新计算了骨盆局部位移。早期归一化将其余仍处于原始骨骼单位的位置又除以 100，导致附加层把身体间距压缩；其权重在 20–200 cm/s 从 0 增至 1，因此越跑越严重。修复前实际脸部间距在 100 cm/s 为参考值的 0.561834，300 cm/s 最低 0.009991。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 生成及既有资产 | `NormalizeCuteCatRetargetedAnimations` ← `Scripts/Art/finalize_character_family.py` | 根缩放正确但子骨骼间距缩为 1/100；恢复参考骨架比例 | 同入口修复原始导出与旧归一化结果，异常单位拒绝写入 | 先修工具，再迁移 19 段序列 | 全部 432 骨骼、所有关键帧、重复运行 | `RepairProportionsFinal.log` 重复执行 Changed=0；新比例契约通过 |
| 步态和转向 | `ABPT_CatCharacterBase` Walk / Run → `Comp_Add_Lean` 三段 Add 序列 | 速度提高会加重压缩；维持原生步态 | 三段附加动画恢复参考位移/缩放，Neutral 恢复参考旋转 | 修好素材再使用原模板消费者 | 100/200/300 cm/s，直行、转弯、启停 | 原失败用例通过；源 Mesh 和最终 IK Mesh 比例范围 0.999956–1.000048 |
| 跳跃、动作、倒地 | 子 ABP、`AnimationOverrides`、`PoseClips` → `Retargeted` | 同源片段缩小；保留播停、状态切换和原动作身份 | 修复同批 19 序列，其 Montage、BlendSpace 和引用不变 | 归一化后验证原调用链 | 两角色跳跃/动作/鱼竿回调及 CuteCat 双端倒地恢复 | 运行及网络回归通过；跳跃截图中身体比例恢复 |
| IK 与移动权威 | `CatQuadrupedLocomotion`、`CatPhysicsPrototypeVisualComponent` → 可见模型 | 旧测试仅比较输入输出；增加独立参考比例 | 保留求解器与 CMC；扩展角色测试 | 正确动画输入继续走原 IK | 四足接地、骨长、旧猫、抓爪与跳跃 | 11 项组合回归全部通过，9 clean、2 warning |
| 持久化、配置、Cook、日志与清理 | 19 个 `Retargeted/*.uasset`；原角色硬引用；本工具和文档 | 修正生成入口及正式资产；无新玩法状态 | 精确替换同包资源；保留用户 ABP、GameMode、材质、骨架与鱼数据改动 | 隔离验证后主工程重载 | 文件哈希、重载、主工程运行检查 | 19 个包接回后哈希与隔离验证一致，主工程重载回归 11/11 通过；Cook/打包未运行 |
| UI、权威、生命周期 | 原 GameMode、CMC、Condition/GAS、原 Montage API | 不涉及 UI、存档、权限或复制字段变化 | 保留全部既有入口，无新增退出清理对象 | 无额外状态衔接 | 双端表现消费者回归 | 通过；模块级正式交付缺口仍保留 |

`contract`：隔离 Editor 构建 `BuildProportionsFinal.log` 成功；新增 `RetargetProportionsAndNeutralLean` 在原资产上失败，修复后通过。`runtime_behavior`：`Saved/CharacterVariants/ProportionsAfterReport/index.json` 的 11 项全部通过；两个警告分别是引擎 `r.MotionVectorSimulation` 渲染线程访问提示及原网络重置后拒绝迟到输入。已有 Development 诊断事件继续生效，修复工具新增 `character_retarget_proportions_repaired`，无每帧刷屏或第二份玩法状态。

`presentation_delivery`：已查看本次 Idle、Speed100、Speed300、Turn300、Jump 渲染图，缩小与骨骼聚拢消失。截图在隔离工程 `Saved/CharacterVariantScreenshots`；本轮没有修改毛发材质。正式地图真人手感、完整动作美术润色、嘴叼鱼对齐及新 Cook/Development 包双端验收仍未完成。本次未修改 Game 模块源码，不重复使用旧 Game 编译记录声称新打包已验收。

主工程接回：用户保存并关闭编辑器后，只替换 `ProportionsDeliveryManifest.json` 中的 19 个动画包，替换前校验均与 HEAD 一致，备份位于 `Saved/CharacterVariants/BeforeProportionsFix`。`BuildProportionsMain.log` 编译成功；`ProportionsMainReport/index.json` 重载用户当前蓝图配置后 11/11 通过（9 clean、2 warning、0 failed、0 notRun），运行比例与隔离工程一致。相关文件均位于 `Saved/CharacterVariants`；未修改、提交用户的并行资产。

## 2026-09-10 毛发取色修复

修改前：四个毛发材质用 UV0 同时采样 RGB 和 Alpha，因而显示贴图顶部的白色毛丝。FBX 的毛片 UV0 范围集中在该区域，UV1 则覆盖身体花色。Calico 毛发及皮肤材质已有未提交修改，本轮保留其参数；动画、角色、鱼数据等并行改动不纳入提交。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 颜色与遮罩 | 四个 `/Game/Characters/CuteCat/Materials/M_CuteCat_*_Fur` → `SK_CuteCat` 第二材质槽 → 角色可见模型 | RGB 与 Alpha 共用 UV0；颜色应取毛根对应花色 | 新增 UV1 RGB 采样，保留 UV0 Alpha 及其他参数 | 备份当前参数后修复同包材质 | 四套连接及实际渲染 | 四套均已修复；Calico 跑动图可见黑、棕、白毛随身体花色分布 |
| 生成与检查 | `import_cute_cat.py::{import_assets,configure_fur_color,repair_fur_materials,verify}` | 重导会重现错误；旧动画计数会包含后来重定向片段 | 新旧资产共用取色规则，原生 21 段按目录计数 | 先修生成规则，再修已有包 | 保存重载、幂等执行、异常图拒绝 | `RepairFurVerify.log` 四套 Changed=false，验证通过；旧误报计数已清理 |
| 保存与交付 | 上述四个包的原硬引用；本文档 | 保留花色选择和皮肤材质 | 主工程直接修改现有毛发图；Calico 提交版本单独基于 HEAD 生成，避免纳入先前参数改动 | 隔离渲染后主工程保存重载 | 材质图检查与备份 | 主工程日志 `RepairFurMain.log`、`RepairFurMainReload.log`；修改前副本在 `Saved/CharacterVariants/BeforeFurFixFinal` |
| 动画、IK、网络、UI、生命周期与 Cook | 原角色/可见模型继续引用同包材质 | 不涉及玩法状态、复制、存档、UI或退出清理变化 | 不新增运行代码或状态；原资产硬引用继续进入 Cook | 无额外依赖迁移 | 验证限定本轮材质表现 | 不涉及功能修改；本轮未运行新 Cook/打包双端验收 |

证据均位于 `Saved/CharacterVariants`。`contract`：四套颜色 UV1 / 遮罩 UV0 与保存重载检查通过；`runtime_behavior`：隔离 `FurRenderReport/index.json` 角色走跑、转弯、跳跃和动作用例 1/1 通过；`presentation_delivery`：已查看 Calico 跑动截图，四套花色均有材质连接检查，另外三套未逐套截图。本轮未修改 C++，不重跑无关构建与整模块验收。
