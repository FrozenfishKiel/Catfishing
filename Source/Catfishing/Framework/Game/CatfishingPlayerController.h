#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Fishing/CatFishingTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Framework/Core/CatRunContracts.h"
#include "Growth/CatGrowthTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Inventory/CatInventoryStatics.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatfishingPlayerController.generated.h"

class UCatAbilityInputBindingComponent;
class UCatCampBodyActionCommandComponent;
class UCatFishingCommandComponent;
class UCatInteractionTargetingComponent;
class UCatSocialBodyActionCommandComponent;
class UCharacterMovementComponent;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class ACatCampHubActor;
class ACatCampInventoryActor;
class ACatCharacter;
class ACatfishingGameState;
class ACatShopKioskActor;
class ACatFishBuyerActor;
class ACatFishGuardActor;
class UCatBackPackComponent;
class UCatInventoryItemInstance;
struct FInputActionValue;

/** owning client 收到公共领域命令结果后的本机通知；UI Model 只用它关联 RequestId，不重新执行领域动作。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCampCommandResultReceived, const FCatDomainCommandResult&);
/** 本机物品栏焦点变化通知；只有独立物品栏订阅，背包列表不包含也不消费选择状态。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatQuickbarSelectionChanged, int32);

/** Lake owning-client 的网络适配器；将 Run、Fishing、Camp、Inventory、Condition、Shop、Social 意图转给 authority，并承接 Profile Grant/CapturePlan/HostExit 回执，不自存领域真相。 */
UCLASS()
class CATFISHING_API ACatfishingPlayerController : public APlayerController
{
	GENERATED_BODY()
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class FCatPhysicalInputRouteTest;
public:
	/** 本人提交物品操作意图；服务器按宿主、槽位和实例身份重读物品，不接受客户端定义或效果。 */
	UFUNCTION(Server, Reliable)
	void ServerExecuteInventoryAction(FGuid RequestId, AActor* SourceInventoryHost, int32 SourceSlotIndex,
		FGuid ItemInstanceId, FGameplayTag Action, int32 Quantity);
	/** 本机物品栏输入选择一个实际库存槽位；只写 Controller 的物品栏焦点，背包窗口不能触发它。 */
	bool RequestSelectQuickbarSlotFromInput(int32 RequestedSlotIndex);
	/** 读取物品栏当前焦点；只读背包容量来排除无效槽位，不在背包组件或库存 Model 中保存选择。 */
	int32 GetSelectedQuickbarSlotIndex() const;
	/** 本机物品栏焦点已变化；独立 View 刷新外圈，普通背包窗口不订阅此通知。 */
	FCatQuickbarSelectionChanged OnQuickbarSelectionChanged;
	/** owning client 按实际背包格数循环选择；Direction 小于零向前、大于零向后，空格同样属于可选目标。 */
	bool RequestCycleQuickbarSlotFromInput(int32 Direction);
	/** G 的按下入口；只在当前本地选择槽位已解析实例时提交统一 Use，请求不会猜测或替换槽位。 */
	void BeginSelectedItemUseFromInput();
	/** G 松开时结束同一持续使用请求；普通瞬时物品没有活动请求时无副作用，取消和旅行也复用它清理。 */
	void EndSelectedItemUseFromInput(bool bCancelled);
	/** 查询当前输入是否可以使用一个本地选中且有有效实例的背包槽位；不做服务器权限裁决。 */
	bool CanUseSelectedBackpackItemFromInput() const;
	/** 服务器复核当前本人背包槽位和实例身份后执行统一 Use；客户端本地选中不作为服务器状态或权限依据。 */
	UFUNCTION(Server, Reliable)
	void ServerUseSelectedBackpackItem(FGuid RequestId, int32 ExpectedSelectedSlot, FGuid ItemInstanceId);
	/** 服务器结束同一个持续使用实例；只接受 Begin 已记录的 RequestId 和实例身份，换格后也不会把结束事件投给新物品。 */
	UFUNCTION(Server, Reliable)
	void ServerEndSelectedBackpackItem(FGuid RequestId, FGuid ItemInstanceId, bool bCancelled);
	/** Ordinary/sprint speed from this controller and the pawn class, in cm/s. Used by CMC flags. */
	float GetConfiguredMovementSpeed(const APawn* TargetPawn, bool bSprinting) const;
	/** 每帧先对齐公开翻天快照与锁，再交给引擎处理输入；同时覆盖 GameState 晚到与复制延迟。 */
	virtual void Tick(float DeltaSeconds) override;
	/** 查询当前公开翻天锁，供本地 UI 和 Ability 输入拒绝操作；客户端超时不能改变服务器的 active 事实。 */
	bool IsDayTransitionInputBlocked() const;
	/** 旅行开始前解除本功能绑定、表现和锁；同一旧 World 后续 Tick 不得重新加锁。 */
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;
	/** 控制器接管 Pawn 后只做宿主级收口：重置本地钓鱼输入和疾跑状态；Ability ASC 路由统一由 SetPawn 写入点刷新。 */
	virtual void OnPossess(APawn* InPawn) override;
	/** owning client 收到 Pawn 复制变化后重置本地输入与疾跑状态；复制链中的 SetPawn 负责切换 Ability ASC 路由。 */
	virtual void OnRep_Pawn() override;
	/** 捕获统一 Pawn 写入点；先撤销旧身体持续输入并归还翻天锁，再刷新 Ability 路由与本地 UI，最后按公开快照接管新身体。 */
	virtual void SetPawn(APawn* InPawn) override;
	/** 每帧旋转收尾先保留父类视角处理，再将鱼竿瞄准或普通视角写成物理身体的 view intent；不直接接管身体朝向。 */
	virtual void UpdateRotation(float DeltaTime) override;
	/** 菜单、失焦和 Pawn 切换只停止自主输入，外部拉力与已有物理速度继续生效。 */
	void ClearPhysicalControlInput(FName Reason);
	/** 视口失焦或输入层移除时先撤销持续物理输入，再交给父类清空按键记录，避免旧按住意图恢复。 */
	virtual void FlushPressedKeys() override;
	virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override { return true; }
	/** 结算夜请求检查本局成像终态与 Grant ACK；只有归档已收口才向 Run StateTree 发送 SettlementComplete。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestSettlementCompletion(FGuid RequestId, int64 ExpectedRevision);

	/** 服务器向 owning client 投递不可变永久 Grant；客户端只有 durable Profile 完成后才发 ACK。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveProfileGrant(const FCatProfileGrant& Grant);

	/** 客户端在本地 Journal 完整落盘后确认 GrantId；服务器重建身份，并在真实 ACK 后复核 Host exit 有界等待。 */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeProfileGrant(FGuid GrantId);

	/** 服务器向 owning client 投递独立 CapturePlan；本 RPC 不表示图片或 Grant 已成功。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveImprintCapturePlan(const FCatCapturePlan& Plan);

	/** 外部本地成像桥回报真实结果；成功必须携带 durable ImprintId，服务器随后才生成 Grant。 */
	UFUNCTION(Server, Reliable)
	void ServerReportImprintCaptureResult(FGuid CapturePlanId, bool bSucceeded, FGuid ImprintId);

	/** 建立项目玩家控制器的输入、交互和命令组件宿主；具体输入绑定会在 BeginPlay/SetupInputComponent 阶段安装。 */
	ACatfishingPlayerController();

	/** 返回本 Controller 持有的钓鱼命令组件；调用方只能通过它提交钓鱼意图，不把组件当库存或 Run 状态来源。 */
	UFUNCTION(BlueprintPure, Category="Catfishing|Fishing")
	UCatFishingCommandComponent* GetFishingCommandComponent() const;

	/** 返回本 Controller 持有的交互扫描组件；表现层可读取当前命中目标，真正交互仍走目标 Actor 的接口。 */
	UFUNCTION(BlueprintPure, Category="Catfishing|Interaction")
	UCatInteractionTargetingComponent* GetInteractionTargetingComponent() const { return InteractionTargetingComponent; }

	/** 翻天或引擎移动忽略时清掉自愿移动意图；其他时候按可见朝向把二维输入转成 Pawn 移动，持竿时读取鱼竿相机方向。 */
	void Move(const FInputActionValue& Value);
	/** 请求当前 Character 跳跃；翻天、引擎移动忽略或持竿操作时拒绝，并清掉保持态，避免过渡和搏斗姿态被起跳打断。 */
	void StartJump();

	/** 权威交互转发；服务器检查玩法 gate 和通用接口后，在目标 Actor 上重新调用同一 Interact 虚函数。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestInteraction(AActor* Target, FGuid RequestId);

	/** 客户端只提交本人意图；false 对发起者表示取消整轮，对其他人表示撤回。服务器按 RPC 所属 Controller 识别身份并复核。 */
	UFUNCTION(Server, Reliable)
	void ServerSetAltarConfirmation(FGuid RequestId, bool bConfirmed);

	/** 把 F8/F9 或已获键盘焦点页面转交的按键转换为当前公开请求的一次确认意图；找到可提交请求时返回 true 供 UI 消费该键。 */
	bool TrySetAltarConfirmationFromKey(const FKey& Key);

	/** 由 owning client 发起固定营地篝火回看请求；Camp 在结算夜全员在场且 CapturePlan 建立成功后触发表现 multicast，并通过 ClientReceiveCampCommandResult 回送领域结果。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestCampfirePlayback(ACatCampHubActor* Camp, FGuid RequestId);

	/** 服务器把公共领域命令结果可靠发给 owning client；所有路径都保留原 RequestId，客户端不重算 Revision 或领域错误。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveCampCommandResult(const FCatDomainCommandResult& Result);

	/** 在两个正式库存宿主之间移动、合并或交换格子；服务器重读 Actor 和槽位后统一裁决背包整理、营地拖放与拿鱼。
	 *  这条就是「拿鱼」机制本身，不是绕过谁的旁路（2026-09-11 裁决②）：机制层只做客观的拿，不问动机也不问归属，
	 *  规则是够得着、鱼护在地面、一嘴一条，三条分别由库存触达规则、鱼护落地状态与猫嘴单占用执行。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Inventory")
	void ServerMoveInventoryItemBetweenHosts(FGuid RequestId, AActor* SourceInventoryHost,
		int32 SourceSlotIndex, AActor* TargetInventoryHost, int32 TargetSlotIndex);

	/** 返回本机最近收到的公共领域命令结果供表现层关联请求；该缓存不作为 Camp、Inventory、Equipment 或 Condition 的权限事实。 */
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

	/** 从指定商店摊位支付整车商品项；服务器先限制购物车载荷，再复核摊位和营地公共仓库。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Shop")
	void ServerSubmitShopCartAtKiosk(ACatShopKioskActor* ShopKiosk,
		const TArray<FCatShopCartLineCommand>& Lines, FGuid RequestId);

	/** 向明确买家出售当前打开鱼护中的鱼实例；不接收客户端价格，整单结果可靠回送给 owning client。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Shop")
	void ServerSellFishBatch(FGuid RequestId, ACatFishBuyerActor* Buyer, ACatFishGuardActor* Guard,
		const TArray<FGuid>& FishInstanceIds);

	/** 长按交互提交拾起鱼护意图；鱼护自己复核距离、身体、嘴部空闲与库存容量。 */
	UFUNCTION(Server, Reliable)
	void ServerPickUpFishGuard(ACatFishGuardActor* Guard, FGuid RequestId);

	/** 请求服务器释放当前嘴叼的原Actor；服务器自行查携带对象，空嘴无副作用。 */
	UFUNCTION(Server, Reliable)
	void ServerDropCarriedItem();

	/** 从当前这组三选一里选中一项；OfferSerial 用来拒绝过期面板，服务器只认自己发出的那一组。 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Growth")
	void ServerChooseGrowthOption(FGuid RequestId, ECatGrowthOptionId OptionId, int32 OfferSerial);

	/** 手动发布普通钓鱼或倒地求助；普通信号不会升级为全局任务。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestManualHelp(FGuid RequestId, ECatHelpSignalKind Kind);

	/** 请求一次普通恶作剧许可；Social 重新验证目标 Controller、冷却与 ProtectionSign。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestMischief(APlayerState* TargetPlayerState, FGuid RequestId, FVector InteractionLocation);

	/** 在本人附近放置或移动唯一防骚扰牌子；Social 用显式范围配置保护普通恶作剧。 */
	UFUNCTION(Server, Reliable)
	void ServerPlaceProtectionSign(FGuid RequestId, FVector SignLocation);

	/**
	 * 房主把某人踢出本局（联机社交 §3.1.1、§4 软性：一切社交僵局的兜底，被踢者跟人走的资产无损）。
	 *
	 * 房主资格、目标有效性与清理全部由 authority 的 UCatRoomOwnerService 裁决，本 RPC 只做网络适配；
	 * 结果沿用公共领域回执通道（ClientReceiveCampCommandResult）回送给发起者。
	 * UI 入口（房间页那颗按钮）不在这里：C++ 侧只保证「谁能按、按下去发生什么」。
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Catfishing|Room")
	void ServerKickPlayer(APlayerState* TargetPlayerState, FGuid RequestId);

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
	/** Pawn 断开前先清理当前 ASC 的 Ability 输入状态、钓鱼本地命令和疾跑意图，再交还父类结束占有，避免状态泄漏到下一次占有。 */
	virtual void OnUnPossess() override;
	/** EndPlay 清理翻天锁及订阅、物理与 Ability 路由和 Native 输入记录，只撤销本 Controller 安装的输入层。 */
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

	/** 按住疾跑键时物理电机的目标地面速度上限。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input|Movement",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s"))
	float SprintMaxSpeed = 350.0f;

	/** 本 Controller 的输入层优先级；不影响其他系统已经安装的 Mapping Context。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catfishing|Input")
	int32 InputMappingPriority = 0;

private:
	friend class UCatFishingCommandComponent;

	/** 消费当前 GameState 快照；绑定变化通知，调和本地输入与服务器移动锁，再刷新 LocalPlayer 的时间轴表现。 */
	void ReconcileDayTransition();
	/** 成对申请或释放翻天专属锁；首次加锁撤销持续物理输入，重复状态不叠加计数，服务器只恢复本功能接管的原移动模式。 */
	void SetDayTransitionLocked(bool bLocked);
	/** 结束或旅行时解绑快照并清理本功能持有的输入、移动和 UI；不触碰 Run 权威状态。 */
	void ClearDayTransition();
	/** 游戏视口直接收到 F8 时复用统一确认提交入口，避免没有模态页面时缺少快捷键。 */
	void ConfirmAltarConfirmationFromInput();
	/** 游戏视口收到 F9 时提交 false；服务器裁决发起者取消整轮或其他人撤回本人，重复按键不会恢复旧请求。 */
	void RevokeAltarConfirmationFromInput();

	/** 当前快照通知来源；调和时写入，清理时配对解绑，不强持有旧 World 的 GameState。 */
	TWeakObjectPtr<ACatfishingGameState> DayTransitionGameState;
	/** 快照订阅的配对句柄；调和创建，换 GameState 或清理时移除。 */
	FDelegateHandle DayTransitionStateHandle;
	/** 正在离开的旧 World；PreClientTravel 写入，调和读取并拒绝旧世界重新加锁，新世界自然失效。 */
	TWeakObjectPtr<UWorld> DayTransitionTravelWorld;
	/** 本功能是否已经申请一层移动和视角忽略计数；锁方法独占写入，清理只按此记录归还一层。 */
	bool bDayTransitionLocked = false;
	/** 翻天专属高优先级输入组件；加锁创建并压栈，解锁只弹出和销毁此组件，保留其他输入层。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> DayTransitionInputBlocker;
	/** 服务器本轮禁用过的移动组件；调和记录所属 Pawn，解锁或换 Pawn 时恢复并清空弱引用。 */
	TWeakObjectPtr<UCharacterMovementComponent> DayTransitionMovement;
	/** 服务器禁用前的移动模式；首次接管写入，释放时读取，不由客户端根据动画时间恢复。 */
	uint8 DayTransitionSavedMovementMode = 0;
	/** 服务器禁用前自定义移动模式的子编号；与普通模式一起保存和恢复，避免丢失自定义移动状态。 */
	uint8 DayTransitionSavedCustomMode = 0;

	/** 幂等安装当前配置的玩法 Mapping Context；BeginPlay/输入初始化均可安全调用。 */
	void ApplyInputMappingContext();
	/** 移除本 Controller 安装的玩法 Mapping Context，并清空弱绑定记录。 */
	void RemoveInputMappingContext();
	/** 翻天期间拒绝视角操作；其他时候把二维输入写入 Controller 的 Yaw/Pitch。 */
	void Look(const FInputActionValue& Value);
	/** 把当前猫身体的自愿移动意图归零；只处理输入持续态，不抵消外力、速度或抓握牵引。 */
	void StopMove();
	/** 对当前已占有的 Character 停止跳跃。 */
	void StopJump();
	/** 本地 Started 输入在非翻天状态开启疾跑，并把布尔意图可靠同步给 authority。 */
	void StartSprint();
	/** 本地 Completed/Canceled 输入关闭疾跑，并把布尔意图可靠同步给 authority。 */
	void StopSprint();
	/** 更新本 Controller 的疾跑意图并应用速度；仅本地输入路径需要向服务器转发。 */
	void SetSprintRequested(bool bNewSprintRequested, bool bNotifyServer);
	/** 把服务器配置的普通/疾跑速度应用到指定 Character；非 Character Pawn 安全跳过。 */
	void ApplySprintSpeed(APawn* TargetPawn, bool bSprinting) const;
	/** 将实际杆朝向或自由视角提交为物理电机意图，不直接写入身体旋转。 */
	void RefreshPhysicalViewIntent();
	/** 项目原生输入标签入口；翻天期间拒绝交互，其他时候处理非 Ability 动作，未知标签无副作用。 */
	void NativeInputTagPressed(FGameplayTag InputTag);
	/** 交互键正常松开时完成鱼护短按；其他原生输入不消费该边沿。 */
	void NativeInputTagReleased(FGameplayTag InputTag);
	/** 输入被取消时清理长按候选，不把失焦或输入层移除当作短按。 */
	void NativeInputTagCanceled(FGameplayTag InputTag);
	/** 从当前 Pawn 解析正式个人背包；只接受 Character 构造的 BackPack，不把鱼护、鱼缸或商店库存误当快捷栏来源。 */
	UCatBackPackComponent* GetControlledBackPack() const;
	/** 结束本机和服务器同一持续 Use 事务的记录；换 Pawn、旅行和按键取消调用它，选择变更不得调用它。 */
	void ClearSelectedItemUseInput(bool bCancelled);
	/** 当 Pawn 或输入组件在 owning client 就绪时通知 LocalPlayer UI；服务器远端 Controller 和非 Cat UI World 安全跳过。 */
	void NotifyLocalPlayerUISubsystemPawnChanged();
	/** 把公共领域命令终态投给 owning client；本地 authority 没有网络回环时直接写本机读模型，远端玩家继续走可靠 RPC。 */
	void DeliverCampCommandResultToOwningClient(const FCatDomainCommandResult& Result);

	/** 接收 owning client 的疾跑开关；翻天期间只接受关闭，最终速度取服务器 Controller 类默认配置。 */
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

	/** 当前 Controller 的 Camp BodyAction 命令子对象；它只创建营地动作载荷，不接触库存或 Wet 反馈。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|BodyAction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatCampBodyActionCommandComponent> CampBodyActionCommandComponent;

	/** 当前 Controller 的 Social BodyAction 命令子对象；它只创建求助、恶作剧和保护牌载荷，不接触营地、库存事务或 Wet 反馈。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|BodyAction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatSocialBodyActionCommandComponent> SocialBodyActionCommandComponent;

	/** NativeInputActions 已绑定的输入组件；只防止交互这类非 Ability 标签在 SetupInputComponent 重入时重复注册。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> NativeInputBoundComponent;

	/** 本机持续使用事务的请求 ID；G 开始后只用它匹配松开或取消，避免选择变化改写结束目标。 */
	FGuid ActiveSelectedItemUseRequestId;
	/** 独立物品栏的本地焦点，初始第一格；仅本机输入修改，换 Pawn 重置，不复制、不持久化、不写入背包 Model。 */
	int32 SelectedQuickbarSlotIndex = 0;
	/** 本机持续使用事务锁定的实例身份；服务器和客户端都据此拒绝把结束输入转给另一个同定义物品。 */
	FGuid ActiveSelectedItemUseItemId;
	/** 本机持续使用事务开始时的背包槽位；它与固定实例共同提供诊断上下文，结束不会改读之后的新选中格。 */
	int32 ActiveSelectedItemUseSlotIndex = INDEX_NONE;
	/** 当前持续使用所属的正式背包；Begin 成功时冻结，换格后 End 仍只回到这份原背包而非新焦点。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatBackPackComponent> ActiveSelectedItemUseBackPack = nullptr;
	/** 当前持续使用的原始实例；服务器用强引用保留它直到 End/Cancel，即使槽位已换物也能取消原来的持续效果。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryItemInstance> ActiveSelectedItemUseInstance = nullptr;

	/** 已安装 F8/F9 绑定的输入组件；输入组件重建后重新绑定一次，避免 SetupInputComponent 重入累积同一确认请求。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInputComponent> AltarConfirmationInputBoundComponent;

	/** 统一向 authority GameMode 查询运行内玩法命令 gate；缺少 GameMode、非 Active 或 teardown 关门时返回 false。 */
	bool CanForwardGameplayCommand() const;
	/** 查询 Fishing/玩家打窝专用 gate；它复用身份与 teardown 判断，但额外要求 Run 处于 DayActive、允许钓鱼且当前猫没有倒地。 */
	bool CanForwardFishingCommand() const;

	/** owning client 最近收到的公共领域命令读模型；可靠 Client RPC 整体写入，UI 只读且不会触发第二次领域操作。 */
	UPROPERTY(Transient)
	FCatDomainCommandResult LastCampCommandResult;



	/** 当前 Controller 创建的钓鱼命令组件；它承接玩家钓鱼输入并把正式事务继续交给领域服务。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Catfishing|Fishing", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCatFishingCommandComponent> FishingCommandComponent;

	/** 当前 Controller 创建的准星交互扫描组件；它只负责发现可交互 Actor，具体交互由目标对象自己的接口执行。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Catfishing|Interaction", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCatInteractionTargetingComponent> InteractionTargetingComponent;
};
