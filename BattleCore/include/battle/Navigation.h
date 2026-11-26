#pragma once
#include "Scenario.h"
#include "Types.h"
#include <functional>
namespace battle {
struct FormationFootprint {
    float width_m{}, depth_m{};
};
struct FormationBlock {
    Pose pose{};
    FormationFootprint footprint{};
};
struct PathRequest {
    Pose start{};
    Vec3 goal{};
    FormationFootprint footprint{};
    std::optional<float> arrival_yaw{};
    std::span<const FormationBlock> occupied{};
    bool prefer_direct{};
    bool preserve_facing{};
    std::function<bool(Pose, Pose)> segment_clear{};
    bool face_travel{};
};
struct PathResult {
    Status status{Status::Ok};
    std::vector<Pose> waypoints;
};
class NavigationService {
  public:
    virtual ~NavigationService() = default;
    virtual PathResult FindPath(const PathRequest &) const = 0;
};
class FlatNavigationService : public NavigationService {
  public:
    PathResult FindPath(const PathRequest &) const override;
};
struct NavigationGridGeometry {
    float cell_m{};
    uint32_t width{}, height{};
    Vec3 origin{};
};
class GridNavigationService : public NavigationService {
    std::vector<Obstacle> obstacles_;
    float cell_m_;
    uint32_t width_, height_;
    Vec3 origin_;

  public:
    explicit GridNavigationService(std::vector<Obstacle> obstacles = {}, float cell_m = 2,
                                   uint32_t width = 256, uint32_t height = 256,
                                   Vec3 origin = {-256, -256, 0})
        : obstacles_(std::move(obstacles)), cell_m_(cell_m), width_(width), height_(height),
          origin_(origin) {}
    NavigationGridGeometry Geometry() const { return {cell_m_, width_, height_, origin_}; }
    bool CanTraverse(Pose from, Pose to, FormationFootprint,
                     std::span<const FormationBlock> occupied = {}) const;
    PathResult FindPath(const PathRequest &) const override;
};
struct World;
struct RegimentState;
PathResult FindFlankRoute(const NavigationService &, const World &, const RegimentState &, Pose goal,
                          RegimentId target);
PathResult FindAttackRoute(const NavigationService &, const World &, const RegimentState &,
                           Pose observed_enemy, RegimentId enemy);
PathResult FindSupportRoute(const NavigationService &, const World &, const RegimentState &,
                            Vec3 goal, RegimentId excluded = {});
PathResult FindFormationRoute(const NavigationService &, const World &, const RegimentState &,
                              Pose goal, bool preserve_facing = false);
bool FormationRouteSegmentClear(const World &, const RegimentState &, Pose goal);
bool CanContinueFormationMarch(const World &, const RegimentState &, Pose waypoint,
                               std::optional<Pose> player_destination = std::nullopt);
} // namespace battle
