# Catfishing 存量代码审查报告（2026-08-20）

- **文档状态**：发现风险待修复
- **审查类型**：存量代码库审计（不是改动 Review，因此不套用 `REVIEW_REPORT_STANDARD.md` 的改动点矩阵与单文件改动卡）
- **审查基线**：工作区当前状态，分支 `codex/current-tree-snapshot`（HEAD `a9b5a05` + 未提交改动）
- **触发原因**：外部审查报告指出 7 类设计问题后，按 `AGENTS.md`「存量审查」新增条款执行的第一轮完整盘点

## 事实来源清单

- 源码：`Source/Catfishing`（20 个模块目录）、`Source/CatfishingEditor`，共 219 个 `.h`/`.cpp`
- 配置：`Config/DefaultGame.ini`、`Config/DefaultEngine.ini`
- 资产：`Content/`（39 个资产：17 装备 DataAsset、12 鱼 DataAsset、5 IA + 1 IMC、2 地图、2 StateTree）
- StateTree 事实源：`Source/CatfishingEditor/StateTree/CatBuildStateTreeAssetsCommandlet.cpp`
- 引擎源码：`D:/UE_5.8/Engine/Source`（用于确认委托广播与复制回调语义）
- 本轮验证基线：`Saved/CI/20260820-115554`（构建 0/0、自动化 206 条 failed=0、冒烟通过）
- 审查方法：5 个独立只读子 Agent 按维度并行盘点，主 Agent 逐条回源码复核并做交叉判定

## 审查方法与覆盖维度

按 `AGENTS.md` 新增的五个可机械搜索维度执行，每个维度独立子 Agent、互不共享结论：

1. 跨聚合写序列无回滚
2. 玩法准入门开着但领域写口已关闭
3. 零非测试调用方的公开符号（清单式全模块盘点）
4. 高频路径上的昂贵操作
5. 幂等/重放结构的配对完整性

**本报告只收录经主 Agent 回源码复核的结论。** 子 Agent 给出但复核不成立的判断已在「交叉核对与订正」中记录，未计入 findings。

## 结论摘要

**审查结论：fail。** 五个维度全部有实质发现。

关键判据是**可达性**——即缺陷能否被一个正常玩家通过当前已启用的 UI 触发。本项目的唯一运行期 UI 入口是开发期命令面板（`ECatCommandPanelAction`，23 个动作），且它在 `Config/DefaultGame.ini:224` 被显式启用（头文件默认为 `false`）。项目内**不存在任何 Blueprint 类或 WBP**，所以没有第二条 UI 路径。据此把发现分为三档：

| 档位       | 含义                                                            | 条数           |
| ---------- | --------------------------------------------------------------- | -------------- |
| A 现网可达 | 正常玩家经已启用 UI 即可触发                                    | 5              |
| B 潜伏     | 缺陷真实存在，但当前无 UI 路径可达，需自造 RPC 或后续接线才生效 | 6              |
| C 结构性   | 死代码、平行实现、契约漂移，无直接运行期损害但持续制造风险      | 8 组 / 53 符号 |

---

## A 档：现网可达缺陷

### A-1 商店订单付款后交付失败，钱扣了东西没到，且无退款无重投

- **位置**：`ShopEconomy/CatShopOrderCoordinator.cpp:126` `RunOrder`；不可逆点 `ShopEconomy/CatShopEconomyService.cpp:522`
- **链路**：面板 `ShopBuyFirstChum` → `ServerSubmitShopPurchase` → `RunOrder` → `PurchaseCatalogEntry`（扣钱、推进钱包版本、扣库存）→ `GrantRunConsumableFromAuthority`（发货）
- **失败分支**：`CatShopOrderCoordinator.cpp:183-193`，只有一条 `UE_LOG(Warning)` 后 `return`。全服务没有任何退款入口（`Wallet.Balance -=` 全文仅此一处，`ApplyFishSale` 只加不减），没有扫 `Pending` 的重投器。
- **可达性（确证）**：`RunConsumableStackCapacity=5`（`DefaultGame.ini:129`），`ShopBugChumOrder` 单价 1 且 `bUnlimitedStock=True`（`:174`）。身上已有 5 份窝料时再买一份 → `CatEquipmentComponent.cpp:256-261` 返回 `CapacityExceeded` → 扣 1 块钱、拿不到东西。另两条可达分支：Pawn 缺失时 `RecipientEquipment` 为空；目录项 `DefinitionId` 在装备目录里查不到（`FCatShopCatalogEntry::IsRuntimeReady` 明确不跨目录校验）。
- **后果**：公款每点一次少 1，限量条目额外白掉一格库存；账本留下 `Pending` 记录并广播全队，客户端只看到余额下降。UI 从不读 `DeliveryState`，**玩家侧零反馈**。面板每次点击新生成 RequestId（`CatLocalPlayerUISubsystem.cpp:590`），所以"再点一次"不是重投而是再买一单再扣一次钱。
- **备注**：`CatShopOrderCoordinator.h:38-40` 与测试 `CatShopEconomyOrderChainTests.cpp:651-658` 都把"付款成功但交付失败"当已知中间态记录了，但没有任何一环负责收口它。

### A-2 结算夜取用团队装备半提交，同一件装备同时存在于角色与公库

- **位置**：`Framework/Game/CatfishingPlayerController.cpp:811-830` `ServerTakeTeamEquipment_Implementation`
- **机制**：先 `Equipment->EquipFromTeamLibraryFromAuthority` 提交到角色，再 `Library->TakeInstance` 从库移除。第二步的写口检查在 `TakeInstance` **内部**，即装备已落到角色之后；失败只 `UE_LOG(Error)`，随后 `TakeTeamEquipmentTerminalCache.Add(RequestId, Result)` 把**第一步成功的结果**写进终态缓存。
- **可达性（确证）**：`CatfishingGameMode.cpp:727/734` 进两个结算夜时 `bRunCommandsOpen = true`，同时 `CloseShopForSettlementNight()`（`:1221-1235`）关闭团队装备库；准入门 `CanAcceptGameplayCommand`（`:509-512`）只看 `bRunCommandsOpen`，因此放行。面板 `TakeFirstTeamEquipment` 是 23 个动作之一，且 `CatCommandPanelWidget.cpp:96-97` 只判 `bHasTeamEquipment`、没有像商店三条那样按结算夜关按钮 —— 一键即达。
- **后果**：装备重复；客户端被告知成功。团队库**没有归还入口**（只有 `GrantFromShopOrder` 与 `TakeInstance`），该状态无法自愈。
- **备注**：`CatfishingPlayerController.cpp:762-763` 的注释称取走那步"只可能因版本漂移失败"——该注释已过时。

### A-3 搏斗期间每帧全场扫描 + 整表校验 + Blake3

- **位置**：`UI/CatLocalPlayerUISubsystem.cpp:505-560` `GatherCommandPanelInput`
- **每次调用的成本**：4 次 `TActorIterator` 全场扫描（`:550/:555/:563/:572`）、2 次 `FindComponentByClass`、以及在耗材栈循环内的 `FindRuntimeDefinition`（`:528`）——后者每次调用都无条件重跑 `ValidateRuntimeCatalog`（`Equipment/CatEquipmentSettings.cpp:96`），内含三轮 `LoadSynchronous` 与一次 Blake3 全表哈希。装备目录 17 条、鱼目录 12 条（`DefaultGame.ini`）。
- **频率（确证为每帧）**：`RefreshSurvivalView` ← `HandleSurvivalAttributeChanged` ← FightStamina 属性委托 ← `ACatFishingSession::ApplyFightExchangeFromStateTree` ← `FCatFishingFightExchangeTask::Tick`（`Fishing/CatFishingStateTreeNodes.cpp:88` 开启逐帧 Tick）。力度模型在拉/僵持/放线三条路上都写非零增量，无"增量为零"分支。
- **可达性**：`bEnableCommandPanel=True`（`DefaultGame.ini:224`），当前每一局都在生效。
- **未确定**：客户端侧频率由属性复制驱动（`REPNOTIFY_Always`），需运行期 `stat net` 抓取，静态不可定。

### A-4 竿磨损幂等键用随机 GUID，两张 Map 每秒增长且永不清理

- **位置**：`Fishing/CatFishingSession.cpp:805` `FlushPendingRodWear` → `Equipment/CatEquipmentComponent.cpp:613-617`
- **机制**：搏斗中每秒触发一次（`CatFishingSession.cpp:329` `RodWearFlushAccumulatorSeconds >= 1.0`），调用时传 `FGuid::NewGuid()`；被调方无条件 `TerminalCache.Add` + `TerminalPayloadByKey.Add`，键由该随机 GUID 构成（`MakeTerminalKey`，`:665`）。
- **后果**：两张 `TMap<FString,…>` 每秒各增一条、整局不清理（内存增长）；同时这套幂等缓存在本路径上**因键随机而永远不可能命中**，付了全部存储成本换零重放保护。
- **可达性**：与 UI 无关，任何一场搏斗都在发生。

### A-5 抛竿失败泄漏已 Committed 的 Boundary attempt

- **位置**：`Fishing/CatFishingService.cpp:118` `StartFishingSession`，不可逆点 `:192` `Boundary->CastAccepted`
- **机制**：`CastAccepted` 在 `Integration/Fishing/CatFishingBoundarySubsystem.cpp:300` 把 Cast operation 写成 `Committed` 并冻结 EncounterSpec；随后 `:226-236` 建会话失败时只 `Session->Destroy()`，**没有 `Boundary->CloseAttempt`**。同函数 Cast 之前的两条出口（`:158`、`:183`）是明确关闭 attempt 的，说明这是本函数自己的约定，Cast 之后的三条出口漏了。
- **可达性（确证）**：断竿后再抛一次。`InitializeSession` → `TryFreezeFisherRod` 在 `CatFishingSession.cpp:720-729` 因 `bRodBroken` 拒绝，而 Boundary 的 `Start`/`CastAccepted` 全程不看鱼竿，所以 Cast 必然已提交。
- **后果**：当前仅内存泄漏 + 协调合同破口（Bait/Fight 后半协议无生产调用方，见 C-4）。一旦后半协议接线，立刻变成"没有会话的 attempt 仍能扣掉玩家特殊饵"。

---

## B 档：潜伏缺陷（真实存在，当前无 UI 路径可达）

判定共同依据：`ECatCommandPanelAction` 的 23 个动作中**不存在**偷窃、恶作剧、保护牌、社交权限、踢人、协作任何一类；项目内零 Blueprint。因此 `ServerBeginTheft` 等 RPC 无合法客户端入口，凡以"存在活跃偷鱼协议"为前提的链路在正常游戏中均不可达。

### B-1 Host teardown 部分关闭后静默失败，整局软锁且可无限复现

- **位置**：`Framework/Game/CatfishingGameMode.cpp:1467-1473` `RequestRunTeardown`
- **机制**：`SacrificeCoordinator->PrepareForRunTeardown()`（`Run/CatSacrificeCoordinator.cpp:105`）、`Fishing->CloseCommandsAndTerminateAll()`（`Fishing/CatFishingService.cpp:325`）、`Social->CloseCommandsAndResolveAll()`（`Social/CatSocialService.cpp:61`）三者**进函数第一行就置 `bCommandsOpen = false`，与返回值无关**；随后 `if (!bSacrificeSettled || !bDomainsClosed) return;` 早退，而 `bRunCommandsOpen = false`（`:1478`）写在早退之后，永不执行。
- **终态**：准入门开着、三到五个领域永久关闭（四个领域的 `bCommandsOpen` 全仓无重开入口）。`ActiveHostExitRequestId` 未写（`:1489`），`:1422` 的重放 gate 命不中，房主再点退出会把同一段部分关闭再跑一遍并再次失败。
- **不变量分析**：`:1466` 的注释论证的是"即使前一步失败，四个领域的写口也必须关上"——作者刻意守住了"域一定关"，但同一失败路径把"门"漏在开着的状态。**不变量只守住了一半。**
- **可达性**：已知的可达触发链（受害者掉线留下孤儿 escrow → `CloseCommandsAndResolveAll` 返回 false）以偷鱼协议存在为前提，因 B 档共同依据不可达。`bSacrificeSettled` 一侧目前也造不出卡在 `ItemsCommitted` 的记录。**定为潜伏，不是现网 bug。**

### B-2 结算归档定稿后受害者的鱼仍会被吃掉

- **位置**：`Social/CatSocialService.cpp:718-762` `HandleTheftWindowExpired`
- **机制**：正常结束走 Ending/Ended 时只置 `bRunCommandsOpen = false`（`CatfishingGameMode.cpp:742`），**没有任何代码关 Social、也没有清偷鱼计时器**——`CloseCommandsAndResolveAll` 只在房主退出的 teardown 路径和世界销毁时调用。计时器不受 `bRunCommandsOpen` 约束，照常触发并执行 `CommitStolenFishConsumption`（不可逆）。
- **窗口**：进食窗 30 秒（`DefaultGame.ini:188`），且结算完成后无自动旅行，世界可长期存活。
- **订正**：该函数**开头是检查了** `bCommandsOpen` 并会返还赃鱼的，不是"从不检查相位"。真正的缺口是正常结束路径不关 Social。
- **可达性**：以偷鱼协议存在为前提，不可达。

### B-3 Items 预留终态缓存与锁记录生命周期脱钩

- **位置**：`Items/CatItemsService.cpp:406-423`（重放查询）、`:518-519`（`ReleaseFishHold`）、`:931-943`（teardown）
- **机制**：`ReservationTerminalCache` / `ReservationPayloadByKey` 全文件只有 `Reset`（`:30-31`）与 `Add`（`:427-428`），**没有任何 `Remove`**；而 `ReleaseFishHold` 只删 `Reservations` 与 `ReservationByFish`。解冻后同 RequestId 重放会命中陈旧终态返回 `bReserved=true, Error=None`，同时 `Reservations.Find` 失败导致 `OutFish` **不回填**（全零 GUID、重量 0）。
- **后果**：调用方拿到"冻结成功"+ 一条空鱼，而实际无锁；该鱼仍可被转移、吃掉、献祭。当前拦住"钱鱼双得"的是下游价格表对 `Weight<=0` 的拒绝（`CatShopEconomySettings.cpp:35`），**不是 Items 自己的契约**。
- **可达性**：需要同一 RequestId 在 Prepare→Cancel 之后重投，而面板每次点击新生成 RequestId，无 UI 路径。定为潜伏。

### B-4 CatchTheft 半条目

`Social/CatSocialService.cpp:239-255` 手写 `TheftTerminalCache.Find` 且完全不查 payload 表，`Finish` lambda 从不写签名。当前读写都绕开模板故不触发；一旦有人把这段"看起来能统一"的代码合回 `CatQueryTerminalReplay`，**每一条已缓存的成功追回立刻变成 `PayloadMismatch` → 永久 `InvalidPayload`**。

### B-5 Condition 恢复命令的声明不变量不存在

`Condition/CatConditionComponent.cpp:211-214`：键为 `MakeTerminalKey(Mode, RequestId)`、签名为 `Mode=%d`——**Mode 同时在键和签名里**，因此 `PayloadMismatch` 分支物理上不可达。同一 RequestId 分别发给 `ServerRequestFieldSelfRecovery` 与 `ServerRequestCampRest` 会落进不同槽位、**各执行一次**。当前损害小（Poison 清除幂等），但 `CatConditionComponent.h:92` 白纸黑字写的"换恢复参数会被拒绝"这条不变量不成立；任何一次让恢复路径变得非幂等的改动都会立刻把它变成重复执行漏洞。

### B-6 抢抄签名过覆盖导致合法重试变永久拒绝

`Fishing/CatFishingSession.cpp:417-420` 把 `ScoopWorldLocation` 写进幂等签名（`%.17g`），而 `CatFishingTypes.h:153` 的注释明确写着该字段"仅用于载荷诊断"，真实授权只读服务器 `Character->GetActorLocation()`。填充方取的正是逐帧漂移的角色坐标，因此同一意图的重试会从 `Replayed` 变成 `PayloadMismatch` → 永久 `InvalidPayload`。这是签名漏字段的**反向**错误。

---

## C 档：结构性问题

### C-1 大面积零生产调用方（8 组 / 53 符号）

全模块清单式盘点结果。其中**带写副作用 23 个**、**在配置里被显式启用 33 个**。

| 组  | 内容                                                                                                                                                                                                                                             | 写副作用                                                 | 配置开关                                                                             |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| 1   | Social/房主治理 RPC 9 个（`ServerBeginTheft`/`ServerCatchTheft`/`ServerSellStolenFish`/`ServerRequestMischief`/`ServerPlaceProtectionSign`/`ServerSetSocialPolicy`/`ServerRequestKickPlayer`/`ServerAssistFishingSession`/`GetLastTheftResult`） | 有（含真断连、写重连压制、Items escrow、计时器）         | `bEnableSocialRuntime=True`、`TheftPermission=Enabled`、`MischiefPermission=Enabled` |
| 2   | `UCatImprintMediaTransportService` 全部公开面 7 个（378 LOC，占 853 行 .cpp 一半）                                                                                                                                                               | 有（版本推进、chunk 累积）                               | `bEnableImprintMediaTransport=True`                                                  |
| 3   | Profile 相册读写面 5 个，含 `SetImprintHidden`（**真存档落盘**）                                                                                                                                                                                 | 有                                                       | `bEnableProfilePersistence=True` 等                                                  |
| 4   | Fishing Boundary Fight 半边协议 7 个（含 `CommitFightResourcesApplied`、`SealFinalFightCursor` **连测试都没有**）                                                                                                                                | 有（仅运行期内存）                                       | 无开关                                                                               |
| 5   | 两个已注册但从未落进 StateTree 资产的节点：`FCatFishingFailureBudgetTask`、`FCatRunResultReasonCondition`                                                                                                                                        | 有（连带 `CommitFishingFailure` 整条竿耐久惩罚链不可达） | `bEnableFishingRuntime=True`                                                         |
| 6   | GameState 四个零订阅者通知通道（共 `Broadcast()` 10 次，零 `AddUObject`）                                                                                                                                                                        | 广播端有写                                               | —                                                                                    |
| 7   | 两个完全无人引用的公开类型：`FCatFishingCastContext`、`FCatFishingInboxEntry`                                                                                                                                                                    | 无                                                       | —                                                                                    |
| 8   | 仅服务测试断言的只读面 15 个，含两处**真平行实现**：`TryGetDailyQuotaTarget`（与在跑的 `TryGetDayParameters` 并行）、`ResolveTimeOfDay`（与在跑的 `TryResolveScheduleSlot` 并行）                                                                | 无                                                       | 多个                                                                                 |

**系统性根因**：UI 命令入口与服务器 RPC 面存在覆盖缺口——面板覆盖 23 个动作，而 PlayerController 暴露的偷窃、恶作剧、保护牌、社交权限、踢人、协作共 6 类玩法 RPC 一个都没有对应入口，且项目内不存在任何 Blueprint 可以补上这条边。

### C-2 两份逐行相同的 SHA-256

`Integration/Fishing/CatFishingBoundaryHash.cpp:5-157` 与 `Collection/CatImprintMediaTransportService.cpp:8-176`，重复约 152-155 行：轮常量表、`RotateRight`/`Choose`/`Majority`/两组 Sigma、大端读写、padding、初始状态、message schedule、compression 全部实质相同，连中文注释都是同一句的改写。唯一实质差别是对外接口（返回 32 字节裸摘要 vs 返回 64 字符十六进制串）。

**同类事故已发生过一次**：`CatFishingBoundaryHash.h:25-27` 记录了 FightCursorLedger 与 BoundarySubsystem 各抄一份 canonical 编码的旧事故，当时的处理是把 Append\* 提成 public，SHA-256 本体这一层没有做同样的合并。

### C-3 幂等模板的四份手抄副本已语义反向漂移

同一个不变量在项目里有 8 处实现，谓词分三派：

| 位置                                                                                                                    | payload 缺失时 | 与模板   |
| ----------------------------------------------------------------------------------------------------------------------- | -------------- | -------- |
| `Condition/CatConditionComponent.cpp:81/:161/:215`                                                                      | 静默重放       | ✗ 反向   |
| `Framework/Game/CatfishingGameMode.cpp:619/:636`                                                                        | 静默重放       | ✗ 反向   |
| `Equipment/CatEquipmentComponent.cpp:511`、`Items/CatItemsService.cpp:409`、`ShopEconomy/CatShopEconomyService.cpp:635` | 判冲突         | ✓        |
| `Social/CatSocialService.cpp:240`                                                                                       | 根本不查       | ✗ 见 B-4 |

模板是 `!CachedPayload || *CachedPayload != Sig`；两份副本写成了 `CachedPayload && ...`，把"漏写一张表"的兜底判定变成了静默放行。当前不可触发（这两处写入严格 1:1 相邻），但正是 `CatDomainCommandTypes.h:126-128` 注释所说"抄出来的副本再各自漂移——这条不变量已经因此复发过多次"指的东西。

### C-4 目录查询即整表校验（性能与设计双重问题）

`UCatEquipmentSettings::FindRuntimeDefinition`（`Equipment/CatEquipmentSettings.cpp:93-115`）每次调用无条件 `ValidateRuntimeCatalog()`，内含三轮 `LoadSynchronous` 与一次 Blake3；无任何缓存或 memo（讽刺的是 `ValidateRuntimeCatalog` 内部 `:170` 恰好构造了一张 `TMap<FName, UCatEquipmentDefinition*>`，但它是局部变量、函数返回即丢弃）。约 15 处生产调用点。

鱼目录同构且**嵌套一层更深**：`Data/CatFishCatalogSettings.cpp:302` 在"遍历鱼 × 遍历该鱼的首选特殊饵"的内层循环里调用装备侧的 `FindRuntimeDefinition`，每次又是一次完整装备校验。且这套嵌套校验**每次抛竿要付两遍**（`CatFishingBoundarySubsystem.cpp:272` 一次、`CatFishingService.cpp:218` 一次）。

**未确定**：每条鱼配几个首选特殊饵存在 `.uasset` 内，静态不可读，所以倍数无法给出确数。

### C-5 瞬时失败被当终态永久缓存（跨 6 个领域）

`CatfishingGameMode.cpp:963-965`（`StateTreeUnavailable`——结算夜唯一收口就此死锁）、`:839/:892/:958`（`RevisionConflict`，其语义本身就是"请重读后再来"）、`CatfishingPlayerController.cpp:799-802`（Pawn 未生成/子系统未就绪）、`CatSocialService.cpp:126-129/:515/:653`、`CatFishingSession.cpp:262/:522/:597`、`CatImprintMediaTransportService.cpp:216/:220/:288/:353/:379`。

同一份代码里存在两套相反口径且无注释解释分歧：`CatSocialService.cpp:369-373` 与 `:248-251` 对瞬时事实**刻意不缓存**并写明理由；`CatfishingPlayerController.cpp:767-770` 对 gate 拒绝不缓存（正确），而 `CatfishingGameMode.cpp:829/:882/:947` 对同样的 `CommandsClosed` 写成终态（错误）。

### C-6 写口状态不复制，客户端只能靠相位猜

`bRunCommandsOpen` 与各领域写口开关全是服务器私有；`FCatShopPublicEconomySnapshot` 与 `FCatTeamEquipmentLibrarySnapshot` 都**不复制"写口是否关闭"**。`UCatCommandPanelWidget::IsActionAvailable`（`CatCommandPanelWidget.cpp:66-115`）是一份手写猜测：`:93-95` 与 `:107` 正确按 `IsSettlementNight` 关掉商店买/领/卖，`:96-97` 的 `TakeFirstTeamEquipment` 却只看 `bHasTeamEquipment`——**这正是 A-2 一键即达的直接原因**。

### C-7 其余低优先级留档

Rest 命令不配签名表的理由正确但无注释（`Camp/CatCampHubActor.h:61`，对照 `:75-78` 的 Campfire 写了）；Fishing 两组幂等键缺 Operation 段与其他领域口径不一致（`CatFishingSession.cpp:209/:415`）；`CatChumContributionCoordinator.cpp:31` 与 `CatChumSpotSubsystem.cpp:97` 对同一物理量用两级精度；`CatConditionComponent.cpp:157-158` 用 `GetName()` 而非 StableNetId 作身份；`CatImprintMediaTransportService` 的 payload 表是全项目唯一不叫 `*PayloadByKey` 的命名离群值（`TerminalPayloadSignatures`）。

---

## 参考实现（正面样本）

`Collection/CatImprintMediaTransportService.cpp:706-738` 的 `CacheTerminalResult` 把两次 `Add` 封在一个函数里，**结构上不可能半写**，且两处偏离模板的地方都在 `:702-706` 写明了理由。`Integration/Fishing/CatFishingOperationJournal.h:63` 走的是把 PayloadHash 存进 entry 内部的另一套设计，从结构上不存在双表配对风险。`ACatfishingGameModeBase::EvaluateAllEligibleReady` 是本仓唯一做了显式回滚的跨聚合写点。

---

## 交叉核对与订正

子 Agent 之间出现两处互相矛盾，主 Agent 回源码判定如下。**这两处直接改变了严重度排序，是本轮整合的主要产出。**

1. **偷鱼类链路的可达性**：维度 2 判定 A-1/A-2/R-1 为"可达性：高"；维度 3 判定 `ServerBeginTheft` 等零生产调用方。核对 `ECatCommandPanelAction` 全枚举确认**无任何偷窃/恶作剧/保护牌/踢人/权限/协作动作**，且 `Content/` 零 Blueprint。**判定：维度 2 的可达性结论有误**，相关链路全部降级为 B 档潜伏。
2. **`ApplyFailureBudgetFromStateTree` 是否在跑**：维度 2 的 R-2 视其为门关之后仍在发生的领域写入；维度 3 判定其驱动节点从未落进 StateTree 资产。核对 `CatBuildStateTreeAssetsCommandlet.cpp:126-243` 的全部 `AddTask<>`/`AddCondition<>` 确认 `FCatFishingFailureBudgetTask` **不在其中**。**判定：R-2 不成立**，该链路整体归入 C-1 第 5 组死代码。

另订正主 Agent 自己的一处统计：payload 签名写入实测 **40 处**（非 39），Δ 为 6（非 7）；漏计原因是 grep 模式 `Payload*By*` 未覆盖唯一命名离群值 `TerminalPayloadSignatures`。

## 未覆盖范围与剩余风险

- **未做运行期性能实测**。A-3、C-4 的绝对成本依赖 `.uasset` 内的数组规模与实际帧率，本轮只做静态调用链推导，未跑 profiler、未抓 `stat net`。
- **未审查渲染、动画、音频、关卡资产**，本轮只覆盖 `Source/` 与 `Config/`。
- **未做安全建模**。B 档"无 UI 路径"的判定基于当前客户端，不等于服务器可以信任客户端——自造 RPC 的客户端仍可触达 B 档全部路径。是否需要在服务器侧补齐拒绝，取决于产品对作弊面的要求，属产品决策。
- **产品决策未定，本报告不作建议**：C-1 中大量实现是"配置已开启但无 UI 接线"，究竟应当补齐接线还是撤下，事实源在飞书知识库，不在代码里。本报告只陈述现状，不替产品决定去留。
- **本轮未修改任何生产代码**，未执行 git 写操作。报告产出后工程状态与审查开始时一致。
