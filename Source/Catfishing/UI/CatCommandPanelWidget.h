#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatCommandPanelWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * 开发期白盒面板能表达的玩法意图；每个值对应 ACatfishingPlayerController 上的一条 Server RPC（映射表在
 * UCatLocalPlayerUISubsystem::DispatchCommandPanelAction）。
 * 它是 View 侧的意图枚举：Widget 只广播这些值，不携带 RequestId、Revision 或任何玩法对象；这些参数由协调器在点击瞬间现取。
 * 新增值时必须同时补分派表和按钮标签表，自动化测试按 StaticEnum 逐值检查两张表是否漏项。
 */
UENUM()
enum class ECatCommandPanelAction : uint8
{
	/** 普通夜晚提交“我准备好翻天”。 */
	RunReady,
	/** 撤回翻天确认。 */
	RunUnready,
	/** 结算夜请求结算完成（服务器仍要求归档与 Grant ACK 已收口）。 */
	SettlementComplete,
	/** 在当前位置开始一次钓鱼会话；水域/相位/装备由服务器裁决。 */
	StartFishing,
	/** 对世界里第一条非终态钓鱼会话发起抢抄。 */
	Scoop,
	/** 在营地请求休息。 */
	CampRest,
	/** 把个人鱼护第一条鱼转入营地共享鱼缸。 */
	TransferFirstFishToTank,
	/** 请求篝火回看。 */
	CampfirePlayback,
	/** 把世界里第一个倒地的角色（可能是自己）送回营地救援点。 */
	RescueDownedToCamp,
	/** 献祭个人鱼护第一条鱼。 */
	SacrificeFirstFish,
	/** 用团队公款买下商店目录里第一条付费装备（当前内容是 2 级竿）。 */
	ShopBuyFirstPaidEntry,
	/** 免费领取普通饵。 */
	ShopClaimFreeBait,
	/** 把个人鱼护第一条鱼卖给商人。 */
	SellFirstFish,
	/** 手动求助；倒地时发倒地求助，否则发钓鱼求助。 */
	ManualHelp,
	/** 完成抖水表现后请求清除 Wet。 */
	ShakeDry,
	/** 倒地时单人请求野外自救（不需要营地或伙伴）。 */
	FieldSelfRecovery,
	/** 按项目配置的 starter 三件套请求装配。 */
	ConfigureStarterEquipment,
	/** 用团队公款买下商店目录里第一条窝料耗材（当前内容是虫虫窝料），落到本人耗材栈。 */
	ShopBuyFirstChum,
	/** 把团队装备库里第一件实物取走并装到本人对应槽位（买到的 2 级竿从这里装上）。 */
	TakeFirstTeamEquipment,
	/** 用本人耗材栈里第一种窝料在猫当前位置投窝。 */
	ContributeChum,
	/** 遛鱼：上报"拖"（等同按住左键）；真咬期第一次点它就是提竿。面板是点击式，意图会一直保持到点别的遛鱼按钮。 */
	FightPull,
	/** 遛鱼：上报"松"（等同按住右键放线）。 */
	FightRelease,
	/** 遛鱼：上报"都没按"，把前两者清掉。 */
	FightNeutral
};

/** View 向协调层上报的纯用户意图；Widget 不读取玩法宿主、不发 RPC，也不解释服务器结果。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCommandPanelActionRequested, ECatCommandPanelAction);

/**
 * 白盒面板一次渲染需要的只读事实；协调器从已订阅的 Run/Condition/鱼护快照和世界里的宿主存在性拼出它，Widget 只用它决定按钮可用性和反馈文本。
 * 这里的可用性只是客户端“猜测”，服务器仍按自己的门禁裁决；所以它宁可放得宽一点，让人能把被拒绝的请求也发出去看日志。
 */
struct FCatCommandPanelViewState
{
	/** 当前 Run 阶段；决定 ready/结算/商店这类按钮开不开。 */
	ECatRunPhase Phase = ECatRunPhase::NotStarted;

	/** 当前 Run 终局原因；StartupFailed 表示命令门根本没开过，面板会把所有玩法按钮关掉。 */
	ECatRunEndReason EndReason = ECatRunEndReason::None;

	/** Run 公开快照版本；只用于在反馈区显示，让人肉眼判断请求前后快照有没有变。 */
	int64 RunRevision = 0;

	/** 服务器当前是否允许钓鱼；关着时不开“开始钓鱼”。 */
	bool bFishingAllowed = false;

	/** 当日献祭额度是否开放；关着时不开献祭。 */
	bool bQuotaOpen = false;

	/** 本人是否倒地；倒地时不开钓鱼/献祭等主动玩法按钮，但保留求助。 */
	bool bDowned = false;

	/** 本人是否淋湿；只有湿着时“抖干”才有意义。 */
	bool bWet = false;

	/** 个人鱼护里当前鱼的条数；为 0 时所有“第一条鱼”类按钮关闭。 */
	int32 GuardFishCount = 0;

	/** 当前 World 里能找到营地宿主；找不到时营地四个按钮全部关闭。 */
	bool bHasCamp = false;

	/** 当前 World 里存在非终态钓鱼会话；有才开抢抄，没有才开“开始钓鱼”。 */
	bool bHasActiveFishingSession = false;

	/** 当前 World 里存在倒地角色可被送回营地；没有就关掉救援。 */
	bool bHasRescueTarget = false;

	/** 团队装备库当前有实物可取；没有就关掉取用。 */
	bool bHasTeamEquipment = false;

	/** 本人耗材栈里有至少一份窝料；没有就关掉投窝。 */
	bool bHasChum = false;

	/**
	 * 当前活跃钓鱼会话的一行只读摘要（阶段、D/L、鱼状态、鱼体力、意图、终局）；没有会话时为空。协调器从会话快照拼好，
	 * Widget 原样显示，让人不看日志也能知道鱼在发力还是累了。
	 */
	FString FishingSessionLine;

	/** 最近一次点击的分派结果文本；由协调器写入，Widget 原样显示。 */
	FString LastFeedback;
};

/**
 * 开发期白盒玩法命令面板；一排原生按钮把 ECatCommandPanelAction 广播出去，再加一块反馈文本区。
 * 它存在的目的只是让人在 PIE 里把每条玩法链路手动走一遍、把服务器日志里的拒绝/接受看清楚；它不是飞书正式 UI 设计（天
 * 数、图鉴入口、背包、交互提示都不在这里），正式 UI 到位后整块删除即可。
 * 完全沿用 UCatTravelWidget 的模式：WidgetTree 手搭、点击只广播意图、Construct/Destruct 成对绑定解绑。
 */
UCLASS()
class CATFISHING_API UCatCommandPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 用一份完整 ViewState 刷新所有按钮可用性与状态/反馈文本；Widget 不缓存上一份状态，每次整体重算。 */
	void Configure(const FCatCommandPanelViewState& ViewState);

	/** 按钮可用性推导的唯一实现：给定 ViewState 和意图，返回该按钮是否应当可点；纯函数，自动化测试直接调用它核对各相位下的结果。 */
	static bool IsActionAvailable(const FCatCommandPanelViewState& ViewState, ECatCommandPanelAction Action);

	/** 返回某个意图的按钮标签；每个枚举值都必须有标签，漏项返回 nullptr，自动化测试据此抓漏。 */
	static const TCHAR* GetActionLabel(ECatCommandPanelAction Action);

	/** 用户意图广播；LocalPlayer UI 子系统订阅后翻译成 PlayerController 的 Server RPC。 */
	FCatCommandPanelActionRequested OnActionRequested;

protected:
	/** 首次初始化时用 WidgetTree 搭出状态文本、每个意图一个按钮、以及反馈文本；整块锚在视口右上角，不和左上角的状态 View 重叠。 */
	virtual void NativeOnInitialized() override;

	/** 每次进入视口对全部按钮做 Remove/Add 去重绑定，保证 Slate 重建后一次点击只广播一次。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除全部按钮绑定，再交还父类生命周期。 */
	virtual void NativeDestruct() override;

private:
	// UButton::OnClicked 不带参数，无法区分是谁点的，因此每个意图各绑一个只做广播的入口；下面 23 个 Handle*Clicked 只
	// 把对应枚举值广播出去，不读任何状态。
	/** 广播“翻天 ready”意图。 */
	UFUNCTION()
	void HandleRunReadyClicked();

	/** 广播“撤回 ready”意图。 */
	UFUNCTION()
	void HandleRunUnreadyClicked();

	/** 广播“结算完成”意图。 */
	UFUNCTION()
	void HandleSettlementCompleteClicked();

	/** 广播“开始钓鱼”意图。 */
	UFUNCTION()
	void HandleStartFishingClicked();

	/** 广播“抢抄”意图。 */
	UFUNCTION()
	void HandleScoopClicked();

	/** 广播“营地休息”意图。 */
	UFUNCTION()
	void HandleCampRestClicked();

	/** 广播“第一条鱼入缸”意图。 */
	UFUNCTION()
	void HandleTransferFirstFishToTankClicked();

	/** 广播“篝火回看”意图。 */
	UFUNCTION()
	void HandleCampfirePlaybackClicked();

	/** 广播“救援倒地者”意图。 */
	UFUNCTION()
	void HandleRescueDownedToCampClicked();

	/** 广播“献祭第一条鱼”意图。 */
	UFUNCTION()
	void HandleSacrificeFirstFishClicked();

	/** 广播“买首条付费目录项”意图。 */
	UFUNCTION()
	void HandleShopBuyFirstPaidEntryClicked();

	/** 广播“免费领饵”意图。 */
	UFUNCTION()
	void HandleShopClaimFreeBaitClicked();

	/** 广播“卖第一条鱼”意图。 */
	UFUNCTION()
	void HandleSellFirstFishClicked();

	/** 广播“手动求助”意图。 */
	UFUNCTION()
	void HandleManualHelpClicked();

	/** 广播“抖干”意图。 */
	UFUNCTION()
	void HandleShakeDryClicked();

	/** 广播“野外自救”意图。 */
	UFUNCTION()
	void HandleFieldSelfRecoveryClicked();

	/** 广播“装配 starter 装备”意图。 */
	UFUNCTION()
	void HandleConfigureStarterEquipmentClicked();

	/** 广播“买首条窝料”意图。 */
	UFUNCTION()
	void HandleShopBuyFirstChumClicked();

	/** 广播“取用装备库首件”意图。 */
	UFUNCTION()
	void HandleTakeFirstTeamEquipmentClicked();

	/** 广播“投窝”意图。 */
	UFUNCTION()
	void HandleContributeChumClicked();

	/** 广播“遛鱼：拖”意图。 */
	UFUNCTION()
	void HandleFightPullClicked();

	/** 广播“遛鱼：松”意图。 */
	UFUNCTION()
	void HandleFightReleaseClicked();

	/** 广播“遛鱼：都不按”意图。 */
	UFUNCTION()
	void HandleFightNeutralClicked();

	/** 顶部状态文本：显示 Run 相位、EndReason、Revision 与鱼护条数，让人不看日志也能知道当前客户端快照。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	/** 底部反馈文本：显示最近一次点击分派了哪条 RPC、带了什么参数，或者为什么没发出去。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackText;

	/** 与 ECatCommandPanelAction 逐值对应的按钮数组；下标即枚举值，Configure 按同一下标设可用性。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> Buttons;
};
