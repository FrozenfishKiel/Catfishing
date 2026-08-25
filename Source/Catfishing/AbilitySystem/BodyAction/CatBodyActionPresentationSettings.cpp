#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"

#include "Animation/AnimMontage.h"

namespace
{
	/** 向默认配置表追加一个动作；默认只建立可取消前摇，不指定 Montage，避免把临时资产写成正式表现。 */
	void AddDefaultBodyActionPresentationConfig(TArray<FCatBodyActionPresentationConfig>& Configs,
		const ECatBodyActionAbilityCommand Command, const float LeadInSeconds)
	{
		FCatBodyActionPresentationConfig Config;
		Config.Command = Command;
		Config.LeadInSeconds = LeadInSeconds;
		Config.PresentationEventTag = UCatBodyActionPayload::GetEventTagForCommand(Command);
		Configs.Add(Config);
	}
}

UCatBodyActionPresentationSettings::UCatBodyActionPresentationSettings()
{
	// 默认表构造流程：一次列出正式 BodyAction 命令，给每个动作同一条可取消前摇和稳定表现标签；
	// 美术资源不在这里猜；后续项目配置可以追加同 Command 行覆盖默认值，不需要再改 RPC、Ability 或领域服务。
	ActionPresentationConfigs.Reserve(14);
	for (const ECatBodyActionAbilityCommand Command : {
		ECatBodyActionAbilityCommand::RequestSacrifice,
		ECatBodyActionAbilityCommand::CampRest,
		ECatBodyActionAbilityCommand::CampfirePlayback,
		ECatBodyActionAbilityCommand::TransferFishToTank,
		ECatBodyActionAbilityCommand::RescueCharacterToCamp,
		ECatBodyActionAbilityCommand::RepairRodAtCamp,
		ECatBodyActionAbilityCommand::UseHerbOnCharacter,
		ECatBodyActionAbilityCommand::ConsumeFish,
		ECatBodyActionAbilityCommand::BeginTheft,
		ECatBodyActionAbilityCommand::CatchTheft,
		ECatBodyActionAbilityCommand::RequestManualHelp,
		ECatBodyActionAbilityCommand::RequestMischief,
		ECatBodyActionAbilityCommand::PlaceProtectionSign,
		ECatBodyActionAbilityCommand::CompleteShakeDry})
	{
		AddDefaultBodyActionPresentationConfig(ActionPresentationConfigs, Command, DefaultLeadInSeconds);
	}
}

const FCatBodyActionPresentationConfig* UCatBodyActionPresentationSettings::FindPresentationConfig(
	const ECatBodyActionAbilityCommand Command) const
{
	// 查找流程：只接受明确动作；重复配置从后往前找，让项目配置追加的同 Command 行能覆盖构造期默认行。
	// 这样默认表负责“全部动作都有兜底”，正式 Montage/前摇接入时不会被早期默认项吞掉。
	if (Command == ECatBodyActionAbilityCommand::Unknown)
	{
		return nullptr;
	}
	for (int32 Index = ActionPresentationConfigs.Num() - 1; Index >= 0; --Index)
	{
		if (ActionPresentationConfigs[Index].Command == Command)
		{
			return &ActionPresentationConfigs[Index];
		}
	}
	return nullptr;
}

float UCatBodyActionPresentationSettings::GetLeadInSecondsForCommand(
	const ECatBodyActionAbilityCommand Command) const
{
	// 前摇解析流程：动作级配置优先，缺失时回退默认值；最终统一压到非负，保证 WaitDelay 不收到非法时间。
	const FCatBodyActionPresentationConfig* Config = FindPresentationConfig(Command);
	return FMath::Max(0.0f, Config ? Config->LeadInSeconds : DefaultLeadInSeconds);
}

FGameplayTag UCatBodyActionPresentationSettings::GetPresentationEventTagForCommand(
	const ECatBodyActionAbilityCommand Command) const
{
	// 表现标签解析流程：优先读取项目配置；没有专用表现标签时回退 AbilityEvent，蓝图仍可稳定按动作分派。
	if (const FCatBodyActionPresentationConfig* Config = FindPresentationConfig(Command);
		Config && Config->PresentationEventTag.IsValid())
	{
		return Config->PresentationEventTag;
	}
	return UCatBodyActionPayload::GetEventTagForCommand(Command);
}

UAnimMontage* UCatBodyActionPresentationSettings::LoadMontageForCommand(
	const ECatBodyActionAbilityCommand Command) const
{
	// Montage 读取流程：只读取显式配置的资产；留空表示当前动作没有正式 Montage，不做临时资产回退。
	const FCatBodyActionPresentationConfig* Config = FindPresentationConfig(Command);
	return Config ? Config->Montage.LoadSynchronous() : nullptr;
}
