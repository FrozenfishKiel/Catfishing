#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "CatCampfireActor.generated.h"

class APlayerState;
class USceneComponent;
class UStaticMeshComponent;

/**
 * 营地篝火：入夜自动点亮、整夜都在、想去就去不阻塞流程（营地册 §3.1.5:85、交互册「交互：篝火」）。
 *
 * **它不是流程里的一环。** 坐不坐火边都不影响翻天，也不产生任何数值或奖励；翻天＝全员到祭坛与石像互动，
 * 与本 Actor 无关。所以这里既没有 ready 写口、没有倒计时、也没有任何会阻塞 Run 的等待。
 *
 * 墓碑（2026-09-12）：设计 v1.14 已删「结算夜篝火回看仪式」。工程侧那条 `ACatCampHubActor::RequestCampfirePlayback`
 * 没有随之删除——它现在唯一的职责是结算夜的**全员合影封面候选**（联机社交 §38 共同印记分发的三个来源之一），
 * 那是另一件事，删掉会造成回退。两者的关系在 CampHubActor 的对应注释里写明了。
 *
 * 点火与否只读 Run 公开阶段，不复制第二份「火有没有亮」——它是阶段的函数，不是独立状态。
 * 坐下/起身则是真状态，由服务器持有并复制。
 */
UCLASS()
class CATFISHING_API ACatCampfireActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	/** 建立关卡可配置外观的交互实体与落座锚点；不注册任何 Run 写口，也不创建第二份营地真相。 */
	ACatCampfireActor();

	/** 登记落座名单的复制；火是否点亮由 Run 公开阶段现算，不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器低频复核落座者是否还在范围内、是否还在局里；走远或离局就自动起身，不留幽灵座位。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 销毁时清空落座名单；表现层据复制值收起动作，不需要额外收口协议。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 只有火点着、请求者在交互范围内且没有倒地时才给提示；它不检查任何 Run 就绪或人数条件。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;

	/** 【F】坐下 / 站起来；文案随本人当前是否已落座切换（交互册「显示提示：【F】坐下」）。 */
	virtual FText GetInteractionPrompt_Implementation() const override;

	/** 沿用本实例配置的交互半径；它只是「够得着火堆」，不是任何到场判定。 */
	virtual double GetInteractionRadius_Implementation() const override;

	/** 落座与起身的唯一入口；服务器复核范围与阶段后翻转本人的落座位，不写 Run、不发 GAS、不阻塞任何流程。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 火此刻是否点着；入夜即点亮、整夜都在，白天与失败终局不点（营地册 §3.1.5「到 0 立即结束……不再生火」）。 */
	UFUNCTION(BlueprintPure, Category = "Camp|Campfire")
	bool IsCampfireLit() const;

	/** 当前围着火坐下的玩家；表现层按它播松弛动作与叠罗汉，名单顺序即落座先后。 */
	UFUNCTION(BlueprintPure, Category = "Camp|Campfire")
	TArray<APlayerState*> GetSeatedPlayers() const;

	/** 本机指定玩家是否已经坐下；提示文案与表现层都读它，不各自从名单里再找一遍。 */
	UFUNCTION(BlueprintPure, Category = "Camp|Campfire")
	bool IsPlayerSeated(const APlayerState* PlayerState) const;

protected:
	/** 点火/熄火与落座名单变化时给蓝图一次表现机会；原生层不播动画、不生成特效，也不决定叠罗汉怎么摆。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Camp|Campfire")
	void BP_RefreshCampfirePresentation(bool bLit, const TArray<APlayerState*>& SeatedPlayerStates);

	/** 复制到达后触发表现刷新；落座名单是唯一复制状态，火是否点亮由阶段现算。 */
	UFUNCTION()
	void OnRep_SeatedPlayers();

	/** 本机点火状态或名单变化时调用一次蓝图表现扩展点；它不改变任何权威状态。 */
	void RefreshCampfirePresentation();

private:
	/** 解析请求者当前 Character 并确认它在火堆交互范围内；只在服务器使用，不接受客户端自报位置。 */
	class ACatCharacter* ResolveCharacterNearCampfire(AController* Controller) const;

	/** 火堆布局与落座锚点的项目根；关卡摆放后不由玩家移动。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> CampfireRoot;

	/** 关卡可配置的火堆外观；没有配置网格时交互仍然成立，只是看不见火堆。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CampfireMesh;

	/** 够得着火堆的距离，单位厘米；它只是交互半径，不是任何到场或结算判定。 */
	UPROPERTY(EditAnywhere, Category = "Campfire", meta = (ClampMin = "1", Units = "cm"))
	float InteractionRadiusCentimeters = 300.0f;

	/** 当前坐在火边的玩家；服务器唯一写入，客户端只读来驱动松弛与叠罗汉表现。 */
	UPROPERTY(ReplicatedUsing = OnRep_SeatedPlayers)
	TArray<TObjectPtr<APlayerState>> SeatedPlayers;

	/** 上一次已经通知过表现层的点火状态；只用于去重，不作为点火事实来源。 */
	bool bLastPresentedLit = false;

	/** 上一次通知表现层时的落座名单签名；只用于去重，避免低频 Tick 每 0.25 秒重播一次同样的松弛动作。 */
	uint32 LastPresentedSeatSignature = 0;

	/** 本机是否已经给表现层发过至少一次；首帧必须发一次，否则「一直没亮」和「还没通知过」分不开。 */
	bool bHasPresentedOnce = false;

	/** 按落座顺序算出当前名单的去重签名；它只服务表现去重，不参与任何权威判定。 */
	uint32 MakeSeatSignature() const;

	/** 落座与起身命令的首次终态缓存；网络重试重放同一结果，不会把同一次按键算成坐下又站起。 */
	TMap<FString, bool> SeatTerminalCache;
};
