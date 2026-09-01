#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Items/CatItemTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatBodyActionAbility.generated.h"

class ACatCampHubActor;
class ACatCharacter;
class APlayerState;

/** 非 Fishing 的猫身体动作类型；RPC 只选择动作，真正进入 Gameplay Ability 后再调用领域事务。 */
UENUM(BlueprintType)
enum class ECatBodyActionAbilityCommand : uint8
{
	/** 未设置动作；Ability 收到这种载荷会直接取消，不进入领域写口。 */
	Unknown,
	/** 祭坛献祭动作；Ability 只表示猫在祭坛提交意图，鱼和额度事务仍由 SacrificeCoordinator 裁决。 */
	RequestSacrifice,
	/** 固定营地休息动作；Ability 只承接身体动作生命周期，恢复规则仍由 Camp/Condition 裁决。 */
	CampRest,
	/** 营地篝火回看动作；Ability 只承接互动意图，全员表现与 CapturePlan 仍由 Camp 收口。 */
	CampfirePlayback,
	/** 容器之间移动物体；Ability 不改容器，转移仍由服务器重读源/目标后交给 Items 原子提交。 */
	TransferObjectBetweenContainers,
	/** 把倒地伙伴搬运回固定营地；Ability 表达搬运动作，落点和恢复仍由 Camp/Condition 裁决。 */
	RescueCharacterToCamp,
	/** 在营地修复当前鱼竿；Ability 表达修理动作，耗材和耐久事务仍由 Equipment 裁决。 */
	RepairRodAtCamp,
	/** 使用草药救援目标角色；Ability 表达施药动作，耗材扣除与身体恢复仍由 Equipment/Condition 裁决。 */
	UseHerbOnCharacter,
	/** 直接吃掉一条鱼；Ability 表达进食动作，鱼实例消费和身体/成长效果仍由 Items/Condition/Growth 裁决。 */
	ConsumeFish,
	/** 开始偷取一条鱼；Ability 表达偷取动作，权限、escrow 和追回窗口仍由 Social/Items 裁决。 */
	BeginTheft,
	/** 在追回窗口内抓回被偷的鱼；Ability 表达追回动作，真实协议状态仍由 Social 裁决。 */
	CatchTheft,
	/** 手动发布钓鱼或倒地求助；Ability 表达发声动作，信号范围和冷却仍由 Social 裁决。 */
	RequestManualHelp,
	/** 对目标玩家请求普通恶作剧许可；Ability 表达动作意图，冷却和保护牌仍由 Social 裁决。 */
	RequestMischief,
	/** 放置或移动防骚扰保护牌；Ability 表达放牌动作，唯一 Actor 和范围仍由 Social 裁决。 */
	PlaceProtectionSign,
	/** 完成抖水表现后清除 Wet；Ability 表达身体表现结束，状态写入仍由 Condition 裁决。 */
	CompleteShakeDry
};

/**
 * Body Action Ability 的一次性事件载荷；由 PlayerController 在服务器 RPC 中创建并同步投给当前 Character ASC。
 * 它只保存原 RPC 参数，不保存事务结果，也不成为 Items、Social、Camp、Condition 或 Equipment 的第二份真相。
 */
UCLASS()
class CATFISHING_API UCatBodyActionPayload : public UObject
{
	GENERATED_BODY()

public:
	/** 载荷代表的身体动作语义；Ability 用它选择内部提交路径，未知值会 fail-closed。 */
	UPROPERTY(Transient)
	ECatBodyActionAbilityCommand Command = ECatBodyActionAbilityCommand::Unknown;

	/** 激活本次 Ability 的事件标签；由 Command 派生，ASC 用它触发同一个正式 Body Action Ability。 */
	UPROPERTY(Transient)
	FGameplayTag EventTag;

	/** 本次营地相关动作引用的固定营地 Actor；只作为服务器重读位置和 Camp 规则的入口。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatCampHubActor> Camp;

	/** 本次救援、施药或进食动作引用的目标 Character；领域层仍会重读 World、状态和距离。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatCharacter> TargetCharacter;

	/** 本次恶作剧动作引用的目标 PlayerState；Social 会从当前 World 反查真实 Controller。 */
	UPROPERTY(Transient)
	TObjectPtr<APlayerState> TargetPlayerState;

	/** 献祭动作的原始命令；Ability 不解释其中 Revision，仍交给 SacrificeCoordinator 处理。 */
	UPROPERTY(Transient)
	FCatSacrificeCommand SacrificeCommand;

	/** 直接吃鱼动作的原始命令；服务器身份会在领域提交前覆盖，客户端不能写 StableNetId。 */
	UPROPERTY(Transient)
	FCatFishConsumeCommand FishConsumeCommand;

	/** 偷鱼开始动作的原始命令；服务器身份和协议窗口仍由 Social 覆盖与创建。 */
	UPROPERTY(Transient)
	FCatTheftCommand TheftCommand;

	/** 通用 RequestId；没有独立命令结构的营地、求助、施药、修理和表现动作使用它做幂等关联。 */
	UPROPERTY(Transient)
	FGuid RequestId;

	/** 本次容器移动要处理的物体类别；服务器领域层会按类别选择正式策略并继续验证源容器事实。 */
	UPROPERTY(Transient)
	ECatContainedObjectKind ContainerObjectKind = ECatContainedObjectKind::Unknown;

	/** 本次容器移动要处理的物体实例 ID；服务器领域层会继续验证它是否仍在源容器。 */
	UPROPERTY(Transient)
	FGuid ContainerObjectInstanceId;

	/** 本次鱼领域动作要处理的鱼实例 ID；偷鱼、吃鱼等鱼专属入口继续使用它，不参与通用容器拖拽。 */
	UPROPERTY(Transient)
	FGuid FishInstanceId;

	/** 偷鱼追回协议 ID；只由 BeginTheft 的服务器结果产生，客户端不能用 RequestId 替代它。 */
	UPROPERTY(Transient)
	FGuid TheftProtocolId;

	/** 本次转移是否使用显式源/目标容器引用；false 表示旧载荷或错误载荷，Controller 会 fail-closed。 */
	UPROPERTY(Transient)
	bool bUsesExplicitContainerTransfer = false;

	/** 通用跨容器转移的源容器 ID；服务器会从 Items 重读种类、宿主和当前可达性，不信任客户端授权。 */
	UPROPERTY(Transient)
	FGuid SourceContainerId;

	/** 通用容器转移的源容器内格子下标；服务器会确认该格当前仍放着本次拖拽的物体。 */
	UPROPERTY(Transient)
	int32 SourceContainerSlotIndex = INDEX_NONE;

	/** 通用跨容器转移的目标容器 ID；它只表达 Drop 目标容器，最终容量和权限由 Items 裁决。 */
	UPROPERTY(Transient)
	FGuid TargetContainerId;

	/** 通用容器转移的目标容器内格子下标；同容器整理和跨容器移动都按这个槽位写入或交换。 */
	UPROPERTY(Transient)
	int32 TargetContainerSlotIndex = INDEX_NONE;

	/** 通用跨容器转移的源容器种类；服务器会与 Items 注册种类比较，不匹配时拒绝。 */
	UPROPERTY(Transient)
	ECatContainerKind SourceContainerKind = ECatContainerKind::Unknown;

	/** 通用跨容器转移的目标容器种类；服务器会与 Items 注册种类比较，不匹配时拒绝。 */
	UPROPERTY(Transient)
	ECatContainerKind TargetContainerKind = ECatContainerKind::Unknown;

	/** 通用跨容器转移在 Drop 时看到的源容器 Revision；Items 用它防止源对象已被其他事务改变。 */
	UPROPERTY(Transient)
	int64 ExpectedSourceContainerRevision = 0;

	/** 通用跨容器转移在 Drop 时看到的目标容器 Revision；Items 与源版本一起比较，防止目标容量或内容已变化。 */
	UPROPERTY(Transient)
	int64 ExpectedTargetContainerRevision = 0;

	/** 调用方观察到的装备 Revision；修竿和草药消费用它防止客户端基于旧装备快照提交。 */
	UPROPERTY(Transient)
	int64 ExpectedEquipmentRevision = 0;

	/** 草药物品实例 ID；Ability 用它反查定义并提交 Equipment Use，避免同类多堆草药被误扣。 */
	UPROPERTY(Transient)
	FGuid HerbItemInstanceId;

	/** 手动求助类型；Social 拒绝 Unknown，并且不会让普通请求伪装成 Giant 系统提示。 */
	UPROPERTY(Transient)
	ECatHelpSignalKind HelpKind = ECatHelpSignalKind::Unknown;

	/** 恶作剧交互位置；Social 用服务器当前配置和目标事实重新裁决距离与保护牌。 */
	UPROPERTY(Transient)
	FVector InteractionLocation = FVector::ZeroVector;

	/** 保护牌期望位置；Social 会按本人当前位置和配置半径重新验证。 */
	UPROPERTY(Transient)
	FVector SignLocation = FVector::ZeroVector;

	/** 根据 Command 填写 EventTag；返回 false 表示动作未知，RPC 应给调用方一个结构化拒绝或静默关闭。 */
	bool InitializeForCommand(ECatBodyActionAbilityCommand InCommand);

	/** 判断载荷是否足以触发 Ability；这里只检查动作和事件标签，具体权限与参数合法性留给领域层。 */
	bool IsRuntimeValid() const;

	/** 返回某类动作对应的 Gameplay Event 标签；PlayerController 和 Ability 使用同一张表，避免标签漂移。 */
	static FGameplayTag GetEventTagForCommand(ECatBodyActionAbilityCommand InCommand);
};

/** 正式非 Fishing 身体动作网关；用 GameplayEvent 承接多类可取消动作，再回到服务器领域事务。 */
UCLASS()
class CATFISHING_API UCatGA_BodyActionCommand : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 配置 ServerOnly、按 Actor 实例化和 BodyAction 事件触发器；授予后即可接收 RPC 转来的服务器动作事件。 */
	UCatGA_BodyActionCommand();

	/** 收到 BodyAction GameplayEvent 后读取载荷并进入提交窗口；窗口结束前被取消时不会触发领域事务。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** 结束 BodyAction Ability 时清理暂存载荷；正常提交、无效载荷取消和外部取消都会走同一条收尾路径。 */
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化测试只读观察默认提交窗口；正式玩法通过配置和 Ability 生命周期生效，不从测试口驱动行为。 */
	float GetBodyActionCommitWindowSecondsForAutomation() const { return BodyActionCommitWindowSeconds; }

	/** 自动化测试只读观察某个动作的实际前摇；动作级表现配置优先，缺失时回退默认提交窗口。 */
	float GetBodyActionLeadInSecondsForAutomation(ECatBodyActionAbilityCommand Command) const;

	/** 自动化测试只读观察某个动作的表现事件标签；用于证明 BodyAction 表现不会借用 Fishing 本地预测通道。 */
	FGameplayTag GetBodyActionPresentationEventTagForAutomation(ECatBodyActionAbilityCommand Command) const;

	/** 自动化测试临时覆盖提交窗口；只影响当前测试进程，让无画面测试能稳定走完正式 BodyAction 提交流程。 */
	void SetBodyActionCommitWindowSecondsForAutomation(float InSeconds);

	/** 自动化测试清除进程级提交窗口覆盖；清除后 Ability 重新读取动作级表现设置和默认窗口。 */
	void ClearBodyActionCommitWindowOverrideForAutomation();

	/** 自动化测试只读观察是否存在进程级提交窗口覆盖；用于防止一个测试污染后续动作级设置验证。 */
	bool HasBodyActionCommitWindowOverrideForAutomation() const;
#endif

private:
	/** 等待窗口结束后的提交回调；只有 Ability 仍处于活跃状态时，才会把暂存载荷交给 Controller 领域入口。 */
	UFUNCTION()
	void CommitBodyActionAfterWindow();

	/**
	 * BodyAction 的服务器提交窗口秒数。
	 * Ability 激活时写入 WaitDelay，Fishing Cancel 的服务器命令可以在这段时间取消它；正式动画资产接入后可按动作前摇调节。
	 */
	UPROPERTY(EditDefaultsOnly, Category="Catfishing|BodyAction", meta=(ClampMin="0.0", UIMin="0.0"))
	float BodyActionCommitWindowSeconds = 0.15f;

	/** 当前等待提交的只读 BodyAction 载荷；ActivateAbility 写入，提交或取消时清空，避免取消后仍触发领域事务。 */
	UPROPERTY(Transient)
	TObjectPtr<const UCatBodyActionPayload> ActivePayload;

	/** 当前等待提交动作的表现标签；ActivateAbility 从表现配置冻结，取消时用同一标签通知蓝图清理前摇表现。 */
	UPROPERTY(Transient)
	FGameplayTag ActivePresentationEventTag;
};
