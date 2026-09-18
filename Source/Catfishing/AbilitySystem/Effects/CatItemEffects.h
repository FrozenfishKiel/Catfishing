#pragma once
#include "GameplayEffect.h"
#include "NativeGameplayTags.h"
#include "CatItemEffects.generated.h"

/** 物品效果生命周期标签；结算读取状态，不按具体物品 ID 判断。 */
namespace CatItemEffectTags
{
 /** 下一次选鱼使用的概率效果；抽样提交后移除，失败准入不消费。 */
 CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(NextFish);
 /** 一场搏斗使用的减耗效果；参与后绑定会话，离手不重置，终局移除。 */
 CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(NextFight);
 /** 减耗效果已被某场搏斗接管；阻止另一会话重复认领同一个效果。 */
 CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(FightBound);
 /** 补充绿色体力的即时数值；GA 按当前缺额填入，不恢复黄色体力。 */
 CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RestoreAmount);
 /** 喷水的表现通知；水枪和假鱼共用，不能借此施加伤害或中断动作。 */
 CATFISHING_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Splash);
}

/** 即时恢复绿段体力；数值由使用能力按提交瞬间的实际缺额填入。 */
UCLASS()
class CATFISHING_API UCatGE_RestoreStamina : public UGameplayEffect
{
 GENERATED_BODY()
public:
 /** 配置即时加值，不保存另一个体力余额。 */
 UCatGE_RestoreStamina();
};
/** 下一次概率结算的权重修正；实际数值可由效果蓝图覆盖。 */
UCLASS()
class CATFISHING_API UCatGE_LuckyCatch : public UGameplayEffect
{
 GENERATED_BODY()
public:
 /** 配置无限时长的待结算效果及互斥标签，结算成功后显式消费。 */
 UCatGE_LuckyCatch();
};
/** 一场搏斗的体力成本修正；效果本身不判断主钓与协助。 */
UCLASS()
class CATFISHING_API UCatGE_FightEfficiency : public UGameplayEffect
{
 GENERATED_BODY()
public:
 /** 配置待参与的减耗效果；钓鱼生命周期负责结束这次效果。 */
 UCatGE_FightEfficiency();
};
/** 短时湿毛表现；只授予表现状态与 GameplayCue，没有数值修饰。 */
UCLASS()
class CATFISHING_API UCatGE_ItemSplash : public UGameplayEffect
{
 GENERATED_BODY()
public:
 /** 配置持续表现及 GC，角色在期间仍能正常移动和搏斗。 */
 UCatGE_ItemSplash();
};
