#include "battle/World.h"
#include "battle/command/Services.h"
#include <set>
namespace battle::command {
const CommandNode *FindNode(const CommandHierarchy &h, NodeId id) {
    auto i =
        std::find_if(h.nodes.begin(), h.nodes.end(), [&](const auto &n) { return n.id == id; });
    return i == h.nodes.end() ? nullptr : &*i;
}
CommandNode *FindNode(CommandHierarchy &h, NodeId id) {
    auto i =
        std::find_if(h.nodes.begin(), h.nodes.end(), [&](const auto &n) { return n.id == id; });
    return i == h.nodes.end() ? nullptr : &*i;
}
const OfficerProfile *FindProfile(const CommandHierarchy &h, OfficerId id) {
    auto i = std::find_if(h.officers.begin(), h.officers.end(),
                          [&](const auto &p) { return p.officer == id; });
    return i == h.officers.end() ? nullptr : &*i;
}
std::vector<RegimentId> OwnedRegiments(const CommandHierarchy &h, NodeId root) {
    std::vector<RegimentId> result;
    std::vector<NodeId> todo{root};
    std::set<uint64_t> seen;
    while (!todo.empty()) {
        auto id = todo.back();
        todo.pop_back();
        if (!seen.insert(id.value).second)
            continue;
        auto *n = FindNode(h, id);
        if (!n)
            continue;
        if (n->level == Level::Regiment && n->regiment)
            result.push_back(n->regiment);
        todo.insert(todo.end(), n->children.begin(), n->children.end());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
std::optional<Pose> NodePose(const World &w, const CommandHierarchy &h, NodeId id) {
    auto *node = FindNode(h, id);
    if (!node)
        return {};
    for (const auto &o : w.officers)
        if (o.id == node->commander) {
            auto *u = FindUnit(w, o.unit);
            if (u && u->alive)
                return u->pose;
        }
    Vec3 centre{};
    float count = 0;
    float yaw = 0;
    for (auto reg : OwnedRegiments(h, id))
        if (auto *r = FindRegiment(w, reg)) {
            for (auto unit : r->units)
                if (auto *u = FindUnit(w, unit); u && u->alive) {
                    centre = centre + r->pose.position;
                    yaw = r->pose.yaw;
                    ++count;
                }
        }
    return count ? std::optional<Pose>{{centre * (1 / count), yaw}} : std::nullopt;
}
bool HasLineOfSight(Vec3 a, Vec3 b, std::span<const Obstacle> obstacles) {
    if (!Finite(a) || !Finite(b))
        return false;
    for (const auto &box : obstacles) {
        float enter = 0, leave = 1;
        bool intersect = true;
        for (unsigned axis = 0; axis < 2; ++axis) {
            float start = axis ? a.y : a.x, delta = axis ? b.y - a.y : b.x - a.x;
            float low = axis ? box.min_y : box.min_x, high = axis ? box.max_y : box.max_x;
            if (std::abs(delta) < 1e-7f) {
                if (start < low || start > high)
                    intersect = false;
            } else {
                float x = (low - start) / delta, y = (high - start) / delta;
                if (x > y)
                    std::swap(x, y);
                enter = std::max(enter, x);
                leave = std::min(leave, y);
            }
        }
        if (intersect && enter <= leave && leave >= 0 && enter <= 1)
            return false;
    }
    return true;
}
StubResult<LocalSituation> BuildSituation(const World &w, const CommandHierarchy &h, NodeId id,
                                          Tick tick) {
    if (ValidateHierarchy(h, w).status != StubStatus::Ready)
        return {StubStatus::InvalidInput, std::nullopt, "Invalid hierarchy."};
    const auto *node = FindNode(h, id);
    if (!node)
        return {StubStatus::InvalidInput, std::nullopt, "Unknown observer."};
    LocalSituation p;
    p.observer = id;
    p.tick = tick;
    float weight = 0;
    uint64_t panicked = 0;
    for (auto reg : OwnedRegiments(h, id))
        if (auto *r = FindRegiment(w, reg)) {
            uint32_t living = 0;
            for (auto unit : r->units)
                if (auto *u = FindUnit(w, unit); u && u->alive)
                    ++living;
            p.own.living += living;
            p.own.initial += uint32_t(r->initial_count);
            if (r->panic)
                panicked += living;
            p.own.pose.position = p.own.pose.position + r->pose.position * float(living);
            p.rally_point = p.rally_point + r->rally_point * float(living);
            if (!weight)
                p.own.pose.yaw = r->pose.yaw;
            weight += float(living);
        }
    if (weight) {
        p.own.pose.position = p.own.pose.position * (1 / weight);
        p.rally_point = p.rally_point * (1 / weight);
    }
    p.own.panic = panicked * 2 > p.own.living;
    auto eye = NodePose(w, h, id);
    float radius = 60;
    if (auto *profile = FindProfile(h, node->commander); profile && profile->command_radius_m)
        radius = *profile->command_radius_m;
    if (eye && node->commander)
        for (const auto &r : w.regiments) {
            if (r.team == node->team)
                continue;
            uint32_t strength = 0;
            for (auto unit : r.units)
                if (auto *u = FindUnit(w, unit); u && u->alive)
                    ++strength;
            if (!strength || Distance(eye->position, r.pose.position) > radius ||
                !HasLineOfSight(eye->position, r.pose.position, w.scenario.obstacles))
                continue;
            KnownContact contact;
            contact.regiment = r.id;
            contact.team = r.team;
            contact.allegiance = Allegiance::Enemy;
            contact.estimated_pose = r.pose;
            contact.estimated_strength = float(strength);
            contact.observed_tick = tick;
            contact.confidence = 1;
            p.contacts.push_back(contact);
            const auto delta = r.pose.position - p.own.pose.position;
            float front = Dot(delta, Forward(p.own.pose.yaw)),
                  side = Dot(delta, Right(p.own.pose.yaw));
            ThreatEstimate threat;
            threat.sector = std::abs(front) >= std::abs(side)
                                ? (front >= 0 ? RelativeSector::Front : RelativeSector::Rear)
                                : (side >= 0 ? RelativeSector::Right : RelativeSector::Left);
            threat.strength = float(strength);
            threat.confidence = 1;
            p.threats.push_back(threat);
        }
    std::sort(p.contacts.begin(), p.contacts.end(), [&](const auto &a, const auto &b) {
        float x = Distance(p.own.pose.position, a.estimated_pose.position);
        float y = Distance(p.own.pose.position, b.estimated_pose.position);
        return x != y ? x < y : a.regiment < b.regiment;
    });
    TerrainAssessment terrain;
    terrain.position = p.own.pose.position;
    terrain.passability = 1;
    for (const auto &b : w.scenario.obstacles)
        if (terrain.position.x >= b.min_x && terrain.position.x <= b.max_x &&
            terrain.position.y >= b.min_y && terrain.position.y <= b.max_y)
            terrain.passability = 0;
    p.terrain.push_back(terrain);
    return {StubStatus::Ready, std::move(p), "Observed visible regiment centres."};
}
} // namespace battle::command
