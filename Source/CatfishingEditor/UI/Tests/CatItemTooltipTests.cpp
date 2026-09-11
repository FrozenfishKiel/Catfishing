#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Data/CatFishDefinition.h"
#include "Engine/Texture2D.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "AssetCompilingManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Slate/SObjectWidget.h"
#include "Slate/WidgetRenderer.h"
#include "UI/CatUISettings.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/ItemTooltip/CatItemTooltipController.h"
#include "UI/ItemTooltip/CatItemTooltipModel.h"
#include "UI/ItemTooltip/CatItemTooltipWidget.h"

namespace CatItemTooltipTests
{
	// 通过现有装备定义和实例初始化路径建立可磨损鱼竿，不伪造 Tooltip 文本；用它验证耐久变化不依赖库存列表通知。
	UCatEquipmentInventoryItemInstance* CreateRod()
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>();
		Definition->EquipmentDefinitionId = TEXT("TooltipRod");
		Definition->FunctionalRouteId = TEXT("TooltipRod");
		Definition->bEnableRuntimeDefinition = true;
		Definition->DisplayName = FText::FromString(TEXT("测试鱼竿"));
		Definition->Description = FText::FromString(TEXT("耐久随同一件鱼竿保存。"));
		Definition->LoadoutSlotId = UCatEquipmentDefinition::FishingRodLoadoutSlotId();
		Definition->UseActorClass = ACatFishingRodActor::StaticClass();
		UCatEquipmentFragment_Rod* Fragment = NewObject<UCatEquipmentFragment_Rod>(Definition);
		Fragment->MaximumRodDurability = 100.0;
		Fragment->MaximumLineLengthCentimeters = 1500.0;
		Fragment->RodPhysicsLengthCentimeters = 200.0;
		Fragment->HighTensionWearMultiplier = 1.0;
		Definition->Fragments.Add(Fragment);
		UCatEquipmentInventoryItemInstance* Rod = NewObject<UCatEquipmentInventoryItemInstance>();
		Rod->SetItemDefinition(Definition);
		Rod->SetItemInstanceIdFromAuthority(FGuid::NewGuid());
		Rod->SetRodRuntimeStateFromAuthority(72.5, false);
		return Rod;
	}

	// 创建具有真实独立重量的鱼实例；名称和说明从鱼定义的覆盖接口读取，捕获身份只服务初始化且不展示。
	UCatFishInventoryItemInstance* CreateFish()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>();
		Definition->DisplayName = FText::FromString(TEXT("银月鳟"));
		Definition->Description = FText::FromString(TEXT("生活在湖中的鱼。\n这条鱼的重量来自当前实例。"));
		UCatFishInventoryItemInstance* Fish = NewObject<UCatFishInventoryItemInstance>();
		Fish->SetItemDefinition(Definition);
		Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("TooltipTestOwner"), 3.125);
		return Fish;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatItemTooltipInstanceProjectionTest,
	"Catfishing.UI.ItemTooltip.InstanceProjection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 从真实实例读取投影，验证同种不同重量、耐久变化及断裂；再切到普通物品和空定义，检查上一物品字段全部清除。
bool FCatItemTooltipInstanceProjectionTest::RunTest(const FString& Parameters)
{
	UCatItemTooltipModel* Model = NewObject<UCatItemTooltipModel>();
	FCatItemTooltipViewData Data;
	UCatFishInventoryItemInstance* Fish = CatItemTooltipTests::CreateFish();
	TestTrue(TEXT("鱼实例可投影"), Model->BuildViewData(Fish, Data));
	TestEqual(TEXT("通过鱼定义覆盖读取名称"), Data.Name.ToString(), FString(TEXT("银月鳟")));
	TestTrue(TEXT("重量保留两位小数和 kg"), Data.InstanceDetails.ToString().Contains(TEXT("3.12 kg")));
	Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("TooltipTestOwner"), 8.5);
	Model->BuildViewData(Fish, Data);
	TestTrue(TEXT("同定义读取当前实例重量"), Data.InstanceDetails.ToString().Contains(TEXT("8.50 kg")));
	TestFalse(TEXT("不显示捕获身份"), Data.InstanceDetails.ToString().Contains(TEXT("TooltipTestOwner")));

	UCatEquipmentInventoryItemInstance* Rod = CatItemTooltipTests::CreateRod();
	TestTrue(TEXT("真实装备实例初始耐久就绪"), FMath::IsNearlyEqual(Rod->GetRodDurability(), 72.5));
	Model->BuildViewData(Rod, Data);
	TestTrue(TEXT("耐久显示当前和上限"), Data.InstanceDetails.ToString().Contains(TEXT("72.5 / 100")));
	Rod->SetRodRuntimeStateFromAuthority(0.0, true);
	Model->BuildViewData(Rod, Data);
	TestTrue(TEXT("同实例更新为断裂"), Data.InstanceDetails.ToString().Contains(TEXT("已断裂")));

	UCatInventoryItemDefinition* OrdinaryDefinition = NewObject<UCatInventoryItemDefinition>();
	UCatInventoryItemInstance* Ordinary = NewObject<UCatInventoryItemInstance>();
	Ordinary->SetItemDefinition(OrdinaryDefinition);
	TestTrue(TEXT("普通物品仍有基本信息"), Model->BuildViewData(Ordinary, Data));
	TestTrue(TEXT("普通物品不残留鱼竿详情"), Data.InstanceDetails.IsEmpty());
	TestNull(TEXT("缺图保持空"), Data.Icon.Get());
	TestFalse(TEXT("空实例无有效提示"), Model->BuildViewData(nullptr, Data));
	TestTrue(TEXT("无效来源清除名称"), Data.Name.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatItemTooltipWidgetLifecycleTest,
	"Catfishing.UI.ItemTooltip.FormalWidgetLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 加载正式迁移 WBP，经 Controller 驱动原生 Slate 事件与动画：覆盖快速换格、迟到关闭、独立耐久更新、缺图和解绑。
// 最后将真实 WBP 离屏渲染到 PNG，提供布局检查证据；此用例不冒充房主/客户端网络或完整游戏鼠标验证。
bool FCatItemTooltipWidgetLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 UI 测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = WorldWrapper.GetTestWorld();
	// UE 只在有效 PlayerContext 下调用 NativeOnInitialized；先初始化世界，使 Controller 注册到世界玩家表，再按 Engine 的 Within 约束创建 LocalPlayer。
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	if (!TestTrue(TEXT("初始化独立 UI 测试世界"), WorldWrapper.BeginPlayInTestWorld())) return false;
	APlayerController* Player = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("创建提示框所属玩家"), Player)) return false;
	Player->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	if (!TestTrue(TEXT("提示框玩家上下文有效"), FLocalPlayerContext(Player).IsValid())) return false;
	const TSubclassOf<UCatItemTooltipWidget> ViewClass = GetDefault<UCatUISettings>()->LoadItemTooltipWidgetClass();
	if (!TestNotNull(TEXT("正式 WBP 已迁移且父类正确"), ViewClass.Get())) return false;
	// 迁移图片首次加载可能仍在异步编译；等资源就绪再取 Slate，避免把首帧占位纹理当成正式外观。
	FAssetCompilingManager::Get().FinishAllCompilation();
	UCatItemTooltipWidget* View = CreateWidget<UCatItemTooltipWidget>(Player, ViewClass);
	if (!TestNotNull(TEXT("创建正式提示 View"), View)) return false;
	TestEqual(TEXT("正式初始化保持透明"), View->GetRenderOpacity(), 0.0f);
	TestEqual(TEXT("正式初始化保持收起"), View->GetVisibility(), ESlateVisibility::Collapsed);
	TSharedRef<SWidget> SlateView = View->TakeWidget();
	UTextBlock* Name = Cast<UTextBlock>(View->GetWidgetFromName(TEXT("ItemNameText")));
	UTextBlock* Details = Cast<UTextBlock>(View->GetWidgetFromName(TEXT("InstanceDetailsText")));
	UImage* Icon = Cast<UImage>(View->GetWidgetFromName(TEXT("ItemIconImage")));
	if (!TestTrue(TEXT("正式控件绑定完整"), Name && Details && Icon && View->GetWidgetFromName(TEXT("RootBorder")))) return false;
	UCatItemTooltipController* Controller = NewObject<UCatItemTooltipController>();
	Controller->Bind(View);
	UCatInventorySlotWidget* FishSlot = CreateWidget<UCatInventorySlotWidget>(Player);
	UCatInventorySlotWidget* RodSlot = CreateWidget<UCatInventorySlotWidget>(Player);
	if (!TestTrue(TEXT("创建两个实际库存格子"), FishSlot && RodSlot)) return false;
	FCatInventoryEntry Entry;
	Entry.Instance = CatItemTooltipTests::CreateFish();
	Entry.StackCount = 1;
	FishSlot->SetSlotContext(0, nullptr, Entry);
	UCatEquipmentInventoryItemInstance* Rod = CatItemTooltipTests::CreateRod();
	Entry.Instance = Rod;
	RodSlot->SetSlotContext(1, nullptr, Entry);
	TSharedRef<SWidget> SlateFish = FishSlot->TakeWidget();
	TSharedRef<SWidget> SlateRod = RodSlot->TakeWidget();
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(1280.0, 720.0), FSlateLayoutTransform());
	const FPointerEvent Pointer;
	SlateFish->OnMouseEnter(Geometry, Pointer);
	Controller->ShowTooltip(FishSlot, FVector2D(100.0, 100.0));
	SlateView->Tick(Geometry, 0.0, 0.1f);
	TestEqual(TEXT("0.1 秒淡入至半透明"), View->GetRenderOpacity(), 0.5f);
	Controller->HideTooltip(FishSlot);
	SlateView->Tick(Geometry, 0.1, 0.05f);
	TestEqual(TEXT("离开从当前透明度淡出"), View->GetRenderOpacity(), 0.25f);
	SlateRod->OnMouseEnter(Geometry, Pointer);
	Controller->ShowTooltip(RodSlot, FVector2D(200.0, 200.0));
	Controller->HideTooltip(FishSlot);
	SlateView->Tick(Geometry, 0.15, 0.1f);
	TestEqual(TEXT("新悬停反向续接且忽略旧格关闭"), View->GetRenderOpacity(), 0.75f);
	TestEqual(TEXT("提示已切换鱼竿"), Name->GetText().ToString(), FString(TEXT("测试鱼竿")));
	TestEqual(TEXT("提示不拦截鼠标"), View->GetVisibility(), ESlateVisibility::HitTestInvisible);
	Rod->SetRodRuntimeStateFromAuthority(37.25, false);
	Controller->Tick(0.016f);
	TestTrue(TEXT("无列表通知仍刷新实例耐久"), Details->GetText().ToString().Contains(TEXT("37.25 / 100")));
	TestEqual(TEXT("内容刷新不重置透明度"), View->GetRenderOpacity(), 0.75f);
	TestEqual(TEXT("无图物品收起图标"), Icon->GetVisibility(), ESlateVisibility::Collapsed);
	Entry = FCatInventoryEntry();
	RodSlot->SetSlotContext(1, nullptr, Entry);
	Controller->Tick(0.016f);
	TestEqual(TEXT("活动格变空立即清理旧详情"), View->GetVisibility(), ESlateVisibility::Collapsed);
	Controller->ShowTooltip(FishSlot, FVector2D::ZeroVector);
	Controller->ForceHideTooltip();
	TestEqual(TEXT("关页立即收起"), View->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("关页停止实例读取"), Controller->IsTickable());

	Controller->ShowTooltip(FishSlot, FVector2D::ZeroVector);
	SlateView->Tick(Geometry, 1.0, 0.2f);
	if (FApp::CanEverRender())
	{
		FWidgetRenderer Renderer(true);
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(SlateView, FVector2D(1280.0, 720.0));
		FBufferArchive PNG;
		if (TestNotNull(TEXT("正式 WBP 渲染目标"), Target)
			&& TestTrue(TEXT("导出正式 WBP 图片"), FImageUtils::ExportRenderTarget2DAsPNG(Target, PNG)))
		{
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/Tooltip");
			IFileManager::Get().MakeDirectory(*Directory, true);
			TestTrue(TEXT("保存视觉核查图片"), FFileHelper::SaveArrayToFile(PNG, *(Directory / TEXT("FishTooltip.png"))));
		}
	}
	Controller->Unbind();
	TestEqual(TEXT("解绑不保留旧画面"), View->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("解绑无活动 Tick"), Controller->IsTickable());
	return true;
}

#endif
