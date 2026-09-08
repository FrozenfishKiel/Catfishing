#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "CatBodyActionPresentationSettings.generated.h"

class UAnimMontage;

/** 单个保留 BodyAction 长动作的表现配置；事件标签是唯一查表键，配置不承载领域权限、距离、Revision 或结果。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatBodyActionPresentationConfig
{
	GENERATED_BODY()
	/** 这条配置代表某个保留 BodyAction 事件的身份；配置资产写入，专用 Ability 与 Character 表现播放只读取同一条记录。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction") FGameplayTag BodyActionEventTag;
	/** 领域提交前可被 Fishing Cancel 中断的前摇秒数；专用 Ability 读取它创建 WaitDelay，不适用于 Wet 状态反馈。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction", meta=(ClampMin="0.0", UIMin="0.0", Units="s")) float LeadInSeconds = 0.15f;
	/** 蓝图表现分派键；配置资产可覆盖，Character 播放/停止表现时读取，留空时回退到 BodyActionEventTag。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction") FGameplayTag PresentationEventTag;
	/** 可选正式 Montage；配置资产写入、Character 表现播放读取，空值时只调用蓝图事件且不阻断领域提交。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction") TSoftObjectPtr<UAnimMontage> Montage;
};

/** BodyAction 长动作表现设置；只服务仍保留的 Camp/Social 事件 Ability，Wet 复制事实不从这里触发或清除。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Body Action Presentation"))
class CATFISHING_API UCatBodyActionPresentationSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	/** 建立六个正式 BodyAction 的默认表现记录；库存、献祭、偷鱼和 Wet 反馈不进入这张表现表。 */
	UCatBodyActionPresentationSettings();
	/** 查找事件标签对应的显式表现配置；重复配置时以最后一条为准，空标签直接拒绝。 */
	const FCatBodyActionPresentationConfig* FindPresentationConfig(FGameplayTag BodyActionEventTag) const;
	/** 解析动作的可取消前摇；配置缺失只回退默认秒数，不让领域服务为表现时长提供第二套规则。 */
	float GetLeadInSeconds(FGameplayTag BodyActionEventTag) const;
	/** 解析角色表现分派键；表现标签未配置时使用动作标签本身，保证开始和停止能用同一身份收尾。 */
	FGameplayTag GetPresentationEventTag(FGameplayTag BodyActionEventTag) const;
	/** 同步加载事件标签对应的可选 Montage；无配置或空软引用时返回空指针。 */
	UAnimMontage* LoadMontage(FGameplayTag BodyActionEventTag) const;
	/** 没有动作级配置时使用的共同前摇秒数；配置或默认对象写入，各专用 Ability 读取它保证仍可取消。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction", meta=(ClampMin="0.0", UIMin="0.0", Units="s")) float DefaultLeadInSeconds = 0.15f;
	/** 按 BodyActionEventTag 索引的表现配置；只描述保留 BodyAction 的前摇、蓝图键和 Montage，不承载物品事务或 Wet 状态。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction") TArray<FCatBodyActionPresentationConfig> ActionPresentationConfigs;
};
