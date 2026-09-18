#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatItemUseFragment.generated.h"

class UCatItemGameplayAbility;
class UGameplayEffect;
class UAnimMontage;

/** 一类物品的使用配置；策划只填写数据，行为、成本提交和联网流程由能力类实现。 */
UCLASS(EditInlineNew, DefaultToInstanced, BlueprintType)
class CATFISHING_API UCatItemUseFragment : public UCatInventoryItemFragment
{
	GENERATED_BODY()
public:
	/** 单件物品可容纳的使用资源；零表示无独立资源，湿毛器水量和响响筒次数由实例保存，不能堆叠。 */
	UPROPERTY(EditDefaultsOnly, Category="使用资源", meta=(ClampMin="0"))
	int32 ResourceCapacity = 0;
	/** 每次主操作支付的资源单位；使用成本读取，补水操作不扣此值。 */
	UPROPERTY(EditDefaultsOnly, Category="使用资源", meta=(ClampMin="1"))
	int32 ResourceCost = 1;
	/** 资源耗尽是否消费物品本体；响响筒启用，能补水的湿毛器关闭。 */
	UPROPERTY(EditDefaultsOnly, Category="使用资源")
	bool bConsumeWhenEmpty = false;
	/** 阻止本次使用的角色状态；用于同类 Buff 不重复消费，GA 提交前再次核对。 */
	UPROPERTY(EditDefaultsOnly, Category="使用")
	FGameplayTagContainer BlockedStateTags;
	/** 使用动作的程序实现；拥有物品时授予对应能力，共享容器由同类交互能力读取目标配置。 */
	UPROPERTY(EditDefaultsOnly, Category="使用", meta=(DisplayName="使用能力"))
	TSubclassOf<UCatItemGameplayAbility> AbilityClass;
	/** 每次成功使用消耗的件数；零表示不消耗，能力成本在服务器提交时读取。 */
	UPROPERTY(EditDefaultsOnly, Category="使用", meta=(ClampMin="0", DisplayName="消耗数量"))
	int32 ConsumeCount = 1;
	/** 从动作开始到生效的秒数；到点前取消不消费，客户端和服务器各自维护能力任务。 */
	UPROPERTY(EditDefaultsOnly, Category="使用", meta=(ClampMin="0", Units="s", DisplayName="生效前摇"))
	float CommitDelay = 0.5f;
	/** 使用时播放的动作；GAS 管理拥有者预测、远端复制和取消，允许尚未配置美术资源。 */
	UPROPERTY(EditDefaultsOnly, Category="表现", meta=(DisplayName="使用蒙太奇"))
	TObjectPtr<UAnimMontage> Montage;
	/** 成功提交后施加给使用者的效果；普通 GE 参数在各效果资产配置，不限制为某个特定属性。 */
	UPROPERTY(EditDefaultsOnly, Category="效果", meta=(DisplayName="使用效果"))
	TArray<TSubclassOf<UGameplayEffect>> Effects;
	/** 效果所需的命名数值；能力追加实例相关参数，策划不能借此修改物品身份或实际重量。 */
	UPROPERTY(EditDefaultsOnly, Category="效果", meta=(DisplayName="效果参数"))
	TMap<FGameplayTag, float> Magnitudes;
	/** 只检查配置是否完整及数值是否合法；不在配置对象中执行 GE 或访问运行角色。 */
	virtual bool IsRuntimeReady() const override;
#if WITH_EDITOR
	/** 编辑器保存和验证时返回可定位的配置错误，避免无效道具进入正式资产。 */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
