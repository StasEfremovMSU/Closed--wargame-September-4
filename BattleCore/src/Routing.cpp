#include "Routing.h"
#include "LocalMelee.h"
#include "battle/Formation.h"

namespace battle {
namespace {
constexpr float WaypointTolerance = .07f;
constexpr uint32_t RoutLookahead = 4;

Vec3 Along(Vec3 from, Vec3 to, float distance) {
    const auto difference = to - from;
    const float length = Length(difference);
    return length <= distance ? to : from + difference * (distance / length);
}

bool RoutSegmentClear(const World &world, const UnitState &unit, Vec3 to, float radius) {
    const auto from = unit.pose.position;
    return UnitTerrainLimit(world, from, to, radius) >= 1.f &&
        (!world.scenario.hill || HillSegmentPassable(*world.scenario.hill, from, to, unit.troop));
}

std::optional<Vec3> RoutGoal(const World &world, const UnitState &unit,
                            const RegimentState &regiment, uint32_t index, float radius) {
    const auto &knot = regiment.route[index];
    const auto columns = regiment.formation.columns;
    if (columns > 1) {
        const auto file = unit.slot_index % columns;
        const float side = file < columns / 2 ? -1.f : 1.f;
        const auto lane = knot.position + Right(knot.yaw) * (side * radius * 1.8f);
        if (RoutSegmentClear(world, unit, lane, radius))
            return lane;
    }
    if (RoutSegmentClear(world, unit, knot.position, radius))
        return knot.position;
    return std::nullopt;
}

Vec3 TakeRoutStep(const World &world, const UnitState &unit, Vec3 wanted, float speed, float seconds,
                  float radius, const LocalMelee *contact, bool yield_rout = false) {
    const auto from = unit.pose.position;
    auto to = Along(from, wanted, speed * seconds);
    if (world.scenario.hill)
        to = from + (to - from) * HillUphillSpeedMultiplier(*world.scenario.hill, from, to, unit.troop, speed);
    to = from + (to - from) * TerrainSpeedMultiplier(from, to, world.scenario.obstacles, unit.troop, speed);
    to = from + (to - from) * UnitTerrainLimit(world, from, to, radius);
    return contact ? contact->Constrain(unit, to, 0.f, 0.f, yield_rout) : to;
}

struct Runner {
    UnitState *unit;
    Vec3 goal;
    float radius;
    float distance;
    uint32_t waypoint;
};
} // namespace

std::vector<Pose> RoutWaypoints(Vec3 start, std::span<const Pose> path) {
    std::vector<Pose> route{{start, 0.f}};
    for (const auto &point : path) {
        const auto from = route.back().position;
        const auto delta = point.position - from;
        const float distance = Length(delta);
        if (distance < .001f)
            continue;
        // Keep regularly spaced entries even on direct segments, so outer files
        // can approach a gate before they can see the exit on the other side.
        const float yaw = std::atan2(delta.y, delta.x);
        const auto parts = uint32_t(std::ceil(distance / 2.f));
        for (uint32_t i = 1; i <= parts; ++i)
            route.push_back({from + delta * (float(i) / float(parts)), yaw});
    }
    if (route.size() > 1)
        route.front().yaw = route[1].yaw;
    return route;
}

void AdvanceIndividualRout(World &world, RegimentState &regiment, float seconds, const LocalMelee *contact) {
    if (regiment.route.empty())
        return;
    const auto count = uint32_t(regiment.route.size());
    const auto rally_slots = regiment.rout_to_rally
        ? BuildFormationSlots(regiment.formation, regiment.route.back(), uint32_t(regiment.slots.size()))
        : std::vector<Pose>{};
    std::vector<Runner> runners;
    runners.reserve(regiment.units.size());
    for (const auto id : regiment.units) {
        auto *unit = FindUnit(world, id);
        if (unit->control == ControlMode::PlayerControlled)
            continue;
        const auto from = unit->pose.position;
        const float radius = contact ? contact->BodyRadius(*unit) : regiment.formation.body_radius_m;
        auto &next = unit->rout_waypoint;
        const auto *rally = unit->slot_index < rally_slots.size() ? &rally_slots[unit->slot_index] : nullptr;
        if (rally && (next == count ||
            (Distance(from, regiment.route.back().position) <= 6.f &&
             RoutSegmentClear(world, *unit, rally->position, radius)))) {
            next = count;
            unit->slot = *rally;
            runners.push_back({unit, rally->position, radius, Distance(from, rally->position), count});
            continue;
        }
        if (next == count)
            continue;
        const auto until = std::min(next + RoutLookahead, count - 1);
        const auto first = next > RoutLookahead ? next - RoutLookahead : 0;
        std::optional<Vec3> goal;
        uint32_t destination_index = next;
        for (auto candidate = until + 1; candidate-- > first;)
            if (auto at = RoutGoal(world, *unit, regiment, candidate, radius)) {
                goal = at;
                destination_index = candidate;
                break;
            }
        if (!goal)
            continue;
        if (Distance(from, *goal) <= WaypointTolerance) {
            next = destination_index + 1;
            continue;
        }
        runners.push_back({unit, *goal, radius, Distance(from, *goal), destination_index});
    }
    // Advance those at the front first so behind them bodies can follow into
    // the space they have just cleared in the same simulation step.
    std::ranges::sort(runners, [](const auto &a, const auto &b) {
        if (a.waypoint != b.waypoint)
            return a.waypoint > b.waypoint;
        if (a.distance != b.distance)
            return a.distance < b.distance;
        return a.unit->id < b.unit->id;
    });
    for (const auto &runner : runners) {
        auto &unit = *runner.unit;
        const auto from = unit.pose.position;
        const float speed = std::min(unit.speed, regiment.speed) * (1.f - .35f * regiment.fatigue);
        if (unit.platform == BattlePlatform::Ballista || runner.distance < .001f || speed <= 0.f)
            continue;
        auto destination = TakeRoutStep(world, unit, runner.goal, speed, seconds, runner.radius, contact);
        if (Distance(from, destination) < .015f &&
            runner.distance > WaypointTolerance && unit.rout_waypoint < count) {
            const auto yaw = std::atan2(runner.goal.y - from.y, runner.goal.x - from.x);
            const auto forward = Forward(yaw);
            const float reach = std::min(runner.distance, speed * seconds);
            for (const float offset : {.85f, -.85f}) {
                const auto to = from + Forward(yaw + offset) * reach;
                const auto candidate = TakeRoutStep(world, unit, to, speed, seconds, runner.radius, contact);
                if (Dot(candidate - destination, forward) > .002f)
                    destination = candidate;
            }
        }
        // A crowd can jam on its own bodies at a one-file opening. Yield only
        // after ordinary movement cannot make progress, and keep the terrain,
        // player, opposing and platform constraints for this same short step.
        if (contact && Distance(from, destination) < .015f)
            destination = TakeRoutStep(world, unit, runner.goal, speed, seconds, runner.radius, contact, true);
        unit.pose.position = destination;
        const auto delta = destination - from;
        if (Length(delta) > .002f) {
            unit.pose.yaw = std::atan2(delta.y, delta.x);
            if (Distance(destination, runner.goal) + .002f < runner.distance)
                regiment.rout_progress_tick = world.tick + 1;
        }
        if (runner.waypoint < count && Distance(destination, runner.goal) <= WaypointTolerance)
            unit.rout_waypoint = runner.waypoint + 1;
    }
    Vec3 center{};
    uint32_t living = 0;
    for (const auto id : regiment.units) {
        const auto &unit = *FindUnit(world, id);
        if (unit.control == ControlMode::PlayerControlled || (!regiment.rout_to_rally && unit.rout_waypoint == count))
            continue;
        center = center + unit.pose.position;
        ++living;
    }
    if (living) {
        const auto next = center * (1.f / float(living));
        const auto delta = next - regiment.pose.position;
        regiment.pose.position = next;
        if (Length(delta) > .002f)
            regiment.pose.yaw = std::atan2(delta.y, delta.x);
    } else {
        regiment.pose = regiment.route.back();
    }
}
} // namespace battle
