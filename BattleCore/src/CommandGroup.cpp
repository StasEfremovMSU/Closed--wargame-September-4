#include "CommandBrainCommon.h"
namespace battle::command {
StubResult<DecisionProposal> GroupBrain::Think(const BrainContext &c) const {
    using namespace detail;
    auto checked = Prepare(c, Level::Group);
    if (checked.status != StubStatus::Ready)
        return {checked.status, std::nullopt, checked.reason};
    auto p = std::move(*checked.value);
    std::vector<const CommandNode *> children;
    auto status_of = [&](NodeId id) -> const ChildStatus & {
        return *std::find_if(c.children_status.begin(), c.children_status.end(),
                             [&](const auto &s) { return s.node == id; });
    };
    for (const auto &child : c.subordinates)
        if (status_of(child.id).living)
            children.push_back(&child);
    std::sort(children.begin(), children.end(), [](auto a, auto b) { return a->id < b->id; });
    TacticalOption option;
    option.destination =
        p.task ? p.task->intent.objective_position : c.situation->own.pose.position;
    TaskKind action = p.task ? p.task->action : TaskKind::HoldArea;
    FsmState state = FsmState::Holding;
    TaskStatus task_status = TaskStatus::Active;
    std::string reason = "Coordinate assigned regiment positions.";
    bool release =
        c.situation->own.initial && c.situation->own.living * 4 <= c.situation->own.initial * 3;
    for (const auto *child : children)
        release = release || status_of(child->id).panic;
    bool attack =
        action == TaskKind::Attack || action == TaskKind::Exploit || action == TaskKind::Flank;
    const KnownContact *target =
        attack ? Contact(c, p.task ? p.task->intent.target_regiment : RegimentId{}) : nullptr;
    if (!p.task)
        if (auto *enemy = Contact(c); enemy && AttackWorthwhile(c, *enemy)) {
            target = enemy;
            attack = true;
            action = TaskKind::Attack;
        }
    if (children.empty() || !c.node->commander || p.expired || (attack && !target)) {
        state = children.empty() || !c.node->commander || attack ? FsmState::Failed
                                                                 : FsmState::Complete;
        task_status = p.expired ? TaskStatus::Cancelled : TaskStatus::Failed;
        action = TaskKind::HoldArea;
        attack = false;
        reason = "Group cannot execute: missing force, commander, target or expired task.";
    } else if (c.situation->own.panic || action == TaskKind::Withdraw ||
               action == TaskKind::Rally) {
        state = FsmState::Withdrawing;
        action = TaskKind::Withdraw;
        attack = false;
        option.destination = c.situation->rally_point;
        reason = "Coordinate group withdrawal.";
    } else if (attack) {
        option.target_regiment = target->regiment;
        Vec3 delta = target->estimated_pose.position - c.situation->own.pose.position;
        float length = Length(delta);
        Vec3 staging = length > 15 ? target->estimated_pose.position - delta * (15 / length)
                                   : c.situation->own.pose.position;
        if (!p.new_task && p.memory.state == FsmState::Deploying)
            staging = p.memory.goal;
        bool ready = true;
        float width = 0;
        for (auto *child : children)
            if (child->sector_role != SectorRole::Reserve)
                width += status_of(child->id).width_m + 2;
        float offset = -width * .5f;
        float yaw = length > 0 ? std::atan2(delta.y, delta.x) : c.situation->own.pose.yaw;
        for (auto *child : children)
            if (child->sector_role != SectorRole::Reserve) {
                float w = status_of(child->id).width_m;
                Vec3 goal = staging + Right(yaw) * (offset + w * .5f + 1);
                offset += w + 2;
                ready = ready && Distance(status_of(child->id).pose.position, goal) <= 2 &&
                        !status_of(child->id).panic;
            }
        bool executing =
            !p.new_task && (p.memory.state == FsmState::Attacking ||
                            (p.memory.state == FsmState::Deploying &&
                             (ready || c.tick >= Later(p.memory.state_entered_tick, 40))));
        state = executing ? FsmState::Attacking : FsmState::Deploying;
        option.destination = executing ? target->estimated_pose.position : staging;
        reason = executing ? "Lead regiment attacks with coordinated support."
                           : "Deploy regiments before joint attack.";
    } else {
        switch (action) {
        case TaskKind::Support:
            state = FsmState::Supporting;
            break;
        case TaskKind::MoveToArea:
        case TaskKind::Screen:
            state = FsmState::Advancing;
            break;
        case TaskKind::Reserve:
            state = FsmState::Reserve;
            break;
        case TaskKind::Reform:
            state = FsmState::Regrouping;
            break;
        default:
            state = FsmState::Holding;
            break;
        }
    }
    option.action = action;
    auto out = Finish(c, p, state, task_status, option, reason);
    state = out.next_memory->state;
    option.destination = out.next_memory->goal;
    option.target_regiment = out.next_memory->target;
    float width = 0;
    for (auto *child : children)
        if (child->sector_role != SectorRole::Reserve)
            width += status_of(child->id).width_m + 2;
    float offset = -width * .5f;
    float yaw =
        target ? std::atan2(target->estimated_pose.position.y - c.situation->own.pose.position.y,
                            target->estimated_pose.position.x - c.situation->own.pose.position.x)
               : c.situation->own.pose.yaw;
    const CommandNode *lead = nullptr;
    for (auto *child : children)
        if (child->sector_role != SectorRole::Reserve && !status_of(child->id).panic) {
            lead = child;
            break;
        }
    if (!lead && !children.empty())
        lead = children.front();
    for (auto *child : children) {
        const auto &s = status_of(child->id);
        TaskKind kind = action;
        Vec3 goal = option.destination;
        RegimentId child_target = option.target_regiment;
        bool reserve = child->sector_role == SectorRole::Reserve;
        if (task_status == TaskStatus::Failed || task_status == TaskStatus::Cancelled) {
            kind = TaskKind::HoldArea;
            goal = s.pose.position;
            child_target = {};
        } else if (state == FsmState::Withdrawing) {
            kind = TaskKind::Withdraw;
            goal = c.situation->rally_point;
            child_target = {};
        } else if (s.panic) {
            kind = TaskKind::Rally;
            goal = c.situation->rally_point;
            child_target = {};
        } else if (reserve && !release) {
            kind = TaskKind::Reserve;
            goal = s.pose.position;
            child_target = {};
        } else if (state == FsmState::Attacking) {
            kind = child == lead ? TaskKind::Attack : TaskKind::Support;
            if (child != lead)
                child_target = lead->regiment;
        } else {
            kind = state == FsmState::Deploying ? TaskKind::MoveToArea : action;
            goal = option.destination + Right(yaw) * (offset + s.width_m * .5f + 1);
            if (!reserve)
                offset += s.width_m + 2;
        }
        out.subordinate_tasks.push_back(ChildTask(c, p, *child, kind, goal, child_target));
    }
    return {StubStatus::Ready, std::move(out), "Group FSM evaluated."};
}
} // namespace battle::command
