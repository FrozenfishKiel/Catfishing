#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Growth/CatGrowthTypes.h"
#include "Items/CatItemTypes.h"
#include "CatInventoryTypes.generated.h"

class UTexture2D;

/** 库存相关的玩家动作意图；它只描述 UI 想对选中物体或鱼领域动作做什么，权限、并发前提和结果仍由服务器裁决。 */
UENUM(BlueprintType)
enum class ECatInventoryAction : uint8
{
	/** 当前没有可提交动作；View 和 Model 用它表示空选择或清空 pending。 */
	None,
	/** 请求吃掉当前选中的地面鱼护实物鱼；身体和成长效果只能在服务器 Items 提交成功后发生。 */
	ConsumeSelectedFish,
	/** 请求把拖拽源物体移动到另一个格子；UI 只提交物体身份、源/目标容器槽位和容器并发前提。 */
	MoveObjectBetweenContainers,
	/** 请求整理运行期库存格；同源只改本数据源，背包和营地之间的拖放会由服务器同时改双方数据源。 */
	MoveInventoryItem,
	/** 请求把当前选中的随身库存物品设为钓鱼选择；服务器仍会重读整套鱼竿、鱼饵和鱼漂持有量。 */
	SelectInventoryFishingItem,
	/** 请求把当前选中的鱼交给献祭协议；Items 与 Run 的不可逆点继续由 SacrificeCoordinator 处理。 */
	SacrificeSelectedFish,
	/** 请求把当前选中的营地公共仓库格取到本人随身库存；服务器仍按公共仓库版本和个人库存版本共同复核。 */
	WithdrawCampInventoryItem
};

/** 库存格子投影的后端事实来源；UI 用它区分运行期库存格和 Items 容器格，避免把不同宿主的写口混用。 */
UENUM(BlueprintType)
enum class ECatInventorySlotSource : uint8
{
	/** 还没有可靠来源；这类格子只能展示占位，不能提交任何后端命令。 */
	Unknown,
	/** 当前角色随身库存数组中的一个格子；它不作为 Items 容器移动源，但可整理、转入营地仓库或把钓具设为当前选择。 */
	InventoryObject,
	/** Items 容器中的槽位；只有这种来源可以作为鱼或容器物体拖拽的源和目标。 */
	ContainerObject,
	/** 营地公共仓库中的一个装备或耗材格；它是另一种运行期库存宿主，可在本仓库内整理或通过取用请求进入本人随身库存。 */
	CampInventoryObject
};

/** 单个库存格子的只读显示投影；它可能来自运行期库存宿主或 Items 容器，但不携带任何后端写入口。 */
USTRUCT(BlueprintType)
struct FCatInventorySlotView
{
	GENERATED_BODY()

	/** 该格在所属库存 UI 自己 Slots 数组里的局部下标；不同库存可以同时拥有第 0 格，操作复核必须看 SlotSource 和宿主内槽位。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotIndex = INDEX_NONE;

	/** 该格来自哪类后端读模型；PageController 用它把运行期库存整理和 Items 容器移动分流到不同服务器复核。 */
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

	/** 该格在随身库存数组中的槽位下标；只有 InventoryObject 有效，拖拽整理、背包/营地转移和右键选择都会让服务器按它复核。 */
	UPROPERTY(BlueprintReadOnly)
	int32 InventorySlotIndex = INDEX_NONE;

	/** 该格所属营地公共仓库在生成投影时的 Revision；取用时作为服务器乐观并发前提。 */
	UPROPERTY(BlueprintReadOnly)
	int64 CampInventoryRevision = 0;

	/** 该格在营地公共仓库数组中的槽位下标；只有 CampInventoryObject 有效，服务器按它重读源格。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CampInventorySlotIndex = INDEX_NONE;

	/** 该格是否有后端可展示内容；容器空格和空库存占位只展示占位，不允许构造移动或鱼领域命令载荷。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOccupied = false;

	/** 该格是否允许启动拖拽；运行期库存格可整理或在背包和营地之间转移，Items 容器物体只在容器槽之间移动。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanDrag = false;

	/** 该格是否是当前库存 WBP 的本地高亮选择；Model 原始投影不写它，具体页面渲染时再标记自己的选择。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelected = false;

	/** 该格展示的通用物体；容器对象可参与容器移动，运行期库存物品只承载当前格里的定义和数量。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainedObjectInstance Object;

	/** 该格物体的领域类别；容器槽用它转发移动，运行期库存物品用它区分装备型和数量型条目。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainedObjectKind ObjectKind = ECatContainedObjectKind::Unknown;

	/** 容器物体的局内稳定实例 ID；拖拽时会和容器槽位一起复核，运行期库存物品不使用它授权。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ObjectInstanceId;

	/** 该格展示的鱼实例副本；只有容器槽的 ObjectKind 为 Fish 时有效，吃鱼和献祭继续读取这份鱼领域载荷。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance Fish;

	/** 该格对应的装备类别；运行期库存条目用它展示鱼竿、鱼饵、鱼漂和抄网类别，只有随身库存格右键会用它路由钓具选择命令。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEquipmentKind EquipmentKind = ECatEquipmentKind::Unknown;

	/** 该格对应的装备定义 ID；随身库存和营地公共仓库都从各自 FCatRunInventorySlot 投影它，服务器仍按对应宿主重读权威数组。 */
	UPROPERTY(BlueprintReadOnly)
	FName EquipmentDefinitionId = NAME_None;

	/** 该格对 UI 暴露的稳定定义 ID；鱼、装备和数量物品都用它定位当前显示对象。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 该格当前数量；不可堆叠物品和鱼通常为 1，空格为 0。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Quantity = 0;

	/** 该格物品的单格最大堆叠数；UI 只用它决定是否显示数量角标，不参与服务器堆叠判断。 */
	UPROPERTY(BlueprintReadOnly)
	int32 MaxStackSize = 1;

	/** 该格是否按定义表现为可堆叠物品；鱼和装备型单件通常为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bStackable = false;

	/** 当前是否应该显示数量角标；Widget 可直接绑定它控制角标显隐。 */
	UPROPERTY(BlueprintReadOnly)
	bool bShowQuantity = false;

	/** 给数量角标直接绑定的文本；空格或不显示数量时为空文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText QuantityText;

	/** 给蓝图直接显示的对象名称；由 Model 从定义资产解析，Widget 不再自己查定义表。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayName;

	/** 给蓝图直接显示的对象说明；由定义资产提供，缺失时保持空文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText Description;

	/** 给蓝图直接显示的缩略图；鱼走鱼定义，运行期库存物品走装备或物品定义。 */
	UPROPERTY(BlueprintReadOnly)
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** 给蓝图 TextBlock 直接读取的中文摘要；C++ 只生成文本，不写具体 TextBlock。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;
};

/** 库存一次打开时纳入显示的一份容器投影；它把具体容器整理成可拖拽列表，不把库存写死到鱼缸或任一 Actor。 */
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

	/** 该容器在 ExternalContainerSlots 数组中的第一个局部下标；蓝图可用它分组或画标题，不参与服务器权限。 */
	UPROPERTY(BlueprintReadOnly)
	int32 FirstSlotIndex = INDEX_NONE;

	/** 该容器在 ExternalContainerSlots 数组中占用的格子数量；容量为 0 时可能只保留一个占位格。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SlotCount = 0;

	/** 该容器是否来自本次世界 Actor 交互；普通背包打开时没有鱼容器。 */
	UPROPERTY(BlueprintReadOnly)
	bool bInteractionFishContainer = false;
};

/** 库存主界面的完整只读显示投影；它聚合随身背包、本次交互世界容器和营地公共仓库，但不提供任何写口。 */
USTRUCT(BlueprintType)
struct FCatInventoryViewState
{
	GENERATED_BODY()

	/** 当前库存展示的世界容器；只来自本次射线命中的交互 Actor，不存在角色随身鱼护。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventoryContainerView> Containers;

	/** 当前库存是否带有本次交互 Actor 提供的世界容器；普通背包打开时为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasExternalContainers = false;

	/** 当前库存是否带有本次交互打开的营地公共仓库；普通背包和鱼容器打开时为 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasCampInventory = false;

	/** 营地公共仓库自己的格子数量；空仓库也会按配置容量保留空格。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CampInventorySlotCount = 0;

	/** 当前营地公共仓库快照版本；取用请求用它证明玩家看到的是哪一版公共仓库。 */
	UPROPERTY(BlueprintReadOnly)
	int64 CampInventoryRevision = 0;

	/** 当前 Character 的随身库存和钓鱼选择快照；EquipmentComponent 写入，库存只读展示格子数组和当前钓鱼选择。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEquipmentLoadoutSnapshot Equipment;

	/** 当前是否已经绑定到本角色 EquipmentComponent；false 表示随身库存事实还不能可靠展示。 */
	UPROPERTY(BlueprintReadOnly)
	bool bEquipmentAvailable = false;

	/** 随身背包自己的格子数量；只来自 EquipmentComponent 的 InventorySlots 和配置容量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 InventorySlotCount = 0;

	/** 随身背包自己的格子投影；普通背包 WBP 构建和刷新时只读取这一份，不混入营地仓库或外部容器。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventorySlotView> InventorySlots;

	/** 当前交互外部容器自己的格子投影；鱼护或其他世界容器 WBP 构建和刷新时读取这一份。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventorySlotView> ExternalContainerSlots;

	/** 营地公共仓库自己的格子投影；营地仓库 WBP 构建和刷新时只读取这一份，不混入玩家随身背包。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatInventorySlotView> CampInventorySlots;

	/** 当前库存 WBP 本地选中的格子身份；Model 原始投影保持为空，页面渲染时写入自己的只读副本。 */
	UPROPERTY(BlueprintReadOnly)
	FCatInventorySlotView SelectedSlot;

	/** 当前页面是否有一份可复核的本地选中格子身份；不同库存 WBP 之间不会共享这个选择。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedSlot = false;

	/** 当前被选中的通用容器物体；没有选择时保持默认值，蓝图不得把它写回容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatContainedObjectInstance SelectedObject;

	/** 当前是否存在一个可展示和可拖拽的选中容器物体；运行期库存物品不进入这条容器选择事实。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedObject = false;

	/** 当前选中物体是否来自本次交互打开的地面鱼护。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelectedObjectInFishGuard = false;

	/** 当前被选中的实物鱼副本；没有鱼选择时保持默认值，蓝图不得把它写回容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance SelectedFish;

	/** 当前是否存在一条可展示和可提交的选中鱼；蓝图用它控制详情区显隐。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasSelectedFish = false;

	/** 当前选中鱼是否来自本次射线打开的地面鱼护；吃鱼和献祭从该明确容器提交。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelectedFishInFishGuard = false;

	/** 库存窗口当前是否打开；Model 只投影 PageController 的状态，不反查 Widget 可见性。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOpen = false;

	/** 当前是否可提交吃鱼或献祭意图；只有打开地面鱼护并选中其中一条鱼时为 true。 */
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

	/** 最近是否存在可展示的库存动作结果；false 时结果区不应展示上一文本。 */
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

	/** 给蓝图直接展示的库存总览；它区分运行期库存物品、鱼护容器和营地仓库，不再把鱼竿说成独立装备槽。 */
	UPROPERTY(BlueprintReadOnly)
	FText SummaryText;

	/** 给蓝图直接展示的当前钓鱼选择摘要；没有同步到 Equipment 时会明确显示等待同步。 */
	UPROPERTY(BlueprintReadOnly)
	FText EquipmentText;

	/** 给蓝图直接展示的随身库存概要；具体格子内容必须从 InventorySlots 读取。 */
	UPROPERTY(BlueprintReadOnly)
	FText InventoryItemsText;

	/** 给蓝图直接展示的当前选择摘要；鱼会显示重量，其他物体显示类别和定义。 */
	UPROPERTY(BlueprintReadOnly)
	FText SelectedFishText;

	/** 给蓝图直接展示的最近动作反馈；pending、成功和错误都只读这条文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText ResultText;
};
