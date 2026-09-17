#include "CatCampInventoryAuthoringLibrary.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Variable.h"
#include "UObject/UObjectHash.h"

// 先限制目标路径，再比较可达树与模板对象；迁移已核实的嵌套背包背景引用，其他变量节点或属性绑定仍用到的旧控件不删。
// 有待删模板且资产含动画时保守拒绝；其余对象经编辑器正式删除后重建 GUID 并编译，失败由脚本中止保存。
bool UCatCampInventoryAuthoringLibrary::FinalizeCampLayout(UBlueprint* Blueprint)
{
	auto* BP = Cast<UWidgetBlueprint>(Blueprint);
	const TSet<FString> Allowed = {TEXT("/Game/UI/Inventory/WBP_CatCampInventory"), TEXT("/Game/UI/Inventory/WBP_CatCampLoadout"),
		TEXT("/Game/UI/InventorySlot/WBP_CatCampLoadoutSlot"), TEXT("/Game/UI/InventorySlot/WBP_CatCampStorageSlot")};
	if (!BP || !BP->WidgetTree || !Allowed.Contains(BP->GetOutermost()->GetName())) return false;
	TArray<UWidget*> Reachable;
	BP->WidgetTree->GetAllWidgets(Reachable);
	TArray<UObject*> Objects;
	GetObjectsWithOuter(BP->WidgetTree, Objects, false);
	TSet<UWidget*> Detached;
	for (auto* Object : Objects) if (auto* Widget = Cast<UWidget>(Object); Widget && !Reachable.Contains(Widget)) Detached.Add(Widget);
	TArray<UK2Node_Variable*> Variables;
	FBlueprintEditorUtils::GetAllNodesOfClass(BP, Variables);
	if (BP->GetName() == TEXT("WBP_CatCampLoadout"))
		if (auto* Background = BP->WidgetTree->FindWidget(TEXT("BackPackBackGround"))) Background->bIsVariable = true;
	// 正式团队页的旧图表读取嵌套背包；换成专属四格外观后仍指向同一职责的控件，保留后续图表连线。
	if (BP->GetName() == TEXT("WBP_CatCampInventory") && BP->WidgetTree->FindWidget(TEXT("CampLoadout")))
	{
		BP->WidgetTree->FindWidget(TEXT("CampLoadout"))->bIsVariable = true;
		for (auto* Node : Variables) if (Node->VariableReference.GetMemberName() == TEXT("WBP_CatInventory"))
		{
			// 保留原输出连线并同步引脚名，避免重建节点把已连接旧引脚当作孤立输入。
			for (auto* Pin : Node->Pins) if (Pin->PinName == TEXT("WBP_CatInventory"))
			{
				UClass* LoadoutClass = BP->WidgetTree->FindWidget(TEXT("CampLoadout"))->GetClass();
				// 原团队页读取嵌套背包的背景 Border 调整外观；专属页保留同名透明 Border，迁移成员宿主而不改后续连线。
				for (auto* Linked : Pin->LinkedTo)
				{
					auto* Member = Cast<UK2Node_Variable>(Linked->GetOwningNode());
					if (!Member || Member->VariableReference.GetMemberName() != TEXT("BackPackBackGround")
						|| !LoadoutClass->FindPropertyByName(TEXT("BackPackBackGround"))) return false;
					Member->VariableReference.SetExternalMember(TEXT("BackPackBackGround"), LoadoutClass);
					Linked->PinType.PinSubCategoryObject = LoadoutClass;
				}
				Pin->PinName = TEXT("CampLoadout");
				Pin->PinType.PinSubCategoryObject = LoadoutClass;
			}
			Node->VariableReference.SetSelfMember(TEXT("CampLoadout"));
			UE_LOG(LogTemp, Display, TEXT("Event=camp_layout_loadout_reference_migrated Node=%s"), *Node->GetName());
		}
	}
	for (auto* Widget : Detached)
	{
		if (!BP->Animations.IsEmpty()) return false;
		for (const auto& Binding : BP->Bindings) if (Binding.ObjectName == Widget->GetName()) return false;
		for (auto* Node : Variables) if (Node->VariableReference.GetMemberName() == Widget->GetFName())
		{
			UE_LOG(LogTemp, Error, TEXT("Event=camp_layout_cleanup_rejected Asset=%s Widget=%s Reason=GraphReference"), *BP->GetName(), *Widget->GetName());
			return false;
		}
	}
	if (!Detached.IsEmpty()) FWidgetBlueprintEditorUtils::DeleteWidgets(BP, Detached, FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
	BP->WidgetVariableNameToGuidMap.Reset();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	BP->WidgetVariableNameToGuidMap.Reset();
	FBlueprintEditorUtils::RefreshAllNodes(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	UE_LOG(LogTemp, Display, TEXT("Event=camp_layout_finalized Asset=%s Detached=%d Compiled=%d"), *BP->GetName(), Detached.Num(), BP->Status != BS_Error);
	return BP->Status != BS_Error;
}
