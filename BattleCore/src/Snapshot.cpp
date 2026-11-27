#include "battle/Snapshot.h"
#include "battle/Control.h"
#include <bit>
#include <map>
#include <set>
namespace battle {
Snapshot Capture(const World &w) {
    return {3, w};
}
Result<World> Restore(const Snapshot &s) {
    auto bad = [](std::string message) {
        return Result<World>{Status::InvalidInput, {}, {{"snapshot", 0, std::move(message)}}};
    };
    if (s.schema_version != 2 && s.schema_version != 3)
        return bad("unsupported snapshot version");
    if (s.schema_version == 2 &&
        (s.state.command_state || s.state.scenario.command_setup ||
         std::any_of(s.state.commands.begin(), s.state.commands.end(), [](const auto &e) {
             return e.command.kind == CommandKind::IssueCommandTask;
         })))
        return bad("version 2 snapshots cannot contain command hierarchy state");
    auto valid = ValidateScenario(s.state.scenario);
    if (!valid)
        return {valid.status, {}, valid.diagnostics};
    World w = s.state;
    w.scenario = std::move(valid.value);
    std::sort(w.units.begin(), w.units.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(w.regiments.begin(), w.regiments.end(), [](auto &a, auto &b) { return a.id < b.id; });
    if (w.units.size() != w.scenario.units.size() ||
        w.regiments.size() != w.scenario.regiments.size() ||
        w.officers.size() != w.scenario.officers.size())
        return bad("snapshot roster differs from scenario");
    auto canonical = [](auto &a, auto &b) { return a.id < b.id; };
    std::sort(w.scenario.regiments.begin(), w.scenario.regiments.end(), canonical);
    std::sort(w.scenario.officers.begin(), w.scenario.officers.end(), canonical);
    std::sort(w.officers.begin(), w.officers.end(), canonical);
    if (w.officers != w.scenario.officers)
        return bad("officer definitions differ from scenario");
    std::map<uint64_t, uint64_t> original_counts;
    for (const auto &unit : w.scenario.units)
        ++original_counts[unit.regiment.value];
    for (size_t i = 0; i < w.regiments.size(); ++i) {
        const auto &initial = w.scenario.regiments[i];
        RegimentSpec definition = w.regiments[i];
        definition.pose = initial.pose;
        definition.formation = initial.formation;
        if (definition != initial ||
            w.regiments[i].initial_count != original_counts[initial.id.value])
            return bad("regiment definitions differ from scenario");
    }
    Scenario runtime = w.scenario;
    runtime.units.clear();
    runtime.regiments.clear();
    runtime.officers = w.officers;
    std::sort(w.scenario.units.begin(), w.scenario.units.end(),
              [](auto &a, auto &b) { return a.id < b.id; });
    for (const auto &u : w.units) {
        auto initial = std::lower_bound(w.scenario.units.begin(), w.scenario.units.end(), u.id,
                                        [](auto &v, UnitId id) { return v.id < id; });
        if (initial == w.scenario.units.end() || initial->id != u.id || !std::isfinite(u.health) ||
            u.health < 0 || u.health > initial->health || u.alive != (u.health > 0) ||
            uint8_t(u.control) > uint8_t(ControlMode::Dead) ||
            ((u.control == ControlMode::Dead) == u.alive) || !Finite(u.slot) ||
            !Finite(u.desired_velocity) || !Finite(u.contact_travel) ||
            !std::isfinite(u.jump_base_z) || !std::isfinite(u.parry_yaw) ||
            u.jump_started_tick > w.tick || (!u.jump_started_tick && u.jump_base_z != 0.f) ||
            (u.parry_until && (u.parry_until <= w.tick || u.parry_until > u.parry_ready_tick)) ||
            (!u.parry_until && u.parry_yaw != 0.f) || (u.parry_until && u.jump_started_tick) ||
            ((u.jump_started_tick || u.parry_until) && u.control != ControlMode::PlayerControlled) ||
            ((u.jump_started_tick || u.parry_ready_tick) && u.platform != BattlePlatform::None) ||
            (u.parry_ready_tick && !u.alive) ||
            u.ammunition_remaining > initial->ammunition ||
            (u.ranged_ready_tick && !u.ranged_attack) ||
            ((u.dagger_drawn || u.skirmisher || u.skirmisher_returning) && (!u.archer || !u.alive)) ||
            (u.skirmisher && u.skirmisher_returning))
            return bad("invalid runtime unit");
        UnitSpec definition = u;
        definition.pose = initial->pose;
        definition.health = initial->health;
        if (definition != *initial)
            return bad("unit definitions differ from scenario");
        UnitSpec spec = u;
        if (!u.alive)
            spec.health = 1;
        runtime.units.push_back(spec);
    }
    for (const auto &r : w.regiments)
        runtime.regiments.push_back(r);
    valid = ValidateScenario(std::move(runtime));
    if (!valid)
        return {valid.status, {}, valid.diagnostics};
    std::set<uint64_t> members;
    const auto normalized = [](float value) { return value >= 0.f && value <= 1.f; };
    for (const auto &r : w.regiments) {
        if (r.initial_count < r.units.size() || r.waypoint > r.route.size() ||
            r.panic_loss_count > r.initial_count || r.last_damage_tick > w.tick ||
            r.last_hostile_shot_tick > w.tick || !std::isfinite(r.artillery_pressure) ||
            r.artillery_pressure < 0.f || r.artillery_pressure > 1.f ||
            !normalized(r.morale) || !normalized(r.cohesion) || !normalized(r.fatigue) ||
            (r.retreat_waypoint && !Finite(*r.retreat_waypoint)) ||
            uint8_t(r.ai_action) > uint8_t(OrderType::Pursue) ||
            uint8_t(r.cavalry_phase) > uint8_t(CavalryPhase::Reform) ||
            !(r.charge_runup_m >= 0.f && r.charge_runup_m <= 12.f) ||
            r.separate_since > w.tick || r.contact_end_tick > w.tick ||
            r.discipline_window_count > r.initial_count ||
            r.melee_attack_aspects > 3 ||
            !Finite(r.rout_direction) || Length(r.rout_direction) > 1.001f ||
            (r.ai_target && !FindRegiment(w, r.ai_target)) ||
            (r.cavalry_target && !FindRegiment(w, r.cavalry_target)) ||
            (r.pursuit_target && !FindRegiment(w, r.pursuit_target)) ||
            (r.melee_opponent && !FindRegiment(w, r.melee_opponent)))
            return bad("invalid runtime regiment");
        for (auto p : r.slots)
            if (!Finite(p))
                return bad("invalid formation slot");
        for (auto p : r.route)
            if (!Finite(p))
                return bad("invalid route pose");
        if (r.march_lane) {
            const auto &lane = *r.march_lane;
            if (!Finite(lane.waypoint) || !Finite(lane.goal) ||
                !(lane.stalled_seconds >= 0.f && lane.stalled_seconds <= MarchLane::StallSeconds) ||
                (!lane.detour.empty() && lane.detour.size() != MarchLane::MaxDetourPoints) ||
                (lane.detour.empty() ? lane.next != 0 : lane.next >= lane.detour.size()) ||
                lane.recovery_attempts > MarchLane::MaxAttempts)
                return bad("invalid march lane");
            for (const Vec3 point : lane.detour)
                if (!Finite(point))
                    return bad("invalid march detour point");
        }
        for (auto id : r.units) {
            auto *u = FindUnit(w, id);
            if (!u || !u->alive || u->regiment != r.id || u->slot_index >= r.slots.size() ||
                !members.insert(id.value).second)
                return bad("duplicate or missing live membership");
        }
        if (r.order &&
            (r.order->recipient != r.id || !ValidOrderShape(*r.order) ||
             uint8_t(r.order->status) > uint8_t(OrderStatus::Cancelled)))
            return bad("invalid current order");
    }
    size_t controlled = 0;
    for (const auto &u : w.units) {
        if (u.alive && u.regiment && !members.contains(u.id.value))
            return bad("living unit missing from regiment");
        if (u.control == ControlMode::PlayerControlled) {
            ++controlled;
            if (w.controlled != u.id)
                return bad("control owner mismatch");
        }
    }
    if (controlled != (w.controlled ? 1u : 0u) || controlled > 1)
        return bad("invalid control ownership");
    if (!w.next_event_id || !w.next_order_id || w.last_launch_id >= w.next_event_id ||
        uint8_t(w.outcome) > uint8_t(BattleOutcome::Draw))
        return bad("invalid counters or outcome");
    std::set<uint64_t> attacks;
    for (auto &a : w.attacks)
        if (!a.id || !attacks.insert(a.id.value).second || a.id.value > w.last_launch_id ||
            !FindUnit(w, a.source) || !FindUnit(w, a.target) || a.due_tick <= w.tick ||
            !std::isfinite(a.damage) || a.damage < 0 || !std::isfinite(a.range_m) || a.range_m <= 0 ||
            ((a.charge || a.pursuit) && a.projectile) || (a.charge && a.pursuit) ||
            (a.round_shot && (!a.projectile || FindUnit(w, a.source)->platform != BattlePlatform::FieldGun ||
                              !Finite(a.round_shot->origin) || !Finite(a.round_shot->aim))))
            return bad("invalid pending attack");
    for (const auto &cloud : w.smoke)
        if (!Finite(cloud.center) || !std::isfinite(cloud.radius_m) || cloud.radius_m <= 0.f ||
            cloud.expires_tick < w.tick)
            return bad("invalid smoke cloud");
    if (!std::is_sorted(w.accepted_sequences.begin(), w.accepted_sequences.end()) ||
        std::adjacent_find(w.accepted_sequences.begin(), w.accepted_sequences.end()) !=
            w.accepted_sequences.end() ||
        (!w.accepted_sequences.empty() && !w.accepted_sequences[0]))
        return bad("invalid accepted sequences");
    std::set<uint64_t> queued;
    std::map<std::pair<Tick, uint64_t>, unsigned> movement;
    for (auto &e : w.commands) {
        if (!e.sequence || !queued.insert(e.sequence).second ||
            !std::binary_search(w.accepted_sequences.begin(), w.accepted_sequences.end(),
                                e.sequence) ||
            e.tick < w.tick || uint8_t(e.command.kind) > uint8_t(CommandKind::FireFieldGun) ||
            e.command.formation.shape > FormationShape::Circle ||
            !Finite(e.command.pose) || !Finite(e.command.velocity) ||
            !ValidOrderShape(e.command.order))
            return bad("invalid queued command");
        if (e.command.kind == CommandKind::IssueCommandTask &&
            (!w.command_state || !command::ValidTaskRequest(e.command.command_task)))
            return bad("invalid queued command task");
        if (e.command.kind == CommandKind::MoveControlledUnit ||
            e.command.kind == CommandKind::FaceControlledUnit ||
            e.command.kind == CommandKind::JumpControlledUnit ||
            e.command.kind == CommandKind::ParryControlledUnit)
            movement[{e.tick, e.command.unit.value}] |= 1;
        if (e.command.kind == CommandKind::ExternalPoseSample) {
            if (e.command.sample_tick != e.tick)
                return bad("external sample tick mismatch");
            movement[{e.tick, e.command.unit.value}] |= 2;
        }
    }
    for (auto &entry : movement)
        if (entry.second == 3)
            return bad("conflicting movement queue");
    for (auto &o : w.pending_orders)
        if (!o.id || !FindRegiment(w, o.recipient) || o.id.value >= w.next_order_id ||
            !ValidOrderShape(o) || uint8_t(o.status) > uint8_t(OrderStatus::Cancelled))
            return bad("invalid pending order");
    if (w.scenario.command_setup.has_value() != w.command_state.has_value())
        return bad("command runtime is missing or has no scenario setup");
    if (w.command_state) {
        auto command_valid = command::ValidateCommandState(w, *w.command_state);
        if (!command_valid)
            return {command_valid.status, {}, command_valid.diagnostics};
    }
    bool following = false;
    for (const auto &r : w.regiments) {
        if (!r.following_officer && !r.follow_replan_tick)
            continue;
        if (following || r.units.empty() || !r.following_officer ||
            r.following_officer != w.controlled ||
            !CanCommandRegiment(w, r.following_officer, r.id))
            return bad("invalid regiment follow ownership");
        following = true;
    }
    return {Status::Ok, std::move(w), {}};
}
namespace {
struct Hash {
    uint64_t h{14695981039346656037ull};
    void n(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= v & 255;
            h *= 1099511628211ull;
            v >>= 8;
        }
    };
    void f(float v) {
        n(std::bit_cast<uint32_t>(v));
    }
    void text(const std::string &s) {
        n(s.size());
        for (unsigned char c : s)
            n(c);
    }
    void vec(Vec3 p) {
        f(p.x);
        f(p.y);
        f(p.z);
    }
    void pose(Pose p) {
        vec(p.position);
        f(p.yaw);
    }
    void formation(FormationSpec s) {
        n(s.columns);
        f(s.spacing_m);
        f(s.body_radius_m);
        if (s.shape != FormationShape::Rectangle) {
            n(0x5348415045ull);
            n(uint8_t(s.shape));
        }
    }
    void weapon(const WeaponSpec &s) {
        f(s.damage);
        f(s.range);
        n(s.cooldown_ticks);
        n(s.windup_ticks);
        f(s.projectile_speed_mps);
        f(s.base_accuracy);
    }
    void order(const Order &o) {
        n(o.id.value);
        n(o.issuer.value);
        n(o.recipient.value);
        n(uint8_t(o.type));
        n(uint64_t(o.priority));
        vec(o.point);
        n(o.target_unit.value);
        n(o.target_regiment.value);
        n(o.created_tick);
        n(o.delivery_tick);
        n(o.expiry_tick);
        n(uint8_t(o.status));
        if (o.player_direct)
            text("player direct order");
        if (o.pace != MovementPace::Walk || o.facing || o.move_columns) {
            text("field maneuver");
            n(uint8_t(o.pace));
            n(o.facing.has_value());
            if (o.facing)
                f(*o.facing);
            n(o.move_columns.value_or(0));
        }
        if (o.maneuver_group) {
            text("formation translation");
            n(o.maneuver_group);
        }
    }
    void cmd(const CommandEnvelope &e) {
        n(e.tick);
        n(e.sequence);
        const auto &c = e.command;
        n(uint8_t(c.kind));
        order(c.order);
        n(c.regiment.value);
        formation(c.formation);
        n(c.unit.value);
        n(c.target.value);
        vec(c.velocity);
        pose(c.pose);
        n(c.sample_tick);
        n(command::CommandTaskHash(c.command_task));
        if (c.kind == CommandKind::SetSkirmishers)
            n(c.skirmishers_enabled);
        if (c.kind == CommandKind::SetRegimentFollowing)
            n(c.follow_enabled);
    }
    void unit(const UnitSpec &u) {
        n(u.id.value);
        n(u.regiment.value);
        text(u.name);
        if (!u.regiment)
            n(uint64_t(u.independent_team));
        pose(u.pose);
        f(u.health);
        f(u.speed);
        f(u.armour);
        f(u.archer_skill);
        n(u.archer);
        if (u.troop != UnitTroop::Infantry) {
            text("unit troop");
            n(uint8_t(u.troop));
        }
        if (u.cavalry != CavalryClass::None || u.ranged_weapon != RangedWeapon::None ||
            u.ammunition) {
            text("troop field profile");
            n(uint8_t(u.cavalry));
            n(uint8_t(u.ranged_weapon));
            n(u.ammunition);
        }
        if (u.platform != BattlePlatform::None) {
            text("battle platform");
            n(uint8_t(u.platform));
        }
        n(uint64_t(u.legacy_id));
        weapon(u.weapon);
        if (u.ranged_attack) {
            text("secondary javelin attack");
            weapon(*u.ranged_attack);
        }
        n(u.equipment.size());
        for (auto i : u.equipment)
            n(i);
    }
};
} // namespace
uint64_t StateHash(const World &w) {
    Hash h;
    h.n(w.tick);
    h.n(w.seed);
    h.n(w.next_event_id);
    h.n(w.next_order_id);
    h.n(w.last_launch_id);
    h.n(uint8_t(w.outcome));
    h.n(uint64_t(w.winner_team));
    h.n(w.controlled.value);
    const auto &rules = w.scenario.rules;
    const auto hash_regiment_profile = [&](const RegimentSpec &r) {
        if (r.loose_skirmish)
            h.text("loose skirmish");
        if (r.deployment_ticks || r.initial_fatigue != 0.f) {
            h.text("regiment deployment");
            h.n(r.deployment_ticks);
            h.f(r.initial_fatigue);
        }
    };
    h.n(w.units.size());
    for (const auto &u : w.units) {
        h.unit(u);
        h.n(u.alive);
        h.n(uint8_t(u.control));
        h.pose(u.slot);
        h.n(u.slot_index);
        h.n(u.ready_tick);
        h.vec(u.desired_velocity);
        h.n(u.attack_target.value);
        if (u.jump_started_tick || u.parry_ready_tick) {
            h.text("personal actions");
            h.n(u.jump_started_tick);
            h.f(u.jump_base_z);
            h.n(u.parry_until);
            h.n(u.parry_ready_tick);
            h.f(u.parry_yaw);
        }
        if (u.ammunition)
            h.n(u.ammunition_remaining);
        if (u.ranged_ready_tick) {
            h.text("secondary javelin cooldown");
            h.n(u.ranged_ready_tick);
        }
        if (u.dagger_drawn || u.skirmisher || u.skirmisher_returning || u.bypassing ||
            u.dagger_ready_tick || u.contact_travel != Vec3{}) {
            h.text("local combat");
            h.n(u.dagger_drawn);
            h.n(u.skirmisher);
            h.n(u.skirmisher_returning);
            h.n(u.bypassing);
            h.n(u.dagger_ready_tick);
            h.vec(u.contact_travel);
        }
    }
    h.n(w.regiments.size());
    for (const auto &r : w.regiments) {
        h.n(r.id.value);
        h.n(uint64_t(r.team));
        h.text(r.name);
        h.pose(r.pose);
        h.formation(r.formation);
        if (r.manual_frontage)
            h.text("manual_frontage");
        h.f(r.speed);
        h.f(r.turn_rate);
        h.vec(r.rally_point);
        if (r.skirmisher_count) {
            h.text("skirmisher count");
            h.n(r.skirmisher_count);
        }
        hash_regiment_profile(r);
        h.n(r.units.size());
        for (auto id : r.units)
            h.n(id.value);
        h.n(r.slots.size());
        for (auto p : r.slots)
            h.pose(p);
        h.n(r.route.size());
        for (auto p : r.route)
            h.pose(p);
        h.n(r.waypoint);
        if (r.march_lane) {
            const auto &lane = *r.march_lane;
            h.text("march lane");
            h.pose(lane.waypoint);
            h.pose(lane.goal);
            h.n(lane.order.value);
            h.f(lane.stalled_seconds);
            h.n(lane.detour.size());
            for (const Vec3 point : lane.detour)
                h.vec(point);
            h.n(lane.next);
            h.n(lane.recovery_attempts);
        }
        if (r.retreat_waypoint) {
            h.text("retreat waypoint");
            h.vec(*r.retreat_waypoint);
        }
        h.n(r.order.has_value());
        if (r.order)
            h.order(*r.order);
        h.n(r.initial_count);
        h.n(r.panic);
        h.n(r.calm_counter);
        h.n(r.last_damage_tick);
        if (r.artillery_pressure > 0.f) {
            h.text("artillery pressure");
            h.f(r.artillery_pressure);
        }
        if (r.last_hostile_shot_tick) {
            h.text("last_hostile_shot_tick");
            h.n(r.last_hostile_shot_tick);
        }
        h.n(r.panic_loss_count);
        h.n(r.decision_tick);
        h.n(r.decision_until);
        h.n(uint8_t(r.ai_action));
        h.n(r.ai_target.value);
        if (r.following_officer || r.follow_replan_tick) {
            h.text("regiment follow");
            h.n(r.following_officer.value);
            h.n(r.follow_replan_tick);
        }
        if (r.melee_engaged || r.skirmishers_enabled) {
            h.text("regiment engagement");
            h.n(r.melee_engaged);
            h.n(r.skirmishers_enabled);
        }
        if (rules.regiment_tactics || r.morale != 1.f || r.cohesion != 1.f ||
            r.fatigue != 0.f || r.disordered) {
            h.text("regiment discipline");
            h.f(r.morale);
            h.f(r.cohesion);
            h.f(r.fatigue);
            h.n(r.disordered);
        }
        if (r.cavalry_phase != CavalryPhase::None || r.cavalry_target || r.pursuit_target ||
            r.melee_opponent || r.charge_runup_m != 0.f || r.cavalry_phase_until ||
            r.brace_ready_tick || r.pursuit_check_tick || r.separate_since || r.contact_end_tick ||
            r.braced || r.discipline_window_count || r.discipline_window_until || r.ranged_losses ||
            r.melee_losses || r.melee_kills || r.ranged_checked || r.rear_checked ||
            r.retreat_overlap || r.cannot_rally || r.rout_direction != Vec3{}) {
            h.text("regiment field combat");
            h.n(uint8_t(r.cavalry_phase));
            h.n(r.cavalry_target.value);
            h.n(r.pursuit_target.value);
            h.n(r.melee_opponent.value);
            h.f(r.charge_runup_m);
            h.n(r.cavalry_phase_until);
            h.n(r.brace_ready_tick);
            h.n(r.pursuit_check_tick);
            h.n(r.separate_since);
            h.n(r.contact_end_tick);
            h.n(r.braced);
            h.n(r.discipline_window_count);
            h.n(r.discipline_window_until);
            h.n(r.ranged_losses);
            h.n(r.melee_losses);
            h.n(r.melee_kills);
            h.n(r.ranged_checked);
            h.n(r.rear_checked);
            h.n(r.retreat_overlap);
            h.n(r.cannot_rally);
            h.vec(r.rout_direction);
        }
        if (r.melee_attack_aspects) {
            h.text("melee attack aspects");
            h.n(r.melee_attack_aspects);
        }
    }
    for (const auto &o : w.officers) {
        h.n(o.id.value);
        h.n(o.unit.value);
        h.n(o.role);
        h.f(o.charisma);
        h.f(o.tactic_mind);
        h.f(o.lead_skill);
        h.f(o.lead_dist);
        h.n(uint64_t(o.legacy_id));
    }
    h.n(w.attacks.size());
    for (auto &a : w.attacks) {
        h.n(a.id.value);
        h.n(a.source.value);
        h.n(a.target.value);
        h.n(a.due_tick);
        h.f(a.damage);
        h.n(a.hit);
        h.f(a.range_m);
        h.n(a.projectile);
        if (a.charge || a.pursuit) {
            h.text("field strike");
            h.n(a.charge);
            h.n(a.pursuit);
        }
        if (a.round_shot) {
            h.text("round shot trajectory");
            h.vec(a.round_shot->origin);
            h.vec(a.round_shot->aim);
        }
    }
    h.n(w.pending_orders.size());
    for (auto &o : w.pending_orders)
        h.order(o);
    h.n(w.commands.size());
    for (auto &e : w.commands)
        h.cmd(e);
    h.n(w.accepted_sequences.size());
    for (auto s : w.accepted_sequences)
        h.n(s);
    h.f(rules.panic_fraction);
    h.n(rules.calm_ticks);
    h.n(rules.ranged_cohorts);
    h.f(rules.rally_tolerance_m);
    h.f(rules.rally_safe_radius_m);
    if (rules.local_melee)
        h.text("local_melee");
    if (rules.regiment_tactics)
        h.text("regiment_tactics");
    if (rules.risky_volley)
        h.text("risky_volley");
    if (rules.adaptive_battle)
        h.text("adaptive_battle");
    if (rules.linear_infantry)
        h.text("linear_infantry");
    h.n(w.scenario.obstacles.size());
    for (auto &o : w.scenario.obstacles) {
        h.f(o.min_x);
        h.f(o.min_y);
        h.f(o.max_x);
        h.f(o.max_y);
        if (o.blocks_projectiles)
            h.text("projectile cover");
        if (o.feature != TerrainFeature::Barrier) {
            h.text("terrain feature");
            h.n(uint8_t(o.feature));
        }
    }
    if (const auto &hill = w.scenario.hill) {
        h.text("terrain hill");
        h.f(hill->radius_x_m);
        h.f(hill->radius_y_m);
        h.f(hill->height_m);
        h.f(hill->crest_fraction);
        if (hill->relief != TerrainRelief::CentralHill)
            h.n(uint8_t(hill->relief));
    }
    if (const auto &objective = w.scenario.objective) {
        h.text("battle objective");
        h.vec(objective->center);
        h.f(objective->radius_m);
        h.n(uint64_t(objective->defending_team));
    }
    if (const auto &bounds = w.scenario.field_bounds) {
        h.text("battlefield bounds");
        h.f(bounds->half_width_m);
        h.f(bounds->half_height_m);
    }
    if (!w.smoke.empty()) {
        h.text("gunpowder smoke");
        h.n(w.smoke.size());
        for (const auto &cloud : w.smoke) {
            h.vec(cloud.center);
            h.f(cloud.radius_m);
            h.n(cloud.expires_tick);
        }
    }
    h.n(w.scenario.schema_version);
    h.n(w.scenario.command_setup.has_value());
    if (w.scenario.command_setup)
        h.n(command::CommandSetupHash(*w.scenario.command_setup));
    h.n(w.scenario.units.size());
    for (auto &u : w.scenario.units)
        h.unit(u);
    h.n(w.scenario.regiments.size());
    for (auto &r : w.scenario.regiments) {
        h.n(r.id.value);
        h.n(uint64_t(r.team));
        h.text(r.name);
        h.pose(r.pose);
        h.formation(r.formation);
        h.f(r.speed);
        h.f(r.turn_rate);
        h.vec(r.rally_point);
        if (r.skirmisher_count) {
            h.text("skirmisher count");
            h.n(r.skirmisher_count);
        }
        hash_regiment_profile(r);
    }
    h.n(w.scenario.officers.size());
    for (auto &o : w.scenario.officers) {
        h.n(o.id.value);
        h.n(o.unit.value);
        h.n(o.role);
        h.f(o.charisma);
        h.f(o.tactic_mind);
        h.f(o.lead_skill);
        h.f(o.lead_dist);
        h.n(uint64_t(o.legacy_id));
    }
    h.n(w.command_state.has_value());
    if (w.command_state)
        h.n(command::CommandStateHash(*w.command_state));
    return h.h;
}
} // namespace battle
