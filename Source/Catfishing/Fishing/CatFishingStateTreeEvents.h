#pragma once

#include "NativeGameplayTags.h"

// Tag 对象带 CATFISHING_API 导出：CatfishingEditor 模块的资产生成 Commandlet 要拿同一个对象给 ST_FishingSession 写事
// 件边，不允许在那边重抄字符串造出第二份事件集合（与 Run 事件同一约定）。
namespace CatFishingStateTreeEvents
{
	/**
	 * 钓手在真咬期提竿（第一次"拖"意图到达服务器）；ST_FishingSession 资产负责把 TrueBiteWindow 转向 HookedFight。是
	 * 否完美中鱼由会话按时间戳另行记录，本事件不携带。
	 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(HookSet);
}
