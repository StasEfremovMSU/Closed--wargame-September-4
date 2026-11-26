#pragma once
#include "Messages.h"
#include "battle/Orders.h"
#include <functional>

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
    bool operator==(const TacticalOption &) const = default;
    TaskKind action{TaskKind::HoldArea};
    NodeId recipient{}, target_command{};
    RegimentId target_regiment{};
    Vec3 destination{};
    std::string rationale;
    bool keep_current_order{}; // Evaluation option only. Never sent as a new task.
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
    uint32_t baseline_option{}; // Option the built-in FSM would select.
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
    bool operator==(const DecisionRecord &) const = default;
    Tick tick{};
    TaskId cause{};
    TacticalOption selected;
    std::string explanation;
};
enum class FsmState : uint8_t {
    Idle,
    Deploying,
    Holding,
    Advancing,
    Attacking,
    Supporting,
    Regrouping,
    Withdrawing,
    Rallying,
    Reserve,
    Failed,
    Complete
};
struct BrainMemory {
    bool operator==(const BrainMemory &) const = default;
    FsmState state{FsmState::Idle};
    Tick state_entered_tick{}, task_started_tick{};
    TaskId active_task{};
    TaskStatus task_status{TaskStatus::Draft};
    RegimentId target{};
    Vec3 goal{};
    std::vector<KnownContact> remembered_contacts;
    std::vector<DecisionRecord> recent_decisions;
    std::vector<TaskId> acknowledged_tasks;
    Tick last_think_tick{};
};

struct ChildStatus {
    NodeId node{};
    Pose pose{};
    uint32_t living{}, initial{};
    float width_m{2};
    bool panic{};
    FsmState state{FsmState::Idle};
    TaskStatus task_status{TaskStatus::Draft};
    bool melee_engaged{};
    RegimentId attack_target{};
    std::optional<Vec3> formation_center;
};
struct ChildOrderObservation {
    bool pending{}, executing{};
    std::optional<Tick> delivered_tick;
    float progress{}; // Friendly execution cue in [0, 1].
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
    std::span<const ChildStatus> children_status;
    std::optional<FormationSpec> current_formation;
    bool formation_ready{true};
    std::function<std::optional<Pose>(NodeId, RegimentId, const KnownContact &, const CommandTask &)> choose_flank;
    std::function<bool(RegimentId)> in_melee_with;
    std::span<const CommandTask> previous_subordinate_tasks;
    std::function<bool(NodeId, const KnownContact &)> can_approach;
    std::function<bool(NodeId, Vec3)> can_stage;
    std::function<std::optional<Pose>(NodeId, const KnownContact &, const CommandTask &)> choose_group_flank;
    std::function<std::optional<Vec3>(NodeId, NodeId, const KnownContact &)> choose_group_support;
    std::optional<Level> parent_level;
    const ITacticalEvaluator *group_policy{};
    bool group_order_commitment{};
    std::function<std::optional<ChildOrderObservation>(const CommandTask &)> observe_order;
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
