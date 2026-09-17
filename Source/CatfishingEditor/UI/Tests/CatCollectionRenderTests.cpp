#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UI/CatUISettings.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "Inventory/CatInventorySettings.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "InputCoreTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatCollectionRenderTest,
    "Catfishing.Presentation.Collection.RenderFormalBook", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 使用正式 WBP 与真实鱼图渲染两种分辨率；仅输入已脱敏的模拟解锁状态，不改玩家档案或正式资产。
bool FCatCollectionRenderTest::RunTest(const FString& Parameters)
{
    if (!FApp::CanEverRender()) { AddError(TEXT("本项必须使用真实 RHI 渲染，不能用 nullrhi 代替画面验证")); return false; }
    FTestWorldWrapper World;
    if (!World.CreateTestWorld(EWorldType::Game)) return false;
    const auto Class = GetDefault<UCatUISettings>()->LoadCollectionWidgetClass();
    if (!TestNotNull(TEXT("正式图鉴蓝图可加载"), Class.Get())) return false;
    if (const auto* Blueprint = Cast<UWidgetBlueprint>(Class->ClassGeneratedBy))
    {
        TArray<UWidget*> Widgets;
        if (Blueprint->WidgetTree) Blueprint->WidgetTree->GetAllWidgets(Widgets);
        for (const UWidget* Child : Widgets) AddInfo(TEXT("ExistingDesignerWidget=") + Child->GetName());
        AddInfo(FString::Printf(TEXT("FormalBlueprint Graphs=%d Bindings=%d"), Blueprint->UbergraphPages.Num(), Blueprint->Bindings.Num()));
    }
    auto* Widget = NewObject<UCatCollectionWidget>(World.GetTestWorld(), Class);
    Widget->Initialize();
    FCatCollectionViewState View;
    View.bAvailable = true;
    View.SummaryText = FText::FromString(TEXT("鱼图鉴"));
    TArray<UCatInventoryItemDefinition*> Definitions;
    FString Error;
    if (!TestTrue(TEXT("正式总表加载"), GetDefault<UCatInventorySettings>()->GetItemDefinitions(Definitions, Error))) return false;
    for (auto* Definition : Definitions)
    {
        const auto* Fish = Cast<UCatFishDefinition>(Definition);
        if (!Fish || !GetDefault<UCatFishCatalogSettings>()->Definitions.Contains(Fish)) continue;
        auto& Entry = View.Entries.AddDefaulted_GetRef();
        Entry.ItemId = Fish->ItemId;
        Entry.bRecordedUnlocked = View.Entries.Num() % 3 != 0;
        Entry.Thumbnail = Fish->GetInventoryThumbnail();
        Entry.DisplayName = Entry.bRecordedUnlocked ? Fish->GetInventoryDisplayName() : FText::FromString(TEXT("？？？"));
        TArray<FString> Names;
        for (const auto& Bait : Fish->BaitWeightMultipliers)
            if (Bait.Multiplier > 1)
                if (const auto* Item = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition(Bait.BaitItemId)) Names.Add(Item->GetInventoryDisplayName().ToString());
        Entry.BaitPreferenceText = FText::FromString(Entry.bRecordedUnlocked ? FString::Join(Names, TEXT("、")) : TEXT("？？？"));
        TArray<FString> Chum;
        if (Fish->ChumPreference.Fishy > 0) Chum.Add(TEXT("腥"));
        if (Fish->ChumPreference.Fragrant > 0) Chum.Add(TEXT("香"));
        if (Fish->ChumPreference.Fermented > 0) Chum.Add(TEXT("发酵"));
        Entry.ChumPreferenceText = FText::FromString(Entry.bRecordedUnlocked ? FString::Join(Chum, TEXT("、")) : TEXT("？？？"));
    }
    // 截图保留一条已确认追踪的投影，实际提交链另由正式库存联机用例覆盖。
    View.TrackedItemId = 3;
    Widget->RenderCollection(View);
    const auto Slate = Widget->TakeWidget();
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FWidgetRenderer Renderer(true);
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("CollectionScreenshots");
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (const FIntPoint Size : {FIntPoint(1920,1080), FIntPoint(1280,720)})
    {
        auto* Target = NewObject<UTextureRenderTarget2D>(Widget);
        Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
        Target->InitAutoFormat(Size.X, Size.Y);
        Target->UpdateResourceImmediate(true);
        Renderer.DrawWidget(Target, Slate, FVector2D(Size), 0.0f);
        // ScaleBox 在实际窗口调整后的下一次布局才稳定；模拟连续帧后再截图，不能拿首帧缓存当适配结果。
        Renderer.DrawWidget(Target, Slate, FVector2D(Size), 1.0f / 60.0f);
        Renderer.DrawWidget(Target, Slate, FVector2D(Size), 1.0f / 60.0f);
        if (!TestNotNull(TEXT("真实控件渲染目标"), Target)) return false;
        const FString Name = FString::Printf(TEXT("Book_%dx%d.png"), Size.X, Size.Y);
        UKismetRenderingLibrary::ExportRenderTarget(World.GetTestWorld(), Target, Directory, Name);
        TestTrue(TEXT("画面文件已生成"), IFileManager::Get().FileExists(*(Directory / Name)));
    }
    // 从实际 Slate 树按文案定位按钮，直接调用按下与抬起处理来翻页；不直接修改页码，也不代表系统输入路由验证。
    TFunction<bool(const TSharedRef<SWidget>&, const FString&)> HasText;
    HasText = [&HasText](const TSharedRef<SWidget>& Node, const FString& Text)
    {
        if (Node->GetTypeAsString() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Node)->GetText().ToString() == Text) return true;
        FChildren* Children = Node->GetChildren();
        for (int32 Index = 0; Index < Children->Num(); ++Index)
            if (HasText(Children->GetChildAt(Index), Text)) return true;
        return false;
    };
    TSharedPtr<SButton> Next, Previous;
    TSharedPtr<SUniformGridPanel> Grid;
    TFunction<void(const TSharedRef<SWidget>&)> Visit;
    Visit = [&](const TSharedRef<SWidget>& Node)
    {
        if (Node->GetTypeAsString() == TEXT("SUniformGridPanel")) Grid = StaticCastSharedRef<SUniformGridPanel>(Node);
        if (Node->GetTypeAsString() == TEXT("SButton"))
        {
            if (HasText(Node, TEXT("下一页"))) Next = StaticCastSharedRef<SButton>(Node);
            if (HasText(Node, TEXT("上一页"))) Previous = StaticCastSharedRef<SButton>(Node);
        }
        FChildren* Children = Node->GetChildren();
        for (int32 Index = 0; Index < Children->Num(); ++Index) Visit(Children->GetChildAt(Index));
    };
    Visit(Slate);
    if (!TestTrue(TEXT("正式页包含翻页按钮与网格"), Next.IsValid() && Previous.IsValid() && Grid.IsValid())) return false;
    TestEqual(TEXT("首跨页十二张真实卡片"), Grid->GetChildren()->Num(), 12);
    TestFalse(TEXT("首页不能向前翻"), Previous->IsEnabled());
    const FKeyEvent Accept(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
    Next->OnKeyDown(Next->GetCachedGeometry(), Accept);
    Next->OnKeyUp(Next->GetCachedGeometry(), Accept);
    // 末页只有一枚透明布局占位，其余子控件才是真实鱼卡；占位不能冒充额外鱼种。
    TestEqual(TEXT("末页保留空白布局占位"), Grid->GetChildren()->GetChildAt(0)->GetTypeAsString(), FString(TEXT("SSpacer")));
    TestEqual(TEXT("末页不补虚构条目"), Grid->GetChildren()->Num() - 1, View.Entries.Num() - 12);
    FAssetCompilingManager::Get().FinishAllCompilation();
    auto* LastPage = NewObject<UTextureRenderTarget2D>(Widget);
    LastPage->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    LastPage->InitAutoFormat(1280, 720);
    LastPage->UpdateResourceImmediate(true);
    for (int32 Frame = 0; Frame < 3; ++Frame) Renderer.DrawWidget(LastPage, Slate, FVector2D(1280,720), 1.0f / 60.0f);
    TestFalse(TEXT("末页布局更新后不能继续后翻"), Next->IsEnabled());
    UKismetRenderingLibrary::ExportRenderTarget(World.GetTestWorld(), LastPage, Directory, TEXT("Book_LastPage_1280x720.png"));
    Previous->OnKeyDown(Previous->GetCachedGeometry(), Accept);
    Previous->OnKeyUp(Previous->GetCachedGeometry(), Accept);
    TestEqual(TEXT("返回首页恢复十二张"), Grid->GetChildren()->Num(), 12);
    return !HasAnyErrors();
}
#endif
