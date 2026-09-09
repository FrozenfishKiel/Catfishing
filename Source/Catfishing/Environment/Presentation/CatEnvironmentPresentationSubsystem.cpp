#include "Environment/Presentation/CatEnvironmentPresentationSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/Presentation/CatEnvironmentPresentationActor.h"
#include "Logging/CatLog.h"

// World 类型筛选流程：只接受运行时游戏世界和 PIE 世界，避免编辑器纯浏览关卡时留下临时表现 Actor。
bool UCatEnvironmentPresentationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

// BeginPlay 流程：先交给父类记录生命周期，再执行一次幂等保底检查；后续同步仍由 Actor 自己订阅 GameState。
void UCatEnvironmentPresentationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EnsurePresentationActor(InWorld);
}

// 保底生成流程：
// 1. 专用服务器直接跳过并清空本子系统的生成引用，避免服务器创建纯画面 Actor。
// 2. 本子系统已生成实例或关卡里已有任意表现 Actor 时复用现状，只写一次可检索日志。
// 3. 找不到消费者时生成一个非复制、临时的默认实例；生成失败只写 warning，不伪造 Run 或 Environment 状态。
void UCatEnvironmentPresentationSubsystem::EnsurePresentationActor(UWorld& World)
{
	if (World.GetNetMode() == NM_DedicatedServer)
	{
		SpawnedPresentationActor.Reset();
		return;
	}
	if (SpawnedPresentationActor.IsValid())
	{
		return;
	}
	for (TActorIterator<ACatEnvironmentPresentationActor> It(&World); It; ++It)
	{
		if (IsValid(*It))
		{
			UE_LOG(LogCatEnvironment, Log,
				TEXT("Event=environment_presentation_actor_existing Actor=%s"),
				*GetNameSafe(*It));
			return;
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(&World, ACatEnvironmentPresentationActor::StaticClass(),
		TEXT("CatEnvironmentPresentationActor"));
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACatEnvironmentPresentationActor* PresentationActor = World.SpawnActor<ACatEnvironmentPresentationActor>(
		ACatEnvironmentPresentationActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!PresentationActor)
	{
		UE_LOG(LogCatEnvironment, Warning,
			TEXT("Event=environment_presentation_actor_spawn_failed World=%s"),
			*GetNameSafe(&World));
		return;
	}

	PresentationActor->SetReplicates(false);
	SpawnedPresentationActor = PresentationActor;
	UE_LOG(LogCatEnvironment, Log,
		TEXT("Event=environment_presentation_actor_spawned Actor=%s"),
		*GetNameSafe(PresentationActor));
}
