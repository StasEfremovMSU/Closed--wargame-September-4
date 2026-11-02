#include "CommandSupport.h"
#include "doctest.h"
using namespace battle;
namespace c = battle::command;
TEST_CASE("part_fsm.frontage_reserve_and_threat") {
    test::UpperBrainFixture f(c::Level::Part);
    auto result = c::PartBrain{}.Think(f.Context());
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    REQUIRE(result.value->subordinate_tasks.size() == 3);
    CHECK(result.value->subordinate_tasks[0].intent.objective_position !=
          result.value->subordinate_tasks[1].intent.objective_position);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Reserve);
    f.memory = *result.value->next_memory;
    f.statuses[0].panic = true;
    result = c::PartBrain{}.Think(f.Context(20));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Supporting);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Support);
    CHECK(std::any_of(result.value->reports.begin(), result.value->reports.end(),
                      [](auto &r) { return r.kind == c::ReportKind::NeedSupport; }));
}
TEST_CASE("part_fsm.expiry_loss_and_withdrawal") {
    test::UpperBrainFixture f(c::Level::Part);
    f.tasks[0].action = c::TaskKind::Withdraw;
    auto result = c::PartBrain{}.Think(f.Context());
    REQUIRE(result.value);
    for (auto &t : result.value->subordinate_tasks)
        CHECK(t.action == c::TaskKind::Withdraw);
    f.tasks[0].expiry_tick = 1;
    result = c::PartBrain{}.Think(f.Context(1));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->task_status == c::TaskStatus::Cancelled);
    for (auto &t : result.value->subordinate_tasks)
        CHECK(t.action == c::TaskKind::HoldArea);
    for (auto &s : f.statuses)
        s.living = 0;
    result = c::PartBrain{}.Think(f.Context(2));
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks.empty());
    CHECK(c::PartBrain{}.Think({}).status == c::StubStatus::InvalidInput);
}
