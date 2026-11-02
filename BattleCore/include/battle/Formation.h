#pragma once
#include "Navigation.h"
#include "World.h"
namespace battle {
std::vector<Pose> BuildFormationSlots(const FormationSpec &, Pose centre, uint32_t count);
FormationFootprint Footprint(const RegimentState &);
bool RectanglesOverlap(Pose, FormationFootprint, Pose, FormationFootprint);
float SweepLimit(Pose from, Pose to, FormationFootprint, Pose obstacle, FormationFootprint);
void InitializeFormations(World &);
Status ReformRegiment(World &, RegimentId, FormationSpec);
void AdvanceFormations(World &, float dt_seconds);
} // namespace battle
