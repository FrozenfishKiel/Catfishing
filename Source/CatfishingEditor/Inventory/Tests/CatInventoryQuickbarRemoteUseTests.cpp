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
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
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
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Components/PrimitiveComponent.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/Items/CatItemAbilityComponent.h"
#include "AbilitySystem/Items/CatEquipmentItemAbilities.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Growth/CatGrowthComponent.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "AbilitySystem/Items/CatItemGameplayAbility.h"
#include "Inventory/CatInventoryAccessRules.h"

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
	/** 按 Spec 来源寻找真实能力实例，测试不再读取物品中已删除的执行状态。 */
	const UCatGA_FishingChum* FindChumAbility(ACatCharacter* Character, const UCatInventoryItemInstance* Item)
	{
		auto* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
		if (ASC) for (const auto& Spec : ASC->GetActivatableAbilities())
			if (Spec.SourceObject.Get() == Item) return Cast<UCatGA_FishingChum>(Spec.GetPrimaryInstance());
		return nullptr;
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
			auto Load = [](const TCHAR* Path) { return LoadObject<UCatEquipmentItemDefinition>(nullptr, Path); };
			UCatEquipmentItemDefinition* A = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_Bug.Equip_Chum_Bug"));
			UCatEquipmentItemDefinition* B = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_FermentedGrain.Equip_Chum_FermentedGrain"));
			UCatEquipmentItemDefinition* RodA = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1.Equip_Rod_StarterT1"));
			UCatEquipmentItemDefinition* RodB = Load(TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_ShopT2.Equip_Rod_ShopT2"));
			const TArray<FCatInventoryEntry> Empty;
			if (!Test->TestTrue(TEXT("remote fixture loads two formal chum and two formal rods"), A && B && RodA && RodB)
				|| !Test->TestTrue(TEXT("remote server backpack resets to four slots"), ServerBackpack->ReplaceInventoryEntriesFromAuthority(Empty, 4))
				|| !ServerBackpack->AddItemDefinition(A, 2) || !ServerBackpack->AddItemDefinition(B, 2)
				|| !ServerBackpack->AddItemDefinition(RodA, 1) || !ServerBackpack->AddItemDefinition(RodB, 1)) return true;
			FirstChumSlot = ServerBackpack->FindFirstInventorySlotIndexByItemId(5);
			SecondChumSlot = ServerBackpack->FindFirstInventorySlotIndexByItemId(10);
			FirstRodSlot = ServerBackpack->FindFirstInventorySlotIndexByItemId(37);
			SecondRodSlot = ServerBackpack->FindFirstInventorySlotIndexByItemId(34);
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
			if (!Test->TestTrue(TEXT("持续使用允许切格但保持原来源"), ClientController->RequestSelectQuickbarSlotFromInput(FirstChumSlot))) return true;
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
			const auto* Ability = FindChumAbility(ServerCharacter.Get(), ActiveChumSource.Get());
			if (!Ability) return false;
			ActiveChumRequestId = Ability->GetUseTarget().RequestId;
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
			const auto* Chum = Entry ? FindChumAbility(Cast<ACatCharacter>(ClientController->GetPawn()), Entry->Instance) : nullptr;
			UCatFishingDebugSubsystem* Preview = ClientWorld->GetSubsystem<UCatFishingDebugSubsystem>();
			ULineBatchComponent* Lines = ClientWorld->GetLineBatcher(UWorld::ELineBatcherType::World);
			IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("cat.Fishing.Debug"));
			IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("cat.Fishing.ChumPreview"));
			if (!Test->TestTrue(TEXT("remote preview has local source, subsystem, line batch and switches"), Chum && Preview && Lines && Debug && Enabled)) return false;
			float HeldSeconds = 0.0f;
			bool bPassed = Test->TestEqual(TEXT("local chum preview follows actual left click lifecycle before server receipt"),
				Chum->TryGetLocalChargePreview(ClientController.Get(), HeldSeconds), bExpectedActive);
			const FCatInventoryEntry* OtherEntry = ClientBackpack->GetInventoryEntryAtSlot(FirstChumSlot);
			const auto* OtherChum = OtherEntry ? FindChumAbility(Cast<ACatCharacter>(ClientController->GetPawn()), OtherEntry->Instance) : nullptr;
			float OtherHeld = 0.0f;
			bPassed &= Test->TestTrue(TEXT("unselected chum never owns the preview"), !OtherChum || !OtherChum->TryGetLocalChargePreview(ClientController.Get(), OtherHeld));
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
			ClientController->RequestSelectQuickbarSlotFromInput(SecondChumSlot);
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
			auto* Net=LoadObject<UCatEquipmentItemDefinition>(nullptr,TEXT("/Game/Catfishing/Data/Equipment/Equip_ScoopNet_Starter.Equip_ScoopNet_Starter"));
			if (!Test->TestTrue(TEXT("正式抄网填入原空格"), Net && ServerBackpack->AddItemDefinition(Net,1))) return true;
			ScoopSlot=ServerBackpack->FindFirstInventorySlotIndexByItemId(38);
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
			FCatItemAbilityTargetData Target;
			GetDefault<UCatGA_UseScoopNet>()->CaptureTarget(ClientController.Get(), Target);
			if (Target.Aim.Actor == ClientFish) return true;
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
			if (FocusClearStarted <= 0)
			{
				// LostFocus iterates all controllers in the listen-server world, including remote ones.
				const auto* Grab = ServerCharacter->GetPhysicalBodyComponent()->GetGrab();
				HeldGripBeforeFocus = Grab->IsGripping(true) ? Grab->GetGripState(true).GripId : Grab->GetGripState(false).GripId;
				for (auto It = ServerWorld->GetPlayerControllerIterator(); It; ++It)
					if (It->Get()) It->Get()->FlushPressedKeys();
				ACatFishingRodActor* ServerRod = ServerWorld->GetSubsystem<UCatFishingService>()->FindDeployedRodById(RodActorId);
				if (!Test->TestTrue(TEXT("host viewport flush cannot release a remote player's rod"), ServerRod
					&& ServerRod->IsPrimaryOperator(ServerCharacter->GetPlayerState()))) return true;
				ClientController->FlushPressedKeys();
				FocusClearStarted = FPlatformTime::Seconds();
				return false;
			}
			if (FPlatformTime::Seconds() - FocusClearStarted < 1.5) return false;
			ACatFishingRodActor* ServerRod = ServerWorld->GetSubsystem<UCatFishingService>()->FindDeployedRodById(RodActorId);
			for (ACatCharacter* Character : {ServerCharacter.Get(), Cast<ACatCharacter>(ClientController->GetPawn())})
			{
				const auto* Grab = Character->GetPhysicalBodyComponent()->GetGrab();
				bool bSameHold = false;
				for (bool bLeft : {true, false}) bSameHold |= Grab->IsGripping(bLeft)
					&& Grab->GetGripState(bLeft).GripId == HeldGripBeforeFocus && Grab->GetGripState(bLeft).bControlledHold;
				if (!Test->TestTrue(TEXT("owning-client flush preserves the same controlled grip on both endpoints"), bSameHold)) return true;
			}
			if (!Test->TestTrue(TEXT("owning-client flush keeps authority and replicated rod ownership"), ServerRod
				&& ServerRod->IsPrimaryOperator(ServerCharacter->GetPlayerState()) && ClientRod->IsPrimaryOperator(ClientController->PlayerState))) return true;
			Test->AddInfo(TEXT("Event=rod_focus_network_verified HostFlush=Isolated ClientFlush=HoldPreserved Result=SameGripAndPrimary"));
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
		/** 等待 X 归还原实例；选中格保持选择，不再自动部署第二个世界竿。 */
		bool VerifyRemotePackedRodSelection()
		{
			auto* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			if (Fishing && Fishing->FindRodOperatedBy(ServerCharacter->GetPlayerState())) return false;
			const int32 Slot = ClientBackpack->FindInventorySlotIndexFromInstanceId(SecondRodId);
			if (Slot == INDEX_NONE || ClientBackpack->GetQuickbarHeldSlot().ItemInstanceId.IsValid()) return false;
			const auto* Entry = ServerBackpack->GetInventoryEntryAtSlot(ServerBackpack->FindInventorySlotIndexFromInstanceId(SecondRodId));
			const auto* Instance = Entry ? Cast<UCatEquipmentInventoryItemInstance>(Entry->Instance) : nullptr;
			Test->TestTrue(TEXT("收竿归还原实例及耐久"), Instance && FMath::IsNearlyEqual(Instance->GetRodDurability(), SecondRodDurability));
			Test->TestEqual(TEXT("收竿后保留选中格但不拿出"), ClientController->GetSelectedQuickbarSlotIndex(), Slot);
			Test->TestNull(TEXT("原世界鱼竿已经收回"), Fishing ? Fishing->FindDeployedRodById(RodActorId) : nullptr);
			return true;
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
		FGuid HeldGripBeforeFocus;
		double FocusClearStarted = 0;
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


	/** 同一进食能力的来源与提交边界场景；每种使用真实服务器物品及远端客户端。 */
	enum class EFoodScenario { Backpack, Mouth, Public, PublicRemoved, Cancel, PublicRace, PendingReplication, PublicOutOfRange };
	/** 真实远端进食：覆盖背包、嘴叼、公共鱼护和争抢/取消边界，核对原实物与成长复制；不覆盖菜单点击。 */
	class FVerifyRemoteFishUse final : public IAutomationLatentCommand
	{
	public:
		/** 保存断言出口和网络条件；世界、角色和库存仅保存弱引用。 */
		FVerifyRemoteFishUse(FAutomationTestBase* InTest, bool bInWeak, EFoodScenario InScenario) : Scenario(InScenario), Test(InTest), bWeak(bInWeak) {}
		/** 先等待两端角色准入并创建真实来源；通常等来源复制后请求，复制等待场景则在来源到达前提交意图，争食场景还提交房主请求。
		 * 取消、移除和超距场景等待服务器能力活动后干预；请求三秒后核对实物与合计经验，三十五秒未到达检查点则报超时。 */
		virtual bool Update() override
		{
			if (StartedAt == 0.0) StartedAt = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StartedAt > 35.0)
			{
				Test->AddError(FString::Printf(TEXT("Remote fish use timed out Stage=%d"), Stage)); return true;
			}
			if (Stage == 0)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					UWorld* World = Context.World();
					if (!World || Context.WorldType != EWorldType::PIE) continue;
					if (World->GetNetMode() == NM_ListenServer) ServerWorld = World;
					if (World->GetNetMode() == NM_Client) ClientWorld = World;
				}
				auto* ClientPC = ClientWorld.IsValid() ? Cast<ACatfishingPlayerController>(ClientWorld->GetFirstPlayerController()) : nullptr;
				if (!ClientPC || !ClientPC->PlayerState || !ServerWorld.IsValid()) return false;
				ClientCat = Cast<ACatCharacter>(ClientPC->GetPawn());
				for (TActorIterator<ACatCharacter> It(ServerWorld.Get()); It; ++It)
					if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == ClientPC->PlayerState->GetPlayerId()) ServerCat = *It;
				const auto* Mode = ServerWorld->GetAuthGameMode<ACatfishingGameModeBase>();
				if (!ClientCat.IsValid() || !ServerCat.IsValid() || !Mode || !Mode->CanAcceptGameplayCommand(ServerCat->GetController())) return false;
				auto* Definition = LoadObject<UCatFishDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Fish/Fish_LittleSilver.Fish_LittleSilver"));
				if (!Test->TestNotNull(TEXT("正式可食用鱼定义"), Definition)) return true;
				const double Weight = 1.0;
				ExpectedGain = FMath::FloorToInt(Definition->ResolveEatingExperiencePoints(Weight));
				BeforeExperience = ServerCat->GetGrowthComponent()->GetSnapshot().TotalExperience;
				if (Scenario == EFoodScenario::PendingReplication)
				{
					// 在创建鱼之前启用实际网络延迟，让客户端先持有意图，再收到库存实例与来源 Spec。
					FPacketSimulationSettings Simulation; Simulation.PktLag = 300;
					ServerWorld->GetNetDriver()->SetPacketSimulationSettings(Simulation);
					ClientWorld->GetNetDriver()->SetPacketSimulationSettings(Simulation);
				}
				ServerInventory = ServerCat->GetInventoryComponent();
				if (Scenario == EFoodScenario::Public || Scenario == EFoodScenario::PublicRemoved || Scenario == EFoodScenario::PublicRace || Scenario == EFoodScenario::PublicOutOfRange)
				{
					Guard = ServerWorld->SpawnActor<ACatFishGuardActor>(ServerCat->GetActorLocation() + FVector(70,0,0), FRotator::ZeroRotator);
					if (!Test->TestTrue(TEXT("创建真实公共鱼护"), Guard.IsValid())) return true;
					if (auto* Body = Cast<UPrimitiveComponent>(Guard->GetRootComponent())) Body->SetSimulatePhysics(false);
					ServerInventory = Guard->GetFishInventoryComponent();
				}
				auto* Fish = NewObject<UCatFishInventoryItemInstance>(ServerInventory->GetOwner());
				Fish->SetItemDefinition(Definition); Fish->SetRuntimeOwnerActor(ServerInventory->GetOwner());
				if (!Test->TestTrue(TEXT("初始化真实鱼实例"), Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("RemoteFoodFixture"), Weight))) return true;
				FishId = Fish->GetItemInstanceId();
				if (Scenario == EFoodScenario::Mouth)
				{
					WorldFish = ServerWorld->SpawnActor<ACatFishPickupActor>(ServerCat->GetActorLocation(), FRotator::ZeroRotator);
					if (!Test->TestTrue(TEXT("创建并叼起同一实物鱼"), WorldFish.IsValid() && WorldFish->InitializeFromInventoryForCarryFromAuthority(Fish, 1)
						&& WorldFish->BeginMouthCarryFromAuthority(ServerCat.Get(), ServerCat->GetPlayerState()))) return true;
				}
				else if (!Test->TestTrue(TEXT("原来源加入唯一实物鱼"), ServerInventory->AddItemInstance(Fish, 1))) return true;
				if (Scenario == EFoodScenario::PublicRace)
				{
					Host = Cast<ACatCharacter>(ServerWorld->GetFirstPlayerController()->GetPawn());
					if (!Test->TestTrue(TEXT("争食房主角色存在"), Host.IsValid())) return true;
					Host->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, ServerCat->GetActorLocation() + FVector(0,70,0)), TEXT("SharedFoodRace"));
					HostExperience = Host->GetGrowthComponent()->GetSnapshot().TotalExperience;
				}
				Stage = 1; return false;
			}
			if (Stage == 1)
			{
				UCatInventoryComponent* Inventory = ClientCat->GetInventoryComponent();
				if (Guard.IsValid())
				{
					Inventory = nullptr;
					for (TActorIterator<ACatFishGuardActor> It(ClientWorld.Get()); It; ++It)
						if (It->GetFName() == Guard->GetFName()) Inventory = CatInventoryAccessRules::ResolveReachableFishContainer(*It, ClientCat.Get());
				}
				if (Scenario == EFoodScenario::Mouth)
				{
					const auto* Carried = ACatFishPickupActor::FindCarriedFish(ClientCat.Get());
					if (!Carried || Carried->GetPresentationState().FishInstanceId != FishId) return false;
				}
				else if (Scenario == EFoodScenario::PendingReplication)
				{
					if (!Test->TestTrue(TEXT("发起意图时客户端尚未收到来源"), Inventory && Inventory->FindInventorySlotIndexFromInstanceId(FishId) == INDEX_NONE)) return true;
				}
				else if (!Inventory || Inventory->FindInventorySlotIndexFromInstanceId(FishId) == INDEX_NONE) return false;
				ClientInventory = Inventory;
				if (bWeak)
				{
					FPacketSimulationSettings Simulation; Simulation.PktLag = 120; Simulation.PktLoss = 5;
					ServerWorld->GetNetDriver()->SetPacketSimulationSettings(Simulation);
					ClientWorld->GetNetDriver()->SetPacketSimulationSettings(Simulation);
					Test->TestEqual(TEXT("服务器实际启用丢包"), ServerWorld->GetNetDriver()->PacketSimulationSettings.PktLoss, 5);
					Test->TestEqual(TEXT("客户端实际启用延迟"), ClientWorld->GetNetDriver()->PacketSimulationSettings.PktLag, 120);
				}
				auto* Items = ClientCat->FindComponentByClass<UCatItemAbilityComponent>();
				RequestId = FGuid::NewGuid();
				const bool bAccepted = Items && (Scenario == EFoodScenario::Mouth
					? Items->RequestUseCarriedFish(ACatFishPickupActor::FindCarriedFish(ClientCat.Get()), RequestId)
					: Items->RequestUse(Inventory, FishId, RequestId));
				if (!Test->TestTrue(TEXT("远端接受原鱼使用意图"), bAccepted)) return true;
				if (Host.IsValid())
				{
					auto* HostItems = Host->FindComponentByClass<UCatItemAbilityComponent>();
					if (!Test->TestTrue(TEXT("房主也提交同一条公共鱼"), HostItems && HostItems->RequestUse(ServerInventory.Get(), FishId, FGuid::NewGuid()))) return true;
				}
				SubmittedAt = FPlatformTime::Seconds();
				Stage = 2; return false;
			}
			if (Stage == 2 && (Scenario == EFoodScenario::Cancel || Scenario == EFoodScenario::PublicRemoved || Scenario == EFoodScenario::PublicOutOfRange))
			{
				bool bServerActive = false;
				for (const auto& Spec : ServerCat->GetCatAbilitySystemComponent()->GetActivatableAbilities())
					if (const auto* Ability = Cast<UCatGA_ConsumeFish>(Spec.GetPrimaryInstance()); Ability && Ability->IsActive() && Ability->GetUseTarget().RequestId == RequestId) bServerActive = true;
				if (!bServerActive) return false;
				if (Scenario == EFoodScenario::PublicRemoved)
				{
					FCatInventoryEntry Removed;
					Test->TestTrue(TEXT("前摇期间原公共鱼被拿走"), ServerInventory->RemoveInventoryEntryAtSlotFromAuthority(ServerInventory->FindInventorySlotIndexFromInstanceId(FishId), Removed));
				}
				else if (Scenario == EFoodScenario::PublicOutOfRange)
				{
					ServerCat->GetPhysicalBodyComponent()->TeleportBodyFromAuthority(FTransform(FRotator::ZeroRotator, Guard->GetActorLocation() + FVector(3000,0,0)), TEXT("ItemUseOutOfRange"));
				}
				else
					for (const auto& Spec : ClientCat->GetCatAbilitySystemComponent()->GetActivatableAbilities())
						if (const auto* Ability = Cast<UCatGA_ConsumeFish>(Spec.GetPrimaryInstance()); Ability && Ability->IsActive() && Ability->GetUseTarget().RequestId == RequestId)
						{ ClientCat->GetCatAbilitySystemComponent()->CancelAbilityHandle(Spec.Handle); break; }
				Stage = 3;
			}
			// 从发出请求起至少等待三秒后取一次结果，可发现该窗口内重复收益；不保证任意网络条件下已完成复制，也不覆盖更晚的重复结算。
			if (FPlatformTime::Seconds() - SubmittedAt < 3.0) return false;
			const bool bRejected = Scenario == EFoodScenario::Cancel || Scenario == EFoodScenario::PublicRemoved || Scenario == EFoodScenario::PublicOutOfRange;
			const int32 Gained = ServerCat->GetGrowthComponent()->GetSnapshot().TotalExperience - BeforeExperience;
			const int32 HostGained = Host.IsValid() ? Host->GetGrowthComponent()->GetSnapshot().TotalExperience - HostExperience : 0;
			Test->TestEqual(TEXT("成功只奖励一条鱼，取消或失去来源不奖励"), Gained + HostGained, bRejected ? 0 : ExpectedGain);
			Test->TestEqual(TEXT("远端复制权威经验"), ClientCat->GetGrowthComponent()->GetSnapshot().TotalExperience, BeforeExperience + Gained);
			if (Scenario == EFoodScenario::Mouth)
			{
				Test->TestNull(TEXT("权威嘴部释放"), ACatFishPickupActor::FindCarriedFish(ServerCat.Get()));
				Test->TestNull(TEXT("远端嘴部释放"), ACatFishPickupActor::FindCarriedFish(ClientCat.Get()));
			}
			else
			{
				const bool bShouldRemain = Scenario == EFoodScenario::Cancel || Scenario == EFoodScenario::PublicOutOfRange;
				Test->TestEqual(TEXT("原来源的实物结果"), ServerInventory->FindInventorySlotIndexFromInstanceId(FishId) != INDEX_NONE, bShouldRemain);
				Test->TestEqual(TEXT("原来源的复制结果"), ClientInventory->FindInventorySlotIndexFromInstanceId(FishId) != INDEX_NONE, bShouldRemain);
			}
			if (Guard.IsValid()) Test->TestEqual(TEXT("公共鱼从未临时加入私人背包"), ServerCat->GetInventoryComponent()->FindInventorySlotIndexFromInstanceId(FishId), INDEX_NONE);
			return true;
		}
	private:
		/** 本轮来源及并发/取消条件，不修改正式鱼配置。 */
		EFoodScenario Scenario;
		/** 使用发出的平台时钟起点，单位秒；三秒结果观察窗口从这里计算。 */
		double SubmittedAt = 0.0;
		/** 远端这次使用的请求身份；取消或移除干预只匹配该次服务器激活。 */
		FGuid RequestId;
		/** 并发食用者的权威初始经验，最终只允许两位玩家合计增加一条鱼经验。 */
		int32 HostExperience = 0;
		/** 争食场景中的房主角色；只在该场景请求同一条公共鱼并读取其经验。 */
		TWeakObjectPtr<ACatCharacter> Host;
		/** 两端原来源库存，公共鱼不会借用个人背包。 */
		TWeakObjectPtr<UCatInventoryComponent> ServerInventory, ClientInventory;
		/** 本轮真实鱼护或嘴叼鱼，退出 PIE 后自然释放。 */
		TWeakObjectPtr<ACatFishGuardActor> Guard;
		/** 嘴叼场景的真实鱼 Actor；服务器创建携带关系，客户端经复制找到同一身份。 */
		TWeakObjectPtr<ACatFishPickupActor> WorldFish;

		/** 当前测试的断言接收者。 */
		FAutomationTestBase* Test;
		/** 本轮是否显式启用延迟与丢包。 */
		bool bWeak = false;
		/** 异步场景的当前阶段；每个输入只执行一次。 */
		int32 Stage = 0;
		/** 超时计时起点，单位为平台秒。 */
		double StartedAt = 0.0;
		/** 操作前权威累计经验和这条正式鱼的预期经验。 */
		int32 BeforeExperience = 0, ExpectedGain = 0;
		/** 两端需要匹配的稳定实物身份。 */
		FGuid FishId;
		/** 当前 PIE 的服务器与远端世界，退出后弱引用失效。 */
		TWeakObjectPtr<UWorld> ServerWorld, ClientWorld;
		/** 同一网络玩家在两端的角色。 */
		TWeakObjectPtr<ACatCharacter> ServerCat, ClientCat;
	};

	/** 复用相同 PIE 拓扑和状态机排入正常或弱网用例；网络参数只在状态机确认初始库存同步后写入 Driver。 */
	bool QueueRemoteSelectedUseScenario(FAutomationTestBase* Test, const bool bSimulateWeakNetwork, const bool bToolsOnly=false, const bool bTransferRod=false, const bool bFishUse=false, EFoodScenario FoodScenario=EFoodScenario::Backpack)
	{
		if (!Test->TestTrue(TEXT("remote quickbar selected-use test requires idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
		const TSharedRef<FRestoreSettings> Restore = MakeShared<FRestoreSettings>();
		ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
		for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
			if (Driver.DefName == TEXT("GameNetDriver")) { Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"); Driver.DriverClassNameFallback = Driver.DriverClassName; }
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap"))));
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FStartPIECommand(false)));
		if (bFishUse) FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FVerifyRemoteFishUse>(Test, bSimulateWeakNetwork, FoodScenario));
		else FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FVerifyRemoteSelectedUse>(Test, bSimulateWeakNetwork, bToolsOnly, bTransferRod));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRemoteFishUseTest,
	"Catfishing.Editor.Inventory.Food.RemoteExactSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 排入正常网络 PIE：通过正式资产、来源授予和 GAS TargetData 验证指定鱼消费与成长复制；收尾交给公共场景队列。
bool FCatRemoteFishUseTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRemoteFishUseWeakTest,
	"Catfishing.Editor.Inventory.Food.RemoteExactSourceWeakNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 排入弱网 PIE：初始库存同步后启用双端 120 毫秒延迟和 5% 丢包，再核对同一来源的消费与成长复制。
bool FCatRemoteFishUseWeakTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, true, false, false, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFoodRemoteMouthTest, "Catfishing.Editor.Inventory.Food.RemoteMouth", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 排入嘴叼场景，检查同一世界鱼消费、两端携带释放与经验。
bool FCatFoodRemoteMouthTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true, CatInventoryQuickbarRemoteUseTests::EFoodScenario::Mouth);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFoodRemotePublicTest, "Catfishing.Editor.Inventory.Food.RemotePublic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 排入公共鱼护场景，检查直接消费原容器鱼且不临时转入私人背包。
bool FCatFoodRemotePublicTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true, CatInventoryQuickbarRemoteUseTests::EFoodScenario::Public);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFoodRemotePublicLostBeforeCommitTest, "Catfishing.Editor.Inventory.Food.RemotePublicLostBeforeCommit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 排入公共来源消失场景，在服务器前摇中移除原鱼，检查不再授予经验。
bool FCatFoodRemotePublicLostBeforeCommitTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true, CatInventoryQuickbarRemoteUseTests::EFoodScenario::PublicRemoved);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFoodRemoteCancelBeforeCommitTest, "Catfishing.Editor.Inventory.Food.RemoteCancelBeforeCommit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 排入客户端取消场景，在服务器前摇中取消对应 Spec，检查原鱼保留且经验不变。
bool FCatFoodRemoteCancelBeforeCommitTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true, CatInventoryQuickbarRemoteUseTests::EFoodScenario::Cancel);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFoodTwoPlayersOnePublicFishTest, "Catfishing.Editor.Inventory.Food.TwoPlayersOnePublicFish", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 排入房主与远端争食场景，检查同一公共鱼消失且双方合计只获得一条鱼经验。
bool FCatFoodTwoPlayersOnePublicFishTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true, CatInventoryQuickbarRemoteUseTests::EFoodScenario::PublicRace);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFoodPendingReplicationTest, "Catfishing.Editor.Inventory.Food.PendingSourceReplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 在实例生成前启用网络延迟，来源到达后由生产组件激活一次；不在客户端补造 Spec。
bool FCatFoodPendingReplicationTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true, CatInventoryQuickbarRemoteUseTests::EFoodScenario::PendingReplication);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFoodPublicOutOfRangeTest, "Catfishing.Editor.Inventory.Food.PublicOutOfRangeBeforeCommit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 服务器接受前摇后移动角色离开公共鱼护，提交复核必须保留鱼且不发经验。
bool FCatFoodPublicOutOfRangeTest::RunTest(const FString& Parameters)
{
	return CatInventoryQuickbarRemoteUseTests::QueueRemoteSelectedUseScenario(this, false, false, false, true, CatInventoryQuickbarRemoteUseTests::EFoodScenario::PublicOutOfRange);
}

#endif
