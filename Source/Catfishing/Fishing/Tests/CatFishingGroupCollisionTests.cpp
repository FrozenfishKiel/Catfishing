#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupUnloadedCollisionTest,
	"Catfishing.Unit.Fishing.Runtime.UnloadedGroupRespectsPeersWallsStepsAndExitCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupUnloadedCollisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// Each scenario has independent real collision geometry and two authoritative Character/CMC pairs.
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		const bool bNearbyPeers = Scenario == 0;
		const bool bSingleLaneWall = Scenario == 1;
		const bool bLowStep = Scenario == 2;
		FTestWorldWrapper Wrapper;
		if (!TestTrue(TEXT("创建组移动碰撞世界"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		const auto AddBox = [World](const FVector& Center, const FVector& Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor) return static_cast<UBoxComponent*>(nullptr);
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Box);
			Actor->AddInstanceComponent(Box);
			Box->InitBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Actor->SetActorLocation(Center);
			return Box;
		};
		if (bLowStep && !TestNotNull(TEXT("Walking使用真实可行走地面"),
			AddBox(FVector(500.0, 0.0, -10.0), FVector(2000.0, 1000.0, 10.0)))) return false;
		ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
		AActor* UnrelatedIgnoredActor = World->SpawnActor<AActor>();
		if (!Rod || !UnrelatedIgnoredActor) return false;
		TArray<ACatCharacter*> Cats;
		TArray<APlayerState*> Players;
		TArray<UCatCharacterMovementComponent*> Movements;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		double CapsuleRadius = 0.0, CapsuleHalfHeight = 0.0;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			ACatCharacter* Cat = World->SpawnActor<ACatCharacter>(FVector(0.0, 200.0 * Index, 1000.0), FRotator::ZeroRotator, Spawn);
			APlayerState* Player = World->SpawnActor<APlayerState>();
			if (!Cat || !Player) return false;
			Player->SetPlayerId(Index + 1);
			Cat->SetPlayerState(Player);
			CapsuleRadius = Cat->GetCapsuleComponent()->GetScaledCapsuleRadius();
			CapsuleHalfHeight = Cat->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector Position(bNearbyPeers ? Index * (2.0 * CapsuleRadius + 1.0) : 0.0,
				bNearbyPeers ? 0.0 : 200.0 * Index, bLowStep ? CapsuleHalfHeight + 2.0 : 1000.0);
			Cat->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
			UCatCharacterMovementComponent* Movement = Cast<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
			UCatAbilitySystemComponent* ASC = Cat->GetCatAbilitySystemComponent();
			if (!Movement || !ASC) return false;
			Movement->bRunPhysicsWithNoController = true;
			Movement->MaxFlySpeed = Movement->MaxWalkSpeed = 200.0f;
			Movement->SetMovementMode(bLowStep ? MOVE_Walking : MOVE_Flying);
			ASC->InitAbilityActorInfo(Cat, Cat);
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 60.0f);
			// This pre-existing ignore belongs to another caller and must survive fishing cleanup.
			Cat->GetCapsuleComponent()->IgnoreActorWhenMoving(UnrelatedIgnoredActor, true);
			Cats.Add(Cat);
			Players.Add(Player);
			Movements.Add(Movement);
			if (Index == 0)
			{
				if (!TestTrue(TEXT("真实主位初始化立即进入无载移动"), Rod->InitializeAuthoritativeIdentity(
					FGuid::NewGuid(), FGuid::NewGuid(), TEXT("GroupCollisionRod"), NAME_None, Player, Player, true, false))) return false;
			}
			else
			{
				int32 Slot = INDEX_NONE;
				if (!TestTrue(TEXT("真实辅助加入同一鱼竿"), Rod->AddOperatorFromAuthority(
					Player, Rod->GetPresentationState().RodActorRevision, Slot))) return false;
			}
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			TestTrue(TEXT("组内胶囊只在同组期间相互忽略"),
				Cats[Index]->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Cats[1 - Index]));
			TestTrue(TEXT("组绑定保留已有外部碰撞忽略"),
				Cats[Index]->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(UnrelatedIgnoredActor));
		}
		const double StepHeight = FMath::Min(20.0, 0.5 * static_cast<double>(Movements[0]->MaxStepHeight));
		if (bSingleLaneWall && !TestNotNull(TEXT("高墙只挡主位所在的一条行走通道"),
			AddBox(FVector(250.0, 0.0, 1000.0), FVector(20.0, 65.0, 250.0)))) return false;
		if (bLowStep && !TestNotNull(TEXT("低台阶高度小于CMC允许跨越高度"),
			AddBox(FVector(350.0, 100.0, StepHeight / 2.0), FVector(200.0, 400.0, StepHeight / 2.0)))) return false;
		const FVector InitialOffset = Cats[1]->GetActorLocation() - Cats[0]->GetActorLocation();
		const FVector InitialPosition = Cats[0]->GetActorLocation();
		FVector LateWallPosition = FVector::ZeroVector;
		for (int32 Frame = 0; Frame < 50; ++Frame)
		{
			World->TimeSeconds += 0.05;
			// Match production tick dependencies: CMC consumes the last published group velocity,
			// then the rod observes both collision-resolved bodies and publishes the next velocity.
			for (UCatCharacterMovementComponent* Movement : Movements)
				Movement->MoveAutonomous(static_cast<float>(World->GetTimeSeconds()), 0.05f, 0,
					FVector::ForwardVector * Movement->GetMaxAcceleration());
			Rod->Tick(0.05f);
			if (Frame == 39) LateWallPosition = Cats[0]->GetActorLocation();
			const FVector Offset = Cats[1]->GetActorLocation() - Cats[0]->GetActorLocation();
			TestTrue(TEXT("同向移动及单人受阻不会改变两人的水平间距"),
				FVector(Offset.X, Offset.Y, 0.0).Equals(FVector(InitialOffset.X, InitialOffset.Y, 0.0), 0.5));
		}
		if (bNearbyPeers)
		{
			TestTrue(TEXT("前后仅隔胶囊直径加1cm的成员可持续同向移动"),
				Cats[0]->GetActorLocation().X - InitialPosition.X > 300.0);
		}
		if (bSingleLaneWall)
		{
			TestTrue(TEXT("有墙成员停在墙外而非穿过墙体"), Cats[0]->GetActorLocation().X > 100.0
				&& Cats[0]->GetActorLocation().X <= 230.0 - CapsuleRadius + 0.5);
			TestTrue(TEXT("只有一人遇墙也会让整组持续停止"),
				Cats[0]->GetActorLocation().Equals(LateWallPosition, 0.2)
				&& Rod->GroupMotionState.UnloadedVelocity.Size2D() < 0.1);
			TestEqual(TEXT("未被墙遮挡的队友不能独自继续前进"),
				Cats[1]->GetActorLocation().X, Cats[0]->GetActorLocation().X, 0.5);
		}
		if (bLowStep)
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				TestTrue(TEXT("组成员通过真实CMC StepUp跨过台阶正面"), Cats[Index]->GetActorLocation().X > 250.0
					&& Cats[Index]->GetActorLocation().X < 550.0 - CapsuleRadius);
				TestTrue(TEXT("身体实际升到台阶表面"), Cats[Index]->GetActorLocation().Z >= CapsuleHalfHeight + StepHeight - 1.0);
				TestEqual(TEXT("跨台阶保留正常Walking模式"), Movements[Index]->MovementMode.GetValue(), MOVE_Walking);
			}
		}

		APlayerState* Promoted = nullptr;
		if (!TestTrue(TEXT("离队使用正式成员移除入口"), Rod->RemoveOperatorFromAuthority(
			Players[1], Rod->GetPresentationState().RodActorRevision, Promoted))) return false;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			TestFalse(TEXT("一人离组立即恢复双方胶囊之间的碰撞"),
				Cats[Index]->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Cats[1 - Index]));
			TestTrue(TEXT("离组不删除其他调用者原有的忽略项"),
				Cats[Index]->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(UnrelatedIgnoredActor));
		}
		if (!TestTrue(TEXT("最后成员离组清理鱼竿移动来源"), Rod->RemoveOperatorFromAuthority(
			Players[0], Rod->GetPresentationState().RodActorRevision, Promoted))) return false;
		if (bNearbyPeers)
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Cats[Index]->TeleportTo(FVector(Index * (2.0 * CapsuleRadius + 1.0), 0.0, 1000.0), FRotator::ZeroRotator, false, true);
				Movements[Index]->Velocity = FVector::ZeroVector;
				Movements[Index]->SetMovementMode(MOVE_Flying);
			}
			Movements[0]->MoveAutonomous(static_cast<float>(World->GetTimeSeconds() + 0.2), 0.2f, 0,
				FVector::ForwardVector * Movements[0]->GetMaxAcceleration());
			TestTrue(TEXT("离组后真实CMC会被站定的前队友挡住"), Cats[0]->GetActorLocation().X < 5.0
				&& Cats[1]->GetActorLocation().X - Cats[0]->GetActorLocation().X >= 2.0 * CapsuleRadius - 0.5);
		}
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
