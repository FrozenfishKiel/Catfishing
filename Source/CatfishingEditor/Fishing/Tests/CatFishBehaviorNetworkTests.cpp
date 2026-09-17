#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/StateTreeComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishAnimInstance.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "StateTree.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace CatFishBehaviorNetwork
{
	static const FName AnimatedMouthBone(TEXT("Bone014"));

	class FRestore final : public IAutomationLatentCommand
	{
	public:
		FRestore()
		{
			const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(Mode);
			Settings->GetPlayNumberOfClients(Count);
			Settings->GetRunUnderOneProcess(OneProcess);
			Drivers = GEngine->NetDriverDefinitions;
		}
		~FRestore() override { Restore(); }
		bool Update() override
		{
			if (GEditor && GEditor->PlayWorld) return false;
			Restore();
			return true;
		}
	private:
		void Restore()
		{
			if (bRestored) return;
			auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(Mode);
			Settings->SetPlayNumberOfClients(Count);
			Settings->SetRunUnderOneProcess(OneProcess);
			if (GEngine) GEngine->NetDriverDefinitions = Drivers;
			// 临时内存设置恢复即可；测试不调用SaveConfig，不改用户的持久PIE配置。
			bRestored = true;
		}
		EPlayNetMode Mode = PIE_Standalone;
		int32 Count = 1;
		bool OneProcess = true;
		bool bRestored = false;
		TArray<FNetDriverDefinition> Drivers;
	};

	struct FSnapshot
	{
		ECatFishBehavior Behavior;
		ECatFishMotionIntent Motion;
		float Effort;
		float Speed;
		FVector Heading;
		FVector BodyHeading;
		FVector Position;
	};
	static const FSnapshot Samples[] = {
		{ECatFishBehavior::OutwardRush, ECatFishMotionIntent::StrugglingOutward, 0.9f, 162.0f, FVector::ForwardVector, FVector::RightVector, FVector(500, 100, 200)},
		{ECatFishBehavior::LateralArc, ECatFishMotionIntent::StrugglingOutward, 0.65f, 117.0f, FVector::RightVector, FVector(-1, 1, 0).GetSafeNormal(), FVector(520, 140, 200)},
		{ECatFishBehavior::EaseOff, ECatFishMotionIntent::CalmOrInward, 0.2f, 36.0f, -FVector::ForwardVector, -FVector::ForwardVector, FVector(480, 150, 200)},
		{ECatFishBehavior::None, ECatFishMotionIntent::AutoHauling, 0.0f, 0.0f, FVector::ZeroVector, -FVector::RightVector, FVector(460, 150, 200)}
	};

	struct FAnimationObservation
	{
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		uint32 LastBoneRevision = 0;
		int32 FinalizedFrames = 0;
		int32 CheckedFrames = 0;
		FVector FirstAnimatedMouth = FVector::ZeroVector;
		double MaximumReferenceDeviation = 0.0;
		double MaximumAnimatedMovement = 0.0;
		double MaximumHookError = 0.0;
		double MaximumPhysicalMouthError = 0.0;
	};

	class FVerify final : public IAutomationLatentCommand
	{
	public:
		explicit FVerify(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45.0)
			{
				Test->AddError(FString::Printf(TEXT("Fish behavior replication/animation timeout Sample=%d Spawned=%d ServerAnimationFrames=%d ClientAnimationFrames=%d; no network verdict"),
					SampleIndex, ServerFish.IsValid(), ServerAnimation.CheckedFrames, ClientAnimation.CheckedFrames));
				return true;
			}
			UWorld* Server = nullptr;
			UWorld* Client = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::PIE || !Context.World()) continue;
				if (Context.World()->GetNetMode() == NM_ListenServer) Server = Context.World();
				if (Context.World()->GetNetMode() == NM_Client) Client = Context.World();
			}
			if (!Server || !Client || !Server->GetFirstPlayerController() || !Client->GetFirstPlayerController()) return false;
			if (!ServerFish.IsValid())
			{
				const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
				UCatFishDefinition* Definition = nullptr;
				for (const auto& Reference : Catalog->Definitions)
				{
					UCatFishDefinition* Candidate = Reference.LoadSynchronous();
					if (Candidate && Catalog->FindRuntimeDefinition(Candidate->ItemId) == Candidate)
					{
						Definition = Candidate;
						break;
					}
				}
				if (!Test->TestNotNull(TEXT("正式目录包含可用鱼定义"), Definition)) return true;
				UCatFishPresentationDefinition* Presentation = Definition->LoadRuntimePresentationDefinition();
				if (!Test->TestNotNull(TEXT("正式鱼定义具有完整表现引用"), Presentation)) return true;
				if (!Test->TestTrue(TEXT("正式鱼定义具有烘焙嘴点和力臂"), Definition->FightBodyGeometry.HasMouthLever())) return true;
				BodyGeometry = Definition->FightBodyGeometry.Scaled(VisualScale);
				// 静态参考点只用于确认 Bone014 对应标定嘴，并证明后续实际动画骨骼发生了变化。
				MeshMouthLocal = Presentation->EncounterMeshRelativeTransform.InverseTransformPosition(Definition->FightBodyGeometry.MouthLocalPositionCentimeters);
				USkeletalMesh* ReferenceMesh = Presentation->SkeletalMesh.LoadSynchronous();
				if (!Test->TestNotNull(TEXT("查询框基线来自正式参考网格"), ReferenceMesh)) return true;
				FTransform ScaledMeshTransform = Presentation->EncounterMeshRelativeTransform;
				ScaledMeshTransform.SetScale3D(ScaledMeshTransform.GetScale3D() * VisualScale);
				const FBoxSphereBounds ReferenceBounds = ReferenceMesh->GetBounds().TransformBy(ScaledMeshTransform);
				ExpectedCollisionCenter = ReferenceBounds.Origin;
				ExpectedCollisionExtent = ReferenceBounds.BoxExtent.ComponentMax(FVector(2.0));
				ExpectedAnimClass = Presentation->AnimInstanceClass.LoadSynchronous();
				if (!Test->TestNotNull(TEXT("正式表现定义提供AnimBP类"), ExpectedAnimClass.Get())) return true;
				ExhaustedRollDegrees = Presentation->ExhaustedVisualRollDegrees;
				if (!Test->TestTrue(TEXT("翻肚样本的正式侧翻角非零"), FMath::Abs(ExhaustedRollDegrees) > 1.0)) return true;
				UClass* FishClass = GetDefault<UCatFishingPresentationSettings>()->FishEncounterActorClass.LoadSynchronous();
				if (!Test->TestTrue(TEXT("正式表现链提供鱼Encounter类"), FishClass && FishClass->IsChildOf(ACatFishEncounterActor::StaticClass()))) return true;
				const FTransform Transform(Samples[0].Position);
				ServerFish = Server->SpawnActorDeferred<ACatFishEncounterActor>(FishClass, Transform);
				if (!Test->TestNotNull(TEXT("服务器生成正式鱼Actor"), ServerFish.Get())) return true;
				if (!Test->TestTrue(TEXT("服务器通过原身份写口初始化"), ServerFish->InitializeAuthoritativeIdentity(
					SessionId, CastId, Definition->ItemId, 600.0, VisualScale))) return true;
				ServerFish->bAlwaysRelevant = true;
				ServerFish->FinishSpawning(Transform);
				if (USkeletalMeshComponent* ServerMesh = ServerFish->FindComponentByClass<USkeletalMeshComponent>())
					EnableAnimationTick(*ServerMesh);
				UClass* HookClass = GetDefault<UCatFishingPresentationSettings>()->HookActorClass.LoadSynchronous();
				if (!Test->TestTrue(TEXT("正式表现链提供鱼钩Actor类"), HookClass && HookClass->IsChildOf(ACatFishingHookActor::StaticClass()))) return true;
				const FTransform HookTransform(ServerFish->GetMouthWorldLocation());
				ServerHook = Server->SpawnActorDeferred<ACatFishingHookActor>(HookClass, HookTransform);
				if (!Test->TestTrue(TEXT("真实鱼钩绑定同一会话和抛竿身份"), ServerHook.IsValid()
					&& ServerHook->InitializeAuthoritativeIdentity(SessionId, CastId)
					&& ServerHook->FinalizeAuthoritativeLandingOnce(true, HookTransform.GetLocation()))) return true;
				ServerHook->bAlwaysRelevant = true;
				ServerHook->FinishSpawning(HookTransform);
				if (!Test->TestTrue(TEXT("真实鱼钩以KeepWorld附着鱼身嘴点"), ServerHook->AttachToActor(ServerFish.Get(), FAttachmentTransformRules::KeepWorldTransform))) return true;
				ServerHook->ForceNetUpdate();
				if (!Publish()) return true;
			}
			ACatFishEncounterActor* ClientFish = nullptr;
			for (TActorIterator<ACatFishEncounterActor> It(Client); It; ++It)
				if (It->GetPresentationState().FishingSessionId == SessionId) ClientFish = *It;
			if (!ClientFish || !Matches(*ClientFish)) return false;
			ACatFishingHookActor* ClientHook = nullptr;
			for (TActorIterator<ACatFishingHookActor> It(Client); It; ++It)
				if (It->GetPresentationState().FishingSessionId == SessionId) ClientHook = *It;
			if (!ClientHook || ClientHook->GetAttachParentActor() != ClientFish
				|| !ClientHook->GetActorLocation().Equals(ClientFish->GetMouthWorldLocation(), 0.05)) return false;
			if (!bAnimationSamplingInitialized)
			{
				if (!BeginAnimationObservation(*ServerFish, ServerAnimation, TEXT("Server"))
					|| !BeginAnimationObservation(*ClientFish, ClientAnimation, TEXT("Client"))) return true;
				ServerAnimationStartedSeconds = Server->GetTimeSeconds();
				ClientAnimationStartedSeconds = Client->GetTimeSeconds();
				bAnimationSamplingInitialized = true;
				return false;
			}
			if (!ObserveAnimatedMouth(*ServerFish, *ServerHook, ServerAnimation, TEXT("Server"))
				|| !ObserveAnimatedMouth(*ClientFish, *ClientHook, ClientAnimation, TEXT("Client"))) return true;
			if (ServerAnimation.CheckedFrames < 12 || ClientAnimation.CheckedFrames < 12
				|| Server->GetTimeSeconds() - ServerAnimationStartedSeconds < 0.6
				|| Client->GetTimeSeconds() - ClientAnimationStartedSeconds < 0.6) return false;
			if (!VerifyAnimationEvidence(ServerAnimation, TEXT("Server"))
				|| !VerifyAnimationEvidence(ClientAnimation, TEXT("Client"))) return true;
			const auto& State = ClientFish->GetPresentationState();
			Test->TestTrue(TEXT("快照来自独立客户端世界的真实复制Actor"), ClientFish != ServerFish.Get()
				&& ClientFish->GetWorld() != ServerFish->GetWorld() && !ClientFish->HasAuthority());
			Test->TestEqual(TEXT("正式鱼身份随同复制"), State.ItemId, ServerFish->GetPresentationState().ItemId);
			Test->TestEqual(TEXT("抛竿关联身份随同复制"), State.CastAttemptId, CastId);
			Test->TestTrue(TEXT("鱼钩也来自独立客户端ActorChannel"), ClientHook != ServerHook.Get() && !ClientHook->HasAuthority());
			Test->TestEqual(TEXT("钩与鱼保持同一抛竿身份"), ClientHook->GetPresentationState().CastAttemptId, CastId);
			Test->TestTrue(TEXT("客户端鱼身朝向使用物理结果"), ClientFish->GetActorForwardVector().Equals(Samples[SampleIndex].BodyHeading, 0.0001));
			Test->TestTrue(TEXT("客户端静态嘴点随实际鱼身朝向和位置移动"), ClientFish->GetMouthWorldLocation().Equals(ExpectedMouth(), 0.05));
			Test->TestTrue(TEXT("客户端实际鱼钩与嘴点同位"), ClientHook->GetActorLocation().Equals(ExpectedMouth(), 0.05));
			if (SampleIndex <= 1)
			{
				Test->TestFalse(TEXT("被侧拉时客户端不会把AI目标直接当身体朝向"), ClientFish->GetActorForwardVector().Equals(State.SwimHeading, 0.01));
				Test->TestFalse(TEXT("被侧拉时服务器也保留独立物理身体朝向"), ServerFish->GetActorForwardVector().Equals(State.SwimHeading, 0.01));
			}
			auto* Mesh = ClientFish->FindComponentByClass<USkeletalMeshComponent>();
			if (!Test->TestTrue(TEXT("客户端正式鱼网格与表现父节点已就绪"), Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetAttachParent())) return true;
			const USceneComponent* VisualRoot = Mesh->GetAttachParent();
			const double ExpectedRoll = State.MotionIntent == ECatFishMotionIntent::AutoHauling ? ExhaustedRollDegrees : 0.0;
			Test->TestTrue(TEXT("翻肚只旋转VisualRoot且使用正式侧翻角"), FMath::Abs(FMath::FindDeltaAngleDegrees(VisualRoot->GetRelativeRotation().Roll, ExpectedRoll)) < 0.01);
			Test->TestTrue(TEXT("服务器动画补偿不改变权威身体或静态物理嘴"), Matches(*ServerFish));
			const USceneComponent* ServerVisualRoot = ServerAnimation.Mesh->GetAttachParent();
			Test->TestTrue(TEXT("服务器翻肚也使用正式侧翻角"), ServerVisualRoot
				&& FMath::Abs(FMath::FindDeltaAngleDegrees(ServerVisualRoot->GetRelativeRotation().Roll, ExpectedRoll)) < 0.01);
			UStateTreeComponent* ClientTree = ClientFish->FindComponentByClass<UStateTreeComponent>();
			if (!Test->TestNotNull(TEXT("客户端保留正式行为组件"), ClientTree)) return true;
			Test->TestFalse(TEXT("客户端组件没有启动平行决策"), ClientTree->IsRunning());
			if (SampleIndex == 0)
			{
				UStateTree* Tree = GetDefault<UCatFishingSettings>()->FishBehaviorStateTree.LoadSynchronous();
				if (!Test->TestNotNull(TEXT("权限检查使用真实配置树"), Tree)) return true;
				UCatFishingFightRunner* ClientRunner = NewObject<UCatFishingFightRunner>(ClientFish);
				Test->TestFalse(TEXT("客户端不能启动权威行为树"), ClientFish->StartFishBehaviorFromAuthority(Tree, ClientRunner));
				Test->TestFalse(TEXT("客户端不能提交权威行为"), ClientFish->BeginFishBehaviorFromStateTree(ECatFishBehavior::EaseOff));
				Test->TestFalse(TEXT("客户端不能推进权威行为固定步"), ClientFish->TickFishBehaviorFromAuthority(0.05f));
				Test->TestFalse(TEXT("客户端不能覆盖鱼出力与运动快照"), ClientFish->ApplyFightStepFromAuthority(
					ECatFishMotionIntent::CalmOrInward, 1.0, FVector::ZeroVector, 0.05f, 0.0f, 0.0f, 1.0f,
					false, false, FVector::UpVector, FVector::RightVector, ECatFishBehavior::EaseOff, 0.1f, -FVector::ForwardVector));
				Test->TestTrue(TEXT("拒绝后的客户端复制快照未被本地操作改写"), Matches(*ClientFish));
				Test->TestTrue(TEXT("拒绝后的客户端钩嘴关系保持"), ClientHook->GetActorLocation().Equals(ClientFish->GetMouthWorldLocation(), 0.05));
			}
			Test->AddInfo(FString::Printf(TEXT("Event=fish_behavior_network_snapshot SessionId=%s Sample=%d Behavior=%s Effort=%.3f Heading=%s BodyHeading=%s Mouth=%s Hook=%s HookAttached=1 VisualRoll=%.3f SpeedCmPerSec=%.3f ServerNetMode=%d ClientNetMode=%d Evidence=runtime_behavior Replication=ActorChannel"),
				*SessionId.ToString(), SampleIndex, *UEnum::GetValueAsString(State.Behavior), State.FishEffortRatio,
				*State.SwimHeading.ToCompactString(), *ClientFish->GetActorForwardVector().ToCompactString(), *ClientFish->GetMouthWorldLocation().ToCompactString(),
				*ClientHook->GetActorLocation().ToCompactString(), VisualRoot->GetRelativeRotation().Roll,
				State.IntendedSwimSpeedCentimetersPerSecond, Server->GetNetMode(), Client->GetNetMode()));
			if (++SampleIndex == static_cast<int32>(UE_ARRAY_COUNT(Samples))) return true;
			return !Publish();
		}
	private:
		static void EnableAnimationTick(USkeletalMeshComponent& Mesh)
		{
			// 无视视口可见性，但仍由正常世界Tick运行正式AnimBP；不手工播放序列或评估参考姿态。
			Mesh.VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh.bEnableUpdateRateOptimizations = false;
			Mesh.SetComponentTickEnabled(true);
		}
		bool BeginAnimationObservation(ACatFishEncounterActor& Fish, FAnimationObservation& Observation, const TCHAR* Side)
		{
			USkeletalMeshComponent* Mesh = Fish.FindComponentByClass<USkeletalMeshComponent>();
			if (!Test->TestTrue(FString::Printf(TEXT("%s正式动画网格就绪"), Side),
				Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetAttachParent())) return false;
			EnableAnimationTick(*Mesh);
			if (!Test->TestTrue(FString::Printf(TEXT("%s运行正式AnimBP且动画未暂停"), Side),
				Mesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint && !Mesh->bPauseAnims
				&& Mesh->GetAnimInstance() && Mesh->GetAnimInstance()->GetClass() == ExpectedAnimClass.Get())) return false;
			int32 BoneIndex = Mesh->GetBoneIndex(AnimatedMouthBone);
			if (!Test->TestTrue(FString::Printf(TEXT("%s正式动画网格存在已审计上唇骨Bone014"), Side), BoneIndex != INDEX_NONE)) return false;
			FVector ReferenceMouth = FVector::ZeroVector;
			for (; BoneIndex != INDEX_NONE; BoneIndex = Mesh->GetBoneIndex(Mesh->GetParentBone(Mesh->GetBoneName(BoneIndex))))
				ReferenceMouth = Mesh->GetRefPoseTransform(BoneIndex).TransformPosition(ReferenceMouth);
			if (!Test->TestTrue(FString::Printf(TEXT("%s上唇骨参考位置与正式静态嘴标定一致"), Side),
				ReferenceMouth.Equals(MeshMouthLocal, 0.01))) return false;
			Observation = FAnimationObservation{};
			Observation.Mesh = Mesh;
			Observation.LastBoneRevision = Mesh->GetBoneTransformRevisionNumber();
			return true;
		}
		bool ObserveAnimatedMouth(ACatFishEncounterActor& Fish, ACatFishingHookActor& Hook,
			FAnimationObservation& Observation, const TCHAR* Side)
		{
			USkeletalMeshComponent* Mesh = Observation.Mesh.Get();
			if (!Test->TestNotNull(FString::Printf(TEXT("%s动画采样期间网格仍存在"), Side), Mesh)) return false;
			const uint32 Revision = Mesh->GetBoneTransformRevisionNumber();
			if (Revision == Observation.LastBoneRevision) return true;
			Observation.LastBoneRevision = Revision;
			// 复制到达后先让动画实例和状态混合经过实际世界帧，不把到达当帧的旧姿态算入结果。
			if (++Observation.FinalizedFrames <= 3) return true;
			UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
			const FEnumProperty* MotionProperty = AnimInstance ? FindFProperty<FEnumProperty>(AnimInstance->GetClass(), TEXT("MotionIntent")) : nullptr;
			if (!Test->TestTrue(FString::Printf(TEXT("%s运行中AnimBP消费本快照的运动意图"), Side), MotionProperty
				&& MotionProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(MotionProperty->ContainerPtrToValuePtr<void>(AnimInstance))
					== static_cast<uint64>(Samples[SampleIndex].Motion))) return false;
			const FVector AnimatedMouth = Mesh->GetSocketTransform(AnimatedMouthBone, RTS_Component).GetLocation();
			const FVector AnimatedMouthWorld = Mesh->GetSocketLocation(AnimatedMouthBone);
			if (Observation.CheckedFrames++ == 0) Observation.FirstAnimatedMouth = AnimatedMouth;
			Observation.MaximumReferenceDeviation = FMath::Max(Observation.MaximumReferenceDeviation, FVector::Distance(AnimatedMouth, MeshMouthLocal));
			Observation.MaximumAnimatedMovement = FMath::Max(Observation.MaximumAnimatedMovement, FVector::Distance(AnimatedMouth, Observation.FirstAnimatedMouth));
			Observation.MaximumHookError = FMath::Max(Observation.MaximumHookError, FVector::Distance(AnimatedMouthWorld, Hook.GetActorLocation()));
			Observation.MaximumPhysicalMouthError = FMath::Max(Observation.MaximumPhysicalMouthError, FVector::Distance(AnimatedMouthWorld, Fish.GetMouthWorldLocation()));
			const UBoxComponent* Collision = Fish.FindComponentByClass<UBoxComponent>();
			if (!Test->TestTrue(FString::Printf(TEXT("%s动画嘴补偿及翻肚不改变Actor局部查询框 Sample=%d Frame=%d"),
				Side, SampleIndex, Observation.CheckedFrames), Collision
				&& Collision->GetRelativeLocation().Equals(ExpectedCollisionCenter, 0.001)
				&& Collision->GetUnscaledBoxExtent().Equals(ExpectedCollisionExtent, 0.001))) return false;
			return Test->TestTrue(FString::Printf(TEXT("%s实际动画嘴在Sample=%d Frame=%d同时贴合钩与物理嘴 HookErrorCm=%.6f PhysicalErrorCm=%.6f"),
				Side, SampleIndex, Observation.CheckedFrames, Observation.MaximumHookError, Observation.MaximumPhysicalMouthError),
				Observation.MaximumHookError <= 0.05 && Observation.MaximumPhysicalMouthError <= 0.05);
		}
		bool VerifyAnimationEvidence(const FAnimationObservation& Observation, const TCHAR* Side)
		{
			const bool bLeftReference = Test->TestTrue(FString::Printf(TEXT("%s真实动画嘴确实偏离参考姿态，未冻结到静态嘴"), Side), Observation.MaximumReferenceDeviation > 0.02);
			const bool bMovedOverTime = Test->TestTrue(FString::Printf(TEXT("%s多个已完成动画帧之间嘴骨确实运动"), Side), Observation.MaximumAnimatedMovement > 0.001);
			Test->AddInfo(FString::Printf(TEXT("Event=fish_animated_mouth_network_sample SessionId=%s Side=%s Sample=%d Motion=%s Bone=Bone014 CheckedFrames=%d ReferenceDeviationCm=%.6f AnimatedMovementCm=%.6f MaxHookErrorCm=%.6f MaxPhysicalMouthErrorCm=%.6f VisualScale=%.2f Animation=FormalAnimBP Evidence=runtime_behavior"),
				*SessionId.ToString(), Side, SampleIndex, *UEnum::GetValueAsString(Samples[SampleIndex].Motion), Observation.CheckedFrames,
				Observation.MaximumReferenceDeviation, Observation.MaximumAnimatedMovement, Observation.MaximumHookError,
				Observation.MaximumPhysicalMouthError, VisualScale));
			return bLeftReference && bMovedOverTime;
		}
		bool Publish()
		{
			bAnimationSamplingInitialized = false;
			ServerAnimation = FAnimationObservation{};
			ClientAnimation = FAnimationObservation{};
			const auto& Sample = Samples[SampleIndex];
			const float ActiveLoad = Sample.Motion == ECatFishMotionIntent::AutoHauling ? 0.0f : 0.5f;
			if (!Test->TestTrue(TEXT("服务器提交下一份行为快照"), ServerFish->ApplyFightStepFromAuthority(
				Sample.Motion, 600.0, Sample.Position, 0.05f, ActiveLoad, ActiveLoad, Sample.Speed,
				false, false, FVector::UpVector, Sample.Heading, Sample.Behavior, Sample.Effort, Sample.BodyHeading))) return false;
			// 后续快照不重新贴钩；附着关系应随着同一步实际鱼身姿态自然移动。
			return Test->TestTrue(TEXT("服务器后续状态保持鱼钩附着及嘴点同位"), ServerHook.IsValid()
				&& ServerHook->GetAttachParentActor() == ServerFish.Get()
				&& ServerHook->GetActorLocation().Equals(ServerFish->GetMouthWorldLocation(), 0.0001));
		}
		FVector ExpectedMouth() const
		{
			const auto& Sample = Samples[SampleIndex];
			return Sample.Position + Sample.BodyHeading.Rotation().RotateVector(BodyGeometry.MouthLocalPositionCentimeters);
		}
		bool Matches(const ACatFishEncounterActor& Fish) const
		{
			const auto& State = Fish.GetPresentationState();
			const auto& Sample = Samples[SampleIndex];
			return State.Behavior == Sample.Behavior && State.MotionIntent == Sample.Motion
				&& FMath::IsNearlyEqual(State.CurrentLineLength, 600.0, 0.0001)
				&& FMath::IsNearlyEqual(State.FishEffortRatio, Sample.Effort, 0.0001f)
				&& FMath::IsNearlyEqual(State.IntendedSwimSpeedCentimetersPerSecond, Sample.Speed, 0.0001f)
				&& State.SwimHeading.Equals(Sample.Heading, 0.0001)
				&& FMath::IsNearlyEqual(State.VisualScale, VisualScale, 0.0001f)
				&& Fish.GetActorLocation().Equals(Sample.Position, 0.05)
				&& Fish.GetActorForwardVector().Equals(Sample.BodyHeading, 0.0001)
				&& Fish.GetMouthWorldLocation().Equals(ExpectedMouth(), 0.05);
		}
		FAutomationTestBase* Test;
		double Started;
		int32 SampleIndex = 0;
		FGuid SessionId = FGuid::NewGuid();
		FGuid CastId = FGuid::NewGuid();
		TWeakObjectPtr<ACatFishEncounterActor> ServerFish;
		TWeakObjectPtr<ACatFishingHookActor> ServerHook;
		TWeakObjectPtr<UClass> ExpectedAnimClass;
		FAnimationObservation ServerAnimation;
		FAnimationObservation ClientAnimation;
		double ServerAnimationStartedSeconds = 0.0;
		double ClientAnimationStartedSeconds = 0.0;
		bool bAnimationSamplingInitialized = false;
		FCatFishBodyGeometry BodyGeometry;
		FVector MeshMouthLocal = FVector::ZeroVector;
		FVector ExpectedCollisionCenter = FVector::ZeroVector;
		FVector ExpectedCollisionExtent = FVector::ZeroVector;
		double ExhaustedRollDegrees = 0.0;
		static constexpr float VisualScale = 1.15f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorNetworkTest,
	"Catfishing.Editor.Fishing.FishBehaviorListenClientSnapshots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("requires an idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const auto Restore = MakeShared<CatFishBehaviorNetwork::FRestore>();
	UWorld* Map = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("isolated unsaved map"), Map)) return false;
	Map->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	Map->SpawnActor<APlayerStart>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatFishBehaviorNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
