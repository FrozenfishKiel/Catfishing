#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatBackPackComponent.h"
#include "UI/CatUISettings.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryQuickbarWidget.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "UObject/UObjectIterator.h"

namespace CatInventoryQuickbarNetwork
{
	/** PIE 设置恢复命令；构造时保存双端启动参数，PIE 销毁后整体还原，避免本回归污染用户编辑器设置。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 读取本轮会改写的网络模式、客户端数量、进程方式和驱动表。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode); Settings->GetPlayNumberOfClients(ClientCount); Settings->GetRunUnderOneProcess(bOneProcess);
			NetDrivers = GEngine->NetDriverDefinitions;
		}
		/** 等 PIE 完整退出后恢复原快照；世界仍存活时继续等待，不能提前覆盖运行期驱动。 */
		bool Update() override
		{
			if (GEditor->PlayWorld) return false;
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode); Settings->SetPlayNumberOfClients(ClientCount); Settings->SetRunUnderOneProcess(bOneProcess);
			GEngine->NetDriverDefinitions = NetDrivers;
			return true;
		}
	private:
		/** 测试开始前的 PIE 网络模式；恢复命令在结束时写回。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 测试开始前的客户端数量；本回归临时设为一个远端客户端。 */
		int32 ClientCount = 1;
		/** 测试开始前的进程拓扑；恢复时不改变用户原有偏好。 */
		bool bOneProcess = true;
		/** 测试开始前的 NetDriver 定义；以整体赋值恢复 IP 驱动临时修改。 */
		TArray<FNetDriverDefinition> NetDrivers;
	};

	/** 正式 TestMap 双端状态机；只从拥有客户端触发 Controller 快捷栏选择，服务器从不收到选择 RPC，正式背包页打开和点击不能反向改写快捷栏焦点。 */
	class FVerifyQuickbar final : public IAutomationLatentCommand
	{
	public:
		/** 保存 Automation 断言接收者；等待计时从 PIE 可见后的首次轮询开始。 */
		explicit FVerifyQuickbar(FAutomationTestBase* InTest) : Test(InTest) {}
		/** 按阶段收集双端、核对正式资产与4格模型，再连续发出本地快捷栏选择，并打开背包页验证页面点击不改 Controller 焦点。 */
		bool Update() override
		{
			if (StartedAt <= 0.0) StartedAt = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StartedAt > 20.0)
			{
				Test->AddError(FString::Printf(TEXT("Quickbar formal two-endpoint timeout Stage=%d Client=%s Backpack=%s ModelSlots=%d Selected=%d"), Stage,
					*GetNameSafe(ClientController.Get()), *GetNameSafe(ClientBackpack.Get()), ClientModel.IsValid() ? ClientModel->GetInventoryList().Num() : -1,
					ClientController.IsValid() ? ClientController->GetSelectedQuickbarSlotIndex() : INDEX_NONE));
				return true;
			}
			if (Stage == 0) return PrepareEndpoints();
			if (Stage == 1)
			{
				const UCatUISettings* Settings = GetDefault<UCatUISettings>();
				if (!Test->TestNotNull(TEXT("formal quickbar WBP is configured and compiled"), Settings ? Settings->LoadInventoryQuickbarWidgetClass().Get() : nullptr)) return true;
				if (!Test->TestEqual(TEXT("client backpack Model has exactly configured four visible slots"), ClientModel->GetInventoryList().Num(), 4)) return true;
				UCatLocalPlayerUISubsystem* UI = ClientController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
				UCatInventoryQuickbarWidget* Quickbar = UI ? UI->GetInventoryQuickbarWidget() : nullptr;
				if (!Test->TestNotNull(TEXT("formal quickbar is actually attached to the owning client viewport"), Quickbar)) return true;
				if (!Test->TestEqual(TEXT("formal quickbar creates one real slot widget per backpack Model slot"), Quickbar->GetDisplayedSlotCount(), ClientModel->GetInventoryList().Num())) return true;
				if (!Test->TestEqual(TEXT("client local initial quickbar selection is first slot"), ClientController->GetSelectedQuickbarSlotIndex(), 0)) return true;
				if (!Test->TestTrue(TEXT("formal first slot visibly renders the local selected outer ring"), Quickbar->IsDisplayedSlotSelected(0))) return true;
				if (!Test->TestTrue(TEXT("number-slot path accepts local quickbar selection request"), ClientController->RequestSelectQuickbarSlotFromInput(1))) return true;
				if (!Test->TestEqual(TEXT("quickbar selection updates immediately without an acknowledgement"), ClientController->GetSelectedQuickbarSlotIndex(), 1)) return true;
				if (!Test->TestTrue(TEXT("formal second slot outer ring updates immediately"), Quickbar->IsDisplayedSlotSelected(1))
					|| !Test->TestFalse(TEXT("formal first slot outer ring hides after local change"), Quickbar->IsDisplayedSlotSelected(0))) return true;
				// 请求当前 PIE 截图消费者连同 UI 写入 Saved/Screenshots；全局请求未承诺固定由 owning-client viewport 消费，文件只用于核查四格底部布局与第二格外圈。
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/InventoryQuickbar/LocalSelectionSlot2.png"), true, false);
				Stage = 2; return false;
			}
			if (Stage == 2)
			{
				// 等上一帧把本地第二格外圈画完，再从 owning-client 的真实 Slate 控件截取像素；不依赖全局截图被哪个 PIE 窗口消费。
				UCatLocalPlayerUISubsystem* UI = ClientController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
				UCatInventoryQuickbarWidget* Quickbar = UI ? UI->GetInventoryQuickbarWidget() : nullptr;
				const TSharedPtr<SWidget> RenderedWidget = Quickbar ? Quickbar->GetCachedWidget() : nullptr;
				TArray<FColor> Pixels;
				FIntVector PixelSize = FIntVector::ZeroValue;
				if (!RenderedWidget.IsValid() || !FSlateApplication::Get().TakeScreenshot(RenderedWidget.ToSharedRef(), Pixels, PixelSize)) return false;
				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(PixelSize.X, PixelSize.Y, Pixels, Png);
				const FString ImagePath = FPaths::ProjectSavedDir() / TEXT("Screenshots/InventoryQuickbar/RemoteClientSlot2.png");
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(ImagePath), true);
				if (!Test->TestTrue(TEXT("owning-client quickbar pixels are saved after local second-slot selection"), FFileHelper::SaveArrayToFile(Png, *ImagePath))) return true;
				ClientController->RequestSelectQuickbarSlotFromInput(2);
				ClientController->RequestSelectQuickbarSlotFromInput(1);
				if (!Test->TestEqual(TEXT("rapid 1->2->1 ends on the final local quickbar slot"), ClientController->GetSelectedQuickbarSlotIndex(), 1)) return true;
				if (!Test->TestTrue(TEXT("wheel-cycle path is local and accepts empty slots"), ClientController->RequestCycleQuickbarSlotFromInput(1))) return true;
				if (!Test->TestEqual(TEXT("wheel-cycle moves to next quickbar slot including empty slot"), ClientController->GetSelectedQuickbarSlotIndex(), 2)) return true;
				if (!Test->TestNotNull(TEXT("quickbar remains attached after rapid local selection"), Quickbar)) return true;
				if (!Test->TestTrue(TEXT("wheel-selected slot has the only visible quickbar outer ring"), Quickbar->IsDisplayedSlotSelected(2)
					&& !Quickbar->IsDisplayedSlotSelected(0) && !Quickbar->IsDisplayedSlotSelected(1))) return true;
				if (!Test->TestNotNull(TEXT("owning client UI subsystem is still available before opening backpack"), UI)) return true;
				if (!UI->IsInventoryOpen()) UI->ToggleInventory();
				if (!Test->TestTrue(TEXT("formal backpack page opens while the independent quickbar remains attached"), UI->IsInventoryOpen())) return true;
				Stage = 3; return false;
			}
			if (Stage == 3)
			{
				UCatLocalPlayerUISubsystem* UI = ClientController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
				UCatInventoryQuickbarWidget* Quickbar = UI ? UI->GetInventoryQuickbarWidget() : nullptr;
				UCatInventoryWidget* BackpackPage = FindClientBackpackPage();
				if (!BackpackPage) return false;
				if (!VerifyBackpackPageUsesPlainSlots(BackpackPage)) return true;
				if (!SaveClientViewportScreenshot(TEXT("BackpackAndQuickbarSlot3.png"))) return true;
				UCatInventorySlotWidget* FourthBackpackSlot = FindBackpackPageSlot(BackpackPage, 3);
				if (!Test->TestNotNull(TEXT("formal backpack page exposes the fourth real slot widget"), FourthBackpackSlot)) return true;
				FourthBackpackSlot->OnSlotSelected.Broadcast(FourthBackpackSlot->GetSlotIndex());
				if (!Test->TestEqual(TEXT("formal backpack slot click route does not change controller quickbar selection"), ClientController->GetSelectedQuickbarSlotIndex(), 2)) return true;
				BackpackPage->RequestSelectSlot(3);
				if (!Test->TestEqual(TEXT("formal backpack page direct selection guard does not change controller quickbar selection"), ClientController->GetSelectedQuickbarSlotIndex(), 2)) return true;
				if (!Test->TestNotNull(TEXT("quickbar remains attached beside the open backpack page"), Quickbar)) return true;
				return Test->TestTrue(TEXT("backpack page route leaves the quickbar outer ring on the wheel-selected slot"), Quickbar->IsDisplayedSlotSelected(2)
					&& !Quickbar->IsDisplayedSlotSelected(0) && !Quickbar->IsDisplayedSlotSelected(1) && !Quickbar->IsDisplayedSlotSelected(3));
			}
			Test->AddError(TEXT("Quickbar network test reached an unknown stage.")); return true;
		}
	private:
		/** 查找 owning-client 当前入视口的正式背包页；只接受显示同一随身背包的页面，避免命中编辑器预览对象或外部容器窗口。 */
		UCatInventoryWidget* FindClientBackpackPage() const
		{
			for (TObjectIterator<UCatInventoryWidget> It; It; ++It)
			{
				UCatInventoryWidget* Widget = *It;
				if (Widget && Widget->GetOwningPlayer() == ClientController.Get() && Widget->IsInViewport()
					&& Widget->GetInventoryContext() == ClientBackpack.Get())
				{
					return Widget;
				}
			}
			return nullptr;
		}

		/** 保存 owning-client 当前游戏视口像素；它包含正式背包页和常驻快捷栏，避免全局截图被 host PIE 窗口消费。 */
		bool SaveClientViewportScreenshot(const TCHAR* LeafName) const
		{
			UGameViewportClient* Viewport = ClientController.IsValid() && ClientController->GetLocalPlayer()
				? ClientController->GetLocalPlayer()->ViewportClient : nullptr;
			TSharedPtr<SViewport> SlateViewport = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
			TArray<FColor> Pixels;
			FIntVector Size = FIntVector::ZeroValue;
			if (!Test->TestTrue(TEXT("owning-client viewport screenshot includes formal backpack and quickbar"), SlateViewport.IsValid()
				&& FSlateApplication::Get().TakeScreenshot(SlateViewport.ToSharedRef(), Pixels, Size)
				&& Size.X > 0 && Size.Y > 0)) return false;
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
			const FString ImagePath = FPaths::ProjectSavedDir() / TEXT("Screenshots/InventoryQuickbar") / LeafName;
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ImagePath), true);
			return Test->TestTrue(TEXT("save owning-client backpack and quickbar viewport screenshot"), FFileHelper::SaveArrayToFile(Png, *ImagePath));
		}

		/** 从正式背包页 WidgetTree 读取指定背包格；只匹配同一随身背包和真实槽位，用于触发格子公开选择委托。 */
		UCatInventorySlotWidget* FindBackpackPageSlot(UCatInventoryWidget* BackpackPage, int32 SlotIndex) const
		{
			if (!BackpackPage || !BackpackPage->WidgetTree) return nullptr;
			TArray<UWidget*> Widgets;
			BackpackPage->WidgetTree->GetAllWidgets(Widgets);
			for (UWidget* Widget : Widgets)
			{
				UCatInventorySlotWidget* Slot = Cast<UCatInventorySlotWidget>(Widget);
				if (Slot && Slot->GetSourceInventory() == ClientBackpack.Get() && Slot->GetSlotIndex() == SlotIndex)
				{
					return Slot;
				}
			}
			return nullptr;
		}

		/** 核对正式背包页格子使用普通背包 WBP；数字提示和选中外圈只能存在于独立快捷栏格子。 */
		bool VerifyBackpackPageUsesPlainSlots(UCatInventoryWidget* BackpackPage) const
		{
			if (!BackpackPage || !BackpackPage->WidgetTree) return false;
			TArray<UWidget*> Widgets;
			BackpackPage->WidgetTree->GetAllWidgets(Widgets);
			int32 BackpackSlotCount = 0;
			for (UWidget* Widget : Widgets)
			{
				UCatInventorySlotWidget* Slot = Cast<UCatInventorySlotWidget>(Widget);
				if (!Slot || Slot->GetSourceInventory() != ClientBackpack.Get()) continue;
				++BackpackSlotCount;
				if (Slot->GetWidgetFromName(TEXT("SelectedBorder")) || Slot->GetWidgetFromName(TEXT("SlotKeyTextBlock")))
				{
					Test->AddError(TEXT("formal backpack slot WBP must not expose quickbar number text or selected border widgets"));
					return false;
				}
				if (!Test->TestFalse(TEXT("formal backpack slot does not render quickbar selection outer ring"), Slot->IsSelectedFromModel())) return false;
			}
			return Test->TestEqual(TEXT("formal backpack page still displays the same four backpack slots"), BackpackSlotCount, ClientModel->GetInventoryList().Num());
		}

		/** 从 PIE World 集合定位唯一远端客户端与其猫背包；所有前置未就绪时返回 false 等待复制，错误端数立即报告。 */
		bool PrepareEndpoints()
		{
			int32 Clients = 0;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_Client) { ClientWorld = World; ++Clients; }
			}
			if (Clients == 0 || !ClientWorld.IsValid()) return false;
			if (!Test->TestEqual(TEXT("formal quickbar regression has exactly one remote client"), Clients, 1)) return true;
			ClientController = Cast<ACatfishingPlayerController>(ClientWorld->GetFirstPlayerController());
			ACatCharacter* ClientCat = ClientController.IsValid() ? Cast<ACatCharacter>(ClientController->GetPawn()) : nullptr;
			ClientBackpack = ClientCat ? Cast<UCatBackPackComponent>(ClientCat->GetInventoryComponent()) : nullptr;
			if (!ClientController.IsValid() || !ClientController->IsLocalController() || !ClientBackpack.IsValid()) return false;
			ClientModel = ClientBackpack->GetInventoryModel();
			if (!ClientModel.IsValid() || ClientModel->GetInventoryList().Num() != 4) return false;
			Stage = 1; return false;
		}
		/** Automation 框架的断言出口；本状态机不拥有 PIE world、角色或库存对象。 */
		FAutomationTestBase* Test = nullptr;
		/** 已进入 PIE 后的单调秒基准；超时日志用它区分地图加载与运行期复制卡住。 */
		double StartedAt = 0.0;
		/** 当前状态机阶段；只由成功的阶段断言递增。 */
		int32 Stage = 0;
		/** 唯一远端 PIE World；阶段零定位，后续只从这里取拥有客户端。 */
		TWeakObjectPtr<UWorld> ClientWorld;
		/** 唯一远端本地 Controller；它承接正式数字键/滚轮等输入转发 API。 */
		TWeakObjectPtr<ACatfishingPlayerController> ClientController;
		/** 远端猫的唯一随身背包；容量和列表 Model 从这里读取，快捷栏选择只读 Controller。 */
		TWeakObjectPtr<UCatBackPackComponent> ClientBackpack;
		/** 背包持有的唯一只读库存 Model；用来证明快捷栏不是第二份列表，选择状态不再写入 Model。 */
		TWeakObjectPtr<UCatInventoryModel> ClientModel;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryQuickbarFormalNetworkTest,
	"Catfishing.Editor.Inventory.Quickbar.FormalTwoEndpointLocalSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 配置正式 TestMap 的双端 IP PIE，运行本地选择回归后按固定顺序结束 PIE 并恢复用户设置。 */
bool FCatInventoryQuickbarFormalNetworkTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestTrue(TEXT("quickbar formal network test requires idle editor"), GEditor && GEngine && !GEditor->PlayWorld)) return false;
	const TSharedRef<CatInventoryQuickbarNetwork::FRestoreSettings> Restore = MakeShared<CatInventoryQuickbarNetwork::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer); Settings->SetPlayNumberOfClients(2); Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver")) { Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver"); Driver.DriverClassNameFallback = Driver.DriverClassName; }
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatInventoryQuickbarNetwork::FVerifyQuickbar>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
