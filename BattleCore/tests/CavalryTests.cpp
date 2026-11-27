#include "TestSupport.h"
#include "doctest.h"
#include "battle/Combat.h"
#include "battle/Formation.h"

using namespace battle;

namespace {
Scenario MountedDuel(CavalryClass cavalry) {
    auto scenario = test::Duel();
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].speed = 5.f;
    scenario.units[0].troop = UnitTroop::Mounted;
    scenario.units[0].cavalry = cavalry;
    scenario.units[0].speed = 6.f;
    scenario.units[0].health = scenario.units[1].health = 100.f;
    scenario.units[0].weapon = {10.f, 1.5f, 24, 2};
    scenario.units[1].weapon.damage = 0.f;
    return scenario;
}

GridNavigationService CavalryNavigation() {
    return GridNavigationService({}, .5f, 120, 120, {-30.f, -30.f, 0.f});
}
} // namespace

TEST_CASE("formation.light_cavalry_wheels_faster_than_heavy_cavalry_at_equal_speed") {
    const auto turn = [](CavalryClass cavalry) {
        auto created = CreateWorld(MountedDuel(cavalry), 7);
        REQUIRE(created);
        auto &world = created.value;
        const auto nav = CavalryNavigation();
        Order order;
        order.type = OrderType::MoveTo;
        order.recipient = {1};
        order.point = {-10.f, 8.f, 0.f};
        order.facing = Pi * .5f;
        REQUIRE(SubmitOrder(world, order) == Status::Ok);
        for (int i = 0; i < 10; ++i)
            REQUIRE(Step(world, {}, {}, nav));
        return world.regiments.front().pose.yaw;
    };

    const float heavy = turn(CavalryClass::Heavy);
    const float legacy = turn(CavalryClass::None);
    const float light = turn(CavalryClass::Light);
    CHECK(heavy < legacy);
    CHECK(legacy < light);
}

TEST_CASE("combat.heavy_cavalry_builds_speed_slower_but_hits_harder_than_light_cavalry") {
    const auto charge = [](CavalryClass cavalry) {
        auto created = CreateWorld(MountedDuel(cavalry), 7);
        REQUIRE(created);
        auto &world = created.value;
        const auto nav = CavalryNavigation();
        Order order;
        order.type = OrderType::Attack;
        order.recipient = {1};
        order.target_regiment = {2};
        REQUIRE(SubmitOrder(world, order) == Status::Ok);
        order.type = OrderType::HoldLine;
        order.recipient = {2};
        order.target_regiment = {};
        REQUIRE(SubmitOrder(world, order) == Status::Ok);
        for (int i = 0; i < 160; ++i) {
            REQUIRE(Step(world, {}, {}, nav));
            for (const auto &event : world.events) {
                if (event.kind == EventKind::CavalryImpact)
                    return std::pair{world.tick, event.amount};
            }
        }
        return std::pair{Tick{}, 0.f};
    };

    const auto [heavy_tick, heavy_damage] = charge(CavalryClass::Heavy);
    const auto [legacy_tick, legacy_damage] = charge(CavalryClass::None);
    const auto [light_tick, light_damage] = charge(CavalryClass::Light);
    REQUIRE(heavy_tick > 0);
    REQUIRE(legacy_tick > 0);
    REQUIRE(light_tick > 0);
    CHECK(light_tick < heavy_tick);
    CHECK(light_damage < legacy_damage);
    CHECK(heavy_damage > 1.35f * light_damage);
}

TEST_CASE("combat.a_coherent_cavalry_wedge_adds_shock_to_a_ready_charge") {
    const auto impact_damage = [](FormationShape shape, float cohesion) {
        auto scenario = MountedDuel(CavalryClass::Heavy);
        scenario.regiments[0].pose.position.x = 8.9f;
        scenario.regiments[0].formation.shape = shape;
        scenario.units[0].pose = scenario.regiments[0].pose;
        auto created = CreateWorld(scenario, 7);
        REQUIRE(created);
        auto &world = created.value;
        auto &formation = world.regiments.front();
        formation.cavalry_phase = CavalryPhase::Approach;
        formation.cavalry_target = {2};
        formation.charge_runup_m = 8.f;
        formation.cohesion = cohesion;
        std::vector<BattleEvent> launches;
        BuildCombatEvents(world, launches);
        for (const auto &event : launches)
            if (event.source == UnitId{1} && event.reason.starts_with("cavalry impact"))
                return event.amount;
        return 0.f;
    };
    const float line = impact_damage(FormationShape::Rectangle, 1.f);
    const float wedge = impact_damage(FormationShape::Wedge, 1.f);
    REQUIRE(line > 0.f);
    CHECK(wedge > line);
    CHECK(wedge < 1.12f * line);
    const float loose_line = impact_damage(FormationShape::Rectangle, .65f);
    REQUIRE(loose_line > 0.f);
    CHECK(impact_damage(FormationShape::Wedge, .65f) == doctest::Approx(loose_line));
}

TEST_CASE("combat.light_riders_throw_finite_javelins_and_keep_their_melee_weapon") {
    auto scenario = MountedDuel(CavalryClass::Light);
    scenario.regiments[0].pose.position.x = 4.f;
    scenario.units[0].pose = scenario.regiments[0].pose;
    scenario.units[0].ranged_weapon = RangedWeapon::Javelin;
    scenario.units[0].ammunition = 2;
    scenario.units[0].ranged_attack = WeaponSpec{2.f, 9.f, 8, 1, 30.f, 1.f};
    auto created = CreateWorld(scenario, 7);
    REQUIRE(created);
    auto &world = created.value;
    const auto nav = CavalryNavigation();
    Order order;
    order.type = OrderType::HoldLine;
    order.recipient = {1};
    REQUIRE(SubmitOrder(world, order) == Status::Ok);
    order.recipient = {2};
    REQUIRE(SubmitOrder(world, order) == Status::Ok);
    unsigned thrown = 0;
    for (int i = 0; i < 35; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        for (const auto &event : world.events)
            thrown += event.kind == EventKind::AttackLaunched && event.source == UnitId{1} && event.projectile;
    }
    CHECK(thrown == 2u);
    CHECK(world.units[0].ammunition_remaining == 0u);

    order.type = OrderType::Attack;
    order.recipient = {1};
    order.target_regiment = {2};
    REQUIRE(SubmitOrder(world, order) == Status::Ok);
    bool struck = false;
    for (int i = 0; i < 70; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        for (const auto &event : world.events)
            struck |= event.kind == EventKind::Hit && event.source == UnitId{1} && !event.projectile;
    }
    CHECK(struck);
}
