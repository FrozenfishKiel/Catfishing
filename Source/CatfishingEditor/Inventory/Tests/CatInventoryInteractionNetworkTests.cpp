#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/WrapBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Camp/CatCampInventoryActor.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatInventoryModel.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "Input/Events.h"
#include "Input/DragAndDrop.h"
#include "Slate/SObjectWidget.h"
#include "Slate/UMGDragDropOp.h"
#include "Widgets/SWidget.h"

namespace CatInventoryInteractionNetwork
{
	/** PIE 网络设置的恢复命令；它保存编辑器共享的运行设置，并只在本测试 PIE 完全停止后还原。 */
	class FRestoreSettings final : public IAutomationLatentCommand
	{
	public:
		/** 记录本测试启动前的网络模式、客户端数量、进程选择和驱动定义，供收尾命令恢复。 */
		FRestoreSettings()
		{
			const ULevelEditorPlaySettings* Settings = GetDefault<ULevelEditorPlaySettings>();
			Settings->GetPlayNetMode(NetMode);
			Settings->GetPlayNumberOfClients(PlayNumberOfClients);
			Settings->GetRunUnderOneProcess(bRunUnderOneProcess);
			NetDriverDefinitions = GEngine->NetDriverDefinitions;
		}

		/** 等待 PIE 消失后写回保存的编辑器设置，避免结束流程反向覆盖恢复结果。 */
		bool Update() override
		{
			if (GEditor->PlayWorld != nullptr)
			{
				return false;
			}
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(NetMode);
			Settings->SetPlayNumberOfClients(PlayNumberOfClients);
			Settings->SetRunUnderOneProcess(bRunUnderOneProcess);
			GEngine->NetDriverDefinitions = NetDriverDefinitions;
			return true;
		}

	private:
		/** 测试前的 PIE 网络模式；恢复时写回默认设置。 */
		EPlayNetMode NetMode = PIE_Standalone;
		/** 测试前参与 PIE 的客户端总数；本用例临时设为三端。 */
		int32 PlayNumberOfClients = 1;
		/** 测试前的单进程开关；恢复后不影响其他自动化用例。 */
		bool bRunUnderOneProcess = true;
		/** 测试前的网络驱动定义；临时 IP 驱动结束后整体还原。 */
		TArray<FNetDriverDefinition> NetDriverDefinitions;
	};

	/** 正式 TestMap 的三端库存交互状态机；它只通过正式 WBP 槽位的 Slate Drop 入口提交 RPC，并从组件独立 Model 观察复制结果。 */
	class FVerifyFormalWidgetDrops final : public IAutomationLatentCommand
	{
	public:
		/** 保存 Automation 断言目标；超时基准延后到已启动 PIE 的首次轮询写入，命令不拥有测试对象。 */
		explicit FVerifyFormalWidgetDrops(FAutomationTestBase* InTest)
			: Test(InTest)
		{
		}

		/** 按阶段执行正式 UI 交互：
		 * 1. 等待 TestMap 的正式 GameMode、三端 Pawn 和复制营地库存可用。
		 * 2. 在服务器给第一位客户端背包放入 BugBait，再由该客户端正式营地 WBP 的 Slot Drop 把它拖入营地。
		 * 3. 确认第二位客户端的营地 Model 已看到变更，再由该客户端自己的正式 WBP 把物品拖到另一营地格。
		 * 4. 每次只等待服务器权威库存和两个客户端独立 Model 收敛；超时或依赖缺失记录失败。 */
		bool Update() override
		{
			// 计时从 TestMap 已加载、PIE 已启动后的首次轮询开始，避免编辑器资源加载占用交互链路的超时预算。
			if (StartedAtSeconds <= 0.0)
			{
				StartedAtSeconds = FPlatformTime::Seconds();
			}
			if (FPlatformTime::Seconds() - StartedAtSeconds > TimeoutSeconds)
			{
				Test->AddError(FString::Printf(TEXT("Formal inventory WBP network interaction timeout at stage %d"), Stage));
				return true;
			}

			if (Stage == 0)
			{
				return PrepareFormalThreeEndpointWorld();
			}
			if (Stage == 1)
			{
				if (!AreModelsReady()) return false;
				return OpenFormalCampWidgetsAndDropFromFirstClient();
			}
			if (Stage == 2)
			{
				if (!DoesCampContain(TEXT("BugBait"), 0) || !DoesBackpackContain(0, NAME_None)
					|| !DoesNestedBackpackWidgetMatch(0, FirstBackpackSourceSlot, NAME_None)) return false;
				return DropCampItemFromSecondClient();
			}
			if (Stage == 3)
			{
				if (!DoesCampContain(NAME_None, 0) || !DoesCampContain(TEXT("BugBait"), 1)) return false;
				if (!DoesNestedBackpackWidgetMatch(0, FirstBackpackSourceSlot, NAME_None)) return false;
				Test->AddInfo(TEXT("Event=formal_inventory_wbp_multiclient_drop Result=TwoRemoteClientsSubmittedActualSlotDropsAndObservedIndependentModels"));
				return true;
			}
			Test->AddError(TEXT("Formal inventory WBP interaction reached an unknown stage."));
			return true;
		}

	private:
		/** 收集正式 TestMap 的 listen server 和两个远端客户端，并建立各端营地、Pawn 和组件 Model 的对应关系。 */
		bool PrepareFormalThreeEndpointWorld()
		{
			UWorld* ServerWorld = nullptr;
			TArray<UWorld*> ClientWorlds;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE) continue;
				if (World->GetNetMode() == NM_ListenServer) ServerWorld = World;
				else if (World->GetNetMode() == NM_Client) ClientWorlds.Add(World);
			}
			if (!ServerWorld || ClientWorlds.Num() < RequiredRemoteClientCount) return false;
			if (ClientWorlds.Num() != RequiredRemoteClientCount)
			{
				Test->AddError(FString::Printf(TEXT("Expected two remote clients, got %d"), ClientWorlds.Num()));
				return true;
			}

			TArray<ACatfishingPlayerController*> ClientControllers;
			for (UWorld* ClientWorld : ClientWorlds)
			{
				ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(ClientWorld->GetFirstPlayerController());
				ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
				if (!Controller || !Character || !Character->GetInventoryComponent()) return false;
				ClientControllers.Add(Controller);
			}
			// 客户端 Pawn 可早于 PlayerState 到达；两端权威对应物就绪前继续等待，不把合法复制时序判成业务失败。
			for (ACatfishingPlayerController* ClientController : ClientControllers)
			{
				ACatfishingPlayerController* ServerController = FindServerControllerForClient(ServerWorld, *ClientController);
				if (!ServerController || !Cast<ACatCharacter>(ServerController->GetPawn())) return false;
			}

			if (!ServerCamp.IsValid())
			{
				ACatfishingPlayerController* FirstServerController = FindServerControllerForClient(ServerWorld, *ClientControllers[0]);
				ACatCharacter* FirstServerCharacter = FirstServerController ? Cast<ACatCharacter>(FirstServerController->GetPawn()) : nullptr;
				if (!FirstServerCharacter || !FirstServerCharacter->GetInventoryComponent()) return false;
				ACatCampInventoryActor* SpawnedCamp = ServerWorld->SpawnActor<ACatCampInventoryActor>(FirstServerCharacter->GetActorLocation(), FRotator::ZeroRotator);
				if (!Test->TestNotNull(TEXT("formal TestMap server spawns replicated camp"), SpawnedCamp)) return true;
				SpawnedCamp->bAlwaysRelevant = true;
				SpawnedCamp->ForceNetUpdate();
				ServerCamp = SpawnedCamp;
				ServerCampName = SpawnedCamp->GetFName();
				ServerFirstCharacter = FirstServerCharacter;
				ACatfishingPlayerController* SecondServerController = FindServerControllerForClient(ServerWorld, *ClientControllers[1]);
				ACatCharacter* SecondServerCharacter = SecondServerController ? Cast<ACatCharacter>(SecondServerController->GetPawn()) : nullptr;
				if (!SecondServerCharacter)
				{
					Test->AddError(TEXT("formal TestMap cannot resolve the second remote client's authority character"));
					return true;
				}
				// 第二位玩家也必须进入营地正式交互半径，才能让其后的 UI RPC 通过服务器已有距离校验，而不是靠测试绕过权限。
				SecondServerCharacter->SetActorLocation(FirstServerCharacter->GetActorLocation() + FVector(50.0, 0.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
				SecondServerCharacter->ForceNetUpdate();
			}

			for (int32 Index = 0; Index < RequiredRemoteClientCount; ++Index)
			{
				ACatCampInventoryActor* ClientCamp = FindCampInWorld(ClientWorlds[Index], ServerCampName);
				ACatCharacter* ClientCharacter = Cast<ACatCharacter>(ClientControllers[Index]->GetPawn());
				if (!ClientCamp || !ClientCharacter) return false;
				ClientCamps.AddUnique(ClientCamp);
				ClientCharacters.AddUnique(ClientCharacter);
				ClientControllersByIndex.AddUnique(ClientControllers[Index]);
				ClientCampModels.AddUnique(ClientCamp->GetInventoryComponent()->GetInventoryModel());
			}
			if (ClientCamps.Num() != 2 || ClientCharacters.Num() != 2 || ClientCampModels.Num() != 2) return false;

			UCatInventoryItemDefinition* BugBait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(TEXT("BugBait"));
			if (!Test->TestNotNull(TEXT("formal BugBait definition exists"), BugBait)
				|| !Test->TestTrue(TEXT("server seeds first remote player backpack"), ServerFirstCharacter->GetInventoryComponent()->AddItemDefinition(BugBait, 1))) return true;
			ServerCamp->ForceNetUpdate();
			Stage = 1;
			return false;
		}

		/** 打开两个远端的正式营地 WBP，确认其显示上下文是各自复制组件，然后从第一端背包向营地空格提交实际 Slate Drop。 */
		bool OpenFormalCampWidgetsAndDropFromFirstClient()
		{
			for (int32 Index = 0; Index < RequiredRemoteClientCount; ++Index)
			{
				UCatLocalPlayerUISubsystem* UI = ClientControllersByIndex[Index]->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
				UClass* CampWidgetClass = LoadClass<UCatInventoryWidget>(nullptr, TEXT("/Game/UI/Inventory/WBP_CatCampInventory.WBP_CatCampInventory_C"));
				if (!UI || !CampWidgetClass || !UI->OpenInventory(ClientCamps[Index]->GetInventoryComponent(), CampWidgetClass)) return false;
			}
			if (!HasFormalCampWidgetFor(0) || !HasFormalCampWidgetFor(1)) return false;
			const int32 SourceSlot = FindOccupiedSlot(ClientCharacters[0]->GetInventoryComponent()->GetInventoryModel()->GetInventoryList(), TEXT("BugBait"));
			UCatInventorySlotWidget* TargetSlot = FindWidgetSlot(ClientControllersByIndex[0].Get(), ClientCamps[0]->GetInventoryComponent(), 0);
			if (SourceSlot == INDEX_NONE || !TargetSlot) return false;
			if (!DoesWidgetEntryMatch(TargetSlot, NAME_None) || !DoesNestedBackpackWidgetMatch(0, SourceSlot, TEXT("BugBait"))) return false;
			FirstBackpackSourceSlot = SourceSlot;
			SubmitActualWidgetDrop(TargetSlot, ClientCharacters[0]->GetInventoryComponent(), SourceSlot);
			Stage = 2;
			return false;
		}

		/** 在第二远端客户端的正式营地 WBP 找到空目标格，并把其看到的营地 BugBait 拖到该格，证明它可独立提交下一次操作。 */
		bool DropCampItemFromSecondClient()
		{
			UCatInventoryComponent* CampInventory = ClientCamps[1]->GetInventoryComponent();
			const int32 SourceSlot = FindOccupiedSlot(CampInventory->GetInventoryModel()->GetInventoryList(), TEXT("BugBait"));
			UCatInventorySlotWidget* TargetSlot = FindWidgetSlot(ClientControllersByIndex[1].Get(), CampInventory, 1);
			if (SourceSlot == INDEX_NONE || !TargetSlot) return false;
			if (!DoesWidgetEntryMatch(FindWidgetSlot(ClientControllersByIndex[1].Get(), CampInventory, SourceSlot), TEXT("BugBait"))
				|| !DoesWidgetEntryMatch(TargetSlot, NAME_None)) return false;
			SubmitActualWidgetDrop(TargetSlot, CampInventory, SourceSlot);
			Stage = 3;
			return false;
		}

		/** 比较服务器和两个远端组件 Model 在指定营地槽位的物品定义；空名称要求三个位置均为空。 */
		bool DoesCampContain(const FName ExpectedDefinition, const int32 SlotIndex) const
		{
			if (!DoesEntryMatch(ServerCamp->GetInventoryComponent()->GetInventoryEntries(), SlotIndex, ExpectedDefinition)) return false;
			for (int32 Index = 0; Index < RequiredRemoteClientCount; ++Index)
			{
				const UCatInventoryModel* Model = ClientCampModels[Index].Get();
				if (!Model || !DoesEntryMatch(Model->GetInventoryList(), SlotIndex, ExpectedDefinition)) return false;
				if (!DoesWidgetEntryMatch(FindWidgetSlot(ClientControllersByIndex[Index].Get(),
					ClientCamps[Index]->GetInventoryComponent(), SlotIndex), ExpectedDefinition)) return false;
			}
			return true;
		}

		/** 比较第一远端玩家背包的服务器权威槽位和客户端组件 Model；用于确认第一次拖放已经从玩家侧移出物品。 */
		bool DoesBackpackContain(const int32 ClientIndex, const FName ExpectedDefinition) const
		{
			if (!ServerFirstCharacter.IsValid() || FirstBackpackSourceSlot == INDEX_NONE
				|| !DoesEntryMatch(ServerFirstCharacter->GetInventoryComponent()->GetInventoryEntries(), FirstBackpackSourceSlot, ExpectedDefinition)) return false;
			const UCatInventoryModel* Model = ClientCharacters[ClientIndex]->GetInventoryComponent()->GetInventoryModel();
			return Model && DoesEntryMatch(Model->GetInventoryList(), FirstBackpackSourceSlot, ExpectedDefinition);
		}

		/** 验证两个客户端的组件 Model 已与其正式组件列表同长度，避免以初始空 Model 判定交互前状态。 */
		bool AreModelsReady() const
		{
			for (int32 Index = 0; Index < RequiredRemoteClientCount; ++Index)
			{
				if (!ClientCampModels[Index].IsValid() || ClientCampModels[Index]->GetInventoryList().Num() != ClientCamps[Index]->GetInventoryComponent()->GetInventoryEntries().Num()) return false;
			}
			return true;
		}

		/** 通过正式 Slot WBP 的 Slate 事件进入 NativeOnDrop：先把 UObject 载荷桥接成引擎 FUMGDragDropOp，再由真实 SObjectWidget 转交给原生 Drop。 */
		static void SubmitActualWidgetDrop(UCatInventorySlotWidget* TargetSlot, UCatInventoryComponent* SourceInventory, const int32 SourceSlotIndex)
		{
			UCatInventoryDragDropOperation* Operation = NewObject<UCatInventoryDragDropOperation>(TargetSlot);
			Operation->SourceInventory = SourceInventory;
			Operation->SourceSlotIndex = SourceSlotIndex;
			const FPointerEvent PointerEvent(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
			const TSharedRef<SObjectWidget> SlateWidget = StaticCastSharedRef<SObjectWidget>(TargetSlot->TakeWidget());
			const TSharedRef<FUMGDragDropOp> UMGOperation = FUMGDragDropOp::New(Operation, 0, FVector2D::ZeroVector, FVector2D::ZeroVector, 1.0f, SlateWidget);
			const FDragDropEvent DragDropEvent(PointerEvent, UMGOperation);
			TargetSlot->TakeWidget()->OnDrop(TargetSlot->GetCachedGeometry(), DragDropEvent);
		}

		/** 在拥有指定库存上下文的正式库存面板 WidgetTree 中按准确下标读取格子，避免同屏嵌套背包的空格被误认为营地目标。 */
		static UCatInventorySlotWidget* FindWidgetSlot(APlayerController* Controller, UCatInventoryComponent* ExpectedInventory, const int32 SlotIndex)
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Controller, Widgets, UCatInventoryWidget::StaticClass(), false);
			for (UUserWidget* Widget : Widgets)
			{
				UCatInventoryWidget* RootView = Cast<UCatInventoryWidget>(Widget);
				if (!RootView || RootView->GetOwningPlayer() != Controller || !RootView->IsInViewport() || !RootView->WidgetTree) continue;
				TArray<UWidget*> Panels;
				RootView->WidgetTree->GetAllWidgets(Panels);
				Panels.Insert(RootView, 0);
				for (UWidget* Panel : Panels)
				{
					UCatInventoryWidget* InventoryWidget = Cast<UCatInventoryWidget>(Panel);
					if (!InventoryWidget || InventoryWidget->GetInventoryContext() != ExpectedInventory || !InventoryWidget->WidgetTree) continue;
					UWrapBox* WrapBox = Cast<UWrapBox>(InventoryWidget->WidgetTree->FindWidget(TEXT("InventorySlotWrapBox")));
					if (WrapBox) return Cast<UCatInventorySlotWidget>(WrapBox->GetChildAt(SlotIndex));
				}
			}
			return nullptr;
		}

		/** 同时核对格子条目、已写入图片控件的纹理与数量角标；空格必须清图，避免实例指针晚到后数据可读但实际图像未刷新的假绿。 */
		static bool DoesWidgetEntryMatch(const UCatInventorySlotWidget* SlotWidget, const FName ExpectedDefinition)
		{
			if (!SlotWidget || !SlotWidget->WidgetTree) return false;
			const FCatInventoryEntry& Entry = SlotWidget->GetInventoryEntry();
			const UImage* Icon = Cast<UImage>(SlotWidget->WidgetTree->FindWidget(TEXT("ThumbnailImage")));
			const UTextBlock* Quantity = Cast<UTextBlock>(SlotWidget->WidgetTree->FindWidget(TEXT("QuantityTextBlock")));
			if (!Icon || !Quantity) return false;
			if (ExpectedDefinition.IsNone())
			{
				return !Entry.Instance && Entry.StackCount == 0 && !Icon->GetBrush().GetResourceObject()
					&& Icon->GetVisibility() == ESlateVisibility::Collapsed && Quantity->GetVisibility() == ESlateVisibility::Collapsed;
			}
			if (!Entry.Instance || Entry.StackCount <= 0 || Entry.Instance->GetItemDefinitionId() != ExpectedDefinition) return false;
			const UCatInventoryItemDefinition* Definition = Entry.Instance->GetItemDefinition();
			UTexture2D* ExpectedIcon = Definition ? Definition->GetInventoryThumbnail().LoadSynchronous() : nullptr;
			return ExpectedIcon && Icon->GetBrush().GetResourceObject() == ExpectedIcon && Icon->GetVisibility() == ESlateVisibility::Visible
				&& Quantity->GetText().EqualTo(FText::AsNumber(Entry.StackCount))
				&& Quantity->GetVisibility() == (Entry.StackCount > 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}

		/** 验证正式营地 View 中嵌套背包面板的 WrapBox 已显示本地角色背包条目，证明两种库存面板没有共用营地数据源。 */
		bool DoesNestedBackpackWidgetMatch(const int32 ClientIndex, const int32 SlotIndex, const FName ExpectedDefinition) const
		{
			return DoesWidgetEntryMatch(FindWidgetSlot(ClientControllersByIndex[ClientIndex].Get(), ClientCharacters[ClientIndex]->GetInventoryComponent(), SlotIndex), ExpectedDefinition);
		}

		/** 检查正式营地 WBP 已创建且注入指定客户端营地组件，而不是测试直接创建的替代 View。 */
		bool HasFormalCampWidgetFor(const int32 ClientIndex) const
		{
			TArray<UUserWidget*> Widgets;
			APlayerController* Controller = ClientControllersByIndex[ClientIndex].Get();
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Controller, Widgets, UCatInventoryWidget::StaticClass(), false);
			for (UUserWidget* Widget : Widgets)
			{
				const UCatInventoryWidget* InventoryWidget = Cast<UCatInventoryWidget>(Widget);
				if (InventoryWidget && InventoryWidget->GetOwningPlayer() == Controller && InventoryWidget->GetInventoryContext() == ClientCamps[ClientIndex]->GetInventoryComponent()) return true;
			}
			return false;
		}

		/** 在同一库存列表里寻找指定定义的占用格；找不到返回 INDEX_NONE，调用方继续等待复制或判定失败。 */
		static int32 FindOccupiedSlot(const TArray<FCatInventoryEntry>& Entries, const FName DefinitionId)
		{
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				if (Entries[Index].Instance && Entries[Index].StackCount > 0 && Entries[Index].Instance->GetItemDefinitionId() == DefinitionId) return Index;
			}
			return INDEX_NONE;
		}

		/** 比较权威列表或客户端 Model 在指定格的最终物品身份；空期望用于证明服务器移动或清空已经传播，而不是只看见旧 UI 残留。 */
		static bool DoesEntryMatch(const TArray<FCatInventoryEntry>& Entries, const int32 SlotIndex, const FName ExpectedDefinition)
		{
			if (!Entries.IsValidIndex(SlotIndex)) return false;
			const FCatInventoryEntry& Entry = Entries[SlotIndex];
			if (ExpectedDefinition.IsNone()) return Entry.Instance == nullptr || Entry.StackCount <= 0;
			return Entry.Instance && Entry.StackCount > 0 && Entry.Instance->GetItemDefinitionId() == ExpectedDefinition;
		}

		/** 从服务器 Controller 集合中按 PlayerState Id 找到某远端客户端的权威对应物，避免依赖迭代次序。 */
		static ACatfishingPlayerController* FindServerControllerForClient(UWorld* ServerWorld, const ACatfishingPlayerController& ClientController)
		{
			const APlayerState* ClientState = ClientController.GetPlayerState<APlayerState>();
			for (TActorIterator<ACatfishingPlayerController> It(ServerWorld); It; ++It)
			{
				const APlayerState* ServerState = It->GetPlayerState<APlayerState>();
				if (ClientState && ServerState && ServerState->GetPlayerId() == ClientState->GetPlayerId()) return *It;
			}
			return nullptr;
		}

		/** 按服务器创建 Actor 的稳定对象名在一个 PIE world 中找到其复制副本；正式 TestMap 原有营地不会被误选。 */
		static ACatCampInventoryActor* FindCampInWorld(UWorld* World, const FName ExpectedActorName)
		{
			for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
			{
				if (It->GetFName() == ExpectedActorName) return *It;
			}
			return nullptr;
		}

		/** Automation 断言目标；命令只在 PIE 存活期向它记录结果。 */
		FAutomationTestBase* Test = nullptr;
		/** 超时基准秒数；首次进入已启动 PIE 的 Update 才写入，避免 TestMap 加载时间侵占交互链路预算。 */
		double StartedAtSeconds = 0.0;
		/** 当前交互阶段；只有上一阶段的正式组件与 Model 都收敛后才推进。 */
		int32 Stage = 0;
		/** 第一位客户端本次从背包拖出的原始格位；第一次服务器 Add 后读取，后续清空断言沿用同一位置而不假定容量或初始空格。 */
		int32 FirstBackpackSourceSlot = INDEX_NONE;
		/** 本用例要求的远端客户端数量；加 listen server 共三端。 */
		static constexpr int32 RequiredRemoteClientCount = 2;
		/** 每个完整 UI 网络流程允许的最长秒数。 */
		static constexpr double TimeoutSeconds = 60.0;
		/** 服务器权威营地；两次移动的最终事实由它保存。 */
		TWeakObjectPtr<ACatCampInventoryActor> ServerCamp;
		/** 服务器创建营地的对象名；客户端只按它查找复制副本，避免 TestMap 预摆营地混入断言。 */
		FName ServerCampName;
		/** 第一客户端在服务器的权威 Character；第一次拖放后用于验证背包已清空。 */
		TWeakObjectPtr<ACatCharacter> ServerFirstCharacter;
		/** 两个客户端上复制到本地的营地 Actor；WBP 和 Model 各自绑定这一份组件。 */
		TArray<TWeakObjectPtr<ACatCampInventoryActor>> ClientCamps;
		/** 两个客户端各自拥有的角色；第一端提供第一次拖放的背包源。 */
		TArray<TWeakObjectPtr<ACatCharacter>> ClientCharacters;
		/** 两个远端 owning Controller；实际 Slot Drop 用它们发 Server RPC。 */
		TArray<TWeakObjectPtr<ACatfishingPlayerController>> ClientControllersByIndex;
		/** 两个营地组件独立持有的 Model；它们是客户端观察复制的唯一 UI 数据源。 */
		TArray<TWeakObjectPtr<UCatInventoryModel>> ClientCampModels;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatInventoryFormalWidgetMulticlientInteractionTest,
	"Catfishing.Editor.Inventory.FormalWidgetMulticlientSlotDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** 加载正式 TestMap，配置三端 IP PIE，并排入真实 WBP 交互和恢复命令；返回值只表示启动前置条件已建立。 */
bool FCatInventoryFormalWidgetMulticlientInteractionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("requires idle editor"), GEditor != nullptr && GEditor->PlayWorld == nullptr)) return false;
	const TSharedRef<CatInventoryInteractionNetwork::FRestoreSettings> Restore = MakeShared<CatInventoryInteractionNetwork::FRestoreSettings>();
	ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(PIE_ListenServer);
	Settings->SetPlayNumberOfClients(3);
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
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<CatInventoryInteractionNetwork::FVerifyFormalWidgetDrops>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(Restore);
	return true;
}

#endif
