// 本文件会创建和保存 Editor 资产，只能在开发自动化与编辑器环境下编译，避免运行时包携带资产生成入口。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UI/HUD/CatHUDWidget.h"
#include "UI/Interaction/CatInteractionPromptWidget.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "UI/Shop/CatShopWidget.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatUIModuleWidgetAssetsCreationTest,
	"Catfishing.Editor.UIModules.CreateFormalWBPAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CatUIModuleWidgetAsset
{
	// 输入 Action 获取流程：
	// 1. 先按正式包路径加载既有资产，保证重复运行不会生成第二份 Action。
	// 2. 缺失时才在目标包里创建新资产并通知 AssetRegistry，供既有 IMC 持久引用。
	// 3. 最后统一写入 Boolean 类型和中文描述，让编辑器里看到的资产语义和交互确认一致。
	UInputAction* LoadOrCreateInputAction(const FString& PackageName, const FName AssetName, const FText& Description)
	{
		UInputAction* Action = Cast<UInputAction>(StaticLoadObject(
			UInputAction::StaticClass(),
			nullptr,
			*(PackageName + TEXT(".") + AssetName.ToString()),
			nullptr,
			LOAD_NoWarn));
		UPackage* Package = Action ? Action->GetOutermost() : CreatePackage(*PackageName);
		if (!Action && Package)
		{
			Action = NewObject<UInputAction>(Package, AssetName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Action);
		}
		if (Action)
		{
			Action->Modify();
			Action->ValueType = EInputActionValueType::Boolean;
			Action->ActionDescription = Description;
			Action->MarkPackageDirty();
		}
		return Action;
	}

	// Actor 蓝图获取流程：
	// 1. 先加载既有商店摆放蓝图，重复运行时只刷新父类关系，不复制新资产。
	// 2. 缺失时创建继承商店 Kiosk C++ Actor 的蓝图，给关卡一个可直接摆放的 ShopEconomy 交互对象。
	// 3. 这里不修改任何关卡；摆放位置仍由地图或后续编辑器步骤决定。
	UBlueprint* LoadOrCreateActorBlueprint(const FString& PackageName, const FName AssetName, UClass* ParentClass)
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *(PackageName + TEXT(".") + AssetName.ToString()));
		if (Blueprint)
		{
			Blueprint->Modify();
			Blueprint->ParentClass = ParentClass;
			return Blueprint;
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			AssetName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
		if (Blueprint)
		{
			FAssetRegistryModule::AssetCreated(Blueprint);
		}
		return Blueprint;
	}

	// WBP 保存流程：
	// 1. 先确认传入的是有效 WidgetBlueprint，并取它所在的包作为唯一保存对象。
	// 2. 再把长包名转换为 Content 下的 uasset 文件路径并写盘。
	// 3. 保存失败会返回 false，由调用测试把具体 WBP 名称暴露出来，避免旧资产被误当成本轮结果。
	bool SaveWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint, const FString& PackageName)
	{
		if (!WidgetBlueprint)
		{
			return false;
		}
		UPackage* Package = WidgetBlueprint->GetOutermost();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return Package && UPackage::SavePackage(Package, WidgetBlueprint, *PackageFilename, SaveArgs);
	}

	// 普通资产保存流程：
	// 1. 先确认资产和外层包都有效，空资产直接失败而不是创建占位包。
	// 2. 再按正式包名写回 Content，覆盖本轮对 InputAction、IMC 或 Actor 蓝图的编辑器修改。
	// 3. 返回值只表达持久化是否成功，调用方负责把失败归因到具体资产。
	bool SaveAssetPackage(UObject* Asset, const FString& PackageName)
	{
		if (!Asset || !Asset->GetOutermost())
		{
			return false;
		}
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset, *PackageFilename, SaveArgs);
	}

	// WBP 获取流程：
	// 1. 先按正式路径加载已有 WidgetBlueprint，重复运行时只校正父类，不派生第二套页面。
	// 2. 缺失时使用 WidgetBlueprintFactory 创建继承指定 C++ View 基类的新 WBP。
	// 3. 创建成功后注册到 AssetRegistry，让后续保存和编辑器内容浏览器都能识别它。
	UWidgetBlueprint* LoadOrCreateWidgetBlueprint(const FString& PackageName, const FName AssetName, UClass* ParentClass)
	{
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(
			nullptr, *(PackageName + TEXT(".") + AssetName.ToString()));
		if (WidgetBlueprint)
		{
			WidgetBlueprint->Modify();
			WidgetBlueprint->ParentClass = ParentClass;
			return WidgetBlueprint;
		}

		UPackage* Package = CreatePackage(*PackageName);
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		if (!Package || !Factory)
		{
			return nullptr;
		}
		Factory->ParentClass = ParentClass;
		UObject* CreatedAsset = Factory->FactoryCreateNew(
			UWidgetBlueprint::StaticClass(),
			Package,
			AssetName,
			RF_Public | RF_Standalone,
			nullptr,
			GWarn);
		WidgetBlueprint = Cast<UWidgetBlueprint>(CreatedAsset);
		if (WidgetBlueprint)
		{
			FAssetRegistryModule::AssetCreated(WidgetBlueprint);
		}
		return WidgetBlueprint;
	}

	// 文本控件创建流程：
	// 1. 先确认 WBP 和父容器存在，避免半成品控件挂到空 WidgetTree。
	// 2. 再创建命名 TextBlock 并写入中文默认文案；名称必须匹配 C++ View 的 BindWidgetOptional 字段。
	// 3. 最后加入垂直容器，调用方用返回值继续做资产结构断言。
	UTextBlock* AddText(UWidgetBlueprint* WidgetBlueprint, UVerticalBox* Parent, const FName Name, const FString& Text)
	{
		if (!WidgetBlueprint || !Parent)
		{
			return nullptr;
		}
		UTextBlock* TextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		if (!TextBlock)
		{
			return nullptr;
		}
		TextBlock->SetText(FText::FromString(Text));
		Parent->AddChildToVerticalBox(TextBlock);
		return TextBlock;
	}

	// 按钮控件创建流程：
	// 1. 先确认 WBP 和按钮行容器存在，空输入直接失败，避免生成散落控件。
	// 2. 再创建命名 Button 和它内部的标签 TextBlock；按钮名负责匹配 C++ 绑定字段。
	// 3. 最后把按钮加入水平容器，交互委托仍由对应 Widget 基类在运行时绑定。
	UButton* AddButton(UWidgetBlueprint* WidgetBlueprint, UHorizontalBox* Parent, const FName Name, const FString& Label)
	{
		if (!WidgetBlueprint || !Parent)
		{
			return nullptr;
		}
		UButton* Button = WidgetBlueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*(Name.ToString() + TEXT("Text"))));
		if (!Button || !Text)
		{
			return nullptr;
		}
		Text->SetText(FText::FromString(Label));
		Button->AddChild(Text);
		Parent->AddChildToHorizontalBox(Button);
		return Button;
	}

	// 根容器重建流程：
	// 1. 先清空 WidgetTree 根、命名槽和绑定，保证本轮资产结构从同一个根重新生成。
	// 2. 再把旧 Designer 子树迁到临时包并去掉事务标记，避免重复运行时控件名被自动追加后缀。
	// 3. 最后创建新的 VerticalBox 根，后续各模块只在这棵新树下补自己的正式控件。
	UVerticalBox* ResetRootToVerticalBox(UWidgetBlueprint* WidgetBlueprint, const FName RootName)
	{
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return nullptr;
		}
		WidgetBlueprint->Modify();
		WidgetBlueprint->WidgetTree->Modify();
		TArray<UWidget*> ExistingSourceWidgets = WidgetBlueprint->GetAllSourceWidgets();
		WidgetBlueprint->WidgetTree->RootWidget = nullptr;
		WidgetBlueprint->WidgetTree->NamedSlotBindings.Reset();
		WidgetBlueprint->Bindings.Reset();
		for (UWidget* ExistingSourceWidget : ExistingSourceWidgets)
		{
			if (!ExistingSourceWidget)
			{
				continue;
			}
			ExistingSourceWidget->Modify();
			ExistingSourceWidget->Slot = nullptr;
			ExistingSourceWidget->Rename(
				nullptr,
				GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			ExistingSourceWidget->SetFlags(RF_Transient);
			ExistingSourceWidget->ClearFlags(RF_Transactional);
		}
		WidgetBlueprint->WidgetVariableNameToGuidMap.Reset();
		UVerticalBox* Root = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), RootName);
		if (!Root)
		{
			return nullptr;
		}
		WidgetBlueprint->WidgetTree->RootWidget = Root;
		return Root;
	}

	// WBP 完成流程：
	// 1. 先标记结构变化，让编辑器重新生成蓝图类和 Designer 树。
	// 2. 再编译并标脏，保证 BindWidgetOptional 字段能从最新控件树解析。
	// 3. 最后保存资产；它只证明正式 WBP 资产结构存在，不运行玩家交互测试。
	bool FinalizeWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint, const FString& PackageName)
	{
		if (!WidgetBlueprint)
		{
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		WidgetBlueprint->MarkPackageDirty();
		return SaveWidgetBlueprint(WidgetBlueprint, PackageName);
	}

	// Actor 蓝图完成流程：
	// 1. 先标记结构变化并编译，让 BP_CatShopKiosk 继承关系和默认组件进入生成类。
	// 2. 再保存到 Content，使人工验收时能从内容浏览器直接拖放商店对象。
	// 3. 这个步骤只提供交互组件宿主，不修改任何关卡。
	bool FinalizeActorBlueprint(UBlueprint* Blueprint, const FString& PackageName)
	{
		if (!Blueprint)
		{
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		return SaveAssetPackage(Blueprint, PackageName);
	}
}

// 测试流程：
// 1. 创建或刷新 IA_Interact，并把 E 键写入项目既有 IMC_InputContext，不生成第二套 IMC。
// 2. 分别创建 HUD、Inventory、InventorySlot、Shop、Interaction、Collection 六个正式 WBP。
// 3. 背包主界面只放 WrapBox 和动作按钮，格子 WBP 是独立 UserWidget 且不创建 Button 根。
// 4. 商店 WBP 只由 ShopInteractionComponent 后续打开；本测试额外保存可放置的 ShopEconomy Kiosk，但不把商店挂到 LocalPlayer。
// 5. 所有控件命名匹配新 C++ View 的 BindWidgetOptional 字段，让蓝图无需复杂绑定也能显示中文文本。
bool FCatUIModuleWidgetAssetsCreationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CatUIModuleWidgetAsset;

	UInputAction* InteractAction = LoadOrCreateInputAction(
		TEXT("/Game/Input/InputAction/IA_Interact"),
		TEXT("IA_Interact"),
		FText::FromString(TEXT("确认当前靠近的世界交互对象")));
	UInputMappingContext* InputContext = Cast<UInputMappingContext>(StaticLoadObject(
		UInputMappingContext::StaticClass(),
		nullptr,
		TEXT("/Game/Input/InputContext/IMC_InputContext.IMC_InputContext"),
		nullptr,
		LOAD_NoWarn));
	if (!TestNotNull(TEXT("创建或加载 IA_Interact"), InteractAction)
		|| !TestNotNull(TEXT("加载项目既有 IMC_InputContext"), InputContext))
	{
		return false;
	}
	InputContext->Modify();
	InputContext->UnmapAllKeysFromAction(InteractAction);
	InputContext->MapKey(InteractAction, EKeys::E);
	InputContext->MarkPackageDirty();

	UWidgetBlueprint* HUD = LoadOrCreateWidgetBlueprint(
		TEXT("/Game/UI/HUD/WBP_CatHUD"), TEXT("WBP_CatHUD"), UCatHUDWidget::StaticClass());
	UWidgetBlueprint* Inventory = LoadOrCreateWidgetBlueprint(
		TEXT("/Game/UI/Inventory/WBP_CatInventory"), TEXT("WBP_CatInventory"), UCatInventoryWidget::StaticClass());
	UWidgetBlueprint* InventorySlot = LoadOrCreateWidgetBlueprint(
		TEXT("/Game/UI/InventorySlot/WBP_CatInventorySlot"), TEXT("WBP_CatInventorySlot"),
		UCatInventorySlotWidget::StaticClass());
	UWidgetBlueprint* Shop = LoadOrCreateWidgetBlueprint(
		TEXT("/Game/UI/Shop/WBP_CatShop"), TEXT("WBP_CatShop"), UCatShopWidget::StaticClass());
	UWidgetBlueprint* Interaction = LoadOrCreateWidgetBlueprint(
		TEXT("/Game/UI/Interaction/WBP_CatInteractionPrompt"), TEXT("WBP_CatInteractionPrompt"),
		UCatInteractionPromptWidget::StaticClass());
	UWidgetBlueprint* Collection = LoadOrCreateWidgetBlueprint(
		TEXT("/Game/UI/Collection/WBP_CatCollection"), TEXT("WBP_CatCollection"), UCatCollectionWidget::StaticClass());
	UBlueprint* ShopKiosk = LoadOrCreateActorBlueprint(
		TEXT("/Game/ShopEconomy/BP_CatShopKiosk"), TEXT("BP_CatShopKiosk"), ACatShopKioskActor::StaticClass());

	if (!TestNotNull(TEXT("创建 HUD WBP"), HUD)
		|| !TestNotNull(TEXT("创建 Inventory WBP"), Inventory)
		|| !TestNotNull(TEXT("创建 InventorySlot WBP"), InventorySlot)
		|| !TestNotNull(TEXT("创建 Shop WBP"), Shop)
		|| !TestNotNull(TEXT("创建 Interaction WBP"), Interaction)
		|| !TestNotNull(TEXT("创建 Collection WBP"), Collection)
		|| !TestNotNull(TEXT("创建 Shop Kiosk 蓝图"), ShopKiosk))
	{
		return false;
	}

	UVerticalBox* HUDRoot = ResetRootToVerticalBox(HUD, TEXT("HUDRoot"));
	TestNotNull(TEXT("HUD 根"), HUDRoot);
	TestNotNull(TEXT("HUD 猫状态文本"), AddText(HUD, HUDRoot, TEXT("CatStatusTextBlock"), TEXT("猫状态：等待同步")));
	TestNotNull(TEXT("HUD 钓鱼反馈文本"), AddText(HUD, HUDRoot, TEXT("FishingFeedbackTextBlock"), TEXT("钓鱼反馈：等待同步")));

	UVerticalBox* InventoryRoot = ResetRootToVerticalBox(Inventory, TEXT("InventoryRoot"));
	TestNotNull(TEXT("背包根"), InventoryRoot);
	TestNotNull(TEXT("背包摘要文本"), AddText(Inventory, InventoryRoot, TEXT("SummaryTextBlock"), TEXT("背包：等待同步")));
	TestNotNull(TEXT("背包装备文本"), AddText(Inventory, InventoryRoot, TEXT("EquipmentTextBlock"), TEXT("当前装备：等待同步")));
	TestNotNull(TEXT("背包耗材文本"), AddText(Inventory, InventoryRoot, TEXT("ConsumablesTextBlock"), TEXT("随身耗材：等待同步")));
	TestNotNull(TEXT("待取装备文本"), AddText(Inventory, InventoryRoot, TEXT("TeamEquipmentTextBlock"), TEXT("待取装备：等待同步")));
	TestNotNull(TEXT("背包选中鱼文本"), AddText(Inventory, InventoryRoot, TEXT("SelectedFishTextBlock"), TEXT("鱼护操作：当前没有选中鱼")));
	UWrapBox* SlotWrapBox = Inventory->WidgetTree->ConstructWidget<UWrapBox>(
		UWrapBox::StaticClass(), TEXT("InventorySlotWrapBox"));
	if (TestNotNull(TEXT("背包 WrapBox"), SlotWrapBox))
	{
		InventoryRoot->AddChildToVerticalBox(SlotWrapBox);
	}
	TestNotNull(TEXT("背包结果文本"), AddText(Inventory, InventoryRoot, TEXT("ResultTextBlock"), TEXT("等待操作")));
	UHorizontalBox* InventoryButtons = Inventory->WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("InventoryActionButtons"));
	if (TestNotNull(TEXT("背包动作按钮行"), InventoryButtons))
	{
		InventoryRoot->AddChildToVerticalBox(InventoryButtons);
		TestNotNull(TEXT("吃鱼按钮"), AddButton(Inventory, InventoryButtons, TEXT("ConsumeFishButton"), TEXT("吃鱼")));
		TestNotNull(TEXT("献祭按钮"), AddButton(Inventory, InventoryButtons, TEXT("SacrificeFishButton"), TEXT("献祭")));
		TestNotNull(TEXT("关闭背包按钮"), AddButton(Inventory, InventoryButtons, TEXT("CloseButton"), TEXT("关闭")));
	}

	UVerticalBox* SlotRoot = ResetRootToVerticalBox(InventorySlot, TEXT("InventorySlotRoot"));
	TestNotNull(TEXT("格子根不是按钮"), SlotRoot);
	TestNotNull(TEXT("格子文本"), AddText(InventorySlot, SlotRoot, TEXT("DisplayTextBlock"), TEXT("[空]")));

	UVerticalBox* ShopRoot = ResetRootToVerticalBox(Shop, TEXT("ShopRoot"));
	TestNotNull(TEXT("商店根"), ShopRoot);
	TestNotNull(TEXT("商店公款文本"), AddText(Shop, ShopRoot, TEXT("WalletTextBlock"), TEXT("商店：公款等待同步")));
	TestNotNull(TEXT("商店商品文本"), AddText(Shop, ShopRoot, TEXT("EntriesTextBlock"), TEXT("商品等待同步")));
	TestNotNull(TEXT("商店结果文本"), AddText(Shop, ShopRoot, TEXT("ResultTextBlock"), TEXT("请选择商品")));
	UHorizontalBox* ShopButtons = Shop->WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ShopButtons"));
	if (TestNotNull(TEXT("商店按钮行"), ShopButtons))
	{
		ShopRoot->AddChildToVerticalBox(ShopButtons);
		TestNotNull(TEXT("二级竿按钮"), AddButton(Shop, ShopButtons, TEXT("PurchaseShopRodT2Button"), TEXT("买二级竿")));
		TestNotNull(TEXT("窝料按钮"), AddButton(Shop, ShopButtons, TEXT("PurchaseBugChumButton"), TEXT("买窝料")));
		TestNotNull(TEXT("免费饵按钮"), AddButton(Shop, ShopButtons, TEXT("ClaimFreeBugBaitButton"), TEXT("领鱼饵")));
		TestNotNull(TEXT("保底竿按钮"), AddButton(Shop, ShopButtons, TEXT("ClaimFreeStarterRodButton"), TEXT("领保底竿")));
		TestNotNull(TEXT("关闭商店按钮"), AddButton(Shop, ShopButtons, TEXT("CloseButton"), TEXT("关闭")));
	}

	UVerticalBox* InteractionRoot = ResetRootToVerticalBox(Interaction, TEXT("InteractionPromptRoot"));
	TestNotNull(TEXT("交互提示根"), InteractionRoot);
	TestNotNull(TEXT("交互提示文本"), AddText(Interaction, InteractionRoot, TEXT("PromptTextBlock"), TEXT("靠近对象后按键交互")));

	UVerticalBox* CollectionRoot = ResetRootToVerticalBox(Collection, TEXT("CollectionRoot"));
	TestNotNull(TEXT("图鉴根"), CollectionRoot);
	TestNotNull(TEXT("图鉴摘要文本"), AddText(Collection, CollectionRoot, TEXT("SummaryTextBlock"), TEXT("图鉴：等待同步")));
	TestNotNull(TEXT("图鉴列表文本"), AddText(Collection, CollectionRoot, TEXT("EntriesTextBlock"), TEXT("暂无记录")));

	const bool bSaved = SaveAssetPackage(InteractAction, TEXT("/Game/Input/InputAction/IA_Interact"))
		&& SaveAssetPackage(InputContext, TEXT("/Game/Input/InputContext/IMC_InputContext"))
		&& FinalizeWidgetBlueprint(HUD, TEXT("/Game/UI/HUD/WBP_CatHUD"))
		&& FinalizeWidgetBlueprint(Inventory, TEXT("/Game/UI/Inventory/WBP_CatInventory"))
		&& FinalizeWidgetBlueprint(InventorySlot, TEXT("/Game/UI/InventorySlot/WBP_CatInventorySlot"))
		&& FinalizeWidgetBlueprint(Shop, TEXT("/Game/UI/Shop/WBP_CatShop"))
		&& FinalizeActorBlueprint(ShopKiosk, TEXT("/Game/ShopEconomy/BP_CatShopKiosk"))
		&& FinalizeWidgetBlueprint(Interaction, TEXT("/Game/UI/Interaction/WBP_CatInteractionPrompt"))
		&& FinalizeWidgetBlueprint(Collection, TEXT("/Game/UI/Collection/WBP_CatCollection"));
	TestTrue(TEXT("保存拆分 UI WBP 资产"), bSaved);
	if (bSaved)
	{
		UE_LOG(LogTemp, Display,
			TEXT("CREATE_UI_MODULE_WBPS_PASS HUD=/Game/UI/HUD/WBP_CatHUD Inventory=/Game/UI/Inventory/WBP_CatInventory Slot=/Game/UI/InventorySlot/WBP_CatInventorySlot Shop=/Game/UI/Shop/WBP_CatShop ShopKiosk=/Game/ShopEconomy/BP_CatShopKiosk Interaction=/Game/UI/Interaction/WBP_CatInteractionPrompt Collection=/Game/UI/Collection/WBP_CatCollection InventorySlotRoot=UserWidgetNotButton SlotContainer=InventorySlotWrapBox InventoryEquipmentText=EquipmentTextBlock InventoryConsumablesText=ConsumablesTextBlock InventoryTeamEquipmentText=TeamEquipmentTextBlock ShopOwner=InteractionObject InteractAction=/Game/Input/InputAction/IA_Interact InteractContext=/Game/Input/InputContext/IMC_InputContext InteractKey=E"));
	}
	return bSaved;
}

#endif
