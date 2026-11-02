#pragma once
#include "Types.h"
#include <unordered_map>
namespace battle {
struct UnitPosition{UnitId id;Vec3 position;};
class SpatialIndex{
 float cell_m_;std::vector<UnitPosition> all_;std::unordered_map<uint64_t,std::vector<size_t>> cells_;
public:explicit SpatialIndex(float cell_m=4):cell_m_(cell_m){};void Rebuild(std::span<const UnitPosition>);std::vector<UnitId> QueryRadius(Vec3,float)const;
};
}
