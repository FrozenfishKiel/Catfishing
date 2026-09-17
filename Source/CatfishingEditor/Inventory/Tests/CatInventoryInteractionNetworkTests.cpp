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
#include "Profile/CatProfileSettings.h"
#include "Profile/CatProfileSubsystem.h"
#include "UI/Collection/CatFishCardWidget.h"
#include "UI/Inventory/CatCampInventoryWidget.h"
#include "Data/CatFishDefinition.h"
#include "Components/Image.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "AssetCompilingManager.h"
#include "HAL/FileManager.h"
#include "UI/Inventory/CatInventoryPageController.h"
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
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Profile/CatCollectionSaveGame.h"
#include "Kismet/GameplayStatics.h"

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
			// 此用例会写捕获与追踪记录，先切换随机开发档名，隔离平时 PIE 的个人数据。
			ProfileBase = GetDefault<UCatProfileSettings>()->SaveSlotBaseName;
			GetMutableDefault<UCatProfileSettings>()->SaveSlotBaseName = TEXT("InventoryTrackingTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		}

		/** 等待 PIE 消失后恢复编辑器设置和开发档配置；保留隔离测试档便于排查，不删除原有玩家文件。 */
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
			GetMutableDefault<UCatProfileSettings>()->SaveSlotBaseName = ProfileBase;
			return true;
		}

	private:
		/** 原开发档基础名；测试写入隔离档，PIE 停止后恢复原配置，避免后续开发继续用测试档。 */
		FString ProfileBase;
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
				if (!DoesCampContain(4, 0) || !DoesBackpackContain(0, 0)
					|| !DoesNestedBackpackWidgetMatch(0, FirstBackpackSourceSlot, 0)) return false;
				return DropCampItemFromSecondClient();
			}
			if (Stage == 3)
			{
				if (!DoesCampContain(0, 0) || !DoesCampContain(4, 1)) return false;
				if (!DoesNestedBackpackWidgetMatch(0, FirstBackpackSourceSlot, 0)) return false;
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

			UCatInventoryItemDefinition* BugBait = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(4);
			if (!Test->TestNotNull(TEXT("formal BugBait definition exists"), BugBait)
				|| !Test->TestTrue(TEXT("server seeds first remote player backpack"), ServerFirstCharacter->GetInventoryComponent()->AddItemDefinition(BugBait, 1))) return true;
			ServerCamp->ForceNetUpdate();
			Stage = 1;
			return false;
		}

		/** 打开两个远端的正式营地 WBP，确认其显示上下文是各自复制组件，然后从第一端背包向营地空格提交实际 Slate Drop。 */
		bool OpenFormalCampWidgetsAndDropFromFirstClient()
		{
			if (!bTrackingChecked)
			{
				// 先验证个人背包不显示追踪卡，再沿原测试进入团队库存验证显示与关闭清理；不绕过页面控制器开关。
				auto* Player = ClientControllersByIndex[0]->GetLocalPlayer();
				auto* UI = Player->GetSubsystem<UCatLocalPlayerUISubsystem>();
				auto* Profile = Player->GetSubsystem<UCatProfileSubsystem>();
				if (!UI || !Profile || !UI->GetInventoryPageController()) return false;
				FCatProfileGrant Grant;
				Grant.GrantId = FGuid::NewGuid();
				Grant.Kind = ECatProfileGrantKind::FishRecorded;
				Grant.ItemId = 3;
				Grant.WeightKilograms = 2.5f;
				if (!Test->TestTrue(TEXT("isolated capture grant saved"), Profile->ApplyGrant(Grant).bAckAllowed)
					|| !VerifyTrackingFromFormalBook(Player)) return true;
				UI->ToggleInventory();
				Test->TestTrue(TEXT("personal inventory opened"), UI->GetInventoryPageController()->IsInventoryOpen());
				Test->TestEqual(TEXT("personal inventory does not show tracking card"), CountVisibleTrackingCards(Player), 0);
				UI->ToggleInventory();
				Test->TestEqual(TEXT("closing inventory removes tracking card"), CountVisibleTrackingCards(Player), 0);
				auto* OtherProfile = ClientControllersByIndex[1]->GetLocalPlayer()->GetSubsystem<UCatProfileSubsystem>();
				Test->TestEqual(TEXT("second PIE player does not inherit tracking"), OtherProfile ? OtherProfile->GetTrackedFish() : -1, 0);
				bTrackingChecked = true;
			}
			for (int32 Index = 0; Index < RequiredRemoteClientCount; ++Index)
			{
				UCatLocalPlayerUISubsystem* UI = ClientControllersByIndex[Index]->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
				UClass* CampWidgetClass = LoadClass<UCatInventoryWidget>(nullptr, TEXT("/Game/UI/Inventory/WBP_CatCampInventory.WBP_CatCampInventory_C"));
				if (!UI || !CampWidgetClass || !UI->OpenInventory(ClientCamps[Index]->GetInventoryComponent(), CampWidgetClass)) return false;
			}
			if (!HasFormalCampWidgetFor(0) || !HasFormalCampWidgetFor(1)) return false;
			Test->TestEqual(TEXT("team inventory shows own tracking card"), CountVisibleTrackingCards(ClientControllersByIndex[0]->GetLocalPlayer()), 1);
			Test->TestEqual(TEXT("untracked teammate has no tracking card"), CountVisibleTrackingCards(ClientControllersByIndex[1]->GetLocalPlayer()), 0);
			auto* TrackingUI = ClientControllersByIndex[0]->GetLocalPlayer()->GetSubsystem<UCatLocalPlayerUISubsystem>();
			TrackingUI->ToggleInventory();
			Test->TestEqual(TEXT("closing team inventory removes tracking card"), CountVisibleTrackingCards(ClientControllersByIndex[0]->GetLocalPlayer()), 0);
			UClass* TrackingCampClass = LoadClass<UCatInventoryWidget>(nullptr, TEXT("/Game/UI/Inventory/WBP_CatCampInventory.WBP_CatCampInventory_C"));
			if (!TrackingUI->OpenInventory(ClientCamps[0]->GetInventoryComponent(), TrackingCampClass)) return false;
			Test->TestEqual(TEXT("reopening team inventory restores tracking card"), CountVisibleTrackingCards(ClientControllersByIndex[0]->GetLocalPlayer()), 1);
			// 有 RHI 时输出刚打开的正式团队页供视觉检查；解锁与推荐是隔离夹具，原鱼资产不会保存。
			TArray<UUserWidget*> CampViews;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(ClientControllersByIndex[0].Get(), CampViews, UCatCampInventoryWidget::StaticClass(), true);
			for (auto* CampView : CampViews)
			{
				if (CampView->GetOwningPlayer() != ClientControllersByIndex[0].Get()) continue;
				auto* BaitImage = Cast<UImage>(CampView->GetWidgetFromName(TEXT("RecommendedBaitImage")));
				auto* ChumImage = Cast<UImage>(CampView->GetWidgetFromName(TEXT("RecommendedChumImage")));
				Test->TestTrue(TEXT("推荐鱼饵按总表 ID 读图"), BaitImage && BaitImage->GetBrush().GetResourceObject() == GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(4)->GetInventoryThumbnail().LoadSynchronous());
				Test->TestTrue(TEXT("推荐窝料按总表 ID 读图"), ChumImage && ChumImage->GetBrush().GetResourceObject() == GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(5)->GetInventoryThumbnail().LoadSynchronous());
				if (!FApp::CanEverRender()) continue;
				FAssetCompilingManager::Get().FinishAllCompilation();
				const FString Directory = FPaths::ProjectSavedDir() / TEXT("CampInventory");
				IFileManager::Get().MakeDirectory(*Directory, true);
				FWidgetRenderer Renderer(true);
				for (const FIntPoint Size : {FIntPoint(1536,1024), FIntPoint(1280,720)})
				{
					auto* Target = NewObject<UTextureRenderTarget2D>(CampView);
					Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
					Target->InitAutoFormat(Size.X, Size.Y);
					Target->UpdateResourceImmediate(true);
					for (int32 Frame = 0; Frame < 3; ++Frame) Renderer.DrawWidget(Target, CampView->TakeWidget(), FVector2D(Size), 1.0f / 60.0f);
					UKismetRenderingLibrary::ExportRenderTarget(CampView, Target, Directory, FString::Printf(TEXT("Camp_%dx%d.png"), Size.X, Size.Y));
				}
			}
			const int32 SourceSlot = FindOccupiedSlot(ClientCharacters[0]->GetInventoryComponent()->GetInventoryModel()->GetInventoryList(), 4);
			UCatInventorySlotWidget* TargetSlot = FindWidgetSlot(ClientControllersByIndex[0].Get(), ClientCamps[0]->GetInventoryComponent(), 0);
			if (SourceSlot == INDEX_NONE || !TargetSlot) return false;
			if (!DoesWidgetEntryMatch(TargetSlot, 0) || !DoesNestedBackpackWidgetMatch(0, SourceSlot, 4)) return false;
			FirstBackpackSourceSlot = SourceSlot;
			SubmitActualWidgetDrop(TargetSlot, ClientCharacters[0]->GetInventoryComponent(), SourceSlot);
			Stage = 2;
			return false;
		}

		/** 从实际鱼卡和追踪按钮提交；以隔离档案准备捕获事实，检查广播、持久化、失败回滚和重开显示。 */
		bool VerifyTrackingFromFormalBook(ULocalPlayer* Player)
		{
			// 仅临时配置内存中的两项推荐，作用域结束恢复，不保存正式鱼资产。
			auto* Fish = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatFishDefinition>(3);
			if (!Test->TestNotNull(TEXT("推荐测试鱼定义"), Fish)) return false;
			TGuardValue<int32> BaitGuard(Fish->RecommendedBaitItemId, 4);
			TGuardValue<int32> ChumGuard(Fish->RecommendedChumItemId, 5);
			auto* UI = Player->GetSubsystem<UCatLocalPlayerUISubsystem>();
			auto* Profile = Player->GetSubsystem<UCatProfileSubsystem>();
			FCatProfileGrant Other;
			Other.GrantId = FGuid::NewGuid(); Other.Kind = ECatProfileGrantKind::FishRecorded; Other.ItemId = 7; Other.WeightKilograms = 2.5;
			if (!Test->TestTrue(TEXT("第二条鱼准备为已捕获"), Profile->ApplyGrant(Other).bAckAllowed)) return false;
			UI->ToggleCollection();
			TArray<UUserWidget*> Views;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Player, Views, UCatCollectionWidget::StaticClass(), true);
			UCatCollectionWidget* Book = nullptr;
			for (auto* View : Views) if (View->GetOwningLocalPlayer() == Player) Book = Cast<UCatCollectionWidget>(View);
			if (!Test->TestNotNull(TEXT("正式图鉴已进入视口"), Book)) return false;
			const TSharedRef<SWidget> Root = Book->TakeWidget();
			TFunction<bool(const TSharedRef<SWidget>&, const FString&)> HasText;
			HasText = [&HasText](const TSharedRef<SWidget>& Node, const FString& Text)
			{
				if (!Node->GetVisibility().IsVisible()) return false;
				if (Node->GetTypeAsString() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Node)->GetText().ToString() == Text) return true;
				for (int32 I = 0; I < Node->GetChildren()->Num(); ++I) if (HasText(Node->GetChildren()->GetChildAt(I), Text)) return true;
				return false;
			};
			TFunction<TSharedPtr<SButton>(const TSharedRef<SWidget>&, const FString&)> FindButton;
			FindButton = [&FindButton, &HasText](const TSharedRef<SWidget>& Node, const FString& Text) -> TSharedPtr<SButton>
			{
				if ((Node->GetTypeAsString() == TEXT("SButton") || Node->GetTypeAsString() == TEXT("SCatTrackedFishButton")) && HasText(Node, Text)) return StaticCastSharedRef<SButton>(Node);
				for (int32 I = 0; I < Node->GetChildren()->Num(); ++I) if (auto Found = FindButton(Node->GetChildren()->GetChildAt(I), Text)) return Found;
				return nullptr;
			};
			// 直接调用正式 Slate 按钮的按下和抬起处理，不跳过 Widget→Controller→Model→Profile 链，也不宣称系统鼠标路由已测。
			const auto Click = [&](const FString& Label)
			{
				Root->SlatePrepass();
				auto Button = FindButton(Root, Label);
				if (!Test->TestTrue(TEXT("按钮存在：") + Label, Button.IsValid())) return;
				const FKeyEvent Key(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
				Button->OnKeyDown(Button->GetCachedGeometry(), Key);
				Button->OnKeyUp(Button->GetCachedGeometry(), Key);
				Root->SlatePrepass();
			};
			const FString First = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(3)->GetInventoryDisplayName().ToString();
			const FString Second = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(7)->GetInventoryDisplayName().ToString();
			int32 Notifications = 0;
			const auto Handle = Profile->OnFishCollectionChanged.AddLambda([&Notifications]() { ++Notifications; });
			ON_SCOPE_EXIT { Profile->OnFishCollectionChanged.Remove(Handle); };
			Click(First);
			Test->TestEqual(TEXT("普通点击不写追踪"), Profile->GetTrackedFish(), 0);
			Test->TestFalse(TEXT("普通点击不显示追踪标记"), HasText(FindButton(Root, First).ToSharedRef(), TEXT("追踪中")));
			Test->TestFalse(TEXT("普通点击松开后不常驻按下"), FindButton(Root, First)->IsPressed());
			Test->TestFalse(TEXT("鱼卡不再显示数字编号"), HasText(Root, TEXT("3")));
			Click(TEXT("追踪"));
			Test->TestEqual(TEXT("正式按钮完成追踪"), Profile->GetTrackedFish(), 3);
			Test->TestEqual(TEXT("保存成功只广播一次"), Notifications, 1);
			Test->TestTrue(TEXT("确认追踪后常驻按下"), FindButton(Root, First)->IsPressed());
			Test->TestTrue(TEXT("追踪鱼卡含明确标记"), HasText(FindButton(Root, First).ToSharedRef(), TEXT("追踪中")));
			Click(Second);
			Test->TestTrue(TEXT("点击另一鱼不清除原追踪样式"), FindButton(Root, First)->IsPressed());
			Test->TestFalse(TEXT("待操作鱼不冒充追踪"), FindButton(Root, Second)->IsPressed());
			// 测试仅将本次随机开发档临时设为只读，强制真实 SaveGame 写入失败，离开作用域即恢复文件属性。
			const FString Account = FString::Printf(TEXT("Editor_%d_%08x_PIE%d"), FMath::Max(0, Player->GetControllerId()),
				GetTypeHash(GetDefault<UCatProfileSettings>()->SaveSlotBaseName), Player->GetWorld()->GetPackage()->GetPIEInstanceID());
			const FString Slot = TEXT("CatCollection/Development/") + Account + TEXT("/Collection_v1");
			const FString File = FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".sav"));
			auto& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (!Test->TestTrue(TEXT("隔离档案存在"), PlatformFile.FileExists(*File))) return false;
			{
				const bool WasReadOnly = PlatformFile.IsReadOnly(*File);
				ON_SCOPE_EXIT { Test->TestTrue(TEXT("恢复隔离档原文件属性"), PlatformFile.SetReadOnly(*File, WasReadOnly)); };
				if (!Test->TestTrue(TEXT("模拟磁盘不可写"), PlatformFile.SetReadOnly(*File, true))) return false;
				Click(TEXT("追踪"));
				Test->TestEqual(TEXT("失败不广播成功"), Notifications, 1);
				Test->TestEqual(TEXT("失败保留原追踪"), Profile->GetTrackedFish(), 3);
				Test->TestTrue(TEXT("失败展示重试提示"), HasText(Root, TEXT("追踪未保存，请重试")));
				Test->TestTrue(TEXT("失败保留原卡样式"), FindButton(Root, First)->IsPressed());
				Click(First); Click(TEXT("取消追踪"));
				Test->TestEqual(TEXT("取消失败也不广播"), Notifications, 1);
				Test->TestEqual(TEXT("取消失败保留追踪"), Profile->GetTrackedFish(), 3);
				Test->TestTrue(TEXT("取消失败提示"), HasText(Root, TEXT("取消追踪未保存，请重试")));
			}
			Click(Second); Click(TEXT("追踪"));
			Test->TestEqual(TEXT("替换追踪成功"), Profile->GetTrackedFish(), 7);
			Test->TestFalse(TEXT("替换后旧卡不按下"), FindButton(Root, First)->IsPressed());
			Test->TestTrue(TEXT("替换后新卡按下"), FindButton(Root, Second)->IsPressed());
			Click(TEXT("下一页")); Click(TEXT("上一页"));
			Test->TestTrue(TEXT("翻页回来仍显示已保存追踪"), FindButton(Root, Second)->IsPressed());
			Click(Second); Click(TEXT("取消追踪"));
			Test->TestEqual(TEXT("取消成功清除追踪"), Profile->GetTrackedFish(), 0);
			Test->TestFalse(TEXT("取消成功清除按下外观"), FindButton(Root, Second)->IsPressed());
			Test->TestFalse(TEXT("取消成功隐藏追踪标记"), HasText(FindButton(Root, Second).ToSharedRef(), TEXT("追踪中")));
			Click(First); Click(TEXT("追踪"));
			const auto* Saved = Cast<UCatCollectionSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
			if (Test->TestNotNull(TEXT("磁盘图鉴可重读"), Saved)) Test->TestEqual(TEXT("磁盘与界面追踪一致"), Saved->TrackedItemId, 3);
			UI->ToggleCollection(); UI->ToggleCollection();
			Root->SlatePrepass();
			Test->TestEqual(TEXT("重开图鉴投影仍为已保存编号"), Book->GetLastCollectionViewState().TrackedItemId, 3);
			UI->ToggleCollection();
			return !Test->HasAnyErrors();
		}

		/** 统计当前玩家已打开团队库存内的可见追踪区域；不再把独立视口鱼卡当作页面内推荐。 */
		int32 CountVisibleTrackingCards(ULocalPlayer* Player) const
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Player, Widgets, UCatCampInventoryWidget::StaticClass(), true);
			int32 Count = 0;
			for (auto* Widget : Widgets)
			{
				auto* Panel = Widget->GetWidgetFromName(TEXT("TrackingPanel"));
				if (Widget->GetOwningLocalPlayer() == Player && Widget->IsInViewport() && Panel && Panel->IsVisible()) ++Count;
			}
			return Count;
		}

		/** 在第二远端客户端的正式营地 WBP 找到空目标格，并把其看到的营地 BugBait 拖到该格，证明它可独立提交下一次操作。 */
		bool DropCampItemFromSecondClient()
		{
			UCatInventoryComponent* CampInventory = ClientCamps[1]->GetInventoryComponent();
			const int32 SourceSlot = FindOccupiedSlot(CampInventory->GetInventoryModel()->GetInventoryList(), 4);
			UCatInventorySlotWidget* TargetSlot = FindWidgetSlot(ClientControllersByIndex[1].Get(), CampInventory, 1);
			if (SourceSlot == INDEX_NONE || !TargetSlot) return false;
			if (!DoesWidgetEntryMatch(FindWidgetSlot(ClientControllersByIndex[1].Get(), CampInventory, SourceSlot), 4)
				|| !DoesWidgetEntryMatch(TargetSlot, 0)) return false;
			SubmitActualWidgetDrop(TargetSlot, CampInventory, SourceSlot);
			Stage = 3;
			return false;
		}

		/** 比较服务器和两个远端组件 Model 在指定营地槽位的物品编号；期望编号为 0 时要求三个位置均为空。 */
		bool DoesCampContain(const int32 ExpectedDefinition, const int32 SlotIndex) const
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
		bool DoesBackpackContain(const int32 ClientIndex, const int32 ExpectedDefinition) const
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
		static bool DoesWidgetEntryMatch(const UCatInventorySlotWidget* SlotWidget, const int32 ExpectedDefinition)
		{
			if (!SlotWidget || !SlotWidget->WidgetTree) return false;
			const FCatInventoryEntry& Entry = SlotWidget->GetInventoryEntry();
			const UImage* Icon = Cast<UImage>(SlotWidget->WidgetTree->FindWidget(TEXT("ThumbnailImage")));
			const UTextBlock* Quantity = Cast<UTextBlock>(SlotWidget->WidgetTree->FindWidget(TEXT("QuantityTextBlock")));
			if (!Icon || !Quantity) return false;
			if (ExpectedDefinition == 0)
			{
				return !Entry.Instance && Entry.StackCount == 0 && !Icon->GetBrush().GetResourceObject()
					&& Icon->GetVisibility() == ESlateVisibility::Collapsed && Quantity->GetVisibility() == ESlateVisibility::Collapsed;
			}
			if (!Entry.Instance || Entry.StackCount <= 0 || Entry.Instance->GetItemId() != ExpectedDefinition) return false;
			const UCatInventoryItemDefinition* Definition = Entry.Instance->GetItemDefinition();
			UTexture2D* ExpectedIcon = Definition ? Definition->GetInventoryThumbnail().LoadSynchronous() : nullptr;
			return ExpectedIcon && Icon->GetBrush().GetResourceObject() == ExpectedIcon && Icon->GetVisibility() == ESlateVisibility::Visible
				&& Quantity->GetText().EqualTo(FText::AsNumber(Entry.StackCount))
				&& Quantity->GetVisibility() == (Entry.StackCount > 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}

		/** 验证正式营地 View 中嵌套背包面板的 WrapBox 已显示本地角色背包条目，证明两种库存面板没有共用营地数据源。 */
		bool DoesNestedBackpackWidgetMatch(const int32 ClientIndex, const int32 SlotIndex, const int32 ExpectedDefinition) const
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
		static int32 FindOccupiedSlot(const TArray<FCatInventoryEntry>& Entries, const int32  ItemId)
		{
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				if (Entries[Index].Instance && Entries[Index].StackCount > 0 && Entries[Index].Instance->GetItemId() == ItemId) return Index;
			}
			return INDEX_NONE;
		}

		/** 比较权威列表或客户端 Model 在指定格的最终物品身份；空期望用于证明服务器移动或清空已经传播，而不是只看见旧 UI 残留。 */
		static bool DoesEntryMatch(const TArray<FCatInventoryEntry>& Entries, const int32 SlotIndex, const int32 ExpectedDefinition)
		{
			if (!Entries.IsValidIndex(SlotIndex)) return false;
			const FCatInventoryEntry& Entry = Entries[SlotIndex];
			if (ExpectedDefinition == 0) return Entry.Instance == nullptr || Entry.StackCount <= 0;
			return Entry.Instance && Entry.StackCount > 0 && Entry.Instance->GetItemId() == ExpectedDefinition;
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
		/** 本次 PIE 已检查过追踪卡的标志；等待营地资产时不重复授予捕获记录。 */
		bool bTrackingChecked = false;
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
