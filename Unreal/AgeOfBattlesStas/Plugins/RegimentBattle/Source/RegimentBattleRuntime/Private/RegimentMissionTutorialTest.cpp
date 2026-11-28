#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattleField.h"
#include "RegimentMissionProgress.h"
#include "RegimentMissionScenario.h"
#include "RegimentMissionTutorial.h"
#include "RegimentOfficerOrders.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Simulation.h"
THIRD_PARTY_INCLUDES_END

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentTutorialDeploymentTest, "RegimentBattle.Tutorials.Deployment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentTutorialDeploymentTest::RunTest(const FString&)
{
    const auto Lessons = RegimentMissionTutorial::Lessons();
    TestEqual(TEXT("The academy offers four lessons"), Lessons.Num(), 4);
    for (const auto& Lesson : Lessons)
    {
        FMissionPreset Preset;
        if (!TestTrue(TEXT("The lesson opens an authored preset"), RegimentMissionTutorial::Load(Lesson.Id, Preset)))
            return false;
        FText Error;
        if (!TestTrue(FString::Printf(TEXT("%s: %s"), *Preset.Name, *Error.ToString()),
            RegimentMissionPresets::Validate(Preset, Error)))
            return false;
        const auto Opening = MakeMissionOpening(Preset);
        auto Created = battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
        if (!TestTrue(TEXT("BattleCore accepts the actual lesson deployment"), bool(Created)))
            return false;
        auto& World = Created.value;
        FRegimentMissionProgress Progress(Preset, World);
        TestFalse(TEXT("The opening instruction is visible"), Progress.Status(World).IsEmpty());
        auto* Formation = battle::FindRegiment(World, {1});
        if (!TestNotNull(TEXT("Blue regiment 1 deploys"), Formation))
            return false;
        if (Lesson.Id == ERegimentTutorial::Infantry)
        {
            TestTrue(TEXT("Infantry starts in the officer"), Progress.StartsPossessed());
            TestEqual(TEXT("The infantry lesson fields 30 people in its regiment"), Formation->initial_count, uint64(30));
            const auto Officer = RegimentOfficerOrders::FindOfficer(World, 0);
            if (!TestTrue(TEXT("The starting officer can be possessed"), battle::AcquireControl(World, Officer) == battle::Status::Ok))
                return false;
            Progress.Update(World);
            const auto Initial = Progress.Status(World).ToString();
            auto Nav = MakeMissionNavigation(World.scenario.obstacles, Preset.Field);
            battle::Command Move;
            Move.kind = battle::CommandKind::MoveControlledUnit;
            Move.unit = Officer;
            Move.velocity = {3.f, 0.f, 0.f};
            for (int32 Index = 0; Index < 40; ++Index)
            {
                TestTrue(TEXT("The officer moves in the simulated field"),
                    bool(battle::ApplyControlCommand(World, Move) == battle::Status::Ok &&
                        battle::Step(World, {}, {}, Nav)));
                Progress.Update(World);
            }
            TestTrue(TEXT("Walking unlocks the Follow instruction"), Progress.Status(World).ToString() != Initial);
            Move.kind = battle::CommandKind::SetRegimentFollowing;
            Move.regiment = {1};
            Move.follow_enabled = true;
            const auto BeforeFollow = Progress.Status(World).ToString();
            TestTrue(TEXT("The regiment accepts Follow"), battle::ApplyControlCommand(World, Move) == battle::Status::Ok);
            Progress.Update(World);
            TestTrue(TEXT("Follow unlocks the march column instruction"), Progress.Status(World).ToString() != BeforeFollow);
            TestTrue(TEXT("The second red regiment remains hidden before the firing drill"),
                Progress.HidesRegiment({MissionRegimentsPerTeam + 2}));

            std::vector<battle::CommandEnvelope> Pending;
            uint64 Sequence = 1;
            const auto DoStep = [&]
            {
                const auto Stepped = battle::Step(World, Pending, {}, Nav);
                Pending.clear();
                if (!Stepped)
                    return false;
                Progress.Update(World);
                return true;
            };
            const auto Queue = [&](ERegimentPlayerOrder Order, std::optional<battle::Vec3> Point = std::nullopt)
            {
                const auto Notice = RegimentOfficerOrders::QueueOrder(World, Nav, {1}, Order, Point, Pending, Sequence);
                return TestTrue(Notice.ToString(), !Pending.empty()) && DoStep();
            };
            if (!Queue(ERegimentPlayerOrder::Column))
                return false;
            if (!TestTrue(TEXT("An actual column gets a marker for lining up with the bridge"),
                Progress.TutorialMarker().has_value()))
                return false;

            Move.kind = battle::CommandKind::MoveControlledUnit;
            TArray<FString> CrossingTrace;
            for (int32 Index = 0; Index < 800 && Progress.TutorialMarker(); ++Index)
            {
                const auto Direction = Progress.TutorialMarker()->center - battle::FindUnit(World, Officer)->pose.position;
                const float Distance = battle::Length(Direction);
                Move.velocity = Distance > .15f ? Direction * (FMath::Min(3.f, Distance * 8.f) / Distance)
                    : battle::Vec3{};
                if (battle::ApplyControlCommand(World, Move) != battle::Status::Ok || !DoStep())
                    return false;
                if (Index % 100 == 99)
                {
                    const auto* Leader = battle::FindUnit(World, Officer);
                    const auto UnitsOnFarBank = std::ranges::count_if(Formation->units, [&](battle::UnitId Id)
                    { return battle::FindUnit(World, Id)->pose.position.x >= 7.f; });
                    CrossingTrace.Add(FString::Printf(
                        TEXT("Tick %llu: officer (%.2f, %.2f) bearing %.2f, regiment (%.2f, %.2f) bearing %.2f, following %llu, path %d/%d, files %u, far bank %d"),
                        World.tick, Leader->pose.position.x, Leader->pose.position.y, Leader->pose.yaw,
                        Formation->pose.position.x, Formation->pose.position.y, Formation->pose.yaw,
                        Formation->following_officer.value, int32(Formation->waypoint), int32(Formation->route.size()),
                        Formation->formation.columns, int32(UnitsOnFarBank)));
                }
            }
            if (!TestTrue(TEXT("The actual following formation crosses the selected bridge in column"),
                !Progress.TutorialMarker() && Formation->pose.position.x >= 13.f))
            {
                for (const auto& Line : CrossingTrace)
                    AddInfo(Line);
                return false;
            }
            Move.velocity = {};
            if (battle::ApplyControlCommand(World, Move) != battle::Status::Ok || !Queue(ERegimentPlayerOrder::Line))
                return false;
            if (!TestTrue(TEXT("A real firing line reveals the practice targets"), Progress.TutorialMarker().has_value()) ||
                !Queue(ERegimentPlayerOrder::Hold))
                return false;
            const battle::RegimentId LiveEnemy{MissionRegimentsPerTeam + 2};
            const float EnemyStart = battle::FindRegiment(World, LiveEnemy)->pose.position.x;
            int32 Shots = 0;
            int32 Hits = 0;
            TArray<FString> EnemyOrders;
            for (int32 Index = 0; Index < 1250 && Progress.HidesRegiment(LiveEnemy); ++Index)
            {
                if (!DoStep())
                    return false;
                for (const auto& Event : World.events)
                {
                    if (Event.regiment == LiveEnemy && (Event.kind == battle::EventKind::OrderChanged ||
                        Event.kind == battle::EventKind::CommandRejected) && EnemyOrders.Num() < 18)
                        EnemyOrders.Add(FString::Printf(TEXT("Enemy tick %llu: %s"), World.tick, UTF8_TO_TCHAR(Event.reason.c_str())));
                    if (Event.projectile)
                    {
                        const auto* Source = battle::FindUnit(World, Event.source);
                        if (!Source || Source->regiment != Formation->id)
                            continue;
                        Shots += Event.kind == battle::EventKind::AttackLaunched;
                        Hits += Event.kind == battle::EventKind::Hit;
                    }
                }
            }
            const auto Enemy = Progress.TutorialMarker();
            if (!TestTrue(TEXT("Infantry fire releases the second regiment and its real movement reveals it"),
                !Progress.HidesRegiment(LiveEnemy) && Enemy && Enemy->center.x < EnemyStart - 3.f))
            {
                const auto* Opponent = battle::FindRegiment(World, LiveEnemy);
                const auto* Targets = battle::FindRegiment(World, {MissionRegimentsPerTeam + 1});
                const auto* Shooter = Formation->units.empty() ? nullptr : battle::FindUnit(World, Formation->units.back());
                AddInfo(Progress.Status(World).ToString());
                AddInfo(FString::Printf(
                    TEXT("Volley %d shots %d hits, ally XY %.1f %.1f yaw %.2f following %llu order %d/%d path %d/%d, enemy XY %.1f %.1f path %d/%d order %d/%d, targets %d, firearm ready at %llu, tick %llu"),
                    Shots, Hits, Formation->pose.position.x, Formation->pose.position.y, Formation->pose.yaw,
                    Formation->following_officer.value, Formation->order ? int32(Formation->order->type) : -1,
                    Formation->order ? int32(Formation->order->status) : -1, int32(Formation->waypoint), int32(Formation->route.size()),
                    Opponent->pose.position.x, Opponent->pose.position.y, int32(Opponent->waypoint), int32(Opponent->route.size()),
                    Opponent->order ? int32(Opponent->order->type) : -1, Opponent->order ? int32(Opponent->order->status) : -1,
                    int32(Targets->units.size()), Shooter ? Shooter->ready_tick : 0, World.tick));
                for (const auto& Line : EnemyOrders)
                    AddInfo(Line);
                return false;
            }
            const auto BeforeAssault = Progress.Status(World).ToString();
            if (!Queue(ERegimentPlayerOrder::MeleeAssault, Enemy->center))
                return false;
            bool bMeleeHit = false;
            unsigned AssaultShots = 0;
            for (int32 Index = 0; Index < 450 && Progress.Status(World).ToString() == BeforeAssault; ++Index)
            {
                if (!DoStep())
                    return false;
                for (const auto& Event : World.events)
                {
                    const auto* Source = battle::FindUnit(World, Event.source);
                    const auto* TargetUnit = battle::FindUnit(World, Event.target);
                    if (!Source || Source->regiment != Formation->id)
                        continue;
                    AssaultShots += Event.kind == battle::EventKind::AttackLaunched && Event.projectile;
                    bMeleeHit |= Event.kind == battle::EventKind::Hit && !Event.projectile &&
                        TargetUnit && TargetUnit->regiment == LiveEnemy;
                }
            }
            TestEqual(TEXT("The assaulting line does not fire another volley"), AssaultShots, 0u);
            if (!TestTrue(TEXT("The chosen enemy actually takes a melee hit before the exercise can advance"), bMeleeHit))
                return false;
            const auto Fallback = Progress.TutorialMarker();
            if (!TestTrue(TEXT("Melee contact opens the retreat phase"), Fallback &&
                Fallback->center.x < Formation->pose.position.x - 2.f &&
                Formation->rally_point == Fallback->center))
                return false;
            if (Formation->melee_engaged || Formation->panic)
            {
                if (!Queue(ERegimentPlayerOrder::Withdraw))
                    return false;
            }
            else
            {
                const auto Routes = RegimentOfficerOrders::PreviewManeuver(
                    World, Nav, {1}, Fallback->center, {}, false, true);
                const auto Notice = RegimentOfficerOrders::QueueManeuver(
                    World, Routes, battle::OrderType::FallBack, battle::MovementPace::March, Pending, Sequence, Nav);
                if (!TestTrue(Notice.ToString(), !Pending.empty()) || !DoStep())
                    return false;
            }
            for (int32 Index = 0; Index < 200 && World.outcome == battle::BattleOutcome::Running; ++Index)
                if (!DoStep())
                    return false;
            TestTrue(TEXT("A real fallback ends the infantry exercise"),
                World.outcome == battle::BattleOutcome::Victory && World.winner_team == 0);
        }
        if (Lesson.Id == ERegimentTutorial::Cavalry)
        {
            TestFalse(TEXT("Cavalry starts on the map"), Progress.StartsPossessed());
            TestTrue(TEXT("The infantry reserve awaits cavalry's release"), Progress.AwaitsReserveRelease({2}));
            const auto OpeningInstruction = Progress.Status(World).ToString();
            const auto WrongOfficer = RegimentOfficerOrders::FindOfficer(World, 2);
            TestTrue(TEXT("A reserve officer can be inspected on the map"),
                battle::AcquireControl(World, WrongOfficer) == battle::Status::Ok);
            Progress.Update(World);
            TestEqual(TEXT("The reserve cannot substitute for the cavalry officer"),
                Progress.Status(World).ToString(), OpeningInstruction);
            const auto HorseOfficer = RegimentOfficerOrders::FindOfficer(World, 1);
            TestTrue(TEXT("The blue cavalry officer can be taken from the map"),
                battle::AcquireControl(World, HorseOfficer) == battle::Status::Ok);
            Progress.Update(World);
            TestTrue(TEXT("Possessing the cavalry officer reveals the first exercise marker"),
                Progress.TutorialMarker().has_value());
        }
        if (Lesson.Id == ERegimentTutorial::Command)
        {
            TestFalse(TEXT("Command training starts on the map"), Progress.StartsPossessed());
            TestTrue(TEXT("The command exercise presents several groups"), World.command_state &&
                std::ranges::count_if(World.command_state->hierarchy.nodes, [](const auto& Node)
                { return Node.team == 0 && Node.level == battle::command::Level::Group; }) == 3);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentTutorialArtilleryTest, "RegimentBattle.Tutorials.Artillery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentTutorialArtilleryTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!RegimentMissionTutorial::Load(ERegimentTutorial::Artillery, Preset))
        return false;
    auto Created = battle::CreateWorld(BuildPresetBattle(Preset, MakeMissionOpening(Preset), EMissionDeployment::AuthoredStart), 42);
    if (!TestTrue(TEXT("The battery and targets deploy"), bool(Created)))
        return false;
    auto& World = Created.value;
    FRegimentMissionProgress Progress(Preset, World);
    const auto Gun = std::ranges::find_if(World.units, [](const auto& Unit)
    { return Unit.regiment == battle::RegimentId{1} && Unit.platform == battle::BattlePlatform::FieldGun; });
    const auto Target = std::ranges::find_if(World.units, [](const auto& Unit)
    { return Unit.regiment == battle::RegimentId{7} && Unit.slot_index == 0; });
    if (!TestTrue(TEXT("The lesson contains a distant opposing formation and a working gun"),
        Gun != World.units.end() && Target != World.units.end() &&
        battle::Distance(Gun->pose.position, Target->pose.position) >= 80.f))
        return false;

    auto Nav = MakeMissionNavigation(World.scenario.obstacles, Preset.Field);
    const auto Step = [&](std::span<const battle::CommandEnvelope> Commands = {})
    {
        if (!battle::Step(World, Commands, {}, Nav))
            return false;
        Progress.Update(World);
        return true;
    };
    const uint32 Ammo = Gun->ammunition_remaining;
    if (!TestTrue(TEXT("The static drill simulates before the officer is taken"), Step()))
        return false;
    TestEqual(TEXT("An unattended tutorial gun does not fire"), Gun->ammunition_remaining, Ammo);

    const auto Officer = RegimentOfficerOrders::FindOfficer(World, 1);
    if (!TestTrue(TEXT("Taking the battery's officer is possible"), battle::AcquireControl(World, Officer) == battle::Status::Ok))
        return false;
    Progress.Update(World);
    if (!TestTrue(TEXT("The occupied battery simulates while waiting for an order"), Step()))
        return false;
    TestEqual(TEXT("The possessed battery waits for the player"), Gun->ammunition_remaining, Ammo);

    battle::Command Fire;
    Fire.kind = battle::CommandKind::FireFieldGun;
    Fire.unit = Officer;
    Fire.target = Target->id;
    for (uint32 Round = 0; Round < Ammo && World.outcome == battle::BattleOutcome::Running; ++Round)
    {
        const battle::CommandEnvelope Packet{World.tick, uint64(Round) + 1, Fire};
        if (!TestTrue(TEXT("The officer's manual fire command advances the battle"),
            Step(std::span<const battle::CommandEnvelope>(&Packet, 1))))
            return false;
        if (!TestEqual(TEXT("The command consumes one real artillery round"), Gun->ammunition_remaining, Ammo - Round - 1))
            return false;
        TestTrue(TEXT("The long-distance launch advances without completing the exercise"),
            std::ranges::any_of(World.events, [Gun](const auto& Event)
            { return Event.kind == battle::EventKind::AttackLaunched && Event.source == Gun->id &&
                Event.reason == battle::FieldGunRoundShotReason; }) && World.outcome == battle::BattleOutcome::Running);
        while (World.outcome == battle::BattleOutcome::Running && World.tick < Gun->ready_tick)
            if (!TestTrue(TEXT("A fired round resolves during live simulation"), Step()))
                return false;
    }
    TestTrue(TEXT("A manually fired round hitting the opposing regiment completes the lesson"),
        World.outcome == battle::BattleOutcome::Victory && World.winner_team == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentTutorialCavalryTest, "RegimentBattle.Tutorials.Cavalry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentTutorialCavalryTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!RegimentMissionTutorial::Load(ERegimentTutorial::Cavalry, Preset))
        return false;
    auto Created = battle::CreateWorld(BuildPresetBattle(Preset, MakeMissionOpening(Preset), EMissionDeployment::AuthoredStart), 42);
    if (!TestTrue(TEXT("The cavalry and the reserve deploy"), bool(Created)))
        return false;
    auto& World = Created.value;
    FRegimentMissionProgress Progress(Preset, World);
    auto Nav = MakeMissionNavigation(World.scenario.obstacles, Preset.Field);
    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 1;
    const auto Step = [&]
    {
        const auto Stepped = battle::Step(World, Pending, {}, Nav);
        Pending.clear();
        if (!Stepped)
            return false;
        Progress.Update(World);
        return true;
    };
    const auto Plot = [&](battle::RegimentId Id, battle::OrderType Type, battle::Vec3 Destination)
    {
        const auto Route = RegimentOfficerOrders::PreviewManeuver(
            World, Nav, Id, Destination, {}, false, Type == battle::OrderType::FallBack);
        const auto Notice = RegimentOfficerOrders::QueueManeuver(
            World, Route, Type, battle::MovementPace::March, Pending, Sequence, Nav);
        return TestTrue(Notice.ToString(), !Pending.empty()) && Step();
    };
    const auto Order = [&](battle::RegimentId Id, ERegimentPlayerOrder Action, std::optional<battle::Vec3> Point = {})
    {
        const auto Notice = RegimentOfficerOrders::QueueOrder(World, Nav, Id, Action, Point, Pending, Sequence);
        return TestTrue(Notice.ToString(), !Pending.empty()) && Step();
    };
    const auto* Horse = battle::FindRegiment(World, {1});
    const auto* Reserve = battle::FindRegiment(World, {2});
    if (!Horse || !Reserve ||
        battle::AcquireControl(World, RegimentOfficerOrders::FindOfficer(World, 1)) != battle::Status::Ok || !Step())
        return false;

    for (int32 Maneuver = 0; Maneuver < 2; ++Maneuver)
    {
        const auto Mark = Progress.TutorialMarker();
        if (!TestTrue(TEXT("The cavalry maneuver has a checkpoint"), Mark.has_value()) ||
            !Plot(Horse->id, battle::OrderType::MoveTo, Mark->center))
            return false;
        for (int32 Tick = 0; Tick < 330 && Progress.TutorialMarker() &&
            Progress.TutorialMarker()->center == Mark->center; ++Tick)
            if (!Step())
                return false;
        if (!TestTrue(TEXT("The actual mounted formation reaches this checkpoint"),
            Progress.TutorialMarker() && Progress.TutorialMarker()->center != Mark->center &&
            battle::Distance(Horse->pose.position, Mark->center) <= Mark->radius_m))
            return false;
        TestTrue(TEXT("The infantry waits while cavalry maneuvers"), Progress.AwaitsReserveRelease(Reserve->id));
        if (Maneuver == 0)
        {
            const auto* FlankOpponent = battle::FindRegiment(World, {MissionRegimentsPerTeam + 1});
            const auto FlankTarget = Progress.TutorialMarker();
            if (!TestTrue(TEXT("Reaching the flank opens an attack, not a rear maneuver"),
                FlankOpponent && FlankTarget && FlankTarget->center == FlankOpponent->pose.position))
                return false;
            if (!Order(Horse->id, ERegimentPlayerOrder::MeleeAssault, FlankTarget->center))
                return false;
            const auto BeforeAttack = Horse->pose.position;
            for (int32 Tick = 0; Tick < 105 && Progress.TutorialMarker() &&
                Progress.TutorialMarker()->center == FlankTarget->center; ++Tick)
                if (!Step())
                    return false;
            if (!TestTrue(TEXT("The player attack is delivered on the flank before the route opens to the rear"),
                Progress.TutorialMarker() && Progress.TutorialMarker()->center != FlankTarget->center &&
                Horse->order && Horse->order->type == battle::OrderType::MeleeAssault &&
                Horse->order->target_regiment == FlankOpponent->id &&
                Horse->order->priority == battle::PlayerOrderPriority &&
                battle::Distance(Horse->pose.position, BeforeAttack) >= 2.5f && !Horse->melee_engaged))
                return false;
        }
    }

    const auto Enemy = Progress.TutorialMarker();
    if (!Enemy || !Order(Horse->id, ERegimentPlayerOrder::MeleeAssault, Enemy->center))
        return false;
    for (int32 Tick = 0; Tick < 420 && Progress.TutorialMarker() &&
        Progress.TutorialMarker()->center == Enemy->center; ++Tick)
        if (!Step())
            return false;
    const auto Retreat = Progress.TutorialMarker();
    if (!TestTrue(TEXT("A rear attack creates an exit instruction"),
        Retreat && Retreat->center != Enemy->center))
        return false;
    if (Horse->melee_engaged ? !Order(Horse->id, ERegimentPlayerOrder::Withdraw)
                            : !Plot(Horse->id, battle::OrderType::FallBack, Retreat->center))
        return false;
    for (int32 Tick = 0; Tick < 1050 && Progress.AwaitsReserveRelease(Reserve->id); ++Tick)
        if (!Step())
            return false;

    if (!TestTrue(TEXT("The routed cavalry is removed by simulation before reserve release"),
        !Progress.AwaitsReserveRelease(Reserve->id) && Horse->units.empty() && !World.controlled))
        return false;
    if (battle::AcquireControl(World, RegimentOfficerOrders::FindOfficer(World, 2)) != battle::Status::Ok ||
        !Step() || !Order(Reserve->id, ERegimentPlayerOrder::Line) ||
        !Order(Reserve->id, ERegimentPlayerOrder::Hold))
        return false;
    for (int32 Tick = 0; Tick < 350 && World.outcome == battle::BattleOutcome::Running; ++Tick)
        if (!Step())
            return false;
    TestTrue(TEXT("The reserve formation fires and completes the exercise"),
        World.outcome == battle::BattleOutcome::Victory && World.winner_team == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentTutorialCommandTest, "RegimentBattle.Tutorials.Command",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentTutorialCommandTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!RegimentMissionTutorial::Load(ERegimentTutorial::Command, Preset))
        return false;
    auto Created = battle::CreateWorld(BuildPresetBattle(Preset, MakeMissionOpening(Preset), EMissionDeployment::AuthoredStart), 42);
    if (!TestTrue(TEXT("The command hierarchy deploys"), bool(Created) && Created.value.command_state.has_value()))
        return false;
    auto& World = Created.value;
    FRegimentMissionProgress Progress(Preset, World);
    const auto& Hierarchy = World.command_state->hierarchy;
    const auto FindNode = [&](battle::command::Level Level)
    {
        return std::ranges::find_if(Hierarchy.nodes, [Level](const auto& Node)
        { return Node.level == Level && Node.team == 0; });
    };
    const auto Group = FindNode(battle::command::Level::Group);
    const auto Army = FindNode(battle::command::Level::Army);
    if (!TestTrue(TEXT("The blue army and a subordinate group have officers"),
        Group != Hierarchy.nodes.end() && Army != Hierarchy.nodes.end()))
        return false;
    const auto GroupSkill = std::ranges::find(Hierarchy.officers, Group->commander,
        &battle::command::OfficerProfile::officer);
    TestTrue(TEXT("The group panel has authored skill and trait data"), GroupSkill != Hierarchy.officers.end() &&
        GroupSkill->tactical_skill > 0.f && GroupSkill->discipline > 0.f);

    if (battle::AcquireControl(World, RegimentOfficerOrders::FindOfficer(World, Group->id.value)) != battle::Status::Ok)
        return false;
    auto Nav = MakeMissionNavigation(World.scenario.obstacles, Preset.Field);
    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 1;
    bool bPanel = false;
    const auto Step = [&]
    {
        const auto Stepped = battle::Step(World, Pending, {}, Nav);
        Pending.clear();
        if (!Stepped)
            return false;
        Progress.Update(World, bPanel);
        return true;
    };
    if (!Step())
        return false;
    const auto OpenGroup = Progress.Status(World).ToString();
    bPanel = true;
    if (!Step() || !TestTrue(TEXT("Opening the group panel enables its practical order"),
        Progress.Status(World).ToString() != OpenGroup))
        return false;
    const battle::RegimentId Selected{Group->children.front().value};
    const auto Local = RegimentOfficerOrders::HoldRecipients(World, Selected, false);
    if (!TestTrue(TEXT("Group holding order has two real recipients"), Local.size() >= 2))
        return false;
    for (const auto Id : Local)
        RegimentOfficerOrders::QueueOrder(World, Nav, Id, ERegimentPlayerOrder::Hold, {}, Pending, Sequence);
    const auto GroupInstruction = Progress.Status(World).ToString();
    for (int32 Tick = 0; Tick < 45 && Progress.Status(World).ToString() == GroupInstruction; ++Tick)
        if (!Step())
            return false;
    if (!TestTrue(TEXT("Actual regiment order delivery unlocks army command"), Progress.Status(World).ToString() != GroupInstruction))
        return false;

    bPanel = false;
    if (battle::AcquireControl(World, RegimentOfficerOrders::FindOfficer(World, Army->id.value)) != battle::Status::Ok ||
        !Step())
        return false;
    bPanel = true;
    if (!Step())
        return false;
    const auto Wing = RegimentOfficerOrders::HoldRecipients(World, Selected, true);
    if (!TestTrue(TEXT("Army holding order uses the actual courier channel"),
        RegimentOfficerOrders::QueueStaffHold(World, Wing, Pending, Sequence).has_value()))
        return false;
    for (int32 Tick = 0; Tick < 550 && World.outcome == battle::BattleOutcome::Running; ++Tick)
        if (!Step())
            return false;
    TestTrue(TEXT("Delivery of the army order completes the lesson"),
        World.outcome == battle::BattleOutcome::Victory && World.winner_team == 0);
    return true;
}
#endif
