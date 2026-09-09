#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishingGroupModel.h"

#include <limits>

namespace
{
	FCatFightGroupInput MakeGroup(const int32 Count)
	{
		FCatFightGroupInput Input;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FCatFightGroupParticipantInput Participant;
			Participant.bPrimary = Index == 0;
			Participant.FishingStrength = 100.0;
			Participant.CurrentStamina = Participant.MaximumStamina = 60.0;
			Participant.MassKilograms = 5.0;
			Participant.MaximumMoveSpeedCentimetersPerSecond = 300.0;
			Input.Participants.Add(Participant);
		}
		return Input;
	}

	FCatFightGroupStaminaInput MakeStaminaInput(const FCatFightGroupInput& Input, const FCatFightGroupResult& Result)
	{
		FCatFightGroupStaminaInput Settlement;
		Settlement.Participants = Input.Participants;
		Settlement.Contributions = Result.Participants;
		Settlement.PersonalMovementDrains.Init(0.0, Input.Participants.Num());
		return Settlement;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupDirectionTest,
	"Catfishing.Unit.Fishing.Group.OneToFourMembersShareOneDirectionalBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupDirectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightGroupResult Result;
	for (int32 Count = 1; Count <= 4; ++Count)
	{
		FCatFightGroupInput Input = MakeGroup(Count);
		TestTrue(TEXT("一到四人站定均通过同一纯入口"), FCatFishingGroupModel::ComputeForces(Input, Result));
		TestEqual(TEXT("人数变化不丢失个人输出"), Result.Participants.Num(), Count);
		TestEqual(TEXT("主位完整力量，辅助使用独立五成折扣"), Result.TotalActiveStrength, 100.0 + 50.0 * (Count - 1));
		TestEqual(TEXT("站定保留既有完整支撑"), Result.SignedResistanceStrength, Result.TotalActiveStrength);
		TestTrue(TEXT("站定支撑不自行产生走路目标"), Result.DesiredVelocityCentimetersPerSecond.IsZero());
		for (FCatFightGroupParticipantInput& Participant : Input.Participants) Participant.MoveIntentWorld = -FVector::ForwardVector;
		TestTrue(TEXT("所有人后退通过同一纯入口"), FCatFishingGroupModel::ComputeForces(Input, Result));
		TestTrue(TEXT("同向协作不会将走路速度乘以人数"), Result.DesiredVelocityCentimetersPerSecond.Equals(FVector(-300.0, 0.0, 0.0), 1e-9));
		for (const FCatFightGroupParticipantResult& Participant : Result.Participants)
		{
			TestTrue(TEXT("主动移动与静止支撑没有重复分配力量"), Participant.AppliedStrengthWorld.Size() <= Participant.ActiveStrength + 1e-9);
		}
	}
	FCatFightGroupInput Four = MakeGroup(4);
	for (FCatFightGroupParticipantInput& Participant : Four.Participants) Participant.MoveIntentWorld = -FVector::ForwardVector;
	Four.Participants[3].MoveIntentWorld = FVector::ForwardVector;
	TestTrue(TEXT("一人反向仍是合法玩法输入"), FCatFishingGroupModel::ComputeForces(Four, Result));
	TestEqual(TEXT("主位和两名辅助后退，一名辅助前进时实际抵抗力降为150"), Result.SignedResistanceStrength, 150.0);
	TestTrue(TEXT("反向输入明确减慢共同后退"), Result.DesiredVelocityCentimetersPerSecond.Equals(FVector(-180.0, 0.0, 0.0), 1e-9));
	Four.Participants[3].MoveIntentWorld = FVector::RightVector;
	TestTrue(TEXT("侧向辅助能改变共同方向"), FCatFishingGroupModel::ComputeForces(Four, Result));
	TestEqual(TEXT("侧向辅助不同时获得完整沿线支撑"), Result.Participants[3].SignedResistanceStrength, 0.0);
	TestTrue(TEXT("侧向贡献进入组移动目标"), Result.DesiredVelocityCentimetersPerSecond.Y > 0.0);
	for (FCatFightGroupParticipantInput& Participant : Four.Participants) Participant.MoveIntentWorld = FVector::ForwardVector;
	TestTrue(TEXT("全队主动向水边走可以产生负抵抗力"), FCatFishingGroupModel::ComputeForces(Four, Result));
	TestEqual(TEXT("朝鱼方向的力量不会被伪装为正支撑"), Result.SignedResistanceStrength, -250.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupStaminaBalanceTest,
	"Catfishing.Unit.Fishing.Group.StaminaSumPreservesIndividualBalancesAndZeroPowerBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupStaminaBalanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightGroupInput Input = MakeGroup(4);
	FCatFightGroupResult Result;
	for (const double Stamina : {60.0, 30.0, 1e-12})
	{
		for (FCatFightGroupParticipantInput& Participant : Input.Participants) Participant.CurrentStamina = Stamina;
		TestTrue(TEXT("任意正体力均可计算"), FCatFishingGroupModel::ComputeForces(Input, Result));
		TestEqual(TEXT("正体力不按比例削弱力量"), Result.TotalActiveStrength, 250.0);
		TestEqual(TEXT("总体力只合计独立余额"), Result.TotalCurrentStamina, 4.0 * Stamina);
	}
	Input.Participants[0].CurrentStamina = 0.0;
	TestTrue(TEXT("零体力主位仍保留在成员输入中"), FCatFishingGroupModel::ComputeForces(Input, Result));
	TestEqual(TEXT("主位恰好零体力才停止个人出力"), Result.Participants[0].ActiveStrength, 0.0);
	TestEqual(TEXT("辅助仍保持自身折扣后完整力量"), Result.TotalActiveStrength, 150.0);
	TestEqual(TEXT("耗尽不会把成员从名单删掉"), Result.Participants.Num(), 4);
	TestEqual(TEXT("耗尽的身体质量仍在组内"), Result.TotalMassKilograms, 20.0);
	TestEqual(TEXT("耗尽不删掉个人最大体力"), Result.TotalMaximumStamina, 240.0);
	const double SurvivorStamina = Input.Participants[1].CurrentStamina;
	Input.Participants.RemoveAt(3);
	TestTrue(TEXT("退出通过新的冻结数组重新计算"), FCatFishingGroupModel::ComputeForces(Input, Result));
	TestEqual(TEXT("退出不为留下成员补体"), Input.Participants[1].CurrentStamina, SurvivorStamina);
	TestEqual(TEXT("退出只移出本人最大体力"), Result.TotalMaximumStamina, 180.0);
	Input.Participants.Reset();
	TestTrue(TEXT("无人输入返回有限的零结果"), FCatFishingGroupModel::ComputeForces(Input, Result));
	TestEqual(TEXT("无人不保留旧总力量"), Result.TotalActiveStrength, 0.0);
	TestTrue(TEXT("无人没有陈旧目标速度"), Result.DesiredVelocityCentimetersPerSecond.IsZero());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupSharedPaymentTest,
	"Catfishing.Unit.Fishing.Group.CommonBillIsEqualAndPersonalMovementStaysPersonal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupSharedPaymentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightGroupInput Input = MakeGroup(4);
	Input.Participants[0].CurrentStamina = 1.0;
	Input.Participants[1].CurrentStamina = 10.0;
	Input.Participants[2].CurrentStamina = 10.0;
	Input.Participants[3].CurrentStamina = 0.0;
	FCatFightGroupResult Forces;
	TestTrue(TEXT("准备余额不足与耗尽参与者"), FCatFishingGroupModel::ComputeForces(Input, Forces));
	FCatFightGroupStaminaInput Bill = MakeStaminaInput(Input, Forces);
	Bill.PersonalMovementDrains[1] = 2.0;
	Bill.SharedStaminaDrain = 9.0;
	FCatFightGroupStaminaResult Paid;
	TestTrue(TEXT("个人移动和共同费用一次结算"), FCatFishingGroupModel::SettleStamina(Bill, Paid));
	TestEqual(TEXT("余额不足主位只支付本人剩余一份"), Paid.Participants[0].SharedDrain, 1.0);
	TestEqual(TEXT("辅助承担相同共同费用，不因力量折扣少付"), Paid.Participants[1].SharedDrain, Paid.Participants[2].SharedDrain);
	TestEqual(TEXT("移动费仅落到实际移动者"), Paid.Participants[1].PersonalMovementDrain, 2.0);
	TestEqual(TEXT("未移动者无额外移动费"), Paid.Participants[2].PersonalMovementDrain, 0.0);
	TestEqual(TEXT("耗尽者不透支，也不消费队友余额"), Paid.Participants[3].StaminaDelta, 0.0);
	TestEqual(TEXT("费用只扣一遍，没有旧主位费用再入账"), Paid.TotalStaminaDrain, 11.0);
	TestEqual(TEXT("实际余额守恒"), Paid.TotalRemainingStamina, 10.0);
	TestEqual(TEXT("有足够余额时共同账单全部付清"), Paid.UnpaidSharedStaminaDrain, 0.0);
	Bill.SharedStaminaDrain = 100.0;
	TestTrue(TEXT("超出全队余额的费用仍得到有界结算"), FCatFishingGroupModel::SettleStamina(Bill, Paid));
	TestEqual(TEXT("再大费用也不会产生负体力"), Paid.TotalRemainingStamina, 0.0);
	TestEqual(TEXT("无法支付的共同费用显式返回"), Paid.UnpaidSharedStaminaDrain, 81.0);
	TestEqual(TEXT("纯结算不会反写输入余额"), Bill.Participants[1].CurrentStamina, 10.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupMovementCostTest,
	"Catfishing.Unit.Fishing.Group.OpposingEffortCostsStaminaWithoutPassiveMotionFees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupMovementCostTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightGroupMovementCostInput Input;
	Input.ActiveStrength = 100.0;
	Input.FixedStepSeconds = 0.05;
	Input.MaximumMoveSpeedCentimetersPerSecond = 300.0;
	Input.MoveIntentWorld = -FVector::ForwardVector;
	FCatFightGroupMovementCostResult Backward;
	TestTrue(TEXT("后退但受队友阻挡仍可结算"), FCatFishingGroupModel::ComputeMovementStaminaDrain(Input, Backward));
	Input.MoveIntentWorld = FVector::ForwardVector;
	Input.ActiveStrength = 50.0;
	FCatFightGroupMovementCostResult Forward;
	TestTrue(TEXT("反向辅助也结算自己的实际努力"), FCatFishingGroupModel::ComputeMovementStaminaDrain(Input, Forward));
	TestTrue(TEXT("零净位移并不让互相拉扯免费"), Forward.StaminaDrain > 0.0 && Backward.StaminaDrain > 0.0);
	TestEqual(TEXT("同等努力不因辅助力量折扣降低个人负担"), Forward.StaminaDrain, Backward.StaminaDrain);
	TestEqual(TEXT("受阻时不捏造实际正功"), Forward.WorkStaminaDrain, 0.0);
	Input.ActualDisplacementCentimeters = FVector(15.0, 0.0, 0.0);
	TestTrue(TEXT("完成主动位移按实际正功付费"), FCatFishingGroupModel::ComputeMovementStaminaDrain(Input, Forward));
	TestTrue(TEXT("完成运动产生正功费用"), Forward.WorkStaminaDrain > 0.0);
	TestEqual(TEXT("完全完成意图不额外收受阻费"), Forward.SupportStaminaDrain, 0.0);
	Input.MoveIntentWorld = FVector::ZeroVector;
	TestTrue(TEXT("纯被动位移保持合法"), FCatFishingGroupModel::ComputeMovementStaminaDrain(Input, Forward));
	TestEqual(TEXT("没有主动移动输入不收个人移动费"), Forward.StaminaDrain, 0.0);
	Input.MoveIntentWorld = FVector::ForwardVector;
	Input.ActiveStrength = 0.0;
	TestTrue(TEXT("力竭仍可记录输入但没有实际出力"), FCatFishingGroupModel::ComputeMovementStaminaDrain(Input, Forward));
	TestEqual(TEXT("无力角色不会获得个人移动账单"), Forward.StaminaDrain, 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupRecoveryTest,
	"Catfishing.Unit.Fishing.Group.ExplicitRecoveryNeverTransfersAnotherMembersStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightGroupInput Input = MakeGroup(3);
	Input.Participants[0].CurrentStamina = 0.0;
	Input.Participants[1].CurrentStamina = 59.0;
	Input.Participants[2].CurrentStamina = 30.0;
	FCatFightGroupResult Forces;
	TestTrue(TEXT("准备独立恢复余额"), FCatFishingGroupModel::ComputeForces(Input, Forces));
	FCatFightGroupStaminaInput Bill = MakeStaminaInput(Input, Forces);
	FCatFightGroupStaminaResult Paid;
	TestTrue(TEXT("模型未收到恢复授权时不猜测放线状态"), FCatFishingGroupModel::SettleStamina(Bill, Paid));
	TestEqual(TEXT("无授权即无恢复"), Paid.TotalRecovery, 0.0);
	Bill.RecoveryPerParticipant = 3.0;
	TestTrue(TEXT("调用方显式允许每人恢复"), FCatFishingGroupModel::SettleStamina(Bill, Paid));
	TestEqual(TEXT("主位恢复自己的三点"), Paid.Participants[0].Recovery, 3.0);
	TestEqual(TEXT("即将满体者只补自身缺口"), Paid.Participants[1].Recovery, 1.0);
	TestEqual(TEXT("未用完的恢复额不转给其他成员"), Paid.Participants[2].Recovery, 3.0);
	TestEqual(TEXT("总恢复是三笔个人实收合计"), Paid.TotalRecovery, 7.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupValidationTest,
	"Catfishing.Unit.Fishing.Group.InvalidSnapshotClearsResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFightGroupInput Input = MakeGroup(1);
	FCatFightGroupResult Forces;
	TestTrue(TEXT("先形成有效输出"), FCatFishingGroupModel::ComputeForces(Input, Forces));
	Input.Participants[0].MoveIntentWorld.X = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("非法移动输入拒绝计算"), FCatFishingGroupModel::ComputeForces(Input, Forces));
	TestEqual(TEXT("拒绝后不泄漏上一帧力量"), Forces.TotalActiveStrength, 0.0);
	TestEqual(TEXT("拒绝后不泄漏上一帧成员快照"), Forces.Participants.Num(), 0);
	Input = MakeGroup(1);
	TestTrue(TEXT("重新生成一致快照"), FCatFishingGroupModel::ComputeForces(Input, Forces));
	FCatFightGroupStaminaInput Bill = MakeStaminaInput(Input, Forces);
	Bill.PersonalMovementDrains.Reset();
	FCatFightGroupStaminaResult Paid;
	TestFalse(TEXT("逐人费用数组不匹配时不能错扣另一成员"), FCatFishingGroupModel::SettleStamina(Bill, Paid));
	TestEqual(TEXT("错误费用不会发布部分支付结果"), Paid.Participants.Num(), 0);
	Input.HelperStrengthMultiplier = 1.1;
	TestFalse(TEXT("辅助折扣不能悄悄变成超额奖励"), FCatFishingGroupModel::ComputeForces(Input, Forces));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupLateralIntegrationTest,
	"Catfishing.Unit.Fishing.Group.LateralIntegrationSharesSubstepsAndNeverBrakesIntoReverse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupLateralIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FVector Whole;
	TestTrue(TEXT("共同侧向力可进入共享积分"), FCatFishingGroupModel::IntegrateLateralVelocity(FVector::ZeroVector,
		FVector(0.0, 100.0, 0.0), FVector(0.0, 100.0, 0.0), 0.1, 8.0, 200.0, Whole));
	FVector Partitioned = FVector::ZeroVector;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FVector Next;
		TestTrue(TEXT("碰撞子步可使用同一函数"), FCatFishingGroupModel::IntegrateLateralVelocity(Partitioned,
			FVector(0.0, 100.0, 0.0), FVector(0.0, 100.0, 0.0), 1.0 / 120.0, 8.0, 200.0, Next));
		Partitioned = Next;
	}
	TestTrue(TEXT("固定步预测与CMC子步得到相同切向速度"), Whole.Equals(Partitioned, 1e-9));
	FVector Braked;
	TestTrue(TEXT("松输入后按已有摩擦和制动减速"), FCatFishingGroupModel::IntegrateLateralVelocity(Whole,
		FVector::ZeroVector, FVector::ZeroVector, 0.1, 8.0, 200.0, Braked));
	TestTrue(TEXT("制动不能反向加速"), Braked.Y >= 0.0 && Braked.Size() < Whole.Size());
	TestFalse(TEXT("非法摩擦拒绝给物理写入NaN"), FCatFishingGroupModel::IntegrateLateralVelocity(Whole,
		FVector::ZeroVector, FVector::ZeroVector, 0.1, std::numeric_limits<double>::quiet_NaN(), 200.0, Braked));
	TestTrue(TEXT("非法积分不保留旧速度输出"), Braked.IsZero());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
