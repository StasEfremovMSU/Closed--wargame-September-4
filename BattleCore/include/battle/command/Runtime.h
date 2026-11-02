#pragma once
#include "Brains.h"
#include "Setup.h"

namespace battle {
class NavigationService;
struct World;
} // namespace battle
namespace battle::command {
struct NodeRuntime {
    bool operator==(const NodeRuntime &) const = default;
    NodeId node{};
    BrainMemory memory;
    std::vector<CommandTask> orders;
    std::vector<SituationReport> reports;
    BrainSchedule schedule;
    OrderId last_order_id{};
    std::vector<CommandTask> sent_tasks;
};

struct CommandState {
    bool operator==(const CommandState &) const = default;
    CommandHierarchy hierarchy;
    std::vector<NodeRuntime> nodes;
    CommunicationsState communications;
    TaskId next_task_id{1};
    MessageId next_message_id{1};
};

bool ValidTaskRequest(const CommandTask &);
StubResult<bool> InitializeCommand(World &);
StubResult<TaskId> SubmitTask(World &, CommandTask);
StubResult<bool> UpdateCommand(World &, const NavigationService &);
} // namespace battle::command
