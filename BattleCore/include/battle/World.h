#pragma once
#include "Orders.h"
namespace battle {
struct UnitState:UnitSpec{bool alive{true};ControlMode control{ControlMode::FormationControlled};Pose slot{};uint32_t slot_index{};Tick ready_tick{};Vec3 desired_velocity{};UnitId attack_target{};};
struct RegimentState:RegimentSpec{std::vector<UnitId> units;std::vector<Pose> slots;std::vector<Pose> route;size_t waypoint{};std::optional<Order> order;uint64_t initial_count{};bool panic{};Tick calm_counter{},last_damage_tick{};Tick decision_tick{},decision_until{};};
struct PendingAttack{EventId id{};UnitId source{},target{};Tick due_tick{};float damage{};bool hit{};};
struct World{Tick tick{};uint64_t seed{},next_event_id{1},next_order_id{1};Scenario scenario{};std::vector<UnitState> units;std::vector<RegimentState> regiments;std::vector<OfficerSpec> officers;std::vector<PendingAttack> attacks;std::vector<Order> pending_orders;std::vector<CommandEnvelope> commands;std::vector<uint64_t> accepted_sequences;UnitId controlled{};std::vector<BattleEvent> events;uint64_t neighbor_checks{},combat_events{};};
struct WorldStats{uint64_t living_units{},dead_units{},regiment_count{},rank_and_file_count{},officer_role_count{};};
Result<World> CreateWorld(const Scenario&,uint64_t seed);
WorldStats GetStats(const World&);
std::optional<UnitState> GetUnit(const World&,UnitId);
UnitState* FindUnit(World&,UnitId);
const UnitState* FindUnit(const World&,UnitId);
RegimentState* FindRegiment(World&,RegimentId);
const RegimentState* FindRegiment(const World&,RegimentId);
}
