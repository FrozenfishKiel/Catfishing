#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
#include "Items/Fish/CatFishPickupActor.h"
#include "UObject/UnrealType.h"

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
			if (ServerGuard.IsValid()) ServerGuard->Destroy();
		}

		/** 按真实网络时序推进一个连续用例：
		 * 1. 等正式登录、身体与地图地面就绪，在服务器生成原鱼护并通过既有库存装鱼。
		 * 2. 等初始复制完整才从客户端拾取；收到对应回执后检查两端归属、嘴部附着与原鱼。
		 * 3. 从客户端背包读取实际槽位和 GUID 发送 Place，等地面、扣格和固定变换收敛后再次拾取。
		 * 4. 第二次携带收敛后无参通知服务器丢弃当前携带物，分别采样两端释放后的位移，等真实刚体落稳、位置收敛且嘴空。
		 * 前提丢失或拾取、放置回执拒绝时立即带阶段报错；丢弃只观察复制结果，未收敛时继续等待并在阶段超时报告原因。 */
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (StageStartedAt <= 0.0) StageStartedAt = Now;
			if (Now - StageStartedAt > 45.0)
			{
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
			if (Stage == 2 || Stage == 4)
			{
				if (!BothSidesMatch(true, false)) return false;
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
				RequestId = FGuid::NewGuid();
				if (Stage == 2)
				{
					ClientController->ServerReleaseInventoryItemToWorld(RequestId, ClientCat, Slot, Item->GetItemInstanceId(), 1,
						ECatInventoryWorldAction::Place);
				}
				else
				{
					ClientController->ServerDropCarriedItem();
				}
				++Stage;
				StageStartedAt = Now;
				StableSince = 0.0;
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
				Test->AddInfo(TEXT("Event=fish_guard_carry_network Result=CarryPlaceRepickDropReplicated Fish=OriginalTwoGUIDs_2.5kg_3.75kg Mouth=Free UI=NotCovered Screenshots=NotCaptured"));
				return true;
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
				// 两端都核对拾取前尺寸，只容许浮点变换误差；不能以低精度附着复制为由接受永久缩放漂移。
				if (!Guard->GetActorScale3D().Equals(OriginalGuardWorldScale, UE_KINDA_SMALL_NUMBER)) return false;
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
				if (!Backpack || ACatFishPickupActor::FindCarriedFish(Character)) return false;
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
				&& ServerTransform.GetScale3D().Equals(ClientTransform.GetScale3D(), 0.01);
		}

		/** 框架拥有的断言接收者；命令只在队列存活期向其报告结果。 */
		FAutomationTestBase* Test = nullptr;
		/** 场景中原鱼护的世界缩放；准备阶段记录非默认尺寸，双方携带和落地阶段读取，防止两端一起变大仍被当作复制正确。 */
		FVector OriginalGuardWorldScale = FVector::OneVector;
		/** 当前异步步骤，0准备/1初始复制/2拾取/3放置/4再拾取/5丢弃；仅在条件齐备后推进，避免重复 RPC。 */
		int32 Stage = 0;
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
		/** 测试唯一生成的正式鱼护；准备时写入，全程保留同一对象，析构销毁。 */
		TWeakObjectPtr<ACatFishGuardActor> ServerGuard;
		/** 原鱼护的远端副本；初始复制时按对象名定位，此后丢失即失败，不接受替代 Actor。 */
		TWeakObjectPtr<ACatFishGuardActor> ClientGuard;
		/** 原权威鱼库存组件；播种时保存，后续用于发现组件被重建或内容丢失。 */
		TWeakObjectPtr<UCatFishOnlyInventoryComponent> ServerFishInventory;
		/** 初始复制的客户端鱼库存组件；首次观察保存，搬运后必须仍是同一组件。 */
		TWeakObjectPtr<UCatFishOnlyInventoryComponent> ClientFishInventory;
		/** 正在等待回执的请求身份；每个真实操作分配新 GUID，避免把前次成功当成本次完成。 */
		FGuid RequestId;
		/** 鱼护首次进入客户端背包后的物品身份；首次读取保存，再拾取与落地扣格都核对此值。 */
		FGuid GuardId;
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
