#include "Environment/CatChumFieldTypes.h"

#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"

namespace CatChumFieldTypesPrivate
{
	// 把编辑器里配置的 UCurveFloat 曲线预烘焙成运行期定长采样表，避免搏斗/衰减热路径里反复走曲线求值。
	static bool BakeCurve(const TSoftObjectPtr<UCurveFloat>& CurveReference, FCatChumFalloffTable& OutTable)
	{
		OutTable = FCatChumFalloffTable(); // 先清空，任何一步失败都保证不留半成品数据
		// 同步加载：装配/投放可能发生在曲线尚未被任何引用加载之前（如 PIE 启动即发 starter 窝料）。
		const UCurveFloat* Curve = CurveReference.LoadSynchronous();
		if (!Curve || Curve->FloatCurve.GetNumKeys() == 0)
		{
			// 曲线资产缺失或者没有任何关键帧，视为未配置，直接失败
			return false;
		}
		for (int32 Index = 0; Index < FCatChumFalloffTable::SampleCount; ++Index)
		{
			// 把采样下标线性映射到曲线的 [0,1] 输入域上
			const float Input = static_cast<float>(Index) / static_cast<float>(FCatChumFalloffTable::SampleCount - 1);
			const double Value = static_cast<double>(Curve->GetFloatValue(Input));
			if (!FMath::IsFinite(Value) || Value < 0.0)
			{
				// 曲线配置出现非法值（NaN/Inf/负数）时整表作废，防止污染后续插值结果
				OutTable = FCatChumFalloffTable();
				return false;
			}
			OutTable.Samples[Index] = Value; // 缓存该采样点的衰减系数
		}
		return OutTable.IsRuntimeReady(); // 再次校验整表是否满足运行期可用条件（如首点必须>0）
	}
}

bool FCatChumInfluenceSpec::IsRuntimeReady() const
{
	FCatChumRuntimeInfluence Runtime;
	// 用数量=1 试烘焙一次，只要能成功产出运行期数据就说明这份配置本身合法
	return BuildRuntimeInfluence(1, Runtime);
}

bool FCatChumInfluenceSpec::IsUnconfigured() const
{
	// 所有字段都停留在默认零值/空引用，说明策划还没有为这个窝料定义填数据（区别于"填了但非法"）
	return RadiusCentimeters == 0.0 && DurationSeconds == 0.0
		&& BaseContribution.Fishy == 0.0 && BaseContribution.Fragrant == 0.0 && BaseContribution.Fermented == 0.0
		&& DistanceFalloffCurve.IsNull() && TimeFalloffCurve.IsNull() && MaximumQuantityPerPlacement == 0
		&& PresentationId.IsNone() && PresentationClass.IsNull();
}

bool FCatChumInfluenceSpec::BuildRuntimeInfluence(const int32 Quantity, FCatChumRuntimeInfluence& OutRuntime) const
{
	OutRuntime = FCatChumRuntimeInfluence(); // 失败路径统一返回空结构，调用方不需要额外清理
	if (!FMath::IsFinite(RadiusCentimeters) || RadiusCentimeters <= 0.0
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0
		|| !BaseContribution.IsValidContribution() || MaximumQuantityPerPlacement <= 0
		|| Quantity <= 0 || Quantity > MaximumQuantityPerPlacement
		|| DistanceFalloffCurve.IsNull() || TimeFalloffCurve.IsNull())
	{
		// 半径/持续时间/基础贡献/数量上限任一非法，或衰减曲线没配，都不允许生成窝料场
		return false;
	}
	if (!PresentationClass.IsNull())
	{
		// 表现 Actor 类可选配置；一旦填了就必须能同步加载且是 AActor 的子类，否则直接判失败
		const UClass* LoadedClass = PresentationClass.LoadSynchronous();
		if (!LoadedClass || !LoadedClass->IsChildOf(AActor::StaticClass()))
		{
			return false;
		}
	}

	FCatChumRuntimeInfluence Candidate;
	Candidate.RadiusCentimeters = RadiusCentimeters;
	Candidate.DurationSeconds = DurationSeconds;
	// 按本次实际投放数量放大基础贡献（例如扔 3 颗窝料 = 单颗贡献 x3）
	Candidate.BaseContribution = BaseContribution.ScaledBy(static_cast<double>(Quantity));
	Candidate.PresentationId = PresentationId;
	if (!Candidate.BaseContribution.IsValidContribution()
		|| !CatChumFieldTypesPrivate::BakeCurve(DistanceFalloffCurve, Candidate.DistanceFalloff)
		|| !CatChumFieldTypesPrivate::BakeCurve(TimeFalloffCurve, Candidate.TimeFalloff))
	{
		// 放大后贡献值本身也要重新校验（防止数量过大导致数值溢出/非法）；两条衰减曲线必须都烘焙成功
		return false;
	}
	OutRuntime = MoveTemp(Candidate); // 只有全部校验通过才把候选值搬进输出，避免调用方拿到半成品
	return true;
}

double FCatChumFalloffTable::Evaluate(const double NormalizedInput) const
{
	if (!FMath::IsFinite(NormalizedInput) || !IsRuntimeReady())
	{
		// 输入非法或采样表本身未就绪，一律按“无影响”处理，而不是抛异常中断玩法逻辑
		return 0.0;
	}
	const double Clamped = FMath::Clamp(NormalizedInput, 0.0, 1.0); // 归一化输入钳制到 [0,1]
	const double Position = Clamped * static_cast<double>(SampleCount - 1); // 映射到采样表下标空间
	const int32 Lower = FMath::Clamp(FMath::FloorToInt(Position), 0, SampleCount - 1); // 下界采样点
	const int32 Upper = FMath::Min(Lower + 1, SampleCount - 1); // 上界采样点，最后一个点时钳制避免越界
	return FMath::Lerp(Samples[Lower], Samples[Upper], Position - static_cast<double>(Lower)); // 线性插值得到平滑衰减值
}

bool FCatChumFalloffTable::IsRuntimeReady() const
{
	for (const double Value : Samples)
	{
		if (!FMath::IsFinite(Value) || Value < 0.0)
		{
			// 只要有一个采样点非法（NaN/Inf/负数），整张表都不可信
			return false;
		}
	}
	// 约定：合法烘焙过的表首个采样点必须 > 0（衰减曲线起点非零），用它区分"已烘焙"与"从未烘焙的全零表"
	return Samples[0] > 0.0;
}
