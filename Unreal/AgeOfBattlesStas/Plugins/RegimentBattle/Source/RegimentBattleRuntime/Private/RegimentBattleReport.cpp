#include "RegimentBattleReport.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RegimentBattlePlayback.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "RegimentBattleReport"

namespace
{
constexpr const TCHAR* SaveVersion = TEXT("RegimentBattleStatistics 1");
constexpr uint64 FRegimentPlayerStats::* SavedCounters[] = {
    &FRegimentPlayerStats::Victories, &FRegimentPlayerStats::Defeats, &FRegimentPlayerStats::Draws,
    &FRegimentPlayerStats::ElapsedSeconds, &FRegimentPlayerStats::Fallen, &FRegimentPlayerStats::Eliminated
};

bool ReadStatistics(const FString& Path, FRegimentPlayerStats& Stats)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() != UE_ARRAY_COUNT(SavedCounters) + 1 ||
        Lines[0] != SaveVersion)
        return false;
    FRegimentPlayerStats Loaded;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(SavedCounters); ++Index)
    {
        const FString& Line = Lines[Index + 1];
        if (Line.IsEmpty() || Line.Len() > 16)
            return false;
        for (const TCHAR Digit : Line)
            if (Digit < '0' || Digit > '9')
                return false;
        Loaded.*SavedCounters[Index] = FCString::Strtoui64(*Line, nullptr, 10);
    }
    Stats = Loaded;
    return true;
}
}

FRegimentBattleReportAccumulator::FRegimentBattleReportAccumulator(const battle::World& World)
{
    TMap<uint64, int32> RowsByRegiment;
    for (const auto& Regiment : World.regiments)
        if (Regiment.team == 0 || Regiment.team == 1)
        {
            const auto Name = Regiment.name.empty()
                ? FText::Format(LOCTEXT("UnnamedRegiment", "Полк {0}"), FText::AsNumber(Regiment.id.value))
                : FText::FromString(UTF8_TO_TCHAR(Regiment.name.c_str()));
            RowsByRegiment.Add(Regiment.id.value, Report.Rows.Add({Name, Regiment.team, {}}));
        }
    RowsByUnit.Reserve(int32(World.units.size()));
    int32 StaffRows[2] = {INDEX_NONE, INDEX_NONE};
    for (const auto& Unit : World.units)
    {
        if (!Unit.alive)
            continue;
        const int32 Team = battle::UnitTeam(World, Unit);
        if (Team != 0 && Team != 1)
            continue;
        const int32* RegimentRow = RowsByRegiment.Find(Unit.regiment.value);
        if (!RegimentRow && StaffRows[Team] == INDEX_NONE)
            StaffRows[Team] = Report.Rows.Add({LOCTEXT("IndependentStaff", "Штаб и свита"), Team, {}});
        const int32 Row = RegimentRow ? *RegimentRow : StaffRows[Team];
        RowsByUnit.Add(Unit.id.value, Row);
        ++Report.Rows[Row].Count.Starting;
    }
}

void FRegimentBattleReportAccumulator::Observe(const battle::World& World)
{
    if (bFinished)
        return;
    for (const auto& Event : World.events)
    {
        if (Event.kind != battle::EventKind::Died || ConfirmedDeaths.Contains(Event.target.value))
            continue;
        const int32* Target = RowsByUnit.Find(Event.target.value);
        const auto* Unit = Target ? battle::FindUnit(World, Event.target) : nullptr;
        if (!Unit || Unit->alive)
            continue;
        ConfirmedDeaths.Add(Event.target.value);
        if (const int32* Source = RowsByUnit.Find(Event.source.value);
            Source && Report.Rows[*Source].Team != Report.Rows[*Target].Team)
            ++Report.Rows[*Source].Count.Eliminated;
    }
}

TOptional<FRegimentBattleReport> FRegimentBattleReportAccumulator::Finish(const battle::World& World)
{
    if (bFinished || World.outcome == battle::BattleOutcome::Running)
        return {};
    Observe(World);
    bFinished = true;

    for (const auto& Unit : World.units)
        if (const int32* Row = Unit.alive ? RowsByUnit.Find(Unit.id.value) : nullptr)
        {
            auto& Count = Report.Rows[*Row].Count;
            ++Count.Living;
            if (const auto* Regiment = battle::FindRegiment(World, Unit.regiment); Regiment && Regiment->panic)
                ++Count.Panicked;
        }
    for (const auto& Row : Report.Rows)
    {
        auto& Side = Report.Sides[Row.Team];
        Side.Starting += Row.Count.Starting;
        Side.Living += Row.Count.Living;
        Side.Eliminated += Row.Count.Eliminated;
        Side.Panicked += Row.Count.Panicked;
    }
    Report.ElapsedSeconds = World.tick / FRegimentBattlePlayback::StepsPerSecond;
    Report.Outcome = World.outcome;
    Report.Winner = World.winner_team;
    return MoveTemp(Report);
}

void FRegimentPlayerStats::Record(const FRegimentBattleReport& Report, int32 PlayerTeam)
{
    if ((PlayerTeam != 0 && PlayerTeam != 1) || Report.Outcome == battle::BattleOutcome::Running)
        return;
    if (Report.Outcome == battle::BattleOutcome::Draw)
        ++Draws;
    else if (Report.Winner == PlayerTeam)
        ++Victories;
    else
        ++Defeats;
    ElapsedSeconds += Report.ElapsedSeconds;
    Fallen += uint64(Report.Sides[PlayerTeam].Fallen());
    Eliminated += uint64(Report.Sides[PlayerTeam].Eliminated);
}

FString FRegimentPlayerStats::SavePath()
{
    return FPaths::ProjectSavedDir() / TEXT("BattleStatistics/Records.txt");
}

void FRegimentPlayerStats::Load(const FString& Path)
{
    *this = {};
    if (!ReadStatistics(Path, *this))
        ReadStatistics(Path + TEXT(".bak"), *this);
}

bool FRegimentPlayerStats::Save(const FString& Path) const
{
    TArray<FString> Lines{SaveVersion};
    for (const auto Field : SavedCounters)
        Lines.Add(FString::Printf(TEXT("%llu"), this->*Field));

    auto& Files = IFileManager::Get();
    Files.MakeDirectory(*FPaths::GetPath(Path), true);
    const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Backup = Path + TEXT(".bak");
    if (!FFileHelper::SaveStringArrayToFile(Lines, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Files.Delete(*Temporary);
        return false;
    }

    if (Files.FileExists(*Path) &&
        (!Files.Delete(*Backup) || !Files.Move(*Backup, *Path, false, false, false, true)))
    {
        Files.Delete(*Temporary);
        return false;
    }
    if (Files.Move(*Path, *Temporary, false, false, false, true))
        return true;
    Files.Delete(*Temporary);
    return false;
}

#undef LOCTEXT_NAMESPACE
