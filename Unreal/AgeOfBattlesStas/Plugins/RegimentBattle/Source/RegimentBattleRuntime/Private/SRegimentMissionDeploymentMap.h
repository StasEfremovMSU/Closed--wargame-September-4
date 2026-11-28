#pragma once

#include "RegimentMissionAuthoring.h"
#include "Widgets/SWidget.h"

enum class EMissionMapView : uint8 { Authoring, Terrain, Opening };

TSharedRef<SWidget> MakeMissionDeploymentMap(const TOptional<FMissionAuthoringPreview>* Preview,
    EMissionMapView View = EMissionMapView::Authoring);
