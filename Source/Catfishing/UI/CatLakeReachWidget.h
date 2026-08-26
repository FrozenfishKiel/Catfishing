#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Condition/CatConditionTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Framework/Core/CatRunContracts.h"
#include "Growth/CatGrowthTypes.h"
#include "Items/CatItemTypes.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "Social/CatSocialTypes.h"
#include "UI/CatFishingViewTypes.h"
#include "CatLakeReachWidget.generated.h"

class UButton;

/** LakeReach 鱼护条目上的玩家动作意图；它只描述 UI 想做什么，真正权限和状态写入仍由服务器命令裁决。 */
UENUM(BlueprintType)
enum class ECatUIReachFishGuardAction : uint8
{
	/** 当前没有可提交的鱼护动作；View 和 Model 用它表示空选择或无待处理请求。 */
	None,
	/** 请求吃掉当前选中的实物鱼；服务器必须先从 Items 成功消费，再应用身体和成长效果。 */
	ConsumeSelectedFish,
	/** 请求把当前选中的鱼从个人鱼护转入固定共享鱼缸；服务器通过 Camp 和 Items 执行双 Revision 事务。 */
	TransferSelectedFishToTank,
	/** 请求把当前选中的鱼提交给献祭协议；服务器通过 SacrificeCoordinator 协调 Items 与 Run。 */
	SacrificeSelectedFish
};

/** LakeReach 商店区的玩家动作意图；它只引用已裁目录项，价格、库存和交付全部由服务器商店链路决定。 */
UENUM(BlueprintType)
enum class ECatUIReachShopAction : uint8
{
	/** 当前没有可提交的商店动作；View 用它过滤空按钮或旧蓝图调用。 */
	None,
	/** 请求购买二级商店鱼竿；团队钱包和库存由 ShopOrderCoordinator 裁决。 */
	PurchaseShopRodT2,
	/** 请求购买基础虫虫窝料；它是局内耗材订单，不由 UI 直接发放。 */
	PurchaseBugChum,
	/** 请求领取免费虫虫鱼饵；免费资格仍由 ShopEconomy 的免费入口配置判断。 */
	ClaimFreeBugBait,
	/** 请求领取保底一级鱼竿；只走免费领取 RPC，不伪造装备入库。 */
	ClaimFreeStarterRod
};

/** UIReach 给蓝图前端消费的图鉴展示条目；它是 Profile durable 记录的只读副本，不改变 Profile 的 SaveGame 合同。 */
USTRUCT(BlueprintType)
struct FCatUIReachFishCollectionEntry
{
	GENERATED_BODY()

	/** 该图鉴行对应的鱼定义稳定 ID；Profile 记录写入，View 只用于显示。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 该鱼在本地图鉴里的公开三态；UI 只展示 Unknown、Silhouette 或 Recorded，不据此补 Grant。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishCollectionState State = ECatFishCollectionState::Unknown;

	/** 本地 durable 记录中的最佳重量，单位千克；它只来自 Profile 副本，不从实物鱼护反推。 */
	UPROPERTY(BlueprintReadOnly)
	double BestWeightKilograms = 0.0;

	/** 合格交手累计次数；蓝图用它表现图鉴进度，不把它写回 Profile。 */
	UPROPERTY(BlueprintReadOnly)
	int32 EncounterCount = 0;
};

/** UIReach Model 投影出的完整 Lake 信息面；所有字段都是可丢弃的只读 DTO，不持有玩法对象或写入口。 */
USTRUCT(BlueprintType)
struct FCatUIReachViewState
{
	GENERATED_BODY()

	/** 当前 Poison 读数；倒地与恢复仍由 Condition 裁决，View 不根据数值推导状态。 */
	UPROPERTY(BlueprintReadOnly)
	float Poison = 0.0f;

	/** 当前钓鱼力量读数；只用于玩家理解身体能力，不成为 Fishing 命令参数。 */
	UPROPERTY(BlueprintReadOnly)
	float FishingStrength = 0.0f;

	/** 当前搏斗体力读数；只展示 Character ASC 的短周期资源，不复制鱼侧体力真相。 */
	UPROPERTY(BlueprintReadOnly)
	float FightStamina = 0.0f;

	/** Character 离散身体完整快照；Wet、Downed 与恢复模式由 Condition 组件发布。 */
	UPROPERTY(BlueprintReadOnly)
	FCatConditionSnapshot Condition;

	/** Character 吃鱼成长完整快照；经验槽和待选次数只展示，不在 View 中弹选择或授 Buff。 */
	UPROPERTY(BlueprintReadOnly)
	FCatGrowthSnapshot Growth;

	/** Character 一局装备完整快照；它不包含 Profile 解锁、鱼护实物或可写库存引用。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEquipmentLoadoutSnapshot Equipment;

	/** 当前 World 的公开 Run 与环境快照；GameState 是来源，View 不推进阶段。 */
	UPROPERTY(BlueprintReadOnly)
	FCatRunPublicState Run;

	/** 当前 World 最近一次公开求助；UI 只表现类型和范围，不缓存可写社交权限。 */
	UPROPERTY(BlueprintReadOnly)
	FCatHelpSignalSnapshot HelpSignal;

	/** 当前团队钱包与公开流水快照；GameState 复制写入，View 只展示余额和最近交易数量。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopPublicEconomySnapshot ShopEconomy;

	/** 当前 GameState 是否能提供商店快照；false 时商店按钮应保持不可点而不是猜测钱包版本。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShopEconomyAvailable = false;

	/** 当前玩家 FishingSession 的复制投影；只有 bHasFishingSession 为 true 时才代表有效会话。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingViewState Fishing;

	/** 当前是否存在属于本玩家的非终态 FishingSession；协调器根据复制 Session 判定。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishingSession = false;

	/** 最近一条服务器 Fishing Command 终态；只用于操作反馈，不承担幂等缓存职责。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingCommandResult LastFishingCommandResult;

	/** 最近是否收到过可展示的 Fishing Command 终态；false 时 View 显示等待玩家操作的提示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishingCommandResult = false;

	/** 当前 Character 个人鱼护的完整复制快照；Items 仍是唯一写者，菜单只列出实物鱼。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainerSnapshot PersonalFishGuard;

	/** 当前鱼护列表中被 UI 高亮的下标；INDEX_NONE 表示没有可选实物鱼，Model 是唯一写者。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SelectedFishGuardIndex = INDEX_NONE;

	/** 当前被高亮的实物鱼副本；没有选择时保持默认，View 不用它直接提交领域写口。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance SelectedFishGuardFish;

	/** 当前是否存在一条可展示和可提交的鱼护选择；蓝图用它决定选中鱼信息是否可见。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedFishGuardFish = false;

	/** 当前选择是否还能向前移动；由 Model 按鱼护数组长度和下标计算，Widget 只读取。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSelectPreviousFishGuardEntry = false;

	/** 当前选择是否还能向后移动；由 Model 按鱼护数组长度和下标计算，Widget 只读取。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSelectNextFishGuardEntry = false;

	/** 当前选中鱼是否可以提交吃鱼、转缸或献祭意图；Model 会同时考虑菜单展开、选择存在和是否已有请求待回包。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSubmitSelectedFishGuardAction = false;

	/** 当前是否已经发出鱼护动作并等待服务器结果；View 用它禁用重复点击，不自行重试。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishGuardActionPending = false;

	/** 正在等待服务器结果的鱼护动作类型；没有待处理请求时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatUIReachFishGuardAction PendingFishGuardAction = ECatUIReachFishGuardAction::None;

	/** 正在等待服务器结果的请求 ID；PageController 创建，Model 用它匹配迟到回包。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid PendingFishGuardRequestId;

	/** 最近一次收到终态的鱼护动作类型；蓝图用它把反馈文案归到吃鱼、转缸或献祭。 */
	UPROPERTY(BlueprintReadOnly)
	ECatUIReachFishGuardAction LastFishGuardAction = ECatUIReachFishGuardAction::None;

	/** 最近一次鱼护动作的公共结果头；吃鱼和转缸直接来自领域服务，献祭由协议结果折算 Items 侧 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult LastFishGuardCommandResult;

	/** 最近是否存在可展示的鱼护动作结果；false 时蓝图不应显示旧反馈。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishGuardCommandResult = false;

	/** 最近一次献祭协议详细结果；只有 LastFishGuardAction 为 SacrificeSelectedFish 时才代表同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatSacrificeResult LastFishGuardSacrificeResult;

	/** 最近一次直接吃鱼详细结果；只有 LastFishGuardAction 为 ConsumeSelectedFish 时才代表同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishConsumeResult LastFishGuardConsumeResult;

	/** 本地 durable 图鉴的公开记录；Profile 提供副本，Journal、相册和装备选择不进入此数组。 */
	// 该记录类型刻意不参与 Blueprint 反射；原生根 View 直接消费副本，避免为展示需求改造 Profile 的权威持久化合同。
	TArray<FCatFishCollectionRecord> FishCollection;

	/** 蓝图前端可读的图鉴展示副本；Render 从 native FishCollection 转换，蓝图不能通过它改 Profile。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatUIReachFishCollectionEntry> FishCollectionEntries;

	/** 本地 Profile 当前是否能提供 durable 图鉴快照；false 表示 gate 未就绪而不是空图鉴。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishCollectionAvailable = false;

	/** 当前 Lake 菜单是否展开；唯一写者是 PageController，Widget 只按值显示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bMenuOpen = false;

	/** 打开菜单所用 Enhanced Input 键名；它从正式 IMC 反查而来，Widget 在 UIOnly 焦点下用同一键请求关闭。 */
	UPROPERTY(BlueprintReadOnly)
	FName MenuToggleKeyName = NAME_None;

	/** 当前 Lake 菜单是否可以提交正式离局意图；Online 快照写入，Widget 只据此显示按钮并禁用迟到点击。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanRequestOnlineLeave = false;
};

/** 蓝图前端实际消费的 UIReach 快照；只包含 Blueprint 支持的展示字段，并与 native ViewState 同步发布。 */
USTRUCT(BlueprintType)
struct FCatUIReachBlueprintViewState
{
	GENERATED_BODY()

	/** 当前 Poison 读数；蓝图只用于展示，不根据数值裁决倒地或恢复。 */
	UPROPERTY(BlueprintReadOnly)
	float Poison = 0.0f;

	/** 当前钓鱼力量读数；蓝图只表现身体能力，不把它提交给 Fishing 命令。 */
	UPROPERTY(BlueprintReadOnly)
	float FishingStrength = 0.0f;

	/** 当前搏斗体力读数；蓝图只展示本机 Character 资源，不复制鱼侧体力真相。 */
	UPROPERTY(BlueprintReadOnly)
	float FightStamina = 0.0f;

	/** Character 离散身体快照；Wet、Downed 与恢复模式仍由 Condition 组件发布。 */
	UPROPERTY(BlueprintReadOnly)
	FCatConditionSnapshot Condition;

	/** Character 成长快照；蓝图只展示经验槽和待选次数，不授予 Buff。 */
	UPROPERTY(BlueprintReadOnly)
	FCatGrowthSnapshot Growth;

	/** 一局装备快照；蓝图只读取当前装备表现，不持有 Profile 解锁或可写库存。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEquipmentLoadoutSnapshot Equipment;

	/** 当前 World 的公开 Run 与环境快照；蓝图不得推进阶段或天气。 */
	UPROPERTY(BlueprintReadOnly)
	FCatRunPublicState Run;

	/** 当前 World 最近一次公开求助；蓝图只表现类型和范围。 */
	UPROPERTY(BlueprintReadOnly)
	FCatHelpSignalSnapshot HelpSignal;

	/** 当前团队钱包与公开流水快照；蓝图只展示，不据此直接改余额或库存。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopPublicEconomySnapshot ShopEconomy;

	/** 当前是否拿到可用商店快照；蓝图用它决定商店按钮是否可点。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShopEconomyAvailable = false;

	/** 当前玩家 FishingSession 的复制投影；只有 bHasFishingSession 为 true 时才有效。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingViewState Fishing;

	/** 当前是否存在属于本玩家的非终态 FishingSession；Model 根据复制 Session 判定。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishingSession = false;

	/** 最近一条服务器 Fishing Command 终态；只用于操作反馈。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingCommandResult LastFishingCommandResult;

	/** 最近是否收到过可展示的 Fishing Command 终态；false 时显示等待玩家操作的提示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishingCommandResult = false;

	/** 当前 Character 个人鱼护的完整复制快照；蓝图只列出实物鱼。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainerSnapshot PersonalFishGuard;

	/** 当前鱼护列表中被 UI 高亮的下标；没有实物鱼时为 INDEX_NONE。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SelectedFishGuardIndex = INDEX_NONE;

	/** 当前被高亮的实物鱼副本；蓝图只展示，不把它当成可写库存引用。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance SelectedFishGuardFish;

	/** 当前是否存在一条可展示的鱼护选择；蓝图用它切换选中鱼详情显隐。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedFishGuardFish = false;

	/** 当前选择是否能前移；蓝图用它禁用上一条按钮。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSelectPreviousFishGuardEntry = false;

	/** 当前选择是否能后移；蓝图用它禁用下一条按钮。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSelectNextFishGuardEntry = false;

	/** 当前选中鱼是否可提交服务器动作；蓝图按钮只读这个 gate。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSubmitSelectedFishGuardAction = false;

	/** 当前鱼护动作是否正在等待服务器终态；蓝图用它展示 pending 状态并避免重复触发。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishGuardActionPending = false;

	/** 当前 pending 的动作类型；没有 pending 时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatUIReachFishGuardAction PendingFishGuardAction = ECatUIReachFishGuardAction::None;

	/** 当前 pending 的请求 ID；蓝图只用于调试和结果关联展示。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid PendingFishGuardRequestId;

	/** 最近收到终态的鱼护动作类型；蓝图据此显示对应反馈。 */
	UPROPERTY(BlueprintReadOnly)
	ECatUIReachFishGuardAction LastFishGuardAction = ECatUIReachFishGuardAction::None;

	/** 最近一次鱼护动作的公共结果；蓝图不需要知道具体服务也能展示成功或错误。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult LastFishGuardCommandResult;

	/** 最近是否存在鱼护动作结果；false 表示没有可展示反馈。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasFishGuardCommandResult = false;

	/** 最近一次献祭动作的详细协议结果；只在对应动作反馈里读取。 */
	UPROPERTY(BlueprintReadOnly)
	FCatSacrificeResult LastFishGuardSacrificeResult;

	/** 最近一次吃鱼动作的详细结果；只在对应动作反馈里读取。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishConsumeResult LastFishGuardConsumeResult;

	/** 蓝图可读的 durable 图鉴展示副本；它不包含 Journal、相册、解锁或存档路径。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatUIReachFishCollectionEntry> FishCollectionEntries;

	/** 当前本地 Profile 是否能提供 durable 图鉴；false 代表未就绪而非空图鉴。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishCollectionAvailable = false;

	/** 当前 Lake 菜单是否展开；唯一写者是 PageController。 */
	UPROPERTY(BlueprintReadOnly)
	bool bMenuOpen = false;

	/** 打开或关闭菜单所用键名；蓝图可用它显示从正式 IMC 解析出的提示文案。 */
	UPROPERTY(BlueprintReadOnly)
	FName MenuToggleKeyName = NAME_None;

	/** 当前 Lake 菜单是否可以提交正式离局意图；蓝图只据此显示或禁用按钮。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanRequestOnlineLeave = false;
};

/** 根 View 的关闭意图；Widget 不直接改 InputMode，PageController 收到后统一恢复游戏输入。 */
DECLARE_MULTICAST_DELEGATE(FCatLakeReachCloseRequested);

/** 根 View 的离局意图；Widget 不接触 Session 或旅行 API，PageController 会转交给 Online 子系统。 */
DECLARE_MULTICAST_DELEGATE(FCatLakeReachLeaveRequested);

/** 根 View 的鱼护选择意图；参数是相对偏移，PageController/Model 决定最终下标并重读快照。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatLakeReachFishGuardSelectionRequested, int32);

/** 根 View 的鱼护动作意图；Widget 不携带鱼 ID，PageController 从 Model 当前选择重建正式命令。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatLakeReachFishGuardActionRequested, ECatUIReachFishGuardAction);

/** 根 View 的商店动作意图；Widget 不携带价格、库存或钱包版本，PageController 从 Model 当前快照重建服务器命令。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatLakeReachShopActionRequested, ECatUIReachShopAction);

/** LakeReach 的正式 UMG View 基类；WBP 负责布局和表现，C++ 只缓存只读 DTO、触发蓝图渲染事件并广播用户意图。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatLakeReachWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收 Model 重建的完整只读投影并一次刷新 View；C++ 会转换蓝图安全 DTO，正式表现交给 WBP。 */
	void Render(const FCatUIReachViewState& ViewState);

	/** 蓝图或绑定按钮请求关闭 Lake 菜单的唯一入口；View 只广播意图，不恢复 InputMode。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestCloseMenu();

	/** 蓝图或绑定按钮请求离开当前 Lake Run 的唯一入口；View 只广播意图，Online 管线仍由 PageController 调用。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestLeaveLake();

	/** 蓝图或绑定按钮请求把鱼护选择前移一条；View 只广播偏移，Model 负责下标裁剪。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestSelectPreviousFishGuardEntry();

	/** 蓝图或绑定按钮请求把鱼护选择后移一条；View 只广播偏移，Model 负责下标裁剪。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestSelectNextFishGuardEntry();

	/** 蓝图或格子按钮请求直接选中某个鱼护格；View 只把目标格折算成偏移，Model 仍按最新快照裁剪。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestSelectFishGuardSlot(int32 SlotIndex);

	/** 蓝图或绑定按钮请求吃掉当前选中鱼；View 不传鱼 ID，避免蓝图持有可伪造命令载荷。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestConsumeSelectedFish();

	/** 蓝图或绑定按钮请求把当前选中鱼放入共享鱼缸；实际 Camp 和 Revision 由 PageController 重读。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestTransferSelectedFishToTank();

	/** 蓝图或绑定按钮请求献祭当前选中鱼；实际 Items/Run 协议命令由 PageController 重建。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestSacrificeSelectedFish();

	/** 蓝图或绑定按钮请求购买二级鱼竿；View 只发商店意图，不提交钱包版本或目录价格。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestPurchaseShopRodT2();

	/** 蓝图或绑定按钮请求购买虫虫窝料；实际耗材发放仍由服务器商店订单链路确认。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestPurchaseBugChum();

	/** 蓝图或绑定按钮请求免费领取虫虫鱼饵；免费入口是否合法由服务器目录判断。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestClaimFreeBugBait();

	/** 蓝图或绑定按钮请求免费领取保底鱼竿；View 不直接写 Equipment。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|UIReach")
	void RequestClaimFreeStarterRod();

	/** 返回最近一次蓝图安全 ViewState 副本；蓝图动画或延迟刷新可读取，但不能写回 Model。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|UIReach")
	FCatUIReachBlueprintViewState GetLastBlueprintViewState() const;

	/** 返回最近一次 native ViewState；C++ 测试用它确认 native-only 图鉴副本仍随 Render 保留。 */
	const FCatUIReachViewState& GetLastNativeViewState() const;

	/** 玩家点击关闭或在菜单焦点内再次按同一菜单键时发出的纯意图；PageController 是唯一订阅者。 */
	FCatLakeReachCloseRequested OnCloseRequested;

	/** 玩家在 Lake 菜单中选择离开本局时发出的纯意图；PageController 收到后仍走正式 Online Leave 管线。 */
	FCatLakeReachLeaveRequested OnLeaveRequested;

	/** 玩家在鱼护面板切换选中鱼时发出的纯意图；PageController 只转交给 Model，不写玩法状态。 */
	FCatLakeReachFishGuardSelectionRequested OnFishGuardSelectionRequested;

	/** 玩家在鱼护面板点击吃鱼、转缸或献祭时发出的纯意图；PageController 负责翻译成正式服务器命令。 */
	FCatLakeReachFishGuardActionRequested OnFishGuardActionRequested;

	/** 玩家在商店面板点击购买或免费领取时发出的纯意图；PageController 负责翻译成正式 Shop RPC。 */
	FCatLakeReachShopActionRequested OnShopActionRequested;

protected:
	/** 首次初始化时只设置可聚焦；正式布局必须来自 WBP，C++ 不在这里构造玩家可见 WidgetTree。 */
	virtual void NativeOnInitialized() override;

	/** 每次进入视口时对 WBP 可选绑定按钮去重绑定，防止 Slate 重建造成一次点击广播多次。 */
	virtual void NativeConstruct() override;

	/** 离开视口时只解除本 View 的按钮绑定；外部意图订阅由 PageController::Unbind 统一移除。 */
	virtual void NativeDestruct() override;

	/** 菜单处于 UIOnly 焦点时消费配置的同一菜单键并请求关闭；其余按键继续交给父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 的可选渲染扩展点；正式表现优先由 WBP Designer 属性绑定读取下方 Blueprint* 投影，蓝图图表可用本事件补动画或复杂布局。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|UIReach")
	void BP_RenderViewState(const FCatUIReachBlueprintViewState& ViewState);

private:
	/** 把关闭按钮点击转换为 OnCloseRequested；不直接访问 PlayerController 或修改鼠标状态。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 把离局按钮点击转换为 OnLeaveRequested；只有最近投影允许离局时才广播，避免销毁期旧点击发起请求。 */
	UFUNCTION()
	void HandleLeaveClicked();

	/** 把上一条按钮点击转换为鱼护选择偏移；不读取或改写鱼护数组。 */
	UFUNCTION()
	void HandlePreviousFishGuardClicked();

	/** 把下一条按钮点击转换为鱼护选择偏移；不读取或改写鱼护数组。 */
	UFUNCTION()
	void HandleNextFishGuardClicked();

	/** 把第 1 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot0Clicked();

	/** 把第 2 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot1Clicked();

	/** 把第 3 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot2Clicked();

	/** 把第 4 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot3Clicked();

	/** 把第 5 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot4Clicked();

	/** 把第 6 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot5Clicked();

	/** 把第 7 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot6Clicked();

	/** 把第 8 个鱼护格子点击转换为目标下标选择；它只走统一选择入口，不直接改鱼护。 */
	UFUNCTION()
	void HandleFishGuardSlot7Clicked();

	/** 把吃鱼按钮点击转换为当前选中鱼动作意图；具体鱼实例由 PageController 从 Model 读取。 */
	UFUNCTION()
	void HandleConsumeFishClicked();

	/** 把转缸按钮点击转换为当前选中鱼动作意图；具体营地和鱼缸版本由 PageController 解析。 */
	UFUNCTION()
	void HandleTransferFishToTankClicked();

	/** 把献祭按钮点击转换为当前选中鱼动作意图；具体协议载荷由 PageController 解析。 */
	UFUNCTION()
	void HandleSacrificeFishClicked();

	/** 把二级鱼竿购买按钮点击转换为商店意图；View 不读取钱包或库存。 */
	UFUNCTION()
	void HandlePurchaseShopRodT2Clicked();

	/** 把虫虫窝料购买按钮点击转换为商店意图；具体订单交给 PageController。 */
	UFUNCTION()
	void HandlePurchaseBugChumClicked();

	/** 把免费虫虫鱼饵按钮点击转换为商店意图；服务器决定是否可领取。 */
	UFUNCTION()
	void HandleClaimFreeBugBaitClicked();

	/** 把保底鱼竿领取按钮点击转换为商店意图；服务器决定是否可领取。 */
	UFUNCTION()
	void HandleClaimFreeStarterRodClicked();

	/** 统一过滤鱼护动作按钮的迟到点击；只有菜单展开、选择可提交且动作明确时才广播。 */
	void RequestFishGuardAction(ECatUIReachFishGuardAction Action);

	/** 统一过滤商店按钮的迟到点击；只有菜单展开且商店快照可用时才广播纯动作枚举。 */
	void RequestShopAction(ECatUIReachShopAction Action);

	/** 把 native ViewState 转成蓝图可读副本；FishCollection 在这里复制成 UI 专用条目而不暴露 Profile 存档结构。 */
	static FCatUIReachBlueprintViewState MakeBlueprintViewState(const FCatUIReachViewState& ViewState);

	/** 刷新 WBP 8 个固定鱼护格的文本和可点击状态；它只写蓝图投影字段，不创建或销毁玩家可见控件。 */
	void UpdateBlueprintFishGuardSlotProjection();

	/** 构造单个鱼护格子的中文展示文本；空格子、选中格和实物鱼信息都来自最近一次只读 ViewState。 */
	FText MakeBlueprintFishGuardSlotText(int32 SlotIndex) const;

	/** WBP 内可选关闭控件；点击只广播意图，由 PageController 成对恢复 InputMode 与鼠标。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	/** WBP 内可选正式离局控件；按钮只在 Online 快照允许 Leave 时启用，点击不直接操作 Session。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> LeaveButton;

	/** WBP 内可选上一条鱼控件；点击只请求调整选择，不直接移动或删除鱼。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> PreviousFishGuardButton;

	/** WBP 内可选下一条鱼控件；点击只请求调整选择，不直接移动或删除鱼。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> NextFishGuardButton;

	/** WBP 内第 1 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot0Button;

	/** WBP 内第 2 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot1Button;

	/** WBP 内第 3 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot2Button;

	/** WBP 内第 4 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot3Button;

	/** WBP 内第 5 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot4Button;

	/** WBP 内第 6 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot5Button;

	/** WBP 内第 7 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot6Button;

	/** WBP 内第 8 个鱼护格按钮；点击只请求选择该格，空格会被最近 ViewState 禁用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> FishGuardSlot7Button;

	/** WBP 内可选吃鱼控件；点击只广播动作意图，服务器结果回包后才更新展示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConsumeFishButton;

	/** WBP 内可选转入共享鱼缸控件；点击只广播动作意图，不持有 Camp 或 Items 写口。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> TransferFishToTankButton;

	/** WBP 内可选献祭控件；点击只广播动作意图，不直接预留或消费鱼实例。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SacrificeFishButton;

	/** WBP 内可选二级鱼竿购买控件；点击只广播商店意图，不携带价格或库存。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> PurchaseShopRodT2Button;

	/** WBP 内可选虫虫窝料购买控件；点击只广播商店意图，耗材入库由服务器确认。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> PurchaseBugChumButton;

	/** WBP 内可选免费虫虫鱼饵领取控件；点击只广播商店意图，免费白名单由后端目录确认。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ClaimFreeBugBaitButton;

	/** WBP 内可选保底一级鱼竿领取控件；点击只广播商店意图，不直接写 Equipment。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> ClaimFreeStarterRodButton;

	/** 最近一次 native Render 输入；测试和 C++ 调试可读，蓝图不直接消费 native-only FishCollection。 */
	FCatUIReachViewState LastNativeViewState;

	/** 最近一次蓝图安全 Render 输入；WBP 的表现和延迟动画都读取这一份。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FCatUIReachBlueprintViewState LastBlueprintViewState;

	/** WBP Designer 绑定用的 Poison 数值；Render 从 DTO 写入，TextBlock 自己在蓝图资产里决定如何展示。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	float BlueprintPoisonValue = 0.0f;

	/** WBP Designer 绑定用的中文 Poison 文本；它把裸数值说明成猫状态读数，避免玩家只看到一个 0。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintPoisonText;

	/** WBP Designer 绑定用的 FishingStrength 数值；它只来自当前 ViewState，不成为命令参数。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	float BlueprintFishingStrengthValue = 0.0f;

	/** WBP Designer 绑定用的中文 FishingStrength 文本；它解释该数值是钓鱼力量而不是鱼护数量。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishingStrengthText;

	/** WBP Designer 绑定用的搏斗体力数值；它只表示 Character ASC 当前资源。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	float BlueprintFightStaminaValue = 0.0f;

	/** WBP Designer 绑定用的中文搏斗体力文本；它让 HUD 展示当前剩余资源含义。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFightStaminaText;

	/** WBP Designer 绑定用的个人鱼护条目数；Render 从鱼护快照派生，蓝图只展示数量。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	int32 BlueprintFishGuardCount = 0;

	/** WBP Designer 绑定用的中文鱼护数量文本；空鱼护时必须告诉玩家先去钓鱼，而不是显示孤立 0。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardCountText;

	/** 第 1 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot0Text;

	/** 第 2 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot1Text;

	/** 第 3 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot2Text;

	/** 第 4 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot3Text;

	/** 第 5 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot4Text;

	/** 第 6 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot5Text;

	/** 第 7 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot6Text;

	/** 第 8 个鱼护格子的蓝图展示投影；Render 从鱼护快照写入格内文本，WBP 只读取它表现空格、选中或实物鱼。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardSlot7Text;

	/** 第 1 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot0Enabled = false;

	/** 第 2 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot1Enabled = false;

	/** 第 3 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot2Enabled = false;

	/** 第 4 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot3Enabled = false;

	/** 第 5 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot4Enabled = false;

	/** 第 6 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot5Enabled = false;

	/** 第 7 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot6Enabled = false;

	/** 第 8 个鱼护格子的蓝图交互 gate；Render 写入，只有菜单打开且该格有实物鱼时 WBP 才允许点击。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardSlot7Enabled = false;

	/** WBP Designer 绑定用的选中鱼下标；没有可选鱼时为 INDEX_NONE。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	int32 BlueprintSelectedFishGuardIndex = INDEX_NONE;

	/** WBP Designer 绑定用的选中鱼定义 ID；Render 从 DTO 复制，蓝图只展示。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FName BlueprintSelectedFishDefinitionId = NAME_None;

	/** WBP Designer 绑定用的选中鱼展示文本；它由定义 ID 和重量拼成，避免 TextBlock 直接绑定不支持的 FName。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintSelectedFishText;

	/** WBP Designer 绑定用的选中鱼重量；单位千克，来自当前鱼护快照。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	double BlueprintSelectedFishWeightKilograms = 0.0;

	/** WBP Designer 绑定用的选中鱼存在标记；它决定鱼护动作区是否显示真实鱼信息。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintHasSelectedFishGuardFish = false;

	/** WBP Designer 绑定用的上一条按钮可用性；Render 从 Model 计算结果复制。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintCanSelectPreviousFishGuardEntry = false;

	/** WBP Designer 绑定用的下一条按钮可用性；Render 从 Model 计算结果复制。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintCanSelectNextFishGuardEntry = false;

	/** WBP Designer 绑定用的鱼护动作按钮可用性；pending 或无选择时为 false。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardActionEnabled = false;

	/** WBP Designer 绑定用的鱼护动作区显隐；没有可选鱼时折叠，避免显示空按钮。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	ESlateVisibility BlueprintFishGuardActionVisibility = ESlateVisibility::Collapsed;

	/** WBP Designer 绑定用的 pending 标记；蓝图可用它显示等待反馈但不发起重试。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintFishGuardActionPending = false;

	/** WBP Designer 绑定用的最近鱼护结果 Revision；成功或拒绝都来自服务器结果。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	int64 BlueprintFishGuardResultRevision = 0;

	/** WBP Designer 绑定用的最近鱼护结果错误枚举；蓝图可把它转成人类可读文案。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	ECatDomainCommandError BlueprintFishGuardResultError = ECatDomainCommandError::InvalidPayload;

	/** WBP Designer 绑定用的鱼护结果展示文本；它由服务器错误枚举转成文本，避免 TextBlock 直接绑定不支持的枚举。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishGuardResultText;

	/** WBP Designer 绑定用的 durable 图鉴条目数；Render 从蓝图安全图鉴副本派生，不暴露 Profile 存档结构。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	int32 BlueprintFishCollectionCount = 0;

	/** WBP Designer 绑定用的中文图鉴文本；Profile 未就绪和空图鉴会显示不同原因。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintFishCollectionText;

	/** WBP Designer 绑定用的中文商店摘要；它展示公款余额和流水数量，不暴露服务器私有身份。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	FText BlueprintShopSummaryText;

	/** WBP Designer 绑定用的商店按钮可用性；缺 GameState 商店快照时所有商店意图 fail-closed。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintShopActionEnabled = false;

	/** WBP Designer 绑定用的菜单显隐值；PageController 写入菜单状态，蓝图资产决定菜单容器如何表现。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	ESlateVisibility BlueprintMenuVisibility = ESlateVisibility::Collapsed;

	/** WBP Designer 绑定用的离局按钮显隐值；Online gate 只影响按钮表现和迟到点击过滤。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	ESlateVisibility BlueprintLeaveVisibility = ESlateVisibility::Collapsed;

	/** WBP Designer 绑定用的离局按钮可用性；点击后仍由 RequestLeaveLake 复核最近 gate。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|UIReach", meta = (AllowPrivateAccess = "true"))
	bool bBlueprintLeaveEnabled = false;

	/** 最近一次 Render 指定的菜单键名；NativeOnKeyDown 读取它来关闭当前菜单，不自行读取配置。 */
	FName RenderedMenuToggleKeyName = NAME_None;

	/** 最近一次 Render 指定的菜单可见状态；只有展开时 Widget 才消费菜单键。 */
	bool bRenderedMenuOpen = false;

	/** 最近一次 Render 指定的正式离局可用性；HandleLeaveClicked 读取它过滤迟到点击。 */
	bool bRenderedCanRequestOnlineLeave = false;

	/** 最近一次 Render 指定的上一条选择可用性；Widget 用它过滤旧按钮点击。 */
	bool bRenderedCanSelectPreviousFishGuardEntry = false;

	/** 最近一次 Render 指定的下一条选择可用性；Widget 用它过滤旧按钮点击。 */
	bool bRenderedCanSelectNextFishGuardEntry = false;

	/** 最近一次 Render 指定的鱼护动作提交 gate；Widget 用它过滤旧的吃鱼、转缸或献祭点击。 */
	bool bRenderedCanSubmitSelectedFishGuardAction = false;

	/** 最近一次 Render 指定的商店动作提交 gate；Widget 用它过滤缺钱包快照或销毁期的旧点击。 */
	bool bRenderedCanSubmitShopAction = false;

	/** 最近一次 Render 指定的鱼护实物数量；格子点击读取它来拒绝空格和超过本轮固定显示范围的旧点击。 */
	int32 RenderedFishGuardCount = 0;

	/** 最近一次 Render 指定的鱼护选中下标；格子点击把目标格折算为相对偏移时读取它。 */
	int32 RenderedSelectedFishGuardIndex = INDEX_NONE;
};
