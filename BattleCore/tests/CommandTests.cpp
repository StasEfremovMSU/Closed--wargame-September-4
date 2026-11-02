#include "CommandSupport.h"
#include "battle/Snapshot.h"
#include "doctest.h"
#include <limits>
using namespace battle;
namespace c = battle::command;
TEST_CASE("command.validation_rejects_broken_graph") {
    test::CommandFixture f;
    auto valid = c::ValidateHierarchy(f.hierarchy, f.world);
    REQUIRE(valid.status == c::StubStatus::Ready);
    REQUIRE(valid.value);
    CHECK(valid.value->issues.empty());
    auto bad = f.hierarchy;
    bad.nodes[0].parent = {4};
    CHECK(c::ValidateHierarchy(bad, f.world).status == c::StubStatus::InvalidInput);
    bad = f.hierarchy;
    bad.nodes[1].children.clear();
    CHECK(c::ValidateHierarchy(bad, f.world).status == c::StubStatus::InvalidInput);
    bad = f.hierarchy;
    bad.nodes[3].team = 1;
    CHECK(c::ValidateHierarchy(bad, f.world).status == c::StubStatus::InvalidInput);
    bad = f.hierarchy;
    bad.nodes[2].commander = bad.nodes[0].commander;
    CHECK(c::ValidateHierarchy(bad, f.world).status == c::StubStatus::InvalidInput);
    bad = f.hierarchy;
    bad.officers[0].caution = std::numeric_limits<float>::quiet_NaN();
    CHECK(c::ValidateHierarchy(bad, f.world).status == c::StubStatus::InvalidInput);
    bad = f.hierarchy;
    bad.officers[0].command_capacity = 0;
    CHECK(c::ValidateHierarchy(bad, f.world).status == c::StubStatus::InvalidInput);
    CHECK(c::ValidateHierarchy({}, f.world).status == c::StubStatus::InvalidInput);
}
TEST_CASE("command.observation_respects_range_and_obstacles") {
    test::CommandFixture f;
    auto before = StateHash(f.world);
    auto result = c::BuildSituation(f.world, f.hierarchy, {4}, 0);
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->own.living == 4);
    REQUIRE(result.value->contacts.size() == 1);
    CHECK(result.value->contacts[0].regiment.value == 2);
    CHECK(result.value->contacts[0].allegiance == c::Allegiance::Enemy);
    CHECK_FALSE(result.value->own.ammunition_fraction);
    CHECK(StateHash(f.world) == before);
    f.world.scenario.obstacles.push_back({-1, -10, 1, 10});
    result = c::BuildSituation(f.world, f.hierarchy, {4}, 0);
    REQUIRE(result.value);
    CHECK(result.value->contacts.empty());
    f.world.scenario.obstacles.clear();
    f.hierarchy.officers.back().command_radius_m = 5;
    result = c::BuildSituation(f.world, f.hierarchy, {4}, 0);
    REQUIRE(result.value);
    CHECK(result.value->contacts.empty());
    CHECK(c::BuildSituation(f.world, f.hierarchy, {999}, 0).status == c::StubStatus::InvalidInput);
}
TEST_CASE("command.successor_and_schedule") {
    test::CommandFixture f;
    auto node = f.hierarchy.nodes[0];
    node.succession = {{3}, {2}};
    auto result = c::SelectSuccessor(f.world, node);
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->value == 4);
    FindUnit(f.world, {4})->alive = false;
    result = c::SelectSuccessor(f.world, node);
    REQUIRE(result.value);
    CHECK(result.value->value == 3);
    f.world.officers[2].role = 3;
    result = c::SelectSuccessor(f.world, node);
    REQUIRE(result.value);
    CHECK(result.value->value == 2);
    std::vector<c::BrainSchedule> schedules{{{3}, 20, 5, 0}, {{1}, 0, 40, 0}, {{2}, 5, 20, 0}};
    auto due = c::ScheduleBrains(schedules, 5);
    REQUIRE(due.status == c::StubStatus::Ready);
    REQUIRE(due.value);
    REQUIRE(due.value->size() == 2);
    CHECK((*due.value)[0].value == 1);
    CHECK((*due.value)[1].value == 2);
    schedules.push_back(schedules.back());
    CHECK(c::ScheduleBrains(schedules, 5).status == c::StubStatus::InvalidInput);
}
TEST_CASE("command.neural_backend_remains_unavailable") {
    auto result = c::NeuralEvaluatorStub{}.Evaluate({});
    CHECK(result.status == c::StubStatus::NotImplemented);
    CHECK_FALSE(result.value);
}