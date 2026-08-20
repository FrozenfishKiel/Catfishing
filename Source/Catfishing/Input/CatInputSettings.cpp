#include "Input/CatInputSettings.h"

// 运行判断流程：先要求显式 gate，再检查是否至少存在一条非空 MappingContext；优先级和层级不替代资产事实。
bool UCatInputSettings::IsRuntimeReady() const
{
	if (!bEnableGlobalInputContexts)
	{
		return false;
	}
	for (const FCatInputMappingContextConfig& Context : MappingContexts)
	{
		if (!Context.MappingContext.IsNull())
		{
			return true;
		}
	}
	return false;
}

// 上下文收集流程：只输出非空资产引用，并以 Priority 为主序、Layer 和资产路径为同级稳定序；协调器后续再负责同步加载和去重。
void UCatInputSettings::GetRuntimeContexts(TArray<FCatInputMappingContextConfig>& OutContexts) const
{
	OutContexts.Reset();
	if (!bEnableGlobalInputContexts)
	{
		return;
	}
	for (const FCatInputMappingContextConfig& Context : MappingContexts)
	{
		if (!Context.MappingContext.IsNull())
		{
			OutContexts.Add(Context);
		}
	}
	OutContexts.Sort([](const FCatInputMappingContextConfig& Left, const FCatInputMappingContextConfig& Right)
	{
		if (Left.Priority != Right.Priority)
		{
			return Left.Priority < Right.Priority;
		}
		if (Left.Layer != Right.Layer)
		{
			return static_cast<uint8>(Left.Layer) < static_cast<uint8>(Right.Layer);
		}
		return Left.MappingContext.ToSoftObjectPath().ToString()
			< Right.MappingContext.ToSoftObjectPath().ToString();
	});
}

// 角色输入判断流程：与 MappingContext 共用同一个总 gate，再要求 Move 与 Look 两个软引用都非空；Jump 缺失只影响跳跃，不阻断基础移动。
bool UCatInputSettings::IsCharacterInputReady() const
{
	return bEnableGlobalInputContexts && !MoveAction.IsNull() && !LookAction.IsNull();
}
