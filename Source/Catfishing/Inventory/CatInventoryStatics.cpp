#include "Inventory/CatInventoryStatics.h"

#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryInterface.h"

// 候选库存收集流程：只读取实现了库存接口的组件，再按统一收货优先级稳定排序。
void UCatInventoryStatics::CollectInventoryComponentsFromActor(const AActor* TargetActor,
	TArray<UCatInventoryComponent*>& OutInventoryComponents)
{
	OutInventoryComponents.Reset();

	if (TargetActor == nullptr)
	{
		return;
	}

	const TArray<UActorComponent*> InventoryComponents =
		TargetActor->GetComponentsByInterface(UCatInventoryInterface::StaticClass());
	for (UActorComponent* ActorComponent : InventoryComponents)
	{
		ICatInventoryInterface* InventoryInterface = Cast<ICatInventoryInterface>(ActorComponent);
		if (InventoryInterface == nullptr)
		{
			continue;
		}

		if (UCatInventoryComponent* InventoryComponent = InventoryInterface->GetInventoryComponent())
		{
			OutInventoryComponents.AddUnique(InventoryComponent);
		}
	}

	OutInventoryComponents.StableSort([](const UCatInventoryComponent& Left,
		const UCatInventoryComponent& Right)
	{
		return Left.GetUnifiedInventoryIntakePriority() > Right.GetUnifiedInventoryIntakePriority();
	});
}

// 外部收集流程：向调用方追加当前 Actor 暴露的库存组件，不清空调用方已有列表。
void UCatInventoryStatics::AppendInventoryComponentsFromActor(const AActor* TargetActor,
	TArray<UCatInventoryComponent*>& OutInventoryComponents)
{
	TArray<UCatInventoryComponent*> CollectedInventoryComponents;
	CollectInventoryComponentsFromActor(TargetActor, CollectedInventoryComponents);

	for (UCatInventoryComponent* InventoryComponent : CollectedInventoryComponents)
	{
		if (InventoryComponent != nullptr)
		{
			OutInventoryComponents.Add(InventoryComponent);
		}
	}
}

// Actor 预检流程：空批次直接成功；非空批次交给第一个允许统一收货且能完整接收的库存组件。
bool UCatInventoryStatics::CanActorFullyAcceptInventoryBatch(const AActor* TargetActor,
	const FCatInventoryReceiveBatch& ReceiveBatch)
{
	if (ReceiveBatch.IsEmpty())
	{
		return true;
	}

	TArray<UCatInventoryComponent*> InventoryComponents;
	CollectInventoryComponentsFromActor(TargetActor, InventoryComponents);

	for (const UCatInventoryComponent* InventoryComponent : InventoryComponents)
	{
		if (InventoryComponent != nullptr
			&& InventoryComponent->CanReceiveUnifiedInventoryIntake()
			&& InventoryComponent->CanFullyAcceptInventoryBatch(ReceiveBatch))
		{
			return true;
		}
	}

	return false;
}

// Actor 收货流程：先按同一规则找到完整可接收者，再只让这个组件执行正式写入，避免多组件分摊一批货。
bool UCatInventoryStatics::TryAddInventoryBatchToActor(AActor* TargetActor,
	const FCatInventoryReceiveBatch& ReceiveBatch)
{
	if (ReceiveBatch.IsEmpty())
	{
		return true;
	}

	TArray<UCatInventoryComponent*> InventoryComponents;
	CollectInventoryComponentsFromActor(TargetActor, InventoryComponents);

	for (UCatInventoryComponent* InventoryComponent : InventoryComponents)
	{
		if (InventoryComponent == nullptr
			|| !InventoryComponent->CanReceiveUnifiedInventoryIntake()
			|| !InventoryComponent->CanFullyAcceptInventoryBatch(ReceiveBatch))
		{
			continue;
		}

		return InventoryComponent->TryAddInventoryBatch(ReceiveBatch);
	}

	return false;
}
