#pragma once
#include "Brains.h"

namespace battle {
struct World;
}
namespace battle::command {
struct HierarchyValidation {
    std::vector<std::string> issues;
};

// All services are inactive. NotImplemented is not validation success or a no-op order.
StubResult<HierarchyValidation> ValidateHierarchy(const CommandHierarchy &, const World &);
StubResult<LocalSituation> BuildSituation(const World &, const CommandHierarchy &, NodeId, Tick);
StubResult<OfficerId> SelectSuccessor(const World &, const CommandNode &);
StubResult<CommunicationsUpdate> AdvanceCommunications(const CommunicationsState &, Tick);
StubResult<std::vector<NodeId>> ScheduleBrains(std::span<const BrainSchedule>, Tick);
StubResult<battle::Order> TranslateRegimentTask(const CommandTask &, Tick);
} // namespace battle::command
