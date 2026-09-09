#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatFishPresentationDefinition.generated.h"

class UAnimSequenceBase;
class UCatFishAnimInstance;
class USkeletalMesh;

/**
 * 单一鱼种的完整表现定义。
 *
 * 该资产不能被全局目录独立枚举，只能由 UCatFishDefinition::PresentationDefinition 直接引用。
 * 水中 Encounter、力竭落地和嘴叼世界鱼都消费同一份定义，避免按 FishDefinitionId 维护第二张表现表。
 */
UCLASS(BlueprintType)
class CATFISHING_API UCatFishPresentationDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 只检查资产引用和数值合同，不同步加载 Mesh/动画，Dedicated Server 可安全调用。 */
	bool IsRuntimeDefinitionReady() const;

	/** 按“重量约等于体积”计算统一线性缩放；非法输入安全回退为 1。 */
	double ComputeUniformVisualScale(double WeightKilograms) const;

	/** 水中、落地和嘴叼共用的骨骼网格；Skeleton 由 Mesh 自身持有，不另设可能冲突的字段。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Mesh")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** 绑定本鱼 Skeleton 的薄 AnimBP；必须继承 UCatFishAnimInstance。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Animation")
	TSoftClassPtr<UCatFishAnimInstance> AnimInstanceClass;

	/** Base AnimBP 的 Calm / Struggle / AutoHauling 三个资产覆盖；生成器与验证器以这里为唯一输入。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Animation")
	TSoftObjectPtr<UAnimSequenceBase> CalmAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Animation")
	TSoftObjectPtr<UAnimSequenceBase> StruggleAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Animation")
	TSoftObjectPtr<UAnimSequenceBase> ExhaustedAnimation;

	/** 岸上和嘴叼状态冻结使用的姿态动画；通常复用力竭/死亡动画的最后一帧。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Animation")
	TSoftObjectPtr<UAnimSequenceBase> LandedAnimation;

	/** 该 Mesh 在 UniformScale=1 时代表的鱼重。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Scale", meta=(ClampMin="0.001", Units="kg"))
	double MeshReferenceWeightKilograms = 1.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Scale", meta=(ClampMin="0.01"))
	double MinimumUniformScale = 0.8;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Scale", meta=(ClampMin="0.01"))
	double MaximumUniformScale = 1.25;

	/** 每套美术资源在水中 VisualRoot 下的局部轴向、位置和基础比例修正。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Transform")
	FTransform EncounterMeshRelativeTransform = FTransform::Identity;

	/** 岸上 Mesh 的美术对齐；贴地高度由缩放后鱼体边界与坡面自动求解，不在这里写固定抬升。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Transform")
	FTransform LandedMeshRelativeTransform = FTransform::Identity;

	/** Actor 根附着到猫嘴 Socket 后，鱼 Mesh 自己的局部对齐修正。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Transform")
	FTransform CarriedMeshRelativeTransform = FTransform::Identity;

	/** 鱼在水中力竭后的纯表现侧翻角；不改变 Actor 权威 Transform。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Transform",
		meta=(ClampMin="-180.0", ClampMax="180.0", Units="deg"))
	double ExhaustedVisualRollDegrees = 90.0;

	/** 岸上鱼 Actor 的权威侧翻角；随 ReplicatedMovement 同步。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Transform",
		meta=(ClampMin="-180.0", ClampMax="180.0", Units="deg"))
	double LandedActorRollDegrees = 90.0;
};
