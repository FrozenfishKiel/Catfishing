#pragma once

#include "CoreMinimal.h"
#include "Save/CatRunSaveGame.h"
#include "UObject/Object.h"
#include "CatFrontendSaveModel.generated.h"

class UCatSaveSubsystem;
class ULocalPlayer;

/** 存档列表数据变化通知；View 收到后重新读取本 Model 的只读槽位、busy 和结果，不持有可写槽位镜像。 */
DECLARE_MULTICAST_DELEGATE(FCatFrontendSaveModelChanged);

/**
 * 主界面存档列表的只读适配 Model；它把 LocalPlayer 生命周期限定到 GameInstance 的正式 Save 子系统。
 * SaveList 和 SaveSlotRow 通过它读取真实槽摘要、忙碌和结果，任何新建、读取或删除仍只提交给 Save 子系统。
 */
UCLASS()
class CATFISHING_API UCatFrontendSaveModel : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 绑定指定 LocalPlayer 所属 GameInstance 的 Save 子系统；初始化完成后立即请求真实槽目录刷新并发布可读取状态。
	 * 本方法不读取 Profile、不构造虚拟日期或进度，也不创建内存存档替身。
	 */
	void Initialize(ULocalPlayer* InLocalPlayer);

	/**
	 * 解绑 Save 子系统通知并清空生命周期弱引用；Controller、World 或 LocalPlayer 销毁时可重复调用。
	 * 本方法不取消已经被正式 Save 子系统接管的异步磁盘请求，迟到通知会因解绑而不再作用于 View。
	 */
	void Shutdown();

	/** 请求异步刷新真实世界存档槽目录；重复刷新由 Save 子系统合并，View 通过 IsBusy 和 OnChanged 表现等待状态。 */
	void RefreshSlotSummaries();

	/** 将玩家输入的显示名称交给 Save 子系统创建空世界槽；同步受理不等于写盘成功，最终状态必须等 OnChanged。 */
	FCatSaveResult RequestCreateSlot(const FString& DisplayName);

	/** 将稳定槽标识交给 Save 子系统异步读取；成功后只提供旅行许可，Controller 决定何时请求房间创建。 */
	FCatSaveResult RequestLoadSlot(FName SlotId);

	/** 将稳定槽标识交给 Save 子系统删除真实文件；活动槽、busy 或非法槽的拒绝完整保留在正式结果文本中。 */
	FCatSaveResult RequestDeleteSlot(FName SlotId);

	/** 释放尚未交给已成立房间的本局载荷与旅行许可；Controller 确认未入房流程结束后调用，忙碌或来源缺失返回 false，目录和文件保持不变。 */
	bool ReleaseActiveRun();

	/** 返回 Save 子系统最近一次成功读写的真实槽摘要；调用方只读，不能把数组作为第二份列表状态改写。 */
	const TArray<FCatSaveSlotSummary>& GetSlotSummaries() const;

	/** 返回是否已有成功读取、尚待世界恢复的正式旅行许可；Controller 只在它与当前选择匹配时进入创建房间。 */
	bool HasLoadedRunForTravel() const;

	/** 返回 Save 子系统是否正在执行真实异步目录或世界 I/O；View 在 true 时保持存档页并禁用重复提交。 */
	bool IsBusy() const;

	/** 返回最近一次同步受理或异步终态的可展示文本；View 只显示文本，不能以文本推导磁盘成功。 */
	FText GetLastResultText() const;

	/** 返回 Save 子系统当前活动槽标识；它是 Controller 校验读档结果避免回调串线的唯一稳定键。 */
	FName GetActiveSlotId() const;

	/** 存档页 native 刷新通知；目录、活动槽、busy 或结果变化后广播，订阅者随后重新查询。 */
	FCatFrontendSaveModelChanged OnChanged;

private:
	/** 只返回 Initialize 已绑定且玩家生命周期仍有效的 Save 来源；不重新查找或创建子系统，初始化的获取责任不进入查询路径。 */
	UCatSaveSubsystem* GetSaveSubsystem() const;

	/** 接收正式 Save 子系统通知并转发前端刷新；不复制槽位、结果或 busy 状态，确保只有一个存档真相。 */
	void HandleSaveChanged();

	/** 当前 Model 所属的本地玩家；Initialize 写入、Shutdown 清空，只用于定位正确 GameInstance 生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;

	/** 当前正式 Save 子系统；Initialize 绑定、Shutdown 成对解绑，所有查询与命令都经由它完成。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatSaveSubsystem> SaveSubsystem;

	/** Save 子系统 OnChanged 的配对解绑句柄；只在有效订阅期间存在，避免 LocalPlayer 销毁后的迟到 View 刷新。 */
	FDelegateHandle SaveChangedHandle;
};
