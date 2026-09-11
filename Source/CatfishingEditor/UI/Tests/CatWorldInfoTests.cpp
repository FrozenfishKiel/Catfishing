#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/ListView.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/CatInteractable.h"
#include "UI/WorldInfo/CatWorldInfoComponent.h"
#include "UI/WorldInfo/CatWorldInfoRegistry.h"
#include "UI/WorldInfo/CatWorldInfoWidget.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"
#include <limits>

// 双端 PIE 的测试入口；Python 编辑器执行保护会把 RPC 强制当作本地调用，所以只在此安排下一帧发起正式请求。
// 命令仅编入 Editor 测试模块，不随游戏 Cook；参数是两端已有库存宿主的名字和格位，不创建或直接改写任何库存。
static FAutoConsoleCommandWithWorldAndArgs GCatWorldInfoProbeMove(
	TEXT("cat.WorldInfo.Probe.Move"), TEXT("PIE only: SourceActor SourceSlot TargetActor TargetSlot"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		// 先限制为正在运行的 PIE，再解析本地玩家和现存宿主；所有对象用弱引用跨过 Python 调用栈，失效就放弃请求。
		if (!World || World->WorldType != EWorldType::PIE || Args.Num() != 4) return;
		ACatfishingPlayerController* Player = Cast<ACatfishingPlayerController>(World->GetFirstPlayerController());
		AActor* Source = nullptr;
		AActor* Target = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetName() == Args[0]) Source = *It;
			if (It->GetName() == Args[2]) Target = *It;
		}
		int32 SourceSlot = INDEX_NONE;
		int32 TargetSlot = INDEX_NONE;
		if (!Player || !Player->IsLocalController() || !Source || !Target
			|| !LexTryParseString(SourceSlot, *Args[1]) || !LexTryParseString(TargetSlot, *Args[3])) return;
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[Player = TWeakObjectPtr<ACatfishingPlayerController>(Player), Source = TWeakObjectPtr<AActor>(Source),
			 Target = TWeakObjectPtr<AActor>(Target), SourceSlot, TargetSlot]()
			{
				// 此前选定的本地 Controller 和两个宿主下一帧仍有效才调用生产 RPC；距离、身份、库存内容及回执继续由原服务器链处理。
				if (Player.IsValid() && Source.IsValid() && Target.IsValid())
					Player->ServerMoveInventoryItemBetweenHosts(FGuid::NewGuid(), Source.Get(), SourceSlot, Target.Get(), TargetSlot);
			}));
	}));

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWorldInfoRegistrationTest,
	"Catfishing.UI.WorldInfo.Registration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 在内存世界给普通 Actor 挂组件，先确认注册组件不等于进入游戏，再通过真实 BeginPlay 验证自动登记。
// 随后检查重复登记、空来源与跨世界拒绝、显式注销和重新登记；最后分别销毁组件和 Actor，确认退出不会留下锚点。
// 两个世界均由引擎包装器在提前返回或正常退出时清理，不加载或保存项目地图。
bool FCatWorldInfoRegistrationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建注册测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	UCatWorldInfoRegistry* Registry = World->GetSubsystem<UCatWorldInfoRegistry>();
	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestTrue(TEXT("普通 Actor 和本世界注册表存在"), Owner && Registry)) return false;
	TestFalse(TEXT("信息宿主不要求交互接口"), Owner->Implements<UCatInteractable>());
	UCatWorldInfoComponent* Source = NewObject<UCatWorldInfoComponent>(Owner);
	Owner->AddInstanceComponent(Source);
	Owner->SetRootComponent(Source);
	Source->RegisterComponent();
	TestTrue(TEXT("普通 Actor 正常持有注册组件"), Source->IsRegistered() && Source->GetOwner() == Owner);
	TestEqual(TEXT("未开始游戏不提前发布信息源"), Registry->GetSources().Num(), 0);
	if (!TestTrue(TEXT("开始内存世界生命周期"), WorldWrapper.BeginPlayInTestWorld())) return false;
	TestTrue(TEXT("BeginPlay 自动登记组件"), Registry->GetSources().Contains(Source));
	TestEqual(TEXT("仅有一个有效锚点"), Registry->GetSources().Num(), 1);
	APlayerController* Viewer = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("创建普通对象的观察者"), Viewer)) return false;
	Source->StaticInfo.Title = FText::FromString(TEXT("普通场景告示牌"));
	FCatWorldInfoViewData Data;
	TestEqual(TEXT("非交互 Actor 默认不因靠近而显示"), Source->EvaluateDisplay(Viewer, false, 100.0), ECatWorldInfoDetail::Hidden);
	TestEqual(TEXT("非交互 Actor 准心命中时显示详情"), Source->EvaluateDisplay(Viewer, true, 100.0), ECatWorldInfoDetail::Full);
	TestTrue(TEXT("非交互 Actor 可以提供信息"), Source->BuildInfo(Viewer, ECatWorldInfoDetail::Full, Data));
	TestEqual(TEXT("信息读取不依赖交互提示"), Data.Title.ToString(), FString(TEXT("普通场景告示牌")));
	Registry->RegisterSource(Source);
	Registry->RegisterSource(nullptr);
	TestEqual(TEXT("重复登记和空来源不增加目录项"), Registry->GetSources().Num(), 1);

	FTestWorldWrapper OtherWrapper;
	if (!TestTrue(TEXT("创建隔离世界"), OtherWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UCatWorldInfoRegistry* OtherRegistry = OtherWrapper.GetTestWorld()->GetSubsystem<UCatWorldInfoRegistry>();
	if (!TestNotNull(TEXT("隔离世界拥有独立注册表"), OtherRegistry)) return false;
	OtherRegistry->RegisterSource(Source);
	TestEqual(TEXT("注册表拒绝其他世界组件"), OtherRegistry->GetSources().Num(), 0);
	Registry->UnregisterSource(nullptr);
	TestTrue(TEXT("注销空来源不影响有效锚点"), Registry->GetSources().Contains(Source));
	Registry->UnregisterSource(Source);
	Registry->UnregisterSource(Source);
	TestEqual(TEXT("重复注销保持空目录"), Registry->GetSources().Num(), 0);
	Registry->RegisterSource(Source);
	TestEqual(TEXT("注销后可重新登记同一组件"), Registry->GetSources().Num(), 1);
	Source->DestroyComponent();
	TestEqual(TEXT("组件 EndPlay 自动注销"), Registry->GetSources().Num(), 0);
	TestTrue(TEXT("仅销毁组件不销毁普通宿主"), IsValid(Owner));

	UCatWorldInfoComponent* Replacement = NewObject<UCatWorldInfoComponent>(Owner);
	Owner->AddInstanceComponent(Replacement);
	Owner->SetRootComponent(Replacement);
	Replacement->RegisterComponent();
	TestTrue(TEXT("游戏中追加组件也自动登记"), Registry->GetSources().Contains(Replacement));
	TestTrue(TEXT("销毁普通宿主成功"), Owner->Destroy());
	TestEqual(TEXT("宿主退出不留下信息锚点"), Registry->GetSources().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWorldInfoDisplayPoliciesTest,
	"Catfishing.UI.WorldInfo.DisplayPolicies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 先核对默认只在准心命中时显示，再为真实观察者逐个选择三种公开策略，以明确的内侧、等距、外侧样例验证层级，不重算生产公式。
// 每种策略再检查焦点不能越界、总开关和无观察者拒绝，以及非有限距离和非法配置；所有设置只写临时组件。
bool FCatWorldInfoDisplayPoliciesTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建策略测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	APlayerController* Viewer = WorldWrapper.GetTestWorld()->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("创建策略观察者"), Viewer)) return false;
	UCatWorldInfoComponent* Source = NewObject<UCatWorldInfoComponent>();
	TestEqual(TEXT("默认策略仅准心命中显示"), Source->DisplayPolicy, ECatWorldInfoPolicy::FocusOnly);
	TestEqual(TEXT("默认策略不因靠近而显示"), Source->EvaluateDisplay(Viewer, false, 100.0), ECatWorldInfoDetail::Hidden);
	TestEqual(TEXT("默认策略在准心命中时显示详情"), Source->EvaluateDisplay(Viewer, true, 100.0), ECatWorldInfoDetail::Full);
	TestEqual(TEXT("默认阅读距离为 500 厘米"), Source->DisplayDistanceCentimeters, 500.0f);
	TestTrue(TEXT("默认允许阅读"), Source->bInfoEnabled);
	const ECatWorldInfoPolicy Policies[] = {ECatWorldInfoPolicy::NearbyFull, ECatWorldInfoPolicy::NearbySummary, ECatWorldInfoPolicy::FocusOnly};
	const ECatWorldInfoDetail Unfocused[] = {ECatWorldInfoDetail::Full, ECatWorldInfoDetail::Summary, ECatWorldInfoDetail::Hidden};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Policies); ++Index)
	{
		Source->DisplayPolicy = Policies[Index];
		Source->DisplayDistanceCentimeters = 300.0f;
		const FString Prefix = FString::Printf(TEXT("策略 %d："), Index);
		for (const double Distance : {0.0, 299.0, 300.0})
		{
			const FString Sample = FString::Printf(TEXT("%.0f 厘米"), Distance);
			TestEqual(Prefix + Sample + TEXT("未聚焦层级"), Source->EvaluateDisplay(Viewer, false, Distance), Unfocused[Index]);
			TestEqual(Prefix + Sample + TEXT("聚焦完整显示"), Source->EvaluateDisplay(Viewer, true, Distance), ECatWorldInfoDetail::Full);
		}
		TestEqual(Prefix + TEXT("外侧未聚焦隐藏"), Source->EvaluateDisplay(Viewer, false, 301.0), ECatWorldInfoDetail::Hidden);
		TestEqual(Prefix + TEXT("焦点不能越过距离上限"), Source->EvaluateDisplay(Viewer, true, 301.0), ECatWorldInfoDetail::Hidden);
		TestEqual(Prefix + TEXT("没有观察者隐藏"), Source->EvaluateDisplay(nullptr, true, 100.0), ECatWorldInfoDetail::Hidden);
		Source->bInfoEnabled = false;
		TestEqual(Prefix + TEXT("总开关优先于焦点"), Source->EvaluateDisplay(Viewer, true, 100.0), ECatWorldInfoDetail::Hidden);
		Source->bInfoEnabled = true;
		for (const double Distance : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
		{
			TestEqual(Prefix + TEXT("非有限观察距离隐藏"), Source->EvaluateDisplay(Viewer, true, Distance), ECatWorldInfoDetail::Hidden);
		}
		for (const float Limit : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		{
			Source->DisplayDistanceCentimeters = Limit;
			TestEqual(Prefix + TEXT("非法阅读半径隐藏"), Source->EvaluateDisplay(Viewer, true, 0.0), ECatWorldInfoDetail::Hidden);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWorldInfoStaticSnapshotTest,
	"Catfishing.UI.WorldInfo.StaticSnapshot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 配置普通组件的标题、状态和两种信息行，分别读取摘要与详情，确认 BuildInfo 保留完整只读快照供 WBP 筛选。
// 修改返回值后重新读取，检查没有反写来源或触发通知；再切换静态内容与失败输入，确认旧输出被覆盖、失败不改源数据。
bool FCatWorldInfoStaticSnapshotTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建快照测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	APlayerController* Viewer = WorldWrapper.GetTestWorld()->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("创建快照观察者"), Viewer)) return false;
	UCatWorldInfoComponent* Source = NewObject<UCatWorldInfoComponent>();
	Source->StaticInfo.Title = FText::FromString(TEXT("湖边告示牌"));
	Source->StaticInfo.Status = FText::FromString(TEXT("仅供阅读"));
	FCatWorldInfoRow Summary;
	Summary.Id = TEXT("WaterTemperature");
	Summary.Label = FText::FromString(TEXT("水温"));
	Summary.Value = FText::FromString(TEXT("18 摄氏度"));
	Summary.Icon = NewObject<UTexture2D>();
	Summary.Progress = 0.25f;
	FCatWorldInfoRow Detail;
	Detail.Id = TEXT("Notice");
	Detail.Label = FText::FromString(TEXT("说明"));
	Detail.Value = FText::FromString(TEXT("夜间保持安静"));
	Detail.bSummary = false;
	Source->StaticInfo.Rows = {Summary, Detail};
	const uint32 OriginalSerial = Source->GetInfoSerial();
	FCatWorldInfoViewData Data;
	for (const ECatWorldInfoDetail Level : {ECatWorldInfoDetail::Summary, ECatWorldInfoDetail::Full})
	{
		TestTrue(TEXT("配置完整的静态对象可读取"), Source->BuildInfo(Viewer, Level, Data));
		TestEqual(TEXT("标题来自静态配置"), Data.Title.ToString(), FString(TEXT("湖边告示牌")));
		TestEqual(TEXT("状态原样保留"), Data.Status.ToString(), FString(TEXT("仅供阅读")));
		if (!TestEqual(TEXT("快照保留两行，摘要筛选归 View"), Data.Rows.Num(), 2)) return false;
		TestEqual(TEXT("首行稳定语义键"), Data.Rows[0].Id, Summary.Id);
		TestEqual(TEXT("概念名称不改写"), Data.Rows[0].Label.ToString(), Summary.Label.ToString());
		TestEqual(TEXT("值和单位不重新格式化"), Data.Rows[0].Value.ToString(), Summary.Value.ToString());
		TestTrue(TEXT("图标仍引用原资源"), Data.Rows[0].Icon == Summary.Icon);
		TestEqual(TEXT("进度保留归一化值"), Data.Rows[0].Progress, 0.25f);
		TestTrue(TEXT("重点行保留摘要资格"), Data.Rows[0].bSummary);
		TestEqual(TEXT("详情行顺序和语义键保留"), Data.Rows[1].Id, Detail.Id);
		TestFalse(TEXT("详情行不被误标为摘要"), Data.Rows[1].bSummary);
	}
	Data.Title = FText::FromString(TEXT("调用方自己的标题"));
	Data.Rows[0].Value = FText::FromString(TEXT("调用方自己的值"));
	Data.Rows.Reset();
	TestTrue(TEXT("修改返回值后仍可读取来源"), Source->BuildInfo(Viewer, ECatWorldInfoDetail::Full, Data));
	TestEqual(TEXT("快照修改未反写源标题"), Data.Title.ToString(), FString(TEXT("湖边告示牌")));
	if (!TestEqual(TEXT("快照删行未反写源数组"), Data.Rows.Num(), 2)) return false;
	TestEqual(TEXT("快照行修改未反写源值"), Data.Rows[0].Value.ToString(), Summary.Value.ToString());
	TestEqual(TEXT("读取和修改副本不触发内容通知"), Source->GetInfoSerial(), OriginalSerial);
	Source->NotifyInfoChanged();
	TestTrue(TEXT("显式通知改变可观察序号"), Source->GetInfoSerial() != OriginalSerial);
	const uint32 NotifiedSerial = Source->GetInfoSerial();
	TestFalse(TEXT("隐藏层级不提供有效展示"), Source->BuildInfo(Viewer, ECatWorldInfoDetail::Hidden, Data));
	TestFalse(TEXT("缺观察者不提供有效展示"), Source->BuildInfo(nullptr, ECatWorldInfoDetail::Full, Data));
	TestEqual(TEXT("拒绝读取不触发额外通知"), Source->GetInfoSerial(), NotifiedSerial);
	TestEqual(TEXT("拒绝读取不改变静态来源"), Source->StaticInfo.Title.ToString(), FString(TEXT("湖边告示牌")));
	Source->StaticInfo = FCatWorldInfoViewData();
	TestFalse(TEXT("空标题表示未配置"), Source->BuildInfo(Viewer, ECatWorldInfoDetail::Full, Data));
	TestTrue(TEXT("空配置覆盖上一份标题状态和行"), Data.Title.IsEmpty() && Data.Status.IsEmpty() && Data.Rows.IsEmpty());
	Source->StaticInfo.Title = FText::FromString(TEXT("另一块告示牌"));
	TestTrue(TEXT("只有标题也可作为信息源"), Source->BuildInfo(Viewer, ECatWorldInfoDetail::Summary, Data));
	TestTrue(TEXT("换对象不残留旧状态与详情"), Data.Status.IsEmpty() && Data.Rows.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatWorldInfoFormalWidgetContractTest,
	"Catfishing.UI.WorldInfo.FormalWidgetContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 1. 沿 Tooltip 的内存世界和 LocalPlayer 模式加载磁盘正式 WBP，先检查真实原生绑定和摘要/详情页面归属，缺失时立即失败。
// 2. 用 RenderInfo 驱动两种版面，检查摘要筛选、完整行顺序及替换数据后的清空，不调用资产生成器或构造替代 WidgetTree。
// 3. 手动推进正式 ListView 的 Slate 布局以生成真实行，通过列表重建覆盖有图有进度、缺图缺进度和非法进度的绑定结果。
// 这里只证明加载、绑定和布局结构契约，不生成截图，也不作为屏幕可读性、双端 PIE 或打包 UI 交付证据。
bool FCatWorldInfoFormalWidgetContractTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建正式 WBP 测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!TestTrue(TEXT("初始化独立 UI 测试世界"), WorldWrapper.BeginPlayInTestWorld())) return false;
	APlayerController* Viewer = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("创建 UI 所属玩家"), Viewer)) return false;
	Viewer->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	if (!TestTrue(TEXT("原生初始化所需玩家上下文有效"), FLocalPlayerContext(Viewer).IsValid())) return false;
	UClass* PanelClass = GetDefault<UCatWorldInfoComponent>()->WidgetClass.LoadSynchronous();
	UClass* RowClass = LoadClass<UCatWorldInfoRowWidget>(nullptr, TEXT("/Game/UI/WorldInfo/WBP_CatWorldInfoRow.WBP_CatWorldInfoRow_C"));
	if (!TestTrue(TEXT("两个正式 WBP 类加载成功且父类正确"), PanelClass && RowClass && PanelClass->IsChildOf(UCatWorldInfoWidget::StaticClass()))) return false;
	TestEqual(TEXT("组件默认接入正式信息牌"), PanelClass->GetPathName(), FString(TEXT("/Game/UI/WorldInfo/WBP_CatWorldInfo.WBP_CatWorldInfo_C")));
	TestEqual(TEXT("行类解析到正式路径"), RowClass->GetPathName(), FString(TEXT("/Game/UI/WorldInfo/WBP_CatWorldInfoRow.WBP_CatWorldInfoRow_C")));
	if (!TestTrue(TEXT("禁止原生类替代正式 WBP"), PanelClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) && RowClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint))) return false;
	UCatWorldInfoWidget* View = CreateWidget<UCatWorldInfoWidget>(Viewer, PanelClass);
	UCatWorldInfoRowWidget* RowProbe = CreateWidget<UCatWorldInfoRowWidget>(Viewer, RowClass);
	if (!TestTrue(TEXT("正式信息牌和行可以实例化"), View && RowProbe)) return false;
	for (UUserWidget* Widget : {static_cast<UUserWidget*>(View), static_cast<UUserWidget*>(RowProbe)})
	{
		if (!TestTrue(TEXT("正式资产拥有可达根控件"), Widget->WidgetTree && Widget->WidgetTree->RootWidget)) return false;
		TArray<UWidget*> Reachable;
		Widget->WidgetTree->GetAllWidgets(Reachable);
		// 在触发绑定回调之前核实原生指针，避免同名 Designer 控件存在、BindWidget 却为空时让测试崩溃。
		for (TFieldIterator<FObjectPropertyBase> Property(Widget->GetClass()); Property; ++Property)
		{
			if (!Property->HasMetaData(TEXT("BindWidget"))) continue;
			UWidget* Child = Widget->GetWidgetFromName(Property->GetFName());
			if (!TestTrue(Widget->GetClass()->GetName() + TEXT(" 绑定 ") + Property->GetName(),
				Child && Child->IsA(Property->PropertyClass) && Reachable.Contains(Child)
				&& Property->GetObjectPropertyValue_InContainer(Widget) == Child)) return false;
		}
		TestFalse(TEXT("只读 WBP 不接管焦点"), Widget->IsFocusable());
		TestEqual(TEXT("只读 WBP 不参与命中"), Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	}
	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(View->GetWidgetFromName(TEXT("DetailSwitcher")));
	UListView* SummaryList = Cast<UListView>(View->GetWidgetFromName(TEXT("SummaryInfoList")));
	UListView* FullList = Cast<UListView>(View->GetWidgetFromName(TEXT("InfoList")));
	if (!TestTrue(TEXT("正式两页和列表存在"), Switcher && SummaryList && FullList)) return false;
	if (!TestEqual(TEXT("版面只有摘要和详情两页"), Switcher->GetChildrenCount(), 2)) return false;
	for (int32 Page = 0; Page < 2; ++Page)
	{
		const TArray<FName> Names = Page == 0
			? TArray<FName>{TEXT("SummaryTitleText"), TEXT("SummaryStatusText"), TEXT("SummaryInfoList")}
			: TArray<FName>{TEXT("TitleText"), TEXT("StatusText"), TEXT("InfoList")};
		for (const FName Name : Names)
		{
			UWidget* Child = View->GetWidgetFromName(Name);
			while (Child && Child->GetParent() != Switcher) Child = Child->GetParent();
			TestTrue(Name.ToString() + TEXT("位于约定页面"), Child && Child == Switcher->GetChildAt(Page));
		}
		UListView* List = Page == 0 ? SummaryList : FullList;
		if (!TestTrue(TEXT("两页均引用正式行模板"), List->GetEntryWidgetClass().Get() == RowClass)) return false;
		TestEqual(TEXT("信息列表不提供选择操作"), List->GetSelectionMode(), ESelectionMode::None);
		TestEqual(TEXT("信息列表不拦截输入"), List->GetVisibility(), ESlateVisibility::HitTestInvisible);
	}
	USizeBox* PanelBounds = Cast<USizeBox>(View->WidgetTree->RootWidget);
	USizeBox* RowBounds = Cast<USizeBox>(RowProbe->WidgetTree->RootWidget);
	if (!TestTrue(TEXT("正式面板和行由资产约束尺寸"), PanelBounds && RowBounds)) return false;
	TestTrue(TEXT("行宽不超过信息牌宽度"), RowBounds->GetWidthOverride() > 0 && RowBounds->GetWidthOverride() <= PanelBounds->GetWidthOverride());
	TestTrue(TEXT("行高有稳定正值"), RowBounds->GetHeightOverride() > 0);

	FCatWorldInfoViewData Data;
	Data.Title = FText::FromString(TEXT("只读告示牌"));
	Data.Status = FText::FromString(TEXT("当前开放"));
	FCatWorldInfoRow Summary;
	Summary.Id = TEXT("Capacity");
	Summary.Label = FText::FromString(TEXT("容量"));
	Summary.Value = FText::FromString(TEXT("1 / 4"));
	Summary.Icon = NewObject<UTexture2D>();
	Summary.Progress = 0.25f;
	FCatWorldInfoRow Detail;
	Detail.Id = TEXT("Description");
	Detail.Label = FText::FromString(TEXT("说明"));
	Detail.Value = FText::FromString(TEXT("请保持整洁"));
	Detail.bSummary = false;
	Data.Rows = {Summary, Detail};
	View->TakeWidget();
	View->RenderInfo(Data, ECatWorldInfoDetail::Summary);
	TestEqual(TEXT("摘要选择页面零"), Switcher->GetActiveWidgetIndex(), 0);
	if (!TestEqual(TEXT("摘要只提交重点行"), SummaryList->GetNumItems(), 1)) return false;
	UCatWorldInfoListItem* SummaryItem = Cast<UCatWorldInfoListItem>(SummaryList->GetItemAt(0));
	if (!TestNotNull(TEXT("摘要使用正式只读行载荷"), SummaryItem)) return false;
	TestEqual(TEXT("摘要保留指定行"), SummaryItem->Data.Id, Summary.Id);
	View->RenderInfo(Data, ECatWorldInfoDetail::Full);
	TestEqual(TEXT("详情选择页面一"), Switcher->GetActiveWidgetIndex(), 1);
	if (!TestEqual(TEXT("详情包含所有行"), FullList->GetNumItems(), 2)) return false;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UCatWorldInfoListItem* Item = Cast<UCatWorldInfoListItem>(FullList->GetItemAt(Index));
		if (!TestNotNull(TEXT("详情使用正式只读行载荷"), Item)) return false;
		TestEqual(TEXT("详情顺序服从数据提供者"), Item->Data.Id, Data.Rows[Index].Id);
	}

	// 摘要只有一个条目，重建时可确定复用同一行；由公开 Slate Tick 触发 UE 的真实绑定，不直接调用受保护回调。
	View->RenderInfo(Data, ECatWorldInfoDetail::Summary);
	TSharedRef<SWidget> SlateList = SummaryList->TakeWidget();
	const FGeometry ListGeometry = FGeometry::MakeRoot(FVector2D(296.0, 72.0), FSlateLayoutTransform());
	SlateList->SlatePrepass(1.0f);
	SlateList->Tick(ListGeometry, 0.0, 0.016f);
	UCatWorldInfoListItem* BoundItem = Cast<UCatWorldInfoListItem>(SummaryList->GetItemAt(0));
	if (!TestNotNull(TEXT("取得待绑定载荷"), BoundItem)) return false;
	UCatWorldInfoRowWidget* OriginalRow = SummaryList->GetEntryWidgetFromItem<UCatWorldInfoRowWidget>(BoundItem);
	if (!TestNotNull(TEXT("摘要列表首次生成正式行"), OriginalRow)) return false;
	const float ProgressCases[] = {0.25f, -1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.0f};
	const float ExpectedPercent[] = {0.25f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
	const ESlateVisibility ExpectedVisibility[] = {ESlateVisibility::HitTestInvisible, ESlateVisibility::Collapsed,
		ESlateVisibility::HitTestInvisible, ESlateVisibility::Collapsed, ESlateVisibility::Collapsed, ESlateVisibility::HitTestInvisible};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ProgressCases); ++Index)
	{
		BoundItem->Data.Progress = ProgressCases[Index];
		BoundItem->Data.Icon = Index == 0 ? Summary.Icon.Get() : nullptr;
		BoundItem->Data.Label = FText::FromString(FString::Printf(TEXT("字段 %d"), Index));
		BoundItem->Data.Value = FText::FromString(FString::Printf(TEXT("值 %d"), Index));
		SummaryList->RegenerateAllEntries();
		SlateList->SlatePrepass(1.0f);
		SlateList->Tick(ListGeometry, (Index + 1) * 0.016, 0.016f);
		UCatWorldInfoRowWidget* Row = SummaryList->GetEntryWidgetFromItem<UCatWorldInfoRowWidget>(BoundItem);
		if (!TestNotNull(TEXT("真实 ListView 生成正式行实例"), Row)) return false;
		TestTrue(TEXT("同一正式行经历有值到缺失的复用"), Row == OriginalRow);
		TestTrue(TEXT("生成行没有使用替代类"), Row->GetClass() == RowClass);
		UTextBlock* Label = Cast<UTextBlock>(Row->GetWidgetFromName(TEXT("LabelText")));
		UTextBlock* Value = Cast<UTextBlock>(Row->GetWidgetFromName(TEXT("ValueText")));
		UImage* Icon = Cast<UImage>(Row->GetWidgetFromName(TEXT("RowIcon")));
		UProgressBar* Progress = Cast<UProgressBar>(Row->GetWidgetFromName(TEXT("RowProgress")));
		if (!TestTrue(TEXT("正式行四个显示字段齐全"), Label && Value && Icon && Progress)) return false;
		TestEqual(TEXT("重新绑定覆盖旧概念名称"), Label->GetText().ToString(), BoundItem->Data.Label.ToString());
		TestEqual(TEXT("重新绑定覆盖旧值"), Value->GetText().ToString(), BoundItem->Data.Value.ToString());
		TestTrue(TEXT("图标资源随本次载荷覆盖或清空"), Icon->GetBrush().GetResourceObject() == BoundItem->Data.Icon.Get());
		TestEqual(TEXT("空图标收起"), Icon->GetVisibility(), Index == 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		TestEqual(TEXT("进度显示或收起符合公开行语义"), Progress->GetVisibility(), ExpectedVisibility[Index]);
		TestEqual(TEXT("进度限制在可显示区间，缺失不伪造进度"), Progress->GetPercent(), ExpectedPercent[Index]);
	}
	TestEqual(TEXT("列表载荷修改不反写提交快照"), Data.Rows[0].Progress, 0.25f);
	for (const ECatWorldInfoDetail Level : {ECatWorldInfoDetail::Summary, ECatWorldInfoDetail::Full})
	{
		const bool bFull = Level == ECatWorldInfoDetail::Full;
		UTextBlock* Title = Cast<UTextBlock>(View->GetWidgetFromName(bFull ? TEXT("TitleText") : TEXT("SummaryTitleText")));
		UTextBlock* Status = Cast<UTextBlock>(View->GetWidgetFromName(bFull ? TEXT("StatusText") : TEXT("SummaryStatusText")));
		if (!TestTrue(TEXT("页面文字控件存在"), Title && Status)) return false;
		View->RenderInfo(Data, Level);
		TestEqual(TEXT("当前页面标题来自快照"), Title->GetText().ToString(), Data.Title.ToString());
		TestEqual(TEXT("当前页面状态来自快照"), Status->GetText().ToString(), Data.Status.ToString());
		TestEqual(TEXT("有状态说明时可读"), Status->GetVisibility(), ESlateVisibility::HitTestInvisible);
		FCatWorldInfoViewData Replacement;
		Replacement.Title = FText::FromString(TEXT("下一块告示牌"));
		View->RenderInfo(Replacement, Level);
		TestEqual(TEXT("切换对象覆盖旧标题"), Title->GetText().ToString(), Replacement.Title.ToString());
		TestTrue(TEXT("切换对象清空旧状态文本"), Status->GetText().IsEmpty());
		TestEqual(TEXT("无状态收起说明"), Status->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("切换对象清空当前页面旧行"), (bFull ? FullList : SummaryList)->GetNumItems(), 0);
	}
	return true;
}

#endif
