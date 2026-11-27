#include "CommandBrainCommon.h"
namespace battle::command {
StubResult<DecisionProposal> RegimentBrain::Think(const BrainContext &c) const {
    using namespace detail;
    auto prepared = Prepare(c, Level::Regiment);
    if (prepared.status != StubStatus::Ready)
        return {prepared.status, std::nullopt, prepared.reason};
    auto p = std::move(*prepared.value);
    TacticalOption option;
    option.destination = c.situation->own.pose.position;
    FsmState state = FsmState::Holding;
    TaskStatus status = TaskStatus::Active;
    std::string reason = "Hold pending orders.";
    auto finish = [&] {
        return StubResult<DecisionProposal>{StubStatus::Ready,
                                            Finish(c, p, state, status, option, reason),
                                            "Regiment FSM evaluated."};
    };
    auto fail = [&](const char *why) {
        state = FsmState::Failed;
        status = TaskStatus::Failed;
        option.action = TaskKind::HoldArea;
        option.target_regiment = {};
        option.destination = c.situation->own.pose.position;
        reason = why;
    };
    if (c.situation->own.panic) {
        bool arrived = Distance(c.situation->own.pose.position, c.situation->rally_point) <= 2;
        state = arrived ? FsmState::Rallying : FsmState::Withdrawing;
        option.action = arrived ? TaskKind::Rally : TaskKind::Withdraw;
        option.destination = c.situation->rally_point;
        reason = "Panic overrides ordinary orders.";
        return finish();
    }
    if (!c.node->commander) {
        fail("No live commander.");
        return finish();
    }
    if (p.expired) {
        state = FsmState::Complete;
        status = TaskStatus::Cancelled;
        reason = "Task expired.";
        return finish();
    }
    if (p.task && !p.new_task &&
        (p.memory.task_status == TaskStatus::Completed ||
         p.memory.task_status == TaskStatus::Failed)) {
        state = p.memory.state;
        status = p.memory.task_status;
        reason = "Terminal task awaits replacement.";
        return finish();
    }
    if (!p.task) {
        if (auto *target = Contact(c); target && AttackWorthwhile(c, *target)) {
            state = FsmState::Attacking;
            option.action = TaskKind::Attack;
            option.target_regiment = target->regiment;
            option.destination = target->estimated_pose.position;
            reason = "Authorized local opportunity.";
        }
        return finish();
    }
    const auto &task = *p.task;
    option.destination = task.intent.objective_position;
    float tolerance = std::max(1.f, task.intent.objective_radius_m);
    switch (task.action) {
    case TaskKind::HoldArea:
    case TaskKind::Defend:
    case TaskKind::Reserve:
        if (Distance(c.situation->own.pose.position, option.destination) > tolerance) {
            option.action = TaskKind::MoveToArea;
            state = FsmState::Deploying;
            reason = "Occupy assigned position.";
        } else {
            state = task.action == TaskKind::Reserve ? FsmState::Reserve : FsmState::Holding;
            reason = "Hold assigned position.";
        }
        break;
    case TaskKind::MoveToArea:
    case TaskKind::Screen:
    case TaskKind::Support: {
        const bool positional_support = task.action == TaskKind::Support && task.intent.target_command;
        if (task.action == TaskKind::Support && task.intent.target_regiment && !positional_support) {
            auto *friend_contact = Contact(c, task.intent.target_regiment, false);
            if (!friend_contact) {
                fail("Support recipient unavailable.");
                break;
            }
            option.destination = friend_contact->estimated_pose.position -
                                 Forward(friend_contact->estimated_pose.yaw) * 6;
        }
        if (Distance(c.situation->own.pose.position, option.destination) <= tolerance) {
            if (positional_support) {
                state = FsmState::Supporting;
                reason = "Hold the assigned supporting position.";
            } else {
                state = FsmState::Complete;
                status = TaskStatus::Completed;
                reason = "Destination reached.";
            }
        } else {
            option.action = TaskKind::MoveToArea;
            state = task.action == TaskKind::Support ? FsmState::Supporting : FsmState::Advancing;
            reason = "Move to assigned destination.";
        }
        break;
    }
    case TaskKind::Attack:
    case TaskKind::Exploit:
    case TaskKind::Flank: {
        auto *target = AttackContact(c, p);
        const auto planned_flank = task.action == TaskKind::Flank ? task.intent.flank_waypoint
                                                                  : std::nullopt;
        if (planned_flank && (target || task.authority.may_pursue)) {
            const auto own = c.situation->own.pose;
            const auto enemy = target ? target->regiment : task.intent.target_regiment;
            const bool ready = Distance(own.position, planned_flank->position) <= .35f &&
                std::abs(std::remainder(own.yaw - planned_flank->yaw, 2 * Pi)) < .2f;
            const bool contact = c.in_melee_with && c.in_melee_with(enemy);
            const bool committed = !p.new_task && (p.memory.state == FsmState::Attacking ||
                std::any_of(p.memory.recent_decisions.begin(), p.memory.recent_decisions.end(),
                            [&](const auto &decision) {
                    return decision.cause == task.id && decision.selected.action == TaskKind::Attack &&
                           decision.selected.target_regiment == enemy;
                }));
            if (!committed && !ready && !contact) {
                state = FsmState::Advancing;
                option.action = TaskKind::MoveToArea;
                option.destination = planned_flank->position;
                option.target_regiment = enemy;
                reason = "Advance to the assigned flank.";
                break;
            }
            if (!target || target->source == ObservationSource::Memory) {
                state = FsmState::Regrouping;
                option.action = TaskKind::HoldArea;
                option.destination = own.position;
                option.target_regiment = enemy;
                reason = "Watch for the assigned enemy from the flank.";
                break;
            }
        }
        if (!target) {
            if (!task.authority.may_pursue)
                fail("Target unavailable in known contacts.");
            else {
                state = FsmState::Regrouping;
                option.action = TaskKind::HoldArea;
                option.destination = c.situation->own.pose.position;
                reason = "Regroup and watch for the assigned enemy.";
            }
            break;
        } else if (!planned_flank && task.action == TaskKind::Flank) {
            Vec3 flank = target->estimated_pose.position +
                         Right(target->estimated_pose.yaw) * ((c.node->id.value % 2) ? 8.f : -8.f) -
                         Forward(target->estimated_pose.yaw) * 3;
            if ((p.new_task || p.memory.state != FsmState::Attacking) &&
                Distance(c.situation->own.pose.position, flank) > tolerance) {
                state = FsmState::Advancing;
                option.action = TaskKind::MoveToArea;
                option.destination = flank;
                reason = "Move to the observed enemy flank.";
                break;
            }
        }
        if (target->source == ObservationSource::Memory) {
            option.target_regiment = target->regiment;
            if (Distance(c.situation->own.pose.position, target->estimated_pose.position) <= tolerance) {
                state = FsmState::Regrouping;
                option.action = TaskKind::HoldArea;
                option.destination = c.situation->own.pose.position;
                reason = "Regroup and watch for the assigned enemy.";
            } else {
                option.action = TaskKind::MoveToArea;
                option.destination = target->estimated_pose.position;
                state = FsmState::Advancing;
                reason = "Advance toward the last known enemy position.";
            }
            break;
        }
        state = FsmState::Attacking;
        option.action = TaskKind::Attack;
        option.target_regiment = target->regiment;
        option.destination = !p.new_task && p.memory.state == FsmState::Attacking &&
                             p.memory.target == target->regiment
                                 ? p.memory.goal : target->estimated_pose.position;
        reason = planned_flank ? "Strike the enemy from the assigned flank."
                 : task.intent.target_regiment && target->regiment != task.intent.target_regiment
                     ? "Assigned enemy is gone. Attack a nearby observed enemy."
                 : p.memory.state == FsmState::Regrouping
                     ? "Contact restored. Resume the attack."
                     : "Attack observed assigned enemy.";
        break;
    }
    case TaskKind::Withdraw:
    case TaskKind::Rally:
        option.destination = c.situation->rally_point;
        if (Distance(c.situation->own.pose.position, option.destination) <= 2) {
            state = FsmState::Complete;
            status = TaskStatus::Completed;
            reason = "Rally point reached.";
        } else {
            state = FsmState::Withdrawing;
            option.action = task.action;
            reason = "Ordered withdrawal or rally.";
        }
        break;
    case TaskKind::Reform: {
        if (!task.formation_columns || !c.current_formation) {
            fail("Reform needs explicit formation parameters.");
            break;
        }
        state = FsmState::Regrouping;
        reason = "Apply assigned formation.";
        auto out = finish();
        FormationSpec formation = *c.current_formation;
        formation.columns = *task.formation_columns;
        formation.shape = FormationShape::Rectangle;
        if (task.formation_spacing_m)
            formation.spacing_m = *task.formation_spacing_m;
        out.value->regiment_directives = RegimentDirectives{formation, std::nullopt, std::nullopt};
        if (!p.new_task && formation == *c.current_formation && c.formation_ready)
            return {StubStatus::Ready,
                    Finish(c, p, FsmState::Complete, TaskStatus::Completed, option,
                           "Formation restored."),
                    "Regiment FSM evaluated."};
        return out;
    }
    }
    if (task.authority.initiative != InitiativePolicy::AwaitOrders &&
        task.authority.acceptable_loss_fraction && c.situation->own.initial &&
        1.f - float(c.situation->own.living) / float(c.situation->own.initial) >=
            *task.authority.acceptable_loss_fraction) {
        state = FsmState::Withdrawing;
        option.action = TaskKind::Withdraw;
        option.destination = c.situation->rally_point;
        option.target_regiment = {};
        reason = "Granted casualty limit reached.";
    }
    return finish();
}
} // namespace battle::command
