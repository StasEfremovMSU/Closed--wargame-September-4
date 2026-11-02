#include "battle/Combat.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
#include <map>
#include <set>
namespace battle {
namespace {
float Roll(uint64_t seed, Tick tick, UnitId unit) {
    uint64_t x = seed ^ (tick * 0x9e3779b97f4a7c15ull) ^ (unit.value * 0xbf58476d1ce4e5b9ull) ^
                 0x4152524f57ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= x >> 31;
    return float(x >> 40) * (1.f / 16777216.f);
}
void Emit(World &w, EventKind kind, UnitId source, UnitId target, RegimentId reg, float amount,
          std::string reason, EventId cause = {}) {
    BattleEvent e{{w.next_event_id++}, w.tick + 1, kind, source, target, reg, amount,
                  std::move(reason)};
    e.cause = cause;
    w.events.push_back(std::move(e));
    ++w.combat_events;
}
} // namespace
uint32_t RangedPhase(UnitId unit, RegimentId regiment, uint32_t cohorts) {
    if (!cohorts)
        return 0;
    uint64_t x = unit.value ^ (regiment.value * 0x9e3779b97f4a7c15ull);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= x >> 31;
    return uint32_t(x % cohorts);
}
float HitProbability(float base, float skill, float distance, float range) {
    return range > 0 ? std::clamp(base * skill * (1 - distance / range), 0.f, 1.f) : 0;
}
void BuildCombatEvents(const World &w, std::vector<BattleEvent> &out, uint64_t *checks) {
    out.clear();
    std::optional<int32_t> first_team;
    bool enemies = false;
    for (const auto &r : w.regiments)
        if (!r.units.empty()) {
            if (!first_team)
                first_team = r.team;
            else if (*first_team != r.team) {
                enemies = true;
                break;
            }
        }
    if (!enemies)
        return;
    struct Shooter {
        size_t index;
        int32_t team;
    };
    std::vector<Shooter> shooters;
    for (size_t i = 0; i < w.units.size(); ++i) {
        const auto &u = w.units[i];
        if (!u.alive || u.ready_tick > w.tick)
            continue;
        auto *r = FindRegiment(w, u.regiment);
        if (!r || r->panic)
            continue;
        if (u.archer && u.control != ControlMode::PlayerControlled) {
            // Exact cooldown completion preserves cadence even for non-divisible periods.
            // Idle retries/first acquisition use a stable phase, never a global volley barrier.
            bool due = u.ready_tick != 0 && u.ready_tick == w.tick;
            if (!due && w.tick % w.scenario.rules.ranged_cohorts !=
                            RangedPhase(u.id, u.regiment, w.scenario.rules.ranged_cohorts))
                continue;
        }
        shooters.push_back({i, r->team});
    }
    if (shooters.empty())
        return;
    bool need_index = std::any_of(shooters.begin(), shooters.end(), [&](auto s) {
        return w.units[s.index].control != ControlMode::PlayerControlled;
    });
    SpatialIndex index;
    if (need_index) {
        std::vector<UnitPosition> positions;
        positions.reserve(w.units.size());
        for (size_t i = 0; i < w.units.size(); ++i) {
            auto &u = w.units[i];
            if (u.alive)
                if (auto *r = FindRegiment(w, u.regiment))
                    positions.push_back({u.id, u.pose.position, r->team, i});
        }
        index.Rebuild(positions);
    }
    for (const auto &shooter : shooters) {
        const auto &u = w.units[shooter.index];
        const UnitState *target = nullptr;
        if (u.control == ControlMode::PlayerControlled) {
            auto *t = FindUnit(w, u.attack_target);
            if (t && t->alive && FindRegiment(w, t->regiment)->team != shooter.team &&
                Distance(u.pose.position, t->pose.position) <= u.weapon.range)
                target = t;
        } else if (auto nearest = index.FindNearestEnemy(u.pose.position, u.weapon.range,
                                                         shooter.team, checks)) {
            // World units are not mutated until commit; payload indices remain valid here.
            target = &w.units[nearest->payload_index];
        }
        if (!target)
            continue;
        float distance = Distance(u.pose.position, target->pose.position);
        BattleEvent e;
        e.id = {w.next_event_id + out.size()};
        e.tick = w.tick;
        e.kind = EventKind::AttackLaunched;
        e.source = u.id;
        e.target = target->id;
        e.regiment = u.regiment;
        e.amount = std::max(0.f, u.weapon.damage - target->armour);
        e.projectile = u.archer;
        e.hit = !u.archer ||
                Roll(w.seed, w.tick, u.id) < HitProbability(u.weapon.base_accuracy, u.archer_skill,
                                                            distance, u.weapon.range);
        Tick duration =
            u.archer ? Tick(std::max(
                           1.0, std::ceil(double(distance) / u.weapon.projectile_speed_mps / .05)))
                     : u.weapon.windup_ticks;
        e.due_tick = w.tick + duration;
        e.reason = u.archer ? "ranged attack" : "melee attack";
        out.push_back(std::move(e));
    }
}
void CommitCombatEvents(World &w, std::span<const BattleEvent> launches) {
    std::vector<BattleEvent> ordered(launches.begin(), launches.end());
    std::sort(ordered.begin(), ordered.end(), [](auto &a, auto &b) { return a.id < b.id; });
    for (const auto &e : ordered) {
        if (e.kind != EventKind::AttackLaunched || !e.id || e.id.value <= w.last_launch_id)
            continue;
        auto *u = FindUnit(w, e.source);
        auto *t = FindUnit(w, e.target);
        if (!u || !t)
            continue;
        w.last_launch_id = e.id.value;
        w.next_event_id = std::max(w.next_event_id, e.id.value + 1);
        u->ready_tick = w.tick + u->weapon.cooldown_ticks;
        w.attacks.push_back(
            {e.id, e.source, e.target, e.due_tick, e.amount, e.hit, u->weapon.range, e.projectile});
        w.events.push_back(e);
        ++w.combat_events;
    }
    std::sort(w.attacks.begin(), w.attacks.end(), [](auto &a, auto &b) {
        return a.due_tick != b.due_tick ? a.due_tick < b.due_tick : a.id < b.id;
    });
    struct Damage {
        double amount{};
        UnitId source{};
        EventId cause{};
    };
    std::map<uint64_t, Damage> damage;
    std::vector<PendingAttack> pending;
    for (const auto &a : w.attacks) {
        if (a.due_tick > w.tick + 1) {
            pending.push_back(a);
            continue;
        }
        auto *t = FindUnit(w, a.target);
        auto *s = FindUnit(w, a.source);
        bool hit = a.hit && t && t->alive && s;
        if (hit && !a.projectile)
            hit = s->alive && Distance(s->pose.position, t->pose.position) <= a.range_m + .0001f;
        if (hit) {
            Emit(w, EventKind::Hit, a.source, a.target, t->regiment, a.damage, "attack resolved",
                 a.id);
            auto &d = damage[a.target.value];
            d.amount += a.damage;
            d.source = a.source;
            d.cause = a.id;
        } else
            Emit(w, EventKind::Miss, a.source, a.target, {}, 0, "miss or unavailable target", a.id);
    }
    w.attacks = std::move(pending);
    std::set<RegimentId> close_ranks;
    for (auto &[id, d] : damage) {
        auto *u = FindUnit(w, {id});
        if (!u || !u->alive)
            continue;
        u->health = float(std::max(0.0, double(u->health) - d.amount));
        auto *r = FindRegiment(w, u->regiment);
        if (d.amount > 0)
            r->last_damage_tick = w.tick + 1;
        if (u->health <= 0) {
            u->alive = false;
            u->control = ControlMode::Dead;
            std::erase(r->units, u->id);
            close_ranks.insert(r->id);
            if (w.controlled == u->id) {
                w.controlled = {};
                Emit(w, EventKind::ControlChanged, {}, u->id, u->regiment, 0,
                     "controlled unit died");
            }
            Emit(w, EventKind::Died, d.source, u->id, u->regiment, 0, "unit died", d.cause);
        }
    }
    for (auto id : close_ranks) {
        auto *r = FindRegiment(w, id);
        if (r->units.empty())
            continue;
        auto formation = r->formation;
        formation.columns = std::min(formation.columns, uint32_t(r->units.size()));
        ReformRegiment(w, id, formation);
    }
    std::set<int32_t> initial, alive;
    for (auto &r : w.regiments) {
        initial.insert(r.team);
        if (!r.units.empty())
            alive.insert(r.team);
    }
    if (initial.size() >= 2 && alive.size() <= 1) {
        w.outcome = alive.empty() ? BattleOutcome::Draw : BattleOutcome::Victory;
        if (!alive.empty())
            w.winner_team = *alive.begin();
    }
}
void UpdateMorale(World &w) {
    for (auto &r : w.regiments) {
        if (r.units.empty())
            continue;
        uint64_t count = r.units.size();
        if (!r.panic && r.initial_count &&
            double(count) / double(r.initial_count) < w.scenario.rules.panic_fraction &&
            (r.panic_loss_count == 0 || count < r.panic_loss_count)) {
            r.panic = true;
            r.panic_loss_count = count;
            r.calm_counter = 0;
            r.route.clear();
            r.decision_tick = 0;
            r.decision_until = 0;
            if (r.order &&
                (r.order->type == OrderType::Attack || r.order->type == OrderType::MoveTo))
                r.order->status = OrderStatus::Cancelled;
            Emit(w, EventKind::OrderChanged, {}, {}, r.id, 0, "panic: withdraw to rally point");
        }
        if (!r.panic)
            continue;
        bool enemy = false;
        for (auto &u : w.units)
            if (u.alive && FindRegiment(w, u.regiment)->team != r.team &&
                Distance(u.pose.position, r.pose.position) < w.scenario.rules.rally_safe_radius_m) {
                enemy = true;
                break;
            }
        bool calm =
            r.order && r.order->type == OrderType::Rally &&
            r.order->status == OrderStatus::Active && Commander(w, r.id) &&
            Distance(r.pose.position, r.rally_point) <= w.scenario.rules.rally_tolerance_m &&
            !enemy && (r.last_damage_tick == 0 || r.last_damage_tick < w.tick);
        if (calm)
            ++r.calm_counter;
        else
            r.calm_counter = 0;
        if (r.calm_counter >= w.scenario.rules.calm_ticks) {
            r.panic = false;
            r.calm_counter = 0;
            r.ai_action = OrderType::Hold;
            r.ai_target = {};
            r.route.clear();
            r.order->status = OrderStatus::Completed;
            Emit(w, EventKind::OrderChanged, {}, {}, r.id, 0, "rallied: hold");
        }
    }
}
} // namespace battle
