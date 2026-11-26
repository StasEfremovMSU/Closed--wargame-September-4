#pragma once
#include "Types.h"

namespace battle {
struct Obstacle;
struct World;
struct RegimentState;
enum class UnitTroop : uint8_t;

struct TerrainFieldBounds {
    float half_width_m{}, half_height_m{};
    bool operator==(const TerrainFieldBounds &) const = default;
};

Vec3 TerrainFieldInset(const TerrainFieldBounds &, float yaw, float half_depth, float half_width);

enum class TerrainFeature : uint8_t {
    Barrier, StoneWall, Hedge, River, Bridge, Ford, Forest, Marsh, PloughedField, Road,
    RedoubtRamp, RedoubtBank, RedoubtInterior
};

struct TerrainShot {
    bool blocked{};
    int shooting_modifier{};
};

bool TerrainBlocksMovement(const Obstacle &);
bool TerrainIsCrossing(const Obstacle &);
bool TerrainAreaOverlaps(const Obstacle &, Pose, float half_depth, float half_width);
bool TerrainForbidsCharge(Vec3 from, Vec3 to, std::span<const Obstacle>, bool wheeled = false);
float TerrainSpeedMultiplier(Vec3 from, Vec3 to, std::span<const Obstacle>, UnitTroop, float speed_mps);
std::optional<Pose> TerrainCoverPosition(const Obstacle &, Pose desired, float half_depth, float half_width);
void AssignCoverGuardSlots(const World &, RegimentState &);
TerrainShot EvaluateTerrainShot(Vec3 from, Vec3 to, std::span<const Obstacle>,
                               bool hard_projectile_cover = false);

enum class TerrainRelief : uint8_t {
    CentralHill, Waterloo, Sekigahara,
    // Stored in snapshots. The historical field IDs and their order are defined in HistoricalTerrainProfiles.cpp.
    HistoricalFirst, HistoricalLast = HistoricalFirst + 21
};

struct TerrainHill {
    float radius_x_m{36.f}, radius_y_m{28.f}, height_m{4.f}, crest_fraction{.35f};
    TerrainRelief relief{TerrainRelief::CentralHill};
    bool operator==(const TerrainHill &) const = default;
};

struct TerrainCrest {
    Vec3 center;
    float radius_x_m, radius_y_m;
};

std::optional<TerrainCrest> HillObjectiveCrest(const TerrainHill &);
float HillHeight(const TerrainHill &, Vec3);
bool OnHillCrest(const TerrainHill &, Vec3);
bool ValidHill(const TerrainHill &);
float HillUphillSpeedMultiplier(const TerrainHill &, Vec3 from, Vec3 to);
bool HillSegmentPassable(const TerrainHill &, Vec3 from, Vec3 to, UnitTroop);
float HillUphillSpeedMultiplier(const TerrainHill &, Vec3 from, Vec3 to, UnitTroop, float speed);
bool HillHasLineOfSight(const TerrainHill &, Vec3 from, Vec3 to);
} // namespace battle
