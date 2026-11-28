#pragma once
#include "World.h"
#include <string_view>
namespace battle {
inline constexpr std::string_view FieldGunCanisterReason = "field gun canister";
inline constexpr std::string_view FieldGunRoundShotReason = "field gun round shot";
inline constexpr std::string_view FieldGunRejectedReason = "field gun unavailable or target blocked";
inline constexpr std::string_view OfficerArrowDodgeReason = "officer moved clear of arrow";
inline constexpr float FieldGunOfficerReachM = 14.f;
enum class FireLaneMask : uint8_t { Clear, Cover, Terrain, Friendly, Melee, Risky };
struct FireSectorSegment {
    Vec3 from, to;
    FireLaneMask mask{};
};
// A sampled fan from the frontmost central available archer. Shares targeting geometry with combat.
std::vector<FireSectorSegment> SampleFireSector(const World &, const RegimentState &,
                                                unsigned rays_per_side = 6, unsigned samples = 24);
bool SmokeObscures(const World &, Vec3 from, Vec3 to);
uint32_t RangedPhase(UnitId, RegimentId, uint32_t cohorts);
float HitProbability(float base_accuracy, float skill, float distance, float range);
// Bearing from the defender to the attacker at their current positions.
AttackAspect ClassifyAttackAspect(const World &, const UnitState &attacker,
                                  const UnitState &defender);
void BuildCombatEvents(const World &, std::vector<BattleEvent> &,
                       uint64_t *neighbor_checks = nullptr);
class LocalMelee;
void BuildCombatEvents(const World &, std::vector<BattleEvent> &, uint64_t *neighbor_checks,
                       const LocalMelee *, std::span<const UnitId> moving_siege, const Command *field_gun_shot);
void CommitCombatEvents(World &, std::span<const BattleEvent>);
void UpdateMorale(World &);
} // namespace battle
