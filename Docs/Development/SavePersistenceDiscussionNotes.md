# Save / Persistence 需求核对笔记

更新时间：2026-09-11

文档状态：2026-09-10 本轮 LocalPlayerSaveGame 重构已完成代码与运行验证；Editor / Win64 Development 构建、16 项回归通过。证据为同进程 PIE 与真实本地磁盘，不代表打包 Steam 房主退出链路或完整 Save 模块已验收。

事实来源：

- 当前项目实现：`Source/Catfishing/Save/CatRunSaveGame.h/.cpp`、`Source/Catfishing/Save/CatSaveSubsystem.h/.cpp`、`Source/Catfishing/Online/CatOnlineSubsystem.h/.cpp`、`Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp`。
- 本轮原始验证：`Saved/Logs/SaveLyraFinalTests.log`、`.codex/state/save-lyra-baseline/editor-build.log`、`.codex/state/save-lyra-baseline/game-build.log`；用例入口 `Source/CatfishingEditor/Save/Tests/CatSaveRoundTripTests.cpp`。
- 参考实现：`D:/UnreaProjects/LyraStarterGame/Source/LyraGame/Settings/LyraSettingsShared.h/.cpp`。
- 需求来源：下文“飞书策划来源”所列的只读版本。

范围：本文记录第九模块 `Save / Persistence` 的策划来源、当前聊天补充决策和仍需确认的问题。本文不记录测试、验收或完成结论；项目长期进度入口待人工另定。

## 飞书策划来源

- 飞书空间：`小猫钓鱼`（space_id：`7670823117626870757`）。
- 读取身份：user，只读查询。
- 关键来源：`局与进程`、`联机社交`、`里程碑`、`设计修改记录`、`任务进度板`。
- 读取时间：2026-08-31。
- 已核对版本：`局与进程` revision_id `71`、`联机社交` revision_id `192`、`里程碑` revision_id `94`、`设计修改记录` revision_id `22`、`任务进度板` revision `289`。

## 需求基线锁定

第九模块 `Save / Persistence` 以 2026-08-31 读取并核对的飞书版本作为实现基线。后续飞书策划案即使继续更新，也不自动改变本模块的实现口径；除非人工明确宣布“重新锁定保存系统需求基线”，否则实现、技术方案和代码审查都按本文记录的版本判断。

当前版本的需求逻辑可以成立：它把“跨局长期事实”和“未结束当前局恢复数据”分开处理，因此不和局末清空冲突；当前 Run 存档只恢复房主本机当前局，不把本地存档升级成共享云档或长期房间。

## 策划已定口径

- 游戏是局制：开一局、玩、结束；不做沙盒房间持续保存、房间成长或局外世界状态。
- 跨局带走的是个人长期事实：图鉴、印记相册、外观解锁；失败不会回滚这些长期事实。
- 跟局走的是本局物资与局内状态：营地、鱼缸与局内图鉴、消耗品数量、公款与营地公共仓库、猫状态、祭坛进度与世界进度。它们可以跨天留存，但局末随局清空。
- 2026-08-27 的任务进度板把“相册 UI 与存档骨架”收窄为“存档骨架（图鉴／解锁／进度落盘）”：相册 UI、落水扑空触发和双人庆祝触发后置。
- W5 仍有“设置和保存、存档和加载、断线回大厅一键重邀 + 进度保底”的完整度要求，但飞书里没有查到 1 自动档 + 3 手动档、ESC 手动保存、读档替换自动档或删除入口的细化规则。

## 当前聊天补充决策

- 第九模块当前只讨论保存系统，不扩展图鉴系统，不做相册界面、新玩法触发或表现资源。
- 保存系统分两层讨论：第一层是系统怎么保存、读取和展示槽位；第二层是保存哪些数据。
- 局内保存菜单由 LocalPlayer UI 或同等独立入口打开，不由 HUD 负责创建、承载或转发。
- 旧的“1 自动档 + 3 手动档”和“读取后删除并重建自动档”聊天决策已废弃：当前实现按 GUID `SlotId` 管理单文件槽目录，`ActiveSlotId` 的检查点保存直接覆盖同一活动槽，读取不会删除或重建其他槽。
- 2026-09-09 代码口径收敛：Run 存档中的玩家状态只保存一个房主本机玩家快照，冷启动后直接恢复反序列化得到的这份快照。
- 2026-09-10 代码口径收敛：当前磁盘 schema 是 v6。仅旧 `USaveGame` 格式的 v5 会在内存中升级到 v6；未知版本或引擎 `SavedDataVersion` 不匹配会被拒绝，不会以新局覆盖原文件。

## 保存内容边界

- 保存系统不能把“跟局走”误做成“跨局永久保留”。本局未结束时，存档可以用于继续这局；一旦局末结算并清空，营地、鱼缸、物资、公款、营地公共仓库、猫状态和祭坛/世界进度不进入下一局。
- 当前局进度需要落盘：天数、阶段、额度目标与进度、世界进度、白天剩余时间或等价的倒计时事实。
- 当前局物资需要落盘：鱼护、共享鱼缸、营地公共仓库、消耗品数量、公款和商店当前库存。它们都是跟局走数据，只为继续当前局服务。
- 当前局玩家状态需要落盘：房主本机玩家的位置、朝向、身体状态、成长/等级相关进度和当前装备选择。它们不是跨局成长。
- 已经进入世界并持续生效的短时世界效果可以作为当前局事实保存，例如窝料场、求助信号、防偷保护牌；它们不因为读档变成跨局永久状态。
- 正在进行中的过程状态不保存：钓鱼会话、偷鱼追回窗口、供品结算预留、商店交易中间态、动作表现、镜头状态、临时输入锁和 UI pending 不进存档。读档只恢复存档中有的事实，不额外做一套“读档后取消”流程。

## 多人读档口径

- 存档首先是房主玩家的本地存档；多人只是允许其他玩家加入这一局，不把本地存档变成共享云档、在线房间快照或长期房间。
- 房主读取本地存档后，以该存档继续当前局；公共世界数据由房主恢复并同步给加入的客户端。
- 当前实现只序列化房主本机玩家快照；远端玩家重新加入时按营地出生，不消费房主本地位置。
- 多人读档不保存网络连接、客户端在线状态、自动重连关系或远端玩家个人位置。

## 当前代码事实

- 当前局世界槽落盘容器是 `Source/Catfishing/Save/CatRunSaveGame.h` 中的 `UCatRunSaveGame`，读写入口在 `Source/Catfishing/Save/CatSaveSubsystem.h` 和 `Source/Catfishing/Save/CatSaveSubsystem.cpp`。
- `UCatRunSaveGame` 继承 `ULocalPlayerSaveGame`。它沿用 Lyra `ULyraSettingsShared` 的本地玩家存档模式：用 `CreateNewSaveGameForLocalPlayer` 创建候选、用 `LoadOrCreateSaveGameForLocalPlayer` 或 `AsyncLoadOrCreateSaveGameForLocalPlayer` 读取、用 `AsyncSaveGameToSlotForLocalPlayer` 写入；差别是 Lyra 保存共享设置，Catfishing 保存房主本机当前局快照。
- `UCatRunSaveGame` 保存本局世界状态、营地库存、世界鱼与一份房主本机玩家快照；保存时写入这个对象，读取时从 UE 反序列化出的同一个对象恢复。
- 跨局长期档案仍由 `Source/Catfishing/Profile/CatProfileSaveGame.h` 中的 `UCatProfileSaveGame` 承载，不与当前局 Run 存档混用。
- 局内菜单保存入口当前走前端/菜单控制器请求到 `UCatSaveSubsystem::RequestSaveActiveRun`，不由 HUD 承载。

## 本轮接入事实

- 首批可恢复的房主玩家状态是 `FCatSavedPlayerRunState`：房主本机 Character 的权威 `FTransform`、随身库存和装备选择。`RestorePlayerAfterSpawn` 只让本机 Controller 消费该快照；远端玩家仍按营地出生。
- 世界侧首批持久化消费者是唯一营地公共仓库和 `UCatFishContainerService` 的关卡稳定键鱼容器。`BuildActiveRunSaveGame` 导出营地已提交库存和 `WorldFishContainers`；`RestoreWorldAfterHostsReady` 先恢复营地，再恢复世界鱼容器。营地恢复后鱼容器失败时只尝试回滚营地，并拒绝继续进入玩法。
- v5 到 v6 不是磁盘就地改写：`UCatRunSaveGame::HandlePostLoad` 只对 `SavedDataVersion == 0 && FormatVersion == 5` 的已加载对象把内存字段升到 v6；下次成功保存才会以 v6 写出。其他版本保留原值，随后由 `ValidateLoadedRunSaveGame` 拒绝。
- 目录扫描和正式读槽先用 `HasRunSaveFileHeader` 确认现代 UE `GVAS` 文件标记，拒绝缺失或无效文件头，避免引擎把垃圾字节当作无标记旧格式类名解析。这里只做文件头检查，不是对任意损坏载荷的完整校验器。
- `LoadOrCreate` 系列 API 在读不到或反序列化失败时可能给出默认对象，因此扫描与正式读取都要求 `WasLoaded()` 为真。`HandleRunLoaded` 对 `WasLoaded()==false` 或载荷校验失败立即撤销旅行许可，坏档不能被当成新档进入世界。
- 写盘“已受理”不等于成功：`UCatRunSaveGame::HandlePostSave` 把引擎真实 `bSuccess` 交给 `UCatSaveSubsystem::HandleRunSaved`，后者只在对象、LocalPlayer、槽名和载荷校验均匹配后广播 `OnSaveCompleted(RequestId, true)`。
- Lake 房主离开时，`UCatOnlineSubsystem::BeginHostLeaveSave` 先订阅 `OnSaveCompleted`，再调用 `RequestSaveActiveRun` 并冻结返回的 `RequestId` 与 Online epoch。只有 `HandleHostLeaveSaveCompleted` 收到同一 RequestId、同一 epoch 的成功回执，才启动 Run teardown；失败或失效回调保留 Session 与 Run，不把保存受理当作离开许可。
- `CapturePlayerBeforeLogout` 现会在旧档的首次恢复尚未应用到新 Pawn 时跳过 `RestartPlayer` 过程中的临时解除占有捕获。这样默认出生库存与 Transform 不会覆盖已读入的 `PlayerSnapshot`；完成恢复后的角色才重新成为退出前采样来源。

## 仍未确认

- 飞书没有给出槽位 UI 细节；当前 GUID 槽目录及活动槽检查点是代码接入事实，前端交互仍需产品确认。
- 飞书没有给出保存安全窗口；哪些阶段允许手动保存，哪些事务需要先等结果再保存，仍需实现前确认。
- 哪些写入必须立即落盘，哪些可以排队或合并。
- 保存失败时哪些流程必须阻止 ACK，哪些可以只给玩家错误提示。
- 如果以后要恢复远端玩家个人位置，需要重新确认跨机器身份、云档或后端来源；当前本地 Run 存档不承担这个职责。

## 2026-09-10 现有实现与目标实现核对结果

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 存档对象与读写 | `Save/CatRunSaveGame`、`CatSaveSubsystem::RequestCreateSlot/RequestLoadSlot/RequestSaveActiveRun` | 普通 SaveGame、固定索引改为 LocalPlayerSaveGame 归属和原生异步生命周期 | 新对象实现版本迁移与完成回执；原 Subsystem 保留前端协调职责 | 绑定本机玩家 → 原生读写 → 真正完成回执 | 对象释放及 GC 后从实际文件重读 | 原生读写及冷读通过；`SaveLyraFinalTests.log` 的 `save_disk_roundtrip_verified` |
| 位置与角色生命周期 | `GameMode::RestartPlayer` → `RestorePlayerAfterSpawn`；解除占有 → `CapturePlayerBeforeLogout` | 保留厘米 Transform；修复首次恢复前临时解除占有覆盖快照 | 首次恢复前跳过旧档离开捕获；生成与占有后应用位置 | 世界恢复 → 角色生成 → 库存装备 → Transform | 正式 RestartPlayer 立即比较完整 Transform；真实 OpenLevel 后比较水平位置 | 两条路径均通过；新 World 由 GameMode 自动恢复，未手工调用恢复 |
| 背包与营地库存 | `FCatSavedRunInventorySlot`、`WriteSavedInventorySlots`、`PrepareInventoryEntriesFromSave` | 保留格位、数量、GUID、耐久；加入鱼重量千克、会话与捕获者身份 | 共同载荷校验，实例由正式定义创建再交给 Inventory | 先校验整批，再提交领域库存 | 鱼饵 3 个、空格、断竿、2.75kg 背包鱼、1.25kg 营地鱼；重复 GUID 拒绝 | 实际磁盘往返恢复通过；重复身份不会启动覆盖 |
| 装备与世界鱼容器 | Equipment 导出恢复；FishContainerService 的持久键快照 | 保留已有消费者和装备选择；统一背包、营地、世界鱼实例唯一性 | 沿用原领域入口，不新增第二份运行状态 | 营地 → 世界鱼容器；背包 → 装备选择 | 16 项回归含正式三端库存 WBP 交互、装备与商店；世界鱼服务运行调用 | 已有回归通过；非空世界鱼护/鱼缸跨图内容本轮未实测，原恢复路径保留 |
| Online / UI / 自动保存 | `BeginHostLeaveSave` 等待 `OnSaveCompleted(RequestId,true)`；前端槽列表与菜单 | 公共接口与自动保存间隔不变；底层完成事件改接原生 SaveGame 回执 | 保留 RequestId/epoch 和 busy 协议 | 保存成功 → teardown → 返回前端 → Release | busy 时 Release 拒绝已实测；前端/Online 编译及调用链核对 | 契约保留；打包 Steam 双端房主退出成功/失败流程本轮未复测 |
| 旧档、文件与工程资产 | v5 FormatVersion 字段；Save 配置、现有 WBP；开发说明 | v5 可读且读取不改文件；新写入 v6；无配置和二进制资产迁移 | 保留兼容字段，去掉旧固定索引与重复校验；无效头/错误类拒绝 | 读取迁移只在内存；后续主动保存才写新版本 | 旧 DTO 文件迁移前后逐字节比较；无效头与错误类读取 | 兼容与拒绝检查通过；Editor 和 Game Development 构建通过；未重新 Cook/打包 |

本轮删减审查保留了必要的三处边界：SaveGame 对象承接引擎生命周期；一次性完成委托衔接 Online 的真实落盘回执；文件头检查阻止已复现的引擎旧格式回退断言。库存载荷只保留一套预检，旧 FormatVersion 和装备磁盘 DTO 因 v5 消费者继续保留。没有新增配置、业务进度清单或并行存档实现。

## 2026-09-11 房主保存退出卡住修复

故障证据是 `打包/Windows/Catfishing/Saved/Logs/Catfishing-backup-2026.09.11-09.21.13.log`：09:20:38 UTC，退出 RequestId `6944D663-4D62-C2A8-A727-BDB64BC4FCFC` 的保存已经 `Verified=1`、`Success=1`；随后 `run_teardown_pending` 为 `PendingRemoteAcks=1 PendingGrantAcks=0`。约 0.18 秒后远端断开并触发 `identity_released Result=ControllerMatched`，房主仍未开始 Destroy/Frontend travel。当前客户端 `HandleDestroySessionComplete` 在平台销毁后才提交末次 Reliable RPC，随后马上旅行；连接结束后不能保证该 RPC 送达。原 `Logout` 删除准入身份却没有解除退出等待，使后续 ACK 也无法再通过 Active Controller 校验。

修复仅改变远端离局的完成证据：精确匹配 Active 准入记录的 authority `Logout` 可以解除该远端的等待，原销毁成功 ACK 仍可先行解除同一等待。服务器完成身份和 `Super::Logout` 清理后，调用原 `NotifyHostExitGrantAckProgress`，仍要求所有永久 Grant 收到真实 durable ACK 才通知 Online 继续。没有超时伪成功、第二次保存、奖励补发或客户端状态伪造；RequestId/epoch、存档格式、计时单位、默认配置与服务器权威均保持。断线本身不证明客户端 Session 销毁或永久档案落盘成功。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 保存和入口 | `Source/Catfishing/UI/Save/CatLakeMainMenuController.cpp::RequestReturnToMainMenuFromWidget` → `Online/CatOnlineSubsystem.cpp::BeginHostLeaveSave/HandleHostLeaveSaveCompleted` | 保留保存成功后才 teardown；保存失败保持原错误回执 | 保留原接口、槽位和异步写盘 | 菜单 → 保存完成 → GameMode | 原始日志写盘成功；核对失败分支仍不拆局 | 未改存档代码；故障日志已确认存档完成 |
| 权威离局与生命周期 | `Source/Catfishing/Framework/Game/CatfishingGameModeBase.cpp::RequestRunTeardown/Logout/AcknowledgeHostExitClient` | 原来只接受末次销毁 RPC；现在还接受精确连接的真实 Logout | 同一等待集合改名 `PendingHostExitRemoteStableNetIds`；完成函数改名 `CompleteHostExitWait` 并复核所有门槛 | 先校验身份，清理 Logout，再通知原 Online 委托 | 两客户端、ACK 丢失/先到、重复 Logout、旧连接、错误 RequestId、单次完成 | `Report-Red` 在原代码复现；`Report-Green` 修复后通过，原 RequestId/epoch 随真实完成委托返回 |
| 永久档案 | `Source/Catfishing/Collection/CatRunImprintService.cpp::AreAllGrantAcksComplete` → GameMode 完成 gate | 离线不得冒充奖励落盘；原契约保持 | 不改 Grant 投递记录，缺 ACK 时 `run_teardown_grants_pending` 明确告警 | 远端离局全齐后仍复核 durable ACK | 用正式服务创建真实未 ACK Grant，所有玩家 Logout 后仍不得完成 | 专项通过，待确认 Grant 仍为 1；没有强行清空记录 |
| 客户端与旅行 | `Source/Catfishing/Online/CatOnlineSubsystem.cpp::RequestRemoteHostExit/HandleDestroySessionComplete` → `ServerAcknowledgeHostExit`、`BeginTravelToFrontend` | 保留客户端销毁与旅行时序；发送日志不再暗示服务器已收到 | 原入口新增 `online_host_exit_received/online_destroy_completed/online_host_exit_ack_submitted/online_host_exit_ack_unavailable` | 房主仍经原 teardown 完成委托调用 Destroy 和旅行 | 源码调用链、预载 World 的 GC/释放回归；真实 Steam 双端另验 | 预载回归通过；本轮没有客户端故障日志，不能宣称双端已验收 |
| UI、配置与资产 | `Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp` 消费 Online 快照；`/Game/UI/Save/WBP_CatLakeMainMenu`、Frontend loading WBP；`Config/DefaultGame.ini` 的 `CatOnlineSettings`、`ProjectPackagingSettings` | 接口、地图、默认值、遮罩消费者不变；动画不涉及 | 不改 WBP/Blueprint/DataAsset、不迁移资产、不改生成脚本与 Cook 入口 | 保留原快照驱动；新包须含修复源码 | 二进制资产额外引用未全面审计，因此保留所有公开/RPC 入口 | 无资产删除；正式新包菜单和双端无 `-log` 落盘尚未验证 |
| 旧路径与诊断 | GameMode 私有等待字段和 `run_teardown_acks_complete` 日志；现有 `Build/Automation/RunCatAutomation.ps1` | 等待人数现在表示远端离局，不能继续称全部是 ACK | 私有字段与所有 C++ 消费者同步改名；新事件为 `run_teardown_complete`，字段 `PendingRemoteExits` | 不改外部 RPC 签名或新建退出状态机 | 搜索 Source/Config/Scripts/Build/Docs，检查最终 diff | 旧私有字段/函数/日志运行入口均移除；原 RPC 保留为已确认消费者，非废弃路径 |

验证证据位于 `Saved/Automation/HostExit-20260911/`：`contract` 的主工程 Editor 构建 `BuildEditor.log` 成功，`Report-Green/index.json` 为两项 clean Success（退出专项内三种时序、原预载生命周期回归）。`runtime_behavior` 限于真实 UE 测试 World 中调用生产 Logout/ACK/完成委托，见 `Green.log`；它不是 Steam 网络传输实测。修改前只有测试夹具的初次编译遇到 UniqueNetId 构造重载歧义，修正后 `Saved/Logs/HostExit-BuildEditor-Red.log` 成功，`Red.log` 证实原退出逻辑失败。主工作区 `BuildGame.log` 被并行 `CatPhysicsPrototypeVisualComponent.cpp` 中未声明的 `InitialAnimationRelativeTransform/InitialVisualRelativeTransform` 阻塞，本修复不修改该代码；游戏构建另使用 `IsolationBase.txt` 中的已提交基线 `d4ec285` 加 `FixSourceHashes.json` 对应的四个修复文件验证，`BuildGame-Isolated.log` 为 Win64 Development `Succeeded`。验证期间另一个任务提交了祭坛/截止计时相关修改；它们保留在主工作区，本次隔离构建与退出检查点不包含这些修改。

`presentation_delivery` 未完成：当前运行的旧包没有自动获得修复；需新 Cook/打包并以房主和客户端分别退出复测，核对双方 `<打包根目录>/Catfishing/Saved/Logs` 默认落盘日志中的同一 RequestId、`run_teardown_remote_departed` 或 `run_teardown_remote_ack_received`、`run_teardown_complete`、Destroy 结果与 Frontend 到达。若 `PendingGrantAcks>0`，原永久档案失败/重连恢复缺口仍须保留，不能用离线或计时器绕过；此次用户故障为 `PendingGrantAcks=0`。Run/Online 原子模块不因此关闭。
