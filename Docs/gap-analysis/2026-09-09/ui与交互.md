## ui与交互

> 2026-09-09 后续用户裁决：多人钓鱼改为只有主控进入会话，旁人通过真实抓竿/抓猫与普通身体运动传力。本文早先盘点的辅助槽位、贡献系数、共享体力和自动接力属于旧方案，不再作为回填目标；当前变更及证据以 [正式物理抓握接入审查](../../FishingArchitecture_zh-CN.md) 为准。全场人数参与选鱼的独立规则保持。本文其他模块的盘点不因此变更。

### Features

| # | Design Requirement | Status | Code Reference | Notes |
|---|-------------------|--------|---------------|-------|
| 1 | 前端主菜单四项入口：开始游戏／加入队伍／设置／退出游戏（主界面.md:33） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:504,508,512,516；Source/Catfishing/UI/Frontend/CatFrontendPageController.h:36,39,42,45 | 四个按钮控件与四条 Request 入口一一对应，页面切换由 FrontendPageSwitcher（RootWidget.h:480）承载 |
| 2 | 退出游戏二次确认弹窗，Esc 取消／Enter 确认退出（主界面.md:17；Sheet2.csv 第25行 当前作用、第30行 当前作用） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendPageController.h:45,48,51；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:520,524；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:344-350 | 确认层状态与「确认／取消」两条意图都在，Esc 走 NativeOnKeyDown 显式处理；Enter 没有任何显式绑定，只能靠 UMG 焦点按钮的默认行为，代码里查不到「Enter＝确认退出」这条约定 |
| 3 | 「加入游戏」页：好友房间列表＋输入邀请码两种入口（主界面.md:21） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendPageController.h:39-40；Source/Catfishing/Online/CatOnlineSubsystem.h:51,55 | `RequestJoinParty` 注释明写「产品尚未定义流程，只保存可读反馈，不调用 RoomModel 的创建、搜索或加入接口」；底层 Online 已有 RequestJoinSession／RequestAcceptInvite，但前端既无好友房间列表页也无邀请码输入框 |
| 4 | 房间页：房间名、房间 ID（邀请码）、成员列表、空位、离开房间、开始游戏（主界面.md:25,45） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:803-841；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:576,580,596,604,608 | 成员行按 Snapshot.RoomMembers 生成，剩余容量补空位行；邀请码取真实 JoinLobbyUri，无法确认时显示「邀请码暂不可用」而不造假值 |
| 5 | 房间人数上限 4（主界面.md:25「已有4名玩家（总上限4人）」；09-08 裁决「单局上限锁死 4 人」） | ✅ Implemented | Source/Catfishing/Framework/Game/CatfishingGameModeBase.h:24；Source/Catfishing/Online/CatOnlineSubsystem.cpp:874,2296 | `MaxCampSpawnPlayers = 4` 同时约束 Session 公开容量与营地出生格数，Lobby 兼容校验也复核该值 |
| 6 | 房间内玩家「已准备」状态，全员准备后才能开始游戏（主界面.md:25,45） | ❌ Missing | — | `FCatOnlineRoomMember`（CatOnlineTypes.h:301-312）只有 DisplayName 与 bIsLobbyOwner；玩家行注释明写「不新增准备、人数或房主第二份状态」，槽状态固定写「已加入房间」（CatFrontendRootWidget.cpp:96）。开始条件是 `CanStartGame`＝房主且无并发操作（CatFrontendRoomModel.h:49），与准备无关 |
| 7 | 邀请好友：Steam 好友列表＋在线／游戏中／离线三态＋邀请按钮（主界面.md:47） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:57-68,786-802；Source/Catfishing/UI/Frontend/CatFrontendRoomModel.h:37,40 | 三态文案「在线，正在游玩／在线／离线」齐备，离线好友的邀请按钮置灰；FriendSearchTextBox 提供按名过滤 |
| 8 | 复制房间 ID／邀请码（主界面.md:45,49） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:312-318,852；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:612 | 复制的是平台确认过的 joinlobby URI，URI 为空时按钮禁用 |
| 9 | 房间设置：编辑房间名称、设置房间密码、改人数上限、开启语音聊天、保存设置（主界面.md:49） | ❌ Missing | — | RoomModel 只有 CreateRoom／LeaveRoom／StartGame／RefreshFriends／InviteFriend 五个写口（CatFrontendRoomModel.h:28-40），无任何房间属性写入；RoomName／MaxPlayers／SessionAccess 都是从 NamedSession 只读推导（CatOnlineSubsystem.cpp:216-219） |
| 10 | 房间底部「语音聊天」入口（主界面.md:25,45） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h:75-81；Config/DefaultGame.ini:38-40 | 语音开关与可用性查询在设置页（IsVoiceChatSettingAvailable／SetDraftVoiceChatEnabled），GameSession 也配了 bRequiresPushToTalk（**行号修正**：段头在 :38，值在 :40）；语音控件全部挂在 FrontendSettingsPage（VoiceChatCheckBox／VoiceInputModeComboBox／VoiceVolumeSlider，RootWidget.cpp:403-409）；房间页与派对菜单都没有语音入口控件 |
| 11 | 房间底部「查看玩家信息」入口（主界面.md:25） | ❌ Missing | — | 玩家行只暴露名称／角色／槽状态三个 TextBlock（CatFrontendRootWidget.h:117-127），无点击入口与详情页 |
| 12 | 选择存档页用左右键切换存档（主界面.md:37） | ❌ Missing | — | 存档行是点击式（CatFrontendRootWidget.cpp:47-54 HandleSelectClicked），Root 的 NativeOnKeyDown 只处理 Escape（CatFrontendRootWidget.cpp:344-350），没有左右方向键的切换逻辑 |
| 13 | 存档条目显示第 N 天、地点、游戏时长（主界面.md:41） | ⚠️ Partial | Source/Catfishing/Save/CatRunSaveGame.h:152-162；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:22-33 | `FCatSaveSlotSummary` 已经带 DayIndex／LocationName／PlayedDurationSeconds 三个字段，但存档行只渲染 DisplayName 与「最近保存：时间」两项，三个字段一个都没进 UI |
| 14 | 存档页快捷键：Enter 载入、N 新建、Esc 返回主菜单（Sheet2.csv 第24行、第29行、第34行 当前作用） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:552-572；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:344-350 | 新建／载入／删除／取消四个动作都有按钮和 Controller 入口，Esc 有键绑定；Enter 与 N 没有任何键处理代码 |
| 15 | 设置页四个选项卡：游戏／画面／声音／控制（主界面.md:61,63,65,89） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h:39-60；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:528-540；Source/Catfishing/UI/Save/CatLakeMainMenuWidget.h:341-353 | 前端设置页与局内菜单设置页各有一套四分类按钮，分类选择用四个明确接口而不是字符串分发 |
| 16 | 「辅助功能」选项卡及六项：界面缩放、文字大小、高对比度界面、色觉模式、减少镜头晃动、减少闪光效果（主界面.md:91） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h:126-129 | 只有界面缩放落在画面分类里（GetDraftUIScale／SetDraftUIScale）；文字大小、高对比度、色觉模式、减少镜头晃动、减少闪光五项以及「辅助功能」这个页签本身，全库 Grep 无任何字段或按钮 |
| 17 | 游戏设置项：游戏语言、语音聊天、语音输入模式、麦克风校准、震动（主界面.md:61） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h:63-96 | 语言有 Draft＋可选列表，语音聊天与震动有 Draft 读写，输入模式与麦克风以 IsInputModeSettingAvailable／IsMicrophoneSettingAvailable 的可用性查询形式接入平台能力 |
| 18 | 画面设置项：显示模式、分辨率、画质预设、垂直同步、亮度（主界面.md:63,85） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h:99-138 | 全息模式／分辨率（含 GetSupportedScreenResolutions）／OverallScalabilityLevel／VSync／DisplayGamma 五项俱全，Apply 时交给 UGameUserSettings（CatFrontendSettingsModel.cpp:576-577） |
| 19 | 画面设置项：帧率上限、抗锯齿、动态模糊（主界面.md:63,85） | ❌ Missing | — | Grep `FrameRate`／`AntiAlias`／`MotionBlur` 在 Source/ 下只命中钓鱼模拟的帧率无关性测试与一处编辑器截图 ShowFlag，设置模型里没有这三项 |
| 20 | 声音设置项：总音量、音乐、音效、环境声、语音音量、输出设备、后台静音（主界面.md:65,87） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendSettingsModel.h:141-201 | 五条音轨各有独立 Draft 读写，另有输出设备异步枚举／选择与 MuteAudioWhenUnfocused |
| 21 | 控制设置项：鼠标灵敏度、镜头灵敏度、反转 Y 轴、按键设置（主界面.md:89） | ❌ Missing | — | `RequestSelectControlsSettings` 注释明写「当前只进入正式空分类说明，不发明控制字段或输入映射」（CatFrontendPageController.h:107-108）；Grep `Sensitivity`／`InvertY` 在 Source/ 下零命中 |
| 22 | 设置页底部快捷键：R 恢复默认、Enter 应用、Esc 返回（主界面.md:85,91；Sheet2.csv 第26行、第31行、第35行 当前作用） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendPageController.h:87-96；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:344-350；Source/Catfishing/UI/Save/CatLakeMainMenuWidget.cpp:637 | 应用／取消／恢复默认三条意图都有按钮入口，Esc 在前端 Root 与局内菜单各有一处键处理；R 与 Enter 无键绑定 |
| 23 | 游戏加载界面：进度条＋阶段提示文字（主界面.md:69） | ✅ Implemented | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:500,620,624；Source/Catfishing/UI/Frontend/CatFrontendRoomModel.h:52,55 | LoadingPage 有进度条与阶段文本，进度取 Online 的真实包预载比例，未知时为 -1 且明写「加载页不得自行补百分比」 |
| 24 | 加载界面显示「第 n 天」与本局进度轴（09-08 已把「献祭进度」作废，改「世界进度」，另给当日任务点数与缸内可献点数）（主界面.md:69,71） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:696,727-741；Source/Catfishing/Save/CatRunSaveGame.h:152-154,165-171 | **复核改判（原判 ❌）**：加载页不止进度条与阶段文本。`RefreshLoadingPresentation` 里 `LoadingDayTextBlock` 写「存档记录：第 %d 天」（cpp:727-731），`LoadingSacrificeProgressTextBlock` 写「存档献祭记录：%d / %d」（cpp:732-738），来源是 `FCatSaveSlotSummary` 的 DayIndex／SacrificeProgress／SacrificeTarget。原报告只 Grep 了 `Quota`／`WorldProgress`／`世界进度`／`任务点数`，漏了 `SacrificeProgress`／`LoadingDayTextBlock` 这两个真正在用的名字。仍缺的是新口径：进度轴文案还叫「献祭进度」（09-08 已作废），未改成「世界进度」，也没有当日任务点数与缸内可献点数；且天数取的是存档记录天、Client 显示「房主尚未提供」，不是本局实时天。无 D 条目登记 |
| 25 | 左上角常驻显示「第 N 天」（Sheet1.csv 第2行 UI内容；主界面.md:75） | ✅ Implemented | Source/Catfishing/UI/HUD/CatHUDWidget.h:36-40,292-294；Source/Catfishing/UI/HUD/CatHUDModel.cpp:142-146 | 天数只读 GameState 公开快照，HUD 不本地推进；文本固定为「第 %d 天」 |
| 26 | 点击左上角天数进入主页菜单（Sheet1.csv 第2行 交互方式） | ⚠️ Partial | Source/Catfishing/UI/HUD/CatHUDWidget.h:292-294,328-330；Source/Catfishing/UI/HUD/CatHUDWidget.h:17-23 | 主页菜单入口存在，但绑在独立的 MainMenuButton 上；天数控件是 UTextBlock，不是可点击入口 |
| 27 | ESC 菜单，含设置、音频、操作等基础功能（Sheet1.csv 第3行；Sheet2.csv 第28行 当前作用） | ✅ Implemented | Source/Catfishing/UI/CatLocalPlayerUISubsystem.h:108-112；Source/Catfishing/UI/Save/CatLakeMainMenuWidget.h:309-325,341-353；Scripts/create_inventory_input_assets.py:134,143 | IA_LakeMenu 由脚本断言映射到 Escape；菜单含设置（四分类）、保存、返回主菜单、退出与关闭 |
| 28 | 左上角猫爪印图标常驻，点击打开鱼图鉴（Sheet1.csv 第4行） | ❌ Missing | — | HUD 只枚举两种入口意图 OpenMainMenu／OpenInventory（CatHUDWidget.h:17-23），没有第三个入口控件或动作；Grep 全 Source 无图鉴入口按钮 |
| 29 | 图鉴页面：按 M 打开，鱼种解锁、追踪剪影鱼种（Sheet1.csv 第16行） | ⚠️ Partial | Source/Catfishing/UI/Collection/CatCollectionModel.h:16-34；Source/Catfishing/UI/Collection/CatCollectionWidget.h:37-54；Scripts/verify_ui_reach_runtime.py:133,143,170；Source/Catfishing/UI/Collection/CatCollectionModel.cpp:44-57；Source/Catfishing/Framework/Core/CatProfileContracts.h:21-31 | Model／Widget／只读投影三件都在，`/Game/UI/Collection` 也在 Cook 目录里（DefaultGame.ini:31）；但全 Source Grep `CatCollectionWidget`／`CatCollectionModel` 在 UI/Collection 之外零引用——没有任何 C++ 装配路径，CatUISettings 只剩 HUD/Frontend/Inventory/InventorySlot/InteractionPrompt/LakeMainMenu 六个 WidgetClass（CatUISettings.h:74-94），已无 CollectionWidgetClass；verify_ui_reach_runtime.py 仍在读该字段（连 ShopWidgetClass 一起，两个字段都已删），脚本本身已失效。**复核修正 Notes**：「鱼种解锁」这一半是有的——`ECatFishCollectionState` 三态 Unknown/Silhouette/Recorded（CatProfileContracts.h:21-31）已进 Entry.State 与行文案（CatCollectionModel.cpp:47,51）；无实现的是「**追踪**剪影鱼种」（Grep `追踪`／`Track` 在 UI/ 与 Profile/ 零命中）与 M 键 |
| 30 | 左下角背包图标常驻，点击打开背包（Sheet1.csv 第5行） | ✅ Implemented | Source/Catfishing/UI/HUD/CatHUDWidget.h:21-22,332-334；Source/Catfishing/UI/CatLocalPlayerUISubsystem.h:51,111-112 | 点击广播 OpenInventory，由子系统转交 Inventory PageController |
| 31 | 按 B 弹出背包、再按 B 关闭（Sheet1.csv 第6行 交互方式；主界面.md:95；Sheet2.csv 第8行 按键） | 🔄 Divergent | Scripts/create_inventory_input_assets.py:133,144,185；Config/DefaultGame.ini:17；Source/Catfishing/UI/CatLocalPlayerUISubsystem.h:51 | 开关语义有（ToggleInventory），但键是 **Tab** 不是 B：脚本硬断言「IA_Inventory 未映射到 Tab」并在成功日志里写 `InventoryKey=Tab`。**无对应 D 条目**——工程自补决策记录里 D-02 只覆盖移动／视角键位，背包键从 B 改 Tab 未登记 |
| 32 | 屏幕右下角提竿提示「鱼儿咬钩啦！」F 提竿，鱼漂明显下沉时出现（Sheet1.csv 第7行） | ⚠️ Partial | Source/Catfishing/UI/HUD/CatHUDModel.cpp:204；Source/Catfishing/UI/HUD/CatHUDWidget.h:123-125,163-165,296-298 | 提示文本与显隐（只在 TrueBiteWindow 阶段）都在，但文案是「鱼儿咬钩啦！提竿」，字段注释明写「默认文案不绑定具体按键，按键图标可由 WBP 覆盖」——设计要求的 F 键提示在 C++ 层不存在 |
| 33 | 提竿倒计时，环形进度／缩短提示（Sheet1.csv 第8行） | ✅ Implemented | Source/Catfishing/UI/HUD/CatHUDModel.cpp:190-217；Source/Catfishing/UI/HUD/CatHUDWidget.h:127-129,139-141,300-302,344-346 | HookCountdownPercent 直接绑 ProgressBar，剩余秒数另有文本；进度只来自复制快照的服务器时间窗口 |
| 34 | 提竿成功反馈，右下角短暂出现（Sheet1.csv 第9行） | ✅ Implemented | Source/Catfishing/UI/HUD/CatHUDModel.cpp:205；Source/Catfishing/UI/HUD/CatHUDWidget.h:131-133,171-173,304-306 | bShowHookSuccessFeedback 供 WBP 触发一次性动画 |
| 35 | 钓鱼状态下的鱼漂反馈，自动显示（Sheet1.csv 第10行） | ✅ Implemented | Source/Catfishing/UI/HUD/CatHUDModel.cpp:222-267；Source/Catfishing/UI/HUD/CatHUDWidget.h:179-181,312-314 | 覆盖抛竿中／平稳／轻微晃动／明显下沉／已中鱼／回线中等十种阶段文案 |
| 36 | 遛鱼状态显示玩家体力，仅钓鱼过程中存在（Sheet1.csv 第11行） | ✅ Implemented | Source/Catfishing/UI/HUD/CatHUDModel.cpp:207-212；Source/Catfishing/UI/HUD/CatHUDWidget.h:119-121,187-189,320-322,336-338 | bShowFightMeters 只在遛鱼相关阶段为 true；同竿时显示总体力与人数，离竿后显示本人余额 |
| 37 | 遛鱼状态显示鱼状态（挣扎、疲劳等）（Sheet1.csv 第12行） | ✅ Implemented | Source/Catfishing/UI/HUD/CatHUDModel.cpp:273-286；Source/Catfishing/UI/HUD/CatHUDWidget.h:183-185,316-318,324-326,340-342 | 强烈挣扎／向外挣扎／可拖回／回游或疲劳四态，另有鱼体力条与百分比文本 |
| 38 | 商店：靠近商人交互打开商品页（Sheet1.csv 第13行） | ✅ Implemented | Source/Catfishing/ShopEconomy/CatShopKioskActor.h:23-26；Source/Catfishing/UI/Shop/CatShopInteractionComponent.h:26；Source/Catfishing/UI/Shop/CatShopInteractionComponent.cpp:16-17,53-63 | 摊位实现 ICatInteractable，交互时由组件按自己的 WBP 软类创建 Model／PageController／View |
| 39 | 首次解锁新鱼种时在钓点附近自动展示鱼种特写（名称、品种介绍、重量，Space 继续）（Sheet1.csv 第14行；主界面.md:121-125；Sheet2.csv 第21行 当前作用） | ❌ Missing | — | Grep `首次`／`NewSpecies`／`Discovery` 在 UI 与 Collection 下没有任何首解锁展示页；Profile 侧只有 durable 图鉴记录（CatCollectionWidget.h:12-35），没有一次性揭示界面，Space 也无对应绑定 |
| 40 | 他人解锁新鱼种时给同房其他玩家提示，且提示期间仍可移动、交互、继续钓鱼（主界面.md:129） | ❌ Missing | — | 无广播型解锁提示：CatRunImprintService 只生成 Grant 并交投递记录（CatRunImprintService.cpp:87-106），不产生任何 UI 事件；HUD 投影里也没有他人解锁字段 |
| 41 | 营地装备与仓库：回营地打开，出行装备与仓库浏览（Sheet1.csv 第15行） | ✅ Implemented | Source/Catfishing/Camp/CatCampInventoryActor.h:67-73,265-271；Source/Catfishing/UI/CatLocalPlayerUISubsystem.h:61-63；Source/Catfishing/UI/Inventory/CatCampInventoryWidget.h:1-21 | 公共仓库有独立 WBP 类与取用动作 WithdrawCampInventoryItem（CatInventoryTypes.h:29-30） |
| 42 | 背包装备栏四格：鱼竿、鱼饵、冰壶（窝料）、鱼篓（鱼护）（主界面.md:97） | ⚠️ Partial | Source/Catfishing/UI/Inventory/CatInventoryModel.cpp:160-162；Source/Catfishing/Equipment/CatEquipmentTypes.h:11-31 | 当前选择文本只汇总鱼竿（含耐久）／鱼饵／鱼漂三格；ECatEquipmentKind 里 Chum（窝料）存在但不进这条选择投影，鱼护是世界 Actor（CatFishGuardActor）不属装备类别，四格布局与代码的三格选择对不上。**复核补充**：这不是漏做而是代码明确回避——`MakeSelectionText` 上方注释写「文案刻意叫『选择』而不是『装备槽』，避免 UI 继续暗示独立装备栏」（CatInventoryModel.cpp:152），设计的四格装备栏与代码口径正面冲突，但**无 D 条目登记** |
| 43 | 湖边／抛竿界面，鼠标左键抛竿（Sheet2.csv 第2行 按键；主界面.md:105；钓鱼规则.md:119） | ❓ Unverifiable | Source/Catfishing/AbilitySystem/Tags/CatFishingAbilityTags.cpp:6；Source/Catfishing/AbilitySystem/Fishing/InputAbilities/CatFishingPrimaryActionAbility.h | 抛竿意图 `Cat.Input.Fishing.Primary` 与对应 Ability 都在；但实际按键在 `/Game/Input/InputContext/IMC_InputContext.uasset` 里，属④类，本轮不跑引擎，无法确认它是否绑到鼠标左键。Config/DefaultInput.ini 只有引擎默认轴配置，不含本项目的 Action 键位 |
| 44 | 抛竿后可放置鱼竿，按 E 确认位置、放置后进入等待钓获（主界面.md:107,109；Sheet2.csv 第9行） | ❓ Unverifiable | Source/Catfishing/AbilitySystem/Tags/CatFishingAbilityTags.cpp:5；Source/Catfishing/AbilitySystem/Fishing/InputAbilities/CatFishingRodInteractAbility.h | 竿交互意图 `Cat.Input.Fishing.RodInteract` 与 Ability 存在；键位同样只在 IMC 资产里，查不了 |
| 45 | 遛鱼状态：按住鼠标左键收线、按住鼠标右键放线（Sheet2.csv 第4行、第7行） | ❓ Unverifiable | Source/Catfishing/AbilitySystem/Tags/CatFishingAbilityTags.cpp:6,10；Source/Catfishing/UI/CatFishingViewTypes.h:49,52 | 收线／放线两条意图（Primary／Slack）与投影字段 bReeling／bSlacking 都在，且「按住」语义由 Ability 激活策略 WhileInputActive 标签支持（CatFishingAbilityTags.h:22）；具体按键在 IMC，查不了 |
| 46 | 抄鱼键（Sheet2.csv 第20行写 Space；以钓鱼规则.md:246、353 为准＝F） | ❓ Unverifiable | Source/Catfishing/AbilitySystem/Tags/CatFishingAbilityTags.cpp:8；Source/Catfishing/AbilitySystem/Fishing/InputAbilities/CatFishingScoopAbility.h | 抄网意图与 Ability、独立冷却标签（CatFishingAbilityTags.h:29）都在；键位在 IMC 查不了。两份设计自相矛盾：Sheet2 写 Space，钓鱼规则页写 F，按本任务口径以钓鱼规则为准，Sheet2 该行应更正 |
| 47 | 鱼落地后按 F 拾取（Sheet2.csv 第15行；钓鱼规则.md:256） | 🔄 Divergent | Source/Catfishing/Items/World/CatFishPickupActor.h:76；Source/Catfishing/Interaction/CatInteractionTags.cpp:5；Source/Catfishing/UI/CatUISettings.h:104-106 | 拾取没有独立按键：地上鱼实现 ICatInteractable，走全局唯一的 `Cat.Input.Interact`（IA_Interact），代码注释明写「对死鱼按 **E** 后由服务器附着到角色嘴部」。设计要求 F 拾取、E 另作他用（查看鱼护／鱼缸／商人），单一交互 Action 结构上无法同时满足。**无 D 条目登记** |
| 48 | 嘴里叼着鱼时：F 放下鱼、长按 E 吃掉鱼（Sheet2.csv 第11行、第16行） | ❌ Missing | — | `ReleaseMouthCarryFromAuthority` 是私有函数，只在持有者销毁等内部路径调用（CatFishPickupActor.h:104），没有玩家侧「放下」命令；吃鱼唯一入口是背包里对容器内鱼提交 ConsumeSelectedFish（CatInventoryPageController.cpp:767-771），不是对嘴里的鱼长按 |
| 49 | F 叼起鱼护、携带鱼护时 F 放下鱼护（Sheet2.csv 第17行、第18行；主界面.md:155,157） | ❌ Missing | — | Grep `PickUpGuard`／`CarryGuard`／`GuardCarry` 全 Source 零命中；ACatFishGuardActor 只有「打开本地库存页并提交嘴上叼鱼」一条交互（CatFishGuardActor.h:42），鱼护是固定地面容器，没有搬运态 |
| 50 | 玩家靠近湖面时：附近有窝则屏幕右下角显示当前区域窝点信息，附近无窝则不显示任何提示（交互.md:68-76） | ❌ Missing | — | Grep `Chum` 在 Source/Catfishing/UI/ 下只命中背包里的装备类别枚举分支（CatInventoryModel.cpp:110）；HUD 投影里没有窝点字段，也没有「附近有窝／无窝」的本地判定 |
| 51 | 按 T 查看鱼窝范围／查询当前鱼窝（主界面.md:99；Sheet2.csv 第22行） | ❌ Missing | — | 窝点范围环是常驻程序化表现，随窝点存在即显示（CatChumFieldPresentationActor.h:27），不是按键切换；无 T 键 Action、无查询入口 |
| 52 | 按 T 在鱼图鉴里追踪鱼类（Sheet2.csv 第23行） | ❌ Missing | — | 图鉴投影只有 FishDefinitionId／State／BestWeight／EncounterCount 四个只读字段（CatCollectionWidget.h:12-35），没有追踪标记字段或写口；图鉴页面本身也未接运行链（见第 29 行） |
| 53 | 小猫靠近鱼缸／鱼护进入交互范围时，镜头轻微转向目标（交互.md:17） | ❌ Missing | — | 目标选取是准星射线（CatInteractionTargetingComponent.cpp:45-91，20Hz、3 米），没有任何镜头朝向修正代码。注：交互.md 的「靠近式」本身已被 09-07 N5「F 目标按视角中心选」覆盖，代码走的是新口径，但「镜头轻微转向」这条呈现要求两版都没实现 |
| 54 | 可交互物体出现淡淡描边高亮效果（交互.md:18） | ⚠️ Partial | Source/Catfishing/Items/World/CatFishPickupActor.cpp:568-577,587-595；Source/Catfishing/Interaction/CatInteractionSettings.h:34-36；Source/Catfishing/Interaction/CatInteractable.h:32-38 | 接口留了 BeginLocalFocus／EndLocalFocus 钩子，FocusStencilValue=1 也已保留为「普通白色交互描边」；但只有地上鱼实现了（SetRenderCustomDepth），鱼缸、鱼护、营地仓库、商店摊位四个 Interactable 都没有覆写焦点钩子。描边材质本身属②之外的资产层 |
| 55 | 靠近鱼缸／鱼护显示提示「【F】取出鱼」（交互.md:20-21） | ⚠️ Partial | Source/Catfishing/UI/Interaction/CatInteractionPageController.cpp:92-101；Source/Catfishing/UI/CatUISettings.cpp:188-204；Source/Catfishing/Items/CatFishTankActor.h:72-73；Source/Catfishing/Items/CatFishGuardActor.h:91-93 | 提示层在：文案模板「按 {键} {动作}」，键名从 IMC 反查 IA_Interact，动作文本由各 Actor 的 InteractionPrompt 编辑器字段提供。差在两点——键固定是通用交互键（IA_Interact，见第 47 行）而非 F；动作文案「取出鱼」是资产里填的值，C++ 里查不到 |
| 56 | 按下取鱼键后播放取鱼动作、镜头推进到鱼的特写（交互.md:23） | ❌ Missing | — | D-28（08-20，暂定）明记「本轮只做信息层，镜头推进与取鱼动作留空，不用任何替代演出顶替」，等美术与动画到位后补。当前工作区仍无对应实现 |
| 57 | 取鱼后左上角显示鱼的信息（名称、品种、重量）（交互.md:24） | 🔄 Divergent | Source/Catfishing/UI/Inventory/CatInventoryTypes.h:15-34；Source/Catfishing/Items/CatFishTankInteractionComponent.h:19-21；Source/Catfishing/UI/CatLocalPlayerUISubsystem.h:53-59；Source/Catfishing/UI/Inventory/CatInventoryModel.cpp:748-765 | 实现改成了另一套形态：鱼缸／鱼护交互＝把容器作为外部上下文打开背包页，在格子里看鱼，而不是「逐条特写＋左上角信息」。**复核补充**：设计要的三项信息本身在容器格投影里齐了——DisplayName（鱼名）、Description（品种介绍）、`%.2f kg`（重量）都由 `MakeSlotView` 从 FishDefinition 写入（CatInventoryModel.cpp:750-765），差的只是呈现形态（格子列表 vs 左上角逐条特写）。**D-27／D-28（均为暂定）登记过这条歧义**，但 D-27 的取值位置 `CatLocalPlayerUISubsystem.cpp` 的 AdvanceFishObservation／ClearFishObservation／FindObservedFish 与 D-28 的 `CatPersistentHudWidget.cpp` 在当前工作区 Grep 全部零命中——两条 D 条目记录的实现已被替换，**D-27／D-28 需复核**；设计侧交互.md 自 08 月以来未改，不属「设计已更新」情形 |
| 58 | 与石像互动显示提示「【F】献给圣猫」（交互.md:36-37） | ❌ Missing | — | 全 Source 无祭坛／石像 Actor：实现 ICatInteractable 的只有营地仓库、鱼护、鱼缸、地上鱼、商店摊位五类。献祭只有服务器 RPC（CatfishingPlayerController.h:117-119）＋背包里的 SacrificeSelectedFish 动作（CatInventoryTypes.h:27-28），没有世界内祭坛交互点与提示 |
| 59 | 献祭呈现要区分三个量：当日任务点数、缸内可献点数、世界进度（交互.md:41） | ❌ Missing | — | Grep `Quota`／`WorldProgress`／`世界进度`／`任务点数` 在 Source/Catfishing/UI/ 下零命中；HUD 投影（CatHUDWidget.h:29-194）里三个量一个都没有 |
| 60 | 世界进度平时隐藏，靠近神像或打开界面时查看（交互.md:41） | ❌ Missing | — | 同上，没有进度量就没有显隐规则；HUD 的显隐标记只覆盖天数入口、背包入口、准星、钓鱼状态、体力条、咬钩提示六类 |
| 61 | 篝火：夜晚靠近出现柔和光效提示，显示【F】坐下，坐下进入松弛状态、别的猫凑过来叠成一堆（交互.md:49-55） | ❌ Missing | — | 无篝火 Interactable，也无「坐下」提示或松弛状态。相关代码只有 CampfirePlayback（印记轮播，CatCampBodyActionAbilities.h:98-122、CatCampHubActor.h:15,46）——而印记功能已裁为 Demo 后置、篝火不做照片轮播，这条链路与「坐下」是两回事 |
| 62 | 玩家靠近湖面时按 F 钓鱼（交互.md:66） | 🔄 Divergent | Source/Catfishing/AbilitySystem/Tags/CatFishingAbilityTags.cpp:5-10 | 代码没有「进入钓鱼模式」这个键，钓鱼靠手持鱼竿＋左键点水面。这是设计内部冲突而非代码走偏：钓鱼规则.md:25 明写「装备即状态：手持鱼竿就是钓鱼待机，**不设模式键**」，规则页为 SSOT，交互.md 这一段是应作废的草稿口径 |
| 63 | E 键：靠近鱼护查看鱼护、靠近营地鱼缸放入鱼、鱼缸交互完成后放回鱼护、商人对话确认选择（Sheet2.csv 第10行、第12行、第13行、第14行） | ✅ Implemented | Source/Catfishing/Interaction/CatInteractionTags.cpp:5；Source/Catfishing/Items/CatFishGuardActor.h:42；Source/Catfishing/Items/CatFishTankInteractionComponent.h:19-21；Source/Catfishing/UI/Inventory/CatInventoryTypes.h:31-32 | 四个场景都收敛到同一个通用交互意图（注释写「默认由 E 触发」）；鱼缸转存有专门动作 StoreSelectedFishInSharedTank。实际键名仍在 IMC，但这四条要求的是「E 一键交互」这一形态，形态成立 |
| 64 | Esc 在游戏中打开或关闭派对菜单（Sheet2.csv 第28行） | ✅ Implemented | Source/Catfishing/UI/CatLocalPlayerUISubsystem.h:108-109；Source/Catfishing/UI/Save/CatLakeMainMenuWidget.cpp:637；Scripts/create_inventory_input_assets.py:143 | ToggleLakeMainMenu 打开前会先关背包，避免两层模态争焦点 |
| 65 | Esc 在商店对话、背包、设置页返回上一层（Sheet2.csv 第31行、第33行；背包页同理） | ✅ Implemented | Source/Catfishing/UI/Shop/CatShopWidget.cpp:793；Source/Catfishing/UI/Inventory/CatInventoryWidget.cpp:556；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:344-350 | 三处各自处理 Escape 键名 |
| 66 | 鱼缸放鱼界面：Tab 进入「选择放入」、Enter 全部放入／确认当前选择（Sheet2.csv 第27行、第36行） | ❌ Missing | — | 鱼缸交互直接开背包容器页，没有「选择放入」子模式；ECatInventoryAction 里也没有批量放入动作（CatInventoryTypes.h:15-34），Tab 已被背包开关占用（见第 31 行） |
| 67 | 派对菜单项：派对设置（主界面.md:79） | ❌ Missing | — | 局内菜单只有设置、保存、返回主菜单、退出、关闭五个按钮（CatLakeMainMenuWidget.h:309-325），没有派对设置页 |
| 68 | 派对菜单项：组队管理（队伍 x/4、等待队列、Steam 好友邀请、仅好友／仅邀请、复制房间密码）（主界面.md:79,81） | ⚠️ Partial | Source/Catfishing/UI/Frontend/CatFrontendRootWidget.cpp:786-802,843-852；Source/Catfishing/UI/Frontend/CatFrontendRootWidget.h:584 | 好友邀请、成员／空位、访问策略文本（公开／仅好友／仅邀请）、复制邀请码这几件事在**前端房间页**都有；但局内派对菜单没有组队管理入口，访问策略是只读展示不可切换，也没有等待队列 |
| 69 | 派对菜单项：申请暂停，中途暂停需经房主同意（主界面.md:77,79） | ❌ Missing | — | Grep `Pause`／`暂停` 在 Source/ 下只命中 ASC 的 bGamePaused 输入边沿处理与钓鱼模拟的内部计时暂停，没有玩家发起的暂停申请、房主审批或对应 UI |
| 70 | 派对菜单项：钓鱼图鉴入口（主界面.md:79） | ❌ Missing | — | 同第 28、29 行：局内菜单五个按钮里没有图鉴，图鉴 Widget 也没有任何 C++ 装配点 |
| 71 | 鱼护界面操作：收、放、丢弃、取鱼（主界面.md:149） | ⚠️ Partial | Source/Catfishing/UI/Inventory/CatInventoryTypes.h:21-26,31-32；Source/Catfishing/UI/Inventory/CatFishGuardInventoryWidget.h:1-21 | 取／放走容器间拖拽 MoveObjectBetweenContainers，鱼护有独立 WBP 基类；「丢弃」（把鱼扔回世界）在 ECatInventoryAction 里没有对应动作 |
| 72 | 鱼缸界面操作：放生、取鱼、放入鱼护（主界面.md:163） | ⚠️ Partial | Source/Catfishing/UI/Inventory/CatInventoryTypes.h:21-22,31-32；Source/Catfishing/Items/CatFishTankActor.h:23-26 | 取鱼与放入／取出走容器拖拽，转存鱼缸有专门动作；「放生」全 Source 无对应动作或命令 |
| 73 | 鱼缸展示鱼的重量与数量 x/容量（主界面.md:161） | ⚠️ Partial | Source/Catfishing/UI/Inventory/CatInventoryModel.cpp:761-765；Source/Catfishing/UI/Inventory/CatInventoryModel.cpp:196-211,447-453；Source/Catfishing/UI/Inventory/CatInventoryTypes.h:180-196 | **复核改证据（原 Code Reference 与 Notes 有误）**：容器格投影**有**重量——`MakeSlotView` 对鱼格写 `DisplayText = "{容器名}\n第 N 格\n{鱼名}\n%.2f kg"`（CatInventoryModel.cpp:761-765），并同时填 `Slot.Fish.WeightKilograms`、DisplayName、Description、Thumbnail。原报告说「容器格投影里没有重量字段」不成立（漏搜 `kg`／`WeightKilograms`）。真正缺的只有「已用/容量」汇总文案：`MakeInventoryItemsText`（cpp:196-211）只给随身库存写「%d/%d 格有物品」，`FCatInventoryContainerView` 只暴露 SlotCount 与 Snapshot.Capacity 原料（Types.h:180-196），没有对应的鱼缸汇总文本——WBP 可自行拼，属④ |
| 74 | 左上角同时显示当前时段（清晨／白天／夜晚）（主界面.md:75） | ❌ Missing | — | HUD 投影只有 DayIndex 与 DayText（CatHUDWidget.h:34-40）；Run 阶段虽被订阅（CatHUDModel.h:78-79）但只用于重读天数，没有生成时段文本。**复核补充（数据侧其实有）**：`ECatEnvironmentTimeOfDay`（Unknown/Morning/Day/Dusk）与 `RunPublicState.TimeOfDay` 已在合同里（CatRunContracts.h:119-131,182-184），只是 UI 层没读——Grep `TimeOfDay`／`清晨`／`黄昏` 在 Source/Catfishing/UI/ 下零命中。缺的是投影与文案，不是数据源 |
| 75 | 钓鱼界面左下角常驻显示鱼竿耐久（主界面.md:119「左下角标注鱼竿耐久780」，与左上角体力同屏） | ⚠️ Partial | Source/Catfishing/UI/Inventory/CatInventoryModel.cpp:158-166；Source/Catfishing/UI/Inventory/CatInventoryWidget.h:188,208 | **复核新增行（原报告漏列）**：耐久值本身有且已进 UI——`Equipment.RodDurability` 与 `bRodBroken` 写进背包「当前选择」文案（「鱼竿 X（耐久 780，已断）」）。但那是**背包页**，要开背包才看得到；HUD 只读投影 `FCatHUDViewState`（CatHUDWidget.h:29-194）里没有任何耐久字段，也没有对应 BindWidgetOptional 控件，钓鱼时的常驻耐久显示在 C++ 层不存在。Grep 关键词：`Durability`／`耐久`／`RodDurability` |
| 76 | 营地装备与仓库界面附带「追踪鱼轻提示」（Sheet1.csv 第15行 备注） | ❌ Missing | — | **复核新增行（原报告只评了同格的「出行装备、仓库浏览」，漏了备注里并列的第三项）**：Grep `追踪`／`Track`／`Tracked`／`Pinned` 在 Source/Catfishing/UI/ 与 Source/Catfishing/Profile/ 下零命中；营地仓库交互只有打开自己 WBP＋WithdrawCampInventoryItem 一条链（CatCampInventoryActor.h:67-79），无任何追踪目标状态或提示投影。与第 52 行（T 键图鉴追踪）同源：全库没有「追踪鱼」这个概念 |
| 77 | 提竿按键（Sheet1.csv 第7行写「F提竿」；Sheet2.csv 第3行写「鼠标左键，鱼儿咬钩，提竿」；钓鱼规则.md:350 只给了「抛竿＝左键点水面」，未单列提竿键） | 🔄 Divergent | Source/Catfishing/AbilitySystem/Fishing/InputAbilities/CatFishingPrimaryActionAbility.h:7,17-23；Source/Catfishing/AbilitySystem/Tags/CatFishingAbilityTags.cpp:6 | **复核新增行（原报告漏列这条按键要求，只评了第 32 行的提竿提示文案）**：结构上提竿不是独立按键——`UCatGA_FishingPrimaryAction` 类注释明写「按下建立按住状态，松开时让服务器按当前阶段解释为**抛竿、提竿或收线**」，三件事共用同一个 `Cat.Input.Fishing.Primary`。因此代码口径＝Sheet2 第3行（左键提竿），与 Sheet1 第7行的「F提竿」结构上不可能同时成立；F 已被抄鱼占用（钓鱼规则.md:246）。属**设计内部冲突**（Sheet1 与 Sheet2 打架，规则页未裁），代码跟的是规则页＋Sheet2 一侧；Sheet1 第7行应更正。具体键仍在 IMC 资产（④），但「提竿有没有独立键」这件事在 C++ 可判 |

### Summary
- Total features: 77
- ✅ Implemented: 23
- ⚠️ Partial: 19
- ❌ Missing: 26
- 🔄 Divergent: 5
- ❓ Unverifiable: 4
- Inspectable: (77 - 4) / 77 * 100% = 94.8%
- Coverage of inspected: (23 + 0.5 * 19) / (77 - 4) * 100% = 44.5%

### Scan Scope

**扫过的目录与文件类型**

- `Source/Catfishing/UI/**`（全部 .h/.cpp，含 HUD、Frontend、Interaction、Inventory、InventorySlot、Collection、Shop、Save 八个子目录与 Tests）
- `Source/Catfishing/Interaction/`、`Source/Catfishing/Input/`、`Source/Catfishing/Items/`（含 `World/`）、`Source/Catfishing/Camp/`、`Source/Catfishing/Online/`、`Source/Catfishing/Save/`、`Source/Catfishing/ShopEconomy/`、`Source/Catfishing/AbilitySystem/Tags|Fishing/InputAbilities|BodyAction/Camp`、`Source/Catfishing/Environment/Presentation/`、`Source/Catfishing/Framework/Game/`、`Source/Catfishing/Collection/`、`Source/Catfishing/Profile/`
- `Config/DefaultInput.ini`（全文）、`Config/DefaultGame.ini`（全文）
- `Scripts/*.py`、`Scripts/*.ps1`（重点读 `create_inventory_input_assets.py`、`verify_ui_reach_runtime.py`）
- 台账：`Docs/Development/工程自补决策记录.md`（D-01…D-32 逐条过，重点 D-02／D-27／D-28）、`Docs/Development/需求对齐差距清单.md`（作为线索，不作为 ✅ 依据）

**检查方法**

Grep 定位（类名 `UCatHUDWidget`／`UCatCollectionModel`、Gameplay Tag 字面量 `Cat.Input.*`、字段名 `MaxPlayers`／`FocusStencilValue`、中文注释与 UI 文案「提竿」「鱼漂反馈」「邀请码」、Config 段名 `[/Script/Catfishing.CatUISettings]`、按键名 `EKeys::`／`_make_key`）→ Read 片段确认上下文 → 记 file:line。所有 ✅ 都给出了 Source/Config/Scripts 里的行号，没有只凭 `需求对齐差距清单.md` 的「已完成」记录下结论。

**没有覆盖的部分（三种「没有」分开写）**

- **查不了（有，但当前手段验证不了）**：
  - `Content/**` 的 `.uasset`/`.umap` 全部未读。具体到本页评分，最关键的是 `/Game/Input/InputContext/IMC_InputContext.uasset` —— 除背包（Tab）与局内菜单（Escape）两条被 `Scripts/create_inventory_input_assets.py` 以断言方式固定在文本里之外，**所有其他键位（抛竿左键、放线右键、抄鱼、拾取、交互 E、T、M、N、R、Enter）都只存在于这张 IMC 资产里**，本轮不跑引擎，一律不判缺失（第 43–46 行即为此类）。
  - WBP 蓝图图逻辑：`/Game/UI/HUD/WBP_CatHUD`、`WBP_CatInventory`、`WBP_CatInteractionPrompt`、`WBP_CatFrontendRoot`、`WBP_CatShop`、`WBP_CatCollection`、`WBP_CatLakeMainMenu` 等的控件布局、动画与 `BP_Render*` 覆写内容。C++ 侧用 `BindWidgetOptional` 声明了期望控件名，本报告只据这些声明判「投影字段与显隐标记是否存在」，不判 WBP 里是否真的摆了控件。
  - 描边高亮所用的后处理材质（第 54 行）、取鱼动作与镜头推进所需的 Montage/Sequence（第 56 行）——前者属资产层，后者 D-28 已明记留空。
- **不适用（结构上就没有）**：
  - Sheet2.csv 第37行「Shift + Tab 打开 Steam 界面」由 Steam Overlay 客户端提供，不经游戏代码，故未列入评分表，也不计为缺失。
  - `Source/**/Tests/` 下的 `CatHUDVisibilityTests.cpp`、`CatHUDCooperativeFishingTests.cpp` 只用作交叉验证 HUD 显隐与合力体力投影，未把「仅测试里出现」的东西计为已实现。
- **未知（本轮没查）**：
  - `Docs/Development/UI_WBP拼装接口清单.md`（662 行）只作参考未逐条比对；它是工程侧拼装说明，不是设计文档，按任务口径不作为评分对象。
  - 图鉴的 Blueprint 侧是否存在自建入口（例如某张 WBP 自己 CreateWidget 出 `WBP_CatCollection`）：C++ 与 Config 里确无装配路径且 CatUISettings 已无 CollectionWidgetClass 字段，因此按 ⚠️ Partial 记，而不是 ❌，也不是 ❓。

**设计侧口径处理**

- 「GDD 系统分册/ui/主界面.md」与「交互.md」两页均自标草稿，主界面.md 正文几乎全是参考图说明。评分只取图注与正文里明确成句的机制／字段／反馈要求。
- 键位冲突按任务口径以「钓鱼规则.md」为准：抄鱼 F（第 46 行）、抛竿左键（第 43 行）、「靠近湖面 F 钓鱼」被规则页 §「装备即状态、不设模式键」覆盖（第 62 行）。
- 主界面.md:71 与 :169 两处「献祭进度」参考图已由 09-08 一致性检查加注作废，改称「世界进度」；本报告按新口径评（第 24、59、60 行）。
- 主界面.md:117「若完美中鱼会显示一秒力量体力下降提示（修改没有）」自标已取消，未列入评分表。

### 复核记录

复核口径：对分析者报的每一行 ❌／🔄／⚠️ 自己重搜一遍（换英文类名、字段名、中文文案、Config 段名多轮 Grep），再抽验 ✅ 与 ❓ 各 ≥5 行，最后对照两份设计文档的标题级别与表格行补漏。

| # | 原判 | 改判 | 依据 file:line 或搜过的关键词 |
|---|------|------|------------------------------|
| 24 | ❌ Missing | ⚠️ Partial | **推翻**。CatFrontendRootWidget.cpp:696,727-731（`LoadingDayTextBlock` ←「存档记录：第 %d 天」）、:732-738（`LoadingSacrificeProgressTextBlock` ←「存档献祭记录：%d / %d」）、CatRunSaveGame.h:152-154,165-171。原报告只搜 `Quota`／`WorldProgress`／`世界进度`／`任务点数`，漏搜 `SacrificeProgress`／`SacrificeTarget`／`Loading*TextBlock`。仍缺新口径三个量 |
| 73 | ⚠️ Partial | ⚠️ Partial（证据推翻） | **Notes 与 Code Reference 全换**。CatInventoryModel.cpp:761-765 容器鱼格 DisplayText 明写 `%.2f kg`，原报告「容器格投影里没有重量字段」不成立。搜过 `kg`／`WeightKilograms`／`SlotCount`／`Capacity`／`容量` |
| 29 | ⚠️ Partial | ⚠️ Partial（Notes 修正） | 「鱼种解锁」已实现：CatProfileContracts.h:21-31 三态、CatCollectionModel.cpp:47,51 进投影。缺的只有「追踪」与 M 键。另补 CatUISettings.h:74-94 证明 CollectionWidgetClass／ShopWidgetClass 两个字段都已删（verify 脚本读两个死字段） |
| 57 | 🔄 Divergent | 🔄 Divergent（维持，Notes 补强） | 名称／品种介绍／重量三项在 CatInventoryModel.cpp:750-765 齐全，差的只是呈现形态。D-27 的 `AdvanceFishObservation`／`ClearFishObservation`／`FindObservedFish` 与 D-28 的 `CatPersistentHudWidget` 复核确认全 Source 零命中，D-27／D-28 需复核这一条成立 |
| 42 | ⚠️ Partial | ⚠️ Partial（维持，Notes 补强） | CatInventoryModel.cpp:152 注释「文案刻意叫『选择』而不是『装备槽』」——是代码主动回避装备栏形态，且无 D 条目 |
| 74 | ❌ Missing | ❌ Missing（维持，Notes 补强） | 数据源存在：CatRunContracts.h:119-131,182-184 `ECatEnvironmentTimeOfDay`／`RunPublicState.TimeOfDay`；UI 层零引用（搜 `TimeOfDay`／`清晨`／`黄昏`） |
| 10 | ⚠️ Partial | ⚠️ Partial（行号修正） | Config/DefaultGame.ini:38 段头、:40 `bRequiresPushToTalk`，原写 38-39 未落在值上 |
| 75 | —（漏列） | ⚠️ Partial（新增） | 主界面.md:119 图注「左下角标注鱼竿耐久780」原报告未评。CatInventoryModel.cpp:158-166 有耐久文案但在背包页；CatHUDWidget.h:29-194 无耐久字段。搜 `Durability`／`耐久` |
| 76 | —（漏列） | ❌ Missing（新增） | Sheet1.csv 第15行备注第三项「追踪鱼轻提示」原报告未评。搜 `追踪`／`Track`／`Tracked`／`Pinned` 在 UI/ 与 Profile/ 零命中 |
| 77 | —（漏列） | 🔄 Divergent（新增） | 提竿按键：Sheet1 第7行 F vs Sheet2 第3行左键。CatFishingPrimaryActionAbility.h:7「按当前阶段解释为抛竿、提竿或收线」＋ CatFishingAbilityTags.cpp:6 单一 Primary Tag，结构上不可能另设 F 键 |
| 2 | ⚠️ Partial | 维持 | CatFrontendRootWidget.cpp:344-350 只判 `EKeys::Escape`；全 Source Grep `EKeys::Enter`／`EKeys::Tab`／`EKeys::M`／`EKeys::N`／`EKeys::R`／`EKeys::T`／`EKeys::F` 仅 4 处命中且全是 Escape（另一处 Shift+Escape 在 CatfishingEditor.cpp:22）。cpp:342 注释自认「按钮的 Enter/Space 由 UMG 原路径处理」 |
| 3 / 6 / 9 / 11 | ⚠️/❌ | 维持 | 搜 `Ready`／`bIsReady`／`准备`／`RoomPassword`／`SetRoomName`／`PlayerInfo`／`查看玩家` 均无写口；CatOnlineSubsystem.cpp:216-217 证实 RoomName／MaxPlayers 只读推导 |
| 12 / 13 / 14 | ❌/⚠️ | 维持 | CatFrontendRootWidget.cpp:21-33 `ConfigureRow` 只写 DisplayName 与「最近保存：…」；无方向键、Enter、N 键处理 |
| 16 / 19 / 21 | ⚠️/❌ | 维持 | 逐条列过 CatFrontendSettingsModel.h 全部 Draft 字段（62-201、275-332）：只有 DraftUIScale 沾边；`TextSize`／`HighContrast`／`ColorBlind`／`Sensitivity`／`InvertY`／`FrameRate`／`AntiAlias`／`MotionBlur` 全零命中 |
| 26 / 28 | ⚠️/❌ | 维持 | CatHUDWidget.h:17-23 只有 OpenMainMenu／OpenInventory 两种意图；:292-294 DayTextBlock 是 UTextBlock，:328-330 MainMenuButton 是独立按钮 |
| 31 | 🔄 Divergent | 维持 | Scripts/create_inventory_input_assets.py:143-145,185-186 硬断言 Inventory→Tab、LakeMenu→Escape；Config/DefaultGame.ini:17 InventoryToggleAction。复核过 D 台账全 57 行，`Tab`／`背包` 只在 D-04 出现且与键位无关，确认无 D 条目 |
| 39 / 40 | ❌ | 维持 | 搜 `剪影`／`Silhouette`／`Reveal`／`FirstUnlock`／`NewSpecies`／`Discover`／`Notification`／`Toast`：Profile 侧只有 durable Grant 记录，无一次性揭示页，也无跨玩家 UI 事件 |
| 47 / 48 / 49 | 🔄/❌ | 维持 | CatInteractionTags.cpp:5 全项目只有一个 `Cat.Input.Interact`；CatFishPickupActor.h:75 注释「对死鱼按 E」；ReleaseMouthCarryFromAuthority 唯一调用点是 HandleAuthorityCarrierDestroyed（cpp:504-508）。搜 `DropCarried`／`PickUpGuard`／`CarryGuard`／`EatFish` 无玩家侧写口 |
| 50 / 51 / 52 | ❌ | 维持 | 搜 `Chum`／`窝` 在 UI/ 下只有 CatInventoryModel.cpp:110 的装备类别分支；ChumFieldPresentationActor.h:27-33 范围环是常驻程序化实例，无按键切换 |
| 53 / 54 / 55 | ❌/⚠️ | 维持 | CatInteractionTargetingComponent.cpp:45-91 纯准星射线无镜头修正；`SetRenderCustomDepth` 全 Source 只在 CatFishPickupActor.cpp:48,574-575 出现；CatInteractionPageController.cpp:92-101 提示模板「按 {0} {1}」＋CatUISettings.cpp:188-204 反查 IA_Interact 键名 |
| 58 / 61 | ❌ | 维持 | 搜 `Altar`／`Shrine`／`Statue`／`石像`／`圣猫`／`SitDown`／`坐下`／`Relax` 全零命中；实现 ICatInteractable 的头文件仅 5 类（Camp 仓库／鱼护／鱼缸＋鱼缸交互组件／地上鱼／商店摊位） |
| 59 / 60 | ❌ | 维持 | 补搜 `Quota` 全库：只在 CatRunAttributeSet／ExecCalc／CatRunContracts.h:234,238 出现，`Source/Catfishing/UI/` 下零命中，未进 HUD 投影 |
| 66 / 67 / 68 / 69 / 70 / 71 / 72 | ❌/⚠️ | 维持 | ECatInventoryAction 全 9 个值逐条读过（CatInventoryTypes.h:12-34）：无批量放入、无丢弃、无放生；CatLakeMainMenuWidget.h:300-325 局内菜单只有设置／保存／回主菜单／退出／关闭五按钮，无派对设置、暂停、图鉴 |
| 62 | 🔄 Divergent | 维持 | 钓鱼规则.md:25「装备即状态…不设模式键」为 SSOT，交互.md:66 是应作废草稿口径；CatFishingAbilityTags.cpp:5-10 无进入钓鱼模式的 Input Tag |
| 抽验 ✅（5 / 25 / 33 / 34 / 35 / 36 / 37 / 38 / 41 / 65） | ✅ | 全部维持 | 逐个 Read 过：CatfishingGameModeBase.h:22-25 `MaxCampSpawnPlayers = 4`；CatHUDModel.cpp:144,146 天数文案；:190-198 倒计时；:204-205 咬钩/提竿成功文案；:207-212 体力；:218-270 十种阶段文案；:271-287 鱼状态四态；CatShopInteractionComponent.cpp:16-17,53-63 商店软类创建；CatCampInventoryActor.h:60-79 仓库交互；Escape 三处（CatShopWidget.cpp:793／CatInventoryWidget.cpp:556／CatFrontendRootWidget.cpp:346）。行号与语义均对得上 |
| 抽验 ❓（43 / 44 / 45 / 46 / 77 一并核） | ❓ | 维持 ❓ | 复核了「是不是把能查的推给了查不了」：Config/DefaultInput.ini 全文 60+ 行确认只有引擎默认 AxisConfig，无本项目 Action 映射；Scripts 里除 create_inventory_input_assets.py 固定 Tab／Escape 外，只有 verify_stage_a_map.py:180-206 与 verify_ui_reach_runtime.py:144-149 走引擎读 IMC（③，本轮不跑）。抛竿／放置竿／收放线／抄鱼的**具体键**确实只在 IMC_InputContext.uasset 里，判 ❓ 成立 |
