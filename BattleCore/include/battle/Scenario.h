#pragma once
#include "Types.h"
namespace battle {
struct FormationSpec {
    bool operator==(const FormationSpec &) const = default;
    uint32_t columns{5};
    float spacing_m{2};
    float body_radius_m{0.3f};
};
struct WeaponSpec {
    bool operator==(const WeaponSpec &) const = default;
    float damage{1}, range{1.5f};
    Tick cooldown_ticks{20}, windup_ticks{2};
    float projectile_speed_mps{20}, base_accuracy{0.8f};
};
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
};
struct Rules {
    float panic_fraction{0.5f};
    Tick calm_ticks{100};
    float rally_tolerance_m{2}, rally_safe_radius_m{20};
};
struct Scenario {
    uint32_t schema_version{1};
    std::vector<UnitSpec> units;
    std::vector<RegimentSpec> regiments;
    std::vector<OfficerSpec> officers;
    std::vector<Obstacle> obstacles;
    Rules rules{};
};
Result<Scenario> ValidateScenario(Scenario input);
} // namespace battle
