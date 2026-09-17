#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Social/CatSocialService.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Logging/CatLog.h"
#include "Condition/CatConditionComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Social/CatProtectionSignActor.h"
#include "Social/CatSocialSettings.h"

// 创建条件流程：只允许 authority Game World 持有 Social 命令终态和保护牌索引；客户端没有平行写状态。
bool UCatSocialService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭新命令，再清一局冷却、终态和保护牌索引；具体 Actor 随 World 生命周期释放。
void UCatSocialService::Deinitialize()
{
	CloseCommands();
	LastManualHelpTimeByPlayer.Reset();
	CommandTerminalCache.Reset();
	ProtectionSignByPlayer.Reset();
	Super::Deinitialize();
}

// Teardown 关门流程：将本局命令开关关闭，后续请求只可重放已完成结果，不能再提交新的社交行为。
void UCatSocialService::CloseCommands()
{
	bCommandsOpen = false;
}

// 恶作剧权限流程：先按身份/操作/RequestId 重放，再忽略客户端位置并从双方权威 Pawn 验证状态、距离与目标保护牌；通过后才写允许终态。
// 没有冷却这一步了（09-12）：设计不设系统级频率上限与时机限制，熟人自治。
FCatDomainCommandResult UCatSocialService::RequestMischief(AController* InstigatorController,
	AController* TargetController, const FGuid RequestId, const FVector InteractionLocation)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString InstigatorId = ResolveStableNetId(InstigatorController);
	const APlayerState* TargetPlayerState = TargetController ? TargetController->PlayerState : nullptr;
	const ACatCharacter* InstigatorCharacter = InstigatorController ? Cast<ACatCharacter>(InstigatorController->GetPawn()) : nullptr;
	const ACatCharacter* TargetCharacter = TargetController ? Cast<ACatCharacter>(TargetController->GetPawn()) : nullptr;
	(void)InteractionLocation;
	if (!RequestId.IsValid() || InstigatorId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(InstigatorId, TEXT("Mischief"), RequestId);
	if (const FCatDomainCommandResult* Cached = CommandTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsMischiefReady() || !TargetPlayerState || InstigatorController == TargetController
		|| !IsCharacterSociallyActive(InstigatorCharacter) || !IsCharacterSociallyActive(TargetCharacter)
		|| FVector::DistSquared(InstigatorCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(Settings->MischiefInteractionRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	for (TActorIterator<ACatProtectionSignActor> It(GetWorld()); It; ++It)
	{
		if (It->ProtectsMischiefAgainst(TargetPlayerState, TargetCharacter->GetActorLocation()))
		{
			Result.Error = ECatDomainCommandError::PermissionDenied;
			return Finish(Result);
		}
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 放牌流程：先按身份/操作/RequestId 重放，再验证 Pawn、显式范围和有限位置；首次提交复用每人唯一 Actor 并配置保护。
// gate 换成了 IsProtectionSignReady()（09-12）：立牌不再挂在恶作剧那套 gate 上——旧写法里恶作剧任何一项缺配
// 都会把立牌一起关死，而立牌正是恶作剧唯一的护栏。
// 牌子只裁决恶作剧，**不挡拿鱼**（2026-09-12 裁决③）：08-16 那句「立牌＝完整免打扰」随「恶作剧权限开关」
// 一并退役——代码一直就是这么做的，此前是文档说错了。所以拿鱼路径不读牌子，这不是待补的半成品，是结论。
FCatDomainCommandResult UCatSocialService::PlaceProtectionSign(AController* RequestingController,
	const FGuid RequestId, const FVector SignLocation)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString StableNetId = ResolveStableNetId(RequestingController);
	APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(StableNetId, TEXT("PlaceProtectionSign"), RequestId);
	if (const FCatDomainCommandResult* Cached = CommandTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsProtectionSignReady() || !Pawn || !PlayerState
		|| SignLocation.ContainsNaN()
		|| FVector::DistSquared(Pawn->GetActorLocation(), SignLocation)
			> FMath::Square(Settings->ProtectionSignPlacementRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	ACatProtectionSignActor* Sign = ProtectionSignByPlayer.FindRef(StableNetId).Get();
	if (!Sign)
	{
		Sign = GetWorld()->SpawnActor<ACatProtectionSignActor>(ACatProtectionSignActor::StaticClass(),
			SignLocation, FRotator::ZeroRotator);
		ProtectionSignByPlayer.Add(StableNetId, Sign);
	}
	else
	{
		Sign->SetActorLocation(SignLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!Sign || !Sign->ConfigureProtection(PlayerState, Settings->ProtectionSignRadiusCentimeters))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 手动求助流程：先按身份/操作/RequestId 重放，再校验两种 Manual 类型、范围、冷却和 Pawn；首次成功才递增 Revision 并发布 nearby 信号。
FCatDomainCommandResult UCatSocialService::RequestManualHelp(AController* RequestingController,
	const FGuid RequestId, const ECatHelpSignalKind Kind)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString StableNetId = ResolveStableNetId(RequestingController);
	const APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(StableNetId, TEXT("ManualHelp"), RequestId);
	if (const FCatDomainCommandResult* Cached = CommandTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsManualHelpReady() || !Pawn || !GameState
		|| (Kind != ECatHelpSignalKind::ManualFishing && Kind != ECatHelpSignalKind::ManualDowned))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* LastTime = LastManualHelpTimeByPlayer.Find(StableNetId);
		LastTime && Now - *LastTime < Settings->ManualHelpCooldownSeconds)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	FCatHelpSignalSnapshot Signal;
	Signal.SignalId = RequestId;
	Signal.Kind = Kind;
	Signal.SourceLocation = Pawn->GetActorLocation();
	Signal.RadiusCentimeters = Settings->ManualHelpRadiusCentimeters;
	Signal.bGlobal = false;
	Signal.Revision = ++HelpSignalRevision;
	GameState->SetHelpSignalFromAuthority(Signal);
	LastManualHelpTimeByPlayer.Add(StableNetId, Now);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_manual_help RequestId=%s Kind=%s Revision=%lld RadiusCm=%.3f"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Kind), Signal.Revision,
		Signal.RadiusCentimeters);
	return Finish(Result);
}

// Giant 提示流程：要求 Social 总 gate、有效会话 ID 和当前 Pawn；直接发布全局系统信号，不占用玩家手动求助冷却。
void UCatSocialService::BroadcastGiantFishingPrompt(AController* FisherController, const FGuid FishingSessionId)
{
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const APawn* Pawn = FisherController ? FisherController->GetPawn() : nullptr;
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!bCommandsOpen || !Settings->bEnableSocialRuntime || !FishingSessionId.IsValid() || !Pawn || !GameState)
	{
		return;
	}
	FCatHelpSignalSnapshot Signal;
	Signal.SignalId = FishingSessionId;
	Signal.Kind = ECatHelpSignalKind::GiantFishSystem;
	Signal.SourceLocation = Pawn->GetActorLocation();
	Signal.bGlobal = true;
	Signal.Revision = ++HelpSignalRevision;
	GameState->SetHelpSignalFromAuthority(Signal);
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_giant_prompt SessionId=%s Revision=%lld Global=true"),
		*FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Signal.Revision);
}

// 终态键构造流程：显式拼接服务器身份、操作域与标准 GUID；操作名隔离相同 RequestId 的不同语义，原始身份只留在服务私有映射中。
FString UCatSocialService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId, Operation,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 身份解析流程：只读取 Controller PlayerState 的继承 UniqueId；原始值只存在 Social 私有命令缓存和冷却键。
FString UCatSocialService::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}

// Social 状态检查流程：要求项目 Character 和 Condition 均有效且未倒地；缺组件或倒地都不能发起、被授权或完成空间交互。
bool UCatSocialService::IsCharacterSociallyActive(const ACatCharacter* Character)
{
	const UCatAbilitySystemComponent* Conditions = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	return Conditions && !Conditions->HasMatchingGameplayTag(CatStateTags::Downed);
}
