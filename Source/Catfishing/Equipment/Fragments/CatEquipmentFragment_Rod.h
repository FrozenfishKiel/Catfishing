#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatEquipmentFragment_Rod.generated.h"

/** 鱼竿的长度、磨损和部署锚点配置，供钓鱼与装备实例读取；资产持有静态值，不复制运行状态。 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatEquipmentFragment_Rod : public UCatInventoryItemFragment
{
	GENERATED_BODY()

public:
	/** 校验本片段数值与空间约束；装备目录和具体玩法仅接收完整配置，不在这里补写缺省值。 */
	virtual bool IsRuntimeReady() const override;

	/** 鱼竿实例耐久上限；新物品按它初始化，钓鱼磨损跨会话保留。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0", DisplayName = "鱼竿耐久上限"))
	double MaximumRodDurability = 0.0;

	/** 鱼线可支持的最大运行长度，单位厘米；抛竿距离、鱼线约束和搏斗 Runner 读取它限制本实例能拉到多远。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0"))
	double MaximumLineLengthCentimeters = 0.0;

	/** 转矩公式使用的玩法杆长；不读取 Mesh Bounds，换皮和视觉缩放不会改变遛鱼手感。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "1.0", Units = "cm"))
	double RodPhysicsLengthCentimeters = 200.0;

	/** 鱼竿在搏斗固定步里的基础耐久磨损速率；Runner 按秒读取它写回库存实例，不改变定义资产本身。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0", DisplayName = "鱼竿基础磨损每秒"))
	double BaseDurabilityWearPerSecond = 0.0;

	/** 高张力状态下的鱼竿磨损倍率；搏斗 Runner 在绷线时读取它放大基础磨损，必须至少保持 1。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod", meta = (ClampMin = "0.0", DisplayName = "绷线磨损倍率"))
	double HighTensionWearMultiplier = 0.0;

	/** 竿尖在鱼竿部署 Actor 本地空间中的锚点；鱼线、钩子和力竭收近目标读取它决定线的起点。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod")
	FTransform RodTipLocalTransform = FTransform::Identity;

	/** 架杆姿态在鱼竿部署 Actor 本地空间中的锚点；部署表现和收杆流程读取它对齐世界鱼竿。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod")
	FTransform StandLocalTransform = FTransform::Identity;

	/** 持竿握把在鱼竿部署 Actor 本地空间中的锚点；角色手部表现读取它，不参与库存身份判断。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rod")
	FTransform GripLocalTransform = FTransform::Identity;
};
