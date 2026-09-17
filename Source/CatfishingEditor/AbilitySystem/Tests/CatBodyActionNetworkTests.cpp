#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/BodyAction/Social/CatGA_BodyActionPlaceProtectionSign.h"
#include "AbilitySystem/Tags/CatStateTags.h"

namespace CatBodyActionNetwork
{
/** PIE 临时设置的所有权；测试结束后恢复网络拓扑、驱动与前摇，避免污染正式配置。 */
class FRestore final : public IAutomationLatentCommand
{
public:
	/** 记录本次测试会临时修改的值。 */
	FRestore()
	{
		auto* Settings = GetDefault<ULevelEditorPlaySettings>();
		Settings->GetPlayNetMode(Mode); Settings->GetPlayNumberOfClients(Clients); Settings->GetRunUnderOneProcess(OneProcess);
		Drivers = GEngine->NetDriverDefinitions;
		Presentation = GetDefault<UCatBodyActionPresentationSettings>()->ActionPresentationConfigs;
	}
	/** 等 PIE 销毁后恢复原值，不在运行中切换驱动。 */
	bool Update() override
	{
		if (GEditor->PlayWorld) return false;
		auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
		Settings->SetPlayNetMode(Mode); Settings->SetPlayNumberOfClients(Clients); Settings->SetRunUnderOneProcess(OneProcess);
		GEngine->NetDriverDefinitions = Drivers;
		GetMutableDefault<UCatBodyActionPresentationSettings>()->ActionPresentationConfigs = Presentation;
		return true;
	}
private:
	/** 用户原有网络模式，结束时恢复。 */
	EPlayNetMode Mode = PIE_Standalone;
	/** 用户原有参与者数量，结束时恢复。 */
	int32 Clients = 1;
	/** 用户原有 PIE 进程方式，结束时恢复。 */
	bool OneProcess = true;
	/** 原有驱动定义，测试临时改用 IP。 */
	TArray<FNetDriverDefinition> Drivers;
	/** 原有正式动作配置，测试仅延长前摇以观察取消窗口。 */
	TArray<FCatBodyActionPresentationConfig> Presentation;
};

/** 正式地图双端回归：远端本地预测启动 GA、拥有者播放正式 Montage，并验证多来源倒地复制与取消。 */
class FVerify final : public IAutomationLatentCommand
{
public:
	/** 保存断言接收者；超时从第一次更新开始。 */
	explicit FVerify(FAutomationTestBase* InTest) : Test(InTest) {}
	/** 先验证同帧本地预测早于服务器激活，再观察倒地取消、独立来源撤销、迟到状态拒绝和客户端主动取消；只走生产请求，不手工调用 OnRep 或播放表现。 */
	bool Update() override
	{
		if (!Started) Started = FPlatformTime::Seconds();
		if (FPlatformTime::Seconds() - Started > 45) { Test->AddError(FString::Printf(TEXT("BodyAction network timeout Stage=%d"), Stage)); return true; }
		if (Stage == 0)
		{
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (Context.WorldType != EWorldType::PIE || !World) continue;
				if (World->GetNetMode() == NM_Client) Client = Cast<ACatfishingPlayerController>(World->GetFirstPlayerController());
				if (World->GetNetMode() == NM_ListenServer)
					for (auto It = World->GetPlayerControllerIterator(); It; ++It)
						if (!It->Get()->IsLocalController()) Server = Cast<ACatfishingPlayerController>(It->Get());
			}
			if (!Client.IsValid() || !Server.IsValid()) return false;
			auto* ClientCat = Cast<ACatCharacter>(Client->GetPawn());
			auto* ServerCat = Cast<ACatCharacter>(Server->GetPawn());
			if (!ClientCat || !ServerCat) return false;
			ClientASC = ClientCat->GetCatAbilitySystemComponent(); ServerASC = ServerCat->GetCatAbilitySystemComponent();
			if (!ClientASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())) return false;
			// 弱网仅影响这两个测试 World；结束 PIE 后随驱动销毁。
			FPacketSimulationSettings Packets; Packets.PktLag = 100; Packets.PktLoss = 5;
			Server->GetWorld()->GetNetDriver()->SetPacketSimulationSettings(Packets);
			Client->GetWorld()->GetNetDriver()->SetPacketSimulationSettings(Packets);
			Client->RequestPlaceProtectionSign(FGuid::NewGuid(), ClientCat->GetActorLocation());
			// 同一个调用栈内断言：网络尚未派发，拥有者已经启动正式动画。
            Test->TestTrue(TEXT("请求当帧本地 GA 已激活"), ClientASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())->IsActive());
            Test->TestNotNull(TEXT("服务器批准前已有本地蒙太奇"), ClientASC->GetCurrentMontage());
            Test->TestFalse(TEXT("本地开始时服务器尚未激活"), ServerASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())->IsActive());
            Stage = 1; return false;
		}
		if (Stage == 1)
		{
			const auto* Spec = ClientASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass());
			if (!Spec || !Spec->IsActive()
                || !ServerASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())->IsActive()) return false;
			// 动画已在请求当帧验证；短 Montage 可以早于网络确认结束，取消窗口由 GA 前摇而非动画长度决定。
			ServerASC->SetStateTagsFromAuthority(TEXT("Test.SourceA"), FGameplayTagContainer(CatStateTags::Downed));
			ServerASC->SetStateTagsFromAuthority(TEXT("Test.SourceB"), FGameplayTagContainer(CatStateTags::Downed));
			Stage = 2; return false;
		}
		if (Stage == 2)
		{
			const auto* Spec = ClientASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass());
			if (!ClientASC->HasMatchingGameplayTag(CatStateTags::Downed) || (Spec && Spec->IsActive())) return false;
			Test->TestFalse(TEXT("服务器前摇已取消"), ServerASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())->IsActive());
			ServerASC->SetStateTagsFromAuthority(TEXT("Test.SourceA"), FGameplayTagContainer());
			ChangedAt = FPlatformTime::Seconds(); Stage = 3; return false;
		}
		if (Stage == 3)
		{
			if (FPlatformTime::Seconds() - ChangedAt < 1) return false;
			Test->TestTrue(TEXT("撤销一个来源后客户端仍倒地"), ClientASC->HasMatchingGameplayTag(CatStateTags::Downed));
			ServerASC->SetStateTagsFromAuthority(TEXT("Test.SourceB"), FGameplayTagContainer());
			Stage = 4; return false;
		}
        if (Stage == 4)
        {
            if (ClientASC->HasMatchingGameplayTag(CatStateTags::Downed)) return false;
            // 状态尚未复制时本地可以预测，服务器随后拒绝；拒绝必须收掉本地动作。
            ServerASC->SetStateTagsFromAuthority(TEXT("Test.Reject"), FGameplayTagContainer(CatStateTags::Downed));
            Client->RequestPlaceProtectionSign(FGuid::NewGuid(), Client->GetPawn()->GetActorLocation());
            Test->TestTrue(TEXT("迟到状态不阻塞本地预测启动"), ClientASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())->IsActive());
            Stage = 5; return false;
        }
        if (Stage == 5)
        {
            if (!ClientASC->HasMatchingGameplayTag(CatStateTags::Downed)
                || ClientASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())->IsActive()) return false;
            ServerASC->SetStateTagsFromAuthority(TEXT("Test.Reject"), FGameplayTagContainer());
            Stage = 6; return false;
        }
        if (Stage == 6)
        {
            if (ClientASC->HasMatchingGameplayTag(CatStateTags::Downed)) return false;
            Client->RequestPlaceProtectionSign(FGuid::NewGuid(), Client->GetPawn()->GetActorLocation());
            const auto* Spec = ClientASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass());
            ClientASC->CancelAbilityHandle(Spec->Handle);
            Test->TestFalse(TEXT("本地取消不等待服务器"), Spec->IsActive());
            ChangedAt = FPlatformTime::Seconds(); Stage = 7; return false;
        }
        if (FPlatformTime::Seconds() - ChangedAt < 2) return false;
        Test->TestFalse(TEXT("预测取消到达服务器且未等待提交窗口"), ServerASC->FindAbilitySpecFromClass(UCatGA_BodyActionPlaceProtectionSign::StaticClass())->IsActive());
        Test->AddInfo(TEXT("Event=body_action_network_verified LocalPredictedBeforeServer=1 RemoteMontage=1 Cancelled=1 RejectedPrediction=1 ClientCancel=1 IndependentSources=1 LagMs=100 LossPct=5"));
        return true;
	}
private:
	/** Automation 断言接收者，测试框架拥有它。 */
	FAutomationTestBase* Test;
	/** 单调时钟起点，只用于测试超时。 */
	double Started = 0;
	/** 当前等待步骤的起点；来源撤销和客户端取消后分别重置，用来给复制预留观察时间。 */
	double ChangedAt = 0;
	/** 已完成的观察步骤，不参与玩法裁决。 */
	int32 Stage = 0;
	/** 远端拥有者控制器，只从此端发起本地 GameplayEvent。 */
	TWeakObjectPtr<ACatfishingPlayerController> Client;
	/** 对应服务器控制器，仅施加测试状态。 */
	TWeakObjectPtr<ACatfishingPlayerController> Server;
	/** 客户端身体 ASC，读取真实网络结果。 */
	TWeakObjectPtr<UCatAbilitySystemComponent> ClientASC;
	/** 服务器身体 ASC，持有两个独立状态来源。 */
	TWeakObjectPtr<UCatAbilitySystemComponent> ServerASC;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatBodyActionNetworkTest,
	"Catfishing.Editor.AbilitySystem.RemoteMontageAndStateCancellationWeakNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 正式地图提供真实角色与蒙太奇；临时延长前摇，让弱网下的观察和取消仍早于提交，PIE 结束后恢复原设置。
bool FCatBodyActionNetworkTest::RunTest(const FString& Parameters)
{
	if (!GEditor || !GEngine || GEditor->PlayWorld) return false;
	auto Restore = MakeShared<CatBodyActionNetwork::FRestore>();
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
	for (auto& Driver : GEngine->NetDriverDefinitions)
		if (Driver.DefName == TEXT("GameNetDriver")) { Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"); Driver.DriverClassNameFallback = Driver.DriverClassName; }
	for (auto& Config : GetMutableDefault<UCatBodyActionPresentationSettings>()->ActionPresentationConfigs) Config.LeadInSeconds = 10;
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatBodyActionNetwork::FVerify>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}
#endif
