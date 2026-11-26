#include "RegimentHistoricalCampaign.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "RegimentMissionPreset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHistoricalCampaignTest, "RegimentBattle.Campaign.Progression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHistoricalCampaignTest::RunTest(const FString&)
{
    using namespace RegimentHistoricalCampaign;
    const auto& Campaigns = Catalog();
    if (!TestEqual(TEXT("Three routes"), Campaigns.Num(), 3))
        return false;

    for (const auto& Campaign : Campaigns)
    {
        TestTrue(TEXT("Substantial route"), Campaign.Stages.Num() >= 4);
        for (const auto& Stage : Campaign.Stages)
        {
            FMissionPreset Preset;
            const FString Mission = Stage.Label.ToString();
            if (TestTrue(FString::Printf(TEXT("Built-in mission loads: %s"), *Mission),
                RegimentMissionPresets::LoadBuiltIn(Stage.MissionId, Preset)))
            {
                TestEqual(FString::Printf(TEXT("Fixed playable faction: %s"), *Mission),
                    Preset.Era.BlueFaction, Campaign.PlayerFaction);
                TestTrue(FString::Printf(TEXT("Authored start: %s"), *Mission), Preset.StartsInBattle());
            }
        }
    }

    const FString Path = FPaths::ProjectIntermediateDir() / TEXT("CampaignTest") /
        (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".txt"));
    FProgress Progress;
    TestTrue(TEXT("First stage opens"), Progress.IsUnlocked(0, 0));
    TestFalse(TEXT("Next stage is locked"), Progress.IsUnlocked(0, 1));
    TestFalse(TEXT("Free-play victory is ignored"), Progress.RecordOutcome({}, battle::BattleOutcome::Victory, 0));
    const TOptional<FRun> FirstRun{FRun{0, 0}};
    TestFalse(TEXT("Defeat is ignored"), Progress.RecordOutcome(FirstRun, battle::BattleOutcome::Victory, 1));
    TestFalse(TEXT("Draw is ignored"), Progress.RecordOutcome(FRun{0, 0}, battle::BattleOutcome::Draw, 0));
    TestFalse(TEXT("Locked stage cannot be recorded"), Progress.RecordOutcome(FRun{0, 1}, battle::BattleOutcome::Victory, 0));
    TestTrue(TEXT("Victory after restarting the same stage is recorded"),
        Progress.RecordOutcome(FirstRun, battle::BattleOutcome::Victory, 0));
    TestTrue(TEXT("Next stage unlocked"), Progress.IsUnlocked(0, 1));
    TestFalse(TEXT("Replay does not advance"), Progress.RecordOutcome(FRun{0, 0}, battle::BattleOutcome::Victory, 0));
    TestFalse(TEXT("Other route is independent"), Progress.IsUnlocked(1, 1));
    TestTrue(TEXT("Progress writes"), Progress.Save(Path));
    TestTrue(TEXT("Subsequent victory is recorded"), Progress.RecordOutcome(FRun{0, 1}, battle::BattleOutcome::Victory, 0));
    TestTrue(TEXT("Progress updates existing save"), Progress.Save(Path));
    FProgress Reloaded;
    Reloaded.Load(Path);
    TestTrue(TEXT("Progress survives reload"), Reloaded.IsUnlocked(0, 2));
    TestTrue(TEXT("Replay remains available"), Reloaded.IsUnlocked(0, 0));
    TestFalse(TEXT("Other route remains locked after reload"), Reloaded.IsUnlocked(1, 1));
    auto& Files = IFileManager::Get();
    TestTrue(TEXT("Test simulates a missing current save"), Files.Delete(*Path));
    Reloaded.Load(Path);
    TestTrue(TEXT("Previous save can be recovered"), Reloaded.IsUnlocked(0, 1));
    TestFalse(TEXT("Recovery uses the previous revision"), Reloaded.IsUnlocked(0, 2));
    Files.Delete(*(Path + TEXT(".bak")));
    return true;
}
#endif
