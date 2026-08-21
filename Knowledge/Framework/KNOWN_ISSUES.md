# Catfishing 当前框架常见误读

更新时间：2026-08-12
文档状态：当前有效误读清单。
范围：记录后续代码审查最容易误判的地方，以及应该从哪里开始核查。

## 旧文档仍有空模板描述

现象：有些文档仍写着工程是空模板、类名是拟定名。
误读：据此判断当前源码没有 A-G 框架，或把已存在的类型当作计划名。
事实：当前 `Source/Catfishing` 已有 Online、Framework/Game、Character、AbilitySystem、Condition、Equipment、Run、Environment、Fishing、Items、Collection、Profile、Social、Camp、Data、UI 等真实代码。工程事实以当前源码和最新构建证据为准。

## Items 不是泛道具系统

现象：看到 `Items/` 就把装备、草药、窝料、浮木、鱼竿、解锁都往里放。
误读：把 `Items` 当成传统 RPG item hierarchy。
事实：当前 `Items/` 是鱼实例和容器事务系统。功能装备与一局耗材在 `Equipment/`，永久解锁和相册在 `Profile/`，捕获/印记授予在 `Collection/`。

## 文件分类不能按挂载位置

现象：组件挂在 `ACatCharacter` 上，就想放进 `Character/`。
误读：把 Actor 组合关系当成目录归属。
事实：AttributeSet 属于 `AbilitySystem/`，Condition 属于 `Condition/`，Equipment 属于 `Equipment/`。`Character/` 只放角色本体和身体生命周期。

## ServerTravel 或 ClientTravel 返回不等于成功

现象：看到 Travel API 调用成功，就认为地图已经到达。
误读：把旅行提交当成旅行完成。
事实：`UCatOnlineSubsystem` 只有在 `HandlePostLoadMap`、`HandleTravelFailure` 或 `HandleNetworkFailure` 收口后，才更新 World/Transport/Operation 终态。

## DestroySession 不代表回到 Frontend

现象：DestroySession 回调成功后就结束 Leave。
误读：把平台 Session 清理当成 World 生命周期完成。
事实：Host/Client Leave 还必须由 `BeginTravelToFrontend` 和 PostLoadMap 收口；Run teardown、远端 ACK 和 Grant ACK 也可能参与 Host exit。

## FastArray 不是权威写口

现象：看到容器复制组件就想直接改数组。
误读：把网络复制结构当成库存数据模型。
事实：容器真相在 `UCatItemsService`。`UCatContainerReplicationComponent` 只发布已提交的 `FCatContainerSnapshot`。

## CapturePlan、Grant 和 ACK 不同

现象：成像计划生成后就认为玩家拿到了永久印记。
误读：把 CapturePlan 投递、ProfileGrant 授予和客户端 durable ACK 混成一条状态。
事实：`FCatCapturePlan` 只活在本局投递；`FCatProfileGrant` 是不可变授予内容；`FCatGrantDeliveryRecord` 等 ACK 表示客户端落盘后回执。

## 巨鱼参与者不等于实物归属者

现象：多人参与搏斗后，认为所有协作者共同拥有鱼或共同抄网。
误读：复活旧“双人抄网”说法。
事实：协作只影响搏斗阶段和候选/印记。NearShore 抢抄由首个合法提交者得到实物鱼。

## Social 不裁决身体救援

现象：求助和救援都像“玩家互动”，所以把恢复状态放进 Social。
误读：Social 管所有玩家互助。
事实：Social 管权限、广播、偷鱼和保护牌。身体恢复由 Camp、Condition、Character、Equipment 链写入。

## PlayerState 不是身体和档案仓库

现象：想把 ASC、物品、Profile 或装备解锁放进 PlayerState，方便复制。
误读：把“跟玩家有关”当成 PlayerState 所有权。
事实：PlayerState 当前只保存连接期公开事实。身体随 Character，实物随 Items/Run，永久档案随 LocalPlayer Profile。

## 诊断 Ability 和临时 Gate 不是最终玩法

现象：看到 `UCatStageCTestAbility`、provisional 配置或默认 0/None，就当成产品默认。
误读：把早期框架验证入口当成正式数值/技能。
事实：这些入口用于证明 ASC、输入、UI 和复制链路；正式 Ability、数值、UI 资产、InputAction 和 DataAsset 仍需后续内容接线。

## Steam 只完成单机初始化验证

现象：看到 AppId 480、SteamSockets 配置和 `Client/Game Server API initialized 1` 就认为 Steam 联机已完整通过。
误读：把单进程初始化成功当成双账号建房、搜索、加入和真实传输证据。
事实：本机 Win64 游戏模式已确认 OSS Steam 初始化成功；SteamSockets 实际监听/连接驱动、Host/Find/Join/Leave 回调顺序和双账号传输仍是专项验证项。

## 共享鱼缸不是跨局仓库

现象：共享鱼缸看起来像储物箱，于是保存到 Profile 或跨局保留。
误读：把局内容器当成永久仓库。
事实：鱼护和共享鱼缸都属于局内容器，局末清空。跨局只保留图鉴、印记、装备选择和已裁授予。
