#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Growth/CatGrowthTypes.h"
#include "Items/CatItemTypes.h"
#include "CatInventoryTypes.generated.h"

/** 个人鱼护背包里的玩家动作意图；它只描述 UI 想做什么，权限、Revision 和结果仍由服务器裁决。 */
UENUM(BlueprintType)
enum class ECatInventoryAction : uint8
{
	/** 当前没有可提交动作；View 和 Model 用它表示空选择或清空 pending。 */
	None,
	/** 请求吃掉当前选中的个人鱼护实物鱼；身体和成长效果只能在服务器 Items 提交成功后发生。 */
	ConsumeSelectedFish,
	/** 请求把当前选中的鱼转入固定共享鱼缸；UI 不持有鱼缸写口，只提交意图给 PlayerController。 */
	TransferSelectedFishToTank,
	/** 请求把当前选中的鱼交给献祭协议；Items 与 Run 的不可逆点继续由 SacrificeCoordinator 处理。 */
	SacrificeSelectedFish
};

/** 背包格子鼠标交互的 UI 层含义；格子不是 Button，因此点击、右键和拖拽都从原生鼠标重写函数发出。 */
UENUM(BlueprintType)
enum class ECatInventorySlotPointerAction : uint8
{
	/** 左键选择该格；主界面收到后让 Model 基于最新快照裁剪选择。 */
	Select,
	/** 右键打开或刷新该格上下文；默认只改变选择，不直接执行吃鱼或删除。 */
	Context,
	/** 拖拽开始；当前版本只暴露交互意图，正式转移仍走服务器命令。 */
	DragStarted
};

/** 单个背包格子的只读显示投影；它来自个人鱼护复制快照，不携带任何容器写入口。 */
USTRUCT(BlueprintType)
struct FCatInventorySlotView
{
	GENERATED_BODY()

	/** 该格在当前鱼护快照中的显示下标；主界面用它把 Slot Widget 事件映射回 Model 选择。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotIndex = INDEX_NONE;

	/** 该格是否有后端实物鱼；空格只展示占位，不允许构造鱼命令载荷。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOccupied = false;

	/** 该格是否是当前 Model 高亮选择；蓝图只用它表现边框或颜色。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelected = false;

	/** 该格展示的鱼实例副本；只有 bOccupied 为 true 时才代表一条真实实物鱼。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance Fish;

	/** 给蓝图 TextBlock 直接读取的中文摘要；C++ 只生成文本，不写具体 TextBlock。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;
};

/** 背包主界面的完整只读显示投影；字段只服务个人鱼护 UI，不混入商店、HUD 或图鉴。 */
USTRUCT(BlueprintType)
struct FCatInventoryViewState
{
	GENERATED_BODY()

	/** 当前 Character 个人鱼护复制快照；Items 仍是唯一写者，UI 只读鱼数组、容量和 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainerSnapshot PersonalFishGuard;

	/** WrapBox 应创建的格子数量；它从后端容量得来，容量缺失时只回退到当前鱼数量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotCount = 0;

	/** WrapBox 每个条目对应的格子显示投影；主界面刷新时按数组重建 Slot Widget。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventorySlotView> Slots;

	/** 当前选中鱼护格子下标；没有实物鱼可选时保持 INDEX_NONE。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SelectedSlotIndex = INDEX_NONE;

	/** 当前被选中的实物鱼副本；没有选择时保持默认值，蓝图不得把它写回容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance SelectedFish;

	/** 当前是否存在一条可展示和可提交的选中鱼；蓝图用它控制详情区显隐。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedFish = false;

	/** 背包窗口当前是否打开；Model 只投影 PageController 的状态，不反查 Widget 可见性。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOpen = false;

	/** 当前是否可提交吃鱼、转缸或献祭意图；pending 或空选择时为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSubmitAction = false;

	/** 当前是否已经发出鱼护动作但还没收到服务器终态；View 用它禁用重复提交。 */
	UPROPERTY(BlueprintReadOnly)
	bool bActionPending = false;

	/** 当前等待服务器回包的动作类型；没有 pending 时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventoryAction PendingAction = ECatInventoryAction::None;

	/** 当前等待服务器回包的 RequestId；PageController 生成，Model 用它匹配迟到结果。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid PendingRequestId;

	/** 最近一次完成或本地拒绝的背包动作；蓝图用它把反馈归类到吃鱼、转缸或献祭。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventoryAction LastAction = ECatInventoryAction::None;

	/** 最近一次背包动作的公共结果头；成功和拒绝都来自服务器或 PageController 适配层。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult LastCommandResult;

	/** 最近是否存在可展示的背包动作结果；false 时结果区不应展示旧文本。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasCommandResult = false;

	/** 最近一次吃鱼动作的详细结果；只有 LastAction 为 ConsumeSelectedFish 时对应同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishConsumeResult LastConsumeResult;

	/** 最近一次献祭动作的详细结果；只有 LastAction 为 SacrificeSelectedFish 时对应同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatSacrificeResult LastSacrificeResult;

	/** 打开或关闭背包所用的键名；它从既有 InputContext 反查而来，只用于提示和 UIOnly 下关闭。 */
	UPROPERTY(BlueprintReadOnly)
	FName ToggleKeyName = NAME_None;

	/** 给蓝图直接展示的鱼护数量和容量摘要；空鱼护会明确告诉玩家当前没有鱼。 */
	UPROPERTY(BlueprintReadOnly)
	FText SummaryText;

	/** 给蓝图直接展示的选中鱼摘要；没有选择时说明需要先点格子。 */
	UPROPERTY(BlueprintReadOnly)
	FText SelectedFishText;

	/** 给蓝图直接展示的最近动作反馈；pending、成功和错误都只读这条文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText ResultText;
};
