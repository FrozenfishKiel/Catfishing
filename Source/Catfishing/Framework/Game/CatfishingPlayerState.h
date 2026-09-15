#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "GameFramework/PlayerState.h"
#include "CatfishingPlayerState.generated.h"

/** Lake 玩家身份与个人局状态宿主；复用 APlayerState::UniqueId，只增加公开鱼图鉴摘要和本局装备解锁投影。 */
UCLASS()
class CATFISHING_API ACatfishingPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	/** 注册公开鱼图鉴摘要和装备解锁投影复制；StableNetId 继续复用 APlayerState::UniqueId。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 仅服务器接收 owning client 提交的公开鱼图鉴摘要；严格校验后整体复制给局内其他玩家查看。 */
	bool SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records);
	/** 提供局内玩家可见的鱼图鉴摘要；相册、Journal 和解锁被排除，避免 PlayerState 成为第二份 Profile。 */
	const TArray<FCatFishCollectionRecord>& GetPublicFishCollection() const;
	/** 服务器接收 owning client 从 durable Profile 汇总出的装备解锁摘要；格式非法时保留失效授权并 fail-closed。 */
	bool SetAuthorizedEquipmentUnlocksFromAuthority(const TArray<FName>& UnlockIds);
	/** 服务器在某份 Unlock Grant 已经 durable ACK 后追加本局授权；它不接受客户端直接指定 Grant 内容。 */
	bool AuthorizeEquipmentUnlockFromProfileGrant(const FCatProfileGrant& Grant);
	/** 查询服务器是否持有指定装备解锁的可信证明；None 视为 starter，非空必须来自本局授权快照。 */
	bool HasServerAuthorizedEquipmentUnlock(FName UnlockId) const;

	/**
	 * 仅 authority 的 RoomOwnerService 写入本人是否为当前房主；它是移交结果的复制载体，不是移交裁决本身。
	 * 房主只活在**房间管理层**（踢人、开局、翻天兜底，2026-09-07 决策点⑩）；机制层没有房主——
	 * 钓鱼、打窝、献祭、拿鱼没有一处读它。UI 读它只决定「踢人」入口露不露面，真正的资格仍由服务器复核。
	 */
	void SetRoomOwnerFromAuthority(bool bNewRoomOwner);

	/** 本人当前是不是房主；客户端读的是复制值，空缺期间全场都是 false。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Room")
	bool IsRoomOwner() const { return bRoomOwner; }
protected:
	/** 玩家状态进入 World 后记录继承 UniqueId 是否有效；原始值是否输出由 StableNetIdExposure 策略控制。 */
	virtual void BeginPlay() override;
private:
	/** authority 在严格校验 owning client 摘要后整体替换的公开鱼图鉴；局内其他玩家可读，不含相册、Journal、解锁、装备或原始 StableNetId。 */
	UPROPERTY(Replicated)
	TArray<FCatFishCollectionRecord> PublicFishCollection;

	/** 服务器当前认可并复制的装备解锁 ID 摘要；Profile Grant ACK 或 owning client durable Profile 摘要写入，Equipment 装配只读它。 */
	UPROPERTY(Replicated)
	TArray<FName> AuthorizedEquipmentUnlockIds;

	/** 本人是否为当前房主；authority 的 RoomOwnerService 唯一写入，全场同时最多一个 true。 */
	UPROPERTY(Replicated)
	bool bRoomOwner = false;
};
