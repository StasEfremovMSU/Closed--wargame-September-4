#include "CommandSupport.h"
#include "doctest.h"
using namespace battle;
namespace c = battle::command;
TEST_CASE("group_fsm.deploys_then_attacks") {
    test::UpperBrainFixture f;
    auto result = c::GroupBrain{}.Think(f.Context());
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    REQUIRE(result.value->next_memory);
    CHECK(result.value->next_memory->state == c::FsmState::Deploying);
    REQUIRE(result.value->subordinate_tasks.size() == 3);
    CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::MoveToArea);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Reserve);
    for (size_t i = 0; i < 3; ++i)
        f.statuses[i].pose.position = result.value->subordinate_tasks[i].intent.objective_position;
    f.memory = *result.value->next_memory;
    result = c::GroupBrain{}.Think(f.Context(1));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Attacking);
    CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::Attack);
    CHECK(result.value->subordinate_tasks[1].action == c::TaskKind::Support);
    CHECK(result.value->subordinate_tasks[1].intent.target_regiment.value == 100);
}
TEST_CASE("group_fsm.reserve_support_and_withdraw") {
    test::UpperBrainFixture f;
    f.memory.state = c::FsmState::Deploying;
    f.memory.active_task = {1};
    f.memory.task_status = c::TaskStatus::Active;
    f.statuses[0].panic = true;
    auto result = c::GroupBrain{}.Think(f.Context(41));
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks[2].action != c::TaskKind::Reserve);
    f.tasks[0].id = {2};
    f.tasks[0].action = c::TaskKind::Withdraw;
    result = c::GroupBrain{}.Think(f.Context(42));
    REQUIRE(result.value);
    for (auto &t : result.value->subordinate_tasks)
        CHECK(t.action == c::TaskKind::Withdraw);
    f.tasks[0].intent.target_regiment = {100};
    f.tasks[0].id = {3};
    f.tasks[0].action = c::TaskKind::Attack;
    result = c::GroupBrain{}.Think(f.Context(43));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Failed);
}
TEST_CASE("group_fsm.empty_and_reversed_children") {
    test::UpperBrainFixture f;
    auto first = c::GroupBrain{}.Think(f.Context());
    REQUIRE(first.value);
    std::reverse(f.children.begin(), f.children.end());
    auto reverse = c::GroupBrain{}.Think(f.Context());
    REQUIRE(reverse.value);
    CHECK(reverse.value->subordinate_tasks == first.value->subordinate_tasks);
    f.children.clear();
    f.statuses.clear();
    f.node.children.clear();
    auto empty = c::GroupBrain{}.Think(f.Context());
    REQUIRE(empty.value);
    CHECK(empty.value->next_memory->state == c::FsmState::Failed);
    CHECK(empty.value->subordinate_tasks.empty());
}
TEST_CASE("group_fsm.commitment_delays_child_attack") {
    test::UpperBrainFixture f;
    f.profile.minimum_commitment_ticks = 100;
    auto deploy = c::GroupBrain{}.Think(f.Context());
    REQUIRE(deploy.value);
    f.memory = *deploy.value->next_memory;
    for (size_t i = 0; i < 3; ++i)
        f.statuses[i].pose.position = deploy.value->subordinate_tasks[i].intent.objective_position;
    auto wait = c::GroupBrain{}.Think(f.Context(1));
    REQUIRE(wait.value);
    CHECK(wait.value->next_memory->state == c::FsmState::Deploying);
    CHECK(wait.value->subordinate_tasks[0].action == c::TaskKind::MoveToArea);
}
TEST_CASE("group_fsm.preserves_assigned_frontage_and_flank") {
    test::UpperBrainFixture f;
    f.tasks[0].intent.objective_position = {30, 20, 0};
    auto plan = c::GroupBrain{}.Think(f.Context());
    REQUIRE(plan.value);
    CHECK(plan.value->next_memory->goal.y > 5);
    f.tasks[0].action = c::TaskKind::Flank;
    f.memory = *plan.value->next_memory;
    plan = c::GroupBrain{}.Think(f.Context(41));
    REQUIRE(plan.value);
    CHECK(plan.value->subordinate_tasks[0].action == c::TaskKind::Flank);
}
