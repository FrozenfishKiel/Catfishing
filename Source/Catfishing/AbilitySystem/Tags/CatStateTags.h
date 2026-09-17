#pragma once
#include "NativeGameplayTags.h"

/** 角色可观察的状态语义；ASC 上的效果授予状态，消费者不读取状态写入者的内部枚举。 */
namespace CatStateTags
{
	/** 明确声明进入倒地时中断的能力类别；求助不带此标签，准入阻挡和运行中取消分别配置。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AbilityInterruptOnDowned);
	/** 所有角色状态的根；只读投影订阅此分支的变化。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State);
	/** 角色已倒地；能力分别配置准入阻挡与运行中取消，求助允许倒地时使用。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Downed);
	/** 毛发湿润的纯表现状态，不附加数值惩罚。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Wet);
	/** 身体在浅水中；环境采样来源保证自身浅水和危险标签互斥，干燥时撤销水域标签。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WaterShallow);
	/** 已满足深水确认阈值；钓鱼边界继续读取同源只读投影。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WaterDangerous);
	/** 疲劳表现根；此分支不参与体力计算。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fatigue);
	/** 主动消耗或持续承重阻止自然恢复，由负载采样来源授予。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RecoveryBlocked);
	/** 钓鱼负责人授予的参与搏斗状态；未接入前仍保留原权威限制。 */
	CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(FishingFight);
}
