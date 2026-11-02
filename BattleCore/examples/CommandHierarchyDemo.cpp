#include "battle/World.h"
#include "battle/command/Services.h"
#include <array>
#include <iostream>

namespace cmd = battle::command;
using namespace battle;
namespace {
template <class T> bool Show(std::string_view name, const cmd::StubResult<T> &result) {
    const bool inactive =
        result.status == cmd::StubStatus::NotImplemented && !result.value.has_value();
    std::cout << name << ": " << (inactive ? "NOT_IMPLEMENTED" : "UNEXPECTED_OUTPUT") << " - "
              << result.reason << '\n';
    return inactive;
}
Scenario ExampleBattle() {
    Scenario scenario;
    for (unsigned side = 0; side < 2; ++side) {
        RegimentSpec regiment;
        regiment.id = {side + 1};
        regiment.team = int32_t(side);
        regiment.pose.position.x = side ? 10.f : -10.f;
        regiment.formation.columns = 4;
        scenario.regiments.push_back(regiment);
        for (unsigned i = 0; i < 4; ++i) {
            UnitSpec unit;
            unit.id = {side * 4 + i + 1};
            unit.regiment = regiment.id;
            unit.pose = regiment.pose;
            scenario.units.push_back(unit);
        }
    }
    // Four different officers share a physical regiment, not a physical body.
    // Their command positions are independent of their legacy numeric roles.
    for (unsigned i = 0; i < 4; ++i)
        scenario.officers.push_back({{i + 1}, {i + 1}, i == 0 ? 2u : 1u});
    return scenario;
}
cmd::CommandHierarchy ExampleHierarchy() {
    cmd::CommandHierarchy hierarchy;
    const std::array levels{cmd::Level::Army, cmd::Level::Part, cmd::Level::Group,
                            cmd::Level::Regiment};
    const std::array names{"Example army", "Center", "Infantry group", "First regiment"};
    for (unsigned i = 0; i < levels.size(); ++i) {
        cmd::CommandNode node;
        node.id = {i + 1};
        node.level = levels[i];
        node.name = names[i];
        node.commander = {4 - i};
        if (i)
            node.parent = {i};
        if (i + 1 < levels.size())
            node.children.push_back({i + 2});
        else
            node.regiment = {1};
        hierarchy.nodes.push_back(node);
        cmd::OfficerProfile profile;
        profile.officer = node.commander;
        hierarchy.officers.push_back(profile);
    }
    return hierarchy;
}
} // namespace

int main() {
    auto created = CreateWorld(ExampleBattle(), 42);
    if (!created) {
        std::cerr << "Example world construction failed.\n";
        return 1;
    }
    const auto &world = created.value;
    const auto hierarchy = ExampleHierarchy();
    const std::array contracts{&cmd::ArmyBrain::Contract(), &cmd::PartBrain::Contract(),
                               &cmd::GroupBrain::Contract(), &cmd::RegimentBrain::Contract()};
    bool ok = true;
    std::cout << "Command hierarchy scaffold. No tactical decisions are executed.\n";
    for (size_t i = 0; i < contracts.size(); ++i) {
        const auto &contract = *contracts[i];
        std::cout << contract.name << " / " << hierarchy.nodes[i].name << ": "
                  << contract.responsibility << " (" << contract.stages.size()
                  << " pending stages)\n";
        for (const auto &stage : contract.stages)
            ok = stage.status == cmd::StubStatus::NotImplemented && ok;
        cmd::BrainContext context;
        context.node = &hierarchy.nodes[i];
        context.profile = &hierarchy.officers[i];
        switch (contract.level) {
        case cmd::Level::Army:
            ok = Show("  Think", cmd::ArmyBrain{}.Think(context)) && ok;
            break;
        case cmd::Level::Part:
            ok = Show("  Think", cmd::PartBrain{}.Think(context)) && ok;
            break;
        case cmd::Level::Group:
            ok = Show("  Think", cmd::GroupBrain{}.Think(context)) && ok;
            break;
        case cmd::Level::Regiment:
            ok = Show("  Think", cmd::RegimentBrain{}.Think(context)) && ok;
            break;
        }
    }
    ok = Show("ValidateHierarchy", cmd::ValidateHierarchy(hierarchy, world)) && ok;
    ok = Show("BuildSituation", cmd::BuildSituation(world, hierarchy, {4}, 0)) && ok;
    ok = Show("SelectSuccessor", cmd::SelectSuccessor(world, hierarchy.nodes[0])) && ok;
    cmd::CommunicationsState communications;
    ok = Show("AdvanceCommunications", cmd::AdvanceCommunications(communications, 0)) && ok;
    std::array<cmd::BrainSchedule, 4> schedules;
    for (size_t i = 0; i < schedules.size(); ++i)
        schedules[i].node = hierarchy.nodes[i].id;
    ok = Show("ScheduleBrains", cmd::ScheduleBrains(schedules, 0)) && ok;
    cmd::CommandTask task;
    task.id = {1};
    task.issuer = {3};
    task.recipient = {4};
    ok = Show("TranslateRegimentTask", cmd::TranslateRegimentTask(task, 0)) && ok;
    ok = Show("RulesEvaluator", cmd::RulesEvaluatorStub{}.Evaluate({})) && ok;
    ok = Show("NeuralEvaluator", cmd::NeuralEvaluatorStub{}.Evaluate({})) && ok;
    std::cout << (ok ? "All placeholders are inactive.\n" : "Scaffold contract violated.\n");
    return ok ? 0 : 1;
}
