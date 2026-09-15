#include "CatShopKioskAuthoringLibrary.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractionSettings.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

/** 资产迁移的独立日志分类；运行脚本以 Event 和 Asset 为过滤键，便于从 Editor 日志定位未迁移或保存失败的原因。 */
DEFINE_LOG_CATEGORY_STATIC(LogCatShopKioskAuthoring, Log, All);

namespace CatShopKioskAuthoring
{
	/** 正式商店摊位蓝图的唯一稳定包路径；迁移器只修改这个用户可见的运行入口，避免扫描并误改测试或原型资产。 */
	const TCHAR* const FormalKioskPath = TEXT("/Game/UI/Shop/BP_CatShopKiosk.BP_CatShopKiosk");
	/** 已移除的原生球体组件名；旧蓝图节点以此作为父节点时必须改挂空间根，防止删除后丢失可见模型。 */
	const FName DeprecatedCollisionName(TEXT("InteractionCollision"));
	/** 原生空间根名称；原来依附旧球体的蓝图组件迁移到此根，保持原相对变换和关卡摆放结果。 */
	const FName SceneRootName(TEXT("SceneRoot"));

	/** 判断原始组件是否确实承载可见的静态或骨骼模型；空组件、辅助碰撞和隐藏模型不能替代玩家实际瞄准的表面。 */
	bool IsVisibleModelComponent(const UPrimitiveComponent* Component)
	{
		// 先拒绝空指针和默认隐藏组件，再按两种正式网格类型核对资源引用；返回 true 表示迁移可安全把射线命中交给该模型。
		if (!Component || !Component->GetVisibleFlag()) return false;
		if (const UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component)) return StaticMesh->GetStaticMesh() != nullptr;
		if (const USkeletalMeshComponent* SkeletalMesh = Cast<USkeletalMeshComponent>(Component)) return SkeletalMesh->GetSkeletalMeshAsset() != nullptr;
		return false;
	}

	/** 在蓝图生成类默认对象上找到所有可见模型；该列表同时作为“允许迁移”的前置证据和后续射线响应的唯一写入目标。 */
	void CollectVisibleModelComponents(UBlueprint* Blueprint, TArray<UPrimitiveComponent*>& OutComponents)
	{
		// 先清空调用方提供的缓存，再读取 CDO 已装配的全部组件；只保留拥有模型资源的可见网格，避免把占位组件当成交互面。
		OutComponents.Reset();
		AActor* KioskCDO = Blueprint && Blueprint->GeneratedClass ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!KioskCDO) return;
		TInlineComponentArray<UPrimitiveComponent*> Components(KioskCDO);
		for (UPrimitiveComponent* Component : Components)
		{
			if (IsVisibleModelComponent(Component)) OutComponents.Add(Component);
		}
		// 蓝图模型由 SCS 在实例构造时创建，CDO 的组件数组不包含它们；修改模板才能使保存后的所有摊位使用新碰撞。
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				UPrimitiveComponent* Component = Node ? Cast<UPrimitiveComponent>(Node->ComponentTemplate) : nullptr;
				if (IsVisibleModelComponent(Component)) OutComponents.AddUnique(Component);
			}
		}
	}

	/** 将蓝图组件从已移除的球体父节点迁到原生空间根；只修改序列化的父组件名，保留每个组件已有的相对变换和套接字。 */
	bool ReparentDeprecatedCollisionChildren(UBlueprint* Blueprint)
	{
		// 遍历 SCS 节点，所有仍引用旧球体的子节点先标脏并改父节点名；随后删除旧节点，避免删除过程中连同实际模型一并移除。
		if (!Blueprint || !Blueprint->SimpleConstructionScript) return false;
		bool bChanged = false;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ParentComponentOrVariableName == DeprecatedCollisionName)
			{
				Node->Modify();
				Node->ParentComponentOrVariableName = SceneRootName;
				bChanged = true;
			}
		}
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName() == DeprecatedCollisionName)
			{
				Blueprint->SimpleConstructionScript->RemoveNodeAndPromoteChildren(Node);
				bChanged = true;
				break;
			}
		}
		return bChanged;
	}

	/** 把模型的查询配置补齐为项目交互射线响应；原有物理碰撞保持不变，只在缺少查询时追加查询能力。 */
	void EnableTargetingTraceOnModels(const TArray<UPrimitiveComponent*>& ModelComponents, const ECollisionChannel TargetingChannel)
	{
		// 逐个修改真实模型：NoCollision 升为 QueryOnly，PhysicsOnly 升为 QueryAndPhysics，其余模式原样保留；最后只把项目指定的交互通道设为 Block。
		for (UPrimitiveComponent* Component : ModelComponents)
		{
			if (!Component) continue;
			Component->Modify();
			if (Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			}
			else if (Component->GetCollisionEnabled() == ECollisionEnabled::PhysicsOnly)
			{
				Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			Component->SetCollisionResponseToChannel(TargetingChannel, ECR_Block);
		}
	}

	/** 复核迁移后的正式蓝图合同；要求它仍继承商店 Actor、保留实际模型交互面，且没有同名旧球体残留。 */
	bool ValidateMigratedKiosk(UBlueprint* Blueprint, const ECollisionChannel TargetingChannel)
	{
		// 先检查编译产物与父类，再读取 CDO 的模型组件；每个模型都必须参与查询并阻挡交互通道，同时拒绝仍名为旧代理的组件。
		if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(ACatShopKioskActor::StaticClass())) return false;
		ACatShopKioskActor* KioskCDO = Cast<ACatShopKioskActor>(Blueprint->GeneratedClass->GetDefaultObject());
		TArray<UPrimitiveComponent*> ModelComponents;
		CollectVisibleModelComponents(Blueprint, ModelComponents);
		if (ModelComponents.IsEmpty()) return false;
		TInlineComponentArray<UPrimitiveComponent*> Components(KioskCDO);
		for (const UPrimitiveComponent* Component : Components)
		{
			if (Component && Component->GetFName() == DeprecatedCollisionName) return false;
		}
		for (const UPrimitiveComponent* Component : ModelComponents)
		{
			if (!Component || Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				|| Component->GetCollisionResponseToChannel(TargetingChannel) != ECR_Block) return false;
		}
		return true;
	}
}

// 正式资产迁移流程：
// 1. 读取唯一正式蓝图并验证其生成类仍继承商店摊位；没有该资产或类型变化时立即失败，不创建替代资产。
// 2. 在写入前确认 CDO 已有真实可见模型，避免删掉旧代理后得到无法交互的空摊位。
// 3. 将蓝图里依附旧球体的组件改挂空间根、删除同名 SCS 节点并编译，使旧原生组件覆写从资产中消失。
// 4. 只为实际模型补齐项目交互射线的查询响应，随后复核继承、组件、碰撞与旧引用，并仅在全部成立时保存包。
bool UCatShopKioskAuthoringLibrary::MigrateFormalShopKioskBlueprint()
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, CatShopKioskAuthoring::FormalKioskPath);
	if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(ACatShopKioskActor::StaticClass()))
	{
		UE_LOG(LogCatShopKioskAuthoring, Error, TEXT("Event=shop_kiosk_migration_invalid_blueprint Asset=%s"), CatShopKioskAuthoring::FormalKioskPath);
		return false;
	}

	ACatShopKioskActor* KioskCDO = Cast<ACatShopKioskActor>(Blueprint->GeneratedClass->GetDefaultObject());
	TArray<UPrimitiveComponent*> ModelComponents;
	CatShopKioskAuthoring::CollectVisibleModelComponents(Blueprint, ModelComponents);
	if (ModelComponents.IsEmpty())
	{
		UE_LOG(LogCatShopKioskAuthoring, Error, TEXT("Event=shop_kiosk_migration_missing_visible_model Asset=%s"), *Blueprint->GetPathName());
		return false;
	}

	Blueprint->Modify();
	CatShopKioskAuthoring::ReparentDeprecatedCollisionChildren(Blueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	KioskCDO = Cast<ACatShopKioskActor>(Blueprint->GeneratedClass->GetDefaultObject());
	CatShopKioskAuthoring::CollectVisibleModelComponents(Blueprint, ModelComponents);
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	const ECollisionChannel TargetingChannel = Settings->TargetingTraceChannel.GetValue();
	CatShopKioskAuthoring::EnableTargetingTraceOnModels(ModelComponents, TargetingChannel);

	if (!CatShopKioskAuthoring::ValidateMigratedKiosk(Blueprint, TargetingChannel))
	{
		UE_LOG(LogCatShopKioskAuthoring, Error, TEXT("Event=shop_kiosk_migration_validation_failed Asset=%s"), *Blueprint->GetPathName());
		return false;
	}

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *PackageFilename, SaveArgs))
	{
		UE_LOG(LogCatShopKioskAuthoring, Error, TEXT("Event=shop_kiosk_migration_save_failed Asset=%s File=%s"), *Blueprint->GetPathName(), *PackageFilename);
		return false;
	}

	UE_LOG(LogCatShopKioskAuthoring, Display, TEXT("Event=shop_kiosk_migration_completed Asset=%s Models=%d Channel=%d"),
		*Blueprint->GetPathName(), ModelComponents.Num(), static_cast<int32>(TargetingChannel));
	return true;
}
