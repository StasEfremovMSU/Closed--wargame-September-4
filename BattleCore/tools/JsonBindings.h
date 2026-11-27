#pragma once
#include "battle/Snapshot.h"
#include "json.hpp"
#include <limits>
namespace battle {
inline void to_json(nlohmann::json &j, CommandKind value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, CommandKind &value) {
    uint64_t raw{};
    if (j.is_number_unsigned())
        raw = j.get<uint64_t>();
    else if (j.is_number_integer() && j.get<int64_t>() >= 0)
        raw = uint64_t(j.get<int64_t>());
    else
        throw std::invalid_argument("CommandKind must be a nonnegative integer");
    if (raw > uint8_t(CommandKind::SetRankPriority))
        throw std::invalid_argument("invalid CommandKind value");
    value = CommandKind(raw);
}
inline uint64_t ReadJsonUnsigned(const nlohmann::json &j, uint64_t maximum, const char *error) {
    if (!j.is_number_integer() ||
        (!j.is_number_unsigned() && j.get<int64_t>() < 0) ||
        j.get<uint64_t>() > maximum)
        throw std::invalid_argument(error);
    return j.get<uint64_t>();
}
inline void to_json(nlohmann::json &j, RankPriority value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, RankPriority &value) {
    value = RankPriority(ReadJsonUnsigned(j, uint8_t(RankPriority::MeleeFront), "invalid RankPriority value"));
}
inline void to_json(nlohmann::json &j, UnitTroop value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, UnitTroop &value) {
    value = UnitTroop(ReadJsonUnsigned(j, uint8_t(UnitTroop::Mounted), "invalid UnitTroop value"));
}
inline void to_json(nlohmann::json &j, CavalryClass value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, CavalryClass &value) {
    value = CavalryClass(ReadJsonUnsigned(j, uint8_t(CavalryClass::Heavy), "invalid CavalryClass value"));
}
inline void to_json(nlohmann::json &j, FormationShape value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, FormationShape &value) {
    value = FormationShape(ReadJsonUnsigned(j, uint8_t(FormationShape::Circle), "invalid FormationShape value"));
}
inline void to_json(nlohmann::json &j, RangedWeapon value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, RangedWeapon &value) {
    value = RangedWeapon(ReadJsonUnsigned(j, uint8_t(RangedWeapon::Arcane), "invalid RangedWeapon value"));
}
inline void to_json(nlohmann::json &j, BattlePlatform value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, BattlePlatform &value) {
    value = BattlePlatform(ReadJsonUnsigned(j, uint8_t(BattlePlatform::FieldGun), "invalid BattlePlatform value"));
}
inline void to_json(nlohmann::json &j, TerrainFeature value) { j = uint8_t(value); }
inline void from_json(const nlohmann::json &j, TerrainFeature &value) {
    value = TerrainFeature(ReadJsonUnsigned(j, uint8_t(TerrainFeature::RedoubtInterior), "invalid TerrainFeature value"));
}
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
} // namespace battle
namespace battle {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Vec3, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Pose, position, yaw)
inline void to_json(nlohmann::json &j, const FormationSpec &formation) {
    j = {{"columns", formation.columns}, {"spacing_m", formation.spacing_m},
         {"body_radius_m", formation.body_radius_m}};
    if (formation.shape != FormationShape::Rectangle)
        j["shape"] = formation.shape;
    if (formation.slot_stagger != 0.f)
        j["slot_stagger"] = formation.slot_stagger;
}
inline void from_json(const nlohmann::json &j, FormationSpec &formation) {
    const FormationSpec defaults;
    formation = {j.value("columns", defaults.columns), j.value("spacing_m", defaults.spacing_m),
                 j.value("body_radius_m", defaults.body_radius_m), j.value("shape", defaults.shape),
                 j.value("slot_stagger", defaults.slot_stagger)};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WeaponSpec, damage, range, cooldown_ticks,
                                                windup_ticks, projectile_speed_mps, base_accuracy)
inline void to_json(nlohmann::json &j, const UnitSpec &u) {
    j = {{"id", u.id}, {"regiment", u.regiment}, {"name", u.name}, {"pose", u.pose},
         {"health", u.health}, {"speed", u.speed}, {"armour", u.armour},
         {"archer_skill", u.archer_skill}, {"archer", u.archer}, {"equipment", u.equipment},
         {"weapon", u.weapon}, {"legacy_id", u.legacy_id}, {"independent_team", u.independent_team},
         {"troop", u.troop}};
    if (u.cavalry != CavalryClass::None)
        j["cavalry"] = u.cavalry;
    if (u.ranged_weapon != RangedWeapon::None)
        j["ranged_weapon"] = u.ranged_weapon;
    if (u.ammunition)
        j["ammunition"] = u.ammunition;
    if (u.platform != BattlePlatform::None)
        j["platform"] = u.platform;
    if (u.ranged_attack)
        j["ranged_attack"] = *u.ranged_attack;
}
inline void from_json(const nlohmann::json &j, UnitSpec &u) {
    const UnitSpec defaults;
    u.id = j.value("id", defaults.id);
    u.regiment = j.value("regiment", defaults.regiment);
    u.name = j.value("name", defaults.name);
    u.pose = j.value("pose", defaults.pose);
    u.health = j.value("health", defaults.health);
    u.speed = j.value("speed", defaults.speed);
    u.armour = j.value("armour", defaults.armour);
    u.archer_skill = j.value("archer_skill", defaults.archer_skill);
    u.archer = j.value("archer", defaults.archer);
    u.equipment = j.value("equipment", defaults.equipment);
    u.weapon = j.value("weapon", defaults.weapon);
    u.legacy_id = j.value("legacy_id", defaults.legacy_id);
    u.independent_team = j.value("independent_team", defaults.independent_team);
    u.troop = j.value("troop", defaults.troop);
    u.cavalry = j.value("cavalry", defaults.cavalry);
    u.ranged_weapon = j.value("ranged_weapon", defaults.ranged_weapon);
    u.platform = j.value("platform", defaults.platform);
    u.ranged_attack.reset();
    if (const auto attack = j.find("ranged_attack"); attack != j.end())
        u.ranged_attack = attack->get<WeaponSpec>();
    u.ammunition = defaults.ammunition;
    if (auto it = j.find("ammunition"); it != j.end())
        u.ammunition = uint32_t(ReadJsonUnsigned(*it, std::numeric_limits<uint32_t>::max(),
                                                "ammunition must be a nonnegative integer"));
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RegimentSpec, id, team, pose, formation, speed,
                                                turn_rate, rally_point, name, skirmisher_count,
                                                deployment_ticks, initial_fatigue, loose_skirmish)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OfficerSpec, id, unit, role, charisma, tactic_mind,
                                                lead_skill, lead_dist, legacy_id)
inline void to_json(nlohmann::json &j, const Obstacle &o) {
    j = {{"min_x", o.min_x}, {"min_y", o.min_y}, {"max_x", o.max_x}, {"max_y", o.max_y},
         {"blocks_projectiles", o.blocks_projectiles}};
    if (o.feature != TerrainFeature::Barrier)
        j["feature"] = o.feature;
}
inline void from_json(const nlohmann::json &j, Obstacle &o) {
    const Obstacle defaults;
    o.min_x = j.value("min_x", defaults.min_x);
    o.min_y = j.value("min_y", defaults.min_y);
    o.max_x = j.value("max_x", defaults.max_x);
    o.max_y = j.value("max_y", defaults.max_y);
    o.blocks_projectiles = j.value("blocks_projectiles", defaults.blocks_projectiles);
    o.feature = j.value("feature", defaults.feature);
}
inline void to_json(nlohmann::json &j, const Rules &r) {
    j = {{"panic_fraction", r.panic_fraction},
         {"calm_ticks", r.calm_ticks},
         {"rally_tolerance_m", r.rally_tolerance_m},
         {"rally_safe_radius_m", r.rally_safe_radius_m},
         {"ranged_cohorts", r.ranged_cohorts}, {"local_melee", r.local_melee}};
    if (r.regiment_tactics)
        j["regiment_tactics"] = true;
    if (r.risky_volley)
        j["risky_volley"] = true;
    if (r.adaptive_battle)
        j["adaptive_battle"] = true;
    if (r.linear_infantry)
        j["linear_infantry"] = true;
    if (r.mixed_rank_tactics)
        j["mixed_rank_tactics"] = true;
    if (r.sustained_melee)
        j["sustained_melee"] = true;
    if (r.controlled_arrow_aim)
        j["controlled_arrow_aim"] = true;
}
inline void from_json(const nlohmann::json &j, Rules &r) {
    const Rules defaults;
    r.panic_fraction = j.value("panic_fraction", defaults.panic_fraction);
    r.calm_ticks = j.value("calm_ticks", defaults.calm_ticks);
    r.rally_tolerance_m = j.value("rally_tolerance_m", defaults.rally_tolerance_m);
    r.rally_safe_radius_m = j.value("rally_safe_radius_m", defaults.rally_safe_radius_m);
    r.local_melee = j.value("local_melee", defaults.local_melee);
    r.regiment_tactics = j.value("regiment_tactics", defaults.regiment_tactics);
    r.risky_volley = j.value("risky_volley", defaults.risky_volley);
    r.adaptive_battle = j.value("adaptive_battle", defaults.adaptive_battle);
    r.linear_infantry = j.value("linear_infantry", defaults.linear_infantry);
    r.mixed_rank_tactics = j.value("mixed_rank_tactics", defaults.mixed_rank_tactics);
    r.sustained_melee = j.value("sustained_melee", defaults.sustained_melee);
    r.controlled_arrow_aim = j.value("controlled_arrow_aim", defaults.controlled_arrow_aim);
    r.ranged_cohorts = defaults.ranged_cohorts;
    if (!j.contains("ranged_cohorts"))
        return;
    const auto &count = j.at("ranged_cohorts");
    // Check the JSON type and wide value before narrowing to avoid wrapping into1..64.
    if (count.is_number_unsigned()) {
        auto n = count.get<uint64_t>();
        if (n < 1 || n > 64)
            throw std::invalid_argument("ranged_cohorts must be an integer1..64");
        r.ranged_cohorts = uint32_t(n);
    } else if (count.is_number_integer()) {
        auto n = count.get<int64_t>();
        if (n < 1 || n > 64)
            throw std::invalid_argument("ranged_cohorts must be an integer1..64");
        r.ranged_cohorts = uint32_t(n);
    } else
        throw std::invalid_argument("ranged_cohorts must be an integer1..64");
}
} // namespace battle
#include "CommandJson.h"
namespace battle {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TerrainHill, radius_x_m, radius_y_m,
                                                height_m, crest_fraction, relief)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TerrainFieldBounds, half_width_m, half_height_m)
inline void to_json(nlohmann::json &j, const ScenarioObjective &objective) {
    j = {{"center", objective.center}, {"radius_m", objective.radius_m},
         {"defending_team", objective.defending_team}};
}
inline void from_json(const nlohmann::json &j, ScenarioObjective &objective) {
    const ScenarioObjective defaults;
    objective.center = j.value("center", defaults.center);
    objective.radius_m = j.value("radius_m", defaults.radius_m);
    objective.defending_team = defaults.defending_team;
    if (const auto team = j.find("defending_team"); team != j.end()) {
        if (team->is_number_integer() && !team->is_number_unsigned() && team->get<int64_t>() == -1)
            return;
        objective.defending_team = int32_t(ReadJsonUnsigned(*team, std::numeric_limits<int32_t>::max(),
                                                            "invalid objective defending_team"));
    }
}
inline void to_json(nlohmann::json &j, const Scenario &s) {
    j = {{"schema_version", s.schema_version}, {"units", s.units}, {"regiments", s.regiments},
         {"officers", s.officers}, {"obstacles", s.obstacles}, {"rules", s.rules},
         {"command_setup", s.command_setup}};
    if (s.hill)
        j["hill"] = *s.hill;
    if (s.objective)
        j["objective"] = *s.objective;
    if (s.field_bounds)
        j["field_bounds"] = *s.field_bounds;
}
inline void from_json(const nlohmann::json &j, Scenario &s) {
    const Scenario defaults;
    s.schema_version = j.value("schema_version", defaults.schema_version);
    s.units = j.value("units", defaults.units);
    s.regiments = j.value("regiments", defaults.regiments);
    s.officers = j.value("officers", defaults.officers);
    s.obstacles = j.value("obstacles", defaults.obstacles);
    s.rules = j.value("rules", defaults.rules);
    s.command_setup = j.value("command_setup", defaults.command_setup);
    s.hill = j.value("hill", defaults.hill);
    s.objective = j.value("objective", defaults.objective);
    s.field_bounds = j.value("field_bounds", defaults.field_bounds);
}
inline void to_json(nlohmann::json &j, const Order &o) {
    j = {{"id", o.id}, {"issuer", o.issuer}, {"recipient", o.recipient}, {"type", o.type},
         {"priority", o.priority}, {"point", o.point}, {"target_unit", o.target_unit},
         {"target_regiment", o.target_regiment}, {"created_tick", o.created_tick},
         {"delivery_tick", o.delivery_tick}, {"expiry_tick", o.expiry_tick}, {"status", o.status}};
    if (o.player_direct)
        j["player_direct"] = true;
    if (o.pace != MovementPace::Walk)
        j["pace"] = uint8_t(o.pace);
    if (o.facing)
        j["facing"] = *o.facing;
    if (o.move_columns)
        j["move_columns"] = *o.move_columns;
    if (o.maneuver_group)
        j["maneuver_group"] = o.maneuver_group;
}
inline void from_json(const nlohmann::json &j, Order &o) {
    const Order defaults;
    o.id = j.value("id", defaults.id);
    o.issuer = j.value("issuer", defaults.issuer);
    o.recipient = j.value("recipient", defaults.recipient);
    o.type = j.value("type", defaults.type);
    o.priority = j.value("priority", defaults.priority);
    o.point = j.value("point", defaults.point);
    o.target_unit = j.value("target_unit", defaults.target_unit);
    o.target_regiment = j.value("target_regiment", defaults.target_regiment);
    o.created_tick = j.value("created_tick", defaults.created_tick);
    o.delivery_tick = j.value("delivery_tick", defaults.delivery_tick);
    o.expiry_tick = j.value("expiry_tick", defaults.expiry_tick);
    o.status = j.value("status", defaults.status);
    o.player_direct = j.value("player_direct", defaults.player_direct);
    o.pace = defaults.pace;
    o.facing.reset();
    o.move_columns.reset();
    o.maneuver_group = 0;
    if (const auto pace = j.find("pace"); pace != j.end())
        o.pace = MovementPace(ReadJsonUnsigned(*pace, uint8_t(MovementPace::Run), "invalid order pace"));
    if (const auto facing = j.find("facing"); facing != j.end()) {
        if (!facing->is_number() || !std::isfinite(facing->get<float>()))
            throw std::invalid_argument("order facing must be a finite number");
        o.facing = facing->get<float>();
    }
    if (const auto columns = j.find("move_columns"); columns != j.end()) {
        o.move_columns = uint32_t(ReadJsonUnsigned(*columns, std::numeric_limits<uint32_t>::max(),
                                                   "invalid order move_columns"));
        if (!*o.move_columns || o.type != OrderType::MoveTo)
            throw std::invalid_argument("order move_columns requires a move order and positive width");
    }
    if (const auto group = j.find("maneuver_group"); group != j.end()) {
        o.maneuver_group = ReadJsonUnsigned(*group, std::numeric_limits<uint64_t>::max(),
                                            "invalid order maneuver_group");
        if (!o.maneuver_group || o.type != OrderType::MoveTo)
            throw std::invalid_argument("order maneuver_group requires a move order and a positive ID");
    }
}
inline void to_json(nlohmann::json &j, const Command &c) {
    j = {{"kind", c.kind}, {"order", c.order}, {"regiment", c.regiment},
         {"formation", c.formation}, {"unit", c.unit}, {"target", c.target},
         {"velocity", c.velocity}, {"pose", c.pose}, {"sample_tick", c.sample_tick},
         {"command_task", c.command_task}, {"skirmishers_enabled", c.skirmishers_enabled}};
    if (c.kind == CommandKind::SetRegimentFollowing)
        j["follow_enabled"] = c.follow_enabled;
    if (c.kind == CommandKind::SetRankPriority)
        j["rank_priority"] = c.rank_priority;
}
inline void from_json(const nlohmann::json &j, Command &c) {
    const Command defaults;
    c.kind = j.value("kind", defaults.kind);
    c.order = j.value("order", defaults.order);
    c.regiment = j.value("regiment", defaults.regiment);
    c.formation = j.value("formation", defaults.formation);
    c.unit = j.value("unit", defaults.unit);
    c.target = j.value("target", defaults.target);
    c.velocity = j.value("velocity", defaults.velocity);
    c.pose = j.value("pose", defaults.pose);
    c.sample_tick = j.value("sample_tick", defaults.sample_tick);
    c.command_task = j.value("command_task", defaults.command_task);
    c.skirmishers_enabled = j.value("skirmishers_enabled", defaults.skirmishers_enabled);
    c.follow_enabled = c.kind == CommandKind::SetRegimentFollowing &&
                       j.at("follow_enabled").get<bool>();
    c.rank_priority = c.kind == CommandKind::SetRankPriority
        ? j.at("rank_priority").get<RankPriority>() : defaults.rank_priority;
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandEnvelope, tick, sequence, command)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BattleEvent, id, tick, kind, source, target,
                                                regiment, amount, reason, due_tick, hit, projectile,
                                                cause, aspect, high_arc)
inline void to_json(nlohmann::json &j, const UnitState &u) {
    j = static_cast<const UnitSpec &>(u);
    j["alive"] = u.alive;
    j["control"] = u.control;
    j["slot"] = u.slot;
    j["slot_index"] = u.slot_index;
    j["ready_tick"] = u.ready_tick;
    j["desired_velocity"] = u.desired_velocity;
    j["attack_target"] = u.attack_target;
    j["dagger_drawn"] = u.dagger_drawn;
    j["skirmisher"] = u.skirmisher;
    j["skirmisher_returning"] = u.skirmisher_returning;
    j["bypassing"] = u.bypassing;
    j["dagger_ready_tick"] = u.dagger_ready_tick;
    j["contact_travel"] = u.contact_travel;
    if (u.jump_started_tick) {
        j["jump_started_tick"] = u.jump_started_tick;
        j["jump_base_z"] = u.jump_base_z;
    }
    if (u.parry_ready_tick) {
        j["parry_until"] = u.parry_until;
        j["parry_ready_tick"] = u.parry_ready_tick;
        j["parry_yaw"] = u.parry_yaw;
    }
    if (u.ammunition)
        j["ammunition_remaining"] = u.ammunition_remaining;
    if (u.ranged_ready_tick)
        j["ranged_ready_tick"] = u.ranged_ready_tick;
}
inline void from_json(const nlohmann::json &j, UnitState &u) {
    const UnitState defaults;
    static_cast<UnitSpec &>(u) = j.get<UnitSpec>();
    u.alive = j.value("alive", defaults.alive);
    u.control = j.value("control", defaults.control);
    u.slot = j.value("slot", defaults.slot);
    u.slot_index = j.value("slot_index", defaults.slot_index);
    u.ready_tick = j.value("ready_tick", defaults.ready_tick);
    u.desired_velocity = j.value("desired_velocity", defaults.desired_velocity);
    u.attack_target = j.value("attack_target", defaults.attack_target);
    u.dagger_drawn = j.value("dagger_drawn", defaults.dagger_drawn);
    u.skirmisher = j.value("skirmisher", defaults.skirmisher);
    u.skirmisher_returning = j.value("skirmisher_returning", defaults.skirmisher_returning);
    u.bypassing = j.value("bypassing", defaults.bypassing);
    u.dagger_ready_tick = j.value("dagger_ready_tick", defaults.dagger_ready_tick);
    u.contact_travel = j.value("contact_travel", defaults.contact_travel);
    u.jump_started_tick = j.value("jump_started_tick", defaults.jump_started_tick);
    u.jump_base_z = j.value("jump_base_z", defaults.jump_base_z);
    u.parry_until = j.value("parry_until", defaults.parry_until);
    u.parry_ready_tick = j.value("parry_ready_tick", defaults.parry_ready_tick);
    u.parry_yaw = j.value("parry_yaw", defaults.parry_yaw);
    u.ammunition_remaining = defaults.ammunition_remaining;
    u.ranged_ready_tick = j.value("ranged_ready_tick", defaults.ranged_ready_tick);
    if (const auto count = j.find("ammunition_remaining"); count != j.end())
        u.ammunition_remaining = uint32_t(ReadJsonUnsigned(*count, std::numeric_limits<uint32_t>::max(),
                                                          "invalid ammunition_remaining"));
}
inline void to_json(nlohmann::json &j, const MarchLane &lane) {
    j = {{"waypoint", lane.waypoint}, {"goal", lane.goal}, {"order", lane.order},
         {"stalled_seconds", lane.stalled_seconds}, {"detour", lane.detour}, {"next", lane.next},
         {"recovery_attempts", lane.recovery_attempts}};
}
inline void from_json(const nlohmann::json &j, MarchLane &lane) {
    j.at("waypoint").get_to(lane.waypoint);
    j.at("goal").get_to(lane.goal);
    j.at("order").get_to(lane.order);
    j.at("stalled_seconds").get_to(lane.stalled_seconds);
    const auto &detour = j.at("detour");
    if (!detour.is_array() || detour.size() > MarchLane::MaxDetourPoints)
        throw std::invalid_argument("invalid march detour");
    detour.get_to(lane.detour);
    lane.next = size_t(ReadJsonUnsigned(j.at("next"), MarchLane::MaxDetourPoints,
                                        "invalid march detour point index"));
    lane.recovery_attempts = uint8_t(ReadJsonUnsigned(j.at("recovery_attempts"), MarchLane::MaxAttempts,
                                                       "invalid march recovery attempt count"));
}
inline void to_json(nlohmann::json &j, const RegimentState &r) {
    j = static_cast<const RegimentSpec &>(r);
    j["units"] = r.units;
    j["slots"] = r.slots;
    j["route"] = r.route;
    j["waypoint"] = r.waypoint;
    if (r.march_lane)
        j["march_lane"] = *r.march_lane;
    if (r.retreat_waypoint)
        j["retreat_waypoint"] = *r.retreat_waypoint;
    j["order"] = r.order ? nlohmann::json(*r.order) : nlohmann::json(nullptr);
    j["initial_count"] = r.initial_count;
    j["panic"] = r.panic;
    j["calm_counter"] = r.calm_counter;
    j["last_damage_tick"] = r.last_damage_tick;
    if (r.last_hostile_shot_tick)
        j["last_hostile_shot_tick"] = r.last_hostile_shot_tick;
    if (r.artillery_pressure > 0.f)
        j["artillery_pressure"] = r.artillery_pressure;
    j["decision_tick"] = r.decision_tick;
    j["decision_until"] = r.decision_until;
    j["ai_action"] = r.ai_action;
    j["ai_target"] = r.ai_target;
    if (r.rank_priority != RankPriority::Automatic)
        j["rank_priority"] = r.rank_priority;
    if (r.rank_layout != RankPriority::Automatic)
        j["rank_layout"] = r.rank_layout;
    j["panic_loss_count"] = r.panic_loss_count;
    j["melee_engaged"] = r.melee_engaged;
    j["skirmishers_enabled"] = r.skirmishers_enabled;
    j["morale"] = r.morale;
    j["cohesion"] = r.cohesion;
    j["fatigue"] = r.fatigue;
    j["disordered"] = r.disordered;
    if (r.following_officer) {
        j["following_officer"] = r.following_officer;
        j["follow_replan_tick"] = r.follow_replan_tick;
    }
    if (r.cavalry_phase != CavalryPhase::None)
        j["cavalry_phase"] = uint8_t(r.cavalry_phase);
    if (r.cavalry_target)
        j["cavalry_target"] = r.cavalry_target;
    if (r.pursuit_target)
        j["pursuit_target"] = r.pursuit_target;
    if (r.melee_opponent)
        j["melee_opponent"] = r.melee_opponent;
    if (r.charge_runup_m)
        j["charge_runup_m"] = r.charge_runup_m;
    if (r.cavalry_phase_until)
        j["cavalry_phase_until"] = r.cavalry_phase_until;
    if (r.brace_ready_tick)
        j["brace_ready_tick"] = r.brace_ready_tick;
    if (r.pursuit_check_tick)
        j["pursuit_check_tick"] = r.pursuit_check_tick;
    if (r.separate_since)
        j["separate_since"] = r.separate_since;
    if (r.contact_end_tick)
        j["contact_end_tick"] = r.contact_end_tick;
    if (r.melee_contact_since)
        j["melee_contact_since"] = r.melee_contact_since;
    if (r.melee_recovery_until)
        j["melee_recovery_until"] = r.melee_recovery_until;
    if (r.braced)
        j["braced"] = r.braced;
    if (r.discipline_window_count)
        j["discipline_window_count"] = r.discipline_window_count;
    if (r.discipline_window_until)
        j["discipline_window_until"] = r.discipline_window_until;
    if (r.ranged_losses)
        j["ranged_losses"] = r.ranged_losses;
    if (r.melee_losses)
        j["melee_losses"] = r.melee_losses;
    if (r.melee_kills)
        j["melee_kills"] = r.melee_kills;
    if (r.ranged_checked)
        j["ranged_checked"] = r.ranged_checked;
    if (r.rear_checked)
        j["rear_checked"] = r.rear_checked;
    if (r.retreat_overlap)
        j["retreat_overlap"] = r.retreat_overlap;
    if (r.cannot_rally)
        j["cannot_rally"] = r.cannot_rally;
    if (r.melee_attack_aspects)
        j["melee_attack_aspects"] = r.melee_attack_aspects;
    if (r.rout_direction != Vec3{})
        j["rout_direction"] = r.rout_direction;
}
inline void from_json(const nlohmann::json &j, RegimentState &r) {
    static_cast<RegimentSpec &>(r) = j.get<RegimentSpec>();
    r.units = j.at("units").get<std::vector<UnitId>>();
    r.slots = j.at("slots").get<std::vector<Pose>>();
    r.route = j.at("route").get<std::vector<Pose>>();
    r.waypoint = j.at("waypoint").get<size_t>();
    r.march_lane.reset();
    if (const auto lane = j.find("march_lane"); lane != j.end())
        r.march_lane = lane->get<MarchLane>();
    if (j.contains("retreat_waypoint"))
        r.retreat_waypoint = j.at("retreat_waypoint").get<Vec3>();
    if (!j.at("order").is_null())
        r.order = j.at("order").get<Order>();
    r.initial_count = j.at("initial_count");
    r.panic = j.at("panic");
    r.calm_counter = j.at("calm_counter");
    r.last_damage_tick = j.at("last_damage_tick");
    r.artillery_pressure = j.value("artillery_pressure", 0.f);
    r.last_hostile_shot_tick = 0;
    if (const auto shot = j.find("last_hostile_shot_tick"); shot != j.end())
        r.last_hostile_shot_tick = ReadJsonUnsigned(*shot, std::numeric_limits<Tick>::max(),
                                                    "invalid last_hostile_shot_tick");
    r.decision_tick = j.at("decision_tick");
    r.decision_until = j.at("decision_until");
    r.ai_action = j.at("ai_action");
    r.ai_target = j.at("ai_target");
    r.rank_priority = j.value("rank_priority", RankPriority::Automatic);
    r.rank_layout = j.value("rank_layout", RankPriority::Automatic);
    r.panic_loss_count = j.at("panic_loss_count");
    r.melee_engaged = j.value("melee_engaged", false);
    r.skirmishers_enabled = j.value("skirmishers_enabled", false);
    r.morale = j.value("morale", 1.f);
    r.cohesion = j.value("cohesion", 1.f);
    r.fatigue = j.value("fatigue", 0.f);
    r.disordered = j.value("disordered", false);
    r.following_officer = j.value("following_officer", UnitId{});
    r.follow_replan_tick = j.value("follow_replan_tick", Tick{});
    r.cavalry_phase = CavalryPhase::None;
    if (const auto phase = j.find("cavalry_phase"); phase != j.end())
        r.cavalry_phase = CavalryPhase(ReadJsonUnsigned(*phase, uint8_t(CavalryPhase::Reform),
                                                        "invalid cavalry_phase"));
    r.cavalry_target = j.value("cavalry_target", RegimentId{});
    r.pursuit_target = j.value("pursuit_target", RegimentId{});
    r.melee_opponent = j.value("melee_opponent", RegimentId{});
    r.charge_runup_m = j.value("charge_runup_m", 0.f);
    r.cavalry_phase_until = j.value("cavalry_phase_until", Tick{});
    r.brace_ready_tick = j.value("brace_ready_tick", Tick{});
    r.pursuit_check_tick = j.value("pursuit_check_tick", Tick{});
    r.separate_since = j.value("separate_since", Tick{});
    r.contact_end_tick = j.value("contact_end_tick", Tick{});
    r.melee_contact_since = j.value("melee_contact_since", Tick{});
    r.melee_recovery_until = j.value("melee_recovery_until", Tick{});
    r.braced = j.value("braced", false);
    r.discipline_window_count = j.value("discipline_window_count", uint64_t{});
    r.discipline_window_until = j.value("discipline_window_until", Tick{});
    r.ranged_losses = j.value("ranged_losses", uint32_t{});
    r.melee_losses = j.value("melee_losses", uint32_t{});
    r.melee_kills = j.value("melee_kills", uint32_t{});
    r.ranged_checked = j.value("ranged_checked", false);
    r.rear_checked = j.value("rear_checked", false);
    r.retreat_overlap = j.value("retreat_overlap", false);
    r.cannot_rally = j.value("cannot_rally", false);
    r.melee_attack_aspects = 0;
    if (const auto aspects = j.find("melee_attack_aspects"); aspects != j.end())
        r.melee_attack_aspects = uint8_t(ReadJsonUnsigned(*aspects, 3, "invalid melee_attack_aspects"));
    r.rout_direction = j.value("rout_direction", Vec3{});
}
inline void to_json(nlohmann::json &j, const PendingAttack &a) {
    j = {{"id", a.id}, {"source", a.source}, {"target", a.target}, {"due_tick", a.due_tick}, {"damage", a.damage},
         {"hit", a.hit}, {"range_m", a.range_m}, {"projectile", a.projectile}};
    if (a.charge)
        j["charge"] = true;
    if (a.pursuit)
        j["pursuit"] = true;
    if (a.round_shot)
        j["round_shot"] = {{"origin", a.round_shot->origin}, {"aim", a.round_shot->aim}};
    if (a.officer_arrow)
        j["officer_arrow"] = {{"origin", a.officer_arrow->origin}, {"aim", a.officer_arrow->aim}};
}
inline void from_json(const nlohmann::json &j, PendingAttack &a) {
    const PendingAttack defaults;
    a.id = j.value("id", defaults.id);
    a.source = j.value("source", defaults.source);
    a.target = j.value("target", defaults.target);
    a.due_tick = j.value("due_tick", defaults.due_tick);
    a.damage = j.value("damage", defaults.damage);
    a.hit = j.value("hit", defaults.hit);
    a.range_m = j.value("range_m", defaults.range_m);
    a.projectile = j.value("projectile", defaults.projectile);
    a.charge = j.value("charge", defaults.charge);
    a.pursuit = j.value("pursuit", defaults.pursuit);
    a.round_shot.reset();
    if (const auto shot = j.find("round_shot"); shot != j.end())
        a.round_shot = RoundShotTrajectory{shot->at("origin").get<Vec3>(), shot->at("aim").get<Vec3>()};
    a.officer_arrow.reset();
    if (const auto arrow = j.find("officer_arrow"); arrow != j.end())
        a.officer_arrow = OfficerArrowTrajectory{arrow->at("origin").get<Vec3>(), arrow->at("aim").get<Vec3>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SmokeCloud, center, radius_m, expires_tick)
inline void to_json(nlohmann::json &j, const World &w) {
    j = {{"tick", w.tick}, {"seed", w.seed}, {"next_event_id", w.next_event_id},
         {"next_order_id", w.next_order_id}, {"last_launch_id", w.last_launch_id}, {"scenario", w.scenario},
         {"units", w.units}, {"regiments", w.regiments}, {"officers", w.officers},
         {"attacks", w.attacks}, {"pending_orders", w.pending_orders}, {"commands", w.commands},
         {"accepted_sequences", w.accepted_sequences}, {"controlled", w.controlled}, {"events", w.events},
         {"neighbor_checks", w.neighbor_checks}, {"combat_events", w.combat_events}, {"outcome", w.outcome},
         {"winner_team", w.winner_team}, {"command_state", w.command_state}};
    if (!w.smoke.empty())
        j["smoke"] = w.smoke;
}
inline void from_json(const nlohmann::json &j, World &w) {
    const World defaults;
    w.tick = j.value("tick", defaults.tick);
    w.seed = j.value("seed", defaults.seed);
    w.next_event_id = j.value("next_event_id", defaults.next_event_id);
    w.next_order_id = j.value("next_order_id", defaults.next_order_id);
    w.last_launch_id = j.value("last_launch_id", defaults.last_launch_id);
    w.scenario = j.value("scenario", defaults.scenario);
    w.units = j.value("units", defaults.units);
    w.regiments = j.value("regiments", defaults.regiments);
    w.officers = j.value("officers", defaults.officers);
    w.attacks = j.value("attacks", defaults.attacks);
    w.pending_orders = j.value("pending_orders", defaults.pending_orders);
    w.commands = j.value("commands", defaults.commands);
    w.accepted_sequences = j.value("accepted_sequences", defaults.accepted_sequences);
    w.controlled = j.value("controlled", defaults.controlled);
    w.events = j.value("events", defaults.events);
    w.neighbor_checks = j.value("neighbor_checks", defaults.neighbor_checks);
    w.combat_events = j.value("combat_events", defaults.combat_events);
    w.outcome = j.value("outcome", defaults.outcome);
    w.winner_team = j.value("winner_team", defaults.winner_team);
    w.command_state = j.value("command_state", defaults.command_state);
    w.smoke = j.value("smoke", defaults.smoke);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Snapshot, schema_version, state)
} // namespace battle
