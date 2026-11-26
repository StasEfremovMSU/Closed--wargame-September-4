#pragma once

#include "RegimentMissionAuthoring.h"
#include "Widgets/SWidget.h"

TSharedRef<SWidget> MakeMissionDeploymentMap(const TOptional<FMissionAuthoringPreview>* Preview);
