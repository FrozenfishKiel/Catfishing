#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishDefinition.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentWetAuthorityTest,
	"Catfishing.Unit.Condition.Component.AuthorityWetChangesRevisionAndRepeatDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentCarryToCampTest,
	"Catfishing.Unit.Condition.Component.CarryToCampRequiresValidRescueFactAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentConsumeFishTest,
	"Catfishing.Unit.Condition.Component.ConsumeCommittedFishSettlesPoisonOncePerRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentRecoveryTest,
	"Catfishing.Unit.Condition.Component.RecoveryPathsClearPoisonAndLiftDowned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatConditionComponentTest
{
	// 组件装配流程：用普通 authority Actor 承载 Condition 组件；这些测试只覆盖不依赖 ASC/Character 的公开合同。
	static UCatConditionComponent* AddConditionComponent(AActor* Host)
	{
		UCatConditionComponent* Component = Host ? NewObject<UCatConditionComponent>(Host) : nullptr;
		if (Host && Component)
		{
			Host->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		return Component;
	}

	/**
	 * 身体命令准入条件的临时改写守卫。吃鱼、恢复和搬运都只从 GetDefault 读 gate，用例必须自己把准入摆正。
	 * 之所以在用例里写而不是直接依赖项目 ini：那份 ini 是产品配置，策划随时可能调 gate 或阈值，用例的成立性不能挂在一份会变的外部数值上。
	 * 之所以在析构时逐项写回：改的是进程内唯一的 CDO，同一批自动化里其他用例也会临时改同一份对象，不还原就会变成谁先跑谁说了算的顺序依赖。
	 */
	struct FScopedConditionGates
	{
		/** 被临时改写的 Ability 默认配置；它决定 Character-owned ASC 是否被当成正式可用的 runtime。 */
		UCatAbilitySettings* AbilitySettings = GetMutableDefault<UCatAbilitySettings>();

		/** 被临时改写的 Condition 默认配置；它提供倒地裁决唯一需要的中毒阈值。 */
		UCatConditionSettings* ConditionSettings = GetMutableDefault<UCatConditionSettings>();

		/** 进入用例前的 Character ASC runtime 开关原值；析构时写回。 */
		bool bOldAbilityRuntime = false;

		/** 进入用例前的 GameplayEffect 复制策略原值；析构时写回。 */
		ECatAbilityReplicationPolicy OldReplication = ECatAbilityReplicationPolicy::Undecided;

		/** 进入用例前的 Condition runtime 开关原值；析构时写回。 */
		bool bOldConditionRuntime = false;

		/** 进入用例前的中毒倒地阈值原值；析构时写回。 */
		double OldPoisonDownedThreshold = 0.0;

		// 准入流程：先抄下四个默认值，再写成用例需要的可运行组合。阈值取 100：吃鱼用例投进去的中毒量远低于它，
		// 恢复用例投进去的远高于它，同一份阈值同时支撑"必定不倒地"和"必定倒地"两种场景。
		// 三条恢复路径没有自己的数值配置，它们的准入只看身份/营地事实和这份倒地阈值。
		FScopedConditionGates()
		{
			if (AbilitySettings)
			{
				bOldAbilityRuntime = AbilitySettings->bEnableCharacterAbilityRuntime;
				OldReplication = AbilitySettings->ReplicationPolicy;
				AbilitySettings->bEnableCharacterAbilityRuntime = true;
				AbilitySettings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
			}
			if (ConditionSettings)
			{
				bOldConditionRuntime = ConditionSettings->bEnableConditionRuntime;
				OldPoisonDownedThreshold = ConditionSettings->PoisonDownedThreshold;
				ConditionSettings->bEnableConditionRuntime = true;
				ConditionSettings->PoisonDownedThreshold = 100.0;
			}
		}

		// 还原流程：把四个默认值原样写回两个 CDO；不调用 SaveConfig，测试改动不落到项目 ini。
		~FScopedConditionGates()
		{
			if (AbilitySettings)
			{
				AbilitySettings->bEnableCharacterAbilityRuntime = bOldAbilityRuntime;
				AbilitySettings->ReplicationPolicy = OldReplication;
			}
			if (ConditionSettings)
			{
				ConditionSettings->bEnableConditionRuntime = bOldConditionRuntime;
				ConditionSettings->PoisonDownedThreshold = OldPoisonDownedThreshold;
			}
		}
	};

	// 可食用鱼构造流程：先把 IsRuntimeDefinitionReady 要求的整套捕获字段填满，再按食用安全性写入 Poison 数值，
	// 使 HasRuntimeConsumptionEffect 对 Safe 走「必须无毒」分支、对 Toxic 走「必须正毒」分支都能通过；
	// 对象 outer 挂在宿主 Character 上，随测试 World 一起回收，不需要额外挂根。
	static UCatFishDefinition* MakeConsumableFish(AActor* Host, const FName DefinitionId,
		const ECatFishFoodSafety FoodSafety, const double PoisonIncrease)
	{
		UCatFishDefinition* Definition = Host ? NewObject<UCatFishDefinition>(Host) : nullptr;
		if (!Definition)
		{
			return nullptr;
		}
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = DefinitionId;
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 1;
		Definition->RegionIds = {TEXT("LakeA")};
		Definition->ChumAffinities = {ECatChumAffinity::Fishy};
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 2.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->FoodSafety = FoodSafety;
		Definition->PoisonIncrease = PoisonIncrease;
		return Definition;
	}
}

// 测试流程：在 authority Actor 上切换 Wet，读取公开 Snapshot 的 Revision/Wet；重复写相同值必须保持 Revision 不变。
bool FCatConditionComponentWetAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition Wet 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatConditionComponent* Component = CatConditionComponentTest::AddConditionComponent(Host);
		TestNotNull(TEXT("Wet 测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("Wet 测试 Condition 组件可创建"), Component);
		if (Component)
		{
			TestEqual(TEXT("初始 Condition Revision 为 0"), Component->GetSnapshot().Revision, int64{0});
			TestFalse(TEXT("初始 Wet 为 false"), Component->GetSnapshot().bWet);
			Component->SetWetFromAuthority(true);
			TestTrue(TEXT("authority 设置 Wet 后为 true"), Component->GetSnapshot().bWet);
			TestEqual(TEXT("authority 设置 Wet 推进 Revision"), Component->GetSnapshot().Revision, int64{1});
			Component->SetWetFromAuthority(true);
			TestEqual(TEXT("重复设置相同 Wet 不推进 Revision"), Component->GetSnapshot().Revision, int64{1});
			Component->SetWetFromAuthority(false);
			TestFalse(TEXT("authority 清 Wet 后为 false"), Component->GetSnapshot().bWet);
			TestEqual(TEXT("Wet 真实变化再次推进 Revision"), Component->GetSnapshot().Revision, int64{2});
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：先锁定搬运失败和成功的首个救援事实，再验证同 RequestId 改变营地点事实会被拒绝；原始事实仍可稳定重放。
// 宿主用真实 Character 而不是裸 Actor：搬运提交现在要清 Poison 并重新裁决倒地，缺 ASC 或缺倒地阈值时它会 fail-closed 成
// DependencyUnavailable，裸 Actor 根本走不到本用例要锁的重放语义。
bool FCatConditionComponentCarryToCampTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const CatConditionComponentTest::FScopedConditionGates Gates;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition 搬运测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
		AController* Helper = World ? World->SpawnActor<APlayerController>() : nullptr;
		UCatConditionComponent* Component = Character ? Character->GetConditionComponent() : nullptr;
		TestNotNull(TEXT("搬运测试宿主 Character 可创建"), Character);
		TestNotNull(TEXT("搬运测试 Controller 可创建"), Helper);
		TestNotNull(TEXT("搬运测试 Condition 组件可创建"), Component);
		if (Component)
		{
			const FGuid InvalidRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult Invalid = Component->CompleteCarryToCamp(Helper, InvalidRequestId, false);
			TestFalse(TEXT("未到营地救援点时搬运不提交"), Invalid.bCommitted);
			TestEqual(TEXT("未到营地救援点返回 InvalidPayload"), Invalid.Error, ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("失败搬运不推进 Revision"), Component->GetSnapshot().Revision, int64{0});

			const FCatDomainCommandResult InvalidDrift = Component->CompleteCarryToCamp(Helper, InvalidRequestId, true);
			TestFalse(TEXT("同 RequestId 更换救援点事实仍不提交"), InvalidDrift.bCommitted);
			TestEqual(TEXT("失败搬运的载荷漂移返回 InvalidPayload"), InvalidDrift.Error, ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("失败载荷漂移不推进 Revision"), Component->GetSnapshot().Revision, int64{0});

			const FGuid RequestId = FGuid::NewGuid();
			const FCatDomainCommandResult First = Component->CompleteCarryToCamp(Helper, RequestId, true);
			TestTrue(TEXT("有效搬运提交成功"), First.bCommitted);
			TestEqual(TEXT("有效搬运无错误"), First.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("有效搬运推进 Revision"), First.Revision, int64{1});
			TestEqual(TEXT("有效搬运写入恢复方式"), Component->GetSnapshot().RecoveryMode, ECatRecoveryMode::CarriedToCamp);
			TestFalse(TEXT("有效搬运后不处于倒地"), Component->GetSnapshot().bDowned);

			const FCatDomainCommandResult Drift = Component->CompleteCarryToCamp(Helper, RequestId, false);
			TestFalse(TEXT("成功搬运后同 RequestId 更换救援点事实不提交"), Drift.bCommitted);
			TestEqual(TEXT("成功搬运后的载荷漂移返回 InvalidPayload"), Drift.Error, ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("成功载荷漂移不推进 Revision"), Component->GetSnapshot().Revision, int64{1});

			const FCatDomainCommandResult Replay = Component->CompleteCarryToCamp(Helper, RequestId, true);
			TestFalse(TEXT("搬运重放不再次提交"), Replay.bCommitted);
			TestEqual(TEXT("搬运重放返回 AlreadyResolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("搬运重放不推进 Revision"), Replay.Revision, First.Revision);
			TestEqual(TEXT("搬运重放后 Snapshot Revision 不变"), Component->GetSnapshot().Revision, int64{1});
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}


// 测试流程：在真实 Character 上走完整条吃鱼结算路径，锁住删除饥饿系统后 ConsumeCommittedFish 剩下的四条不变量。
// 先吃一条 Toxic 鱼，确认 Poison 按 PoisonIncrease 增加、Revision 推进一格；再用同一 RequestId 重放同一条鱼，
// 确认返回 AlreadyResolved 且 Poison 与 Revision 都不动，说明终态缓存挡住了二次结算；接着用同一 RequestId 换成另一条鱼，
// 确认载荷签名的判别力还在（返回 InvalidPayload），即从签名里去掉 HungerRelief 一项也没有削弱这道防重放分辨能力；
// 最后吃一条 Safe 鱼，确认它照样 bCommitted=true 并推进 Revision，但一个 Attribute 都不改——
// 实物鱼在 Items 侧已被不可逆移除，这一步表达的是这次消费已结算完毕，而不是改到了什么数值。
bool FCatConditionComponentConsumeFishTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const CatConditionComponentTest::FScopedConditionGates Gates;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建吃鱼结算测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
		TestNotNull(TEXT("吃鱼测试可生成项目 Character"), Character);
		UCatConditionComponent* Component = Character ? Character->GetConditionComponent() : nullptr;
		UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
		TestNotNull(TEXT("Character 暴露 Condition 组件"), Component);
		TestNotNull(TEXT("Character 暴露 ASC"), AbilitySystem);
		UCatFishDefinition* ToxicFish = CatConditionComponentTest::MakeConsumableFish(
			Character, TEXT("ConsumeToxicFish"), ECatFishFoodSafety::Toxic, 5.0);
		UCatFishDefinition* SafeFish = CatConditionComponentTest::MakeConsumableFish(
			Character, TEXT("ConsumeSafeFish"), ECatFishFoodSafety::Safe, 0.0);
		TestNotNull(TEXT("可构造 Toxic 测试鱼定义"), ToxicFish);
		TestNotNull(TEXT("可构造 Safe 测试鱼定义"), SafeFish);
		if (Component && AbilitySystem && ToxicFish && SafeFish)
		{
			AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 0.0f);
			const int64 BaseRevision = Component->GetSnapshot().Revision;

			const FGuid ToxicRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult ToxicResult = Component->ConsumeCommittedFish(ToxicRequestId, ToxicFish);
			TestTrue(TEXT("Toxic 鱼消费提交成功"), ToxicResult.bCommitted);
			TestEqual(TEXT("Toxic 鱼消费无错误"), ToxicResult.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("Toxic 鱼按 PoisonIncrease 增加 Poison"),
				AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 5.0f);
			TestEqual(TEXT("Toxic 鱼消费推进一格 Revision"), Component->GetSnapshot().Revision, BaseRevision + 1);

			const FCatDomainCommandResult ToxicReplay = Component->ConsumeCommittedFish(ToxicRequestId, ToxicFish);
			TestFalse(TEXT("同鱼同 RequestId 重放不再次提交"), ToxicReplay.bCommitted);
			TestEqual(TEXT("同鱼同 RequestId 重放返回 AlreadyResolved"), ToxicReplay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重放返回首次的同一终态 Revision"), ToxicReplay.Revision, ToxicResult.Revision);
			TestEqual(TEXT("重放不二次结算 Poison"),
				AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 5.0f);
			TestEqual(TEXT("重放不推进 Revision"), Component->GetSnapshot().Revision, BaseRevision + 1);

			const FCatDomainCommandResult PayloadDrift = Component->ConsumeCommittedFish(ToxicRequestId, SafeFish);
			TestFalse(TEXT("同 RequestId 换另一条鱼不提交"), PayloadDrift.bCommitted);
			TestEqual(TEXT("同 RequestId 换另一条鱼返回 InvalidPayload"), PayloadDrift.Error, ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("换鱼被拒后 Poison 不变"),
				AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 5.0f);
			TestEqual(TEXT("换鱼被拒后 Revision 不变"), Component->GetSnapshot().Revision, BaseRevision + 1);

			const FGuid SafeRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult SafeResult = Component->ConsumeCommittedFish(SafeRequestId, SafeFish);
			TestTrue(TEXT("Safe 鱼消费同样提交成功"), SafeResult.bCommitted);
			TestEqual(TEXT("Safe 鱼消费无错误"), SafeResult.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("Safe 鱼不改 Poison"),
				AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 5.0f);
			TestEqual(TEXT("Safe 鱼消费仍推进 Revision"), Component->GetSnapshot().Revision, BaseRevision + 2);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：野外自救、营地休息、伙伴搬运三条恢复路径各走一遍"吃毒鱼倒地 → 恢复 → 再触发一次倒地裁决"的完整回合。
// 锁住的不变量是本轮修复的核心：恢复必须真的把 Poison 清掉，而不只是写一条 RecoveryMode 记录。倒地由 Poison >= 阈值 推导，
// 每次裁决都从当前毒值重算，所以毒值只要还在阈值以上，恢复完的下一次裁决就会立刻把人判回倒地，
// 飞书猫咪与状态册 §3.1.5「单人可爬回营地休息自愈，保证不卡死」当场失效。
// 倒地不是直接写 Attribute 造出来的，而是吃一条毒值高于阈值的 Toxic 鱼推起来的，走的就是生产里唯一的中毒来源。
// 恢复后的二次裁决用"再吃一条 Safe 鱼"触发：Safe 鱼不改任何 Attribute，但会走 EvaluateDownedFromAttributes，
// 这是从公开接口重新裁决倒地最直接的方式，比断言一个私有函数更贴近真实调用链。
bool FCatConditionComponentRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const CatConditionComponentTest::FScopedConditionGates Gates;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition 恢复测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
		APlayerController* Owner = World ? World->SpawnActor<APlayerController>() : nullptr;
		APlayerController* Helper = World ? World->SpawnActor<APlayerController>() : nullptr;
		TestNotNull(TEXT("恢复测试可生成项目 Character"), Character);
		TestNotNull(TEXT("恢复测试可生成本人 Controller"), Owner);
		TestNotNull(TEXT("恢复测试可生成救援者 Controller"), Helper);
		UCatConditionComponent* Component = Character ? Character->GetConditionComponent() : nullptr;
		UAbilitySystemComponent* AbilitySystem = Character ? Character->GetAbilitySystemComponent() : nullptr;
		TestNotNull(TEXT("恢复测试 Character 暴露 Condition 组件"), Component);
		TestNotNull(TEXT("恢复测试 Character 暴露 ASC"), AbilitySystem);
		if (Character && Owner && Helper && Component && AbilitySystem)
		{
			// 占有流程：野外自救与营地休息都要求请求者正拥有这具身体，所以先建立真实占有关系，不用传空 Controller 绕过身份校验。
			Owner->Possess(Character);
			TestTrue(TEXT("本人 Controller 已占有恢复测试 Character"), Character->GetController() == Owner);

			// 倒地制造流程：每条路径开始前吃一条毒值 150 的 Toxic 鱼，越过夹具设定的 100 倒地阈值；鱼定义 ID 各不相同，
			// 避免同一条鱼的载荷签名在终态缓存里互相干扰。
			const auto DriveIntoDowned = [&](const TCHAR* PathName, const FName FishDefinitionId)
			{
				UCatFishDefinition* ToxicFish = CatConditionComponentTest::MakeConsumableFish(
					Character, FishDefinitionId, ECatFishFoodSafety::Toxic, 150.0);
				Component->ConsumeCommittedFish(FGuid::NewGuid(), ToxicFish);
				TestEqual(FString::Printf(TEXT("%s 前毒鱼把 Poison 推到 150"), PathName),
					AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 150.0f);
				TestTrue(FString::Printf(TEXT("%s 前中毒者已倒地"), PathName), Component->GetSnapshot().bDowned);
			};

			// 二次裁决流程：吃一条 Safe 鱼只为再走一次 EvaluateDownedFromAttributes，验证倒地不会被残留毒值判回来。
			const auto ReevaluateAfterRecovery = [&](const TCHAR* PathName, const FName FishDefinitionId)
			{
				UCatFishDefinition* SafeFish = CatConditionComponentTest::MakeConsumableFish(
					Character, FishDefinitionId, ECatFishFoodSafety::Safe, 0.0);
				Component->ConsumeCommittedFish(FGuid::NewGuid(), SafeFish);
				TestFalse(FString::Printf(TEXT("%s 后重新裁决不会把人判回倒地"), PathName), Component->GetSnapshot().bDowned);
			};

			DriveIntoDowned(TEXT("野外自救"), TEXT("RecoveryFieldToxicFish"));
			const FCatDomainCommandResult FieldRecovery = Component->RequestFieldSelfRecovery(Owner, FGuid::NewGuid());
			TestTrue(TEXT("野外自救提交成功"), FieldRecovery.bCommitted);
			TestEqual(TEXT("野外自救无错误"), FieldRecovery.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("野外自救清空 Poison"),
				AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 0.0f);
			TestFalse(TEXT("野外自救解除倒地"), Component->GetSnapshot().bDowned);
			TestEqual(TEXT("野外自救记录恢复方式"), Component->GetSnapshot().RecoveryMode, ECatRecoveryMode::FieldSelfRecovery);
			ReevaluateAfterRecovery(TEXT("野外自救"), TEXT("RecoveryFieldSafeFish"));

			DriveIntoDowned(TEXT("营地休息"), TEXT("RecoveryCampToxicFish"));
			const FCatDomainCommandResult CampRecovery = Component->RequestCampRest(Owner, FGuid::NewGuid(), true);
			TestTrue(TEXT("营地休息提交成功"), CampRecovery.bCommitted);
			TestEqual(TEXT("营地休息无错误"), CampRecovery.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("营地休息清空 Poison"),
				AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 0.0f);
			TestFalse(TEXT("营地休息解除倒地"), Component->GetSnapshot().bDowned);
			TestEqual(TEXT("营地休息记录恢复方式"), Component->GetSnapshot().RecoveryMode, ECatRecoveryMode::CampRest);
			ReevaluateAfterRecovery(TEXT("营地休息"), TEXT("RecoveryCampSafeFish"));

			DriveIntoDowned(TEXT("伙伴搬运"), TEXT("RecoveryCarryToxicFish"));
			const FCatDomainCommandResult CarryRecovery = Component->CompleteCarryToCamp(Helper, FGuid::NewGuid(), true);
			TestTrue(TEXT("伙伴搬运提交成功"), CarryRecovery.bCommitted);
			TestEqual(TEXT("伙伴搬运无错误"), CarryRecovery.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("伙伴搬运清空 Poison"),
				AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 0.0f);
			TestFalse(TEXT("伙伴搬运解除倒地"), Component->GetSnapshot().bDowned);
			TestEqual(TEXT("伙伴搬运记录恢复方式"), Component->GetSnapshot().RecoveryMode, ECatRecoveryMode::CarriedToCamp);
			ReevaluateAfterRecovery(TEXT("伙伴搬运"), TEXT("RecoveryCarrySafeFish"));

			Owner->UnPossess();
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
