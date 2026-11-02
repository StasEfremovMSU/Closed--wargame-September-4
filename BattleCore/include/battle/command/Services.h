#pragma once
#include "Brains.h"

namespace battle {
struct World;
}
namespace battle::command {
struct HierarchyValidation {
    std::vector<std::string> issues;
};
const CommandNode *FindNode(const CommandHierarchy &, NodeId);
CommandNode *FindNode(CommandHierarchy &, NodeId);
const OfficerProfile *FindProfile(const CommandHierarchy &, OfficerId);
std::vector<RegimentId> OwnedRegiments(const CommandHierarchy &, NodeId);
std::optional<Pose> NodePose(const World &, const CommandHierarchy &, NodeId);
bool HasLineOfSight(Vec3, Vec3, std::span<const Obstacle>);

// All services are inactive. NotImplemented is not validation success or a no-op order.
StubResult<HierarchyValidation> ValidateHierarchy(const CommandHierarchy &, const World &);
StubResult<LocalSituation> BuildSituation(const World &, const CommandHierarchy &, NodeId, Tick);
StubResult<OfficerId> SelectSuccessor(const World &, const CommandNode &);
StubResult<CommunicationsUpdate> AdvanceCommunications(const CommunicationsState &, Tick);
StubResult<std::vector<NodeId>> ScheduleBrains(std::span<const BrainSchedule>, Tick);
StubResult<battle::Order> TranslateRegimentTask(const CommandTask &, Tick);
} // namespace battle::command
