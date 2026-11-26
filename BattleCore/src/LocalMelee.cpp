#include "LocalMelee.h"
#include "ArmyStaff.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
#include <limits>

namespace battle {
namespace {
constexpr float PlatformMeleeMargin = .25f;
constexpr float PlatformContactMargin = .5f;

Vec3 Ground(Vec3 p) {
    return {p.x, p.y, 0};
}

bool HoldsContact(const RegimentState &r) {
    return !r.panic && !IsWithdrawing(r.ai_action);
}

void Face(UnitState &u, Vec3 point) {
    const Vec3 offset = Ground(point - u.pose.position);
    if (Dot(offset, offset) > 1e-8f)
        u.pose.yaw = std::atan2(offset.y, offset.x);
}
} // namespace

WeaponSpec DaggerWeapon(const UnitState &unit) {
    if (IsElephant(unit.platform))
        return {2.8f, 2.4f, 30, 4, 20.f, 1.f};
    if (unit.ranged_weapon == RangedWeapon::Musket)
        return {2.1f, 1.75f, 24, 4, 20.f, 1.f};
    WeaponSpec dagger;
    dagger.damage = std::min(.9f, unit.weapon.damage * .35f);
    dagger.range = .95f;
    dagger.cooldown_ticks = 26;
    dagger.windup_ticks = 3;
    return dagger;
}

LocalMelee::LocalMelee(const World &w, float dt)
    : world_(w), start_(w.units.size()), unit_regiments_(w.units.size()), radii_(w.units.size()),
      windup_targets_(w.units.size()),
      engaged_(w.regiments.size()), personal_contact_(w.units.size()),
      opponents_(w.regiments.size()) {
    std::vector<UnitPosition> positions;
    positions.reserve(w.units.size());
    std::vector<bool> platform_regiments(w.regiments.size());
    for (const auto &u : w.units) {
        const auto *regiment = FindRegiment(w, u.regiment);
        unit_regiments_[UnitIndex(u)] = regiment;
        if (regiment && u.platform != BattlePlatform::None)
            platform_regiments[regiment - w.regiments.data()] = true;
    }
    for (const auto &u : w.units) {
        if (!u.alive)
            continue;
        const size_t i = UnitIndex(u);
        const auto *r = unit_regiments_[i];
        start_[i] = u.pose.position;
        radii_[i] = r ? r->formation.body_radius_m : .25f;
        if (u.platform != BattlePlatform::None)
            max_platform_radius_ = std::max(max_platform_radius_, radii_[i]);
        else if (r && platform_regiments[r - w.regiments.data()])
            radii_[i] = std::min(.3f, radii_[i]);
        max_radius_ = std::max(max_radius_, radii_[i]);
        max_step_ = std::max(max_step_, std::max(u.speed * 1.45f, Length(u.desired_velocity)) * dt);
        positions.push_back({u.id, Ground(u.pose.position), r ? r->team : u.independent_team, i});
    }
    if (w.scenario.rules.adaptive_battle) {
        unindexed_ = std::move(positions);
        DetectCoarseCandidates(dt);
    } else {
        index_.Rebuild(positions);
    }
    for (const auto &attack : w.attacks) {
        if (attack.projectile)
            continue;
        if (const auto *source = FindUnit(w, attack.source); source && source->alive) {
            auto &target = windup_targets_[UnitIndex(*source)];
            if (!target)
                target = attack.target;
        }
    }
    DetectContact();
}

void LocalMelee::DetectCoarseCandidates(float dt) {
    // These are bounds of the actual fighters, including stragglers and detached archers.
    // Keep the broad phase cheap even for a custom scenario with many independent figures.
    constexpr size_t MaxGroups = 256;
    const size_t regiments = world_.regiments.size();
    if (regiments > MaxGroups)
        return;
    coarse_groups_.resize(regiments);
    unit_groups_.resize(world_.units.size());
    for (size_t i = 0; i < regiments; ++i) {
        const auto &r = world_.regiments[i];
        auto &group = coarse_groups_[i];
        group.team = r.team;
        group.clear_march = world_.scenario.obstacles.empty() && !r.panic && !r.melee_engaged &&
            !r.retreat_waypoint && r.slots.size() == r.units.size();
    }
    for (const auto &position : unindexed_) {
        const auto &u = world_.units[position.payload_index];
        const auto *r = Regiment(u);
        if (!r && coarse_groups_.size() == MaxGroups) {
            coarse_groups_.clear();
            return;
        }
        const size_t index = r ? size_t(r - world_.regiments.data()) : coarse_groups_.size();
        if (!r) {
            coarse_groups_.emplace_back();
            coarse_groups_.back().team = u.independent_team;
        }
        unit_groups_[position.payload_index] = index;
        auto &group = coarse_groups_[index];
        const auto p = position.position;
        if (group.count++ == 0) {
            group.min = group.max = p;
        } else {
            group.min = {std::min(group.min.x, p.x), std::min(group.min.y, p.y), 0.f};
            group.max = {std::max(group.max.x, p.x), std::max(group.max.y, p.y), 0.f};
        }
        group.max_range = std::max(group.max_range, std::max(u.weapon.range, u.archer ? .95f : 0.f));
        if (u.ranged_attack && u.ammunition_remaining)
            group.max_range = std::max(group.max_range, u.ranged_attack->range);
        group.max_step = std::max(group.max_step,
                                  std::max(u.speed * 1.45f, Length(u.desired_velocity)) * dt);
        if (group.clear_march) {
            group.clear_march = r && u.control == ControlMode::FormationControlled &&
                !u.skirmisher && !u.skirmisher_returning && u.slot_index < r->slots.size() &&
                r->slots[u.slot_index].position == u.pose.position;
        }
    }
    const auto near = [](const CoarseGroup &a, const CoarseGroup &b, float margin) {
        return a.min.x <= b.max.x + margin && b.min.x <= a.max.x + margin &&
               a.min.y <= b.max.y + margin && b.min.y <= a.max.y + margin;
    };
    const float platform_reach = max_platform_radius_ > 0.f
        ? max_platform_radius_ + max_radius_ + PlatformMeleeMargin : 0.f;
    for (size_t i = 0; i < coarse_groups_.size(); ++i) {
        auto &a = coarse_groups_[i];
        if (!a.count)
            continue;
        // Two soldiers on distinct slots can each move one step. Losing the clearance
        // proof restores the normal swept per-body checks, including during sharp turns.
        if (a.clear_march) {
            const auto &r = world_.regiments[i];
            a.clear_march = r.formation.spacing_m >
                2.f * (r.formation.body_radius_m + a.max_step) + .002f;
        }
        for (size_t j = i + 1; j < coarse_groups_.size(); ++j) {
            auto &b = coarse_groups_[j];
            if (!b.count)
                continue;
            const float motion = a.max_step + b.max_step + .002f;
            if (near(a, b, motion + 2.f * max_radius_))
                a.clear_march = b.clear_march = false;
            if (a.team == b.team)
                continue;
            if (j < regiments && near(a, b, motion + std::max(4.f, platform_reach + PlatformContactMargin)))
                a.can_contact = b.can_contact = true;
            if (near(a, b, motion + std::max(a.max_range, platform_reach)))
                a.can_acquire = true;
            if (near(a, b, motion + std::max(b.max_range, platform_reach)))
                b.can_acquire = true;
        }
        if (a.can_contact)
            a.clear_march = false;
    }
}

void LocalMelee::DetectContact() {
    std::fill(engaged_.begin(), engaged_.end(), false);
    std::fill(personal_contact_.begin(), personal_contact_.end(), false);
    std::fill(opponents_.begin(), opponents_.end(), RegimentId{});
    std::vector<float> nearest(opponents_.size(), std::numeric_limits<float>::max());
    if (!coarse_groups_.empty() && std::none_of(coarse_groups_.begin(), coarse_groups_.end(),
            [](const auto &group) { return group.can_contact; }))
        return;
    // A soldier cut off from the parent line can still fight personally. If
    // the rest of both regiments have already pulled apart, that duel must
    // not keep their formation anchors frozen.
    std::vector<bool> with_formation(world_.units.size());
    for (const auto &r : world_.regiments) {
        if (!coarse_groups_.empty() && !coarse_groups_[&r - world_.regiments.data()].can_contact)
            continue;
        const auto footprint = Footprint(r);
        const auto forward = Forward(r.pose.yaw);
        const auto right = Right(r.pose.yaw);
        const float margin = std::max(.8f, r.formation.spacing_m);
        for (const auto id : r.units) {
            const auto *unit = FindUnit(world_, id);
            if (!unit || !unit->alive || unit->skirmisher || unit->skirmisher_returning)
                continue;
            const auto relative = unit->pose.position - r.pose.position;
            with_formation[UnitIndex(*unit)] =
                std::abs(Dot(relative, forward)) <= footprint.depth_m * .5f + margin &&
                std::abs(Dot(relative, right)) <= footprint.width_m * .5f + margin;
        }
    }
    for (const auto &u : world_.units) {
        if (!u.alive || !u.regiment)
            continue;
        const auto &r = *Regiment(u);
        if (!coarse_groups_.empty() && !coarse_groups_[unit_groups_[UnitIndex(u)]].can_contact)
            continue;
        const float weapon_range = u.archer ? DaggerWeapon(u).range : u.weapon.range;
        // Stay in contact while the front readjusts just outside a weapon's reach.
        const float reach = std::min(std::max(weapon_range, radii_[UnitIndex(u)] + max_radius_) +
                                         r.formation.spacing_m * .55f, 4.f);
        const float platform_reach = max_platform_radius_ > 0.f
            ? max_platform_radius_ + max_radius_ + PlatformMeleeMargin + PlatformContactMargin : 0.f;
        Neighbors(u.pose.position, std::max(reach, platform_reach), r.team);
        for (auto index : nearby_) {
            const auto &other = world_.units[index];
            const auto *opponent = unit_regiments_[index];
            if (!opponent)
                continue;
            const float distance = Distance(u.pose.position, other.pose.position);
            const float pair_reach = u.platform == BattlePlatform::None && other.platform == BattlePlatform::None
                ? reach : std::max(reach, MeleeReach(u, other, weapon_range) + PlatformContactMargin);
            if (distance > pair_reach ||
                UnitTerrainLimit(world_, u.pose.position, other.pose.position, .002f) < 1.f)
                continue;
            if (!with_formation[UnitIndex(u)] || !with_formation[index]) {
                // A detached or straggling soldier can defend itself without stopping the main body.
                personal_contact_[UnitIndex(u)] = true;
                personal_contact_[index] = true;
            } else {
                const auto regiment_index = &r - world_.regiments.data();
                const auto opposite = opponent - world_.regiments.data();
                engaged_[regiment_index] = engaged_[opposite] = true;
                const auto record = [&](size_t i, RegimentId id) {
                    if (distance < nearest[i] || (distance == nearest[i] && id < opponents_[i])) {
                        nearest[i] = distance;
                        opponents_[i] = id;
                    }
                };
                record(regiment_index, opponent->id);
                record(opposite, r.id);
            }
        }
    }
}

bool LocalMelee::DaggerDrawn(const UnitState &u) const {
    if (!u.alive || !u.archer || !u.regiment)
        return false;
    return personal_contact_[UnitIndex(u)] || (!u.skirmisher && Engaged(*Regiment(u)));
}

bool LocalMelee::CanAcquire(const UnitState &u) const {
    return coarse_groups_.empty() || coarse_groups_[unit_groups_[UnitIndex(u)]].can_acquire;
}

bool LocalMelee::RangedTargetAllowed(const UnitState &u, bool skirmisher_shooter) const {
    if (personal_contact_[UnitIndex(u)])
        return false;
    if (!u.regiment)
        return command::IsArmyStaffTarget(world_, u);
    if (u.skirmisher)
        return skirmisher_shooter;
    return !Engaged(*Regiment(u));
}

void LocalMelee::Refresh(World &w) {
    // Keep the start-of-step index for swept collision. The max movement margin finds
    // bodies inside that index even after they have advanced to their new positions.
    DetectContact();
    for (auto &r : w.regiments) {
        const bool engaged = Engaged(r);
        if (w.scenario.rules.regiment_tactics) {
            if (engaged) {
                r.melee_opponent = opponents_[&r - w.regiments.data()];
                r.separate_since = 0;
            } else if (r.melee_engaged) {
                r.separate_since = w.tick + 1;
                r.contact_end_tick = w.tick + 1;
                r.decision_tick = 0;
            }
        }
        r.melee_engaged = engaged;
    }
    for (auto &u : w.units) {
        u.dagger_drawn = DaggerDrawn(u);
        const auto *r = Regiment(u);
        if (!u.skirmisher && (!r || !HoldsContact(*r) || !Engaged(*r))) {
            u.bypassing = false;
            u.contact_travel = {};
        }
    }
}

size_t LocalMelee::UnitIndex(const UnitState &u) const {
    return static_cast<size_t>(&u - world_.units.data());
}

const RegimentState *LocalMelee::Regiment(const UnitState &u) const {
    return unit_regiments_[UnitIndex(u)];
}

int32_t LocalMelee::Team(const UnitState &u) const {
    const auto *r = Regiment(u);
    return r ? r->team : u.independent_team;
}

float LocalMelee::BodyRadius(const UnitState &unit) const {
    return radii_[UnitIndex(unit)];
}

float LocalMelee::MeleeReach(const UnitState &attacker, const UnitState &defender,
                            float weapon_range) const {
    if (attacker.platform == BattlePlatform::None && defender.platform == BattlePlatform::None)
        return weapon_range;
    // Weapon range is measured at the center for ordinary fighters. Large animals and engines
    // can engage or be struck from outside their footprint.
    return std::max(weapon_range, BodyRadius(attacker) + BodyRadius(defender) + PlatformMeleeMargin);
}

bool LocalMelee::Engaged(const RegimentState &r) const {
    return engaged_[&r - world_.regiments.data()];
}

void LocalMelee::Neighbors(Vec3 p, float range, std::optional<int32_t> excluded_team) const {
    if (!unindexed_.empty()) {
        index_.Rebuild(unindexed_);
        unindexed_.clear();
    }
    // World unit storage is sorted by ID, so payload indices keep the same tie order.
    index_.QueryRadiusPayloadsInto(Ground(p), range + max_step_ + shove_margin_, nearby_, excluded_team);
}

bool LocalMelee::ClearMove(const UnitState &u, Vec3 destination) const {
    const size_t i = UnitIndex(u);
    const auto from = start_[i];
    Neighbors(from, Length(Ground(destination - from)) + radii_[i] + max_radius_);
    return ClearMove(u, destination, nearby_);
}

bool LocalMelee::ClearMove(const UnitState &u, Vec3 destination,
                           std::span<const size_t> neighbors, const UnitState **blocker) const {
    const size_t i = UnitIndex(u);
    const Vec3 from = start_[i];
    const Vec3 travel = Ground(destination - from);
    for (auto j : neighbors) {
        if (j == i)
            continue;
        const auto &other = world_.units[j];
        const Vec3 gap = Ground(from - start_[j]);
        const Vec3 relative = travel - Ground(other.pose.position - start_[j]);
        const float relative_squared = Dot(relative, relative);
        const float t = relative_squared > 0.f
                            ? std::clamp(-Dot(gap, relative) / relative_squared, 0.f, 1.f)
                            : 0.f;
        const Vec3 nearest = gap + relative * t;
        const float radius = radii_[i] + radii_[j];
        // Imported layouts can already overlap. A move may increase their clearance.
        if (Dot(nearest, nearest) + 1e-6f < std::min(radius * radius, Dot(gap, gap))) {
            if (blocker)
                *blocker = &other;
            return false;
        }
    }
    return true;
}

std::optional<Vec3> LocalMelee::BypassStaff(const UnitState &u, const UnitState &staff, Vec3 from,
                                         Vec3 travel, float speed, float dt) const {
    const auto *regiment = Regiment(u);
    if (!regiment || staff.regiment || Team(staff) != regiment->team)
        return std::nullopt;
    const auto separation = Ground(from - staff.pose.position);
    const float length = Length(separation);
    const float clearance = BodyRadius(u) + BodyRadius(staff) + .04f;
    // A free slot beyond the officer remains the destination. A slot occupied by the
    // officer cannot be reached by circling, so wait until the slot becomes free.
    if (length < .001f || Length(Ground(u.slot.position - staff.pose.position)) < clearance)
        return std::nullopt;
    const float distance = Length(Ground(travel));
    const auto direction = Ground(travel) * (1.f / distance);
    const Vec3 tangent{-separation.y / length, separation.x / length, 0};
    const auto outward = Ground(u.slot.position - regiment->pose.position);
    const auto lateral = Vec3{-direction.y, direction.x, 0};
    const float stride = speed * dt;
    const float radius = BodyRadius(u);
    const auto terrain_clear = [&](Vec3 point) {
        if (const auto &bounds = world_.scenario.field_bounds;
            bounds && (std::abs(point.x) + radius > bounds->half_width_m ||
                       std::abs(point.y) + radius > bounds->half_height_m))
            return false;
        return UnitTerrainLimit(world_, from, point, radius) >= 1.f;
    };
    std::optional<Vec3> best;
    float best_progress = 0.f;
    float best_outside = 0.f;
    for (float side : {1.f, -1.f}) {
        const auto heading = tangent * side;
        const float outside_sign = std::copysign(1.f, Dot(lateral, heading));
        const auto clearance_point = staff.pose.position + lateral * (outside_sign * clearance);
        if (!terrain_clear(clearance_point))
            continue;
        auto next = from + heading * stride;
        if (world_.scenario.hill)
            next = from + (next - from) * HillUphillSpeedMultiplier(*world_.scenario.hill, from, next, u.troop, speed);
        const float terrain = TerrainSpeedMultiplier(from, next, world_.scenario.obstacles, u.troop, speed);
        // The original collision query encloses this much motion in every direction.
        next = from + heading * std::min(distance, Distance(from, next) * terrain);
        const Vec3 step = next - from;
        if (Length(step) < .002f || !terrain_clear(next) || !ClearMove(u, next, nearby_))
            continue;
        const float progress = Dot(step, direction);
        const float outside = Dot(step, outward);
        if (!best || progress > best_progress + .001f ||
            (std::abs(progress - best_progress) <= .001f && outside > best_outside)) {
            best = next;
            best_progress = progress;
            best_outside = outside;
        }
    }
    return best;
}

Vec3 LocalMelee::Constrain(const UnitState &u, Vec3 destination, float march_speed, float dt) const {
    const Vec3 from = start_[UnitIndex(u)];
    if (!coarse_groups_.empty() && coarse_groups_[unit_groups_[UnitIndex(u)]].clear_march)
        return destination;
    if (destination == from)
        return from;
    const auto travel = destination - from;
    const float distance = Length(Ground(travel));
    const bool can_bypass = march_speed > 0.f && dt > 0.f && distance > .001f;
    Neighbors(from, distance + radii_[UnitIndex(u)] + max_radius_);
    const UnitState *blocker = nullptr;
    if (ClearMove(u, destination, nearby_, can_bypass ? &blocker : nullptr))
        return destination;
    if (blocker && !blocker->regiment)
        if (const auto way_around = BypassStaff(u, *blocker, from, travel, march_speed, dt))
            return *way_around;
    float clear = 0.f;
    float blocked = 1.f;
    for (int attempt = 0; attempt < 5; ++attempt) {
        const float fraction = (clear + blocked) * .5f;
        if (ClearMove(u, from + travel * fraction, nearby_))
            clear = fraction;
        else
            blocked = fraction;
    }
    return from + travel * clear;
}

bool LocalMelee::ClearStrike(const UnitState &u, const UnitState &target,
                            std::span<const size_t> neighbors) const {
    if (UnitTerrainLimit(world_, u.pose.position, target.pose.position, .002f) < 1.f)
        return false;
    const Vec3 line = Ground(target.pose.position - u.pose.position);
    const float length_squared = Dot(line, line);
    if (length_squared <= 1e-8f)
        return true;
    const size_t source = UnitIndex(u), destination = UnitIndex(target);
    for (auto j : neighbors) {
        if (j == source || j == destination)
            continue;
        const auto &other = world_.units[j];
        const Vec3 offset = Ground(other.pose.position - u.pose.position);
        const float t = Dot(offset, line) / length_squared;
        if (t <= 0.f || t >= 1.f)
            continue;
        const Vec3 side = offset - line * t;
        const float radius = radii_[j];
        if (Dot(side, side) < radius * radius)
            return false;
    }
    return true;
}

const UnitState *LocalMelee::StrikeTarget(const UnitState &u, int32_t team) const {
    const float range = u.archer ? DaggerWeapon(u).range : u.weapon.range;
    const float search = max_platform_radius_ > 0.f
        ? std::max(range + max_radius_, max_platform_radius_ + max_radius_ + PlatformMeleeMargin)
        : range + max_radius_;
    Neighbors(u.pose.position, search);
    float best = search;
    const UnitState *target = nullptr;
    for (auto index : nearby_) {
        const auto &other = world_.units[index];
        if (Team(other) == team)
            continue;
        const float distance = Distance(u.pose.position, other.pose.position);
        if (distance <= MeleeReach(u, other, range) && (!target || distance < best) &&
            ClearStrike(u, other, nearby_)) {
            best = distance;
            target = &other;
        }
    }
    return target;
}

bool LocalMelee::CanStrike(const UnitState &u, const UnitState &target, float range) const {
    const float reach = MeleeReach(u, target, range);
    Neighbors(u.pose.position, reach + max_radius_);
    return ClearStrike(u, target, nearby_);
}

bool LocalMelee::MoveTowards(UnitState &u, Vec3 goal, float step, float turn_dt,
                              bool allow_detour) const {
    const Vec3 from = u.pose.position;
    const Vec3 offset = Ground(goal - from);
    const float distance = Length(offset);
    if (distance < .01f || step <= 0.f)
        return false;
    const auto direct = offset * (1.f / distance);
    auto heading = direct;
    if (turn_dt > 0.f) {
        if (Length(u.contact_travel) > .01f) {
            const float current = std::atan2(u.contact_travel.y, u.contact_travel.x);
            const float wanted = std::atan2(heading.y, heading.x);
            const float max_turn = 8.f * turn_dt;
            heading = Forward(current + std::clamp(std::remainder(wanted - current, 2.f * Pi),
                                                   -max_turn, max_turn));
        }
        u.contact_travel = heading;
    }
    const float stride = std::min(step, distance);
    const auto step_in = [&](Vec3 direction) {
        auto next = from + direction * stride;
        if (world_.scenario.hill)
            next = from + (next - from) * HillUphillSpeedMultiplier(*world_.scenario.hill, from, next, u.troop, u.speed);
        const float speed_multiplier = TerrainSpeedMultiplier(from, next, world_.scenario.obstacles,
                                                              u.troop, u.speed);
        next = from + direction * std::min(distance, Distance(from, next) * speed_multiplier);
        const float terrain = UnitTerrainLimit(world_, from, next, radii_[UnitIndex(u)]);
        return Constrain(u, from + (next - from) * terrain);
    };
    auto destination = step_in(heading);
    if (allow_detour && Distance(from, destination) < stride * .45f) {
        // Test short diagonal steps around a body before giving up on the goal.
        // Use the same swept body and terrain checks as for normal movement.
        const float yaw = std::atan2(heading.y, heading.x);
        const float preferred_side = Dot(direct, Right(yaw)) < 0.f ? -1.f : 1.f;
        float progress = Dot(destination - from, direct);
        for (const float side : {preferred_side, -preferred_side}) {
            const auto direction = Forward(yaw + side * 1.05f);
            if (Dot(direction, direct) <= .05f)
                continue;
            const auto candidate = step_in(direction);
            const float candidate_progress = Dot(candidate - from, direct);
            if (candidate_progress > progress + .002f) {
                progress = candidate_progress;
                destination = candidate;
                u.contact_travel = direction;
            }
        }
    }
    if (Distance(from, destination) < .001f)
        return false;
    Face(u, destination);
    u.pose.position = destination;
    return true;
}

float LocalMelee::FightingDistance(const UnitState &u, const UnitState &target) const {
    const float bodies = BodyRadius(u) + BodyRadius(target);
    const float range = MeleeReach(u, target, u.archer ? DaggerWeapon(u).range : u.weapon.range);
    const float margin = std::clamp((range - bodies) * .5f, 0.f, .08f);
    return std::max(bodies + margin, range * .65f);
}

const UnitState *LocalMelee::WindupTarget(const UnitState &u) const {
    return FindUnit(world_, windup_targets_[UnitIndex(u)]);
}

bool LocalMelee::HoldForWindup(UnitState &u) const {
    const auto *target = WindupTarget(u);
    if (!target)
        return false;
    Face(u, target->pose.position);
    return true;
}

const UnitState *LocalMelee::EnemyAhead(const UnitState &u, const RegimentState &r,
                                      const Pose &lane, bool adjacent_lanes) const {
    const float range = u.archer ? DaggerWeapon(u).range : u.weapon.range;
    const float search = std::clamp(range + 3.f * r.formation.spacing_m, 3.5f, 8.f);
    const float width = adjacent_lanes ? std::max(1.6f, 2.1f * r.formation.spacing_m)
                                       : std::max(.55f, .75f * r.formation.spacing_m);
    const auto forward = Forward(lane.yaw);
    const auto right = Right(lane.yaw);
    Neighbors(u.pose.position, search, r.team);
    const UnitState *nearest = nullptr;
    float best = search;
    for (auto index : nearby_) {
        const auto &other = world_.units[index];
        if (std::abs(Dot(other.pose.position - lane.position, right)) > width)
            continue;
        const auto offset = other.pose.position - u.pose.position;
        const float distance = Length(offset);
        if (Dot(offset, forward) < -.25f || distance >= best ||
            UnitTerrainLimit(world_, u.pose.position, other.pose.position, .002f) < 1.f)
            continue;
        best = distance;
        nearest = &other;
    }
    return nearest;
}

std::optional<LocalMelee::OpposingFront>
LocalMelee::FindOpposingFront(const RegimentState &r, size_t columns) const {
    const auto forward = Forward(r.pose.yaw);
    const auto right = Right(r.pose.yaw);
    const auto centre = (r.slots.front().position + r.slots[columns - 1].position) * .5f;
    const float half_width = float(columns - 1) * r.formation.spacing_m * .5f;
    const float reach = std::clamp(4.f * r.formation.spacing_m, 3.5f, 7.f);
    const RegimentState *opponent = nullptr;
    float nearest = std::numeric_limits<float>::max();
    Neighbors(centre, std::hypot(half_width + r.formation.spacing_m, reach), r.team);
    for (auto index : nearby_) {
        const auto &unit = world_.units[index];
        if (!unit.regiment || unit.skirmisher || unit.skirmisher_returning)
            continue;
        const auto offset = start_[UnitIndex(unit)] - centre;
        const float depth = Dot(offset, forward);
        const float lateral = Dot(offset, right);
        if (depth < -r.formation.spacing_m || depth > reach ||
            std::abs(lateral) > half_width + r.formation.spacing_m)
            continue;
        const auto lane = centre + right * std::clamp(lateral, -half_width, half_width);
        const float distance = Distance(lane, start_[UnitIndex(unit)]);
        if (distance >= nearest ||
            UnitTerrainLimit(world_, lane, start_[UnitIndex(unit)], .002f) < 1.f)
            continue;
        nearest = distance;
        opponent = Regiment(unit);
    }
    if (!opponent)
        return {};
    const float extreme = std::numeric_limits<float>::max();
    OpposingFront bounds{extreme, -extreme, extreme, -extreme,
                         opponent->formation.body_radius_m};
    for (auto id : opponent->units) {
        const auto *unit = FindUnit(world_, id);
        if (!unit || !unit->alive || unit->skirmisher || unit->skirmisher_returning)
            continue;
        const auto position = start_[UnitIndex(*unit)];
        const float depth = Dot(position, forward);
        const float lateral = Dot(position, right);
        bounds.near_depth = std::min(bounds.near_depth, depth);
        bounds.far_depth = std::max(bounds.far_depth, depth);
        bounds.min_lateral = std::min(bounds.min_lateral, lateral);
        bounds.max_lateral = std::max(bounds.max_lateral, lateral);
    }
    return bounds;
}

bool LocalMelee::AdvanceWing(UnitState &u, const RegimentState &r, const Pose &lane,
                             const OpposingFront &front, float dt) const {
    const auto forward = Forward(lane.yaw);
    const auto right = Right(lane.yaw);
    const float lateral = Dot(lane.position, right);
    const float edge = lateral < front.min_lateral ? front.min_lateral : front.max_lateral;
    const float side = lateral < front.min_lateral ? -1.f : 1.f;
    const float overhang = (lateral - edge) * side;
    const float spacing = r.formation.spacing_m;
    const float radius = radii_[UnitIndex(u)];
    const float clearance = radius + front.body_radius + .18f;
    const float shoulder = front.near_depth + std::clamp(.75f * overhang, .5f,
                                                    std::max(.6f, front.far_depth - front.near_depth));
    const auto waypoint = forward * shoulder + right * (edge + side * clearance) +
                          Vec3{0.f, 0.f, lane.position.z};
    const float leash = std::clamp(4.5f * spacing, 4.f, 7.f);
    if (overhang < .55f * spacing || Distance(lane.position, waypoint) > leash ||
        Distance(lane.position, u.pose.position) > leash ||
        UnitTerrainLimit(world_, u.pose.position, waypoint, radius) < 1.f)
        return false;

    const auto *target = StrikeTarget(u, r.team);
    u.bypassing = true;
    if ((!target || Dot(u.pose.position, forward) < shoulder - .20f) &&
        MoveTowards(u, waypoint, u.speed * dt, dt, true))
        return true;
    if (!target)
        return false;
    Face(u, target->pose.position);
    return true;
}

const UnitState *LocalMelee::AdvanceLead(
    UnitState &u, const RegimentState &r, const Pose &lane,
    const std::optional<OpposingFront> &front, float dt) const {
    if (HoldForWindup(u))
        return WindupTarget(u);
    const auto *in_lane = EnemyAhead(u, r, lane);
    const bool holding_line = r.ai_action == OrderType::HoldLine || r.ai_action == OrderType::Brace;
    if (!holding_line && front && !in_lane && AdvanceWing(u, r, lane, *front, dt))
        return nullptr;
    if (u.bypassing) {
        u.bypassing = false;
        u.contact_travel = {};
    }
    const auto *target = StrikeTarget(u, r.team);
    if (!target)
        target = in_lane;
    if (!target && !holding_line)
        target = EnemyAhead(u, r, lane, true);
    if (!target)
        return nullptr;

    const float desired = FightingDistance(u, *target);
    const auto offset = Ground(target->pose.position - u.pose.position);
    const float distance = Length(offset);
    const float range = MeleeReach(u, *target, u.archer ? DaggerWeapon(u).range : u.weapon.range);
    if (distance > desired && (distance > desired + .025f || distance > range)) {
        // Keep the advance on this stretch of the line. An open wing can extend past its old slot.
        const auto forward = Forward(lane.yaw);
        const auto right = Right(lane.yaw);
        const auto goal = target->pose.position - offset * (desired / distance);
        const float reach = holding_line ? r.formation.spacing_m * .25f :
                            std::max(3.f * r.formation.spacing_m, 2.5f);
        // Keep the broader approach once that opponent enters weapon range.
        // Returning to the old lane at that point would reopen the same gap.
        const bool adjacent_lane = !holding_line &&
            std::abs(Dot(target->pose.position - lane.position, right)) >
                std::max(.55f, .75f * r.formation.spacing_m);
        const float half_lane = (adjacent_lane ? 1.45f : .42f) * r.formation.spacing_m;
        const float depth = std::clamp(Dot(goal - lane.position, forward), -reach, reach);
        const float lateral = std::clamp(Dot(goal - lane.position, right), -half_lane, half_lane);
        MoveTowards(u, lane.position + forward * depth + right * lateral, u.speed * dt,
                    adjacent_lane ? dt : 0.f, !holding_line);
    }
    Face(u, target->pose.position);
    return target;
}

void LocalMelee::AdvanceSupport(UnitState &u, const UnitState &lead, const RegimentState &r,
                                float edge, float dt) const {
    if (HoldForWindup(u))
        return;
    if (const auto *target = StrikeTarget(u, r.team)) {
        Face(u, target->pose.position);
        return;
    }
    const auto forward = Forward(r.pose.yaw);
    const auto outside = Right(r.pose.yaw) * edge;
    const float clearance = radii_[UnitIndex(u)] + radii_[UnitIndex(lead)];
    const float step = u.speed * dt;
    if (edge != 0.f && (u.bypassing || StrikeTarget(lead, r.team))) {
        const float side_gap = std::max(clearance + .18f, .70f * r.formation.spacing_m);
        const float lateral = Dot(u.pose.position - lead.pose.position, outside);
        const float ahead = Dot(lead.pose.position - u.pose.position, forward) + .20f;
        // Arc around the shoulder. The flanker keeps its lane choice even while the
        // front fighter briefly loses its target or steps sideways.
        const float corner = std::clamp(lateral / side_gap, 0.f, 1.f);
        const Vec3 waypoint = u.pose.position + outside * (side_gap - lateral) +
                              forward * (std::max(ahead, 0.f) * corner);
        const float leash = std::max(2.5f * r.formation.spacing_m, 2.f);
        if (Distance(u.slot.position, waypoint) <= leash) {
            u.bypassing = true;
            MoveTowards(u, waypoint, step, dt, true);
            return;
        }
    }
    const float gap = std::max(clearance + .10f, .70f * r.formation.spacing_m);
    Vec3 queue = lead.pose.position - forward * gap;
    if (lead.bypassing) {
        const auto right = Right(r.pose.yaw);
        queue = queue + right * Dot(u.slot.position - queue, right);
    }
    const float space_ahead = Dot(queue - u.pose.position, forward);
    if (space_ahead > .03f)
        MoveTowards(u, queue, step, dt, space_ahead > .45f * r.formation.spacing_m);
}

void LocalMelee::AdvanceLine(World &w, const RegimentState &r, float dt) const {
    const size_t columns = std::min<size_t>(r.formation.columns, r.slots.size());
    if (!HoldsContact(r) || columns == 0)
        return;
    const auto opposing_front = FindOpposingFront(r, columns);
    std::vector<UnitState *> by_slot(r.slots.size());
    for (auto id : r.units) {
        auto *unit = FindUnit(w, id);
        if (unit && unit->alive && !unit->skirmisher && !unit->skirmisher_returning && unit->control != ControlMode::PlayerControlled &&
            unit->slot_index < by_slot.size())
            by_slot[unit->slot_index] = unit;
    }
    const bool drives = w.scenario.rules.regiment_tactics && IsAttacking(r.ai_action) &&
                        !r.disordered && !r.braced && r.cavalry_phase == CavalryPhase::None;
    const auto forward = drives ? Forward(r.pose.yaw) : Vec3{};
    for (size_t column = 0; column < columns; ++column) {
        UnitState *ahead = nullptr;
        const UnitState *pressure_target = nullptr;
        for (size_t index = column; index < by_slot.size(); index += r.formation.columns) {
            auto *unit = by_slot[index];
            if (!unit)
                continue;
            if (IsSiegeEngine(unit->platform))
                continue;
            if (!ahead) {
                const auto *target = AdvanceLead(*unit, r, r.slots[column], opposing_front, dt);
                if (drives)
                    pressure_target = target;
            } else {
                const bool first_support = index - r.formation.columns == ahead->slot_index &&
                                           ahead->slot_index < r.formation.columns;
                float edge = 0.f;
                if (first_support && r.ai_action != OrderType::HoldLine &&
                    r.ai_action != OrderType::Brace)
                    edge = columns == 1 ? (unit->id.value % 2 ? -1.f : 1.f)
                                       : column == 0 ? -1.f : column + 1 == columns ? 1.f : 0.f;
                AdvanceSupport(*unit, *ahead, r, edge, dt);
                if (pressure_target) {
                    QueueRankShove(*ahead, *unit, *pressure_target, r, forward, dt);
                    pressure_target = nullptr;
                }
            }
            ahead = unit;
        }
    }
}

void LocalMelee::QueueRankShove(const UnitState &lead, const UnitState &support,
                                   const UnitState &target, const RegimentState &regiment,
                                   Vec3 forward, float dt) const {
    const auto *defending = Regiment(target);
    if (!defending || defending->team == regiment.team || IsSiegeEngine(target.platform) ||
        (regiment.ai_target && regiment.ai_target != target.regiment) ||
        lead.bypassing || support.bypassing)
        return;
    const auto behind = Ground(lead.pose.position - support.pose.position);
    const float depth = Dot(behind, forward);
    const float spacing = regiment.formation.spacing_m;
    if (depth < .35f * spacing || depth > 1.18f * spacing ||
        std::abs(Dot(behind, {-forward.y, forward.x, 0.f})) > .45f * spacing)
        return;
    const auto offset = Ground(target.pose.position - lead.pose.position);
    const float range = Length(offset);
    const float reach = std::max(BodyRadius(lead) + BodyRadius(target) + .20f,
                                 .75f * (lead.archer ? DaggerWeapon(lead).range : lead.weapon.range));
    if (range < .01f || range > reach || Dot(offset, forward) < range * .6f ||
        (defending->braced && Dot(offset, Forward(defending->pose.yaw)) < -range * .6f))
        return;
    const float resistance = target.troop == UnitTroop::Mounted || IsElephant(target.platform)
                                ? .45f : 1.f;
    shoves_.push_back({UnitIndex(lead), UnitIndex(target),
                      .85f * dt * regiment.cohesion * resistance, false});
}

void LocalMelee::QueueCavalryShove(const UnitState &source, const UnitState &target,
                                   float damage, AttackAspect aspect) const {
    if (damage <= 0.f || Team(source) == Team(target) || IsSiegeEngine(target.platform))
        return;
    const auto *defending = Regiment(target);
    const auto &initial = world_.scenario.units[UnitIndex(target)];
    const float weight = std::min(1.f, damage / initial.health);
    const float pace = source.cavalry == CavalryClass::Heavy || source.platform != BattlePlatform::None
                        ? 1.f : .76f;
    const float brace = defending && defending->braced && aspect == AttackAspect::Front ? .20f : 1.f;
    shoves_.push_back({UnitIndex(source), UnitIndex(target),
                      std::min(.55f, (.18f + weight * .55f) * pace * brace), true});
}

LocalMelee::ShoveClearance LocalMelee::SweepShove(const UnitState &unit, Vec3 direction,
                                                 float distance) const {
    const auto from = unit.pose.position;
    const float radius = BodyRadius(unit);
    const float terrain = UnitTerrainLimit(world_, from, from + direction * distance, radius);
    ShoveClearance result{distance * terrain, {}};
    if (result.distance < .0001f)
        return result;
    // The same broad phase encloses ordinary movement and the shoves accepted so
    // far. Test the current body locations: the earlier accepted motion is complete.
    Neighbors(from, result.distance + radius + max_radius_ + .1f);
    for (const auto candidate : nearby_) {
        const auto &other = world_.units[candidate];
        if (!other.alive || other.id == unit.id)
            continue;
        const auto gap = Ground(from - other.pose.position);
        const double approach = -double(Dot(gap, direction));
        if (approach <= 0.0)
            continue;
        const double combined = double(radius) + radii_[candidate];
        const double separation = double(gap.x) * gap.x + double(gap.y) * gap.y;
        const double c = std::max(0.0, separation - combined * combined);
        const double discriminant = approach * approach - c;
        if (discriminant < 0.0)
            continue;
        const float first = std::max(0.f, float(approach - std::sqrt(discriminant)) - .0001f);
        if (first < result.distance) {
            result.distance = first;
            result.blocker = candidate;
        }
    }
    return result;
}

void LocalMelee::ShoveUnit(World &world, size_t index, Vec3 direction, float distance, float budget,
                           bool pass_on) const {
    auto &unit = world.units[index];
    distance = std::min(distance, budget - shoved_[index]);
    if (!unit.alive || distance < .0001f || IsSiegeEngine(unit.platform))
        return;
    auto clearance = SweepShove(unit, direction, distance);
    if (pass_on && clearance.blocker && clearance.distance < distance * .75f) {
        const auto follower = *clearance.blocker;
        if (Team(world.units[follower]) == Team(unit)) {
            ShoveUnit(world, follower, direction, (distance - clearance.distance) * .75f,
                      budget, false);
            clearance = SweepShove(unit, direction, distance);
        }
    }
    if (clearance.distance < .0001f)
        return;
    unit.pose.position = unit.pose.position + direction * clearance.distance;
    shoved_[index] += clearance.distance;
    shove_margin_ = std::max(shove_margin_, shoved_[index]);
}

void LocalMelee::ApplyShoves(World &world) const {
    if (shoves_.empty())
        return;
    if (shoved_.empty())
        shoved_.resize(world.units.size());
    for (const auto &shove : shoves_) {
        const auto &source = world.units[shove.source];
        const auto &target = world.units[shove.target];
        if (!target.alive)
            continue;
        // A charge was confirmed before simultaneous casualties were applied.
        // Ongoing rank pressure still needs a living fighter and a clear lane.
        if (!shove.impact && (!source.alive ||
            (!world.scenario.obstacles.empty() &&
             UnitTerrainLimit(world, source.pose.position, target.pose.position, .002f) < 1.f)))
            continue;
        const auto offset = Ground(target.pose.position - source.pose.position);
        const float separation = Length(offset);
        if (separation < .001f)
            continue;
        ShoveUnit(world, shove.target, offset * (1.f / separation), shove.distance,
                  shove.impact ? .55f : .045f, true);
    }
    shoves_.clear();
}

Vec3 LocalMelee::ScreenWaypoint(const UnitState &u, Vec3 goal, Vec3 fallback) const {
    const auto start = u.pose.position;
    const float radius = radii_[UnitIndex(u)];
    const auto clear = [&](Vec3 a, Vec3 b) { return UnitTerrainLimit(world_, a, b, radius) >= 1.f; };
    if (!clear(goal, goal))
        goal = fallback;
    if (clear(start, goal))
        return goal;
    // If a flank station is separated by water, follow the shortest visible first
    // leg around the blocking rectangle. Re-evaluate from the next position.
    Vec3 next = start;
    float best = std::numeric_limits<float>::infinity();
    const float margin = radius + .35f;
    for (const auto &box : world_.scenario.obstacles) {
        if (!TerrainBlocksMovement(box))
            continue;
        const Pose obstacle{{(box.min_x + box.max_x) * .5f, (box.min_y + box.max_y) * .5f, start.z}, 0};
        if (SweepLimit({start, 0}, {goal, 0}, {2.f * radius, 2.f * radius}, obstacle,
                       {box.max_y - box.min_y, box.max_x - box.min_x}) >= 1.f)
            continue;
        for (float x : {box.min_x - margin, box.max_x + margin})
            for (float y : {box.min_y - margin, box.max_y + margin}) {
                const Vec3 corner{x, y, start.z};
                const float travel = Distance(start, corner);
                const float cost = travel + Distance(corner, goal);
                if (travel > .25f && cost < best && clear(start, corner)) {
                    next = corner;
                    best = cost;
                }
            }
    }
    return next;
}

void LocalMelee::AdvanceSkirmisher(UnitState &u, const RegimentState &r, float dt) const {
    if (HoldForWindup(u))
        return;
    if (u.skirmisher && DaggerDrawn(u)) {
        if (const auto *target = StrikeTarget(u, r.team)) {
            Face(u, target->pose.position);
            return;
        }
        const UnitState *nearest = nullptr;
        float best = 2.f;
        Neighbors(u.pose.position, best, r.team);
        for (auto index : nearby_) {
            const auto &other = world_.units[index];
            const float distance = Distance(u.pose.position, other.pose.position);
            if (distance < best &&
                UnitTerrainLimit(world_, u.pose.position, other.pose.position, .002f) >= 1.f) {
                nearest = &other;
                best = distance;
            }
        }
        if (nearest) {
            const auto goal = nearest->pose.position - (nearest->pose.position - u.pose.position) *
                                                        (FightingDistance(u, *nearest) / best);
            MoveTowards(u, goal, u.speed * dt, dt);
        }
        return;
    }
    const auto forward = Forward(r.pose.yaw);
    const auto right = Right(r.pose.yaw);
    const auto relative = u.pose.position - r.pose.position;
    const auto footprint = Footprint(r);
    const auto column = u.slot_index % r.formation.columns;
    const float side = (float(column) - float(r.formation.columns - 1) * .5f) * r.formation.spacing_m;
    const float sign = side == 0.f ? (u.id.value % 2 ? -1.f : 1.f) : std::copysign(1.f, side);
    const float flank = footprint.width_m * .5f + 1.45f + std::abs(side) * .35f;
    const float tail = -footprint.depth_m * .5f - .7f;
    const auto trail = r.pose.position + forward * tail;
    const float desired_depth = footprint.depth_m * .5f + 2.f - std::abs(side) * .42f;
    const float lateral = Dot(relative, right) * sign;
    const float depth = Dot(relative, forward);
    if (u.skirmisher_returning) {
        if (Distance(u.pose.position, u.slot.position) < .06f) {
            u.skirmisher_returning = false;
            u.contact_travel = {};
            return;
        }
        auto goal = u.slot.position;
        const float slot_depth = Dot(u.slot.position - r.pose.position, forward);
        if (lateral > footprint.width_m * .5f && depth > slot_depth + .2f)
            goal = u.pose.position + forward * (tail - depth);
        MoveTowards(u, ScreenWaypoint(u, goal, trail), u.speed * dt, dt);
        return;
    }
    // Follow the column's trail when left behind by an obstacle or a turn.
    if (depth < tail - .5f) {
        MoveTowards(u, ScreenWaypoint(u, trail, r.pose.position), u.speed * dt, dt);
        return;
    }
    const auto wing = r.pose.position + right * (flank * sign);
    if (lateral < flank - .3f) {
        // Use an open side exit when there is room, otherwise step around the last row.
        const auto side_exit = wing + forward * depth;
        const auto radius = radii_[UnitIndex(u)];
        if (UnitTerrainLimit(world_, u.pose.position, side_exit, radius) >= 1.f &&
            ClearMove(u, side_exit)) {
            MoveTowards(u, side_exit, u.speed * dt, dt);
            return;
        }
        const auto rear_exit = lateral < footprint.width_m * .5f && depth > tail + .2f
                                   ? u.pose.position + forward * (tail - depth)
                                   : wing + forward * std::min(depth, tail);
        MoveTowards(u, ScreenWaypoint(u, rear_exit, trail), u.speed * dt, dt);
        return;
    }
    // Once clear of the ranks, move to a firing station in front of the shoulder.
    const auto station = wing + forward * desired_depth;
    const auto shoulder = wing + forward * std::clamp(depth, tail, footprint.depth_m * .5f);
    const auto fallback = UnitTerrainLimit(world_, shoulder, shoulder, radii_[UnitIndex(u)]) >= 1.f
                              ? shoulder : trail;
    MoveTowards(u, ScreenWaypoint(u, station, fallback), u.speed * dt, dt);
}
} // namespace battle
