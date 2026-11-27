#include "FieldCombat.h"
#include "CavalryTuning.h"
#include "LocalMelee.h"
#include "battle/Formation.h"
#include "battle/Officers.h"

namespace battle {
namespace {
constexpr Tick BraceTicks = 12; // .6 seconds of steady spears.
// This wave is shorter than the personal parry recovery. A rider who breaks
// momentum to parry cannot rejoin the same wave with the shortened cooldown.
constexpr Tick ImpactTicks = 12;
constexpr Tick ReformTicks = 32;
constexpr Tick PursuitCheckTicks = 40; // One discipline check per two-second maneuver.

bool IsMountedRegiment(const World &world, const RegimentState &regiment) {
    const auto *end = world.units.data() + world.units.size();
    const UnitState *unit = nullptr;
    size_t mounted = 0;
    const size_t half = regiment.units.size() / 2;
    for (auto id : regiment.units) {
        // The usual roster occupies a contiguous range of the world's ID-sorted units.
        // Irregular rosters and gaps left by casualties use the same exact lookup as before.
        if (unit && unit + 1 != end && (unit + 1)->id == id)
            ++unit;
        else
            unit = FindUnit(world, id);
        if (IsElephant(unit->platform) || unit->platform == BattlePlatform::ScythedChariot)
            return true;
        if (unit->troop == UnitTroop::Mounted && ++mounted > half)
            return true;
    }
    return false;
}

void UpdateCavalry(World &world, RegimentState &regiment) {
    auto &phase = regiment.cavalry_phase;
    const auto tick = world.tick;
    if (regiment.panic || regiment.units.empty()) {
        phase = CavalryPhase::None;
        regiment.charge_runup_m = 0.f;
        regiment.cavalry_target = {};
        return;
    }
    if (phase == CavalryPhase::Impact && tick >= regiment.cavalry_phase_until) {
        phase = CavalryPhase::Reform;
        regiment.cavalry_phase_until = tick + ReformTicks;
        regiment.charge_runup_m = 0.f;
    }
    if (phase == CavalryPhase::Reform) {
        if (tick < regiment.cavalry_phase_until || regiment.melee_engaged)
            return;
        phase = CavalryPhase::None;
        regiment.cavalry_target = {};
        regiment.charge_runup_m = 0.f;
    }
    if (phase == CavalryPhase::Impact)
        return;
    const auto target = regiment.pursuit_target ? regiment.pursuit_target : regiment.ai_target;
    if (!IsAttacking(regiment.ai_action) || !target || regiment.disordered ||
        IsDeploying(world, regiment) || !IsMountedRegiment(world, regiment)) {
        phase = CavalryPhase::None;
        regiment.cavalry_target = {};
        regiment.charge_runup_m = 0.f;
    } else if (phase == CavalryPhase::None || regiment.cavalry_target != target) {
        phase = CavalryPhase::Approach;
        regiment.cavalry_target = target;
        regiment.charge_runup_m = 0.f;
    }
}

void UpdateBrace(World &world, RegimentState &regiment) {
    const bool square = regiment.formation.shape == FormationShape::Square;
    const bool solid_square = square && IsHolding(regiment.ai_action) && !regiment.following_officer &&
        regiment.waypoint >= regiment.route.size() && regiment.units.size() >= 4 &&
        (regiment.braced || std::ranges::count_if(regiment.units, [&](UnitId id) {
            return IsAtFormationPost(regiment, *FindUnit(world, id));
        }) >= (int64_t(regiment.units.size()) * 3 + 3) / 4);
    const bool spear_line = !square && regiment.ai_action == OrderType::Brace &&
        std::any_of(regiment.units.begin(), regiment.units.end(), [&](UnitId id) {
            return FindUnit(world, id)->troop == UnitTroop::Spearman;
        });
    const bool ready_to_prepare = (solid_square || spear_line) && !regiment.panic &&
        !regiment.disordered && !IsDeploying(world, regiment);
    if (!ready_to_prepare) {
        regiment.braced = false;
        regiment.brace_ready_tick = 0;
    } else {
        if (!regiment.brace_ready_tick && !regiment.melee_engaged)
            regiment.brace_ready_tick = world.tick + BraceTicks;
        regiment.braced = regiment.brace_ready_tick && regiment.brace_ready_tick <= world.tick;
    }
}

void UpdatePursuit(World &world, RegimentState &regiment) {
    if (regiment.units.empty() || regiment.panic || regiment.disordered ||
        (!IsAttacking(regiment.ai_action) && !regiment.pursuit_target)) {
        regiment.pursuit_target = {};
        return;
    }
    const bool authorized = regiment.order && regiment.order->status == OrderStatus::Active &&
                            regiment.order->type == OrderType::Pursue;
    const auto target_id = authorized && regiment.ai_action == OrderType::Pursue ? regiment.ai_target :
        regiment.pursuit_target ? regiment.pursuit_target : regiment.melee_opponent;
    const auto *target = FindRegiment(world, target_id);
    if (!target || !target->panic || target->units.empty() ||
        (regiment.melee_engaged && regiment.melee_opponent != target->id)) {
        regiment.pursuit_target = {};
        return;
    }
    if (authorized && regiment.ai_action == OrderType::Pursue && regiment.ai_target == target->id) {
        regiment.pursuit_target = target->id;
        return;
    }
    if (world.tick < regiment.pursuit_check_tick)
        return;
    const float leadership = GetStaffEffects(world, regiment).leadership;
    if (!DisciplineTest(world, regiment, leadership, 0x50555253ull))
        regiment.pursuit_target = target->id;
    else {
        regiment.pursuit_target = {};
        if (!regiment.melee_engaged)
            regiment.melee_opponent = {};
    }
    regiment.decision_tick = 0;
    regiment.pursuit_check_tick = world.tick + PursuitCheckTicks;
}
} // namespace

bool DisciplineTest(const World &world, const RegimentState &regiment, float probability,
                    uint64_t salt) {
    // Tabletop d20 with deterministic per-incident rolls for replays.
    uint64_t roll = world.seed ^ (world.tick * 0x9e3779b97f4a7c15ull) ^
                    (regiment.id.value * 0xbf58476d1ce4e5b9ull) ^ salt;
    roll = (roll ^ (roll >> 30)) * 0xbf58476d1ce4e5b9ull;
    roll = (roll ^ (roll >> 27)) * 0x94d049bb133111ebull;
    roll ^= roll >> 31;
    return float(1 + roll % 20) <= 20.f * std::clamp(probability, 0.f, 1.f);
}

void UpdateFieldPosture(World &world) {
    std::erase_if(world.smoke, [&](const auto &cloud) { return cloud.expires_tick <= world.tick; });
    if (!world.scenario.rules.regiment_tactics)
        return;
    for (auto &regiment : world.regiments) {
        UpdatePursuit(world, regiment);
        UpdateBrace(world, regiment);
        UpdateCavalry(world, regiment);
    }
}

bool ChargeReady(const World &world, const RegimentState &regiment, const UnitState &attacker,
                 const UnitState &defender) {
    const bool chariot = attacker.platform == BattlePlatform::ScythedChariot;
    const bool elephant = IsElephant(attacker.platform);
    const float runup = chariot ? 6.f : elephant ? 2.5f : HorseCavalryTuning(attacker).charge_runup_m;
    const bool impact = regiment.cavalry_phase == CavalryPhase::Impact &&
                        world.tick < regiment.cavalry_phase_until;
    if ((!impact && regiment.cavalry_phase != CavalryPhase::Approach) ||
        regiment.panic || regiment.disordered || regiment.cavalry_target != defender.regiment ||
        regiment.charge_runup_m < runup || !IsFormationFrontRank(regiment, attacker) ||
        attacker.skirmisher || attacker.skirmisher_returning || attacker.parry_until > world.tick + 1 ||
        (attacker.troop != UnitTroop::Mounted && !chariot && !elephant))
        return false;
    if (impact) {
        if (attacker.control != ControlMode::FormationControlled || attacker.parry_ready_tick)
            return false;
        const bool dagger = world.scenario.rules.local_melee && attacker.archer;
        const Tick cooldown = dagger ? DaggerWeapon(attacker).cooldown_ticks : attacker.weapon.cooldown_ticks;
        const Tick ready = dagger ? attacker.dagger_ready_tick : attacker.ready_tick;
        const Tick start = regiment.cavalry_phase_until - std::min(ImpactTicks, regiment.cavalry_phase_until);
        if (ready >= cooldown && ready - cooldown >= start)
            return false;
    }
    return Dot(defender.pose.position - attacker.pose.position, Forward(regiment.pose.yaw)) > 0.f &&
           !TerrainForbidsCharge(attacker.pose.position, defender.pose.position,
                                 world.scenario.obstacles, chariot);
}

void RegisterCharge(World &world, RegimentState &regiment) {
    if (regiment.cavalry_phase != CavalryPhase::Approach)
        return;
    regiment.cavalry_phase = CavalryPhase::Impact;
    regiment.cavalry_phase_until = world.tick + ImpactTicks;
    regiment.fatigue = std::min(1.f, regiment.fatigue + .12f);
}

bool SmokeObscures(const World &world, Vec3 from, Vec3 to) {
    from.z = to.z = 0.f;
    const auto ray = to - from;
    const float squared = Dot(ray, ray);
    if (squared <= 0.f)
        return false;
    return std::any_of(world.smoke.begin(), world.smoke.end(), [&](const auto &cloud) {
        if (cloud.expires_tick <= world.tick)
            return false;
        const Vec3 center{cloud.center.x, cloud.center.y, 0.f};
        const float along = std::clamp(Dot(center - from, ray) / squared, 0.f, 1.f);
        return Distance(center, from + ray * along) < cloud.radius_m;
    });
}
} // namespace battle
