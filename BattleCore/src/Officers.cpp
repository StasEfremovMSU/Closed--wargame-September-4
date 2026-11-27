#include "battle/Officers.h"
#include "battle/Formation.h"
#include "battle/Control.h"
#include "battle/command/Services.h"
#include "FieldCombat.h"
#include <array>
#include <limits>
namespace battle {
namespace {
constexpr float RegimentSightRange = 60.f;

PathResult FindRoutExit(const NavigationService &navigation, const World &world,
                        const RegimentState &regiment, Vec3 limit) {
    const auto current = regiment.pose.position;
    const float x = std::clamp(current.x, -limit.x, limit.x);
    const float y = std::clamp(current.y, -limit.y, limit.y);
    // Fleeing soldiers can pass through allied formations. The navigation terrain
    // and the formation's movement sweep still govern the route around obstacles.
    PathRequest request{.start = regiment.pose, .footprint = Footprint(regiment), .face_travel = true};
    const auto *lead = FindUnit(world, regiment.units.front());
    if (world.scenario.hill && lead && lead->troop == UnitTroop::Mounted)
        request.segment_clear = [hill = *world.scenario.hill](Pose from, Pose to) {
            return HillSegmentPassable(hill, from.position, to.position, UnitTroop::Mounted);
        };
    const auto path_to = [&](Vec3 goal) {
        request.goal = goal;
        return navigation.FindPath(request);
    };
    if (Length(regiment.rout_direction) > .01f) {
        const auto ray = regiment.rout_direction;
        const auto reach = [&](float component, float from, float edge) {
            return std::abs(component) > .0001f
                ? (std::copysign(edge, component) - from) / component
                : std::numeric_limits<float>::infinity();
        };
        const float distance = std::min(reach(ray.x, x, limit.x), reach(ray.y, y, limit.y));
        const Vec3 goal{std::clamp(x + distance * ray.x, -limit.x, limit.x),
                        std::clamp(y + distance * ray.y, -limit.y, limit.y), current.z};
        auto route = path_to(goal);
        if (route.status == Status::Ok)
            return route;
    }

    std::array<Vec3, 4> goals{{{-limit.x, y, current.z}, {limit.x, y, current.z},
                               {x, -limit.y, current.z}, {x, limit.y, current.z}}};
    // With no viable attack vector, prefer the edge nearest the regiment's home
    // rally point, then the nearest remaining reachable edge.
    const std::array<float, 4> home_distances{
        std::abs(regiment.rally_point.x + limit.x), std::abs(regiment.rally_point.x - limit.x),
        std::abs(regiment.rally_point.y + limit.y), std::abs(regiment.rally_point.y - limit.y)};
    const auto first = std::ranges::min_element(home_distances) - home_distances.begin();
    std::swap(goals.front(), goals[first]);
    std::stable_sort(goals.begin() + 1, goals.end(), [&](const Vec3 &a, const Vec3 &b) {
        return Distance(current, a) < Distance(current, b);
    });
    for (const auto &goal : goals) {
        auto route = path_to(goal);
        if (route.status == Status::Ok)
            return route;
    }
    return {Status::NoPath, {}};
}

Perception ObserveRegiment(const World &w, const RegimentState &r) {
    Perception p;
    p.regiment = r.id;
    p.pose = r.pose;
    p.tick = w.tick;
    p.strength = float(r.units.size());
    p.panic = r.panic;
    unsigned archers = 0;
    float range = 0;
    float siege_range = 0;
    for (auto id : r.units)
        if (auto *u = FindUnit(w, id); u && u->alive && u->archer &&
            (!u->ammunition || u->ammunition_remaining)) {
            ++archers;
            range = std::max(range, u->weapon.range * .6f);
            if (IsSiegeEngine(u->platform))
                siege_range = std::max(siege_range, u->weapon.range * .6f);
        }
    if (siege_range > 0.f)
        p.preferred_range = siege_range;
    else if (archers > r.units.size() / 2)
        p.preferred_range = range;
    for (const auto &e : w.regiments)
        if (e.team != r.team && !e.units.empty()) {
            float d = Distance(r.pose.position, e.pose.position);
            if (d <= RegimentSightRange && (!w.scenario.hill ||
                HillHasLineOfSight(*w.scenario.hill, r.pose.position, e.pose.position)))
                p.enemies.push_back({e.id, e.pose.position, float(e.units.size()), d});
        }
    std::sort(p.enemies.begin(), p.enemies.end(), [](auto &a, auto &b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    return p;
}

void UpdateMixedRanks(World &world, RegimentState &regiment, const Perception &perception) {
    if (regiment.panic || regiment.disordered || regiment.melee_engaged ||
        !SupportsRankPriority(world, regiment))
        return;
    RankPriority automatic = RankPriority::RangedFront;
    if (regiment.rank_priority == RankPriority::Automatic) {
        if (UsesLinearInfantry(world, regiment)) {
            // Linear doctrine already distinguishes firing lines from assault columns.
            UpdateRegimentRankLayout(world, regiment, RankPriority::Automatic);
            return;
        }
        const auto own = Footprint(regiment);
        for (const auto &contact : perception.enemies) {
            const auto &enemy = *FindRegiment(world, contact.id);
            if (enemy.panic)
                continue;
            const auto delta = enemy.pose.position - regiment.pose.position;
            const float distance = std::hypot(delta.x, delta.y);
            const auto axis = distance > .001f ? Vec3{delta.x / distance, delta.y / distance, 0.f}
                                                : Forward(regiment.pose.yaw);
            const auto edge = [&](Pose pose, FormationFootprint size) {
                return .5f * (std::abs(Dot(axis, Forward(pose.yaw))) * size.depth_m +
                              std::abs(Dot(axis, Right(pose.yaw))) * size.width_m);
            };
            const float gap = distance - edge(regiment.pose, own) - edge(enemy.pose, Footprint(enemy));
            // Allow time to cross into rank. Keep the screen up until the enemy is clearly away.
            const float approach = std::max(8.f, 2.5f * FormationTravelSpeed(world, enemy));
            const float buffer = regiment.rank_layout == RankPriority::MeleeFront ? 5.f : 0.f;
            if (gap <= approach + buffer) {
                automatic = RankPriority::MeleeFront;
                break;
            }
        }
    }
    UpdateRegimentRankLayout(world, regiment, automatic);
}

void UpdateManeuverRanks(World &world, RegimentState &regiment, bool automatic_due) {
    if (regiment.rank_priority != RankPriority::Automatic) {
        UpdateRegimentRankLayout(world, regiment, regiment.rank_layout);
    } else if (automatic_due && !regiment.melee_engaged && !regiment.panic &&
               !regiment.disordered && SupportsRankPriority(world, regiment)) {
        UpdateMixedRanks(world, regiment, ObserveRegiment(world, regiment));
    }
}

std::optional<Pose> HoldingThreatFacing(const World &world, const RegimentState &regiment,
                                        const Perception &perception) {
    const auto size = Footprint(regiment);
    const float threat_range = std::max(30.f, 12.f + .5f * std::max(size.width_m, size.depth_m));
    const auto origin = regiment.pose.position;
    for (const auto &contact : perception.enemies) {
        if (contact.distance > threat_range)
            break;
        const auto delta = contact.position - origin;
        if (std::hypot(delta.x, delta.y) <= .01f ||
            !command::HasLineOfSight(origin, contact.position, world.scenario.obstacles) ||
            SmokeObscures(world, origin, contact.position))
            continue;
        return Pose{origin, std::atan2(delta.y, delta.x)};
    }
    return std::nullopt;
}

PathResult FindHoldPivot(const NavigationService &nav, const World &world,
                         const RegimentState &regiment, Pose facing) {
    if (!FormationRouteSegmentClear(world, regiment, facing))
        return {};
    auto pivot = FindFormationRoute(nav, world, regiment, facing, true);
    if (pivot.status != Status::Ok || std::ranges::any_of(pivot.waypoints,
            [&](const auto &point) { return point.position != facing.position; }))
        return {};
    return pivot;
}

const command::CommandTask *ActiveTaskForOrder(const World &world,
                                               const RegimentState &regiment) {
    if (!world.command_state || !regiment.order)
        return nullptr;
    const auto &state = *world.command_state;
    const auto runtime = std::find_if(state.nodes.begin(), state.nodes.end(), [&](const auto &node) {
        return node.last_order_id == regiment.order->id;
    });
    if (runtime == state.nodes.end())
        return nullptr;
    const auto task = std::find_if(runtime->orders.begin(), runtime->orders.end(), [&](const auto &t) {
        return t.id == runtime->memory.active_task && t.status == command::TaskStatus::Active;
    });
    return task == runtime->orders.end() ? nullptr : &*task;
}

Vec3 AttackPosition(Vec3 from, Vec3 target, float range) {
    const auto delta = target - from;
    const float distance = Length(delta);
    return distance > range ? target - delta * (range / distance) : from;
}

bool ApplyLinearInfantryTactics(World &w, RegimentState &r, const NavigationService &navigation,
                               Perception &perception, Decision &decision, bool active_order) {
    if (r.formation.shape != FormationShape::Rectangle || !UsesLinearInfantry(w, r) || r.panic ||
        (!IsHolding(decision.action) && !IsAttacking(decision.action)))
        return false;

    size_t muskets = 0;
    float range = 0.f;
    for (auto id : r.units) {
        const auto &unit = *FindUnit(w, id);
        if (unit.ranged_weapon != RangedWeapon::Musket || unit.skirmisher ||
            unit.skirmisher_returning || (unit.ammunition && !unit.ammunition_remaining))
            continue;
        ++muskets;
        range = std::max(range, unit.weapon.range * .6f);
    }
    const bool attacking = decision.action == OrderType::Attack;
    const bool direct = active_order && r.order->player_direct;
    if (!r.manual_frontage && !r.melee_engaged && !r.disordered &&
        (IsHolding(decision.action) || (attacking && !direct))) {
        const bool hold_existing_line = IsHolding(decision.action) && IsLinearFiringLine(r);
        const bool ordered_line = decision.action == OrderType::HoldLine && muskets != 0;
        const bool line = hold_existing_line || ordered_line || muskets > r.units.size() / 2;
        const auto formation = LinearInfantryFormation(r, line);
        if (r.formation != formation && !(line && IsLinearFiringLine(r))) {
            auto candidate = r;
            candidate.formation = formation;
            if (FindFormationRoute(navigation, w, candidate, r.pose).status == Status::Ok) {
                ReformRegiment(w, r.id, formation);
                r.route.clear();
                r.waypoint = 0;
                r.decision_tick = 0;
            }
        }
    }
    if (!attacking)
        return false;
    const auto target = std::ranges::find(perception.enemies, decision.target, &Contact::id);
    if (target == perception.enemies.end())
        return false;
    const bool firing_line = IsLinearFiringLine(r) && range > 0.f;
    perception.preferred_range = firing_line ? std::min(range, RegimentSightRange - 2.f)
                                             : Perception{}.preferred_range;
    decision.point = AttackPosition(r.pose.position, target->position, perception.preferred_range);
    decision.reason = firing_line ? "deploy in line and fire from range"
                                  : "close with the enemy in a bayonet assault";
    return firing_line;
}

void Emit(World &w, EventKind kind, RegimentId id, std::string reason) {
    w.events.push_back({{w.next_event_id++}, w.tick, kind, {}, {}, id, 0, std::move(reason)});
}
} // namespace
OfficerId Commander(const World &w, RegimentId reg) {
    if (w.scenario.rules.regiment_tactics && w.command_state && reg) {
        const auto &nodes = w.command_state->hierarchy.nodes;
        const auto node = std::ranges::find(nodes, reg, &command::CommandNode::regiment);
        if (node != nodes.end()) {
            const auto entry = std::ranges::find(w.officers, node->commander, &OfficerSpec::id);
            if (entry != w.officers.end()) {
                const auto *unit = FindUnit(w, entry->unit);
                if (unit && unit->alive)
                    return entry->id;
            }
            return {};
        }
    }
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
RegimentStaffEffects GetStaffEffects(const World &w, const RegimentState &regiment) {
    RegimentStaffEffects effects{w.scenario.rules.panic_fraction, w.scenario.rules.calm_ticks};
    if (w.scenario.rules.regiment_tactics) {
        for (const auto &officer : w.officers) {
            const auto *unit = FindUnit(w, officer.unit);
            if (!unit || !unit->alive || UnitTeam(w, *unit) != regiment.team)
                continue;
            const bool embedded = unit->regiment == regiment.id;
            const float reach = embedded ? std::max(regiment.formation.spacing_m, officer.lead_dist)
                                         : officer.lead_dist;
            if (reach <= 0.f || Distance(unit->pose.position, regiment.pose.position) > reach)
                continue;
            if (officer.role == 1 || officer.role == 2)
                effects.leadership = std::max(effects.leadership,
                    std::clamp(.35f + .65f * officer.charisma, 0.f, 1.f));
            else if (embedded && !w.command_state && officer.role == 3)
                effects.standard = std::max(effects.standard, std::clamp(officer.charisma, 0.f, 1.f));
            else if (embedded && !w.command_state && officer.role == 4)
                effects.music = std::max(effects.music, std::clamp(officer.lead_skill, 0.f, 1.f));
        }
        effects.leadership = std::min(1.f, effects.leadership + .05f * effects.standard);
    }
    if (!w.command_state || w.command_state->hierarchy.support.empty())
        return effects;
    const auto &hierarchy = w.command_state->hierarchy;
    const auto node = std::find_if(hierarchy.nodes.begin(), hierarchy.nodes.end(),
                                  [&](const auto &entry) { return entry.regiment == regiment.id; });
    if (node == hierarchy.nodes.end())
        return effects;
    const auto in_position = [&](OfficerId id, uint32_t role) -> const OfficerSpec * {
        const auto officer = std::lower_bound(w.officers.begin(), w.officers.end(), id,
            [](const auto &record, OfficerId target) { return record.id < target; });
        if (officer == w.officers.end() || officer->id != id || officer->role != role)
            return nullptr;
        const auto *unit = FindUnit(w, officer->unit);
        if (!unit || !unit->alive || unit->regiment != regiment.id)
            return nullptr;
        const float radius = std::max(0.f, officer->lead_dist);
        return radius > 0 && Distance(unit->pose.position, regiment.pose.position) <= radius
                   ? &*officer : nullptr;
    };
    const auto *commander = in_position(node->commander, 2);
    float standard = 0;
    float music = 0;
    for (const auto &assignment : hierarchy.support) {
        if (assignment.command != node->id)
            continue;
        if (assignment.duty == command::SupportDuty::StandardBearer) {
            if (const auto *officer = in_position(assignment.officer, 3))
                standard = std::max(standard, std::clamp(officer->charisma, 0.f, 1.f));
        } else if (assignment.duty == command::SupportDuty::Musician) {
            if (const auto *officer = in_position(assignment.officer, 4))
                music = std::max(music, std::clamp(officer->lead_skill, 0.f, 1.f));
        }
    }
    const float lead = commander ? std::clamp(commander->charisma, 0.f, 1.f) : 0.f;
    effects.leadership = std::max(effects.leadership, lead);
    effects.standard = std::max(effects.standard, standard);
    effects.music = std::max(effects.music, music);
    effects.panic_fraction = std::max(0.f, effects.panic_fraction - .06f * lead - .08f * standard);
    effects.calm_ticks -= static_cast<Tick>(double(effects.calm_ticks) * .3 * music);
    return effects;
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
    if (IsAttacking(d.action)) {
        auto target = std::find_if(p.enemies.begin(), p.enemies.end(),
                                   [&](auto &c) { return !d.target || c.id == d.target; });
        if (target == p.enemies.end()) {
            d.action = OrderType::Hold;
            d.reason = "hold: target unavailable";
        } else {
            d.target = target->id;
            d.point = AttackPosition(p.pose.position, target->position, p.preferred_range);
        }
    }
    return d;
}
void ApplyDecision(World &w, OfficerId officer, const Decision &d) {
    auto *r = FindRegiment(w, d.regiment);
    if (!r || (officer && Commander(w, r->id) != officer))
        return;
    const bool changed = r->ai_action != d.action || r->ai_target != d.target ||
        (!d.keep_route && r->route != d.path.waypoints);
    r->ai_action = d.action;
    r->ai_target = d.target;
    r->decision_until = w.tick + 20;
    if (IsHolding(d.action)) {
        if (d.keep_route)
            return;
        r->route = d.path.waypoints;
        r->waypoint = 0;
    } else if (!d.keep_route) {
        if (d.path.status != Status::Ok) {
            r->route.clear();
            r->waypoint = 0;
            const bool backing_out = w.scenario.rules.local_melee && r->melee_engaged &&
                IsWithdrawing(d.action);
            if (r->order && !backing_out)
                r->order->status = OrderStatus::Failed;
        } else {
            r->route = d.path.waypoints;
            r->waypoint = 0;
        }
    }
    if (!w.scenario.rules.regiment_tactics || changed || !r->decision_tick)
        Emit(w, EventKind::DecisionTrace, r->id, d.reason);
}
void UpdateOfficers(World &w, const NavigationService &nav) {
    const bool adaptive = w.scenario.rules.adaptive_battle;
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
        if (o.type == OrderType::FallBack && !r->order->facing && PreserveFallbackFacing(w, *r, o.point))
            r->order->facing = r->pose.yaw;
        r->pursuit_target = {};
        r->pursuit_check_tick = 0;
        r->decision_tick = 0;
        r->decision_until = 0;
        Emit(w, EventKind::OrderChanged, r->id, "order delivered");
    }
    w.pending_orders = std::move(future);
    std::vector<bool> shared_maneuver(w.regiments.size());
    for (const auto &plan : ActiveFormationTranslations(w)) {
        const bool new_plan = std::ranges::any_of(plan.members, [&](RegimentId id) {
            const auto &r = *FindRegiment(w, id);
            return !r.decision_tick || r.ai_action != OrderType::MoveTo ||
                   (r.route.empty() && w.tick >= r.decision_tick);
        });
        if (new_plan) {
            const auto route = FindFormationGroupRoute(nav, w, plan.members, plan.delta);
            const auto origin = FindRegiment(w, plan.members.front())->pose.position;
            for (const auto id : plan.members) {
                auto &r = *FindRegiment(w, id);
                r.route.clear();
                r.waypoint = 0;
                for (const auto &point : route.waypoints)
                    r.route.push_back({point.position + (r.pose.position - origin), r.pose.yaw});
                r.decision_tick = w.tick + 5;
                Emit(w, EventKind::DecisionTrace, r.id, r.route.empty()
                     ? "wait for shared passage" : "advance together on the shared route");
            }
        }
        for (const auto id : plan.members)
            shared_maneuver[FindRegiment(w, id) - w.regiments.data()] = true;
    }
    for (auto &r : w.regiments) {
        const bool follow_replan_due = w.tick >= r.follow_replan_tick;
        if (r.following_officer && UpdateRegimentFollowing(w, r, nav)) {
            UpdateManeuverRanks(w, r, follow_replan_due);
            if (r.order && r.order->expiry_tick && r.order->expiry_tick <= w.tick)
                r.order->status = OrderStatus::Cancelled;
            continue;
        }
        if (r.units.empty() || (IsDeploying(w, r) && !r.panic))
            continue;
        if (shared_maneuver[&r - w.regiments.data()]) {
            r.ai_action = OrderType::MoveTo;
            r.ai_target = {};
            // Long shared routes keep their route plan. Stagger the independent rank decisions.
            constexpr Tick RankDecisionInterval = 10;
            const bool rank_decision_due = w.tick % RankDecisionInterval == r.id.value % RankDecisionInterval ||
                                           (r.separate_since && r.separate_since == w.tick);
            UpdateManeuverRanks(w, r, rank_decision_due);
            continue;
        }
        bool emergency = false;
        if (r.ai_target) {
            auto *t = FindRegiment(w, r.ai_target);
            if (!t || t->units.empty()) {
                r.ai_target = {};
                r.route.clear();
                r.ai_action = OrderType::Hold;
                emergency = true;
                if (r.order && IsAttacking(r.order->type))
                    r.order->status = OrderStatus::Failed;
            }
        }
        if (r.order && r.order->expiry_tick && r.order->expiry_tick <= w.tick) {
            r.order->status = OrderStatus::Cancelled;
            r.route.clear();
            emergency = true;
        }
        const bool active_order = r.order && r.order->status == OrderStatus::Active;
        if (adaptive && active_order && IsAttacking(r.order->type) &&
            (r.order->target_unit || r.order->player_direct)) {
            const auto *target = r.order->target_unit ? FindUnit(w, r.order->target_unit) : nullptr;
            const auto *enemy = r.order->player_direct ?
                FindRegiment(w, target ? target->regiment : r.order->target_regiment) : nullptr;
            if ((r.order->target_unit && (!target || !target->alive)) ||
                (r.order->player_direct && (!enemy || enemy->units.empty()))) {
                r.order->status = OrderStatus::Failed;
                emergency = true;
            }
        }
        const bool completing_move = active_order &&
            (r.order->type == OrderType::MoveTo || r.order->type == OrderType::Withdraw ||
             r.order->type == OrderType::FallBack) &&
            Distance(r.pose.position, r.order->point) < .01f &&
            (!r.order->facing || std::abs(std::remainder(r.pose.yaw - *r.order->facing, 2 * Pi)) < .01f);
        if (adaptive && !emergency && !completing_move && w.tick < r.decision_tick)
            continue;
        OfficerId commander = Commander(w, r.id);
        const auto *task = ActiveTaskForOrder(w, r);
        const auto flank = task && task->intent.target_regiment == r.order->target_regiment
                               ? task->intent.flank_waypoint : std::nullopt;
        const bool positional_support = task && task->action == command::TaskKind::Support &&
                                         task->intent.target_command;
        if (!flank && completing_move && r.order->status == OrderStatus::Active) {
            bool fits = true;
            if (r.order->move_columns) {
                auto widened = r;
                widened.formation.columns = std::min(*r.order->move_columns, uint32_t(r.units.size()));
                widened.formation.shape = FormationShape::Rectangle;
                const auto plan = FindFormationRoute(nav, w, widened, r.pose);
                fits = plan.status == Status::Ok &&
                       FormationRouteSegmentClear(w, widened, r.pose) &&
                       ReformRegiment(w, r.id, widened.formation) == Status::Ok;
                if (fits && UsesLinearInfantry(w, r))
                    r.manual_frontage = true;
            }
            r.order->status = fits ? OrderStatus::Completed : OrderStatus::Failed;
            r.route.clear();
            if (!fits)
                Emit(w, EventKind::CommandRejected, r.id, "requested frontage would not fit at destination");
        }
        bool active = r.order && r.order->status == OrderStatus::Active;
        if (!emergency && !active && w.tick < r.decision_tick)
            continue;
        auto p = ObserveRegiment(w, r);
        UpdateMixedRanks(w, r, p);
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
            if (active && IsAttacking(resolved.type) && resolved.player_direct) {
                const auto *target = FindRegiment(w, resolved.target_regiment);
                if (!target || target->units.empty()) {
                    r.order->status = OrderStatus::Failed;
                    active = false;
                    emergency = true;
                }
            }
        }
        Decision d;
        const bool standing_position = !active && r.order && r.order->player_direct &&
            r.order->status == OrderStatus::Completed &&
            (r.order->type == OrderType::MoveTo || r.order->type == OrderType::FallBack);
        if (standing_position || (!commander && !active)) {
            d.regiment = r.id;
            d.point = r.pose.position;
            d.action = standing_position ? OrderType::HoldLine : OrderType::Hold;
            d.reason = standing_position ? "hold the assigned position" :
                                           "hold: no commander, awaiting explicit order";
        } else
            d = Evaluate(p, traits, active ? &resolved : nullptr);
        if (!active && !r.panic && HasOutwardRanks(r.formation.shape) && !IsWithdrawing(d.action)) {
            d.action = OrderType::HoldLine;
            d.target = {};
            d.point = r.pose.position;
            d.reason = "hold the closed formation";
        }
        if (!r.panic && r.pursuit_target) {
            if (const auto *enemy = FindRegiment(w, r.pursuit_target); enemy && enemy->panic) {
                d.action = OrderType::Pursue;
                d.target = enemy->id;
                d.point = enemy->pose.position;
                d.reason = "pursue the fleeing regiment";
            }
        }
        if (w.scenario.rules.regiment_tactics && d.action == OrderType::Attack && d.target &&
            !r.pursuit_target && FindRegiment(w, d.target)->panic) {
            d.action = OrderType::HoldLine;
            d.reason = "hold the line as the enemy breaks";
        }
        const bool approach_report = active && IsAttacking(resolved.type) &&
                                     resolved.player_direct && !r.panic &&
                                     d.action == OrderType::Hold;
        if (approach_report) {
            d.target = {};
            d.point = resolved.point;
            if (Distance(r.pose.position, resolved.point) >= .25f) {
                d.action = OrderType::MoveTo;
                d.reason = "approach the reported enemy position";
            } else {
                d.reason = "await enemy contact at the reported position";
            }
        }
        if (r.panic) {
            d.regiment = r.id;
            d.action = OrderType::Withdraw;
            d.target = {};
            d.point = r.rally_point;
            d.reason = "panic: withdraw";
            emergency = true;
            const bool rallying = active && r.order->type == OrderType::Rally && !r.cannot_rally;
            if (w.scenario.rules.regiment_tactics && w.scenario.field_bounds && !rallying) {
                if (r.retreat_waypoint)
                    continue;
                const auto shape = Footprint(r);
                const auto limit = TerrainFieldInset(*w.scenario.field_bounds, r.pose.yaw,
                                                     .5f * shape.depth_m, .5f * shape.width_m);
                if (r.ai_action == OrderType::Withdraw && r.waypoint < r.route.size()) {
                    const auto end = r.route.back().position;
                    if (std::abs(end.x) >= limit.x - .01f || std::abs(end.y) >= limit.y - .01f)
                        continue;
                }
                if (r.ai_action == OrderType::Withdraw && w.tick < r.decision_tick)
                    continue;
                d.path = FindRoutExit(nav, w, r, limit);
                d.reason = "panic: flee to the battlefield edge";
                if (d.path.status != Status::Ok)
                    d.path = {Status::Ok, {}};
                ApplyDecision(w, commander, d);
                r.decision_tick = w.tick + 10;
                continue;
            }
        }
        if (IsWithdrawing(d.action)) {
            if (r.retreat_waypoint)
                continue;
            if (d.action != OrderType::FallBack)
                d.point = r.rally_point;
        }
        if (!emergency && !active && w.tick < r.decision_until && d.action != r.ai_action) {
            r.decision_tick = w.tick + 10;
            continue;
        }
        const bool line_fire = ApplyLinearInfantryTactics(w, r, nav, p, d, active);
        const bool fixed_battery = std::ranges::any_of(r.units, [&](UnitId id) {
            return FindUnit(w, id)->platform == BattlePlatform::Ballista;
        });
        const bool fire_position = IsAttacking(d.action) && d.target &&
            (fixed_battery || (Distance(r.pose.position, d.point) < .01f &&
                (line_fire || std::ranges::any_of(r.units, [&](UnitId id) {
                    const auto &unit = *FindUnit(w, id);
                    return IsSiegeEngine(unit.platform) && unit.ammunition_remaining;
                }))));
        if (fire_position) {
            d.point = r.pose.position;
            const auto *target = resolved.target_unit ? FindUnit(w, resolved.target_unit) : nullptr;
            const auto point = target ? target->pose.position : FindRegiment(w, d.target)->pose.position;
            const auto delta = point - r.pose.position;
            const Pose aim{r.pose.position, std::atan2(delta.y, delta.x)};
            if (std::abs(std::remainder(aim.yaw - r.pose.yaw, 2 * Pi)) >= .01f) {
                d.keep_route = r.waypoint < r.route.size() && r.route.back() == aim;
                if (!d.keep_route)
                    d.path = FindFormationRoute(nav, w, r, aim);
            }
        } else if (fixed_battery && !IsHolding(d.action) && Distance(r.pose.position, d.point) >= .01f) {
            d.path.status = Status::NoPath;
        } else if (IsHolding(d.action) && !r.melee_engaged) {
            const auto prescribed = active ? resolved.facing : standing_position ? r.order->facing
                                                                                : std::nullopt;
            const bool assigned_guard = task && (task->action == command::TaskKind::HoldArea ||
                                                 task->action == command::TaskKind::Defend);
            const bool holding_post = standing_position ||
                (active && (resolved.player_direct || assigned_guard || !w.command_state));
            if (prescribed) {
                const Pose facing{r.pose.position, *prescribed};
                if (std::abs(std::remainder(r.pose.yaw - facing.yaw, 2 * Pi)) >= .01f) {
                    d.keep_route = r.waypoint < r.route.size() && r.route.back() == facing &&
                                   r.route[r.waypoint].position == facing.position;
                    if (!d.keep_route)
                        d.path = FindHoldPivot(nav, w, r, facing);
                }
            } else if (w.scenario.rules.regiment_tactics && holding_post && !approach_report) {
                // A hold may turn its front toward local pressure, but it does not leave its post.
                constexpr float FacingTolerance = .08f;
                const auto aim = HoldingThreatFacing(w, r, p);
                if (aim) {
                    d.keep_route = IsHolding(r.ai_action) && r.waypoint < r.route.size() &&
                        r.route[r.waypoint].position == aim->position &&
                        r.route.back().position == aim->position &&
                        std::abs(std::remainder(r.route.back().yaw - aim->yaw, 2 * Pi)) < FacingTolerance;
                    if (!d.keep_route &&
                        std::abs(std::remainder(r.pose.yaw - aim->yaw, 2 * Pi)) >= FacingTolerance)
                        d.path = FindHoldPivot(nav, w, r, *aim);
                }
            }
        } else if (!IsHolding(d.action)) {
            bool existing_route = !r.route.empty() && Distance(r.route.back().position, d.point) < .25f;
            const bool at_flank = flank && Distance(r.pose.position, flank->position) <= .35f &&
                std::abs(std::remainder(r.pose.yaw - flank->yaw, 2 * Pi)) < .2f;
            if (flank && d.action == OrderType::MoveTo) {
                // The regiment brain owns the transition from this maneuver to Attack.
                // Do not complete or replan a route already at the flank between brain ticks.
                if (at_flank)
                    continue;
                existing_route = existing_route && r.waypoint < r.route.size() &&
                    CanContinueFormationMarch(w, r, r.route[r.waypoint]);
                if (!existing_route && !emergency && w.tick < r.decision_tick && !r.route.empty())
                    continue;
                if (!existing_route)
                    d.path = FindFlankRoute(nav, w, r, *flank, d.target);
            } else if (flank && d.action == OrderType::Attack && active &&
                       (r.melee_engaged || (at_flank && w.tick - r.order->delivery_tick < 15))) {
                // Let soldiers close the final gap from the assigned position. The target's
                // center may be across impassable terrain or inside the opposing formation.
                existing_route = false;
            } else if (!flank && (active || r.pursuit_target) && w.scenario.rules.local_melee &&
                       IsAttacking(d.action) && d.target &&
                       p.preferred_range <= Perception{}.preferred_range) {
                const bool same_attack = r.ai_action == d.action && r.ai_target == d.target;
                if (same_attack && !emergency && w.tick < r.decision_tick)
                    continue;
                const auto *enemy = FindRegiment(w, d.target);
                const auto a = Footprint(r), b = Footprint(*enemy);
                const float contact_radius = .5f *
                    (std::hypot(a.width_m, a.depth_m) + std::hypot(b.width_m, b.depth_m));
                existing_route = same_attack && (r.melee_engaged ||
                    (r.waypoint < r.route.size() &&
                     Distance(r.route.back().position, enemy->pose.position) <= contact_radius + 1 &&
                     FormationRouteSegmentClear(w, r, r.route[r.waypoint])));
                if (!existing_route)
                    d.path = FindAttackRoute(nav, w, r, enemy->pose, d.target);
                if (!existing_route && d.path.status != Status::Ok) {
                    // Occupied lanes can reopen as other formations advance or withdraw.
                    d.path = {Status::Ok, {}};
                    d.reason = "wait for a clear approach to the enemy line";
                }
            } else if (line_fire || ((positional_support || approach_report) && d.action == OrderType::MoveTo)) {
                if (!emergency && w.tick < r.decision_tick)
                    continue;
                existing_route = existing_route && r.waypoint < r.route.size() &&
                    FormationRouteSegmentClear(w, r, r.route[r.waypoint]);
                if (!existing_route)
                    d.path = FindSupportRoute(nav, w, r, d.point,
                                              approach_report ? resolved.target_regiment : RegimentId{});
                if (!existing_route && d.path.status != Status::Ok) {
                    d.path = {Status::Ok, {}};
                    d.reason = line_fire ? "wait for a clear route to a firing position"
                               : approach_report ? "wait for a clear route to the reported position"
                                               : "wait for a clear route to the supporting position";
                }
            } else if (active && d.action == OrderType::FallBack) {
                const bool back_step = PreserveFallbackFacing(w, r, d.point);
                const auto delta = d.point - r.pose.position;
                const float travel_yaw = Length(delta) > .01f ? std::atan2(delta.y, delta.x) : r.pose.yaw;
                const Pose end{d.point, resolved.facing.value_or(back_step ? r.pose.yaw : travel_yaw)};
                existing_route = existing_route && r.waypoint < r.route.size() &&
                                 FormationRouteSegmentClear(w, r, r.route[r.waypoint]);
                if (!existing_route) {
                    d.path = FindFormationRoute(nav, w, r, end, back_step);
                    if (r.melee_engaged && d.path.status == Status::NoPath)
                        d.path = {Status::Ok, {}};
                }
            } else if (active && d.action == OrderType::MoveTo && !positional_support &&
                       (resolved.facing || resolved.move_columns)) {
                const auto delta = d.point - r.pose.position;
                const float yaw = resolved.facing.value_or(Length(delta) > .01f
                                      ? std::atan2(delta.y, delta.x) : r.pose.yaw);
                const Pose end{d.point, yaw};
                const auto player_destination = resolved.player_direct ? std::optional<Pose>{end} : std::nullopt;
                existing_route = existing_route && (!resolved.facing || r.route.back() == end) &&
                                 (r.waypoint == r.route.size() ||
                                  CanContinueFormationMarch(w, r, r.route[r.waypoint], player_destination));
                if (!existing_route)
                    d.path = FindFormationRoute(nav, w, r, end);
            } else if (const auto &lane = r.march_lane;
                       existing_route && active && d.action == OrderType::MoveTo && !resolved.player_direct &&
                       w.scenario.field_bounds && w.tick >= r.decision_tick && lane && lane->order == resolved.id &&
                       lane->detour.empty() && lane->recovery_attempts == MarchLane::MaxAttempts &&
                       lane->stalled_seconds >= MarchLane::StallSeconds) {
                // The original marching route used the terrain grid. Once a regiment has waited
                // and tried local detours, route around live formations or report the failed move.
                existing_route = false;
                d.path = FindFormationRoute(nav, w, r, {d.point, r.route.back().yaw});
                d.reason = d.path.status == Status::Ok ? "reroute around occupied formations"
                                                       : "assigned movement route blocked by formations";
            } else if (!existing_route) {
                const auto *lead = FindUnit(w, r.units.front());
                const bool face_travel = w.scenario.rules.regiment_tactics &&
                    (r.panic || (lead && lead->troop == UnitTroop::Mounted)) &&
                    (d.action == OrderType::MoveTo || IsWithdrawing(d.action));
                d.path = nav.FindPath({.start = r.pose, .goal = d.point, .footprint = Footprint(r),
                                       .face_travel = face_travel});
            }
            d.keep_route = existing_route;
        }
        if (emergency || !active || w.tick >= r.decision_tick || r.route.empty()) {
            ApplyDecision(w, commander, d);
            if (adaptive) {
                constexpr Tick interval = 20;
                const Tick phase = (r.id.value % interval * 17) % interval;
                const Tick delay = 1 + (phase + interval - 1 - w.tick % interval) % interval;
                r.decision_tick = delay > UINT64_MAX - w.tick ? UINT64_MAX : w.tick + delay;
            } else {
                r.decision_tick = w.tick + 10;
            }
        }
    }
}
} // namespace battle
