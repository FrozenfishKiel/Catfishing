#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WrapBox.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/CatFishPickupSettings.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatFishGuardInventoryItemInstance.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Interaction/Carry/CatCarryableActor.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UObject/UnrealType.h"

#include <type_traits>

// 共同携带回归首先在编译期锁住两个真实消费者的继承关系；后续运行阶段才验证复制、物理和 UI 行为。
static_assert(std::is_base_of_v<ACatCarryableActor, ACatFishGuardActor>);
static_assert(std::is_base_of_v<ACatCarryableActor, ACatFishPickupActor>);

namespace CatFishGuardCarryNetwork
{
	/** 本用例的 PIE 设置恢复命令；沿用售鱼双端用例的快照和结束顺序，不保存配置。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 在启动配置改变前保存网络模式、端数、进程选择和驱动表，供 PIE 结束后恢复。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode);
			Settings->GetPlayNumberOfClients(ClientCount);
			Settings->GetRunUnderOneProcess(bOneProcess);
			NetDrivers = GEngine->NetDriverDefinitions;
		}

		/** 等所有 PIE 世界销毁后写回快照；仍有世界时继续等待，防止结束流程覆盖恢复值。 */
		bool Update() override
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE && Context.World()) return false;
			}
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode);
			Settings->SetPlayNumberOfClients(ClientCount);
			Settings->SetRunUnderOneProcess(bOneProcess);
			GEngine->NetDriverDefinitions = NetDrivers;
			return true;
		}

	private:
		/** 测试前的 PIE 网络模式；构造读取，结束时写回，避免留下 listen server 设置。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 测试前的参与端数；构造保存，结束时恢复用户原值。 */
		int32 ClientCount = 1;
		/** 测试前的单进程选择；构造保存，结束时撤销本用例的临时选择。 */
		bool bOneProcess = true;
		/** 测试前的完整驱动定义；结束时整体恢复，包括原在线驱动与 fallback。 */
		TArray<FNetDriverDefinition> NetDrivers;
	};

	/** 正式地图上一次拾取、放置、再拾取、丢弃的双端回归；命令只由拥有角色的客户端发送。 */
	class FVerifyCarry final : public IAutomationLatentCommand
	{
	public:
		/** 保存框架断言接收者；等待预算从首次轮询开始，不计入地图加载时间。 */
		explicit FVerifyCarry(FAutomationTestBase* InTest) : Test(InTest) {}

		/** 成功、拒绝或超时退出时销毁唯一测试鱼护及其内鱼；后续 EndPIE 销毁世界，再由恢复命令还原设置。 */
		~FVerifyCarry() override
		{
			// 用例可能在晚到 Mesh 的复制窗口超时；析构先还原远端本地表现资产，不能把临时空 Mesh 留给随后 PIE 清理或失败截图。
			RestoreClientFishMesh();
			if (OccupiedViewGuard.IsValid()) OccupiedViewGuard->Destroy();
			if (ServerGuard.IsValid()) ServerGuard->Destroy();
		}

		/** 按真实网络时序推进一个连续用例：
		 * 1. 等正式登录、身体与地图地面就绪，在服务器生成原鱼护并通过既有库存装鱼。
		 * 2. 等初始复制完整才从客户端拾取；收到对应回执后检查两端归属、嘴部附着与原鱼。
		 * 3. 从客户端背包读取实际槽位和 GUID 发送 Place，等地面、扣格和固定变换收敛后再次拾取。
		 * 4. 第二次携带收敛后无参通知服务器丢弃当前携带物，分别采样两端释放后的位移，等真实刚体落稳、位置收敛且嘴空。
		 * 5. 每次落稳均由拥有客户端重新拾取同一 Actor，连续三次核对原库存、GUID、关闭物理和嘴部附着；权威角色每次移动250厘米，再等客户端角色和鱼护的世界位置共同收敛。
		 * 6. 第三次重新拾取后的移动收敛后再次丢下，保留原占嘴按钮禁用检查，并从落地原鱼护经真实 Slate 点击取出原鱼和保存前后画面。
		 * 7. 对同一条原鱼再执行三轮拥有客户端丢弃、物理落地、E 交互再拾取和250厘米移动，确认共同携带基类同时覆盖鱼护与鱼。
		 * 前提丢失或拾取、放置回执拒绝时立即带阶段报错；丢弃只观察复制结果，未收敛时继续等待并在阶段超时报告原因。 */
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StageStartedAt <= 0.0) StageStartedAt = Now;
			if (Now - StageStartedAt > 45.0)
			{
				// Drop 后重新拾取的旧实现可能只同步库存而不触发客户端附着；超时前读取实际父级和 socket，避免把该回归写成笼统的阶段等待失败。
				if (Stage == 7 && ClientGuard.IsValid() && ClientController.IsValid())
				{
					const ACatCharacter* TimedOutClientCat = Cast<ACatCharacter>(ClientController->GetPawn());
					if (TimedOutClientCat && (ClientGuard->GetAttachParentActor() != TimedOutClientCat
						|| ClientGuard->GetRootComponent()->GetAttachParent() != TimedOutClientCat->GetMesh()
						|| ClientGuard->GetRootComponent()->GetAttachSocketName() != GetDefault<UCatFishPickupSettings>()->MouthCarrySocketName))
					{
						Test->AddError(TEXT("FishGuard post-drop repick attachment failed: original client guard did not restore its mouth parent/socket."));
						return true;
					}
				}
				Test->AddError(FString::Printf(TEXT("FishGuard carry timed out: stage=%d waiting=%s"), Stage, *WaitingFor));
				return true;
			}
			if (Stage == 0) return PrepareWorlds();
			ACatCharacter* ServerCat = ServerController.IsValid() ? Cast<ACatCharacter>(ServerController->GetPawn()) : nullptr;
			ACatCharacter* ClientCat = ClientController.IsValid() ? Cast<ACatCharacter>(ClientController->GetPawn()) : nullptr;
			if (!ServerWorld.IsValid() || !ClientWorld.IsValid() || !ServerGuard.IsValid() || !ServerCat || !ClientCat)
			{
				Test->AddError(FString::Printf(TEXT("FishGuard carry lost world, original guard or player: stage=%d"), Stage));
				return true;
			}
			if (Stage == 1)
			{
				WaitingFor = TEXT("initial original guard and both frozen fish replicated");
				for (TActorIterator<ACatFishGuardActor> It(ClientWorld.Get()); It; ++It)
				{
					if (It->GetFName() == ServerGuard->GetFName()) ClientGuard = *It;
				}
				if (!ClientGuard.IsValid()) return false;
				if (!ClientFishInventory.IsValid()) ClientFishInventory = ClientGuard->GetFishInventoryComponent();
				if (!BothSidesMatch(false, false)) return false;
				RequestId = FGuid::NewGuid();
				ClientController->ServerPickUpFishGuard(ClientGuard.Get(), RequestId);
				Stage = 2;
				StageStartedAt = Now;
				return false;
			}
			if (!ClientGuard.IsValid())
			{
				Test->AddError(FString::Printf(TEXT("Original client guard disappeared: stage=%d"), Stage));
				return true;
			}

			// Drop 从各端首次解除归属后才采样，排除嘴部到释放点的瞬移；采样不依赖回执到达顺序。
			if (Stage == 6)
			{
				if (!VerifyFormalFishCarryFromGroundedGuard(*ServerCat, *ClientCat)) return false;
				ServerCarriedFish = Cast<ACatFishPickupActor>(ServerCat->GetMouthCarriedActor());
				ClientCarriedFish = Cast<ACatFishPickupActor>(ClientCat->GetMouthCarriedActor());
				if (!ServerCarriedFish.IsValid() || !ClientCarriedFish.IsValid()) return false;
				OriginalFishWorldScale = ServerCarriedFish->GetActorScale3D();
				Stage = 9;
				StageStartedAt = Now;
				StableSince = 0.0;
				return false;
			}
			if (Stage == 9)
			{
				if (!BothSidesFishMatch(true, false)) return false;
				// 正式 UI 已经把原鱼叼起；此处仍只经拥有客户端调用原有 RPC，避免测试直接操纵附件或刚体绕过网络链路。
				ClientController->ServerDropCarriedItem();
				Stage = 10;
				StageStartedAt = Now;
				StableSince = 0.0;
				FishDropSampled[0] = FishDropSampled[1] = false;
				FishDropMoved[0] = FishDropMoved[1] = false;
				return false;
			}
			if (Stage == 10)
			{
				if (!WaitForFishDropAndRequestRepick(*ServerCat, *ClientCat, Now)) return false;
				return false;
			}
			if (Stage == 11)
			{
				// 普通 E 交互没有 CampCommand 回执；由此前两端落地到本轮同一鱼嘴叼的真实复制变化证明 RPC 生效。
				if (!BothSidesFishMatch(true, false)) return false;
				if (bClientFishMeshRestorePending)
				{
					// 空 Mesh 期间只断言附件落在同一个 SkeletalMeshComponent 与 Mouth socket 名；不要求资源未加载时该 socket 可查询。
					if (Now - ClientFishMeshClearedAt < 0.3) return false;
					RestoreClientFishMesh();
					return false;
				}
				// 每轮都从两端实际世界位置取基准；鱼的相对嘴部变换正确不足以证明角色复制和共同携带均已收敛。
				FishCarryMoveStart[0] = ServerCat->GetActorLocation();
				FishCarryMoveStart[1] = ClientCat->GetActorLocation();
				FishMoveStart[0] = ServerCarriedFish->GetActorLocation();
				FishMoveStart[1] = ClientCarriedFish->GetActorLocation();
				const double Direction = FishDropRepickCount % 2 == 0 ? -1.0 : 1.0;
				FishCarryMoveTarget = FishCarryMoveStart[0] + ServerCat->GetActorForwardVector().GetSafeNormal2D() * (250.0 * Direction);
				ServerCat->SetActorLocation(FishCarryMoveTarget, false, nullptr, ETeleportType::None);
				ServerCat->ForceNetUpdate();
				ServerCarriedFish->ForceNetUpdate();
				Stage = 12;
				StageStartedAt = Now;
				StableSince = 0.0;
				return false;
			}
			if (Stage == 12)
			{
				if (!VerifyFishCarryMovement(*ServerCat, *ClientCat, Now)) return false;
				if (FishDropRepickCount == 3)
				{
					Test->AddInfo(TEXT("Event=fish_guard_carry_network Result=GuardAndFishCompleteThreeDropRepickMoveRounds Fish=OriginalTwoGUIDs_2.5kg_3.75kg Screenshots=ClientBeforeCarry,ClientAfterCarry"));
					return true;
				}
				ClientController->ServerDropCarriedItem();
				Stage = 10;
				StageStartedAt = Now;
				StableSince = 0.0;
				FishDropSampled[0] = FishDropSampled[1] = false;
				FishDropMoved[0] = FishDropMoved[1] = false;
				return false;
			}

			if (Stage == 5)
			{
				for (int32 Peer = 0; Peer < 2; ++Peer)
				{
					ACatFishGuardActor* Guard = Peer == 0 ? ServerGuard.Get() : ClientGuard.Get();
					const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Guard->GetRootComponent());
					if (!Guard->IsGrounded() || Guard->GetAttachParentActor() || !Body || !Body->IsSimulatingPhysics()) continue;
					if (!bDropSampled[Peer])
					{
						DropStart[Peer] = Guard->GetActorLocation();
						bDropSampled[Peer] = true;
					}
					else if (FVector::Dist(Guard->GetActorLocation(), DropStart[Peer]) > 2.0) bDropMoved[Peer] = true;
				}
			}
			WaitingFor = FString::Printf(TEXT("owning-client RPC receipt Request=%s"), *RequestId.ToString());
			const FCatDomainCommandResult Result = ClientController->GetLastCampCommandResult();
			if (Stage != 5 && Result.RequestId != RequestId) return false;
			if (Stage != 5 && (!Result.bCommitted || Result.Error != ECatDomainCommandError::None))
			{
				Test->AddError(FString::Printf(TEXT("FishGuard RPC rejected: stage=%d Request=%s Error=%d Committed=%d"),
					Stage, *RequestId.ToString(), int32(Result.Error), Result.bCommitted));
				return true;
			}
			if (Stage == 2 || Stage == 4 || Stage == 7)
			{
				if (!BothSidesMatch(true, false)) return false;
				if (Stage == 2 && !VerifyFormalCarryButtonDisabledForOccupiedGuard()) return false;
				if (Stage == 2 && CarryView.IsValid())
				{
					CarryView->RequestCloseInventory();
					CarryView.Reset();
					if (OccupiedViewGuard.IsValid()) OccupiedViewGuard->Destroy();
				}
				UCatInventoryComponent* Backpack = ClientCat->GetInventoryComponent();
				const int32 Slot = Backpack->FindFirstInventorySlotIndexByDefinitionId(TEXT("FishGuard"));
				const FCatInventoryEntry* Entry = Backpack->GetInventoryEntryAtSlot(Slot);
				const UCatFishGuardInventoryItemInstance* Item = Entry ? Cast<UCatFishGuardInventoryItemInstance>(Entry->Instance) : nullptr;
				WaitingFor = TEXT("client backpack FishGuard instance, slot, quantity and runtime owner");
				if (!Item || Entry->StackCount != 1 || Item->GetRuntimeOwnerActor() != ClientCat || !Item->GetItemInstanceId().IsValid()) return false;
				if (!GuardId.IsValid()) GuardId = Item->GetItemInstanceId();
				if (!Test->TestEqual(FString::Printf(TEXT("stage=%d repick preserves guard item GUID"), Stage), Item->GetItemInstanceId(), GuardId)) return true;
				const UCatInventoryComponent* ServerBackpack = ServerCat->GetInventoryComponent();
				const FCatInventoryEntry* ServerEntry = ServerBackpack->GetInventoryEntryAtSlot(
					ServerBackpack->FindInventorySlotIndexFromInstanceId(GuardId));
				WaitingFor = TEXT("authority backpack holds same GUID and original world actor");
				if (!ServerEntry || ServerEntry->StackCount != 1 || !ServerEntry->Instance
					|| ServerEntry->Instance->GetWorldActor() != ServerGuard.Get()) return false;
				if (Stage == 2)
				{
					RequestId = FGuid::NewGuid();
					ClientController->ServerReleaseInventoryItemToWorld(RequestId, ClientCat, Slot, Item->GetItemInstanceId(), 1,
						ECatInventoryWorldAction::Place);
				}
				else if (Stage == 4)
				{
					RequestId = FGuid::NewGuid();
					ClientController->ServerDropCarriedItem();
				}
				else
				{
					// 携带状态已在两端逐项确认后才移动权威角色；250厘米既超过插值噪声，也不越过本用例的200至400厘米验收窗口。
					CarryMoveStart[0] = ServerCat->GetActorLocation();
					CarryMoveStart[1] = ClientCat->GetActorLocation();
					CarryGuardMoveStart[0] = ServerGuard->GetActorLocation();
					CarryGuardMoveStart[1] = ClientGuard->GetActorLocation();
					// 三轮按前、后、前交替移动，单轮仍是250厘米，同时让最终落点留在原地面鱼护 UI 的可交互范围内。
					const double Direction = DropRepickCount % 2 == 0 ? -1.0 : 1.0;
					CarryMoveTarget = CarryMoveStart[0] + ServerCat->GetActorForwardVector().GetSafeNormal2D() * (250.0 * Direction);
					ServerCat->SetActorLocation(CarryMoveTarget, false, nullptr, ETeleportType::None);
					ServerCat->ForceNetUpdate();
					ServerGuard->ForceNetUpdate();
				}
				Stage = Stage == 7 ? 8 : Stage + 1;
				StageStartedAt = Now;
				StableSince = 0.0;
				return false;
			}
			if (Stage == 8)
			{
				// 不能只读嘴部相对变换：角色复制未跟随时，鱼护仍可能相对附着正确；这里同时要求两端角色和鱼护各自在世界空间移动并收敛。
				WaitingFor = TEXT("authority moves 200-400 cm and client cat plus attached original guard converge in world space");
				const double ServerCatDistance = FVector::Dist(ServerCat->GetActorLocation(), CarryMoveStart[0]);
				const double ClientCatDistance = FVector::Dist(ClientCat->GetActorLocation(), CarryMoveStart[1]);
				const double ServerGuardDistance = FVector::Dist(ServerGuard->GetActorLocation(), CarryGuardMoveStart[0]);
				const double ClientGuardDistance = FVector::Dist(ClientGuard->GetActorLocation(), CarryGuardMoveStart[1]);
				const bool bWorldMovementConverged = BothSidesMatch(true, false)
					&& ServerCatDistance >= 200.0 && ServerCatDistance <= 400.0
					&& FVector::Dist(ServerCat->GetActorLocation(), CarryMoveTarget) <= 2.0
					&& ClientCatDistance >= 200.0 && ClientCatDistance <= 400.0
					&& FVector::Dist(ServerCat->GetActorLocation(), ClientCat->GetActorLocation()) <= 15.0
					&& ServerGuardDistance >= 200.0 && ServerGuardDistance <= 400.0
					&& ClientGuardDistance >= 200.0 && ClientGuardDistance <= 400.0
					&& FVector::Dist(ServerGuard->GetActorLocation(), ClientGuard->GetActorLocation()) <= 15.0;
				if (!bWorldMovementConverged) { StableSince = 0.0; return false; }
				if (StableSince <= 0.0) StableSince = Now;
				if (Now - StableSince < 0.25) return false;
				ClientController->ServerDropCarriedItem();
				Stage = 5;
				StageStartedAt = Now;
				StableSince = 0.0;
				bDropSampled[0] = bDropSampled[1] = false;
				bDropMoved[0] = bDropMoved[1] = false;
				return false;
			}
			if (Stage == 3 || Stage == 5)
			{
				bool bReady = BothSidesMatch(false, Stage == 5);
				if (bReady && Stage == 5)
				{
					WaitingFor = TEXT("both peers move after physical release, settle on map ground and converge within 15 cm");
					bReady = bDropMoved[0] && bDropMoved[1];
					for (int32 Peer = 0; Peer < 2 && bReady; ++Peer)
					{
						ACatFishGuardActor* Guard = Peer == 0 ? ServerGuard.Get() : ClientGuard.Get();
						const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Guard->GetRootComponent());
						FCollisionQueryParams Query(SCENE_QUERY_STAT(CatGuardCarryDropFloor), false, Guard);
						Query.AddIgnoredActor(Peer == 0 ? ServerCat : ClientCat);
						FHitResult Hit;
						bReady = Body->GetPhysicsLinearVelocity().Size() < 10.0
							&& FVector::Dist(Guard->GetActorLocation(), DropStart[Peer]) < 1000.0
							&& Guard->GetWorld()->LineTraceSingleByChannel(Hit, Body->Bounds.Origin,
								Body->Bounds.Origin - FVector(0, 0, Body->Bounds.BoxExtent.Z + 10.0), ECC_WorldDynamic, Query)
							&& Hit.ImpactNormal.Z > 0.5;
					}
				}
				if (!bReady) { StableSince = 0.0; return false; }
				// 用持续稳定窗口过滤单帧巧合；Place 保持半秒，Drop 落稳一秒，单位均为单调时钟秒。
				if (StableSince <= 0.0) StableSince = Now;
				if (Now - StableSince < (Stage == 3 ? 0.5 : 1.0)) return false;
				if (Stage == 3)
				{
					RequestId = FGuid::NewGuid();
					ClientController->ServerPickUpFishGuard(ClientGuard.Get(), RequestId);
					Stage = 4;
					StageStartedAt = Now;
					return false;
				}
				if (DropRepickCount < 3)
				{
					// 只在物理落稳、两端都已空嘴后经拥有客户端发起 RPC；旧实现缺失此段，会在附着复制断开时被后续断言明确捕获。
					RequestId = FGuid::NewGuid();
					++DropRepickCount;
					ClientController->ServerPickUpFishGuard(ClientGuard.Get(), RequestId);
					Stage = 7;
				}
				else Stage = 6;
				StageStartedAt = Now;
				StableSince = 0.0;
				return false;
			}
			Test->AddError(FString::Printf(TEXT("Unknown fish guard carry stage=%d"), Stage));
			return true;
		}

	private:
		/** 定位真实 listen server 与唯一远端，并按有效 UniqueId 匹配权威 PC；等待正式玩法门、落地角色和空嘴。
		 * 查询角色前方现有地图地面后生成正式 BP，以实际物理根尺寸对齐地面，再经原库存入口加入两条正式鱼。
		 * 登录尚未完成时重试；资产或地面不满足时报告 stage=0，不修改角色身份、朝向、权限或地图碰撞。 */
		bool PrepareWorlds()
		{
			WaitingFor = TEXT("formal TestMap listen server and exactly one remote client");
			int32 RemoteClients = 0;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_ListenServer) ServerWorld = World;
				else if (World->GetNetMode() == NM_Client) { ClientWorld = World; ++RemoteClients; }
			}
			if (!ServerWorld.IsValid() || !ClientWorld.IsValid()) return false;
			if (!Test->TestEqual(TEXT("stage=0 exactly one remote client"), RemoteClients, 1)) return true;
			WaitingFor = TEXT("local PC, actual PlayerState UniqueId and matching authority pawn");
			ClientController = Cast<ACatfishingPlayerController>(ClientWorld->GetFirstPlayerController());
			if (!ClientController.IsValid() || !ClientController->IsLocalController() || !ClientController->GetLocalPlayer()
				|| !Cast<ACatCharacter>(ClientController->GetPawn())) return false;
			const APlayerState* ClientState = ClientController->GetPlayerState<APlayerState>();
			if (!ClientState || !ClientState->GetUniqueId().IsValid()) return false;
			for (TActorIterator<ACatfishingPlayerController> It(ServerWorld.Get()); It; ++It)
			{
				const APlayerState* State = It->GetPlayerState<APlayerState>();
				if (!It->IsLocalController() && State && State->GetUniqueId().IsValid()
					&& State->GetUniqueId()->ToString() == ClientState->GetUniqueId()->ToString()) ServerController = *It;
			}
			if (!ServerController.IsValid()) return false;
			ACatCharacter* Character = Cast<ACatCharacter>(ServerController->GetPawn());
			const ACatfishingGameModeBase* GameMode = ServerWorld->GetAuthGameMode<ACatfishingGameModeBase>();
			WaitingFor = TEXT("formal gameplay gate, standing grounded character, empty mouth and initialized backpack");
			if (!Character || !GameMode || !GameMode->CanAcceptGameplayCommand(ServerController.Get())
				|| !Character->GetConditionComponent() || Character->GetConditionComponent()->GetSnapshot().bDowned
				|| !Character->GetCharacterMovement()->IsMovingOnGround() || !Character->GetInventoryComponent()
				|| Character->GetInventoryComponent()->GetInventorySlotCount() <= 0) return false;
			if (!Test->TestTrue(TEXT("stage=0 unmodified formal character has empty mouth and configured socket"),
				!ACatFishPickupActor::FindCarriedFish(Character) && !ACatFishGuardActor::FindCarriedGuard(Character)
				&& Character->GetMesh()->DoesSocketExist(GetDefault<UCatFishPickupSettings>()->MouthCarrySocketName))) return true;
			UClass* GuardClass = LoadClass<ACatFishGuardActor>(nullptr, TEXT("/Game/Blueprint/Actors/BP_CatGuard.BP_CatGuard_C"));
			UCatFishDefinition* Definition = LoadObject<UCatFishDefinition>(nullptr,
				TEXT("/Game/Catfishing/Data/Fish/Fish_RiverPattern.Fish_RiverPattern"));
			if (!Test->TestTrue(TEXT("stage=0 formal guard and fish assets load"), GuardClass && Definition)) return true;
			const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
			FVector Candidate = Character->GetActorLocation() + Character->GetActorForwardVector().GetSafeNormal2D() * 100.0;
			Candidate.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FCollisionQueryParams Query(SCENE_QUERY_STAT(CatGuardCarrySpawnFloor), false, Character);
			FHitResult Ground;
			const double Height = Settings->PlacementHeightDifferenceCentimeters + 2.0;
			// 地面法线Z是坡度余弦；把正式配置的角度转为弧度再比较，2厘米仅扩展射线端点。
			if (!Test->TestTrue(TEXT("stage=0 existing TestMap ground is reachable in front of remote character"),
				ServerWorld->LineTraceSingleByChannel(Ground, Candidate + FVector(0, 0, Height), Candidate - FVector(0, 0, Height), ECC_WorldDynamic, Query)
				&& Ground.ImpactNormal.Z >= FMath::Cos(FMath::DegreesToRadians(Settings->PlacementSlopeDegrees)))) return true;
			ServerGuard = ServerWorld->SpawnActor<ACatFishGuardActor>(GuardClass, Ground.ImpactPoint + FVector(0, 0, 40), FRotator::ZeroRotator);
			if (!Test->TestNotNull(TEXT("stage=0 authority spawns original BP_CatGuard"), ServerGuard.Get())) return true;
			OriginalGuardWorldScale = ServerGuard->GetActorScale3D() * 0.65;
			ServerGuard->SetActorScale3D(OriginalGuardWorldScale);
			UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(ServerGuard->GetRootComponent());
			ServerFishInventory = ServerGuard->GetFishInventoryComponent();
			if (!Test->TestTrue(TEXT("stage=0 formal guard has physical root and existing FishInventory"), Body && ServerFishInventory.IsValid())) return true;
			const FBox Bounds = Body->CalcBounds(FTransform::Identity).GetBox();
			const FVector Scale = ServerGuard->GetActorScale3D();
			ServerGuard->SetActorLocation(Ground.ImpactPoint + FVector(0, 0, Bounds.GetExtent().Z * FMath::Abs(Scale.Z) + 1.0) - Bounds.GetCenter() * Scale);
			const FString OwnerId = ServerController->GetPlayerState<APlayerState>()->GetUniqueId()->ToString();
			for (const double Weight : {2.5, 3.75})
			{
				UCatFishInventoryItemInstance* Fish = NewObject<UCatFishInventoryItemInstance>(ServerGuard.Get());
				Fish->SetItemDefinition(Definition);
				const FGuid Id = FGuid::NewGuid();
				if (!Test->TestTrue(TEXT("stage=0 authority initializes independent frozen fish and original inventory accepts it"),
					Fish->InitializeFishFromAuthority(FGuid::NewGuid(), Id, OwnerId, Weight)
					&& ServerFishInventory->AddItemInstance(Fish, 1))) return true;
				FishIds.Add(Id);
				OriginalFish.Add(Fish);
			}
			ServerGuard->ForceNetUpdate();
			Stage = 1;
			StageStartedAt = FPlatformTime::Seconds();
			return false;
		}

		/** 原鱼护占嘴时，在另一个仍可交互的地面鱼护中选择鱼；等待它复制和页面布局，核对正式按钮禁用。
		 * 已叼起鱼护自己的页面会按现行生命周期关闭，因此另放一只地面鱼护作为可见 UI 夹具，不强行保留失效页面。 */
		bool VerifyFormalCarryButtonDisabledForOccupiedGuard()
		{
			ACatCharacter* ServerCat = ServerController.IsValid() ? Cast<ACatCharacter>(ServerController->GetPawn()) : nullptr;
			if (!OccupiedViewGuard.IsValid() && ServerCat)
			{
				OccupiedViewGuard = ServerWorld->SpawnActor<ACatFishGuardActor>(ServerGuard->GetClass(),
					ServerCat->GetActorLocation() + ServerCat->GetActorRightVector() * 150.0, FRotator::ZeroRotator);
				if (!Test->TestNotNull(TEXT("grounded guard for visible occupied-mouth UI"), OccupiedViewGuard.Get())) return true;
				UCatFishInventoryItemInstance* ViewFish = NewObject<UCatFishInventoryItemInstance>(OccupiedViewGuard.Get());
				ViewFish->SetItemDefinition(OriginalFish[0]->GetFishDefinition());
				OccupiedViewFishId = FGuid::NewGuid();
				if (!Test->TestTrue(TEXT("grounded UI guard contains a distinct real fish"), ViewFish->InitializeFishFromAuthority(
					FGuid::NewGuid(), OccupiedViewFishId, ServerController->PlayerState->GetUniqueId()->ToString(), 2.5)
					&& OccupiedViewGuard->GetFishInventoryComponent()->AddItemInstance(ViewFish, 1))) return true;
				OccupiedViewGuard->ForceNetUpdate();
			}
			UCatLocalPlayerUISubsystem* UI = ClientController.IsValid() && ClientController->GetLocalPlayer()
				? ClientController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
			UCatFishOnlyInventoryComponent* FishInventory = nullptr;
			for (TActorIterator<ACatFishGuardActor> It(ClientWorld.Get()); It; ++It)
			{
				if (OccupiedViewGuard.IsValid() && It->GetFName() == OccupiedViewGuard->GetFName()) FishInventory = It->GetFishInventoryComponent();
			}
			WaitingFor = TEXT("grounded occupied-mouth UI guard and its fish replicate");
			if (!UI || !FishInventory || FishInventory->FindInventorySlotIndexFromInstanceId(OccupiedViewFishId) == INDEX_NONE) return false;
			if (!CarryView.IsValid())
			{
				UClass* ViewClass = LoadClass<UCatInventoryWidget>(nullptr,
					TEXT("/Game/UI/Inventory/WBP_CatFishGuardInventory.WBP_CatFishGuardInventory_C"));
				WaitingFor = TEXT("formal fish-guard WBP with CarryButton opens for the remote client");
				if (!ViewClass || !UI->OpenInventory(FishInventory, ViewClass)) return false;
				CarryView = FindFormalInventoryView(*ClientController, FishInventory);
				return false;
			}
			UCatInventoryWidget* View = CarryView.Get();
			UWrapBox* Slots = View ? Cast<UWrapBox>(View->GetWidgetFromName(TEXT("InventorySlotWrapBox"))) : nullptr;
			const int32 FishSlotIndex = FishInventory->FindInventorySlotIndexFromInstanceId(OccupiedViewFishId);
			UCatInventorySlotWidget* FishSlot = Slots && FishSlotIndex != INDEX_NONE
				? Cast<UCatInventorySlotWidget>(Slots->GetChildAt(FishSlotIndex)) : nullptr;
			UButton* CarryButton = View ? Cast<UButton>(View->GetWidgetFromName(TEXT("CarryButton"))) : nullptr;
			WaitingFor = FString::Printf(TEXT("formal occupied UI View=%s InViewport=%d Slots=%s Count=%d Index=%d FishSlot=%s SlotSize=%s CarryButton=%s"),
				*GetNameSafe(View), View && View->IsInViewport(), *GetNameSafe(Slots), Slots ? Slots->GetChildrenCount() : -1,
				FishSlotIndex, *GetNameSafe(FishSlot), FishSlot ? *FishSlot->GetCachedGeometry().GetLocalSize().ToString() : TEXT("None"),
				*GetNameSafe(CarryButton));
			// 操作区可能随未选中状态折叠；先选鱼才有按钮布局，不能反过来等折叠按钮尺寸。
			if (!FishSlot || !CarryButton || FishSlot->GetCachedGeometry().GetLocalSize().GetMin() <= 0.0) return false;
			const FPointerEvent Released(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>(),
				EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
			FishSlot->TakeWidget()->OnMouseButtonUp(FishSlot->GetCachedGeometry(), Released);
			return Test->TestFalse(TEXT("formal CarryButton is disabled while original guard owns the mouth"), CarryButton->GetIsEnabled());
		}

		/** 在既有鱼护已经落地、嘴部已释放后，经正式 WBP 点击原鱼 Carry；等待权威回执并核对两端同一鱼身份成为唯一嘴部 Actor。 */
		bool VerifyFormalFishCarryFromGroundedGuard(ACatCharacter& ServerCat, ACatCharacter& ClientCat)
		{
			UCatFishOnlyInventoryComponent* FishInventory = ClientGuard.IsValid() ? ClientGuard->GetFishInventoryComponent() : nullptr;
			if (!CarryView.IsValid())
			{
				UCatLocalPlayerUISubsystem* UI = ClientController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
				UClass* ViewClass = LoadClass<UCatInventoryWidget>(nullptr,
					TEXT("/Game/UI/Inventory/WBP_CatFishGuardInventory.WBP_CatFishGuardInventory_C"));
				WaitingFor = TEXT("original grounded guard opens its formal inventory");
				if (!UI || !ViewClass || !FishInventory || !UI->OpenInventory(FishInventory, ViewClass)) return false;
				CarryView = FindFormalInventoryView(*ClientController, FishInventory);
				return false;
			}
			UCatInventoryWidget* View = CarryView.Get();
			UWrapBox* Slots = View ? Cast<UWrapBox>(View->GetWidgetFromName(TEXT("InventorySlotWrapBox"))) : nullptr;
			const int32 FishSlotIndex = FishInventory && !FishIds.IsEmpty() ? FishInventory->FindInventorySlotIndexFromInstanceId(FishIds[0]) : INDEX_NONE;
			UCatInventorySlotWidget* FishSlot = Slots && FishSlotIndex != INDEX_NONE ? Cast<UCatInventorySlotWidget>(Slots->GetChildAt(FishSlotIndex)) : nullptr;
			UButton* CarryButton = View ? Cast<UButton>(View->GetWidgetFromName(TEXT("CarryButton"))) : nullptr;
			WaitingFor = TEXT("formal fish slot layout for free-mouth selection");
			if (!CarryButton) return false;
			if (!bCarryButtonClickSent)
			{
				if (!FishSlot || FishSlot->GetCachedGeometry().GetLocalSize().GetMin() <= 0.0) return false;
				const FPointerEvent Released(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>(), EKeys::LeftMouseButton,
					0.0f, FModifierKeysState());
				FishSlot->TakeWidget()->OnMouseButtonUp(FishSlot->GetCachedGeometry(), Released);
				WaitingFor = TEXT("selected CarryButton becomes laid out");
				if (CarryButton->GetCachedGeometry().GetLocalSize().GetMin() <= 0.0) return false;
				if (!Test->TestTrue(TEXT("formal fish slot selection enables CarryButton after guard release"), CarryButton->GetIsEnabled())) return true;
				if (!Test->TestTrue(TEXT("capture actual client viewport before Carry"), CaptureCarryViewport(TEXT("BeforeCarry")))) return true;
				const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(CarryButton->TakeWidget());
				if (!Test->TestTrue(TEXT("formal CarryButton belongs to a native client window"), Window.IsValid())) return true;
				const FVector2D Center = CarryButton->GetCachedGeometry().LocalToAbsolute(CarryButton->GetCachedGeometry().GetLocalSize() * 0.5f);
				const FVector2D Previous = FSlateApplication::Get().GetCursorPos();
				FSlateApplication::Get().SetCursorPos(Center);
				const FPointerEvent Move(0, Center, Previous, TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
				FSlateApplication::Get().ProcessMouseMoveEvent(Move);
				const FPointerEvent Down(0, Center, Center, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
				const FPointerEvent Up(0, Center, Center, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
				CarryReceiptBeforeClick = ClientController->GetLastCampCommandResult().RequestId;
				Window->BringToFront(true);
				if (!Test->TestTrue(TEXT("actual Slate CarryButton mouse click is handled"),
					FSlateApplication::Get().ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down)
					&& FSlateApplication::Get().ProcessMouseButtonUpEvent(Up))) return true;
				if (!Test->TestFalse(TEXT("CarryButton is disabled while its request is pending"), CarryButton->GetIsEnabled())) return true;
				bCarryButtonClickSent = true;
				return false;
			}
			const FCatDomainCommandResult CarryResult = ClientController->GetLastCampCommandResult();
			WaitingFor = TEXT("committed CarryButton receipt and cleared formal fish selection");
			if (CarryResult.RequestId == CarryReceiptBeforeClick) return false;
			if (!Test->TestTrue(TEXT("free-mouth fish CarryButton request commits on authority"), CarryResult.bCommitted)
				|| !Test->TestFalse(TEXT("committed CarryButton clears the selected actionable fish"), CarryButton->GetIsEnabled())) return true;
			AActor* ServerMouth = ServerCat.GetMouthCarriedActor();
			AActor* ClientMouth = ClientCat.GetMouthCarriedActor();
			ACatFishPickupActor* ServerFish = Cast<ACatFishPickupActor>(ServerMouth);
			ACatFishPickupActor* ClientFish = Cast<ACatFishPickupActor>(ClientMouth);
			// 源格移除、角色 Actor 引用和鱼表现分属不同复制事实；源格消失后继续等回执及鱼身份，不能再要求旧格控件存在。
			WaitingFor = TEXT("both mouth actor references and original fish GUID/weight converge after source removal");
			if (!ServerFish || !ClientFish || ServerFish->GetPresentationState().FishInstanceId != FishIds[0]
				|| ClientFish->GetPresentationState().FishInstanceId != FishIds[0]
				|| ServerFish->GetPresentationState().WeightKilograms != 2.5
				|| ClientFish->GetPresentationState().WeightKilograms != 2.5) return false;
			WaitingFor = TEXT("both source slots removed and the same original instance belongs to the mouth actor");
			if (ServerFishInventory->FindInventorySlotIndexFromInstanceId(FishIds[0]) != INDEX_NONE
				|| ClientFishInventory->FindInventorySlotIndexFromInstanceId(FishIds[0]) != INDEX_NONE) return false;
			if (!Test->TestTrue(TEXT("authority Carry keeps the original inventory instance"), OriginalFish[0].IsValid()
				&& OriginalFish[0]->GetWorldActor() == ServerFish && OriginalFish[0]->GetRuntimeOwnerActor() == ServerFish)) return true;
			// 数据复制先于下一帧 Slate 绘制；只为截图等待 0.15 秒，让画面反映已核对的源格移除，不用旧帧冒充交付证据。
			if (StableSince <= 0.0) StableSince = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StableSince < 0.15) return false;
			if (!Test->TestTrue(TEXT("capture actual client viewport after Carry"), CaptureCarryViewport(TEXT("AfterCarry")))) return true;
			return true;
		}

		/** 等原鱼在双方真正解除嘴部、启用刚体并落到地图地面后，才由拥有客户端通过正式 E 交互 RPC 重新叼起。
		 * 先记录每端物理释放后的世界位置，排除嘴部解绑瞬移；再要求速度、地面接触和15厘米网络收敛连续一秒成立。
		 * 条件满足时才分配本轮请求 GUID 并请求 ClientFish，保证服务器空间裁决看到的是合法可触达的落点，而非测试直接改变附件或物理。 */
		bool WaitForFishDropAndRequestRepick(ACatCharacter& ServerCat, ACatCharacter& ClientCat, const double Now)
		{
			for (int32 Peer = 0; Peer < 2; ++Peer)
			{
				ACatFishPickupActor* Fish = Peer == 0 ? ServerCarriedFish.Get() : ClientCarriedFish.Get();
				const UPrimitiveComponent* Body = Fish ? Cast<UPrimitiveComponent>(Fish->GetRootComponent()) : nullptr;
				if (!Fish || Fish->GetPresentationState().State != ECatFishPickupState::Available || Fish->GetAttachParentActor()
					|| !Body || !Body->IsSimulatingPhysics()) continue;
				if (!FishDropSampled[Peer])
				{
					FishDropStart[Peer] = Fish->GetActorLocation();
					FishDropSampled[Peer] = true;
				}
				else if (FVector::Dist(Fish->GetActorLocation(), FishDropStart[Peer]) > 2.0) FishDropMoved[Peer] = true;
			}
			bool bReady = BothSidesFishMatch(false, true) && FishDropMoved[0] && FishDropMoved[1];
			for (int32 Peer = 0; Peer < 2 && bReady; ++Peer)
			{
				ACatFishPickupActor* Fish = Peer == 0 ? ServerCarriedFish.Get() : ClientCarriedFish.Get();
				const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Fish->GetRootComponent());
				FCollisionQueryParams Query(SCENE_QUERY_STAT(CatFishCarryDropFloor), false, Fish);
				Query.AddIgnoredActor(Peer == 0 ? &ServerCat : &ClientCat);
				FHitResult Hit;
				bReady = Body->GetPhysicsLinearVelocity().Size() < 10.0
					&& FVector::Dist(Fish->GetActorLocation(), FishDropStart[Peer]) < 1000.0
					&& Fish->GetWorld()->LineTraceSingleByChannel(Hit, Body->Bounds.Origin,
						Body->Bounds.Origin - FVector(0, 0, Body->Bounds.BoxExtent.Z + 10.0), ECC_WorldDynamic, Query)
					&& Hit.ImpactNormal.Z > 0.5;
			}
			WaitingFor = TEXT("both original fish physically move after release, settle on map ground and converge within 15 cm");
			if (!bReady) { StableSince = 0.0; return false; }
			if (StableSince <= 0.0) StableSince = Now;
			if (Now - StableSince < 1.0) return false;
			RequestId = FGuid::NewGuid();
			if (FishDropRepickCount == 0)
			{
				USkeletalMeshComponent* ClientMesh = ClientCat.GetMesh();
				if (!ClientMesh)
				{
					Test->AddError(TEXT("Fish late-Mesh regression requires the remote character SkeletalMeshComponent."));
					Stage = -1;
					return false;
				}
				ClientFishMeshAsset = ClientMesh->GetSkeletalMeshAsset();
				if (!ClientFishMeshAsset.IsValid())
				{
					Test->AddError(TEXT("Fish late-Mesh regression requires an already loaded remote character skeletal mesh asset."));
					Stage = -1;
					return false;
				}
				// 只在本地客户端暂时清除骨骼资源，复现 socket 晚就绪；服务器 Mesh 和鱼自身物理、附件均不被测试直接改写。
				ClientMesh->SetSkeletalMeshAsset(nullptr);
				bClientFishMeshRestorePending = true;
				ClientFishMeshClearedAt = Now;
			}
			++FishDropRepickCount;
			ClientController->ServerRequestInteraction(ClientCarriedFish.Get(), RequestId);
			Stage = 11;
			StageStartedAt = Now;
			StableSince = 0.0;
			return true;
		}

		/** 把晚到 Mesh 回归临时清空的远端骨骼资源写回原组件。
		 * 先检查是否仍处于空 Mesh 窗口，再解析当前远端角色和 Mesh 组件；角色已经销毁时只清标记，让 PIE 清理接管。
		 * 成功写回后关闭恢复标记，避免 Stage 11 与析构路径重复把同一资源写入已经恢复的组件。 */
		void RestoreClientFishMesh()
		{
			if (!bClientFishMeshRestorePending) return;
			if (ACatCharacter* ClientCat = ClientController.IsValid() ? Cast<ACatCharacter>(ClientController->GetPawn()) : nullptr)
			{
				if (USkeletalMeshComponent* Mesh = ClientCat->GetMesh()) Mesh->SetSkeletalMeshAsset(ClientFishMeshAsset.Get());
			}
			bClientFishMeshRestorePending = false;
		}

		/** 核对同一条原鱼在两端的可用或嘴叼事实。
		 * 先逐端读取原鱼 Actor、角色和物理根，确认 GUID、重量、状态、缩放、物理模式和嘴部引用都与目标阶段一致。
		 * 携带态额外要求根组件挂在角色 Mesh 与 Mouth socket；落地态要求完全脱离附件。
		 * 最后按当前状态比较世界或相对变换，避免只验证单端的表现修正。 */
		bool BothSidesFishMatch(const bool bCarried, const bool bDrop)
		{
			for (int32 Peer = 0; Peer < 2; ++Peer)
			{
				ACatFishPickupActor* Fish = Peer == 0 ? ServerCarriedFish.Get() : ClientCarriedFish.Get();
				ACatCharacter* Character = Cast<ACatCharacter>((Peer == 0 ? ServerController.Get() : ClientController.Get())->GetPawn());
				const UPrimitiveComponent* Body = Fish ? Cast<UPrimitiveComponent>(Fish->GetRootComponent()) : nullptr;
				WaitingFor = FString::Printf(TEXT("peer=%d fish=%s carried=%d id=%s weight=%.3f state=%d scale=%s expectedScale=%s sim=%d mouth=%s parent=%s socket=%s"),
					Peer, *GetNameSafe(Fish), bCarried, Fish ? *Fish->GetPresentationState().FishInstanceId.ToString() : TEXT("None"),
					Fish ? Fish->GetPresentationState().WeightKilograms : 0.0, Fish ? int32(Fish->GetPresentationState().State) : -1,
					Fish ? *Fish->GetActorScale3D().ToString() : TEXT("None"), *OriginalFishWorldScale.ToString(), Body && Body->IsSimulatingPhysics(),
					*GetNameSafe(Character ? Character->GetMouthCarriedActor() : nullptr), *GetNameSafe(Fish ? Fish->GetAttachParentActor() : nullptr),
					Body ? *Body->GetAttachSocketName().ToString() : TEXT("None"));
				if (!Fish || !Character || !Body || Fish->GetPresentationState().FishInstanceId != FishIds[0]
					|| Fish->GetPresentationState().WeightKilograms != 2.5
					|| Fish->GetPresentationState().State != (bCarried ? ECatFishPickupState::Carried : ECatFishPickupState::Available)
					|| !Fish->GetActorScale3D().Equals(OriginalFishWorldScale, UE_KINDA_SMALL_NUMBER)
					|| Body->IsSimulatingPhysics() != bDrop || Character->GetMouthCarriedActor() != (bCarried ? static_cast<AActor*>(Fish) : nullptr)) return false;
				if (bCarried)
				{
					if (Fish->GetAttachParentActor() != Character || Fish->GetRootComponent()->GetAttachParent() != Character->GetMesh()
						|| Fish->GetRootComponent()->GetAttachSocketName() != GetDefault<UCatFishPickupSettings>()->MouthCarrySocketName) return false;
				}
				else if (Fish->GetAttachParentActor()) return false;
			}
			const FTransform ServerTransform = bCarried ? ServerCarriedFish->GetRootComponent()->GetRelativeTransform() : ServerCarriedFish->GetActorTransform();
			const FTransform ClientTransform = bCarried ? ClientCarriedFish->GetRootComponent()->GetRelativeTransform() : ClientCarriedFish->GetActorTransform();
			WaitingFor = TEXT("both original fish transforms converge");
			return FVector::Dist(ServerTransform.GetLocation(), ClientTransform.GetLocation()) <= (bDrop ? 15.0 : 2.0)
				&& ServerTransform.GetRotation().AngularDistance(ClientTransform.GetRotation()) <= FMath::DegreesToRadians(bDrop ? 10.0 : 2.0)
				&& ServerTransform.GetScale3D().Equals(ClientTransform.GetScale3D(), 0.01);
		}

		/** 验证本轮250厘米权威移动同时带动两端角色和同一条嘴叼鱼。
		 * 先复用携带态身份和附件核对，再分别计算服务器/客户端角色与鱼的世界位移，确认都落在200至400厘米窗口。
		 * 还要检查服务器角色抵达本轮目标点、两端角色位置收敛、两端鱼位置收敛；条件连续0.25秒成立后才允许下一轮丢弃或结束。 */
		bool VerifyFishCarryMovement(ACatCharacter& ServerCat, ACatCharacter& ClientCat, const double Now)
		{
			WaitingFor = TEXT("authority moves 200-400 cm and client cat plus attached original fish converge in world space");
			const bool bConverged = BothSidesFishMatch(true, false)
				&& FVector::Dist(ServerCat.GetActorLocation(), FishCarryMoveStart[0]) >= 200.0 && FVector::Dist(ServerCat.GetActorLocation(), FishCarryMoveStart[0]) <= 400.0
				&& FVector::Dist(ClientCat.GetActorLocation(), FishCarryMoveStart[1]) >= 200.0 && FVector::Dist(ClientCat.GetActorLocation(), FishCarryMoveStart[1]) <= 400.0
				&& FVector::Dist(ServerCarriedFish->GetActorLocation(), FishMoveStart[0]) >= 200.0 && FVector::Dist(ServerCarriedFish->GetActorLocation(), FishMoveStart[0]) <= 400.0
				&& FVector::Dist(ClientCarriedFish->GetActorLocation(), FishMoveStart[1]) >= 200.0 && FVector::Dist(ClientCarriedFish->GetActorLocation(), FishMoveStart[1]) <= 400.0
				&& FVector::Dist(ServerCat.GetActorLocation(), FishCarryMoveTarget) <= 2.0
				&& FVector::Dist(ServerCat.GetActorLocation(), ClientCat.GetActorLocation()) <= 15.0
				&& FVector::Dist(ServerCarriedFish->GetActorLocation(), ClientCarriedFish->GetActorLocation()) <= 15.0;
			if (!bConverged) { StableSince = 0.0; return false; }
			if (StableSince <= 0.0) StableSince = Now;
			return Now - StableSince >= 0.25;
		}

		/** 从当前远端 LocalPlayer 的已入视口根页面中查找指定库存对应的正式 WBP；页面尚未布局或上下文不同则返回空供阶段机等待。 */
		static UCatInventoryWidget* FindFormalInventoryView(ACatfishingPlayerController& Controller, UCatInventoryComponent* Inventory)
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&Controller, Widgets, UCatInventoryWidget::StaticClass(), true);
			for (UUserWidget* Widget : Widgets)
			{
				UCatInventoryWidget* View = Cast<UCatInventoryWidget>(Widget);
				if (View && View->IsInViewport() && View->GetOwningPlayer() == &Controller && View->GetInventoryContext() == Inventory) return View;
			}
			return nullptr;
		}

		/** 把当前远端玩家真实视口保存为 PNG；它记录正式 WBP 的布局和 Carry 按钮状态，不以离屏渲染替代可点击 UI。 */
		bool CaptureCarryViewport(const TCHAR* Phase) const
		{
			UGameViewportClient* Viewport = ClientController.IsValid() && ClientController->GetLocalPlayer()
				? ClientController->GetLocalPlayer()->ViewportClient : nullptr;
			TSharedPtr<SViewport> SlateViewport = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
			TArray<FColor> Pixels;
			FIntVector Size = FIntVector::ZeroValue;
			if (!SlateViewport.IsValid() || !FSlateApplication::Get().TakeScreenshot(SlateViewport.ToSharedRef(), Pixels, Size)
				|| Size.X <= 0 || Size.Y <= 0 || Pixels.Num() != Size.X * Size.Y) return false;
			TArray64<uint8> PNG;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/FishGuardCarry");
			return IFileManager::Get().MakeDirectory(*Directory, true)
				&& FFileHelper::SaveArrayToFile(PNG, *(Directory / FString::Printf(TEXT("Client%s.png"), Phase)));
		}

		/** 同时观察原两端对象：先核对原库存、精确 GUID/重量/数量，再读归属、刚体、嘴部附件与背包。
		 * InventoryOwner 仅通过反射只读核对，不为测试新增生产 getter；服务器内鱼还须保持原 UObject。
		 * 携带核对正式 socket 与可见性，落地核对空嘴和背包扣格；两端都须保留拾取前尺寸，最后比较位置、旋转和缩放。
		 * 任一复制事实未到位返回 false 并写明端与等待项，让上层继续等待或带阶段超时报错。 */
		bool BothSidesMatch(const bool bCarried, const bool bDrop)
		{
			const FObjectPropertyBase* OwnerProperty = FindFProperty<FObjectPropertyBase>(ACatFishGuardActor::StaticClass(), TEXT("InventoryOwner"));
			for (int32 Peer = 0; Peer < 2; ++Peer)
			{
				ACatFishGuardActor* Guard = Peer == 0 ? ServerGuard.Get() : ClientGuard.Get();
				ACatCharacter* Character = Cast<ACatCharacter>((Peer == 0 ? ServerController.Get() : ClientController.Get())->GetPawn());
				UCatFishOnlyInventoryComponent* Inventory = Peer == 0 ? ServerFishInventory.Get() : ClientFishInventory.Get();
				WaitingFor = FString::Printf(TEXT("peer=%d original FishInventory with exactly two original GUIDs, quantities and weights"), Peer);
				if (!Guard || Guard->IsActorBeingDestroyed() || !Character || !Inventory || Guard->GetFishInventoryComponent() != Inventory) return false;
				WaitingFor = FString::Printf(TEXT("peer=%d original world scale expected=%s actual=%s"), Peer,
					*OriginalGuardWorldScale.ToCompactString(), *Guard->GetActorScale3D().ToCompactString());
				// FRepAttachment.RelativeScale3D 是 NetQuantize100；正式 Mouth 父尺度为2，0.325传为0.33后世界尺度为0.66。
				// 服务器仍须精确保持原尺寸；客户端仅接受一次量化的0.01误差，三轮都对同一初始值比较，不能累计漂移。
				if (!Guard->GetActorScale3D().Equals(OriginalGuardWorldScale, Peer == 0 ? UE_KINDA_SMALL_NUMBER : 0.0101)) return false;
				TSet<FGuid> Seen;
				for (const FCatInventoryEntry& Entry : Inventory->GetInventoryEntries())
				{
					if (!Entry.Instance && Entry.StackCount == 0) continue;
					const UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry.Instance);
					if (!Fish || Entry.StackCount != 1) return false;
					const int32 Index = FishIds.IndexOfByKey(Fish->GetItemInstanceId());
					if (Index == INDEX_NONE || Seen.Contains(Fish->GetItemInstanceId()) || Fish->GetItemDefinitionId() != TEXT("RiverPatternFish")
						|| Fish->GetFishWeightKilograms() != (Index == 0 ? 2.5 : 3.75)
						|| (Peer == 0 && Fish != OriginalFish[Index].Get())) return false;
					Seen.Add(Fish->GetItemInstanceId());
				}
				if (Seen.Num() != 2) return false;
				WaitingFor = FString::Printf(TEXT("peer=%d InventoryOwner, Actor owner, visibility, collision and physics mode carried=%d drop=%d"), Peer, bCarried, bDrop);
				const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Guard->GetRootComponent());
				if (!OwnerProperty || OwnerProperty->GetObjectPropertyValue_InContainer(Guard) != (bCarried ? Character : nullptr)
					|| Guard->GetOwner() != (bCarried ? Character : nullptr) || Guard->IsGrounded() == bCarried
					|| Guard->IsHidden() || Guard->GetActorEnableCollision() == bCarried || !Body || Body->IsSimulatingPhysics() != bDrop) return false;
				WaitingFor = FString::Printf(TEXT("peer=%d mouth attachment/socket and backpack agree with carried=%d"), Peer, bCarried);
				const UCatInventoryComponent* Backpack = Character->GetInventoryComponent();
				if (!Backpack || ACatFishPickupActor::FindCarriedFish(Character)
					|| Character->GetMouthCarriedActor() != (bCarried ? static_cast<AActor*>(Guard) : nullptr)) return false;
				// 初始背包必须没有其他鱼护，首次客户端按正式定义查槽位才唯一对应本用例生成的载体。
				if (!GuardId.IsValid() && !bCarried && Backpack->CountVisibleInventoryQuantityByDefinitionId(TEXT("FishGuard")) != 0) return false;
				if (bCarried)
				{
					if (ACatFishGuardActor::FindCarriedGuard(Character) != Guard || Guard->GetAttachParentActor() != Character
						|| Guard->GetRootComponent()->GetAttachParent() != Character->GetMesh()
						|| Guard->GetRootComponent()->GetAttachSocketName() != GetDefault<UCatFishPickupSettings>()->MouthCarrySocketName) return false;
				}
				else if (Guard->GetAttachParentActor() || ACatFishGuardActor::FindCarriedGuard(Character)
					|| (GuardId.IsValid() && Backpack->FindInventorySlotIndexFromInstanceId(GuardId) != INDEX_NONE)) return false;
			}
			WaitingFor = TEXT("both original guards converge in position, rotation and scale");
			// 落稳丢弃允许15厘米网络物理修正差；固定放置限2厘米、2度；携带只比较局部变换，排除角色插值差。
			const FTransform ServerTransform = bCarried ? ServerGuard->GetRootComponent()->GetRelativeTransform() : ServerGuard->GetActorTransform();
			const FTransform ClientTransform = bCarried ? ClientGuard->GetRootComponent()->GetRelativeTransform() : ClientGuard->GetActorTransform();
			WaitingFor = FString::Printf(TEXT("both original guards converge carried=%d server=%s client=%s"), bCarried,
				*ServerTransform.ToHumanReadableString(), *ClientTransform.ToHumanReadableString());
			return FVector::Dist(ServerTransform.GetLocation(), ClientTransform.GetLocation()) <= (bDrop ? 15.0 : 2.0)
				// 四元数夹角以弧度返回；Drop落稳允许10度刚体修正，静态放置与嘴部偏移限2度。
				&& ServerTransform.GetRotation().AngularDistance(ClientTransform.GetRotation()) <= FMath::DegreesToRadians(bDrop ? 10.0 : 2.0)
				&& ServerTransform.GetScale3D().Equals(ClientTransform.GetScale3D(), 0.0101);
		}

		/** 框架拥有的断言接收者；命令只在队列存活期向其报告结果。 */
		FAutomationTestBase* Test = nullptr;
		/** 场景中原鱼护的世界缩放；准备阶段记录非默认尺寸，双方携带和落地阶段读取，防止两端一起变大仍被当作复制正确。 */
		FVector OriginalGuardWorldScale = FVector::OneVector;
		/** 当前异步步骤，0至8覆盖原鱼护，9至12覆盖正式 UI 取出的原鱼；仅在本阶段的复制、物理或回执事实齐备后推进，避免重复 RPC。 */
		int32 Stage = 0;
		/** 已完成落稳并重新拾取的循环次数；Stage 5 写入，Stage 7/8 消费，达到三次后才允许最终落地进入原 Stage 6 取鱼。 */
		int32 DropRepickCount = 0;
		/** 当前步骤起始单调时间，单位秒；每次推进刷新，超时用它限制等待。 */
		double StageStartedAt = 0.0;
		/** 连续稳定窗口起始时间，单位秒；任一地面条件失配清零，防止单帧巧合通过。 */
		double StableSince = 0.0;
		/** 当前未满足的端与条件；观察方法更新，超时输出它定位复制或空间失败。 */
		FString WaitingFor;
		/** 正式房主 PIE 世界；准备阶段发现，后续只在这里生成 fixture 和读取权威事实。 */
		TWeakObjectPtr<UWorld> ServerWorld;
		/** 唯一远端 PIE 世界；准备阶段发现，后续从中读取独立复制事实。 */
		TWeakObjectPtr<UWorld> ClientWorld;
		/** 远端玩家的权威 PC；按正式 UniqueId 匹配，只读资格与角色。 */
		TWeakObjectPtr<ACatfishingPlayerController> ServerController;
		/** 远端本地 PC；准备时获取，所有拾取与释放 RPC 都经其拥有连接发送。 */
		TWeakObjectPtr<ACatfishingPlayerController> ClientController;
		/** 本轮搬运的原正式鱼护；准备时写入，全程保留同一对象，析构销毁。 */
		TWeakObjectPtr<ACatFishGuardActor> ServerGuard;
		/** 占嘴按钮验证期间可正常打开的另一只地面鱼护；阶段二生成，验证完成或析构时销毁。 */
		TWeakObjectPtr<ACatFishGuardActor> OccupiedViewGuard;
		/** 地面 UI 夹具内独立鱼的身份；等待它复制后选择，不能与原鱼护的两条鱼共享实例。 */
		FGuid OccupiedViewFishId;
		/** 原鱼护的远端副本；初始复制时按对象名定位，此后丢失即失败，不接受替代 Actor。 */
		TWeakObjectPtr<ACatFishGuardActor> ClientGuard;
		/** 正式 WBP 从原鱼护取出的同一条权威世界鱼；Stage 6 保存，后续三轮丢弃与交互都必须使用它，防止替换 Actor 掩盖身份问题。 */
		TWeakObjectPtr<ACatFishPickupActor> ServerCarriedFish;
		/** 上述原鱼在远端的复制副本；Stage 6 保存，Stage 10 由拥有客户端以它为交互目标，之后全程核对同名实例的状态。 */
		TWeakObjectPtr<ACatFishPickupActor> ClientCarriedFish;
		/** 原鱼进入本回归循环前的世界缩放；Stage 6 在权威端记录，之后每端每态检查，避免两端一起发生尺寸漂移仍被误判为收敛。 */
		FVector OriginalFishWorldScale = FVector::OneVector;
		/** 原权威鱼库存组件；播种时保存，后续用于发现组件被重建或内容丢失。 */
		TWeakObjectPtr<UCatFishOnlyInventoryComponent> ServerFishInventory;
		/** 初始复制的客户端鱼库存组件；首次观察保存，搬运后必须仍是同一组件。 */
		TWeakObjectPtr<UCatFishOnlyInventoryComponent> ClientFishInventory;
		/** 正在等待回执的请求身份；每个真实操作分配新 GUID，避免把前次成功当成本次完成。 */
		FGuid RequestId;
		/** 鱼护首次进入客户端背包后的物品身份；首次读取保存，再拾取与落地扣格都核对此值。 */
		FGuid GuardId;
		/** 当前客户端正式鱼护库存页面；占嘴和空嘴阶段切换到对应地面容器，回执阶段读取它确认选择刷新。 */
		TWeakObjectPtr<UCatInventoryWidget> CarryView;
		/** 点击 Carry 前最后一条控制器回执；后续只有新 RequestId 才能作为本次 UI 请求的终态。 */
		FGuid CarryReceiptBeforeClick;
		/** 真实 Slate 点击是否已发生；置位后阶段机只等待本次回执，避免每帧重复提交同一条鱼。 */
		bool bCarryButtonClickSent = false;
		/** 原两条鱼的身份，按2.5和3.75公斤排列；播种保存，两端逐条精确匹配。 */
		TArray<FGuid> FishIds;
		/** 服务器原两条鱼的弱引用；播种保存，阶段检查防止以重新创建实例掩盖搬运丢失。 */
		TArray<TWeakObjectPtr<UCatFishInventoryItemInstance>> OriginalFish;
		/** 两端各自开始模拟后的第一帧位置，单位厘米；Drop采样写入，后续排除仅发生释放瞬移的假移动。 */
		FVector DropStart[2] = {FVector::ZeroVector, FVector::ZeroVector};
		/** 两端是否已取得释放后的基准位置；Drop首次模拟时置位，避免后续覆盖移动基准。 */
		bool bDropSampled[2] = {false, false};
		/** 两端是否都发生过超过2厘米的释放后位移；采样累积，最终必须连同落地收敛成立。 */
		bool bDropMoved[2] = {false, false};
		/** 本轮权威移动前两端角色的世界位置，单位厘米；Stage 7 写入，Stage 8 读取，用来证明客户端角色实际移动而非只保留相对嘴部附件。 */
		FVector CarryMoveStart[2] = {FVector::ZeroVector, FVector::ZeroVector};
		/** 本轮权威移动前两端原鱼护的世界位置，单位厘米；与角色起点配对保存，Stage 8 据此检查附着鱼护也在世界空间移动。 */
		FVector CarryGuardMoveStart[2] = {FVector::ZeroVector, FVector::ZeroVector};
		/** 服务器角色本轮应抵达的世界位置；Stage 7 由当前朝向计算并写入，作为250厘米移动的权威目标供 Stage 8 排查。 */
		FVector CarryMoveTarget = FVector::ZeroVector;
		/** 已完成“原鱼物理丢弃、E 交互再拾取、共同移动”的轮数；每次合法落地后写入，第三轮移动收敛才结束用例。 */
		int32 FishDropRepickCount = 0;
		/** 原鱼两端第一次进入物理丢弃态后的世界位置，单位厘米；每轮重置，用来排除仅由嘴部解绑造成的瞬移。 */
		FVector FishDropStart[2] = {FVector::ZeroVector, FVector::ZeroVector};
		/** 原鱼两端是否已有本轮物理释放起点；等待阶段写入，下一帧起才比较真实刚体位移。 */
		bool FishDropSampled[2] = {false, false};
		/** 原鱼两端是否已在释放后移动超过2厘米；只有两端都发生真实刚体运动时才允许请求重新交互。 */
		bool FishDropMoved[2] = {false, false};
		/** 原鱼共同携带移动前两端猫的世界位置，单位厘米；Stage 11 写入，Stage 12 验证角色本身也完成250厘米同步。 */
		FVector FishCarryMoveStart[2] = {FVector::ZeroVector, FVector::ZeroVector};
		/** 原鱼共同携带移动前两端鱼的世界位置，单位厘米；与猫的位置配对，防止只验证相对附件而遗漏世界空间随行。 */
		FVector FishMoveStart[2] = {FVector::ZeroVector, FVector::ZeroVector};
		/** 本轮服务器猫的250厘米共同携带目标位置；由 Stage 11 写入，Stage 12 用2厘米容差核对权威移动未被物理阻挡。 */
		FVector FishCarryMoveTarget = FVector::ZeroVector;
		/** 远端角色原有的骨骼资源；第一轮鱼重拾前保存，Stage 11 或析构写回，确保临时晚资源场景不泄漏到 PIE 结束以后。 */
		TWeakObjectPtr<USkeletalMesh> ClientFishMeshAsset;
		/** 远端角色是否仍处在本回归人为制造的空 Mesh 窗口；请求前写入，Stage 11 成功附着并等待0.3秒后清除。 */
		bool bClientFishMeshRestorePending = false;
		/** 远端骨骼资源被临时清空时的单调秒数；Stage 11 读取以保证附件在无 socket 资源的状态下至少保持0.3秒。 */
		double ClientFishMeshClearedAt = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishGuardCarryFormalNetworkTest,
	"Catfishing.Editor.Inventory.WorldActions.FormalTwoEndpointGuardCarryPlaceDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 确认编辑器空闲，先保存 PIE 设置，再临时配置双端 IP 驱动；依序排入正式地图、启动、断言、EndPIE 与恢复。
 * 与已通过的售鱼用例使用同一启动链；返回 true 只表示排队，运行结论留给后续真实 RPC 和复制检查。 */
bool FCatFishGuardCarryFormalNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("stage=setup requires idle editor and engine"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const TSharedRef<CatFishGuardCarryNetwork::FRestoreSettings> Restore = MakeShared<CatFishGuardCarryNetwork::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			// 和售鱼双端用例相同，fallback也走本机IP；结束后恢复完整驱动表，不触碰游戏准入规则。
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishGuardCarryNetwork::FVerifyCarry>(this));
	// 断言命令无论成功或失败均先释放fixture，再结束PIE；最后的恢复命令等世界销毁完成才写回设置。
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
