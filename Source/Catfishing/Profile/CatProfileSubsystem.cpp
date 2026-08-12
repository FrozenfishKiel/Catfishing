#include "Profile/CatProfileSubsystem.h"

#include "Logging/CatLog.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Profile/CatProfileSaveGame.h"
#include "Profile/CatProfileSettings.h"

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

// Grant 应用流程：先拒绝无效/不可写输入，再对已 durable Grant 幂等允许 ACK；新 Grant 先追加 Pending 并保存，只有该保存成功才进入合并与 Complete 保存。
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
	if (CurrentProfile->AppliedGrantIds.Contains(Grant.GrantId))
	{
		Result.bAckAllowed = true;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (CurrentProfile->GrantJournal.ContainsByPredicate([&Grant](const FCatPendingGrantJournalEntry& Entry)
	{
		return Entry.Grant.GrantId == Grant.GrantId;
	}))
	{
		return CompletePendingGrant(Grant.GrantId);
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

// 装备选择流程：先验证 Request/槽位/正式定义及解锁资格，暂存旧值后写新选择并同步保存；保存失败恢复旧内存，绝不把局内耐久或数量带进 Profile。
FCatDomainCommandResult UCatProfileSubsystem::SetEquipmentSelection(const FGuid RequestId, const FName SlotId,
	const FName EquipmentDefinitionId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(EquipmentDefinitionId);
	if (!bPersistenceReady || !CurrentProfile || !RequestId.IsValid() || SlotId.IsNone() || !Definition
		|| Definition->LoadoutSlotId != SlotId
		|| (!Definition->RequiredUnlockId.IsNone() && !CurrentProfile->UnlockIds.Contains(Definition->RequiredUnlockId)))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	const FName Previous = CurrentProfile->EquipmentSelectionBySlot.FindRef(SlotId);
	CurrentProfile->EquipmentSelectionBySlot.Add(SlotId, EquipmentDefinitionId);
	if (!SaveCurrentProfile())
	{
		if (Previous.IsNone())
		{
			CurrentProfile->EquipmentSelectionBySlot.Remove(SlotId);
		}
		else
		{
			CurrentProfile->EquipmentSelectionBySlot.Add(SlotId, Previous);
		}
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Result;
}

// 装备选择读取流程：先清输出，再从当前 durable Profile 查精确槽位；它不加载定义或自动选择替代品。
bool UCatProfileSubsystem::TryGetEquipmentSelection(const FName SlotId, FName& OutEquipmentDefinitionId) const
{
	OutEquipmentDefinitionId = NAME_None;
	if (!bPersistenceReady || !CurrentProfile)
	{
		return false;
	}
	if (const FName* Selected = CurrentProfile->EquipmentSelectionBySlot.Find(SlotId))
	{
		OutEquipmentDefinitionId = *Selected;
		return !OutEquipmentDefinitionId.IsNone();
	}
	return false;
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
	default:
		return ECatDomainCommandError::InvalidPayload;
	}
}

// 内容合并流程：按 Grant 类型只推进对应 SSOT；鱼图鉴单向升级并保留首次条件，印记按 ID 去重，封面只接受明确 cover 标记，解锁只追加一次。
bool UCatProfileSubsystem::MergeGrantIntoProfile(const FCatProfileGrant& Grant)
{
	if (!CurrentProfile)
	{
		return false;
	}
	if (Grant.Kind == ECatProfileGrantKind::FishRecorded || Grant.Kind == ECatProfileGrantKind::FishSilhouette)
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
