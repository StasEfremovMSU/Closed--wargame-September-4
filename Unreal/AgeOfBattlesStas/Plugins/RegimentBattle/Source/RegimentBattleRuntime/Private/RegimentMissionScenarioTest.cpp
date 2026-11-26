#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentMissionScenario.h"
#include "battle/Formation.h"
#include "battle/Simulation.h"
#include "battle/Terrain.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMeetingEngagementTest, "RegimentBattle.Mission.MeetingEngagement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMeetingEngagementTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.Field = {360, 200};
    Preset.bDirectDeployment = true;
    Preset.bAdaptiveBattle = true;
    for (int32 Index = 0; Index < MissionRegimentCount; ++Index)
    {
        auto& Entry = Preset.Roster[Index];
        const int32 Slot = Index % MissionRegimentsPerTeam;
        const bool bBlue = Index < MissionRegimentsPerTeam;
        Entry.bEnabled = Slot < 2;
        Entry.Group = 2;
        Entry.StartX = bBlue ? -62.f : 62.f;
        Entry.StartY = Slot == 0 ? 0.f : 24.f;
    }

    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
    const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    auto Created = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("The meeting engagement loads through the mission builder"), bool(Created)))
        return false;
    auto& World = Created.value;
    const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
    const auto Separation = [&World]
    {
        const auto* Blue = battle::FindRegiment(World, {1});
        const auto* Orange = battle::FindRegiment(World, {MissionRegimentsPerTeam + 1});
        return Orange->pose.position.x - Blue->pose.position.x;
    };
    const float Start = Separation();
    TestTrue(TEXT("The armies start beyond the generals' normal 90 m observation radius"), Start > 120.f);
    float Closest = Start;
    for (int32 Tick = 0; Tick < 600 && Closest > Start - 25.f; ++Tick)
    {
        if (!battle::Step(World, {}, {}, Navigation))
        {
            AddError(TEXT("The meeting engagement failed to step."));
            return false;
        }
        Closest = FMath::Min(Closest, Separation());
    }
    AddInfo(FString::Printf(TEXT("Meeting engagement separation: %.1f m to %.1f m by tick %llu"), Start, Closest, World.tick));
    TestTrue(TEXT("The front closes by at least 25 m in the first 30 s"), Closest <= Start - 25.f);

    auto BlockedScenario = Scenario;
    const float MapEdge = Preset.Field.HalfHeight();
    BlockedScenario.obstacles.push_back({-1.f, -MapEdge, 1.f, MapEdge, true, battle::TerrainFeature::StoneWall});
    auto Blocked = battle::CreateWorld(BlockedScenario, 42);
    if (!TestTrue(TEXT("The same battle loads with its central passage blocked"), bool(Blocked)))
        return false;
    const auto BlockedNavigation = MakeMissionNavigation(BlockedScenario.obstacles, Preset.Field);
    for (int32 Tick = 0; Tick < 360; ++Tick)
    {
        if (!battle::Step(Blocked.value, {}, {}, BlockedNavigation))
        {
            AddError(TEXT("The blocked meeting engagement failed to step."));
            return false;
        }
    }
    const auto* Blue = battle::FindRegiment(Blocked.value, {1});
    const auto* Orange = battle::FindRegiment(Blocked.value, {MissionRegimentsPerTeam + 1});
    AddInfo(FString::Printf(TEXT("Blocked meeting engagement front X: %.1f m, %.1f m"), Blue->pose.position.x, Orange->pose.position.x));
    TestTrue(TEXT("Holding at a blocked passage does not drag the advancing regiments backwards"),
        Blue->pose.position.x >= -63.f && Orange->pose.position.x <= 63.f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentMissionScenarioTest, "RegimentBattle.MissionRoster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentMissionScenarioTest::RunTest(const FString& Parameters)
{
    auto Roster = MakeDefaultMissionRoster();
    TestEqual(TEXT("Default infantry strength includes three command figures"), Roster[0].Total(), 50);
    TestEqual(TEXT("Untrusted infantry counts respect the total cap"), FMissionRosterEntry{77, 20, 100}.Total(), 100);
    Roster[0] = {12, 4};
    Roster[1] = {8, 2};
    Roster[6] = {17, 3};
    Roster[7] = {4, 0};
    std::vector<battle::MarchRegiment> Companies;
    for (int32 Side = 0; Side < 2; ++Side)
    for (int32 Index = 0; Index < MissionRegimentsPerTeam; ++Index)
    {
        const auto Id = Side * MissionRegimentsPerTeam + Index + 1;
        const float Y = battle::MarchCrossings[Index / 2].y;
        const float X = (Side == 0 ? -1.f : 1.f) * (Index % 2 == 0 ? 9.f : 20.f);
        Companies.push_back({{uint64(Id)}, Side, {{X, Y, 0}, Side == 0 ? 0.f : battle::Pi}});
    }

    const auto Scenario = BuildMissionBattle(Roster, Companies);
    const auto Result = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("BattleCore accepts the deployed mission"), bool(Result)) ||
        !TestTrue(TEXT("Mission enables hierarchical command"), Result.value.command_state.has_value()))
        return false;
    const auto& World = Result.value;
    const auto& Hierarchy = World.command_state->hierarchy;
    const auto CheckRallyStations = [this](const battle::World& Battle)
    {
        const battle::GridNavigationService Navigation(
            Battle.scenario.obstacles, 1.f, 121, 77, {-60.f, -38.f, 0.f});
        for (const auto& Regiment : Battle.regiments)
        {
            const auto Rally = Regiment.rally_point;
            const float Rearward = Regiment.team == 0 ? -Rally.x : Rally.x;
            const int32 Index = int32(Regiment.id.value - 1) % MissionRegimentsPerTeam;
            const auto& Crossing = battle::MarchCrossings[Index / 2];
            TestTrue(TEXT("Rally station is well behind the river on the regiment's original bank"),
                Rearward > battle::MarchHalfWidth * .6f);
            TestTrue(TEXT("Rally station leaves the crossing lane"),
                FMath::Abs(Rally.y - Crossing.y) > Crossing.half_width);

            const auto Footprint = battle::Footprint(Regiment);
            const battle::Pose RallyPose{Rally, Regiment.team == 0 ? 0.f : battle::Pi};
            TestTrue(TEXT("The whole regiment fits at its rally station"),
                Navigation.CanTraverse(RallyPose, RallyPose, Footprint));
            const auto Path = Navigation.FindPath({Regiment.pose, Rally, Footprint});
            TestTrue(TEXT("The starting formation can reach its rally station"),
                Path.status == battle::Status::Ok && !Path.waypoints.empty() &&
                Path.waypoints.back().position == Rally);

            if (Index % 2 != 0)
            {
                const auto* Partner = battle::FindRegiment(Battle, {Regiment.id.value - 1});
                if (Partner)
                    TestTrue(TEXT("The two regiments have separate rally stations"),
                        FMath::Abs(Rally.y - Partner->rally_point.y) >
                            (Footprint.width_m + battle::Footprint(*Partner).width_m) * .5f);
            }
        }
    };
    CheckRallyStations(World);
    TestEqual(TEXT("Twelve campaign regiments become tactical regiments"), int32(World.regiments.size()), 12);
    TestEqual(TEXT("All fighters, six group commanders and two generals appear"), int32(World.units.size()), 470);
    TestEqual(TEXT("Twelve regiment, six group and two army nodes"), int32(Hierarchy.nodes.size()), 20);
    TestEqual(TEXT("Twenty-four support roles assigned"), int32(Hierarchy.support.size()), 24);
    using namespace battle::command;
    const auto FindNode = [&](NodeId Id)
    {
        return std::find_if(Hierarchy.nodes.begin(), Hierarchy.nodes.end(),
            [&](const auto& Node) { return Node.id == Id; });
    };
    const battle::GridNavigationService NavigationAtDeployment(
        Scenario.obstacles, 1.f, 121, 77, {-60.f, -38.f, 0.f});
    int32 Generals = 0;
    for (const auto& Node : Hierarchy.nodes)
    {
        if (Node.level != Level::Army)
            continue;
        ++Generals;
        TestFalse(TEXT("General commands at the army root"), bool(Node.parent));
        TestEqual(TEXT("General directly commands all three groups"), int32(Node.children.size()), 3);
        const auto Officer = std::find_if(World.officers.begin(), World.officers.end(),
            [&](const auto& Record) { return Record.id == Node.commander; });
        const auto* Figure = Officer == World.officers.end() ? nullptr : battle::FindUnit(World, Officer->unit);
        if (TestNotNull(TEXT("General's officer record refers to a real figure"), Figure))
        {
            TestTrue(TEXT("General is an independent, living, mounted figure"),
                !Figure->regiment && Figure->alive && Figure->troop == battle::UnitTroop::Mounted);
            TestEqual(TEXT("General belongs to the army's side"), battle::UnitTeam(World, *Figure), Node.team);
            TestTrue(TEXT("General starts on passable terrain"), NavigationAtDeployment.CanTraverse(
                Figure->pose, Figure->pose, {.75f, 1.65f}));
        }
        const auto ArmyMission = std::find_if(Scenario.command_setup->missions.begin(),
            Scenario.command_setup->missions.end(), [&](const auto& Task) { return Task.recipient == Node.id; });
        TestTrue(TEXT("Opening mission is assigned to the General"),
            ArmyMission != Scenario.command_setup->missions.end() &&
            ArmyMission->action == TaskKind::Attack && ArmyMission->status == TaskStatus::Active);
    }
    TestEqual(TEXT("One General leads each army"), Generals, 2);
    TestEqual(TEXT("Only the two armies receive opening missions"), int32(Scenario.command_setup->missions.size()), 2);
    for (int32 Index = 0; Index < MissionRegimentCount; ++Index)
    {
        const auto* Regiment = battle::FindRegiment(World, {uint64(Index + 1)});
        if (!TestNotNull(TEXT("March regiment ID survives deployment"), Regiment))
            return false;
        TestEqual(TEXT("This roster is deployed once"), int32(Regiment->units.size()),
            Roster[Index].Total());
        int32 Archers = 0, Spears = 0, Riders = 0;
        for (auto Id : Regiment->units)
        {
            const auto* Unit = battle::FindUnit(World, Id);
            Archers += Unit->archer ? 1 : 0;
            if (Unit->troop == battle::UnitTroop::Spearman)
            {
                ++Spears;
                TestTrue(TEXT("Spear uses a longer melee range"), !Unit->archer && Unit->weapon.range > 2.5f);
            }
            if (Unit->troop == battle::UnitTroop::Mounted)
            {
                ++Riders;
                TestTrue(TEXT("Cavalry has its own movement speed"), !Unit->archer && Unit->speed > 6.f);
            }
        }
        const bool bCavalry = Index % MissionRegimentsPerTeam == 5;
        TestEqual(TEXT("Archer count survives transition"), Archers, bCavalry ? 0 : Roster[Index].Archers);
        TestEqual(TEXT("Spearman count survives transition"), Spears, bCavalry ? 0 : Roster[Index].Spearmen);
        TestEqual(TEXT("Cavalry deploys in its own regiment, including its command staff"),
            Riders, bCavalry ? int32(Regiment->units.size()) : 0);
        for (const auto& Officer : World.officers)
        {
            const auto* Figure = battle::FindUnit(World, Officer.unit);
            if (Figure && Figure->regiment == Regiment->id)
                TestEqual(TEXT("The officer, standard bearer and musician travel with their regiment"),
                    Figure->troop == battle::UnitTroop::Mounted, bCavalry);
        }
        TestEqual(TEXT("Only a subset of archers is detached"), Regiment->skirmisher_count,
            uint32(FMath::Min(3, Archers / 2)));
    }
    for (const auto& Node : Hierarchy.nodes)
    {
        if (Node.level != Level::Group)
            continue;
        const auto Parent = FindNode(Node.parent);
        TestTrue(TEXT("Each group reports to its own general"), Parent != Hierarchy.nodes.end() &&
            Parent->level == Level::Army && Parent->team == Node.team &&
            std::ranges::find(Parent->children, Node.id) != Parent->children.end());
        if (!Node.children.empty())
        {
            const auto Lane = (Node.children.front().value - 1) % MissionRegimentsPerTeam / 2;
            const std::array ExpectedRoles = {SectorRole::LeftFlank, SectorRole::Center, SectorRole::RightFlank};
            TestTrue(TEXT("Group wing follows its own army's direction of advance"),
                Node.sector_role == ExpectedRoles[Node.team == 0 ? Lane : 2 - Lane]);
        }
        TestEqual(TEXT("Each group commands two regiments"), int32(Node.children.size()), 2);
        const auto Officer = std::find_if(World.officers.begin(), World.officers.end(),
            [&](const auto& Record) { return Record.id == Node.commander; });
        if (Officer == World.officers.end())
            return false;
        const auto* Figure = battle::FindUnit(World, Officer->unit);
        TestFalse(TEXT("Group commander is a separate figure"), bool(Figure->regiment));
        TestEqual(TEXT("Group commander has the correct team"), battle::UnitTeam(World, *Figure), Node.team);
        const bool bCommandsCavalry = std::ranges::any_of(Node.children, [](const auto Child)
        {
            return Child.value == 6 || Child.value == 12;
        });
        TestEqual(TEXT("A group commander with a cavalry regiment rides separately"),
            Figure->troop == battle::UnitTroop::Mounted, bCommandsCavalry);
    }
    for (const auto& Unit : World.units)
    for (const auto& Obstacle : Scenario.obstacles)
    {
        if (!battle::TerrainBlocksMovement(Obstacle))
            continue;
        const auto P = Unit.pose.position;
        TestFalse(TEXT("No figure starts in open water"),
            P.x > Obstacle.min_x && P.x < Obstacle.max_x &&
            P.y > Obstacle.min_y && P.y < Obstacle.max_y);
    }
    auto MountedOnlyRoster = Roster;
    MountedOnlyRoster[0].bCavalry = true;
    MountedOnlyRoster[0].Mounted = 15;
    MountedOnlyRoster[1].bCavalry = true;
    MountedOnlyRoster[1].Mounted = MissionMaxSpecialists;
    const auto MountedOnlyWorld = battle::CreateWorld(BuildMissionBattle(MountedOnlyRoster, Companies), 42);
    if (!TestTrue(TEXT("Mounted-only regiment deploys"), bool(MountedOnlyWorld)))
        return false;
    const auto* MountedOnly = battle::FindRegiment(MountedOnlyWorld.value, {1});
    TestEqual(TEXT("Command staff ride in a wholly mounted regiment"), int32(MountedOnly->units.size()), 18);
    TestTrue(TEXT("A wholly mounted regiment marches faster"), MountedOnly->speed > 5.f);
    const auto HasMountedUnit = [&](battle::UnitId Id)
    {
        const auto* Unit = battle::FindUnit(MountedOnlyWorld.value, Id);
        return Unit && Unit->troop == battle::UnitTroop::Mounted;
    };
    TestTrue(TEXT("No dismounted member slows the mounted regiment"),
        std::all_of(MountedOnly->units.begin(), MountedOnly->units.end(), HasMountedUnit));
    CheckRallyStations(MountedOnlyWorld.value);
    const auto* MaximumMounted = battle::FindRegiment(MountedOnlyWorld.value, {2});
    TestTrue(TEXT("Maximum mounted strength fits the river crossings"),
        battle::Footprint(*MaximumMounted).width_m < 2.f * battle::MarchCrossings.front().half_width);

    for (auto& Entry : Roster)
        Entry = {MissionMaxSwords, MissionMaxArchers};
    const auto LargeResult = battle::CreateWorld(BuildMissionBattle(Roster, Companies), 42);
    if (!TestTrue(TEXT("The maximum two-army roster deploys"), bool(LargeResult)))
        return false;
    TestEqual(TEXT("Maximum roster includes all independent commanders"), int32(LargeResult.value.units.size()), 1208);
    CheckRallyStations(LargeResult.value);
    const battle::GridNavigationService Navigation(
        LargeResult.value.scenario.obstacles, 1.f, 121, 77, {-60.f, -38.f, 0});
    for (const auto& Regiment : LargeResult.value.regiments)
    {
        const auto Footprint = battle::Footprint(Regiment);
        TestTrue(TEXT("A full-strength regiment can still cross the ford"),
            Navigation.CanTraverse({{-18, 0, 0}, 0}, {{18, 0, 0}, 0}, Footprint));
        TestTrue(TEXT("A full-strength regiment still deploys on land"),
            Navigation.CanTraverse(Regiment.pose, Regiment.pose, Footprint));
    }
    const std::array<battle::MarchRegiment, 2> ObliqueFord{{
        {{1}, 0, {{0, 0, 0}, 0}},
        {{7}, 1, {{10, .5f, 0}, battle::Pi}},
    }};
    const auto ObliqueResult = battle::CreateWorld(BuildMissionBattle(Roster, ObliqueFord), 42);
    if (!TestTrue(TEXT("Oblique contact on a crossing deploys"), bool(ObliqueResult)))
        return false;
    const auto& ForwardRegiment = ObliqueResult.value.regiments.front();
    const auto FordPath = Navigation.FindPath({ForwardRegiment.pose, {-18.f, 0.f, 0.f},
        battle::Footprint(ForwardRegiment)});
    TestTrue(TEXT("Maximum-strength regiment can exit the crossing after oblique contact"),
        FordPath.status == battle::Status::Ok);
    auto BehindEnemyLines = ObliqueFord;
    BehindEnemyLines[0].pose.position = {13.f, 0.f, 0.f};
    BehindEnemyLines[1].pose.position = {-13.f, 0.f, 0.f};
    const auto CrossedResult = battle::CreateWorld(BuildMissionBattle(FMissionRoster{}, BehindEnemyLines), 42);
    if (!TestTrue(TEXT("Regiments can start tactical play on the opposing bank"), bool(CrossedResult)))
        return false;
    CheckRallyStations(CrossedResult.value);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAuthoredMissionTest, "RegimentBattle.AuthoredMission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAuthoredMissionTest::RunTest(const FString& Parameters)
{
    auto Roster = MakeDefaultMissionRoster();
    for (auto& Entry : Roster)
        Entry.bEnabled = false;
    constexpr int32 BlueIndex = 1;
    constexpr int32 OrangeIndex = MissionRegimentCount - 1;
    Roster[BlueIndex].bEnabled = true;
    Roster[BlueIndex].Group = 3;
    Roster[BlueIndex].StartX = -42.5f;
    Roster[BlueIndex].StartY = -18.25f;
    Roster[BlueIndex].Swords = 7;
    Roster[BlueIndex].Archers = 5;
    Roster[BlueIndex].Spearmen = 4;
    Roster[OrangeIndex].bEnabled = true;
    Roster[OrangeIndex].Group = 1;
    Roster[OrangeIndex].StartX = 43.25f;
    Roster[OrangeIndex].StartY = 19.5f;
    Roster[OrangeIndex].Mounted = 13;

    constexpr auto Terrain = battle::MarchTerrain::OpenField;
    const auto Opening = MakeMissionOpening(Roster, Terrain);
    if (!TestEqual(TEXT("March contains only the enabled slots"), int32(Opening.size()), 2))
        return false;
    TestEqual(TEXT("Blue keeps its stable slot number"), Opening.front().id.value, uint64(BlueIndex + 1));
    TestEqual(TEXT("Orange keeps its stable slot number"), Opening.back().id.value, uint64(OrangeIndex + 1));
    const battle::Vec3 BlueStart{-42.5f, -18.25f, 0.f};
    const battle::Vec3 OrangeStart{43.25f, 19.5f, 0.f};
    TestTrue(TEXT("March starts at the authored positions"),
        Opening.front().pose.position == BlueStart && Opening.back().pose.position == OrangeStart);

    battle::MarchMission March(Opening, Terrain);
    for (int32 Tick = 0; Tick < 450 && !March.EnemyFound(); ++Tick)
        March.Step(.05f);
    if (!TestTrue(TEXT("Sparse armies in different original lanes can discover each other"), March.EnemyFound()))
        return false;
    TestFalse(TEXT("A disabled blue slot cannot receive a march flag"),
        March.SetWaypoint({1}, March.Regiments().back().pose.position));
    if (!TestTrue(TEXT("The enabled blue slot can receive a march flag"),
        March.SetWaypoint(Opening.front().id, March.Regiments().back().pose.position)))
        return false;
    for (int32 Tick = 0; Tick < 150 && !March.Encounter(); ++Tick)
        March.Step(.05f);
    if (!TestTrue(TEXT("The sparse march can enter tactical contact"), bool(March.Encounter())))
        return false;
    const auto Contact = battle::CreateWorld(BuildMissionBattle(Roster, March.Regiments(), Terrain), 42);
    if (!TestTrue(TEXT("BattleCore accepts the sparse encounter"), bool(Contact)))
        return false;
    TestEqual(TEXT("Both active regiments survive the march handoff"), int32(Contact.value.regiments.size()), 2);

    const auto Scenario = BuildMissionBattle(Roster, Opening, Terrain, EMissionDeployment::AuthoredStart);
    const auto Result = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("BattleCore accepts direct deployment"), bool(Result)) ||
        !TestTrue(TEXT("The authored mission has hierarchical command"), Result.value.command_state.has_value()))
        return false;
    const auto& World = Result.value;
    const auto* Blue = battle::FindRegiment(World, {uint64(BlueIndex + 1)});
    const auto* Orange = battle::FindRegiment(World, {uint64(OrangeIndex + 1)});
    if (!TestNotNull(TEXT("Blue regiment deployed"), Blue) || !TestNotNull(TEXT("Orange regiment deployed"), Orange))
        return false;
    TestTrue(TEXT("Direct deployment retains passable fractional starts"),
        Blue->pose.position == BlueStart && Orange->pose.position == OrangeStart);
    TestEqual(TEXT("Authored infantry and support strength reaches the battlefield"), int32(Blue->units.size()), 19);
    TestEqual(TEXT("Authored mounted and support strength reaches the battlefield"), int32(Orange->units.size()), 16);
    TestEqual(TEXT("Only occupied groups and their generals deploy"), int32(World.units.size()), 39);
    const auto& Nodes = World.command_state->hierarchy.nodes;
    TestEqual(TEXT("Two regiments, two nonempty groups and two armies"), int32(Nodes.size()), 6);
    const auto ChildOfGroup = [&](int32 Index, int32 Group)
    {
        const auto Id = battle::command::NodeId{uint64(Index + 1)};
        const auto Node = std::ranges::find(Nodes, Id, &battle::command::CommandNode::id);
        const int32 Team = Index / MissionRegimentsPerTeam;
        return Node != Nodes.end() &&
            Node->parent.value == uint64(MissionRegimentCount + Team * MissionGroupsPerTeam + Group);
    };
    TestTrue(TEXT("The blue regiment reports to its authored group"), ChildOfGroup(BlueIndex, 3));
    TestTrue(TEXT("The orange regiment reports to its authored group"), ChildOfGroup(OrangeIndex, 1));

    for (int32 Index = 0; Index < MissionRegimentsPerTeam; ++Index)
    {
        Roster[Index].bEnabled = true;
        Roster[Index].Group = 2;
    }
    const auto ConcentratedOpening = MakeMissionOpening(Roster, Terrain);
    const auto Concentrated = battle::CreateWorld(
        BuildMissionBattle(Roster, ConcentratedOpening, Terrain, EMissionDeployment::AuthoredStart), 42);
    if (!TestTrue(TEXT("A commander accepts six regiments in one group"), bool(Concentrated)) ||
        !TestTrue(TEXT("Concentrated deployment has hierarchical command"), Concentrated.value.command_state.has_value()))
        return false;
    const auto& ConcentratedNodes = Concentrated.value.command_state->hierarchy.nodes;
    const auto Center = std::ranges::find(ConcentratedNodes, battle::command::NodeId{MissionRegimentCount + 2},
        &battle::command::CommandNode::id);
    if (TestTrue(TEXT("The blue center is present"), Center != ConcentratedNodes.end()))
        TestEqual(TEXT("All six regiments report to the same commander"), int32(Center->children.size()), 6);
    return true;
}
#endif
