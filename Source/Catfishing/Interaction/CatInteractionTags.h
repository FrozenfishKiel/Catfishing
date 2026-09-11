#pragma once

#include "NativeGameplayTags.h"

namespace CatInteractionTags
{
	/** 通用准星交互意图；默认由 E 触发，不与具体物品或 Ability 类型绑定。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Interact);
	/** 丢弃当前嘴部携带物的按下意图；键位由 IMC 配置，Controller 只提交当前 Actor，不读取背包选中格。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_DropCarriedItem);
}
