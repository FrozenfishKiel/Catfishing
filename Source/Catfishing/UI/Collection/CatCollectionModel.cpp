#include "UI/Collection/CatCollectionModel.h"

#include "Engine/LocalPlayer.h"
#include "Profile/CatProfileSubsystem.h"

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

// 解绑流程：从原 Profile 移除变化订阅并清空投影，避免跨 LocalPlayer 显示旧图鉴。
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

// 刷新流程：只从 Profile durable 快照复制图鉴记录；Journal、隐藏相册和实物鱼都不进入 Collection UI。
void UCatCollectionModel::Refresh()
{
	FCatCollectionViewState NewState;
	TArray<FCatFishCollectionRecord> Records;
	if (const UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		NewState.bAvailable = Profile->GetFishCollectionSnapshot(Records);
	}
	NewState.Entries.Reserve(Records.Num());
	for (const FCatFishCollectionRecord& Record : Records)
	{
		FCatCollectionEntryView Entry;
		Entry.FishDefinitionId = Record.FishDefinitionId;
		Entry.State = Record.State;
		Entry.BestWeightKilograms = Record.BestWeightKilograms;
		Entry.EncounterCount = Record.EncounterCount;
		Entry.DisplayText = FText::FromString(FString::Printf(TEXT("%s | %s | 最佳 %.2fkg | 交手 %d"),
			*Record.FishDefinitionId.ToString(),
			*UEnum::GetValueAsString(Record.State),
			Record.BestWeightKilograms,
			Record.EncounterCount));
		NewState.Entries.Add(Entry);
	}
	NewState.SummaryText = NewState.bAvailable
		? FText::FromString(FString::Printf(TEXT("图鉴：%d 条记录"), NewState.Entries.Num()))
		: FText::FromString(TEXT("图鉴：本地记录未就绪"));
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// 状态读取流程：返回最近图鉴投影；调用方不能通过它取得 Profile 写口。
const FCatCollectionViewState& UCatCollectionModel::GetViewState() const
{
	return ViewState;
}

// Profile 变化流程：统一重读完整图鉴快照，避免 UI 保存增量私有状态。
void UCatCollectionModel::HandleFishCollectionChanged()
{
	Refresh();
}
