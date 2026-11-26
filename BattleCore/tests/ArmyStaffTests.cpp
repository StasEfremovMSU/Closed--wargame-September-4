#include "TestSupport.h"
#include "battle/Control.h"
#include "battle/Snapshot.h"
#include "doctest.h"

using namespace battle;
namespace c = battle::command;

namespace {
Scenario StaffBattle(bool with_staff = true, bool with_part = false, bool with_guard = false) {
    auto scenario = test::Duel();
    constexpr c::NodeId army{1}, left{2}, right{3}, part{6};
    constexpr UnitId general{20}, adjutant{23};

    auto second = scenario.regiments.front();
    second.id = {3};
    second.pose.position.y = 7.f;
    scenario.regiments.push_back(second);
    auto soldier = scenario.units.front();
    soldier.id = {3};
    soldier.regiment = second.id;
    soldier.pose = second.pose;
    scenario.units.push_back(soldier);
    for (auto &unit : scenario.units) {
        unit.health = 100.f;
        unit.weapon.damage = 0.f;
    }

    auto &hierarchy = scenario.command_setup.emplace().hierarchy;
    const auto add_officer = [&](UnitId id, Vec3 position, bool staff) {
        UnitSpec unit;
        unit.id = id;
        unit.pose.position = position;
        unit.troop = UnitTroop::Mounted;
        unit.speed = staff ? 12.f : .001f;
        unit.health = 15.f;
        unit.weapon.damage = 0.f;
        scenario.units.push_back(unit);
        scenario.officers.push_back({{id.value}, id, staff ? 5u : 1u});
        if (!staff) {
            c::OfficerProfile profile;
            profile.officer = {id.value};
            profile.reevaluation_ticks = 10000;
            hierarchy.officers.push_back(profile);
        }
    };
    add_officer(general, {-19.f, 0.f, 0.f}, false);
    add_officer({21}, {-4.f, -3.f, 0.f}, false);
    add_officer({22}, {-4.f, 6.f, 0.f}, false);
    if (with_staff) {
        add_officer(adjutant, {-21.5f, -1.2f, 0.f}, true);
        hierarchy.support.push_back({{adjutant.value}, army, c::SupportDuty::Adjutant});
    }
    if (with_guard) {
        add_officer({24}, {-17.4f, -1.3f, 0.f}, true);
        hierarchy.support.push_back({{24}, army, c::SupportDuty::Bodyguard});
        scenario.units[1].archer = true;
        scenario.units[1].ranged_weapon = RangedWeapon::Musket;
    }
    const auto parent = with_part ? part : army;
    const auto add_node = [&](c::NodeId id, c::Level level, c::NodeId parent_id, OfficerId officer,
                              std::vector<c::NodeId> children, RegimentId regiment = {}) {
        c::CommandNode node;
        node.id = id;
        node.level = level;
        node.parent = parent_id;
        node.commander = officer;
        node.children = std::move(children);
        node.regiment = regiment;
        hierarchy.nodes.push_back(std::move(node));
    };
    add_node(army, c::Level::Army, {}, {general.value}, with_part ? std::vector{part} : std::vector{left, right});
    add_node(left, c::Level::Group, parent, {21}, {{4}});
    add_node(right, c::Level::Group, parent, {22}, {{5}});
    add_node({4}, c::Level::Regiment, left, {}, {}, {1});
    add_node({5}, c::Level::Regiment, right, {}, {}, {3});
    if (with_part)
        add_node(part, c::Level::Part, army, {}, {left, right});
    return scenario;
}

CommandEnvelope PlayerGroupOrder(const World &world, c::NodeId recipient, uint64_t sequence) {
    Command command;
    command.kind = CommandKind::IssueCommandTask;
    command.unit = {20};
    auto &task = command.command_task;
    task.issuer = {1};
    task.author = {20};
    task.recipient = recipient;
    task.priority = PlayerOrderPriority;
    task.action = c::TaskKind::HoldArea;
    task.intent.objective_position = {-5.f, 0.f, 0.f};
    return {world.tick, sequence, command};
}

const c::MessageEnvelope *PlayerDispatch(const World &world, c::NodeId id) {
    const auto &messages = world.command_state->communications.messages;
    const auto found = std::ranges::find_if(messages, [&](const auto &message) {
        const auto *task = std::get_if<c::CommandTask>(&message.payload);
        return message.recipient == id && task && task->priority == PlayerOrderPriority;
    });
    return found == messages.end() ? nullptr : &*found;
}

const c::NodeRuntime &OrdersFor(const World &world, c::NodeId node) {
    const auto &nodes = world.command_state->nodes;
    const auto found = std::ranges::find(nodes, node, &c::NodeRuntime::node);
    REQUIRE(found != nodes.end());
    return *found;
}

bool ReceivedPlayerOrder(const World &world, c::NodeId node) {
    return std::ranges::any_of(OrdersFor(world, node).orders, [](const auto &task) {
        return task.priority == PlayerOrderPriority && task.received_tick.has_value();
    });
}
} // namespace

TEST_CASE("runtime.staff_orders_reach_only_the_selected_group_when_the_rider_arrives") {
    auto created = CreateWorld(StaffBattle(), 17);
    REQUIRE(created.status == Status::Ok);
    auto &world = created.value;
    const auto start = FindUnit(world, {23})->pose.position;
    const auto request = PlayerGroupOrder(world, {2}, 1);
    FlatNavigationService navigation;

    REQUIRE(Step(world, std::span(&request, 1), {}, navigation));
    const auto *dispatch = PlayerDispatch(world, {2});
    REQUIRE(dispatch);
    CHECK(dispatch->channel == c::DeliveryChannel::Courier);
    CHECK(dispatch->status == c::DeliveryStatus::InTransit);
    CHECK(dispatch->courier == UnitId{23});
    CHECK_FALSE(ReceivedPlayerOrder(world, {2}));
    CHECK(Distance(FindUnit(world, {23})->pose.position, start) > .01f);
    auto saved = Restore(Capture(world));
    REQUIRE(saved);
    CHECK(StateHash(saved.value) == StateHash(world));

    for (int i = 0; i < 75 && !ReceivedPlayerOrder(world, {2}); ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(ReceivedPlayerOrder(world, {2}));
    REQUIRE(PlayerDispatch(world, {2}));
    CHECK(PlayerDispatch(world, {2})->status == c::DeliveryStatus::Delivered);
    CHECK_FALSE(ReceivedPlayerOrder(world, {3}));
    CHECK(Distance(FindUnit(world, {23})->pose.position, FindUnit(world, {21})->pose.position) < 3.f);
}

TEST_CASE("runtime.staff_queues_the_second_group_until_an_adjutant_returns") {
    auto created = CreateWorld(StaffBattle(), 17);
    REQUIRE(created);
    auto &world = created.value;
    const CommandEnvelope requests[] = {PlayerGroupOrder(world, {2}, 1), PlayerGroupOrder(world, {3}, 2)};
    FlatNavigationService navigation;
    REQUIRE(Step(world, requests, {}, navigation));
    REQUIRE(PlayerDispatch(world, {3}));
    CHECK(PlayerDispatch(world, {3})->status == c::DeliveryStatus::Queued);
    REQUIRE(Restore(Capture(world)));

    for (int i = 0; i < 75 && !ReceivedPlayerOrder(world, {2}); ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(ReceivedPlayerOrder(world, {2}));
    REQUIRE(PlayerDispatch(world, {3}));
    CHECK(PlayerDispatch(world, {3})->status == c::DeliveryStatus::Queued);

    for (int i = 0; i < 95 && !ReceivedPlayerOrder(world, {3}); ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(ReceivedPlayerOrder(world, {3}));
}

TEST_CASE("runtime.staff_group_hold_stops_following_at_the_post_reached_on_delivery") {
    auto created = CreateWorld(StaffBattle(), 17);
    REQUIRE(created);
    auto &world = created.value;
    REQUIRE(AcquireControl(world, {20}) == Status::Ok);
    Command follow;
    follow.kind = CommandKind::SetRegimentFollowing;
    follow.unit = {20};
    follow.regiment = {1};
    follow.follow_enabled = true;
    REQUIRE(ApplyControlCommand(world, follow) == Status::Ok);
    const auto request = PlayerGroupOrder(world, {2}, 1);
    auto *regiment = FindRegiment(world, {1});
    FlatNavigationService navigation;
    REQUIRE(Step(world, std::span(&request, 1), {}, navigation));
    CHECK(regiment->following_officer == UnitId{20});
    Vec3 post{};
    for (int i = 0; i < 75 && !ReceivedPlayerOrder(world, {2}); ++i) {
        post = regiment->pose.position;
        REQUIRE(Step(world, {}, {}, navigation));
    }
    REQUIRE(ReceivedPlayerOrder(world, {2}));
    CHECK_FALSE(regiment->following_officer);
    REQUIRE(regiment->order);
    CHECK(regiment->order->type == OrderType::Hold);
    CHECK(regiment->order->point == post);
    CHECK(regiment->order->player_direct);
}

TEST_CASE("runtime.staff_loses_a_dispatch_when_its_adjutant_is_killed") {
    auto created = CreateWorld(StaffBattle(), 17);
    REQUIRE(created);
    auto &world = created.value;
    const auto request = PlayerGroupOrder(world, {2}, 1);
    FlatNavigationService navigation;
    REQUIRE(Step(world, std::span(&request, 1), {}, navigation));
    REQUIRE(PlayerDispatch(world, {2}));
    REQUIRE(PlayerDispatch(world, {2})->status == c::DeliveryStatus::InTransit);

    world.attacks.push_back({{world.next_event_id++}, {2}, {23}, world.tick + 1, 30.f, true, 20.f, true});
    REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE_FALSE(FindUnit(world, {23})->alive);
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK(PlayerDispatch(world, {2})->status == c::DeliveryStatus::Lost);
    CHECK_FALSE(ReceivedPlayerOrder(world, {2}));
}

TEST_CASE("runtime.staff_can_be_dispatched_when_the_general_is_against_the_field_boundary") {
    auto scenario = StaffBattle();
    scenario.field_bounds = TerrainFieldBounds{19.f, 15.f};
    auto created = CreateWorld(scenario, 17);
    REQUIRE(created);
    auto &world = created.value;
    FindUnit(world, {23})->pose.position.x = -18.7f;
    const auto request = PlayerGroupOrder(world, {2}, 1);
    FlatNavigationService navigation;
    REQUIRE(Step(world, std::span(&request, 1), {}, navigation));
    REQUIRE(PlayerDispatch(world, {2}));
    CHECK(PlayerDispatch(world, {2})->status == c::DeliveryStatus::InTransit);
}

TEST_CASE("runtime.staff_army_ai_uses_couriers_for_group_tasks") {
    auto created = CreateWorld(StaffBattle(), 17);
    REQUIRE(created);
    auto &world = created.value;
    c::CommandTask mission;
    mission.recipient = {1};
    mission.action = c::TaskKind::MoveToArea;
    mission.intent.objective_position = {4.f, 0.f, 0.f};
    REQUIRE(c::SubmitTask(world, mission).value);
    FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    const auto &messages = world.command_state->communications.messages;
    const auto dispatch = std::ranges::find_if(messages, [](const auto &message) {
        return message.sender == c::NodeId{1} && std::holds_alternative<c::CommandTask>(message.payload);
    });
    REQUIRE(dispatch != messages.end());
    CHECK(dispatch->channel == c::DeliveryChannel::Courier);
    CHECK_FALSE(dispatch->status == c::DeliveryStatus::Delivered);
}

TEST_CASE("runtime.staff_opening_assignments_follow_the_prebattle_briefing") {
    auto scenario = StaffBattle();
    c::CommandTask briefing;
    briefing.recipient = {1};
    briefing.action = c::TaskKind::MoveToArea;
    briefing.intent.objective_position = {4.f, 0.f, 0.f};
    scenario.command_setup->missions.push_back(briefing);
    auto created = CreateWorld(scenario, 17);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    const auto &messages = world.command_state->communications.messages;
    const auto dispatch = std::ranges::find_if(messages, [](const auto &message) {
        return message.sender == c::NodeId{1} && std::holds_alternative<c::CommandTask>(message.payload);
    });
    REQUIRE(dispatch != messages.end());
    CHECK(dispatch->channel == c::DeliveryChannel::Signal);
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK(std::ranges::any_of(OrdersFor(world, {2}).orders, [](const auto &task) {
        return task.issuer == c::NodeId{1} && task.received_tick.has_value();
    }));
}

TEST_CASE("runtime.staff_authored_attack_can_act_on_first_contact_during_briefing") {
    Tick contact_tick = 4;
    auto expected_channel = c::DeliveryChannel::Signal;
    SUBCASE("Contact arrives during the opening reports") {}
    SUBCASE("Contact arrives after the opening reports") {
        contact_tick = 15;
        expected_channel = c::DeliveryChannel::Courier;
    }
    auto scenario = StaffBattle();
    scenario.regiments[1].pose.position.x = 100.f;
    scenario.units[1].pose = scenario.regiments[1].pose;
    auto &setup = *scenario.command_setup;
    setup.hierarchy.officers.front().reevaluation_ticks = 1;
    c::CommandTask briefing;
    briefing.recipient = {1};
    briefing.action = c::TaskKind::Attack;
    briefing.intent.target_regiment = {2};
    briefing.intent.objective_position = {10.f, 0.f, 0.f};
    briefing.authority.initiative = c::InitiativePolicy::WithinIntent;
    briefing.authority.may_change_target = briefing.authority.may_pursue = true;
    setup.missions.push_back(briefing);

    auto created = CreateWorld(scenario, 17);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    while (world.tick < contact_tick)
        REQUIRE(Step(world, {}, {}, navigation));
    auto *enemy = FindRegiment(world, {2});
    enemy->pose.position.x = 10.f;
    auto *scout = FindUnit(world, {2});
    scout->pose = scout->slot = enemy->pose;
    REQUIRE(Step(world, {}, {}, navigation));

    const auto &messages = world.command_state->communications.messages;
    const auto attack = std::ranges::find_if(messages, [&](const auto &message) {
        const auto *task = std::get_if<c::CommandTask>(&message.payload);
        return message.created_tick == contact_tick && message.sender == c::NodeId{1} &&
               task && task->action == c::TaskKind::Attack && task->intent.target_regiment == RegimentId{2};
    });
    REQUIRE(attack != messages.end());
    CHECK(attack->channel == expected_channel);
}

TEST_CASE("runtime.staff_leaves_legacy_and_intermediate_command_links_immediate") {
    bool with_part = false;
    SUBCASE("No staff assigned") {}
    SUBCASE("Army has staff but sends to its intermediate command") { with_part = true; }

    auto created = CreateWorld(StaffBattle(with_part, with_part), 17);
    REQUIRE(created);
    auto &world = created.value;
    const c::NodeId recipient{with_part ? 6u : 2u};
    const auto request = PlayerGroupOrder(world, recipient, 1);
    FlatNavigationService navigation;
    REQUIRE(Step(world, std::span(&request, 1), {}, navigation));
    CHECK(ReceivedPlayerOrder(world, recipient));
    CHECK_FALSE(PlayerDispatch(world, recipient));
}

TEST_CASE("runtime.staff_guard_follows_the_general") {
    auto created = CreateWorld(StaffBattle(true, false, true), 17);
    REQUIRE(created);
    auto &world = created.value;
    const auto start = FindUnit(world, {24})->pose.position;
    auto *general = FindUnit(world, {20});
    general->pose.position = {-26.f, 7.f, 0.f};
    FlatNavigationService navigation;
    for (int i = 0; i < 45; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(Distance(FindUnit(world, {24})->pose.position, start) > 5.f);
    CHECK(Distance(FindUnit(world, {24})->pose.position, general->pose.position) < 3.f);
}

TEST_CASE("runtime.staff_guard_can_die_from_ordinary_ball_damage_without_immunizing_the_general") {
    auto created = CreateWorld(StaffBattle(true, false, true), 17);
    REQUIRE(created);
    auto &world = created.value;
    auto *general = FindUnit(world, {20});
    auto *guard = FindUnit(world, {24});
    general->health = 100.f;
    for (int i = 0; i < 50; ++i)
        world.attacks.push_back({{world.next_event_id++}, {2}, {20}, 1, 2.f, true, 80.f, true});
    FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK_FALSE(guard->alive);
    CHECK(general->alive);
    CHECK(general->health < 100.f);
    CHECK(std::ranges::any_of(world.events, [](const auto &event) {
        return event.kind == EventKind::Died && event.target == UnitId{24};
    }));
    world.attacks.push_back({{world.next_event_id++}, {2}, {20}, 2, 100.f, true, 80.f, true});
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK_FALSE(general->alive);
}

TEST_CASE("runtime.staff_is_exposed_to_live_musket_fire_only_in_a_staffed_battle") {
    bool staffed = true;
    bool tactics = true;
    SUBCASE("Tactical firearm aiming") {}
    SUBCASE("Local combat without tactical aiming") { tactics = false; }
    SUBCASE("Legacy independent commander") { staffed = false; }

    auto scenario = StaffBattle(staffed, false, staffed);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = tactics;
    scenario.regiments[1].pose = {{-12.f, 0.f, 0.f}, Pi};
    scenario.units[1].pose = scenario.regiments[1].pose;
    scenario.units[1].archer = true;
    scenario.units[1].ranged_weapon = RangedWeapon::Musket;
    auto &weapon = scenario.units[1].weapon;
    weapon.range = 40.f;
    weapon.damage = 20.f;
    weapon.projectile_speed_mps = 300.f;
    weapon.cooldown_ticks = 1;
    weapon.base_accuracy = 1.f;
    for (auto &regiment : scenario.regiments)
        if (regiment.team == 0) {
            regiment.pose.position.x -= 35.f;
            for (auto &unit : scenario.units)
                if (unit.regiment == regiment.id)
                    unit.pose = regiment.pose;
        }
    auto created = CreateWorld(scenario, 17);
    REQUIRE(created);
    auto &world = created.value;
    auto *general = FindUnit(world, {20});
    general->health = 1000.f;
    REQUIRE(AcquireControl(world, {2}) == Status::Ok);
    Command aim;
    aim.kind = CommandKind::AttackControlledUnit;
    aim.unit = {2};
    aim.target = {20};
    REQUIRE(ApplyControlCommand(world, aim) == Status::Ok);
    FlatNavigationService navigation;
    bool launched = false, escort_loss = false;
    for (int i = 0; i < 100 && (!escort_loss || general->health == 1000.f); ++i) {
        REQUIRE(Step(world, {}, {}, navigation));
        for (const auto &event : world.events) {
            launched |= event.kind == EventKind::AttackLaunched && event.source == UnitId{2} &&
                        event.target == UnitId{20} && event.projectile;
            escort_loss |= event.kind == EventKind::Died && event.source == UnitId{2} &&
                           event.target == UnitId{24};
        }
    }
    CHECK(launched == staffed);
    CHECK(escort_loss == staffed);
    CHECK((general->health < 1000.f) == staffed);
}
