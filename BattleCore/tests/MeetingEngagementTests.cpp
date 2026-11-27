#include "TestSupport.h"
#include "battle/command/Runtime.h"
#include "doctest.h"

using namespace battle;
namespace c = battle::command;

namespace {
Scenario MeetingArmies(float west, float east) {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = scenario.rules.local_melee = true;
    scenario.rules.meeting_engagement = true;
    scenario.regiments[0].pose.position.x = west;
    scenario.regiments[1].pose.position.x = east;
    auto &setup = scenario.command_setup.emplace();
    for (unsigned side = 0; side < 2; ++side) {
        const uint64_t offset = side * 3;
        for (unsigned level = 0; level < 3; ++level) {
            const uint64_t id = offset + level + 1;
            auto &unit = scenario.units[id - 1];
            unit.pose = scenario.regiments[side].pose;
            unit.health = 100.f;
            unit.weapon.damage = 0.f;
            scenario.officers.push_back({{id}, unit.id, level == 2 ? 2u : 1u});
            c::OfficerProfile profile;
            profile.officer = {id};
            profile.command_radius_m = 30.f;
            setup.hierarchy.officers.push_back(profile);
            c::CommandNode node;
            node.id = {id};
            node.team = int32_t(side);
            node.commander = {id};
            node.level = level == 0 ? c::Level::Army : level == 1 ? c::Level::Group : c::Level::Regiment;
            if (level)
                node.parent = {id - 1};
            if (level < 2)
                node.children = {{id + 1}};
            else
                node.regiment = {side + 1};
            setup.hierarchy.nodes.push_back(node);
        }
    }
    return scenario;
}

c::CommandTask Search(c::NodeId army, float checkpoint) {
    c::CommandTask mission;
    mission.recipient = army;
    mission.action = c::TaskKind::Attack;
    mission.intent.objective_position = {checkpoint, 0, 0};
    mission.authority.initiative = c::InitiativePolicy::WithinIntent;
    mission.authority.may_change_target = mission.authority.may_pursue = true;
    return mission;
}
}

TEST_CASE("runtime.meeting_armies_choose_one_attacker_until_contact") {
    auto scenario = MeetingArmies(-24.f, 24.f);
    SUBCASE("armies with no assigned mission and headquarters behind the defending line") {
        auto &general = scenario.units[3];
        general.regiment = {};
        general.independent_team = 1;
        general.pose.position.x += 8.f;
    }
    SUBCASE("symmetric automatic search missions") {
        scenario.command_setup->missions = {Search({4}, -12.f), Search({1}, 12.f)};
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    for (unsigned i = 0; i < 110; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(world.regiments[0].pose.position.x > -22.f);
    CHECK(world.regiments[1].pose.position.x == doctest::Approx(24.f));
    bool contact = false;
    for (unsigned i = 0; i < 580 && !contact; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        contact = world.regiments[0].melee_engaged;
    }
    CHECK(contact);
}

TEST_CASE("runtime.meeting_attacker_probes_past_a_checkpoint_without_contact") {
    auto scenario = MeetingArmies(-20.f, 65.f);
    auto hold = Search({4}, 65.f);
    hold.action = c::TaskKind::Defend;
    scenario.command_setup->missions = {Search({1}, 0.f), hold};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    bool contact = false;
    for (unsigned i = 0; i < 1060 && !contact; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        contact = world.regiments[0].melee_engaged;
    }
    CHECK(world.regiments[0].pose.position.x > 45.f);
    CHECK(world.regiments[1].pose.position.x == doctest::Approx(65.f));
    CHECK(contact);
}

TEST_CASE("runtime.meeting_initiative_yields_to_player_army_orders") {
    auto created = CreateWorld(MeetingArmies(-24.f, 24.f), 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    auto hold = Search({1}, -24.f);
    hold.action = c::TaskKind::HoldArea;
    hold.author = {1};
    hold.priority = PlayerOrderPriority;
    const auto submitted = c::SubmitTask(world, hold);
    REQUIRE(submitted.value);
    for (unsigned i = 0; i < 150; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(world.command_state->nodes.front().memory.active_task == *submitted.value);
    CHECK(world.regiments[0].pose.position.x == doctest::Approx(-24.f));
    CHECK(world.regiments[1].pose.position.x == doctest::Approx(24.f));
}

TEST_CASE("runtime.meeting_attacker_replaces_a_probe_route_blocked_after_dispatch") {
    auto scenario = MeetingArmies(-25.f, 60.f);
    auto hold = Search({4}, 60.f);
    hold.action = c::TaskKind::Defend;
    scenario.command_setup->missions = {Search({1}, 0.f), hold};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService open({}, 1.f, 121, 43, {-40, -21, 0});
    for (unsigned i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, open));
    REQUIRE(world.regiments[0].pose.position.x > -23.f);
    REQUIRE(world.regiments[0].pose.position.x < -7.f);
    REQUIRE(world.command_state->nodes.front().sent_tasks.size() == 1);
    const auto original = world.command_state->nodes.front().sent_tasks.front();
    REQUIRE(original.action == c::TaskKind::MoveToArea);
    REQUIRE(std::abs(original.intent.objective_position.x) < .01f);

    world.scenario.obstacles.push_back({-2.f, -3.f, 3.f, 3.f});
    GridNavigationService detour(world.scenario.obstacles, 1.f, 121, 43, {-40, -21, 0});
    bool replaced = false;
    bool contact = false;
    for (unsigned i = 0; i < 1350 && !contact; ++i) {
        REQUIRE(Step(world, {}, {}, detour));
        const auto &tasks = world.command_state->nodes.front().sent_tasks;
        if (!tasks.empty())
            replaced |= tasks.front().id != original.id && tasks.front().action == c::TaskKind::MoveToArea &&
                        tasks.front().intent.objective_position.x > original.intent.objective_position.x + 1.f;
        contact = world.regiments[0].melee_engaged;
    }
    CHECK(replaced);
    CHECK(contact);
}

TEST_CASE("runtime.meeting_attacker_reestablishes_contact_after_enemy_disengages") {
    auto scenario = MeetingArmies(-12.f, 12.f);
    auto hold = Search({4}, 12.f);
    hold.action = c::TaskKind::Defend;
    scenario.command_setup->missions = {Search({1}, 0.f), hold};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    auto &attacker = world.regiments[0];
    for (unsigned i = 0; i < 350 && !attacker.melee_engaged; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(attacker.melee_engaged);

    auto &defender = world.regiments[1];
    const Vec3 retreat = {65.f - defender.pose.position.x, 0, 0};
    defender.pose.position = defender.pose.position + retreat;
    for (auto &slot : defender.slots)
        slot.position = slot.position + retreat;
    for (const auto id : defender.units) {
        auto &unit = *FindUnit(world, id);
        unit.pose.position = unit.pose.position + retreat;
        unit.slot.position = unit.slot.position + retreat;
    }
    hold.author = {4};
    hold.priority = PlayerOrderPriority;
    hold.intent.objective_position = defender.pose.position;
    REQUIRE(c::SubmitTask(world, hold).value);
    REQUIRE(Step(world, {}, {}, nav));
    REQUIRE_FALSE(attacker.melee_engaged);
    for (unsigned i = 0; i < 950 && !attacker.melee_engaged; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(attacker.melee_engaged);
    CHECK(attacker.pose.position.x > 55.f);
}
