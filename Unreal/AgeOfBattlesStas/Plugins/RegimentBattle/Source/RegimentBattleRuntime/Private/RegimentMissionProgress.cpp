#include "RegimentMissionProgress.h"

#include "RegimentBattlePlayback.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Formation.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "RegimentMissionProgress"

namespace
{
constexpr int32 ReserveOrderPriority = 120;
constexpr battle::Tick DeliveryDelay = FRegimentBattlePlayback::StepsPerSecond;
constexpr float ExitX = battle::MarchRiverHalfWidth + 2.f;
constexpr battle::Tick HillHoldTicks = MissionHillHoldSeconds * FRegimentBattlePlayback::StepsPerSecond;

FText SideName(battle::MarchTerrain Terrain, int32 Side)
{
    if (Terrain == battle::MarchTerrain::StirlingBridge)
        return Side == 0 ? LOCTEXT("Scotland", "Шотландия (синие)") : LOCTEXT("England", "Англия (оранжевые)");
    return Side == 0 ? LOCTEXT("Blue", "Синие") : LOCTEXT("Orange", "Оранжевые");
}

FText EventTime(TOptional<battle::Tick> Tick)
{
    return Tick ? FText::AsCultureInvariant(FString::Printf(TEXT("%.1f с"),
        double(*Tick) * FRegimentBattlePlayback::StepSeconds)) : LOCTEXT("NoEvent", "—");
}
}

FRegimentMissionProgress::FRegimentMissionProgress(const FMissionPreset& Preset, battle::World& World)
    : Objective(Preset.Objective), Terrain(Preset.Terrain)
{
    if (Objective.Goal == EMissionGoal::Crossing)
    {
        const float Direction = Objective.CrossingTeam == 0 ? 1.f : -1.f;
        for (const auto& Regiment : World.regiments)
            if (Regiment.team == Objective.CrossingTeam && Regiment.pose.position.x * Direction <= -ExitX)
                StartsOnAttackingBank.Add(Regiment.id);
    }
    else if (Objective.Goal == EMissionGoal::Breakthrough && Objective.Breakthrough)
    {
        for (int32 Slot = 0; Slot < MissionRegimentsPerTeam; ++Slot)
            if (IsMissionBreakthroughInfantry(Slot, Preset.Roster[Slot], Objective.Breakthrough->Group))
                BreakthroughInfantry.Add({uint64(Slot + 1)});
    }
    else if (IsReserveExercise() && World.command_state)
    {
        const auto& Hierarchy = World.command_state->hierarchy;
        for (const auto& Group : Hierarchy.nodes)
        {
            if (Group.team != 0 || Group.level != battle::command::Level::Group || Group.children.size() < 2)
                continue;
            for (const auto Child : Group.children)
            {
                const auto It = std::find_if(Hierarchy.nodes.begin(), Hierarchy.nodes.end(),
                    [Child](const auto& Node) { return Node.id == Child; });
                if (It == Hierarchy.nodes.end() || !It->regiment)
                    continue;
                if (!Lead)
                    Lead = It->regiment;
                else
                {
                    Reserve = It->regiment;
                    break;
                }
            }
            if (Reserve)
                break;
        }
        if (const auto* Regiment = battle::FindRegiment(World, Lead))
            LeadInitial = Regiment->initial_count;
        if (const auto* Regiment = battle::FindRegiment(World, Reserve))
        {
            battle::Order Hold;
            Hold.recipient = Regiment->id;
            Hold.point = Regiment->pose.position;
            Hold.type = battle::OrderType::Hold;
            Hold.priority = ReserveOrderPriority;
            battle::SubmitOrder(World, Hold);
        }
    }
}

bool FRegimentMissionProgress::CanReleaseReserve(const battle::World& World) const
{
    const auto* Regiment = IsReserveExercise() ? battle::FindRegiment(World, Reserve) : nullptr;
    return Regiment && !Regiment->units.empty() && !Issued && Result.IsEmpty() &&
        World.outcome == battle::BattleOutcome::Running;
}

bool FRegimentMissionProgress::AwaitsReserveRelease(battle::RegimentId Regiment) const
{
    return Reserve && Regiment == Reserve && !Delivered;
}

void FRegimentMissionProgress::ReleaseReserve(battle::World& World)
{
    if (!CanReleaseReserve(World))
        return;
    battle::Order Release;
    Release.id = { World.next_order_id };
    Release.recipient = Reserve;
    Release.type = battle::OrderType::Attack;
    Release.priority = ReserveOrderPriority;
    Release.created_tick = World.tick;
    Release.delivery_tick = World.tick + DeliveryDelay;
    if (battle::SubmitOrder(World, Release) == battle::Status::Ok)
    {
        Issued = World.tick;
        ReleaseOrder = Release.id;
    }
}

void FRegimentMissionProgress::Finish(battle::World& World, int32 Winner, FText Explanation)
{
    Result = MoveTemp(Explanation);
    World.outcome = battle::BattleOutcome::Victory;
    World.winner_team = Winner;
}

void FRegimentMissionProgress::UpdateHillHold(battle::World& World, battle::Tick Deadline)
{
    if (World.outcome == battle::BattleOutcome::Draw ||
        (World.outcome == battle::BattleOutcome::Victory && World.winner_team != 0))
    {
        Finish(World, 1, LOCTEXT("HillBlueDefeated", "Синие полки разгромлены. Высота не удержана."));
        return;
    }
    if (!World.scenario.hill)
    {
        Finish(World, 1, LOCTEXT("HillMissing", "На поле нет вершины для удержания."));
        return;
    }

    if (World.tick > 0 && (!LastHillSample || World.tick != *LastHillSample))
    {
        bool bBlueOccupiesCrest = false;
        bHillContested = false;
        for (const auto& Regiment : World.regiments)
        {
            bool& bControlsCrest = Regiment.team == 0 ? bBlueOccupiesCrest : bHillContested;
            if (Regiment.panic || Regiment.disordered || battle::IsDeploying(World, Regiment) ||
                Regiment.units.empty() || bControlsCrest)
                continue;
            // Staff in formation count. A possessed or returning officer and detached
            // skirmishers cannot hold or contest the crest on behalf of their formation.
            const uint64 Required = Regiment.team == 0 ? (Regiment.initial_count + 1) / 2 : 1;
            if (Regiment.units.size() < Required)
                continue;
            uint64 InCrest = 0;
            for (const auto Id : Regiment.units)
            {
                const auto* Fighter = battle::FindUnit(World, Id);
                if (!Fighter || !Fighter->alive || Fighter->control != battle::ControlMode::FormationControlled ||
                    Fighter->skirmisher || Fighter->skirmisher_returning ||
                    !battle::OnHillCrest(*World.scenario.hill, Fighter->pose.position))
                    continue;
                if (++InCrest >= Required)
                {
                    bControlsCrest = true;
                    break;
                }
            }
            if (bBlueOccupiesCrest && bHillContested)
                break;
        }

        if (bBlueOccupiesCrest && !bHillContested)
        {
            HillHeldTicks = LastHillSample && World.tick - 1 == *LastHillSample ? HillHeldTicks + 1 : 1;
            BestHillHeldTicks = FMath::Max(BestHillHeldTicks, HillHeldTicks);
            if (!FirstHillControl)
                FirstHillControl = World.tick;
        }
        else
            HillHeldTicks = 0;
        LastHillSample = World.tick;
    }

    if (HillHeldTicks >= HillHoldTicks)
        Finish(World, 0, LOCTEXT("HillHeld", "Синие заняли и удержали высоту."));
    else if (World.tick >= Deadline)
        Finish(World, 1, LOCTEXT("HillTimeExpired", "Синие не успели удержать вершину."));
    else if (World.outcome == battle::BattleOutcome::Victory)
        // Eliminating the defenders opens the crest. The surviving blue troops still have to hold it.
        World.outcome = battle::BattleOutcome::Running;
}

void FRegimentMissionProgress::Update(battle::World& World)
{
    if (!Result.IsEmpty() || Objective.Goal == EMissionGoal::Eliminate)
        return;
    const auto Deadline = battle::Tick(Objective.TimeLimitSeconds) * FRegimentBattlePlayback::StepsPerSecond;
    if (Objective.Goal == EMissionGoal::HillHold)
    {
        UpdateHillHold(World, Deadline);
        return;
    }
    if (Objective.Goal == EMissionGoal::Breakthrough)
    {
        UpdateBreakthrough(World, Deadline);
        return;
    }
    if (Objective.Goal == EMissionGoal::Crossing)
    {
        const float Direction = Objective.CrossingTeam == 0 ? 1.f : -1.f;
        const auto Crossings = battle::MarchCrossingsFor(Terrain);
        const auto& Crossing = Crossings[Terrain == battle::MarchTerrain::StirlingBridge ? 0 : Objective.Crossing];
        for (const auto Id : StartsOnAttackingBank)
        {
            const auto* Regiment = battle::FindRegiment(World, Id);
            if (!Regiment || Passed.Contains(Id.value))
                continue;
            uint64 Beyond = 0;
            for (const auto Fighter : Regiment->units)
            {
                const auto* Unit = battle::FindUnit(World, Fighter);
                if (!Unit || !Unit->alive || Unit->skirmisher)
                    continue;
                const float X = Unit->pose.position.x * Direction;
                const float YOffset = FMath::Abs(Unit->pose.position.y - Crossing.y);
                if (X <= -ExitX)
                    UsedCrossing.Remove(Fighter.value);
                else if (FMath::Abs(X) <= 0.75f && YOffset <= Crossing.half_width)
                    UsedCrossing.Add(Fighter.value);
                if (X >= ExitX && YOffset <= Crossing.half_width + 5.f && UsedCrossing.Contains(Fighter.value))
                    ++Beyond;
            }
            if (!Regiment->panic && Regiment->pose.position.x * Direction >= ExitX &&
                FMath::Abs(Regiment->pose.position.y - Crossing.y) <= Crossing.half_width + 5.f &&
                Beyond * 2 >= Regiment->initial_count)
                Passed.Add(Id.value);
        }
        if (Passed.Num() >= Objective.RequiredRegiments)
            Finish(World, Objective.CrossingTeam, LOCTEXT("Crossed", "Нужное число полков вышло на другой берег."));
        else if (World.outcome == battle::BattleOutcome::Victory && World.winner_team == Objective.CrossingTeam)
            Finish(World, Objective.CrossingTeam, LOCTEXT("OpenedBridge", "Оборона разгромлена. Выход к переправе открыт."));
        else if (World.tick >= Deadline || World.outcome != battle::BattleOutcome::Running)
            Finish(World, 1 - Objective.CrossingTeam, LOCTEXT("Held", "Выход к переправе удержан. Цель наступающих не выполнена."));
        return;
    }

    const auto* Regiment = battle::FindRegiment(World, Reserve);
    if (ReleaseOrder && Regiment && Regiment->order && Regiment->order->id == ReleaseOrder && !Delivered)
        Delivered = World.tick > 0 ? World.tick - 1 : 0;
    if (Regiment && Regiment->melee_engaged && !FirstContact)
    {
        FirstContact = World.tick > 0 ? World.tick - 1 : 0;
        if (const auto* Main = battle::FindRegiment(World, Lead))
            LeadAtContact = uint64(Main->units.size());
        if (!Delivered)
            Finish(World, 1, LOCTEXT("TooLate", "Резерв вступил в контакт до доставки приказа."));
        else if (LeadAtContact * 2 < LeadInitial)
            Finish(World, 1, LOCTEXT("LeadWeakened", "Резерв вступил слишком поздно. Передовой полк потерял больше половины состава."));
        else
            Finish(World, 0, LOCTEXT("ReserveSuccess", "Цель выполнена. Резерв вступил в бой, пока передовой полк держался."));
    }
    else if (!Regiment || Regiment->units.empty() || World.tick >= Deadline ||
             World.outcome != battle::BattleOutcome::Running)
        Finish(World, 1, LOCTEXT("ReserveNotEngaged", "Цель не выполнена. Резерв не вступил в бой вовремя."));
}

void FRegimentMissionProgress::UpdateBreakthrough(battle::World& World, battle::Tick Deadline)
{
    if (!World.scenario.objective || World.outcome == battle::BattleOutcome::Draw ||
        (World.outcome == battle::BattleOutcome::Victory && World.winner_team != 0))
    {
        Finish(World, 1, LOCTEXT("BreakthroughDefeated", "Наступление сорвано. Рубеж не пройден."));
        return;
    }

    const auto& Zone = *World.scenario.objective;
    const auto Inside = [&Zone](battle::Vec3 Point)
    {
        return FMath::Square(Point.x - Zone.center.x) + FMath::Square(Point.y - Zone.center.y) <=
            FMath::Square(Zone.radius_m);
    };
    InBreakthrough = 0;
    for (const auto Id : BreakthroughInfantry)
    {
        const auto* Regiment = battle::FindRegiment(World, Id);
        if (!Regiment || Regiment->panic || Regiment->disordered || battle::IsDeploying(World, *Regiment) ||
            !Inside(Regiment->pose.position))
            continue;
        const uint64 Required = (Regiment->initial_count + 1) / 2;
        uint64 InsideCount = 0;
        for (const auto Fighter : Regiment->units)
        {
            const auto* Unit = battle::FindUnit(World, Fighter);
            if (Unit && Unit->alive && Unit->control == battle::ControlMode::FormationControlled &&
                !Unit->skirmisher && !Unit->skirmisher_returning && Inside(Unit->pose.position) &&
                ++InsideCount >= Required)
            {
                ++InBreakthrough;
                break;
            }
        }
    }

    if (InBreakthrough >= Objective.RequiredRegiments)
        Finish(World, 0, LOCTEXT("BreakthroughReached", "Пехота прошла через рубеж прорыва."));
    else if (World.tick >= Deadline)
        Finish(World, 1, LOCTEXT("BreakthroughExpired", "Время вышло. Нужные полки не прошли рубеж."));
    else if (World.outcome == battle::BattleOutcome::Victory)
        World.outcome = battle::BattleOutcome::Running;
}

FText FRegimentMissionProgress::Status(const battle::World& World) const
{
    if (!Result.IsEmpty())
        return Result;
    if (Objective.Goal == EMissionGoal::Eliminate)
        return FText::GetEmpty();
    const uint64 Elapsed = World.tick / FRegimentBattlePlayback::StepsPerSecond;
    const int32 Remaining = FMath::Max(0, Objective.TimeLimitSeconds -
        int32(FMath::Min<uint64>(Elapsed, uint64(Objective.TimeLimitSeconds))));
    if (Objective.Goal == EMissionGoal::Breakthrough && Objective.Breakthrough)
        return FText::Format(LOCTEXT("BreakthroughProgress",
            "Прорыв · пехота синей группы {0}\nНа рубеже {1}/{2} · осталось {3} с"),
            Objective.Breakthrough->Group, InBreakthrough, Objective.RequiredRegiments, Remaining);
    if (Objective.Goal == EMissionGoal::Crossing)
        return FText::Format(LOCTEXT("CrossingProgress",
            "{0} переправляют полки · {1}/{2}\n{3} удерживают выход · осталось {4} с"),
            SideName(Terrain, Objective.CrossingTeam), Passed.Num(), Objective.RequiredRegiments,
            SideName(Terrain, 1 - Objective.CrossingTeam), Remaining);
    if (Objective.Goal == EMissionGoal::HillHold)
    {
        const FText Stage = bHillContested ? LOCTEXT("HillContested", "Противник оспаривает вершину.")
            : HillHeldTicks ? LOCTEXT("HillOccupied", "Синие удерживают вершину.")
                           : LOCTEXT("HillAwaiting", "Выведите на вершину хотя бы половину исходного состава полка.");
        return FText::Format(LOCTEXT("HillProgress", "{0}\nУдержание {1} из {2} с · осталось {3} с"),
            Stage, EventTime(TOptional<battle::Tick>(HillHeldTicks)), MissionHillHoldSeconds, Remaining);
    }
    const FText Stage = Delivered ? LOCTEXT("Arrived", "Приказ доставлен. Резерв выдвигается.")
        : Issued ? LOCTEXT("EnRoute", "Приказ передаётся резерву.")
                 : LOCTEXT("Waiting", "Передовой полк действует. Резерв ждёт твоего приказа.");
    return FText::Format(LOCTEXT("ReserveProgress", "{0}\nДо конца упражнения {1} с"), Stage, Remaining);
}

FText FRegimentMissionProgress::Report() const
{
    if (Objective.Goal == EMissionGoal::HillHold)
        return FText::Format(LOCTEXT("HillReport", "Первое удержание · {0}\nЛучший непрерывный отрезок · {1}"),
            EventTime(FirstHillControl), EventTime(TOptional<battle::Tick>(BestHillHeldTicks)));
    if (!IsReserveExercise())
        return FText::GetEmpty();
    const FText Timeline = FText::Format(LOCTEXT("ReportTimeline",
        "Приказ отдан · {0}\nДоставлен · {1}\nПервый контакт резерва · {2}"),
        EventTime(Issued), EventTime(Delivered), EventTime(FirstContact));
    if (!FirstContact)
        return Timeline;
    return FText::Format(LOCTEXT("ReportStrength", "{0}\nВ передовом полку осталось {1} из {2}"),
        Timeline, LeadAtContact, LeadInitial);
}

#undef LOCTEXT_NAMESPACE
