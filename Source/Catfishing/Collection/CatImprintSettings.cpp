#include "Collection/CatImprintSettings.h"

// 准入判断流程：先挡掉 None，再在总清单里做精确 FName 比较。
// 这里刻意不做去空白或大小写归一：EventId 是各来源册写死在配置或资产里的稳定名，不是玩家输入，
// 做模糊匹配只会让「清单里到底有没有这一条」变得不可断言。空清单自然走完循环返回 false。
bool UCatImprintSettings::IsImprintEventAllowed(const FName EventId) const
{
	return !EventId.IsNone() && AllowedImprintEventIds.Contains(EventId);
}
