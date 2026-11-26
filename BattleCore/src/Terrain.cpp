#include "battle/Terrain.h"
#include "battle/Scenario.h"
#include "battle/Formation.h"
#include "HistoricalTerrainProfiles.h"

namespace battle {
namespace {
double HillScale(const TerrainHill &hill, double radial_distance) {
    if (radial_distance <= hill.crest_fraction)
        return 1.;
    if (radial_distance >= 1.)
        return 0.;
    const double t = (1. - radial_distance) / (1. - hill.crest_fraction);
    return t * t * (3. - 2. * t);
}

// Smooth compact contours keep the historical profiles cheap to sample per unit.
double SquaredPositive(double v) {
    return v > 0. ? v * v : 0.;
}

double HillMass(double x, double y, double center_x, double center_y, double rx, double ry) {
    const double u = (x - center_x) / rx, v = (y - center_y) / ry;
    return SquaredPositive(1. - u * u - v * v);
}

double Height(const TerrainHill &hill, double x, double y) {
    switch (hill.relief) {
    case TerrainRelief::CentralHill:
        return hill.height_m * HillScale(hill, std::hypot(x, y));
    case TerrainRelief::Waterloo: {
        // Mont-Saint-Jean and the gentler French approach rise above the valley.
        const double center = .27 + .10 * y * y;
        const double u = (x - center) / .33;
        const double ridge = SquaredPositive(1. - u * u) * std::max(0., 1. - .30 * y * y);
        return hill.height_m * (ridge + .28 * HillMass(x, y, -.48, -.03, .35, 1.12));
    }
    case TerrainRelief::Sekigahara:
        // The hills along the old roads leave an open basin through the center.
        return hill.height_m * std::max({
            HillMass(x, y, -.47, -.93, .51, .49),
            .92 * HillMass(x, y, -.49, .92, .55, .45),
            .63 * HillMass(x, y, .69, -.91, .47, .43),
            .52 * HillMass(x, y, .72, .92, .42, .41)});
    default:
        double rise = 0., depression = 0.;
        if (const auto *profile = historical::FindProfile(hill.relief))
            for (const auto &lobe : profile->heights) {
                const double height = lobe.height_m * HillMass(x, y, lobe.x, lobe.y, lobe.rx, lobe.ry);
                rise = std::max(rise, height);
                depression = std::min(depression, height);
            }
        return rise + depression;
    }
}

struct SegmentInterval {
    float first{0.f}, last{1.f};
};

std::optional<SegmentInterval> ClipSegment(const Obstacle &area, Vec3 from, Vec3 to) {
    SegmentInterval interval;
    const auto clip_axis = [&](float start, float end, float minimum, float maximum) {
        if (start == end)
            return start >= minimum && start <= maximum;
        const float delta = end - start;
        const float near_edge = (minimum - start) / delta;
        const float far_edge = (maximum - start) / delta;
        interval.first = std::max(interval.first, std::min(near_edge, far_edge));
        interval.last = std::min(interval.last, std::max(near_edge, far_edge));
        return interval.first <= interval.last;
    };
    if (!clip_axis(from.x, to.x, area.min_x, area.max_x) ||
        !clip_axis(from.y, to.y, area.min_y, area.max_y))
        return std::nullopt;
    return interval;
}

bool IsCover(const Obstacle &area) {
    return area.feature == TerrainFeature::StoneWall || area.feature == TerrainFeature::Hedge;
}

bool CrossesSteepSlope(const TerrainHill &hill, Vec3 from, Vec3 to) {
    // The tabletop distinguishes gentle and steep faces. On this smooth mesh,
    // define the latter by the local surface gradient, including downhill travel.
    constexpr double SteepGrade = .30;
    if (!ValidHill(hill) || !std::isfinite(from.x) || !std::isfinite(from.y) ||
        !std::isfinite(to.x) || !std::isfinite(to.y))
        return false;
    if (hill.relief != TerrainRelief::CentralHill) {
        if (hill.relief == TerrainRelief::Waterloo &&
            6. * hill.height_m / std::min(hill.radius_x_m, hill.radius_y_m) < SteepGrade)
            return false;
        if (hill.relief == TerrainRelief::Sekigahara &&
            std::max(std::abs(from.y), std::abs(to.y)) < .43f * hill.radius_y_m)
            return false;

        if (const auto *profile = historical::FindProfile(hill.relief)) {
            double rise_grade = 0., depression_grade = 0.;
            for (const auto &lobe : profile->heights) {
                // Upper bound on the gradient of a squared compact elliptical lobe.
                const double grade = 1.55 * std::abs(lobe.height_m) /
                    std::min(lobe.rx * hill.radius_x_m, lobe.ry * hill.radius_y_m);
                auto &bound = lobe.height_m >= 0.f ? rise_grade : depression_grade;
                bound = std::max(bound, grade);
            }
            if (rise_grade + depression_grade < SteepGrade)
                return false;
        }
        const auto delta = to - from;
        const int steps = std::max(1, int(std::ceil(std::min(160., std::hypot(
            double(delta.x) / hill.radius_x_m, double(delta.y) / hill.radius_y_m) * 55.))));
        constexpr double StepM = .3;
        const double sx = StepM / hill.radius_x_m, sy = StepM / hill.radius_y_m;
        for (int i = 0; i <= steps; ++i) {
            const double t = double(i) / steps;
            const double x = (from.x + delta.x * t) / hill.radius_x_m;
            const double y = (from.y + delta.y * t) / hill.radius_y_m;
            const double dx = Height(hill, x + sx, y) - Height(hill, x - sx, y);
            const double dy = Height(hill, x, y + sy) - Height(hill, x, y - sy);
            if (dx * dx + dy * dy >= std::pow(2. * StepM * SteepGrade, 2.))
                return true;
        }
        return false;
    }
    if (hill.height_m * 1.5 /
        ((1. - hill.crest_fraction) * std::min(hill.radius_x_m, hill.radius_y_m)) < SteepGrade)
        return false;
    const Obstacle bounds{-hill.radius_x_m, -hill.radius_y_m, hill.radius_x_m, hill.radius_y_m};
    const auto interval = ClipSegment(bounds, from, to);
    if (!interval) return false;
    const auto start = from + (to - from) * interval->first;
    const auto end = from + (to - from) * interval->last;
    const double dx = (double(end.x) - start.x) / hill.radius_x_m;
    const double dy = (double(end.y) - start.y) / hill.radius_y_m;
    const int steps = std::max(1, int(std::ceil(std::hypot(dx, dy) * 100.)));
    for (int i = 0; i <= steps; ++i) {
        const double x = start.x / hill.radius_x_m + dx * i / steps;
        const double y = start.y / hill.radius_y_m + dy * i / steps;
        const double r = std::hypot(x, y);
        if (r <= hill.crest_fraction || r >= 1.) continue;
        const double t = (1. - r) / (1. - hill.crest_fraction);
        const double grade = hill.height_m * 6. * t * (1. - t) /
            ((1. - hill.crest_fraction) * r) * std::hypot(x / hill.radius_x_m, y / hill.radius_y_m);
        if (grade >= SteepGrade) return true;
    }
    return false;
}
} // namespace

Vec3 TerrainFieldInset(const TerrainFieldBounds &bounds, float yaw, float half_depth, float half_width) {
    const Vec3 forward = Forward(yaw), right = Right(yaw);
    return {std::max(0.f, bounds.half_width_m - std::abs(forward.x) * half_depth - std::abs(right.x) * half_width),
            std::max(0.f, bounds.half_height_m - std::abs(forward.y) * half_depth - std::abs(right.y) * half_width),
            0.f};
}

bool TerrainBlocksMovement(const Obstacle &area) {
    switch (area.feature) {
    case TerrainFeature::Bridge:
    case TerrainFeature::Ford:
    case TerrainFeature::PloughedField:
    case TerrainFeature::Road:
    case TerrainFeature::RedoubtRamp:
    case TerrainFeature::RedoubtInterior:
        return false;
    default:
        return true;
    }
}

bool TerrainIsCrossing(const Obstacle &area) {
    return area.feature == TerrainFeature::Bridge || area.feature == TerrainFeature::Ford;
}

bool TerrainAreaOverlaps(const Obstacle &area, Pose pose, float half_depth, float half_width) {
    const auto offset = pose.position - Vec3{(area.min_x + area.max_x) * .5f,
                                             (area.min_y + area.max_y) * .5f, 0};
    const float half_x = (area.max_x - area.min_x) * .5f;
    const float half_y = (area.max_y - area.min_y) * .5f;
    const auto forward = Forward(pose.yaw);
    const auto right = Right(pose.yaw);
    for (const Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, forward, right}) {
        const float sum = std::abs(axis.x) * half_x + std::abs(axis.y) * half_y +
                          std::abs(Dot(axis, forward)) * half_depth +
                          std::abs(Dot(axis, right)) * half_width;
        if (std::abs(Dot(offset, axis)) >= sum)
            return false;
    }
    return true;
}

bool TerrainForbidsCharge(Vec3 from, Vec3 to, std::span<const Obstacle> terrain, bool wheeled) {
    return std::ranges::any_of(terrain, [=](const Obstacle &area) {
        return (TerrainIsCrossing(area) || area.feature == TerrainFeature::RedoubtRamp ||
                (wheeled && area.feature == TerrainFeature::PloughedField)) &&
               ClipSegment(area, from, to).has_value();
    });
}

float TerrainSpeedMultiplier(Vec3 from, Vec3 to, std::span<const Obstacle> terrain,
                             UnitTroop troop, float speed_mps) {
    bool road = false;
    float multiplier = 1.f;
    for (const auto &area : terrain) {
        if (!TerrainIsCrossing(area) && area.feature != TerrainFeature::PloughedField &&
            area.feature != TerrainFeature::Road && area.feature != TerrainFeature::RedoubtRamp)
            continue;
        const auto interval = ClipSegment(area, from, to);
        if (!interval || interval->first == interval->last)
            continue;
        // Rules pp. 66–68: half movement on crossings or furrows, and a road bonus
        // only when the movement stays on the road. The simulation uses 1 m per step.
        if (area.feature == TerrainFeature::RedoubtRamp)
            multiplier = std::min(multiplier, troop == UnitTroop::Mounted ? .29f : .42f);
        else if (area.feature != TerrainFeature::Road)
            multiplier = std::min(multiplier, .5f);
        else
            road |= interval->first == 0.f && interval->last == 1.f;
    }
    if (multiplier < 1.f)
        return multiplier;
    return road && speed_mps > 0.f
        ? 1.f + (troop == UnitTroop::Mounted ? 2.f : 1.f) / speed_mps : 1.f;
}

std::optional<Pose> TerrainCoverPosition(const Obstacle &area, Pose desired, float half_depth,
                                         float half_width) {
    if (!Finite(desired) || !std::isfinite(half_depth) ||
        !std::isfinite(half_width) || half_depth <= 0.f || half_width <= 0.f)
        return std::nullopt;

    if (area.feature == TerrainFeature::RedoubtInterior) {
        constexpr float clearance = .25f;
        const float min_x = area.min_x + half_depth + clearance;
        const float max_x = area.max_x - half_depth - clearance;
        const float min_y = area.min_y + half_width + clearance;
        const float max_y = area.max_y - half_width - clearance;
        if (min_x > max_x || min_y > max_y)
            return std::nullopt;
        return Pose{{min_x, std::clamp(desired.position.y, min_y, max_y), desired.position.z}, Pi};
    }
    if (!IsCover(area))
        return std::nullopt;

    const bool vertical = area.max_y - area.min_y >= area.max_x - area.min_x;
    const float min_along = vertical ? area.min_y : area.min_x;
    const float max_along = vertical ? area.max_y : area.max_x;
    if (2.f * half_width > max_along - min_along)
        return std::nullopt;
    const float min_across = vertical ? area.min_x : area.min_y;
    const float max_across = vertical ? area.max_x : area.max_y;
    const float across = vertical ? desired.position.x : desired.position.y;
    const float center = (min_across + max_across) * .5f;
    const auto facing = Forward(desired.yaw);
    const bool low = across == center ? (vertical ? facing.x : facing.y) >= 0.f : across < center;
    const float along = std::clamp(vertical ? desired.position.y : desired.position.x,
                                   min_along + half_width, max_along - half_width);
    constexpr float Clearance = .12f;
    const float shelter = low ? min_across - half_depth - Clearance : max_across + half_depth + Clearance;
    return vertical ? Pose{{shelter, along, desired.position.z}, low ? 0.f : Pi}
                    : Pose{{along, shelter, desired.position.z}, low ? Pi / 2.f : -Pi / 2.f};
}

void AssignCoverGuardSlots(const World &world, RegimentState &regiment) {
    const auto size = Footprint(regiment);
    for (const auto &cover : world.scenario.obstacles) {
        if (!IsCover(cover))
            continue;
        const auto station = TerrainCoverPosition(cover, regiment.pose, size.depth_m * .5f, size.width_m * .5f);
        if (!station || Distance(station->position, regiment.pose.position) > .12f ||
            Dot(Forward(station->yaw), Forward(regiment.pose.yaw)) < .99f)
            continue;
        const bool vertical = cover.max_y - cover.min_y >= cover.max_x - cover.min_x;
        const float radius = regiment.formation.body_radius_m;
        UnitId assigned{};
        for (const bool high : {false, true}) {
            const float edge = vertical ? (high ? cover.max_y : cover.min_y) : (high ? cover.max_x : cover.min_x);
            // A gap needs a second section on the same axis. An exposed end of
            // the entire wall (including the map edge) is not a gate.
            const auto across = vertical ? (cover.min_x + cover.max_x) * .5f : (cover.min_y + cover.max_y) * .5f;
            float gap = std::numeric_limits<float>::infinity();
            for (const auto &neighbor : world.scenario.obstacles) {
                if (!IsCover(neighbor) || &neighbor == &cover ||
                    across < (vertical ? neighbor.min_x : neighbor.min_y) ||
                    across > (vertical ? neighbor.max_x : neighbor.max_y))
                    continue;
                const float next = vertical ? (high ? neighbor.min_y : neighbor.max_y)
                                            : (high ? neighbor.min_x : neighbor.max_x);
                const float space = (high ? 1.f : -1.f) * (next - edge);
                if (space >= 0.f) gap = std::min(gap, space);
            }
            if (!std::isfinite(gap) || gap < 2.f * radius) continue;
            auto target = regiment.pose.position + Forward(station->yaw) * (size.depth_m * .5f - radius);
            (vertical ? target.y : target.x) = edge + (high ? 1.f : -1.f) * radius;
            const UnitState *nearest = nullptr;
            float best = std::numeric_limits<float>::infinity();
            for (auto id : regiment.units) {
                const auto *unit = FindUnit(world, id);
                if (!unit || !unit->alive || unit->id == assigned || unit->skirmisher || unit->skirmisher_returning ||
                    unit->control != ControlMode::FormationControlled || unit->slot_index >= regiment.slots.size() ||
                    std::ranges::any_of(world.officers, [id](const auto &o) { return o.unit == id; }))
                    continue;
                const float distance = Distance(regiment.slots[unit->slot_index].position, target);
                if (distance < best || (distance == best && nearest && id < nearest->id)) {
                    nearest = unit;
                    best = distance;
                }
            }
            if (nearest) {
                regiment.slots[nearest->slot_index].position = target;
                assigned = nearest->id;
            }
        }
        return;
    }
}

TerrainShot EvaluateTerrainShot(Vec3 from, Vec3 to, std::span<const Obstacle> terrain,
                               bool hard_projectile_cover) {
    if (!Finite(from) || !Finite(to))
        return {true, 0};
    TerrainShot result;
    const float distance = std::hypot(to.x - from.x, to.y - from.y);
    for (const auto &area : terrain) {
        const bool ramp = area.feature == TerrainFeature::RedoubtRamp;
        if (!area.blocks_projectiles && !IsCover(area) && !ramp)
            continue;
        const auto interval = ClipSegment(area, from, to);
        if (!interval)
            continue;
        if (ramp) {
            // Parapets protect targets close behind the crest. A crew facing the
            // ditch can still fire out and units at the rear entrance fire normally.
            const float depth = area.max_x - area.min_x;
            if (from.x < area.max_x && to.x >= area.max_x && to.x <= area.max_x + 4.f * depth &&
                to.y > area.min_y && to.y < area.max_y)
                result.shooting_modifier = std::min(result.shooting_modifier, -3);
            continue;
        }
        if (IsCover(area) && !(hard_projectile_cover && area.blocks_projectiles)) {
            // The first defending rank can use an adjacent obstacle as cover (p. 69).
            // The same firing step is available to a shooter behind its own cover.
            constexpr float CoverReach = 2.f;
            if ((1.f - interval->last) * distance <= CoverReach) {
                result.shooting_modifier = -3;
                continue;
            }
            if (interval->first * distance <= CoverReach)
                continue;
        }
        if (area.blocks_projectiles)
            return {true, 0};
    }
    return result;
}

bool ValidHill(const TerrainHill &hill) {
    return std::isfinite(hill.radius_x_m) && hill.radius_x_m > 0.f &&
           std::isfinite(hill.radius_y_m) && hill.radius_y_m > 0.f &&
           std::isfinite(hill.height_m) && hill.height_m > 0.f &&
           std::isfinite(hill.crest_fraction) && hill.crest_fraction >= 0.f &&
           hill.crest_fraction < 1.f &&
           hill.relief <= TerrainRelief::HistoricalLast;
}

float HillHeight(const TerrainHill &hill, Vec3 point) {
    if (!ValidHill(hill) || !std::isfinite(point.x) || !std::isfinite(point.y))
        return 0.f;
    return static_cast<float>(Height(hill, double(point.x) / hill.radius_x_m,
                                           double(point.y) / hill.radius_y_m));
}

std::optional<TerrainCrest> HillObjectiveCrest(const TerrainHill &hill) {
    if (!ValidHill(hill) || hill.relief == TerrainRelief::Sekigahara)
        return std::nullopt;
    if (const auto *profile = historical::FindProfile(hill.relief)) {
        if (!profile->crest)
            return std::nullopt;
        const auto &crest = *profile->crest;
        Vec3 center{crest.x * hill.radius_x_m, crest.y * hill.radius_y_m, 0.f};
        center.z = HillHeight(hill, center);
        return TerrainCrest{center, crest.rx * hill.radius_x_m, crest.ry * hill.radius_y_m};
    }
    if (hill.relief == TerrainRelief::Waterloo)
        return TerrainCrest{{.27f * hill.radius_x_m, 0.f, hill.height_m},
                            .085f * hill.radius_x_m, .64f * hill.radius_y_m};
    return TerrainCrest{{0.f, 0.f, hill.height_m},
                        hill.crest_fraction * hill.radius_x_m, hill.crest_fraction * hill.radius_y_m};
}

bool OnHillCrest(const TerrainHill &hill, Vec3 point) {
    const auto crest = HillObjectiveCrest(hill);
    if (!crest) return false;
    const auto offset = point - crest->center;
    if (crest->radius_x_m == 0.f || crest->radius_y_m == 0.f)
        return offset.x == 0.f && offset.y == 0.f;
    return std::hypot(double(offset.x) / crest->radius_x_m,
                      double(offset.y) / crest->radius_y_m) <= 1.;
}

float HillUphillSpeedMultiplier(const TerrainHill &hill, Vec3 from, Vec3 to) {
    const double distance = std::hypot(double(to.x) - from.x, double(to.y) - from.y);
    if (distance <= .00001 || !std::isfinite(distance))
        return 1.f;
    const double rise = double(HillHeight(hill, to)) - HillHeight(hill, from);
    if (rise <= 0.)
        return 1.f;
    return static_cast<float>(std::max(.55, 1. / (1. + 2. * rise / distance)));
}

bool HillSegmentPassable(const TerrainHill &hill, Vec3 from, Vec3 to, UnitTroop troop) {
    return troop != UnitTroop::Mounted || !CrossesSteepSlope(hill, from, to);
}

float HillUphillSpeedMultiplier(const TerrainHill &hill, Vec3 from, Vec3 to, UnitTroop troop, float speed) {
    if (CrossesSteepSlope(hill, from, to))
        return troop == UnitTroop::Mounted ? 0.f : (speed > 1.f ? 1.f / speed : 1.f);
    return HillUphillSpeedMultiplier(hill, from, to);
}

bool HillHasLineOfSight(const TerrainHill &hill, Vec3 from, Vec3 to) {
    if (!ValidHill(hill))
        return true;
    if (!std::isfinite(from.x) || !std::isfinite(from.y) ||
        !std::isfinite(to.x) || !std::isfinite(to.y))
        return false;
    const double x = double(from.x) / hill.radius_x_m;
    const double y = double(from.y) / hill.radius_y_m;
    const double dx = (double(to.x) - from.x) / hill.radius_x_m;
    const double dy = (double(to.y) - from.y) / hill.radius_y_m;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared == 0.)
        return true;
    const double nearest = std::clamp(-(x * dx + y * dy) / length_squared, 0., 1.);
    const auto ground = [&](double t) { return Height(hill, x + dx * t, y + dy * t); };
    constexpr double EyeHeight = 1.6;
    const double from_height = ground(0.) + EyeHeight;
    const double to_height = ground(1.) + EyeHeight;
    const auto ray = [&](double t) { return std::lerp(from_height, to_height, t); };
    if (hill.relief != TerrainRelief::CentralHill) {
        if (hill.relief == TerrainRelief::Sekigahara && std::max(std::abs(y), std::abs(y + dy)) < .43)
            return true;
        const int steps = std::clamp(int(std::ceil(std::min(128., std::sqrt(length_squared) * 50.))), 2, 128);
        for (int i = 1; i < steps; ++i) {
            const double t = double(i) / steps;
            if (ground(t) > ray(t)) return false;
        }
        return true;
    }
    // On any subinterval, the terrain is highest at its closest point to
    // the origin. Use that upper bound to prove clearance before subdividing.
    const auto clear = [&](auto &self, double first, double last, int depth) -> bool {
        const double peak_at = std::clamp(nearest, first, last);
        const double peak = ground(peak_at);
        if (peak <= std::min(ray(first), ray(last)))
            return true;
        if (peak > ray(peak_at) || depth == 0)
            return false;
        const double middle = (first + last) * .5;
        return self(self, first, middle, depth - 1) &&
               self(self, middle, last, depth - 1);
    };
    return clear(clear, 0., 1., 10);
}
} // namespace battle
