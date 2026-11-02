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
    auto *r = FindRegiment(w, u->regiment);
    return r && r->team == n.team && (n.level != Level::Regiment || r->id == n.regiment);
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
        } else {
            auto *p = FindNode(h, n.parent);
            if (!p || unsigned(p->level) + 1 != unsigned(n.level) || p->team != n.team ||
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
            auto *r = u ? FindRegiment(w, u->regiment) : nullptr;
            if (!commanders.insert(n.commander.value).second || !o ||
                (o->role != 1 && o->role != 2) || !r || r->team != n.team ||
                (n.level == Level::Regiment && r->id != n.regiment) || !FindProfile(h, n.commander))
                issue("Invalid or duplicate commander.");
        }
        if (auto *p = FindProfile(h, n.commander);
            p && p->command_capacity && n.children.size() > *p->command_capacity)
            issue("Direct subordinate capacity exceeded.");
        std::set<uint64_t> successors;
        for (auto candidate : n.succession) {
            auto *o = Officer(w, candidate);
            auto *u = o ? FindUnit(w, o->unit) : nullptr;
            auto *r = u ? FindRegiment(w, u->regiment) : nullptr;
            if (!candidate || !successors.insert(candidate.value).second || !o ||
                (o->role != 1 && o->role != 2) || !r || r->team != n.team ||
                (n.level == Level::Regiment && r->id != n.regiment))
                issue("Invalid successor.");
        }
    }
    for (const auto &a : h.support) {
        auto *n = FindNode(h, a.command);
        auto *o = Officer(w, a.officer);
        auto *u = o ? FindUnit(w, o->unit) : nullptr;
        auto *r = u ? FindRegiment(w, u->regiment) : nullptr;
        if (!n || !r || r->team != n->team || unsigned(a.duty) > unsigned(SupportDuty::Musician))
            issue("Invalid support assignment.");
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
StubResult<CommunicationsUpdate> AdvanceCommunications(const CommunicationsState &, Tick) {
    return {StubStatus::NotImplemented, std::nullopt, "Transport follows in the runtime task."};
}
StubResult<battle::Order> TranslateRegimentTask(const CommandTask &, Tick) {
    return {StubStatus::NotImplemented, std::nullopt, "Translation follows in the runtime task."};
}
} // namespace battle::command