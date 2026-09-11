#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Online/CatOnlineSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatOnlinePreloadLifetimeTest,
	"Catfishing.Online.Preload.WorldSurvivesTravelGCAndReleasesAtCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCatOnlinePreloadLifetimeTest::RunTest(const FString& Parameters)
{
	// 重现旧合同：包的强引用不能保活包里的 World，LoadMap 将命中空包而不重新从磁盘加载。
	const FString Prefix = TEXT("/Temp/CatOnlinePreload_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TStrongObjectPtr<UPackage> OldPackage(CreatePackage(*(Prefix + TEXT("_Old"))));
	TWeakObjectPtr<UWorld> OldWorld(NewObject<UWorld>(OldPackage.Get(), TEXT("OldWorld"), RF_Public));
	CollectGarbage(RF_NoFlags, true);
	TestFalse(TEXT("Package-only ownership loses the map World during GC"), OldWorld.IsValid());
	TestNull(TEXT("LoadMap cannot find a World in the retained old package"), UWorld::FindWorldInPackage(OldPackage.Get()));

	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UCatOnlineSubsystem> Online(NewObject<UCatOnlineSubsystem>(GameInstance.Get()));
	for (const bool bFrontend : {false, true})
	{
		for (const bool bSuccess : {false, true})
		{
			UPackage* Package = CreatePackage(*(Prefix + FString::Printf(TEXT("_%d_%d"), bFrontend, bSuccess)));
			UWorld* World = NewObject<UWorld>(Package, TEXT("PreloadedWorld"), RF_Public);
			TWeakObjectPtr<UWorld> WeakWorld(World);
			if (bFrontend)
			{
				Online->PreloadedFrontendWorld = World;
			}
			else
			{
				Online->PreloadedGameplayWorld = World;
			}
			Online->ActiveOperation = ECatOnlineOperation::Start;
			CollectGarbage(RF_NoFlags, true);
			TestTrue(TEXT("Subsystem retains the actual map World across travel GC"), WeakWorld.IsValid());
			if (WeakWorld.IsValid())
			{
				TestEqual(TEXT("LoadMap can still resolve the preloaded map"), UWorld::FindWorldInPackage(Package), WeakWorld.Get());
			}
			if (bSuccess)
			{
				Online->FinishOperationSuccess();
			}
			else
			{
				Online->FinishOperationFailure(ECatOnlineError::TravelFailed);
			}
			CollectGarbage(RF_NoFlags, true);
			TestFalse(TEXT("Success and failure release preloaded maps"), WeakWorld.IsValid());
		}
	}

	// 空包即使报告 Succeeded，也不能推进到 ServerTravel。
	Online->ActiveOperation = ECatOnlineOperation::Start;
	Online->OperationRole = ECatOnlineSessionRole::Host;
	Online->GameplayMapPackage = OldPackage->GetName();
	AddExpectedMessage(TEXT("Event=online_map_preload_world_missing"), EAutomationExpectedMessageFlags::Contains, 1);
	Online->HandleGameplayPackagePreloadComplete(OldPackage->GetFName(), OldPackage.Get(),
		EAsyncLoadingResult::Succeeded, Online->OperationEpoch);
	TestEqual(TEXT("A successful empty package is rejected before travel"), Online->LastError, ECatOnlineError::GameplayPreloadFailed);
	TestTrue(TEXT("No travel was queued for an empty package"), Online->ExpectedPackage.IsEmpty());
	return !HasAnyErrors();
}

#endif
