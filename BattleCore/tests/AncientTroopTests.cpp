#include "TestSupport.h"
#include "JsonBindings.h"
#include "battle/Combat.h"
#include "battle/Formation.h"
#include "doctest.h"

using namespace battle;

namespace {
Scenario LegionaryDuel() {
    auto scenario = test::Duel();
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose.position.x = 0.f;
    scenario.regiments[1].pose.position.x = 8.f;
    scenario.units[0].pose = scenario.regiments[0].pose;
    scenario.units[1].pose = scenario.regiments[1].pose;
    scenario.units[1].health = 100.f;
    auto &legionary = scenario.units[0];
    legionary.weapon = {2.f, 1.5f, 11, 4, 20.f, 1.f};
    nlohmann::json profile = legionary;
    profile["ranged_weapon"] = RangedWeapon::Javelin;
    profile["ranged_attack"] = WeaponSpec{3.f, 12.f, 5, 2, 20.f, 1.f};
    profile["ammunition"] = 2;
    legionary = profile.get<UnitSpec>();
    return scenario;
}

World SkirmishDuel(bool loose, bool enemy_archers = false) {
    auto scenario = test::Duel(2);
    scenario.regiments[0].pose.position.x = 0.f;
    scenario.regiments[1].pose.position.x = 6.f;
    scenario.regiments[0].formation.spacing_m = .8f;
    nlohmann::json formation = scenario.regiments[0];
    if (loose)
        formation["loose_skirmish"] = true;
    scenario.regiments[0] = formation.get<RegimentSpec>();
    for (auto &unit : scenario.units) {
        const bool skirmisher = unit.regiment == RegimentId{1};
        unit.pose = scenario.regiments[skirmisher ? 0 : 1].pose;
        if (skirmisher || enemy_archers) {
            unit.archer = true;
            unit.ranged_weapon = RangedWeapon::Sling;
            unit.weapon.range = 18.f;
        }
    }
    auto world = CreateWorld(scenario, 31);
    REQUIRE(world);
    auto &enemy = *FindRegiment(world.value, {2});
    enemy.route.push_back({{-10.f, 0.f, 0.f}, Pi});
    enemy.ai_action = OrderType::Attack;
    enemy.ai_target = {1};
    return std::move(world.value);
}

const BattleEvent *AttackBy(const World &world, UnitId attacker) {
    const auto attack = std::ranges::find_if(world.events, [&](const BattleEvent &event) {
        return event.kind == EventKind::AttackLaunched && event.source == attacker;
    });
    return attack == world.events.end() ? nullptr : &*attack;
}
} // namespace

TEST_CASE("combat.ancient_legionary_throws_two_pila_at_range_in_live_combat") {
    auto world = CreateWorld(LegionaryDuel(), 31);
    REQUIRE(world);
    const FlatNavigationService navigation;
    unsigned throws = 0;
    for (Tick tick = 0; tick < 17; ++tick) {
        REQUIRE(Step(world.value, {}, {}, navigation));
        if (const auto *attack = AttackBy(world.value, {1})) {
            CHECK(attack->projectile);
            CHECK(attack->amount == doctest::Approx(3.f));
            CHECK(attack->due_tick - attack->tick == 8);
            CHECK(attack->tick == 5 * throws);
            ++throws;
        }
    }
    CHECK(throws == 2);
    CHECK(FindUnit(world.value, {1})->ammunition_remaining == 0);
    CHECK_FALSE(FindUnit(world.value, {1})->archer);

    auto &opponent = *FindRegiment(world.value, {2});
    opponent.pose.position.x = 1.f;
    FindUnit(world.value, {2})->pose = opponent.pose;
    REQUIRE(Step(world.value, {}, {}, navigation));
    const auto *attack = AttackBy(world.value, {1});
    REQUIRE(attack);
    CHECK_FALSE(attack->projectile);
}

TEST_CASE("combat.ancient_legionary_uses_melee_at_contact_even_with_a_pilum_left") {
    auto scenario = LegionaryDuel();
    scenario.regiments[1].pose.position.x = 1.f;
    scenario.units[1].pose = scenario.regiments[1].pose;
    auto world = CreateWorld(scenario, 31);
    REQUIRE(world);
    const FlatNavigationService navigation;
    REQUIRE(Step(world.value, {}, {}, navigation));
    const auto *attack = AttackBy(world.value, {1});
    REQUIRE(attack);
    CHECK_FALSE(attack->projectile);
    CHECK(attack->amount == doctest::Approx(2.f));
    CHECK(attack->due_tick - attack->tick == 4);
    CHECK(FindUnit(world.value, {1})->ammunition_remaining == 2);
}

TEST_CASE("replay.ancient_legionary_resumes_with_the_correct_pilum_stock_and_reload") {
    auto world = CreateWorld(LegionaryDuel(), 31);
    REQUIRE(world);
    const FlatNavigationService navigation;
    REQUIRE(Step(world.value, {}, {}, navigation));
    const nlohmann::json encoded = Capture(world.value);
    auto restored = Restore(encoded.get<Snapshot>());
    REQUIRE(restored);
    CHECK(FindUnit(restored.value, {1})->ammunition_remaining == 1);
    CHECK(StateHash(restored.value) == StateHash(world.value));
    for (Tick tick = 1; tick < 6; ++tick) {
        REQUIRE(Step(restored.value, {}, {}, navigation));
        const auto *attack = AttackBy(restored.value, {1});
        if (tick < 5)
            CHECK_FALSE(attack);
        else {
            REQUIRE(attack);
            CHECK(attack->projectile);
            CHECK(FindUnit(restored.value, {1})->ammunition_remaining == 0);
        }
    }
}

TEST_CASE("combat.ancient_dedicated_bow_sling_and_javelin_troops_use_their_ranged_profile") {
    for (const auto weapon : {RangedWeapon::Bow, RangedWeapon::Sling, RangedWeapon::Javelin}) {
        auto scenario = LegionaryDuel();
        auto &shooter = scenario.units[0];
        shooter.archer = true;
        shooter.weapon = *shooter.ranged_attack;
        shooter.ranged_attack.reset();
        shooter.ranged_weapon = weapon;
        auto world = CreateWorld(scenario, 31);
        REQUIRE(world);
        const FlatNavigationService navigation;
        REQUIRE(Step(world.value, {}, {}, navigation));
        const auto *attack = AttackBy(world.value, {1});
        REQUIRE(attack);
        CHECK(attack->projectile);
        CHECK(attack->amount == doctest::Approx(3.f));
        CHECK(FindUnit(world.value, {1})->ammunition_remaining == 1);
    }
}

TEST_CASE("formation.ancient_loose_skirmishers_deploy_with_more_room_between_fighters") {
    auto loose = SkirmishDuel(true);
    auto ordinary = SkirmishDuel(false);
    const auto spacing = [](const World &world) {
        return Distance(FindUnit(world, {1})->pose.position, FindUnit(world, {2})->pose.position);
    };
    CHECK(spacing(loose) > 1.5f * spacing(ordinary));
    REQUIRE(ReformRegiment(loose, {1}, {.columns = 2, .spacing_m = .9f}) == Status::Ok);
    const auto &slots = FindRegiment(loose, {1})->slots;
    CHECK(Distance(slots[0].position, slots[1].position) > 1.5f * spacing(ordinary));
}

TEST_CASE("formation.ancient_loose_skirmishers_give_ground_to_an_approaching_melee_formation") {
    auto loose = SkirmishDuel(true);
    auto ordinary = SkirmishDuel(false);
    auto against_archers = SkirmishDuel(true, true);
    auto against_disorder = SkirmishDuel(true);
    FindRegiment(against_disorder, {2})->disordered = true;
    auto against_holding = SkirmishDuel(true);
    FindRegiment(against_holding, {2})->route.clear();
    FindRegiment(against_holding, {2})->ai_action = OrderType::Hold;
    for (auto *world : {&loose, &ordinary, &against_archers, &against_disorder, &against_holding})
        AdvanceFormations(*world, .05f);
    CHECK(FindRegiment(loose, {1})->pose.position.x < 0.f);
    CHECK(FindUnit(loose, {1})->pose.position.x < 0.f);
    CHECK(FindUnit(loose, {1})->pose.yaw == doctest::Approx(0.f));
    CHECK(FindRegiment(ordinary, {1})->pose.position.x == 0.f);
    CHECK(FindRegiment(against_archers, {1})->pose.position.x == 0.f);
    CHECK(FindRegiment(against_disorder, {1})->pose.position.x == 0.f);
    CHECK(FindRegiment(against_holding, {1})->pose.position.x == 0.f);
}

TEST_CASE("formation.ancient_loose_skirmishers_react_to_melee_orders_in_standalone_live_combat") {
    auto world = SkirmishDuel(true);
    auto &enemy = *FindRegiment(world, {2});
    enemy.route.clear();
    enemy.ai_action = OrderType::Hold;
    enemy.ai_target = {};
    Order attack;
    attack.type = OrderType::Attack;
    attack.recipient = {2};
    attack.target_regiment = {1};
    REQUIRE(SubmitOrder(world, attack) == Status::Ok);
    const FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK(enemy.pose.position.x < 6.f);
    CHECK(FindRegiment(world, {1})->pose.position.x < 0.f);
    const nlohmann::json encoded = Capture(world);
    auto restored = Restore(encoded.get<Snapshot>());
    REQUIRE(restored);
    CHECK(StateHash(restored.value) == StateHash(world));
}
