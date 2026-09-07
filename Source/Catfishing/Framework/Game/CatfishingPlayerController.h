#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Fishing/CatFishingTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Framework/Core/CatRunContracts.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Items/CatItemTypes.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatfishingPlayerController.generated.h"

class UCatAbilityInputBindingComponent;
class UCatCampBodyActionCommandComponent;
class UCatFishingCommandComponent;
class UCatInteractionTargetingComponent;
class UCatSocialBodyActionCommandComponent;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class ACatCampHubActor;
class ACatCampInventoryActor;
class ACatCharacter;
class ACatShopKioskActor;
struct FInputActionValue;

/** owning client 收到献祭协议结果后的本机通知；UI Model 只用它刷新读模型，不改变 Items 或 Run。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatSacrificeResultReceived, const FCatSacrificeResult&);

/** owning client 收到公共领域命令结果后的本机通知；UI Model 只用它关联 RequestId，不重新执行领域动作。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCampCommandResultReceived, const FCatDomainCommandResult&);

/** owning client 收到直接吃鱼结果后的本机通知；UI Model 只用它证明鱼护命令终态并刷新显示。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatFishConsumeResultReceived, const FCatFishConsumeResult&);

/** Lake owning-client 的网络适配器；将 Run、Fishing、Camp、Condition、Items、Social 意图转给 authority，并承接 Profile Grant/CapturePlan/HostExit 回执，不自存领域真相。 */
UCLASS()
class CATFISHING_API ACatfishingPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	/** 控制器接管 Pawn 后只做宿主级收口：重置临时钓鱼输入和疾跑状态；Ability ASC 路由统一由 SetPawn 写入点刷新。 */
	virtual void OnPossess(APawn* InPawn) override;
	/** owning client 收到 Pawn 复制变化后重置临时输入与疾跑状态；复制链中的 SetPawn 负责切换 Ability ASC 路由。 */
	virtual void OnRep_Pawn() override;
	/** 捕获服务器、客户端复制和 ClientRestart 的统一 Pawn 写入点；同步刷新 Ability ASC 路由，再通知 LocalPlayer UI 重新装配。 */
	virtual void SetPawn(APawn* InPawn) override;
	/** 把客户端额度意图转发给 authority GameMode；身份由服务器 PlayerState 派生。 */
	UFUNCTION(Server, Reliable)
	void ServerSubmitQuotaContribution(FGuid RequestId, int64 ExpectedRevision, int32 Contribution);
	/** 把客户端翻天确认转发给 authority GameMode；GameMode 负责资格、Revision 与幂等裁决。 */
	UFUNCTION(Server, Reliable)
	void ServerSetNextDayReady(FGuid RequestId, int64 ExpectedRevision, bool bReady);

	/** 结算夜请求检查本局成像终态与 Grant ACK；只有归档已收口才向 Run StateTree 发送 SettlementComplete。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestSettlementCompletion(FGuid RequestId, int64 ExpectedRevision);

	/** 服务器向 owning client 投递不可变永久 Grant；客户端只有 durable Profile 完成后才发 ACK。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveProfileGrant(const FCatProfileGrant& Grant);

	/** 客户端在本地 Journal 完整落盘后确认 GrantId；服务器重建身份，并在真实 ACK 后复核 Host exit 有界等待。 */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeProfileGrant(FGuid GrantId);

	/** owning client 把 durable Profile 中的装备解锁摘要提交给服务器；服务器只把它作为本 PlayerState 的本局授权投影。 */
	UFUNCTION(Server, Reliable)
	void ServerPublishEquipmentUnlocks(const TArray<FName>& UnlockIds);

	/** 服务器向 owning client 投递独立 CapturePlan；本 RPC 不表示图片或 Grant 已成功。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveImprintCapturePlan(const FCatCapturePlan& Plan);

	/** 外部本地成像桥回报真实结果；成功必须携带 durable ImprintId，服务器随后才生成 Grant。 */
	UFUNCTION(Server, Reliable)
	void ServerReportImprintCaptureResult(FGuid CapturePlanId, bool bSucceeded, FGuid ImprintId);

	/** 把 Giant HookedFight 的手动协作意图转给指定 FishingSession。 */
	UFUNCTION(Server, Reliable)
	void ServerAssistFishingSession(FGuid FishingSessionId, FGuid RequestId, int64 ExpectedRevision);

	/** 把抄网意图转给指定 FishingSession；后续由命令组件和 Session 判断鱼是否可被抄起并完成结果。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestScoop(FGuid FishingSessionId, FCatScoopCommand Command);

	/** 建立项目玩家控制器的输入、交互和命令组件宿主；具体输入绑定会在 BeginPlay/SetupInputComponent 阶段安装。 */
	ACatfishingPlayerController();

	/** 返回本 Controller 持有的钓鱼命令组件；调用方只能通过它提交钓鱼意图，不把组件当库存或 Run 状态来源。 */
	UFUNCTION(BlueprintPure, Category="Catfishing|Fishing")
	UCatFishingCommandComponent* GetFishingCommandComponent() const;

	/** 返回本 Controller 持有的交互扫描组件；表现层可读取当前命中目标，真正交互仍走目标 Actor 的接口。 */
	UFUNCTION(BlueprintPure, Category="Catfishing|Interaction")
	UCatInteractionTargetingComponent* GetInteractionTargetingComponent() const { return InteractionTargetingComponent; }

	/** 按 Controller 的水平朝向把二维输入转成当前 Pawn 的前后/左右移动；自动化夹具也通过同一入口验证正式移动仲裁。 */
	void Move(const FInputActionValue& Value);
	/** 对当前已占有的 Character 开始跳跃；自动化夹具复用正式输入入口，避免另建测试专线。 */
	void StartJump();

	/** 权威交互转发；服务器检查玩法 gate 和通用接口后，在目标 Actor 上重新调用同一 Interact 虚函数。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestInteraction(AActor* Target, FGuid RequestId);


	/** 由 owning client 发起献祭服务器入口；把命令转给唯一 SacrificeCoordinator，完成后通过 ClientReceiveSacrificeResult 回送完整阶段结果，Controller 不直接删鱼或增加 Run 额度。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestSacrifice(FCatSacrificeCommand Command);

	/** 服务器把献祭协调器的完整阶段结果可靠发给 owning client；客户端只保存最近读模型，不据此改鱼或 Run 额度。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveSacrificeResult(const FCatSacrificeResult& Result);

	/** 返回本机最近收到的献祭协议结果供 UI 关联 RequestId 和阶段；权威恢复仍只发生在服务器协调器。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Run")
	FCatSacrificeResult GetLastSacrificeResult() const;

	/** 献祭结果到达 owning client 后广播的本机读模型事件；订阅方不能通过它修改服务器状态。 */
	FCatSacrificeResultReceived OnSacrificeResultReceived;

	/** 由 owning client 发起固定营地休息请求；把位置和身体裁决交给 Camp/Condition，完成后通过 ClientReceiveCampCommandResult 回送领域结果。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestCampRest(ACatCampHubActor* Camp, FGuid RequestId);

	/** 由 owning client 发起固定营地篝火回看请求；Camp 在结算夜全员在场且 CapturePlan 建立成功后触发表现 multicast，并通过 ClientReceiveCampCommandResult 回送领域结果。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestCampfirePlayback(ACatCampHubActor* Camp, FGuid RequestId);

	/** 由 owning client 发起普通容器库存拖拽请求；这是 Items 请求，不进入 BodyAction/Social，服务器按容器宿主、距离、Revision 和 Items 权限复核后回送领域结果。 */
	UFUNCTION(Server, Reliable)
	void ServerTransferObjectBetweenContainers(FGuid RequestId, ECatContainedObjectKind ObjectKind, FGuid ObjectInstanceId,
		FGuid SourceContainerId, ECatContainerKind SourceContainerKind, int32 SourceContainerSlotIndex,
		int64 ExpectedSourceRevision, FGuid TargetContainerId, ECatContainerKind TargetContainerKind,
		int32 TargetContainerSlotIndex,
		int64 ExpectedTargetRevision);

	/** 由 owning client 从鱼护页请求把选中鱼存入营地共享鱼缸；服务器重读源鱼护、固定营地鱼缸和首个空目标格后复用 Items 转移。 */
	UFUNCTION(Server, Reliable)
	void ServerStoreFishInSharedTank(FGuid RequestId, FGuid FishInstanceId, FGuid SourceContainerId,
		int32 SourceContainerSlotIndex, int64 ExpectedSourceRevision);

	/** 由 owning client 发起伙伴救援请求；把倒地目标送往固定营地 RescuePoint 并交给 Camp/Condition 裁决，完成后通过 ClientReceiveCampCommandResult 回送领域结果，不进入死亡或重生旁路。 */
	UFUNCTION(Server, Reliable)
	void ServerRescueCharacterToCamp(ACatCampHubActor* Camp, ACatCharacter* TargetCharacter, FGuid RequestId);

	/** 服务器把公共领域命令结果可靠发给 owning client；所有路径都保留原 RequestId，客户端不重算 Revision 或领域错误。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveCampCommandResult(const FCatDomainCommandResult& Result);

	/** 从指定营地公共仓库 Actor 取物到本人随身库存；直接箱子交互使用它，服务器按距离、仓库版本和随身库存版本共同裁决。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Camp")
	void ServerWithdrawCampInventoryItemAtActor(ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 SourceSlotIndex, int32 Quantity, int64 ExpectedEquipmentRevision);

	/** 整理指定营地公共仓库 Actor 内部两个格子；服务器按仓库距离、版本和槽位下标重读后移动、合并或交换。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Camp")
	void ServerMoveCampInventorySlotAtActor(ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 把本人随身库存指定格拖入营地公共仓库指定格；服务器按双方版本和仓库距离同时裁决两份数据源。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Camp")
	void ServerDepositInventoryItemToCampAtActor(ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 TargetCampSlotIndex, int64 ExpectedEquipmentRevision,
		int32 SourceEquipmentSlotIndex);

	/** 把营地公共仓库指定格拖到本人随身库存指定格；服务器按双方版本和仓库距离同时裁决两份数据源。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Camp")
	void ServerWithdrawCampInventoryItemToSlotAtActor(ACatCampInventoryActor* CampInventory, FGuid RequestId,
		int64 ExpectedCampInventoryRevision, int32 SourceCampSlotIndex, int64 ExpectedEquipmentRevision,
		int32 TargetEquipmentSlotIndex);

	/** 返回本机最近收到的公共领域命令结果供表现层关联请求；该缓存不作为 Camp、Items、Equipment 或 Condition 的权限事实。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Camp")
	FCatDomainCommandResult GetLastCampCommandResult() const;

	/** 公共领域命令结果到达 owning client 后广播的本机读模型事件；订阅方只能刷新 UI。 */
	FCatCampCommandResultReceived OnCampCommandResultReceived;

	/** 供保留的 Camp/Social BodyAction Ability 查询当前服务器玩法命令 gate；它只暴露开关结果，不泄漏 GameMode、阶段对象或具体业务入口。 */
	bool CanSubmitBodyActionCommand() const;

	/** 供保留的 Camp/Social BodyAction Ability 回送领域命令终态给 owning client；它只做网络适配，不重算结果或提交第二次领域动作。 */
	void DeliverBodyActionCommandResultToOwningClient(const FCatDomainCommandResult& Result);

	/** 提交当前钓鱼选择；实例 ID 用来锁定同定义下的具体物品，服务器仍以目录、解锁证明和库存事实作最终裁决。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Equipment")
	void ServerConfigureEquipment(FGuid RequestId, int64 ExpectedRevision, FName RodDefinitionId,
		FName BaitDefinitionId, FName FloatDefinitionId, FName ScoopNetDefinitionId,
		FGuid RodItemInstanceId, FGuid BaitItemInstanceId, FGuid FloatItemInstanceId,
		FGuid ScoopNetItemInstanceId);

	/** 整理当前角色随身库存中的两个格子；服务器按 Equipment Revision 和数组下标重读后移动、合并或交换。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Equipment")
	void ServerMoveInventorySlot(FGuid RequestId, int64 ExpectedRevision,
		int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 从指定商店摊位支付整车服务器目录项；服务器先限制购物车载荷，再复核摊位和营地公共仓库。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Shop")
	void ServerSubmitShopCartAtKiosk(ACatShopKioskActor* ShopKiosk,
		const TArray<FCatShopCartLineCommand>& Lines, FGuid RequestId, int64 ExpectedWalletRevision);

	/** 售出指定 Items 鱼容器中的鱼；服务器从地面鱼护箱子或共享鱼缸读取重量，并在删除鱼后把收入记入团队公款。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Shop")
	void ServerSellFish(FGuid FishInstanceId, FGuid ContainerId, int64 ExpectedContainerRevision,
		FGuid RequestId, int64 ExpectedWalletRevision);

	/** 在固定营地消费浮木并修复当前鱼竿；不升级或替换装备。 */
	UFUNCTION(Server, Reliable)
	void ServerRepairRodAtCamp(ACatCampHubActor* Camp, FGuid RequestId, int64 ExpectedEquipmentRevision);

	/** 消费本人指定草药实例的一份数量后恢复目标 Character；库存提交成功前不会修改身体。 */
	UFUNCTION(Server, Reliable)
	void ServerUseHerbOnCharacter(ACatCharacter* TargetCharacter, FGuid RequestId,
		int64 ExpectedEquipmentRevision, FGuid HerbItemInstanceId);

	/** 从地面鱼护箱子或共享鱼缸直接吃一条鱼；Items 移除成功后才按 FishDefinition 修改 Poison 并推进吃鱼成长。 */
	UFUNCTION(Server, Reliable)
	void ServerConsumeFish(ACatCharacter* EatingCharacter, FCatFishConsumeCommand Command);

	/** 服务器把直接吃鱼的 Items 终态和身体终态可靠发给 owning client；客户端只缓存和展示，不应用身体效果。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveFishConsumeResult(const FCatFishConsumeResult& Result);

	/** 返回本机最近收到的直接吃鱼结果供 UI 关联 RequestId、容器 Revision 和身体提交结果；权威状态仍由各领域复制。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Items")
	FCatFishConsumeResult GetLastFishConsumeResult() const;

	/** 直接吃鱼结果到达 owning client 后广播的本机读模型事件；订阅方只能刷新 UI。 */
	FCatFishConsumeResultReceived OnFishConsumeResultReceived;

	/** 开始一条鱼的偷取与追回窗口；Social 覆盖客户端身份并保证每个小偷最多一条。 */
	UFUNCTION(Server, Reliable)
	void ServerBeginTheft(FCatTheftCommand Command);

	/** 服务器把 Begin/Catch/到期消费的首次或重放结果发回 owning client；ProtocolId 和身体终态只能通过权威结果取得。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveTheftResult(const FCatTheftResult& Result);

	/** 提供本机最近收到的偷鱼协议结果供 UI 读取；它不授权客户端直接访问 Social、Items 或身体写口。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Social")
	FCatTheftResult GetLastTheftResult() const;

	/** 在进食窗口内按服务器返回的 ProtocolId 追回；Social 按权威主人、状态、距离与共享缸策略授权。 */
	UFUNCTION(Server, Reliable)
	void ServerCatchTheft(FGuid TheftProtocolId);

	/** 手动发布普通钓鱼或倒地求助；普通信号不会升级为全局任务。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestManualHelp(FGuid RequestId, ECatHelpSignalKind Kind);

	/** 请求一次普通恶作剧许可；Social 重新验证目标 Controller、冷却与 ProtectionSign。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestMischief(APlayerState* TargetPlayerState, FGuid RequestId, FVector InteractionLocation);

	/** 在本人附近放置或移动唯一防骚扰牌子；Social 用显式范围配置保护普通恶作剧。 */
	UFUNCTION(Server, Reliable)
	void ServerPlaceProtectionSign(FGuid RequestId, FVector SignLocation);

	/** Online Client 在 DestroySession 前通知服务器这是主动离局；GameMode 不把它误判为连接故障。 */
	UFUNCTION(Server, Reliable)
	void ServerMarkVoluntaryLeave();

	/** Host exit 通知远端在本地执行统一 Online Destroy/Frontend 链；不把它标成玩家主动离局。 */
	UFUNCTION(Client, Reliable)
	void ClientPrepareForHostExit(FGuid RequestId);

	/** 远端本地 DestroySession 成功后向 Host 回 ACK；GameMode 只接受当前 Active Controller 与同 RequestId。 */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeHostExit(FGuid RequestId);

	/** 服务器登录完成后让 owning client 从 durable Profile 刷新公开图鉴摘要。 */
	UFUNCTION(Client, Reliable)
	void ClientRefreshPublicFishCollection();

	/** owning client 只提交鱼图鉴记录；PlayerState 在服务器验证唯一 ID、数量和数值后整体发布。 */
	UFUNCTION(Server, Reliable)
	void ServerPublishPublicFishCollection(const TArray<FCatFishCollectionRecord>& Records);

protected:
	/** 为本地 Controller 安装玩法输入层，并在可用时把 durable Profile 装备解锁投影给服务器；服务端远端 Controller 不接触本地输入或 Profile 子系统。 */
	virtual void BeginPlay() override;
	/** 绑定物理移动输入、项目 NativeInput 标签和 AbilityInputConfig；同一 InputComponent 只安装一次 Native/Ability 回调，避免 SetupInputComponent 重入重复绑定。 */
	virtual void SetupInputComponent() override;
	/** Super 完成每帧输入后恰好一次把 Ability 输入组件积累的边沿交给当前 Pawn ASC。 */
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	/** Pawn 断开前先清理当前 ASC 的 Ability 输入状态、钓鱼临时命令和疾跑意图，再交还父类结束占有，避免状态泄漏到下一次占有。 */
	virtual void OnUnPossess() override;
	/** EndPlay 时清空 Ability 路由状态、Native 输入弱绑定记录并只撤销本 Controller 安装的 Mapping Context；不清空 LocalPlayer 的其他输入层。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 玩法输入映射；在 PlayerController 蓝图默认值中接入 IMC。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	/** 二维移动输入：X 为左右，Y 为前后。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> MoveAction;

	/** 二维视角输入：X 为 Yaw，Y 为 Pitch；反转与灵敏度由 IMC Modifier 配置。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> LookAction;

	/** 跳跃输入；Started 调用 Jump，Completed/Canceled 调用 StopJumping。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> JumpAction;

	/** 长按疾跑输入；Started 开启疾跑，Completed/Canceled 恢复普通移动速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	TObjectPtr<UInputAction> SprintAction;

	/** 未按疾跑键时 CharacterMovement 的最大地面移动速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input|Movement",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float WalkMaxSpeed = 100.0f;

	/** 按住疾跑键时 CharacterMovement 的最大地面移动速度。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input|Movement",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float SprintMaxSpeed = 350.0f;

	/** 本 Controller 的输入层优先级；不影响其他系统已经安装的 Mapping Context。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	int32 InputMappingPriority = 0;

private:
	friend class UCatFishingCommandComponent;

	/** 幂等安装当前配置的玩法 Mapping Context；BeginPlay/输入初始化均可安全调用。 */
	void ApplyInputMappingContext();
	/** owning client 读取本地 durable Profile 的 UnlockIds 并提交服务器投影；本方法不生成或修改任何永久 Grant。 */
	void PublishProfileEquipmentUnlocksIfAvailable();
	/** 移除本 Controller 安装的玩法 Mapping Context，并清空弱绑定记录。 */
	void RemoveInputMappingContext();
	/** 把二维输入写入 Controller 的 Yaw/Pitch。 */
	void Look(const FInputActionValue& Value);
	/** 对当前已占有的 Character 停止跳跃。 */
	void StopJump();
	/** 本地 Started 输入开启疾跑，并把布尔意图可靠同步给 authority。 */
	void StartSprint();
	/** 本地 Completed/Canceled 输入关闭疾跑，并把布尔意图可靠同步给 authority。 */
	void StopSprint();
	/** 更新本 Controller 的疾跑意图并应用速度；仅本地输入路径需要向服务器转发。 */
	void SetSprintRequested(bool bNewSprintRequested, bool bNotifyServer);
	/** 把服务器配置的普通/疾跑速度应用到指定 Character；非 Character Pawn 安全跳过。 */
	void ApplySprintSpeed(APawn* TargetPawn, bool bSprinting) const;
	/** 项目原生输入标签入口；处理交互这类非 Ability 动作，未知标签必须保持无副作用。 */
	void NativeInputTagPressed(FGameplayTag InputTag);
	/** 当 Pawn 或输入组件在 owning client 就绪时通知 LocalPlayer UI；服务器远端 Controller 和非 Cat UI World 安全跳过。 */
	void NotifyLocalPlayerUISubsystemPawnChanged();

	/** 把献祭终态投给 owning client；本地 authority 没有网络回环时直接写本机读模型，远端玩家继续走可靠 RPC。 */
	void DeliverSacrificeResultToOwningClient(const FCatSacrificeResult& Result);
	/** 把公共领域命令终态投给 owning client；本地 authority 没有网络回环时直接写本机读模型，远端玩家继续走可靠 RPC。 */
	void DeliverCampCommandResultToOwningClient(const FCatDomainCommandResult& Result);
	/** 把直接吃鱼终态投给 owning client；本地 authority 没有网络回环时直接写本机读模型，远端玩家继续走可靠 RPC。 */
	void DeliverFishConsumeResultToOwningClient(const FCatFishConsumeResult& Result);

	/** owning client 只提交疾跑开关；最终速度始终取服务器 PlayerController 类默认配置。 */
	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	/** 实际接收 AddMappingContext 的本地输入子系统；只用于成对 Remove。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> AppliedInputSubsystem;

	/** 实际安装的 Context；与蓝图配置分开记录以支持安全清理。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AppliedMappingContext;

	/** 当前 Controller 的疾跑按键意图；客户端和 authority 分别维护，不作为远端动画事实复制。 */
	UPROPERTY(Transient)
	bool bSprintRequested = false;

	/** 当前 Controller 的 Ability 输入绑定子对象；它拥有 ASC 输入路由状态，Controller 只把 Pawn/输入生命周期转交给它。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatAbilityInputBindingComponent> AbilityInputBindingComponent;

	/** 当前 Controller 的 Camp BodyAction 命令子对象；它只创建营地动作载荷，不接触库存、修竿或 Wet 反馈。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|BodyAction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatCampBodyActionCommandComponent> CampBodyActionCommandComponent;

	/** 当前 Controller 的 Social BodyAction 命令子对象；它只创建求助、恶作剧和保护牌载荷，不接触营地、库存事务或 Wet 反馈。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|BodyAction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatSocialBodyActionCommandComponent> SocialBodyActionCommandComponent;

	/** NativeInputActions 已绑定的输入组件；只防止交互这类非 Ability 标签在 SetupInputComponent 重入时重复注册。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> NativeInputBoundComponent;

	/** 统一向 authority GameMode 查询运行内玩法命令 gate；缺少 GameMode、非 Active 或 teardown 关门时返回 false。 */
	bool CanForwardGameplayCommand() const;
	/** 查询 Fishing/玩家打窝专用 gate；它复用身份与 teardown 判断，但额外要求 Run 处于 DayActive、允许钓鱼且当前猫没有倒地。 */
	bool CanForwardFishingCommand() const;

	/** owning client 最近收到的 Social 协议读模型；由可靠结果 RPC 整体替换，不复制回服务器或作为权限/身体事实。 */
	UPROPERTY(Transient)
	FCatTheftResult LastTheftResult;

	/** owning client 最近收到的献祭协议读模型，表示服务器协调器最后回送的完整阶段结果；可靠 Client RPC 整体写入，UI 只读且不会影响 Items 或 Run。 */
	UPROPERTY(Transient)
	FCatSacrificeResult LastSacrificeResult;

	/** owning client 最近收到的公共领域命令读模型；可靠 Client RPC 整体写入，UI 只读且不会触发第二次领域操作。 */
	UPROPERTY(Transient)
	FCatDomainCommandResult LastCampCommandResult;

	/** owning client 最近收到的直接吃鱼读模型，表示鱼实例移除和身体效果提交后的终态；可靠 Client RPC 整体写入，UI 只读且不会应用效果。 */
	UPROPERTY(Transient)
	FCatFishConsumeResult LastFishConsumeResult;

	/** 当前 Controller 创建的钓鱼命令组件；它承接玩家钓鱼输入并把正式事务继续交给领域服务。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Catfishing|Fishing", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCatFishingCommandComponent> FishingCommandComponent;

	/** 当前 Controller 创建的准星交互扫描组件；它只负责发现可交互 Actor，具体交互由目标对象自己的接口执行。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Catfishing|Interaction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCatInteractionTargetingComponent> InteractionTargetingComponent;
};
