#include "UI/HUD/CatHUDModel.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Growth/CatGrowthComponent.h"
#include "UI/CatFishingViewBridge.h"

// 绑定流程：校验本地玩家、Controller、Character 和 ASC，随后订阅三项属性、Condition、Growth 和 Fishing 命令结果，最后发布首份 HUD 投影。
bool UCatHUDModel::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InCharacter || InController->GetPawn() != InCharacter)
	{
		return false;
	}
	UAbilitySystemComponent* AbilitySystem = InCharacter->GetAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return false;
	}
	FishingViewBridge = NewObject<UCatFishingViewBridge>(this);
	if (!FishingViewBridge)
	{
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	BoundPlayerController = InController;
	BoundAbilitySystem = AbilitySystem;
	BoundCondition = InCharacter->GetConditionComponent();
	BoundGrowth = InCharacter->GetGrowthComponent();
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		BoundFishingCommand = CatController->GetFishingCommandComponent();
	}
	PoisonChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute())
		.AddUObject(this, &ThisClass::HandleAttributeChanged);
	FishingStrengthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	FightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
	if (UCatConditionComponent* Condition = BoundCondition.Get())
	{
		ConditionChangedHandle = Condition->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleConditionChanged);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		GrowthChangedHandle = Growth->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleGrowthChanged);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.AddDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	FishingViewChangedHandle = FishingViewBridge->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleFishingViewStateChanged);
	RefreshFishingSessionBinding();
	Refresh();
	return true;
}

// 解绑流程：从原 ASC、Condition、Growth、Fishing 命令和 Bridge 移除订阅，再清弱引用、最近结果和投影，防止跨 Pawn 显示旧状态。
void UCatHUDModel::Unbind()
{
	if (UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute()).Remove(PoisonChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).Remove(FishingStrengthChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(FightStaminaChangedHandle);
	}
	if (UCatConditionComponent* Condition = BoundCondition.Get())
	{
		Condition->OnSnapshotChanged.Remove(ConditionChangedHandle);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		Growth->OnSnapshotChanged.Remove(GrowthChangedHandle);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.RemoveDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	if (FishingViewBridge)
	{
		FishingViewBridge->OnViewStateChanged.Remove(FishingViewChangedHandle);
		FishingViewBridge->UnbindSession();
	}
	PoisonChangedHandle.Reset();
	FishingStrengthChangedHandle.Reset();
	FightStaminaChangedHandle.Reset();
	ConditionChangedHandle.Reset();
	GrowthChangedHandle.Reset();
	FishingViewChangedHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundAbilitySystem.Reset();
	BoundCondition.Reset();
	BoundGrowth.Reset();
	BoundFishingCommand.Reset();
	FishingViewBridge = nullptr;
	LastFishingCommandResult = FCatFishingCommandResult();
	bHasFishingCommandResult = false;
	ViewState = FCatHUDViewState();
}

// 刷新流程：读取 ASC 三项数值、Condition、Growth 和 FishingBridge 当前投影，再生成 HUD 文本并广播完整状态。
void UCatHUDModel::Refresh()
{
	FCatHUDViewState NewState;
	if (const UAbilitySystemComponent* AbilitySystem = BoundAbilitySystem.Get())
	{
		NewState.Poison = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute());
		NewState.FishingStrength = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
		NewState.FightStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	}
	if (const UCatConditionComponent* Condition = BoundCondition.Get())
	{
		NewState.Condition = Condition->GetSnapshot();
	}
	if (const UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		NewState.Growth = Growth->GetSnapshot();
	}
	if (FishingViewBridge && FishingViewBridge->GetBoundSession())
	{
		NewState.Fishing = FishingViewBridge->GetViewState();
		NewState.bHasFishingSession = true;
	}
	NewState.LastFishingCommandResult = LastFishingCommandResult;
	NewState.bHasFishingCommandResult = bHasFishingCommandResult;
	NewState.CatStatusText = FText::FromString(FString::Printf(TEXT("猫状态：中毒 %.0f | 钓鱼力量 %.0f | 搏斗体力 %.0f | 成长总经验 %d，当前槽 %d，待选 %d"),
		NewState.Poison,
		NewState.FishingStrength,
		NewState.FightStamina,
		NewState.Growth.TotalExperience,
		NewState.Growth.ExperienceInCurrentSlot,
		NewState.Growth.PendingChoiceCount));
	NewState.FishingFeedbackText = NewState.bHasFishingSession
		? FText::FromString(TEXT("钓鱼反馈：正在钓鱼，等待会话更新"))
		: FText::FromString(TEXT("钓鱼反馈：当前没有进行中的钓鱼会话"));
	if (NewState.bHasFishingCommandResult)
	{
		NewState.FishingFeedbackText = FText::FromString(FString::Printf(TEXT("钓鱼反馈：最近命令 %s，版本 %lld"),
			*UEnum::GetValueAsString(NewState.LastFishingCommandResult.Error),
			NewState.LastFishingCommandResult.Revision));
	}
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// ViewState 读取流程：返回最近 HUD 投影；调用方不能通过它访问 ASC 或会话对象。
const FCatHUDViewState& UCatHUDModel::GetViewState() const
{
	return ViewState;
}

// 属性变化流程：事件只表达事实变更，Model 统一重读三项 HUD 数值。
void UCatHUDModel::HandleAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	Refresh();
}

// Condition 变化流程：重读完整 HUD 事实，避免增量顺序形成 UI 私有状态。
void UCatHUDModel::HandleConditionChanged()
{
	Refresh();
}

// Growth 变化流程：重读完整 HUD 事实，让经验槽、待选次数和身体状态保持同帧投影。
void UCatHUDModel::HandleGrowthChanged()
{
	Refresh();
}

// Fishing 投影变化流程：Bridge 已保存最新会话 DTO，HUD 只重建展示文本。
void UCatHUDModel::HandleFishingViewStateChanged(const FCatFishingViewState& InViewState)
{
	(void)InViewState;
	Refresh();
}

// Fishing 结果流程：缓存最近命令终态，重新定位可能新建的 FishingSession，然后刷新 HUD 反馈。
void UCatHUDModel::HandleFishingCommandResult(const FCatFishingCommandResult& Result)
{
	LastFishingCommandResult = Result;
	bHasFishingCommandResult = true;
	RefreshFishingSessionBinding();
	Refresh();
}

// Session 调和流程：按当前 PlayerState 找客户端可见 FishingSession；会话变化时让 Bridge 重新绑定，找不到就清空会话投影。
void UCatHUDModel::RefreshFishingSessionBinding()
{
	if (!FishingViewBridge)
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatFishingSession* Session = UCatFishingViewBridge::FindFishingSessionForPlayerState(
		Controller, PlayerState);
	if (FishingViewBridge->GetBoundSession() == Session)
	{
		return;
	}
	if (Session)
	{
		FishingViewBridge->BindSession(Session);
	}
	else
	{
		FishingViewBridge->UnbindSession();
	}
}
