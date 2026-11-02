#pragma once
#include "Messages.h"
#include "battle/Orders.h"

namespace battle::command {
enum class BrainStage : uint8_t {
    Perceive,
    ReconcileOrders,
    AssessSituation,
    GenerateOptions,
    ScoreOptions,
    ChoosePlan,
    BuildTasks,
    MonitorExecution,
    ReportUpward
};
struct StageContract {
    BrainStage stage{};
    StubStatus status{StubStatus::NotImplemented};
    std::string_view purpose;
};
struct BrainContract {
    Level level{};
    std::string_view name, responsibility;
    std::span<const StageContract> stages; // Static storage, safe across later calls.
};

struct TacticalOption {
    TaskKind action{TaskKind::HoldArea};
    NodeId recipient{}, target_command{};
    RegimentId target_regiment{};
    Vec3 destination{};
    std::string rationale;
};
struct ActionScore {
    uint32_t option_index{};
    float utility{};
    std::string explanation;
};
struct EvaluationRequest {
    Level level{Level::Regiment};
    NodeId node{};
    Tick tick{};
    uint32_t feature_schema{}; // Zero means no agreed feature encoding.
    std::span<const float> features;
    std::span<const TacticalOption> options;
};
class ITacticalEvaluator {
  public:
    virtual ~ITacticalEvaluator() = default;
    virtual StubResult<std::vector<ActionScore>> Evaluate(const EvaluationRequest &) const = 0;
};
class RulesEvaluatorStub final : public ITacticalEvaluator {
  public:
    StubResult<std::vector<ActionScore>> Evaluate(const EvaluationRequest &) const override;
};
class NeuralEvaluatorStub final : public ITacticalEvaluator {
  public:
    StubResult<std::vector<ActionScore>> Evaluate(const EvaluationRequest &) const override;
};

struct DecisionRecord {
    Tick tick{};
    TaskId cause{};
    TacticalOption selected;
    std::string explanation;
};
struct BrainMemory {
    std::vector<KnownContact> remembered_contacts;
    std::vector<DecisionRecord> recent_decisions;
    std::vector<TaskId> acknowledged_tasks;
    Tick last_think_tick{};
};

// Borrowed, read-only views valid for the duration of Think. No World access here.
struct BrainContext {
    Tick tick{};
    const CommandNode *node{};
    const OfficerProfile *profile{};
    const LocalSituation *situation{};
    const BrainMemory *memory{};
    std::span<const CommandNode> subordinates;
    std::span<const CommandTask> orders;
    std::span<const SituationReport> reports;
    const ITacticalEvaluator *evaluator{};
};

enum class FireDiscipline : uint8_t { HoldFire, Volley, AtWill, Conserve };
enum class TargetPolicy : uint8_t { Assigned, Nearest, GreatestThreat };
struct RegimentDirectives {
    std::optional<FormationSpec> formation;
    std::optional<FireDiscipline> fire;
    std::optional<TargetPolicy> targeting;
};
// Proposed output only. A future commit/authority layer must accept it before use.
struct DecisionProposal {
    NodeId node{};
    Level level{Level::Regiment};
    TaskId source_task{};
    std::vector<CommandTask> subordinate_tasks;
    std::vector<SituationReport> reports;
    std::optional<battle::Order> execution_order;
    std::optional<RegimentDirectives> regiment_directives;
    std::optional<BrainMemory> next_memory;
    std::string explanation;
};

// Each level has its own contract; stubs do not run the stages or the evaluator.
class ArmyBrain {
  public:
    static const BrainContract &Contract();
    StubResult<DecisionProposal> Think(const BrainContext &) const;
};
class PartBrain {
  public:
    static const BrainContract &Contract();
    StubResult<DecisionProposal> Think(const BrainContext &) const;
};
class GroupBrain {
  public:
    static const BrainContract &Contract();
    StubResult<DecisionProposal> Think(const BrainContext &) const;
};
class RegimentBrain {
  public:
    static const BrainContract &Contract();
    StubResult<DecisionProposal> Think(const BrainContext &) const;
};
} // namespace battle::command
