#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Condition/CatConditionTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Framework/Core/CatRunContracts.h"
#include "Growth/CatGrowthTypes.h"
#include "Items/CatItemTypes.h"
#include "Social/CatSocialTypes.h"
#include "UI/CatFishingViewTypes.h"
#include "CatLakeReachWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

/** LocalPlayer 协调器投影出的完整 Lake 信息面；所有字段都是可丢弃的只读 DTO，不持有玩法对象或写入口。 */
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

	/** 本地 durable 图鉴的公开记录；Profile 提供副本，Journal、相册和装备选择不进入此数组。 */
	// 该记录类型刻意不参与 Blueprint 反射；原生根 View 直接消费副本，避免为展示需求改造 Profile 的权威持久化合同。
	TArray<FCatFishCollectionRecord> FishCollection;

	/** 本地 Profile 当前是否能提供 durable 图鉴快照；false 表示 gate 未就绪而不是空图鉴。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishCollectionAvailable = false;

	/** 当前 Lake 菜单是否展开；唯一写者是 UCatLocalPlayerUISubsystem，Widget 只按值显示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bMenuOpen = false;

	/** 打开菜单所用 Enhanced Input 键名；Widget 在 UIOnly 焦点下用同一键请求关闭，避免出现第二套键位。 */
	UPROPERTY(BlueprintReadOnly)
	FName MenuToggleKeyName = TEXT("Tab");

	/** 当前 Lake 菜单是否可以提交正式离局意图；Online 快照写入，Widget 只据此显示按钮并禁用迟到点击。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanRequestOnlineLeave = false;
};

/** 根 View 的关闭意图；Widget 不直接改 InputMode，LocalPlayer 协调器收到后统一恢复游戏输入。 */
DECLARE_MULTICAST_DELEGATE(FCatLakeReachCloseRequested);

/** 根 View 的离局意图；Widget 不接触 Session 或旅行 API，LocalPlayer 协调器会转交给 Online 子系统。 */
DECLARE_MULTICAST_DELEGATE(FCatLakeReachLeaveRequested);

/** Lake 唯一原生根 View；同一棵 WidgetTree 呈现 HUD、Fishing 反馈、菜单、个人鱼护与图鉴。 */
UCLASS()
class CATFISHING_API UCatLakeReachWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收协调器重建的完整只读投影并一次刷新所有信息面；Widget 不保存 Model 或发玩法命令。 */
	void Render(const FCatUIReachViewState& ViewState);

	/** 玩家点击关闭或在菜单焦点内再次按同一菜单键时发出的纯意图；协调器是唯一订阅者。 */
	FCatLakeReachCloseRequested OnCloseRequested;

	/** 玩家在 Lake 菜单中选择离开本局时发出的纯意图；协调器收到后仍走正式 Online Leave 管线。 */
	FCatLakeReachLeaveRequested OnLeaveRequested;

protected:
	/** 首次初始化时创建一棵原生根树并设置可聚焦；不依赖 WBP，也不绑定 Pawn、Session 或 Profile。 */
	virtual void NativeOnInitialized() override;

	/** 每次进入视口时对菜单按钮去重绑定，防止 Slate 重建造成一次点击广播多次。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除菜单按钮绑定并清纯意图广播，再交还父类 Slate 生命周期。 */
	virtual void NativeDestruct() override;

	/** 菜单处于 UIOnly 焦点时消费配置的同一菜单键并请求关闭；其余按键继续交给父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** 把关闭按钮点击转换为 OnCloseRequested；不直接访问 PlayerController 或修改鼠标状态。 */
	UFUNCTION()
	void HandleCloseClicked();

	/** 把离局按钮点击转换为 OnLeaveRequested；只有最近投影允许离局时才广播，避免销毁期旧点击发起请求。 */
	UFUNCTION()
	void HandleLeaveClicked();

	/** 身体、Condition、装备、Run 与求助的 HUD 文本；Render 是唯一写者。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SurvivalText;

	/** 当前 Fishing 阶段、鱼体力、收放线状态和阶段提示文本；只消费复制投影。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FishingText;

	/** 最近 Fishing Command 的结构化成功或错误反馈；没有结果时显示操作入口提示。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackText;

	/** 菜单内容的单一容器；Render 按 bMenuOpen 整体显示或折叠，不拆出独立页面 Widget。 */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MenuPanel;

	/** 个人鱼护实物鱼列表；只显示 Items 复制 DTO，不提供转移、消费或献祭按钮。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FishGuardText;

	/** 本地 durable 图鉴列表；只显示 Profile 公开快照，不暴露 Journal 或存档路径。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FishCollectionText;

	/** 菜单内显式关闭控件；点击只广播意图，由协调器成对恢复 InputMode 与鼠标。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> CloseButton;

	/** 菜单内正式离局控件；按钮只在 Online 快照允许 Leave 时显示，点击不直接操作 Session。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> LeaveButton;

	/** 最近一次 Render 指定的菜单键名；NativeOnKeyDown 读取它来关闭当前菜单，不自行读取配置。 */
	FName RenderedMenuToggleKeyName = TEXT("Tab");

	/** 最近一次 Render 指定的菜单可见状态；只有展开时 Widget 才消费菜单键。 */
	bool bRenderedMenuOpen = false;

	/** 最近一次 Render 指定的正式离局可用性；HandleLeaveClicked 读取它过滤迟到点击。 */
	bool bRenderedCanRequestOnlineLeave = false;
};
