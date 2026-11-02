#include "battle/command/Services.h"

namespace battle::command {
StubResult<HierarchyValidation> ValidateHierarchy(const CommandHierarchy &, const World &) {
    return {StubStatus::NotImplemented, std::nullopt,
            "Hierarchy references, authority, teams and cycles are not validated yet."};
}
StubResult<LocalSituation> BuildSituation(const World &, const CommandHierarchy &, NodeId, Tick) {
    return {StubStatus::NotImplemented, std::nullopt,
            "Command observations, report merging and visibility are not implemented."};
}
StubResult<OfficerId> SelectSuccessor(const World &, const CommandNode &) {
    return {StubStatus::NotImplemented, std::nullopt,
            "Hierarchy succession is not implemented; no replacement is selected."};
}
StubResult<CommunicationsUpdate> AdvanceCommunications(const CommunicationsState &, Tick) {
    return {StubStatus::NotImplemented, std::nullopt,
            "Message transport, delivery and expiry are not implemented; queues are unchanged."};
}
StubResult<std::vector<NodeId>> ScheduleBrains(std::span<const BrainSchedule>, Tick) {
    return {StubStatus::NotImplemented, std::nullopt,
            "Multi-level scheduling and emergency interrupts are not implemented."};
}
StubResult<battle::Order> TranslateRegimentTask(const CommandTask &, Tick) {
    return {
        StubStatus::NotImplemented, std::nullopt,
        "Task translation and authority checks are not implemented; no battle order is emitted."};
}
} // namespace battle::command
