#include "battle/Formation.h"
#include "LocalMelee.h"
#include "battle/Officers.h"
#include <array>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>
namespace battle {
namespace {
float Angle(float x) {
    return std::remainder(x, 2 * Pi);
}
Vec3 Towards(Vec3 a, Vec3 b, float distance) {
    auto d = b - a;
    float n = Length(d);
    return n <= distance ? b : a + d * (distance / n);
}
float OrderedPaceMultiplier(MovementPace pace) {
    switch (pace) {
    case MovementPace::March: return 1.15f;
    case MovementPace::Run: return 1.45f;
    default: return 1.f;
    }
}
float PaceMultiplier(const RegimentState &regiment) {
    if (regiment.panic)
        return 1.f;
    if (regiment.ai_action == OrderType::FallBack)
        return .5f; // An organized retreat preserves the front at half movement.
    if (regiment.cavalry_phase == CavalryPhase::Approach || regiment.pursuit_target)
        return 1.45f;
    if (!regiment.order || regiment.order->status != OrderStatus::Active)
        return 1.f;
    return OrderedPaceMultiplier(regiment.order->pace);
}
float RankRefillSpeed(const UnitState &unit, float pace, float fatigue_factor, bool disordered,
                      float pressure_factor) {
    const float paced_speed = unit.speed * pace;
    return (IsSiegeEngine(unit.platform) ? std::min(SiegeTravelLimit(unit.platform), paced_speed)
                                        : paced_speed) *
           fatigue_factor * (disordered ? .82f : 1.f) * pressure_factor;
}

float WheelGroundFactor(const World &world, const RegimentState &regiment, float turn, float dt,
                        float fatigue_factor, bool disordered, float pressure_factor) {
    if (turn == 0.f || (!world.scenario.hill && world.scenario.obstacles.empty()))
        return 1.f;
    const auto axis = Forward(turn);
    const float pace = PaceMultiplier(regiment);
    float factor = 1.f;
    for (const auto id : regiment.units) {
        const auto &unit = *FindUnit(world, id);
        if (unit.control == ControlMode::PlayerControlled || unit.skirmisher ||
            unit.skirmisher_returning || unit.slot_index >= regiment.slots.size())
            continue;
        const auto start = regiment.slots[unit.slot_index].position;
        const auto offset = start - regiment.pose.position;
        const auto end = regiment.pose.position + Vec3{
            offset.x * axis.x - offset.y * axis.y, offset.x * axis.y + offset.y * axis.x, offset.z};
        const float travel = Distance(start, end);
        if (travel < .00001f)
            continue;
        const float speed = RankRefillSpeed(unit, pace, fatigue_factor, disordered, pressure_factor);
        const float hill = world.scenario.hill
            ? HillUphillSpeedMultiplier(*world.scenario.hill, start, end, unit.troop, speed) : 1.f;
        // A locally blocked rider still obeys its own terrain check. Do not stop
        // the legal wheel of the entire formation because one rider cannot follow.
        if (hill == 0.f)
            continue;
        const float terrain = TerrainSpeedMultiplier(start, end, world.scenario.obstacles, unit.troop, speed);
        // Do not spend more of this turn than the slowest file can cover on its terrain.
        factor = std::min(factor, speed * dt * hill * terrain / travel);
        if (factor <= 0.f)
            return 0.f;
    }
    return factor;
}

bool CanShareTranslation(const World &world, const RegimentState &regiment) {
    return !regiment.units.empty() && !regiment.panic && !regiment.melee_engaged &&
           !regiment.following_officer && !IsDeploying(world, regiment);
}
void SetFormationSlots(std::vector<Pose> &slots, const FormationSpec &formation, Pose centre,
                       uint32_t count, bool routing = false) {
    if (!formation.columns || !count) {
        slots.clear();
        return;
    }
    slots.resize(count);
    const uint32_t rows = (count + formation.columns - 1) / formation.columns;
    const auto forward = Forward(centre.yaw);
    const auto right = Right(centre.yaw);
    const float rout_gap = routing ? std::max(0.f, formation.spacing_m - 2.f * formation.body_radius_m) * .42f : 0.f;
    const float half_depth = float(rows - 1) * .5f * formation.spacing_m;
    const float half_width = float(formation.columns - 1) * .5f * formation.spacing_m;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t rank = i / formation.columns;
        float x = (float(rows - 1) * .5f - float(rank)) * formation.spacing_m;
        float y = (float(i % formation.columns) - float(formation.columns - 1) * .5f) * formation.spacing_m;
        if (rout_gap > 0.f) {
            // Break the standing alignment within the existing swept footprint and body clearance.
            x = std::clamp(x + ((i + rank) % 2 ? rout_gap : -rout_gap), -half_depth, half_depth);
            y = std::clamp(y + (float((i + rank) % 3) - 1.f) * rout_gap, -half_width, half_width);
        }
        slots[i] = {centre.position + forward * x + right * y, centre.yaw};
    }
}

bool HasNegativeZero(Vec3 position) {
    return (position.x == 0.f && std::signbit(position.x)) ||
           (position.y == 0.f && std::signbit(position.y)) ||
           (position.z == 0.f && std::signbit(position.z));
}

bool IsArtilleryBattery(const World &world, const RegimentState &regiment) {
    return std::ranges::any_of(regiment.units, [&](UnitId id) {
        const auto *unit = FindUnit(world, id);
        return unit && IsSiegeEngine(unit->platform);
    });
}
} // namespace
void OpenSkirmishSpacing(FormationSpec &formation) {
    formation.spacing_m = std::max({formation.spacing_m, 1.8f, 4.f * formation.body_radius_m});
}
std::vector<Pose> BuildFormationSlots(const FormationSpec &formation, Pose centre, uint32_t count) {
    std::vector<Pose> slots;
    SetFormationSlots(slots, formation, centre, count);
    return slots;
}
FormationFootprint Footprint(const RegimentState &r) {
    size_t count = r.slots.empty() ? r.units.size() : r.slots.size();
    if (!count)
        return {};
    size_t rows = (count + r.formation.columns - 1) / r.formation.columns;
    return {(r.formation.columns - 1) * r.formation.spacing_m + 2 * r.formation.body_radius_m,
            float(rows - 1) * r.formation.spacing_m + 2 * r.formation.body_radius_m};
}
bool UsesLinearInfantry(const World &w, const RegimentState &r) {
    return w.scenario.rules.regiment_tactics && w.scenario.rules.linear_infantry &&
           !r.units.empty() && std::ranges::all_of(r.units, [&](UnitId id) {
               const auto &unit = *FindUnit(w, id);
               return unit.troop != UnitTroop::Mounted && unit.platform == BattlePlatform::None;
           });
}
FormationSpec LinearInfantryFormation(FormationSpec spec, uint32_t count, bool firing_line) {
    // A large formation represents several ranks of a battalion rather than a map-wide line.
    const uint32_t line = std::min(64u, count / 2 + count % 2);
    const uint32_t column = std::min(24u, count / 4);
    spec.columns = std::max(1u, firing_line ? line : column);
    return spec;
}
FormationSpec LinearInfantryFormation(const RegimentState &r, bool firing_line) {
    return LinearInfantryFormation(r.formation, uint32_t(r.units.size()), firing_line);
}
bool IsLinearFiringLine(const RegimentState &r) {
    const auto layout_size = uint32_t(r.slots.empty() ? r.units.size() : r.slots.size());
    return r.formation.columns >= LinearInfantryFormation(r.formation, layout_size, true).columns;
}
namespace {
void PrioritizeMusketRanks(const World &w, const RegimentState &r,
                           std::vector<UnitState *> &rank_order) {
    if (!IsDeploying(w, r) && UsesLinearInfantry(w, r) && IsLinearFiringLine(r))
        std::stable_partition(rank_order.begin(), rank_order.end(), [](const auto *unit) {
            return unit->ranged_weapon == RangedWeapon::Musket &&
                   (!unit->ammunition || unit->ammunition_remaining) &&
                   !unit->skirmisher && !unit->skirmisher_returning &&
                   unit->control != ControlMode::PlayerControlled;
        });
}

std::vector<UnitState *> RankMembers(World &w, const RegimentState &r) {
    std::vector<UnitState *> members;
    members.reserve(r.units.size());
    for (const auto id : r.units)
        members.push_back(FindUnit(w, id));
    return members;
}
} // namespace
bool RectanglesOverlap(Pose a, FormationFootprint af, Pose b, FormationFootprint bf) {
    if (af.width_m <= 0 || af.depth_m <= 0 || bf.width_m <= 0 || bf.depth_m <= 0)
        return false;
    auto ax = Forward(a.yaw), ay = Right(a.yaw), bx = Forward(b.yaw), by = Right(b.yaw);
    for (auto axis : {ax, ay, bx, by}) {
        float ra =
            std::abs(Dot(axis, ax)) * af.depth_m * .5f + std::abs(Dot(axis, ay)) * af.width_m * .5f;
        float rb =
            std::abs(Dot(axis, bx)) * bf.depth_m * .5f + std::abs(Dot(axis, by)) * bf.width_m * .5f;
        if (std::abs(Dot(b.position - a.position, axis)) >= ra + rb - 0.00001f)
            return false;
    }
    return true;
}
float SweepLimit(Pose a, Pose end, FormationFootprint af, Pose b, FormationFootprint bf) {
    if (af.width_m <= 0 || af.depth_m <= 0 || bf.width_m <= 0 || bf.depth_m <= 0)
        return 1;
    auto ax = Forward(a.yaw), ay = Right(a.yaw), bx = Forward(b.yaw), by = Right(b.yaw);
    bool rotating = std::abs(Angle(end.yaw - a.yaw)) > 0.00001f;
    float enter = -1e30f, leave = 1e30f;
    for (auto axis : {ax, ay, bx, by}) {
        float ra = rotating ? std::hypot(af.width_m, af.depth_m) * .5f
                            : std::abs(Dot(axis, ax)) * af.depth_m * .5f +
                                  std::abs(Dot(axis, ay)) * af.width_m * .5f;
        float rb =
            std::abs(Dot(axis, bx)) * bf.depth_m * .5f + std::abs(Dot(axis, by)) * bf.width_m * .5f;
        float radius = ra + rb;
        float d = Dot(b.position - a.position, axis), v = Dot(end.position - a.position, axis);
        if (std::abs(v) < 1e-9f) {
            if (std::abs(d) >= radius - .00001f)
                return 1;
            continue;
        }
        float x = (d - radius) / v, y = (d + radius) / v;
        if (x > y)
            std::swap(x, y);
        enter = std::max(enter, x);
        leave = std::min(leave, y);
        if (enter >= leave)
            return 1;
    }
    if (leave <= .00001f || enter >= 1)
        return 1;
    return std::clamp(enter - .00001f, 0.f, 1.f);
}
namespace {
std::vector<const RegimentState *> TranslationMembers(const World &w, std::span<const RegimentId> members) {
    if (members.size() < 2)
        return {};
    const auto *first = FindRegiment(w, members.front());
    if (!first)
        return {};
    std::vector<const RegimentState *> moving;
    for (auto id : members) {
        const auto *regiment = FindRegiment(w, id);
        if (!regiment || regiment->team != first->team || !CanShareTranslation(w, *regiment) ||
            std::ranges::find(moving, regiment) != moving.end())
            return {};
        moving.push_back(regiment);
    }
    return moving;
}
bool GroupSegmentClear(const World &w, const std::vector<const RegimentState *> &moving,
                       Vec3 offset, Vec3 delta) {
    for (const auto *regiment : moving) {
        const Pose start{regiment->pose.position + offset, regiment->pose.yaw};
        const Pose end{start.position + delta, start.yaw};
        const auto size = Footprint(*regiment);
        if (w.scenario.hill && !HillSegmentPassable(*w.scenario.hill, start.position, end.position,
                FindUnit(w, regiment->units.front())->troop))
            return false;
        if (const auto &bounds = w.scenario.field_bounds) {
            const auto inset = TerrainFieldInset(*bounds, start.yaw, size.depth_m * .5f,
                                                 size.width_m * .5f);
            if (std::abs(end.position.x) > inset.x || std::abs(end.position.y) > inset.y)
                return false;
        }
        for (const auto &other : w.regiments) {
            if (other.id == regiment->id || other.units.empty())
                continue;
            const auto footprint = Footprint(other);
            if (std::ranges::find(moving, &other) != moving.end()) {
                if (RectanglesOverlap(regiment->pose, size, other.pose, footprint))
                    return false;
            } else if (SweepLimit(start, end, size, other.pose, footprint) < .99999f ||
                       RectanglesOverlap(end, size, other.pose, footprint))
                return false;
        }
        for (const auto &area : w.scenario.obstacles) {
            const Pose obstacle{{(area.min_x + area.max_x) * .5f,
                                 (area.min_y + area.max_y) * .5f, start.position.z}, 0.f};
            const FormationFootprint footprint{area.max_y - area.min_y, area.max_x - area.min_x};
            if (SweepLimit(start, end, size, obstacle, footprint) >= .99999f &&
                !RectanglesOverlap(end, size, obstacle, footprint))
                continue;
            if (TerrainBlocksMovement(area))
                return false;
            if (TerrainIsCrossing(area) &&
                std::ranges::any_of(w.regiments, [&](const auto &other) {
                    if (other.id == regiment->id || other.units.empty())
                        return false;
                    const auto footprint_other = Footprint(other);
                    const bool group_member =
                        std::ranges::find(moving, &other) != moving.end();
                    const Pose other_start{other.pose.position + (group_member ? offset : Vec3{}),
                                          other.pose.yaw};
                    const Pose other_end{other_start.position + (group_member ? delta : Vec3{}),
                                         other.pose.yaw};
                    return SweepLimit(other_start, other_end, footprint_other,
                                      obstacle, footprint) < .99999f ||
                        RectanglesOverlap(other_end, footprint_other, obstacle, footprint);
                }))
                return false;
        }
    }
    return true;
}
} // namespace
bool CanTranslateFormationGroup(const World &w, std::span<const RegimentId> members, Vec3 delta) {
    const auto moving = TranslationMembers(w, members);
    return !moving.empty() && Finite(delta) && GroupSegmentClear(w, moving, {}, delta);
}
std::optional<FormationRouteConflict> FirstFormationRouteConflict(
    std::span<const TimedFormationPose> a, std::span<const TimedFormationPose> b) {
    const auto valid = [](std::span<const TimedFormationPose> path) {
        if (path.empty() || path.front().seconds != 0.f)
            return false;
        float previous = -1.f;
        for (const auto& p : path) {
            if (!std::isfinite(p.seconds) || p.seconds <= previous || !Finite(p.pose) ||
                !std::isfinite(p.footprint.width_m) || !std::isfinite(p.footprint.depth_m) ||
                p.footprint.width_m <= 0.f || p.footprint.depth_m <= 0.f)
                return false;
            previous = p.seconds;
        }
        return true;
    };
    if (!valid(a) || !valid(b))
        return {};
    const auto sample = [](std::span<const TimedFormationPose> route, size_t i, float t) {
        const auto &p = route[i];
        if (i + 1 == route.size())
            return p.pose;
        const auto &q = route[i + 1];
        const float fraction = std::clamp((t - p.seconds) / (q.seconds - p.seconds), 0.f, 1.f);
        return Pose{p.pose.position + (q.pose.position - p.pose.position) * fraction,
                    p.pose.yaw + Angle(q.pose.yaw - p.pose.yaw) * fraction};
    };
    const auto interval_size = [](FormationFootprint size, Pose from, Pose to) {
        // Enclose a short turning arc at its midpoint orientation.
        const float padding = std::hypot(size.depth_m, size.width_m) *
                              std::abs(Angle(to.yaw - from.yaw)) * .5f;
        return FormationFootprint{size.width_m + padding, size.depth_m + padding};
    };
    size_t i = 0, j = 0;
    float time = 0.f;
    while (i + 1 < a.size() || j + 1 < b.size()) {
        const float next_a = i + 1 < a.size() ? a[i + 1].seconds : std::numeric_limits<float>::infinity();
        const float next_b = j + 1 < b.size() ? b[j + 1].seconds : std::numeric_limits<float>::infinity();
        const float next = std::min(next_a, next_b);
        if (next > time) {
            const Pose a0 = sample(a, i, time), a1 = sample(a, i, next);
            const Pose b0 = sample(b, j, time), b1 = sample(b, j, next);
            const Pose start{a0.position, a0.yaw + Angle(a1.yaw - a0.yaw) * .5f};
            const Pose obstacle{b0.position, b0.yaw + Angle(b1.yaw - b0.yaw) * .5f};
            const Pose end{a1.position - (b1.position - b0.position), start.yaw};
            const auto af = interval_size(a[i].footprint, a0, a1);
            const auto bf = interval_size(b[j].footprint, b0, b1);
            const auto fraction = SweepLimit(start, end, af, obstacle, bf);
            if (fraction < .99999f || RectanglesOverlap(end, af, obstacle, bf)) {
                const auto centre0 = (a0.position + b0.position) * .5f;
                const auto centre1 = (a1.position + b1.position) * .5f;
                return FormationRouteConflict{time + (next - time) * fraction,
                    centre0 + (centre1 - centre0) * fraction};
            }
        }
        i += next == next_a;
        j += next == next_b;
        time = next;
    }
    if (RectanglesOverlap(a.back().pose, a.back().footprint, b.back().pose, b.back().footprint))
        return FormationRouteConflict{time, (a.back().pose.position + b.back().pose.position) * .5f};
    return {};
}
PathResult FindFormationGroupRoute(const NavigationService &nav, const World &w,
                                   std::span<const RegimentId> members, Vec3 delta) {
    const auto moving = TranslationMembers(w, members);
    if (moving.empty() || !Finite(delta))
        return {Status::NoPath, {}};
    const auto &lead = *moving.front();
    const auto clear = [&](Pose from, Pose to) {
        return GroupSegmentClear(w, moving, from.position - lead.pose.position,
                                 to.position - from.position);
    };
    auto result = nav.FindPath({lead.pose, lead.pose.position + delta, Footprint(lead),
                                lead.pose.yaw, {}, true, true, clear});
    if (result.status != Status::Ok || result.waypoints.empty())
        return {Status::NoPath, {}};
    std::vector<Pose> path;
    auto previous = lead.pose;
    for (const auto &point : result.waypoints) {
        if (!Finite(point) || !clear(previous, point))
            return {Status::NoPath, {}};
        if (Distance(previous.position, point.position) > .00001f)
            path.push_back({point.position, lead.pose.yaw});
        previous = point;
    }
    if (Distance(previous.position, lead.pose.position + delta) > .001f)
        return {Status::NoPath, {}};
    if (path.empty())
        path.push_back(previous);
    return {Status::Ok, std::move(path)};
}

std::vector<FormationTranslation> ActiveFormationTranslations(const World &w) {
    std::map<std::tuple<int32_t, OfficerId, uint64_t, Tick>, FormationTranslation> by_gesture;
    for (const auto &r : w.regiments)
        if (r.order && r.order->type == OrderType::MoveTo && r.order->maneuver_group)
            by_gesture[{r.team, r.order->issuer, r.order->maneuver_group,
                        r.order->delivery_tick}].members.push_back(r.id);
    std::vector<FormationTranslation> result;
    for (auto &[key, plan] : by_gesture) {
        (void)key;
        const auto &lead = *FindRegiment(w, plan.members.front());
        plan.delta = lead.order->point - lead.pose.position;
        if (plan.members.size() < 2 || Length(plan.delta) <= .001f ||
            !std::ranges::all_of(plan.members, [&](RegimentId id) {
                const auto &r = *FindRegiment(w, id);
                return CanShareTranslation(w, r) && r.order->status == OrderStatus::Active &&
                    (!r.order->expiry_tick || w.tick < r.order->expiry_tick) &&
                    Distance(r.order->point - r.pose.position, plan.delta) <= .001f;
            }))
            continue;
        result.push_back(std::move(plan));
    }
    return result;
}
void InitializeFormations(World &w) {
    for (auto &r : w.regiments) {
        const bool battery = IsArtilleryBattery(w, r);
        if (battery) {
            r.formation.columns = uint32_t(r.units.size());
            r.deployment_ticks = 0;
        } else if (IsDeploying(w, r)) {
            r.formation = TravelColumnFormation(r.formation);
            if (!r.units.empty())
                r.formation.columns = std::min(r.formation.columns, uint32_t(r.units.size()));
        } else if (r.loose_skirmish) {
            OpenSkirmishSpacing(r.formation);
        }
        r.slots = BuildFormationSlots(r.formation, r.pose, uint32_t(r.units.size()));
        auto ranks = RankMembers(w, r);
        PrioritizeMusketRanks(w, r, ranks);
        for (size_t i = 0; i < ranks.size(); ++i) {
            auto *u = ranks[i];
            u->slot_index = uint32_t(i);
            u->slot = r.slots[i];
            if (battery || u->pose == r.pose)
                u->pose = u->slot;
        }
    }
}
bool IsDeploying(const World &w, const RegimentState &r) {
    return w.scenario.rules.regiment_tactics && w.tick < r.deployment_ticks;
}

void CompleteDeployments(World &w) {
    if (!w.scenario.rules.regiment_tactics)
        return;
    for (auto &r : w.regiments) {
        if (!r.deployment_ticks || IsDeploying(w, r))
            continue;
        r.deployment_ticks = 0;
        if (r.units.empty())
            continue;
        const auto authored =
            std::ranges::lower_bound(w.scenario.regiments, r.id, {}, &RegimentSpec::id);
        if (authored == w.scenario.regiments.end() || authored->id != r.id)
            continue;
        auto formation = authored->formation;
        formation.columns = std::min(formation.columns, uint32_t(r.units.size()));
        ReformRegiment(w, r.id, formation);
        r.decision_tick = 0;
    }
}

Status ReformRegiment(World &w, RegimentId id, FormationSpec s) {
    if (!s.columns || !std::isfinite(s.spacing_m) || s.spacing_m <= 0 ||
        !std::isfinite(s.body_radius_m) || s.body_radius_m <= 0)
        return Status::InvalidInput;
    auto *r = FindRegiment(w, id);
    if (!r)
        return Status::NotFound;
    const bool battery = IsArtilleryBattery(w, *r);
    if (battery) {
        // A battery uses one rank even when the order was generated for infantry.
        // Keep each gun's clearance from the authored battery layout.
        s.columns = std::max(1u, uint32_t(std::ranges::count_if(r->units, [&](UnitId member) {
            const auto *unit = FindUnit(w, member);
            return unit && unit->alive;
        })));
        s.spacing_m = r->formation.spacing_m;
        s.body_radius_m = r->formation.body_radius_m;
    }
    if (r->loose_skirmish && !IsDeploying(w, *r))
        OpenSkirmishSpacing(s);
    if (IsDeploying(w, *r) && (s.columns > r->formation.columns ||
                              s.spacing_m != r->formation.spacing_m ||
                              s.body_radius_m != r->formation.body_radius_m))
        return Status::Conflict;
    const bool changed_layout = r->formation != s || r->slots.size() != r->units.size();
    if (changed_layout)
        r->march_lane.reset();
    const bool contact_just_ended = r->separate_since && r->separate_since == w.tick;
    r->formation = s;
    std::erase_if(r->units, [&](auto id) {
        auto *u = FindUnit(w, id);
        return !u || !u->alive;
    });
    r->slots = BuildFormationSlots(s, r->pose, uint32_t(r->units.size()));
    auto rank_order = RankMembers(w, *r);
    if ((changed_layout || contact_just_ended) && w.scenario.rules.regiment_tactics &&
        w.scenario.rules.local_melee) {
        const auto forward = Forward(r->pose.yaw);
        const auto right = Right(r->pose.yaw);
        const auto rank_position = [](const UnitState *unit) {
            return unit->control == ControlMode::PlayerControlled ||
                   unit->skirmisher || unit->skirmisher_returning ?
                       unit->slot.position : unit->pose.position;
        };
        // Rear-rank replacements may already be on the front when contact breaks.
        // Keep the physical order of each new rank so neighbors can refill without crossing.
        std::stable_sort(rank_order.begin(), rank_order.end(), [&](const auto *a, const auto *b) {
            return Dot(rank_position(a), forward) > Dot(rank_position(b), forward);
        });
        for (size_t i = 0; i < rank_order.size(); i += s.columns) {
            const auto last = std::min(rank_order.size(), i + s.columns);
            std::stable_sort(rank_order.begin() + i, rank_order.begin() + last,
                             [&](const auto *a, const auto *b) {
                return Dot(rank_position(a), right) < Dot(rank_position(b), right);
            });
        }
    }
    PrioritizeMusketRanks(w, *r, rank_order);
    for (size_t i = 0; i < rank_order.size(); ++i) {
        auto *u = rank_order[i];
        u->slot_index = uint32_t(i);
        u->slot = r->slots[i];
    }
    return Status::Ok;
}
float UnitTerrainLimit(const World &w, Vec3 start, Vec3 destination, float radius) {
    const float left = std::min(start.x, destination.x) - radius;
    const float right = std::max(start.x, destination.x) + radius;
    const float bottom = std::min(start.y, destination.y) - radius;
    const float top = std::max(start.y, destination.y) + radius;
    float fraction = 1;
    for (const auto &box : w.scenario.obstacles) {
        if (right <= box.min_x || left >= box.max_x || top <= box.min_y || bottom >= box.max_y ||
            !TerrainBlocksMovement(box))
            continue;
        const Pose obstacle{{(box.min_x + box.max_x) * .5f, (box.min_y + box.max_y) * .5f, start.z}, 0};
        fraction = std::min(fraction, SweepLimit({start, 0}, {destination, 0}, {2 * radius, 2 * radius},
                                                obstacle, {box.max_y - box.min_y, box.max_x - box.min_x}));
    }
    return fraction;
}
Status SetSkirmishers(World &w, RegimentId id, bool enabled) {
    auto *r = FindRegiment(w, id);
    if (!r)
        return Status::NotFound;
    if (!w.scenario.rules.local_melee || r->skirmisher_count == 0 ||
        (enabled && (IsDeploying(w, *r) || !Commander(w, id))))
        return Status::InvalidInput;
    r->skirmishers_enabled = enabled;
    return Status::Ok;
}

void UpdateSkirmishers(World &w) {
    for (auto &r : w.regiments) {
        if (!r.skirmisher_count)
            continue;
        const bool can_detach = r.skirmishers_enabled && !r.panic && !IsDeploying(w, r) &&
                                Commander(w, r.id);
        uint32_t remaining = can_detach ? r.skirmisher_count : 0;
        for (auto i = r.units.rbegin(); i != r.units.rend(); ++i) {
            auto &u = *FindUnit(w, *i);
            if (!u.alive || !u.archer || u.platform != BattlePlatform::None)
                continue;
            const bool detached = remaining > 0 && u.control != ControlMode::PlayerControlled;
            if (detached)
                --remaining;
            if (detached == u.skirmisher)
                continue;
            u.skirmisher_returning = u.skirmisher && u.control != ControlMode::PlayerControlled;
            u.skirmisher = detached;
            u.bypassing = false;
            u.contact_travel = {};
        }
    }
}

static float FormationSpeedAtPace(const World &w, const RegimentState &r, float pace) {
    float speed = r.speed;
    float engine_limit = std::numeric_limits<float>::max();
    for (auto id : r.units) {
        const auto *u = FindUnit(w, id);
        if (!u || !u->alive)
            continue;
        if (u->platform == BattlePlatform::Ballista)
            return 0.f;
        if (!u->skirmisher && u->control != ControlMode::PlayerControlled) {
            speed = std::min(speed, u->speed);
            if (IsSiegeEngine(u->platform))
                engine_limit = std::min(engine_limit, SiegeTravelLimit(u->platform));
        }
    }
    speed = std::min(speed * pace, engine_limit);
    if (!w.scenario.rules.regiment_tactics)
        return speed;
    return speed * (1.f - .35f * r.fatigue) * (r.disordered ? .70f : 1.f);
}
float FormationTravelSpeed(const World &w, const RegimentState &r) {
    return FormationSpeedAtPace(w, r, PaceMultiplier(r));
}
float FormationTravelSpeed(const World &w, const RegimentState &r, OrderType type, MovementPace pace) {
    return FormationSpeedAtPace(w, r, type == OrderType::FallBack ? .5f : OrderedPaceMultiplier(pace));
}
bool PreserveFallbackFacing(const World &world, const RegimentState &regiment, Vec3 destination) {
    if (!world.scenario.rules.regiment_tactics)
        return true;
    const auto *lead = regiment.units.empty() ? nullptr : FindUnit(world, regiment.units.front());
    return lead && lead->troop != UnitTroop::Mounted && lead->platform == BattlePlatform::None &&
           Distance(regiment.pose.position, destination) <= 4.f;
}

namespace {
float FormationMotionLimit(const World &w, const RegimentState &r, Pose start, Pose destination,
                           FormationFootprint footprint) {
    float fraction = 1.f;
    for (const auto &other : w.regiments)
        if (other.id != r.id && !other.units.empty() &&
            (!r.panic || !w.scenario.rules.regiment_tactics || other.team != r.team))
            fraction = std::min(fraction, SweepLimit(start, destination, footprint,
                                                   other.pose, Footprint(other)));
    for (const auto &box : w.scenario.obstacles) {
        if (TerrainIsCrossing(box) &&
            !TerrainAreaOverlaps(box, start, footprint.depth_m * .5f, footprint.width_m * .5f) &&
            TerrainAreaOverlaps(box, destination, footprint.depth_m * .5f, footprint.width_m * .5f)) {
            const bool occupied = std::ranges::any_of(w.regiments, [&](const auto &other) {
                const auto size = Footprint(other);
                return other.id != r.id && !other.units.empty() &&
                    TerrainAreaOverlaps(box, other.pose, size.depth_m * .5f, size.width_m * .5f);
            });
            if (occupied)
                return 0.f;
        }
        if (!TerrainBlocksMovement(box))
            continue;
        const Pose obstacle{{(box.min_x + box.max_x) * .5f, (box.min_y + box.max_y) * .5f,
                             start.position.z}, 0};
        const FormationFootprint size{box.max_y - box.min_y, box.max_x - box.min_x};
        fraction = std::min(fraction, SweepLimit(start, destination, footprint, obstacle, size));
    }
    return fraction;
}

float HalfExtent(Pose pose, FormationFootprint size, Vec3 axis) {
    return .5f * (std::abs(Dot(Forward(pose.yaw), axis)) * size.depth_m +
                  std::abs(Dot(Right(pose.yaw), axis)) * size.width_m);
}

Vec3 LooseSkirmishEscape(const World &world, const RegimentState &regiment) {
    if (!regiment.loose_skirmish || regiment.panic || regiment.disordered ||
        regiment.following_officer || IsDeploying(world, regiment) || IsWithdrawing(regiment.ai_action) ||
        (regiment.order && regiment.order->status == OrderStatus::Active && regiment.order->player_direct))
        return {};

    const auto footprint = Footprint(regiment);
    Vec3 escape{};
    float nearest_gap = 7.f;
    for (const auto &enemy : world.regiments) {
        if (enemy.team == regiment.team || enemy.units.empty() || enemy.loose_skirmish ||
            enemy.panic || enemy.disordered || IsWithdrawing(enemy.ai_action) || IsDeploying(world, enemy))
            continue;
        auto away = regiment.pose.position - enemy.pose.position;
        away.z = 0.f;
        const float distance = Length(away);
        if (distance < .001f)
            continue;
        away = away * (1.f / distance);
        const float gap = distance - HalfExtent(enemy.pose, Footprint(enemy), away) -
                          HalfExtent(regiment.pose, footprint, away);
        if (gap >= nearest_gap || Dot(Forward(enemy.pose.yaw), away) < .5f)
            continue;

        // Ignore turning waypoints and reacting to a line that is marching past this screen.
        bool closing = enemy.melee_engaged && enemy.melee_opponent == regiment.id;
        for (size_t next = enemy.waypoint; !closing && next < enemy.route.size(); ++next) {
            const auto travel = enemy.route[next].position - enemy.pose.position;
            const float length = std::hypot(travel.x, travel.y);
            if (length < .01f)
                continue;
            closing = Dot(travel, away) >= .5f * length;
            break;
        }
        if (!closing)
            continue;
        const size_t melee = size_t(std::ranges::count_if(enemy.units, [&](UnitId id) {
            const auto *unit = FindUnit(world, id);
            return unit && unit->alive && unit->control != ControlMode::PlayerControlled &&
                   ((!unit->archer && !IsSiegeEngine(unit->platform)) || IsElephant(unit->platform));
        }));
        if (melee <= enemy.units.size() / 2 || FormationTravelSpeed(world, enemy) <= 0.f)
            continue;
        escape = away;
        nearest_gap = gap;
    }
    return escape;
}

void AdvanceLooseSkirmish(World &world, RegimentState &regiment, UnitTroop troop, Vec3 escape, float dt) {
    const float speed = FormationTravelSpeed(world, regiment);
    const auto origin = regiment.pose.position;
    const auto endpoint = origin + escape * (speed * dt);
    const auto footprint = Footprint(regiment);
    const float hill = world.scenario.hill
        ? HillUphillSpeedMultiplier(*world.scenario.hill, origin, endpoint, troop, speed) : 1.f;
    const float terrain = TerrainSpeedMultiplier(origin, endpoint, world.scenario.obstacles, troop, speed);
    Pose target{origin + (endpoint - origin) * (hill * terrain), regiment.pose.yaw};
    if (const auto &bounds = world.scenario.field_bounds) {
        const auto inset = TerrainFieldInset(*bounds, target.yaw, footprint.depth_m * .5f,
                                             footprint.width_m * .5f);
        const float limit_x = std::max(inset.x, std::abs(origin.x));
        const float limit_y = std::max(inset.y, std::abs(origin.y));
        target.position.x = std::clamp(target.position.x, -limit_x, limit_x);
        target.position.y = std::clamp(target.position.y, -limit_y, limit_y);
    }
    const float fraction = FormationMotionLimit(world, regiment, regiment.pose, target, footprint);
    regiment.pose.position = origin + (target.position - origin) * fraction;
}

constexpr size_t MaxMarchLookahead = 32;
bool SameMarchLine(Pose pose, Pose lane, Vec3 side) {
    return std::abs(Angle(pose.yaw - lane.yaw)) < .0001f &&
           std::abs(Dot(pose.position - lane.position, side)) < .002f;
}

std::vector<Vec3> FindMarchDetour(const World &w, const RegimentState &regiment,
                                 Pose intended_step, Pose waypoint, UnitTroop troop,
                                 FormationFootprint footprint) {
    const auto start = regiment.pose;
    const auto delta = waypoint.position - start.position;
    const Vec3 horizontal{delta.x, delta.y, 0.f};
    const float distance = Length(horizontal);
    if (distance < .01f)
        return {};
    const auto direction = horizontal * (1.f / distance);
    const Vec3 sideways{-direction.y, direction.x, 0.f};
    const RegimentState *blocker = nullptr;
    float first = 1.f;
    for (const auto &other : w.regiments) {
        if (other.id == regiment.id || other.units.empty())
            continue;
        const auto fraction = SweepLimit(start, intended_step, footprint, other.pose, Footprint(other));
        if (fraction < first) {
            blocker = &other;
            first = fraction;
        }
    }
    // Enemy contact and closed terrain keep their existing hold/replan behavior.
    if (!blocker || blocker->team != regiment.team)
        return {};
    const auto size = Footprint(*blocker);
    const auto offset = blocker->pose.position - start.position;
    constexpr float clearance = .16f;
    const float forward = Dot(offset, direction) + HalfExtent(start, footprint, direction) +
                          HalfExtent(blocker->pose, size, direction) + clearance;
    if (forward <= 0.f || forward > 12.f)
        return {};
    // A grid route often represents one straight lane as many one-cell steps.
    // Rejoin within that same run, before the first route corner.
    float clear_run = distance;
    const size_t lookahead = std::min(regiment.route.size() - regiment.waypoint, MaxMarchLookahead);
    for (size_t i = 1; clear_run < forward && i < lookahead; ++i) {
        const auto pose = regiment.route[regiment.waypoint + i];
        const float along = Dot(pose.position - start.position, direction);
        if (along <= clear_run || !SameMarchLine(pose, waypoint, sideways))
            break;
        clear_run = along;
    }
    if (clear_run < forward)
        return {};
    const float shift = Dot(offset, sideways);
    const float side_extent = HalfExtent(start, footprint, sideways) +
                              HalfExtent(blocker->pose, size, sideways) + clearance;
    const float near_side = shift <= 0.f ? 1.f : -1.f;
    for (const float side : {near_side, -near_side}) {
        const float lateral = shift + side * side_extent;
        const float width = std::clamp(footprint.width_m * 2.f, 3.f, 8.f);
        if (std::abs(lateral) > width)
            continue;
        const Vec3 rejoin = start.position + delta * (forward / distance);
        const Vec3 shoulder = sideways * lateral;
        std::vector<Vec3> points{start.position + shoulder, rejoin + shoulder, rejoin};
        Pose from = start;
        const bool clear = std::ranges::all_of(points, [&](Vec3 point) {
            const Pose to{point, start.yaw};
            if (const auto &bounds = w.scenario.field_bounds) {
                const auto inset = TerrainFieldInset(*bounds, to.yaw, footprint.depth_m * .5f,
                                                     footprint.width_m * .5f);
                if (std::abs(point.x) > inset.x || std::abs(point.y) > inset.y)
                    return false;
            }
            if ((w.scenario.hill && !HillSegmentPassable(*w.scenario.hill, from.position, point, troop)) ||
                FormationMotionLimit(w, regiment, from, to, footprint) < .99999f)
                return false;
            from = to;
            return true;
        });
        if (clear)
            return points;
    }
    return {};
}

void FinishMarchDetour(RegimentState &regiment, const MarchLane &lane) {
    const auto delta = lane.detour[1] - lane.detour[0];
    const Vec3 side{-delta.y, delta.x, 0.f};
    const auto length = Length(side);
    if (length < .01f)
        return;
    const auto end = lane.detour.back();
    const size_t limit = regiment.waypoint +
        std::min(regiment.route.size() - regiment.waypoint, MaxMarchLookahead);
    for (; regiment.waypoint < limit; ++regiment.waypoint) {
        const auto pose = regiment.route[regiment.waypoint];
        if (Dot(pose.position - end, delta) > .001f ||
            !SameMarchLine(pose, {end, lane.waypoint.yaw}, side * (1.f / length)))
            break;
    }
}
} // namespace

void AdvanceFormations(World &w, float dt) {
    CompleteDeployments(w);
    std::optional<LocalMelee> contact;
    if (w.scenario.rules.local_melee) {
        UpdateSkirmishers(w);
        contact.emplace(w, dt);
    }
    AdvanceFormations(w, dt, contact ? &*contact : nullptr);
    if (contact) {
        contact->ApplyShoves(w);
        contact->Refresh(w);
    }
}
void AdvanceFormations(World &w, float dt, const LocalMelee *contact) {
    std::vector<size_t> order(w.regiments.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        auto &x = w.regiments[a];
        auto &y = w.regiments[b];
        int xp = x.order ? x.order->priority : 0, yp = y.order ? y.order->priority : 0;
        return xp != yp ? xp > yp : x.id < y.id;
    });
    const bool regiment_tactics = w.scenario.rules.regiment_tactics;
    std::vector<std::optional<Pose>> shared_step(w.regiments.size());
    for (const auto &plan : ActiveFormationTranslations(w)) {
        for (const auto id : plan.members) {
            auto &r = *FindRegiment(w, id);
            shared_step[&r - w.regiments.data()] = r.pose;
        }
        const bool ready = std::ranges::all_of(plan.members, [&](RegimentId id) {
            const auto &r = *FindRegiment(w, id);
            return r.ai_action == OrderType::MoveTo && r.waypoint < r.route.size() &&
                   (!contact || !contact->Engaged(r)) &&
                   (!regiment_tactics || !r.separate_since || r.pursuit_target);
        });
        if (!ready)
            continue;
        const auto &lead = *FindRegiment(w, plan.members.front());
        const auto delta = lead.route[lead.waypoint].position - lead.pose.position;
        if (!CanTranslateFormationGroup(w, plan.members, delta)) {
            for (const auto id : plan.members) {
                auto &r = *FindRegiment(w, id);
                r.route.clear();
                r.waypoint = 0;
                r.decision_tick = w.tick + 5;
            }
            continue;
        }
        const float distance = Length(delta);
        float advance = distance;
        for (const auto id : plan.members) {
            const auto &r = *FindRegiment(w, id);
            const auto troop = FindUnit(w, r.units.front())->troop;
            const float speed = FormationTravelSpeed(w, r);
            const auto end = Towards(r.pose.position, r.route[r.waypoint].position, speed * dt);
            const float hill = w.scenario.hill
                ? HillUphillSpeedMultiplier(*w.scenario.hill, r.pose.position, end, troop, speed) : 1.f;
            const float terrain =
                TerrainSpeedMultiplier(r.pose.position, end, w.scenario.obstacles, troop, speed);
            advance = std::min(advance, speed * hill * terrain * dt);
        }
        const auto step = distance > 0.f ? delta * (advance / distance) : Vec3{};
        for (const auto id : plan.members) {
            auto &r = *FindRegiment(w, id);
            if (advance >= distance)
                r.pose.position = r.route[r.waypoint++].position;
            else
                r.pose.position = r.pose.position + step;
        }
    }
    for (size_t index : order) {
        auto &r = w.regiments[index];
        if (r.units.empty())
            continue;
        const bool deploying = IsDeploying(w, r);
        const bool disordered = regiment_tactics && r.disordered;
        const auto troop = FindUnit(w, r.units.front())->troop;
        const float fatigue_factor = regiment_tactics ? 1.f - .35f * r.fatigue : 1.f;
        const Pose previous = shared_step[index].value_or(r.pose);
        const auto escape = !shared_step[index] ? LooseSkirmishEscape(w, r) : Vec3{};
        const bool evading = escape != Vec3{};
        const bool ordered_retreat = IsWithdrawing(r.ai_action);
        const bool withdrawing = ordered_retreat || evading;
        const bool locked = !deploying && contact && contact->Engaged(r) && !r.panic && !withdrawing;
        if (locked)
            contact->AdvanceLine(w, r, dt);
        const bool contact_just_ended = r.separate_since && r.separate_since == w.tick &&
                                       !r.panic && !withdrawing;
        // Tactical casualties leave unoccupied slots until the formation actually reforms.
        const bool close_legacy_ranks = !regiment_tactics && r.slots.size() != r.units.size();
        if (contact && !locked && (close_legacy_ranks || contact_just_ended)) {
            auto formation = r.formation;
            formation.columns = std::min(formation.columns, uint32_t(r.units.size()));
            ReformRegiment(w, r.id, formation);
        }
        if (!contact || !ordered_retreat)
            r.retreat_waypoint.reset();
        const bool has_waypoint = r.waypoint < r.route.size();
        const bool can_detour = has_waypoint && r.ai_action == OrderType::MoveTo &&
                                !deploying && !locked && !r.panic && !shared_step[index];
        const OrderId route_order = r.order && r.order->status == OrderStatus::Active ? r.order->id : OrderId{};
        if (r.march_lane && (!can_detour || r.march_lane->order != route_order ||
            r.march_lane->waypoint != r.route[r.waypoint] || r.march_lane->goal != r.route.back()))
            r.march_lane.reset();
        const bool can_back_out = contact && ordered_retreat && (has_waypoint || contact->Engaged(r));
        bool reassembling = false;
        if (regiment_tactics && !withdrawing && !r.panic && !r.pursuit_target && r.separate_since) {
            constexpr Tick SeparationReformTicks = 12;
            const auto elapsed = w.tick - std::min(w.tick, r.separate_since);
            const float tolerance = std::max(.4f, r.formation.spacing_m * .55f);
            const bool out_of_rank = std::ranges::any_of(r.units, [&](const auto id) {
                const auto &unit = *FindUnit(w, id);
                return unit.control != ControlMode::PlayerControlled && !unit.skirmisher &&
                       !unit.skirmisher_returning &&
                       Distance(unit.pose.position, unit.slot.position) > tolerance;
            });
            reassembling = elapsed < SeparationReformTicks || (out_of_rank && elapsed < 40);
            if (!reassembling) {
                r.separate_since = 0;
                if (!out_of_rank)
                    w.events.push_back({{w.next_event_id++}, w.tick + 1, EventKind::FormationReformed,
                                        {}, {}, r.id, 0.f, "ranks reassembled after separation"});
            }
        }
        if (evading) {
            AdvanceLooseSkirmish(w, r, troop, escape, dt);
        } else if (!shared_step[index] && (!deploying || r.panic) && !locked &&
            !reassembling && (r.retreat_waypoint || has_waypoint || can_back_out)) {
            Pose next = r.pose;
            const bool detouring = r.march_lane && r.march_lane->next < r.march_lane->detour.size();
            const auto target = r.retreat_waypoint ? Pose{*r.retreat_waypoint, r.pose.yaw}
                                : detouring      ? Pose{r.march_lane->detour[r.march_lane->next], r.pose.yaw}
                                : has_waypoint   ? r.route[r.waypoint]
                                                 : r.pose;
            const float pressure_factor = 1.f - .35f * r.artillery_pressure;
            // Fleeing figures turn to run without waiting for the whole formation to wheel.
            float turn_limit = regiment_tactics && r.panic ? std::max(r.turn_rate, 2 * Pi) * dt
                : r.turn_rate * dt * fatigue_factor * (disordered ? .65f : 1.f) * pressure_factor;
            const float requested_turn = Angle(target.yaw - r.pose.yaw);
            const float speed = FormationTravelSpeed(w, r);
            float movement_budget = speed * dt;
            float wheel_radius = 0.f;
            float rank_budget = movement_budget;
            if (regiment_tactics && !r.panic && speed > 0.f && requested_turn != 0.f) {
                const auto footprint = Footprint(r);
                wheel_radius = std::hypot(footprint.width_m * .5f - r.formation.body_radius_m,
                                          footprint.depth_m * .5f - r.formation.body_radius_m);
                rank_budget *= pressure_factor;
                // A wide formation wheels at the walking pace of its outer file.
                // Its center cannot turn faster than that file can reach its next slot.
                if (wheel_radius > 0.f) {
                    turn_limit = std::min(turn_limit, rank_budget / wheel_radius);
                    const float planned_turn = std::clamp(requested_turn, -turn_limit, turn_limit);
                    const float ground = WheelGroundFactor(w, r, planned_turn, dt, fatigue_factor,
                                                          disordered, pressure_factor);
                    turn_limit = std::abs(planned_turn) * ground;
                    rank_budget *= ground;
                }
            }
            const float turn = std::clamp(requested_turn, -turn_limit, turn_limit);
            next.yaw = Angle(r.pose.yaw + turn);
            if (std::abs(Angle(target.yaw - next.yaw)) < .01f) {
                if (wheel_radius > 0.f)
                    movement_budget = std::max(0.f, rank_budget - wheel_radius * std::abs(turn));
                next.position = Towards(r.pose.position, target.position, movement_budget);
                if (w.scenario.hill) {
                    const auto step = next.position - r.pose.position;
                    next.position = r.pose.position + step *
                        HillUphillSpeedMultiplier(*w.scenario.hill, r.pose.position, next.position, troop, speed);
                }
                const auto step = next.position - r.pose.position;
                const float terrain = TerrainSpeedMultiplier(r.pose.position, next.position,
                    w.scenario.obstacles, troop, speed);
                next.position = Towards(r.pose.position, target.position, Length(step) * terrain);
            }
            const auto footprint = Footprint(r);
            const auto sweep = [&](Pose destination) {
                return FormationMotionLimit(w, r, r.pose, destination, footprint);
            };
            const float fraction = r.retreat_waypoint || has_waypoint ? sweep(next) : 0.f;
            if (can_detour && next != r.pose && fraction < .001f) {
                if (!r.march_lane)
                    r.march_lane = MarchLane{.waypoint = r.route[r.waypoint], .goal = r.route.back(),
                                             .order = route_order, .detour = {}};
                auto &lane = *r.march_lane;
                lane.stalled_seconds = std::min(MarchLane::StallSeconds, lane.stalled_seconds + dt);
                if (lane.stalled_seconds >= MarchLane::StallSeconds) {
                    if (detouring) {
                        lane.detour.clear();
                        lane.next = 0;
                        lane.stalled_seconds = 0.f;
                    } else if (lane.recovery_attempts < MarchLane::MaxAttempts) {
                        if (next.yaw == r.pose.yaw && next.position != r.pose.position)
                            lane.detour = FindMarchDetour(w, r, next, r.route[r.waypoint], troop, footprint);
                        ++lane.recovery_attempts;
                        lane.stalled_seconds = 0.f;
                    }
                }
            }
            if (r.retreat_waypoint || (has_waypoint && fraction >= .001f)) {
                next = {r.pose.position + (next.position - r.pose.position) * fraction,
                        r.pose.yaw + Angle(next.yaw - r.pose.yaw) * fraction};
                if (r.march_lane && next != r.pose) {
                    if (detouring || r.march_lane->recovery_attempts != 0)
                        r.march_lane->stalled_seconds = 0.f;
                    else
                        r.march_lane.reset();
                }
                r.pose = next;
                const bool arrived = Distance(next.position, target.position) < .001f &&
                    std::abs(Angle(next.yaw - target.yaw)) < .001f;
                if (r.retreat_waypoint) {
                    if (arrived || fraction < .001f) {
                        r.retreat_waypoint.reset();
                        r.decision_tick = 0;
                    }
                } else if (arrived && detouring) {
                    auto &lane = *r.march_lane;
                    if (++lane.next == lane.detour.size()) {
                        FinishMarchDetour(r, lane);
                        r.march_lane.reset();
                    }
                } else if (arrived) {
                    ++r.waypoint;
                    r.march_lane.reset();
                }
            } else if (can_back_out && speed > 0.f) {
                const auto right = Right(r.pose.yaw);
                const auto &deployed = w.scenario.regiments[index].pose.position;
                const auto retreat_goal = r.ai_action == OrderType::FallBack && r.order ?
                                              r.order->point : r.rally_point;
                const auto shoulder = right * (Dot(retreat_goal - deployed, right) < 0.f ? -1.f : 1.f);
                const auto back = Forward(r.pose.yaw) * -1.f;
                std::array escape_directions{r.panic ? r.rout_direction : Vec3{},
                                             back, shoulder, shoulder * -1.f};
                if (Dot(back, retreat_goal - r.pose.position) <= 0.f)
                    std::swap(escape_directions[1], escape_directions[2]);
                // Clear enough space for the navigation grid to snap to a new cell.
                // Commit to the full step before letting the planner choose another route.
                const float reach = std::clamp(r.formation.spacing_m * 1.5f, 1.5f, 3.f);
                for (const auto direction : escape_directions) {
                    if (Length(direction) < .5f)
                        continue;
                    const Pose escape{r.pose.position + direction * reach, r.pose.yaw};
                    const float clear = sweep(escape);
                    if (reach * clear < .85f)
                        continue;
                    r.retreat_waypoint = r.pose.position + direction * (reach * clear);
                    r.route.clear();
                    r.waypoint = 0;
                    break;
                }
            }
        }
        const float advance = Dot(r.pose.position - previous.position, Forward(r.pose.yaw));
        if (regiment_tactics && r.cavalry_phase == CavalryPhase::Approach) {
            const bool wheeled = std::ranges::any_of(r.units, [&](UnitId id) {
                return FindUnit(w, id)->platform == BattlePlatform::ScythedChariot;
            });
            const float wheeling = std::abs(Angle(r.pose.yaw - previous.yaw));
            if (wheeling > .05f || (wheeling > .0001f && advance <= 0.f) || advance < 0.f ||
                TerrainForbidsCharge(previous.position, r.pose.position, w.scenario.obstacles, wheeled))
                r.charge_runup_m = 0.f;
            else if (advance > 0.f)
                r.charge_runup_m = std::min(12.f, r.charge_runup_m + advance);
        }
        if (regiment_tactics && advance > 0.f && r.order &&
            r.order->pace == MovementPace::Run)
            r.fatigue = std::min(1.f, r.fatigue + .025f * dt);
        if (regiment_tactics && !locked && r.pose == previous)
            r.fatigue = std::max(0.f, r.fatigue - .025f * dt);
        SetFormationSlots(r.slots, r.formation, r.pose, uint32_t(r.slots.size()), regiment_tactics && r.panic);
        if (regiment_tactics && !locked && !r.panic && !deploying && !withdrawing &&
            r.waypoint >= r.route.size() && !r.following_officer)
            AssignCoverGuardSlots(w, r);
        const float pace = PaceMultiplier(r);
        const bool guard_stance = regiment_tactics && IsHolding(r.ai_action) && !r.panic &&
                                  !r.following_officer && r.pose.position == previous.position;
        const bool back_step = evading || (regiment_tactics && r.ai_action == OrderType::FallBack &&
            !r.panic && r.order && PreserveFallbackFacing(w, r, r.order->point));
        for (auto id : r.units) {
            auto *u = FindUnit(w, id);
            if (!u || !u->alive || u->control == ControlMode::PlayerControlled)
                continue;
            if (u->slot_index >= r.slots.size())
                continue;
            u->slot = r.slots[u->slot_index];
            if (contact && (u->skirmisher || u->skirmisher_returning)) {
                contact->AdvanceSkirmisher(*u, r, dt);
                continue;
            }
            if (locked)
                continue;
            if (contact && contact->HoldForWindup(*u))
                continue;
            const auto from = u->pose.position;
            float refill_speed = RankRefillSpeed(*u, pace, fatigue_factor, disordered,
                                                1.f - .35f * r.artillery_pressure);
            // A unit already in place has no terrain segment to traverse. Preserve the
            // original arithmetic for unusual inputs where it can change float bit patterns.
            if (from == u->slot.position && !HasNegativeZero(from) &&
                !HasNegativeZero(u->slot.position) && std::isfinite(dt) && dt >= 0.f &&
                std::isfinite(refill_speed) &&
                (refill_speed == 0.f || refill_speed >= std::numeric_limits<float>::min())) {
                u->pose.yaw = u->slot.yaw;
                if (u->control == ControlMode::Returning)
                    u->control = ControlMode::FormationControlled;
                continue;
            }
            const auto to_slot = u->slot.position - from;
            const float slot_gap_squared = Dot(to_slot, to_slot);
            const bool infantry = u->troop != UnitTroop::Mounted && u->platform == BattlePlatform::None;
            const bool close_guard_step = infantry && guard_stance && slot_gap_squared <= 1.f &&
                Dot(to_slot, Forward(u->slot.yaw)) < -.001f;
            if (close_guard_step)
                refill_speed *= .5f;
            Vec3 destination = Towards(from, u->slot.position, refill_speed * dt);
            if (w.scenario.hill) {
                destination = from + (destination - from) *
                    HillUphillSpeedMultiplier(*w.scenario.hill, from, destination, u->troop, refill_speed);
            }
            const float terrain = TerrainSpeedMultiplier(from, destination, w.scenario.obstacles,
                                                         u->troop, refill_speed);
            destination = Towards(from, u->slot.position, Distance(from, destination) * terrain);
            const float radius = contact ? contact->BodyRadius(*u) : r.formation.body_radius_m;
            const float fraction = UnitTerrainLimit(w, u->pose.position, destination, radius);
            if (fraction < 1.f)
                destination = u->pose.position + (destination - u->pose.position) * fraction;
            const float bypass_speed = !r.panic && !withdrawing && !deploying &&
                !IsSiegeEngine(u->platform) ? refill_speed : 0.f;
            u->pose.position = contact ? contact->Constrain(*u, destination, bypass_speed, dt) : destination;
            const bool face_motion = regiment_tactics && (r.panic || (!back_step && !close_guard_step &&
                (advance < -.001f || u->troop == UnitTroop::Mounted || slot_gap_squared > 1.f)));
            const auto moved = u->pose.position - from;
            u->pose.yaw = face_motion && moved.x * moved.x + moved.y * moved.y > .000001f
                ? std::atan2(moved.y, moved.x) : u->slot.yaw;
            if (u->control == ControlMode::Returning &&
                Distance(u->pose.position, u->slot.position) < .01f)
                u->control = ControlMode::FormationControlled;
        }
    }
}
} // namespace battle
