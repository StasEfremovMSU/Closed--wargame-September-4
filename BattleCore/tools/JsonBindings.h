#pragma once
#include "battle/Snapshot.h"
#include "json.hpp"
namespace battle {
template <class T> void to_json(nlohmann::json &j, const Id<T> &v) {
    j = v.value;
}
template <class T> void from_json(const nlohmann::json &j, Id<T> &v) {
    if (!j.is_number_unsigned() && !j.is_number_integer())
        throw std::invalid_argument("ID must be an integer");
    if (j.type() == nlohmann::json::value_t::number_integer && j.get<int64_t>() < 0)
        throw std::invalid_argument("negative ID");
    v.value = j.get<uint64_t>();
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Vec3, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Pose, position, yaw)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FormationSpec, columns, spacing_m, body_radius_m)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponSpec, damage, range, cooldown_ticks,
                                                windup_ticks, projectile_speed_mps, base_accuracy)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UnitSpec, id, regiment, name, pose, health, speed,
                                                armour, archer_skill, archer, equipment, weapon,
                                                legacy_id)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RegimentSpec, id, team, pose, formation, speed,
                                                turn_rate, rally_point, name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OfficerSpec, id, unit, role, charisma, tactic_mind,
                                                lead_skill, lead_dist, legacy_id)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Obstacle, min_x, min_y, max_x, max_y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Rules, panic_fraction, calm_ticks,
                                                rally_tolerance_m, rally_safe_radius_m)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scenario, schema_version, units, regiments,
                                                officers, obstacles, rules)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Order, id, issuer, recipient, type, priority, point,
                                                target_unit, target_regiment, created_tick,
                                                delivery_tick, expiry_tick, status)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Command, kind, order, regiment, formation, unit,
                                                target, velocity, pose, sample_tick)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandEnvelope, tick, sequence, command)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BattleEvent, id, tick, kind, source, target,
                                                regiment, amount, reason, due_tick, hit, projectile,
                                                cause)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UnitState, id, regiment, name, pose, health, speed,
                                                armour, archer_skill, archer, equipment, weapon,
                                                legacy_id, alive, control, slot, slot_index,
                                                ready_tick, desired_velocity, attack_target)
inline void to_json(nlohmann::json &j, const RegimentState &r) {
    j = static_cast<const RegimentSpec &>(r);
    j["units"] = r.units;
    j["slots"] = r.slots;
    j["route"] = r.route;
    j["waypoint"] = r.waypoint;
    j["order"] = r.order ? nlohmann::json(*r.order) : nlohmann::json(nullptr);
    j["initial_count"] = r.initial_count;
    j["panic"] = r.panic;
    j["calm_counter"] = r.calm_counter;
    j["last_damage_tick"] = r.last_damage_tick;
    j["decision_tick"] = r.decision_tick;
    j["decision_until"] = r.decision_until;
    j["ai_action"] = r.ai_action;
    j["ai_target"] = r.ai_target;
    j["panic_loss_count"] = r.panic_loss_count;
}
inline void from_json(const nlohmann::json &j, RegimentState &r) {
    static_cast<RegimentSpec &>(r) = j.get<RegimentSpec>();
    r.units = j.at("units").get<std::vector<UnitId>>();
    r.slots = j.at("slots").get<std::vector<Pose>>();
    r.route = j.at("route").get<std::vector<Pose>>();
    r.waypoint = j.at("waypoint").get<size_t>();
    if (!j.at("order").is_null())
        r.order = j.at("order").get<Order>();
    r.initial_count = j.at("initial_count");
    r.panic = j.at("panic");
    r.calm_counter = j.at("calm_counter");
    r.last_damage_tick = j.at("last_damage_tick");
    r.decision_tick = j.at("decision_tick");
    r.decision_until = j.at("decision_until");
    r.ai_action = j.at("ai_action");
    r.ai_target = j.at("ai_target");
    r.panic_loss_count = j.at("panic_loss_count");
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PendingAttack, id, source, target, due_tick, damage,
                                                hit, range_m, projectile)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(World, tick, seed, next_event_id, next_order_id,
                                                last_launch_id, scenario, units, regiments,
                                                officers, attacks, pending_orders, commands,
                                                accepted_sequences, controlled, events,
                                                neighbor_checks, combat_events, outcome,
                                                winner_team)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Snapshot, schema_version, state)
} // namespace battle
