#include "CatFrontendWidgetAuthoringLibrary.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

bool UCatFrontendWidgetAuthoringLibrary::BeginFloatingFrontendPreview(int32 Width, int32 Height)
{
    if (!GEditor || GEditor->PlayWorld || Width < 640 || Height < 480) { return false; }
    FRequestPlaySessionParams Params;
    Params.EditorPlaySettings = DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
    Params.EditorPlaySettings->NewWindowWidth = Width;
    Params.EditorPlaySettings->NewWindowHeight = Height;
    Params.EditorPlaySettings->SetPlayNumberOfClients(1);
    Params.EditorPlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
    Params.bAllowOnlineSubsystem = false;
    GEditor->RequestPlaySession(Params);
    return true;
}

void UCatFrontendWidgetAuthoringLibrary::PressFrontendPreviewKey(FName KeyName)
{
    if (!GEditor || !GEditor->PlayWorld || !FSlateApplication::IsInitialized()) { return; }
    const FKey Key(KeyName);
    if (!Key.IsValid()) { return; }
    const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
    FSlateApplication::Get().ProcessKeyDownEvent(Event);
    FSlateApplication::Get().ProcessKeyUpEvent(Event);
}
