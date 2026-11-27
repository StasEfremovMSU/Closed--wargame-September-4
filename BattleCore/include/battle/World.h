#pragma once
#include "Orders.h"
#include "battle/command/Runtime.h"
namespace battle {
struct UnitState : UnitSpec {
    bool alive{true};
    ControlMode control{ControlMode::FormationControlled};
    Pose slot{};
    uint32_t slot_index{};
    Tick ready_tick{};
    Vec3 desired_velocity{};
    UnitId attack_target{};
    bool dagger_drawn{}, skirmisher{}, skirmisher_returning{}, bypassing{};
    Tick dagger_ready_tick{};
    Vec3 contact_travel{};
    uint32_t ammunition_remaining{};
    Tick jump_started_tick{};
    float jump_base_z{};
    Tick parry_until{}, parry_ready_tick{};
    float parry_yaw{};
    Tick ranged_ready_tick{};
};
enum class CavalryPhase : uint8_t { None, Approach, Impact, Reform };
struct MarchLane {
    static constexpr float StallSeconds = .6f;
    static constexpr uint8_t MaxAttempts = 2;
    static constexpr size_t MaxDetourPoints = 3;
    Pose waypoint{}, goal{};
    OrderId order{};
    float stalled_seconds{};
    std::vector<Vec3> detour;
    size_t next{};
    uint8_t recovery_attempts{};
};
struct RegimentState : RegimentSpec {
    std::vector<UnitId> units;
    std::vector<Pose> slots;
    std::vector<Pose> route;
    size_t waypoint{};
    std::optional<MarchLane> march_lane;
    std::optional<Vec3> retreat_waypoint;
    std::optional<Order> order;
    uint64_t initial_count{};
    bool panic{};
    Tick calm_counter{}, last_damage_tick{};
    Tick last_hostile_shot_tick{};
    uint64_t panic_loss_count{};
    Tick decision_tick{}, decision_until{};
    OrderType ai_action{OrderType::Hold};
    RegimentId ai_target{};
    bool melee_engaged{}, skirmishers_enabled{};
    float morale{1.f}, cohesion{1.f}, fatigue{}, artillery_pressure{};
    bool disordered{};
    bool manual_frontage{};
    RankPriority rank_priority{RankPriority::Automatic};
    RankPriority rank_layout{RankPriority::Automatic};
    UnitId following_officer{};
    Tick follow_replan_tick{}; // Nonzero while follow owns the regiment's route.
    CavalryPhase cavalry_phase{CavalryPhase::None};
    RegimentId cavalry_target{}, pursuit_target{}, melee_opponent{};
    float charge_runup_m{};
    Tick cavalry_phase_until{}, brace_ready_tick{}, pursuit_check_tick{}, separate_since{};
    Tick contact_end_tick{};
    Tick melee_contact_since{}, melee_recovery_until{};
    bool braced{};
    uint64_t discipline_window_count{};
    Tick discipline_window_until{};
    uint32_t ranged_losses{}, melee_losses{}, melee_kills{};
    bool ranged_checked{}, rear_checked{}, retreat_overlap{}, cannot_rally{};
    uint8_t melee_attack_aspects{};
    Vec3 rout_direction{};
};
struct SmokeCloud {
    Vec3 center{};
    float radius_m{};
    Tick expires_tick{};
};
struct RoundShotTrajectory {
    Vec3 origin{}, aim{};
};
struct OfficerArrowTrajectory {
    Vec3 origin{}, aim{};
};
struct PendingAttack {
    EventId id{};
    UnitId source{}, target{};
    Tick due_tick{};
    float damage{};
    bool hit{};
    float range_m{1.5f};
    bool projectile{};
    bool charge{}, pursuit{};
    std::optional<RoundShotTrajectory> round_shot;
    std::optional<OfficerArrowTrajectory> officer_arrow;
};
struct World {
    Tick tick{};
    uint64_t seed{}, next_event_id{1}, next_order_id{1}, last_launch_id{};
    Scenario scenario{};
    std::vector<UnitState> units;
    std::vector<RegimentState> regiments;
    std::vector<OfficerSpec> officers;
    std::vector<PendingAttack> attacks;
    std::vector<Order> pending_orders;
    std::vector<CommandEnvelope> commands;
    std::vector<uint64_t> accepted_sequences;
    UnitId controlled{};
    std::vector<BattleEvent> events;
    uint64_t neighbor_checks{}, combat_events{};
    BattleOutcome outcome{BattleOutcome::Running};
    int32_t winner_team{};
    std::optional<command::CommandState> command_state;
    std::vector<SmokeCloud> smoke;
};
struct WorldStats {
    uint64_t living_units{}, dead_units{}, regiment_count{}, rank_and_file_count{},
        officer_role_count{};
};
Result<World> CreateWorld(const Scenario &, uint64_t seed);
WorldStats GetStats(const World &);
std::optional<UnitState> GetUnit(const World &, UnitId);
UnitState *FindUnit(World &, UnitId);
const UnitState *FindUnit(const World &, UnitId);
RegimentState *FindRegiment(World &, RegimentId);
const RegimentState *FindRegiment(const World &, RegimentId);
int32_t UnitTeam(const World &, const UnitState &);
} // namespace battle
