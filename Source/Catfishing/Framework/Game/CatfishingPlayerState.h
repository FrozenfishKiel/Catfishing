#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatProfileContracts.h"
#include "GameFramework/PlayerState.h"
#include "CatfishingPlayerState.generated.h"

/**
 * Lake 单个玩家的公开身份面。
 *
 * 它拥有三份、且只有三份局内复制事实：本人当晚的翻天确认、本人主动公开给同局玩家看的鱼图鉴摘要，
 * 以及服务器持有的跨局装备解锁清单。StableNetId 继续复用 APlayerState::UniqueId，这里不建第二份身份。
 *
 * 它不拥有：Profile 存档（在 UCatProfileSubsystem，含相册、Journal 与完整解锁）、身体与生存数值
 * （在 ACatCharacter 及其组件）、任何领域协议状态。三个写口都叫 FromAuthority，只接受服务器调用；
 * 装备解锁清单虽然由 owning client 上报，但是"服务器校验后自己持有的一份事实"，不是客户端存档的直读，
 * 装配 gate 只认这一份。
 */
UCLASS()
class CATFISHING_API ACatfishingPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	/** 注册个人翻天确认、公开鱼图鉴摘要与装备解锁清单复制；StableNetId 继续复用 APlayerState::UniqueId。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 仅由服务器 GameMode 写入本人的普通夜晚 ready 事实，客户端 RPC 参数不能直接赋值。 */
	void SetNextDayReadyFromAuthority(bool bNewReady);
	/** 返回本人的最终 ready 事实；它不代表全员资格或 StateTree 已经转移。 */
	bool IsReadyForNextDay() const;
	/** 仅服务器接收 owning client 提交的公开鱼图鉴摘要；严格校验后整体复制给局内其他玩家查看。
	 *  合法记录至少要在钓起轨或抄获轨之一上有内容，两轨皆空的行会让整份摘要被拒。 */
	bool SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records);
	/** 提供局内玩家可见的鱼图鉴摘要；相册、Journal 和解锁被排除，避免 PlayerState 成为第二份 Profile。 */
	const TArray<FCatFishCollectionRecord>& GetPublicFishCollection() const;
	/**
	 * 仅服务器接收 owning client 上报的跨局装备解锁清单；校验条数、非空和唯一后整体替换并复制。
	 * 它和公开鱼图鉴是同一种模式（客户端从 durable Profile 上报 → 服务器持有并复制）。依据是飞书联机社交册的熟人邀请制/熟人自治基准，
	 * 不为生人局做防作弊设计；详见 Docs/Development/工程自补决策记录.md D-04。
	 */
	bool SetAuthorizedEquipmentUnlocksFromAuthority(const TArray<FName>& UnlockIds);
	/** 提供服务器当前持有的本人装备解锁清单；只供装配 gate 与只读展示，不是 Profile 的第二份存档。 */
	const TArray<FName>& GetAuthorizedEquipmentUnlocks() const;
	/**
	 * 查询服务器是否持有指定装备解锁的可信证明：None 视为 starter 直接放行，其余只认服务器自己持有的解锁清单。
	 * 这份清单是 owning client 从 durable Profile 上报、服务器校验后持有并复制的（见 SetAuthorizedEquipmentUnlocksFromAuthority），
	 * 不是客户端本地 SaveGame 的直读——后者会让"客户端想装什么就装什么"，是权限提升；前者至少让服务器持有一份自己校验过的事实，
	 * 并且只有 owning client 能上报自己的清单。任何时候都不要把这里改成去读客户端 SaveGame 或信任 Profile 选择本身。
	 */
	bool HasServerAuthorizedEquipmentUnlock(FName UnlockId) const;
protected:
	/** 玩家状态进入 World 后记录继承 UniqueId 是否有效；原始值是否输出由 StableNetIdExposure 策略控制。 */
	virtual void BeginPlay() override;
	/** 客户端接收个人 ready 变化后记录诊断；不从 RepNotify 发送 Run 事件。 */
	UFUNCTION()
	void OnRep_ReadyForNextDay();
private:
	/** 当前普通夜晚的个人翻天确认；仅 authority GameMode 写入/在新夜资格冻结前清零，客户 RepNotify 只更新本人表现。 */
	UPROPERTY(ReplicatedUsing = OnRep_ReadyForNextDay)
	bool bReadyForNextDay = false;

	/** authority 在严格校验 owning client 摘要后整体替换的公开鱼图鉴；局内其他玩家可读，不含相册、Journal、解锁、装备或原始 StableNetId。 */
	UPROPERTY(Replicated)
	TArray<FCatFishCollectionRecord> PublicFishCollection;

	/** authority 在校验 owning client 上报后整体替换的跨局装备解锁清单；只供 Equipment 装配 gate 读取，不含 Profile 其他内容。 */
	UPROPERTY(Replicated)
	TArray<FName> AuthorizedEquipmentUnlockIds;
};
