#include "Save/CatSaveSubsystem.h"

#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"

#include "Async/Async.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
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
#include "Kismet/GameplayStatics.h"
#include "Logging/CatLog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	/** 世界槽文件名前缀；后面只拼稳定 SlotId，形成 UE SaveGame 的唯一槽名。 */
	constexpr const TCHAR* RunSlotFilePrefix = TEXT("CatRun_");
	/** UE SaveGame 本地文件扩展名；扫描目录时只接受这一类正式槽文件。 */
	constexpr const TCHAR* SaveGameFileExtension = TEXT(".sav");

	// Windows 本地槽预检流程：只读取 UE 文件标记；本项目 v5/v6 都是现代 GVAS 格式，不接受引擎的无标记旧格式回退。
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
		return Summary;
	}

	// 玩家钓具选择导出流程：只复制 Equipment 交出的选择和鱼竿摘要；随身库存由玩家运行状态单独保存。
	FCatSavedEquipmentLoadout ToSavedEquipment(const FCatEquipmentLoadoutSnapshot& Snapshot)
	{
		FCatSavedEquipmentLoadout Saved;
		Saved.Revision = Snapshot.Revision;
		Saved.RodDefinitionId = Snapshot.RodDefinitionId;
		Saved.RodItemInstanceId = Snapshot.RodItemInstanceId;
		Saved.BaitDefinitionId = Snapshot.BaitDefinitionId;
		Saved.BaitItemInstanceId = Snapshot.BaitItemInstanceId;
		Saved.FloatDefinitionId = Snapshot.FloatDefinitionId;
		Saved.FloatItemInstanceId = Snapshot.FloatItemInstanceId;
		Saved.ScoopNetDefinitionId = Snapshot.ScoopNetDefinitionId;
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
		Snapshot.RodDefinitionId = Saved.RodDefinitionId;
		Snapshot.RodItemInstanceId = Saved.RodItemInstanceId;
		Snapshot.BaitDefinitionId = Saved.BaitDefinitionId;
		Snapshot.BaitItemInstanceId = Saved.BaitItemInstanceId;
		Snapshot.FloatDefinitionId = Saved.FloatDefinitionId;
		Snapshot.FloatItemInstanceId = Saved.FloatItemInstanceId;
		Snapshot.ScoopNetDefinitionId = Saved.ScoopNetDefinitionId;
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
			const bool bOccupied = !Slot.DefinitionId.IsNone() && Slot.Quantity > 0;
			if (!bOccupied)
			{
				if (!Slot.DefinitionId.IsNone() || Slot.ItemInstanceId.IsValid() || Slot.Quantity != 0
					|| Slot.RodDurability != 0.0 || Slot.bRodBroken
					|| Slot.FishSessionId.IsValid() || Slot.FishWeightKilograms != 0.0 || !Slot.FishOwnerStableNetId.IsEmpty())
				{
					OutFailure = FText::FromString(TEXT("存档随身库存空格包含残留数据。"));
					return false;
				}
				continue;
			}
			const UCatInventoryItemDefinition* InventoryDefinition =
				InventorySettings->FindRuntimeDefinition(Slot.DefinitionId);
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
			Saved.DefinitionId = Entry.Instance->GetItemDefinitionId();
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
			if (Slot.DefinitionId.IsNone() || Slot.Quantity <= 0)
			{
				continue;
			}
			UCatInventoryItemDefinition* Definition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
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
		const auto HasSelectedInstance = [&InventorySlots, InventorySettings](const FName DefinitionId,
			const FGuid InstanceId, const FName ExpectedSlotId)
		{
			if (DefinitionId.IsNone())
			{
				return !InstanceId.IsValid();
			}
			return InstanceId.IsValid() && InventorySlots.ContainsByPredicate(
				[DefinitionId, InstanceId, ExpectedSlotId, InventorySettings](const FCatSavedRunInventorySlot& Slot)
				{
					const UCatEquipmentDefinition* Definition =
						Cast<UCatEquipmentDefinition>(InventorySettings->FindRuntimeDefinition(Slot.DefinitionId));
					return Slot.DefinitionId == DefinitionId && Slot.ItemInstanceId == InstanceId && Definition
						&& Definition->CanServeFishingLoadoutSlot(ExpectedSlotId);
				});
		};
		if (!HasSelectedInstance(Snapshot.RodDefinitionId, Snapshot.RodItemInstanceId,
				UCatEquipmentDefinition::FishingRodLoadoutSlotId())
			|| !HasSelectedInstance(Snapshot.BaitDefinitionId, Snapshot.BaitItemInstanceId,
				UCatEquipmentDefinition::FishingBaitLoadoutSlotId())
			|| !HasSelectedInstance(Snapshot.FloatDefinitionId, Snapshot.FloatItemInstanceId,
				UCatEquipmentDefinition::FishingFloatLoadoutSlotId())
			|| !HasSelectedInstance(Snapshot.ScoopNetDefinitionId, Snapshot.ScoopNetItemInstanceId,
				UCatEquipmentDefinition::ScoopNetLoadoutSlotId()))
		{
			OutFailure = FText::FromString(TEXT("存档钓具选择没有指向同一库存中的合法实例。"));
			return false;
		}
		const FCatSavedRunInventorySlot* SelectedRod = InventorySlots.FindByPredicate(
			[&Snapshot](const FCatSavedRunInventorySlot& Slot) { return Slot.ItemInstanceId == Snapshot.RodItemInstanceId; });
		if ((!Snapshot.RodDefinitionId.IsNone() && (!SelectedRod || Snapshot.RodDurability != SelectedRod->RodDurability
			|| Snapshot.bRodBroken != SelectedRod->bRodBroken))
			|| (Snapshot.RodDefinitionId.IsNone() && (Snapshot.RodItemInstanceId.IsValid()
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
}

// 反初始化流程：异步委托以 UObject 弱绑定自动失效；这里先清所有强引用和旅行许可，使失效 World 回调即使晚到也不能恢复到下一次游戏实例。
void UCatSaveSubsystem::Deinitialize()
{
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
	if (bBusy)
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
				LoadedSummaries.Add(MakeSummary(*SaveGame));
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
	if (!LocalPlayer || bBusy || !bSlotDirectoryLoaded || bWorldRestoreApplied || PendingRestoreSaveGame || TrimmedDisplayName.IsEmpty()
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
// 成功只建立待恢复快照和旅行许可，不从显示名或列表下标反推载荷；失败由回调写入结果文本并清理许可。
FCatSaveResult UCatSaveSubsystem::RequestLoadSlot(const FName SlotId)
{
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	if (!LocalPlayer || bBusy || !bSlotDirectoryLoaded || bWorldRestoreApplied || PendingRestoreSaveGame || !IsValidSlotId(SlotId)
		|| !SlotSummaries.ContainsByPredicate([SlotId](const FCatSaveSlotSummary& Summary) { return Summary.SlotId == SlotId; }))
	{
		return MakeResult(false, FText::FromString(TEXT("存档不存在、目录未就绪或正在执行其他操作。")));
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
	if (!LocalPlayer || bBusy || !bSlotDirectoryLoaded || SlotId == ActiveSlotId || !IsValidSlotId(SlotId)
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
		SlotSummaries.RemoveAll([SlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == SlotId; });
	}
	FinishDiskRequest(Result.RequestId, bDeleted, bDeleted
		? FText::FromString(TEXT("存档文件已删除。"))
		: FText::FromString(TEXT("存档文件删除失败，原目录已保留。")));
	return Result;
}

// 运行保存请求流程：
// 1. 目录终态不明时先安排重读并拒绝本次保存，调用方需要在目录完成后重新发起请求。
// 2. 目录、活动槽和摘要都就绪后，从 authority World 完整采集载荷；任何领域快照失败都只返回错误，不启动写盘。
// 3. 游戏线程采样成功后先合并玩家基线，再交给 UE 异步覆盖该槽唯一文件，回调负责发布保存完成通知。
FCatSaveResult UCatSaveSubsystem::RequestSaveActiveRun()
{
	if (!bBusy && !bSlotDirectoryLoaded)
	{
		RefreshSlotSummaries();
		return MakeResult(false, bBusy ? FText::FromString(TEXT("正在重读存档目录，请完成后重试保存。")) : LastResultText);
	}
	if (bBusy || !bSlotDirectoryLoaded || ActiveSlotId.IsNone())
	{
		return MakeResult(false, FText::FromString(TEXT("没有可保存的活动世界槽，或已有存档操作正在进行。")));
	}
	const FCatSaveSlotSummary* Summary = SlotSummaries.FindByPredicate(
		[this](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == ActiveSlotId; });
	if (!Summary)
	{
		return MakeResult(false, FText::FromString(TEXT("活动槽不在已提交目录中，请刷新存档目录。")));
	}
	const ULocalPlayer* LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	if (!LocalPlayer)
	{
		return MakeResult(false, FText::FromString(TEXT("本机玩家不可用，不能保存世界。")));
	}
	UCatRunSaveGame* SaveGame = CastChecked<UCatRunSaveGame>(
		ULocalPlayerSaveGame::CreateNewSaveGameForLocalPlayer(UCatRunSaveGame::StaticClass(),
			LocalPlayer, MakeRunSlotFileName(ActiveSlotId)));
	FText Failure;
	if (!BuildActiveRunSaveGame(*SaveGame, Failure))
	{
		return MakeResult(false, Failure);
	}
	// 游戏线程采样时更新内存快照；异步回调绝不把本轮之后的新采样覆盖回来，保证写盘途中发生的 Logout 捕获仍留给下一次保存。
	PendingRestoreSaveGame->bHasPlayerSnapshot = SaveGame->bHasPlayerSnapshot;
	PendingRestoreSaveGame->PlayerSnapshot = SaveGame->PlayerSnapshot;
	ActiveAsyncRunSaveGame = SaveGame;
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在保存当前世界。")));
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_slot_write_started RequestId=%s Slot=%s File=%s"),
		*Result.RequestId.ToString(), *ActiveSlotId.ToString(), *MakeRunSlotFileName(ActiveSlotId));
	SaveGame->OnSaveFinished.BindUObject(this, &ThisClass::HandleRunSaved, Result.RequestId);
	if (!SaveGame->AsyncSaveGameToSlotForLocalPlayer())
	{
		FinishDiskRequest(Result.RequestId, false, FText::FromString(TEXT("本机存档写入未能启动。")));
		return MakeResult(false, LastResultText);
	}
	return Result;
}

// 终态释放流程：先拒绝仍有磁盘回调的请求；调用方确认房间或世界已离开后，清本局强引用、玩家快照、时长和旅行许可，目录保留供下一次选槽。
// 这里既不采集也不保存，Host 必须先等自己的 OnSaveCompleted 成功，再完成离开，最后调用本入口。
bool UCatSaveSubsystem::ReleaseActiveRun()
{
	if (bBusy)
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
	return bBusy;
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
bool UCatSaveSubsystem::RestoreWorldAfterHostsReady(ACatfishingGameModeBase& GameMode)
{
	if (!HasLoadedRunForTravel() || !GameMode.HasAuthority() || GameMode.GetWorld() != GetGameInstance()->GetWorld())
	{
		return true;
	}
	UWorld* World = GameMode.GetWorld();
	ACatCampInventoryActor* CampInventory = nullptr;
	for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
	{
		if (CampInventory)
		{
			RejectPendingRestore(FText::FromString(TEXT("当前地图存在多个营地公共仓库，不能安全恢复世界槽。")));
			return false;
		}
		CampInventory = *It;
	}
	UCatFishContainerService* FishContainers = World ? World->GetSubsystem<UCatFishContainerService>() : nullptr;
	const TArray<FCatSavedRunInventorySlot>& SavedCampInventory = PendingRestoreSaveGame->CampInventory.InventorySlots;
	FText Failure;
	if (!PendingRestoreSaveGame->bHasWorldSnapshot)
	{
		// 新局没有要覆盖的世界内容；验证真实宿主可采样即可，沿用关卡初始仓库和全部初始箱子。
		TArray<FCatPersistentContainerSnapshot> InitialContainers;
		TArray<FCatInventoryEntry> InitialCampInventorySlots;
		if (!CampInventory || !FishContainers
			|| !CampInventory->ExportInventorySlotsFromAuthority(InitialCampInventorySlots, Failure)
			|| !FishContainers->ExportPersistedWorldFishContainers(InitialContainers, Failure))
		{
			RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("新局营地或容器尚未就绪。")) : Failure);
			return false;
		}
		bWorldRestoreApplied = true;
		bLoadedRunForTravel = false;
		ActiveRunStartedWorldSeconds = World->GetTimeSeconds();
		ActiveRunBasePlayedDurationSeconds = 0.0;
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_new_world_ready Slot=%s World=%s Containers=%d"),
			*ActiveSlotId.ToString(), *GetNameSafe(World), InitialContainers.Num());
		return true;
	}
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
	UCatInventoryComponent* Inventory = Character.GetInventoryComponent();
	const FCatSavedPlayerRunState& SavedPlayer = PendingRestoreSaveGame->PlayerSnapshot;
	const FCatEquipmentLoadoutSnapshot SavedEquipmentSnapshot = ToRuntimeEquipment(SavedPlayer.EquipmentSnapshot);
	const TArray<FCatSavedRunInventorySlot>& SavedInventorySlots = SavedPlayer.InventorySlots;
	FText Failure;
	if (!Equipment || !Inventory || SavedPlayer.CharacterTransform.ContainsNaN())
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
	Character.SetActorTransform(RestoredTransform, false, nullptr, ETeleportType::TeleportPhysics);
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
// 1. 只接受 authority GameMode、唯一营地和已恢复的玩法 World，客户端或前端 World 不会写磁盘。
// 2. 世界槽只保留一个本机玩家快照；若有在线本机 Controller 就现场采样，否则沿用退出捕获已经写入内存的快照。
// 3. 最后导出已提交世界鱼与真实 Run 展示元数据；偷鱼窗口、Profile 和 Run 状态机都不会进入载荷。
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
	ACatCampInventoryActor* CampInventory = nullptr;
	for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
	{
		if (CampInventory)
		{
			OutFailure = FText::FromString(TEXT("当前地图存在多个营地公共仓库。"));
			return false;
		}
		CampInventory = *It;
	}
	UCatFishContainerService* FishContainers = World->GetSubsystem<UCatFishContainerService>();
	if (!CampInventory || !FishContainers)
	{
		OutFailure = FText::FromString(TEXT("营地或鱼容器服务未就绪。"));
		return false;
	}
	OutSaveGame.bHasPlayerSnapshot = PendingRestoreSaveGame->bHasPlayerSnapshot;
	OutSaveGame.PlayerSnapshot = PendingRestoreSaveGame->PlayerSnapshot;
	OutSaveGame.WorldFishContainers.Reset();
	OutSaveGame.CampInventory = FCatSavedCampInventory();
	OutSaveGame.FormatVersion = OutSaveGame.GetLatestDataVersion();
	OutSaveGame.bHasWorldSnapshot = true;
	OutSaveGame.SlotId = ActiveSlotId;
	if (const FCatSaveSlotSummary* Summary = SlotSummaries.FindByPredicate(
		[this](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == ActiveSlotId; }))
	{
		OutSaveGame.DisplayName = Summary->DisplayName;
	}
	OutSaveGame.LastSavedAt = FDateTime::UtcNow();
	TArray<FCatInventoryEntry> CampInventorySlots;
	if (!CampInventory->ExportInventorySlotsFromAuthority(CampInventorySlots, OutFailure))
	{
		return false;
	}
	if (!WriteSavedInventorySlots(CampInventorySlots, OutSaveGame.CampInventory.InventorySlots, OutFailure))
	{
		return false;
	}
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
	if (!FishContainers->ExportPersistedWorldFishContainers(OutSaveGame.WorldFishContainers, OutFailure))
	{
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
	if (!SaveGame.bHasWorldSnapshot && (SaveGame.bHasPlayerSnapshot || !SaveGame.WorldFishContainers.IsEmpty()
		|| !SaveGame.CampInventory.InventorySlots.IsEmpty()))
	{
		OutFailure = FText::FromString(TEXT("未开始的新槽夹带已有世界库存，不能按新局进入。"));
		return false;
	}
	if (SaveGame.bHasWorldSnapshot && !SaveGame.bHasPlayerSnapshot)
	{
		OutFailure = FText::FromString(TEXT("世界存档缺少本机玩家快照。"));
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
	ActiveAsyncRunSaveGame = nullptr;
	bBusy = false;
	LastResultText = Message;
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_request_completed RequestId=%s Success=%d Slot=%s World=%s Message=%s"),
		*RequestId.ToString(), bSuccess, *ActiveSlotId.ToString(), *GetNameSafe(GetWorld()), *Message.ToString());
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
