#pragma once
#include "World.h"
namespace battle {
float HitProbability(float base_accuracy, float skill, float distance, float range);
void BuildCombatEvents(const World &, std::vector<BattleEvent> &,
                       uint64_t *neighbor_checks = nullptr);
void CommitCombatEvents(World &, std::span<const BattleEvent>);
void UpdateMorale(World &);
} // namespace battle
