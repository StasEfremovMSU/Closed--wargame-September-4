#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
#include "battle/command/Services.h"
#include "ArmyStaff.h"
#include "CavalryTuning.h"
#include "LocalMelee.h"
#include "FieldCombat.h"
#include <array>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
namespace battle {
namespace {
Vec3 Ground(Vec3 p) {
    return {p.x, p.y, 0.f};
}

const WeaponSpec &FiringWeapon(const UnitSpec &unit) {
    return unit.ranged_attack ? *unit.ranged_attack : unit.weapon;
}

bool RangedReadyThisTick(const World &world, const UnitState &unit, Tick ready) {
    // Exact cooldown completion preserves cadence even for non-divisible periods.
    // Idle retries/first acquisition use a stable phase, never a global volley barrier.
    if (unit.control == ControlMode::PlayerControlled || (ready != 0 && ready == world.tick))
        return true;
    const auto configured = world.scenario.rules.ranged_cohorts;
    const auto cohorts = world.scenario.rules.adaptive_battle && unit.platform == BattlePlatform::FieldGun
        ? std::max(configured, 12u) : configured;
    return world.tick % cohorts == RangedPhase(unit.id, unit.regiment, cohorts);
}

bool CanFireFromRank(const World &world, const UnitState &unit, const RegimentState *regiment) {
    if (regiment && !unit.skirmisher) {
        if (regiment->formation.shape == FormationShape::Testudo)
            return false;
        if (HasOutwardRanks(regiment->formation.shape) && unit.platform == BattlePlatform::None)
            return IsFormationFrontRank(*regiment, unit) && IsAtFormationPost(*regiment, unit) &&
                   regiment->waypoint >= regiment->route.size();
        if (regiment->formation.shape == FormationShape::Rectangle &&
            regiment->rank_layout != RankPriority::Automatic &&
            unit.control != ControlMode::PlayerControlled && !IsAtFormationPost(*regiment, unit))
            return false;
    }
    if (!regiment || !world.scenario.rules.regiment_tactics || !world.scenario.rules.linear_infantry ||
        unit.ranged_weapon != RangedWeapon::Musket || unit.platform != BattlePlatform::None ||
        unit.troop == UnitTroop::Mounted)
        return true;
    if (unit.skirmisher && !unit.skirmisher_returning)
        return true;
    return !unit.skirmisher_returning && IsLinearFiringLine(*regiment) &&
           regiment->waypoint >= regiment->route.size() &&
           unit.slot_index < regiment->formation.columns &&
           Distance(Ground(unit.pose.position), Ground(unit.slot.position)) <=
               .45f * regiment->formation.spacing_m;
}

constexpr float CatapultMinimumRange = 10.f;
constexpr float FieldGunCanisterRange = 28.f;
constexpr float SiegeArcCosine = .70710678f;
constexpr float InfantryArcCosine = .5735764f;

std::vector<Obstacle> ProjectileCover(const World &world) {
    std::vector<Obstacle> cover;
    for (const auto &obstacle : world.scenario.obstacles)
        if (obstacle.blocks_projectiles || obstacle.feature == TerrainFeature::RedoubtRamp ||
            (world.scenario.rules.regiment_tactics && obstacle.feature != TerrainFeature::Barrier))
            cover.push_back(obstacle);
    return cover;
}

float Roll(uint64_t seed, Tick tick, UnitId unit, uint64_t salt = 0) {
    uint64_t x = seed ^ (tick * 0x9e3779b97f4a7c15ull) ^ (unit.value * 0xbf58476d1ce4e5b9ull) ^
                 0x4152524f57ull ^ salt;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= x >> 31;
    return float(x >> 40) * (1.f / 16777216.f);
}
void Emit(World &w, EventKind kind, UnitId source, UnitId target, RegimentId reg, float amount,
          std::string reason, EventId cause = {}, AttackAspect aspect = AttackAspect::Front,
          bool projectile = false) {
    BattleEvent e{{w.next_event_id++}, w.tick + 1, kind, source, target, reg, amount,
                  std::move(reason)};
    e.cause = cause;
    e.aspect = aspect;
    e.projectile = projectile;
    w.events.push_back(std::move(e));
    ++w.combat_events;
}

std::string MeleeReason(std::string_view action, AttackAspect aspect) {
    const char *direction = aspect == AttackAspect::Front ? "front" :
                            aspect == AttackAspect::Flank ? "flank" : "rear";
    return std::string(action) + " (" + direction + ")";
}

float AttackStrength(const RegimentState *regiment) {
    if (!regiment)
        return 1.f;
    return (.8f + .2f * regiment->cohesion) * (regiment->disordered ? .9f : 1.f);
}

float MeleeDamage(const World &world, const UnitState &attacker, const UnitState &defender,
                  const WeaponSpec &weapon, AttackAspect aspect, bool charge, bool pursuit) {
    if (pursuit)
        return defender.health;
    float attack = weapon.damage * AttackStrength(FindRegiment(world, attacker.regiment));
    float guard = defender.armour;
    const auto *regiment = FindRegiment(world, defender.regiment);
    if (regiment) {
        guard *= .75f + .25f * regiment->cohesion;
        if (regiment->disordered || IsDeploying(world, *regiment))
            guard *= .85f;
        if (regiment->formation.shape == FormationShape::Circle && !regiment->panic &&
            !regiment->disordered && aspect == AttackAspect::Front &&
            IsAtFormationPost(*regiment, defender))
            attack *= .87f;
    }
    if (aspect == AttackAspect::Flank) {
        attack *= 1.08f;
        guard *= .8f;
    } else if (aspect == AttackAspect::Rear) {
        attack *= 1.18f;
        guard *= .55f;
    }
    if (charge) {
        if (regiment && regiment->braced && aspect == AttackAspect::Front &&
            (regiment->formation.shape != FormationShape::Square || IsAtFormationPost(*regiment, defender)))
            attack *= .65f;
        else if (attacker.platform == BattlePlatform::IndianElephant)
            attack *= 2.2f;
        else if (attacker.platform == BattlePlatform::AfricanElephant)
            attack *= 1.85f;
        else if (attacker.platform == BattlePlatform::ScythedChariot)
            attack *= 2.1f;
        else {
            const auto *formation = FindRegiment(world, attacker.regiment);
            const float readiness = formation ? (.8f + .2f * formation->cohesion) *
                                                  (1.f - .2f * formation->fatigue) : 1.f;
            float momentum = HorseCavalryTuning(attacker).charge_momentum;
            if (formation && formation->formation.shape == FormationShape::Wedge && formation->cohesion >= .8f)
                momentum *= 1.12f;
            attack *= 1.f + momentum * readiness;
        }
    }
    return std::max(0.f, attack - guard);
}

struct RangedAim {
    const UnitState *target{};
    const UnitState *friendly{};
    bool contested{}, intercepted{}, arced{};
    int cover_modifier{};
    bool bow_loft{};
};

class TacticalArchery {
    struct ShotUnit {
        int32_t team{};
        std::optional<bool> in_contact;
    };

    const World &world_;
    const LocalMelee &contact_;
    std::span<const Obstacle> cover_;
    SpatialIndex index_;
    float max_radius_{};
    std::vector<ShotUnit> units_;
    std::vector<size_t> candidates_, lane_, contact_allies_;
    using TargetRank = std::tuple<bool, int, float, size_t>;
    std::vector<TargetRank> artillery_targets_;

    size_t UnitIndex(const UnitState &unit) const {
        return size_t(&unit - world_.units.data());
    }

    bool ClearShot(Vec3 from, Vec3 to) const {
        // The existing lofting rule clears friendly ranks. It models no terrain
        // trajectory, so a crest must be visible for both shot types.
        return !EvaluateTerrainShot(from, to, cover_).blocked &&
               (!world_.scenario.hill || HillHasLineOfSight(*world_.scenario.hill, from, to));
    }

    float BodyRadius(const UnitState &unit) const {
        return contact_.BodyRadius(unit);
    }

    bool InContact(const UnitState &unit) {
        auto &cached = units_[UnitIndex(unit)].in_contact;
        if (!cached)
            cached = unit.regiment && !contact_.RangedTargetAllowed(unit, true);
        return *cached;
    }

    bool FriendlyContact(const UnitState &friendly, const UnitState &target) const {
        const auto reach = [](const UnitState &unit) {
            return unit.archer ? DaggerWeapon(unit).range : unit.weapon.range;
        };
        const float range = std::min(4.f, std::max(reach(friendly), reach(target)));
        return Distance(friendly.pose.position, target.pose.position) <= range &&
               UnitTerrainLimit(world_, friendly.pose.position, target.pose.position, .002f) >= 1.f;
    }

    const UnitState *NearbyFriendly(const UnitState &shooter, const UnitState &target,
                                    int32_t team) {
        index_.QueryRadiusPayloadsInto(Ground(target.pose.position), 4.f, contact_allies_);
        const UnitState *best = nullptr;
        float best_distance = 4.f;
        for (auto index : contact_allies_) {
            const auto &friendly = world_.units[index];
            if (friendly.id == shooter.id || units_[index].team != team ||
                !FriendlyContact(friendly, target) ||
                Distance(shooter.pose.position, friendly.pose.position) > FiringWeapon(shooter).range ||
                !ClearShot(shooter.pose.position, friendly.pose.position))
                continue;
            const float distance = Distance(friendly.pose.position, target.pose.position);
            if (!best || distance < best_distance) {
                best = &friendly;
                best_distance = distance;
            }
        }
        return best;
    }

    FireLaneMask AssessLane(const UnitState &shooter, Vec3 point, const UnitState *target,
                            int32_t team, RangedAim &aim) {
        const bool siege = IsSiegeEngine(shooter.platform);
        const bool catapult = shooter.platform == BattlePlatform::Catapult;
        const bool tactical_contact = world_.scenario.rules.local_melee || world_.scenario.rules.regiment_tactics;
        const bool contested = target && (!siege || tactical_contact) && InContact(*target);
        if (contested && (siege || !world_.scenario.rules.risky_volley))
            return FireLaneMask::Melee;
        const Vec3 from = Ground(shooter.pose.position);
        const Vec3 line = Ground(point) - from;
        const float length_squared = Dot(line, line);
        const float length = std::sqrt(length_squared);
        const float cosine_half_arc = siege ? SiegeArcCosine : InfantryArcCosine;
        if ((catapult && length < CatapultMinimumRange) ||
            Dot(Forward(shooter.pose.yaw), line) + .00001f < length * cosine_half_arc)
            return FireLaneMask::Terrain;
        const bool field_gun = shooter.platform == BattlePlatform::FieldGun;
        const bool round_shot = field_gun && length > FieldGunCanisterRange;
        const auto terrain = EvaluateTerrainShot(shooter.pose.position, point, cover_, field_gun);
        if (terrain.blocked || (world_.scenario.hill &&
                                !HillHasLineOfSight(*world_.scenario.hill, shooter.pose.position, point)))
            return FireLaneMask::Terrain;
        if (catapult) {
            aim = {target, nullptr, false, false, true, terrain.shooting_modifier};
            return terrain.shooting_modifier ? FireLaneMask::Cover : FireLaneMask::Clear;
        }
        index_.QueryRadiusPayloadsInto(from + line * .5f, length * .5f + max_radius_, lane_);
        const auto *regiment = world_.scenario.rules.mixed_rank_tactics && !siege &&
            shooter.archer && shooter.platform == BattlePlatform::None &&
            shooter.troop != UnitTroop::Mounted && shooter.control == ControlMode::FormationControlled &&
            (shooter.ranged_weapon == RangedWeapon::Bow || shooter.ranged_weapon == RangedWeapon::None)
            ? FindRegiment(world_, shooter.regiment) : nullptr;
        const bool rear_bow = regiment && regiment->formation.shape == FormationShape::Rectangle &&
            !IsFormationFrontRank(*regiment, shooter) && IsAtFormationPost(*regiment, shooter);
        const bool protected_ranks = rear_bow && regiment->rank_layout == RankPriority::MeleeFront;
        const float rank_depth = rear_bow
            ? float(shooter.slot_index / regiment->formation.columns) * regiment->formation.spacing_m : 0.f;
        const bool bow_can_loft = rear_bow && length >= std::max(6.f, rank_depth + 4.f);
        if (protected_ranks && !bow_can_loft)
            return FireLaneMask::Friendly;
        const bool can_arc = !siege && !UsesGunpowder(shooter.ranged_weapon) &&
                             length >= std::max(8.f, .45f * FiringWeapon(shooter).range);
        bool arced = protected_ranks;
        bool bow_loft = protected_ranks;
        const UnitState *intercept = nullptr;
        float first = 1.f;
        for (auto index : lane_) {
            const auto &friendly = world_.units[index];
            const bool ally = units_[index].team == team;
            if (friendly.id == shooter.id || (target && friendly.id == target->id) || (!ally && !siege))
                continue;
            const Vec3 offset = Ground(friendly.pose.position) - from;
            const float t = length_squared > 0.f ? Dot(line, offset) / length_squared : 0.f;
            const Vec3 separation = offset - line * t;
            const float radius = BodyRadius(friendly);
            if (t <= 0.f || t >= 1.f || Dot(separation, separation) >= radius * radius)
                continue;
            if (!ally) {
                if (round_shot && (!tactical_contact || !InContact(friendly)))
                    continue;
                return FireLaneMask::Terrain;
            }
            if (contested && FriendlyContact(friendly, *target)) {
                if (t < first) {
                    intercept = &friendly;
                    first = t;
                }
            } else if (shooter.regiment && friendly.regiment == shooter.regiment &&
                       (can_arc || (bow_can_loft && (!friendly.archer || protected_ranks)))) {
                // A lofted shot can clear ranks from the shooter's own formation at range.
                arced = true;
                bow_loft |= bow_can_loft && (!friendly.archer || protected_ranks);
            } else {
                return FireLaneMask::Friendly;
            }
        }
        aim = {target, intercept, contested, intercept != nullptr, arced, terrain.shooting_modifier};
        aim.bow_loft = bow_loft;
        if (intercept)
            aim.cover_modifier = EvaluateTerrainShot(shooter.pose.position, intercept->pose.position,
                                                     cover_).shooting_modifier;
        else if (contested)
            aim.friendly = NearbyFriendly(shooter, *target, team);
        if (contested)
            return FireLaneMask::Risky;
        return terrain.shooting_modifier ? FireLaneMask::Cover : FireLaneMask::Clear;
    }

    bool SelectableTarget(const UnitState &shooter, const UnitState &target, int32_t team) const {
        if (!target.alive || units_[UnitIndex(target)].team == team)
            return false;
        if (target.skirmisher && !shooter.skirmisher && !IsSiegeEngine(shooter.platform) &&
            !world_.scenario.rules.formed_fire_on_skirmishers)
            return false;
        return target.regiment || !world_.scenario.rules.local_melee ||
               contact_.RangedTargetAllowed(target, shooter.skirmisher);
    }

    RangedAim AimAt(const UnitState &shooter, const UnitState &target, int32_t team) {
        if (!SelectableTarget(shooter, target, team))
            return {};
        RangedAim aim;
        AssessLane(shooter, target.pose.position, &target, team, aim);
        return aim;
    }

  public:
    TacticalArchery(const World &world, const LocalMelee &contact, std::span<const Obstacle> cover)
        : world_(world), contact_(contact), cover_(cover), units_(world.units.size()) {
        std::vector<UnitPosition> positions;
        positions.reserve(world.units.size());
        for (size_t i = 0; i < world.units.size(); ++i) {
            const auto &unit = world.units[i];
            if (!unit.alive)
                continue;
            max_radius_ = std::max(max_radius_, BodyRadius(unit));
            const auto team = UnitTeam(world, unit);
            units_[i].team = team;
            positions.push_back({unit.id, Ground(unit.pose.position), team, i});
        }
        index_.Rebuild(positions);
    }

    FireLaneMask Preview(const UnitState &shooter, Vec3 point, float sample_radius, int32_t team) {
        if (InContact(shooter) && (!IsSiegeEngine(shooter.platform) || world_.scenario.rules.local_melee ||
                                   world_.scenario.rules.regiment_tactics))
            return FireLaneMask::Melee;
        index_.QueryRadiusPayloadsInto(Ground(point), sample_radius, candidates_);
        const UnitState *nearest = nullptr;
        float best = sample_radius;
        for (auto index : candidates_) {
            const auto &unit = world_.units[index];
            if (!SelectableTarget(shooter, unit, team) ||
                Distance(shooter.pose.position, unit.pose.position) > FiringWeapon(shooter).range)
                continue;
            const float distance = Distance(Ground(unit.pose.position), Ground(point));
            if (distance <= best) {
                best = distance;
                nearest = &unit;
            }
        }
        RangedAim aim;
        const Vec3 endpoint = nearest ? nearest->pose.position : point;
        const auto mask = AssessLane(shooter, endpoint, nearest, team, aim);
        return mask == FireLaneMask::Clear && SmokeObscures(world_, shooter.pose.position, endpoint)
            ? FireLaneMask::Cover : mask;
    }

    struct Impact {
        const UnitState *unit{};
        float damage{};
    };

    std::array<Impact, 9> CanisterSpread(const UnitState &shooter, const UnitState &target,
                                         int32_t team) {
        constexpr float radius = 5.5f;
        constexpr float cosine = .955f;
        constexpr float sine = .2966f;
        const auto origin = Ground(shooter.pose.position);
        const auto center = Ground(target.pose.position);
        const auto axis = center - origin;
        const float axis_length = Length(axis);
        const float range = std::min(shooter.weapon.range, FieldGunCanisterRange);
        std::array<Impact, 9> spread{};
        if (axis_length <= .001f)
            return spread;

        const auto direction = axis * (1.f / axis_length);
        index_.QueryRadiusPayloadsInto(center, radius, candidates_);
        std::ranges::sort(candidates_, {}, [&](size_t index) {
            const auto offset = Ground(world_.units[index].pose.position) - center;
            return std::pair{Dot(offset, offset), index};
        });
        size_t count = 0;
        for (const auto index : candidates_) {
            const auto &unit = world_.units[index];
            const auto ray = Ground(unit.pose.position) - origin;
            const float length = Length(ray);
            if (unit.id == target.id || units_[index].team == team || length > range ||
                Dot(axis, ray) < axis_length * length * cosine || !AimAt(shooter, unit, team).target)
                continue;
            const float axial = Dot(direction, ray);
            const float lateral = Length(ray - direction * axial);
            const float fraction = std::clamp(lateral / std::max(.01f, std::min(radius, length * sine)), 0.f, 1.f);
            const float damage = shooter.weapon.damage * std::lerp(.92f, .42f, fraction);
            spread[count++] = {&unit, std::max(0.f, damage - unit.armour)};
            if (count == spread.size())
                break;
        }
        return spread;
    }

    std::array<Impact, 4> RoundShot(const UnitState &shooter, const PendingAttack &attack,
                                    float damage) {
        const auto &trajectory = *attack.round_shot;
        const auto from = Ground(trajectory.origin);
        const auto axis = Ground(trajectory.aim) - from;
        const float distance = Length(axis);
        std::array<Impact, 4> hits{};
        if (distance < .001f)
            return hits;

        constexpr float penetration_distance = 14.f;
        const auto direction = axis * (1.f / distance);
        const float length = std::min(attack.range_m, distance + penetration_distance);
        const auto middle = from + direction * (length * .5f);
        index_.QueryRadiusPayloadsInto(middle, length * .5f + max_radius_, candidates_);
        std::vector<std::pair<float, size_t>> collisions;
        for (const auto index : candidates_) {
            const auto &unit = world_.units[index];
            if (unit.id == shooter.id)
                continue;
            const auto offset = Ground(unit.pose.position) - from;
            const float axial = Dot(offset, direction);
            const auto lateral = offset - direction * axial;
            const float radius = BodyRadius(unit);
            const float intersection = radius * radius - Dot(lateral, lateral);
            if (axial <= 0.f || intersection < 0.f)
                continue;
            const float entry = std::max(0.f, axial - std::sqrt(intersection));
            if (entry <= length)
                collisions.emplace_back(entry, index);
        }
        std::ranges::sort(collisions);

        const auto team = UnitTeam(world_, shooter);
        size_t count = 0;
        size_t limit = hits.size();
        for (const auto &[entry, index] : collisions) {
            const auto &unit = world_.units[index];
            const auto point = from + direction * entry;
            const bool covered = EvaluateTerrainShot(from, point, cover_, true).blocked;
            if (units_[index].team == team || InContact(unit) || covered ||
                (world_.scenario.hill && !HillHasLineOfSight(*world_.scenario.hill, from, point)))
                break;

            if (unit.troop == UnitTroop::Mounted)
                limit = std::min(limit, size_t(3));
            if (count == limit)
                break;
            const float amount = std::max(0.f, damage - unit.armour);
            hits[count++] = {&unit, amount};
            if (count == limit || amount < unit.health || unit.skirmisher || unit.skirmisher_returning)
                break;
            damage *= .96f;
        }
        return hits;
    }

    RangedAim PickManualGun(const UnitState &gun, const UnitState &target, int32_t team) {
        if (InContact(gun) || Distance(gun.pose.position, target.pose.position) > gun.weapon.range)
            return {};
        return AimAt(gun, target, team);
    }

    RangedAim Pick(const UnitState &shooter, const RegimentState *regiment, int32_t team,
                   uint64_t *checks) {
        if (InContact(shooter) && (!IsSiegeEngine(shooter.platform) || world_.scenario.rules.local_melee ||
                                   world_.scenario.rules.regiment_tactics))
            return {};
        const float range = FiringWeapon(shooter).range;
        if (shooter.control == ControlMode::PlayerControlled) {
            const auto *target = FindUnit(world_, shooter.attack_target);
            if (target &&
                Distance(shooter.pose.position, target->pose.position) <= range)
                return AimAt(shooter, *target, team);
            return {};
        }

        UnitId ordered_unit{};
        RegimentId ordered_regiment{};
        if (regiment) {
            ordered_regiment = regiment->ai_target;
            if (regiment->order && IsAttacking(regiment->order->type) &&
                regiment->order->status == OrderStatus::Active) {
                ordered_unit = regiment->order->target_unit;
                ordered_regiment = regiment->order->target_regiment;
            }
        }

        // A safe shot is preferred. Within either class, honor orders, then range and ID.
        RangedAim result;
        TargetRank best;
        const bool field_gun = shooter.platform == BattlePlatform::FieldGun;
        const float cosine_half_arc = IsSiegeEngine(shooter.platform) ? SiegeArcCosine : InfantryArcCosine;
        const auto forward = Forward(shooter.pose.yaw);
        const bool tactical_contact = world_.scenario.rules.local_melee || world_.scenario.rules.regiment_tactics;
        if (field_gun)
            artillery_targets_.clear();
        // World units are ID-sorted, so dense payload order preserves target and blocker ties.
        index_.QueryRadiusPayloadsUnorderedInto(Ground(shooter.pose.position), range, candidates_);
        for (auto index : candidates_) {
            if (checks)
                ++*checks;
            if (units_[index].team == team)
                continue;
            const auto &target = world_.units[index];
            const auto delta = target.pose.position - shooter.pose.position;
            const auto ground = Ground(delta);
            const float angle = Dot(forward, ground) + .00001f;
            if (angle < 0.f)
                continue;
            const float ground_distance = Length(ground);
            const float width = ground_distance * cosine_half_arc;
            if (field_gun ? !(angle >= width) : angle < width)
                continue;
            const float distance = delta.z == 0.f ? ground_distance : Length(delta);
            if (distance > range)
                continue;
            const int priority = target.id == ordered_unit ? 0 :
                                 ordered_regiment && target.regiment == ordered_regiment ? 1 : 2;
            const auto rank = TargetRank{InContact(target), priority, distance, index};
            if (field_gun) {
                if (!tactical_contact || !std::get<0>(rank))
                    artillery_targets_.push_back(rank);
                continue;
            }
            if (result.target && rank >= best)
                continue;
            if (auto aim = AimAt(shooter, target, team); aim.target) {
                result = aim;
                best = rank;
            }
        }
        if (field_gun) {
            // Test terrain and the firing lane in preference order, stopping at the first valid shot.
            std::ranges::sort(artillery_targets_);
            for (const auto &rank : artillery_targets_)
                if (auto aim = AimAt(shooter, world_.units[std::get<3>(rank)], team); aim.target)
                    return aim;
        }
        return result;
    }
};
} // namespace

std::vector<FireSectorSegment> SampleFireSector(const World &world, const RegimentState &regiment,
                                                unsigned rays_per_side, unsigned samples) {
    if (regiment.panic || IsDeploying(world, regiment) || samples == 0)
        return {};
    rays_per_side = std::min(rays_per_side, 24u);
    samples = std::min(samples, 64u);
    const auto front = regiment.pose.position + Forward(regiment.pose.yaw) *
                       (.5f * Footprint(regiment).depth_m);
    const UnitState *shooter = nullptr;
    float closest{};
    for (auto id : regiment.units) {
        const auto *unit = FindUnit(world, id);
        if (!unit || !unit->alive || (!unit->archer && !unit->ranged_attack) || unit->dagger_drawn ||
            FiringWeapon(*unit).range <= 0 || (unit->ammunition && !unit->ammunition_remaining) ||
            !CanFireFromRank(world, *unit, &regiment))
            continue;
        const float distance = Distance(unit->pose.position, front);
        if (!shooter || distance < closest) {
            shooter = unit;
            closest = distance;
        }
    }
    if (!shooter)
        return {};
    const LocalMelee melee(world, 0.f);
    TacticalArchery archery(world, melee, world.scenario.obstacles);
    std::vector<FireSectorSegment> segments;
    segments.reserve((rays_per_side * 2 + 1) * samples);
    const float step = FiringWeapon(*shooter).range / float(samples);
    for (int ray = -int(rays_per_side); ray <= int(rays_per_side); ++ray) {
        const float half_arc = IsSiegeEngine(shooter->platform) ? 45.f : 55.f;
        const float angle = shooter->pose.yaw + (half_arc * Pi / 180.f) * float(ray) / float(std::max(1u, rays_per_side));
        const auto direction = Forward(angle);
        auto previous = shooter->pose.position;
        for (unsigned sample = 1; sample <= samples; ++sample) {
            const auto point = shooter->pose.position + direction * (step * float(sample));
            segments.push_back({previous, point, archery.Preview(*shooter, point,
                std::max(.35f, .5f * step), regiment.team)});
            previous = point;
        }
    }
    return segments;
}
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
namespace {
bool HoldsFormationPosition(const UnitState &unit) {
    return unit.alive && unit.control == ControlMode::FormationControlled &&
           !unit.skirmisher && !unit.skirmisher_returning;
}

bool TestudoShields(const World &world, const UnitState &shooter, const UnitState &defender) {
    const auto *regiment = FindRegiment(world, defender.regiment);
    if (!regiment || regiment->formation.shape != FormationShape::Testudo || regiment->panic ||
        regiment->disordered || !IsAtFormationPost(*regiment, defender) ||
        shooter.platform != BattlePlatform::None || UsesGunpowder(shooter.ranged_weapon) ||
        shooter.ranged_weapon == RangedWeapon::Arcane)
        return false;
    const auto incoming = Ground(shooter.pose.position - defender.pose.position);
    return Dot(Forward(regiment->pose.yaw), incoming) >= -.5f * Length(incoming);
}

class AttackAspects {
  public:
    explicit AttackAspects(const World &world) : world_(world) {}

    AttackAspect Classify(const UnitState &attacker, const UnitState &defender) {
        const auto *regiment = FindRegiment(world_, defender.regiment);
        const bool attached = regiment && HoldsFormationPosition(defender);
        const bool outward = attached && HasOutwardRanks(regiment->formation.shape)
            && !regiment->panic && !regiment->disordered && IsAtFormationPost(*regiment, defender);
        const float yaw = outward ? defender.slot.yaw : attached ? regiment->pose.yaw : defender.pose.yaw;
        const auto forward = Forward(yaw);
        const auto offset = Ground(attacker.pose.position - defender.pose.position);
        constexpr float cosine_sector = .70710678f;
        const float edge = Length(offset) * cosine_sector;
        const float facing = Dot(offset, forward);
        if (facing >= edge)
            return AttackAspect::Front;
        if (!attached || outward)
            return facing <= -edge ? AttackAspect::Rear : AttackAspect::Flank;

        // A diagonal blow against one fighter inside a frontage is still frontal
        // at formation scale. Confirm that the attacker has passed an occupied edge.
        const auto &bounds = Bounds(*regiment);
        const Vec3 relative = attacker.pose.position - regiment->pose.position;
        const float depth = Dot(relative, forward);
        const float lateral = Dot(relative, Right(yaw));
        const float radius = regiment->formation.body_radius_m;
        if (depth > bounds.front + radius)
            return AttackAspect::Front;
        if (facing <= -edge && depth < bounds.rear - radius)
            return AttackAspect::Rear;
        if (lateral >= bounds.left - radius && lateral <= bounds.right + radius)
            return AttackAspect::Front;

        // In a head-on engagement, an overhanging file is still attacking the
        // front while its fighter remains ahead of the defender's front rank.
        // Once the individual passes that line, the regiment's facing no longer
        // determines whether the strike came from the flank.
        if (depth > bounds.front && HoldsFormationPosition(attacker)) {
            const auto *attacking = FindRegiment(world_, attacker.regiment);
            if (attacking && Dot(Forward(attacking->pose.yaw), forward) <= -cosine_sector)
                return AttackAspect::Front;
        }
        return AttackAspect::Flank;
    }

  private:
    struct Frontage {
        float front, rear, left, right;
    };

    const Frontage &Bounds(const RegimentState &regiment) {
        if (frontages_.empty())
            frontages_.resize(world_.regiments.size());
        auto &cached = frontages_[&regiment - world_.regiments.data()];
        if (!cached) {
            const auto forward = Forward(regiment.pose.yaw);
            const auto right = Right(regiment.pose.yaw);
            for (const auto id : regiment.units) {
                const auto *unit = FindUnit(world_, id);
                if (!unit || !HoldsFormationPosition(*unit))
                    continue;
                const auto offset = unit->pose.position - regiment.pose.position;
                const float depth = Dot(offset, forward);
                const float lateral = Dot(offset, right);
                if (!cached)
                    cached = Frontage{depth, depth, lateral, lateral};
                else {
                    cached->front = std::max(cached->front, depth);
                    cached->rear = std::min(cached->rear, depth);
                    cached->left = std::min(cached->left, lateral);
                    cached->right = std::max(cached->right, lateral);
                }
            }
            if (!cached)
                cached = Frontage{};
        }
        return *cached;
    }

    const World &world_;
    std::vector<std::optional<Frontage>> frontages_;
};
} // namespace

AttackAspect ClassifyAttackAspect(const World &world, const UnitState &attacker,
                                  const UnitState &defender) {
    return AttackAspects(world).Classify(attacker, defender);
}
void BuildCombatEvents(const World &w, std::vector<BattleEvent> &out, uint64_t *checks) {
    std::optional<LocalMelee> contact;
    if (w.scenario.rules.local_melee)
        contact.emplace(w, 0.f);
    BuildCombatEvents(w, out, checks, contact ? &*contact : nullptr);
}
void BuildCombatEvents(const World &w, std::vector<BattleEvent> &out, uint64_t *checks,
                       const LocalMelee *contact, std::span<const UnitId> moving_siege) {
    BuildCombatEvents(w, out, checks, contact, moving_siege, nullptr);
}
void BuildCombatEvents(const World &w, std::vector<BattleEvent> &out, uint64_t *checks,
                       const LocalMelee *contact, std::span<const UnitId> moving_siege,
                       const Command *field_gun_shot) {
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
    const bool tactics = w.scenario.rules.regiment_tactics;
    AttackAspects aspects(w);
    const auto *hill = w.scenario.hill ? &*w.scenario.hill : nullptr;
    struct Shooter {
        size_t index;
        const RegimentState *regiment;
        int32_t team;
        bool projectile;
        bool manual = false;
    };
    const auto *officer = FindUnit(w, w.controlled);
    const RegimentId battery = officer && officer->platform == BattlePlatform::None &&
                                        CanCommandRegiment(w, officer->id, officer->regiment)
                                    ? officer->regiment : RegimentId{};
    const auto *manual_target = battery && field_gun_shot && field_gun_shot->unit == w.controlled
                                    ? FindUnit(w, field_gun_shot->target) : nullptr;
    std::vector<Shooter> shooters;
    std::optional<Shooter> manual_shooter;
    float closest_gun = FieldGunOfficerReachM;
    for (size_t i = 0; i < w.units.size(); ++i) {
        const auto &u = w.units[i];
        if (!u.alive)
            continue;
        // A closed rank can prevent the optional throw while still allowing a melee strike.
        const auto *javelin_regiment = u.ranged_attack ? FindRegiment(w, u.regiment) : nullptr;
        const bool alternate_ready = u.ranged_attack && u.ammunition_remaining &&
            u.ranged_ready_tick <= w.tick && RangedReadyThisTick(w, u, u.ranged_ready_tick) &&
            CanFireFromRank(w, u, javelin_regiment) &&
            (!contact || !u.regiment || contact->RangedTargetAllowed(u, false));
        if ((!alternate_ready && u.ready_tick > w.tick &&
             (!contact || !u.archer || u.dagger_ready_tick > w.tick)) ||
            (contact && !contact->CanAcquire(u)))
            continue;
        const auto *r = u.ranged_attack ? javelin_regiment : FindRegiment(w, u.regiment);
        if (r && (r->panic || IsDeploying(w, *r)))
            continue;
        const bool dagger = contact && contact->DaggerDrawn(u);
        if (IsSiegeEngine(u.platform) && (dagger || std::ranges::binary_search(moving_siege, u.id)))
            continue;
        const bool projectile = (u.archer && !dagger) || alternate_ready;
        const Tick ready = alternate_ready ? u.ranged_ready_tick : dagger ? u.dagger_ready_tick : u.ready_tick;
        if (ready > w.tick ||
            (projectile && ((u.ammunition && !u.ammunition_remaining) || !CanFireFromRank(w, u, r))))
            continue;
        if (battery && u.regiment == battery && u.platform == BattlePlatform::FieldGun) {
            if (manual_target && projectile) {
                const float officer_distance = Distance(officer->pose.position, u.pose.position);
                const auto target_delta = Ground(manual_target->pose.position) - Ground(u.pose.position);
                const float target_distance = Length(target_delta);
                if (officer_distance <= closest_gun && target_distance <= u.weapon.range &&
                    Dot(Forward(u.pose.yaw), target_delta) >= target_distance * SiegeArcCosine &&
                    (!manual_shooter || officer_distance < closest_gun)) {
                    closest_gun = officer_distance;
                    manual_shooter = Shooter{i, r, r->team, true, true};
                }
            }
            continue;
        }
        if (projectile && !alternate_ready && !RangedReadyThisTick(w, u, ready))
            continue;
        shooters.push_back({i, r, r ? r->team : u.independent_team, projectile});
    }
    if (manual_shooter)
        shooters.push_back(*manual_shooter);
    if (shooters.empty())
        return;
    const auto projectile_cover = ProjectileCover(w);
    const bool need_index = std::any_of(shooters.begin(), shooters.end(), [&](auto s) {
        const auto &u = w.units[s.index];
        return u.control != ControlMode::PlayerControlled &&
               ((s.projectile && !tactics && !IsSiegeEngine(u.platform)) ||
                (!contact && (!s.projectile || u.ranged_attack)));
    });
    const bool need_screen_index = !tactics && contact && !w.scenario.rules.formed_fire_on_skirmishers &&
        std::any_of(shooters.begin(), shooters.end(), [&](auto s) { return s.projectile && w.units[s.index].skirmisher; });
    std::optional<LocalMelee> ranged_contact;
    std::optional<TacticalArchery> archery;
    if (std::ranges::any_of(shooters, [&](auto s) {
            return s.projectile && (tactics || IsSiegeEngine(w.units[s.index].platform));
        })) {
        if (!contact)
            ranged_contact.emplace(w, 0.f);
        archery.emplace(w, contact ? *contact : *ranged_contact, projectile_cover);
    }
    SpatialIndex index, screen_index;
    if (need_index) {
        std::vector<UnitPosition> positions, screen_positions;
        positions.reserve(w.units.size());
        if (need_screen_index)
            screen_positions.reserve(w.units.size());
        for (size_t i = 0; i < w.units.size(); ++i) {
            const auto &u = w.units[i];
            if (!u.alive)
                continue;
            const UnitPosition point{u.id, u.pose.position, UnitTeam(w, u), i};
            if (!contact || contact->RangedTargetAllowed(u, false))
                positions.push_back(point);
            if (need_screen_index && contact->RangedTargetAllowed(u, true))
                screen_positions.push_back(point);
        }
        index.Rebuild(positions);
        if (need_screen_index)
            screen_index.Rebuild(screen_positions);
    }
    for (const auto &shooter : shooters) {
        const auto &u = w.units[shooter.index];
        const bool siege = IsSiegeEngine(u.platform);
        const auto *regiment = tactics || siege ? shooter.regiment : nullptr;
        RangedAim aim;
        const auto pick_target = [&](bool projectile, float weapon_range) -> const UnitState * {
            if (archery && projectile && (tactics || siege)) {
                aim = shooter.manual ? archery->PickManualGun(u, *manual_target, shooter.team) :
                                       archery->Pick(u, regiment, shooter.team, checks);
                return aim.target;
            }
            if (u.control == ControlMode::PlayerControlled) {
                const auto *target = FindUnit(w, u.attack_target);
                const float range = target && contact && !projectile
                    ? contact->MeleeReach(u, *target, weapon_range) : weapon_range;
                if (target && target->alive && UnitTeam(w, *target) != shooter.team &&
                    Distance(u.pose.position, target->pose.position) <= range &&
                    (!projectile || projectile_cover.empty() ||
                     command::HasLineOfSight(u.pose.position, target->pose.position, projectile_cover)) &&
                    (!projectile || !hill ||
                     HillHasLineOfSight(*hill, u.pose.position, target->pose.position)) &&
                    (!contact || (projectile ? contact->RangedTargetAllowed(*target, u.skirmisher)
                                             : contact->CanStrike(u, *target, weapon_range))))
                    return target;
                return nullptr;
            }
            if (contact && !projectile)
                return contact->StrikeTarget(u, shooter.team);
            const auto &targets = u.skirmisher && need_screen_index ? screen_index : index;
            const auto obstacles = projectile ? std::span<const Obstacle>(projectile_cover)
                                              : std::span<const Obstacle>(w.scenario.obstacles);
            if (auto nearest = targets.FindNearestEnemy(u.pose.position, weapon_range, shooter.team,
                                                        checks, obstacles, projectile ? hill : nullptr))
                return &w.units[nearest->payload_index];
            return nullptr;
        };
        bool projectile = shooter.projectile;
        const UnitState *target = nullptr;
        if (projectile && u.ranged_attack) {
            target = pick_target(false, u.weapon.range);
            if (target && !contact && UnitTerrainLimit(w, u.pose.position, target->pose.position, .002f) < 1.f)
                target = nullptr;
            if (target) {
                if (u.ready_tick > w.tick)
                    continue;
                projectile = false;
            }
        }
        const auto weapon = projectile ? FiringWeapon(u) : u.archer ? DaggerWeapon(u) : u.weapon;
        if (!target)
            target = pick_target(projectile, weapon.range);
        if (!target || (!projectile && !contact &&
                        UnitTerrainLimit(w, u.pose.position, target->pose.position, .002f) < 1.f))
            continue;
        float distance = Distance(u.pose.position, target->pose.position);
        float accuracy = weapon.base_accuracy * AttackStrength(regiment);
        float damage = weapon.damage;
        if (u.platform == BattlePlatform::Ballista) {
            const unsigned band = distance <= 10.f ? 0 : distance <= 20.f ? 1 : 2;
            constexpr float accuracy_scale[]{1.f, 10.f / 14.f, 8.f / 14.f};
            constexpr float damage_scale[]{1.f, 5.f / 7.f, 3.f / 7.f};
            accuracy *= accuracy_scale[band];
            damage *= damage_scale[band];
        }
        if (aim.contested)
            accuracy *= .8f;
        if (aim.arced)
            accuracy *= .8f;
        if (aim.bow_loft)
            damage *= .75f;
        if (tactics && target->skirmisher)
            accuracy *= .82f;
        if (projectile && (tactics || siege)) {
            accuracy = std::max(0.f, accuracy + .1f * aim.cover_modifier);
            if (SmokeObscures(w, u.pose.position, target->pose.position))
                accuracy *= .55f;
        }
        const float hit_chance = siege ? accuracy * u.archer_skill :
            HitProbability(accuracy, u.archer_skill, distance, weapon.range);
        const bool hit = !projectile || Roll(w.seed, w.tick, u.id) < hit_chance;
        constexpr uint64_t friendly_fire_roll = 0x564f4c4c4559ull;
        if (hit && aim.friendly &&
            (aim.intercepted || Roll(w.seed, w.tick, u.id, friendly_fire_roll) < .35f)) {
            target = aim.friendly;
            distance = Distance(u.pose.position, target->pose.position);
        }
        BattleEvent e;
        e.id = {w.next_event_id + out.size()};
        e.tick = w.tick;
        e.kind = EventKind::AttackLaunched;
        e.source = u.id;
        e.target = target->id;
        e.regiment = u.regiment;
        e.amount = std::max(0.f, damage - target->armour);
        e.projectile = projectile;
        e.hit = hit;
        e.high_arc = aim.arced;
        const bool field_gun = e.projectile && u.platform == BattlePlatform::FieldGun;
        const bool canister = field_gun && distance <= FieldGunCanisterRange;
        const bool pursuit = tactics && !e.projectile && regiment &&
                             regiment->pursuit_target == target->regiment &&
                             IsFormationFrontRank(*regiment, u);
        const bool charge = tactics && !e.projectile && regiment && !pursuit &&
                            ChargeReady(w, *regiment, u, *target);
        constexpr Tick OfficerStrikeWindup = 9;
        const Tick melee_windup = target->id == w.controlled &&
            target->control == ControlMode::PlayerControlled && !charge
            ? std::max(weapon.windup_ticks, OfficerStrikeWindup) : weapon.windup_ticks;
        // Canister resolves on the same simulation step, with the firing geometry from launch.
        const Tick duration = canister ? 1 :
            e.projectile ? Tick(std::max(
                               1.0, std::ceil(double(distance) / weapon.projectile_speed_mps / .05)))
                         : melee_windup;
        e.due_tick = w.tick + duration;
        const std::string_view ranged_reason = canister ? FieldGunCanisterReason :
            field_gun ? FieldGunRoundShotReason : aim.contested ? "risky volley" :
            aim.arced ? "ranged attack (high arc)" : "ranged attack";
        e.reason = e.projectile ? ranged_reason : (u.archer ? "dagger attack" : "melee attack");
        if (tactics || (contact && !e.projectile))
            e.aspect = aspects.Classify(u, *target);
        if (tactics) {
            if (!e.projectile) {
                if (charge)
                    e.reason = "cavalry impact";
                else if (pursuit)
                    e.reason = "pursuit";
                e.amount = MeleeDamage(w, u, *target, weapon, e.aspect, charge, pursuit);
                e.reason = MeleeReason(e.reason, e.aspect);
            }
        }
        out.push_back(std::move(e));
    }
}
void CommitCombatEvents(World &w, std::span<const BattleEvent> launches) {
    std::optional<LocalMelee> contact;
    if (w.scenario.rules.local_melee)
        contact.emplace(w, 0.f);
    CommitCombatEvents(w, launches, contact ? &*contact : nullptr);
}
void CommitCombatEvents(World &w, std::span<const BattleEvent> launches, const LocalMelee *contact) {
    std::vector<BattleEvent> ordered(launches.begin(), launches.end());
    std::sort(ordered.begin(), ordered.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::set<RegimentId> charges;
    std::vector<UnitId> smoke_sources;
    std::vector<EventId> canister_launches;
    for (const auto &e : ordered) {
        if (e.kind != EventKind::AttackLaunched || !e.id || e.id.value <= w.last_launch_id)
            continue;
        auto *u = FindUnit(w, e.source);
        auto *t = FindUnit(w, e.target);
        if (!u || !t)
            continue;
        w.last_launch_id = e.id.value;
        w.next_event_id = std::max(w.next_event_id, e.id.value + 1);
        if (e.projectile && u->ammunition) {
            if (!u->ammunition_remaining)
                continue;
            --u->ammunition_remaining;
        }
        const bool dagger = contact && u->archer && !e.projectile;
        const auto weapon = e.projectile ? FiringWeapon(*u) : dagger ? DaggerWeapon(*u) : u->weapon;
        auto &cooldown = e.projectile && u->ranged_attack ? u->ranged_ready_tick
                         : dagger ? u->dagger_ready_tick : u->ready_tick;
        cooldown = w.tick + weapon.cooldown_ticks;
        const bool charge = e.reason.starts_with("cavalry impact");
        const bool pursuit = e.reason.starts_with("pursuit");
        auto &attack = w.attacks.emplace_back(PendingAttack{e.id, e.source, e.target, e.due_tick,
            e.amount, e.hit, weapon.range, e.projectile, charge, pursuit, {}, {}});
        if (e.hit && e.projectile && u->platform == BattlePlatform::FieldGun &&
            e.reason == FieldGunRoundShotReason) {
            attack.round_shot = RoundShotTrajectory{u->pose.position, t->pose.position};
            attack.damage = weapon.damage;
        }
        if (w.scenario.rules.controlled_arrow_aim && e.projectile && t->id == w.controlled &&
            u->platform == BattlePlatform::None &&
            UnitTeam(w, *u) != UnitTeam(w, *t) &&
            (u->ranged_weapon == RangedWeapon::Bow || u->ranged_weapon == RangedWeapon::Crossbow ||
             (u->archer && u->ranged_weapon == RangedWeapon::None)))
            attack.officer_arrow = OfficerArrowTrajectory{u->pose.position, t->pose.position};
        if (e.projectile && u->platform == BattlePlatform::FieldGun && e.due_tick == w.tick + 1 &&
            e.reason == FieldGunCanisterReason)
            canister_launches.push_back(e.id);
        if (w.scenario.rules.regiment_tactics && e.projectile && UnitTeam(w, *u) != UnitTeam(w, *t))
            if (auto *recipient = FindRegiment(w, t->regiment))
                recipient->last_hostile_shot_tick = w.tick + 1;
        if (charge)
            charges.insert(u->regiment);
        if (e.projectile && (UsesGunpowder(u->ranged_weapon) || u->platform == BattlePlatform::FieldGun))
            smoke_sources.push_back(u->id);
        w.events.push_back(e);
        ++w.combat_events;
    }
    for (const auto id : charges) {
        if (auto *regiment = FindRegiment(w, id))
            RegisterCharge(w, *regiment);
    }
    for (const auto id : smoke_sources) {
        const auto &unit = *FindUnit(w, id);
        constexpr float Radius = 2.5f;
        constexpr Tick Duration = 45; // 2.25 seconds at 20 Hz.
        auto overlapping = std::find_if(w.smoke.begin(), w.smoke.end(), [&](const auto &cloud) {
            return Distance(cloud.center, unit.pose.position) < Radius * .5f;
        });
        if (overlapping == w.smoke.end())
            w.smoke.push_back({unit.pose.position, Radius, w.tick + Duration});
        else
            overlapping->expires_tick = w.tick + Duration;
        Emit(w, EventKind::SmokeCreated, id, {}, unit.regiment, Radius, "gunpowder smoke");
    }
    std::sort(w.attacks.begin(), w.attacks.end(), [](auto &a, auto &b) {
        return a.due_tick != b.due_tick ? a.due_tick < b.due_tick : a.id < b.id;
    });
    struct Damage {
        double amount{};
        UnitId source{};
        EventId cause{};
        AttackAspect aspect{AttackAspect::Front};
        bool projectile{};
    };
    const bool tactics = w.scenario.rules.regiment_tactics;
    AttackAspects aspects(w);
    std::map<uint64_t, Damage> damage;
    const auto record_hit = [&](const PendingAttack &attack, const UnitState &target, float amount,
                                std::string reason, AttackAspect aspect, bool projectile) {
        Emit(w, EventKind::Hit, attack.source, target.id, target.regiment, amount,
             std::move(reason), attack.id, aspect, projectile);
        auto &d = damage[target.id.value];
        d.amount += amount;
        if (!d.cause && d.amount >= target.health && amount > 0.f) {
            d.source = attack.source;
            d.cause = attack.id;
            d.aspect = aspect;
            d.projectile = projectile;
        }
    };
    std::vector<Obstacle> artillery_cover;
    std::optional<LocalMelee> artillery_contact;
    std::optional<TacticalArchery> artillery_aim;
    const auto artillery = [&]() -> TacticalArchery & {
        if (!artillery_aim) {
            artillery_cover = ProjectileCover(w);
            if (!contact)
                artillery_contact.emplace(w, 0.f);
            artillery_aim.emplace(w, contact ? *contact : *artillery_contact, artillery_cover);
        }
        return *artillery_aim;
    };
    std::vector<PendingAttack> pending;
    for (const auto &a : w.attacks) {
        if (a.due_tick > w.tick + 1) {
            pending.push_back(a);
            continue;
        }
        auto *t = FindUnit(w, a.target);
        auto *s = FindUnit(w, a.source);
        if (a.hit && a.round_shot && t && s) {
            bool connected = false;
            for (const auto &impact : artillery().RoundShot(*s, a, a.damage)) {
                if (!impact.unit)
                    break;
                connected = true;
                record_hit(a, *impact.unit, impact.damage, std::string(FieldGunRoundShotReason),
                           AttackAspect::Front, true);
            }
            if (!connected)
                Emit(w, EventKind::Miss, a.source, a.target, {}, 0, "round shot did not hit",
                     a.id, AttackAspect::Front, true);
            continue;
        }
        bool hit = a.hit && t && t->alive && s;
        if (hit && a.officer_arrow) {
            const Vec3 aim = a.officer_arrow->aim;
            const Vec3 feet = t->pose.position;
            const Vec3 horizontal = Ground(feet - aim);
            const bool mounted = t->troop == UnitTroop::Mounted;
            const float radius = mounted ? .65f : .4f;
            bool cleared = Dot(horizontal, horizontal) > radius * radius;
            if (!cleared) {
                // The rendered arrow ends 102 cm above the launch-time target footing.
                constexpr float ArrowAimHeightM = 1.02f;
                const auto &hill = w.scenario.hill;
                const float impact_height = aim.z + ArrowAimHeightM + (hill ? HillHeight(*hill, aim) : 0.f);
                const float feet_height = feet.z + (hill ? HillHeight(*hill, feet) : 0.f);
                const float body_height = mounted ? 2.5f : 2.f;
                cleared = impact_height < feet_height || impact_height > feet_height + body_height;
            }
            if (cleared) {
                Emit(w, EventKind::Miss, a.source, a.target, t->regiment, 0,
                     std::string(OfficerArrowDodgeReason), a.id, AttackAspect::Front, true);
                continue;
            }
        }
        if (hit && !a.projectile) {
            const float reach = contact ? contact->MeleeReach(*s, *t, a.range_m) : a.range_m;
            hit = s->alive && Distance(s->pose.position, t->pose.position) <= reach + .0001f &&
                  (contact ? contact->CanStrike(*s, *t, a.range_m) :
                             UnitTerrainLimit(w, s->pose.position, t->pose.position, .002f) >= 1.f);
        }
        if (hit && !a.projectile && !a.charge && w.tick + 1 < t->parry_until) {
            constexpr float ParryArcCosine = .57357644f; // 55 degrees on each side.
            const Vec3 toward_attacker = Ground(s->pose.position - t->pose.position);
            const float distance = Length(toward_attacker);
            if (distance > .0001f &&
                Dot(Forward(t->parry_yaw), toward_attacker) >= distance * ParryArcCosine) {
                t->parry_until = 0;
                t->parry_yaw = 0.f;
                Tick &melee_ready = t->archer ? t->dagger_ready_tick : t->ready_tick;
                melee_ready = std::min(melee_ready, w.tick + 1);
                Emit(w, EventKind::Parried, t->id, s->id, t->regiment, 0,
                     "melee strike parried", a.id);
                continue;
            }
        }
        const bool canister = std::binary_search(canister_launches.begin(), canister_launches.end(), a.id);
        if (hit && a.projectile && !canister && UsesGunpowder(s->ranged_weapon) &&
            Roll(w.seed, w.tick, s->id, a.id.value ^ 0x2d49u) < .25f) {
            if (const auto *guard = command::BodyguardForProjectile(w, *s, *t)) {
                record_hit(a, *guard, a.damage, "musket ball struck army escort", AttackAspect::Front, true);
                continue;
            }
        }
        if (hit) {
            float amount = a.damage;
            AttackAspect aspect = AttackAspect::Front;
            std::string reason = "attack resolved";
            if (tactics || (contact && !a.projectile))
                aspect = aspects.Classify(*s, *t);
            if (tactics) {
                if (!a.projectile) {
                    const auto weapon = s->archer ? DaggerWeapon(*s) : s->weapon;
                    const auto *defending = FindRegiment(w, t->regiment);
                    const bool pursuit = a.pursuit && defending && defending->panic;
                    amount = MeleeDamage(w, *s, *t, weapon, aspect, a.charge, pursuit);
                    reason = MeleeReason(a.charge ? "cavalry impact" :
                                         pursuit ? "pursuit" : "melee hit", aspect);
                    if (a.charge) {
                        Emit(w, EventKind::CavalryImpact, a.source, a.target, t->regiment, amount,
                             reason, a.id, aspect);
                        if (contact)
                            contact->QueueCavalryShove(*s, *t, amount, aspect);
                    }
                } else {
                    if (TestudoShields(w, *s, *t)) {
                        amount *= .4f;
                        reason = "testudo shield cover";
                    }
                    if (UnitTeam(w, *s) == UnitTeam(w, *t))
                        reason = "risky volley: friendly fire";
                }
            }
            if (canister) {
                record_hit(a, *t, amount, std::string(FieldGunCanisterReason), AttackAspect::Front, true);
                for (const auto &impact : artillery().CanisterSpread(*s, *t, UnitTeam(w, *s)))
                    if (impact.unit)
                        record_hit(a, *impact.unit, impact.damage, std::string(FieldGunCanisterReason),
                                   AttackAspect::Front, true);
            } else {
                record_hit(a, *t, amount, std::move(reason), aspect, tactics && a.projectile);
            }
        } else
            Emit(w, EventKind::Miss, a.source, a.target, {}, 0, "miss or unavailable target",
                 a.id, AttackAspect::Front, tactics && a.projectile);
    }
    w.attacks = std::move(pending);
    std::set<RegimentId> close_ranks;
    for (auto &[id, d] : damage) {
        auto *u = FindUnit(w, {id});
        if (!u || !u->alive)
            continue;
        u->health = float(std::max(0.0, double(u->health) - d.amount));
        auto *r = FindRegiment(w, u->regiment);
        if (r && d.amount > 0)
            r->last_damage_tick = w.tick + 1;
        if (u->health <= 0) {
            u->alive = false;
            u->control = ControlMode::Dead;
            u->dagger_drawn = u->skirmisher = u->skirmisher_returning = u->bypassing = false;
            u->contact_travel = {};
            if (u->jump_started_tick || u->parry_ready_tick)
                ClearPersonalActions(*u);
            if (r) {
                std::erase(r->units, u->id);
                if (!tactics)
                    close_ranks.insert(r->id);
                if (r->units.empty() && r->following_officer)
                    ClearRegimentFollowing(w, r->following_officer);
            }
            if (w.controlled == u->id) {
                ClearRegimentFollowing(w, u->id);
                w.controlled = {};
                Emit(w, EventKind::ControlChanged, {}, u->id, u->regiment, 0,
                     "controlled unit died");
            }
            Emit(w, EventKind::Died, d.source, u->id, u->regiment, 0, "unit died", d.cause,
                 d.aspect, d.projectile);
        }
    }
    if (contact)
        contact->ApplyShoves(w);
    for (auto id : close_ranks) {
        auto *r = FindRegiment(w, id);
        if (r->units.empty() || (contact && contact->Engaged(*r)))
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
        const auto effects = GetStaffEffects(w, r);
        uint64_t count = r.units.size();
        if (!r.panic && r.initial_count &&
            double(count) / double(r.initial_count) < effects.panic_fraction &&
            (r.panic_loss_count == 0 || count < r.panic_loss_count)) {
            r.panic = true;
            command::WakeCommandForPanicChange(w, r.id);
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
            if (u.alive && UnitTeam(w, u) != r.team &&
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
        if (r.calm_counter >= effects.calm_ticks) {
            r.panic = false;
            command::WakeCommandForPanicChange(w, r.id);
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
