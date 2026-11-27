#pragma once
#include "battle/World.h"

namespace battle {
class LocalMelee;
inline constexpr float RallySlotArrivalM = .08f;
std::vector<Pose> RoutWaypoints(Vec3 start, std::span<const Pose> path);
void AdvanceIndividualRout(World &, RegimentState &, float seconds, const LocalMelee *);
}
