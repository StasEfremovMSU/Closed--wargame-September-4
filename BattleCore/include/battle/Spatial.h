#pragma once
#include "Scenario.h"
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

    template <class Value, class Projection>
    void QueryRadiusValuesInto(Vec3, float, std::vector<Value> &, Projection,
                               std::optional<int32_t> excluded_team = {}, bool ordered = true) const;

  public:
    explicit SpatialIndex(float cell_m = 4) : cell_m_(cell_m) {};
    void Rebuild(std::span<const UnitPosition>);
    // Payload indices belong to the immutable source used for this rebuild only.
    // checks counts visited point candidates before team/radius rejection.
    std::optional<NearestEnemy> FindNearestEnemy(Vec3, float, int32_t,
                                                 uint64_t *checks = nullptr,
                                                 std::span<const Obstacle> cover = {},
                                                 const TerrainHill *hill = nullptr) const;
    std::vector<UnitId> QueryRadius(Vec3, float) const;
    void QueryRadiusInto(Vec3, float, std::vector<UnitId> &) const;
    // Same geometric query, returning unique payload indices in ascending order.
    void QueryRadiusPayloadsInto(Vec3, float, std::vector<size_t> &,
                                 std::optional<int32_t> excluded_team = {}) const;
    // Returns one entry per indexed point, with no order or deduplication guarantee.
    void QueryRadiusPayloadsUnorderedInto(Vec3, float, std::vector<size_t> &) const;
};
} // namespace battle
