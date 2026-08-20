#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatProfileContracts.generated.h"

/** 永久授予的稳定类别；Grant 内容不可变且不包含 ACK 状态。 */
UENUM(BlueprintType)
enum class ECatProfileGrantKind : uint8
{
	/** 「钓起」轨：捕获已提交后记录鱼种、真实重量与首次条件，收件人是上钩成立时的钓手。 */
	FishRecorded,
	/**
	 * 鱼册剪影页的永久 Grant。飞书鱼册口径是剪影由供奉推进（World_Progress）解锁；旧的"重试耗尽"资格路径已随重试制废
	 * 除（钓鱼规则 2026-08-18）删除，当前生产侧还没有任何入口生成这一类 Grant，Profile 只保留消费能力。
	 */
	FishSilhouette,
	/** CapturePlan 本地成像成功后授予一张印记索引。 */
	Imprint,
	/** 已提交里程碑归约出的解锁；具体内容和收益由产品定义。 */
	Unlock,
	/** 「抄获」轨：同一次已提交捕获里抄网命中者独得的记录，与 FishRecorded 并列而不是替代；自钓自抄时两条都发。 */
	FishScooped
};

/** 鱼图鉴页的三态；状态只能 Unknown→Silhouette→Recorded 单向推进。 */
UENUM(BlueprintType)
enum class ECatFishCollectionState : uint8
{
	/** 从未获得合格候选或捕获事实。 */
	Unknown,
	/** 曾经合格交手但未捕获。 */
	Silhouette,
	/** 至少一次捕获事务已经提交。 */
	Recorded
};

/** 捕获时冻结的图鉴条件；轴的枚举/内容由 Environment/Data 所有，本合同只保存稳定名称。 */
USTRUCT(BlueprintType)
struct FCatCaptureConditionSnapshot
{
	GENERATED_BODY()

	/** 捕获所在 WaterRegion 稳定 ID。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FName RegionId = NAME_None;

	/** 局内时段稳定 ID；未接正式时段资产时为 None。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FName TimeOfDayId = NAME_None;

	/** 局内天气稳定 ID；未接正式天气资产时为 None。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FName WeatherId = NAME_None;
};

/** 服务器归约后投递给拥有者客户端的不可变永久授予；ACK 保存在独立 DeliveryRecord。 */
USTRUCT(BlueprintType)
struct FCatProfileGrant
{
	GENERATED_BODY()

	/** 每份授予的全局随机 ID；客户端 Journal、服务器重发和 ACK 都只用该键。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FGuid GrantId;

	/** 授予类别。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	ECatProfileGrantKind Kind = ECatProfileGrantKind::FishRecorded;

	/** 鱼图鉴授予对应的 FishDefinition ID；印记/解锁可为 None。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FName FishDefinitionId = NAME_None;

	/** 捕获真实重量，单位千克；仅 FishRecorded 使用。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	double WeightKilograms = 0.0;

	/** 首次捕获条件；重复捕获只更新最佳重量，不覆盖首次条件。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FCatCaptureConditionSnapshot CaptureCondition;

	/** 印记授予对应的稳定 ImprintId；只有 CapturePlan 成像成功后生成。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FGuid ImprintId;

	/** 印记所属一局相册 ID；服务器不持有该相册或本地路径。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FGuid RunAlbumId;

	/** 该印记是否来自已确认全员到场的固定营地篝火计划；本地只能在此值为 true 时更新相册封面。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	bool bRunAlbumCover = false;

	/** 解锁授予对应的稳定内容 ID；内容收益未裁时服务不生成此 Grant。 */
	UPROPERTY(BlueprintReadOnly, SaveGame)
	FName UnlockId = NAME_None;

	/** 服务器私有接收者 StableNetId；无 UPROPERTY，因而不进入蓝图/SaveGame/复制内容。 */
	FString RecipientStableNetId;
};

/** 本地 durable Journal 的单向阶段；CapturePlanId 永远不进入该结构。 */
UENUM()
enum class ECatGrantJournalStage : uint8
{
	/** Grant 内容已写入 SaveGame，但尚未确认合并。 */
	Pending,
	/** Grant 已幂等合并并再次写盘，可向服务器 ACK。 */
	Complete
};

/** 一个 GrantId 的本地先落盘记录；启动恢复只重放 Pending。 */
USTRUCT()
struct FCatPendingGrantJournalEntry
{
	GENERATED_BODY()

	/** 不可变 Grant 内容；Journal 不接受 CapturePlan。 */
	UPROPERTY(SaveGame)
	FCatProfileGrant Grant;

	/** 当前 durable 阶段。 */
	UPROPERTY(SaveGame)
	ECatGrantJournalStage Stage = ECatGrantJournalStage::Pending;
};

/** 本地应用 Grant 的结构化结果；只有 bAckAllowed=true 才可更新服务器 GrantDeliveryRecord。 */
USTRUCT(BlueprintType)
struct FCatProfileApplyResult
{
	GENERATED_BODY()

	/** 输入 GrantId。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid GrantId;

	/** 本次是否完成首次 durable 合并。 */
	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	/** 本地已持久化完整终态，允许 ACK。 */
	UPROPERTY(BlueprintReadOnly)
	bool bAckAllowed = false;

	/** 结构化错误；重复 GrantId 已 durable 时为 AlreadyResolved 且仍允许 ACK。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::DependencyUnavailable;
};

/** 本地鱼图鉴单页持久事实；同一页并列保存「钓起」轨（状态与个人最佳）和「抄获」轨（次数）。实物鱼删除不会触碰此结构。 */
USTRUCT()
struct FCatFishCollectionRecord
{
	GENERATED_BODY()

	/** FishDefinition 稳定 ID。 */
	UPROPERTY(SaveGame)
	FName FishDefinitionId = NAME_None;

	/** 当前三态；只单向推进。 */
	UPROPERTY(SaveGame)
	ECatFishCollectionState State = ECatFishCollectionState::Unknown;

	/** 已记录状态下的个人最佳真实重量。 */
	UPROPERTY(SaveGame)
	double BestWeightKilograms = 0.0;

	/** 首次记录时冻结的条件；后续破纪录不覆盖。 */
	UPROPERTY(SaveGame)
	FCatCaptureConditionSnapshot FirstCaptureCondition;

	/** 合格剪影候选累计次数；Recorded 后仍保留历史。 */
	UPROPERTY(SaveGame)
	int32 EncounterCount = 0;

	/**
	 * 本人用抄网命中这个鱼种的累计次数，即「抄获」记录轨的全部内容。
	 * 它与 State/BestWeightKilograms 所在的「钓起」轨互不影响：抄网命中只累加这个计数，
	 * 不推进图鉴状态也不参与个人最佳；自己钓自己抄时同一次捕获会同时推进两轨。
	 * 只记次数是因为飞书目前只把抄获拍成一条独立记录轨，没有定义它展示哪些字段。
	 */
	UPROPERTY(SaveGame)
	int32 ScoopedCount = 0;
};

/** 本地相册索引中的一条印记记录；它只保存稳定 ID，不保存图片字节、编码格式或磁盘路径。 */
USTRUCT()
struct FCatLocalImprintRecord
{
	GENERATED_BODY()

	/** 成像桥成功后生成的稳定印记 ID。 */
	UPROPERTY(SaveGame)
	FGuid ImprintId;

	/** 该印记所属的一局相册 ID。 */
	UPROPERTY(SaveGame)
	FGuid RunAlbumId;

	/** 只有全员篝火计划成功时为 true；Profile 用它决定是否可设置封面。 */
	UPROPERTY(SaveGame)
	bool bRunAlbumCover = false;

	/** 本人是否在自己的相册隐藏该印记；只影响本地索引，不删除其他参与者副本。 */
	UPROPERTY(SaveGame)
	bool bHidden = false;
};
