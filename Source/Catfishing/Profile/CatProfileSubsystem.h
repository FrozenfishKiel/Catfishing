#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatProfileSubsystem.generated.h"

class UCatProfileSaveGame;

/** 外部本地成像桥监听的新计划广播；监听方只在真实图片 durable 后回报成功。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCapturePlanReceived, const FCatCapturePlan&);

/**
 * 本人相册里一局对应的一页摘要，即飞书「按局分组、一局一页」中那一页的目录项。
 * 它是从 SaveGame 现算的只读投影，不是第二份持久事实：封面取自 RunAlbumCovers，条数取自 Imprints，
 * 因此不存在“摘要与档案不同步”的中间状态。它只描述本人档案，任何回看入口都拿不到别人的相册。
 */
USTRUCT(BlueprintType)
struct FCatRunAlbumSummary
{
	GENERATED_BODY()

	/** 这一页对应的一局相册 ID；它由服务器在建立成像计划时冻结，本地只做分组键。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RunAlbumId;

	/** 这一页的封面印记 ID，即该局篝火全员合影。没有封面 Grant，或封面被本人隐藏而调用方又要求过滤时保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid CoverImprintId;

	/** 这一页在当前过滤条件下的印记条数；调用方要求排除隐藏项时，隐藏的那些不计入。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ImprintCount = 0;
};

/** 每个 LocalPlayer 的永久档案深模块；它拥有 SaveGame Journal 和内容合并，不接触服务器实物容器。 */
UCLASS()
class CATFISHING_API UCatProfileSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** 解析本地槽位、加载或创建 SaveGame，并重放所有 durable Pending Grant；未配置时保持不可写。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 释放内存 SaveGame 与瞬时 CapturePlan 广播；不在销毁阶段把未落盘状态冒充成功。 */
	virtual void Deinitialize() override;

	/**
	 * 应用一份不可变 Grant：先写 Pending Journal，再合并内容并写 Complete，只有第二次落盘成功才允许 ACK。
	 * 会让相册变长的印记授予还要先过跨局容量上限；超容返回 CapacityExceeded 且不允许 ACK，
	 * 服务器因此保留该 Grant 待重投，而不是被告知“已收好”。上限只拒绝新增，不淘汰任何已有印记。
	 */
	FCatProfileApplyResult ApplyGrant(const FCatProfileGrant& Grant);

	/** 接收服务器 CapturePlan；只有计划完整且外部桥已接入时才广播并返回 true，否则返回 false 让 Controller 明确回报失败终态。 */
	bool ReceiveCapturePlan(const FCatCapturePlan& Plan);

	/** 复制本地鱼图鉴公开快照供“互看图鉴”；不包含相册、隐藏印记、Journal、解锁或装备选择。 */
	bool GetFishCollectionSnapshot(TArray<FCatFishCollectionRecord>& OutRecords) const;

	/** 复制本地已正式授予的跨局解锁 ID 清单，供 owning client 上报给服务器 PlayerState 做装配 gate；Profile 不可用时返回 false。 */
	bool GetUnlockIdsSnapshot(TArray<FName>& OutUnlockIds) const;

	/** 只在本地相册切换本人隐藏状态并 durable 保存；不产生服务器全局撤下或修改其他玩家副本。 */
	FCatDomainCommandResult SetImprintHidden(FGuid RequestId, FGuid ImprintId, bool bHidden);

	/**
	 * 按局分组列出本人相册的全部页；档案不可用时返回 false 且清空输出。
	 * 页序为各局相册在本地索引里首次出现的顺序，也就是印记落盘的先后，因此不需要额外时间戳就能稳定“翻旧账”。
	 * bIncludeHidden=false 时排除本人隐藏的印记，一页被隐藏光了就不再出现在目录里；隐藏可逆，取消隐藏后该页原位复现。
	 * 局内相册、篝火局末轮播和跨局个人相册共用这一个入口，它只读当前 LocalPlayer 自己的档案。
	 */
	bool GetRunAlbumSummaries(bool bIncludeHidden, TArray<FCatRunAlbumSummary>& OutAlbums) const;

	/**
	 * 读取指定一局相册的封面印记 ID；没有封面、档案不可用，或封面被本人隐藏而调用方要求过滤时返回 false 且输出无效值。
	 * 它直接读封面映射这份 SSOT，不从印记列表反推，也不会在缺封面时挑一张顶替。
	 */
	bool TryGetRunAlbumCover(FGuid RunAlbumId, bool bIncludeHidden, FGuid& OutCoverImprintId) const;

	/**
	 * 读取指定一局相册的印记列表，顺序与本地索引落盘顺序一致；档案不可用或相册 ID 无效时返回 false 且清空输出。
	 * bIncludeHidden=false 时排除本人隐藏项。相册为空是合法结果，仍返回 true——它表示“这一局没有可看的印记”，不是读取失败。
	 */
	bool GetRunAlbumImprints(FGuid RunAlbumId, bool bIncludeHidden, TArray<FCatLocalImprintRecord>& OutImprints) const;

	/** 外部本地成像桥订阅入口；订阅者负责自己的图片格式、原子文件写与容量策略。 */
	FCatCapturePlanReceived OnCapturePlanReceived;

private:
	/** 校验 Grant 内容是否足以进入 Journal；拒绝发生在任何 SaveGame 写入之前。 */
	static ECatDomainCommandError ValidateGrant(const FCatProfileGrant& Grant);

	/** 把一份已落 Pending 的 Grant 幂等合并到内存档案；不自行保存或发 ACK。 */
	bool MergeGrantIntoProfile(const FCatProfileGrant& Grant);

	/** 完成一个已存在的 Pending Journal：合并、标 Complete、保存；失败时重新加载磁盘 Pending 事实。 */
	FCatProfileApplyResult CompletePendingGrant(FGuid GrantId);

	/** 把当前 SaveGame 同步写入精确槽位；失败只返回 false，不修改 Journal 阶段。 */
	bool SaveCurrentProfile() const;

	/** 从磁盘重新加载当前槽位，恢复最后一次 durable 事实；加载失败时清空可写状态。 */
	bool ReloadDurableProfile();

	/** 当前 LocalPlayer 独占的内存 SaveGame；只在持久化 gate 与槽位解析成功后有效。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatProfileSaveGame> CurrentProfile;

	/** 由基础名和 ControllerId 组成的实际槽位；不从服务器或 StableNetId 派生。 */
	FString ResolvedSlotName;

	/** SaveGame API 的本地用户索引；直接取 LocalPlayer ControllerId，负值时不启用持久化。 */
	int32 ResolvedUserIndex = INDEX_NONE;

	/** 初始化完成且当前内存对象与 durable 槽位一致时为 true；任一次恢复失败都会关闭后续 ACK。 */
	bool bPersistenceReady = false;
};
