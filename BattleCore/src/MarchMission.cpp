#include "battle/MarchMission.h"
#include "battle/Navigation.h"
#include "HistoricalTerrainProfiles.h"
#include <limits>
#include <utility>

namespace battle {
namespace {
constexpr int32_t PlayerTeam = 0;
constexpr float StagingX = -17.f;
constexpr float SightingDistance = 28.f;
constexpr float EncounterDistance = 12.f;
constexpr double FrameSeconds = .05;
constexpr double FrameTolerance = 1e-8;

float DeploymentDuration(const MarchRegiment &regiment) {
    const float files = static_cast<float>(std::min(regiment.formation.columns, 12u));
    const float mounted = regiment.troop == UnitTroop::Mounted ? .45f : 0.f;
    return .65f + .15f * files + mounted;
}

float HalfFrontage(const MarchRegiment &regiment, bool regiment_tactics) {
    if (!regiment_tactics)
        return 0.f;
    const auto formation = regiment.fixed_frontage || regiment.posture == MarchPosture::BattleLine
                               ? regiment.formation
                               : TravelColumnFormation(regiment.formation);
    return static_cast<float>(formation.columns - 1) * formation.spacing_m * .5f +
           formation.body_radius_m;
}

struct CrossingStrip {
    float half_width;
    std::span<const MarchCrossing> crossings;
    bool blocks_projectiles{};
};

constexpr CrossingStrip River{MarchRiverHalfWidth, MarchCrossings};
constexpr std::array<MarchCrossing, 3> WallOpenings{{{-24, 5, false}, {0, 5, false}, {24, 5, false}}};
constexpr std::array<MarchCrossing, 1> StirlingOpening{{{0, 4, false}}};
constexpr CrossingStrip StoneWalls{.75f, WallOpenings, true};
constexpr CrossingStrip Stirling{MarchRiverHalfWidth, StirlingOpening};
constexpr std::array<MarchCrossing, 3> NagashinoOpenings{{{-65.f, 17.f, true},
                                                         {  0.f, 19.f, true},
                                                         { 65.f, 17.f, true}}};
constexpr CrossingStrip NagashinoStream{MarchRiverHalfWidth, NagashinoOpenings};
constexpr std::array<MarchCrossing, 3> BattlefieldFords{{{-70.f, 23.f, true},
                                                         {  0.f, 25.f, true},
                                                         { 70.f, 23.f, true}}};
constexpr CrossingStrip BattlefieldStream{MarchRiverHalfWidth, BattlefieldFords};

constexpr std::array<MarchCrossing, 3> GranicusFords{{{-78.f, 18.f, true},
                                                       {  0.f, 25.f, true},
                                                       { 78.f, 16.f, true}}};
constexpr CrossingStrip GranicusRiver{MarchRiverHalfWidth, GranicusFords};

const CrossingStrip *StripFor(MarchTerrain terrain) {
    if (terrain == MarchTerrain::Granicus)
        return &GranicusRiver;
    if (historical::ReliefFor(terrain))
        return nullptr;
    switch (terrain) {
    case MarchTerrain::OpenField:
    case MarchTerrain::Cannae:
    case MarchTerrain::Peipus:
    case MarchTerrain::Hill:
    case MarchTerrain::Waterloo:
    case MarchTerrain::Sekigahara:
    case MarchTerrain::Agincourt:
    case MarchTerrain::Pavia:
    case MarchTerrain::Poltava:
    case MarchTerrain::Gaugamela:
    case MarchTerrain::Zama:
    case MarchTerrain::Grunwald:
    case MarchTerrain::Crecy:
    case MarchTerrain::Breitenfeld:
    case MarchTerrain::Lutzen:
    case MarchTerrain::Narva:
    case MarchTerrain::Austerlitz:
    case MarchTerrain::Borodino:
        return nullptr;
    case MarchTerrain::StoneWalls:
        return &StoneWalls;
    case MarchTerrain::River:
        return &River;
    case MarchTerrain::StirlingBridge:
        return &Stirling;
    case MarchTerrain::Nagashino:
        return &NagashinoStream;
    case MarchTerrain::Kalka:
    case MarchTerrain::Dorylaeum:
    case MarchTerrain::Lesnaya:
    case MarchTerrain::Kawanakajima:
        return &BattlefieldStream;
    default:
        return &River;
    }
}

bool WithinCrossing(const MarchCrossing &crossing, float y, float half_frontage) {
    return std::abs(y - crossing.y) + half_frontage < crossing.half_width;
}

bool ValidField(MarchField field) {
    return field.width_m > 0 && field.height_m > 0;
}

bool Walkable(Vec3 p, const CrossingStrip *strip, MarchField field, float half_frontage = 0.f) {
    return ValidField(field) && Finite(p) &&
           std::abs(p.x) <= field.HalfWidth() && std::abs(p.y) <= field.HalfHeight() &&
           (!strip || std::abs(p.x) > strip->half_width ||
            std::ranges::any_of(strip->crossings, [&](const auto &crossing) {
                return WithinCrossing(crossing, p.y, half_frontage);
            }));
}

FormationFootprint MarchFootprint(const MarchRegiment &regiment, bool regiment_tactics) {
    if (!regiment_tactics)
        return {.02f, .02f};
    const auto f = regiment.ActiveFormation();
    const uint32_t columns = std::max(1u, f.columns);
    const uint32_t figures = std::max(1u, regiment.figure_count);
    const uint32_t rows = 1 + (figures - 1) / columns;
    const float padding = 2.f * f.body_radius_m;
    return {(std::min(columns, figures) - 1) * f.spacing_m + padding,
            (rows - 1) * f.spacing_m + padding};
}

bool ClearSegment(Vec3 a, Vec3 b, const CrossingStrip *strip, float half_frontage = 0.f) {
    if (!strip || std::min(a.x, b.x) > strip->half_width ||
        std::max(a.x, b.x) < -strip->half_width)
        return true;

    // Both endpoints of the line clipped to the obstacle strip must lie in one opening.
    if (a.x > b.x)
        std::swap(a, b);
    float entry_y = a.y, exit_y = b.y;
    if (a.x < -strip->half_width)
        entry_y = std::lerp(a.y, b.y, (-strip->half_width - a.x) / (b.x - a.x));
    if (b.x > strip->half_width)
        exit_y = std::lerp(a.y, b.y, (strip->half_width - a.x) / (b.x - a.x));
    return std::ranges::any_of(strip->crossings, [&](const auto &crossing) {
        return WithinCrossing(crossing, entry_y, half_frontage) &&
               WithinCrossing(crossing, exit_y, half_frontage);
    });
}

Pose Facing(Vec3 from, Vec3 to) {
    return {to, std::atan2(to.y - from.y, to.x - from.x)};
}

std::optional<std::vector<Pose>> PlanRoute(Vec3 start, Vec3 goal,
                                           const CrossingStrip *strip, MarchField field, float half_frontage) {
    if (!Walkable(start, strip, field, half_frontage) || !Walkable(goal, strip, field, half_frontage))
        return std::nullopt;
    if (start == goal)
        return std::vector<Pose>{};
    if (ClearSegment(start, goal, strip, half_frontage))
        return std::vector<Pose>{Facing(start, goal)};

    // Connect both sides of every opening. The same openings define tactical collision.
    constexpr std::size_t start_node = 0, goal_node = 1;
    constexpr std::size_t max_nodes = 2 + 2 * MarchCrossings.size();
    std::size_t node_count = 2;
    const float bank_x = std::min(strip->half_width + 2.f, field.HalfWidth());
    std::array<Vec3, max_nodes> positions{start, goal};
    for (const auto &crossing : strip->crossings) {
        const float y = std::clamp(crossing.y, -field.HalfHeight(), field.HalfHeight());
        if (!WithinCrossing(crossing, y, half_frontage))
            continue;
        positions[node_count++] = {-bank_x, y, 0};
        positions[node_count++] = {bank_x, y, 0};
    }

    std::array<float, max_nodes> costs;
    costs.fill(std::numeric_limits<float>::infinity());
    costs[start_node] = 0;
    std::array<std::size_t, max_nodes> predecessors{};
    std::array<bool, max_nodes> visited{};
    while (true) {
        std::size_t current = node_count;
        float best_cost = std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < node_count; ++i) {
            if (!visited[i] && costs[i] < best_cost) {
                current = i;
                best_cost = costs[i];
            }
        }
        if (current == node_count)
            return std::nullopt;
        if (current == goal_node)
            break;

        visited[current] = true;
        for (std::size_t i = 0; i < node_count; ++i) {
            if (visited[i] || !ClearSegment(positions[current], positions[i], strip, half_frontage))
                continue;
            const float cost = best_cost + Distance(positions[current], positions[i]);
            if (cost < costs[i]) {
                costs[i] = cost;
                predecessors[i] = current;
            }
        }
    }

    std::vector<Pose> route;
    for (auto node = goal_node; node != start_node; node = predecessors[node]) {
        const auto &from = positions[predecessors[node]];
        if (from != positions[node])
            route.push_back(Facing(from, positions[node]));
    }
    std::ranges::reverse(route);
    return route;
}

std::optional<std::vector<Pose>> RouteFor(const MarchRegiment &regiment, Vec3 goal,
                                          MarchTerrain terrain, MarchField field,
                                          bool regiment_tactics) {
    if (!Finite(goal))
        return std::nullopt;
    goal.z = 0;
    const auto hill = MarchHill(terrain, field);
    if (MarchUsesDetailedTerrain(terrain)) {
        if (!ValidField(field)) return std::nullopt;
        const auto size = MarchFootprint(regiment, regiment_tactics);
        const float cell = std::max(2.f, std::max(field.width_m, field.height_m) / 256.f);
        const GridNavigationService grid(MarchObstacles(terrain, field), cell,
            uint32_t(std::ceil(field.width_m / cell)) + 1,
            uint32_t(std::ceil(field.height_m / cell)) + 1, {-field.HalfWidth(), -field.HalfHeight(), 0});
        PathRequest request{regiment.pose, goal, size};
        request.segment_clear = [&](Pose a, Pose b) {
            const auto bounds = TerrainFieldInset({field.HalfWidth(), field.HalfHeight()}, b.yaw,
                                                   .5f * size.depth_m, .5f * size.width_m);
            return std::abs(b.position.x) <= bounds.x && std::abs(b.position.y) <= bounds.y &&
                   (!hill || HillSegmentPassable(*hill, a.position, b.position, regiment.troop));
        };
        auto path = grid.FindPath(request);
        return path.status == Status::Ok ? std::optional{std::move(path.waypoints)} : std::nullopt;
    }
    if (hill && !HillSegmentPassable(*hill, regiment.pose.position, goal, regiment.troop)) {
        if (!HillSegmentPassable(*hill, goal, goal, regiment.troop))
            return std::nullopt;
        const float cell = std::max(1.f, std::max(field.width_m, field.height_m) / 400.f);
        const GridNavigationService grid({}, cell, uint32_t(std::ceil(field.width_m / cell)) + 1,
            uint32_t(std::ceil(field.height_m / cell)) + 1, {-field.HalfWidth(), -field.HalfHeight(), 0});
        const float diameter = std::max(.01f, 2.f * regiment.formation.body_radius_m);
        PathRequest request{regiment.pose, goal, {diameter, diameter}};
        request.preserve_facing = true;
        request.segment_clear = [&](Pose a, Pose b) {
            return Walkable(b.position, nullptr, field) && HillSegmentPassable(*hill, a.position, b.position, regiment.troop);
        };
        auto path = grid.FindPath(request);
        if (path.status != Status::Ok) return std::nullopt;
        auto previous = regiment.pose.position;
        for (auto &point : path.waypoints) {
            point = Facing(previous, point.position);
            previous = point.position;
        }
        return path.waypoints;
    }
    return PlanRoute(regiment.pose.position, goal, StripFor(terrain), field,
                     HalfFrontage(regiment, regiment_tactics));
}

bool AssignRoute(MarchRegiment &regiment, Vec3 goal, MarchTerrain terrain,
                 MarchField field, bool regiment_tactics) {
    auto route = RouteFor(regiment, goal, terrain, field, regiment_tactics);
    if (!route)
        return false;
    goal.z = 0;
    regiment.waypoint = goal;
    regiment.route = std::move(*route);
    regiment.next_waypoint = 0;
    return true;
}

Vec3 DefaultWaypoint(const MarchRegiment &regiment, const CrossingStrip *strip, MarchField field) {
    const float x = std::max(StagingX, -field.HalfWidth());
    const float y = regiment.pose.position.y;
    if (regiment.team == PlayerTeam || !strip)
        return {x, y, 0};
    const auto nearest = std::ranges::min_element(
        strip->crossings, {}, [&](const auto &crossing) { return std::abs(y - crossing.y); });
    return {x, std::clamp(nearest->y, -field.HalfHeight(), field.HalfHeight()), 0};
}

void Advance(MarchRegiment &regiment, bool regiment_tactics, const std::optional<TerrainHill> &hill,
             std::span<const Obstacle> terrain) {
    constexpr float seconds = static_cast<float>(FrameSeconds);
    if (regiment.deployment_seconds > 0.f) {
        const float remaining = regiment.deployment_seconds - seconds;
        regiment.deployment_seconds = remaining > 1e-6f ? remaining : 0.f;
        return;
    }
    const bool column = regiment.posture == MarchPosture::TravelColumn;
    const bool mounted = regiment.troop == UnitTroop::Mounted;
    float speed = regiment.team == PlayerTeam ? 7.f : 8.f;
    if (regiment_tactics) {
        speed = column ? 7.f : 5.f;
        if (mounted)
            speed += column ? 2.f : 1.f;
        speed *= 1.f - .25f * regiment.fatigue;
    }
    const float distance_budget = speed * seconds;
    float budget = distance_budget;
    float travelled = 0.f;
    while (budget > 0 && regiment.next_waypoint < regiment.route.size()) {
        const Pose target = regiment.route[regiment.next_waypoint];
        const Vec3 remaining = target.position - regiment.pose.position;
        const float distance = Length(remaining);
        const Vec3 estimated = distance > 0.f ? regiment.pose.position +
            remaining * (std::min(distance, budget) / distance) : target.position;
        const float terrain_speed = TerrainSpeedMultiplier(regiment.pose.position, estimated, terrain,
                                                            regiment.troop, speed);
        const float multiplier = hill ? terrain_speed *
            HillUphillSpeedMultiplier(*hill, regiment.pose.position, estimated, regiment.troop, speed) : terrain_speed;
        if (multiplier <= 0.f) break;
        const float advance = budget * multiplier;
        if (distance > advance) {
            regiment.pose = {regiment.pose.position + remaining * (advance / distance), target.yaw};
            travelled += advance;
            budget = 0.f;
            break;
        }
        regiment.pose = target;
        budget -= distance / multiplier;
        travelled += distance;
        ++regiment.next_waypoint;
    }
    if (regiment_tactics) {
        const float march_effort = column ? .008f : .0035f;
        regiment.fatigue = std::min(1.f, regiment.fatigue + travelled * march_effort);
    }
}

float MarchClearance(const MarchRegiment &regiment, bool regiment_tactics) {
    return regiment_tactics ? regiment.formation.body_radius_m : 0.f;
}

bool OccupiesCrossing(const MarchRegiment &regiment, const Obstacle &crossing, bool regiment_tactics) {
    const float radius = MarchClearance(regiment, regiment_tactics);
    return TerrainAreaOverlaps(crossing, {regiment.pose.position, 0.f}, radius, radius);
}

void SpaceApproachingRegiments(std::span<const MarchRegiment> previous, std::span<MarchRegiment> next,
                                const Obstacle &crossing) {
    // Distances refer to the march view's abstract regiment markers.
    constexpr float QueueGap = 3.f;
    for (const float side : {-1.f, 1.f}) {
        const float edge = side < 0.f ? crossing.min_x : crossing.max_x;
        const auto distance = [&](const MarchRegiment &regiment) {
            return side * (regiment.pose.position.x - edge);
        };
        std::vector<std::size_t> approach;
        for (std::size_t index = 0; index < previous.size(); ++index) {
            const auto &regiment = previous[index];
            if (regiment.crossing_order) continue; // Staff orders govern the order of approach.
            const float bank_distance = distance(regiment);
            const float y = regiment.pose.position.y;
            if (regiment.waypoint && side * (regiment.waypoint->x - edge) < 0.f &&
                bank_distance >= 0.f && y > crossing.min_y && y < crossing.max_y)
                approach.push_back(index);
        }
        std::ranges::sort(approach, [&](std::size_t a, std::size_t b) {
            const float first = distance(previous[a]), second = distance(previous[b]);
            return first != second ? first < second : previous[a].id < previous[b].id;
        });
        for (std::size_t index = 1; index < approach.size(); ++index) {
            const auto leader = approach[index - 1], follower = approach[index];
            if (distance(next[follower]) < distance(previous[follower]) &&
                distance(next[follower]) - distance(next[leader]) < QueueGap)
                next[follower] = previous[follower];
        }
    }
}

void AdmitCrossing(std::span<const MarchRegiment> previous, std::span<MarchRegiment> next,
                   const Obstacle &crossing, bool regiment_tactics) {
    // Reserve the span for the regiment already using it. Break simultaneous arrivals by
    // distance to the portal, then stable regiment ID, independent of input order.
    const auto bank_distance = [&](const MarchRegiment &regiment) {
        const float x = regiment.pose.position.x;
        return std::abs(x - std::clamp(x, crossing.min_x, crossing.max_x));
    };
    std::optional<std::size_t> admitted;
    for (std::size_t index = 0; index < previous.size(); ++index) {
        if (OccupiesCrossing(previous[index], crossing, regiment_tactics)) {
            admitted = index;
            break;
        }
        if (!OccupiesCrossing(next[index], crossing, regiment_tactics))
            continue;
        if (!admitted || bank_distance(previous[index]) < bank_distance(previous[*admitted]) ||
            (bank_distance(previous[index]) == bank_distance(previous[*admitted]) &&
             previous[index].id < previous[*admitted].id))
            admitted = index;
    }
    if (!admitted)
        return;
    for (std::size_t index = 0; index < previous.size(); ++index) {
        if (index != *admitted && !OccupiesCrossing(previous[index], crossing, regiment_tactics) &&
            OccupiesCrossing(next[index], crossing, regiment_tactics))
            next[index] = previous[index];
    }
}
} // namespace

FormationSpec TravelColumnFormation(FormationSpec battle_line) {
    battle_line.columns = std::max(1u, battle_line.columns / 2 + battle_line.columns % 2);
    battle_line.shape = FormationShape::Rectangle;
    battle_line.slot_stagger = 0.f;
    const float compact_spacing = std::max(2.f * battle_line.body_radius_m,
                                           .8f * battle_line.spacing_m);
    battle_line.spacing_m = std::min(battle_line.spacing_m, compact_spacing);
    return battle_line;
}

FormationSpec MarchRegiment::ActiveFormation() const {
    return !fixed_frontage && (posture == MarchPosture::TravelColumn || deployment_seconds > 0.f)
               ? TravelColumnFormation(formation)
               : formation;
}

float MarchRegiment::DeploymentSeconds() const {
    if (fixed_frontage)
        return 0.f;
    return posture == MarchPosture::TravelColumn ? DeploymentDuration(*this) : deployment_seconds;
}

bool MarchStartWalkable(const MarchRegiment &regiment, MarchTerrain terrain, bool regiment_tactics, MarchField field) {
    const auto hill = MarchHill(terrain, field);
    if (!Finite(regiment.pose) || !ValidField(field) ||
        (hill && !HillSegmentPassable(*hill, regiment.pose.position, regiment.pose.position, regiment.troop)))
        return false;
    if (!MarchUsesDetailedTerrain(terrain))
        return Walkable(regiment.pose.position, StripFor(terrain), field, HalfFrontage(regiment, regiment_tactics));

    const auto size = MarchFootprint(regiment, regiment_tactics);
    const auto bounds = TerrainFieldInset({field.HalfWidth(), field.HalfHeight()}, regiment.pose.yaw,
                                          .5f * size.depth_m, .5f * size.width_m);
    if (std::abs(regiment.pose.position.x) > bounds.x || std::abs(regiment.pose.position.y) > bounds.y)
        return false;
    return std::ranges::none_of(MarchObstacles(terrain, field), [&](const auto &area) {
        return TerrainAreaOverlaps(area, regiment.pose, .5f * size.depth_m, .5f * size.width_m);
    });
}

bool MarchUsesDetailedTerrain(MarchTerrain terrain) {
    if (historical::ReliefFor(terrain))
        return true;
    switch (terrain) {
    case MarchTerrain::Waterloo:
    case MarchTerrain::Sekigahara:
    case MarchTerrain::Agincourt:
    case MarchTerrain::Pavia:
    case MarchTerrain::Poltava:
    case MarchTerrain::Nagashino:
    case MarchTerrain::Gaugamela:
    case MarchTerrain::Zama:
    case MarchTerrain::Kalka:
    case MarchTerrain::Dorylaeum:
    case MarchTerrain::Grunwald:
    case MarchTerrain::Crecy:
    case MarchTerrain::Breitenfeld:
    case MarchTerrain::Lutzen:
    case MarchTerrain::Kawanakajima:
    case MarchTerrain::Narva:
    case MarchTerrain::Lesnaya:
    case MarchTerrain::Austerlitz:
    case MarchTerrain::Borodino:
        return true;
    default:
        return false;
    }
}

bool MarchHasWaterCrossings(MarchTerrain terrain) {
    const auto *strip = StripFor(terrain);
    return strip && !strip->blocks_projectiles;
}

std::span<const MarchCrossing> MarchCrossingsFor(MarchTerrain terrain) {
    const auto *strip = StripFor(terrain);
    return strip ? strip->crossings : std::span<const MarchCrossing>{};
}

std::optional<TerrainHill> MarchHill(MarchTerrain terrain, MarchField field) {
    if (!ValidField(field)) return std::nullopt;
    if (const auto relief = historical::ReliefFor(terrain)) {
        const auto &profile = *historical::FindProfile(*relief);
        float height = 0.f;
        for (const auto &lobe : profile.heights)
            height = std::max(height, std::abs(lobe.height_m));
        return height > 0.f
            ? std::optional{TerrainHill{field.HalfWidth(), field.HalfHeight(), height, .35f, *relief}}
            : std::nullopt;
    }
    switch (terrain) {
    case MarchTerrain::Hill:
        return TerrainHill{};
    case MarchTerrain::Waterloo:
        return TerrainHill{field.HalfWidth(), field.HalfHeight(), 9.f, .35f, TerrainRelief::Waterloo};
    case MarchTerrain::Sekigahara:
        return TerrainHill{field.HalfWidth(), field.HalfHeight(), 23.f, .35f, TerrainRelief::Sekigahara};
    case MarchTerrain::Zama:
        return TerrainHill{.37f * field.HalfWidth(), .90f * field.HalfHeight(), 2.4f, .26f};
    case MarchTerrain::Grunwald:
        return TerrainHill{.67f * field.HalfWidth(), .81f * field.HalfHeight(), 4.2f, .40f};
    case MarchTerrain::Lutzen:
        return TerrainHill{.44f * field.HalfWidth(), .33f * field.HalfHeight(), 5.5f, .47f};
    case MarchTerrain::Borodino:
        return TerrainHill{.59f * field.HalfWidth(), .47f * field.HalfHeight(), 7.f, .43f};
    default:
        return std::nullopt;
    }
}

namespace {
void AppendHistoricalAreas(std::vector<Obstacle> &areas, MarchTerrain terrain, MarchField field) {
    const auto add = [&](float x1, float y1, float x2, float y2, TerrainFeature feature,
                         bool blocks_projectiles = false) {
        areas.push_back({x1 * field.HalfWidth(), y1 * field.HalfHeight(),
                         x2 * field.HalfWidth(), y2 * field.HalfHeight(), blocks_projectiles, feature});
    };
    if (const auto relief = historical::ReliefFor(terrain)) {
        for (const auto &area : historical::FindProfile(*relief)->areas)
            add(area.min_x, area.min_y, area.max_x, area.max_y, area.feature, area.blocks_projectiles);
        return;
    }
    using T = TerrainFeature;
    const auto redoubt = [&](float front, float back, float low, float high) {
        const float ramp_depth = .25f * (back - front);
        const float bank_width = .075f * (high - low);
        add(front - ramp_depth, low, front, high, T::RedoubtRamp);
        add(front, low, back, low + bank_width, T::RedoubtBank, true);
        add(front, high - bank_width, back, high, T::RedoubtBank, true);
        add(front, low + bank_width, back, high - bank_width, T::RedoubtInterior);
    };
    switch (terrain) {
    case MarchTerrain::Waterloo:
        add(-1.f, -.054f, 1.f, -.035f, T::Road); // chaussée de Bruxelles
        add(.27f, -.79f, .30f, .89f, T::Road); // track along Mont-Saint-Jean
        add(-.02f, -.70f, .065f, -.52f, T::Forest);
        add(.019f, -.65f, .060f, -.58f, T::Barrier, true); // Hougoumont
        add(.108f, -.104f, .164f, -.057f, T::Barrier, true); // La Haye Sainte
        add(.315f, .732f, .374f, .823f, T::Barrier, true); // Papelotte
        add(-.83f, .47f, -.05f, .66f, T::PloughedField);
        add(-.75f, -.39f, -.11f, -.18f, T::PloughedField);
        break;
    case MarchTerrain::Sekigahara:
        add(-1.f, -.038f, 1.f, -.014f, T::Road); // Nakasendo
        add(-.34f, -.69f, -.318f, .015f, T::Road); // Kitakoku road into the valley
        add(-.85f, -.99f, -.24f, -.70f, T::Forest);
        add(-.78f, .67f, -.23f, .99f, T::Forest);
        add(.53f, -.99f, .98f, -.69f, T::Forest);
        add(.67f, .73f, .96f, .96f, T::Forest);
        add(-.11f, .30f, .40f, .55f, T::PloughedField);
        add(-.68f, -.54f, -.11f, -.30f, T::PloughedField);
        break;
    case MarchTerrain::Agincourt:
        add(-.92f, -.99f, -.16f, -.68f, T::Forest);
        add(-.16f, -.99f, .46f, -.46f, T::Forest);
        add(.46f, -.99f, .92f, -.65f, T::Forest);
        add(-.87f, .67f, -.22f, .99f, T::Forest);
        add(-.22f, .47f, .45f, .99f, T::Forest);
        add(.45f, .65f, .90f, .99f, T::Forest);
        add(-.50f, -.45f, .53f, .46f, T::PloughedField);
        break;
    case MarchTerrain::Pavia:
        add(-.126f, -.78f, -.117f, -.50f, T::StoneWall, true);
        add(-.126f, .44f, -.117f, .81f, T::StoneWall, true);
        add(-.126f, -.80f, .68f, -.786f, T::StoneWall, true);
        add(.09f, .115f, .17f, .224f, T::Barrier, true); // Mirabello manor
        add(-.80f, -.94f, -.27f, -.59f, T::Forest);
        add(.22f, .61f, .62f, .76f, T::Forest);
        add(-.11f, -.035f, .82f, -.012f, T::Road);
        break;
    case MarchTerrain::Poltava:
        for (const float x : {-.26f, -.09f, .08f, .24f})
            redoubt(x - .030f, x + .035f, -.060f, .060f);
        for (const float y : {-.27f, .27f})
            redoubt(.205f, .280f, y - .069f, y + .069f);
        add(-.86f, -.99f, -.15f, -.61f, T::Forest);
        add(.24f, -.99f, .86f, -.66f, T::Forest);
        add(-.80f, .68f, .04f, .99f, T::Forest);
        add(-.83f, -.09f, -.27f, -.064f, T::Road);
        break;
    case MarchTerrain::Nagashino:
        for (const auto y : {std::pair{-.91f, -.51f}, std::pair{-.25f, -.14f},
                             std::pair{.14f, .25f}, std::pair{.51f, .91f}})
            add(-.204f, y.first, -.195f, y.second, T::Hedge, true);
        add(-.75f, -.98f, -.31f, -.64f, T::Forest);
        add(.34f, .64f, .82f, .98f, T::Forest);
        add(.24f, -.55f, .62f, -.30f, T::PloughedField);
        break;
    case MarchTerrain::Gaugamela:
        // Keep the central lane suitable for the Persian chariots.
        add(-1.f, -.95f, -.44f, -.72f, T::PloughedField);
        add(.49f, .73f, 1.f, .96f, T::PloughedField);
        break;
    case MarchTerrain::Zama:
        add(-.51f, .69f, .20f, .95f, T::PloughedField);
        add(.17f, -.96f, .83f, -.76f, T::PloughedField);
        break;
    case MarchTerrain::Kalka:
        add(-.79f, -.89f, -.31f, -.68f, T::Marsh);
        add(.06f, .59f, .41f, .95f, T::Marsh);
        break;
    case MarchTerrain::Dorylaeum:
        add(-.80f, -.98f, -.26f, -.74f, T::Forest);
        add(.03f, -.96f, .35f, -.68f, T::Marsh);
        add(.38f, .76f, .83f, .99f, T::Forest);
        break;
    case MarchTerrain::Grunwald:
        add(-.91f, -.99f, -.18f, -.70f, T::Forest);
        add(.04f, .69f, .81f, .99f, T::Forest);
        add(-.41f, .28f, .12f, .63f, T::PloughedField);
        break;
    case MarchTerrain::Breitenfeld:
        add(-1.f, .49f, 1.f, .51f, T::Road);
        add(-.24f, -.70f, .31f, -.43f, T::PloughedField);
        add(.58f, .66f, .64f, .74f, T::Barrier, true);
        add(.71f, .76f, .76f, .82f, T::Barrier, true);
        break;
    case MarchTerrain::Lutzen:
        add(-1.f, -.17f, 1.f, -.14f, T::Road);
        add(-.66f, -.27f, -.19f, -.17f, T::Marsh);
        add(.03f, -.27f, .54f, -.17f, T::Marsh);
        add(.03f, .40f, .43f, .67f, T::PloughedField);
        break;
    case MarchTerrain::Kawanakajima:
        add(-.33f, -.90f, -.05f, -.53f, T::Marsh);
        add(.20f, .09f, .58f, .34f, T::PloughedField);
        add(.41f, -.96f, .92f, -.72f, T::Forest);
        break;
    case MarchTerrain::Narva:
        for (const auto span : {std::pair{-1.f, -.52f}, std::pair{.52f, 1.f}})
            add(-.028f, span.first, .003f, span.second, T::StoneWall, true);
        redoubt(-.041f, .052f, -.295f, -.119f);
        redoubt(-.041f, .052f, .119f, .295f);
        add(.80f, -.99f, 1.f, -.26f, T::River);
        add(.58f, .36f, .71f, .54f, T::Barrier, true);
        break;
    case MarchTerrain::Lesnaya:
        add(-.69f, -.98f, -.03f, -.62f, T::Forest);
        add(-.49f, .51f, -.05f, .99f, T::Forest);
        add(.074f, .52f, .54f, .87f, T::Forest);
        add(.45f, -.44f, .50f, -.36f, T::Barrier, true);
        add(.10f, .09f, .34f, .26f, T::Marsh);
        break;
    case MarchTerrain::Borodino:
        add(-1.f, -.99f, -.35f, -.74f, T::River);
        add(-.35f, -.99f, .27f, -.65f, T::River);
        add(.27f, -.99f, .82f, -.58f, T::River);
        redoubt(-.015f, .105f, -.15f, .15f);
        redoubt(-.125f, -.045f, -.40f, -.24f);
        redoubt(-.125f, -.045f, .24f, .40f);
        add(-.88f, .27f, .73f, .29f, T::Road);
        add(.52f, .65f, .98f, .99f, T::Forest);
        break;
    default:
        break;
    }
}

std::vector<Obstacle> StripObstacles(MarchTerrain terrain, MarchField field) {
    std::vector<Obstacle> obstacles;
    const auto *strip = StripFor(terrain);
    if (!strip || !ValidField(field))
        return obstacles;
    const float half_width = std::min(field.HalfWidth(), strip->half_width);
    const float half_height = field.HalfHeight();
    const auto add_band = [&](float start_y, float end_y) {
        if (start_y >= end_y)
            return;
        const auto feature = !strip->blocks_projectiles ? TerrainFeature::River :
            (start_y >= WallOpenings.back().y + WallOpenings.back().half_width
                ? TerrainFeature::Hedge : TerrainFeature::StoneWall);
        obstacles.push_back({-half_width, start_y, half_width, end_y, strip->blocks_projectiles,
                             feature});
    };
    float start_y = -half_height;
    for (const auto &crossing : strip->crossings) {
        const float lower = std::max(-half_height, crossing.y - crossing.half_width);
        const float upper = std::min(half_height, crossing.y + crossing.half_width);
        if (lower >= upper)
            continue;
        add_band(start_y, lower);
        start_y = upper;
    }
    add_band(start_y, half_height);
    return obstacles;
}

} // namespace

std::vector<Obstacle> MarchObstacles(MarchTerrain terrain, MarchField field) {
    auto areas = MarchTerrainAreas(terrain, field);
    std::erase_if(areas, [](const auto &area) { return !TerrainBlocksMovement(area); });
    return areas;
}

std::vector<Obstacle> MarchTerrainAreas(MarchTerrain terrain, MarchField field) {
    if (!ValidField(field)) return {};
    auto areas = StripObstacles(terrain, field);
    AppendHistoricalAreas(areas, terrain, field);
    const auto *strip = StripFor(terrain);
    if (!strip || strip->blocks_projectiles || !ValidField(field))
        return areas;
    const float half_width = std::min(field.HalfWidth(), strip->half_width);
    for (const auto &opening : strip->crossings) {
        const float bottom = std::max(-field.HalfHeight(), opening.y - opening.half_width);
        const float top = std::min(field.HalfHeight(), opening.y + opening.half_width);
        if (bottom >= top)
            continue;
        areas.push_back({-half_width, bottom, half_width, top, false,
                         opening.ford ? TerrainFeature::Ford : TerrainFeature::Bridge});
        const float road_end = std::min(field.HalfWidth(), 14.2f);
        const float road_min_y = std::max(bottom, opening.y - 2.26f);
        const float road_max_y = std::min(top, opening.y + 2.26f);
        if (road_end > half_width && road_min_y < road_max_y) {
            areas.push_back({-road_end, road_min_y, -half_width, road_max_y, false, TerrainFeature::Road});
            areas.push_back({half_width, road_min_y, road_end, road_max_y, false, TerrainFeature::Road});
        }
    }
    return areas;
}

MarchMission::MarchMission(std::vector<MarchRegiment> regiments, MarchTerrain terrain,
                           bool regiment_tactics, MarchField field)
    : regiments_(std::move(regiments)), terrain_(terrain), field_(field),
      terrain_areas_(MarchTerrainAreas(terrain, field)), regiment_tactics_(regiment_tactics) {
    const auto *strip = StripFor(terrain_);
    for (auto &regiment : regiments_) {
        auto initial_waypoint = std::exchange(regiment.waypoint, std::nullopt);
        regiment.crossing_order.reset();
        regiment.route.clear();
        regiment.next_waypoint = 0;
        if (regiment_tactics_) {
            auto &formation = regiment.formation;
            if (!formation.columns || !std::isfinite(formation.spacing_m) ||
                formation.spacing_m <= 0.f || !std::isfinite(formation.body_radius_m) ||
                formation.body_radius_m <= 0.f)
                formation = {};
            regiment.fatigue = std::isfinite(regiment.fatigue)
                                  ? std::clamp(regiment.fatigue, 0.f, 1.f)
                                  : 0.f;
            const bool deploying = regiment.posture == MarchPosture::BattleLine &&
                                   std::isfinite(regiment.deployment_seconds);
            regiment.deployment_seconds = deploying
                ? std::clamp(regiment.deployment_seconds, 0.f, DeploymentDuration(regiment))
                : 0.f;
        } else {
            regiment.posture = MarchPosture::BattleLine;
            regiment.deployment_seconds = 0.f;
            regiment.fatigue = 0.f;
        }
        if (regiment.fixed_frontage) {
            regiment.posture = MarchPosture::BattleLine;
            regiment.deployment_seconds = 0.f;
        }
        if (!MarchStartWalkable(regiment, terrain_, regiment_tactics_, field_))
            continue;
        regiment.pose.position.z = 0;
        if (!initial_waypoint || !AssignRoute(regiment, *initial_waypoint, terrain_, field_, regiment_tactics_))
            AssignRoute(regiment, DefaultWaypoint(regiment, strip, field_), terrain_, field_, regiment_tactics_);
    }
    if (regiment_tactics_ && MarchHasWaterCrossings(terrain_))
        ScheduleEnemyCrossings();
}

const std::vector<MarchRegiment> &MarchMission::Regiments() const {
    return regiments_;
}

bool MarchMission::EnemyFound() const {
    return enemy_found_;
}

const std::optional<MarchEncounter> &MarchMission::Encounter() const {
    return encounter_;
}

std::optional<std::vector<Pose>> MarchMission::PreviewWaypoint(RegimentId id, Vec3 goal) const {
    const auto regiment = std::ranges::find(regiments_, id, &MarchRegiment::id);
    if (encounter_ || regiment == regiments_.end() || regiment->team != PlayerTeam)
        return std::nullopt;
    return RouteFor(*regiment, goal, terrain_, field_, regiment_tactics_);
}

bool MarchMission::SetWaypoint(RegimentId id, Vec3 goal) {
    if (encounter_)
        return false;
    const auto regiment = std::ranges::find(regiments_, id, &MarchRegiment::id);
    if (regiment == regiments_.end() || regiment->team != PlayerTeam ||
        !AssignRoute(*regiment, goal, terrain_, field_, regiment_tactics_))
        return false;
    regiment->crossing_order.reset();
    awaiting_order_ = false;
    return true;
}

namespace {
Vec3 LineHalfSize(const MarchRegiment &regiment) {
    const auto &f = regiment.formation;
    const uint32_t rows = 1u + (std::max(1u, regiment.figure_count) - 1u) / f.columns;
    return {(rows - 1) * f.spacing_m * .5f + f.body_radius_m,
            (f.columns - 1) * f.spacing_m * .5f + f.body_radius_m, 0.f};
}

std::optional<std::size_t> CrossingOnRoute(Vec3 start, std::span<const Pose> route,
                                          std::span<const Obstacle> areas, float radius) {
    if (route.empty()) return std::nullopt;
    const auto goal = route.back().position;
    for (const auto &point : route) {
        const auto end = point.position;
        if ((start.x < 0.f) != (end.x < 0.f)) {
            const float y = std::lerp(start.y, end.y, -start.x / (end.x - start.x));
            for (std::size_t i = 0; i < areas.size(); ++i) {
                const auto &area = areas[i];
                if (TerrainIsCrossing(area) && y > area.min_y && y < area.max_y &&
                    (goal.x < area.min_x - radius || goal.x > area.max_x + radius))
                    return i;
            }
        }
        start = end;
    }
    return std::nullopt;
}
}

std::optional<std::vector<Pose>> MarchMission::PreviewCrossing(RegimentId id, Vec3 goal) const {
    const auto found = std::ranges::find(regiments_, id, &MarchRegiment::id);
    if (found == regiments_.end() || found->team != PlayerTeam) return std::nullopt;
    auto plan = PlanCrossing(*found, goal);
    return plan ? std::optional{std::move(plan->route)} : std::nullopt;
}

std::optional<MarchMission::CrossingPlan> MarchMission::PlanCrossing(const MarchRegiment &regiment, Vec3 goal) const {
    if (!regiment_tactics_ || encounter_ || !Finite(goal) ||
        !MarchHasWaterCrossings(terrain_))
        return std::nullopt;
    const auto extent = LineHalfSize(regiment);
    if (std::abs(regiment.pose.position.x) <= MarchRiverHalfWidth + regiment.formation.body_radius_m ||
        regiment.pose.position.x * goal.x >= 0.f || std::abs(goal.x) <= MarchRiverHalfWidth + extent.x + 1.f ||
        std::abs(goal.x) + extent.x > field_.HalfWidth() || std::abs(goal.y) + extent.y > field_.HalfHeight())
        return std::nullopt;
    for (const auto &other : regiments_) {
        if (other.id == regiment.id || other.team != regiment.team) continue;
        const auto size = LineHalfSize(other);
        const auto intersects = [&](Vec3 center) {
            return std::abs(center.x - goal.x) < extent.x + size.x + 1.f &&
                   std::abs(center.y - goal.y) < extent.y + size.y + 1.f;
        };
        if (intersects(other.pose.position) || (other.crossing_order && other.waypoint && intersects(*other.waypoint)))
            return std::nullopt;
    }
    auto column = regiment;
    column.posture = MarchPosture::TravelColumn;
    auto route = RouteFor(column, goal, terrain_, field_, true);
    if (!route) return std::nullopt;
    const auto area = CrossingOnRoute(column.pose.position, *route, terrain_areas_, extent.x);
    if (!area) return std::nullopt;
    return CrossingPlan{*area, std::move(*route)};
}

void MarchMission::AssignCrossing(MarchRegiment &r, CrossingPlan plan) {
    r.posture = MarchPosture::TravelColumn;
    r.deployment_seconds = 0.f;
    r.route = std::move(plan.route);
    r.waypoint = r.route.back().position;
    r.next_waypoint = 0;
    r.crossing_order = MarchRegiment::CrossingOrder{plan.area, ++crossing_sequence_, r.waypoint->x < 0.f ? -1.f : 1.f};
}

bool MarchMission::QueueCrossing(RegimentId id, Vec3 goal) {
    const auto found = std::ranges::find(regiments_, id, &MarchRegiment::id);
    if (found == regiments_.end() || found->team != PlayerTeam) return false;
    auto plan = PlanCrossing(*found, goal);
    if (!plan) return false;
    AssignCrossing(*found, std::move(*plan));
    awaiting_order_ = false;
    return true;
}

void MarchMission::ScheduleEnemyCrossings() {
    // Scenario roster order is the staff's dispatch order. Only allocate once:
    // clicks from the player and later posture changes retain their ownership.
    std::vector<MarchRegiment *> staff;
    for (auto &r : regiments_)
        if (r.team != PlayerTeam && r.waypoint && r.pose.position.x * r.waypoint->x <= 0.f)
            staff.push_back(&r);
    std::ranges::sort(staff, {}, [](const auto *r) { return r->id; });
    for (auto *r : staff) {
        const auto extent = LineHalfSize(*r);
        const float side = r->pose.position.x < 0.f ? 1.f : -1.f;
        Vec3 base = *r->waypoint;
        base.x = side * std::max(std::abs(base.x), MarchRiverHalfWidth + extent.x + 2.f);
        const float spacing = std::max(3.f, 2.f * extent.y + 1.f);
        bool assigned = false;
        for (std::size_t i = 0; i <= 2 * staff.size(); ++i) {
            auto goal = base;
            goal.y += float((i + 1) / 2) * (i % 2 ? spacing : -spacing);
            if (auto plan = PlanCrossing(*r, goal)) {
                AssignCrossing(*r, std::move(*plan));
                assigned = true;
                break;
            }
        }
        if (!assigned) {
            r->waypoint = r->pose.position;
            r->route.clear();
            r->next_waypoint = 0;
        }
    }
}

std::size_t MarchMission::CrossingQueuePosition(RegimentId id) const {
    const auto found = std::ranges::find(regiments_, id, &MarchRegiment::id);
    if (found == regiments_.end() || !found->crossing_order) return 0;
    const auto &order = *found->crossing_order;
    const auto cleared = [&](const MarchRegiment &r) {
        const auto &area = terrain_areas_[order.area];
        const float side = r.crossing_order->exit_side;
        const float edge = side > 0.f ? area.max_x : area.min_x;
        return side * (r.pose.position.x - edge) > r.formation.body_radius_m;
    };
    if (cleared(*found)) return 0;
    return 1 + std::ranges::count_if(regiments_, [&](const auto &r) {
        return r.team == found->team && r.crossing_order && r.crossing_order->area == order.area &&
            r.crossing_order->sequence < order.sequence && !cleared(r);
    });
}

bool MarchMission::SetPosture(RegimentId id, MarchPosture posture) {
    if (!regiment_tactics_ || encounter_ ||
        (posture != MarchPosture::TravelColumn && posture != MarchPosture::BattleLine))
        return false;
    const auto regiment = std::ranges::find(regiments_, id, &MarchRegiment::id);
    if (regiment == regiments_.end() || regiment->team != PlayerTeam)
        return false;
    if (regiment->fixed_frontage)
        return posture == MarchPosture::BattleLine;
    if (regiment->posture == posture)
        return true;

    const auto previous = std::exchange(regiment->posture, posture);
    if (!MarchStartWalkable(*regiment, terrain_, regiment_tactics_, field_)) {
        regiment->posture = previous;
        return false;
    }
    regiment->deployment_seconds = posture == MarchPosture::BattleLine
                                       ? DeploymentDuration(*regiment)
                                       : 0.f;
    regiment->crossing_order.reset();
    if (regiment->waypoint &&
        !AssignRoute(*regiment, *regiment->waypoint, terrain_, field_, regiment_tactics_)) {
        regiment->waypoint.reset();
        regiment->route.clear();
        regiment->next_waypoint = 0;
    }
    awaiting_order_ = false;
    return true;
}

void MarchMission::CheckForContact() {
    const auto *strip = StripFor(terrain_);
    const auto hill = MarchHill(terrain_, field_);
    for (const auto &first : regiments_) {
        if (first.team != PlayerTeam || !Walkable(first.pose.position, strip, field_))
            continue;
        for (const auto &second : regiments_) {
            if (second.team == PlayerTeam || !Walkable(second.pose.position, strip, field_))
                continue;
            const Vec3 a = first.pose.position, b = second.pose.position;
            const Vec3 difference = b - a;
            const float distance_squared = Dot(difference, difference);
            if (!enemy_found_ && distance_squared <= SightingDistance * SightingDistance &&
                !EvaluateTerrainShot(a, b, terrain_areas_).blocked &&
                (!hill || HillHasLineOfSight(*hill, a, b))) {
                enemy_found_ = awaiting_order_ = true;
                return;
            }
            if (enemy_found_ && distance_squared <= EncounterDistance * EncounterDistance &&
                ClearSegment(a, b, strip) &&
                (!MarchUsesDetailedTerrain(terrain_) ||
                 std::ranges::none_of(terrain_areas_, [&](const auto &area) {
                     if (!TerrainBlocksMovement(area)) return false;
                     auto barrier = area;
                     barrier.blocks_projectiles = true;
                     return EvaluateTerrainShot(a, b, std::span{&barrier, 1}).blocked;
                 }))) {
                encounter_ = MarchEncounter{first.id, second.id, (a + b) * .5f};
                return;
            }
        }
    }
}

void MarchMission::Step(float dt) {
    if (!std::isfinite(dt) || dt <= 0 || awaiting_order_ || encounter_)
        return;

    elapsed_ += dt;
    const auto hill = MarchHill(terrain_, field_);
    const bool queue_crossings = std::ranges::any_of(terrain_areas_, TerrainIsCrossing);
    std::vector<MarchRegiment> next;
    while (elapsed_ + FrameTolerance >= FrameSeconds) {
        elapsed_ = std::max(0.0, elapsed_ - FrameSeconds);
        if (queue_crossings)
            next = regiments_;
        for (auto &regiment : queue_crossings ? next : regiments_) {
            if (!regiment.crossing_order || CrossingQueuePosition(regiment.id) <= 1)
                Advance(regiment, regiment_tactics_, hill, terrain_areas_);
        }
        if (queue_crossings) {
            for (const auto &area : terrain_areas_)
                if (TerrainIsCrossing(area)) {
                    AdmitCrossing(regiments_, next, area, regiment_tactics_);
                    SpaceApproachingRegiments(regiments_, next, area);
                }
            regiments_.swap(next);
            for (auto &r : regiments_)
                if (r.crossing_order && r.next_waypoint == r.route.size()) {
                    r.pose.yaw = r.crossing_order->exit_side > 0.f ? 0.f : Pi;
                    r.posture = MarchPosture::BattleLine;
                    r.deployment_seconds = DeploymentDuration(r);
                    r.crossing_order.reset();
                }
        }
        CheckForContact();
        if (awaiting_order_ || encounter_ || std::ranges::none_of(regiments_, [](const auto &r) {
                return r.next_waypoint < r.route.size() || r.deployment_seconds > 0.f;
            })) {
            elapsed_ = 0;
            break;
        }
    }
}
} // namespace battle
