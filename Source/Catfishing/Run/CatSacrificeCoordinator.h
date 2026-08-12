#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatSacrificeCoordinator.generated.h"

class ACatfishingGameModeBase;

/** 一局服务器献祭协调器；唯一外部命令按可逆预留→Run 预检→Items commit→Run apply 顺序推进。 */
UCLASS()
class CATFISHING_API UCatSacrificeCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；玩家掉线不会销毁本协调器。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时关闭新命令并释放本局协议记录。 */
	virtual void Deinitialize() override;

	/** 祭坛/UI 的唯一外部入口；服务器重建 StableNetId 并推进或重放同 RequestId 协议。 */
	FCatSacrificeResult RequestSacrifice(AController* RequestingController, const FCatSacrificeCommand& Command);

	/** Host teardown 前补齐献祭、关闭 Fishing/Social 并在 Items 关闭前归还 theft escrow；全部领域收口才返回 true。 */
	bool PrepareForRunTeardown();

private:
	/** 一条协议的服务器内存真相；Command 冻结身份/两个 Revision，Result 保存单向阶段。 */
	struct FProtocolRecord
	{
		/** 服务器适配后的原始命令。 */
		FCatSacrificeCommand Command;
		/** 当前可重放协议结果。 */
		FCatSacrificeResult Result;
	};

	/** 从 Controller PlayerState 的继承 UniqueId 读取私有身份；无效返回空。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 组合服务器身份、固定 Sacrifice 命令类别与 RequestId；同一玩家改鱼重放仍只能得到首次协议。 */
	static FString MakeProtocolKey(const FString& StableNetId, const FGuid& RequestId);

	/** 把 Run 拒绝映射为跨领域错误；不会把失败布尔冒充成功。 */
	static ECatDomainCommandError MapRunError(ECatRunCommandError Error);

	/** Items commit 后向当前 GameMode 提交冻结贡献；成功推进 Completed，失败保留 ItemsCommitted。 */
	FCatSacrificeResult ApplyCommittedRecord(FProtocolRecord& Record);

	/** 服务器身份作用域幂等键到单向协议记录；ItemsCommitted 以后保留到 Run apply 或 World 销毁。 */
	TMap<FString, FProtocolRecord> Protocols;

	/** teardown 后永久拒绝新献祭。 */
	bool bCommandsOpen = true;
};
