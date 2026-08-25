#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Blueprint/UserWidget.h"
#include "UI/CatLakeReachWidget.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLakeReachWidgetViewStateContractTest,
	"Catfishing.Unit.UI.Reach.SingleRootCarriesHudFishingGuardAndCollectionFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：只通过统一根 View 的公开 DTO seam 填入身体、Fishing 反馈、鱼护和图鉴事实；复制结果必须保留全部切面，且类型仍只有一个 UUserWidget 根。
bool FCatLakeReachWidgetViewStateContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("LakeReachWidget 是唯一原生 UUserWidget 根"),
		UCatLakeReachWidget::StaticClass()->IsChildOf(UUserWidget::StaticClass()));
	TestNull(TEXT("Profile 图鉴记录保持原生 C++ 展示字段，不要求其权威类型支持 Blueprint"),
		FindFProperty<FProperty>(FCatUIReachViewState::StaticStruct(),
			GET_MEMBER_NAME_CHECKED(FCatUIReachViewState, FishCollection)));

	FCatUIReachViewState ViewState;
	ViewState.Poison = 12.5f;
	ViewState.Growth.TotalExperience = 18;
	ViewState.Growth.ExperienceInCurrentSlot = 8;
	ViewState.Growth.PendingChoiceCount = 1;
	ViewState.Fishing.Phase = ECatFishingPhase::NearShore;
	ViewState.bHasFishingSession = true;
	ViewState.LastFishingCommandResult.CommandType = ECatFishingCommandType::RequestScoop;
	ViewState.LastFishingCommandResult.Error = ECatFishingCommandError::None;
	ViewState.bHasFishingCommandResult = true;
	ViewState.bMenuOpen = true;
	ViewState.MenuToggleKeyName = TEXT("Tab");
	ViewState.bCanRequestOnlineLeave = true;
	FCatFishInstance Fish;
	Fish.FishInstanceId = FGuid::NewGuid();
	Fish.FishDefinitionId = TEXT("Carp");
	Fish.WeightKilograms = 2.5;
	ViewState.PersonalFishGuard.Fish.Add(Fish);
	FCatFishCollectionRecord Record;
	Record.FishDefinitionId = TEXT("Carp");
	Record.State = ECatFishCollectionState::Recorded;
	ViewState.FishCollection.Add(Record);
	ViewState.bFishCollectionAvailable = true;

	const FCatUIReachViewState CopiedState = ViewState;
	TestEqual(TEXT("统一 DTO 保留 Poison HUD"), CopiedState.Poison, 12.5f);
	TestEqual(TEXT("统一 DTO 保留成长经验槽"), CopiedState.Growth.ExperienceInCurrentSlot, 8);
	TestEqual(TEXT("统一 DTO 保留待选次数"), CopiedState.Growth.PendingChoiceCount, 1);
	TestEqual(TEXT("统一 DTO 保留 Fishing 阶段"), CopiedState.Fishing.Phase, ECatFishingPhase::NearShore);
	TestEqual(TEXT("统一 DTO 保留结构化命令反馈"), CopiedState.LastFishingCommandResult.CommandType,
		ECatFishingCommandType::RequestScoop);
	TestEqual(TEXT("统一 DTO 保留个人鱼护实物"), CopiedState.PersonalFishGuard.Fish.Num(), 1);
	TestEqual(TEXT("统一 DTO 保留 durable 图鉴"), CopiedState.FishCollection.Num(), 1);
	TestTrue(TEXT("菜单、鱼护和图鉴仍属于同一根状态"), CopiedState.bMenuOpen && CopiedState.bFishCollectionAvailable);
	TestTrue(TEXT("Lake 菜单保留正式 Online 离局 gate"), CopiedState.bCanRequestOnlineLeave);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
