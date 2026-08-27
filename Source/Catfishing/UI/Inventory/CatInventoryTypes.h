#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Growth/CatGrowthTypes.h"
#include "Items/CatItemTypes.h"
#include "CatInventoryTypes.generated.h"

/** 库存相关的玩家动作意图；它只描述 UI 想对选中物体或鱼领域动作做什么，权限、Revision 和结果仍由服务器裁决。 */
UENUM(BlueprintType)
enum class ECatInventoryAction : uint8
{
	/** 当前没有可提交动作；View 和 Model 用它表示空选择或清空 pending。 */
	None,
	/** 请求吃掉当前选中的个人鱼护实物鱼；身体和成长效果只能在服务器 Items 提交成功后发生。 */
	ConsumeSelectedFish,
	/** 请求把拖拽源物体移动到另一个格子；UI 只提交物体身份、源/目标容器槽位和两个 Revision。 */
	MoveObjectBetweenContainers,
	/** 请求整理随身库存里的两个格子；服务器只接收格子下标和 Equipment Revision，再重读库存数组。 */
	MoveInventoryItem,
	/** 请求把当前选中的随身库存物品设为钓鱼选择；服务器仍会重读整套鱼竿、鱼饵和鱼漂持有量。 */
	SelectInventoryFishingItem,
	/** 请求把当前选中的鱼交给献祭协议；Items 与 Run 的不可逆点继续由 SacrificeCoordinator 处理。 */
	SacrificeSelectedFish
};

/** 库存格子鼠标交互的 UI 层含义；格子不是 Button，因此点击、右键和拖拽都从原生鼠标重写函数发出。 */
UENUM(BlueprintType)
enum class ECatInventorySlotPointerAction : uint8
{
	/** 左键选择该格；主界面收到后让 Model 基于最新快照裁剪选择。 */
	Select,
	/** 右键打开或刷新该格上下文；默认只改变选择，不直接执行吃鱼或删除。 */
	Context,
	/** 拖拽开始；它只驱动 UMG 视觉链路，真正移动必须等待目标格 Drop 后走服务器事务。 */
	DragStarted
};

/** 库存格子投影的后端事实来源；UI 用它区分随身库存物品和可拖拽鱼容器格，避免把两类写口混用。 */
UENUM(BlueprintType)
enum class ECatInventorySlotSource : uint8
{
	/** 还没有可靠来源；这类格子只能展示占位，不能提交任何后端命令。 */
	Unknown,
	/** 当前角色随身库存数组中的一个格子；它不作为 Items 容器移动源，但可整理格子或把钓具设为当前选择。 */
	InventoryObject,
	/** Items 容器中的槽位；只有这种来源可以作为鱼或容器物体拖拽的源和目标。 */
	ContainerObject
};

/** 单个库存格子的只读显示投影；它可能来自随身库存物品或 Items 鱼容器，但不携带任何后端写入口。 */
USTRUCT(BlueprintType)
struct FCatInventorySlotView
{
	GENERATED_BODY()

	/** 该格在当前库存 WrapBox 中的全局显示下标；主界面用它把 Slot Widget 事件映射回 Model 选择。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotIndex = INDEX_NONE;

	/** 该格来自哪类后端读模型；PageController 用它确认只有容器槽能提交容器移动。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventorySlotSource SlotSource = ECatInventorySlotSource::Unknown;

	/** 该格所属后端容器的公开种类；PageController 用它识别源目标容器类别，服务器仍会重建真实容器。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainerKind ContainerKind = ECatContainerKind::Unknown;

	/** 该格所属后端容器的一局公开 ID；它只用于识别当前 ViewState 中的来源和目标，不授权客户端写容器。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ContainerId;

	/** 该格所属后端容器在生成投影时的 Revision；拖拽提交时会作为服务器乐观并发前提。 */
	UPROPERTY(BlueprintReadOnly)
	int64 ContainerRevision = 0;

	/** 该格在所属容器内部的槽位下标；它与 Items 权威数组下标一致，是拖拽源/目标复核的一部分。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ContainerSlotIndex = INDEX_NONE;

	/** 该格在随身库存数组中的槽位下标；只有 InventoryObject 有效，拖拽整理和右键选择都会让服务器按它复核。 */
	UPROPERTY(BlueprintReadOnly)
	int32 InventorySlotIndex = INDEX_NONE;

	/** 该随身库存格生成投影时的 Equipment Revision；拖拽提交用它防止源格在拖拽过程中刷新成别的物品。 */
	UPROPERTY(BlueprintReadOnly)
	int64 InventoryRevision = 0;

	/** 该格是否有后端可展示内容；容器空格和空库存占位只展示占位，不允许构造移动或鱼领域命令载荷。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOccupied = false;

	/** 该格是否允许启动拖拽；随身库存物品只可拖到随身库存格，容器物体只可拖到容器格。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanDrag = false;

	/** 该格是否是当前 Model 高亮选择；蓝图只用它表现边框或颜色。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelected = false;

	/** 该格展示的通用物体；容器对象可参与容器移动，随身库存物品只承载当前格里的定义和数量。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainedObjectInstance Object;

	/** 该格物体的领域类别；容器槽用它转发移动，随身库存物品用它区分装备型和数量型条目。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainedObjectKind ObjectKind = ECatContainedObjectKind::Unknown;

	/** 容器物体的局内稳定实例 ID；拖拽时会和容器槽位一起复核，随身库存物品不使用它授权。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ObjectInstanceId;

	/** 该格展示的鱼实例副本；只有容器槽的 ObjectKind 为 Fish 时有效，吃鱼和献祭继续读取这份鱼领域载荷。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance Fish;

	/** 该格对应的装备类别；随身库存条目用它区分鱼竿、鱼饵、鱼漂和抄网，并让 PageController 路由钓具选择命令。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEquipmentKind EquipmentKind = ECatEquipmentKind::Unknown;

	/** 该格对应的装备定义 ID；随身库存条目从 Equipment 快照读取它来证明玩家已获得哪种物品。 */
	UPROPERTY(BlueprintReadOnly)
	FName EquipmentDefinitionId = NAME_None;

	/** 给蓝图 TextBlock 直接读取的中文摘要；C++ 只生成文本，不写具体 TextBlock。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;
};

/** 库存一次打开时纳入显示的一份容器投影；它把具体容器统一成可拖拽列表，不把库存写死到鱼缸或任一 Actor。 */
USTRUCT(BlueprintType)
struct FCatInventoryContainerView
{
	GENERATED_BODY()

	/** 容器的公开复制快照；UI 只读其中 ID、Kind、Revision、容量和对象投影，不能通过它写回 Items。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainerSnapshot Snapshot;

	/** 该容器在本次库存里的玩家可读名称；默认来自容器种类，具体交互对象以后可以提供更细名字。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayName;

	/** 该容器在 Slots 数组中的第一个显示下标；蓝图可用它分组或画标题，不参与服务器权限。 */
	UPROPERTY(BlueprintReadOnly)
	int32 FirstSlotIndex = INDEX_NONE;

	/** 该容器在 Slots 数组中占用的显示格数量；容量为 0 时可能只保留一个占位格。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotCount = 0;

	/** 该容器是否是当前玩家自己的个人鱼护；吃鱼和献祭只对它开放。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPrimaryPersonalContainer = false;
};

/** 库存主界面的完整只读显示投影；它聚合随身库存、独立鱼护和外部鱼容器，但不提供任何写口。 */
USTRUCT(BlueprintType)
struct FCatInventoryViewState
{
	GENERATED_BODY()

	/** 当前玩家独立鱼护的复制快照；Items 仍是唯一写者，UI 只读鱼数组、容量和 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainerSnapshot PersonalFishGuard;

	/** 当前库存展示的所有容器；第一个通常是本人鱼护，后续来自本次交互对象提供的外部容器上下文。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventoryContainerView> Containers;

	/** 当前库存是否带有个人鱼护以外的容器；蓝图可用它决定是否显示跨容器拖拽分组。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasExternalContainers = false;

	/** 当前 Character 的随身库存和钓鱼选择快照；EquipmentComponent 写入，库存只读展示格子数组和当前钓鱼选择。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEquipmentLoadoutSnapshot Equipment;

	/** 当前是否已经绑定到本角色 EquipmentComponent；false 表示随身库存事实还不能可靠展示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bEquipmentAvailable = false;

	/** WrapBox 应创建的格子总数；随身库存物品、个人鱼护和外部容器公开容量会合并到同一个显示列表。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotCount = 0;

	/** WrapBox 每个条目对应的格子显示投影；主界面刷新时按数组重建 Slot Widget。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventorySlotView> Slots;

	/** 当前选中显示格子下标；没有可选格或下标已失效时保持 INDEX_NONE。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SelectedSlotIndex = INDEX_NONE;

	/** 当前被选中的通用容器物体；没有选择时保持默认值，蓝图不得把它写回容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainedObjectInstance SelectedObject;

	/** 当前是否存在一个可展示和可拖拽的选中容器物体；随身库存物品不进入这条容器选择事实。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedObject = false;

	/** 当前选中物体是否来自个人主容器；鱼领域按钮和未来个人容器动作都从这里派生 gate。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelectedObjectInPersonalContainer = false;

	/** 当前被选中的实物鱼副本；没有鱼选择时保持默认值，蓝图不得把它写回容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance SelectedFish;

	/** 当前是否存在一条可展示和可提交的选中鱼；蓝图用它控制详情区显隐。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedFish = false;

	/** 当前选中鱼是否来自个人鱼护；吃鱼和献祭按钮只允许使用本人鱼护选择。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelectedFishInPersonalGuard = false;

	/** 库存窗口当前是否打开；Model 只投影 PageController 的状态，不反查 Widget 可见性。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOpen = false;

	/** 当前是否可提交吃鱼或献祭意图；pending、空选择或外部容器选择时为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanSubmitAction = false;

	/** 当前是否已经发出库存动作但还没收到服务器终态；View 用它禁用重复提交。 */
	UPROPERTY(BlueprintReadOnly)
	bool bActionPending = false;

	/** 当前等待服务器回包的动作类型；没有 pending 时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventoryAction PendingAction = ECatInventoryAction::None;

	/** 当前等待服务器回包的 RequestId；PageController 生成，Model 用它匹配迟到结果。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid PendingRequestId;

	/** 最近一次完成或本地拒绝的库存动作；蓝图用它把反馈归类到吃鱼、移动或献祭。 */
	UPROPERTY(BlueprintReadOnly)
	ECatInventoryAction LastAction = ECatInventoryAction::None;

	/** 最近一次库存动作的公共结果头；成功和拒绝都来自服务器或 PageController 适配层。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult LastCommandResult;

	/** 最近是否存在可展示的库存动作结果；false 时结果区不应展示旧文本。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasCommandResult = false;

	/** 最近一次吃鱼动作的详细结果；只有 LastAction 为 ConsumeSelectedFish 时对应同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishConsumeResult LastConsumeResult;

	/** 最近一次献祭动作的详细结果；只有 LastAction 为 SacrificeSelectedFish 时对应同一请求。 */
	UPROPERTY(BlueprintReadOnly)
	FCatSacrificeResult LastSacrificeResult;

	/** 打开或关闭库存所用的键名；它从既有 InputContext 反查而来，只用于提示和模态焦点下关闭。 */
	UPROPERTY(BlueprintReadOnly)
	FName ToggleKeyName = NAME_None;

	/** 给蓝图直接展示的库存总览；它区分随身库存物品和鱼护容器，不再把鱼竿说成独立装备槽。 */
	UPROPERTY(BlueprintReadOnly)
	FText SummaryText;

	/** 给蓝图直接展示的当前钓鱼选择摘要；没有同步到 Equipment 时会明确显示等待同步。 */
	UPROPERTY(BlueprintReadOnly)
	FText EquipmentText;

	/** 给蓝图直接展示的随身库存概要；具体格子内容必须从 Slots 里的 InventoryObject 读取。 */
	UPROPERTY(BlueprintReadOnly)
	FText InventoryItemsText;

	/** 给蓝图直接展示的当前选择摘要；鱼会显示重量，其他物体显示类别和定义，字段名保留给既有 WBP 兼容。 */
	UPROPERTY(BlueprintReadOnly)
	FText SelectedFishText;

	/** 给蓝图直接展示的最近动作反馈；pending、成功和错误都只读这条文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText ResultText;
};
