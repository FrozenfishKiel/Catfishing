#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "Blueprint/WidgetTree.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Slate/SObjectWidget.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryPageController.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/ItemTooltip/CatItemTooltipWidget.h"

namespace CatItemTooltipNetwork
{
	/** 保存本回归改写的编辑器联机启动设置；PIE 完全退出后恢复，避免影响后续编辑器用例。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 记录启动前设置和网络驱动定义，供结束命令在同一编辑器会话中还原。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode);
			Settings->GetPlayNumberOfClients(ClientCount);
			Settings->GetRunUnderOneProcess(bOneProcess);
			NetDrivers = GEngine->NetDriverDefinitions;
			CursorPosition = FSlateApplication::Get().GetCursorPos();
		}

		/** 等到 PIE 已销毁再写回设置，防止 PIE 收尾把本测试恢复的数据重新覆盖。 */
		bool Update() override
		{
			if (GEditor->PlayWorld) return false;
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode);
			Settings->SetPlayNumberOfClients(ClientCount);
			Settings->SetRunUnderOneProcess(bOneProcess);
			GEngine->NetDriverDefinitions = NetDrivers;
			FSlateApplication::Get().SetCursorPos(CursorPosition);
			return true;
		}

	private:
		/** 测试开始前的 PIE 网络模式，结束时由恢复命令写回。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 测试开始前的参与者数量，避免本用例把双端配置遗留给其他测试。 */
		int32 ClientCount = 1;
		/** 测试开始前的单进程选择，网络世界结束后恢复。 */
		bool bOneProcess = true;
		/** 测试开始前的驱动定义，临时切到 IP 驱动后整体回填。 */
		TArray<FNetDriverDefinition> NetDrivers;
		/** 测试前的桌面光标位置；实际悬停驱动临时移动它，PIE 结束后恢复。 */
		FVector2D CursorPosition = FVector2D::ZeroVector;
	};

	/** 两端正式 Tooltip 回归状态机；只经库存复制、正式 WBP 格子的 Slate 悬停和本地 Tooltip View 观察结果。 */
	class FVerifyTooltipNetwork final : public IAutomationLatentCommand
	{
	public:
		/** 保存 Automation 断言入口；测试框架拥有它，本命令只在 PIE 存活期间报告结果。 */
		explicit FVerifyTooltipNetwork(FAutomationTestBase* InTest) : Test(InTest) {}

		/** 命令结束时兜底撤销临时列表监听，避免失败或超时路径让 Model 回调访问已释放的状态机。 */
		~FVerifyTooltipNetwork()
		{
			if (ClientRodListChangedHandle.IsValid() && ClientCharacter.IsValid())
			{
				ClientCharacter->GetInventoryComponent()->GetInventoryModel()->OnInventoryListChanged.Remove(ClientRodListChangedHandle);
			}
		}

		/** 依序等待两端、写入服务器实例、等待客户端复制、观察外库存鱼和背包鱼竿，再确认关闭清理。 */
		bool Update() override
		{
			if (StartedAt <= 0.0) StartedAt = FPlatformTime::Seconds();
			if (FPlatformTime::Seconds() - StartedAt > TimeoutSeconds)
			{
				Test->AddError(FString::Printf(TEXT("Formal item-tooltip network test timed out at stage %d"), Stage));
				return true;
			}
			if (Stage == 0) return PrepareEndpointsAndSeedAuthority();
			if (Stage == 1) return OpenFormalInventories();
			if (Stage == 2 || Stage == 3) return ObserveFishOnBothLocalPlayers();
			if (Stage == 4) return SwitchToRodThenMutateAuthority();
			if (Stage == 5) return ObserveReplicatedRodThenClose();
			Test->AddError(TEXT("Formal item-tooltip network test reached an unknown stage."));
			return true;
		}

	private:
		/** 收集 TestMap 的 listen server 与唯一远端客户端，在服务器创建可复制营地并把正式数据资产实例入库。 */
		bool PrepareEndpointsAndSeedAuthority()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_ListenServer) ServerWorld = World;
				else if (World->GetNetMode() == NM_Client) ClientWorld = World;
			}
			if (!ServerWorld.IsValid() || !ClientWorld.IsValid()) return false;
			ClientController = Cast<ACatfishingPlayerController>(ClientWorld->GetFirstPlayerController());
			HostController = Cast<ACatfishingPlayerController>(ServerWorld->GetFirstPlayerController());
			ClientCharacter = ClientController.IsValid() ? Cast<ACatCharacter>(ClientController->GetPawn()) : nullptr;
			if (!ClientController.IsValid() || !HostController.IsValid() || !ClientCharacter.IsValid() || !ClientCharacter->GetInventoryComponent()) return false;
			ACatfishingPlayerController* ServerController = FindServerController(*ServerWorld, *ClientController);
			ServerCharacter = ServerController ? Cast<ACatCharacter>(ServerController->GetPawn()) : nullptr;
			if (!ServerCharacter.IsValid() || !ServerCharacter->GetInventoryComponent()) return false;

			if (!ServerCamp.IsValid())
			{
				ACatCampInventoryActor* Camp = ServerWorld->SpawnActor<ACatCampInventoryActor>(ServerCharacter->GetActorLocation(), FRotator::ZeroRotator);
				if (!Test->TestNotNull(TEXT("server creates replicated formal camp"), Camp)) return true;
				Camp->bAlwaysRelevant = true;
				Camp->ForceNetUpdate();
				ServerCamp = Camp;
				ServerCampName = Camp->GetFName();

				UCatFishDefinition* FishDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatFishDefinition>(TEXT("SilvermoonTrout"));
				UCatEquipmentDefinition* RodDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(TEXT("StarterRodT1"));
				if (!Test->TestNotNull(TEXT("formal SilvermoonTrout definition resolves"), FishDefinition)
					|| !Test->TestNotNull(TEXT("formal StarterRodT1 definition resolves"), RodDefinition)) return true;
				UCatFishInventoryItemInstance* Fish = NewObject<UCatFishInventoryItemInstance>(Camp);
				Fish->SetItemDefinition(FishDefinition);
				Fish->SetItemInstanceIdFromAuthority(FGuid::NewGuid());
				UCatEquipmentInventoryItemInstance* Rod = NewObject<UCatEquipmentInventoryItemInstance>(ServerCharacter.Get());
				Rod->SetItemDefinition(RodDefinition);
				Rod->SetItemInstanceIdFromAuthority(FGuid::NewGuid());
				if (!Test->TestTrue(TEXT("authority initializes real fish weight"), Fish->InitializeFishFromAuthority(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("TooltipNetwork"), 3.125))
					|| !Test->TestTrue(TEXT("authority adds fish to external formal inventory"), Camp->GetInventoryComponent()->AddItemInstance(Fish, 1))
					|| !Test->TestTrue(TEXT("authority adds real rod instance to backpack"), ServerCharacter->GetInventoryComponent()->AddItemInstance(Rod, 1))) return true;
				Rod->SetRodRuntimeStateFromAuthority(75.0, false);
				ServerRod = Rod;
			}

			ClientCamp = FindCamp(*ClientWorld, ServerCampName);
			if (!ClientCamp.IsValid()) return false;
			if (!FindDefinitionSlot(ServerCamp->GetInventoryComponent()->GetInventoryEntries(), TEXT("SilvermoonTrout"), ServerFishSlot)
				|| !FindDefinitionSlot(ServerCharacter->GetInventoryComponent()->GetInventoryEntries(), TEXT("StarterRodT1"), ServerRodSlot)) return false;
			if (!FindDefinitionSlot(ClientCamp->GetInventoryComponent()->GetInventoryModel()->GetInventoryList(), TEXT("SilvermoonTrout"), ClientFishSlot)
				|| !FindDefinitionSlot(ClientCharacter->GetInventoryComponent()->GetInventoryModel()->GetInventoryList(), TEXT("StarterRodT1"), ClientRodSlot)) return false;
			Stage = 1;
			return false;
		}

		/** 两端依赖都就绪后打开正式营地 WBP；成功后下一帧才读取动态格，避免同一阶段反复销毁和重建页面。 */
		bool OpenFormalInventories()
		{
			UCatLocalPlayerUISubsystem* UI = ClientController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
			UCatLocalPlayerUISubsystem* HostUI = HostController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
			UClass* CampClass = LoadClass<UCatInventoryWidget>(nullptr, TEXT("/Game/UI/Inventory/WBP_CatCampInventory.WBP_CatCampInventory_C"));
			if (!UI || !HostUI || !CampClass || !UI->OpenInventory(ClientCamp->GetInventoryComponent(), CampClass)
				|| !HostUI->OpenInventory(ServerCamp->GetInventoryComponent(), CampClass)) return false;
			Stage = 2;
			return false;
		}

		/** 让房主和客户端各自的正式外库存 Slot 进入 Slate 悬停，观察两份 LocalPlayer Tooltip 的鱼重量、位置和淡入。 */
		bool ObserveFishOnBothLocalPlayers()
		{
			FishSlot = FindWidgetSlot(*ClientController, ClientCamp->GetInventoryComponent(), ClientFishSlot);
			RodSlot = FindWidgetSlot(*ClientController, ClientCharacter->GetInventoryComponent(), ClientRodSlot);
			Tooltip = FindTooltip(*ClientController);
			HostFishSlot = FindWidgetSlot(*HostController, ServerCamp->GetInventoryComponent(), ServerFishSlot);
			HostTooltip = FindTooltip(*HostController);
			if (!FishSlot.IsValid() || !RodSlot.IsValid() || !Tooltip.IsValid() || !HostFishSlot.IsValid() || !HostTooltip.IsValid()) return false;
			const FPointerEvent Pointer(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
			// 进入事件只发一次；后续帧等待引擎正常推进淡入，不能手动 Tick 代替真实视口调度。
			if (Stage == 2)
			{
				FishSlate = FishSlot->TakeWidget();
				// 客户端窗口可能在房主窗口后面；先激活真实目标窗口，使后续系统鼠标采样仍落在这个格子。
				if (TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(FishSlate.ToSharedRef())) Window->BringToFront(true);
				FSlateApplication::Get().SetCursorPos(FishSlot->GetCachedGeometry().LocalToAbsolute(FishSlot->GetCachedGeometry().GetLocalSize() * 0.5f));
				FishSlate->OnMouseEnter(FishSlot->GetCachedGeometry(), Pointer);
				HostFishSlate = HostFishSlot->TakeWidget();
				HostFishSlate->OnMouseEnter(HostFishSlot->GetCachedGeometry(), Pointer);
				Stage = 3;
				return false;
			}
			UTextBlock* Details = Cast<UTextBlock>(Tooltip->GetWidgetFromName(TEXT("InstanceDetailsText")));
			UTextBlock* HostDetails = Cast<UTextBlock>(HostTooltip->GetWidgetFromName(TEXT("InstanceDetailsText")));
			UBorder* Root = Cast<UBorder>(Tooltip->GetWidgetFromName(TEXT("RootBorder")));
			if (!Details || !HostDetails || !Root) return false;
			if (!Details->GetText().ToString().Contains(TEXT("3.12 kg")) || !HostDetails->GetText().ToString().Contains(TEXT("3.12 kg"))
				|| Tooltip->GetVisibility() != ESlateVisibility::HitTestInvisible || Tooltip->GetRenderOpacity() < 0.99f) return false;
			const FVector2D Expected = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(ClientController.Get()).AbsoluteToLocal(
				FishSlot->GetCachedGeometry().LocalToAbsolute(FishSlot->GetCachedGeometry().GetLocalSize() * 0.5f));
			if (!Root->GetRenderTransform().Translation.Equals(Expected, 0.1f)) return false;
			if (!Test->TestTrue(TEXT("正式格子已经有实际布局尺寸"), FishSlot->GetCachedGeometry().GetLocalSize().GetMin() > 0.0)) return true;
			// 保存两端实际玩家视口，包含正式库存与 Tooltip；它补足离屏渲染不能证明实际定位和背景资源的盲区。
			IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Automation/Tooltip")), true);
			for (APlayerController* Player : { HostController.Get(), ClientController.Get() })
			{
				UGameViewportClient* Viewport = Player->GetLocalPlayer()->ViewportClient;
				TSharedPtr<SViewport> SlateViewport = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
				TArray<FColor> Pixels;
				FIntVector Size = FIntVector::ZeroValue;
				if (!Test->TestTrue(TEXT("实际玩家视口截图可读取"), SlateViewport.IsValid()
					&& FSlateApplication::Get().TakeScreenshot(SlateViewport.ToSharedRef(), Pixels, Size)
					&& Size.X > 0 && Size.Y > 0)) return true;
				TArray64<uint8> PNG;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
				const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/Tooltip")
					/ (Player == HostController.Get() ? TEXT("HostInventoryTooltip.png") : TEXT("ClientInventoryTooltip.png"));
				if (!Test->TestTrue(TEXT("保存实际玩家视口截图"), FFileHelper::SaveArrayToFile(PNG, *Path))) return true;
			}
			Stage = 4;
			return false;
		}

		/** 从同一客户端的嵌套背包 Slot 切到初始 75 耐久鱼竿，让旧鱼格 Leave 验证来源保护后才写服务器耐久。 */
		bool SwitchToRodThenMutateAuthority()
		{
			if (!Test->TestTrue(TEXT("实际背包鱼竿格已经完成视口布局"), RodSlot->GetCachedGeometry().GetLocalSize().GetMin() > 0.0)) return true;
			// 只伪造 Enter 会被下一帧真实光标位置产生的 Leave 抵消；经 Slate 路由鼠标移动，让悬停状态与真实光标一致。
			const FVector2D Center = RodSlot->GetCachedGeometry().LocalToAbsolute(RodSlot->GetCachedGeometry().GetLocalSize() * 0.5f);
			const FVector2D Previous = FSlateApplication::Get().GetCursorPos();
			const FPointerEvent Pointer(0, Center, Previous, TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
			FSlateApplication::Get().SetCursorPos(Center);
			FSlateApplication::Get().ProcessMouseMoveEvent(Pointer);
			FishSlate->OnMouseLeave(Pointer);
			UTextBlock* Details = Cast<UTextBlock>(Tooltip->GetWidgetFromName(TEXT("InstanceDetailsText")));
			if (!Details || !Details->GetText().ToString().Contains(TEXT("75")) || Tooltip->GetVisibility() != ESlateVisibility::HitTestInvisible)
			{
				return false;
			}
			// 耐久是实例独立复制字段；只在同一客户端列表稳定后计数，再由服务器更新该实例，不能把列表重建当成 Tooltip 刷新的前提。
			ClientRodListChanges = 0;
			ClientRodListChangedHandle = ClientCharacter->GetInventoryComponent()->GetInventoryModel()->OnInventoryListChanged.AddLambda([this]() { ++ClientRodListChanges; });
			ServerRod->SetRodRuntimeStateFromAuthority(37.25, false);
			Stage = 5;
			return false;
		}

		/** 等待同一客户端鱼竿实例复制新耐久，不重发 Enter；由活动 Tooltip 自己重读实例，并经正式关页入口立即清理。 */
		bool ObserveReplicatedRodThenClose()
		{
			const TArray<FCatInventoryEntry>& ClientEntries = ClientCharacter->GetInventoryComponent()->GetInventoryModel()->GetInventoryList();
			UCatEquipmentInventoryItemInstance* ClientRod = ClientEntries.IsValidIndex(ClientRodSlot) ? Cast<UCatEquipmentInventoryItemInstance>(ClientEntries[ClientRodSlot].Instance) : nullptr;
			if (!ClientRod || !FMath::IsNearlyEqual(ClientRod->GetRodDurability(), 37.25)) return false;
			if (!Test->TestEqual(TEXT("replicated durability update does not rebuild inventory list"), ClientRodListChanges, 0)) return true;
			UTextBlock* Details = Cast<UTextBlock>(Tooltip->GetWidgetFromName(TEXT("InstanceDetailsText")));
			if (!Details || !Details->GetText().ToString().Contains(TEXT("37.25"))) return false;
			if (!Test->TestTrue(TEXT("server rod keeps the same inventory list after durability mutation"), ServerRod.IsValid() && ServerCharacter->GetInventoryComponent()->GetInventoryEntries().IsValidIndex(ServerRodSlot))) return true;
			ClientCharacter->GetInventoryComponent()->GetInventoryModel()->OnInventoryListChanged.Remove(ClientRodListChangedHandle);
			ClientController->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>()->GetInventoryPageController()->RequestCloseInventoryFromWidget();
			if (Tooltip->GetVisibility() != ESlateVisibility::Collapsed || Tooltip->GetRenderOpacity() != 0.0f) return false;
			Test->AddInfo(TEXT("Event=formal_item_tooltip_network Result=ClientObservedReplicatedFishAndRodThroughNativeSlateHover"));
			return true;
		}

		/** 按玩家状态 ID 找到远端客户端在服务器上的权威 Controller，不依赖 Actor 枚举顺序。 */
		static ACatfishingPlayerController* FindServerController(UWorld& World, const ACatfishingPlayerController& Client)
		{
			const APlayerState* ClientState = Client.GetPlayerState<APlayerState>();
			for (TActorIterator<ACatfishingPlayerController> It(&World); It; ++It)
			{
				const APlayerState* State = It->GetPlayerState<APlayerState>();
				if (ClientState && State && State->GetPlayerId() == ClientState->GetPlayerId()) return *It;
			}
			return nullptr;
		}

		/** 用服务器创建的稳定对象名定位客户端复制营地，避免 TestMap 原有交互物混入断言。 */
		static ACatCampInventoryActor* FindCamp(UWorld& World, const FName Name)
		{
			for (TActorIterator<ACatCampInventoryActor> It(&World); It; ++It) if (It->GetFName() == Name) return *It;
			return nullptr;
		}

		/** 在复制数组或 Model 数组中查找指定正式定义的格位；找不到说明客户端仍未完成真实入库同步。 */
		static bool FindDefinitionSlot(const TArray<FCatInventoryEntry>& Entries, const FName DefinitionId, int32& OutSlot)
		{
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				if (Entries[Index].Instance && Entries[Index].StackCount > 0 && Entries[Index].Instance->GetItemDefinitionId() == DefinitionId)
				{
					OutSlot = Index;
					return true;
				}
			}
			return false;
		}

		/** 从当前 LocalPlayer 的正式库存 WBP 树中按库存上下文和格位找目标，不创建替代白盒格子。 */
		static UCatInventorySlotWidget* FindWidgetSlot(APlayerController& Controller, UCatInventoryComponent* Inventory, const int32 SlotIndex)
		{
			TArray<UUserWidget*> Widgets;
			// PageController 还持有未入视口的默认背包；只从已入视口的根向内找，避免选到零几何的备用背包格。
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&Controller, Widgets, UCatInventoryWidget::StaticClass(), true);
			for (UUserWidget* Widget : Widgets)
			{
				UCatInventoryWidget* Root = Cast<UCatInventoryWidget>(Widget);
				if (!Root || Root->GetOwningPlayer() != &Controller || !Root->WidgetTree) continue;
				TArray<UWidget*> Panels;
				Root->WidgetTree->GetAllWidgets(Panels);
				Panels.Insert(Root, 0);
				for (UWidget* Panel : Panels)
				{
					UCatInventoryWidget* InventoryWidget = Cast<UCatInventoryWidget>(Panel);
					if (!InventoryWidget || InventoryWidget->GetInventoryContext() != Inventory) continue;
					if (UWrapBox* Slots = Cast<UWrapBox>(InventoryWidget->GetWidgetFromName(TEXT("InventorySlotWrapBox")))) return Cast<UCatInventorySlotWidget>(Slots->GetChildAt(SlotIndex));
				}
			}
			return nullptr;
		}

		/** 从同一玩家视口读取子系统已经装配的正式 Tooltip WBP，确保观察的是实际 LocalPlayer UI。 */
		static UCatItemTooltipWidget* FindTooltip(APlayerController& Controller)
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&Controller, Widgets, UCatItemTooltipWidget::StaticClass(), false);
			for (UUserWidget* Widget : Widgets) if (Widget->GetOwningPlayer() == &Controller) return Cast<UCatItemTooltipWidget>(Widget);
			return nullptr;
		}

		/** Automation 断言接收者；命令不拥有其生命周期。 */
		FAutomationTestBase* Test = nullptr;
		/** 首次轮询记录的单调计时基准，排除编辑器开始 PIE 前的资源加载耗时。 */
		double StartedAt = 0.0;
		/** 当前真实链路阶段，只在前一阶段的复制或 UI 观察成立后推进。 */
		int32 Stage = 0;
		/** 测试限定的完整双端链路最长等待时间，防止异常 PIE 挂住自动化队列。 */
		static constexpr double TimeoutSeconds = 45.0;
		/** listen server 的正式世界，服务器写入仅在这里执行。 */
		TWeakObjectPtr<UWorld> ServerWorld;
		/** 远端客户端的正式世界，所有 UI 观察只在这里执行。 */
		TWeakObjectPtr<UWorld> ClientWorld;
		/** 远端客户端拥有的 Controller，作为正式 LocalPlayer UI 和 WBP 的所有者。 */
		TWeakObjectPtr<ACatfishingPlayerController> ClientController;
		/** listen host 的本地 Controller，房主侧正式 Tooltip 也必须从它自己的外库存 Slot 进入。 */
		TWeakObjectPtr<ACatfishingPlayerController> HostController;
		/** 远端客户端的复制角色，背包 Model 和背包格由它提供。 */
		TWeakObjectPtr<ACatCharacter> ClientCharacter;
		/** 远端客户端对应的服务器权威角色，正式背包入库与耐久更新由它持有。 */
		TWeakObjectPtr<ACatCharacter> ServerCharacter;
		/** 服务器生成的公共营地，用它的稳定对象名关联客户端复制副本。 */
		TWeakObjectPtr<ACatCampInventoryActor> ServerCamp;
		/** 客户端复制到的公共营地，外库存正式 WBP 绑定它的库存组件。 */
		TWeakObjectPtr<ACatCampInventoryActor> ClientCamp;
		/** 服务器创建营地的对象名，排除 TestMap 中预摆放的其他营地。 */
		FName ServerCampName;
		/** 权威鱼竿实例，耐久单独复制后库存格列表仍应保持同一条目。 */
		TWeakObjectPtr<UCatEquipmentInventoryItemInstance> ServerRod;
		/** 服务器外库存鱼所在格位，复制前后作为稳定断言锚点。 */
		int32 ServerFishSlot = INDEX_NONE;
		/** 服务器背包鱼竿所在格位，耐久更新不触发列表重排。 */
		int32 ServerRodSlot = INDEX_NONE;
		/** 客户端外库存鱼所在格位，正式外库存 Slot 从此处取出。 */
		int32 ClientFishSlot = INDEX_NONE;
		/** 客户端背包鱼竿所在格位，嵌套背包 Slot 从此处取出。 */
		int32 ClientRodSlot = INDEX_NONE;
		/** 已实际进入 Slate 悬停的外库存鱼格，后续用旧 Leave 验证不关闭新来源。 */
		TWeakObjectPtr<UCatInventorySlotWidget> FishSlot;
		/** 房主侧正式外库存鱼格，与客户端格分开持有，防止两份 LocalPlayer UI 互相替代。 */
		TWeakObjectPtr<UCatInventorySlotWidget> HostFishSlot;
		/** 已实际进入 Slate 悬停的背包鱼竿格，切换后成为唯一 Tooltip 来源。 */
		TWeakObjectPtr<UCatInventorySlotWidget> RodSlot;
		/** LocalPlayer 子系统实际创建的正式 Tooltip WBP，测试只读取其可见性、文本和位置。 */
		TWeakObjectPtr<UCatItemTooltipWidget> Tooltip;
		/** 房主 LocalPlayer 子系统创建的正式 Tooltip WBP，只读取房主端真实鱼实例投影。 */
		TWeakObjectPtr<UCatItemTooltipWidget> HostTooltip;
		/** 鱼格对应的 Slate 控件，保留到切换后发出迟到 Leave。 */
		TSharedPtr<SWidget> FishSlate;
		/** 房主鱼格的 Slate 控件，确保房主读数同样来自 Native OnMouseEnter 而非直接 View 调用。 */
		TSharedPtr<SWidget> HostFishSlate;
		/** 客户端背包 Model 的列表通知句柄，仅在耐久复制期间监听，验证 Tooltip 不依赖列表重建。 */
		FDelegateHandle ClientRodListChangedHandle;
		/** 耐久同步窗口内收到的客户端列表重建次数；正确路径应保持零，详情由同一实例字段更新。 */
		int32 ClientRodListChanges = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatItemTooltipFormalNetworkTest,
	"Catfishing.Editor.UI.ItemTooltip.FormalTwoEndpointNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 使用正式 TestMap 和双端 IP PIE 排入 Tooltip 端到端回归；返回值只代表启动前置已建立。 */
bool FCatItemTooltipFormalNetworkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor"), GEditor && GEditor->PlayWorld == nullptr)) return false;
	const TSharedRef<CatItemTooltipNetwork::FRestoreSettings> Restore = MakeShared<CatItemTooltipNetwork::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(2);
	Settings->SetRunUnderOneProcess(true);
	for (FNetDriverDefinition& Driver : GEngine->NetDriverDefinitions)
	{
		if (Driver.DefName == TEXT("GameNetDriver"))
		{
			// 让正式双端 PIE 明确使用 IP 驱动；fallback 同步到同一类，避免本机缺少在线后端时走到另一套网络驱动。
			Driver.DriverClassName = TEXT("/Script/OnlineSubsystemUtils.IpNetDriver");
			Driver.DriverClassNameFallback = Driver.DriverClassName;
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Catfishing/Maps/TestMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatItemTooltipNetwork::FVerifyTooltipNetwork>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
