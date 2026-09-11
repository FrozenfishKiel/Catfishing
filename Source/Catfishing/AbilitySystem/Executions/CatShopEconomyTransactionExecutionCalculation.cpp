#include "AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.h"

#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"

// 售鱼估价流程：逐行按鱼种 ID 查系数，以 double 计算并逐条四舍五入；任一输入无法形成精确非负整数时不产出部分金额。
bool UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(const UDataTable* PriceTable,
	const TArray<FCatShopFishSaleLine>& Fish, int32& OutSaleValue)
{
	OutSaleValue = 0;
	if (!PriceTable || Fish.IsEmpty())
	{
		return false;
	}
	int64 TotalValue = 0;
	for (const FCatShopFishSaleLine& Line : Fish)
	{
		if (Line.FishDefinitionId.IsNone() || !FMath::IsFinite(Line.WeightKilograms)
			|| Line.WeightKilograms <= 0.0)
		{
			return false;
		}
		const FCatShopFishSalePriceRow* Row = PriceTable->FindRow<FCatShopFishSalePriceRow>(Line.FishDefinitionId,
			TEXT("FishSale"), false);
		if (!Row || !Row->IsRuntimeReady())
		{
			return false;
		}
		const double RawValue = static_cast<double>(Row->MoneyCoefficient) * static_cast<double>(Line.WeightKilograms);
		// 边界约束应用于逐鱼舍入后的金币；先限制 double 转整数的输入，再检查整数，允许上限内的小数尾部。
		if (!FMath::IsFinite(RawValue) || RawValue < 0.0 || RawValue >= 16777216.5)
		{
			return false;
		}
		const int32 RoundedValue = static_cast<int32>(FMath::RoundToInt(RawValue));
		if (RoundedValue < 0 || TotalValue > 16777216 - RoundedValue)
		{
			return false;
		}
		TotalValue += RoundedValue;
	}
	OutSaleValue = static_cast<int32>(TotalValue);
	return true;
}

// GE 执行流程：先要求权威 ASC 和未消费的 source；售鱼重新检查实例并在此逐鱼查表、舍入、求和，购买只接受非正增量。
// 随后读取唯一余额的基础值和当前值，拒绝非整数、临时修饰或越界；最后只输出一次 modifier，并留下本次执行的金额回执。
void UCatShopEconomyTransactionExecutionCalculation::Execute_Implementation(
	const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	UCatShopEconomyTransactionSource* Source = Cast<UCatShopEconomyTransactionSource>(
		ExecutionParams.GetOwningSpec().GetContext().GetSourceObject());
	UAbilitySystemComponent* ASC = ExecutionParams.GetTargetAbilitySystemComponent();
	const AActor* Owner = ASC ? ASC->GetOwnerActor() : nullptr;
	if (!Source || Source->bExecuted || !Owner || !Owner->HasAuthority())
	{
		return;
	}
	int32 Delta = Source->WalletDelta;
	bool bValid = Delta <= 0;
	if (!Source->Fish.IsEmpty())
	{
		TSet<FGuid> SeenFish;
		bValid = true;
		for (const FCatShopFishSaleLine& Line : Source->Fish)
		{
			if (!Line.FishInstanceId.IsValid() || SeenFish.Contains(Line.FishInstanceId))
			{
				bValid = false;
				break;
			}
			SeenFish.Add(Line.FishInstanceId);
		}
		bValid = bValid && TryCalculateFishSale(Source->PriceTable.Get(), Source->Fish, Delta);
	}
	const FGameplayAttribute Attribute = UCatEconomyAttributeSet::GetTeamWalletBalanceAttribute();
	const double Balance = ASC->GetNumericAttribute(Attribute);
	const double NextBalance = Balance + static_cast<double>(Delta);
	if (!bValid || !ASC->HasAttributeSetForAttribute(Attribute) || !FMath::IsFinite(Balance)
		|| Balance < 0.0 || Balance > 16777216.0 || Balance != FMath::RoundToDouble(Balance)
		|| ASC->GetNumericAttributeBase(Attribute) != Balance || NextBalance < 0.0 || NextBalance > 16777216.0)
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=WalletExecutionRejected Request=%s World=%s NetMode=%d Authority=1 Actor=%s Role=%d Result=InvalidInputOrBalance"),
			*Source->RequestId.ToString(), *GetNameSafe(Owner->GetWorld()), static_cast<int32>(Owner->GetNetMode()),
			*Owner->GetName(), static_cast<int32>(Owner->GetLocalRole()));
		return;
	}
	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
		Attribute, EGameplayModOp::Additive, static_cast<float>(Delta)));
	Source->WalletDelta = Delta;
	Source->bExecuted = true;
}
