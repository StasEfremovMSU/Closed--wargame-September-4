#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentMissionPreset.h"
#include "RegimentMissionScenario.h"
#include "RegimentEraCatalog.h"
#include "RegimentGameModules.h"
#include "battle/Formation.h"
#include "battle/Simulation.h"

#include "Containers/Set.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <tuple>

namespace
{
struct FMissionPresetTestFolder
{
    const FString TestRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()) / TEXT("MissionPresetTests");
    const FString Path = TestRoot / FGuid::NewGuid().ToString(EGuidFormats::Digits);

    ~FMissionPresetTestFolder()
    {
        if (Path != TestRoot && FPaths::IsUnderDirectory(Path, TestRoot))
            IFileManager::Get().DeleteDirectory(*Path, false, true);
    }

    bool WriteFixture(FGuid Id, const TCHAR* Contents) const
    {
        const FString Filename = Path / (Id.ToString(EGuidFormats::Digits) + TEXT(".json"));
        return IFileManager::Get().MakeDirectory(*Path, true) &&
            FFileHelper::SaveStringToFile(Contents, *Filename);
    }
};

FString PresetFixture(int32 Version, const TCHAR* AdditionalEntryFields, const TCHAR* AdditionalPresetFields)
{
    TArray<FString> Entries;
    for (int32 Index = 0; Index < MissionRegimentCount; ++Index)
    {
        Entries.Add(FString::Printf(TEXT("{\"type\":\"Infantry\",\"swords\":28,\"archers\":8,\"spearmen\":7,\"mounted\":40%s}"),
            AdditionalEntryFields));
    }
    return FString::Printf(TEXT("{\"schema_version\":%d,\"name\":\"Fixture\",\"terrain\":\"River\",\"regiments\":[%s]%s}"),
        Version, *FString::Join(Entries, TEXT(",")), AdditionalPresetFields);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetVersionTwoTest, "RegimentBattle.MissionPresets.VersionTwo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetVersionTwoTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    const FGuid Id = FGuid::NewGuid();
    const FString Fixture = PresetFixture(2,
        TEXT(",\"enabled\":true,\"group\":2,\"start_x\":-14.5,\"start_y\":-20"),
        TEXT(",\"objective\":{\"goal\":\"Crossing\",\"time_limit_seconds\":120,\"crossing\":1,\"required_regiments\":2,\"crossing_team\":0},\"briefing\":\"Цель\",\"sources\":[\"https://example.org/history\"]"));
    if (!TestTrue(TEXT("Writes a complete version 2 fixture"), Folder.WriteFixture(Id, *Fixture)))
        return false;

    FMissionPreset Loaded;
    FText Error;
    if (!TestTrue(TEXT("Loads a scenario saved with the version 2 schema"), RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error)))
        return false;
    TestEqual(TEXT("Reads the existing troop fields"), Loaded.Roster[0].Swords, 28);
    TestEqual(TEXT("Reads an explicit group"), Loaded.Roster[0].Group, 2);
    TestEqual(TEXT("Reads an explicit start X"), Loaded.Roster[0].StartX.value_or(0.f), -14.5f);
    TestEqual(TEXT("Reads an explicit start Y"), Loaded.Roster[0].StartY.value_or(0.f), -20.f);
    TestEqual(TEXT("Reads goal type"), Loaded.Objective.Goal, EMissionGoal::Crossing);
    TestEqual(TEXT("Reads time limit"), Loaded.Objective.TimeLimitSeconds, 120);
    TestEqual(TEXT("Reads required regiments"), Loaded.Objective.RequiredRegiments, 2);
    TestEqual(TEXT("Uses the file identity for a later in-place save"), Loaded.EditableCopyId, Id);
    TestEqual(TEXT("Reads the authored briefing"), Loaded.Briefing, FString(TEXT("Цель")));
    TestFalse(TEXT("Version 2 copies keep the legacy era"), Loaded.Era.IsSet());
    TestEqual(TEXT("A version 2 copy without field data keeps its width"), Loaded.Field.width_m, 120);
    TestEqual(TEXT("A version 2 copy without field data keeps its depth"), Loaded.Field.height_m, 76);
    if (TestEqual(TEXT("Reads one source"), Loaded.Sources.Num(), 1))
        TestEqual(TEXT("Preserves the source URL"), Loaded.Sources[0], FString(TEXT("https://example.org/history")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetLegacyTest, "RegimentBattle.MissionPresets.ReadsLegacyCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetLegacyTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    const FGuid Id = FGuid::NewGuid();
    const FString Fixture = PresetFixture(1, TEXT(""), TEXT(""));
    if (!TestTrue(TEXT("Writes a legacy preset"), Folder.WriteFixture(Id, *Fixture)))
        return false;

    FMissionPreset Loaded;
    FText Error;
    if (!TestTrue(TEXT("Opens the old copy"), RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error)))
        return false;
    for (int32 Slot = 0; Slot < MissionRosterCapacity; ++Slot)
    {
        const auto& Entry = Loaded.Roster[Slot];
        TestEqual(TEXT("Legacy missions only activate their original slots"), Entry.bEnabled, Slot < MissionRegimentCount);
        TestFalse(TEXT("Legacy X remains automatic"), Entry.StartX.has_value());
        TestFalse(TEXT("Legacy Y remains automatic"), Entry.StartY.has_value());
    }
    TestEqual(TEXT("The last blue regiment retains its group"), MissionGroup(5, Loaded.Roster[5]), 3);
    TestEqual(TEXT("The first orange regiment retains its group"), MissionGroup(6, Loaded.Roster[6]), 1);
    TestEqual(TEXT("Preserves the existing victory behavior"), Loaded.Objective.Goal, EMissionGoal::Eliminate);
    TestFalse(TEXT("Old copies keep the legacy era"), Loaded.Era.IsSet());
    TestEqual(TEXT("A version 1 copy keeps its width"), Loaded.Field.width_m, 120);
    TestEqual(TEXT("A version 1 copy keeps its depth"), Loaded.Field.height_m, 76);
    TestTrue(TEXT("Old presets play under current regiment rules"), Loaded.bRegimentTactics);
    TestFalse(TEXT("Old presets do not allow shooting into a melee"), Loaded.bRiskyVolley);
    TestFalse(TEXT("Old presets retain the march opening"), Loaded.bDirectDeployment);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetRoundTripTest, "RegimentBattle.MissionPresets.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetRoundTripTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    FMissionPreset Original;
    Original.Name = TEXT("  У стен  ");
    Original.Terrain = battle::MarchTerrain::StirlingBridge;
    Original.Objective = {EMissionGoal::Crossing, 150, 1, 3, 1};
    Original.bRiskyVolley = true;
    Original.bDirectDeployment = true;
    Original.Briefing = TEXT("Перейти на другой берег.");
    Original.Sources.Add(TEXT("https://example.org/bridge"));
    for (int32 Index = 0; Index < MissionRegimentCount; ++Index)
    {
        auto& Entry = Original.Roster[Index];
        Entry.Swords = 27 + Index;
        Entry.Archers = 19 - Index;
        Entry.Spearmen = Index;
        Entry.Mounted = 97 - Index;
        Entry.bCavalry = Index % 2 == 0;
    }
    Original.Roster[0].StartX = -43.5f;
    Original.Roster[7].bEnabled = false;
    Original.Roster[7].Group = 3;

    FText Error;
    FGuid Id;
    if (!TestTrue(TEXT("Saves a named copy"), RegimentMissionPresets::SaveCopy(Folder.Path, Original, Id, Error)))
        return false;
    TestTrue(TEXT("Assigns a valid identity"), Id.IsValid());
    FString Contents;
    const FString Filename = Folder.Path / (Id.ToString(EGuidFormats::Digits) + TEXT(".json"));
    if (TestTrue(TEXT("Reads back the saved JSON"), FFileHelper::LoadFileToString(Contents, *Filename)))
    {
        TSharedPtr<FJsonObject> Json;
        if (TestTrue(TEXT("Parses the saved JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Json)))
        {
            TestEqual(TEXT("Still writes schema 2 for legacy missions"), Json->GetIntegerField(TEXT("schema_version")), 2);
            TestFalse(TEXT("Writes a legacy copy without era metadata"), Json->HasField(TEXT("era")));
            TestFalse(TEXT("Omits default field dimensions from the JSON"), Json->HasField(TEXT("field")));
        }
    }

    const auto Choices = RegimentMissionPresets::List(Folder.Path);
    if (!TestEqual(TEXT("One copy appears in the list"), Choices.Num(), 1))
        return false;
    TestEqual(TEXT("List returns the copy identity"), Choices[0].Id, Id);
    TestEqual(TEXT("List shows the trimmed Cyrillic name"), Choices[0].Name, FString(TEXT("У стен")));

    FMissionPreset Loaded;
    if (!TestTrue(TEXT("Loads the copy by identity"), RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error)))
        return false;
    TestEqual(TEXT("Preserves the selected terrain"), Loaded.Terrain, battle::MarchTerrain::StirlingBridge);
    TestEqual(TEXT("Preserves crossing time"), Loaded.Objective.TimeLimitSeconds, 150);
    TestEqual(TEXT("Preserves required regiments"), Loaded.Objective.RequiredRegiments, 3);
    TestEqual(TEXT("Preserves the attacking side"), Loaded.Objective.CrossingTeam, 1);
    TestTrue(TEXT("Preserves permissive ranged fire"), Loaded.bRiskyVolley);
    TestTrue(TEXT("Preserves authored deployment"), Loaded.bDirectDeployment);
    TestEqual(TEXT("Preserves a custom X"), Loaded.Roster[0].StartX.value_or(0.f), -43.5f);
    TestFalse(TEXT("Leaves untouched Y automatic"), Loaded.Roster[0].StartY.has_value());
    TestFalse(TEXT("Preserves a disabled slot"), Loaded.Roster[7].bEnabled);
    TestEqual(TEXT("Preserves the group of a disabled slot"), Loaded.Roster[7].Group, 3);
    TestEqual(TEXT("Preserves the briefing"), Loaded.Briefing, Original.Briefing);
    if (TestEqual(TEXT("Preserves one source"), Loaded.Sources.Num(), 1))
        TestEqual(TEXT("Preserves the source"), Loaded.Sources[0], Original.Sources[0]);
    for (int32 Index = 0; Index < MissionRegimentCount; ++Index)
    {
        const auto& Entry = Loaded.Roster[Index];
        const auto Label = [Index](const TCHAR* Field) { return FString::Printf(TEXT("Slot %d preserves %s"), Index, Field); };
        TestEqual(Label(TEXT("swords")), Entry.Swords, 27 + Index);
        TestEqual(Label(TEXT("archers")), Entry.Archers, 19 - Index);
        TestEqual(Label(TEXT("spearmen")), Entry.Spearmen, Index);
        TestEqual(Label(TEXT("mounted")), Entry.Mounted, 97 - Index);
        TestEqual(Label(TEXT("its type")), Entry.bCavalry, Index % 2 == 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetValidateTest, "RegimentBattle.MissionPresets.ValidatesConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetValidateTest::RunTest(const FString& Parameters)
{
    FMissionPreset Preset;
    FText Error;
    const auto Reject = [this, &Preset, &Error](const TCHAR* Label)
    {
        TestFalse(Label, RegimentMissionPresets::Validate(Preset, Error));
        TestFalse(TEXT("Explains why the mission cannot start"), Error.IsEmpty());
    };

    Preset.bRegimentTactics = false;
    Preset.bRiskyVolley = true;
    Reject(TEXT("Risky volley requires the new tactical rules"));
    Preset.bRegimentTactics = true;
    Preset.bRiskyVolley = false;

    Preset.Roster[0].StartX = 57.f;
    Reject(TEXT("Rejects a start off the playable field"));
    Preset.Roster[0].StartX = 0.f;
    Preset.Roster[0].StartY = -12.f;
    Reject(TEXT("Rejects a start in the river"));
    Preset.Roster[0].StartX = -1.f;
    Preset.Roster[0].StartY = 3.5f;
    Reject(TEXT("Rejects a position at the ford edge too narrow for the marching column"));
    Preset.Roster[0].StartY = 0.f;
    TestTrue(TEXT("Allows a column centered on the ford"), RegimentMissionPresets::Validate(Preset, Error));
    Preset.Roster[0].StartX.reset();
    Preset.Roster[0].StartY.reset();
    Preset.Roster[0].Group = 4;
    Reject(TEXT("Rejects a group outside the command structure"));
    Preset.Roster[0].Group = 0;

    for (int32 Index = 0; Index < MissionRegimentsPerTeam; ++Index)
        Preset.Roster[Index].bEnabled = false;
    Reject(TEXT("Rejects an army with no active regiments"));

    Preset.Roster[0].bEnabled = true;
    Preset.Objective.Goal = EMissionGoal::Crossing;
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Reject(TEXT("A crossing mission requires a river terrain"));
    Preset.Terrain = battle::MarchTerrain::River;
    Preset.Objective.RequiredRegiments = 2;
    Reject(TEXT("Rejects a target greater than the active regiment count"));
    Preset.Objective.RequiredRegiments = 1;
    Preset.Roster[0].StartX = 18.f;
    Reject(TEXT("Rejects active attackers already on the destination bank"));
    Preset.Roster[0].StartX.reset();
    TestTrue(TEXT("One attacker on the origin bank satisfies the configuration"), RegimentMissionPresets::Validate(Preset, Error));

    Preset.Terrain = battle::MarchTerrain::Kalka;
    Preset.Objective.Crossing = 0;
    Reject(TEXT("Rejects a side ford outside the resized battlefield"));
    TestTrue(TEXT("Names the invalid ford field"), Error.ToString().Contains(TEXT("objective.crossing")));
    Preset.Objective.Crossing = 1;
    TestTrue(TEXT("Keeps the center ford usable on the resized battlefield"), RegimentMissionPresets::Validate(Preset, Error));
    Preset.Terrain = battle::MarchTerrain::River;
    Preset.Objective.Goal = EMissionGoal::ReserveExercise;
    Preset.Roster[2].bEnabled = true;
    Reject(TEXT("Exercise needs two enabled blue regiments in the same group"));
    Preset.Roster[2].Group = 1;
    TestTrue(TEXT("Two blue regiments in the same group can start the exercise"), RegimentMissionPresets::Validate(Preset, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetCopiesTest, "RegimentBattle.MissionPresets.SaveCopies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetCopiesTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    FMissionPreset First;
    First.Name = TEXT("Миссия");
    First.Terrain = battle::MarchTerrain::OpenField;
    FText Error;
    FGuid FirstId;
    if (!TestTrue(TEXT("Saves the first copy"), RegimentMissionPresets::SaveCopy(Folder.Path, First, FirstId, Error)))
        return false;

    FMissionPreset Second = First;
    Second.Name = TEXT(" МИССИЯ ");
    Second.Terrain = battle::MarchTerrain::StoneWalls;
    FGuid SecondId;
    TestFalse(TEXT("Rejects a case-insensitive Cyrillic name collision"),
        RegimentMissionPresets::SaveCopy(Folder.Path, Second, SecondId, Error));
    TestFalse(TEXT("Explains the collision"), Error.IsEmpty());

    Second.Name = TEXT("Second");
    TestTrue(TEXT("Saves the edited setup under a new name"),
        RegimentMissionPresets::SaveCopy(Folder.Path, Second, SecondId, Error));
    TestTrue(TEXT("The two saves have distinct identities"), SecondId.IsValid() && SecondId != FirstId);
    TestEqual(TEXT("Both saved copies remain in the list"), RegimentMissionPresets::List(Folder.Path).Num(), 2);
    FMissionPreset Loaded;
    TestTrue(TEXT("First copy can still be loaded"), RegimentMissionPresets::Load(Folder.Path, FirstId, Loaded, Error));
    TestEqual(TEXT("Saving an edited setup keeps the first terrain"), Loaded.Terrain, battle::MarchTerrain::OpenField);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetUpdateTest, "RegimentBattle.MissionPresets.Update",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetUpdateTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    FMissionPreset Original;
    Original.Name = TEXT("Editable copy");
    FGuid Id;
    FText Error;
    if (!TestTrue(TEXT("Creates a copy"), RegimentMissionPresets::SaveCopy(Folder.Path, Original, Id, Error)))
        return false;

    FMissionPreset Edited = Original;
    Edited.Terrain = battle::MarchTerrain::StoneWalls;
    Edited.Roster[0].Swords = 22;
    TestTrue(TEXT("Updates the same copy"), RegimentMissionPresets::Save(Folder.Path, Id, Edited, Error));
    TestEqual(TEXT("Updating leaves only one copy"), RegimentMissionPresets::List(Folder.Path).Num(), 1);

    Edited.Roster[0].Swords = MissionMaxSwords + 1;
    TestFalse(TEXT("Rejects an invalid update"), RegimentMissionPresets::Save(Folder.Path, Id, Edited, Error));
    FMissionPreset Loaded;
    if (!TestTrue(TEXT("The copy still opens under its original identity"), RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error)))
        return false;
    TestEqual(TEXT("Keeps the valid update after a rejected save"), Loaded.Terrain, battle::MarchTerrain::StoneWalls);
    TestEqual(TEXT("The invalid troop change was never written"), Loaded.Roster[0].Swords, 22);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetInvalidDataTest, "RegimentBattle.MissionPresets.InvalidData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetInvalidDataTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    const FGuid BrokenId = FGuid::NewGuid();
    const FGuid FutureId = FGuid::NewGuid();
    const FGuid IncompleteId = FGuid::NewGuid();
    if (!TestTrue(TEXT("Writes a truncated fixture"), Folder.WriteFixture(BrokenId, TEXT("{\"name\":\"broken\""))) ||
        !TestTrue(TEXT("Writes a future-version fixture"), Folder.WriteFixture(FutureId,
            TEXT("{\"schema_version\":6,\"name\":\"future\"}"))) ||
        !TestTrue(TEXT("Writes an incomplete-roster fixture"), Folder.WriteFixture(IncompleteId,
            TEXT("{\"schema_version\":1,\"name\":\"incomplete\",\"terrain\":\"River\",\"regiments\":[]}"))))
        return false;

    FMissionPreset Output;
    Output.Name = TEXT("Current setup");
    Output.Terrain = battle::MarchTerrain::OpenField;
    Output.Roster[11].Mounted = 12;

    const FGuid InvalidIds[] = {BrokenId, FutureId, IncompleteId, FGuid{}};
    for (const FGuid Id : InvalidIds)
    {
        FText Error;
        TestFalse(TEXT("Rejects an invalid copy"), RegimentMissionPresets::Load(Folder.Path, Id, Output, Error));
        TestFalse(TEXT("Returns an explanation"), Error.IsEmpty());
        TestEqual(TEXT("Failed load preserves the name"), Output.Name, FString(TEXT("Current setup")));
        TestEqual(TEXT("Failed load preserves the terrain"), Output.Terrain, battle::MarchTerrain::OpenField);
        TestEqual(TEXT("Failed load preserves the army"), Output.Roster[11].Mounted, 12);
    }

    FMissionPreset Invalid;
    Invalid.Name = TEXT("Invalid");
    Invalid.Roster[0] = {77, 20, 1};
    FGuid SaveId;
    FText Error;
    TestFalse(TEXT("Rejects infantry stronger than 100 including staff"),
        RegimentMissionPresets::SaveCopy(Folder.Path, Invalid, SaveId, Error));
    TestFalse(TEXT("Explains invalid troop strength"), Error.IsEmpty());

    FMissionPreset Valid;
    Valid.Name = TEXT("Valid");
    TestFalse(TEXT("Reports a file used as the save directory"),
        RegimentMissionPresets::SaveCopy(
            Folder.Path / (BrokenId.ToString(EGuidFormats::Digits) + TEXT(".json")), Valid, SaveId, Error));
    TestFalse(TEXT("Explains the directory failure"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetEraCopyTest, "RegimentBattle.MissionPresets.EraCopy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetEraCopyTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    FMissionPreset EraMission;
    int32 EraMissions = 0;
    TSet<FGuid> BuiltInIds;
    for (const FMissionPresetListItem& Item : RegimentMissionPresets::BuiltIns())
    {
        TestFalse(TEXT("Built-in IDs are distinct"), BuiltInIds.Contains(Item.Id));
        BuiltInIds.Add(Item.Id);
        FMissionPreset Candidate;
        if (const auto* Era = RegimentEraCatalog::Find(TEXT("ancient"));
            Era && Item.Id == Era->MissionId && RegimentMissionPresets::LoadBuiltIn(Item.Id, Candidate))
        {
            TestTrue(TEXT("The ancient mission has a stable nonempty ID"), Item.Id.IsValid());
            TestTrue(TEXT("The ancient mission is listed as built in"), Item.bBuiltIn);
            EraMission = MoveTemp(Candidate);
            ++EraMissions;
        }
    }
    if (!TestEqual(TEXT("The ancient module adds one built-in mission"), EraMissions, 1))
        return false;
    TestEqual(TEXT("Rome is the default blue faction"), EraMission.Era.BlueFaction, FString(TEXT("rome")));
    TestEqual(TEXT("Carthage is the default orange faction"), EraMission.Era.OrangeFaction, FString(TEXT("carthage")));
    TestFalse(TEXT("The built-in mission has no editable copy identity"), EraMission.EditableCopyId.IsValid());
    TestEqual(TEXT("The battle takes place on an open field"), EraMission.Terrain, battle::MarchTerrain::OpenField);
    TestEqual(TEXT("Defeating the other faction is the objective"), EraMission.Objective.Goal, EMissionGoal::Eliminate);
    TestTrue(TEXT("The built-in starts with deployed regiments"), EraMission.StartsInBattle());
    TestTrue(TEXT("Regiment tactics are enabled by default"), EraMission.bRegimentTactics);
    TestFalse(TEXT("Risky volley is disabled by default"), EraMission.bRiskyVolley);
    TestEqual(TEXT("Uses the authored blue formation"), EraMission.Roster[0].Swords, 30);
    TestEqual(TEXT("Uses the authored orange formation"), EraMission.Roster[MissionRegimentsPerTeam].Spearmen, 17);
    TestEqual(TEXT("Uses the authored blue starting position"), EraMission.Roster[0].StartX.value_or(0.f), -23.f);
    TestEqual(TEXT("Uses the authored orange starting position"), EraMission.Roster[MissionRegimentsPerTeam].StartX.value_or(0.f), 23.f);

    EraMission.Name = TEXT("Ancient alternate setup");
    EraMission.Field = {220, 140};
    Swap(EraMission.Era.BlueFaction, EraMission.Era.OrangeFaction);
    EraMission.Roster[0].Swords = 11;
    EraMission.bRegimentTactics = true;
    EraMission.bRiskyVolley = true;
    FGuid CopyId;
    FText Error;
    if (!TestTrue(TEXT("Saves an edited ancient setup"), RegimentMissionPresets::SaveCopy(Folder.Path, EraMission, CopyId, Error)))
        return false;

    FString Contents;
    TSharedPtr<FJsonObject> Json;
    const FString Filename = Folder.Path / (CopyId.ToString(EGuidFormats::Digits) + TEXT(".json"));
    if (!TestTrue(TEXT("Parses the saved era JSON"), FFileHelper::LoadFileToString(Contents, *Filename) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Json)))
        return false;
    TestEqual(TEXT("Writes schema 3 for a selected era"), Json->GetIntegerField(TEXT("schema_version")), 3);

    FMissionPreset Loaded;
    if (!TestTrue(TEXT("Reopens the edited ancient setup"), RegimentMissionPresets::Load(Folder.Path, CopyId, Loaded, Error)))
        return false;
    TestEqual(TEXT("Persists the era ID"), Loaded.Era.Id, FString(TEXT("ancient")));
    TestEqual(TEXT("Persists the module version"), Loaded.Era.Version, EraMission.Era.Version);
    TestEqual(TEXT("Persists the chosen blue faction"), Loaded.Era.BlueFaction, FString(TEXT("carthage")));
    TestEqual(TEXT("Persists the chosen orange faction"), Loaded.Era.OrangeFaction, FString(TEXT("rome")));
    TestEqual(TEXT("A version 3 copy retains the custom field width"), Loaded.Field.width_m, 220);
    TestEqual(TEXT("A version 3 copy retains the custom field depth"), Loaded.Field.height_m, 140);
    TestEqual(TEXT("Persists edits to the roster"), Loaded.Roster[0].Swords, 11);
    TestTrue(TEXT("Schema 3 copies resolve mounted and ranged profiles from each faction"),
        Loaded.Roster[2].MountedClass == ERegimentMountClass::FactionDefault &&
        Loaded.Roster[0].RangedClass == ERegimentRangedClass::FactionDefault);
    TestTrue(TEXT("Persists a tactical override"), Loaded.bRiskyVolley);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetTroopProfilesTest, "RegimentBattle.MissionPresets.TroopProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetTroopProfilesTest::RunTest(const FString&)
{
    const FMissionPresetTestFolder Folder;
    const struct
    {
        const TCHAR* Era;
        int32 RangedSlot;
        ERegimentRangedClass RangedClass;
        battle::RangedWeapon Weapon;
        uint32 Ammo;
        int32 LightSlot;
        int32 HeavySlot;
    } Cases[] = {
        {TEXT("ancient"), 0, ERegimentRangedClass::Javelin, battle::RangedWeapon::Javelin, 3, 2, 8},
        {TEXT("high_medieval"), 6, ERegimentRangedClass::Crossbow, battle::RangedWeapon::Crossbow, 8, 8, 2},
    };
    for (const auto& Case : Cases)
    {
        const auto* Era = RegimentEraCatalog::Find(Case.Era);
        FMissionPreset Preset;
        if (!TestNotNull(Case.Era, Era) ||
            !TestTrue(TEXT("Loads the era mission"), RegimentMissionPresets::LoadBuiltIn(Era->MissionId, Preset)))
            return false;
        Preset.Roster[Case.RangedSlot].RangedClass = Case.RangedClass;
        Preset.Roster[Case.LightSlot].MountedClass = ERegimentMountClass::Light;
        Preset.Roster[Case.HeavySlot].MountedClass = ERegimentMountClass::Heavy;
        Preset.Name = FString::Printf(TEXT("Проверка профилей · %s"), Case.Era);
        FGuid Id;
        FText Error;
        FMissionPreset Loaded;
        if (!TestTrue(TEXT("Saves the explicit troop selections"), RegimentMissionPresets::SaveCopy(Folder.Path, Preset, Id, Error)) ||
            !TestTrue(TEXT("Reloads the explicit troop selections"), RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error)))
        {
            AddError(Error.ToString());
            return false;
        }
        TestTrue(TEXT("Era-filtered listing includes the saved selection"),
            RegimentMissionPresets::ListForEra(Folder.Path, Case.Era).ContainsByPredicate(
                [Id](const FMissionPresetListItem& Item) { return Item.Id == Id; }));
        TestTrue(TEXT("Keeps the explicit ranged choice"), Loaded.Roster[Case.RangedSlot].RangedClass == Case.RangedClass);
        TestTrue(TEXT("Keeps both explicit cavalry classes"),
            Loaded.Roster[Case.LightSlot].MountedClass == ERegimentMountClass::Light &&
            Loaded.Roster[Case.HeavySlot].MountedClass == ERegimentMountClass::Heavy);

        const auto Opening = MakeMissionOpening(Loaded.Roster, Loaded.Terrain, Loaded.Field, Loaded.FigureTarget);
        const auto Scenario = BuildPresetBattle(Loaded, Opening, EMissionDeployment::AuthoredStart);
        const auto RangedId = battle::RegimentId{uint64(Case.RangedSlot + 1)};
        const auto LightId = battle::RegimentId{uint64(Case.LightSlot + 1)};
        const auto HeavyId = battle::RegimentId{uint64(Case.HeavySlot + 1)};
        int32 Shooters = 0, Light = 0, Heavy = 0;
        for (const auto& Unit : Scenario.units)
        {
            if (Unit.regiment == RangedId && Unit.archer)
            {
                ++Shooters;
                TestTrue(TEXT("BattleCore receives ranged weapon identity"), Unit.ranged_weapon == Case.Weapon);
                TestEqual(TEXT("BattleCore receives the rulebook's stock per soldier"), Unit.ammunition, Case.Ammo);
            }
            if (Unit.regiment == LightId)
            {
                ++Light;
                TestTrue(TEXT("Light regiment consists of light cavalry"), Unit.troop == battle::UnitTroop::Mounted &&
                    Unit.cavalry == battle::CavalryClass::Light);
            }
            if (Unit.regiment == HeavyId)
            {
                ++Heavy;
                TestTrue(TEXT("Heavy regiment consists of heavy cavalry"), Unit.troop == battle::UnitTroop::Mounted &&
                    Unit.cavalry == battle::CavalryClass::Heavy);
            }
        }
        TestEqual(TEXT("Ranged strength reaches the scenario"), Shooters, Loaded.Roster[Case.RangedSlot].Archers);
        TestEqual(TEXT("Light cavalry strengths include command staff"), Light, Loaded.Roster[Case.LightSlot].Total());
        TestEqual(TEXT("Heavy cavalry strengths include command staff"), Heavy, Loaded.Roster[Case.HeavySlot].Total());
        TestTrue(TEXT("BattleCore accepts the reloaded era scenario"), bool(battle::CreateWorld(Scenario, 42)));

        Loaded.Roster[Case.RangedSlot].RangedClass = Case.Weapon == battle::RangedWeapon::Javelin
            ? ERegimentRangedClass::Crossbow : ERegimentRangedClass::Sling;
        TestFalse(TEXT("Rejects a troop profile outside the faction's catalogue"), RegimentMissionPresets::Validate(Loaded, Error));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetEraInvalidTest, "RegimentBattle.MissionPresets.InvalidEra",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetEraInvalidTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    const FGuid Id = FGuid::NewGuid();
    const TCHAR* InvalidSelections[] = {
        TEXT(""),
        TEXT(",\"era\":{\"id\":\"ancient\",\"blue_faction\":\"rome\",\"orange_faction\":\"carthage\"}"),
        TEXT(",\"era\":{\"id\":\"ancient\",\"version\":0.5,\"blue_faction\":\"rome\",\"orange_faction\":\"carthage\"}"),
        TEXT(",\"era\":{\"id\":\"ancient\",\"version\":1000000,\"blue_faction\":\"rome\",\"orange_faction\":\"carthage\"}"),
        TEXT(",\"era\":{\"id\":\"ancient\",\"version\":1,\"blue_faction\":\"unknown\",\"orange_faction\":\"carthage\"}"),
        TEXT(",\"era\":{\"id\":\"ancient\",\"version\":1,\"blue_faction\":\"rome\"}"),
        TEXT(",\"era\":{\"id\":\"unknown\",\"version\":1,\"blue_faction\":\"rome\",\"orange_faction\":\"carthage\"}"),
    };
    for (const TCHAR* Invalid : InvalidSelections)
    {
        const FString Details = FString(TEXT(",\"objective\":{\"goal\":\"Eliminate\",\"time_limit_seconds\":90,\"crossing\":1,\"required_regiments\":1,\"crossing_team\":0},\"briefing\":\"\",\"sources\":[]")) + Invalid;
        const FString Fixture = PresetFixture(3, TEXT(",\"enabled\":true,\"group\":1"), *Details);
        if (!TestTrue(TEXT("Writes the fixture"), Folder.WriteFixture(Id, *Fixture)))
            return false;
        FMissionPreset Loaded;
        Loaded.Name = TEXT("Current setup");
        FText Error;
        TestFalse(FString::Printf(TEXT("Rejects invalid era metadata [%s]"), Invalid),
            RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error));
        TestFalse(TEXT("Explains invalid era metadata"), Error.IsEmpty());
        TestEqual(TEXT("Rejected input preserves the current setup"), Loaded.Name, FString(TEXT("Current setup")));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentDedicatedHistoricalTerrainTest, "RegimentBattle.MissionPresets.DedicatedHistoricalTerrain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentDedicatedHistoricalTerrainTest::RunTest(const FString&)
{
    const FString Directory = FPaths::ProjectContentDir() / TEXT("Battle/HistoricalPresets");
    const struct
    {
        const TCHAR* Id;
        battle::MarchTerrain Terrain;
    } Cases[] = {
        {TEXT("FD67C93DB19B57598941C25169B7EBFF"), battle::MarchTerrain::Thermopylae},
        {TEXT("458A83AA2AFE423F8A6DE3E17F556DBC"), battle::MarchTerrain::Issus},
        {TEXT("E7EC562E317D5FADA49C380D1E3684AE"), battle::MarchTerrain::Alesia},
        {TEXT("120A3C1B7539440E8D53A2A671463891"), battle::MarchTerrain::Chaeronea},
        {TEXT("CF730162A275449991AFABD448FFA2B8"), battle::MarchTerrain::Hydaspes},
        {TEXT("47EB0F39FBB54ECCA5CC7C9744028A40"), battle::MarchTerrain::Granicus},
        {TEXT("831D2E85792C5DFCAFBF95C9977A4114"), battle::MarchTerrain::Fraustadt},
        {TEXT("2DB8DD75301050769300FF906C1C84DE"), battle::MarchTerrain::Kliszow},
        {TEXT("8DCBBD50057A5ECABAE1A0A58D94849D"), battle::MarchTerrain::Bouvines},
        {TEXT("E9487555480B53DDB55F05CEE1885F45"), battle::MarchTerrain::Hastings},
        {TEXT("3C6B98A848E1574FB7B55AAC7B34E809"), battle::MarchTerrain::Patay},
        {TEXT("22E674A16632562B9E4F1718E52E2859"), battle::MarchTerrain::Bosworth},
        {TEXT("6CA83B6340274A0B92DBBDC6DCDBE4AD"), battle::MarchTerrain::Castillon},
        {TEXT("33BC0C6F226A508195759648C085EA58"), battle::MarchTerrain::Jena},
        {TEXT("99B894D5BDAE4DEC91627D84D24FE7C9"), battle::MarchTerrain::Marengo},
        {TEXT("6DC62DF89CFA521AB4829690956C5B22"), battle::MarchTerrain::Wachau},
        {TEXT("A938BFE636485BD3A1B7DBA8251D74B4"), battle::MarchTerrain::Nordlingen},
        {TEXT("F63E551F8B3152D399681B394B5F531A"), battle::MarchTerrain::Rocroi},
        {TEXT("6D7EE25807D4514B8F6C6E69A2C87431"), battle::MarchTerrain::Mikatagahara},
        {TEXT("1AB2126AA76C5916B8E82E65A7F3C978"), battle::MarchTerrain::Okehazama},
        {TEXT("478F6CC7240352E4A8BA99FCF4DF769A"), battle::MarchTerrain::Austerlitz},
        {TEXT("B60CD12CBABE4FF6894C48ECB4F96421"), battle::MarchTerrain::Crecy},
    };
    for (const auto& Case : Cases)
    {
        const FString Label = RegimentMissionPresets::TerrainKey(Case.Terrain);
        FGuid Id;
        FGuid::ParseExact(Case.Id, EGuidFormats::Digits, Id);
        FMissionPreset Disk, BuiltIn;
        FText Error;
        if (!RegimentMissionPresets::Load(Directory, Id, Disk, Error))
        {
            AddError(Label + TEXT(" preset: ") + Error.ToString());
            continue;
        }
        if (!TestTrue(Label + TEXT(" built-in loads"), RegimentMissionPresets::LoadBuiltIn(Id, BuiltIn)))
            continue;
        TestTrue(Label + TEXT(" keeps its terrain through built-in loading"), Disk.Terrain == Case.Terrain &&
            BuiltIn.Terrain == Case.Terrain && battle::MarchUsesDetailedTerrain(BuiltIn.Terrain));
        TestFalse(Label + TEXT(" has map sources"), BuiltIn.Sources.IsEmpty());
        const auto Scenario = BuildPresetBattle(BuiltIn, MakeMissionOpening(BuiltIn), EMissionDeployment::AuthoredStart);
        const auto AreaFields = [](const battle::Obstacle& Area)
        {
            return std::tie(Area.min_x, Area.min_y, Area.max_x, Area.max_y, Area.feature, Area.blocks_projectiles);
        };
        TestTrue(Label + TEXT(" shares terrain across editor and battle"),
            std::ranges::equal(Scenario.obstacles, battle::MarchTerrainAreas(Case.Terrain, BuiltIn.Field),
                std::ranges::equal_to{}, AreaFields, AreaFields) &&
            Scenario.hill == battle::MarchHill(Case.Terrain, BuiltIn.Field));
        TestTrue(Label + TEXT(" deploys its authored forces"), bool(battle::CreateWorld(Scenario, 42)));
        if (BuiltIn.Objective.Goal == EMissionGoal::HillHold && Scenario.hill)
            TestTrue(Label + TEXT(" has a reachable height objective"), battle::HillObjectiveCrest(*Scenario.hill).has_value());
        if (BuiltIn.Objective.Goal == EMissionGoal::Crossing)
            TestTrue(Label + TEXT(" uses a ford for the crossing objective"),
                battle::MarchCrossingsFor(Case.Terrain)[BuiltIn.Objective.Crossing].ford);
        if (Case.Terrain == battle::MarchTerrain::Crecy)
        {
            Disk.Objective.Goal = EMissionGoal::HillHold;
            TestTrue(TEXT("Older custom Crecy presets can still hold the crest"),
                RegimentMissionPresets::Validate(Disk, Error));
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHistoricalMissionsTest, "RegimentBattle.MissionPresets.HistoricalBattles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHistoricalMissionsTest::RunTest(const FString& Parameters)
{
    const auto BuiltIns = RegimentMissionPresets::BuiltIns();
    const FMissionPresetTestFolder Folder;
    const struct
    {
        FGuid Id;
        const TCHAR* Era;
        const TCHAR* Blue;
        const TCHAR* Orange;
        int32 RegimentCount;
    } Cases[] = {
        {FGuid(0xBD8E7423, 0xCDA541E0, 0xAD6DAF78, 0xD4C9F547), TEXT("ancient"), TEXT("rome"), TEXT("carthage"), 11},
        {FGuid(0x8E7D530F, 0x47E5433E, 0x92ED12CA, 0x13FD89C8), TEXT("high_medieval"), TEXT("rus"), TEXT("livonian_order"), 8}
    };
    TSet<battle::MarchTerrain> Terrains;
    for (const auto& Case : Cases)
    {
        const auto* Item = BuiltIns.FindByPredicate([&](const auto& Candidate) { return Candidate.Id == Case.Id; });
        if (!TestTrue(TEXT("Historical battle appears as a built-in mission"), Item && Item->bBuiltIn))
            continue;

        FMissionPreset Preset;
        if (!TestTrue(TEXT("The built-in historical battle loads"), RegimentMissionPresets::LoadBuiltIn(Case.Id, Preset)))
            continue;

        TestFalse(TEXT("Built-in presets are read-only until copied"), Preset.EditableCopyId.IsValid());
        TestTrue(TEXT("Source and briefing are visible in the mission editor"), !Preset.Sources.IsEmpty() && !Preset.Briefing.IsEmpty());
        TestEqual(TEXT("Correct era"), Preset.Era.Id, FString(Case.Era));
        TestEqual(TEXT("Correct first faction"), Preset.Era.BlueFaction, FString(Case.Blue));
        TestEqual(TEXT("Correct second faction"), Preset.Era.OrangeFaction, FString(Case.Orange));
        TestTrue(TEXT("Deploys immediately from the historic scenario layout"), Preset.StartsInBattle());
        TestTrue(TEXT("Uses its own open terrain without a central crossing"),
            Preset.Terrain != battle::MarchTerrain::OpenField && battle::MarchCrossingsFor(Preset.Terrain).empty());

        Terrains.Add(Preset.Terrain);
        const auto Opening = MakeMissionOpening(Preset);
        const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
        TestEqual(TEXT("Deploys the authored regiments"), int32(Scenario.regiments.size()), Case.RegimentCount);
        auto World = battle::CreateWorld(Scenario, 42);
        if (TestTrue(TEXT("BattleCore accepts the scenario"), bool(World)))
        {
            const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
            bool bContact = false;
            for (int32 Tick = 0; Tick < 420 && !bContact; ++Tick)
            {
                const auto Frame = battle::Step(World.value, {}, {}, Navigation);
                if (!Frame)
                    break;
                for (const auto& Event : Frame.value.events)
                    bContact |= (Event.kind == battle::EventKind::Hit && !Event.projectile);
            }
            TestTrue(Item->Name + TEXT(" enters melee within 21 simulated seconds"), bContact);
        }

        Preset.Name += TEXT(" — проверка копии");
        FGuid Saved;
        FText Error;
        FMissionPreset Copy;
        if (TestTrue(TEXT("Can save a copy of the historical mission"),
                RegimentMissionPresets::SaveCopy(Folder.Path, Preset, Saved, Error)) &&
            TestTrue(TEXT("Can reopen its copy"), RegimentMissionPresets::Load(Folder.Path, Saved, Copy, Error)))
        {
            TestEqual(TEXT("The copy retains its terrain"), int32(Copy.Terrain), int32(Preset.Terrain));
            TestTrue(TEXT("The copy retains its sources"), Copy.Sources == Preset.Sources);
        }
    }
    TestEqual(TEXT("Each historic battlefield has a distinct terrain"), Terrains.Num(), 2);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionPresetFieldSizeTest, "RegimentBattle.MissionPresets.FieldSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionPresetFieldSizeTest::RunTest(const FString& Parameters)
{
    const FMissionPresetTestFolder Folder;
    const FGuid Id = FGuid::NewGuid();
    const TCHAR* Details = TEXT(",\"objective\":{\"goal\":\"Eliminate\",\"time_limit_seconds\":90,\"crossing\":1,\"required_regiments\":1,\"crossing_team\":0},\"briefing\":\"\",\"sources\":[],\"direct_deployment\":true");
    const auto FixtureWithField = [Details](const TCHAR* Width, const TCHAR* Height, const TCHAR* Coordinates)
    {
        const FString Regiments = FString(TEXT(",\"enabled\":true,\"group\":1")) + Coordinates;
        const FString Fields = FString::Printf(TEXT("%s,\"field\":{\"width_m\":%s,\"height_m\":%s}"), Details, Width, Height);
        return PresetFixture(2, *Regiments, *Fields);
    };

    FMissionPreset Loaded;
    FText Error;
    if (!TestTrue(TEXT("Writes a large field with larger coordinates"),
            Folder.WriteFixture(Id, *FixtureWithField(TEXT("240"), TEXT("160"), TEXT(",\"start_x\":95,\"start_y\":-61")))) ||
        !TestTrue(TEXT("Loads coordinates valid on a larger field"), RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error)))
        return false;
    TestEqual(TEXT("Loads field width"), Loaded.Field.width_m, 240);
    TestEqual(TEXT("Loads field depth"), Loaded.Field.height_m, 160);
    TestEqual(TEXT("Preserves the larger X coordinate"), Loaded.Roster[0].StartX.value_or(0.f), 95.f);
    TestEqual(TEXT("Preserves the larger Y coordinate"), Loaded.Roster[0].StartY.value_or(0.f), -61.f);

    FGuid CopyId;
    Loaded.Name = TEXT("Large field copy");
    if (!TestTrue(TEXT("Makes an editable copy"), RegimentMissionPresets::SaveCopy(Folder.Path, Loaded, CopyId, Error)) ||
        !TestTrue(TEXT("Reopens the copy"), RegimentMissionPresets::Load(Folder.Path, CopyId, Loaded, Error)))
        return false;
    TestEqual(TEXT("Round-trips field width"), Loaded.Field.width_m, 240);
    TestEqual(TEXT("Round-trips field depth"), Loaded.Field.height_m, 160);
    TestEqual(TEXT("Round-trips coordinates beyond the old field"),
        Loaded.Roster[0].StartX.value_or(0.f), 95.f);

    const FString InvalidFixtures[] = {
        FixtureWithField(TEXT("79"), TEXT("76"), TEXT("")),
        FixtureWithField(TEXT("120"), TEXT("701"), TEXT("")),
        FixtureWithField(TEXT("120.5"), TEXT("76"), TEXT("")),
        FixtureWithField(TEXT("80"), TEXT("60"), TEXT(",\"start_x\":37")),
        FixtureWithField(TEXT("80"), TEXT("60"), TEXT(",\"start_y\":27")),
    };
    for (const FString& Invalid : InvalidFixtures)
    {
        if (!TestTrue(TEXT("Writes an invalid field fixture"), Folder.WriteFixture(Id, *Invalid)))
            return false;
        TestFalse(TEXT("Rejects invalid dimensions or starts outside the resized field"),
            RegimentMissionPresets::Load(Folder.Path, Id, Loaded, Error));
        TestFalse(TEXT("Explains the invalid field"), Error.IsEmpty());
        TestEqual(TEXT("Leaves the loaded field intact"), Loaded.Field.width_m, 240);
    }

    FMissionPreset Compact = Loaded;
    Compact.Name = TEXT("Compact field with an inactive regiment");
    Compact.Field = {80, 60};
    for (int32 Index = 1; Index < MissionRegimentCount; ++Index)
    {
        Compact.Roster[Index].StartX.reset();
        Compact.Roster[Index].StartY.reset();
    }
    Compact.Roster[0].bEnabled = false;
    FGuid CompactId;
    if (!TestTrue(TEXT("A disabled regiment does not block downsizing"),
            RegimentMissionPresets::SaveCopy(Folder.Path, Compact, CompactId, Error)) ||
        !TestTrue(TEXT("Can reopen the downsized mission"), RegimentMissionPresets::Load(Folder.Path, CompactId, Compact, Error)))
        return false;
    TestEqual(TEXT("Disabled regiment retains its authored position"), Compact.Roster[0].StartX.value_or(0.f), 95.f);
    Compact.Roster[0].bEnabled = true;
    TestFalse(TEXT("Re-enabling requires moving the regiment onto the smaller field"), RegimentMissionPresets::Validate(Compact, Error));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHillHoldPresetTest, "RegimentBattle.MissionPresets.HillHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHillHoldPresetTest::RunTest(const FString&)
{
    constexpr auto Goal = EMissionGoal::HillHold;
    const FGuid Builtin(0xA16D79C4, 0xB2474F66, 0x98D7143E, 0x622FCE91);
    FMissionPreset Preset;
    FText Error;
    if (!TestTrue(TEXT("Loads the built-in height exercise"), RegimentMissionPresets::LoadBuiltIn(Builtin, Preset)))
        return false;
    TestEqual(TEXT("The exercise uses the hill map"), Preset.Terrain, battle::MarchTerrain::Hill);
    TestEqual(TEXT("The exercise uses the hill objective"), Preset.Objective.Goal, Goal);
    TestEqual(TEXT("The exercise allows 90 seconds"), Preset.Objective.TimeLimitSeconds, 90);
    TestTrue(TEXT("The exercise starts from the authored deployment"), Preset.StartsInBattle());

    const FMissionPresetTestFolder Folder;
    FGuid Copy;
    Preset.Name = TEXT("Hill exercise fixture");
    if (!RegimentMissionPresets::SaveCopy(Folder.Path, Preset, Copy, Error))
    {
        AddError(FString::Printf(TEXT("The hill exercise can be saved: %s"), *Error.ToString()));
        return false;
    }
    FMissionPreset Loaded;
    if (!RegimentMissionPresets::Load(Folder.Path, Copy, Loaded, Error))
    {
        AddError(FString::Printf(TEXT("The hill copy can be reopened: %s"), *Error.ToString()));
        return false;
    }
    TestEqual(TEXT("Goal round trips"), Loaded.Objective.Goal, Preset.Objective.Goal);
    TestEqual(TEXT("Terrain round trips"), Loaded.Terrain, Preset.Terrain);
    TestEqual(TEXT("Deadline round trips"), Loaded.Objective.TimeLimitSeconds, Preset.Objective.TimeLimitSeconds);
    const auto Opening = MakeMissionOpening(Loaded.Roster, Loaded.Terrain, Loaded.Field);
    const auto Scenario = BuildPresetBattle(Loaded, Opening, EMissionDeployment::AuthoredStart);
    if (TestTrue(TEXT("Hill crest becomes an army objective"), Scenario.objective.has_value()))
    {
        const auto& Objective = *Scenario.objective;
        TestTrue(TEXT("Army objective is at the playable hill crest"),
            Scenario.hill && battle::OnHillCrest(*Scenario.hill, Objective.center) &&
            Objective.center.z == 0.f);
        TestEqual(TEXT("Orange defends the crest against the blue hold objective"), Objective.defending_team, 1);
        for (const auto& Mission : Scenario.command_setup->missions)
            TestTrue(TEXT("Army task uses the crest and radius"),
                Mission.intent.objective_position == Objective.center &&
                Mission.intent.objective_radius_m == Objective.radius_m);
    }
    Loaded.Terrain = battle::MarchTerrain::OpenField;
    TestFalse(TEXT("The hill goal requires the hill map"), RegimentMissionPresets::Validate(Loaded, Error));
    Loaded.Terrain = Preset.Terrain;
    Loaded.Objective.TimeLimitSeconds = 19;
    TestFalse(TEXT("A deadline shorter than the required hold is invalid"), RegimentMissionPresets::Validate(Loaded, Error));
    Loaded.Objective = {};
    TestTrue(TEXT("The hill terrain can be used with a different objective"), RegimentMissionPresets::Validate(Loaded, Error));
    return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAncientSpecialistPresetTest, "RegimentBattle.MissionPresets.AncientSpecialists",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAncientSpecialistPresetTest::RunTest(const FString&)
{
    const FMissionPresetTestFolder Folder;
    const FGuid Exercise(0x6C326E44, 0xB6484ABB, 0x893D83EE, 0xDB4F9D6F);
    const auto Choices = RegimentGameModules::Presets(TEXT("ancient"), Folder.Path);
    const auto* Choice = Choices.FindByPredicate([&](const auto& Item) { return Item.Id == Exercise; });
    if (!TestNotNull(TEXT("Ancient missions include the specialist exercise"), Choice))
        return false;

    FMissionPreset Preset;
    FText Error;
    if (!TestTrue(TEXT("The exercise launches from the Ancient module"),
            RegimentGameModules::Load(TEXT("ancient"), Folder.Path, *Choice, Preset, Error)))
        return false;
    TestTrue(TEXT("Starts from the test positions"), Preset.StartsInBattle());
    using enum battle::BattlePlatform;
    const battle::BattlePlatform Expected[] =
        {None, IndianElephant, ScythedChariot, Catapult, Ballista, None, None, AfricanElephant};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expected); ++Index)
        TestTrue(TEXT("The demonstration fields the intended platform"), Preset.Roster[Index].Platform == Expected[Index]);
    TestTrue(TEXT("The exercise roster is valid"), RegimentMissionPresets::Validate(Preset, Error));

    Preset.Name = TEXT("Ancient specialist test copy");
    Preset.Roster[1].PlatformCount = 6;
    FGuid Copy;
    if (!TestTrue(TEXT("Saves a copy with specialist units"),
            RegimentMissionPresets::SaveCopy(Folder.Path, Preset, Copy, Error)))
        return false;
    FMissionPreset Loaded;
    if (!TestTrue(TEXT("Reloads the specialist copy"), RegimentMissionPresets::Load(Folder.Path, Copy, Loaded, Error)))
        return false;
    TestTrue(TEXT("Keeps the platform"), Loaded.Roster[1].Platform == IndianElephant);
    TestEqual(TEXT("Keeps the platform count"), Loaded.Roster[1].PlatformCount, 6);
    TestEqual(TEXT("Keeps both support staff and platforms in the total"), Loaded.Roster[1].Total(), 9);

    Loaded.Roster[1].PlatformCount = 7;
    TestFalse(TEXT("Rejects an oversized platform detachment"), RegimentMissionPresets::Validate(Loaded, Error));
    Loaded.Roster[1].PlatformCount = 6;
    Loaded.Roster[1].Mounted = 5;
    TestFalse(TEXT("Rejects ordinary troops mixed into a platform detachment"),
        RegimentMissionPresets::Validate(Loaded, Error));
    Loaded.Roster[1].Mounted = 0;
    Loaded.Era.BlueFaction = TEXT("rome");
    TestFalse(TEXT("Rejects a platform unavailable to the faction"), RegimentMissionPresets::Validate(Loaded, Error));
    Loaded.Era.BlueFaction = TEXT("persia");

    for (auto& Entry : Loaded.Roster)
        if (Entry.Platform == None)
            Entry.bEnabled = false;
    Loaded.FigureTarget = MissionFigureCount(Loaded.Roster);
    TestTrue(TEXT("Platform-only armies can use their unscaled count"), RegimentMissionPresets::Validate(Loaded, Error));
    ++Loaded.FigureTarget;
    TestFalse(TEXT("Rejects scaling an army without ordinary troops"), RegimentMissionPresets::Validate(Loaded, Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHistoricalInfantryTest, "RegimentBattle.MissionPresets.HistoricalInfantry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHistoricalInfantryTest::RunTest(const FString& Parameters)
{
    using namespace battle;
    FMissionPreset Zama;
    if (!TestTrue(TEXT("Zama loads"), RegimentMissionPresets::LoadBuiltIn(
        FGuid(0x03BAE43F, 0x906A4969, 0xAD2D5FE8, 0x5C976925), Zama)))
        return false;
    const auto Opening = MakeMissionOpening(Zama);
    const auto Scenario = BuildPresetBattle(Zama, Opening, EMissionDeployment::AuthoredStart);
    TestEqual(TEXT("Zama preserves its figure budget"), int32(Scenario.units.size()), Zama.FigureTarget);
    const auto World = CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("Starts Zama with historical troop profiles"), bool(World)))
        return false;
    const auto Legionary = std::ranges::find_if(Scenario.units, [](const UnitSpec& Unit)
    {
        return Unit.regiment == RegimentId{1} && Unit.ranged_attack.has_value();
    });
    if (TestTrue(TEXT("Legionaries carry a separate ranged attack"), Legionary != Scenario.units.end()))
    {
        TestFalse(TEXT("Pilum does not turn a legionary into an archer"), Legionary->archer);
        TestTrue(TEXT("Legionaries throw pila"), Legionary->ranged_weapon == RangedWeapon::Javelin);
        TestEqual(TEXT("Legionaries carry two pila"), Legionary->ammunition, 2u);
        TestTrue(TEXT("Melee and pilum have independent weapon stats"), Legionary->weapon.range < Legionary->ranged_attack->range);
    }
    const auto* Slingers = FindRegiment(World.value, RegimentId{9});
    if (TestNotNull(TEXT("A complete slinger regiment deploys"), Slingers))
    {
        TestTrue(TEXT("Slingers deploy in loose order"), Slingers->loose_skirmish);
        TestTrue(TEXT("A loose-order deployment reserves wider spaces"), Slingers->formation.spacing_m >= 1.8f);
        TestEqual(TEXT("The loose regiment has no additional detached screen"), Slingers->skirmisher_count, 0u);
        const auto MarchSlingers = std::ranges::find(Opening, Slingers->id, &MarchRegiment::id);
        if (TestTrue(TEXT("The marching representation includes the loose regiment"), MarchSlingers != Opening.end()))
        {
            TestEqual(TEXT("March and battle agree on the loose spacing"), MarchSlingers->formation.spacing_m,
                Slingers->formation.spacing_m);
            TestEqual(TEXT("March and battle agree on troop strength"), int32(MarchSlingers->figure_count),
                int32(Slingers->units.size()));
        }
    }
    TestTrue(TEXT("A separate regiment uses throwing javelins"), std::ranges::any_of(Scenario.units, [](const UnitSpec& Unit)
    {
        return Unit.regiment == RegimentId{10} && Unit.archer && Unit.ranged_weapon == RangedWeapon::Javelin;
    }));

    const FMissionPresetTestFolder Folder;
    FGuid Copy;
    FText Error;
    Zama.Name = TEXT("Зама · проверочная копия");
    if (!TestTrue(TEXT("Can save a historical formation as a custom copy"),
        RegimentMissionPresets::SaveCopy(Folder.Path, Zama, Copy, Error)))
    {
        AddError(Error.ToString());
        return false;
    }
    FMissionPreset Reloaded;
    if (!TestTrue(TEXT("Can reopen the copy"), RegimentMissionPresets::Load(Folder.Path, Copy, Reloaded, Error)))
    {
        AddError(Error.ToString());
        return false;
    }
    TestEqual(TEXT("Keeps the legionary profile"), Reloaded.Roster[0].InfantryProfile, FString(TEXT("legionaries")));
    TestEqual(TEXT("Keeps the loose slinger profile"), Reloaded.Roster[8].InfantryProfile, FString(TEXT("loose_sling")));
    Reloaded.Roster[0].InfantryProfile = TEXT("highlanders");
    TestFalse(TEXT("Rejects a profile outside its faction and era"), RegimentMissionPresets::Validate(Reloaded, Error));

    FMissionPreset Waterloo;
    if (!TestTrue(TEXT("Waterloo loads"), RegimentMissionPresets::LoadBuiltIn(
        FGuid(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33), Waterloo)))
        return false;
    TestEqual(TEXT("Waterloo has a Highland regiment"), Waterloo.Roster[8].InfantryProfile, FString(TEXT("highlanders")));
    auto& Highlanders = Waterloo.Roster[8];
    Highlanders.Swords += Highlanders.Archers;
    Highlanders.Archers = 0;
    Highlanders.FormationFiles = 0;
    const auto WaterlooOpening = MakeMissionOpening(Waterloo);
    const auto WaterlooScenario = BuildPresetBattle(Waterloo, WaterlooOpening, EMissionDeployment::AuthoredStart);
    TestEqual(TEXT("Waterloo preserves its figure budget"), int32(WaterlooScenario.units.size()), Waterloo.FigureTarget);
    TestTrue(TEXT("Highlanders use muskets"), std::ranges::any_of(WaterlooScenario.units, [](const UnitSpec& Unit)
    {
        return Unit.regiment == RegimentId{9} && Unit.archer && Unit.ranged_weapon == RangedWeapon::Musket;
    }));
    const auto HighlanderFormation = std::ranges::find(WaterlooScenario.regiments, RegimentId{9}, &RegimentSpec::id);
    if (TestTrue(TEXT("The Highlanders deploy"), HighlanderFormation != WaterlooScenario.regiments.end()))
    {
        const auto MarchHighlanders = std::ranges::find(WaterlooOpening, RegimentId{9}, &MarchRegiment::id);
        if (TestTrue(TEXT("The Highlander figure count is available"), MarchHighlanders != WaterlooOpening.end()))
            TestEqual(TEXT("The named musket profile selects a firing line despite the legacy counts"),
                HighlanderFormation->formation.columns,
                LinearInfantryFormation(HighlanderFormation->formation, MarchHighlanders->figure_count, true).columns);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpandedBattalionPresetTest, "RegimentBattle.MissionPresets.ExpandedBattalions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpandedBattalionPresetTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!TestTrue(TEXT("Loads the expanded Waterloo scenario"), RegimentMissionPresets::LoadBuiltIn(
        FGuid(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33), Preset)))
        return false;
    TestTrue(TEXT("The enlarged map reserves space beyond the original field"),
        Preset.Field.width_m * Preset.Field.height_m >= 560 * 380 * 19 / 10);
    const auto Opening = MakeMissionOpening(Preset);
    const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    TestEqual(TEXT("Waterloo has 24 independent battalions and batteries"), int32(Scenario.regiments.size()), 24);
    TestEqual(TEXT("The total stays at five thousand figures"), int32(Scenario.units.size()), 5000);
    const auto World = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("The expanded command tree is valid"), bool(World)))
        return false;
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const auto LastBattalion = battle::RegimentId{uint64(MissionRosterIndex(Team, 11) + 1)};
        const auto* Found = battle::FindRegiment(World.value, LastBattalion);
        if (TestNotNull(TEXT("The last battalion is present"), Found))
            TestEqual(TEXT("Expanded battalions retain the correct team"), Found->team, Team);
    }
    const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
    const float OpenWing = Preset.Field.HalfHeight() - 20.f;
    const battle::Pose MarchStart{{-80.f, OpenWing, 0.f}, 0.f};
    TestTrue(TEXT("The larger playable flank has a path"),
        Navigation.FindPath({MarchStart, {80.f, OpenWing, 0.f}, {6.f, 5.f}}).status == battle::Status::Ok);

    const FMissionPresetTestFolder Folder;
    FGuid Copy;
    FText Error;
    Preset.Name = TEXT("Expanded Waterloo copy");
    if (!TestTrue(TEXT("Saves all twenty-four slots"), RegimentMissionPresets::SaveCopy(Folder.Path, Preset, Copy, Error)) ||
        !TestTrue(TEXT("Loads the expanded copy"), RegimentMissionPresets::Load(Folder.Path, Copy, Preset, Error)))
    {
        AddError(Error.ToString());
        return false;
    }
    TestEqual(TEXT("Records the enabled reserve slots"), MissionRosterExtent(Preset.Roster), 24);
    const auto ExtraStart = Preset.Roster[MissionRegimentCount].StartX;
    for (int32 Slot = MissionRegimentCount; Slot < MissionRosterCapacity; ++Slot)
        Preset.Roster[Slot].bEnabled = false;
    if (TestTrue(TEXT("Can suspend all extra battalions"), RegimentMissionPresets::Save(Folder.Path, Copy, Preset, Error)) &&
        TestTrue(TEXT("Can reopen suspended extra battalions"), RegimentMissionPresets::Load(Folder.Path, Copy, Preset, Error)))
    {
        TestEqual(TEXT("Suspended extra battalions stay disabled"), MissionRosterExtent(Preset.Roster), MissionRegimentCount);
        TestTrue(TEXT("Suspended battalions keep their authored positions"),
            ExtraStart && Preset.Roster[MissionRegimentCount].StartX == ExtraStart);
    }
    return !HasAnyErrors();
}

#endif
