#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Camp/CatCampCollectionBoardActor.h"
#include "Character/CatCharacter.h"
#include "Collection/CatRunFishCollectionComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "EngineUtils.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/CatFishingSession.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystemTypes.h"
#include "Save/CatRunSaveGame.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRunFishCollectionLifecycleTest,
	"Catfishing.Unit.Collection.RunBoard.UnionIdentityReplayAndRunLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRunFishCollectionSaveTest,
	"Catfishing.Unit.Collection.RunBoard.WorldSaveRoundTripAndLegacyEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRunFishCollectionHandoffTest,
	"Catfishing.Unit.Collection.RunBoard.LandedAndScoopedCapturesUseHooker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatRunFishCollectionTests
{
	struct FFixture
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		ACatfishingGameState* State = nullptr;
		FCatRunPublicState Run;
		bool Create(FAutomationTestBase* Test)
		{
			if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
			Wrapper.ForwardErrorMessages(Test);
			World = Wrapper.GetTestWorld();
			FURL URL;
			URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
			if (!World->SetGameMode(URL) || !Wrapper.BeginPlayInTestWorld()) return false;
			State = World->GetGameState<ACatfishingGameState>();
			if (!State || !State->GetRunFishCollection()) return false;
			Run.Phase.RunId = FGuid::NewGuid();
			Run.Phase.Phase = ECatRunPhase::DayActive;
			Run.Phase.DayIndex = 1;
			State->SetRunPublicStateFromAuthority(Run);
			return true;
		}
	};
}

bool FCatRunFishCollectionLifecycleTest::RunTest(const FString& Parameters)
{
	CatRunFishCollectionTests::FFixture F;
	if (!TestTrue(TEXT("创建真实 GameState 默认公共容器"), F.Create(this))) return false;
	auto* Collection = F.State->GetRunFishCollection();
	const FGuid FishA = FGuid::NewGuid();
	TestTrue(TEXT("第一次捕获上页"), Collection->RecordCaptureFromAuthority(FishA, TEXT("FishA"), TEXT("HookerA")));
	const int64 Revision = Collection->GetSnapshot().Revision;
	TestTrue(TEXT("同一实物重放成功"), Collection->RecordCaptureFromAuthority(FishA, TEXT("FishA"), TEXT("HookerA")));
	TestEqual(TEXT("重放不重复发布"), Collection->GetSnapshot().Revision, Revision);
	AddExpectedMessage(TEXT("Event=run_collection_rejected"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("同鱼不能改记拾取者"), Collection->RecordCaptureFromAuthority(FishA, TEXT("FishA"), TEXT("PickerB")));
	TestTrue(TEXT("同鱼种另一个上钩者加入"), Collection->RecordCaptureFromAuthority(FGuid::NewGuid(), TEXT("FishA"), TEXT("HookerB")));
	TestTrue(TEXT("同人再钓不重复盖章"), Collection->RecordCaptureFromAuthority(FGuid::NewGuid(), TEXT("FishA"), TEXT("HookerA")));
	TestTrue(TEXT("新鱼种加入新页"), Collection->RecordCaptureFromAuthority(FGuid::NewGuid(), TEXT("FishB"), TEXT("HookerA")));
	const auto Snapshot = Collection->GetSnapshot();
	TestEqual(TEXT("两鱼种两页"), Snapshot.Pages.Num(), 2);
	TestEqual(TEXT("同种两登记者"), Snapshot.Pages[0].Pawprints.Num(), 2);
	TestEqual(TEXT("同人跨页保持相同爪印"), Snapshot.Pages[0].Pawprints[0].RegistrantId, Snapshot.Pages[1].Pawprints[0].RegistrantId);
	auto* Board = F.World->SpawnActor<ACatCampCollectionBoardActor>();
	if (!TestNotNull(TEXT("捕获后才摆放的板子"), Board)) return false;
	TestEqual(TEXT("迟到陈列读取全部历史页"), Board->GetCollectionSnapshot().Pages.Num(), 2);
	F.Run.Phase.Phase = ECatRunPhase::NormalNight;
	F.State->SetRunPublicStateFromAuthority(F.Run);
	F.Run.Phase.Phase = ECatRunPhase::DayActive;
	++F.Run.Phase.DayIndex;
	F.State->SetRunPublicStateFromAuthority(F.Run);
	TestEqual(TEXT("跨天保留页与爪印"), Board->GetCollectionSnapshot().Pages.Num(), 2);
	F.Run.Phase.Phase = ECatRunPhase::Ending;
	F.Run.EndReason = ECatRunEndReason::Success;
	F.State->SetRunPublicStateFromAuthority(F.Run);
	TestTrue(TEXT("自然局末清空"), Board->GetCollectionSnapshot().Pages.IsEmpty());
	TestFalse(TEXT("终局迟到捕获不能复活板子"), Collection->RecordCaptureFromAuthority(FGuid::NewGuid(), TEXT("FishC"), TEXT("HookerA")));
	F.Run.Phase.RunId = FGuid::NewGuid();
	F.Run.Phase.Phase = ECatRunPhase::DayActive;
	F.Run.EndReason = ECatRunEndReason::None;
	F.State->SetRunPublicStateFromAuthority(F.Run);
	TestTrue(TEXT("新局可独立登记旧实物键"), Collection->RecordCaptureFromAuthority(FishA, TEXT("FishA"), TEXT("HookerA")));
	TestTrue(TEXT("新局不继承旧爪印身份"), Collection->GetSnapshot().Pages[0].Pawprints[0].RegistrantId != Snapshot.Pages[0].Pawprints[0].RegistrantId);
	return !HasAnyErrors();
}

bool FCatRunFishCollectionSaveTest::RunTest(const FString& Parameters)
{
	CatRunFishCollectionTests::FFixture F;
	if (!F.Create(this)) return false;
	auto* Collection = F.State->GetRunFishCollection();
	const FGuid FishId = FGuid::NewGuid();
	Collection->RecordCaptureFromAuthority(FishId, TEXT("FishA"), TEXT("HookerA"));
	F.Run.Phase.Phase = ECatRunPhase::Ending;
	F.Run.EndReason = ECatRunEndReason::HostExit;
	F.State->SetRunPublicStateFromAuthority(F.Run);
	TestEqual(TEXT("房主中断保留世界断点"), Collection->GetCapturesForWorldSave().Num(), 1);
	auto* Save = NewObject<UCatRunSaveGame>();
	Save->RunFishCollectionCaptures = Collection->GetCapturesForWorldSave();
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("使用引擎 SaveGame 序列化"), UGameplayStatics::SaveGameToMemory(Save, Bytes))) return false;
	auto* Loaded = Cast<UCatRunSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("真实反序列化成功"), Loaded)) return false;
	TestEqual(TEXT("公共记录随分仓断点写入 v7 格式"), Loaded->FormatVersion, 7);
	F.Run = FCatRunPublicState();
	F.Run.Phase.RunId = FGuid::NewGuid();
	F.State->SetRunPublicStateFromAuthority(F.Run);
	TestTrue(TEXT("启动前恢复公共记录"), Collection->RestoreCapturesFromAuthority(Loaded->RunFishCollectionCaptures));
	TestEqual(TEXT("爪印原样恢复"), Collection->GetSnapshot().Pages[0].Pawprints[0].RegistrantId,
		Save->RunFishCollectionCaptures[0].Pawprint.RegistrantId);
	F.Run.Phase.Phase = ECatRunPhase::DayActive;
	F.State->SetRunPublicStateFromAuthority(F.Run);
	const int64 Revision = Collection->GetSnapshot().Revision;
	TestTrue(TEXT("恢复后的旧捕获仍幂等"), Collection->RecordCaptureFromAuthority(FishId, TEXT("FishA"), TEXT("HookerA")));
	TestEqual(TEXT("恢复重放不重复上页"), Collection->GetSnapshot().Revision, Revision);
	TestTrue(TEXT("重连上钩者继续沿用爪印"), Collection->RecordCaptureFromAuthority(FGuid::NewGuid(), TEXT("FishA"), TEXT("HookerA")));
	TestEqual(TEXT("恢复后同人同种仍一枚爪印"), Collection->GetSnapshot().Pages[0].Pawprints.Num(), 1);
	auto Invalid = Loaded->RunFishCollectionCaptures;
	const auto Duplicate = Invalid[0];
	Invalid.Add(Duplicate);
	TestFalse(TEXT("损坏断点的重复实物键被预检拒绝"), UCatRunFishCollectionComponent::ValidateCaptures(Invalid));
	F.Run.Phase.RunId = FGuid::NewGuid();
	F.Run.Phase.Phase = ECatRunPhase::NotStarted;
	F.State->SetRunPublicStateFromAuthority(F.Run);
	TestTrue(TEXT("没有新增字段的旧档按空数组恢复"), Collection->RestoreCapturesFromAuthority({}));
	TestTrue(TEXT("旧档空板不从个人图鉴补页"), Collection->GetSnapshot().Pages.IsEmpty());
	return !HasAnyErrors();
}

// 直接执行正式实物交接出口：验证成功才上页、抄网者 B 不覆盖上钩者 A、销毁实物不撤销公共页。
bool FCatRunFishCollectionHandoffTest::RunTest(const FString& Parameters)
{
	CatRunFishCollectionTests::FFixture F;
	if (!F.Create(this)) return false;
	UCatFishDefinition* Definition = nullptr;
	for (const auto& Ref : GetDefault<UCatFishCatalogSettings>()->Definitions)
	{
		if (auto* Candidate = Ref.LoadSynchronous(); Candidate && Candidate->IsRuntimeDefinitionReady())
		{
			Definition = Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("正式鱼资产可用"), Definition)) return false;
	auto* Hooker = F.World->SpawnActor<ACatfishingPlayerState>();
	auto* Picker = F.World->SpawnActor<ACatfishingPlayerState>();
	auto* Cat = F.World->SpawnActor<ACatCharacter>();
	if (!Hooker || !Picker || !Cat) return false;
	// FUniqueNetIdRepl 只接 FUniqueNetIdRef；Create 返回的是派生的 FUniqueNetIdStringRef，
	// 必须先经一个 FUniqueNetIdRef 局部量做上行转换（全库其余调用点都是这个两步写法）。
	const FUniqueNetIdRef HookerId = FUniqueNetIdString::Create(TEXT("HookerA"), FName(TEXT("CAT_TEST")));
	const FUniqueNetIdRef PickerId = FUniqueNetIdString::Create(TEXT("PickerB"), FName(TEXT("CAT_TEST")));
	Hooker->SetUniqueId(FUniqueNetIdRepl(HookerId));
	Picker->SetUniqueId(FUniqueNetIdRepl(PickerId));
	Hooker->SetPlayerName(TEXT("A"));
	Picker->SetPlayerName(TEXT("B"));
	Cat->SetPlayerState(Picker);
	auto* Session = F.World->SpawnActor<ACatFishingSession>();
	auto* Encounter = F.World->SpawnActor<ACatFishEncounterActor>();
	if (!Session || !Encounter) return false;
	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	Session->Snapshot.FishEncounterActor = Encounter;
	Session->FishDefinition = Definition;
	Session->FishWeightKilograms = Definition->MinimumWeightKilograms;
	Session->FishVisualScale = 1.0;
	Session->AttemptSnapshot.WaterRegion.RegionId = Definition->RegionIds[0];
	Session->AttemptSnapshot.WaterRegion.GeometryRevision = 1;
	Session->CatchFisherStableNetId = TEXT("HookerA");
	auto* Collection = F.State->GetRunFishCollection();
	TestFalse(TEXT("失败收鱼不上页"), Session->SpawnScoopedFishPickupFromAuthority(nullptr, Picker, TEXT("PickerB")));
	TestTrue(TEXT("失败后仍为空板"), Collection->GetSnapshot().Pages.IsEmpty());
	if (!TestTrue(TEXT("正式抄网出口完成嘴部交接"), Session->SpawnScoopedFishPickupFromAuthority(Cat, Picker, TEXT("PickerB")))) return false;
	TestEqual(TEXT("未入库已经自动上页"), Collection->GetSnapshot().Pages.Num(), 1);
	const auto& Capture = Collection->GetCapturesForWorldSave()[0];
	TestEqual(TEXT("公共登记归上钩者 A"), Capture.HookerStableNetId, FString(TEXT("HookerA")));
	TestEqual(TEXT("可见爪印不是抄网者 B"), Capture.Pawprint.DisplayName, FString(TEXT("A")));
	auto* Pickup = ACatFishPickupActor::FindCarriedFish(Cat);
	if (!TestNotNull(TEXT("B 实际叼鱼"), Pickup)) return false;
	Pickup->Destroy();
	TestEqual(TEXT("实物消失后板子保留记录"), Collection->GetSnapshot().Pages.Num(), 1);

	// 力竭拖岸与碾压共用同一落地出口；在夹具中使用正式钓具冻结和磨损接口。
	auto* LandedSession = F.World->SpawnActor<ACatFishingSession>();
	if (!LandedSession) return false;
	LandedSession->Snapshot.FishingSessionId = FGuid::NewGuid();
	LandedSession->Snapshot.FishEncounterActor = F.World->SpawnActor<ACatFishEncounterActor>();
	LandedSession->FishDefinition = Definition;
	LandedSession->FishWeightKilograms = Definition->MinimumWeightKilograms;
	LandedSession->FishVisualScale = 1.0;
	LandedSession->AttemptSnapshot.WaterRegion = Session->AttemptSnapshot.WaterRegion;
	LandedSession->CatchFisherStableNetId = TEXT("HookerA");
	auto* Equipment = Cat->GetEquipmentComponent();
	for (const FName Id : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
		if (!Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Id).bCommitted) return false;
	if (!Equipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, TEXT("BugBait"), 1).bCommitted) return false;
	if (!Equipment->Use(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, Equipment->GetSnapshot().RodItemInstanceId).bCommitted) return false;
	const auto Loadout = Equipment->GetSnapshot();
	if (!Equipment->BeginFishingUse(LandedSession->Snapshot.FishingSessionId, Loadout.RodItemInstanceId,
		Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId, Loadout.RodDefinitionId,
		Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Loadout.Revision).bUseAccepted) return false;
	if (!TestTrue(TEXT("落地夹具先完成真咬唯一扣饵"), Equipment->CommitFishingBaitDeferred(
		LandedSession->Snapshot.FishingSessionId).bApplied)) return false;
	LandedSession->CastEquipment = Equipment;
	if (!TestTrue(TEXT("正式落地出口完成交接"), LandedSession->SpawnLandedFishPickupFromAuthority(
		FVector(0, 0, 100), FVector::UpVector, TEXT("RunBoardAutomation")))) return false;
	TestEqual(TEXT("两条实际捕获各留事实"), Collection->GetCapturesForWorldSave().Num(), 2);
	TestEqual(TEXT("同人同种两条鱼仍一页一枚爪印"), Collection->GetSnapshot().Pages[0].Pawprints.Num(), 1);
	return !HasAnyErrors();
}

#endif
