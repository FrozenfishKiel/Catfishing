# FRAMEWORK_WIRING

更新时间：2026-08-11
范围：Catfishing 当前有效的 UE 框架接线规则。
解决问题：明确每类真相由谁持有、命令如何结算、跨局数据如何落盘，以及 UI、GAS、StateTree 与联机生命周期如何接线。
工程状态：工程仍为空模板；本文中的类名均为**拟定名**，只定义职责边界，不表示类型或资产已存在。

## 使用边界

本文是长期检索用的当前规则，不记录会议过程、Agent 工作流或已撤回方案。产品规则以 GDD 为准；本文件仅把已确认的规则投射到 UE 对象和运行链。

## 真相与宿主矩阵

| 真相或职责 | 拟定宿主 | 写入边界 | 生命周期 |
|---|---|---|---|
| Steam 身份、邀请、创建/加入/退出 Session | `UCatOnlineSubsystem` | 只适配平台与 UE Session，不裁决玩法 | `UGameInstance` |
| 本地档案、图鉴、印记索引、解锁 | 拥有者客户端的 `UCatProfileSubsystem` + `USaveGame` | 只按 GrantId 把不可变 `FProfileGrant` 写入 `FPendingGrantJournalEntry`（均拟定），再回执和合并；不接受 CapturePlanId，服务器不能写远端档案 | 进程/本地玩家 |
| 局规则、日夜、额度、入夜、翻天、局末 | `ACatfishingGameMode` + `ST_RunFlow` | 仅服务器裁决 | 一局服务器 |
| 献祭跨聚合协调与首次终态缓存 | GameMode/Run-owned `USacrificeCoordinator`（拟定） | 外部只接收 `SacrificeCommand`；Items 与 Run 是内部参与者，协调器不持有第二份鱼或额度真相 | 一局服务器 |
| 当前天、阶段、天气、额度、公共事件 | `ACatfishingGameState` | 服务器写、复制给客户端只读 | 一局复制 |
| 稳定身份、连接/在场、翻天确认、重连索引 | `ACatfishingPlayerState` | 不持有猫身体、ASC 或物品真相 | 连接期 |
| 猫身体、移动、动作、Attribute、Effect、Tag | `ACatCharacter` + Character-owned ASC | 服务器权威；拥有客户端只做合法预测/表现 | 单一猫身体 |
| 玩家输入、镜头、本地 HUD 路由 | `ACatfishingPlayerController` | 转发意图；不裁决玩法 | 控制期 |
| 单次钓鱼共享阶段与唯一结算键 | `AFishingSession` + `ST_FishingSession` | 服务器唯一推进与结算 | 单次钓鱼 |
| 鱼行为、鱼群、环境交互 | `AFishActor` / 服务器世界 Actor | 服务器模拟；Hooked 后交由钓鱼会话 | 世界/局内 |
| 鱼护、共享鱼缸、祭坛输入 | 容器 Aggregate / `AItemContainerActor` | 仅 Inventory 事务可改写 | 局内 |
| 一局印记候选、裁决、待分发项 | Collection/Imprint 下的 `URunImprintService` | 只消费已提交 Result/不可变 DTO，接收候选、去重、裁决；以 `FImprintCaptureDeliveryRecord`（拟定）跟踪 CapturePlan 成像投递，不依赖 Social/Fishing/Run 实现，不写客户端文件 | 一局服务器 |
| 断线恢复白名单与待补发结果 | `FRunPlayerRecord`（GameMode 注册表） | 同一 StableNetId 不得同时有两份在线真相 | 断线间隙 |
| UMG 根节点、页面与输入焦点 | `UCatUIManagerSubsystem` | 只装配 MVC；不持有玩法真相 | `ULocalPlayer` |

### 核心归属

- ASC 固定由 `ACatCharacter` 持有，Character 同时是 Owner 与 Avatar。动作、饥饿、疲惫、中毒、淋湿、倒地及动作许可都属于项目唯一猫身体。
- `ACatfishingPlayerState` 只保留身份和连接期事实：Stable Net ID、在场状态、翻天票与重连索引。
- 图鉴、印记与解锁跟人走；猫状态、局内物资、鱼护/鱼缸和祭坛进度跟局走。实物鱼可转移或消失，已记录图鉴不可因实物变化回滚。

## 统一执行链

```text
Enhanced Input / UMG View
  -> PlayerController / MVC Controller
  -> 服务器校验身份、权限、阶段、输入上下文
  -> StateTree 编排长流程，或领域模块直接执行短事务
  -> 唯一真相持有者一次提交结构化 Result
  -> GameState / PlayerState / Character ASC / 领域 Actor 复制公开结果
  -> Gameplay Cue、AnimBP、音效与 MVC View 消费
  -> 服务器按 CapturePlanId 投递印记计划，并以 `FImprintCaptureDeliveryRecord`（拟定）跟踪捕获完成、失败或重试
  -> CapturePlan 捕获成功后，Collection/Imprint 才生成带 GrantId 的不可变 `FProfileGrant`（拟定）
  -> 客户端 ProfileSubsystem 按 GrantId 幂等落入 `FPendingGrantJournalEntry`（拟定），再回执服务器 `FGrantDeliveryRecord`（拟定）并合并跨局记录
```

场景交互与 UI 交互必须汇入同一领域命令。叼鱼到祭坛和在祭坛页面献祭都只能提交 Run-owned `SacrificeCommand`（拟定）；GameMode/Run-owned `USacrificeCoordinator` 再让 Items 与 Run 作为内部参与者完成可恢复协议。Widget、Ability、StateTree Task、世界 Actor 与 PlayerController 均不得直接调用 Items Sacrifice、修改容器数组或增加额度。

### 命令结果约定

所有服务器领域入口返回结构化 Result，而不是仅返回布尔值。

- 成功 Result 至少表达提交的领域版本、受影响对象和对表现层可公开的结果。
- 失败 Result 必须区分权限、阶段、目标失效、版本冲突、取消和已结算，调用者不能用“失败”猜原因。
- 幂等入口应接受或推导稳定请求键；同键重放只返回首次终态，不重复生成鱼、图鉴或印记。
- Result 是复制与表现的消费边界，不是第二份可写领域状态。

### 服务器校验顺序

1. 定位 StableNetId、PlayerState 与当前 Character，确认身份没有被重连替换。
2. 校验玩家在场、权限、局阶段、距离或交互上下文。
3. 校验领域对象仍有效、会话仍处于允许阶段、容器版本或结算键未被提交。
4. 执行单一事务并产出终态 Result。
5. 发布复制状态、Gameplay Event 或待持久化事件；任何后续失败不得倒写已提交的唯一真相。

## 局与地图

- 地图只有 `Frontend` 与 `Lake`。Session 是联机会话，不是一张地图，也不设准备房需求。
- 创建或加入 Session 后直接进入 `Lake`；河流、森林湖和营地属于同一湖畔 Gameplay World。
- 白天、夜晚、篝火回看与翻天都在同一局内进行；昼夜不是旅行边界，不做无缝旅行。
- `ST_RunFlow` 在服务器编排开局、白天、额度判定、入夜、翻天、失败/毕业结算。客户端只消费 `GameState` 公开快照。
- 夜晚无倒计时。翻天由在场玩家确认；离开的玩家不阻塞在场玩家的决定。

### 局内清理边界

- 局失败、毕业或房主退出都进入局末收口；局内物资、猫状态、鱼缸/鱼护内容和世界 Actor 随局释放。
- 局末对在线玩家分别收口两类本局投递：按 CapturePlanId 处理 `FImprintCaptureDeliveryRecord` 的完成/失败/重试，按 GrantId 对未 ACK `FGrantDeliveryRecord` 做有界重发与回执收集。CapturePlan 状态不是永久授予 ACK；服务器不能宣称远端图鉴、印记或解锁已经落盘。
- 前端不保存 Lake Actor 的引用；返回 Frontend 后只显示 Profile 与 Online 的进程级信息。

## 钓鱼会话与原子结算

`AFishingSession` 是一次钓鱼的服务器权威宿主。它持有钓手、目标鱼、协作者、阶段、终止原因和唯一结算键；会话负责从抛竿到捕获或失败的共享长流程。

捕获必须通过一次幂等事务完成：

1. 校验会话阶段、参与者权限、目标鱼与结算键。
2. 首个合法抢抄请求在服务器 Compare-and-Commit 中只原子关闭会话、写入鱼实例归属/容器真相，并生成不可变 `CaptureCommittedResult`（拟定，或等价 committed DTO）与印记候选；Collection/Imprint 再按该已提交事实幂等归约为 `FProfileGrant`，捕获事务既不直接生成 Grant，也不包含远端磁盘写入。
3. 同键后的重复请求不再修改任何真相，返回 `AlreadyResolved`。
4. 每个 `FProfileGrant` 使用稳定唯一 Grant ID，表达不可因实物鱼后续去向而撤销的图鉴授予；它不包含可写 ACK 状态。服务器用本局 `FGrantDeliveryRecord` 保存投递/ACK，客户端持久化后才成为本地永久记录；重复投递按 Grant ID 返回既有处理结果。

近岸抢抄的归属规则是第一个合法成功者取得鱼。Fish Actor、钓手 Ability、协作者、UI 与动画只能提出请求或消费结果，不能并行推进捕获结算。

### 会话中断

- 主动取消、钓手离开、关键对象失效、断线或局末都进入 `AFishingSession` 的明确终止分支。
- 终止分支必须释放鱼、参与者和 Ability/Task 关联，避免旧引用对下一会话生效。
- 已提交捕获不会因会话清理撤销；未提交的会话不得补造捕获 Result。
- 重连只恢复允许的玩家状态，不恢复旧钓鱼会话的中间阶段。

## 鱼流转与容器事务

- 每条局内实物鱼是唯一 Item Instance；任一时刻只属于一个合法容器或世界状态。
- Capture、Transfer、Steal、Consume 走 Items/Inventory 事务，带容器版本或等价并发校验。献祭的唯一外部入口是 Run-owned `SacrificeCommand`；Items 的预留/消费与 Run 的额度提交仅由 `USacrificeCoordinator` 在内部调用。
- 事务先校验、再一次提交源/目标变化，输出结构化 Result；禁止“先删后加”、直接改复制数组或让多个容器同时拥有同一鱼。
- 全队共享鱼缸可跨天留存、局末清空；它是展示、储备与可偷取的局内物体，不是跨局仓库。

### 容器读写规则

- UI 可读取容器的复制快照和变化事件，但不持有可编辑副本。
- 祭坛、鱼护、共享鱼缸和世界拾取物之间的流转都经同一事务边界，不能为某个 UI 页面设置旁路。
- 偷取是 Items 短事务。献祭是 Run-owned 应用命令协调的跨聚合短协议：Items 已 committed 后只允许补 Run 额度，不允许回滚鱼；它不由 StateTree 承担，也不向 UI、Ability 或世界交互暴露 Items Sacrifice 旁路。
- 容器的展示顺序、容量和美术表现可独立迭代，不得改变唯一 Item Instance 的所有权判定。

## 猫状态与救援

- 猫的 Attribute、Effect、Tag 与动作互斥由 Character-owned ASC 管理；属性和状态随局清空，不写入跨局档案。
- 疲惫可在营地休息点恢复；倒地的猫被救援后送回营地允许落点。
- 环境的淋湿等表现不能绕过身体状态规则直接变更内部字段。任何救援、吃鱼、解毒或状态改变须由 Ability 或明确领域命令校验后写入。

### 动作与状态边界

- 输入触发 Ability；Ability 使用 Tag/Effect 声明动作许可、阻塞、取消和表现，不由 UI 或 AnimBP 维护另一套开关。
- 世界交互在执行前读取 Character 的当前许可状态，不能假定玩家一定拥有动作能力。
- 救援和休息改变的是局内猫身体状态；它们不替代局流程、物品事务或永久档案。

## 印记与 Profile

- 各玩法仅提交语义化印记候选；Collection/Imprint 下的 `URunImprintService` 只消费已提交 Result/不可变 DTO，统一去重、裁决参与者并生成 Capture Plan。Social 只负责权限、偷取意图与恶作剧策略，不裁决印记候选。
- 共同印记以同一照片、同一帧为目标；服务器以 CapturePlanId 键控的 `FImprintCaptureDeliveryRecord`（拟定）保存计划投递、客户端捕获完成/失败和重试状态。它只活到本局结束，不保存永久授予 ACK。
- 图鉴记录、印记相册和解锁属于拥有者客户端本地 Profile 的跨局事实；鱼被偷、献祭或局末清理不得回滚已经持久化的图鉴记录。
- `FProfileGrant`（拟定）是不可变授予内容，不含 ACK；只有 CapturePlan 捕获成功 Result 到达 Collection/Imprint 后，才为印记生成新的 GrantId。到达客户端后，`ProfileSubsystem` 只按 GrantId 幂等写入版本化 `USaveGame` 的 `FPendingGrantJournalEntry`（拟定），再发送回执并合并到图鉴、印记或解锁。Grant ACK 只更新同 GrantId 的服务器 `FGrantDeliveryRecord`（拟定），不更新 `FImprintCaptureDeliveryRecord`；任何 Widget 或局内 Actor 均不得直接读写文件。

### 跨局提交时机

- 钓获结算成功时，捕获事务只生成 `CaptureCommittedResult`（拟定）与印记候选；Collection/Imprint 依据已提交结果幂等生成图鉴 `FProfileGrant`。图鉴授予不等待实物鱼是否继续留在容器，客户端持久化后成为不可因实物去向而撤销的本地记录。
- 共同印记的参与者由服务端裁决；客户端按 CapturePlan 本地成像并回报捕获 Result。只有成功 Result 才由 Collection/Imprint 生成印记 `FProfileGrant`；失败或待重试只更新 `FImprintCaptureDeliveryRecord`，不得伪装成已授予。
- 重连分别读取服务器本局记录：按 CapturePlanId 恢复 `FImprintCaptureDeliveryRecord` 的计划/重试，按 GrantId 重发 `FGrantDeliveryRecord` 对应的 `FProfileGrant`。不得通过任一重发重新触发玩法、重建鱼实例或改变局内状态。

## UMG MVC

- View 是 `UUserWidget` / Widget Blueprint，只负责布局、局部动画、控件反馈和显示。
- 页面 Controller 把交互解释为语义意图，调用 Ability、本地命令或服务器请求；它不保存玩法真相。
- Model 是领域对象提供的只读查询与变化通知。仅在复杂展示组合多个来源时增加专用读取结构，不默认创建第二套 ViewModel 真相。
- `UCatUIManagerSubsystem` 负责根 UI、页面、输入模式、焦点与 World 生命周期配对。地图旅行或 Controller 替换时先解绑旧引用，再装配新 World。

### UI 读取与刷新

- View 从只读查询、复制更新或正式变化事件刷新；默认不以 Tick 轮询领域状态。
- 需要即时反馈的交互可展示 Pending 状态，最终以服务器复制 Result 收敛。
- UI 的本地缓存只能服务展示和请求关联；缓存失效时重新读取权威公开状态，不能覆盖服务器结果。

## GAS 与 StateTree

GAS 管单猫动作、Attribute、Gameplay Effect、Gameplay Tag、取消/互斥和表现触发。它不承担整局流程、多人钓鱼会话总图、鱼 AI 总流程或物品事务总账。

StateTree 仅用于服务器长生命周期流程：

- `ST_RunFlow`：局、天、额度、入夜、翻天和结算。
- `ST_FishingSession`：单次钓鱼会话的共享阶段与中断收口。
- `ST_FishBehavior`：必要的鱼行为层级状态。

短事务不使用 StateTree：印记候选、偷取、容器转移和最终捕获结算直接由对应领域事务提交；献祭由 Run-owned `SacrificeCommand` 进入 `USacrificeCoordinator` 的短协议。StateTree Task 可发出 Gameplay Event 或请求 Ability，并等待成功、失败、取消或结构化 Result；它不重复实现 Attribute、Effect、Tag 或客户端预测。

### 长流程进入与退出

- `ST_RunFlow` 持有局阶段，不持有玩家身体 Attribute 或容器内容的可写副本。
- `ST_FishingSession` 持有会话阶段与参与关系，不持有跨局档案真相。
- `ST_FishBehavior` 只负责鱼在世界中的必要行为；Hooked 后避免与 FishingSession 双重推进同一条鱼。
- 每个长流程都要有取消、失效和局末终止入口；终止后不再接受旧事件推进状态。

## 生命周期与重连

### 启动与入局

1. GameInstance 创建 Online 与 Profile Subsystem，并完成 Profile 加载。
2. Frontend 的 LocalPlayer UI Manager 装配根 UI；组局入口调用 Online Subsystem。
3. 房主以 Listen Server 打开 Lake，GameMode 创建局服务并启动 `ST_RunFlow`。
4. 玩家加入时先建立 StableNetId 对应的 PlayerState 与在场记录，再生成 Character。
5. Character 在服务端和拥有客户端初始化 ASC、授予基础 Ability；随后绑定 Enhanced Input 与 UI 订阅。

### 普通玩家重连

恢复顺序固定为：**StableNetId → PlayerState → Character/ASC → 恢复允许状态 → 按 CapturePlanId 恢复本局成像投递 → 按 GrantId 重发未 ACK 授予**。

- 服务器按 StableNetId 查找限时 `FRunPlayerRecord`，拒绝同身份同时占用两份在线真相。
- 只恢复白名单中的局内身体状态和容器关联；具体白名单与保留时长待实现验证。
- 进行中的 `AFishingSession` 在掉线时按中断规则结束，不原地续接半场搏斗，也不得形成重复结算。

### 离局与局末

- 先解绑 UI 的 World、Controller、Model 与 Delegate，并取消本地 Pending 交互。
- 服务器停止新命令；进行中的请求必须一次提交或以终态失败返回，不能留下半事务。
- 服务器对在线玩家先按 CapturePlanId 有界收口 `FImprintCaptureDeliveryRecord`，再按 GrantId 对未 ACK `FGrantDeliveryRecord` 做最终重发与回执收集；前者不是永久授予 ACK，后者也不代表远端本地存档已经完成。
- Run teardown 前，`USacrificeCoordinator` 必须取消未提交预留，并把 Items 已 committed 的献祭补到 Run 终态；已 committed 的鱼不允许回滚。随后终止 FishingSession、鱼行为和 RunFlow，清空局内容器和猫状态；销毁 Session 并回到 Frontend，释放 Lake Actor 引用、重连记录、`FImprintCaptureDeliveryRecord` 与 `FGrantDeliveryRecord`。协调器依附一局服务器而非玩家；玩家掉线不会销毁它，但 Host 进程消失且无外部后端时无法恢复内存中的协议进度。
- 若授予尚未送达或尚未写入客户端待处理日志，远端玩家掉线后房主又结束本局，在没有 Steam Cloud 或外部后端的当前范围内无法保证恢复；这是已接受的本地存档边界，不引入跨主机授权账本。

### 监听服务器边界

- 房主运行 Listen Server，仍以服务器逻辑为唯一玩法裁决源；房主本地玩家不因同机而绕过校验。
- 普通玩家可中途加入，加入流程先完成身份与身体装配，再消费公开局状态。
- 房主退出结束本局；当前不设计主机迁移。

## 拟定接线入口

以下是后续框架实现的建议检索入口，不是已存在的 API。

| 领域 | 拟定入口 | 调用者 | 输出 |
|---|---|---|---|
| Online | `CreateSession`、`JoinSession`、`LeaveSession` | Frontend MVC Controller | 会话 Result / Lake 打开结果 |
| GameFlow | `StartRun`、`SubmitDayReady`、`ResolveQuota` | GameMode、RunFlow Task | 阶段转换 Result |
| Sacrifice | `SacrificeCommand` | 场景交互、MVC Controller、Ability 只提交命令；GameMode/Run 接收 | 协调后的完整终态 Result |
| Character | `TryActivateAbility`、`ApplyStateChange` | PlayerController、领域命令 | Ability / 状态 Result |
| Fishing | `StartFishing`、`JoinFishing`、`AttemptCapture`、`CancelFishing` | Character Ability、交互命令 | Session / Capture Result |
| Inventory | `Capture`、`Transfer`、`Steal`、`Consume` | Fishing、世界交互、UI Controller | 事务 Result；献祭预留/消费仅供 `USacrificeCoordinator` 内部调用 |
| Imprint | `SubmitCandidate`、`ReportCaptureResult` | 领域 Result 消费者、客户端成像结果 | CapturePlan / `FImprintCaptureDeliveryRecord` 状态；无永久授予 ACK |
| Profile | `ApplyProfileGrant`、`AcknowledgeGrant`、`LoadProfile`、`CommitProfile` | Profile Subsystem | GrantId 键控的档案读取/保存与 `FGrantDeliveryRecord` ACK Result |

入口命名可随源码风格调整，但职责不得漂移。任何新增玩法应先判断它属于现有命令、长流程、状态变化或 Profile 提交中的哪一类，再选择入口。

## 领域事件边界

- Gameplay Event 用于连接 GAS 与 StateTree 或领域行为，不用作无约束的全局消息总线。
- 事件载荷必须能定位其来源会话、目标对象或请求键；不能仅凭 UI 文本或临时 Actor 指针恢复真相。
- 事件可触发表现或流程转换，不能绕过事务写入物品、图鉴或印记。
- 事件到达迟到、重复或目标已失效时，接收端按当前阶段与幂等键拒绝或返回既有终态。

## 复制与表现边界

- `GameState` 复制全局公开局快照，`PlayerState` 复制连接期公开事实，Character ASC 复制身体相关玩法状态。
- `AFishingSession` 与容器只复制客户端显示、交互提示和结果收敛所需的公开状态；敏感或无关的服务器内部数据不作为 UI 真相。
- Gameplay Cue、动画、音效和特效从公开结果或 GAS 状态派生表现，不回写领域数据。
- AnimBP 是表现消费者，不裁决鱼行为、捕获成功、倒地或容器流转。

## 新功能接线检查

新玩法接入时先按以下问题定位：

1. 它的唯一可写真相属于局、猫身体、会话、容器、世界 Actor 还是本地 Profile？
2. 它是需要中断和等待的服务器长流程，还是可立即提交的短事务？
3. 它是否需要 GAS 的动作、Attribute、Effect、Tag 或预测？
4. 它的最终 Result 是否可能被重放、并发或掉线后补发？
5. UI、场景交互和自动流程是否会汇入同一命令入口？
6. 结束、掉线、局末和地图卸载时，哪些引用、Pending 状态和局内数据必须释放？

若无法回答上述问题，不应先增加 Widget 内逻辑、临时复制字段或独立状态机；先补齐宿主和 Result 边界。

## 事实等级

- **GDD / 用户确认事实**：玩法和产品边界，实施不得擅自改写。
- **当前架构决定**：为落实事实而选定的宿主、执行链与禁止路径；后续改动需同时更新本文件。
- **待 UE 5.8 实装验证**：尚未被项目原型证明的引擎接缝，只能作为验证假设，不能写成已通过结论。

工程仍为空模板时，优先建立最小纵向链路验证这些边界：入局、Character ASC 初始化、单次钓鱼唯一结算、容器复制、重连补发与 Profile 读取。验证结论应回填到对应实现或验收资料，而非在本文复制临时日志。

维护本文件时，只在宿主、写入边界、生命周期或禁止路径改变时更新内容。
数值、鱼种清单、UI 视觉和临时调试步骤应保留在各自领域资料或实现资料中。

新增持久化字段前，先确认它确实属于跨局事实。
新增复制字段前，先确认客户端必须公开消费它。
新增 StateTree 前，先确认该逻辑不是短事务。
新增 Ability 前，先确认它确实属于单猫动作或效果。

新建容器前，先确定唯一 Item Instance 的归属与转移事务。
新建页面前，先确定其 Model 的只读来源与页面 Controller 的命令出口。
新增重连字段前，先确定它是否处于允许恢复白名单。
新增印记入口前，先确定它提交的是候选而非客户端文件。
新增世界 Actor 前，先确定其销毁时机是会话结束、局末还是 World 卸载。

## 禁止路径

- 不把 ASC、身体 Attribute、Effect、物品容器放入 PlayerState。
- 不创建 Lobby Map、准备房流程、Lobby → Lake 无缝旅行，或按昼夜/区域旅行。
- 不让客户端运行权威 StateTree，也不让 UI 直接推动天数、额度或物品真相。
- 不让长生命周期 StateTree 取代短事务；不让 GAS 承担多人会话或局流程总图。
- 不让 Ability、鱼 Actor、钓手、协作者和 UI 各自结算同一条鱼。
- 不让 UI、世界交互、Ability、PlayerController 或 StateTree Task 直调 Items Sacrifice；它们只能提交 Run-owned `SacrificeCommand`。
- 不让任意系统直接修改容器数组、Widget 直接解锁图鉴或写档、鱼被偷后回滚永久记录。
- 不在重连时恢复已中断的钓鱼会话；不允许同一身份同时拥有两份在线真相。
- 不把 Session 生命周期、Profile 生命周期和 Lake 世界生命周期混为同一状态容器。
- 不以“客户端已经显示成功”作为物品、图鉴或印记已提交的依据。
- 不把服务器捕获事务与远端 `USaveGame` 写入描述为同一原子提交，也不宣称局末服务器已完成远端存档。
- 不引入跨主机授权账本来掩盖本地存档在房主结束后的未送达风险。

## 待 UE 5.8 实装验证

1. Character 同时作为 ASC Owner/Avatar 时，服务端与拥有客户端的初始化、ActorInfo 刷新与释放回调。
2. StateTree Task 通过 Gameplay Event 驱动 Ability 并等待 Result 的线程和生命周期边界。
3. Fast Array 或等价容器复制在同帧抢抄、偷取、献祭时的版本校验、去重与客户端收敛。
4. Listen Server 下 Steam Stable Net ID、邀请、中途加入、掉线与重连回调的真实顺序。
5. 共同印记的同帧成像与重连补发：以 `FImprintCaptureDeliveryRecord` 验证 CapturePlan 的完成/失败/重试；只有成功 Result 生成独立 GrantId，`FGrantDeliveryRecord` 只追踪该 Grant 的投递/ACK。
6. UMG MVC 与 Enhanced Input Mapping Context、鼠标捕获和焦点恢复的实际配对。

## GDD 来源清单

- `Knowledge/GDD/局与进程.md`：白天/夜晚/翻天、局末清理、跨局归属。
- `Knowledge/GDD/环境与氛围.md`：河流、森林湖和营地同属湖畔主场景；日夜和事件边界。
- `Knowledge/GDD/钓鱼系统.md`：抛竿、搏斗、协作、近岸抢抄与捕获链路。
- `Knowledge/GDD/鱼类图鉴.md`：实物鱼与永久图鉴记录分离。
- `Knowledge/GDD/猫咪与状态.md`：猫身体状态、倒地、救援与恢复边界。
- `Knowledge/GDD/联机社交.md`：创建、邀请、中途加入、退出与轻量联机要求。
- `Knowledge/GDD/印记图鉴.md`：候选、裁决、共同印记、分发与跨局相册。
- `Knowledge/GDD/营地.md`：鱼缸、休息点、篝火回看与局内物资边界。
- `Knowledge/Development/PROJECT_RESEARCH.md:42-57,77-80`：用户确认的项目约束与冲突裁决。
- `Knowledge/Development/PROJECT_RESEARCH.md:31-40`：调研同时确认 UE 工程仍为空模板，并列出工程、配置、资产与产品约束的核查来源。
