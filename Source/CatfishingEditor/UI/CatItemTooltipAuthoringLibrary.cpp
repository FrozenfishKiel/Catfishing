#include "CatItemTooltipAuthoringLibrary.h"

#include "Blueprint/BlueprintExtension.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UI/ItemTooltip/CatItemTooltipWidget.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"

namespace CatItemTooltipAuthoring
{
	/** 旧项目中 WBP 序列化的原生父类全名；只用于编辑器导入阶段把无法加载的历史类型映射到当前类型。 */
	const TCHAR* const LegacyParentClassName = TEXT("/Script/AegisOdyssey.AOItemHoverTooltipWidget");
	/** Catfishing 中承接旧 WBP 布局的唯一原生父类；收尾时显式写入它，避免保存结果继续依赖临时重定向。 */
	const TCHAR* const TargetParentClassName = TEXT("/Script/Catfishing.CatItemTooltipWidget");
	/** 原始 Aegis 设计器中详情说明的稳定控件名；新增实例详情必须紧接它，才能保留原有标题、图标和说明层级。 */
	const FName ItemDescriptionTextName(TEXT("ItemDescriptionText"));
	/** 新 runtime BindWidget 合同使用的稳定控件名；迁移后由主运行时实现写入每个物品实例的差异化信息。 */
	const FName InstanceDetailsTextName(TEXT("InstanceDetailsText"));

	/** 将目标 WBP 的顶层对象完整保存到 Content 路径；只有编译成功的副本才能跨下次编辑器启动继续使用。 */
	bool SaveTooltipWidget(UWidgetBlueprint* WidgetBlueprint)
	{
		// 保存流程：先标记本次父类和控件树修改，再按 /Game 包路径写出 .uasset；副本已由 AssetTools 登记，不能重复登记同一个资产。
		if (!WidgetBlueprint)
		{
			return false;
		}

		WidgetBlueprint->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			WidgetBlueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(WidgetBlueprint->GetOutermost(), WidgetBlueprint, *Filename, SaveArgs);
	}

	/** 为运行时实例详情创建空白的 TextBlock，并继承说明文本的排版约束；它不会写入示例文案，以免默认 Designer 内容冒充真实物品数据。 */
	UTextBlock* AddInstanceDetailsText(UWidgetBlueprint* WidgetBlueprint, UTextBlock* ItemDescriptionText, UVerticalBox* ParentBox)
	{
		// 控件补齐流程：先复制说明文本的完整排版，再断开副本的旧 Slot 引用，防止插入新行时影响原说明。
		// 随后清空示例文字、插到说明后一位并复制边距；继承完整文本样式也避免依赖 UE 未提供的 Justification getter。
		if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !ItemDescriptionText || !ParentBox)
		{
			return nullptr;
		}

		UTextBlock* InstanceDetailsText = DuplicateObject<UTextBlock>(
			ItemDescriptionText, WidgetBlueprint->WidgetTree, InstanceDetailsTextName);
		if (!InstanceDetailsText)
		{
			return nullptr;
		}

		InstanceDetailsText->Slot = nullptr;
		InstanceDetailsText->SetText(FText::GetEmpty());
		InstanceDetailsText->SetVisibility(ESlateVisibility::Collapsed);

		const int32 DescriptionIndex = ParentBox->GetChildIndex(ItemDescriptionText);
		ParentBox->InsertChildAt(DescriptionIndex + 1, InstanceDetailsText);
		if (UVerticalBoxSlot* SourceSlot = Cast<UVerticalBoxSlot>(ItemDescriptionText->Slot))
		{
			if (UVerticalBoxSlot* DetailsSlot = Cast<UVerticalBoxSlot>(InstanceDetailsText->Slot))
			{
				DetailsSlot->SetPadding(SourceSlot->GetPadding());
				DetailsSlot->SetHorizontalAlignment(SourceSlot->GetHorizontalAlignment());
				DetailsSlot->SetVerticalAlignment(SourceSlot->GetVerticalAlignment());
			}
		}

		FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable(WidgetBlueprint, InstanceDetailsText, true, false);
		if (!WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(InstanceDetailsTextName))
		{
			WidgetBlueprint->OnVariableAdded(InstanceDetailsTextName);
		}
		return InstanceDetailsText;
	}

	/** 清理旧 Aegis MVVM 和 Designer 属性绑定，避免复制后的 WBP 在新 MVC 父类下继续解析已经失效的 ViewModel 或旧属性路径。 */
	void RemoveLegacyViewModelArtifacts(UWidgetBlueprint* WidgetBlueprint)
	{
		// 旧绑定清理流程：先丢弃源 WBP 的 Designer 委托绑定并复位统计值，再按模块路径移除 Aegis 与 MVVM 扩展；普通 UMG 扩展不在匹配范围内，保持原样。
		if (!WidgetBlueprint)
		{
			return;
		}

		WidgetBlueprint->Bindings.Reset();
		WidgetBlueprint->PropertyBindings = 0;
		WidgetBlueprint->RemoveAllExtension([](UBlueprintExtension* Extension)
		{
			const FString ExtensionClassPath = Extension ? Extension->GetClass()->GetPathName() : FString();
			return ExtensionClassPath.Contains(TEXT("/Script/AegisOdyssey"))
				|| ExtensionClassPath.Contains(TEXT("/Script/ModelViewViewModel"));
		});
	}
}

bool UCatItemTooltipAuthoringLibrary::InstallLegacyParentRedirect()
{
	// 临时重定向安装流程：初始化 CoreRedirect 服务后只登记一次旧 Aegis 类到当前原生类的 Class 映射；映射存活于编辑器进程，既不写 DefaultEngine.ini 也不会进入运行时模块。
	using namespace CatItemTooltipAuthoring;
	static bool bRedirectInstalled = false;
	if (bRedirectInstalled)
	{
		return true;
	}

	FCoreRedirects::Initialize();
	const FCoreRedirect Redirect(ECoreRedirectFlags::Type_Class, LegacyParentClassName, TargetParentClassName);
	bRedirectInstalled = FCoreRedirects::AddRedirectList(MakeArrayView(&Redirect, 1), TEXT("CatItemTooltipAuthoringTemporaryRedirect"));
	if (!bRedirectInstalled)
	{
		UE_LOG(LogTemp, Error, TEXT("Event=item_tooltip_authoring_redirect_failed OldClass=%s NewClass=%s"), LegacyParentClassName, TargetParentClassName);
	}
	return bRedirectInstalled;
}

bool UCatItemTooltipAuthoringLibrary::FinalizeMigratedTooltipWidget(UObject* TooltipAsset)
{
	// 收尾流程：
	// 1. 确认迁移副本是可编辑的 WidgetBlueprint，并把 ParentClass 固定为当前 runtime 类。
	// 2. 清除源 WBP 的 Aegis/MVVM 扩展和 Designer 属性绑定，防止旧 ViewModel 在当前 MVC View 上复活。
	// 3. 从原始 WidgetTree 精确查找四个 BindWidget 合同；任一缺失立即失败，绝不重建或改写旧布局。
	// 4. 只在实例详情尚不存在时把它插到说明下方，并复制说明文本的排版与槽位约束。
	// 5. 编译后同时检查 Blueprint 状态与生成类父类，只有两者成立才保存，确保下次启动不再依赖临时 Aegis 重定向。
	using namespace CatItemTooltipAuthoring;
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(TooltipAsset);
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		UE_LOG(LogTemp, Error, TEXT("Event=item_tooltip_authoring_invalid_asset Asset=%s"), *GetNameSafe(TooltipAsset));
		return false;
	}

	WidgetBlueprint->ParentClass = UCatItemTooltipWidget::StaticClass();
	RemoveLegacyViewModelArtifacts(WidgetBlueprint);
	UWidget* RootBorder = WidgetBlueprint->WidgetTree->FindWidget(TEXT("RootBorder"));
	UWidget* ItemIconImage = WidgetBlueprint->WidgetTree->FindWidget(TEXT("ItemIconImage"));
	UWidget* ItemNameText = WidgetBlueprint->WidgetTree->FindWidget(TEXT("ItemNameText"));
	UTextBlock* ItemDescriptionText = Cast<UTextBlock>(WidgetBlueprint->WidgetTree->FindWidget(ItemDescriptionTextName));
	if (!RootBorder || !ItemIconImage || !ItemNameText || !ItemDescriptionText)
	{
		UE_LOG(LogTemp, Error, TEXT("Event=item_tooltip_authoring_source_contract_missing Asset=%s RootBorder=%d ItemIconImage=%d ItemNameText=%d ItemDescriptionText=%d"),
			*WidgetBlueprint->GetPathName(), RootBorder != nullptr, ItemIconImage != nullptr, ItemNameText != nullptr, ItemDescriptionText != nullptr);
		return false;
	}

	if (!WidgetBlueprint->WidgetTree->FindWidget(InstanceDetailsTextName))
	{
		UVerticalBox* DescriptionParent = Cast<UVerticalBox>(ItemDescriptionText->GetParent());
		if (!DescriptionParent || !AddInstanceDetailsText(WidgetBlueprint, ItemDescriptionText, DescriptionParent))
		{
			UE_LOG(LogTemp, Error, TEXT("Event=item_tooltip_authoring_details_insert_failed Asset=%s Parent=%s"),
				*WidgetBlueprint->GetPathName(), *GetNameSafe(ItemDescriptionText->GetParent()));
			return false;
		}
	}

	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
	if (WidgetBlueprint->Status == BS_Error
		|| !WidgetBlueprint->GeneratedClass
		|| !WidgetBlueprint->GeneratedClass->IsChildOf(UCatItemTooltipWidget::StaticClass())
		|| !SaveTooltipWidget(WidgetBlueprint))
	{
		UE_LOG(LogTemp, Error, TEXT("Event=item_tooltip_authoring_finalize_failed Asset=%s"), *WidgetBlueprint->GetPathName());
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("Event=item_tooltip_authoring_finalized Asset=%s Parent=%s"),
		*WidgetBlueprint->GetPathName(), *GetNameSafe(WidgetBlueprint->ParentClass));
	return true;
}
