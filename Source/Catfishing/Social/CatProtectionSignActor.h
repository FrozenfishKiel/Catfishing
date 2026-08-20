#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatProtectionSignActor.generated.h"

class APlayerState;

/**
 * 玩家在钓点放置的防骚扰牌子；持有受保护 PlayerState、固定半径和 Revision。它是恶作剧与偷窃共用的同一道护栏，立牌即完
 * 整免打扰，但不改变救援、交易等其他玩法权限。
 */
UCLASS()
class CATFISHING_API ACatProtectionSignActor : public AActor
{
	GENERATED_BODY()

public:
	/** 开启复制并关闭 Tick；范围判断按 Actor 固定位置执行。 */
	ACatProtectionSignActor();

	/** 注册受保护 PlayerState、范围与 Revision 复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** authority 放牌时配置一次受保护玩家和显式正范围；无效输入保持未配置。 */
	bool ConfigureProtection(APlayerState* InProtectedPlayerState, double InRadiusCentimeters);

	/** 用服务器绑定的目标 PlayerState 与牌子半径裁决保护；身份或位置不匹配即不保护。恶作剧与偷窃两条路径共用这一个结果，救援等其他权限不读它。 */
	bool ProtectsAgainst(const APlayerState* TargetPlayerState, const FVector& InteractionLocation) const;

private:
	/** 受保护玩家的引擎 PlayerState；身份继续由其 UniqueId 拥有，不复制第二份 StableNetId。 */
	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> ProtectedPlayerState;

	/** 牌子的保护半径，单位厘米；恶作剧与偷窃共用这一个半径，由放牌命令显式配置，0 表示未裁。 */
	UPROPERTY(Replicated)
	double RadiusCentimeters = 0.0;

	/** 配置提交版本；客户端表现可据此刷新牌子文案/范围。 */
	UPROPERTY(Replicated)
	int64 Revision = 0;
};
