#include "Profile/CatProfileSubsystem.h"

#include "Logging/CatLog.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Profile/CatProfileSaveGame.h"
#include "Profile/CatProfileSettings.h"

namespace
{
	// Grant 内容等价流程：只比较会进入 SaveGame 的不可变授予事实；RecipientStableNetId 是服务器私有路由字段，不参与本地 ACK 幂等判断。
	bool AreProfileGrantPayloadsEquivalent(const FCatProfileGrant& Left, const FCatProfileGrant& Right)
	{
		return Left.GrantId == Right.GrantId
			&& Left.Kind == Right.Kind
			&& Left.FishDefinitionId == Right.FishDefinitionId
			&& FMath::IsNearlyEqual(Left.WeightKilograms, Right.WeightKilograms)
			&& Left.CaptureCondition.RegionId == Right.CaptureCondition.RegionId
			&& Left.CaptureCondition.TimeOfDayId == Right.CaptureCondition.TimeOfDayId
			&& Left.CaptureCondition.WeatherId == Right.CaptureCondition.WeatherId
			&& Left.ImprintId == Right.ImprintId
			&& Left.RunAlbumId == Right.RunAlbumId
			&& Left.bRunAlbumCover == Right.bRunAlbumCover
			&& Left.UnlockId == Right.UnlockId;
	}
}
// 初始化流程：先验证显式设置和 LocalPlayer 索引，再加载既有档案或创建空档案；最后逐个重放 Pending，任何落盘失败都关闭本次会话的 ACK 能力。
void UCatProfileSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UCatProfileSettings* Settings = GetDefault<UCatProfileSettings>();
	ResolvedUserIndex = GetLocalPlayer() ? GetLocalPlayer()->GetControllerId() : INDEX_NONE;
	if (!Settings || !Settings->IsPersistenceReady() || ResolvedUserIndex < 0)
	{
		return;
	}
	ResolvedSlotName = FString::Printf(TEXT("%s_%d"), *Settings->SaveSlotBaseName.TrimStartAndEnd(), ResolvedUserIndex);
	if (UGameplayStatics::DoesSaveGameExist(ResolvedSlotName, ResolvedUserIndex))
	{
		CurrentProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(ResolvedSlotName, ResolvedUserIndex));
	}
	else
	{
		CurrentProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::CreateSaveGameObject(UCatProfileSaveGame::StaticClass()));
		if (CurrentProfile && !SaveCurrentProfile())
		{
			CurrentProfile = nullptr;
		}
	}
	if (!CurrentProfile || CurrentProfile->SchemaVersion != UCatProfileSaveGame::CurrentSchemaVersion)
	{
		CurrentProfile = nullptr;
		return;
	}
	bPersistenceReady = true;
	TArray<FGuid> PendingGrantIds;
	for (const FCatPendingGrantJournalEntry& Entry : CurrentProfile->GrantJournal)
	{
		if (Entry.Stage == ECatGrantJournalStage::Pending && Entry.Grant.GrantId.IsValid())
		{
			PendingGrantIds.Add(Entry.Grant.GrantId);
		}
	}
	for (const FGuid& GrantId : PendingGrantIds)
	{
		if (!CompletePendingGrant(GrantId).bAckAllowed)
		{
			bPersistenceReady = false;
			break;
		}
	}
}

// 销毁流程：先关闭广播并清 durable 对象引用、槽位和索引，再交还父类；这里不做隐式保存，避免把尚未 Complete 的内存变化提交为成功。
void UCatProfileSubsystem::Deinitialize()
{
	OnCapturePlanReceived.Clear();
	bPersistenceReady = false;
	CurrentProfile = nullptr;
	ResolvedSlotName.Reset();
	ResolvedUserIndex = INDEX_NONE;
	Super::Deinitialize();
}

// Grant 应用流程：先拒绝无效/不可写输入，再用 durable Journal 校验同 GrantId 内容不可漂移；
// 首次进来的印记授予还要过一道相册容量检查，超容直接返回 CapacityExceeded 且不落 Journal；
// 通过后新 Grant 先追加 Pending 并保存，只有该保存成功才进入合并与 Complete 保存。
FCatProfileApplyResult UCatProfileSubsystem::ApplyGrant(const FCatProfileGrant& Grant)
{
	FCatProfileApplyResult Result;
	Result.GrantId = Grant.GrantId;
	Result.Error = ValidateGrant(Grant);
	if (Result.Error != ECatDomainCommandError::None || !bPersistenceReady || !CurrentProfile)
	{
		if (Result.Error == ECatDomainCommandError::None)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
		return Result;
	}
	FCatPendingGrantJournalEntry* ExistingEntry = CurrentProfile->GrantJournal.FindByPredicate(
		[&Grant](const FCatPendingGrantJournalEntry& Entry)
		{
			return Entry.Grant.GrantId == Grant.GrantId;
		});
	if (CurrentProfile->AppliedGrantIds.Contains(Grant.GrantId))
	{
		if (!ExistingEntry || !AreProfileGrantPayloadsEquivalent(ExistingEntry->Grant, Grant))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result.bAckAllowed = true;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (ExistingEntry)
	{
		if (!AreProfileGrantPayloadsEquivalent(ExistingEntry->Grant, Grant))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		return CompletePendingGrant(Grant.GrantId);
	}
	// 相册容量检查只挡“会新增一条印记索引”的首次授予，所以它在这里、在写 Journal 之前：
	// 上面两个分支（已 durable、已落 Pending）都是重放同一份授予，名额在它们首次进来时就已经占过，重放再判一次会把恢复期卡死。
	// 已存在同 ImprintId 的授予也不占新名额，因为合并阶段会按 ID 去重，不会让数组变长。
	if (Grant.Kind == ECatProfileGrantKind::Imprint)
	{
		const UCatProfileSettings* Settings = GetDefault<UCatProfileSettings>();
		const int32 Capacity = Settings ? Settings->MaxLocalAlbumImprints : 0;
		const bool bAlreadyIndexed = CurrentProfile->Imprints.ContainsByPredicate(
			[&Grant](const FCatLocalImprintRecord& Existing)
			{
				return Existing.ImprintId == Grant.ImprintId;
			});
		if (!bAlreadyIndexed && CurrentProfile->Imprints.Num() >= Capacity)
		{
			// 明确拒绝并留证，不做淘汰也不静默丢：这条授予不进 Journal，因此客户端不会 ACK，服务器保留该 Grant 待重投。
			// 上限未配置时 Capacity 为 0，第一条印记就走这里，与“真的存满了”共用同一条拒绝路径和同一条日志。
			UE_LOG(LogCatProfile, Warning,
				TEXT("Event=profile_album_capacity_exceeded GrantId=%s ImprintId=%s RunAlbumId=%s Stored=%d Capacity=%d"),
				*Grant.GrantId.ToString(EGuidFormats::DigitsWithHyphens),
				*Grant.ImprintId.ToString(EGuidFormats::DigitsWithHyphens),
				*Grant.RunAlbumId.ToString(EGuidFormats::DigitsWithHyphens),
				CurrentProfile->Imprints.Num(), Capacity);
			Result.Error = ECatDomainCommandError::CapacityExceeded;
			return Result;
		}
	}
	FCatPendingGrantJournalEntry& Entry = CurrentProfile->GrantJournal.AddDefaulted_GetRef();
	Entry.Grant = Grant;
	Entry.Grant.RecipientStableNetId.Reset();
	Entry.Stage = ECatGrantJournalStage::Pending;
	if (!SaveCurrentProfile())
	{
		CurrentProfile->GrantJournal.Pop();
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	return CompletePendingGrant(Grant.GrantId);
}

// CapturePlan 接收流程：验证计划稳定键、外部桥 gate 和真实订阅者；只有本地成像桥确实接管时才广播，拒绝时返回 false 供
// owning Controller 把服务器计划收口为明确失败。
bool UCatProfileSubsystem::ReceiveCapturePlan(const FCatCapturePlan& Plan)
{
	const UCatProfileSettings* Settings = GetDefault<UCatProfileSettings>();
	if (!Settings || !Settings->IsExternalImprintBridgeReady() || !OnCapturePlanReceived.IsBound() || !Plan.CapturePlanId.IsValid()
		|| !Plan.CandidateId.IsValid() || !Plan.RunId.IsValid() || !Plan.RunAlbumId.IsValid())
	{
		return false;
	}
	OnCapturePlanReceived.Broadcast(Plan);
	return true;
}

// 图鉴公开查询流程：先清输出，只在 durable Profile 可用时复制 FishCollection；相册、隐藏状态、Journal 与装备均不进入结果。
bool UCatProfileSubsystem::GetFishCollectionSnapshot(TArray<FCatFishCollectionRecord>& OutRecords) const
{
	OutRecords.Reset();
	if (!bPersistenceReady || !CurrentProfile)
	{
		return false;
	}
	OutRecords = CurrentProfile->FishCollection;
	return true;
}

// 解锁清单查询流程：先清输出，只在 durable Profile 可用时复制 UnlockIds；它是只读复制，不把 Profile 对象交出去。
bool UCatProfileSubsystem::GetUnlockIdsSnapshot(TArray<FName>& OutUnlockIds) const
{
	OutUnlockIds.Reset();
	if (!bPersistenceReady || !CurrentProfile)
	{
		return false;
	}
	OutUnlockIds = CurrentProfile->UnlockIds;
	return true;
}

// 印记隐藏流程：定位本人本地索引并只改 bHidden；保存失败恢复旧值，不发送服务器 RPC，也不删除图片或其他玩家记录。
FCatDomainCommandResult UCatProfileSubsystem::SetImprintHidden(const FGuid RequestId, const FGuid ImprintId,
	const bool bHidden)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	FCatLocalImprintRecord* Record = CurrentProfile ? CurrentProfile->Imprints.FindByPredicate([ImprintId](const FCatLocalImprintRecord& Candidate)
	{
		return Candidate.ImprintId == ImprintId;
	}) : nullptr;
	if (!bPersistenceReady || !RequestId.IsValid() || !ImprintId.IsValid() || !Record)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	const bool bPrevious = Record->bHidden;
	Record->bHidden = bHidden;
	if (!SaveCurrentProfile())
	{
		Record->bHidden = bPrevious;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

// 相册目录流程：按本地索引顺序遍历一次印记，边过滤边把首次见到的相册追加成新页，已见到的相册只累加条数；
// 页序因此等于各局第一条印记的落盘顺序。全部页建好后再补封面，让“封面本身被隐藏”与页内过滤用同一套口径。
bool UCatProfileSubsystem::GetRunAlbumSummaries(const bool bIncludeHidden, TArray<FCatRunAlbumSummary>& OutAlbums) const
{
	OutAlbums.Reset();
	if (!bPersistenceReady || !CurrentProfile)
	{
		return false;
	}
	TMap<FGuid, int32> PageIndexByAlbum;
	for (const FCatLocalImprintRecord& Record : CurrentProfile->Imprints)
	{
		if (!Record.RunAlbumId.IsValid() || (!bIncludeHidden && Record.bHidden))
		{
			continue;
		}
		const int32* ExistingPageIndex = PageIndexByAlbum.Find(Record.RunAlbumId);
		int32 PageIndex = ExistingPageIndex ? *ExistingPageIndex : INDEX_NONE;
		if (PageIndex == INDEX_NONE)
		{
			PageIndex = OutAlbums.Num();
			PageIndexByAlbum.Add(Record.RunAlbumId, PageIndex);
			FCatRunAlbumSummary& Page = OutAlbums.AddDefaulted_GetRef();
			Page.RunAlbumId = Record.RunAlbumId;
		}
		++OutAlbums[PageIndex].ImprintCount;
	}
	for (FCatRunAlbumSummary& Page : OutAlbums)
	{
		// 封面查不到就保持无效：这一页仍然成立，只是没有全员合影可放，调用方据此决定用什么占位。
		TryGetRunAlbumCover(Page.RunAlbumId, bIncludeHidden, Page.CoverImprintId);
	}
	return true;
}

// 封面读取流程：先清输出，再从封面映射取该局的唯一封面；要求过滤隐藏项时还要回查这条印记本身是否被本人隐藏。
// 回查是必须的：隐藏只改印记记录的 bHidden，不会从封面映射里删键，少了这一步就会把已经隐藏的照片当封面播出去。
bool UCatProfileSubsystem::TryGetRunAlbumCover(const FGuid RunAlbumId, const bool bIncludeHidden,
	FGuid& OutCoverImprintId) const
{
	OutCoverImprintId.Invalidate();
	if (!bPersistenceReady || !CurrentProfile || !RunAlbumId.IsValid())
	{
		return false;
	}
	const FGuid* CoverImprintId = CurrentProfile->RunAlbumCovers.Find(RunAlbumId);
	if (!CoverImprintId || !CoverImprintId->IsValid())
	{
		return false;
	}
	if (!bIncludeHidden)
	{
		const FCatLocalImprintRecord* CoverRecord = CurrentProfile->Imprints.FindByPredicate(
			[CoverImprintId](const FCatLocalImprintRecord& Candidate)
			{
				return Candidate.ImprintId == *CoverImprintId;
			});
		if (!CoverRecord || CoverRecord->bHidden)
		{
			return false;
		}
	}
	OutCoverImprintId = *CoverImprintId;
	return true;
}

// 相册翻页流程：先清输出，再按本地索引顺序复制归属该局的印记记录；空相册返回 true，表示“这一局没有可看的印记”而不是读失败。
bool UCatProfileSubsystem::GetRunAlbumImprints(const FGuid RunAlbumId, const bool bIncludeHidden,
	TArray<FCatLocalImprintRecord>& OutImprints) const
{
	OutImprints.Reset();
	if (!bPersistenceReady || !CurrentProfile || !RunAlbumId.IsValid())
	{
		return false;
	}
	for (const FCatLocalImprintRecord& Record : CurrentProfile->Imprints)
	{
		if (Record.RunAlbumId == RunAlbumId && (bIncludeHidden || !Record.bHidden))
		{
			OutImprints.Add(Record);
		}
	}
	return true;
}

// Grant 校验流程：先验证全局 GrantId，再按种类检查其最小稳定内容；未裁图片格式和解锁收益不在这里伪造默认值。
ECatDomainCommandError UCatProfileSubsystem::ValidateGrant(const FCatProfileGrant& Grant)
{
	if (!Grant.GrantId.IsValid())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	switch (Grant.Kind)
	{
	case ECatProfileGrantKind::FishRecorded:
		return !Grant.FishDefinitionId.IsNone() && FMath::IsFinite(Grant.WeightKilograms) && Grant.WeightKilograms > 0.0
			? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	case ECatProfileGrantKind::FishSilhouette:
		return !Grant.FishDefinitionId.IsNone() ? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	case ECatProfileGrantKind::Imprint:
		return Grant.ImprintId.IsValid() && Grant.RunAlbumId.IsValid()
			? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	case ECatProfileGrantKind::Unlock:
		return !Grant.UnlockId.IsNone() ? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	case ECatProfileGrantKind::FishScooped:
		// 抄获轨只落一个计数，重量与条件不属于这条轨；这里因此不校验 WeightKilograms，只要求鱼种可定位。
		return !Grant.FishDefinitionId.IsNone() ? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	default:
		return ECatDomainCommandError::InvalidPayload;
	}
}

// 内容合并流程：按 Grant 类型只推进对应 SSOT；同一张图鉴页上「钓起」轨（状态、首次条件、个人最佳）与「抄获」轨（次
// 数）分别推进、互不覆盖，印记按 ID 去重，封面只接受明确 cover 标记，解锁只追加一次。
bool UCatProfileSubsystem::MergeGrantIntoProfile(const FCatProfileGrant& Grant)
{
	if (!CurrentProfile)
	{
		return false;
	}
	if (Grant.Kind == ECatProfileGrantKind::FishRecorded || Grant.Kind == ECatProfileGrantKind::FishSilhouette
		|| Grant.Kind == ECatProfileGrantKind::FishScooped)
	{
		FCatFishCollectionRecord* Record = CurrentProfile->FishCollection.FindByPredicate([&Grant](const FCatFishCollectionRecord& Existing)
		{
			return Existing.FishDefinitionId == Grant.FishDefinitionId;
		});
		if (!Record)
		{
			FCatFishCollectionRecord& NewRecord = CurrentProfile->FishCollection.AddDefaulted_GetRef();
			NewRecord.FishDefinitionId = Grant.FishDefinitionId;
			Record = &NewRecord;
		}
		if (Grant.Kind == ECatProfileGrantKind::FishScooped)
		{
			// 抄获轨到此为止：不推进 State、不参与个人最佳，也不计入钓起轨的 EncounterCount。
			// 只抄不钓的玩家因此可以有一条 ScoopedCount>0 但仍是 Unknown 的图鉴页，这是飞书两轨制的预期形态。
			++Record->ScoopedCount;
		}
		else
		{
			++Record->EncounterCount;
			if (Grant.Kind == ECatProfileGrantKind::FishRecorded)
			{
				if (Record->State != ECatFishCollectionState::Recorded)
				{
					Record->FirstCaptureCondition = Grant.CaptureCondition;
				}
				Record->State = ECatFishCollectionState::Recorded;
				Record->BestWeightKilograms = FMath::Max(Record->BestWeightKilograms, Grant.WeightKilograms);
			}
			else if (Record->State == ECatFishCollectionState::Unknown)
			{
				Record->State = ECatFishCollectionState::Silhouette;
			}
		}
	}
	else if (Grant.Kind == ECatProfileGrantKind::Imprint)
	{
		if (!CurrentProfile->Imprints.ContainsByPredicate([&Grant](const FCatLocalImprintRecord& Existing)
		{
			return Existing.ImprintId == Grant.ImprintId;
		}))
		{
			FCatLocalImprintRecord& Record = CurrentProfile->Imprints.AddDefaulted_GetRef();
			Record.ImprintId = Grant.ImprintId;
			Record.RunAlbumId = Grant.RunAlbumId;
			Record.bRunAlbumCover = Grant.bRunAlbumCover;
		}
		if (Grant.bRunAlbumCover)
		{
			CurrentProfile->RunAlbumCovers.Add(Grant.RunAlbumId, Grant.ImprintId);
		}
	}
	else if (Grant.Kind == ECatProfileGrantKind::Unlock)
	{
		CurrentProfile->UnlockIds.AddUnique(Grant.UnlockId);
	}
	return true;
}

// Pending 完成流程：定位精确 Journal、幂等合并并标记 Complete/Applied；第二次保存失败时立即从磁盘重载先前 Pending，确保内存不会错误允许 ACK。
FCatProfileApplyResult UCatProfileSubsystem::CompletePendingGrant(const FGuid GrantId)
{
	FCatProfileApplyResult Result;
	Result.GrantId = GrantId;
	Result.Error = ECatDomainCommandError::NotFound;
	if (!bPersistenceReady || !CurrentProfile)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	FCatPendingGrantJournalEntry* Entry = CurrentProfile->GrantJournal.FindByPredicate([GrantId](const FCatPendingGrantJournalEntry& Candidate)
	{
		return Candidate.Grant.GrantId == GrantId;
	});
	if (!Entry)
	{
		return Result;
	}
	if (!MergeGrantIntoProfile(Entry->Grant))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Entry->Stage = ECatGrantJournalStage::Complete;
	CurrentProfile->AppliedGrantIds.AddUnique(GrantId);
	if (!SaveCurrentProfile())
	{
		bPersistenceReady = ReloadDurableProfile();
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result.bApplied = true;
	Result.bAckAllowed = true;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatProfile, Log, TEXT("Event=profile_grant_durable GrantId=%s Kind=%s AckAllowed=true"),
		*GrantId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Entry->Grant.Kind));
	return Result;
}

// 保存流程：只把当前内存对象写入初始化时解析的精确槽位；参数缺失直接失败，调用方决定是否重载或关闭 ACK。
bool UCatProfileSubsystem::SaveCurrentProfile() const
{
	return CurrentProfile && !ResolvedSlotName.IsEmpty() && ResolvedUserIndex >= 0
		&& UGameplayStatics::SaveGameToSlot(CurrentProfile, ResolvedSlotName, ResolvedUserIndex);
}

// durable 重载流程：从同一槽位读取最后成功文件并验证类型；失败时清对象，成功时用磁盘 Pending 覆盖可能未落盘的内存合并。
bool UCatProfileSubsystem::ReloadDurableProfile()
{
	if (ResolvedSlotName.IsEmpty() || ResolvedUserIndex < 0)
	{
		CurrentProfile = nullptr;
		return false;
	}
	CurrentProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(ResolvedSlotName, ResolvedUserIndex));
	if (!CurrentProfile || CurrentProfile->SchemaVersion != UCatProfileSaveGame::CurrentSchemaVersion)
	{
		CurrentProfile = nullptr;
		return false;
	}
	return true;
}
