#pragma once
#include "battle/World.h"

namespace battle::command {
void AdvanceArmyStaff(World &, const NavigationService &);
bool HasLivingStaffAdjutant(const World &, NodeId army);
bool IsArmyStaffTarget(const World &, const UnitState &);
const UnitState *BodyguardForProjectile(const World &, const UnitState &attacker,
                                       const UnitState &commander);
} // namespace battle::command
