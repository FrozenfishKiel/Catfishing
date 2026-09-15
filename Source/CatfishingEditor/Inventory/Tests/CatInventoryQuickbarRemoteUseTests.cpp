#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Components/BoxComponent.h"
#include "Interaction/CatInteractionSettings.h"

namespace CatInventoryQuickbarRemoteUseTests
{
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

	/** 远端 owning-client 快捷栏状态机：真实 G/Release 在弱网下只使用选中实例，并验证第二根鱼竿的部署载荷。 */
	class FVerifyRemoteSelectedUse final : public IAutomationLatentCommand
	{
	public:
		/** 保存 Automation 断言出口，状态机不拥有 PIE 对象。 */
		explicit FVerifyRemoteSelectedUse(FAutomationTestBase* InTest) : Test(InTest) {}
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
			case 4: return VerifyRemoteChumConsumption();
			case 5: return DeployRemoteSecondRod();
			case 6: return VerifySecondRodPayload();
			case 7: return RequestRemoteTargetedRodLeave();
			case 8: return VerifyRemoteTargetedRodLeave();
			case 9: return VerifyRemoteTargetedRodLeaveReplay();
			case 10: return RequestRemoteTargetedRodOperate();
			case 11: return VerifyRemoteTargetedRodOperate();
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
			Stage = 3; return false;
		}
		/** 远端先选第二份窝料按 G，再立刻选第一格并 Release；换格不得取消或把释放改投第一份窝料。 */
		bool StartRemoteSecondChumThenChangeSelection()
		{
			if (!VerifyFixtureChumLineOfSight()) return true;
			if (!Test->TestTrue(TEXT("remote selects second chum locally"), ClientController->RequestSelectQuickbarSlotFromInput(SecondChumSlot))) return true;
			ClientController->BeginSelectedItemUseFromInput();
			UCatFishingCommandComponent* ClientCommands = ClientController->GetFishingCommandComponent();
			if (!Test->TestTrue(TEXT("remote G begins the local chum preview"), ClientCommands && ClientCommands->GetLocalChumChargeStartTime() >= 0.0)) return true;
			if (!Test->TestTrue(TEXT("remote changes local selection without ending the active chum input"), ClientController->RequestSelectQuickbarSlotFromInput(FirstChumSlot))) return true;
			if (!Test->TestTrue(TEXT("remote slot change keeps the original local chum preview active"), ClientCommands->GetLocalChumChargeStartTime() >= 0.0)) return true;
			ClientController->EndSelectedItemUseFromInput(false);
			if (!Test->TestTrue(TEXT("remote release clears the original local chum preview"), ClientCommands->GetLocalChumChargeStartTime() < 0.0)) return true;
			Stage = 4; return false;
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
			if (!Test->TestTrue(TEXT("remote quickbar fixture predicts a water landing before G"), Water.bSucceeded)) return false;
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
			if (!Test->TestEqual(TEXT("remote selection change leaves first chum quantity unchanged"), First->StackCount, FirstChumCount)
				|| !Test->TestTrue(TEXT("remote release consumes only the second chum selected at Begin"), Second->StackCount < SecondChumCount)) return true;
			Stage = 5; return false;
		}
		/** 远端选择第二根鱼竿并按 G；服务器部署时必须读取该槽的实例，第一根仍留在背包。 */
		bool DeployRemoteSecondRod()
		{
			if (!Test->TestTrue(TEXT("remote selects second rod locally"), ClientController->RequestSelectQuickbarSlotFromInput(SecondRodSlot))) return true;
			ClientController->BeginSelectedItemUseFromInput();
			Stage = 6; return false;
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
			if (!Test->TestEqual(TEXT("remote G deploys the selected second rod instance into world payload"), Rod->GetPresentationState().ItemInstanceId, SecondRodId)
				&& Test->TestEqual(TEXT("first rod remains visible in remote server backpack"), FirstInstance->GetItemInstanceId(), FirstRodId)
				&& Test->TestTrue(TEXT("two rod runtime durability values stay distinct across selected deployment"),
					FMath::IsNearlyEqual(FirstInstance->GetRodDurability(), FirstRodDurability)
					&& !FMath::IsNearlyEqual(FirstInstance->GetRodDurability(), HeldInstance->GetRodDurability())
					&& FMath::IsNearlyEqual(HeldInstance->GetRodDurability(), SecondRodDurability))) return true;
			RodActorId = Rod->GetPresentationState().RodActorId;
			Stage = 7; return false;
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
			Stage = 8; return false;
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
			Stage = 9; return false;
		}
		/** 重放后仍应保持放下状态；通过后才为下一次新 RequestId 发起拾取。 */
		bool VerifyRemoteTargetedRodLeaveReplay()
		{
			UCatFishingService* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			ACatFishingRodActor* Rod = Fishing ? Fishing->FindDeployedRodById(RodActorId) : nullptr;
			if (!Rod || Rod->IsPrimaryOperator(ServerCharacter->GetPlayerState())) return false;
			Stage = 10; return false;
		}
		/** 对同一远端复制 Actor 发新的正式交互 RPC，验证拾回不会改操作到其它场景鱼竿。 */
		bool RequestRemoteTargetedRodOperate()
		{
			ACatFishingRodActor* ClientRod = nullptr;
			for (TActorIterator<ACatFishingRodActor> It(ClientWorld.Get()); It; ++It)
				if (It->GetPresentationState().RodActorId == RodActorId) ClientRod = *It;
			if (!ClientRod) return false;
			ClientController->ServerRequestInteraction(ClientRod, FGuid::NewGuid());
			Stage = 11; return false;
		}
		/** 操作位恢复到远端玩家且实例 ID 仍为第二根，证明两次 E 都以 target RodActorId 为唯一目标。 */
		bool VerifyRemoteTargetedRodOperate()
		{
			UCatFishingService* Fishing = ServerWorld->GetSubsystem<UCatFishingService>();
			ACatFishingRodActor* Rod = Fishing ? Fishing->FindDeployedRodById(RodActorId) : nullptr;
			if (!Rod || !Rod->IsPrimaryOperator(ServerCharacter->GetPlayerState())) return false;
			return Test->TestEqual(TEXT("targeted remote E retakes the same deployed second rod instance"), Rod->GetPresentationState().ItemInstanceId, SecondRodId);
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
		/** 发起真实本地 G 输入的远端控制器。 */
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
		/** 远端 G 部署出的场景鱼竿身份；后续 E RPC 只把此 ID 对应的复制 Actor 当 target。 */
		FGuid RodActorId;
		/** 首次放下动作的稳定请求 ID；同一 ID 的第二次 RPC 必须命中 Actor 缓存而不能翻转操作状态。 */
		FGuid LeaveRequestId;
		/** 两根鱼竿刻意不同的运行时耐久样本。 */
		double FirstRodDurability = 0.0, SecondRodDurability = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryQuickbarRemoteSelectedUseTest,
	"Catfishing.Editor.Inventory.Quickbar.RemoteSelectedUseWeakNetworkExactInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 启动一名远端客户端的 IP listen-server PIE，执行弱网下的真实本地快捷栏 G/Release 路径并恢复用户设置。 */
bool FCatInventoryQuickbarRemoteSelectedUseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("remote quickbar selected-use test requires idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const TSharedRef<CatInventoryQuickbarRemoteUseTests::FRestoreSettings> Restore = MakeShared<CatInventoryQuickbarRemoteUseTests::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions) if (Driver.DefName == TEXT("GameNetDriver")) { Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"); Driver.DriverClassNameFallback = Driver.DriverClassName; }
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatInventoryQuickbarRemoteUseTests::FVerifyRemoteSelectedUse>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
