#pragma once
#include "Scenario.h"
namespace battle {
enum class OrderType : uint8_t { Hold, MoveTo, Attack, Withdraw, Rally, HoldLine, FallBack, Brace, Pursue };
enum class OrderStatus : uint8_t { Pending, Active, Completed, Failed, Cancelled };
enum class MovementPace : uint8_t { Walk, March, Run };

inline bool IsHolding(OrderType type) {
    return type == OrderType::Hold || type == OrderType::HoldLine || type == OrderType::Brace;
}
inline bool IsAttacking(OrderType type) {
    return type == OrderType::Attack || type == OrderType::Pursue;
}
inline bool IsWithdrawing(OrderType type) {
    return type == OrderType::Withdraw || type == OrderType::Rally || type == OrderType::FallBack;
}
inline constexpr int PlayerOrderPriority = 120;
struct Order {
    OrderId id{};
    OfficerId issuer{};
    RegimentId recipient{};
    OrderType type{OrderType::Hold};
    int priority{};
    Vec3 point{};
    UnitId target_unit{};
    RegimentId target_regiment{};
    Tick created_tick{}, delivery_tick{}, expiry_tick{};
    OrderStatus status{OrderStatus::Pending};
    bool player_direct{};
    MovementPace pace{MovementPace::Walk};
    std::optional<float> facing;
    std::optional<uint32_t> move_columns;
    uint64_t maneuver_group{};
};
enum class CommandKind : uint8_t {
    IssueOrder,
    ReformRegiment,
    AcquireControl,
    ReleaseControl,
    MoveControlledUnit,
    AttackControlledUnit,
    ExternalPoseSample,
    IssueCommandTask,
    SetSkirmishers,
    FaceControlledUnit,
    SetRegimentFollowing,
    JumpControlledUnit,
    ParryControlledUnit,
    FireFieldGun
};
// A tagged wire record; only fields belonging to kind are consumed.
struct Command {
    CommandKind kind{CommandKind::IssueOrder};
    Order order{};
    RegimentId regiment{};
    FormationSpec formation{};
    UnitId unit{}, target{};
    Vec3 velocity{};
    Pose pose{};
    Tick sample_tick{};
    command::CommandTask command_task{};
    bool skirmishers_enabled{}, follow_enabled{};
};
struct CommandEnvelope {
    Tick tick{};
    uint64_t sequence{};
    Command command{};
};
struct World;
Status SubmitOrder(World &, const Order &);
bool ValidOrderShape(const Order &);
} // namespace battle
