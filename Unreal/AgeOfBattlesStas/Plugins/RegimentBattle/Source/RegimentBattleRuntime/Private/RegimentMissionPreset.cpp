#include "RegimentMissionPreset.h"
#include "RegimentEraCatalog.h"
#include "RegimentMissionScenario.h"

#include "Algo/Find.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Internationalization/TextComparison.h"
#include "Misc/Char.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Windows/WindowsHWrapper.h"

#define LOCTEXT_NAMESPACE "RegimentMissionPresets"

namespace
{
constexpr int32 LegacyPresetSchemaVersion = 1;
constexpr int32 DetailedPresetSchemaVersion = 2;
constexpr int32 EraPresetSchemaVersion = 3;
constexpr int32 ProfilePresetSchemaVersion = 4;
constexpr int32 PlatformPresetSchemaVersion = 5;
constexpr int32 ExpandedRosterSchemaVersion = 6;
constexpr int32 MaxPresetNameLength = 80;
constexpr int64 MaxPresetFileSize = 64 * 1024;
constexpr battle::MarchField MaxMissionField{MissionMaxFieldWidth, MissionMaxFieldHeight};

struct FPresetTerrain
{
    battle::MarchTerrain Terrain;
    const TCHAR* Value;
};

constexpr FPresetTerrain PresetTerrains[] =
{
    {battle::MarchTerrain::River, TEXT("River")},
    {battle::MarchTerrain::OpenField, TEXT("OpenField")},
    {battle::MarchTerrain::StoneWalls, TEXT("StoneWalls")},
    {battle::MarchTerrain::StirlingBridge, TEXT("StirlingBridge")},
    {battle::MarchTerrain::Cannae, TEXT("Cannae")},
    {battle::MarchTerrain::Peipus, TEXT("Peipus")},
    {battle::MarchTerrain::Hill, TEXT("Hill")},
    {battle::MarchTerrain::Waterloo, TEXT("Waterloo")},
    {battle::MarchTerrain::Sekigahara, TEXT("Sekigahara")},
    {battle::MarchTerrain::Agincourt, TEXT("Agincourt")},
    {battle::MarchTerrain::Pavia, TEXT("Pavia")},
    {battle::MarchTerrain::Poltava, TEXT("Poltava")},
    {battle::MarchTerrain::Nagashino, TEXT("Nagashino")},
    {battle::MarchTerrain::Gaugamela, TEXT("Gaugamela")},
    {battle::MarchTerrain::Zama, TEXT("Zama")},
    {battle::MarchTerrain::Kalka, TEXT("Kalka")},
    {battle::MarchTerrain::Dorylaeum, TEXT("Dorylaeum")},
    {battle::MarchTerrain::Grunwald, TEXT("Grunwald")},
    {battle::MarchTerrain::Crecy, TEXT("Crecy")},
    {battle::MarchTerrain::Breitenfeld, TEXT("Breitenfeld")},
    {battle::MarchTerrain::Lutzen, TEXT("Lutzen")},
    {battle::MarchTerrain::Kawanakajima, TEXT("Kawanakajima")},
    {battle::MarchTerrain::Narva, TEXT("Narva")},
    {battle::MarchTerrain::Lesnaya, TEXT("Lesnaya")},
    {battle::MarchTerrain::Austerlitz, TEXT("Austerlitz")},
    {battle::MarchTerrain::Borodino, TEXT("Borodino")},
};

struct FPresetGoal
{
    EMissionGoal Goal;
    const TCHAR* Value;
};

constexpr FPresetGoal PresetGoals[] =
{
    {EMissionGoal::Eliminate, TEXT("Eliminate")},
    {EMissionGoal::Crossing, TEXT("Crossing")},
    {EMissionGoal::ReserveExercise, TEXT("ReserveExercise")},
    {EMissionGoal::HillHold, TEXT("HillHold")},
    {EMissionGoal::Breakthrough, TEXT("Breakthrough")},
};

struct FPresetCount
{
    const TCHAR* Key;
    int32 FMissionRosterEntry::* Member;
    int32 Maximum;
};

constexpr FPresetCount PresetCounts[] =
{
    {TEXT("swords"), &FMissionRosterEntry::Swords, MissionMaxSwords},
    {TEXT("archers"), &FMissionRosterEntry::Archers, MissionMaxArchers},
    {TEXT("spearmen"), &FMissionRosterEntry::Spearmen, MissionMaxSpecialists},
    {TEXT("mounted"), &FMissionRosterEntry::Mounted, MissionMaxSpecialists},
};

FString PresetFilename(const FString& Directory, FGuid Id)
{
    return Directory / (Id.ToString(EGuidFormats::Digits) + TEXT(".json"));
}

bool IsPresetNameValid(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > MaxPresetNameLength)
        return false;

    for (const TCHAR Character : Name)
    {
        if (FChar::IsControl(Character))
            return false;
    }
    return true;
}

TSharedPtr<FJsonObject> ReadPresetJson(const FString& Filename)
{
    const int64 Size = IFileManager::Get().FileSize(*Filename);
    FString Contents;
    TSharedPtr<FJsonObject> Object;
    if (Size <= 0 || Size > MaxPresetFileSize || !FFileHelper::LoadFileToString(Contents, *Filename) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Object))
        return nullptr;
    return Object;
}

bool ReadPresetName(const FJsonObject& Object, FString& Name)
{
    if (!Object.HasTypedField<EJson::String>(TEXT("name")) || !Object.TryGetStringField(TEXT("name"), Name))
        return false;
    Name.TrimStartAndEndInline();
    return IsPresetNameValid(Name);
}

bool ReadWholeNumber(const FJsonObject& Object, const TCHAR* Key, int32 Maximum, int32& Result)
{
    double Number;
    if (!Object.HasTypedField<EJson::Number>(Key) || !Object.TryGetNumberField(Key, Number) ||
        !FMath::IsFinite(Number) || Number < 0.0 || Number > Maximum || Number != FMath::TruncToDouble(Number))
        return false;
    Result = static_cast<int32>(Number);
    return true;
}

bool IsFieldSizeValid(const battle::MarchField& Field)
{
    return Field.width_m >= MissionMinFieldWidth && Field.width_m <= MissionMaxFieldWidth &&
        Field.height_m >= MissionMinFieldHeight && Field.height_m <= MissionMaxFieldHeight;
}

bool ReadFieldSize(const FJsonObject& Root, battle::MarchField& Field)
{
    if (!Root.HasField(TEXT("field")))
        return true;

    const TSharedPtr<FJsonObject>* Object;
    return Root.TryGetObjectField(TEXT("field"), Object) && *Object &&
        ReadWholeNumber(**Object, TEXT("width_m"), MissionMaxFieldWidth, Field.width_m) &&
        ReadWholeNumber(**Object, TEXT("height_m"), MissionMaxFieldHeight, Field.height_m) &&
        IsFieldSizeValid(Field);
}

FText FieldSizeError()
{
    return FText::Format(LOCTEXT("InvalidFieldSize",
        "Укажите ширину поля от {0} до {1} м и глубину от {2} до {3} м целыми числами."),
        MissionMinFieldWidth, MissionMaxFieldWidth, MissionMinFieldHeight, MissionMaxFieldHeight);
}

bool InfantryFitsRegiment(const FMissionRosterEntry& Entry)
{
    // The inactive counts are editable again after switching modes.
    return Entry.Swords + Entry.Archers + Entry.Spearmen <= MissionMaxSpecialists;
}

bool ReadOptionalCoordinate(const FJsonObject& Object, const TCHAR* Key, float Limit, std::optional<float>& Result)
{
    if (!Object.HasField(Key))
    {
        Result.reset();
        return true;
    }
    double Coordinate;
    if (!Object.HasTypedField<EJson::Number>(Key) || !Object.TryGetNumberField(Key, Coordinate) ||
        !FMath::IsFinite(Coordinate) || FMath::Abs(Coordinate) > Limit)
        return false;
    Result = static_cast<float>(Coordinate);
    return true;
}

template <typename E, size_t N>
bool ReadProfileClass(const FJsonObject& Object, const TCHAR* Key, const TCHAR* const (&Names)[N], E& Result)
{
    if (!Object.HasField(Key))
        return true;
    FString Name;
    if (!Object.HasTypedField<EJson::String>(Key) || !Object.TryGetStringField(Key, Name))
        return false;
    for (int32 Index = 0; Index < int32(N); ++Index)
    {
        if (Name.Equals(Names[Index], ESearchCase::CaseSensitive))
        {
            Result = static_cast<E>(Index);
            return true;
        }
    }
    return false;
}

bool HasSelectedTroopClass(const FMissionRosterEntry& Entry)
{
    return Entry.MountedClass != ERegimentMountClass::FactionDefault ||
        Entry.RangedClass != ERegimentRangedClass::FactionDefault || !Entry.InfantryProfile.IsEmpty();
}

bool ReadRosterEntry(const TSharedPtr<FJsonValue>& Value, int32 Version, FMissionRosterEntry& Entry,
    const TCHAR*& Field)
{
    Field = TEXT("type");
    if (!Value || Value->Type != EJson::Object)
        return false;

    const auto& Object = Value->AsObject();
    FString Type;
    if (!Object || !Object->HasTypedField<EJson::String>(TEXT("type")) || !Object->TryGetStringField(TEXT("type"), Type))
        return false;
    if (Type.Equals(TEXT("Cavalry"), ESearchCase::CaseSensitive))
        Entry.bCavalry = true;
    else if (Type.Equals(TEXT("Infantry"), ESearchCase::CaseSensitive))
        Entry.bCavalry = false;
    else if (Version >= PlatformPresetSchemaVersion && Type.Equals(TEXT("Platform"), ESearchCase::CaseSensitive))
    {
        Entry.bCavalry = false;
        Field = TEXT("platform");
        if (!ReadProfileClass(*Object, Field, RegimentEraCatalog::PlatformIds, Entry.Platform) ||
            Entry.Platform == battle::BattlePlatform::None)
            return false;
        Field = TEXT("platform_count");
        if (!ReadWholeNumber(*Object, Field, MissionMaxPlatforms, Entry.PlatformCount) ||
            Entry.PlatformCount < 1)
            return false;
    }
    else
        return false;

    if (Entry.Platform == battle::BattlePlatform::None &&
        (Object->HasField(TEXT("platform")) || Object->HasField(TEXT("platform_count"))))
    {
        Field = Object->HasField(TEXT("platform")) ? TEXT("platform") : TEXT("platform_count");
        return false;
    }
    Field = TEXT("infantry_profile");
    if (Object->HasField(Field) &&
        (Version < ProfilePresetSchemaVersion || !Object->HasTypedField<EJson::String>(Field) ||
            !Object->TryGetStringField(Field, Entry.InfantryProfile) ||
            Entry.InfantryProfile.IsEmpty() || Entry.InfantryProfile.Len() > 64))
        return false;

    for (const FPresetCount& Count : PresetCounts)
    {
        Field = Count.Key;
        if (!ReadWholeNumber(*Object, Field, Count.Maximum, Entry.*Count.Member))
            return false;
    }
    if (Version == LegacyPresetSchemaVersion)
        return true;
    if (Version >= ProfilePresetSchemaVersion)
    {
        Field = TEXT("mounted_class");
        if (!ReadProfileClass(*Object, Field, RegimentEraCatalog::MountedClassIds, Entry.MountedClass))
            return false;
        Field = TEXT("ranged_class");
        if (!ReadProfileClass(*Object, Field, RegimentEraCatalog::RangedClassIds, Entry.RangedClass))
            return false;
    }

    Field = TEXT("enabled");
    if (!Object->HasTypedField<EJson::Boolean>(Field) || !Object->TryGetBoolField(Field, Entry.bEnabled))
        return false;
    Field = TEXT("group");
    if (!ReadWholeNumber(*Object, Field, MissionGroupsPerTeam, Entry.Group) || Entry.Group < 1)
        return false;
    Field = TEXT("formation_files");
    if (Object->HasField(Field) && !ReadWholeNumber(*Object, Field, MissionMaxFormationFiles, Entry.FormationFiles))
        return false;
    Field = TEXT("start_x");
    if (!ReadOptionalCoordinate(*Object, Field, MaxMissionField.HalfWidth() - MissionStartInset, Entry.StartX))
        return false;
    Field = TEXT("start_y");
    return ReadOptionalCoordinate(*Object, Field, MaxMissionField.HalfHeight() - MissionStartInset, Entry.StartY);
}

bool ReadVersionTwoFields(const FJsonObject& Root, FMissionPreset& Preset, FString& Field)
{
    const TSharedPtr<FJsonObject>* ObjectiveObject;
    const TArray<TSharedPtr<FJsonValue>>* Sources;
    Field = TEXT("briefing");
    if (!Root.HasTypedField<EJson::String>(*Field) || !Root.TryGetStringField(*Field, Preset.Briefing))
        return false;
    Field = TEXT("sources");
    if (!Root.TryGetArrayField(*Field, Sources))
        return false;
    Field = TEXT("objective");
    if (!Root.TryGetObjectField(*Field, ObjectiveObject) || !*ObjectiveObject)
        return false;

    const FJsonObject& Object = **ObjectiveObject;
    FString Goal;
    FMissionObjective& Objective = Preset.Objective;
    Field = TEXT("objective.goal");
    if (!Object.HasTypedField<EJson::String>(TEXT("goal")) || !Object.TryGetStringField(TEXT("goal"), Goal))
        return false;

    const auto* GoalMatch = Algo::FindByPredicate(PresetGoals, [&Goal](const FPresetGoal& Candidate)
    {
        return Goal.Equals(Candidate.Value, ESearchCase::CaseSensitive);
    });
    if (!GoalMatch)
        return false;
    Objective.Goal = GoalMatch->Goal;

    const auto ReadObjectiveNumber = [&Object, &Field](const TCHAR* Key, int32 Maximum, int32& Value)
    {
        Field = FString(TEXT("objective.")) + Key;
        return ReadWholeNumber(Object, Key, Maximum, Value);
    };
    if (!ReadObjectiveNumber(TEXT("time_limit_seconds"), MissionMaxTimeLimit, Objective.TimeLimitSeconds) ||
        !ReadObjectiveNumber(TEXT("crossing"), int32(battle::MarchCrossings.size()) - 1, Objective.Crossing) ||
        !ReadObjectiveNumber(TEXT("required_regiments"), MissionMaxRegimentsPerTeam, Objective.RequiredRegiments) ||
        !ReadObjectiveNumber(TEXT("crossing_team"), 1, Objective.CrossingTeam))
        return false;

    if (Objective.Goal == EMissionGoal::Breakthrough)
    {
        Field = TEXT("objective.breakthrough");
        const TSharedPtr<FJsonObject>* ZoneObject;
        if (!Object.TryGetObjectField(TEXT("breakthrough"), ZoneObject) || !*ZoneObject)
            return false;
        const auto ReadZoneNumber = [&ZoneObject, &Field](const TCHAR* Key, float& Value)
        {
            Field = FString(TEXT("objective.breakthrough.")) + Key;
            std::optional<float> Parsed;
            if (!ReadOptionalCoordinate(**ZoneObject, Key, MissionMaxFieldWidth, Parsed) || !Parsed)
                return false;
            Value = *Parsed;
            return true;
        };
        FMissionBreakthrough Zone;
        if (!ReadZoneNumber(TEXT("x_m"), Zone.X) || !ReadZoneNumber(TEXT("y_m"), Zone.Y) ||
            !ReadZoneNumber(TEXT("radius_m"), Zone.Radius))
            return false;
        Field = TEXT("objective.breakthrough.group");
        if (!ReadWholeNumber(**ZoneObject, TEXT("group"), MissionGroupsPerTeam, Zone.Group))
            return false;
        Objective.Breakthrough = Zone;
    }

    const auto ReadOption = [&Root, &Field](const TCHAR* Key, bool& Value)
    {
        Field = Key;
        return !Root.HasField(Key) || (Root.HasTypedField<EJson::Boolean>(Key) && Root.TryGetBoolField(Key, Value));
    };
    if (!ReadOption(TEXT("regiment_tactics"), Preset.bRegimentTactics) ||
        !ReadOption(TEXT("risky_volley"), Preset.bRiskyVolley) ||
        !ReadOption(TEXT("direct_deployment"), Preset.bDirectDeployment) ||
        !ReadOption(TEXT("adaptive_battle"), Preset.bAdaptiveBattle) ||
        !ReadOption(TEXT("smoke_exercise"), Preset.bSmokeExercise))
        return false;
    Field = TEXT("figure_target");
    if (Root.HasField(*Field) && !ReadWholeNumber(Root, *Field, MissionMaxFigureTarget, Preset.FigureTarget))
        return false;

    for (int32 Index = 0; Index < Sources->Num(); ++Index)
    {
        const auto& Source = (*Sources)[Index];
        if (!Source || Source->Type != EJson::String)
        {
            Field = FString::Printf(TEXT("sources[%d]"), Index);
            return false;
        }
        Preset.Sources.Add(Source->AsString());
    }
    return true;
}

bool ReadEraSelection(const FJsonObject& Root, FMissionEraSelection& Selection)
{
    const TSharedPtr<FJsonObject>* EraObject;
    if (!Root.TryGetObjectField(TEXT("era"), EraObject) || !*EraObject)
        return false;

    const FJsonObject& Object = **EraObject;
    const auto ReadId = [&Object](const TCHAR* Key, FString& Id)
    {
        return Object.HasTypedField<EJson::String>(Key) && Object.TryGetStringField(Key, Id) && !Id.IsEmpty();
    };
    return ReadId(TEXT("id"), Selection.Id) &&
        ReadWholeNumber(Object, TEXT("version"), MAX_int32, Selection.Version) && Selection.Version > 0 &&
        ReadId(TEXT("blue_faction"), Selection.BlueFaction) &&
        ReadId(TEXT("orange_faction"), Selection.OrangeFaction);
}

bool ValidateEraSelection(const FMissionEraSelection& Selection, FText& OutError)
{
    if (!Selection.IsSet())
    {
        if (Selection.Version == 0 && Selection.BlueFaction.IsEmpty() && Selection.OrangeFaction.IsEmpty())
            return true;

        OutError = LOCTEXT("MissingEraId", "Укажите эпоху для выбранных фракций.");
        return false;
    }

    const FRegimentEraDefinition* Era = RegimentEraCatalog::Find(FStringView(Selection.Id));
    if (!Era || Selection.Version != Era->Version)
    {
        OutError = LOCTEXT("UnavailableEraVersion", "Эпоха или её версия недоступна.");
        return false;
    }
    if (!RegimentEraCatalog::FindFaction(*Era, FStringView(Selection.BlueFaction)) ||
        !RegimentEraCatalog::FindFaction(*Era, FStringView(Selection.OrangeFaction)))
    {
        OutError = LOCTEXT("UnknownEraFaction", "Выберите фракцию этой эпохи для каждой стороны.");
        return false;
    }
    return true;
}

FText TeamName(int32 Team)
{
    return Team == 0 ? LOCTEXT("BlueTeam", "Синие") : LOCTEXT("OrangeTeam", "Оранжевые");
}

FText RosterEntryError(int32 Index, const FText& Message)
{
    return FText::Format(LOCTEXT("InvalidRegiment", "{0}, полк {1}. {2}"),
        TeamName(MissionTeam(Index)), MissionTeamSlot(Index) + 1, Message);
}

FText AtField(const FString& Path, const FText& Message)
{
    return FText::Format(LOCTEXT("AtField", "{0} · {1}"), FText::AsCultureInvariant(Path), Message);
}

FText RosterFieldError(int32 Index, const TCHAR* Field, const FText& Message)
{
    return AtField(FString::Printf(TEXT("regiments[%d].%s"), Index, Field), RosterEntryError(Index, Message));
}
}

void RegimentMissionPresets::ApplyEra(FMissionPreset& Preset, const FRegimentEraDefinition& Era)
{
    Preset.Era = {Era.Id, Era.Version, Era.BlueFaction, Era.OrangeFaction};
    Preset.bRegimentTactics = Era.Rules.regiment_tactics;
    Preset.bRiskyVolley = Era.Rules.risky_volley;
}

bool RegimentMissionPresets::Validate(const FMissionPreset& Preset, FText& OutError)
{
    if (!IsFieldSizeValid(Preset.Field))
    {
        OutError = FieldSizeError();
        return false;
    }
    if (!ValidateEraSelection(Preset.Era, OutError))
        return false;
    if (Preset.bSmokeExercise && (Preset.Era.IsSet() || !Preset.bRegimentTactics || !Preset.StartsInBattle()))
    {
        OutError = LOCTEXT("SmokeExerciseNeedsPracticeRules",
            "Для проверки дыма нужен полигон с включённой полковой тактикой и стартом сразу в бою.");
        return false;
    }

    if (Preset.bRiskyVolley && !Preset.bRegimentTactics)
    {
        OutError = LOCTEXT("RiskyVolleyNeedsTactics", "Стрельба по связанной цели требует полковой тактики.");
        return false;
    }
    if (!Algo::FindBy(PresetTerrains, Preset.Terrain, &FPresetTerrain::Terrain))
    {
        OutError = LOCTEXT("UnknownPresetTerrain", "Карта сохранённой миссии неизвестна.");
        return false;
    }

    const FRegimentEraFaction* Factions[2] = {};
    if (const auto* Era = RegimentEraCatalog::Find(Preset.Era.Id))
    {
        Factions[0] = RegimentEraCatalog::FindFaction(*Era, Preset.Era.BlueFaction);
        Factions[1] = RegimentEraCatalog::FindFaction(*Era, Preset.Era.OrangeFaction);
    }
    const auto Obstacles = battle::MarchObstacles(Preset.Terrain, Preset.Field);
    int32 EnabledPerTeam[2] = {};
    int32 BlueGroups[MissionGroupsPerTeam] = {};
    bool bHasEnabledPlatform = false;
    bool bHasFormationOverride = false;
    for (int32 Index = 0; Index < MissionRosterCapacity; ++Index)
    {
        const FMissionRosterEntry& Entry = Preset.Roster[Index];
        const int32 Team = MissionTeam(Index);
        const auto* Faction = Factions[Team];
        if (Entry.Platform != battle::BattlePlatform::None)
        {
            if (!Faction || !RegimentEraCatalog::PlatformTroop(*Faction, Entry.Platform))
            {
                OutError = RosterFieldError(Index, TEXT("platform"), LOCTEXT("UnavailablePlatform", "Выберите боевую технику, доступную этой фракции."));
                return false;
            }
            if (Entry.PlatformCount < 1 || Entry.PlatformCount > MissionMaxPlatforms || Entry.bCavalry ||
                Entry.Swords != 0 || Entry.Archers != 0 || Entry.Spearmen != 0 || Entry.Mounted != 0 ||
                HasSelectedTroopClass(Entry))
            {
                OutError = RosterFieldError(Index, TEXT("platform_count"), FText::Format(LOCTEXT("InvalidPlatformStrength",
                    "В отряде может быть от 1 до {0} единиц выбранного типа."), MissionMaxPlatforms));
                return false;
            }
        }
        else if (Entry.PlatformCount != 0)
        {
            OutError = RosterFieldError(Index, TEXT("platform"), LOCTEXT("PlatformCountNeedsType", "Укажите тип боевой техники для отряда."));
            return false;
        }
        if (!Entry.InfantryProfile.IsEmpty() &&
            (Entry.bCavalry || !Faction || !Faction->InfantryProfiles.Contains(Entry.InfantryProfile) ||
                Entry.RangedClass != ERegimentRangedClass::FactionDefault))
        {
            OutError = RosterFieldError(Index, TEXT("infantry_profile"),
                LOCTEXT("UnavailableInfantryProfile", "Выберите пеший полк этой фракции или настройте смешанный состав."));
            return false;
        }
        if (HasSelectedTroopClass(Entry) &&
            (!Faction || !RegimentEraCatalog::MountedTroop(*Faction, Entry.MountedClass) ||
             !RegimentEraCatalog::RangedTroop(*Faction, Entry.RangedClass)))
        {
            const bool bInvalidMount = !Faction || !RegimentEraCatalog::MountedTroop(*Faction, Entry.MountedClass);
            const TCHAR* Field = bInvalidMount && Entry.MountedClass != ERegimentMountClass::FactionDefault
                ? TEXT("mounted_class") : TEXT("ranged_class");
            OutError = RosterFieldError(Index, Field, LOCTEXT("UnavailableTroopClass", "Выберите род войск, доступный этой фракции."));
            return false;
        }
        for (const FPresetCount& Count : PresetCounts)
        {
            const int32 Value = Entry.*Count.Member;
            if (Value < 0 || Value > Count.Maximum)
            {
                OutError = RosterFieldError(Index, Count.Key, LOCTEXT("InvalidStrength", "Проверьте численность."));
                return false;
            }
        }
        if (!InfantryFitsRegiment(Entry))
        {
            OutError = RosterEntryError(Index, LOCTEXT("InvalidInfantry", "В полку может быть до 100 фигур с командованием."));
            return false;
        }
        if (Entry.Group < 0 || Entry.Group > MissionGroupsPerTeam)
        {
            OutError = RosterFieldError(Index, TEXT("group"), LOCTEXT("InvalidGroup", "Укажите группу от 1 до 3."));
            return false;
        }
        if (Entry.FormationFiles < 0 || Entry.FormationFiles > MissionMaxFormationFiles ||
            (Entry.FormationFiles > 0 && Entry.Platform != battle::BattlePlatform::None))
        {
            OutError = RosterFieldError(Index, TEXT("formation_files"), LOCTEXT("InvalidFormationFiles",
                "Для пехоты или конницы укажите от 1 до 100 файлов строя, либо 0 для выбора по умолчанию."));
            return false;
        }
        const auto Limit = Entry.bEnabled ? Preset.Field : MaxMissionField;
        const float StartMaxX = Limit.HalfWidth() - MissionStartInset;
        const float StartMaxY = Limit.HalfHeight() - MissionStartInset;
        const bool bInvalidX = Entry.StartX && (!FMath::IsFinite(*Entry.StartX) || FMath::Abs(*Entry.StartX) > StartMaxX);
        const bool bInvalidY = Entry.StartY && (!FMath::IsFinite(*Entry.StartY) || FMath::Abs(*Entry.StartY) > StartMaxY);
        if (bInvalidX || bInvalidY)
        {
            OutError = RosterFieldError(Index, bInvalidX ? TEXT("start_x") : TEXT("start_y"),
                FText::Format(LOCTEXT("InvalidStart", "Координата должна быть числом от {0} до {1} м."),
                    bInvalidX ? -StartMaxX : -StartMaxY, bInvalidX ? StartMaxX : StartMaxY));
            return false;
        }
        if (!Entry.bEnabled)
            continue;

        bHasEnabledPlatform |= Entry.Platform != battle::BattlePlatform::None;
        bHasFormationOverride |= Entry.FormationFiles > 0;
        ++EnabledPerTeam[Team];
        if (Team == 0)
            ++BlueGroups[MissionGroup(Index, Entry) - 1];

        if (Entry.StartX || Entry.StartY)
        {
            const auto Position = MissionStartingPosition(Index, Entry, Preset.Field);
            for (const auto& Obstacle : Obstacles)
            {
                if (Position.x >= Obstacle.min_x && Position.x <= Obstacle.max_x &&
                    Position.y >= Obstacle.min_y && Position.y <= Obstacle.max_y)
                {
                    OutError = RosterEntryError(Index, FText::Format(
                        LOCTEXT("BlockedStart", "Старт ({0}, {1}) м пересекает препятствие. Сдвиньте полк на проходимую местность."),
                        Position.x, Position.y));
                    return false;
                }
            }
        }
    }

    for (int32 Team = 0; Team < 2; ++Team)
    {
        if (EnabledPerTeam[Team] == 0)
        {
            OutError = FText::Format(LOCTEXT("EmptyTeam", "{0}. Включите хотя бы один полк."), TeamName(Team));
            return false;
        }
    }

    const bool bHasTroops = std::ranges::any_of(Preset.Roster, [](const auto& Entry)
        { return Entry.bEnabled && Entry.Total() > MissionCommandStaffCount; });
    const int32 BaseFigures = MissionFigureCount(Preset.Roster);
    if (Preset.FigureTarget < 0 || Preset.FigureTarget > MissionMaxFigureTarget ||
        (Preset.FigureTarget > 0 && (!bHasTroops || Preset.FigureTarget < BaseFigures)))
    {
        OutError = LOCTEXT("InvalidFigureTarget", "Нагрузочная численность должна быть не ниже состава полков и не выше 10 000 фигур.");
        return false;
    }

    if (Preset.FigureTarget > BaseFigures &&
        !std::ranges::any_of(Preset.Roster, [](const auto& Entry)
        {
            return Entry.bEnabled && Entry.Platform == battle::BattlePlatform::None &&
                Entry.Total() > MissionCommandStaffCount;
        }))
    {
        OutError = LOCTEXT("ScalingNeedsOrdinaryTroops", "Для увеличения числа фигур добавьте пехоту или конницу.");
        return false;
    }

    if (Preset.FigureTarget > 0 || bHasEnabledPlatform || bHasFormationOverride)
        if (const auto Invalid = FindInvalidScaledDeployment(Preset))
        {
            OutError = RosterEntryError(*Invalid,
                LOCTEXT("DeploymentBlocked", "Полк не помещается. Измените состав, размер поля или стартовую позицию."));
            return false;
        }

    if (Preset.bRegimentTactics && !Preset.StartsInBattle())
        for (const auto& Company : MakeMissionOpening(Preset))
            if (!battle::MarchStartWalkable(Company, Preset.Terrain, Preset.bRegimentTactics, Preset.Field))
            {
                OutError = RosterEntryError(int32(Company.id.value - 1),
                    LOCTEXT("MarchStartTooNarrow", "Походная колонна здесь не помещается. Сдвиньте старт к середине прохода или на сушу."));
                return false;
            }

    const FMissionObjective& Objective = Preset.Objective;
    const TCHAR* InvalidObjectiveField = nullptr;
    if (!Algo::FindBy(PresetGoals, Objective.Goal, &FPresetGoal::Goal))
        InvalidObjectiveField = TEXT("objective.goal");
    else if (Objective.TimeLimitSeconds < MissionMinTimeLimit || Objective.TimeLimitSeconds > MissionMaxTimeLimit)
        InvalidObjectiveField = TEXT("objective.time_limit_seconds");
    else if (Objective.CrossingTeam < 0 || Objective.CrossingTeam > 1)
        InvalidObjectiveField = TEXT("objective.crossing_team");
    else if (Objective.Crossing < 0 || Objective.Crossing >= int32(battle::MarchCrossings.size()))
        InvalidObjectiveField = TEXT("objective.crossing");
    else if (Objective.RequiredRegiments < 1 || Objective.RequiredRegiments > MissionMaxRegimentsPerTeam)
        InvalidObjectiveField = TEXT("objective.required_regiments");
    if (InvalidObjectiveField)
    {
        OutError = AtField(InvalidObjectiveField, LOCTEXT("InvalidObjective", "Значение цели миссии выходит за допустимый диапазон."));
        return false;
    }
    if (Objective.Goal == EMissionGoal::Breakthrough)
    {
        if (!Objective.Breakthrough)
        {
            OutError = AtField(TEXT("objective.breakthrough"), LOCTEXT("MissingBreakthrough", "Укажите рубеж прорыва."));
            return false;
        }
        const auto& Zone = *Objective.Breakthrough;
        if (!FMath::IsFinite(Zone.X) || !FMath::IsFinite(Zone.Y) || !FMath::IsFinite(Zone.Radius) ||
            Zone.Radius < MissionStartInset || FMath::Abs(Zone.X) + Zone.Radius > Preset.Field.HalfWidth() ||
            FMath::Abs(Zone.Y) + Zone.Radius > Preset.Field.HalfHeight())
        {
            OutError = AtField(TEXT("objective.breakthrough"),
                LOCTEXT("InvalidBreakthroughArea", "Укажите круг радиусом не менее 4 м, целиком внутри поля."));
            return false;
        }
        if (Zone.Group < 1 || Zone.Group > MissionGroupsPerTeam)
        {
            OutError = AtField(TEXT("objective.breakthrough.group"), LOCTEXT("InvalidBreakthroughGroup", "Укажите синюю группу от 1 до 3."));
            return false;
        }
        if (std::ranges::any_of(Obstacles, [&](const auto& Area)
            {
                return Area.min_x <= Zone.X - Zone.Radius && Area.max_x >= Zone.X + Zone.Radius &&
                    Area.min_y <= Zone.Y - Zone.Radius && Area.max_y >= Zone.Y + Zone.Radius;
            }))
        {
            OutError = AtField(TEXT("objective.breakthrough"), LOCTEXT("BlockedBreakthrough", "Рубеж целиком занят препятствием."));
            return false;
        }
        int32 Eligible = 0;
        for (int32 Slot = 0; Slot < MissionRosterCapacity; ++Slot)
            Eligible += IsMissionBreakthroughInfantry(Slot, Preset.Roster[Slot], Zone.Group);
        if (Eligible < Objective.RequiredRegiments)
        {
            OutError = AtField(TEXT("objective.required_regiments"), FText::Format(LOCTEXT("TooFewBreakthroughRegiments",
                "В синей группе {0} только {1} подходящих пехотных полков, а цель требует {2}."),
                Zone.Group, Eligible, Objective.RequiredRegiments));
            return false;
        }
    }
    if (Objective.Goal == EMissionGoal::Crossing)
    {
        if (!battle::MarchHasWaterCrossings(Preset.Terrain))
        {
            OutError = LOCTEXT("NoCrossingTerrain", "Для цели переправы выберите карту с рекой.");
            return false;
        }
        if (Preset.Terrain == battle::MarchTerrain::StirlingBridge && Objective.Crossing != 1)
        {
            OutError = LOCTEXT("UnknownStirlingCrossing", "На карте Стерлинга выберите центральный мост.");
            return false;
        }
        const auto Crossings = battle::MarchCrossingsFor(Preset.Terrain);
        const int32 CrossingIndex = Preset.Terrain == battle::MarchTerrain::StirlingBridge ? 0 : Objective.Crossing;
        if (size_t(CrossingIndex) >= Crossings.size() ||
            FMath::Abs(Crossings[CrossingIndex].y) >= Preset.Field.HalfHeight())
        {
            OutError = AtField(TEXT("objective.crossing"),
                LOCTEXT("CrossingOutsideField", "Выбранная переправа находится за границей карты."));
            return false;
        }
        if (Objective.RequiredRegiments > EnabledPerTeam[Objective.CrossingTeam])
        {
            OutError = LOCTEXT("NotEnoughCrossingRegiments", "Цель переправы требует больше полков, чем включено у выбранной стороны.");
            return false;
        }
        const float Direction = Objective.CrossingTeam == 0 ? 1.f : -1.f;
        int32 EligibleRegiments = 0;
        for (int32 Slot = 0; Slot < MissionMaxRegimentsPerTeam; ++Slot)
        {
            const int32 Index = MissionRosterIndex(Objective.CrossingTeam, Slot);
            const FMissionRosterEntry& Entry = Preset.Roster[Index];
            if (Entry.bEnabled && MissionStartingPosition(Index, Entry, Preset.Field).x * Direction <= -(battle::MarchRiverHalfWidth + 2.f))
                ++EligibleRegiments;
        }
        if (Objective.RequiredRegiments > EligibleRegiments)
        {
            OutError = LOCTEXT("WrongCrossingStartBank", "Для цели переправы укажите больше стартовых позиций на исходной стороне.");
            return false;
        }
    }
    if (Objective.Goal == EMissionGoal::HillHold)
    {
        const auto Hill = battle::MarchHill(Preset.Terrain, Preset.Field);
        if (!Hill || !battle::HillObjectiveCrest(*Hill))
        {
            OutError = LOCTEXT("HillHoldNeedsCrest", "Для этой цели выберите карту с высотой, которую можно удерживать.");
            return false;
        }
        if (Objective.TimeLimitSeconds < MissionHillHoldSeconds)
        {
            OutError = FText::Format(LOCTEXT("HillHoldTimeTooShort",
                "Дайте полку не меньше {0} секунд на удержание вершины."), MissionHillHoldSeconds);
            return false;
        }
    }
    if (Objective.Goal == EMissionGoal::ReserveExercise && !Algo::FindByPredicate(BlueGroups, [](int32 GroupSize)
        { return GroupSize >= 2; }))
    {
        OutError = LOCTEXT("NotEnoughReserveRegiments", "Для учения включите хотя бы два синих полка в одной группе.");
        return false;
    }

    OutError = FText::GetEmpty();
    return true;
}

const TCHAR* RegimentMissionPresets::TerrainKey(battle::MarchTerrain Terrain)
{
    const auto* Match = Algo::FindBy(PresetTerrains, Terrain, &FPresetTerrain::Terrain);
    return Match ? Match->Value : TEXT("River");
}

TOptional<battle::MarchTerrain> RegimentMissionPresets::FindTerrain(FStringView Name)
{
    const auto* Match = Algo::FindByPredicate(PresetTerrains, [Name](const FPresetTerrain& Candidate)
    {
        return Name.Equals(Candidate.Value);
    });
    return Match ? TOptional<battle::MarchTerrain>(Match->Terrain) : TOptional<battle::MarchTerrain>();
}

FString RegimentMissionPresets::Directory()
{
    return FPaths::ProjectSavedDir() / TEXT("BattlePresets");
}

namespace
{
bool MatchesEra(const FJsonObject& Object, FStringView EraId)
{
    int32 Version;
    if (!ReadWholeNumber(Object, TEXT("schema_version"), ExpandedRosterSchemaVersion, Version))
        return false;
    if (Version == LegacyPresetSchemaVersion || Version == DetailedPresetSchemaVersion ||
        (Version == ExpandedRosterSchemaVersion && !Object.HasField(TEXT("era"))))
        return EraId.IsEmpty();
    FMissionEraSelection Selection;
    return Version >= EraPresetSchemaVersion && ReadEraSelection(Object, Selection) && EraId.Equals(Selection.Id);
}

TArray<FMissionPresetListItem> ListPresets(const FString& Directory, const FStringView* EraId)
{
    TArray<FString> Filenames;
    IFileManager::Get().FindFiles(Filenames, *(Directory / TEXT("*.json")), true, false);

    TArray<FMissionPresetListItem> Presets;
    for (const FString& Filename : Filenames)
    {
        const FString Stem = FPaths::GetBaseFilename(Filename);
        FGuid Id;
        if (!FGuid::ParseExact(Stem, EGuidFormats::Digits, Id) || !Id.IsValid())
            continue;

        FString Name;
        const auto Object = ReadPresetJson(Directory / Filename);
        if (EraId && (!Object || !MatchesEra(*Object, *EraId)))
            continue;
        if (!Object || !ReadPresetName(*Object, Name))
        {
            Name = FText::Format(LOCTEXT("UnnamedCopy", "Копия без названия · {0}"),
                FText::FromString(Stem.Left(8))).ToString();
        }
        Presets.Add({Id, MoveTemp(Name), false, EraId ? FString(*EraId) : FString{}});
    }
    Presets.Sort([](const FMissionPresetListItem& Left, const FMissionPresetListItem& Right)
    {
        const int32 Order = FTextComparison::CompareToCaseIgnored(Left.Name, Right.Name);
        return Order == 0 ? Left.Id < Right.Id : Order < 0;
    });
    return Presets;
}
}

TArray<FMissionPresetListItem> RegimentMissionPresets::List(const FString& Directory)
{
    return ListPresets(Directory, nullptr);
}

TArray<FMissionPresetListItem> RegimentMissionPresets::ListForEra(const FString& Directory, FStringView EraId)
{
    return ListPresets(Directory, &EraId);
}

bool RegimentMissionPresets::Load(const FString& Directory, FGuid Id, FMissionPreset& OutPreset, FText& OutError)
{
    const FString Filename = PresetFilename(Directory, Id);
    if (!Id.IsValid() || !IFileManager::Get().FileExists(*Filename))
    {
        OutError = LOCTEXT("PresetNotFound", "Сохранённая копия не найдена.");
        return false;
    }

    const auto Object = ReadPresetJson(Filename);
    int32 Version;
    if (!Object || !ReadWholeNumber(*Object, TEXT("schema_version"), MAX_int32, Version))
    {
        OutError = LOCTEXT("InvalidPresetFile", "Не удалось прочитать файл сохранённой миссии.");
        return false;
    }
    if (Version < LegacyPresetSchemaVersion || Version > ExpandedRosterSchemaVersion)
    {
        OutError = LOCTEXT("UnsupportedPresetVersion", "Эта версия файла сохранённой миссии не поддерживается.");
        return false;
    }

    FMissionPreset Loaded;
    if (Version >= DetailedPresetSchemaVersion && !ReadFieldSize(*Object, Loaded.Field))
    {
        OutError = FieldSizeError();
        return false;
    }
    FString Terrain;
    const int32 RosterSize = Version == ExpandedRosterSchemaVersion ? MissionRosterCapacity : MissionRegimentCount;
    const TArray<TSharedPtr<FJsonValue>>* Entries;
    if (!ReadPresetName(*Object, Loaded.Name) || !Object->HasTypedField<EJson::String>(TEXT("terrain")) ||
        !Object->TryGetStringField(TEXT("terrain"), Terrain) || !Object->TryGetArrayField(TEXT("regiments"), Entries) ||
        Entries->Num() != RosterSize)
    {
        OutError = LOCTEXT("IncompleteRosterPreset", "Проверьте название, карту и количество полков в файле миссии.");
        return false;
    }

    const auto TerrainMatch = FindTerrain(Terrain);
    if (!TerrainMatch)
    {
        OutError = LOCTEXT("UnknownPresetTerrain", "Карта сохранённой миссии неизвестна.");
        return false;
    }
    Loaded.Terrain = *TerrainMatch;

    for (int32 Index = 0; Index < RosterSize; ++Index)
    {
        const TCHAR* Field;
        if (!ReadRosterEntry((*Entries)[Index], Version, Loaded.Roster[Index], Field))
        {
            OutError = RosterFieldError(Index, Field, LOCTEXT("InvalidRegimentFile", "Отсутствует обязательное значение или указанное значение недопустимо."));
            return false;
        }
    }
    if (Version >= EraPresetSchemaVersion &&
        (Version < ExpandedRosterSchemaVersion || Object->HasField(TEXT("era"))))
    {
        FMissionEraSelection Selection;
        if (!ReadEraSelection(*Object, Selection))
        {
            OutError = LOCTEXT("IncompleteEraSelection", "В файле миссии нужны эпоха, её версия и фракции обеих сторон.");
            return false;
        }
        if (const FRegimentEraDefinition* Era = RegimentEraCatalog::Find(FStringView(Selection.Id)))
            ApplyEra(Loaded, *Era);
        Loaded.Era = MoveTemp(Selection);
    }
    FString Field;
    if (Version >= DetailedPresetSchemaVersion && !ReadVersionTwoFields(*Object, Loaded, Field))
    {
        OutError = AtField(Field, LOCTEXT("IncompletePresetDetails", "Отсутствует обязательное значение или указанное значение недопустимо."));
        return false;
    }
    if (!Validate(Loaded, OutError))
        return false;

    Loaded.EditableCopyId = Id;
    OutPreset = MoveTemp(Loaded);
    OutError = FText::GetEmpty();
    return true;
}

namespace
{
bool SerializePreset(const FMissionPreset& Preset, const FString& Name, FString& Contents, FText& OutError)
{
    if (!IsPresetNameValid(Name))
    {
        OutError = LOCTEXT("InvalidPresetName", "Введите название от 1 до 80 символов в одну строку.");
        return false;
    }

    if (!RegimentMissionPresets::Validate(Preset, OutError))
        return false;

    const auto* TerrainMatch = Algo::FindBy(PresetTerrains, Preset.Terrain, &FPresetTerrain::Terrain);
    TArray<TSharedPtr<FJsonValue>> Entries;
    const int32 RosterSize = MissionSavedRosterExtent(Preset.Roster);
    Entries.Reserve(RosterSize);
    bool bHasSelectedTroopClass = false;
    bool bHasPlatforms = false;
    for (int32 Index = 0; Index < RosterSize; ++Index)
    {
        const FMissionRosterEntry& Entry = Preset.Roster[Index];
        const auto Object = MakeShared<FJsonObject>();
        if (Entry.Platform != battle::BattlePlatform::None)
        {
            Object->SetStringField(TEXT("type"), TEXT("Platform"));
            Object->SetStringField(TEXT("platform"), RegimentEraCatalog::PlatformIds[int32(Entry.Platform)]);
            Object->SetNumberField(TEXT("platform_count"), Entry.PlatformCount);
            bHasPlatforms = true;
        }
        else
        {
            Object->SetStringField(TEXT("type"), Entry.bCavalry ? TEXT("Cavalry") : TEXT("Infantry"));
        }
        Object->SetBoolField(TEXT("enabled"), Entry.bEnabled);
        if (Entry.MountedClass != ERegimentMountClass::FactionDefault)
            Object->SetStringField(TEXT("mounted_class"), RegimentEraCatalog::MountedClassIds[int32(Entry.MountedClass)]);
        if (Entry.RangedClass != ERegimentRangedClass::FactionDefault)
            Object->SetStringField(TEXT("ranged_class"), RegimentEraCatalog::RangedClassIds[int32(Entry.RangedClass)]);
        if (!Entry.InfantryProfile.IsEmpty())
            Object->SetStringField(TEXT("infantry_profile"), Entry.InfantryProfile);
        bHasSelectedTroopClass |= HasSelectedTroopClass(Entry);
        Object->SetNumberField(TEXT("group"), MissionGroup(Index, Entry));
        if (Entry.FormationFiles > 0)
            Object->SetNumberField(TEXT("formation_files"), Entry.FormationFiles);
        if (Entry.StartX)
            Object->SetNumberField(TEXT("start_x"), *Entry.StartX);
        if (Entry.StartY)
            Object->SetNumberField(TEXT("start_y"), *Entry.StartY);
        for (const FPresetCount& Count : PresetCounts)
            Object->SetNumberField(Count.Key, Entry.*Count.Member);
        Entries.Add(MakeShared<FJsonValueObject>(Object));
    }

    const FMissionObjective& Objective = Preset.Objective;
    const auto* GoalMatch = Algo::FindBy(PresetGoals, Objective.Goal, &FPresetGoal::Goal);
    const auto ObjectiveObject = MakeShared<FJsonObject>();
    ObjectiveObject->SetStringField(TEXT("goal"), GoalMatch->Value);
    ObjectiveObject->SetNumberField(TEXT("time_limit_seconds"), Objective.TimeLimitSeconds);
    ObjectiveObject->SetNumberField(TEXT("crossing"), Objective.Crossing);
    ObjectiveObject->SetNumberField(TEXT("required_regiments"), Objective.RequiredRegiments);
    ObjectiveObject->SetNumberField(TEXT("crossing_team"), Objective.CrossingTeam);
    if (Objective.Goal == EMissionGoal::Breakthrough)
    {
        const auto& Zone = *Objective.Breakthrough;
        const auto ZoneObject = MakeShared<FJsonObject>();
        ZoneObject->SetNumberField(TEXT("x_m"), Zone.X);
        ZoneObject->SetNumberField(TEXT("y_m"), Zone.Y);
        ZoneObject->SetNumberField(TEXT("radius_m"), Zone.Radius);
        ZoneObject->SetNumberField(TEXT("group"), Zone.Group);
        ObjectiveObject->SetObjectField(TEXT("breakthrough"), ZoneObject);
    }

    TArray<TSharedPtr<FJsonValue>> Sources;
    Sources.Reserve(Preset.Sources.Num());
    for (const FString& Source : Preset.Sources)
        Sources.Add(MakeShared<FJsonValueString>(Source));

    const auto Root = MakeShared<FJsonObject>();
    int32 Version = Preset.Era.IsSet() ? EraPresetSchemaVersion : DetailedPresetSchemaVersion;
    if (bHasSelectedTroopClass)
        Version = ProfilePresetSchemaVersion;
    if (bHasPlatforms)
        Version = PlatformPresetSchemaVersion;
    if (RosterSize == MissionRosterCapacity)
        Version = ExpandedRosterSchemaVersion;
    Root->SetNumberField(TEXT("schema_version"), Version);
    Root->SetStringField(TEXT("name"), Name);
    Root->SetStringField(TEXT("terrain"), TerrainMatch->Value);
    if (Preset.Field != battle::MarchField{})
    {
        const auto FieldObject = MakeShared<FJsonObject>();
        FieldObject->SetNumberField(TEXT("width_m"), Preset.Field.width_m);
        FieldObject->SetNumberField(TEXT("height_m"), Preset.Field.height_m);
        Root->SetObjectField(TEXT("field"), FieldObject);
    }
    Root->SetArrayField(TEXT("regiments"), MoveTemp(Entries));
    Root->SetObjectField(TEXT("objective"), ObjectiveObject);
    Root->SetStringField(TEXT("briefing"), Preset.Briefing);
    Root->SetArrayField(TEXT("sources"), MoveTemp(Sources));
    Root->SetBoolField(TEXT("regiment_tactics"), Preset.bRegimentTactics);
    Root->SetBoolField(TEXT("risky_volley"), Preset.bRiskyVolley);
    Root->SetBoolField(TEXT("direct_deployment"), Preset.bDirectDeployment);
    if (Preset.bAdaptiveBattle)
        Root->SetBoolField(TEXT("adaptive_battle"), Preset.bAdaptiveBattle);
    if (Preset.bSmokeExercise)
        Root->SetBoolField(TEXT("smoke_exercise"), true);
    if (Preset.FigureTarget > 0)
        Root->SetNumberField(TEXT("figure_target"), Preset.FigureTarget);
    if (Preset.Era.IsSet())
    {
        const auto EraObject = MakeShared<FJsonObject>();
        EraObject->SetStringField(TEXT("id"), Preset.Era.Id);
        EraObject->SetNumberField(TEXT("version"), Preset.Era.Version);
        EraObject->SetStringField(TEXT("blue_faction"), Preset.Era.BlueFaction);
        EraObject->SetStringField(TEXT("orange_faction"), Preset.Era.OrangeFaction);
        Root->SetObjectField(TEXT("era"), EraObject);
    }

    if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Contents)))
    {
        OutError = LOCTEXT("CannotSerializePreset", "Не удалось подготовить данные миссии к сохранению.");
        return false;
    }
    if (FTCHARToUTF8(*Contents).Length() > MaxPresetFileSize)
    {
        OutError = LOCTEXT("PresetTooLarge", "Файл миссии превышает 64 КБ.");
        return false;
    }
    return true;
}

bool IsNameAvailable(const FString& Directory, const FString& Name, FGuid UpdatingId, FText& OutError)
{
    for (const FMissionPresetListItem& BuiltIn : RegimentMissionPresets::BuiltIns())
    {
        if (FTextComparison::EqualToCaseIgnored(BuiltIn.Name, Name))
        {
            OutError = LOCTEXT("ReservedPresetName", "Это имя встроенного пресета. Введите другое.");
            return false;
        }
    }
    for (const FMissionPresetListItem& Existing : RegimentMissionPresets::List(Directory))
    {
        if (Existing.Id != UpdatingId && FTextComparison::EqualToCaseIgnored(Existing.Name, Name))
        {
            OutError = LOCTEXT("DuplicatePresetName", "Копия с таким названием уже есть. Введите другое название.");
            return false;
        }
    }
    return true;
}

bool CommitPresetFile(const FString& Filename, const FString& Contents, bool bReplace, FText& OutError)
{
    IFileManager& Files = IFileManager::Get();
    const FString Temporary = Filename + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(Contents, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &Files, FILEWRITE_NoReplaceExisting))
    {
        Files.Delete(*Temporary, false, false, true);
        OutError = LOCTEXT("CannotWritePresetFile", "Не удалось сохранить миссию.");
        return false;
    }

    // Unreal's file manager deletes the destination before replacing it. Rename the complete temp file over it instead.
    const bool bMoved = bReplace
        ? ::MoveFileExW(*Files.ConvertToAbsolutePathForExternalAppForWrite(*Temporary),
              *Files.ConvertToAbsolutePathForExternalAppForWrite(*Filename), MOVEFILE_REPLACE_EXISTING) != 0
        : Files.Move(*Filename, *Temporary, false, false, false, true);
    if (!bMoved)
    {
        Files.Delete(*Temporary, false, false, true);
        OutError = LOCTEXT("CannotWritePresetFile", "Не удалось сохранить миссию.");
        return false;
    }

    OutError = FText::GetEmpty();
    return true;
}

FCriticalSection PresetSaveMutex;
}

bool RegimentMissionPresets::SaveCopy(const FString& Directory, const FMissionPreset& Preset, FGuid& OutId, FText& OutError)
{
    const FString Name = Preset.Name.TrimStartAndEnd();
    FString Contents;
    if (!SerializePreset(Preset, Name, Contents, OutError))
        return false;

    const FScopeLock Lock(&PresetSaveMutex);
    IFileManager& Files = IFileManager::Get();
    if (Directory.IsEmpty() || !Files.MakeDirectory(*Directory, true))
    {
        OutError = LOCTEXT("CannotCreatePresetDirectory", "Не удалось открыть папку для сохранения миссий.");
        return false;
    }
    if (!IsNameAvailable(Directory, Name, FGuid(), OutError))
        return false;

    const FGuid Id = FGuid::NewGuid();
    const FString Filename = PresetFilename(Directory, Id);
    if (!CommitPresetFile(Filename, Contents, false, OutError))
        return false;

    OutId = Id;
    return true;
}

bool RegimentMissionPresets::Save(const FString& Directory, FGuid Id, const FMissionPreset& Preset, FText& OutError)
{
    if (!Id.IsValid() || Algo::FindBy(BuiltIns(), Id, &FMissionPresetListItem::Id))
    {
        OutError = LOCTEXT("CannotUpdateBuiltIn", "Для встроенного пресета сохраните новую копию.");
        return false;
    }

    const FString Name = Preset.Name.TrimStartAndEnd();
    FString Contents;
    if (!SerializePreset(Preset, Name, Contents, OutError))
        return false;

    const FScopeLock Lock(&PresetSaveMutex);
    const FString Filename = PresetFilename(Directory, Id);
    if (Directory.IsEmpty() || !IFileManager::Get().FileExists(*Filename))
    {
        OutError = LOCTEXT("PresetNotFound", "Сохранённая копия не найдена.");
        return false;
    }
    const auto Existing = ReadPresetJson(Filename);
    FString ExistingName;
    const bool bKeepsName = Existing && ReadPresetName(*Existing, ExistingName) &&
        FTextComparison::EqualToCaseIgnored(ExistingName, Name);
    return (bKeepsName || IsNameAvailable(Directory, Name, Id, OutError)) &&
        CommitPresetFile(Filename, Contents, true, OutError);
}

#undef LOCTEXT_NAMESPACE
