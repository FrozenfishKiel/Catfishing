#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/StateTreeComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "StateTree.h"

namespace CatFishBehaviorNetwork
{
	class FRestore final : public IAutomationLatentCommand
	{
	public:
		FRestore()
		{
			const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(Mode);
			Settings->GetPlayNumberOfClients(Count);
			Settings->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
		}
		~FRestore() override { Restore(); }
		bool Update() override
		{
			if (GEditor && GEditor->PlayWorld) return false;
			Restore();
			return true;
		}
	private:
		void Restore()
		{
			if (bRestored) return;
			auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode);
			Settings->SetPlayNumberOfClients(Count);
			Settings->SetRunUnderOneProcess(OneProcess);
			if (GEngine) GEngine->NetDriverDefinitions = Drivers;
			// 临时内存设置恢复即可；测试不调用SaveConfig，不改用户的持久PIE配置。
			bRestored = true;
		}
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true;
		bool bRestored = false;
		TArray<FNetDriverDefinition> Drivers;
	};

	struct FSnapshot
	{
		ECatFishBehavior Behavior;
		ECatFishMotionIntent Motion;
		float Effort;
		float Speed;
		FVector Heading;
		FVector Position;
	};
	static const FSnapshot Samples[] = {
		{ECatFishBehavior::OutwardRush, ECatFishMotionIntent::StrugglingOutward, 0.9f, 162.0f, FVector::ForwardVector, FVector(500, 100, 200)},
		{ECatFishBehavior::LateralArc, ECatFishMotionIntent::StrugglingOutward, 0.65f, 117.0f, FVector::RightVector, FVector(520, 140, 200)},
		{ECatFishBehavior::EaseOff, ECatFishMotionIntent::CalmOrInward, 0.2f, 36.0f, -FVector::ForwardVector, FVector(480, 150, 200)},
		{ECatFishBehavior::None, ECatFishMotionIntent::AutoHauling, 0.0f, 0.0f, FVector::ZeroVector, FVector(460, 150, 200)}
	};

	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45.0)
			{
				Test->AddError(FString::Printf(TEXT("Fish behavior replication timeout Sample=%d Spawned=%d; no network verdict"), SampleIndex, ServerFish.IsValid()));
				return true;
			}
			UWorld* Server = nullptr;
			UWorld* Client = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
				if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
				if (Context.World()->GetNetMode() == NM_Client) Client = Context.World();
			}
			if (!Server || !Client || !Server->GetFirstPlayerController() || !Client->GetFirstPlayerController()) return false;
			if (!ServerFish.IsValid())
			{
				const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
				UCatFishDefinition* Definition = nullptr;
				for (const auto& Reference : Catalog->Definitions)
				{
					UCatFishDefinition* Candidate = Reference.LoadSynchronous();
					if (Candidate && Catalog->FindRuntimeDefinition(Candidate->FishDefinitionId) == Candidate)
					{
						Definition = Candidate;
						break;
					}
				}
				if (!Test->TestNotNull(TEXT("正式目录包含可用鱼定义"), Definition)) return true;
				UCatFishPresentationDefinition* Presentation = Definition->LoadRuntimePresentationDefinition();
				if (!Test->TestNotNull(TEXT("正式鱼定义具有完整表现引用"), Presentation)) return true;
				UClass* FishClass = GetDefault<UCatFishingPresentationSettings>()->FishEncounterActorClass.LoadSynchronous();
				if (!Test->TestTrue(TEXT("正式表现链提供鱼Encounter类"), FishClass && FishClass->IsChildOf(ACatFishEncounterActor::StaticClass()))) return true;
				const FTransform Transform(Samples[0].Position);
				ServerFish = Server->SpawnActorDeferred<ACatFishEncounterActor>(FishClass, Transform);
				if (!Test->TestNotNull(TEXT("服务器生成正式鱼Actor"), ServerFish.Get())) return true;
				if (!Test->TestTrue(TEXT("服务器通过原身份写口初始化"), ServerFish->InitializeAuthoritativeIdentity(
					SessionId, CastId, Definition->FishDefinitionId, 600.0, 1.0))) return true;
				ServerFish->bAlwaysRelevant = true;
				ServerFish->FinishSpawning(Transform);
				if (!Publish()) return true;
			}
			ACatFishEncounterActor* ClientFish = nullptr;
			for (TActorIterator<ACatFishEncounterActor> It(Client); It; ++It)
				if (It->GetPresentationState().FishingSessionId == SessionId) ClientFish = *It;
			if (!ClientFish || !Matches(*ClientFish)) return false;
			const auto& State = ClientFish->GetPresentationState();
			Test->TestTrue(TEXT("快照来自独立客户端世界的真实复制Actor"), ClientFish != ServerFish.Get()
				&& ClientFish->GetWorld() != ServerFish->GetWorld() && !ClientFish->HasAuthority());
			Test->TestEqual(TEXT("正式鱼身份随同复制"), State.FishDefinitionId, ServerFish->GetPresentationState().FishDefinitionId);
			Test->TestEqual(TEXT("抛竿关联身份随同复制"), State.CastAttemptId, CastId);
			UStateTreeComponent* ClientTree = ClientFish->FindComponentByClass<UStateTreeComponent>();
			if (!Test->TestNotNull(TEXT("客户端保留正式行为组件"), ClientTree)) return true;
			Test->TestFalse(TEXT("客户端组件没有启动平行决策"), ClientTree->IsRunning());
			if (SampleIndex == 0)
			{
				UStateTree* Tree = GetDefault<UCatFishingSettings>()->FishBehaviorStateTree.LoadSynchronous();
				if (!Test->TestNotNull(TEXT("权限检查使用真实配置树"), Tree)) return true;
				UCatFishingFightRunner* ClientRunner = NewObject<UCatFishingFightRunner>(ClientFish);
				Test->TestFalse(TEXT("客户端不能启动权威行为树"), ClientFish->StartFishBehaviorFromAuthority(Tree, ClientRunner));
				Test->TestFalse(TEXT("客户端不能提交权威行为"), ClientFish->BeginFishBehaviorFromStateTree(ECatFishBehavior::EaseOff));
				Test->TestFalse(TEXT("客户端不能推进权威行为固定步"), ClientFish->TickFishBehaviorFromAuthority(0.05f));
				Test->TestFalse(TEXT("客户端不能覆盖鱼出力与运动快照"), ClientFish->ApplyFightStepFromAuthority(
					ECatFishMotionIntent::CalmOrInward, 1.0, FVector::ZeroVector, 0.05f, 0.0f, 0.0f, 1.0f,
					false, false, FVector::UpVector, FVector::RightVector, ECatFishBehavior::EaseOff, 0.1f));
				Test->TestTrue(TEXT("拒绝后的客户端复制快照未被本地操作改写"), Matches(*ClientFish));
			}
			Test->AddInfo(FString::Printf(TEXT("Event=fish_behavior_network_snapshot SessionId=%s Sample=%d Behavior=%s Effort=%.3f Heading=%s SpeedCmPerSec=%.3f ServerNetMode=%d ClientNetMode=%d Evidence=runtime_behavior Replication=ActorChannel"),
				*SessionId.ToString(), SampleIndex, *UEnum::GetValueAsString(State.Behavior), State.FishEffortRatio,
				*State.SwimHeading.ToCompactString(), State.IntendedSwimSpeedCentimetersPerSecond, Server->GetNetMode(), Client->GetNetMode()));
			if (++SampleIndex == static_cast<int32>(UE_ARRAY_COUNT(Samples))) return true;
			return !Publish();
		}
	private:
		bool Publish()
		{
			const auto& Sample = Samples[SampleIndex];
			const float ActiveLoad = Sample.Motion == ECatFishMotionIntent::AutoHauling ? 0.0f : 0.5f;
			return Test->TestTrue(TEXT("服务器提交下一份行为快照"), ServerFish->ApplyFightStepFromAuthority(
				Sample.Motion, 600.0, Sample.Position, 0.05f, ActiveLoad, ActiveLoad, Sample.Speed,
				false, false, FVector::UpVector, Sample.Heading, Sample.Behavior, Sample.Effort));
		}
		bool Matches(const ACatFishEncounterActor& Fish) const
		{
			const auto& State = Fish.GetPresentationState();
			const auto& Sample = Samples[SampleIndex];
			return State.Behavior == Sample.Behavior && State.MotionIntent == Sample.Motion
				&& FMath::IsNearlyEqual(State.CurrentLineLength, 600.0, 0.0001)
				&& FMath::IsNearlyEqual(State.FishEffortRatio, Sample.Effort, 0.0001f)
				&& FMath::IsNearlyEqual(State.IntendedSwimSpeedCentimetersPerSecond, Sample.Speed, 0.0001f)
				&& State.SwimHeading.Equals(Sample.Heading, 0.0001)
				&& Fish.GetActorLocation().Equals(Sample.Position, 0.05);
		}
		FAutomationTestBase* Test;
		double Started;
		int32 SampleIndex = 0;
		FGuid SessionId = FGuid::NewGuid();
		FGuid CastId = FGuid::NewGuid();
		TWeakObjectPtr<ACatFishEncounterActor> ServerFish;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorNetworkTest,
	"Catfishing.Editor.Fishing.FishBehaviorListenClientSnapshots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("requires an idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatFishBehaviorNetwork::FRestore>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("isolated unsaved map"), Map)) return false;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
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
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishBehaviorNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
