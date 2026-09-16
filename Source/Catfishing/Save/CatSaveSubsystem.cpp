#include "Save/CatSaveSubsystem.h"
#include "Collection/CatRunFishCollectionComponent.h"
#include "Framework/Game/CatfishingGameState.h"

#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"

#include "Async/Async.h"
#include "Camp/CatCampInventoryActor.h"
#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "FishContainers/CatFishContainerService.h"
#include "FishContainers/CatFishTankActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/CatFishContainerSettings.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatFishGuardInventoryItemInstance.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/CatLog.h"
#include "Run/CatRunSettings.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	/** 世界槽文件名前缀；后面只拼稳定 SlotId，形成 UE SaveGame 的唯一槽名。 */
	constexpr const TCHAR* RunSlotFilePrefix = TEXT("CatRun_");
	/** UE SaveGame 本地文件扩展名；扫描目录时只接受这一类正式槽文件。 */
	constexpr const TCHAR* SaveGameFileExtension = TEXT(".sav");

	// Windows 本地槽预检流程：只读取 UE 文件标记；本项目 v5/v6/v7 都是现代 GVAS 格式，不接受引擎的无标记旧格式回退。
	// UE 会把任意无效文件头当作旧版类名解析，可能触发 FName 长度断言；这里在 UObject 加载前拒绝这类文件。
	bool HasRunSaveFileHeader(const FString& SlotName)
	{
		const FString FilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), SlotName + SaveGameFileExtension);
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath));
		if (!Reader || Reader->TotalSize() < 8) return false;
		uint8 Magic[4] = {};
		Reader->Serialize(Magic, sizeof(Magic));
		return !Reader->IsError() && Magic[0] == 'G' && Magic[1] == 'V' && Magic[2] == 'A' && Magic[3] == 'S';
	}

	// 槽文件名识别流程：只接受 CatRun_<32位GUID>.sav 对应的基名；其他文件不会成为当前目录项。
	bool TryParseRunSlotIdFromBaseName(const FString& BaseName, FName& OutSlotId)
	{
		OutSlotId = NAME_None;
		if (!BaseName.StartsWith(RunSlotFilePrefix))
		{
			return false;
		}
		const FString SlotIdText = BaseName.Mid(FCString::Strlen(RunSlotFilePrefix));
		if (SlotIdText.Contains(TEXT(".")))
		{
			return false;
		}
		FGuid Parsed;
		if (!FGuid::ParseExact(SlotIdText, EGuidFormats::Digits, Parsed) || !Parsed.IsValid())
		{
			return false;
		}
		OutSlotId = FName(*SlotIdText);
		return true;
	}

	// 槽 ID 格式判断流程：只接受 32 位十六进制 GUID；Digits 格式含 A-F，不能误用纯十进制检查拒绝新建槽。
	bool IsValidSlotId(const FName SlotId)
	{
		FGuid Parsed;
		return FGuid::ParseExact(SlotId.ToString(), EGuidFormats::Digits, Parsed) && Parsed.IsValid();
	}

	// 存档摘要构造流程：只复制 Run 文件自身公开的显示字段，不把这些字段回写到 GameMode 或当作可恢复玩法真相。
	FCatSaveSlotSummary MakeSummary(const UCatRunSaveGame& SaveGame)
	{
		FCatSaveSlotSummary Summary;
		Summary.SlotId = SaveGame.SlotId;
		Summary.DisplayName = SaveGame.DisplayName.TrimStartAndEnd();
		Summary.LastSavedAt = SaveGame.LastSavedAt;
		Summary.DayIndex = SaveGame.DayIndex;
		Summary.LocationName = SaveGame.LocationName;
		Summary.PlayedDurationSeconds = SaveGame.PlayedDurationSeconds;
		Summary.LastOfferingPoints = SaveGame.LastOfferingPoints;
		Summary.DailyOfferingTarget = SaveGame.DailyOfferingTarget;
		Summary.WorldProgress = SaveGame.WorldProgress;
		Summary.LastWorldProgressDelta = SaveGame.LastWorldProgressDelta;
		Summary.bRunCompleted = SaveGame.bRunCompleted;
		Summary.TankOfferingPoints = SaveGame.TankOfferingPoints;
		return Summary;
	}

	// 玩家钓具选择导出流程：只复制 Equipment 交出的选择和鱼竿摘要；随身库存由玩家运行状态单独保存。
	FCatSavedEquipmentLoadout ToSavedEquipment(const FCatEquipmentLoadoutSnapshot& Snapshot)
	{
		FCatSavedEquipmentLoadout Saved;
		Saved.Revision = Snapshot.Revision;
		Saved.RodItemId = Snapshot.RodItemId;
		Saved.RodItemInstanceId = Snapshot.RodItemInstanceId;
		Saved.BaitItemId = Snapshot.BaitItemId;
		Saved.BaitItemInstanceId = Snapshot.BaitItemInstanceId;
		Saved.FloatItemId = Snapshot.FloatItemId;
		Saved.FloatItemInstanceId = Snapshot.FloatItemInstanceId;
		Saved.ScoopNetItemId = Snapshot.ScoopNetItemId;
		Saved.ScoopNetItemInstanceId = Snapshot.ScoopNetItemInstanceId;
		Saved.RodSkinDefinitionId = Snapshot.RodSkinDefinitionId;
		Saved.RodDurability = Snapshot.RodDurability;
		Saved.bRodBroken = Snapshot.bRodBroken;
		return Saved;
	}

	// 玩家钓具选择恢复输入转换流程：只重建 Equipment 选择快照；随身库存由 InventoryComponent 单独恢复。
	FCatEquipmentLoadoutSnapshot ToRuntimeEquipment(const FCatSavedEquipmentLoadout& Saved)
	{
		FCatEquipmentLoadoutSnapshot Snapshot;
		Snapshot.Revision = Saved.Revision;
		Snapshot.RodItemId = Saved.RodItemId;
		Snapshot.RodItemInstanceId = Saved.RodItemInstanceId;
		Snapshot.BaitItemId = Saved.BaitItemId;
		Snapshot.BaitItemInstanceId = Saved.BaitItemInstanceId;
		Snapshot.FloatItemId = Saved.FloatItemId;
		Snapshot.FloatItemInstanceId = Saved.FloatItemInstanceId;
		Snapshot.ScoopNetItemId = Saved.ScoopNetItemId;
		Snapshot.ScoopNetItemInstanceId = Saved.ScoopNetItemInstanceId;
		Snapshot.RodSkinDefinitionId = Saved.RodSkinDefinitionId;
		Snapshot.RodDurability = Saved.RodDurability;
		Snapshot.bRodBroken = Saved.bRodBroken;
		return Snapshot;
	}

	// 运行库存载荷预检流程：
	// 1. 先读取正式物品目录；玩家和营地共用这项载荷检查，容量由各自消费者检查。
	// 2. 逐格确认空格没有残留、占用格引用有效定义、数量不越界、实例键不重复。
	// 3. 鱼竿耐久和损坏状态按当前定义复核；非鱼竿物品不能携带鱼竿状态。
	// 4. 鱼必须有单条重量和捕获来源，普通物品不能夹带鱼字段；只写失败原因，不修改运行库存。
	bool ValidateSavedInventorySlots(const TArray<FCatSavedRunInventorySlot>& InventorySlots, FText& OutFailure)
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		if (!InventorySettings)
		{
			OutFailure = FText::FromString(TEXT("存档库存缺少运行物品目录。"));
			return false;
		}
		TSet<FGuid> SeenInstanceIds;
		for (const FCatSavedRunInventorySlot& Slot : InventorySlots)
		{
			const bool bOccupied = !(Slot.ItemId == 0) && Slot.Quantity > 0;
			if (!bOccupied)
			{
				if (!(Slot.ItemId == 0) || Slot.ItemInstanceId.IsValid() || Slot.Quantity != 0
					|| Slot.RodDurability != 0.0 || Slot.bRodBroken || !Slot.FishGuardHostName.IsNone()
					|| Slot.FishSessionId.IsValid() || Slot.FishWeightKilograms != 0.0 || !Slot.FishOwnerStableNetId.IsEmpty())
				{
					OutFailure = FText::FromString(TEXT("存档随身库存空格包含残留数据。"));
					return false;
				}
				continue;
			}
			const UCatInventoryItemDefinition* InventoryDefinition =
				InventorySettings->FindRuntimeDefinition(Slot.ItemId);
			const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(InventoryDefinition);
			const int32 StackLimit = InventoryDefinition != nullptr ? InventoryDefinition->GetMaxStackCount() : 0;
			if (!Slot.ItemInstanceId.IsValid() || SeenInstanceIds.Contains(Slot.ItemInstanceId) || !InventoryDefinition
				|| !InventoryDefinition->IsInventoryRuntimeDefinitionReady() || Slot.Quantity > StackLimit)
			{
				OutFailure = FText::FromString(TEXT("存档随身库存含有失效定义、数量或重复实例。"));
				return false;
			}
			if (EquipmentDefinition && EquipmentDefinition->CanServeFishingRod())
			{
				if (!FMath::IsFinite(Slot.RodDurability) || Slot.RodDurability < 0.0
					|| Slot.RodDurability > EquipmentDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumRodDurability
					|| (Slot.bRodBroken && Slot.RodDurability != 0.0))
				{
					OutFailure = FText::FromString(TEXT("存档鱼竿耐久与当前定义不匹配。"));
					return false;
				}
			}
			else if (Slot.RodDurability != 0.0 || Slot.bRodBroken)
			{
				OutFailure = FText::FromString(TEXT("存档非鱼竿物品含有鱼竿状态。"));
				return false;
			}
			const TSubclassOf<UCatInventoryItemInstance> InstanceClass =
				UCatInventoryItemDefinition::ResolveItemInstanceClass(InventoryDefinition);
			if (!Slot.FishGuardHostName.IsNone() && (!InstanceClass
				|| !InstanceClass->IsChildOf(UCatFishGuardInventoryItemInstance::StaticClass())))
			{ OutFailure = FText::FromString(TEXT("非鱼护物品含有鱼护载体关联。")); return false; }
			if (!InstanceClass)
			{
				OutFailure = FText::FromString(TEXT("存档物品定义没有可恢复的实例类型。"));
				return false;
			}
			if (InstanceClass->IsChildOf(UCatFishInventoryItemInstance::StaticClass()))
			{
				if (Slot.Quantity != 1 || !Slot.FishSessionId.IsValid() || Slot.FishSessionId == Slot.ItemInstanceId
					|| Slot.FishOwnerStableNetId.IsEmpty() || !FMath::IsFinite(Slot.FishWeightKilograms)
					|| Slot.FishWeightKilograms <= 0.0)
				{
					OutFailure = FText::FromString(TEXT("存档鱼的数量、重量或来源无效。"));
					return false;
				}
			}
			else if (Slot.FishSessionId.IsValid() || Slot.FishWeightKilograms != 0.0 || !Slot.FishOwnerStableNetId.IsEmpty())
			{
				OutFailure = FText::FromString(TEXT("非鱼物品含有鱼载荷。"));
				return false;
			}
			SeenInstanceIds.Add(Slot.ItemInstanceId);
		}
		return true;
	}

	// 保存边界直接读取库存实例：保留空格位置、身份与数量，再采集装备耐久或鱼来源和重量。
	// 鱼保持原始实例 GUID，不重新生成身份；运行宿主、复制状态和临时 Use 上下文不进入磁盘。
	bool WriteSavedInventorySlots(const TArray<FCatInventoryEntry>& Entries,
		TArray<FCatSavedRunInventorySlot>& OutSaved, FText& OutFailure)
	{
		OutSaved.Reset();
		OutSaved.Reserve(Entries.Num());
		for (const FCatInventoryEntry& Entry : Entries)
		{
			FCatSavedRunInventorySlot& Saved = OutSaved.AddDefaulted_GetRef();
			if (!Entry.Instance || Entry.StackCount <= 0)
			{
				continue;
			}
			if (const UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance))
			{
				Saved.FishSessionId = Fish->GetSourceFishingSessionId();
				Saved.FishWeightKilograms = Fish->GetFishWeightKilograms();
				Saved.FishOwnerStableNetId = Fish->GetFishOwnerStableNetId();
			}
			Saved.ItemId = Entry.Instance->GetItemId();
			if (const auto* Guard = Cast<UCatFishGuardInventoryItemInstance>(Entry.Instance))
				if (const auto* Host = Guard->GetWorldActor()) Saved.FishGuardHostName = Host->GetFName();
			Saved.ItemInstanceId = Entry.Instance->GetItemInstanceId();
			Saved.Quantity = Entry.StackCount;
			if (const UCatEquipmentInventoryItemInstance* Equipment = Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance))
			{
				Saved.RodDurability = Equipment->GetRodDurability();
				Saved.bRodBroken = Equipment->IsRodBroken();
			}
		}
		return ValidateSavedInventorySlots(OutSaved, OutFailure);
	}

	// 恢复边界先准备完整实例批次，不改现有库存：按保存的定义创建正确子类，恢复原实例身份、竿状态或鱼的来源与重量。
	// 不合法的空格、重复身份和不可表示的状态会使整批失败；准备成功后，调用方才把 Entries 交给库存一次性接收。
	bool PrepareInventoryEntriesFromSave(const TArray<FCatSavedRunInventorySlot>& Saved,
		UCatInventoryComponent& Inventory, TArray<FCatInventoryEntry>& OutEntries, FText& OutFailure)
	{
		OutEntries.Reset();
		const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
		AActor* Owner = Inventory.GetOwner();
		if (!Owner || !Owner->HasAuthority() || !Settings)
		{
			OutFailure = FText::FromString(TEXT("库存恢复缺少服务器宿主或物品目录。"));
			return false;
		}
		if (!ValidateSavedInventorySlots(Saved, OutFailure))
		{
			return false;
		}
		for (const FCatSavedRunInventorySlot& Slot : Saved)
		{
			FCatInventoryEntry& Entry = OutEntries.Emplace_GetRef(&Inventory);
			if ((Slot.ItemId == 0) || Slot.Quantity <= 0)
			{
				continue;
			}
			UCatInventoryItemDefinition* Definition = Settings->FindRuntimeDefinition(Slot.ItemId);
			const TSubclassOf<UCatInventoryItemInstance> InstanceClass = UCatInventoryItemDefinition::ResolveItemInstanceClass(Definition);
			UCatInventoryItemInstance* Instance = NewObject<UCatInventoryItemInstance>(Owner, InstanceClass);
			Instance->SetRuntimeOwnerActor(Owner);
			Instance->SetItemDefinition(Definition);
			Instance->SetItemInstanceIdFromAuthority(Slot.ItemInstanceId);
			if (UCatEquipmentInventoryItemInstance* Equipment = Cast<UCatEquipmentInventoryItemInstance>(Instance))
			{
				Equipment->SetRodRuntimeStateFromAuthority(Slot.RodDurability, Slot.bRodBroken);
				if (Equipment->GetRodDurability() != Slot.RodDurability || Equipment->IsRodBroken() != Slot.bRodBroken)
				{
					OutFailure = FText::FromString(TEXT("库存存档的鱼竿状态与当前定义不匹配。"));
					return false;
				}
			}
			if (UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Instance))
			{
				if (!Fish->InitializeFishFromAuthority(Slot.FishSessionId, Slot.ItemInstanceId,
					Slot.FishOwnerStableNetId, Slot.FishWeightKilograms))
				{
					OutFailure = FText::FromString(TEXT("库存鱼的身份、重量或捕获来源无效。"));
					return false;
				}
			}
			if (!Slot.FishGuardHostName.IsNone())
			{
				ACatFishGuardActor* Guard = nullptr;
				for (TActorIterator<ACatFishGuardActor> It(Owner->GetWorld()); It; ++It)
					if (It->GetFName() == Slot.FishGuardHostName) { Guard = *It; break; }
				if (!Guard || !Guard->InitializeFromInventoryFromAuthority(Instance, Slot.Quantity))
				{ OutFailure = FText::FromString(TEXT("存档鱼护载体关联无效。")); return false; }
				Instance->SetRuntimeOwnerActor(Owner);
			}
			Entry.Instance = Instance;
			Entry.StackCount = Slot.Quantity;
		}
		return true;
	}

	// 存档随身容量解析流程：容量只取正式库存设置；没有设置时返回 0，让调用方拒绝不完整配置。
	int32 ResolvePlayerSnapshotInventorySlotCapacity()
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		return InventorySettings != nullptr ? InventorySettings->GetPlayerInventorySlotCapacity() : 0;
	}

	// 玩家钓具选择预检流程：
	// 1. 空选择必须清空实例身份；非空选择必须在同一玩家随身库存中找到同实例。
	// 2. 选中物品定义必须能服务对应钓具槽，避免读档后 Equipment 指向普通库存物品。
	// 3. 鱼竿摘要耐久和损坏状态必须与选中鱼竿格一致；没有鱼竿选择时摘要状态必须为空。
	bool ValidateSavedEquipmentSnapshot(const FCatEquipmentLoadoutSnapshot& Snapshot,
		const TArray<FCatSavedRunInventorySlot>& InventorySlots, FText& OutFailure)
	{
		if (InventorySlots.Num() > ResolvePlayerSnapshotInventorySlotCapacity()
			|| !ValidateSavedInventorySlots(InventorySlots, OutFailure))
		{
			return false;
		}
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		if (!InventorySettings)
		{
			OutFailure = FText::FromString(TEXT("存档钓具目录不可用。"));
			return false;
		}
		const auto HasSelectedInstance = [&InventorySlots, InventorySettings](const int32  ItemId,
			const FGuid InstanceId, const FName ExpectedSlotId)
		{
			if ((ItemId == 0))
			{
				return !InstanceId.IsValid();
			}
			return InstanceId.IsValid() && InventorySlots.ContainsByPredicate(
				[ItemId, InstanceId, ExpectedSlotId, InventorySettings](const FCatSavedRunInventorySlot& Slot)
				{
					const UCatEquipmentDefinition* Definition =
						Cast<UCatEquipmentDefinition>(InventorySettings->FindRuntimeDefinition(Slot.ItemId));
					return Slot.ItemId == ItemId && Slot.ItemInstanceId == InstanceId && Definition
						&& Definition->CanServeFishingLoadoutSlot(ExpectedSlotId);
				});
		};
		if (!HasSelectedInstance(Snapshot.RodItemId, Snapshot.RodItemInstanceId,
				UCatEquipmentDefinition::FishingRodLoadoutSlotId())
			|| !HasSelectedInstance(Snapshot.BaitItemId, Snapshot.BaitItemInstanceId,
				UCatEquipmentDefinition::FishingBaitLoadoutSlotId())
			|| !HasSelectedInstance(Snapshot.FloatItemId, Snapshot.FloatItemInstanceId,
				UCatEquipmentDefinition::FishingFloatLoadoutSlotId())
			|| !HasSelectedInstance(Snapshot.ScoopNetItemId, Snapshot.ScoopNetItemInstanceId,
				UCatEquipmentDefinition::ScoopNetLoadoutSlotId()))
		{
			OutFailure = FText::FromString(TEXT("存档钓具选择没有指向同一库存中的合法实例。"));
			return false;
		}
		const FCatSavedRunInventorySlot* SelectedRod = InventorySlots.FindByPredicate(
			[&Snapshot](const FCatSavedRunInventorySlot& Slot) { return Slot.ItemInstanceId == Snapshot.RodItemInstanceId; });
		if ((!(Snapshot.RodItemId == 0) && (!SelectedRod || Snapshot.RodDurability != SelectedRod->RodDurability
			|| Snapshot.bRodBroken != SelectedRod->bRodBroken))
			|| ((Snapshot.RodItemId == 0) && (Snapshot.RodItemInstanceId.IsValid()
				|| Snapshot.RodDurability != 0.0 || Snapshot.bRodBroken)))
		{
			OutFailure = FText::FromString(TEXT("存档鱼竿选择状态与库存实例不一致。"));
			return false;
		}
		return true;
	}
}

// 初始化流程：GameInstance 建立后只清空本轮内存状态并给前端一个可展示的初始结果；目录读取仍显式异步发起，避免把启动磁盘 I/O 藏进构造期。
void UCatSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LastResultText = FText::FromString(TEXT("存档目录尚未读取。"));
	SaveQueueTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickSaveQueue));
}

// 反初始化流程：异步委托以 UObject 弱绑定自动失效；这里先清所有强引用和旅行许可，使失效 World 回调即使晚到也不能恢复到下一次游戏实例。
void UCatSaveSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(SaveQueueTicker);
	SaveQueueTicker.Reset();
	QueuedSaveRequests.Reset();
	QueuedSaveSlots.Reset();
	QueuedSavePayloads.Reset();
	RetrySavePayloads.Reset();
	CompletedRunSlots.Reset();
	PendingCompletionSlots.Reset();
	bBusy = false;
	bLoadedRunForTravel = false;
	bWorldRestoreApplied = false;
	PendingRestoreSaveGame = nullptr;
	ActiveAsyncRunSaveGame = nullptr;
	bLocalPlayerRestoredInCurrentWorld = false;
	CapturedDepartingControllers.Reset();
	PlayerCaptureFailure = FText::GetEmpty();
	OnChanged.Clear();
	OnSaveCompleted.Clear();
	Super::Deinitialize();
}

// 槽目录刷新流程：
// 1. 先进入 busy 状态，让前端在目录扫描期间合并重复刷新。
// 2. 后台只扫描 SaveGames 里的 CatRun_*.sav 文件名，不读取额外目录文件。
// 3. 回到游戏线程后逐个反序列化合法单文件槽，校验版本、槽归属和载荷内容。
// 4. 最后整体替换内存摘要；非当前格式不会进入目录，避免无效载荷继续影响正式读档。
void UCatSaveSubsystem::RefreshSlotSummaries()
{
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	if (!LocalPlayer)
	{
		MakeResult(false, FText::FromString(TEXT("本机玩家尚未就绪，不能读取存档目录。")));
		return;
	}
	if (IsBusy())
	{
		return;
	}
	bBusy = true;
	LastResultText = FText::FromString(TEXT("正在读取存档目录。"));
	OnChanged.Broadcast();
	const TWeakObjectPtr<UCatSaveSubsystem> WeakThis(this);
	// 这里只把文件名枚举放到后台；UObject 反序列化仍回游戏线程执行，避免 SaveGame 生命周期跨线程。
	Async(EAsyncExecution::ThreadPool, [WeakThis]()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files,
			*FPaths::Combine(FPaths::ProjectSavedDir(),
				FString::Printf(TEXT("SaveGames/%s*%s"), RunSlotFilePrefix, SaveGameFileExtension)), true, false);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Files = MoveTemp(Files)]()
		{
			UCatSaveSubsystem* Self = WeakThis.Get();
			if (!Self || !Self->bBusy)
			{
				return;
			}
			const ULocalPlayer* LocalPlayer = Self->GetGameInstance()->GetFirstGamePlayer();
			if (!LocalPlayer)
			{
				Self->FinishDiskRequest(FGuid(), false, FText::FromString(TEXT("本机玩家已离开，目录读取取消。")));
				return;
			}
			TArray<FCatSaveSlotSummary> LoadedSummaries;
			TSet<FName> SeenSlotIds;
			int32 RejectedFileCount = 0;
			for (const FString& File : Files)
			{
				const FString BaseName = FPaths::GetBaseFilename(File);
				FName SlotId;
				if (!TryParseRunSlotIdFromBaseName(BaseName, SlotId))
				{
					continue;
				}
				if (SeenSlotIds.Contains(SlotId))
				{
					++RejectedFileCount;
					continue;
				}
				if (!HasRunSaveFileHeader(BaseName))
				{
					++RejectedFileCount;
					UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_slot_scan_rejected Slot=%s File=%s Reason=InvalidFileHeader"),
						*SlotId.ToString(), *File);
					continue;
				}
				FText Failure;
				UCatRunSaveGame* SaveGame = Cast<UCatRunSaveGame>(
					ULocalPlayerSaveGame::LoadOrCreateSaveGameForLocalPlayer(UCatRunSaveGame::StaticClass(),
						LocalPlayer, UCatSaveSubsystem::MakeRunSlotFileName(SlotId)));
				if (!SaveGame || !SaveGame->WasLoaded() || !Self->ValidateLoadedRunSaveGame(*SaveGame, SlotId, Failure))
				{
					++RejectedFileCount;
					UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_slot_scan_rejected Slot=%s File=%s Reason=%s"),
						*SlotId.ToString(), *File, *Failure.ToString());
					continue;
				}
				SeenSlotIds.Add(SlotId);
				FCatSaveSlotSummary Summary = MakeSummary(*SaveGame);
				Summary.bRunCompleted |= Self->CompletedRunSlots.Contains(SlotId);
				LoadedSummaries.Add(MoveTemp(Summary));
			}
			LoadedSummaries.Sort([](const FCatSaveSlotSummary& Left, const FCatSaveSlotSummary& Right)
			{
				return Left.LastSavedAt > Right.LastSavedAt;
			});
			Self->SlotSummaries = MoveTemp(LoadedSummaries);
			Self->bSlotDirectoryLoaded = true;
			UE_LOG(LogCatRun, Log,
				TEXT("Event=persistence_slot_scan_completed Slots=%d RejectedFiles=%d World=%s"),
				Self->SlotSummaries.Num(), RejectedFileCount, *GetNameSafe(Self->GetWorld()));
			const FText Message = Self->SlotSummaries.IsEmpty()
				? FText::FromString(TEXT("未找到存档目录，可以创建新世界。"))
				: FText::FromString(TEXT("存档目录已读取。"));
			Self->FinishDiskRequest(FGuid(), true, Message);
		});
	});
}

// 槽列表读取流程：返回最近一次成功目录，不在这里触发磁盘访问或返回可写副本。
const TArray<FCatSaveSlotSummary>& UCatSaveSubsystem::GetSlotSummaries() const
{
	return SlotSummaries;
}

// 新建槽流程：
// 1. 先校验目录扫描已完成、当前没有活动世界、显示名可保存。
// 2. 再分配稳定 SlotId，把显示名和空世界载荷写进同一个 SaveGame 对象。
// 3. 最后调用 UE 原生异步单槽写入；完成回调会把这个槽加入内存摘要。
FCatSaveResult UCatSaveSubsystem::RequestCreateSlot(const FString& DisplayName)
{
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	const FString TrimmedDisplayName = DisplayName.TrimStartAndEnd();
	if (!LocalPlayer || IsBusy() || !bSlotDirectoryLoaded || bWorldRestoreApplied || PendingRestoreSaveGame || TrimmedDisplayName.IsEmpty()
		|| TrimmedDisplayName.Len() > 64)
	{
		return MakeResult(false, FText::FromString(TEXT("存档目录未就绪、操作进行中或名称无效。")));
	}
	const FName SlotId(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UCatRunSaveGame* NewSaveGame = CastChecked<UCatRunSaveGame>(
		ULocalPlayerSaveGame::CreateNewSaveGameForLocalPlayer(UCatRunSaveGame::StaticClass(),
			LocalPlayer, MakeRunSlotFileName(SlotId)));
	NewSaveGame->SlotId = SlotId;
	NewSaveGame->DisplayName = TrimmedDisplayName;
	NewSaveGame->LastSavedAt = FDateTime::UtcNow();
	ActiveAsyncRunSaveGame = NewSaveGame;
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在创建新存档。")));
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_slot_write_started RequestId=%s Slot=%s File=%s"),
		*Result.RequestId.ToString(), *SlotId.ToString(), *MakeRunSlotFileName(SlotId));
	NewSaveGame->OnSaveFinished.BindUObject(this, &ThisClass::HandleRunSaved, Result.RequestId);
	if (!NewSaveGame->AsyncSaveGameToSlotForLocalPlayer())
	{
		FinishDiskRequest(Result.RequestId, false, FText::FromString(TEXT("本机存档写入未能启动。")));
		return MakeResult(false, LastResultText);
	}
	return Result;
}

// 读取槽流程：先拒绝 busy、未扫描目录、已进入世界或不在摘要中的 SlotId；通过后异步读取同名单文件 Run。
// 已终局的槽在这里被拒绝：毕业或团灭之后那一局打完了，不再提供「继续」，玩家要开新局就新建一个槽（2026-09-11 拍）。
// 拒绝只挡住「继续」这一条路；文件一个不动，仍留在目录里当战绩回看，也仍然只能由玩家自己在前端删。
// 成功只建立待恢复快照和旅行许可，不从显示名或列表下标反推载荷；失败由回调写入结果文本并清理许可。
FCatSaveResult UCatSaveSubsystem::RequestLoadSlot(const FName SlotId)
{
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	const FCatSaveSlotSummary* RequestedSummary = SlotSummaries.FindByPredicate(
		[SlotId](const FCatSaveSlotSummary& Summary) { return Summary.SlotId == SlotId; });
	if (!LocalPlayer || IsBusy() || !bSlotDirectoryLoaded || bWorldRestoreApplied || PendingRestoreSaveGame || !IsValidSlotId(SlotId)
		|| !RequestedSummary)
	{
		return MakeResult(false, FText::FromString(TEXT("存档不存在、目录未就绪或正在执行其他操作。")));
	}
	if (RequestedSummary->bRunCompleted || CompletedRunSlots.Contains(SlotId))
	{
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_slot_read_rejected Slot=%s World=%s Reason=RunCompleted"),
			*SlotId.ToString(), *GetNameSafe(GetWorld()));
		return MakeResult(false, FText::FromString(TEXT("这一局已经结束，只能回看不能继续；请新建一个世界开新局。")));
	}
	if (RetrySavePayloads.Contains(SlotId))
	{
		EnqueueRunSave(SlotId, RetrySavePayloads.FindAndRemoveChecked(SlotId));
		return MakeResult(false, FText::FromString(TEXT("正在重试该槽上次未成功的保存，请完成后再继续。")));
	}
	if (!HasRunSaveFileHeader(MakeRunSlotFileName(SlotId)))
	{
		UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_slot_read_rejected Slot=%s World=%s Reason=InvalidFileHeader"),
			*SlotId.ToString(), *GetNameSafe(GetWorld()));
		return MakeResult(false, FText::FromString(TEXT("存档文件缺失或文件头损坏，不能读取。")));
	}
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在读取存档。")));
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_slot_read_started RequestId=%s Slot=%s File=%s"),
		*Result.RequestId.ToString(), *SlotId.ToString(), *MakeRunSlotFileName(SlotId));
	// Lyra 的异步入口失败时会创建默认对象；完成回调必须检查 WasLoaded，坏档不能悄悄变成新局。
	if (!ULocalPlayerSaveGame::AsyncLoadOrCreateSaveGameForLocalPlayer(UCatRunSaveGame::StaticClass(),
		LocalPlayer, MakeRunSlotFileName(SlotId),
		FOnLocalPlayerSaveGameLoadedNative::CreateUObject(this, &ThisClass::HandleRunLoaded, Result.RequestId, SlotId)))
	{
		FinishDiskRequest(Result.RequestId, false, FText::FromString(TEXT("本机存档读取未能启动。")));
		return MakeResult(false, LastResultText);
	}
	return Result;
}

// 未进入玩法的当前单文件槽才允许被移除：
// 1. 先拒绝活动槽、未扫描目录和并发请求，避免删除正在恢复或保存的世界。
// 2. 再删除该槽唯一 SaveGame 文件；成功后从内存摘要移除它。
// 3. 删除失败时保留上一份摘要，让玩家仍能看到失败前的槽并重试。
FCatSaveResult UCatSaveSubsystem::RequestDeleteSlot(const FName SlotId)
{
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	if (!LocalPlayer || IsBusy() || !bSlotDirectoryLoaded || SlotId == ActiveSlotId || !IsValidSlotId(SlotId)
		|| !SlotSummaries.ContainsByPredicate([SlotId](const FCatSaveSlotSummary& Summary) { return Summary.SlotId == SlotId; }))
	{
		return MakeResult(false, FText::FromString(TEXT("运行中的存档不能删除，或目标槽不存在。")));
	}
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在删除存档。")));
	const FString SlotName = MakeRunSlotFileName(SlotId);
	const bool bDeleted = UGameplayStatics::DeleteGameInSlot(SlotName, LocalPlayer->GetPlatformUserIndex());
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_slot_delete_completed RequestId=%s Slot=%s File=%s Deleted=%d"),
		*Result.RequestId.ToString(), *SlotId.ToString(), *SlotName, bDeleted);
	if (bDeleted)
	{
		RetrySavePayloads.Remove(SlotId);
		CompletedRunSlots.Remove(SlotId);
		PendingCompletionSlots.Remove(SlotId);
		SlotSummaries.RemoveAll([SlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == SlotId; });
	}
	FinishDiskRequest(Result.RequestId, bDeleted, bDeleted
		? FText::FromString(TEXT("存档文件已删除。"))
		: FText::FromString(TEXT("存档文件删除失败，原目录已保留。")));
	return Result;
}

// 墓碑（2026-09-14，T33）：busy 拒绝和仅在快照末尾采样终局已移除。
// 《局与进程》Knowledge/Design/GDD 系统分册/局与进程.md:103：已完结槽不可续，写失败不影响本局、下局重试。
// 快照仍同步完整校验；受理后串行写入，每个 RequestId 都收到完成回执，失败载荷跨 World 保留。
FCatSaveResult UCatSaveSubsystem::RequestSaveActiveRun()
{
	if (!bSlotDirectoryLoaded || ActiveSlotId.IsNone() || !PendingRestoreSaveGame
		|| !SlotSummaries.ContainsByPredicate([this](const FCatSaveSlotSummary& Summary) { return Summary.SlotId == ActiveSlotId; }))
		return MakeResult(false, FText::FromString(TEXT("没有可保存的活动世界槽。")));
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	if (!LocalPlayer)
		return MakeResult(false, FText::FromString(TEXT("本机玩家不可用，不能保存世界。")));
	UCatRunSaveGame* SaveGame = CastChecked<UCatRunSaveGame>(
		ULocalPlayerSaveGame::CreateNewSaveGameForLocalPlayer(UCatRunSaveGame::StaticClass(),
			LocalPlayer, MakeRunSlotFileName(ActiveSlotId)));
	FText Failure;
	if (!BuildActiveRunSaveGame(*SaveGame, Failure))
	{
		if (PendingCompletionSlots.Contains(ActiveSlotId))
		{
			UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_completion_snapshot_failed Slot=%s World=%s Reason=%s Result=QueueMarker"),
				*ActiveSlotId.ToString(), *GetNameSafe(GetWorld()), *Failure.ToString());
			return EnqueueRunSave(ActiveSlotId, nullptr);
		}
		return MakeResult(false, Failure);
	}
	PendingRestoreSaveGame->bHasPlayerSnapshot = SaveGame->bHasPlayerSnapshot;
	PendingRestoreSaveGame->PlayerSnapshot = SaveGame->PlayerSnapshot;
	PendingRestoreSaveGame->bRunCompleted = SaveGame->bRunCompleted;
	return EnqueueRunSave(ActiveSlotId, SaveGame);
}

FCatSaveResult UCatSaveSubsystem::RequestCompleteActiveRun()
{
	const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!Mode || !Mode->HasAuthority() || ActiveSlotId.IsNone() || !PendingRestoreSaveGame)
		return MakeResult(false, FText::FromString(TEXT("没有可标记完成的房主活动槽。")));
	const auto Reason = Mode->GetRunPublicState().EndReason;
	if (Reason != ECatRunEndReason::Success && Reason != ECatRunEndReason::WorldProgressDepleted)
		return MakeResult(false, FText::FromString(TEXT("当前局尚未终局。")));
	// 先保留事实，任何快照失败、旧在途写入或 ReleaseActiveRun 都不能撤销它。
	CompletedRunSlots.Add(ActiveSlotId);
	PendingCompletionSlots.Add(ActiveSlotId);
	PendingRestoreSaveGame->bRunCompleted = true;
	for (auto& Summary : SlotSummaries)
		if (Summary.SlotId == ActiveSlotId) Summary.bRunCompleted = true;
	const FCatSaveResult Result = RequestSaveActiveRun();
	if (!Result.bAccepted) RetrySavePayloads.FindOrAdd(ActiveSlotId);
	UE_LOG(LogCatRun, Display, TEXT("Event=persistence_completion_requested RequestId=%s Slot=%s RunId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Accepted=%d"),
		*Result.RequestId.ToString(), *ActiveSlotId.ToString(), *Mode->GetRunPublicState().Phase.RunId.ToString(),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Mode->GetLocalRole()), Result.bAccepted);
	return Result;
}

FCatSaveResult UCatSaveSubsystem::EnqueueRunSave(const FName SlotId, UCatRunSaveGame* Payload)
{
	// 先建队列再广播受理；消费者重入时 CRUD/释放已可见队列占用。
	const FGuid RequestId = FGuid::NewGuid();
	QueuedSaveRequests.Add(RequestId);
	QueuedSaveSlots.Add(RequestId, SlotId);
	QueuedSavePayloads.Add(RequestId, Payload);
	FCatSaveResult Result;
	Result.bAccepted = true;
	Result.RequestId = RequestId;
	Result.Message = FText::FromString(TEXT("世界存档已排队。"));
	LastResultText = Result.Message;
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_slot_write_queued RequestId=%s Slot=%s World=%s MarkerOnly=%d QueueDepth=%d"),
		*RequestId.ToString(), *SlotId.ToString(), *GetNameSafe(GetWorld()), Payload == nullptr, QueuedSaveRequests.Num());
	OnChanged.Broadcast();
	return Result;
}

bool UCatSaveSubsystem::TickSaveQueue(float DeltaSeconds)
{
	if (bBusy) return true;
	if (QueuedSaveRequests.IsEmpty() && !RetrySavePayloads.IsEmpty()
		&& FPlatformTime::Seconds() >= NextSaveRetrySeconds)
	{
		// 一轮失败最多每五秒重试一次；CoreTicker 随 GameInstance 跨旅行，终局关 Run 命令不关重试。
		TArray<FName> RetrySlots;
		RetrySavePayloads.GenerateKeyArray(RetrySlots);
		for (const FName SlotId : RetrySlots)
		{
			if (RetrySavePayloads.Contains(SlotId))
				EnqueueRunSave(SlotId, RetrySavePayloads.FindAndRemoveChecked(SlotId));
		}
	}
	StartNextRunSave();
	return true;
}

void UCatSaveSubsystem::StartNextRunSave()
{
	if (bBusy || QueuedSaveRequests.IsEmpty()) return;
	ActiveQueuedSaveRequest = QueuedSaveRequests[0];
	QueuedSaveRequests.RemoveAt(0);
	ActiveQueuedSaveSlot = QueuedSaveSlots.FindAndRemoveChecked(ActiveQueuedSaveRequest);
	ActiveAsyncRunSaveGame = QueuedSavePayloads.FindAndRemoveChecked(ActiveQueuedSaveRequest);
	bBusy = true;
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	const bool bMarkerOnly = !ActiveAsyncRunSaveGame;
	if (bMarkerOnly && LocalPlayer)
	{
		// 只有轮到本写请求才读取最后成功的文件；不重建或覆盖其他槽，也不写失败的部分世界快照。
		ActiveAsyncRunSaveGame = Cast<UCatRunSaveGame>(ULocalPlayerSaveGame::LoadOrCreateSaveGameForLocalPlayer(
			UCatRunSaveGame::StaticClass(), LocalPlayer, MakeRunSlotFileName(ActiveQueuedSaveSlot)));
		if (ActiveAsyncRunSaveGame && !ActiveAsyncRunSaveGame->WasLoaded()) ActiveAsyncRunSaveGame = nullptr;
	}
	FText Failure;
	if (!LocalPlayer || !ActiveAsyncRunSaveGame
		|| !ValidateLoadedRunSaveGame(*ActiveAsyncRunSaveGame, ActiveQueuedSaveSlot, Failure))
	{
		if (bMarkerOnly) ActiveAsyncRunSaveGame = nullptr; // 修复文件后重试须重新读盘，不能永久复用坏载荷。
		FinishDiskRequest(ActiveQueuedSaveRequest, false, Failure.IsEmpty()
			? FText::FromString(TEXT("终局标记补写未能读取原槽，已保留重试。")) : Failure);
		return;
	}
	ActiveAsyncRunSaveGame->bRunCompleted |= CompletedRunSlots.Contains(ActiveQueuedSaveSlot);
	ActiveAsyncRunSaveGame->LastSavedAt = FDateTime::UtcNow();
	ActiveAsyncRunSaveGame->OnSaveFinished.BindUObject(this, &ThisClass::HandleRunSaved, ActiveQueuedSaveRequest);
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_slot_write_started RequestId=%s Slot=%s World=%s RunCompleted=%d"),
		*ActiveQueuedSaveRequest.ToString(), *ActiveQueuedSaveSlot.ToString(), *GetNameSafe(GetWorld()), ActiveAsyncRunSaveGame->bRunCompleted);
	const FGuid RequestId = ActiveQueuedSaveRequest;
	if (!ActiveAsyncRunSaveGame->AsyncSaveGameToSlotForLocalPlayer() && ActiveQueuedSaveRequest == RequestId)
		FinishDiskRequest(RequestId, false, FText::FromString(TEXT("本机存档写入未能启动，已保留重试。")));
}

// 终态释放流程：先拒绝仍有磁盘回调的请求；调用方确认房间或世界已离开后，清本局强引用、玩家快照、时长和旅行许可，目录保留供下一次选槽。
// 这里既不采集也不保存，Host 等待一次保存回执后即可离开；失败载荷和完成意图由独立重试集合保留。
bool UCatSaveSubsystem::ReleaseActiveRun()
{
	if (IsBusy())
	{
		UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_release_rejected Slot=%s Reason=Busy"), *ActiveSlotId.ToString());
		return false;
	}
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_run_released Slot=%s World=%s"),
		*ActiveSlotId.ToString(), *GetNameSafe(GetWorld()));
	PendingRestoreSaveGame = nullptr;
	ActiveAsyncRunSaveGame = nullptr;
	ActiveSlotId = NAME_None;
	bLoadedRunForTravel = false;
	bWorldRestoreApplied = false;
	bLocalPlayerRestoredInCurrentWorld = false;
	CapturedDepartingControllers.Reset();
	PlayerCaptureFailure = FText::GetEmpty();
	ActiveRunStartedWorldSeconds = -1.0;
	ActiveRunBasePlayedDurationSeconds = 0.0;
	OnChanged.Broadcast();
	return true;
}

// 旅行许可读取流程：只在完整载荷已读入、未被拒绝且共享世界尚未写入时为真；Online 不接触载荷内容。
bool UCatSaveSubsystem::HasLoadedRunForTravel() const
{
	return bLoadedRunForTravel && PendingRestoreSaveGame != nullptr && !bWorldRestoreApplied;
}

// 忙碌读取流程：返回目录或世界异步读写的统一串行状态；前端据此禁止重复点击。
bool UCatSaveSubsystem::IsBusy() const
{
	return bBusy || !QueuedSaveRequests.IsEmpty();
}

// 结果读取流程：返回最近一次同步受理或异步完成的文本；不会把日志解析结果伪装成 UI 状态。
FText UCatSaveSubsystem::GetLastResultText() const
{
	return LastResultText;
}

// 活动槽读取流程：只有成功读入的槽才成为活动槽；新建只增加目录项，不凭列表排序猜测玩家选中了哪个槽。
FName UCatSaveSubsystem::GetActiveSlotId() const
{
	return ActiveSlotId;
}

// 世界恢复流程：
// 1. 先确认 GameMode、营地与鱼容器服务都属于 authority World；新局只要求真实宿主可导出当前状态。
// 2. 有世界载荷时先导出营地当前状态，再提交营地和世界鱼容器；鱼容器服务异常时只尝试还原营地并阻止进入玩法。
// 3. 成功后保留玩家载荷等待 Pawn 生成时恢复，同时开始统计本 World 的实际玩法时长。
namespace
{
	UCatInventoryComponent* GetSavedHostInventory(AActor* Host)
	{
		if (auto* Camp = Cast<ACatCampInventoryActor>(Host)) return Camp->GetInventoryComponent();
		if (auto* Tank = Cast<ACatFishTankActor>(Host)) return Tank->GetFishInventoryComponent();
		if (auto* Guard = Cast<ACatFishGuardActor>(Host)) return Guard->GetFishInventoryComponent();
		return nullptr;
	}

	bool ValidateWorldInventory(const FCatSavedWorldInventory& Saved, FText& Failure)
	{
		UClass* Class = Saved.HostClass.LoadSynchronous();
		const bool bTank = Class && Class->IsChildOf(ACatFishTankActor::StaticClass());
		if (!Class || (!bTank && !Class->IsChildOf(ACatFishGuardActor::StaticClass())
			&& !Class->IsChildOf(ACatCampInventoryActor::StaticClass())) || Saved.HostName.IsNone()
			|| !Saved.HostTransform.IsValid() || Saved.HostTransform.GetScale3D().GetMin() <= 0.0
			|| Saved.Capacity <= 0 || Saved.InventorySlots.Num() > Saved.Capacity
			|| uint8(Saved.TeamStorageRole) > uint8(ECatTeamStorageRole::SupplyStore)
			|| (bTank ? Saved.CapacityTier < 0
				: Saved.CapacityTier != INDEX_NONE))
		{
			Failure = FText::FromString(TEXT("世界库存宿主、角色、容量或鱼缸档位无效。"));
			return false;
		}
		return ValidateSavedInventorySlots(Saved.InventorySlots, Failure);
	}
}

bool UCatSaveSubsystem::CaptureWorldInventories(UWorld& World,
	TArray<FCatSavedWorldInventory>& OutInventories, FText& OutFailure) const
{
	OutInventories.Reset();
	if (World.GetNetMode() == NM_Client) return false;
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		AActor* Host = *It;
		UCatInventoryComponent* Inventory = GetSavedHostInventory(Host);
		if (!Inventory) continue;
		FCatSavedWorldInventory Saved;
		Saved.HostName = Host->GetFName();
		Saved.HostClass = Host->GetClass();
		Saved.HostTransform = Host->GetActorTransform();
		Saved.bRuntimeCreated = !Host->IsNetStartupActor();
		Saved.Capacity = Inventory->GetInventorySlotCount();
		Saved.TeamStorageRole = Inventory->GetTeamStorageRole();
		if (const auto* Tank = Cast<ACatFishTankActor>(Host)) Saved.CapacityTier = Tank->GetCapacityTier();
		TArray<FCatInventoryEntry> Entries;
		if (!Inventory->ExportInventorySlotsFromAuthority(Entries, Saved.Capacity, OutFailure)
			|| !WriteSavedInventorySlots(Entries, Saved.InventorySlots, OutFailure)
			|| !ValidateWorldInventory(Saved, OutFailure)) return false;
		OutInventories.Add(MoveTemp(Saved));
	}
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_inventories_exported World=%s NetMode=%d Authority=1 Hosts=%d"),
		*World.GetName(), World.GetNetMode(), OutInventories.Num());
	return true;
}

bool UCatSaveSubsystem::RestoreWorldInventories(UWorld& World,
	const TArray<FCatSavedWorldInventory>& Inventories, FText& OutFailure) const
{
	if (World.GetNetMode() == NM_Client) return false;
	TSet<FName> Names;
	TSet<FGuid> Ids;
	for (const auto& Saved : Inventories)
	{
		if (Names.Contains(Saved.HostName) || !ValidateWorldInventory(Saved, OutFailure)) return false;
		Names.Add(Saved.HostName);
		if (const auto* TankDefaults = Cast<ACatFishTankActor>(Saved.HostClass.Get()->GetDefaultObject()))
		{
			// 目标取当前配置；容量只约束占用数，旧空槽与尾部位置不使有效存档失效。
			const ACatFishTankActor* CapacityHost = TankDefaults;
			for (TActorIterator<ACatFishTankActor> It(&World); It; ++It)
				if (It->GetFName() == Saved.HostName) { CapacityHost = *It; break; }
			const int32 TargetCapacity = CapacityHost->ResolveSlotCapacityForTier(Saved.CapacityTier);
			const int32 Occupied = Saved.InventorySlots.FilterByPredicate([](const auto& Slot) { return Slot.Quantity > 0; }).Num();
			if (TargetCapacity <= 0 || Occupied > TargetCapacity)
			{
				OutFailure = FText::FromString(TEXT("世界鱼容器的已保存鱼超过当前容量。"));
				UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_inventory_capacity_exceeded World=%s NetMode=%d Authority=1 Host=%s SavedCapacity=%d TargetCapacity=%d FishCount=%d Result=RestoreRejectedDiskPreserved"),
					*World.GetName(), World.GetNetMode(), *Saved.HostName.ToString(), Saved.Capacity, TargetCapacity, Occupied);
				return false;
			}
		}
		for (const auto& Slot : Saved.InventorySlots)
		{
			if (!Slot.ItemInstanceId.IsValid()) continue;
			if (Ids.Contains(Slot.ItemInstanceId)) return false;
			Ids.Add(Slot.ItemInstanceId);
		}
	}
	// 全部磁盘数据先预检；分两遍准备宿主与库存，鱼护关联不依赖遍历顺序。
	TMap<FName, AActor*> Hosts;
	for (const auto& Saved : Inventories)
	{
		AActor* Host = nullptr;
		for (TActorIterator<AActor> It(&World); It; ++It)
			if (It->GetFName() == Saved.HostName) { Host = *It; break; }
		if (Host && Host->GetClass() != Saved.HostClass.Get()) return false;
		if (!Host)
		{
			if (!Saved.bRuntimeCreated) return false;
			FActorSpawnParameters Spawn;
			Spawn.Name = Saved.HostName;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Host = World.SpawnActor<AActor>(Saved.HostClass.Get(), Saved.HostTransform, Spawn);
		}
		// 地图原有鱼护也可能已被玩家挪动；不能只给新生成的宿主恢复位置。
		if (Host && Host->IsA<ACatFishGuardActor>() && !Host->GetActorTransform().Equals(Saved.HostTransform)
			&& !Host->SetActorTransform(Saved.HostTransform, false, nullptr, ETeleportType::TeleportPhysics)) return false;
		auto* Inventory = GetSavedHostInventory(Host);
		if (!Inventory || !Inventory->RestoreTeamStorageRoleFromAuthority(Saved.TeamStorageRole)) return false;
		if (auto* Tank = Cast<ACatFishTankActor>(Host))
			if (!Tank->RestoreCapacityTierFromAuthority(Saved.CapacityTier)) return false;
		Hosts.Add(Saved.HostName, Host);
	}
	for (const auto& Saved : Inventories)
	{
		AActor* Host = Hosts.FindChecked(Saved.HostName);
		auto* Inventory = GetSavedHostInventory(Host);
		const auto* Tank = Cast<ACatFishTankActor>(Host);
		const int32 TargetCapacity = Tank ? Tank->ResolveSlotCapacityForTier(Saved.CapacityTier) : Saved.Capacity;
		TArray<FCatSavedRunInventorySlot> Slots = Saved.InventorySlots;
		if (Slots.Num() > TargetCapacity)
		{
			for (int32 Index = TargetCapacity; Index < Slots.Num(); ++Index)
			{
				if (Slots[Index].Quantity <= 0) continue;
				const int32 Empty = Slots.IndexOfByPredicate([](const auto& Slot) { return Slot.Quantity == 0; });
				if (Empty == INDEX_NONE || Empty >= TargetCapacity) return false; // 占用数已在宿主修改前预检。
				Slots[Empty] = Slots[Index];
			}
			Slots.SetNum(TargetCapacity);
		}
		TArray<FCatInventoryEntry> Entries;
		if (!PrepareInventoryEntriesFromSave(Slots, *Inventory, Entries, OutFailure)
			|| !Inventory->RestoreInventorySlotsFromAuthority(Entries, TargetCapacity, OutFailure)) return false;
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_inventory_restored World=%s NetMode=%d Authority=1 LocalRole=%d Host=%s Role=%d SavedCapacity=%d TargetCapacity=%d Tier=%d Slots=%d"),
			*World.GetName(), World.GetNetMode(), Host->GetLocalRole(), *Host->GetName(), uint8(Saved.TeamStorageRole),
			Saved.Capacity, TargetCapacity, Saved.CapacityTier, Saved.InventorySlots.Num());
	}
	return true;
}

bool UCatSaveSubsystem::RestoreWorldAfterHostsReady(ACatfishingGameModeBase& GameMode)
{
	if (!HasLoadedRunForTravel() || !GameMode.HasAuthority() || GameMode.GetWorld() != GetGameInstance()->GetWorld())
	{
		return true;
	}
	UWorld* World = GameMode.GetWorld();
	FText CheckpointFailure;
	if (!PendingRestoreSaveGame->bHasWorldSnapshot || PendingRestoreSaveGame->bHasInventoryCheckpoint)
	{
		if (!PendingRestoreSaveGame->bHasWorldSnapshot)
		{
			TArray<FCatSavedWorldInventory> Initial;
			if (!CaptureWorldInventories(*World, Initial, CheckpointFailure))
			{ RejectPendingRestore(CheckpointFailure); return false; }
		}
		else
		{
			auto* Shop = World->GetSubsystem<UCatShopEconomyService>();
			auto* State = World->GetGameState<ACatfishingGameState>();
			if (!ValidateLoadedRunSaveGame(*PendingRestoreSaveGame, ActiveSlotId, CheckpointFailure)
				|| !RestoreWorldInventories(*World, PendingRestoreSaveGame->WorldInventories, CheckpointFailure)
				|| !Shop || !Shop->RestoreWalletFromAuthority(PendingRestoreSaveGame->TeamWalletBalance)
				|| !GameMode.RestoreWorldProgressFromSave(PendingRestoreSaveGame->WorldProgress, PendingRestoreSaveGame->DayIndex)
				|| !State || !State->GetRunFishCollection()->RestoreCapturesFromAuthority(PendingRestoreSaveGame->RunFishCollectionCaptures))
			{
				RejectPendingRestore(CheckpointFailure.IsEmpty() ? FText::FromString(TEXT("世界断点恢复失败，未开放玩法。")) : CheckpointFailure);
				return false;
			}
		}
		bWorldRestoreApplied = true;
		bLoadedRunForTravel = false;
		ActiveRunBasePlayedDurationSeconds = PendingRestoreSaveGame->PlayedDurationSeconds;
		ActiveRunStartedWorldSeconds = World->GetTimeSeconds();
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_checkpoint_restored Slot=%s World=%s NetMode=%d Authority=1 Hosts=%d WorldProgress=%d Wallet=%d"),
			*ActiveSlotId.ToString(), *World->GetName(), World->GetNetMode(), PendingRestoreSaveGame->WorldInventories.Num(),
			GameMode.GetRunPublicState().WorldProgress, PendingRestoreSaveGame->TeamWalletBalance);
		return true;
	}
	// 旧 v6 单仓/旧服务快照的兼容读取；新保存不再生产这两个旧载荷。
	ACatCampInventoryActor* CampInventory = nullptr;
	// 旧文件没有宿主名：沿原 CampHub.PublicInventory 绑定定位原仓，新公共架／公库不参与猜测。
	for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
	{
		auto* Bound = It->ResolvePublicInventoryForShopOrder();
		if (!Bound) continue;
		if (CampInventory && CampInventory != Bound)
		{ RejectPendingRestore(FText::FromString(TEXT("旧档公共仓库绑定不唯一。"))); return false; }
		CampInventory = Bound;
	}
	if (!CampInventory)
	{
		for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
		{
			if (CampInventory)
			{ RejectPendingRestore(FText::FromString(TEXT("旧档缺少 CampHub.PublicInventory 迁移绑定。"))); return false; }
			CampInventory = *It;
		}
	}
	UCatFishContainerService* FishContainers = World ? World->GetSubsystem<UCatFishContainerService>() : nullptr;
	const TArray<FCatSavedRunInventorySlot>& SavedCampInventory = PendingRestoreSaveGame->CampInventory.InventorySlots;
	FText Failure;
	if (!CampInventory || !FishContainers)
	{
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("营地或世界鱼容器尚未就绪。")) : Failure);
		return false;
	}
	TArray<FCatInventoryEntry> PreviousCampInventory;
	TArray<FCatInventoryEntry> RestoredCampEntries;
	if (!CampInventory->ExportInventorySlotsFromAuthority(PreviousCampInventory, Failure)
		|| !PrepareInventoryEntriesFromSave(SavedCampInventory, *CampInventory->GetInventoryComponent(), RestoredCampEntries, Failure)
		|| !CampInventory->RestoreInventorySlotsFromAuthority(RestoredCampEntries, Failure))
	{
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("领域恢复提交被拒绝，已阻止继续进入玩法。")) : Failure);
		return false;
	}
	if (!FishContainers->RestorePersistedWorldFishContainers(PendingRestoreSaveGame->WorldFishContainers))
	{
		FText RollbackFailure;
		const bool bCampRolledBack = CampInventory->RestoreInventorySlotsFromAuthority(PreviousCampInventory, RollbackFailure);
		// 鱼容器服务已关闭写口且可能已销毁部分宿主；只回滚可恢复的营地，不把这个 World 宣称为可继续游玩的原状态。
		UE_LOG(LogCatRun, Error, TEXT("Event=persistence_world_restore_aborted Slot=%s CampRolledBack=%d FishContainersUsable=0"),
			*ActiveSlotId.ToString(), bCampRolledBack);
		RejectPendingRestore(FText::FromString(TEXT("世界鱼容器提交失败，已尝试回滚并阻止继续进入玩法。")));
		return false;
	}
	// 公共板子跟世界槽恢复；启动前一次提交，已预检的旧档空数组也可接受，绝不读取个人图鉴补页。
	const ACatfishingGameState* CollectionGameState = World->GetGameState<ACatfishingGameState>();
	UCatRunFishCollectionComponent* RunCollection = CollectionGameState ? CollectionGameState->GetRunFishCollection() : nullptr;
	if (!RunCollection || !RunCollection->RestoreCapturesFromAuthority(PendingRestoreSaveGame->RunFishCollectionCaptures))
	{
		RejectPendingRestore(FText::FromString(TEXT("局内公共图鉴恢复失败，已阻止继续进入玩法。")));
		return false;
	}
	bWorldRestoreApplied = true;
	bLoadedRunForTravel = false;
	ActiveRunBasePlayedDurationSeconds = PendingRestoreSaveGame->PlayedDurationSeconds;
	ActiveRunStartedWorldSeconds = World->GetTimeSeconds();
	LastResultText = FText::FromString(TEXT("共享世界已恢复，正在等待玩家角色恢复。"));
	OnChanged.Broadcast();
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_world_restore_committed Slot=%s PlayerSnapshot=%d Containers=%d"),
		*ActiveSlotId.ToString(), PendingRestoreSaveGame->bHasPlayerSnapshot,
		PendingRestoreSaveGame->WorldFishContainers.Num());
	return true;
}

// 玩家恢复流程：
// 1. 先清除同连接的离开捕获标记，并确认共享世界已经恢复；未进入恢复阶段时直接放行。
// 2. 新槽没有玩家快照时按营地出生；正式读档只读取反序列化得到的 PlayerSnapshot。
// 3. 只有本机 Controller 会消费这份本地快照；随后交给 Equipment 恢复入口内部校验并提交，再传送角色位置并记录实际落点。
bool UCatSaveSubsystem::RestorePlayerAfterSpawn(AController& Controller, ACatCharacter& Character)
{
	CapturedDepartingControllers.Remove(&Controller);
	if (!bWorldRestoreApplied || !PendingRestoreSaveGame)
	{
		return true;
	}
	if (!PendingRestoreSaveGame->bHasWorldSnapshot)
	{
		UE_LOG(LogCatRun, Log,
			TEXT("Event=persistence_player_restore_skipped Slot=%s Controller=%s Character=%s Reason=NewWorldNoSnapshot"),
			*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(&Character));
		return true;
	}
	const APlayerController* PlayerController = Cast<APlayerController>(&Controller);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		UE_LOG(LogCatRun, Log,
			TEXT("Event=persistence_player_restore_skipped Slot=%s Controller=%s Character=%s Reason=NotLocalController"),
			*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(&Character));
		return true;
	}
	if (!PendingRestoreSaveGame->bHasPlayerSnapshot)
	{
		UE_LOG(LogCatRun, Warning,
			TEXT("Event=persistence_player_restore_rejected Slot=%s Character=%s Reason=MissingPlayerSnapshot"),
			*ActiveSlotId.ToString(), *GetNameSafe(&Character));
		RejectPendingRestore(FText::FromString(TEXT("世界存档缺少本机玩家快照。")));
		return false;
	}
	if (bLocalPlayerRestoredInCurrentWorld)
	{
		return true;
	}
	UCatEquipmentComponent* Equipment = Character.GetEquipmentComponent();
	UCatPhysicalBodyComponent* PhysicalBody = Character.GetPhysicalBodyComponent();
	UCatInventoryComponent* Inventory = Character.GetInventoryComponent();
	const FCatSavedPlayerRunState& SavedPlayer = PendingRestoreSaveGame->PlayerSnapshot;
	const FCatEquipmentLoadoutSnapshot SavedEquipmentSnapshot = ToRuntimeEquipment(SavedPlayer.EquipmentSnapshot);
	const TArray<FCatSavedRunInventorySlot>& SavedInventorySlots = SavedPlayer.InventorySlots;
	FText Failure;
	if (!PhysicalBody || !PhysicalBody->GetBody() || !Equipment || !Inventory || SavedPlayer.CharacterTransform.ContainsNaN())
	{
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("玩家库存或位置快照无效。")) : Failure);
		return false;
	}
	TArray<FCatInventoryEntry> RestoredEntries;
	if (!PrepareInventoryEntriesFromSave(SavedInventorySlots, *Inventory, RestoredEntries, Failure)
		|| !Inventory->RestoreInventorySlotsFromAuthority(
		RestoredEntries, ResolvePlayerSnapshotInventorySlotCapacity(), Failure))
	{
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("玩家随身库存恢复提交被拒绝。")) : Failure);
		return false;
	}
	if (!Equipment->RestoreSnapshotFromAuthority(SavedEquipmentSnapshot, Failure))
	{
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("玩家钓具选择恢复提交被拒绝。")) : Failure);
		return false;
	}
	const FTransform RestoredTransform = SavedPlayer.CharacterTransform;
	if (!PhysicalBody->TeleportBodyFromAuthority(RestoredTransform, TEXT("SavedRunRestore")))
	{
		RejectPendingRestore(FText::FromString(TEXT("玩家物理身体位置恢复被拒绝。")));
		return false;
	}
	const FTransform AppliedTransform = Character.GetActorTransform();
	bLocalPlayerRestoredInCurrentWorld = true;
	UE_LOG(LogCatRun, Log,
		TEXT("Event=persistence_player_restore_committed Slot=%s Character=%s SavedTransform=%s AppliedTransform=%s"),
		*ActiveSlotId.ToString(), *GetNameSafe(&Character), *RestoredTransform.ToHumanReadableString(),
		*AppliedTransform.ToHumanReadableString());
	return true;
}

// 退出捕获流程：先核对活动 World 和本机 Controller；解除占有通知提供尚未销毁的 Character，Logout 无 Pawn 时必须命中同连接的捕获记录。
// 成功后直接覆盖当前 SaveGame 对象里的 PlayerSnapshot；失败锁住当前槽的后续写盘，绝不把过时库存当成末次状态。
bool UCatSaveSubsystem::CapturePlayerBeforeLogout(AController& Controller, ACatCharacter* DepartingCharacter)
{
	if (!bWorldRestoreApplied || !PendingRestoreSaveGame)
	{
		return true;
	}
	// 旧档尚未应用到新 Pawn 时，RestartPlayer 的临时解除占有不能采集默认库存覆盖读入载荷。
	// 新世界没有待读档的玩家状态，仍允许正常捕获；已完成恢复的角色才成为后续采样事实源。
	if (PendingRestoreSaveGame->bHasWorldSnapshot && !bLocalPlayerRestoredInCurrentWorld)
	{
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_player_departure_skipped Slot=%s Controller=%s World=%s Reason=InitialRestorePending"),
			*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(GetWorld()));
		return true;
	}
	const APlayerController* PlayerController = Cast<APlayerController>(&Controller);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		UE_LOG(LogCatRun, Log,
			TEXT("Event=persistence_player_departure_skipped Slot=%s Controller=%s World=%s Reason=NotLocalController"),
			*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(GetWorld()));
		return true;
	}
	ACatCharacter* Character = DepartingCharacter ? DepartingCharacter : Cast<ACatCharacter>(Controller.GetPawn());
	if (!Character && CapturedDepartingControllers.Contains(&Controller))
	{
		return true;
	}
	APlayerState* PlayerState = Controller.PlayerState;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	UCatInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	FCatEquipmentLoadoutSnapshot EquipmentSnapshot;
	TArray<FCatInventoryEntry> InventorySlots;
	FText Failure;
	if (!Controller.HasAuthority() || Controller.GetWorld() != GetWorld() || !PlayerState
		|| !Equipment || !Inventory || Character->GetActorTransform().ContainsNaN()
		|| !Inventory->ExportInventorySlotsFromAuthority(
			InventorySlots, ResolvePlayerSnapshotInventorySlotCapacity(), Failure)
		|| !Equipment->ExportSnapshotFromAuthority(EquipmentSnapshot, Failure))
	{
		PlayerCaptureFailure = Failure.IsEmpty() ? FText::FromString(TEXT("退出玩家末次库存或位置未能捕获，已停止覆盖世界存档。")) : Failure;
		LastResultText = PlayerCaptureFailure;
		UE_LOG(LogCatRun, Error, TEXT("Event=persistence_player_departure_rejected Slot=%s Controller=%s World=%s Reason=%s"),
			*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(GetWorld()), *PlayerCaptureFailure.ToString());
		OnChanged.Broadcast();
		return false;
	}
	FCatSavedPlayerRunState CapturedPlayer;
	CapturedPlayer.EquipmentSnapshot = ToSavedEquipment(EquipmentSnapshot);
	if (!WriteSavedInventorySlots(InventorySlots, CapturedPlayer.InventorySlots, PlayerCaptureFailure))
	{
		LastResultText = PlayerCaptureFailure;
		OnChanged.Broadcast();
		return false;
	}
	CapturedPlayer.CharacterTransform = Character->GetActorTransform();
	if (!Equipment->RetireDeploymentAfterPersistentCapture(*PlayerState))
	{
		PlayerCaptureFailure = FText::FromString(TEXT("退出玩家的部署实物未能收口，已停止覆盖世界存档。"));
		LastResultText = PlayerCaptureFailure;
		OnChanged.Broadcast();
		return false;
	}
	PendingRestoreSaveGame->bHasPlayerSnapshot = true;
	PendingRestoreSaveGame->PlayerSnapshot = CapturedPlayer;
	CapturedDepartingControllers.Add(&Controller);
	bLocalPlayerRestoredInCurrentWorld = false;
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_player_departure_captured Slot=%s Controller=%s World=%s InventorySlots=%d Transform=%s"),
		*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(GetWorld()),
		InventorySlots.Num(), *CapturedPlayer.CharacterTransform.ToHumanReadableString());
	return true;
}

// 文件名构造流程：入口已校验 SlotId 为 32 位十六进制 GUID，固定前缀把世界槽与 Profile 等其他 SaveGame 隔离。
FString UCatSaveSubsystem::MakeRunSlotFileName(const FName SlotId)
{
	return FString::Printf(TEXT("CatRun_%s"), *SlotId.ToString());
}

// 受理结果构造流程：每次入口立即分配关联 ID 并更新前端可读结果，异步磁盘成败仍由后续回调覆盖这条文本。
FCatSaveResult UCatSaveSubsystem::MakeResult(const bool bAccepted, const FText& Message)
{
	FCatSaveResult Result;
	Result.RequestId = FGuid::NewGuid();
	Result.bAccepted = bAccepted;
	Result.Message = Message;
	LastResultText = Message;
	OnChanged.Broadcast();
	return Result;
}

// 运行载荷采集流程：
// 1. 只接受 authority GameMode 和已恢复的玩法 World；多仓按宿主身份导出，客户端或前端不写磁盘。
// 2. 世界槽只保留一个本机玩家快照；若有在线本机 Controller 就现场采样，否则沿用退出捕获已经写入内存的快照。
// 3. 最后导出已提交世界鱼与真实 Run 展示元数据，并读取终局原因把「已完结」写进载荷；Profile 不进入世界载荷，Run 世界进度与公款由各自权威恢复入口消费。
bool UCatSaveSubsystem::BuildActiveRunSaveGame(UCatRunSaveGame& OutSaveGame, FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	ACatfishingGameModeBase* GameMode = World ? Cast<ACatfishingGameModeBase>(World->GetAuthGameMode()) : nullptr;
	if (!World || !GameMode || !GameMode->HasAuthority() || !bWorldRestoreApplied || !PendingRestoreSaveGame)
	{
		OutFailure = FText::FromString(TEXT("当前不是已恢复的房主玩法世界。"));
		return false;
	}
	if (!PlayerCaptureFailure.IsEmpty())
	{
		OutFailure = PlayerCaptureFailure;
		return false;
	}
	if (!CaptureWorldInventories(*World, OutSaveGame.WorldInventories, OutFailure)) return false;
	const auto* Shop = World->GetSubsystem<UCatShopEconomyService>();
	if (!Shop || !Shop->ExportWalletFromAuthority(OutSaveGame.TeamWalletBalance))
	{ OutFailure = FText::FromString(TEXT("公款尚未就绪。")); return false; }
	OutSaveGame.bHasInventoryCheckpoint = true;
	OutSaveGame.bHasPlayerSnapshot = PendingRestoreSaveGame->bHasPlayerSnapshot;
	OutSaveGame.PlayerSnapshot = PendingRestoreSaveGame->PlayerSnapshot;
	OutSaveGame.WorldFishContainers.Reset();
	OutSaveGame.CampInventory = FCatSavedCampInventory();
	OutSaveGame.FormatVersion = OutSaveGame.GetLatestDataVersion();
	OutSaveGame.bHasWorldSnapshot = true;
	const ACatfishingGameState* CollectionGameState = World->GetGameState<ACatfishingGameState>();
	const UCatRunFishCollectionComponent* RunCollection = CollectionGameState ? CollectionGameState->GetRunFishCollection() : nullptr;
	if (!RunCollection)
	{
		OutFailure = FText::FromString(TEXT("局内公共图鉴宿主未就绪，不能保存不完整的世界断点。"));
		return false;
	}
	OutSaveGame.RunFishCollectionCaptures = RunCollection->GetCapturesForWorldSave();
	OutSaveGame.SlotId = ActiveSlotId;
	if (const FCatSaveSlotSummary* Summary = SlotSummaries.FindByPredicate(
		[this](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == ActiveSlotId; }))
	{
		OutSaveGame.DisplayName = Summary->DisplayName;
	}
	OutSaveGame.LastSavedAt = FDateTime::UtcNow();
	APlayerController* ControllerToSave = nullptr;
	for (TActorIterator<APlayerController> It(World); It; ++It)
	{
		APlayerController* Controller = *It;
		if (!Controller->IsLocalController() || !GameMode->IsControllerActive(Controller)
			|| CapturedDepartingControllers.Contains(Controller))
		{
			continue;
		}
		ControllerToSave = Controller;
		break;
	}
	if (ControllerToSave)
	{
		ACatCharacter* Character = Cast<ACatCharacter>(ControllerToSave->GetPawn());
		if (!Character || !Character->GetEquipmentComponent() || !Character->GetInventoryComponent())
		{
			OutFailure = FText::FromString(TEXT("本机玩家缺少可持久化的角色或钓具库存状态。"));
			return false;
		}
		FCatEquipmentLoadoutSnapshot EquipmentSnapshot;
		TArray<FCatInventoryEntry> PlayerInventorySlots;
		if (!Character->GetInventoryComponent()->ExportInventorySlotsFromAuthority(
			PlayerInventorySlots, ResolvePlayerSnapshotInventorySlotCapacity(), OutFailure))
		{
			return false;
		}
		if (!Character->GetEquipmentComponent()->ExportSnapshotFromAuthority(EquipmentSnapshot, OutFailure))
		{
			return false;
		}
		FCatSavedPlayerRunState& SavedPlayer = OutSaveGame.PlayerSnapshot;
		SavedPlayer.EquipmentSnapshot = ToSavedEquipment(EquipmentSnapshot);
		if (!WriteSavedInventorySlots(PlayerInventorySlots, SavedPlayer.InventorySlots, OutFailure))
		{
			return false;
		}
		SavedPlayer.CharacterTransform = Character->GetActorTransform();
		OutSaveGame.bHasPlayerSnapshot = true;
	}
	else if (!OutSaveGame.bHasPlayerSnapshot)
	{
		OutFailure = FText::FromString(TEXT("本机玩家快照尚未写入，不能覆盖世界存档。"));
		return false;
	}
	const FCatRunPublicState& RunPublicState = GameMode->GetRunPublicState();
	OutSaveGame.DayIndex = RunPublicState.Phase.DayIndex;
	OutSaveGame.LocationName = FPackageName::GetShortName(World->GetMapName());
	OutSaveGame.PlayedDurationSeconds = ActiveRunBasePlayedDurationSeconds
		+ (ActiveRunStartedWorldSeconds >= 0.0 ? FMath::Max(0.0, World->GetTimeSeconds() - ActiveRunStartedWorldSeconds) : 0.0);
	OutSaveGame.LastOfferingPoints = RunPublicState.LastOfferingPoints;
	OutSaveGame.DailyOfferingTarget = RunPublicState.DailyOfferingTarget;
	OutSaveGame.WorldProgress = RunPublicState.WorldProgress;
	OutSaveGame.LastWorldProgressDelta = RunPublicState.LastWorldProgressDelta;
	// 摘要与恢复共读正式鱼库存，不再从未注册现行鱼缸的旧服务推导点数。
	OutSaveGame.TankOfferingPoints = INDEX_NONE;
	int64 TankPoints = 0;
	bool bHasTank = false;
	bool bPointsValid = true;
	for (const auto& Inventory : OutSaveGame.WorldInventories)
	{
		if (Inventory.CapacityTier == INDEX_NONE) continue;
		bHasTank = true;
		for (const auto& Fish : Inventory.InventorySlots)
		{
			if (!Fish.ItemInstanceId.IsValid()) continue;
			ECatOfferingWeightClass WeightClass;
			int32 Points = 0;
			if (!GetDefault<UCatRunSettings>()->TryClassifyOfferingWeight(Fish.FishWeightKilograms, WeightClass, Points))
			{ bPointsValid = false; break; }
			TankPoints += Points;
		}
	}
	if (bHasTank && bPointsValid && TankPoints <= MAX_int32) OutSaveGame.TankOfferingPoints = int32(TankPoints);
	// 终局只认两个原因：进度归零＝团灭、Success＝毕业。房主退出（HostExit）是可续的局中断点，不是终局；
	// StartupFailed 与 None 同理。标记只增不减，终局那一夜之后的任何一次写盘都不会把槽变回「可继续」。
	const bool bTerminalRun = RunPublicState.EndReason == ECatRunEndReason::WorldProgressDepleted
		|| RunPublicState.EndReason == ECatRunEndReason::Success;
	OutSaveGame.bRunCompleted = PendingRestoreSaveGame->bRunCompleted || bTerminalRun;
	if (OutSaveGame.bRunCompleted)
	{
		UE_LOG(LogCatRun, Log,
			TEXT("Event=persistence_run_completed Slot=%s World=%s EndReason=%d Phase=%d Day=%d WorldProgress=%d"),
			*ActiveSlotId.ToString(), *GetNameSafe(World), static_cast<int32>(RunPublicState.EndReason),
			static_cast<int32>(RunPublicState.Phase.Phase), RunPublicState.Phase.DayIndex, RunPublicState.WorldProgress);
	}
	return ValidateLoadedRunSaveGame(OutSaveGame, ActiveSlotId, OutFailure);
}

// 载荷验证流程：
// 1. 先核对格式版本、槽归属、显示字段和数值边界；只接受当前格式或对象生命周期明确迁移过的旧档。
// 2. 新建空槽只能没有世界快照和玩家快照，正式世界快照必须带一个可反序列化的本机玩家状态。
// 3. 玩家域分别验证随身库存和钓具选择，恢复阶段也按 InventoryComponent、Equipment 的顺序消费。
// 4. 最后继续检查营地和世界鱼实例唯一性；领域容量、定义和容器键仍会由对应恢复入口内部裁决。
bool UCatSaveSubsystem::ValidateLoadedRunSaveGame(const UCatRunSaveGame& SaveGame, const FName ExpectedSlotId,
	FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	const FString TrimmedDisplayName = SaveGame.DisplayName.TrimStartAndEnd();
	if (SaveGame.FormatVersion != SaveGame.GetLatestDataVersion()
		|| (SaveGame.WasLoaded() && SaveGame.GetSavedDataVersion() != 0
			&& SaveGame.GetSavedDataVersion() != SaveGame.GetLatestDataVersion()) || SaveGame.SlotId != ExpectedSlotId
		|| TrimmedDisplayName.IsEmpty() || TrimmedDisplayName.Len() > 64
		|| !IsValidSlotId(SaveGame.SlotId) || !FMath::IsFinite(SaveGame.PlayedDurationSeconds)
		|| SaveGame.PlayedDurationSeconds < 0.0 || SaveGame.DayIndex < 0 || SaveGame.LastOfferingPoints < 0
		|| SaveGame.DailyOfferingTarget < 0 || SaveGame.WorldProgress < 0 || SaveGame.WorldProgress > 100
		|| SaveGame.LastWorldProgressDelta < -100 || SaveGame.LastWorldProgressDelta > 100)
	{
		OutFailure = FText::FromString(TEXT("存档版本、槽归属或展示元数据无效。"));
		return false;
	}
	// 墓碑（2026-09-14，T33；局与进程.md:103）：首个完整快照之前也可能终局；空槽允许仅持有完成位，仍禁止半套世界/玩家数据。
	if (!SaveGame.bHasWorldSnapshot && (SaveGame.bHasPlayerSnapshot || !SaveGame.WorldFishContainers.IsEmpty()
		|| !SaveGame.CampInventory.InventorySlots.IsEmpty() || !SaveGame.RunFishCollectionCaptures.IsEmpty()
		|| SaveGame.bHasInventoryCheckpoint || !SaveGame.WorldInventories.IsEmpty()))
	{
		OutFailure = FText::FromString(TEXT("没有完整世界快照的槽夹带世界或玩家数据，不能按新局进入。"));
		return false;
	}
	if (SaveGame.bHasWorldSnapshot && !SaveGame.bHasPlayerSnapshot)
	{
		OutFailure = FText::FromString(TEXT("世界存档缺少本机玩家快照。"));
		return false;
	}
	if (!UCatRunFishCollectionComponent::ValidateCaptures(SaveGame.RunFishCollectionCaptures))
	{
		OutFailure = FText::FromString(TEXT("局内公共图鉴的捕获身份或爪印记录无效。"));
		return false;
	}
	TSet<FGuid> SeenItemInstanceIds;
	if (SaveGame.bHasPlayerSnapshot)
	{
		const FCatSavedPlayerRunState& SavedPlayer = SaveGame.PlayerSnapshot;
		const FCatEquipmentLoadoutSnapshot SavedEquipmentSnapshot = ToRuntimeEquipment(SavedPlayer.EquipmentSnapshot);
		const TArray<FCatSavedRunInventorySlot>& SavedPlayerInventory = SavedPlayer.InventorySlots;
		if (SavedPlayer.CharacterTransform.ContainsNaN()
			|| !ValidateSavedEquipmentSnapshot(SavedEquipmentSnapshot, SavedPlayerInventory, OutFailure))
		{
			OutFailure = FText::FromString(TEXT("存档包含无效的本机玩家记录与位置。"));
			return false;
		}
		for (const FCatSavedRunInventorySlot& Slot : SavedPlayerInventory)
		{
			if (Slot.ItemInstanceId.IsValid() && SeenItemInstanceIds.Contains(Slot.ItemInstanceId))
			{
				OutFailure = FText::FromString(TEXT("存档包含玩家快照内部重复的物品实例。"));
				return false;
			}
			if (Slot.ItemInstanceId.IsValid())
			{
				SeenItemInstanceIds.Add(Slot.ItemInstanceId);
			}
		}
	}
	if (SaveGame.bHasInventoryCheckpoint)
	{
		if (SaveGame.TeamWalletBalance < 0 || SaveGame.TeamWalletBalance > 16777216
			|| !SaveGame.CampInventory.InventorySlots.IsEmpty() || !SaveGame.WorldFishContainers.IsEmpty()) return false;
		TSet<FName> Names;
		for (const auto& Saved : SaveGame.WorldInventories)
		{
			if (Names.Contains(Saved.HostName) || !ValidateWorldInventory(Saved, OutFailure)) return false;
			Names.Add(Saved.HostName);
			for (const auto& Slot : Saved.InventorySlots)
			{
				if (!Slot.ItemInstanceId.IsValid()) continue;
				if (SeenItemInstanceIds.Contains(Slot.ItemInstanceId))
				{ OutFailure = FText::FromString(TEXT("世界库存与玩家之间有重复物品身份。")); return false; }
				SeenItemInstanceIds.Add(Slot.ItemInstanceId);
			}
		}
		TSet<FName> ClaimedGuards;
		const auto ValidateGuardLinks = [&](const TArray<FCatSavedRunInventorySlot>& Slots)
		{
			for (const auto& Slot : Slots)
			{
				if (Slot.FishGuardHostName.IsNone()) continue;
				const auto* Host = SaveGame.WorldInventories.FindByPredicate(
					[&](const auto& Candidate) { return Candidate.HostName == Slot.FishGuardHostName; });
				if (!Host || !Host->HostClass.Get()->IsChildOf(ACatFishGuardActor::StaticClass())
					|| ClaimedGuards.Contains(Slot.FishGuardHostName)) return false;
				ClaimedGuards.Add(Slot.FishGuardHostName);
			}
			return true;
		};
		if (!ValidateGuardLinks(SaveGame.PlayerSnapshot.InventorySlots)) return false;
		for (const auto& Host : SaveGame.WorldInventories)
			if (!ValidateGuardLinks(Host.InventorySlots)) return false;
	}
	else if (!SaveGame.WorldInventories.IsEmpty()) return false;
	const TArray<FCatSavedRunInventorySlot>& SavedCampInventory = SaveGame.CampInventory.InventorySlots;
	if (!ValidateSavedInventorySlots(SavedCampInventory, OutFailure))
	{
		return false;
	}
	for (const FCatSavedRunInventorySlot& Slot : SavedCampInventory)
	{
		if (Slot.ItemInstanceId.IsValid() && SeenItemInstanceIds.Contains(Slot.ItemInstanceId))
		{
			OutFailure = FText::FromString(TEXT("存档包含营地与玩家之间重复的物品实例。"));
			return false;
		}
		if (Slot.ItemInstanceId.IsValid())
		{
			SeenItemInstanceIds.Add(Slot.ItemInstanceId);
		}
	}
	TSet<FGuid> SeenFishInstanceIds;
	for (const FCatPersistentContainerSnapshot& Container : SaveGame.WorldFishContainers)
	{
		for (const FCatFishInstance& Fish : Container.Fish)
		{
			if (Fish.FishInstanceId.IsValid())
			{
				if (SeenFishInstanceIds.Contains(Fish.FishInstanceId) || SeenItemInstanceIds.Contains(Fish.FishInstanceId))
				{
					OutFailure = FText::FromString(TEXT("存档含有背包、营地或世界容器之间重复的鱼实例。"));
					return false;
				}
				SeenFishInstanceIds.Add(Fish.FishInstanceId);
			}
		}
	}
	return true;
}

// Run 写回流程：
// 1. 只接受当前 busy 请求和匹配 SlotId 的异步回调，迟到或串槽结果不更新目录。
// 2. 写入成功后从仍被强引用的候选 Run 对象重建摘要，摘要来源与刚写入对象一致。
// 3. 日志带出本次写入的玩家位置，再按同一请求 ID 发布完成通知，让 Online 和 UI 继续复用原来的等待协议。
void UCatSaveSubsystem::HandleRunSaved(UCatRunSaveGame* SavedGame, const bool bSuccess, const FGuid RequestId)
{
	if (!bBusy)
	{
		return;
	}
	const FName SlotId = SavedGame ? SavedGame->SlotId : NAME_None;
	const FString SlotName = SavedGame ? SavedGame->GetSaveSlotName() : FString();
	if (!bSuccess || !SavedGame || SavedGame != ActiveAsyncRunSaveGame
		|| SavedGame->GetLocalPlayer() != GetGameInstance()->GetFirstGamePlayer()
		|| SlotName != MakeRunSlotFileName(SlotId))
	{
		FinishDiskRequest(RequestId, false, FText::FromString(TEXT("世界存档文件写入失败，请重试；本次保存未确认成功。")));
		return;
	}
	FText Failure;
	if (!ValidateLoadedRunSaveGame(*ActiveAsyncRunSaveGame, SlotId, Failure))
	{
		FinishDiskRequest(RequestId, false, Failure.IsEmpty()
			? FText::FromString(TEXT("世界存档文件写入后校验失败。")) : Failure);
		return;
	}
	FCatSaveSlotSummary* Existing = SlotSummaries.FindByPredicate(
		[SlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == SlotId; });
	FCatSaveSlotSummary& Summary = Existing ? *Existing : SlotSummaries.AddDefaulted_GetRef();
	Summary = MakeSummary(*ActiveAsyncRunSaveGame);
	Summary.bRunCompleted |= CompletedRunSlots.Contains(SlotId);
	SlotSummaries.Sort([](const FCatSaveSlotSummary& Left, const FCatSaveSlotSummary& Right)
	{
		return Left.LastSavedAt > Right.LastSavedAt;
	});
	bSlotDirectoryLoaded = true;
	const FString SavedPlayerTransformText = ActiveAsyncRunSaveGame->bHasPlayerSnapshot
		? ActiveAsyncRunSaveGame->PlayerSnapshot.CharacterTransform.ToHumanReadableString()
		: FString(TEXT("None"));
	UE_LOG(LogCatRun, Log,
		TEXT("Event=persistence_slot_write_completed RequestId=%s Slot=%s File=%s Verified=1 HasWorld=%d HasPlayer=%d PlayerTransform=%s"),
		*RequestId.ToString(), *SlotId.ToString(), *SlotName, ActiveAsyncRunSaveGame->bHasWorldSnapshot,
		ActiveAsyncRunSaveGame->bHasPlayerSnapshot, *SavedPlayerTransformText);
	FinishDiskRequest(RequestId, true, FText::FromString(TEXT("世界存档文件已提交。")));
}

// 世界槽读取回调流程：只处理当前 busy 请求；读到真实 Run 类型且通过版本、槽归属和实例唯一性检查后才持有载荷并开放旅行。
// 摘要改用实际载荷观测值，恢复标记、玩家捕获状态和计时基线同时重置；日志带出反序列化得到的玩家位置。
void UCatSaveSubsystem::HandleRunLoaded(ULocalPlayerSaveGame* LoadedGame,
	const FGuid RequestId, const FName RequestedSlotId)
{
	if (!bBusy)
	{
		return;
	}
	UCatRunSaveGame* LoadedRunSaveGame = Cast<UCatRunSaveGame>(LoadedGame);
	FText Failure;
	const FString SlotName = LoadedGame ? LoadedGame->GetSaveSlotName() : FString();
	if (!LoadedGame || LoadedGame->GetLocalPlayer() != GetGameInstance()->GetFirstGamePlayer()
		|| SlotName != MakeRunSlotFileName(RequestedSlotId))
	{
		bBusy = false;
		RejectPendingRestore(FText::FromString(TEXT("世界存档读取回调与请求槽不匹配。")));
		return;
	}
	if (!LoadedRunSaveGame || !LoadedRunSaveGame->WasLoaded() || !ValidateLoadedRunSaveGame(*LoadedRunSaveGame, RequestedSlotId, Failure))
	{
		bBusy = false;
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("世界存档读取或校验失败。")) : Failure);
		return;
	}
	// 实际载荷是最终依据；目录缓存之外的完成标记同样不得授予恢复/旅行许可。
	if (LoadedRunSaveGame->bRunCompleted || CompletedRunSlots.Contains(RequestedSlotId))
	{
		bBusy = false;
		CompletedRunSlots.Add(RequestedSlotId);
		for (auto& Summary : SlotSummaries)
			if (Summary.SlotId == RequestedSlotId) Summary.bRunCompleted = true;
		RejectPendingRestore(FText::FromString(TEXT("这一局已经结束，请新建世界槽。")));
		return;
	}
	PendingRestoreSaveGame = LoadedRunSaveGame;
	if (FCatSaveSlotSummary* Summary = SlotSummaries.FindByPredicate(
		[RequestedSlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == RequestedSlotId; }))
	{
		*Summary = MakeSummary(*LoadedRunSaveGame);
	}
	ActiveSlotId = RequestedSlotId;
	bWorldRestoreApplied = false;
	bLoadedRunForTravel = true;
	bLocalPlayerRestoredInCurrentWorld = false;
	CapturedDepartingControllers.Reset();
	PlayerCaptureFailure = FText::GetEmpty();
	ActiveRunStartedWorldSeconds = -1.0;
	ActiveRunBasePlayedDurationSeconds = LoadedRunSaveGame->PlayedDurationSeconds;
	bBusy = false;
	LastResultText = FText::FromString(TEXT("存档已读取，等待房主进入玩法世界。"));
	OnChanged.Broadcast();
	const FString LoadedPlayerTransformText = LoadedRunSaveGame->bHasPlayerSnapshot
		? LoadedRunSaveGame->PlayerSnapshot.CharacterTransform.ToHumanReadableString()
		: FString(TEXT("None"));
	UE_LOG(LogCatRun, Log,
		TEXT("Event=persistence_slot_read_completed RequestId=%s Slot=%s File=%s HasWorld=%d HasPlayer=%d PlayerTransform=%s"),
		*RequestId.ToString(), *RequestedSlotId.ToString(), *SlotName, LoadedRunSaveGame->bHasWorldSnapshot,
		LoadedRunSaveGame->bHasPlayerSnapshot, *LoadedPlayerTransformText);
}

// 磁盘请求收口流程：先清候选对象和 busy，保留退出时更新的唯一玩家基线，再写结果与关联日志；最后广播 UI 和有效请求回执，回调可安全发起下一次请求。
void UCatSaveSubsystem::FinishDiskRequest(const FGuid RequestId, const bool bSuccess, const FText& Message)
{
	const FName CompletedSlot = RequestId == ActiveQueuedSaveRequest && RequestId.IsValid()
		? ActiveQueuedSaveSlot : (ActiveAsyncRunSaveGame ? ActiveAsyncRunSaveGame->SlotId : ActiveSlotId);
	if (RequestId.IsValid() && RequestId == ActiveQueuedSaveRequest)
	{
		if (bSuccess)
		{
			RetrySavePayloads.Remove(ActiveQueuedSaveSlot);
			if (ActiveAsyncRunSaveGame && ActiveAsyncRunSaveGame->bRunCompleted)
				PendingCompletionSlots.Remove(ActiveQueuedSaveSlot);
		}
		else
		{
			RetrySavePayloads.Add(ActiveQueuedSaveSlot, ActiveAsyncRunSaveGame);
			NextSaveRetrySeconds = FPlatformTime::Seconds() + 5.0;
			UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_retry_retained RequestId=%s Slot=%s World=%s CompletionPending=%d Reason=%s"),
				*RequestId.ToString(), *ActiveQueuedSaveSlot.ToString(), *GetNameSafe(GetWorld()),
				PendingCompletionSlots.Contains(ActiveQueuedSaveSlot), *Message.ToString());
		}
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_write_receipt RequestId=%s Slot=%s World=%s Success=%d CompletionPending=%d"),
			*RequestId.ToString(), *ActiveQueuedSaveSlot.ToString(), *GetNameSafe(GetWorld()), bSuccess,
			PendingCompletionSlots.Contains(ActiveQueuedSaveSlot));
		ActiveQueuedSaveRequest.Invalidate();
		ActiveQueuedSaveSlot = NAME_None;
	}

	ActiveAsyncRunSaveGame = nullptr;
	bBusy = false;
	LastResultText = Message;
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_request_completed RequestId=%s Success=%d Slot=%s World=%s Message=%s"),
		*RequestId.ToString(), bSuccess, *CompletedSlot.ToString(), *GetNameSafe(GetWorld()), *Message.ToString());
	OnChanged.Broadcast();
	if (RequestId.IsValid())
	{
		OnSaveCompleted.Broadcast(RequestId, bSuccess);
	}
}

// 待恢复拒绝流程：统一清掉旅行许可、已恢复标记和载荷强引用；Online 因而不能以失效读取结果发起玩法旅行或重复局部恢复。
void UCatSaveSubsystem::RejectPendingRestore(const FText& Failure)
{
	bLoadedRunForTravel = false;
	bWorldRestoreApplied = false;
	PendingRestoreSaveGame = nullptr;
	bLocalPlayerRestoredInCurrentWorld = false;
	ActiveRunStartedWorldSeconds = -1.0;
	ActiveRunBasePlayedDurationSeconds = 0.0;
	LastResultText = Failure;
	OnChanged.Broadcast();
	UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_restore_rejected Slot=%s Reason=%s"),
		*ActiveSlotId.ToString(), *Failure.ToString());
}
