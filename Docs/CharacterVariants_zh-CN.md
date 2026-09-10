# 角色换装与公共动画模板

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

## 生产与回归工具

`Scripts/Art/prepare_cute_cat.py` 在副本中处理源 FBX 的顶层骨架名称；`import_cute_cat.py` 导入模型、21 段源动画及四套花色。`create_cute_cat_retarget.py` 生成缺失的跳跃、倒地及玩法动作；原生 idle / walk / run 用作 CuteCat 步态。

`create_character_family.py` 调用 Editor-only 的 `UCatCharacterVariantAuthoringLibrary::CreateCharacterFamily` 做一次性迁移，并在验证工程的 `Saved/CharacterFamilyBackup` 中备份原资产。工具拒绝覆盖已有角色族；不要把该脚本作为每次启动或反复重导的入口。迁移既有资产应在其未被其他编辑器占用时执行。

之后运行 `finalize_character_family.py`：它把重定向动画的单位缩放转换回 CuteCat 原始根骨的 100 倍表示，同时保持模型空间的动作轨迹，并把鱼竿蓝图的原猫专用 Cast / 直接 Mesh Montage 播放迁移为 `ACatCharacter::PlayAnimMontage`。这一步不可跳过，否则与原生步态混合时身体会异常偏移。原 FBX、原猫骨架和原动画数据不因此改写。

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
