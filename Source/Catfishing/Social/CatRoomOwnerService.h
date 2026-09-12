#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatRoomOwnerService.generated.h"

class AController;
class AGameModeBase;
class APlayerController;
class APlayerState;

/**
 * 房间管理层的房主（2026-09-07 决策点⑩「房主两层论」）：
 * **游戏机制层没有房主**——钓鱼、移动、献祭都没有身份特权；房主只活在房间管理层，职责是踢人、开局、翻天兜底，
 * 离开时移交最早加入者（联机社交 §3.1.1）。
 *
 * 为什么它不是 Online 那个 Host：`ECatOnlineSessionRole::Host` 说的是「本进程是这局的 listen server」，
 * 那是网络事实；房主是社交事实。两者在开局那一刻恰好是同一个人（建局的人最先登录），此后就可以分开——
 * 本服务正是把它们分开的地方，这样「房主走了」不再等于「这局没了」（09-09 账本六问⑤：只换房主、不收口、本局接着打）。
 *
 * 仍然没被分开的那一半写在 Docs 交接说明里：listen server 进程退出时世界随之消失，这需要真正的 Host Migration
 * （新 listen server 重建会话 ＋ 世界状态跨机迁移 ＋ 全员重连），不在本轮范围内。
 *
 * 它不持有任何玩法状态，只持有「谁先来的」这张顺序表和当前房主身份；写口在 authority，读口经 GameState 复制。
 */
UCLASS()
class CATFISHING_API UCatRoomOwnerService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；客户端从 ACatfishingPlayerState::IsRoomOwner() 读复制结论，不在本地平行推断。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 订阅引擎的 PostLogin/Logout 事件建立加入顺序；不改 GameMode，也不接管它的准入协议。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 成对解除订阅并清空顺序表；World 销毁后不得再有回调改一个已消失房间的房主。 */
	virtual void Deinitialize() override;

	/** 当前房主的公开身份；未确立时返回空。客户端请改读 ACatfishingPlayerState::IsRoomOwner()。 */
	APlayerState* GetRoomOwnerPlayerState() const;

	/** 该 Controller 当前是不是房主；空 Controller、无身份或房主尚未确立一律 false。 */
	bool IsRoomOwner(const AController* Controller) const;

	/**
	 * 房主把某人踢出本局（联机社交 §3.1.1、§4 软性：一切社交僵局的兜底）。
	 *
	 * 「被踢者跟人走的资产无损」是自动成立的：跟人走的三样（个人图鉴、印记相册、外观解锁）都在 durable Profile 里，
	 * 本入口一个字都不碰；局内物资本来就跟局走，踢与不踢一样留在局里。
	 *
	 * 走的是现成的 Logout 路径：先按主动离局标记，再交给 GameSession::KickPlayer，
	 * 于是准入记录（资格）与重连记录（ready）由 GameMode::Logout 一次删干净，这里不另建第二套清理。
	 */
	FCatDomainCommandResult RequestKickPlayer(AController* RequestingController, APlayerState* TargetPlayerState,
		FGuid RequestId);

private:
	/** 新玩家登录：首次见到就分配加入序号（重连沿用原序号，最早加入者的身份不会因为掉线一次而丢），房主空缺则由他接任。 */
	void HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);

	/** 玩家离开：房主走了才移交，其他人走不动房主；本入口不关闭本局任何东西。 */
	void HandleLogout(AGameModeBase* GameMode, AController* Exiting);

	/** 在仍然连着的玩家里挑加入序号最小的接任；并列时按 PlayerId 取小，保证服务器结果可复现。 */
	void TransferRoomOwnershipToEarliestJoiner(const AController* DepartingController);

	/** 写入房主身份并把复制标记从旧房主挪到新房主；相同身份重复写入直接返回。 */
	void PublishRoomOwner(APlayerState* NewOwnerPlayerState, const TCHAR* Reason);

	/** 只读 Controller 的 PlayerState::UniqueId；无效返回空串。原始值只留在本服务私有的顺序表里。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 事件是不是发生在本 World（引擎那两个委托是全局静态的，PIE 下多份 World 会互相串门）。 */
	bool IsSameWorld(const AGameModeBase* GameMode) const;

	/** StableNetId 到加入序号；只增不减，退出的人保留原序号，重连回来仍算「那么早就来了」。 */
	TMap<FString, int64> JoinSequenceByPlayer;

	/** 下一个加入序号；单调递增，不复用。 */
	int64 NextJoinSequence = 1;

	/** 当前房主的服务器私有身份；空串表示房主空缺（开局前或全员离开后）。 */
	FString RoomOwnerStableNetId;

	/** 当前房主的公开投影；GameState 复制同一份给客户端。 */
	TWeakObjectPtr<APlayerState> RoomOwnerPlayerState;

	/** 身份+RequestId 到踢人命令首次终态；网络重试不会连踢两次。 */
	TMap<FString, FCatDomainCommandResult> KickTerminalCache;

	/** 引擎 PostLogin/Logout 的订阅句柄；Deinitialize 成对移除。 */
	FDelegateHandle PostLoginHandle;
	FDelegateHandle LogoutHandle;
};
