#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "UObject/Object.h"
#include "CatOnlineSettings.generated.h"

/** 当前实现能可靠分类的可恢复断线原因位；其他原因没有证据时不得塞入掩码。 */
UENUM()
enum class ECatRecoverableFailure : uint8
{
	/** 没有可恢复失败。 */
	None = 0,
	/** 未收到主动离局标记的连接丢失；恢复只重建准入，不续接 Fishing 半场。 */
	ConnectionLost = 1 << 0
};
ENUM_CLASS_FLAGS(ECatRecoverableFailure);

/** 集中的 Online 策略入口；所有 O 值默认保持 Undecided、零或负值哨兵，运行路径不得自行补产品结论。 */
UCLASS(Config = Game, DefaultConfig)
class CATFISHING_API UCatOnlineSettings : public UObject
{
	GENERATED_BODY()

public:
	/** 判断重连准入配置是否完整；只接受当前实现的 ConnectionLost 位和正 TTL，出现任何未识别位都 fail-closed。 */
	bool IsReconnectAdmissionReady() const;

	/** 读取 CreateSession 应使用的公开连接容量；只有 1 到 8 人的好友局边界被当前 WORK-02 合同接受，读取失败会把输出清零。 */
	bool TryGetSessionPublicConnectionLimit(int32& OutPublicConnectionLimit) const;

	/** 读取 Host exit 等待远端 Destroy ACK 与最终 Grant ACK 的统一正超时秒数；未裁或非有限时返回 false。 */
	bool TryGetHostExitAckTimeout(double& OutSeconds) const;

	/** 读取 Create/Find/Join/Destroy 等待平台完成回调的统一正超时秒数；未裁或非有限时返回 false，请求入口据此拒绝无界等待。 */
	bool TryGetSessionOperationTimeout(double& OutSeconds) const;
	/** Session 的搜索与准入策略；Create/Find 在 Undecided 时返回 PolicyUndecided。 */
	UPROPERTY(Config, EditAnywhere, Category = "Session")
	ECatSessionAccessPolicy SessionAccess = ECatSessionAccessPolicy::Undecided;

	/** 建局时对外开放的连接容量，表示当前好友局允许同时进入的玩家数；0 表示尚未裁定，CreateSession 不得用硬编码人数替代它。 */
	UPROPERTY(Config, EditAnywhere, Category = "Session", meta = (ClampMin = "0", ClampMax = "8"))
	int32 SessionPublicConnectionLimit = 0;

	/**
	 * 一次平台会话操作（CreateSession/FindSessions/JoinSession/DestroySession）允许等待完成回调的秒数；0 表示未裁，四
	 * 个操作都会因此拒绝提交，而不是无限挂起。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Session", meta = (ClampMin = "0.0"))
	double SessionOperationTimeoutSeconds = 0.0;

	/** 主动 Client leave 是否保留重连资格；未裁时离局请求不会把 Disabled 或 Enabled 当默认。 */
	UPROPERTY(Config, EditAnywhere, Category = "Reconnect")
	ECatPolicyDecision VoluntaryLeaveRecovery = ECatPolicyDecision::Undecided;

	/** 断线记录保留秒数；负值表示尚未裁定 TTL，任何恢复/过期分支不得读取它作为时长。 */
	UPROPERTY(Config, EditAnywhere, Category = "Reconnect", meta = (ClampMin = "-1"))
	int32 ReconnectRecordTtlSeconds = INDEX_NONE;

	/** 允许创建恢复准入记录的 ECatRecoverableFailure 白名单位集；负值表示未裁，未识别位或非 ConnectionLost 位会使重连 gate 失败。 */
	UPROPERTY(Config, EditAnywhere, Category = "Reconnect")
	int64 RecoverableFailureMask = -1;

	/** 重连记录过期后是否允许按新玩家准入；Undecided 时不得恢复或静默当作中途加入。 */
	UPROPERTY(Config, EditAnywhere, Category = "Reconnect")
	ECatPolicyDecision ExpiredAdmission = ECatPolicyDecision::Undecided;

	/** 原始 StableNetId 是否允许出现在公开快照或完整日志；Undecided 时只记录 Valid/Redacted。 */
	UPROPERTY(Config, EditAnywhere, Category = "Privacy")
	ECatPolicyDecision StableNetIdExposure = ECatPolicyDecision::Undecided;

	/** Host exit 等待远端 DestroySession ACK 与最终 Profile Grant ACK 的统一秒数；0 表示未裁，不能直接跳过有界收口。 */
	UPROPERTY(Config, EditAnywhere, Category = "Leave", meta = (ClampMin = "0.0"))
	double HostExitAckTimeoutSeconds = 0.0;
};
