#pragma once

#include "CoreMinimal.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UObject/Object.h"
#include "CatCollectionModel.generated.h"

class UCatProfileSubsystem;
class ULocalPlayer;

/** Collection Model 投影变化通知；View 收到后只重绘图鉴列表。 */
DECLARE_MULTICAST_DELEGATE(FCatCollectionModelChanged);

/**
 * 图鉴/相册 Model；它以正式鱼目录为骨架、用 LocalPlayer Profile 的 durable 快照覆盖解锁位，不访问实物鱼容器。
 * 唯一的写口是印记隐藏（印记册「本人可一键隐藏任意一张」），图鉴记录本身只读。
 */
UCLASS()
class CATFISHING_API UCatCollectionModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定 LocalPlayer Profile；成功后订阅图鉴变化并发布首份只读投影。 */
	bool Bind(ULocalPlayer* InLocalPlayer);

	/** 解除 Profile 图鉴变化订阅并清空当前投影。 */
	void Unbind();

	/** 主动从 Profile 重读图鉴快照；Profile 未就绪时发布 unavailable 状态。 */
	void Refresh();

	/** 提供最近发布的图鉴投影副本；View 用它重绘，不通过返回值拿 Profile 写权。 */
	const FCatCollectionViewState& GetViewState() const;

	/**
	 * 本人一键隐藏／取消隐藏相册里的任意一张印记；转交 Profile 的唯一 durable 写口，成功后重发投影。
	 * 它只改本地这份索引：不发服务器 RPC、不删图片、不影响其他参与者手里的同一张。
	 */
	bool SetImprintHidden(FGuid ImprintId, bool bHidden);

	/** 图鉴投影变化通知。 */
	FCatCollectionModelChanged OnViewStateChanged;

private:
	/** Profile 图鉴变化入口；事件只表示需要重读，不携带写权限。 */
	void HandleFishCollectionChanged();

	/** 当前 LocalPlayer 的 durable Profile 读源。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatProfileSubsystem> BoundProfile;

	/** Profile 图鉴变化订阅句柄；Unbind 必须从同一 Profile 移除。 */
	FDelegateHandle FishCollectionChangedHandle;

	/** 最近发布给 Collection View 的完整投影。 */
	FCatCollectionViewState ViewState;
};
