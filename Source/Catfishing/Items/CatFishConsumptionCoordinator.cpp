#include "Items/CatFishConsumptionCoordinator.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatContainerAccessRules.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"

bool UCatFishConsumptionCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	// 创建条件流程：只在服务器 Game World 建立直接吃鱼协调器；客户端不拥有 Items 写口或 Condition 提交权。
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatFishConsumeResult UCatFishConsumptionCoordinator::ConsumeReachableFish(AController* RequestingController,
	ACatCharacter* EatingCharacter, FCatFishConsumeCommand Command)
{
	// 吃鱼协调流程：
	// 1. 先做不会产生副作用的 RPC 形状和服务器身份校验，再只读查询 Items 终态重放，避免当前距离或容器状态覆盖旧终态。
	// 2. 不是重放时才检查玩法 gate、源容器宿主、种类、当前 Character 可达性和身体食用预检。
	// 3. Items 首次成功或成功重放后才提交 Condition/Growth；身体失败会随结果返回，不把容器成功伪装成整体成功。
	FCatFishConsumeResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Body.RequestId = Command.Context.RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	UCatConditionComponent* Conditions = EatingCharacter ? EatingCharacter->GetConditionComponent() : nullptr;
	const APlayerState* CurrentPlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!RequestingController || EatingCharacter != RequestingController->GetPawn()
		|| !Command.Context.RequestId.IsValid() || !Command.FishInstanceId.IsValid()
		|| !Command.SourceContainerId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (!Items || !Conditions || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid()
		|| EatingCharacter->GetWorld() != World)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	const auto SubmitBodyFromDefinition = [&](UCatFishDefinition* Definition)
	{
		if (!Definition)
		{
			Result.Body.Error = ECatDomainCommandError::PolicyUndecided;
			return;
		}
		Result.Body = Conditions->ConsumeCommittedFish(Command.Context.RequestId, Definition);
		if (!CatIsAcceptedDomainCommandResult(Result.Body))
		{
			UE_LOG(LogCatItems, Error,
				TEXT("Event=items_consume_body_commit_failed RequestId=%s FishInstanceId=%s ContainerId=%s ItemsRevision=%lld BodyError=%s BodyReplay=%s BodyReplayError=%s BodyRevision=%lld"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				*Command.SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens),
				Result.Command.Revision,
				*UEnum::GetValueAsString(Result.Body.Error),
				Result.Body.bTerminalReplay ? TEXT("true") : TEXT("false"),
				*UEnum::GetValueAsString(Result.Body.ReplayedTerminalError), Result.Body.Revision);
		}
	};
	FCatFishConsumeResult ReplayResult;
	if (Items->TryReplayFishConsumeTerminal(Command, ReplayResult))
	{
		Result = ReplayResult;
		Result.Body.RequestId = Command.Context.RequestId;
		if (CatIsAcceptedDomainCommandResult(Result.Command))
		{
			UCatFishDefinition* ReplayDefinition =
				GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Result.Fish.FishDefinitionId);
			SubmitBodyFromDefinition(ReplayDefinition);
		}
		return Result;
	}
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	FCatContainerSnapshot Source;
	if (!Items->TryGetContainerSnapshot(Command.SourceContainerId, Source))
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Command.Revision = Source.Revision;
	ECatContainerKind SourceKind = ECatContainerKind::Unknown;
	AActor* SourceHost = nullptr;
	if (Conditions->GetSnapshot().bDowned)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	if (!Items->TryGetContainerHost(Command.SourceContainerId, SourceKind, SourceHost))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (SourceKind != ECatContainerKind::FishGuard && SourceKind != ECatContainerKind::SharedFishTank)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!SourceHost)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (!CatContainerAccessRules::IsHostReachable(SourceHost, EatingCharacter, CampSettings))
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	const FCatFishInstance* Fish = Source.Fish.FindByPredicate([&Command](const FCatFishInstance& Candidate)
	{
		return Candidate.FishInstanceId == Command.FishInstanceId;
	});
	if (!Fish)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	UCatFishDefinition* Definition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Fish->FishDefinitionId);
	if (!Definition)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	Result.Command.Error = Conditions->ValidateFishConsumption(Definition);
	if (Result.Command.Error != ECatDomainCommandError::None)
	{
		return Result;
	}
	Result = Items->ConsumeFish(Command);
	Result.Body.RequestId = Command.Context.RequestId;
	if (CatIsAcceptedDomainCommandResult(Result.Command))
	{
		SubmitBodyFromDefinition(Definition);
	}
	return Result;
}
