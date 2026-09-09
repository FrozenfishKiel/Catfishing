#include "Save/CatSaveSubsystem.h"

#include "Async/Async.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Inventory/CatInventorySettings.h"
#include "Items/CatItemsService.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/CatLog.h"
#include "Misc/PackageName.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	/** 本机 Host 世界使用独立文件名空间；该索引只用于引擎完成委托，不借用 LocalPlayer Profile 槽。 */
	constexpr int32 SaveUserIndex = 0;
	/** 世界 schema v2 加入动态容器宿主与新槽标记；旧载荷必须显式拒绝，不能把缺字段解释为新局。 */
	constexpr int32 CurrentSaveFormatVersion = 2;
	/** 目录 v2 记录正式 Run 代号和待清理删除意图；旧单文件格式不能静默按新目录覆盖。 */
	constexpr int32 CurrentIndexFormatVersion = 2;
	/** 双代信封的固定标记；读取器先校验它和 CRC，再让游戏线程反序列化 UObject。 */
	constexpr uint32 SaveEnvelopeMagic = 0x43534732;
	/** 单代文件的最大字节数；损坏的长度字段不能导致无限分配内存。 */
	constexpr int64 MaxSaveFileBytes = 128 * 1024 * 1024;

	// 文件定位流程：Host 世界槽和索引都在 SaveGames 下维护两代，不访问或改写 Profile 的文件。
	FString MakeGenerationPath(const FString& SlotName, const int32 GenerationIndex)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"),
			FString::Printf(TEXT("%s.%d.sav"), *SlotName, GenerationIndex));
	}

	// 单代读取流程：先限制文件长度，再验证标记、递增代号、载荷长度与覆盖头和载荷的 CRC；失败不会输出可反序列化的字节。
	bool ReadSaveGeneration(const FString& Path, TArray<uint8>& OutPayload, int64& OutGeneration)
	{
		OutPayload.Reset();
		OutGeneration = 0;
		TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
		const int64 Size = File ? File->Size() : 0;
		if (Size < 20 || Size > MaxSaveFileBytes)
		{
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(static_cast<int32>(Size));
		if (!File->Read(Bytes.GetData(), Size))
		{
			return false;
		}
		FMemoryReader Reader(Bytes, true);
		uint32 Magic = 0;
		int64 Generation = 0;
		int32 PayloadSize = 0;
		Reader << Magic << Generation << PayloadSize;
		if (Magic != SaveEnvelopeMagic || Generation <= 0 || PayloadSize <= 0 || static_cast<int64>(PayloadSize) + 20 != Size)
		{
			return false;
		}
		TArray<uint8> Payload;
		Payload.SetNumUninitialized(PayloadSize);
		Reader.Serialize(Payload.GetData(), PayloadSize);
		uint32 StoredCrc = 0;
		Reader << StoredCrc;
		if (Reader.IsError() || StoredCrc != FCrc::MemCrc32(Bytes.GetData(), Bytes.Num() - sizeof(uint32)))
		{
			return false;
		}
		OutPayload = MoveTemp(Payload);
		OutGeneration = Generation;
		return true;
	}

	// 双代写入流程：读取两代，保护索引允许读取的最新有效代，只覆盖另一代；未提交索引的孤立文件不会挤掉最后正式样本。
	// 新文件完整写入、Flush(true)、关闭并回读相等后才返回代号；首写再播种另一代，后续永不覆盖受保护代。旧代均损坏或新槽撞文件时保留材料并拒绝覆盖。
	bool WriteSaveGeneration(const FString& SlotName, const TArray<uint8>& Payload, int64& OutGeneration,
		const int64 CommittedGeneration)
	{
		OutGeneration = 0;
		if (Payload.IsEmpty() || static_cast<int64>(Payload.Num()) + 20 > MaxSaveFileBytes)
		{
			return false;
		}
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		TArray<uint8> Previous[2];
		int64 Generations[2] = {0, 0};
		bool bAnyFile = false;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FString Path = MakeGenerationPath(SlotName, Index);
			bAnyFile |= PlatformFile.FileExists(*Path);
			ReadSaveGeneration(Path, Previous[Index], Generations[Index]);
		}
		const int64 Latest = FMath::Max(Generations[0], Generations[1]);
		int32 ProtectedIndex = INDEX_NONE;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Generations[Index] > 0 && Generations[Index] <= CommittedGeneration
				&& (ProtectedIndex == INDEX_NONE || Generations[Index] > Generations[ProtectedIndex]))
			{
				ProtectedIndex = Index;
			}
		}
		if ((bAnyFile && ProtectedIndex == INDEX_NONE) || Latest == MAX_int64
			|| (!bAnyFile && CommittedGeneration > 0 && CommittedGeneration != MAX_int64))
		{
			return false;
		}
		const int32 TargetIndex = ProtectedIndex == 0 ? 1 : 0;
		const FString TargetPath = MakeGenerationPath(SlotName, TargetIndex);
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		uint32 Magic = SaveEnvelopeMagic;
		int64 Generation = Latest + 1;
		int32 PayloadSize = Payload.Num();
		Writer << Magic << Generation << PayloadSize;
		Writer.Serialize(const_cast<uint8*>(Payload.GetData()), Payload.Num());
		uint32 Crc = FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
		Writer << Crc;
		if (!PlatformFile.CreateDirectoryTree(*FPaths::GetPath(TargetPath)))
		{
			return false;
		}
		TUniquePtr<IFileHandle> File(PlatformFile.OpenWrite(*TargetPath));
		if (!File || !File->Write(Bytes.GetData(), Bytes.Num()) || !File->Flush(true))
		{
			return false;
		}
		File.Reset();
		TArray<uint8> VerifiedPayload;
		int64 VerifiedGeneration = 0;
		const bool bVerified = ReadSaveGeneration(TargetPath, VerifiedPayload, VerifiedGeneration)
			&& VerifiedGeneration == Generation && VerifiedPayload == Payload;
		OutGeneration = bVerified ? Generation : 0;
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_generation_written File=%s Generation=%lld PreviousGeneration=%lld Verified=%d"),
			*TargetPath, Generation, Latest, bVerified);
		if (bVerified && !bAnyFile)
		{
			// 第一份正式样本也保留双份；递归调用已能读到第一代，只会写另一文件，不再进入这个分支。
			return WriteSaveGeneration(SlotName, Payload, OutGeneration, MAX_int64);
		}
		return bVerified;
	}

	// 异步读流程：后台只接触字节，选取不晚于已提交索引的最新有效代，损坏新代可退到有效旧代；游戏线程才创建 UObject 并交给弱绑定完成委托。
	void LoadProtectedSaveGame(const FString& SlotName, const FAsyncLoadGameFromSlotDelegate& Completed,
		const int64 CommittedGeneration = MAX_int64)
	{
		Async(EAsyncExecution::ThreadPool, [SlotName, Completed, CommittedGeneration]()
		{
			TArray<uint8> Payloads[2];
			int64 Generations[2] = {0, 0};
			ReadSaveGeneration(MakeGenerationPath(SlotName, 0), Payloads[0], Generations[0]);
			ReadSaveGeneration(MakeGenerationPath(SlotName, 1), Payloads[1], Generations[1]);
			for (int32 Index = 0; Index < 2; ++Index)
			{
				if (Generations[Index] > CommittedGeneration)
				{
					Payloads[Index].Reset();
					Generations[Index] = 0;
				}
			}
			const int32 Selected = Generations[1] > Generations[0] ? 1 : 0;
			UE_LOG(LogCatRun, Log, TEXT("Event=persistence_generation_selected Slot=%s Generation0=%lld Generation1=%lld Selected=%d"),
				*SlotName, Generations[0], Generations[1], Selected);
			AsyncTask(ENamedThreads::GameThread, [SlotName, Completed, Payload = MoveTemp(Payloads[Selected])]()
			{
				USaveGame* Loaded = Payload.IsEmpty() ? nullptr : UGameplayStatics::LoadGameFromMemory(Payload);
				Completed.ExecuteIfBound(SlotName, SaveUserIndex, Loaded);
			});
		});
	}

	// 异步写流程：游戏线程先序列化不可变快照，后台按正式代号保护旧样本；删除索引时额外滚动一次，使两代都不再引用目标后才允许删实体文件。
	// 完成委托始终回游戏线程，且只在刷盘和回读校验完成后通知；后台不解引用 Subsystem 或任何 SaveGame 对象。
	void SaveProtectedSaveGame(USaveGame* SaveGame, const FString& SlotName,
		TFunction<void(bool, int64)> Completed, const int64 CommittedGeneration = MAX_int64,
		const bool bMirrorDeletion = false)
	{
		TArray<uint8> Payload;
		const bool bSerialized = UGameplayStatics::SaveGameToMemory(SaveGame, Payload);
		Async(EAsyncExecution::ThreadPool, [SlotName, Completed, bSerialized, bMirrorDeletion, CommittedGeneration, Payload = MoveTemp(Payload)]()
		{
			int64 Generation = 0;
			const bool bSuccess = bSerialized && WriteSaveGeneration(SlotName, Payload, Generation, CommittedGeneration)
				&& (!bMirrorDeletion || WriteSaveGeneration(SlotName, Payload, Generation, MAX_int64));
			AsyncTask(ENamedThreads::GameThread, [Completed, bSuccess, Generation]()
			{
				Completed(bSuccess, Generation);
			});
		});
	}

	// 旧文件检查流程：只识别旧固定文件名而忽略双代后缀；遇到任何旧世界槽都暂停新目录写入，交由人工确认迁移，不删文件也不提供隐式旧读写入口。
	bool HasLegacyWorldSaveFiles()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames/CatRun_*.sav")), true, false);
		for (const FString& File : Files)
		{
			const FString BaseName = FPaths::GetBaseFilename(File);
			FGuid SlotId;
			if (BaseName == TEXT("CatRun_Index") || (BaseName.StartsWith(TEXT("CatRun_"))
				&& FGuid::ParseExact(BaseName.Mid(7), EGuidFormats::Digits, SlotId)))
			{
				return true;
			}
		}
		return false;
	}

	// 槽 ID 格式判断流程：只接受 32 位十六进制 GUID；Digits 格式含 A-F，不能误用纯十进制检查拒绝新建槽。
	bool IsValidSlotId(const FName SlotId)
	{
		FGuid Parsed;
		return FGuid::ParseExact(SlotId.ToString(), EGuidFormats::Digits, Parsed) && Parsed.IsValid();
	}

	// 存档摘要构造流程：只复制 Run 已公开观测值和世界时钟，不把这些展示字段回写到 GameMode 或当作可恢复玩法真相。
	FCatSaveSlotSummary MakeSummary(const UCatRunSaveGame& SaveGame, const FString& DisplayName)
	{
		FCatSaveSlotSummary Summary;
		Summary.SlotId = SaveGame.SlotId;
		Summary.DisplayName = DisplayName;
		Summary.LastSavedAt = SaveGame.LastSavedAt;
		Summary.DayIndex = SaveGame.DayIndex;
		Summary.LocationName = SaveGame.LocationName;
		Summary.PlayedDurationSeconds = SaveGame.PlayedDurationSeconds;
		Summary.SacrificeProgress = SaveGame.SacrificeProgress;
		Summary.SacrificeTarget = SaveGame.SacrificeTarget;
		return Summary;
	}

	// 库存格磁盘转换流程：只拷贝已提交库存格的定义、实例、数量和工具状态；不把运行复制类型或私有缓存变成磁盘契约。
	FCatSavedRunInventorySlot ToSavedInventorySlot(const FCatRunInventorySlot& Slot)
	{
		FCatSavedRunInventorySlot Saved;
		Saved.DefinitionId = Slot.DefinitionId;
		Saved.ItemInstanceId = Slot.ItemInstanceId;
		Saved.Quantity = Slot.Quantity;
		Saved.RodDurability = Slot.RodDurability;
		Saved.bRodBroken = Slot.bRodBroken;
		return Saved;
	}

	// 库存格恢复转换流程：从磁盘 DTO 重建领域恢复输入；恢复 API 后续会把这些格位重建进正式 InventoryComponent。
	FCatRunInventorySlot ToRuntimeInventorySlot(const FCatSavedRunInventorySlot& Saved)
	{
		FCatRunInventorySlot Slot;
		Slot.DefinitionId = Saved.DefinitionId;
		Slot.ItemInstanceId = Saved.ItemInstanceId;
		Slot.Quantity = Saved.Quantity;
		Slot.RodDurability = Saved.RodDurability;
		Slot.bRodBroken = Saved.bRodBroken;
		return Slot;
	}

	// 玩家运行载荷导出流程：复制 ExportSnapshotFromAuthority 交出的钓具选择、耐久和可保存格位；正式角色来自 InventoryComponent 投影，旧宿主保留兼容快照。
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
		Saved.InventorySlots.Reserve(Snapshot.InventorySlots.Num());
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			Saved.InventorySlots.Add(ToSavedInventorySlot(Slot));
		}
		return Saved;
	}

	// 玩家运行载荷恢复输入转换流程：先完整重建兼容 DTO，再交给 Equipment 校验并显式导入正式 InventoryComponent；转换本身不修改任何运行库存。
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
		Snapshot.InventorySlots.Reserve(Saved.InventorySlots.Num());
		for (const FCatSavedRunInventorySlot& Slot : Saved.InventorySlots)
		{
			Snapshot.InventorySlots.Add(ToRuntimeInventorySlot(Slot));
		}
		return Snapshot;
	}

	// 营地磁盘转换流程：营地与玩家复用同一格 DTO，但各自仍通过独立领域恢复 API 维护容量和复制语义。
	FCatSavedCampInventory ToSavedCampInventory(const FCatCampInventorySnapshot& Snapshot)
	{
		FCatSavedCampInventory Saved;
		Saved.Revision = Snapshot.Revision;
		Saved.InventorySlots.Reserve(Snapshot.InventorySlots.Num());
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			Saved.InventorySlots.Add(ToSavedInventorySlot(Slot));
		}
		return Saved;
	}

	// 营地恢复输入转换流程：只从已读载荷构造领域 DTO，不写 Actor；Camp 后续以受控 API 执行最终校验和提交。
	FCatCampInventorySnapshot ToRuntimeCampInventory(const FCatSavedCampInventory& Saved)
	{
		FCatCampInventorySnapshot Snapshot;
		Snapshot.Revision = Saved.Revision;
		Snapshot.InventorySlots.Reserve(Saved.InventorySlots.Num());
		for (const FCatSavedRunInventorySlot& Slot : Saved.InventorySlots)
		{
			Snapshot.InventorySlots.Add(ToRuntimeInventorySlot(Slot));
		}
		return Snapshot;
	}

	// 存档随身容量解析流程：正式默认值来自库存设置；旧 Equipment 配置只有偏离项目默认时才作为迁移期覆盖值，避免旧自动化和旧资产马上失效。
	int32 ResolveSavedPlayerInventorySlotCapacity()
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		const int32 InventorySlotCapacity =
			InventorySettings != nullptr ? InventorySettings->GetPlayerInventorySlotCapacity() : 0;
		const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
		const int32 LegacySlotCapacity =
			EquipmentSettings != nullptr ? FMath::Max(0, EquipmentSettings->InventorySlotCapacity)
			: UCatInventorySettings::ProjectDefaultPlayerInventorySlotCapacity;
		return LegacySlotCapacity != UCatInventorySettings::ProjectDefaultPlayerInventorySlotCapacity
			? LegacySlotCapacity : InventorySlotCapacity;
	}

	// 玩家库存载荷预检流程：在还没旅行到玩法 World 时按当前目录和容量验证格子、实例、耐久及选择引用；这一步不依赖 Pawn，先挡住会导致半恢复的坏磁盘数据。
	bool ValidateSavedEquipmentSnapshot(const FCatEquipmentLoadoutSnapshot& Snapshot, FText& OutFailure)
	{
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		if (!Settings || Snapshot.InventorySlots.Num() > ResolveSavedPlayerInventorySlotCapacity())
		{
			OutFailure = FText::FromString(TEXT("存档随身库存缺少运行目录或超过当前容量。"));
			return false;
		}
		TSet<FGuid> SeenInstanceIds;
		for (const FCatRunInventorySlot& Slot : Snapshot.InventorySlots)
		{
			const bool bOccupied = !Slot.DefinitionId.IsNone() && Slot.Quantity > 0;
			if (!bOccupied)
			{
				if (!Slot.DefinitionId.IsNone() || Slot.ItemInstanceId.IsValid() || Slot.Quantity != 0
					|| Slot.RodDurability != 0.0 || Slot.bRodBroken)
				{
					OutFailure = FText::FromString(TEXT("存档随身库存空格包含残留数据。"));
					return false;
				}
				continue;
			}
			const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
			const int32 StackLimit = Definition != nullptr ? Definition->GetMaxStackCount() : 0;
			if (!Slot.ItemInstanceId.IsValid() || SeenInstanceIds.Contains(Slot.ItemInstanceId) || !Definition
				|| !Definition->IsRuntimeDefinitionReady() || Slot.Quantity > StackLimit)
			{
				OutFailure = FText::FromString(TEXT("存档随身库存含有失效定义、数量或重复实例。"));
				return false;
			}
			if (Definition->Kind == ECatEquipmentKind::Rod)
			{
				if (!FMath::IsFinite(Slot.RodDurability) || Slot.RodDurability < 0.0
					|| Slot.RodDurability > Definition->MaximumRodDurability
					|| (Slot.bRodBroken && Slot.RodDurability != 0.0))
				{
					OutFailure = FText::FromString(TEXT("存档鱼竿耐久与当前定义不兼容。"));
					return false;
				}
			}
			else if (Slot.RodDurability != 0.0 || Slot.bRodBroken)
			{
				OutFailure = FText::FromString(TEXT("存档非鱼竿物品含有鱼竿状态。"));
				return false;
			}
			SeenInstanceIds.Add(Slot.ItemInstanceId);
		}
		const auto HasSelectedInstance = [&Snapshot, Settings](const FName DefinitionId, const FGuid InstanceId,
			const ECatEquipmentKind ExpectedKind)
		{
			if (DefinitionId.IsNone())
			{
				return !InstanceId.IsValid();
			}
			return InstanceId.IsValid() && Snapshot.InventorySlots.ContainsByPredicate(
				[DefinitionId, InstanceId, ExpectedKind, Settings](const FCatRunInventorySlot& Slot)
				{
					const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(Slot.DefinitionId);
					return Slot.DefinitionId == DefinitionId && Slot.ItemInstanceId == InstanceId && Definition
						&& Definition->Kind == ExpectedKind;
				});
		};
		if (!HasSelectedInstance(Snapshot.RodDefinitionId, Snapshot.RodItemInstanceId, ECatEquipmentKind::Rod)
			|| !HasSelectedInstance(Snapshot.BaitDefinitionId, Snapshot.BaitItemInstanceId, ECatEquipmentKind::Bait)
			|| !HasSelectedInstance(Snapshot.FloatDefinitionId, Snapshot.FloatItemInstanceId, ECatEquipmentKind::Float)
			|| !HasSelectedInstance(Snapshot.ScoopNetDefinitionId, Snapshot.ScoopNetItemInstanceId,
				ECatEquipmentKind::ScoopNet))
		{
			OutFailure = FText::FromString(TEXT("存档钓具选择没有指向同一库存中的合法实例。"));
			return false;
		}
		const FCatRunInventorySlot* SelectedRod = Snapshot.InventorySlots.FindByPredicate(
			[&Snapshot](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == Snapshot.RodItemInstanceId; });
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

// 反初始化流程：异步委托以 UObject 弱绑定自动失效；这里先清所有强引用和旅行许可，使旧 World 回调即使晚到也不能恢复到下一次游戏实例。
void UCatSaveSubsystem::Deinitialize()
{
	bBusy = false;
	bLoadedRunForTravel = false;
	bWorldRestoreApplied = false;
	PendingRestoreSaveGame = nullptr;
	ActiveAsyncRunSaveGame = nullptr;
	ActiveAsyncIndexSaveGame = nullptr;
	PendingDeletionSlotIds.Reset();
	RestoredPlayerStableNetIds.Reset();
	CapturedDepartingControllers.Reset();
	PlayerCaptureFailure = FText::GetEmpty();
	OnChanged.Clear();
	OnSaveCompleted.Clear();
	Super::Deinitialize();
}

// 槽目录刷新流程：先串行化请求并检查旧固定文件，存在旧格式则暂停并等待迁移确认；否则异步读取两代目录，回调处理空目录、有效代或待重试删除。
void UCatSaveSubsystem::RefreshSlotSummaries()
{
	if (bBusy)
	{
		return;
	}
	if (HasLegacyWorldSaveFiles())
	{
		bIndexLoaded = false;
		LastResultText = FText::FromString(TEXT("发现旧格式世界存档，已停止写入；请先确认迁移方案，原文件未改动。"));
		OnChanged.Broadcast();
		UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_legacy_files_blocked World=%s"), *GetNameSafe(GetWorld()));
		return;
	}
	bBusy = true;
	LoadProtectedSaveGame(GetIndexSlotFileName(),
		FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleIndexLoaded));
}

// 槽列表读取流程：返回最近一次成功目录，不在这里触发磁盘访问或返回可写副本。
const TArray<FCatSaveSlotSummary>& UCatSaveSubsystem::GetSlotSummaries() const
{
	return SlotSummaries;
}

// 新建槽流程：先校验可写目录、活动状态和显示名，再以新槽 ID 异步写空 Run；回调仅准备带代号的候选索引，两者均提交后才发布摘要。
FCatSaveResult UCatSaveSubsystem::RequestCreateSlot(const FString& DisplayName)
{
	const FString TrimmedDisplayName = DisplayName.TrimStartAndEnd();
	if (bBusy || !bIndexLoaded || bWorldRestoreApplied || PendingRestoreSaveGame || TrimmedDisplayName.IsEmpty()
		|| TrimmedDisplayName.Len() > 64)
	{
		return MakeResult(false, FText::FromString(TEXT("存档目录未就绪、操作进行中或名称无效。")));
	}
	const FName SlotId(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UCatRunSaveGame* NewSaveGame = NewObject<UCatRunSaveGame>(this);
	NewSaveGame->FormatVersion = CurrentSaveFormatVersion;
	NewSaveGame->SlotId = SlotId;
	NewSaveGame->LastSavedAt = FDateTime::UtcNow();
	ActiveAsyncRunSaveGame = NewSaveGame;
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在创建新存档。")));
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_run_write_started RequestId=%s Slot=%s CommittedGeneration=0"),
		*Result.RequestId.ToString(), *SlotId.ToString());
	const TWeakObjectPtr<UCatSaveSubsystem> WeakThis(this);
	SaveProtectedSaveGame(NewSaveGame, MakeRunSlotFileName(SlotId),
		[WeakThis, RequestId = Result.RequestId, SlotId, TrimmedDisplayName](const bool bSuccess, const int64 Generation)
		{
			if (UCatSaveSubsystem* Self = WeakThis.Get(); Self && Self->bBusy)
			{
				Self->HandleRunSaved(RequestId, SlotId, TrimmedDisplayName, bSuccess, Generation);
			}
		}, 0);
	return Result;
}

// 读取槽流程：目录先确认槽与已提交代号，再异步读取不晚于该代号的最新有效 Run；成功只建立待恢复快照和旅行许可，未提交索引的新文件不会被采纳。
FCatSaveResult UCatSaveSubsystem::RequestLoadSlot(const FName SlotId)
{
	if (bBusy || !bIndexLoaded || bWorldRestoreApplied || PendingRestoreSaveGame || !IsValidSlotId(SlotId)
		|| !SlotSummaries.ContainsByPredicate([SlotId](const FCatSaveSlotSummary& Summary) { return Summary.SlotId == SlotId; }))
	{
		return MakeResult(false, FText::FromString(TEXT("存档不存在、目录未就绪或正在执行其他操作。")));
	}
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在读取存档。")));
	const FCatSaveSlotSummary* Summary = SlotSummaries.FindByPredicate(
		[SlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == SlotId; });
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_run_read_started RequestId=%s Slot=%s CommittedGeneration=%lld"),
		*Result.RequestId.ToString(), *SlotId.ToString(), Summary->RunGeneration);
	LoadProtectedSaveGame(MakeRunSlotFileName(SlotId),
		FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &ThisClass::HandleRunLoaded, Result.RequestId, SlotId),
		Summary->RunGeneration);
	return Result;
}

// 删除槽流程：拒绝活动槽或并发操作，正常槽及上次未清完的删除均可重试；先准备无目标摘要且带删除意图的索引，两代提交后才清理两代 Run。
FCatSaveResult UCatSaveSubsystem::RequestDeleteSlot(const FName SlotId)
{
	if (bBusy || !bIndexLoaded || SlotId == ActiveSlotId || !IsValidSlotId(SlotId)
		|| (!PendingDeletionSlotIds.Contains(SlotId)
			&& !SlotSummaries.ContainsByPredicate([SlotId](const FCatSaveSlotSummary& Summary) { return Summary.SlotId == SlotId; })))
	{
		return MakeResult(false, FText::FromString(TEXT("运行中的存档不能删除，或目标槽不存在。")));
	}
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在删除存档。")));
	ActiveAsyncIndexSaveGame = NewObject<UCatSaveIndexSaveGame>(this);
	ActiveAsyncIndexSaveGame->SlotSummaries = SlotSummaries;
	ActiveAsyncIndexSaveGame->SlotSummaries.RemoveAll([SlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == SlotId; });
	ActiveAsyncIndexSaveGame->PendingDeletionSlotIds = PendingDeletionSlotIds;
	ActiveAsyncIndexSaveGame->PendingDeletionSlotIds.AddUnique(SlotId);
	SaveIndexAsync(Result.RequestId, SlotId);
	return Result;
}

// 运行保存请求流程：目录终态不明时先安排重读并拒绝本次保存；目录和 Host 槽就绪后完整采集并合并玩家基线，异步保留正式代，Run 与索引均提交才通知成功。
FCatSaveResult UCatSaveSubsystem::RequestSaveActiveRun()
{
	if (!bBusy && !bIndexLoaded)
	{
		RefreshSlotSummaries();
		return MakeResult(false, bBusy ? FText::FromString(TEXT("正在重读存档目录，请完成后重试保存。")) : LastResultText);
	}
	if (bBusy || !bIndexLoaded || ActiveSlotId.IsNone())
	{
		return MakeResult(false, FText::FromString(TEXT("没有可保存的活动世界槽，或已有存档操作正在进行。")));
	}
	const FCatSaveSlotSummary* Summary = SlotSummaries.FindByPredicate(
		[this](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == ActiveSlotId; });
	if (!Summary)
	{
		return MakeResult(false, FText::FromString(TEXT("活动槽不在已提交目录中，请刷新存档目录。")));
	}
	const FString DisplayName = Summary->DisplayName;
	const int64 CommittedGeneration = Summary->RunGeneration;
	UCatRunSaveGame* SaveGame = NewObject<UCatRunSaveGame>(this);
	FText Failure;
	if (!BuildActiveRunSaveGame(*SaveGame, Failure))
	{
		return MakeResult(false, Failure);
	}
	// 游戏线程采样时更新基线；异步回调绝不把旧采样覆盖回来，保证写盘途中发生的 Logout 捕获仍留给下一次保存。
	PendingRestoreSaveGame->Players = SaveGame->Players;
	ActiveAsyncRunSaveGame = SaveGame;
	bBusy = true;
	const FCatSaveResult Result = MakeResult(true, FText::FromString(TEXT("正在保存当前世界。")));
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_run_write_started RequestId=%s Slot=%s CommittedGeneration=%lld"),
		*Result.RequestId.ToString(), *ActiveSlotId.ToString(), CommittedGeneration);
	const TWeakObjectPtr<UCatSaveSubsystem> WeakThis(this);
	SaveProtectedSaveGame(SaveGame, MakeRunSlotFileName(ActiveSlotId),
		[WeakThis, RequestId = Result.RequestId, SlotId = ActiveSlotId, DisplayName](const bool bSuccess, const int64 Generation)
		{
			if (UCatSaveSubsystem* Self = WeakThis.Get(); Self && Self->bBusy)
			{
				Self->HandleRunSaved(RequestId, SlotId, DisplayName, bSuccess, Generation);
			}
		}, CommittedGeneration);
	return Result;
}

// 终态释放流程：先拒绝仍有磁盘回调的请求；调用方确认房间或世界已离开后，清本局强引用、身份基线、时长和旅行许可，目录保留供下一次选槽。
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
	ActiveAsyncIndexSaveGame = nullptr;
	ActiveSlotId = NAME_None;
	bLoadedRunForTravel = false;
	bWorldRestoreApplied = false;
	RestoredPlayerStableNetIds.Reset();
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
// 1. 先确认 GameMode、营地与 Items 都属于 authority World，并在任何库存写入前完成两个领域的只读预检。
// 2. 全部通过才提交营地和世界鱼容器；Items 异常时尝试还原营地，但不声称已销毁宿主可回滚，必须取消旅行许可并让 GameMode 保持失败。
// 3. 成功后保留玩家载荷等待每位 Pawn 生成时恢复，同时开始统计本 World 的实际玩法时长。
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
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	const FCatCampInventorySnapshot SavedCampInventory = ToRuntimeCampInventory(PendingRestoreSaveGame->CampInventory);
	FText Failure;
	if (!PendingRestoreSaveGame->bHasWorldSnapshot)
	{
		// 新局没有要覆盖的世界内容；验证真实宿主可采样即可，保留关卡初始仓库和全部初始箱子。
		TArray<FCatPersistentContainerSnapshot> InitialContainers;
		if (!CampInventory || !Items || !CampInventory->CanRestoreSnapshotFromAuthority(CampInventory->GetSnapshot(), Failure)
			|| !Items->ExportPersistedWorldFishContainers(InitialContainers, Failure))
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
	if (!CampInventory || !Items || !CampInventory->CanRestoreSnapshotFromAuthority(SavedCampInventory, Failure)
		|| !Items->CanRestorePersistedWorldFishContainers(PendingRestoreSaveGame->WorldFishContainers, Failure))
	{
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("营地或世界鱼容器尚未就绪。")) : Failure);
		return false;
	}
	FCatCampInventorySnapshot PreviousCampInventory = CampInventory->GetSnapshot();
	if (!CampInventory->RestoreSnapshotFromAuthority(SavedCampInventory))
	{
		RejectPendingRestore(FText::FromString(TEXT("领域恢复提交被拒绝，已阻止继续进入玩法。")));
		return false;
	}
	if (!Items->RestorePersistedWorldFishContainers(PendingRestoreSaveGame->WorldFishContainers))
	{
		const bool bCampRolledBack = CampInventory->RestoreSnapshotFromAuthority(PreviousCampInventory);
		// Items 已关闭写口且可能已销毁部分旧宿主；只回滚可恢复的营地，不把这个 World 宣称为可继续游玩的原状态。
		UE_LOG(LogCatRun, Error, TEXT("Event=persistence_world_restore_aborted Slot=%s CampRolledBack=%d ItemsUsable=0"),
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
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_world_restore_committed Slot=%s Players=%d Containers=%d"),
		*ActiveSlotId.ToString(), PendingRestoreSaveGame->Players.Num(), PendingRestoreSaveGame->WorldFishContainers.Num());
	return true;
}

// 玩家恢复流程：在 GameMode 已生成 Character 但尚未交付给玩家前按 PlayerState StableNetId 查找快照；找不到记录表示本局新加入玩家，不会覆盖其空的新库存。
bool UCatSaveSubsystem::RestorePlayerAfterSpawn(AController& Controller, ACatCharacter& Character)
{
	CapturedDepartingControllers.Remove(&Controller);
	if (!bWorldRestoreApplied || !PendingRestoreSaveGame)
	{
		return true;
	}
	const APlayerState* PlayerState = Controller.PlayerState;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
	if (StableNetId.IsEmpty())
	{
		RejectPendingRestore(FText::FromString(TEXT("生成的玩家缺少稳定网络身份，不能恢复库存。")));
		return false;
	}
	const FCatSavedPlayerRunState* SavedPlayer = PendingRestoreSaveGame->Players.FindByPredicate(
		[&StableNetId](const FCatSavedPlayerRunState& Candidate) { return Candidate.StableNetId == StableNetId; });
	if (!SavedPlayer || RestoredPlayerStableNetIds.Contains(StableNetId))
	{
		return true;
	}
	UCatEquipmentComponent* Equipment = Character.GetEquipmentComponent();
	const FCatEquipmentLoadoutSnapshot SavedEquipmentSnapshot = ToRuntimeEquipment(SavedPlayer->EquipmentSnapshot);
	FText Failure;
	if (!Equipment || !Equipment->CanRestoreSnapshotFromAuthority(SavedEquipmentSnapshot, Failure)
		|| SavedPlayer->CharacterTransform.ContainsNaN())
	{
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("玩家库存或位置快照无效。")) : Failure);
		return false;
	}
	if (!Equipment->RestoreSnapshotFromAuthority(SavedEquipmentSnapshot))
	{
		RejectPendingRestore(FText::FromString(TEXT("玩家库存恢复提交被拒绝。")));
		return false;
	}
	Character.SetActorTransform(SavedPlayer->CharacterTransform, false, nullptr, ETeleportType::TeleportPhysics);
	RestoredPlayerStableNetIds.Add(StableNetId);
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_player_restore_committed Slot=%s StableNetId=Valid(Redacted) Character=%s"),
		*ActiveSlotId.ToString(), *GetNameSafe(&Character));
	return true;
}

// 退出捕获流程：先核对活动 World 和稳定身份；解除占有通知提供尚未销毁的 Character，Logout 无 Pawn 时必须命中同连接的捕获记录。
// 成功只合并该玩家的正式记录并允许重连重新恢复；失败锁住当前槽的后续写盘，绝不把过时库存当成末次状态。
bool UCatSaveSubsystem::CapturePlayerBeforeLogout(AController& Controller, ACatCharacter* DepartingCharacter)
{
	if (!bWorldRestoreApplied || !PendingRestoreSaveGame)
	{
		return true;
	}
	ACatCharacter* Character = DepartingCharacter ? DepartingCharacter : Cast<ACatCharacter>(Controller.GetPawn());
	if (!Character && CapturedDepartingControllers.Contains(&Controller))
	{
		return true;
	}
	APlayerState* PlayerState = Controller.PlayerState;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	FCatEquipmentLoadoutSnapshot EquipmentSnapshot;
	FText Failure;
	if (!Controller.HasAuthority() || Controller.GetWorld() != GetWorld() || StableNetId.IsEmpty()
		|| !Equipment || Character->GetActorTransform().ContainsNaN()
		|| !Equipment->ExportSnapshotFromAuthority(EquipmentSnapshot, Failure))
	{
		PlayerCaptureFailure = Failure.IsEmpty() ? FText::FromString(TEXT("退出玩家末次库存或位置未能捕获，已停止覆盖世界存档。")) : Failure;
		LastResultText = PlayerCaptureFailure;
		UE_LOG(LogCatRun, Error, TEXT("Event=persistence_player_departure_rejected Slot=%s Controller=%s World=%s Reason=%s"),
			*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(GetWorld()), *PlayerCaptureFailure.ToString());
		OnChanged.Broadcast();
		return false;
	}
	FCatSavedPlayerRunState* Saved = PendingRestoreSaveGame->Players.FindByPredicate(
		[&StableNetId](const FCatSavedPlayerRunState& Entry) { return Entry.StableNetId == StableNetId; });
	if (!Saved)
	{
		Saved = &PendingRestoreSaveGame->Players.AddDefaulted_GetRef();
	}
	Saved->StableNetId = StableNetId;
	Saved->EquipmentSnapshot = ToSavedEquipment(EquipmentSnapshot);
	Saved->CharacterTransform = Character->GetActorTransform();
	if (!Equipment->RetireDeploymentAfterPersistentCapture(*PlayerState))
	{
		PlayerCaptureFailure = FText::FromString(TEXT("退出玩家的部署实物未能收口，已停止覆盖世界存档。"));
		LastResultText = PlayerCaptureFailure;
		OnChanged.Broadcast();
		return false;
	}
	CapturedDepartingControllers.Add(&Controller);
	RestoredPlayerStableNetIds.Remove(StableNetId);
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_player_departure_captured Slot=%s Controller=%s World=%s Players=%d InventorySlots=%d"),
		*ActiveSlotId.ToString(), *GetNameSafe(&Controller), *GetNameSafe(GetWorld()),
		PendingRestoreSaveGame->Players.Num(), EquipmentSnapshot.InventorySlots.Num());
	return true;
}

// 文件名构造流程：入口已校验 SlotId 为 32 位十六进制 GUID，固定前缀把世界载荷与目录文件隔离。
FString UCatSaveSubsystem::MakeRunSlotFileName(const FName SlotId)
{
	return FString::Printf(TEXT("CatRun_%s"), *SlotId.ToString());
}

// 索引文件名读取流程：目录永远使用固定单文件名；具体世界槽仍由各自稳定 SlotId 文件隔离。
FString UCatSaveSubsystem::GetIndexSlotFileName()
{
	return TEXT("CatRun_Index");
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
// 2. 从活动槽的全部正式玩家记录起步，只以在线 Controller 的新采样覆盖同一身份；离线成员和退出时捕获的最后状态不会消失。
// 3. 最后导出已提交世界鱼与真实 Run 展示元数据；预留/escrow、Profile 和 Run 状态机都不会进入载荷。
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
	UCatItemsService* Items = World->GetSubsystem<UCatItemsService>();
	if (!CampInventory || !Items)
	{
		OutFailure = FText::FromString(TEXT("营地或世界物品服务未就绪。"));
		return false;
	}
	OutSaveGame.Players = PendingRestoreSaveGame->Players;
	OutSaveGame.WorldFishContainers.Reset();
	OutSaveGame.CampInventory = FCatSavedCampInventory();
	OutSaveGame.FormatVersion = CurrentSaveFormatVersion;
	OutSaveGame.bHasWorldSnapshot = true;
	OutSaveGame.SlotId = ActiveSlotId;
	OutSaveGame.LastSavedAt = FDateTime::UtcNow();
	OutSaveGame.CampInventory = ToSavedCampInventory(CampInventory->GetSnapshot());
	TSet<FString> SeenStableNetIds;
	for (TActorIterator<APlayerController> It(World); It; ++It)
	{
		APlayerController* Controller = *It;
		if (!GameMode->IsControllerActive(Controller) || CapturedDepartingControllers.Contains(Controller))
		{
			continue;
		}
		APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
		ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
		const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
		if (!Controller || !Character || !Character->GetEquipmentComponent() || StableNetId.IsEmpty() || SeenStableNetIds.Contains(StableNetId))
		{
			OutFailure = FText::FromString(TEXT("在线玩家缺少可持久化的身份、角色或钓具库存状态。"));
			return false;
		}
		FCatEquipmentLoadoutSnapshot EquipmentSnapshot;
		if (!Character->GetEquipmentComponent()->ExportSnapshotFromAuthority(EquipmentSnapshot, OutFailure))
		{
			return false;
		}
		FCatSavedPlayerRunState* Existing = OutSaveGame.Players.FindByPredicate(
			[&StableNetId](const FCatSavedPlayerRunState& Entry) { return Entry.StableNetId == StableNetId; });
		FCatSavedPlayerRunState& SavedPlayer = Existing ? *Existing : OutSaveGame.Players.AddDefaulted_GetRef();
		SavedPlayer.StableNetId = StableNetId;
		SavedPlayer.EquipmentSnapshot = ToSavedEquipment(EquipmentSnapshot);
		SavedPlayer.CharacterTransform = Character->GetActorTransform();
		SeenStableNetIds.Add(StableNetId);
	}
	if (!Items->ExportPersistedWorldFishContainers(OutSaveGame.WorldFishContainers, OutFailure))
	{
		return false;
	}
	const FCatRunPublicState& RunPublicState = GameMode->GetRunPublicState();
	OutSaveGame.DayIndex = RunPublicState.Phase.DayIndex;
	OutSaveGame.LocationName = FPackageName::GetShortName(World->GetMapName());
	OutSaveGame.PlayedDurationSeconds = ActiveRunBasePlayedDurationSeconds
		+ (ActiveRunStartedWorldSeconds >= 0.0 ? FMath::Max(0.0, World->GetTimeSeconds() - ActiveRunStartedWorldSeconds) : 0.0);
	OutSaveGame.SacrificeProgress = RunPublicState.QuotaProgress;
	OutSaveGame.SacrificeTarget = RunPublicState.QuotaTarget;
	return ValidateLoadedRunSaveGame(OutSaveGame, ActiveSlotId, OutFailure);
}

// 载荷验证流程：读入后先核对格式、槽归属、展示元数据与玩家 StableNetId；跨领域定义/容量/容器键会在 GameMode 生命周期里在任何世界库存写入前再次预检。
bool UCatSaveSubsystem::ValidateLoadedRunSaveGame(const UCatRunSaveGame& SaveGame, const FName ExpectedSlotId,
	FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	if (SaveGame.FormatVersion != CurrentSaveFormatVersion || SaveGame.SlotId != ExpectedSlotId
		|| !IsValidSlotId(SaveGame.SlotId) || !FMath::IsFinite(SaveGame.PlayedDurationSeconds)
		|| SaveGame.PlayedDurationSeconds < 0.0 || SaveGame.DayIndex < 0 || SaveGame.SacrificeProgress < 0
		|| SaveGame.SacrificeTarget < 0)
	{
		OutFailure = FText::FromString(TEXT("存档版本、槽归属或展示元数据无效。"));
		return false;
	}
	if (!SaveGame.bHasWorldSnapshot && (!SaveGame.Players.IsEmpty() || !SaveGame.WorldFishContainers.IsEmpty()
		|| !SaveGame.CampInventory.InventorySlots.IsEmpty()))
	{
		OutFailure = FText::FromString(TEXT("未开始的新槽夹带旧世界库存，不能按新局进入。"));
		return false;
	}
	TSet<FString> SeenStableNetIds;
	TSet<FGuid> SeenItemInstanceIds;
	for (const FCatSavedPlayerRunState& SavedPlayer : SaveGame.Players)
	{
		const FCatEquipmentLoadoutSnapshot SavedEquipmentSnapshot = ToRuntimeEquipment(SavedPlayer.EquipmentSnapshot);
		if (SavedPlayer.StableNetId.IsEmpty() || SeenStableNetIds.Contains(SavedPlayer.StableNetId)
			|| SavedPlayer.CharacterTransform.ContainsNaN()
			|| !ValidateSavedEquipmentSnapshot(SavedEquipmentSnapshot, OutFailure))
		{
			OutFailure = FText::FromString(TEXT("存档包含无效或重复的玩家身份与位置。"));
			return false;
		}
		for (const FCatRunInventorySlot& Slot : SavedEquipmentSnapshot.InventorySlots)
		{
			if (Slot.ItemInstanceId.IsValid() && SeenItemInstanceIds.Contains(Slot.ItemInstanceId))
			{
				OutFailure = FText::FromString(TEXT("存档包含跨玩家重复的物品实例。"));
				return false;
			}
			if (Slot.ItemInstanceId.IsValid())
			{
				SeenItemInstanceIds.Add(Slot.ItemInstanceId);
			}
		}
		SeenStableNetIds.Add(SavedPlayer.StableNetId);
	}
	const FCatCampInventorySnapshot SavedCampInventory = ToRuntimeCampInventory(SaveGame.CampInventory);
	for (const FCatRunInventorySlot& Slot : SavedCampInventory.InventorySlots)
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
				if (SeenFishInstanceIds.Contains(Fish.FishInstanceId))
				{
					OutFailure = FText::FromString(TEXT("存档含有跨世界容器重复的鱼实例。"));
					return false;
				}
				SeenFishInstanceIds.Add(Fish.FishInstanceId);
			}
		}
	}
	return true;
}

// 候选索引写入流程：先校验 Run 代号和删除意图，保持旧目录可读，再异步写候选；有删除意图时必须镜像到两代，回调前不允许删除任何 Run 文件。
void UCatSaveSubsystem::SaveIndexAsync(const FGuid RequestId, const FName RelatedSlotId)
{
	FText Failure;
	if (!ActiveAsyncIndexSaveGame || !ValidateIndex(*ActiveAsyncIndexSaveGame, Failure))
	{
		FinishDiskRequest(RequestId, false, Failure.IsEmpty() ? FText::FromString(TEXT("候选存档目录无效。")) : Failure);
		return;
	}
	const TWeakObjectPtr<UCatSaveSubsystem> WeakThis(this);
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_index_write_started RequestId=%s RelatedSlot=%s PendingDeletes=%d"),
		*RequestId.ToString(), *RelatedSlotId.ToString(), ActiveAsyncIndexSaveGame->PendingDeletionSlotIds.Num());
	SaveProtectedSaveGame(ActiveAsyncIndexSaveGame, GetIndexSlotFileName(),
		[WeakThis, RequestId, RelatedSlotId](const bool bSuccess, int64)
		{
			if (UCatSaveSubsystem* Self = WeakThis.Get(); Self && Self->bBusy)
			{
				Self->HandleIndexSaved(RequestId, RelatedSlotId, bSuccess);
			}
		}, MAX_int64, !ActiveAsyncIndexSaveGame->PendingDeletionSlotIds.IsEmpty());
}

// 索引读取回调流程：有效代整体替换目录；持久化删除意图先重新确认两代索引再幂等清理文件。两代都不存在才是空目录，都无效则封锁写入并保留原文件。
void UCatSaveSubsystem::HandleIndexLoaded(const FString& SlotName, const int32 UserIndex, USaveGame* LoadedGame)
{
	if (!bBusy)
	{
		return;
	}
	UCatSaveIndexSaveGame* IndexSaveGame = Cast<UCatSaveIndexSaveGame>(LoadedGame);
	FText Failure;
	if (IndexSaveGame && ValidateIndex(*IndexSaveGame, Failure))
	{
		SlotSummaries = IndexSaveGame->SlotSummaries;
		PendingDeletionSlotIds = IndexSaveGame->PendingDeletionSlotIds;
		bIndexLoaded = true;
		if (!PendingDeletionSlotIds.IsEmpty())
		{
			ActiveAsyncIndexSaveGame = IndexSaveGame;
			SaveIndexAsync(FGuid(), NAME_None);
			return;
		}
		FinishDiskRequest(FGuid(), true, FText::FromString(TEXT("存档目录已读取。")));
	}
	else if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*MakeGenerationPath(GetIndexSlotFileName(), 0))
		&& !FPlatformFileManager::Get().GetPlatformFile().FileExists(*MakeGenerationPath(GetIndexSlotFileName(), 1)))
	{
		SlotSummaries.Reset();
		PendingDeletionSlotIds.Reset();
		bIndexLoaded = true;
		FinishDiskRequest(FGuid(), true, FText::FromString(TEXT("未找到存档目录，可以创建新世界。")));
	}
	else
	{
		bIndexLoaded = false;
		FinishDiskRequest(FGuid(), false, Failure.IsEmpty() ? FText::FromString(TEXT("两代存档目录均无法读取，原文件已保留。")) : Failure);
	}
}

// 索引提交回调流程：失败不发布候选并要求重读磁盘终态；成功才替换正式目录。有删除意图时后台删除两代 Run，缺失视为已清理，失败保留意图供刷新重试。
// 目录已经双代移除目标才允许物理删除；断电或清理失败不会让旧索引重新指向被删文件，完成通知仍区分提交与全部清理成功。
void UCatSaveSubsystem::HandleIndexSaved(const FGuid RequestId, const FName RelatedSlotId, const bool bSuccess)
{
	UE_LOG(LogCatRun, Log, TEXT("Event=persistence_index_write_completed RequestId=%s RelatedSlot=%s Verified=%d"),
		*RequestId.ToString(), *RelatedSlotId.ToString(), bSuccess);
	if (!bSuccess || !ActiveAsyncIndexSaveGame)
	{
		bIndexLoaded = false;
		FinishDiskRequest(RequestId, false, FText::FromString(TEXT("存档目录提交未确认，旧有效代已保留；请刷新目录判定终态后重试。")));
		return;
	}
	SlotSummaries = ActiveAsyncIndexSaveGame->SlotSummaries;
	PendingDeletionSlotIds = ActiveAsyncIndexSaveGame->PendingDeletionSlotIds;
	bIndexLoaded = true;
	if (PendingDeletionSlotIds.IsEmpty())
	{
		FinishDiskRequest(RequestId, true, FText::FromString(TEXT("世界存档与目录已提交。")));
		return;
	}
	const TWeakObjectPtr<UCatSaveSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, RequestId, SlotIds = PendingDeletionSlotIds]()
	{
		bool bDeleted = true;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		for (const FName SlotId : SlotIds)
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const FString Path = MakeGenerationPath(MakeRunSlotFileName(SlotId), Index);
				const bool bRemoved = !PlatformFile.FileExists(*Path) || PlatformFile.DeleteFile(*Path);
				bDeleted &= bRemoved;
				UE_LOG(LogCatRun, Log, TEXT("Event=persistence_generation_deleted RequestId=%s File=%s Removed=%d"),
					*RequestId.ToString(), *Path, bRemoved);
			}
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, RequestId, bDeleted]()
		{
			if (UCatSaveSubsystem* Self = WeakThis.Get(); Self && Self->bBusy)
			{
				if (bDeleted)
				{
					Self->PendingDeletionSlotIds.Reset();
				}
				Self->FinishDiskRequest(RequestId, bDeleted, bDeleted
					? FText::FromString(TEXT("存档目录已提交，待删除文件已清理。"))
					: FText::FromString(TEXT("目录已提交，但部分存档文件清理失败；刷新目录会重试，删除意图已保留。")));
			}
		});
	});
}

// Run 写回流程：新建和检查点共用同一提交链；新代失败时旧目录不变，成功则从旧目录构造带真实代号的候选摘要，最终成功留给索引回执。
void UCatSaveSubsystem::HandleRunSaved(const FGuid RequestId, const FName SlotId, const FString DisplayName,
	const bool bSuccess, const int64 RunGeneration)
{
	if (!bSuccess || !ActiveAsyncRunSaveGame || ActiveAsyncRunSaveGame->SlotId != SlotId || RunGeneration <= 0)
	{
		FinishDiskRequest(RequestId, false, FText::FromString(TEXT("世界存档新代写入或校验失败，旧正式代未被覆盖。")));
		return;
	}
	ActiveAsyncIndexSaveGame = NewObject<UCatSaveIndexSaveGame>(this);
	ActiveAsyncIndexSaveGame->SlotSummaries = SlotSummaries;
	ActiveAsyncIndexSaveGame->PendingDeletionSlotIds = PendingDeletionSlotIds;
	FCatSaveSlotSummary* Existing = ActiveAsyncIndexSaveGame->SlotSummaries.FindByPredicate(
		[SlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == SlotId; });
	FCatSaveSlotSummary& Summary = Existing ? *Existing : ActiveAsyncIndexSaveGame->SlotSummaries.AddDefaulted_GetRef();
	Summary = MakeSummary(*ActiveAsyncRunSaveGame, DisplayName);
	Summary.RunGeneration = RunGeneration;
	SaveIndexAsync(RequestId, SlotId);
}

// 世界槽读取回调流程：核对真实类型、版本和实例 ID 后才持有载荷并开放旅行；退到旧有效代时摘要改用实际载荷观测值，磁盘正式代号仍由索引保持，失败不允许旅行。
void UCatSaveSubsystem::HandleRunLoaded(const FString& SlotName, const int32 UserIndex, USaveGame* LoadedGame,
	const FGuid RequestId, const FName RequestedSlotId)
{
	if (!bBusy)
	{
		return;
	}
	UCatRunSaveGame* LoadedRunSaveGame = Cast<UCatRunSaveGame>(LoadedGame);
	FText Failure;
	if (!LoadedRunSaveGame || !ValidateLoadedRunSaveGame(*LoadedRunSaveGame, RequestedSlotId, Failure))
	{
		bBusy = false;
		RejectPendingRestore(Failure.IsEmpty() ? FText::FromString(TEXT("世界存档读取或校验失败。")) : Failure);
		return;
	}
	PendingRestoreSaveGame = LoadedRunSaveGame;
	if (FCatSaveSlotSummary* Summary = SlotSummaries.FindByPredicate(
		[RequestedSlotId](const FCatSaveSlotSummary& Entry) { return Entry.SlotId == RequestedSlotId; }))
	{
		const int64 CommittedGeneration = Summary->RunGeneration;
		*Summary = MakeSummary(*LoadedRunSaveGame, Summary->DisplayName);
		Summary->RunGeneration = CommittedGeneration;
	}
	ActiveSlotId = RequestedSlotId;
	bWorldRestoreApplied = false;
	bLoadedRunForTravel = true;
	RestoredPlayerStableNetIds.Reset();
	CapturedDepartingControllers.Reset();
	PlayerCaptureFailure = FText::GetEmpty();
	ActiveRunStartedWorldSeconds = -1.0;
	ActiveRunBasePlayedDurationSeconds = LoadedRunSaveGame->PlayedDurationSeconds;
	bBusy = false;
	LastResultText = FText::FromString(TEXT("存档已读取，等待房主进入玩法世界。"));
	OnChanged.Broadcast();
}

// 磁盘请求收口流程：先清候选对象和 busy，保留退出时更新的正式玩家基线，再写结果与关联日志；最后广播 UI 和有效请求回执，回调可安全发起下一次请求。
void UCatSaveSubsystem::FinishDiskRequest(const FGuid RequestId, const bool bSuccess, const FText& Message)
{
	ActiveAsyncRunSaveGame = nullptr;
	ActiveAsyncIndexSaveGame = nullptr;
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

// 索引验证流程：验证格式、正式 Run 代号、唯一槽摘要和展示数值；删除意图必须与可见槽互斥、无重复且不指向活动槽，失败不得覆盖内存目录或删除文件。
bool UCatSaveSubsystem::ValidateIndex(const UCatSaveIndexSaveGame& Index, FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	if (Index.FormatVersion != CurrentIndexFormatVersion)
	{
		OutFailure = FText::FromString(TEXT("存档目录版本不受支持。"));
		return false;
	}
	TSet<FName> SeenSlotIds;
	for (const FCatSaveSlotSummary& Summary : Index.SlotSummaries)
	{
		if (!IsValidSlotId(Summary.SlotId) || Summary.RunGeneration <= 0 || Summary.DisplayName.TrimStartAndEnd().IsEmpty()
			|| SeenSlotIds.Contains(Summary.SlotId) || Summary.DayIndex < 0
			|| !FMath::IsFinite(Summary.PlayedDurationSeconds) || Summary.PlayedDurationSeconds < 0.0
			|| Summary.SacrificeProgress < 0 || Summary.SacrificeTarget < 0)
		{
			OutFailure = FText::FromString(TEXT("存档目录含有无效或重复槽摘要。"));
			return false;
		}
		SeenSlotIds.Add(Summary.SlotId);
	}
	for (const FName SlotId : Index.PendingDeletionSlotIds)
	{
		if (!IsValidSlotId(SlotId) || SlotId == ActiveSlotId || SeenSlotIds.Contains(SlotId))
		{
			OutFailure = FText::FromString(TEXT("存档删除意图无效、重复或仍指向活动槽。"));
			return false;
		}
		SeenSlotIds.Add(SlotId);
	}
	return true;
}

// 待恢复拒绝流程：统一清掉旅行许可、已恢复标记和载荷强引用；Online 因而不能以旧成功读取结果发起玩法旅行或重复局部恢复。
void UCatSaveSubsystem::RejectPendingRestore(const FText& Failure)
{
	bLoadedRunForTravel = false;
	bWorldRestoreApplied = false;
	PendingRestoreSaveGame = nullptr;
	RestoredPlayerStableNetIds.Reset();
	ActiveRunStartedWorldSeconds = -1.0;
	ActiveRunBasePlayedDurationSeconds = 0.0;
	LastResultText = Failure;
	OnChanged.Broadcast();
	UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_restore_rejected Slot=%s Reason=%s"),
		*ActiveSlotId.ToString(), *Failure.ToString());
}
