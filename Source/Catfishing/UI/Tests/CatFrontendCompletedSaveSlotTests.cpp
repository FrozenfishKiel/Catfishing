#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UI/Frontend/CatFrontendSaveModel.h"
#include "UI/Frontend/CatFrontendRootWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFrontendCompletedSaveSlotTest,
	"Catfishing.Unit.UI.Frontend.CompletedSaveSlotDisablesContinueAndResetsOnReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFrontendCompletedSaveSlotTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	UClass* RowClass = LoadClass<UCatFrontendSaveSlotRowWidget>(nullptr,
		TEXT("/Game/UI/Frontend/WBP_CatSaveSlotRow.WBP_CatSaveSlotRow_C"));
	if (!TestNotNull(TEXT("正式存档行 WBP 可以加载"), RowClass)) return false;
	// 用 NewObject 而不是 CreateWidget：CreateWidget 会走 UMG 的 WidgetTree 初始化，
	// 在 -NullRHI 的 commandlet 里会为抽象原生基类构造一个瞬态占位对象并触发 ensure
	// （UObjectGlobals「Class which was marked abstract」），automation 把它当 Error 判失败。
	// ConfigureRow 只写字段与 SetRenderOpacity，控件指针为空时都有守卫，不需要 WidgetTree。
	auto* Row = NewObject<UCatFrontendSaveSlotRowWidget>(Wrapper.GetTestWorld(), RowClass);
	if (!TestNotNull(TEXT("通过正式资产创建存档行"), Row)) return false;
	FCatSaveSlotSummary Summary;
	Summary.SlotId = TEXT("CompletedRun");
	Summary.bRunCompleted = true;
	Row->ConfigureRow(nullptr, Summary);
	TestFalse(TEXT("完结槽的共用继续策略拒绝"), UCatFrontendSaveModel::CanContinueSummary(Summary));
	TestTrue(TEXT("行向蓝图暴露完结态"), Row->bRunCompleted);
	TestFalse(TEXT("行继续按钮不可用"), Row->bCanContinue);
	TestEqual(TEXT("完结行置灰"), Row->GetRenderOpacity(), 0.5f);
	TestEqual(TEXT("完结文案无需新必填控件即可读取"), Row->CompletionStatusText.ToString(), FString(TEXT("已完结")));
	Summary = FCatSaveSlotSummary();
	Summary.SlotId = TEXT("LegacyOrNewRun");
	Row->ConfigureRow(nullptr, Summary);
	TestTrue(TEXT("旧 v6 与新槽的默认完成标记仍允许继续"), UCatFrontendSaveModel::CanContinueSummary(Summary));
	TestFalse(TEXT("行复用清除完成标记"), Row->bRunCompleted);
	TestTrue(TEXT("行复用恢复继续"), Row->bCanContinue);
	TestEqual(TEXT("行复用恢复正常亮度"), Row->GetRenderOpacity(), 1.0f);
	TestTrue(TEXT("行复用不残留完结文案"), Row->CompletionStatusText.IsEmpty());
	Summary.SlotId = NAME_None;
	TestFalse(TEXT("空槽不能继续"), UCatFrontendSaveModel::CanContinueSummary(Summary));
	return !HasAnyErrors();
}
#endif
