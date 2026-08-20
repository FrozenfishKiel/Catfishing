#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Items/CatItemTypes.h"
#include "CatSocialTypes.generated.h"

class APlayerState;

/** 手动/系统求助信号类型；普通帮助必须手动，只有巨鱼搏斗允许系统全体提示。 */
UENUM(BlueprintType)
enum class ECatHelpSignalKind : uint8
{
	/** 未配置信号。 */
	Unknown,
	/** 玩家手动请求钓鱼协助。 */
	ManualFishing,
	/** 倒地玩家手动请求救援。 */
	ManualDowned,
	/** Giant FishingSession 开始时由系统发布的全体提示。 */
	GiantFishSystem
};

/** 共享鱼缸偷鱼追回权限；具体团队体验未裁时保持 Undecided。 */
UENUM()
enum class ECatSharedTankRecoveryPolicy : uint8
{
	/** 谁能追回团队鱼尚未裁决，偷共享缸路径 fail-closed。 */
	Undecided,
	/** 只有 FishInstance 原主人能追回。 */
	OriginalOwner,
	/** 任一当前 Active 玩家都能追回。 */
	AnyActivePlayer
};

/** GameState 复制的最近求助信号；客户端表现只消费，不据此自动加入玩法。 */
USTRUCT(BlueprintType)
struct FCatHelpSignalSnapshot
{
	GENERATED_BODY()

	/** 每次服务器接受信号生成的稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SignalId;

	/** 手动或 Giant 系统提示类型。 */
	UPROPERTY(BlueprintReadOnly)
	ECatHelpSignalKind Kind = ECatHelpSignalKind::Unknown;

	/** 信号来源的服务器世界坐标；附近提示按 Radius 表现。 */
	UPROPERTY(BlueprintReadOnly)
	FVector SourceLocation = FVector::ZeroVector;

	/** 非全局手动信号的感知半径，单位厘米；全局信号为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RadiusCentimeters = 0.0;

	/** 只有 GiantFishSystem 为 true；普通求助不能借此升级为全体任务。 */
	UPROPERTY(BlueprintReadOnly)
	bool bGlobal = false;

	/** GameState 发布序号；客户端忽略陈旧信号。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/** 偷鱼开始命令；客户端只提供稳定意图，Social/Items 在服务器重建身份并裁决唯一实物鱼。 */
USTRUCT(BlueprintType)
struct FCatTheftCommand
{
	GENERATED_BODY()

	/** RequestId 与源容器 ExpectedRevision；StableNetId 由服务器覆盖。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 要偷的一条鱼实例。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid FishInstanceId;

	/** 他人鱼护或共享鱼缸 ID。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid SourceContainerId;
};

/** Social 偷鱼协议公开结果；只暴露阶段和结构化错误，不复制受害者 StableNetId。 */
USTRUCT(BlueprintType)
struct FCatTheftResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 对应源容器。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 首次合法 Begin 后由服务器分配的协议 ID；追回 RPC、Timer 和 Items escrow 只使用此键。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid TheftProtocolId;

	/** 进入追回窗口的 FishInstanceId。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 本次是否仍处于进食前追回窗口。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRecoveryWindowOpen = false;

	/** 本次是否已被合法追回并物归原主。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReturned = false;

	/** 本次是否已过窗口并不可逆吃掉。 */
	UPROPERTY(BlueprintReadOnly)
	bool bConsumed = false;

	/** 本次是否已由小偷在追回窗口内售出并入账团队钱包。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSold = false;

	/** 售出终态提交后的团队钱包 Revision；非售出终态保持 0。 */
	UPROPERTY(BlueprintReadOnly)
	int64 EconomyRevision = 0;
};

/**
 * 一只猫正把一条鱼叼在嘴里这件公开事实。
 * 它存在的唯一理由是让"叼鱼逃跑的猫全场显眼"：受害者和旁观者必须能在客户端认出谁在跑，追回窗口才有人去追。
 * 因此这里只放旁观者判断"要不要追这只猫"所需的最小信息，受害者身份和服务器偷鱼协议 ID 都不进来——
 * 前者是飞书尚未裁决的通知问题，后者是服务器私有键，客户端拿到也只会变成伪造追回命令的材料。
 */
USTRUCT(BlueprintType)
struct FCatStolenFishCarrySnapshot
{
	GENERATED_BODY()

	/**
	 * 正叼着这条鱼的玩家 PlayerState。选它而不是 StableNetId，是因为 PlayerState 本身已经是全场复制的公开身份，客户端
	 * 可以直接把它对回到场上的那只猫，而 StableNetId 在本项目里始终只留在服务器。
	 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> CarrierPlayerState = nullptr;

	/** 嘴里这条鱼的鱼种定义 ID；客户端据此显示是哪种鱼，让"明晃晃的一条鱼"能被认出来。它不携带重量、主人或价值。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/**
	 * 这条鱼是不是偷来的赃物。当前唯一写入方是 Social 的偷鱼协议，所以服务器发布的条目恒为 true；保留这个显式字段是为
	 * 了让表现层读事实而不是靠"出现在列表里就是赃物"的隐含约定去反推。
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bStolen = false;

	/** 该条目所属的公开列表版本；服务器每次改变叼鱼集合都会推进，客户端据此丢弃陈旧快照。同一次发布内所有条目共用一个值。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/**
 * 一局内由局主裁决的 Social 权限事实。
 * 偷取和恶作剧这两道护栏必须能在开局之后被局主关掉，所以运行期权限的唯一真相在这里，而不在 UCatSocialSettings；
 * Settings 里的同名项只提供开局默认值，服务初始化后就不再参与裁决。
 */
USTRUCT(BlueprintType)
struct FCatSocialPolicySnapshot
{
	GENERATED_BODY()

	/** 当前是否允许发起偷鱼；Unset 表示尚未裁决，与 Disabled 一样不放行，但拒绝原因不同。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainPolicy TheftPermission = ECatDomainPolicy::Unset;

	/** 当前是否允许发起普通恶作剧；它不影响防骚扰牌能否放置，牌子仍是独立护栏。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainPolicy MischiefPermission = ECatDomainPolicy::Unset;

	/** 策略版本；开局初始化写 1，之后每次真正改变权限值才递增，只增不减。读方据此判断手上的策略是否已经过时。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};
