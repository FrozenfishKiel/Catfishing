#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatBackPackComponent.generated.h"

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

private:
	/** 读取项目正式个人背包容量；配置缺失时返回零，调用方据此避免沿用蓝图遗留的 24 格默认值。 */
	int32 GetConfiguredPlayerSlotCapacity() const;
};
