#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "ShopEconomy/CatFishBuyerActor.h"

namespace CatFishSaleNetwork
{
	/** 本用例临时改写的编辑器启动设置；双端 PIE 完全销毁后整体恢复，不保存到用户配置文件。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 在任何改写之前读取网络模式、端数、进程选择和驱动定义，作为结束阶段的恢复来源。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode);
			Settings->GetPlayNumberOfClients(ClientCount);
			Settings->GetRunUnderOneProcess(bOneProcess);
			NetDrivers = GEngine->NetDriverDefinitions;
		}

		/** 先等所有 PIE 世界消失，再写回共享默认设置和驱动表，避免结束流程覆盖恢复结果。 */
		bool Update() override
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE && Context.World()) return false;
			}
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode);
			Settings->SetPlayNumberOfClients(ClientCount);
			Settings->SetRunUnderOneProcess(bOneProcess);
			GEngine->NetDriverDefinitions = NetDrivers;
			return true;
		}

	private:
		/** 原 PIE 网络模式；构造时读取，结束时恢复，避免把房主模式留给用户。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 原参与端数；本测试临时使用两端，收尾恢复原值。 */
		int32 ClientCount = 1;
		/** 原单进程选择；仅为本测试的世界观察临时开启，收尾恢复。 */
		bool bOneProcess = true;
		/** 原网络驱动表；测试结束整体写回，保留用户原有在线驱动及 fallback。 */
		TArray<FNetDriverDefinition> NetDrivers;
	};

	/** 正式 TestMap 的售鱼 RPC 回归；在真实准入和 GAS 下观察双端结果，不替代交易、角色或 GameMode。 */
	class FVerifyFishSale final : public IAutomationLatentCommand
	{
	public:
		/** 保存框架拥有的断言接收者；PIE 尚未启动，不在构造时占用网络等待预算。 */
		explicit FVerifyFishSale(FAutomationTestBase* InTest) : Test(InTest) {}

		/** 成功、失败和超时均销毁本用例创建的两件服务器 Actor；其余世界状态由随后排队的 EndPIE 清理。 */
		~FVerifyFishSale() override
		{
			if (ServerGuard.IsValid()) ServerGuard->Destroy();
			if (ServerBuyer.IsValid()) ServerBuyer->Destroy();
		}

		/** 按真实异步链逐步推进：
		 * 1. 等待正式身份和玩法门，创建近距蓝图并在原鱼护装入两条冻结重量鱼。
		 * 2. 等客户端读到完整实例后提交 GUID 列表，收到成功回执并确认双端库存清空、余额增加 96。
		 * 3. 重发同一请求，必须收到终态重放回执且双端金额不再增加，然后在同一鱼护装入第二对鱼。
		 * 4. 冻结客户端第二份列表，服务器移走第二条后提交旧列表，检查 NotFound 且第一条原格原实例仍在。
		 * 任一前提失败立即报告；复制未收敛继续等待，超时报告所在阶段，不把静止余额当作请求完成。 */
		bool Update() override
		{
			if (StartedAt <= 0.0) StartedAt = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StartedAt > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Formal fish sale timed out: stage=%d waiting=%s"), Stage, *WaitingFor));
				return true;
			}
			if (Stage == 0) return PrepareWorlds();
			if (!ServerWorld.IsValid() || !ClientWorld.IsValid() || !ServerController.IsValid()
				|| !ClientController.IsValid() || !ServerGuard.IsValid() || !ServerBuyer.IsValid())
			{
				Test->AddError(TEXT("Formal fish sale lost a required PIE world, player or spawned actor."));
				return true;
			}

			if (Stage == 1 || Stage == 4)
			{
				WaitingFor = TEXT("both fish identities/weights and baseline GAS balance replicated");
				for (TActorIterator<ACatFishGuardActor> It(ClientWorld.Get()); It; ++It)
				{
					if (It->GetFName() == ServerGuard->GetFName()) ClientGuard = *It;
				}
				for (TActorIterator<ACatFishBuyerActor> It(ClientWorld.Get()); It; ++It)
				{
					if (It->GetFName() == ServerBuyer->GetFName()) ClientBuyer = *It;
				}
				if (!ClientGuard.IsValid() || !ClientBuyer.IsValid()
					|| !BothSidesMatch(PairIds, InitialBalance + (Stage == 4 ? 96.0f : 0.0f))) return false;
				if (!Test->TestTrue(TEXT("real gameplay gate, standing character and server reachability remain valid"), CanSubmit())
					|| !Test->TestTrue(TEXT("replicated buyer can serve this local client and ground guard"),
						ClientBuyer->CanServeSource(ClientController.Get(), ClientGuard.Get()))) return true;

				// 载荷取自客户端实际复制条目；第二轮保持第一条有效、第二条缺失，才能发现边遍历边扣鱼的部分提交。
				SubmittedIds.Reset();
				for (const FGuid Id : PairIds)
				{
					UCatFishOnlyInventoryComponent* Inventory = ClientGuard->GetFishInventoryComponent();
					const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(Inventory->FindInventorySlotIndexFromInstanceId(Id));
					SubmittedIds.Add(Entry->Instance->GetItemInstanceId());
				}
				if (Stage == 4)
				{
					UCatFishOnlyInventoryComponent* Inventory = ServerGuard->GetFishInventoryComponent();
					SurvivorSlot = Inventory->FindInventorySlotIndexFromInstanceId(SubmittedIds[0]);
					Survivor = Inventory->GetInventoryEntryAtSlot(SurvivorSlot)->Instance;
					FCatInventoryEntry Removed;
					if (!Test->TestTrue(TEXT("server removes only the second fish before stale client submission"),
						Inventory->RemoveInventoryEntryAtSlotFromAuthority(
							Inventory->FindInventorySlotIndexFromInstanceId(SubmittedIds[1]), Removed))
						|| !Test->TestTrue(TEXT("removed fish is exactly the second frozen GUID"),
							Removed.Instance && Removed.Instance->GetItemInstanceId() == SubmittedIds[1])) return true;
				}
				ClientController->ServerSellFishBatch(Stage == 1 ? SaleRequestId : StaleRequestId,
					ClientBuyer.Get(), ClientGuard.Get(), SubmittedIds);
				++Stage;
				return false;
			}

			const FCatDomainCommandResult Result = ClientController->GetLastCampCommandResult();
			if (Stage == 2)
			{
				WaitingFor = TEXT("first successful RPC receipt, empty original guard and +96 GAS balance on both peers");
				if (Result.RequestId != SaleRequestId) return false;
				if (!Test->TestTrue(TEXT("first sale commits successfully through owning-client receipt"),
					Result.bCommitted && !Result.bTerminalReplay && Result.Error == ECatDomainCommandError::None)) return true;
				if (!BothSidesMatch({}, InitialBalance + 96.0f)) return false;
				ClientController->ServerSellFishBatch(SaleRequestId, ClientBuyer.Get(), ClientGuard.Get(), SubmittedIds);
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
				WaitingFor = TEXT("same RequestId terminal replay receipt with unchanged inventory and GAS balance");
				// 前次回执仍留在 PC 上，只有显式重放标记才证明第二次 RPC 已经过服务器。
				if (Result.RequestId != SaleRequestId || !Result.bTerminalReplay) return false;
				if (!Test->TestTrue(TEXT("replay acknowledges the original commit without committing again"),
					!Result.bCommitted && Result.bReplayedTerminalCommitted
					&& Result.Error == ECatDomainCommandError::AlreadyResolved
					&& Result.ReplayedTerminalError == ECatDomainCommandError::None)) return true;
				if (!BothSidesMatch({}, InitialBalance + 96.0f)) return false;
				if (!SeedPair()) return true;
				Stage = 4;
				return false;
			}
			if (Stage == 5)
			{
				WaitingFor = TEXT("stale-list NotFound receipt, original surviving fish and unchanged GAS balance on both peers");
				if (Result.RequestId != StaleRequestId) return false;
				if (!Test->TestTrue(TEXT("stale list is rejected as a whole because one fish is missing"),
					Result.Error == ECatDomainCommandError::NotFound && !Result.bCommitted && !Result.bTerminalReplay)) return true;
				if (!BothSidesMatch({PairIds[0]}, InitialBalance + 96.0f)) return false;
				const FCatInventoryEntry* Remaining = ServerGuard->GetFishInventoryComponent()->GetInventoryEntryAtSlot(SurvivorSlot);
				if (!Test->TestTrue(TEXT("rejected batch preserves the other fish's original slot, instance and quantity"),
					Survivor.IsValid() && Remaining && Remaining->Instance == Survivor.Get() && Remaining->StackCount == 1)) return true;
				Test->AddInfo(TEXT("Event=formal_fish_sale_network Result=Batch96Replicated_ReplayUnchanged_StaleBatchRejected UI=NotCovered"));
				return true;
			}
			Test->AddError(TEXT("Formal fish sale reached an unknown stage."));
			return true;
		}

	private:
		/** 查找真实 listen server 和唯一客户端，按继承 UniqueId 匹配权威 PC；
		 * 等正式 GameMode 开门后读取初始 GAS 余额，在角色近处生成正式鱼护和买家，再为原鱼护播种。
		 * 未完成登录或启动时继续等，资产、生成或空间前提失败则报告并结束，不写身份或 Run 状态。 */
		bool PrepareWorlds()
		{
			WaitingFor = TEXT("formal TestMap listen server and one remote client");
			int32 RemoteClients = 0;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_ListenServer) ServerWorld = World;
				else if (World->GetNetMode() == NM_Client) { ClientWorld = World; ++RemoteClients; }
			}
			if (!ServerWorld.IsValid() || !ClientWorld.IsValid()) return false;
			if (!Test->TestEqual(TEXT("exactly one remote client participates"), RemoteClients, 1)) return true;
			WaitingFor = TEXT("local client PC, actual PlayerState UniqueId and matching server character");
			ClientController = Cast<ACatfishingPlayerController>(ClientWorld->GetFirstPlayerController());
			if (!ClientController.IsValid() || !ClientController->IsLocalController() || !ClientController->GetLocalPlayer()
				|| !Cast<ACatCharacter>(ClientController->GetPawn())) return false;
			const APlayerState* ClientState = ClientController->GetPlayerState<APlayerState>();
			if (!ClientState || !ClientState->GetUniqueId().IsValid()) return false;
			for (TActorIterator<ACatfishingPlayerController> It(ServerWorld.Get()); It; ++It)
			{
				const APlayerState* State = It->GetPlayerState<APlayerState>();
				if (!It->IsLocalController() && State && State->GetUniqueId().IsValid()
					&& State->GetUniqueId()->ToString() == ClientState->GetUniqueId()->ToString()) ServerController = *It;
			}
			if (!ServerController.IsValid()) return false;
			ACatCharacter* Character = Cast<ACatCharacter>(ServerController->GetPawn());
			ACatfishingGameModeBase* GameMode = ServerWorld->GetAuthGameMode<ACatfishingGameModeBase>();
			WaitingFor = TEXT("formal GameMode CanAcceptGameplayCommand and standing character");
			if (!Character || !GameMode || !GameMode->CanAcceptGameplayCommand(ServerController.Get())
				|| !Character->GetConditionComponent() || Character->GetConditionComponent()->GetSnapshot().bDowned) return false;
			ACatfishingGameState* State = ServerWorld->GetGameState<ACatfishingGameState>();
			WaitingFor = TEXT("GameState economy AttributeSet");
			if (!State || !State->GetEconomyAttributeSet()) return false;
			InitialBalance = State->GetEconomyAttributeSet()->GetTeamWalletBalance();

			UClass* GuardClass = LoadClass<ACatFishGuardActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatGuard.BP_CatGuard_C"));
			UClass* BuyerClass = LoadClass<ACatFishBuyerActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatFishBuyer.BP_CatFishBuyer_C"));
			if (!Test->TestNotNull(TEXT("formal BP_CatGuard loads"), GuardClass)
				|| !Test->TestNotNull(TEXT("formal BP_CatFishBuyer loads"), BuyerClass)) return true;
			const FVector Origin = Character->GetActorLocation();
			const FVector Forward = Character->GetActorForwardVector();
			const FVector Right = Character->GetActorRightVector();
			ServerGuard = ServerWorld->SpawnActor<ACatFishGuardActor>(GuardClass, Origin + Forward * 120.0, FRotator::ZeroRotator);
			ServerBuyer = ServerWorld->SpawnActor<ACatFishBuyerActor>(BuyerClass, Origin + Forward * 120.0 + Right * 120.0, FRotator::ZeroRotator);
			if (!Test->TestNotNull(TEXT("authority spawns formal ground guard"), ServerGuard.Get())
				|| !Test->TestNotNull(TEXT("authority spawns formal fish buyer"), ServerBuyer.Get())
				|| !Test->TestTrue(TEXT("spawned formal actors satisfy unmodified gameplay and spatial gates"), CanSubmit())) return true;
			ServerGuard->ForceNetUpdate();
			ServerBuyer->ForceNetUpdate();
			if (!SeedPair()) return true;
			Stage = 1;
			return false;
		}

		/** 读取正式玩法门、身体和两段空间规则；全部满足才允许测试发请求，不修改任何生产资格。 */
		bool CanSubmit() const
		{
			const ACatfishingGameModeBase* GameMode = ServerWorld->GetAuthGameMode<ACatfishingGameModeBase>();
			const ACatCharacter* Character = Cast<ACatCharacter>(ServerController->GetPawn());
			return GameMode && GameMode->CanAcceptGameplayCommand(ServerController.Get())
				&& Character && Character->GetConditionComponent() && !Character->GetConditionComponent()->GetSnapshot().bDowned
				&& ServerGuard.IsValid() && ServerBuyer.IsValid()
				&& ServerBuyer->CanServeSource(ServerController.Get(), ServerGuard.Get())
				&& CatInventoryAccessRules::IsHostReachable(ServerGuard.Get(), Character, GetDefault<UCatCampSettings>());
		}

		/** 加载正式 RiverPattern 定义，用已登录玩家身份初始化两条各 2.5 kg 的独立鱼，再经正式入库入口写入同一鱼护；
		 * 保存本轮 GUID 供后续与客户端复制匹配，失败立即返回，不创建替代价格或余额写口。 */
		bool SeedPair()
		{
			UCatFishDefinition* Definition = LoadObject<UCatFishDefinition>(nullptr,
				TEXT("/Game/Catfishing/Data/Fish/Fish_RiverPattern.Fish_RiverPattern"));
			UCatFishOnlyInventoryComponent* Inventory = ServerGuard->GetFishInventoryComponent();
			if (!Test->TestNotNull(TEXT("formal Fish_RiverPattern definition loads"), Definition)
				|| !Test->TestNotNull(TEXT("formal guard owns its fish inventory"), Inventory)) return false;
			PairIds.Reset();
			const FString OwnerId = ServerController->GetPlayerState<APlayerState>()->GetUniqueId()->ToString();
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const FGuid Id = FGuid::NewGuid();
				UCatFishInventoryItemInstance* Fish = NewObject<UCatFishInventoryItemInstance>(ServerGuard.Get());
				Fish->SetItemDefinition(Definition);
				if (!Test->TestTrue(TEXT("authority freezes formal fish identity and 2.5 kg weight"),
					Fish->InitializeFishFromAuthority(FGuid::NewGuid(), Id, OwnerId, 2.5))
					|| !Test->TestTrue(TEXT("original guard accepts the real fish instance"), Inventory->AddItemInstance(Fish, 1))) return false;
				PairIds.Add(Id);
			}
			ServerGuard->ForceNetUpdate();
			return true;
		}

		/** 从指定鱼护的实际组件读取所有格子，核对唯一 GUID、正式库存定义 Fish_RiverPattern、数量和冻结重量；
		 * 库存 ID 来自鱼定义的 GetInventoryDefinitionId/FishDefinitionId，不使用表现字段 FishId。
		 * 空目标要求没有任何剩余实物，实例尚未复制完整时返回 false，让调用方继续等待。 */
		static bool InventoryMatches(const ACatFishGuardActor* Guard, const TArray<FGuid>& ExpectedIds)
		{
			if (!Guard || !Guard->GetFishInventoryComponent()) return false;
			TSet<FGuid> Seen;
			for (const FCatInventoryEntry& Entry : Guard->GetFishInventoryComponent()->GetInventoryEntries())
			{
				if (!Entry.Instance && Entry.StackCount == 0) continue;
				const UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance);
				if (!Fish || Entry.StackCount != 1 || !ExpectedIds.Contains(Fish->GetItemInstanceId())
					|| Seen.Contains(Fish->GetItemInstanceId()) || Fish->GetItemDefinitionId() != TEXT("RiverPatternFish")
					|| Fish->GetFishWeightKilograms() != 2.5) return false;
				Seen.Add(Fish->GetItemInstanceId());
			}
			return Seen.Num() == ExpectedIds.Num();
		}

		/** 同时读取服务器和客户端的原鱼护及各自 GameState 经济属性；只有两份独立复制事实都吻合才推进阶段。
		 * 金额是精确整数，重量是可精确表示的 2.5，不引入会掩盖重复入账的数值容差。 */
		bool BothSidesMatch(const TArray<FGuid>& ExpectedIds, const float ExpectedBalance) const
		{
			const ACatfishingGameState* ServerState = ServerWorld->GetGameState<ACatfishingGameState>();
			const ACatfishingGameState* ClientState = ClientWorld->GetGameState<ACatfishingGameState>();
			return ServerState && ClientState && ServerState->GetEconomyAttributeSet() && ClientState->GetEconomyAttributeSet()
				&& ServerState->GetEconomyAttributeSet()->GetTeamWalletBalance() == ExpectedBalance
				&& ClientState->GetEconomyAttributeSet()->GetTeamWalletBalance() == ExpectedBalance
				&& InventoryMatches(ServerGuard.Get(), ExpectedIds) && InventoryMatches(ClientGuard.Get(), ExpectedIds);
		}

		/** 框架拥有的测试对象；本命令仅在队列执行期间向其提交断言。 */
		FAutomationTestBase* Test = nullptr;
		/** 首次 PIE 轮询的单调时间；超时检查读取，排除启动前资源加载时间。 */
		double StartedAt = 0.0;
		/** 当前异步步骤；只有回执与复制事实满足后写入下一步，避免重复提交。 */
		int32 Stage = 0;
		/** 当前等待的具体前提；每阶段更新，超时消息据此定位框架或业务阻塞。 */
		FString WaitingFor;
		/** 首次交易前权威 GAS 余额；后续只做差额断言，测试不改初始经济配置。 */
		float InitialBalance = 0.0f;
		/** 房主的权威 PIE 世界；播种、移鱼和服务器事实读取都限定在这里。 */
		TWeakObjectPtr<UWorld> ServerWorld;
		/** 唯一远端 PIE 世界；客户端载荷、回执和复制断言都从这里读取。 */
		TWeakObjectPtr<UWorld> ClientWorld;
		/** 远端玩家在服务器上的正式 PC；按真实 UniqueId 关联，用于资格检查。 */
		TWeakObjectPtr<ACatfishingPlayerController> ServerController;
		/** 远端拥有的本地 PC；它实际发送 RPC 并保存服务器返回的领域回执。 */
		TWeakObjectPtr<ACatfishingPlayerController> ClientController;
		/** 测试生成的唯一原鱼护；两轮都复用其库存，命令析构时销毁。 */
		TWeakObjectPtr<ACatFishGuardActor> ServerGuard;
		/** 原鱼护的客户端复制副本；按服务器对象名定位，用于提交真实 Actor 引用和读实例。 */
		TWeakObjectPtr<ACatFishGuardActor> ClientGuard;
		/** 正式蓝图生成的服务器买家；保留原服务半径和碰撞规则，析构时销毁。 */
		TWeakObjectPtr<ACatFishBuyerActor> ServerBuyer;
		/** 买家的客户端复制副本；RPC 必须提交客户端自身世界中的引用。 */
		TWeakObjectPtr<ACatFishBuyerActor> ClientBuyer;
		/** 首笔出售的稳定请求标识；首次与重放共用，确保命中同一终态记录。 */
		FGuid SaleRequestId = FGuid::NewGuid();
		/** 第二笔失效列表的独立请求标识；避免把首笔缓存误判为整单拒绝。 */
		FGuid StaleRequestId = FGuid::NewGuid();
		/** 当前一对服务器鱼的身份；每次播种替换，用于精确匹配客户端复制。 */
		TArray<FGuid> PairIds;
		/** 从客户端实际鱼实例冻结的提交列表；重放保持原样，第二笔移鱼也不更新这份旧载荷。 */
		TArray<FGuid> SubmittedIds;
		/** 第二笔应保留的鱼原实例；弱引用让断言能发现被错误移除或替换，不替库存保活。 */
		TWeakObjectPtr<UCatInventoryItemInstance> Survivor;
		/** 第二笔应保留的原格位；拒绝后检查同一格位，防止靠重新入库掩盖部分提交。 */
		int32 SurvivorSlot = INDEX_NONE;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishSaleFormalNetworkTest,
	"Catfishing.Editor.ShopEconomy.FishSale.FormalTwoEndpointBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 确认编辑器空闲并保存默认设置，临时配置双端 IP PIE 后依序排入正式地图加载、场景断言、结束和恢复；
 * 返回 true 仅表示命令已排队，所有业务结论由之后的真实 RPC 回执和复制检查产生。 */
bool FCatFishSaleFormalNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor and engine"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const TSharedRef<CatFishSaleNetwork::FRestoreSettings> Restore = MakeShared<CatFishSaleNetwork::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			// 与现有网络用例一致：主驱动和 fallback 都用 IP，避免在线平台不可用时改变本机回归路径。
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishSaleNetwork::FVerifyFishSale>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
