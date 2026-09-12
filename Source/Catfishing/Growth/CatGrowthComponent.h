#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Growth/CatGrowthTypes.h"
#include "CatGrowthComponent.generated.h"

class AController;
class UCatFishDefinition;

/** Growth 完整快照发生提交或复制变化的本机通知；订阅者必须重新读取 GetSnapshot。 */
DECLARE_MULTICAST_DELEGATE(FCatGrowthSnapshotChanged);

/**
 * Character 本局吃鱼成长组件；以鱼定义经验推进槽、按配表抽三选一、把选中的加成叠进本局 build。
 * 成长跟局走：快照挂在 Character 上，局末随之销毁，不写 Profile 也不进存档。
 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatGrowthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启组件复制并关闭 Tick；成长只由服务器吃鱼提交与玩家选择推进。 */
	UCatGrowthComponent();

	/** 注册唯一 Growth Snapshot 复制；幂等缓存只存在 authority 内存中。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供服务器最终值或客户端最近复制值；外部不能通过返回值消耗选择或伪造加成。 */
	const FCatGrowthSnapshot& GetSnapshot() const;

	/**
	 * 在实物鱼被不可逆移除前只读校验成长配置、鱼定义与本条鱼的实际重量；返回 None 才允许上层提交库存事务。
	 * 经验＝鱼种经验系数×实际重量，所以重量非法与系数未裁一样要 fail-closed。
	 */
	ECatDomainCommandError ValidateFishGrowth(const UCatFishDefinition* FishDefinition,
		double WeightKilograms) const;

	/** 实物鱼消费提交后按同一个 RequestId 增加经验（系数×重量）；重复 RequestId 只重放终态，不重复发槽。 */
	FCatDomainCommandResult ApplyCommittedFish(FGuid RequestId, const UCatFishDefinition* FishDefinition,
		double WeightKilograms);

	/**
	 * 玩家从当前这组三选一里选中一项。只接受拥有本 Character 的 Controller、当前组序号与仍在池里的那三项之一；
	 * 选中当场生效、本局永久，随后按剩余待选次数立刻抽下一组（连满时逐个弹出）。
	 */
	FCatDomainCommandResult ChooseOfferedOptionFromAuthority(AController* RequestingController, FGuid RequestId,
		ECatGrowthOptionId OptionId, int32 OfferSerial);

	/**
	 * 读取某一项本局累计生效量（已按配表上限夹住）。这是成长加成对外的唯一读口：
	 * 力量与搏斗体力上限由本组件直接写进 ASC，其余各项由各自的消费系统读这里，不自己保存第二份 build。
	 * 百分比类返回小数（+30% 返回 0.30），减益类（咬钩间隔、竿磨损）返回负数。
	 */
	double GetTotalMagnitude(ECatGrowthOptionId OptionId) const;

	/** 本机完整成长快照变化通知；LocalPlayer UI 成对订阅，领域写入者不依赖该通知推进。 */
	FCatGrowthSnapshotChanged OnSnapshotChanged;

private:
	/** 客户端收到完整 Snapshot 后只供表现读取；不会在 RepNotify 自动消费三选一。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 把一次吃鱼经验压进槽内；处理溢出和连满，满槽后按配表抽出当前这一组三选一。 */
	void AddExperienceFromCommittedFish(int32 ExperienceAmount);

	/** 待选次数为正且当前没有面板时抽一组；同次三项互不相同、跳过未解锁与已顶上限的项。 */
	void RefreshCurrentOffer();

	/** 把一次选中记进 Stacks 并按配表上限夹住；返回本次真正生效的增量（夹住后可能为 0）。 */
	double AccumulateStack(ECatGrowthOptionId OptionId, const struct FCatGrowthOptionConfig& Config);

	/** 把本次生效增量写到真正承载它的系统上；只处理由本组件直接拥有写口的那几项。 */
	void ApplyOptionEffect(ECatGrowthOptionId OptionId, double AppliedDelta);

	/** 构造操作+RequestId 的局内幂等键；身份由上层 Controller 权限另行验证。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；集中保证 UI 不漏掉成长变化。 */
	void PublishSnapshot();

	/** 吃鱼成长的唯一复制事实；经验、待选、当前面板和已叠 build 跟局走，局末随 Character 销毁。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatGrowthSnapshot Snapshot;

	/** 成长命令的首次完整终态；防止同一 RequestId 重复吃出经验或重复选中同一组。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;
};
