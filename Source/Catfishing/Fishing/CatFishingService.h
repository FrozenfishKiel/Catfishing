#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishingService.generated.h"

class ACatCharacter;
class ACatFishingSession;
class UCatFishDefinition;

/** 一局服务器 Fishing 入口；创建/查询/终止会话并把所有阶段写入留给会话内 StateTree。 */
UCLASS()
class CATFISHING_API UCatFishingService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建服务；客户端通过复制 Session 观察。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时先终止所有未结算会话，再清弱映射。 */
	virtual void Deinitialize() override;

	/** 通过 Boundary Start/Cast 获取 PreCast context 与 EncounterSpec，再为该身份建立唯一 StateTree 会话；巨鱼成功后才附带广播可选 Social 提示。 */
	FCatFishingStartResult StartFishingSession(AController* FisherController, FGuid RequestId);

	/**
	 * 按钓手当前装的鱼漂算出这一竿浮漂落在哪儿；返回 false 表示没漂、漂没进运行目录或漂的射程/精准度非法，此时不该抛竿。
	 *
	 * 落点 = 猫的位置 + 水平朝向 × 射程 + 一个半径由精准度决定的水平随机偏移。
	 * 飞书的抛竿规则本来是"点击水面即抛、点哪落哪、点击距离 ≤ 射程"，但项目还没有点击瞄准输入，
	 * 所以这里退化成"朝哪抛哪"，射程直接充当抛投距离；接上瞄准后只要把方向与距离换成点击结果，偏移这一段可以原样保留。
	 * 偏移只走水平面：射程和精准度在装备册里都是水面上的距离，把高度差算进去会让落点被地形起伏拉偏。
	 * 随机流用 RequestId 播种而不是全局随机，同一次抛竿重放会落在同一点，审计和自动化都能复现。
	 * 它是抛竿几何的唯一实现，所以直接公开给自动化断言，免得为了验一段向量运算搭起一整局。
	 */
	static bool TryResolveFloatCastLocation(const ACatCharacter& Fisher, const FGuid& RequestId, FVector& OutCastLocation);

	/** 把巨鱼搏斗协作意图转给指定会话；会话用统一谓词拒绝非 Active、倒地、无当前 Character 或力量/体力非正的请求者。 */
	FCatDomainCommandResult SubmitFightAssist(FGuid FishingSessionId, AController* AssistingController,
		FGuid RequestId, int64 ExpectedRevision);

	/** 把钓手当前按住的遛鱼操作（拖/松/无）转给他自己的活跃会话；按服务器身份找会话，客户端不指定会话 ID，没有活跃会话返回 NotFound。 */
	FCatDomainCommandResult SubmitFightIntent(AController* FisherController, ECatFishingFightIntent Intent);

	/** 把 NearShore 抢抄意图转给指定会话；服务不自己创建鱼或选择胜者。 */
	FCatScoopResult RequestScoop(FGuid FishingSessionId, AController* ScoopingController, const FCatScoopCommand& Command);

	/** Character 失去占有、倒地或销毁时终止所有相关未结算会话；不恢复旧半场。 */
	void TerminateSessionsForCharacter(const ACatCharacter* Character);

	/** Host teardown 关闭入口并终止所有未结算会话。 */
	void CloseCommandsAndTerminateAll();

private:
	/** 清除已销毁或已终态 Session 弱引用，并同时释放对应钓手的单活跃槽位。 */
	void CompactSessions();

	/** FishingSessionId 到服务器 Actor 弱引用；Actor/StateTree 自己持有阶段真相。 */
	TMap<FGuid, TWeakObjectPtr<ACatFishingSession>> Sessions;

	/** 会话 ID 到初始钓手私有身份；服务压缩终态时据此精确释放单活跃索引。 */
	TMap<FGuid, FString> SessionFisherById;

	/** 钓手私有身份到当前唯一非终态会话；同一玩家不能并行创建第二个未结算 Attempt。 */
	TMap<FString, FGuid> ActiveSessionByFisher;

	/** Boundary Cast Operation 到最终 Service Start 结果；同一 Cast 重放必须返回同一个 SessionId 或同一个创建失败。 */
	TMap<FString, FCatFishingStartResult> StartResultByBoundaryOperation;

	/** Boundary Attempt 到最终 Service Start 结果；同一 Start 重放先用它绕过当前命令关闭、活跃会话和 Character 生命周期漂移。 */
	TMap<FGuid, FCatFishingStartResult> StartResultByAttempt;

	/** teardown 后永久拒绝本 World 新会话。 */
	bool bCommandsOpen = true;
};
