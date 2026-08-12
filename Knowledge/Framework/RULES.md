# Catfishing 当前框架规则

更新时间：2026-08-12
文档状态：当前有效规则。
范围：给后续开发和 AI 审查使用，列出当前框架允许、禁止和必须保持的边界。本文不记录历史讨论。

## 目录归属规则

新增或迁移 C++ 类型时，先判断类型本身属于哪个系统，不按“服务谁”“挂在哪个 Actor 上”“当前谁调用它”归类。

- `Character/` 只放角色本体、身体生命周期和 `ACharacter` 直接职责。
- `AbilitySystem/` 放 `UGameplayAbility`、`UAttributeSet`、Ability 设置和能力相关诊断类。
- `Condition/` 放 Wet、Downed、Recovery 等离散身体状态。
- `Equipment/` 放装备定义、功能型装配、一局耗材、鱼竿耐久和失败预算。
- `Items/` 当前只放鱼实例、容器事务和容器复制适配；不要把所有“物品”都放进来。
- `Framework/Game/` 放 GameMode、GameState、PlayerState、PlayerController 等 UE 框架宿主。
- `UI/` 放 LocalPlayer UI 子系统、Widget、UI 设置和只读 View DTO。
- `Online/` 放 Session、旅行、邀请、联机策略与平台回调。
- 模块根目录只保留模块入口、Build 配置和极少数真正跨系统入口。

若一个 Actor 是世界设施，例如营地或鱼缸，应优先判断它的领域身份。它暴露容器并不自动属于 `Items/`；它是营地设施时应由 `Camp/` 拥有，再调用 Items 事务。

## 唯一写入者

每个运行时真相只能有一个写入者。

- Online 的 Session、World、Transport 和 Operation 事实只由 `UCatOnlineSubsystem` 写。
- Run 阶段、额度、ready、Host exit 等局事实只由 `ACatfishingGameModeBase` 写。
- GameState 只复制公开快照，不裁决玩法。
- PlayerState 只保存连接期身份、ready 和公开摘要，不保存 ASC、身体状态、物品或本地档案。
- Character 持有猫身体 ASC、Condition、Equipment 和个人鱼护出口；这些状态不搬到 PlayerState 或 Profile。
- FishingSession 持有单次钓鱼阶段、参与者和抢抄终态；Ability、鱼 Actor、UI 不能各自结算同一条鱼。
- ItemsService 是鱼实例和容器事务唯一写口；FastArray、Widget、StateTree Task 只能消费提交结果。
- ProfileSubsystem 是本地永久档案唯一写口；服务器不能声明远端 `USaveGame` 已经原子提交。

## 命令与幂等

共享、竞争、不可逆或可重试命令必须带稳定请求键。`RequestId` 负责幂等，`Revision` 负责拒绝陈旧视图，二者不能替代。

服务器入口必须输出结构化 Result。失败要区分权限、阶段、目标失效、版本冲突、取消、已结算、策略未裁和传输失败；不能只返回 `false` 让调用者猜。

终态缓存由真正的聚合持有。Run 命令在 GameMode，Fishing 起始和阶段命令在 FishingService/FishingSession，容器命令在 ItemsService，Profile Grant 在 ProfileSubsystem 或 RunImprintService 的投递记录中。UI 不持有终态缓存。

## 联机与旅行

Frontend、Lake 和 Online 的生命周期都必须经过 `UCatOnlineSubsystem`。

- Host Create 成功后，只有 Online 可以打开 Lake listen。
- Join 成功后，只有 Online 可以解析地址并 ClientTravel。
- Leave/Host exit 先经 Run teardown 或 DestroySession，再统一回 Frontend。
- PostLoadMap、TravelFailure、NetworkFailure 是收口事实；ServerTravel/ClientTravel 发出不等于成功。
- 不新增 Lobby Map、Seamless Travel、Host Migration 或 Widget 旁路旅行。

SteamSockets、NetDriverDefinitions、`bInitServerOnClient` 和双账号 Steam 回调顺序仍是专项验证项。当前代码不能把候选配置写成已定案。

## Character、GAS 与身体状态

`ACatCharacter` 同时作为 ASC Owner 和 Avatar。服务端负责授予 Ability、初始化属性和注册个人鱼护；拥有客户端只刷新 ActorInfo、输入映射和 UI 订阅。

`UCatSurvivalAttributeSet` 属于 `AbilitySystem/`，因为它表达 GAS 属性，不属于 `Character/`。`UCatConditionComponent` 属于 `Condition/`，因为它表达 Wet、Downed、Recovery 等离散状态。`UCatEquipmentComponent` 属于 `Equipment/`，因为它表达功能装配和一局耗材。

`UnPossessed`、`EndPlay` 和 Controller 变化必须先收口 Fishing/Social、移除自有 MappingContext、取消 Ability，再清 ActorInfo。`ClearActorInfo` 只清 ASC 缓存，不替代领域清理。

## Run 与献祭

Run 的阶段、额度和翻天由 `ACatfishingGameModeBase` 拥有。StateTree 只负责长流程拓扑；C++ 方法只执行阶段副作用、校验、提交和事件发送。

献祭外部入口是 `FCatSacrificeCommand`，由 `UCatSacrificeCoordinator` 协调。外部系统不能直接调用 Items 删除鱼并自行增加额度。协议顺序固定为：Items 预留、Run 预检、Items 不可逆 commit、Run apply。Items 已 commit 后不能回滚鱼，只能补 Run 额度或暴露失败。

## Fishing 与捕获

巨鱼协作只发生在搏斗阶段。近岸抢抄不是合作结算，首个合法抢抄者通过服务器 Compare-and-Commit 取得实物鱼。

鱼池筛选必须使用真实在场协作能力，而不是只看 PlayerArray 数量。有效参与者需要 Active Controller、当前 Character、未倒地、正 FishingStrength 和正 FightStamina。

失败预算一次只允许一种惩罚。丢特殊饵和伤竿不能同时发生。

## Items、Equipment 与 Profile

`Items` 当前的意思是“鱼实例与容器事务”，不是传统意义的泛道具系统。

- 实物鱼是 `FCatFishInstance`，任一时刻只属于一个容器或 escrow。
- 容器有 Revision，转移、捕获、吃鱼、偷鱼和献祭预留都走 `UCatItemsService`。
- 装备、草药、窝料、浮木、鱼竿耐久等由 `Equipment/` 管。
- 图鉴、印记、解锁和本地相册由 `Profile/` 与 `Collection/` 管。

不要创建万能 `Item` 基类。只有在多个领域确实共享同一交易/容器规则时，才抽小型 DTO 或事务语义。

## Collection、Imprint 与 Grant

捕获事务产生实物鱼和 `FCatCaptureCommittedResult`，不直接代表本地永久档案已写入。

`UCatRunImprintService` 消费 committed 事实，生成 `FCatProfileGrant` 或 `FCatCapturePlan`。CapturePlan 是本局成像投递记录，Grant 是永久授予内容，ACK 是客户端 durable 后的回执。三者 ID、状态和重试路径不能混用。

未 ACK Grant 不能被服务器伪装为已经写入玩家档案。Host 进程直接消失时，没有 Steam Cloud 或外部后端就无法保证未送达远端玩家的 Grant 恢复。

## Social 与 Camp

Social 只裁决求助、恶作剧、保护牌和偷鱼协议权限，不拥有身体救援状态，也不裁决印记候选。倒地、搬运、恢复属于 Character/Condition/Camp 链。

Camp 是固定营地，不是建造系统。营地提供休息、救援落点、鱼缸转移和篝火回看。篝火回看只建立表现和可选 CapturePlan，不写普通夜 ready。

## 配置 Gate

未裁策略必须 fail-closed，不能用默认值假装产品已定。

- Online SessionAccess、恢复、StableNetId 暴露等策略由 `UCatOnlineSettings` 集中 gate。
- Ability、Condition、Equipment、Fishing、Run、Social、Camp、Items、Profile、UI 等设置中的 0、None、Unset 通常表示未裁或未接线。
- Shipping 下的诊断或 provisional runtime 不能自动开启。

新增默认配置时必须说明它是产品裁决、验证临时值还是 fail-closed gate。不要把临时可运行默认写成最终设计。
