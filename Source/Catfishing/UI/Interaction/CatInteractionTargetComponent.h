#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatInteractionTargetComponent.generated.h"

class APlayerController;

/** 世界交互目标的通用组件；具体对象自己拥有打开逻辑，LocalPlayer 只扫描和调用这个能力入口。 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatInteractionTargetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 构造交互目标默认值；目标默认不开 Tick，扫描方按固定节奏读取它的只读信息。 */
	UCatInteractionTargetComponent();

	/** 返回该目标当前是否允许指定玩家交互；蓝图可扩展距离外的对象状态判断。 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Catfishing|Interaction")
	bool CanInteract(APlayerController* PlayerController) const;

	/** C++ 默认可交互规则；派生组件 override 它时仍保留蓝图事件入口。 */
	virtual bool CanInteract_Implementation(APlayerController* PlayerController) const;

	/** 执行该目标的交互；基类只返回 false，具体商店、鱼缸或祭坛组件负责真正打开自己的 UI。 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Catfishing|Interaction")
	bool Interact(APlayerController* PlayerController);

	/** C++ 默认交互执行；派生组件通过 override 接入自己的打开或操作逻辑。 */
	virtual bool Interact_Implementation(APlayerController* PlayerController);

	/** 返回提示里显示的目标名称；扫描方只读它，不按具体类名判断目标类型。 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Catfishing|Interaction")
	FText GetInteractionTargetText(APlayerController* PlayerController) const;

	/** C++ 默认提示文本；派生组件可复用组件配置，也可 override 成对象自己的命名策略。 */
	virtual FText GetInteractionTargetText_Implementation(APlayerController* PlayerController) const;

	/** 返回该目标的可交互半径，单位厘米；小于等于 0 时扫描方会忽略该目标。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Interaction")
	double GetInteractionRadiusCentimeters() const;

	/** 交互是否启用；对象关闭、售罄或剧情锁定时可由蓝图或组件切换。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction")
	bool bInteractionEnabled = true;

	/** 玩家靠近时提示显示的目标名称；例如“商店”“鱼缸”“祭坛”。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction")
	FText InteractionTargetText;

	/** 玩家与目标之间允许交互的最大距离，单位厘米；扫描方使用玩家 Pawn 到 Owner Actor 的距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
	double InteractionRadiusCentimeters = 250.0;
};
