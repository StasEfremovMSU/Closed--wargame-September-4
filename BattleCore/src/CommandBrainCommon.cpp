#include "CommandBrainCommon.h"
#include "battle/World.h"
#include <array>
#include <map>
#include <set>
#include <tuple>
namespace battle::command::detail {
Tick Later(Tick a, Tick b) { return b > UINT64_MAX - a ? UINT64_MAX : a + b; }
std::optional<Tick> TaskExpiry(const CommandTask &t, Tick received) {
    auto end = t.expiry_tick;
    if (t.duration_ticks) {
        auto finish =
            Later(t.duration_origin == DurationOrigin::Created ? t.created_tick
                                                               : t.received_tick.value_or(received),
                  *t.duration_ticks);
        if (!end || finish < *end)
            end = finish;
    }
    return end;
}
bool ValidTask(const CommandTask &t) {
    if (!t.id || t.id.value == UINT64_MAX || !t.recipient ||
        unsigned(t.action) > unsigned(TaskKind::Reform) ||
        unsigned(t.status) > unsigned(TaskStatus::Cancelled) ||
        unsigned(t.authority.initiative) > unsigned(InitiativePolicy::Free) ||
        unsigned(t.duration_origin) > unsigned(DurationOrigin::Delivered) ||
        !Finite(t.intent.objective_position) || !std::isfinite(t.intent.objective_radius_m) ||
        t.intent.objective_radius_m < 0 || (t.duration_ticks && *t.duration_ticks > 1000000) ||
        (t.intent.flank_waypoint && (t.action != TaskKind::Flank || !t.intent.target_regiment ||
                                    !Finite(*t.intent.flank_waypoint))) ||
        (t.formation_columns && (*t.formation_columns == 0 || *t.formation_columns > 4096)) ||
        (t.formation_spacing_m &&
         (!std::isfinite(*t.formation_spacing_m) || *t.formation_spacing_m <= 0)))
        return false;
    if (t.authority.pursuit_limit_m &&
        (!std::isfinite(*t.authority.pursuit_limit_m) || *t.authority.pursuit_limit_m < 0))
        return false;
    if (t.authority.acceptable_loss_fraction &&
        (!std::isfinite(*t.authority.acceptable_loss_fraction) ||
         *t.authority.acceptable_loss_fraction < 0 || *t.authority.acceptable_loss_fraction > 1))
        return false;
    return true;
}
bool ValidTaskTarget(const World &world, const CommandNode &recipient, const CommandTask &task) {
    if (!task.intent.target_regiment)
        return true;
    const auto *target = FindRegiment(world, task.intent.target_regiment);
    if (!target || target->id == recipient.regiment)
        return false;
    return (target->team == recipient.team) == (task.action == TaskKind::Support);
}
StubResult<Prepared> Prepare(const BrainContext &c, Level level) {
    auto invalid = [] {
        return StubResult<Prepared>{StubStatus::InvalidInput, std::nullopt,
                                    "Invalid brain context."};
    };
    if (!c.node || !c.node->id || c.node->level != level || !c.situation ||
        c.situation->observer != c.node->id || c.tick != c.situation->tick ||
        !Finite(c.situation->own.pose) || !Finite(c.situation->rally_point) ||
        (c.node->commander && (!c.profile || c.profile->officer != c.node->commander)))
        return invalid();
    if (c.profile)
        for (float x : {c.profile->tactical_skill, c.profile->initiative, c.profile->discipline,
                        c.profile->aggression, c.profile->caution, c.profile->charisma,
                        c.profile->leadership})
            if (!std::isfinite(x) || x < 0 || x > 1)
                return invalid();
    for (const auto &p : c.situation->contacts)
        if (!Finite(p.estimated_pose) || p.observed_tick > c.tick || !std::isfinite(p.confidence) ||
            p.confidence < 0 || p.confidence > 1 ||
            (p.estimated_strength &&
             (!std::isfinite(*p.estimated_strength) || *p.estimated_strength < 0)))
            return invalid();
    std::set<uint64_t> child_ids;
    for (const auto &n : c.subordinates) {
        if (!n.id || !child_ids.insert(n.id.value).second || n.parent != c.node->id ||
            n.team != c.node->team || !CanCommandLevel(level, n.level) ||
            std::count(c.node->children.begin(), c.node->children.end(), n.id) != 1)
            return invalid();
        auto found = std::find_if(c.children_status.begin(), c.children_status.end(),
                                  [&](const auto &s) { return s.node == n.id; });
        if (found == c.children_status.end() || !Finite(found->pose) ||
            !std::isfinite(found->width_m) || found->width_m <= 0 || found->living > found->initial)
            return invalid();
    }
    if (level != Level::Regiment && child_ids.size() != c.node->children.size())
        return invalid();
    Prepared p;
    if (c.memory)
        p.memory = *c.memory;
    const CommandTask *expired = nullptr;
    for (const auto &t : c.orders) {
        if (!ValidTask(t) || t.recipient != c.node->id)
            return invalid();
        if (t.status == TaskStatus::Completed || t.status == TaskStatus::Failed ||
            t.status == TaskStatus::Cancelled)
            continue;
        auto end =
            TaskExpiry(t, t.id == p.memory.active_task ? p.memory.task_started_tick : c.tick);
        if (end && *end <= c.tick) {
            if (!expired || t.id == p.memory.active_task)
                expired = &t;
            continue;
        }
        if (t.not_before_tick && *t.not_before_tick > c.tick)
            continue;
        if (!p.task || std::tuple(t.priority, t.created_tick, t.id) >
                           std::tuple(p.task->priority, p.task->created_tick, p.task->id))
            p.task = &t;
    }
    p.new_task = p.task && p.task->id != p.memory.active_task;
    if (p.new_task) {
        p.memory.active_task = p.task->id;
        p.memory.task_started_tick = c.tick;
        p.memory.task_status = TaskStatus::Active;
    }
    if (!p.task && expired) {
        p.expired = true;
        p.memory.active_task = expired->id;
        p.memory.task_status = TaskStatus::Cancelled;
    }
    return {StubStatus::Ready, std::move(p), "Prepared command task."};
}
const KnownContact *Contact(const BrainContext &c, RegimentId id, bool enemy) {
    const KnownContact *best = nullptr;
    for (const auto &x : c.situation->contacts) {
        if ((id && x.regiment != id) ||
            x.allegiance != (enemy ? Allegiance::Enemy : Allegiance::Friendly) ||
            (x.team && ((*x.team == c.node->team) == enemy)) || x.confidence <= 0 ||
            (x.estimated_strength && *x.estimated_strength <= 0))
            continue;
        float distance = Distance(c.situation->own.pose.position, x.estimated_pose.position);
        if (!best ||
            distance < Distance(c.situation->own.pose.position, best->estimated_pose.position) ||
            (distance == Distance(c.situation->own.pose.position, best->estimated_pose.position) &&
             x.regiment < best->regiment))
            best = &x;
    }
    return best;
}
const KnownContact *AttackContact(const BrainContext &c, const Prepared &p) {
    if (!p.task)
        return Contact(c);
    const auto &intent = p.task->intent;
    const auto &authority = p.task->authority;
    const auto within_intent = [&](const KnownContact &contact) {
        return !authority.pursuit_limit_m ||
               Distance(intent.objective_position, contact.estimated_pose.position) <=
                   *authority.pursuit_limit_m;
    };
    const auto preferred = authority.may_change_target && !p.new_task && p.memory.target
                               ? p.memory.target : intent.target_regiment;
    if (const auto *assigned = Contact(c, preferred);
        assigned && within_intent(*assigned) &&
        (assigned->source != ObservationSource::Memory || authority.may_pursue))
        return assigned;
    if (!authority.may_change_target)
        return nullptr;
    const KnownContact *nearest = nullptr;
    float best = std::numeric_limits<float>::infinity();
    for (const auto &candidate : c.situation->contacts) {
        if (!candidate.regiment || candidate.allegiance != Allegiance::Enemy ||
            candidate.source != ObservationSource::Direct || !within_intent(candidate) ||
            (candidate.team && *candidate.team == c.node->team) || candidate.confidence <= 0 ||
            (candidate.estimated_strength && *candidate.estimated_strength <= 0))
            continue;
        const float distance = Distance(c.situation->own.pose.position, candidate.estimated_pose.position);
        if (distance < best || (distance == best && nearest && candidate.regiment < nearest->regiment)) {
            best = distance;
            nearest = &candidate;
        }
    }
    return nearest;
}

bool AttackWorthwhile(const BrainContext &c, const KnownContact &target) {
    if (!c.profile || c.profile->initiative <= 0)
        return false;
    std::array<TacticalOption, 2> options;
    options[0].action = TaskKind::HoldArea;
    options[1].action = TaskKind::Attack;
    std::array<float, 3> features{
        float(c.situation->own.living),
        target.estimated_strength.value_or(float(c.situation->own.living)),
        Distance(c.situation->own.pose.position, target.estimated_pose.position)};
    EvaluationRequest request{c.node->level, c.node->id, c.tick, 1, features, options};
    auto scores =
        c.evaluator ? c.evaluator->Evaluate(request) : RulesEvaluatorStub{}.Evaluate(request);
    if (scores.status != StubStatus::Ready || !scores.value || scores.value->size() != 2)
        scores = RulesEvaluatorStub{}.Evaluate(request);
    if (!scores.value || scores.value->size() != 2)
        return false;
    return (*scores.value)[1].utility * (.5f + c.profile->aggression) / (1 + c.profile->caution) >
           (*scores.value)[0].utility;
}
DecisionProposal Finish(const BrainContext &c, Prepared p, FsmState state, TaskStatus status,
                        TacticalOption option, std::string reason) {
    const auto before = c.memory ? *c.memory : BrainMemory{};
    bool emergency = c.situation->own.panic || !c.node->commander || status == TaskStatus::Failed ||
                     status == TaskStatus::Cancelled || status == TaskStatus::Completed;
    if (!emergency && !p.new_task && c.memory && state != before.state && c.profile &&
        c.tick < Later(before.state_entered_tick, c.profile->minimum_commitment_ticks)) {
        state = before.state;
        option.destination = before.goal;
        option.target_regiment = before.target;
        switch (state) {
        case FsmState::Attacking:
            option.action = TaskKind::Attack;
            break;
        case FsmState::Advancing:
        case FsmState::Deploying:
        case FsmState::Supporting:
            option.action = TaskKind::MoveToArea;
            break;
        default:
            option.action = TaskKind::HoldArea;
            break;
        }
        reason = "Maintain committed decision.";
    }
    bool changed = p.new_task || state != before.state || status != before.task_status ||
                   option.target_regiment != before.target;
    p.memory.state = state;
    p.memory.task_status = status;
    if (state != before.state || p.new_task)
        p.memory.state_entered_tick = c.tick;
    p.memory.target = option.target_regiment;
    p.memory.goal = option.destination;
    p.memory.last_think_tick = c.tick;
    std::map<uint64_t, KnownContact> contacts;
    for (auto x : p.memory.remembered_contacts)
        if (x.observed_tick <= c.tick && c.tick - x.observed_tick <= 100)
            contacts[x.regiment.value] = x;
    for (auto x : c.situation->contacts)
        contacts[x.regiment.value] = x;
    p.memory.remembered_contacts.clear();
    for (auto &[id, x] : contacts) {
        (void)id;
        p.memory.remembered_contacts.push_back(x);
    }
    if (p.memory.remembered_contacts.size() > 64)
        p.memory.remembered_contacts.resize(64);
    if (changed) {
        p.memory.recent_decisions.push_back({c.tick, p.memory.active_task, option, reason});
        if (p.memory.recent_decisions.size() > 64)
            p.memory.recent_decisions.erase(p.memory.recent_decisions.begin());
    }
    DecisionProposal out;
    out.node = c.node->id;
    out.level = c.node->level;
    out.source_task = p.memory.active_task;
    out.explanation = reason;
    if (c.node->level == Level::Regiment) {
        Order order;
        order.recipient = c.node->regiment;
        order.issuer = c.node->commander;
        order.priority = p.task ? p.task->priority : 0;
        order.point = option.destination;
        order.target_regiment = option.target_regiment;
        order.created_tick = c.tick;
        order.delivery_tick = c.tick;
        if (p.task)
            order.expiry_tick = TaskExpiry(*p.task, p.memory.task_started_tick).value_or(0);
        switch (option.action) {
        case TaskKind::Attack:
        case TaskKind::Exploit:
            order.type = OrderType::Attack;
            break;
        case TaskKind::MoveToArea:
        case TaskKind::Screen:
        case TaskKind::Support:
        case TaskKind::Flank:
            order.type = OrderType::MoveTo;
            break;
        case TaskKind::Withdraw:
            order.type = OrderType::Withdraw;
            break;
        case TaskKind::Rally:
            order.type = OrderType::Rally;
            break;
        default:
            order.type = std::ranges::any_of(c.situation->threats, [](const auto &threat) {
                return threat.cavalry_approaching && threat.sector == RelativeSector::Front;
            }) ? OrderType::Brace : OrderType::Hold;
            break;
        }
        out.execution_order = order;
    }
    if (changed && c.node->parent) {
        SituationReport report;
        report.sender = c.node->id;
        report.recipient = c.node->parent;
        report.about_task = p.memory.active_task;
        report.observed_tick = c.tick;
        report.created_tick = c.tick;
        report.situation = *c.situation;
        report.explanation = reason;
        report.kind = status == TaskStatus::Completed ? ReportKind::TaskCompleted
                      : status == TaskStatus::Failed || status == TaskStatus::Cancelled
                          ? ReportKind::CannotExecute
                      : p.new_task ? ReportKind::Acknowledged
                                   : ReportKind::Situation;
        out.reports.push_back(std::move(report));
    }
    out.next_memory = std::move(p.memory);
    return out;
}
CommandTask ChildTask(const BrainContext &c, const Prepared &p, const CommandNode &child,
                      TaskKind action, Vec3 point, RegimentId target) {
    CommandTask t;
    t.parent_task = p.memory.active_task;
    t.issuer = c.node->id;
    t.recipient = child.id;
    t.author = c.node->commander;
    t.action = action;
    t.created_tick = c.tick;
    t.intent.objective_position = point;
    t.intent.target_regiment = target;
    t.intent.objective_radius_m = p.task ? p.task->intent.objective_radius_m : 2;
    t.authority = p.task ? p.task->authority : AuthorityLimits{};
    if (!p.task)
        t.authority.initiative = InitiativePolicy::WithinIntent;
    t.priority = p.task ? p.task->priority : 0;
    if (p.task) {
        t.expiry_tick = TaskExpiry(*p.task, p.memory.task_started_tick);
        t.formation_columns = p.task->formation_columns;
        t.formation_spacing_m = p.task->formation_spacing_m;
    }
    return t;
}
} // namespace battle::command::detail
