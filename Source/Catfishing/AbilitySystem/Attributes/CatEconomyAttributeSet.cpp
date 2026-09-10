#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"

#include "AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// 金额规整流程：先拒绝非有限值，再在 double 中夹限和舍入后转回 float；避免 float 加 0.5 把 8388609 等合法奇数改成相邻整数。
	float ClampTeamWalletBalance(const float Value)
	{
		return FMath::IsFinite(Value)
			? static_cast<float>(FMath::RoundToDouble(FMath::Clamp(static_cast<double>(Value), 0.0, 16777216.0))) : 0.0f;
	}
}

// 复制注册流程：只复制唯一团队余额，版本和交易账本仍由 ShopEconomy 服务维护其事务语义。
void UCatEconomyAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UCatEconomyAttributeSet, TeamWalletBalance, COND_None, REPNOTIFY_Always);
}

// 基础值规整流程：GAS 修改基础值时统一夹回可精确表示的非负整数；Init 访问器绕过此钩子，其输入边界由服务读模型另行校验。
void UCatEconomyAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	if (Attribute == GetTeamWalletBalanceAttribute())
	{
		NewValue = ClampTeamWalletBalance(NewValue);
	}
}

// 当前值规整流程：聚合后的观察值也使用同一边界，购买与售鱼不会从不同读取路径得到不同金额。
void UCatEconomyAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetTeamWalletBalanceAttribute())
	{
		NewValue = ClampTeamWalletBalance(NewValue);
	}
}

// 执行确认流程：先交父类，再仅对团队余额读取交易 source；执行器已产出 modifier 时标记应用回执，不在此重新估价或修改余额。
void UCatEconomyAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	if (Data.EvaluatedData.Attribute == GetTeamWalletBalanceAttribute())
	{
		if (UCatShopEconomyTransactionSource* Source = Cast<UCatShopEconomyTransactionSource>(Data.EffectSpec.GetContext().GetSourceObject());
			Source && Source->bExecuted)
		{
			Source->bBalanceApplied = true;
		}
	}
}

// 复制通知流程：把旧值交回 GAS，让监听团队余额的界面得到标准属性更新而不重算交易。
void UCatEconomyAttributeSet::OnRep_TeamWalletBalance(const FGameplayAttributeData& OldTeamWalletBalance)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCatEconomyAttributeSet, TeamWalletBalance, OldTeamWalletBalance);
}
