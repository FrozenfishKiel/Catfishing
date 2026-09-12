#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatFishDataStatics.generated.h"

class UCatFishDefinition;
class UCatFishPresentationDefinition;

/**
 * 鱼表数据的蓝图只读入口。
 *
 * 存在的理由：表现层（鱼缸里游动的鱼、浮漂漂讯、水面特效）需要按 FishDefinitionId 拿到鱼种与表现资产，
 * 而正式目录 UCatFishCatalogSettings 不是 BlueprintType、也不该让蓝图去遍历 Definitions 数组。
 * 这里只转发目录已有的 fail-closed 查询，不维护第二份鱼表，也不提供任何写口。
 */
UCLASS()
class CATFISHING_API UCatFishDataStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 按稳定 ID 取已启用且完整的鱼定义；查不到或重复 ID 返回空，调用方按「这条鱼不存在」处理。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	static UCatFishDefinition* FindFishDefinition(FName FishDefinitionId);

	/** 按稳定 ID 取该鱼种的表现定义（网格／AnimBP／四段动画／缩放／Transform／漂讯与水面槽位）。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fish")
	static UCatFishPresentationDefinition* FindFishPresentationDefinition(FName FishDefinitionId);
};
