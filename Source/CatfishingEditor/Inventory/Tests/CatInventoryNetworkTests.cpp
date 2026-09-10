#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Camp/CatCampInventoryActor.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "UI/Inventory/CatInventoryModel.h"

namespace CatInventoryNetwork
{
	/** PIE 网络设置的测试前快照；它代表编辑器共享的启动环境，构造时读取、PIE 完全退出后写回，避免本用例改变其他联机测试的驱动或客户端数量。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 记录本次 PIE 前的网络启动设置和 NetDriver 定义，供测试结束后无条件恢复。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode);
			Settings->GetPlayNumberOfClients(PlayNumberOfClients);
			Settings->GetRunUnderOneProcess(bRunUnderOneProcess);
			NetDriverDefinitions = GEngine->NetDriverDefinitions;
		}

		/** 在 PIE 关闭后恢复编辑器网络设置；PlayWorld 还存在时继续等待，防止恢复后的驱动被本轮 PIE 重写。 */
		bool Update() override
		{
			if (GEditor->PlayWorld != nullptr)
			{
				return false;
			}

			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode);
			Settings->SetPlayNumberOfClients(PlayNumberOfClients);
			Settings->SetRunUnderOneProcess(bRunUnderOneProcess);
			GEngine->NetDriverDefinitions = NetDriverDefinitions;
			return true;
		}

	private:
		/** 测试前的 PIE 联网模式；恢复命令将它写回编辑器默认设置。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 测试前的 PIE 总参与者数；本用例临时设为 3 以启动一个 listen server 和两个远端客户端。 */
		int32 PlayNumberOfClients = 1;
		/** 测试前是否单进程运行 PIE；保存它使其他编辑器测试保留自己的进程隔离选择。 */
		bool bRunUnderOneProcess = true;
		/** 测试前所有 NetDriver 定义；本用例临时换成 IP 驱动后整体恢复，避免保留 Steam 驱动改写。 */
		TArray<FNetDriverDefinition> NetDriverDefinitions;
	};

	/** 实际 PIE 复制链路的轮询命令；服务器只做正式库存写入，两个客户端只通过复制后的组件事件驱动各自 Model 重建列表。 */
	class FVerifyReplicatedInventory final : public IAutomationLatentCommand
	{
	public:
		/** 创建网络验证状态机；Test 由 Automation 框架拥有，命令只在本轮 PIE 存活期间写入断言结果。 */
		explicit FVerifyReplicatedInventory(FAutomationTestBase* InTest)
			: Test(InTest)
			, StartedAtSeconds(FPlatformTime::Seconds())
		{
		}

		/** 推进真实复制验证：
		 * 1. 等待 listen server、两个 client world 和同一营地 Actor 的复制对象全部可读。
		 * 2. 让每个客户端营地组件按实际 UI 路径创建自己的 Model，绝不直接调用 Refresh、SetInventoryList 或 OnRep。
		 * 3. 服务器依次新增、移动、清空和交换正式槽位；每阶段都等待两个客户端的组件条目与该组件独立 Model 列表同时到达预期。
		 * 4. 任一依赖缺失或超时会记录可定位错误并结束，成功时才结束 latent command 让 PIE 收尾。 */
		bool Update() override
		{
			if (FPlatformTime::Seconds() - StartedAtSeconds > TimeoutSeconds)
			{
				Test->AddError(FString::Printf(TEXT("Inventory network replication timeout at stage %d"), Stage));
				return true;
			}

			UWorld* ServerWorld = nullptr;
			TArray<UWorld*> ClientWorlds;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World == nullptr || Context.WorldType != EWorldType::PIE)
				{
					continue;
				}
				if (World->GetNetMode() == NM_ListenServer)
				{
					ServerWorld = World;
				}
				else if (World->GetNetMode() == NM_Client)
				{
					ClientWorlds.Add(World);
				}
			}

			if (ServerWorld == nullptr || ClientWorlds.Num() < RequiredRemoteClientCount)
			{
				return false;
			}
			if (ClientWorlds.Num() != RequiredRemoteClientCount)
			{
				Test->AddError(FString::Printf(TEXT("Expected %d remote PIE clients, got %d"), RequiredRemoteClientCount, ClientWorlds.Num()));
				return true;
			}

			if (Stage == 0)
			{
				return CreateServerCampAndClientModels(ServerWorld, ClientWorlds);
			}

			if (!AreAllClientViewsReady())
			{
				return false;
			}

			switch (Stage)
			{
			case 1:
				if (!DoClientsShow(TEXT("BugBait"), NAME_None, 0, 4)) return false;
				return MoveServerEntryToSlotFour();
			case 2:
				if (!DoClientsShow(NAME_None, TEXT("BugBait"), 0, 4)) return false;
				return ClearServerSlotFour();
			case 3:
				if (!DoClientsShow(NAME_None, NAME_None, 0, 4)) return false;
				return AddServerEntriesForExchange();
			case 4:
				if (!DoClientsShow(TEXT("BugBait"), TEXT("FlashingBait"), 0, 1)) return false;
				return ExchangeServerEntries();
			case 5:
				if (!DoClientsShow(TEXT("FlashingBait"), TEXT("BugBait"), 0, 1)) return false;
				return ReplaceServerEntriesFromAuthority();
			case 6:
				if (!DoClientsShow(TEXT("BugBait"), TEXT("FlashingBait"), 0, 1)) return false;
				return ResizeServerSnapshot(2, 7);
			case 7:
				if (ClientModels[0]->GetInventoryList().Num() != 2 || ClientModels[1]->GetInventoryList().Num() != 2) return false;
				if (!DoClientsShow(TEXT("BugBait"), TEXT("FlashingBait"), 0, 1)) return false;
				return ResizeServerSnapshot(48, 8);
			case 8:
				if (ClientModels[0]->GetInventoryList().Num() != 48 || ClientModels[1]->GetInventoryList().Num() != 48) return false;
				if (!DoClientsShow(TEXT("BugBait"), TEXT("FlashingBait"), 0, 1) || !DoClientsShow(NAME_None, NAME_None, 2, 47)) return false;
				Test->AddInfo(TEXT("Event=inventory_ui_multiclient_replication Result=AddMoveClearExchangeReplaceShrinkGrowOnBothRemoteModels"));
				return true;
			default:
				Test->AddError(TEXT("Inventory network test reached an unknown stage."));
				return true;
			}
		}

	private:
		/** 在 listen server 创建唯一的可复制营地仓库，并为两个 client world 找到其复制副本后取得组件独立 Model；初始化完成后服务器才新增第一件正式物品。 */
		bool CreateServerCampAndClientModels(UWorld* ServerWorld, const TArray<UWorld*>& ClientWorlds)
		{
			if (!ServerCamp.IsValid())
			{
				ACatCampInventoryActor* SpawnedCamp = ServerWorld->SpawnActor<ACatCampInventoryActor>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
				if (!Test->TestNotNull(TEXT("server creates replicated camp inventory"), SpawnedCamp)
					|| !Test->TestNotNull(TEXT("server camp has formal inventory"), SpawnedCamp->GetInventoryComponent()))
				{
					return true;
				}
				SpawnedCamp->bAlwaysRelevant = true;
				SpawnedCamp->ForceNetUpdate();
				ServerCamp = SpawnedCamp;
			}

			if (ClientCamps.Num() == 0)
			{
				TArray<ACatCampInventoryActor*> ResolvedClientCamps;
				for (UWorld* ClientWorld : ClientWorlds)
				{
					if (ClientWorld->GetFirstPlayerController() == nullptr)
					{
						return false;
					}
					ACatCampInventoryActor* ClientCamp = FindCampInWorld(ClientWorld);
					if (ClientCamp == nullptr || ClientCamp->GetInventoryComponent() == nullptr)
					{
						return false;
					}
					ResolvedClientCamps.Add(ClientCamp);
				}
				for (int32 ClientIndex = 0; ClientIndex < ClientWorlds.Num(); ++ClientIndex)
				{
					UWorld* ClientWorld = ClientWorlds[ClientIndex];
					ACatCampInventoryActor* ClientCamp = ResolvedClientCamps[ClientIndex];
					ClientCamps.Add(ClientCamp);
					UCatInventoryModel* ClientModel = ClientCamp->GetInventoryComponent()->GetInventoryModel();
					if (!Test->TestNotNull(TEXT("client creates inventory UI model"), ClientModel))
					{
						return true;
					}
					ClientModels.Emplace(ClientModel);
				}
			}

			if (ClientCamps.Num() != RequiredRemoteClientCount || ClientModels.Num() != RequiredRemoteClientCount)
			{
				Test->AddError(TEXT("Each remote client must own one replicated camp actor and one UI model."));
				return true;
			}

			UCatInventoryItemDefinition* BugBait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(TEXT("BugBait"));
			UCatInventoryComponent* ServerInventory = ServerCamp->GetInventoryComponent();
			if (!Test->TestNotNull(TEXT("formal BugBait definition loads for replication"), BugBait)
				|| !Test->TestTrue(TEXT("server adds first formal inventory entry"), ServerInventory->AddItemDefinition(BugBait, 1)))
			{
				return true;
			}
			ServerCamp->ForceNetUpdate();
			Stage = 1;
			return false;
		}

		/** 确认两个客户端的组件和各自 Model 都已拥有可读槽位；该门槛避免把尚未复制的空数组误判为清空成功。 */
		bool AreAllClientViewsReady() const
		{
			for (int32 ClientIndex = 0; ClientIndex < RequiredRemoteClientCount; ++ClientIndex)
			{
				const ACatCampInventoryActor* ClientCamp = ClientCamps.IsValidIndex(ClientIndex) ? ClientCamps[ClientIndex].Get() : nullptr;
				const UCatInventoryModel* ClientModel = ClientModels.IsValidIndex(ClientIndex) ? ClientModels[ClientIndex].Get() : nullptr;
				if (ClientCamp == nullptr || ClientCamp->GetInventoryComponent() == nullptr || ClientModel == nullptr
					|| ClientModel->GetInventoryList().Num() != ClientCamp->GetInventoryComponent()->GetInventoryEntries().Num())
				{
					return false;
				}
			}
			return true;
		}

		/** 逐客户端对比正式复制条目与组件独立 Model 中两个关心格；空 FName 表示该格必须为空，从而专门捕获“物品移走后旧格仍显示”的回归。 */
		bool DoClientsShow(const FName ExpectedFirstDefinition, const FName ExpectedSecondDefinition,
			const int32 FirstSlot, const int32 SecondSlot)
		{
			for (int32 ClientIndex = 0; ClientIndex < RequiredRemoteClientCount; ++ClientIndex)
			{
				const ACatCampInventoryActor* ClientCamp = ClientCamps[ClientIndex].Get();
				const UCatInventoryModel* ClientModel = ClientModels[ClientIndex].Get();
				if (!DoesClientSlotMatch(*ClientCamp, *ClientModel, FirstSlot, ExpectedFirstDefinition)
					|| !DoesClientSlotMatch(*ClientCamp, *ClientModel, SecondSlot, ExpectedSecondDefinition))
				{
					return false;
				}
			}
			return true;
		}

		/** 读取一个客户端正式组件和同一客户端 Model 的原样槽位列表；两者都必须符合期望，避免仅验证复制或仅验证 UI 数据源。 */
		bool DoesClientSlotMatch(const ACatCampInventoryActor& ClientCamp, const UCatInventoryModel& ClientModel,
			const int32 SlotIndex, const FName ExpectedDefinition) const
		{
			if (SlotIndex == INDEX_NONE)
			{
				return true;
			}

			const UCatInventoryComponent* ClientInventory = ClientCamp.GetInventoryComponent();
			const FCatInventoryEntry* ReplicatedEntry = ClientInventory ? ClientInventory->GetInventoryEntryAtSlot(SlotIndex) : nullptr;
			const TArray<FCatInventoryEntry>& ModelEntries = ClientModel.GetInventoryList();
			if (ReplicatedEntry == nullptr || !ModelEntries.IsValidIndex(SlotIndex))
			{
				return false;
			}

			const FCatInventoryEntry& ModelEntry = ModelEntries[SlotIndex];
			const bool bComponentOccupied = ReplicatedEntry->Instance != nullptr && ReplicatedEntry->StackCount > 0;
			const bool bModelOccupied = ModelEntry.Instance != nullptr && ModelEntry.StackCount > 0;
			const bool bExpectedOccupied = !ExpectedDefinition.IsNone();
			if (bComponentOccupied != bExpectedOccupied || bModelOccupied != bExpectedOccupied)
			{
				return false;
			}
			return !bExpectedOccupied || (ReplicatedEntry->Instance->GetItemDefinitionId() == ExpectedDefinition
				&& ModelEntry.Instance->GetItemDefinitionId() == ExpectedDefinition
				&& ReplicatedEntry->StackCount == ModelEntry.StackCount);
		}

		/** 在服务器把第 0 格搬到第 4 格；成功后强制 Actor 调度网络更新，客户端只能通过 FastArray 复制和 Model 委托收到变化。 */
		bool MoveServerEntryToSlotFour()
		{
			UCatInventoryComponent* ServerInventory = ServerCamp->GetInventoryComponent();
			const FCatDomainCommandResult Result = ServerInventory->MoveItemToInventoryFromAuthority(
				FGuid::NewGuid(), 0, ServerInventory, 4, TEXT("InventoryNetworkTestMove"));
			if (!Test->TestTrue(TEXT("server moves inventory entry from slot zero to slot four"), Result.bCommitted))
			{
				return true;
			}
			ServerCamp->ForceNetUpdate();
			Stage = 2;
			return false;
		}

		/** 在服务器清空第 4 格；后续阶段要求两份客户端 View 都把旧格变为空，直接覆盖已移动物品残留显示的故障。 */
		bool ClearServerSlotFour()
		{
			ServerCamp->GetInventoryComponent()->RemoveItemInstanceFromIndex(4);
			ServerCamp->ForceNetUpdate();
			Stage = 3;
			return false;
		}

		/** 在服务器新增两种正式目录物品到空格；它们为下一阶段同库存交换提供可区分的复制和 UI 投影输入。 */
		bool AddServerEntriesForExchange()
		{
			UCatInventoryComponent* ServerInventory = ServerCamp->GetInventoryComponent();
			UCatInventorySettings* Settings = GetMutableDefault<UCatInventorySettings>();
			UCatInventoryItemDefinition* BugBait = Settings->FindRuntimeDefinition(TEXT("BugBait"));
			UCatInventoryItemDefinition* FlashingBait = Settings->FindRuntimeDefinition(TEXT("FlashingBait"));
			if (!Test->TestNotNull(TEXT("formal BugBait remains available"), BugBait)
				|| !Test->TestNotNull(TEXT("formal FlashingBait definition loads"), FlashingBait)
				|| !Test->TestTrue(TEXT("server adds exchange source"), ServerInventory->AddItemDefinition(BugBait, 1))
				|| !Test->TestTrue(TEXT("server adds exchange target"), ServerInventory->AddItemDefinition(FlashingBait, 1)))
			{
				return true;
			}
			ServerCamp->ForceNetUpdate();
			Stage = 4;
			return false;
		}

		/** 在服务器交换两种已复制物品；成功后要求每个 client 的组件和 Model 都看到相反的格位身份。 */
		bool ExchangeServerEntries()
		{
			UCatInventoryComponent* ServerInventory = ServerCamp->GetInventoryComponent();
			if (!Test->TestTrue(TEXT("server exchanges two formal inventory entries"),
				UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(ServerInventory, 0, ServerInventory, 1)))
			{
				return true;
			}
			ServerCamp->ForceNetUpdate();
			Stage = 5;
			return false;
		}

		/** 在服务器以完整条目快照替换当前库存：先交换快照中第 0/1 格，再走正式 Replace 入口；两个客户端必须恢复相同顺序，覆盖整表重置时丢失 FastArray 身份的复制路径。 */
		bool ReplaceServerEntriesFromAuthority()
		{
			UCatInventoryComponent* ServerInventory = ServerCamp->GetInventoryComponent();
			TArray<FCatInventoryEntry> ReplacedEntries = ServerInventory->GetInventoryEntries();
			if (!Test->TestTrue(TEXT("server replacement snapshot owns two occupied slots"),
				ReplacedEntries.IsValidIndex(1) && ReplacedEntries[0].Instance != nullptr && ReplacedEntries[1].Instance != nullptr))
			{
				return true;
			}
			Swap(ReplacedEntries[0], ReplacedEntries[1]);
			if (!Test->TestTrue(TEXT("server replaces formal inventory entries after snapshot reorder"),
				ServerInventory->ReplaceInventoryEntriesFromAuthority(ReplacedEntries, ServerInventory->GetInventorySlotCount())))
			{
				return true;
			}
			ServerCamp->ForceNetUpdate();
			Stage = 6;
			return false;
		}

		/** 用保留头部物品的快照改变格数；先裁剪快照再走正式恢复入口，等待客户端自然删除或新增格子，检验 Model 是否读取复制结束后的数组。 */
		bool ResizeServerSnapshot(const int32 SlotCount, const int32 NextStage)
		{
			UCatInventoryComponent* Inventory = ServerCamp->GetInventoryComponent();
			TArray<FCatInventoryEntry> Snapshot = Inventory->GetInventoryEntries();
			Snapshot.SetNum(SlotCount);
			if (!Test->TestTrue(TEXT("server resizes inventory through replacement"), Inventory->ReplaceInventoryEntriesFromAuthority(Snapshot, SlotCount)))
			{
				return true;
			}
			ServerCamp->ForceNetUpdate();
			Stage = NextStage;
			return false;
		}

		/** 在一个 PIE world 中寻找本用例唯一的营地库存副本；测试使用隔离临时地图，所以第一个命中即为服务器创建的目标。 */
		static ACatCampInventoryActor* FindCampInWorld(UWorld* World)
		{
			for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
			{
				return *It;
			}
			return nullptr;
		}

		/** Automation 断言写入目标；命令不拥有它，PIE 结束前只调用其线程内测试 API。 */
		FAutomationTestBase* Test = nullptr;
		/** 本命令开始的单调时间秒数；超过阈值时结束 PIE，防止复制或 Map 启动失败挂住整个测试队列。 */
		double StartedAtSeconds = 0.0;
		/** 当前服务器写入与客户端观察阶段；只在上一阶段的两个客户端都同步后递增，避免手动刷新掩盖时序问题。 */
		int32 Stage = 0;
		/** 本测试明确要求验证的远端客户端数量；PIE 设置为三个总参与者，因此应产生一个 listen server 和两个 client world。 */
		static constexpr int32 RequiredRemoteClientCount = 2;
		/** 每个复制阶段允许的最长秒数；超时表示真实 PIE 网络或 UI 订阅没有形成闭环。 */
		static constexpr double TimeoutSeconds = 45.0;
		/** 服务器持有的权威公共仓库；所有库存写入只对它执行，再由引擎复制给两个 client world。 */
		TWeakObjectPtr<ACatCampInventoryActor> ServerCamp;
		/** 每个远端客户端上的公共仓库复制副本；数组顺序与 ClientModels 一一对应，只用于读取复制状态。 */
		TArray<TWeakObjectPtr<ACatCampInventoryActor>> ClientCamps;
		/** 每个远端客户端营地组件独立持有的库存 UI Model；它只订阅本客户端组件通知，原样槽位列表是本用例的 UI 数据层断言目标。 */
		TArray<TStrongObjectPtr<UCatInventoryModel>> ClientModels;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryMulticlientReplicationTest,
	"Catfishing.Editor.Inventory.MulticlientCampInventoryReplicatesToModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 配置隔离临时地图和 IP listen-server PIE；完成后排入测试状态机、结束 PIE 与设置恢复命令，返回值只表示启动前置条件成立。 */
bool FCatInventoryMulticlientReplicationTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor"), GEditor != nullptr && GEditor->PlayWorld == nullptr))
	{
		return false;
	}

	const TSharedRef<CatInventoryNetwork::FRestoreSettings> Restore = MakeShared<CatInventoryNetwork::FRestoreSettings>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("creates isolated unsaved map"), Map))
	{
		return false;
	}
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>();

	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(3);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}

	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatInventoryNetwork::FVerifyReplicatedInventory>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif

