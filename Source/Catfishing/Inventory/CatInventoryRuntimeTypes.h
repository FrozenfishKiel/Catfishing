#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatInventoryRuntimeTypes.generated.h"

/** 一局随身库存的单个格子；数组下标就是玩家看到和操作的格子位置，空格保持默认值。 */
USTRUCT(BlueprintType)
struct FCatRunInventorySlot
{
	GENERATED_BODY()

	/** 这个格子里物品的稳定库存 ID；旧装备资产会映射到自己的 EquipmentDefinitionId，运行链路仍把它当同一个库存物品身份读取。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 这个格子里这份运行期物品或堆栈的实例身份；放置 Actor、仓库转移和收回都会用它确认自己处理的是同一份物品。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ItemInstanceId;

	/** 这个格子里的堆叠数量；装备型物品固定为 1，数量型物品按配置上限在同一个格子内堆叠。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Quantity = 0;

	/** 这份实例当前携带的鱼竿耐久；只有 Rod 会读写它，其他物品保持 0，避免把工具状态藏在选择快照里。 */
	UPROPERTY(BlueprintReadOnly)
	double RodDurability = 0.0;

	/** 这份实例是否已经断竿；只有 Rod 使用它，部署 Actor 和库存 UI 都从同一实例状态同步。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};

/** 一次运行期物品 Use/UnUse 的结果；调用方拿到的是实例副本和库存版本，不需要自己改库存数组。 */
USTRUCT(BlueprintType)
struct FCatInventoryItemUseResult
{
	GENERATED_BODY()

	/** 本次使用或收回请求的关联 ID；日志、回执和上层命令用它把库存变化与世界 Actor 变化串起来。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 本次被移出、扣减或放回库存的运行期实例副本；部署回滚和收回归还都必须沿用同一 ItemInstanceId。 */
	UPROPERTY(BlueprintReadOnly)
	FCatRunInventorySlot Item;

	/** Use/UnUse 的领域结果；成功只表示库存事务成立，不代表调用方后续 Actor 生成一定成功。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** 库存事务结束后的正式库存内容版本；Use/UnUse 成功、失败或回放时都尽量写入，调用方用它确认背包事实推进到哪一版。 */
	UPROPERTY(BlueprintReadOnly)
	int64 InventoryRevision = 0;

	/** 库存 Use/UnUse 同步旧 Equipment 读模型后的版本；迁移期保留给旧前端和旧钓鱼命令回执，新调用方应优先读取 InventoryRevision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	/** 本次调用是否实际改变了库存或活动使用记录；重放、无实现或已收口路径会保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	/** 本结果是否来自同一 Use/UnUse 请求的终态缓存；协调器用它判断是否需要补放后续领域提交。 */
	UPROPERTY(BlueprintReadOnly)
	bool bTerminalReplay = false;

	/** 终态重放对应的首次库存请求是否真的扣量、移出或放回；失败重放不会驱动草药恢复等后续效果。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReplayedTerminalCommitted = false;

	/** 终态重放对应的首次库存错误；非重放结果保持默认，成功重放为 None，失败重放继续暴露库存拒绝原因。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError ReplayedTerminalError = ECatDomainCommandError::InvalidPayload;
};

/** 把库存 Use/UnUse 的首次终态改写成可诊断重放；成功重放显示 AlreadyResolved，失败重放继续暴露首次失败。 */
inline void MarkInventoryItemUseReplayed(FCatInventoryItemUseResult& Result)
{
	// 重放标记流程：先保留首次终态是否提交和原始错误，再把当前返回改成“本次没有再提交”，让外层能区分首次成功和幂等回包。
	const bool bOriginalCommitted = Result.bCommitted;
	const ECatDomainCommandError OriginalError = Result.Error;
	Result.bCommitted = false;
	Result.bTerminalReplay = true;
	Result.bReplayedTerminalCommitted = bOriginalCommitted;
	Result.Error = bOriginalCommitted && OriginalError == ECatDomainCommandError::None
		? ECatDomainCommandError::AlreadyResolved : OriginalError;
	Result.ReplayedTerminalError = OriginalError;
}

/** 判断库存 Use/UnUse 是否已经被服务器接受；只允许首次成功或成功终态重放驱动后续领域提交。 */
inline bool CatIsAcceptedInventoryItemUseResult(const FCatInventoryItemUseResult& Result)
{
	// 接受态判断流程：首次提交看 bCommitted+None；终态重放只信首次提交结果，失败重放不能继续驱动放杆、草药恢复或其他下游效果。
	return (Result.bCommitted && Result.Error == ECatDomainCommandError::None)
		|| (Result.bTerminalReplay && Result.bReplayedTerminalCommitted
			&& Result.ReplayedTerminalError == ECatDomainCommandError::None);
}
