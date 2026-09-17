#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camp/CatAltarActor.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Character/CatCharacter.h"
#include "Components/Border.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextBlock.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/Shop/CatShopInteractionComponent.h"
#include "UI/Shop/CatShopWidget.h"
#include "Widgets/SWindow.h"

namespace CatAltarConfirmationTests
{
	/** 保存本用例临时覆盖的 PIE 网络偏好；所有世界销毁后恢复，避免三端 IP 设置遗留给编辑器用户或后续回归。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 在启动 PIE 前读取共享设置和网络驱动，作为本测试结束时唯一的恢复来源。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode);
			Settings->GetPlayNumberOfClients(ClientCount);
			Settings->GetRunUnderOneProcess(bOneProcess);
			NetDrivers = GEngine->NetDriverDefinitions;
		}

		/** 等待 PIE 完整停止后回填原偏好，避免 EndPIE 收尾再次覆盖已经恢复的数据。 */
		bool Update() override
		{
			if (GEditor && GEditor->PlayWorld) return false;
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode);
			Settings->SetPlayNumberOfClients(ClientCount);
			Settings->SetRunUnderOneProcess(bOneProcess);
			GEngine->NetDriverDefinitions = NetDrivers;
			return true;
		}

	private:
		/** 测试前的 PIE 网络模式；恢复命令在所有临时世界消失后写回。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 测试前的参与端数量；三端确认回归不能改变用户下次启动时的端数。 */
		int32 ClientCount = 1;
		/** 测试前是否单进程运行；窗口与本地服务器时钟检查依赖临时单进程但不持久化。 */
		bool bOneProcess = true;
		/** 测试前的完整网络驱动定义；IP 驱动只在本回归运行期间替换。 */
		TArray<FNetDriverDefinition> NetDrivers;
	};

	/** 三端正式祭坛确认回归状态机；只通过实际 GameMode、Controller RPC、GameState 复制和已入视口 WBP 观察结果。 */
	class FVerifyAltarConfirmation final : public IAutomationLatentCommand
	{
	public:
		/** 保存 Automation 断言宿主；PIE 尚未建立时不读取任何世界或玩家。 */
		explicit FVerifyAltarConfirmation(FAutomationTestBase* InTest)
			: Test(InTest)
		{
		}

		/** 结束或失败时销毁本回归创建的鱼、祭坛和商店摊位；过场、计时器和联机连接仍由 EndPIE 正式收口。 */
		~FVerifyAltarConfirmation() override
		{
			if (OfferingFish.IsValid()) OfferingFish->Destroy();
			if (OfferingGuard.IsValid()) OfferingGuard->Destroy();
			if (Altar.IsValid()) Altar->Destroy();
			if (ServerShopKiosk.IsValid()) ServerShopKiosk->Destroy();
		}

		/** 建立三端并进入夜晚，先验证房主及远端发起者取消、窗口关闭和 F9 无截图绑定，再覆盖普通撤回、生命周期、超时与唯一翻天。 */
		bool Update() override
		{
			if (StartedAtSeconds <= 0.0) StartedAtSeconds = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StartedAtSeconds > TimeoutSeconds)
			{
				Test->AddError(FString::Printf(TEXT("Formal altar confirmation test timed out: stage=%d waiting=%s"), Stage, *WaitingFor));
				return true;
			}

			if (Stage == 0) return PrepareEndpoints();
			if (!ServerWorld.IsValid() || !ServerMode.IsValid() || !HostController.IsValid() || !ClientOneController.IsValid()
				|| !ClientTwoController.IsValid() || (Stage > 1 && !Altar.IsValid()))
			{
				Test->AddError(TEXT("Formal altar confirmation lost a required PIE endpoint, GameMode, controller or altar."));
				return true;
			}

			switch (Stage)
			{
			case 1: return EnterNormalNightAndBeginFirstRequest();
			case 2: return VerifyInitialSnapshotAndConfirmationWidget();
			case 3: return VerifyRepeatedConfirmThenWithdraw();
			case 4: return VerifyShopKeysAndAltarDestructionCancels();
			case 5: return BeginLifecycleRequest();
			case 6: return VerifyPlayerLeaveCancels();
			case 7: return BeginTimeoutRequest();
			case 8: return VerifyServerTimeoutCancels();
			case 9: return BeginFinalRequest();
			case 10: return ConfirmFinalRequestFromRemainingRemotePlayer();
			case 11: return VerifyAcceptedRequestStartsExactlyOneTransition();
			case 12: return VerifyCommittedTransitionAndBeginSinglePlayerRequest();
			case 13: return VerifySinglePlayerImmediatelyAccepted();
			case 14: return VerifySinglePlayerTransitionCommitted();
			case 15: return CancelFromInitiator();
			case 16: return VerifyInitiatorCancellationClosesAllWindows();
			default:
				Test->AddError(TEXT("Formal altar confirmation reached an unknown state-machine stage."));
				return true;
			}
		}

	private:
		/** 收集 Listen Server 与两名远端客户端，按 PlayerState Id 反查服务器 Controller，并等待正式 Character/Condition/Run 依赖全部就绪。 */
		bool PrepareEndpoints()
		{
			WaitingFor = TEXT("three formal PIE worlds, active controllers and character condition components");
			UWorld* FoundServer = nullptr;
			TArray<UWorld*> FoundClients;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_ListenServer) FoundServer = World;
				else if (World->GetNetMode() == NM_Client) FoundClients.Add(World);
			}
			FoundClients.Sort([](const UWorld& Left, const UWorld& Right) { return Left.GetName() < Right.GetName(); });
			if (!FoundServer || FoundClients.Num() != RequiredRemoteClientCount) return false;

			ACatfishingGameModeBase* Mode = FoundServer->GetAuthGameMode<ACatfishingGameModeBase>();
			ACatfishingPlayerController* Host = Cast<ACatfishingPlayerController>(FoundServer->GetFirstPlayerController());
			ACatfishingPlayerController* ClientOne = Cast<ACatfishingPlayerController>(FoundClients[0]->GetFirstPlayerController());
			ACatfishingPlayerController* ClientTwo = Cast<ACatfishingPlayerController>(FoundClients[1]->GetFirstPlayerController());
			if (!Mode || !Host || !ClientOne || !ClientTwo || !Host->GetPawn() || !ClientOne->GetPawn() || !ClientTwo->GetPawn()) return false;

			ACatfishingPlayerController* ServerOne = FindServerControllerForClient(*FoundServer, *ClientOne);
			ACatfishingPlayerController* ServerTwo = FindServerControllerForClient(*FoundServer, *ClientTwo);
			ACatCharacter* HostCharacter = Cast<ACatCharacter>(Host->GetPawn());
			ACatCharacter* ServerOneCharacter = ServerOne ? Cast<ACatCharacter>(ServerOne->GetPawn()) : nullptr;
			ACatCharacter* ServerTwoCharacter = ServerTwo ? Cast<ACatCharacter>(ServerTwo->GetPawn()) : nullptr;
			if (!ServerOne || !ServerTwo || !HostCharacter || !ServerOneCharacter || !ServerTwoCharacter
				|| !HostCharacter->GetConditionComponent() || !ServerOneCharacter->GetConditionComponent() || !ServerTwoCharacter->GetConditionComponent()) return false;

			ServerWorld = FoundServer;
			ServerMode = Mode;
			HostController = Host;
			ClientOneController = ClientOne;
			ClientTwoController = ClientTwo;
			ServerClientOneController = ServerOne;
			ServerClientTwoController = ServerTwo;
			HostPlayerState = Host->GetPlayerState<APlayerState>();
			ClientOnePlayerState = ServerOne->GetPlayerState<APlayerState>();
			ClientTwoPlayerState = ServerTwo->GetPlayerState<APlayerState>();
			if (!HostPlayerState.IsValid() || !ClientOnePlayerState.IsValid() || !ClientTwoPlayerState.IsValid()) return false;
			Stage = 1;
			return false;
		}

		/** 等待正式 Run 进入可献祭的普通夜晚，再在房主交互半径内生成祭坛和真实地面鱼，并让远端二号倒地后远离祭坛。 */
		bool EnterNormalNightAndBeginFirstRequest()
		{
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			if (Run.Phase.Phase != ECatRunPhase::NormalNight)
			{
				WaitingFor = TEXT("formal Run StateTree to reach NormalNight through debug day-end command");
				if (Run.Phase.Phase == ECatRunPhase::DayActive) ServerMode->ApplyDebugSkipToNight();
				return false;
			}
			if (!Run.Phase.bOfferingOpen) return false;

			if (!Altar.IsValid())
			{
				ACatCharacter* HostCharacter = Cast<ACatCharacter>(HostController->GetPawn());
				ACatCharacter* RemoteTwoCharacter = Cast<ACatCharacter>(ServerClientTwoController->GetPawn());
				if (!HostCharacter || !RemoteTwoCharacter) return false;
				const FVector AltarLocation = HostCharacter->GetActorLocation() + HostCharacter->GetActorForwardVector() * 100.0f;
				ACatAltarActor* SpawnedAltar = ServerWorld->SpawnActor<ACatAltarActor>(AltarLocation, FRotator::ZeroRotator);
				if (!Test->TestNotNull(TEXT("server spawns the replicated altar in the initiator's real interaction range"), SpawnedAltar)) return true;
				SpawnedAltar->bAlwaysRelevant = true;
				// 留出提交前观察窗口，让重复 F8 断言发生在正式黑屏结算之前，而非缩短或绕过生产过场。
				SpawnedAltar->FadeOutSeconds = 2.0f;
				AltarSpawnLocation = AltarLocation;
				Altar = SpawnedAltar;
				if (!SeedGroundOffering(*SpawnedAltar)) return true;
				if (!Test->TestTrue(TEXT("authority can mark a remote participant downed before confirmation"),
					RemoteTwoCharacter->GetConditionComponent()->SetDownedFromAuthority(true))) return true;
				RemoteTwoCharacter->SetActorLocation(AltarLocation + FVector(500000.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
				RemoteTwoCharacter->ForceNetUpdate();
				SpawnedAltar->ForceNetUpdate();
			}

			FirstRequestId = FGuid::NewGuid();
			if (!Test->TestTrue(TEXT("host begins the first altar confirmation through the public GameMode entry"),
				ServerMode->BeginAltarConfirmation(Altar.Get(), HostController.Get(), FirstRequestId))) return true;
			Stage = InitiatorCancellationRounds < 2 ? 15 : 2;
			return false;
		}

		/** 房主和远端依次作为发起者，核对同一个正式 WBP 只提供取消，再由该端本人输入入口发送 F9。 */
		bool CancelFromInitiator()
		{
			WaitingFor = TEXT("initiator-only cancel UI and owning-client F9");
			if (!IsWaitingSnapshotReplicated(*ClientOneController, FirstRequestId, 3)
				|| !IsWaitingSnapshotReplicated(*ClientTwoController, FirstRequestId, 3)) return false;
			ACatfishingPlayerController* Initiator = InitiatorCancellationRounds == 0 ? HostController.Get() : ClientOneController.Get();
			UUserWidget* Widget = FindConfirmationWindow(*Initiator);
			UTextBlock* Hints = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("InputHintsTextBlock"))) : nullptr;
			if (!Hints) return false;
			Test->TestFalse(TEXT("initiator has no confirm action in the shared WBP"), Hints->GetText().ToString().Contains(TEXT("F8")));
			Test->TestTrue(TEXT("initiator only sees cancellation"), Hints->GetText().ToString().Contains(TEXT("取消")));
			for (ACatfishingPlayerController* Controller : {HostController.Get(), ClientOneController.Get(), ClientTwoController.Get()})
			{
				if (!Controller->PlayerInput) return false;
				Test->TestFalse(TEXT("effective player input has no F9 screenshot debug binding"),
					Controller->PlayerInput->DebugExecBindings.ContainsByPredicate([](const FKeyBind& Binding)
					{ return Binding.Key == EKeys::F9 && Binding.Command.Contains(TEXT("shot"), ESearchCase::IgnoreCase); }));
			}
			if (!Test->TestTrue(TEXT("initiator F9 submits cancel"), Initiator->TrySetAltarConfirmationFromKey(EKeys::F9))) return true;
			InitiatorCancellationSentAtSeconds = FPlatformTime::Seconds();
			Stage = 16;
			return false;
		}

		/** 主动取消须在短网络缓冲内关闭全部窗口且不结算；迟到确认不能恢复旧轮，再用远端身份重复一次。 */
		bool VerifyInitiatorCancellationClosesAllWindows()
		{
			WaitingFor = TEXT("initiator cancellation closes every endpoint without the two-second feedback delay");
			bool bAllClosed = true;
			for (ACatfishingPlayerController* Controller : {HostController.Get(), ClientOneController.Get(), ClientTwoController.Get()})
			{
				const ACatfishingGameState* State = Controller->GetWorld()->GetGameState<ACatfishingGameState>();
				bAllClosed &= State && State->GetRunPublicState().AltarConfirmation.State == ECatAltarConfirmationState::Idle
					&& !FindConfirmationWindow(*Controller);
			}
			if (!bAllClosed && FPlatformTime::Seconds() - InitiatorCancellationSentAtSeconds < 1.0) return false;
			if (!Test->TestTrue(TEXT("initiator cancellation immediately ends the request and closes all UIs"), bAllClosed)) return true;
			ServerMode->SetAltarConfirmation(ServerClientTwoController.Get(), FirstRequestId, true);
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			if (!Test->TestTrue(TEXT("late confirmation cannot restart cancelled request"), Run.AltarConfirmation.State == ECatAltarConfirmationState::Idle)
				|| !Test->TestFalse(TEXT("initiator cancel does not start transition"), Run.DayTransition.bActive)
				|| !Test->TestTrue(TEXT("initiator cancel keeps ground fish and current night"), OfferingFish.IsValid()
					&& !OfferingFish->IsActorBeingDestroyed() && Run.Phase.Phase == ECatRunPhase::NormalNight)) return true;
			++InitiatorCancellationRounds;
			if (InitiatorCancellationRounds == 1)
			{
				ServerClientOneController->GetPawn()->SetActorLocation(HostController->GetPawn()->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
				FirstRequestId = FGuid::NewGuid();
				if (!Test->TestTrue(TEXT("remote player can initiate the next round"),
					ServerMode->BeginAltarConfirmation(Altar.Get(), ServerClientOneController.Get(), FirstRequestId))) return true;
				Stage = 15;
			}
			else Stage = 1;
			return false;
		}

		/** 验证包含远处倒地者的三人服务器名单已复制到两端正式 WBP，随后从第一远端重复发送 F8 的同值确认。 */
		bool VerifyInitialSnapshotAndConfirmationWidget()
		{
			WaitingFor = TEXT("waiting confirmation snapshot and real confirmation WBP on both remote clients");
			const FCatAltarConfirmationSnapshot& ServerSnapshot = ServerMode->GetRunPublicState().AltarConfirmation;
			if (ServerSnapshot.State != ECatAltarConfirmationState::Waiting || ServerSnapshot.RequestId != FirstRequestId
				|| ServerSnapshot.Participants.Num() != 3 || !HasParticipant(ServerSnapshot, HostPlayerState.Get(), true)
				|| !HasParticipant(ServerSnapshot, ClientOnePlayerState.Get(), false) || !HasParticipant(ServerSnapshot, ClientTwoPlayerState.Get(), false)) return false;
			if (!IsWaitingSnapshotReplicated(*ClientOneController, FirstRequestId, 3)
				|| !IsWaitingSnapshotReplicated(*ClientTwoController, FirstRequestId, 3)) return false;
			if (!VerifyAndCaptureConfirmationWindows() || !OpenFocusedInventory(*ClientOneController)) return false;

			if (!Test->TestTrue(TEXT("focused formal inventory preview dispatch consumes F8 and forwards it to confirmation"),
				DispatchFocusedInventoryKey(EKeys::F8))
				|| !Test->TestTrue(TEXT("focused formal inventory preview dispatch forwards repeated F8 as the same target state"),
					DispatchFocusedInventoryKey(EKeys::F8))) return true;
			Stage = 3;
			return false;
		}

		/** 等待第一个远端的重复 F8 收敛为一个确认，再用 F9 撤回并确认等待态、名单和 DayTransition 都未被错误推进。 */
		bool VerifyRepeatedConfirmThenWithdraw()
		{
			WaitingFor = TEXT("server handling repeated F8 as one participant state change");
			const FCatAltarConfirmationSnapshot& Snapshot = ServerMode->GetRunPublicState().AltarConfirmation;
			if (Snapshot.State != ECatAltarConfirmationState::Waiting || Snapshot.RequestId != FirstRequestId
				|| CountConfirmed(Snapshot) != 2 || !HasParticipant(Snapshot, ClientOnePlayerState.Get(), true)) return false;
			if (!Test->TestFalse(TEXT("waiting confirmation does not activate the formal day-transition input lock"),
				ServerMode->GetRunPublicState().DayTransition.bActive)) return true;
			if (!Test->TestTrue(TEXT("focused formal inventory preview dispatch consumes F9 and forwards the withdrawal"),
				DispatchFocusedInventoryKey(EKeys::F9))) return true;
			Stage = 4;
			return false;
		}

		/** 等待库存 F9 后让正式商店页转交同一人的 F8/F9，再验证远处倒地输入、旧请求拒绝和祭坛销毁的精确收口范围。 */
		bool VerifyShopKeysAndAltarDestructionCancels()
		{
			WaitingFor = TEXT("formal shop F8/F9, downed remote confirmation, stale request rejection and altar destruction cleanup");
			const FCatAltarConfirmationSnapshot& Snapshot = ServerMode->GetRunPublicState().AltarConfirmation;
			if (!bDestroyedConfirmationAltar)
			{
				if (Snapshot.State != ECatAltarConfirmationState::Waiting) return false;
				if (!bSentShopConfirmation)
				{
					if (CountConfirmed(Snapshot) != 1 || !HasParticipant(Snapshot, ClientOnePlayerState.Get(), false)
						|| !OpenFocusedShop(*ClientOneController)) return false;
					bSentShopConfirmation = true;
					if (!Test->TestTrue(TEXT("focused formal shop preview dispatch consumes F8 and forwards the first remote confirmation"),
						DispatchFocusedShopKey(EKeys::F8))) return true;
					return false;
				}
				if (!bSentShopWithdrawal)
				{
					if (CountConfirmed(Snapshot) != 2 || !HasParticipant(Snapshot, ClientOnePlayerState.Get(), true)) return false;
					bSentShopWithdrawal = true;
					if (!Test->TestTrue(TEXT("focused formal shop preview dispatch consumes F9 and revokes the same remote confirmation"),
						DispatchFocusedShopKey(EKeys::F9))) return true;
					return false;
				}
				if (!HasParticipant(Snapshot, ClientOnePlayerState.Get(), false)) return false;
				if (!bSentDownedConfirmation)
				{
					if (CountConfirmed(Snapshot) != 1) return false;
					bSentDownedConfirmation = true;
					if (!Test->TestTrue(TEXT("a downed remote player can still submit F8 from outside the altar range"),
						ClientTwoController->TrySetAltarConfirmationFromKey(EKeys::F8))) return true;
					return false;
				}
				if (!bSentDownedWithdrawal)
				{
					if (CountConfirmed(Snapshot) != 2 || !HasParticipant(Snapshot, ClientTwoPlayerState.Get(), true)) return false;
					bSentDownedWithdrawal = true;
					if (!Test->TestTrue(TEXT("the same downed remote player can revoke with F9"),
						ClientTwoController->TrySetAltarConfirmationFromKey(EKeys::F9))) return true;
					return false;
				}
				if (CountConfirmed(Snapshot) != 1 || !HasParticipant(Snapshot, ClientTwoPlayerState.Get(), false)) return false;

				if (!bSentStaleRequest)
				{
					bSentStaleRequest = true;
					ClientOneController->ServerSetAltarConfirmation(FGuid::NewGuid(), true);
					return false;
				}
				if (CountConfirmed(ServerMode->GetRunPublicState().AltarConfirmation) != 1) return false;
				if (StaleObservationFrames++ < 5) return false;
				if (!bDestroyedUnrelatedAltar)
				{
					ACatAltarActor* UnrelatedAltar = ServerWorld->SpawnActor<ACatAltarActor>(AltarSpawnLocation + FVector(0.0f, 5000.0f, 0.0f), FRotator::ZeroRotator);
					if (!Test->TestNotNull(TEXT("server spawns an unrelated altar for destruction-scope verification"), UnrelatedAltar)) return true;
					bDestroyedUnrelatedAltar = true;
					UnrelatedAltar->Destroy();
					return false;
				}
				if (!bDestroyedConfirmationAltar)
				{
					if (Snapshot.RequestId != FirstRequestId || !Test->TestTrue(TEXT("destroying an unrelated altar keeps the active confirmation waiting"),
						OfferingFish.IsValid() && !OfferingFish->IsActorBeingDestroyed())) return true;
					ACatAltarActor* ActiveAltar = Altar.Get();
					bDestroyedConfirmationAltar = true;
					ActiveAltar->Destroy();
					ACatAltarActor* ReplacementAltar = ServerWorld->SpawnActor<ACatAltarActor>(AltarSpawnLocation, FRotator::ZeroRotator);
					if (!Test->TestNotNull(TEXT("server replaces the destroyed active altar for later lifecycle confirmation cases"), ReplacementAltar)) return true;
					ReplacementAltar->bAlwaysRelevant = true;
					ReplacementAltar->FadeOutSeconds = 2.0f;
					ReplacementAltar->ForceNetUpdate();
					Altar = ReplacementAltar;
					return false;
				}
			}
			if (!bValidatedConfigurationFailure)
			{
				if (Snapshot.State != ECatAltarConfirmationState::Cancelled || Snapshot.RequestId != FirstRequestId
					|| !Test->TestFalse(TEXT("destroying the active altar does not begin a formal transition"), ServerMode->GetRunPublicState().DayTransition.bActive)
					|| !Test->TestTrue(TEXT("destroying the active altar keeps uncommitted ground fish"), OfferingFish.IsValid() && !OfferingFish->IsActorBeingDestroyed())) return true;
				ACatCharacter* HostCharacter = Cast<ACatCharacter>(HostController->GetPawn());
				if (!HostCharacter) return false;
				ACatAltarActor* InvalidConfigurationAltar = ServerWorld->SpawnActor<ACatAltarActor>(
					HostCharacter->GetActorLocation() + HostCharacter->GetActorRightVector() * 100.0f, FRotator::ZeroRotator);
				if (!Test->TestNotNull(TEXT("server spawns an altar with intentionally invalid offering configuration"), InvalidConfigurationAltar)) return true;
				InvalidConfigurationAltar->OfferingRadiusCentimeters = 0.0f;
				ConfigFailureRequestId = FGuid::NewGuid();
				bValidatedConfigurationFailure = true;
				const bool bConfigurationRequestAccepted = ServerMode->BeginAltarConfirmation(InvalidConfigurationAltar, HostController.Get(), ConfigFailureRequestId);
				InvalidConfigurationAltar->Destroy();
				if (!Test->TestFalse(TEXT("invalid offering configuration rejects confirmation before any waiting state or consumption"), bConfigurationRequestAccepted)
					|| !Test->TestTrue(TEXT("configuration preflight publishes the rejected request without consuming the ground fish"),
						ServerMode->GetRunPublicState().AltarConfirmation.State == ECatAltarConfirmationState::Cancelled
						&& ServerMode->GetRunPublicState().AltarConfirmation.RequestId == ConfigFailureRequestId
						&& OfferingFish.IsValid() && !OfferingFish->IsActorBeingDestroyed())) return true;
			}
			Stage = 5;
			return false;
		}

		/** 等待配置预检取消的公开结果复制后开始一轮独立请求，为真实 Logout 生命周期取消建立等待态。 */
		bool BeginLifecycleRequest()
		{
			WaitingFor = TEXT("offering configuration rejection replicated before creating the lifecycle request");
			if (ServerMode->GetRunPublicState().AltarConfirmation.State != ECatAltarConfirmationState::Cancelled
				|| ServerMode->GetRunPublicState().AltarConfirmation.RequestId != ConfigFailureRequestId
				|| !IsCancelledSnapshotReplicated(*ClientOneController)) return false;
			LifecycleRequestId = FGuid::NewGuid();
			if (!Test->TestTrue(TEXT("host begins an independent confirmation before a participant leaves"),
				ServerMode->BeginAltarConfirmation(Altar.Get(), HostController.Get(), LifecycleRequestId))) return true;
			Stage = 6;
			return false;
		}

		/** 在名单固定后的等待阶段调用正式 GameMode Logout；玩家离局必须中止本轮而不靠 Tick 扫描、倒地状态或距离变化推断。 */
		bool VerifyPlayerLeaveCancels()
		{
			WaitingFor = TEXT("confirmation waiting before authority Logout of the second remote player");
			const FCatAltarConfirmationSnapshot& Snapshot = ServerMode->GetRunPublicState().AltarConfirmation;
			if (!bIssuedLifecycleLogout)
			{
				if (Snapshot.State != ECatAltarConfirmationState::Waiting || Snapshot.RequestId != LifecycleRequestId
					|| Snapshot.Participants.Num() != 3) return false;
				bIssuedLifecycleLogout = true;
				ServerMode->Logout(ServerClientTwoController.Get());
				return false;
			}
			if (Snapshot.State != ECatAltarConfirmationState::Cancelled || Snapshot.RequestId != LifecycleRequestId) return false;
			if (!Test->TestFalse(TEXT("participant departure does not start the formal transition"), ServerMode->GetRunPublicState().DayTransition.bActive)
				|| !Test->TestTrue(TEXT("participant departure keeps uncommitted ground fish"), OfferingFish.IsValid() && !OfferingFish->IsActorBeingDestroyed())) return true;
			Stage = 7;
			return false;
		}

		/** 从已经收敛为两名 Active 玩家的房间开始单独的超时轮，并断言服务器公布的窗口仍是产品规定的约三十秒。 */
		bool BeginTimeoutRequest()
		{
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			if (!Test->TestTrue(TEXT("timeout confirmation begins during a playable NormalNight"),
				Run.Phase.Phase == ECatRunPhase::NormalNight)) return true;
			TimeoutRequestDayIndex = Run.Phase.DayIndex;
			TimeoutRequestId = FGuid::NewGuid();
			if (!Test->TestTrue(TEXT("host begins a confirmation after the departed player was removed from Active admission"),
				ServerMode->BeginAltarConfirmation(Altar.Get(), HostController.Get(), TimeoutRequestId))) return true;
			const FCatAltarConfirmationSnapshot& Snapshot = ServerMode->GetRunPublicState().AltarConfirmation;
			if (!Test->TestEqual(TEXT("post-leave request contains the remaining two Active players"), Snapshot.Participants.Num(), 2)) return true;
			const double RemainingSeconds = Snapshot.DeadlineServerTimeSeconds - ServerWorld->GetTimeSeconds();
			if (!Test->TestTrue(TEXT("server publishes the configured thirty-second confirmation deadline"), RemainingSeconds > 28.0 && RemainingSeconds <= 30.5)) return true;
			Stage = 8;
			return false;
		}

		/** 等待权威计时器自己取消请求，再保留取消窗口超过产品两秒提示期，验证 Idle 清理、窗口收起和超时请求绝不进入自己的翻天。 */
		bool VerifyServerTimeoutCancels()
		{
			WaitingFor = TEXT("the authority thirty-second altar confirmation timeout and its two-second cancellation presentation");
			const FCatAltarConfirmationSnapshot& Snapshot = ServerMode->GetRunPublicState().AltarConfirmation;
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			if (!Test->TestTrue(TEXT("timeout confirmation remains in its original NormalNight and does not advance the day"),
				Run.Phase.Phase == ECatRunPhase::NormalNight && Run.Phase.DayIndex == TimeoutRequestDayIndex)) return true;
			if (!bObservedTimeoutCancellation)
			{
				if (Snapshot.State != ECatAltarConfirmationState::Cancelled || Snapshot.RequestId != TimeoutRequestId
					|| !IsCancelledSnapshotReplicated(*ClientOneController) || !FindConfirmationWindow(*ClientOneController)) return false;
				if (!Test->TestTrue(TEXT("timeout cancellation retains the original ground fish"), OfferingFish.IsValid() && !OfferingFish->IsActorBeingDestroyed())
					|| !Test->TestFalse(TEXT("timeout confirmation never routes its RequestId into a formal day transition"),
						Run.DayTransition.RequestId == TimeoutRequestId)) return true;
				bObservedTimeoutCancellation = true;
				TimeoutCancellationObservedAtSeconds = FPlatformTime::Seconds();
				return false;
			}

			if (FPlatformTime::Seconds() - TimeoutCancellationObservedAtSeconds < CancellationPresentationSeconds) return false;
			if (Snapshot.State != ECatAltarConfirmationState::Idle || FindConfirmationWindow(*ClientOneController)) return false;
			if (!Test->TestFalse(TEXT("timeout cleanup never associates the timed-out request with a formal day transition"),
				Run.DayTransition.RequestId == TimeoutRequestId)) return true;
			Stage = 9;
			return false;
		}

		/** 在超时轮完全收口后建立最终两人请求；只让仍 Active 的第一远端用自身 RPC 提交，避免测试伪造服务器身份。 */
		bool BeginFinalRequest()
		{
			FinalRequestId = FGuid::NewGuid();
			if (!Test->TestTrue(TEXT("host begins final two-player altar confirmation"),
				ServerMode->BeginAltarConfirmation(Altar.Get(), HostController.Get(), FinalRequestId))) return true;
			Stage = 10;
			return false;
		}

		/** 确认最终名单已复制后由唯一仍 Active 的远端确认；发起者的自动确认和这一次 RPC 应恰好触发正式翻天。 */
		bool ConfirmFinalRequestFromRemainingRemotePlayer()
		{
			WaitingFor = TEXT("final request replication to the remaining remote player before its F8 RPC");
			const FCatAltarConfirmationSnapshot& Snapshot = ServerMode->GetRunPublicState().AltarConfirmation;
			if (!bSentFinalConfirmation)
			{
				if (Snapshot.State != ECatAltarConfirmationState::Waiting || Snapshot.RequestId != FinalRequestId
					|| Snapshot.Participants.Num() != 2 || CountConfirmed(Snapshot) != 1
					|| !IsWaitingSnapshotReplicated(*ClientOneController, FinalRequestId, 2)) return false;
				bSentFinalConfirmation = true;
				if (!Test->TestTrue(TEXT("final F8 from the remaining remote player is accepted by the shared input entry"),
					ClientOneController->TrySetAltarConfirmationFromKey(EKeys::F8))) return true;
				return false;
			}
			Stage = 11;
			return false;
		}

		/** 验证接受快照和 DayTransition 使用同一 RequestId；随后重复 F8，下一帧仍只保留同一过场且未在淡出前消费供品。 */
		bool VerifyAcceptedRequestStartsExactlyOneTransition()
		{
			WaitingFor = TEXT("accepted confirmation routed exactly once into the existing day-transition entry");
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			if (Run.AltarConfirmation.State != ECatAltarConfirmationState::Accepted || Run.AltarConfirmation.RequestId != FinalRequestId
				|| !Run.DayTransition.bActive || Run.DayTransition.RequestId != FinalRequestId) return false;
			if (!bSentPostAcceptanceDuplicate)
			{
				const ACatfishingGameState* ClientState = ClientOneController->GetWorld()
					? ClientOneController->GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
				if (!ClientState || ClientState->GetRunPublicState().AltarConfirmation.State != ECatAltarConfirmationState::Accepted) return false;
				bSentPostAcceptanceDuplicate = true;
				if (!Test->TestFalse(TEXT("F8 is not routed again after the accepted snapshot closes the confirmation write path"),
					ClientOneController->TrySetAltarConfirmationFromKey(EKeys::F8))) return true;
				return false;
			}
			if (!Test->TestEqual(TEXT("post-acceptance F8 cannot replace the active formal transition request"),
				ServerMode->GetRunPublicState().DayTransition.RequestId, FinalRequestId)
				|| !Test->TestTrue(TEXT("formal transition remains active only after the accepted confirmation"),
					ServerMode->GetRunPublicState().DayTransition.bActive)
				|| !Test->TestTrue(TEXT("fish remains present during the configured pre-commit fade"),
					OfferingFish.IsValid() && !OfferingFish->IsActorBeingDestroyed())) return true;
			Stage = 12;
			return false;
		}

		/** 等待既有过场自行提交和收口，检查同一请求只消费一次、散鱼消失、两端鱼护库存清空并释放输入锁；随后让最后远端正式离局，验证单人发起会立即接受。 */
		bool VerifyCommittedTransitionAndBeginSinglePlayerRequest()
		{
			WaitingFor = TEXT("existing day transition commit, fish consumption, phase advance and local input-lock release");
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			const ACatfishingGameState* ClientState = ClientOneController->GetWorld()
				? ClientOneController->GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
			if (!Run.DayTransition.bCommitted || Run.DayTransition.RequestId != FinalRequestId || Run.DayTransition.bActive
				|| OfferingFish.IsValid() || !ClientState || ClientState->GetRunPublicState().DayTransition.bActive
				|| HostController->IsDayTransitionInputBlocked() || ClientOneController->IsDayTransitionInputBlocked()) return false;
			// 等服务器鱼护和按同名 Actor 找到的远端鱼护库存一起清空，不能用结算回执代替库存复制到达。
			if (!OfferingGuard.IsValid() || !OfferingGuard->IsGrounded() || OfferingGuard->GetFishInventoryComponent()->HasItemAtSlot(0)) return false;
			bool bRemoteGuardEmpty = false;
			for (TActorIterator<ACatFishGuardActor> It(ClientOneController->GetWorld()); It; ++It)
			{
				if (It->GetFName() == OfferingGuard->GetFName()) bRemoteGuardEmpty = It->IsGrounded() && !It->GetFishInventoryComponent()->HasItemAtSlot(0);
			}
			if (!bRemoteGuardEmpty) return false;
			if (!bIssuedSinglePlayerLogout)
			{
				if (!Test->TestTrue(TEXT("committed transition reaches the next playable day or an explicit settlement end state"),
					Run.Phase.Phase == ECatRunPhase::DayActive || Run.Phase.Phase == ECatRunPhase::SuccessSettlementNight
					|| Run.Phase.Phase == ECatRunPhase::FailureSettlementNight)) return true;
				bIssuedSinglePlayerLogout = true;
				ServerMode->Logout(ServerClientOneController.Get());
				return false;
			}
			if (ServerMode->IsControllerActive(ServerClientOneController.Get())) return false;
			if (Run.Phase.Phase != ECatRunPhase::NormalNight)
			{
				if (Run.Phase.Phase == ECatRunPhase::DayActive) ServerMode->ApplyDebugSkipToNight();
				return false;
			}
			if (!OfferingFish.IsValid() && !SeedGroundOffering(*Altar.Get())) return true;
			SinglePlayerRequestId = FGuid::NewGuid();
			if (!Test->TestTrue(TEXT("the remaining host can begin a single-player confirmation"),
				ServerMode->BeginAltarConfirmation(Altar.Get(), HostController.Get(), SinglePlayerRequestId))) return true;
			Stage = 13;
			return false;
		}

		/** 单人轮创建后立即读取服务器公开快照；发起者默认确认且唯一名单项完成时必须直接进入既有翻天而不等待三十秒。 */
		bool VerifySinglePlayerImmediatelyAccepted()
		{
			WaitingFor = TEXT("single-player confirmation immediate acceptance and existing day-transition handoff");
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			if (Run.AltarConfirmation.State != ECatAltarConfirmationState::Accepted || Run.AltarConfirmation.RequestId != SinglePlayerRequestId
				|| Run.AltarConfirmation.Participants.Num() != 1 || !HasParticipant(Run.AltarConfirmation, HostPlayerState.Get(), true)
				|| !Run.DayTransition.bActive || Run.DayTransition.RequestId != SinglePlayerRequestId) return false;
			Stage = 14;
			return false;
		}

		/** 等待单人轮沿同一条既有过场完成提交；测试在散鱼已消费、鱼护本体仍有效且库存清空时收口，避免 EndPIE 销毁 Actor 被误记成业务失败。 */
		bool VerifySinglePlayerTransitionCommitted()
		{
			WaitingFor = TEXT("single-player day transition commit and cleanup before EndPIE");
			const FCatRunPublicState& Run = ServerMode->GetRunPublicState();
			if (!Run.DayTransition.bCommitted || Run.DayTransition.RequestId != SinglePlayerRequestId || Run.DayTransition.bActive
				|| OfferingFish.IsValid() || HostController->IsDayTransitionInputBlocked()
				|| !OfferingGuard.IsValid() || OfferingGuard->GetFishInventoryComponent()->HasItemAtSlot(0)) return false;
			Test->AddInfo(FString::Printf(TEXT("Event=formal_altar_confirmation Result=RemoteConfirmWithdrawTimeoutLifecycleCommitAndSinglePlayer RequestId=%s"),
				*SinglePlayerRequestId.ToString(EGuidFormats::DigitsWithHyphens)));
			return true;
		}

		/** 在祭坛供品半径中放入项目真实散鱼和带库存鱼护；取消路径以散鱼仍在证明未提前消费，正式提交后用鱼护库存清空证明整批结算。 */
		bool SeedGroundOffering(ACatAltarActor& InAltar)
		{
			UCatFishDefinition* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatFishDefinition>(35);
			if (!Test->TestNotNull(TEXT("formal SilvermoonTrout definition resolves for altar offering"), Definition)) return false;
			ACatFishPickupActor* Fish = ServerWorld->SpawnActor<ACatFishPickupActor>(InAltar.GetActorLocation() + FVector(50.0f, 0.0f, 40.0f), FRotator::ZeroRotator);
			if (!Test->TestNotNull(TEXT("server creates a real available fish at the altar"), Fish)) return false;
			UCatFishInventoryItemInstance* Item = NewObject<UCatFishInventoryItemInstance>(Fish);
			Item->SetItemDefinition(Definition);
			Item->SetRuntimeOwnerActor(Fish);
			if (!Test->TestTrue(TEXT("authority initializes the offering fish identity and weight"),
				Item->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("AltarConfirmationTest"), 2.5))
				|| !Test->TestTrue(TEXT("world fish accepts the initialized formal inventory instance"), Fish->InitializeFromInventoryFromAuthority(Item, 1)))
			{
				Fish->Destroy();
				return false;
			}
			if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Fish->GetRootComponent()))
			{
				// 供品是确认与过场边界的观察对象；暂停这条测试鱼的刚体，避免无关物理下落移出祭坛半径掩盖确认语义。
				RootPrimitive->SetSimulatePhysics(false);
			}
			Fish->bAlwaysRelevant = true;
			Fish->ForceNetUpdate();
			OfferingFish = Fish;
			// 散鱼与鱼护共同参与同一正式结算；每轮复用已清空的鱼护，只新增这一轮要被整批消费的真实鱼实例。
			// 放在左侧保留玩家到祭坛以及右侧配置失败祭坛的真实视线，不能让测试供品挡住发起交互。
			if (!OfferingGuard.IsValid()) OfferingGuard = ServerWorld->SpawnActor<ACatFishGuardActor>(InAltar.GetActorLocation() + FVector(0, -120, 40), FRotator::ZeroRotator);
			if (!Test->TestNotNull(TEXT("正式供品范围内生成地面鱼护"), OfferingGuard.Get())) return false;
			UCatInventoryComponent* GuardInventory = OfferingGuard->GetFishInventoryComponent();
			UCatFishInventoryItemInstance* GuardFish = NewObject<UCatFishInventoryItemInstance>(OfferingGuard.Get());
			GuardFish->SetItemDefinition(Definition);
			GuardFish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("AltarGuardNetwork"), 2.5);
			if (!Test->TestTrue(TEXT("鱼护装入本轮原鱼实例"), GuardInventory && GuardInventory->AddItemInstance(GuardFish, 1))) return false;
			OfferingGuard->bAlwaysRelevant = true;
			OfferingGuard->ForceNetUpdate();
			return true;
		}

		/** 按 PlayerState 指针检查公开名单中的确认值；名单是权威快照，测试不从局部集合或 UI 文本反推人数。 */
		static bool HasParticipant(const FCatAltarConfirmationSnapshot& Snapshot, const APlayerState* PlayerState, const bool bExpectedConfirmed)
		{
			for (const FCatAltarConfirmationParticipant& Participant : Snapshot.Participants)
			{
				if (Participant.PlayerState == PlayerState) return Participant.bConfirmed == bExpectedConfirmed;
			}
			return false;
		}

		/** 从唯一公开参与者数组计算确认人数；测试刻意不依赖已废弃的祭坛计数或另一份服务器集合。 */
		static int32 CountConfirmed(const FCatAltarConfirmationSnapshot& Snapshot)
		{
			int32 Count = 0;
			for (const FCatAltarConfirmationParticipant& Participant : Snapshot.Participants) Count += Participant.bConfirmed ? 1 : 0;
			return Count;
		}

		/** 通过本机 Client GameState 检查等待快照已真正复制；客户端 Controller 不能直接读取服务器 RunPublicState 代替这个断言。 */
		static bool IsWaitingSnapshotReplicated(const ACatfishingPlayerController& Controller, const FGuid RequestId, const int32 ExpectedParticipants)
		{
			const ACatfishingGameState* State = Controller.GetWorld() ? Controller.GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
			const FCatAltarConfirmationSnapshot* Snapshot = State ? &State->GetRunPublicState().AltarConfirmation : nullptr;
			return Snapshot && Snapshot->State == ECatAltarConfirmationState::Waiting && Snapshot->RequestId == RequestId
				&& Snapshot->Participants.Num() == ExpectedParticipants;
		}

		/** 检查取消结果已在远端可见；取消原因由 UI 自己展示，测试这里只确认状态机没有保留 Waiting 写口。 */
		static bool IsCancelledSnapshotReplicated(const ACatfishingPlayerController& Controller)
		{
			const ACatfishingGameState* State = Controller.GetWorld() ? Controller.GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
			return State && State->GetRunPublicState().AltarConfirmation.State == ECatAltarConfirmationState::Cancelled;
		}

		/** 按远端 PlayerState Id 解析服务器正式 Controller；避免 Actor 枚举顺序或窗口顺序被误当成网络身份。 */
		static ACatfishingPlayerController* FindServerControllerForClient(UWorld& World, const ACatfishingPlayerController& ClientController)
		{
			const APlayerState* ClientPlayerState = ClientController.GetPlayerState<APlayerState>();
			for (TActorIterator<ACatfishingPlayerController> It(&World); It; ++It)
			{
				const APlayerState* CandidatePlayerState = It->GetPlayerState<APlayerState>();
				if (ClientPlayerState && CandidatePlayerState && ClientPlayerState->GetPlayerId() == CandidatePlayerState->GetPlayerId()) return *It;
			}
			return nullptr;
		}

		/** 查找指定 LocalPlayer 已入视口的正式确认 WBP；按资产类和所有者筛选，不能用测试自建的替代 Widget 伪造成功。 */
		static UUserWidget* FindConfirmationWindow(ACatfishingPlayerController& Controller)
		{
			UClass* ConfirmationClass = LoadClass<UUserWidget>(nullptr,
				TEXT("/Game/UI/Run/WBP_CatAltarConfirmation.WBP_CatAltarConfirmation_C"));
			if (!ConfirmationClass) return nullptr;
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&Controller, Widgets, ConfirmationClass, false);
			for (UUserWidget* Widget : Widgets)
			{
				if (Widget && Widget->GetOwningPlayer() == &Controller && Widget->IsInViewport()) return Widget;
			}
			return nullptr;
		}

		/** 打开第一远端自己的正式背包 WBP 并把 Slate 焦点放到根页；后续 F8/F9 必须经该页的 PreviewKeyDown 转交，不直接调用 Controller。 */
		bool OpenFocusedInventory(ACatfishingPlayerController& Controller)
		{
			ACatCharacter* Character = Cast<ACatCharacter>(Controller.GetPawn());
			UCatInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
			ULocalPlayer* LocalPlayer = Controller.GetLocalPlayer();
			UCatLocalPlayerUISubsystem* UI = LocalPlayer ? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
			UClass* InventoryClass = LoadClass<UCatInventoryWidget>(nullptr,
				TEXT("/Game/UI/Inventory/WBP_CatCampInventory.WBP_CatCampInventory_C"));
			if (!Inventory || !UI || !InventoryClass || !UI->OpenInventory(Inventory, InventoryClass)) return false;
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&Controller, Widgets, UCatInventoryWidget::StaticClass(), false);
			for (UUserWidget* Widget : Widgets)
			{
				UCatInventoryWidget* Candidate = Cast<UCatInventoryWidget>(Widget);
				if (!Candidate || Candidate->GetOwningPlayer() != &Controller || Candidate->GetInventoryContext() != Inventory || !Candidate->IsInViewport()) continue;
				InputForwardingInventory = Candidate;
				Candidate->SetKeyboardFocus();
				FSlateApplication::Get().SetKeyboardFocus(Candidate->TakeWidget(), EFocusCause::SetDirectly);
				return Candidate->HasKeyboardFocus();
			}
			return false;
		}

		/** 把真实 FKeyEvent 交给 Slate 当前焦点路由；已聚焦库存根会先收到 PreviewKeyDown，返回值表明它已消费并转交确认键。 */
		bool DispatchFocusedInventoryKey(const FKey& Key) const
		{
			UCatInventoryWidget* Inventory = InputForwardingInventory.Get();
			if (!Inventory || !Inventory->HasKeyboardFocus()) return false;
			const FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
			return FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);
		}

		/** 在第一远端附近生成已复制的正式商店摊位，并由该端的 Interact/PageController 打开项目 WBP 后聚焦根页；不会自行创建 Widget。 */
		bool OpenFocusedShop(ACatfishingPlayerController& Controller)
		{
			if (!ServerShopKiosk.IsValid())
			{
				ACatCharacter* ServerCharacter = ServerClientOneController.IsValid() ? Cast<ACatCharacter>(ServerClientOneController->GetPawn()) : nullptr;
				if (!ServerCharacter) return false;
				ShopKioskLocation = ServerCharacter->GetActorLocation() + ServerCharacter->GetActorRightVector() * 100.0f;
				ACatShopKioskActor* SpawnedKiosk = ServerWorld->SpawnActor<ACatShopKioskActor>(ShopKioskLocation, FRotator::ZeroRotator);
				if (!Test->TestNotNull(TEXT("server spawns a replicated formal shop kiosk beside the first remote player"), SpawnedKiosk)) return false;
				SpawnedKiosk->bAlwaysRelevant = true;
				SpawnedKiosk->ForceNetUpdate();
				ServerShopKiosk = SpawnedKiosk;
			}

			ACatShopKioskActor* ClientKiosk = nullptr;
			for (TActorIterator<ACatShopKioskActor> It(Controller.GetWorld()); It; ++It)
			{
				if (It->GetActorLocation().Equals(ShopKioskLocation, 1.0f))
				{
					ClientKiosk = *It;
					break;
				}
			}
			UCatShopInteractionComponent* ShopInteraction = ClientKiosk ? ClientKiosk->GetShopInteraction() : nullptr;
			if (!ClientKiosk || !ShopInteraction) return false;
			if (!ShopInteraction->IsShopOpen() && !ClientKiosk->Interact_Implementation(&Controller, FGuid::NewGuid())) return false;

			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&Controller, Widgets, UCatShopWidget::StaticClass(), false);
			for (UUserWidget* Widget : Widgets)
			{
				UCatShopWidget* Candidate = Cast<UCatShopWidget>(Widget);
				if (!Candidate || Candidate->GetOwningPlayer() != &Controller || !Candidate->IsInViewport()
					|| Candidate->GetClass()->GetPathName() != TEXT("/Game/UI/Shop/WBP_CatShop.WBP_CatShop_C")) continue;
				InputForwardingShop = Candidate;
				Candidate->SetKeyboardFocus();
				FSlateApplication::Get().SetKeyboardFocus(Candidate->TakeWidget(), EFocusCause::SetDirectly);
				return Candidate->HasKeyboardFocus();
			}
			return false;
		}

		/** 把真实 FKeyEvent 交给商店根页当前焦点；返回值证明 F8/F9 被 PreviewKeyDown 消费并送入同一 Controller 确认入口。 */
		bool DispatchFocusedShopKey(const FKey& Key) const
		{
			UCatShopWidget* Shop = InputForwardingShop.Get();
			if (!Shop || !Shop->HasKeyboardFocus()) return false;
			const FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
			return FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);
		}

		/** 验证两端 WBP 的必需绑定均实际生成且有可见文本，等滑入完成后从各自真实游戏窗口截取最终 Slate 组合画面。 */
		bool VerifyAndCaptureConfirmationWindows()
		{
			bool bAllPanelsLaidOut = true;
			for (ACatfishingPlayerController* Controller : {ClientOneController.Get(), ClientTwoController.Get()})
			{
				UUserWidget* Widget = Controller ? FindConfirmationWindow(*Controller) : nullptr;
				UBorder* Panel = Widget ? Cast<UBorder>(Widget->GetWidgetFromName(TEXT("ConfirmationPanel"))) : nullptr;
				UTextBlock* Initiator = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("InitiatorTextBlock"))) : nullptr;
				UTextBlock* Countdown = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CountdownTextBlock"))) : nullptr;
				UTextBlock* Count = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("ConfirmationCountTextBlock"))) : nullptr;
				UTextBlock* Own = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("OwnConfirmationTextBlock"))) : nullptr;
				UTextBlock* Reason = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CancelReasonTextBlock"))) : nullptr;
				if (!Widget || !Panel || !Initiator || !Countdown || !Count || !Own || !Reason || !Panel->IsVisible()
					|| Initiator->GetText().IsEmpty() || Countdown->GetText().IsEmpty() || Count->GetText().IsEmpty() || Own->GetText().IsEmpty()) return false;
				const FVector2D PanelSize = Panel->GetCachedGeometry().GetLocalSize();
				bAllPanelsLaidOut &= PanelSize.X > 1.0f && PanelSize.Y > 1.0f;
			}
			if (!bAllPanelsLaidOut)
			{
				ConfirmationWindowLayoutReadyAtSeconds = 0.0;
				return false;
			}
			if (ConfirmationWindowLayoutReadyAtSeconds <= 0.0)
			{
				ConfirmationWindowLayoutReadyAtSeconds = FPlatformTime::Seconds();
				return false;
			}
			if (FPlatformTime::Seconds() - ConfirmationWindowLayoutReadyAtSeconds < ConfirmationSlideInSeconds) return false;

			for (ACatfishingPlayerController* Controller : {ClientOneController.Get(), ClientTwoController.Get()})
			{
				UGameViewportClient* Viewport = Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->ViewportClient : nullptr;
				const TSharedPtr<SWindow> Window = Viewport ? Viewport->GetWindow() : nullptr;
				TArray<FColor> Pixels;
				FIntVector Size = FIntVector::ZeroValue;
				if (!Test->TestTrue(TEXT("formal altar confirmation actual game window can be captured without activating or foregrounding it"),
					Window.IsValid() && FSlateApplication::IsInitialized() && FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size)
					&& Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y)) return false;
				int32 NonBlackPixelCount = 0;
				for (const FColor& Pixel : Pixels)
				{
					if (Pixel.R > 8 || Pixel.G > 8 || Pixel.B > 8) ++NonBlackPixelCount;
				}
				if (!Test->TestTrue(TEXT("formal altar confirmation window screenshot is not the empty black capture"),
					NonBlackPixelCount >= FMath::Max(512, static_cast<int32>(Pixels.Num() / 200)))) return false;
				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
				IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("AltarConfirmation")), true);
				const FString ScreenshotPath = FPaths::ProjectSavedDir() / TEXT("AltarConfirmation")
					/ FString::Printf(TEXT("%s.png"), Controller == ClientOneController.Get() ? TEXT("ClientOne") : TEXT("ClientTwo"));
				if (!Test->TestTrue(TEXT("formal altar confirmation screenshot is saved"), FFileHelper::SaveArrayToFile(Png, *ScreenshotPath))) return false;
			}
			return true;
		}

		/** 该正式网络用例需要的远端客户端数；加 Listen Server 后形成三名 Active 玩家，足以独立覆盖 F8 与 F9 而不提前通过。 */
		static constexpr int32 RequiredRemoteClientCount = 2;
		/** 用例整体最长等待秒数；包含真实三十秒确认超时、两秒取消提示收口和运行期复制缓冲，超出即报告当前阶段而不是给出假绿。 */
		static constexpr double TimeoutSeconds = 75.0;
		/** 正式确认窗口滑入完成前所需的最小观察秒数；测试只等待表现动画，不参与服务器倒计时和玩法裁决。 */
		static constexpr double ConfirmationSlideInSeconds = 0.25;
		/** 取消窗口按产品规则展示的最小秒数再加少量调度余量；超时测试只在此后要求公开快照回到 Idle。 */
		static constexpr double CancellationPresentationSeconds = 2.2;
		/** 首次开始更新的单调时间；只用于 Automation 防挂死，不参与产品倒计时裁决。 */
		double StartedAtSeconds = 0.0;
		/** 两端确认面板同时取得非零 Slate 布局的单调时间；截图只在此后越过滑入动画，避免保存未绘制黑帧。 */
		double ConfirmationWindowLayoutReadyAtSeconds = 0.0;
		/** 服务器超时取消真正可见于第一远端的单调时间；用于让产品的两秒取消说明自行收起后再创建下一轮。 */
		double TimeoutCancellationObservedAtSeconds = 0.0;
		/** 当前状态机步骤；每步仅在前一条真实网络状态收敛后推进，避免本地调用冒充复制完成。 */
		int32 Stage = 0;
		/** 已验证主动取消的身份数；零为房主，一为远端，两者通过后进入原有撤回、超时和结算回归。 */
		int32 InitiatorCancellationRounds = 0;
		/** 主动取消输入的本机单调时钟秒数；给复制留一秒缓冲，同时拒绝把两秒原因停留误算成立即关闭。 */
		double InitiatorCancellationSentAtSeconds = 0.0;
		/** Automation 框架提供的断言接收者；所有异步阶段把失败写回同一测试实例，不跨线程或跨世界持有断言状态。 */
		FAutomationTestBase* Test = nullptr;
		/** 超时错误中展示的当前等待条件；帮助区分 PIE 装配、复制、业务 Timer 和 UI 资产失败。 */
		FString WaitingFor;
		/** 房主所在 Listen Server World；祭坛生成、权威状态读取和正式 Logout 均限定到这里。 */
		TWeakObjectPtr<UWorld> ServerWorld;
		/** Listen Server 上的唯一权威 GameMode；所有确认裁决都通过它的公开入口，不复刻业务规则。 */
		TWeakObjectPtr<ACatfishingGameModeBase> ServerMode;
		/** 房主的本地 Controller；它发起每轮现场交互并自动确认。 */
		TWeakObjectPtr<ACatfishingPlayerController> HostController;
		/** 第一远端本地 Controller；它实际发送重复 F8、F9、过期请求和最终 F8 RPC。 */
		TWeakObjectPtr<ACatfishingPlayerController> ClientOneController;
		/** 第二远端本地 Controller；第一轮作为远处倒地参与者存在，后续通过正式 Logout 离开名单。 */
		TWeakObjectPtr<ACatfishingPlayerController> ClientTwoController;
		/** 第一远端在服务器的身份对应物；初始快照和 Active 资格断言用它的 PlayerState 关联。 */
		TWeakObjectPtr<ACatfishingPlayerController> ServerClientOneController;
		/** 第二远端在服务器的身份对应物；生命周期阶段调用 GameMode::Logout 模拟正式离局。 */
		TWeakObjectPtr<ACatfishingPlayerController> ServerClientTwoController;
		/** 发起者在服务器的 PlayerState；参与者数组按 PlayerState 保存，测试以它判定自动确认。 */
		TWeakObjectPtr<APlayerState> HostPlayerState;
		/** 第一远端在服务器的 PlayerState；用于确认/撤回状态的权威名单断言。 */
		TWeakObjectPtr<APlayerState> ClientOnePlayerState;
		/** 第二远端在服务器的 PlayerState；用于证明倒地且远处玩家仍在第一轮名单。 */
		TWeakObjectPtr<APlayerState> ClientTwoPlayerState;
		/** 本回归临时生成的祭坛；每轮 Begin 都提交此同一对象，析构时销毁并让 EndPIE 回收关联计时器。 */
		TWeakObjectPtr<ACatAltarActor> Altar;
		/** 首个祭坛的世界位置；主动销毁它后在同一交互距离重建后续用例所需的正式祭坛，不把 Actor 失效误当业务终止。 */
		FVector AltarSpawnLocation = FVector::ZeroVector;
		/** 服务器生成的正式商店摊位；它只为第一远端的真实 WBP 焦点路径提供世界入口，析构时由 PIE 回收。 */
		TWeakObjectPtr<ACatShopKioskActor> ServerShopKiosk;
		/** 服务器摊位的稳定位置；客户端用复制后的同位置 Actor 查找自己的本地交互组件和 PageController。 */
		FVector ShopKioskLocation = FVector::ZeroVector;
		/** 同一条真实地面散鱼供品；每次取消后检查它仍有效，最终确认前仍存在以证明等待阶段没有扣鱼。 */
		TWeakObjectPtr<ACatFishPickupActor> OfferingFish;
		/** 参与同批献祭的原地面鱼护；正式提交后本体应留在地面且两端真实库存清空，析构只回收这个测试 Actor。 */
		TWeakObjectPtr<ACatFishGuardActor> OfferingGuard;
		/** 已取得键盘焦点的正式库存 WBP；首轮通过它的 PreviewKeyDown 把 F8/F9 转交给 Controller，析构时由 LocalPlayer UI 正式回收。 */
		TWeakObjectPtr<UCatInventoryWidget> InputForwardingInventory;
		/** 已取得键盘焦点的正式商店 WBP；库存撤回之后由它的 PreviewKeyDown 再转交同一玩家的 F8/F9，不构造替代界面。 */
		TWeakObjectPtr<UCatShopWidget> InputForwardingShop;
		/** 第一轮确认标识；用于确认重复 F8/F9 与旧 ID 拒绝都指向同一服务器轮次。 */
		FGuid FirstRequestId;
		/** 生命周期取消轮的标识；与第一轮和超时轮分离，避免缓存或终态复用掩盖退出处理。 */
		FGuid LifecycleRequestId;
		/** 超时轮的标识；权威 Timer 必须仅取消这一轮。 */
		FGuid TimeoutRequestId;
		/** 配置预检拒绝轮的标识；它必须直接得到取消快照，不能覆盖后续真实生命周期取消的请求。 */
		FGuid ConfigFailureRequestId;
		/** 超时轮开始时的权威天数；等待和两秒取消展示期必须保持这个 DayIndex，防止超时错误翻天。 */
		int32 TimeoutRequestDayIndex = INDEX_NONE;
		/** 最终接受轮的标识；接受快照和既有 DayTransition 必须同时等于它。 */
		FGuid FinalRequestId;
		/** 最后一名 Active 玩家自动确认的单人轮标识；只用于断言它无需等待三十秒便进入正式过场。 */
		FGuid SinglePlayerRequestId;
		/** 是否已发送一次随机旧请求；后续观察帧只读回同一人数，避免重复发送制造竞态。 */
		bool bSentStaleRequest = false;
		/** 是否已由远处倒地的第二远端发送 F8；它证明固定名单保留倒地者且远程确认不复核空间资格。 */
		bool bSentDownedConfirmation = false;
		/** 是否已由远处倒地的第二远端发送 F9；撤回后人数必须回到只有发起者确认的等待状态。 */
		bool bSentDownedWithdrawal = false;
		/** 是否已由正式商店根页发送 F8；随后只等待这个 PlayerState 在公开名单变为确认，防止重复打开商店重走交互。 */
		bool bSentShopConfirmation = false;
		/** 是否已由同一正式商店根页发送 F9；它必须把同一 PlayerState 恢复为未确认，随后才测试远处倒地玩家。 */
		bool bSentShopWithdrawal = false;
		/** 旧请求发出后保留的本地 PIE 帧数；给可靠 RPC 到达服务器并被拒绝留出机会，再核对原名单没有被改写。 */
		int32 StaleObservationFrames = 0;
		/** 是否已对第二远端调用正式 Logout；防止等待期间重复执行销毁性生命周期操作。 */
		bool bIssuedLifecycleLogout = false;
		/** 是否已销毁不在当前快照中的祭坛；下一帧仍要求原确认保持 Waiting，证明 EndPlay 只收口所属请求。 */
		bool bDestroyedUnrelatedAltar = false;
		/** 是否已销毁当前快照引用的祭坛；销毁后立即重建后续用例所需对象，但必须先等这一轮变为 Cancelled。 */
		bool bDestroyedConfirmationAltar = false;
		/** 是否已执行失效供品配置的发起预检；置位后直接等待其取消快照复制，避免重复创建临时失效 Actor。 */
		bool bValidatedConfigurationFailure = false;
		/** 是否已观察到本轮超时取消的服务器与远端窗口；置位后仅等待产品提示期结束和 Idle 清理，不重做取消断言。 */
		bool bObservedTimeoutCancellation = false;
		/** 是否已由第一远端提交最终 F8；等待其权威接受前不重复创建请求。 */
		bool bSentFinalConfirmation = false;
		/** 是否已在接受后重复 F8；下一帧只校验既有 DayTransition 没有被替换或二次开始。 */
		bool bSentPostAcceptanceDuplicate = false;
		/** 是否已让第一远端正式离局；过场收口后只执行一次，留下房主验证单人立即通过。 */
		bool bIssuedSinglePlayerLogout = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatAltarConfirmationFormalNetworkTest,
	"Catfishing.Editor.Camp.AltarConfirmation.FormalThreeEndpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 配置三端 IP PIE 并排入正式地图、验证状态机、结束和恢复命令；返回 true 只代表异步回归已排队，所有结论由后续真实 RPC/复制断言给出。 */
bool FCatAltarConfirmationFormalNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("requires an idle editor and engine"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const TSharedRef<CatAltarConfirmationTests::FRestoreSettings> Restore = MakeShared<CatAltarConfirmationTests::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(3);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			// 与既有正式网络用例一致，临时固定 IP 驱动以排除本机在线平台登录状态对 PIE 回归的影响。
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatAltarConfirmationTests::FVerifyAltarConfirmation>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
