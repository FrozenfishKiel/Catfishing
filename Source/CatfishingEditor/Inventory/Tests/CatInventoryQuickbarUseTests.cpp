#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Character/CatCharacter.h"
#include "AbilitySystem/Fishing/InputAbilities/CatFishingChumAbility.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace CatInventoryQuickbarUseTests
{
	/** 读取精确来源实例的活动 Chum Spec 与 WaitInputRelease 状态；测试不再依赖 CommandComponent 的影子蓄力时间。 */
	bool IsChumUseWaiting(ACatCharacter* Character, UCatInventoryComponent* Inventory, const FGuid InstanceId, const FGuid RequestId)
	{
		const FCatInventoryEntry* Entry = Inventory ? Inventory->GetInventoryEntryAtSlot(Inventory->FindInventorySlotIndexFromInstanceId(InstanceId)) : nullptr;
		const UCatAbilitySystemComponent* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
		if (!Entry || !Entry->Instance || !ASC) return false;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
			if (Spec.SourceObject.Get() == Entry->Instance && Spec.IsActive())
				if (const UCatGA_FishingChum* Ability = Cast<UCatGA_FishingChum>(Spec.GetPrimaryInstance()))
					return (!RequestId.IsValid() || Ability->MatchesActiveUseRequest(RequestId)) && Ability->IsWaitingForInputRelease();
		return false;
	}
	/** PIE 设置恢复命令：保存本测试临时覆盖的网络拓扑，结束后还原编辑器原有运行偏好。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 构造时读取用户原本的 PIE 网络参数和驱动定义，避免测试退出后留下联机配置。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode);
			Settings->GetPlayNumberOfClients(ClientCount);
			Settings->GetRunUnderOneProcess(bOneProcess);
			NetDrivers = GEngine->NetDriverDefinitions;
		}

		/** 等待 PIE 真正销毁后整体写回初始快照，防止运行期驱动被过早替换。 */
		virtual bool Update() override
		{
			if (GEditor->PlayWorld)
			{
				return false;
			}
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode);
			Settings->SetPlayNumberOfClients(ClientCount);
			Settings->SetRunUnderOneProcess(bOneProcess);
			GEngine->NetDriverDefinitions = NetDrivers;
			return true;
		}

	private:
		/** 测试启动前的 PIE 网络模式；恢复时必须完整保留用户对单机或联机的选择。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 测试启动前的额外客户端数量；listen-server 用例只在运行期临时改为一名远端玩家。 */
		int32 ClientCount = 1;
		/** 测试启动前的进程方式；恢复后不强制改变用户的多进程工作流。 */
		bool bOneProcess = true;
		/** 测试启动前所有 NetDriver 定义；IP 驱动覆盖只对本次 PIE 生效。 */
		TArray<FNetDriverDefinition> NetDrivers;
	};

	/** 正式双端 PIE 状态机：通过 listen host 的 Controller 快捷栏 RPC 验证槽位身份、重放和连续窝料清理。 */
	class FVerifySelectedItemUse final : public IAutomationLatentCommand
	{
	public:
		/** 保存 Automation 断言出口；状态机不拥有 PIE 世界、角色或库存的生命周期。 */
		explicit FVerifySelectedItemUse(FAutomationTestBase* InTest) : Test(InTest) {}

		/** 每轮只推进一个权威可观测步骤；未就绪时等待，超时则输出能复查的阶段和对象状态。 */
		virtual bool Update() override
		{
			if (StartedAt <= 0.0)
			{
				StartedAt = FPlatformTime::Seconds();
			}
			if (FPlatformTime::Seconds() - StartedAt > 20.0)
			{
				ACatCharacter* TimeoutCharacter = HostController.IsValid() ? Cast<ACatCharacter>(HostController->GetPawn()) : nullptr;
				Test->AddError(FString::Printf(TEXT("Quickbar selected-use formal timeout Stage=%d Server=%s Controller=%s Backpack=%s Charge=%.3f"),
					Stage, *GetNameSafe(ServerWorld.Get()), *GetNameSafe(HostController.Get()), *GetNameSafe(BackPack.Get()),
					IsChumUseWaiting(TimeoutCharacter, BackPack.Get(), FirstChumId, FirstChumRequestId) ? 1.0 : -1.0));
				return true;
			}

			switch (Stage)
			{
			case 0: return PrepareAuthorityEndpoint();
			case 1: return SeedFormalInventory();
			case 2: return VerifyMismatchedSlotRejected();
			case 3: return BeginAndReplayFirstChum();
			case 4: return SwapThenCancelOriginalChum();
			case 5: return RemoveThenCancelSecondChum();
			case 6: return BeginAndReleaseRemainingChum();
			case 7: return VerifyNormalReleaseCompleted();
			default:
				Test->AddError(TEXT("Quickbar selected-use test reached an unknown stage."));
				return true;
			}
		}

	private:
		/** 定位 TestMap 的 listen-server、主机 Controller、个人背包和命令门；只有正式玩法门已打开才开始发送 Use RPC。 */
		bool PrepareAuthorityEndpoint()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && Context.WorldType == EWorldType::PIE && World->GetNetMode() == NM_ListenServer)
				{
					ServerWorld = World;
					break;
				}
			}
			if (!ServerWorld.IsValid())
			{
				return false;
			}
			HostController = Cast<ACatfishingPlayerController>(ServerWorld->GetFirstPlayerController());
			ACatCharacter* HostCharacter = HostController.IsValid() ? Cast<ACatCharacter>(HostController->GetPawn()) : nullptr;
			BackPack = HostCharacter ? Cast<UCatBackPackComponent>(HostCharacter->GetInventoryComponent()) : nullptr;
			Commands = HostController.IsValid() ? HostController->GetFishingCommandComponent() : nullptr;
			const ACatfishingGameModeBase* GameMode = ServerWorld->GetAuthGameMode<ACatfishingGameModeBase>();
			if (!HostController.IsValid() || !BackPack.IsValid() || !Commands.IsValid() || !GameMode
				|| !GameMode->CanAcceptGameplayCommand(HostController.Get()))
			{
				return false;
			}
			Stage = 1;
			return false;
		}

		/** 清空测试 PIE 背包后装入两种正式窝料和两根正式鱼竿；四个实例让本回归只依赖真实资产及四格容量。 */
		bool SeedFormalInventory()
		{
			UCatEquipmentDefinition* BugChum = LoadObject<UCatEquipmentDefinition>(nullptr,
				TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_Bug.Equip_Chum_Bug"));
			UCatEquipmentDefinition* GrainChum = LoadObject<UCatEquipmentDefinition>(nullptr,
				TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_FermentedGrain.Equip_Chum_FermentedGrain"));
			UCatEquipmentDefinition* StarterRod = LoadObject<UCatEquipmentDefinition>(nullptr,
				TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_StarterT1.Equip_Rod_StarterT1"));
			UCatEquipmentDefinition* ShopRod = LoadObject<UCatEquipmentDefinition>(nullptr,
				TEXT("/Game/Catfishing/Data/Equipment/Equip_Rod_ShopT2.Equip_Rod_ShopT2"));
			if (!Test->TestTrue(TEXT("two formal chum and two formal rod definitions load"),
				BugChum && GrainChum && StarterRod && ShopRod))
			{
				return true;
			}
			const TArray<FCatInventoryEntry> EmptyEntries;
			if (!Test->TestTrue(TEXT("clear host backpack to the configured four visible slots"),
				BackPack->ReplaceInventoryEntriesFromAuthority(EmptyEntries, 4))
				|| !Test->TestTrue(TEXT("add the first formal chum"), BackPack->AddItemDefinition(BugChum, 1))
				|| !Test->TestTrue(TEXT("add the second formal chum"), BackPack->AddItemDefinition(GrainChum, 1))
				|| !Test->TestTrue(TEXT("add the first formal rod"), BackPack->AddItemDefinition(StarterRod, 1))
				|| !Test->TestTrue(TEXT("add the second formal rod"), BackPack->AddItemDefinition(ShopRod, 1)))
			{
				return true;
			}
			if (!Test->TestEqual(TEXT("formal selected-use fixture keeps exactly four backpack slots"), BackPack->GetInventorySlotCount(), 4))
			{
				return true;
			}
			FirstChumSlot = BackPack->FindFirstInventorySlotIndexByDefinitionId(TEXT("BugChum"));
			SecondChumSlot = BackPack->FindFirstInventorySlotIndexByDefinitionId(TEXT("FermentedGrainChum"));
			const FCatInventoryEntry* FirstChum = BackPack->GetInventoryEntryAtSlot(FirstChumSlot);
			const FCatInventoryEntry* SecondChum = BackPack->GetInventoryEntryAtSlot(SecondChumSlot);
			if (!Test->TestTrue(TEXT("formal chum entries resolve to distinct live instances"),
				FirstChum && FirstChum->Instance && SecondChum && SecondChum->Instance
				&& FirstChum->Instance->GetItemInstanceId().IsValid() && SecondChum->Instance->GetItemInstanceId().IsValid()
				&& FirstChum->Instance->GetItemInstanceId() != SecondChum->Instance->GetItemInstanceId()))
			{
				return true;
			}
			FirstChumId = FirstChum->Instance->GetItemInstanceId();
			SecondChumId = SecondChum->Instance->GetItemInstanceId();
			FirstChumInitialCount = FirstChum->StackCount;
			Stage = 2;
			return false;
		}

		/** 用正确槽位配上错误实例 ID 调快捷栏 RPC；拒绝必须保持未蓄力，证明服务器不按定义或客户端选择猜测物品。 */
		bool VerifyMismatchedSlotRejected()
		{
			HostController->ServerUseSelectedBackpackItem(FGuid::NewGuid(), FirstChumSlot, SecondChumId);
			if (!Test->TestFalse(TEXT("slot/item mismatch does not begin a source chum ability"), IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FGuid())))
			{
				return true;
			}
			Stage = 3;
			return false;
		}

		/** 开始第一份窝料并原样重放同一 RequestId；后者必须复用库存终态，不能建立第二次连续输入或重复消耗。 */
		bool BeginAndReplayFirstChum()
		{
			FirstChumRequestId = FGuid::NewGuid();
			HostController->ServerUseSelectedBackpackItem(FirstChumRequestId, FirstChumSlot, FirstChumId);
			if (!Test->TestTrue(TEXT("listen host begins the first formal chum through selected-use gateway"),
				IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FirstChumRequestId)))
			{
				return true;
			}
			HostController->ServerUseSelectedBackpackItem(FirstChumRequestId, FirstChumSlot, FirstChumId);
			const FCatInventoryEntry* FirstChumAfterReplay = BackPack->GetInventoryEntryAtSlot(FirstChumSlot);
			if (!Test->TestTrue(TEXT("replaying the same request keeps one active formal chum session"),
				IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FirstChumRequestId))
				|| !Test->TestNotNull(TEXT("replay keeps the original formal chum entry readable"), FirstChumAfterReplay)
				|| !Test->TestEqual(TEXT("replaying continuous Begin does not consume the formal chum twice"),
					FirstChumAfterReplay->StackCount, FirstChumInitialCount))
			{
				return true;
			}
			Stage = 4;
			return false;
		}

		/** 交换两份窝料后仍以 Begin 的原 RequestId 与原实例取消；槽位旧身份再次 Use 必须被拒绝，取消则必须结束原持续会话。 */
		bool SwapThenCancelOriginalChum()
		{
			if (!Test->TestTrue(TEXT("swap two formal chum slots while the first one is held"),
				UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(BackPack.Get(), FirstChumSlot, BackPack.Get(), SecondChumSlot)))
			{
				return true;
			}
			HostController->ServerUseSelectedBackpackItem(FGuid::NewGuid(), FirstChumSlot, FirstChumId);
			if (!Test->TestTrue(TEXT("old slot plus original item identity is rejected after the swap"),
				IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FirstChumRequestId)))
			{
				return true;
			}
			HostController->ServerEndSelectedBackpackItem(FirstChumRequestId, FirstChumId, true);
			if (!Test->TestTrue(TEXT("changing slots does not redirect cancel and the original chum session closes"),
				!IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FirstChumRequestId)))
			{
				return true;
			}
			Stage = 5;
			return false;
		}

		/** 第二份窝料开始后从可见格移走再取消；原实例引用必须仍能清掉命令组件状态，后续 Use 才不会卡在旧 Phase。 */
		bool RemoveThenCancelSecondChum()
		{
			const int32 CurrentSecondChumSlot = BackPack->FindInventorySlotIndexFromInstanceId(SecondChumId);
			SecondChumRequestId = FGuid::NewGuid();
			HostController->ServerUseSelectedBackpackItem(SecondChumRequestId, CurrentSecondChumSlot, SecondChumId);
			if (!Test->TestTrue(TEXT("the second formal chum begins before removal"), IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), SecondChumId, SecondChumRequestId)))
			{
				return true;
			}
			FCatInventoryEntry RemovedEntry;
			if (!Test->TestTrue(TEXT("remove the active second chum from its visible backpack slot"),
				BackPack->RemoveInventoryEntryAtSlotFromAuthority(CurrentSecondChumSlot, RemovedEntry)))
			{
				return true;
			}
			HostController->ServerEndSelectedBackpackItem(SecondChumRequestId, SecondChumId, true);
			if (!Test->TestTrue(TEXT("removed active chum still cancels the original command session"),
				!IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), SecondChumId, SecondChumRequestId)))
			{
				return true;
			}
			Stage = 6;
			return false;
		}

		/** 使用仍在背包的第一份窝料并正常松开；这验证 listen host 不会因本地清理顺序而丢掉 End 的匹配身份。 */
		bool BeginAndReleaseRemainingChum()
		{
			const int32 CurrentFirstChumSlot = BackPack->FindInventorySlotIndexFromInstanceId(FirstChumId);
			if (!Test->TestTrue(TEXT("listen host selects the remaining formal chum through the local quickbar path"),
				HostController->RequestSelectQuickbarSlotFromInput(CurrentFirstChumSlot)))
			{
				return true;
			}
			HostController->BeginSelectedItemUseFromInput();
			if (!Test->TestTrue(TEXT("remaining formal chum activates its source ability after moved-item cancellation"),
				IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FGuid())))
			{
				return true;
			}
			HostController->EndSelectedItemUseFromInput(false);
			if (!Test->TestFalse(TEXT("listen-host release ends the source ability task"), IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FGuid()))) return true;
			Stage = 7;
			return false;
		}

		/** 正常 Release 后只检查连续会话已收口；投放结果继续由正式水域、弹道和库存事务决定，不在测试里伪造。 */
		bool VerifyNormalReleaseCompleted()
		{
			return Test->TestFalse(TEXT("listen-host normal release clears the original continuous chum session"),
				IsChumUseWaiting(Cast<ACatCharacter>(HostController->GetPawn()), BackPack.Get(), FirstChumId, FGuid()));
		}

		/** Automation 断言出口；所有失败都写入同一个测试实例。 */
		FAutomationTestBase* Test = nullptr;
		/** PIE 可见后的单调秒基准；用于识别地图加载或 Run 命令门没有打开的超时。 */
		double StartedAt = 0.0;
		/** 只在前一阶段成功后推进的状态机位置；每个值对应一次独立的权威断言。 */
		int32 Stage = 0;
		/** TestMap 的 listen-server World；快捷栏 RPC 的服务器裁决必须发生在此端。 */
		TWeakObjectPtr<UWorld> ServerWorld;
		/** listen host 的本地 Controller；它直接承接本轮真实 ServerUse/ServerEnd 网关调用。 */
		TWeakObjectPtr<ACatfishingPlayerController> HostController;
		/** 主机 Pawn 的唯一随身背包；四个正式测试物都从这里读取槽位和实例身份。 */
		TWeakObjectPtr<UCatBackPackComponent> BackPack;
		/** 主机的正式钓鱼命令组件；Chum 蓄力时间是连续 Use 是否遗留的权威可观测状态。 */
		TWeakObjectPtr<UCatFishingCommandComponent> Commands;
		/** 第一份窝料初始所在槽位；交换后只用于构造必须被拒绝的旧槽位身份组合。 */
		int32 FirstChumSlot = INDEX_NONE;
		/** 第二份窝料初始所在槽位；交换后改按实例 ID 重新解析它的真实位置。 */
		int32 SecondChumSlot = INDEX_NONE;
		/** 第一份正式窝料的稳定实例 ID；连续 Begin、换格 Cancel 和最终 Release 都固定使用它。 */
		FGuid FirstChumId;
		/** 第二份正式窝料的稳定实例 ID；移出可见格后仍作为 Cancel 的原始身份。 */
		FGuid SecondChumId;
		/** 第一份窝料 Begin 前的数量；重放后仍须相同，防止同一 RequestId 被当作第二次消耗。 */
		int32 FirstChumInitialCount = 0;
		/** 第一份窝料的 Begin 请求；重放与 Cancel 必须携带完全相同的关联 ID。 */
		FGuid FirstChumRequestId;
		/** 第二份窝料的 Begin 请求；用于验证移走后仍能清理原命令。 */
		FGuid SecondChumRequestId;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryQuickbarSelectedUseFormalNetworkTest,
	"Catfishing.Editor.Inventory.Quickbar.FormalSelectedUseGatewayAndContinuousCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 启动正式 TestMap listen-server PIE，验证快捷栏 Use 的服务器身份校验、库存幂等与持续使用清理，然后恢复编辑器网络设置。 */
bool FCatInventoryQuickbarSelectedUseFormalNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("quickbar selected-use formal test requires an idle editor"), GEditor && GEngine && !GEditor->PlayWorld))
	{
		return false;
	}
	const TSharedRef<CatInventoryQuickbarUseTests::FRestoreSettings> Restore = MakeShared<CatInventoryQuickbarUseTests::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatInventoryQuickbarUseTests::FVerifySelectedItemUse>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
