#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Online/CatOnlineSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
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

#if WITH_EDITOR
	// 编辑器会为预载的 Inactive 原图初始化子系统；终态必须在 GC 前反初始化，而不是仅清强引用。
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	for (const bool bFrontend : {false, true})
	{
		for (const bool bSuccess : {false, true})
		{
			TStrongObjectPtr<UWorld> Inactive(UWorld::CreateWorld(EWorldType::Inactive, false, NAME_None, nullptr, false,
				ERHIFeatureLevel::Num, &Init));
			UWorldPartitionSubsystem* Partition = Inactive->GetSubsystem<UWorldPartitionSubsystem>();
			if (!TestNotNull(TEXT("Inactive preload has a real initialized WorldPartition subsystem"), Partition)) { return false; }
			TestTrue(TEXT("Inactive partition begins initialized"), Partition->IsInitialized());
			if (bFrontend) { Online->PreloadedFrontendWorld = Inactive.Get(); }
			else { Online->PreloadedGameplayWorld = Inactive.Get(); }
			Online->ActiveOperation = ECatOnlineOperation::Start;
			if (bSuccess) { Online->FinishOperationSuccess(); }
			else { Online->FinishOperationFailure(ECatOnlineError::TravelFailed); }
			TestFalse(TEXT("Success/failure cleans the unused original World before GC"), Inactive->IsInitialized());
			TestFalse(TEXT("Success/failure deinitializes its ticking subsystem before GC"), Partition->IsInitialized());
			TWeakObjectPtr<UWorld> WeakInactive(Inactive.Get());
			Inactive.Reset();
			CollectGarbage(RF_NoFlags, true);
			TestFalse(TEXT("Cleaned inactive World is collectible"), WeakInactive.IsValid());
		}
	}
	{
		TStrongObjectPtr<UWorld> Shared(UWorld::CreateWorld(EWorldType::Inactive, false, NAME_None, nullptr, false,
			ERHIFeatureLevel::Num, &Init));
		TStrongObjectPtr<UCatOnlineSubsystem> Other(NewObject<UCatOnlineSubsystem>(GameInstance.Get()));
		Online->PreloadedGameplayWorld = Shared.Get();
		Other->PreloadedFrontendWorld = Shared.Get();
		Online->ActiveOperation = ECatOnlineOperation::Start;
		Online->FinishOperationSuccess();
		TestTrue(TEXT("A second preload owner keeps the shared source initialized"), Shared->IsInitialized());
		Other->Deinitialize();
		TestFalse(TEXT("The final preload owner cleans the source on deinitialize"), Shared->IsInitialized());
	}
	for (const bool bFrontend : {false, true})
	{
		TStrongObjectPtr<UWorld> Late(UWorld::CreateWorld(EWorldType::Inactive, false, NAME_None, nullptr, false,
			ERHIFeatureLevel::Num, &Init));
		AddExpectedMessage(bFrontend ? TEXT("Event=online_callback_ignored Callback=FrontendPreload")
			: TEXT("Event=online_callback_ignored Callback=GameplayPreload"), EAutomationExpectedMessageFlags::Contains, 1);
		if (bFrontend)
		{
			Online->HandleFrontendPackagePreloadComplete(Late->GetOutermost()->GetFName(), Late->GetOutermost(),
				EAsyncLoadingResult::Succeeded, Online->OperationEpoch - 1);
		}
		else
		{
			Online->HandleGameplayPackagePreloadComplete(Late->GetOutermost()->GetFName(), Late->GetOutermost(),
				EAsyncLoadingResult::Succeeded, Online->OperationEpoch - 1);
		}
		TestFalse(TEXT("Ignored completion cleans its unused initialized source"), Late->IsInitialized());
		TestEqual(TEXT("Ignored completion never restarts the operation"), Online->ActiveOperation, ECatOnlineOperation::None);
	}
	for (const EWorldType::Type Type : {EWorldType::Editor, EWorldType::Game, EWorldType::PIE, EWorldType::Inactive})
	{
		TStrongObjectPtr<UWorld> Active(UWorld::CreateWorld(Type, false, NAME_None, nullptr, false,
			ERHIFeatureLevel::Num, &Init));
		// Even an inactive source can be held by an engine context; the preload layer must not clean it.
		GEngine->CreateNewWorldContext(Type).SetCurrentWorld(Active.Get());
		Online->PreloadedGameplayWorld = Active.Get();
		Online->ActiveOperation = ECatOnlineOperation::Start;
		Online->FinishOperationSuccess();
		TestTrue(TEXT("Engine-context World remains initialized when its preload reference is released"), Active->IsInitialized());
		GEngine->DestroyWorldContext(Active.Get());
		Active->DestroyWorld(false);
	}
	CollectGarbage(RF_NoFlags, true);
#endif

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
