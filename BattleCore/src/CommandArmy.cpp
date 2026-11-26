#include "CommandBrainCommon.h"
#include <limits>
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
    const bool commands_groups = std::any_of(children.begin(), children.end(),
        [](auto child) { return child->level == Level::Group; });
    auto previous_assignment = [&](NodeId id) -> const CommandTask * {
        const auto found = std::find_if(c.previous_subordinate_tasks.begin(),
            c.previous_subordinate_tasks.end(), [&](const auto &task) {
                return task.recipient == id && task.parent_task == p.memory.active_task;
            });
        return found == c.previous_subordinate_tasks.end() ? nullptr : &*found;
    };

    TaskKind action = p.task ? p.task->action : TaskKind::HoldArea;
    TacticalOption option;
    option.action = action;
    option.destination =
        p.task ? p.task->intent.objective_position : c.situation->own.pose.position;
    bool attack =
        action == TaskKind::Attack || action == TaskKind::Exploit || action == TaskKind::Flank;
    const KnownContact *target = nullptr;
    if (p.task && attack && (p.task->intent.target_regiment || p.task->authority.may_change_target))
        target = commands_groups ? AttackContact(c, p) : Contact(c, p.task->intent.target_regiment);
    if (!p.task) {
        if (const auto *visible = Contact(c); visible && AttackWorthwhile(c, *visible)) {
            target = visible;
            attack = true;
            action = TaskKind::Attack;
        } else {
            action = TaskKind::HoldArea;
        }
    }
    const bool area_attack = commands_groups && p.task && action == TaskKind::Attack &&
        !p.task->intent.target_regiment && p.task->authority.may_change_target &&
        p.task->authority.may_pursue;
    const bool search_for_contact = area_attack && !target;

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
    if (!supported) {
        Tick request_tick = 0;
        for (const auto &report : c.reports) {
            if (report.kind != ReportKind::NeedSupport || report.recipient != c.node->id ||
                report.observed_tick > c.tick)
                continue;
            const auto sender = std::ranges::find_if(children, [&](const CommandNode *child) {
                return child->id == report.sender && child->sector_role != SectorRole::Reserve;
            });
            if (sender == children.end())
                continue;
            if (!supported || report.observed_tick > request_tick ||
                (report.observed_tick == request_tick && report.sender < supported->id)) {
                supported = *sender;
                request_tick = report.observed_tick;
            }
            threatened = true;
        }
    }
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
    } else if (search_for_contact) {
        state = FsmState::Advancing;
        action = TaskKind::MoveToArea;
        reason = "Advance through assigned sectors to establish enemy contact.";
    } else if (attack && !target && commands_groups && p.task && p.task->authority.may_pursue) {
        state = FsmState::Regrouping;
        action = TaskKind::HoldArea;
        option.destination = c.situation->own.pose.position;
        reason = "Regroup and watch for the assigned enemy.";
    } else if (attack && !target) {
        state = FsmState::Failed;
        task_status = TaskStatus::Failed;
        action = TaskKind::HoldArea;
        attack = false;
        reason = "Assigned attack target is no longer visible; hold assigned sectors.";
    } else if (attack) {
        state = FsmState::Advancing;
        option.target_regiment = target->regiment;
        if (area_attack || option.destination == Vec3{} || (commands_groups && !p.task))
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
        case TaskKind::Defend:
            state = FsmState::Holding;
            reason = "Occupy and defend the assigned objective.";
            break;
        default:
            state = FsmState::Holding;
            action = TaskKind::HoldArea;
            break;
        }
    }
    if (threatened && state != FsmState::Withdrawing && state != FsmState::Failed &&
        state != FsmState::Complete && state != FsmState::Regrouping) {
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

    const CommandNode *center = nullptr;
    const bool coordinate_groups = commands_groups && target && attack &&
        option.target_regiment == target->regiment &&
        (state == FsmState::Advancing || state == FsmState::Supporting);
    if (coordinate_groups) {
        const auto center_rank = [&](const CommandNode &child) {
            const auto *old = previous_assignment(child.id);
            const bool committed = old && old->action == TaskKind::Attack &&
                                   old->intent.target_regiment == target->regiment;
            return std::tuple(child.sector_role != SectorRole::Center, !committed,
                Distance(status_of(child.id).pose.position, target->estimated_pose.position), child.id);
        };
        for (const auto *child : children)
            if (child->level == Level::Group && child->commander &&
                child->sector_role != SectorRole::Reserve && !status_of(child->id).panic &&
                (!center || center_rank(*child) < center_rank(*center)))
                center = child;
    }

    float frontage = 0;
    for (const auto *child : children)
        if (child->sector_role != SectorRole::Reserve)
            frontage += status_of(child->id).width_m + 8;
    float offset = -frontage * .5f;
    Vec3 direction = option.destination - c.situation->own.pose.position;
    float yaw =
        Length(direction) > 0 ? std::atan2(direction.y, direction.x) : c.situation->own.pose.yaw;
    const bool release_reserve = state == FsmState::Supporting;
    const auto search_forward = Forward(c.situation->own.pose.yaw);
    const auto search_right = Right(c.situation->own.pose.yaw);
    const bool searching = search_for_contact && action == TaskKind::MoveToArea;
    bool probe_issued = false;
    bool planned_flank = false, planned_support = false;
    bool sector_attacks = false, holding_outer_sector = false;
    std::vector<RegimentId> assigned_targets{option.target_regiment};
    const auto is_wing = [](const CommandNode &group) {
        return group.sector_role == SectorRole::LeftFlank || group.sector_role == SectorRole::RightFlank;
    };
    const Vec3 center_position = center ? status_of(center->id).pose.position : Vec3{};
    const Vec3 center_approach = target ? target->estimated_pose.position - center_position : Vec3{};
    const float front_yaw = Length(center_approach) > .01f
        ? std::atan2(center_approach.y, center_approach.x) : yaw;
    const auto across_front = Right(front_yaw);
    const auto wing_target = [&](const CommandNode &wing, const CommandTask *old)
        -> const KnownContact * {
        if (!p.task || !p.task->authority.may_change_target || !is_wing(wing))
            return nullptr;
        const auto wing_position = status_of(wing.id).pose.position;
        const auto lateral = across_front * (wing.sector_role == SectorRole::LeftFlank ? -1.f : 1.f);
        // Use actual lateral separation because regiments may occupy several ranks in depth.
        const float sector_edge = std::max(2.f, .5f * Dot(wing_position - center_position, lateral));
        const KnownContact *best = nullptr;
        float best_distance = std::numeric_limits<float>::infinity();
        for (const auto &contact : c.situation->contacts) {
            const auto position = contact.estimated_pose.position;
            if (!contact.regiment || contact.allegiance != Allegiance::Enemy ||
                (contact.team && *contact.team == c.node->team) ||
                contact.source == ObservationSource::Memory || contact.confidence <= 0 ||
                (contact.estimated_strength && *contact.estimated_strength <= 0) ||
                std::ranges::find(assigned_targets, contact.regiment) != assigned_targets.end() ||
                Dot(position - target->estimated_pose.position, lateral) < sector_edge ||
                (p.task->authority.pursuit_limit_m &&
                 Distance(position, p.task->intent.objective_position) > *p.task->authority.pursuit_limit_m))
                continue;
            if (old && old->action == TaskKind::Attack && old->intent.target_regiment == contact.regiment)
                return &contact;
            const float distance = Distance(position, wing_position);
            if (distance < best_distance || (distance == best_distance && best &&
                                             contact.regiment < best->regiment)) {
                best = &contact;
                best_distance = distance;
            }
        }
        return best;
    };
    const CommandNode *objective_anchor = nullptr;
    if (p.task && p.task->intent.objective_radius_m > 0.f &&
        (action == TaskKind::Defend || action == TaskKind::MoveToArea)) {
        for (const auto *child : children) {
            if (!child->commander || child->sector_role == SectorRole::Reserve ||
                status_of(child->id).panic)
                continue;
            const auto rank = [&](const CommandNode *candidate) {
                return std::tuple(candidate->sector_role != SectorRole::Center,
                                  Distance(status_of(candidate->id).pose.position, option.destination),
                                  candidate->id);
            };
            if (!objective_anchor || rank(child) < rank(objective_anchor))
                objective_anchor = child;
        }
    }
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
        } else if (child->level == Level::Group && child_status.panic) {
            child_action = TaskKind::Rally;
            goal = c.situation->rally_point;
            target_id = {};
        } else if (child->sector_role == SectorRole::Reserve && !release_reserve) {
            child_action = TaskKind::Reserve;
            goal = child_status.pose.position;
            target_id = {};
        } else if (child->sector_role == SectorRole::Reserve && supported) {
            // A released reserve serves the threatened sector, even during a coordinated attack.
            child_action = TaskKind::Support;
            goal = status_of(supported->id).pose.position;
            target_id = {};
            if (target && c.choose_group_support && child->level == Level::Group &&
                supported->level == Level::Group) {
                const auto *old = previous_assignment(child->id);
                if (old && old->action == TaskKind::Support && old->intent.target_command == supported->id) {
                    goal = old->intent.objective_position;
                } else if (const auto staging = c.choose_group_support(child->id, supported->id, *target)) {
                    goal = *staging;
                } else {
                    child_action = TaskKind::Reserve;
                    goal = child_status.pose.position;
                }
            }
        } else if (searching && child->level == Level::Group) {
            const auto *old = previous_assignment(child->id);
            const auto formation = child_status.formation_center.value_or(child_status.pose.position);
            child_action = TaskKind::HoldArea;
            goal = old && old->action == TaskKind::HoldArea ? old->intent.objective_position : formation;
            target_id = {};
            const auto within_search_area = [&](Vec3 position) {
                return !p.task->authority.pursuit_limit_m ||
                    Distance(position, p.task->intent.objective_position) <=
                        *p.task->authority.pursuit_limit_m;
            };
            const bool has_probe = old && old->action == TaskKind::MoveToArea &&
                                   within_search_area(old->intent.objective_position);
            bool extend_probe = false;
            if (has_probe) {
                goal = old->intent.objective_position;
                child_action = TaskKind::MoveToArea;
                if (Dot(option.destination - goal, search_forward) > 1.f) {
                    const auto observed = c.observe_order ? c.observe_order(*old) : std::nullopt;
                    extend_probe = observed ? observed->progress >= 1.f :
                        Dot(goal - formation, search_forward) <= 2.f;
                }
            }
            if (!has_probe || extend_probe) {
                // Keep the group's lateral lane. If the checkpoint is blocked,
                // try a nearer point on the same lane, still ahead of the group.
                const auto lane = has_probe ? goal : formation;
                const auto checkpoint = option.destination + search_right *
                    Dot(lane - option.destination, search_right);
                for (const auto setback : {0.f, 12.f, 24.f}) {
                    const auto candidate = checkpoint - search_forward * setback;
                    if (Dot(candidate - formation, search_forward) <= 1.f ||
                        (has_probe && Dot(candidate - goal, search_forward) <= 1.f) ||
                        !within_search_area(candidate) ||
                        (c.can_stage && !c.can_stage(child->id, candidate)))
                        continue;
                    goal = candidate;
                    child_action = TaskKind::MoveToArea;
                    break;
                }
            }
            probe_issued |= child_action == TaskKind::MoveToArea;
        } else if (center && child->level == Level::Group && child->commander) {
            auto task = ChildTask(c, p, *child, TaskKind::Attack, goal, target_id);
            // Keep the original objective while this group pursues the same regiment.
            const auto *old = previous_assignment(child->id);
            if (old && old->intent.target_regiment == task.intent.target_regiment)
                task.intent.objective_position = old->intent.objective_position;
            if (child != center) {
                if (const auto *enemy = wing_target(*child, old)) {
                    const auto position = enemy->estimated_pose.position;
                    task.intent.target_regiment = enemy->regiment;
                    if (old && old->action == TaskKind::Attack && old->intent.target_regiment == enemy->regiment &&
                        (!old->authority.pursuit_limit_m ||
                         Distance(position, old->intent.objective_position) <= *old->authority.pursuit_limit_m)) {
                        task.intent.objective_position = old->intent.objective_position;
                        task.authority.pursuit_limit_m = old->authority.pursuit_limit_m;
                    } else {
                        task.intent.objective_position = position;
                        if (task.authority.pursuit_limit_m) {
                            // Keep the delegated pursuit area inside the army's original order.
                            const float objective_offset = Distance(position, p.task->intent.objective_position);
                            task.authority.pursuit_limit_m =
                                std::max(0.f, *task.authority.pursuit_limit_m - objective_offset);
                        }
                    }
                    assigned_targets.push_back(enemy->regiment);
                    sector_attacks = true;
                    out.subordinate_tasks.push_back(std::move(task));
                    continue;
                }
                if (p.task && p.task->authority.may_change_target && p.task->authority.pursuit_limit_m &&
                    is_wing(*child) && std::abs(Dot(child_status.pose.position -
                        p.task->intent.objective_position, across_front)) > *p.task->authority.pursuit_limit_m) {
                    task.action = TaskKind::HoldArea;
                    task.intent.target_regiment = {};
                    task.intent.objective_position = old && old->action == TaskKind::HoldArea
                        ? old->intent.objective_position : child_status.pose.position;
                    holding_outer_sector = true;
                    out.subordinate_tasks.push_back(std::move(task));
                    continue;
                }
                if (c.choose_group_flank && is_wing(*child)) {
                    task.intent.flank_waypoint = c.choose_group_flank(child->id, *target, task);
                }
                if (task.intent.flank_waypoint) {
                    task.action = TaskKind::Flank;
                    task.authority.may_change_target = false;
                    planned_flank = true;
                } else {
                    std::optional<Vec3> staging;
                    if (old && old->action == TaskKind::Support && old->intent.target_command == center->id)
                        staging = old->intent.objective_position;
                    else if (c.choose_group_support)
                        staging = c.choose_group_support(child->id, center->id, *target);
                    task.intent.target_regiment = {};
                    if (staging) {
                        task.action = TaskKind::Support;
                        task.intent.target_command = center->id;
                        task.intent.objective_position = *staging;
                        planned_support = true;
                    } else {
                        task.action = TaskKind::Reserve;
                        task.intent.objective_position = old && old->action == TaskKind::Reserve
                            ? old->intent.objective_position : child_status.pose.position;
                    }
                }
            }
            out.subordinate_tasks.push_back(std::move(task));
            continue;
        } else if (child->level == Level::Group && state == FsmState::Regrouping && attack) {
            goal = child_status.pose.position;
        } else if (child != objective_anchor && child->sector_role != SectorRole::Reserve) {
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
    if (center || (searching && !probe_issued)) {
        out.explanation = !center ? "Hold position because no clear route reaches the search area."
                          : sector_attacks ? "Attack the observed opposition in the assigned sectors."
                          : holding_outer_sector ? "Center attacks while the distant wings hold their sectors."
                          : planned_flank ? "Center attacks while the wings take open flanks."
                          : planned_support ? "Center attacks with wings in support."
                                            : "Center attacks while the wings wait in reserve.";
        auto &history = out.next_memory->recent_decisions;
        if (!history.empty() && history.back().tick == c.tick)
            history.back().explanation = out.explanation;
        else if (history.empty() || history.back().explanation != out.explanation) {
            history.push_back({c.tick, out.source_task, option, out.explanation});
            if (history.size() > 64)
                history.erase(history.begin());
        }
    }
    return {StubStatus::Ready, std::move(out), "Army FSM evaluated."};
}
} // namespace battle::command
