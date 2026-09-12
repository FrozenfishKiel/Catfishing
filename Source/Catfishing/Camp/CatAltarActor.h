#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatAltarActor.generated.h"

class ACatFishPickupActor;
class ACatCampHubActor;
class UCatAltarWorldInfoComponent;
class UStaticMeshComponent;

/** 营地献祭交互点；只拥有本夜到场事实、可打断倒计时和冻结供品，GAS 与阶段仍由 GameMode 裁决。 */
UCLASS()
class CATFISHING_API ACatAltarActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()
public:
	/** 建立关卡可配置外观的交互实体和信息锚点，并启用服务器低频到场复核与地面预览。 */
	ACatAltarActor();
	/** 登记应到人数、倒计时截止、拒绝原因、营地关联和地面预览的复制；客户端据此展示，不保存另一套参与者名单。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 服务器定期更新地面预览；无活动过渡时复核到场名单，倒计时里有合格的人走出到场圈就中断，走完才请求翻天。 */
	virtual void Tick(float DeltaSeconds) override;
	/** 祭坛销毁时统一收口其过渡与倒计时；提交前放弃供品，提交后保留结算并解除锁定，清除 GameMode 的失效引用。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 仅允许普通夜晚、非倒地且处于交互距离内的玩家确认。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	/** 祭坛的玩家可见提示；现有 E 键 UI 读取固定动作名、倒计时剩余秒数和拒绝原因，不自行维护人数或倒计时。 */
	virtual FText GetInteractionPrompt_Implementation() const override;
	/** 沿用项目通用交互距离，不把八米到场范围当作远程按键权限。 */
	virtual double GetInteractionRadius_Implementation() const override;
	/** 客户端只转发意图；全员到场后由任一人发起唯一倒计时，倒计时走完才请求 Run 过渡，重复按不重开倒计时。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	/** 供品集合固定在过渡开始时；仅服务器调用，先复核全员仍在到场圈内，输出来自落地鱼事实而非客户端数量。 */
	bool FreezeOffering(AController* Controller, FGuid RequestId, FCatOfferingSettlementCommand& OutCommand, FText& OutError);
	/** 黑屏提交前复核同一批鱼；不消费、不修改 GAS，失败时返回可展示原因。 */
	bool ValidateFrozenOffering(AController* Controller, FGuid RequestId, FText& OutError);
	/** GAS 接受后消费已复核的同一批供品；只由 GameMode 的同步提交段调用。 */
	bool ConsumeFrozenOffering(AController* Controller, FGuid RequestId);
	/** 清除本轮倒计时及供品引用；失败说明保留给交互提示，不建立重试状态机。 */
	void ResetOffering(const FText& Error = FText::GetEmpty());

	/** 读取服务器地面预览；未就绪或配置无效时返回 false 并清零输出，调用方只有在 true 时才能把零解释为空供品。 */
	bool TryGetGroundOfferingPoints(int32& OutPoints) const;
	/** 读取本轮献祭倒计时；未在倒计时或依赖缺失时返回 false 并清零输出，调用方只有在 true 时才能把零解释为即将结算。
	 *  剩余秒数由复制的服务器截止时间现算：服务器读自己的世界时钟，客户端读 GameState 的服务器时钟，不另存一份倒计时。 */
	bool TryGetOfferingCountdown(double& OutRemainingSeconds, double& OutTotalSeconds) const;
	/** 信息提供者读取应到场的玩家数；直接返回本机计数，分母按 Active 且非倒地规则计算，未到场者也包含在内。 */
	int32 GetEligibleCount() const { return EligibleCount; }
	/** 读取最近一次拒绝原因；显示组件不得将它清除或当作成功结果。 */
	const FText& GetOfferingError() const { return LastError; }
	/** 信息提供者解析此祭坛的显式营地关系；有效引用直接返回，失效时返回空并让储备显示不可用，不进行全图猜测。 */
	ACatCampHubActor* GetCampHub() const;
	/** 所属营地的唯一关联；设计者按实例填写，客户端通过复制等待引用，不另配一份鱼缸。 */
	UPROPERTY(EditInstanceOnly, ReplicatedUsing=OnRep_InfoChanged, Category="Altar")
	TObjectPtr<ACatCampHubActor> CampHub;

	/** 摆鱼范围，单位厘米；设计者按祭坛实例配置，服务器只收范围内地面鱼。 */
	UPROPERTY(EditAnywhere, Category="Altar", meta=(ClampMin="1", Units="cm"))
	float OfferingRadiusCentimeters = 200.0f;
	/** 参与者必须到场的距离，单位厘米；倒计时里有合格的人走出此范围即中断。 */
	UPROPERTY(EditAnywhere, Category="Altar", meta=(ClampMin="1", Units="cm"))
	float AttendanceRadiusCentimeters = 800.0f;
	/** 发起到结算之间的可打断倒计时秒数；设计只写「短倒计时」没给数，这里是占位，非有限或非正值一律拒绝发起。 */
	UPROPERTY(EditAnywhere, Category="Altar|Transition", meta=(ClampMin="0.1", Units="s"))
	float OfferingCountdownSeconds = 3.0f;
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
	/** 冻结与预览共用的服务器地面供品筛选；输出鱼引用、分类数量和点数，不锁鱼或消费；false 时输出可能仅含部分结果，不得提交。 */
	bool CollectOffering(TArray<TWeakObjectPtr<ACatFishPickupActor>>& OutFish, FCatOfferingSettlementCommand& OutCommand, int32& OutPoints, FText& OutError) const;
	/** 服务器低频发布实际地面点数；只有值或就绪状态变化时复制并通知本地视图。 */
	void RefreshGroundOfferingPreview();
	/** 展示字段复制到达时通知只读组件；不重新计算客户端供品或人数。 */
	UFUNCTION()
	void OnRep_InfoChanged();
	/** 祭坛自己的只读数据适配器；构造时创建并挂到雕像，字段变化回调通知它刷新，控制器读取其内容，蓝图可调整展示配置。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Altar", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCatAltarWorldInfoComponent> WorldInfo;
	/** 最近一次服务器地面预览是否有效；RefreshGroundOfferingPreview 写入并复制，getter 用它决定能否读取点数，默认 false 表示暂不可用。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	bool bGroundOfferingReady = false;
	/** 当前地面候选供品的总点数；服务器预览入口写入并复制，getter 仅在就绪时公开，不代表已提交或缸内储备，无效时保存零。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	int32 GroundOfferingPoints = 0;
	/** 重新收集本房间非倒地 Active 玩家与其中已进入到场圈的人；返回分母非空且全员都在圈内。 */
	bool RefreshAttendance();
	/** 服务器结束本轮倒计时并保留说明；只清倒计时字段，不动供品引用，也不改 Run 阶段。 */
	void CancelCountdown(const FText& Error);
	/** 可由关卡设置雕像网格的根组件；负责通用交互射线命中，不生成替代美术。 */
	UPROPERTY(VisibleAnywhere, Category="Altar")
	TObjectPtr<UStaticMeshComponent> StatueMesh;
	/** 当前仍站在到场圈内的合格控制器；只有服务器写入，每次名单复核整体重算，不作为「同意」记录保留。 */
	TSet<TWeakObjectPtr<AController>> PresentPlayers;
	/** 当前到场事实属于哪一夜；服务器用天数清除上一轮倒计时，客户端不读取。 */
	int32 OfferingDay = 0;
	/** 发起倒计时的控制器；倒计时走完时若他仍在圈内就由他提交，否则改由任一在场者提交，到场本身就是同意。 */
	TWeakObjectPtr<AController> CountdownInitiator;
	/** 发起倒计时那次交互的请求标识；倒计时走完后原样交给 GameMode，不另生成一个。 */
	FGuid CountdownRequestId;
	/** 本轮倒计时是否仍在走；服务器发起、中断和提交时写入并复制，客户端只读展示，不自行推进或撤销。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	bool bCountdownActive = false;
	/** 本轮倒计时的服务器截止时间，单位秒；bCountdownActive=false 时为 0，客户端据此本地推算剩余而非等待每帧复制。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	double CountdownDeadlineServerTimeSeconds = 0.0;
	/** 本轮倒计时的总秒数；发起时从配置冻结并复制，供进度条算比例，中断或结束后清零。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	double CountdownTotalSeconds = 0.0;
	/** 当前房间应到场的玩家数；服务器复核时写入并复制，提示和信息牌读取；倒地与已退出者排除，未到场者仍计入分母。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	int32 EligibleCount = 0;
	/** 最近一次被拒绝或中断的原因；服务器重置入口写入并复制，提示与信息牌读取，新的一次成功发起会清除。 */
	UPROPERTY(ReplicatedUsing=OnRep_InfoChanged)
	FText LastError;
	/** 同一批供品的关联标识；由冻结入口写入，消费与清理必须匹配它。 */
	FGuid OfferingRequestId;
	/** 过渡开始时范围内落地鱼的弱引用；不拥有鱼生命周期，也不复制或建立新库存。 */
	TArray<TWeakObjectPtr<ACatFishPickupActor>> FrozenFish;
};
