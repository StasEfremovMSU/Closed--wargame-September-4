#include "ArmyStaff.h"
#include "CommandBrainCommon.h"
#include "LocalMelee.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Navigation.h"
#include "battle/Officers.h"
#include "battle/World.h"
#include "battle/command/Runtime.h"
#include "battle/command/Services.h"
#include <array>
#include <map>
#include <set>
#include <tuple>

namespace battle::command {
namespace {
constexpr Tick ContactTtl = 100;
constexpr Tick DeploymentBriefingTicks = 10; // The opening half-second lets the first reports reach army HQ.
constexpr size_t HistoryLimit = 64;

Tick DefaultInterval(Level level, bool adaptive = false) {
    switch (level) {
    case Level::Army:
        return adaptive ? 100 : 40;
    case Level::Part:
        return adaptive ? 60 : 20;
    case Level::Group:
        return adaptive ? 40 : 10;
    case Level::Regiment:
        return adaptive ? 20 : 5;
    }
    return 5;
}

Tick BrainPhase(NodeId node, Tick interval) {
    interval = std::max<Tick>(1, interval);
    return ((node.value % interval) * 17) % interval;
}

Tick NextBrainPhase(NodeId node, Tick interval, Tick tick) {
    interval = std::max<Tick>(1, interval);
    const Tick phase = BrainPhase(node, interval);
    const Tick remainder = tick % interval;
    const Tick delay = phase > remainder ? phase - remainder : interval - remainder + phase;
    return delay > UINT64_MAX - tick ? UINT64_MAX : tick + delay;
}

NodeRuntime *FindRuntime(CommandState &state, NodeId id) {
    auto it = std::find_if(state.nodes.begin(), state.nodes.end(),
                           [&](const auto &node) { return node.node == id; });
    return it == state.nodes.end() ? nullptr : &*it;
}
const NodeRuntime *FindRuntime(const CommandState &state, NodeId id) {
    return FindRuntime(const_cast<CommandState &>(state), id);
}

void AdvanceGroupCommanders(World &world, const CommandHierarchy &hierarchy,
                            const NavigationService &navigation) {
    if (std::none_of(world.units.begin(), world.units.end(),
                     [](const auto &unit) { return unit.alive && !unit.regiment; }))
        return;
    constexpr float Radius = .3f;
    constexpr float Dt = .05f;
    auto barriers = world.scenario.obstacles;
    std::erase_if(barriers, [](const auto &box) { return !TerrainBlocksMovement(box); });
    for (auto &box : barriers) {
        box.min_x -= Radius;
        box.min_y -= Radius;
        box.max_x += Radius;
        box.max_y += Radius;
    }
    for (const auto &node : hierarchy.nodes) {
        if (node.level != Level::Group || !node.commander)
            continue;
        const auto officer = std::lower_bound(
            world.officers.begin(), world.officers.end(), node.commander,
            [](const auto &record, OfficerId id) { return record.id < id; });
        if (officer == world.officers.end() || officer->id != node.commander)
            continue;
        auto *unit = FindUnit(world, officer->unit);
        if (!unit || !unit->alive || unit->regiment || unit->control == ControlMode::PlayerControlled)
            continue;
        const auto position = unit->pose.position;
        if (world.tick % 10 == node.id.value % 10 ||
            Distance(position, unit->slot.position) < .05f) {
            Vec3 rear{};
            Vec3 heading{};
            std::vector<Vec3> rear_positions;
            for (auto child_id : node.children) {
                const auto *child = FindNode(hierarchy, child_id);
                const auto *regiment = child ? FindRegiment(world, child->regiment) : nullptr;
                if (!regiment || regiment->units.empty())
                    continue;
                const auto forward = Forward(regiment->pose.yaw);
                const auto behind = regiment->pose.position -
                    forward * (Footprint(*regiment).depth_m * .5f + 3.f);
                rear_positions.push_back(behind);
                rear = rear + behind;
                heading = heading + forward;
            }
            unit->slot = unit->pose;
            if (!rear_positions.empty()) {
                Vec3 target = rear * (1.f / float(rear_positions.size()));
                const float heading_length = Length(heading);
                if (heading_length > .1f) {
                    const auto forward = heading * (1.f / heading_length);
                    float behind = 0;
                    for (const auto point : rear_positions)
                        behind = std::min(behind, Dot(point - target, forward));
                    target = target + forward * behind;
                }
                if (HasLineOfSight(position, target, barriers)) {
                    unit->slot.position = target;
                } else if (Distance(position, target) > .15f) {
                    const auto path =
                        navigation.FindPath({unit->pose, target, {Radius * 2, Radius * 2}, {},
                                             {}, world.scenario.rules.adaptive_battle});
                    for (const auto &point : path.waypoints) {
                        if (!HasLineOfSight(position, point.position, barriers))
                            break;
                        unit->slot.position = point.position;
                    }
                }
            }
        }
        const auto delta = unit->slot.position - position;
        const auto distance = Length(delta);
        if (distance < .05f)
            continue;
        auto next = position + delta * (std::min(distance, unit->speed * Dt) / distance);
        if (world.scenario.hill)
            next = position + (next - position) * HillUphillSpeedMultiplier(*world.scenario.hill, position, next, unit->troop, unit->speed);
        if (HasLineOfSight(position, next, barriers)) {
            unit->pose.position = next;
            const float desired_yaw = std::atan2(delta.y, delta.x);
            const float turn = std::remainder(desired_yaw - unit->pose.yaw, 2 * Pi);
            unit->pose.yaw += std::clamp(turn, -3 * Dt, 3 * Dt);
        }
    }
}

bool EligibleSuccessor(const World &world, const CommandHierarchy &hierarchy,
                       const CommandNode &node, OfficerId id) {
    const auto officer = std::find_if(world.officers.begin(), world.officers.end(),
                                      [&](const auto &candidate) { return candidate.id == id; });
    if (officer == world.officers.end() || (officer->role != 1 && officer->role != 2))
        return false;
    const auto *unit = FindUnit(world, officer->unit);
    if (!unit || !unit->alive || UnitTeam(world, *unit) != node.team ||
        (node.level == Level::Regiment && unit->regiment != node.regiment) ||
        !FindProfile(hierarchy, id))
        return false;
    return std::none_of(hierarchy.nodes.begin(), hierarchy.nodes.end(), [&](const auto &other) {
        return other.id != node.id && other.commander == id;
    });
}

OfficerId ResolveSuccessor(const World &world, const CommandHierarchy &hierarchy,
                           const CommandNode &node) {
    if (EligibleSuccessor(world, hierarchy, node, node.commander))
        return node.commander;
    for (auto candidate : node.succession)
        if (EligibleSuccessor(world, hierarchy, node, candidate))
            return candidate;
    if (node.level == Level::Regiment) {
        OfficerId best{};
        for (const auto &officer : world.officers)
            if (EligibleSuccessor(world, hierarchy, node, officer.id) &&
                (!best || officer.id < best))
                best = officer.id;
        return best;
    }
    return {};
}

bool SameAssignment(CommandTask a, CommandTask b) {
    a.id = {};
    b.id = {};
    a.created_tick = b.created_tick = 0;
    a.received_tick.reset();
    b.received_tick.reset();
    a.status = b.status = TaskStatus::Active;
    return a == b;
}

bool SameOrder(const Order &a, const Order &b, bool regiment_tactics) {
    const bool engaged_on_same_target = regiment_tactics && IsAttacking(a.type) && a.target_regiment;
    return a.issuer == b.issuer && a.recipient == b.recipient && a.type == b.type &&
           a.priority == b.priority && (engaged_on_same_target || Distance(a.point, b.point) <= 1.f) &&
           a.target_unit == b.target_unit && a.target_regiment == b.target_regiment &&
           a.expiry_tick == b.expiry_tick && a.pace == b.pace && a.facing == b.facing &&
           a.move_columns == b.move_columns && a.maneuver_group == b.maneuver_group;
}

bool ExternalOrder(const World &world, const RegimentState &regiment, OrderId owned) {
    if (regiment.following_officer && !regiment.panic)
        return true;
    if (regiment.order && (regiment.order->status == OrderStatus::Active ||
                          (regiment.order->player_direct && regiment.order->status == OrderStatus::Completed)) &&
        regiment.order->id != owned)
        return true;
    return std::any_of(world.pending_orders.begin(), world.pending_orders.end(),
                       [&](const auto &order) {
                           return order.recipient == regiment.id &&
                                  order.status == OrderStatus::Pending && order.id != owned;
                       });
}

void CancelObsoleteAssignments(NodeRuntime &runtime, Tick tick) {
    for (auto &old : runtime.orders) {
        if (!old.issuer || old.status != TaskStatus::Active)
            continue;
        const auto replaced = [&](const auto &assignment) {
            if (assignment.issuer != old.issuer || assignment.priority < old.priority ||
                std::tie(assignment.created_tick, assignment.id) <=
                    std::tie(old.created_tick, old.id) ||
                (assignment.not_before_tick && *assignment.not_before_tick > tick))
                return false;
            if (assignment.status == TaskStatus::Completed || assignment.status == TaskStatus::Failed)
                return true;
            if (assignment.status != TaskStatus::Active)
                return false;
            const auto started = assignment.id == runtime.memory.active_task
                                     ? runtime.memory.task_started_tick : tick;
            const auto expiry = detail::TaskExpiry(assignment, started);
            return !expiry || *expiry > tick;
        };
        if (std::any_of(runtime.orders.begin(), runtime.orders.end(), replaced))
            old.status = TaskStatus::Cancelled;
    }
}

void QueueMessage(CommandState &state, NodeId sender, NodeId recipient, Tick tick,
                  std::variant<CommandTask, SituationReport> payload,
                  DeliveryChannel channel = DeliveryChannel::Signal) {
    MessageEnvelope message;
    message.id = state.next_message_id;
    if (state.next_message_id.value != UINT64_MAX)
        ++state.next_message_id.value;
    message.sender = sender;
    message.recipient = recipient;
    message.channel = channel;
    message.created_tick = tick;
    message.delivery_tick = tick == UINT64_MAX ? tick : tick + 1;
    message.status = DeliveryStatus::InTransit;
    message.payload = std::move(payload);
    if (const auto *task = std::get_if<CommandTask>(&message.payload))
        message.expiry_tick = task->expiry_tick;
    if (channel == DeliveryChannel::Courier) {
        message.status = DeliveryStatus::Queued;
        const Tick expiry = detail::Later(tick, 1800);
        message.expiry_tick = std::clamp(message.expiry_tick.value_or(expiry), tick, expiry);
    }
    state.communications.messages.push_back(std::move(message));
}

bool ReserveEarlierCourierHolds(World &world, CommandState &state, const CommandTask &attack) {
    std::vector<CommandTask *> holds;
    for (auto &message : state.communications.messages) {
        if (message.channel != DeliveryChannel::Courier || message.sender != attack.issuer ||
            message.recipient != attack.recipient ||
            (message.status != DeliveryStatus::Queued && message.status != DeliveryStatus::InTransit))
            continue;
        auto *hold = std::get_if<CommandTask>(&message.payload);
        if (hold && hold->action == TaskKind::HoldArea && hold->author == attack.author &&
            hold->priority == PlayerOrderPriority && !hold->supersede_player_orders_through &&
            std::tie(hold->created_tick, hold->id) < std::tie(attack.created_tick, attack.id))
            holds.push_back(hold);
    }
    if (holds.empty())
        return true;
    const auto count = OwnedRegiments(state.hierarchy, attack.recipient).size();
    if (count && holds.size() > (UINT64_MAX - world.next_order_id) / count)
        return false;
    // Reserve the same order IDs even if some recipients are gone when the rider arrives.
    for (auto *hold : holds) {
        hold->supersede_player_orders_through = world.next_order_id - 1;
        world.next_order_id += count;
    }
    return true;
}

bool SupersededCourierHold(const NodeRuntime &runtime, const CommandTask &hold, Tick now) {
    return hold.supersede_player_orders_through && std::ranges::any_of(runtime.orders, [&](const auto &attack) {
        return attack.action == TaskKind::Attack && attack.author == hold.author && attack.issuer == hold.issuer &&
               attack.received_tick && (!attack.not_before_tick || *attack.not_before_tick <= now) &&
               attack.supersede_player_orders_through > hold.supersede_player_orders_through &&
               std::tie(hold.created_tick, hold.id) < std::tie(attack.created_tick, attack.id);
    });
}

void HoldGroupAtItsPosts(World &world, const CommandHierarchy &hierarchy, const CommandTask &task) {
    const auto expiry = detail::TaskExpiry(task, world.tick);
    if ((task.not_before_tick && *task.not_before_tick > world.tick) ||
        (expiry && *expiry <= world.tick))
        return;
    auto reserved_id = task.supersede_player_orders_through.value_or(0);
    for (const auto id : OwnedRegiments(hierarchy, task.recipient)) {
        const OrderId order_id{task.supersede_player_orders_through ? ++reserved_id : 0};
        const auto *regiment = FindRegiment(world, id);
        if (!regiment || regiment->units.empty() || regiment->panic)
            continue;
        if (order_id) {
            const auto newer = [&](const Order &order) {
                return order.recipient == id && order.player_direct && order.id > order_id &&
                       order.created_tick >= task.created_tick;
            };
            if ((regiment->order && newer(*regiment->order) &&
                 (regiment->order->status == OrderStatus::Active || regiment->order->status == OrderStatus::Completed)) ||
                std::ranges::any_of(world.pending_orders, newer))
                continue;
        }
        Order hold;
        hold.id = order_id;
        hold.issuer = task.author;
        hold.recipient = id;
        hold.type = OrderType::Hold;
        hold.point = regiment->pose.position;
        hold.priority = task.priority;
        hold.created_tick = order_id ? task.created_tick : world.tick;
        hold.delivery_tick = world.tick;
        hold.expiry_tick = expiry.value_or(0);
        hold.player_direct = true;
        if (SubmitOrder(world, hold) == Status::Ok && regiment->following_officer) {
            Command release;
            release.kind = CommandKind::SetRegimentFollowing;
            release.unit = regiment->following_officer;
            release.regiment = id;
            ApplyControlCommand(world, release);
        }
    }
}

void PrepareGroupForPlayerAttack(World &world, const CommandHierarchy &hierarchy, const CommandTask &task) {
    const auto expiry = detail::TaskExpiry(task, world.tick);
    if (!task.supersede_player_orders_through ||
        (task.not_before_tick && *task.not_before_tick > world.tick) || (expiry && *expiry <= world.tick))
        return;
    const auto members = OwnedRegiments(hierarchy, task.recipient);
    const auto earlier = [&](const Order &order) {
        return order.player_direct && order.issuer == task.author && order.created_tick <= task.created_tick &&
               order.id.value <= *task.supersede_player_orders_through;
    };
    std::erase_if(world.pending_orders, [&](const Order &order) {
        return std::binary_search(members.begin(), members.end(), order.recipient) && earlier(order);
    });
    const auto officer = std::ranges::find(world.officers, task.author, &OfficerSpec::id);
    for (const auto id : members) {
        auto *regiment = FindRegiment(world, id);
        if (!regiment || regiment->panic)
            continue;
        if (regiment->order && earlier(*regiment->order) &&
            (regiment->order->status == OrderStatus::Active || regiment->order->status == OrderStatus::Completed)) {
            regiment->order->status = OrderStatus::Cancelled;
            regiment->route.clear();
            regiment->waypoint = 0;
            regiment->march_lane.reset();
            regiment->decision_tick = regiment->decision_until = 0;
        }
        if (officer != world.officers.end() && regiment->following_officer == officer->unit) {
            Command release;
            release.kind = CommandKind::SetRegimentFollowing;
            release.unit = officer->unit;
            release.regiment = id;
            ApplyControlCommand(world, release);
        }
    }
}

bool BriefedAtDeployment(const World &world, const CommandTask &assignment) {
    if (world.tick >= DeploymentBriefingTicks || !assignment.parent_task || !world.scenario.command_setup)
        return false;
    // Opening reports refine the orders agreed before deployment. A mission
    // submitted during play has a different ID even at tick zero.
    TaskId next{1};
    for (const auto &mission : world.scenario.command_setup->missions) {
        const auto id = mission.id ? mission.id : next;
        if (id == assignment.parent_task && mission.recipient == assignment.issuer)
            return true;
        next.value = std::max(next.value, id.value + 1);
    }
    return false;
}

void AddReport(std::vector<SituationReport> &reports, const SituationReport &report) {
    auto same = [&](const auto &old) {
        return old.sender == report.sender && old.recipient == report.recipient &&
               old.about_task == report.about_task && old.kind == report.kind &&
               old.created_tick == report.created_tick;
    };
    if (std::none_of(reports.begin(), reports.end(), same))
        reports.push_back(report);
    std::erase_if(reports, [&](const auto &old) {
        return report.created_tick >= old.created_tick &&
               report.created_tick - old.created_tick > ContactTtl;
    });
    if (reports.size() > HistoryLimit)
        reports.erase(reports.begin(), reports.begin() + (reports.size() - HistoryLimit));
}

void Prune(CommandState &state, Tick tick) {
    auto old = [&](Tick created) { return tick >= created && tick - created > ContactTtl; };
    for (auto &runtime : state.nodes) {
        std::erase_if(runtime.orders, [&](const auto &task) {
            const bool terminal = task.status == TaskStatus::Completed ||
                                  task.status == TaskStatus::Failed ||
                                  task.status == TaskStatus::Cancelled;
            return terminal && old(task.created_tick);
        });
        std::erase_if(runtime.reports,
                      [&](const auto &report) { return old(report.created_tick); });
    }
    std::erase_if(state.communications.messages, [&](const auto &message) {
        const Tick last_update = message.channel == DeliveryChannel::Courier
                                     ? message.delivery_tick : message.created_tick;
        if (!old(last_update))
            return false;
        if (message.status == DeliveryStatus::Lost) {
            // A commander with a long think interval still needs the failed
            // dispatch to distinguish a retry from an identical live order.
            const auto *task = std::get_if<CommandTask>(&message.payload);
            const auto *sender = task ? FindRuntime(state, message.sender) : nullptr;
            if (sender && std::ranges::find(sender->sent_tasks, task->id, &CommandTask::id) !=
                              sender->sent_tasks.end())
                return false;
        }
        const bool terminal = message.status == DeliveryStatus::Delivered ||
                              message.status == DeliveryStatus::Expired ||
                              message.status == DeliveryStatus::Lost ||
                              message.status == DeliveryStatus::RecipientUnavailable;
        return terminal;
    });
}

bool ReportRecentlyQueued(const CommunicationsState &communications, const SituationReport &report,
                          Tick tick) {
    return std::any_of(
        communications.messages.begin(), communications.messages.end(), [&](const auto &message) {
            if (message.status == DeliveryStatus::Expired ||
                message.status == DeliveryStatus::Lost ||
                message.status == DeliveryStatus::RecipientUnavailable)
                return false;
            const auto *old = std::get_if<SituationReport>(&message.payload);
            return old && old->sender == report.sender && old->recipient == report.recipient &&
                   old->kind == report.kind && old->about_task == report.about_task &&
                   tick >= old->created_tick && tick - old->created_tick < 20;
        });
}

bool HasPanic(const World &world, const CommandHierarchy &hierarchy, NodeId id) {
    for (auto regiment_id : OwnedRegiments(hierarchy, id))
        if (const auto *regiment = FindRegiment(world, regiment_id); regiment && regiment->panic)
            return true;
    return false;
}

std::optional<DeliveryStatus> TaskDispatchStatus(const CommandState &state, TaskId id) {
    const auto &messages = state.communications.messages;
    const auto dispatch = std::find_if(messages.rbegin(), messages.rend(), [&](const auto &message) {
        const auto *task = std::get_if<CommandTask>(&message.payload);
        return task && task->id == id;
    });
    return dispatch == messages.rend() ? std::nullopt : std::optional{dispatch->status};
}

bool TaskInTransit(const CommandState &state, TaskId id) {
    const auto status = TaskDispatchStatus(state, id);
    return status == DeliveryStatus::Queued || status == DeliveryStatus::InTransit;
}

bool RetryLostAssignment(const World &world, const CommandState &state, const CommandTask &task, Tick now) {
    if (task.expiry_tick && *task.expiry_tick <= now)
        return false;
    if (UsesStaffCourier(world, task.issuer, task.recipient) &&
        !HasLivingStaffAdjutant(world, task.issuer))
        return false;
    const auto *recipient = FindRuntime(state, task.recipient);
    return recipient && std::ranges::find(recipient->orders, task.id, &CommandTask::id) ==
                            recipient->orders.end() &&
           TaskDispatchStatus(state, task.id) == DeliveryStatus::Lost;
}

std::optional<ChildOrderObservation> ObserveChildOrder(
    const World &world, const CommandState &state, const CommandTask &task,
    const RegimentState &regiment, std::span<const SituationReport> reports) {
    const auto *recipient = FindRuntime(state, task.recipient);
    if (!recipient || ExternalOrder(world, regiment, recipient->last_order_id))
        return {};
    const auto report_for = [&](const SituationReport &report) {
        return report.sender == task.recipient && report.about_task == task.id;
    };
    if (std::ranges::any_of(reports, [&](const auto &report) {
            return report_for(report) && (report.kind == ReportKind::CannotExecute ||
                                           report.kind == ReportKind::TaskCompleted);
        }))
        return {};

    const auto delivery = std::ranges::find(recipient->orders, task.id, &CommandTask::id);
    if (delivery == recipient->orders.end())
        return TaskInTransit(state, task.id)
            ? std::optional{ChildOrderObservation{true, false, std::nullopt, 0.f}} : std::nullopt;
    if (!delivery->received_tick || (delivery->status != TaskStatus::Accepted &&
                                    delivery->status != TaskStatus::Active))
        return {};
    if (recipient->memory.active_task != task.id && *delivery->received_tick < world.tick)
        return {};
    if (recipient->memory.active_task == task.id && recipient->memory.task_status != TaskStatus::Accepted &&
        recipient->memory.task_status != TaskStatus::Active)
        return {};

    // Execution telemetry is limited to the subordinate's own orders, position, and reports.
    const bool executing = recipient->memory.active_task == task.id && regiment.order &&
        regiment.order->id == recipient->last_order_id && regiment.order->status == OrderStatus::Active;
    float progress = 0;
    if (executing) {
        const auto ack = std::ranges::find_if(reports, [&](const auto &report) {
            return report_for(report) && report.kind == ReportKind::Acknowledged;
        });
        if (ack != reports.end()) {
            const auto goal = regiment.order->point;
            const auto start_distance = Distance(ack->situation.own.pose.position, goal);
            const auto remaining = Distance(regiment.pose.position, goal);
            progress = std::clamp(1.f - remaining / std::max(1.f, start_distance), 0.f, 1.f);
        }
    }
    return ChildOrderObservation{false, executing, delivery->received_tick, progress};
}

void MergeReportedContacts(LocalSituation &situation, std::span<const SituationReport> reports,
                           Tick tick) {
    std::map<uint64_t, KnownContact> contacts;
    for (const auto &contact : situation.contacts)
        contacts[contact.regiment.value] = contact;
    for (const auto &report : reports) {
        if (report.observed_tick > tick || tick - report.observed_tick > ContactTtl)
            continue;
        for (auto contact : report.situation.contacts) {
            if (!contact.regiment || contact.allegiance != Allegiance::Enemy ||
                contact.observed_tick > report.observed_tick ||
                tick - contact.observed_tick > ContactTtl)
                continue;
            contact.source = ObservationSource::Report;
            auto it = contacts.find(contact.regiment.value);
            if (it == contacts.end() || (it->second.source != ObservationSource::Direct &&
                                         contact.observed_tick > it->second.observed_tick))
                contacts[contact.regiment.value] = contact;
        }
    }
    situation.contacts.clear();
    for (auto &[id, contact] : contacts) {
        (void)id;
        situation.contacts.push_back(contact);
    }
    std::sort(situation.contacts.begin(), situation.contacts.end(),
              [&](const auto &a, const auto &b) {
                  float da = Distance(situation.own.pose.position, a.estimated_pose.position);
                  float db = Distance(situation.own.pose.position, b.estimated_pose.position);
                  return da != db ? da < db : a.regiment < b.regiment;
              });
}

bool InMeleeWith(const World &world, const RegimentState &regiment, RegimentId enemy_id) {
    const auto *enemy = FindRegiment(world, enemy_id);
    if (!enemy || enemy->team == regiment.team || !regiment.melee_engaged ||
        !enemy->melee_engaged)
        return false;
    const auto formed = [](const UnitState *unit) {
        return unit && unit->alive && !unit->skirmisher && !unit->skirmisher_returning &&
               unit->control != ControlMode::PlayerControlled;
    };
    const auto a = Footprint(regiment), b = Footprint(*enemy);
    if (Distance(regiment.pose.position, enemy->pose.position) >
        .5f * (std::hypot(a.width_m, a.depth_m) + std::hypot(b.width_m, b.depth_m)) + 5.f)
        return false;
    for (const auto id : regiment.units) {
        const auto *unit = FindUnit(world, id);
        if (!formed(unit))
            continue;
        for (const auto target_id : enemy->units) {
            const auto *target = FindUnit(world, target_id);
            if (!formed(target))
                continue;
            const float range = std::min(4.f, std::max(
                (unit->archer ? .95f : unit->weapon.range) + .55f * regiment.formation.spacing_m,
                (target->archer ? .95f : target->weapon.range) + .55f * enemy->formation.spacing_m));
            if (Distance(unit->pose.position, target->pose.position) <= range &&
                UnitTerrainLimit(world, unit->pose.position, target->pose.position, .002f) >= 1.f)
                return true;
        }
    }
    return false;
}

std::optional<Pose> FindFlankWaypoint(const World &world, const NavigationService &navigation,
                                     const RegimentState &mover, const RegimentState &enemy,
                                     const KnownContact &contact, const CommandTask &assignment,
                                     float preferred_side, bool either_side, float maximum_length,
                                     std::optional<Pose> failed_goal) {
    const auto target = contact.estimated_pose;
    const auto depth = Forward(target.yaw);
    const auto side = Right(target.yaw);
    const auto enemy_size = Footprint(enemy);
    const auto move_size = Footprint(mover);
    const Vec3 relative = mover.pose.position - target.position;
    const auto contact_edge = enemy_size.depth_m * .5f;
    const float preferred_end = Dot(relative, depth) < 0.f ? -1.f : 1.f;
    for (const float sign : {preferred_side, -preferred_side}) {
        if (!either_side && sign != preferred_side)
            break;
        const auto wing = side * sign;
        const float aim = std::round(std::atan2(-wing.y, -wing.x) / (Pi / 2.f)) * (Pi / 2.f);
        const auto projection = [&](Vec3 axis) {
            return .5f * (std::abs(Dot(Forward(aim), axis)) * move_size.depth_m +
                          std::abs(Dot(Right(aim), axis)) * move_size.width_m);
        };
        const float lateral = enemy_size.width_m * .5f + projection(wing) + .55f;
        const float reach = projection(depth);
        const std::array offsets{0.f, contact_edge * .5f, contact_edge,
                                 contact_edge + .15f * reach, contact_edge + .35f * reach,
                                 contact_edge + .55f * reach, contact_edge + .7f * reach};
        std::optional<Pose> best;
        float best_score = std::numeric_limits<float>::infinity();
        for (size_t i = 0; i < offsets.size() * 2 - 1; ++i) {
            // Try the center first, then both shoulders. A small shift along the
            // enemy's line can reach dry ground and still put several ranks in reach.
            const float offset = offsets[(i + 1) / 2] * preferred_end * (i % 2 ? 1.f : -1.f);
            const Pose goal{target.position + wing * lateral + depth * offset, aim};
            if ((assignment.authority.pursuit_limit_m &&
                 Distance(goal.position, assignment.intent.objective_position) >
                    *assignment.authority.pursuit_limit_m) ||
                Distance(goal.position, mover.pose.position) > maximum_length - 5.f ||
                (failed_goal && Distance(failed_goal->position, goal.position) < .25f))
                continue;
            // At the end of the maneuver attackers must reach a piece of the known line
            // across clear ground. The center itself can still be on the bridge.
            const auto along_line = depth * std::clamp(offset, -contact_edge, contact_edge);
            const auto enemy_shoulder = target.position + along_line + wing * (enemy_size.width_m * .5f);
            if (EvaluateTerrainShot(goal.position, enemy_shoulder, world.scenario.obstacles).blocked)
                continue;
            const auto path = FindFlankRoute(navigation, world, mover, goal, enemy.id);
            if (path.status != Status::Ok || path.waypoints.empty())
                continue;
            float distance = 0;
            auto position = mover.pose.position;
            for (const auto &waypoint : path.waypoints) {
                distance += Distance(position, waypoint.position);
                position = waypoint.position;
            }
            const float score = distance + 3.f * std::max(0.f, std::abs(offset) - contact_edge);
            if (distance <= maximum_length && score < best_score) {
                best = goal;
                best_score = score;
            }
            if (best && i == 0 && distance <= Distance(mover.pose.position, goal.position) + 4.f)
                break;
        }
        if (best)
            return best;
    }
    return {};
}

const CommandTask *SentFlank(const NodeRuntime &issuer, NodeId recipient, TaskId parent_task,
                             RegimentId target) {
    const auto found = std::find_if(issuer.sent_tasks.begin(), issuer.sent_tasks.end(),
        [&](const auto &task) {
            return task.recipient == recipient && task.parent_task == parent_task &&
                   task.intent.target_regiment == target && task.intent.flank_waypoint;
        });
    return found == issuer.sent_tasks.end() ? nullptr : &*found;
}

bool AssignedOrInTransit(const CommandState &state, const NodeRuntime &recipient, TaskId id) {
    const auto delivered = std::find_if(recipient.orders.begin(), recipient.orders.end(),
        [&](const auto &task) { return task.id == id; });
    return delivered == recipient.orders.end() ? TaskInTransit(state, id) :
        delivered->status == TaskStatus::Accepted || delivered->status == TaskStatus::Active;
}

std::optional<Pose> FlankRoute(const World &world, const NavigationService &navigation,
                              const CommandState &state, const NodeRuntime &group, NodeId mover_id,
                              RegimentId lead_id, const KnownContact &contact,
                              const CommandTask &assignment) {
    const auto *mover_node = FindNode(state.hierarchy, mover_id);
    const auto *mover_runtime = FindRuntime(state, mover_id);
    const auto *mover = mover_node ? FindRegiment(world, mover_node->regiment) : nullptr;
    const auto *enemy = FindRegiment(world, contact.regiment);
    if (!mover || !mover_node->commander || !mover_runtime || mover->panic ||
        !enemy || enemy->units.empty() || ExternalOrder(world, *mover, mover_runtime->last_order_id))
        return {};
    const auto *previous = SentFlank(group, mover_id, assignment.id, contact.regiment);
    if (previous && AssignedOrInTransit(state, *mover_runtime, previous->id))
        return previous->intent.flank_waypoint;
    if (mover->melee_engaged)
        return {};
    if (lead_id) {
        const auto *lead = FindRegiment(world, lead_id);
        if (!lead || lead->panic || lead->ai_target != enemy->id || !InMeleeWith(world, *lead, enemy->id))
            return {};
    }
    const float side = Dot(mover->pose.position - contact.estimated_pose.position,
                           Right(contact.estimated_pose.yaw)) < 0.f ? -1.f : 1.f;
    return FindFlankWaypoint(world, navigation, *mover, *enemy, contact, assignment, side, true, 35.f,
                             previous ? previous->intent.flank_waypoint : std::nullopt);
}

const CommandNode *GroupLead(const World &world, const CommandHierarchy &hierarchy, NodeId id) {
    const auto *group = FindNode(hierarchy, id);
    const CommandNode *lead = nullptr;
    if (group)
        for (auto child : group->children) {
            const auto *node = FindNode(hierarchy, child);
            if (!node || !node->commander || node->sector_role == SectorRole::Reserve ||
                (lead && lead->id < child))
                continue;
            const auto *regiment = FindRegiment(world, node->regiment);
            if (regiment && !regiment->panic && !regiment->units.empty())
                lead = node;
        }
    return lead;
}

std::optional<Pose> GroupFlankRoute(const World &world, const NavigationService &navigation,
                                   const CommandState &state, const NodeRuntime &army, NodeId wing_id,
                                   const KnownContact &contact, const CommandTask &assignment) {
    const auto *wing_node = FindNode(state.hierarchy, wing_id);
    const auto *wing = FindRuntime(state, wing_id);
    const auto *lead = GroupLead(world, state.hierarchy, wing_id);
    const auto *mover = lead ? FindRegiment(world, lead->regiment) : nullptr;
    const auto *mover_runtime = lead ? FindRuntime(state, lead->id) : nullptr;
    const auto *enemy = FindRegiment(world, contact.regiment);
    if (!wing_node || !wing || !mover || !mover_runtime || !enemy || enemy->units.empty())
        return {};

    const auto *previous = SentFlank(army, wing_id, assignment.parent_task, contact.regiment);
    if (previous && AssignedOrInTransit(state, *wing, previous->id)) {
        const auto *leaf_task = SentFlank(*wing, lead->id, previous->id, contact.regiment);
        const bool issued_to_old_lead = !leaf_task && std::any_of(wing->sent_tasks.begin(),
            wing->sent_tasks.end(), [&](const auto &task) {
                return task.parent_task == previous->id && task.intent.flank_waypoint;
            });
        if (!issued_to_old_lead && (!leaf_task || AssignedOrInTransit(state, *mover_runtime, leaf_task->id)))
            return previous->intent.flank_waypoint;
    }
    if (mover->melee_engaged || contact.source == ObservationSource::Memory ||
        ExternalOrder(world, *mover, mover_runtime->last_order_id))
        return {};
    const auto army_pose = NodePose(world, state.hierarchy, army.node);
    const float army_yaw = army_pose ? army_pose->yaw : mover->pose.yaw;
    const float wing_sign = wing_node->sector_role == SectorRole::LeftFlank ? -1.f : 1.f;
    const float alignment = Dot(Right(army_yaw), Right(contact.estimated_pose.yaw));
    const float side = alignment < 0.f ? -wing_sign : wing_sign;
    return FindFlankWaypoint(world, navigation, *mover, *enemy, contact, assignment, side, false,
                             65.f, previous ? previous->intent.flank_waypoint : std::nullopt);
}

std::optional<Vec3> GroupSupportPosition(const World &world, const NavigationService &navigation,
                                        const CommandHierarchy &hierarchy, NodeId wing_id,
                                        NodeId center_id, const KnownContact &contact) {
    const auto *wing_lead = GroupLead(world, hierarchy, wing_id);
    const auto *center_lead = GroupLead(world, hierarchy, center_id);
    const auto *wing = wing_lead ? FindRegiment(world, wing_lead->regiment) : nullptr;
    const auto *center = center_lead ? FindRegiment(world, center_lead->regiment) : nullptr;
    if (!wing || !center)
        return {};
    const auto delta = contact.estimated_pose.position - center->pose.position;
    const float yaw = Length(delta) > .01f ? std::atan2(delta.y, delta.x) : center->pose.yaw;
    const auto forward = Forward(yaw);
    const auto side = Right(yaw);
    const auto wing_size = Footprint(*wing);
    const auto center_size = Footprint(*center);
    const auto rear = center->pose.position - forward * (.5f * (wing_size.depth_m + center_size.depth_m) + 5.f);
    const float lateral = Dot(wing->pose.position - rear, side);
    const float clearance = .5f * (wing_size.width_m + center_size.width_m) + 2.f;
    for (const float offset : {std::copysign(clearance, lateral), lateral}) {
        const auto goal = rear + side * offset;
        if (FindSupportRoute(navigation, world, *wing, goal).status == Status::Ok)
            return goal;
    }
    return {};
}

ChildStatus StatusOf(const World &world, const CommandHierarchy &hierarchy,
                     const CommandState &state, const CommandNode &node) {
    ChildStatus result;
    result.node = node.id;
    if (node.level == Level::Regiment) {
        if (const auto *regiment = FindRegiment(world, node.regiment)) {
            result.pose = regiment->pose;
            result.melee_engaged = regiment->melee_engaged;
            result.attack_target = regiment->ai_target;
        }
    } else if (auto pose = NodePose(world, hierarchy, node.id))
        result.pose = *pose;
    uint64_t living = 0, initial = 0, panicked = 0;
    float width = 0;
    Vec3 formation_center{};
    for (auto regiment_id : OwnedRegiments(hierarchy, node.id))
        if (const auto *regiment = FindRegiment(world, regiment_id)) {
            uint32_t alive = 0;
            for (auto unit_id : regiment->units)
                if (const auto *unit = FindUnit(world, unit_id); unit && unit->alive)
                    ++alive;
            living += alive;
            formation_center = formation_center + regiment->pose.position * float(alive);
            initial += regiment->initial_count;
            if (regiment->panic)
                panicked += alive;
            width += Footprint(*regiment).width_m;
        }
    result.living = uint32_t(std::min<uint64_t>(living, UINT32_MAX));
    if (living)
        result.formation_center = formation_center * (1.f / float(living));
    result.initial = uint32_t(std::min<uint64_t>(initial, UINT32_MAX));
    result.width_m = std::max(2.f, width);
    result.panic = living && panicked * 2 > living;
    if (const auto *runtime = FindRuntime(state, node.id)) {
        result.state = runtime->memory.state;
        result.task_status = runtime->memory.task_status;
    }
    return result;
}

void AddNeedSupportFallback(CommandState &state, NodeRuntime &runtime, const CommandNode &node,
                            Tick tick, const LocalSituation &situation, std::string explanation) {
    if (!node.parent)
        return;
    const bool recent =
        std::any_of(runtime.reports.begin(), runtime.reports.end(), [&](const auto &old) {
            return old.kind == ReportKind::NeedSupport &&
                   old.about_task == runtime.memory.active_task && tick >= old.created_tick &&
                   tick - old.created_tick < 20;
        });
    if (recent)
        return;
    SituationReport report;
    report.kind = ReportKind::NeedSupport;
    report.sender = node.id;
    report.recipient = node.parent;
    report.about_task = runtime.memory.active_task;
    report.observed_tick = tick;
    report.created_tick = tick;
    report.situation = situation;
    report.explanation = std::move(explanation);
    AddReport(runtime.reports, report);
    if (state.next_message_id && state.next_message_id.value != UINT64_MAX)
        QueueMessage(state, node.id, node.parent, tick, report);
}

void AssignMeetingInitiative(const World &world, const CommandHierarchy &hierarchy,
                             std::vector<CommandTask> &missions) {
    if (!world.scenario.rules.meeting_engagement || world.scenario.objective)
        return;
    std::vector<const CommandNode *> armies;
    for (const auto &node : hierarchy.nodes)
        if (node.level == Level::Army && IsCommandRoot(node) && node.commander)
            armies.push_back(&node);
    if (armies.size() != 2 || armies[0]->team == armies[1]->team)
        return;

    std::array<std::optional<size_t>, 2> assigned;
    for (size_t side = 0; side < armies.size(); ++side) {
        for (size_t i = 0; i < missions.size(); ++i) {
            const auto &mission = missions[i];
            if (mission.recipient != armies[side]->id)
                continue;
            if (assigned[side] || !detail::IsAutomaticAreaAttack(mission) ||
                mission.not_before_tick || mission.expiry_tick || mission.duration_ticks ||
                !ValidTaskRequest(mission))
                return;
            assigned[side] = i;
        }
    }
    // Honor a single opening assault. For equal missions, team and command IDs break the tie.
    const auto rank = [&](size_t side) {
        return std::tuple(!assigned[side], armies[side]->team, armies[side]->id);
    };
    const size_t attacker = rank(0) < rank(1) ? 0u : 1u;
    const size_t defender = 1 - attacker;
    const auto deployment_center = [&](NodeId army) -> std::optional<Vec3> {
        Vec3 center{};
        float weight = 0.f;
        for (const auto regiment_id : OwnedRegiments(hierarchy, army))
            if (const auto *regiment = FindRegiment(world, regiment_id)) {
                const float living = float(regiment->units.size());
                center = center + regiment->pose.position * living;
                weight += living;
            }
        return weight > 0.f ? std::optional(center * (1.f / weight)) : std::nullopt;
    };
    const auto assault_post = deployment_center(armies[attacker]->id);
    const auto defense_post = deployment_center(armies[defender]->id);
    if (!assault_post || !defense_post)
        return;

    if (!assigned[attacker]) {
        CommandTask attack;
        attack.recipient = armies[attacker]->id;
        attack.action = TaskKind::Attack;
        attack.intent.objective_position = *assault_post + (*defense_post - *assault_post) * .75f;
        attack.authority.initiative = InitiativePolicy::WithinIntent;
        attack.authority.may_change_target = true;
        attack.authority.may_pursue = true;
        missions.push_back(std::move(attack));
    }
    CommandTask hold = assigned[defender] ? missions[*assigned[defender]] : CommandTask{};
    hold.recipient = armies[defender]->id;
    hold.action = TaskKind::HoldArea;
    hold.authority = {};
    hold.intent = {};
    hold.intent.objective_position = *defense_post;
    if (assigned[defender])
        missions[*assigned[defender]] = std::move(hold);
    else
        missions.push_back(std::move(hold));
}

const CommandNode *ScopedAttackCommander(const World &world, UnitId officer) {
    const auto *unit = FindUnit(world, officer);
    if (!world.command_state || !unit || !unit->alive || world.controlled != officer ||
        unit->control != ControlMode::PlayerControlled || world.outcome != BattleOutcome::Running)
        return nullptr;
    const auto record = std::ranges::find(world.officers, officer, &OfficerSpec::unit);
    if (record == world.officers.end() || (record->role != 1 && record->role != 2))
        return nullptr;
    const CommandNode *scope = nullptr;
    for (const auto &node : world.command_state->hierarchy.nodes)
        if (node.commander == record->id && node.level != Level::Regiment && node.team == UnitTeam(world, *unit) &&
            (!scope || std::tie(node.level, node.id) < std::tie(scope->level, scope->id)))
            scope = &node;
    return scope;
}

} // namespace

bool CanOrderScopedAttack(const World &world, UnitId officer) {
    return ScopedAttackCommander(world, officer) != nullptr;
}

std::vector<Command> PlanScopedAttacks(const World &world, const NavigationService &navigation,
                                       UnitId officer, std::span<const RegimentId> excluded) {
    const auto *scope = ScopedAttackCommander(world, officer);
    if (!scope)
        return {};
    const auto &state = *world.command_state;
    auto perceived = BuildSituation(world, state.hierarchy, scope->id, world.tick);
    if (perceived.status != StubStatus::Ready || !perceived.value)
        return {};
    auto &situation = *perceived.value;
    if (const auto *runtime = FindRuntime(state, scope->id))
        MergeReportedContacts(situation, runtime->reports, world.tick);
    std::vector<const KnownContact *> known;
    for (const auto &contact : situation.contacts) {
        const auto *enemy = FindRegiment(world, contact.regiment);
        if (contact.allegiance == Allegiance::Enemy && contact.confidence > 0.f &&
            contact.observed_tick <= world.tick && world.tick - contact.observed_tick <= ContactTtl &&
            enemy && enemy->team != scope->team && !enemy->panic && !enemy->units.empty())
            known.push_back(&contact);
    }
    if (known.empty())
        return {};

    const auto eligible = [&](RegimentId id) {
        const auto *regiment = FindRegiment(world, id);
        return regiment && !regiment->units.empty() && !regiment->panic &&
               std::ranges::find(excluded, id) == excluded.end() && CanCommandRegiment(world, officer, id);
    };
    const auto nearest = [&](const RegimentState &regiment) -> const KnownContact * {
        std::ranges::sort(known, {}, [&](const auto *contact) {
            return std::pair(Distance(regiment.pose.position, contact->estimated_pose.position),
                             contact->regiment);
        });
        for (const auto *contact : known)
            if (InMeleeWith(world, regiment, contact->regiment) ||
                FindAttackRoute(navigation, world, regiment, contact->estimated_pose, contact->regiment).status == Status::Ok)
                return contact;
        return nullptr;
    };
    std::vector<Command> packets;
    std::set<RegimentId> addressed_by_courier;
    for (const auto child : scope->children) {
        if (!UsesStaffCourier(world, scope->id, child))
            continue;
        const auto members = OwnedRegiments(state.hierarchy, child);
        addressed_by_courier.insert(members.begin(), members.end());
        // Guards in a restricted scenario branch must continue to wait for their scripted release.
        if (std::ranges::any_of(members, [&](RegimentId id) {
                return std::ranges::find(excluded, id) != excluded.end();
            }))
            continue;
        const auto *lead = GroupLead(world, state.hierarchy, child);
        if (!lead || !eligible(lead->regiment))
            continue;
        if (const auto *contact = nearest(*FindRegiment(world, lead->regiment))) {
            Command packet;
            packet.kind = CommandKind::IssueCommandTask;
            packet.unit = officer;
            auto &task = packet.command_task;
            task.issuer = scope->id;
            task.recipient = child;
            task.author = scope->commander;
            task.action = TaskKind::Attack;
            task.priority = PlayerOrderPriority;
            task.intent.target_regiment = contact->regiment;
            task.intent.objective_position = contact->estimated_pose.position;
            task.authority.initiative = InitiativePolicy::WithinIntent;
            task.authority.may_pursue = true;
            task.supersede_player_orders_through = 0;
            packets.push_back(std::move(packet));
        }
    }
    for (const auto id : OwnedRegiments(state.hierarchy, scope->id)) {
        if (addressed_by_courier.contains(id) || !eligible(id))
            continue;
        const auto &regiment = *FindRegiment(world, id);
        const auto *contact = nearest(regiment);
        if (!contact)
            continue;
        if (regiment.following_officer == officer) {
            Command release;
            release.kind = CommandKind::SetRegimentFollowing;
            release.unit = officer;
            release.regiment = id;
            packets.push_back(release);
        }
        Command packet;
        packet.kind = CommandKind::IssueOrder;
        packet.unit = officer;
        packet.regiment = id;
        auto &order = packet.order;
        order.type = OrderType::Attack;
        order.issuer = scope->commander;
        order.recipient = id;
        order.priority = PlayerOrderPriority;
        order.target_regiment = contact->regiment;
        order.point = contact->estimated_pose.position;
        order.created_tick = world.tick;
        order.delivery_tick = detail::Later(world.tick, 20);
        packets.push_back(packet);
    }
    return packets;
}

bool ValidTaskRequest(const CommandTask &task) {
    auto candidate = task;
    if (!candidate.id)
        candidate.id = {1};
    return detail::ValidTask(candidate);
}

StubResult<bool> InitializeCommand(World &world) {
    if (!world.scenario.command_setup) {
        world.command_state.reset();
        return {StubStatus::Ready, true, "Scenario does not enable command runtime."};
    }
    if (world.command_state)
        return {StubStatus::Ready, true, "Command runtime is already initialized."};

    auto validation = ValidateHierarchy(world.scenario.command_setup->hierarchy, world);
    if (validation.status != StubStatus::Ready)
        return {StubStatus::InvalidInput, std::nullopt, "Invalid command hierarchy."};
    CommandState state;
    state.hierarchy = world.scenario.command_setup->hierarchy;
    std::sort(state.hierarchy.nodes.begin(), state.hierarchy.nodes.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(state.hierarchy.officers.begin(), state.hierarchy.officers.end(),
              [](const auto &a, const auto &b) { return a.officer < b.officer; });
    for (const auto &node : state.hierarchy.nodes) {
        NodeRuntime runtime;
        runtime.node = node.id;
        runtime.schedule.node = node.id;
        auto *profile = FindProfile(state.hierarchy, node.commander);
        runtime.schedule.interval_ticks = profile && profile->reevaluation_ticks
                                              ? profile->reevaluation_ticks
                                              : DefaultInterval(node.level);
        if (world.scenario.rules.adaptive_battle) {
            runtime.schedule.interval_ticks = std::max(runtime.schedule.interval_ticks,
                                                       DefaultInterval(node.level, true));
            runtime.schedule.next_think_tick = NextBrainPhase(node.id, runtime.schedule.interval_ticks,
                world.tick);
            if (world.tick % runtime.schedule.interval_ticks == BrainPhase(node.id, runtime.schedule.interval_ticks))
                runtime.schedule.next_think_tick = world.tick;
        }
        state.nodes.push_back(runtime);
    }
    std::set<uint64_t> task_ids;
    auto missions = world.scenario.command_setup->missions;
    AssignMeetingInitiative(world, state.hierarchy, missions);
    if (const auto &objective = world.scenario.objective) {
        for (const auto &node : state.hierarchy.nodes) {
            if (!IsCommandRoot(node) || std::ranges::any_of(missions, [&](const auto &mission) {
                    return mission.recipient == node.id;
                }))
                continue;
            CommandTask mission;
            mission.recipient = node.id;
            mission.action = objective->defending_team == node.team ? TaskKind::Defend :
                                                                    TaskKind::MoveToArea;
            mission.intent.objective_position = objective->center;
            mission.intent.objective_radius_m = objective->radius_m;
            missions.push_back(mission);
        }
    }
    for (auto mission : missions) {
        const auto *recipient = FindNode(state.hierarchy, mission.recipient);
        if (!ValidTaskRequest(mission) || mission.issuer || mission.created_tick > world.tick ||
            !recipient || !IsCommandRoot(*recipient) ||
            unsigned(mission.status) > unsigned(TaskStatus::Cancelled))
            return {StubStatus::InvalidInput, std::nullopt, "Invalid initial command mission."};
        if (!mission.id) {
            mission.id = state.next_task_id;
            ++state.next_task_id.value;
        }
        if (mission.id.value == UINT64_MAX)
            return {StubStatus::InvalidInput, std::nullopt, "Initial mission ID is exhausted."};
        if (!task_ids.insert(mission.id.value).second)
            return {StubStatus::InvalidInput, std::nullopt, "Duplicate initial mission ID."};
        if (!detail::ValidTaskTarget(world, *recipient, mission))
            return {StubStatus::InvalidInput, std::nullopt, "Invalid initial mission target."};
        if (auto expiry = detail::TaskExpiry(mission, world.tick); expiry && *expiry <= world.tick)
            return {StubStatus::InvalidInput, std::nullopt, "Initial mission is already expired."};
        state.next_task_id.value = std::max(state.next_task_id.value, mission.id.value + 1);
        mission.status = TaskStatus::Active;
        mission.received_tick = world.tick;
        if (auto *runtime = FindRuntime(state, mission.recipient)) {
            runtime->orders.push_back(std::move(mission));
            if (world.scenario.rules.adaptive_battle)
                runtime->schedule.next_think_tick = world.tick;
        }
    }
    world.command_state = std::move(state);
    return {StubStatus::Ready, true, "Command hierarchy initialized."};
}

StubResult<TaskId> SubmitTask(World &world, CommandTask task) {
    if (!world.command_state || !ValidTaskRequest(task))
        return {StubStatus::InvalidInput, std::nullopt,
                "Command task needs an initialized hierarchy and valid fields."};
    auto &state = *world.command_state;
    Prune(state, world.tick);
    const auto *recipient = FindNode(state.hierarchy, task.recipient);
    if (!recipient ||
        (task.issuer ? task.issuer != recipient->parent : !IsCommandRoot(*recipient)))
        return {StubStatus::InvalidInput, std::nullopt, "Task issuer has no authority."};
    if (task.author) {
        const auto *issuer = task.issuer ? FindNode(state.hierarchy, task.issuer) : recipient;
        if (!issuer || (issuer->commander && issuer->commander != task.author))
            return {StubStatus::InvalidInput, std::nullopt, "Task author is not the issuer."};
    }
    if (!detail::ValidTaskTarget(world, *recipient, task))
        return {StubStatus::InvalidInput, std::nullopt, "Invalid task target."};
    const bool staff_dispatch = UsesStaffCourier(world, task.issuer, task.recipient);
    if (task.supersede_player_orders_through) {
        const auto *scope = ScopedAttackCommander(world, world.controlled);
        if (!staff_dispatch || !scope || scope->id != task.issuer || scope->commander != task.author ||
            task.action != TaskKind::Attack || task.priority != PlayerOrderPriority || !task.intent.target_regiment)
            return {StubStatus::InvalidInput, std::nullopt, "Invalid scoped player attack."};
    }
    if (staff_dispatch && (!state.next_message_id || state.next_message_id.value == UINT64_MAX))
        return {StubStatus::InvalidInput, std::nullopt, "Message ID counter is exhausted."};
    if (!task.created_tick)
        task.created_tick = world.tick;
    if (task.created_tick > world.tick ||
        (task.not_before_tick && *task.not_before_tick > UINT64_MAX - 1))
        return {StubStatus::InvalidInput, std::nullopt, "Task begins in an invalid future."};
    if (task.id) {
        for (const auto &runtime : state.nodes)
            if (std::any_of(runtime.orders.begin(), runtime.orders.end(),
                            [&](const auto &old) { return old.id == task.id; }))
                return {StubStatus::InvalidInput, std::nullopt, "Duplicate task ID."};
        for (const auto &runtime : state.nodes)
            if (std::any_of(runtime.sent_tasks.begin(), runtime.sent_tasks.end(),
                            [&](const auto &old) { return old.id == task.id; }))
                return {StubStatus::InvalidInput, std::nullopt, "Duplicate task ID."};
        for (const auto &message : state.communications.messages)
            if (const auto *queued = std::get_if<CommandTask>(&message.payload);
                queued && queued->id == task.id)
                return {StubStatus::InvalidInput, std::nullopt, "Duplicate task ID."};
        if (task.id.value == UINT64_MAX)
            return {StubStatus::InvalidInput, std::nullopt, "Task ID is exhausted."};
        state.next_task_id.value = std::max(state.next_task_id.value, task.id.value + 1);
    } else {
        if (!state.next_task_id || state.next_task_id.value == UINT64_MAX)
            return {StubStatus::InvalidInput, std::nullopt, "Task ID counter is exhausted."};
        task.id = state.next_task_id;
        ++state.next_task_id.value;
    }
    task.status = TaskStatus::Active;
    if (staff_dispatch) {
        const auto *sender = FindNode(state.hierarchy, task.issuer);
        task.author = sender->commander;
        task.received_tick.reset();
        if (task.supersede_player_orders_through) {
            if (!ReserveEarlierCourierHolds(world, state, task))
                return {StubStatus::InvalidInput, std::nullopt, "Order ID counter is exhausted."};
            // Commands submitted after this dispatch receive higher OrderIds, even on the same tick.
            task.supersede_player_orders_through = world.next_order_id - 1;
        }
        QueueMessage(state, task.issuer, task.recipient, world.tick, task, DeliveryChannel::Courier);
        return {StubStatus::Ready, task.id, "Command task assigned for courier delivery."};
    }
    task.received_tick = world.tick;
    auto *runtime = FindRuntime(state, recipient->id);
    if (!runtime)
        return {StubStatus::InvalidInput, std::nullopt, "Recipient runtime is missing."};
    if (runtime->orders.size() >= HistoryLimit)
        return {StubStatus::InvalidInput, std::nullopt, "Recipient task history is full."};
    runtime->orders.push_back(task);
    runtime->schedule.next_think_tick = world.tick;
    return {StubStatus::Ready, task.id, "Command task accepted."};
}

StubResult<bool> UpdateCommand(World &world, const NavigationService &navigation,
                               const ITacticalEvaluator *group_policy, bool group_order_commitment) {
    if (!world.command_state)
        return {StubStatus::Ready, false, "Command runtime is disabled."};
    auto &state = *world.command_state;
    const bool adaptive = world.scenario.rules.adaptive_battle;
    Prune(state, world.tick);

    for (auto &node : state.hierarchy.nodes) {
        auto successor = ResolveSuccessor(world, state.hierarchy, node);
        if (successor != node.commander) {
            const auto previous = node.commander;
            node.commander = successor;
            if (world.scenario.rules.regiment_tactics && previous) {
                const auto old = std::ranges::find(world.officers, previous, &OfficerSpec::id);
                const auto replacement =
                    std::ranges::find(world.officers, successor, &OfficerSpec::id);
                world.events.push_back({{world.next_event_id++}, world.tick,
                    successor ? EventKind::OfficerSucceeded : EventKind::CommandVacant,
                    replacement == world.officers.end() ? UnitId{} : replacement->unit,
                    old == world.officers.end() ? UnitId{} : old->unit, node.regiment, 0.f,
                    "command succession"});
            }
            if (auto *runtime = FindRuntime(state, node.id))
                runtime->schedule.next_think_tick = world.tick;
            if (previous && node.parent) {
                SituationReport report;
                report.kind = ReportKind::CommanderLost;
                report.sender = node.id;
                report.recipient = node.parent;
                report.observed_tick = world.tick;
                report.created_tick = world.tick;
                report.explanation = "Commander lost; command succession resolved.";
                auto situation = BuildSituation(world, state.hierarchy, node.id, world.tick);
                if (situation.status == StubStatus::Ready && situation.value)
                    report.situation = std::move(*situation.value);
                if (state.next_message_id && state.next_message_id.value != UINT64_MAX)
                    QueueMessage(state, node.id, node.parent, world.tick, std::move(report));
            }
        }
    }
    auto hierarchy = ValidateHierarchy(state.hierarchy, world);
    if (hierarchy.status != StubStatus::Ready)
        return {StubStatus::InvalidInput, std::nullopt,
                "Runtime command hierarchy became invalid."};

    for (auto &runtime : state.nodes) {
        const auto *node = FindNode(state.hierarchy, runtime.node);
        if (!node || node->level != Level::Regiment || !runtime.last_order_id)
            continue;
        auto *regiment = FindRegiment(world, node->regiment);
        if (!regiment || !regiment->order || regiment->order->id != runtime.last_order_id ||
            (regiment->order->status != OrderStatus::Failed &&
             regiment->order->status != OrderStatus::Cancelled))
            continue;
        const auto active = std::find_if(runtime.orders.begin(), runtime.orders.end(),
            [&](const auto &task) { return task.id == runtime.memory.active_task; });
        const bool recover_attack = world.scenario.rules.local_melee && !regiment->panic &&
            regiment->order->status == OrderStatus::Failed && active != runtime.orders.end() &&
            active->authority.may_pursue && (active->action == TaskKind::Attack ||
                active->action == TaskKind::Exploit || (active->action == TaskKind::Flank &&
                    !active->intent.flank_waypoint));
        if (!recover_attack) {
            if (active != runtime.orders.end())
                active->status = TaskStatus::Failed;
            runtime.memory.task_status = TaskStatus::Failed;
            runtime.memory.state = FsmState::Failed;
        }
        runtime.last_order_id = {};
        runtime.schedule.next_think_tick = world.tick;
        auto situation = BuildSituation(world, state.hierarchy, node->id, world.tick);
        if (situation.status == StubStatus::Ready && situation.value)
            AddNeedSupportFallback(state, runtime, *node, world.tick, *situation.value,
                                   "Assigned route or target became unavailable.");
    }

    AdvanceArmyStaff(world, navigation);
    for (auto &message : state.communications.messages) {
        if (message.channel != DeliveryChannel::Courier ||
            message.status != DeliveryStatus::InTransit ||
            UsesStaffCourier(world, message.sender, message.recipient))
            continue;
        if (message.expiry_tick && *message.expiry_tick <= world.tick) {
            message.status = DeliveryStatus::Expired;
            continue;
        }
        auto *courier = FindUnit(world, message.courier);
        if (!courier || !courier->alive) {
            message.status = DeliveryStatus::Lost;
            continue;
        }
        const auto *recipient = FindNode(state.hierarchy, message.recipient);
        auto destination =
            recipient ? NodePose(world, state.hierarchy, recipient->id) : std::nullopt;
        if (!destination) {
            message.status = DeliveryStatus::RecipientUnavailable;
            continue;
        }
        auto path = navigation.FindPath({courier->pose, destination->position, {1, 1}, {},
                                         {}, world.scenario.rules.adaptive_battle});
        if (path.status != Status::Ok || path.waypoints.empty() ||
            std::any_of(path.waypoints.begin(), path.waypoints.end(),
                        [](Pose waypoint) { return !Finite(waypoint); })) {
            message.status = DeliveryStatus::Lost;
            continue;
        }
        float distance = 0;
        Vec3 position = courier->pose.position;
        for (const auto &waypoint : path.waypoints) {
            distance += Distance(position, waypoint.position);
            position = waypoint.position;
        }
        const float speed = std::max(courier->speed, .01f);
        const double ticks = std::ceil(double(distance) / (double(speed) * .05));
        if (!std::isfinite(ticks)) {
            message.status = DeliveryStatus::Lost;
            continue;
        }
        Tick duration = ticks >= double(UINT64_MAX) ? UINT64_MAX
                                                    : std::max<Tick>(1, Tick(ticks));
        Tick arrival = duration > UINT64_MAX - message.created_tick
                           ? UINT64_MAX
                           : message.created_tick + duration;
        message.delivery_tick = std::max(message.delivery_tick, arrival);
    }

    auto advanced = AdvanceCommunications(state.communications, world.tick);
    if (advanced.status != StubStatus::Ready || !advanced.value)
        return {StubStatus::InvalidInput, std::nullopt, "Invalid communications state."};
    state.communications = std::move(advanced.value->next);
    for (const auto &message : advanced.value->delivered) {
        auto *runtime = FindRuntime(state, message.recipient);
        if (!runtime) {
            auto found = std::find_if(state.communications.messages.begin(),
                                      state.communications.messages.end(),
                                      [&](const auto &m) { return m.id == message.id; });
            if (found != state.communications.messages.end())
                found->status = DeliveryStatus::RecipientUnavailable;
            continue;
        }
        if (const auto *task = std::get_if<CommandTask>(&message.payload)) {
            const auto *recipient = FindNode(state.hierarchy, message.recipient);
            const auto *sender = FindNode(state.hierarchy, message.sender);
            if (!recipient || !sender || recipient->parent != sender->id ||
                (sender->commander && task->author != sender->commander) ||
                !detail::ValidTaskTarget(world, *recipient, *task)) {
                auto found = std::find_if(state.communications.messages.begin(),
                                          state.communications.messages.end(),
                                          [&](const auto &m) { return m.id == message.id; });
                if (found != state.communications.messages.end())
                    found->status = DeliveryStatus::RecipientUnavailable;
                continue;
            }
            if (runtime->orders.size() >= HistoryLimit) {
                auto found = std::find_if(state.communications.messages.begin(),
                                          state.communications.messages.end(), [&](const auto &m) {
                                              return m.id == message.id;
                                          });
                if (found != state.communications.messages.end())
                    found->status = DeliveryStatus::RecipientUnavailable;
                continue;
            }
            auto delivered = *task;
            delivered.received_tick = world.tick;
            delivered.status = TaskStatus::Active;
            if (std::none_of(runtime->orders.begin(), runtime->orders.end(),
                             [&](const auto &old) { return old.id == delivered.id; })) {
                if (message.channel == DeliveryChannel::Courier && task->priority == PlayerOrderPriority &&
                    UsesStaffCourier(world, sender->id, recipient->id)) {
                    if (task->action == TaskKind::HoldArea) {
                        if (SupersededCourierHold(*runtime, *task, world.tick))
                            delivered.status = TaskStatus::Cancelled;
                        else
                            HoldGroupAtItsPosts(world, state.hierarchy, *task);
                    }
                    else if (task->action == TaskKind::Attack && task->author)
                        PrepareGroupForPlayerAttack(world, state.hierarchy, *task);
                }
                runtime->orders.push_back(std::move(delivered));
            }
            runtime->schedule.next_think_tick = world.tick;
        } else if (const auto *report = std::get_if<SituationReport>(&message.payload)) {
            const auto *sender = FindNode(state.hierarchy, message.sender);
            if (!sender || sender->parent != message.recipient) {
                auto found = std::find_if(state.communications.messages.begin(),
                                          state.communications.messages.end(),
                                          [&](const auto &m) { return m.id == message.id; });
                if (found != state.communications.messages.end())
                    found->status = DeliveryStatus::RecipientUnavailable;
                continue;
            }
            AddReport(runtime->reports, *report);
            runtime->schedule.next_think_tick = world.tick;
        }
    }

    std::vector<BrainSchedule> schedules;
    schedules.reserve(state.nodes.size());
    for (auto &runtime : state.nodes) {
        const auto *node = FindNode(state.hierarchy, runtime.node);
        if (!node)
            return {StubStatus::InvalidInput, std::nullopt, "Runtime node disappeared."};
        if (adaptive) {
            const auto *target = FindRegiment(world, runtime.memory.target);
            const bool target_destroyed = target && target->units.empty() &&
                target->last_damage_tick == world.tick && world.tick != 0;
            const auto *own = node->level == Level::Regiment ? FindRegiment(world, node->regiment) : nullptr;
            const bool panic_arrival = own && own->panic && runtime.memory.state == FsmState::Withdrawing &&
                Distance(own->pose.position, own->rally_point) <= 2.f;
            const auto active = std::find_if(runtime.orders.begin(), runtime.orders.end(), [&](const auto &task) {
                return task.id == runtime.memory.active_task && task.status == TaskStatus::Active;
            });
            const bool task_begins = std::any_of(runtime.orders.begin(), runtime.orders.end(), [&](const auto &task) {
                return task.status == TaskStatus::Active && task.not_before_tick == world.tick;
            });
            const auto expiry = active == runtime.orders.end() ? std::nullopt :
                detail::TaskExpiry(*active, runtime.memory.task_started_tick);
            if (target_destroyed || panic_arrival || task_begins || (expiry && *expiry <= world.tick))
                runtime.schedule.next_think_tick = world.tick;
            schedules.push_back(runtime.schedule);
            continue;
        }
        const bool new_order =
            std::any_of(runtime.orders.begin(), runtime.orders.end(), [&](const auto &task) {
                return task.status == TaskStatus::Active && task.id != runtime.memory.active_task;
            });
        const bool new_report =
            std::any_of(runtime.reports.begin(), runtime.reports.end(),
                        [&](const auto &report) { return report.created_tick == world.tick; });
        const bool target_lost = runtime.memory.target && [&] {
            const auto *target = FindRegiment(world, runtime.memory.target);
            return !target ||
                   std::none_of(target->units.begin(), target->units.end(), [&](UnitId unit_id) {
                       const auto *unit = FindUnit(world, unit_id);
                       return unit && unit->alive;
                   });
        }();
        if (new_order || new_report || target_lost || HasPanic(world, state.hierarchy, node->id))
            runtime.schedule.next_think_tick = world.tick;
        schedules.push_back(runtime.schedule);
    }
    auto due = ScheduleBrains(schedules, world.tick);
    if (due.status != StubStatus::Ready || !due.value)
        return {StubStatus::InvalidInput, std::nullopt, "Invalid brain schedule."};

    for (NodeId id : *due.value) {
        auto *runtime = FindRuntime(state, id);
        const auto *node_ptr = FindNode(state.hierarchy, id);
        if (!runtime || !node_ptr)
            return {StubStatus::InvalidInput, std::nullopt, "Scheduled node is missing."};
        CancelObsoleteAssignments(*runtime, world.tick);
        const auto node = *node_ptr;
        auto perception = BuildSituation(world, state.hierarchy, node.id, world.tick);
        if (perception.status != StubStatus::Ready || !perception.value)
            return {StubStatus::InvalidInput, std::nullopt, "Could not build local situation."};
        auto situation = std::move(*perception.value);
        MergeReportedContacts(situation, runtime->reports, world.tick);
        // A report can outlive the regiment it describes. Only a surviving target
        // can be pursued to its last known position.
        const auto alive = [&](RegimentId id) {
            const auto *regiment = FindRegiment(world, id);
            return regiment && !regiment->units.empty();
        };
        std::erase_if(situation.contacts, [&](const auto &contact) { return !alive(contact.regiment); });
        const auto tracked = runtime->memory.target;
        if (tracked && alive(tracked) &&
            std::none_of(situation.contacts.begin(), situation.contacts.end(),
                         [&](const auto &contact) { return contact.regiment == tracked; })) {
            const auto &known = runtime->memory.remembered_contacts;
            const auto last = std::find_if(known.begin(), known.end(), [&](const auto &contact) {
                return contact.regiment == tracked && contact.observed_tick <= world.tick &&
                       world.tick - contact.observed_tick <= ContactTtl;
            });
            if (last != known.end()) {
                auto contact = *last;
                contact.source = ObservationSource::Memory;
                situation.contacts.push_back(contact);
            }
        }

        std::vector<CommandNode> children;
        std::vector<ChildStatus> child_statuses;
        for (NodeId child_id : node.children)
            if (const auto *child = FindNode(state.hierarchy, child_id)) {
                children.push_back(*child);
                child_statuses.push_back(StatusOf(world, state.hierarchy, state, *child));
            }
        const auto *profile = FindProfile(state.hierarchy, node.commander);
        BrainContext context;
        context.tick = world.tick;
        context.node = &node;
        context.profile = profile;
        context.situation = &situation;
        context.memory = &runtime->memory;
        context.subordinates = children;
        context.orders = runtime->orders;
        context.reports = runtime->reports;
        context.children_status = child_statuses;
        context.previous_subordinate_tasks = runtime->sent_tasks;
        if (const auto *parent = FindNode(state.hierarchy, node.parent))
            context.parent_level = parent->level;

        StubResult<DecisionProposal> decision;
        switch (node.level) {
        case Level::Army:
            context.meeting_engagement = world.scenario.rules.meeting_engagement &&
                                         !world.scenario.objective;
            if (world.scenario.rules.local_melee) {
                context.can_stage = [&](NodeId group, Vec3 goal) {
                    const auto *lead = GroupLead(world, state.hierarchy, group);
                    const auto *regiment = lead ? FindRegiment(world, lead->regiment) : nullptr;
                    return regiment && (context.meeting_engagement || Dot(goal - regiment->pose.position,
                                            Forward(situation.own.pose.yaw)) > 1.f) &&
                        FindSupportRoute(navigation, world, *regiment, goal).status == Status::Ok;
                };
                context.observe_order = [&](const CommandTask &task) -> std::optional<ChildOrderObservation> {
                    const auto *lead = GroupLead(world, state.hierarchy, task.recipient);
                    const auto *regiment = lead ? FindRegiment(world, lead->regiment) : nullptr;
                    if (!regiment || task.action != TaskKind::MoveToArea)
                        return {};
                    const float distance_ahead = Dot(task.intent.objective_position - regiment->pose.position,
                                                    Forward(situation.own.pose.yaw));
                    ChildOrderObservation observed;
                    observed.progress = distance_ahead <= 2.f ? 1.f : 0.f;
                    if (context.meeting_engagement) {
                        observed.progress = Distance(task.intent.objective_position, regiment->pose.position) <= 2.f
                            ? 1.f : 0.f;
                        if (const auto *group = FindRuntime(state, task.recipient);
                            group && (!group->memory.active_task || group->memory.active_task == task.id)) {
                            const auto delivered = std::ranges::find(group->orders, task.id, &CommandTask::id);
                            if (delivered != group->orders.end())
                                observed.delivered_tick = delivered->received_tick;
                        }
                        const auto *lead_runtime = FindRuntime(state, lead->id);
                        const bool paused_by_player = lead_runtime &&
                            ExternalOrder(world, *regiment, lead_runtime->last_order_id);
                        const auto &lane = regiment->march_lane;
                        const bool detours_exhausted = lane && lane->detour.empty() &&
                            lane->recovery_attempts >= MarchLane::MaxAttempts;
                        observed.executing = paused_by_player ||
                            (!detours_exhausted && regiment->waypoint < regiment->route.size());
                    }
                    return observed;
                };
                context.choose_group_flank = [&](NodeId wing, const KnownContact &target,
                                                 const CommandTask &task) {
                    return GroupFlankRoute(world, navigation, state, *runtime, wing, target, task);
                };
                context.choose_group_support = [&](NodeId wing, NodeId center, const KnownContact &target) {
                    return GroupSupportPosition(world, navigation, state.hierarchy, wing, center, target);
                };
            }
            decision = ArmyBrain{}.Think(context);
            break;
        case Level::Part:
            decision = PartBrain{}.Think(context);
            break;
        case Level::Group: {
            const auto regiment_of = [&](NodeId id) -> const RegimentState * {
                const auto *child = FindNode(state.hierarchy, id);
                return child ? FindRegiment(world, child->regiment) : nullptr;
            };
            if (world.scenario.rules.local_melee) {
                context.choose_flank = [&](NodeId mover, RegimentId lead, const KnownContact &target,
                                           const CommandTask &task) {
                    return FlankRoute(world, navigation, state, *runtime, mover, lead, target, task);
                };
                context.can_approach = [&](NodeId mover, const KnownContact &target) {
                    const auto *regiment = regiment_of(mover);
                    return regiment && FindAttackRoute(navigation, world, *regiment,
                        target.estimated_pose, target.regiment).status == Status::Ok;
                };
                context.can_stage = [&](NodeId mover, Vec3 goal) {
                    const auto *regiment = regiment_of(mover);
                    return regiment && FindSupportRoute(navigation, world, *regiment, goal).status == Status::Ok;
                };
            }
            context.group_policy = group_policy;
            context.group_order_commitment = group_order_commitment;
            context.observe_order = [&](const CommandTask &task) -> std::optional<ChildOrderObservation> {
                const auto *regiment = regiment_of(task.recipient);
                return regiment ? ObserveChildOrder(world, state, task, *regiment, runtime->reports)
                                : std::nullopt;
            };
            decision = GroupBrain{}.Think(context);
            break;
        }
        case Level::Regiment:
            if (auto *regiment = FindRegiment(world, node.regiment)) {
                context.current_formation = regiment->formation;
                context.in_melee_with = [&, regiment](RegimentId enemy) {
                    return InMeleeWith(world, *regiment, enemy);
                };
            }
            decision = RegimentBrain{}.Think(context);
            break;
        }
        if (decision.status != StubStatus::Ready || !decision.value || !decision.value->next_memory)
            return {StubStatus::InvalidInput, std::nullopt,
                    "Commander rejected its runtime input."};
        auto proposal = std::move(*decision.value);
        runtime->memory = *proposal.next_memory;
        for (auto &task : runtime->orders)
            if (task.id == runtime->memory.active_task)
                task.status = runtime->memory.task_status;

        for (auto &report : proposal.reports) {
            if (!report.recipient || !FindNode(state.hierarchy, report.recipient) ||
                !state.next_message_id || state.next_message_id.value == UINT64_MAX ||
                ReportRecentlyQueued(state.communications, report, world.tick))
                continue;
            QueueMessage(state, report.sender, report.recipient, world.tick, report);
        }

        std::vector<CommandTask> sent;
        for (auto task : proposal.subordinate_tasks) {
            auto old = std::find_if(
                runtime->sent_tasks.begin(), runtime->sent_tasks.end(),
                [&](const auto &candidate) { return candidate.recipient == task.recipient; });
            if (old != runtime->sent_tasks.end() && SameAssignment(*old, task) &&
                !RetryLostAssignment(world, state, *old, world.tick)) {
                sent.push_back(*old);
                continue;
            }
            if (!state.next_task_id || state.next_task_id.value == UINT64_MAX ||
                !state.next_message_id || state.next_message_id.value == UINT64_MAX)
                return {StubStatus::InvalidInput, std::nullopt,
                        "Command task or message counter exhausted."};
            task.id = state.next_task_id;
            ++state.next_task_id.value;
            task.issuer = node.id;
            task.author = node.commander;
            task.status = TaskStatus::Active;
            task.created_tick = world.tick;
            task.received_tick.reset();
            const auto channel = UsesStaffCourier(world, node.id, task.recipient) && !BriefedAtDeployment(world, task)
                                     ? DeliveryChannel::Courier : DeliveryChannel::Signal;
            QueueMessage(state, node.id, task.recipient, world.tick, task, channel);
            sent.push_back(std::move(task));
        }
        runtime->sent_tasks = std::move(sent);

        if (proposal.regiment_directives && node.level == Level::Regiment) {
            auto *regiment = FindRegiment(world, node.regiment);
            if (regiment && (!regiment->following_officer || regiment->panic) &&
                proposal.regiment_directives->formation &&
                regiment->formation != *proposal.regiment_directives->formation) {
                auto status =
                    ReformRegiment(world, regiment->id, *proposal.regiment_directives->formation);
                if (status != Status::Ok)
                    AddNeedSupportFallback(state, *runtime, node, world.tick, situation,
                                           "Requested formation could not be applied.");
            }
        }
        if (proposal.execution_order && node.level == Level::Regiment) {
            auto *regiment = FindRegiment(world, node.regiment);
            if (regiment && !ExternalOrder(world, *regiment, runtime->last_order_id)) {
                Order order = *proposal.execution_order;
                order.issuer = node.commander;
                order.recipient = regiment->id;
                auto current = regiment->order && regiment->order->id == runtime->last_order_id &&
                                       regiment->order->status == OrderStatus::Active
                                   ? &*regiment->order
                                   : nullptr;
                if (!current || !SameOrder(*current, order, world.scenario.rules.regiment_tactics)) {
                    auto pending =
                        std::find_if(world.pending_orders.begin(), world.pending_orders.end(),
                                     [&](const auto &x) { return x.id == runtime->last_order_id; });
                    if (pending == world.pending_orders.end() ||
                        !SameOrder(*pending, order, world.scenario.rules.regiment_tactics)) {
                        order.id = {world.next_order_id};
                        auto status = SubmitOrder(world, order);
                        if (status == Status::Ok)
                            runtime->last_order_id = order.id;
                        else if (node.parent)
                            AddNeedSupportFallback(state, *runtime, node, world.tick, situation,
                                                   "The proposed order was rejected by the world.");
                    }
                }
            }
        }
        runtime->schedule.next_think_tick = adaptive
            ? NextBrainPhase(node.id, runtime->schedule.interval_ticks, world.tick)
            :
            world.tick > UINT64_MAX - runtime->schedule.interval_ticks
                ? UINT64_MAX
                : world.tick + runtime->schedule.interval_ticks;
        runtime->schedule.commitment_until =
            runtime->memory.state_entered_tick >
                    UINT64_MAX - (profile ? profile->minimum_commitment_ticks : 0)
                ? UINT64_MAX
                : runtime->memory.state_entered_tick +
                      (profile ? profile->minimum_commitment_ticks : 0);
    }

    AdvanceGroupCommanders(world, state.hierarchy, navigation);
    return {StubStatus::Ready, true, "Due command brains updated."};
}

void WakeCommandForPanicChange(World &world, RegimentId regiment) {
    if (!world.scenario.rules.adaptive_battle || !world.command_state)
        return;
    auto &state = *world.command_state;
    auto leaf = std::find_if(state.hierarchy.nodes.begin(), state.hierarchy.nodes.end(), [&](const auto &node) {
        return node.level == Level::Regiment && node.regiment == regiment;
    });
    if (leaf == state.hierarchy.nodes.end())
        return;
    for (const auto *node = &*leaf; node; node = FindNode(state.hierarchy, node->parent)) {
        if (auto *runtime = FindRuntime(state, node->id))
            runtime->schedule.next_think_tick = world.tick;
    }
}
} // namespace battle::command
