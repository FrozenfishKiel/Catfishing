#include "CatInventoryContextMenuAuthoringLibrary.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Algo/Find.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Widget.h"
#include "WidgetBlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_CallFunction.h"
#include "EdGraph/EdGraph.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UI/Inventory/CatInventoryContextMenuWidget.h"
#include "WidgetBlueprint.h"

namespace CatInventoryContextMenuAuthoring
{
	/** 唯一正式菜单资产路径；运行时设置和作者器都只认这一个路径。 */
	const TCHAR* const ContextMenuPath = TEXT("/Game/UI/Inventory/WBP_CatInventoryContextMenu");
	/** 需要迁移的正式库存页；普通背包、营地和鱼护都共用同一菜单而不是各自保留固定按钮。 */
	const TCHAR* const InventoryWidgetPaths[] = {
		TEXT("/Game/UI/Inventory/WBP_CatInventory"), TEXT("/Game/UI/Inventory/WBP_CatCampInventory"), TEXT("/Game/UI/Inventory/WBP_CatFishGuardInventory")};
	/** 旧固定物品操作、数量面板、库存关闭按钮及鱼护单鱼出售控件名；全部出售容器入口不在本清单。 */
	const FName ObsoleteWidgetNames[] = {
		TEXT("CloseButton"), TEXT("ConsumeFishButton"), TEXT("DropButton"), TEXT("PlaceButton"), TEXT("CarryButton"), TEXT("ReleaseQuantityPanel"),
		TEXT("ReleaseQuantitySpinBox"), TEXT("ReleaseQuantityConfirmButton"), TEXT("ReleaseQuantityCancelButton"), TEXT("SellFishButton"), TEXT("SellFishPriceText")};
	/** 已替换的固定操作及 RPC；作者器遇到仍在使用的图节点会拒绝迁移，不机械删除玩家的执行链。 */
	const FName ObsoleteFunctionNames[] = {
		TEXT("RequestUseItem"), TEXT("RequestUseSelectedItem"), TEXT("RequestDropSelectedItem"), TEXT("RequestPlaceSelectedItem"),
		TEXT("RequestCarrySelectedFish"), TEXT("HandleConsumeClicked"), TEXT("HandleDropClicked"), TEXT("HandlePlaceClicked"),
		TEXT("HandleReleaseQuantityConfirmed"), TEXT("HandleReleaseQuantityCancelled"), TEXT("BeginReleaseSelectedItem"),
		TEXT("ServerUseInventoryItem"), TEXT("ServerUseInventoryItemFromHost"), TEXT("ServerReleaseInventoryItemToWorld")};

	/** 保存已编译 WBP；作者器只在编译和父类合同都正确后落盘，避免留下半迁移资产。 */
	bool SaveWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint)
	{
		if (!WidgetBlueprint) { return false; }
		WidgetBlueprint->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(WidgetBlueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone; SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(WidgetBlueprint->GetOutermost(), WidgetBlueprint, *Filename, SaveArgs);
	}

	/** 校验菜单根和四个运行时 BindWidget 节点；运行时菜单只依赖这些稳定名称，布局仍由正式 WBP 维护。 */
	bool HasContextMenuContract(const UWidgetBlueprint* WidgetBlueprint)
	{
		return WidgetBlueprint && WidgetBlueprint->ParentClass && WidgetBlueprint->ParentClass->IsChildOf(UCatInventoryContextMenuWidget::StaticClass())
			&& WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->FindWidget(TEXT("ActionList"))
			&& WidgetBlueprint->WidgetTree->FindWidget(TEXT("QuantityPanel")) && WidgetBlueprint->WidgetTree->FindWidget(TEXT("QuantitySpinBox"))
			&& WidgetBlueprint->WidgetTree->FindWidget(TEXT("QuantityConfirmButton")) && WidgetBlueprint->WidgetTree->FindWidget(TEXT("QuantityCancelButton"));
	}

	/** 扫描旧固定操作调用；发现仍有图引用时拒绝保存迁移，避免机械删节点破坏执行链。 */
	bool HasObsoleteActionGraphNodes(UBlueprint* WidgetBlueprint)
	{
		if (!WidgetBlueprint) { return false; }
		auto GraphHasObsoleteNode = [](const UEdGraph* Graph)
		{
			if (!Graph) { return false; }
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
				if (CallNode && Algo::Find(MakeArrayView(ObsoleteFunctionNames), CallNode->FunctionReference.GetMemberName())) { return true; }
			}
			return false;
		};
		TArray<UEdGraph*> Graphs;
		WidgetBlueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs) { if (GraphHasObsoleteNode(Graph)) { return true; } }
		return false;
	}
}

// 菜单创建流程：目标不存在时用编辑器工厂建立唯一 WBP 和最小控件合同；存在时绝不覆盖人工排版，只验证并重新编译保存。
bool UCatInventoryContextMenuAuthoringLibrary::CreateOrValidateInventoryContextMenuWidget()
{
	using namespace CatInventoryContextMenuAuthoring;
	UWidgetBlueprint* MenuBlueprint = LoadObject<UWidgetBlueprint>(nullptr, ContextMenuPath);
	if (!MenuBlueprint)
	{
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = UCatInventoryContextMenuWidget::StaticClass();
		MenuBlueprint = Cast<UWidgetBlueprint>(FAssetToolsModule::GetModule().Get().CreateAsset(
			TEXT("WBP_CatInventoryContextMenu"), TEXT("/Game/UI/Inventory"), UWidgetBlueprint::StaticClass(), Factory));
		if (!MenuBlueprint || !MenuBlueprint->WidgetTree) { return false; }
		UVerticalBox* Root = MenuBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Root"));
		UVerticalBox* ActionList = MenuBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ActionList"));
		UVerticalBox* QuantityPanel = MenuBlueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("QuantityPanel"));
		USpinBox* QuantitySpinBox = MenuBlueprint->WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), TEXT("QuantitySpinBox"));
		UButton* ConfirmButton = MenuBlueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("QuantityConfirmButton"));
		UButton* CancelButton = MenuBlueprint->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("QuantityCancelButton"));
		UTextBlock* ConfirmText = MenuBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuantityConfirmText"));
		UTextBlock* CancelText = MenuBlueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QuantityCancelText"));
		if (!Root || !ActionList || !QuantityPanel || !QuantitySpinBox || !ConfirmButton || !CancelButton || !ConfirmText || !CancelText) { return false; }
		ConfirmText->SetText(NSLOCTEXT("CatInventory", "Confirm", "确认")); CancelText->SetText(NSLOCTEXT("CatInventory", "Cancel", "取消"));
		ConfirmButton->AddChild(ConfirmText); CancelButton->AddChild(CancelText);
		MenuBlueprint->WidgetTree->RootWidget = Root;
		Root->AddChildToVerticalBox(ActionList); Root->AddChildToVerticalBox(QuantityPanel);
		QuantityPanel->AddChildToVerticalBox(QuantitySpinBox); QuantityPanel->AddChildToVerticalBox(ConfirmButton); QuantityPanel->AddChildToVerticalBox(CancelButton);
	}
	FKismetEditorUtilities::CompileBlueprint(MenuBlueprint);
	if (!HasContextMenuContract(MenuBlueprint) || MenuBlueprint->Status == BS_Error || !MenuBlueprint->GeneratedClass) { return false; }
	return SaveWidgetBlueprint(MenuBlueprint);
}

// 迁移流程：逐个加载三个正式库存页，精确删除废弃命名控件（含关闭按钮），保留现有格子和鱼护全部出售控件，然后编译保存。
bool UCatInventoryContextMenuAuthoringLibrary::MigrateInventoryActionWidgets()
{
	using namespace CatInventoryContextMenuAuthoring;
	bool bSucceeded = true;
	// 删除原生旧入口前审计项目全部蓝图图引用，包含格子、派生页及嵌套图；未知消费者直接报告资产路径。
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.Get().GetAssets(Filter, Assets);
	for (const FAssetData& Asset : Assets)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
		if (!Blueprint || HasObsoleteActionGraphNodes(Blueprint))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=inventory_legacy_action_reference Asset=%s"), *Asset.GetObjectPathString());
			bSucceeded = false;
		}
	}
	if (!bSucceeded) return false;
	UE_LOG(LogTemp, Display, TEXT("Event=inventory_legacy_action_audit Blueprints=%d References=0"), Assets.Num());
	for (const TCHAR* WidgetPath : InventoryWidgetPaths)
	{
		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, WidgetPath);
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree) { bSucceeded = false; continue; }
		for (const FName WidgetName : ObsoleteWidgetNames)
		{
			// Designer 控件删除必须同步移除变量 GUID，否则后续载入编译仍认为旧控件存在。
			WidgetBlueprint->WidgetVariableNameToGuidMap.Remove(WidgetName);
			if (UWidget* ObsoleteWidget = WidgetBlueprint->WidgetTree->FindWidget(WidgetName)) { WidgetBlueprint->WidgetTree->RemoveWidget(ObsoleteWidget); }
		}
		if (HasObsoleteActionGraphNodes(WidgetBlueprint)) { bSucceeded = false; continue; }
		FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
		bSucceeded &= WidgetBlueprint->Status != BS_Error && SaveWidgetBlueprint(WidgetBlueprint);
	}
	return bSucceeded;
}
