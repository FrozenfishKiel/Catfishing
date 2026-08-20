#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"

#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Collection/CatImprintTypes.h"
#include "Collection/CatRunImprintService.h"
#include "Collection/Tests/CatImprintSettingsTestOverride.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Framework/Core/CatRunContracts.h"
#include "Framework/Game/CatfishingGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatFishTankActor.h"
#include "Items/CatItemsService.h"
#include "Items/CatItemsSettings.h"
#include "Net/OnlineEngineInterface.h"
#include "OnlineSubsystemTypes.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampHubActorRangeAndFailClosedTest,
	"Catfishing.Unit.Camp.HubActor.RestRescueRangeReplayAndBodyRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampHubActorTankTransferReplayTest,
	"Catfishing.Unit.Camp.HubActor.TankTransferReplaysBeforeGuardRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatCampfireNightPhaseAndPartialPresenceTest,
	"Catfishing.Unit.Camp.HubActor.CampfireLightsEveryNightWithoutRequiringFullPresence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatCampHubActorTest
{
	/** 临时改动默认营地设置；析构只恢复内存 CDO，不写 Config 文件。 */
	struct FScopedCampSettings
	{
		/** 保存运行 gate 原值，避免本测试影响同进程后续 Automation。 */
		bool bOldRuntime = false;

		/** 保存交互半径原值，避免跨测试泄漏营地范围。 */
		double OldRadius = 0.0;

		/** 保存篝火事件原值；本测试不需要成像副作用。 */
		FName OldCampfireEvent = NAME_None;

		/** 记录可写默认设置对象；它是引擎 CDO，原始指针只在当前测试作用域恢复内存值。 */
		UCatCampSettings* Settings = nullptr;

		// 设置流程：仅在内存里开启固定营地范围，让测试可以验证服务器位置 gate；不调用 SaveConfig。
		FScopedCampSettings()
		{
			Settings = GetMutableDefault<UCatCampSettings>();
			if (Settings)
			{
				bOldRuntime = Settings->bEnableCampRuntime;
				OldRadius = Settings->InteractionRadiusCentimeters;
				OldCampfireEvent = Settings->CampfireCoverEventId;
				Settings->bEnableCampRuntime = true;
				Settings->InteractionRadiusCentimeters = 200.0;
				Settings->CampfireCoverEventId = NAME_None;
			}
		}

		// 恢复流程：把所有被测设置恢复到进入测试前状态，防止后续模块测试读到临时营地配置。
		~FScopedCampSettings()
		{
			if (Settings)
			{
				Settings->bEnableCampRuntime = bOldRuntime;
				Settings->InteractionRadiusCentimeters = OldRadius;
				Settings->CampfireCoverEventId = OldCampfireEvent;
			}
		}
	};

	/** 临时开启 Condition 恢复数值；析构恢复 CDO，避免营地休息测试污染同进程身体状态测试。 */
	struct FScopedConditionRestSettings
	{
		/** Condition 设置 CDO；Camp Rest 通过 ConditionComponent 间接读取它。 */
		UCatConditionSettings* Settings = nullptr;

		/** 进入测试前的 Condition 总 gate。 */
		bool bOldRuntime = false;

		/** 进入测试前的 Poison 倒地阈值。 */
		double OldPoisonThreshold = 0.0;

		// 设置流程：只在内存默认对象上给出完整倒地阈值，让 CampHub 能走真实 Condition 写口；恢复路径没有自己的数值配置。
		FScopedConditionRestSettings()
		{
			Settings = GetMutableDefault<UCatConditionSettings>();
			if (Settings)
			{
				bOldRuntime = Settings->bEnableConditionRuntime;
				OldPoisonThreshold = Settings->PoisonDownedThreshold;
				Settings->bEnableConditionRuntime = true;
				Settings->PoisonDownedThreshold = 100.0;
			}
		}

		// 恢复流程：还原所有被测 Condition 字段，避免后续测试误以为项目默认已开启恢复链路。
		~FScopedConditionRestSettings()
		{
			if (Settings)
			{
				Settings->bEnableConditionRuntime = bOldRuntime;
				Settings->PoisonDownedThreshold = OldPoisonThreshold;
			}
		}
	};
	/** 临时开启入缸链路所需容量与鱼表；析构恢复 CDO，避免污染同进程后续目录和 Items 测试。 */
	struct FScopedTankTransferSettings
	{
		/** 保存个人鱼护容量原值；Character Possess 注册鱼护时会读取它。 */
		int32 OldPersonalGuardCapacity = 0;

		/** 保存共享鱼缸容量原值；FishTank BeginPlay 注册容器时会读取它。 */
		int32 OldSharedFishTankCapacity = 0;

		/** 保存鱼目录内容 Schema 原值；测试只临时替换默认鱼表，不写配置。 */
		int32 OldContentSchemaVersion = 0;

		/** 保存鱼目录 Revision 原值；恢复后不让后续测试读到本用例的测试修订。 */
		int64 OldDataRevision = 0;

		/** 保存鱼目录来源戳原值；FindRuntimeDefinition 的来源门禁需要测试期间给出显式来源。 */
		FCatDataCatalogSourceStamp OldSourceStamp;

		/** 保存默认鱼定义软引用列表；析构时完整还原项目配置中的目录内容。 */
		TArray<TSoftObjectPtr<UCatFishDefinition>> OldDefinitions;

		/** 运行时容量设置 CDO；测试只改内存值，不调用 SaveConfig。 */
		UCatItemsSettings* ItemsSettings = nullptr;

		/** 鱼表设置 CDO；Camp 入缸路径通过 GetDefault 查询，因此测试必须临时替换默认目录。 */
		UCatFishCatalogSettings* FishCatalog = nullptr;

		/** 测试鱼定义稳定 ID；种鱼和 Camp 展示资格检查必须引用同一个 ID。 */
		FName FishDefinitionId = TEXT("CampTankFish");

		/** 测试期间挂根的瞬态鱼定义；避免默认目录软引用在同一用例内悬空。 */
		TObjectPtr<UCatFishDefinition> RuntimeFishDefinition = nullptr;

		// 设置流程：给 Items 两类容器明确容量，并给默认鱼表放入一条完整且允许入缸展示的瞬态鱼。
		FScopedTankTransferSettings()
		{
			ItemsSettings = GetMutableDefault<UCatItemsSettings>();
			if (ItemsSettings)
			{
				OldPersonalGuardCapacity = ItemsSettings->PersonalGuardCapacity;
				OldSharedFishTankCapacity = ItemsSettings->SharedFishTankCapacity;
				ItemsSettings->PersonalGuardCapacity = 3;
				ItemsSettings->SharedFishTankCapacity = 3;
			}

			FishCatalog = GetMutableDefault<UCatFishCatalogSettings>();
			if (FishCatalog)
			{
				OldContentSchemaVersion = FishCatalog->ContentSchemaVersion;
				OldDataRevision = FishCatalog->DataRevision;
				OldSourceStamp = FishCatalog->SourceStamp;
				OldDefinitions = FishCatalog->Definitions;

				RuntimeFishDefinition = NewObject<UCatFishDefinition>(GetTransientPackage());
				RuntimeFishDefinition->AddToRoot();
				RuntimeFishDefinition->bEnableRuntimeDefinition = true;
				RuntimeFishDefinition->FishDefinitionId = FishDefinitionId;
				RuntimeFishDefinition->BodyClass = ECatFishBodyClass::Standard;
				RuntimeFishDefinition->SacrificeContribution = 3;
				RuntimeFishDefinition->RegionIds = {TEXT("LakeA")};
				RuntimeFishDefinition->ChumAffinities = {ECatChumAffinity::Fishy};
				RuntimeFishDefinition->MinimumWeightKilograms = 1.0;
				RuntimeFishDefinition->MaximumWeightKilograms = 2.0;
				RuntimeFishDefinition->MinimumFightParticipants = 1;
				RuntimeFishDefinition->FishStrength = 1.0;
				RuntimeFishDefinition->FishFightStamina = 1.0;
				RuntimeFishDefinition->FoodSafety = ECatFishFoodSafety::Safe;
				RuntimeFishDefinition->bTankDisplayEligible = true;

				FishCatalog->ContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;
				FishCatalog->DataRevision = 1;
				FishCatalog->SourceStamp.SourceKind = TEXT("AutomationFishSheet");
				FishCatalog->SourceStamp.SourceNodeToken = TEXT("CampTankTransfer");
				FishCatalog->SourceStamp.SourceRevision = 1;
				FishCatalog->SourceStamp.SourceSliceName = TEXT("入缸回归");
				FishCatalog->Definitions = {RuntimeFishDefinition.Get()};
			}
		}

		// 恢复流程：先还原默认设置，再解除测试鱼挂根；这样 CDO 不会持有测试目录，也不会留下容量污染。
		~FScopedTankTransferSettings()
		{
			if (FishCatalog)
			{
				FishCatalog->ContentSchemaVersion = OldContentSchemaVersion;
				FishCatalog->DataRevision = OldDataRevision;
				FishCatalog->SourceStamp = OldSourceStamp;
				FishCatalog->Definitions = OldDefinitions;
			}
			if (RuntimeFishDefinition)
			{
				RuntimeFishDefinition->RemoveFromRoot();
			}
			if (ItemsSettings)
			{
				ItemsSettings->PersonalGuardCapacity = OldPersonalGuardCapacity;
				ItemsSettings->SharedFishTankCapacity = OldSharedFishTankCapacity;
			}
		}
	};
	/**
	 * 测试身份装配流程：先校验 World/Controller，再以 Controller 为 Owner 生成 PlayerState、写入稳定 UniqueId，并挂回
	 * Controller；Camp 救援与篝火围坐者判定都只接受这类服务器身份，不从 Actor 名字推导目标。Owner 必须显式给出，因为
	 * APlayerState::GetPlayerController 只认 Owner，而 AController::SetPlayerState 不写 Owner，正式
	 * AController::InitPlayerState 也是带 Owner 生成 PlayerState 的。
	 */
	static bool AttachStablePlayerState(UWorld* World, APlayerController* Controller, const FString& StableNetId)
	{
		if (!World || !Controller || StableNetId.IsEmpty())
		{
			return false;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Controller;
		APlayerState* PlayerState = World->SpawnActor<APlayerState>(APlayerState::StaticClass(), SpawnParameters);
		if (!PlayerState)
		{
			return false;
		}
		const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(StableNetId,
			UOnlineEngineInterface::Get()->GetDefaultOnlineSubsystemName());
		PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
		Controller->SetPlayerState(PlayerState);
		// 测试 World 不会走 InitializeActorsForPlay，SpawnActor 因此根本不调用 PostInitializeComponents，
		// PlayerState 也就没机会像正式流程那样自己登记进 GameState->PlayerArray。这里补上引擎替我们做的那一步，
		// 让读 PlayerArray 的营地遍历拿到真实数据；没有 GameState 的用例保持原样，不需要这份登记。
		if (AGameStateBase* GameState = World->GetGameState())
		{
			GameState->AddPlayerState(PlayerState);
		}
		return true;
	}

	/** 鱼缸挂接流程：通过反射设置关卡私有引用，只用于 Automation 装配；生产代码仍要求关卡显式摆放并配置该属性。 */
	static bool AttachSharedFishTank(ACatCampHubActor* Camp, ACatFishTankActor* Tank)
	{
		FObjectProperty* SharedTankProperty = FindFProperty<FObjectProperty>(ACatCampHubActor::StaticClass(), TEXT("SharedFishTank"));
		if (!Camp || !Tank || !SharedTankProperty)
		{
			return false;
		}
		SharedTankProperty->SetObjectPropertyValue_InContainer(Camp, Tank);
		return true;
	}
}

// 测试流程：在真实 World 中放置营地、Controller、Character 和目标身份；先用正式范围查询观察进入/离开，再验证休息
// fail-closed 与救援拒绝重放不会因同 RequestId 换目标而复活。
bool FCatCampHubActorRangeAndFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatCampHubActorTest::FScopedCampSettings SettingsGuard;
	CatCampHubActorTest::FScopedConditionRestSettings ConditionSettingsGuard;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 CampHub 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCampHubActor* Camp = World ? World->SpawnActor<ACatCampHubActor>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatCharacter* TargetCharacter = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatCharacter* DriftTargetCharacter = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	APlayerController* Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
	APlayerController* TargetController = World ? World->SpawnActor<APlayerController>() : nullptr;
	APlayerController* DriftTargetController = World ? World->SpawnActor<APlayerController>() : nullptr;
	TestNotNull(TEXT("营地测试 World 可用"), World);
	TestNotNull(TEXT("可生成固定营地 Actor"), Camp);
	TestNotNull(TEXT("可生成项目 Character"), Character);
	TestNotNull(TEXT("可生成救援目标 Character"), TargetCharacter);
	TestNotNull(TEXT("可生成漂移目标 Character"), DriftTargetCharacter);
	TestNotNull(TEXT("可生成测试 Controller"), Controller);
	TestNotNull(TEXT("可生成目标 Controller"), TargetController);
	TestNotNull(TEXT("可生成漂移目标 Controller"), DriftTargetController);
	if (!Camp || !Character || !TargetCharacter || !DriftTargetCharacter || !Controller || !TargetController || !DriftTargetController)
	{
		return false;
	}

	TestTrue(TEXT("Helper Controller 绑定稳定身份"), CatCampHubActorTest::AttachStablePlayerState(World, Controller,
		TEXT("player:camp-helper")));
	TestTrue(TEXT("目标 Controller 绑定稳定身份"), CatCampHubActorTest::AttachStablePlayerState(World, TargetController,
		TEXT("player:camp-target")));
	TestTrue(TEXT("漂移目标 Controller 绑定稳定身份"), CatCampHubActorTest::AttachStablePlayerState(World, DriftTargetController,
		TEXT("player:camp-drift-target")));
	Camp->SetActorLocation(FVector::ZeroVector);
	Character->SetActorLocation(FVector(50.0, 0.0, 0.0));
	TargetCharacter->SetActorLocation(FVector(75.0, 0.0, 0.0));
	DriftTargetCharacter->SetActorLocation(FVector(90.0, 0.0, 0.0));
	Controller->Possess(Character);
	TargetController->Possess(TargetCharacter);
	DriftTargetController->Possess(DriftTargetCharacter);
	Character->SetActorLocation(FVector(500.0, 0.0, 0.0));
	const FGuid RequestId = FGuid::NewGuid();
	const FCatDomainCommandResult RestResult = Camp->RequestRest(Controller, RequestId);
	TestFalse(TEXT("范围外休息不会提交"), RestResult.bCommitted);
	TestEqual(TEXT("范围外休息返回 PolicyUndecided"), RestResult.Error, ECatDomainCommandError::PolicyUndecided);
	TestEqual(TEXT("休息拒绝保留 RequestId"), RestResult.RequestId, RequestId);
	Character->SetActorLocation(FVector(50.0, 0.0, 0.0));
	const FCatDomainCommandResult RestRejectedReplay = Camp->RequestRest(Controller, RequestId);
	TestFalse(TEXT("范围外休息拒绝重放不重新读取当前位置"), RestRejectedReplay.bCommitted);
	TestEqual(TEXT("范围外休息拒绝重放返回 AlreadyResolved"), RestRejectedReplay.Error, ECatDomainCommandError::AlreadyResolved);

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	TestNotNull(TEXT("营地休息测试 Character ASC 可用"), ASC);
	if (ASC)
	{
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 12.0);
		const FGuid RestSuccessRequestId = FGuid::NewGuid();
		const FCatDomainCommandResult RestSuccess = Camp->RequestRest(Controller, RestSuccessRequestId);
		TestTrue(TEXT("配置完整且人在营地时休息提交"), RestSuccess.bCommitted);
		TestEqual(TEXT("营地休息成功无错误"), RestSuccess.Error, ECatDomainCommandError::None);
		TestEqual(TEXT("营地休息清空 Poison"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 0.0f);
		// 先把 Poison 改回非零，再用同一个 RequestId 离营重放：重放只回放首次终态、不再次执行清毒，所以这个值必须原样留下。
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 30.0);
		Character->SetActorLocation(FVector(500.0, 0.0, 0.0));
		const FCatDomainCommandResult RestSuccessReplay = Camp->RequestRest(Controller, RestSuccessRequestId);
		TestFalse(TEXT("休息成功后离营重放不再次提交"), RestSuccessReplay.bCommitted);
		TestEqual(TEXT("休息成功后离营重放返回 AlreadyResolved"), RestSuccessReplay.Error, ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("休息成功重放不重复清毒"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 30.0f);
	}

	const FCatDomainCommandResult CampfireResult = Camp->RequestCampfirePlayback(nullptr, FGuid::NewGuid());
	TestFalse(TEXT("缺身份篝火请求不会提交"), CampfireResult.bCommitted);
	TestEqual(TEXT("缺身份篝火请求 fail-closed"), CampfireResult.Error, ECatDomainCommandError::PolicyUndecided);

	Character->SetActorLocation(FVector(50.0, 0.0, 0.0));
	const FGuid RescueRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult RescueRejected = Camp->RescueToCamp(Controller, TargetCharacter, RescueRequestId);
	TestFalse(TEXT("目标未倒地时救援首次不提交"), RescueRejected.bCommitted);
	TestEqual(TEXT("目标未倒地时救援返回 InvalidPhase"), RescueRejected.Error, ECatDomainCommandError::InvalidPhase);
	const FCatDomainCommandResult RescueReplay = Camp->RescueToCamp(Controller, TargetCharacter, RescueRequestId);
	TestFalse(TEXT("救援拒绝重放不重新读取目标状态"), RescueReplay.bCommitted);
	TestEqual(TEXT("救援拒绝重放返回 AlreadyResolved"), RescueReplay.Error, ECatDomainCommandError::AlreadyResolved);
	const FCatDomainCommandResult RescuePayloadDrift = Camp->RescueToCamp(Controller, DriftTargetCharacter, RescueRequestId);
	TestFalse(TEXT("同 RequestId 更换救援目标不提交"), RescuePayloadDrift.bCommitted);
	TestEqual(TEXT("同 RequestId 更换救援目标返回 InvalidPayload"), RescuePayloadDrift.Error,
		ECatDomainCommandError::InvalidPayload);
	return !HasAnyErrors();
}

// 测试流程：用真实 Character 鱼护、共享鱼缸和 Items 捕获事务种出一条鱼；首次 Camp 入缸成功后，原样重放必须命中 Camp
// 缓存而不是重新读已为空的鱼护，同 RequestId 换鱼则稳定拒绝。
bool FCatCampHubActorTankTransferReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatCampHubActorTest::FScopedCampSettings CampSettingsGuard;
	CatCampHubActorTest::FScopedTankTransferSettings TankSettingsGuard;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Camp 入缸测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCampHubActor* Camp = World ? World->SpawnActor<ACatCampHubActor>() : nullptr;
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatFishTankActor* Tank = World ? World->SpawnActor<ACatFishTankActor>() : nullptr;
	APlayerController* Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	TestNotNull(TEXT("入缸测试 World 可用"), World);
	TestNotNull(TEXT("可生成营地 Actor"), Camp);
	TestNotNull(TEXT("可生成钓手 Character"), Character);
	TestNotNull(TEXT("可生成共享鱼缸 Actor"), Tank);
	TestNotNull(TEXT("可生成请求 Controller"), Controller);
	TestNotNull(TEXT("可取得 Items 服务"), Items);
	if (!World || !Camp || !Character || !Tank || !Controller || !Items)
	{
		return false;
	}

	const FString StableNetId(TEXT("player:camp-tank"));
	TestTrue(TEXT("入缸 Controller 绑定稳定身份"), CatCampHubActorTest::AttachStablePlayerState(World, Controller, StableNetId));
	TestTrue(TEXT("Automation 关卡挂接共享鱼缸引用"), CatCampHubActorTest::AttachSharedFishTank(Camp, Tank));
	Camp->SetActorLocation(FVector::ZeroVector);
	Character->SetActorLocation(FVector(50.0, 0.0, 0.0));
	Controller->Possess(Character);
	if (!Tank->HasActorBegunPlay())
	{
		Tank->DispatchBeginPlay();
	}
	TestTrue(TEXT("占有后个人鱼护完成注册"), Character->GetPersonalFishGuardId().IsValid());
	TestTrue(TEXT("鱼缸 BeginPlay 后完成共享容器注册"), Tank->GetTankContainerId().IsValid());

	FCatContainerSnapshot InitialGuardSnapshot;
	FCatContainerSnapshot InitialTankSnapshot;
	TestTrue(TEXT("可读取注册后的个人鱼护 Snapshot"), Items->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), InitialGuardSnapshot));
	TestTrue(TEXT("可读取注册后的共享鱼缸 Snapshot"), Items->TryGetContainerSnapshot(Tank->GetTankContainerId(), InitialTankSnapshot));

	const FGuid FishInstanceId = FGuid::NewGuid();
	FCatCaptureCommitCommand CaptureCommand;
	CaptureCommand.Context.RequestId = FGuid::NewGuid();
	CaptureCommand.Context.ExpectedRevision = InitialGuardSnapshot.Revision;
	CaptureCommand.Context.StableNetId = StableNetId;
	CaptureCommand.FishingSessionId = FGuid::NewGuid();
	CaptureCommand.FishInstanceId = FishInstanceId;
	CaptureCommand.FishDefinitionId = TankSettingsGuard.FishDefinitionId;
	CaptureCommand.TargetContainerId = Character->GetPersonalFishGuardId();
	CaptureCommand.WeightKilograms = 1.5;
	CaptureCommand.SacrificeContribution = 3;
	const FCatCaptureCommitResult CaptureResult = Items->CommitCapture(CaptureCommand);
	TestTrue(TEXT("通过 Items 正式捕获入口种入个人鱼护"), CaptureResult.Command.bCommitted);
	TestEqual(TEXT("种鱼后个人鱼护 Revision 前提可用于入缸"), CaptureResult.Command.Error, ECatDomainCommandError::None);

	const FGuid TransferRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult FirstTransfer = Camp->TransferFishToTank(Controller, TransferRequestId, FishInstanceId,
		CaptureResult.Command.Revision, InitialTankSnapshot.Revision);
	TestTrue(TEXT("首次入缸通过 Camp 正式入口提交"), FirstTransfer.bCommitted);
	TestEqual(TEXT("首次入缸成功"), FirstTransfer.Error, ECatDomainCommandError::None);

	FCatContainerSnapshot GuardSnapshot;
	FCatContainerSnapshot TankSnapshot;
	TestTrue(TEXT("可读取入缸后的个人鱼护 Snapshot"), Items->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), GuardSnapshot));
	TestTrue(TEXT("可读取入缸后的共享鱼缸 Snapshot"), Items->TryGetContainerSnapshot(Tank->GetTankContainerId(), TankSnapshot));
	TestEqual(TEXT("首次成功后个人鱼护已移走该鱼"), GuardSnapshot.Fish.Num(), 0);
	TestEqual(TEXT("首次成功后共享鱼缸持有该鱼"), TankSnapshot.Fish.Num(), 1);

	const FCatDomainCommandResult ReplayTransfer = Camp->TransferFishToTank(Controller, TransferRequestId, FishInstanceId,
		CaptureResult.Command.Revision, InitialTankSnapshot.Revision);
	TestFalse(TEXT("原样重放不再次提交"), ReplayTransfer.bCommitted);
	TestEqual(TEXT("原样重放在 Camp 层返回 AlreadyResolved，而不是重新读取空鱼护"), ReplayTransfer.Error,
		ECatDomainCommandError::AlreadyResolved);

	const FCatDomainCommandResult DriftTransfer = Camp->TransferFishToTank(Controller, TransferRequestId, FGuid::NewGuid(),
		CaptureResult.Command.Revision, InitialTankSnapshot.Revision);
	TestFalse(TEXT("同 RequestId 更换入缸鱼不提交"), DriftTransfer.bCommitted);
	TestEqual(TEXT("同 RequestId 更换入缸鱼返回 InvalidPayload"), DriftTransfer.Error,
		ECatDomainCommandError::InvalidPayload);
	return !HasAnyErrors();
}

// 测试流程：在真实 World 中放置 GameState、营地和两名玩家，一人站在营地内、一人远在营地外，然后逐个阶段驱动 Run 公共
// 快照。白天必须拒绝篝火，普通夜晚必须能点亮并广播一次表现，结算夜必须在有人不在营地时照样成立且封面候选只登记在场
// 者，离开玩家后单人局仍成立，进入 Ending 后重新拒绝。这样锁住的是“每晚都有篝火”和“不在场的人不阻塞篝火”两条产品事
// 实，而不是某一次成功调用。
bool FCatCampfireNightPhaseAndPartialPresenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatCampHubActorTest::FScopedCampSettings SettingsGuard;
	// 印记准入是工程事实源，项目默认空清单 = 全拒。本用例下面会把 CampfireCoverEventId 打开成 "CampfireCover"，
	// 那只是"篝火想提交哪种候选"；候选真正能不能被接受还要过总清单这道独立的门。
	// 不在这里显式放行的话，三次封面候选全部落 PolicyUndecided，而本用例锁的是篝火本身每晚都成立，不是准入名单。
	// 上限取 8 是给本用例的三次候选留足余量；单局上限的产品取值飞书未裁，这个数只是夹具刻度，不进任何 .ini。
	const CatImprintSettingsTest::FImprintSettingsOverride ImprintGuard({TEXT("CampfireCover")}, 8);
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建篝火阶段测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	// GameState 必须先于 PlayerState 生成，并且要手动挂到 World 上：测试 World 没有 GameMode，
	// AGameStateBase::PostInitializeComponents 里那句 World->SetGameState 也就不会执行，
	// 之后所有按 World->GetGameState() 找营地围坐者的代码都会读到空。
	ACatfishingGameState* GameState = World ? World->SpawnActor<ACatfishingGameState>() : nullptr;
	if (World && GameState)
	{
		World->SetGameState(GameState);
	}
	ACatCampHubActor* Camp = World ? World->SpawnActor<ACatCampHubActor>() : nullptr;
	ACatCharacter* SeatedCharacter = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatCharacter* AwayCharacter = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	APlayerController* SeatedController = World ? World->SpawnActor<APlayerController>() : nullptr;
	APlayerController* AwayController = World ? World->SpawnActor<APlayerController>() : nullptr;
	UCatRunImprintService* Imprint = World ? World->GetSubsystem<UCatRunImprintService>() : nullptr;
	TestNotNull(TEXT("篝火测试 World 可用"), World);
	TestNotNull(TEXT("可生成项目 GameState"), GameState);
	TestNotNull(TEXT("可生成固定营地 Actor"), Camp);
	TestNotNull(TEXT("可生成围坐 Character"), SeatedCharacter);
	TestNotNull(TEXT("可生成离营 Character"), AwayCharacter);
	TestNotNull(TEXT("可生成围坐 Controller"), SeatedController);
	TestNotNull(TEXT("可生成离营 Controller"), AwayController);
	TestNotNull(TEXT("可取得 RunImprint 服务"), Imprint);
	if (!World || !GameState || !Camp || !SeatedCharacter || !AwayCharacter || !SeatedController || !AwayController
		|| !Imprint)
	{
		return false;
	}

	const FString SeatedStableNetId(TEXT("player:campfire-seated"));
	const FString AwayStableNetId(TEXT("player:campfire-away"));
	TestTrue(TEXT("围坐 Controller 绑定稳定身份"), CatCampHubActorTest::AttachStablePlayerState(World, SeatedController,
		SeatedStableNetId));
	TestTrue(TEXT("离营 Controller 绑定稳定身份"), CatCampHubActorTest::AttachStablePlayerState(World, AwayController,
		AwayStableNetId));
	Camp->SetActorLocation(FVector::ZeroVector);
	SeatedCharacter->SetActorLocation(FVector(50.0, 0.0, 0.0));
	AwayCharacter->SetActorLocation(FVector(5000.0, 0.0, 0.0));
	SeatedController->Possess(SeatedCharacter);
	AwayController->Possess(AwayCharacter);
	TestEqual(TEXT("两名玩家都登记进 PlayerArray"), GameState->PlayerArray.Num(), 2);
	int32 PlaybackBroadcastCount = 0;
	const FDelegateHandle PlaybackHandle = Camp->OnCampfirePlaybackRequested.AddLambda(
		[&PlaybackBroadcastCount](FGuid) { ++PlaybackBroadcastCount; });

	FCatRunPublicState RunState;
	RunState.Phase.RunId = FGuid::NewGuid();
	RunState.Phase.DayIndex = 1;
	RunState.Phase.Phase = ECatRunPhase::DayActive;
	GameState->SetRunPublicStateFromAuthority(RunState);
	const FCatDomainCommandResult DayResult = Camp->RequestCampfirePlayback(SeatedController, FGuid::NewGuid());
	TestFalse(TEXT("白天不提供篝火回看"), DayResult.bCommitted);
	TestEqual(TEXT("白天篝火返回 InvalidPhase"), DayResult.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("白天篝火不广播表现"), PlaybackBroadcastCount, 0);

	RunState.Phase.Phase = ECatRunPhase::NormalNight;
	GameState->SetRunPublicStateFromAuthority(RunState);
	const FCatDomainCommandResult NormalNightResult = Camp->RequestCampfirePlayback(SeatedController, FGuid::NewGuid());
	TestTrue(TEXT("普通夜晚也能点亮篝火"), NormalNightResult.bCommitted);
	TestEqual(TEXT("普通夜晚篝火无错误"), NormalNightResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("普通夜晚篝火广播一次表现"), PlaybackBroadcastCount, 1);

	SettingsGuard.Settings->CampfireCoverEventId = TEXT("CampfireCover");
	RunState.Phase.Phase = ECatRunPhase::FailureSettlementNight;
	GameState->SetRunPublicStateFromAuthority(RunState);
	const FGuid CoverRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult CoverResult = Camp->RequestCampfirePlayback(SeatedController, CoverRequestId);
	TestTrue(TEXT("有人不在营地时结算夜篝火仍然成立"), CoverResult.bCommitted);
	TestEqual(TEXT("有人不在营地时结算夜篝火无错误"), CoverResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("结算夜篝火广播一次表现"), PlaybackBroadcastCount, 2);

	// 用只读候选预检回读 Camp 实际提交了谁：同 CandidateId 只有全部不可变事实一致才返回 true，
	// 因此它能同时证明在场者被登记、离营者没有被登记。
	FCatImprintCandidate SeatedOnlyCandidate;
	SeatedOnlyCandidate.CandidateId = CoverRequestId;
	SeatedOnlyCandidate.RunId = RunState.Phase.RunId;
	SeatedOnlyCandidate.EventType = TEXT("CampfireCover");
	SeatedOnlyCandidate.SubjectId = RunState.Phase.RunId;
	SeatedOnlyCandidate.ParticipantStableNetIds = {SeatedStableNetId};
	SeatedOnlyCandidate.ParticipantCount = 1;
	TestTrue(TEXT("封面候选只登记在场围坐者"), Imprint->CanAcceptImprintCandidate(SeatedOnlyCandidate));
	FCatImprintCandidate BothPlayersCandidate = SeatedOnlyCandidate;
	BothPlayersCandidate.ParticipantStableNetIds = {SeatedStableNetId, AwayStableNetId};
	BothPlayersCandidate.ParticipantCount = 2;
	TestFalse(TEXT("离营玩家没有被写进封面候选"), Imprint->CanAcceptImprintCandidate(BothPlayersCandidate));

	AwayController->Destroy();
	TestEqual(TEXT("离开的玩家不再留在 PlayerArray"), GameState->PlayerArray.Num(), 1);
	const FGuid SoloRequestId = FGuid::NewGuid();
	const FCatDomainCommandResult SoloResult = Camp->RequestCampfirePlayback(SeatedController, SoloRequestId);
	TestTrue(TEXT("单人局篝火照样成立"), SoloResult.bCommitted);
	TestEqual(TEXT("单人局篝火无错误"), SoloResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("单人局篝火广播一次表现"), PlaybackBroadcastCount, 3);
	FCatImprintCandidate SoloCandidate = SeatedOnlyCandidate;
	SoloCandidate.CandidateId = SoloRequestId;
	TestTrue(TEXT("单人局封面候选只有一只猫"), Imprint->CanAcceptImprintCandidate(SoloCandidate));

	const FCatDomainCommandResult SoloReplay = Camp->RequestCampfirePlayback(SeatedController, SoloRequestId);
	TestFalse(TEXT("同 RequestId 重放不再次点亮篝火"), SoloReplay.bCommitted);
	TestEqual(TEXT("同 RequestId 重放返回 AlreadyResolved"), SoloReplay.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("同 RequestId 重放不重复广播表现"), PlaybackBroadcastCount, 3);

	RunState.Phase.Phase = ECatRunPhase::Ending;
	GameState->SetRunPublicStateFromAuthority(RunState);
	const FCatDomainCommandResult EndingResult = Camp->RequestCampfirePlayback(SeatedController, FGuid::NewGuid());
	TestFalse(TEXT("局末收口阶段不再点亮篝火"), EndingResult.bCommitted);
	TestEqual(TEXT("局末收口阶段篝火返回 InvalidPhase"), EndingResult.Error, ECatDomainCommandError::InvalidPhase);
	TestEqual(TEXT("局末收口阶段不广播表现"), PlaybackBroadcastCount, 3);

	Camp->OnCampfirePlaybackRequested.Remove(PlaybackHandle);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
