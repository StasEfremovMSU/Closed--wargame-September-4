#pragma once
#include "World.h"
namespace battle {
Status AcquireControl(World &, UnitId);
Status ReleaseControl(World &, UnitId);
Status SetExternalPose(World &, UnitId, Pose, Tick);
Status ApplyControlCommand(World &, const Command &);
void AdvanceControlled(World &, float dt_seconds);
} // namespace battle
