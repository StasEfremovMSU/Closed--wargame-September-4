#include "battle/Formation.h"
#include "battle/MarchMission.h"
#include "TestSupport.h"
#include "doctest.h"
#include <limits>

using namespace battle;

TEST_CASE("formation.marching_files_pass_friendly_independent_staff_and_rejoin") {
    auto scenario = test::Duel(20);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].formation = {4, 1.1f, .25f};
    scenario.regiments[1].formation = {4, 1.1f, .25f};
    scenario.regiments[1].pose.position = {29.f, -10.f, 0.f};
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.speed = 3.8f;
    }
    UnitSpec general;
    general.id = {100};
    general.independent_team = 0;
    general.pose.position = {0.f, .55f, 0.f};
    general.health = 100.f;
    bool walled = false;
    bool hostile = false;
    SUBCASE("five marching ranks share a lane with a friendly officer") {}
    SUBCASE("the collision broad phase still lets a rank bypass an officer") {
        scenario.rules.adaptive_battle = true;
    }
    SUBCASE("a road never lets the side-step exceed the collision sweep") {
        scenario.obstacles.push_back({-21.f, -4.f, 21.f, 4.f, false, TerrainFeature::Road});
    }
    SUBCASE("a wall forces the regiment to use the clear side") {
        walled = true;
        general.pose.position.y = 1.65f;
        scenario.obstacles.push_back({-2.f, 2.04f, 2.f, 4.f, true, TerrainFeature::StoneWall});
    }
    SUBCASE("a hostile figure still blocks its file") {
        hostile = true;
        general.independent_team = 1;
        general.health = 100000.f;
        general.weapon.damage = 0.f;
    }
    scenario.units.push_back(general);
    scenario.officers.push_back({{1}, general.id, 1});
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    const GridNavigationService navigation(scenario.obstacles, 1.f, 81, 61, {-40, -30, 0});
    Order march;
    march.recipient = {1};
    march.type = OrderType::MoveTo;
    march.point = {10.f, 0.f, 0.f};
    march.facing = 0.f;
    march.player_direct = true;
    REQUIRE(SubmitOrder(world, march) == Status::Ok);
    const auto &regiment = world.regiments[0];
    float lowest_clearance = std::numeric_limits<float>::max();
    float lowest_rank_clearance = std::numeric_limits<float>::max();
    float outermost_wall_step = 0.f;
    for (int tick = 0; tick < 420; ++tick) {
        REQUIRE(Step(world, {}, {}, navigation));
        const auto &officer = *FindUnit(world, general.id);
        CHECK(officer.pose.position == general.pose.position);
        for (const auto id : regiment.units) {
            const auto &unit = *FindUnit(world, id);
            lowest_clearance = std::min(lowest_clearance, Distance(unit.pose.position, officer.pose.position));
            for (const auto other : regiment.units)
                if (other > id)
                    lowest_rank_clearance = std::min(lowest_rank_clearance,
                        Distance(unit.pose.position, FindUnit(world, other)->pose.position));
            if (walled && std::abs(unit.pose.position.x) < 1.7f)
                outermost_wall_step = std::max(outermost_wall_step, unit.pose.position.y);
        }
    }
    REQUIRE(regiment.units.size() == 20);
    CHECK(regiment.pose.position == march.point);
    CHECK(lowest_clearance >= .499f);
    CHECK(lowest_rank_clearance >= .499f);
    if (walled)
        CHECK(outermost_wall_step <= 1.7901f);
    const auto assembled = std::ranges::count_if(regiment.units, [&](const auto id) {
        const auto &unit = *FindUnit(world, id);
        return Distance(unit.pose.position, unit.slot.position) < .15f;
    });
    CHECK(assembled == (hostile ? 15 : 20));
}

TEST_CASE("formation.hill_slows_a_formation_climbing_but_not_descending") {
    auto scenario = test::Duel();
    scenario.regiments.resize(1);
    scenario.units.resize(1);
    scenario.regiments.front().pose.position = {-20, 0, 0};
    scenario.units.front().pose = scenario.regiments.front().pose;
    for (float goal : {-11.f, -27.f}) {
        auto flat = CreateWorld(scenario, 2);
        auto variable = scenario;
        variable.hill = TerrainHill{};
        auto hill = CreateWorld(variable, 2);
        REQUIRE(flat);
        REQUIRE(hill);
        for (auto *world : {&flat.value, &hill.value}) {
            auto &regiment = world->regiments.front();
            regiment.pose.yaw = goal > -20.f ? 0.f : Pi;
            regiment.route = {{{goal, 0, 0}, regiment.pose.yaw}};
            AdvanceFormations(*world, .05f);
        }
        if (goal > -20.f)
            CHECK(hill.value.regiments.front().pose.position.x < flat.value.regiments.front().pose.position.x);
        else
            CHECK(hill.value.regiments.front().pose == flat.value.regiments.front().pose);
        CHECK(hill.value.regiments.front().pose.position.z == 0.f);
    }
}

TEST_CASE("formation.hill_slows_a_soldier_refilling_an_uphill_slot") {
    auto scenario = test::Duel();
    scenario.regiments.resize(1);
    scenario.units.resize(1);
    scenario.regiments.front().pose.position.x = -15.f;
    scenario.units.front().pose.position.x = -20.f;
    auto flat = CreateWorld(scenario, 2);
    REQUIRE(flat);
    scenario.hill = TerrainHill{};
    auto hill = CreateWorld(scenario, 2);
    REQUIRE(hill);

    for (auto *world : {&flat.value, &hill.value}) {
        world->units.front().control = ControlMode::Returning;
        AdvanceFormations(*world, .05f);
        CHECK(world->units.front().control == ControlMode::Returning);
        CHECK(world->units.front().pose.position.x > -20.f);
    }
    CHECK(hill.value.units.front().pose.position.x < flat.value.units.front().pose.position.x);
    CHECK(hill.value.units.front().pose.position.z == 0.f);
}

TEST_CASE("integration.travel_columns_trade_readiness_for_speed") {
    MarchRegiment infantry{{1}, 0, {{-45, -10, 0}, 0}};
    auto horse = infantry;
    horse.id = {2};
    horse.troop = UnitTroop::Mounted;
    auto line = infantry;
    line.id = {3};
    line.posture = MarchPosture::BattleLine;
    MarchMission march({infantry, horse, line}, MarchTerrain::OpenField, true);

    march.Step(1.f);

    const auto &columns = march.Regiments();
    CHECK(columns[1].pose.position.x > columns[0].pose.position.x);
    CHECK(columns[0].pose.position.x > columns[2].pose.position.x);
    CHECK(columns[0].ActiveFormation().columns < columns[2].ActiveFormation().columns);
    CHECK(columns[0].DeploymentSeconds() > 0.f);
    CHECK(columns[2].DeploymentSeconds() == 0.f);
    CHECK(columns[0].fatigue > 0.f);
}

TEST_CASE("integration.only_column_fits_a_narrow_crossing") {
    MarchRegiment line{{1}, 0, {{-20, 0, 0}, 0}};
    line.posture = MarchPosture::BattleLine;
    MarchMission march({line}, MarchTerrain::StirlingBridge, true);
    const Vec3 far_bank{20, 0, 0};

    CHECK_FALSE(march.SetWaypoint(line.id, far_bank));
    REQUIRE(march.SetPosture(line.id, MarchPosture::TravelColumn));
    REQUIRE(march.SetWaypoint(line.id, far_bank));
    march.Step(10.f);
    CHECK(march.Regiments().front().pose.position == far_bank);
}

TEST_CASE("integration.march_start_checks_column_clearance_at_crossing_edge") {
    MarchRegiment regiment{{1}, 0, {{-1, 3.5f, 0}, 0}};
    const auto terrain = MarchTerrain::River;
    CHECK(MarchStartWalkable(regiment, terrain, false));
    CHECK_FALSE(MarchStartWalkable(regiment, terrain, true));
    const MarchMission stuck({regiment}, terrain, true);
    CHECK_FALSE(stuck.Regiments().front().waypoint.has_value());

    regiment.pose.position.y = 0.f;
    CHECK(MarchStartWalkable(regiment, terrain, true));
    MarchMission centered({regiment}, terrain, true);
    CHECK(centered.SetWaypoint(regiment.id, {12, 0, 0}));
}

TEST_CASE("integration.deployment_can_finish_at_a_halt_after_sighting") {
    MarchRegiment blue{{1}, 0, {{-20, 0, 0}, 0}, Vec3{-20, 0, 0}};
    MarchRegiment red{{2}, 1, {{0, 0, 0}, Pi}, Vec3{0, 0, 0}};
    MarchMission march({blue, red}, MarchTerrain::OpenField, true);

    march.Step(.05f);
    REQUIRE(march.EnemyFound());
    REQUIRE(march.SetPosture(blue.id, MarchPosture::BattleLine));
    const float deployment = march.Regiments().front().DeploymentSeconds();
    REQUIRE(deployment > .05f);
    march.Step(.05f);
    CHECK(march.Regiments().front().ActiveFormation().columns < blue.formation.columns);
    march.Step(deployment);
    CHECK(march.Regiments().front().pose == blue.pose);
    CHECK(march.Regiments().front().ActiveFormation() == blue.formation);
}

TEST_CASE("formation.travel_deployment_starts_in_column_and_holds_the_regiment") {
    auto scenario = test::Duel(4);
    scenario.rules.regiment_tactics = true;
    scenario.regiments.resize(1);
    scenario.units.resize(4);
    auto &spec = scenario.regiments.front();
    spec.formation.columns = 4;
    spec.deployment_ticks = 10;
    for (auto &unit : scenario.units)
        unit.pose = spec.pose;
    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    const Vec3 ahead = regiment.pose.position + Forward(regiment.pose.yaw) * 20.f;
    regiment.route = {{ahead, regiment.pose.yaw}};

    CHECK(Footprint(regiment).width_m < 4.f);
    CHECK(FindUnit(world, regiment.units.front())->pose == regiment.slots.front());
    CHECK(FindUnit(world, regiment.units.front())->pose.position.x > spec.pose.position.x);
    for (; world.tick < spec.deployment_ticks; ++world.tick)
        AdvanceFormations(world, .05f);
    CHECK(regiment.pose == spec.pose);

    AdvanceFormations(world, .05f);
    CHECK(regiment.formation == spec.formation);
    CHECK(regiment.pose.position != spec.pose.position);
}

TEST_CASE("formation.disorder_delays_manoeuvre_until_the_line_recovers") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.regiments.resize(1);
    scenario.units.resize(1);
    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    auto orderly = created.value;
    auto disordered = orderly;
    const auto start = orderly.regiments.front().pose;
    const Pose goal{start.position + Forward(start.yaw) * 20.f, start.yaw};
    orderly.regiments.front().route = {goal};
    disordered.regiments.front().route = {goal};
    disordered.regiments.front().disordered = true;

    AdvanceFormations(orderly, .05f);
    AdvanceFormations(disordered, .05f);
    const auto progress = Distance(disordered.regiments.front().pose.position, start.position);
    CHECK(progress > 0.f);
    CHECK(progress < Distance(orderly.regiments.front().pose.position, start.position));
}

TEST_CASE("formation.march_fatigue_costs_speed_and_recovers_at_a_halt") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.regiments.resize(1);
    scenario.units.resize(1);
    auto fresh = CreateWorld(scenario, 1);
    REQUIRE(fresh);
    scenario.regiments.front().initial_fatigue = .7f;
    auto tired = CreateWorld(scenario, 1);
    REQUIRE(tired);
    auto &regiment = tired.value.regiments.front();
    CHECK(regiment.fatigue == doctest::Approx(.7));

    const auto start = regiment.pose;
    const Pose goal{start.position + Forward(start.yaw) * 20.f, start.yaw};
    regiment.route = {goal};
    fresh.value.regiments.front().route = {goal};
    AdvanceFormations(tired.value, .05f);
    AdvanceFormations(fresh.value, .05f);
    const auto rested_progress = Distance(fresh.value.regiments.front().pose.position, start.position);
    CHECK(Distance(regiment.pose.position, start.position) < rested_progress);
    CHECK(regiment.fatigue == doctest::Approx(.7));

    regiment.route.clear();
    for (int tick = 0; tick < 20; ++tick)
        AdvanceFormations(tired.value, .05f);
    CHECK(regiment.fatigue < .7f);
}
