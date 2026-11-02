#include "battle/Control.h"
#include "battle/Formation.h"
namespace battle {
namespace {
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
} // namespace
Status ReleaseControl(World &w, UnitId id) {
    auto *u = FindUnit(w, id);
    if (!u || !u->alive)
        return Status::NotFound;
    if (w.controlled != id)
        return Status::Conflict;
    u->control = ControlMode::Returning;
    u->desired_velocity = {};
    u->attack_target = {};
    w.controlled = {};
    Changed(w, id, "returning to formation");
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
    if (w.controlled != c.unit)
        return Status::Conflict;
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
            FindRegiment(w, t->regiment)->team == FindRegiment(w, u->regiment)->team)
            return Status::NotFound;
        u->attack_target = c.target;
        return Status::Ok;
    }
    return Status::InvalidInput;
}
void AdvanceControlled(World &w, float dt) {
    auto *u = FindUnit(w, w.controlled);
    if (!u || !u->alive)
        return;
    Pose next = u->pose;
    next.position = next.position + u->desired_velocity * dt;
    if (Length(u->desired_velocity) > 0.00001f)
        next.yaw = std::atan2(u->desired_velocity.y, u->desired_velocity.x);
    float radius = FindRegiment(w, u->regiment)->formation.body_radius_m;
    float fraction = 1;
    for (auto &box : w.scenario.obstacles) {
        Pose p{{(box.min_x + box.max_x) * .5f, (box.min_y + box.max_y) * .5f, u->pose.position.z},
               0};
        fraction = std::min(fraction, SweepLimit(u->pose, next, {radius * 2, radius * 2}, p,
                                                 {box.max_y - box.min_y, box.max_x - box.min_x}));
    }
    u->pose.position = u->pose.position + (next.position - u->pose.position) * fraction;
    u->pose.yaw = next.yaw;
}
} // namespace battle
