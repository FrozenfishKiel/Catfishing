#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "GameFramework/PlayerState.h"
#include "CatfishingPlayerState.generated.h"

/** Lake 玩家身份与个人局状态宿主；复用 APlayerState::UniqueId，只增加公开鱼图鉴摘要。 */
UCLASS()
class CATFISHING_API ACatfishingPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	/** 注册公开鱼图鉴摘要复制；StableNetId 继续复用 APlayerState::UniqueId。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 仅服务器接收 owning client 提交的公开鱼图鉴摘要；严格校验后整体复制给局内其他玩家查看。 */
	bool SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records);
	/** 提供局内玩家可见的鱼图鉴摘要；相册、Journal 和解锁被排除，避免 PlayerState 成为第二份 Profile。 */
	const TArray<FCatFishCollectionRecord>& GetPublicFishCollection() const;
protected:
	/** 玩家状态进入 World 后记录继承 UniqueId 是否有效；原始值是否输出由 StableNetIdExposure 策略控制。 */
	virtual void BeginPlay() override;
private:
	/** authority 在严格校验 owning client 摘要后整体替换的公开鱼图鉴；局内其他玩家可读，不含相册、Journal、解锁、装备或原始 StableNetId。 */
	UPROPERTY(Replicated)
	TArray<FCatFishCollectionRecord> PublicFishCollection;

};
