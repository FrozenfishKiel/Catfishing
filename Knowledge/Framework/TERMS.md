# Catfishing 当前框架术语

更新时间：2026-08-12
文档状态：当前有效术语表。
范围：稳定后续沟通和 AI 检索用词；只收录本项目容易混淆的概念。

## 地图与进程

**Frontend**：前台菜单地图，承载 LocalPlayer UI 和 Online 入口，不生成玩法 Character。
_Avoid_: Lobby、准备房。

**Lake**：一局玩法地图，承载 Run、Fishing、Camp、Items、Social 和 Character。昼夜、营地和结算都在这里发生。
_Avoid_: 白天地图、夜晚地图、营地地图。

**Session**：UE/Steam 联机会话，由 `UCatOnlineSubsystem` 适配。它不是地图，也不是局内 Run 状态。
_Avoid_: 房间地图、Lobby World。

## 身份与玩家

**StableNetId**：服务器从 `APlayerState::UniqueId` 派生的私有身份键，用于准入、幂等和重连。日志默认不公开原始值。
_Avoid_: SteamId 明文、玩家名、ControllerId。

**Admission Record**：GameMode 内存中的 Reserved/Active 准入记录，用来防止同身份同时占用两份在线真相。
_Avoid_: 登录缓存、玩家档案。

**PlayerState**：连接期公开事实宿主，保存 ready 和公开图鉴摘要。它不是身体状态或本地档案宿主。
_Avoid_: 玩家所有数据、角色数据。

## 局与身体

**Run**：一次 Lake 局的阶段、额度、夜晚 ready、Host exit 等服务器流程。
_Avoid_: MatchState、地图生命周期。

**Run Public State**：GameState 复制的公开局快照，客户端只读。
_Avoid_: 客户端 Run 真相。

**Cat Character**：局内猫身体 Actor，同时是 ASC Owner/Avatar，持有身体组件和个人鱼护出口。
_Avoid_: 玩家档案、PlayerState 身体。

**Condition**：Wet、Downed、Recovery 等离散身体状态，由 `UCatConditionComponent` 持有。
_Avoid_: ASC 属性、社交状态。

**Survival Attribute**：Hunger、Fatigue、Poison、FishingStrength、FightStamina 等 GAS 属性，由 `UCatSurvivalAttributeSet` 持有。
_Avoid_: Character 普通字段、PlayerState 属性。

## 物件、鱼与容器

**Equipment**：功能型装配和一局耗材，包括竿、饵、漂、窝料、草药、浮木和耐久。
_Avoid_: Items、永久解锁。

**Items**：鱼实例与容器事务系统，不是泛道具系统。
_Avoid_: Item 基类、装备系统。

**Fish Instance**：局内唯一实物鱼，包含实例 ID、鱼种 ID、重量和容器归属。
_Avoid_: 鱼种、图鉴记录。

**Container**：鱼护、共享鱼缸等局内容器聚合，带 Revision。
_Avoid_: 背包、跨局仓库。

**Escrow**：偷鱼或献祭过程中被服务器临时锁定的鱼事实。
_Avoid_: 客户端暂存、复制数组副本。

## 钓鱼与捕获

**Fishing Session**：一次钓鱼长流程 Actor，持有阶段、参与者、鱼运行态和抢抄终态。
_Avoid_: Ability 状态、UI 状态。

**HookedFight**：搏斗阶段，巨鱼可以登记协作者。
_Avoid_: 近岸协作抄网。

**NearShore Scoop**：近岸抢抄提交。首个合法成功者取得实物鱼。
_Avoid_: 双人抄网、共同拥有鱼。

**CaptureCommittedResult**：实物鱼捕获已经提交的不可变事实，供 Collection 生成 Grant 或 CapturePlan。
_Avoid_: 本地图鉴已保存、印记已成像。

## 印记与档案

**Imprint Candidate**：由已提交领域事实产生的印记候选，还不是图片或永久授予。
_Avoid_: 已拍照、已解锁。

**CapturePlan**：服务器向本地成像桥投递的本局成像计划。
_Avoid_: ProfileGrant、图片文件。

**ProfileGrant**：不可变永久授予内容，可能是图鉴记录、剪影、印记或解锁。
_Avoid_: ACK、CapturePlan。

**Grant Journal**：客户端本地 durable 写盘流程中的 Pending/Complete 记录。
_Avoid_: 服务器投递记录。

**Grant ACK**：客户端确认 Grant 已 durable 后发回服务器的回执。ACK 不修改 Grant 内容。
_Avoid_: 授予本身、成像结果。

## 命令与一致性

**RequestId**：命令幂等键的一部分，用于网络重试返回首次终态。
_Avoid_: Revision、随机日志 ID。

**Revision**：读取后写入的版本校验，用于拒绝陈旧视图。
_Avoid_: RequestId、时间戳。

**Terminal Cache**：聚合保存的首次终态缓存，让同键重放不重复提交。
_Avoid_: UI pending、临时日志。

**Result**：服务器命令返回的结构化结果，必须表达提交状态、错误、Revision 和公开事实。
_Avoid_: boolean success。

## 社交与营地

**Social**：求助、恶作剧、保护牌和偷鱼协议权限系统。
_Avoid_: 救援身体状态、印记裁决。

**Protection Sign**：玩家放置的防普通恶作剧范围 Actor。
_Avoid_: 全局免疫、偷鱼保护。

**Camp**：Lake 中固定营地，提供休息、救援落点、鱼缸转移和篝火回看。
_Avoid_: 建造系统、跨局基地。

**Fish Tank**：共享鱼缸的世界设施或容器适配者。它承载局内共享容器，不是永久仓库。
_Avoid_: Profile 仓库、装备背包。
