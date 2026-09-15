#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatFishPresentationDefinition.generated.h"

class UAnimSequenceBase;
class UCatFishAnimInstance;
class UFXSystemAsset;
class USkeletalMesh;

/**
 * 逐鱼的一条「漂讯／水面」表现槽位。
 *
 * 鱼表格有三列要落在这里：「鱼刚咬饵时鱼漂的变化」「鱼刚咬饵时水面的变化」「与猫搏斗时水面的变化」。
 * 此前逐鱼表现的唯一入口只有网格／AnimBP／四段动画／缩放／Transform，这三列无处安放。
 *
 * 槽位只描述「放哪个特效、挂多大、持续多久」，不描述什么时候播——触发时机由既有的
 * ECatFishingBobberPresentationMode 与搏斗表现步决定，本结构不引入第二套时序。
 * Effect 为空＝这条鱼在该时刻不叠加专属特效，退回全局漂讯／水面表现，不是「未裁」。
 */
USTRUCT(BlueprintType)
struct FCatFishSurfaceCue
{
	GENERATED_BODY()

	/**
	 * 要播放的特效资产（Niagara 或 Cascade，两者都派生自 UFXSystemAsset）。
	 * 本轮只开槽位，正式 VFX 资产不在范围内，全部留空。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue")
	TSoftObjectPtr<UFXSystemAsset> Effect;

	/** 特效整体缩放；<= 0 按 1 处理，避免留空的槽位把特效缩成看不见。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue", meta = (ClampMin = "0.0"))
	double EffectScale = 1.0;

	/** 一次性特效的持续秒数；0 表示跟随所在表现状态、由表现层自己收尾。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue", meta = (ClampMin = "0.0", Units = "s"))
	double DurationSeconds = 0.0;

	/** 槽位是否真的配了特效；表现层用它决定叠加逐鱼特效还是只走全局表现。 */
	bool HasEffect() const
	{
		return !Effect.IsNull();
	}
};

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

	/** 世界鱼侧躺角，单位度；保留现有资产序列化字段名，鱼拾取Actor按该角旋转网格，物理根不再承担侧躺表现。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Transform",
		meta=(ClampMin="-180.0", ClampMax="180.0", Units="deg"))
	double LandedActorRollDegrees = 90.0;

	/**
	 * 鱼表格「鱼刚咬饵时，鱼漂的变化」列的落点：真咬瞬间叠加在浮漂上的逐鱼特效。
	 * 漂讯的三档时序（Calm／BiteWarning／Sunk）仍归 UCatFishingPresentationSettings，这里只加逐鱼那一层。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Cue")
	FCatFishSurfaceCue BiteBobberCue;

	/** 鱼表格「鱼刚咬饵时，水面的变化」列的落点：真咬瞬间在浮漂周围水面播放的逐鱼特效。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Cue")
	FCatFishSurfaceCue BiteWaterSurfaceCue;

	/** 鱼表格「与猫搏斗时，水面的变化」列的落点：搏斗期间跟随鱼位置的持续水面特效。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fish|Cue")
	FCatFishSurfaceCue FightWaterSurfaceCue;
};
