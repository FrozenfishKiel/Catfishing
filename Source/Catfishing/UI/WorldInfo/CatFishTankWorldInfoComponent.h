#pragma once

#include "CoreMinimal.h"
#include "UI/WorldInfo/CatWorldInfoComponent.h"
#include "CatFishTankWorldInfoComponent.generated.h"

class ACatfishingGameState;
class AGameStateBase;
class UCatFishOnlyInventoryComponent;

/** 缸内实物鱼的公开只读投影；四个字段共同复制，避免客户端把新就绪标记和旧计数拼成有效摘要。 */
USTRUCT()
struct FCatFishTankOfferingSummary
{
	GENERATED_BODY()

	/** 整份摘要是否完成服务器核对；服务器汇总后写入，读者仅在 true 时消费数值，默认 false 表示尚未同步。 */
	UPROPERTY()
	bool bReady = false;

	/** 缸内全部实物鱼按真实千克重量分类后的储备总点数；服务器计算，客户端只读，INDEX_NONE 表示未知而非零。 */
	UPROPERTY()
	int32 Points = INDEX_NONE;

	/** 正式库存中的实物鱼条数；服务器从实例快照计数，展示与祭坛只读，INDEX_NONE 表示未取得库存。 */
	UPROPERTY()
	int32 Count = INDEX_NONE;

	/** 正式库存实际拥有的槽位数；服务器读取已建立的槽位数量，展示和祭坛仅读取，INDEX_NONE 表示未知，零可以是合法空容量。 */
	UPROPERTY()
	int32 Capacity = INDEX_NONE;
};

/** 共享鱼缸的世界信息适配组件；只汇总正式库存和读取 Run 目标，不持有鱼、目标或消费命令的第二份玩法状态。 */
UCLASS(ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatFishTankWorldInfoComponent : public UCatWorldInfoComponent
{
	GENERATED_BODY()

public:
	/** 配置默认 500 厘米附近摘要策略并启用只读摘要复制；不会创建额外 UI 或 Tick。 */
	UCatFishTankWorldInfoComponent();

	/** 注册整份摘要的复制字段；客户端通过同一次 RepNotify 请求本地视图重建。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 按公共 WorldInfo 协议返回储备、今日任务与详情；未知数据明确显示待同步，不用零填补缺失。 */
	virtual bool BuildInfo_Implementation(APlayerController* Viewer, ECatWorldInfoDetail Detail,
		FCatWorldInfoViewData& OutData) const override;

	/** 服务器在库存变化或 Actor 初始化容量后重建摘要；客户端调用无效果，未完成 Actor 入场时延后到显式刷新。 */
	void RefreshSummary();

	/** 返回已经就绪的储备总点数、条数和实际容量；false 时三个输出均为 INDEX_NONE，供本组件视图和祭坛保守处理未知值。 */
	bool TryGetOfferingSummary(int32& OutPoints, int32& OutCount, int32& OutCapacity) const;

protected:
	/** 入场时注册公共锚点、服务器库存监听及本机 GameState 监听；初次摘要由 Actor 完成容量设置后发布。 */
	virtual void BeginPlay() override;

	/** 退出前成对解除库存、Run 快照和 World 的监听，再注销公共锚点，不留下旅行后的通知。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 当前公开摘要的服务器结果或最近复制结果；服务器由 RefreshSummary 写入，客户端由属性复制更新，本组件视图与祭坛经只读查询消费，不参与库存裁决。 */
	UPROPERTY(ReplicatedUsing=OnRep_OfferingSummary)
	FCatFishTankOfferingSummary OfferingSummary;

	/** 服务器实际订阅的正式库存；弱引用不延长库存寿命，EndPlay 按它解除同一事件源的监听。 */
	TWeakObjectPtr<UCatFishOnlyInventoryComponent> ObservedInventory;

	/** 当前本机订阅的 Run 公开状态宿主；World 更换 GameState 时解绑旧宿主，读取目标不另存一份数值。 */
	TWeakObjectPtr<ACatfishingGameState> ObservedGameState;

	/** 库存变化监听的配对凭据；服务器 BeginPlay 设置，EndPlay 移除，客户端保持无效。 */
	FDelegateHandle InventoryChangedHandle;

	/** Run 公开快照监听的配对凭据；GameState 接入时设置，更换或退出时移除，只递增本地信息序号。 */
	FDelegateHandle RunChangedHandle;

	/** World 发布 GameState 的监听凭据；处理客户端 GameState 晚到，退出时移除，不使用轮询重试。 */
	FDelegateHandle GameStateSetHandle;

	/** 复制或服务器发布摘要后通知本地 UI，并以默认落盘等级记录本端观察到的完整数值。 */
	UFUNCTION()
	void OnRep_OfferingSummary();

	/** 本机 GameState 建立或替换后切换 Run 快照监听并请求重读；非正式 GameState 按目标未知处理。 */
	void HandleGameStateSet(AGameStateBase* GameState);
};
