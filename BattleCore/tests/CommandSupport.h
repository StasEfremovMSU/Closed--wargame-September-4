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
struct UpperBrainFixture {
    battle::command::CommandNode node;
    battle::command::OfficerProfile profile;
    battle::command::LocalSituation situation;
    battle::command::BrainMemory memory;
    std::vector<battle::command::CommandNode> children;
    std::vector<battle::command::ChildStatus> statuses;
    std::vector<battle::command::CommandTask> tasks;
    explicit UpperBrainFixture(battle::command::Level level = battle::command::Level::Group) {
        namespace c = battle::command;
        node.id = {20};
        node.level = level;
        node.commander = {1};
        if (level != c::Level::Army)
            node.parent = {10};
        profile.officer = {1};
        profile.tactical_skill = .5f;
        profile.aggression = .5f;
        profile.discipline = 1;
        profile.initiative = 1;
        situation.observer = node.id;
        situation.own.living = 100;
        situation.own.initial = 100;
        situation.rally_point = {-50, 0, 0};
        c::KnownContact contact;
        contact.regiment = {2};
        contact.team = 1;
        contact.confidence = 1;
        contact.allegiance = c::Allegiance::Enemy;
        contact.estimated_strength = 30;
        contact.estimated_pose.position = {30, 0, 0};
        situation.contacts.push_back(contact);
        for (unsigned i = 0; i < 3; ++i) {
            c::CommandNode child;
            child.id = {100 + i};
            child.parent = node.id;
            child.level = c::Level(unsigned(level) + 1);
            if (level == c::Level::Group)
                child.regiment = {100 + i};
            if (i == 2)
                child.sector_role = c::SectorRole::Reserve;
            children.push_back(child);
            node.children.push_back(child.id);
            c::ChildStatus status;
            status.node = child.id;
            status.pose.position = {0, float(i) * 6, 0};
            status.living = 10;
            status.initial = 10;
            status.width_m = 4;
            statuses.push_back(status);
        }
        c::CommandTask task;
        task.id = {1};
        task.issuer = node.parent;
        task.recipient = node.id;
        task.action = c::TaskKind::Attack;
        task.status = c::TaskStatus::Active;
        task.intent.target_regiment = {2};
        task.intent.objective_position = {30, 0, 0};
        tasks.push_back(task);
    }
    battle::command::BrainContext Context(battle::Tick tick = 0) {
        situation.tick = tick;
        battle::command::BrainContext context;
        context.tick = tick;
        context.node = &node;
        context.profile = &profile;
        context.situation = &situation;
        context.memory = &memory;
        context.orders = tasks;
        context.subordinates = children;
        context.children_status = statuses;
        return context;
    }
};
} // namespace test
