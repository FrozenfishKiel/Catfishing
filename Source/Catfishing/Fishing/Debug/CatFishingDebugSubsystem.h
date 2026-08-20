#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatFishingDebugSubsystem.generated.h"

class APlayerController;

/**
 * 前期无美术资源时的钓鱼调试可视化：水域边界、瞄准落点、蓄力抛物线、窝点圈、钩/鱼/鱼线与屏幕状态条。
 * 纯本地绘制，不写任何权威状态。CVar `cat.Fishing.Debug`（默认 1）控制开关；Shipping 构建整体不编译绘制体。
 */
UCLASS()
class CATFISHING_API UCatFishingDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	void DrawWaterRegions() const;
	void DrawChumFields() const;
	void DrawRodTips() const;
	/** 抄网射线：从抄手沿面向水平画一条长度 = min(全局, 抄网 DA) 的线段。 */
	void DrawScoopRange(APlayerController* Controller) const;
	/** 鱼身上的可捞圆圈（半径来自鱼 DA）；够得着画绿色、够不着画红色，颜色直接调权威判定函数得出。 */
	void DrawScoopTargetCircle(APlayerController* Controller, const class ACatFishEncounterActor* Fish) const;
	/** 抛竿瞄准落点球+圈（纯调试，受 cat.Fishing.Debug 控制）。 */
	void DrawCastAimPoint(APlayerController* Controller) const;
	/**
	 * 窝料蓄力抛物线 + 落点球 + 蓄力百分比（玩法反馈，默认常开，受 cat.Fishing.ChumPreview 控制）。
	 * 纯本地绘制的占位表现：有美术的 Spline/Niagara 后把 CVar 设 0，蓝图调同一个 PredictChumThrow 自绘即可。
	 */
	void DrawChumChargePreview(APlayerController* Controller) const;
	void DrawSession(APlayerController* Controller, bool bFullDetail) const;
	void PushStatus(int32 Slot, const FColor& Color, const FString& Text) const;
};
