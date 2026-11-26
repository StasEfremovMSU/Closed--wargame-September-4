#include "JsonBindings.h"
#include "TestSupport.h"
#include "battle/Formation.h"
#include "battle/Spatial.h"
#include "doctest.h"
using namespace battle;
TEST_CASE("navigation.spatial_updates_and_edges") {
    SpatialIndex index;
    std::vector<UnitPosition> p{{{9}, {10, 10, 0}}, {{2}, {13, 14, 0}}, {{7}, {-100, -100, 0}}};
    index.Rebuild(p);
    auto ids = index.QueryRadius({10, 10, 0}, 5.01f);
    REQUIRE(ids.size() == 2);
    CHECK(ids[0].value == 2);
    CHECK(ids[1].value == 9);
    CHECK(Distance(p[0].position, p[1].position) == 5);
    CHECK(index.QueryRadius({-100, -100, 0}, 1).size() == 1);
    p[0].position = {50, 50, 0};
    index.Rebuild(p);
    CHECK(index.QueryRadius({10, 10, 0}, 1).empty());
}
TEST_CASE("navigation.footprint_corridor") {
    std::vector<Obstacle> wall{{-1, -100, 1, -1}, {-1, 1, 1, 100}};
    GridNavigationService nav(wall, 1, 40, 40, {-20, -20, 0});
    auto column = nav.FindPath({{{-8, 0, 0}, 0}, {8, 0, 0}, {.6f, 6}});
    CHECK(column.status == Status::Ok);
    CHECK(!column.waypoints.empty());
    auto line = nav.FindPath({{{-8, 0, 0}, 0}, {8, 0, 0}, {4, 1}});
    CHECK(line.status == Status::NoPath);
    CHECK(nav.FindPath({{{-100, 0, 0}, 0}, {8, 0, 0}, {.6f, 1}}).status == Status::NoPath);
    CHECK(nav.FindPath({{{-8, 0, 0}, 0}, {0, 3, 0}, {.6f, 1}}).status == Status::NoPath);
}
TEST_CASE("navigation.a_disconnected_search_does_not_hide_routes_on_this_or_a_changed_map") {
    GridNavigationService nav({{-.5f, -44.f, .5f, 44.f}}, 2.f, 41, 41, {-40, -40, 0});
    const PathRequest far{{{-28.f, 0, 0}, 0}, {23.8f, 0, 0}, {2.f, 2.f}};
    CHECK(nav.FindPath(far).status == Status::NoPath);

    auto nearby = far;
    nearby.goal = {-15.7f, .3f, 0};
    const auto local = nav.FindPath(nearby);
    REQUIRE(local.status == Status::Ok);
    REQUIRE_FALSE(local.waypoints.empty());
    CHECK(local.waypoints.back().position == nearby.goal);

    nav = GridNavigationService({{-.5f, 8.f, .5f, 44.f}}, 2.f, 41, 41, {-40, -40, 0});
    const auto across = nav.FindPath(far);
    REQUIRE(across.status == Status::Ok);
    REQUIRE_FALSE(across.waypoints.empty());
    CHECK(across.waypoints.back().position == far.goal);
}
TEST_CASE("navigation.motion_does_not_cross_wall") {
    auto s = test::Duel();
    s.regiments.resize(1);
    s.units.resize(1);
    s.regiments[0].pose.position.x = -3;
    s.regiments[0].speed = 100;
    s.units[0].pose = s.regiments[0].pose;
    s.units[0].speed = 100;
    s.obstacles = {{-.1f, -2, .1f, 2}};
    auto w = CreateWorld(s, 42).value;
    w.regiments[0].route = {{{3, 0, 0}, 0}};
    AdvanceFormations(w, .1f);
    CHECK(w.regiments[0].pose.position.x <= -.399f);
}
TEST_CASE("navigation.rotation_sweep") {
    GridNavigationService blocked({{1, 2, 2, 3}}, 1, 30, 30, {-15, -15, 0});
    CHECK_FALSE(blocked.CanTraverse({{}, 0}, {{}, Pi / 2}, {.6f, 10}));
    GridNavigationService open({}, 1, 30, 30, {-15, -15, 0});
    CHECK(open.CanTraverse({{}, 0}, {{}, Pi / 2}, {.6f, 10}));
}
TEST_CASE("navigation.fallback_has_a_swept_route_without_turning_its_back") {
    const GridNavigationService navigation({{-.4f, -2.f, .4f, 2.f}}, 1, 35, 30, {-17, -15, 0});
    const Pose start{{9, 0, 0}, .22f};
    const Vec3 goal{-9, 0, 0};
    const FormationFootprint footprint{2.f, 2.f};
    const auto route = navigation.FindPath({start, goal, footprint, start.yaw, {}, true, true});
    REQUIRE(route.status == Status::Ok);
    REQUIRE(route.waypoints.size() > 1);
    auto from = start;
    for (const auto &pose : route.waypoints) {
        CHECK(pose.yaw == start.yaw);
        CHECK(navigation.CanTraverse(from, pose, footprint));
        from = pose;
    }
    CHECK(route.waypoints.back().position == goal);
}
TEST_CASE("navigation.purposeful_route_turns_through_obstacles_and_at_arrival") {
    const GridNavigationService navigation({{-.4f, -2.f, .4f, 2.f}}, 1, 35, 30, {-17, -15, 0});
    const Pose start{{9.1f, -.2f, 0}, .22f};
    const Vec3 goal{-9.2f, .3f, 0};
    const FormationFootprint footprint{2.f, 2.f};
    const auto route = navigation.FindPath({.start = start, .goal = goal, .footprint = footprint,
        .arrival_yaw = .4f, .face_travel = true});
    REQUIRE(route.status == Status::Ok);
    REQUIRE_FALSE(route.waypoints.empty());
    CHECK((route.waypoints.back() == Pose{goal, .4f}));
    auto from = start;
    bool turned = false;
    for (const auto &pose : route.waypoints) {
        REQUIRE(navigation.CanTraverse(from, pose, footprint));
        const auto step = pose.position - from.position;
        if (Length(step) > .0001f)
            CHECK(Dot(step, Forward(pose.yaw)) == doctest::Approx(Length(step)).epsilon(.001));
        else if (pose.yaw != from.yaw)
            turned = true;
        from = pose;
    }
    CHECK(turned);
}
TEST_CASE("navigation.infantry_column_can_slide_past_a_tight_endpoint_with_its_ordered_front") {
    auto scenario = test::Duel(2);
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose = {{0.f, 0.f, 0.f}, 0.f};
    scenario.regiments[0].formation = {1, 2.f, .3f};
    scenario.units[0].pose = scenario.units[1].pose = scenario.regiments[0].pose;
    scenario.obstacles = {{5.8f, -11.4f, 6.2f, -10.8f, true}};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    const auto &world = created.value;
    const auto &column = world.regiments[0];
    const FlatNavigationService navigation;
    const Pose goal{{6.f, -10.f, 0.f}, 0.f};
    const auto route = FindFormationRoute(navigation, world, column, goal);
    REQUIRE(route.status == Status::Ok);
    REQUIRE_FALSE(route.waypoints.empty());
    CHECK(route.waypoints.back() == goal);
    auto widened = column;
    widened.pose = goal;
    widened.formation.columns = 2;
    CHECK(FindFormationRoute(navigation, world, widened, goal).status == Status::NoPath);
}
TEST_CASE("navigation.bottleneck_complete_motion") {
    Scenario s;
    RegimentSpec r;
    r.id = {1};
    r.pose.position = {-10, 0, 0};
    r.formation = {1, 1, .3f};
    s.regiments.push_back(r);
    s.obstacles = {{-1, -100, 1, -1}, {-1, 1, 1, 100}};
    for (unsigned i = 0; i < 10; ++i) {
        UnitSpec u;
        u.id = {i + 1};
        u.regiment = {1};
        u.pose = r.pose;
        s.units.push_back(u);
    }
    auto w = CreateWorld(s, 42).value;
    GridNavigationService nav(s.obstacles, 1, 50, 50, {-25, -25, 0});
    auto path = nav.FindPath({r.pose, {10, 0, 0}, Footprint(w.regiments[0])});
    REQUIRE(path.status == Status::Ok);
    w.regiments[0].route = path.waypoints;
    const auto dir = std::filesystem::path(BATTLE_SOURCE_DIR) / "out/artifacts/navigation";
    std::filesystem::create_directories(dir);
    for (unsigned tick = 0; tick < 600; ++tick) {
        if (tick % 200 == 0) {
            std::ofstream f(dir / (std::to_string(tick) + ".json"));
            f << nlohmann::json(Capture(w)).dump(2);
        }
        AdvanceFormations(w, .05f);
        ++w.tick;
    }
    CHECK(w.regiments[0].pose.position.x == doctest::Approx(10));
}
namespace {
std::optional<UnitPosition> RadiusReference(const std::vector<UnitPosition> &points, Vec3 p,
                                            float radius, int team) {
    std::optional<UnitPosition> best;
    float distance = radius;
    for (const auto &point : points) {
        auto d = point.position - p;
        if (point.team == team ||
            double(d.x) * d.x + double(d.y) * d.y + double(d.z) * d.z > double(radius) * radius)
            continue;
        float candidate = Length(d);
        if (!best || candidate < distance || (candidate == distance && point.id < best->id)) {
            best = point;
            distance = candidate;
        }
    }
    return best;
}
} // namespace
TEST_CASE("navigation.nearest_enemy_matches_radius_reference") {
    std::vector<UnitPosition> points;
    uint64_t state = 123;
    auto sample = [&]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return float(int((state >> 32) % 40001) - 20000) / 100.f;
    };
    for (size_t i = 0; i < 2000; ++i)
        points.push_back(
            {{i * 17 + 5}, {sample(), sample(), sample() * .1f}, int(i % 3), i + 9000});
    for (bool reverse : {false, true}) {
        if (reverse)
            std::reverse(points.begin(), points.end());
        SpatialIndex index;
        index.Rebuild(points);
        for (int query = 0; query < 300; ++query) {
            Vec3 p{sample(), sample(), sample() * .1f};
            float radius = query % 7 ? 35.f : 100000.f;
            int team = query % 3;
            auto expected = RadiusReference(points, p, radius, team);
            auto actual = index.FindNearestEnemy(p, radius, team);
            REQUIRE(bool(actual) == bool(expected));
            if (expected) {
                CHECK(actual->id == expected->id);
                CHECK(actual->payload_index == expected->payload_index);
                CHECK(actual->distance == Distance(p, expected->position));
            }
        }
    }
    points = {{{99}, {4, 0, 0}, 1, 700},
              {{2}, {-4, .0001f, 0}, 1, 12345},
              {{1}, {0, 0, 0}, 0, 5},
              {{3}, {0, 0, 10}, 1, 6}};
    SpatialIndex index;
    index.Rebuild(points);
    auto tied = index.FindNearestEnemy({0, 0, 0}, 5, 0);
    REQUIRE(tied);
    CHECK(tied->id == UnitId{2});
    CHECK(tied->payload_index == 12345);
    CHECK_FALSE(index.FindNearestEnemy({0, 0, 0}, 3.9f, 0));
    CHECK_FALSE(index.FindNearestEnemy({0, 0, 0}, -1, 0));
    CHECK_FALSE(index.FindNearestEnemy({0, 0, 0}, std::numeric_limits<float>::infinity(), 0));
    CHECK_FALSE(index.FindNearestEnemy({NAN, 0, 0}, 10, 0));
    points = {{{91}, {1e30f, 1e30f, 0}, 1, 42}};
    index.Rebuild(points);
    auto saturated = index.FindNearestEnemy({1e30f, 1e30f, 0}, 1, 0);
    REQUIRE(saturated);
    CHECK(saturated->payload_index == 42);
    SpatialIndex bad(0);
    bad.Rebuild(points);
    CHECK_FALSE(bad.FindNearestEnemy({0, 0, 0}, 10, 0));
}
TEST_CASE("navigation.nearest_enemy_prunes") {
    std::vector<UnitPosition> points{{{1}, {1, 0, 0}, 1, 0}};
    for (size_t i = 1; i < 4001; ++i)
        points.push_back({{i + 1}, {60 + float(i % 50), 60 + float(i / 50), 0}, 1, i});
    SpatialIndex index;
    index.Rebuild(points);
    uint64_t checks = 0;
    auto nearest = index.FindNearestEnemy({0, 0, 0}, 200, 0, &checks);
    REQUIRE(nearest);
    CHECK(nearest->id == UnitId{1});
    CHECK(checks < 100);
}
