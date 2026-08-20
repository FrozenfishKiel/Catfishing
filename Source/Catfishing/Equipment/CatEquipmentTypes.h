#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatEquipmentTypes.generated.h"

/** 功能型装备/道具类别；不存在品质、等级、随机词条或通用战力轴。 */
UENUM(BlueprintType)
enum class ECatEquipmentKind : uint8
{
	/** 数据尚未声明功能类别；运行目录必须拒绝该定义，不能把它当通用道具。 */
	Unknown,
	/** 有耐久的鱼竿。 */
	Rod,
	/** 鱼饵；普通饵与特殊饵都是一局消耗品（飞书 §3.4），只在鱼偏好、进货限量和丢饵预算上区分身份。 */
	Bait,
	/** 四种正式玩法路线之一的鱼漂。 */
	Float,
	/** 近岸抢抄工具；它不改变首个合法抢抄规则。 */
	ScoopNet,
	/** 共享聚鱼池的窝料。 */
	Chum,
	/** 旧草药恢复路径的保留枚举；WORK-04 起不再允许进入 Runtime Catalog。 */
	Herb,
	/** 旧修竿材料路径的保留枚举；WORK-04 起不再允许进入 Runtime Catalog。 */
	Driftwood,
	/** 其他正式一次性特效道具。 */
	Utility
};

/** 一次钓鱼失败预算允许的唯一惩罚；None 与两个正式结果之外没有第二刀。 */
UENUM(BlueprintType)
enum class ECatFishingFailurePenalty : uint8
{
	/** 失败发生但本次不提交物资惩罚。 */
	None,
	/** 只损失一份已选特殊鱼饵；普通饵的每次咬钩扣饵走 Fishing 的 Bait 语义链，不走这条失败预算。 */
	LoseSpecialBait,
	/** 旧失败预算伤竿选择；WORK-04 起不直接扣耐久，正式断竿由 Fight Resource Cursor 产生。 */
	DamageRod
};

/** 一局耗材堆叠；数量随 World/Character 清空，不进入 Profile。 */
USTRUCT(BlueprintType)
struct FCatRunConsumableStack
{
	GENERATED_BODY()

	/** EquipmentDefinition 稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 当前一局数量；只由 authority EquipmentComponent 写入。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Quantity = 0;
};

/** Character 当前功能型装配的复制读模型；解锁与跨局选择仍在本地 Profile。 */
USTRUCT(BlueprintType)
struct FCatEquipmentLoadoutSnapshot
{
	GENERATED_BODY()

	/** 每次装配、耗材、耐久或维修提交后递增。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前鱼竿稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName RodDefinitionId = NAME_None;

	/** 当前鱼饵稳定 ID，表示"现在挂的是哪种饵"；它对应的随身份数另在 Consumables 里按同一定义 ID 计数。 */
	UPROPERTY(BlueprintReadOnly)
	FName BaitDefinitionId = NAME_None;

	/** 当前鱼漂稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName FloatDefinitionId = NAME_None;

	/** 当前鱼竿耐久；没有合法鱼竿时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RodDurability = 0.0;

	/**
	 * 当前鱼竿是否已断；只有 FishingSession 通过 CommitFightRodDurabilityFromAuthority 把耐久磨到 0 才会把它写成
	 * true（判定表①强度断竿也走这条路）。
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;

	/** 一局消耗品数量，按定义 ID 分栈：普通饵、特殊饵、窝料都在这里；不包含跨局解锁。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatRunConsumableStack> Consumables;
};

/**
 * 团队装备库里的一件实物。飞书商店册 §3.2 写"买来的物品进团队装备库"，所以这是一件东西被买下来之后在局内的落点，
 * 和挂在某只猫身上的装配是两回事：装配表达"现在谁在用"，这里表达"这局的队伍手上有什么"。
 * 一件实例对应一次购买或一次免费自取，随局存在，World 结束就没了。
 */
USTRUCT(BlueprintType)
struct FCatTeamEquipmentInstance
{
	GENERATED_BODY()

	/** 这件东西在本局的稳定 ID；它同时被当作交回商店的交付回执 ID，让账本能指回具体哪一件。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid InstanceId;

	/** 对应的 EquipmentDefinition 稳定 ID；入库时已经过运行目录校验，不是随手记下的名字。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 从定义冻结下来的功能类别；后续资产改类别不改变已入库实例的性质。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEquipmentKind Kind = ECatEquipmentKind::Unknown;

	/** 把这件东西买进来的那笔商店订单；它是"同一笔订单只入库一次"的判断依据。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SourceTransactionId;
};

/** 团队装备库的复制读模型；全队共有一份，客户端只读，取用规则不在这份快照里。 */
USTRUCT(BlueprintType)
struct FCatTeamEquipmentLibrarySnapshot
{
	GENERATED_BODY()

	/** 每次成功入库后递增；入库命令用它做乐观并发前提，商店也用它作为交付回执的下游版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前库里的全部实物，按入库顺序排列。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatTeamEquipmentInstance> Instances;
};

/** 把一笔已经付过款的商店订单交付进团队装备库的命令；Shop 不自己创建实例，只能提交这条命令。 */
USTRUCT(BlueprintType)
struct FCatTeamEquipmentGrantCommand
{
	GENERATED_BODY()

	/** RequestId、装备库 ExpectedRevision 与服务器重建的身份；客户端不能直接指定要入库什么。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 触发这次入库的商店账本 ID；同一笔订单重复提交只会拿回第一次入库的那件实物。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid SourceTransactionId;

	/** 要入库的 EquipmentDefinition 稳定 ID；装备库会自己去运行目录核对，不信任调用方说它合法。 */
	UPROPERTY(BlueprintReadWrite)
	FName DefinitionId = NAME_None;
};

/** 从团队装备库按实例取走一件实物的命令；取走之后这件东西归取用者装配，装备库里不再有它。 */
USTRUCT(BlueprintType)
struct FCatTeamEquipmentTakeCommand
{
	GENERATED_BODY()

	/** RequestId、装备库 ExpectedRevision 与服务器重建的取用者身份；客户端只能指定要哪一件，身份由 RPC 层重建。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 要取走的那件实物在本局的稳定 ID；装备库按它定位，查不到就拒绝，不按定义名"随便拿一件同款"。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid InstanceId;
};

/** 入库/取用命令的终态；成功和合法重放都会带回那件实物，入库时供商店拿它当交付回执，取用时供 Equipment 装配。 */
USTRUCT(BlueprintType)
struct FCatTeamEquipmentGrantResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 对应团队装备库聚合。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 本次入库或此前已入库的那件实物；拒绝时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatTeamEquipmentInstance Instance;
};

/** 一次失败预算提交结果；明确记录唯一选择的惩罚。 */
USTRUCT(BlueprintType)
struct FCatFishingFailureResult
{
	GENERATED_BODY()

	/** 公共幂等终态；Revision 对应 Equipment 聚合。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 首次提交的唯一惩罚类别。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingFailurePenalty Penalty = ECatFishingFailurePenalty::None;

	/** 惩罚后的鱼竿耐久；丢饵时保持原值。 */
	UPROPERTY(BlueprintReadOnly)
	double RemainingRodDurability = 0.0;
};
