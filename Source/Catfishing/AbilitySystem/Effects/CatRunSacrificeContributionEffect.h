#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CatRunSacrificeContributionEffect.generated.h"

/** 已提交献祭的即时 Run GE；它只承载冻结原始贡献并委托 ExecCalc 增加进度。 */
UCLASS()
class CATFISHING_API UCatGE_RunApplySacrifice : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 配置即时执行计算，应用后不保留持续 Modifier，实际进度由 AttributeSet 保存和复制。 */
	UCatGE_RunApplySacrifice();
};
