#include "HistoricalTerrainProfiles.h"

namespace battle::historical {

Profile LateProfile(MarchTerrain terrain) {
    using Feature = TerrainFeature;

    switch (terrain) {
    case MarchTerrain::Jena: {
        // The French approach the open plateau from Jena, between its broken wooded sides.
        static constexpr Obstacle areas[] = {
            {-.93f, -.93f, -.37f, -.57f, false, Feature::Forest},
            {-.60f, -.57f, -.27f, -.44f, false, Feature::Forest},
            {-.77f, .63f, -.26f, .96f, false, Feature::Forest},
            {-.26f, .71f, .04f, .99f, false, Feature::Forest},
            {-1.f, -.09f, -.53f, -.025f, false, Feature::Road},
            {-.53f, -.055f, .34f, .009f, false, Feature::Road},
            {.34f, -.02f, .89f, .044f, false, Feature::Road},
            {.47f, .34f, .81f, .59f, false, Feature::PloughedField},
            {.71f, -.60f, .89f, -.30f, false, Feature::PloughedField},
        };
        static constexpr HeightLobe heights[] = {
            {.055f, -.055f, .54f, .69f, 14.f},
            {.055f, .81f, .57f, .50f, 10.f},
            {.055f, -.86f, .53f, .48f, 11.f},
            {-.65f, .43f, .29f, .25f, -2.5f},
        };
        return {areas, heights, Crest{.055f, -.055f, .085f, .22f}};
    }
    case MarchTerrain::Marengo: {
        // Desaix's late action took place on the agricultural plain near San Giuliano.
        static constexpr Obstacle areas[] = {
            {-1.f, -.055f, 1.f, .015f, false, Feature::Road},
            {-.93f, .083f, -.49f, .38f, false, Feature::PloughedField},
            {-.94f, .46f, -.35f, .78f, false, Feature::PloughedField},
            {-.35f, .10f, .29f, .45f, false, Feature::PloughedField},
            {.24f, .51f, .91f, .74f, false, Feature::PloughedField},
            {-.83f, -.52f, -.23f, -.12f, false, Feature::PloughedField},
            {.03f, -.55f, .86f, -.17f, false, Feature::PloughedField},
            {.46f, -.85f, .88f, -.62f, false, Feature::PloughedField},
            {-.75f, .47f, -.61f, .48f, true, Feature::StoneWall},
            {-.75f, .47f, -.74f, .61f, true, Feature::StoneWall},
            {-.68f, .015f, -.638f, .47f, false, Feature::Road},
        };
        static constexpr HeightLobe heights[] = {
            {-.59f, .35f, .63f, .64f, 2.4f},
            {.10f, -.62f, .77f, .43f, 1.5f},
            {.72f, .21f, .74f, .66f, .9f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Wachau: {
        // The ridge road links the fringes of Wachau and Liebertwolkwitz behind the French line.
        static constexpr Obstacle areas[] = {
            {-.41f, -1.f, -.36f, -.44f, false, Feature::Road},
            {-.41f, -.44f, -.36f, .29f, false, Feature::Road},
            {-.36f, .29f, -.30f, 1.f, false, Feature::Road},
            {-.39f, .05f, 1.f, .102f, false, Feature::Road},
            {-.62f, -.88f, -.56f, -.67f, true, Feature::StoneWall},
            {-.56f, -.88f, -.44f, -.87f, true, Feature::StoneWall},
            {-.29f, .83f, -.15f, .85f, true, Feature::Hedge},
            {-.16f, .85f, -.15f, .98f, true, Feature::StoneWall},
            {-.25f, -.76f, .15f, -.49f, false, Feature::PloughedField},
            {.22f, -.70f, .83f, -.36f, false, Feature::PloughedField},
            {.17f, .44f, .88f, .76f, false, Feature::PloughedField},
            {-.84f, .25f, -.48f, .64f, false, Feature::PloughedField},
        };
        static constexpr HeightLobe heights[] = {
            {-.34f, -.50f, .40f, .63f, 6.7f},
            {-.24f, .20f, .51f, .63f, 7.5f},
            {-.28f, .77f, .48f, .42f, 8.3f},
            {.70f, -.63f, .57f, .54f, 2.6f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Nordlingen: {
        // Broken earthworks follow the face of the Albuch above the Swedish approach.
        static constexpr Obstacle areas[] = {
            {-.83f, .57f, -.40f, .96f, false, Feature::Forest},
            {-.40f, .65f, .21f, .98f, false, Feature::Forest},
            {-.023f, -.45f, -.010f, -.21f, true, Feature::RedoubtBank},
            {-.010f, -.24f, .026f, -.21f, true, Feature::RedoubtBank},
            {.009f, .18f, .023f, .44f, true, Feature::RedoubtBank},
            {.023f, .41f, .065f, .44f, true, Feature::RedoubtBank},
            {-.01f, -.18f, .033f, .15f, false, Feature::RedoubtRamp},
            {.19f, -.77f, .64f, -.52f, false, Feature::PloughedField},
        };
        static constexpr HeightLobe heights[] = {
            {.11f, .022f, .44f, .68f, 12.f},
            {-.47f, .83f, .52f, .41f, 7.f},
            {.68f, -.57f, .29f, .51f, 2.4f},
        };
        return {areas, heights, Crest{.11f, .022f, .095f, .22f}};
    }
    case MarchTerrain::Rocroi: {
        // The open ground at Rouge-Fontaine is enclosed by woodland and damp meadows.
        static constexpr Obstacle areas[] = {
            {-1.f, -.99f, -.54f, -.63f, false, Feature::Forest},
            {-.54f, -.93f, -.27f, -.73f, false, Feature::Forest},
            {-.26f, -.97f, .24f, -.82f, false, Feature::Forest},
            {.23f, -.99f, .83f, -.72f, false, Feature::Forest},
            {-.86f, .72f, -.34f, .99f, false, Feature::Marsh},
            {-.34f, .61f, .13f, .93f, false, Feature::Marsh},
            {.11f, .69f, .54f, .99f, false, Feature::Marsh},
            {.49f, .83f, .95f, .99f, false, Feature::Forest},
            {-.95f, -.23f, -.47f, -.167f, false, Feature::Road},
            {-.47f, -.17f, .96f, -.112f, false, Feature::Road},
        };
        static constexpr HeightLobe heights[] = {
            {-.31f, .85f, .81f, .51f, -1.9f},
            {.45f, .87f, .63f, .53f, -1.3f},
            {-.47f, -.77f, .75f, .45f, 5.f},
            {.59f, -.62f, .57f, .48f, 4.f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Mikatagahara: {
        // Most of the plateau is open. The steepening at one edge represents the Hoda slope.
        static constexpr Obstacle areas[] = {
            {-.95f, -.93f, -.48f, -.68f, false, Feature::Forest},
            {-.46f, -.88f, -.02f, -.71f, false, Feature::Forest},
            {.27f, -.97f, .81f, -.74f, false, Feature::Forest},
            {-.96f, -.38f, -.46f, -.32f, false, Feature::Road},
            {-.46f, -.325f, .12f, -.265f, false, Feature::Road},
            {.12f, -.27f, .91f, -.21f, false, Feature::Road},
        };
        static constexpr HeightLobe heights[] = {
            {-.19f, .39f, 1.42f, 1.17f, 11.f},
            {.64f, .29f, .72f, .77f, 2.f},
            {-.22f, -.94f, .96f, .46f, -6.f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Okehazama: {
        // A hollow between wooded shoulders opens toward the upper camp clearing.
        static constexpr Obstacle areas[] = {
            {-.95f, -.87f, -.34f, -.53f, false, Feature::Forest},
            {-.34f, -.83f, .08f, -.54f, false, Feature::Forest},
            {.09f, -.93f, .54f, -.62f, false, Feature::Forest},
            {.58f, -.76f, .96f, -.40f, false, Feature::Forest},
            {-.72f, .53f, -.28f, .91f, false, Feature::Forest},
            {-.28f, .64f, .24f, .97f, false, Feature::Forest},
            {.24f, .70f, .72f, .96f, false, Feature::Forest},
            {-.96f, .12f, -.40f, .176f, false, Feature::Road},
            {-.40f, .075f, -.09f, .132f, false, Feature::Road},
            {-.09f, .035f, .51f, .090f, false, Feature::Road},
        };
        static constexpr HeightLobe heights[] = {
            {-.45f, -.72f, .74f, .60f, 10.f},
            {.37f, -.72f, .67f, .56f, 12.f},
            {-.31f, .77f, .83f, .62f, 13.f},
            {.71f, .17f, .59f, .66f, 8.f},
            {-.37f, .10f, .58f, .37f, -3.2f},
        };
        return {areas, heights};
    }
    default:
        return {};
    }
}

} // namespace battle::historical
