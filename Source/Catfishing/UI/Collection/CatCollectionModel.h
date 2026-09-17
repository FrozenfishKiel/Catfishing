#pragma once

#include "CoreMinimal.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "UObject/Object.h"
#include "CatCollectionModel.generated.h"

class UCatProfileSubsystem;
class ULocalPlayer;

/** Collection Model 投影变化通知；View 收到后只重绘图鉴列表。 */
DECLARE_MULTICAST_DELEGATE(FCatCollectionModelChanged);

/** 将正式鱼目录、物品总表和账号捕获记录组合为唯一鱼卡投影；图鉴和追踪读取同一份数据。 */
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

	/** 保存或取消一个已捕获鱼种的追踪；零代表取消，成功由 Profile 广播刷新。 */
	bool SetTrackedFish(int32 ItemId);

	/** 图鉴投影变化通知。 */
	FCatCollectionModelChanged OnViewStateChanged;

private:
	/** Profile 图鉴变化入口；事件只表示需要重读，不携带写权限。 */
	void HandleFishCollectionChanged();

	/** 当前 LocalPlayer 的 Profile 协调器；Bind 写入、Refresh 读取其账号图鉴快照，不读取旧本机图鉴归档。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatProfileSubsystem> BoundProfile;

	/** Profile 图鉴变化订阅句柄；Unbind 必须从同一 Profile 移除。 */
	FDelegateHandle FishCollectionChangedHandle;

	/** 最近发布给 Collection View 的完整投影。 */
	FCatCollectionViewState ViewState;
};
