#include "RegimentDiscipline.h"
#include "FieldCombat.h"
#include "Routing.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
#include <limits>
#include <set>

namespace battle {
namespace {
constexpr float TickSeconds = .05f;
constexpr float PanicThreshold = .22f;
constexpr float DisorderThreshold = .56f;
constexpr float ReformThreshold = .76f;
// At 20 Hz a two-second window takes the place of the tabletop turn's
// shooting and melee resolution. Discrete shocks are tested once per window.
constexpr Tick DisciplineWindowTicks = 40;
constexpr uint8_t FrontAttack = 1, RearAttack = 2;

struct Pressure {
    float morale{}, cohesion{}, cavalry_disruption{};
    unsigned casualties{}, ranged_losses{}, melee_losses{}, melee_kills{}, artillery_shots{};
    bool rear_hit{}, commander_lost{}, army_commander_lost{};
    uint8_t melee_aspects{};
    size_t retreat_size{};
    Vec3 escape{};
};

void CollectImpact(const World &world, std::vector<Pressure> &pressures) {
    std::set<std::pair<RegimentId, EventId>> artillery_impacts;
    for (const auto &event : world.events) {
        if (event.kind == EventKind::CavalryImpact && event.amount > 0.f) {
            const auto *attacker = FindUnit(world, event.source);
            const auto *defending = FindRegiment(world, event.regiment);
            const auto *target = FindUnit(world, event.target);
            if (attacker && defending && target && UnitTeam(world, *attacker) != defending->team) {
                auto &pressure = pressures[defending - world.regiments.data()];
                if (IsElephant(attacker->platform)) {
                    const float shock = attacker->platform == BattlePlatform::AfricanElephant ? .15f : .075f;
                    pressure.morale += shock;
                    pressure.cohesion += shock * .5f;
                } else if (attacker->troop == UnitTroop::Mounted ||
                           attacker->platform == BattlePlatform::ScythedChariot) {
                    // Impacts disturb the files they reach. A blow on the narrow side
                    // hits a larger share of the exposed line than the same frontal blow.
                    const size_t positions = defending->slots.empty() ? defending->units.size()
                                                                    : defending->slots.size();
                    const size_t width = std::min<size_t>(defending->formation.columns, positions);
                    const size_t depth = (positions + defending->formation.columns - 1) /
                                         defending->formation.columns;
                    const auto exposed = std::max<size_t>(1, event.aspect == AttackAspect::Flank
                                                               ? depth : width);
                    const auto &original = world.scenario.units[target - world.units.data()];
                    const float force = std::min(1.f, event.amount / original.health);
                    const float resistance = defending->braced && event.aspect == AttackAspect::Front
                                                 ? .25f : 1.f;
                    pressure.cavalry_disruption += 1.35f * force * resistance / float(exposed);
                }
            }
        }
        if (!event.projectile && event.kind == EventKind::Hit && event.amount > 0.f &&
            event.aspect != AttackAspect::Flank) {
            const auto *target = FindUnit(world, event.target);
            const auto *attacker = FindUnit(world, event.source);
            const auto *regiment = target ? FindRegiment(world, target->regiment) : nullptr;
            if (regiment && attacker && UnitTeam(world, *attacker) != regiment->team)
                pressures[regiment - world.regiments.data()].melee_aspects |=
                    event.aspect == AttackAspect::Front ? FrontAttack : RearAttack;
        }
        if (event.kind != EventKind::Died && (event.kind != EventKind::Hit || event.amount <= 0.f))
            continue;
        const auto *regiment = FindRegiment(world, event.regiment);
        if (!regiment || !regiment->initial_count)
            continue;
        auto &pressure = pressures[regiment - world.regiments.data()];
        const float share = 1.f / float(regiment->initial_count);
        if (const auto *attacker = FindUnit(world, event.source);
            attacker && UnitTeam(world, *attacker) != regiment->team) {
            if (event.kind == EventKind::Hit && event.projectile &&
                attacker->platform == BattlePlatform::FieldGun &&
                artillery_impacts.emplace(regiment->id, event.cause ? event.cause : event.id).second)
                ++pressure.artillery_shots;
            auto away = regiment->pose.position - attacker->pose.position;
            away.z = 0.f;
            const float length = Length(away);
            if (length > .01f)
                pressure.escape = pressure.escape + away * (1.f / length);
        }
        if (event.kind == EventKind::Died) {
            ++pressure.casualties;
            if (event.projectile)
                ++pressure.ranged_losses;
            else {
                ++pressure.melee_losses;
                if (const auto *attacker = FindUnit(world, event.source)) {
                    if (const auto *source = FindRegiment(world, attacker->regiment);
                        source && source->team != regiment->team)
                        ++pressures[source - world.regiments.data()].melee_kills;
                }
            }
            pressure.morale += share * 1.25f;
            pressure.cohesion += share * .85f;
            continue;
        }
        const auto *target = FindUnit(world, event.target);
        if (!target)
            continue;
        // The scenario and live rosters have the same sorted IDs, including fallen units.
        const auto &original = world.scenario.units[target - world.units.data()];
        const float force = std::min(1.f, event.amount / original.health) * share;
        if (event.projectile) {
            pressure.morale += force * .45f;
            pressure.cohesion += force * .25f;
        } else if (event.aspect == AttackAspect::Rear) {
            pressure.rear_hit = true;
            pressure.morale += force * 1.55f;
            pressure.cohesion += force * 1.2f;
        } else if (event.aspect == AttackAspect::Flank) {
            pressure.morale += force * .9f;
            pressure.cohesion += force * .95f;
        } else {
            pressure.morale += force * .42f;
            pressure.cohesion += force * .38f;
        }
    }
    for (const auto &event : world.events) {
        if (event.kind != EventKind::Died)
            continue;
        const auto officer = std::ranges::find(world.officers, event.target, &OfficerSpec::unit);
        if (officer == world.officers.end())
            continue;
        if (world.command_state) {
            for (const auto &node : world.command_state->hierarchy.nodes) {
                if (node.commander != officer->id)
                    continue;
                if (node.level == command::Level::Army) {
                    for (size_t i = 0; i < world.regiments.size(); ++i)
                        pressures[i].army_commander_lost |= world.regiments[i].team == node.team;
                } else if (node.level == command::Level::Regiment) {
                    if (const auto *regiment = FindRegiment(world, node.regiment))
                        pressures[regiment - world.regiments.data()].commander_lost = true;
                }
            }
        } else if (officer->role == 1 || officer->role == 2) {
            if (const auto *regiment = FindRegiment(world, event.regiment))
                pressures[regiment - world.regiments.data()].commander_lost = true;
        }
    }
}

void CollectRetreats(const World &world, std::vector<Pressure> &pressures) {
    for (const auto &retreating : world.regiments) {
        if (retreating.units.empty() ||
            !retreating.panic ||
            retreating.individual_routing ||
            Distance(retreating.pose.position, retreating.rally_point) <=
                world.scenario.rules.rally_tolerance_m)
            continue;
        auto footprint = Footprint(retreating);
        // Soldiers can brush past allies at the edge of a formation's reserved route.
        const float margin = retreating.formation.spacing_m * .65f;
        footprint.width_m += margin;
        footprint.depth_m += margin;
        for (size_t i = 0; i < world.regiments.size(); ++i) {
            const auto &ally = world.regiments[i];
            if (ally.id != retreating.id && ally.team == retreating.team && !ally.units.empty() &&
                RectanglesOverlap(retreating.pose, footprint, ally.pose, Footprint(ally)))
                pressures[i].retreat_size = std::max(pressures[i].retreat_size, retreating.units.size());
        }
    }
}

void Emit(World &world, const RegimentState &regiment, std::string reason,
          EventKind kind = EventKind::OrderChanged) {
    world.events.push_back({{world.next_event_id++},
                            world.tick + 1,
                            kind,
                            {},
                            {},
                            regiment.id,
                            0,
                            std::move(reason)});
    ++world.combat_events;
}

void ClearManeuver(RegimentState &regiment) {
    regiment.route.clear();
    regiment.waypoint = 0;
    regiment.retreat_waypoint.reset();
    regiment.decision_tick = 0;
    regiment.decision_until = 0;
    regiment.ai_target = {};
}

void LeaveBattlefield(World &world, UnitId id) {
    if (auto *unit = FindUnit(world, id)) {
        unit->health = 0.f;
        unit->alive = false;
        unit->control = ControlMode::Dead;
        unit->dagger_drawn = unit->skirmisher = unit->skirmisher_returning = unit->bypassing = false;
        unit->contact_travel = {};
        unit->rout_waypoint = 0;
        if (unit->jump_started_tick || unit->parry_ready_tick)
            ClearPersonalActions(*unit);
    }
    if (world.controlled == id) {
        ClearRegimentFollowing(world, id);
        world.controlled = {};
    }
}

void RemoveRoutedRegiment(World &world, RegimentState &regiment, const char *reason) {
    ClearRegimentFollowing(world, regiment.following_officer);
    for (const auto id : regiment.units)
        LeaveBattlefield(world, id);
    regiment.units.clear();
    regiment.slots.clear();
    regiment.order.reset();
    regiment.pursuit_target = {};
    regiment.individual_routing = regiment.rout_to_rally = false;
    regiment.rout_progress_tick = 0;
    ClearManeuver(regiment);
    command::WakeCommandForPanicChange(world, regiment.id);
    Emit(world, regiment, reason);
}

bool ReachedFieldEdge(const World &world, const RegimentState &regiment) {
    if (!world.scenario.field_bounds)
        return false;
    const auto shape = Footprint(regiment);
    const auto limit = TerrainFieldInset(*world.scenario.field_bounds, regiment.pose.yaw,
                                        shape.depth_m * .5f, shape.width_m * .5f);
    constexpr float Tolerance = .01f;
    return std::abs(regiment.pose.position.x) >= limit.x - Tolerance ||
           std::abs(regiment.pose.position.y) >= limit.y - Tolerance;
}

bool RallyAreaClear(const World &world, const RegimentState &regiment) {
    const auto &rules = world.scenario.rules;
    if (regiment.cannot_rally || !regiment.order || regiment.order->type != OrderType::Rally ||
        regiment.order->status != OrderStatus::Active ||
        Distance(regiment.pose.position, regiment.rally_point) > rules.rally_tolerance_m)
        return false;
    if (regiment.individual_routing &&
        (!regiment.rout_to_rally || regiment.route.empty() ||
         std::ranges::any_of(regiment.units, [&](UnitId id) {
             const auto &unit = *FindUnit(world, id);
             return unit.control != ControlMode::PlayerControlled &&
                 (unit.rout_waypoint < regiment.route.size() ||
                  Distance(unit.pose.position, unit.slot.position) > RallySlotArrivalM);
         })))
        return false;
    if (std::any_of(world.regiments.begin(), world.regiments.end(), [&](const auto &enemy) {
            return enemy.pursuit_target == regiment.id && !enemy.units.empty() && !enemy.panic;
        }))
        return false;
    return std::none_of(world.units.begin(), world.units.end(), [&](const auto &unit) {
        return unit.alive && UnitTeam(world, unit) != regiment.team &&
               Distance(unit.pose.position, regiment.pose.position) < rules.rally_safe_radius_m;
    });
}

void UpdateRegiment(World &world, RegimentState &regiment, const Pressure &pressure) {
    const auto staff = GetStaffEffects(world, regiment);
    const auto count = regiment.units.size();
    const float steadiness = 1.f - .15f * staff.leadership - .2f * staff.standard;
    const bool retreat_through = pressure.retreat_size >= count;
    const float disruption = retreat_through ? .35f * TickSeconds : 0.f;
    const float cavalry_disruption = std::min(.55f, pressure.cavalry_disruption);
    regiment.morale = std::max(0.f, regiment.morale -
        (pressure.morale + disruption * .4f + cavalry_disruption * .55f) * steadiness);
    const float shell_shock = std::min(4u, pressure.artillery_shots) * .06f;
    regiment.cohesion = std::max(0.f, regiment.cohesion - pressure.cohesion -
                                     disruption - shell_shock - cavalry_disruption);
    regiment.artillery_pressure = std::min(1.f, std::max(0.f, regiment.artillery_pressure - .25f * TickSeconds) +
                                               std::min(4u, pressure.artillery_shots) * .24f);
    const bool fresh_attrition =
        pressure.casualties &&
        double(count) < double(regiment.initial_count) * double(staff.panic_fraction);
    if (!regiment.discipline_window_until) {
        regiment.discipline_window_until = world.tick + DisciplineWindowTicks;
        regiment.discipline_window_count = count + pressure.casualties;
    }
    regiment.ranged_losses += pressure.ranged_losses;
    regiment.melee_losses += pressure.melee_losses;
    regiment.melee_kills += pressure.melee_kills;
    regiment.melee_attack_aspects |= pressure.melee_aspects;
    const bool encircled = regiment.melee_attack_aspects == (FrontAttack | RearAttack) &&
                            (pressure.melee_aspects || regiment.melee_engaged);
    const float resolve = std::clamp(.65f + .15f * (regiment.morale - 1.f) +
        .15f * staff.leadership + .05f * staff.standard, 0.f, .95f);
    bool fails = false;
    std::string_view cause;
    const auto check = [&](bool incident, float penalty, uint64_t salt, std::string_view reason) {
        if (incident && !DisciplineTest(world, regiment, resolve - penalty, salt)) {
            fails = true;
            cause = reason;
            return true;
        }
        return false;
    };
    if (uint64_t(regiment.ranged_losses) * 2 > regiment.discipline_window_count) {
        fails = true;
        cause = "panic: lost over half the regiment to shooting";
    } else if (!regiment.ranged_checked &&
               uint64_t(regiment.ranged_losses) * 3 > regiment.discipline_window_count) {
        regiment.ranged_checked = true;
        check(true, 0.f, 0x52414e47ull, "panic: sustained shooting losses");
    }
    if (pressure.rear_hit && !regiment.rear_checked) {
        regiment.rear_checked = true;
        check(true, .15f, 0x52454152ull, "panic: attacked from the rear");
    }
    check(pressure.commander_lost, .05f, 0x434f4d4dull, "panic: lost the regiment commander");
    if (check(pressure.army_commander_lost, .10f, 0x41524d59ull,
              "panic: lost the army commander"))
        regiment.cannot_rally = true;
    if (retreat_through && !regiment.retreat_overlap) {
        if (pressure.retreat_size > count) {
            fails = true;
            cause = "panic: swept along by a larger fleeing regiment";
        } else {
            check(true, 0.f, 0x414c4c59ull, "panic: swept along by an equal fleeing regiment");
        }
    }
    regiment.retreat_overlap = retreat_through;
    if (world.tick + 1 >= regiment.discipline_window_until) {
        const auto loss = int64_t(regiment.melee_losses) - int64_t(regiment.melee_kills);
        const float share = float(std::max<int64_t>(0, loss)) /
                            float(std::max<uint64_t>(1, regiment.discipline_window_count));
        const float resolve_margin = std::min(regiment.morale, regiment.cohesion) - PanicThreshold;
        // The window decides a *lost exchange*, not each individual death. The
        // surviving formation may already have less resolve after a severe shock.
        if (share > 0.f && share * 5.f > resolve_margin)
            check(true, std::min(.60f, share * 1.5f), 0x4d454c45ull,
                  regiment.melee_kills ? "panic: lost the melee exchange" :
                                         "panic: could not break the enemy defense");
        regiment.discipline_window_count = count;
        regiment.ranged_losses = regiment.melee_losses = regiment.melee_kills = 0;
        regiment.ranged_checked = regiment.rear_checked = false;
        regiment.melee_attack_aspects = 0;
        regiment.discipline_window_until = world.tick + 1 + DisciplineWindowTicks;
    }
    if (!regiment.panic && (fails || regiment.morale <= PanicThreshold || fresh_attrition)) {
        regiment.panic = true;
        if (encircled) {
            RemoveRoutedRegiment(world, regiment, "rout: surrounded under front and rear attack");
            return;
        }
        const float escape_length = Length(pressure.escape);
        regiment.rout_direction = escape_length > .1f ? pressure.escape * (1.f / escape_length) :
                                                        Vec3{};
        command::WakeCommandForPanicChange(world, regiment.id);
        regiment.calm_counter = 0;
        regiment.cohesion = std::min(regiment.cohesion, .38f);
        ClearManeuver(regiment);
        if (regiment.order && (IsAttacking(regiment.order->type) ||
                               regiment.order->type == OrderType::MoveTo))
            regiment.order->status = OrderStatus::Cancelled;
        Emit(world, regiment, cause.empty() ? "panic: withdraw to rally point" : std::string(cause));
    }

    const bool undisturbed = !retreat_through && !regiment.melee_engaged &&
                             regiment.last_damage_tick <= world.tick;
    if (regiment.panic) {
        if (regiment.individual_routing && !regiment.rout_to_rally &&
            world.scenario.field_bounds && !regiment.route.empty()) {
            std::erase_if(regiment.units, [&](UnitId id) {
                const auto &unit = *FindUnit(world, id);
                if (unit.control == ControlMode::PlayerControlled || unit.rout_waypoint < regiment.route.size())
                    return false;
                LeaveBattlefield(world, id);
                return true;
            });
        }
        if (regiment.units.empty() || (!regiment.individual_routing && ReachedFieldEdge(world, regiment))) {
            RemoveRoutedRegiment(world, regiment, "rout: fled off the battlefield");
            return;
        }
        regiment.panic_loss_count = count;
        const bool rally_clear = RallyAreaClear(world, regiment);
        if (undisturbed && rally_clear) {
            if (regiment.calm_counter < std::numeric_limits<Tick>::max())
                ++regiment.calm_counter;
        } else {
            regiment.calm_counter = 0;
        }
        const auto leadership_delay =
            Tick(double(staff.calm_ticks) * .5 * (1.f - staff.leadership));
        if (rally_clear && regiment.calm_counter >= staff.calm_ticks &&
            regiment.calm_counter - staff.calm_ticks >= leadership_delay &&
            ((world.officers.empty() && !world.command_state) ||
             DisciplineTest(world, regiment, staff.leadership, 0x52414c4cull))) {
            regiment.panic = false;
            command::WakeCommandForPanicChange(world, regiment.id);
            regiment.calm_counter = 0;
            regiment.morale = std::max(regiment.morale, .62f);
            regiment.cohesion = std::max(regiment.cohesion, .48f);
            regiment.ai_action = OrderType::Hold;
            regiment.rout_direction = {};
            if (regiment.individual_routing)
                regiment.pose = regiment.route.back();
            regiment.individual_routing = regiment.rout_to_rally = false;
            regiment.rout_progress_tick = 0;
            for (const auto id : regiment.units)
                FindUnit(world, id)->rout_waypoint = 0;
            ClearManeuver(regiment);
            regiment.order->status = OrderStatus::Completed;
            Emit(world, regiment, "rallied: hold and reform ranks");
        } else if (regiment.calm_counter >= staff.calm_ticks &&
                   regiment.calm_counter - staff.calm_ticks >= leadership_delay) {
            regiment.calm_counter = 0;
        }
    } else if (undisturbed) {
        regiment.morale =
            std::min(1.f, regiment.morale + TickSeconds * (.08f + .05f * staff.leadership +
                                                           .04f * staff.standard));
        regiment.cohesion =
            std::min(1.f, regiment.cohesion +
                              TickSeconds * (.11f + .04f * staff.leadership + .1f * staff.music));
    }

    if (!regiment.disordered && regiment.cohesion < DisorderThreshold) {
        regiment.disordered = true;
        Emit(world, regiment,
             retreat_through ? "formation disordered: allied retreat"
             : pressure.artillery_shots ? "formation disordered: artillery fire"
             : cavalry_disruption > 0.f ? "formation disordered: cavalry impact"
                                        : "formation disordered: combat shock");
    } else if (regiment.disordered && !regiment.panic && regiment.cohesion >= ReformThreshold) {
        regiment.disordered = false;
        Emit(world, regiment, "formation reformed", EventKind::FormationReformed);
    }
}
} // namespace

void UpdateRegimentDiscipline(World &world) {
    std::vector<Pressure> pressures(world.regiments.size());
    CollectImpact(world, pressures);
    CollectRetreats(world, pressures);
    bool regiment_left = false;
    for (size_t i = 0; i < world.regiments.size(); ++i) {
        auto &regiment = world.regiments[i];
        if (!regiment.units.empty()) {
            UpdateRegiment(world, regiment, pressures[i]);
            regiment_left |= regiment.units.empty();
        }
    }
    if (!regiment_left || world.regiments.empty() || std::ranges::all_of(world.regiments, [&](const auto &r) {
            return r.team == world.regiments.front().team;
        }))
        return;
    const auto survivor = std::ranges::find_if(world.regiments, [](const auto &r) { return !r.units.empty(); });
    if (survivor == world.regiments.end())
        world.outcome = BattleOutcome::Draw;
    else if (std::ranges::none_of(world.regiments, [&](const auto &r) {
                 return !r.units.empty() && r.team != survivor->team;
             })) {
        world.outcome = BattleOutcome::Victory;
        world.winner_team = survivor->team;
    }
}
} // namespace battle
