#pragma once
#include "Perception.h"
#include <variant>

namespace battle::command {
enum class TaskKind : uint8_t {
    HoldArea,
    MoveToArea,
    Attack,
    Defend,
    Screen,
    Support,
    Flank,
    Exploit,
    Reserve,
    Withdraw,
    Rally,
    Reform
};
enum class InitiativePolicy : uint8_t { AwaitOrders, WithinIntent, EmergencyOnly, Free };
enum class TaskStatus : uint8_t { Draft, Accepted, Active, Completed, Failed, Cancelled };
enum class DurationOrigin : uint8_t { Created, Delivered };

struct CommandIntent {
    bool operator==(const CommandIntent &) const = default;
    std::string purpose; // Explanation; no language model or script executes this text.
    Vec3 objective_position{};
    float objective_radius_m{};
    NodeId target_command{};
    RegimentId target_regiment{};
};

struct AuthorityLimits {
    bool operator==(const AuthorityLimits &) const = default;
    InitiativePolicy initiative{InitiativePolicy::AwaitOrders};
    bool may_pursue{}, may_break_formation{}, may_change_target{};
    std::optional<float> pursuit_limit_m, acceptable_loss_fraction;
};

struct CommandTask {
    bool operator==(const CommandTask &) const = default;
    TaskId id{}, parent_task{};
    NodeId issuer{}, recipient{};
    OfficerId author{};
    TaskKind action{TaskKind::HoldArea};
    CommandIntent intent;
    AuthorityLimits authority;
    int priority{};
    Tick created_tick{};
    std::optional<Tick> not_before_tick, expiry_tick, duration_ticks;
    DurationOrigin duration_origin{DurationOrigin::Delivered};
    TaskStatus status{TaskStatus::Draft};
    std::optional<uint32_t> formation_columns;
    std::optional<float> formation_spacing_m;
    std::optional<Tick> received_tick;
};

enum class ReportKind : uint8_t {
    Situation,
    Acknowledged,
    CannotExecute,
    NeedSupport,
    TaskCompleted,
    CommanderLost
};
struct SituationReport {
    bool operator==(const SituationReport &) const = default;
    ReportKind kind{ReportKind::Situation};
    NodeId sender{}, recipient{};
    TaskId about_task{};
    Tick observed_tick{}, created_tick{};
    LocalSituation situation;
    std::string explanation;
};

enum class DeliveryChannel : uint8_t { Voice, Signal, Courier };
enum class DeliveryStatus : uint8_t {
    Queued,
    InTransit,
    Delivered,
    Expired,
    Lost,
    RecipientUnavailable
};
struct MessageEnvelope {
    bool operator==(const MessageEnvelope &) const = default;
    MessageId id{};
    NodeId sender{}, recipient{};
    DeliveryChannel channel{DeliveryChannel::Voice};
    UnitId courier{}; // Existing body; not a second unit spawned by a message.
    Tick created_tick{}, delivery_tick{};
    std::optional<Tick> expiry_tick;
    DeliveryStatus status{DeliveryStatus::Queued};
    std::variant<CommandTask, SituationReport> payload;
};
struct CommunicationsState {
    bool operator==(const CommunicationsState &) const = default;
    std::vector<MessageEnvelope> messages;
};
struct CommunicationsUpdate {
    bool operator==(const CommunicationsUpdate &) const = default;
    CommunicationsState next;
    std::vector<MessageEnvelope> delivered;
};
} // namespace battle::command
