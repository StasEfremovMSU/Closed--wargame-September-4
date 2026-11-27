#pragma once
#include "battle/Spatial.h"
#include "battle/World.h"

namespace battle {
class LocalMelee {
  public:
    LocalMelee(const World &, float dt);
    bool Engaged(const RegimentState &) const;
    bool DaggerDrawn(const UnitState &) const;
    bool CanAcquire(const UnitState &) const;
    bool RangedTargetAllowed(const UnitState &, bool skirmisher_shooter) const;
    void Refresh(World &);
    void AdvanceSkirmisher(UnitState &, const RegimentState &, float dt) const;
    void AdvanceLine(World &, const RegimentState &, float dt) const;
    bool HoldForWindup(UnitState &) const;
    bool CanYieldForOfficer(const RegimentState &) const;
    Vec3 OfficerClearanceGoal(const UnitState &, const RegimentState &) const;
    Vec3 Constrain(const UnitState &, Vec3 destination, float march_speed = 0.f, float dt = 0.f) const;
    float BodyRadius(const UnitState &) const;
    float MeleeReach(const UnitState &, const UnitState &, float weapon_range) const;
    const UnitState *StrikeTarget(const UnitState &, int32_t team) const;
    bool CanStrike(const UnitState &, const UnitState &, float range) const;
    void QueueCavalryShove(const UnitState &, const UnitState &, float damage, AttackAspect) const;
    void ApplyShoves(World &) const;

  private:
    bool ClearMove(const UnitState &, Vec3 destination) const;
    bool ClearMove(const UnitState &, Vec3 destination, std::span<const size_t> neighbors,
                   const UnitState **blocker = nullptr) const;
    std::optional<Vec3> BypassStaff(const UnitState &, const UnitState &staff, Vec3 from,
                                    Vec3 travel, float speed, float dt) const;
    bool ClearStrike(const UnitState &, const UnitState &, std::span<const size_t>) const;
    bool MoveTowards(UnitState &, Vec3 goal, float step, float turn_dt = 0.f,
                     bool allow_detour = false) const;
    void DetectContact();
    void DetectCoarseCandidates(float dt);
    Vec3 ScreenWaypoint(const UnitState &, Vec3 goal, Vec3 fallback) const;
    float FightingDistance(const UnitState &, const UnitState &) const;
    const UnitState *WindupTarget(const UnitState &) const;
    const UnitState *EnemyAhead(const UnitState &, const RegimentState &, const Pose &lane,
                                bool adjacent_lanes = false) const;
    struct OpposingFront {
        float near_depth, far_depth, min_lateral, max_lateral;
        float body_radius;
    };
    std::optional<OpposingFront> FindOpposingFront(const RegimentState &, size_t columns) const;
    bool AdvanceWing(UnitState &, const RegimentState &, const Pose &lane,
                     const OpposingFront &, float dt, float looseness, float wing) const;
    const UnitState *AdvanceLead(UnitState &, const RegimentState &, const Pose &lane,
                     const std::optional<OpposingFront> &, float dt,
                     float looseness = 0.f, float wing = 0.f) const;
    void AdvanceSupport(UnitState &, const UnitState &lead, const RegimentState &, float edge,
                        float dt) const;
    void QueueRankShove(const UnitState &, const UnitState &support, const UnitState &,
                        const RegimentState &, Vec3 forward, float dt) const;
    struct Shove {
        size_t source, target;
        float distance;
        bool impact;
    };
    struct ShoveClearance {
        float distance;
        std::optional<size_t> blocker;
    };
    ShoveClearance SweepShove(const UnitState &, Vec3 direction, float distance) const;
    void ShoveUnit(World &, size_t, Vec3 direction, float distance, float budget, bool pass_on) const;
    void Neighbors(Vec3, float range, std::optional<int32_t> excluded_team = {}) const;
    size_t UnitIndex(const UnitState &) const;
    const RegimentState *Regiment(const UnitState &) const;
    int32_t Team(const UnitState &) const;

    const World &world_;
    struct CoarseGroup {
        Vec3 min{}, max{};
        size_t count{};
        int32_t team{};
        float max_range{}, max_step{};
        bool can_contact{}, can_acquire{}, clear_march{};
    };
    mutable SpatialIndex index_{2.f};
    mutable std::vector<UnitPosition> unindexed_;
    std::vector<Vec3> start_;
    std::vector<const RegimentState *> unit_regiments_;
    std::vector<float> radii_;
    std::vector<UnitId> windup_targets_;
    std::vector<bool> engaged_, personal_contact_;
    std::vector<RegimentId> opponents_;
    std::vector<CoarseGroup> coarse_groups_;
    std::vector<size_t> unit_groups_;
    const UnitState *passing_officer_{};
    Vec3 officer_heading_{};
    float officer_lookahead_{};
    float officer_clearance_radius_{};
    mutable std::vector<size_t> nearby_;
    mutable std::vector<Shove> shoves_;
    mutable std::vector<float> shoved_;
    mutable float shove_margin_{};
    float max_radius_{};
    float max_platform_radius_{};
    float max_step_{};
};

WeaponSpec DaggerWeapon(const UnitState &);
void UpdateSkirmishers(World &);
float UnitTerrainLimit(const World &, Vec3 start, Vec3 end, float radius);
void AdvanceFormations(World &, float dt, const LocalMelee *);
void BuildCombatEvents(const World &, std::vector<BattleEvent> &, uint64_t *checks,
                       const LocalMelee *, std::span<const UnitId> moving_siege = {});
void CommitCombatEvents(World &, std::span<const BattleEvent>, const LocalMelee *);
} // namespace battle
