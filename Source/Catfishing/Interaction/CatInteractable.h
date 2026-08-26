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
 * 通用交互目标合同。Focus 只改变本机表现，绝不复制；真正写玩法状态的请求必须在 authority 重新校验。
 */
class CATFISHING_API ICatInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	void BeginLocalFocus();
	virtual void BeginLocalFocus_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	void EndLocalFocus();
	virtual void EndLocalFocus_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Catfishing|Interaction")
	FText GetInteractionPrompt() const;
	virtual FText GetInteractionPrompt_Implementation() const { return FText::GetEmpty(); }

	/** 客户端 Target 只是提示；实现者必须自行复核距离、视线、身份和可用状态。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintAuthorityOnly, Category="Catfishing|Interaction")
	void RequestInteractionFromAuthority(AController* RequestingController, FGuid RequestId);
	virtual void RequestInteractionFromAuthority_Implementation(AController* RequestingController, FGuid RequestId) {}
};
