#include "battle/command/Runtime.h"
#include "battle/World.h"
#include "battle/command/Services.h"
#include <bit>
#include <set>

namespace battle::command {
namespace {
bool EnumAtMost(auto value, unsigned maximum) {
    using E = decltype(value);
    return std::is_enum_v<E> && unsigned(value) <= maximum;
}
bool UnitFloat(std::optional<float> value, float low = 0.f, float high = 1.f) {
    return !value || (std::isfinite(*value) && *value >= low && *value <= high);
}
bool ValidContact(const KnownContact &c, const CommandHierarchy &h, const World &w,
                  Tick now) {
    return EnumAtMost(c.allegiance, unsigned(Allegiance::Enemy)) &&
           EnumAtMost(c.source, unsigned(ObservationSource::Memory)) &&
           (!c.command || FindNode(h, c.command)) &&
           (!c.regiment || FindRegiment(w, c.regiment)) && Finite(c.estimated_pose) &&
           UnitFloat(c.estimated_strength, 0.f, std::numeric_limits<float>::max()) &&
           c.observed_tick <= now && std::isfinite(c.confidence) && c.confidence >= 0.f &&
           c.confidence <= 1.f;
}
bool ValidSituation(const LocalSituation &s, const CommandHierarchy &h, const World &w,
                    Tick now) {
    if (!s.observer || !FindNode(h, s.observer) || s.tick > now ||
        s.own.living > s.own.initial || !Finite(s.own.pose) ||
        !Finite(s.rally_point) || !UnitFloat(s.own.morale) || !UnitFloat(s.own.cohesion) ||
        !UnitFloat(s.own.fatigue) || !UnitFloat(s.own.ammunition_fraction))
        return false;
    for (const auto &c : s.contacts)
        if (!ValidContact(c, h, w, now))
            return false;
    for (const auto &t : s.threats)
        if (!EnumAtMost(t.sector, unsigned(RelativeSector::Rear)) ||
            !UnitFloat(t.strength, 0.f, std::numeric_limits<float>::max()) ||
            !std::isfinite(t.confidence) || t.confidence < 0.f || t.confidence > 1.f)
            return false;
    for (const auto &t : s.terrain)
        if (!Finite(t.position) || !UnitFloat(t.cover) || !UnitFloat(t.passability) ||
            !UnitFloat(t.height_advantage, -std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()))
            return false;
    return true;
}
bool ValidTask(const CommandTask &t, const CommandHierarchy &h, const World &w, Tick now) {
    const auto *recipient = FindNode(h, t.recipient);
    const auto *issuer = t.issuer ? FindNode(h, t.issuer) : nullptr;
    if (!t.id || !recipient ||
        (t.issuer ? (!issuer || t.issuer != recipient->parent)
                  : recipient->level != Level::Army) ||
        (t.author && !FindProfile(h, t.author)) ||
        !ValidTaskRequest(t) || t.created_tick > now ||
        (t.received_tick && *t.received_tick > now) ||
        (t.parent_task && t.parent_task.value == t.id.value) ||
        (t.intent.target_command && !FindNode(h, t.intent.target_command)))
        return false;
    if (t.intent.target_regiment) {
        const auto *target = FindRegiment(w, t.intent.target_regiment);
        if (!target || target->team == recipient->team)
            return false;
    }
    return true;
}
bool ValidReport(const SituationReport &r, const CommandHierarchy &h, const World &w,
                 Tick now) {
    return EnumAtMost(r.kind, unsigned(ReportKind::CommanderLost)) && r.sender &&
           FindNode(h, r.sender) && (!r.recipient || FindNode(h, r.recipient)) &&
           r.observed_tick <= r.created_tick && r.created_tick <= now &&
           ValidSituation(r.situation, h, w, now);
}

struct Hash {
    uint64_t h{14695981039346656037ull};
    void n(uint64_t v) {
        for (unsigned i = 0; i < 8; ++i) {
            h ^= v & 255;
            h *= 1099511628211ull;
            v >>= 8;
        }
    }
    void f(float v) { n(std::bit_cast<uint32_t>(v)); }
    void text(const std::string &v) {
        n(v.size());
        for (unsigned char c : v)
            n(c);
    }
    void vec(Vec3 v) { f(v.x); f(v.y); f(v.z); }
    void pose(Pose v) { vec(v.position); f(v.yaw); }
    template <class T> void id(Id<T> v) { n(v.value); }
    template <class T, class F> void optional(const std::optional<T> &v, F add) {
        n(v.has_value());
        if (v)
            add(*v);
    }
    void profile(const OfficerProfile &v) {
        id(v.officer); f(v.tactical_skill); f(v.initiative); f(v.discipline); f(v.aggression);
        f(v.caution); f(v.charisma); f(v.leadership); n(v.reaction_delay_ticks);
        n(v.reevaluation_ticks); n(v.minimum_commitment_ticks);
        optional(v.command_capacity, [&](auto x) { n(x); });
        optional(v.command_radius_m, [&](auto x) { f(x); });
    }
    void node(const CommandNode &v) {
        id(v.id); id(v.parent); n(unsigned(v.level)); n(uint64_t(v.team)); text(v.name);
        n(unsigned(v.sector_role)); id(v.commander); n(v.succession.size());
        for (auto x : v.succession) id(x);
        n(v.children.size()); for (auto x : v.children) id(x); id(v.regiment);
    }
    void hierarchy(const CommandHierarchy &v) {
        n(v.nodes.size()); for (const auto &x : v.nodes) node(x);
        n(v.officers.size()); for (const auto &x : v.officers) profile(x);
        n(v.support.size());
        for (const auto &x : v.support) { id(x.officer); id(x.command); n(unsigned(x.duty)); }
    }
    void contact(const KnownContact &v) {
        id(v.command); id(v.regiment); n(unsigned(v.allegiance));
        optional(v.team, [&](auto x) { n(uint64_t(int64_t(x))); }); pose(v.estimated_pose);
        optional(v.estimated_strength, [&](auto x) { f(x); }); n(v.observed_tick); f(v.confidence);
        n(unsigned(v.source));
    }
    void situation(const LocalSituation &v) {
        id(v.observer); n(v.tick); n(v.own.living); n(v.own.initial); pose(v.own.pose);
        n(v.own.panic); optional(v.own.morale, [&](auto x) { f(x); });
        optional(v.own.cohesion, [&](auto x) { f(x); });
        optional(v.own.fatigue, [&](auto x) { f(x); });
        optional(v.own.ammunition_fraction, [&](auto x) { f(x); }); vec(v.rally_point);
        n(v.contacts.size()); for (const auto &x : v.contacts) contact(x);
        n(v.threats.size());
        for (const auto &x : v.threats) {
            n(unsigned(x.sector)); optional(x.strength, [&](auto y) { f(y); }); f(x.confidence);
            n(x.under_ranged_fire); n(x.cavalry_approaching);
        }
        n(v.terrain.size());
        for (const auto &x : v.terrain) {
            vec(x.position); optional(x.cover, [&](auto y) { f(y); });
            optional(x.passability, [&](auto y) { f(y); });
            optional(x.height_advantage, [&](auto y) { f(y); });
        }
    }
    void intent(const CommandIntent &v) {
        text(v.purpose); vec(v.objective_position); f(v.objective_radius_m);
        id(v.target_command); id(v.target_regiment);
    }
    void task(const CommandTask &v) {
        id(v.id); id(v.parent_task); id(v.issuer); id(v.recipient); id(v.author);
        n(unsigned(v.action)); intent(v.intent); n(unsigned(v.authority.initiative));
        n(v.authority.may_pursue); n(v.authority.may_break_formation);
        n(v.authority.may_change_target);
        optional(v.authority.pursuit_limit_m, [&](auto x) { f(x); });
        optional(v.authority.acceptable_loss_fraction, [&](auto x) { f(x); });
        n(uint64_t(int64_t(v.priority))); n(v.created_tick);
        optional(v.not_before_tick, [&](auto x) { n(x); });
        optional(v.expiry_tick, [&](auto x) { n(x); });
        optional(v.duration_ticks, [&](auto x) { n(x); }); n(unsigned(v.duration_origin));
        n(unsigned(v.status)); optional(v.formation_columns, [&](auto x) { n(x); });
        optional(v.formation_spacing_m, [&](auto x) { f(x); });
        optional(v.received_tick, [&](auto x) { n(x); });
    }
    void report(const SituationReport &v) {
        n(unsigned(v.kind)); id(v.sender); id(v.recipient); id(v.about_task);
        n(v.observed_tick); n(v.created_tick); situation(v.situation); text(v.explanation);
    }
    void option(const TacticalOption &v) {
        n(unsigned(v.action)); id(v.recipient); id(v.target_command); id(v.target_regiment);
        vec(v.destination); text(v.rationale);
    }
    void memory(const BrainMemory &v) {
        n(unsigned(v.state)); n(v.state_entered_tick); n(v.task_started_tick); id(v.active_task);
        n(unsigned(v.task_status)); id(v.target); vec(v.goal);
        n(v.remembered_contacts.size()); for (const auto &x : v.remembered_contacts) contact(x);
        n(v.recent_decisions.size());
        for (const auto &x : v.recent_decisions) {
            n(x.tick); id(x.cause); option(x.selected); text(x.explanation);
        }
        n(v.acknowledged_tasks.size()); for (auto x : v.acknowledged_tasks) id(x);
        n(v.last_think_tick);
    }
    void node_runtime(const NodeRuntime &v) {
        id(v.node); memory(v.memory); n(v.orders.size());
        for (const auto &x : v.orders) task(x);
        n(v.reports.size()); for (const auto &x : v.reports) report(x);
        id(v.schedule.node); n(v.schedule.next_think_tick); n(v.schedule.interval_ticks);
        n(v.schedule.commitment_until); id(v.last_order_id); n(v.sent_tasks.size());
        for (const auto &x : v.sent_tasks) task(x);
    }
    void message(const MessageEnvelope &v) {
        id(v.id); id(v.sender); id(v.recipient); n(unsigned(v.channel)); id(v.courier);
        n(v.created_tick); n(v.delivery_tick); optional(v.expiry_tick, [&](auto x) { n(x); });
        n(unsigned(v.status)); n(v.payload.index());
        if (auto *t = std::get_if<CommandTask>(&v.payload)) task(*t);
        else report(std::get<SituationReport>(v.payload));
    }
    void command_state(const CommandState &v) {
        hierarchy(v.hierarchy); n(v.nodes.size());
        for (const auto &x : v.nodes) node_runtime(x);
        n(v.communications.messages.size());
        for (const auto &x : v.communications.messages) message(x);
        id(v.next_task_id); id(v.next_message_id);
    }
};

uint64_t SetupHash(const CommandSetup &setup) {
    Hash h;
    h.hierarchy(setup.hierarchy);
    h.n(setup.missions.size());
    for (const auto &mission : setup.missions) h.task(mission);
    return h.h;
}
} // namespace

Result<bool> ValidateCommandState(const World &world, const CommandState &state) {
    auto bad = [](const char *why) {
        return Result<bool>{Status::InvalidInput, false,
                            {{"command_state", 0, why}}};
    };
    if (!world.scenario.command_setup || !world.command_state ||
        state.nodes.size() != state.hierarchy.nodes.size() ||
        !state.next_task_id || !state.next_message_id ||
        state.next_task_id.value == UINT64_MAX || state.next_message_id.value == UINT64_MAX)
        return bad("command runtime does not match scenario setup or has invalid counters");
    auto hierarchy = ValidateHierarchy(state.hierarchy, world);
    if (hierarchy.status != StubStatus::Ready || !hierarchy.value)
        return bad("invalid command hierarchy");

    auto original = world.scenario.command_setup->hierarchy;
    auto current = state.hierarchy;
    auto node_order = [](const auto &a, const auto &b) { return a.id < b.id; };
    auto officer_order = [](const auto &a, const auto &b) { return a.officer < b.officer; };
    auto support_order = [](const auto &a, const auto &b) {
        return std::tie(a.command, a.duty, a.officer) < std::tie(b.command, b.duty, b.officer);
    };
    std::sort(original.nodes.begin(), original.nodes.end(), node_order);
    std::sort(current.nodes.begin(), current.nodes.end(), node_order);
    std::sort(original.officers.begin(), original.officers.end(), officer_order);
    std::sort(current.officers.begin(), current.officers.end(), officer_order);
    std::sort(original.support.begin(), original.support.end(), support_order);
    std::sort(current.support.begin(), current.support.end(), support_order);
    if (original.nodes.size() != current.nodes.size() || original.officers != current.officers ||
        original.support != current.support)
        return bad("command topology or officer profiles differ from scenario");
    for (size_t i = 0; i < original.nodes.size(); ++i) {
        current.nodes[i].commander = original.nodes[i].commander;
        if (original.nodes[i] != current.nodes[i])
            return bad("command topology differs from scenario");
    }

    uint64_t max_task = 0;
    uint64_t max_message = 0;
    std::set<uint64_t> runtime_nodes;
    for (size_t i = 0; i < state.nodes.size(); ++i) {
        const auto &runtime = state.nodes[i];
        if (!runtime.node || !runtime_nodes.insert(runtime.node.value).second ||
            !FindNode(state.hierarchy, runtime.node) || runtime.schedule.node != runtime.node ||
            !runtime.schedule.interval_ticks ||
            !EnumAtMost(runtime.memory.state, unsigned(FsmState::Complete)) ||
            !EnumAtMost(runtime.memory.task_status, unsigned(TaskStatus::Cancelled)) ||
            !Finite(runtime.memory.goal) || runtime.memory.state_entered_tick > world.tick ||
            runtime.memory.task_started_tick > world.tick || runtime.memory.last_think_tick > world.tick ||
            runtime.schedule.next_think_tick > UINT64_MAX - runtime.schedule.interval_ticks ||
            (runtime.last_order_id && runtime.last_order_id.value >= world.next_order_id))
            return bad("invalid command node runtime or schedule");
        if (runtime.memory.active_task) {
            if (runtime.memory.active_task.value >= state.next_task_id.value)
                return bad("active task ID is outside its counter");
            max_task = std::max(max_task, runtime.memory.active_task.value);
        }
        for (const auto &task : runtime.orders) {
            if (!ValidTask(task, state.hierarchy, world, world.tick))
                return bad("invalid active command task");
            max_task = std::max(max_task, task.id.value);
            if (task.parent_task) max_task = std::max(max_task, task.parent_task.value);
        }
        for (const auto &task : runtime.sent_tasks) {
            if (!ValidTask(task, state.hierarchy, world, world.tick))
                return bad("invalid sent command task");
            max_task = std::max(max_task, task.id.value);
            if (task.parent_task) max_task = std::max(max_task, task.parent_task.value);
        }
        for (const auto &report : runtime.reports) {
            if (!ValidReport(report, state.hierarchy, world, world.tick))
                return bad("invalid command report");
            if (report.about_task) max_task = std::max(max_task, report.about_task.value);
        }
        for (const auto &contact : runtime.memory.remembered_contacts)
            if (!ValidContact(contact, state.hierarchy, world, world.tick))
                return bad("invalid remembered contact");
        for (const auto &decision : runtime.memory.recent_decisions)
            if (decision.tick > world.tick || !EnumAtMost(decision.selected.action,
                                                           unsigned(TaskKind::Reform)) ||
                !Finite(decision.selected.destination))
                return bad("invalid command decision record");
            else if (decision.cause)
                max_task = std::max(max_task, decision.cause.value);
        for (auto task_id : runtime.memory.acknowledged_tasks) {
            if (!task_id || task_id.value >= state.next_task_id.value)
                return bad("invalid acknowledged task ID");
            max_task = std::max(max_task, task_id.value);
        }
    }
    if (runtime_nodes.size() != state.hierarchy.nodes.size())
        return bad("command runtime nodes do not cover hierarchy");
    std::set<uint64_t> message_ids;
    for (const auto &message : state.communications.messages) {
        if (!message.id || message.id.value >= state.next_message_id.value ||
            !message_ids.insert(message.id.value).second ||
            !FindNode(state.hierarchy, message.sender) ||
            !FindNode(state.hierarchy, message.recipient) ||
            !EnumAtMost(message.channel, unsigned(DeliveryChannel::Courier)) ||
            !EnumAtMost(message.status, unsigned(DeliveryStatus::RecipientUnavailable)) ||
            message.created_tick > world.tick || message.delivery_tick < message.created_tick ||
            (message.expiry_tick && *message.expiry_tick < message.created_tick) ||
            (message.courier && !FindUnit(world, message.courier)) ||
            (message.channel == DeliveryChannel::Courier && !message.courier))
            return bad("invalid communications envelope");
        max_message = std::max(max_message, message.id.value);
        if (const auto *task = std::get_if<CommandTask>(&message.payload)) {
            if (!ValidTask(*task, state.hierarchy, world, world.tick))
                return bad("invalid task message payload");
            max_task = std::max(max_task, task->id.value);
        } else if (!ValidReport(std::get<SituationReport>(message.payload), state.hierarchy,
                                world, world.tick))
            return bad("invalid report message payload");
        else if (const auto *report = std::get_if<SituationReport>(&message.payload);
                 report && report->about_task)
            max_task = std::max(max_task, report->about_task.value);
    }
    if (max_task >= state.next_task_id.value || max_message >= state.next_message_id.value)
        return bad("command ID counter does not exceed persisted IDs");
    return {Status::Ok, true, {}};
}

uint64_t CommandStateHash(const CommandState &state) {
    Hash h;
    h.command_state(state);
    return h.h;
}
uint64_t CommandSetupHash(const CommandSetup &setup) { return SetupHash(setup); }
uint64_t CommandTaskHash(const CommandTask &task) {
    Hash h;
    h.task(task);
    return h.h;
}
} // namespace battle::command
