#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Navigation.h"
#include "battle/command/Services.h"
#include <limits>
namespace battle {
namespace {
constexpr Tick JumpDurationTicks = 12;
constexpr float JumpHeightM = .78f;
constexpr Tick ParryWindowTicks = 6;
constexpr Tick ParryCooldownTicks = 13;

void ClearJump(UnitState &u) {
    if (u.jump_started_tick)
        u.pose.position.z = u.jump_base_z;
    u.jump_started_tick = 0;
    u.jump_base_z = 0.f;
}

void EndParry(UnitState &u) {
    u.parry_until = 0;
    u.parry_yaw = 0.f;
}

void Changed(World &w, UnitId id, std::string reason) {
    auto *u = FindUnit(w, id);
    w.events.push_back({{w.next_event_id++},
                        w.tick,
                        EventKind::ControlChanged,
                        {},
                        id,
                        u ? u->regiment : RegimentId{},
                        0,
                        std::move(reason)});
}
void ClearFollowRoute(RegimentState &r) {
    r.route.clear();
    r.waypoint = 0;
    r.retreat_waypoint.reset();
}

void StopFollowing(RegimentState &r) {
    if (r.follow_replan_tick)
        ClearFollowRoute(r);
    r.following_officer = {};
    r.follow_replan_tick = 0;
    r.decision_tick = 0;
    r.decision_until = 0;
}
} // namespace

bool CanCommandRegiment(const World &w, UnitId unit, RegimentId regiment) {
    const auto *u = FindUnit(w, unit);
    const auto *r = FindRegiment(w, regiment);
    if (!u || !u->alive || !r || UnitTeam(w, *u) != r->team)
        return false;
    const auto officer = std::ranges::find(w.officers, unit, &OfficerSpec::unit);
    if (officer == w.officers.end() || (officer->role != 1 && officer->role != 2))
        return false;
    if (u->regiment == regiment)
        return true;
    if (!w.command_state)
        return false;

    const auto &nodes = w.command_state->hierarchy.nodes;
    const auto leaf = std::ranges::find(nodes, regiment, &command::CommandNode::regiment);
    auto *node = leaf != nodes.end() ? &*leaf : nullptr;
    for (size_t i = 0; node && i < nodes.size(); ++i) {
        if (node->commander == officer->id)
            return true;
        node = node->parent ? command::FindNode(w.command_state->hierarchy, node->parent) : nullptr;
    }
    return false;
}

void ClearRegimentFollowing(World &w, UnitId officer) {
    if (!officer)
        return;
    for (auto &r : w.regiments)
        if (r.following_officer == officer)
            StopFollowing(r);
}

bool UpdateRegimentFollowing(World &w, RegimentState &r, const NavigationService &nav) {
    if (!r.following_officer)
        return false;
    const auto *leader = FindUnit(w, r.following_officer);
    if (r.units.empty() || w.controlled != r.following_officer || !leader ||
        leader->control != ControlMode::PlayerControlled ||
        !CanCommandRegiment(w, r.following_officer, r.id)) {
        StopFollowing(r);
        return false;
    }

    if (r.panic || IsDeploying(w, r)) {
        if (r.follow_replan_tick) {
            ClearFollowRoute(r);
            r.follow_replan_tick = 0;
            r.decision_tick = 0;
            r.decision_until = 0;
        }
        return false;
    }

    if (!r.follow_replan_tick || r.melee_engaged)
        ClearFollowRoute(r);
    r.ai_action = OrderType::Hold;
    r.ai_target = {};
    if (r.melee_engaged) {
        r.follow_replan_tick = 0;
        return true;
    }
    if (w.tick < r.follow_replan_tick)
        return true;

    const float distance = std::max(2.f, Footprint(r).depth_m * .5f + r.formation.spacing_m);
    Pose goal{leader->pose.position - Forward(leader->pose.yaw) * distance, leader->pose.yaw};
    goal.position.z = r.pose.position.z;
    const auto yaw_error = [](Pose a, Pose b) {
        return std::abs(std::remainder(a.yaw - b.yaw, 2 * Pi));
    };
    constexpr Tick ReplanInterval = 10;
    r.follow_replan_tick = std::min(w.tick, std::numeric_limits<Tick>::max() - ReplanInterval) +
                           ReplanInterval;
    const bool in_transit = r.waypoint < r.route.size();
    if (!in_transit && Distance(r.pose.position, goal.position) < .35f &&
        yaw_error(r.pose, goal) < .08f)
        return true;
    if (in_transit && Distance(r.route.back().position, goal.position) < .75f &&
        yaw_error(r.route.back(), goal) < .12f &&
        FormationRouteSegmentClear(w, r, r.route[r.waypoint]))
        return true;

    auto next = FindFormationRoute(nav, w, r, goal);
    r.route = next.status == Status::Ok ? std::move(next.waypoints) : std::vector<Pose>{};
    r.waypoint = 0;
    return true;
}

void ClearPersonalActions(UnitState &u) {
    ClearJump(u);
    EndParry(u);
    u.parry_ready_tick = 0;
}

Status ReleaseControl(World &w, UnitId id) {
    auto *u = FindUnit(w, id);
    if (!u || !u->alive)
        return Status::NotFound;
    if (w.controlled != id)
        return Status::Conflict;
    ClearJump(*u);
    EndParry(*u);
    u->control = u->regiment ? ControlMode::Returning : ControlMode::FormationControlled;
    if (!u->regiment)
        u->slot = u->pose;
    u->desired_velocity = {};
    u->attack_target = {};
    ClearRegimentFollowing(w, id);
    w.controlled = {};
    Changed(w, id, u->regiment ? "returning to formation" : "returned to group command");
    return Status::Ok;
}
Status AcquireControl(World &w, UnitId id) {
    auto *u = FindUnit(w, id);
    if (!u || !u->alive)
        return Status::NotFound;
    if (w.controlled == id)
        return Status::Ok;
    if (w.controlled)
        ReleaseControl(w, w.controlled);
    u->control = ControlMode::PlayerControlled;
    u->desired_velocity = {};
    u->attack_target = {};
    w.controlled = id;
    Changed(w, id, "player acquired control");
    return Status::Ok;
}
Status SetExternalPose(World &w, UnitId id, Pose pose, Tick tick) {
    if (tick != w.tick || !Finite(pose))
        return Status::InvalidInput;
    auto *u = FindUnit(w, id);
    if (!u || !u->alive)
        return Status::NotFound;
    if (w.controlled != id || u->control != ControlMode::PlayerControlled)
        return Status::Conflict;
    if (u->platform == BattlePlatform::Ballista && pose.position != u->pose.position)
        return Status::Conflict;
    ClearJump(*u);
    EndParry(*u);
    u->pose = pose;
    u->desired_velocity = {};
    return Status::Ok;
}
Status ApplyControlCommand(World &w, const Command &c) {
    if (c.kind == CommandKind::AcquireControl)
        return AcquireControl(w, c.unit);
    if (c.kind == CommandKind::ReleaseControl)
        return ReleaseControl(w, c.unit);
    if (c.kind == CommandKind::ExternalPoseSample)
        return SetExternalPose(w, c.unit, c.pose, c.sample_tick);
    auto *u = FindUnit(w, c.unit);
    if (!u || !u->alive)
        return Status::NotFound;
    if (w.controlled != c.unit || u->control != ControlMode::PlayerControlled)
        return Status::Conflict;
    if (c.kind == CommandKind::FaceControlledUnit) {
        if (!std::isfinite(c.pose.yaw))
            return Status::InvalidInput;
        u->pose.yaw = std::remainder(c.pose.yaw, 2 * Pi);
        u->desired_velocity = {};
        return Status::Ok;
    }
    if (c.kind == CommandKind::SetRegimentFollowing) {
        auto *r = FindRegiment(w, c.regiment);
        if (!r || r->units.empty())
            return Status::NotFound;
        if (!CanCommandRegiment(w, c.unit, r->id))
            return Status::Conflict;
        if (c.follow_enabled && r->following_officer != c.unit) {
            ClearRegimentFollowing(w, c.unit);
            r->following_officer = c.unit;
        } else if (!c.follow_enabled && r->following_officer == c.unit)
            StopFollowing(*r);
        return Status::Ok;
    }
    if (c.kind == CommandKind::JumpControlledUnit) {
        if (u->platform != BattlePlatform::None || u->jump_started_tick ||
            w.tick + 1 < u->parry_until)
            return Status::Conflict;
        u->jump_started_tick = w.tick + 1;
        u->jump_base_z = u->pose.position.z;
        return Status::Ok;
    }
    if (c.kind == CommandKind::ParryControlledUnit) {
        if (!std::isfinite(c.pose.yaw))
            return Status::InvalidInput;
        if (u->platform != BattlePlatform::None || u->jump_started_tick ||
            w.tick < u->parry_ready_tick)
            return Status::Conflict;
        // Impacts carry the result tick. Store the first result tick outside the window.
        u->parry_until = w.tick + 1 + ParryWindowTicks;
        u->parry_ready_tick = w.tick + ParryCooldownTicks;
        u->parry_yaw = std::remainder(c.pose.yaw, 2 * Pi);
        return Status::Ok;
    }
    if (c.kind == CommandKind::MoveControlledUnit) {
        float n = Length(c.velocity);
        u->desired_velocity = n > u->speed ? c.velocity * (u->speed / n) : c.velocity;
        return Status::Ok;
    }
    if (c.kind == CommandKind::AttackControlledUnit) {
        if (!c.target) {
            u->attack_target = {};
            return Status::Ok;
        }
        auto *t = FindUnit(w, c.target);
        if (!t || !t->alive ||
            UnitTeam(w, *t) == UnitTeam(w, *u))
            return Status::NotFound;
        u->attack_target = c.target;
        return Status::Ok;
    }
    return Status::InvalidInput;
}
void AdvanceControlled(World &w, float dt) {
    auto *u = FindUnit(w, w.controlled);
    if (!u || !u->alive || u->platform == BattlePlatform::Ballista)
        return;
    Pose next = u->pose;
    next.position = next.position + u->desired_velocity * dt;
    const float speed = Length(u->desired_velocity);
    const float terrain_speed = w.scenario.hill
        ? HillUphillSpeedMultiplier(*w.scenario.hill, u->pose.position, next.position, u->troop, speed)
        : TerrainSpeedMultiplier(u->pose.position, next.position, w.scenario.obstacles, u->troop, speed);
    next.position = u->pose.position + (next.position - u->pose.position) * terrain_speed;
    if (speed > 0.00001f)
        next.yaw = std::atan2(u->desired_velocity.y, u->desired_velocity.x);
    if (u->parry_until && w.tick + 1 >= u->parry_until)
        EndParry(*u);
    if (u->parry_until)
        next.yaw = u->parry_yaw;
    if (u->parry_ready_tick && w.tick + 1 >= u->parry_ready_tick)
        u->parry_ready_tick = 0;

    const auto *regiment = FindRegiment(w, u->regiment);
    float radius = regiment ? regiment->formation.body_radius_m : .3f;
    float fraction = 1;
    for (auto &box : w.scenario.obstacles) {
        if (!TerrainBlocksMovement(box))
            continue;
        Pose p{{(box.min_x + box.max_x) * .5f, (box.min_y + box.max_y) * .5f, u->pose.position.z},
               0};
        fraction = std::min(fraction, SweepLimit(u->pose, next, {radius * 2, radius * 2}, p,
                                                 {box.max_y - box.min_y, box.max_x - box.min_x}));
    }
    u->pose.position = u->pose.position + (next.position - u->pose.position) * fraction;
    u->pose.yaw = next.yaw;
    if (u->jump_started_tick) {
        const Tick elapsed = w.tick + 2 - u->jump_started_tick;
        if (elapsed >= JumpDurationTicks)
            ClearJump(*u);
        else {
            const float phase = float(elapsed) / float(JumpDurationTicks);
            u->pose.position.z = u->jump_base_z + 4.f * JumpHeightM * phase * (1.f - phase);
        }
    }
}
} // namespace battle
