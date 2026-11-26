#include "HistoricalTerrainProfiles.h"

namespace battle::historical {

Profile MedievalProfile(MarchTerrain terrain) {
    using Feature = TerrainFeature;

    switch (terrain) {
    case MarchTerrain::Fraustadt: {
        // The Swedish left crosses ice near Geyersdorf. The field between the villages is open.
        static constexpr Obstacle areas[] = {
            {-.41f, -.74f, .13f, -.47f, false, Feature::Ford},
            {-.08f, -.85f, .22f, -.74f, false, Feature::Ford},
            {.11f, -.82f, .51f, -.58f, false, Feature::Marsh},
            {-.18f, -.92f, .66f, -.87f, false, Feature::Road},
            {-.49f, .77f, .70f, .82f, false, Feature::Road},
            {.21f, .70f, .50f, .76f, false, Feature::PloughedField},
            {.51f, .60f, .87f, .75f, false, Feature::PloughedField},
            {.44f, -.98f, .79f, -.87f, false, Feature::Forest},
            {.62f, -.84f, .97f, -.67f, false, Feature::Forest},
        };
        static constexpr HeightLobe heights[] = {
            {-.16f, -.62f, .55f, .39f, -1.7f},
            {.42f, -.88f, .54f, .29f, 2.2f},
            {.31f, .83f, .55f, .32f, 2.5f},
            {-.56f, .17f, .65f, .61f, 1.5f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Kliszow: {
        // The Nida floodplain reaches the western flank. The eastern approach stays dry.
        static constexpr Obstacle areas[] = {
            {-1.f, -.96f, -.63f, -.58f, false, Feature::Marsh},
            {-.63f, -.97f, -.18f, -.51f, false, Feature::Marsh},
            {-.18f, -.98f, .29f, -.44f, false, Feature::Marsh},
            {.29f, -.98f, .70f, -.55f, false, Feature::Marsh},
            {.70f, -.98f, 1.f, -.66f, false, Feature::Marsh},
            {-.13f, -.52f, .05f, -.26f, false, Feature::Marsh},
            {-.09f, -.26f, .01f, .04f, false, Feature::Marsh},
            {-.93f, -.46f, -.54f, -.19f, false, Feature::Forest},
            {-.85f, .57f, -.42f, .87f, false, Feature::Forest},
            {.54f, -.57f, .88f, -.32f, false, Feature::Forest},
            {-.41f, .29f, .72f, .35f, false, Feature::Road},
        };
        static constexpr HeightLobe heights[] = {
            {-.66f, .37f, .57f, .62f, 9.f},
            {.31f, -.12f, .34f, .51f, 8.f},
            {-.06f, -.32f, .18f, .50f, -2.3f},
            {.13f, -.77f, 1.23f, .45f, -3.3f},
            {.75f, .58f, .65f, .48f, 3.f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Bouvines: {
        // The French turn back onto the cultivated plateau, leaving the bridge at their rear.
        static constexpr Obstacle areas[] = {
            {-.77f, -1.f, -.70f, -.52f, false, Feature::River},
            {-.71f, -.52f, -.65f, -.08f, false, Feature::River},
            {-.71f, -.08f, -.64f, .08f, false, Feature::Bridge},
            {-.70f, .08f, -.64f, .48f, false, Feature::River},
            {-.75f, .48f, -.68f, 1.f, false, Feature::River},
            {-.64f, -.90f, -.49f, -.26f, false, Feature::Marsh},
            {-.63f, .25f, -.48f, .86f, false, Feature::Marsh},
            {-1.f, -.05f, -.71f, .05f, false, Feature::Road},
            {-.64f, -.05f, .13f, .05f, false, Feature::Road},
            {-.41f, -.74f, .06f, -.57f, false, Feature::PloughedField},
            {-.18f, .47f, .42f, .68f, false, Feature::PloughedField},
            {.51f, -.32f, .88f, -.08f, false, Feature::PloughedField},
            {.56f, .66f, .95f, .94f, false, Feature::Forest},
        };
        static constexpr HeightLobe heights[] = {
            {-.71f, .02f, .21f, 1.24f, -3.7f},
            {-.80f, -.66f, .25f, .40f, -1.2f},
            {.09f, -.07f, .73f, 1.01f, 5.5f},
            {.62f, .09f, .56f, .76f, 2.6f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Hastings: {
        // Senlac runs across the field. The Normans start below the crest, on the Telham side.
        static constexpr Obstacle areas[] = {
            {-.43f, -.90f, -.12f, -.57f, false, Feature::Marsh},
            {-.23f, -.82f, .01f, -.60f, false, Feature::Marsh},
            {-.46f, .56f, -.21f, .83f, false, Feature::Marsh},
            {-.22f, .62f, -.02f, .91f, false, Feature::Marsh},
            {.39f, -.89f, .85f, -.59f, false, Feature::Forest},
            {.38f, .58f, .84f, .88f, false, Feature::Forest},
            {.63f, -.58f, .98f, .32f, false, Feature::Forest},
        };
        static constexpr HeightLobe heights[] = {
            {.16f, 0.f, .39f, .76f, 17.f},
            {-.75f, -.03f, .48f, .80f, 6.f},
            {-.20f, -.74f, .34f, .25f, -2.3f},
            {-.19f, .77f, .36f, .27f, -2.f},
            {.57f, -.53f, .27f, .28f, -2.5f},
            {.58f, .56f, .29f, .27f, -2.2f},
        };
        return {areas, heights, Crest{.16f, 0.f, .075f, .34f}};
    }
    case MarchTerrain::Patay: {
        // A dry swale precedes the hedge-lined track at the edge of the Lignerolles wood.
        static constexpr Obstacle areas[] = {
            {-.60f, -.025f, -.07f, .030f, false, Feature::Road},
            {-.07f, -.033f, .51f, .023f, false, Feature::Road},
            {-.08f, -.164f, .067f, -.141f, true, Feature::Hedge},
            {-.02f, .144f, .052f, .168f, true, Feature::Hedge},
            {.052f, .130f, .107f, .152f, true, Feature::Hedge},
            {-.71f, -.54f, -.36f, -.18f, false, Feature::PloughedField},
            {-.65f, .24f, -.30f, .62f, false, Feature::PloughedField},
            {.29f, .34f, .72f, .73f, false, Feature::Forest},
            {.51f, .11f, .91f, .58f, false, Feature::Forest},
            {.56f, -.65f, .89f, -.33f, false, Feature::Forest},
        };
        static constexpr HeightLobe heights[] = {
            {-.41f, -.42f, .14f, .66f, -2.3f},
            {-.32f, .44f, .15f, .67f, -2.2f},
            {.44f, .47f, .56f, .60f, 3.4f},
            {-.80f, -.03f, .59f, .77f, 1.6f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Bosworth: {
        // The fighting centers on Fenn Lane beside the Redemore wetland, below Ambion Hill.
        static constexpr Obstacle areas[] = {
            {-1.f, -.075f, -.42f, -.019f, false, Feature::Road},
            {-.42f, -.052f, .22f, .008f, false, Feature::Road},
            {.22f, -.028f, .65f, .032f, false, Feature::Road},
            {.65f, -.006f, 1.f, .048f, false, Feature::Road},
            {-.24f, -.49f, .14f, -.32f, false, Feature::Marsh},
            {-.15f, -.33f, .24f, -.17f, false, Feature::Marsh},
            {.02f, -.52f, .32f, -.34f, false, Feature::Marsh},
            {.20f, -.43f, .48f, -.24f, false, Feature::Marsh},
            {-.61f, .48f, -.26f, .74f, false, Feature::PloughedField},
            {.37f, .37f, .70f, .60f, false, Feature::PloughedField},
        };
        static constexpr HeightLobe heights[] = {
            {.10f, -.35f, .60f, .48f, -2.8f},
            {.89f, .33f, .62f, .70f, 8.2f},
            {-.47f, .78f, .55f, .51f, 3.3f},
            {-.54f, -.77f, .72f, .37f, 2.1f},
        };
        return {areas, heights};
    }
    case MarchTerrain::Castillon: {
        // Under Drouyn's eastern hypothesis an old Lidoire channel backs the camp on the floodplain.
        static constexpr Obstacle areas[] = {
            {-.84f, -.89f, -.77f, -.68f, false, Feature::River},
            {-.80f, -.68f, -.72f, -.12f, false, Feature::River},
            {-.74f, -.12f, -.68f, .32f, false, Feature::River},
            {-.81f, .32f, -.75f, .83f, false, Feature::River},
            {-1.f, -.96f, -.37f, -.85f, false, Feature::River},
            {-.37f, -.99f, .25f, -.88f, false, Feature::River},
            {.25f, -1.f, 1.f, -.91f, false, Feature::River},
            {-.65f, -.52f, .002f, .49f, false, Feature::RedoubtInterior},
            {-.026f, -.47f, .004f, -.132f, true, Feature::RedoubtBank},
            {-.026f, -.132f, .034f, .12f, false, Feature::RedoubtRamp},
            {-.026f, .12f, .004f, .45f, true, Feature::RedoubtBank},
            {-.20f, -.51f, -.026f, -.478f, true, Feature::RedoubtBank},
            {-.26f, .45f, -.026f, .484f, true, Feature::RedoubtBank},
            {.018f, -.46f, .061f, -.147f, false, Feature::Marsh},
            {.018f, .135f, .061f, .44f, false, Feature::Marsh},
            {-.65f, -.057f, -.026f, .049f, false, Feature::Road},
            {.41f, .52f, .75f, .74f, false, Feature::PloughedField},
        };
        static constexpr HeightLobe heights[] = {
            {-.741f, .08f, .17f, .87f, -1.5f},
            {.08f, -.91f, 1.17f, .27f, -1.9f},
            {-.21f, .03f, .50f, .68f, 1.3f},
            {.61f, .59f, .58f, .46f, 1.8f},
        };
        return {areas, heights};
    }
    default:
        return {};
    }
}

} // namespace battle::historical
