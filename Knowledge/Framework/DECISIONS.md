# Catfishing 当前框架决策

更新时间：2026-08-12
文档状态：当前有效决策。
范围：记录后续程序员和 AI 审查最容易“修错”的架构取舍。本文只写已经在当前框架中体现的决策。

## 单 Runtime 模块化单体

项目保持一个 `Catfishing` Runtime 模块，内部按领域目录分层。这样可以让 Run、Fishing、Items、Profile、Online 在同一局内共享清晰的 C++ 调用和生命周期，不提前制造跨模块 ABI、加载顺序和插件边界问题。

后续可以整理目录，但不应为了“看起来模块化”拆多个 Runtime 模块。

## 按系统归属分类文件

文件目录按类型本身的系统归属决定，不按消费者或挂载位置决定。`UCatSurvivalAttributeSet` 属于 `AbilitySystem/`，不是 `Character/`；`UCatConditionComponent` 属于 `Condition/`；`UCatEquipmentComponent` 属于 `Equipment/`。这是为了让后续 AI 审查能按职责检索，而不是被 Actor 组合关系带偏。

## Online 是唯一旅行和 Session 深模块

`UCatOnlineSubsystem` 同时维护 Session、World、Transport 和 ActiveOperation 四份事实。Create/Join/Leave/HostExit/失败补偿都在这里收口，UI 只发语义意图。

这样做的代价是 Online 类很深；收益是不会出现 Widget、Controller、GameMode 各自旅行，或 DestroySession 与 World 旅行互相冒充成功的情况。

## Character-owned ASC

ASC 放在 `ACatCharacter`，Character 同时作为 Owner 和 Avatar。PlayerState 不持有身体属性、Ability、Condition 或 Equipment。这样重连会生成新的身体，局内状态随身体和局释放，跨局档案仍由 Profile 保存。

这不是“谁方便复制谁就持有”的选择；它是为区分连接身份、猫身体和跨局档案而做的边界。

## Items 表示鱼实例与容器事务

当前 `Items/` 不是传统道具系统，也不是装备系统。它只拥有局内实物鱼、容器、转移、捕获、吃鱼、献祭预留和偷鱼 escrow。

装备、草药、窝料、浮木和鱼竿耐久由 `Equipment/` 拥有；图鉴、印记和解锁由 `Collection/` 与 `Profile/` 拥有。这个拆分避免建立万能 `Item` 基类，也避免让实物鱼、装备选择、本地永久记录混成一份状态。

## 献祭由 Run-owned 协调器跨 Items 和 Run

献祭不是简单 Items 操作，也不是纯 Run 操作。`UCatSacrificeCoordinator` 用单向协议协调：预留鱼、Run 预检、Items commit、Run apply。

Items commit 之后鱼已经不可逆消费，不能回滚；如果 Run apply 失败，只能重试补 Run 或暴露失败。这比伪装数据库事务更诚实，也让 Host teardown 能有明确收口点。

## CapturePlan 与 ProfileGrant 分离

CapturePlan 表示本局成像任务；ProfileGrant 表示永久授予内容；Grant ACK 表示客户端 durable 写入后回执。它们由 `UCatRunImprintService` 和 `UCatProfileSubsystem` 分开持有。

这个分离防止服务器把“已经生成成像任务”误说成“玩家本地相册已经保存”，也防止鱼被偷、献祭或局末清空后回滚已经授予的图鉴记录。

## 巨鱼协作只发生在搏斗阶段

旧“双人抄网”说法不再采用。当前规则是：巨鱼可在 HookedFight 阶段协作；NearShore 后由第一个合法抢抄者得到实物鱼。

实现中 `ACatFishingSession` 保留搏斗参与者用于候选/印记，但实物归属仍按抢抄提交者决定。

## Social 不拥有救援状态

Social 管求助、恶作剧、保护牌和偷鱼协议权限，不持有 Downed、Recovery、搬运或草药恢复状态。救援写入由 Camp、Condition、Character 和 Equipment 链完成。

这样能避免“社交系统看起来和玩家互动有关，所以把救援状态也塞进去”的误分类。

## Profile 是 LocalPlayer 本地 durable

`UCatProfileSubsystem` 是 LocalPlayer 子系统，拥有 `USaveGame`、Grant Journal、装备选择和本地相册隐藏状态。服务器只能投递 Grant、接收 ACK 和复制公开摘要，不能直接写远端玩家的本地档案。

当前没有 Steam Cloud 或外部后端，因此 Host 进程直接消失后的未送达 Grant 不能被保证恢复。

## 两张地图边界

当前地图只有 `Frontend` 和 `Lake`。Session 是联机会话，不是 Lobby Map。昼夜、营地、钓鱼、失败结算夜和篝火回看都发生在 Lake World 内，不通过地图旅行切换。

## Fail-closed 配置优先

未裁数值、未接资产、未验证策略和临时诊断能力默认关闭或拒绝。这样会让早期原型显得保守，但能防止占位 DataAsset、空配置或测试入口在多人运行中产生“看似成功”的假事实。
