#pragma once

#include "CoreMinimal.h"
#include "Condition/CatConditionTypes.h"
#include "Environment/CatWaterTypes.h"
#include "Components/ActorComponent.h"
#include "CatConditionComponent.generated.h"


/** Condition 完整快照发生提交或复制变化的本机通知；订阅者必须重新读取 GetSnapshot，不使用增量载荷拼状态。 */
DECLARE_MULTICAST_DELEGATE(FCatConditionSnapshotChanged);

/** Character 局内离散身体状态组件；维护 Wet、危险水域和倒地事实，向交互与表现消费者发布快照。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatConditionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启组件复制并关闭 Tick；所有状态只由显式 authority 命令推进。 */
	UCatConditionComponent();

	/** 注册唯一 Snapshot 复制；终态缓存和 Controller/身份不会进入网络。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供身体条件的服务器最终值或客户端复制值；外部只据此判断交互资格，不能借返回值改写 Wet/Downed。 */
	const FCatConditionSnapshot& GetSnapshot() const;

	/** 落水、天气等非技能反馈在 authority 设置纯表现 Wet；重复相同值不增加 Revision，也不修改任何 Attribute。 */
	void SetWetFromAuthority(bool bNewWet);

	/** 由钓鱼固定步提交采样时长；组件自行查询脚点水深并维护滞回/确认。 */
	ECatWaterExposureUpdate UpdateWaterExposureFromAuthority(const FCatWaterRegionHandle& WaterRegion,
		double DeltaSeconds, double& OutImmersionDepthCentimeters);

	/** 服务器更新纯表现疲惫档；相同值不发布，不影响体力或物品效果。 */
	void SetFatigueTierFromAuthority(ECatFatigueTier NewTier);

	/** 服务器开发验证入口设置离散倒地状态；首次倒地会收口进行中的钓鱼，重复同值不会重复发布。 */
	bool SetDownedFromAuthority(bool bNewDowned);

	/** 本机完整快照变化通知；LocalPlayer UI 成对订阅，领域写入者不依赖该通知推进。 */
	FCatConditionSnapshotChanged OnSnapshotChanged;

private:
	/** 客户端收到完整 Snapshot 后只供表现读取；不会在 RepNotify 改 Attribute 或发送恢复命令。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；集中保证 UI 不漏掉任何完整快照变化。 */
	void PublishSnapshot();

	/** Wet/Downed/水域暴露 的唯一复制事实；Condition 写入、UI 和表现层读取，Wet 本身不由任何 Ability 清除或触发。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatConditionSnapshot Snapshot;

	/** 当前脚点持续处在危险水深中的确认时长，单位为 World 秒；水域暴露更新写入它，用来给危险水域进入判定提供滞回前的累计证据。 */
	double DangerousWaterBuildUpSeconds = 0.0;
};
