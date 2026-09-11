#include "CatDayTransitionAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UI/Run/CatDayTransitionWidget.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"

namespace CatDayTransitionAuthoring
{
	/** 翻天视图唯一的正式内容目录；运行时设置通过这个稳定路径加载已烘焙的 WBP，而不是构建替身控件树。 */
	const FString WidgetDirectory = TEXT("/Game/UI/Run");
	/** 翻天视图唯一的正式资产名；它同时约束软类路径、编辑器生成脚本和三个 BindWidget 的所属 WBP。 */
	const TCHAR* const WidgetAssetName = TEXT("WBP_CatDayTransition");
	/** 项目现有中文字体资产；正式翻天文案沿用此字体以保证中文三行摘要在 Designer 和打包版本中一致显示。 */
	const TCHAR* const ChineseFontPath = TEXT("/Game/UI/Shop/F_CatShopChinese.F_CatShopChinese");
	/** 项目中文字体承诺的字面名称；当前界面用字号而非不存在的 Bold 字面区分标题层级。 */
	const FName ChineseTypefaceName = TEXT("Regular");

	/** 将新建遮罩或文本控件登记为 Blueprint 变量，供外层编译后按 BindWidget 名称连接运行时成员。 */
	void ExposeWidget(UWidgetBlueprint* WidgetBlueprint, UWidget* Widget)
	{
		// 变量登记流程：空蓝图或控件直接返回；为新控件启用 Designer 变量，缺 GUID 时补登记，由外层统一编译与保存。
		if (!WidgetBlueprint || !Widget)
		{
			return;
		}
		FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(WidgetBlueprint, Widget, true, false);
		if (!WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
		{
			WidgetBlueprint->OnVariableAdded(Widget->GetFName());
		}
	}

	/** 同步当前 WidgetTree 的变量 GUID 表，保证结构化编译不会保留已不存在的 Designer 控件，也能为新布局容器补齐引擎需要的登记。 */
	void SyncWidgetVariableGuids(UWidgetBlueprint* WidgetBlueprint)
	{
		// GUID 同步流程：空蓝图直接返回；遍历当前源控件收集名字，删除不再对应源控件的条目，再为未登记的现存控件补充 GUID。
		if (!WidgetBlueprint)
		{
			return;
		}
		TSet<FName> SourceWidgetNames;
		WidgetBlueprint->ForEachSourceWidget([&SourceWidgetNames](UWidget* Widget)
		{
			SourceWidgetNames.Add(Widget->GetFName());
		});
		for (auto It = WidgetBlueprint->WidgetVariableNameToGuidMap.CreateIterator(); It; ++It)
		{
			if (!SourceWidgetNames.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
		for (const FName WidgetName : SourceWidgetNames)
		{
			if (!WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(WidgetName))
			{
				WidgetBlueprint->OnVariableAdded(WidgetName);
			}
		}
	}

	/** 把子控件铺满 Canvas，确保黑色遮罩随任意视口尺寸完整覆盖，而标题和摘要的内部排版由 Border 子树完成。 */
	void AddFullCanvasChild(UCanvasPanel* Canvas, UWidget* Child)
	{
		// Canvas 装配流程：空输入直接返回；加入子控件后若取得 CanvasSlot，就固定四角锚点、零边距和零对齐以铺满视口。
		// 未取得 Slot 时不再设置布局，本方法没有返回状态，调用方无法据此区分装配失败。
		if (!Canvas || !Child)
		{
			return;
		}
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child))
		{
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
			Slot->SetAlignment(FVector2D::ZeroVector);
		}
	}

	/** 构造新资产的黑幕、单行标题和可换行结算摘要区域；布局完成返回 true，之后由 Designer 持有布局所有权。 */
	bool BuildDayTransitionWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 首次布局流程：
		// 1. 核对蓝图、WidgetTree 和工厂 Canvas 根，再加载项目字体；缺少这些前提则返回 false，字体缺失另记日志。
		// 2. 创建全屏黑色 Border 并登记遮罩变量；同级另建带安全边距的居中内容层，避免黑色透明度为零时把失败文本也隐藏。
		// 3. 创建最大期望宽度为 1120 的内容列、标题样例和初始为空的可换行摘要，设置字体并登记三处运行时绑定。
		// 4. 已检查的控件创建失败时返回 false，先前生成的部分保留在新内存包中；成功只表示布局构造结束，编译保存由外层负责。
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return false;
		}

		UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(Tree->RootWidget);
		if (!Canvas)
		{
			return false;
		}
		UObject* ChineseFont = LoadObject<UObject>(nullptr, ChineseFontPath);
		if (!ChineseFont)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=day_transition_authoring_font_missing Font=%s"), ChineseFontPath);
			return false;
		}

		UBorder* BlackOverlay = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BlackOverlay"));
		if (!BlackOverlay)
		{
			return false;
		}
		BlackOverlay->SetBrush(FSlateColorBrush(FLinearColor::Black));
		BlackOverlay->SetPadding(FMargin(72.0f, 48.0f));
		BlackOverlay->SetHorizontalAlignment(HAlign_Fill);
		BlackOverlay->SetVerticalAlignment(VAlign_Center);
		AddFullCanvasChild(Canvas, BlackOverlay);
		ExposeWidget(WidgetBlueprint, BlackOverlay);
		// 背景和结果是两个独立全屏层；UI 只改黑层透明度，失败文字保持可见且不继承黑层的零透明度。
		UBorder* ContentLayer = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TransitionContentLayer"));
		ContentLayer->SetBrush(FSlateColorBrush(FLinearColor::Transparent));
		ContentLayer->SetPadding(FMargin(72.0f, 48.0f));
		ContentLayer->SetHorizontalAlignment(HAlign_Center);
		ContentLayer->SetVerticalAlignment(VAlign_Center);
		AddFullCanvasChild(Canvas, ContentLayer);

		USizeBox* ContentBounds = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TransitionContentBounds"));
		UVerticalBox* ContentColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TransitionContentColumn"));
		if (!ContentBounds || !ContentColumn)
		{
			return false;
		}
		ContentBounds->SetMaxDesiredWidth(1120.0f);
		ContentBounds->SetContent(ContentColumn);
		ContentLayer->SetContent(ContentBounds);

		UTextBlock* ResultText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultText"));
		if (!ResultText)
		{
			return false;
		}
		ResultText->SetText(FText::FromString(TEXT("第 2 天")));
		ResultText->SetFont(FSlateFontInfo(ChineseFont, 40, ChineseTypefaceName));
		ResultText->SetColorAndOpacity(FLinearColor::White);
		ResultText->SetJustification(ETextJustify::Center);
		ResultText->SetAutoWrapText(false);
		ResultText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		ResultText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		ResultText->SetShadowOffset(FVector2D(1.0f, 1.0f));
		ContentColumn->AddChild(ResultText);
		if (UVerticalBoxSlot* ResultSlot = Cast<UVerticalBoxSlot>(ResultText->Slot))
		{
			ResultSlot->SetHorizontalAlignment(HAlign_Fill);
			ResultSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
		}
		ExposeWidget(WidgetBlueprint, ResultText);

		UTextBlock* SettlementText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SettlementText"));
		if (!SettlementText)
		{
			return false;
		}
		SettlementText->SetText(FText::GetEmpty());
		SettlementText->SetFont(FSlateFontInfo(ChineseFont, 22, ChineseTypefaceName));
		SettlementText->SetColorAndOpacity(FLinearColor(0.90f, 0.94f, 0.93f));
		SettlementText->SetJustification(ETextJustify::Center);
		SettlementText->SetAutoWrapText(true);
		SettlementText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		SettlementText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		SettlementText->SetShadowOffset(FVector2D(1.0f, 1.0f));
		ContentColumn->AddChild(SettlementText);
		if (UVerticalBoxSlot* SettlementSlot = Cast<UVerticalBoxSlot>(SettlementText->Slot))
		{
			SettlementSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		ExposeWidget(WidgetBlueprint, SettlementText);
		return true;
	}

	/** 只读确认正式 WBP 已编译且三个必需控件可被绑定、位于实际布局中；失败不会修补或覆盖人工布局。 */
	bool ValidateExistingWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 先检查编译状态、生成类继承及根控件，再从可达布局核验三个绑定的类型与变量标志；不标脏、不重新编译或保存。
		if (!WidgetBlueprint || !WidgetBlueprint->GeneratedClass || !WidgetBlueprint->GeneratedClass->IsChildOf(UCatDayTransitionWidget::StaticClass())
			|| (WidgetBlueprint->Status != BS_UpToDate && WidgetBlueprint->Status != BS_UpToDateWithWarnings)
			|| !WidgetBlueprint->WidgetTree || !WidgetBlueprint->WidgetTree->RootWidget)
		{
			return false;
		}
		TArray<UWidget*> Reachable;
		WidgetBlueprint->WidgetTree->GetAllWidgets(Reachable);
		const TArray<UWidget*> Required = {
			Cast<UBorder>(WidgetBlueprint->WidgetTree->FindWidget(TEXT("BlackOverlay"))),
			Cast<UTextBlock>(WidgetBlueprint->WidgetTree->FindWidget(TEXT("ResultText"))),
			Cast<UTextBlock>(WidgetBlueprint->WidgetTree->FindWidget(TEXT("SettlementText")))
		};
		for (const UWidget* Widget : Required)
		{
			if (!Widget || !Widget->bIsVariable || !Reachable.Contains(Widget)) return false;
		}
		return true;
	}

	/** 编译新 WBP 后先登记资产再尝试保存，返回保存结果；保存失败不会撤销内存中的注册或脏标记。 */
	bool CompileRegisterAndSaveWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 保存流程：空蓝图直接失败；结构刷新前后各同步一次 GUID，再显式编译并核验可达绑定，不满足合同时停止。
		// 全部校验通过才标脏并通知资产注册表，再按 /Game 包路径保存且返回保存结果；保存失败保留内存对象与注册记录，不回滚或重试。
		if (!WidgetBlueprint)
		{
			return false;
		}
		SyncWidgetVariableGuids(WidgetBlueprint);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
		SyncWidgetVariableGuids(WidgetBlueprint);
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		if (!ValidateExistingWidget(WidgetBlueprint))
		{
			return false;
		}
		WidgetBlueprint->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(WidgetBlueprint);
		const FString Filename = FPackageName::LongPackageNameToFilename(
			WidgetBlueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(WidgetBlueprint->GetOutermost(), WidgetBlueprint, *Filename, SaveArgs);
	}
}

bool UCatDayTransitionAuthoringLibrary::CreateMissingDayTransitionWidgetBlueprint()
{
	// 编辑器入口流程：
	// 1. 磁盘或内存存在目标包时加载同名 WBP，检查编译状态、生成类与可达控件绑定；失败记录错误，既有布局不被改写。
	// 2. 目标包缺失才创建包和 Canvas 根，再构造首份视觉树；任一步失败保留已创建的内存内容，不自动删除。
	// 3. 布局完成后编译、登记并尝试保存；只有保存成功才返回 true 和成功日志，保存失败虽已登记仍报告失败，不保存地图或其他脏包。
	using namespace CatDayTransitionAuthoring;
	const FString PackageName = FString::Printf(TEXT("%s/%s"), *WidgetDirectory, WidgetAssetName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, WidgetAssetName);
	if (FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
	{
		UWidgetBlueprint* ExistingWidget = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		const bool bValid = ValidateExistingWidget(ExistingWidget);
		if (!bValid)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=day_transition_authoring_existing_contract_invalid Asset=%s"), *ObjectPath);
		}
		return bValid;
	}

	UPackage* Package = CreatePackage(*PackageName);
	UWidgetBlueprint* WidgetBlueprint = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(
		Package, FName(WidgetAssetName), BPTYPE_Normal, UCatDayTransitionWidget::StaticClass(), UCanvasPanel::StaticClass(),
		TEXT("CatDayTransitionAuthoring"), false);
	if (!WidgetBlueprint || !BuildDayTransitionWidget(WidgetBlueprint) || !CompileRegisterAndSaveWidget(WidgetBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("Event=day_transition_authoring_create_failed Asset=%s"), *PackageName);
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("Event=day_transition_authoring_created Asset=%s"), *ObjectPath);
	return true;
}
