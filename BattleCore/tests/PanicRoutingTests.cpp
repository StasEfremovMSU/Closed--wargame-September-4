#include "TestSupport.h"
#include "../src/RegimentDiscipline.h"
#include "battle/Snapshot.h"
#include "doctest.h"

using namespace battle;

TEST_CASE("combat.a_regiment_forced_to_rout_under_front_and_rear_melee_is_removed") {
    auto scenario = test::Duel(2);
    scenario.rules.regiment_tactics = true;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments[0];
    const auto strike = [&](AttackAspect aspect, UnitId source) {
        BattleEvent hit;
        hit.kind = EventKind::Hit;
        hit.source = source;
        hit.target = {1};
        hit.regiment = regiment.id;
        hit.amount = .01f;
        hit.aspect = aspect;
        world.events = {hit};
        UpdateRegimentDiscipline(world);
        ++world.tick;
    };
    strike(AttackAspect::Front, {3});
    CHECK(regiment.units.size() == 2);
    regiment.morale = .20f;
    strike(AttackAspect::Rear, {4});
    CHECK(regiment.units.empty());
    CHECK_FALSE(FindUnit(world, {1})->alive);
    CHECK(world.outcome == BattleOutcome::Victory);
    CHECK(world.winner_team == 1);
    CHECK(Restore(Capture(world)));
}

TEST_CASE("combat.an_unrallyable_regiment_flees_to_its_field_edge_and_leaves_the_battle") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.field_bounds = TerrainFieldBounds{16.f, 10.f};
    scenario.regiments[0].rally_point = scenario.regiments[0].pose.position;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments[0];
    regiment.panic = true;
    regiment.cannot_rally = true;
    const GridNavigationService nav({}, 1.f, 33, 21, {-16, -10, 0});
    bool turned_in_place = false;
    float travel = 0.f;
    float rearward = 0.f;
    for (unsigned frame = 0; frame < 200 && !regiment.units.empty(); ++frame) {
        const auto before = regiment.pose;
        REQUIRE(Step(world, {}, {}, nav));
        const auto delta = regiment.pose.position - before.position;
        turned_in_place |= Length(delta) < .001f && std::abs(regiment.pose.yaw - before.yaw) > .001f;
        travel += Length(delta);
        rearward += std::max(0.f, -Dot(delta, Forward(regiment.pose.yaw)));
    }
    CHECK(turned_in_place);
    CHECK(travel > 5.f);
    CHECK(rearward < .05f);
    CHECK(regiment.pose.position.x < -15.f);
    CHECK(regiment.units.empty());
    CHECK(world.outcome == BattleOutcome::Victory);
    CHECK(world.winner_team == 1);
    CHECK(Restore(Capture(world)));
}

TEST_CASE("combat.mounted_rout_routes_around_an_impassable_hill") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.field_bounds = TerrainFieldBounds{23.f, 17.f};
    scenario.hill = TerrainHill{7.f, 5.f, 11.f, .30f};
    scenario.regiments[1].pose.position = {-18.f, 4.f, 0.f};
    scenario.units[1].pose = scenario.regiments[1].pose;
    scenario.units[0].troop = UnitTroop::Mounted;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    regiment.panic = true;
    regiment.cannot_rally = true;
    regiment.rout_direction = {1.f, 0.f, 0.f};
    const GridNavigationService nav({}, 1.f, 47, 35, {-23.f, -17.f, 0.f});
    CHECK_FALSE(HillSegmentPassable(*scenario.hill, regiment.pose.position, {20.f, 0.f, 0.f},
                                   UnitTroop::Mounted));
    REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(regiment.route.size() > 1);
    auto prior = scenario.regiments[0].pose.position;
    for (const auto &waypoint : regiment.route) {
        CHECK(HillSegmentPassable(*scenario.hill, prior, waypoint.position, UnitTroop::Mounted));
        prior = waypoint.position;
    }
    for (int tick = 0; tick < 750 && !regiment.units.empty(); ++tick)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.units.empty());
    CHECK(regiment.pose.position.x > 20.f);
}
