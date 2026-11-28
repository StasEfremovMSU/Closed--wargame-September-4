#include "CommandSupport.h"
#include "battle/Control.h"
#include "doctest.h"

using namespace battle;
namespace c = battle::command;

namespace {
Scenario ScopedBattle(bool obscured = false) {
    const test::CommandFixture fixture;
    auto scenario = fixture.world.scenario;
    scenario.rules.regiment_tactics = true;
    auto &hierarchy = scenario.command_setup.emplace().hierarchy;
    hierarchy = fixture.hierarchy;
    auto near_friend = scenario.regiments[0];
    near_friend.id = {3};
    near_friend.pose.position.y = 20.f;
    near_friend.formation.columns = 2;
    scenario.regiments.push_back(near_friend);
    auto far_enemy = scenario.regiments[1];
    far_enemy.id = {4};
    far_enemy.pose.position.y = 20.f;
    far_enemy.formation.columns = 1;
    scenario.regiments.push_back(far_enemy);
    for (const auto id : {9u, 10u, 11u}) {
        UnitSpec unit;
        unit.id = {id};
        unit.regiment = id == 11 ? far_enemy.id : near_friend.id;
        unit.pose = id == 11 ? far_enemy.pose : near_friend.pose;
        scenario.units.push_back(unit);
        if (id != 11) {
            scenario.officers.push_back({{id}, unit.id, id == 9 ? 1u : 2u});
            auto profile = hierarchy.officers.front();
            profile.officer = {id};
            hierarchy.officers.push_back(profile);
        }
    }
    hierarchy.nodes[1].children.push_back({5});
    auto group = hierarchy.nodes[2];
    group.id = {5};
    group.commander = {9};
    group.children = {{6}};
    hierarchy.nodes.push_back(group);
    auto leaf = hierarchy.nodes[3];
    leaf.id = {6};
    leaf.parent = group.id;
    leaf.commander = {10};
    leaf.regiment = near_friend.id;
    hierarchy.nodes.push_back(leaf);
    if (obscured)
        scenario.obstacles.push_back({-1, -5, 1, 5, true});
    return scenario;
}
}

TEST_CASE("runtime.scoped_attack_uses_possession_authority_and_assigns_each_regiment_a_near_target") {
    auto created = CreateWorld(ScopedBattle(), 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    REQUIRE(AcquireControl(world, {1}) == Status::Ok);
    CHECK_FALSE(c::CanOrderScopedAttack(world, {1}));
    CHECK(c::PlanScopedAttacks(world, navigation, {1}).empty());
    CHECK(c::PlanScopedAttacks(world, navigation, {4}).empty());

    REQUIRE(AcquireControl(world, {2}) == Status::Ok);
    REQUIRE(c::CanOrderScopedAttack(world, {2}));
    auto plan = c::PlanScopedAttacks(world, navigation, {2});
    REQUIRE(plan.size() == 1);
    CHECK(plan[0].order.recipient == RegimentId{1});
    CHECK(plan[0].order.target_regiment == RegimentId{2});
    CHECK(plan[0].order.delivery_tick > world.tick);

    REQUIRE(AcquireControl(world, {4}) == Status::Ok);
    REQUIRE(c::CanOrderScopedAttack(world, {4}));
    plan = c::PlanScopedAttacks(world, navigation, {4});
    REQUIRE(plan.size() == 2);
    CHECK(plan[0].order.recipient == RegimentId{1});
    CHECK(plan[0].order.target_regiment == RegimentId{2});
    CHECK(plan[1].order.recipient == RegimentId{3});
    CHECK(plan[1].order.target_regiment == RegimentId{4});
    const RegimentId reserve = {3};
    CHECK(c::PlanScopedAttacks(world, navigation, {4}, std::span(&reserve, 1)).size() == 1);
    const CommandEnvelope packet{world.tick, 1, plan.front()};
    REQUIRE(Step(world, std::span(&packet, 1), {}, navigation));
    REQUIRE(world.pending_orders.size() == 1);
    CHECK(world.pending_orders.front().player_direct);
}

TEST_CASE("runtime.scoped_attack_skips_a_nearer_enemy_the_commander_cannot_see") {
    auto created = CreateWorld(ScopedBattle(true), 42);
    REQUIRE(created);
    auto &world = created.value;
    REQUIRE(AcquireControl(world, {2}) == Status::Ok);
    GridNavigationService navigation(world.scenario.obstacles, 1.f, 81, 81, {-40, -40, 0});
    const auto plan = c::PlanScopedAttacks(world, navigation, {2});
    REQUIRE(plan.size() == 1);
    CHECK(plan[0].order.target_regiment == RegimentId{4});
    CHECK(plan[0].order.point == Vec3{10.f, 20.f, 0.f});
}

TEST_CASE("runtime.scoped_attack_advances_on_a_contact_outside_the_recipient_sight") {
    auto scenario = ScopedBattle();
    for (auto &regiment : scenario.regiments)
        if (regiment.team != 0)
            regiment.pose.position.x = 70.f;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    REQUIRE(AcquireControl(world, {2}) == Status::Ok);
    FindUnit(world, {2})->pose.position = {20.f, 0.f, 0.f};
    const auto &recipient = world.regiments.front();
    const float start = recipient.pose.position.x;
    FlatNavigationService navigation;
    const auto plan = c::PlanScopedAttacks(world, navigation, {2});
    REQUIRE(plan.size() == 1);
    REQUIRE(plan[0].kind == CommandKind::IssueOrder);
    CHECK(plan[0].order.target_regiment == RegimentId{2});
    CHECK(plan[0].order.point.x == 70.f);
    const CommandEnvelope packet{world.tick, 1, plan.front()};
    REQUIRE(Step(world, std::span(&packet, 1), {}, navigation));
    for (int i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(recipient.order);
    CHECK(recipient.order->type == OrderType::Attack);
    CHECK(recipient.ai_action == OrderType::MoveTo);
    CHECK(recipient.pose.position.x > start + 1.f);
}
