#include "battle/MarchMission.h"
#include "battle/Navigation.h"
#include "battle/Formation.h"
#include "TestSupport.h"
#include "doctest.h"

TEST_CASE("terrain.borodino_redoubt_slows_a_frontal_assault_and_protects_its_interior") {
    using namespace battle;
    const auto areas = MarchTerrainAreas(MarchTerrain::Borodino, {580, 380});
    const Vec3 front{-18.f, 0, 0};
    const Vec3 interior{5.f, 0, 0};
    const Vec3 rear{30.f, 0, 0};
    CHECK(TerrainSpeedMultiplier(front, interior, areas, UnitTroop::Infantry, 3.f) < .5f);
    CHECK(TerrainSpeedMultiplier(rear, interior, areas, UnitTroop::Infantry, 3.f) == 1.f);
    CHECK(TerrainForbidsCharge(front, interior, areas));
    CHECK_FALSE(TerrainForbidsCharge(rear, interior, areas));
    CHECK_FALSE(EvaluateTerrainShot(front, interior, areas).blocked);
    CHECK(EvaluateTerrainShot(front, interior, areas).shooting_modifier < 0);
    CHECK(EvaluateTerrainShot(interior, front, areas).shooting_modifier == 0);
    const auto emplacement = std::ranges::find_if(areas, [](const Obstacle &area) {
        return area.feature == TerrainFeature::RedoubtInterior && area.min_y < 0.f && area.max_y > 0.f;
    });
    REQUIRE(emplacement != areas.end());
    CHECK_FALSE(TerrainBlocksMovement(*emplacement));
    const auto station = TerrainCoverPosition(*emplacement, {interior, 0.f}, 2.f, 14.f);
    REQUIRE(station);
    CHECK(station->position.x > emplacement->min_x + 2.f);
    CHECK(station->yaw == doctest::Approx(Pi));
    CHECK_FALSE(TerrainCoverPosition(*emplacement, {interior, 0.f}, 2.f, 40.f));
}

TEST_CASE("march.artillery_stays_in_line") {
    using namespace battle;
    MarchRegiment reg;
    reg.id = {1};
    reg.pose.position = {-40.f, 0, 0};
    reg.formation = {5, 3.8f, 1.7f};
    reg.figure_count = 5;
    reg.fixed_frontage = true;
    MarchMission march({reg}, MarchTerrain::OpenField, true);
    CHECK(march.Regiments().front().posture == MarchPosture::BattleLine);
    CHECK(march.Regiments().front().ActiveFormation().columns == 5);
    CHECK(march.Regiments().front().DeploymentSeconds() == 0.f);
    CHECK_FALSE(march.SetPosture({1}, MarchPosture::TravelColumn));
}

namespace battle {
namespace {
std::vector<MarchRegiment> OpposingRegiments(float y) {
    return {{{1}, 0, {{-39, y, 0}, 0}, Vec3{-17.f, y, 0}, {}, 0},
            {{2}, 1, {{39, y, 0}, Pi}, Vec3{-17.f, y, 0}, {}, 0}};
}

void StepUntilSighting(MarchMission &mission) {
    for (int frame = 0; frame < 300 && !mission.EnemyFound(); ++frame)
        mission.Step(.05f);
}
} // namespace

TEST_CASE("integration.hill_plateau_uses_elliptical_fixed_metric_geometry") {
    const auto hill = MarchHill(MarchTerrain::Hill, {240, 160});
    REQUIRE(hill);
    CHECK_FALSE(MarchHill(MarchTerrain::OpenField));
    CHECK(HillHeight(*hill, {0.f, 0.f, 0.f}) == doctest::Approx(4.f));
    CHECK(HillHeight(*hill, {36.f, 0.f, 0.f}) == 0.f);
    CHECK(HillHeight(*hill, {0.f, 28.f, 0.f}) == 0.f);
    CHECK(OnHillCrest(*hill, {12.f, 0.f, 0.f}));
    CHECK_FALSE(OnHillCrest(*hill, {0.f, 11.f, 0.f}));
    CHECK(HillHeight(*hill, {18.f, 0.f, 0.f}) > 0.f);
    CHECK(HillHeight(*hill, {18.f, 0.f, 0.f}) < 4.f);
    CHECK(HillHeight(*hill, {-18.f, 0.f, 20.f}) == HillHeight(*hill, {18.f, 0.f, 0.f}));
    // The standard contour rises less than 0.34 m per horizontal metre.
    for (int y = 0; y < 28; ++y)
        CHECK(HillHeight(*hill, {0.f, float(y), 0.f}) -
              HillHeight(*hill, {0.f, float(y + 1), 0.f}) < .34f);
    CHECK(MarchObstacles(MarchTerrain::Hill).empty());
}

TEST_CASE("integration.hill_hides_opponents_until_marching_to_a_view") {
    const std::vector<MarchRegiment> hidden = {
        {{1}, 0, {{0, -24, 0}, Pi / 2}, Vec3{0, -24, 0}},
        {{2}, 1, {{0, 4, 0}, -Pi / 2}, Vec3{0, 4, 0}}
    };
    MarchMission flat(hidden, MarchTerrain::OpenField);
    MarchMission hill(hidden, MarchTerrain::Hill);
    flat.Step(.05f);
    hill.Step(.05f);
    CHECK(flat.EnemyFound());
    REQUIRE_FALSE(hill.EnemyFound());
    REQUIRE(hill.SetWaypoint({1}, {0, -6, 0}));
    StepUntilSighting(hill);
    CHECK(hill.EnemyFound());
    CHECK(hill.Regiments().front().pose.position.z == 0);
}

TEST_CASE("integration.march_open_field_uses_direct_routes_and_reaches_contact") {
    CHECK(MarchObstacles(MarchTerrain::OpenField).empty());
    auto regiments = OpposingRegiments(12.f);
    MarchMission mission(std::move(regiments), MarchTerrain::OpenField);
    CHECK(mission.Regiments().back().route.size() == 1);
    StepUntilSighting(mission);
    REQUIRE(mission.EnemyFound());
    CHECK_FALSE(mission.Encounter());
    REQUIRE(mission.SetWaypoint({1}, {23, 12, 0}));
    for (int frame = 0; frame < 300 && !mission.Encounter(); ++frame)
        mission.Step(.05f);
    REQUIRE(mission.Encounter());
    CHECK(mission.Encounter()->center.y == doctest::Approx(12.f));
}

TEST_CASE("integration.march_stone_walls_use_openings_and_reject_wall_waypoints") {
    const auto walls = MarchObstacles(MarchTerrain::StoneWalls);
    REQUIRE(walls.size() >= 3);
    for (const auto &wall : walls)
        CHECK(wall.blocks_projectiles);
    for (const auto &water : MarchObstacles(MarchTerrain::River))
        CHECK_FALSE(water.blocks_projectiles);
    MarchMission mission({{{1}, 0, {{-42, 12, 0}, 0}, {}, {}, 0}}, MarchTerrain::StoneWalls);
    const auto previous = mission.Regiments().front().waypoint;
    const auto &wall = walls[1];
    CHECK_FALSE(mission.SetWaypoint({1}, {(wall.min_x + wall.max_x) * .5f,
                                          (wall.min_y + wall.max_y) * .5f, 0}));
    CHECK(mission.Regiments().front().waypoint == previous);
    const Vec3 goal{42, 12, 0};
    REQUIRE(mission.SetWaypoint({1}, goal));
    REQUIRE(mission.Regiments().front().route.size() > 1);
    for (int frame = 0; frame < 600; ++frame) {
        mission.Step(.05f);
        const Vec3 p = mission.Regiments().front().pose.position;
        for (const auto &box : walls)
            CHECK((p.x < box.min_x || p.x > box.max_x ||
                   p.y < box.min_y || p.y > box.max_y));
    }
    CHECK(mission.Regiments().front().pose.position == goal);

    GridNavigationService navigation(walls, 1.f, 121, 77, {-60.f, -38.f, 0});
    CHECK_FALSE(navigation.CanTraverse({{-8, 12, 0}, 0}, {{8, 12, 0}, 0}, {7, 6}));
    for (float y : {-24.f, 0.f, 24.f})
        CHECK(navigation.CanTraverse({{-8, y, 0}, 0}, {{8, y, 0}, 0}, {7, 6}));
}

TEST_CASE("integration.march_stone_walls_occlude_contact_and_openings_allow_battle") {
    auto hidden = OpposingRegiments(12.f);
    hidden[0].pose.position.x = -5.f;
    hidden[1].pose.position.x = 5.f;
    for (auto &regiment : hidden)
        regiment.waypoint = regiment.pose.position;
    MarchMission behind_wall(std::move(hidden), MarchTerrain::StoneWalls);
    behind_wall.Step(.1f);
    CHECK_FALSE(behind_wall.EnemyFound());
    CHECK_FALSE(behind_wall.Encounter());

    MarchMission through_gate(OpposingRegiments(-24.f), MarchTerrain::StoneWalls);
    StepUntilSighting(through_gate);
    REQUIRE(through_gate.EnemyFound());
    REQUIRE(through_gate.SetWaypoint({1}, {23, -24, 0}));
    for (int frame = 0; frame < 300 && !through_gate.Encounter(); ++frame)
        through_gate.Step(.05f);
    REQUIRE(through_gate.Encounter());
    CHECK(through_gate.Encounter()->center.y == doctest::Approx(-24.f));
}

TEST_CASE("integration.stirling_has_a_single_passable_bridge") {
    const auto terrain = MarchTerrain::StirlingBridge;
    const auto crossings = MarchCrossingsFor(terrain);
    REQUIRE(crossings.size() == 1);
    CHECK(crossings.front().y == 0.f);
    const auto water = MarchObstacles(terrain);
    const FormationFootprint formation{3.f, 4.f};
    const GridNavigationService navigation(water, 1.f, 121, 77, {-60.f, -38.f, 0});
    CHECK(navigation.CanTraverse({{-8, 0, 0}, 0}, {{8, 0, 0}, 0}, formation));
    for (float y : {-24.f, 24.f})
        CHECK_FALSE(navigation.CanTraverse({{-8, y, 0}, 0}, {{8, y, 0}, 0}, formation));
    MarchMission march({{{1}, 0, {{-38, -24, 0}, 0}}}, terrain);
    CHECK_FALSE(march.SetWaypoint({1}, {0, -24, 0}));
    REQUIRE(march.SetWaypoint({1}, {22, -24, 0}));
    bool entered_bridge = false;
    for (int frame = 0; frame < 500; ++frame) {
        march.Step(.05f);
        const auto position = march.Regiments().front().pose.position;
        if (std::abs(position.x) < MarchRiverHalfWidth) {
            entered_bridge = true;
            CHECK(std::abs(position.y) < crossings.front().half_width);
        }
    }
    CHECK(entered_bridge);
    CHECK(march.Regiments().front().pose.position.x == doctest::Approx(22.f));
}

TEST_CASE("integration.march_wide_field_keeps_outer_positions_and_waypoints") {
    const MarchField field{240, 160};
    const auto terrain = MarchTerrain::OpenField;
    const MarchRegiment regiment{{1}, 0, {{-88, 57, 0}, 0}};
    CHECK_FALSE(MarchStartWalkable(regiment, terrain, true));
    REQUIRE(MarchStartWalkable(regiment, terrain, true, field));
    CHECK_FALSE(MarchMission({regiment}, terrain, true).SetWaypoint({1}, {90, 70, 0}));

    MarchMission march({regiment}, terrain, true, field);
    CHECK_FALSE(march.SetWaypoint({1}, {121, 70, 0}));
    CHECK_FALSE(march.SetWaypoint({1}, {90, 81, 0}));
    const Vec3 goal{90, 70, 0};
    REQUIRE(march.SetWaypoint({1}, goal));
    for (int frame = 0; frame < 900; ++frame)
        march.Step(.05f);
    CHECK(march.Regiments().front().pose.position == goal);
}

TEST_CASE("integration.march_variable_river_bands_preserve_crossing_geometry") {
    const auto terrain = MarchTerrain::River;
    const auto standard = MarchObstacles(terrain);
    const auto wider = MarchObstacles(terrain, {240, 160});
    REQUIRE(standard.size() == 4);
    REQUIRE(wider.size() == standard.size());
    CHECK(standard.front().min_y == -38.f);
    CHECK(standard.back().max_y == 38.f);
    CHECK(wider.front().min_y == -80.f);
    CHECK(wider.back().max_y == 80.f);
    for (std::size_t i = 0; i < wider.size(); ++i) {
        CHECK(wider[i].min_x == -5.f);
        CHECK(wider[i].max_x == 5.f);
        if (i > 0)
            CHECK(wider[i].min_y == standard[i].min_y);
        if (i + 1 < wider.size())
            CHECK(wider[i].max_y == standard[i].max_y);
    }
    MarchMission march({{{1}, 0, {{-90, 56, 0}, 0}}}, terrain, true, {240, 160});
    CHECK_FALSE(march.SetWaypoint({1}, {0, 56, 0}));
    REQUIRE(march.SetWaypoint({1}, {90, 56, 0}));
    CHECK(march.Regiments().front().route.size() > 1);
}

TEST_CASE("integration.march_small_fields_clip_bands_and_routes") {
    const auto terrain = MarchTerrain::River;
    const auto narrow = MarchObstacles(terrain, {4, 36});
    REQUIRE(narrow.size() == 2);
    CHECK(narrow.front().min_x == -2.f);
    CHECK(narrow.front().max_x == 2.f);
    CHECK(narrow.front().min_y == -18.f);
    CHECK(narrow.front().max_y == -4.f);
    CHECK(narrow.back().min_y == 4.f);
    CHECK(narrow.back().max_y == 18.f);
    CHECK(MarchObstacles(terrain, {0, 36}).empty());

    const MarchField short_field{40, 38};
    MarchMission march({{{1}, 0, {{-20, 19, 0}, 0}}}, terrain, false, short_field);
    REQUIRE(march.SetWaypoint({1}, {20, 19, 0}));
    for (const auto &part : march.Regiments().front().route)
        CHECK(std::abs(part.position.y) <= short_field.HalfHeight());
}
TEST_CASE("integration.march_preview_matches_the_route_and_leaves_orders_intact") {
    MarchMission march({{{1}, 0, {{-35, -15, 0}, 0}}}, MarchTerrain::StirlingBridge);
    const auto original = march.Regiments().front().waypoint;
    const Vec3 goal{30, -12, 0};
    const auto route = march.PreviewWaypoint({1}, goal);
    REQUIRE(route);
    CHECK(route->size() > 1);
    CHECK(march.Regiments().front().waypoint == original);
    CHECK_FALSE(march.PreviewWaypoint({1}, {0, 15, 0}));
    REQUIRE(march.SetWaypoint({1}, goal));
    CHECK(march.Regiments().front().route == *route);
}

TEST_CASE("integration.march_bridge_queues_regiments_and_releases_the_follower") {
    MarchRegiment first{{11}, 0, {{-11, 0, 0}, 0}, Vec3{16, 0, 0}};
    MarchRegiment second{{4}, 0, {{-19, 0, 0}, 0}, Vec3{27, 0, 0}};
    MarchRegiment third{{1}, 0, {{-28, 0, 0}, 0}, Vec3{38, 0, 0}};
    first.formation = second.formation = third.formation = {4, 1, .4f};
    MarchMission march({second, first, third}, MarchTerrain::StirlingBridge, true);
    bool first_crossed = false;
    for (int frame = 0; frame < 450; ++frame) {
        march.Step(.05f);
        const auto follower_x = march.Regiments()[0].pose.position.x;
        const auto leader_x = march.Regiments()[1].pose.position.x;
        const auto tail_x = march.Regiments()[2].pose.position.x;
        first_crossed = first_crossed || leader_x > MarchRiverHalfWidth;
        if (std::abs(follower_x) < MarchRiverHalfWidth) {
            CHECK(first_crossed);
            CHECK(leader_x > MarchRiverHalfWidth);
        }
        if (leader_x < MarchRiverHalfWidth)
            CHECK(follower_x < leader_x);
        if (follower_x < -MarchRiverHalfWidth)
            CHECK(follower_x - tail_x > 1.f);
    }
    CHECK(march.Regiments()[0].pose.position.x == 27);
    CHECK(march.Regiments()[1].pose.position.x == 16);
    CHECK(march.Regiments()[2].pose.position.x == 38);
}

TEST_CASE("integration.terrain_cover_protects_only_with_the_barrier_between_shot_and_target") {
    const std::vector<Obstacle> stone{{-.75f, -10.f, .75f, 10.f, true,
                                        TerrainFeature::StoneWall}};
    const auto cover = TerrainCoverPosition(stone.front(), {{-4, 3, 0}, 0}, 1.f, 2.f);
    REQUIRE(cover);
    CHECK(cover->position.x < -1.75f);
    CHECK(cover->position.y == 3.f);
    CHECK(Forward(cover->yaw).x == doctest::Approx(1.f));
    CHECK_FALSE(TerrainAreaOverlaps(stone.front(), *cover, 1.f, 2.f));
    CHECK_FALSE(TerrainCoverPosition(stone.front(), *cover, 1.f, 11.f));

    const auto protected_shot = EvaluateTerrainShot({8, 3, 0}, cover->position, stone);
    CHECK_FALSE(protected_shot.blocked);
    CHECK(protected_shot.shooting_modifier == -3);
    CHECK(EvaluateTerrainShot({-8, 3, 0}, cover->position, stone).shooting_modifier == 0);
    CHECK(EvaluateTerrainShot({-8, 3, 0}, {8, 3, 0}, stone).blocked);
    CHECK_FALSE(EvaluateTerrainShot(cover->position, {8, 3, 0}, stone).blocked);
}

TEST_CASE("integration.bridge_and_ford_are_walkable_terrain_at_half_speed") {
    const auto areas = MarchTerrainAreas(MarchTerrain::River);
    const GridNavigationService navigation(areas, 1, 121, 77, {-60, -38, 0});
    for (float y : {-24.f, 0.f, 24.f}) {
        CHECK(navigation.CanTraverse({{-8, y, 0}, 0}, {{8, y, 0}, 0}, {3, 4}));
        CHECK(TerrainSpeedMultiplier({-2, y, 0}, {2, y, 0}, areas, UnitTroop::Infantry, 4.f) == .5f);
    }
    CHECK_FALSE(navigation.CanTraverse({{-8, 10, 0}, 0}, {{8, 10, 0}, 0}, {3, 4}));
    CHECK(TerrainSpeedMultiplier({-8, 10, 0}, {-6, 10, 0}, areas, UnitTroop::Mounted, 4.f) == 1.f);
    CHECK(TerrainForbidsCharge({-8, 0, 0}, {8, 0, 0}, areas));
    CHECK_FALSE(TerrainForbidsCharge({-14, 0, 0}, {-8, 0, 0}, areas));
}

TEST_CASE("integration.road_has_a_larger_movement_bonus_for_cavalry") {
    const auto areas = MarchTerrainAreas(MarchTerrain::StirlingBridge);
    const Vec3 start{-10, 0, 0}, end{-7, 0, 0};
    CHECK(TerrainSpeedMultiplier(start, end, areas, UnitTroop::Infantry, 4.f) == 1.25f);
    CHECK(TerrainSpeedMultiplier(start, end, areas, UnitTroop::Mounted, 4.f) == 1.5f);
    CHECK(TerrainSpeedMultiplier({-20, 0, 0}, end, areas, UnitTroop::Mounted, 4.f) == 1.f);
}
TEST_CASE("integration.wall_and_hedge_send_existing_members_to_adjoining_openings") {
    for (auto feature : {TerrainFeature::StoneWall, TerrainFeature::Hedge}) {
        auto scenario = test::Duel(3);
        scenario.regiments.resize(1);
        scenario.units.resize(3);
        scenario.rules.regiment_tactics = true;
        scenario.obstacles = {{-1,-20,1,-9,true,feature}, {-1,-5,1,5,true,feature},
                              {-1,9,1,20,true,feature}};
        auto &spec = scenario.regiments.front();
        spec.pose = *TerrainCoverPosition(scenario.obstacles[1], {{-2,0,0},0}, .3f, 2.3f);
        for (auto &unit : scenario.units)
            unit.pose = spec.pose;
        auto created = CreateWorld(scenario, 2);
        REQUIRE(created.status == Status::Ok);
        auto &world = created.value;
        for (int frame = 0; frame < 100; ++frame)
            AdvanceFormations(world, .05f);
        REQUIRE(world.units.size() == 3);
        CHECK(world.units[0].pose.position.y < -5.f);
        CHECK(world.units[1].pose.position.y == doctest::Approx(0.f));
        CHECK(world.units[2].pose.position.y > 5.f);
        auto &regiment = world.regiments.front();
        regiment.route = {{{-10.f,0.f,0.f}, Pi}};
        for (int frame = 0; frame < 300; ++frame)
            AdvanceFormations(world, .05f);
        CHECK(Distance(world.units[0].pose.position, regiment.pose.position) < 2.1f);
        CHECK(Distance(world.units[2].pose.position, regiment.pose.position) < 2.1f);
    }
}
TEST_CASE("integration.march_steep_hillsides_distinguish_mounted_and_foot_regiments") {
    MarchRegiment foot{{1}, 0, {{0,-32,0}, Pi/2}};
    MarchRegiment horse = foot;
    horse.troop = UnitTroop::Mounted;
    MarchMission mounted({horse}, MarchTerrain::Hill, true);
    MarchMission infantry({foot}, MarchTerrain::Hill, true);
    CHECK_FALSE(mounted.PreviewWaypoint({1}, {0,-19,0}));
    REQUIRE(mounted.SetWaypoint({1}, {0,0,0}));
    CHECK(mounted.Regiments()[0].route.size() > 1);
    for (int tick = 0; tick < 800; ++tick) mounted.Step(.05f);
    CHECK(Distance(mounted.Regiments()[0].pose.position, {}) < .01f);
    horse.pose.position = {0,-19,0};
    CHECK_FALSE(MarchStartWalkable(horse, MarchTerrain::Hill, true));
    CHECK(HillUphillSpeedMultiplier(TerrainHill{}, {0,-19,0}, {0,-20,0}, UnitTroop::Mounted, 4.f) == 0.f);
    CHECK(HillUphillSpeedMultiplier(TerrainHill{}, {0,-19,0}, {0,-20,0}, UnitTroop::Infantry, 4.f) == .25f);
    REQUIRE(infantry.SetWaypoint({1}, {0,-19,0}));
    for (int tick = 0; tick < 600; ++tick) infantry.Step(.05f);
    CHECK(infantry.Regiments()[0].pose.position.y == doctest::Approx(-19.f));
}
TEST_CASE("integration.field_order_delivers_guard_detail_from_the_moving_regiment") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    scenario.obstacles = {{-1,-20,1,-9,true,TerrainFeature::StoneWall},
                          {-1,-5,1,5,true,TerrainFeature::Hedge},
                          {-1,9,1,20,true,TerrainFeature::StoneWall}};
    auto world = CreateWorld(scenario, 2).value;
    const GridNavigationService grid(scenario.obstacles, .5f, 101, 101, {-25,-25,0});
    const auto &r = world.regiments.front();
    const auto footprint = Footprint(r);
    const auto cover = TerrainCoverPosition(scenario.obstacles[1], r.pose, footprint.depth_m * .5f, footprint.width_m * .5f);
    REQUIRE(cover);
    Order order;
    order.type = OrderType::MoveTo;
    order.recipient = r.id;
    order.point = cover->position;
    order.facing = cover->yaw;
    order.player_direct = true;
    REQUIRE(SubmitOrder(world, order) == Status::Ok);
    for (int tick = 0; tick < 240; ++tick) REQUIRE(Step(world, {}, {}, grid));
    REQUIRE(r.order);
    CHECK(r.order->status == OrderStatus::Completed);
    CHECK(Distance(r.pose.position, cover->position) < .02f);
    CHECK(world.units[0].pose.position.y < -5.f);
    CHECK(world.units[2].pose.position.y > 5.f);
}
TEST_CASE("integration.staff_crossing_order_sets_precedence_and_far_bank_deployment") {
    MarchRegiment near{{12},0,{{-13,0,0},0}, Vec3{-13,0,0}};
    MarchRegiment far{{22},0,{{-24,0,0},0}, Vec3{-24,0,0}};
    MarchMission march({near,far}, MarchTerrain::StirlingBridge, true);
    CHECK_FALSE(march.QueueCrossing({12}, {-20,1,0}));
    REQUIRE(march.QueueCrossing({22}, {19,6,0}));
    REQUIRE(march.QueueCrossing({12}, {19,-6,0}));
    CHECK(march.CrossingQueuePosition({12}) == 2);
    CHECK(march.CrossingQueuePosition({22}) == 1);
    CHECK_FALSE(march.QueueCrossing({12}, {19,6,0})); // Already reserved by the head of the queue.
    CHECK_FALSE(march.QueueCrossing({22}, {-17,0,0}));
    CHECK(march.CrossingQueuePosition({22}) == 1);
    bool first_clear = false;
    bool follower_entered_early = false;
    for (int tick = 0; tick < 600; ++tick) {
        march.Step(.05f);
        const auto &first = march.Regiments()[1], &second = march.Regiments()[0];
        first_clear |= first.pose.position.x > MarchRiverHalfWidth;
        follower_entered_early |= second.pose.position.x >= -MarchRiverHalfWidth && !first_clear;
    }
    CHECK_FALSE(follower_entered_early);
    for (const auto &regiment : march.Regiments()) {
        CHECK(regiment.posture == MarchPosture::BattleLine);
        CHECK(regiment.deployment_seconds == 0.f);
        CHECK(regiment.pose.position.x == doctest::Approx(19.f));
    }
}
TEST_CASE("integration.independent_crossing_queues_preserve_order_and_release_cancelled_entries") {
    MarchRegiment a{{1},0,{{-25,-24,0},0}};
    MarchRegiment b{{2},0,{{-17,-24,0},0}};
    MarchRegiment c{{3},0,{{-25,24,0},0}};
    MarchMission march({c,b,a}, MarchTerrain::River, true);
    REQUIRE(march.QueueCrossing(a.id, {20,-24,0}));
    REQUIRE(march.QueueCrossing(b.id, {22,-24,0}));
    REQUIRE(march.QueueCrossing(c.id, {20,24,0}));
    CHECK(march.CrossingQueuePosition(b.id) == 2);
    CHECK(march.CrossingQueuePosition(c.id) == 1);
    REQUIRE(march.SetWaypoint(a.id, {-30,-20,0}));
    CHECK(march.CrossingQueuePosition(a.id) == 0);
    CHECK(march.CrossingQueuePosition(b.id) == 1);
    for (int i = 0; i < 500; ++i) march.Step(.05f);
    CHECK(march.Regiments()[0].posture == MarchPosture::BattleLine);
    CHECK(march.Regiments()[1].posture == MarchPosture::BattleLine);
    CHECK(march.Regiments()[2].posture == MarchPosture::TravelColumn);
}
TEST_CASE("integration.enemy_staff_assigns_crossing_and_free_deployment_points_once") {
    MarchRegiment near{{22},1,{{13,0,0},Pi},Vec3{-17,0,0}};
    MarchRegiment far{{12},1,{{32,0,0},Pi},Vec3{-17,0,0}};
    MarchMission march({near,far}, MarchTerrain::StirlingBridge, true);
    CHECK(march.CrossingQueuePosition(far.id) == 1);
    CHECK(march.CrossingQueuePosition(near.id) == 2);
    const auto goal = march.Regiments()[0].waypoint;
    REQUIRE(goal);
    REQUIRE(march.Regiments()[1].waypoint);
    CHECK(Distance(*goal, *march.Regiments()[1].waypoint) > 4.f);
    bool first_clear = false, second_entered_early = false;
    for (int tick = 0; tick < 650; ++tick) {
        march.Step(.05f);
        first_clear |= march.Regiments()[1].pose.position.x < -MarchRiverHalfWidth;
        second_entered_early |= march.Regiments()[0].pose.position.x <= MarchRiverHalfWidth && !first_clear;
    }
    CHECK_FALSE(second_entered_early);
    CHECK(march.Regiments()[0].waypoint == goal);
    for (const auto &regiment : march.Regiments()) {
        CHECK(regiment.posture == MarchPosture::BattleLine);
        CHECK(regiment.pose.position.x < -MarchRiverHalfWidth);
    }
}
TEST_CASE("integration.historical_relief_and_navigation_follow_the_map") {
    const MarchField waterloo_field{560, 380};
    const auto waterloo = MarchHill(MarchTerrain::Waterloo, waterloo_field);
    REQUIRE(waterloo);
    const auto crest = HillObjectiveCrest(*waterloo);
    REQUIRE(crest);
    CHECK(crest->center.x == doctest::Approx(75.6f));
    CHECK(OnHillCrest(*waterloo, crest->center));
    CHECK_FALSE(OnHillCrest(*waterloo, {}));
    CHECK_FALSE(HillHasLineOfSight(*waterloo, {-40.f, 0.f, 0.f}, {176.f, 0.f, 0.f}));
    CHECK(std::ranges::count(MarchObstacles(MarchTerrain::Waterloo, waterloo_field),
                             TerrainFeature::Barrier, &Obstacle::feature) == 3);

    const MarchField sekigahara_field{520, 360};
    const auto sekigahara = MarchHill(MarchTerrain::Sekigahara, sekigahara_field);
    REQUIRE(sekigahara);
    CHECK_FALSE(HillObjectiveCrest(*sekigahara));
    CHECK(HillHeight(*sekigahara, {-120.f, -166.f, 0.f}) > 20.f);
    CHECK(HillHeight(*sekigahara, {-120.f, 166.f, 0.f}) > 18.f);
    CHECK(HillHeight(*sekigahara, {}) == 0.f);
    CHECK(HillHasLineOfSight(*sekigahara, {-120.f, 0.f, 0.f}, {120.f, 0.f, 0.f}));
    CHECK(std::ranges::count(MarchObstacles(MarchTerrain::Sekigahara, sekigahara_field),
                             TerrainFeature::Forest, &Obstacle::feature) == 4);

    const MarchField wall_field{520, 340};
    MarchRegiment regiment{{1}, 0, {{-100.f, 0.f, 0.f}, 0.f}};
    MarchMission pavia({regiment}, MarchTerrain::Pavia, true, wall_field);
    CHECK(pavia.PreviewWaypoint(regiment.id, {12.f, 0.f, 0.f}));
    regiment.pose.position = {34.f, 29.f, 0.f};
    CHECK_FALSE(MarchStartWalkable(regiment, MarchTerrain::Pavia, true, wall_field));
    const auto poltava = MarchTerrainAreas(MarchTerrain::Poltava, {540, 350});
    CHECK(std::ranges::count(poltava, TerrainFeature::RedoubtInterior, &Obstacle::feature) == 6);
    CHECK(std::ranges::count(MarchObstacles(MarchTerrain::Poltava, {540, 350}),
                             TerrainFeature::RedoubtBank, &Obstacle::feature) == 12);
    const auto agincourt = MarchTerrainAreas(MarchTerrain::Agincourt, {500, 300});
    CHECK(std::ranges::count(agincourt, TerrainFeature::Forest, &Obstacle::feature) == 6);
    CHECK(TerrainSpeedMultiplier({-20.f, 0.f, 0.f}, {20.f, 0.f, 0.f}, agincourt, UnitTroop::Mounted, 7.f) < 1.f);

    const auto nagashino = MarchTerrainAreas(MarchTerrain::Nagashino, wall_field);
    CHECK(MarchHasWaterCrossings(MarchTerrain::Nagashino));
    CHECK(std::ranges::count(nagashino, TerrainFeature::Ford, &Obstacle::feature) == 3);
    CHECK(std::ranges::count(nagashino, TerrainFeature::Hedge, &Obstacle::feature) == 4);
    CHECK(std::ranges::count(nagashino, TerrainFeature::Bridge, &Obstacle::feature) == 0);
    regiment.pose.position = {-100.f, 0.f, 0.f};
    MarchMission crossing({regiment}, MarchTerrain::Nagashino, true, wall_field);
    CHECK(crossing.PreviewCrossing(regiment.id, {24.f, 0.f, 0.f}));
}
} // namespace battle
