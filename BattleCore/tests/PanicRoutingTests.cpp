#include "TestSupport.h"
#include "../src/RegimentDiscipline.h"
#include "battle/Formation.h"
#include "battle/Snapshot.h"
#include "doctest.h"
#include <array>

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

TEST_CASE("combat.large_rout_flows_through_a_gate_and_allied_ranks") {
    constexpr unsigned RunnerCount = 480;
    Scenario scenario;
    scenario.rules.regiment_tactics = scenario.rules.local_melee = scenario.rules.individual_rout = true;
    scenario.rules.calm_ticks = 1;
    scenario.rules.rally_safe_radius_m = 8.f;
    scenario.field_bounds = TerrainFieldBounds{20.f, 30.f};
    // Only the northern boundary can be reached from this courtyard. Its gate
    // is wider than one fighter and narrower than even the smallest side of the regiment.
    scenario.obstacles = {{-20.f, -2.f, -.9f, 1.f}, {.9f, -2.f, 20.f, 1.f},
                          {-20.f, -30.f, -13.f, -1.f}, {13.f, -30.f, 20.f, -1.f},
                          {-20.f, -30.f, 20.f, -22.f}};
    const auto add_regiment = [&](uint64_t id, int team, unsigned count, unsigned files, Vec3 at, float speed) {
        RegimentSpec company;
        company.id = {id};
        company.team = team;
        company.formation = {files, .8f, .25f};
        company.pose = {at, Pi / 2};
        company.rally_point = {0, 20, 0};
        company.speed = speed;
        scenario.regiments.push_back(company);
        for (unsigned i = 0; i < count; ++i) {
            UnitSpec unit;
            unit.id = {id * 1000 + i + 1};
            unit.regiment = company.id;
            unit.pose = company.pose;
            unit.speed = speed;
            unit.weapon.damage = 0.f;
            scenario.units.push_back(unit);
        }
    };
    add_regiment(1, 0, RunnerCount, 24, {0, -12, 0}, 5.f);
    add_regiment(2, 0, 12, 4, {0, 3, 0}, 0.f);
    add_regiment(3, 1, 1, 1, {15, 20, 0}, 0.f);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &runners = world.regiments.front();
    runners.panic = true;
    bool rally = false;
    SUBCASE("flee the battlefield") { runners.cannot_rally = true; }
    SUBCASE("reach an active and safe rally point") { rally = true; }
    runners.rout_direction = {0, 1, 0};
    const GridNavigationService navigation(scenario.obstacles, 1.f, 41, 61, {-20, -30, 0});
    if (rally) {
        Order order;
        order.recipient = runners.id;
        order.type = OrderType::Rally;
        order.point = runners.rally_point;
        REQUIRE(SubmitOrder(world, order) == Status::Ok);
    }
    unsigned passed = 0;
    float max_step = 0.f;
    bool checked_snapshot = false;
    std::optional<uint64_t> restored_next_hash;
    bool exited_in_sequence = false;
    bool entered_wall = false;
    std::optional<Pose> rally_center;
    std::array<Vec3, RunnerCount> previous{};
    for (unsigned i = 0; i < RunnerCount; ++i)
        previous[i] = world.units[i].pose.position;
    for (unsigned tick = 0; tick < 2200 && !runners.units.empty() && runners.panic; ++tick) {
        if (rally && runners.rout_to_rally && !runners.route.empty())
            rally_center = runners.route.back();
        REQUIRE(Step(world, {}, {}, navigation));
        if (restored_next_hash) {
            CHECK(StateHash(world) == *restored_next_hash);
            restored_next_hash.reset();
        }
        passed = 0;
        for (unsigned i = 0; i < RunnerCount; ++i) {
            const auto now = world.units[i].pose.position;
            passed += now.y > 4.f;
            entered_wall |= now.y > -2.249f && now.y < 1.249f && std::abs(now.x) > .651f;
            max_step = std::max(max_step, Distance(now, previous[i]));
            previous[i] = now;
        }
        exited_in_sequence |= passed < RunnerCount && runners.units.size() < RunnerCount;
        const bool underway = rally ? passed >= RunnerCount / 4 : runners.units.size() < RunnerCount;
        if (!checked_snapshot && underway && passed < RunnerCount) {
            auto restored = Restore(Capture(world));
            REQUIRE(restored);
            CHECK(StateHash(world) == StateHash(restored.value));
            REQUIRE(Step(restored.value, {}, {}, navigation));
            restored_next_hash = StateHash(restored.value);
            checked_snapshot = true;
        }
    }
    CHECK(checked_snapshot);
    CHECK(passed == RunnerCount);
    if (rally) {
        CHECK_FALSE(runners.panic);
        CHECK(runners.units.size() == RunnerCount);
        REQUIRE(runners.order);
        CHECK(runners.order->status == OrderStatus::Completed);
        REQUIRE(rally_center);
        const auto slots = BuildFormationSlots(runners.formation, *rally_center, RunnerCount);
        float most_distant = 0.f;
        for (const auto id : runners.units) {
            const auto &unit = *FindUnit(world, id);
            most_distant = std::max(most_distant, Distance(unit.pose.position, slots[unit.slot_index].position));
        }
        CHECK(most_distant <= .10f);
    } else {
        CHECK(exited_in_sequence);
        CHECK(runners.units.empty());
    }
    CHECK(max_step <= .251f);
    CHECK_FALSE(entered_wall);
    CHECK_FALSE(world.regiments[1].panic);
    CHECK(world.regiments[1].units.size() == 12);
    CHECK(Restore(Capture(world)));
}
