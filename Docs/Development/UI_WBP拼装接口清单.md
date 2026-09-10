# UI WBP 拼装接口清单

文档状态：当前代码与 Frontend / 局内菜单接口核对版（2026-09-08）

范围：这份文档只说明当前项目给 WBP 提供的父类、控件名、蓝图事件、蓝图可调用函数和只读数据。它用于手工重做 UI 样式，不作为验收文档，也不规定最终美术风格。

事实来源清单：

- `Source/Catfishing/UI/CatUISettings.h/.cpp`：正式 UI 资产软引用、输入 Action 与 IMC 配置入口。
- `Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp`：Frontend、全局加载遮罩与局内玩家 UI 的创建、绑定和拆除入口。
- `Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h/.cpp`：主界面 Root、子页面控件解析、SettingsModel 复用边界。
- `Source/Catfishing/UI/Save/CatLakeMainMenuWidget.h/.cpp`：局内 ESC 菜单 WBP 父类、控件名、蓝图事件和设置页输入回填。
- `Source/Catfishing/UI/ItemTooltip/CatItemTooltipWidget.h/.cpp`、`CatItemTooltipModel.h/.cpp`、`CatItemTooltipController.h/.cpp`：物品悬停提示的正式父类、只读投影和本地控制器。
- `Source/Catfishing/UI/Save/CatLakeMainMenuController.h/.cpp`：局内菜单打开态、输入模式、保存、设置和退出请求的 Controller 边界。
- `Source/CatfishingEditor/UI/CatFrontendWidgetAuthoringLibrary.h/.cpp`：正式 Frontend 与局内菜单 WBP 的编辑器生成和合同校验入口。
- `Source/CatfishingEditor/UI/CatItemTooltipAuthoringLibrary.h/.cpp`、`Scripts/migrate_item_tooltip.py`、`Source/CatfishingEditor/UI/Tests/CatItemTooltipTests.cpp`：Aegis 悬停 WBP 迁移、合同收尾和自动化用例入口。
- `Config/DefaultGame.ini`：当前项目配置覆盖的输入 Action、IMC、地图和 Cook 目录。
- 本轮运行证据（2026-09-10）：Editor 构建通过；正式 Tooltip WBP 已迁移并在新进程加载，`Saved/Automation/Tooltip/TooltipRerun.log` 记录投影与生命周期两项通过；`Saved/Automation/Tooltip/NetworkFinal.log` 记录正式两端联机检查通过；打包资源尚待核对。

## 总原则

当前 UI 采用“C++ 管状态，WBP 管表现”的方式。WBP 可以随便改布局、颜色、字体和动效，但不要绕过 C++ 直接改背包、商店、公款、鱼护或角色状态。

`BindWidgetOptional` 表示控件不是强制存在。控件存在且名字对上时，C++ 会自动写文本或绑定按钮；控件不存在时，WBP 只能使用当前父类真实暴露的 Blueprint 事件、纯函数或 `BlueprintReadOnly` 字段自己做表现，不要照搬其他页面的旧事件名。

按钮点击不要自己写后端逻辑。背包、商店这类页面已经提供 `Request...` 函数，蓝图按钮只需要调用这些函数，把“玩家想做什么”交给 PageController 和服务器处理。

模态页面打开后会切到 UIOnly 输入模式，并把焦点交给当前 Widget。根 WBP 必须保持继承正确；商店、背包关闭应走对应 `RequestClose...` 函数，不要在蓝图里直接 `RemoveFromParent`。

当前正式拼装入口是下面这些拆分 WBP。

## 正式 WBP 入口

| WBP 资产 | 父类 | 用途 | 谁创建 |
| --- | --- | --- | --- |
| `/Game/UI/HUD/WBP_CatHUD` | `UCatHUDWidget` | 局内主 HUD，默认常驻天数、背包入口和设置入口 | `UCatLocalPlayerUISubsystem` 启动局内 UI 时创建 |
| `/Game/UI/Inventory/WBP_CatInventory` | `UCatInventoryWidget` | 默认背包页面，绑定一份明确的 `UCatInventoryComponent` 并按该库存自己的 Model 建格子 | `UCatLocalPlayerUISubsystem` 创建，`UCatInventoryPageController` 打开 |
| `/Game/UI/Inventory/WBP_CatFishGuardInventory` | `UCatFishGuardInventoryWidget` | 鱼护库存页面，显示本次交互鱼护 Actor 自己的 `UCatInventoryComponent` | `ACatFishGuardActor` 提供页面类，`UCatInventoryPageController` 按需创建 |
| `/Game/UI/Inventory/WBP_CatCampInventory` | `UCatCampInventoryWidget` | 营地公共仓库页面，显示营地 Actor 自己的 `UCatInventoryComponent`；需要玩家背包区时另放普通库存子页 | `ACatCampInventoryActor` 提供页面类，`UCatInventoryPageController` 按需创建 |
| `/Game/UI/InventorySlot/WBP_CatInventorySlot` | `UCatInventorySlotWidget` | 背包单个格子，负责显示占用、选中、拖拽和 Drop | `UCatInventoryWidget` 重建格子列表时动态创建 |
| `/Game/UI/Inventory/WBP_CatItemTooltip` | `UCatItemTooltipWidget` | Aegis 迁移来的物品悬停提示，显示名称、说明、图标和实例详情 | `UCatLocalPlayerUISubsystem` 启动局内 UI 时创建，库存格子悬停时由 `UCatItemTooltipController` 驱动 |
| `/Game/UI/Shop/WBP_CatShop` | `UCatShopWidget` | 世界商店页面，显示商品、公款、购买和领取反馈 | `UCatShopInteractionComponent` 在靠近商店交互时创建 |
| `/Game/UI/Interaction/WBP_CatInteractionPrompt` | `UCatInteractionPromptWidget` | 靠近对象时的“按键交互”提示 | `UCatLocalPlayerUISubsystem` 启动局内 UI 时创建 |
| `/Game/UI/Save/WBP_CatLakeMainMenu` | `UCatLakeMainMenuWidget` | 局内 ESC 暂停菜单，承载返回游戏、设置、保存、退出到主菜单和退出游戏 | `UCatLocalPlayerUISubsystem` 启动局内 UI 时创建，`UCatLakeMainMenuController` 响应输入打开 |
| `/Game/UI/Collection/WBP_CatCollection` | `UCatCollectionWidget` | 图鉴/相册只读页面；当前不是 HUD 常驻入口 | 当前没有运行时创建入口，也不由 `CatUISettings` 装配 |

HUD、背包、背包格子、交互提示和局内 ESC 菜单的默认路径来自 `Source/Catfishing/UI/CatUISettings.cpp`。鱼护页面类跟随 `ACatFishGuardActor` 自己的 `InventoryViewClass`；营地公共仓库页面类跟随 `ACatCampInventoryActor` 自己的 `InventoryViewClass`。这两个世界库存页面保留独立原生父类，当前父类只提供资产身份和 `IsChildOf` 校验，显示与操作仍继承 `UCatInventoryWidget` 的一库存一 Model 流程。图鉴只保留 View 接口，运行入口必须先明确 PageController/Model/View 链路。如果 `InventoryViewClass` 没有指到对应的库存 WBP 父类，交互会打开失败并记录日志。

## Frontend：`WBP_CatFrontendRoot`

正式 Frontend Root 目标路径是 `/Game/UI/Frontend/WBP_CatFrontendRoot`，父类是 `UCatFrontendRootWidget`。整套 Frontend 共九个资产：一个 Root、四个业务子 WBP、一个全局 Loading WBP 和三个动态列表行资产。Root 是 `UCatLocalPlayerUISubsystem` 的主界面创建目标；Loading WBP 由同一个 LocalPlayer UI 作为最高层遮罩创建，不嵌入 Root。

| 资产 | 父类 / 装配位置 | 用途 |
| --- | --- | --- |
| `/Game/UI/Frontend/WBP_CatFrontendRoot` | `UCatFrontendRootWidget` | 唯一 Frontend 根视图，持有背景层、页面切换器和四个业务子 WBP |
| `/Game/UI/Frontend/WBP_CatFrontendMenu` | `UUserWidget`，装到 `MenuPage` | 首页、退出确认，以及当前只保留按钮的“加入队伍” |
| `/Game/UI/Frontend/WBP_CatFrontendSaveList` | `UUserWidget`，装到 `SaveListPage` | Minecraft 风格单页存档列表 |
| `/Game/UI/Frontend/WBP_CatFrontendRoom` | `UUserWidget`，装到 `RoomPage` | Steam 好友、邀请、当前房间和房主开始游戏 |
| `/Game/UI/Frontend/WBP_CatFrontendSettings` | `UUserWidget`，装到 `FrontendSettingsPage` | 游戏、画面、声音、控制四类设置 |
| `/Game/UI/Frontend/WBP_CatFrontendLoading` | `UUserWidget`，全局遮罩内容 | 进入游戏时用真实 gate 合成总进度；退出到主菜单时只显示真实等待状态，不要求条形进度 |
| `/Game/UI/Frontend/WBP_CatSaveSlotRow` | `UCatFrontendSaveSlotRowWidget`，存档页动态行 | 显示 `UCatFrontendSaveModel` 提供的世界存档槽摘要 |
| `/Game/UI/Frontend/WBP_CatRoomFriendRow` | `UCatFrontendRoomFriendRowWidget`，房间页动态行 | 显示 `UCatFrontendRoomModel` 提供的 Steam 好友摘要并提交邀请意图 |
| `/Game/UI/Frontend/WBP_CatRoomPlayerSlot` | `UCatFrontendRoomPlayerSlotWidget`，房间页动态行 | 显示当前房间真实成员槽，不自行补假成员 |

### Root 必需装配

`FrontendPageSwitcher`、`MenuPage`、`SaveListPage`、`RoomPage` 和 `FrontendSettingsPage` 是 Root 的必需控件。`StaticBackgroundImage` 和 `DynamicBackgroundContainer` 是两个可选背景资产位；静态图和动态材质、媒体或场景子 WBP 都由资产侧承载，Controller 和 Model 不感知背景形态。

四个页面按业务通信边界拆分，业务状态集中在 Root、Model 和 PageController。Root 通过 `BP_RenderMenu()`、`BP_RenderSaveList()`、`BP_RenderRoom()` 和 `BP_RenderFrontendSettings()` 通知蓝图重绘；子 WBP 分别通过 Root 的 `GetSaveModel()`、`GetRoomModel()`、`GetSettingsModel()` 读取数据，并通过 `GetPageController()` 或 Root 的 `Request...` 函数提交玩家意图。全局 Loading WBP 不读取这些 Model，也不向 Root 提交意图。

Root 会在四个子 WBP 的 WidgetTree 内按名称解析以下关键控件：菜单页的 `StartGameButton`、`JoinPartyButton`、`FrontendSettingsButton`、`ExitGameButton`、`ConfirmExitButton`、`CancelExitButton`；设置页的 `GameSettingsCategoryButton`、`GraphicsSettingsCategoryButton`、`AudioSettingsCategoryButton`、`ControlsSettingsCategoryButton`；以及各业务页的 `SaveResultTextBlock`、`RoomResultTextBlock`、`FrontendSettingsResultTextBlock`。全局 Loading WBP 由 `UCatLocalPlayerUISubsystem` 写入 `LoadingProgressTextBlock`、`LoadingProgressBar` 和等待原因文本；进入游戏用 Start、地图包、旅行、World、BeginPlay 和本地 UI 的真实 gate 合成总进度，退出到主菜单会折叠进度条。

### 数据与流程边界

`UCatFrontendPageController` 只管理“首页 -> 存档 -> 房间 -> 加载”的页面流程、当前选中槽和确认状态。`UCatFrontendSaveModel`、`UCatFrontendRoomModel`、`UCatFrontendSettingsModel` 分别读取 Save、Online 和正式设置来源，Root 不复制第二份业务状态。

世界 Save 独立于 Profile：`UCatSaveSubsystem` 和 `UCatRunSaveGame` 负责世界槽、库存内容和角色位置；`UCatProfileSubsystem` 继续负责 Grant Journal、图鉴、解锁与装备选择，不能拿 Profile 拼主界面存档行。

创建房间与开始游戏是两个阶段：Online 创建成功后应停留在 Frontend 房间页，只有房主显式点击“开始游戏”才提交异步预载和旅行。设置页固定为游戏、画面、声音、控制四类；控制分类当前只保留正式入口，不虚构控制字段。“加入队伍”同样只保留首页按钮，不接搜索、加入或本地替身房间。

人工已允许麦克风选择和语音输入模式本轮暂不可用。设置页保留 `MicrophoneComboBox`、`VoiceInputModeComboBox` 两行并禁用，用 `MicrophoneUnavailableText`、`VoiceInputModeUnavailableText` 分别说明现有 Steam 语音未接通设备选择、输入模式切换；麦克风可提示在系统声音设置中调整默认输入设备。占位文本只供展示，不保存为偏好；其他设置范围不变。控件与禁用逻辑已在 Root 和资产生成器源码中落地，尚无正式 WBP 的运行证据。

### 当前实施边界

当前源码事实：Root、PageController、三个 Model、LocalPlayer 全局 Loading 遮罩接线已落 `.h/.cpp`；`UCatSaveSubsystem` 已实现槽目录、异步创建/读写及删除入口，库存与角色位置恢复已接领域接口。Frontend Root 承载加载遮罩，局内菜单只提交退出主菜单请求。`CatUISettings` 软类指向 `/Game/UI/Frontend/WBP_CatFrontendRoot.WBP_CatFrontendRoot_C`，`Config/DefaultGame.ini` 已精确 Cook `/Game/UI/Frontend` 和 `/Game/Audio/Settings`。

`Source/CatfishingEditor/UI/CatFrontendWidgetAuthoringLibrary.h/.cpp` 与 `Scripts/create_frontend_assets.py` 已提供资产生成入口，目标为上述 9 个 WBP，以及 `/Game/Audio/Settings` 下的 1 个 SoundMix（`SMX_CatFrontendSettings`）和 5 个 SoundClass（`SC_CatMaster`、`SC_CatMusic`、`SC_CatSFX`、`SC_CatAmbience`、`SC_CatVoice`），共 6 个音频资产。2026-09-08 已通过现有资产脚本重建 `WBP_CatFrontendLoading`、`WBP_CatFrontendRoot` 和 `WBP_CatLakeMainMenu`，局部 LoadingPage 与局内等待面板已从资产树移出。

当前源码与资产合同已有 Editor Development 构建和资产脚本成功证据；完整 Steam 双端、打包 Development 日志、真实存档创建到房主开始游戏再到地图加载百分比推进的端到端表现仍未完成，不能声明正式主界面模块关闭。

## 局内 ESC 菜单：`WBP_CatLakeMainMenu`

源码入口：`Source/Catfishing/UI/Save/CatLakeMainMenuWidget.h`、`Source/Catfishing/UI/Save/CatLakeMainMenuController.h`

正式局内菜单路径是 `/Game/UI/Save/WBP_CatLakeMainMenu`，父类必须是 `UCatLakeMainMenuWidget`。它不是 HUD 的子区域，也不是主界面 Frontend Root 的子页；`UCatLocalPlayerUISubsystem` 在玩家进入 Lake UI 链路时创建菜单 View 和 Controller，菜单平时不在视口里，只有 `MainMenuToggleAction` 触发或 HUD 的菜单入口触发时才打开。

`UCatLakeMainMenuWidget` 是 View，只负责控件绑定、显示切页和设置控件回填。`UCatLakeMainMenuController` 持有菜单打开态、输入模式、保存请求、设置应用、退出到主菜单请求和退出游戏请求。WBP 可以改布局、动画和美术层级，但不要直接保存游戏、直接写设置、直接销毁 Session、直接旅行、直接退出或自己 `RemoveFromParent`。

普通 Escape 对应 `IA_LakeMenu`，用于打开或关闭局内菜单；PIE 里的 Shift+Escape 保留给编辑器停止运行。这个键位关系由局内菜单 Controller 和 `Source/CatfishingEditor/CatfishingEditor.cpp` 一起维护，WBP 不需要自己判断编辑器停止运行。

`退出到主菜单` 和 `退出游戏` 是两条不同意图：前者交给 Online 的 Leave 链路异步完成保存、拆局、Session 销毁和回前台旅行，等待期间显示全局加载遮罩且不使用固定倒计时上限；后者直接调用本地 `QuitGame` 退出游戏进程。

### 运行链路

| 环节 | 人话说明 |
| --- | --- |
| `UCatUISettings::LakeMainMenuWidgetClass` | 配置或默认指向 `/Game/UI/Save/WBP_CatLakeMainMenu.WBP_CatLakeMainMenu_C`。类加载失败时局内玩家 UI fail-closed，不创建空白菜单。 |
| `UCatLocalPlayerUISubsystem::AttachPlayerLakeUI()` | 创建 HUD、背包、交互提示和局内菜单；`LakeMainMenuWidget` 与 `LakeMainMenuController` 在这里配对。 |
| `UCatLakeMainMenuController::Bind()` | 注入 LocalPlayer、PlayerController 和 View；创建局内设置用的 `UCatFrontendSettingsModel`；订阅 Save 子系统和按钮意图。 |
| `UCatLakeMainMenuController::ToggleMenu()` | 响应普通菜单键切换打开态；编辑器内检测到 Shift+Escape 时放行给 PIE 停止运行。 |
| `UCatLakeMainMenuController::SetMenuOpen()` | 菜单打开时加入视口并切 UI 输入模式；菜单关闭时恢复游戏输入。 |

### 命令页控件名（稳定合同）

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `LakeMainMenuPageSwitcher` | `WidgetSwitcher` | 在暂停命令页和设置页之间切换。存在时 C++ 会用它显式切页，不靠猜 Visibility。 |
| `LakeCommandPanel` | `PanelWidget` | 暂停命令页根容器。没有 Switcher 的布局仍可通过它显隐命令区。 |
| `LakeSettingsPanel` | `PanelWidget` | 局内设置页根容器。它承载和主界面同名的设置控件，数据仍来自同一个设置模型规则。 |
| `CloseButton` | `Button` | 返回游戏。点击后只关闭局内菜单并恢复输入，不保存、不退出、不旅行。 |
| `SettingsButton` | `Button` | 打开局内设置页。点击后 Controller 切到 `LakeSettingsPanel`，不创建第二套设置来源。 |
| `SaveButton` | `Button` | 保存当前活动世界。点击后 Controller 调用 `UCatSaveSubsystem::RequestSaveActiveRun()`，是否可保存由 Save 子系统判断。 |
| `ReturnToMainMenuButton` | `Button` | 退出到主菜单。点击后 Controller 调用 `UCatOnlineSubsystem::RequestLeave()`，等待真实异步链路完成。 |
| `ExitGameButton` | `Button` | 退出游戏。点击后走本地 `QuitGame`，不走回前台、离局等待或 DestroySession 链路。 |
| `StatusTextBlock` | `TextBlock` | 显示保存、设置或退出入口返回的反馈，例如“正在保存当前游戏。”或失败原因。 |

### 全局 Loading 遮罩控件名（稳定合同）

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `LoadingProgressTextBlock` | `TextBlock` | Start、Leave 或 Travel 的阶段文本；由 LocalPlayer UI 从 Online 快照派生。 |
| `LoadingProgressBar` | `ProgressBar` | 进入游戏时按真实 gate 合成总进度，其中地图包区间读取引擎异步百分比；退出到主菜单会被 C++ 折叠，资产侧可改成旋转等待动画。 |
| `LoadingDayTextBlock` | `TextBlock` | 全局遮罩下显示“正在切换世界”，只表达当前旅行阶段。 |
| `LoadingProgressTextBlock` | `TextBlock` | 全局遮罩下显示当前真实等待细节，例如地图包百分比、DestroySession 回调、PostLoadMap 确认或 Frontend Root 入视口。 |

### 设置页控件名

局内设置页复用主界面的 `UCatFrontendSettingsModel`：分类、草稿、应用、恢复默认、音频设备刷新和失败文案都沿用主界面设置规则。WBP 只摆控件和表现状态，不保存第二份设置。

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `GameSettingsCategoryButton` | `Button` | 切到游戏设置分类。 |
| `GraphicsSettingsCategoryButton` | `Button` | 切到画面设置分类。 |
| `AudioSettingsCategoryButton` | `Button` | 切到声音设置分类。 |
| `ControlsSettingsCategoryButton` | `Button` | 切到控制分类；当前只显示正式占位说明，不生成键位配置草稿。 |
| `ApplySettingsButton` | `Button` | 应用当前设置草稿。成功后回到暂停命令页；失败时留在设置页显示原因。 |
| `RestoreSettingsDefaultsButton` | `Button` | 把设置草稿恢复为项目默认值；玩家仍需点应用才写入正式配置。 |
| `CancelSettingsButton` | `Button` | 放弃本次设置草稿并回到暂停命令页，游戏仍保持菜单打开状态。 |
| `SettingsDescriptionTextBlock` | `TextBlock` | 显示当前设置分类的人话说明。 |
| `FrontendSettingsResultTextBlock` | `TextBlock` | 显示设置操作结果；不要混用保存反馈。 |
| `GameSettingsPanel` | `PanelWidget` | 游戏设置分类内容容器。可见性由 SettingsModel 当前分类控制。 |
| `GraphicsSettingsPanel` | `PanelWidget` | 画面设置分类内容容器。 |
| `AudioSettingsPanel` | `PanelWidget` | 声音设置分类内容容器。 |
| `ControlsSettingsPanel` | `PanelWidget` | 控制设置分类内容容器；当前用于占位说明。 |
| `LanguageComboBox` | `ComboBoxString` | 语言选择；选项和草稿来自 SettingsModel。 |
| `FullscreenModeComboBox` | `ComboBoxString` | 窗口模式选择；显示项映射到 UE 窗口模式枚举。 |
| `ScreenResolutionComboBox` | `ComboBoxString` | 分辨率选择；选项由 SettingsModel 根据当前窗口模式刷新。 |
| `OverallQualityComboBox` | `ComboBoxString` | 整体画质选择；显示项映射到 UE 质量档。 |
| `VSyncCheckBox` | `CheckBox` | 垂直同步草稿。 |
| `UIScaleSlider` | `Slider` | UI 比例草稿；0..1 的视图值会换算成项目正式范围。 |
| `BrightnessSlider` | `Slider` | 亮度 / Gamma 草稿；0..1 的视图值会换算成项目正式范围。 |
| `VibrationCheckBox` | `CheckBox` | 手柄震动草稿；只有当前本地 Controller 支持时才可用。 |
| `VoiceChatCheckBox` | `CheckBox` | 网络语音开关草稿；只有正式 OSS Voice 来源可用时才可用。 |
| `MuteAudioWhenUnfocusedCheckBox` | `CheckBox` | 失焦静音草稿，应用后映射到引擎失焦音量倍率。 |
| `VoiceInputModeComboBox` | `ComboBoxString` | 语音输入模式占位控件；当前禁用，不写草稿。 |
| `VoiceInputModeUnavailableText` | `TextBlock` | 语音输入模式不可用说明。 |
| `MicrophoneComboBox` | `ComboBoxString` | 麦克风选择占位控件；当前禁用，不写草稿。 |
| `MicrophoneUnavailableText` | `TextBlock` | 麦克风选择不可用说明。 |
| `AudioOutputDeviceComboBox` | `ComboBoxString` | 音频输出设备选择；显示项映射到 AudioMixer 稳定设备 ID。 |
| `RefreshAudioOutputDevicesButton` | `Button` | 刷新音频输出设备列表；枚举或切换在途时禁用。 |
| `MasterVolumeSlider` | `Slider` | 主音量草稿。 |
| `MusicVolumeSlider` | `Slider` | 音乐音量草稿。 |
| `SFXVolumeSlider` | `Slider` | 音效音量草稿。 |
| `AmbienceVolumeSlider` | `Slider` | 环境音音量草稿。 |
| `VoiceVolumeSlider` | `Slider` | 语音分类音量草稿，不代替网络语音开关。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderMenu(ViewState)` | 蓝图事件 | 菜单状态刷新后触发。适合做按钮高亮、结果文案动画、焦点表现。 |
| `BP_HandleMenuAction(Action)` | 蓝图事件 | 玩家点击按钮后触发的表现扩展点。只能做动画或音效，不替代 Controller 逻辑。 |
| `BP_RenderSettings()` | 蓝图事件 | 设置控件回填完成后触发。适合做分类动画和局部表现刷新。 |
| `GetLastMenuViewState()` | 蓝图纯函数 | 读取最近一次菜单显示状态。只用于表现，不代表可写业务状态。 |

### 可调用意图

| 名称 | 人话说明 |
| --- | --- |
| `RequestCloseMenu()` | 提交返回游戏意图。按钮、普通 Escape 和蓝图关闭入口都应走这里。 |
| `RequestOpenSettings()` | 提交打开设置页意图。 |
| `RequestSave()` | 提交手动保存意图。 |
| `RequestReturnToMainMenu()` | 提交退出到主菜单意图。Widget 不保存、不销毁 Session、不旅行，只把意图交给 Controller。 |
| `RequestExitGame()` | 提交直接退出游戏意图。 |
| `RequestApplySettings()` | 提交应用设置意图。 |
| `RequestCancelSettings()` | 提交取消设置意图。 |
| `RequestRestoreSettingsDefaults()` | 提交恢复默认草稿意图。 |
| `RequestRefreshAudioOutputDevices()` | 提交刷新音频输出设备意图。 |
| `RequestSelectGameSettings()` | 提交切到游戏设置分类意图。 |
| `RequestSelectGraphicsSettings()` | 提交切到画面设置分类意图。 |
| `RequestSelectAudioSettings()` | 提交切到声音设置分类意图。 |
| `RequestSelectControlsSettings()` | 提交切到控制设置分类意图。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `StatusText` | 当前菜单底部展示的结果或降级说明。它由 Controller 写入，蓝图只显示。 |
| `bSettingsEnabled` | 设置按钮是否可点击；当前由局内设置模型是否可用决定。 |
| `bCloseEnabled` | 返回游戏按钮是否可点击；当前保持可点击。 |
| `bSaveEnabled` | 保存按钮是否可点击；Save 服务缺失或忙碌时为 false。 |
| `bReturnToMainMenuEnabled` | 退出到主菜单按钮是否可点击；只有 Online 空闲且当前确实在局内会话时才为 true。 |
| `bReturnToMainMenuPending` | 是否正在等待退出到主菜单链路完成；为 true 时设置、保存和返回游戏入口会被锁住。 |
| `bExitEnabled` | 退出游戏按钮是否可点击；当前保持可点击，点击后通常不会停留等待。 |

### 拼装注意

重新拼 `WBP_CatLakeMainMenu` 时，先保证父类正确，再保证上面这些控件名和类型能被合同校验找到。控件可以换位置、换样式、换容器层级；不要改名后只在蓝图事件图里自己接逻辑，因为那会绕开 Controller、Save 子系统和 SettingsModel。

当前代码已经有编辑器生成和校验入口：`UCatFrontendWidgetAuthoringLibrary::CreateMissingLakeMainMenuWidgetBlueprint()` 会在 `/Game/UI/Save` 下重建 `WBP_CatLakeMainMenu` 并核验父类与控件名。手工重拼后可以参考 `ValidateLakeMainMenuWidgetContract()` 的控件清单做复查。

## HUD：`WBP_CatHUD`

源码入口：`Source/Catfishing/UI/HUD/CatHUDWidget.h`

父类必须是 `UCatHUDWidget`。默认主界面只应该常驻左上天数、左下背包入口和右上设置入口；打开背包后的装备栏/物品栏属于背包页面，不放进常驻 HUD。`UCatHUDWidget` 只绑定蓝图里真实存在的命名控件，不在运行时生成 HUD 控件。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `DayTextBlock` | `TextBlock` | 左上角天数文本，C++ 写入“第 N 天”。 |
| `MainMenuButton` | `Button` | 右上角设置/主页入口，点击后广播 `OpenMainMenu`。 |
| `InventoryButton` | `Button` | 左下角背包入口，点击后广播 `OpenInventory`，由背包控制器打开页面。 |
| `CatStatusTextBlock` | `TextBlock` | 猫状态调试摘要，默认隐藏；只在诊断布局里显式打开。 |
| `FishingFeedbackTextBlock` | `TextBlock` | 钓鱼流程调试反馈，默认隐藏；只在诊断布局里显式打开。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderHUD(ViewState)` | 蓝图事件 | 每次 HUD 数据刷新时触发。想做动画、分区排版、图标变化，用这个事件。 |
| `GetLastHUDViewState()` | 蓝图纯函数 | 读取最近一次 HUD 数据。适合绑定文本、进度条、显隐状态。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `DayText` | C++ 整理好的天数字符串，常驻显示在左上角。 |
| `bMainMenuEntryVisible` | 是否显示设置/主页入口。 |
| `bInventoryEntryVisible` | 是否显示背包入口。 |
| `bShowCatStatusDebugText` | 是否显示猫状态调试摘要；当前默认值为 false。 |
| `bShowFishingFeedbackDebugText` | 是否显示钓鱼调试反馈；当前默认值为 false。 |
| `bShowCrosshair` | 是否绘制 HUD 中心准星；当前默认值为 false。 |
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

父类必须是 `UCatInventoryWidget`。它是“显示一份库存”的通用库存页：页面只绑定一个 `UCatInventoryComponent`，读取该组件自己的 `UCatInventoryModel` 列表，然后按槽位原序创建 `WBP_CatInventorySlot`。普通背包打开时，`UCatInventoryPageController::Bind()` 会把角色身上的库存组件注入默认背包；鱼护、鱼缸和营地这类世界库存打开时，交互对象把自己的库存组件和页面类传给统一 `OpenInventory()` 入口。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `InventorySlotWrapBox` | `WrapBox` | 当前页面自己的格子容器。C++ 只按 `DisplayInventory->GetInventoryModel()->GetInventoryList()` 创建格子，不再读取旧版三组聚合数组。 |
| `CloseButton` | `Button` | 关闭当前库存窗口。存在时 C++ 自动绑定到 `RequestCloseInventory()`，关闭和输入恢复由 `UCatInventoryPageController` 处理。 |
| `ConsumeFishButton` | `Button` | 使用当前选中格。存在时 C++ 自动绑定到 `RequestUseSelectedItem()`；鱼的食用、装备使用或其他实例语义由服务器按槽位重读后裁决。 |

旧文档里提到的 `SummaryTextBlock`、`EquipmentTextBlock`、`InventoryItemsTextBlock`、`SelectedFishTextBlock` 和 `ResultTextBlock` 不是当前 `UCatInventoryWidget` 的自动绑定字段。如果资产仍保留这些控件，它们只能作为蓝图自己的表现层；C++ 当前不会向它们写库存 ViewState 文本。

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `SetInventoryContext(Inventory)` | 蓝图可调用 | 指定本页面显示哪一份库存。页面只绑定这份库存的 Model，并在切换时解绑旧 Model。 |
| `GetInventoryContext()` | 蓝图纯函数 | 读取当前页面绑定的库存组件。槽位操作必须带回这份库存宿主，不能按页面类型猜是背包、营地还是鱼护。 |
| `RequestCloseInventory()` | 蓝图可调用 | 请求关闭当前库存窗口。Widget 只提交关闭意图，窗口状态、焦点和输入锁由 PageController 统一处理。 |
| `RequestSelectSlot(SlotIndex)` | 蓝图可调用 | 选择当前页面内的一个槽位下标，只更新本页高亮和使用按钮状态，不写库存事实。 |
| `RequestUseSelectedItem()` | 蓝图可调用 | 使用当前选中格，最终会调用该格子的 `RequestUseItem()`，由服务器按库存宿主和槽位重读真实条目。 |

### 当前数据边界

| 数据 | 人话说明 |
| --- | --- |
| `DisplayInventory` | 本页唯一库存上下文；页面注入或构建时解析，决定刷新、选择、使用和拖放的宿主。 |
| `UCatInventoryComponent::GetInventoryModel()` | 每份库存组件自己的 Model。组件提交或收到复制后调用 `SetInventoryList()`，绑定它的页面收到通知后刷新。 |
| `UCatInventoryModel::InventoryList` | 只保存这份库存的 `FCatInventoryEntry` 列表；不聚合其他库存、不保存本地等待请求、不生成三组显示数组。 |
| `SelectedSlotIndex` | 当前页面本地选择下标。库存刷新会清掉选择，使用按钮只读这个本地状态。 |
| `SlotWidgets` | 当前页面创建出来的格子控件和选择委托所有权记录；刷新和销毁时解绑。 |

当前库存 UI 没有旧槽位视图类型，也没有旧版三组聚合数组。槽位直接保存 `SourceInventory`、`SlotIndex` 和显示用 `FCatInventoryEntry`；使用和拖放时，`UCatInventorySlotWidget` 直接通过 owning `ACatfishingPlayerController` 提交 `ServerUseInventoryItemFromHost()` 或 `ServerMoveInventoryItemBetweenHosts()`。`UCatInventoryPageController` 不再复核旧 UI 投影、不再维护本地等待动作，也不负责把右键转成营地取物；它只管理页面打开、关闭、输入绑定和焦点。

## 营地公共仓库：`WBP_CatCampInventory`

源码入口：`Source/Catfishing/UI/Inventory/CatCampInventoryWidget.h`

父类必须是 `UCatCampInventoryWidget`。这个类当前没有额外字段或重写逻辑，但它仍是正式营地仓库 WBP 的原生父类：`ACatCampInventoryActor::LoadInventoryViewClass()` 会校验资产必须继承它。运行时营地 Actor 调用 `UCatLocalPlayerUISubsystem::OpenInventory(InventoryComponent, LoadInventoryViewClass())`，页面显示营地 Actor 自己的 `UCatInventoryComponent`。

推荐拼法：根 `WBP_CatCampInventory` 负责标题、关闭按钮和营地仓库格子容器。若要在同屏展示玩家背包，另放一个普通 `UCatInventoryWidget` 子页，让子页按 owning Pawn 自己解析角色背包；不要期待父页分发同一份聚合投影或三数组。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `InventorySlotWrapBox` | `WrapBox` | 营地库存格子容器。C++ 会按营地 Actor 自己库存组件的 Model 列表创建格子。 |
| `CloseButton` | `Button` | 关闭整个营地仓库界面。存在时 C++ 自动绑定。 |
| `ConsumeFishButton` | `Button` | 使用当前选中格。是否能使用由服务器和物品实例裁决。 |

### 资产拼装与接手核对

`ACatCampInventoryActor` 上的 `InventoryViewClass` 必须指到继承 `UCatCampInventoryWidget` 的独立 WBP。程序员或 UI 接手人改完资产后，至少核对三件事：直接和营地仓库交互时打开的是 `WBP_CatCampInventory`；页面格子来自营地 Actor 自己的库存组件；拖放公共仓库和背包物品时日志出现 `ui_inventory_slot_drop_submitted`，随后服务器从两端库存宿主和槽位重读事实，而不是 WBP 本地改数组。

## 鱼护箱子：`WBP_CatFishGuardInventory`

源码入口：`Source/Catfishing/UI/Inventory/CatFishGuardInventoryWidget.h`

父类必须是 `UCatFishGuardInventoryWidget`。这个类当前同样只提供正式 WBP 父类身份；`ACatFishGuardActor::LoadInventoryViewClass()` 会校验资产必须继承它。鱼护 Actor 在本地命中交互后，把自己的 `FishInventory` 和这张页面类交给统一 `OpenInventory()`，页面显示这份鱼护库存，不再从旧外部容器聚合投影中筛显示数组。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `InventorySlotWrapBox` | `WrapBox` | 鱼护库存格子容器。C++ 只会往里面创建本次鱼护库存组件的格子。 |
| `CloseButton` | `Button` | 关闭整个鱼护交互界面。存在时 C++ 自动绑定。 |
| `ConsumeFishButton` | `Button` | 使用当前选中格。鱼的食用只是物品实例 Use 语义之一，服务器负责复核。 |

## 背包格子：`WBP_CatInventorySlot`

源码入口：`Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h`

父类必须是 `UCatInventorySlotWidget`。它不是 Button 根节点。点击、右键、拖拽和 Drop 都由 C++ 的鼠标事件处理；WBP 负责表现，不负责改库存数组。

### 可选控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `ThumbnailImage` | `Image` | 当前格子的物品图。`SetSlotContext()` 会从物品定义读取缩略图，空格会清掉旧图片。 |
| `QuantityTextBlock` | `TextBlock` | 当前格子的数量角标。数量大于 1 时显示，否则隐藏。 |

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `GetInventoryEntry()` | 蓝图纯函数 | 读取当前格子的显示副本，只用于图标、数量和按钮状态。真实库存事实仍由服务器按宿主和槽位重读。 |
| `RequestUseItem()` | 蓝图可调用 | 使用当前格。右键和父页使用按钮共用这个入口。 |
| `BP_InitializeSlot()` | 蓝图事件 | C++ 已经写入实例、数量、选中状态、默认图像和数量文本后触发，适合补充边框、动效或自定义表现。 |

### 常用数据

| 字段 | 人话说明 |
| --- | --- |
| `SourceInventory` | 本格所属库存组件；使用和拖放都会把它的 Owner 作为服务器请求宿主。 |
| `SlotIndex` | 本格在所属库存组件里的槽位下标。不同库存可以有相同下标，所以请求必须同时携带 `SourceInventory`。 |
| `InventoryEntry` | Model 提供的显示副本；不要通过它修改库存。 |
| `bSelected` | 本格本地选中表现；父页面写入，蓝图读取。 |

### 操作含义

左键在松开时选中格子。右键调用 `RequestUseItem()`，由 owning PlayerController 提交 `ServerUseInventoryItemFromHost(RequestId, SourceHost, SlotIndex)`。拖拽到另一个有效格时，目标格固定源宿主、源槽位、目标宿主和目标槽位，然后提交 `ServerMoveInventoryItemBetweenHosts()`；服务器会从两个 `InventoryComponent` 当前内容重读并裁决。WBP 不需要自己写移动、取物、吃鱼或装备选择逻辑。

## 物品悬停提示：`WBP_CatItemTooltip`

源码入口：`Source/Catfishing/UI/ItemTooltip/CatItemTooltipWidget.h`、`Source/Catfishing/UI/ItemTooltip/CatItemTooltipModel.h`、`Source/Catfishing/UI/ItemTooltip/CatItemTooltipController.h`

正式物品提示路径是 `/Game/UI/Inventory/WBP_CatItemTooltip`，父类必须是 `UCatItemTooltipWidget`。这张 WBP 目标是承接 Aegis 的原始悬停框布局；`UCatUISettings::ItemTooltipWidgetClass` 默认指向它，`UCatLocalPlayerUISubsystem` 启动局内 UI 时把它加到本地玩家屏幕高层级，并创建唯一的 `UCatItemTooltipController`。库存格子只在鼠标进入、离开、销毁或重建时提交显示/隐藏意图，不在每个格子里创建自己的提示框。

### 必需控件名

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `RootBorder` | `Border` | 迁移布局的定位根；C++ 只写入格子中心换算后的屏幕位置，不重排原始面板结构。 |
| `ItemIconImage` | `Image` | 物品图标；当前物品没有缩略图时收起，不能沿用上一件物品的图。 |
| `ItemNameText` | `TextBlock` | 物品名称，来自物品定义的库存显示名。 |
| `ItemDescriptionText` | `TextBlock` | 物品说明，来自物品定义的库存说明，保留原换行。 |
| `InstanceDetailsText` | `TextBlock` | 实例详情行；鱼显示重量，鱼竿显示耐久，普通物品或无详情时收起。 |

### 只读 Model 信息范围

`UCatItemTooltipModel` 只把当前 `UCatInventoryItemInstance` 投影成 `FCatItemTooltipViewData`，字段范围是 `Name`、`Description`、`Icon` 和 `InstanceDetails`。它不持有库存宿主、槽位、权限、网络请求或可写玩法状态。

鱼实例只显示当前重量，格式为两位小数的 kg，例如 `重量：3.12 kg`；捕获者、Owner 或其他身份信息不进入悬停框。鱼竿实例读取当前耐久和定义里的最大耐久，断裂时在同一行追加 `（已断裂）`。耐久或重量复制尚未到达、数值非法或没有对应实例详情时，`InstanceDetailsText` 应为空并收起。

### Controller 来源与关闭

`UCatInventorySlotWidget::NativeOnMouseEnter()` 会把本格和格子中心屏幕坐标交给 `UCatItemTooltipController::ShowTooltip()`；`NativeOnMouseLeave()`、`NativeDestruct()`、拖拽开始和格子重建会撤销本格来源。只有当前来源能隐藏当前提示，旧格子的迟到 Leave 不会关掉新格子的提示。

库存页或局内 UI 关闭时会调用 `ForceHideTooltip()` 或 `Unbind()`，立即收起提示并停止继续读取实例。Controller 活动期间会 Tick 当前来源，从同一个实例重新投影耐久等变化；这个刷新不依赖库存列表重建，也不重启动画。

### 默认动画与迁移脚本

默认淡入和淡出时间都是 `0.2` 秒，位置取鼠标进入时的格子中心，不跟随鼠标移动。`ShowAt()` 只把屏幕绝对坐标转换到玩家屏幕几何，并写到 `RootBorder` 的 RenderTranslation；换格时透明度从当前值接续，避免先闪断再淡入。

迁移入口是 `Scripts/migrate_item_tooltip.py`。脚本从 `D:\UnreaProjects\AegisOdyssey\Content` 复制旧 WBP 和最小依赖闭包，目标资产是 `/Game/UI/Inventory/WBP_CatItemTooltip`；它会调用 `UCatItemTooltipAuthoringLibrary::InstallLegacyParentRedirect()` 临时解析 Aegis 旧父类，再调用 `FinalizeMigratedTooltipWidget()` 固定父类、清理旧 MVVM 绑定并补齐 `InstanceDetailsText`。脚本拒绝覆盖已有目标资产或不同内容的同路径依赖。

### 接手核对

| 核对项 | 怎么看 | 当前状态 |
| --- | --- | --- |
| 父类与控件合同 | 打开 `/Game/UI/Inventory/WBP_CatItemTooltip`，确认父类是 `UCatItemTooltipWidget`，并存在上面五个必需控件名。 | 正式资产已迁移并成功加载；必需控件由 FormalWidgetLifecycle 检查通过。 |
| 只读投影 | 读 `UCatItemTooltipModel::BuildViewData()`，确认普通物品不残留详情，鱼重量是两位 kg，鱼竿耐久能显示已断裂。 | InstanceProjection 已通过，覆盖重量、耐久、断裂与普通物品清理。 |
| 格子悬停接入 | 读 `UCatInventorySlotWidget::NativeOnMouseEnter()` / `CancelTooltip()` 与 `UCatLocalPlayerUISubsystem::GetItemTooltipController()`，确认来源只走本地唯一 Controller。 | Editor 构建及 FormalTwoEndpointNetwork 通过；覆盖两端显示定位、真实鼠标切换、无列表重建的耐久复制刷新和关页清理。 |
| 自动化用例 | 运行 `Catfishing.UI.ItemTooltip.InstanceProjection` 和 `Catfishing.UI.ItemTooltip.FormalWidgetLifecycle`。后者依赖正式 WBP 已迁移，并会尝试导出 `Saved/Automation/Tooltip/FishTooltip.png` 供布局检查。 | 两项已通过，日志为 `Saved/Automation/Tooltip/TooltipRerun.log`；布局截图见 `Saved/Automation/Tooltip/FishTooltip.png`。 |

## 商店：`WBP_CatShop`

源码入口：`Source/Catfishing/UI/Shop/CatShopWidget.h`

父类必须是 `UCatShopWidget`。商店不是 LocalPlayer 启动时预创建的，它由世界里的商店交互对象打开。

### 主页面的稳定容器和入口

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `CategoryTabsPanel` | `PanelWidget` | 顶部分类页签容器。C++ 会按 `ViewState.Categories` 创建一个 `WBP_CatShopCategoryTab` 对应一条分类。 |
| `ShopButtons` | `PanelWidget` | 左侧商品卡容器。C++ 会按当前本地 `DisplayedEntries` 重建 `WBP_CatShopGoodsItem`。 |
| `CartLinesPanel` | `PanelWidget` | 右侧已选购列表容器。C++ 会按当前购物车行重建 `WBP_CatShopCartLine`。 |
| `PayButton` | `Button` | 支付整个购物车。资金不足、空车、购物车失效或 pending 时会被禁用。 |
| `CloseButton` | `Button` | 关闭商店。存在时 C++ 自动绑定到 `RequestCloseShop()`。 |
| `WalletTextBlock` | `TextBlock` | 团队公款摘要。 |
| `ResultTextBlock` | `TextBlock` | 最近一次加购、删除、支付或拒绝反馈。 |
| `CartTotalTextBlock` | `TextBlock` | 右侧购物车总金额。 |
| `PayButtonLabelTextBlock` | `TextBlock` | 支付按钮内部文案，当前为“支付”。 |
| `PayDisabledHintLayer` | `Widget` | 支付禁用时的鼠标命中层，用来显示“资金不足，无法购买！”等悬停提示。 |

主 WBP 的分类数量来自商品表归纳出的 `ViewState.Categories`，策划增加分类时只改表，主 WBP 不需要手动加固定按钮。上表控件都是 C++ 自动接线点；纯展示控件可以删除或换成蓝图自定义表现，但删除容器会导致对应动态区域无法生成。

### 蓝图接口

| 名称 | 类型 | 人话说明 |
| --- | --- | --- |
| `BP_RenderShop(ViewState)` | 蓝图事件 | 商店数据刷新时触发。主页面可在这里补动画或额外视觉状态。 |
| `GetLastShopViewState()` | 蓝图纯函数 | 读取最近一次商店数据。 |
| `GetDisplayedEntries()` | 蓝图纯函数 | 读取当前客户端分类过滤后的商品数组。商品区应读它，不直接读完整 `ViewState.Entries`。 |
| `GetCategories()` | 蓝图纯函数 | 读取由真实商品数组归纳出的分类按钮数据；选中态由当前客户端本地写入。 |
| `GetCartLines()` | 蓝图纯函数 | 读取右侧已选购列表。 |
| `RequestAddEntryToCart(EntryId)` | 蓝图可调用 | 请求把商品加入本地购物车。只传 EntryId，不传价格、库存或发货数量。 |
| `RequestRemoveOneCartItem(EntryId)` | 蓝图可调用 | 请求从本地购物车删除一份该商品。 |
| `RequestPayCart()` | 蓝图可调用 | 请求支付整个购物车。服务器会重新查价、查库存、扣公款并发货到营地公共仓库。 |
| `RequestSelectCategory(CategoryId)` | 蓝图可调用 | 切换本地分类页，不会写回 Model 或服务器。 |
| `RequestShowAllCategory()` | 蓝图可调用 | 清空本地分类过滤，显示全部商品。 |
| `RequestCloseShop()` | 蓝图可调用 | 请求关闭商店。 |

### 子控件 WBP

`WBP_CatShopCategoryTab` 的父类必须是 `UCatShopCategoryTabWidget`，每个实例代表顶部分类栏中的一条分类。建议保留：

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `CategoryButton` | `Button` | 点击后切换当前客户端的本地分类过滤。 |
| `CategoryLabelTextBlock` | `TextBlock` | 分类名，来自 `DisplayNameText` 或分类 ID 回退。 |
| `CategoryCountTextBlock` | `TextBlock` | 该分类当前商品数量角标。 |
| `CategorySelectedVisual` | `Widget` | 当前客户端选中该分类时显示的装饰。 |

`WBP_CatShopGoodsItem` 的父类必须是 `UCatShopGoodsItemWidget`，每个实例代表左侧货架上的一条商品。建议保留：

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `GoodsButton` | `Button` | 点击后把本商品加入本地购物车。 |
| `GoodsNameTextBlock` | `TextBlock` | 商品名。 |
| `GoodsIconImage` | `Image` | 商品图标，来自商品表或定义投影。 |
| `GoodsGlyphTextBlock` | `TextBlock` | 当前无正式商品图标时的后备识别符号。 |
| `GoodsPriceTextBlock` | `TextBlock` | 单价。 |
| `GoodsMetaTextBlock` | `TextBlock` | 已选数量、库存充足、余量或售罄提示。 |
| `GoodsDisabledVisual` | `Widget` | 商品不可加购时显示的遮罩。 |

`WBP_CatShopCartLine` 的父类必须是 `UCatShopCartLineWidget`，每个实例代表右侧购物车中的一条商品。建议保留：

| 控件名 | 类型 | 人话说明 |
| --- | --- | --- |
| `CartLineRemoveButton` | `Button` | 垃圾桶删除按钮，每次删除一份。 |
| `CartLineNameTextBlock` | `TextBlock` | 商品名。 |
| `CartLineCountTextBlock` | `TextBlock` | 本地选购次数。 |
| `CartLinePriceTextBlock` | `TextBlock` | 行小计。 |
| `CartLineIconImage` | `Image` | 购物车行的小图标，来自商品投影。 |
| `CartLineGlyphTextBlock` | `TextBlock` | 右侧行内后备识别符号。 |
| `CartLineInvalidVisual` | `Widget` | 货架变化导致本行不可结算时显示。 |

以上子控件里的视觉字段都是可选自动绑定点。默认 WBP 应尽量保留它们，方便 C++ 自动把数据刷进去；如果蓝图想换结构，也可以不放这些名字，再在对应 `BP_RenderCategoryTab`、`BP_RenderGoodsItem`、`BP_RenderCartLine` 里按投影字段自己渲染。

### 当前商店商品表口径

商店出售内容由 `/Game/Catfishing/Data/Shop/DT_ShopCatalog_Default` 维护，行结构是 `FCatShopCatalogTableRow`。分类不写在程序枚举里，直接来自表里的 `DisplayCategoryId` 和 `DisplayCategoryNameOverride`。当前策划表给多少分类，分类栏就生成多少页签；“全部”是程序从完整商品数组归纳出的本地页签。

正式样式的标准做法是让主 WBP 提供 `CategoryTabsPanel`、`ShopButtons` 与 `CartLinesPanel` 三个容器；C++ 只按投影创建分类页签、商品卡和购物车行子 WBP，不在 C++ 里生成整页布局。商店打开后可以用 `CloseButton`、Escape、交互键或背包键关闭。关卡里的商店摊位不需要单独设置营地；服务器支付购物车时会在当前关卡全图寻找营地，并让营地检查自己的公共仓库。没有可用营地公共仓库时，订单会在扣款前失败并回显原因。

| 字段 | 人话说明 |
| --- | --- |
| `EntryId` | 点击时回传的商品 ID。 |
| `DefinitionId` | 商品对应的装备或消耗品定义。用于展示名字或图标。 |
| `PurchaseQuantity` | 单次购买会发到营地公共仓库的数量。 |
| `UnitPrice` | 单价。只展示，服务器才是最终扣款者。 |
| `DisplayCategoryId` | 分类 ID。主页面点击分类后只在本地过滤 `DisplayedEntries`。 |
| `DisplayCategoryNameText` | 分类显示名。 |
| `RemainingStock` | 剩余库存。 |
| `bUnlimitedStock` | 是否无限库存。 |
| `bSoldOut` | 是否售罄。 |
| `bAffordable` | 团队公款是否够买单个条目。当前加购不受它影响，支付时按整车总价裁决。 |
| `bActionEnabled` | 当前按钮是否应该可点。 |
| `CartCount` | 当前购物车里这个商品已选几次。 |
| `DisplayText` | C++ 整理好的商品行文本。 |
| `ActionText` | C++ 整理好的按钮文字，当前语义是加入购物车。 |
| `DisplayNameText` | 商品显示名，优先来自商店表覆盖，其次来自装备定义。 |
| `DescriptionText` | 商品说明，优先来自商店表覆盖，其次来自装备定义。 |

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

父类必须是 `UCatCollectionWidget`。当前只保留图鉴 View 的渲染接口；运行时创建图鉴前，必须先确定正式入口，再按 PageController/Model/View 链路接入。

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

Frontend 唯一 Root 路径是 `/Game/UI/Frontend/WBP_CatFrontendRoot`；资产缺失时暴露加载失败，不创建原生替身。

`UCatInteractionWidget` 是本地准星和目标提示的原生 View，也是在 `UCatLocalPlayerUISubsystem` 中用 `StaticClass()` 创建。它不是 `WBP_CatInteractionPrompt`。当前可拼样式的交互 WBP 是靠近对象提示 `WBP_CatInteractionPrompt`。

## 拼装时最容易踩的点

不要把 `WBP_CatInventorySlot` 的根改成纯 Button 逻辑。格子点击、右键、拖拽、Drop 都已经由 `UCatInventorySlotWidget` 处理；你可以在里面放 Button、Border、Image、Text，但不要绕过父类事件自己提交移动。

不要在 WBP 里写死 Tab、E 等键名。背包和交互键来自 `/Game/Input/InputContext/IMC_InputContext`；当前库存页只用这些配置判断关闭和交互，不再向库存 WBP 输出固定按键文本字段。

不要在商店按钮里自己改公款、库存或装备。商品卡只调用 `RequestAddEntryToCart`，购物车垃圾桶只调用 `RequestRemoveOneCartItem`，支付按钮只调用 `RequestPayCart`；服务器回包后 UI 会刷新。

不要把外部鱼护箱子页面做成另一套状态。`WBP_CatFishGuardInventory` 仍继承通用库存页，但它绑定的是鱼护 Actor 自己的库存组件；不要重新引入外部容器聚合投影或筛选数组。

不要把营地公共仓库的根页面复用默认背包 WBP。`WBP_CatCampInventory` 应继承 `UCatCampInventoryWidget`，并绑定营地 Actor 自己的库存组件；需要双栏时嵌入普通背包子页，让子页按 owning Pawn 绑定玩家背包。

不要在 `UCatLocalPlayerUISubsystem` 里给世界库存对象增加专用成员。LocalPlayer 只保留 HUD、普通背包和交互提示这些本地玩家模块；鱼护、鱼缸和新增箱子应从自己的交互对象传入库存组件和页面类，统一走 `OpenInventory()`。

正式入口只使用 `/Game/UI/...` 下的拆分 WBP。

## 事实来源

- `Source/Catfishing/UI/CatUISettings.h`
- `Source/Catfishing/UI/CatUISettings.cpp`
- `Source/Catfishing/UI/CatLocalPlayerUISubsystem.cpp`
- `Source/Catfishing/Camp/CatCampInventoryActor.h`
- `Source/Catfishing/Camp/CatCampInventoryActor.cpp`
- `Source/Catfishing/FishContainers/CatFishGuardActor.h`
- `Source/Catfishing/FishContainers/CatFishGuardActor.cpp`
- `Source/Catfishing/UI/CatUIModalInputMode.cpp`
- `Source/Catfishing/UI/HUD/CatHUDWidget.h`
- `Source/Catfishing/UI/Inventory/CatCampInventoryWidget.h`
- `Source/Catfishing/UI/Inventory/CatInventoryWidget.h`
- `Source/Catfishing/UI/Inventory/CatInventoryModel.h` / `.cpp`
- `Source/Catfishing/UI/InventorySlot/CatInventorySlotWidget.h`
- `Source/Catfishing/UI/ItemTooltip/CatItemTooltipWidget.h` / `.cpp`
- `Source/Catfishing/UI/ItemTooltip/CatItemTooltipModel.h` / `.cpp`
- `Source/Catfishing/UI/ItemTooltip/CatItemTooltipController.h` / `.cpp`
- `Source/Catfishing/UI/Shop/CatShopWidget.h`
- `Source/Catfishing/UI/Shop/CatShopTypes.h`
- `Source/Catfishing/UI/Interaction/CatInteractionPromptWidget.h`
- `Source/Catfishing/UI/Collection/CatCollectionWidget.h`
- `Docs/Development/主界面重构设计笔记.md`
- `Docs/Development/主界面子技术文档.md`
- `.codex/state/frontend-online-context.json`
- `Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h/.cpp`
- `Source/Catfishing/UI/Frontend/CatFrontendPageController.h/.cpp`
- `Source/Catfishing/UI/Frontend/CatFrontendSaveModel.h/.cpp`
- `Source/Catfishing/UI/Frontend/CatFrontendRoomModel.h/.cpp`
- `Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h/.cpp`
- `Source/Catfishing/Settings/CatGameUserSettings.h/.cpp`
- `Source/Catfishing/Save/CatSaveSubsystem.h/.cpp`
- `Source/Catfishing/Online/CatOnlineSubsystem.h/.cpp`
- `Source/CatfishingEditor/UI/CatFrontendWidgetAuthoringLibrary.h/.cpp`
- `Source/CatfishingEditor/UI/CatItemTooltipAuthoringLibrary.h/.cpp`
- `Source/CatfishingEditor/UI/Tests/CatItemTooltipTests.cpp`
- `Scripts/migrate_item_tooltip.py`
- `Scripts/create_frontend_assets.py`
- `Config/DefaultGame.ini`
- `Saved/Logs/FrontendIntegrationBuild.log`、`Saved/Logs/FrontendIntegrationBuild2.log`（失败记录，非完整构建通过证据）
- 本轮人工决策与主线程交接（2026-09-07）：仅麦克风选择和语音输入模式允许暂不可用；Build1 部分修复、Build2 待冻结后强制 UHT 重编，正式资产与 runtime 未验证。
- 本轮运行证据（2026-09-10）：Editor 构建通过；正式 Tooltip WBP 已迁移并在新进程加载，`Saved/Automation/Tooltip/TooltipRerun.log` 记录投影与生命周期两项通过；`Saved/Automation/Tooltip/NetworkFinal.log` 记录正式两端联机检查通过；打包资源尚待核对。
- `Source/Catfishing/UI/CatInteractionWidget.h`



