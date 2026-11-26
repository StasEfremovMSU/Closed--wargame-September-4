#include "RegimentAudioSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAudioVolumeTest, "RegimentAudio.VolumeSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAudioVolumeTest::RunTest(const FString&)
{
    using namespace RegimentAudioSettings;
    FVolumeSettings Settings;
    TestEqual(TEXT("Missing settings use the full combat mix"), Settings.GetVolume(EBus::Combat), 1.f);
    Settings.SetVolume(EBus::Combat, -1.f);
    Settings.SetVolume(EBus::Commands, .75f);
    Settings.SetVolume(EBus::Ambience, .125f);
    Settings.SetVolume(EBus::Interface, 2.f);

    const FString Filename = FConfigCacheIni::NormalizeConfigIniPath(
        FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("RegimentAudio"), TEXT(".ini")));
    GConfig->Add(Filename, FConfigFile());
    Settings.Save(*GConfig, Filename);
    GConfig->UnloadFile(Filename);
    FVolumeSettings Loaded;
    Loaded.Load(*GConfig, Filename);
    GConfig->UnloadFile(Filename);
    IFileManager::Get().Delete(*Filename, false, true);

    TestEqual(TEXT("Combat can be muted and never has negative gain"), Loaded.GetVolume(EBus::Combat), 0.f);
    TestEqual(TEXT("Command gain persists independently"), Loaded.GetVolume(EBus::Commands), .75f);
    TestEqual(TEXT("Ambience gain persists independently"), Loaded.GetVolume(EBus::Ambience), .125f);
    TestEqual(TEXT("Interface gain cannot exceed full volume"), Loaded.GetVolume(EBus::Interface), 1.f);
    Loaded.Reset();
    TestEqual(TEXT("Restore defaults raises a muted channel"), Loaded.GetVolume(EBus::Combat), 1.f);
    return true;
}
#endif
