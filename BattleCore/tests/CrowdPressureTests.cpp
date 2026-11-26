#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Formation.h"
#include "battle/Snapshot.h"
#include "doctest.h"

using namespace battle;

namespace {
World ChargeCrowd(bool braced = false, bool wall = false, bool lethal = false) {
    auto scenario = test::Duel(2);
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    scenario.rules.adaptive_battle = true;
    scenario.regiments[0].pose.position = {-1.6f, 0, 0};
    scenario.regiments[0].formation = {1, 2.f, .25f};
    scenario.regiments[1].pose.position = {.85f, 0, 0};
    scenario.regiments[1].formation = {1, .5f, .25f};
    for (size_t i = 0; i < scenario.units.size(); ++i) {
        auto &unit = scenario.units[i];
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.health = 100.f;
        unit.weapon = {i < 2 ? (lethal ? 200.f : 21.f) : 0.f, 1.55f, 5, 1};
        if (i < 2) {
            unit.troop = UnitTroop::Mounted;
            unit.cavalry = CavalryClass::Heavy;
        } else if (braced)
            unit.troop = UnitTroop::Spearman;
    }
    if (wall)
        scenario.obstacles.push_back({1.36f, -1.f, 1.6f, 1.f, true, TerrainFeature::StoneWall});
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto world = std::move(created.value);
    auto &riders = world.regiments.front();
    const auto target = world.regiments.back().id;
    riders.cavalry_phase = CavalryPhase::Approach;
    riders.charge_runup_m = 8.f;
    riders.cavalry_target = target;
    world.regiments.back().braced = braced;
    return world;
}

void ResolveOnlyRider(World &world) {
    std::vector<BattleEvent> attacks;
    BuildCombatEvents(world, attacks);
    std::erase_if(attacks, [](const auto &event) { return event.source != UnitId{1}; });
    REQUIRE(attacks.size() == 1);
    REQUIRE(attacks.front().reason.starts_with("cavalry impact"));
    CommitCombatEvents(world, attacks);
    REQUIRE(std::ranges::any_of(world.events, [](const auto &event) {
        return event.kind == EventKind::CavalryImpact && event.source == UnitId{1};
    }));
}
} // namespace

TEST_CASE("combat.cavalry_impact_displaces_living_targets_and_transmits_a_bounded_shove") {
    auto open = ChargeCrowd();
    const auto target_before = FindUnit(open, {3})->pose.position;
    const auto support_before = FindUnit(open, {4})->pose.position;
    ResolveOnlyRider(open);
    const auto target_after = FindUnit(open, {3})->pose.position;
    const auto support_after = FindUnit(open, {4})->pose.position;
    CHECK(target_after.x > target_before.x + .1f);
    CHECK(target_after.x < target_before.x + .62f);
    CHECK(support_after.x > support_before.x + .08f);
    CHECK(Distance(target_after, support_after) >= .4999f);
    ++open.tick;
    auto restored = Restore(Capture(open));
    REQUIRE(restored);
    CHECK(StateHash(open) == StateHash(restored.value));

    auto walled = ChargeCrowd(false, true);
    ResolveOnlyRider(walled);
    CHECK(FindUnit(walled, {4})->pose.position.x <= 1.1101f);
    CHECK(FindUnit(walled, {3})->pose.position.x <= .6101f);

    auto braced = ChargeCrowd(true);
    ResolveOnlyRider(braced);
    CHECK(FindUnit(braced, {3})->pose.position.x < target_after.x - .05f);

    auto fatal = ChargeCrowd(false, false, true);
    ResolveOnlyRider(fatal);
    CHECK_FALSE(FindUnit(fatal, {3})->alive);
    CHECK(FindUnit(fatal, {4})->pose.position == support_before);
}

TEST_CASE("combat.backed_attackers_push_while_idle_ranks_do_not") {
    auto scenario = test::Duel(9);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.rules.adaptive_battle = true;
    scenario.regiments[0].pose.position = {-1.58f, 0.f, 0.f};
    scenario.regiments[0].formation = {3, .65f, .25f};
    scenario.regiments[1].pose.position = {.17f, 0.f, 0.f};
    scenario.regiments[1].formation = {3, .65f, .25f};
    scenario.units.resize(12);
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto attack = created.value;
    auto hold = created.value;
    auto friendly = created.value;
    auto solo = created.value;
    friendly.regiments[1].team = friendly.regiments[0].team;
    for (auto *world : {&attack, &friendly, &solo}) {
        world->regiments[0].ai_action = OrderType::Attack;
        world->regiments[0].ai_target = {2};
    }
    solo.regiments[0].units.resize(3);
    for (size_t i = 3; i < 9; ++i)
        solo.units[i].alive = false;
    const auto start = FindUnit(attack, {11})->pose.position;
    for (int tick = 0; tick < 5; ++tick) {
        for (auto *world : {&hold, &attack, &solo, &friendly})
            AdvanceFormations(*world, .05f);
    }
    CHECK(FindUnit(attack, {11})->pose.position.x > FindUnit(hold, {11})->pose.position.x + .015f);
    CHECK(FindUnit(solo, {11})->pose.position.x <= FindUnit(attack, {11})->pose.position.x - .015f);
    CHECK(FindUnit(friendly, {11})->pose.position == start);
}

TEST_CASE("combat.a_confirmed_charge_shoves_even_if_the_rider_falls_in_the_same_tick") {
    auto world = ChargeCrowd();
    auto *rider = FindUnit(world, {1});
    auto *defender = FindUnit(world, {3});
    defender->weapon.damage = 150.f;
    const float start = defender->pose.position.x;
    std::vector<BattleEvent> attacks;
    BuildCombatEvents(world, attacks);
    std::erase_if(attacks, [](const auto &event) {
        return event.source != UnitId{1} && event.source != UnitId{3};
    });
    REQUIRE(attacks.size() == 2);
    CommitCombatEvents(world, attacks);
    CHECK_FALSE(rider->alive);
    REQUIRE(defender->alive);
    REQUIRE(std::ranges::any_of(world.events, [](const auto &event) {
        return event.kind == EventKind::CavalryImpact && event.source == UnitId{1};
    }));
    CHECK(defender->pose.position.x > start + .10f);
}

TEST_CASE("combat.retained_windup_cannot_push_a_defender_through_a_wall") {
    auto scenario = test::Duel(9);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose.position = {-1.58f, 0.f, 0.f};
    scenario.regiments[1].pose.position = {.17f, 0.f, 0.f};
    for (auto &regiment : scenario.regiments)
        regiment.formation = {3, .65f, .25f};
    scenario.units.resize(12);
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    scenario.obstacles.push_back({-.55f, -.15f, -.45f, .15f, true, TerrainFeature::StoneWall});
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto holding = created.value;
    auto attacking = created.value;
    attacking.regiments[0].ai_action = OrderType::Attack;
    attacking.regiments[0].ai_target = {2};
    for (auto *world : {&holding, &attacking}) {
        world->attacks.push_back({{1}, {2}, {11}, 10, 1.f, true, 1.5f, false});
        AdvanceFormations(*world, .05f);
        REQUIRE(world->regiments[0].melee_engaged); // Both exterior files remain in contact.
    }
    CHECK(FindUnit(attacking, {11})->pose.position.x ==
          doctest::Approx(FindUnit(holding, {11})->pose.position.x).epsilon(.0001));
}
