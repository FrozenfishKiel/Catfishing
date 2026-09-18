#pragma once
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "Environment/CatWaterTypes.h"
#include "CatGA_CastNet.generated.h"

/** 一次撒网产生一批真实鱼；复用鱼目录与捕获额外奖励，随后限制该区域的新鱼生成。 */
UCLASS()
class CATFISHING_API UCatGA_CastNet : public UCatItemGameplayAbility
{
	GENERATED_BODY()
public:
	/** 本次捕获逐条喷出的间隔，单位秒；交给独立出鱼对象执行，不延长使用能力。 */
	UPROPERTY(EditDefaultsOnly, Category="渔网", meta=(ClampMin="0.05", Units="s")) float FishEmissionInterval = 0.2f;
	/** 每次撒网的最低鱼数；策划在能力资产设置，含端点随机。 */
	UPROPERTY(EditDefaultsOnly, Category="渔网", meta=(ClampMin="1")) int32 MinimumFish = 5;
	/** 每次撒网的最高鱼数；不得低于最低数量。 */
	UPROPERTY(EditDefaultsOnly, Category="渔网", meta=(ClampMin="1")) int32 MaximumFish = 8;
	/** 可撒网水面距使用者的最大距离，单位厘米。 */
	UPROPERTY(EditDefaultsOnly, Category="渔网", meta=(ClampMin="1", Units="cm")) double CastRange = 500.0;
	/** 撒网后无新鱼的水平半径，单位厘米；已有咬钩和搏斗不受影响。 */
	UPROPERTY(EditDefaultsOnly, Category="渔网", meta=(ClampMin="1", Units="cm")) double SuppressionRadius = 300.0;
	/** 撒网后无新鱼的时长，单位秒；策划初值为三十秒。 */
	UPROPERTY(EditDefaultsOnly, Category="渔网", meta=(ClampMin="1", Units="s")) double SuppressionSeconds = 30.0;
	/** 编辑器校验撒网的数量、射程和成本契约；奖励归每条鱼结算，因此不能同时配置自用效果。 */
	virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
	/** 复用来源门并确认目标是可撒网的新鱼水域。 */
	virtual bool ValidateUse() const override;
	/** 物品与整批鱼准备完成后才公开库存变化。 */
	virtual bool DefersInventoryCostNotification() const override { return true; }
protected:
	/** 准备整批鱼和出鱼宿主成功后消费网、结算奖励并发布禁生区域，再把隐藏鱼交给独立队列；准备失败清理本次候选且不扣网。 */
	virtual void CommitUse() override;
private:
	/** 能力预检和提交共用的水面资格：服务器复核身体射程及遮挡，已有禁生区拒绝新网以免重复出鱼。 */
	FCatWaterSpatialResult ResolveWater() const;
};
