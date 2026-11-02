#include "battle/command/Brains.h"

namespace battle::command {
namespace {
using S = BrainStage;
constexpr auto Pending = StubStatus::NotImplemented;
constexpr StageContract ArmyStages[]{
    {S::Perceive, StubStatus::Ready, "Read observed battle strength, enemy contacts and reports."},
    {S::ReconcileOrders, StubStatus::Ready, "Resolve the army mission, intent and task expiry."},
    {S::AssessSituation, StubStatus::Ready,
     "Assess visible force balance, losses and threatened sectors."},
    {S::GenerateOptions, StubStatus::Ready,
     "Choose hold, advance, support or organized withdrawal."},
    {S::ScoreOptions, StubStatus::Ready,
     "Compare autonomous attack with holding using deterministic rules."},
    {S::ChoosePlan, StubStatus::Ready,
     "Apply panic, mission intent, reserve trigger and commitment."},
    {S::BuildTasks, StubStatus::Ready,
     "Assign objectives and reserve duties to subordinate parts."},
    {S::MonitorExecution, StubStatus::Ready,
     "Track visible losses, local panic and support requests."},
    {S::ReportUpward, StubStatus::Ready,
     "Report a changed army plan to its superior, if present."}};
constexpr StageContract PartStages[]{
    {S::Perceive, StubStatus::Ready, "Combine group reports and information about adjacent parts."},
    {S::ReconcileOrders, StubStatus::Ready, "Resolve army intent and the assigned sector mission."},
    {S::AssessSituation, StubStatus::Ready, "Assess local balance, gaps, frontage and support."},
    {S::GenerateOptions, StubStatus::Ready,
     "Consider lines, concentration, reinforcement and fallback."},
    {S::ScoreOptions, StubStatus::Ready, "Compare group allocations and local reserve use."},
    {S::ChoosePlan, StubStatus::Ready, "Choose a sector plan within the granted initiative."},
    {S::BuildTasks, StubStatus::Ready, "Assign coordinated objectives and timing to groups."},
    {S::MonitorExecution, StubStatus::Ready, "Track sector progress and request army assistance."},
    {S::ReportUpward, StubStatus::Ready,
     "Report sector status, losses and unmet needs to the army."}};
constexpr StageContract GroupStages[]{
    {S::Perceive, StubStatus::Ready, "Combine regiment observations and nearby support reports."},
    {S::ReconcileOrders, StubStatus::Ready, "Resolve the group objective and manoeuvre limits."},
    {S::AssessSituation, StubStatus::Ready,
     "Assess regiment readiness, threats and mutual support."},
    {S::GenerateOptions, StubStatus::Ready,
     "Consider lead, support, screen and reserve assignments."},
    {S::ScoreOptions, StubStatus::Ready, "Compare joint manoeuvres rather than isolated attacks."},
    {S::ChoosePlan, StubStatus::Ready, "Choose coordinated action using officer traits."},
    {S::BuildTasks, StubStatus::Ready, "Issue complementary tasks to individual regiments."},
    {S::MonitorExecution, StubStatus::Ready, "Track cohesion and synchronise supporting actions."},
    {S::ReportUpward, StubStatus::Ready,
     "Report group progress or inability to execute to the part."}};
constexpr StageContract RegimentStages[]{
    {S::Perceive, StubStatus::Ready, "Read local contacts, incoming fire, formation and losses."},
    {S::ReconcileOrders, StubStatus::Ready, "Resolve delivered tasks, priority and expiry."},
    {S::AssessSituation, StubStatus::Ready, "Assess targets, flank threats and ability to obey."},
    {S::GenerateOptions, StubStatus::Ready,
     "Consider hold, move, attack, reform, fire and withdrawal."},
    {S::ScoreOptions, StubStatus::Ready, "Score legal options from officer-visible information."},
    {S::ChoosePlan, StubStatus::Ready,
     "Apply judgement, discipline, reaction delay and persistence."},
    {S::BuildTasks, StubStatus::Ready,
     "Propose one regiment order and separate formation/fire directives."},
    {S::MonitorExecution, StubStatus::Ready,
     "Track completion, unreachable targets and emergencies."},
    {S::ReportUpward, StubStatus::Ready,
     "Acknowledge orders and report threats or support needs."}};
} // namespace

const BrainContract &ArmyBrain::Contract() {
    static constexpr BrainContract contract{
        Level::Army, "Army", "Battle intent, part coordination and army reserves.", ArmyStages};
    return contract;
}
const BrainContract &PartBrain::Contract() {
    static constexpr BrainContract contract{
        Level::Part, "Part", "Sector mission, group allocation and local reserves.", PartStages};
    return contract;
}

const BrainContract &GroupBrain::Contract() {
    static constexpr BrainContract contract{
        Level::Group, "Group", "Joint manoeuvres and mutual support of regiments.", GroupStages};
    return contract;
}

const BrainContract &RegimentBrain::Contract() {
    static constexpr BrainContract contract{Level::Regiment, "Regiment",
                                            "Local tactics and regiment execution proposals.",
                                            RegimentStages};
    return contract;
}

StubResult<std::vector<ActionScore>>
RulesEvaluatorStub::Evaluate(const EvaluationRequest &r) const {
    if (r.feature_schema > 1)
        return {StubStatus::InvalidInput, std::nullopt, "Unknown feature schema."};
    for (float x : r.features)
        if (!std::isfinite(x) || x < 0)
            return {StubStatus::InvalidInput, std::nullopt, "Invalid rule features."};
    float own = r.features.size() > 0 ? r.features[0] : 1;
    float enemy = r.features.size() > 1 ? r.features[1] : 1;
    float distance = r.features.size() > 2 ? r.features[2] : 0;
    std::vector<ActionScore> scores;
    for (size_t i = 0; i < r.options.size(); ++i) {
        auto action = r.options[i].action;
        if (unsigned(action) > unsigned(TaskKind::Reform))
            return {StubStatus::InvalidInput, std::nullopt, "Invalid action."};
        float score = 5;
        if (action == TaskKind::Attack || action == TaskKind::Exploit)
            score = 10 * std::min(100.f, own / std::max(1.f, enemy)) / (1 + distance / 100);
        scores.push_back({uint32_t(i), score, "Deterministic rule utility."});
    }
    return {StubStatus::Ready, std::move(scores), "Rule options scored."};
}
StubResult<std::vector<ActionScore>>
NeuralEvaluatorStub::Evaluate(const EvaluationRequest &) const {
    return {Pending, std::nullopt, "Neural inference and feature encoding are not implemented."};
}
} // namespace battle::command
