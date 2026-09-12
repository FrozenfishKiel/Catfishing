#pragma once

#include "CoreMinimal.h"
#include "CatGrowthTypes.generated.h"

/**
 * 三选一选项池的稳定身份；每一项对应「升级效果」页 §2 表里的一行。
 * 枚举只声明身份，每次数值/上限/出现次序全部由 CatGrowthSettings 的配表行持有，代码里不写死数字。
 * 池上限 16（升级效果页 §1 准入三闸）；新增项必须同时补配表行，否则该项永远抽不到。
 */
UENUM(BlueprintType)
enum class ECatGrowthOptionId : uint8
{
	/** 未配置；配表里出现它表示这一行无效，抽取时跳过。 */
	None = 0,
	/** 力量：猫的力量值，参与搏斗全部力量比较与消耗公式。 */
	FishingStrength,
	/** 搏斗体力上限：体力条上限；提升时当场按差值补满。 */
	MaxFightStamina,
	/** 放线回体速度：搏斗内放线回体速率（基础 0，选了才有）。 */
	SlackStaminaRegen,
	/** buff 持续时长：所有限时 buff 的持续时间；不作用于猫神的祝福。 */
	BuffDuration,
	/** 移动速度：世界移动速度。 */
	MoveSpeed,
	/** 背包格数：格数制携带的背包格。 */
	InventorySlots,
	/** 完美窗加宽：完美提竿判定窗。 */
	PerfectWindow,
	/** 后勤扩容：窝料与鱼饵随身上限。 */
	SupplyCapacity,
	/** 咬钩间隔缩短：自己浮漂的咬钩等待间隔。 */
	BiteInterval,
	/** 渔获重量上浮：钓上的鱼重量 roll 上浮。 */
	CatchWeight,
	/** 竿耐久磨损减免：全部竿耐久损耗。 */
	RodWear
};

/** 一项已被选中并累计生效的成长加成；「本局永久、同项可跨次叠加」的唯一事实载体。 */
USTRUCT(BlueprintType)
struct FCatGrowthOptionStack
{
	GENERATED_BODY()

	/** 该条记录对应的选项身份。 */
	UPROPERTY(BlueprintReadOnly)
	ECatGrowthOptionId OptionId = ECatGrowthOptionId::None;

	/** 本局累计选中次数；三选一卡面按它显示「已选 N 次」。 */
	UPROPERTY(BlueprintReadOnly)
	int32 TimesChosen = 0;

	/** 本局累计生效量＝每次数值×次数，已按配表上限夹住；消费系统只读这个合计，不自己乘次数。 */
	UPROPERTY(BlueprintReadOnly)
	double TotalMagnitude = 0.0;
};

/** 猫本局吃鱼成长的复制读模型；经验槽进度、当前待选的三项与已叠 build 都在这里。 */
USTRUCT(BlueprintType)
struct FCatGrowthSnapshot
{
	GENERATED_BODY()

	/** 每次吃鱼成长提交后递增；UI 和测试用它判断整份快照是否更新。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 本局累计获得的吃鱼经验；只随当前 Character 生命周期存在，局末丢弃。 */
	UPROPERTY(BlueprintReadOnly)
	int32 TotalExperience = 0;

	/** 当前经验槽内已经累积但尚未触发下一次三选一的经验。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ExperienceInCurrentSlot = 0;

	/** 已满槽但尚未被玩家选掉的次数；连满时逐个弹出，选完一次才抽下一组。 */
	UPROPERTY(BlueprintReadOnly)
	int32 PendingChoiceCount = 0;

	/** 本局至今完成的三选一次数；配表的「出现次序」按它解锁系统档选项。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CompletedChoiceCount = 0;

	/** 当前正在等待玩家挑选的三项（互不相同）；为空表示没有待选面板。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<ECatGrowthOptionId> CurrentOffer;

	/** 当前这组待选的序号；客户端提交选择时带上它，防止面板过期后选到上一组。 */
	UPROPERTY(BlueprintReadOnly)
	int32 OfferSerial = 0;

	/** 本局已叠加成的完整 build；主动查看面板与三选一卡面的已叠次数都从这里读。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatGrowthOptionStack> Stacks;
};
