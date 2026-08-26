#pragma once

#include "CoreMinimal.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UObject/Object.h"
#include "CatCollectionModel.generated.h"

class UCatProfileSubsystem;
class ULocalPlayer;

/** Collection Model 投影变化通知；View 收到后只重绘图鉴列表。 */
DECLARE_MULTICAST_DELEGATE(FCatCollectionModelChanged);

/** 图鉴/相册 Model；它只读 LocalPlayer Profile 的 durable 图鉴快照，不访问实物鱼容器。 */
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

	/** 返回最近一次图鉴投影；调用方只能读取。 */
	const FCatCollectionViewState& GetViewState() const;

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
