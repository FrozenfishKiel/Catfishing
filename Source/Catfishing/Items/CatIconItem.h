#pragma once
#include "Items/CatItem.h"
#include "CatIconItem.generated.h"
class UBillboardComponent;

/** 未配置独立模型的道具世界载体；直接显示物品图标，拾取与次数沿通用实物流程。 */
UCLASS()
class CATFISHING_API ACatIconItem : public ACatItem
{
	GENERATED_BODY()
public:
	/** 创建可见图标与紧凑物理盒，避免无模型道具掉落后不可见。 */
	ACatIconItem();
	/** 世界实例复制定义用于图标，库存载荷及剩余资源仍由父类保管。 */
	virtual bool InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, int32 Quantity) override;
	/** 声明显示定义复制，客户端不依赖服务器的非复制载荷。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	/** 本世界物的静态定义；权威初始化写入，客户端读取图标，不作为可领取数量的依据。 */
	UPROPERTY(ReplicatedUsing=RefreshIcon) TObjectPtr<UCatInventoryItemDefinition> DisplayDefinition;
	/** 面向摄像机的图标板；不参与物理或交互裁决，未来可用模型载体替换。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBillboardComponent> Icon;
	/** 图标显示以复制定义为准；两端初始化时按固定世界尺寸缩放，避免源图片分辨率改变拾取物大小。 */
	UFUNCTION() void RefreshIcon();
};
