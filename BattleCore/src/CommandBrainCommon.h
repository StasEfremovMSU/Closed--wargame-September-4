#pragma once
#include "battle/command/Brains.h"
namespace battle::command::detail {
Tick Later(Tick, Tick);
std::optional<Tick> TaskExpiry(const CommandTask &, Tick received);
bool ValidTask(const CommandTask &);
struct Prepared {
    BrainMemory memory;
    const CommandTask *task{};
    bool new_task{}, expired{};
};
StubResult<Prepared> Prepare(const BrainContext &, Level);
const KnownContact *Contact(const BrainContext &, RegimentId = {}, bool enemy = true);
bool AttackWorthwhile(const BrainContext &, const KnownContact &);
DecisionProposal Finish(const BrainContext &, Prepared, FsmState, TaskStatus, TacticalOption,
                        std::string);
CommandTask ChildTask(const BrainContext &, const Prepared &, const CommandNode &, TaskKind, Vec3,
                      RegimentId = {});
} // namespace battle::command::detail
