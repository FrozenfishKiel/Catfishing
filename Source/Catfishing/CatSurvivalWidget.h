#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Character/CatConditionTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "Social/CatSocialTypes.h"
#include "CatSurvivalWidget.generated.h"

class UTextBlock;

/** LocalPlayer MVC 投影出的只读 Lake 状态；只承载可渲染 DTO，不暴露 ASC、Pawn、服务或身份对象。 */
USTRUCT(BlueprintType)
struct FCatSurvivalViewState
{
	GENERATED_BODY()

	/** 当前猫身体的 Hunger 只读投影；范围与单位尚未裁决，View 不做夹取或解释。 */
	UPROPERTY(BlueprintReadOnly)
	float Hunger = 0.0f;

	/** 当前猫身体的 Fatigue 只读投影；范围与单位尚未裁决，View 不反向写入身体状态。 */
	UPROPERTY(BlueprintReadOnly)
	float Fatigue = 0.0f;

	/** 当前 Poison 只读投影；阈值和倒地裁决仍由 Character Condition 拥有。 */
	UPROPERTY(BlueprintReadOnly)
	float Poison = 0.0f;

	/** 当前 FishingStrength 只读投影；HUD 不把它换算成等级或战力。 */
	UPROPERTY(BlueprintReadOnly)
	float FishingStrength = 0.0f;

	/** 当前短周期 FightStamina 只读投影；只在钓鱼搏斗事务中被 authority 消耗。 */
	UPROPERTY(BlueprintReadOnly)
	float FightStamina = 0.0f;

	/** Character 离散身体完整快照；Wet/Downed/Recovery 不从数值反推。 */
	UPROPERTY(BlueprintReadOnly)
	FCatConditionSnapshot Condition;

	/** Character 一局功能装配完整快照；不含 Profile 解锁或可写库存引用。 */
	UPROPERTY(BlueprintReadOnly)
	FCatEquipmentLoadoutSnapshot Equipment;

	/** GameState 复制的 Run/Environment 组合快照；View 不推进阶段。 */
	UPROPERTY(BlueprintReadOnly)
	FCatRunPublicState Run;

	/** GameState 最近一次手动或巨鱼求助；View 只表现范围与类型。 */
	UPROPERTY(BlueprintReadOnly)
	FCatHelpSignalSnapshot HelpSignal;
};

/** Lake 原生状态 View；唯一职责是把 FCatSurvivalViewState 渲染成文本，不查询或写入任何玩法宿主。 */
UCLASS()
class CATFISHING_API UCatSurvivalWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 接收 LocalPlayer 子系统生成的完整只读投影并一次性刷新显示；不缓存 Model 或发出玩法命令。 */
	void Render(const FCatSurvivalViewState& ViewState);

protected:
	/** Widget 初始化时构造最小文本树；只创建白盒表现，不绑定 Pawn、ASC、PlayerState、OSS 或输入。 */
	virtual void NativeOnInitialized() override;

private:
	/** Hunger 数值文本节点；仅由 Render 写入，WidgetTree 持有实际生命周期。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HungerText;

	/** Fatigue 数值文本节点；仅由 Render 写入，WidgetTree 持有实际生命周期。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FatigueText;

	/** Poison、搏斗资源、Condition、装备、Run、天气与求助的多行摘要；仅由 Render 一次性覆盖。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;
};
