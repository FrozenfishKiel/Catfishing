#pragma once

#include "NativeGameplayTags.h"

/** 独立物品栏的原生输入意图；键位资产可自由调整，Controller 只按这些稳定标签选择或使用当前格。 */
namespace CatInventoryInputTags
{
	/** 选择物品栏第一个槽位的意图；鱼竿经服务器确认后立即装备，其他物品等待左键使用。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SelectSlot1);
	/** 选择物品栏第二个槽位的意图；空槽同样是有效焦点，供玩家预先安排后续拾取。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SelectSlot2);
	/** 选择物品栏第三个槽位的意图；实际使用时服务器才按提交的槽位和实例身份复核。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SelectSlot3);
	/** 选择物品栏第四个槽位的意图；容量不足时 Controller 会拒绝而不重定向到其他格。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SelectSlot4);
	/** 选择当前槽位前一格的意图；Controller 按背包实际格数循环，包含空格。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SelectPreviousSlot);
	/** 选择当前槽位后一格的意图；Controller 按背包实际格数循环，包含空格。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_SelectNextSlot);
	/** 使用当前本地选中槽位的意图；服务器按提交的槽位和观察到的实例身份裁决，不能自动改用其它格。 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_UseSelectedItem);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_ParkRod);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_PackRod);
}
