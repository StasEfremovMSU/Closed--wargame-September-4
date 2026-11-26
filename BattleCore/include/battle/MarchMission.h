#pragma once
#include "Scenario.h"
#include <array>
#include <cstddef>
#include <span>

namespace battle {
enum class MarchTerrain { River, OpenField, StoneWalls, StirlingBridge, Cannae, Peipus, Hill,
                          Waterloo, Sekigahara, Agincourt, Pavia, Poltava, Nagashino,
                          Gaugamela, Zama, Kalka, Dorylaeum, Grunwald, Crecy, Breitenfeld, Lutzen,
                          Kawanakajima, Narva, Lesnaya, Austerlitz, Borodino };
enum class MarchPosture { TravelColumn, BattleLine };

struct MarchCrossing {
    float y, half_width;
    bool ford;
};

struct MarchField {
    int32_t width_m{120};
    int32_t height_m{76};

    constexpr float HalfWidth() const { return width_m * .5f; }
    constexpr float HalfHeight() const { return height_m * .5f; }
    bool operator==(const MarchField &) const = default;
};

inline constexpr float MarchRiverHalfWidth = 5.f;
inline constexpr float MarchHalfWidth = MarchField{}.HalfWidth();
inline constexpr float MarchHalfHeight = MarchField{}.HalfHeight();
inline constexpr std::array<MarchCrossing, 3> MarchCrossings{
    {{-24, 4, false}, {0, 4, true}, {24, 4, false}}};

struct MarchRegiment {
    RegimentId id;
    int32_t team;
    Pose pose;
    std::optional<Vec3> waypoint;
    std::vector<Pose> route;
    std::size_t next_waypoint{};
    FormationSpec formation{}; // The battle line to use after deployment.
    UnitTroop troop{};
    MarchPosture posture{MarchPosture::TravelColumn};
    float deployment_seconds{};
    float fatigue{};
    struct CrossingOrder {
        std::size_t area{};
        uint64_t sequence{};
        float exit_side{};
    };
    std::optional<CrossingOrder> crossing_order;
    uint32_t figure_count{1};
    bool fixed_frontage{}; // Siege batteries keep the deployment line during a march.

    FormationSpec ActiveFormation() const;
    // Time needed to become battle-ready if contact starts now.
    float DeploymentSeconds() const;
};

struct MarchEncounter {
    RegimentId first, second;
    Vec3 center;
};

std::vector<Obstacle> MarchObstacles(MarchTerrain = MarchTerrain::River, MarchField field = {});
std::vector<Obstacle> MarchTerrainAreas(MarchTerrain = MarchTerrain::River, MarchField field = {});
bool MarchUsesDetailedTerrain(MarchTerrain);
bool MarchHasWaterCrossings(MarchTerrain);
std::span<const MarchCrossing> MarchCrossingsFor(MarchTerrain);
std::optional<TerrainHill> MarchHill(MarchTerrain, MarchField field = {});
bool MarchStartWalkable(const MarchRegiment &, MarchTerrain, bool regiment_tactics, MarchField field = {});

class MarchMission {
  public:
    explicit MarchMission(std::vector<MarchRegiment>, MarchTerrain = MarchTerrain::River,
                          bool regiment_tactics = false, MarchField field = {});
    const std::vector<MarchRegiment> &Regiments() const;
    bool EnemyFound() const;
    const std::optional<MarchEncounter> &Encounter() const;
    std::optional<std::vector<Pose>> PreviewWaypoint(RegimentId, Vec3) const;
    bool SetWaypoint(RegimentId, Vec3);
    std::optional<std::vector<Pose>> PreviewCrossing(RegimentId, Vec3 deployment_point) const;
    bool QueueCrossing(RegimentId, Vec3 deployment_point);
    std::size_t CrossingQueuePosition(RegimentId) const;
    void ResumeMarch() { awaiting_order_ = false; }
    bool SetPosture(RegimentId, MarchPosture);
    void Step(float dt);

  private:
    struct CrossingPlan {
        std::size_t area{};
        std::vector<Pose> route;
    };
    std::optional<CrossingPlan> PlanCrossing(const MarchRegiment &, Vec3) const;
    void AssignCrossing(MarchRegiment &, CrossingPlan);
    void ScheduleEnemyCrossings();
    void CheckForContact();

    std::vector<MarchRegiment> regiments_;
    MarchTerrain terrain_;
    MarchField field_;
    std::vector<Obstacle> terrain_areas_;
    std::optional<MarchEncounter> encounter_;
    double elapsed_{};
    bool enemy_found_{};
    bool awaiting_order_{};
    bool regiment_tactics_{};
    uint64_t crossing_sequence_{};
};
} // namespace battle
