#pragma once

#include "CoreMinimal.h"
#include "Camp/CatCampInventoryActor.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Items/CatItemsService.h"
#include "GameFramework/SaveGame.h"
#include "CatRunSaveGame.generated.h"

/** 磁盘中一格已提交运行库存；Save 拥有这份 DTO，来源可以是正式库存投影或旧宿主兼容快照，不把运行复制类型变成磁盘契约。 */
USTRUCT()
struct FCatSavedRunInventorySlot
{
	GENERATED_BODY()

	/** 物品运行定义键；恢复时由领域目录重新验证，未知定义不能进入库存。 */
	UPROPERTY(SaveGame)
	FName DefinitionId = NAME_None;

	/** 该格物品的稳定实例身份；玩家与营地库存之间不能重复，世界鱼的 FishInstanceId 属于独立校验域。 */
	UPROPERTY(SaveGame)
	FGuid ItemInstanceId;

	/** 已提交堆叠数量；预留与 escrow 从不折算进这个数。 */
	UPROPERTY(SaveGame)
	int32 Quantity = 0;

	/** 鱼竿格当前耐久；非鱼竿格必须保持零，恢复前会按定义复核。 */
	UPROPERTY(SaveGame)
	double RodDurability = 0.0;

	/** 鱼竿是否已损坏；只对鱼竿定义有意义，其他物品出现该状态即拒绝。 */
	UPROPERTY(SaveGame)
	bool bRodBroken = false;
};

/** 磁盘中的玩家运行载荷；字段名沿用 EquipmentSnapshot 兼容旧格式，内容表达钓具选择与可恢复随身库存格。 */
USTRUCT()
struct FCatSavedEquipmentLoadout
{
	GENERATED_BODY()

	/** 保存时的钓具选择兼容修订号；恢复会产生新的权威修订，原值只保留审计上下文。 */
	UPROPERTY(SaveGame)
	int64 Revision = 0;

	/** 当前选中鱼竿的定义键与实例键；必须引用同一已保存库存格。 */
	UPROPERTY(SaveGame)
	FName RodDefinitionId = NAME_None;

	/** 当前选中鱼竿的实例键；没有选择时保持无效 GUID。 */
	UPROPERTY(SaveGame)
	FGuid RodItemInstanceId;

	/** 当前选中鱼饵的定义键与实例键；恢复前必须匹配库存中的可装备定义。 */
	UPROPERTY(SaveGame)
	FName BaitDefinitionId = NAME_None;

	/** 当前选中鱼饵的实例键；没有选择时保持无效 GUID。 */
	UPROPERTY(SaveGame)
	FGuid BaitItemInstanceId;

	/** 当前选中浮漂的定义键与实例键；恢复前必须匹配库存中的可装备定义。 */
	UPROPERTY(SaveGame)
	FName FloatDefinitionId = NAME_None;

	/** 当前选中浮漂的实例键；没有选择时保持无效 GUID。 */
	UPROPERTY(SaveGame)
	FGuid FloatItemInstanceId;

	/** 当前选中抄网的定义键与实例键；恢复前必须匹配库存中的可装备定义。 */
	UPROPERTY(SaveGame)
	FName ScoopNetDefinitionId = NAME_None;

	/** 当前选中抄网的实例键；没有选择时保持无效 GUID。 */
	UPROPERTY(SaveGame)
	FGuid ScoopNetItemInstanceId;

	/** 当前选中鱼竿皮肤定义键；它不携带实例身份，领域会验证其与鱼竿组合。 */
	UPROPERTY(SaveGame)
	FName RodSkinDefinitionId = NAME_None;

	/** 当前选中鱼竿的摘要耐久；必须与选中鱼竿库存格耐久一致。 */
	UPROPERTY(SaveGame)
	double RodDurability = 0.0;

	/** 当前选中鱼竿的摘要损坏状态；必须与选中鱼竿库存格损坏状态一致。 */
	UPROPERTY(SaveGame)
	bool bRodBroken = false;

	/** 所有已提交随身库存格；正式角色保存时来自 InventoryComponent 投影，旧宿主来自兼容快照，顺序保持玩家整理后的槽位顺序。 */
	UPROPERTY(SaveGame)
	TArray<FCatSavedRunInventorySlot> InventorySlots;
};

/** 磁盘中的营地公共库存；它由 Save 转换为 Camp 的受控恢复输入，避免 Camp 反向依赖 Save。 */
USTRUCT()
struct FCatSavedCampInventory
{
	GENERATED_BODY()

	/** 保存时营地库存修订号；恢复将递增领域修订，原值不直接写运行复制状态。 */
	UPROPERTY(SaveGame)
	int64 Revision = 0;

	/** 营地内已提交库存格；保存前和恢复前均由 Camp 按容量与定义核验。 */
	UPROPERTY(SaveGame)
	TArray<FCatSavedRunInventorySlot> InventorySlots;
};

/** 单位玩家在某个世界槽中的可恢复运行事实；长期档案、解锁和 Profile 不进入这份世界存档。 */
USTRUCT()
struct FCatSavedPlayerRunState
{
	GENERATED_BODY()

	/** 该玩家的平台稳定身份键；服务器从 PlayerState 读取并用它把存档归还给同一位玩家。 */
	UPROPERTY(SaveGame)
	FString StableNetId;

	/** 该玩家离开时的钓具选择、随身库存格和鱼竿耐久；字段名保持兼容，正式角色恢复时会经 Equipment 重建正式库存组件。 */
	UPROPERTY(SaveGame)
	FCatSavedEquipmentLoadout EquipmentSnapshot;

	/** 该玩家角色在当前世界中的权威 Transform；生成 Pawn 后由 GameMode 恢复，坐标以厘米表示。 */
	UPROPERTY(SaveGame)
	FTransform CharacterTransform = FTransform::Identity;
};

/** 前端存档列表的一项摘要；索引文件只保留展示和磁盘定位所需的最小信息。 */
USTRUCT(BlueprintType)
struct FCatSaveSlotSummary
{
	GENERATED_BODY()

	/** 槽的稳定内部标识；文件名、加载和删除都以它为准，显示名称不能参与定位。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SlotId = NAME_None;

	/** 索引已提交的世界载荷代号；读取只接受不晚于此值的有效代，未提交索引的孤立新代不能进入游戏。 */
	UPROPERTY(SaveGame)
	int64 RunGeneration = 0;

	/** 玩家为槽设置的显示名称；仅用于前端列表，不是磁盘路径或跨会话键。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString DisplayName;

	/** 最近一次成功写入该槽的 UTC 时间；前端按它显示和排序，失败写入不会更新它。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDateTime LastSavedAt;

	/** 最近一次保存时 Run 公共快照给出的天数；0 表示载荷尚未进入可观测的正式 DayActive。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 DayIndex = 0;

	/** 最近一次保存时所在的真实地图短名；它来自 World，不假造策划地点名称。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString LocationName;

	/** 当前槽已累计的实际玩法秒数；由 Save 在每个已恢复 World 的真实运行时长上累加，不计前端停留时间。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	double PlayedDurationSeconds = 0.0;

	/** 最近一次保存时 Run 公开的献祭额度进度；它是列表/加载展示元数据，不参与后续 Run 恢复。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 SacrificeProgress = 0;

	/** 最近一次保存时 Run 公开的献祭额度目标；未配置时保持 0，前端不能把它显示成已完成。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 SacrificeTarget = 0;
};

/** 存档请求的同步受理结果；异步磁盘完成情况由 UCatSaveSubsystem 委托单独通知。 */
USTRUCT(BlueprintType)
struct FCatSaveResult
{
	GENERATED_BODY()

	/** 本次请求的关联 ID；调用方用它匹配之后的保存完成委托。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 请求是否已被子系统接收；true 不代表异步磁盘操作已经成功。 */
	UPROPERTY(BlueprintReadOnly)
	bool bAccepted = false;

	/** 受理或拒绝的可展示说明；真实失败原因也会写入最后结果文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText Message;
};

/** 磁盘上的槽位索引；它是槽列表的唯一来源，不把 Profile 误用为世界存档目录。 */
UCLASS()
class CATFISHING_API UCatSaveIndexSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	/** 索引格式版本；读取时先核对，避免新旧字段含义不同仍继续覆盖磁盘。 */
	UPROPERTY(SaveGame)
	int32 FormatVersion = 2;

	/** 当前机器可见的世界槽摘要；仅 Save 子系统读写，前端拿到的是 const 列表。 */
	UPROPERTY(SaveGame)
	TArray<FCatSaveSlotSummary> SlotSummaries;

	/** 已确认删除但可能尚未清完实体文件的槽；两代索引先移除其摘要并保留此意图，刷新时可幂等重试磁盘清理。 */
	UPROPERTY(SaveGame)
	TArray<FName> PendingDeletionSlotIds;
};

/** 一份可进入玩法世界的完整持久化载荷；它只保存明确已提交的世界状态。 */
UCLASS()
class CATFISHING_API UCatRunSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	/** 世界存档格式版本；未来迁移必须显式处理，未知版本直接拒绝恢复。 */
	UPROPERTY(SaveGame)
	int32 FormatVersion = 2;

	/** 是否已采集过正式世界；新建空槽为 false，首次采样后为 true，区分新局和缺失营地/容器的旧载荷。 */
	UPROPERTY(SaveGame)
	bool bHasWorldSnapshot = false;

	/** 该载荷归属的槽标识；读取时必须与请求槽一致，防止文件串档。 */
	UPROPERTY(SaveGame)
	FName SlotId = NAME_None;

	/** 该载荷最后成功写入的 UTC 时间；只在异步保存成功前准备，成功后同步到索引摘要。 */
	UPROPERTY(SaveGame)
	FDateTime LastSavedAt;

	/** 该槽累计的实际玩法秒数；Save 采样当前 World 已运行秒数后写入，恢复时只用于继续统计摘要。 */
	UPROPERTY(SaveGame)
	double PlayedDurationSeconds = 0.0;

	/** 写盘时权威 Run 已公开的天数；只给前端和加载页展示，不恢复 Run 的阶段或时钟。 */
	UPROPERTY(SaveGame)
	int32 DayIndex = 0;

	/** 写盘时真实 World 的地图短名；只作为存档列表地点，不驱动旅行目标选择。 */
	UPROPERTY(SaveGame)
	FString LocationName;

	/** 写盘时权威 Run 公共献祭进度；它不成为第二份 Run 真相，也不在恢复时写回 GameMode。 */
	UPROPERTY(SaveGame)
	int32 SacrificeProgress = 0;

	/** 写盘时权威 Run 公共献祭目标；0 表示本局未发布正目标。 */
	UPROPERTY(SaveGame)
	int32 SacrificeTarget = 0;

	/** 活动槽内所有玩家的末次正式状态，包含离线成员；在线采样按稳定身份合并，退出时捕获最后库存和位置。 */
	UPROPERTY(SaveGame)
	TArray<FCatSavedPlayerRunState> Players;

	/** 当前世界唯一共享营地仓库的已提交内容；多营地或无营地时保存与恢复都拒绝。 */
	UPROPERTY(SaveGame)
	FCatSavedCampInventory CampInventory;

	/** 世界鱼容器的已提交鱼；每项用关卡稳定键重新关联，而非运行期随机 GUID。 */
	UPROPERTY(SaveGame)
	TArray<FCatPersistentContainerSnapshot> WorldFishContainers;
};
