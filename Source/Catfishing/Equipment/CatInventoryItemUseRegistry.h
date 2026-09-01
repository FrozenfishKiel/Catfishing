#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatInventoryItemUseRegistry.generated.h"

class AActor;

/** 当前 World 中所有“库存物品实例已变成场景 Actor”的通用索引；仓库、交易和拖拽只问这里，不问具体玩法服务。 */
UCLASS()
class CATFISHING_API UCatInventoryItemUseRegistry final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端只看各自 Actor 复制状态，不能本地裁决物品实例是否可入库。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 结束时清空弱登记；物品是否回库仍由对应 Equipment 的 UnUse 事务裁决，不在这里补偿。 */
	virtual void Deinitialize() override;

	/** 登记一个已经从 Use 成功链路生成的场景 Actor；同一 ItemInstanceId 只能被同一个存活 Actor 占用。 */
	bool RegisterWorldItemActor(AActor* Actor);

	/** 精确注销一个场景 Actor；只有当前登记值仍是这个 Actor 时才移除，避免旧 Actor 迟到清理新实例。 */
	void UnregisterWorldItemActor(AActor* ExpectedActor);

	/** 按库存实例身份查询当前存活场景 Actor；未知、失效或身份错位都会返回空并清理残留登记。 */
	AActor* FindWorldItemActor(FGuid ItemInstanceId);

	/** 只读判断某个库存实例是否已经由场景 Actor 占用；仓库和交易 gate 用它统一拒绝同实例回流。 */
	bool IsItemInstanceInWorld(FGuid ItemInstanceId);

private:
	/** 压缩失效或身份错位的弱登记；不会扫描 World 重建状态，只维护本登记器自己的事实。 */
	void Compact();

	/** ItemInstanceId 到当前场景 Actor 的弱引用；弱引用避免 Actor 销毁时被登记器强行续命。 */
	TMap<FGuid, TWeakObjectPtr<AActor>> ActorByItemInstanceId;
};
