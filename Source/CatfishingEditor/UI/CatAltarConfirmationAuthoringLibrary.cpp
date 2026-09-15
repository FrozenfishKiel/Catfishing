#include "CatAltarConfirmationAuthoringLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UI/Run/CatAltarConfirmationWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"

// 首先读取既有资产并检查七个可达绑定；已有布局只验证，不覆盖设计者调整。
// 资产缺失时创建顶部面板和中文文本，再编译、核验并保存；只写本窗口包，不拼运行时替身。
bool UCatAltarConfirmationAuthoringLibrary::CreateMissingAltarConfirmationWidgetBlueprint()
{
	const FString PackageName = TEXT("/Game/UI/Run/WBP_CatAltarConfirmation");
	const FString ObjectPath = PackageName + TEXT(".WBP_CatAltarConfirmation");
	const TArray<FName> TextNames = { TEXT("InitiatorTextBlock"), TEXT("CountdownTextBlock"),
		TEXT("ConfirmationCountTextBlock"), TEXT("OwnConfirmationTextBlock"), TEXT("CancelReasonTextBlock") };
	// 同一绑定校验用于已有资产和新建资产；必须在真实布局树内且类型吻合，不能只靠生成类存在判成功。
	const auto Validate = [&TextNames](UWidgetBlueprint* Blueprint)
	{
		if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(UCatAltarConfirmationWidget::StaticClass())
			|| (Blueprint->Status != BS_UpToDate && Blueprint->Status != BS_UpToDateWithWarnings)
			|| !Blueprint->WidgetTree || !Blueprint->WidgetTree->RootWidget) return false;
		TArray<UWidget*> Reachable;
		Blueprint->WidgetTree->GetAllWidgets(Reachable);
		UBorder* Panel = Cast<UBorder>(Blueprint->WidgetTree->FindWidget(TEXT("ConfirmationPanel")));
		if (!Panel || !Panel->bIsVariable || !Reachable.Contains(Panel)) return false;
		UTextBlock* Hints = Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(TEXT("InputHintsTextBlock")));
		if (!Hints || !Hints->bIsVariable || !Reachable.Contains(Hints)) return false;
		for (FName Name : TextNames)
		{
			UTextBlock* Text = Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(Name));
			if (!Text || !Text->bIsVariable || !Reachable.Contains(Text)) return false;
		}
		return true;
	};
	if (FPackageName::DoesPackageExist(PackageName))
	{
		const bool bValid = Validate(LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath));
		UE_LOG(LogTemp, Display, TEXT("Event=AltarConfirmationAssetChecked Asset=%s Valid=%d"), *ObjectPath, bValid);
		return bValid;
	}
	UObject* Font = LoadObject<UObject>(nullptr, TEXT("/Game/UI/Shop/F_CatShopChinese.F_CatShopChinese"));
	if (!Font) return false;
	UPackage* Package = CreatePackage(*PackageName);
	UWidgetBlueprint* Blueprint = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(Package,
		TEXT("WBP_CatAltarConfirmation"), BPTYPE_Normal, UCatAltarConfirmationWidget::StaticClass(),
		UCanvasPanel::StaticClass(), TEXT("CatAltarConfirmationAuthoring"), false);
	if (!Blueprint || !Blueprint->WidgetTree) return false;
	UWidgetTree* Tree = Blueprint->WidgetTree;
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(Tree->RootWidget);
	if (!Canvas) return false;
	UBorder* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConfirmationPanel"));
	Panel->SetBrushColor(FLinearColor(0.035f, 0.055f, 0.07f, 0.96f));
	Panel->SetPadding(FMargin(28.0f, 16.0f));
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.0f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.0f));
	PanelSlot->SetPosition(FVector2D(0.0f, 24.0f));
	// 为长玩家名的两行标题和底部快捷键留足高度；小尺寸 PIE 窗口的 DPI 缩放后也不能把提示挤出背景。
	PanelSlot->SetSize(FVector2D(760.0f, 300.0f));
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConfirmationColumn"));
	Panel->SetContent(Column);
	const TArray<FText> Labels = { FText::FromString(TEXT("准备献祭并结束今天")), FText::FromString(TEXT("剩余 30 秒")),
		FText::FromString(TEXT("已确认 0 / 0")), FText::FromString(TEXT("你的状态：未确认")), FText::GetEmpty() };
	for (int32 Index = 0; Index < TextNames.Num(); ++Index)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TextNames[Index]);
		Text->SetText(Labels[Index]);
		Text->SetFont(FSlateFontInfo(Font, Index == 0 ? 24 : 18, TEXT("Regular")));
		Text->SetColorAndOpacity(Index == 1 ? FLinearColor(1.0f, 0.82f, 0.4f) : FLinearColor(0.94f, 0.96f, 0.98f));
		Text->SetJustification(ETextJustify::Center);
		Text->SetAutoWrapText(true);
		Column->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.0f, 2.0f));
	}
	UTextBlock* Hints = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InputHintsTextBlock"));
	Hints->SetText(FText::FromString(TEXT("F8 确认    F9 撤回确认")));
	Hints->SetFont(FSlateFontInfo(Font, 18, TEXT("Regular")));
	Hints->SetJustification(ETextJustify::Center);
	Hints->SetColorAndOpacity(FLinearColor(0.6f, 0.85f, 0.8f));
	Column->AddChildToVerticalBox(Hints)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	// 登记源控件 GUID 和必需变量，保持 UMG 编译绑定与运行时成员一致。
	Blueprint->ForEachSourceWidget([Blueprint, &TextNames](UWidget* Widget)
	{
		if (Widget->GetFName() == TEXT("ConfirmationPanel") || Widget->GetFName() == TEXT("InputHintsTextBlock") || TextNames.Contains(Widget->GetFName()))
			FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(Blueprint, Widget, true, false);
		if (!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName())) Blueprint->OnVariableAdded(Widget->GetFName());
	});
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (!Validate(Blueprint)) return false;
	FAssetRegistryModule::AssetCreated(Blueprint);
	Blueprint->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, Blueprint,
		*FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs);
	UE_LOG(LogTemp, Display, TEXT("Event=AltarConfirmationAssetCreated Asset=%s Saved=%d"), *ObjectPath, bSaved);
	return bSaved;
}
