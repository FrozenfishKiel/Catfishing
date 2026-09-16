#include "CatInventoryQuickbarAuthoringLibrary.h"

#include "AssetToolsModule.h"
#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "EnhancedActionKeyMapping.h"
#include "Factories/DataAssetFactory.h"
#include "Input/CatInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "../Inventory/CatInventoryActionsAuthoringLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "UI/Inventory/CatInventoryQuickbarWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "WidgetBlueprintFactory.h"

namespace CatInventoryQuickbarAuthoring
{
	/** 快捷栏唯一正式 WBP 路径；UI Settings 的默认软引用和作者器共同收口在这里。 */
	const TCHAR* const QuickbarPath = TEXT("/Game/UI/Inventory/WBP_CatInventoryQuickbar");
	/** 背包正式共享格路径；作者器只把早期快捷栏专属控件从这里定向移除，不能再向它装配快捷栏表现。 */
	const TCHAR* const SharedSlotPath = TEXT("/Game/UI/InventorySlot/WBP_CatInventorySlot");
	/** 快捷栏正式专用格路径；首次从共享格复制，随后外圈和数字提示只写入该资产。 */
	const TCHAR* const QuickbarSlotPath = TEXT("/Game/UI/InventorySlot/WBP_CatInventoryQuickbarSlot");
	/** 项目唯一玩法 IMC；作者器只编辑它，不创建第二份 Mapping Context。 */
	const TCHAR* const GameplayMappingContextPath = TEXT("/Game/Input/InputContext/IMC_InputContext.IMC_InputContext");
	/** Native Input Config 正式路径；Controller 遍历它把统一动作路由到 core 的 NativeInput tags。 */
	const TCHAR* const NativeInputConfigPath = TEXT("/Game/Data/Abilities/DA_CatAbilityInputConfig.DA_CatAbilityInputConfig");
	/** 默认能力集合正式路径；统一 G 接管 Scoop/Chum 后只从这里移除对应旧授予条目。 */
	const TCHAR* const DefaultAbilitySetPath = TEXT("/Game/Data/Abilities/DA_CatAbilitySet_Default.DA_CatAbilitySet_Default");

	/** 保存已编译/编辑的资产；保存失败返回 false，让 Python 脚本中断且保留可检查的 Editor 日志。 */
	bool SaveAsset(UObject* Asset)
	{
		if (!Asset) return false;
		Asset->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
	}

	/** 加载或创建 Bool 输入 Action；只在目标路径不存在时建立资产，存在资产保持其编辑器可见配置。 */
	UInputAction* LoadOrCreateInputAction(const TCHAR* AssetName)
	{
		const FString PackagePath = FString::Printf(TEXT("/Game/Input/InputAction/%s.%s"), AssetName, AssetName);
		if (UInputAction* Existing = LoadObject<UInputAction>(nullptr, *PackagePath)) return Existing;
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UInputAction::StaticClass();
		return Cast<UInputAction>(FAssetToolsModule::GetModule().Get().CreateAsset(AssetName, TEXT("/Game/Input/InputAction"), UInputAction::StaticClass(), Factory));
	}

	/** 把动作在 Native Config 中更新为目标 tag；同 tag 的旧 Action 会先删除，保证 Controller 只绑定一个统一入口。 */
	bool UpsertNativeInputAction(UCatInputConfig& Config, UInputAction* Action, const TCHAR* TagName)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
		if (!Action || !Tag.IsValid()) return false;
		Config.NativeInputActions.RemoveAll([Tag](const FCatNativeInputAction& Entry) { return Entry.InputTag.MatchesTagExact(Tag); });
		FCatNativeInputAction& Entry = Config.NativeInputActions.AddDefaulted_GetRef();
		Entry.InputAction = Action;
		Entry.InputTag = Tag;
		return true;
	}

	/** 将指定 Action 的全部现有键移除后写入唯一目标键；映射来自正式 IMC，运行时代码不 MapKey。 */
	void ReplaceActionMapping(UInputMappingContext& Context, UInputAction& Action, const FKey Key)
	{
		TArray<FEnhancedActionKeyMapping> ExistingMappings;
		for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
		{
			if (Mapping.Action == &Action) ExistingMappings.Add(Mapping);
		}
		for (const FEnhancedActionKeyMapping& Mapping : ExistingMappings) Context.UnmapKey(&Action, Mapping.Key);
		Context.MapKey(&Action, Key);
	}

	/** 在快捷栏专用 Slot 的 SizeBox 内建立一次 Overlay；原 child 先脱离 SizeBox 再作为底层加入，SizeBox 自身尺寸约束和旧 child 布局均保持不变。 */
	UOverlay* GetOrCreateSlotOverlay(UWidgetBlueprint& SlotBlueprint)
	{
		if (!SlotBlueprint.WidgetTree) return nullptr;
		if (UOverlay* ExistingOverlay = Cast<UOverlay>(SlotBlueprint.WidgetTree->FindWidget(TEXT("QuickbarSlotOverlay")))) return ExistingOverlay;
		USizeBox* SizeRoot = Cast<USizeBox>(SlotBlueprint.WidgetTree->RootWidget);
		if (!SizeRoot) return nullptr;
		UWidget* OriginalChild = SizeRoot->GetContent();
		if (!OriginalChild) return nullptr;
		UOverlay* Overlay = SlotBlueprint.WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("QuickbarSlotOverlay"));
		if (!Overlay) return nullptr;
		// UContentWidget 只能容纳一个 child：先释放旧 child，再把它作为 Overlay 第一层，最后将 Overlay 放回同一 SizeBox。
		SizeRoot->SetContent(nullptr);
		UOverlaySlot* OriginalSlot = Overlay->AddChildToOverlay(OriginalChild);
		if (!OriginalSlot || !SizeRoot->SetContent(Overlay)) return nullptr;
		return Overlay;
	}

	/**
	 * 为本作者器新增的命名控件补齐蓝图变量 GUID；只修复缺失条目，不改复制来源共享 Slot 的其它变量。
	 * 先读取已序列化的变量表，只有缺项才委托 WidgetBlueprint 生成 GUID；随后再次读取表并以该后置条件作为调用方是否允许编译的依据。
	 */
	bool RegisterNewQuickbarWidgetVariable(UWidgetBlueprint& SlotBlueprint, UWidget* Widget)
	{
		if (!Widget) return false;
		if (!SlotBlueprint.WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
		{
			SlotBlueprint.OnVariableAdded(Widget->GetFName());
			UE_LOG(LogTemp, Display, TEXT("Event=inventory_quickbar_authoring_widget_guid_repaired Asset=%s Widget=%s"), *GetNameSafe(&SlotBlueprint), *Widget->GetName());
		}
		return SlotBlueprint.WidgetVariableNameToGuidMap.Contains(Widget->GetFName());
	}

	/** 首次创建快捷栏专用格时复制共享资产；复制完成后两个 WBP 不共享 WidgetTree，后续装配不会回写背包格。 */
	UWidgetBlueprint* LoadOrDuplicateQuickbarSlot(UWidgetBlueprint& SharedSlot)
	{
		if (UWidgetBlueprint* Existing = LoadObject<UWidgetBlueprint>(nullptr, QuickbarSlotPath)) return Existing;
		return Cast<UWidgetBlueprint>(FAssetToolsModule::GetModule().Get().DuplicateAsset(
			TEXT("WBP_CatInventoryQuickbarSlot"), TEXT("/Game/UI/InventorySlot"), &SharedSlot));
	}

	/**
	 * 清理早期作者器写入共享背包格的三项快捷栏专属控件。
	 * 只接受 SizeBox -> Overlay[原 child, SelectedBorder, SlotKeyTextBlock] 的完整旧结构；任一名称或层级不符都拒绝，避免删除并行编辑的界面内容。
	 */
	bool RemoveLegacyQuickbarControlsFromSharedSlot(UWidgetBlueprint& SharedSlot, bool& bOutChanged)
	{
		bOutChanged = false;
		if (!SharedSlot.WidgetTree) return false;
		UWidget* OverlayWidget = SharedSlot.WidgetTree->FindWidget(TEXT("QuickbarSlotOverlay"));
		UWidget* BorderWidget = SharedSlot.WidgetTree->FindWidget(TEXT("SelectedBorder"));
		UWidget* KeyWidget = SharedSlot.WidgetTree->FindWidget(TEXT("SlotKeyTextBlock"));
		if (!OverlayWidget && !BorderWidget && !KeyWidget) return true;
		USizeBox* SizeRoot = Cast<USizeBox>(SharedSlot.WidgetTree->RootWidget);
		UOverlay* Overlay = Cast<UOverlay>(OverlayWidget);
		if (!SizeRoot || SizeRoot->GetContent() != Overlay || !Overlay || !BorderWidget || !KeyWidget
			|| Overlay->GetChildrenCount() != 3 || Overlay->GetChildAt(1) != BorderWidget || Overlay->GetChildAt(2) != KeyWidget)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_shared_slot_cleanup_rejected Asset=%s Root=%s Overlay=%s Border=%s Key=%s ChildCount=%d"),
				*GetNameSafe(&SharedSlot), *GetNameSafe(SizeRoot), *GetNameSafe(Overlay), *GetNameSafe(BorderWidget), *GetNameSafe(KeyWidget),
				Overlay ? Overlay->GetChildrenCount() : INDEX_NONE);
			return false;
		}
		UWidget* OriginalChild = Overlay->GetChildAt(0);
		if (!OriginalChild || OriginalChild == BorderWidget || OriginalChild == KeyWidget)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_shared_slot_cleanup_missing_original Asset=%s Overlay=%s"),
				*GetNameSafe(&SharedSlot), *GetNameSafe(Overlay));
			return false;
		}
		// 先取回原内容，再交给编辑器的正式删除入口清理三项新增控件及其变量。
		// 仅 RemoveWidget 会留下仍以 WidgetTree 为 Outer 的对象；编译器仍枚举它们，因此不能只删 GUID 和父子关系。
		Overlay->RemoveChild(OriginalChild);
		SizeRoot->SetContent(OriginalChild);
		FWidgetBlueprintEditorUtils::DeleteWidgets(&SharedSlot, { Overlay, BorderWidget, KeyWidget },
			FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
		bOutChanged = SizeRoot->GetContent() == OriginalChild
			&& !SharedSlot.WidgetTree->FindWidget(TEXT("QuickbarSlotOverlay"))
			&& !SharedSlot.WidgetTree->FindWidget(TEXT("SelectedBorder"))
			&& !SharedSlot.WidgetTree->FindWidget(TEXT("SlotKeyTextBlock"));
		if (!bOutChanged)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_shared_slot_cleanup_incomplete Asset=%s Original=%s"),
				*GetNameSafe(&SharedSlot), *GetNameSafe(OriginalChild));
		}
		return bOutChanged;
	}
}

// WBP 创建流程：
// 1. 不存在时创建底部 Canvas + 横向 WrapBox，Widget 只依赖稳定的 QuickbarSlotWrapBox 合同。
// 2. 已存在时校验父类和合同；仅把完全匹配旧生成默认值的快捷栏上移，人工布局保持不变。
// 3. 先从共享背包格复制快捷栏专用格，再只在专用格内装配数字与选中外圈；共享格只定向清理本轮早期写入的完整旧结构。
// 4. 三个 WBP 编译且合同成立后分别保存；后一个保存失败不会回滚前一个已保存资产，日志保留失败位置供作者器脚本修复后重试。
bool UCatInventoryQuickbarAuthoringLibrary::CreateOrValidateInventoryQuickbarWidgets()
{
	using namespace CatInventoryQuickbarAuthoring;
	UWidgetBlueprint* Quickbar = LoadObject<UWidgetBlueprint>(nullptr, QuickbarPath);
	UE_LOG(LogTemp, Display, TEXT("Event=inventory_quickbar_authoring_begin Quickbar=%s Found=%d"), QuickbarPath, Quickbar != nullptr);
	if (!Quickbar)
	{
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = UCatInventoryQuickbarWidget::StaticClass();
		Quickbar = Cast<UWidgetBlueprint>(FAssetToolsModule::GetModule().Get().CreateAsset(TEXT("WBP_CatInventoryQuickbar"), TEXT("/Game/UI/Inventory"), UWidgetBlueprint::StaticClass(), Factory));
		if (!Quickbar || !Quickbar->WidgetTree)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_create_failed Quickbar=%s Blueprint=%s WidgetTree=%d"), QuickbarPath, *GetNameSafe(Quickbar), Quickbar && Quickbar->WidgetTree != nullptr);
			return false;
		}
		UCanvasPanel* Root = Quickbar->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		UWrapBox* Slots = Quickbar->WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("QuickbarSlotWrapBox"));
		if (!Root || !Slots)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_widget_construct_failed Root=%d Slots=%d"), Root != nullptr, Slots != nullptr);
			return false;
		}
		Quickbar->WidgetTree->RootWidget = Root;
		UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Slots);
		Slot->SetAnchors(FAnchors(0.5f, 1.0f));
		Slot->SetAlignment(FVector2D(0.5f, 1.0f));
		Slot->SetPosition(FVector2D(0.0f, -110.0f));
		Slot->SetAutoSize(true);
		Slots->SetInnerSlotPadding(FVector2D(4.0f, 0.0f));
	}
	UWidgetBlueprint* SharedSlotBlueprint = LoadObject<UWidgetBlueprint>(nullptr, SharedSlotPath);
	if (!SharedSlotBlueprint || !SharedSlotBlueprint->WidgetTree)
	{
		UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_shared_slot_missing Asset=%s"), SharedSlotPath);
		return false;
	}
	UWidgetBlueprint* QuickbarSlotBlueprint = LoadOrDuplicateQuickbarSlot(*SharedSlotBlueprint);
	UE_LOG(LogTemp, Display, TEXT("Event=inventory_quickbar_authoring_slots_loaded Shared=%s Quickbar=%s SharedFound=%d QuickbarFound=%d"),
		SharedSlotPath, QuickbarSlotPath, SharedSlotBlueprint != nullptr, QuickbarSlotBlueprint != nullptr);
	if (!Quickbar || !Quickbar->ParentClass || !Quickbar->ParentClass->IsChildOf(UCatInventoryQuickbarWidget::StaticClass())
		|| !Quickbar->WidgetTree || !Quickbar->WidgetTree->FindWidget(TEXT("QuickbarSlotWrapBox")) || !QuickbarSlotBlueprint || !QuickbarSlotBlueprint->WidgetTree)
	{
		UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_contract_failed Quickbar=%s Parent=%s Tree=%d SlotContainer=%d SlotBlueprint=%s SlotTree=%d"),
			*GetNameSafe(Quickbar), Quickbar ? *GetNameSafe(Quickbar->ParentClass) : TEXT("None"), Quickbar && Quickbar->WidgetTree != nullptr,
			Quickbar && Quickbar->WidgetTree && Quickbar->WidgetTree->FindWidget(TEXT("QuickbarSlotWrapBox")) != nullptr,
			*GetNameSafe(QuickbarSlotBlueprint), QuickbarSlotBlueprint && QuickbarSlotBlueprint->WidgetTree != nullptr);
		return false;
	}
	// 复制必须先发生，确保新快捷栏格继承早期资产中已经可用的数字和外圈；随后才允许从共享背包格恢复原 child。
	bool bSharedSlotCleaned = false;
	if (!RemoveLegacyQuickbarControlsFromSharedSlot(*SharedSlotBlueprint, bSharedSlotCleaned)) return false;
	// 旧作者器把快捷栏生成在 -36px；只有锚点、对齐、自动尺寸和坐标都仍是该旧默认值时才迁移，避免覆盖人工调过的布局。
	UWrapBox* QuickbarSlots = Cast<UWrapBox>(Quickbar->WidgetTree->FindWidget(TEXT("QuickbarSlotWrapBox")));
	UCanvasPanelSlot* QuickbarCanvasSlot = QuickbarSlots ? Cast<UCanvasPanelSlot>(QuickbarSlots->Slot) : nullptr;
	const FAnchors OldGeneratedAnchors(0.5f, 1.0f);
	if (QuickbarCanvasSlot
		&& QuickbarCanvasSlot->GetAnchors().Minimum.Equals(OldGeneratedAnchors.Minimum)
		&& QuickbarCanvasSlot->GetAnchors().Maximum.Equals(OldGeneratedAnchors.Maximum)
		&& QuickbarCanvasSlot->GetAlignment().Equals(FVector2D(0.5f, 1.0f))
		&& QuickbarCanvasSlot->GetAutoSize()
		&& QuickbarCanvasSlot->GetPosition().Equals(FVector2D(0.0f, -36.0f)))
	{
		QuickbarCanvasSlot->SetPosition(FVector2D(0.0f, -110.0f));
		UE_LOG(LogTemp, Display, TEXT("Event=inventory_quickbar_authoring_layout_migrated Asset=%s Widget=%s PreviousY=-36 NewY=-110"),
			*GetNameSafe(Quickbar), *GetNameSafe(QuickbarSlots));
	}
	if (!QuickbarSlotBlueprint->WidgetTree->FindWidget(TEXT("SelectedBorder")))
	{
		UOverlay* SlotOverlay = GetOrCreateSlotOverlay(*QuickbarSlotBlueprint);
		if (!SlotOverlay)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_selected_border_root_unsupported Asset=%s RootClass=%s RootName=%s"),
				*GetNameSafe(QuickbarSlotBlueprint), *GetNameSafe(QuickbarSlotBlueprint->WidgetTree->RootWidget ? QuickbarSlotBlueprint->WidgetTree->RootWidget->GetClass() : nullptr),
				*GetNameSafe(QuickbarSlotBlueprint->WidgetTree->RootWidget));
			return false;
		}
		UBorder* SelectedBorder = QuickbarSlotBlueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SelectedBorder"));
		if (!SelectedBorder) return false;
		SelectedBorder->SetVisibility(ESlateVisibility::Collapsed);
		FSlateBrush SelectedBrush;
		SelectedBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		SelectedBrush.TintColor = FSlateColor(FLinearColor::Transparent);
		SelectedBrush.OutlineSettings.Width = 2.0f;
		SelectedBrush.OutlineSettings.CornerRadii = FVector4(6.0f, 6.0f, 6.0f, 6.0f);
		SelectedBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		SelectedBrush.OutlineSettings.Color = FSlateColor(FLinearColor(1.0f, 0.78f, 0.16f, 1.0f));
		SelectedBorder->SetBrush(SelectedBrush);
		UOverlaySlot* BorderSlot = SlotOverlay->AddChildToOverlay(SelectedBorder);
		if (!BorderSlot) return false;
		BorderSlot->SetHorizontalAlignment(HAlign_Fill);
		BorderSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (!QuickbarSlotBlueprint->WidgetTree->FindWidget(TEXT("SlotKeyTextBlock")))
	{
		UOverlay* SlotOverlay = GetOrCreateSlotOverlay(*QuickbarSlotBlueprint);
		if (!SlotOverlay)
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_slot_key_root_unsupported Asset=%s RootClass=%s RootName=%s"),
				*GetNameSafe(QuickbarSlotBlueprint), *GetNameSafe(QuickbarSlotBlueprint->WidgetTree->RootWidget ? QuickbarSlotBlueprint->WidgetTree->RootWidget->GetClass() : nullptr),
				*GetNameSafe(QuickbarSlotBlueprint->WidgetTree->RootWidget));
			return false;
		}
		UTextBlock* SlotKeyText = QuickbarSlotBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotKeyTextBlock"));
		if (!SlotKeyText) return false;
		SlotKeyText->SetText(FText::GetEmpty());
		SlotKeyText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		FSlateFontInfo SlotKeyFont = SlotKeyText->GetFont();
		SlotKeyFont.Size = 12;
		SlotKeyText->SetFont(SlotKeyFont);
		UOverlaySlot* KeySlot = SlotOverlay->AddChildToOverlay(SlotKeyText);
		if (!KeySlot) return false;
		KeySlot->SetHorizontalAlignment(HAlign_Left);
		KeySlot->SetVerticalAlignment(VAlign_Top);
		KeySlot->SetPadding(FMargin(6.0f, 4.0f, 0.0f, 0.0f));
	}
	// UE 5.8 不会为已存在 WBP 后插入的命名控件自动建立 GUID；先补注册再编译，避免编译器将 WidgetTree 与变量表判为不一致。
	UWidget* const SlotOverlay = QuickbarSlotBlueprint->WidgetTree->FindWidget(TEXT("QuickbarSlotOverlay"));
	UWidget* const SelectedBorder = QuickbarSlotBlueprint->WidgetTree->FindWidget(TEXT("SelectedBorder"));
	UWidget* const SlotKeyText = QuickbarSlotBlueprint->WidgetTree->FindWidget(TEXT("SlotKeyTextBlock"));
	const bool bRegisteredSlotOverlay = RegisterNewQuickbarWidgetVariable(*QuickbarSlotBlueprint, SlotOverlay);
	const bool bRegisteredSelectedBorder = RegisterNewQuickbarWidgetVariable(*QuickbarSlotBlueprint, SelectedBorder);
	const bool bRegisteredSlotKey = RegisterNewQuickbarWidgetVariable(*QuickbarSlotBlueprint, SlotKeyText);
	if (!bRegisteredSlotOverlay || !bRegisteredSelectedBorder || !bRegisteredSlotKey)
	{
		UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_widget_guid_registration_failed Asset=%s Overlay=%d Border=%d Key=%d"),
			*GetNameSafe(QuickbarSlotBlueprint), bRegisteredSlotOverlay, bRegisteredSelectedBorder, bRegisteredSlotKey);
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(QuickbarSlotBlueprint);
	if (bSharedSlotCleaned) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(SharedSlotBlueprint);
	FKismetEditorUtilities::CompileBlueprint(Quickbar);
	FKismetEditorUtilities::CompileBlueprint(SharedSlotBlueprint);
	FKismetEditorUtilities::CompileBlueprint(QuickbarSlotBlueprint);
	const bool bQuickbarValid = Quickbar->Status != BS_Error && Quickbar->GeneratedClass && Quickbar->GeneratedClass->IsChildOf(UCatInventoryQuickbarWidget::StaticClass());
	const bool bSharedSlotValid = SharedSlotBlueprint->Status != BS_Error && SharedSlotBlueprint->GeneratedClass && SharedSlotBlueprint->GeneratedClass->IsChildOf(UCatInventorySlotWidget::StaticClass());
	const bool bQuickbarSlotValid = QuickbarSlotBlueprint->Status != BS_Error && QuickbarSlotBlueprint->GeneratedClass && QuickbarSlotBlueprint->GeneratedClass->IsChildOf(UCatInventorySlotWidget::StaticClass());
	const bool bSavedQuickbar = bQuickbarValid && SaveAsset(Quickbar);
	const bool bSavedSharedSlot = bSharedSlotValid && SaveAsset(SharedSlotBlueprint);
	const bool bSavedQuickbarSlot = bQuickbarSlotValid && SaveAsset(QuickbarSlotBlueprint);
	if (bSavedQuickbar && bSavedSharedSlot && bSavedQuickbarSlot)
	{
		UE_LOG(LogTemp, Display, TEXT("Event=inventory_quickbar_authoring_complete QuickbarValid=%d SharedSlotValid=%d QuickbarSlotValid=%d SavedQuickbar=%d SavedSharedSlot=%d SavedQuickbarSlot=%d"),
			bQuickbarValid, bSharedSlotValid, bQuickbarSlotValid, bSavedQuickbar, bSavedSharedSlot, bSavedQuickbarSlot);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Event=inventory_quickbar_authoring_complete QuickbarValid=%d SharedSlotValid=%d QuickbarSlotValid=%d SavedQuickbar=%d SavedSharedSlot=%d SavedQuickbarSlot=%d"),
			bQuickbarValid, bSharedSlotValid, bQuickbarSlotValid, bSavedQuickbar, bSavedSharedSlot, bSavedQuickbarSlot);
	}
	return bSavedQuickbar && bSavedSharedSlot && bSavedQuickbarSlot;
}

// 输入资产迁移流程：
// 1. 读取唯一 IMC 和 Native Config，缺少任一正式资产直接拒绝，避免生成第二套运行时入口。
// 2. 创建或复用八个选择/使用/丢弃 Action，并精确替换这些 Action 的键映射。
// 3. 移除旧 Q/R/F、G 使用及 X 持续剪线映射；R 架竿、X 收竿，左键沿按下时的统一路由使用选中物品。
// 4. 将 Action/tag 对写入 Native Config，删除 RodInteract/Scoop/Chum 的旧 AbilityInputActions，并从默认 AbilitySet 移除 Scoop/Chum 授予。
// 5. 调用既有库存动作迁移为 Chum 资产补齐 Use；所有资产保存成功才报告本次迁移完成。
bool UCatInventoryQuickbarAuthoringLibrary::MigrateBackpackQuickbarInputAssets()
{
	using namespace CatInventoryQuickbarAuthoring;
	UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, GameplayMappingContextPath);
	UCatInputConfig* Config = LoadObject<UCatInputConfig>(nullptr, NativeInputConfigPath);
	UCatAbilitySet* AbilitySet = LoadObject<UCatAbilitySet>(nullptr, DefaultAbilitySetPath);
	if (!Context || !Config || !AbilitySet) return false;
	struct FInputMigration { const TCHAR* Name; const TCHAR* Tag; FKey Key; };
	const FInputMigration Migrations[] = {
		{ TEXT("IA_SelectInventorySlot1"), TEXT("Cat.Input.Inventory.SelectSlot1"), EKeys::One },
		{ TEXT("IA_SelectInventorySlot2"), TEXT("Cat.Input.Inventory.SelectSlot2"), EKeys::Two },
		{ TEXT("IA_SelectInventorySlot3"), TEXT("Cat.Input.Inventory.SelectSlot3"), EKeys::Three },
		{ TEXT("IA_SelectInventorySlot4"), TEXT("Cat.Input.Inventory.SelectSlot4"), EKeys::Four },
		{ TEXT("IA_SelectPreviousInventorySlot"), TEXT("Cat.Input.Inventory.SelectPreviousSlot"), EKeys::MouseScrollUp },
		{ TEXT("IA_SelectNextInventorySlot"), TEXT("Cat.Input.Inventory.SelectNextSlot"), EKeys::MouseScrollDown },
		{ TEXT("IA_ParkHeldFishingRod"), TEXT("Cat.Input.Inventory.ParkRod"), EKeys::R },
		{ TEXT("IA_PackHeldFishingRod"), TEXT("Cat.Input.Inventory.PackRod"), EKeys::X },
		{ TEXT("IA_DropCarriedItem"), TEXT("Cat.Input.DropCarriedItem"), EKeys::Q }};
	for (const FInputMigration& Migration : Migrations)
	{
		UInputAction* Action = LoadOrCreateInputAction(Migration.Name);
		if (!Action || !UpsertNativeInputAction(*Config, Action, Migration.Tag)) return false;
		ReplaceActionMapping(*Context, *Action, Migration.Key);
		if (!SaveAsset(Action)) return false;
	}
	const TCHAR* const LegacyActions[] = { TEXT("IA_BaitSpot"), TEXT("IA_PutDownFishingRod"), TEXT("IA_CatchFish"), TEXT("IA_CancelFishing"), TEXT("IA_UseSelectedInventoryItem") };
	const FKey LegacyKeys[] = { EKeys::Q, EKeys::R, EKeys::F, EKeys::X, EKeys::G };
	Config->NativeInputActions.RemoveAll([](const FCatNativeInputAction& Entry) { return Entry.InputTag.MatchesTagExact(FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Inventory.UseSelectedItem"))); });
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(LegacyActions); ++Index)
	{
		const FString LegacyPath = FString::Printf(TEXT("/Game/Input/InputAction/%s.%s"), LegacyActions[Index], LegacyActions[Index]);
		if (UInputAction* LegacyAction = LoadObject<UInputAction>(nullptr, *LegacyPath)) Context->UnmapKey(LegacyAction, LegacyKeys[Index]);
	}
	// 旧鱼竿交互、抄网和打窝 GAS 输入已由选中物品的统一入口替代；Primary、Slack、Cancel 仍是部署后内部交互，不能删除。
	if (UCatAbilityInputConfig* AbilityConfig = Cast<UCatAbilityInputConfig>(Config))
	{
		const FGameplayTag LegacyRodInteract = FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Fishing.RodInteract"), false);
		const FGameplayTag LegacyScoop = FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Fishing.Scoop"), false);
		const FGameplayTag LegacyChum = FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Fishing.Chum"), false);
		AbilityConfig->AbilityInputActions.RemoveAll([LegacyRodInteract, LegacyScoop, LegacyChum](const FCatAbilityInputAction& Entry)
		{
			return Entry.InputTag.MatchesTagExact(LegacyRodInteract) || Entry.InputTag.MatchesTagExact(LegacyScoop) || Entry.InputTag.MatchesTagExact(LegacyChum);
		});
	}
	const FGameplayTag LegacyScoop = FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Fishing.Scoop"), false);
	const FGameplayTag LegacyChum = FGameplayTag::RequestGameplayTag(TEXT("Cat.Input.Fishing.Chum"), false);
	AbilitySet->GrantedAbilities.RemoveAll([LegacyScoop, LegacyChum](const FCatAbilitySetAbility& Entry)
	{
		return Entry.InputTag.MatchesTagExact(LegacyScoop) || Entry.InputTag.MatchesTagExact(LegacyChum);
	});
	return SaveAsset(Context) && SaveAsset(Config) && SaveAsset(AbilitySet)
		&& UCatInventoryActionsAuthoringLibrary::MigrateFormalInventoryDefinitionActions();
}
