#pragma once
#include "Navigation.h"
#include "World.h"
namespace battle {
struct FormationTranslation {
    std::vector<RegimentId> members;
    Vec3 delta{};
};
struct TimedFormationPose {
    float seconds{};
    Pose pose{};
    FormationFootprint footprint{};
};
struct FormationRouteConflict {
    float seconds{};
    Vec3 position{};
};
std::optional<FormationRouteConflict> FirstFormationRouteConflict(std::span<const TimedFormationPose>,
                                                                 std::span<const TimedFormationPose>);
bool CanTranslateFormationGroup(const World &, std::span<const RegimentId>, Vec3 delta);
// Waypoints belong to the first member. The others traverse them at their original offsets.
PathResult FindFormationGroupRoute(const NavigationService &, const World &,
                                   std::span<const RegimentId>, Vec3 delta);
std::vector<FormationTranslation> ActiveFormationTranslations(const World &);
void OpenSkirmishSpacing(FormationSpec &);
std::vector<Pose> BuildFormationSlots(const FormationSpec &, Pose centre, uint32_t count);
FormationFootprint Footprint(const FormationSpec &, uint32_t count);
FormationFootprint Footprint(const RegimentState &);
bool IsFormationFrontRank(const RegimentState &, const UnitState &);
bool IsAtFormationPost(const RegimentState &, const UnitState &);
bool CanUseFormationShape(FormationShape, const UnitSpec &);
bool UsesLinearInfantry(const World &, const RegimentState &);
bool IsLinearFiringLine(const RegimentState &);
FormationSpec LinearInfantryFormation(FormationSpec, uint32_t count, bool firing_line);
FormationSpec LinearInfantryFormation(const RegimentState &, bool firing_line);
float FormationTravelSpeed(const World &, const RegimentState &);
float FormationTravelSpeed(const World &, const RegimentState &, OrderType, MovementPace);
bool PreserveFallbackFacing(const World &, const RegimentState &, Vec3 destination);
bool RectanglesOverlap(Pose, FormationFootprint, Pose, FormationFootprint);
float SweepLimit(Pose from, Pose to, FormationFootprint, Pose obstacle, FormationFootprint);
void InitializeFormations(World &);
bool IsDeploying(const World &, const RegimentState &);
bool IsRecoveringFromMelee(const World &, const RegimentState &);
void CompleteDeployments(World &);
Status ReformRegiment(World &, RegimentId, FormationSpec);
bool SupportsRankPriority(const World &, const RegimentState &);
Status SetRegimentRankPriority(World &, RegimentId, RankPriority);
void UpdateRegimentRankLayout(World &, RegimentState &, RankPriority automatic);
void AdvanceFormations(World &, float dt_seconds);
Status SetSkirmishers(World &, RegimentId, bool enabled);
} // namespace battle
