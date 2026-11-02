#include "battle/Simulation.h"
#include "battle/command/Runtime.h"
#include <array>
#include <iostream>

namespace cmd = battle::command;
using namespace battle;

namespace {
cmd::CommandSetup ExampleSetup() {
    cmd::CommandSetup setup;
    const std::array levels{cmd::Level::Army, cmd::Level::Part, cmd::Level::Group,
                            cmd::Level::Regiment};
    const std::array names{"Example army", "Center", "Infantry group", "First regiment"};
    for (unsigned i = 0; i < levels.size(); ++i) {
        cmd::CommandNode node;
        node.id = {i + 1};
        node.level = levels[i];
        node.name = names[i];
        node.commander = {4 - i};
        node.team = 0;
        if (i)
            node.parent = {i};
        if (i + 1 < levels.size())
            node.children.push_back({i + 2});
        else
            node.regiment = {1};
        setup.hierarchy.nodes.push_back(node);
        cmd::OfficerProfile profile;
        profile.officer = node.commander;
        profile.tactical_skill = .7f;
        profile.initiative = .7f;
        profile.discipline = .8f;
        profile.aggression = .8f;
        profile.command_radius_m = 60;
        if (node.level == cmd::Level::Regiment)
            profile.initiative = 0;
        setup.hierarchy.officers.push_back(profile);
    }
    cmd::CommandTask mission;
    mission.id = {1};
    mission.recipient = {1};
    mission.action = cmd::TaskKind::Attack;
    mission.intent.target_regiment = {2};
    mission.intent.objective_position = {10, 0, 0};
    mission.status = cmd::TaskStatus::Active;
    setup.missions.push_back(mission);
    return setup;
}

Scenario ExampleBattle() {
    Scenario scenario;
    for (unsigned side = 0; side < 2; ++side) {
        RegimentSpec regiment;
        regiment.id = {side + 1};
        regiment.team = int32_t(side);
        regiment.pose.position.x = side ? 10.f : -10.f;
        regiment.formation.columns = 4;
        regiment.speed = 2;
        scenario.regiments.push_back(regiment);
        for (unsigned i = 0; i < 4; ++i) {
            UnitSpec unit;
            unit.id = {side * 4 + i + 1};
            unit.regiment = regiment.id;
            unit.pose = regiment.pose;
            scenario.units.push_back(unit);
        }
    }
    for (unsigned i = 0; i < 4; ++i)
        scenario.officers.push_back({{i + 1}, {i + 1}, i == 0 ? 2u : 1u});
    scenario.command_setup = ExampleSetup();
    return scenario;
}

const char *StateName(cmd::FsmState state) {
    switch (state) {
    case cmd::FsmState::Idle:
        return "Idle";
    case cmd::FsmState::Deploying:
        return "Deploying";
    case cmd::FsmState::Holding:
        return "Holding";
    case cmd::FsmState::Advancing:
        return "Advancing";
    case cmd::FsmState::Attacking:
        return "Attacking";
    case cmd::FsmState::Supporting:
        return "Supporting";
    case cmd::FsmState::Regrouping:
        return "Regrouping";
    case cmd::FsmState::Withdrawing:
        return "Withdrawing";
    case cmd::FsmState::Rallying:
        return "Rallying";
    case cmd::FsmState::Reserve:
        return "Reserve";
    case cmd::FsmState::Failed:
        return "Failed";
    case cmd::FsmState::Complete:
        return "Complete";
    }
    return "Unknown";
}
} // namespace

int main() {
    auto created = CreateWorld(ExampleBattle(), 42);
    if (!created || !created.value.command_state) {
        std::cerr << "Could not initialize the command battle.\n";
        return 1;
    }
    auto world = std::move(created.value);
    GridNavigationService navigation(world.scenario.obstacles);
    bool hit = false;
    for (unsigned tick = 0; tick < 400 && !hit; ++tick) {
        auto result = Step(world, {}, {}, navigation);
        if (result.status != Status::Ok) {
            std::cerr << "Simulation rejected tick " << tick << ".\n";
            return 1;
        }
        hit = std::any_of(result.value.events.begin(), result.value.events.end(),
                          [](const auto &event) { return event.kind == EventKind::Hit; });
    }
    std::cout << "Four-level commander runtime: " << (hit ? "hit confirmed" : "no hit") << '\n';
    for (const auto &runtime : world.command_state->nodes)
        std::cout << "Node " << runtime.node.value << ": " << StateName(runtime.memory.state)
                  << ", decisions=" << runtime.memory.recent_decisions.size() << '\n';
    return hit ? 0 : 1;
}
