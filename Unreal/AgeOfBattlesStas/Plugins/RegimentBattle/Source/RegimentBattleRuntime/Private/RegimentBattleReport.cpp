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

FText BattleEndDescription(const battle::World& World)
{
    bool bFielded[2] = {};
    bool bInBattle[2] = {};
    for (const auto& Regiment : World.regiments)
        if (Regiment.team == 0 || Regiment.team == 1)
        {
            bFielded[Regiment.team] = true;
            bInBattle[Regiment.team] |= !Regiment.units.empty();
        }
    if (bFielded[0] && bFielded[1])
    {
        if (World.outcome == battle::BattleOutcome::Draw && !bInBattle[0] && !bInBattle[1])
            return LOCTEXT("BothSidesEliminated", "Обе стороны остались без боевых полков.");
        const int32 Winner = World.winner_team;
        if (World.outcome == battle::BattleOutcome::Victory && (Winner == 0 || Winner == 1) &&
            bInBattle[Winner] && !bInBattle[1 - Winner])
            return LOCTEXT("LosingSideEliminated", "У проигравшей стороны не осталось боевых полков.");
    }
    return LOCTEXT("BattleEnded", "Бой завершён.");
}
}

FRegimentBattleReportAccumulator::FRegimentBattleReportAccumulator(const battle::World& World)
{
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

void FRegimentBattleReportAccumulator::RememberMoment(ERegimentBattleMomentKind Kind, battle::Tick Tick,
    int32 Row, const FText& Message)
{
    auto& Moment = CombatMoments[int32(Kind)];
    // The final rout can end a conventional battle. Keep the first occurrence of other events.
    if (!Moment || (Kind == ERegimentBattleMomentKind::Rout && Tick > Moment->Tick))
        Moment = FRegimentBattleMoment{Kind, Tick, FText::Format(Message, Report.Rows[Row].Name)};
}

void FRegimentBattleReportAccumulator::ObserveOrderMoment(const battle::BattleEvent& Event)
{
    using EKind = ERegimentBattleMomentKind;
    const int32* Row = RowsByRegiment.Find(Event.regiment.value);
    if (!Row)
        return;
    const std::string& Reason = Event.reason;
    if (Reason == "rout: fled off the battlefield")
        RememberMoment(EKind::Rout, Event.tick, *Row, LOCTEXT("Fled", "«{0}»: полк бежал с поля."));
    else if (Reason == "rout: surrounded under front and rear attack")
        RememberMoment(EKind::Rout, Event.tick, *Row,
            LOCTEXT("Encircled", "«{0}»: полк окружён и выбыл из боя."));
    else if (Reason == "panic: attacked from the rear")
        RememberMoment(EKind::Panic, Event.tick, *Row, LOCTEXT("RearPanic", "«{0}»: паника после удара с тыла."));
    else if (Reason == "panic: lost over half the regiment to shooting" || Reason == "panic: sustained shooting losses")
        RememberMoment(EKind::Panic, Event.tick, *Row,
            LOCTEXT("ShootingPanic", "«{0}»: потери от обстрела привели к панике."));
    else if (Reason.starts_with("panic: "))
        RememberMoment(EKind::Panic, Event.tick, *Row, LOCTEXT("Panic", "«{0}»: полк запаниковал."));
    else if (Reason.starts_with("rallied: "))
        RememberMoment(EKind::Rally, Event.tick, *Row, LOCTEXT("Rally", "«{0}»: полк оправился от паники."));
}

void FRegimentBattleReportAccumulator::Observe(const battle::World& World)
{
    if (bFinished)
        return;
    using EKind = ERegimentBattleMomentKind;
    for (const auto& Event : World.events)
    {
        if (Event.kind == battle::EventKind::OrderChanged)
            ObserveOrderMoment(Event);
        else if (Event.kind == battle::EventKind::CommandVacant)
        {
            const int32* Row = RowsByRegiment.Find(Event.regiment.value);
            if (!Row)
                Row = RowsByUnit.Find(Event.target.value);
            if (Row)
                RememberMoment(EKind::CommandVacant, Event.tick, *Row,
                    LOCTEXT("CommanderVacant", "«{0}»: командир выбыл, преемника нет."));
        }
        else if (Event.kind == battle::EventKind::Hit && Event.amount > 0.f &&
            !CombatMoments[int32(EKind::FirstHit)])
        {
            const int32* Source = RowsByUnit.Find(Event.source.value);
            const int32* Target = RowsByUnit.Find(Event.target.value);
            if (Source && Target && Report.Rows[*Source].Team != Report.Rows[*Target].Team)
                RememberMoment(EKind::FirstHit, Event.tick, *Target,
                    LOCTEXT("FirstHit", "Первое попадание по противнику · «{0}»."));
        }
        if (Event.kind != battle::EventKind::Died || ConfirmedDeaths.Contains(Event.target.value))
            continue;
        const int32* Target = RowsByUnit.Find(Event.target.value);
        const auto* Unit = Target ? battle::FindUnit(World, Event.target) : nullptr;
        if (!Unit || Unit->alive)
            continue;
        ConfirmedDeaths.Add(Event.target.value);
        if (const int32* Source = RowsByUnit.Find(Event.source.value);
            Source && Report.Rows[*Source].Team != Report.Rows[*Target].Team)
        {
            ++Report.Rows[*Source].Count.Eliminated;
            RememberMoment(EKind::FirstLoss, Event.tick, *Target,
                LOCTEXT("FirstLoss", "Первая потеря от удара противника · «{0}»."));
        }
    }
}

void FRegimentBattleReportAccumulator::BuildTimeline(const battle::World& World,
    const FRegimentBattleDebriefMilestones& Milestones)
{
    using EKind = ERegimentBattleMomentKind;
    if (Milestones.FirstCrestControl && *Milestones.FirstCrestControl <= World.tick)
        Report.Moments.Add({EKind::CrestControl, *Milestones.FirstCrestControl,
            LOCTEXT("CrestControl", "Гребень под контролем синих полков.")});
    if (Milestones.FirstReserveContact && *Milestones.FirstReserveContact <= World.tick)
        Report.Moments.Add({EKind::ReserveContact, *Milestones.FirstReserveContact,
            LOCTEXT("ReserveContact", "Резерв вступил в ближний бой.")});

    // Prefer mission milestones and formation changes over ordinary losses and hits.
    constexpr EKind CombatPriority[] = {
        EKind::Rout, EKind::CommandVacant, EKind::Panic, EKind::Rally, EKind::FirstLoss, EKind::FirstHit
    };
    const auto& FirstLoss = CombatMoments[int32(EKind::FirstLoss)];
    for (const EKind Kind : CombatPriority)
    {
        if (Report.Moments.Num() == 2)
            break;
        const auto& Moment = CombatMoments[int32(Kind)];
        if (Moment && Moment->Tick <= World.tick &&
            !(Kind == EKind::FirstHit && FirstLoss && FirstLoss->Tick == Moment->Tick))
            Report.Moments.Add(*Moment);
    }
    Report.Moments.StableSort([](const FRegimentBattleMoment& A, const FRegimentBattleMoment& B)
    {
        return A.Tick < B.Tick;
    });
    Report.Moments.Add({EKind::BattleEnded, World.tick, BattleEndDescription(World)});
}

TOptional<FRegimentBattleReport> FRegimentBattleReportAccumulator::Finish(const battle::World& World,
    const FRegimentBattleDebriefMilestones& Milestones)
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
    BuildTimeline(World, Milestones);
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
