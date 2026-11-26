#include "HistoricalTerrainProfiles.h"

namespace battle::historical {
namespace {
constexpr int FirstField = int(MarchTerrain::Thermopylae);
constexpr int FieldCount = int(MarchTerrain::Okehazama) - FirstField + 1;
constexpr int FirstRelief = int(TerrainRelief::HistoricalFirst);
constexpr int ReliefCount = int(TerrainRelief::HistoricalLast) - FirstRelief + 1;
static_assert(FieldCount + 2 == ReliefCount);

Profile RevisedRelief(MarchTerrain terrain) {
    using T = TerrainFeature;
    // The Goldbach is on the west side of the Pratzen plateau, with village sites along the valley.
    static constexpr Obstacle pratzen[] = {
        {-.138f, -1.f, -.110f, -.47f, false, T::River},
        {-.153f, -.47f, -.118f, -.23f, false, T::River},
        {-.153f, -.23f, -.108f, -.095f, false, T::Ford},
        {-.132f, -.095f, -.100f, .28f, false, T::River},
        {-.141f, .28f, -.093f, .45f, false, T::Ford},
        {-.129f, .45f, -.097f, .85f, false, T::River},
        {-.143f, .85f, -.090f, 1.f, false, T::Ford},
        {-.180f, -.49f, -.149f, -.41f, true, T::Barrier},
        {-.212f, -.42f, -.173f, -.35f, true, T::Barrier},
        {-.110f, .54f, -.066f, .63f, true, T::Barrier},
        {.12f, .67f, .51f, .71f, false, T::Road},
        {.36f, .75f, .42f, .83f, true, T::Barrier},
        {.46f, .78f, .94f, 1.f, false, T::River},
        {-.76f, -.66f, -.35f, -.43f, false, T::PloughedField},
    };
    static constexpr HeightLobe pratzen_height[] = {
        {.215f, -.12f, .43f, .72f, 24.f},
        {.41f, .39f, .49f, .64f, 18.f},
        {.63f, -.62f, .32f, .51f, 14.f},
        {-.66f, -.67f, .33f, .44f, 6.f},
    };

    // Traditional Crecy-en-Ponthieu site. The valley and an oblique slope replace the symmetric ridge.
    static constexpr Obstacle crecy[] = {
        {-1.f, -.61f, -.44f, -.57f, false, T::Road},
        {-.44f, -.60f, -.13f, -.54f, false, T::Road},
        {-.13f, -.56f, .29f, -.49f, false, T::Road},
        {-.74f, -.92f, -.34f, -.71f, false, T::Forest},
        {-.40f, -.47f, -.06f, .44f, false, T::PloughedField},
        {.66f, -.97f, .74f, -.83f, true, T::Barrier},
        {.75f, -.89f, .84f, -.77f, true, T::Barrier},
        {.70f, .79f, .91f, .99f, false, T::Forest},
    };
    static constexpr HeightLobe crecy_height[] = {
        {.32f, -.43f, .45f, .63f, 14.f},
        {.52f, .19f, .49f, .68f, 11.f},
        {.66f, .67f, .38f, .55f, 10.f},
        {-.66f, -.76f, .37f, .40f, 5.f},
    };

    if (terrain == MarchTerrain::Austerlitz)
        return {pratzen, pratzen_height, Crest{.215f, -.12f, .092f, .27f}};
    return {crecy, crecy_height, Crest{.33f, -.39f, .10f, .23f}};
}
} // namespace

std::optional<TerrainRelief> ReliefFor(MarchTerrain terrain) {
    const int offset = int(terrain) - FirstField;
    if (offset >= 0 && offset < FieldCount)
        return TerrainRelief(FirstRelief + offset);
    if (terrain == MarchTerrain::Austerlitz)
        return TerrainRelief(FirstRelief + FieldCount);
    if (terrain == MarchTerrain::Crecy)
        return TerrainRelief(FirstRelief + FieldCount + 1);
    return std::nullopt;
}

const Profile *FindProfile(TerrainRelief relief) {
    const int index = int(relief) - FirstRelief;
    if (index < 0 || index >= ReliefCount)
        return nullptr;
    static const auto profiles = [] {
        std::array<Profile, ReliefCount> catalog{};
        for (int i = 0; i < FieldCount; ++i) {
            const auto terrain = MarchTerrain(FirstField + i);
            auto profile = AncientProfile(terrain);
            if (profile.areas.empty() && profile.heights.empty())
                profile = MedievalProfile(terrain);
            if (profile.areas.empty() && profile.heights.empty())
                profile = LateProfile(terrain);
            catalog[i] = profile;
        }
        catalog[FieldCount] = RevisedRelief(MarchTerrain::Austerlitz);
        catalog[FieldCount + 1] = RevisedRelief(MarchTerrain::Crecy);
        return catalog;
    }();
    return &profiles[index];
}
} // namespace battle::historical
