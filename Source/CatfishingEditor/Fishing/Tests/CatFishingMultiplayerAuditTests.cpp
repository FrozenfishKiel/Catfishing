#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Framework/Game/CatfishingPlayerState.h"

// 专项审计用例保持独立过滤器；断言目标行为，失败表示待修复缺陷，不能作为交付绿灯。
// 装备测试仅证明 authority 运行行为；下面的 PIE 测试才包含真实 NetDriver 复制。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBorrowedRodReservationAudit,
	"Catfishing.Audit.FishingMultiplayer.BorrowedRodCanReserveFisherBait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatBorrowedRodReservationAudit::RunTest(const FString& Parameters)
{
	struct FRestoreSettings
	{
		UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> Definitions = Settings->Definitions;
		ECatDomainPolicy Trust = Settings->ProfileLoadoutTrustPolicy;
		int32 Capacity = Settings->InventorySlotCapacity;
		~FRestoreSettings()
		{
			Settings->Definitions = Definitions;
			Settings->ProfileLoadoutTrustPolicy = Trust;
			Settings->InventorySlotCapacity = Capacity;
		}
	} Restore;
	Restore.Settings->Definitions.Reset();
	Restore.Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Enabled;
	Restore.Settings->InventorySlotCapacity = 12;
	TArray<TStrongObjectPtr<UCatEquipmentDefinition>> Definitions;
	const auto AddDefinition = [&](const FName Id, const ECatEquipmentKind Kind)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>();
		Definitions.Emplace(Definition);
		Definition->EquipmentDefinitionId = Id;
		Definition->Kind = Kind;
		Definition->FunctionalRouteId = Id;
		Definition->LoadoutSlotId = Id;
		Definition->bEnableRuntimeDefinition = true;
		Restore.Settings->Definitions.Add(Definition);
		return Definition;
	};
	UCatEquipmentDefinition* RodDefinition = AddDefinition(TEXT("AuditRod"), ECatEquipmentKind::Rod);
	RodDefinition->MaximumRodDurability = 100.0;
	RodDefinition->MaximumLineLengthCentimeters = 1500.0;
	RodDefinition->HighTensionWearMultiplier = 1.0;
	RodDefinition->UseActorClass = ACatFishingRodActor::StaticClass();
	RodDefinition->UseInventoryEffect = ECatEquipmentUseInventoryEffect::HoldInstanceUntilUnUse;
	UCatEquipmentDefinition* BaitDefinition = AddDefinition(TEXT("AuditBait"), ECatEquipmentKind::Bait);
	BaitDefinition->bRunConsumable = true;
	BaitDefinition->BiteRateMultiplier = 1.0;
	BaitDefinition->MinimumBiteDelayMultiplier = 1.0;
	AddDefinition(TEXT("AuditFloat"), ECatEquipmentKind::Float)->MaximumCastDistanceCentimeters = 1000.0;
	for (const auto& Definition : Definitions)
	{
		if (!TestTrue(TEXT("audit definitions are valid"), Definition->IsRuntimeDefinitionReady())) return false;
	}
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("create authority world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	UWorld* World = Wrapper.GetTestWorld();
	const auto CreatePlayerEquipment = [&]() -> UCatEquipmentComponent*
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACatCharacter* Character = World->SpawnActor<ACatCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		ACatfishingPlayerState* Player = World->SpawnActor<ACatfishingPlayerState>();
		if (!Character || !Player) return nullptr;
		Character->SetPlayerState(Player);
		UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent();
		for (const FName Id : {FName(TEXT("AuditRod")), FName(TEXT("AuditFloat"))})
		{
			if (!Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted) return nullptr;
		}
		if (!Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision,
			TEXT("AuditBait"), 4).bCommitted) return nullptr;
		const auto Loadout = Equipment->GetSnapshot();
		return Equipment->Use(FGuid::NewGuid(), Loadout.Revision, Loadout.RodItemInstanceId).bCommitted ? Equipment : nullptr;
	};
	UCatEquipmentComponent* OwnerEquipment = CreatePlayerEquipment();
	UCatEquipmentComponent* FisherEquipment = CreatePlayerEquipment();
	if (!TestTrue(TEXT("two players deploy distinct rods of the same definition"), OwnerEquipment && FisherEquipment)) return false;
	const FGuid OwnerRodId = OwnerEquipment->GetSnapshot().RodItemInstanceId;
	const auto FisherLoadout = FisherEquipment->GetSnapshot();
	TestNotEqual(TEXT("rod instances belong to different players"), OwnerRodId, FisherLoadout.RodItemInstanceId);
	// 与 FishingService::BeginCast 的真实调用完全一致：使用操作者 Equipment，但传入场景鱼竿实例。
	const auto Borrowed = FisherEquipment->BeginFishingUse(FGuid::NewGuid(), OwnerRodId,
		FisherLoadout.BaitItemInstanceId, FisherLoadout.FloatItemInstanceId, FisherLoadout.RodDefinitionId,
		FisherLoadout.BaitDefinitionId, FisherLoadout.FloatDefinitionId, FisherLoadout.Revision);
	AddInfo(FString::Printf(TEXT("Event=multiplayer_borrowed_rod_probe Reserved=%s Error=%s"),
		Borrowed.bReserved ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Borrowed.Error)));
	// 对照组走同一生产入口，证明不是装备夹具缺配置导致一切抛竿均失败。
	const auto CurrentLoadout = FisherEquipment->GetSnapshot();
	const auto Own = FisherEquipment->BeginFishingUse(FGuid::NewGuid(), CurrentLoadout.RodItemInstanceId,
		CurrentLoadout.BaitItemInstanceId, CurrentLoadout.FloatItemInstanceId, CurrentLoadout.RodDefinitionId,
		CurrentLoadout.BaitDefinitionId, CurrentLoadout.FloatDefinitionId, CurrentLoadout.Revision);
	TestTrue(TEXT("control: own deployed rod can reserve bait"), Own.bReserved);
	TestTrue(TEXT("shared rod: another fisher can reserve their own bait for the owner's rod"), Borrowed.bReserved);
	return !HasAnyErrors();
}

namespace CatFishingMultiplayerAudit
{
	class FCompareReplicatedAnchors final : public IAutomationLatentCommand
	{
	public:
		explicit FCompareReplicatedAnchors(FAutomationTestBase* InTest, bool bInFormal = false)
			: Test(InTest), Started(FPlatformTime::Seconds()), bFormal(bInFormal) {}
		virtual bool Update() override
		{
			UWorld* Server = nullptr;
			UWorld* Client = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_ListenServer) Server = World;
				if (World->GetNetMode() == NM_Client) Client = World;
			}
			if (Server && Client && Client->GetFirstPlayerController())
			{
				if (!ServerRod.IsValid())
				{
					APlayerState* Owner = Server->GetFirstPlayerController()->PlayerState;
					if (!Owner) return TimedOut();
					const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(TEXT("StarterRodT1"));
					if (!Test->TestNotNull(TEXT("formal starter rod definition"), Definition)) return true;
					UClass* RodClass = bFormal ? Definition->UseActorClass.LoadSynchronous() : ACatFishingRodActor::StaticClass();
					if (!Test->TestNotNull(TEXT("rod actor class is available"), RodClass)) return true;
					const FTransform SpawnTransform(FVector(100, 200, 300));
					ACatFishingRodActor* Rod = Server->SpawnActorDeferred<ACatFishingRodActor>(RodClass, SpawnTransform,
						Server->GetFirstPlayerController(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					if (!Test->TestNotNull(TEXT("server spawns rod"), Rod)) return true;
					ServerRod = Rod;
					RodId = FGuid::NewGuid();
					// 原生和正式蓝图分别验证，区分玩法 getter 与视觉 RodTipMarker 的口径。
					Rod->ConfigureCanonicalAnchorsFromAuthority(Definition->RodTipLocalTransform,
						Definition->StandLocalTransform, Definition->GripLocalTransform);
					Rod->InitializeAuthoritativeIdentity(RodId, FGuid::NewGuid(), Definition->EquipmentDefinitionId,
						TEXT("AuditSkin"), Owner, nullptr, true, false);
					Rod->FinishSpawning(SpawnTransform);
					Rod->bAlwaysRelevant = true;
					Rod->ForceNetUpdate();
				}
				for (TActorIterator<ACatFishingRodActor> It(Client); It; ++It)
				{
					if (It->GetPresentationState().RodActorId != RodId) continue;
					const ACatFishingRodActor* AuthorityRod = ServerRod.Get();
					if (!It->GetActorTransform().Equals(AuthorityRod->GetActorTransform(), 0.01)) continue;
					const double TipError = FVector::Distance(It->GetRodTipWorldTransform().GetLocation(), AuthorityRod->GetRodTipWorldTransform().GetLocation());
					const double StandError = FVector::Distance(It->GetOperatorInteractionWorldTransform().GetLocation(), AuthorityRod->GetOperatorInteractionWorldTransform().GetLocation());
					const double GripError = FVector::Distance(It->GetGripWorldTransform().GetLocation(), AuthorityRod->GetGripWorldTransform().GetLocation());
					const auto FindVisualLineAnchor = [](const ACatFishingRodActor* Rod) -> const USceneComponent*
					{
						TInlineComponentArray<USceneComponent*> Components(Rod);
						for (const USceneComponent* Component : Components)
							if (Component->ComponentHasTag(TEXT("RodTipMarker"))) return Component;
						for (const USceneComponent* Component : Components)
							if (Component->GetFName() == TEXT("RodTipAnchor")) return Component;
						return Rod->GetRootComponent();
					};
					const USceneComponent* ServerVisual = FindVisualLineAnchor(AuthorityRod);
					const USceneComponent* ClientVisual = FindVisualLineAnchor(*It);
					const double VisualError = FVector::Distance(ServerVisual->GetComponentLocation(), ClientVisual->GetComponentLocation());
					Test->AddInfo(FString::Printf(TEXT("Event=multiplayer_anchor_probe Variant=%s ServerNetMode=%d ClientNetMode=%d RodActorId=%s TipErrorCm=%.3f StandErrorCm=%.3f GripErrorCm=%.3f VisualErrorCm=%.3f VisualAnchor=%s"),
						bFormal ? TEXT("FormalBlueprint") : TEXT("Native"), Server->GetNetMode(), Client->GetNetMode(),
						*RodId.ToString(), TipError, StandError, GripError, VisualError, *GetNameSafe(ClientVisual)));
					Test->TestTrue(TEXT("control: replicated grip agrees"), GripError < 0.1);
					Test->TestTrue(TEXT("client rod tip agrees with server"), TipError < 0.1);
					Test->TestTrue(TEXT("client interaction anchor agrees with server"), StandError < 0.1);
					return true;
				}
			}
			return TimedOut();
		}
	private:
		bool TimedOut()
		{
			if (FPlatformTime::Seconds() - Started < 35.0) return false;
			Test->AddError(TEXT("PIE listen/client connection or rod replication timed out; no network verdict"));
			return true;
		}
		FAutomationTestBase* Test;
		double Started;
		FGuid RodId;
		TWeakObjectPtr<ACatFishingRodActor> ServerRod;
		bool bFormal = false;
	};

	class FRestorePIESettings final : public IAutomationLatentCommand
	{
	public:
		FRestorePIESettings()
		{
			const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(Mode);
			Settings->GetPlayNumberOfClients(Count);
			Settings->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
		}
		virtual bool Update() override
		{
			if (GEditor->PlayWorld) return false;
			auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode);
			Settings->SetPlayNumberOfClients(Count);
			Settings->SetRunUnderOneProcess(OneProcess);
			// EndPIE 会保存 CDO（包括临时多人参数），因此恢复后也要回写原偏好。
			Settings->SaveConfig();
			GEngine->NetDriverDefinitions = Drivers;
			return true;
		}
	private:
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true;
		TArray<FNetDriverDefinition> Drivers;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRodNetworkAnchorsAudit,
	"Catfishing.Audit.FishingMultiplayer.ListenClientCanonicalAnchorsAgree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatRodNetworkAnchorsAudit::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires an idle editor"), GEditor && !GEditor->PlayWorld)) return false;
	TSharedPtr<CatFishingMultiplayerAudit::FRestorePIESettings> Restore = MakeShared<CatFishingMultiplayerAudit::FRestorePIESettings>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("create unsaved isolated test map"), Map)) return false;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	// 此用例只验证 Actor 的真实本机网络复制；Steam 账号准入及打包表现另做双机验收。
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishingMultiplayerAudit::FCompareReplicatedAnchors>(this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishingMultiplayerAudit::FCompareReplicatedAnchors>(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore.ToSharedRef());
	return true;
}

#endif
