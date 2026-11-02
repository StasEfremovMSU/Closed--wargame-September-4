#include "TestSupport.h"
#include "battle/Snapshot.h"
#include "battle/command/Brains.h"
#include "battle/command/Services.h"
#include "doctest.h"
#include <array>
using namespace battle;
namespace cmd = battle::command;
namespace {
template <class T> void Inactive(const cmd::StubResult<T> &result) {
    CHECK(result.status == cmd::StubStatus::NotImplemented);
    CHECK_FALSE(result.value.has_value());
    CHECK_FALSE(result.reason.empty());
}
void CheckBrains(const cmd::BrainContext &context) {
    Inactive(cmd::ArmyBrain{}.Think(context));
    Inactive(cmd::PartBrain{}.Think(context));
    Inactive(cmd::GroupBrain{}.Think(context));
    Inactive(cmd::RegimentBrain{}.Think(context));
}
} // namespace

TEST_CASE("command.brains_are_explicitly_inactive") {
    CheckBrains({});
    cmd::CommandNode node;
    node.id = {11};
    node.level = cmd::Level::Regiment;
    node.regiment = {1};
    cmd::OfficerProfile profile;
    profile.officer = {1};
    cmd::LocalSituation situation;
    situation.observer = node.id;
    situation.tick = 40;
    cmd::BrainMemory memory;
    std::array<cmd::CommandTask, 1> orders;
    orders[0].id = {7};
    orders[0].recipient = node.id;
    cmd::BrainContext context;
    context.tick = 40;
    context.node = &node;
    context.profile = &profile;
    context.situation = &situation;
    context.memory = &memory;
    context.orders = orders;
    CheckBrains(context);
    CHECK(orders[0].id.value == 7);
}

TEST_CASE("command.contracts_do_not_claim_completed_stages") {
    const std::array contracts{&cmd::ArmyBrain::Contract(), &cmd::PartBrain::Contract(),
                               &cmd::GroupBrain::Contract(), &cmd::RegimentBrain::Contract()};
    const std::array levels{cmd::Level::Army, cmd::Level::Part, cmd::Level::Group,
                            cmd::Level::Regiment};
    for (size_t i = 0; i < contracts.size(); ++i) {
        CHECK(contracts[i]->level == levels[i]);
        CHECK_FALSE(contracts[i]->responsibility.empty());
        REQUIRE_FALSE(contracts[i]->stages.empty());
        for (const auto &stage : contracts[i]->stages) {
            CHECK(stage.status == cmd::StubStatus::NotImplemented);
            CHECK_FALSE(stage.purpose.empty());
        }
    }
}

TEST_CASE("command.evaluators_do_not_fabricate_scores") {
    cmd::RulesEvaluatorStub rules;
    cmd::NeuralEvaluatorStub neural;
    const std::array<const cmd::ITacticalEvaluator *, 2> evaluators{&rules, &neural};
    const std::array features{1.f, .5f, .25f};
    std::array<cmd::TacticalOption, 1> options;
    options[0].action = cmd::TaskKind::HoldArea;
    cmd::EvaluationRequest request;
    request.features = features;
    request.options = options;
    for (auto *evaluator : evaluators) {
        Inactive(evaluator->Evaluate({}));
        Inactive(evaluator->Evaluate(request));
    }
}

TEST_CASE("command.services_do_not_validate_or_consume") {
    auto world = CreateWorld(test::Duel(), 42).value;
    cmd::CommandHierarchy hierarchy;
    // Deliberately contradictory: a self-parented army bound to a missing regiment.
    cmd::CommandNode node;
    node.id = {1};
    node.parent = node.id;
    node.level = cmd::Level::Army;
    node.regiment = {999};
    hierarchy.nodes.push_back(node);
    Inactive(cmd::ValidateHierarchy(hierarchy, world));
    Inactive(cmd::ValidateHierarchy({}, world));
    Inactive(cmd::BuildSituation(world, hierarchy, {999}, 40));
    Inactive(cmd::SelectSuccessor(world, node));
    Inactive(cmd::SelectSuccessor(world, {}));

    cmd::CommunicationsState communications;
    cmd::CommandTask task;
    task.id = {7};
    task.issuer = {1};
    task.recipient = {2};
    task.action = cmd::TaskKind::HoldArea;
    cmd::MessageEnvelope message;
    message.id = {9};
    message.sender = task.issuer;
    message.recipient = task.recipient;
    message.delivery_tick = 40;
    message.payload = task;
    communications.messages.push_back(message);
    Inactive(cmd::AdvanceCommunications(communications, 40));
    REQUIRE(communications.messages.size() == 1);
    CHECK(communications.messages[0].status == cmd::DeliveryStatus::Queued);
    CHECK(communications.messages[0].delivery_tick == 40);
    CHECK(std::get<cmd::CommandTask>(communications.messages[0].payload).id.value == 7);
    std::array<cmd::BrainSchedule, 1> schedules;
    schedules[0].node = {2};
    schedules[0].next_think_tick = 0;
    Inactive(cmd::ScheduleBrains(schedules, 40));
    CHECK(schedules[0].next_think_tick == 0);
    Inactive(cmd::TranslateRegimentTask(task, 40));
    task.action = cmd::TaskKind::Flank;
    Inactive(cmd::TranslateRegimentTask(task, 40));
    Inactive(cmd::TranslateRegimentTask({}, 0));
}

TEST_CASE("command.scaffold_does_not_change_battle") {
    auto scenario = test::Duel(3);
    scenario.officers = {{{1}, {1}, 2}, {{2}, {4}, 2}};
    auto actual = CreateWorld(scenario, 42).value;
    auto reference = actual;
    FlatNavigationService nav;
    cmd::CommandHierarchy hierarchy;
    cmd::CommunicationsState communications;
    cmd::BrainContext context;
    for (unsigned i = 0; i < 40; ++i) {
        const auto before = StateHash(actual);
        context.tick = actual.tick;
        CheckBrains(context);
        Inactive(cmd::ValidateHierarchy(hierarchy, actual));
        Inactive(cmd::BuildSituation(actual, hierarchy, {}, actual.tick));
        Inactive(cmd::SelectSuccessor(actual, {}));
        Inactive(cmd::AdvanceCommunications(communications, actual.tick));
        Inactive(cmd::ScheduleBrains({}, actual.tick));
        Inactive(cmd::TranslateRegimentTask({}, actual.tick));
        CHECK(StateHash(actual) == before);
        REQUIRE(Step(actual, {}, {}, nav));
        REQUIRE(Step(reference, {}, {}, nav));
        CHECK(StateHash(actual) == StateHash(reference));
    }
}
