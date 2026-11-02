#include "battle/command/Brains.h"

namespace battle::command {
namespace {
using S = BrainStage;
constexpr auto Pending = StubStatus::NotImplemented;
constexpr StageContract ArmyStages[]{
    {S::Perceive, Pending, "Collect dated reports from parts; distinguish facts and estimates."},
    {S::ReconcileOrders, Pending, "Read the battle mission, constraints and superior intent."},
    {S::AssessSituation, Pending, "Assess force balance, terrain, fronts and reserve needs."},
    {S::GenerateOptions, Pending, "Consider defence, advance, envelopment or withdrawal."},
    {S::ScoreOptions, Pending, "Compare plans with rules or a trained evaluator."},
    {S::ChoosePlan, Pending, "Apply commander judgement, risk tolerance and commitment."},
    {S::BuildTasks, Pending, "Assign sectors, objectives and reserves to parts."},
    {S::MonitorExecution, Pending, "Track progress and decide whether to release reserves."},
    {S::ReportUpward, Pending, "Produce a battle-level progress report for the caller."}};
constexpr StageContract PartStages[]{
    {S::Perceive, Pending, "Combine group reports and information about adjacent parts."},
    {S::ReconcileOrders, Pending, "Resolve army intent and the assigned sector mission."},
    {S::AssessSituation, Pending, "Assess local balance, gaps, frontage and support."},
    {S::GenerateOptions, Pending, "Consider lines, concentration, reinforcement and fallback."},
    {S::ScoreOptions, Pending, "Compare group allocations and local reserve use."},
    {S::ChoosePlan, Pending, "Choose a sector plan within the granted initiative."},
    {S::BuildTasks, Pending, "Assign coordinated objectives and timing to groups."},
    {S::MonitorExecution, Pending, "Track sector progress and request army assistance."},
    {S::ReportUpward, Pending, "Report sector status, losses and unmet needs to the army."}};
constexpr StageContract GroupStages[]{
    {S::Perceive, Pending, "Combine regiment observations and nearby support reports."},
    {S::ReconcileOrders, Pending, "Resolve the group objective and manoeuvre limits."},
    {S::AssessSituation, Pending, "Assess regiment readiness, threats and mutual support."},
    {S::GenerateOptions, Pending, "Consider lead, support, screen and reserve assignments."},
    {S::ScoreOptions, Pending, "Compare joint manoeuvres rather than isolated attacks."},
    {S::ChoosePlan, Pending, "Choose coordinated action using officer traits."},
    {S::BuildTasks, Pending, "Issue complementary tasks to individual regiments."},
    {S::MonitorExecution, Pending, "Track cohesion and synchronise supporting actions."},
    {S::ReportUpward, Pending, "Report group progress or inability to execute to the part."}};
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
StubResult<DecisionProposal> ArmyBrain::Think(const BrainContext &) const {
    return {Pending, std::nullopt, "Army planning is not implemented."};
}
const BrainContract &PartBrain::Contract() {
    static constexpr BrainContract contract{
        Level::Part, "Part", "Sector mission, group allocation and local reserves.", PartStages};
    return contract;
}
StubResult<DecisionProposal> PartBrain::Think(const BrainContext &) const {
    return {Pending, std::nullopt, "Part planning is not implemented."};
}
const BrainContract &GroupBrain::Contract() {
    static constexpr BrainContract contract{
        Level::Group, "Group", "Joint manoeuvres and mutual support of regiments.", GroupStages};
    return contract;
}
StubResult<DecisionProposal> GroupBrain::Think(const BrainContext &) const {
    return {Pending, std::nullopt, "Group planning is not implemented."};
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
