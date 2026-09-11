#pragma once

#include "CoreMinimal.h"
#include "FishContainers/CatFishContainerTypes.h"
#include "GameFramework/SaveGame.h"
#include "CatRunSaveGame.generated.h"

/** 磁盘中一格已提交运行库存；Save 拥有这份 DTO，来源是领域系统导出的正式快照，不把运行复制类型变成磁盘契约。 */
USTRUCT()
struct FCatSavedRunInventorySlot
{
	GENERATED_BODY()

	/** 物品运行定义键；恢复时由领域目录重新验证，未知定义不能进入库存。 */
	UPROPERTY(SaveGame)
	FName DefinitionId = NAME_None;

	/** 该格物品的稳定实例键；玩家、营地库存和世界鱼容器之间不能重复，跨容器移动仍保留同一身份。 */
	UPROPERTY(SaveGame)
	FGuid ItemInstanceId;

	/** 持久化格的堆叠数量；Inventory 导出会把合法 held 实例放回空格，未提交偷鱼窗口不能保存。 */
	UPROPERTY(SaveGame)
	int32 Quantity = 0;

	/** 鱼竿格当前耐久；非鱼竿格必须保持零，恢复前会按定义复核。 */
	UPROPERTY(SaveGame)
	double RodDurability = 0.0;

	/** 鱼竿是否已损坏；只对鱼竿定义有意义，其他物品出现该状态即拒绝。 */
	UPROPERTY(SaveGame)
	bool bRodBroken = false;

	/** 鱼的捕获会话身份；保存从鱼实例读取，恢复时用于保留来源，普通物品保持无效 GUID。 */
	UPROPERTY(SaveGame)
	FGuid FishSessionId;

	/** 单条鱼冻结的真实重量，单位千克；存档原样保留，加载不会重新抽取重量。 */
	UPROPERTY(SaveGame)
	double FishWeightKilograms = 0.0;

	/** 鱼的捕获者稳定标识；仅在本机磁盘与服务器恢复链使用，不写日志或复制给其他玩家。 */
	UPROPERTY(SaveGame)
	FString FishOwnerStableNetId;
};

/** 磁盘中的玩家钓具选择载荷；随身库存由玩家运行状态单独保存。 */
USTRUCT()
struct FCatSavedEquipmentLoadout
{
	GENERATED_BODY()

	/** 保存时的钓具选择读模型版本；恢复会产生新的权威版本，原值只保留审计上下文。 */
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

	/** 当前选中鱼竿皮肤定义键；它不携带实例键，领域会验证其与鱼竿组合。 */
	UPROPERTY(SaveGame)
	FName RodSkinDefinitionId = NAME_None;

	/** 当前选中鱼竿的摘要耐久；必须与选中鱼竿库存格耐久一致。 */
	UPROPERTY(SaveGame)
	double RodDurability = 0.0;

	/** 当前选中鱼竿的摘要损坏状态；必须与选中鱼竿库存格损坏状态一致。 */
	UPROPERTY(SaveGame)
	bool bRodBroken = false;
};

/** 磁盘中的营地公共库存；它由 Save 转换为 Camp 的受控恢复输入，避免 Camp 反向依赖 Save。 */
USTRUCT()
struct FCatSavedCampInventory
{
	GENERATED_BODY()

	/** 营地内已提交库存格；保存前和恢复前均由 Camp 按容量与定义核验。 */
	UPROPERTY(SaveGame)
	TArray<FCatSavedRunInventorySlot> InventorySlots;
};

/** 本机玩家在某个世界槽中的可恢复运行事实；长期档案、解锁和 Profile 不进入这份世界存档。 */
USTRUCT()
struct FCatSavedPlayerRunState
{
	GENERATED_BODY()

	/** 该玩家离开时的钓具选择和鱼竿摘要；正式角色恢复时由 Equipment 校验它是否引用随身库存中的实例。 */
	UPROPERTY(SaveGame)
	FCatSavedEquipmentLoadout EquipmentSnapshot;

	/** 该玩家离开时的随身库存格；正式角色恢复时由 InventoryComponent 反序列化为唯一库存事实。 */
	UPROPERTY(SaveGame)
	TArray<FCatSavedRunInventorySlot> InventorySlots;

	/** 该玩家角色在当前世界中的权威 Transform；生成 Pawn 后由 GameMode 恢复，坐标以厘米表示。 */
	UPROPERTY(SaveGame)
	FTransform CharacterTransform = FTransform::Identity;
};

/** 前端存档列表的一项摘要；当前格式从世界槽文件自身提取列表所需信息。 */
USTRUCT(BlueprintType)
struct FCatSaveSlotSummary
{
	GENERATED_BODY()

	/** 槽的稳定内部标识；文件名、加载和删除都以它为准，显示名称不能参与定位。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName SlotId = NAME_None;

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

	/** 最近一次保存时 Run 公开的上次供品点数；它只做列表/加载展示元数据，不参与后续 Run 恢复。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 LastOfferingPoints = 0;

	/** 最近一次保存时 Run 公开的每日供品目标；未配置时保持 0，前端不能把它显示成已完成。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 DailyOfferingTarget = 0;

	/** 最近一次保存时 Run 公开的世界进度；它是当前一局成败进度的摘要，不参与后续 Run 恢复。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 WorldProgress = 10;

	/** 最近一次保存时 Run 公开的上一晚世界进度变化；它只用于摘要解释，不参与后续 Run 恢复。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 LastWorldProgressDelta = 0;
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

class UCatRunSaveGame;

/** LocalPlayer 存档对象的磁盘完成回执；协调器绑定一次，收到真实结果后才允许房主退出。 */
DECLARE_DELEGATE_TwoParams(FCatRunSaveFinished, UCatRunSaveGame*, bool);

/** 房主本机玩家拥有的世界存档；沿用 Lyra 的 LocalPlayerSaveGame 生命周期，领域恢复由 SaveSubsystem 协调。 */
UCLASS()
class CATFISHING_API UCatRunSaveGame : public ULocalPlayerSaveGame
{
	GENERATED_BODY()
public:
	/** 返回本项目当前载荷版本；引擎在保存前写入 SavedDataVersion，读取方据此拒绝未知格式。 */
	virtual int32 GetLatestDataVersion() const override;

	/** 读盘后只迁移已知 v5 数据，不访问 World 或角色；库存与位置等到正式宿主就绪后再应用。 */
	virtual void HandlePostLoad() override;

	/** 写盘完成后接收引擎真实结果，并消费一次完成委托；不会把受理成功当作落盘成功。 */
	virtual void HandlePostSave(bool bSuccess) override;

	/** 当前不可变写盘候选的完成接收者；Subsystem 写盘前绑定，完成时清空，不进入磁盘。 */
	FCatRunSaveFinished OnSaveFinished;

	/** 旧 USaveGame 文件的格式标记；保留字段名以识别 v5，加载时升级为 v6，新文件同时由引擎记录数据版本。 */
	UPROPERTY(SaveGame)
	int32 FormatVersion = 6;

	/** 是否已采集过正式世界；新建空槽为 false，首次采样后为 true，区分新局和缺失世界载荷。 */
	UPROPERTY(SaveGame)
	bool bHasWorldSnapshot = false;

	/** 该载荷归属的槽标识；读取时必须与请求槽一致，防止文件串档。 */
	UPROPERTY(SaveGame)
	FName SlotId = NAME_None;

	/** 玩家给这个世界槽起的显示名；文件扫描时直接读它生成列表。 */
	UPROPERTY(SaveGame)
	FString DisplayName;

	/** 该载荷最后成功写入的 UTC 时间；只在异步保存成功前准备，成功后由文件自身提供列表摘要。 */
	UPROPERTY(SaveGame)
	FDateTime LastSavedAt;

	/** 该槽累计的实际玩法秒数；Save 采样当前 World 已运行秒数后写入，恢复时只用于继续统计摘要。 */
	UPROPERTY(SaveGame)
	double PlayedDurationSeconds = 0.0;

	/** 写盘时权威 Run 已公开的天数；只给前端摘要或全局遮罩展示，不恢复 Run 的阶段或时钟。 */
	UPROPERTY(SaveGame)
	int32 DayIndex = 0;

	/** 写盘时真实 World 的地图短名；只作为存档列表地点，不驱动旅行目标选择。 */
	UPROPERTY(SaveGame)
	FString LocationName;

	/** 写盘时权威 Run 公共上次供品点数；它不成为第二份 Run 真相，也不在恢复时写回 GameMode。 */
	UPROPERTY(SaveGame)
	int32 LastOfferingPoints = 0;

	/** 写盘时权威 Run 公共每日供品目标；0 表示本局未发布正目标。 */
	UPROPERTY(SaveGame)
	int32 DailyOfferingTarget = 0;

	/** 写盘时权威 Run 公共世界进度；它不成为第二份 Run 真相，也不在恢复时写回 GameMode。 */
	UPROPERTY(SaveGame)
	int32 WorldProgress = 10;

	/** 写盘时权威 Run 上次世界进度变化；它只为存档列表和调试展示保留。 */
	UPROPERTY(SaveGame)
	int32 LastWorldProgressDelta = 0;

	/** 本机玩家快照是否已经写入这个槽；新建空槽为 false，首次保存或离开前捕获成功后为 true。 */
	UPROPERTY(SaveGame)
	bool bHasPlayerSnapshot = false;

	/** 活动槽内唯一的本机玩家末次状态；保存时由 UE SaveGame 序列化，恢复时直接反序列化回来应用到新 Pawn。 */
	UPROPERTY(SaveGame)
	FCatSavedPlayerRunState PlayerSnapshot;

	/** 当前世界唯一共享营地仓库的已提交内容；多营地或无营地时保存与恢复都拒绝。 */
	UPROPERTY(SaveGame)
	FCatSavedCampInventory CampInventory;

	/** 世界鱼容器的已提交鱼；每项用关卡稳定键重新关联，而非运行期随机 GUID。 */
	UPROPERTY(SaveGame)
	TArray<FCatPersistentContainerSnapshot> WorldFishContainers;
};
