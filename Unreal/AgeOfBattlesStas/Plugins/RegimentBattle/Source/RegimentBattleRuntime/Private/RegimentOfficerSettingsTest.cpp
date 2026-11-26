#include "RegimentOfficerSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerSettingsTest, "RegimentBattle.Presentation.OfficerSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentOfficerSettingsTest::RunTest(const FString&)
{
    using Action = ERegimentOfficerInputAction;
    FRegimentOfficerSettings Settings;
    TestTrue(TEXT("First person is the default on possession"), Settings.IsFirstPerson());
    TestEqual(TEXT("V switches the view by default"), Settings.Key(Action::Camera), EKeys::V);
    TestEqual(TEXT("Previous mouse attack by default"), Settings.Key(Action::Attack), EKeys::LeftMouseButton);
    TestFalse(TEXT("Protects the existing regiment panel shortcut"), Settings.Bind(Action::Jump, EKeys::Tab));
    TestFalse(TEXT("Cannot share a key with a different personal action"), Settings.Bind(Action::Jump, EKeys::W));
    TestTrue(TEXT("An unused key can be assigned to a personal action"), Settings.Bind(Action::Jump, EKeys::J));
    Settings.SetMouse(1.75f);
    Settings.SetFirstPerson(false);
    const FString Filename = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("OfficerSettings"), TEXT(".ini"));
    Settings.Save(Filename);
    FRegimentOfficerSettings Loaded;
    Loaded.Load(Filename);
    TestEqual(TEXT("Jump binding persists"), Loaded.Key(Action::Jump), EKeys::J);
    TestEqual(TEXT("Mouse sensitivity persists"), Loaded.Mouse(), 1.75f);
    TestFalse(TEXT("The shoulder view choice persists"), Loaded.IsFirstPerson());

    FConfigFile Legacy;
    Legacy.SetString(TEXT("OfficerControls"), TEXT("Jump"), TEXT("V"));
    Legacy.Write(Filename, false);
    Loaded.Load(Filename);
    TestEqual(TEXT("An existing V binding stays with its original action"), Loaded.Key(Action::Jump), EKeys::V);
    TestTrue(TEXT("A legacy profile receives a free view key"),
        Loaded.Key(Action::Camera).IsValid() && Loaded.Key(Action::Camera) != EKeys::V);
    GConfig->UnloadFile(Filename);
    IFileManager::Get().Delete(*Filename, false, true);
    Settings.SetMouse(99.f);
    TestEqual(TEXT("Clamps unusably large sensitivity"), Settings.Mouse(), FRegimentOfficerSettings::MaxMouse);
    return true;
}
#endif
