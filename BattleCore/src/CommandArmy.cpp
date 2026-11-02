#include "CommandBrainCommon.h"
#include <tuple>

namespace battle::command {
StubResult<DecisionProposal> ArmyBrain::Think(const BrainContext &c) const {
    using namespace detail;
    auto checked = Prepare(c, Level::Army);
    if (checked.status != StubStatus::Ready)
        return {checked.status, std::nullopt, checked.reason};

    auto p = std::move(*checked.value);
    auto status_of = [&](NodeId id) -> const ChildStatus & {
        return *std::find_if(c.children_status.begin(), c.children_status.end(),
                             [&](const auto &s) { return s.node == id; });
    };
    std::vector<const CommandNode *> children;
    for (const auto &child : c.subordinates)
        if (status_of(child.id).living)
            children.push_back(&child);
    std::sort(children.begin(), children.end(), [](auto a, auto b) {
        return std::tie(a->sector_role, a->id) < std::tie(b->sector_role, b->id);
    });

    TaskKind action = p.task ? p.task->action : TaskKind::HoldArea;
    TacticalOption option;
    option.action = action;
    option.destination =
        p.task ? p.task->intent.objective_position : c.situation->own.pose.position;
    bool attack =
        action == TaskKind::Attack || action == TaskKind::Exploit || action == TaskKind::Flank;
    const KnownContact *target =
        attack ? Contact(c, p.task ? p.task->intent.target_regiment : RegimentId{}) : nullptr;
    if (!p.task) {
        if (const auto *visible = Contact(c); visible && AttackWorthwhile(c, *visible)) {
            target = visible;
            attack = true;
            action = TaskKind::Attack;
        } else {
            action = TaskKind::HoldArea;
        }
    }

    bool threatened = c.situation->own.initial &&
                      double(c.situation->own.living) <= double(c.situation->own.initial) * .75;
    const CommandNode *supported = nullptr;
    for (const auto *child : children) {
        if (status_of(child->id).panic && child->sector_role != SectorRole::Reserve) {
            threatened = true;
            if (!supported)
                supported = child;
        } else if (status_of(child->id).panic) {
            threatened = true;
        }
    }
    for (const auto &report : c.reports)
        if (report.kind == ReportKind::NeedSupport)
            threatened = true;
    if (!supported)
        for (const auto *child : children)
            if (child->sector_role != SectorRole::Reserve) {
                supported = child;
                break;
            }

    FsmState state = FsmState::Holding;
    TaskStatus task_status = TaskStatus::Active;
    std::string reason = "Hold the battle line and protect the assigned sectors.";
    if (children.empty() || !c.node->commander) {
        state = FsmState::Failed;
        task_status = TaskStatus::Failed;
        action = TaskKind::HoldArea;
        attack = false;
        reason = "Army cannot execute without a commander and living parts.";
    } else if (p.expired) {
        state = FsmState::Complete;
        task_status = TaskStatus::Cancelled;
        action = TaskKind::HoldArea;
        attack = false;
        reason = "Army mission expired; parts return to a defensive posture.";
    } else if (c.situation->own.panic || action == TaskKind::Withdraw ||
               action == TaskKind::Rally) {
        state = FsmState::Withdrawing;
        task_status = TaskStatus::Active;
        action = TaskKind::Withdraw;
        attack = false;
        option.destination = c.situation->rally_point;
        reason = "Order an organized army withdrawal to the rally point.";
    } else if (attack && !target) {
        state = FsmState::Failed;
        task_status = TaskStatus::Failed;
        action = TaskKind::HoldArea;
        attack = false;
        reason = "Assigned attack target is no longer visible; hold assigned sectors.";
    } else if (attack) {
        state = FsmState::Advancing;
        option.target_regiment = target->regiment;
        if (option.destination == Vec3{})
            option.destination = target->estimated_pose.position;
        reason = "Advance the assigned parts toward the visible battle objective.";
    } else {
        switch (action) {
        case TaskKind::MoveToArea:
        case TaskKind::Screen:
            state = FsmState::Advancing;
            reason = "Move the assigned parts to their army sectors.";
            break;
        case TaskKind::Support:
            state = FsmState::Supporting;
            reason = "Coordinate support across the army sectors.";
            break;
        case TaskKind::Reserve:
            state = FsmState::Reserve;
            reason = "Keep reserve parts uncommitted behind the battle line.";
            break;
        case TaskKind::Reform:
            state = FsmState::Regrouping;
            reason = "Regroup the army before continuing the mission.";
            break;
        default:
            state = FsmState::Holding;
            action = TaskKind::HoldArea;
            break;
        }
    }
    if (threatened && state != FsmState::Withdrawing && state != FsmState::Failed &&
        state != FsmState::Complete) {
        state = FsmState::Supporting;
        reason = "Release the army reserve to support a threatened sector.";
    }

    option.action = action;
    const FsmState requested_state = state;
    auto out = Finish(c, p, state, task_status, option, reason);
    state = out.next_memory->state;
    if (state != requested_state) {
        switch (state) {
        case FsmState::Attacking:
            action = TaskKind::Attack;
            break;
        case FsmState::Advancing:
        case FsmState::Deploying:
        case FsmState::Supporting:
            action = TaskKind::MoveToArea;
            break;
        case FsmState::Withdrawing:
            action = TaskKind::Withdraw;
            break;
        case FsmState::Reserve:
            action = TaskKind::Reserve;
            break;
        case FsmState::Regrouping:
            action = TaskKind::Reform;
            break;
        default:
            action = TaskKind::HoldArea;
            break;
        }
    }
    option.destination = out.next_memory->goal;
    option.target_regiment = out.next_memory->target;

    float frontage = 0;
    for (const auto *child : children)
        if (child->sector_role != SectorRole::Reserve)
            frontage += status_of(child->id).width_m + 8;
    float offset = -frontage * .5f;
    Vec3 direction = option.destination - c.situation->own.pose.position;
    float yaw =
        Length(direction) > 0 ? std::atan2(direction.y, direction.x) : c.situation->own.pose.yaw;
    const bool release_reserve = state == FsmState::Supporting;
    for (const auto *child : children) {
        const auto &child_status = status_of(child->id);
        TaskKind child_action = action;
        Vec3 goal = option.destination;
        RegimentId target_id = option.target_regiment;
        if (task_status == TaskStatus::Failed || task_status == TaskStatus::Cancelled) {
            child_action = TaskKind::HoldArea;
            goal = child_status.pose.position;
            target_id = {};
        } else if (state == FsmState::Withdrawing) {
            child_action = TaskKind::Withdraw;
            goal = c.situation->rally_point;
            target_id = {};
        } else if (child->sector_role == SectorRole::Reserve && !release_reserve) {
            child_action = TaskKind::Reserve;
            goal = child_status.pose.position;
            target_id = {};
        } else if (child->sector_role == SectorRole::Reserve && supported) {
            child_action = TaskKind::Support;
            goal = status_of(supported->id).pose.position;
            target_id = {};
        } else if (child->sector_role != SectorRole::Reserve) {
            const float width = child_status.width_m;
            float sector_offset = offset + width * .5f + 4;
            offset += width + 8;
            if (child->sector_role == SectorRole::LeftFlank)
                sector_offset = -frontage * .5f - 4;
            else if (child->sector_role == SectorRole::RightFlank)
                sector_offset = frontage * .5f + 4;
            goal = option.destination + Right(yaw) * sector_offset;
        }
        auto task = ChildTask(c, p, *child, child_action, goal, target_id);
        if (child_action == TaskKind::Support && supported)
            task.intent.target_command = supported->id;
        out.subordinate_tasks.push_back(std::move(task));
    }
    return {StubStatus::Ready, std::move(out), "Army FSM evaluated."};
}
} // namespace battle::command
