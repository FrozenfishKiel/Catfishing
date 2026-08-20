# 钓鱼系统 Bug 复盘记录

> **维护约定**：本文档由 Claude 自动维护——每次诊断并修复一个 bug 后追加一条。条目模板：
> `### [日期] 标题` + **症状 / 根因 / 修复 / 教训** 四段。按主题分组，新条目加进对应主题（没有就新开）。
> 人也可以手工补充，格式一致即可。

---

## 一、StateTree / 资产

### [2026-08] ST_RunFlow 双击/加载即崩溃（两次）
- **症状**：崩溃栈在 `PostLoad → FInstancedStructContainer` 断言 `IsValid() && IsValidIndex(Index)`。
- **根因**：状态树里存在**畸形转移**（EventTag 留空 + GotoState 指向 Root 之类的不完整配置），编译产物损坏；资产坏了之后连打开都崩。
- **修复**：把损坏资产挪出 Content（`.corrupt.bak`）后重建；重建时转移条件/目标全部填完整。
- **教训**：StateTree 转移必须填全；**保存前必先 Compile**，编译报错就不要保存。

### [2026-08] StateTree 状态进入即秒退
- **症状**：状态刚进入就完成退出，阶段像被跳过。
- **根因**：状态的 **TasksCompletion 引擎默认是 Any**——任一 Task 完成整个状态就算完成。
- **修复**：所有多任务状态改为 **All**。
- **教训**：建每个状态都手动检查 TasksCompletion；树不可自然结束——终态只归 C++ 的 FinalizeSession。

## 二、水域 / 样条烘焙

### [2026-08] 烘焙几何一重启就丢
- **症状**：编辑器里 BakeGeometry 成功，重启后 Region 又是"无烘焙"。
- **根因**：样条 Actor 的 `PostEditChangeProperty` **无条件作废烘焙**；蓝图 compile-on-load 重实例化会以**空 Property** 触发该回调，启动即清空。
- **修复**：回调加 `PropertyChangedEvent.Property &&` 空属性守卫（CatWaterBoundarySplineActor.cpp）。
- **教训**：PostEditChangeProperty 必须防空 Property——compile-on-load / 重实例化都会用空事件调它。

### [2026-08] 放竿 InvalidWaterTarget
- **症状**：明明站在岸边，放竿报 InvalidWaterTarget。
- **根因**：Region 的 `BankHeightToleranceCm=0`，岸点高度识别永远失败；且原判定要求点在样条**内**，而放竿应在岸上（样条外）。
- **修复**：参数调 300/150/30 并重烘焙；PlaceRod 改为"样条外且 |岸距|≤400cm"的岸带规则。
- **教训**：烘焙参数为 0 通常意味着"未配置"而非"严格"；判定语义要按玩法（岸上放竿）而不是几何直觉（水里）。

## 三、命令链 / 输入

### [2026-08] 咬钩提竿失败后，松开左键变成重新抛竿
- **症状**：显示鱼咬钩，点左键却整个重新开始钓鱼。
- **根因**：会话在左键**按住期间**终止 → 松开时已无会话 → 松开事件落入"无会话=抛竿"分支。
- **修复**：服务器记录 `ServerAimingCorrelationId`——只有**与按下同一次**的松开才触发抛竿；有会话的按下会先 Invalidate 它。
- **教训**：按下/松开跨越状态边界时必须配对（correlation），不能只看松开时刻的状态。

### [2026-08] 放竿后"猫视角钉死在竿上无法脱离"
- **症状**：OperateRod 后镜头锁死、角色卡住。
- **根因**：传送到站位锚点时胶囊**穿地**（锚点在地面高度）；且 DA_Rod 锚点数据未配。
- **修复**：传送点抬胶囊半高 + `TeleportTo`；进入操作 DisableMovement、离开恢复 MOVE_Walking；补齐 DA_Rod_Basic 三锚点。
- **教训**：任何"传送到数据锚点"都要按胶囊半高修正；锁定态必须有对称的解锁路径（E 离开 / X 收竿都要恢复移动）。

### [2026-08-19] Ability 蓝图里放的 Montage 完全不播，后面的 Print String 却立刻输出
- **症状**：`BP_GA_RodInteract` 的 `BP_OnLocalInputActivated` 里接了播放动画节点，按 E 时动画一帧都看不到，但后续调试信息立即打印。
- **根因**：用的是 **`Play Montage and Wait`**（GAS AbilityTask，依赖 `/Script/GameplayTasks`），它的生命周期绑在 Ability 上。一次性 Ability（E/F/X）在 `BP_OnLocalInputActivated` 返回后**同帧** `EndAbility` → 引擎销毁全部 AbilityTask → `UAbilityTask_PlayMontageAndWait::OnDestroy(AbilityEnded=true)` 且 `bStopWhenAbilityEnds` 默认 true → `StopPlayingMontage()`，动画被立刻掐断。不是"异步导致跳过"——异步让 exec 立即往下走是正常行为。
- **修复**：蓝图改用 **`Play Montage`**（`UPlayMontageCallbackProxy`，不绑 Ability 生命周期，同样有 OnCompleted/OnNotify 等引脚）或 `Play Anim Montage`。不要靠"取消 bStopWhenAbilityEnds 勾选"绕开——引擎注释明写 ability 被 **cancel** 时无条件停，而命令提交失败时 C++ 就会 CancelAbility。
- **教训**：GAS 里凡是 AbilityTask（名字常带 "and Wait"）都会随 Ability 死亡被回收，只适合"Ability 要一直活着等它"的场景；我们的架构里 Ability 只是命令发射器、发完即死，所以**表现一律用不绑生命周期的普通异步节点**。同类排查技巧：看蓝图资产依赖里有没有 `/Script/GameplayTasks`，有就是误用了 AbilityTask。

### [2026-08-19] 换成 `Play Montage` 后动画**仍然**不播（第二层原因，与上一条独立）
- **症状**：已按上条把节点换成 `Play Montage`，ABP 里 Slot 也确认存在，动画依旧一帧不出；`then` 后面的 Print String 照常立即输出。
- **排查过程**（这条路径值得复用）：逐层证伪——`AM_Scratching` 骨架=`Cat_Skeleton`、slot=`DefaultSlot`、1 段 8.37s ✅；`BP_CatCharacter` 的 Mesh=Cat、AnimClass=ABP_Cat、AnimationMode=AnimationBlueprint、Visible ✅；导出 `ABP_Cat` 的 AnimGraph 确认接线是 `StateMachine → Slot(Source)`、`Slot(Pose) → Root(Result)`，Slot 确实在最终姿势链路上且 SlotName 为默认 `DefaultSlot` ✅。全部排除后才导出 Ability 蓝图的节点引脚，一眼看到问题。
- **根因**：`Play Montage` 节点的 **`In Skeletal Mesh Component` 引脚为空**（导出里该 Pin 既无 `LinkedTo=` 也无 `DefaultObject=`）。引擎 `UPlayMontageCallbackProxy::PlayMontage` 是 `if (InSkeletalMeshComponent) { ...Montage_Play... }`，为空直接跳过并 `OnInterrupted.Broadcast()` —— **静默失败，不报错不打日志**。而 Print String 接在 `then` 引脚上，`then` 在发起播放那一帧就同步触发，所以"立即输出"是正常现象，**不是**动画被掐断的证据（这点一开始误导了排查方向）。
- **修复**：Cast 节点的 `As Character` → `Get Mesh` → 接到 `In Skeletal Mesh Component`；或改用 `Play Anim Montage`（Character 函数，内部自取 Mesh，没有这个引脚）。顺带删掉图里游离的 `Play Montage and Wait`（虽已无接线不执行，但仍拉着 `/Script/GameplayTasks` 依赖）。
- **教训**：①"节点静默失败"是蓝图最难查的一类——凡是带 Target/Component 对象引脚的节点，为空时通常什么都不做且**不报错**，排查时优先怀疑对象引脚而不是资产配置；②`then` 引脚 ≠ 动作完成，想等播完必须接 `On Completed`；③排查动画不播要按"资产 → 骨架 → Mesh/AnimClass → AnimGraph 接线 → 调用方引脚"顺序逐层证伪，别一上来就怀疑 Slot。
- **[2026-08-20] 复发一次**：`BP_GA_Primary` 里按阶段分支（无会话=举竿 / 非搏斗=提竿 / 搏斗=收线）新加的 `Play Montage`，Mesh 引脚同样全空，症状还是"按左键完全没反应"。**这个引脚是本项目复发率最高的坑**——新建任何 `Play Montage` 节点时把接 `Get Mesh` 当成建节点的一部分，或者干脆默认用 `Play Anim Montage`（Ability 蓝图里 Target 接 `Cast To Character` 的 `As Character`，不能留空——留空是 Ability 对象自己）。判定方法不用进编辑器：导出节点搜该 Pin 有没有 `LinkedTo=`，没有就是它。

### [2026-08-19] 鱼进抄网半圆内，按 F 抄鱼无反应
- **症状**：搏斗把鱼拉到近岸、鱼明明在 debug 翡翠半圆里，按 F 毫无反应（服务器静默拒绝 PolicyUndecided，无日志）。
- **根因**：范围判定**两套口径**——debug 半圆按新规则画"圆心=面向与岸线交点、半径固定 200cm"，但服务器 `RequestScoop` 还是旧规则"**猫到鱼直线距离** ≤200cm"。猫站得离水边稍远时，鱼在半圆内却离猫超 200cm，被服务器拒绝。改需求时只改了绘制，权威判定没同步。
- **修复**：把"面向找岸点 + 前向半圆判含"抽进 `UCatFishingAimLibrary::ResolveScoopAnchor / IsPointInScoopSemicircle`，服务器裁决与 debug 绘制调同一函数；debug 半径也对齐权威的 min(全局设置, 装备抄网 DA)。
- **教训**：判定规则变更必须"权威 + 表现"一起改；本项目的第五条契约（AimLibrary 同源数学）就是防这个的——凡是"画出来给玩家看的范围/落点"，画图代码一律调裁决同款函数，禁止在 debug 里复写一份几何。

## 四、装备 / 数值 / DataAsset

### [2026-08] No eligible fish（选不出鱼）
- **症状**：真咬窗永远不来 / 日志报无候选鱼。
- **根因**：两层——① CatFishCatalogSettings 三项（饱和曲线/半饱和/上限）未配，曲线校验要求 **v(0) 恰=1.0 且单调不减**；② `Curve_ChumSaturation` 在 **PIE 运行中保存被静默拒绝**（返回成功但不落盘），重启即丢。
- **修复**：停 PIE 后重新 save_asset 落盘；配齐三项。
- **教训**：**PIE 运行中保存资产必失败且无报错**——一切资产保存都停 PIE 再做；带形状校验的曲线（v(0)=1 等）改完要跑一遍验证。

### [2026-08-19] 窝料发放失败 → 左上角窝料 x0 → 打窝无效
- **症状**：预览抛物线正常，投出后无窝点圈；日志 `starter_chum_grant InvalidPayload`、`place_chum_result EquipmentUnavailable`。
- **根因**：`CatChumFieldTypes.cpp` 的 BakeCurve 用 `TSoftObjectPtr.Get()`（**只取已加载，不加载**）。PIE 启动发 starter 窝料的瞬间，两条衰减曲线还没被任何引用加载 → 烘焙失败 → Chum_Basic 判"未就绪" → 发放被拒。此前 Python 自测能过是因为脚本恰好预加载了曲线，**掩盖了问题**。
- **修复**：`.Get()` → `.LoadSynchronous()`（连同 PresentationClass 同款隐患）。
- **教训**：运行时校验/装配路径上的软引用一律 `LoadSynchronous()`；自动化测试若预加载资产，会掩盖加载顺序类 bug——至少跑一次"冷启动"验证。

### [2026-08] 放竿 SpawnActor 失败（给鱼竿 Mesh 调了朝向之后开始）
- **症状**：之前能放竿，调整表现 Mesh 后 SpawnActor 返回空，还误报 ActiveSessionExists。
- **根因**：Spawn 用 `DontSpawnIfColliding`，**表现 Mesh 的碰撞**否决了生成——表现层反向干扰了权威流程。
- **修复**：改 `AlwaysSpawn`；规定 Rod/Hook/Fish 蓝图内所有表现 Mesh 必须 **NoCollision**。
- **教训**：权威生成不能被表现碰撞否决；表现 Mesh 默认带碰撞，建蓝图第一件事设 NoCollision。

### [2026-08-19] DA_Rod_Basic 三个锚点值全部归零（第二次踩 PIE 保存陷阱）
- **症状**：竿尖调试线从竿根/错误一侧出发；检查发现 DA 里竿尖/站位/握把三个 Transform 全是 0（此前配过的值不见了）。
- **根因**：高度怀疑当时在 PIE 运行中保存——返回成功但未落盘，重启后回到默认 0。
- **修复**：按 Mesh 包围盒+组件旋转重算视觉竿尖(161.5,-1.3,151.9)写回 DA 并落盘；同时在 Rod 蓝图 VisualRoot 下生成带 RodTipMarker 标签的表现标记组件（调试线优先跟随，蓝图里可直接拖动微调）。
- **教训**：重要资产数值配置完，重启 Editor 验证一次是否真的还在；表现对位类需求优先用可拖动的标记组件而不是反复改 DA。

## 四点五、表现与权威的对位

### [2026-08-19] 鱼的 Mesh 朝向和实际游动方向不一致（差点误判成"需要给鱼加 AIController"）
- **症状**：搏斗中鱼的位置在动，但 Mesh 一直朝着同一个方向，看起来像横着飘。
- **根因**：`ACatFishEncounterActor::ApplyFightStepFromAuthority` 从头到尾**只调 `SetActorLocation`，从未设置过旋转**；鱼生成时用的是 `FTransform(FishLocation)`（零旋转），所以永远面朝世界 +X。与 AI/控制器无关——鱼的位置本来就是约束求解（线长 + 竿尖 + 水域包围盒）的输出，不存在"谁来决策朝向"的问题，只是没人写这一步。
- **修复**：在 `ApplyFightStepFromAuthority` 里用本步位移的水平分量求偏航角，按 `MaximumTurnRateDegreesPerSecond` 限速转向（`FMath::FixedTurn` 自带角度环绕），位移 < 1cm 时保持上一帧朝向避免噪声乱转；只转 Yaw 不转 Pitch（鱼贴水面走，Z 抖动会让俯仰角疯跳）。美术资源前向轴不是 +X 的情况用 `VisualYawOffsetDegrees` 加在 VisualRoot 上修正。
- **教训**：①"表现不对"先分清是**权威数据没写**还是**表现层没读**——这次是权威侧压根没产出旋转数据，在蓝图里怎么调都没用；②看到"物体运动表现不自然"容易条件反射地想引入 AI/行为树，但如果它的运动是**约束求解的输出**而非自主决策，引入 Controller 只会多一层壳并引入第二个时钟（破坏固定步进的可复现性），正确做法是在既有的固定步进里补齐输出字段。

### [2026-08-20] 想从表现事件里的 `PlayerState` 拿到猫 Actor，走 Controller 这条路在客户端拿不到
- **症状**：在 `BP_OnRodPresentationChanged` 里拿到 `Current.OperatorPlayerState`，想顺藤摸瓜找到猫来播 Montage，结果从 PlayerState 引脚只能拖出 `Get Owner`，找不到 `Get Owning Controller`；即使用 `Get Player Controller`，在客户端上对**别人**的 PlayerState 也返回空。
- **根因**：两条都不通，原因不同——①`APlayerState::GetOwningController()`（`PlayerState.h:203`）**没有 UFUNCTION 标记**，蓝图里根本不存在这个节点；②`GetPlayerController()` 的实现就是 `Cast<APlayerController>(GetOwner())`（`PlayerState.cpp:174-177`），而 `AActor::Owner` 只复制给**属主连接**，所以在你的客户端上，别人的 PlayerState 的 Owner 是空的。多人场景下这条路天然只对"自己"成立。
- **修复**：改用 `APlayerState::GetPawn()`（`PlayerState.h:194-196`，`BlueprintCallable`）→ `Cast To BP_CatCharacter` → `Get Mesh`。它读的 `PawnPrivate` 不依赖 Owner，而是由 Pawn 自己的**复制属性** `PlayerState` 回填的（`APawn::OnRep_PlayerState` → `SetPlayerState` → `FSetPlayerStatePawn`，`Pawn.cpp:647-665`），所以在所有客户端上对所有玩家都有效，还少一跳。
- **教训**：在多人游戏里从 `PlayerState` 反查"人"，永远优先走 `GetPawn()`，不要走 `Owner`/`Controller`——**Controller 是服务器权威对象，客户端上只有自己那一个**；凡是"这个引脚在单机好使、联机对别人失效"的表现层 bug，先怀疑链路上有没有踩到 Owner/Controller。同理可推：`PlayerState` 是所有客户端都能看到全体的（在 `GameState.PlayerArray` 里），它才是表现层做"跨玩家反查"的正确起点。

### [2026-08-20] 放竿会播动画，收竿（X）不播
- **症状**：Rod 蓝图里 `BP_OnRodPresentationChanged` → `IsValid(Current.OperatorPlayerState)` → 播猫的 Montage，放竿正常，按 X 收竿完全没反应。
- **根因**：两层，都不在蓝图里。①**判据选错**：按 X 收自己的竿是**两步命令**——先 `LeaveRod` 再 `PackRod`（`CatFishingCommandComponent.cpp:478-493`），因为 `Service.cpp:455` 硬性要求 `OperatorPlayerState` 为空才允许打包。所以收竿触发的两次表现事件里 `Current.Operator` 都是 null，`IsValid` 两次都走没接线的 False 分支。②**联机时那次变化根本到不了客户端**：`PackRod` 里 `SetDeployedFromAuthority(false)` 的 `ForceNetUpdate()` 只是标脏，真正发包要等下一次 NetDriver tick，而同一帧结尾就 `Rod->Destroy()`，Actor 已 pending kill，远端客户端收到的是"销毁"而非属性更新。listen server 本机因为是同步 dispatch 反而会触发，所以这一层在 PIE 单机下被完全掩盖。
- **修复**：C++ 两处——`PackRod` 的裸 `Destroy()` 换成 `SetLifeSpan(TerminalReplicationWindowSeconds)`，与 `ACatFishingSession::ScheduleTerminalDestroy` 用同一个终态复制窗；同时在 `ACatFishingRodActor::DispatchPresentationChanged` 里对 `!Current.bDeployed` 立刻 `SetActorHiddenInGame(true)` + `SetActorEnableCollision(false)`，避免死亡窗里留一根挡路的幽灵竿。蓝图侧新增 `Previous.bDeployed && !Current.bDeployed` 分支，猫从 **`OwnerPlayerState`** 取（Operator 此时已被清空）。
- **实证签名**：根因②的表现是"**单机 PIE 下一切正常，联机时只有 listen server 自己看得到，其他客户端完全没反应**"。最初我只从代码推断出这一层、没有实测就动手改，被要求还原；后来玩家实测"E 放竿的动画会同步，X 收竿不会"才拿到实证。**这个不对称本身就是诊断线索**：两者都走同一个 `BP_OnRodPresentationChanged`，唯一区别是收竿路径的 Actor 在属性发包前被销毁了。
- **教训**：①**"一个按键 = 一条命令"是错觉**——X 收竿实际是 LeaveRod + PackRod 两条，会触发两次表现事件，选判据前必须先读命令分派层看一个按键到底展开成几步；②**销毁前的最后一次属性变化是不可靠的**：`ForceNetUpdate` 标脏 ≠ 已发送，同帧 `Destroy()` 会把它吃掉。凡是"Actor 消失前要让客户端看到的状态"，都必须留复制窗（本项目既有解法 `ScheduleTerminalDestroy`），而留窗又必须配套本地隐藏+关碰撞，否则修好一个 bug 造出一个幽灵；③`bHidden` 是复制属性但 `bActorEnableCollision` 不是，所以隐藏动作要放在**分发路径**（服务器和每个客户端各自本地执行），不能只在权威写口做；④**只在单机 PIE 验证的"修好了"不算数**——这类只在多客户端下暴露的表现 bug，验收必须开 2 个 PIE 客户端互看。

### [2026-08-20] 挥网/提竿动画其他玩家完全看不到（Ability 表现钩子天生只有本人可见）
- **症状**：放竿（E）的动画在其他客户端能看到，但抄鱼（F）挥网、提竿的动画只有操作者自己能看到。
- **根因**：这几个动画都挂在 Ability 的 `BP_OnLocalInputActivated` 上，而该钩子在 `ActivateAbility` 里被 `IsRemoteAuthorityMirror(ActorInfo)` 提前拦截（`CatFishingAbilities.cpp:94-97`），服务器侧镜像直接 return——**设计上就只在本地控制端调用**。放竿之所以能同步，是因为它的动画挂在 Rod 的 `BP_OnRodPresentationChanged` 上，那个事件由 `OnRep_PresentationState` 在每个客户端各自触发。深层原因：**Montage 本身不复制**（`ACharacter::PlayAnimMontage` 就是一句本地 `AnimInstance->Montage_Play`，`Character.cpp:2190-2195`），所谓"同步动画"从来都是"每台机器各自本地播一次"，靠的是权威状态复制过去后各自触发表现事件。
- **修复**：放竿/收竿/断竿/抛竿/打窝都有复制状态，天然全同步，不用动。真正的缺口只有**失败时不留任何权威痕迹**的两个动作（挥网落空、提竿空竿）——服务器上什么都没改变，表现层没有可读的事实。为此在 `ACatCharacter` 上开 `Multicast_PlayCosmeticEvent(FGameplayTag)`（Unreliable）+ `BP_PlayCosmeticEvent` 蓝图落点，由 `UCatFishingCommandComponent::HandleAbilityCommandFromAuthority` 在**裁决之前**广播；落地时跳过本地控制端（发起者已在 Ability 钩子里零延迟播过，重播会从头打断自己的 Montage）。
- **教训**：①判断一个表现该写在哪，先问"**这件事该不该被别人看到**"——该 → 挂在有复制状态的表现 Actor 事件上；只给自己的即时反馈 → Ability 钩子；②"动画不同步"几乎从来不该用"复制 Montage"来解决，而是找到那件事对应的**权威事实**，让每个客户端各自播；③**只有当一个动作失败时完全不改变任何权威状态**，才需要额外开多播通道——有状态可读却还加多播，同一动作会播两遍。

## 五、Actor 生命周期

### [2026-08-19] Hook（和鱼）会话结束后永久残留在水面
- **症状**：抛竿→收线后，钩子 Mesh/调试球留在河面上，越钓越多。
- **根因**：Hook/FishEncounter 只在**生成失败回滚**路径被 Destroy；正常终结走 `FinalizeSession`，它只销毁 Session 自己。
- **修复**：`ScheduleTerminalDestroy` 把 Hook 与 Fish 纳入与会话相同的终态复制窗 `SetLifeSpan`（3s 后统一消失，客户端能看完收尾表现）。
- **教训**：Actor 生命周期要按"谁生成谁负责终结的**全部**路径"检查——失败路径有清理不代表成功路径有。

## 六、工具链 / 流程（长期有效的规则）

- **反射变更必须重启 Editor**：新增/修改 UCLASS/UFUNCTION/UPROPERTY/枚举后热重载不可靠——蓝图里搜不到新事件、行为异常都先想到这条。
- **UDeveloperSettings 只在启动读 ini**：改 `Config/DefaultGame.ini` 或 Project Settings 里的 Config 项都要重启。
- **PIE 中保存资产静默失败**：见上文 No eligible fish 案例。
- **Python 驱动编辑器的坑**：强制 load TSoftObjectPtr 曾致崩溃（改读 ini 或预加载）；`Guid.import_text` 静默失败（用 `GuidLibrary.new_guid`）；非 UFUNCTION 的 C++ 函数（如 GetPawnViewLocation）Python 调不到；EditDefaultsOnly 结构体用 `import_text` 写。
- **已知无害噪音**：`FAppTime` 渲染线程 ensure 是引擎问题，忽略；`ABP_Cat` 的 Divide by zero Script Msg 来自动画蓝图（用户侧，待动画蓝图完善时顺手修）。
