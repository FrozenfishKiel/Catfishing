#include "Fishing/Debug/CatFishingDebugSubsystem.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "Character/CatCharacter.h"
#include "CanvasItem.h"
#include "Data/CatFishPersonalityDefinition.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
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
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Items/CatWorldItemSettings.h"
#include "Items/World/CatFishPickupActor.h"
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

	// 调试死鱼生成流程：只在 authority 为目标玩家前方生成可交互的世界鱼。
	// 后续仍必须由玩家按 E 叼起，再对具体地面鱼护按 E 入箱；调试命令不绕过正式交互与容器事务。
	static void GiveFishToPlayer(const TArray<FString>& Args, UWorld* World)
	{
		UCatFishDefinition* Definition = ResolveFishDefinition(Args);
		double WeightKilograms = Definition
			? (Definition->MinimumWeightKilograms + Definition->MaximumWeightKilograms) * 0.5 : 0.0;
		if (Args.IsValidIndex(1))
		{
			WeightKilograms = FCString::Atod(*Args[1]);
		}
		int32 PlayerIndex = 0;
		if (Args.IsValidIndex(2))
		{
			PlayerIndex = FMath::Max(0, FCString::Atoi(*Args[2]));
		}
		APlayerController* Controller = ResolvePlayerController(World, PlayerIndex);
		ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
		APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
		if (!World || !Definition || !Controller || !Controller->HasAuthority() || !Character || !PlayerState
			|| !PlayerState->GetUniqueId().IsValid() || !FMath::IsFinite(WeightKilograms)
			|| WeightKilograms < Definition->MinimumWeightKilograms
			|| WeightKilograms > Definition->MaximumWeightKilograms)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_debug_give_fish_rejected Reason=InvalidPayload World=%s Controller=%s FishDefinition=%s WeightKg=%.3f"),
				World ? *World->GetName() : TEXT("None"),
				*GetNameSafe(Controller),
				Definition ? *Definition->FishDefinitionId.ToString() : TEXT("None"), WeightKilograms);
			return;
		}

		const UCatFishPresentationDefinition* FishPresentation =
			Definition->LoadRuntimePresentationDefinition();
		const FVector SpawnLocation = Character->GetActorLocation()
			+ Character->GetActorForwardVector() * 150.0 + FVector(0.0, 0.0, 40.0);
		FRotator SpawnRotation = Character->GetActorRotation();
		SpawnRotation.Pitch = 0.0;
		SpawnRotation.Roll = FishPresentation ? FishPresentation->LandedActorRollDegrees : 90.0;
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACatFishPickupActor* Pickup = World->SpawnActor<ACatFishPickupActor>(
			ACatFishPickupActor::StaticClass(), SpawnLocation, SpawnRotation, SpawnParams);
		const FString StableNetId = PlayerState->GetUniqueId()->ToString();
		const TArray<FString> Participants{ StableNetId };
		const double VisualScale = FishPresentation
			? FishPresentation->ComputeUniformVisualScale(WeightKilograms) : 1.0;
		if (!Pickup || !Pickup->InitializeFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), Definition,
			WeightKilograms, VisualScale, TEXT("DebugSpawn"), Participants))
		{
			if (Pickup)
			{
				Pickup->Destroy();
			}
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_debug_give_fish_rejected Reason=SpawnFailed FishDefinition=%s PlayerIndex=%d"),
				*Definition->FishDefinitionId.ToString(), PlayerIndex);
			return;
		}
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_debug_dead_fish_spawned Pickup=%s FishDefinition=%s WeightKg=%.3f PlayerIndex=%d"),
			*GetNameSafe(Pickup), *Definition->FishDefinitionId.ToString(), WeightKilograms, PlayerIndex);
	}

	/** 非 Shipping 构建里的死鱼生成入口；只生成世界 Actor，不直接改背包或鱼护。 */
	static FAutoConsoleCommandWithWorldAndArgs CmdGiveFish(
		TEXT("cat.Fishing.Debug.GiveFish"),
		TEXT("在玩家前方生成可按 E 叼起的死鱼。参数：FishDefinitionId WeightKg PlayerIndex。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveFishToPlayer),
		ECVF_Cheat);
}
#endif

#if ENABLE_DRAW_DEBUG
static TAutoConsoleVariable<int32> CVarCatFishingDebug(
	TEXT("cat.Fishing.Debug"), 0,
	TEXT("钓鱼世界调试可视化：0=全部关闭（默认）；1=全量（水域边界/瞄准落点/窝点/竿尖球/钩鱼球/抄网射线与鱼圈/鱼线/阶段提示）；")
	TEXT("2=精简（只保留抄网射线、鱼身上的可捞圆圈、鱼线和阶段提示，关闭其余调试球/圈/线）。")
	TEXT("抄网圆圈绿色=当前按 F 抄得到，红色=够不着；颜色直接来自服务器同一个判定函数。"),
	ECVF_Default);

// 三方资源/力量只在排查时需要观察，不应作为默认主界面内容常驻。
// 因此它拥有独立 CVar，默认关闭；`cat.Fishing.Debug 0` 不会隐藏本面板，反之开启本面板也不改变世界调试模式。
static TAutoConsoleVariable<int32> CVarCatFishingStats(
	TEXT("cat.Fishing.Stats"), 0,
	TEXT("屏幕右上角钓鱼数值调试：1=显示（当前鱼种、鱼体力/力量、竿或鱼线耐久/力量、猫体力/力量）；0=关闭（默认）。")
	TEXT("本开关与 cat.Fishing.Debug 相互独立。"),
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

void UCatFishingDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if ENABLE_DRAW_DEBUG
	FishingStatsDrawHandle = UDebugDrawService::Register(TEXT("Game"),
		FDebugDrawDelegate::CreateUObject(this, &ThisClass::DrawFishingStats));
#endif
}

void UCatFishingDebugSubsystem::Deinitialize()
{
#if ENABLE_DRAW_DEBUG
	if (FishingStatsDrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(FishingStatsDrawHandle);
		FishingStatsDrawHandle.Reset();
	}
#endif
	Super::Deinitialize();
}

FString UCatFishingDebugSubsystem::FormatFishTypeLine(const FName FishDefinitionId)
{
	return FishDefinitionId.IsNone()
		? FString(TEXT("FISH TYPE  --"))
		: FString::Printf(TEXT("FISH TYPE  %s"), *FishDefinitionId.ToString());
}

// 右上角数值面板：每一项都读取与权威玩法相同的公开事实/定义，不从表现位置反推资源。
// 鱼和本场鱼线读取 Session 复制快照；猫读取本地 Character ASC；力量与上限按稳定 DefinitionId 查正式目录。
void UCatFishingDebugSubsystem::DrawFishingStats(UCanvas* Canvas, APlayerController* Controller)
{
#if ENABLE_DRAW_DEBUG
	if (CVarCatFishingStats.GetValueOnGameThread() == 0 || !Canvas || !Controller
		|| !Controller->IsLocalController() || Controller->GetWorld() != GetWorld() || !GEngine)
	{
		return;
	}

	APawn* Pawn = Controller->GetPawn();
	const ACatCharacter* Character = Cast<ACatCharacter>(Pawn);
	const ACatFishingSession* Session = UCatFishingViewBridge::FindFishingSessionForPlayerState(
		GetWorld(), Controller->PlayerState);
	const FCatFishingSessionSnapshot* SessionSnapshot = Session ? &Session->GetSnapshot() : nullptr;
	FString FishTypeLine = FormatFishTypeLine(
		SessionSnapshot ? SessionSnapshot->FishDefinitionId : NAME_None);

	FString FishLine = TEXT("FISH  Stamina --  Strength --");
	if (SessionSnapshot && !SessionSnapshot->FishDefinitionId.IsNone())
	{
		const UCatFishDefinition* FishDefinition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(
			SessionSnapshot->FishDefinitionId);
		if (FishDefinition)
		{
			double StaminaScale = 1.0;
			if (SessionSnapshot->bPerfectHook)
			{
				const UCatFishingSettings* FishingSettings = GetDefault<UCatFishingSettings>();
				const UCatBitePersonalityDefinition* Bite = FishingSettings
					? FishingSettings->FindBitePersonality(FishDefinition->BitePersonalityId) : nullptr;
				if (Bite)
				{
					StaminaScale = Bite->PerfectFishStaminaMultiplier;
				}
			}
			const double MaximumStamina = FishDefinition->FishFightStamina * StaminaScale;
			const double StaminaPercent = MaximumStamina > 0.0
				? FMath::Clamp(SessionSnapshot->FishFightStaminaRemaining / MaximumStamina * 100.0, 0.0, 100.0) : 0.0;
			FishLine = FString::Printf(TEXT("FISH  Stamina %.1f / %.1f (%.1f%%)  Strength %.1f"),
				SessionSnapshot->FishFightStaminaRemaining, MaximumStamina, StaminaPercent,
				SessionSnapshot->FishStrength);
		}
	}

	const UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	const FCatEquipmentLoadoutSnapshot* Loadout = Equipment ? &Equipment->GetSnapshot() : nullptr;
	FName RodDefinitionId = Loadout ? Loadout->RodDefinitionId : NAME_None;
	if (SessionSnapshot && SessionSnapshot->RodActor)
	{
		RodDefinitionId = SessionSnapshot->RodActor->GetPresentationState().RodDefinitionId;
	}
	FString RodLine = TEXT("LINE  SessionDurability --  ROD Strength --");
	if (const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(
		RodDefinitionId))
	{
		double CurrentDurability = RodDefinition->MaximumRodDurability;
		const bool bUsesSessionDurability = SessionSnapshot
			&& (SessionSnapshot->Phase == ECatFishingPhase::HookedFight
				|| SessionSnapshot->Phase == ECatFishingPhase::NearShore
				|| SessionSnapshot->Phase == ECatFishingPhase::ExhaustedReel);
		if (bUsesSessionDurability)
		{
			CurrentDurability = SessionSnapshot->RodDurabilityRemaining;
		}
		else if (Loadout && Loadout->RodDefinitionId == RodDefinitionId)
		{
			CurrentDurability = Loadout->RodDurability;
		}
		RodLine = bUsesSessionDurability
			? FString::Printf(TEXT("LINE  SessionDurability %.1f / %.1f  ROD Strength %.1f"),
				CurrentDurability, RodDefinition->MaximumRodDurability, RodDefinition->FishingStrength)
			: FString::Printf(TEXT("ROD   EquipmentDurability %.1f / %.1f  Strength %.1f  LINE --"),
				CurrentDurability, RodDefinition->MaximumRodDurability, RodDefinition->FishingStrength);
	}

	FString CatLine = TEXT("CAT   Stamina --  Strength --");
	if (const IAbilitySystemInterface* AbilityInterface = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (const UAbilitySystemComponent* AbilitySystem = AbilityInterface->GetAbilitySystemComponent())
		{
			const double CurrentStamina = AbilitySystem->GetNumericAttribute(
				UCatSurvivalAttributeSet::GetFightStaminaAttribute());
			const double Strength = AbilitySystem->GetNumericAttribute(
				UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
			float MaximumStamina = 0.0f;
			if (Character)
			{
				GetDefault<UCatAbilitySettings>()->TryGetFightStaminaBaselineForCharacter(
					Character->GetCatDefinitionId(), MaximumStamina);
			}
			CatLine = MaximumStamina > 0.0f
				? FString::Printf(TEXT("CAT   Stamina %.1f / %.1f  Strength %.1f"),
					CurrentStamina, MaximumStamina, Strength)
				: FString::Printf(TEXT("CAT   Stamina %.1f  Strength %.1f"), CurrentStamina, Strength);
		}
	}

	struct FPanelLine
	{
		FString Text;
		FLinearColor Color;
	};
	const TArray<FPanelLine> Lines
	{
		{ TEXT("Fishing Stats  [cat.Fishing.Stats 0 = off]"), FLinearColor::Yellow },
		{ MoveTemp(FishTypeLine), FLinearColor(0.55f, 0.9f, 1.0f) },
		{ MoveTemp(FishLine), FLinearColor(0.25f, 0.85f, 1.0f) },
		{ MoveTemp(RodLine), FLinearColor(1.0f, 0.65f, 0.2f) },
		{ MoveTemp(CatLine), FLinearColor(0.35f, 1.0f, 0.35f) }
	};
	UFont* Font = GEngine->GetSmallFont();
	if (!Font)
	{
		return;
	}
	const FVector2D TextScale(1.05f, 1.05f);
	float MaximumTextWidth = 0.0f;
	float MaximumTextHeight = 0.0f;
	for (const FPanelLine& Line : Lines)
	{
		const FVector2D TextSize = Canvas->K2_TextSize(Font, Line.Text, TextScale);
		MaximumTextWidth = FMath::Max(MaximumTextWidth, TextSize.X);
		MaximumTextHeight = FMath::Max(MaximumTextHeight, TextSize.Y);
	}

	constexpr float ViewMargin = 24.0f;
	constexpr float PanelPadding = 10.0f;
	constexpr float LineGap = 3.0f;
	const float ViewWidth = Canvas->ClipX > 0.0f ? Canvas->ClipX : static_cast<float>(Canvas->SizeX);
	const float LineAdvance = MaximumTextHeight + LineGap;
	const FVector2D PanelSize(MaximumTextWidth + PanelPadding * 2.0f,
		LineAdvance * Lines.Num() - LineGap + PanelPadding * 2.0f);
	const FVector2D PanelPosition(FMath::Max(0.0f, ViewWidth - ViewMargin - PanelSize.X), ViewMargin);
	FCanvasTileItem Background(PanelPosition, PanelSize, FLinearColor(0.0f, 0.0f, 0.0f, 0.72f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	const float PanelRight = PanelPosition.X + PanelSize.X - PanelPadding;
	float TextY = PanelPosition.Y + PanelPadding;
	for (const FPanelLine& Line : Lines)
	{
		const FVector2D TextSize = Canvas->K2_TextSize(Font, Line.Text, TextScale);
		Canvas->K2_DrawText(Font, Line.Text, FVector2D(PanelRight - TextSize.X, TextY), TextScale,
			Line.Color, 0.0f, FLinearColor::Black, FVector2D(1.0f, 1.0f), false, false, true,
			FLinearColor::Black);
		TextY += LineAdvance;
	}
#else
	(void)Canvas;
	(void)Controller;
#endif
}

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
	const bool bFullDetail = DebugMode != 2; // 2=精简：只留抄网射线+鱼圈+鱼线+阶段提示。
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
	FVector ViewOrigin, ViewDirection;
	if (UCatFishingAimLibrary::TryGetLocalCastViewRay(Controller, ViewOrigin, ViewDirection)
		&& UCatFishingAimLibrary::ResolveCastAimPoint(World, ViewOrigin,
			ViewDirection.Rotation(), AimRegion, AimLanding))
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

	// 装备/库存常驻显示：世界 Debug 只保留窝料操作提示。竿/线耐久已经迁到独立的右上角 Stats 面板，
	// 避免 cat.Fishing.Debug 打开后出现两套不同位置、不同精度的数值入口。
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
			PushStatus(1, FColor::White, FString::Printf(TEXT("窝料 x%d"), ChumCount));
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
		PushStatus(3, FColor::White, FString::Printf(TEXT("线长 %.0f  %s%s%s"), LineLength,
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
	const ACatCharacter* Character = Cast<ACatCharacter>(Pawn);
	double Reach = 0.0;
	// 与权威裁决共用同一解析函数：没有服务器认可的已装备抄网时不显示可用范围。
	if (!UCatFishingAimLibrary::TryResolveScoopReach(
		Character ? Character->GetEquipmentComponent() : nullptr, Reach)) return;
	// 抄网是角色身体动作：范围必须跟 Character 面朝方向一致，镜头自由转动不改变提示方向。
	const FVector Facing = UCatFishingAimLibrary::ResolveScoopFacingHorizontal(Character);
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

	const ACatCharacter* Character = Cast<ACatCharacter>(Pawn);
	double Reach = 0.0;
	if (!UCatFishingAimLibrary::TryResolveScoopReach(
		Character ? Character->GetEquipmentComponent() : nullptr, Reach)) return;
	const FVector Facing = UCatFishingAimLibrary::ResolveScoopFacingHorizontal(Character);
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
