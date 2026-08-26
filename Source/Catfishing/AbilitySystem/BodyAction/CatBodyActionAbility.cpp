#include "AbilitySystem/BodyAction/CatBodyActionAbility.h"

#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/Pawn.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化进程内的 BodyAction 提交窗口覆盖；负值表示未设置，所有实例继续读取 Ability 默认属性。 */
	float GBodyActionCommitWindowOverrideSeconds = -1.0f;
#endif

	/** 向 BodyAction Ability 注册一个 GameplayEvent 触发器；同一个 Ability 用事件标签区分具体动作。 */
	void AddBodyActionTrigger(TArray<FAbilityTriggerData>& AbilityTriggers, const FGameplayTag EventTag)
	{
		if (!EventTag.IsValid())
		{
			return;
		}
		FAbilityTriggerData Trigger;
		Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		Trigger.TriggerTag = EventTag;
		AbilityTriggers.Add(Trigger);
	}

	/** 从 AbilityActorInfo 解析当前 owning Controller；PlayerController 缺失时回退到 Avatar Pawn 的 Controller。 */
	ACatfishingPlayerController* ResolveBodyActionController(const FGameplayAbilityActorInfo* ActorInfo)
	{
		ACatfishingPlayerController* Controller = ActorInfo
			? Cast<ACatfishingPlayerController>(ActorInfo->PlayerController.Get()) : nullptr;
		if (!Controller && ActorInfo)
		{
			const APawn* AvatarPawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
			Controller = AvatarPawn ? Cast<ACatfishingPlayerController>(AvatarPawn->GetController()) : nullptr;
		}
		return Controller;
	}

	/** 从 AbilityActorInfo 解析当前 Avatar Character；BodyAction 表现只在角色身体上播放，不能落到 Controller 或领域服务。 */
	ACatCharacter* ResolveBodyActionCharacter(const FGameplayAbilityActorInfo* ActorInfo)
	{
		return ActorInfo ? Cast<ACatCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	}

	/** 读取动作级前摇秒数；表现设置缺失时回退 Ability 默认窗口，保证老配置仍有可取消生命周期。 */
	float ResolveBodyActionLeadInSeconds(const ECatBodyActionAbilityCommand Command, const float FallbackSeconds)
	{
		const UCatBodyActionPresentationSettings* Settings = GetDefault<UCatBodyActionPresentationSettings>();
		return Settings ? Settings->GetLeadInSecondsForCommand(Command) : FMath::Max(0.0f, FallbackSeconds);
	}

	/** 读取动作级表现标签；配置缺失时回退 AbilityEvent 标签，保证蓝图仍能按动作稳定分派表现。 */
	FGameplayTag ResolveBodyActionPresentationEventTag(const ECatBodyActionAbilityCommand Command)
	{
		const UCatBodyActionPresentationSettings* Settings = GetDefault<UCatBodyActionPresentationSettings>();
		return Settings ? Settings->GetPresentationEventTagForCommand(Command)
			: UCatBodyActionPayload::GetEventTagForCommand(Command);
	}
}

bool UCatBodyActionPayload::InitializeForCommand(const ECatBodyActionAbilityCommand InCommand)
{
	// 初始化流程：先保存动作枚举，再从唯一标签表解析事件标签；未知动作不会猜默认事件，调用方据此 fail-closed。
	Command = InCommand;
	EventTag = GetEventTagForCommand(InCommand);
	return EventTag.IsValid();
}

bool UCatBodyActionPayload::IsRuntimeValid() const
{
	// 载荷只证明“这是一条可投递给 Ability 的身体动作事件”：
	// 具体 RequestId、距离、Revision、权限和阶段继续交给原领域服务裁决，避免 Ability 复制第二套业务规则。
	return Command != ECatBodyActionAbilityCommand::Unknown && EventTag.IsValid();
}

FGameplayTag UCatBodyActionPayload::GetEventTagForCommand(const ECatBodyActionAbilityCommand InCommand)
{
	// 命令到事件标签的唯一映射：RPC 和 Ability 都走这里，保证新增动作时不会出现“能路由但 Ability 不触发”的分叉。
	switch (InCommand)
	{
	case ECatBodyActionAbilityCommand::RequestSacrifice:
		return CatFishingAbilityTags::AbilityEvent_Body_RequestSacrifice;
	case ECatBodyActionAbilityCommand::CampRest:
		return CatFishingAbilityTags::AbilityEvent_Body_CampRest;
	case ECatBodyActionAbilityCommand::CampfirePlayback:
		return CatFishingAbilityTags::AbilityEvent_Body_CampfirePlayback;
	case ECatBodyActionAbilityCommand::TransferFishToTank:
		return CatFishingAbilityTags::AbilityEvent_Body_TransferFishToTank;
	case ECatBodyActionAbilityCommand::RescueCharacterToCamp:
		return CatFishingAbilityTags::AbilityEvent_Body_RescueCharacterToCamp;
	case ECatBodyActionAbilityCommand::RepairRodAtCamp:
		return CatFishingAbilityTags::AbilityEvent_Body_RepairRodAtCamp;
	case ECatBodyActionAbilityCommand::UseHerbOnCharacter:
		return CatFishingAbilityTags::AbilityEvent_Body_UseHerbOnCharacter;
	case ECatBodyActionAbilityCommand::ConsumeFish:
		return CatFishingAbilityTags::AbilityEvent_Body_ConsumeFish;
	case ECatBodyActionAbilityCommand::BeginTheft:
		return CatFishingAbilityTags::AbilityEvent_Body_BeginTheft;
	case ECatBodyActionAbilityCommand::CatchTheft:
		return CatFishingAbilityTags::AbilityEvent_Body_CatchTheft;
	case ECatBodyActionAbilityCommand::RequestManualHelp:
		return CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp;
	case ECatBodyActionAbilityCommand::RequestMischief:
		return CatFishingAbilityTags::AbilityEvent_Body_RequestMischief;
	case ECatBodyActionAbilityCommand::PlaceProtectionSign:
		return CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign;
	case ECatBodyActionAbilityCommand::CompleteShakeDry:
		return CatFishingAbilityTags::AbilityEvent_Body_CompleteShakeDry;
	default:
		return FGameplayTag();
	}
}

UCatGA_BodyActionCommand::UCatGA_BodyActionCommand()
{
	// 构造流程：
	// 1. 先声明 BodyAction 只在服务器执行，客户端 UI/输入仍必须通过 owning RPC 进入服务器。
	// 2. 再按 Character 实例化，给后续 Montage、AbilityTask 或取消窗口留下同一个 Ability 生命周期。
	// 3. 最后注册所有身体动作事件标签；少配任何一个标签都会让对应 RPC 无法被正式 Ability 接管。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Command));

	for (const ECatBodyActionAbilityCommand Command : {
		ECatBodyActionAbilityCommand::RequestSacrifice,
		ECatBodyActionAbilityCommand::CampRest,
		ECatBodyActionAbilityCommand::CampfirePlayback,
		ECatBodyActionAbilityCommand::TransferFishToTank,
		ECatBodyActionAbilityCommand::RescueCharacterToCamp,
		ECatBodyActionAbilityCommand::RepairRodAtCamp,
		ECatBodyActionAbilityCommand::UseHerbOnCharacter,
		ECatBodyActionAbilityCommand::ConsumeFish,
		ECatBodyActionAbilityCommand::BeginTheft,
		ECatBodyActionAbilityCommand::CatchTheft,
		ECatBodyActionAbilityCommand::RequestManualHelp,
		ECatBodyActionAbilityCommand::RequestMischief,
		ECatBodyActionAbilityCommand::PlaceProtectionSign,
		ECatBodyActionAbilityCommand::CompleteShakeDry})
	{
		AddBodyActionTrigger(AbilityTriggers, UCatBodyActionPayload::GetEventTagForCommand(Command));
	}
}

#if WITH_DEV_AUTOMATION_TESTS
float UCatGA_BodyActionCommand::GetBodyActionLeadInSecondsForAutomation(
	const ECatBodyActionAbilityCommand Command) const
{
	// 自动化读取流程：直接走运行时解析函数，先查动作级表现设置，设置缺失时才回退到 Ability 默认窗口；不写状态、不影响正在运行的 Ability。
	return ResolveBodyActionLeadInSeconds(Command, BodyActionCommitWindowSeconds);
}

FGameplayTag UCatGA_BodyActionCommand::GetBodyActionPresentationEventTagForAutomation(
	const ECatBodyActionAbilityCommand Command) const
{
	// 自动化读取流程：暴露本动作真正会广播的表现标签，配置未覆盖时回退到 BodyAction payload 的 AbilityEvent 标签，方便测试确认前后端使用同一标识。
	return ResolveBodyActionPresentationEventTag(Command);
}

void UCatGA_BodyActionCommand::SetBodyActionCommitWindowSecondsForAutomation(const float InSeconds)
{
	// 测试覆盖流程：只接受非负窗口作为进程级覆盖，确保已授予或蓝图派生的 BodyAction 实例也读取同一个自动化窗口。
	// 清除覆盖必须走 ClearBodyActionCommitWindowOverrideForAutomation，避免测试把默认窗口误写成全局覆盖后绕过动作级表现设置。
	GBodyActionCommitWindowOverrideSeconds = FMath::Max(0.0f, InSeconds);
}

void UCatGA_BodyActionCommand::ClearBodyActionCommitWindowOverrideForAutomation()
{
	// 覆盖清理流程：恢复 -1 哨兵，让后续同一 Editor 进程里的 BodyAction 激活重新读取 UCatBodyActionPresentationSettings。
	// 这一步只影响自动化进程内全局变量，不改 Ability CDO、项目配置或正在运行的 Ability 实例。
	GBodyActionCommitWindowOverrideSeconds = -1.0f;
}

bool UCatGA_BodyActionCommand::HasBodyActionCommitWindowOverrideForAutomation() const
{
	// 覆盖观察流程：只读全局哨兵状态，帮助自动化确认临时即时提交流程已经退出，不影响正式动作级前摇解析。
	return GBodyActionCommitWindowOverrideSeconds >= 0.0f;
}
#endif

void UCatGA_BodyActionCommand::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 先从 GameplayEvent 读取服务器 RPC 创建的载荷，并确认事件标签、动作和 owning Controller 有效。
	// 2. 再从表现配置冻结本次前摇标签并广播开始表现；这一步只给 Character 蓝图/Montage 用，不提交领域结果。
	// 3. 接着把载荷暂存在 Ability 实例上，打开动作级 WaitDelay 提交窗口，让统一 Cancel 输入有机会中止未落地动作。
	// 4. WaitDelay 由当前 Ability 生命周期托管；Ability 被取消时父类会结束任务，后续即便回调被调度也只能看到已清空的载荷并取消，不能提交旧命令。
	// 5. 窗口为 0 时仍走同一个提交回调，避免即时动作和长动作形成两套领域提交路径。
	// 6. 真实结果继续经原 Client RPC、复制快照或领域缓存回到 UI；本 Ability 只表达身体动作生命周期。
	const UCatBodyActionPayload* Payload = TriggerEventData
		? Cast<UCatBodyActionPayload>(TriggerEventData->OptionalObject.Get()) : nullptr;
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!Payload || !Payload->IsRuntimeValid() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	ActivePayload = Payload;
	ActivePresentationEventTag = ResolveBodyActionPresentationEventTag(Payload->Command);
	if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo);
		Character && ActivePresentationEventTag.IsValid())
	{
		Character->Multicast_PlayBodyActionPresentation(Payload->Command, ActivePresentationEventTag);
	}
	float EffectiveCommitWindowSeconds = ResolveBodyActionLeadInSeconds(Payload->Command, BodyActionCommitWindowSeconds);
#if WITH_DEV_AUTOMATION_TESTS
	if (GBodyActionCommitWindowOverrideSeconds >= 0.0f)
	{
		EffectiveCommitWindowSeconds = GBodyActionCommitWindowOverrideSeconds;
	}
#endif
	if (EffectiveCommitWindowSeconds <= 0.0f)
	{
		CommitBodyActionAfterWindow();
		return;
	}
	UAbilityTask_WaitDelay* CommitDelay = UAbilityTask_WaitDelay::WaitDelay(this, EffectiveCommitWindowSeconds);
	if (!CommitDelay)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	// 委托生命周期由 AbilityTask 绑定到本 Ability；EndAbility 会清 ActivePayload，因此取消路径不会让迟到回调带着旧载荷再次提交。
	CommitDelay->OnFinish.AddDynamic(this, &ThisClass::CommitBodyActionAfterWindow);
	CommitDelay->ReadyForActivation();
}

void UCatGA_BodyActionCommand::CommitBodyActionAfterWindow()
{
	// 提交流程：WaitDelay 结束时重读当前 ActorInfo 和 Controller，避免占有关系在窗口内变化后还用旧身份落地事务。
	// 载荷缺失、控制器缺失或领域入口拒绝接管时取消 Ability；成功提交后正常结束并清理暂存载荷。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!ActivePayload || !ActivePayload->IsRuntimeValid() || !Controller
		|| !Controller->ExecuteBodyActionAbilityPayload(*ActivePayload))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}

void UCatGA_BodyActionCommand::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：取消时先用当前载荷和冻结标签广播停止表现，再清空暂存载荷和表现标签，最后交给 GAS 父类结束任务和实例状态。
	// 这个顺序保证蓝图能拿到要停止的动作，同时让任何迟到的 WaitDelay 回调只能看到空载荷而无法提交旧命令。
	if (bWasCancelled && ActivePayload && ActivePresentationEventTag.IsValid())
	{
		if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo))
		{
			Character->Multicast_StopBodyActionPresentation(ActivePayload->Command, ActivePresentationEventTag);
		}
	}
	ActivePayload = nullptr;
	ActivePresentationEventTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
