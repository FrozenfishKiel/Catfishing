#include "UI/Collection/CatCollectionModel.h"

#include "Inventory/CatInventorySettings.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
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

// 刷新流程：先确认账号与总表就绪，再按鱼目录建立卡片；只有成功捕获记录才开放名称和偏好。
// 已解锁鱼和偏好鱼饵使用统一显示名占位；未解锁项仍用图鉴的未知文案，不提前暴露内容。
// 鱼饵仅取倍率大于 1 的关联，窝料只读现有三轴需求；不计算水中混合、概率或推荐分数。
void UCatCollectionModel::Refresh()
{
	FCatCollectionViewState NewState;
	TArray<FCatFishCollectionRecord> Records;
	const UCatProfileSubsystem* Profile = BoundProfile.Get();
	const UCatInventorySettings* Items = GetDefault<UCatInventorySettings>();
	TArray<UCatInventoryItemDefinition*> Definitions;
	FString Error;
	NewState.bAvailable = Profile && Profile->GetFishCollectionSnapshot(Records)
		&& Items->GetItemDefinitions(Definitions, Error);
	NewState.TrackedItemId = NewState.bAvailable ? Profile->GetTrackedFish() : 0;
	NewState.SummaryText = FText::FromString(NewState.bAvailable ? TEXT("鱼图鉴") : TEXT("图鉴数据未就绪"));
	const FText Unknown = FText::FromString(TEXT("？？？"));
	TSet<int32> Seen;
	if (NewState.bAvailable)
	{
		for (const auto& Reference : GetDefault<UCatFishCatalogSettings>()->Definitions)
		{
			const UCatFishDefinition* Fish = Reference.LoadSynchronous();
			if (!Fish || Seen.Contains(Fish->ItemId) || Items->FindRuntimeDefinition(Fish->ItemId) != Fish) continue;
			Seen.Add(Fish->ItemId);
			FCatCollectionEntryView& Entry = NewState.Entries.AddDefaulted_GetRef();
			Entry.ItemId = Fish->ItemId;
			const auto* Record = Records.FindByPredicate([Fish](const auto& Value) { return Value.ItemId == Fish->ItemId; });
			Entry.bRecordedUnlocked = Record && Record->bRecordedUnlocked;
			Entry.Thumbnail = Fish->GetInventoryThumbnail();
			Entry.BestWeightKilograms = Record ? Record->BestWeightKilograms : 0.0;
			Entry.DisplayName = Entry.bRecordedUnlocked ? UCatInventoryItemDefinition::GetPlayerFacingName(Fish) : Unknown;
			Entry.BaitPreferenceText = Entry.ChumPreferenceText = Unknown;
			if (!Entry.bRecordedUnlocked) continue;
			// 推荐只按策划指定身份读图，不从权重或窝料轴推导；零或无效配置保留空格。
			if (const auto* Bait = Items->FindRuntimeDefinition(Fish->RecommendedBaitItemId)) Entry.RecommendedBaitThumbnail = Bait->GetInventoryThumbnail();
			if (const auto* Chum = Items->FindRuntimeDefinition(Fish->RecommendedChumItemId)) Entry.RecommendedChumThumbnail = Chum->GetInventoryThumbnail();
			TArray<FString> Baits;
			for (const auto& Weight : Fish->BaitWeightMultipliers)
				// 中性倍率是 1；只有提高选鱼权重的关联才属于偏好，不把中性或抑制项列入。
				if (Weight.Multiplier > 1.0)
					if (const auto* Bait = Items->FindRuntimeDefinition(Weight.BaitItemId)) Baits.AddUnique(UCatInventoryItemDefinition::GetPlayerFacingName(Bait).ToString());
			Entry.BaitPreferenceText = FText::FromString(Baits.IsEmpty() ? TEXT("未配置") : FString::Join(Baits, TEXT("、")));
			TArray<FString> Chum;
			if (Fish->ChumPreference.Fishy > 0.0) Chum.Add(TEXT("腥"));
			if (Fish->ChumPreference.Fragrant > 0.0) Chum.Add(TEXT("香"));
			if (Fish->ChumPreference.Fermented > 0.0) Chum.Add(TEXT("发酵"));
			Entry.ChumPreferenceText = FText::FromString(Chum.IsEmpty() ? TEXT("未配置") : FString::Join(Chum, TEXT("、")));
		}
		NewState.Entries.Sort([](const auto& A, const auto& B) { return A.ItemId < B.ItemId; });
	}
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// 状态读取流程：返回最近图鉴投影；调用方不能通过它取得 Profile 写口。
const FCatCollectionViewState& UCatCollectionModel::GetViewState() const
{
	return ViewState;
}

// 追踪提交流程：只允许当前有效投影里的已捕获鱼种；零为取消，持久化失败保持旧追踪。
bool UCatCollectionModel::SetTrackedFish(const int32 ItemId)
{
	UCatProfileSubsystem* Profile = BoundProfile.Get();
	const auto* Entry = ViewState.Entries.FindByPredicate([ItemId](const auto& Value) { return Value.ItemId == ItemId; });
	return ViewState.bAvailable && Profile && (ItemId == 0 || (Entry && Entry->bRecordedUnlocked))
		&& Profile->SetTrackedFish(ItemId);
}

// Profile 变化流程：统一重读完整图鉴快照，避免 UI 保存增量私有状态。
void UCatCollectionModel::HandleFishCollectionChanged()
{
	Refresh();
}
