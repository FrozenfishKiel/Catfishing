#include "Profile/CatProfileSubsystem.h"

#include "Logging/CatLog.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Data/CatFishDefinition.h"
#include "Inventory/CatInventorySettings.h"
#include "Kismet/GameplayStatics.h"
#include "Profile/CatProfileSaveGame.h"
#include "Profile/CatCollectionSaveGame.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/Package.h"

#if WITH_STEAMWORKS
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace
{
	// 只按既有授予类别区分存储归属；历史剪影仅供旧账本恢复，不再由咬钩产生。
	bool IsCollectionGrant(ECatProfileGrantKind Kind)
	{
		return Kind == ECatProfileGrantKind::FishRecorded || Kind == ECatProfileGrantKind::FishKnowledge || Kind == ECatProfileGrantKind::FishSilhouette;
	}
}

#include "Profile/CatProfileSettings.h"

// 初始化流程：先安装周期账号检查并尝试加载专用图鉴，再按设置和 LocalPlayer 索引加载本机 Profile。
// 旧本机档先备份并转换数字身份，失败拒绝写盘；仅重放非图鉴 Pending，旧图鉴记录和账本保留待确认归属，不自动导入账号。
void UCatProfileSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AccountTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::RefreshCollectionAccount), 1.0f);
	RefreshCollectionAccount(0.0f);
	if (!bCollectionReady) UE_LOG(LogCatProfile, Warning, TEXT("Event=collection_initially_unavailable IdentityResolved=%d Result=NoWritableCollection"), !CollectionAccountKey.IsEmpty());
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
		const int32 FoundSchemaVersion = CurrentProfile ? CurrentProfile->SchemaVersion : INDEX_NONE;
		FString MigrationError;
		if (FoundSchemaVersion == 2)
		{
			// 原始字节另存，备份成功且身份全部可解释后才升级内存；不把反序列化后的默认值当原档备份。
			TArray<uint8> OriginalBytes;
			const FString BackupSlot = ResolvedSlotName + TEXT("_BeforeNumericIds_v2");
			const bool bReadOriginal = UGameplayStatics::LoadDataFromSlot(OriginalBytes, ResolvedSlotName, ResolvedUserIndex);
			TArray<uint8> ExistingBackup;
			// 已有备份必须与这次原文件一致；不覆盖较早备份，也不在备份过期时升级当前文件。
			const bool bBackupReady = bReadOriginal && (UGameplayStatics::DoesSaveGameExist(BackupSlot, ResolvedUserIndex)
				? UGameplayStatics::LoadDataFromSlot(ExistingBackup, BackupSlot, ResolvedUserIndex) && ExistingBackup == OriginalBytes
				: UGameplayStatics::SaveDataToSlot(OriginalBytes, BackupSlot, ResolvedUserIndex));
			if (!bBackupReady) MigrationError = TEXT("OriginalUnavailableOrBackupConflict");
			if (!bBackupReady
				|| !UCatInventorySettings::MigrateLegacyItemReferences(CurrentProfile, MigrationError))
			{
				UE_LOG(LogCatProfile, Error, TEXT("Event=profile_item_migration_rejected Reason=%s"), *MigrationError);
				CurrentProfile = nullptr;
				return;
			}
			CurrentProfile->SchemaVersion = UCatProfileSaveGame::CurrentSchemaVersion;
			if (!SaveCurrentProfile()) { CurrentProfile = nullptr; return; }
		}
		else if (FoundSchemaVersion != UCatProfileSaveGame::CurrentSchemaVersion)
		{
			UE_LOG(LogCatProfile, Error, TEXT("Event=profile_load_rejected FoundSchemaVersion=%d ExpectedSchemaVersion=%d OriginalFilePreserved=1"),
				FoundSchemaVersion, UCatProfileSaveGame::CurrentSchemaVersion);
			CurrentProfile = nullptr;
			return;
		}
	}
	if (!CurrentProfile)
	{
		CurrentProfile = Cast<UCatProfileSaveGame>(UGameplayStatics::CreateSaveGameObject(UCatProfileSaveGame::StaticClass()));
		if (!CurrentProfile || !SaveCurrentProfile())
		{
			CurrentProfile = nullptr;
			return;
		}
	}
	bPersistenceReady = true;
	if (!CurrentProfile->FishCollection.IsEmpty())
		UE_LOG(LogCatProfile, Warning, TEXT("Event=legacy_local_collection_preserved RecordCount=%d Result=ManualOwnershipMigrationRequired"), CurrentProfile->FishCollection.Num());
	TArray<FGuid> PendingGrantIds;
	for (const FCatPendingGrantJournalEntry& Entry : CurrentProfile->GrantJournal)
	{
		if (!IsCollectionGrant(Entry.Grant.Kind) && Entry.Stage == ECatGrantJournalStage::Pending && Entry.Grant.GrantId.IsValid())
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

// 销毁流程：先关闭成像与图鉴广播并清 durable 对象引用、槽位和索引，再交还父类；这里不做隐式保存，避免把尚未 Complete 的内存变化提交为成功。
void UCatProfileSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(AccountTicker);
	AccountTicker.Reset();
	bCollectionReady = false;
	CurrentCollection = nullptr;
	CollectionAccountKey.Reset();
	CollectionSlotName.Reset();
	OnCapturePlanReceived.Clear();
	OnFishCollectionChanged.Clear();
	bPersistenceReady = false;
	CurrentProfile = nullptr;
	ResolvedSlotName.Reset();
	ResolvedUserIndex = INDEX_NONE;
	Super::Deinitialize();
}

// Grant 应用流程：先拒绝无效/不可写输入，再对已 durable Grant 幂等允许 ACK；新 Grant 先追加 Pending 并保存，只有该保存成功才进入合并与 Complete 保存。
FCatProfileApplyResult UCatProfileSubsystem::ApplyGrant(const FCatProfileGrant& Grant)
{
	FCatProfileApplyResult Result;
	Result.GrantId = Grant.GrantId;
	Result.Error = ValidateGrant(Grant);
	const bool bCollection = IsCollectionGrant(Grant.Kind);
	if (bCollection)
	{
		RefreshCollectionAccount(0.0f);
		if (CollectionAccountKey.IsNumeric() && Grant.RecipientStableNetId != CollectionAccountKey)
			Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	if (Result.Error != ECatDomainCommandError::None || (bCollection ? (!bCollectionReady || !CurrentCollection) : (!bPersistenceReady || !CurrentProfile)))
	{
		if (Result.Error == ECatDomainCommandError::None)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
		UE_LOG(LogCatProfile, Warning, TEXT("Event=profile_grant_rejected GrantId=%s Collection=%d Error=%s AckAllowed=0"),
			*Grant.GrantId.ToString(EGuidFormats::DigitsWithHyphens), bCollection, *UEnum::GetValueAsString(Result.Error));
		return Result;
	}
	auto& Applied = bCollection ? CurrentCollection->AppliedGrantIds : CurrentProfile->AppliedGrantIds;
	auto& Journal = bCollection ? CurrentCollection->GrantJournal : CurrentProfile->GrantJournal;
	if (Applied.Contains(Grant.GrantId))
	{
		Result.bAckAllowed = true;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (Journal.ContainsByPredicate([&Grant](const FCatPendingGrantJournalEntry& Entry)
	{
		return Entry.Grant.GrantId == Grant.GrantId;
	}))
	{
		return CompletePendingGrant(Grant.GrantId, bCollection);
	}
	FCatPendingGrantJournalEntry& Entry = Journal.AddDefaulted_GetRef();
	Entry.Grant = Grant;
	Entry.Grant.RecipientStableNetId.Reset();
	Entry.Stage = ECatGrantJournalStage::Pending;
	if (!SaveCurrentProfile(bCollection))
	{
		Journal.Pop();
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	return CompletePendingGrant(Grant.GrantId, bCollection);
}

// CapturePlan 接收流程：验证计划稳定键并读取外部桥 gate；只有两者成立才广播并返回已接管，拒绝时返回 false 供 owning Controller 把服务器计划收口为明确失败。
bool UCatProfileSubsystem::ReceiveCapturePlan(const FCatCapturePlan& Plan)
{
	const UCatProfileSettings* Settings = GetDefault<UCatProfileSettings>();
	if (!Settings || !Settings->IsExternalImprintBridgeReady() || !Plan.CapturePlanId.IsValid()
		|| !Plan.CandidateId.IsValid() || !Plan.RunId.IsValid() || !Plan.RunAlbumId.IsValid())
	{
		return false;
	}
	OnCapturePlanReceived.Broadcast(Plan);
	return true;
}

// 装备选择流程：先验证 Request、槽位和正式定义，暂存变更前值后写新选择并同步保存；保存失败恢复失效内存，绝不把局内耐久、数量或所有权带进 Profile。
FCatDomainCommandResult UCatProfileSubsystem::SetEquipmentSelection(const FGuid RequestId, const FName SlotId,
	const int32  ItemId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UCatEquipmentDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(ItemId);
	if (!bPersistenceReady || !CurrentProfile || !RequestId.IsValid() || SlotId.IsNone() || !Definition
		|| Definition->LoadoutSlotId != SlotId)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	const int32 Previous = CurrentProfile->EquipmentItemBySlot.FindRef(SlotId);
	CurrentProfile->EquipmentItemBySlot.Add(SlotId, ItemId);
	if (!SaveCurrentProfile())
	{
		if (Previous == 0)
		{
			CurrentProfile->EquipmentItemBySlot.Remove(SlotId);
		}
		else
		{
			CurrentProfile->EquipmentItemBySlot.Add(SlotId, Previous);
		}
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

// 装备选择读取流程：先清输出，再从当前 durable Profile 查精确槽位；它不加载定义或自动选择替代品。
bool UCatProfileSubsystem::TryGetEquipmentSelection(const FName SlotId, int32& OutItemId) const
{
	OutItemId = 0;
	if (!bPersistenceReady || !CurrentProfile)
	{
		return false;
	}
	if (const int32* Selected = CurrentProfile->EquipmentItemBySlot.Find(SlotId))
	{
		OutItemId = *Selected;
		return !(OutItemId == 0);
	}
	return false;
}

// 图鉴公开查询流程：先清输出，只在账号图鉴已就绪时复制 CurrentCollection 的记录；不读本机旧图鉴，相册、账本和装备不进入结果。
bool UCatProfileSubsystem::GetFishCollectionSnapshot(TArray<FCatFishCollectionRecord>& OutRecords) const
{
	OutRecords.Reset();
	if (!bCollectionReady || !CurrentCollection)
	{
		return false;
	}
	OutRecords = CurrentCollection->FishCollection;
	return true;
}

// 相册索引读取流程：只复制本人 durable 相册索引（ImprintId/RunAlbumId/封面位/隐藏位）；不复制图片字节或路径，也不提供别人的相册。
bool UCatProfileSubsystem::GetLocalImprintSnapshot(TArray<FCatLocalImprintRecord>& OutRecords) const
{
	OutRecords.Reset();
	if (!bPersistenceReady || !CurrentProfile)
	{
		return false;
	}
	OutRecords = CurrentProfile->Imprints;
	return true;
}

// 印记隐藏流程：定位本人本地索引并只改 bHidden；保存失败恢复变更前值，不发送服务器 RPC，也不删除图片或其他玩家记录。
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
		return Grant.ItemId > 0 && FMath::IsFinite(Grant.WeightKilograms) && Grant.WeightKilograms > 0.0
			? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	case ECatProfileGrantKind::FishSilhouette:
	case ECatProfileGrantKind::FishKnowledge:
		return Grant.ItemId > 0 ? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	case ECatProfileGrantKind::Imprint:
		return Grant.ImprintId.IsValid() && Grant.RunAlbumId.IsValid()
			? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	case ECatProfileGrantKind::Unlock:
		return !Grant.UnlockId.IsNone() ? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPayload;
	default:
		return ECatDomainCommandError::InvalidPayload;
	}
}

// 内容合并流程：按 Grant 类型只推进对应 SSOT；鱼图鉴按字段级解锁位单向升级并保留首次条件，印记按 ID 去重，封面只接受明确 cover 标记，解锁只追加一次。
bool UCatProfileSubsystem::MergeGrantIntoProfile(const FCatProfileGrant& Grant, bool& bOutFirstRecordedUnlock)
{
	bOutFirstRecordedUnlock = false;
	if (IsCollectionGrant(Grant.Kind) ? !CurrentCollection : !CurrentProfile)
	{
		return false;
	}
	if (Grant.Kind == ECatProfileGrantKind::FishRecorded || Grant.Kind == ECatProfileGrantKind::FishSilhouette
		|| Grant.Kind == ECatProfileGrantKind::FishKnowledge)
	{
		FCatFishCollectionRecord* Record = CurrentCollection->FishCollection.FindByPredicate([&Grant](const FCatFishCollectionRecord& Existing)
		{
			return Existing.ItemId == Grant.ItemId;
		});
		if (!Record)
		{
			FCatFishCollectionRecord& NewRecord = CurrentCollection->FishCollection.AddDefaulted_GetRef();
			NewRecord.ItemId = Grant.ItemId;
			Record = &NewRecord;
		}
		if (Grant.Kind == ECatProfileGrantKind::FishRecorded)
		{
			++Record->EncounterCount;
			// 首次成功捕获该鱼种时开放卡片；食用记录可能已存在，但不能代替成功捕获。
			// 已有知识与重量继续保留，只有捕获记录这一位控制名称和偏好的展示。
			bOutFirstRecordedUnlock = !Record->bRecordedUnlocked;
			if (!Record->bRecordedUnlocked)
			{
				// 首次条件只在第一次收集时冻结；此后破纪录只刷新最佳重量，不覆盖首次条件（图鉴 §3.1.4:126）。
				Record->FirstCaptureCondition = Grant.CaptureCondition;
			}
			// 保留旧记录的字段兼容性；本版卡片只读取 bRecordedUnlocked，不通过剪影位泄漏偏好。
			Record->bSilhouetteUnlocked = true;
			Record->bRecordedUnlocked = true;
			Record->BestWeightKilograms = FMath::Max(Record->BestWeightKilograms, Grant.WeightKilograms);
		}
		else if (Grant.Kind == ECatProfileGrantKind::FishSilhouette)
		{
			++Record->EncounterCount;
			Record->bSilhouetteUnlocked = true;
		}
		else
		{
			// 知识层不经过「交手」，吃掉别人钓的鱼也算；因此不递增 EncounterCount，也不要求先有收集层。
			Record->bKnowledgeUnlocked = true;
		}
		// 保留兼容状态投影：只吃过且无剪影记录时仍为 Unknown；本版鱼卡始终只凭捕获位开放名称和偏好。
		Record->State = Record->bRecordedUnlocked
			? (Record->bKnowledgeUnlocked ? ECatFishCollectionState::Knowledge : ECatFishCollectionState::Recorded)
			: (Record->bSilhouetteUnlocked ? ECatFishCollectionState::Silhouette : ECatFishCollectionState::Unknown);
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

// Pending 完成流程：定位精确 Journal，记住授予种类后幂等合并并标记 Complete/Applied；第二次保存失败立即回载磁盘 Pending，成功时才允许 ACK，并仅为图鉴类 Grant 广播新快照可读。
FCatProfileApplyResult UCatProfileSubsystem::CompletePendingGrant(const FGuid GrantId, const bool bCollection)
{
	FCatProfileApplyResult Result;
	Result.GrantId = GrantId;
	Result.Error = ECatDomainCommandError::NotFound;
	if (bCollection ? (!bCollectionReady || !CurrentCollection) : (!bPersistenceReady || !CurrentProfile))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	auto& Journal = bCollection ? CurrentCollection->GrantJournal : CurrentProfile->GrantJournal;
	auto& Applied = bCollection ? CurrentCollection->AppliedGrantIds : CurrentProfile->AppliedGrantIds;
	FCatPendingGrantJournalEntry* Entry = Journal.FindByPredicate([GrantId](const FCatPendingGrantJournalEntry& Candidate)
	{
		return Candidate.Grant.GrantId == GrantId;
	});
	if (!Entry)
	{
		return Result;
	}
	const ECatProfileGrantKind CompletedKind = Entry->Grant.Kind;
	const int32  CompletedItemId = Entry->Grant.ItemId;
	const double CompletedWeightKilograms = Entry->Grant.WeightKilograms;
	bool bFirstRecordedUnlock = false;
	if (!MergeGrantIntoProfile(Entry->Grant, bFirstRecordedUnlock))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Entry->Stage = ECatGrantJournalStage::Complete;
	Applied.AddUnique(GrantId);
	if (!SaveCurrentProfile(bCollection))
	{
		const bool bRestored = ReloadDurableProfile(bCollection);
		if (bCollection) bCollectionReady = bRestored; else bPersistenceReady = bRestored;
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result.bApplied = true;
	Result.bAckAllowed = true;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatProfile, Log, TEXT("Event=profile_grant_durable GrantId=%s Kind=%s AckAllowed=true"),
		*GrantId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Entry->Grant.Kind));
	if (CompletedKind == ECatProfileGrantKind::FishRecorded || CompletedKind == ECatProfileGrantKind::FishSilhouette
		|| CompletedKind == ECatProfileGrantKind::FishKnowledge)
	{
		OnFishCollectionChanged.Broadcast();
	}
	// 首解锁特写只在档案确实写上之后才弹：上面那次 SaveCurrentProfile 失败会提前 return，不会走到这里。
	if (bFirstRecordedUnlock && !(CompletedItemId == 0))
	{
		OnFishSpeciesFirstRecorded.Broadcast(CompletedItemId, CompletedWeightKilograms);
	}
	return Result;
}

// 保存流程：图鉴分支先校验账号载荷，再写账号专用缓存；其余数据写初始化解析的本机 Profile 槽。
// 参数或校验失败返回 false，由调用者回滚或关闭 ACK；SaveGame 成功仅代表本机写入，Steam 云同步由外部平台完成。
bool UCatProfileSubsystem::SaveCurrentProfile(const bool bCollection) const
{
	if (bCollection)
	{
		const bool bSaved = CurrentCollection && ValidateCollectionSave() && !CollectionSlotName.IsEmpty()
			&& UGameplayStatics::SaveGameToSlot(CurrentCollection, CollectionSlotName, 0);
		if (!bSaved) UE_LOG(LogCatProfile, Warning, TEXT("Event=collection_cache_write_failed Result=NoDurableReceipt"));
		return bSaved;
	}
	return CurrentProfile && !ResolvedSlotName.IsEmpty() && ResolvedUserIndex >= 0
		&& UGameplayStatics::SaveGameToSlot(CurrentProfile, ResolvedSlotName, ResolvedUserIndex);
}

// 重载流程：按存储归属读取同一槽位，图鉴完整校验账号与载荷，本机 Profile 校验类型和版本。
// 返回结果交给调用者更新就绪标记；图鉴校验失败可能仍持有无效对象，必须通过就绪标记禁止继续消费。
bool UCatProfileSubsystem::ReloadDurableProfile(const bool bCollection)
{
	if (bCollection)
	{
		CurrentCollection = Cast<UCatCollectionSaveGame>(UGameplayStatics::LoadGameFromSlot(CollectionSlotName, 0));
		return ValidateCollectionSave();
	}
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

// 追踪读取流程：仅在档案就绪时返回已保存编号，未就绪不暴露旧账号残留。
int32 UCatProfileSubsystem::GetTrackedFish() const
{
	return bCollectionReady && CurrentCollection ? CurrentCollection->TrackedItemId : 0;
}

// 追踪写入流程：先核对成功捕获与总表，再暂存旧值并写盘；失败回滚，成功才通知两个同源界面刷新。
bool UCatProfileSubsystem::SetTrackedFish(const int32 ItemId)
{
	RefreshCollectionAccount(0.0f);
	if (!bCollectionReady || !CurrentCollection || ItemId < 0) return false;
	if (ItemId != 0)
	{
		const auto* Record = CurrentCollection->FishCollection.FindByPredicate([ItemId](const auto& E) { return E.ItemId == ItemId; });
		if (!Record || !Record->bRecordedUnlocked || !GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatFishDefinition>(ItemId)) return false;
	}
	const int32 Previous = CurrentCollection->TrackedItemId;
	if (Previous == ItemId) return true;
	CurrentCollection->TrackedItemId = ItemId;
	if (!SaveCurrentProfile(true)) { CurrentCollection->TrackedItemId = Previous; return false; }
	UE_LOG(LogCatProfile, Log, TEXT("Event=collection_tracking_saved ItemId=%d Result=LocalCacheWritten"), ItemId);
	OnFishCollectionChanged.Broadcast();
	return true;
}

// 账号检查流程：编辑器始终使用开发目录；正式构建只接受 Steam 已提供的个人账号，不用 ControllerId 冒充账号。
// 离线 Steam 只要仍能提供有效账号即可读取本机缓存。身份变化先关闭旧档案，再验证新文件；失败不覆写或重试空档。
bool UCatProfileSubsystem::RefreshCollectionAccount(const float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (!GetDefault<UCatProfileSettings>()->IsPersistenceReady()) return true;
	FString Account;
	bool bDevelopment = false;
#if WITH_EDITOR
	if (GIsEditor)
	{
		const ULocalPlayer* Player = GetLocalPlayer();
		Account = CollectionAccountKey.StartsWith(TEXT("Editor_")) ? CollectionAccountKey
			: FString::Printf(TEXT("Editor_%d_%08x"), Player ? FMath::Max(0, Player->GetControllerId()) : 0,
				GetTypeHash(GetDefault<UCatProfileSettings>()->SaveSlotBaseName));
		// 单进程 PIE 的各客户端 ControllerId 常常同为零，必须再用实例号隔离开发档，避免并发写同一文件。
		const UWorld* World = Player ? Player->GetWorld() : nullptr;
		if (CollectionAccountKey.IsEmpty() && World && World->WorldType == EWorldType::PIE)
			Account += FString::Printf(TEXT("_PIE%d"), World->GetPackage()->GetPIEInstanceID());
		bDevelopment = true;
	}
#endif
#if WITH_STEAMWORKS
	if (!bDevelopment && SteamAPI_IsSteamRunning() && SteamUser() && SteamUser()->GetSteamID().IsValid()
		&& SteamUser()->GetSteamID().BIndividualAccount())
		Account = FString::Printf(TEXT("%llu"), SteamUser()->GetSteamID().ConvertToUint64());
#endif
	// 一个 Steam 客户端只有一个账号；次本地玩家不能另建同一文件的写入者。
	const UGameInstance* Instance = GetLocalPlayer() ? GetLocalPlayer()->GetGameInstance() : nullptr;
	if (!bDevelopment && Instance && Instance->GetFirstGamePlayer() != GetLocalPlayer()) Account.Reset();
	if (Account == CollectionAccountKey) return true;
	bCollectionReady = false;
	CurrentCollection = nullptr;
	CollectionAccountKey = Account;
	CollectionSlotName.Reset();
	OnFishCollectionChanged.Broadcast();
	if (Account.IsEmpty()) return true;
	CollectionSlotName = FString::Printf(TEXT("CatCollection/%s/%s/Collection_v1"), bDevelopment ? TEXT("Development") : TEXT("Steam"), *Account);
	if (UGameplayStatics::DoesSaveGameExist(CollectionSlotName, 0))
	{
		CurrentCollection = Cast<UCatCollectionSaveGame>(UGameplayStatics::LoadGameFromSlot(CollectionSlotName, 0));
		if (!ValidateCollectionSave())
		{
			CurrentCollection = nullptr;
			UE_LOG(LogCatProfile, Error, TEXT("Event=collection_account_load_rejected Reason=InvalidOrUnknownData OriginalFilePreserved=1"));
			return true;
		}
	}
	else
	{
		CurrentCollection = NewObject<UCatCollectionSaveGame>(this);
		CurrentCollection->AccountKey = Account;
		if (!SaveCurrentProfile(true))
		{
			CurrentCollection = nullptr;
			UE_LOG(LogCatProfile, Error, TEXT("Event=collection_account_load_rejected Reason=InitialCacheWriteFailed"));
			return true;
		}
	}
	bCollectionReady = true;
	TArray<FGuid> Pending;
	for (const auto& Entry : CurrentCollection->GrantJournal)
		if (Entry.Stage == ECatGrantJournalStage::Pending) Pending.Add(Entry.Grant.GrantId);
	for (const FGuid Id : Pending)
		if (!CompletePendingGrant(Id, true).bAckAllowed) { bCollectionReady = false; break; }
	UE_LOG(LogCatProfile, Log, TEXT("Event=collection_account_loaded Development=%d Ready=%d CloudSync=NotVerified"), bDevelopment, bCollectionReady);
	OnFishCollectionChanged.Broadcast();
	return true;
}

// 载荷检查流程：账号和格式必须匹配，记录编号必须存在且不重复，重量与计数合法；追踪必须指向已捕获鱼。
// 账本只接受图鉴授予，不接纳相册或装备数据，防止专用云文件承载无关内容。
bool UCatProfileSubsystem::ValidateCollectionSave() const
{
	if (!CurrentCollection || CurrentCollection->SchemaVersion != 1 || CollectionAccountKey.IsEmpty()
		|| CurrentCollection->AccountKey != CollectionAccountKey) return false;
	TSet<int32> Seen;
	for (const auto& Record : CurrentCollection->FishCollection)
	{
		if (Record.ItemId <= 0 || Seen.Contains(Record.ItemId) || !FMath::IsFinite(Record.BestWeightKilograms)
			|| Record.BestWeightKilograms < 0 || Record.EncounterCount < 0
			|| !GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatFishDefinition>(Record.ItemId)) return false;
		Seen.Add(Record.ItemId);
	}
	if (CurrentCollection->TrackedItemId != 0 && !CurrentCollection->FishCollection.ContainsByPredicate([this](const auto& Record)
		{ return Record.ItemId == CurrentCollection->TrackedItemId && Record.bRecordedUnlocked; })) return false;
	TSet<FGuid> Grants;
	for (const auto& Entry : CurrentCollection->GrantJournal)
	{
		if (!Entry.Grant.GrantId.IsValid() || Grants.Contains(Entry.Grant.GrantId) || !IsCollectionGrant(Entry.Grant.Kind)
			|| ValidateGrant(Entry.Grant) != ECatDomainCommandError::None
			|| (Entry.Stage != ECatGrantJournalStage::Pending && Entry.Stage != ECatGrantJournalStage::Complete)
			|| (Entry.Stage == ECatGrantJournalStage::Complete) != CurrentCollection->AppliedGrantIds.Contains(Entry.Grant.GrantId)
			|| Entry.Grant.ItemId <= 0 || !GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatFishDefinition>(Entry.Grant.ItemId)) return false;
		Grants.Add(Entry.Grant.GrantId);
	}
	// 去重列表必须与完成账本一一对应，孤立或重复编号会掩盖丢失的授予，不能继续覆盖原档。
	TSet<FGuid> Applied;
	for (const FGuid Id : CurrentCollection->AppliedGrantIds)
	{
		if (!Grants.Contains(Id) || Applied.Contains(Id)) return false;
		Applied.Add(Id);
	}
	return true;
}
