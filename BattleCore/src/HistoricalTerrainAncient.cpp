#include "HistoricalTerrainProfiles.h"

namespace battle::historical {

Profile AncientProfile(MarchTerrain terrain) {
    using Feature = TerrainFeature;

    switch (terrain) {
    case MarchTerrain::Thermopylae: {
        // The Middle Gate runs between the mountain foot and the spring-fed coastal marsh.
        static constexpr Obstacle areas[] = {
            {-1.f, -.49f, -.20f, -.42f, false, Feature::Marsh},
            {-.20f, -.44f, -.05f, -.40f, false, Feature::Marsh},
            {-.05f, -.41f, .15f, -.32f, false, Feature::Marsh},
            {.15f, -.46f, .43f, -.39f, false, Feature::Marsh},
            {.43f, -.50f, 1.f, -.42f, false, Feature::Marsh},
            {-1.f, -1.f, -.20f, -.49f, false, Feature::River},
            {-.20f, -1.f, -.05f, -.44f, false, Feature::River},
            {-.05f, -1.f, .15f, -.41f, false, Feature::River},
            {.15f, -1.f, .43f, -.46f, false, Feature::River},
            {.43f, -1.f, 1.f, -.50f, false, Feature::River},
            {-1.f, -.065f, 1.f, .063f, false, Feature::Road},
            {.028f, -.43f, .042f, -.17f, true, Feature::StoneWall},
            {.028f, .17f, .042f, .55f, true, Feature::StoneWall},
            {-.72f, .60f, -.34f, .89f, false, Feature::Forest},
            {.23f, .61f, .61f, .94f, false, Feature::Forest},
        };
        static constexpr HeightLobe heights[] = {
            {-.72f, .92f, .62f, .59f, 20.f},
            {-.08f, .87f, .64f, .65f, 29.f},
            {.56f, .96f, .67f, .61f, 23.f},
            {-.12f, -.76f, 1.35f, .45f, -1.5f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Issus: {
        // Three breaks in the Pinarus banks retain a coastal approach and the interior crossings.
        static constexpr Obstacle areas[] = {
            {-1.f, -1.f, 1.f, -.79f, false, Feature::River},
            {-.044f, .53f, -.002f, 1.f, false, Feature::River},
            {-.040f, .11f, .006f, .35f, false, Feature::River},
            {-.012f, -.42f, .035f, -.10f, false, Feature::River},
            {.015f, -.79f, .063f, -.59f, false, Feature::River},
            {-.065f, .35f, .028f, .53f, false, Feature::Ford},
            {-.051f, -.10f, .051f, .11f, false, Feature::Ford},
            {-.015f, -.59f, .088f, -.42f, false, Feature::Ford},
            {-.84f, .57f, -.43f, .85f, false, Feature::Forest},
            {.42f, .62f, .79f, .87f, false, Feature::Forest},
            {-.86f, -.62f, -.52f, -.33f, false, Feature::PloughedField},
        };
        static constexpr HeightLobe heights[] = {
            {-.73f, .90f, .64f, .63f, 21.f},
            {-.12f, .98f, .55f, .65f, 28.f},
            {.56f, .93f, .66f, .61f, 24.f},
            {.065f, .26f, .10f, .18f, 3.f},
            {.087f, -.26f, .11f, .23f, 2.5f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Alesia: {
        // A section of the outward-facing Roman line on the plain west of Mont Auxois.
        static constexpr Obstacle areas[] = {
            {-.11f, -1.f, -.061f, -.67f, false, Feature::Marsh},
            {-.11f, -.43f, -.061f, -.155f, false, Feature::Marsh},
            {-.11f, .155f, -.061f, .43f, false, Feature::Marsh},
            {-.11f, .67f, -.061f, 1.f, false, Feature::Marsh},
            {-.020f, -1.f, .018f, -.67f, true, Feature::RedoubtBank},
            {-.020f, -.43f, .018f, -.155f, true, Feature::RedoubtBank},
            {-.020f, .155f, .018f, .43f, true, Feature::RedoubtBank},
            {-.020f, .67f, .018f, 1.f, true, Feature::RedoubtBank},
            {-.49f, -.064f, .34f, .064f, false, Feature::Road},
            {-.93f, -.53f, -.52f, -.22f, false, Feature::PloughedField},
            {-.91f, .30f, -.53f, .60f, false, Feature::PloughedField},
            {.63f, -.76f, .95f, -.52f, false, Feature::Forest},
            {.59f, .52f, .91f, .81f, false, Feature::Forest},
        };
        static constexpr HeightLobe heights[] = {
            {1.07f, -.02f, .53f, .52f, 23.f},
            {1.04f, .22f, .49f, .52f, 14.f},
            {-.53f, -.98f, .73f, .43f, 10.f},
            {.47f, .96f, .84f, .43f, 13.f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Chaeronea: {
        // The open valley lies between the Kephissos and the hills above the ancient town.
        static constexpr Obstacle areas[] = {
            {-1.f, -.87f, -.32f, -.80f, false, Feature::River},
            {-.32f, -.90f, .41f, -.83f, false, Feature::River},
            {.41f, -.95f, 1.f, -.88f, false, Feature::River},
            {-.81f, -.79f, -.36f, -.62f, false, Feature::PloughedField},
            {.31f, -.77f, .82f, -.57f, false, Feature::PloughedField},
            {-.75f, .56f, -.50f, .79f, false, Feature::Forest},
            {.43f, .64f, .76f, .84f, false, Feature::Forest},
            {-.90f, .43f, .94f, .49f, false, Feature::Road},
        };
        static constexpr HeightLobe heights[] = {
            {-.68f, .92f, .67f, .56f, 13.f},
            {.12f, .96f, .64f, .55f, 19.f},
            {.72f, .90f, .57f, .44f, 10.f},
            {-.26f, .47f, .58f, .33f, 3.0f},
            {.33f, -.31f, .61f, .53f, 1.5f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Hydaspes: {
        // Both armies deploy on the firm floodplain of the same bank.
        static constexpr Obstacle areas[] = {
            {-1.f, .77f, -.53f, 1.f, false, Feature::River},
            {-.53f, .84f, .12f, 1.f, false, Feature::River},
            {.12f, .87f, .61f, 1.f, false, Feature::River},
            {.61f, .76f, 1.f, 1.f, false, Feature::River},
            {-1.f, .63f, -.56f, .77f, false, Feature::Marsh},
            {-.53f, .70f, .14f, .84f, false, Feature::Marsh},
            {.15f, .72f, .60f, .87f, false, Feature::Marsh},
            {.63f, .62f, 1.f, .76f, false, Feature::Marsh},
            {-.84f, -.73f, -.52f, -.48f, false, Feature::Forest},
            {.62f, -.82f, .88f, -.59f, false, Feature::Forest},
            {.65f, .43f, .87f, .60f, false, Feature::Forest},
        };
        static constexpr HeightLobe heights[] = {
            {-.57f, -.86f, .67f, .46f, 3.5f},
            {.55f, -.89f, .83f, .54f, 5.f},
            {-.04f, .43f, 1.16f, .71f, -1.2f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Granicus: {
        // The crossing geometry is supplied by the shared river layout.
        static constexpr Obstacle areas[] = {
            {-.87f, -.84f, -.56f, -.53f, false, Feature::Forest},
            {-.85f, .49f, -.56f, .77f, false, Feature::Forest},
            {.41f, -.77f, .83f, -.53f, false, Feature::PloughedField},
            {.46f, .58f, .87f, .83f, false, Feature::PloughedField},
        };
        static constexpr HeightLobe heights[] = {
            {-.78f, -.75f, .64f, .68f, 11.f},
            {-.84f, .73f, .74f, .63f, 14.f},
            {.17f, -.89f, .23f, .33f, 4.f},
            {.16f, .89f, .24f, .34f, 5.f},
            {.72f, .09f, .72f, .85f, 2.f},
        };
        return {areas, heights};
    }
    default:
        return {};
    }
}

} // namespace battle::historical
