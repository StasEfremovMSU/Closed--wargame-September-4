#include "battle/Snapshot.h"
#include <bit>
#include <map>
#include <set>
namespace battle {
Snapshot Capture(const World &w) {
    return {1, w};
}
Result<World> Restore(const Snapshot &s) {
    auto bad = [](std::string message) {
        return Result<World>{Status::InvalidInput, {}, {{"snapshot", 0, std::move(message)}}};
    };
    if (s.schema_version != 1)
        return bad("unsupported snapshot version");
    auto valid = ValidateScenario(s.state.scenario);
    if (!valid)
        return {valid.status, {}, valid.diagnostics};
    World w = s.state;
    std::sort(w.units.begin(), w.units.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(w.regiments.begin(), w.regiments.end(), [](auto &a, auto &b) { return a.id < b.id; });
    if (w.units.size() != w.scenario.units.size() ||
        w.regiments.size() != w.scenario.regiments.size() ||
        w.officers.size() != w.scenario.officers.size())
        return bad("snapshot roster differs from scenario");
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
            !Finite(u.desired_velocity))
            return bad("invalid runtime unit");
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
    for (const auto &r : w.regiments) {
        if (r.initial_count < r.units.size() || r.waypoint > r.route.size() ||
            r.panic_loss_count > r.initial_count || r.last_damage_tick > w.tick ||
            uint8_t(r.ai_action) > uint8_t(OrderType::Rally) ||
            (r.ai_target && !FindRegiment(w, r.ai_target)))
            return bad("invalid runtime regiment");
        for (auto p : r.slots)
            if (!Finite(p))
                return bad("invalid formation slot");
        for (auto p : r.route)
            if (!Finite(p))
                return bad("invalid route pose");
        for (auto id : r.units) {
            auto *u = FindUnit(w, id);
            if (!u || !u->alive || u->regiment != r.id || u->slot_index >= r.slots.size() ||
                !members.insert(id.value).second)
                return bad("duplicate or missing live membership");
        }
        if (r.order &&
            (r.order->recipient != r.id || uint8_t(r.order->type) > uint8_t(OrderType::Rally) ||
             uint8_t(r.order->status) > uint8_t(OrderStatus::Cancelled) || !Finite(r.order->point)))
            return bad("invalid current order");
    }
    size_t controlled = 0;
    for (const auto &u : w.units) {
        if (u.alive && !members.contains(u.id.value))
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
            !std::isfinite(a.damage) || a.damage < 0 || !std::isfinite(a.range_m) || a.range_m <= 0)
            return bad("invalid pending attack");
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
            e.tick < w.tick || uint8_t(e.command.kind) > uint8_t(CommandKind::ExternalPoseSample) ||
            !Finite(e.command.pose) || !Finite(e.command.velocity) ||
            !Finite(e.command.order.point))
            return bad("invalid queued command");
        if (e.command.kind == CommandKind::MoveControlledUnit)
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
            uint8_t(o.type) > uint8_t(OrderType::Rally) ||
            uint8_t(o.status) > uint8_t(OrderStatus::Cancelled) || !Finite(o.point))
            return bad("invalid pending order");
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
    }
    void unit(const UnitSpec &u) {
        n(u.id.value);
        n(u.regiment.value);
        text(u.name);
        pose(u.pose);
        f(u.health);
        f(u.speed);
        f(u.armour);
        f(u.archer_skill);
        n(u.archer);
        n(uint64_t(u.legacy_id));
        weapon(u.weapon);
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
    }
    h.n(w.regiments.size());
    for (const auto &r : w.regiments) {
        h.n(r.id.value);
        h.n(uint64_t(r.team));
        h.text(r.name);
        h.pose(r.pose);
        h.formation(r.formation);
        h.f(r.speed);
        h.f(r.turn_rate);
        h.vec(r.rally_point);
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
        h.n(r.order.has_value());
        if (r.order)
            h.order(*r.order);
        h.n(r.initial_count);
        h.n(r.panic);
        h.n(r.calm_counter);
        h.n(r.last_damage_tick);
        h.n(r.panic_loss_count);
        h.n(r.decision_tick);
        h.n(r.decision_until);
        h.n(uint8_t(r.ai_action));
        h.n(r.ai_target.value);
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
    const auto &rules = w.scenario.rules;
    h.f(rules.panic_fraction);
    h.n(rules.calm_ticks);
    h.f(rules.rally_tolerance_m);
    h.f(rules.rally_safe_radius_m);
    h.n(w.scenario.obstacles.size());
    for (auto &o : w.scenario.obstacles) {
        h.f(o.min_x);
        h.f(o.min_y);
        h.f(o.max_x);
        h.f(o.max_y);
    }
    h.n(w.scenario.schema_version);
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
    return h.h;
}
} // namespace battle
