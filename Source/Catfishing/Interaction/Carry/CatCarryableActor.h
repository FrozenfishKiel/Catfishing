#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatCarryableActor.generated.h"

class ACatCharacter;
class UCatInventoryItemInstance;

/** 可以在物理掉落和角色附着之间切换的世界实物。客户端仅还原服务器移动与附件，不从库存或表现状态推导携带者。 */
UCLASS(Abstract)
class CATFISHING_API ACatCarryableActor : public AActor
{
	GENERATED_BODY()

public:
	/** 主动放开当前嘴叼实物；服务器复核操作者、嘴部引用、物理和落点，同一 RequestId 贯穿库存预留和释放日志，旧调用未传 ID 时现场补一个。 */
	bool DropFromAuthority(AController* Controller, FGuid RequestId = FGuid());
	/** 倒地及宿主退出时放下同一实物；跳过玩家输入资格但仍复用物品落地预检和公共提交，并为这次强制释放生成独立请求号。 */
	void ReleaseMouthCarryFromAuthority(const FVector& DropLocation);
	/** 新一次嘴部占用的版本；角色认领时递增，用来拒绝上一次携带期间发送的丢弃。 */
	void BeginCarryRevisionFromAuthority();
	/** 读取当前携带代次；客户端随目标发回，不能以同一 Actor 引用替代代次校验。 */
	uint32 GetCarryRevision() const { return CarryRevision; }
	/** 复制携带代次，其余附件和物理仍使用引擎原生复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 实物可选关联的库存身份；公共释放用它精确移出角色背包中的同一实例，没有身份的世界物不会为丢弃临时创建库存实例。 */
	virtual UCatInventoryItemInstance* GetCarriedInventoryItem() const;
	/** 物品仅补充自身可释放条件及落地形状预检，可调整候选姿态，不修改归属、物理或附件。 */
	virtual bool PrepareCarryRelease(ACatCharacter* Character, FTransform& Transform, bool bThrow) const;
	/** 公共解绑后刷新物品自身状态、落地姿态与专属效果；不重复处理根物理、库存扣除或嘴部占用。 */
	virtual void OnCarryReleased(ACatCharacter* Character, bool bThrow);
	/** 完成已预检的释放；RequestId 是本次库存预留和诊断日志的同一关联号，主动 Q 缺原槽时拒绝，强制释放允许清掉生命周期残留嘴占用。 */
	bool CommitCarryRelease(ACatCharacter* Character, const FTransform& Transform, bool bThrow, FGuid RequestId);
	/** 携带代次由服务器每次认领递增并复制，跨丢弃再拾取不会重用旧请求目标。 */
	UPROPERTY(Replicated)
	uint32 CarryRevision = 0;
	/** 延后附件通知的应用，确保本批移动通知已处理；派生类不能重新引入先附着后停止物理的路径。 */
	virtual void OnRep_AttachmentReplication() override final;
	/** 本批复制及未解析引用补齐后统一收敛物理和附件，再通知派生类刷新自身表现；不维护第二份携带状态。 */
	virtual void PostRepNotifies() override final;
	/** 共同同步完成后的表现扩展点；派生类可改网格姿态和碰撞，但不得重新裁决根附件或物理模拟。强制刷新用于自身表现资源刚更新时。 */
	virtual void RefreshCarryPresentation(bool bForceRefresh);
};
