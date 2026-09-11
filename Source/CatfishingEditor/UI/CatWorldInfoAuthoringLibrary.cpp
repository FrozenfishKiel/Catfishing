#include "CatWorldInfoAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ListView.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Font.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UI/WorldInfo/CatWorldInfoWidget.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"

/** 资产创作的独立日志分类；脚本失败时按 Event 和 Asset 定位具体合同或保存问题。 */
DEFINE_LOG_CATEGORY_STATIC(LogCatWorldInfoAuthoring, Log, All);

namespace CatWorldInfoAuthoring
{
	/** 正式行模板的稳定包路径；创建器先保存它，两个列表随后把生成类序列化为硬引用，供 Cook 收集。 */
	const FString RowPackage = TEXT("/Game/UI/WorldInfo/WBP_CatWorldInfoRow");
	/** 正式信息牌的稳定包路径；外部 UI 配置应引用此包中的生成类，作者器不修改运行时配置。 */
	const FString PanelPackage = TEXT("/Game/UI/WorldInfo/WBP_CatWorldInfo");
	/** 项目现有的中文 Font 资产；新建布局必须加载成功，不以引擎西文字体掩盖缺失依赖。 */
	const TCHAR* const ChineseFontPath = TEXT("/Game/UI/Shop/F_CatShopChinese.F_CatShopChinese");
	/** 单条信息在 1080p、DPI 比例为一时的逻辑高度；生成器用它给摘要两行和详情八行分配固定空间。 */
	constexpr float RowHeight = 36.0f;

	/** 读取目标包并区分真正缺失与已有但不合法的对象；输出为空且成功才允许创建，任何占用都禁止覆盖。 */
	bool ReadExisting(const FString& PackageName, UWidgetBlueprint*& OutWidget)
	{
		// 先记录磁盘和内存占用，再只加载已有包中的同名对象；避免对缺失路径的试探加载制造空包，错误类型和损坏文件均交给人工处理。
		OutWidget = nullptr;
		const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
		const bool bOnDisk = FPackageName::DoesPackageExist(PackageName);
		const bool bInMemory = FindPackage(nullptr, *PackageName) != nullptr;
		UObject* Existing = FindObject<UObject>(nullptr, *ObjectPath);
		if (!Existing && bOnDisk) Existing = LoadObject<UObject>(nullptr, *ObjectPath);
		if (Existing)
		{
			if (!bOnDisk || Existing->GetPathName() != ObjectPath)
			{
				UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_existing_not_persisted_or_redirected Asset=%s Resolved=%s"), *ObjectPath, *Existing->GetPathName());
				return false;
			}
			OutWidget = Cast<UWidgetBlueprint>(Existing);
			if (OutWidget) return true;
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_asset_type_invalid Asset=%s Type=%s"), *ObjectPath, *Existing->GetClass()->GetName());
			return false;
		}
		if (bOnDisk || bInMemory)
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_package_occupied Asset=%s Reason=UnloadableOrIncompletePackage"), *PackageName);
			return false;
		}
		return true;
	}

	/** 只读检查已编译父类和原生 BindWidget 合同；绑定必须在当前可达 Designer 树中，不能只留下同名孤立对象。 */
	bool ValidateBindings(UWidgetBlueprint* Blueprint, UClass* NativeParent)
	{
		// 先拒绝未编译、编译失败、父类错误或无根控件的资产，再核对每个 BindWidget 的名称、类型、变量身份和可达性。
		// 最后要求生成类默认对象不可聚焦，且默认视图与根均不参与命中；任一不符就记录并返回 false，不编译或标脏。
		if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(NativeParent)
			|| !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(NativeParent)
			|| (Blueprint->Status != BS_UpToDate && Blueprint->Status != BS_UpToDateWithWarnings)
			|| !Blueprint->WidgetTree || !Blueprint->WidgetTree->RootWidget)
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_parent_or_compile_invalid Asset=%s ExpectedParent=%s"), *GetPathNameSafe(Blueprint), *NativeParent->GetName());
			return false;
		}
		TArray<UWidget*> Widgets;
		Blueprint->WidgetTree->GetAllWidgets(Widgets);
		for (TFieldIterator<FObjectPropertyBase> It(NativeParent); It; ++It)
		{
			if (!It->HasMetaData(TEXT("BindWidget"))) continue;
			UWidget* Bound = Blueprint->WidgetTree->FindWidget(It->GetFName());
			if (!Bound || !Bound->IsA(It->PropertyClass) || !Bound->bIsVariable || !Widgets.Contains(Bound))
			{
				UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_binding_invalid Asset=%s Binding=%s ExpectedType=%s"),
					*Blueprint->GetPathName(), *It->GetName(), *It->PropertyClass->GetName());
				return false;
			}
		}
		const UUserWidget* Defaults = Blueprint->GeneratedClass->GetDefaultObject<UUserWidget>();
		if (!Defaults || Defaults->IsFocusable() || Defaults->GetVisibility() != ESlateVisibility::HitTestInvisible
			|| Blueprint->WidgetTree->RootWidget->GetVisibility() != ESlateVisibility::HitTestInvisible)
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_input_contract_invalid Asset=%s Reason=ExpectedNoFocusAndNoHitTesting"), *Blueprint->GetPathName());
			return false;
		}
		return true;
	}

	/** 核验面板的页面归属及正式行模板引用；允许设计师调整外观，但摘要零、详情一和只读列表不可改义。 */
	bool ValidatePanel(UWidgetBlueprint* Blueprint, UWidgetBlueprint* RowBlueprint)
	{
		// 基础绑定成立后，从每个显示字段向上追到 Switcher 的直接子页；随后检查列表引用和输入策略，发现错误只记录不修补。
		if (!ValidateBindings(Blueprint, UCatWorldInfoWidget::StaticClass())) return false;
		UWidgetTree* Tree = Blueprint->WidgetTree;
		UWidgetSwitcher* Switcher = CastChecked<UWidgetSwitcher>(Tree->FindWidget(TEXT("DetailSwitcher")));
		if (Switcher->GetChildrenCount() != 2)
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_switcher_invalid Asset=%s ExpectedPages=2"), *Blueprint->GetPathName());
			return false;
		}
		for (int32 Page = 0; Page < 2; ++Page)
		{
			const TArray<FName> Names = Page == 0
				? TArray<FName>{TEXT("SummaryTitleText"), TEXT("SummaryStatusText"), TEXT("SummaryInfoList")}
				: TArray<FName>{TEXT("TitleText"), TEXT("StatusText"), TEXT("InfoList")};
			for (const FName Name : Names)
			{
				UWidget* Child = Tree->FindWidget(Name);
				while (Child && Child->GetParent() != Switcher) Child = Child->GetParent();
				if (Child != Switcher->GetChildAt(Page))
				{
					UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_page_binding_invalid Asset=%s Binding=%s ExpectedPage=%d"), *Blueprint->GetPathName(), *Name.ToString(), Page);
					return false;
				}
			}
			UListView* List = CastChecked<UListView>(Tree->FindWidget(Names[2]));
			const FBoolProperty* Focus = FindFProperty<FBoolProperty>(UListView::StaticClass(), TEXT("bIsFocusable"));
			if (!RowBlueprint || List->GetEntryWidgetClass().Get() != RowBlueprint->GeneratedClass
				|| List->GetSelectionMode() != ESelectionMode::None || !Focus || Focus->GetPropertyValue_InContainer(List)
				|| List->GetVisibility() != ESlateVisibility::HitTestInvisible)
			{
				UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_list_contract_invalid Asset=%s Binding=%s Reason=EntryClassOrInput"), *Blueprint->GetPathName(), *Names[2].ToString());
				return false;
			}
		}
		return true;
	}

	/** 创建带中文字体的单行文本模板；固定字号与省略策略让内容更新不会改变行高，实际文案仍由运行时绑定写入。 */
	UTextBlock* AddText(UWidgetTree* Tree, FName Name, UFont* Font, int32 Size, const FLinearColor& Color)
	{
		// 在新树中创建空文本，写入项目 Regular 字面和显式字号，再关闭自动换行并禁用命中；不加载字体替代品或写入虚构玩法值。
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetFont(FSlateFontInfo(Font, Size, TEXT("Regular")));
		Text->SetColorAndOpacity(Color);
		Text->SetText(FText::GetEmpty());
		Text->SetAutoWrapText(false);
		Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Text;
	}

	/** 首次构造行 WBP 的视觉树；稳定宽高、图标槽和可选进度样式全部保存为设计资产，不给运行时新增排版逻辑。 */
	void BuildRow(UWidgetBlueprint* Blueprint, UFont* Font)
	{
		// 1. 调用方已提供新蓝图、SizeBox 根和有效字体；固定 296x36 的行边界，再添加背景与纵向内容列。
		// 2. 在文字区依次划分固定图标列、标签列和占满余宽的右对齐数值列；空图标初始收起，列宽仍保留。
		// 3. 预留四逻辑像素进度区并配置从左到右的样式，进度初始收起；这里只构建新树，变量登记、编译和保存交由外层。
		UWidgetTree* Tree = Blueprint->WidgetTree;
		USizeBox* Root = CastChecked<USizeBox>(Tree->RootWidget);
		Root->SetWidthOverride(296.0f);
		Root->SetHeightOverride(RowHeight);
		UBorder* Background = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RowBackground"));
		Background->SetBrush(FSlateColorBrush(FLinearColor(0.08f, 0.10f, 0.09f, 0.70f)));
		Background->SetPadding(FMargin(0.0f, 2.0f));
		Root->SetContent(Background);
		UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowColumn"));
		Background->SetContent(Column);
		USizeBox* TextBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RowTextBounds"));
		TextBounds->SetHeightOverride(26.0f);
		Column->AddChildToVerticalBox(TextBounds);
		UHorizontalBox* TextLine = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RowTextLine"));
		TextBounds->SetContent(TextLine);
		USizeBox* IconBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RowIconBounds"));
		IconBounds->SetWidthOverride(20.0f);
		IconBounds->SetHeightOverride(20.0f);
		UHorizontalBoxSlot* IconSlot = TextLine->AddChildToHorizontalBox(IconBounds);
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
		UImage* Icon = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("RowIcon"));
		Icon->SetDesiredSizeOverride(FVector2D(20.0f, 20.0f));
		Icon->SetVisibility(ESlateVisibility::Collapsed);
		IconBounds->SetContent(Icon);
		// 图标为空仍保留同宽列，避免同一列表中带图标和不带图标的字段左右跳动。
		USizeBox* LabelBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LabelBounds"));
		LabelBounds->SetWidthOverride(102.0f);
		LabelBounds->SetContent(AddText(Tree, TEXT("LabelText"), Font, 14, FLinearColor(0.76f, 0.82f, 0.80f)));
		TextLine->AddChildToHorizontalBox(LabelBounds)->SetVerticalAlignment(VAlign_Center);
		UTextBlock* Value = AddText(Tree, TEXT("ValueText"), Font, 15, FLinearColor::White);
		Value->SetJustification(ETextJustify::Right);
		UHorizontalBoxSlot* ValueSlot = TextLine->AddChildToHorizontalBox(Value);
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ValueSlot->SetVerticalAlignment(VAlign_Center);
		ValueSlot->SetPadding(FMargin(8.0f, 0.0f, 4.0f, 0.0f));
		USizeBox* ProgressBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RowProgressBounds"));
		ProgressBounds->SetHeightOverride(4.0f);
		Column->AddChildToVerticalBox(ProgressBounds);
		UProgressBar* Progress = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("RowProgress"));
		FProgressBarStyle ProgressStyle;
		ProgressStyle.SetBackgroundImage(FSlateColorBrush(FLinearColor(0.18f, 0.21f, 0.20f)));
		ProgressStyle.SetFillImage(FSlateColorBrush(FLinearColor::White));
		Progress->SetWidgetStyle(ProgressStyle);
		Progress->SetFillColorAndOpacity(FLinearColor(0.34f, 0.80f, 0.62f));
		Progress->SetBarFillType(EProgressBarFillType::LeftToRight);
		Progress->SetBarFillStyle(EProgressBarFillStyle::Scale);
		Progress->SetPercent(0.0f);
		Progress->SetVisibility(ESlateVisibility::Collapsed);
		ProgressBounds->SetContent(Progress);
	}

	/** 首次构造摘要与详情页面；列表行类写入模板属性，固定容量负责屏幕空间预算，数据筛选仍由运行时负责。 */
	bool BuildPanel(UWidgetBlueprint* Blueprint, UFont* Font, UClass* RowClass)
	{
		// 1. 核实 UE 编辑器可序列化的行类、焦点和列表样式属性；属性缺失或结构类型变化时记录失败，不继续建页。
		// 2. 在调用方提供的 SizeBox 根内设置 320 逻辑像素宽和 Switcher，按零摘要、一详情创建各自标题、状态与列表区域。
		// 3. 列表分别预留两行和八行高度，将正式行类与透明背景写入模板，并关闭焦点、选择和各类滚动输入。
		// 4. 默认选择摘要并返回成功；本方法不编译保存，也不向 ListView 填入示例业务对象。
		FClassProperty* EntryProperty = FindFProperty<FClassProperty>(UListView::StaticClass(), TEXT("EntryWidgetClass"));
		FBoolProperty* FocusProperty = FindFProperty<FBoolProperty>(UListView::StaticClass(), TEXT("bIsFocusable"));
		FStructProperty* StyleProperty = FindFProperty<FStructProperty>(UListView::StaticClass(), TEXT("WidgetStyle"));
		if (!EntryProperty || !FocusProperty || !StyleProperty || StyleProperty->Struct != FTableViewStyle::StaticStruct())
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_list_property_missing Reason=EngineContractChanged"));
			return false;
		}
		UWidgetTree* Tree = Blueprint->WidgetTree;
		USizeBox* Root = CastChecked<USizeBox>(Tree->RootWidget);
		Root->SetWidthOverride(320.0f);
		UWidgetSwitcher* Switcher = Tree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("DetailSwitcher"));
		Root->SetContent(Switcher);
		for (int32 Page = 0; Page < 2; ++Page)
		{
			const bool bSummary = Page == 0;
			UBorder* Background = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), bSummary ? TEXT("SummaryBackground") : TEXT("DetailBackground"));
			Background->SetBrush(FSlateColorBrush(FLinearColor(0.025f, 0.035f, 0.030f, 0.94f)));
			Background->SetPadding(FMargin(12.0f));
			Switcher->AddChild(Background);
			UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), bSummary ? TEXT("SummaryColumn") : TEXT("DetailColumn"));
			Background->SetContent(Column);
			USizeBox* TitleBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), bSummary ? TEXT("SummaryTitleBounds") : TEXT("TitleBounds"));
			TitleBounds->SetHeightOverride(30.0f);
			TitleBounds->SetContent(AddText(Tree, bSummary ? TEXT("SummaryTitleText") : TEXT("TitleText"), Font, 18, FLinearColor::White));
			Column->AddChildToVerticalBox(TitleBounds);
			USizeBox* StatusBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), bSummary ? TEXT("SummaryStatusBounds") : TEXT("StatusBounds"));
			StatusBounds->SetHeightOverride(24.0f);
			StatusBounds->SetContent(AddText(Tree, bSummary ? TEXT("SummaryStatusText") : TEXT("StatusText"), Font, 13, FLinearColor(0.95f, 0.79f, 0.41f)));
			Column->AddChildToVerticalBox(StatusBounds);
			USizeBox* ListBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), bSummary ? TEXT("SummaryListBounds") : TEXT("DetailListBounds"));
			ListBounds->SetHeightOverride(RowHeight * (bSummary ? 2.0f : 8.0f));
			Column->AddChildToVerticalBox(ListBounds);
			UListView* List = Tree->ConstructWidget<UListView>(UListView::StaticClass(), bSummary ? TEXT("SummaryInfoList") : TEXT("InfoList"));
			// UE 5.8 只提供这些属性的读取接口；反射仅在新资产模板上写入，结果随 WBP 保存，运行时不再次设置行类。
			EntryProperty->SetObjectPropertyValue_InContainer(List, RowClass);
			FocusProperty->SetPropertyValue_InContainer(List, false);
			FTableViewStyle ListStyle;
			ListStyle.SetBackgroundBrush(FSlateColorBrush(FLinearColor::Transparent));
			*StyleProperty->ContainerPtrToValuePtr<FTableViewStyle>(List) = ListStyle;
			List->SetSelectionMode(ESelectionMode::None);
			List->SetScrollbarVisibility(ESlateVisibility::Collapsed);
			List->SetAllowOverScroll(false);
			List->SetIsPointerScrollingEnabled(false);
			List->SetIsTouchScrollingEnabled(false);
			List->SetIsGamepadScrollingEnabled(false);
			List->SetWheelScrollMultiplier(0.0f);
			List->SetHorizontalEntrySpacing(0.0f);
			List->SetVerticalEntrySpacing(0.0f);
			ListBounds->SetContent(List);
		}
		Switcher->SetActiveWidgetIndex(0);
		return true;
	}

	/** 编译并保存一份新建 WBP；变量登记和只读默认值均写入正式资产，失败对象留在编辑器供诊断，不自动删除或覆盖重试。 */
	bool CompileAndSave(UWidgetBlueprint* Blueprint, UClass* NativeParent, UWidgetBlueprint* RowBlueprint)
	{
		// 1. 给新树的原生绑定控件启用变量，为所有源控件登记 GUID；可选图标和进度继续保持收起。
		// 2. 结构编译后写入生成类的无焦点默认值，再核验父类、绑定和列表；任一错误都停止保存。
		// 3. 仅保存当前新包并登记资产；保存失败明确返回 false，绝不保存其他脏包或地图。
		Blueprint->WidgetTree->ForEachWidget([Blueprint, NativeParent](UWidget* Widget)
		{
			if (Widget->GetVisibility() != ESlateVisibility::Collapsed) Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
			const FProperty* Property = FindFProperty<FProperty>(NativeParent, Widget->GetFName());
			if (Property && Property->HasMetaData(TEXT("BindWidget")))
			{
				FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(Blueprint, Widget, true, false);
			}
			if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())) Blueprint->OnVariableAdded(Widget->GetFName());
		});
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass) return false;
		UUserWidget* Defaults = Blueprint->GeneratedClass->GetDefaultObject<UUserWidget>();
		Defaults->SetIsFocusable(false);
		Defaults->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (!ValidateBindings(Blueprint, NativeParent)
			|| (NativeParent == UCatWorldInfoWidget::StaticClass() && !ValidatePanel(Blueprint, RowBlueprint))) return false;
		Blueprint->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs)) return false;
		FAssetRegistryModule::AssetCreated(Blueprint);
		UE_LOG(LogCatWorldInfoAuthoring, Display, TEXT("Event=world_info_asset_created Asset=%s"), *Blueprint->GetPathName());
		return true;
	}
}

bool UCatWorldInfoAuthoringLibrary::CreateMissingWorldInfoWidgetBlueprints()
{
	// 编辑器一次性创作流程：
	// 1. 预检两个正式路径；已有对象必须先通过只读合同核验，避免发现冲突前生成另一个资产。
	// 2. 有缺失项才加载项目中文字体，先创建并保存行，再把真实生成类写进新面板的两个列表。
	// 3. 逐包保存，不做跨资产回滚；中途失败时已保存的行可供重试复用，未完成的内存包需人工处理。
	// 4. 两份既有资产均有效时直接成功返回；不重新布局、不编译、不保存，也不访问关卡或编辑器会话。
	using namespace CatWorldInfoAuthoring;
	UWidgetBlueprint* Row = nullptr;
	UWidgetBlueprint* Panel = nullptr;
	if (!ReadExisting(RowPackage, Row) || !ReadExisting(PanelPackage, Panel)) return false;
	if ((Row && !ValidateBindings(Row, UCatWorldInfoRowWidget::StaticClass())) || (Panel && !ValidatePanel(Panel, Row))) return false;
	if (Row && Panel)
	{
		UE_LOG(LogCatWorldInfoAuthoring, Display, TEXT("Event=world_info_existing_assets_valid Mode=ReadOnly"));
		return true;
	}
	UFont* Font = LoadObject<UFont>(nullptr, ChineseFontPath);
	if (!Font)
	{
		UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_chinese_font_missing Asset=%s"), ChineseFontPath);
		return false;
	}
	if (!Row)
	{
		Row = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(CreatePackage(*RowPackage),
			FName(*FPackageName::GetLongPackageAssetName(RowPackage)), BPTYPE_Normal,
			UCatWorldInfoRowWidget::StaticClass(), USizeBox::StaticClass(), TEXT("CatWorldInfoAuthoring"), false);
		if (!Row || !Row->WidgetTree || !Row->WidgetTree->RootWidget)
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_create_failed Asset=%s Stage=Factory"), *RowPackage);
			return false;
		}
		BuildRow(Row, Font);
		if (!CompileAndSave(Row, UCatWorldInfoRowWidget::StaticClass(), nullptr))
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_create_failed Asset=%s Stage=CompileOrSave"), *RowPackage);
			return false;
		}
	}
	if (!Panel)
	{
		Panel = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(CreatePackage(*PanelPackage),
			FName(*FPackageName::GetLongPackageAssetName(PanelPackage)), BPTYPE_Normal,
			UCatWorldInfoWidget::StaticClass(), USizeBox::StaticClass(), TEXT("CatWorldInfoAuthoring"), false);
		if (!Panel || !Panel->WidgetTree || !Panel->WidgetTree->RootWidget)
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_create_failed Asset=%s Stage=Factory"), *PanelPackage);
			return false;
		}
		if (!BuildPanel(Panel, Font, Row->GeneratedClass) || !CompileAndSave(Panel, UCatWorldInfoWidget::StaticClass(), Row))
		{
			UE_LOG(LogCatWorldInfoAuthoring, Error, TEXT("Event=world_info_create_failed Asset=%s Stage=LayoutCompileOrSave"), *PanelPackage);
			return false;
		}
	}
	return true;
}
