#pragma once
#include "Messages.h"
namespace battle::command {
struct CommandSetup {
    bool operator==(const CommandSetup &) const = default;
    CommandHierarchy hierarchy;
    std::vector<CommandTask> missions;
};
} // namespace battle::command
