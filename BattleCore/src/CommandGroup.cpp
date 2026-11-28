#include "CommandBrainCommon.h"
#include "battle/command/GroupPolicy.h"
#include <array>
namespace battle::command {
namespace {
constexpr float AssemblyDistance = 15.f;

bool StillExecutingAssignment(const ChildOrderObservation &observation, Tick now, Tick minimum) {
    if (observation.pending)
        return true;
    if (!observation.delivered_tick || *observation.delivered_tick > now)
        return false;
    const Tick execution_age = now - *observation.delivered_tick;
    return execution_age < minimum ||
        (observation.executing && observation.progress >= .15f && execution_age / 3 < minimum);
}

std::optional<Vec3> SupportStage(const BrainContext &context, const CommandTask *assignment,
                                 const ChildStatus &mover, const ChildStatus &lead, Pose approach,
                                 std::span<const Vec3> assigned_goals) {
    if (!context.can_stage)
        return std::nullopt;
    const float spacing = .5f * (mover.width_m + lead.width_m) + 2.f;
    const Vec3 side = Right(approach.yaw);
    const Vec3 forward = Forward(approach.yaw);
    const Vec3 trailing = approach.position - forward * (2.f * spacing);
    const float sign = Dot(mover.pose.position - trailing, side) < 0 ? -1.f : 1.f;
    const std::array candidates{trailing + side * (sign * spacing),
                                trailing - side * (sign * spacing), trailing};
    for (const auto point : candidates) {
        if (Dot(point - mover.pose.position, approach.position - mover.pose.position) <= 1.f ||
            (assignment && assignment->authority.pursuit_limit_m &&
             Distance(point, assignment->intent.objective_position) >
                 *assignment->authority.pursuit_limit_m) ||
            std::any_of(assigned_goals.begin(), assigned_goals.end(), [&](const auto goal) {
                return Distance(goal, point) < spacing;
            }))
            continue;
        if (context.can_stage(mover.node, point))
            return point;
    }
    return std::nullopt;
}

struct WingChoice {
    TaskKind action;
    bool keep{};
    bool policy_override{};
};

WingChoice ScoreSecondRegiment(const BrainContext &context, const ChildStatus &lead,
                              const ChildStatus &wing, const KnownContact &primary,
                              const KnownContact *alternate, const std::optional<Pose> &flank,
                              Vec3 support_goal, const CommandTask *previous, TaskKind baseline,
                              const std::optional<ChildOrderObservation> &observation,
                              Tick minimum_execution) {
    const bool ranged_pressure = context.situation->own.under_ranged_fire ||
        std::any_of(context.situation->threats.begin(),
        context.situation->threats.end(), [](const auto &threat) { return threat.under_ranged_fire; });
    const auto fraction = [](uint32_t living, uint32_t initial) {
        return initial ? std::min(1.f, float(living) / float(initial)) : 0.f;
    };
    const auto metres = [](float value) { return std::clamp(value / 50.f, -2.f, 2.f); };
    const auto strength = [](float value) { return std::clamp(value / 100.f, 0.f, 2.f); };
    const auto delta = wing.pose.position - primary.estimated_pose.position;
    const bool commitment = context.group_order_commitment;
    const auto row_size = commitment ? GroupPolicyCommitmentFeatureNames.size()
                                     : GroupPolicyFeatureNames.size();
    const auto keep = observation ? previous : nullptr;
    const auto age = keep ? context.tick - previous->created_tick : Tick{};
    const auto execution_age = observation && observation->delivered_tick &&
        *observation->delivered_tick <= context.tick
        ? context.tick - *observation->delivered_tick : Tick{};
    const bool prefer_keep = observation && observation->executing &&
        observation->progress >= .15f && execution_age < minimum_execution * 3;
    std::array<TacticalOption, 4> options;
    std::array<float, GroupPolicyCommitmentFeatureNames.size() * 4> features{};
    uint32_t count = 0, baseline_index = 0;
    bool baseline_is_keep = prefer_keep;
    const auto add = [&](TaskKind action, const KnownContact &contact, Vec3 destination,
                         RegimentId target, bool keep_option = false) {
        const float row[] = {
            float(!keep_option && action == TaskKind::Support),
            float(!keep_option && action == TaskKind::Attack),
            float(!keep_option && action == TaskKind::Flank),
            float(previous && previous->action == TaskKind::Support),
            float(previous && previous->action == TaskKind::Attack),
            float(previous && previous->action == TaskKind::Flank),
            fraction(context.situation->own.living, context.situation->own.initial),
            fraction(lead.living, lead.initial), fraction(wing.living, wing.initial),
            strength(float(lead.living)), strength(float(wing.living)),
            strength(primary.estimated_strength.value_or(0.f)),
            float(primary.estimated_strength.has_value()),
            strength(contact.estimated_strength.value_or(0.f)), float(contact.estimated_strength.has_value()),
            primary.confidence, contact.confidence, metres(Length(delta)),
            metres(Distance(lead.pose.position, primary.estimated_pose.position)),
            metres(Distance(wing.pose.position, contact.estimated_pose.position)),
            metres(Distance(wing.pose.position, lead.pose.position)),
            metres(Distance(wing.pose.position, destination)), float(lead.melee_engaged),
            float(ranged_pressure), float(alternate != nullptr), float(flank.has_value()),
            metres(Dot(delta, Forward(primary.estimated_pose.yaw))),
            metres(Dot(delta, Right(primary.estimated_pose.yaw))),
            float(keep_option), float(observation && observation->pending),
            float(observation && observation->executing), std::min(2.f, float(age) / 200.f),
            observation ? observation->progress : 0.f,
        };
        static_assert(std::size(row) == GroupPolicyCommitmentFeatureNames.size());
        std::copy_n(std::begin(row), row_size, features.begin() + count * row_size);
        options[count] = {action, wing.node, action == TaskKind::Support ? lead.node : NodeId{},
                          target, destination, {}, keep_option};
        if ((!keep_option && action == baseline) || (keep_option && baseline_is_keep))
            baseline_index = count;
        ++count;
    };
    const auto same_support = keep && keep->action == TaskKind::Support &&
        keep->intent.objective_position == support_goal;
    const auto same_attack = keep && alternate && keep->action == TaskKind::Attack &&
        keep->intent.target_regiment == alternate->regiment;
    const auto same_flank = keep && flank && keep->action == TaskKind::Flank &&
        keep->intent.flank_waypoint == flank;
    baseline_is_keep |= (same_support && baseline == TaskKind::Support) ||
        (same_attack && baseline == TaskKind::Attack) || (same_flank && baseline == TaskKind::Flank);
    if (!same_support)
        add(TaskKind::Support, primary, support_goal, {});
    if (alternate && !same_attack)
        add(TaskKind::Attack, *alternate, alternate->estimated_pose.position, alternate->regiment);
    if (flank && !same_flank)
        add(TaskKind::Flank, primary, flank->position, primary.regiment);
    if (keep) {
        const auto &contact = same_attack ? *alternate : primary;
        const auto destination = keep->intent.flank_waypoint
            ? keep->intent.flank_waypoint->position : keep->intent.objective_position;
        add(keep->action, contact, destination, keep->intent.target_regiment, true);
    }
    const auto choice = [&](uint32_t index) {
        return WingChoice{options[index].action, options[index].keep_current_order, index != baseline_index};
    };
    if (count < 2 || !context.group_policy)
        return choice(baseline_index);

    const auto result = context.group_policy->Evaluate({
        Level::Group, context.node->id, context.tick,
        commitment ? GroupPolicyCommitmentFeatureSchema : GroupPolicyFeatureSchema,
        std::span{features}.first(count * row_size),
        std::span{options}.first(count), baseline_index,
    });
    if (result.status != StubStatus::Ready || !result.value || result.value->size() != count)
        return choice(baseline_index);

    std::array<float, 4> utilities{};
    std::array<bool, 4> seen{};
    for (const auto &score : *result.value) {
        const auto index = score.option_index;
        if (index >= count || seen[index] || !std::isfinite(score.utility))
            return choice(baseline_index);
        seen[index] = true;
        utilities[index] = score.utility;
    }
    auto chosen = baseline_index;
    for (uint32_t index = 0; index < count; ++index)
        if (utilities[index] > utilities[chosen])
            chosen = index;
    return choice(chosen);
}
} // namespace
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
    const bool army_attack = p.task && action == TaskKind::Attack && p.task->intent.target_regiment &&
                             c.parent_level == Level::Army && p.task->issuer == c.node->parent;
    const KnownContact *target = army_attack ? Contact(c, p.task->intent.target_regiment)
                                            : attack ? AttackContact(c, p) : nullptr;
    if (!p.task)
        if (auto *enemy = Contact(c); enemy && AttackWorthwhile(c, *enemy)) {
            target = enemy;
            attack = true;
            action = TaskKind::Attack;
        }
    const auto select_lead = [&](bool require_commander) -> const CommandNode * {
        for (auto *child : children)
            if (child->sector_role != SectorRole::Reserve && !status_of(child->id).panic &&
                (!require_commander || child->commander))
                return child;
        return nullptr;
    };
    const CommandNode *lead = select_lead(true);
    if (!lead)
        lead = select_lead(false);
    if (!lead && !children.empty())
        lead = children.front();
    const auto planned_flank = p.task && action == TaskKind::Flank ? p.task->intent.flank_waypoint
                                                                 : std::nullopt;
    float yaw = c.situation->own.pose.yaw;
    const bool can_regroup = p.task && (army_attack || p.task->authority.may_pursue);
    if (children.empty() || !c.node->commander || p.expired ||
        (attack && !target && !can_regroup)) {
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
    } else if (!target && planned_flank && can_regroup) {
        state = FsmState::Attacking;
        option.target_regiment = p.task->intent.target_regiment;
        reason = "Advance along the assigned flank and watch for its enemy.";
    } else if (army_attack && !target && p.task->supersede_player_orders_through &&
               Distance(c.situation->own.pose.position, option.destination) > 2.f) {
        state = FsmState::Advancing;
        action = TaskKind::MoveToArea;
        reason = "Advance to the enemy position reported by the army commander.";
    } else if (attack && !target) {
        state = FsmState::Regrouping;
        option.destination = c.situation->own.pose.position;
        action = TaskKind::HoldArea;
        reason = "Regroup and watch for the assigned enemy.";
    } else if (attack) {
        option.target_regiment = target->regiment;
        Vec3 assigned = p.task && p.task->intent.objective_position != Vec3{}
                            ? p.task->intent.objective_position
                            : target->estimated_pose.position;
        const Vec3 assigned_delta = assigned - c.situation->own.pose.position;
        const float assigned_distance = Length(assigned_delta);
        Vec3 staging = assigned_distance > AssemblyDistance
                           ? assigned - assigned_delta * (AssemblyDistance / assigned_distance)
                           : c.situation->own.pose.position;
        if (!p.new_task && p.memory.state == FsmState::Deploying)
            staging = p.memory.goal;
        const Vec3 approach = assigned - staging;
        if (Length(approach) > .01f)
            yaw = std::atan2(approach.y, approach.x);
        bool ready = true;
        float width = 0;
        for (auto *child : children)
            if (child->sector_role != SectorRole::Reserve)
                width += status_of(child->id).width_m + 2;
        float offset = -width * .5f;
        for (auto *child : children)
            if (child->sector_role != SectorRole::Reserve) {
                const auto &s = status_of(child->id);
                const Vec3 goal = staging + Right(yaw) * (offset + s.width_m * .5f + 1);
                offset += s.width_m + 2;
                ready = ready && Distance(s.pose.position, goal) <= 2 && !s.panic;
            }
        const bool lead_was_committed = lead && !status_of(lead->id).panic &&
            (status_of(lead->id).attack_target == target->regiment ||
             Distance(status_of(lead->id).pose.position, target->estimated_pose.position) <=
                 AssemblyDistance);
        const bool under_pressure = c.situation->own.living < c.situation->own.initial ||
            std::any_of(c.situation->threats.begin(), c.situation->threats.end(),
                        [](const auto &threat) { return threat.under_ranged_fire; });
        const bool executing = planned_flank || lead_was_committed || under_pressure ||
            (!p.new_task && (p.memory.state == FsmState::Attacking ||
                             p.memory.state == FsmState::Regrouping ||
                             (p.memory.state == FsmState::Deploying &&
                              (ready || c.tick >= Later(p.memory.state_entered_tick, 40)))));
        state = executing ? FsmState::Attacking : FsmState::Deploying;
        if (executing && !p.new_task && p.memory.state == FsmState::Attacking &&
            p.memory.target == target->regiment)
            assigned = p.memory.goal;
        option.destination = executing ? assigned : staging;
        reason = p.task && p.task->intent.target_regiment &&
                         p.task->intent.target_regiment != target->regiment
                     ? "Assigned enemy is gone. Attack a nearby observed enemy."
                 : planned_flank ? "Advance along the assigned flank."
                 : p.memory.state == FsmState::Regrouping
                     ? "Contact restored. Resume the attack."
                     : executing ? "Lead regiment attacks with coordinated support."
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
    const bool defend_objective = lead && p.task && p.task->intent.objective_radius_m > 0.f &&
        (action == TaskKind::Defend || action == TaskKind::MoveToArea);
    float left_edge = defend_objective ? status_of(lead->id).width_m * .5f : 0.f;
    float right_edge = left_edge;
    const auto previous_assignment = [&](NodeId id) -> const CommandTask * {
        const auto it = std::find_if(c.previous_subordinate_tasks.begin(),
                                     c.previous_subordinate_tasks.end(), [&](const auto &task) {
            return task.recipient == id && task.parent_task == out.source_task;
        });
        return it == c.previous_subordinate_tasks.end() ? nullptr : &*it;
    };
    const bool can_plan_support = p.task && target && target->regiment == option.target_regiment &&
        target->source != ObservationSource::Memory && p.task->authority.may_pursue &&
        (p.task->authority.initiative == InitiativePolicy::WithinIntent ||
         p.task->authority.initiative == InitiativePolicy::Free);
    const bool lead_pins = can_plan_support && lead && status_of(lead->id).melee_engaged &&
                           status_of(lead->id).attack_target == target->regiment;
    const bool group_commitment = c.group_order_commitment && children.size() == 2;
    std::vector<RegimentId> assigned_targets{option.target_regiment};
    const auto secondary_contact = [&](const ChildStatus &s, RegimentId preferred)
        -> const KnownContact * {
        const KnownContact *best = nullptr;
        float best_distance = std::numeric_limits<float>::infinity();
        for (const auto &contact : c.situation->contacts) {
            if ((preferred && preferred != contact.regiment) || !contact.regiment ||
                contact.allegiance != Allegiance::Enemy ||
                (contact.team && *contact.team == c.node->team) ||
                contact.source != ObservationSource::Direct || contact.confidence <= 0 ||
                (contact.estimated_strength && *contact.estimated_strength <= 0) ||
                std::find(assigned_targets.begin(), assigned_targets.end(), contact.regiment) !=
                    assigned_targets.end() ||
                (p.task->authority.pursuit_limit_m &&
                 Distance(contact.estimated_pose.position, p.task->intent.objective_position) >
                     *p.task->authority.pursuit_limit_m))
                continue;
            const float distance = Distance(s.pose.position, contact.estimated_pose.position);
            if ((distance < best_distance || (distance == best_distance && best &&
                                              contact.regiment < best->regiment)) &&
                ((preferred && !group_commitment) || c.can_approach(s.node, contact))) {
                best = &contact;
                best_distance = distance;
            }
        }
        return best;
    };
    bool attempted_flank = false;
    bool ordered_flank = false;
    bool ordered_secondary = false;
    bool policy_changed = false;
    bool policy_kept_support = false;
    bool retained_order = false;
    std::vector<Vec3> support_goals;
    for (auto *child : children) {
        const auto &s = status_of(child->id);
        const auto *previous = previous_assignment(child->id);
        std::optional<Pose> flank;
        NodeId supported;
        TaskKind kind = action;
        Vec3 goal = option.destination;
        RegimentId child_target = option.target_regiment;
        bool reserve = child->sector_role == SectorRole::Reserve;
        if (state == FsmState::Regrouping && action == TaskKind::HoldArea) {
            kind = TaskKind::HoldArea;
            goal = s.pose.position;
            child_target = {};
        } else if (task_status == TaskStatus::Failed || task_status == TaskStatus::Cancelled) {
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
            goal = previous && previous->action == kind ? previous->intent.objective_position
                                                         : s.pose.position;
            child_target = {};
        } else if (state == FsmState::Attacking && option.target_regiment) {
            kind = child == lead ? (action == TaskKind::Flank ? TaskKind::Flank : TaskKind::Attack)
                                 : TaskKind::Support;
            if (child != lead) {
                supported = lead->id;
                child_target = lead->regiment;
                goal = s.pose.position;
                const bool commitment = group_commitment && !reserve && !planned_flank;
                const auto support_goal = [&](bool keep_previous) {
                    if (keep_previous && previous && previous->action == TaskKind::Support &&
                        previous->intent.target_command == lead->id)
                        return previous->intent.objective_position;
                    if (planned_flank || (target && target->source != ObservationSource::Memory)) {
                        const Pose approach = planned_flank.value_or(
                            Pose{target ? target->estimated_pose.position : option.destination, yaw});
                        if (const auto stage = SupportStage(c, p.task, s, status_of(lead->id),
                                                            approach, support_goals))
                            return *stage;
                    }
                    return s.pose.position;
                };
                std::optional<Vec3> staged_support;
                bool reuse_support_goal = !commitment;
                if (can_plan_support && child->commander) {
                    const bool use_policy = c.group_policy && children.size() == 2 && !reserve &&
                        !s.melee_engaged && !planned_flank;
                    const auto execution_window = std::max<Tick>(
                        60, c.profile ? c.profile->minimum_commitment_ticks : 0);
                    const auto retain = [&] {
                        retained_order = true;
                        out.subordinate_tasks.push_back(*previous);
                    };
                    const bool observable_assignment = previous && previous->id && c.observe_order &&
                        previous->author == c.node->commander && previous->created_tick <= c.tick &&
                        previous->status == TaskStatus::Active &&
                        (!previous->expiry_tick || *previous->expiry_tick > c.tick);
                    if (!commitment && observable_assignment) {
                        const auto &intent = previous->intent;
                        const auto *alternate = previous->action == TaskKind::Attack
                            ? Contact(c, intent.target_regiment) : nullptr;
                        const bool can_continue =
                            (previous->action == TaskKind::Support && intent.target_command == lead->id) ||
                            (previous->action == TaskKind::Flank && intent.flank_waypoint &&
                             intent.target_regiment == target->regiment) ||
                            (alternate && alternate->source != ObservationSource::Memory &&
                             p.task->authority.may_change_target &&
                             (!p.task->authority.pursuit_limit_m ||
                              Distance(alternate->estimated_pose.position, p.task->intent.objective_position) <=
                                  *p.task->authority.pursuit_limit_m));
                        if (can_continue) {
                            const auto observation = c.observe_order(*previous);
                            if (observation && (StillExecutingAssignment(*observation, c.tick, execution_window) ||
                                (s.melee_engaged && s.attack_target && s.attack_target == intent.target_regiment))) {
                                retain();
                                continue;
                            }
                        }
                    }
                    const KnownContact *secondary = nullptr;
                    if (p.task->authority.may_change_target && c.can_approach) {
                        if (previous && previous->action == TaskKind::Attack)
                            secondary = secondary_contact(s, previous->intent.target_regiment);
                        if (!secondary)
                            secondary = secondary_contact(s, {});
                    }
                    std::optional<ChildOrderObservation> observation;
                    if (commitment && observable_assignment) {
                        const auto &intent = previous->intent;
                        const bool reachable_support = previous->action == TaskKind::Support &&
                            intent.target_command == lead->id && c.can_stage &&
                            c.can_stage(s.node, intent.objective_position);
                        const bool reachable_attack = previous->action == TaskKind::Attack &&
                            secondary && secondary->regiment == intent.target_regiment;
                        const bool reachable_flank = previous->action == TaskKind::Flank &&
                            intent.target_regiment == target->regiment && intent.flank_waypoint &&
                            c.can_stage && c.can_stage(s.node, intent.flank_waypoint->position);
                        if (reachable_support || reachable_attack || reachable_flank)
                            observation = c.observe_order(*previous);
                        if (reachable_support && observation)
                            reuse_support_goal = true;
                    }
                    if (observation &&
                        (observation->pending ||
                         (observation->delivered_tick && *observation->delivered_tick <= c.tick &&
                          c.tick - *observation->delivered_tick < execution_window) ||
                         (s.melee_engaged && s.attack_target &&
                          previous->intent.target_regiment == s.attack_target))) {
                        retain();
                        continue;
                    }
                    // A confirmed pin reduces the force needed to take the flank.
                    // Both estimates come from this commander's situation.
                    const float flank_share = lead_pins ? .15f : .30f;
                    const bool enough_to_flank = target->estimated_strength &&
                        float(s.living) >= flank_share * *target->estimated_strength;
                    if (enough_to_flank && (!secondary || use_policy || commitment) &&
                        !ordered_flank && !planned_flank && c.choose_flank) {
                        attempted_flank |= !secondary;
                        flank = c.choose_flank(child->id, lead_pins ? lead->regiment : RegimentId{},
                                               *target, *p.task);
                    }
                    if (secondary)
                        kind = TaskKind::Attack;
                    else if (flank)
                        kind = TaskKind::Flank;
                    if ((use_policy || commitment) && (secondary || flank || observation)) {
                        staged_support = support_goal(reuse_support_goal);
                        auto score_context = c;
                        if (!use_policy)
                            score_context.group_policy = nullptr;
                        const auto selected = ScoreSecondRegiment(score_context, status_of(lead->id),
                            s, *target, secondary, flank, *staged_support, previous, kind,
                            observation, execution_window);
                        policy_changed |= selected.policy_override;
                        if (selected.keep) {
                            retain();
                            continue;
                        }
                        kind = selected.action;
                    }
                    if (kind == TaskKind::Attack) {
                        supported = {};
                        child_target = secondary->regiment;
                        goal = previous && previous->action == TaskKind::Attack &&
                            previous->intent.target_regiment == child_target
                            ? previous->intent.objective_position : secondary->estimated_pose.position;
                        assigned_targets.push_back(child_target);
                        ordered_secondary = true;
                    } else if (kind == TaskKind::Flank) {
                        ordered_flank = true;
                        supported = {};
                        child_target = target->regiment;
                        goal = p.task->intent.objective_position;
                    } else if (use_policy && (secondary || flank)) {
                        policy_kept_support = true;
                    }
                    if (kind != TaskKind::Flank)
                        flank.reset();
                }
                if (kind == TaskKind::Support) {
                    goal = staged_support ? *staged_support : support_goal(reuse_support_goal);
                    support_goals.push_back(goal);
                }
            } else if (kind == TaskKind::Flank && planned_flank) {
                flank = planned_flank;
            }
        } else if (state == FsmState::Supporting && action == TaskKind::Support && p.task &&
                   p.task->intent.target_command) {
            supported = p.task->intent.target_command;
            child_target = p.task->intent.target_regiment;
            if (child_target == child->regiment)
                child_target = {};
            // The parent checked this exact position for the lead formation.
            if (child != lead) {
                goal = s.pose.position;
                if (previous && previous->action == kind &&
                    previous->intent.target_command == supported) {
                    goal = previous->intent.objective_position;
                } else if (const auto stage = SupportStage(c, p.task, s, status_of(lead->id),
                            {option.destination, yaw}, support_goals)) {
                    goal = *stage;
                }
                support_goals.push_back(goal);
            }
        } else {
            kind = state == FsmState::Deploying ? TaskKind::MoveToArea : action;
            float lateral = offset + s.width_m * .5f + 1;
            if (defend_objective) {
                if (child == lead) {
                    lateral = 0.f;
                } else {
                    const bool left = lateral < 0.f;
                    auto &edge = left ? left_edge : right_edge;
                    lateral = (edge + s.width_m * .5f + 2.f) * (left ? -1.f : 1.f);
                    edge += s.width_m + 2.f;
                }
            }
            goal = option.destination + Right(yaw) * lateral;
            if (!reserve)
                offset += s.width_m + 2;
            if (kind == TaskKind::Support && p.task) {
                supported = p.task->intent.target_command;
                child_target = p.task->intent.target_regiment;
                if (child_target == child->regiment)
                    child_target = {};
            }
        }
        auto task = ChildTask(c, p, *child, kind, goal, child_target);
        task.intent.target_command = supported;
        task.intent.flank_waypoint = flank;
        if (flank || (kind == TaskKind::Attack && (army_attack || child != lead)))
            task.authority.may_change_target = false;
        out.subordinate_tasks.push_back(std::move(task));
    }
    if (lead && state == FsmState::Attacking && option.target_regiment) {
        const auto lead_name = lead->name.empty() ? "Regiment " + std::to_string(lead->regiment.value) : lead->name;
        out.explanation = lead_name + " leads the attack. ";
        out.explanation += retained_order ? "The second regiment continues its current assignment."
            : ordered_secondary
            ? "The second regiment attacks another observed enemy."
            : ordered_flank ? "The second regiment takes the open flank."
            : policy_kept_support ? "Keep the supporting position beside the lead regiment."
            : attempted_flank ? "Keep the supporting position while the flank is closed."
                              : "The other regiments hold their supporting assignments.";
        if (policy_changed)
            out.explanation += GroupPolicyOverrideExplanation;
        auto &history = out.next_memory->recent_decisions;
        if (!history.empty() && history.back().tick == c.tick)
            history.back().explanation = out.explanation;
        else if (history.empty() || history.back().explanation != out.explanation) {
            history.push_back({c.tick, out.source_task, option, out.explanation});
            if (history.size() > 64)
                history.erase(history.begin());
        }
    }
    return {StubStatus::Ready, std::move(out), "Group FSM evaluated."};
}
} // namespace battle::command
