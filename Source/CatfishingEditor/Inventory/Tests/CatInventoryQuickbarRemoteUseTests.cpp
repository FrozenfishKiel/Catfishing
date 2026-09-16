#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CatSelectedUseInputTestAccess.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Fishing/InputAbilities/CatFishingChumAbility.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/CatEquipmentUseItemInstances.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Debug/CatFishingDebugSubsystem.h"
#include "Components/LineBatchComponent.h"
#include "HAL/IConsoleManager.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Components/BoxComponent.h"
#include "Interaction/CatInteractionSettings.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Components/PrimitiveComponent.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"

namespace CatInventoryQuickbarRemoteUseTests
{
	/** 远端用例只从服务器上精确来源实例的活动 Spec/Task 读取持续 Use，不读取客户端命令组件影子时间。 */
	bool IsChumUseWaiting(ACatCharacter* Character, const UCatInventoryItemInstance* SourceItem)
	{
		const UCatAbilitySystemComponent* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
		if (!ASC || !SourceItem) return false;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
			if (Spec.SourceObject.Get() == SourceItem && Spec.IsActive())
				if (const UCatGA_FishingChum* Ability = Cast<UCatGA_FishingChum>(Spec.GetPrimaryInstance())) return Ability->IsWaitingForInputRelease();
		return false;
	}
	/** 恢复 PIE 网络设置：本用例结束后写回用户原本的启动拓扑和 NetDriver 定义。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 保存本轮会覆盖的 PIE 设置，保证弱网验证不会污染后续编辑器运行。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode); Settings->GetPlayNumberOfClients(ClientCount); Settings->GetRunUnderOneProcess(bOneProcess);
			NetDrivers = GEngine->NetDriverDefinitions;
		}
		/** 只在 PIE 完整退出后恢复快照，运行中继续等待避免驱动相互覆盖。 */
		virtual bool Update() override
		{
			if (GEditor->PlayWorld) return false;
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode); Settings->SetPlayNumberOfClients(ClientCount); Settings->SetRunUnderOneProcess(bOneProcess);
			GEngine->NetDriverDefinitions = NetDrivers;
			return true;
		}
	private:
		/** 用户原有网络模式。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 用户原有总参与者数。 */
		int32 ClientCount = 1;
		/** 用户原有单进程开关。 */
		bool bOneProcess = true;
		/** 用户原有驱动定义集合。 */
		TArray<FNetDriverDefinition> NetDrivers;
	};

	/** 远端 owning-client 快捷栏状态机：复用同一条正式 左键/Release 路径，在正常或显式弱网条件下验证精确实例与回执。 */
	class FVerifyRemoteSelectedUse final : public IAutomationLatentCommand
	{
	public:
		/** 保存 Automation 断言出口和本次网络场景；状态机不拥有 PIE 对象，也不会修改全局网络配置。 */
		explicit FVerifyRemoteSelectedUse(FAutomationTestBase* InTest, const bool bInSimulateWeakNetwork, const bool bInToolsOnly=false, const bool bInTransferRod=false)
			: Test(InTest), bSimulateWeakNetwork(bInSimulateWeakNetwork), bToolsOnly(bInToolsOnly), bTransferRod(bInTransferRod) {}
		/** 等待复制或按阶段提交真实本地输入；超时输出端点和库存状态供日志复查。 */
		virtual bool Update() override
		{
			if (StartedAt <= 0.0) StartedAt = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StartedAt > 35.0)
			{
				Test->AddError(FString::Printf(TEXT("Remote quickbar use timeout Stage=%d Client=%s ServerBackpack=%s"), Stage,
					*GetNameSafe(ClientController.Get()), *GetNameSafe(ServerBackpack.Get())));
				return true;
			}
			switch (Stage)
			{
			case 0: return Prepare();
			case 1: return SeedServerInventory();
			case 2: return WaitForRemoteInstances();
			case 3: return StartRemoteSecondChumThenChangeSelection();
			case 4: return WaitForRemoteSecondChumThenRelease();
			case 5: return VerifyRemoteChumConsumption();
			case 6: return DeployRemoteSecondRod();
			case 7: return VerifySecondRodPayload();
			case 8: return RequestRemoteTargetedRodLeave();
			case 9: return VerifyRemoteTargetedRodLeave();
			case 10: return VerifyRemoteTargetedRodLeaveReplay();
			case 11: return RequestRemoteTargetedRodOperate();
			case 12: return VerifyRemoteTargetedRodOperate();
			case 13: return PrepareRemoteScoop();
			case 14: return AimRemoteScoop();
			case 15:
				if (FPlatformTime::Seconds()-ScoopAimAt<0.8) return false;
				if (!IsScoopAimReady()) return false;
				FCatSelectedUseInputTestAccess::Press(ClientController.Get());
				FCatSelectedUseInputTestAccess::Release(ClientController.Get());
				Stage=16; return false;
			case 16: return VerifyRemoteScoop();
			case 17: return BeginRemoteChumCancellation();
			case 18: return CancelRemoteChum();
			case 19: return VerifyRemoteChumCancellation();
			case 20: return VerifyRemotePackedRodSelection();
			default: Test->AddError(TEXT("Remote quickbar use test reached an unknown stage.")); return true;
			}
		}
	private:
		/** 找到 listen server 与唯一远端 owning client，再按 PlayerState 身份配对服务器角色；只接受正式命令门开启后的端点。 */
		bool Prepare()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_ListenServer) ServerWorld = World;
				else if (World->GetNetMode() == NM_Client) ClientWorld = World;
			}
			ClientController = ClientWorld.IsValid() ? Cast<ACatfishingPlayerController>(ClientWorld->GetFirstPlayerController()) : nullptr;
			if (!ServerWorld.IsValid() || !ClientController.IsValid() || !ClientController->PlayerState) return false;
			for (TActorIterator<ACatCharacter> It(ServerWorld.Get()); It; ++It)
				if ((*It)->GetPlayerState() && (*It)->GetPlayerState()->GetPlayerId() == ClientController->PlayerState->GetPlayerId()) ServerCharacter = *It;
			ACatCharacter* ClientCharacter = Cast<ACatCharacter>(ClientController->GetPawn());
			ServerBackpack = ServerCharacter.IsValid() ? Cast<UCatBackPackComponent>(ServerCharacter->GetInventoryComponent()) : nullptr;
			ClientBackpack = ClientCharacter ? Cast<UCatBackPackComponent>(ClientCharacter->GetInventoryComponent()) : nullptr;
			const ACatfishingGameModeBase* Mode = ServerWorld->GetAuthGameMode<ACatfishingGameModeBase>();
			if (!ServerCharacter.IsValid() || !ServerBackpack.IsValid() || !ClientBackpack.IsValid() || !Mode || !Mode->CanAcceptGameplayCommand(ServerCharacter->GetController())) return false;
			if (!WaterRegion.IsValid())
			{
				FCatWaterGeometryBuildInput Geometry;
				Geometry.RegionId = TEXT("QuickbarRemoteUseWater");
				Geometry.WaterPointVerticalToleranceCm = 100.0;
				Geometry.BankHeightToleranceCm = 50.0;
				Geometry.BoundaryToleranceCm = 1.0;
				Geometry.MaxLandingCorrectionCm = 100.0;
				Geometry.MinimumWaterInsetCm = 1.0;
				Geometry.PlaneToWorld = FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 10.0));
				FCatWaterPolygonBuildInput& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
				Boundary.BoundaryId = TEXT("QuickbarRemoteUseShore");
				Boundary.Vertices = {{400, -1200}, {1800, -1200}, {1800, 1200}, {400, 1200}};
				const FCatWaterGeometryBuildResult Built = FCatWaterGeometry::Build(Geometry);
				if (!Test->TestTrue(TEXT("remote quickbar fixture builds formal water geometry"), Built.bSucceeded)) return true;
				ACatWaterRegion* DeferredRegion = ServerWorld->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity);
				if (!Test->TestNotNull(TEXT("remote quickbar fixture creates deferred water region"), DeferredRegion)) return true;
				FCatWaterRegionTestAccess::InjectBakedGeometry(*DeferredRegion, Built.Cache);
				DeferredRegion->FinishSpawning(FTransform::Identity);
				WaterRegion = DeferredRegion;
				if (UCatPhysicalBodyComponent* Body = ServerCharacter->GetPhysicalBodyComponent())
					Body->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, Body->GetStandRootHeightCm())), TEXT("QuickbarRemoteUseWaterFixture"));
				ServerCharacter->GetController()->SetControlRotation(FRotator::ZeroRotator);
				ClientController->SetControlRotation(FRotator::ZeroRotator);
			}
			Stage = 1; return false;
		}
		/** 用两种正式窝料和两根正式鱼竿填充服务器远端背包，并给两根竿写入不同耐久以检测实例串线。 */
		bool SeedServerInventory()
		{
			auto Load = [](const TCHAR* Path) { return LoadObject<UCatEquipmentDefinition>(nullptr, Path); };
			UCatEquipmentDefinition* A = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_Bug.Equip_Chum_Bug"));
			UCatEquipmentDefinition* B = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_FermentedGrain.Equip_Chum_FermentedGrain"));
			UCatEquipmentDefinition* RodA = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1.Equip_Rod_StarterT1"));
			UCatEquipmentDefinition* RodB = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_ShopT2.Equip_Rod_ShopT2"));
			const TArray<FCatInventoryEntry> Empty;
			if (!Test->TestTrue(TEXT("remote fixture loads two formal chum and two formal rods"), A && B && RodA && RodB)
				|| !Test->TestTrue(TEXT("remote server backpack resets to four slots"), ServerBackpack->ReplaceInventoryEntriesFromAuthority(Empty, 4))
				|| !ServerBackpack->AddItemDefinition(A, 2) || !ServerBackpack->AddItemDefinition(B, 2)
				|| !ServerBackpack->AddItemDefinition(RodA, 1) || !ServerBackpack->AddItemDefinition(RodB, 1)) return true;
			FirstChumSlot = ServerBackpack->FindFirstInventorySlotIndexByDefinitionId(TEXT("BugChum"));
			SecondChumSlot = ServerBackpack->FindFirstInventorySlotIndexByDefinitionId(TEXT("FermentedGrainChum"));
			FirstRodSlot = ServerBackpack->FindFirstInventorySlotIndexByDefinitionId(TEXT("StarterRodT1"));
			SecondRodSlot = ServerBackpack->FindFirstInventorySlotIndexByDefinitionId(TEXT("ShopRodT2"));
			if (!Test->TestTrue(TEXT("four formal fixture definitions occupy real server backpack slots"),
				FirstChumSlot != INDEX_NONE && SecondChumSlot != INDEX_NONE && FirstRodSlot != INDEX_NONE && SecondRodSlot != INDEX_NONE)) return true;
			const FCatInventoryEntry* FirstRod = ServerBackpack->GetInventoryEntryAtSlot(FirstRodSlot);
			const FCatInventoryEntry* SecondRod = ServerBackpack->GetInventoryEntryAtSlot(SecondRodSlot);
			UCatEquipmentInventoryItemInstance* FirstInstance = FirstRod ? Cast<UCatEquipmentInventoryItemInstance>(FirstRod->Instance) : nullptr;
			UCatEquipmentInventoryItemInstance* SecondInstance = SecondRod ? Cast<UCatEquipmentInventoryItemInstance>(SecondRod->Instance) : nullptr;
			if (!Test->TestTrue(TEXT("remote fixture resolves four distinct selected-use instances"), FirstRod && SecondRod && FirstInstance && SecondInstance)) return true;
			FirstChumCount = ServerBackpack->GetInventoryEntryAtSlot(FirstChumSlot)->StackCount;
			SecondChumCount = ServerBackpack->GetInventoryEntryAtSlot(SecondChumSlot)->StackCount;
			FirstRodId = FirstInstance->GetItemInstanceId(); SecondRodId = SecondInstance->GetItemInstanceId();
			FirstRodDurability = FirstInstance->GetRodDurability() * 0.75; SecondRodDurability = SecondInstance->GetRodDurability() * 0.5;
			FirstInstance->SetRodRuntimeStateFromAuthority(FirstRodDurability, false); SecondInstance->SetRodRuntimeStateFromAuthority(SecondRodDurability, false);
			ServerCharacter->ForceNetUpdate(); Stage = 2; return false;
		}
		/** 等待远端背包解析四个复制实例；选择与 G 只能从 owning client 的实际本地投影发起。 */
		bool WaitForRemoteInstances()
		{
			if (ClientBackpack->GetInventorySlotCount() != 4 || !ClientBackpack->HasItemAtSlot(SecondChumSlot) || !ClientBackpack->HasItemAtSlot(SecondRodSlot)) return false;
			const int32 SelectedBeforeModal = ClientController->GetSelectedQuickbarSlotIndex();
			ClientController->SetIgnoreMoveInput(true);
			const bool bModalBlocksWheel = !ClientController->RequestCycleQuickbarSlotFromInput(1);
			const bool bModalBlocksUse = !ClientController->CanUseSelectedBackpackItemFromInput();
			ClientController->SetIgnoreMoveInput(false);
			if (!Test->TestTrue(TEXT("modal input gate blocks quickbar wheel cycling and G use without clearing local selection"),
				bModalBlocksWheel && bModalBlocksUse && ClientController->GetSelectedQuickbarSlotIndex() == SelectedBeforeModal)) return true;
			if (bSimulateWeakNetwork && !bPacketSimulationApplied)
			{
				if (!ApplyWeakNetworkAfterFixtureSync()) return bPacketSimulationFailed;
			}
			Stage = 3; return false;
		}
		/** 在初始库存已复制后只改本次 PIE 的两端 Driver，并以读回值证明后续输入实际经过弱网模拟。 */
		bool ApplyWeakNetworkAfterFixtureSync()
		{
			UNetDriver* ServerDriver = ServerWorld.IsValid() ? ServerWorld->GetNetDriver() : nullptr;
			UNetDriver* ClientDriver = ClientWorld.IsValid() ? ClientWorld->GetNetDriver() : nullptr;
			if (!ServerDriver || !ClientDriver) return false;
			FPacketSimulationSettings WeakNetwork;
			WeakNetwork.PktLag = 100;
			WeakNetwork.PktLagVariance = 30;
			WeakNetwork.PktLoss = 5;
			ServerDriver->SetPacketSimulationSettings(WeakNetwork);
			ClientDriver->SetPacketSimulationSettings(WeakNetwork);
			const bool bReadBack = ServerDriver->PacketSimulationSettings.PktLag == WeakNetwork.PktLag
				&& ServerDriver->PacketSimulationSettings.PktLagVariance == WeakNetwork.PktLagVariance
				&& ServerDriver->PacketSimulationSettings.PktLoss == WeakNetwork.PktLoss
				&& ClientDriver->PacketSimulationSettings.PktLag == WeakNetwork.PktLag
				&& ClientDriver->PacketSimulationSettings.PktLagVariance == WeakNetwork.PktLagVariance
				&& ClientDriver->PacketSimulationSettings.PktLoss == WeakNetwork.PktLoss;
			if (!Test->TestTrue(TEXT("remote quickbar applies 100ms ±30ms and 5% loss to both PIE NetDrivers"), bReadBack))
			{
				bPacketSimulationFailed = true;
				return false;
			}
			Test->AddInfo(FString::Printf(TEXT("Event=quickbar_remote_weak_network_configured ServerDriver=%s ClientDriver=%s LagMs=%d LagVarianceMs=%d LossPercent=%d"),
				*GetNameSafe(ServerDriver), *GetNameSafe(ClientDriver), WeakNetwork.PktLag, WeakNetwork.PktLagVariance, WeakNetwork.PktLoss));
			bPacketSimulationApplied = true;
			return true;
		}
		/** 远端先选第二份窝料按左键，再立刻换到第一格；弱网下不能把客户端 RPC 尚未抵达服务器误判为 Begin 失败。 */
		bool StartRemoteSecondChumThenChangeSelection()
		{
			if (!VerifyFixtureChumLineOfSight()) return true;
			if (!Test->TestTrue(TEXT("remote selects second chum locally"), ClientController->RequestSelectQuickbarSlotFromInput(SecondChumSlot))) return true;
			if (!VerifyLocalChumPreview(false)) return true;
			FCatSelectedUseInputTestAccess::Press(ClientController.Get());
			if (!VerifyLocalChumPreview(true)) return true;
			const FCatInventoryEntry* SourceChum = ServerBackpack->GetInventoryEntryAtSlot(SecondChumSlot);
			ActiveChumSource = SourceChum ? SourceChum->Instance : nullptr;
			if (!Test->TestTrue(TEXT("remote left click retains the selected source chum identity before RPC arrives"), ActiveChumSource.IsValid())) return true;
			if (!Test->TestFalse(TEXT("continuous selected item locks switching until release"), ClientController->RequestSelectQuickbarSlotFromInput(FirstChumSlot))) return true;
			Stage = 4; return false;
		}
		/** 等待弱网 RPC 在服务器原实例上真正激活后才发送 Release，保证结束事件与 Begin 的精确实例配对。 */
		bool WaitForRemoteSecondChumThenRelease()
		{
			if (!ActiveChumSource.IsValid())
			{
				Test->AddError(TEXT("remote chum source identity was lost before the server ability could activate."));
				return true;
			}
			if (!IsChumUseWaiting(ServerCharacter.Get(), ActiveChumSource.Get())) return false;
			const UCatChumEquipmentItemInstance* ChumSource = Cast<UCatChumEquipmentItemInstance>(ActiveChumSource.Get());
			FCatInventoryItemUseContext ActiveUseContext;
			if (!ChumSource || !ChumSource->TryGetActiveUseContext(ActiveUseContext))
			{
				Test->AddError(TEXT("remote chum ability was waiting without an active source use context."));
				return true;
			}
			ActiveChumRequestId = ActiveUseContext.RequestId;
			if (!Test->TestTrue(TEXT("remote source chum keeps a valid Begin request for Release receipt"), ActiveChumRequestId.IsValid())) return true;
			if (!Test->TestTrue(TEXT("remote slot change keeps the original source ability active"), IsChumUseWaiting(ServerCharacter.Get(), ActiveChumSource.Get()))) return true;
			if (!VerifyLocalChumPreview(true)) return true;
			FCatSelectedUseInputTestAccess::Release(ClientController.Get());
			if (!VerifyLocalChumPreview(false)) return true;
			Stage = 5; return false;
		}
		/** 在真实输入/RPC 链路上核对本地实例及线批次；关闭总调试开关仍须绘制，松开当帧不得再提交曲线。 */
		bool VerifyLocalChumPreview(const bool bExpectedActive)
		{
			const FCatInventoryEntry* Entry = ClientBackpack->GetInventoryEntryAtSlot(SecondChumSlot);
			const UCatChumEquipmentItemInstance* Chum = Entry ? Cast<UCatChumEquipmentItemInstance>(Entry->Instance) : nullptr;
			UCatFishingDebugSubsystem* Preview = ClientWorld->GetSubsystem<UCatFishingDebugSubsystem>();
			ULineBatchComponent* Lines = ClientWorld->GetLineBatcher(UWorld::ELineBatcherType::World);
			IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("cat.Fishing.Debug"));
			IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("cat.Fishing.ChumPreview"));
			if (!Test->TestTrue(TEXT("remote preview has local source, subsystem, line batch and switches"), Chum && Preview && Lines && Debug && Enabled)) return false;
			float HeldSeconds = 0.0f;
			bool bPassed = Test->TestEqual(TEXT("local chum preview follows actual left click lifecycle before server receipt"),
				Chum->TryGetLocalChargePreview(ClientController.Get(), HeldSeconds), bExpectedActive);
			const FCatInventoryEntry* OtherEntry = ClientBackpack->GetInventoryEntryAtSlot(FirstChumSlot);
			const UCatChumEquipmentItemInstance* OtherChum = OtherEntry ? Cast<UCatChumEquipmentItemInstance>(OtherEntry->Instance) : nullptr;
			float OtherHeld = 0.0f;
			bPassed &= Test->TestTrue(TEXT("unselected chum never owns the preview"), OtherChum && !OtherChum->TryGetLocalChargePreview(ClientController.Get(), OtherHeld));
			const int32 PreviousDebug = Debug->GetInt(), PreviousPreview = Enabled->GetInt();
			Debug->SetWithCurrentPriority(0); Enabled->SetWithCurrentPriority(1);
			const int32 LinesBefore = Lines->BatchedLines.Num();
			Preview->Tick(0.0f);
			const int32 AddedLines = Lines->BatchedLines.Num() - LinesBefore;
			if (bExpectedActive)
			{
				TArray<FVector> Path; FVector Landing; FCatWaterRegionHandle Region; bool bHitWater = false;
				UCatFishingAimLibrary::PredictChumThrow(ClientWorld.Get(), ClientController->GetPawn()->GetActorLocation(),
					ClientController->GetControlRotation(), UCatFishingAimLibrary::ChargeAlphaFromHeldSeconds(HeldSeconds), Path, Landing, Region, bHitWater);
				bPassed &= Test->TestTrue(TEXT("preview submits arc and landing marker with main fishing debug disabled"), Path.Num() > 1 && AddedLines > Path.Num() - 1);
				for (int32 Index = 1; Index < Path.Num() && Index <= AddedLines; ++Index)
				{
					const FBatchedLine& Line = Lines->BatchedLines[LinesBefore + Index - 1];
					bPassed &= Test->TestTrue(TEXT("drawn arc uses shared throw prediction points"), Line.Start.Equals(Path[Index - 1]) && Line.End.Equals(Path[Index]));
				}
			}
			else bPassed &= Test->TestEqual(TEXT("inactive preview submits no lines"), AddedLines, 0);
			Enabled->SetWithCurrentPriority(0);
			const int32 DisabledBefore = Lines->BatchedLines.Num();
			Preview->Tick(0.0f);
			bPassed &= Test->TestEqual(TEXT("preview toggle suppresses line submission"), Lines->BatchedLines.Num(), DisabledBefore);
			Debug->SetWithCurrentPriority(PreviousDebug); Enabled->SetWithCurrentPriority(PreviousPreview);
			return bPassed;
		}
		/** 使用与正式 PlaceChum 相同的抛物线、水域吸附和 Visibility 射线预检测试场景；命中时记录精确遮挡物，避免把固定夹具问题误报为 RPC 超时。 */
		bool VerifyFixtureChumLineOfSight()
		{
			UCatFishingCommandComponent* Commands = ClientController.IsValid() ? ClientController->GetFishingCommandComponent() : nullptr;
			UCatWaterQuerySubsystem* WaterQuery = ServerWorld.IsValid() ? ServerWorld->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
			const UCatChumFieldSettings* Settings = GetDefault<UCatChumFieldSettings>();
			if (!Commands || !WaterQuery || !Settings || !WaterRegion.IsValid()) return false;
			TArray<FVector> Path;
			FVector Landing = FVector::ZeroVector;
			FCatWaterRegionHandle Region;
			bool bHitWater = false;
			const bool bPredicted = UCatFishingAimLibrary::PredictChumThrow(ServerWorld.Get(), ServerCharacter->GetActorLocation(),
				ClientController->GetControlRotation(), 0.0f, Path, Landing, Region, bHitWater);
			const FCatWaterSpatialResult Water = bPredicted && bHitWater && Region.IsValid()
				? WaterQuery->ResolveCandidatePointToWater(Landing, Region) : FCatWaterSpatialResult{};
			if (!Test->TestTrue(TEXT("remote quickbar fixture predicts a water landing before left click"), Water.bSucceeded)) return false;
			FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CatQuickbarRemoteChumLineOfSight), true);
			TraceParams.AddIgnoredActor(ServerCharacter.Get());
			FHitResult Hit;
			const bool bOccluded = ServerWorld->LineTraceSingleByChannel(Hit, ServerCharacter->GetPawnViewLocation(),
				Water.WaterSurfaceWorldPoint, Settings->PlacementLineOfSightChannel, TraceParams);
			if (bOccluded)
			{
				UE_LOG(LogTemp, Warning, TEXT("Event=quickbar_remote_chum_fixture_los Occluded=1 HitActor=%s HitComponent=%s HitPoint=%s Target=%s"),
					*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToString(), *Water.WaterSurfaceWorldPoint.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Event=quickbar_remote_chum_fixture_los Occluded=0 HitActor=None HitComponent=None HitPoint=%s Target=%s"),
					*Hit.ImpactPoint.ToString(), *Water.WaterSurfaceWorldPoint.ToString());
			}
			return Test->TestFalse(TEXT("remote quickbar fixture has an unobstructed formal chum placement trace"), bOccluded);
		}
		/** 等待可靠弱网 RPC 收口后核对第一份窝料不变、第二份才扣量，证明释放始终绑定 Begin 实例。 */
		bool VerifyRemoteChumConsumption()
		{
			const FCatInventoryEntry* First = ServerBackpack->GetInventoryEntryAtSlot(FirstChumSlot);
			const FCatInventoryEntry* Second = ServerBackpack->GetInventoryEntryAtSlot(SecondChumSlot);
			if (!First || !Second || First->StackCount != FirstChumCount || Second->StackCount == SecondChumCount) return false;
			ACatfishingPlayerController* ServerController = ServerCharacter.IsValid() ? Cast<ACatfishingPlayerController>(ServerCharacter->GetController()) : nullptr;
			UCatFishingCommandComponent* ServerCommands = ServerController ? ServerController->GetFishingCommandComponent() : nullptr;
			UCatFishingCommandComponent* ClientCommands = ClientController.IsValid() ? ClientController->GetFishingCommandComponent() : nullptr;
			if (!ServerCommands || !ClientCommands || !ActiveChumRequestId.IsValid())
			{
				Test->AddError(TEXT("remote chum receipt fixture lost an endpoint or the Begin request identity."));
				return true;
			}
			FCatPlaceChumResult ServerPlaceResult;
			FCatPlaceChumResult ClientPlaceResult;
			if (!ServerCommands->TryGetPlaceChumResult(ActiveChumRequestId, ServerPlaceResult)
				|| !ClientCommands->TryGetPlaceChumResult(ActiveChumRequestId, ClientPlaceResult)) return false;
			const bool bFirstChumUnchanged = Test->TestEqual(TEXT("remote selection change leaves first chum quantity unchanged"), First->StackCount, FirstChumCount);
			const bool bSecondChumConsumed = Test->TestTrue(TEXT("remote release consumes only the second chum selected at Begin"), Second->StackCount < SecondChumCount);
			// 数量只证明库存变化；同一 Request 的权威与 owning-client 回执都必须确认真实 PlaceChum 成功，不能把缓存错误伪装成消费成功。
			const bool bServerReceiptMatches = Test->TestTrue(TEXT("remote server caches the matching successful PlaceChum receipt"),
				ServerPlaceResult.RequestId == ActiveChumRequestId && ServerPlaceResult.bCommitted && ServerPlaceResult.Error == ECatChumFieldError::None);
			const bool bClientReceiptMatches = Test->TestTrue(TEXT("remote owning client receives the matching successful PlaceChum receipt"),
				ClientPlaceResult.RequestId == ActiveChumRequestId && ClientPlaceResult.bCommitted && ClientPlaceResult.Error == ECatChumFieldError::None);
			if (!bFirstChumUnchanged || !bSecondChumConsumed || !bServerReceiptMatches || !bClientReceiptMatches) return true;
			Stage = 17; return false;
		}
		/** 正常投放后再走一次真实按住/取消，验证预览不会因原请求收尾而残留。 */
		bool BeginRemoteChumCancellation()
		{
			const FCatInventoryEntry* Entry = ClientBackpack->GetInventoryEntryAtSlot(SecondChumSlot);
			if (!Entry || Entry->StackCount != SecondChumCount - 1) return false;
			FCatSelectedUseInputTestAccess::Press(ClientController.Get());
			if (!VerifyLocalChumPreview(true)) return true;
			Stage = 18; return false;
		}
		bool CancelRemoteChum()
		{
			if (!IsChumUseWaiting(ServerCharacter.Get(), ActiveChumSource.Get())) return false;
			ClientController->ClearPhysicalControlInput(TEXT("ChumPreviewTestCancel"));
			FCatSelectedUseInputTestAccess::Release(ClientController.Get());
			if (!VerifyLocalChumPreview(false)) return true;
			Stage = 19; return false;
		}
		bool VerifyRemoteChumCancellation()
		{
			if (IsChumUseWaiting(ServerCharacter.Get(), ActiveChumSource.Get())) return false;
			const FCatInventoryEntry* Entry = ServerBackpack->GetInventoryEntryAtSlot(SecondChumSlot);
			if (!Test->TestTrue(TEXT("cancelling preview does not consume another chum"), Entry && Entry->StackCount == SecondChumCount - 1)) return true;
			if (!VerifyLocalChumPreview(false)) return true;
			Stage = bToolsOnly ? 13 : 6; return false;
		}
		/** 打窝完成后沿同一左键入口选择抄网；使用正式库存与真实可叼鱼验证网络终态。 */
		bool PrepareRemoteScoop()
		{
			FCatInventoryEntry Removed;
			if (!ServerBackpack->RemoveInventoryEntryAtSlotFromAuthority(FirstRodSlot, Removed)) return true;
			auto* Net=LoadObject<UCatEquipmentDefinition>(nullptr,TEXT("/Game/Catfishing/Data/Equipment/Equip_ScoopNet_Starter.Equip_ScoopNet_Starter"));
			if (!Test->TestTrue(TEXT("正式抄网填入原空格"), Net && ServerBackpack->AddItemDefinition(Net,1))) return true;
			ScoopSlot=ServerBackpack->FindFirstInventorySlotIndexByDefinitionId(TEXT("StarterScoopNet"));
			ScoopId=ServerBackpack->GetInventoryEntryAtSlot(ScoopSlot)->Instance->GetItemInstanceId();
			UCatFishDefinition* Definition=nullptr;
			for (const auto& Ref:GetDefault<UCatFishCatalogSettings>()->Definitions)
				if (auto* Fish=Ref.LoadSynchronous(); Fish && Fish->IsRuntimeDefinitionReady()) { Definition=Fish; break; }
			ScoopFish=ServerWorld->SpawnActor<ACatFishPickupActor>(ServerCharacter->GetActorLocation()+FVector(120,0,0),FRotator::ZeroRotator);
			FCatCaptureConditionSnapshot Condition; Condition.RegionId=TEXT("QuickbarRemoteUseWater");
			if (!Test->TestTrue(TEXT("真实目标鱼初始化"), ScoopFish.IsValid() && Definition && ScoopFish->InitializeFromAuthority(
				FGuid::NewGuid(),FGuid::NewGuid(),Definition,1,1,Condition,TEXT("ScoopNetwork"),{}))) return true;
			if (auto* Root=Cast<UPrimitiveComponent>(ScoopFish->GetRootComponent())) Root->SetSimulatePhysics(false);
			Stage=14; return false;
		}
		bool AimRemoteScoop()
		{
			if (FVector::Distance(ClientController->GetPawn()->GetActorLocation(),ServerCharacter->GetActorLocation())>5) return false;
			const auto* Entry=ClientBackpack->GetInventoryEntryAtSlot(ScoopSlot);
			if (!Entry || !Entry->Instance || Entry->Instance->GetItemInstanceId()!=ScoopId) return false;
			ACatFishPickupActor* ClientFish=nullptr;
			for (TActorIterator<ACatFishPickupActor> It(ClientWorld.Get());It;++It)
				if (It->GetPresentationState().FishInstanceId==ScoopFish->GetPresentationState().FishInstanceId) ClientFish=*It;
			if (!ClientFish) return false;
			ClientController->SetShowMouseCursor(false);
			ClientController->SetControlRotation((ClientFish->GetFishingCollisionCenter()-ClientController->GetPawn()->GetPawnViewLocation()).Rotation());
			if (!Test->TestTrue(TEXT("远端选中抄网"),ClientController->RequestSelectQuickbarSlotFromInput(ScoopSlot))) return true;
			ScoopAimAt=FPlatformTime::Seconds(); Stage=15; return false;
		}
		/** 等待第三人称相机跟随和瞄准收敛；不伪造 Use 的目标载荷。 */
		bool IsScoopAimReady()
		{
			const auto* Entry=ClientBackpack->GetInventoryEntryAtSlot(ScoopSlot);
			ACatFishPickupActor* ClientFish=nullptr;
			for (TActorIterator<ACatFishPickupActor> It(ClientWorld.Get());It;++It)
				if (It->GetPresentationState().FishInstanceId==ScoopFish->GetPresentationState().FishInstanceId) ClientFish=*It;
			if (!Entry || !Entry->Instance || !ClientFish) return false;
			if (Entry->Instance->CaptureUseTarget(ClientController.Get()).Actor==ClientFish) return true;
			FVector View; FRotator Rotation; ClientController->GetPlayerViewPoint(View,Rotation);
			ClientController->SetControlRotation((ClientFish->GetFishingCollisionCenter()-View).Rotation());
			ScoopAimAt=FPlatformTime::Seconds();
			return false;
		}
		bool VerifyRemoteScoop()
		{
			auto* ClientCat=Cast<ACatCharacter>(ClientController->GetPawn());
			auto* ClientFish=ClientCat ? ACatFishPickupActor::FindCarriedFish(ClientCat) : nullptr;
			const auto Result=ClientController->GetLastCampCommandResult();
			if (!ClientFish || ACatFishPickupActor::FindCarriedFish(ServerCharacter.Get())!=ScoopFish.Get() || Result.RequestId==ActiveChumRequestId) return false;
			FCatFishingCommandResult FishingResult;
			if (!ClientController->GetFishingCommandComponent()->TryGetResult(Result.RequestId,FishingResult)) return false;
			Test->TestTrue(TEXT("统一库存与钓鱼回执同时确认同一次抄鱼成功"),Result.bCommitted && !Result.bPending && FishingResult.bCommitted && FishingResult.CommandType==ECatFishingCommandType::RequestScoop);
			Test->TestEqual(TEXT("两端嘴叼同一鱼身份"),ClientFish->GetPresentationState().FishInstanceId,ScoopFish->GetPresentationState().FishInstanceId);
			Test->TestFalse(TEXT("成功抄鱼不受挥空硬直"),UCatGE_FishingScoopCooldown::IsOperationBlocked(ServerCharacter.Get()));
			Test->AddInfo(TEXT("Event=tools_left_click_network_verified Scope=ChumConsumption,ScoopTarget,InventoryReceipt,FishingReceipt,MouthReplication"));
			return true;
		}
		/** 远端选择第二根鱼竿并按左键；服务器部署时必须读取该槽的实例，第一根仍留在背包。 */
		bool DeployRemoteSecondRod()
		{
			if (!Test->TestTrue(TEXT("remote selects second rod locally"), ClientController->RequestSelectQuickbarSlotFromInput(SecondRodSlot))) return true;
			ClientController->BeginSelectedItemUseFromInput();
			Stage = 7; return false;
		}
		/** 查询正式服务器鱼竿 Actor 与 held/visible 背包条目，确认 world payload 使用第二根的 ID 和耐久而第一根未被拿走。 */
		bool VerifySecondRodPayload()
		{
			UCatFishingService* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			ACatFishingRodActor* Rod = Fishing ? Fishing->FindDeployedRod(ServerCharacter->GetPlayerState()) : nullptr;
			const FCatInventoryEntry* First = ServerBackpack->GetInventoryEntryAtSlot(FirstRodSlot);
			const FCatInventoryEntry* Held = ServerBackpack->FindHeldInventoryEntryFromAuthority(SecondRodId);
			UCatEquipmentInventoryItemInstance* FirstInstance = First ? Cast<UCatEquipmentInventoryItemInstance>(First->Instance) : nullptr;
			UCatEquipmentInventoryItemInstance* HeldInstance = Held ? Cast<UCatEquipmentInventoryItemInstance>(Held->Instance) : nullptr;
			if (!Rod || !FirstInstance || !HeldInstance) return false;
			// 三项都是同一次选中实例部署的独立证据；逐项执行可在回归时同时报告身份、库存和耐久是否串线。
			const bool bSecondRodPayloadMatches = Test->TestEqual(TEXT("remote slot selection deploys the selected second rod instance into world payload"), Rod->GetPresentationState().ItemInstanceId, SecondRodId);
			const bool bFirstRodStillVisible = Test->TestEqual(TEXT("first rod remains visible in remote server backpack"), FirstInstance->GetItemInstanceId(), FirstRodId);
			const bool bRodDurabilityRemainsDistinct = Test->TestTrue(TEXT("two rod runtime durability values stay distinct across selected deployment"),
				FMath::IsNearlyEqual(FirstInstance->GetRodDurability(), FirstRodDurability)
				&& !FMath::IsNearlyEqual(FirstInstance->GetRodDurability(), HeldInstance->GetRodDurability())
				&& FMath::IsNearlyEqual(HeldInstance->GetRodDurability(), SecondRodDurability));
			if (!bSecondRodPayloadMatches || !bFirstRodStillVisible || !bRodDurabilityRemainsDistinct) return true;
			RodActorId = Rod->GetPresentationState().RodActorId;
			Stage = 8; return false;
		}
		/** 从远端客户端找到同一复制鱼竿后通过正式交互 RPC 放下；target 就是该 Actor，不能改走最近竿或 R 路径。 */
		bool RequestRemoteTargetedRodLeave()
		{
			ACatFishingRodActor* ClientRod = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(ClientWorld.Get()); It; ++It)
				if (It->GetPresentationState().RodActorId == RodActorId) ClientRod = *It;
			if (!ClientRod) return false;
			// 部署后真实物理体必须能被正式交互通道命中；检查运行配置而非仅凭蓝图类实现接口推断可达性。
			if (!ClientRod->IsUsingPhysicalRod()) return false;
			const UBoxComponent* Body = ClientRod->GetPhysicalRodBody();
			const UCatInteractionSettings* InteractionSettings = GetDefault<UCatInteractionSettings>();
			FHitResult TargetHit;
			FCollisionQueryParams TargetParams(SCENE_QUERY_STAT(CatQuickbarRodTarget), InteractionSettings->bTraceComplex);
			TargetParams.AddIgnoredActor(ClientController->GetPawn());
			const FVector BodyCenter = Body->GetComponentLocation();
			const bool bHitRod = ClientWorld->LineTraceSingleByChannel(TargetHit, BodyCenter + FVector(0, 100, 0), BodyCenter,
				InteractionSettings->TargetingTraceChannel, TargetParams) && TargetHit.GetActor() == ClientRod;
			if (!Test->TestTrue(TEXT("formal deployed rod body is reachable through the real interaction trace channel"), bHitRod)) return true;
			LeaveRequestId = FGuid::NewGuid();
			ClientController->ServerRequestInteraction(ClientRod, LeaveRequestId);
			Stage = 9; return false;
		}
		/** 服务器状态先显示目标竿已由远端放下，再重放同一 RequestId；缓存命中不得因当前非主控而反向拾起。 */
		bool VerifyRemoteTargetedRodLeave()
		{
			UCatFishingService* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			ACatFishingRodActor* Rod = Fishing ? Fishing->FindDeployedRodById(RodActorId) : nullptr;
			if (!Rod || Rod->IsPrimaryOperator(ServerCharacter->GetPlayerState())) return false;
			if (!Test->TestEqual(TEXT("targeted remote E leave retains the deployed rod owner"), Rod->GetPresentationState().OwnerPlayerState.Get(), ServerCharacter->GetPlayerState())) return true;
			ACatFishingRodActor* ClientRod = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(ClientWorld.Get()); It; ++It)
				if (It->GetPresentationState().RodActorId == RodActorId) ClientRod = *It;
			if (!Test->TestNotNull(TEXT("remote replay finds the original replicated rod target"), ClientRod)) return true;
			ClientController->ServerRequestInteraction(ClientRod, LeaveRequestId);
			Stage = 10; return false;
		}
		/** 重放后仍应保持放下状态；通过后才为下一次新 RequestId 发起拾取。 */
		bool VerifyRemoteTargetedRodLeaveReplay()
		{
			UCatFishingService* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			ACatFishingRodActor* Rod = Fishing ? Fishing->FindDeployedRodById(RodActorId) : nullptr;
			if (!Rod || Rod->IsPrimaryOperator(ServerCharacter->GetPlayerState())) return false;
			if (bTransferRod)
			{
				auto* Host = Cast<ACatfishingPlayerController>(ServerWorld->GetFirstPlayerController());
				auto* HostCharacter = Host ? Cast<ACatCharacter>(Host->GetPawn()) : nullptr;
				auto* HostBackPack = HostCharacter ? Cast<UCatBackPackComponent>(HostCharacter->GetInventoryComponent()) : nullptr;
				if (!Test->TestTrue(TEXT("host prepares inventory for cross-player rod custody"), HostBackPack && HostBackPack->ReplaceInventoryEntriesFromAuthority({}, 4))) return true;
				HostCharacter->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator,
					Rod->GetGripWorldTransform().GetLocation() - FVector(80, 0, 0)), TEXT("CrossPlayerRodFixture"));
				if (!Test->TestTrue(TEXT("host E-acquires the parked remote player's exact rod"), Rod->Interact_Implementation(Host, FGuid::NewGuid()))) return true;
				if (!Test->TestTrue(TEXT("source item moves to host with no duplicate remote active record"), HostBackPack->FindHeldInventoryEntryFromAuthority(SecondRodId)
					&& !ServerBackpack->FindHeldInventoryEntryFromAuthority(SecondRodId))) return true;
				Host->ParkHeldRodFromInput();
				if (!Test->TestNull(TEXT("host R leaves the same rod for the remote player's E pickup"), Fishing->FindRodOperatedBy(Host->PlayerState))) return true;
			}
			Stage = 11; return false;
		}
		/** 对同一远端复制 Actor 发新的正式交互 RPC，验证拾回不会改操作到其它场景鱼竿。 */
		bool RequestRemoteTargetedRodOperate()
		{
			ACatFishingRodActor* ClientRod = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(ClientWorld.Get()); It; ++It)
				if (It->GetPresentationState().RodActorId == RodActorId) ClientRod = *It;
			if (!ClientRod) return false;
			ClientController->ServerRequestInteraction(ClientRod, FGuid::NewGuid());
			Stage = 12; return false;
		}
		/** 操作位恢复到远端玩家且实例 ID 仍为第二根，证明两次 E 都以 target RodActorId 为唯一目标。 */
		bool VerifyRemoteTargetedRodOperate()
		{
			UCatFishingService* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			ACatFishingRodActor* Rod = Fishing ? Fishing->FindDeployedRodById(RodActorId) : nullptr;
			if (!Rod || !Rod->IsPrimaryOperator(ServerCharacter->GetPlayerState())) return false;
			const auto& ClientHeld = ClientBackpack->GetQuickbarHeldSlot();
			if (ClientHeld.ItemInstanceId != SecondRodId || ClientController->GetSelectedQuickbarSlotIndex() != ClientHeld.SlotIndex) return false;
			if (!Test->TestEqual(TEXT("targeted remote E retakes the same deployed second rod instance"), Rod->GetPresentationState().ItemInstanceId, SecondRodId)) return true;
			if (!Test->TestEqual(TEXT("E restores the same quickbar reservation on both endpoints"), ServerBackpack->GetQuickbarHeldSlot().ItemInstanceId, SecondRodId)) return true;
			for (TActorIterator<ACatFishingRodActor> It(ClientWorld.Get()); It; ++It)
				if (It->GetPresentationState().RodActorId == RodActorId && It->IsPrimaryOperator(ClientController->PlayerState))
				{
					ClientController->PackHeldRodFromInput();
					Stage = 20; return false;
				}
			return false;
		}
		/** 等待 X 的归还和选中格重装备同时复制，不能只凭库存数量或服务器持竿判断成功。 */
		bool VerifyRemotePackedRodSelection()
		{
			auto* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			auto* Rod = Fishing ? Fishing->FindRodOperatedBy(ServerCharacter->GetPlayerState()) : nullptr;
			if (!Rod || Rod->GetPresentationState().RodActorId == RodActorId) return false;
			const auto& Held = ClientBackpack->GetQuickbarHeldSlot();
			if (Held.ItemInstanceId != SecondRodId || ClientController->GetSelectedQuickbarSlotIndex() != Held.SlotIndex) return false;
			for (TActorIterator<ACatFishingRodActor> It(ClientWorld.Get()); It; ++It)
				if (It->GetPresentationState().RodActorId == Rod->GetPresentationState().RodActorId && It->IsPrimaryOperator(ClientController->PlayerState))
				{
					const auto* Entry = ServerBackpack->FindHeldInventoryEntryFromAuthority(SecondRodId);
					const auto* Instance = Entry ? Cast<UCatEquipmentInventoryItemInstance>(Entry->Instance) : nullptr;
					Test->TestTrue(TEXT("remote X keeps original rod durability and exact active instance"), Instance && FMath::IsNearlyEqual(Instance->GetRodDurability(), SecondRodDurability));
					Test->TestEqual(TEXT("remote X immediately holds the selected original rod"), It->GetPresentationState().ItemInstanceId, SecondRodId);
					return true;
				}
			return false;
		}
		/** Automation 的断言写入对象。 */
		FAutomationTestBase* Test = nullptr;
		/** PIE 启动后的超时计时基准。 */
		double StartedAt = 0.0;
		/** 当前权威检查与远端输入阶段。 */
		int32 Stage = 0;
		/** listen server 的权威世界。 */
		TWeakObjectPtr<UWorld> ServerWorld;
		/** owning remote client 的本地世界。 */
		TWeakObjectPtr<UWorld> ClientWorld;
		/** 发起真实本地左键输入的远端控制器。 */
		TWeakObjectPtr<ACatfishingPlayerController> ClientController;
		/** 服务器上与远端玩家身份匹配的角色。 */
		TWeakObjectPtr<ACatCharacter> ServerCharacter;
		/** 服务器权威随身背包。 */
		TWeakObjectPtr<UCatBackPackComponent> ServerBackpack;
		/** 远端客户端复制得到的背包投影。 */
		TWeakObjectPtr<UCatBackPackComponent> ClientBackpack;
		/** 用 deferred spawn 注入后注册的真实水域；水面提高 10cm 避开 TestMap 地面，投放仍走正式水域和视线规则。 */
		TWeakObjectPtr<ACatWaterRegion> WaterRegion;
		/** 四种正式物品对应的槽位，用于精确身份断言。 */
		int32 FirstChumSlot = INDEX_NONE, SecondChumSlot = INDEX_NONE, FirstRodSlot = INDEX_NONE, SecondRodSlot = INDEX_NONE;
		/** 远端连续 Use 前两份窝料的权威数量。 */
		int32 FirstChumCount = 0, SecondChumCount = 0;
		/** 两根鱼竿的稳定实例身份。 */
		FGuid FirstRodId, SecondRodId;
		/** 远端选格部署出的场景鱼竿身份；后续 E RPC 只把此 ID 对应的复制 Actor 当 target。 */
		FGuid RodActorId;
		/** 首次放下动作的稳定请求 ID；同一 ID 的第二次 RPC 必须命中 Actor 缓存而不能翻转操作状态。 */
		FGuid LeaveRequestId;
		/** 两根鱼竿刻意不同的运行时耐久样本。 */
		double FirstRodDurability = 0.0, SecondRodDurability = 0.0;
		/** 远端 Begin 时服务器背包中第二份窝料的实例；等待与 Release 都必须绑定它，换格不能改写该身份。 */
		TWeakObjectPtr<UCatInventoryItemInstance> ActiveChumSource;
		/** 从原窝料实例的活动上下文冻结的 Begin 请求；服务器和客户端回执只能用它核对，不能按当前选中格重新生成。 */
		FGuid ActiveChumRequestId;
		/** 本次状态机是否应在初始复制完成后启用两端 PIE Driver 的弱网配置；正常网络入口保持 false。 */
		bool bSimulateWeakNetwork = false;
		bool bToolsOnly = false;
		/** 额外验证原实例先迁到房主，再由远端 E 接回时的复制和 X 重装备。 */
		bool bTransferRod = false;
		TWeakObjectPtr<ACatFishPickupActor> ScoopFish;
		FGuid ScoopId;
		int32 ScoopSlot=INDEX_NONE;
		double ScoopAimAt=0;
		/** 两端 Driver 是否已经接受并读回本次弱网配置；只影响当前 PIE 生命周期。 */
		bool bPacketSimulationApplied = false;
		/** Driver 存在但拒绝配置时终止测试，避免把正常网络回退伪装成弱网验证。 */
		bool bPacketSimulationFailed = false;
	};

	/** 复用相同 PIE 拓扑和状态机排入正常或弱网用例；网络参数只在状态机确认初始库存同步后写入 Driver。 */
	bool QueueRemoteSelectedUseScenario(FAutomationTestBase* Test, const bool bSimulateWeakNetwork, const bool bToolsOnly=false, const bool bTransferRod=false)
	{
		if (!Test->TestTrue(TEXT("remote quickbar selected-use test requires idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
		const TSharedRef<FRestoreSettings> Restore = MakeShared<FRestoreSettings>();
		ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
		for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
			if (Driver.DefName == TEXT("GameNetDriver")) { Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"); Driver.DriverClassNameFallback = Driver.DriverClassName; }
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap"))));
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FStartPIECommand(false)));
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FVerifyRemoteSelectedUse>(Test, bSimulateWeakNetwork, bToolsOnly, bTransferRod));
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FEndPlayMapCommand()));
		FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryQuickbarRemoteSelectedUseNormalNetworkTest,
	"Catfishing.Editor.Inventory.Quickbar.RemoteSelectedUseNormalNetworkExactInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 启动一名远端客户端的 IP listen-server PIE，在正常网络下复用真实本地快捷栏 左键/Release 与回执验证链。 */
bool FCatInventoryQuickbarRemoteSelectedUseNormalNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryQuickbarRemoteSelectedUseTest,
	"Catfishing.Editor.Inventory.Quickbar.RemoteSelectedUseWeakNetworkExactInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 启动一名远端客户端的 IP listen-server PIE；初始库存同步后显式启用双端弱网，再验证真实本地快捷栏 左键/Release 与回执。 */
bool FCatInventoryQuickbarRemoteSelectedUseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatQuickbarCrossPlayerRodNormalTest,
	"Catfishing.Editor.Inventory.Quickbar.CrossPlayerRodAcquireAndPack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatQuickbarCrossPlayerRodNormalTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatQuickbarCrossPlayerRodWeakTest,
	"Catfishing.Editor.Inventory.Quickbar.CrossPlayerRodAcquireAndPackWeakNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatQuickbarCrossPlayerRodWeakTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, true, false, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatToolsLeftClickNetworkTest,
	"Catfishing.Editor.Inventory.Tools.LeftClickChumThenScoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatToolsLeftClickNetworkTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this,false,true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatToolsLeftClickWeakNetworkTest,
	"Catfishing.Editor.Inventory.Tools.LeftClickChumThenScoopWeakNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatToolsLeftClickWeakNetworkTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this,true,true);
}
#endif
