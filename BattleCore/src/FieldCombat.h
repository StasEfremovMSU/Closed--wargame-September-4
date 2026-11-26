#pragma once

#include "battle/World.h"

namespace battle {
void UpdateFieldPosture(World &);
bool ChargeReady(const World &, const RegimentState &, const UnitState &, const UnitState &);
void RegisterCharge(World &, RegimentState &);
bool DisciplineTest(const World &, const RegimentState &, float probability, uint64_t salt);
bool SmokeObscures(const World &, Vec3 from, Vec3 to);
} // namespace battle
