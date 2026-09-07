#pragma once

#include "CoreMinimal.h"
#include "AudioDeviceHandle.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Containers/Ticker.h"
#include "UObject/Object.h"
#include "CatAudioOutputRequest.generated.h"

/**
 * 一次输出设备枚举或切换的生命周期；持有对应 AudioDevice，在音频线程读取完成更新后的活动设备信息，最多等待八秒。
 * 页面和正式设置宿主共用此请求；它没有设置草稿或持久化职责，调用方必须强持有到结束，并在关闭时 Cancel。
 */
UCLASS(Transient)
class CATFISHING_API UCatAudioOutputRequest : public UObject
{
	GENERATED_BODY()

public:
	/** 请求最终结果通知；空错误表示枚举完成或目标设备已活动，原生委托允许调用方附带自身生命周期代次。 */
	using FOnCompleted = TDelegate<void(UCatAudioOutputRequest*, FName)>;

	/** 仅允许新对象启动一次操作；空设备 ID 枚举，非空 ID 切换，完成委托报告最终结果，禁止复用已结束对象以隔离旧回调。 */
	void Start(UWorld* World, const FString& DeviceId, FOnCompleted InCompleted);

	/** 放弃结果接收并撤销轮询、释放设备句柄；不能撤回平台已受理的硬件切换，但晚到结果不会再通知调用方。 */
	void Cancel();

	/** 对象回收时对称撤销轮询和完成委托，避免无人接收的请求持续占用设备。 */
	virtual void BeginDestroy() override;

	/** 返回请求所属的弱 World；调用方可在该 World 清理时取消请求，不会延长地图生命周期。 */
	virtual UWorld* GetWorld() const override;

	/** 请求目标是在创建时固定的音频输出设备 ID；空字符串代表枚举模式，完成回调用它区分枚举和切换结果。 */
	const FString& GetRequestedDeviceId() const;

	/** 返回枚举所得设备；完成前仅由请求回调写入，当前设备标志经过音频线程实际活动 ID 校正。 */
	const TArray<FAudioOutputDeviceInfo>& GetDevices() const;

private:
	/** 每 0.1 秒检查一次所属 World、设备和八秒真实时间限额，再提交首次操作或单个活动设备查询。 */
	bool TickRequest(float DeltaTime);

	/** 接收引擎枚举结果并发起活动设备核对；空结果明确失败，已取消或已结束的请求忽略回调。 */
	void HandleDevicesObtained(const TArray<FAudioOutputDeviceInfo>& AvailableDevices);

	/** 仅记录 RequestDeviceSwap 的受理状态；拒绝立即结束，受理后继续等待音频线程观察到目标设备。 */
	void HandleSwapAccepted(const FSwapAudioOutputResult& Result);

	/** 在音频线程读取本请求强持有设备的活动 ID，并在游戏线程消费；同一时刻最多一个查询，不从硬件枚举缓存猜测当前设备。 */
	void QueryActiveDevice();

	/** 消费所属设备的实际活动 ID；枚举时校正当前标志，切换前允许已在目标的无操作成功，受理后只有目标匹配才成功。 */
	void HandleActiveDevice(const FString& ActiveDeviceId);

	/** 撤销计时、释放设备并一次性通知调用方；错误码明确区分超时、World 失效、设备缺失和请求拒绝。 */
	void Finish(FName Error);

	/** 请求所依据的 World；Start 写入，查询和生命周期检查读取，弱引用避免阻止旅行清理。 */
	TWeakObjectPtr<UWorld> RequestWorld;

	/** 请求期间固定的音频设备句柄；跨线程查询各自复制它以保证设备寿命，Cancel 和 Finish 释放本对象持有的一份。 */
	FAudioDeviceHandle AudioDevice;

	/** 请求创建时固定的输出设备目标；空值为枚举，非空值用于比较实际活动设备，完成前不会被页面草稿覆盖。 */
	FString RequestedDeviceId;

	/** 本次枚举结果；HandleDevicesObtained 写入，实际活动查询校正标志后才对外发布。 */
	TArray<FAudioOutputDeviceInfo> Devices;

	/** 唯一完成通知；Start 接管，Finish 取走执行，Cancel 清空，避免旧回调修改重新初始化的调用方。 */
	FOnCompleted Completed;

	/** 真实时间轮询句柄；Start 登记，Cancel、Finish 或 Tick 返回 false 时撤销，暂停游戏不会暂停超时。 */
	FTSTicker::FDelegateHandle TickerHandle;

	/** 本次请求截止的单调时钟秒数；零表示尚未启动，Start 写入后不重置，既限定八秒等待也禁止复用旧请求身份。 */
	double DeadlineSeconds = 0.0;

	/** 首次枚举或实际切换命令是否已提交；TickRequest 与活动查询共同维护，防止轮询反复发起平台切换。 */
	bool bSubmitted = false;

	/** 平台是否已受理切换；受理回调写入，只决定是否继续查询，不能直接触发保存。 */
	bool bSwapAccepted = false;

	/** 是否已有活动设备查询排队；游戏线程发起时设置、消费时清除，防止音频线程迟滞造成队列堆积。 */
	bool bQueryPending = false;

	/** 本次请求是否已结束；Start 清除，Cancel 和 Finish 设置，所有晚到的异步结果先检查它。 */
	bool bFinished = true;
};
