#include "CommandBrainCommon.h"
namespace battle::command {
StubResult<DecisionProposal> PartBrain::Think(const BrainContext &c) const {
    using namespace detail;
    auto checked = Prepare(c, Level::Part);
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
    std::sort(children.begin(), children.end(), [](auto a, auto b) { return a->id < b->id; });
    TaskKind action = p.task ? p.task->action : TaskKind::Defend;
    TacticalOption option;
    option.destination =
        p.task ? p.task->intent.objective_position : c.situation->own.pose.position;
    FsmState state = FsmState::Holding;
    TaskStatus status = TaskStatus::Active;
    bool threatened = c.situation->own.initial &&
                      double(c.situation->own.living) <= double(c.situation->own.initial) * .75;
    for (auto *child : children)
        threatened = threatened || status_of(child->id).panic;
    for (const auto &report : c.reports)
        threatened = threatened || report.kind == ReportKind::NeedSupport;
    bool release = threatened || (!p.new_task && p.memory.state == FsmState::Supporting);
    bool attack =
        action == TaskKind::Attack || action == TaskKind::Exploit || action == TaskKind::Flank;
    const auto *target =
        attack ? Contact(c, p.task ? p.task->intent.target_regiment : RegimentId{}) : nullptr;
    std::string reason = "Maintain the assigned sector and group frontage.";
    if (children.empty() || !c.node->commander || p.expired || (attack && !target)) {
        state = p.expired ? FsmState::Complete : FsmState::Failed;
        status = p.expired ? TaskStatus::Cancelled : TaskStatus::Failed;
        action = TaskKind::HoldArea;
        reason = "Sector task cannot execute.";
    } else if (c.situation->own.panic || action == TaskKind::Withdraw ||
               action == TaskKind::Rally) {
        state = FsmState::Withdrawing;
        action = TaskKind::Withdraw;
        option.destination = c.situation->rally_point;
        reason = "Organize sector withdrawal.";
    } else if (release) {
        state = FsmState::Supporting;
        reason = "Commit local reserve to threatened groups.";
    } else if (attack) {
        state = FsmState::Advancing;
        option.target_regiment = target->regiment;
        if (option.destination == Vec3{})
            option.destination = target->estimated_pose.position;
        reason = "Advance groups across assigned frontage.";
    } else if (action == TaskKind::MoveToArea || action == TaskKind::Screen)
        state = FsmState::Advancing;
    else if (action == TaskKind::Support)
        state = FsmState::Supporting;
    else if (action == TaskKind::Reserve)
        state = FsmState::Reserve;
    else if (action == TaskKind::Reform)
        state = FsmState::Regrouping;
    option.action = action;
    auto out = Finish(c, p, state, status, option, reason);
    state = out.next_memory->state;
    option.destination = out.next_memory->goal;
    const CommandNode *supported = children.empty() ? nullptr : children.front();
    for (auto *child : children)
        if (status_of(child->id).panic) {
            supported = child;
            break;
        }
    float total_width = 0;
    for (auto *child : children)
        if (child->sector_role != SectorRole::Reserve)
            total_width += status_of(child->id).width_m + 4;
    float offset = -total_width * .5f;
    const CommandNode *objective_anchor = nullptr;
    if (p.task && p.task->intent.objective_radius_m > 0.f &&
        (action == TaskKind::Defend || action == TaskKind::MoveToArea)) {
        for (const auto *child : children)
            if (child->sector_role != SectorRole::Reserve && !status_of(child->id).panic) {
                objective_anchor = child;
                break;
            }
    }
    float left_edge = objective_anchor ? status_of(objective_anchor->id).width_m * .5f : 0.f;
    float right_edge = left_edge;
    Vec3 direction = option.destination - c.situation->own.pose.position;
    float yaw =
        Length(direction) > 0 ? std::atan2(direction.y, direction.x) : c.situation->own.pose.yaw;
    for (auto *child : children) {
        const auto &s = status_of(child->id);
        TaskKind kind = action;
        Vec3 goal = option.destination;
        RegimentId target_id = target ? target->regiment : RegimentId{};
        if (status == TaskStatus::Failed || status == TaskStatus::Cancelled) {
            kind = TaskKind::HoldArea;
            goal = s.pose.position;
            target_id = {};
        } else if (state == FsmState::Withdrawing) {
            kind = TaskKind::Withdraw;
            goal = c.situation->rally_point;
            target_id = {};
        } else if (child->sector_role == SectorRole::Reserve && !release) {
            kind = TaskKind::Reserve;
            goal = s.pose.position;
            target_id = {};
        } else if (child->sector_role == SectorRole::Reserve) {
            kind = TaskKind::Support;
            goal = status_of(supported->id).pose.position;
            target_id = {};
        } else {
            float lateral = offset + s.width_m * .5f + 2;
            if (objective_anchor) {
                if (child == objective_anchor) {
                    lateral = 0.f;
                } else {
                    const bool left = lateral < 0.f;
                    auto &edge = left ? left_edge : right_edge;
                    lateral = (edge + s.width_m * .5f + 4.f) * (left ? -1.f : 1.f);
                    edge += s.width_m + 4.f;
                }
            }
            goal = option.destination + Right(yaw) * lateral;
            offset += s.width_m + 4;
        }
        auto task = ChildTask(c, p, *child, kind, goal, target_id);
        if (kind == TaskKind::Support && supported)
            task.intent.target_command = supported->id;
        out.subordinate_tasks.push_back(std::move(task));
    }
    if (threatened && c.node->parent) {
        SituationReport request;
        request.kind = ReportKind::NeedSupport;
        request.sender = c.node->id;
        request.recipient = c.node->parent;
        request.about_task = p.memory.active_task;
        request.created_tick = c.tick;
        request.observed_tick = c.tick;
        request.situation = *c.situation;
        request.explanation = "Sector requires support.";
        out.reports.push_back(std::move(request));
    }
    return {StubStatus::Ready, std::move(out), "Part FSM evaluated."};
}
} // namespace battle::command
