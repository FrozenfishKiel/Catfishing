#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatBackPackComponent.generated.h"

/** 手持物品的格位保留；实例仍只有一份，由库存活动区保管。 */
USTRUCT()
struct FCatQuickbarHeldSlot
{
	GENERATED_BODY()
	UPROPERTY() int32 SlotIndex = INDEX_NONE;
	UPROPERTY() FGuid ItemInstanceId;
	UPROPERTY() FName DefinitionId;
	UPROPERTY() bool bInUse = false;
};

/** 角色随身背包的正式库存宿主；它只拥有玩家容量初始化，物品收货、移动和使用继续由通用库存实现。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatBackPackComponent : public UCatInventoryComponent
{
	GENERATED_BODY()

public:
	/** 角色随身物品的默认接收者；构造时设置收货优先级，确保拾取和奖励优先进入背包而非角色上的附属容器。 */
	UCatBackPackComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	/** 组件初始化前收束蓝图遗留的正数槽位默认值；只在尚未生成任何格子时改写，存档和运行物品永不截断。 */
	virtual void InitializeComponent() override;
	/** authority 在角色被占有后调用，按 InventorySettings 写入玩家背包容量并建立空槽位。 */
	void InitializePlayerInventorySlotCapacityFromAuthority();

	/**
	 * 背包是唯一受「随身携带总量」约束的库存（道具册：普通饵 8 份、窝料 5 份）。
	 * 营地公库、鱼护、鱼缸和商店货架都不受它管——那几条限制各自另有容量口径。
	 */
	virtual bool EnforcesCarryLimits() const override { return true; }

	bool ReserveQuickbarHeldSlotFromAuthority(int32 SlotIndex, FGuid ItemId);
	/** 地上接回的原实例已在活动区；为它保留空格，不执行第二次 Use。 */
	bool ReserveExistingHeldQuickbarSlotFromAuthority(int32 SlotIndex, FGuid ItemId);
	void ClearQuickbarHeldSlotFromAuthority();
	void SetQuickbarHeldSlotInUseFromAuthority(bool bInUse);
	const FCatQuickbarHeldSlot& GetQuickbarHeldSlot() const { return QuickbarHeldSlot; }
	virtual bool CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& Entry, int32 TargetSlotIndex) const override;
	virtual bool CanAcceptInventoryDefinitionAtSlot(const UCatInventoryItemDefinition& Definition, int32 TargetSlotIndex) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UPROPERTY(ReplicatedUsing=OnRep_QuickbarHeldSlot) FCatQuickbarHeldSlot QuickbarHeldSlot;
	UFUNCTION() void OnRep_QuickbarHeldSlot();
	/** 读取项目基础格数与角色成长容量之和；缺失项按零处理，初始化和成长扩容据此替代蓝图遗留容量。 */
	int32 GetConfiguredPlayerSlotCapacity() const;
};
