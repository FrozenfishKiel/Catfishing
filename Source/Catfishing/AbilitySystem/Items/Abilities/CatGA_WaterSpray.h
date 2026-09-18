#pragma once
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "CatGA_WaterSpray.generated.h"

/** 有限水量喷水工具；同一精确来源能力处理喷水与近水补充，目标只由服务器射线确定。 */
UCLASS()
class CATFISHING_API UCatGA_WaterSpray : public UCatItemGameplayAbility
{
 GENERATED_BODY()
public:
 /** 补水和喷水都在资源最终落定后统一通知，避免费用提交时发布中间状态。 */
 virtual bool DefersInventoryCostNotification() const override { return true; }
 /** 副操作表示对准水面补水，输入层仅对支持它的物品改道。 */
 virtual bool SupportsSecondaryUse() const override { return true; }
 /** 使用前和提交前核对水量，补水还须命中近处水域且没有遮挡。 */
 virtual bool ValidateUse() const override;
 /** 喷水需要零件数成本、独立水量以及至少一个目标表现 GE。 */
 virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
 /** 水柱可达距离（厘米）；能力资产配置，服务器用于命中查询。 */
 UPROPERTY(EditDefaultsOnly, Category="喷水", meta=(ClampMin="1", Units="cm")) float SprayRange = 800.f;
 /** 可补水距离（厘米）；能力资产配置，河水射线及遮挡验证都遵守此上限。 */
 UPROPERTY(EditDefaultsOnly, Category="喷水", meta=(ClampMin="1", Units="cm")) float RefillRange = 250.f;
protected:
 /** 权威提交水量成本后喷向目标 ASC，或在补水成功后恢复原实例资源；喷空仍耗水。 */
 virtual void CommitUse() override;
};
