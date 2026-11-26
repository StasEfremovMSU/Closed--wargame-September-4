#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "RegimentBattlePlayback.h"
#include "RegimentMissionProgress.h"
#include "RegimentMissionScenario.h"
#include "RegimentOfficerOrders.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Combat.h"
#include "battle/Simulation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
struct FHistoricalPlaythrough
{
    FGuid Preset;
    EMissionGoal Goal;
    battle::RegimentId ObservedRegiment;
};

struct FFormationTravel
{
    battle::Vec3 Start;
    battle::Pose Previous;
    float MaxTravel = 0.f;
    float MaxApproach = 0.f;
    battle::Tick StationaryTicks = 0;
    battle::Tick LongestStall = 0;
    bool bSawMelee = false;
};

float HorizontalDistance(battle::Vec3 A, battle::Vec3 B)
{
    return std::hypot(A.x - B.x, A.y - B.y);
}

void ObserveTravel(FFormationTravel& Travel, const battle::RegimentState& Regiment, battle::Vec3 Objective)
{
    const auto Position = Regiment.pose.position;
    Travel.MaxTravel = FMath::Max(Travel.MaxTravel, HorizontalDistance(Position, Travel.Start));
    Travel.MaxApproach = FMath::Max(Travel.MaxApproach,
        HorizontalDistance(Travel.Start, Objective) - HorizontalDistance(Position, Objective));
    Travel.bSawMelee |= Regiment.melee_engaged;
    const bool bExpectedMotion = !Regiment.units.empty() && !Regiment.panic && !Regiment.disordered &&
        !Regiment.melee_engaged && Regiment.order && Regiment.order->status == battle::OrderStatus::Active &&
        (Regiment.order->type == battle::OrderType::MoveTo || battle::IsAttacking(Regiment.order->type)) &&
        Regiment.waypoint < Regiment.route.size();
    const bool bStationary = HorizontalDistance(Position, Travel.Previous.position) < .01f &&
        std::abs(std::remainder(Regiment.pose.yaw - Travel.Previous.yaw, 2.f * battle::Pi)) < .001f;
    Travel.StationaryTicks = bExpectedMotion && bStationary
        ? Travel.StationaryTicks + 1 : 0;
    Travel.LongestStall = FMath::Max(Travel.LongestStall, Travel.StationaryTicks);
    Travel.Previous = Regiment.pose;
}

bool RunHistoricalPlaythrough(FAutomationTestBase& Test, const FHistoricalPlaythrough& Case)
{
    FMissionPreset Preset;
    if (!Test.TestTrue(TEXT("The historical mission loads from the catalogue"),
        RegimentMissionPresets::LoadBuiltIn(Case.Preset, Preset)))
        return false;
    if (!Test.TestTrue(TEXT("The mission begins directly in battle with its historical objective"),
        Preset.StartsInBattle() && Preset.Objective.Goal == Case.Goal))
        return false;

    const auto Opening = MakeMissionOpening(Preset);
    auto Created = battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
    if (!Test.TestTrue(TEXT("The authored battle deploys"), bool(Created)))
        return false;
    auto& World = Created.value;
    const auto* Observed = battle::FindRegiment(World, Case.ObservedRegiment);
    if (!Test.TestNotNull(TEXT("The objective regiment deploys"), Observed) ||
        !Test.TestTrue(TEXT("Full-size battle starts with a live objective and command hierarchy"),
            World.outcome == battle::BattleOutcome::Running && World.units.size() == 5000 &&
            World.scenario.objective.has_value() && World.command_state.has_value()))
        return false;

    const auto Navigation = MakeMissionNavigation(World.scenario.obstacles, Preset.Field);
    FRegimentMissionProgress Mission(Preset, World);
    const auto& Objective = *World.scenario.objective;
    constexpr auto StepsPerSecond = FRegimentBattlePlayback::StepsPerSecond;
    const auto Deadline = battle::Tick(Preset.Objective.TimeLimitSeconds) * StepsPerSecond;
    const auto Start = FPlatformTime::Seconds();

    uint64 Sequence = 1;
    std::vector<battle::CommandEnvelope> Inputs;
    const bool bSenlac = Case.Goal == EMissionGoal::HillDefense;
    const bool bGranicus = Case.Goal == EMissionGoal::Crossing;
    if (bSenlac || bGranicus)
    {
        uint64 Commander = Mission.InitialCommander();
        if (bGranicus)
        {
            const auto& Nodes = World.command_state->hierarchy.nodes;
            const auto Leaf = std::ranges::find(Nodes, Case.ObservedRegiment, &battle::command::CommandNode::regiment);
            if (!Test.TestTrue(TEXT("The regiment has a commander in the hierarchy"), Leaf != Nodes.end()))
                return false;
            Commander = Leaf->id.value;
        }
        battle::Command Acquire;
        Acquire.kind = battle::CommandKind::AcquireControl;
        Acquire.unit = RegimentOfficerOrders::FindOfficer(World, Commander);
        if (!Test.TestTrue(TEXT("The player can select the designated commander"), Commander != 0 && bool(Acquire.unit)))
            return false;
        Inputs.push_back({World.tick, Sequence++, Acquire});
    }

    const auto Tracked = size_t(Observed - World.regiments.data());
    std::vector<FFormationTravel> Travel;
    for (const auto& Regiment : World.regiments)
        Travel.push_back({Regiment.pose.position, Regiment.pose});

    int32 MeleeHits = 0;
    int32 Gunshots = 0;
    battle::Tick FirstMelee = 0;
    battle::Tick FirstArtillery = 0;
    int32 CommandRejections = 0;
    int32 AutonomousReroutes = 0;
    int32 AutonomousBlocked = 0;
    battle::OrderId ObjectiveOrder;
    bool bObjectiveOrderDelivered = false;
    bool bObjectiveOrderCompleted = false;
    bool bObjectiveOrderFailed = false;
    int32 FordFighters = 0;
    while (World.tick < Deadline && World.outcome == battle::BattleOutcome::Running)
    {
        if (World.tick == 1 && (bSenlac || bGranicus))
        {
            if (!Test.TestTrue(TEXT("BattleCore acquired control of the player officer"), bool(World.controlled)))
                return false;
            const battle::OrderId NextOrder{World.next_order_id};
            if (bSenlac)
            {
                Test.TestTrue(TEXT("The reserve awaits the player's release"), Mission.CanReleaseReserve(World));
                Mission.ReleaseReserve(World);
                if (World.next_order_id > NextOrder.value)
                    ObjectiveOrder = NextOrder;
                Test.TestTrue(TEXT("Releasing the reserve sends a movement order"), bool(ObjectiveOrder));
            }
            else
            {
                // Allow the rear ranks to clear the ford before the regiment stops.
                const battle::Vec3 FarBank{Objective.center.x + 25.f, Objective.center.y, 0.f};
                const auto Routes = RegimentOfficerOrders::PreviewManeuver(
                    World, Navigation, Case.ObservedRegiment, FarBank, {}, false);
                const auto Notice = RegimentOfficerOrders::QueueManeuver(World, Routes, battle::OrderType::MoveTo,
                    battle::MovementPace::March, Inputs, Sequence, Navigation);
                if (Test.TestTrue(TEXT("The officer can plot and dispatch a route through the selected ford"), !Inputs.empty()))
                    ObjectiveOrder = NextOrder;
                else
                    Test.AddInfo(Notice.ToString());
            }
        }

        const auto Step = battle::Step(World, Inputs, {}, Navigation);
        if (!Step)
        {
            Test.AddError(FString::Printf(TEXT("BattleCore rejected the step at tick %llu."), World.tick));
            for (const auto& Diagnostic : Step.diagnostics)
                Test.AddInfo(UTF8_TO_TCHAR(Diagnostic.message.c_str()));
            return false;
        }
        Inputs.clear();
        Mission.Update(World);

        for (size_t Index = 0; Index < World.regiments.size(); ++Index)
            ObserveTravel(Travel[Index], World.regiments[Index], Objective.center);
        for (const auto& Event : World.events)
        {
            if (Event.kind == battle::EventKind::Hit && !Event.projectile)
            {
                if (MeleeHits++ == 0)
                    FirstMelee = World.tick;
            }
            if (Event.kind == battle::EventKind::AttackLaunched &&
                (Event.reason == battle::FieldGunRoundShotReason || Event.reason == battle::FieldGunCanisterReason))
            {
                if (Gunshots++ == 0)
                    FirstArtillery = World.tick;
            }
            if (Event.kind == battle::EventKind::CommandRejected && Event.source)
            {
                ++CommandRejections;
                Test.AddInfo(FString::Printf(TEXT("Command rejected at tick %llu (%s)."),
                    Event.tick, UTF8_TO_TCHAR(Event.reason.c_str())));
            }
            if (Event.kind == battle::EventKind::DecisionTrace)
            {
                AutonomousReroutes += Event.reason == "reroute around occupied formations";
                AutonomousBlocked += Event.reason == "assigned movement route blocked by formations";
            }
        }
        if (ObjectiveOrder && Observed->order && Observed->order->id == ObjectiveOrder)
        {
            bObjectiveOrderDelivered |= Observed->order->status == battle::OrderStatus::Active ||
                Observed->order->status == battle::OrderStatus::Completed;
            bObjectiveOrderCompleted |= Observed->order->status == battle::OrderStatus::Completed;
            if (!bObjectiveOrderFailed && Observed->order->status == battle::OrderStatus::Failed)
            {
                bObjectiveOrderFailed = true;
                Test.AddInfo(FString::Printf(TEXT("Objective order failed at tick %llu at (%.1f, %.1f)."),
                    World.tick, Observed->pose.position.x, Observed->pose.position.y));
            }
        }
        if (bGranicus && Observed->pose.position.x > -20.f)
        {
            int32 Beyond = 0;
            for (const auto Id : Observed->units)
                if (const auto* Unit = battle::FindUnit(World, Id); Unit &&
                    Unit->pose.position.x >= Objective.center.x &&
                    FMath::Abs(Unit->pose.position.y - Objective.center.y) <= Objective.radius_m + 5.f)
                    ++Beyond;
            FordFighters = FMath::Max(FordFighters, Beyond);
        }
    }

    const auto Casualties = battle::GetStats(World).dead_units;
    const FString Status = Mission.Status(World).ToString().Replace(TEXT("\n"), TEXT(" / "));
    Test.AddInfo(FString::Printf(TEXT("%s: tick %llu/%llu, winner %d, casualties %llu, melee hits %d, gunshots %d, ")
        TEXT("first melee tick %llu, first artillery tick %llu, %.1f s wall time. %s"),
        *Preset.Name, World.tick, Deadline, World.winner_team, Casualties, MeleeHits, Gunshots, FirstMelee, FirstArtillery,
        FPlatformTime::Seconds() - Start, *Status));
    Test.AddInfo(FString::Printf(TEXT("Autonomous orders rerouted %d, blocked %d."),
        AutonomousReroutes, AutonomousBlocked));
    if (bSenlac || bGranicus)
        Test.AddInfo(FString::Printf(TEXT("Objective order: issued %d, delivered %d, completed %d, failed %d."),
            bool(ObjectiveOrder), bObjectiveOrderDelivered, bObjectiveOrderCompleted, bObjectiveOrderFailed));
    if (bGranicus)
        Test.AddInfo(FString::Printf(TEXT("Fighters beyond the selected ford: %d/%llu."), FordFighters, Observed->initial_count));

    for (size_t Index = 0; Index < World.regiments.size(); ++Index)
    {
        const auto& R = World.regiments[Index];
        const auto& T = Travel[Index];
        Test.AddInfo(FString::Printf(TEXT("Regiment %llu team %d: (%.1f, %.1f) to (%.1f, %.1f), ")
            TEXT("max travel %.1f m, approach %.1f m, longest stall %.1f s, size %llu/%llu, ")
            TEXT("order %d/%d, route %llu/%llu, melee %d, panic %d, disordered %d."),
            R.id.value, R.team, T.Start.x, T.Start.y, R.pose.position.x, R.pose.position.y, T.MaxTravel, T.MaxApproach,
            double(T.LongestStall) / StepsPerSecond, uint64(R.units.size()), R.initial_count,
            R.order ? int32(R.order->type) : -1, R.order ? int32(R.order->status) : -1,
            uint64(R.waypoint), uint64(R.route.size()), T.bSawMelee, R.panic, R.disordered));
    }

    Test.TestTrue(TEXT("The mission reaches a side's victory by the advertised deadline"),
        World.outcome == battle::BattleOutcome::Victory && (World.winner_team == 0 || World.winner_team == 1));
    if (IsCrestObjective(Case.Goal))
        Test.TestFalse(TEXT("The objective closes when the hill battle ends"), Mission.ShowsCrest());
    Test.TestTrue(TEXT("The run includes casualties from real combat"), Casualties > 0);
    Test.TestEqual(TEXT("The officer inputs were accepted by BattleCore"), CommandRejections, 0);
    if (bSenlac)
    {
        Test.TestTrue(TEXT("A reserve order is delivered and the reserve physically advances toward the crest"),
            bObjectiveOrderDelivered && Travel[Tracked].MaxApproach > 12.f);
        Test.TestTrue(TEXT("Norman formations actually advance toward the crest"),
            std::ranges::any_of(World.regiments, [&](const auto& R)
            {
                return R.team == 1 && Travel[size_t(&R - World.regiments.data())].MaxApproach > 20.f;
            }));
    }
    else if (bGranicus)
    {
        Test.TestTrue(TEXT("The ford order is delivered and the phalanx physically advances toward the selected ford"),
            bObjectiveOrderDelivered && Travel[Tracked].MaxApproach > 12.f);
        Test.TestTrue(TEXT("The phalanx contests or clears the ford with its fighters"),
            Travel[Tracked].bSawMelee || FordFighters > 0);
    }
    else
    {
        Test.TestTrue(TEXT("The French column advances toward the ridge"), Travel[Tracked].MaxApproach > 20.f);
        Test.TestTrue(TEXT("The artillery batteries fire during the battle"), Gunshots > 0);
    }
    return !Test.HasAnyErrors();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSenlacFullBattleTest, "RegimentBattle.HistoricalPlaythrough.Senlac",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FRegimentSenlacFullBattleTest::RunTest(const FString&)
{
    return RunHistoricalPlaythrough(*this,
        {FGuid(0xB1CED0D3, 0xC76848B1, 0xB8AAEEA9, 0x1E6E501A), EMissionGoal::HillDefense, {4}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentGranicusFullBattleTest, "RegimentBattle.HistoricalPlaythrough.Granicus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FRegimentGranicusFullBattleTest::RunTest(const FString&)
{
    return RunHistoricalPlaythrough(*this,
        {FGuid(0x47EB0F39, 0xFBB54ECC, 0xA5CC7C97, 0x44028A40), EMissionGoal::Crossing, {1}});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentWaterlooFullBattleTest, "RegimentBattle.HistoricalPlaythrough.Waterloo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FRegimentWaterlooFullBattleTest::RunTest(const FString&)
{
    return RunHistoricalPlaythrough(*this,
        {FGuid(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33), EMissionGoal::HillHold, {1}});
}

#endif
