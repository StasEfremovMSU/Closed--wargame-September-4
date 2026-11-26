#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentMissionProgress.h"
#include "RegimentMissionScenario.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Formation.h"
#include "battle/Simulation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
FMissionPreset FindBuiltIn(EMissionGoal Goal)
{
    for (const auto& Item : RegimentMissionPresets::BuiltIns())
    {
        FMissionPreset Preset;
        if (RegimentMissionPresets::LoadBuiltIn(Item.Id, Preset) && Preset.Objective.Goal == Goal)
            return Preset;
    }
    return {};
}

auto CreateAuthoredWorld(const FMissionPreset& Preset)
{
    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain);
    return battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCrossingObjectiveTest, "RegimentBattle.MissionObjectives.Crossing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCrossingObjectiveTest::RunTest(const FString&)
{
    const auto Preset = FindBuiltIn(EMissionGoal::Crossing);
    FText Error;
    if (!TestTrue(TEXT("Stirling is a valid built-in preset"), RegimentMissionPresets::Validate(Preset, Error)) ||
        !TestEqual(TEXT("Historical preset has a single crossing"), int32(battle::MarchCrossingsFor(Preset.Terrain).size()), 1))
        return false;
    TestEqual(TEXT("Historical briefing keeps both source records"), Preset.Sources.Num(), 2);

    auto River = Preset;
    River.Terrain = battle::MarchTerrain::River;
    auto Created = CreateAuthoredWorld(River);
    if (!TestTrue(TEXT("The selected-crossing variant deploys"), bool(Created)))
        return false;
    auto& World = Created.value;
    FRegimentMissionProgress Progress(River, World);
    auto* Vanguard = battle::FindRegiment(World, {MissionRegimentsPerTeam + 1});
    if (!TestNotNull(TEXT("English vanguard is on the attacking bank"), Vanguard))
        return false;
    const auto PlaceVanguard = [&](float X, float Y)
    {
        Vanguard->pose.position = {X, Y, 0.f};
        for (const auto Id : Vanguard->units)
            if (auto* Fighter = battle::FindUnit(World, Id))
                Fighter->pose.position = Vanguard->pose.position;
        ++World.tick;
        Progress.Update(World);
    };
    PlaceVanguard(0.f, battle::MarchCrossings.front().y);
    PlaceVanguard(-8.f, 0.f);
    TestTrue(TEXT("A different crossing cannot satisfy the objective"), World.outcome == battle::BattleOutcome::Running);
    PlaceVanguard(8.f, 0.f);
    PlaceVanguard(0.f, 0.f);
    PlaceVanguard(-8.f, 0.f);
    TestTrue(TEXT("A regiment through the selected crossing wins"), World.outcome == battle::BattleOutcome::Victory);
    TestEqual(TEXT("Attacking team wins the crossing"), World.winner_team, 1);

    auto Deadline = CreateAuthoredWorld(Preset);
    if (!TestTrue(TEXT("Historical deployment is accepted by BattleCore"), bool(Deadline)))
        return false;
    FRegimentMissionProgress Defense(Preset, Deadline.value);
    Deadline.value.tick = Preset.Objective.TimeLimitSeconds * 20;
    Defense.Update(Deadline.value);
    TestTrue(TEXT("Deadline ends the battle"), Deadline.value.outcome == battle::BattleOutcome::Victory);
    TestEqual(TEXT("Defending team wins at deadline"), Deadline.value.winner_team, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentReserveObjectiveTest, "RegimentBattle.MissionObjectives.ReserveExercise",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentReserveObjectiveTest::RunTest(const FString&)
{
    const auto Preset = FindBuiltIn(EMissionGoal::ReserveExercise);
    FText Error;
    if (!TestTrue(TEXT("Reserve exercise is a valid built-in"), RegimentMissionPresets::Validate(Preset, Error)))
        return false;
    auto Created = CreateAuthoredWorld(Preset);
    if (!TestTrue(TEXT("Reserve exercise deploys in BattleCore"), bool(Created)))
        return false;
    auto& World = Created.value;
    const battle::GridNavigationService Navigation(
        World.scenario.obstacles, 1.f, 121, 77, {-60.f, -38.f, 0.f});
    FRegimentMissionProgress Progress(Preset, World);
    const auto Step = [&]
    {
        if (!battle::Step(World, {}, {}, Navigation))
            return false;
        Progress.Update(World);
        return true;
    };
    while (World.tick < 40)
        if (!TestTrue(TEXT("Simulation can advance while holding the reserve"), Step()))
            return false;
    const auto* Reserve = battle::FindRegiment(World, {2});
    if (!TestNotNull(TEXT("The exercise has a reserve regiment"), Reserve))
        return false;
    TestTrue(TEXT("Reserve is holding before release"), Reserve->order && Reserve->order->type == battle::OrderType::Hold);
    TestTrue(TEXT("Reserve waits for the exercise release"), Progress.AwaitsReserveRelease(Reserve->id));
    TestFalse(TEXT("Lead regiment can still take orders"), Progress.AwaitsReserveRelease({1}));
    TestTrue(TEXT("Player can release the reserve"), Progress.CanReleaseReserve(World));
    Progress.ReleaseReserve(World);
    TestFalse(TEXT("Player can only issue this order once"), Progress.CanReleaseReserve(World));
    while (World.tick < 60)
        if (!TestTrue(TEXT("Simulation steps through the delivery delay"), Step()))
            return false;
    TestTrue(TEXT("Previous order is still active before delivery"), Reserve->order && Reserve->order->type == battle::OrderType::Hold);
    TestTrue(TEXT("Manual orders remain gated while release is in transit"), Progress.AwaitsReserveRelease(Reserve->id));
    if (!TestTrue(TEXT("Order is processed on its delivery tick"), Step()))
        return false;
    TestTrue(TEXT("Reserve receives the player's attack order"), Reserve->order && Reserve->order->type == battle::OrderType::Attack);
    TestFalse(TEXT("Manual orders are available after delivery"), Progress.AwaitsReserveRelease(Reserve->id));
    while (World.outcome == battle::BattleOutcome::Running && World.tick <= 1200)
        if (!TestTrue(TEXT("Exercise advances to first contact"), Step()))
            return false;
    TestTrue(TEXT("Exercise stops at its result"), World.outcome == battle::BattleOutcome::Victory);
    TestEqual(TEXT("This timely release succeeds"), World.winner_team, 0);
    TestTrue(TEXT("Report records the issued and delivered times"),
        Progress.Report().ToString().Contains(TEXT("2.0")) && Progress.Report().ToString().Contains(TEXT("3.0")));
    TestTrue(TEXT("Winning exercise records first contact"), Reserve->melee_engaged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentTacticsCampaignTest, "RegimentBattle.RegimentTactics.Campaign",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentTacticsCampaignTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain);
    battle::MarchMission March(std::move(Opening), Preset.Terrain, Preset.bRegimentTactics);
    // Let normal staging reach the first sighting. Set destinations on each of the three crossings.
    for (int32 Step = 0; Step < 1400 && !March.EnemyFound(); ++Step)
        March.Step(.05f);
    if (!TestTrue(TEXT("Travelling columns reach sighting distance"), March.EnemyFound()))
        return false;
    for (int32 Index = 0; Index < MissionRegimentsPerTeam; ++Index)
    {
        const auto Lane = battle::MarchCrossings[Index / MissionDefaultRegimentsPerGroup];
        if (!TestTrue(TEXT("March receives a viable waypoint"), March.SetWaypoint({uint64(Index + 1)}, {18.f, Lane.y, 0.f})))
            return false;
    }
    TestTrue(TEXT("One regiment deploys before resuming march"), March.SetPosture({1}, battle::MarchPosture::BattleLine));
    for (int32 Step = 0; Step < 1400 && !March.Encounter(); ++Step)
        March.Step(.05f);
    if (!TestTrue(TEXT("March transitions to tactical battle"), March.Encounter().has_value()))
        return false;
    const auto Scenario = BuildPresetBattle(Preset, March.Regiments(), EMissionDeployment::MarchContact);
    if (!TestTrue(TEXT("The new policy is enabled for the default mission"), Scenario.rules.regiment_tactics))
        return false;
    TestEqual(TEXT("All companies survive the handoff"), Scenario.regiments.size(), size_t(MissionRegimentCount));
    auto Created = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("Deployment produces a real BattleCore world"), bool(Created)))
        return false;
    auto& World = Created.value;
    const auto* Column = battle::FindRegiment(World, {2});
    if (!TestNotNull(TEXT("The unreformed second regiment exists"), Column) ||
        !TestTrue(TEXT("The second regiment retains its travel fatigue"), Column->fatigue > 0.f) ||
        !TestTrue(TEXT("The second regiment starts deploying"), Column->deployment_ticks > 0u))
        return false;
    const battle::Tick Until = Column->deployment_ticks + 2;
    const uint32 ColumnFiles = Column->formation.columns;
    const battle::GridNavigationService Navigation(
        Scenario.obstacles, 1.f, 121, 77, {-60.f, -38.f, 0.f});
    while (World.tick < Until)
        if (!TestTrue(TEXT("BattleCore advances in real time during deployment"), bool(battle::Step(World, {}, {}, Navigation))))
            return false;
    TestTrue(TEXT("The column expands to its combat formation"), Column->formation.columns > ColumnFiles);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHillHoldObjectiveTest, "RegimentBattle.MissionObjectives.HillHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHillHoldObjectiveTest::RunTest(const FString&)
{
    constexpr auto Goal = EMissionGoal::HillHold;
    const auto Preset = FindBuiltIn(Goal);
    if (!TestEqual(TEXT("Hill exercise is a built-in"), Preset.Objective.Goal, Goal))
        return false;
    auto Created = CreateAuthoredWorld(Preset);
    if (!TestTrue(TEXT("Hill exercise deploys in BattleCore"), bool(Created)) ||
        !TestTrue(TEXT("The objective shares the deployed hill geometry"), Created.value.scenario.hill.has_value()))
        return false;
    auto& World = Created.value;
    auto* Blue = battle::FindRegiment(World, {1});
    auto* Orange = battle::FindRegiment(World, {MissionRegimentsPerTeam + 1});
    if (!TestNotNull(TEXT("The blue lead can occupy the hill"), Blue) ||
        !TestNotNull(TEXT("The orange lead can contest it"), Orange))
        return false;
    FRegimentMissionProgress GoalProgress(Preset, World);

    auto Advance = [&](int32 Ticks)
    {
        for (int32 Tick = 0; Tick < Ticks; ++Tick)
        {
            ++World.tick;
            GoalProgress.Update(World);
        }
    };
    auto Place = [&](const battle::RegimentState& Regiment, size_t Inside)
    {
        for (size_t Index = 0; Index < Regiment.units.size(); ++Index)
        {
            auto* Fighter = battle::FindUnit(World, Regiment.units[Index]);
            Fighter->pose.position = Index < Inside ? battle::Vec3{0, 0, 8} : Regiment.pose.position;
        }
    };

    const size_t HalfOriginal = size_t((Blue->initial_count + 1) / 2);
    Place(*Blue, HalfOriginal - 1);
    Advance(20);
    auto* Detached = battle::FindUnit(World, Blue->units[HalfOriginal - 1]);
    Place(*Blue, HalfOriginal);
    Detached->skirmisher = true;
    Advance(20);
    Detached->skirmisher = false;
    TestTrue(TEXT("A detached skirmisher does not fill the last place required to hold"),
        GoalProgress.Report().ToString().Contains(TEXT("—")));

    Detached->control = battle::ControlMode::PlayerControlled;
    Advance(1);
    TestTrue(TEXT("A possessed officer does not count as part of the formation"),
        GoalProgress.Report().ToString().Contains(TEXT("—")));
    Detached->control = battle::ControlMode::FormationControlled;
    Blue->disordered = true;
    Advance(1);
    TestTrue(TEXT("A disordered regiment does not capture the crest"),
        GoalProgress.Report().ToString().Contains(TEXT("—")));
    Blue->disordered = false;
    Blue->deployment_ticks = World.tick + 2;
    Advance(1);
    TestTrue(TEXT("A regiment still deploying does not capture the crest"),
        GoalProgress.Report().ToString().Contains(TEXT("—")));
    Blue->deployment_ticks = 0;

    Advance(120);
    Place(*Orange, 1);
    Advance(1);
    TestTrue(TEXT("One formed enemy contests the crest"),
        GoalProgress.Status(World).ToString().Contains(TEXT("оспаривает")));
    Orange->panic = true;
    Advance(399);
    TestTrue(TEXT("An earlier streak is not added to the new streak"), World.outcome == battle::BattleOutcome::Running);
    GoalProgress.Update(World);
    TestTrue(TEXT("Two updates on the same tick do not extend the streak"), World.outcome == battle::BattleOutcome::Running);
    Advance(1);
    TestTrue(TEXT("A routed enemy cannot contest a continuous 20-second hold"), World.outcome == battle::BattleOutcome::Victory);
    TestEqual(TEXT("Blue wins for holding the crest"), World.winner_team, 0);
    TestTrue(TEXT("Debrief records the longest streak"),
        GoalProgress.Report().ToString().Contains(TEXT("20.0")));
    TestFalse(TEXT("Debrief records the first secure control time"),
        GoalProgress.Report().ToString().Contains(TEXT("—")));

    auto Timeout = CreateAuthoredWorld(Preset);
    if (!TestTrue(TEXT("A second world starts the exercise fresh"), bool(Timeout)))
        return false;
    FRegimentMissionProgress Deadline(Preset, Timeout.value);
    Timeout.value.outcome = battle::BattleOutcome::Victory;
    Timeout.value.winner_team = 0;
    Timeout.value.tick = 20;
    Deadline.Update(Timeout.value);
    TestTrue(TEXT("Eliminating orange still leaves blue to capture the crest"),
        Timeout.value.outcome == battle::BattleOutcome::Running);
    Timeout.value.tick = Preset.Objective.TimeLimitSeconds * 20;
    Deadline.Update(Timeout.value);
    TestEqual(TEXT("Orange wins if blue misses the deadline"), Timeout.value.winner_team, 1);

    auto Lost = CreateAuthoredWorld(Preset);
    if (!TestTrue(TEXT("A third world starts the exercise fresh"), bool(Lost)))
        return false;
    FRegimentMissionProgress Defeat(Preset, Lost.value);
    Lost.value.outcome = battle::BattleOutcome::Victory;
    Lost.value.winner_team = 1;
    Lost.value.tick = 20;
    Defeat.Update(Lost.value);
    TestTrue(TEXT("Losing all blue regiments ends the exercise immediately"),
        Defeat.Status(Lost.value).ToString().Contains(TEXT("разгромлены")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHillHoldRouteTest, "RegimentBattle.MissionObjectives.HillHoldRoute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHillHoldRouteTest::RunTest(const FString&)
{
    const auto Preset = FindBuiltIn(EMissionGoal::HillHold);
    auto Created = CreateAuthoredWorld(Preset);
    if (!TestTrue(TEXT("The authored battle and crest exist"), bool(Created) && Created.value.scenario.hill.has_value()))
        return false;
    auto& World = Created.value;
    const auto& Hill = *World.scenario.hill;
    const auto Navigation = MakeMissionNavigation(World.scenario.obstacles, Preset.Field);
    const battle::Vec3 Crest{};
    const battle::RegimentId Blue{1};
    const auto* Lead = battle::FindRegiment(World, Blue);
    if (!TestNotNull(TEXT("The moving regiment exists"), Lead))
        return false;
    for (const auto& Regiment : World.regiments)
    {
        const auto Path = Navigation.FindPath({Regiment.pose, Crest, battle::Footprint(Regiment)});
        if (!TestTrue(TEXT("Both sides' authored positions can reach the crest"),
            Path.status == battle::Status::Ok && !Path.waypoints.empty() &&
            battle::OnHillCrest(Hill, Path.waypoints.back().position)))
            return false;
        // Isolate success of the route and objective from chance in the combat trials.
        // The defending regiments remain alive at their authored positions.
        battle::Order Order;
        Order.recipient = Regiment.id;
        Order.priority = battle::PlayerOrderPriority;
        Order.type = Regiment.id == Blue ? battle::OrderType::MoveTo : battle::OrderType::Hold;
        Order.point = Regiment.id == Blue ? Crest : Regiment.pose.position;
        if (!TestTrue(TEXT("The simulation accepts the exercise order"), battle::SubmitOrder(World, Order) == battle::Status::Ok))
            return false;
    }

    FRegimentMissionProgress Progress(Preset, World);
    bool bHoldIssued = false;
    while (World.outcome == battle::BattleOutcome::Running && World.tick < 1000)
    {
        if (!TestTrue(TEXT("The simulation can move the regiment and advance the objective"),
            bool(battle::Step(World, {}, {}, Navigation))))
            return false;
        Progress.Update(World);
        if (!bHoldIssued && Lead->order && Lead->order->type == battle::OrderType::MoveTo &&
            Lead->order->status == battle::OrderStatus::Completed)
        {
            battle::Order Hold;
            Hold.recipient = Blue;
            Hold.priority = battle::PlayerOrderPriority;
            Hold.type = battle::OrderType::Hold;
            Hold.point = Crest;
            if (!TestTrue(TEXT("A regiment on the crest can receive a hold order"), battle::SubmitOrder(World, Hold) == battle::Status::Ok))
                return false;
            bHoldIssued = true;
        }
    }
    if (!TestTrue(TEXT("The moving regiment can route onto the crest and hold it before the deadline"),
        bHoldIssued && World.outcome == battle::BattleOutcome::Victory && World.winner_team == 0))
    {
        AddInfo(FString::Printf(TEXT("tick=%llu blue=(%.2f,%.2f,%.2f) order_type=%d order_status=%d route=%llu/%llu ")
            TEXT("disordered=%d panic=%d hold_issued=%d status=%s report=%s"),
            static_cast<unsigned long long>(World.tick), Lead->pose.position.x, Lead->pose.position.y, Lead->pose.position.z,
            Lead->order ? int32(Lead->order->type) : -1, Lead->order ? int32(Lead->order->status) : -1,
            static_cast<unsigned long long>(Lead->waypoint), static_cast<unsigned long long>(Lead->route.size()),
            Lead->disordered, Lead->panic, bHoldIssued, *Progress.Status(World).ToString(), *Progress.Report().ToString()));
    }
    return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSenlacDefenseTest, "RegimentBattle.MissionObjectives.SenlacDefense",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentSenlacDefenseTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    const FGuid Defense(0xB1CED0D3, 0xC76848B1, 0xB8AAEEA9, 0x1E6E501A);
    if (!TestTrue(TEXT("The Hastings defense is available in the scenario catalogue"),
        RegimentMissionPresets::LoadBuiltIn(Defense, Preset)))
        return false;
    TestEqual(TEXT("The English are the player side"), Preset.Era.BlueFaction, FString(TEXT("anglo_saxons")));
    const auto Opening = MakeMissionOpening(Preset);
    auto Created = battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
    if (!TestTrue(TEXT("The defensive battle deploys"), bool(Created) && Created.value.scenario.hill.has_value()))
        return false;
    auto& World = Created.value;
    FRegimentMissionProgress Progress(Preset, World);
    TestTrue(TEXT("The player begins in the commander of the reserve group"),
        Progress.StartsPossessed() && Progress.InitialCommander() != 0);
    TestTrue(TEXT("The designated reserve awaits orders at the rear"), Progress.AwaitsReserveRelease({4}));
    TestTrue(TEXT("The player can introduce the reserve"), Progress.CanReleaseReserve(World));
    if (TestTrue(TEXT("The crest has a defending side"), World.scenario.objective.has_value()))
        TestEqual(TEXT("Blue defends the crest"), World.scenario.objective->defending_team, 0);
    const auto* Lead = battle::FindRegiment(World, battle::RegimentId{1});
    if (TestNotNull(TEXT("The line regiment exists"), Lead))
        TestTrue(TEXT("The English line faces the Normans on the west side"), battle::Forward(Lead->pose.yaw).x < -.9f);
    Progress.ReleaseReserve(World);
    TestFalse(TEXT("The reserve command cannot be repeated in transit"), Progress.CanReleaseReserve(World));

    const auto* Rear = battle::FindRegiment(World, battle::RegimentId{4});
    if (!TestNotNull(TEXT("The reserve exists"), Rear))
        return false;
    const auto RearStart = Rear->pose.position;
    const auto Navigation = MakeMissionNavigation(World.scenario.obstacles, Preset.Field);
    bool bArrived = false;
    for (int32 Frame = 0; Frame < 550 && World.outcome == battle::BattleOutcome::Running; ++Frame)
    {
        if (!battle::Step(World, {}, {}, Navigation))
        {
            AddError(TEXT("The simulation failed while the reserve was moving"));
            return false;
        }
        Progress.Update(World);
        if (Rear->order && Rear->order->type == battle::OrderType::MoveTo &&
            Rear->order->status == battle::OrderStatus::Completed)
        {
            bArrived = true;
            break;
        }
    }
    TestTrue(TEXT("The reserve physically moves toward the line after delivery"),
        Rear->pose.position.x < RearStart.x - 12.f);
    TestTrue(TEXT("The reserve completes the route to the crest"), bArrived);
    if (!bArrived)
        AddInfo(FString::Printf(TEXT("tick=%llu reserve=(%.1f, %.1f) lead=(%.1f, %.1f) order=%d/%d route=%llu/%llu "
            "engaged=%d disordered=%d panic=%d size=%.1fx%.1f"),
            static_cast<unsigned long long>(World.tick), Rear->pose.position.x, Rear->pose.position.y,
            Lead ? Lead->pose.position.x : 0.f, Lead ? Lead->pose.position.y : 0.f,
            Rear->order ? int32(Rear->order->type) : -1, Rear->order ? int32(Rear->order->status) : -1,
            static_cast<unsigned long long>(Rear->waypoint), static_cast<unsigned long long>(Rear->route.size()),
            Rear->melee_engaged, Rear->disordered, Rear->panic, battle::Footprint(*Rear).width_m, battle::Footprint(*Rear).depth_m));

    auto Loss = battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
    if (!TestTrue(TEXT("A fresh defense can be started"), bool(Loss)))
        return false;
    FRegimentMissionProgress Contest(Preset, Loss.value);
    const auto Crest = battle::HillObjectiveCrest(*Loss.value.scenario.hill);
    for (const auto& Regiment : Loss.value.regiments)
        for (const auto Fighter : Regiment.units)
            if (auto* Unit = battle::FindUnit(Loss.value, Fighter))
                Unit->pose.position = Regiment.team == 1 ? Crest->center : battle::Vec3{250.f, 0.f, 0.f};
    while (Loss.value.tick < 399)
    {
        ++Loss.value.tick;
        Contest.Update(Loss.value);
    }
    TestEqual(TEXT("A recapture is possible before twenty uninterrupted seconds"),
        Loss.value.outcome, battle::BattleOutcome::Running);
    ++Loss.value.tick;
    Contest.Update(Loss.value);
    TestEqual(TEXT("The attackers win after twenty seconds in sole control"), Loss.value.winner_team, 1);
    return !HasAnyErrors();
}

#endif
