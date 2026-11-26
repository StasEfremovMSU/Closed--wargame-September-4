#pragma once

#include "battle/MarchMission.h"

namespace battle::historical {
struct HeightLobe {
    float x, y, rx, ry, height_m;
};

struct Crest {
    float x, y, rx, ry;
};

struct Profile {
    // The four horizontal coordinates are fractions of the field half extents.
    std::span<const Obstacle> areas;
    std::span<const HeightLobe> heights;
    std::optional<Crest> crest{};
};

Profile AncientProfile(MarchTerrain);
Profile MedievalProfile(MarchTerrain);
Profile LateProfile(MarchTerrain);
std::optional<TerrainRelief> ReliefFor(MarchTerrain);
const Profile *FindProfile(TerrainRelief);
} // namespace battle::historical
