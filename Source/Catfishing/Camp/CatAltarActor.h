#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Framework/Core/CatRunContracts.h"
#include "Inventory/CatInventoryComponent.h"
#include "CatAltarActor.generated.h"

class ACatFishPickupActor;
class ACatFishGuardActor;
class ACatCampHubActor;
class UCatAltarWorldInfoComponent;
class UStaticMeshComponent;

/** 营地献祭交互点；只负责现场发起、预览和冻结现场供品，全队确认、结算写口与阶段切换仍由 GameMode 裁决。 */
UCLASS()
class CATFISHING_API ACatAltarActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()
public:
	/** 建立关卡可配置外观的交互实体和信息锚点，并启用服务器低频地面供品预览。 */
	ACatAltarActor();
	/** 登记营地关联和地面预览的复制；全队确认状态由 GameState 的 RunPublicState 单独同步。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 服务器定期更新地面预览；确认名单不再由祭坛 Tick 轮询，避免和 GameMode 的权威快照重复。 */
	virtual void Tick(float DeltaSeconds) override;
	/** 祭坛销毁时统一收口其确认与过渡；提交前放弃供品，提交后保留结算并解除锁定。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 仅允许普通夜晚、非倒地且处于交互距离内的玩家发起确认；远程确认资格由 GameMode 处理。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	/** 祭坛的玩家可见提示；全队确认进度由顶部窗口读取 RunPublicState，本提示只保留现场发起动作。 */
	virtual FText GetInteractionPrompt_Implementation() const override;
	/** 沿用项目通用交互距离，不把已删除的到场范围当作远程按键权限。 */
	virtual double GetInteractionRadius_Implementation() const override;
	/** 客户端只转发现场发起意图；服务器让 GameMode 固定本轮 Active 名单并开启远程确认。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 发起确认与冻结供品前验证当前日参数及重量档配置；只读检查，不冻结鱼或消费实物。 */
	bool CanPrepareOffering(FText& OutError) const;
	/** 供品集合固定在过渡开始时；仅服务器调用，输出来自散鱼与地面鱼护内鱼的服务器事实，而非客户端数量。 */
	bool FreezeOffering(AController* Controller, FGuid RequestId, FCatOfferingSettlementCommand& OutCommand, FText& OutError);
	/** 黑屏提交前复核同一批供品；不消费、不写结算，也不再重复裁决全队确认。 */
	bool ValidateFrozenOffering(AController* Controller, FGuid RequestId, FText& OutError);
	/** 同步提交段内先预留同一批供品，再执行结算回调；回调接受才真正清空散鱼和鱼护内鱼。 */
	bool ConsumeFrozenOffering(AController* Controller, FGuid RequestId, TFunction<bool()> CommitSettlement = {});
	/** 清除本轮冻结供品引用；确认和结算结果归 GameMode 的公开快照，不在祭坛保存第二份状态。 */
	void ResetOffering();

	/** 读取服务器地面预览；未就绪或配置无效时返回 false 并清零输出，调用方只有在 true 时才能把零解释为空供品。 */
	bool TryGetGroundOfferingPoints(int32& OutPoints) const;
	/** 信息提供者解析此祭坛的显式营地关系；有效引用直接返回，失效时返回空并让储备显示不可用，不进行全图猜测。 */
	ACatCampHubActor* GetCampHub() const;
	/** 所属营地的唯一关联；设计者按实例填写，客户端通过复制等待引用，不另配一份鱼缸。 */
	UPROPERTY(EditInstanceOnly, ReplicatedUsing=OnRep_InfoChanged, Category="Altar")
	TObjectPtr<ACatCampHubActor> CampHub;

	/** 摆鱼范围，单位厘米；设计者按祭坛实例配置，服务器收范围内散鱼和地面鱼护内的鱼。 */
	UPROPERTY(EditAnywhere, Category="Altar", meta=(ClampMin="1", Units="cm"))
	float OfferingRadiusCentimeters = 200.0f;
	/** 淡出持续秒数；GameMode 在开始时冻结配置，完全遮黑后结算。 */
	UPROPERTY(EditAnywhere, Category="Altar|Transition", meta=(ClampMin="0.01", Units="s"))
	float FadeOutSeconds = 0.4f;
	/** 结算标题停留秒数；不计入新一天的可玩时间。 */
	UPROPERTY(EditAnywhere, Category="Altar|Transition", meta=(ClampMin="0.01", Units="s"))
	float HoldSeconds = 1.2f;
	/** 恢复场景持续秒数；结束后才释放操作门并启动白天计时。 */
	UPROPERTY(EditAnywhere, Category="Altar|Transition", meta=(ClampMin="0.01", Units="s"))
	float FadeInSeconds = 0.4f;

private:
	/** 冻结与预览共用的服务器现场供品筛选；输出散鱼引用、鱼护槽快照、分类数量和点数，不锁鱼或消费；false 时不得提交。 */
	bool CollectOffering(TArray<TWeakObjectPtr<ACatFishPickupActor>>& OutFish, TMap<TWeakObjectPtr<ACatFishGuardActor>, TArray<FCatInventoryEntry>>& OutGuards, FCatOfferingSettlementCommand& OutCommand, int32& OutPoints, FText& OutError) const;
	/** 服务器低频发布实际现场点数；只有值或就绪状态变化时复制并通知本地视图。 */
	void RefreshGroundOfferingPreview();
	/** 展示字段复制到达时通知只读组件；不重新计算客户端供品或人数。 */
	UFUNCTION()
	void OnRep_InfoChanged();
	/** 祭坛自己的只读数据适配器；构造时创建并挂到雕像，字段变化回调通知它刷新，控制器读取其内容，蓝图可调整展示配置。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Altar", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCatAltarWorldInfoComponent> WorldInfo;
	/** 最近一次服务器现场供品预览是否有效；RefreshGroundOfferingPreview 写入并复制，getter 用它决定能否读取点数，默认 false 表示暂不可用。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	bool bGroundOfferingReady = false;
	/** 当前散鱼和地面鱼护内候选供品的总点数；服务器预览入口写入并复制，getter 仅在就绪时公开，不代表已提交或缸内储备，无效时保存零。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	int32 GroundOfferingPoints = 0;
	/** 可由关卡设置雕像网格的根组件；负责通用交互射线命中，不生成替代美术。 */
	UPROPERTY(VisibleAnywhere, Category="Altar")
	TObjectPtr<UStaticMeshComponent> StatueMesh;
	/** 同一批供品的关联标识；由冻结入口写入，消费与清理必须匹配它。 */
	FGuid OfferingRequestId;
	/** 过渡开始时范围内散鱼的弱引用；不拥有鱼生命周期，也不复制或建立新库存。 */
	TArray<TWeakObjectPtr<ACatFishPickupActor>> FrozenFish;
	/** 正式过场开始时各地面鱼护的原槽快照；记录护内鱼实例和数量用于复核与整批移除，不构成另一份可操作库存。 */
	TMap<TWeakObjectPtr<ACatFishGuardActor>, TArray<FCatInventoryEntry>> FrozenGuards;
	/** 本祭坛是否正在同步提交冻结批次；阻止结算回调重入同一批或 ResetOffering 清掉正在配对完成的预留。 */
	bool bOfferingCommitInProgress = false;
};
