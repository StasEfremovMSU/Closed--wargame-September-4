#pragma once
#include "Types.h"
#include "Terrain.h"
#include "battle/command/Setup.h"
namespace battle {
enum class FormationShape : uint8_t { Rectangle, Wedge, Square, Testudo, Circle };
struct FormationSpec {
    bool operator==(const FormationSpec &) const = default;
    uint32_t columns{5};
    float spacing_m{2};
    float body_radius_m{0.3f};
    FormationShape shape{FormationShape::Rectangle};
    float slot_stagger{}; // [0, 1] of the allowed variation within a rectangular slot grid.
};
constexpr bool ValidSlotStagger(float value) { return value >= 0.f && value <= 1.f; }
constexpr bool HasOutwardRanks(FormationShape shape) {
    return shape == FormationShape::Square || shape == FormationShape::Circle;
}
FormationSpec TravelColumnFormation(FormationSpec);
struct WeaponSpec {
    bool operator==(const WeaponSpec &) const = default;
    float damage{1}, range{1.5f};
    Tick cooldown_ticks{20}, windup_ticks{2};
    float projectile_speed_mps{20}, base_accuracy{0.8f};
};
enum class UnitTroop : uint8_t { Infantry, Spearman, Mounted };
enum class CavalryClass : uint8_t { None, Light, Heavy };
enum class RangedWeapon : uint8_t { None, Bow, Crossbow, Sling, Javelin, Gunpowder, Musket, Arcane };
constexpr bool UsesGunpowder(RangedWeapon weapon) {
    return weapon == RangedWeapon::Gunpowder || weapon == RangedWeapon::Musket;
}
enum class BattlePlatform : uint8_t { None, IndianElephant, AfricanElephant, ScythedChariot, Catapult, Ballista, FieldGun };
constexpr bool IsSiegeEngine(BattlePlatform platform) {
    return platform == BattlePlatform::Catapult || platform == BattlePlatform::Ballista ||
           platform == BattlePlatform::FieldGun;
}
constexpr float SiegeTravelLimit(BattlePlatform platform) {
    if (platform == BattlePlatform::Ballista)
        return 0.f;
    return platform == BattlePlatform::Catapult ? 1.f : 1.1f;
}
constexpr bool IsElephant(BattlePlatform platform) {
    return platform == BattlePlatform::IndianElephant || platform == BattlePlatform::AfricanElephant;
}
struct UnitSpec {
    bool operator==(const UnitSpec &) const = default;
    UnitId id{};
    RegimentId regiment{};
    std::string name;
    Pose pose{};
    float health{1}, speed{2}, armour{}, archer_skill{1};
    bool archer{};
    std::vector<uint32_t> equipment;
    WeaponSpec weapon{};
    int32_t legacy_id{-1};
    int32_t independent_team{}; // Faction of a command figure without a regiment.
    UnitTroop troop{UnitTroop::Infantry};
    CavalryClass cavalry{};
    RangedWeapon ranged_weapon{};
    uint32_t ammunition{}; // A zero starting stock preserves unlimited fire in legacy scenarios.
    BattlePlatform platform{};
    std::optional<WeaponSpec> ranged_attack{}; // A finite javelin stock carried alongside a melee weapon.
};
struct RegimentSpec {
    bool operator==(const RegimentSpec &) const = default;
    RegimentId id{};
    int32_t team{};
    Pose pose{};
    FormationSpec formation{};
    float speed{2}, turn_rate{Pi / 2};
    Vec3 rally_point{};
    std::string name;
    uint32_t skirmisher_count{};
    Tick deployment_ticks{}; // Hold in a travel column through the opening tactical ticks.
    float initial_fatigue{};
    bool loose_skirmish{};
};
struct OfficerSpec {
    bool operator==(const OfficerSpec &) const = default;
    OfficerId id{};
    UnitId unit{};
    uint32_t role{1};
    float charisma{}, tactic_mind{}, lead_skill{}, lead_dist{};
    int32_t legacy_id{-1};
};
struct Obstacle {
    float min_x{}, min_y{}, max_x{}, max_y{};
    bool blocks_projectiles{};
    TerrainFeature feature{TerrainFeature::Barrier};
};
struct Rules {
    float panic_fraction{0.5f};
    Tick calm_ticks{100};
    float rally_tolerance_m{2}, rally_safe_radius_m{20};
    uint32_t ranged_cohorts{4};
    bool local_melee{};
    bool regiment_tactics{};
    bool risky_volley{};
    bool adaptive_battle{};
    bool linear_infantry{};
    bool mixed_rank_tactics{};
    bool sustained_melee{};
    bool controlled_arrow_aim{};
    bool individual_rout{};
    bool meeting_engagement{};
    bool formed_fire_on_skirmishers{};
};
struct ScenarioObjective {
    bool operator==(const ScenarioObjective &) const = default;
    Vec3 center{};
    float radius_m{5.f};
    int32_t defending_team{-1}; // -1 for a neutral objective, otherwise a regiment team ID.
};
struct Scenario {
    uint32_t schema_version{1};
    std::vector<UnitSpec> units;
    std::vector<RegimentSpec> regiments;
    std::vector<OfficerSpec> officers;
    std::vector<Obstacle> obstacles;
    Rules rules{};
    std::optional<command::CommandSetup> command_setup;
    std::optional<TerrainHill> hill;
    std::optional<ScenarioObjective> objective;
    std::optional<TerrainFieldBounds> field_bounds;
};
Result<Scenario> ValidateScenario(Scenario input);
} // namespace battle
