#include "TestSupport.h"
#include "battle/Formation.h"
#include "battle/Simulation.h"
#include "doctest.h"

using namespace battle;

TEST_CASE("formation.plotted_crossings_compare_time_and_remain_occupied_on_arrival") {
    const FormationFootprint size{.6f, .6f};
    const std::vector<TimedFormationPose> across{
        {0.f, {{-5.f, 0.f, 0.f}, 0.f}, size}, {10.f, {{5.f, 0.f, 0.f}, 0.f}, size}};
    std::vector<TimedFormationPose> crossing{
        {0.f, {{0.f, -5.f, 0.f}, 0.f}, size}, {10.f, {{0.f, 5.f, 0.f}, 0.f}, size}};
    const auto hit = FirstFormationRouteConflict(across, crossing);
    REQUIRE(hit);
    CHECK(hit->seconds == doctest::Approx(4.4f).epsilon(.001f));
    CHECK(Distance(hit->position, {-.3f, -.3f, 0.f}) < .002f);
    crossing.back().seconds = 3.f;
    CHECK_FALSE(FirstFormationRouteConflict(across, crossing));
    crossing.back().pose.position = {0.f, 0.f, 0.f};
    CHECK(FirstFormationRouteConflict(across, crossing));
}

TEST_CASE("formation.group_detours_together_at_the_slowest_pace") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.field_bounds = TerrainFieldBounds{12.f, 7.f};
    scenario.regiments[1].team = 0;
    scenario.regiments[1].speed = 1.f;
    scenario.regiments[0].pose = {{-4.f, 0.f, 0.f}, 0.f};
    scenario.regiments[1].pose = {{-4.f, 2.f, 0.f}, 0.f};
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    scenario.obstacles.push_back({-1.f, -.5f, 1.f, .5f});
    auto created = CreateWorld(scenario, 18);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService nav(scenario.obstacles, .5f, 49, 29, {-12.f, -7.f, 0.f});
    for (const auto &regiment : world.regiments) {
        Order order;
        order.type = OrderType::MoveTo;
        order.recipient = regiment.id;
        order.maneuver_group = 7;
        order.point = regiment.pose.position + Vec3{8.f, 0.f, 0.f};
        order.facing = regiment.pose.yaw;
        REQUIRE(SubmitOrder(world, order) == Status::Ok);
    }
    float lateral = 0.f;
    for (int tick = 0; tick < 320; ++tick) {
        const auto previous = world.regiments[0].pose.position;
        REQUIRE(Step(world, {}, {}, nav));
        const auto a = world.regiments[0].pose.position;
        const auto b = world.regiments[1].pose.position;
        REQUIRE(Distance(b - a, {0.f, 2.f, 0.f}) < .001f);
        REQUIRE(Distance(previous, a) <= .0501f);
        REQUIRE_FALSE(RectanglesOverlap(world.regiments[0].pose, Footprint(world.regiments[0]),
                                       {{0.f, 0.f, 0.f}, 0.f}, {1.f, 2.f}));
        lateral = std::max(lateral, std::abs(a.y));
    }
    CHECK(lateral > .7f);
    CHECK(Distance(world.regiments[0].pose.position, {4.f, 0.f, 0.f}) < .01f);
    CHECK(Distance(world.regiments[1].pose.position, {4.f, 2.f, 0.f}) < .01f);
}
