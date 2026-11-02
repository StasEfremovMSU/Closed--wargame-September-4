#include "CommandSupport.h"
#include "doctest.h"
using namespace battle;
namespace c = battle::command;

TEST_CASE("army_fsm.mission_balance_and_lost_target") {
    test::UpperBrainFixture f(c::Level::Army);

    f.tasks[0].action = c::TaskKind::HoldArea;
    auto result = c::ArmyBrain{}.Think(f.Context());
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks.size() == 3);
    for (const auto &task : result.value->subordinate_tasks)
        CHECK((task.action == c::TaskKind::HoldArea || task.action == c::TaskKind::Reserve));

    f.tasks[0].action = c::TaskKind::Attack;
    f.tasks[0].intent.target_regiment = {2};
    f.tasks[0].intent.objective_position = {30, 0, 0};
    f.tasks[0].id = {2};
    result = c::ArmyBrain{}.Think(f.Context(1));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Advancing);

    f.situation.contacts.clear();
    result = c::ArmyBrain{}.Think(f.Context(2));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Failed);
    for (const auto &task : result.value->subordinate_tasks)
        CHECK(task.action == c::TaskKind::HoldArea);
}

TEST_CASE("army_fsm.defend_reserve_release_and_safe_empty") {
    test::UpperBrainFixture f(c::Level::Army);
    f.tasks.clear();
    f.situation.contacts.clear();
    auto result = c::ArmyBrain{}.Think(f.Context());
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Holding);

    c::KnownContact contact;
    contact.regiment = {2};
    contact.team = 1;
    contact.allegiance = c::Allegiance::Enemy;
    contact.estimated_pose.position = {30, 0, 0};
    contact.estimated_strength = 30;
    contact.confidence = 1;
    f.situation.contacts.push_back(contact);
    result = c::ArmyBrain{}.Think(f.Context(1));
    REQUIRE(result.value);
    CHECK((result.value->next_memory->state == c::FsmState::Attacking ||
           result.value->next_memory->state == c::FsmState::Advancing));
    REQUIRE(result.value->subordinate_tasks.size() == 3);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Reserve);

    f.memory = *result.value->next_memory;
    f.situation.own.living = 75;
    f.situation.own.initial = 100;
    result = c::ArmyBrain{}.Think(f.Context(2));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Supporting);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Support);

    test::UpperBrainFixture panic(c::Level::Army);
    panic.tasks.clear();
    panic.statuses[0].panic = true;
    result = c::ArmyBrain{}.Think(panic.Context());
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Supporting);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Support);

    f.children.clear();
    f.statuses.clear();
    f.node.children.clear();
    result = c::ArmyBrain{}.Think(f.Context(3));
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks.empty());
    CHECK(c::ArmyBrain{}.Think({}).status == c::StubStatus::InvalidInput);
}
