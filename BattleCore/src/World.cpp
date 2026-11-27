#include "battle/World.h"
#include "battle/Formation.h"
#include <map>
#include <set>
namespace battle {
namespace {
bool ValidWeapon(const WeaponSpec &weapon) {
    return std::isfinite(weapon.range) && weapon.range > 0 &&
           std::isfinite(weapon.damage) && weapon.damage >= 0 &&
           weapon.base_accuracy >= 0 && weapon.base_accuracy <= 1 &&
           std::isfinite(weapon.projectile_speed_mps) && weapon.projectile_speed_mps > 0 &&
           weapon.cooldown_ticks != 0;
}
} // namespace

Result<Scenario> ValidateScenario(Scenario s) {
    auto bad = [&](std::string message) {
        return Result<Scenario>{Status::InvalidInput, {}, {{"scenario", 0, std::move(message)}}};
    };
    if (s.schema_version != 1)
        return bad("unsupported scenario version");
    if (s.hill && !ValidHill(*s.hill))
        return bad("invalid terrain hill");
    if (s.field_bounds && (!std::isfinite(s.field_bounds->half_width_m) || s.field_bounds->half_width_m <= 0.f ||
                           !std::isfinite(s.field_bounds->half_height_m) || s.field_bounds->half_height_m <= 0.f))
        return bad("invalid battlefield bounds");
    if (s.objective && (!Finite(s.objective->center) || !std::isfinite(s.objective->radius_m) ||
                        s.objective->radius_m <= 0 || s.objective->defending_team < -1))
        return bad("invalid scenario objective");
    std::set<uint64_t> regs, units, offs, off_units, classified_cavalry_regiments;
    std::set<RegimentId> loose_regiments;
    std::map<RegimentId, FormationShape> shaped_regiments;
    for (const auto &r : s.regiments) {
        if (!r.id || !regs.insert(r.id.value).second || !Finite(r.pose) || !Finite(r.rally_point) ||
            r.formation.columns == 0 || !std::isfinite(r.formation.spacing_m) ||
            r.formation.spacing_m <= 0 || !std::isfinite(r.formation.body_radius_m) ||
            r.formation.body_radius_m <= 0 || r.formation.shape > FormationShape::Circle ||
            !std::isfinite(r.speed) || r.speed < 0 ||
            !std::isfinite(r.turn_rate) || r.turn_rate <= 0)
            return bad("invalid or duplicate regiment");
        if (r.deployment_ticks > 1200 || !(r.initial_fatigue >= 0.f && r.initial_fatigue <= 1.f))
            return bad("regiment deployment exceeds 60 seconds or fatigue is outside [0,1]");
        if (r.loose_skirmish)
            loose_regiments.insert(r.id);
        if (r.formation.shape != FormationShape::Rectangle) {
            if (r.loose_skirmish)
                return bad("a loose skirmish formation cannot use a closed shape");
            shaped_regiments.emplace(r.id, r.formation.shape);
        }
    }
    for (auto &u : s.units) {
        if (!u.id || !units.insert(u.id.value).second ||
            (u.regiment && !regs.contains(u.regiment.value)) ||
            uint8_t(u.troop) > uint8_t(UnitTroop::Mounted) ||
            !Finite(u.pose) || !std::isfinite(u.health) || u.health <= 0 ||
            !std::isfinite(u.speed) || u.speed < 0 || !std::isfinite(u.armour) || u.armour < 0 ||
            !std::isfinite(u.archer_skill) || u.archer_skill < 0 || u.archer_skill > 1 ||
            !ValidWeapon(u.weapon))
            return bad("invalid or duplicate unit");
        if (uint8_t(u.cavalry) > uint8_t(CavalryClass::Heavy) ||
            (u.cavalry != CavalryClass::None && u.troop != UnitTroop::Mounted) ||
            uint8_t(u.ranged_weapon) > uint8_t(RangedWeapon::Arcane) ||
            ((u.ranged_weapon != RangedWeapon::None || u.ammunition != 0) && !u.archer &&
             !u.ranged_attack) ||
            uint8_t(u.platform) > uint8_t(BattlePlatform::FieldGun))
            return bad("invalid unit troop profile");
        if (u.ranged_attack && (u.archer || u.platform != BattlePlatform::None ||
            u.ranged_weapon != RangedWeapon::Javelin || !u.ammunition || !ValidWeapon(*u.ranged_attack)))
            return bad("invalid secondary javelin profile");
        if ((u.troop == UnitTroop::Mounted || u.platform != BattlePlatform::None) &&
            loose_regiments.contains(u.regiment))
            return bad("loose skirmish formations require foot troops");
        if (const auto shape = shaped_regiments.find(u.regiment);
            shape != shaped_regiments.end() && !CanUseFormationShape(shape->second, u))
            return bad("unit troop profile does not support the regiment's shape");
        if (IsSiegeEngine(u.platform)) {
            const bool catapult = u.platform == BattlePlatform::Catapult;
            const bool field_gun = u.platform == BattlePlatform::FieldGun;
            if (!u.archer || u.ranged_weapon != RangedWeapon::None || u.cavalry != CavalryClass::None ||
                (catapult && u.weapon.range < 10.f))
                return bad("invalid siege engine profile");
            const uint32_t stock = field_gun ? 10u : catapult ? 12u : 14u;
            u.ammunition = u.ammunition ? std::min(u.ammunition, stock) : stock;
            u.weapon.range = std::min(u.weapon.range, field_gun ? 220.f : catapult ? 40.f : 30.f);
            u.weapon.cooldown_ticks = std::max(u.weapon.cooldown_ticks, field_gun ? Tick{160} : catapult ? Tick{80} : Tick{60});
            u.speed = std::min(u.speed, SiegeTravelLimit(u.platform));
        }
        if (u.regiment && u.cavalry != CavalryClass::None)
            classified_cavalry_regiments.insert(u.regiment.value);
    }
    if (!classified_cavalry_regiments.empty())
        for (const auto &u : s.units)
            if (u.troop != UnitTroop::Mounted && classified_cavalry_regiments.contains(u.regiment.value))
                return bad("a classified cavalry regiment cannot include infantry");
    for (const auto &o : s.officers) {
        if (!o.id || !offs.insert(o.id.value).second || !units.contains(o.unit.value) ||
            !off_units.insert(o.unit.value).second || o.role < 1 ||
            o.role > command::ArmyStaffOfficerRole ||
            !std::isfinite(o.charisma) || !std::isfinite(o.tactic_mind) ||
            !std::isfinite(o.lead_skill) || !std::isfinite(o.lead_dist))
            return bad("invalid officer reference");
        if (o.role == command::ArmyStaffOfficerRole &&
            (!s.command_setup || std::ranges::none_of(s.command_setup->hierarchy.support, [&](const auto &staff) {
                return staff.officer == o.id && (staff.duty == command::SupportDuty::Adjutant ||
                                                 staff.duty == command::SupportDuty::Bodyguard);
            })))
            return bad("independent army staff require a support assignment");
    }
    for (const auto &u : s.units) {
        if (!u.regiment && std::none_of(s.officers.begin(), s.officers.end(), [&](auto &officer) {
                return officer.unit == u.id &&
                       (officer.role == 1 || officer.role == command::ArmyStaffOfficerRole);
            }))
            return bad("only independent command figures can omit regiment membership");
    }
    for (auto o : s.obstacles)
        if (!std::isfinite(o.min_x) || !std::isfinite(o.min_y) || !std::isfinite(o.max_x) ||
            !std::isfinite(o.max_y) || o.min_x >= o.max_x || o.min_y >= o.max_y ||
            uint8_t(o.feature) > uint8_t(TerrainFeature::RedoubtInterior))
            return bad("invalid obstacle");
    const auto &r = s.rules;
    if (r.ranged_cohorts == 0 || r.ranged_cohorts > 64)
        return bad("ranged_cohorts must be 1..64");
    if (!std::isfinite(r.panic_fraction) || r.panic_fraction < 0 || r.panic_fraction > 1 ||
        r.calm_ticks == 0 || !std::isfinite(r.rally_tolerance_m) || r.rally_tolerance_m < 0 ||
        !std::isfinite(r.rally_safe_radius_m) || r.rally_safe_radius_m < 0)
        return bad("invalid morale rules");
    return {Status::Ok, std::move(s), {}};
}
Result<World> CreateWorld(const Scenario &s, uint64_t seed) {
    auto valid = ValidateScenario(s);
    if (!valid)
        return {valid.status, {}, valid.diagnostics};
    World w;
    w.scenario = std::move(valid.value);
    w.seed = seed;
    auto canonical = [](auto &a, auto &b) { return a.id < b.id; };
    std::sort(w.scenario.units.begin(), w.scenario.units.end(), canonical);
    std::sort(w.scenario.regiments.begin(), w.scenario.regiments.end(), canonical);
    std::sort(w.scenario.officers.begin(), w.scenario.officers.end(), canonical);
    for (const auto &r : w.scenario.regiments) {
        RegimentState x;
        static_cast<RegimentSpec &>(x) = r;
        x.skirmishers_enabled = w.scenario.rules.local_melee && r.skirmisher_count > 0;
        if (w.scenario.rules.regiment_tactics)
            x.fatigue = r.initial_fatigue;
        w.regiments.push_back(x);
    }
    for (const auto &u : w.scenario.units) {
        UnitState x;
        static_cast<UnitSpec &>(x) = u;
        x.slot = u.pose;
        x.ammunition_remaining = u.ammunition;
        w.units.push_back(x);
    }
    std::sort(w.units.begin(), w.units.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(w.regiments.begin(), w.regiments.end(), [](auto &a, auto &b) { return a.id < b.id; });
    w.officers = w.scenario.officers;
    for (const auto &u : w.units)
        if (auto *regiment = FindRegiment(w, u.regiment))
            regiment->units.push_back(u.id);
    for (auto &r : w.regiments)
        r.initial_count = r.units.size();
    InitializeFormations(w);
    auto command = command::InitializeCommand(w);
    if (command.status != command::StubStatus::Ready)
        return {Status::InvalidInput,
                {},
                {{"scenario.command_setup", 0, "invalid command hierarchy or missions"}}};
    return {Status::Ok, std::move(w), {}};
}
UnitState *FindUnit(World &w, UnitId id) {
    auto i = std::lower_bound(w.units.begin(), w.units.end(), id,
                              [](const auto &u, UnitId x) { return u.id < x; });
    return i != w.units.end() && i->id == id ? &*i : nullptr;
}
const UnitState *FindUnit(const World &w, UnitId id) {
    return FindUnit(const_cast<World &>(w), id);
}
RegimentState *FindRegiment(World &w, RegimentId id) {
    auto i = std::lower_bound(w.regiments.begin(), w.regiments.end(), id,
                              [](const auto &r, RegimentId x) { return r.id < x; });
    return i != w.regiments.end() && i->id == id ? &*i : nullptr;
}
const RegimentState *FindRegiment(const World &w, RegimentId id) {
    return FindRegiment(const_cast<World &>(w), id);
}
int32_t UnitTeam(const World &w, const UnitState &u) {
    const auto *regiment = FindRegiment(w, u.regiment);
    return regiment ? regiment->team : u.independent_team;
}
std::optional<UnitState> GetUnit(const World &w, UnitId id) {
    auto u = FindUnit(w, id);
    if (u)
        return *u;
    return {};
}
WorldStats GetStats(const World &w) {
    WorldStats s;
    s.regiment_count = w.regiments.size();
    for (const auto &u : w.units) {
        if (!u.alive) {
            ++s.dead_units;
            continue;
        }
        ++s.living_units;
        bool officer = std::any_of(w.officers.begin(), w.officers.end(),
                                   [&](auto &o) { return o.unit == u.id; });
        if (officer)
            ++s.officer_role_count;
        else
            ++s.rank_and_file_count;
    }
    return s;
}
} // namespace battle
