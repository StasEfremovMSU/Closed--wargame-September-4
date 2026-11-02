#pragma once
#include "TestSupport.h"
#include "battle/command/Services.h"
namespace test {
struct CommandFixture {
    battle::World world;
    battle::command::CommandHierarchy hierarchy;
    CommandFixture() {
        namespace c = battle::command;
        auto scenario = Duel(4);
        for (unsigned i = 0; i < 4; ++i)
            scenario.officers.push_back({{i + 1}, {i + 1}, i ? 1u : 2u});
        world = battle::CreateWorld(scenario, 42).value;
        for (unsigned i = 0; i < 4; ++i) {
            c::CommandNode node;
            node.id = {i + 1};
            node.level = c::Level(i);
            node.parent = {i};
            node.commander = {4 - i};
            if (i < 3)
                node.children.push_back({i + 2});
            else
                node.regiment = {1};
            hierarchy.nodes.push_back(node);
            c::OfficerProfile profile;
            profile.officer = node.commander;
            profile.tactical_skill = .5f;
            profile.discipline = 1;
            profile.command_radius_m = 60;
            hierarchy.officers.push_back(profile);
        }
    }
};
} // namespace test
