#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CatInteractable.generated.h"

class AController;

UINTERFACE(BlueprintType)
class CATFISHING_API UCatInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 通用 E 键交互目标合同。准星扫描、提示和确认只依赖该接口，不按鱼、商人或容器类型分支。
 * Focus 只改变本机表现；需要写权威状态的实现必须在 Interact 中转发服务器并重新校验。
 */
class CATFISHING_API ICatInteractable
{
	GENERATED_BODY()

public:
	/** 当前请求者是否可以看到并执行交互；本地扫描和服务器转发均会调用。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	bool CanInteract(AController* RequestingController) const;
	virtual bool CanInteract_Implementation(AController* RequestingController) const
	{
		return RequestingController != nullptr;
	}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	void BeginLocalFocus();
	virtual void BeginLocalFocus_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	void EndLocalFocus();
	virtual void EndLocalFocus_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	FText GetInteractionPrompt() const;
	virtual FText GetInteractionPrompt_Implementation() const { return FText::GetEmpty(); }

	/** 目标允许的交互距离；容器等服务器触达校验可复用，0 表示没有对外距离声明。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	double GetInteractionRadius() const;
	virtual double GetInteractionRadius_Implementation() const { return 0.0; }

	/**
	 * 唯一交互确认入口。本地 UI 目标可直接打开自己的页面；权威玩法目标由实现者转发
	 * ACatfishingPlayerController::ServerRequestInteraction，服务器会在基础 gate 后再次调用同一函数。
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	bool Interact(AController* RequestingController, FGuid RequestId);
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) { return false; }
};
