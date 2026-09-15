#include "UI/Collection/CatCollectionModel.h"

#include "Collection/CatFishCollectionLayers.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Profile/CatProfileSubsystem.h"

namespace
{
	/** 全库统一用词：已开页里没到条件的字段一律写「待解锁」，不显示灰掉的假数值（图鉴 §4 软性:171）。 */
	FText PendingUnlockText()
	{
		return NSLOCTEXT("Catfishing", "CollectionPendingUnlock", "待解锁");
	}

	/** 首次遇上的条件回显：水域／时段／天气三轴（图鉴 §3.1.5:132）。任一轴没冻结到就写「未记录」，不编一个。 */
	FText FormatFirstCondition(const FCatCaptureConditionSnapshot& Condition)
	{
		const auto Axis = [](const FName Value)
		{
			return Value.IsNone() ? FText::FromString(TEXT("未记录")) : FText::FromName(Value);
		};
		return FText::Format(NSLOCTEXT("Catfishing", "CollectionFirstCondition", "{0} · {1} · {2}"),
			Axis(Condition.RegionId), Axis(Condition.TimeOfDayId), Axis(Condition.WeatherId));
	}
}

// 绑定流程：从 LocalPlayer 取得 Profile 子系统并订阅图鉴变化；失败时不留下半套订阅。
bool UCatCollectionModel::Bind(ULocalPlayer* InLocalPlayer)
{
	Unbind();
	UCatProfileSubsystem* Profile = InLocalPlayer ? InLocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	if (!Profile)
	{
		return false;
	}
	BoundProfile = Profile;
	FishCollectionChangedHandle = Profile->OnFishCollectionChanged.AddUObject(
		this, &ThisClass::HandleFishCollectionChanged);
	Refresh();
	return true;
}

// 解绑流程：从原 Profile 移除变化订阅并清空投影，避免跨 LocalPlayer 显示失效图鉴。
void UCatCollectionModel::Unbind()
{
	if (UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		Profile->OnFishCollectionChanged.Remove(FishCollectionChangedHandle);
	}
	FishCollectionChangedHandle.Reset();
	BoundProfile.Reset();
	ViewState = FCatCollectionViewState();
}

// 刷新流程：以正式鱼目录为骨架铺满整本图鉴，再用 Profile durable 快照逐条覆盖解锁位
// （图鉴 §3.1.5:130「开局满图都是影，你知道湖里有多少种，别的什么都不知道」）。
// 没解锁的字段写「待解锁」而不是灰掉的 0.00kg；不可食用的鱼连吃鱼效果这一栏都不存在（§3.1.4:122）。
// Journal、解锁清单和实物鱼都不进入 Collection UI；相册只带索引与隐藏位，不带图片路径。
void UCatCollectionModel::Refresh()
{
	FCatCollectionViewState NewState;
	TArray<FCatFishCollectionRecord> Records;
	TArray<FCatLocalImprintRecord> ImprintRecords;
	if (const UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		NewState.bAvailable = Profile->GetFishCollectionSnapshot(Records);
		Profile->GetLocalImprintSnapshot(ImprintRecords);
	}

	TMap<FName, const FCatFishCollectionRecord*> RecordsByFish;
	RecordsByFish.Reserve(Records.Num());
	for (const FCatFishCollectionRecord& Record : Records)
	{
		RecordsByFish.Add(Record.FishDefinitionId, &Record);
	}

	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	TSet<FName> CatalogFishIds;
	const auto AppendEntry = [&NewState](const UCatFishDefinition* Definition, const FName FishDefinitionId,
		const FCatFishCollectionRecord* Record)
	{
		FCatCollectionEntryView Entry;
		Entry.FishDefinitionId = FishDefinitionId;
		Entry.bHasKnowledgeLayer = CatFishCollectionLayers::HasKnowledgeLayer(Definition);
		if (Record)
		{
			Entry.State = Record->State;
			Entry.bSilhouetteUnlocked = Record->bSilhouetteUnlocked;
			Entry.bRecordedUnlocked = Record->bRecordedUnlocked;
			Entry.bKnowledgeUnlocked = Record->bKnowledgeUnlocked;
			Entry.BestWeightKilograms = Record->BestWeightKilograms;
			Entry.EncounterCount = Record->EncounterCount;
		}
		// 纯黑影不给名字：收集层解锁之后才写鱼名（图鉴 §3.1.5:130-132）。
		Entry.DisplayName = Entry.bRecordedUnlocked && Definition
			? Definition->GetInventoryDisplayName() : FText::GetEmpty();
		Entry.BestWeightText = Entry.bRecordedUnlocked
			? FText::AsNumber(Entry.BestWeightKilograms) : PendingUnlockText();
		Entry.FirstConditionText = Entry.bRecordedUnlocked && Record
			? FormatFirstCondition(Record->FirstCaptureCondition) : PendingUnlockText();
		// 没有知识层的鱼这一栏整条不存在，连「待解锁」都不占位。
		Entry.KnowledgeText = !Entry.bHasKnowledgeLayer
			? FText::GetEmpty()
			: (Entry.bKnowledgeUnlocked
				? NSLOCTEXT("Catfishing", "CollectionKnowledgeUnlocked", "吃鱼效果已解锁")
				: PendingUnlockText());

		const FString NameValue = Entry.bRecordedUnlocked
			? Entry.DisplayName.ToString() : TEXT("？？？");
		Entry.DisplayText = FText::FromString(FString::Printf(TEXT("%s | %s | 最佳 %s | 首次 %s | 吃 %s | 交手 %d"),
			*NameValue, *UEnum::GetValueAsString(Entry.State), *Entry.BestWeightText.ToString(),
			*Entry.FirstConditionText.ToString(),
			Entry.bHasKnowledgeLayer ? *Entry.KnowledgeText.ToString() : TEXT("—"),
			Entry.EncounterCount));
		NewState.Entries.Add(MoveTemp(Entry));
	};

	if (Catalog)
	{
		NewState.Entries.Reserve(Catalog->Definitions.Num());
		for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Catalog->Definitions)
		{
			const UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
			if (!Definition || !Definition->IsRuntimeDefinitionReady()
				|| CatalogFishIds.Contains(Definition->FishDefinitionId))
			{
				continue;
			}
			CatalogFishIds.Add(Definition->FishDefinitionId);
			AppendEntry(Definition, Definition->FishDefinitionId,
				RecordsByFish.FindRef(Definition->FishDefinitionId));
		}
	}
	// 档案里有、目录里已经没有的鱼仍然显示：玩家挣来的那一页不能因为策划下线一条鱼就从图鉴上消失。
	for (const FCatFishCollectionRecord& Record : Records)
	{
		if (!CatalogFishIds.Contains(Record.FishDefinitionId))
		{
			AppendEntry(nullptr, Record.FishDefinitionId, &Record);
		}
	}

	NewState.Imprints.Reserve(ImprintRecords.Num());
	for (const FCatLocalImprintRecord& Record : ImprintRecords)
	{
		FCatImprintAlbumEntryView Entry;
		Entry.ImprintId = Record.ImprintId;
		Entry.RunAlbumId = Record.RunAlbumId;
		Entry.bRunAlbumCover = Record.bRunAlbumCover;
		Entry.bHidden = Record.bHidden;
		NewState.Imprints.Add(MoveTemp(Entry));
	}

	// 墓碑（2026-09-14，T43）：删除“已收集数/总数、相册 N 张”完成度摘要；页和个人最佳仍保留。
	// Knowledge/Design/GDD 系统分册/印记.md:45 禁止印记数量/完成度，图鉴.md:19 定位为认识记录而非收集进度条。
	NewState.SummaryText = NewState.bAvailable
		? NSLOCTEXT("Catfishing", "CollectionKnowledgeSummary", "图鉴 · 认识的鱼与个人记录")
		: FText::FromString(TEXT("图鉴：本地记录未就绪"));
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// 状态读取流程：返回最近图鉴投影；调用方不能通过它取得 Profile 写口。
const FCatCollectionViewState& UCatCollectionModel::GetViewState() const
{
	return ViewState;
}

// 印记隐藏流程：Model 是本页面唯一持有 Profile 引用的地方，所以一键隐藏从这里转交唯一 durable 写口；
// 成功后立刻重读一次投影，页面上的隐藏标记不等下一次 Grant 落盘才更新。
bool UCatCollectionModel::SetImprintHidden(const FGuid ImprintId, const bool bHidden)
{
	UCatProfileSubsystem* Profile = BoundProfile.Get();
	if (!Profile || !ImprintId.IsValid())
	{
		return false;
	}
	const FCatDomainCommandResult Result = Profile->SetImprintHidden(FGuid::NewGuid(), ImprintId, bHidden);
	if (Result.bCommitted)
	{
		Refresh();
	}
	return Result.bCommitted;
}

// Profile 变化流程：统一重读完整图鉴快照，避免 UI 保存增量私有状态。
void UCatCollectionModel::HandleFishCollectionChanged()
{
	Refresh();
}
