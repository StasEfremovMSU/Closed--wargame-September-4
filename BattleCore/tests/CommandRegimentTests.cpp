#include "CommandSupport.h"
#include "doctest.h"
using namespace battle;
namespace c = battle::command;
namespace {
struct BrainFixture : test::CommandFixture {
    c::LocalSituation situation;
    c::BrainMemory memory;
    std::vector<c::CommandTask> tasks;
    BrainFixture() {
        situation = *c::BuildSituation(world, hierarchy, {4}, 0).value;
        c::CommandTask task;
        task.id = {1};
        task.issuer = {3};
        task.recipient = {4};
        task.status = c::TaskStatus::Active;
        task.action = c::TaskKind::HoldArea;
        task.intent.objective_position = situation.own.pose.position;
        tasks.push_back(task);
    }
    c::BrainContext Context(Tick tick = 0) {
        situation.tick = tick;
        c::BrainContext context;
        context.tick = tick;
        context.node = &hierarchy.nodes.back();
        context.profile = &hierarchy.officers.back();
        context.situation = &situation;
        context.memory = &memory;
        context.orders = tasks;
        context.current_formation = world.regiments[0].formation;
        return context;
    }
    c::DecisionProposal Think(Tick tick = 0) {
        auto result = c::RegimentBrain{}.Think(Context(tick));
        REQUIRE(result.status == c::StubStatus::Ready);
        REQUIRE(result.value);
        REQUIRE(result.value->next_memory);
        memory = *result.value->next_memory;
        return *result.value;
    }
};
} // namespace
TEST_CASE("regiment_fsm.hold_move_completion_and_reports") {
    BrainFixture f;
    auto hold = f.Think();
    REQUIRE(hold.execution_order);
    CHECK(hold.execution_order->type == OrderType::Hold);
    CHECK(f.memory.state == c::FsmState::Holding);
    f.tasks[0].id = {2};
    f.tasks[0].action = c::TaskKind::MoveToArea;
    f.tasks[0].intent.objective_position = {-5, 0, 0};
    auto move = f.Think(1);
    REQUIRE(move.execution_order);
    CHECK(move.execution_order->type == OrderType::MoveTo);
    f.situation.own.pose.position = {-5, 0, 0};
    auto complete = f.Think(2);
    CHECK(f.memory.state == c::FsmState::Complete);
    CHECK(f.memory.task_status == c::TaskStatus::Completed);
    CHECK(std::any_of(complete.reports.begin(), complete.reports.end(),
                      [](auto &r) { return r.kind == c::ReportKind::TaskCompleted; }));
    auto repeat = f.Think(3);
    CHECK(repeat.reports.empty());
}
TEST_CASE("regiment_fsm.attack_loss_panic_and_authority") {
    BrainFixture f;
    f.tasks[0].action = c::TaskKind::Attack;
    f.tasks[0].intent.target_regiment = {2};
    auto attack = f.Think();
    REQUIRE(attack.execution_order);
    CHECK(attack.execution_order->type == OrderType::Attack);
    CHECK(attack.execution_order->target_regiment.value == 2);
    f.situation.contacts.clear();
    auto lost = f.Think(1);
    CHECK(f.memory.state == c::FsmState::Failed);
    REQUIRE(lost.execution_order);
    CHECK(lost.execution_order->type == OrderType::Hold);
    f.situation.own.panic = true;
    auto panic = f.Think(2);
    CHECK(f.memory.state == c::FsmState::Withdrawing);
    REQUIRE(panic.execution_order);
    CHECK(panic.execution_order->type == OrderType::Withdraw);
    CHECK(panic.execution_order->point == f.situation.rally_point);
    BrainFixture strict;
    strict.hierarchy.officers.back().aggression = 1;
    strict.hierarchy.officers.back().initiative = 1;
    auto defence = strict.Think();
    CHECK(defence.execution_order->type == OrderType::Hold);
}
TEST_CASE("regiment_fsm.priority_time_and_new_order_interrupt") {
    BrainFixture f;
    f.hierarchy.officers.back().minimum_commitment_ticks = 100;
    f.Think();
    auto task = f.tasks[0];
    task.id = {2};
    task.priority = 10;
    task.action = c::TaskKind::MoveToArea;
    task.intent.objective_position = {0, 5, 0};
    task.not_before_tick = 5;
    f.tasks.push_back(task);
    CHECK(f.Think(4).execution_order->type == OrderType::Hold);
    CHECK(f.Think(5).execution_order->type == OrderType::MoveTo);
    f.tasks[1].expiry_tick = 6;
    f.Think(6);
    CHECK(f.memory.active_task.value == 1); // Lower still-active order resumes.
    f.tasks.resize(1);
    f.tasks[0].expiry_tick = 7;
    f.Think(7);
    CHECK(f.memory.task_status == c::TaskStatus::Cancelled);
}
TEST_CASE("regiment_fsm.flank_and_reform") {
    BrainFixture f;
    f.tasks[0].action = c::TaskKind::Flank;
    f.tasks[0].intent.target_regiment = {2};
    auto flank = f.Think();
    REQUIRE(flank.execution_order);
    CHECK(flank.execution_order->type == OrderType::MoveTo);
    CHECK(flank.execution_order->point.y != 0);
    f.situation.own.pose.position = flank.execution_order->point;
    auto attack = f.Think(1);
    CHECK(attack.execution_order->type == OrderType::Attack);
    BrainFixture reform;
    reform.tasks[0].action = c::TaskKind::Reform;
    reform.Think();
    CHECK(reform.memory.state == c::FsmState::Failed);
    reform.tasks[0].id = {2};
    reform.tasks[0].formation_columns = 2;
    auto proposal = reform.Think(1);
    REQUIRE(proposal.regiment_directives);
    REQUIRE(proposal.regiment_directives->formation);
    CHECK(proposal.regiment_directives->formation->columns == 2);
}
TEST_CASE("regiment_fsm.rejects_bad_context_and_scores_legal_options") {
    CHECK(c::RegimentBrain{}.Think({}).status == c::StubStatus::InvalidInput);
    BrainFixture f;
    auto context = f.Context();
    context.node = &f.hierarchy.nodes[0];
    CHECK(c::RegimentBrain{}.Think(context).status == c::StubStatus::InvalidInput);
    c::TacticalOption option;
    option.action = c::TaskKind::Attack;
    std::vector<c::TacticalOption> options{option};
    const std::vector<float> features{4, 2, 10};
    c::EvaluationRequest request;
    request.feature_schema = 1;
    request.features = features;
    request.options = options;
    auto scores = c::RulesEvaluatorStub{}.Evaluate(request);
    REQUIRE(scores.status == c::StubStatus::Ready);
    REQUIRE(scores.value);
    REQUIRE(scores.value->size() == 1);
    CHECK((*scores.value)[0].option_index == 0);
    CHECK(std::isfinite((*scores.value)[0].utility));
}
