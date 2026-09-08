#pragma once

#include "CoreMinimal.h"
#include "Equipment/Inventory/CatInventoryTransferTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "CatInventoryTransferService.generated.h"

/** 服务器原生转移协调器；现有玩法 RPC 负责身份、距离和玩法权限，不向客户端开放任意端点写入。 */
UCLASS()
class CATFISHING_API UCatInventoryTransferService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	FCatInventoryTransferResult TransferFromAuthority(const FCatInventoryTransferRequest& Request);
	virtual void Deinitialize() override;

private:
	struct FRequestKey
	{
		FObjectKey Initiator;
		FGuid RequestId;
		bool operator==(const FRequestKey& Other) const
		{
			return Initiator == Other.Initiator && RequestId == Other.RequestId;
		}
		friend uint32 GetTypeHash(const FRequestKey& Key)
		{
			return HashCombine(GetTypeHash(Key.Initiator), GetTypeHash(Key.RequestId));
		}
	};
	struct FTerminalRecord
	{
		/** 保存请求不变量作精确签名，端点弱引用比较对象索引与序列，不依赖变化后的库存。 */
		FCatInventoryTransferRequest Request;
		FCatInventoryTransferResult Result;
	};
	TMap<FRequestKey, FTerminalRecord> TerminalRecords;
	bool bClosing = false;
};
