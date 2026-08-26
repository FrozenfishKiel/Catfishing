#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "CatProfileSubsystem.generated.h"

class UCatProfileSaveGame;

/** 外部本地成像桥监听的新计划广播；监听方只在真实图片 durable 后回报成功。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCapturePlanReceived, const FCatCapturePlan&);

/** 本地图鉴 durable 内容已经变化的只读通知；订阅者收到信号后重新读取公开快照，不读取 Journal 或写入档案。 */
DECLARE_MULTICAST_DELEGATE(FCatFishCollectionChanged);

/** 每个 LocalPlayer 的永久档案深模块；它拥有 SaveGame Journal 和内容合并，不接触服务器实物容器。 */
UCLASS()
class CATFISHING_API UCatProfileSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** 解析本地槽位、加载或创建 SaveGame，并重放所有 durable Pending Grant；未配置时保持不可写。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 释放内存 SaveGame 与瞬时 CapturePlan 广播；不在销毁阶段把未落盘状态冒充成功。 */
	virtual void Deinitialize() override;

	/** 应用一份不可变 Grant：先写 Pending Journal，再合并内容并写 Complete，只有第二次落盘成功才允许 ACK。 */
	FCatProfileApplyResult ApplyGrant(const FCatProfileGrant& Grant);

	/** 接收服务器 CapturePlan；只有计划完整且外部桥已接入时才广播并返回 true，否则返回 false 让 Controller 明确回报失败终态。 */
	bool ReceiveCapturePlan(const FCatCapturePlan& Plan);

	/** 本地选择一个已解锁或正式 starter 的功能装备；验证槽位/定义后 durable 保存，不写 Character 运行态。 */
	FCatDomainCommandResult SetEquipmentSelection(FGuid RequestId, FName SlotId, FName EquipmentDefinitionId);

	/** 读取某个稳定槽位的跨局选择；不存在时返回 false 且输出 None。 */
	bool TryGetEquipmentSelection(FName SlotId, FName& OutEquipmentDefinitionId) const;

	/** 复制本地鱼图鉴公开快照供“互看图鉴”；不包含相册、隐藏印记、Journal、解锁或装备选择。 */
	bool GetFishCollectionSnapshot(TArray<FCatFishCollectionRecord>& OutRecords) const;

	/** 复制本地 durable 装备解锁摘要；只给 owning Controller 上报本 PlayerState 的运行期授权投影。 */
	bool GetEquipmentUnlockSnapshot(TArray<FName>& OutUnlockIds) const;

	/** 只在本地相册切换本人隐藏状态并 durable 保存；不产生服务器全局撤下或修改其他玩家副本。 */
	FCatDomainCommandResult SetImprintHidden(FGuid RequestId, FGuid ImprintId, bool bHidden);

	/** 外部本地成像桥订阅入口；订阅者负责自己的图片格式、原子文件写与容量策略。 */
	FCatCapturePlanReceived OnCapturePlanReceived;

	/** 鱼图鉴公开快照变化的订阅入口；只在 FishRecorded/FishSilhouette 完成第二次 durable 保存后触发。 */
	FCatFishCollectionChanged OnFishCollectionChanged;

private:
	/** 校验 Grant 内容是否足以进入 Journal；拒绝发生在任何 SaveGame 写入之前。 */
	static ECatDomainCommandError ValidateGrant(const FCatProfileGrant& Grant);

	/** 把一份已落 Pending 的 Grant 幂等合并到内存档案；不自行保存或发 ACK。 */
	bool MergeGrantIntoProfile(const FCatProfileGrant& Grant);

	/** 完成一个已存在的 Pending Journal：合并、标 Complete、保存；失败时重新加载磁盘 Pending 事实。 */
	FCatProfileApplyResult CompletePendingGrant(FGuid GrantId);

	/** 把当前 SaveGame 同步写入精确槽位；失败只返回 false，不修改 Journal 阶段。 */
	bool SaveCurrentProfile() const;

	/** 从磁盘重新加载当前槽位，恢复最后一次 durable 事实；加载失败时清空可写状态。 */
	bool ReloadDurableProfile();

	/** 当前 LocalPlayer 独占的内存 SaveGame；只在持久化 gate 与槽位解析成功后有效。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatProfileSaveGame> CurrentProfile;

	/** 由基础名和 ControllerId 组成的实际槽位；不从服务器或 StableNetId 派生。 */
	FString ResolvedSlotName;

	/** SaveGame API 的本地用户索引；直接取 LocalPlayer ControllerId，负值时不启用持久化。 */
	int32 ResolvedUserIndex = INDEX_NONE;

	/** 初始化完成且当前内存对象与 durable 槽位一致时为 true；任一次恢复失败都会关闭后续 ACK。 */
	bool bPersistenceReady = false;
};
