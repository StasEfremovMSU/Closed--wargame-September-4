#include "CommandBrainCommon.h"
#include "battle/World.h"
#include "battle/command/Services.h"
#include <set>
namespace battle::command {
namespace {
const OfficerSpec *Officer(const World &w, OfficerId id) {
    auto i = std::find_if(w.officers.begin(), w.officers.end(),
                          [&](const auto &o) { return o.id == id; });
    return i == w.officers.end() ? nullptr : &*i;
}
bool Eligible(const World &w, const CommandNode &n, OfficerId id) {
    auto *o = Officer(w, id);
    if (!o || (o->role != 1 && o->role != 2))
        return false;
    auto *u = FindUnit(w, o->unit);
    if (!u || !u->alive)
        return false;
    return UnitTeam(w, *u) == n.team &&
           (n.level != Level::Regiment || u->regiment == n.regiment);
}
} // namespace
StubResult<HierarchyValidation> ValidateHierarchy(const CommandHierarchy &h, const World &w) {
    HierarchyValidation report;
    auto issue = [&](const char *s) { report.issues.push_back(s); };
    if (h.nodes.empty())
        issue("Hierarchy is empty.");
    std::set<uint64_t> ids, commanders, regiments, profiles;
    for (const auto &p : h.officers) {
        if (!p.officer || !profiles.insert(p.officer.value).second || !Officer(w, p.officer))
            issue("Invalid or duplicate officer profile.");
        for (float v : {p.tactical_skill, p.initiative, p.discipline, p.aggression, p.caution,
                        p.charisma, p.leadership})
            if (!std::isfinite(v) || v < 0 || v > 1)
                issue("Profile trait must be finite0..1.");
        if ((p.command_radius_m &&
             (!std::isfinite(*p.command_radius_m) || *p.command_radius_m <= 0)) ||
            p.reaction_delay_ticks > 1000000 || p.reevaluation_ticks > 1000000 ||
            p.minimum_commitment_ticks > 1000000)
            issue("Invalid command radius or timer.");
    }
    for (const auto &n : h.nodes) {
        if (!n.id || n.id.value == UINT64_MAX || !ids.insert(n.id.value).second ||
            unsigned(n.level) > unsigned(Level::Regiment) ||
            unsigned(n.sector_role) > unsigned(SectorRole::Detached))
            issue("Invalid or duplicate command node.");
        if (n.level == Level::Army) {
            if (n.parent)
                issue("Army cannot have a parent.");
        } else if (!IsCommandRoot(n)) {
            auto *p = FindNode(h, n.parent);
            if (!p || !CanCommandLevel(p->level, n.level) || p->team != n.team ||
                std::count(p->children.begin(), p->children.end(), n.id) != 1)
                issue("Invalid command parent, level, team or back-reference.");
        }
        std::set<uint64_t> children;
        for (auto child : n.children) {
            auto *p = FindNode(h, child);
            if (!child || !children.insert(child.value).second || !p || p->parent != n.id)
                issue("Invalid child reference.");
        }
        if (n.level == Level::Regiment) {
            auto *r = FindRegiment(w, n.regiment);
            if (!r || r->team != n.team || !n.children.empty() ||
                !regiments.insert(n.regiment.value).second)
                issue("Invalid or duplicate regiment ownership.");
        } else if (n.regiment)
            issue("Only regiment leaves bind physical regiments.");
        if (n.commander) {
            auto *o = Officer(w, n.commander);
            auto *u = o ? FindUnit(w, o->unit) : nullptr;
            if (!commanders.insert(n.commander.value).second || !o ||
                (o->role != 1 && o->role != 2) || !u || UnitTeam(w, *u) != n.team ||
                (n.level == Level::Regiment && u->regiment != n.regiment) ||
                !FindProfile(h, n.commander))
                issue("Invalid or duplicate commander.");
        }
        if (auto *p = FindProfile(h, n.commander);
            p && p->command_capacity && n.children.size() > *p->command_capacity)
            issue("Direct subordinate capacity exceeded.");
        std::set<uint64_t> successors;
        for (auto candidate : n.succession) {
            auto *o = Officer(w, candidate);
            auto *u = o ? FindUnit(w, o->unit) : nullptr;
            if (!candidate || !successors.insert(candidate.value).second || !o ||
                (o->role != 1 && o->role != 2) || !u || UnitTeam(w, *u) != n.team ||
                (n.level == Level::Regiment && u->regiment != n.regiment))
                issue("Invalid successor.");
        }
    }
    std::set<OfficerId> army_staff;
    for (const auto &a : h.support) {
        const auto *n = FindNode(h, a.command);
        const auto *o = Officer(w, a.officer);
        const auto *u = o ? FindUnit(w, o->unit) : nullptr;
        if (!n || !u || UnitTeam(w, *u) != n->team || unsigned(a.duty) > unsigned(SupportDuty::Bodyguard)) {
            issue("Invalid support assignment.");
            continue;
        }
        if (o->role == ArmyStaffOfficerRole) {
            if (n->level != Level::Army || u->regiment || u->troop != UnitTroop::Mounted ||
                (a.duty != SupportDuty::Adjutant && a.duty != SupportDuty::Bodyguard) ||
                !army_staff.insert(a.officer).second)
                issue("Invalid army staff assignment.");
        } else if (!u->regiment || a.duty == SupportDuty::Bodyguard) {
            issue("Invalid regiment support assignment.");
        }
    }
    bool ok = report.issues.empty();
    return {ok ? StubStatus::Ready : StubStatus::InvalidInput, std::move(report),
            ok ? "Hierarchy validated." : "Invalid command hierarchy."};
}
StubResult<OfficerId> SelectSuccessor(const World &w, const CommandNode &node) {
    if (!node.id || unsigned(node.level) > unsigned(Level::Regiment))
        return {StubStatus::InvalidInput, std::nullopt, "Invalid command node."};
    if (Eligible(w, node, node.commander))
        return {StubStatus::Ready, node.commander, "Current commander alive."};
    for (auto id : node.succession)
        if (Eligible(w, node, id))
            return {StubStatus::Ready, id, "Ordered successor selected."};
    if (node.level == Level::Regiment) {
        OfficerId best{};
        for (const auto &o : w.officers)
            if (Eligible(w, node, o.id) && (!best || o.id < best))
                best = o.id;
        return {StubStatus::Ready, best, "Live regiment successor or vacancy."};
    }
    return {StubStatus::Ready, OfficerId{}, "Command position vacant."};
}
StubResult<std::vector<NodeId>> ScheduleBrains(std::span<const BrainSchedule> schedules,
                                               Tick tick) {
    std::vector<NodeId> due;
    std::set<uint64_t> seen;
    for (const auto &s : schedules) {
        if (!s.node || !seen.insert(s.node.value).second || s.interval_ticks > 1000000)
            return {StubStatus::InvalidInput, std::nullopt, "Invalid or duplicate schedule."};
        if (s.next_think_tick <= tick)
            due.push_back(s.node);
    }
    std::sort(due.begin(), due.end());
    return {StubStatus::Ready, std::move(due), "Due command nodes."};
}
StubResult<CommunicationsUpdate> AdvanceCommunications(const CommunicationsState &state,
                                                       Tick tick) {
    std::set<uint64_t> ids;
    for (const auto &message : state.messages) {
        if (!message.id || !message.sender || !message.recipient ||
            !ids.insert(message.id.value).second ||
            unsigned(message.channel) > unsigned(DeliveryChannel::Courier) ||
            unsigned(message.status) > unsigned(DeliveryStatus::RecipientUnavailable) ||
            message.created_tick > message.delivery_tick ||
            (message.expiry_tick && *message.expiry_tick < message.created_tick) ||
            (message.channel == DeliveryChannel::Courier && !message.courier &&
             (message.status == DeliveryStatus::InTransit || message.status == DeliveryStatus::Delivered)))
            return {StubStatus::InvalidInput, std::nullopt, "Invalid or duplicate message."};
        if (auto *task = std::get_if<CommandTask>(&message.payload)) {
            if (!detail::ValidTask(*task) || task->issuer != message.sender ||
                task->recipient != message.recipient)
                return {StubStatus::InvalidInput, std::nullopt, "Message task identity mismatch."};
        } else if (auto *report = std::get_if<SituationReport>(&message.payload)) {
            if (report->sender != message.sender || report->recipient != message.recipient ||
                report->observed_tick > report->created_tick || report->created_tick > tick)
                return {StubStatus::InvalidInput, std::nullopt,
                        "Message report identity mismatch."};
        }
    }
    CommunicationsUpdate update;
    update.next = state;
    std::vector<size_t> order(update.next.messages.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const auto &x = update.next.messages[a];
        const auto &y = update.next.messages[b];
        return x.delivery_tick != y.delivery_tick ? x.delivery_tick < y.delivery_tick : x.id < y.id;
    });
    for (auto index : order) {
        auto &message = update.next.messages[index];
        if (message.status != DeliveryStatus::Queued && message.status != DeliveryStatus::InTransit)
            continue;
        if (message.expiry_tick && *message.expiry_tick <= tick) {
            message.status = DeliveryStatus::Expired;
            continue;
        }
        if (message.delivery_tick > tick ||
            (message.channel == DeliveryChannel::Courier && !message.courier))
            continue;
        message.status = DeliveryStatus::Delivered;
        update.delivered.push_back(message);
    }
    return {StubStatus::Ready, std::move(update), "Due messages advanced once."};
}
StubResult<RegimentTaskTranslation> TranslateRegimentTask(const CommandTask &task,
                                                          RegimentId regiment,
                                                          battle::FormationSpec current,
                                                          Tick tick) {
    if (!detail::ValidTask(task) || !regiment || !current.columns ||
        !std::isfinite(current.spacing_m) || current.spacing_m <= 0 ||
        !std::isfinite(current.body_radius_m) || current.body_radius_m <= 0)
        return {StubStatus::InvalidInput, std::nullopt, "Invalid regiment translation input."};
    RegimentTaskTranslation result;
    auto &order = result.order;
    order.issuer = task.author;
    order.recipient = regiment;
    order.priority = task.priority;
    order.point = task.intent.objective_position;
    order.target_regiment = task.intent.target_regiment;
    order.created_tick = tick;
    order.delivery_tick = tick;
    order.expiry_tick = detail::TaskExpiry(task, task.received_tick.value_or(tick)).value_or(0);
    switch (task.action) {
    case TaskKind::HoldArea:
    case TaskKind::Defend:
    case TaskKind::Reserve:
        order.type = OrderType::Hold;
        break;
    case TaskKind::MoveToArea:
    case TaskKind::Screen:
    case TaskKind::Support:
    case TaskKind::Flank:
        order.type = OrderType::MoveTo;
        break;
    case TaskKind::Attack:
    case TaskKind::Exploit:
        if (!task.intent.target_regiment)
            return {StubStatus::InvalidInput, std::nullopt,
                    "Attack translation needs an assigned regiment target."};
        order.type = OrderType::Attack;
        break;
    case TaskKind::Withdraw:
        order.type = OrderType::Withdraw;
        break;
    case TaskKind::Rally:
        order.type = OrderType::Rally;
        break;
    case TaskKind::Reform: {
        if (!task.formation_columns)
            return {StubStatus::InvalidInput, std::nullopt,
                    "Reform translation needs an explicit column count."};
        FormationSpec formation = current;
        formation.columns = *task.formation_columns;
        if (task.formation_spacing_m)
            formation.spacing_m = *task.formation_spacing_m;
        if (!std::isfinite(formation.spacing_m) || formation.spacing_m <= 0)
            return {StubStatus::InvalidInput, std::nullopt,
                    "Reform translation needs positive spacing."};
        result.formation = formation;
        order.type = OrderType::Hold;
        break;
    }
    }
    order.status = OrderStatus::Pending;
    return {StubStatus::Ready, std::move(result), "Regiment task translated."};
}
} // namespace battle::command
