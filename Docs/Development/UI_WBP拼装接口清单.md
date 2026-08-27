# UI WBP 拼装接口清单

文档状态：当前代码核对版（2026-08-27）

范围：这份文档只说明当前项目给 WBP 预留了哪些父类、控件名、蓝图事件、蓝图可调用函数和只读数据。它用于手工重做 UI 样式，不作为验收文档，也不规定最终美术风格。

## 总原则

当前 UI 采用“C++ 管状态，WBP 管表现”的方式。WBP 可以随便改布局、颜色、字体和动效，但不要绕过 C++ 直接改背包、商店、公款、鱼护或角色状态。

`BindWidgetOptional` 表示控件不是强制存在。控件存在且名字对上时，C++ 会自动写文本或绑定按钮；控件不存在时，WBP 仍可以通过 `BP_Render...` 事件和 `GetLast...ViewState()` 自己做动态表现。

按钮点击不要自己写后端逻辑。背包、商店这类页面已经提供 `Request...` 函数，蓝图按钮只需要调用这些函数，把“玩家想做什么”交给 PageController 和服务器处理。

模态页面打开后会切到 UIOnly 输入模式，并把焦点交给当前 Widget。根 WBP 必须保持继承正确；商店、背包关闭应走对应 `RequestClose...` 函数，不要在蓝图里直接 `RemoveFromParent`。

旧的一体化页面 `Content/UI/WBP_CatLakeReach.uasset` 不是当前正式拼装入口。现在正式入口是下面这些拆分 WBP。

## 正式 WBP 入口

| WBP 资产 | 父类 | 用途 | 谁创建 |
| --- | --- | --- | --- |
| `/Game/UI/HUD/WBP_CatHUD` | `UCatHUDWidget` | 局内状态 HUD，显示猫状态和钓鱼反馈 | `UCatLocalPlayerUISubsystem` 启动局内 UI 时创建 |
| `/Game/UI/Inventory/WBP_CatInventory` | `UCatInventoryWidget` | 默认背包页面，显示随身库存、当前钓鱼选择和选中详情 | `UCatLocalPlayerUISubsystem` 创建，`UCatInventoryPageController` 打开 |
| `/Game/UI/Inventory/WBP_CatFishGuardInventory` | `UCatFishGuardInventoryWidget` | 鱼护箱子页面，只显示本次交互到的地面鱼护容器格 | `ACatFishGuardActor` 提供页面类，`UCatInventoryPageController` 按需创建 |
| `/Game/UI/InventorySlot/WBP_CatInventorySlot` | `UCatInventorySlotWidget` | 背包单个格子，负责显示占用、选中、拖拽和 Drop | `UCatInventoryWidget` 重建格子列表时动态创建 |
| `/Game/UI/Shop/WBP_CatShop` | `UCatShopWidget` | 世界商店页面，显示商品、公款、购买和领取反馈 | `UCatShopInteractionComponent` 在靠近商店交互时创建 |
| `/Game/UI/Interaction/WBP_CatInteractionPrompt` | `UCatInteractionPromptWidget` | 靠近对象时的“按键交互”提示 | `UCatLocalPlayerUISubsystem` 启动局内 UI 时创建 |
| `/Game/UI/Collection/WBP_CatCollection` | `UCatCollectionWidget` | 图鉴/相册只读页面 | 当前已有配置和渲染接口；当前代码未看到完整打开入口 |

这些默认路径大多来自 `Source/Catfishing/UI/CatUISettings.cpp`。鱼护箱子页面类跟随 `ACatFishGuardActor` 自己的 `InventoryViewClass`，以后新增世界库存对象也应由对象自己提供页面类或打开上下文，不在 `UCatLocalPlayerUISubsystem` 里继续加专用字段。

## HUD：`WBP_CatHUD`

源码入口：`Source/Catfishing/UI/HUD/CatHUDWidget.h`

父类必须是 `UCatHUDWidget`。它只负责显示，不提供按钮。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `CatStatusTextBlock` | `TextBlock` | 猫当前状态摘要，比如毒值、钓鱼力量、体力、湿身/倒地/成长信息。 |
| `FishingFeedbackTextBlock` | `TextBlock` | 当前钓鱼反馈，比如会话阶段、鱼的状态、最近一次命令结果。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderHUD(ViewState)` | 蓝图事件 | 每次 HUD 数据刷新时触发。想做动画、分区排版、图标变化，用这个事件。 |
| `GetLastHUDViewState()` | 蓝图纯函数 | 读取最近一次 HUD 数据。适合绑定文本、进度条、显隐状态。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `Poison` | 当前毒值，只展示，不在 UI 里裁决倒地。 |
| `FishingStrength` | 当前钓鱼力量，只展示。 |
| `FightStamina` | 当前搏斗体力，只展示。 |
| `Condition` | 湿身、倒地、恢复等状态快照。 |
| `Growth` | 成长经验和待选次数快照。 |
| `Fishing` | 当前钓鱼会话投影。 |
| `CatStatusText` | C++ 已经整理好的猫状态文本。 |
| `FishingFeedbackText` | C++ 已经整理好的钓鱼反馈文本。 |

## 默认背包：`WBP_CatInventory`

源码入口：`Source/Catfishing/UI/Inventory/CatInventoryWidget.h`

父类必须是 `UCatInventoryWidget`。它是背包主页面，不直接改后端库存。普通打开时使用这个页面。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `InventorySlotWrapBox` | `WrapBox` | 默认格子容器。C++ 会按 `Slots` 数组自动往里面塞 `WBP_CatInventorySlot`。 |
| `CloseButton` | `Button` | 关闭背包。存在时 C++ 自动绑定到 `RequestCloseInventory()`。 |
| `ConsumeFishButton` | `Button` | 吃掉当前选中鱼。存在时 C++ 自动绑定到 `RequestConsumeSelectedFish()`。 |
| `SacrificeFishButton` | `Button` | 献祭当前选中鱼。存在时 C++ 自动绑定到 `RequestSacrificeSelectedFish()`。 |
| `SummaryTextBlock` | `TextBlock` | 背包总览文本，适合先做调试/临时摘要。 |
| `EquipmentTextBlock` | `TextBlock` | 当前鱼竿、鱼饵、鱼漂、耐久等钓鱼选择摘要。 |
| `InventoryItemsTextBlock` | `TextBlock` | 随身库存格概要。 |
| `SelectedFishTextBlock` | `TextBlock` | 当前选中格说明。名字里有 Fish，但现在也会展示非鱼物体摘要。 |
| `ResultTextBlock` | `TextBlock` | 最近一次吃鱼、献祭、移动、选择钓具的反馈。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderInventory(ViewState)` | 蓝图事件 | 背包数据刷新时触发。复杂布局、动态列表、详情面板可以从这里更新。 |
| `GetLastInventoryViewState()` | 蓝图纯函数 | 读取最近一次背包完整数据。 |
| `RequestCloseInventory()` | 蓝图可调用 | 请求关闭背包。 |
| `RequestSelectSlot(SlotIndex)` | 蓝图可调用 | 请求选中某个显示格。 |
| `RequestConsumeSelectedFish()` | 蓝图可调用 | 请求吃掉当前选中鱼。服务器会复核，不是 UI 直接删鱼。 |
| `RequestSacrificeSelectedFish()` | 蓝图可调用 | 请求献祭当前选中鱼。服务器会复核。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `Slots` | 当前页面所有格子的显示数组。每个格子会生成一个 `WBP_CatInventorySlot`。 |
| `SlotCount` | 当前应该显示多少格。 |
| `Containers` | 本次打开背包纳入展示的鱼护/外部容器列表。 |
| `bHasExternalContainers` | 是否带有外部容器。打开鱼护箱子时通常为 true。 |
| `Equipment` | 当前随身库存和当前钓鱼选择快照。 |
| `SelectedSlotIndex` | 当前选中的显示格下标。 |
| `bHasSelectedFish` | 当前是否选中一条鱼。 |
| `bSelectedFishInFishGuard` | 当前选中鱼是否来自本次打开的地面鱼护。吃鱼/献祭只应该看这个条件。 |
| `bCanSubmitAction` | 当前是否允许提交吃鱼/献祭等按钮动作。 |
| `bActionPending` | 是否已有请求等待服务器结果。pending 时按钮应表现为不可重复提交。 |
| `SummaryText` | C++ 整理好的背包总览文本。 |
| `EquipmentText` | C++ 整理好的当前钓鱼选择文本。 |
| `InventoryItemsText` | C++ 整理好的随身库存文本。 |
| `SelectedFishText` | C++ 整理好的当前选择文本。 |
| `ResultText` | C++ 整理好的最近结果文本。 |
| `ToggleKeyName` | 当前背包开关键名，来自正式输入资产，不在 WBP 里写死。 |

## 鱼护箱子：`WBP_CatFishGuardInventory`

源码入口：`Source/Catfishing/UI/Inventory/CatFishGuardInventoryWidget.h`

父类必须是 `UCatFishGuardInventoryWidget`。它就是打开地面鱼护时显示的箱子库存页，不再要求里面嵌玩家背包 WBP，也不再需要另一个鱼护箱子 WBP。鱼护 Actor 把这张页面类交给通用库存打开入口；C++ 会从同一份库存 Model 投影里只留下本次交互到的地面鱼护容器格，再交给普通库存渲染流程。拖拽、选择、吃鱼和献祭仍然走同一个 PageController 和服务器复核链路。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `InventorySlotWrapBox` | `WrapBox` | 鱼护箱子格子容器。C++ 只会往里面塞本次地面鱼护容器的格子。 |
| `CloseButton` | `Button` | 关闭整个鱼护交互界面。存在时 C++ 自动绑定。 |
| `ConsumeFishButton` | `Button` | 吃掉当前鱼护中选中的鱼。服务器会复核，不是 UI 直接删鱼。 |
| `SacrificeFishButton` | `Button` | 献祭当前鱼护中选中的鱼。服务器会复核。 |
| `SummaryTextBlock` | `TextBlock` | 鱼护箱子总览文本。 |
| `SelectedFishTextBlock` | `TextBlock` | 当前选中的箱子格说明。 |
| `ResultTextBlock` | `TextBlock` | 最近一次移动、吃鱼、献祭或拒绝反馈。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderInventory(ViewState)` | 蓝图事件 | 鱼护箱子数据刷新时触发。传入的 `Slots` 已经只剩地面鱼护容器格。 |
| `GetLastInventoryViewState()` | 蓝图纯函数 | 读取最近一次鱼护箱子数据。 |
| `RequestCloseInventory()` | 蓝图可调用 | 请求关闭鱼护交互界面。 |
| `RequestSelectSlot(SlotIndex)` | 蓝图可调用 | 请求选中某个箱子格，`SlotIndex` 仍是库存 Model 的原始下标。 |
| `RequestConsumeSelectedFish()` | 蓝图可调用 | 请求吃掉当前选中鱼。 |
| `RequestSacrificeSelectedFish()` | 蓝图可调用 | 请求献祭当前选中鱼。 |

## 背包格子：`WBP_CatInventorySlot`

源码入口：`Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h`

父类必须是 `UCatInventorySlotWidget`。它不是 Button 根节点。点击、右键、拖拽和 Drop 都由 C++ 的鼠标事件处理。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `DisplayTextBlock` | `TextBlock` | 当前格子的文字摘要。可以不用它，改用图标、数量、边框等蓝图表现。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderSlot(SlotView)` | 蓝图事件 | 单个格子刷新时触发。最适合更新图标、选中框、空格样式。 |
| `GetLastSlotView()` | 蓝图纯函数 | 读取当前格子的只读数据。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `SlotIndex` | 这个格子在当前页面里的显示下标。 |
| `SlotSource` | 格子来源。`InventoryObject` 是随身库存，`ContainerObject` 是鱼护/容器格。 |
| `bOccupied` | 这个格子是否有东西。 |
| `bCanDrag` | 是否允许拖拽。 |
| `bSelected` | 是否当前选中。 |
| `DisplayText` | C++ 整理好的格子文本。 |
| `ObjectKind` | 容器物体类型，比如鱼或其他物体。 |
| `Fish` | 当格子是鱼时，这里有鱼实例副本。 |
| `EquipmentKind` | 当格子是随身库存装备时，表示鱼竿、鱼饵、鱼漂等类别。 |
| `EquipmentDefinitionId` | 当前装备或消耗品定义 ID。 |

### 操作含义

左键会选中格子。右键会尝试把随身库存里的装备设为当前钓鱼选择。拖拽到另一个格子会由 PageController 复核后提交服务器移动；WBP 不需要自己写移动逻辑。

## 商店：`WBP_CatShop`

源码入口：`Source/Catfishing/UI/Shop/CatShopWidget.h`

父类必须是 `UCatShopWidget`。商店不是 LocalPlayer 启动时预创建的，它由世界里的商店交互对象打开。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `CloseButton` | `Button` | 关闭商店。存在时 C++ 自动绑定到 `RequestCloseShop()`。 |
| `WalletTextBlock` | `TextBlock` | 团队公款摘要。 |
| `ResultTextBlock` | `TextBlock` | 最近一次购买/领取反馈。 |
| `EntriesTextBlock` | `TextBlock` | 商品列表的简单文本版。适合临时展示；正式样式建议做动态商品行。 |
| `ShopButtons` | `HorizontalBox` | 旧版固定按钮行。旧资产缺鱼漂按钮时，C++ 会在这里补一个最小按钮。 |
| `PurchaseShopRodT2Button` | `Button` | 固定按钮：购买二级竿。 |
| `PurchaseBugChumButton` | `Button` | 固定按钮：购买窝料。 |
| `PurchaseYarnBallFloatButton` | `Button` | 固定按钮：购买鱼漂。 |
| `ClaimFreeBugBaitButton` | `Button` | 固定按钮：领取普通鱼饵。 |
| `ClaimFreeStarterRodButton` | `Button` | 固定按钮：领取保底竿。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderShop(ViewState)` | 蓝图事件 | 商店数据刷新时触发。动态商品列表建议从这里生成或刷新。 |
| `GetLastShopViewState()` | 蓝图纯函数 | 读取最近一次商店数据。 |
| `RequestPurchaseEntry(EntryId)` | 蓝图可调用 | 请求购买某个商品。只传商品 ID，不传价格和库存。 |
| `RequestFreeClaimEntry(EntryId)` | 蓝图可调用 | 请求领取免费商品。 |
| `RequestCloseShop()` | 蓝图可调用 | 请求关闭商店。 |

### 固定商品 ID

| EntryId | 人话说明 |
| --- | --- |
| `ShopRodT2Order` | 二级鱼竿订单。 |
| `ShopBugChumOrder` | 窝料订单。 |
| `ShopFloatYarnBallOrder` | 鱼漂订单。 |
| `FreeBugBaitClaim` | 免费普通鱼饵领取。 |
| `FreeStarterRodClaim` | 免费保底竿领取。 |

### 动态商品行推荐做法

正式样式不必依赖上面的固定按钮。更好的做法是在 `BP_RenderShop` 里读取 `ViewState.Entries`，每条 `FCatShopEntryView` 生成一个商品行。

| 字段 | 人话说明 |
| --- | --- |
| `EntryId` | 点击时回传的商品 ID。 |
| `DefinitionId` | 商品对应的装备或消耗品定义。用于展示名字或图标。 |
| `UnitPrice` | 单价。只展示，服务器才是最终扣款者。 |
| `RemainingStock` | 剩余库存。 |
| `bUnlimitedStock` | 是否无限库存。 |
| `bSoldOut` | 是否售罄。 |
| `bAffordable` | 团队公款是否够买。 |
| `bFreeClaim` | 是否免费领取项。true 时按钮调用 `RequestFreeClaimEntry`。 |
| `bActionEnabled` | 当前按钮是否应该可点。 |
| `DisplayText` | C++ 整理好的商品行文本。 |
| `ActionText` | C++ 整理好的按钮文字，例如购买或领取。 |

## 交互提示：`WBP_CatInteractionPrompt`

源码入口：`Source/Catfishing/UI/Interaction/CatInteractionPromptWidget.h`

父类必须是 `UCatInteractionPromptWidget`。它只显示“靠近什么，按什么键”，不负责真正打开商店、鱼缸或祭坛。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `PromptTextBlock` | `TextBlock` | 完整交互提示文本。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderPrompt(ViewState)` | 蓝图事件 | 交互目标变化时触发。 |
| `GetLastPromptViewState()` | 蓝图纯函数 | 读取最近一次提示数据。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `bVisible` | 是否应该显示提示。 |
| `TargetText` | 当前交互对象短名称，比如商店、鱼缸、祭坛。 |
| `ConfirmKeyName` | 当前交互键名，来自正式输入资产。 |
| `PromptText` | C++ 整理好的完整提示文本。 |

## 图鉴：`WBP_CatCollection`

源码入口：`Source/Catfishing/UI/Collection/CatCollectionWidget.h`

父类必须是 `UCatCollectionWidget`。当前已有 WBP 配置和渲染接口，但当前代码检索没有看到像背包、商店那样完整的打开入口。可以先拼资产和样式，后续需要接打开入口。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `SummaryTextBlock` | `TextBlock` | 图鉴摘要，比如记录数量。 |
| `EntriesTextBlock` | `TextBlock` | 图鉴条目的简单文本列表。正式样式建议用 `Entries` 做动态条目。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderCollection(ViewState)` | 蓝图事件 | 图鉴数据刷新时触发。 |
| `GetLastCollectionViewState()` | 蓝图纯函数 | 读取最近一次图鉴数据。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `bAvailable` | durable Profile 图鉴数据是否可读。false 不等于空图鉴。 |
| `Entries` | 图鉴条目数组。 |
| `SummaryText` | C++ 整理好的图鉴摘要文本。 |
| `FishDefinitionId` | 单条鱼定义 ID。 |
| `State` | 图鉴公开状态，比如未知、剪影、已记录。 |
| `BestWeightKilograms` | 历史最佳重量。 |
| `EncounterCount` | 合格交手次数。 |
| `DisplayText` | C++ 整理好的条目文本。 |

## 当前不是 WBP 拼装合同的 UI

`UCatTravelWidget` 是当前 Frontend/Online 的原生白盒界面。它在 `UCatLocalPlayerUISubsystem` 中用 `UCatTravelWidget::StaticClass()` 创建，没有走 `UCatUISettings` 的 WBP 配置。如果要把联机前端也改成 WBP 样式，需要新增配置项和对应 WBP 基类接线。

`UCatInteractionWidget` 是本地准星和目标提示的原生 View，也是在 `UCatLocalPlayerUISubsystem` 中用 `StaticClass()` 创建。它不是 `WBP_CatInteractionPrompt`。当前可拼样式的交互 WBP 是靠近对象提示 `WBP_CatInteractionPrompt`。

## 拼装时最容易踩的点

不要把 `WBP_CatInventorySlot` 的根改成纯 Button 逻辑。格子点击、右键、拖拽、Drop 都已经由 `UCatInventorySlotWidget` 处理；你可以在里面放 Button、Border、Image、Text，但不要绕过父类事件自己提交移动。

不要在 WBP 里写死 Tab、E 等键名。背包和交互键来自 `/Game/Input/InputContext/IMC_InputContext`，C++ 会解析成 `ToggleKeyName` 或 `ConfirmKeyName` 给 UI 展示。

不要在商店按钮里自己改公款、库存或装备。按钮只调用 `RequestPurchaseEntry` 或 `RequestFreeClaimEntry`，服务器回包后 UI 会刷新。

不要把外部鱼护箱子页面做成另一套状态。`WBP_CatFishGuardInventory` 复用同一个 `UCatInventoryWidget` 和同一个 Model，只是在进入渲染前把显示格裁成地面鱼护容器格。

不要在 `UCatLocalPlayerUISubsystem` 里给世界库存对象继续加专用成员。LocalPlayer 只保留 HUD、普通背包和交互提示这些本地玩家模块；鱼护、鱼缸和以后新增的箱子应从自己的交互对象传入容器上下文和页面类。

不要把 `Content/UI/WBP_CatLakeReach.uasset` 当正式入口继续改。当前正式拆分 WBP 在 `/Game/UI/...` 下。

## 事实来源

- `Source/Catfishing/UI/CatUISettings.h`
- `Source/Catfishing/UI/CatUISettings.cpp`
- `Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp`
- `Source/Catfishing/Items/CatFishGuardActor.h`
- `Source/Catfishing/Items/CatFishGuardActor.cpp`
- `Source/Catfishing/UI/CatUIModalInputMode.cpp`
- `Source/Catfishing/UI/HUD/CatHUDWidget.h`
- `Source/Catfishing/UI/Inventory/CatInventoryWidget.h`
- `Source/Catfishing/UI/Inventory/CatInventoryTypes.h`
- `Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h`
- `Source/Catfishing/UI/Shop/CatShopWidget.h`
- `Source/Catfishing/UI/Shop/CatShopTypes.h`
- `Source/Catfishing/UI/Interaction/CatInteractionPromptWidget.h`
- `Source/Catfishing/UI/Collection/CatCollectionWidget.h`
- `Source/Catfishing/UI/CatTravelWidget.h`
- `Source/Catfishing/UI/CatInteractionWidget.h`
