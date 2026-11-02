#include "CommandBrainCommon.h"
#include "battle/Formation.h"
#include "battle/Navigation.h"
#include "battle/Officers.h"
#include "battle/World.h"
#include "battle/command/Runtime.h"
#include "battle/command/Services.h"
#include <map>
#include <set>

namespace battle::command {
namespace {
constexpr Tick ContactTtl = 100;
constexpr size_t HistoryLimit = 64;

Tick DefaultInterval(Level level) {
    switch (level) {
    case Level::Army:
        return 40;
    case Level::Part:
        return 20;
    case Level::Group:
        return 10;
    case Level::Regiment:
        return 5;
    }
    return 5;
}

NodeRuntime *FindRuntime(CommandState &state, NodeId id) {
    auto it = std::find_if(state.nodes.begin(), state.nodes.end(),
                           [&](const auto &node) { return node.node == id; });
    return it == state.nodes.end() ? nullptr : &*it;
}
const NodeRuntime *FindRuntime(const CommandState &state, NodeId id) {
    return FindRuntime(const_cast<CommandState &>(state), id);
}

bool EligibleSuccessor(const World &world, const CommandHierarchy &hierarchy,
                       const CommandNode &node, OfficerId id) {
    const auto officer = std::find_if(world.officers.begin(), world.officers.end(),
                                      [&](const auto &candidate) { return candidate.id == id; });
    if (officer == world.officers.end() || (officer->role != 1 && officer->role != 2))
        return false;
    const auto *unit = FindUnit(world, officer->unit);
    const auto *regiment = unit ? FindRegiment(world, unit->regiment) : nullptr;
    if (!unit || !unit->alive || !regiment || regiment->team != node.team ||
        (node.level == Level::Regiment && regiment->id != node.regiment) ||
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

bool SameOrder(const Order &a, const Order &b) {
    return a.issuer == b.issuer && a.recipient == b.recipient && a.type == b.type &&
           a.priority == b.priority && Distance(a.point, b.point) <= 1.f &&
           a.target_unit == b.target_unit && a.target_regiment == b.target_regiment &&
           a.expiry_tick == b.expiry_tick;
}

bool ExternalOrder(const World &world, const RegimentState &regiment, OrderId owned) {
    if (regiment.order && regiment.order->status == OrderStatus::Active &&
        regiment.order->id != owned)
        return true;
    return std::any_of(world.pending_orders.begin(), world.pending_orders.end(),
                       [&](const auto &order) {
                           return order.recipient == regiment.id &&
                                  order.status == OrderStatus::Pending && order.id != owned;
                       });
}

void QueueMessage(CommandState &state, NodeId sender, NodeId recipient, Tick tick,
                  std::variant<CommandTask, SituationReport> payload) {
    MessageEnvelope message;
    message.id = state.next_message_id;
    if (state.next_message_id.value != UINT64_MAX)
        ++state.next_message_id.value;
    message.sender = sender;
    message.recipient = recipient;
    message.channel = DeliveryChannel::Signal;
    message.created_tick = tick;
    message.delivery_tick = tick == UINT64_MAX ? tick : tick + 1;
    message.status = DeliveryStatus::InTransit;
    message.payload = std::move(payload);
    if (const auto *task = std::get_if<CommandTask>(&message.payload))
        message.expiry_tick = task->expiry_tick;
    state.communications.messages.push_back(std::move(message));
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
        const bool terminal = message.status == DeliveryStatus::Delivered ||
                              message.status == DeliveryStatus::Expired ||
                              message.status == DeliveryStatus::Lost ||
                              message.status == DeliveryStatus::RecipientUnavailable;
        return terminal && old(message.created_tick);
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

ChildStatus StatusOf(const World &world, const CommandHierarchy &hierarchy,
                     const CommandState &state, const CommandNode &node) {
    ChildStatus result;
    result.node = node.id;
    if (node.level == Level::Regiment) {
        if (const auto *regiment = FindRegiment(world, node.regiment))
            result.pose = regiment->pose;
    } else if (auto pose = NodePose(world, hierarchy, node.id))
        result.pose = *pose;
    uint64_t living = 0, initial = 0, panicked = 0;
    float width = 0;
    for (auto regiment_id : OwnedRegiments(hierarchy, node.id))
        if (const auto *regiment = FindRegiment(world, regiment_id)) {
            uint32_t alive = 0;
            for (auto unit_id : regiment->units)
                if (const auto *unit = FindUnit(world, unit_id); unit && unit->alive)
                    ++alive;
            living += alive;
            initial += regiment->initial_count;
            if (regiment->panic)
                panicked += alive;
            width += Footprint(*regiment).width_m;
        }
    result.living = uint32_t(std::min<uint64_t>(living, UINT32_MAX));
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

} // namespace

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
        state.nodes.push_back(runtime);
    }
    std::set<uint64_t> task_ids;
    for (auto mission : world.scenario.command_setup->missions) {
        const auto *recipient = FindNode(state.hierarchy, mission.recipient);
        if (!ValidTaskRequest(mission) || mission.issuer || !recipient ||
            recipient->level != Level::Army ||
            unsigned(mission.status) > unsigned(TaskStatus::Cancelled))
            return {StubStatus::InvalidInput, std::nullopt, "Invalid initial army mission."};
        if (!mission.id) {
            mission.id = state.next_task_id;
            ++state.next_task_id.value;
        }
        if (mission.id.value == UINT64_MAX)
            return {StubStatus::InvalidInput, std::nullopt, "Initial mission ID is exhausted."};
        if (!task_ids.insert(mission.id.value).second)
            return {StubStatus::InvalidInput, std::nullopt, "Duplicate initial mission ID."};
        if (mission.intent.target_regiment) {
            const auto *target = FindRegiment(world, mission.intent.target_regiment);
            if (!target || target->team == recipient->team)
                return {StubStatus::InvalidInput, std::nullopt,
                        "Initial mission target is missing or friendly."};
        }
        if (auto expiry = detail::TaskExpiry(mission, world.tick); expiry && *expiry <= world.tick)
            return {StubStatus::InvalidInput, std::nullopt, "Initial mission is already expired."};
        state.next_task_id.value = std::max(state.next_task_id.value, mission.id.value + 1);
        mission.status = TaskStatus::Active;
        mission.received_tick = world.tick;
        if (auto *runtime = FindRuntime(state, mission.recipient))
            runtime->orders.push_back(std::move(mission));
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
        (task.issuer ? task.issuer != recipient->parent : recipient->level != Level::Army))
        return {StubStatus::InvalidInput, std::nullopt, "Task issuer has no authority."};
    if (task.author) {
        const auto *issuer = task.issuer ? FindNode(state.hierarchy, task.issuer) : recipient;
        if (!issuer || (issuer->commander && issuer->commander != task.author))
            return {StubStatus::InvalidInput, std::nullopt, "Task author is not the issuer."};
    }
    if (task.intent.target_regiment) {
        const auto *target = FindRegiment(world, task.intent.target_regiment);
        if (!target || target->team == recipient->team)
            return {StubStatus::InvalidInput, std::nullopt, "Task target is missing or friendly."};
    }
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

StubResult<bool> UpdateCommand(World &world, const NavigationService &navigation) {
    if (!world.command_state)
        return {StubStatus::Ready, false, "Command runtime is disabled."};
    auto &state = *world.command_state;
    Prune(state, world.tick);

    for (auto &node : state.hierarchy.nodes) {
        auto successor = ResolveSuccessor(world, state.hierarchy, node);
        if (successor != node.commander) {
            const auto previous = node.commander;
            node.commander = successor;
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
        for (auto &task : runtime.orders)
            if (task.id == runtime.memory.active_task)
                task.status = TaskStatus::Failed;
        runtime.memory.task_status = TaskStatus::Failed;
        runtime.memory.state = FsmState::Failed;
        runtime.last_order_id = {};
        runtime.schedule.next_think_tick = world.tick;
        auto situation = BuildSituation(world, state.hierarchy, node->id, world.tick);
        if (situation.status == StubStatus::Ready && situation.value)
            AddNeedSupportFallback(state, runtime, *node, world.tick, *situation.value,
                                   "Assigned route or target became unavailable.");
    }

    for (auto &message : state.communications.messages) {
        if (message.channel != DeliveryChannel::Courier ||
            message.status != DeliveryStatus::InTransit)
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
        auto path = navigation.FindPath({courier->pose, destination->position, {1, 1}});
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
                (sender->commander && task->author != sender->commander)) {
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
                             [&](const auto &old) { return old.id == delivered.id; }))
                runtime->orders.push_back(std::move(delivered));
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
        const auto node = *node_ptr;
        auto perception = BuildSituation(world, state.hierarchy, node.id, world.tick);
        if (perception.status != StubStatus::Ready || !perception.value)
            return {StubStatus::InvalidInput, std::nullopt, "Could not build local situation."};
        auto situation = std::move(*perception.value);
        MergeReportedContacts(situation, runtime->reports, world.tick);

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

        StubResult<DecisionProposal> decision;
        switch (node.level) {
        case Level::Army:
            decision = ArmyBrain{}.Think(context);
            break;
        case Level::Part:
            decision = PartBrain{}.Think(context);
            break;
        case Level::Group:
            decision = GroupBrain{}.Think(context);
            break;
        case Level::Regiment:
            if (auto *regiment = FindRegiment(world, node.regiment))
                context.current_formation = regiment->formation;
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
            if (old != runtime->sent_tasks.end() && SameAssignment(*old, task)) {
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
            QueueMessage(state, node.id, task.recipient, world.tick, task);
            sent.push_back(std::move(task));
        }
        runtime->sent_tasks = std::move(sent);

        if (proposal.regiment_directives && node.level == Level::Regiment) {
            auto *regiment = FindRegiment(world, node.regiment);
            if (regiment && proposal.regiment_directives->formation &&
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
                if (!current || !SameOrder(*current, order)) {
                    auto pending =
                        std::find_if(world.pending_orders.begin(), world.pending_orders.end(),
                                     [&](const auto &x) { return x.id == runtime->last_order_id; });
                    if (pending == world.pending_orders.end() || !SameOrder(*pending, order)) {
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
        runtime->schedule.next_think_tick =
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

    return {StubStatus::Ready, true, "Due command brains updated."};
}
} // namespace battle::command
