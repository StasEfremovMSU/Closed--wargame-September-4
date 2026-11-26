#include "battle/Navigation.h"
#include "battle/Formation.h"
#include <array>
#include <limits>
#include <queue>
#include <tuple>
namespace battle {
namespace {
class StaticReachability {
    struct Entry {
        int start;
        FormationFootprint size;
        bool face_travel;
        std::vector<uint64_t> reached;
        uint64_t last_used;
    };

    NavigationGridGeometry grid_{};
    std::vector<Obstacle> obstacles_;
    std::vector<Entry> entries_;
    uint64_t clock_{};

    auto Find(int start, FormationFootprint size, bool face_travel) {
        return std::ranges::find_if(entries_, [&](const Entry &entry) {
            return entry.start == start && entry.size.width_m == size.width_m &&
                   entry.size.depth_m == size.depth_m && entry.face_travel == face_travel;
        });
    }

  public:
    void UseMap(NavigationGridGeometry grid, const std::vector<Obstacle> &obstacles) {
        const auto same_obstacle = [](const Obstacle &a, const Obstacle &b) {
            return std::tie(a.min_x, a.min_y, a.max_x, a.max_y, a.feature, a.blocks_projectiles) ==
                   std::tie(b.min_x, b.min_y, b.max_x, b.max_y, b.feature, b.blocks_projectiles);
        };
        if (grid.cell_m == grid_.cell_m && grid.width == grid_.width &&
            grid.height == grid_.height && grid.origin == grid_.origin &&
            std::ranges::equal(obstacles, obstacles_, same_obstacle))
            return;
        grid_ = grid;
        obstacles_ = obstacles;
        entries_.clear();
    }

    std::optional<uint64_t> ReachableHeadings(int start, FormationFootprint size, int goal,
                                              bool face_travel) {
        const auto entry = Find(start, size, face_travel);
        if (entry == entries_.end())
            return std::nullopt;
        entry->last_used = ++clock_;
        // Each cell owns four consecutive bits, so a cell doesn't cross a word.
        return (entry->reached[goal / 64] >> (goal % 64)) & 0xfull;
    }

    void Remember(int start, FormationFootprint size, bool face_travel, std::span<const int> cost) {
        constexpr size_t MaxEntries = 8;
        if (Find(start, size, face_travel) != entries_.end())
            return;
        Entry coverage{start, size, face_travel, std::vector<uint64_t>((cost.size() + 63) / 64), ++clock_};
        for (size_t i = 0; i < cost.size(); ++i)
            if (cost[i] != std::numeric_limits<int>::max())
                coverage.reached[i / 64] |= uint64_t{1} << (i % 64);
        if (entries_.size() < MaxEntries)
            entries_.push_back(std::move(coverage));
        else
            *std::ranges::min_element(entries_, {}, &Entry::last_used) = std::move(coverage);
    }
};

bool ClearFormationBlocks(Pose from, Pose to, FormationFootprint footprint,
                          std::span<const FormationBlock> occupied) {
    if (occupied.empty())
        return true;
    // A turn projects a circular mover onto its starting axes. The resulting
    // conservative sweep can extend past that circle in world space. Keep room
    // for sqrt(2) times the summed half-extents before rejecting a distant block.
    const float scale = from.yaw == to.yaw ? .51f : .75f;
    const float padding = (footprint.width_m + footprint.depth_m) * scale + .001f;
    const bool can_bound = Finite(from) && Finite(to) && std::isfinite(padding);
    const float left = std::min(from.position.x, to.position.x) - padding;
    const float right = std::max(from.position.x, to.position.x) + padding;
    const float bottom = std::min(from.position.y, to.position.y) - padding;
    const float top = std::max(from.position.y, to.position.y) + padding;
    return std::none_of(occupied.begin(), occupied.end(), [&](const auto &block) {
        const float extent = (block.footprint.width_m + block.footprint.depth_m) * scale;
        const auto position = block.pose.position;
        if (can_bound && Finite(block.pose) && std::isfinite(extent) &&
            (position.x < left - extent || position.x > right + extent ||
             position.y < bottom - extent || position.y > top + extent))
            return false;
        return SweepLimit(from, to, footprint, block.pose, block.footprint) < .99999f ||
               RectanglesOverlap(to, footprint, block.pose, block.footprint);
    });
}
bool ClearTerrain(Pose from, Pose to, FormationFootprint footprint, const World &world) {
    return std::none_of(world.scenario.obstacles.begin(), world.scenario.obstacles.end(),
                        [&](const auto &area) {
        if (!TerrainBlocksMovement(area))
            return false;
        const Pose obstacle{{(area.min_x + area.max_x) * .5f,
                             (area.min_y + area.max_y) * .5f, from.position.z}, 0.f};
        const FormationFootprint size{area.max_y - area.min_y, area.max_x - area.min_x};
        return SweepLimit(from, to, footprint, obstacle, size) < .99999f ||
               RectanglesOverlap(to, footprint, obstacle, size);
    });
}
std::vector<FormationBlock> FormationBlocks(const World &world, RegimentId moving,
                                            RegimentId padded = {}, RegimentId excluded = {}) {
    std::vector<FormationBlock> occupied;
    for (const auto &regiment : world.regiments)
        if (regiment.id != moving && regiment.id != excluded && !regiment.units.empty()) {
            auto footprint = Footprint(regiment);
            if (regiment.id == padded) {
                footprint.width_m += 3.6f;
                footprint.depth_m += 3.6f;
            }
            occupied.push_back({regiment.pose, footprint});
        }
    return occupied;
}

std::function<bool(Pose, Pose)> HillConstraint(const World &world, const RegimentState &moving) {
    if (!world.scenario.hill || moving.units.empty())
        return {};
    const auto *unit = FindUnit(world, moving.units.front());
    if (!unit || unit->troop != UnitTroop::Mounted)
        return {};
    return [hill = *world.scenario.hill](Pose a, Pose b) {
        return HillSegmentPassable(hill, a.position, b.position, UnitTroop::Mounted);
    };
}

std::array<Pose, 3> TravelLeg(Pose from, Vec3 to, std::optional<float> arrival_yaw) {
    const auto delta = to - from.position;
    const float yaw = std::hypot(delta.x, delta.y) > .00001f ? std::atan2(delta.y, delta.x) : from.yaw;
    return {Pose{from.position, yaw}, Pose{to, yaw}, Pose{to, arrival_yaw.value_or(yaw)}};
}

bool TravelLegClear(Pose from, const std::array<Pose, 3> &leg, const auto &traversable) {
    for (const auto &pose : leg) {
        if (from != pose && !traversable(from, pose))
            return false;
        from = pose;
    }
    return true;
}

void AppendTravelLeg(std::vector<Pose> &waypoints, Pose from, const std::array<Pose, 3> &leg) {
    for (const auto &pose : leg)
        if (pose != from) {
            waypoints.push_back(pose);
            from = pose;
        }
}

PathResult DirectPath(const PathRequest &request, const auto &traversable) {
    if (request.face_travel && !request.preserve_facing) {
        const auto leg = TravelLeg(request.start, request.goal, request.arrival_yaw);
        if (!TravelLegClear(request.start, leg, traversable) ||
            (leg.back() == request.start && !traversable(request.start, request.start)))
            return {Status::NoPath, {}};
        PathResult path;
        AppendTravelLeg(path.waypoints, request.start, leg);
        if (path.waypoints.empty())
            path.waypoints.push_back(request.start);
        return path;
    }
    const Pose arrival{request.goal, request.arrival_yaw.value_or(request.start.yaw)};
    const Pose turn_first{request.start.position, arrival.yaw};
    if (turn_first != request.start && traversable(request.start, turn_first) &&
        traversable(turn_first, arrival))
        return {Status::Ok, {turn_first, arrival}};

    if (request.preserve_facing && turn_first != request.start)
        return {Status::NoPath, {}};

    const Pose approach{request.goal, request.start.yaw};
    if (!traversable(request.start, approach) || !traversable(approach, arrival))
        return {Status::NoPath, {}};
    if (approach == arrival)
        return {Status::Ok, {arrival}};
    return {Status::Ok, {approach, arrival}};
}
} // namespace

PathResult FindFlankRoute(const NavigationService &navigation, const World &world,
                          const RegimentState &moving, Pose goal, RegimentId target) {
    const auto size = Footprint(moving);
    const auto occupied = FormationBlocks(world, moving.id);
    const auto slope = HillConstraint(world, moving);
    const auto clear = [&](Pose from, Pose to) {
        return (!slope || slope(from, to)) && ClearFormationBlocks(from, to, size, occupied) &&
               ClearTerrain(from, to, size, world);
    };
    const auto forward = Forward(goal.yaw);
    const auto difference = goal.position - moving.pose.position;
    if (std::abs(std::remainder(moving.pose.yaw - goal.yaw, 2 * Pi)) < .001f &&
        Dot(difference, forward) >= -.05f && std::abs(Dot(difference, Right(goal.yaw))) < .05f &&
        clear(moving.pose, goal))
        return {Status::Ok, {goal}};

    // Finish the turn beyond the melee reach, then enter contact head-on from the side.
    const float turn_padding = (std::hypot(size.width_m, size.depth_m) - size.depth_m) * .5f;
    const Pose staging{goal.position - forward * (2.2f + turn_padding), goal.yaw};
    if (!clear(staging, goal))
        return {Status::NoPath, {}};
    const auto padded = FormationBlocks(world, moving.id, target);
    PathResult best{Status::NoPath, {}};
    float best_length = std::numeric_limits<float>::infinity();
    const auto right = Right(goal.yaw);
    const float turn_clearance = .5f * (std::hypot(size.width_m, size.depth_m) - size.width_m) + 1.f;
    const float toward_mover = Dot(moving.pose.position - staging.position, right) >= 0.f ? 1.f : -1.f;
    for (const float shift : {0.f, turn_clearance * toward_mover, -turn_clearance * toward_mover}) {
        // A bank may leave enough room to pass in formation but too little room to turn.
        const Pose turn{staging.position + right * shift, staging.yaw};
        if (!clear(turn, staging))
            continue;
        auto route = navigation.FindPath({moving.pose, turn.position, size, turn.yaw, padded,
                                          world.scenario.rules.adaptive_battle, false, slope});
        if (route.status != Status::Ok)
            continue;
        if (shift != 0.f)
            route.waypoints.push_back(staging);
        route.waypoints.push_back(goal);
        float length = 0.f;
        Vec3 position = moving.pose.position;
        for (const auto &waypoint : route.waypoints) {
            length += Distance(position, waypoint.position);
            position = waypoint.position;
        }
        if (length < best_length) {
            best = std::move(route);
            best_length = length;
        }
        if (best_length <= Distance(moving.pose.position, goal.position) + 4.f)
            break;
    }
    return best;
}

PathResult FindAttackRoute(const NavigationService &navigation, const World &world,
                           const RegimentState &moving, Pose observed_enemy, RegimentId enemy_id) {
    const auto *enemy = FindRegiment(world, enemy_id);
    if (!enemy || enemy->units.empty() || enemy->team == moving.team || !Finite(observed_enemy))
        return {Status::NoPath, {}};
    const auto enemy_size = Footprint(*enemy);
    const auto moving_size = Footprint(moving);
    const auto offset = moving.pose.position - observed_enemy.position;
    std::array<float, 4> sides{0.f, Pi / 2, Pi, -Pi / 2};
    std::stable_sort(sides.begin(), sides.end(), [&](float a, float b) {
        return Dot(offset, Forward(a)) > Dot(offset, Forward(b));
    });
    for (const float away : sides) {
        const auto normal = Forward(away);
        const float enemy_radius = .5f *
            (std::abs(Dot(normal, Forward(observed_enemy.yaw))) * enemy_size.depth_m +
             std::abs(Dot(normal, Right(observed_enemy.yaw))) * enemy_size.width_m);
        const float separation = enemy_radius + .5f * moving_size.depth_m + .4f;
        const Pose approach{observed_enemy.position + normal * separation, away + Pi};
        if (away == sides.front()) {
            const Pose straight{approach.position, moving.pose.yaw};
            const bool ally_yielding = std::any_of(world.regiments.begin(), world.regiments.end(),
                [&](const auto &ally) {
                    return ally.id != moving.id && ally.team == moving.team && !ally.units.empty() &&
                           (ally.ai_action == OrderType::Withdraw || ally.ai_action == OrderType::Rally) &&
                           SweepLimit(moving.pose, straight, moving_size, ally.pose, Footprint(ally)) <
                               .99999f;
                });
            // The relief unit can take the short lane as soon as its ally yields it.
            if (ally_yielding)
                return {Status::NoPath, {}};
        }
        auto route = FindFlankRoute(navigation, world, moving, approach, enemy_id);
        if (route.status == Status::Ok)
            return route;
    }
    return {Status::NoPath, {}};
}

PathResult FindSupportRoute(const NavigationService &navigation, const World &world,
                            const RegimentState &moving, Vec3 goal, RegimentId excluded) {
    const auto occupied = FormationBlocks(world, moving.id, {}, excluded);
    const auto slope = HillConstraint(world, moving);
    auto route = navigation.FindPath({moving.pose, goal, Footprint(moving), std::nullopt, occupied,
                                      world.scenario.rules.adaptive_battle, false, slope});
    if (route.status != Status::Ok)
        return route;
    auto from = moving.pose;
    for (const auto waypoint : route.waypoints) {
        if ((slope && !slope(from, waypoint)) || !ClearTerrain(from, waypoint, Footprint(moving), world))
            return {Status::NoPath, {}};
        from = waypoint;
    }
    return route;
}

PathResult FindFormationRoute(const NavigationService &navigation, const World &world,
                              const RegimentState &moving, Pose goal, bool preserve_facing) {
    const auto occupied = FormationBlocks(world, moving.id);
    const auto footprint = Footprint(moving);
    const auto slope = HillConstraint(world, moving);
    const auto *lead = moving.units.empty() ? nullptr : FindUnit(world, moving.units.front());
    const bool face_travel = world.scenario.rules.regiment_tactics && !preserve_facing &&
        (moving.panic || (lead && lead->troop == UnitTroop::Mounted));
    // Infantry can keep the swept orientation of a narrow column while its figures turn to walk.
    auto route = navigation.FindPath({moving.pose, goal.position, footprint, goal.yaw, occupied,
                                      true, preserve_facing, slope, face_travel});
    if (route.status != Status::Ok)
        return route;
    auto from = moving.pose;
    for (const auto waypoint : route.waypoints) {
        if (!Finite(waypoint) || (slope && !slope(from, waypoint)) || !ClearTerrain(from, waypoint, footprint, world) ||
            !ClearFormationBlocks(from, waypoint, footprint, occupied))
            return {Status::NoPath, {}};
        from = waypoint;
    }
    return route;
}

bool FormationRouteSegmentClear(const World &world, const RegimentState &moving, Pose goal) {
    const auto occupied = FormationBlocks(world, moving.id);
    const auto slope = HillConstraint(world, moving);
    return (!slope || slope(moving.pose, goal)) && ClearFormationBlocks(moving.pose, goal, Footprint(moving), occupied);
}

bool CanContinueFormationMarch(const World &world, const RegimentState &moving, Pose waypoint) {
    const auto footprint = Footprint(moving);
    const auto slope = HillConstraint(world, moving);
    if ((slope && !slope(moving.pose, waypoint)) ||
        !ClearTerrain(moving.pose, waypoint, footprint, world))
        return false;
    if (const auto &lane = moving.march_lane; lane && lane->waypoint == waypoint &&
        lane->detour.empty() && lane->recovery_attempts == MarchLane::MaxAttempts &&
        lane->stalled_seconds >= MarchLane::StallSeconds)
        return FormationRouteSegmentClear(world, moving, waypoint);
    // The movement sweep handles allied traffic on this lane. Keeping the existing route
    // allows allies to pass and gives a genuinely stalled march time to recover.
    return std::ranges::none_of(world.regiments, [&](const auto &other) {
        if (other.team == moving.team || other.units.empty())
            return false;
        const auto other_size = Footprint(other);
        return SweepLimit(moving.pose, waypoint, footprint, other.pose, other_size) < .99999f ||
               RectanglesOverlap(waypoint, footprint, other.pose, other_size);
    });
}

PathResult FlatNavigationService::FindPath(const PathRequest &request) const {
    const auto traversable = [&](Pose from, Pose to) {
        return ClearFormationBlocks(from, to, request.footprint, request.occupied) &&
               (!request.segment_clear || request.segment_clear(from, to));
    };
    if (request.prefer_direct || request.preserve_facing || request.face_travel)
        return DirectPath(request, traversable);
    const Pose approach{request.goal, request.start.yaw};
    const Pose arrival{request.goal, request.arrival_yaw.value_or(approach.yaw)};
    if (!traversable(request.start, approach) || !traversable(approach, arrival))
        return {Status::NoPath, {}};
    if (approach == arrival)
        return {Status::Ok, {arrival}};
    return {Status::Ok, {approach, arrival}};
}

bool GridNavigationService::CanTraverse(Pose a, Pose b, FormationFootprint fp,
                                        std::span<const FormationBlock> occupied) const {
    if (!Finite(a) || !Finite(b) || !std::isfinite(fp.width_m) || !std::isfinite(fp.depth_m) ||
        fp.width_m <= 0 || fp.depth_m <= 0 || cell_m_ <= 0 || !width_ || !height_)
        return false;
    bool turn = std::abs(std::remainder(b.yaw - a.yaw, 2 * Pi)) > .00001f;
    float radius = std::hypot(fp.width_m, fp.depth_m) * .5f;
    for (Pose p : {a, b}) {
        float ex = turn ? radius
                        : (std::abs(std::cos(p.yaw)) * fp.depth_m +
                           std::abs(std::sin(p.yaw)) * fp.width_m) *
                              .5f;
        float ey = turn ? radius
                        : (std::abs(std::sin(p.yaw)) * fp.depth_m +
                           std::abs(std::cos(p.yaw)) * fp.width_m) *
                              .5f;
        if (p.position.x - ex < origin_.x - cell_m_ * .5f ||
            p.position.x + ex > origin_.x + (float(width_) - .5f) * cell_m_ ||
            p.position.y - ey < origin_.y - cell_m_ * .5f ||
            p.position.y + ey > origin_.y + (float(height_) - .5f) * cell_m_)
            return false;
    }
    // Enclose the whole swept formation, including the circle used during a turn.
    // Distant obstacles cannot affect the exact sweep or overlap tests below.
    const float padding = radius + .001f;
    const float left = std::min(a.position.x, b.position.x) - padding;
    const float right = std::max(a.position.x, b.position.x) + padding;
    const float bottom = std::min(a.position.y, b.position.y) - padding;
    const float top = std::max(a.position.y, b.position.y) + padding;
    for (auto &o : obstacles_) {
        if (!TerrainBlocksMovement(o) || o.max_x < left || o.min_x > right ||
            o.max_y < bottom || o.min_y > top)
            continue;
        Pose box{{(o.min_x + o.max_x) * .5f, (o.min_y + o.max_y) * .5f, a.position.z}, 0};
        FormationFootprint size{o.max_y - o.min_y, o.max_x - o.min_x};
        if (turn) {
            float x = std::clamp(a.position.x, o.min_x, o.max_x),
                  y = std::clamp(a.position.y, o.min_y, o.max_y);
            if (std::hypot(a.position.x - x, a.position.y - y) < radius)
                return false;
        }
        if (SweepLimit(a, b, fp, box, size) < .99999f || RectanglesOverlap(b, fp, box, size))
            return false;
    }
    return ClearFormationBlocks(a, b, fp, occupied);
}
PathResult GridNavigationService::FindPath(const PathRequest &r) const {
    auto fail = []() { return PathResult{Status::NoPath, {}}; };
    const bool face_travel = r.face_travel && !r.preserve_facing;
    if (!Finite(r.start) || !Finite(r.goal) || !std::isfinite(cell_m_) || cell_m_ <= 0 || !width_ ||
        !height_ || uint64_t(width_) * height_ > 1000000 ||
        (r.arrival_yaw && !std::isfinite(*r.arrival_yaw)))
        return fail();
    const auto traversable = [&](Pose from, Pose to) {
        return CanTraverse(from, to, r.footprint, r.occupied) &&
               (!r.segment_clear || r.segment_clear(from, to));
    };
    if (r.arrival_yaw) {
        const Pose goal{r.goal, *r.arrival_yaw};
        if (!traversable(goal, goal))
            return fail();
    }
    auto point = [&](int x, int y, int h) {
        return Pose{{origin_.x + x * cell_m_, origin_.y + y * cell_m_, r.start.position.z},
                    r.preserve_facing ? r.arrival_yaw.value_or(r.start.yaw) : h * Pi / 2};
    };
    auto cell = [&](Vec3 p) {
        return std::pair{int(std::round((p.x - origin_.x) / cell_m_)),
                         int(std::round((p.y - origin_.y) / cell_m_))};
    };
    // Reject bounds before converting untrusted coordinates to int.
    if (r.start.position.x < origin_.x || r.start.position.y < origin_.y ||
        r.start.position.x > origin_.x + (width_ - 1) * cell_m_ ||
        r.start.position.y > origin_.y + (height_ - 1) * cell_m_ || r.goal.x < origin_.x ||
        r.goal.y < origin_.y || r.goal.x > origin_.x + (width_ - 1) * cell_m_ ||
        r.goal.y > origin_.y + (height_ - 1) * cell_m_)
        return fail();
    if (r.prefer_direct || r.preserve_facing || face_travel) {
        auto direct = DirectPath(r, traversable);
        if (direct.status == Status::Ok)
            return direct;
    }
    if (!r.arrival_yaw && !r.preserve_facing) {
        const Pose along_x{r.goal, 0.f};
        const Pose along_y{r.goal, Pi / 2};
        if (!CanTraverse(along_x, along_x, r.footprint, r.occupied) &&
            !CanTraverse(along_y, along_y, r.footprint, r.occupied))
            return fail();
    }
    auto [sx, sy] = cell(r.start.position);
    auto [gx, gy] = cell(r.goal);
    int sh = r.preserve_facing ? 0 : int(std::round(std::remainder(r.start.yaw, 2 * Pi) / (Pi / 2)));
    sh = (sh % 4 + 4) % 4;
    auto start = point(sx, sy, sh);
    Pose initial_turn{r.start.position, start.yaw};
    std::array<Pose, 3> initial_leg{};
    if (face_travel) {
        initial_leg = TravelLeg(r.start, start.position, start.yaw);
        if (!TravelLegClear(r.start, initial_leg, traversable) || !traversable(start, start))
            return fail();
    } else if (!traversable(r.start, initial_turn) || !traversable(initial_turn, start)) {
        return fail();
    }
    auto node = [&](int x, int y, int h) { return (y * int(width_) + x) * 4 + h; };
    int begin = node(sx, sy, sh);
    size_t total = size_t(width_) * height_ * 4;
    const auto can_finish = [&](Pose pose) {
        if (face_travel)
            return TravelLegClear(pose, TravelLeg(pose, r.goal, r.arrival_yaw), traversable);
        const Pose approach{r.goal, pose.yaw};
        return traversable(pose, approach) &&
               (!r.arrival_yaw || traversable(approach, {r.goal, *r.arrival_yaw}));
    };
    StaticReachability *coverage = nullptr;
    // Cache only proofs from an exhausted static search. The direct path and
    // exact entry to the start node have already been tested above.
    if (!r.preserve_facing && !r.segment_clear && r.occupied.empty() &&
        !obstacles_.empty() && obstacles_.size() <= 128 && total <= 1048576 &&
        gx >= 0 && gy >= 0 && gx < int(width_) && gy < int(height_)) {
        static thread_local StaticReachability memo;
        memo.UseMap(Geometry(), obstacles_);
        coverage = &memo;
        if (const auto reached = memo.ReachableHeadings(begin, r.footprint, node(gx, gy, 0), face_travel)) {
            const std::array headings{0, 1, 2, 3};
            if (std::ranges::none_of(headings, [&](int h) {
                    return (*reached & (uint64_t{1} << h)) && can_finish(point(gx, gy, h));
                }))
                return fail();
        }
    }
    std::vector<int> cost(total, std::numeric_limits<int>::max()), parent(total, -1);
    // With equal estimated path costs, finish the route closest to the goal first.
    // The node index breaks remaining ties deterministically.
    using Entry = std::tuple<int, int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    cost[begin] = 0;
    open.push({std::abs(gx - sx) + std::abs(gy - sy), 0, begin});
    int found = -1;
    constexpr int dx[]{1, 0, -1, 0}, dy[]{0, 1, 0, -1};
    while (!open.empty()) {
        const auto [estimate, negative_cost, n] = open.top();
        open.pop();
        if (-negative_cost != cost[n])
            continue;
        int h = n % 4, x = (n / 4) % int(width_), y = (n / 4) / int(width_);
        auto pose = point(x, y, h);
        if (x == gx && y == gy && can_finish(pose)) {
            found = n;
            break;
        }
        for (int action = 0; action < 4; ++action) {
            if (face_travel && action == 1)
                continue;
            int xx = x, yy = y, hh = h;
            if (r.preserve_facing) {
                xx += dx[action];
                yy += dy[action];
            } else if (action < 2) {
                int direction = action == 0 ? 1 : -1;
                xx += dx[h] * direction;
                yy += dy[h] * direction;
            } else
                hh = (h + (action == 2 ? 1 : 3)) % 4;
            if (xx < 0 || yy < 0 || xx >= int(width_) || yy >= int(height_))
                continue;
            int next = node(xx, yy, hh);
            if (cost[n] + 1 >= cost[next])
                continue;
            auto target = point(xx, yy, hh);
            if (!traversable(pose, target))
                continue;
            cost[next] = cost[n] + 1;
            parent[next] = n;
            open.push({cost[next] + std::abs(gx - xx) + std::abs(gy - yy), -cost[next], next});
        }
    }
    if (found < 0) {
        if (coverage)
            coverage->Remember(begin, r.footprint, face_travel, cost);
        return fail();
    }
    std::vector<Pose> reverse;
    for (int n = found; n != -1; n = parent[n])
        reverse.push_back(point((n / 4) % int(width_), (n / 4) / int(width_), n % 4));
    std::reverse(reverse.begin(), reverse.end());
    if (face_travel) {
        std::vector<Pose> route;
        AppendTravelLeg(route, r.start, initial_leg);
        route.insert(route.end(), reverse.begin() + 1, reverse.end());
        AppendTravelLeg(route, reverse.back(), TravelLeg(reverse.back(), r.goal, r.arrival_yaw));
        if (route.empty())
            route.push_back(r.start);
        return {Status::Ok, std::move(route)};
    }
    std::vector<Pose> route{initial_turn};
    route.insert(route.end(), reverse.begin(), reverse.end());
    route.push_back({r.goal, route.back().yaw});
    if (r.arrival_yaw && std::abs(std::remainder(route.back().yaw - *r.arrival_yaw, 2 * Pi)) > .00001f)
        route.push_back({r.goal, *r.arrival_yaw});
    return {Status::Ok, std::move(route)};
}
} // namespace battle
