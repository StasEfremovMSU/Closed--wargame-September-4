#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Snapshot.h"
#include "../src/FieldCombat.h"
#include "../src/RegimentDiscipline.h"
#include "battle/command/Services.h"
#include "doctest.h"
#include <limits>
#include <tuple>

using namespace battle;

namespace {
Scenario ImpactScenario(Vec3 attacker) {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose = {};
    scenario.regiments[1].pose = {attacker, std::atan2(-attacker.y, -attacker.x)};
    for (size_t i = 0; i < scenario.units.size(); ++i)
        scenario.units[i].pose = scenario.regiments[i].pose;
    scenario.units[0].health = 100.f;
    scenario.units[0].weapon.damage = 0.f;
    scenario.units[1].weapon = {15.f, 1.5f, 1, 1};
    return scenario;
}
} // namespace

TEST_CASE("combat.rear_shock_can_break_an_intact_regiment") {
    auto front = CreateWorld(ImpactScenario({1, 0, 0}), 42);
    auto flank = CreateWorld(ImpactScenario({0, 1, 0}), 42);
    auto rear = CreateWorld(ImpactScenario({-1, 0, 0}), 42);
    REQUIRE(front);
    REQUIRE(flank);
    REQUIRE(rear);
    GridNavigationService nav({}, 1.f, 25, 25, {-12, -12, 0});
    for (unsigned i = 0; i < 5; ++i) {
        REQUIRE(Step(front.value, {}, {}, nav));
        REQUIRE(Step(flank.value, {}, {}, nav));
        REQUIRE(Step(rear.value, {}, {}, nav));
    }
    CHECK(front.value.units[0].alive);
    CHECK(rear.value.units[0].alive);
    CHECK_FALSE(front.value.regiments[0].panic);
    CHECK(rear.value.regiments[0].panic);
    CHECK(front.value.regiments[0].morale > flank.value.regiments[0].morale);
    CHECK(flank.value.regiments[0].morale > rear.value.regiments[0].morale);
}

TEST_CASE("combat.only_a_resolved_in_reach_melee_strike_creates_directional_pressure") {
    const FlatNavigationService navigation;
    for (const Vec3 position : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{-1, 0, 0}}) {
        auto scenario = ImpactScenario(position);
        scenario.units[1].weapon.windup_ticks = 8;
        auto created = CreateWorld(scenario, 42);
        REQUIRE(created);
        auto &world = created.value;
        auto &defender = world.regiments[0];
        auto &attacker = *FindUnit(world, {2});
        world.regiments[1].ai_target = defender.id; // All approaches have the same assigned target.
        defender.decision_tick = world.regiments[1].decision_tick = 100;

        REQUIRE(Step(world, {}, {}, navigation));
        const auto launched = std::ranges::find_if(world.events, [](const BattleEvent &event) {
            return event.kind == EventKind::AttackLaunched && event.source == UnitId{2};
        });
        REQUIRE(launched != world.events.end());
        const auto due = launched->due_tick;
        CHECK(defender.melee_attack_aspects == 0);
        CHECK(defender.morale == 1.f);
        const auto expected = position.x == 1.f ? AttackAspect::Front :
                              position.x == -1.f ? AttackAspect::Rear : AttackAspect::Flank;
        auto cut_off = world;
        if (position.x == -1.f)
            cut_off.scenario.obstacles.push_back({-.70f, -2.f, -.40f, 2.f});
        else
            FindUnit(cut_off, {2})->pose.position = {20, 0, 0};
        if (position.x != -1.f) {
            cut_off.regiments[1].pose.position = {20, 0, 0};
            cut_off.regiments[1].slots.front().position = {20, 0, 0};
        }
        while (world.tick < due) {
            REQUIRE(Step(world, {}, {}, navigation));
            REQUIRE(Step(cut_off, {}, {}, navigation));
        }
        const auto landed = std::ranges::find_if(world.events, [](const BattleEvent &event) {
            return event.kind == EventKind::Hit && event.source == UnitId{2};
        });
        REQUIRE(landed != world.events.end());
        CHECK(landed->aspect == expected);
        CHECK(Distance(attacker.pose.position, world.units.front().pose.position) <= attacker.weapon.range);
        CHECK(defender.morale < 1.f);
        CHECK(std::ranges::none_of(cut_off.events, [](const BattleEvent &event) {
            return event.kind == EventKind::Hit && event.source == UnitId{2};
        }));
        CHECK(cut_off.regiments.front().melee_attack_aspects == 0);
        CHECK(cut_off.regiments.front().morale == 1.f);
    }
}

TEST_CASE("combat.rally_order_recovers_tactical_regiment_without_commander") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.rules.calm_ticks = 4;
    scenario.rules.rally_safe_radius_m = 5.f;
    scenario.regiments[0].rally_point = scenario.regiments[0].pose.position;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments[0];
    regiment.panic = true;
    Order rally;
    rally.recipient = regiment.id;
    rally.type = OrderType::Rally;
    rally.point = regiment.rally_point;
    REQUIRE(SubmitOrder(world, rally) == Status::Ok);
    GridNavigationService nav({}, 1.f, 49, 31, {-24, -15, 0});
    CHECK(Step(world, {}, {}, nav));
    CHECK(regiment.panic);
    for (unsigned i = 0; i < 12; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK_FALSE(regiment.panic);
    REQUIRE(regiment.order);
    CHECK(regiment.order->status == OrderStatus::Completed);
}

TEST_CASE("combat.retreat_disorders_nearby_allies_until_they_reform") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    auto ally = scenario.regiments[0];
    ally.id = {3};
    ally.pose.position.x -= .75f;
    scenario.regiments.push_back(ally);
    auto soldier = scenario.units[0];
    soldier.id = {3};
    soldier.regiment = ally.id;
    soldier.pose = ally.pose;
    scenario.units.push_back(soldier);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &retreating = world.regiments[0];
    auto &disrupted = world.regiments[2];
    retreating.panic = true;
    GridNavigationService nav({}, 1.f, 49, 31, {-24, -15, 0});
    for (unsigned i = 0; i < 30; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(disrupted.disordered);
    CHECK_FALSE(disrupted.panic);
    CHECK_FALSE(world.regiments[1].disordered);

    retreating.panic = false;
    retreating.ai_action = OrderType::Hold;
    for (unsigned i = 0; i < 100; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK_FALSE(disrupted.disordered);
}

TEST_CASE("world.checkpoint_validates_and_hashes_regiment_discipline") {
    auto scenario = test::Duel(2);
    scenario.rules.regiment_tactics = true;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto checkpoint = Capture(world);
    checkpoint.state.regiments[0].morale = .65f;
    checkpoint.state.regiments[0].cohesion = .4f;
    checkpoint.state.regiments[0].disordered = true;
    auto restored = Restore(checkpoint);
    REQUIRE(restored);
    CHECK(StateHash(restored.value) != StateHash(world));
    CHECK(StateHash(restored.value) == StateHash(checkpoint.state));

    checkpoint.state.regiments[0].morale = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(Restore(checkpoint));
    checkpoint.state.regiments[0].morale = .65f;
    checkpoint.state.regiments[0].cohesion = 1.1f;
    CHECK_FALSE(Restore(checkpoint));
}

TEST_CASE("world.tactical_deployment_needs_bounded_time_and_fatigue") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    auto &regiment = scenario.regiments[0];
    regiment.deployment_ticks = 12;
    regiment.initial_fatigue = .45f;
    auto valid = CreateWorld(scenario, 42);
    REQUIRE(valid);
    CHECK(valid.value.regiments[0].fatigue == doctest::Approx(.45f));

    regiment.initial_fatigue = -.05f;
    CHECK_FALSE(CreateWorld(scenario, 42));
    regiment.initial_fatigue = .45f;
    regiment.deployment_ticks = 1201;
    CHECK_FALSE(CreateWorld(scenario, 42));
}

TEST_CASE("combat.a_broken_regiment_withdraws_then_reforms_at_rally") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.rules.calm_ticks = 8;
    scenario.rules.rally_tolerance_m = 1.f;
    scenario.rules.rally_safe_radius_m = 10.f;
    scenario.regiments[0].rally_point = {-14, 0, 0};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments[0];
    regiment.panic = true;
    regiment.morale = .1f;
    regiment.cohesion = .3f;
    regiment.disordered = true;
    Order rally;
    rally.recipient = regiment.id;
    rally.type = OrderType::Rally;
    rally.point = regiment.rally_point;
    REQUIRE(SubmitOrder(world, rally) == Status::Ok);

    GridNavigationService nav({}, 1.f, 49, 31, {-24, -15, 0});
    for (unsigned i = 0; i < 300; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(Distance(regiment.pose.position, regiment.rally_point) <= 1.f);
    CHECK_FALSE(regiment.panic);
    CHECK_FALSE(regiment.disordered);
    REQUIRE(regiment.order);
    CHECK(regiment.order->status == OrderStatus::Completed);
}

TEST_CASE("combat.banner_and_musician_improve_reform_after_disorder") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.officers = {{{1}, {1}, 2, 1.f, 0, 0, 12.f},
                         {{2}, {2}, 3, 1.f, 0, 0, 12.f},
                         {{3}, {3}, 4, 0, 0, 1.f, 12.f}};
    auto &hierarchy = scenario.command_setup.emplace().hierarchy;
    command::CommandNode group;
    group.id = {1};
    group.level = command::Level::Group;
    group.children = {{2}};
    command::CommandNode leaf;
    leaf.id = {2};
    leaf.parent = group.id;
    leaf.commander = {1};
    leaf.regiment = {1};
    hierarchy.nodes = {group, leaf};
    command::OfficerProfile profile;
    profile.officer = leaf.commander;
    hierarchy.officers = {profile};
    auto unsupported = CreateWorld(scenario, 42);
    hierarchy.support = {{{2}, leaf.id, command::SupportDuty::StandardBearer},
                         {{3}, leaf.id, command::SupportDuty::Musician}};
    auto supported = CreateWorld(scenario, 42);
    REQUIRE(unsupported);
    REQUIRE(supported);

    for (auto *world : {&unsupported.value, &supported.value}) {
        auto &regiment = world->regiments[0];
        regiment.morale = .45f;
        regiment.cohesion = .45f;
        regiment.disordered = true;
        FlatNavigationService nav;
        for (unsigned i = 0; i < 36; ++i)
            REQUIRE(Step(*world, {}, {}, nav));
    }
    const auto &assisted = supported.value.regiments[0];
    const auto &unaided = unsupported.value.regiments[0];
    CHECK(assisted.morale > unaided.morale);
    CHECK_FALSE(assisted.disordered);
    CHECK(unaided.disordered);
}

TEST_CASE("world.command_perception_weights_discipline_by_living_troops") {
    auto scenario = test::Duel(3);
    auto ally = scenario.regiments[0];
    ally.id = {3};
    ally.formation.columns = 1;
    scenario.regiments.push_back(ally);
    auto soldier = scenario.units[0];
    soldier.id = {7};
    soldier.regiment = ally.id;
    scenario.units.push_back(soldier);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);

    command::CommandNode group;
    group.id = {1};
    group.level = command::Level::Group;
    group.children = {{2}, {3}};
    command::CommandNode first;
    first.id = {2};
    first.parent = group.id;
    first.regiment = {1};
    auto second = first;
    second.id = {3};
    second.regiment = {3};
    command::CommandHierarchy hierarchy;
    hierarchy.nodes = {group, first, second};

    auto &world = created.value;
    const auto observe = [&] { return command::BuildSituation(world, hierarchy, group.id, 0); };
    auto legacy = observe();
    REQUIRE(legacy.value);
    CHECK_FALSE(legacy.value->own.morale);
    CHECK_FALSE(legacy.value->own.cohesion);
    CHECK_FALSE(legacy.value->own.fatigue);

    world.scenario.rules.regiment_tactics = true;
    world.regiments[0].morale = .8f;
    world.regiments[0].cohesion = .6f;
    world.regiments[0].fatigue = .1f;
    world.regiments[2].morale = .2f;
    world.regiments[2].cohesion = .2f;
    world.regiments[2].fatigue = .9f;
    auto observed = observe();
    REQUIRE(observed.value);
    const auto &own = observed.value->own;
    CHECK(own.living == 4);
    CHECK(own.morale.value_or(-1.f) == doctest::Approx(.65f));
    CHECK(own.cohesion.value_or(-1.f) == doctest::Approx(.5f));
    CHECK(own.fatigue.value_or(-1.f) == doctest::Approx(.3f));

    world.regiments[0].units.clear();
    world.regiments[2].units.clear();
    auto wiped = observe();
    REQUIRE(wiped.value);
    CHECK_FALSE(wiped.value->own.morale);
    CHECK_FALSE(wiped.value->own.cohesion);
    CHECK_FALSE(wiped.value->own.fatigue);
}


namespace {
World LossWindow(unsigned strength, bool artillery) {
    auto scenario = test::Duel(strength);
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    const unsigned files = std::min(20u, strength);
    const unsigned ranks = (strength + files - 1) / files;
    for (auto &regiment : scenario.regiments) {
        regiment.formation = {files, 1.1f, .25f};
        const float depth = float(ranks - 1) * .55f + .45f;
        regiment.pose.position.x = regiment.team ? depth : -depth;
    }
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.health = 10.f;
        unit.weapon.damage = 20.f;
    }
    if (artillery)
        for (unsigned i = 0; i < 5; ++i) {
            auto &gun = scenario.units[strength + files - i - 1];
            gun.platform = BattlePlatform::FieldGun;
            gun.archer = true;
        }
    auto made = CreateWorld(scenario, 42);
    REQUIRE(made);
    auto world = std::move(made.value);
    world.tick = 39;
    for (auto &regiment : world.regiments) {
        regiment.discipline_window_count = regiment.units.size();
        regiment.discipline_window_until = 40;
    }
    for (unsigned i = 0; i < 5; ++i) {
        const EventId attack{world.next_event_id++};
        world.last_launch_id = attack.value;
        const UnitId source{strength + files - i};
        const UnitId target{i + 1};
        world.attacks.push_back({attack, source, target, 40, 20.f, true, 1.5f, artillery});
    }
    CommitCombatEvents(world, {});
    REQUIRE(world.regiments.front().units.size() == strength - 5);
    UpdateRegimentDiscipline(world);
    return world;
}
} // namespace

TEST_CASE("combat.five_losses_have_different_effects_on_a_battalion_and_a_depleted_detachment") {
    const auto battalion = LossWindow(120, false);
    const auto detachment = LossWindow(12, false);
    CHECK_FALSE(battalion.regiments.front().panic);
    CHECK(battalion.regiments.front().morale > .9f);
    CHECK(detachment.regiments.front().panic);
}

TEST_CASE("combat.a_few_artillery_losses_suppress_a_battalion_without_routing_it") {
    const auto battalion = LossWindow(120, true);
    const auto &regiment = battalion.regiments.front();
    CHECK_FALSE(regiment.panic);
    CHECK(regiment.artillery_pressure > .8f);
    CHECK(regiment.cohesion < .8f);
    CHECK(regiment.morale > .9f);
}

TEST_CASE("combat.a_staggered_cavalry_front_spends_momentum_once_per_file") {
    auto problem = test::Duel(2);
    problem.rules.regiment_tactics = true;
    problem.rules.local_melee = true;
    problem.regiments[0].pose.position.x = -1.f;
    problem.regiments[1].pose.position.x = .25f;
    for (size_t i = 0; i < problem.units.size(); ++i) {
        auto &unit = problem.units[i];
        unit.pose = problem.regiments[unit.regiment.value - 1].pose;
        unit.health = 100.f;
        unit.weapon = {i < 2 ? 12.f : 0.f, 1.5f, 1, 1};
        if (i < 2) {
            unit.troop = UnitTroop::Mounted;
            unit.cavalry = CavalryClass::Heavy;
        } else {
            unit.troop = UnitTroop::Spearman;
        }
    }
    auto made = CreateWorld(problem, 42);
    REQUIRE(made);
    auto base = std::move(made.value);
    auto resolve = [&](bool brace, bool runup) {
        auto world = base;
        auto &riders = world.regiments[0];
        auto &defenders = world.regiments[1];
        defenders.braced = brace;
        riders.cavalry_phase = CavalryPhase::Approach;
        riders.cavalry_target = defenders.id;
        riders.charge_runup_m = runup ? 6.f : 1.f;
        FindUnit(world, {2})->pose.position.x -= 3.f;
        float second_file_first_hit = 0.f;
        for (Tick tick = 0; tick < 3; ++tick) {
            world.tick = tick;
            world.events.clear();
            if (tick == 1)
                FindUnit(world, {2})->pose.position.x = -1.f;
            std::vector<BattleEvent> attacks;
            BuildCombatEvents(world, attacks);
            CommitCombatEvents(world, attacks);
            UpdateRegimentDiscipline(world);
            if (tick == 1)
                for (const auto &event : world.events)
                    if (event.kind == EventKind::Hit && event.source == UnitId{2})
                        second_file_first_hit = event.amount;
            const auto count = [&](UnitId id) {
                return std::ranges::count_if(world.events, [&](const auto &event) {
                    return event.kind == EventKind::CavalryImpact && event.source == id;
                });
            };
            CHECK(count({1}) == (runup && tick == 0 ? 1 : 0));
            CHECK(count({2}) == (runup && tick == 1 ? 1 : 0));
        }
        return std::tuple{defenders.cohesion, defenders.morale, second_file_first_hit};
    };
    const auto [charged_cohesion, charged_morale, charged_hit] = resolve(false, true);
    const auto [blocked_cohesion, blocked_morale, blocked_hit] = resolve(true, true);
    const auto [walking_cohesion, walking_morale, walking_hit] = resolve(false, false);
    CHECK(charged_cohesion < walking_cohesion - .1f);
    CHECK(charged_morale < walking_morale - .05f);
    CHECK(charged_hit > walking_hit + 8.f);
    CHECK(blocked_hit < charged_hit);
    CHECK(blocked_cohesion > charged_cohesion);
    CHECK(blocked_morale > charged_morale);
}


TEST_CASE("combat.a_parry_cannot_rearm_a_completed_cavalry_impact") {
    auto scenario = test::Duel();
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose.position.x = -1.f;
    scenario.regiments[1].pose.position.x = .25f;
    for (size_t i = 0; i < scenario.units.size(); ++i) {
        scenario.units[i].pose = scenario.regiments[i].pose;
        scenario.units[i].health = 100.f;
        scenario.units[i].weapon.windup_ticks = 1;
        scenario.units[i].weapon.cooldown_ticks = 20;
    }
    scenario.units[0].troop = UnitTroop::Mounted;
    scenario.units[0].cavalry = CavalryClass::Heavy;
    auto made = CreateWorld(scenario, 42);
    REQUIRE(made);
    auto &world = made.value;
    auto &regiment = world.regiments.front();
    auto &rider = world.units.front();
    const auto &enemy = world.units.back();
    regiment.cavalry_phase = CavalryPhase::Approach;
    regiment.cavalry_target = enemy.regiment;
    regiment.charge_runup_m = 6.f;
    REQUIRE(AcquireControl(world, rider.id) == Status::Ok);
    rider.attack_target = enemy.id;
    REQUIRE(ChargeReady(world, regiment, rider, enemy));
    std::vector<BattleEvent> attacks;
    BuildCombatEvents(world, attacks);
    std::erase_if(attacks, [&](const auto &attack) { return attack.source != rider.id; });
    REQUIRE(attacks.size() == 1);
    CommitCombatEvents(world, attacks);
    REQUIRE(regiment.cavalry_phase == CavalryPhase::Impact);

    world.tick = 1;
    world.events.clear();
    Command parry;
    parry.kind = CommandKind::ParryControlledUnit;
    parry.unit = rider.id;
    REQUIRE(ApplyControlCommand(world, parry) == Status::Ok);
    const EventId strike{world.next_event_id++};
    world.last_launch_id = strike.value;
    world.attacks.push_back({strike, enemy.id, rider.id, 2, 8.f, true, 1.5f, false});
    CommitCombatEvents(world, {});
    REQUIRE(std::ranges::any_of(world.events, [](const auto &event) {
        return event.kind == EventKind::Parried;
    }));
    world.tick = 2;
    REQUIRE(rider.ready_tick == world.tick);
    CHECK_FALSE(ChargeReady(world, regiment, rider, enemy));

    REQUIRE(ReleaseControl(world, rider.id) == Status::Ok);
    // Simulate a rider that has already rejoined their original formation position.
    rider.control = ControlMode::FormationControlled;
    CHECK_FALSE(ChargeReady(world, regiment, rider, enemy));
}

TEST_CASE("combat.cavalry_cannot_bank_a_runup_through_an_in_place_wheel") {
    auto scenario = test::Duel(9);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    for (size_t i = 0; i < 9; ++i) {
        scenario.units[i].troop = UnitTroop::Mounted;
        scenario.units[i].cavalry = CavalryClass::Heavy;
    }
    auto made = CreateWorld(scenario, 42);
    REQUIRE(made);
    auto &world = made.value;
    auto &riders = world.regiments.front();
    riders.cavalry_phase = CavalryPhase::Approach;
    riders.cavalry_target = world.regiments.back().id;
    riders.charge_runup_m = 8.f;
    const auto start = riders.pose;
    riders.route = {{start.position, Pi * .5f}};
    AdvanceFormations(world, .05f);
    REQUIRE(riders.pose.yaw > 0.f);
    REQUIRE(riders.pose.yaw < .05f);
    REQUIRE(riders.pose.position == start.position);
    CHECK(riders.charge_runup_m == 0.f);
}
