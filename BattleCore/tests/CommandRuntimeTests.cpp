#include "CommandSupport.h"
#include "ReplayIo.h"
#include "battle/Snapshot.h"
#include "battle/Officers.h"
#include "battle/Formation.h"
#include "battle/command/Services.h"
#include "doctest.h"
using namespace battle;
namespace c = battle::command;

namespace {
Scenario CommandDuel(bool with_mission = true) {
    auto scenario = test::Duel(4);
    for (unsigned i = 0; i < 4; ++i)
        scenario.officers.push_back({{i + 1}, {i + 1}, i ? 1u : 2u});
    c::CommandSetup setup;
    for (unsigned i = 0; i < 4; ++i) {
        c::CommandNode node;
        node.id = {i + 1};
        node.level = c::Level(i);
        node.parent = {i};
        node.team = 0;
        node.commander = {4 - i};
        if (i < 3)
            node.children.push_back({i + 2});
        else
            node.regiment = {1};
        setup.hierarchy.nodes.push_back(node);
        c::OfficerProfile profile;
        profile.officer = node.commander;
        profile.tactical_skill = .7f;
        profile.initiative = .7f;
        profile.discipline = .8f;
        profile.aggression = .8f;
        if (node.level == c::Level::Regiment)
            profile.initiative = 0;
        profile.command_radius_m = 60;
        setup.hierarchy.officers.push_back(profile);
    }
    if (with_mission) {
        c::CommandTask mission;
        mission.id = {1};
        mission.recipient = {1};
        mission.action = c::TaskKind::Attack;
        mission.intent.target_regiment = {2};
        mission.intent.objective_position = {10, 0, 0};
        mission.status = c::TaskStatus::Active;
        setup.missions.push_back(mission);
    }
    scenario.command_setup = std::move(setup);
    return scenario;
}

Scenario TwoRegimentGroup() {
    auto scenario = CommandDuel(false);
    auto &hierarchy = scenario.command_setup->hierarchy;
    hierarchy.nodes.erase(hierarchy.nodes.begin(), hierarchy.nodes.begin() + 2);
    hierarchy.officers.erase(hierarchy.officers.begin(), hierarchy.officers.begin() + 2);
    auto &group = hierarchy.nodes.front();
    group.parent = {};
    group.children.push_back({5});
    auto regiment = scenario.regiments.front();
    regiment.id = {3};
    regiment.pose.position = {-24, 0, 0};
    regiment.formation.columns = 1;
    scenario.regiments.push_back(regiment);
    auto unit = scenario.units.front();
    unit.id = {9};
    unit.regiment = regiment.id;
    unit.pose = regiment.pose;
    scenario.units.push_back(unit);
    scenario.officers.push_back({{9}, unit.id, 2});
    auto leaf = hierarchy.nodes.back();
    leaf.id = {5};
    leaf.regiment = regiment.id;
    leaf.commander = {9};
    hierarchy.nodes.push_back(leaf);
    auto profile = hierarchy.officers.back();
    profile.officer = leaf.commander;
    hierarchy.officers.push_back(profile);
    return scenario;
}

struct NoPathNavigation final : NavigationService {
    PathResult FindPath(const PathRequest &) const override {
        return {Status::NoPath, {}};
    }
};
} // namespace

TEST_CASE("runtime.hill_crest_occludes_officer_observation") {
    auto scenario = CommandDuel(false);
    const c::NodeId regiment_node{4};
    scenario.regiments[0].pose.position.x = -28.f;
    scenario.regiments[1].pose.position.x = 28.f;
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    auto created = CreateWorld(scenario, 13);
    REQUIRE(created);
    auto &world = created.value;
    const auto &hierarchy = world.command_state->hierarchy;
    const auto sees_enemy = [&] {
        const auto result = c::BuildSituation(world, hierarchy, regiment_node, world.tick);
        REQUIRE(result.value);
        return std::ranges::any_of(result.value->contacts, [](const auto &contact) {
            return contact.regiment == RegimentId{2};
        });
    };
    CHECK(sees_enemy());
    world.scenario.hill = TerrainHill{};
    CHECK_FALSE(sees_enemy());
    FindUnit(world, {1})->pose.position = {10.f, 0.f, 0.f};
    CHECK(sees_enemy());
}

TEST_CASE("runtime.adaptive_command_schedules_stagger_uninterrupted_brains") {
    auto scenario = CommandDuel(false);
    scenario.rules.adaptive_battle = true;
    auto created = CreateWorld(scenario, 13);
    REQUIRE(created);
    const auto &nodes = created.value.command_state->nodes;
    REQUIRE(nodes.size() == 4);
    CHECK(nodes[0].schedule.interval_ticks == 100);
    CHECK(nodes[1].schedule.interval_ticks == 60);
    CHECK(nodes[2].schedule.interval_ticks == 40);
    CHECK(nodes[3].schedule.interval_ticks == 20);
    CHECK(nodes[0].schedule.next_think_tick != nodes[2].schedule.next_think_tick);
    CHECK(nodes[2].schedule.next_think_tick != nodes[3].schedule.next_think_tick);
    for (const auto &node : nodes)
        CHECK(node.schedule.next_think_tick < node.schedule.interval_ticks);
}

TEST_CASE("runtime.communication_order_expiry_and_dedup") {
    c::CommunicationsState state;
    c::MessageEnvelope signal;
    signal.id = {1};
    signal.sender = {10};
    signal.recipient = {11};
    signal.channel = c::DeliveryChannel::Signal;
    signal.created_tick = 0;
    signal.delivery_tick = 2;
    signal.expiry_tick = 4;
    signal.status = c::DeliveryStatus::InTransit;
    c::CommandTask payload;
    payload.id = {3};
    payload.issuer = signal.sender;
    payload.recipient = signal.recipient;
    payload.created_tick = 0;
    payload.action = c::TaskKind::HoldArea;
    signal.payload = payload;
    state.messages.push_back(signal);
    c::MessageEnvelope expired = signal;
    expired.id = {2};
    expired.delivery_tick = 3;
    expired.expiry_tick = 2;
    state.messages.push_back(expired);

    auto early = c::AdvanceCommunications(state, 1);
    REQUIRE(early.status == c::StubStatus::Ready);
    REQUIRE(early.value);
    CHECK(early.value->delivered.empty());
    CHECK(early.value->next.messages[0].status == c::DeliveryStatus::InTransit);

    auto due = c::AdvanceCommunications(early.value->next, 2);
    REQUIRE(due.status == c::StubStatus::Ready);
    REQUIRE(due.value);
    REQUIRE(due.value->delivered.size() == 1);
    CHECK(due.value->delivered[0].id.value == 1);
    CHECK(due.value->next.messages[0].status == c::DeliveryStatus::Delivered);
    CHECK(due.value->next.messages[1].status == c::DeliveryStatus::Expired);
    auto repeated = c::AdvanceCommunications(due.value->next, 2);
    REQUIRE(repeated.value);
    CHECK(repeated.value->delivered.empty());
}

TEST_CASE("runtime.regiment_task_translation") {
    c::CommandTask task;
    task.id = {5};
    task.issuer = {2};
    task.recipient = {3};
    task.author = {9};
    task.action = c::TaskKind::Attack;
    task.intent.target_regiment = {7};
    task.intent.objective_position = {15, 2, 0};
    task.created_tick = 3;
    task.duration_ticks = 10;
    task.duration_origin = c::DurationOrigin::Created;
    task.status = c::TaskStatus::Active;
    auto translated = c::TranslateRegimentTask(task, {42}, {}, 5);
    REQUIRE(translated.status == c::StubStatus::Ready);
    REQUIRE(translated.value);
    CHECK(translated.value->order.recipient.value == 42);
    CHECK(translated.value->order.issuer.value == 9);
    CHECK(translated.value->order.type == OrderType::Attack);
    CHECK(translated.value->order.target_regiment.value == 7);
    CHECK(translated.value->order.expiry_tick == 13);

    task.action = c::TaskKind::Reform;
    CHECK(c::TranslateRegimentTask(task, {42}, {}, 5).status == c::StubStatus::InvalidInput);
    task.formation_columns = 3;
    task.formation_spacing_m = 1.5f;
    translated = c::TranslateRegimentTask(task, {42}, {}, 5);
    REQUIRE(translated.value);
    REQUIRE(translated.value->formation);
    CHECK(translated.value->formation->columns == 3);
    CHECK(translated.value->formation->spacing_m == doctest::Approx(1.5f));
}

TEST_CASE("runtime.four_level_delivery_and_no_order_spam") {
    auto made = CreateWorld(CommandDuel(), 41);
    REQUIRE(made.status == Status::Ok);
    World world = std::move(made.value);
    REQUIRE(world.command_state);
    FlatNavigationService nav;
    auto *regiment = FindRegiment(world, {1});
    REQUIRE(regiment);
    const float starting_x = regiment->pose.position.x;
    for (int i = 0; i < 100 && (!regiment->order || regiment->order->type != OrderType::Attack);
         ++i)
        REQUIRE(Step(world, {}, {}, nav).status == Status::Ok);
    REQUIRE(regiment->order);
    CHECK(regiment->order->type == OrderType::Attack);
    CHECK(regiment->order->target_regiment.value == 2);
    CHECK(regiment->pose.position.x > starting_x);
    const auto id = regiment->order->id;
    for (int i = 0; i < 4; ++i)
        REQUIRE(Step(world, {}, {}, nav).status == Status::Ok);
    CHECK(regiment->order->id == id);
    bool hit = false;
    for (int i = 0; i < 160 && !hit; ++i) {
        auto step = Step(world, {}, {}, nav);
        REQUIRE(step.status == Status::Ok);
        hit = std::any_of(step.value.events.begin(), step.value.events.end(),
                          [](const auto &event) { return event.kind == EventKind::Hit; });
    }
    CHECK(hit);
}

TEST_CASE("runtime.external_order_precedes_fsm_and_issue_task_is_live") {
    auto made = CreateWorld(CommandDuel(), 42);
    REQUIRE(made.status == Status::Ok);
    World world = std::move(made.value);
    CommandEnvelope external;
    external.tick = 0;
    external.sequence = 1;
    external.command.kind = CommandKind::IssueOrder;
    external.command.order.id = {50};
    external.command.order.recipient = {1};
    external.command.order.type = OrderType::MoveTo;
    external.command.order.point = {-20, 0, 0};
    external.command.order.priority = 50;
    FlatNavigationService nav;
    for (int i = 0; i < 8; ++i) {
        std::array<CommandEnvelope, 1> input{external};
        REQUIRE(Step(world,
                     i == 0 ? std::span<const CommandEnvelope>(input)
                            : std::span<const CommandEnvelope>{},
                     {}, nav)
                    .status == Status::Ok);
    }
    REQUIRE(FindRegiment(world, {1})->order);
    CHECK(FindRegiment(world, {1})->order->id.value == 50);
    CHECK(FindRegiment(world, {1})->order->type == OrderType::MoveTo);

    auto no_mission = CreateWorld(CommandDuel(false), 43);
    REQUIRE(no_mission.status == Status::Ok);
    Command request;
    request.kind = CommandKind::IssueCommandTask;
    request.command_task.recipient = {1};
    request.command_task.action = c::TaskKind::HoldArea;
    CommandEnvelope envelope{0, 1, request};
    REQUIRE(
        Step(no_mission.value, std::span<const CommandEnvelope>(&envelope, 1), {}, nav).status ==
        Status::Ok);
    REQUIRE(no_mission.value.command_state);
    CHECK(no_mission.value.command_state->nodes[0].memory.active_task);
}

TEST_CASE("runtime.courier_route_and_death_are_checked") {
    auto made = CreateWorld(CommandDuel(false), 44);
    REQUIRE(made.status == Status::Ok);
    World world = std::move(made.value);
    REQUIRE(world.command_state);
    c::MessageEnvelope message;
    message.id = {10};
    message.sender = {1};
    message.recipient = {2};
    message.channel = c::DeliveryChannel::Courier;
    message.courier = {1};
    message.created_tick = 0;
    message.delivery_tick = 100;
    message.status = c::DeliveryStatus::InTransit;
    c::CommandTask payload;
    payload.id = {10};
    payload.issuer = message.sender;
    payload.recipient = message.recipient;
    payload.action = c::TaskKind::HoldArea;
    message.payload = payload;
    world.command_state->communications.messages.push_back(message);
    world.command_state->next_message_id = {11};
    NoPathNavigation no_path;
    auto updated = c::UpdateCommand(world, no_path);
    REQUIRE(updated.status == c::StubStatus::Ready);
    CHECK(world.command_state->communications.messages[0].status == c::DeliveryStatus::Lost);

    world.command_state->communications.messages[0].status = c::DeliveryStatus::InTransit;
    FindUnit(world, {1})->alive = false;
    updated = c::UpdateCommand(world, no_path);
    INFO(updated.reason);
    REQUIRE(updated.status == c::StubStatus::Ready);
    CHECK(world.command_state->communications.messages[0].status == c::DeliveryStatus::Lost);
}

TEST_CASE("runtime.unreachable_regiment_reports_support_request") {
    auto made = CreateWorld(CommandDuel(), 45);
    REQUIRE(made.status == Status::Ok);
    World world = std::move(made.value);
    NoPathNavigation no_path;
    for (int i = 0; i < 12; ++i)
        REQUIRE(Step(world, {}, {}, no_path).status == Status::Ok);
    REQUIRE(world.command_state);
    CHECK(std::any_of(world.command_state->communications.messages.begin(),
                      world.command_state->communications.messages.end(), [](const auto &message) {
                          const auto *report = std::get_if<c::SituationReport>(&message.payload);
                          return report && report->kind == c::ReportKind::NeedSupport;
                      }));
}

TEST_CASE("runtime.commander_loss_report_retains_a_valid_observation") {
    auto made = CreateWorld(CommandDuel(false), 47);
    REQUIRE(made);
    World world = std::move(made.value);
    auto *fallen = FindUnit(world, {1});
    REQUIRE(fallen);
    fallen->alive = false;
    fallen->health = 0;
    fallen->control = ControlMode::Dead;
    auto *regiment = FindRegiment(world, {1});
    REQUIRE(regiment);
    std::erase(regiment->units, UnitId{1});

    FlatNavigationService nav;
    auto updated = command::UpdateCommand(world, nav);
    REQUIRE(updated.status == command::StubStatus::Ready);
    REQUIRE(world.command_state);
    auto lost = std::find_if(world.command_state->communications.messages.begin(),
                             world.command_state->communications.messages.end(), [](const auto &m) {
                                 const auto *report = std::get_if<command::SituationReport>(&m.payload);
                                 return report && report->kind == command::ReportKind::CommanderLost;
                             });
    REQUIRE(lost != world.command_state->communications.messages.end());
    const auto &report = std::get<command::SituationReport>(lost->payload);
    CHECK(report.situation.observer == report.sender);
    CHECK(report.situation.tick == report.created_tick);
    CHECK(Restore(Capture(world)));
}

TEST_CASE("runtime.command_loss_names_a_real_successor_or_leaves_the_post_vacant") {
    for (const bool reserve : {false, true}) {
        auto scenario = CommandDuel(false);
        scenario.rules.regiment_tactics = true;
        if (reserve) {
            auto deputy = scenario.units.front();
            deputy.id = {9};
            scenario.units.push_back(deputy);
            scenario.officers.push_back({{9}, deputy.id, 2});
            auto &hierarchy = scenario.command_setup->hierarchy;
            auto profile = hierarchy.officers.back();
            profile.officer = {9};
            hierarchy.officers.push_back(profile);
            hierarchy.nodes.back().succession.push_back({9});
        }
        auto created = CreateWorld(scenario, 2);
        REQUIRE(created);
        auto &world = created.value;
        auto &fallen = world.units.front();
        fallen.alive = false;
        fallen.health = 0;
        fallen.control = ControlMode::Dead;
        std::erase(world.regiments.front().units, fallen.id);
        world.tick = 20;
        FlatNavigationService nav;
        REQUIRE(c::UpdateCommand(world, nav).status == c::StubStatus::Ready);
        const auto expected = reserve ? EventKind::OfficerSucceeded : EventKind::CommandVacant;
        const auto event = std::ranges::find(world.events, expected, &BattleEvent::kind);
        REQUIRE(event != world.events.end());
        CHECK(event->source == (reserve ? UnitId{9} : UnitId{}));
        CHECK(event->target == fallen.id);
        CHECK(event->regiment == fallen.regiment);
        CHECK(event->tick == world.tick);
        world.events.clear();
        REQUIRE(c::UpdateCommand(world, nav).status == c::StubStatus::Ready);
        CHECK(std::ranges::find(world.events, expected, &BattleEvent::kind) == world.events.end());
    }
}

TEST_CASE("runtime.future_dated_initial_mission_is_rejected") {
    auto scenario = CommandDuel(false);
    c::CommandTask mission;
    mission.id = {1};
    mission.recipient = {1};
    mission.action = c::TaskKind::HoldArea;
    mission.created_tick = 1;
    mission.status = c::TaskStatus::Active;
    scenario.command_setup->missions.push_back(mission);
    CHECK(CreateWorld(scenario, 48).status == Status::InvalidInput);
}

TEST_CASE("runtime.task_submission_checks_authority_side_and_ids") {
    auto made = CreateWorld(CommandDuel(false), 46);
    REQUIRE(made.status == Status::Ok);
    World world = std::move(made.value);
    c::CommandTask task;
    task.recipient = {1};
    task.action = c::TaskKind::Attack;
    task.intent.target_regiment = {1}; // Friendly to the Army.
    CHECK(c::SubmitTask(world, task).status == c::StubStatus::InvalidInput);
    task.intent.target_regiment = {2};
    task.issuer = {2}; // Army tasks may only be submitted externally to the root.
    CHECK(c::SubmitTask(world, task).status == c::StubStatus::InvalidInput);
    task.issuer = {};
    auto accepted = c::SubmitTask(world, task);
    REQUIRE(accepted.status == c::StubStatus::Ready);
    REQUIRE(accepted.value);
    CHECK(accepted.value->value != 0);
}

TEST_CASE("runtime.group_commitment_keeps_one_message_across_due_decisions") {
    auto scenario = TwoRegimentGroup();
    scenario.rules.local_melee = true;
    auto &primary = scenario.regiments[1];
    primary.pose.position.x = 2.f;
    for (auto &unit : scenario.units) {
        if (unit.regiment == primary.id)
            unit.pose = primary.pose;
        unit.weapon.damage = 0;
    }
    auto alternate = primary;
    alternate.id = {4};
    alternate.formation.columns = 1;
    alternate.pose.position.y = 9.f;
    scenario.regiments.push_back(alternate);
    auto unit = scenario.units[4];
    unit.id = {10};
    unit.regiment = alternate.id;
    unit.pose = alternate.pose;
    scenario.units.push_back(unit);

    c::CommandTask mission;
    mission.id = {1};
    mission.recipient = {3};
    mission.action = c::TaskKind::Attack;
    mission.intent.target_regiment = {2};
    mission.intent.objective_position = {2, 0, 0};
    mission.authority.initiative = c::InitiativePolicy::WithinIntent;
    mission.authority.may_pursue = true;
    mission.authority.may_change_target = true;
    scenario.command_setup->missions.push_back(mission);

    struct PreferAction final : c::ITacticalEvaluator {
        c::TaskKind action{c::TaskKind::Support};
        mutable unsigned calls{};
        c::StubResult<std::vector<c::ActionScore>> Evaluate(const c::EvaluationRequest &request) const override {
            ++calls;
            std::vector<c::ActionScore> scores;
            for (uint32_t i = 0; i < request.options.size(); ++i)
                scores.push_back({i, request.options[i].action == action ? 1.f : 0.f, {}});
            return {c::StubStatus::Ready, std::move(scores), {}};
        }
    } evaluator;

    auto created = CreateWorld(scenario, 23);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    const auto assignment = [&] {
        const auto &tasks = world.command_state->nodes.front().sent_tasks;
        const auto found = std::ranges::find(tasks, c::NodeId{5}, &c::CommandTask::recipient);
        return found == tasks.end() ? c::CommandTask{} : *found;
    };
    const SimulationOptions options{50, 1, &evaluator, true};
    for (int i = 0; i < 40 && evaluator.calls == 0; ++i)
        REQUIRE(Step(world, {}, options, navigation));
    REQUIRE(evaluator.calls == 1);
    const auto original = assignment();
    REQUIRE(original.id);
    REQUIRE(original.action == c::TaskKind::Support);
    evaluator.action = c::TaskKind::Attack;
    for (int i = 0; i < 8; ++i) {
        world.command_state->nodes.front().schedule.next_think_tick = world.tick;
        REQUIRE(Step(world, {}, options, navigation));
        CHECK(assignment() == original);
        CHECK(evaluator.calls == 1);
    }
    const auto &messages = world.command_state->communications.messages;
    CHECK(std::ranges::count_if(messages, [](const auto &message) {
        const auto *task = std::get_if<c::CommandTask>(&message.payload);
        return task && task->recipient == c::NodeId{5} && task->action == c::TaskKind::Support;
    }) == 1);
}

TEST_CASE("runtime.three_command_levels_deliver_the_second_regiments_assignment_before_it_acts") {
    bool alternate_visible = true;
    bool manual_wing = false;
    bool lose_message = false;
    SUBCASE("a visible and reachable second enemy gets a separate attack") {}
    SUBCASE("the weaker wing stages in support and keeps that order when a new enemy appears") {
        alternate_visible = false;
    }
    SUBCASE("a manually held wing stays put") {
        manual_wing = true;
    }
    SUBCASE("a lost support dispatch is retried unchanged") {
        alternate_visible = false;
        lose_message = true;
    }
    SUBCASE("a lost alternate attack dispatch is retried unchanged") {
        lose_message = true;
    }

    auto scenario = TwoRegimentGroup();
    scenario.rules.local_melee = true;
    auto &setup = *scenario.command_setup;
    setup.hierarchy.nodes[0].parent = {1};
    setup.hierarchy.nodes[1].name = "Lead regiment";
    setup.hierarchy.nodes[2].name = "Wing regiment";
    c::CommandNode army;
    army.id = {1};
    army.level = c::Level::Army;
    army.commander = {4};
    army.children = {{3}};
    setup.hierarchy.nodes.insert(setup.hierarchy.nodes.begin(), army);
    c::OfficerProfile general;
    general.officer = army.commander;
    general.command_radius_m = 55.f;
    setup.hierarchy.officers.push_back(general);
    auto &primary = scenario.regiments[1];
    primary.pose.position.x = 2.f;
    for (auto &unit : scenario.units) {
        if (unit.regiment == primary.id)
            unit.pose = primary.pose;
        unit.weapon.damage = 0.f;
        unit.health = 100.f;
    }
    auto alternate = primary;
    alternate.id = {4};
    alternate.formation.columns = 1;
    alternate.pose.position = {1, alternate_visible ? 9.f : 90.f, 0};
    scenario.regiments.push_back(alternate);
    auto unit = scenario.units[4];
    unit.id = {10};
    unit.regiment = alternate.id;
    unit.pose = alternate.pose;
    scenario.units.push_back(unit);

    auto created = CreateWorld(scenario, 23);
    REQUIRE(created);
    auto &world = created.value;
    const auto wing_start = FindRegiment(world, {3})->pose.position;
    if (lose_message && !alternate_visible) {
        // Keep the original Support decision valid instead of turning it into a flank when the lead engages.
        Order hold_lead;
        hold_lead.recipient = {1};
        hold_lead.type = OrderType::HoldLine;
        hold_lead.priority = PlayerOrderPriority;
        hold_lead.point = FindRegiment(world, {1})->pose.position;
        REQUIRE(SubmitOrder(world, hold_lead) == Status::Ok);
    }
    if (manual_wing) {
        Order hold;
        hold.recipient = {3};
        hold.type = OrderType::HoldLine;
        hold.priority = PlayerOrderPriority;
        hold.point = wing_start;
        REQUIRE(SubmitOrder(world, hold) == Status::Ok);
    }
    c::CommandTask mission;
    mission.recipient = {1};
    mission.action = c::TaskKind::Attack;
    mission.intent.objective_position = {2, 0, 0};
    mission.intent.target_regiment = {2};
    mission.authority.initiative = c::InitiativePolicy::WithinIntent;
    mission.authority.may_pursue = true;
    mission.authority.may_change_target = true;
    const auto submitted = c::SubmitTask(world, mission);
    REQUIRE(submitted.status == c::StubStatus::Ready);
    REQUIRE(submitted.value);

    const GridNavigationService navigation({}, 1.f, 81, 61, {-40, -30, 0});
    auto &nodes = world.command_state->nodes;
    c::TaskId assigned{};
    std::optional<c::CommandTask> lost_task;
    Vec3 stage{};
    Tick delivered{};
    bool in_transit = false, issued_move = false, progressed = false;
    bool loss_confirmed = false;
    for (int tick = 0; tick < 150 && !progressed; ++tick) {
        REQUIRE(Step(world, {}, {}, navigation));
        const auto &group = nodes[1];
        const auto &wing = nodes[3];
        const auto &regiment = *FindRegiment(world, {3});
        const auto &army_orders = nodes[0].sent_tasks;
        const auto assigned_group = std::ranges::find(army_orders, c::NodeId{3}, &c::CommandTask::recipient);
        const auto main = std::ranges::find(group.sent_tasks, c::NodeId{4}, &c::CommandTask::recipient);
        const auto second = std::ranges::find(group.sent_tasks, c::NodeId{5}, &c::CommandTask::recipient);
        if (!assigned && assigned_group != army_orders.end() && assigned_group->parent_task == *submitted.value &&
            assigned_group->id == group.memory.active_task && main != group.sent_tasks.end() &&
            main->action == c::TaskKind::Attack &&
            second != group.sent_tasks.end()) {
            CHECK(main->intent.target_regiment == RegimentId{2});
            CHECK(group.memory.active_task == main->parent_task);
            CHECK(second->parent_task == main->parent_task);
            assigned = second->id;
            if (!manual_wing) {
                CHECK(std::ranges::any_of(group.memory.recent_decisions, [](const auto &decision) {
                    return decision.explanation.find("Lead regiment leads") != std::string::npos;
                }));
                const auto situation = c::BuildSituation(world, world.command_state->hierarchy, {3}, world.tick);
                REQUIRE(situation.value);
                const auto observed = std::ranges::find(situation.value->contacts, RegimentId{4},
                                                       &c::KnownContact::regiment);
                CHECK((observed != situation.value->contacts.end()) == alternate_visible);
                if (alternate_visible && observed != situation.value->contacts.end())
                    CHECK(FindAttackRoute(navigation, world, regiment, observed->estimated_pose,
                                          observed->regiment).status == Status::Ok);
                const auto expected = alternate_visible ? c::TaskKind::Attack : c::TaskKind::Support;
                REQUIRE(second->action == expected);
                CHECK(second->intent.target_regiment == RegimentId{alternate_visible ? 4u : 1u});
                if (alternate_visible)
                    CHECK(second->intent.objective_position == alternate.pose.position);
                else
                    CHECK(Distance(second->intent.objective_position, wing_start) > 1.f);
            }
            stage = second->intent.objective_position;
            if (lose_message) {
                lost_task = *second;
                auto &messages = world.command_state->communications.messages;
                const auto dispatch = std::ranges::find_if(messages, [&](const auto &message) {
                    const auto *task = std::get_if<c::CommandTask>(&message.payload);
                    return task && task->id == assigned &&
                        (message.status == c::DeliveryStatus::Queued || message.status == c::DeliveryStatus::InTransit);
                });
                REQUIRE(dispatch != messages.end());
                dispatch->status = c::DeliveryStatus::InTransit;
                dispatch->channel = c::DeliveryChannel::Courier;
                dispatch->courier = {999999}; // The simulation discovers the missing courier.
            } else if (!alternate_visible) {
                auto *enemy = FindRegiment(world, {4});
                auto *soldier = FindUnit(world, {10});
                enemy->pose.position.y = soldier->pose.position.y = 9.f;
                enemy->slots.front().position.y = 9.f;
            }
        }
        if (!assigned)
            continue;
        if (lost_task && assigned == lost_task->id) {
            CHECK(wing.memory.active_task != lost_task->id);
            loss_confirmed |= std::ranges::any_of(world.command_state->communications.messages, [&](const auto &message) {
                const auto *task = std::get_if<c::CommandTask>(&message.payload);
                return task && task->id == assigned && message.status == c::DeliveryStatus::Lost;
            });
            if (second != group.sent_tasks.end() && second->id != lost_task->id) {
                CHECK(second->action == lost_task->action);
                CHECK(second->recipient == lost_task->recipient);
                CHECK(second->intent == lost_task->intent);
                assigned = second->id;
            }
        }
        const auto received = std::ranges::find(wing.orders, assigned, &c::CommandTask::id);
        if (received == wing.orders.end()) {
            in_transit = true;
            CHECK(wing.memory.active_task != assigned);
            // Give the group a decision while its message is in transit.
            nodes[1].schedule.next_think_tick = world.tick;
        } else if (received->received_tick) {
            delivered = *received->received_tick;
        }
        if (manual_wing) {
            REQUIRE(regiment.order);
            CHECK(regiment.order->priority == PlayerOrderPriority);
            CHECK(regiment.pose.position == wing_start);
        } else {
            if (second != group.sent_tasks.end())
                REQUIRE(second->id == assigned);
            if (wing.memory.active_task == assigned && regiment.order && regiment.order->id == wing.last_order_id) {
                CHECK(delivered != 0);
                issued_move |= alternate_visible
                    ? regiment.order->type == OrderType::Attack && regiment.order->target_regiment == RegimentId{4}
                    : regiment.order->type == OrderType::MoveTo && regiment.order->point == stage;
                progressed |= issued_move && Distance(regiment.pose.position, wing_start) > 1.f;
            }
        }
    }
    CHECK(assigned);
    if (lose_message) {
        REQUIRE(lost_task);
        CHECK(loss_confirmed);
        CHECK(assigned != lost_task->id);
    }
    CHECK(in_transit);
    CHECK(delivered != 0);
    if (!manual_wing) {
        CHECK(issued_move);
        CHECK(progressed);
    }
}

TEST_CASE("runtime.independent_group_commander_leads_regiments_and_follows_their_movement") {
    auto scenario = CommandDuel(false);
    auto &setup = *scenario.command_setup;
    setup.hierarchy.nodes.erase(setup.hierarchy.nodes.begin(), setup.hierarchy.nodes.begin() + 2);
    setup.hierarchy.officers.erase(setup.hierarchy.officers.begin(), setup.hierarchy.officers.begin() + 2);
    auto &group = setup.hierarchy.nodes.front();
    group.parent = {};
    const UnitId commander_id{1000};
    scenario.officers[1].unit = commander_id;
    UnitSpec commander;
    commander.id = commander_id;
    commander.pose = {{-17, -3, 0}, 0};
    scenario.units.push_back(commander);
    c::CommandTask mission;
    mission.id = {1};
    mission.recipient = group.id;
    mission.action = c::TaskKind::Attack;
    mission.intent.target_regiment = {2};
    mission.intent.objective_position = {10, 0, 0};
    setup.missions.push_back(mission);

    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    REQUIRE(world.command_state);
    CHECK(world.regiments.front().units.size() == 4);
    CHECK_FALSE(FindUnit(world, commander_id)->regiment);
    FlatNavigationService navigation;
    for (int i = 0; i < 80; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(Distance(FindUnit(world, commander_id)->pose.position, commander.pose.position) > 2.f);
    CHECK(world.command_state->hierarchy.nodes.front().commander == group.commander);
    CHECK_FALSE(world.command_state->nodes.front().memory.recent_decisions.empty());
    auto restored = Restore(Capture(world));
    REQUIRE(restored);
    CHECK(StateHash(restored.value) == StateHash(world));
}


TEST_CASE("runtime.regiment_command_staff_effects_end_when_their_figures_are_lost") {
    auto scenario = CommandDuel(false);
    scenario.officers.front().charisma = 1.f;
    scenario.officers.front().lead_dist = 12.f;
    for (unsigned id = 9; id <= 10; ++id) {
        UnitSpec unit;
        unit.id = {id};
        unit.regiment = {1};
        unit.pose = scenario.regiments.front().pose;
        scenario.units.push_back(unit);
        scenario.officers.push_back({{id}, unit.id, id == 9 ? 3u : 4u, 1.f, 0.f, 1.f, 12.f});
    }
    auto &assignments = scenario.command_setup->hierarchy.support;
    assignments.push_back({{9}, {4}, c::SupportDuty::StandardBearer});
    assignments.push_back({{10}, {4}, c::SupportDuty::Musician});
    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    auto &world = created.value;
    const auto &regiment = world.regiments.front();
    const auto effects = [&] { return GetStaffEffects(world, regiment); };
    CHECK(effects().panic_fraction == doctest::Approx(.36f));
    CHECK(effects().calm_ticks == 70);
    FindUnit(world, {9})->alive = false;
    CHECK(effects().panic_fraction == doctest::Approx(.44f));
    CHECK(effects().calm_ticks == 70);
    FindUnit(world, {10})->pose.position.x += 20.f;
    CHECK(effects().calm_ticks == 100);
    FindUnit(world, {1})->alive = false;
    CHECK(effects().panic_fraction == doctest::Approx(.5f));
}

TEST_CASE("runtime.group_support_follows_its_friendly_lead_and_can_be_restored") {
    auto scenario = TwoRegimentGroup();
    c::CommandTask mission;
    mission.recipient = {3};
    mission.action = c::TaskKind::Attack;
    mission.intent.target_regiment = {2};
    mission.intent.objective_position = {10, 0, 0};
    scenario.command_setup->missions.push_back(mission);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &state = *world.command_state;

    const auto perception = c::BuildSituation(world, state.hierarchy, {5}, world.tick);
    REQUIRE(perception.value);
    const auto &contacts = perception.value->contacts;
    CHECK(std::any_of(contacts.begin(), contacts.end(), [](const auto &contact) {
        return contact.regiment == RegimentId{1} && contact.allegiance == c::Allegiance::Friendly;
    }));
    CHECK(std::none_of(contacts.begin(), contacts.end(), [](const auto &contact) {
        return contact.regiment == RegimentId{3};
    }));
    CHECK(perception.value->threats.size() == 1);

    const auto &follower = state.nodes.back();
    FlatNavigationService navigation;
    for (int i = 0; i < 65 && follower.memory.state != c::FsmState::Supporting; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(follower.memory.state == c::FsmState::Supporting);
    CHECK(Restore(Capture(world)));
}

TEST_CASE("runtime.support_requires_a_friendly_target_when_one_is_specified") {
    auto scenario = TwoRegimentGroup();
    c::CommandTask task;
    task.recipient = {3};
    task.action = c::TaskKind::Support;
    task.intent.target_regiment = {1};
    scenario.command_setup->missions.push_back(task);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    CHECK(c::SubmitTask(created.value, task).status == c::StubStatus::Ready);
    task.intent.target_regiment = {2};
    CHECK(c::SubmitTask(created.value, task).status == c::StubStatus::InvalidInput);
    task.action = c::TaskKind::Attack;
    task.intent.target_regiment = {1};
    CHECK(c::SubmitTask(created.value, task).status == c::StubStatus::InvalidInput);
}

TEST_CASE("runtime.completed_replacement_does_not_resume_same_parent_staging_order") {
    auto created = CreateWorld(TwoRegimentGroup(), 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &runtime = world.command_state->nodes.back();
    FlatNavigationService navigation;
    c::CommandTask task;
    task.issuer = {3};
    task.recipient = {5};
    task.action = c::TaskKind::MoveToArea;
    task.intent.objective_position = {-40, 0, 0};
    task.priority = 5;
    const auto staging = c::SubmitTask(world, task);
    REQUIRE(staging.value);
    REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(runtime.memory.active_task == *staging.value);

    task.intent.objective_position = FindRegiment(world, {3})->pose.position;
    const auto replacement = c::SubmitTask(world, task);
    REQUIRE(replacement.value);
    REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(runtime.memory.task_status == c::TaskStatus::Completed);
    for (int i = 0; i < 15; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(runtime.memory.active_task != *staging.value);
    const auto old = std::find_if(runtime.orders.begin(), runtime.orders.end(),
                                 [&](const auto &entry) { return entry.id == *staging.value; });
    REQUIRE(old != runtime.orders.end());
    CHECK(old->status == c::TaskStatus::Cancelled);
}

TEST_CASE("runtime.lower_priority_assignment_keeps_the_current_order") {
    auto created = CreateWorld(TwoRegimentGroup(), 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &runtime = world.command_state->nodes.back();
    FlatNavigationService navigation;
    c::CommandTask task;
    task.issuer = {3};
    task.recipient = {5};
    task.action = c::TaskKind::MoveToArea;
    task.intent.objective_position = {-45, 0, 0};
    task.priority = 5;
    const auto preferred = c::SubmitTask(world, task);
    REQUIRE(preferred.value);
    REQUIRE(Step(world, {}, {}, navigation));

    task.intent.objective_position = {-35, 0, 0};
    task.priority = 4;
    REQUIRE(c::SubmitTask(world, task).value);
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK(runtime.memory.active_task == *preferred.value);
    CHECK(runtime.memory.task_status == c::TaskStatus::Active);
}

TEST_CASE("runtime.withdrawal_breaks_contact_when_formation_cannot_turn_in_place") {
    auto scenario = test::Duel(12);
    bool shoreline = false;
    SUBCASE("blocked by the facing regiment") {}
    SUBCASE("blocked by riverbank before grid planning can turn") {
        shoreline = true;
        scenario.obstacles.push_back({-1.45f, 2.15f, 3.f, 6.f});
    }
    scenario.rules.local_melee = true;
    auto &blue = scenario.regiments.front();
    blue.pose = {{-1.7f, 0, 0}, .16f};
    blue.rally_point = {-8, 4, 0};
    scenario.regiments.back().pose = {{1.7f, 0, 0}, Pi};
    for (auto &regiment : scenario.regiments)
        regiment.formation = {4, 1.1f, .25f};
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.weapon.damage = 0.f;
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService navigation(scenario.obstacles, 1.f, 41, 41, {-20, -20, 0});
    if (shoreline) {
        CHECK(navigation.CanTraverse(blue.pose, blue.pose, Footprint(world.regiments.front())));
        CHECK(navigation.FindPath({blue.pose, blue.rally_point, Footprint(world.regiments.front())})
                  .status == Status::NoPath);
    }
    REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(world.regiments.front().melee_engaged);
    Order withdraw;
    withdraw.recipient = blue.id;
    withdraw.type = OrderType::Withdraw;
    withdraw.point = blue.rally_point;
    REQUIRE(SubmitOrder(world, withdraw) == Status::Ok);
    for (int i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(Distance(world.regiments.front().pose.position, blue.pose.position) > 2.f);
    CHECK_FALSE(world.regiments.front().melee_engaged);
}

namespace {
Scenario RecoveryGroup() {
    auto scenario = CommandDuel(false);
    auto &setup = *scenario.command_setup;
    setup.hierarchy.nodes.erase(setup.hierarchy.nodes.begin(), setup.hierarchy.nodes.begin() + 2);
    setup.hierarchy.officers.erase(setup.hierarchy.officers.begin(), setup.hierarchy.officers.begin() + 2);
    setup.hierarchy.nodes.front().parent = {};
    for (auto &profile : setup.hierarchy.officers)
        profile.command_radius_m = 12.f;
    scenario.rules.local_melee = true;
    for (auto &regiment : scenario.regiments) {
        regiment.pose.position.x = regiment.team ? 1.5f : -1.5f;
        regiment.formation = {2, 1.1f, .25f};
    }
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.health = 100.f;
        unit.weapon.damage = 0.f;
    }
    scenario.units[1].archer = true;
    scenario.units[1].weapon.range = 16.f;
    c::CommandTask mission;
    mission.id = {1};
    mission.recipient = {3};
    mission.action = c::TaskKind::Attack;
    mission.intent.target_regiment = {2};
    mission.intent.objective_position = scenario.regiments.back().pose.position;
    mission.authority.initiative = c::InitiativePolicy::WithinIntent;
    mission.authority.may_change_target = true;
    mission.authority.may_pursue = true;
    mission.authority.pursuit_limit_m = 20.f;
    setup.missions.push_back(mission);
    return scenario;
}

void MoveRegimentForObservation(World &world, RegimentId id, Vec3 position) {
    auto *regiment = FindRegiment(world, id);
    const auto delta = position - regiment->pose.position;
    regiment->pose.position = position;
    for (auto id : regiment->units)
        FindUnit(world, id)->pose.position = FindUnit(world, id)->pose.position + delta;
}
} // namespace

TEST_CASE("runtime.breaking_contact_retains_the_attack_and_reacquires_the_same_target") {
    auto created = CreateWorld(RecoveryGroup(), 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    for (int i = 0; i < 50; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(world.regiments.front().melee_engaged);
    REQUIRE(FindUnit(world, {2})->dagger_drawn);
    MoveRegimentForObservation(world, {2}, {19, 0, 0});
    for (int i = 0; i < 125; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK_FALSE(world.regiments.front().melee_engaged);
    CHECK_FALSE(FindUnit(world, {2})->dagger_drawn);
    auto &group = world.command_state->nodes.front();
    CHECK(group.orders.front().status == c::TaskStatus::Active);
    CHECK(group.memory.state == c::FsmState::Regrouping);
    CHECK_FALSE(group.memory.target);
    MoveRegimentForObservation(world, {2}, {5, 0, 0});
    const auto before = world.regiments.front().pose.position;
    for (int i = 0; i < 75; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(group.memory.target == RegimentId{2});
    CHECK(group.orders.front().status == c::TaskStatus::Active);
    CHECK(world.regiments.front().ai_action == OrderType::Attack);
    CHECK(world.regiments.front().ai_target == RegimentId{2});
    CHECK(Distance(before, world.regiments.front().pose.position) > 1.f);
}

TEST_CASE("runtime.defeated_enemy_is_replaced_by_a_reachable_enemy_within_attack_intent") {
    auto scenario = RecoveryGroup();
    auto enemy = scenario.regiments.back();
    enemy.id = {3};
    enemy.pose.position = {8, 2, 0};
    scenario.regiments.push_back(enemy);
    auto soldier = scenario.units.back();
    soldier.id = {9};
    soldier.regiment = {3};
    soldier.pose = enemy.pose;
    scenario.units.push_back(soldier);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    for (int i = 0; i < 50; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(world.command_state->nodes.front().memory.target == RegimentId{2});
    for (auto &unit : world.units)
        if (unit.regiment == RegimentId{2}) {
            unit.alive = false;
            unit.health = 0.f;
            unit.control = ControlMode::Dead;
        }
    FindRegiment(world, {2})->units.clear();
    for (int i = 0; i < 14; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    auto &group = world.command_state->nodes.front();
    CHECK(group.memory.target == RegimentId{3});
    CHECK(group.orders.front().status == c::TaskStatus::Active);
    CHECK(world.regiments.front().ai_target == RegimentId{3});
}


TEST_CASE("runtime.straggler_melee_does_not_pin_two_separated_regiments") {
    auto scenario = test::Duel(5);
    scenario.rules.local_melee = true;
    auto &blue = scenario.units.front();
    auto &orange = scenario.units[5];
    blue.archer = true;
    blue.weapon.range = 16.f;
    blue.pose.position = {-.38f, 0, 0};
    orange.pose.position = {.38f, 0, 0};
    auto &watching_archer = scenario.units[6];
    watching_archer.archer = true;
    watching_archer.weapon.range = 16.f;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK_FALSE(world.regiments[0].melee_engaged);
    CHECK_FALSE(world.regiments[1].melee_engaged);
    CHECK(FindUnit(world, blue.id)->dagger_drawn);
    CHECK(FindUnit(world, blue.id)->pose.position.x < blue.pose.position.x);
    CHECK(std::none_of(world.events.begin(), world.events.end(), [&](const auto &event) {
        return event.kind == EventKind::AttackLaunched && event.source == watching_archer.id &&
               event.target == blue.id && event.projectile;
    }));
}

TEST_CASE("runtime.retreat_steps_out_of_column_when_the_reserve_is_behind_it") {
    auto scenario = test::Duel(12);
    scenario.rules.local_melee = true;
    float offset = 0;
    SUBCASE("between grid cells") {}
    SUBCASE("on a grid cell") { offset = -.35f; }
    scenario.regiments[0].pose = {{-1.65f + offset, 0, 0}, 0};
    scenario.regiments[0].rally_point = {-12.f, 7.f, 0};
    scenario.regiments[1].pose = {{1.65f + offset, 0, 0}, Pi};
    for (auto &regiment : scenario.regiments)
        regiment.formation = {4, 1.1f, .25f};
    auto reserve = scenario.regiments[0];
    reserve.id = {3};
    reserve.pose.position.x = -4.36f + offset;
    scenario.regiments.push_back(reserve);
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.weapon.damage = 0;
    }
    for (size_t i = 0; i < 12; ++i) {
        auto unit = scenario.units[i];
        unit.id = {25 + i};
        unit.regiment = reserve.id;
        unit.pose = reserve.pose;
        scenario.units.push_back(unit);
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService grid({}, 1.f, 41, 41, {-20, -20, 0});
    REQUIRE(Step(world, {}, {}, grid));
    REQUIRE(world.regiments[0].melee_engaged);
    Order withdraw;
    withdraw.recipient = {1};
    withdraw.type = OrderType::Withdraw;
    withdraw.point = scenario.regiments[0].rally_point;
    REQUIRE(SubmitOrder(world, withdraw) == Status::Ok);
    Order relieve;
    relieve.recipient = {3};
    relieve.type = OrderType::Attack;
    relieve.target_regiment = {2};
    REQUIRE(SubmitOrder(world, relieve) == Status::Ok);
    float greatest_y = 0;
    bool reserve_attacked = false;
    bool saved_escape = false;
    for (int i = 0; i < 200; ++i) {
        REQUIRE(Step(world, {}, {}, grid));
        const auto &retreating = world.regiments[0];
        const auto &advancing = world.regiments[2];
        greatest_y = std::max(greatest_y, retreating.pose.position.y);
        if (!saved_escape && retreating.retreat_waypoint) {
            test::TempDir temp;
            const auto file = temp.path / "retreat.json";
            REQUIRE(io::SaveSnapshot(file, Capture(world)));
            const auto snapshot = io::LoadSnapshot(file);
            REQUIRE(snapshot);
            const auto restored = Restore(snapshot.value);
            REQUIRE(restored);
            CHECK(restored.value.regiments[0].retreat_waypoint == retreating.retreat_waypoint);
            CHECK(StateHash(restored.value) == StateHash(world));
            saved_escape = true;
        }
        for (const auto &event : world.events)
            reserve_attacked |= event.kind == EventKind::AttackLaunched && !event.projectile &&
                                event.source.value >= 25 && event.target.value >= 13 &&
                                event.target.value <= 24;
        CHECK_FALSE(RectanglesOverlap(retreating.pose, Footprint(retreating),
                                     advancing.pose, Footprint(advancing)));
    }
    CHECK(saved_escape);
    CHECK(greatest_y > 3.f);
    CHECK_FALSE(world.regiments[0].melee_engaged);
    CHECK(world.regiments[2].pose.position.x > reserve.pose.position.x + .25f);
    CHECK(reserve_attacked);
}
