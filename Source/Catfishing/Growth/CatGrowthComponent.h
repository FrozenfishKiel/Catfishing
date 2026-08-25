#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Growth/CatGrowthTypes.h"
#include "CatGrowthComponent.generated.h"

class UCatFishDefinition;

/** Growth 完整快照发生提交或复制变化的本机通知；订阅者必须重新读取 GetSnapshot。 */
DECLARE_MULTICAST_DELEGATE(FCatGrowthSnapshotChanged);

/** Character 本局吃鱼成长组件；以鱼定义经验推进槽和待选次数，Buff 未裁时不生成临时效果。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatGrowthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启组件复制并关闭 Tick；成长只由服务器吃鱼提交推进。 */
	UCatGrowthComponent();

	/** 注册唯一 Growth Snapshot 复制；幂等缓存只存在 authority 内存中。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供服务器最终值或客户端最近复制值；外部不能通过返回值消耗选择或伪造 Buff。 */
	const FCatGrowthSnapshot& GetSnapshot() const;

	/** 在实物鱼被不可逆移除前只读校验成长配置和鱼定义经验；返回 None 才允许上层提交 Items 事务。 */
	ECatDomainCommandError ValidateFishGrowth(const UCatFishDefinition* FishDefinition) const;

	/** 实物鱼消费提交后按同一个 RequestId 增加经验；重复 RequestId 只重放终态，不重复发槽。 */
	FCatDomainCommandResult ApplyCommittedFish(FGuid RequestId, const UCatFishDefinition* FishDefinition);

	/** 本机完整成长快照变化通知；LocalPlayer UI 成对订阅，领域写入者不依赖该通知推进。 */
	FCatGrowthSnapshotChanged OnSnapshotChanged;

private:
	/** 客户端收到完整 Snapshot 后只供表现读取；不会在 RepNotify 自动消费三选一。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 把一次吃鱼经验压进槽内；处理溢出和连满，只增加 PendingChoiceCount，不生成未裁 Buff。 */
	void AddExperienceFromCommittedFish(int32 ExperienceAmount);

	/** 构造操作+RequestId 的局内幂等键；身份由上层 Controller 权限另行验证。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；集中保证 UI 不漏掉成长变化。 */
	void PublishSnapshot();

	/** 吃鱼成长的唯一复制事实；经验和待选次数跟局走，局末随 Character 销毁。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatGrowthSnapshot Snapshot;

	/** 成长命令的首次完整终态；防止同一 RequestId 重复吃出经验或重复满槽。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;
};
