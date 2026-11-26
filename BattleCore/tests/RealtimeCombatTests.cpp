#include "TestSupport.h"
#include "battle/Combat.h"
#include "doctest.h"

using namespace battle;

TEST_CASE("combat.hill_blocks_ranged_targeting_until_the_archer_can_see_it") {
    auto scenario = test::Duel();
    scenario.regiments[0].pose.position.x = -28.f;
    scenario.regiments[1].pose.position.x = 28.f;
    for (size_t i = 0; i < scenario.units.size(); ++i)
        scenario.units[i].pose = scenario.regiments[i].pose;
    scenario.units.front().archer = true;
    scenario.units.front().weapon.range = 60.f;
    for (bool tactics : {false, true}) {
        scenario.rules.regiment_tactics = tactics;
        auto flat = CreateWorld(scenario, 2);
        REQUIRE(flat);
        std::vector<BattleEvent> attacks;
        BuildCombatEvents(flat.value, attacks, nullptr);
        REQUIRE(1 == std::ranges::count_if(attacks, [](const auto &attack) { return attack.projectile; }));
        flat.value.scenario.hill = TerrainHill{};
        BuildCombatEvents(flat.value, attacks, nullptr);
        CHECK(0 == std::ranges::count_if(attacks, [](const auto &attack) { return attack.projectile; }));
        flat.value.units[1].pose.position.x = -18.f;
        BuildCombatEvents(flat.value, attacks, nullptr);
        CHECK(1 == std::ranges::count_if(attacks, [](const auto &attack) { return attack.projectile; }));
    }
}

TEST_CASE("combat.adaptive_clear_march_preserves_figures_and_resumes_combat") {
    auto scenario = test::Duel(40);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    for (auto &regiment : scenario.regiments) {
        regiment.pose.position.x *= 3.f;
        regiment.formation = {10, 1.1f, .25f};
    }
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    scenario.units[0].archer = true;
    scenario.units[0].weapon.range = 12.f;
    auto baseline = CreateWorld(scenario, 31);
    REQUIRE(baseline);
    scenario.rules.adaptive_battle = true;
    auto adaptive = CreateWorld(scenario, 31);
    REQUIRE(adaptive);
    for (auto *world : {&baseline.value, &adaptive.value})
        for (auto &regiment : world->regiments) {
            Order order;
            order.id = {regiment.id.value};
            order.recipient = regiment.id;
            order.status = OrderStatus::Active;
            order.type = OrderType::MoveTo;
            order.point = {regiment.team == 0 ? -2.2f : 2.2f, 0, 0};
            regiment.order = order;
        }
    FlatNavigationService navigation;
    bool ranged_attack = false;
    bool melee_hit = false;
    for (int i = 0; i < 400 && !(ranged_attack && melee_hit); ++i) {
        auto original = Step(baseline.value, {}, {}, navigation);
        auto optimized = Step(adaptive.value, {}, {}, navigation);
        REQUIRE(original);
        REQUIRE(optimized);
        const auto officer_trace = [](const auto &event) { return event.kind == EventKind::DecisionTrace; };
        std::erase_if(original.value.events, officer_trace);
        std::erase_if(optimized.value.events, officer_trace);
        CAPTURE(i);
        REQUIRE(original.value.events.size() == optimized.value.events.size());
        for (std::size_t e = 0; e < original.value.events.size(); ++e) {
            const auto &expected = original.value.events[e];
            const auto &actual = optimized.value.events[e];
            REQUIRE(expected.kind == actual.kind);
            REQUIRE(expected.source == actual.source);
            REQUIRE(expected.target == actual.target);
            REQUIRE(expected.amount == actual.amount);
            REQUIRE(expected.projectile == actual.projectile);
        }
        REQUIRE(baseline.value.units.size() == adaptive.value.units.size());
        for (std::size_t j = 0; j < baseline.value.units.size(); ++j) {
            REQUIRE(baseline.value.units[j].id == adaptive.value.units[j].id);
            REQUIRE(baseline.value.units[j].pose == adaptive.value.units[j].pose);
        }
        for (const auto &event : optimized.value.events) {
            ranged_attack |= event.kind == EventKind::AttackLaunched && event.projectile;
            melee_hit |= event.kind == EventKind::Hit && !event.projectile;
        }
    }
    CHECK(ranged_attack);
    CHECK(melee_hit);
}

namespace {
Scenario TacticalBattle() {
    Scenario scenario;
    scenario.rules.regiment_tactics = true;
    scenario.rules.ranged_cohorts = 1;
    return scenario;
}

UnitId AddFighter(Scenario &scenario, int32_t team, Vec3 position, float yaw, bool archer = false) {
    RegimentSpec regiment;
    regiment.id = {scenario.regiments.size() + 1};
    regiment.pose = {position, yaw};
    regiment.team = team;
    regiment.formation.columns = 1;
    scenario.regiments.push_back(regiment);
    UnitSpec unit;
    unit.id = {scenario.units.size() + 1};
    unit.regiment = regiment.id;
    unit.pose = regiment.pose;
    unit.health = 10.f;
    unit.archer = archer;
    if (archer)
        unit.weapon = {1.f, 24.f, 20, 2, 1000.f, 1.f};
    scenario.units.push_back(unit);
    return unit.id;
}

World CreateBattle(const Scenario &scenario) {
    auto world = CreateWorld(scenario, 42);
    REQUIRE(world);
    return std::move(world.value);
}

std::optional<BattleEvent> Shot(const World &world, UnitId shooter = {1}) {
    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    for (const auto &launch : launches)
        if (launch.source == shooter && launch.projectile)
            return launch;
    return {};
}
} // namespace

TEST_CASE("combat.tactical_archer_ignores_nearer_enemy_behind") {
    auto scenario = TacticalBattle();
    AddFighter(scenario, 0, {}, Pi / 2, true);
    AddFighter(scenario, 1, {0, -5, 0}, 0);
    auto front = AddFighter(scenario, 1, {2, 9, 0}, 0);
    const auto world = CreateBattle(scenario);
    const auto shot = Shot(world);
    REQUIRE(shot);
    CHECK(shot->target == front);
}

TEST_CASE("combat.tactical_archer_chooses_clear_lane_past_friendly_formation") {
    auto scenario = TacticalBattle();
    AddFighter(scenario, 0, {}, 0, true);
    AddFighter(scenario, 0, {3, 0, 0}, 0);
    AddFighter(scenario, 1, {8, 0, 0}, Pi);
    auto open = AddFighter(scenario, 1, {8, 3, 0}, Pi);
    const auto world = CreateBattle(scenario);
    const auto shot = Shot(world);
    REQUIRE(shot);
    CHECK(shot->target == open);
}

TEST_CASE("combat.tactical_bow_arcs_over_its_own_front_rank_at_midrange") {
    auto scenario = TacticalBattle();
    const auto front = AddFighter(scenario, 0, {1, 0, 0}, 0);
    const auto archer = AddFighter(scenario, 0, {1, 0, 0}, 0, true);
    const auto ally = AddFighter(scenario, 0, {7, 2, 0}, 0);
    const auto enemy = AddFighter(scenario, 1, {14, 0, 0}, Pi);
    scenario.units[1].regiment = scenario.units[0].regiment;
    scenario.regiments.erase(scenario.regiments.begin() + 1);
    auto world = CreateBattle(scenario);
    world.seed = 10;

    const auto arced = Shot(world, archer);
    REQUIRE(arced);
    CHECK(arced->target == enemy);
    CHECK(arced->high_arc);
    CHECK_FALSE(arced->hit);

    FindUnit(world, front)->pose.position.y = 1.f;
    const auto direct = Shot(world, archer);
    REQUIRE(direct);
    CHECK_FALSE(direct->high_arc);
    CHECK(direct->hit);
    FindUnit(world, front)->pose.position.y = 0.f;

    FindUnit(world, enemy)->pose.position.x = 6.f;
    CHECK_FALSE(Shot(world, archer));
    FindUnit(world, enemy)->pose.position.x = 14.f;

    FindUnit(world, ally)->pose.position.y = 0.f;
    CHECK_FALSE(Shot(world, archer));
    FindUnit(world, ally)->pose.position.y = 2.f;

    world.scenario.obstacles.push_back({4, -1, 5, 1, true});
    CHECK_FALSE(Shot(world, archer));
}

TEST_CASE("combat.tactical_controlled_archer_respects_cover_and_friendly_line") {
    auto scenario = TacticalBattle();
    const auto archer = AddFighter(scenario, 0, {}, 0, true);
    const auto blocker = AddFighter(scenario, 0, {2, 0, 0}, 0);
    const auto enemy = AddFighter(scenario, 1, {8, 0, 0}, Pi);
    scenario.obstacles.push_back({4.f, -1.f, 5.f, 1.f, true});
    auto world = CreateBattle(scenario);
    auto &unit = *FindUnit(world, archer);
    unit.attack_target = enemy;
    unit.control = ControlMode::PlayerControlled;

    CHECK_FALSE(Shot(world));
    world.scenario.obstacles.front().blocks_projectiles = false;
    CHECK_FALSE(Shot(world));
    FindUnit(world, blocker)->pose.position.y = 2.f;
    CHECK(Shot(world));
}

TEST_CASE("combat.tactical_archer_prioritizes_ordered_regiment_in_range") {
    auto scenario = TacticalBattle();
    AddFighter(scenario, 0, {}, 0, true);
    AddFighter(scenario, 1, {6, -2, 0}, Pi);
    const auto ordered = AddFighter(scenario, 1, {9, 2, 0}, Pi);
    auto world = CreateBattle(scenario);
    world.regiments[0].order = Order{};
    world.regiments[0].order->type = OrderType::Attack;
    world.regiments[0].order->status = OrderStatus::Active;
    world.regiments[0].order->target_regiment = {3};
    const auto shot = Shot(world);
    REQUIRE(shot);
    CHECK(shot->target == ordered);
}

TEST_CASE("combat.tactical_bows_hold_fire_in_both_engaged_formations") {
    auto scenario = TacticalBattle();
    AddFighter(scenario, 0, {}, 0, true);
    AddFighter(scenario, 1, {8, 0, 0}, Pi);
    const auto ally = AddFighter(scenario, 0, {8, 1, 0}, 0);
    auto world = CreateBattle(scenario);
    CHECK_FALSE(Shot(world));
    FindUnit(world, ally)->pose.position = {0, 5, 0};
    CHECK(Shot(world));
    FindUnit(world, {2})->pose.position.x = 1;
    CHECK_FALSE(Shot(world));
}

TEST_CASE("combat.tactical_risky_volley_can_hit_a_friendly_fighter") {
    auto scenario = TacticalBattle();
    scenario.rules.local_melee = true;
    scenario.rules.risky_volley = true;
    const auto archer = AddFighter(scenario, 0, {}, 0, true);
    const auto ally = AddFighter(scenario, 0, {7, 0, 0}, 0);
    const auto enemy = AddFighter(scenario, 1, {8, 0, 0}, Pi);
    auto world = CreateBattle(scenario);
    const auto shot = Shot(world);
    REQUIRE(shot);
    REQUIRE(shot->hit);
    CommitCombatEvents(world, std::span(&*shot, 1));
    CHECK(FindUnit(world, ally)->health < 10.f);
    CHECK(FindUnit(world, enemy)->health == 10.f);
    CHECK(std::any_of(world.events.begin(), world.events.end(), [&](const auto &event) {
        return event.kind == EventKind::Hit && event.source == archer && event.target == ally &&
               event.projectile && event.cause == shot->id;
    }));
}

TEST_CASE("combat.tactical_skirmishers_can_shoot_at_other_free_skirmishers") {
    auto scenario = TacticalBattle();
    scenario.rules.local_melee = true;
    AddFighter(scenario, 0, {}, 0, true);
    auto enemy = AddFighter(scenario, 1, {8, 0, 0}, Pi, true);
    auto world = CreateBattle(scenario);
    FindUnit(world, enemy)->skirmisher = true;
    CHECK_FALSE(Shot(world));
    world.units.front().skirmisher = true;
    const auto shot = Shot(world);
    REQUIRE(shot);
    CHECK(shot->target == enemy);
}

TEST_CASE("combat.tactical_scattered_skirmisher_is_harder_to_hit") {
    auto scenario = TacticalBattle();
    scenario.rules.local_melee = true;
    const auto enemy = AddFighter(scenario, 1, {14, 0, 0}, Pi, true);
    const auto archer = AddFighter(scenario, 0, {}, 0, true);
    auto world = CreateBattle(scenario);
    world.seed = 10;
    FindUnit(world, archer)->skirmisher = true;
    const auto formed = Shot(world, archer);
    REQUIRE(formed);
    CHECK(formed->hit);

    FindUnit(world, enemy)->skirmisher = true;
    const auto loose = Shot(world, archer);
    REQUIRE(loose);
    CHECK_FALSE(loose->hit);
}

TEST_CASE("combat.tactical_march_column_must_deploy_before_shooting") {
    auto scenario = TacticalBattle();
    AddFighter(scenario, 0, {}, 0, true);
    AddFighter(scenario, 1, {8, 0, 0}, Pi);
    scenario.regiments[0].deployment_ticks = 4;
    auto world = CreateBattle(scenario);
    world.tick = 3;
    CHECK_FALSE(Shot(world));
    world.tick = 4;
    CHECK(Shot(world));
}

TEST_CASE("combat.tactical_march_column_must_deploy_before_striking") {
    auto scenario = TacticalBattle();
    const auto deploying = AddFighter(scenario, 0, {}, 0);
    const auto ready = AddFighter(scenario, 1, {1, 0, 0}, Pi);
    scenario.regiments[0].deployment_ticks = 4;
    auto world = CreateBattle(scenario);
    std::vector<BattleEvent> strikes;
    world.tick = 3;
    BuildCombatEvents(world, strikes);
    REQUIRE(strikes.size() == 1);
    CHECK(strikes.front().source == ready);
    world.tick = 4;
    BuildCombatEvents(world, strikes);
    REQUIRE(strikes.size() == 2);
    CHECK(strikes.front().source == deploying);
}

TEST_CASE("combat.linear_muskets_use_the_front_rank_even_when_the_rear_has_a_clear_lane") {
    auto scenario = TacticalBattle();
    const auto front = AddFighter(scenario, 0, {}, 0, true);
    const auto rear = AddFighter(scenario, 0, {}, 0, true);
    scenario.regiments.pop_back();
    scenario.units.back().regiment = {1};
    for (auto &unit : scenario.units)
        unit.ranged_weapon = RangedWeapon::Musket;
    AddFighter(scenario, 1, {5, 4, 0}, Pi);

    const auto legacy = CreateBattle(scenario);
    REQUIRE(Shot(legacy, rear)); // The diagonal clears the first rank.
    scenario.rules.linear_infantry = true;
    const auto line = CreateBattle(scenario);
    CHECK(Shot(line, front));
    CHECK_FALSE(Shot(line, rear));
}

TEST_CASE("combat.waiting_for_orders_preserves_an_assault_column") {
    auto scenario = test::Duel(12);
    scenario.rules.regiment_tactics = scenario.rules.linear_infantry = true;
    scenario.regiments[0].formation = {3, 1.f, .25f};
    scenario.regiments[0].pose.position.x = -100.f;
    scenario.regiments[1].pose.position.x = 100.f;
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        if (unit.regiment.value == 1 && unit.id.value <= 4) {
            unit.archer = true;
            unit.ranged_weapon = RangedWeapon::Musket;
            unit.ammunition = 30;
        }
    }
    auto world = CreateBattle(scenario);
    const FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments.front().formation.columns == 3);
}

TEST_CASE("combat.linear_officers_lead_a_bayonet_column_into_a_firing_line") {
    auto scenario = test::Duel(12);
    scenario.rules.local_melee = scenario.rules.regiment_tactics = true;
    scenario.rules.linear_infantry = true;
    for (auto &regiment : scenario.regiments) {
        regiment.formation = {4, 1.f, .25f};
        regiment.pose.position.x *= 1.5f;
    }
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.health = 100.f;
        if (unit.regiment.value == 2 || unit.id.value <= 4) {
            unit.archer = true;
            unit.ranged_weapon = RangedWeapon::Musket;
            unit.ammunition = 30;
            unit.weapon.range = 60.f;
        }
    }
    auto world = CreateBattle(scenario);
    for (auto &regiment : world.regiments) {
        Order order;
        order.recipient = regiment.id;
        order.status = OrderStatus::Active;
        order.type = regiment.team == 0 ? OrderType::Attack : OrderType::HoldLine;
        order.target_regiment = regiment.team == 0 ? RegimentId{2} : RegimentId{};
        order.point = regiment.pose.position;
        regiment.order = order;
    }
    FlatNavigationService navigation;
    unsigned defensive_shots = 0;
    bool bayonet_contact = false;
    for (int i = 0; i < 500 && !bayonet_contact; ++i) {
        auto result = Step(world, {}, {}, navigation);
        REQUIRE(result);
        for (const auto &event : result.value.events) {
            if (event.kind != EventKind::AttackLaunched)
                continue;
            if (event.regiment.value == 1 && event.projectile)
                FAIL("The assault column fired before contact");
            defensive_shots += event.regiment.value == 2 && event.projectile;
            bayonet_contact |= event.regiment.value == 1 && !event.projectile;
        }
    }
    CHECK(defensive_shots > 0);
    CHECK(bayonet_contact);
    CHECK(world.regiments[0].formation.columns < 4);
    CHECK(world.regiments[1].formation.columns > 4);
}

TEST_CASE("combat.tactical_disordered_line_loses_attack_and_guard_strength") {
    auto scenario = TacticalBattle();
    AddFighter(scenario, 0, {}, 0);
    AddFighter(scenario, 1, {1, 0, 0}, Pi);
    scenario.units[0].armour = scenario.units[1].armour = .5f;
    auto world = CreateBattle(scenario);
    std::vector<BattleEvent> strikes;
    BuildCombatEvents(world, strikes);
    REQUIRE(strikes.size() == 2);
    const float organized_damage = strikes.front().amount;
    CHECK(strikes.back().amount == doctest::Approx(organized_damage));

    world.regiments[0].disordered = true;
    world.regiments[0].cohesion = .3f;
    BuildCombatEvents(world, strikes);
    REQUIRE(strikes.size() == 2);
    CHECK(strikes.front().amount < organized_damage);
    CHECK(strikes.back().amount > organized_damage);
}

TEST_CASE("combat.tactical_melee_damage_depends_on_defender_facing_at_impact") {
    auto scenario = TacticalBattle();
    AddFighter(scenario, 0, {0, 1, 0}, 0);
    const auto defender = AddFighter(scenario, 1, {}, Pi / 2);
    scenario.units[1].armour = .6f;
    auto world = CreateBattle(scenario);
    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    REQUIRE(launches.size() == 2);
    REQUIRE(launches.front().target == defender);

    auto resolve = [&](float yaw) {
        auto state = world;
        FindRegiment(state, {2})->pose.yaw = yaw;
        CommitCombatEvents(state, std::span(launches.data(), 1));
        ++state.tick;
        CommitCombatEvents(state, {});
        return state.events.back();
    };
    const auto front = resolve(Pi / 2);
    const auto flank = resolve(0);
    const auto rear = resolve(-Pi / 2);
    REQUIRE(front.kind == EventKind::Hit);
    REQUIRE(flank.kind == EventKind::Hit);
    REQUIRE(rear.kind == EventKind::Hit);
    CHECK(front.amount < flank.amount);
    CHECK(flank.amount < rear.amount);
    CHECK(front.aspect == AttackAspect::Front);
    CHECK(flank.aspect == AttackAspect::Flank);
    CHECK(rear.aspect == AttackAspect::Rear);
    CHECK(front.reason.find("front") != std::string::npos);
    CHECK(flank.reason.find("flank") != std::string::npos);
    CHECK(rear.reason.find("rear") != std::string::npos);
}
