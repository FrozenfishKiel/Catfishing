#include "Fishing/Debug/CatFishingDebugSubsystem.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/CatWaterRegion.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "Fishing/CatFishingSettings.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"
#include "UI/CatFishingViewBridge.h"

#if !UE_BUILD_SHIPPING
namespace CatFishingDebugCommands
{
	// 鱼定义选择流程：显式参数先按稳定 FishDefinitionId 查找，再同步加载配置里的候选资产名做兼容。
	// 没有参数时优先返回可进鱼缸展示的正式鱼，若都不可展示则退到第一条可运行定义。
	// 同步加载只发生在非 Shipping 调试命令里，避免正式链路为验收便利付成本。
	static UCatFishDefinition* ResolveFishDefinition(const TArray<FString>& Args)
	{
		const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
		if (!Catalog)
		{
			return nullptr;
		}
		if (!Args.IsEmpty() && !Args[0].IsEmpty())
		{
			const FName RequestedName(*Args[0]);
			if (UCatFishDefinition* Definition = Catalog->FindRuntimeDefinition(RequestedName))
			{
				return Definition;
			}
			for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Catalog->Definitions)
			{
				UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
				const FName AssetName(*DefinitionRef.ToSoftObjectPath().GetAssetName());
				if (Definition && Definition->IsRuntimeDefinitionReady() && AssetName == RequestedName)
				{
					return Definition;
				}
			}
			return nullptr;
		}

		UCatFishDefinition* FirstReadyDefinition = nullptr;
		for (const TSoftObjectPtr<UCatFishDefinition>& DefinitionRef : Catalog->Definitions)
		{
			UCatFishDefinition* Definition = DefinitionRef.LoadSynchronous();
			if (!Definition || !Definition->IsRuntimeDefinitionReady())
			{
				continue;
			}
			if (!FirstReadyDefinition)
			{
				FirstReadyDefinition = Definition;
			}
			if (Definition->bTankDisplayEligible)
			{
				return Definition;
			}
		}
		return FirstReadyDefinition;
	}

	// 玩家选择流程：按当前 World 的 PlayerController 迭代顺序选择目标；World 缺失或索引非法时返回空，
	// 让外层统一记录拒绝日志。PlayerIndex 只影响调试命令落点，不进入领域身份。
	static APlayerController* ResolvePlayerController(UWorld* World, const int32 PlayerIndex)
	{
		if (!World || PlayerIndex < 0)
		{
			return nullptr;
		}
		int32 CurrentIndex = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller)
			{
				continue;
			}
			if (CurrentIndex == PlayerIndex)
			{
				return Controller;
			}
			++CurrentIndex;
		}
		return nullptr;
	}

	// 给鱼提交流程：
	// 1. 先解析鱼定义和目标玩家依赖；任一关键对象缺失时只写拒绝日志，不伪造容器状态。
	// 2. 独立鱼护对象尚未接入时直接拒绝，不再从 Character 读取旧个人鱼护 ID。
	// 3. 后续正式鱼护对象完成后，本命令再改为从该对象读取容器快照并走 Items 捕获事务。
	static void GiveFishToPlayer(const TArray<FString>& Args, UWorld* World)
	{
		UCatFishDefinition* Definition = ResolveFishDefinition(Args);
		int32 PlayerIndex = 0;
		if (Args.IsValidIndex(2))
		{
			PlayerIndex = FMath::Max(0, FCString::Atoi(*Args[2]));
		}
		APlayerController* Controller = ResolvePlayerController(World, PlayerIndex);
		const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
		UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
		if (!World || !Definition || !Controller || !PlayerState || !PlayerState->GetUniqueId().IsValid() || !Items)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_debug_give_fish_rejected Reason=DependencyUnavailable World=%s Controller=%s FishDefinition=%s"),
				World ? *World->GetName() : TEXT("None"),
				*GetNameSafe(Controller),
				Definition ? *Definition->FishDefinitionId.ToString() : TEXT("None"));
			return;
		}
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_debug_give_fish_rejected Reason=FishGuardActorUnavailable FishDefinition=%s PlayerIndex=%d"),
			*Definition->FishDefinitionId.ToString(),
			PlayerIndex);
	}

	/** 非 Shipping 构建里的给鱼控制台入口；它代表一条会修改权威 Items 状态的作弊命令，执行时仍走正式捕获提交事务。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdGiveFish(
		TEXT("cat.Fishing.Debug.GiveFish"),
		TEXT("调试给鱼入口；走正式个人鱼护 CommitCapture 事务。参数：FishDefinitionId WeightKg PlayerIndex。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveFishToPlayer),
		ECVF_Cheat);
}
#endif

#if ENABLE_DRAW_DEBUG
static TAutoConsoleVariable<int32> CVarCatFishingDebug(
	TEXT("cat.Fishing.Debug"), 0,
	TEXT("钓鱼调试可视化：0=全部关闭；1=全量（水域边界/瞄准落点/蓄力抛物线/窝点/竿尖球/钩鱼球/抄网射线与鱼圈/鱼线/状态条）；")
	TEXT("2=精简（只保留抄网射线、鱼身上的可捞圆圈、鱼线和状态条，关闭其余调试球/圈/线）。")
	TEXT("抄网圆圈绿色=当前按 F 抄得到，红色=够不着；颜色直接来自服务器同一个判定函数。"),
	ECVF_Default);

// 与上面那个 debug 总开关刻意分开：窝料蓄力抛物线不是"调试信息"，而是**玩法必需的瞄准反馈**——
// 看不见抛物线就没法判断该蓄多久，等同于闭眼投掷。所以它默认常开，且不受 cat.Fishing.Debug 0 影响。
// 当前这条线是**占位表现**：等美术给出 Spline/Niagara 轨迹后，把本 CVar 设 0 关掉即可，
// 蓝图侧可以直接调用同一个 UCatFishingAimLibrary::PredictChumThrow 拿到路径点数组自行绘制，
// 两者用的是同一份预测，替换后落点不会有任何偏差。
static TAutoConsoleVariable<int32> CVarCatFishingChumPreview(
	TEXT("cat.Fishing.ChumPreview"), 1,
	TEXT("窝料蓄力抛物线预览：1=显示（默认，纯本地表现，不参与任何判定）；0=关闭（换成美术资源后设 0）。"),
	ECVF_Default);

namespace
{
	/**
	 * 竿尖绘制位置：优先蓝图自放的表现标记——在 Rod 蓝图 VisualRoot 下任意加一个 Scene/Arrow 组件，
	 * Details→Component Tags 加 "RodTipMarker"，调试线/竿尖球就跟随它；没有标记时回退权威竿尖锚点。
	 * 仅表现读取：服务器抛竿原点、线长判定仍只用权威锚点，不受该标记影响。
	 */
	FVector ResolveRodTipDrawLocation(const ACatFishingRodActor& Rod)
	{
		static const FName RodTipMarkerTag(TEXT("RodTipMarker"));
		TInlineComponentArray<USceneComponent*> Components(&Rod);
		for (const USceneComponent* Component : Components)
		{
			if (Component && Component->ComponentHasTag(RodTipMarkerTag))
			{
				return Component->GetComponentLocation();
			}
		}
		return Rod.GetRodTipWorldTransform().GetLocation();
	}
}
#endif

bool UCatFishingDebugSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

TStatId UCatFishingDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCatFishingDebugSubsystem, STATGROUP_Tickables);
}

void UCatFishingDebugSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World) return;
	APlayerController* Controller = World->GetFirstPlayerController();
	if (!Controller || !Controller->IsLocalController()) return;
	// 蓄力抛物线先画，且不看 debug 总开关：它是玩法反馈（不知道蓄了多少就没法瞄），不是调试信息。
	if (CVarCatFishingChumPreview.GetValueOnGameThread() != 0)
	{
		DrawChumChargePreview(Controller);
	}
	const int32 DebugMode = CVarCatFishingDebug.GetValueOnGameThread();
	if (DebugMode == 0) return;
	const bool bFullDetail = DebugMode != 2; // 2=精简：只留抄网射线+鱼圈+鱼线+状态条。
	if (bFullDetail)
	{
		DrawWaterRegions();
		DrawChumFields();
		DrawRodTips();
		DrawCastAimPoint(Controller);
	}
	DrawScoopRange(Controller);
	DrawSession(Controller, bFullDetail);
#endif
}

void UCatFishingDebugSubsystem::PushStatus(const int32 Slot, const FColor& Color, const FString& Text) const
{
#if ENABLE_DRAW_DEBUG
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()) * 16ULL + Slot, 0.5f, Color, Text);
	}
#endif
}

// 水域边界：Include 多边形青色、Exclude 橙色，画在水面高度；调试线仅本地，不参与任何判定。
void UCatFishingDebugSubsystem::DrawWaterRegions() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	for (TActorIterator<ACatWaterRegion> It(World); It; ++It)
	{
		const ACatWaterRegion* Region = *It;
		if (!IsValid(Region) || !Region->HasValidBakedGeometry()) continue;
		const FCatWaterGeometryCache& Cache = Region->GetBakedGeometryForDebug();
		auto DrawPolygon = [World, &Cache](const FCatWaterBakedPolygon& Polygon, const FColor Color)
		{
			const int32 Num = Polygon.Vertices.Num();
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const FVector2D& A2 = Polygon.Vertices[Index];
				const FVector2D& B2 = Polygon.Vertices[(Index + 1) % Num];
				const FVector A = Cache.PlaneToWorld.TransformPosition(FVector(A2.X, A2.Y, 0.0));
				const FVector B = Cache.PlaneToWorld.TransformPosition(FVector(B2.X, B2.Y, 0.0));
				DrawDebugLine(World, A, B, Color, false, -1.0f, 0, 4.0f);
			}
		};
		for (const FCatWaterBakedPolygon& Polygon : Cache.IncludePolygons) DrawPolygon(Polygon, FColor::Cyan);
		for (const FCatWaterBakedPolygon& Polygon : Cache.ExcludePolygons) DrawPolygon(Polygon, FColor::Orange);
	}
#endif
}

// 窝点：绿色圆圈 + 剩余时间文字；直接读 GameState 的复制公开数组，不依赖表现 Actor 是否生成。
void UCatFishingDebugSubsystem::DrawChumFields() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const ACatfishingGameState* GameState = World->GetGameState<ACatfishingGameState>();
	const UCatChumFieldReplicationComponent* Replication = GameState ? GameState->GetChumFieldReplication() : nullptr;
	if (!Replication) return;
	for (const FCatChumFieldPublicItem& State : Replication->GetPublicFields())
	{
		if (State.RadiusCentimeters <= 0.0) continue;
		const double Remaining = FMath::Max(0.0, State.ExpireServerTime - Now);
		DrawDebugCircle(World, State.CenterWorldPoint + FVector(0, 0, 4), static_cast<float>(State.RadiusCentimeters),
			48, FColor::Green, false, -1.0f, 0, 3.0f, FVector::ForwardVector, FVector::RightVector, false);
		DrawDebugString(World, State.CenterWorldPoint + FVector(0, 0, 40),
			FString::Printf(TEXT("窝点 %.0fs"), Remaining), nullptr, FColor::Green, 0.0f, true);
	}
#endif
}

// 瞄准与蓄力：抛竿落点绿球（与服务器同一函数求解）；Q 蓄力时画预测抛物线与落点球（命中水=绿，否则红）。
// 抛竿瞄准落点（纯调试）：受 cat.Fishing.Debug 控制。
void UCatFishingDebugSubsystem::DrawCastAimPoint(APlayerController* Controller) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	APawn* Pawn = Controller->GetPawn();
	if (!Pawn) return;
	FCatWaterRegionHandle AimRegion;
	FVector AimLanding;
	if (UCatFishingAimLibrary::ResolveCastAimPoint(World, Pawn->GetPawnViewLocation(),
		Controller->GetControlRotation(), AimRegion, AimLanding))
	{
		DrawDebugSphere(World, AimLanding, 20.0f, 12, FColor::Green, false, -1.0f, 0, 2.0f);
		DrawDebugCircle(World, AimLanding + FVector(0, 0, 2), 60.0f, 24, FColor::Green, false, -1.0f, 0, 1.5f,
			FVector::ForwardVector, FVector::RightVector, false);
	}
#endif
}

// 窝料蓄力抛物线（玩法反馈，默认常开，受 cat.Fishing.ChumPreview 控制）。
// 纯本地绘制：不复制、不提交任何命令，别的玩家看不到你的预览线——这正是想要的，瞄准辅助只该给瞄准的人看。
// 用 GetLocalChumChargeStartTime 而非权威那份：后者只在服务器写，客户端读永远是 -1，会变成只有主机看得见。
void UCatFishingDebugSubsystem::DrawChumChargePreview(APlayerController* Controller) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	APawn* Pawn = Controller->GetPawn();
	const ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller);
	const UCatFishingCommandComponent* Commands = CatController ? CatController->GetFishingCommandComponent() : nullptr;
	const double ChargeStart = Commands ? Commands->GetLocalChumChargeStartTime() : -1.0;
	if (!Pawn || ChargeStart < 0.0) return; // <0 = 当前没按住 Q，不画。

	const float Alpha = UCatFishingAimLibrary::ChargeAlphaFromHeldSeconds(
		static_cast<float>(World->GetTimeSeconds() - ChargeStart));
	TArray<FVector> Path;
	FVector Landing;
	FCatWaterRegionHandle Region;
	bool bHitWater = false;
	// 与服务器投放走同一个 PredictChumThrow：画出来的落点就是真实落点，换成美术资源后也应继续调它。
	UCatFishingAimLibrary::PredictChumThrow(World, Pawn->GetActorLocation(), Controller->GetControlRotation(),
		Alpha, Path, Landing, Region, bHitWater);
	// 黄=会落进水里（有效打窝），红=落在岸上（服务器会拒绝），颜色直接来自预测结果，不是另判一次。
	const FColor PathColor = bHitWater ? FColor::Yellow : FColor::Red;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		DrawDebugLine(World, Path[Index - 1], Path[Index], PathColor, false, -1.0f, 0, 2.0f);
	}
	DrawDebugSphere(World, Landing, 24.0f, 12, bHitWater ? FColor::Green : FColor::Red, false, -1.0f, 0, 2.0f);
	PushStatus(0, FColor::Yellow, FString::Printf(TEXT("窝料蓄力 %.0f%%  松开 Q 投出"), Alpha * 100.0f));
#endif
}

// 会话状态：钩/鱼位置球、竿尖到鱼的连线、近岸圈与规格 7.1 的状态提示文字。
// bFullDetail=false（精简模式）：只画鱼线和状态文字，跳过钩球/鱼球/近岸圈；浮漂正式表现由 Hook 自己驱动。
void UCatFishingDebugSubsystem::DrawSession(APlayerController* Controller, const bool bFullDetail) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	ACatFishingSession* Session = UCatFishingViewBridge::FindFishingSessionForPlayerState(World, Controller->PlayerState);
	APawn* Pawn = Controller->GetPawn();

	// 装备/库存常驻显示：Debug 也读取 InventorySlots 这个事实源，再按 Chum 定义筛数量；不读兼容汇总，避免装备型格子或旧摘要污染窝料提示。
	if (const ACatCharacter* Character = Cast<ACatCharacter>(Pawn))
	{
		if (const UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent())
		{
			const FCatEquipmentLoadoutSnapshot& Loadout = Equipment->GetSnapshot();
			int32 ChumCount = 0;
			for (const FCatRunInventorySlot& Slot : Loadout.InventorySlots)
			{
				const UCatEquipmentDefinition* Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
					Slot.DefinitionId);
				if (Definition && Definition->Kind == ECatEquipmentKind::Chum && Slot.Quantity > 0)
				{
					ChumCount += Slot.Quantity;
				}
			}
			PushStatus(1, FColor::White, FString::Printf(TEXT("竿耐久 %.0f  窝料 x%d"), Loadout.RodDurability, ChumCount));
		}
	}

	if (!Session)
	{
		PushStatus(2, FColor::Silver, TEXT("R 放竿/操作/离开 · E 准星交互/拾取 · 操作中松开左键=抛竿 · Q 长按=打窝"));
		return;
	}
	const FCatFishingSessionSnapshot& Snapshot = Session->GetSnapshot();

	const ACatFishingRodActor* Rod = Snapshot.RodActor;
	ACatFishingHookActor* Hook = Snapshot.HookActor;
	const ACatFishEncounterActor* Fish = Snapshot.FishEncounterActor;
	const FVector RodTip = Rod ? ResolveRodTipDrawLocation(*Rod) : FVector::ZeroVector;
	if (Hook)
	{
		// Debug 只为正式浮漂表现着色/画锚点，不再写 VisualRoot，避免关闭 Debug 后玩法反馈一起消失。
		FColor HookColor = FColor::Blue;
		switch (Hook->GetPresentationState().BobberMode)
		{
		case ECatFishingBobberPresentationMode::BiteWarning:
			HookColor = FColor::Yellow;
			break;
		case ECatFishingBobberPresentationMode::Sunk:
			HookColor = FColor::Red;
			break;
		default:
			break;
		}
		// 搏斗/近岸阶段钩 Actor 已跟随鱼移动：不再单独画钩球（避免与鱼球重叠），线也直接画到鱼。
		if (!Fish)
		{
			const FVector HookDrawLocation = Hook->GetPresentationVisualWorldLocation();
			if (bFullDetail) DrawDebugSphere(World, HookDrawLocation, 14.0f, 10, HookColor, false, -1.0f, 0, 2.0f);
			if (Rod) DrawDebugLine(World, RodTip, HookDrawLocation, FColor::Silver, false, -1.0f, 0, 1.5f);
		}
	}
	if (Fish)
	{
		const bool bStruggling = Snapshot.FishMotionIntent == ECatFishMotionIntent::StrugglingOutward;
		// 与鱼的表现偏移对齐：球和线画到 VisualRoot 实际所在处（含下沉/前后/左右），而不是水面上的权威位置。
		const FVector FishDrawLocation = Fish->GetVisualWorldLocation();
		if (bFullDetail)
		{
			DrawDebugSphere(World, FishDrawLocation, 22.0f, 12, bStruggling ? FColor::Red : FColor::Green,
				false, -1.0f, 0, 2.5f);
		}
		if (Rod) DrawDebugLine(World, RodTip, FishDrawLocation,
			bStruggling ? FColor::Red : FColor::Green, false, -1.0f, 0, 2.0f);
	}

	double CatStamina = 0.0;
	if (const IAbilitySystemInterface* AbilityInterface = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* AbilitySystem = AbilityInterface->GetAbilitySystemComponent())
		{
			CatStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		}
	}

	switch (Snapshot.Phase)
	{
	case ECatFishingPhase::Waiting:
		if (Hook && Hook->GetPresentationState().BobberMode == ECatFishingBobberPresentationMode::BiteWarning)
		{
			PushStatus(2, FColor::Yellow, TEXT("浮漂快速抖动…等它下沉再提竿"));
		}
		else
		{
			PushStatus(2, FColor::Silver, TEXT("等待咬钩…（Q 可补窝 / X 收竿零损失）"));
		}
		break;
	case ECatFishingPhase::Probe:
		PushStatus(2, FColor::Yellow, TEXT("鱼在试探…别急着提竿"));
		break;
	case ECatFishingPhase::TrueBiteWindow:
		PushStatus(2, FColor::Red, TEXT("咬钩了！按左键提竿！（1 秒内 = 完美）"));
		break;
	case ECatFishingPhase::HookedFight:
	{
		// 搏斗中同样画圈：抄网对 HookedFight 也开放，鱼被收到射线够得着的位置就能直接抄上来。
		DrawScoopTargetCircle(Controller, Fish);
		const bool bStruggling = Snapshot.FishMotionIntent == ECatFishMotionIntent::StrugglingOutward;
		PushStatus(2, bStruggling ? FColor::Red : FColor::Green, bStruggling
			? TEXT("鱼在发力！按住右键松开线杯，让鱼自由带线！")
			: TEXT("鱼累了！按住左键拖回来！"));
		const double LineLength = Fish ? Fish->GetPresentationState().CurrentLineLength : 0.0;
		// 保留一位百分比小数：旧的 %.0f 会把不足 0.5% 的权威剩余体力显示为 0%，误导为侧翻收近阶段失效。
		PushStatus(3, FColor::White, FString::Printf(TEXT("猫体力 %.0f  鱼体力 %.1f%%  线耐久 %.0f  线长 %.0f  %s%s%s"),
			CatStamina, Snapshot.NormalizedFishStamina * 100.0, Snapshot.RodDurabilityRemaining, LineLength,
			Snapshot.bReeling ? TEXT("[拖]") : TEXT(""),
			Snapshot.bSlacking ? TEXT("[松线]") : TEXT(""),
			Snapshot.bPerfectHook ? TEXT(" ★完美中鱼") : TEXT("")));
		break;
	}
	case ECatFishingPhase::NearShore:
		// 不受 bFullDetail 影响：精简模式（cat.Fishing.Debug 2）的用途正是"只看抄网相关"，这个圈是核心信息。
		// 半径来自鱼定义、颜色来自权威判定函数，绿=现在按 F 抄得到，红=够不着。
		DrawScoopTargetCircle(Controller, Fish);
		PushStatus(2, FColor::Emerald, TEXT("鱼到近岸了！快抄！（按 F）"));
		break;
	case ECatFishingPhase::ExhaustedReel:
		DrawScoopTargetCircle(Controller, Fish);
		PushStatus(2, FColor::Emerald, Snapshot.bReeling
			? TEXT("鱼已力竭：正在收向竿尖水面投影（也可按 F 抄）")
			: TEXT("鱼已力竭：按住左键收近（也可按 F 抄）"));
		break;
	default:
		break;
	}
#endif
}

// 竿尖常驻标记：所有已部署鱼竿的竿尖画青色小球（优先蓝图 RodTipMarker 标记组件），无会话时也可对位。
void UCatFishingDebugSubsystem::DrawRodTips() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World) return;
	for (TActorIterator<ACatFishingRodActor> It(World); It; ++It)
	{
		const ACatFishingRodActor* Rod = *It;
		if (!IsValid(Rod)) continue;
		DrawDebugSphere(World, ResolveRodTipDrawLocation(*Rod), 8.0f, 8, FColor::Cyan, false, -1.0f, 0, 1.5f);
	}
#endif
}

// 抄网范围半圆（常驻，不依赖会话阶段）：
// 圆心 = 玩家【面向方向】与岸线的交点（沿视线 2D 方向步进采样，找地→水的跨越点）；
// 半圆直边（直径）垂直于视线，弧朝面向一侧展开；半径 = ScoopReachCentimeters 固定值，与离岸远近无关。
void UCatFishingDebugSubsystem::DrawScoopRange(APlayerController* Controller) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn || !World) return;
	const FVector PawnLocation = Pawn->GetActorLocation();
	double Reach = GetDefault<UCatFishingSettings>()->ScoopReachCentimeters;
	// 与权威判定同口径：射线长度取全局设置与已装备抄网 DA 的较小值。
	if (const ACatCharacter* Character = Cast<ACatCharacter>(Pawn))
	{
		if (const UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent())
		{
			const UCatEquipmentDefinition* ScoopDefinition = GetDefault<UCatEquipmentSettings>()
				->FindRuntimeDefinition(Equipment->GetSnapshot().ScoopNetDefinitionId);
			if (ScoopDefinition && ScoopDefinition->Kind == ECatEquipmentKind::ScoopNet
				&& ScoopDefinition->ScoopReachCentimeters > 0.0)
			{
				Reach = FMath::Min(Reach, ScoopDefinition->ScoopReachCentimeters);
			}
		}
	}
	if (Reach <= 0.0) return;
	// 面向方向取控制器视角的水平分量（与抛竿/打窝的瞄准语义一致）。
	const FVector Facing = FVector(Controller->GetControlRotation().Vector().X,
		Controller->GetControlRotation().Vector().Y, 0.0).GetSafeNormal();
	if (Facing.IsNearlyZero()) return;

	// 抄网射线：从抄手水平位置沿面向画一条长度 = Reach 的线段，抬高 4cm 避免和地面 z-fighting。
	// 画在脚下高度而不是视点高度，因为判定是纯水平投影的——线画在哪个高度都不影响结果，
	// 但画在脚下能让玩家直观看出"这是俯视平面上的一条线"。
	const FVector RayStart = PawnLocation + FVector(0, 0, 4);
	const FVector RayEnd = RayStart + Facing * Reach;
	DrawDebugLine(World, RayStart, RayEnd, FColor::Emerald, false, -1.0f, 0, 2.5f);
	DrawDebugSphere(World, RayEnd, 8.0f, 8, FColor::Emerald, false, -1.0f, 0, 2.0f);
#endif
}

// 鱼身上的可捞圆圈：半径来自鱼定义，与权威判定同一个数；命中时变色，直接告诉玩家"现在按 F 抄得到"。
void UCatFishingDebugSubsystem::DrawScoopTargetCircle(APlayerController* Controller,
	const ACatFishEncounterActor* Fish) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	if (!World || !Pawn || !Fish || !Settings) return;
	const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
	const UCatFishDefinition* FishDefinition = Catalog
		? Catalog->FindRuntimeDefinition(Fish->GetPresentationState().FishDefinitionId) : nullptr;
	const double Radius = FishDefinition ? FishDefinition->ScoopTargetRadiusCentimeters : 0.0;
	if (Radius <= 0.0) return;

	double Reach = Settings->ScoopReachCentimeters;
	if (const ACatCharacter* Character = Cast<ACatCharacter>(Pawn))
	{
		if (const UCatEquipmentComponent* Equipment = Character->GetEquipmentComponent())
		{
			const UCatEquipmentDefinition* ScoopDefinition = GetDefault<UCatEquipmentSettings>()
				->FindRuntimeDefinition(Equipment->GetSnapshot().ScoopNetDefinitionId);
			if (ScoopDefinition && ScoopDefinition->Kind == ECatEquipmentKind::ScoopNet
				&& ScoopDefinition->ScoopReachCentimeters > 0.0)
			{
				Reach = FMath::Min(Reach, ScoopDefinition->ScoopReachCentimeters);
			}
		}
	}
	const FVector Facing = FVector(Controller->GetControlRotation().Vector().X,
		Controller->GetControlRotation().Vector().Y, 0.0).GetSafeNormal();
	// 调 AimLibrary 里那个唯一的判定函数：画出来的"能不能抄到"就是服务器的结论，不存在两套口径。
	const bool bReachable = UCatFishingAimLibrary::DoesScoopRayReachFish(Pawn->GetActorLocation(), Facing,
		static_cast<float>(Reach), Fish->GetActorLocation(), static_cast<float>(Radius),
		static_cast<float>(Settings->MaximumScoopVerticalDeltaCentimeters));
	// 圆画在鱼的权威高度（水面），不用 GetVisualWorldLocation——判定用的是权威位置，debug 必须跟判定一致。
	DrawDebugCircle(World, Fish->GetActorLocation(), Radius, 32,
		bReachable ? FColor::Green : FColor::Red, false, -1.0f, 0, 2.5f,
		FVector(1, 0, 0), FVector(0, 1, 0), false);
#endif
}
