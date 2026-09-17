#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Condition/CatConditionTypes.h"
#include "Environment/CatWaterTypes.h"
#include "Components/ActorComponent.h"
#include "CatConditionComponent.generated.h"



/** Character 局内离散身体状态组件；采样水域并将状态交给 ASC；快照仅为钓鱼和表现消费者的只读投影。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatConditionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 关闭组件状态复制和 Tick；状态由 ASC 效果复制。 */
	UCatConditionComponent();

	/** 开始观察 ASC Tag；首次绑定立即生成本机读模型。 */
	virtual void BeginPlay() override;
	/** 解绑 ASC 状态监听，避免跨身体回调。 */
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** 按本机 ASC 当前状态生成旧接口读模型；服务器与客户端各自读取，快照本身不复制，不能借返回值改写 Wet/Downed。 */
	const FCatConditionSnapshot& GetSnapshot() const;
	/** 从 ASC 读取状态真相；非能力消费者使用同一标签语义。 */
	bool HasState(FGameplayTag Tag) const;

	/** 落水、天气等非技能反馈在 authority 设置纯表现 Wet；重复相同值不增加 Revision，也不修改任何 Attribute。 */
	void SetWetFromAuthority(bool bNewWet);

	/** 由钓鱼固定步提交采样时长；组件自行查询脚点水深并维护滞回/确认。 */
	ECatWaterExposureUpdate UpdateWaterExposureFromAuthority(const FCatWaterRegionHandle& WaterRegion,
		double DeltaSeconds, double& OutImmersionDepthCentimeters);


	/** 服务器开发验证入口更新本来源的倒地 GE；聚合状态变化由 ASC 通知身体消费者，钓鱼退出归 Character 的标签监听。 */
	bool SetDownedFromAuthority(bool bNewDowned);


private:
	/** ASC 的状态计数改变后重新投影；不将快照写回 GE，也不生成玩法命令。 */
	void HandleStateTagChanged(FGameplayTag Tag, int32 Count);
	/** 从同一 ASC 的状态重新生成兼容视图；没有独立复制或写入入口。 */
	void RefreshSnapshot() const;
	/** 由 GAS 派生的本机读模型；只服务尚未迁移的钓鱼接口与 UI，不保存第二份权威状态。 */
	mutable FCatConditionSnapshot Snapshot;
	/** 订阅所在 ASC；结束时从同一对象解绑，不通过新 Pawn 寻找。 */
	TWeakObjectPtr<class UCatAbilitySystemComponent> ObservedASC;
	/** 状态订阅句柄；BeginPlay 创建，EndPlay 移除。 */
	FDelegateHandle StateTagHandle;

	/** 当前脚点持续处在危险水深中的确认时长，单位为 World 秒；水域暴露更新写入它，用来给危险水域进入判定提供滞回前的累计证据。 */
	double DangerousWaterBuildUpSeconds = 0.0;
};
