#include "battle/Officers.h"
#include "battle/Formation.h"
namespace battle {
namespace {
Perception ObserveRegiment(const World &w, const RegimentState &r) {
    Perception p;
    p.regiment = r.id;
    p.pose = r.pose;
    p.tick = w.tick;
    p.strength = float(r.units.size());
    p.panic = r.panic;
    unsigned archers = 0;
    float range = 0;
    for (auto id : r.units)
        if (auto *u = FindUnit(w, id); u && u->alive && u->archer) {
            ++archers;
            range = std::max(range, u->weapon.range * .6f);
        }
    if (archers > r.units.size() / 2)
        p.preferred_range = range;
    for (const auto &e : w.regiments)
        if (e.team != r.team && !e.units.empty()) {
            float d = Distance(r.pose.position, e.pose.position);
            if (d <= 60)
                p.enemies.push_back({e.id, e.pose.position, float(e.units.size()), d});
        }
    std::sort(p.enemies.begin(), p.enemies.end(), [](auto &a, auto &b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    return p;
}
void Emit(World &w, EventKind kind, RegimentId id, std::string reason) {
    w.events.push_back({{w.next_event_id++}, w.tick, kind, {}, {}, id, 0, std::move(reason)});
}
} // namespace
OfficerId Commander(const World &w, RegimentId reg) {
    const OfficerSpec *best = nullptr;
    for (const auto &o : w.officers) {
        auto *u = FindUnit(w, o.unit);
        if (!u || !u->alive || u->regiment != reg || (o.role != 1 && o.role != 2))
            continue;
        if (!best || o.role > best->role || (o.role == best->role && o.id < best->id))
            best = &o;
    }
    return best ? best->id : OfficerId{};
}
Perception Observe(const World &w, OfficerId id, const SpatialIndex &index) {
    (void)index;
    for (const auto &o : w.officers)
        if (o.id == id) {
            auto *u = FindUnit(w, o.unit);
            if (u && u->alive)
                if (auto *r = FindRegiment(w, u->regiment))
                    return ObserveRegiment(w, *r);
        }
    return {};
}
Decision Evaluate(const Perception &p, const OfficerTraits &traits, const Order *order) {
    Decision d;
    d.regiment = p.regiment;
    d.point = p.pose.position;
    d.reason = "hold: no reachable observed enemy";
    if (order) {
        d.action = order->type;
        d.point = order->point;
        d.target = order->target_regiment;
        d.score = 1000;
        d.reason = "execute delivered order";
    } else if (!p.enemies.empty()) {
        float attack = 10 * (p.strength / std::max(1.f, p.enemies[0].strength)) *
                       (.5f + std::clamp(traits.tactic_mind, 0.f, 1.f));
        d.scores = {{OrderType::Hold, 5}, {OrderType::Attack, attack}};
        if (attack > 5) {
            d.action = OrderType::Attack;
            d.target = p.enemies[0].id;
            d.score = attack;
            d.reason = "attack: strength and tactical confidence";
        }
    }
    if (d.action == OrderType::Attack) {
        auto target = std::find_if(p.enemies.begin(), p.enemies.end(),
                                   [&](auto &c) { return !d.target || c.id == d.target; });
        if (target == p.enemies.end()) {
            d.action = OrderType::Hold;
            d.reason = "hold: target unavailable";
        } else {
            d.target = target->id;
            Vec3 delta = target->position - p.pose.position;
            float distance = Length(delta);
            d.point = distance > p.preferred_range
                          ? target->position - delta * (p.preferred_range / distance)
                          : p.pose.position;
        }
    }
    return d;
}
void ApplyDecision(World &w, OfficerId officer, const Decision &d) {
    auto *r = FindRegiment(w, d.regiment);
    if (!r || (officer && Commander(w, r->id) != officer))
        return;
    r->ai_action = d.action;
    r->ai_target = d.target;
    r->decision_until = w.tick + 20;
    if (d.action == OrderType::Hold) {
        r->route.clear();
        r->waypoint = 0;
    } else if (!d.keep_route) {
        if (d.path.status != Status::Ok) {
            r->route.clear();
            r->waypoint = 0;
            if (r->order)
                r->order->status = OrderStatus::Failed;
        } else {
            r->route = d.path.waypoints;
            r->waypoint = 0;
        }
    }
    Emit(w, EventKind::DecisionTrace, r->id, d.reason);
}
void UpdateOfficers(World &w, const NavigationService &nav) {
    std::sort(w.pending_orders.begin(), w.pending_orders.end(), [](auto &a, auto &b) {
        return a.delivery_tick != b.delivery_tick ? a.delivery_tick < b.delivery_tick : a.id < b.id;
    });
    std::vector<Order> future;
    for (auto o : w.pending_orders) {
        if (o.delivery_tick > w.tick) {
            future.push_back(o);
            continue;
        }
        auto *r = FindRegiment(w, o.recipient);
        if (!r)
            continue;
        if (o.expiry_tick && o.expiry_tick <= w.tick) {
            Emit(w, EventKind::CommandRejected, r->id, "order expired before delivery");
            continue;
        }
        if (r->order && r->order->status == OrderStatus::Active &&
            r->order->priority > o.priority) {
            Emit(w, EventKind::CommandRejected, r->id, "lower priority order rejected");
            continue;
        }
        o.status = OrderStatus::Active;
        r->order = o;
        r->decision_tick = 0;
        r->decision_until = 0;
        Emit(w, EventKind::OrderChanged, r->id, "order delivered");
    }
    w.pending_orders = std::move(future);
    for (auto &r : w.regiments) {
        if (r.units.empty())
            continue;
        OfficerId commander = Commander(w, r.id);
        bool emergency = false;
        if (r.ai_target) {
            auto *t = FindRegiment(w, r.ai_target);
            if (!t || t->units.empty()) {
                r.ai_target = {};
                r.route.clear();
                r.ai_action = OrderType::Hold;
                emergency = true;
                if (r.order && r.order->type == OrderType::Attack)
                    r.order->status = OrderStatus::Failed;
            }
        }
        if (r.order && r.order->expiry_tick && r.order->expiry_tick <= w.tick) {
            r.order->status = OrderStatus::Cancelled;
            r.route.clear();
            emergency = true;
        }
        if (r.order && r.order->status == OrderStatus::Active &&
            (r.order->type == OrderType::MoveTo || r.order->type == OrderType::Withdraw) &&
            Distance(r.pose.position, r.order->point) < .05f) {
            r.order->status = OrderStatus::Completed;
            r.route.clear();
        }
        bool active = r.order && r.order->status == OrderStatus::Active;
        if (!emergency && !active && w.tick < r.decision_tick)
            continue;
        auto p = ObserveRegiment(w, r);
        OfficerTraits traits;
        for (auto &o : w.officers)
            if (o.id == commander)
                traits = {o.charisma, o.tactic_mind, o.lead_skill, o.lead_dist};
        Order resolved;
        if (active) {
            resolved = *r.order;
            if (resolved.target_unit) {
                if (auto *u = FindUnit(w, resolved.target_unit); u && u->alive)
                    resolved.target_regiment = u->regiment;
                else {
                    r.order->status = OrderStatus::Failed;
                    active = false;
                    emergency = true;
                }
            }
        }
        Decision d;
        if (!commander && !active) {
            d.regiment = r.id;
            d.point = r.pose.position;
            d.reason = "hold: no commander, awaiting explicit order";
        } else
            d = Evaluate(p, traits, active ? &resolved : nullptr);
        if (r.panic) {
            d.regiment = r.id;
            d.action = OrderType::Withdraw;
            d.target = {};
            d.point = r.rally_point;
            d.reason = "panic: withdraw";
            emergency = true;
        }
        if (d.action == OrderType::Withdraw || d.action == OrderType::Rally)
            d.point = r.rally_point;
        if (!emergency && !active && w.tick < r.decision_until && d.action != r.ai_action) {
            r.decision_tick = w.tick + 10;
            continue;
        }
        if (d.action != OrderType::Hold) {
            if (!r.route.empty() && Distance(r.route.back().position, d.point) < .25f)
                d.keep_route = true;
            else
                d.path = nav.FindPath({r.pose, d.point, Footprint(r)});
        }
        if (emergency || !active || w.tick >= r.decision_tick || r.route.empty()) {
            ApplyDecision(w, commander, d);
            r.decision_tick = w.tick + 10;
        }
    }
}
} // namespace battle
