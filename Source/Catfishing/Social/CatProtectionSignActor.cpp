#include "Social/CatProtectionSignActor.h"

#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

// 构造流程：开启 Actor 复制并关闭 Tick；牌子位置由服务器 Spawn/关卡摆放确定，不接受客户端持续更新。
ACatProtectionSignActor::ACatProtectionSignActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
}

// 复制声明流程：保留父类字段并复制受保护 PlayerState、范围与 Revision；不复制原始 StableNetId。
void ACatProtectionSignActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ProtectedPlayerState);
	DOREPLIFETIME(ThisClass, RadiusCentimeters);
	DOREPLIFETIME(ThisClass, Revision);
}

// 配置流程：只接受 authority、有效 PlayerState 与有限正范围；首次或覆盖提交都递增 Revision 并请求网络更新。
bool ACatProtectionSignActor::ConfigureProtection(APlayerState* InProtectedPlayerState, const double InRadiusCentimeters)
{
	if (!HasAuthority() || !InProtectedPlayerState || !FMath::IsFinite(InRadiusCentimeters) || InRadiusCentimeters <= 0.0)
	{
		return false;
	}
	ProtectedPlayerState = InProtectedPlayerState;
	RadiusCentimeters = InRadiusCentimeters;
	++Revision;
	ForceNetUpdate();
	return true;
}

// 保护判断流程：要求配置完整、目标 PlayerState 精确相同且交互位置落在球形范围内；不按名字或队伍推断。
bool ACatProtectionSignActor::ProtectsMischiefAgainst(const APlayerState* TargetPlayerState,
	const FVector& InteractionLocation) const
{
	return ProtectedPlayerState && ProtectedPlayerState == TargetPlayerState && RadiusCentimeters > 0.0
		&& !InteractionLocation.ContainsNaN()
		&& FVector::DistSquared(InteractionLocation, GetActorLocation()) <= FMath::Square(RadiusCentimeters);
}
