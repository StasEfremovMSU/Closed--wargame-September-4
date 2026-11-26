#pragma once
#include "World.h"
namespace battle {
class NavigationService;
bool CanCommandRegiment(const World &, UnitId, RegimentId);
Status AcquireControl(World &, UnitId);
Status ReleaseControl(World &, UnitId);
Status SetExternalPose(World &, UnitId, Pose, Tick);
Status ApplyControlCommand(World &, const Command &);
void ClearRegimentFollowing(World &, UnitId);
bool UpdateRegimentFollowing(World &, RegimentState &, const NavigationService &);
void ClearPersonalActions(UnitState &);
void AdvanceControlled(World &, float dt_seconds);
} // namespace battle
