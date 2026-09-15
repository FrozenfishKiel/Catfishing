#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Animation/AnimMontage.h"
#include "Condition/CatFishThrowEffectActor.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishThrowReceiverTest,
	"Catfishing.Unit.Condition.ThrownFishConsumesOneImpactAndExpiresRepelBarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishThrowReceiverTest::RunTest(const FString&)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld()) return false;
	auto* World = Wrapper.GetTestWorld();
	auto* Fish = World->SpawnActor<ACatFishPickupActor>();
	Fish->FishDefinition = NewObject<UCatFishDefinition>();
	Fish->PresentationState.FishInstanceId = FGuid::NewGuid();
	auto& Effect = Fish->FishDefinition->ThrowEffect;
	Effect.Kind = ECatFishThrowEffectKind::RepelAura;
	Fish->bThrowEffectArmed = true;
	FHitResult Hit;
	Hit.ImpactPoint = FVector(1000, 0, 0);
	auto* Ground = World->SpawnActor<AActor>();
	AddExpectedMessage(TEXT("Event=fish_throw_effect_unavailable"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Event=fish_throw_rejected"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	Fish->HandleThrownFishHit(Fish->WorldCollision, Ground, nullptr, FVector::ZeroVector, Hit);
	TestFalse(TEXT("缺配命中也消费本次投掷机会，不会反复告警"), Fish->bThrowEffectArmed);
	Effect.EffectRadiusCentimeters = 100.0; // 仅夹具，不是正式值。
	Effect.DurationSeconds = 0.1;
	Effect.ReactionMontage = NewObject<UAnimMontage>();
	Fish->bThrowEffectArmed = true;
	Fish->HandleThrownFishHit(Fish->WorldCollision, Ground, nullptr, FVector::ZeroVector, Hit);
	ACatFishThrowEffectActor* Receiver = nullptr;
	int32 Count = 0;
	for (TActorIterator<ACatFishThrowEffectActor> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed()) { Receiver = *It; ++Count; }
	}
	if (!TestEqual(TEXT("权威命中产生一份效果接收方"), Count, 1) || !Receiver) return false;
	TestEqual(TEXT("接收方使用逐鱼半径"), Receiver->Barrier->GetUnscaledSphereRadius(), 100.0f);
	TestEqual(TEXT("驱散实际阻挡 Pawn"), Receiver->Barrier->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	TestEqual(TEXT("驱散区域实际启用碰撞"), Receiver->Barrier->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	Fish->HandleThrownFishHit(Fish->WorldCollision, Ground, nullptr, FVector::ZeroVector, Hit);
	Count = 0;
	for (TActorIterator<ACatFishThrowEffectActor> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++Count;
	TestEqual(TEXT("反弹命中不再生成效果"), Count, 1);
	TWeakObjectPtr<ACatFishThrowEffectActor> WeakReceiver = Receiver;
	for (int32 Index = 0; Index < 20; ++Index) Wrapper.TickTestWorld(0.01f);
	TestTrue(TEXT("到期清除效果与碰撞"), !WeakReceiver.IsValid() || WeakReceiver->IsActorBeingDestroyed());
	return !HasAnyErrors();
}
#endif
