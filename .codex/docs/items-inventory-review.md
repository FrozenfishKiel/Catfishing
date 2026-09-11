# Items / Inventory 收敛审查报告

## 1. 文档状态

待程序员审查。本文不是验收报告，也不声明模块端到端完成或已提交。

2026-09-10 补充修订：已删除正式库存版本字段、复制通知、递增、请求前提及冲突分支，UI/RPC 只提交宿主与槽位。后续 MVC 修订又把库存 UI 从旧聚合投影收敛为“一份 `UCatInventoryComponent` 持有一份 `UCatInventoryModel`”：Widget 只显示显式库存，Slot 直接用 owning PlayerController 提交宿主与槽位 RPC，PageController 只管理窗口、输入和焦点。鱼容器快照序号仅保留复制与恢复一致性用途。

前一轮库存版本删除的事实基线为 `.codex/state/inventory-revision-removal-baseline/Source`，完整差异见同目录 `source-delta.patch`。该目录的 `editor-build-final.log`、`game-build-final.log`、`targeted-tests.log` 和 `blueprint-check-final.log` 分别记录 Editor/Game 构建通过、30 项定向回归通过、23 项定义配置一致和 15 个蓝图编译通过。以下版本删除记录保留原始基线；当前 MVC 修订以紧接的补充表和当前源码为准。

### 当前 MVC 与多客户端修订（2026-09-10）

本轮差异基线为 `.codex/state/inventory-multiclient-baseline/Source`。审查对象是实际库存复制与 UI 职责链；不关闭更大的 UI/Items 原子模块。

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 格位复制与恢复 | `Inventory/CatInventoryComponent.h` 的 `FCatInventoryEntry::operator=`；cpp 的交换、`ReplaceInventoryEntriesFromAuthority` 和 `PostReplicatedReceive` | 原赋值/交换及整表 Reset 破坏 FastArray 格位身份；删除前通知读到旧数组 | 内容赋值保留格子复制身份，恢复原位写入，整批复制结束再通知 | 服务器事务 → FastArray → 每库存 Model；实例、数量、容量与服务器权威契约保持 | 真实三端新增、移动、清空、交换、恢复、缩2格再扩48格 | 三处均有修前失败和修后通过；`network-before.log`、`replace-before.log`、`mvc-formal-ui-before.log` 对照 `mvc-final-tests.log` |
| Model 与面板 | `CatInventoryComponent::GetInventoryModel` → `CatInventoryModel::SetInventoryList` → `CatInventoryWidget::SetInventoryContext/RefreshInventorySlots` | 旧 LocalPlayer 聚合 Model 选择多个库存；现每个库存仅更新自己的 Model | 替换旧三数组 ViewState、pending 与上下文分支；Widget 明确订阅一份库存 | 先组件 Model，再面板订阅；营地根与嵌套背包分别绑定营地与 owning Pawn | 同屏两个库存及两客户端显示独立性 | 正式 WBP 三端测试通过，实际图片 brush、数量文字和空格清图断言通过：`mvc-display-test.log` |
| 操作与窗口 | `CatInventorySlotWidget::NativeOnDrop/RequestUseItem`、`CatInventoryPageController::OpenInventory`、Camp/FishGuard/FishTank 的打开调用 | 旧 PageController 转译聚合条目并等待回执；现 Slot 只提交源/目标宿主和格位 | 删除操作中转；窗口控制器只管理打开、关闭和输入；统一 `OpenInventory(Component,Class)` | Slot → owning PlayerController RPC → 原权限/事务；复制 → Model → Widget | 一端背包拖入营地，另一端继续移动；关闭/右键其余场景只作源码审查 | 两次真实 Slate Drop、服务器提交及两端正式格子收敛通过；右键使用、鱼护/鱼缸完整实玩未运行 |
| 正式资产、配置与周边 | `/Game/UI/Inventory/WBP_CatCampInventory`、普通背包/鱼护 WBP、`/Game/UI/InventorySlot/WBP_CatInventorySlot`；既有 UCatUISettings | 保留布局、悬停、配置类和库存字段含义 | 保留 Camp/FishGuard 反射父类身份；没有修改二进制资产、持久化结构、数量单位或网络权威 | 先导出确认旧字段无引用，再移除反射 API，最后编译与运行真实 WBP | 蓝图父类、同屏嵌套面板、装备/商店/存档消费者 | `mvc-final-assets.log`：15蓝图、23定义；`mvc-final-tests.log`：32项通过；存档整场恢复和打包Steam实玩未运行 |
| 删减与旧导航 | 三个空客户端复制扩展、`CatInventoryTypes.h`、两份空子类 cpp、`SetSelected/bSelected/BP_InitializeSlot`；既有 WBP 清单 | 无消费者的转发、状态、扩展误导导航 | 已删除；保留父页使用按钮选择下标、实际玩法观察者和现有事务幂等 | 引用搜索与正式 WBP 导出确认后删除 | 编译、符号扫描、独立注释语义审查 | 19源码文件730条注释检查通过，26热点已有逐项说明；旧库存投影/pending无残留 |

本轮构建与验证来源均在 `.codex/state/inventory-multiclient-baseline/`：`mvc-final-editor-build.log`、`mvc-display-test-build.log`、`mvc-final-game-build.log`、`mvc-final-tests.log`、`mvc-display-test.log`、`mvc-final-assets.log`。这些证据覆盖 contract 与三端 PIE runtime_behavior，并检查正式 WBP 的显示控件内容；NullRHI 不证明画面像素、交互手感或重新打包后的 Steam 多进程表现。

### 前一轮版本删除记录

| 功能/环节 | 当前位置与引用证据 | 现有行为与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 处理结果与证据 |
| --- | --- | --- | --- | --- | --- | --- |
| 正式库存 | `Source/Catfishing/Inventory/CatInventoryComponent.cpp:251,2286,2352`，复制、Use 和 Move | 原先依赖库存版本；现在按当前条目执行 | 删除版本字段、复制、递增、冲突和参数 | 先核心，再消费者 | 容量、实例存活、held 归还和重放 | Editor/Game 通过；库存与装备回归通过 |
| UI/RPC | `Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.cpp:43,142`、`Source/Catfishing/Framework/Game/CatfishingPlayerController.cpp:646,773` | 客户端不再提交库存版本，也不再经 PageController 转译 UI 投影 | Slot 直接提交库存宿主 Actor 与槽位；服务器重读 `InventoryComponent` | Slot → PlayerController RPC → Statics/Inventory | WBP 父类、右键/拖放资产事件、正式点击拖放未实玩 | 代码链路已按源码核对；不声明 UI 实玩完成 |
| 鱼容器 | `Source/Catfishing/FishContainers/CatFishContainerService.cpp:525`，TransferOwnedFish | 捕获、转移、偷取、进食不再比较源/目标版本 | 删除命令前提；删除无调用、非反射的售鱼预检与可触达转移包装入口 | 服务命令与调用方一起衔接 | 快照复制与恢复原子性继续保留 | 构建通过；源文件差异逐项检查；鱼容器玩法未做双端实玩 |
| 装备、窝料、身体与偷鱼 | `Source/Catfishing/{Equipment,Environment,Condition,Social}` 的库存调用 | 删除库存参数及别名，不再返回库存版本 | 使用当前实例、数量与服务结果 | 先库存，再所有调用与现有测试 | 补饵、鱼竿换装、打窝与身体效果 | 30 项定向回归通过；真实身体/偷鱼操作未实玩 |
| 商店确认 | `Source/Catfishing/ShopEconomy/CatShopEconomyService.cpp:516` | 原 `DeliveryRevision > 0` 会拒绝已入库订单 | 删除别名及校验；使用已有交易与入库回执 ID | Purchase → Grant → Confirm | 扣款后确认失败、重放重复扣款/发货 | `CatShopInventoryDeliveryTests.cpp:24` 真实服务边界回归通过 |
| 文档和规则 | `AGENTS.md`、库存 WBP 清单、商店方案、钓鱼蓝图指南 | 旧说明仍要求提交库存版本 | 删除旧说明，记录不得恢复该机制的规则 | 以当前接口更新说明 | 误导后续蓝图接线 | 活跃源码和说明中的已删字段名扫描无残留 |

删减结果：除一条覆盖实际付款、入库、确认与重放的回归外，没有增加生产类型、包装层或替代计数器；已删除的库存版本未用零值兼容。通用命令结果类型中的 Revision 仍服务其他领域，库存不再写入它。打包双端、正式 UI 实玩和存档完整恢复未在本次验证，不能以构建和上述回归替代。

## 2. 事实来源

- 差异基线：`.codex/state/items-inventory-baseline/{Source,Config,Knowledge,Docs}`；本报告不把完整 `git diff` 或 `items-inventory-changes.json` 当作范围权威。
- 当前源码：`Source/Catfishing/Inventory/`、`Equipment/`、`Data/`、`Fishing/`、`Save/`、`UI/Inventory/`、`Items/`。
- 参照源码：`D:/UnreaProjects/AegisOdyssey/Source/AegisOdyssey/Inventory/AOInventoryComponent.h:27`、`AOInventoryItemInstance.cpp:43`、`AOInventoryItemDefinition.h:50`、`Items/AOItem.cpp:48-69`、`Items/AOEquipmentItem.cpp:25-39`、`Equipment/AOEquipmentInstance.cpp:144`。
- 最终证据复核：`.codex/state/items-inventory-baseline/final-editor-build.log:116`、`final-game-build.log:115`、`final-comment-scan.log:1`、`fragment-final-check.log:1646,1650`、`final-inventory-equipment-tests.log:2569-2571,2610`、`pickup-final-retest.log:1989-1996,2006`、`anchor-retest.log:2084,2100,2108,2111`；原 `integrated-tests.log:2159,2183,2195` 只作为 anchor 旧失败证据。

## 3. 改动总览

目标是把 Catfishing 运行库存收敛为唯一的 `FCatInventoryEntry + UCatInventoryItemInstance`：格子只保留实例、数量和库存归属；鱼重量/来源、鱼竿耐久等领域运行态留在实例。定义静态能力拆成 Rod、Bait、Float、Scoop、Chum 五个 Fragment，背包以 `CatBackPackComponent` 明确归属。旧 `CatInventoryRuntimeTypes.h`、`FCatRunInventorySlot` 和 Definition 归一化投影被删除。

仍保留存档 v5 的 `FCatSavedRunInventorySlot` 磁盘结构、四个钓具槽位玩法，以及服务器事务的 RequestId 幂等合同。库存 UI 的 Action/pending 回执状态已在 MVC 修订中删除；Slot 只生成请求 ID，界面只随所属库存 Model 更新。

| 功能/环节 | 当前位置与引用证据 | 现有实现与目标差异 | 处理方式与目标位置 | 衔接依赖与顺序 | 回归风险与验证方式 | 最终结果 |
| --- | --- | --- | --- | --- | --- | --- |
| 运行库存 | 旧 RuntimeTypes / 当前 `CatInventoryComponent.h:21-55` | 投影重复承载实例状态 | Entry 只存实例+数量+本地 SlotOwner | 先实例，再消费者 | Entry/Save 测试 | 旧投影删除 |
| 装备能力 | `CatEquipmentDefinition.cpp:115-152` | 单一 DA 字段混杂五种能力 | 五 Fragment | Definition -> Fragment -> consumer | Fragment 重存检查 | 23 模块重存后 0 error/0 warning、10 BP 通过 |
| 使用回执 | `CatInventoryComponent.cpp:1594,1845` | 鱼专用 UI/Slot 回执 | Entry + RequestId | Inventory -> Fishing/UI | 终态重放测试 | 保留服务器 RequestId 幂等合同 |
| 存档 | `CatSaveSubsystem.cpp:127-197` | v5 DTO 不能直接删 | DTO 边界转 Entry | Save prepare -> Restore | 旧档/字段审查 | v5 保留 |
| 世界拾取 | `Items/CatItem.cpp:63-95`、`Items/Fish/CatFishPickupActor.cpp:616-661` | `ACatItem` C++ 拾取锁已覆盖；Fish 只迁目录并保留反射类名 | 保留新通用世界物和既有鱼拾取玩法 | 先源码与 fixture，再核对正式 BP/场景 | `pickup-final-retest.log:1989-1996`；正式资产接线待查 | C++ 满包拒绝、重入拒绝、唯一提交通过；正式 `ACatItem` BP 接线未验证 |
| UI 路由 | `CatInventoryPageController.cpp:14,48`、`CatInventoryWidget.cpp:17,85`、`CatInventorySlotWidget.cpp:43,142` | 旧来源枚举和聚合投影路由已删除 | 每库存 Model + Widget 显式上下文 + Slot 直提 RPC | InventoryComponent → Model → Widget → Slot → RPC | WBP 父类与同屏多库存上下文 | 已核对当前源码；资产实玩未覆盖 |
| 背包 | `CatCharacter.cpp:29` | 通用组件归属不清 | BackPack 子类 | Character 创建 -> 初始化 | Inventory 测试 | 已接入 |

## 4. 文件结构与审查地图

```text
Source/Catfishing/Inventory/
  CatInventoryComponent.h/.cpp            Entry 真相、Use/UnUse、held 所有权、导入导出
  CatInventoryItemDefinition.h/.cpp       Fragment 查找与实例类选择
  CatInventoryItemInstance.h/.cpp         通用实例使用契约
  CatFishInventoryItemInstance.h/.cpp     鱼重量/来源实例状态
  CatBackPackComponent.h/.cpp             角色随身库存专用组件
Source/Catfishing/Equipment/
  CatEquipmentDefinition.h/.cpp           装备身份和 Fragment 能力裁决
  CatEquipmentInventoryItemInstance.h/.cpp 鱼竿耐久实例状态
  Fragments/CatEquipmentFragment_*.{h,cpp} 五组静态能力
Source/Catfishing/Items/                  新通用世界物；鱼拾取纯 C++ 迁目录且保留既有类名/玩法
Source/Catfishing/Save/CatSaveSubsystem.cpp v5 DTO <-> Entry/Instance 边界
Source/Catfishing/Fishing/CatFishingService.cpp Entry Use 回执的鱼竿实例消费
Source/Catfishing/UI/Inventory/            每库存 Model、显式 Widget 上下文、统一 OpenInventory 窗口入口
Source/Catfishing/Inventory/Tests/CatInventoryInstanceTests.cpp 新增 Entry/Instance 契约测试
```

`Docs/Development/非钓鱼核心功能待办.md` 虽出现在 `items-inventory-changes.json`，但不是本轮作者或审查范围。生成 `Intermediate/`、`Binaries/` 与日志不是代码审查对象。

| 分类 | 真实完整路径 | 审查角色 |
| --- | --- | --- |
| 新增 | `Source/Catfishing/Inventory/CatBackPackComponent.h/.cpp` | 角色背包实现 |
| 新增 | `Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_{Rod,Bait,Float,Scoop,Chum}.h/.cpp` | 五个静态能力片段 |
| 新增 | `Source/Catfishing/Items/CatItem.h/.cpp`、`CatEquipmentItem.h/.cpp` | 通用/装备世界拾取 |
| 迁位新增 | `Source/Catfishing/Items/Fish/CatFishPickupActor.h/.cpp` | 世界鱼拾取新路径 |
| 删除 | `Source/Catfishing/Inventory/CatInventoryRuntimeTypes.h` | 运行 Slot 投影类型 |
| 迁位删除 | `Source/Catfishing/FishContainers/World/CatFishPickupActor.h/.cpp` | 原世界鱼拾取路径 |
| 修改 | `Source/Catfishing/Inventory/CatInventoryComponent.h/.cpp`、`CatInventoryItemDefinition.h/.cpp`、`CatInventoryItemInstance.h/.cpp`、`CatFishInventoryItemInstance.h/.cpp` | Entry、实例、片段、Use/Save 边界 |
| 修改 | `Source/Catfishing/Equipment/CatEquipmentDefinition.h/.cpp`、`CatEquipmentComponent.h/.cpp`、`CatEquipmentInventoryItemInstance.h/.cpp` | 装备能力、选择与耐久实例 |
| 修改 | `Source/Catfishing/Save/CatSaveSubsystem.cpp`、`Fishing/CatFishingService.cpp`、`Fishing/Actors/CatFishingRodActor.cpp`、`Character/CatCharacter.cpp` | Save/Fishing/Actor/角色消费者 |
| 修改 | `Source/Catfishing/UI/Inventory/CatInventory{Model,PageController,Widget}.h/.cpp`、`Source/Catfishing/UI/Inventory/CatCampInventoryWidget.h`、`Source/Catfishing/UI/Inventory/CatFishGuardInventoryWidget.h`、`Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h/.cpp` | 每库存 Model、统一打开入口和 Slot 直接提交 RPC |
| 测试 | `Source/Catfishing/Inventory/Tests/CatInventoryInstanceTests.cpp` | 新增约 208 行的库存/拾取契约覆盖 |
| 配置/资产 | `Config/DefaultGame.ini`、5 个旧 Equipment DA | 配置变更和待人工资产引用盘点 |
| 文档 | `Knowledge/Framework/PROJECT_MAP.md`、4 份既有 Docs、`.codex/docs/items-inventory-review.md` | 当前口径与本审查入口 |

机械 include 路径调整（FishContainers、Fishing、Debug、Integration、Tests、GameMode）由 `items-inventory-diff.patch` 列出；它们不定义新状态，审查时以新 `Items/Fish/CatFishPickupActor` 引用是否完整为主。

## 5. 改动点矩阵

| 编号 | 文件与行号 | 符号 | 类型 | 写了什么 | 优先级 | 风险 |
| --- | --- | --- | --- | --- | --- | --- |
| CP-01 | `Inventory/CatInventoryComponent.h:21,58,217,299` | Entry、UseResult、导入导出、Use | 迁移 | 运行槽与回执统一携带 Entry | 高 | 复制、幂等、GC |
| CP-02 | `Inventory/CatInventoryComponent.cpp:765,825,1554,1903` | Export/Restore/Use/held return | 重写 | 严格 Entry 校验和当前组件归还 | 高 | 保存、回滚、容量 |
| CP-03 | `Inventory/CatInventoryItemDefinition.h:35` | `FindFragment<T>` | 新增 | 片段类型化读取 | 中 | 空片段 fail-closed |
| CP-04 | `Equipment/CatEquipmentDefinition.cpp:62,115` | preferred type/CanServe* | 拆分 | 装备能力改查 Fragment | 高 | 旧资产迁移 |
| CP-05 | `Equipment/Fragments/CatEquipmentFragment_Rod.h:11` | 五个 Fragment | 新增 | 静态装备能力分片 | 高 | 默认值和资产 |
| CP-06 | `Equipment/CatEquipmentInventoryItemInstance.cpp:46` | `SetRodRuntimeStateFromAuthority` | 迁移 | 耐久留在装备实例 | 高 | 保存/断竿 |
| CP-07 | `Inventory/CatFishInventoryItemInstance.cpp:56` | `GetSourceFishingSessionId` | 迁移 | 重量、来源、归属网留在鱼实例 | 高 | 鱼容器/吃鱼 |
| CP-08 | `Inventory/CatBackPackComponent.h:10` | backpack component | 新增 | 明确角色随身库存 | 中 | Actor 收货优先级 |
| CP-09 | `Save/CatSaveSubsystem.cpp:127,158,789` | v5 DTO boundary | 迁移 | DTO 边界创建/恢复 Entry+Instance | 高 | 旧存档兼容 |
| CP-10 | `Fishing/CatFishingService.cpp:465` | rod Use result | 迁移 | 从 `UseResult.Item.Instance` 读鱼竿 | 高 | 部署/回滚 |
| CP-11 | `UI/Inventory/CatInventoryModel.h:19`、`Inventory/CatInventoryComponent.cpp:252,2655` | per-inventory Model | 重写 | 每份库存组件持有自己的列表 Model | 高 | 多客户端复制刷新与旧聚合残留 |
| CP-12 | `Items/CatItem.cpp:63` | new Items hierarchy | 新增/迁位 | 通用世界物与鱼拾取目录归属 | 中 | 通用世界物正式 BP/场景接线未验 |

## 6. 单文件改动卡

### CP-01 `Source/Catfishing/Inventory/CatInventoryComponent.h:21` `FCatInventoryEntry / FCatInventoryItemUseResult`

- 模块/层级：Inventory 运行真相与网络命令合同。
- 改动类型：迁移、删除旧 Slot DTO。
- 写了什么：FastArray Entry 仅保存实例、数量、复制观察数量与本地 SlotOwner；Use/UnUse 回执传递同一实例。
- 为什么写：避免槽投影复制鱼/竿领域状态。
- 怎么工作：组件读取 Entry，实例提供领域能力，回执返回同一 Entry。
- 关键输入：实例、StackCount、SlotOwner、RequestId。
- 关键输出：复制 Entry、终态回执。
- 状态读写：`InventoryList`、held map、终态缓存。
- 调用方：Save、Equipment、Fishing、UI Controller。
- 消费方：FishContainers、Environment、库存 Model/WBP。
- 风险与审查重点：确认所有 `Item.Instance` 是反射可保活引用。
- 程序员建议先看：随后看 CP-02 的验证与回滚。

### CP-02 `Source/Catfishing/Inventory/CatInventoryComponent.cpp:765` `Export/Restore/Use/ReturnHeld`

- 模块/层级：Inventory 权威流程。
- 改动类型：重写、清理投影。
- 写了什么：空格只允许 null+0；导入导出校验 ready、stack、实例 ID；held 归还调用当前组件批次入口。
- 为什么写：保存不能静默修复坏 Entry，部署回收不能路由到同 Actor 的另一库存。
- 怎么工作：Restore 临时补齐槽位供 CanAccept 校验后撤销，再 Replace；Use 缓存首个终态。
- 关键输入：Entry 数组、最大容量、RequestId。
- 关键输出：完整替换、失败文本、幂等回执。
- 状态读写：InventoryList、ActiveHeldItemEntries、Transient terminal cache。
- 调用方：Camp/Save、Equipment Use 回调。
- 消费方：复制客户端、Save DTO、Fishing 部署。
- 风险与审查重点：检查临时空 Entry 不广播；检查失败路径不丢 held 实例。
- 程序员建议先看：`RestoreInventorySlotsFromAuthority`、`ReturnHeldInventoryEntryFromAuthority`。

### CP-03 `Source/Catfishing/Inventory/CatInventoryItemDefinition.h:35` `FindFragment<T>`

- 模块/层级：Inventory 定义层。
- 改动类型：新增模板转发。
- 写了什么：编译期片段类型转发至 `FindFragmentByClass`。
- 为什么写：调用方不再重复 StaticClass/Cast。
- 怎么工作：无片段返回空，能力裁决保持 fail-closed。
- 关键输入：FragmentType。
- 关键输出：对应片段指针或 null。
- 状态读写：只读定义 Fragments。
- 调用方：Equipment Definition。
- 消费方：Fishing/Environment 能力判断。
- 风险与审查重点：模板返回非 const 指针，调用方不得写 DataAsset。
- 程序员建议先看：CP-04。

### CP-04 `Source/Catfishing/Equipment/CatEquipmentDefinition.cpp:62` `GetPreferredInstanceType / CanServe*`

- 模块/层级：Equipment 定义能力层。
- 改动类型：迁移、字段拆分。
- 写了什么：显式实例类只接受装备子类；五种能力读取各自 Fragment。
- 为什么写：避免用无关字段零值分类装备。
- 怎么工作：身份、槽位、消耗语义和片段 ready 同时成立才放行。
- 关键输入：Definition 通用字段和 Fragment。
- 关键输出：实例类型、CanServe 判定。
- 状态读写：只读 DataAsset。
- 调用方：EquipmentComponent、Fishing、Chum placement。
- 消费方：实例创建与玩法入口。
- 风险与审查重点：现有资产必须持有正确 Fragment。
- 程序员建议先看：CP-05 和五个旧 DA 风险。

### CP-05 `Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Rod.h:11` `UCatEquipmentFragment_Rod`

- 模块/层级：Equipment 静态配置。
- 改动类型：新增五个文件对。
- 写了什么：Rod 8 字段和 `IsRuntimeReady`，包括物理长度与三组本地锚点。
- 为什么写：每个玩法能力拥有自己的配置和有效性边界。
- 怎么工作：Definition 的 CanServe* 获取指定 Fragment 并调用 ready。
- 关键输入：DataAsset 片段属性。
- 关键输出：能力合法性。
- 状态读写：仅定义静态值。
- 调用方：CP-04 的 `CanServeFishingRod`。
- 消费方：Fishing、装备实例。
- 风险与审查重点：Rod 三个 Transform 与物理长度的锚点约束。
- 程序员建议先看：`CatEquipmentFragment_Rod.cpp:7`。

### CP-05B `Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Bait.h:11` `UCatEquipmentFragment_Bait`

- 模块/层级：Equipment 静态配置。
- 改动类型：新增单文件片段。
- 写了什么：特殊饵标记和两种咬钩倍率。
- 为什么写：饵料语义不能由鱼竿或窝料字段零值推断。
- 怎么工作：`CanServeFishingBait` 查找本片段并调用 ready。
- 关键输入：`bSpecialBait`、两种倍率。
- 关键输出：鱼饵能力合法性。
- 状态读写：只读 DataAsset。
- 调用方：`CatEquipmentDefinition.cpp:123`。
- 消费方：Fishing 等待和失败预算。
- 风险与审查重点：倍率必须为有限正数。
- 程序员建议先看：`CatEquipmentFragment_Bait.cpp:6`。

### CP-05C `Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Float.h:11` `UCatEquipmentFragment_Float`

- 模块/层级：Equipment 静态配置。
- 改动类型：新增单文件片段。
- 写了什么：抛投距离、误差标准差/上限和信号稳定度。
- 为什么写：浮漂参数需要独立的空间约束。
- 怎么工作：`CanServeFishingFloat` 只读取本片段。
- 关键输入：四个 Float 配置值。
- 关键输出：浮漂能力合法性。
- 状态读写：只读 DataAsset。
- 调用方：`CatEquipmentDefinition.cpp:131`。
- 消费方：服务器抛投裁决。
- 风险与审查重点：标准差不得大于误差半径。
- 程序员建议先看：`CatEquipmentFragment_Float.cpp:6`。

### CP-05D `Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Scoop.h:10` `UCatEquipmentFragment_Scoop`

- 模块/层级：Equipment 静态配置。
- 改动类型：新增单文件片段。
- 写了什么：抄网触达距离。
- 为什么写：抄网范围不应混在鱼竿或鱼定义。
- 怎么工作：`CanServeScoopNet` 检查片段有限正距离。
- 关键输入：厘米单位 `ScoopReachCentimeters`。
- 关键输出：抄网能力合法性。
- 状态读写：只读 DataAsset。
- 调用方：`CatEquipmentDefinition.cpp:139`。
- 消费方：捕鱼范围裁决。
- 风险与审查重点：单位是厘米，仍受 Fishing 全局上限。
- 程序员建议先看：`CatEquipmentFragment_Scoop.cpp:6`。

### CP-05E `Source/Catfishing/Equipment/Fragments/CatEquipmentFragment_Chum.h:11` `UCatEquipmentFragment_Chum`

- 模块/层级：Equipment 静态配置。
- 改动类型：新增单文件片段。
- 写了什么：窝料的 `FCatChumInfluenceSpec`。
- 为什么写：水域影响属于窝料能力，不是装备通用字段。
- 怎么工作：`CanServeChumPlacement` 查片段并按 ready 拒绝缺曲线配置。
- 关键输入：半径、时长、三轴贡献、曲线和单次数量。
- 关键输出：窝料投放合法性。
- 状态读写：只读 DataAsset。
- 调用方：`CatEquipmentDefinition.cpp:147`。
- 消费方：`CatChumPlacementService`。
- 风险与审查重点：曲线与 Duration 的资产配置缺失会 fail-closed。
- 程序员建议先看：`CatEquipmentFragment_Chum.cpp:6`。

### CP-06 `Source/Catfishing/Equipment/CatEquipmentInventoryItemInstance.cpp:46` `UCatEquipmentInventoryItemInstance::SetRodRuntimeStateFromAuthority`

- 模块/层级：Equipment 运行实例。
- 改动类型：迁移。
- 写了什么：鱼竿耐久和断竿状态由装备实例保存。
- 为什么写：格子不再承载装备专属运行态。
- 怎么工作：Fishing 读/写同一实例，Save 在边界恢复状态。
- 关键输入：耐久、断竿、实例 ID。
- 关键输出：当前 rod runtime state。
- 状态读写：实例 UObject。
- 调用方：Fishing、Save、EquipmentComponent。
- 消费方：装备选择、断竿处理。
- 风险与审查重点：跨部署/回收保持同一 UObject。
- 程序员建议先看：CP-09、CP-10。

### CP-07 `Source/Catfishing/Inventory/CatFishInventoryItemInstance.cpp:56` `UCatFishInventoryItemInstance::GetSourceFishingSessionId`

- 模块/层级：Fish Inventory 实例。
- 改动类型：迁移。
- 写了什么：重量、FishingSession 来源、OwnerStableNetId 从 Slot 投影移入实例 getter/setter。
- 为什么写：每条鱼不可堆叠，状态与实例身份不可分。
- 怎么工作：吃鱼、售鱼、容器与 UI 先 Cast 鱼实例再读取字段。
- 关键输入：捕获时写入的鱼运行态。
- 关键输出：鱼领域只读数据。
- 状态读写：鱼实例 UObject。
- 调用方：Fishing、FishContainers、Save。
- 消费方：食用、展示和归属逻辑。
- 风险与审查重点：Cast 失败必须拒绝，不得回退定义字段。
- 程序员建议先看：`CatFishInventoryItemInstance.cpp` 的 getter/setter。

### CP-08 `Source/Catfishing/Inventory/CatBackPackComponent.h:10` `UCatBackPackComponent`

- 模块/层级：角色随身 Inventory。
- 改动类型：新增。
- 写了什么：独立标识角色背包的 InventoryComponent 子类。
- 为什么写：背包不再作为 Character 中的匿名通用库存。
- 怎么工作：构造期配置可复制背包与统一收货优先级。
- 关键输入：Actor 生命周期与组件配置。
- 关键输出：可供 InventoryStatics 解析的背包组件。
- 状态读写：继承 InventoryList。
- 调用方：Character 组件装配。
- 消费方：Shop、UI、跨库存移动。
- 风险与审查重点：与营地公共库存候选优先级的选择。
- 程序员建议先看：`CatCharacter.cpp` 装配点。

### CP-09 `Source/Catfishing/Save/CatSaveSubsystem.cpp:127` `v5 DTO boundary`

- 模块/层级：Save 兼容边界。
- 改动类型：迁移，磁盘格式保留。
- 写了什么：保留 `FCatSavedRunInventorySlot`，从 Entry 导出并在恢复边界创建实例、恢复 ID 和 rod state。
- 为什么写：既有 v5 存档不可被内部类型收敛破坏。
- 怎么工作：Prepare 准备 Entry 后交 Component Replace/Restore；保存时从实例读取鱼竿状态。
- 关键输入：v5 Slot、DefinitionId、InstanceId、RodDurability。
- 关键输出：准备好的 Entry 数组或 DTO。
- 状态读写：SaveGame DTO 与实例字段。
- 调用方：请求保存/恢复链。
- 消费方：Camp/Character Inventory。
- 风险与审查重点：磁盘 DTO 没有静默丢鱼字段的路径。
- 程序员建议先看：`PrepareInventoryEntriesFromSave:158`、`SetItemInstanceId:194`、`SetRodRuntimeState:197`。

### CP-10 `Source/Catfishing/Fishing/CatFishingService.cpp:465` `UseResult.Item.Instance`

- 模块/层级：Fishing 消费者。
- 改动类型：迁移。
- 写了什么：Use 回执直接 Cast 装备实例读取冻结/耐久状态。
- 为什么写：不再通过 FCatRunInventorySlot 取鱼竿数据。
- 怎么工作：库存提交 Use 后，Fishing 只从回执 Entry 的实例继续部署或回滚。
- 关键输入：UseResult、Item.Instance。
- 关键输出：鱼竿会话状态。
- 状态读写：库存 held entry、Fishing session。
- 调用方：Fishing Use 流程。
- 消费方：Rod Actor、失败回滚。
- 风险与审查重点：回执重放不应再次部署。
- 程序员建议先看：CP-01 的终态 cache。

### CP-11 `Source/Catfishing/UI/Inventory/CatInventoryModel.h:19` / `Source/Catfishing/Inventory/CatInventoryComponent.cpp:252` `UCatInventoryModel`

- 模块/层级：UI Model 与库存组件边界。
- 改动类型：重写、删除旧聚合投影。
- 写了什么：`UCatInventoryComponent` 懒创建并持有自己的 `UCatInventoryModel`；组件提交或收到复制后写入 `InventoryList` 并广播本地 UI 通知。
- 为什么写：当前 MVC 方向要求每份库存独立显示和通知，不能再由一个 Model 聚合背包、外部容器、营地仓库和 pending 结果。
- 怎么工作：`GetInventoryModel()` 创建 Model 并写当前 `InventoryList.Entries`；后续 `BroadcastInventoryChange()` 调用 `SetInventoryList()`，绑定该 Model 的 Widget 重新读取列表。
- 关键输入：`FCatInventoryEntry` 数组、复制回调、服务器库存 mutation。
- 关键输出：某一份库存自己的显示列表和 `OnInventoryListChanged` 通知。
- 状态读写：库存组件写 Model；Widget 只读 Model；Model 不保存服务器 pending、不保存 SlotView、不保存其他库存列表。
- 调用方：`UCatInventoryComponent::GetInventoryModel()`、`BroadcastInventoryChange()`。
- 消费方：`UCatInventoryWidget::SetInventoryContext()`、`RefreshInventorySlots()`。
- 风险与审查重点：旧文档或 WBP 若仍期待已删除的库存 UI 投影类型或三数组，会直接失效；审查时先确认资产父类和蓝图节点是否已迁移。
- 程序员建议先看：`CatInventoryModel.h:19-30`、`CatInventoryComponent.cpp:252-2660`。
### CP-12 `Source/Catfishing/Items/CatItem.cpp:63` `ACatItem / ACatFishPickupActor`

- 模块/层级：世界物品目录。
- 改动类型：新增通用世界物，并把 Fish pickup 纯 C++ 文件迁入 Items/Fish。
- 写了什么：新增通用 `ACatItem`、`ACatEquipmentItem`；`ACatFishPickupActor` 只从 FishContainers/World 迁到 `Items/Fish/`，反射类名和嘴叼鱼玩法保持。
- 为什么写：世界物品类型按自身领域归类，同时不把鱼拾取玩法改造成空包装。
- 怎么工作：`ACatItem::Interact_Implementation` 按 RequestId、距离和重入锁把 ReceiveBatch 提交给 InventoryStatics，成功后销毁；Fish pickup 继续走原嘴叼/鱼护链路。
- 关键输入：RequestId、交互者、PickupBatch、鱼 pickup 的 PresentationState。
- 关键输出：通用拾取提交/销毁结果，或既有鱼 pickup 状态转换。
- 状态读写：`ACatItem::bPickupClaimed` 防重入；Fish pickup 继续写自己的展示/携带状态，不新增第二份库存状态。
- 调用方：Interaction RPC、当前 C++ include 和正式资产候选。
- 消费方：Inventory batch、既有 Fish spawn/配置、正式 BP/场景。
- 风险与审查重点：通用 `ACatItem` fixture 已过；正式 BP/场景是否采用新通用世界物仍未验证。Fish pickup 保留类名，不要求重设 native 父类。
- 程序员建议先看：`CatItem.cpp:63-95`、`CatFishPickupActor.cpp:616-661`。

### CP-13 `Source/Catfishing/Equipment/CatEquipmentComponent.cpp:285` `UCatEquipmentComponent` Entry 查询

- 模块/层级：Equipment 读模型与选择。
- 改动类型：实现迁移。
- 写了什么：装备查询改直接读取正式 Entry 和装备实例。
- 为什么写：移除运行 Slot 投影。
- 怎么工作：按实例 ID 定位 Entry，Cast 装备实例读取耐久。
- 关键输入：InventoryComponent、实例 ID。
- 关键输出：LoadoutSnapshot 与冻结选择。
- 状态读写：只读库存，写 Equipment 选择。
- 调用方：Character/Fishing。
- 消费方：Fishing、UI。
- 风险与审查重点：冻结选择与移仓后的失效清理。
- 程序员建议先看：`CatEquipmentComponent.cpp:465,815,1254`。

### CP-14 `Source/Catfishing/UI/Inventory/CatInventoryPageController.cpp:48` `OpenInventory`

- 模块/层级：UI 窗口生命周期与输入。
- 改动类型：重写、职责收口。
- 写了什么：PageController 只绑定默认背包页、打开调用方指定库存页、安装/移除库存输入，并维护唯一 `bInventoryOpen` 状态。
- 为什么写：移动和使用已经下沉到 Slot 直接提交 owning PlayerController，PageController 不应继续转译 SlotView、维护 pending 或按页面类型生成服务器命令。
- 怎么工作：普通 `Bind()` 给默认背包注入角色库存；世界对象调用 `OpenInventory(Inventory, ViewClass)` 时先关闭旧页、创建指定 WBP、注入该库存和格子类，再加入视口并申请输入锁。
- 关键输入：本地 `APlayerController`、默认背包 View、调用方传入的 `UCatInventoryComponent` 与 WBP 类。
- 关键输出：当前打开的库存页面、模态输入状态和 `bInventoryOpen`。
- 状态读写：写 `BoundController`、`BoundView`、`DefaultInventoryView`、输入绑定句柄和输入模式恢复记录；不写库存 Model 或命令 pending。
- 调用方：`UCatLocalPlayerUISubsystem::ToggleInventory()`、`OpenInventory()`。
- 消费方：`UCatInventoryWidget::ShouldCloseInventoryFromKey()`、局内菜单打开状态判断。
- 风险与审查重点：关闭交互页后必须恢复默认背包引用；输入绑定重复刷新必须先移除旧句柄；不要把旧 PageController 命令转发入口写回。
- 程序员建议先看：`CatInventoryPageController.cpp:14-70`、`:96-158`。

### CP-15 `Source/Catfishing/UI/Inventory/CatInventoryWidget.cpp:17` / `:85` `SetInventoryContext / RefreshInventorySlots`

- 模块/层级：UI View。
- 改动类型：重写、删除旧 ViewState 消费。
- 写了什么：Widget 绑定一份 `DisplayInventory`，订阅该库存 Model 的变化，刷新时按 Model 列表创建格子并传入库存、槽位和 Entry。
- 为什么写：同屏背包、营地和鱼护必须各自显示独立库存，不能由父页把一份聚合 ViewState 再分发给子页。
- 怎么工作：`SetInventoryContext()` 先解绑旧 Model，再绑定新 Model 通知并刷新；`NativeConstruct()` 在未注入外部库存时回退到 owning Pawn 背包；`RefreshInventorySlots()` 清空旧格、清选择、读取当前 Model 列表并创建 Slot。
- 关键输入：`UCatInventoryComponent`、`UCatInventoryModel::InventoryList`、格子 WBP 类。
- 关键输出：当前页面的 SlotWidget 列表、选中高亮、使用按钮启用状态。
- 状态读写：写 `DisplayInventory`、`InventoryModelChangedHandle`、`SlotWidgets`、`SelectedSlotIndex`；不写服务器库存事实。
- 调用方：PageController 默认绑定和统一 `OpenInventory()`。
- 消费方：WBP 根页、SlotWidget、关闭键处理。
- 风险与审查重点：旧资产若仍依赖库存页旧渲染事件或旧完整视图读取函数，当前 C++ 不会调用；嵌套普通背包页只能靠 owning Pawn 自行解析背包。
- 程序员建议先看：`CatInventoryWidget.cpp:17-32`、`:41-65`、`:85-116`。

### CP-16 `Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.cpp:15` / `:43` / `:142` `SetSlotContext / RequestUseItem / NativeOnDrop`

- 模块/层级：UI Slot 与 RPC 提交边界。
- 改动类型：重写、职责下沉。
- 写了什么：Slot 持有 `SourceInventory`、`SlotIndex` 和显示用 `FCatInventoryEntry`；右键/按钮使用与拖放都由 Slot 直接调用 owning PlayerController 的服务器 RPC。
- 为什么写：当前槽位身份已经是“库存宿主 + 宿主内槽位”，无需旧槽位视图、来源枚举或 PageController 再做一次转发复核。
- 怎么工作：父 Widget 创建格子时调用 `SetSlotContext()`；右键或父页按钮进入 `RequestUseItem()` 提交 `ServerUseInventoryItemFromHost()`；Drop 目标固定源/目标库存宿主与槽位后提交 `ServerMoveInventoryItemBetweenHosts()`，服务器从两个库存组件当前内容重读。
- 关键输入：源库存组件、源槽位、目标库存组件、目标槽位、显示 Entry。
- 关键输出：使用或移动 RPC 请求，及用于蓝图表现的 `BP_InitializeSlot()`。
- 状态读写：Slot 只写本地显示副本和 `bSelected`；服务器 mutation 和复制刷新仍由 InventoryComponent/Model 回来驱动 UI。
- 调用方：`UCatInventoryWidget::RefreshInventorySlots()`、鼠标事件、`RequestUseSelectedItem()`。
- 消费方：`ACatfishingPlayerController::ServerUseInventoryItemFromHost_Implementation()`、`ServerMoveInventoryItemBetweenHosts_Implementation()`。
- 风险与审查重点：WBP 不能继续绑定旧格子渲染事件或旧格子视图读取函数；拖放提交后本格可能同步被刷新，提交前固定参数是必要边界。
- 程序员建议先看：`CatInventorySlotWidget.h:37-101`、`CatInventorySlotWidget.cpp:15-33`、`:43-54`、`:142-164`。
### CP-17 `Source/Catfishing/Inventory/CatInventoryItemInstance.cpp:160` `CanUseFromInventory`

- 模块/层级：Inventory 实例语义。
- 改动类型：实现迁移。
- 写了什么：实例接收 Entry 的通用可用性判断。
- 为什么写：对齐 AO 实例 CanUse/TryUse 模式。
- 怎么工作：库存先验证 Entry，再让具体子类决定消费或部署。
- 关键输入：Entry、UserPawn。
- 关键输出：可用性/消费决定。
- 状态读写：实例或 Entry。
- 调用方：Inventory Use。
- 消费方：鱼/装备子类。
- 风险与审查重点：基类不应猜测领域状态。
- 程序员建议先看：CP-06、CP-07。

### CP-18 `Source/Catfishing/Items/CatEquipmentItem.cpp:23` `ACatEquipmentItem::GetPickupInventory`

- 模块/层级：世界装备拾取。
- 改动类型：新增实现。
- 写了什么：将装备定义物化为 ReceiveBatch。
- 为什么写：世界拾取不创建第二份库存真相。
- 怎么工作：Actor 交互交由当前库存批次入口。
- 关键输入：EquipmentDefinition。
- 关键输出：ReceiveBatch。
- 状态读写：Actor 静态拾取配置。
- 调用方：`ACatItem::Interact_Implementation:63`。
- 消费方：Inventory batch。
- 风险与审查重点：原 fixture 在 BeginPlay 前失败；后续 `pickup-final-retest.log:1989-1996` 覆盖满包拒绝、重入拒绝和唯一提交成功并通过。正式 BP/场景是否已改用 `ACatItem` 仍需资产侧核对；`ACatFishPickupActor` 保留反射类名和既有鱼 spawn/配置，不要求重设父类。
- 程序员建议先看：`CatItem.cpp:37,63`。

### CP-19 `Source/Catfishing/Character/CatCharacter.cpp:29` `CreateDefaultSubobject<UCatBackPackComponent>`

- 模块/层级：Character 装配。
- 改动类型：实现迁移。
- 写了什么：角色库存实例化为 BackPack 子类。
- 为什么写：把随身库存与公共仓库的归属显式化。
- 怎么工作：BeginPlay 附近 `:196` 初始化配置容量。
- 关键输入：Character 生命周期。
- 关键输出：玩家正式背包组件。
- 状态读写：组件创建和容量。
- 调用方：Character 构造。
- 消费方：Shop/UI/InventoryStatics。
- 风险与审查重点：构造时类替换的资产/蓝图继承。
- 程序员建议先看：CP-08。

### CP-20 `Source/Catfishing/Items/Fish/CatFishPickupActor.cpp:53` `BeginPlay`

- 模块/层级：世界鱼拾取表现。
- 改动类型：目录迁位和实现修正。
- 写了什么：迁至 Items/Fish，BeginPlay 初始化展示/交互。
- 为什么写：世界 Actor 按 Items 领域归类。
- 怎么工作：Authority 初始化、复制展示、交互提交捕获。
- 关键输入：鱼实例/会话 ID。
- 关键输出：可交互的世界鱼 Actor。
- 状态读写：复制展示状态和附着。
- 调用方：Fishing 完成链。
- 消费方：FishContainer/Character。
- 风险与审查重点：正式 BP/场景路径仍无证据。
- 程序员建议先看：`InitializeFromAuthority:96`、`Interact_Implementation:675`。

### CP-21 `Source/Catfishing/Fishing/Actors/CatFishingRodActor.cpp:142` `ConfigureCanonicalAnchorsFromAuthority`

- 模块/层级：Fishing 世界鱼竿 Actor。
- 改动类型：实例/片段消费者衔接。
- 写了什么：从 Rod Fragment 配置三组初始复制锚点。
- 为什么写：竿尖、架竿和握持空间数据不再来自运行 Slot。
- 怎么工作：身份初始化前写 canonical transform，初始复制后客户端应用。
- 关键输入：Rod Tip/Stand/Grip 本地 Transform。
- 关键输出：权威世界锚点与表现状态。
- 状态读写：InitialOnly transform、Actor presentation revision。
- 调用方：Fishing 部署鱼竿。
- 消费方：线约束、角色表现、客户端展示。
- 风险与审查重点：锚点必须在身份初始化前写；检查 3 字段均复制。
- 程序员建议先看：`CatFishingRodActor.cpp:72,80,93` 和 `anchor-retest.log:2100-2108`。

## 7. 工作流程

1. **库存 Use**：Slot 的 `RequestUseItem()`（CP-15）直接提交宿主+槽位 RPC -> CP-01 `UseItemInstanceFromAuthority` -> CP-02 终态缓存/扣量或 held 转移 -> CP-10 读取 `Result.Item.Instance` -> Fishing 或鱼消费提交自身副作用；失败不重复扣量。
2. **保存/恢复**：Save v5 DTO -> CP-09 准备同一实例 Entry -> CP-02 Restore 校验容器 -> `ReplaceInventoryEntriesFromAuthority`；反向导出时 CP-02 持有 held 归位后，CP-09 仅负责 DTO。
3. **静态能力与运行状态**：Definition `FindFragment<T>`（CP-03）-> `CanServe*`（CP-04）-> 五片段 ready（CP-05）；会话内变化写 CP-06/CP-07 的实例，而非定义或槽。

## 8. 建议阅读顺序

1. CP-01/CP-02：先确认 Entry、复制、幂等和 held 所有权。
2. CP-09：确认 v5 磁盘边界没有改格式。
3. CP-04/CP-05/CP-06：确认资产片段和鱼竿实例状态完整。
4. CP-07/CP-10：确认鱼与 Fishing 消费者没有投影残留。
5. CP-11/CP-14/CP-15/CP-16：最后审库存 UI 的每库存 Model、窗口入口、Slot RPC 与正式 WBP 父类接线风险；CP-12 再看世界资产接线。

## 9. 风险与审查重点

- Save：v5 DTO 保留是兼容选择；普通库存鱼在 `CatSaveSubsystem.cpp:138-142` 会显式拒绝保存，恢复侧 `:185-188` 也拒绝鱼实例类，避免 v5 丢失重量与来源载荷。
- 网络/幂等：终态 cache 现在持有 Entry，需要确认 RequestId 生命周期和 GC 行为。
- 资产：旧 5 个 Equipment DA 已做项目内硬/软 AssetRegistry 引用检查且 referencers 为空；项目外消费者仍未确认，暂留。
- 世界拾取：`ACatItem` C++ fixture 已覆盖满包拒绝、重入拒绝和唯一提交；正式 `ACatItem` BP/场景接线仍未验证。`ACatFishPickupActor` 只是纯 C++ 迁目录并保留类名，既有鱼 spawn/配置不要求重设父类。
- UI：当前代码已无旧库存 UI 投影文件和三数组聚合 ViewState；旧 WBP 如果仍绑定库存页旧渲染事件、旧格子视图读取函数或 PageController 右键/移动入口，需要资产侧迁到 `GetInventoryEntry`、`RequestUseItem` 和 C++ 鼠标事件链。本报告不包含 UI 实玩路径验收。

## 10. 设计取舍

Entry+Instance 复用 AO 的“格子只存实例与数量、实例拥有可用性/运行态”主线；库存 UI 只适配 AO 的“Model 提供列表、Widget 建格、Slot 持有上下文并提交操作”边界，没有照搬 AO 的完整 UI 资产结构。Catfishing 仍保留 RequestId 回执、存档 DTO、四槽 Equipment 玩法和正式营地/鱼护 WBP 父类校验。代价是 Save、UI、Fishing 都需要显式 Cast 实例；收益是去掉运行 Slot 投影、聚合 Model、PageController 转发命令和重复所有权。

| AO 真实位置 | AO 符号与行号 | Catfishing 对应位置 | Catfishing 符号与行号 | 对照结论 |
| --- | --- | --- | --- | --- |
| `Inventory/AOInventoryComponent.h` | `FAOInventoryEntry:27` | `Inventory/CatInventoryComponent.h` | `FCatInventoryEntry:21` | 两者均为 FastArray Entry；Cat 增加本地 SlotOwner。 |
| `Items/AOItem.cpp` | `ExecuteInteraction:48-63`、`GetPickUpInventory:69` | `Items/CatItem.cpp` | `Interact_Implementation:63-95`、`GetPickupInventory:31` | 两者均由世界物把拾取载荷交给库存链并在成功后销毁；Cat 增加 RequestId、服务端 RPC 转发、距离校验和重入锁。 |
| `Items/AOEquipmentItem.cpp` | `GetPickUpInventory:25-39` | `Items/CatEquipmentItem.cpp` | `GetPickupInventory:23-34` | 两者均由装备世界物按 Definition 生成 1 个入库条目；Cat 使用 DataAsset 实例和 `GetPreferredInstanceType()`。 |
| `Inventory/AOInventoryItemInstance.cpp` | `CanUseFromInventory:43`、`TryUseFromInventory:48` | `Inventory/CatInventoryItemInstance.cpp` | `CanUseFromInventory:160` | 使用语义由实例接收 Entry；Cat 以 UseResult 保留网络回执。 |
| `Equipment/AOEquipmentInstance.cpp` | `CanUseFromInventory:144`、`TryUseFromInventory:149` | `Equipment/CatEquipmentInventoryItemInstance.cpp` | `CanUseFromInventory:134` | 装备实例而非格子持有专属使用状态。 |
| `Inventory/AOInventoryItemDefinition.h` | `FindFragmentByClass:50,64-73` | `Inventory/CatInventoryItemDefinition.h` | `FindFragmentByClass:35-43` | 均提供模板化 Fragment 查询；Cat 名称简化为 `FindFragment<T>`。 |
| `Inventory/AOBackPackComponent.cpp` | `UAOBackPackComponent` 构造入口 | `Inventory/CatBackPackComponent.cpp` | `UCatBackPackComponent:6` | 角色背包从通用库存中显式分型。 |

## 11. 影响范围

- 已确认影响：Inventory、Equipment、Fish、Save、Fishing、Camp、Environment 与 UI Inventory 的编译期消费者。
- 可能影响：WBP、场景中旧类路径引用，以及项目外仍持有旧 DA 的消费者。
- 未确认影响：五个旧 DA 的项目外消费者，以及新 `ACatItem/ACatEquipmentItem` 的正式 BP/场景接线。

## 12. 未改但相关区域

`FCatSavedRunInventorySlot` 继续是 v5 磁盘 DTO；Equipment 四槽选择、UI Action 回执和 Actor 外层复制身份未在本轮按猜测重构。`Docs/Development/非钓鱼核心功能待办.md` 是并行无关文件，排除。

## 13. 最小充分实现审查

删除了 RuntimeTypes、Definition Normalize/Build/Restore Slot 投影与重复 held/use 记录；保留的 Entry、实例子类、五 Fragment、v5 DTO、Action 回执各自承担唯一状态或兼容职责。没有新增万能 DTO。

## 14. 残留代码审查

| 候选 | 结论 | 依据与后续条件 |
| --- | --- | --- |
| `CatInventoryRuntimeTypes.h` / `FCatRunInventorySlot` | 删除 | 当前 Source 搜索无运行引用。 |
| v5 `FCatSavedRunInventorySlot` | 保留 | 磁盘格式兼容；仅 Save 边界使用。 |
| 5 个旧 Equipment DA 资产 | 暂留 | 项目内硬/软 AssetRegistry referencers 为空；项目外消费者未确认，需人工确认后再删。 |
| `ACatItem` 世界拾取接线 | 保留，资产接线待确认 | C++ fixture 已由 `pickup-final-retest.log:1989-1996` 覆盖；正式 BP/场景和项目外引用未确认。 |

## 15. 修复入口

- Entry/回滚/容量：`Source/Catfishing/Inventory/CatInventoryComponent.cpp:765,825,1903`。
- 静态装备能力：`Source/Catfishing/Equipment/CatEquipmentDefinition.cpp:115-152` 和 `Equipment/Fragments/`。
- 存档兼容：`Source/Catfishing/Save/CatSaveSubsystem.cpp:127-197`。
- WBP 使用入口：`Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.cpp:43` 的 `RequestUseItem()`；拖放入口是同文件 `:142` 的 `NativeOnDrop()`；窗口入口是 `Source/Catfishing/UI/Inventory/CatInventoryPageController.cpp:48` 的 `OpenInventory()`。
- 资产迁移：五个旧 DA 的项目外消费者、`ACatItem/ACatEquipmentItem` 的正式 BP/场景接线；`Items/Fish/CatFishPickupActor` 保留反射类名和既有鱼玩法接线。

## 16. Review 断点与待人工审查项

1. 最终 Editor/Game 构建均成功：`final-editor-build.log` 与 `final-game-build.log` 内均可用 `Result: Succeeded` 定位；当前快照分别在 `:116`、`:115`。这是编译证据，不替代打包双端、UI 实玩或资产接线验收。
2. 注释最终扫描通过：`final-comment-scan.log:1` 为 pass，67 文件、2249 注释；C1-C119 候选已写入隔离 state 的 decisions，未手动伪造自动扫描结果。
3. Fragment 资产重存 23 后再加载：`fragment-final-check.log:1646` 为 matched=23、blueprints=10，`:1650` 为 0 error、0 warning。
4. `final-inventory-equipment-tests.log` 跑完 12 tests：`:2569-2571` 记录 `EquipmentItemPickupRejectsFullBagAndPreventsReentrantDoubleGrant` fixture fail，`:2610` 队列完成。该失败由后续 `pickup-final-retest.log:1989-1996` 覆盖：满包拒绝、重入拒绝、唯一提交成功，`:2006` 单项队列完成。
5. 原 `integrated-tests.log:2159/2183/2195` 的 listen-client anchor 失败由 `anchor-retest.log:2084` 成功覆盖；`:2100` Native 与 `:2108` FormalBlueprint 的 Tip/Stand/Grip/Visual 误差均为 0。
6. 原综合测试中 StarterRod 旧 150 期望与资产 500 的冲突不在本轮改资产解决；最终 review 以保留基线资产值为边界。
7. 未验证范围：未做 packaged 双端联机实跑，未做 UI 实玩路径验证，未证明新 `ACatItem/ACatEquipmentItem` 已成为正式世界资产/BP 父类，未证明五个旧 DA 的项目外引用可删除。
8. Review 结论：本文可作为程序员代码审查入口；不作为验收完成或上线证明。



