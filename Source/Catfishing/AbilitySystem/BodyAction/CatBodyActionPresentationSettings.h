#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/BodyAction/CatBodyActionAbility.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "CatBodyActionPresentationSettings.generated.h"

class UAnimMontage;

/**
 * 单个 BodyAction 的表现前摇配置。
 * 它只决定 Ability 提交前要等多久、给蓝图哪个表现标签、可选播放哪段 Montage；不拥有权限、距离、Revision 或领域结果。
 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatBodyActionPresentationConfig
{
	GENERATED_BODY()

	/** 这条表现配置对应的身体动作；Unknown 只作为未配置哨兵，不参与运行查找。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction")
	ECatBodyActionAbilityCommand Command = ECatBodyActionAbilityCommand::Unknown;

	/** 动作提交前的可取消前摇秒数；BodyAction Ability 读取它来创建 WaitDelay，Cancel 可在这段时间停止表现并放弃领域提交。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float LeadInSeconds = 0.15f;

	/** 蓝图表现事件标签；留空时使用该动作的 AbilityEvent 标签，保证没有正式标签时仍有稳定分派键。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction")
	FGameplayTag PresentationEventTag;

	/** 可选正式 Montage；留空时只触发蓝图表现事件，让 WBP、音效或后续美术资源自己接管。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction")
	TSoftObjectPtr<UAnimMontage> Montage;
};

/** BodyAction 长动作表现设置；Ability 和 Character 共用这一份查表，避免各领域服务各自硬编码动作时长和 Montage。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Body Action Presentation"))
class CATFISHING_API UCatBodyActionPresentationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 构造默认前摇表；项目配置可以覆盖，但不能让领域服务绕过这份统一表现接口。 */
	UCatBodyActionPresentationSettings();

	/** 查找某个身体动作的显式表现配置；没有配置时返回 nullptr，调用方应使用默认前摇和 AbilityEvent 标签。 */
	const FCatBodyActionPresentationConfig* FindPresentationConfig(ECatBodyActionAbilityCommand Command) const;

	/** 返回某动作的可取消前摇秒数；显式配置优先，否则使用默认前摇并保持非负。 */
	float GetLeadInSecondsForCommand(ECatBodyActionAbilityCommand Command) const;

	/** 返回某动作的蓝图表现分派标签；显式标签优先，否则回退到同一动作的 AbilityEvent 标签。 */
	FGameplayTag GetPresentationEventTagForCommand(ECatBodyActionAbilityCommand Command) const;

	/** 读取某动作的可选 Montage；没有正式 Montage 时返回 nullptr，由蓝图事件或后续资产接管表现。 */
	UAnimMontage* LoadMontageForCommand(ECatBodyActionAbilityCommand Command) const;

	/** 默认前摇秒数；没有动作级配置时由 BodyAction Ability 读取，保证所有身体动作至少有同一条可取消窗口。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction", meta=(ClampMin="0.0", UIMin="0.0", Units="s"))
	float DefaultLeadInSeconds = 0.15f;

	/** 动作级表现配置表；只覆盖前摇、标签和 Montage，不得承载领域权限或结果。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Catfishing|BodyAction")
	TArray<FCatBodyActionPresentationConfig> ActionPresentationConfigs;
};
