// 本文件会创建和保存 Editor 资产，只能在开发自动化与编辑器环境下编译，避免运行时包携带资产生成入口。
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UI/CatLakeReachWidget.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLakeReachWidgetFormalWBPAssetCreationTest,
	"Catfishing.Editor.UIReach.CreateFormalWBPAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 测试流程：
// 1. 先创建或刷新 Lake 菜单输入 IA，并把 Tab 映射写进项目既有 IMC_InputContext，避免生成第二套菜单 IMC。
// 2. 再加载现有 WBP_CatLakeReach；缺失时用 WidgetBlueprintFactory 创建继承 CatLakeReachWidget 的正式资产。
// 3. 复用已有资产时清空旧 WidgetTree 源控件和变量 GUID，再重建唯一 LakeReachRoot，避免旧白盒或半生成控件继续留在蓝图里。
// 4. HUD、Fishing 反馈、鱼护格子、鱼护动作、图鉴、商店和菜单按钮全部挂在同一个 WBP 根内，避免把单一 UIReach 模块拆成多个业务交付项。
// 5. 给这些控件写入 Designer 属性绑定，让 WBP 读取 C++ View 的中文文本、结果和 Visibility 投影，正式表现不再靠 C++ 默认 SetText。
// 6. 最后编译并保存包；编译 ensure、保存失败、父类错误或绑定缺失都会让本测试失败，不能把脏资产当作正式前端证据。
bool FCatLakeReachWidgetFormalWBPAssetCreationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString MenuActionPackageName = TEXT("/Game/Input/InputAction/IA_LakeMenu");
	const FString MenuMappingPackageName = TEXT("/Game/Input/InputContext/IMC_InputContext");
	UPackage* MenuActionPackage = CreatePackage(*MenuActionPackageName);
	if (!TestNotNull(TEXT("创建 Lake 菜单输入 Action 包"), MenuActionPackage))
	{
		return false;
	}

	UInputAction* MenuAction = Cast<UInputAction>(StaticLoadObject(
		UInputAction::StaticClass(),
		nullptr,
		TEXT("/Game/Input/InputAction/IA_LakeMenu.IA_LakeMenu"),
		nullptr,
		LOAD_NoWarn));
	if (!MenuAction)
	{
		MenuAction = NewObject<UInputAction>(MenuActionPackage, TEXT("IA_LakeMenu"), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(MenuAction);
	}
	if (!TestNotNull(TEXT("创建或加载 IA_LakeMenu"), MenuAction))
	{
		return false;
	}
	MenuAction->Modify();
	MenuAction->ValueType = EInputActionValueType::Boolean;
	MenuAction->ActionDescription = FText::FromString(TEXT("打开或关闭鱼护/商店菜单"));
	MenuAction->MarkPackageDirty();

	UInputMappingContext* MenuMappingContext = Cast<UInputMappingContext>(StaticLoadObject(
		UInputMappingContext::StaticClass(),
		nullptr,
		TEXT("/Game/Input/InputContext/IMC_InputContext.IMC_InputContext"),
		nullptr,
		LOAD_NoWarn));
	if (!TestNotNull(TEXT("加载项目既有 IMC_InputContext"), MenuMappingContext))
	{
		return false;
	}
	MenuMappingContext->Modify();
	MenuMappingContext->UnmapAllKeysFromAction(MenuAction);
	MenuMappingContext->MapKey(MenuAction, EKeys::Tab);
	MenuMappingContext->MarkPackageDirty();

	// 资产保存流程：同一条自动化同时落 IA、既有 InputContext 更新和 WBP；任一包保存失败都不能产出正式 UIReach 证据。
	const auto SaveGeneratedAsset = [this](UPackage* Package, UObject* Asset, const FString& LongPackageName, const TCHAR* Description) -> bool
	{
		if (!Package || !Asset)
		{
			return false;
		}
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(
			LongPackageName,
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
		TestTrue(Description, bSaved);
		return bSaved;
	};
	if (!SaveGeneratedAsset(MenuActionPackage, MenuAction, MenuActionPackageName, TEXT("保存 IA_LakeMenu 资产"))
		|| !SaveGeneratedAsset(MenuMappingContext->GetOutermost(), MenuMappingContext, MenuMappingPackageName, TEXT("保存 IMC_InputContext 资产")))
	{
		return false;
	}

	const FString PackageName = TEXT("/Game/UI/WBP_CatLakeReach");
	const FString ObjectPath = PackageName + TEXT(".WBP_CatLakeReach");
	UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
	if (!WidgetBlueprint)
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (!TestNotNull(TEXT("创建 WBP_CatLakeReach 包"), Package))
		{
			return false;
		}
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		if (!TestNotNull(TEXT("创建 WidgetBlueprintFactory"), Factory))
		{
			return false;
		}
		Factory->ParentClass = UCatLakeReachWidget::StaticClass();
		UObject* CreatedAsset = Factory->FactoryCreateNew(
			UWidgetBlueprint::StaticClass(),
			Package,
			TEXT("WBP_CatLakeReach"),
			RF_Public | RF_Standalone,
			nullptr,
			GWarn);
		WidgetBlueprint = Cast<UWidgetBlueprint>(CreatedAsset);
		if (!TestNotNull(TEXT("创建正式 UIReach WBP 资产"), WidgetBlueprint))
		{
			return false;
		}
		FAssetRegistryModule::AssetCreated(WidgetBlueprint);
	}

	if (!TestTrue(TEXT("正式 WBP 继承 CatLakeReachWidget 基类"),
		WidgetBlueprint->ParentClass == UCatLakeReachWidget::StaticClass()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("正式 WBP 有 WidgetTree"), WidgetBlueprint->WidgetTree.Get()))
	{
		return false;
	}

	WidgetBlueprint->Modify();
	WidgetBlueprint->WidgetTree->Modify();
	TArray<UWidget*> ExistingSourceWidgets = WidgetBlueprint->GetAllSourceWidgets();
	// 这里不是运行时销毁界面，而是在 Editor 自动化里把旧 Designer 树移出正式资产。
	// 这样每次生成都从干净根节点开始，避免旧控件名、旧绑定或半成品布局混进本轮 WBP 证据。
	WidgetBlueprint->WidgetTree->RootWidget = nullptr;
	WidgetBlueprint->WidgetTree->NamedSlotBindings.Reset();
	WidgetBlueprint->Bindings.Reset();
	for (UWidget* ExistingSourceWidget : ExistingSourceWidgets)
	{
		if (ExistingSourceWidget)
		{
			ExistingSourceWidget->Modify();
			ExistingSourceWidget->Slot = nullptr;
			ExistingSourceWidget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			ExistingSourceWidget->SetFlags(RF_Transient);
			ExistingSourceWidget->ClearFlags(RF_Transactional);
		}
	}
	WidgetBlueprint->WidgetVariableNameToGuidMap.Reset();

	// 下面这些局部工具只负责生成可保存的 Designer 控件树。
	// 它们不读写背包、鱼缸或献祭数据；真正的运行时行为仍由 CatLakeReachWidget 和 PageController 接线。
	auto ConfigureText = [](UTextBlock* TextBlock, const int32 FontSize, const FLinearColor Color)
	{
		if (!TextBlock)
		{
			return;
		}
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = FontSize;
		TextBlock->SetFont(Font);
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetAutoWrapText(true);
	};
	auto AddVerticalText = [WidgetBlueprint, &ConfigureText](
		UVerticalBox* Parent,
		const FName Name,
		const FString& Text,
		const bool bVariable,
		const int32 FontSize,
		const FLinearColor Color,
		const FMargin Padding) -> UTextBlock*
	{
		UTextBlock* TextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), Name);
		if (!TextBlock)
		{
			return nullptr;
		}
		TextBlock->SetText(FText::FromString(Text));
		TextBlock->bIsVariable = bVariable;
		ConfigureText(TextBlock, FontSize, Color);
		if (UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(TextBlock))
		{
			Slot->SetPadding(Padding);
		}
		return TextBlock;
	};
	auto AddHorizontalText = [WidgetBlueprint, &ConfigureText](
		UHorizontalBox* Parent,
		const FName Name,
		const FString& Text,
		const int32 FontSize,
		const FLinearColor Color,
		const FMargin Padding) -> UTextBlock*
	{
		UTextBlock* TextBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), Name);
		if (!TextBlock)
		{
			return nullptr;
		}
		TextBlock->SetText(FText::FromString(Text));
		TextBlock->bIsVariable = false;
		ConfigureText(TextBlock, FontSize, Color);
		if (UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(TextBlock))
		{
			Slot->SetPadding(Padding);
			Slot->SetVerticalAlignment(VAlign_Center);
		}
		return TextBlock;
	};
	auto AddSection = [WidgetBlueprint](UVerticalBox* Parent, const FName Name, const FLinearColor Color, const FMargin Padding) -> UVerticalBox*
	{
		UBorder* Section = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), Name);
		if (!Section)
		{
			return nullptr;
		}
		Section->bIsVariable = false;
		Section->SetBrushColor(Color);
		Section->SetPadding(Padding);
		if (UVerticalBoxSlot* SectionSlot = Parent->AddChildToVerticalBox(Section))
		{
			SectionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		}

		UVerticalBox* SectionContent = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), FName(*(Name.ToString() + TEXT("Content"))));
		if (!SectionContent)
		{
			return nullptr;
		}
		SectionContent->bIsVariable = false;
		Section->SetContent(SectionContent);
		return SectionContent;
	};
	auto AddSpacer = [WidgetBlueprint](UVerticalBox* Parent, const FName Name, const float Height)
	{
		USpacer* Spacer = WidgetBlueprint->WidgetTree->ConstructWidget<USpacer>(
			USpacer::StaticClass(), Name);
		if (!Spacer)
		{
			return;
		}
		Spacer->SetSize(FVector2D(1.0f, Height));
		Spacer->bIsVariable = false;
		Parent->AddChildToVerticalBox(Spacer);
	};
	auto AddButton = [WidgetBlueprint, &ConfigureText](UHorizontalBox* Parent, const FName Name, const FString& Label, const FMargin Padding) -> UButton*
	{
		UButton* Button = WidgetBlueprint->WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), Name);
		if (!Button)
		{
			return nullptr;
		}
		Button->bIsVariable = true;
		UTextBlock* LabelBlock = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*(Name.ToString() + TEXT("Label"))));
		if (!LabelBlock)
		{
			return nullptr;
		}
		LabelBlock->SetText(FText::FromString(Label));
		LabelBlock->SetJustification(ETextJustify::Center);
		LabelBlock->bIsVariable = false;
		ConfigureText(LabelBlock, 13, FLinearColor(0.95f, 0.98f, 1.0f, 1.0f));
		Button->AddChild(LabelBlock);
		if (UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(Button))
		{
			Slot->SetPadding(Padding);
			Slot->SetVerticalAlignment(VAlign_Center);
		}
		return Button;
	};
	auto AddFishGuardSlot = [WidgetBlueprint, &ConfigureText](UWrapBox* Parent, const int32 SlotIndex) -> bool
	{
		USizeBox* SlotBox = WidgetBlueprint->WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), FName(*FString::Printf(TEXT("FishGuardSlot%dBox"), SlotIndex)));
		if (!SlotBox)
		{
			return false;
		}
		SlotBox->bIsVariable = false;
		SlotBox->SetWidthOverride(104.0f);
		SlotBox->SetHeightOverride(76.0f);

		UButton* SlotButton = WidgetBlueprint->WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), FName(*FString::Printf(TEXT("FishGuardSlot%dButton"), SlotIndex)));
		if (!SlotButton)
		{
			return false;
		}
		SlotButton->bIsVariable = true;
		SlotBox->AddChild(SlotButton);

		UTextBlock* SlotText = WidgetBlueprint->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("FishGuardSlot%dText"), SlotIndex)));
		if (!SlotText)
		{
			return false;
		}
		SlotText->SetText(FText::FromString(FString::Printf(TEXT("第 %d 格\n空"), SlotIndex + 1)));
		SlotText->SetJustification(ETextJustify::Center);
		SlotText->bIsVariable = true;
		ConfigureText(SlotText, 12, FLinearColor(0.92f, 0.98f, 1.0f, 1.0f));
		SlotButton->AddChild(SlotText);

		if (UWrapBoxSlot* Slot = Parent->AddChildToWrapBox(SlotBox))
		{
			Slot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 6.0f));
		}
		return true;
	};

	// 生成测试在这里写 Designer 属性绑定，而不是让 C++ View 直接 SetText、SetVisibility 或 SetIsEnabled。
	// 绑定源统一来自 CatLakeReachWidget 暴露给蓝图的只读字段，缺字段或目标控件名写错都必须在测试里立刻失败。
	auto AddWidgetPropertyBinding = [this, WidgetBlueprint](const FName WidgetName, const FName DestinationPropertyName, const FName SourcePropertyName) -> bool
	{
		UWidget* TargetWidget = WidgetBlueprint->WidgetTree->FindWidget(WidgetName);
		if (!TestNotNull(*FString::Printf(TEXT("找到 WBP 绑定目标 %s"), *WidgetName.ToString()), TargetWidget))
		{
			return false;
		}
		FProperty* SourceProperty = FindFProperty<FProperty>(WidgetBlueprint->SkeletonGeneratedClass, SourcePropertyName);
		if (!SourceProperty)
		{
			SourceProperty = FindFProperty<FProperty>(UCatLakeReachWidget::StaticClass(), SourcePropertyName);
		}
		if (!TestNotNull(*FString::Printf(TEXT("找到 WBP 绑定源属性 %s"), *SourcePropertyName.ToString()), SourceProperty))
		{
			return false;
		}

		FDelegateEditorBinding Binding;
		Binding.ObjectName = WidgetName.ToString();
		Binding.PropertyName = DestinationPropertyName;
		Binding.SourceProperty = SourcePropertyName;
		Binding.SourcePath = FEditorPropertyPath(TArray<FFieldVariant>{SourceProperty});
		Binding.Kind = EBindingKind::Property;
		UBlueprint::GetGuidFromClassByFieldName<FProperty>(
			WidgetBlueprint->SkeletonGeneratedClass,
			SourcePropertyName,
			Binding.MemberGuid);
		WidgetBlueprint->Bindings.Remove(Binding);
		WidgetBlueprint->Bindings.AddUnique(Binding);
		return true;
	};

	UCanvasPanel* Root = WidgetBlueprint->WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("LakeReachRoot"));
	if (!TestNotNull(TEXT("创建 LakeReachRoot"), Root))
	{
		return false;
	}
	Root->bIsVariable = false;
	WidgetBlueprint->WidgetTree->RootWidget = Root;

	UBorder* LakeReachRootFrame = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("LakeReachRootFrame"));
	if (!TestNotNull(TEXT("创建 LakeReachRootFrame"), LakeReachRootFrame))
	{
		return false;
	}
	LakeReachRootFrame->bIsVariable = false;
	LakeReachRootFrame->SetBrushColor(FLinearColor(0.05f, 0.08f, 0.10f, 0.88f));
	LakeReachRootFrame->SetPadding(FMargin(18.0f, 16.0f));
	if (UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(LakeReachRootFrame))
	{
		FrameSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		FrameSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		FrameSlot->SetPosition(FVector2D(28.0f, 24.0f));
		FrameSlot->SetSize(FVector2D(520.0f, 620.0f));
	}

	UVerticalBox* MainStack = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("LakeReachMainStack"));
	if (!TestNotNull(TEXT("创建 LakeReachMainStack"), MainStack))
	{
		return false;
	}
	MainStack->bIsVariable = false;
	LakeReachRootFrame->SetContent(MainStack);

	TestNotNull(TEXT("创建标题文本"), AddVerticalText(
		MainStack, TEXT("LakeReachTitle"), TEXT("猫钓鱼湖"), false, 24,
		FLinearColor(0.92f, 0.98f, 1.0f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 4.0f)));
	TestNotNull(TEXT("创建副标题文本"), AddVerticalText(
		MainStack, TEXT("LakeReachSubtitle"), TEXT("状态、鱼护、商店"), false, 12,
		FLinearColor(0.60f, 0.72f, 0.78f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 12.0f)));

	UVerticalBox* HudSection = AddSection(
		MainStack, TEXT("LakeReachHudSection"), FLinearColor(0.12f, 0.18f, 0.20f, 0.94f), FMargin(12.0f, 10.0f));
	if (!TestNotNull(TEXT("创建 LakeReachHudSection"), HudSection))
	{
		return false;
	}
	TestNotNull(TEXT("创建 HUD 标题"), AddVerticalText(
		HudSection, TEXT("LakeReachHudTitle"), TEXT("当前猫状态"), false, 13,
		FLinearColor(0.68f, 0.82f, 0.86f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 5.0f)));
	TestNotNull(TEXT("创建 Survival 绑定文本"), AddVerticalText(
		HudSection, TEXT("SurvivalText"), TEXT("中毒值：0"), true, 16,
		FLinearColor(0.95f, 0.97f, 0.88f, 1.0f), FMargin(0.0f, 2.0f)));
	TestNotNull(TEXT("创建 Fishing 绑定文本"), AddVerticalText(
		HudSection, TEXT("FishingText"), TEXT("钓鱼力量：0"), true, 16,
		FLinearColor(0.84f, 0.95f, 1.0f, 1.0f), FMargin(0.0f, 2.0f)));
	TestNotNull(TEXT("创建反馈绑定文本"), AddVerticalText(
		HudSection, TEXT("FeedbackText"), TEXT("搏斗体力：0"), true, 16,
		FLinearColor(0.96f, 0.86f, 0.72f, 1.0f), FMargin(0.0f, 2.0f)));

	UBorder* MenuPanel = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("MenuPanel"));
	if (!TestNotNull(TEXT("创建 MenuPanel 外框"), MenuPanel))
	{
		return false;
	}
	MenuPanel->bIsVariable = true;
	MenuPanel->SetVisibility(ESlateVisibility::Collapsed);
	MenuPanel->SetBrushColor(FLinearColor(0.07f, 0.10f, 0.12f, 0.96f));
	MenuPanel->SetPadding(FMargin(12.0f, 12.0f));
	if (UVerticalBoxSlot* MenuSlot = MainStack->AddChildToVerticalBox(MenuPanel))
	{
		MenuSlot->SetPadding(FMargin(0.0f));
	}
	UVerticalBox* MenuStack = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MenuStack"));
	if (!TestNotNull(TEXT("创建 MenuStack"), MenuStack))
	{
		return false;
	}
	MenuStack->bIsVariable = false;
	MenuPanel->SetContent(MenuStack);

	TestNotNull(TEXT("创建菜单标题文本"), AddVerticalText(
		MenuStack, TEXT("LakeReachMenuTitle"), TEXT("鱼护 / 商店"), false, 18,
		FLinearColor(0.96f, 0.98f, 1.0f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 6.0f)));

	UBorder* FishGuardPanel = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("FishGuardPanel"));
	if (!TestNotNull(TEXT("创建鱼护主面板"), FishGuardPanel))
	{
		return false;
	}
	FishGuardPanel->bIsVariable = false;
	FishGuardPanel->SetBrushColor(FLinearColor(0.10f, 0.15f, 0.16f, 0.94f));
	FishGuardPanel->SetPadding(FMargin(10.0f, 10.0f));
	if (UVerticalBoxSlot* FishGuardPanelSlot = MenuStack->AddChildToVerticalBox(FishGuardPanel))
	{
		FishGuardPanelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}
	UVerticalBox* FishGuardStack = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("FishGuardStack"));
	if (!TestNotNull(TEXT("创建 FishGuardStack"), FishGuardStack))
	{
		return false;
	}
	FishGuardStack->bIsVariable = false;
	FishGuardPanel->SetContent(FishGuardStack);

	TestNotNull(TEXT("创建鱼护主面板标题"), AddVerticalText(
		FishGuardStack, TEXT("FishGuardPanelTitle"), TEXT("个人鱼护"), false, 15,
		FLinearColor(0.84f, 0.95f, 1.0f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 4.0f)));
	TestNotNull(TEXT("创建鱼护绑定文本"), AddVerticalText(
		FishGuardStack, TEXT("FishGuardText"), TEXT("个人鱼护：空"), true, 15,
		FLinearColor(0.80f, 0.91f, 0.96f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 4.0f)));

	UWrapBox* FishGuardSlotWrapBox = WidgetBlueprint->WidgetTree->ConstructWidget<UWrapBox>(
		UWrapBox::StaticClass(), TEXT("FishGuardSlotWrapBox"));
	if (!TestNotNull(TEXT("创建鱼护 WrapBox 格子容器"), FishGuardSlotWrapBox))
	{
		return false;
	}
	FishGuardSlotWrapBox->bIsVariable = false;
	FishGuardSlotWrapBox->SetInnerSlotPadding(FVector2D(2.0f, 2.0f));
	if (UVerticalBoxSlot* SlotWrapVerticalSlot = FishGuardStack->AddChildToVerticalBox(FishGuardSlotWrapBox))
	{
		SlotWrapVerticalSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 8.0f));
	}
	for (int32 SlotIndex = 0; SlotIndex < 8; ++SlotIndex)
	{
		TestTrue(*FString::Printf(TEXT("创建鱼护第 %d 个格子按钮"), SlotIndex + 1),
			AddFishGuardSlot(FishGuardSlotWrapBox, SlotIndex));
	}

	TestNotNull(TEXT("创建选中鱼绑定文本"), AddVerticalText(
		FishGuardStack, TEXT("SelectedFishGuardText"), TEXT("当前没有选中鱼"), true, 15,
		FLinearColor(1.0f, 0.94f, 0.72f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 8.0f)));

	UHorizontalBox* SelectionRow = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("FishGuardSelectionRow"));
	if (!TestNotNull(TEXT("创建鱼护选择按钮行"), SelectionRow))
	{
		return false;
	}
	SelectionRow->bIsVariable = false;
	FishGuardStack->AddChildToVerticalBox(SelectionRow);
	// 这些按钮名要和 CatLakeReachWidget 的可选 BindWidget 属性保持一致。
	// 蓝图只负责提供按钮本体，NativeConstruct 会自动把点击转成选择意图，不需要在蓝图图表里重复写逻辑。
	TestNotNull(TEXT("创建上一条鱼按钮"), AddButton(
		SelectionRow, TEXT("PreviousFishGuardButton"), TEXT("上一格"), FMargin(0.0f, 0.0f, 6.0f, 0.0f)));
	TestNotNull(TEXT("创建下一条鱼按钮"), AddButton(
		SelectionRow, TEXT("NextFishGuardButton"), TEXT("下一格"), FMargin(6.0f, 0.0f, 0.0f, 0.0f)));
	AddSpacer(FishGuardStack, TEXT("SelectionActionSpacer"), 8.0f);

	UBorder* FishGuardActionPanel = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("FishGuardActionPanel"));
	if (!TestNotNull(TEXT("创建鱼护动作区"), FishGuardActionPanel))
	{
		return false;
	}
	FishGuardActionPanel->bIsVariable = true;
	FishGuardActionPanel->SetBrushColor(FLinearColor(0.13f, 0.16f, 0.12f, 0.92f));
	FishGuardActionPanel->SetPadding(FMargin(8.0f, 8.0f));
	if (UVerticalBoxSlot* ActionPanelSlot = FishGuardStack->AddChildToVerticalBox(FishGuardActionPanel))
	{
		ActionPanelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}
	UVerticalBox* ActionStack = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("FishGuardActionStack"));
	if (!TestNotNull(TEXT("创建 FishGuardActionStack"), ActionStack))
	{
		return false;
	}
	ActionStack->bIsVariable = false;
	FishGuardActionPanel->SetContent(ActionStack);
	TestNotNull(TEXT("创建动作区标题"), AddVerticalText(
		ActionStack, TEXT("FishGuardActionTitle"), TEXT("选中鱼操作"), false, 12,
		FLinearColor(0.72f, 0.84f, 0.70f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 6.0f)));
	UHorizontalBox* ActionRow = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("FishGuardActionRow"));
	if (!TestNotNull(TEXT("创建鱼护动作按钮行"), ActionRow))
	{
		return false;
	}
	ActionRow->bIsVariable = false;
	ActionStack->AddChildToVerticalBox(ActionRow);
	// 动作按钮同样只命名和展示，不在 WBP 里直接删鱼、转缸或献祭。
	// 点击后的后端命令由 PageController 根据当前 Model 快照统一发起。
	TestNotNull(TEXT("创建吃鱼按钮"), AddButton(
		ActionRow, TEXT("ConsumeFishButton"), TEXT("吃掉"), FMargin(0.0f, 0.0f, 5.0f, 0.0f)));
	TestNotNull(TEXT("创建转缸按钮"), AddButton(
		ActionRow, TEXT("TransferFishToTankButton"), TEXT("入鱼缸"), FMargin(5.0f, 0.0f, 5.0f, 0.0f)));
	TestNotNull(TEXT("创建献祭按钮"), AddButton(
		ActionRow, TEXT("SacrificeFishButton"), TEXT("献祭"), FMargin(5.0f, 0.0f, 0.0f, 0.0f)));

	TestNotNull(TEXT("创建鱼护反馈绑定文本"), AddVerticalText(
		FishGuardStack, TEXT("FishGuardResultText"), TEXT("最近鱼护操作：暂无"), true, 13,
		FLinearColor(0.94f, 0.88f, 0.80f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 8.0f)));
	TestNotNull(TEXT("创建图鉴绑定文本"), AddVerticalText(
		MenuStack, TEXT("FishCollectionText"), TEXT("图鉴：0 条已记录"), true, 13,
		FLinearColor(0.72f, 0.86f, 0.96f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 8.0f)));

	UBorder* ShopPanel = WidgetBlueprint->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("ShopPanel"));
	if (!TestNotNull(TEXT("创建商店区"), ShopPanel))
	{
		return false;
	}
	ShopPanel->bIsVariable = false;
	ShopPanel->SetBrushColor(FLinearColor(0.10f, 0.14f, 0.18f, 0.92f));
	ShopPanel->SetPadding(FMargin(8.0f, 8.0f));
	if (UVerticalBoxSlot* ShopPanelSlot = MenuStack->AddChildToVerticalBox(ShopPanel))
	{
		ShopPanelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}
	UVerticalBox* ShopStack = WidgetBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("ShopStack"));
	if (!TestNotNull(TEXT("创建 ShopStack"), ShopStack))
	{
		return false;
	}
	ShopStack->bIsVariable = false;
	ShopPanel->SetContent(ShopStack);
	TestNotNull(TEXT("创建商店标题"), AddVerticalText(
		ShopStack, TEXT("ShopTitle"), TEXT("商店"), false, 13,
		FLinearColor(0.80f, 0.92f, 1.0f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 4.0f)));
	TestNotNull(TEXT("创建商店摘要绑定文本"), AddVerticalText(
		ShopStack, TEXT("ShopSummaryText"), TEXT("商店：团队公款 0"), true, 13,
		FLinearColor(0.88f, 0.95f, 1.0f, 1.0f), FMargin(0.0f, 0.0f, 0.0f, 6.0f)));
	UHorizontalBox* ShopPurchaseRow = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ShopPurchaseRow"));
	if (!TestNotNull(TEXT("创建商店购买按钮行"), ShopPurchaseRow))
	{
		return false;
	}
	ShopPurchaseRow->bIsVariable = false;
	ShopStack->AddChildToVerticalBox(ShopPurchaseRow);
	TestNotNull(TEXT("创建购买二级竿按钮"), AddButton(
		ShopPurchaseRow, TEXT("PurchaseShopRodT2Button"), TEXT("买二级竿"), FMargin(0.0f, 0.0f, 5.0f, 0.0f)));
	TestNotNull(TEXT("创建购买窝料按钮"), AddButton(
		ShopPurchaseRow, TEXT("PurchaseBugChumButton"), TEXT("买窝料"), FMargin(5.0f, 0.0f, 0.0f, 0.0f)));
	UHorizontalBox* ShopClaimRow = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ShopClaimRow"));
	if (!TestNotNull(TEXT("创建商店领取按钮行"), ShopClaimRow))
	{
		return false;
	}
	ShopClaimRow->bIsVariable = false;
	ShopStack->AddChildToVerticalBox(ShopClaimRow);
	TestNotNull(TEXT("创建免费鱼饵按钮"), AddButton(
		ShopClaimRow, TEXT("ClaimFreeBugBaitButton"), TEXT("领鱼饵"), FMargin(0.0f, 6.0f, 5.0f, 0.0f)));
	TestNotNull(TEXT("创建保底鱼竿按钮"), AddButton(
		ShopClaimRow, TEXT("ClaimFreeStarterRodButton"), TEXT("领保底竿"), FMargin(5.0f, 6.0f, 0.0f, 0.0f)));

	UHorizontalBox* FooterRow = WidgetBlueprint->WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("LakeReachFooterRow"));
	if (!TestNotNull(TEXT("创建底部操作行"), FooterRow))
	{
		return false;
	}
	FooterRow->bIsVariable = false;
	MenuStack->AddChildToVerticalBox(FooterRow);
	TestNotNull(TEXT("创建离局按钮"), AddButton(
		FooterRow, TEXT("LeaveButton"), TEXT("离开本局"), FMargin(0.0f, 0.0f, 6.0f, 0.0f)));
	TestNotNull(TEXT("创建关闭按钮"), AddButton(
		FooterRow, TEXT("CloseButton"), TEXT("关闭"), FMargin(6.0f, 0.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("WBP 绑定 Poison 中文展示值"), AddWidgetPropertyBinding(TEXT("SurvivalText"), TEXT("Text"), TEXT("BlueprintPoisonText")));
	TestTrue(TEXT("WBP 绑定 FishingStrength 中文展示值"), AddWidgetPropertyBinding(TEXT("FishingText"), TEXT("Text"), TEXT("BlueprintFishingStrengthText")));
	TestTrue(TEXT("WBP 绑定 FightStamina 中文展示值"), AddWidgetPropertyBinding(TEXT("FeedbackText"), TEXT("Text"), TEXT("BlueprintFightStaminaText")));
	TestTrue(TEXT("WBP 绑定个人鱼护中文数量"), AddWidgetPropertyBinding(TEXT("FishGuardText"), TEXT("Text"), TEXT("BlueprintFishGuardCountText")));
	TestTrue(TEXT("WBP 绑定鱼护第 1 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot0Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot0Text")));
	TestTrue(TEXT("WBP 绑定鱼护第 2 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot1Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot1Text")));
	TestTrue(TEXT("WBP 绑定鱼护第 3 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot2Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot2Text")));
	TestTrue(TEXT("WBP 绑定鱼护第 4 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot3Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot3Text")));
	TestTrue(TEXT("WBP 绑定鱼护第 5 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot4Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot4Text")));
	TestTrue(TEXT("WBP 绑定鱼护第 6 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot5Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot5Text")));
	TestTrue(TEXT("WBP 绑定鱼护第 7 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot6Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot6Text")));
	TestTrue(TEXT("WBP 绑定鱼护第 8 格文本"), AddWidgetPropertyBinding(TEXT("FishGuardSlot7Text"), TEXT("Text"), TEXT("BlueprintFishGuardSlot7Text")));
	TestTrue(TEXT("WBP 绑定选中鱼文本"), AddWidgetPropertyBinding(TEXT("SelectedFishGuardText"), TEXT("Text"), TEXT("BlueprintSelectedFishText")));
	TestTrue(TEXT("WBP 绑定鱼护反馈文本"), AddWidgetPropertyBinding(TEXT("FishGuardResultText"), TEXT("Text"), TEXT("BlueprintFishGuardResultText")));
	TestTrue(TEXT("WBP 绑定鱼护第 1 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot0Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot0Enabled")));
	TestTrue(TEXT("WBP 绑定鱼护第 2 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot1Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot1Enabled")));
	TestTrue(TEXT("WBP 绑定鱼护第 3 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot2Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot2Enabled")));
	TestTrue(TEXT("WBP 绑定鱼护第 4 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot3Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot3Enabled")));
	TestTrue(TEXT("WBP 绑定鱼护第 5 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot4Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot4Enabled")));
	TestTrue(TEXT("WBP 绑定鱼护第 6 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot5Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot5Enabled")));
	TestTrue(TEXT("WBP 绑定鱼护第 7 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot6Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot6Enabled")));
	TestTrue(TEXT("WBP 绑定鱼护第 8 格可用性"), AddWidgetPropertyBinding(TEXT("FishGuardSlot7Button"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardSlot7Enabled")));
	TestTrue(TEXT("WBP 绑定上一条鱼按钮可用性"), AddWidgetPropertyBinding(TEXT("PreviousFishGuardButton"), TEXT("bIsEnabled"), TEXT("bBlueprintCanSelectPreviousFishGuardEntry")));
	TestTrue(TEXT("WBP 绑定下一条鱼按钮可用性"), AddWidgetPropertyBinding(TEXT("NextFishGuardButton"), TEXT("bIsEnabled"), TEXT("bBlueprintCanSelectNextFishGuardEntry")));
	TestTrue(TEXT("WBP 绑定吃鱼按钮可用性"), AddWidgetPropertyBinding(TEXT("ConsumeFishButton"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardActionEnabled")));
	TestTrue(TEXT("WBP 绑定转缸按钮可用性"), AddWidgetPropertyBinding(TEXT("TransferFishToTankButton"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardActionEnabled")));
	TestTrue(TEXT("WBP 绑定献祭按钮可用性"), AddWidgetPropertyBinding(TEXT("SacrificeFishButton"), TEXT("bIsEnabled"), TEXT("bBlueprintFishGuardActionEnabled")));
	TestTrue(TEXT("WBP 绑定鱼护动作区显隐"), AddWidgetPropertyBinding(TEXT("FishGuardActionPanel"), TEXT("Visibility"), TEXT("BlueprintFishGuardActionVisibility")));
	TestTrue(TEXT("WBP 绑定图鉴中文文本"), AddWidgetPropertyBinding(TEXT("FishCollectionText"), TEXT("Text"), TEXT("BlueprintFishCollectionText")));
	TestTrue(TEXT("WBP 绑定商店摘要文本"), AddWidgetPropertyBinding(TEXT("ShopSummaryText"), TEXT("Text"), TEXT("BlueprintShopSummaryText")));
	TestTrue(TEXT("WBP 绑定购买二级竿按钮可用性"), AddWidgetPropertyBinding(TEXT("PurchaseShopRodT2Button"), TEXT("bIsEnabled"), TEXT("bBlueprintShopActionEnabled")));
	TestTrue(TEXT("WBP 绑定购买窝料按钮可用性"), AddWidgetPropertyBinding(TEXT("PurchaseBugChumButton"), TEXT("bIsEnabled"), TEXT("bBlueprintShopActionEnabled")));
	TestTrue(TEXT("WBP 绑定免费鱼饵按钮可用性"), AddWidgetPropertyBinding(TEXT("ClaimFreeBugBaitButton"), TEXT("bIsEnabled"), TEXT("bBlueprintShopActionEnabled")));
	TestTrue(TEXT("WBP 绑定保底鱼竿按钮可用性"), AddWidgetPropertyBinding(TEXT("ClaimFreeStarterRodButton"), TEXT("bIsEnabled"), TEXT("bBlueprintShopActionEnabled")));
	TestTrue(TEXT("WBP 绑定菜单显隐"), AddWidgetPropertyBinding(TEXT("MenuPanel"), TEXT("Visibility"), TEXT("BlueprintMenuVisibility")));
	TestTrue(TEXT("WBP 绑定离局按钮显隐"), AddWidgetPropertyBinding(TEXT("LeaveButton"), TEXT("Visibility"), TEXT("BlueprintLeaveVisibility")));
	TestTrue(TEXT("WBP 绑定离局按钮可用性"), AddWidgetPropertyBinding(TEXT("LeaveButton"), TEXT("bIsEnabled"), TEXT("bBlueprintLeaveEnabled")));
	// 绑定数量是 WBP 前端合同的一部分。
	// 新增或删掉正式展示字段时，资产生成测试和验证脚本必须一起更新，不能让缺绑定的资产静默保存。
	TestEqual(TEXT("正式 WBP 前端拥有 Designer 属性绑定"), WidgetBlueprint->Bindings.Num(), 37);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	WidgetBlueprint->MarkPackageDirty();
	UPackage* Package = WidgetBlueprint->GetOutermost();
	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, WidgetBlueprint, *PackageFilename, SaveArgs);
	TestTrue(TEXT("保存 WBP_CatLakeReach 资产"), bSaved);
	if (bSaved)
	{
		UE_LOG(LogTemp, Display, TEXT("CREATE_UI_REACH_WBP_PASS Asset=%s RootClass=%s WidgetCount=%d BindingCount=%d FrontendPanel=LakeReachRootFrame FishGuardPanel=FishGuardPanel SlotWrap=FishGuardSlotWrapBox SlotPreviewCount=8 ActionPanel=FishGuardActionPanel ShopPanel=ShopPanel MenuAction=/Game/Input/InputAction/IA_LakeMenu MenuContext=/Game/Input/InputContext/IMC_InputContext"),
			*ObjectPath,
			*GetNameSafe(WidgetBlueprint->GeneratedClass),
			WidgetBlueprint->GetAllSourceWidgets().Num(),
			WidgetBlueprint->Bindings.Num());
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
