#include "CommandSupport.h"
#include "battle/Snapshot.h"
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

struct NoPathNavigation final : NavigationService {
    PathResult FindPath(const PathRequest &) const override {
        return {Status::NoPath, {}};
    }
};
} // namespace

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
