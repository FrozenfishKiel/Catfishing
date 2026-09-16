#include "CatCollectionAuthoringLibrary.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

// 先只读核对父类、控件名单、绑定和事件连线；只有已知空壳才清旧树，重新编译成功后保存同一包。
// 外部引用不改路径；额外控件名、绑定、函数图或事件连线会阻止迁移；此预检不比较同名控件的全部属性。
// 断开根树后编译或保存失败不自动回滚内存，失败会话不可再手工保存为成功资产。
bool UCatCollectionAuthoringLibrary::MigrateCollectionWidget()
{
    auto* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/UI/Collection/WBP_CatCollection.WBP_CatCollection"));
    if (!Blueprint || Blueprint->ParentClass != UCatCollectionWidget::StaticClass() || !Blueprint->WidgetTree
        || !Blueprint->Bindings.IsEmpty() || !Blueprint->FunctionGraphs.IsEmpty()) return false;
    TArray<UWidget*> Widgets;
    Blueprint->WidgetTree->GetAllWidgets(Widgets);
    const TSet<FName> Known = {TEXT("CollectionRoot"), TEXT("SummaryTextBlock"), TEXT("EntriesTextBlock")};
    for (const UWidget* Widget : Widgets) if (!Known.Contains(Widget->GetFName())) return false;
    for (const UEdGraph* Graph : Blueprint->UbergraphPages)
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node->IsA<UK2Node_Event>()) return false;
            for (const UEdGraphPin* Pin : Node->Pins) if (!Pin->LinkedTo.IsEmpty()) return false;
        }
    Blueprint->WidgetTree->RootWidget = nullptr;
    Blueprint->WidgetVariableNameToGuidMap.Reset();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    // 结构失效可能从旧生成类回填变量 GUID，正式编译前再次按空源树同步，避免残留文本变量。
    Blueprint->WidgetVariableNameToGuidMap.Reset();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status != BS_UpToDate) return false;
    Blueprint->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint,
        *FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
    UE_LOG(LogTemp, Display, TEXT("Event=collection_legacy_layout_migrated RemovedWidgets=%d Saved=%d"), Widgets.Num(), bSaved);
    return bSaved;
}
