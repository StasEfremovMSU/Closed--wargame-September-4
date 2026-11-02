#pragma once
#include "Types.h"
#include <unordered_map>
namespace battle {
struct UnitPosition {
    UnitId id;
    Vec3 position;
    int32_t team{};
    size_t payload_index{};
};
struct NearestEnemy {
    UnitId id{};
    size_t payload_index{};
    float distance{};
};
class SpatialIndex {
    float cell_m_;
    bool unbounded_cells_{};
    std::vector<UnitPosition> all_;
    std::unordered_map<uint64_t, std::vector<size_t>> cells_;

  public:
    explicit SpatialIndex(float cell_m = 4) : cell_m_(cell_m) {};
    void Rebuild(std::span<const UnitPosition>);
    // Payload indices belong to the immutable source used for this rebuild only.
    // checks counts visited point candidates before team/radius rejection.
    std::optional<NearestEnemy> FindNearestEnemy(Vec3, float, int32_t,
                                                 uint64_t *checks = nullptr) const;
    std::vector<UnitId> QueryRadius(Vec3, float) const;
    void QueryRadiusInto(Vec3, float, std::vector<UnitId> &) const;
};
} // namespace battle
