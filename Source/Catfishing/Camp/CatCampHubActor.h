#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Items/CatItemTypes.h"
#include "CatCampHubActor.generated.h"

class ACatCharacter;
class ACatFishTankActor;
class ACatCampInventoryActor;
class APawn;
class USceneComponent;

/** 篝火公共回看请求；它只启动可跳过表现，不参与普通夜晚 ready 或 StateTree 转移。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCampfirePlaybackRequested, FGuid);

/** 玩法世界唯一固定营地宿主；同时承载玩家出生点语义、休息、救援落点、共享鱼缸引用和可选回看，不支持建造/装饰/搬迁。 */
UCLASS()
class CATFISHING_API ACatCampHubActor : public APlayerStart
{
	GENERATED_BODY()

public:
	/** 建立关卡摆放营地所需的出生点父类、固定根节点、救援落点和复制属性；布局仍由关卡资产负责，运行时不会生成第二套营地真相。 */
	ACatCampHubActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 按当前营地附近地面为 0 到 MaxCampSpawnPlayers-1 的玩家序号解析合法出生位置；超过容量会拒绝而不是循环复用，成功时返回的是 Pawn 根胶囊中心 Transform，失败时调用方必须保持无 Pawn。 */
	bool TryResolvePlayerEntryTransform(int32 PreferredEntryIndex, const APawn* PawnToFit, FTransform& OutTransform) const;

	/** 本人位于营地范围时请求快速休息；Character ConditionComponent 拥有最终身体写入。 */
	FCatDomainCommandResult RequestRest(AController* RequestingController, FGuid RequestId);

	/** 伙伴把倒地目标送到固定 RescuePoint；Teleport 成功后才提交 CarriedToCamp 事实。 */
	FCatDomainCommandResult RescueToCamp(AController* HelpingController, ACatCharacter* TargetCharacter, FGuid RequestId);

	/** 读取固定共享鱼缸当前快照；UI 只用它投影共享容器，不获得鱼缸写权限。 */
	bool TryGetSharedFishTankSnapshot(FCatContainerSnapshot& OutSnapshot) const;

	/** 判断传入鱼缸是否就是本营地显式关联的共享鱼缸；交互组件只用它解析 Camp 上下文，不取得写权限。 */
	bool IsSharedFishTank(const ACatFishTankActor* Candidate) const;

	/** 商店发货询问本营地能否提供公共仓库；PlayerController 全图扫描命中后调用它，空值表示本营地当前不能接收购买物。 */
	ACatCampInventoryActor* ResolvePublicInventoryForShopOrder() const;

	/** 幂等请求可跳过的篝火回看；结算封面先为全体在场玩家批量建齐 Planned 事实，成功才广播一次表现意图，且不写 Run ready。 */
	FCatDomainCommandResult RequestCampfirePlayback(AController* RequestingController, FGuid RequestId);

	/** 服务器完成营地、结算夜和全员 CapturePlan 校验后，把同一 RequestId 可靠送到该营地的相关客户端；每端只复用本地表现委托，不保存播放状态。 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastCampfirePlaybackRequested(FGuid RequestId);

	/** 只读判断 Controller 当前 Character 是否位于固定营地交互范围；供修竿等其他领域适配，不产生回看或休息副作用。 */
	bool IsControllerInCamp(AController* Controller) const;

	/** 篝火表现订阅入口；表现结束/跳过无需回写 Run。 */
	FCatCampfirePlaybackRequested OnCampfirePlaybackRequested;

private:
	/** 验证 Controller 当前 Character 是否处于固定营地交互范围；不接受客户端位置参数。 */
	ACatCharacter* ResolveCharacterInCamp(AController* Controller) const;

	/** 固定营地布局和交互子组件的项目根；它挂在 PlayerStart 胶囊根下，关卡摆放后不会被玩家移动。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> CampRoot;

	/** 倒地搬运的固定落点；没有动态建造或玩家自定义坐标。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> RescuePoint;

	/** 关卡显式关联的共享鱼缸；空引用时外部容器上下文 fail-closed，不在命令中临时 Spawn。 */
	UPROPERTY(EditInstanceOnly, Category = "Camp")
	TObjectPtr<ACatFishTankActor> SharedFishTank;

	/** 关卡显式关联的营地公共仓库；它接收商店购买物，并作为玩家取用公共装备的唯一营地入口，不由商店摊位配置。 */
	UPROPERTY(EditInstanceOnly, Category = "Camp")
	TObjectPtr<ACatCampInventoryActor> PublicInventory;

	/** 救援者身份、命令类别与 RequestId 到首次成功终态；网络重试先重放，避免重复 Teleport 同一倒地目标。 */
	TMap<FString, FCatDomainCommandResult> RescueTerminalCache;

	/** 玩家身份+RequestId 到篝火回看首次终态；成功重试只重放结果，不再次广播表现或创建成像计划。 */
	TMap<FString, FCatDomainCommandResult> CampfirePlaybackTerminalCache;
};
