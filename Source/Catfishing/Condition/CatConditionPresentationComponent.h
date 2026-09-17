#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CatConditionPresentationComponent.generated.h"

class UAnimSequence;
class UAnimMontage;
class UAbilitySystemComponent;

/** ASC 倒地标签的动画消费者；动画阶段只控制姿势，不裁决倒地或恢复。 */
UCLASS(ClassGroup=(Catfishing))
class CATFISHING_API UCatConditionPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	/** 启用动画完成后的阶段推进，并装配默认猫骨架的五段姿势资源。 */
	UCatConditionPresentationComponent();
	/** 供表现诊断读取当前姿势阶段；空闲或未知索引统一标为正常移动，该名称不能代替 ASC 倒地标签判断玩法资格。 */
	FName GetObservedPosePhase() const;
protected:
	/** 开始观察身体 ASC 的倒地标签，并立即对齐初始姿势。 */
	virtual void BeginPlay() override;
	/** 解绑原 ASC 并停止本组件创建的动态蒙太奇，避免退出后继续播放。 */
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	/** 按本机片段时长推进过渡姿势；倒地循环与正常移动不自动切换阶段。 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	/** 倒地标签变化时更新本机姿势，初次绑定也立即刷新。 */
	void RefreshCondition(FGameplayTag Tag = FGameplayTag(), int32 Count = 0);
	/** 停止上一姿势后播放指定片段并记录结束时刻；缺少资源仍推进显示阶段，不改变玩法状态。 */
	void PlayPhase(int32 NewPhase);
	/** 正在观察的身体 ASC；BeginPlay 绑定，EndPlay 从同一实例解绑。 */
	UPROPERTY() TObjectPtr<UAbilitySystemComponent> AbilitySystem;
	/** 本组件创建的当前姿势蒙太奇；阶段切换或退出时仅停止此实例。 */
	UPROPERTY() TObjectPtr<UAnimMontage> ActiveMontage;
	/** 按坐下、躺下、躺卧循环、起坐、站起排列的动画片段；角色资产可按自己的骨架覆盖，播放时只读。 */
	UPROPERTY(EditDefaultsOnly, Category="Catfishing|Animation") TArray<TObjectPtr<UAnimSequence>> PoseClips;
	/** 当前动画阶段索引，INDEX_NONE 表示正常移动；仅是本机表现进度，不是倒地真相。 */
	int32 Phase = INDEX_NONE;
	/** 当前片段预计结束的 World 秒数；播放阶段写入，Tick 用它推进非循环动画。 */
	double PhaseEndsAt = 0;
	/** 上次已用于选择姿势的倒地观察值；过滤重复播放，不供玩法判断读取。 */
	bool bObservedDowned = false;
	/** 是否已经对齐过一次 ASC 状态；让首次观察也能够启动正确姿势。 */
	bool bInitialized = false;
};
