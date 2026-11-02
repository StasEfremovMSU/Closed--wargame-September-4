#include "battle/Spatial.h"
namespace battle {
namespace {
int64_t Cell(float x, float scale) {
    return int64_t(std::clamp(std::floor(double(x) / scale), -1000000000.0, 1000000000.0));
}
uint64_t Key(int64_t x, int64_t y) {
    return (uint64_t(uint32_t(x)) << 32) | uint32_t(y);
}
} // namespace
void SpatialIndex::Rebuild(std::span<const UnitPosition> input) {
    all_.assign(input.begin(), input.end());
    cells_.clear();
    unbounded_cells_ = false;
    if (!std::isfinite(cell_m_) || cell_m_ <= 0)
        return;
    for (size_t i = 0; i < all_.size(); ++i)
        if (Finite(all_[i].position)) {
            auto x = Cell(all_[i].position.x, cell_m_), y = Cell(all_[i].position.y, cell_m_);
            unbounded_cells_ |= std::abs(x) == 1000000000 || std::abs(y) == 1000000000;
            cells_[Key(x, y)].push_back(i);
        }
}
std::optional<NearestEnemy> SpatialIndex::FindNearestEnemy(Vec3 p, float radius, int32_t team,
                                                           uint64_t *checks) const {
    if (!Finite(p) || !std::isfinite(radius) || radius < 0 || !std::isfinite(cell_m_) ||
        cell_m_ <= 0)
        return {};
    std::optional<NearestEnemy> result;
    float best = radius;
    double radius2 = double(radius) * radius;
    auto consider = [&](const UnitPosition &point) {
        if (checks)
            ++*checks;
        if (point.team == team)
            return;
        auto d = point.position - p;
        double squared = double(d.x) * d.x + double(d.y) * d.y + double(d.z) * d.z;
        if (!(squared <= radius2))
            return;
        float distance = Length(d);
        if (!result || distance < best || (distance == best && point.id < result->id)) {
            best = distance;
            result = NearestEnemy{point.id, point.payload_index, distance};
        }
    };
    auto x0 = Cell(p.x - radius, cell_m_), x1 = Cell(p.x + radius, cell_m_),
         y0 = Cell(p.y - radius, cell_m_), y1 = Cell(p.y + radius, cell_m_);
    auto cx = Cell(p.x, cell_m_), cy = Cell(p.y, cell_m_);
    if (unbounded_cells_ || std::abs(cx) == 1000000000 || std::abs(cy) == 1000000000 ||
        double(x1 - x0 + 1) * double(y1 - y0 + 1) > 100000) {
        for (const auto &point : all_)
            consider(point);
        return result;
    }
    // Round positive bounds toward zero, then use the same float norm as targeting.
    // Prune strictly farther cells only: equal rounded distances still need ID ties.
    auto lower = [](double d) { return std::nextafter(float(std::max(0.0, d)), 0.f); };
    auto visit = [&](int64_t x, int64_t y) {
        if (x < x0 || x > x1 || y < y0 || y > y1)
            return;
        double minx = double(x) * cell_m_, maxx = double(x + 1) * cell_m_,
               miny = double(y) * cell_m_, maxy = double(y + 1) * cell_m_;
        Vec3 bound{lower(std::max(minx - double(p.x), double(p.x) - maxx)),
                   lower(std::max(miny - double(p.y), double(p.y) - maxy)), 0};
        if (Length(bound) > best)
            return;
        auto cell = cells_.find(Key(x, y));
        if (cell != cells_.end())
            for (auto i : cell->second)
                consider(all_[i]);
    };
    int64_t last = std::max({cx - x0, x1 - cx, cy - y0, y1 - cy});
    for (int64_t ring = 0; ring <= last; ++ring) {
        if (!ring)
            visit(cx, cy);
        else {
            for (int64_t d = -ring; d <= ring; ++d) {
                visit(cx + d, cy - ring);
                visit(cx + d, cy + ring);
            }
            for (int64_t d = -ring + 1; d < ring; ++d) {
                visit(cx - ring, cy + d);
                visit(cx + ring, cy + d);
            }
        }
        double outside = std::min(
            {double(p.x) - double(cx - ring) * cell_m_, double(cx + ring + 1) * cell_m_ - p.x,
             double(p.y) - double(cy - ring) * cell_m_, double(cy + ring + 1) * cell_m_ - p.y});
        if (Length({lower(outside), 0, 0}) > best)
            break;
    }
    return result;
}
std::vector<UnitId> SpatialIndex::QueryRadius(Vec3 p, float radius) const {
    std::vector<UnitId> out;
    QueryRadiusInto(p, radius, out);
    return out;
}
void SpatialIndex::QueryRadiusInto(Vec3 p, float radius, std::vector<UnitId> &out) const {
    out.clear();
    if (!Finite(p) || !std::isfinite(radius) || radius < 0 || !std::isfinite(cell_m_) ||
        cell_m_ <= 0)
        return;
    auto x0 = Cell(p.x - radius, cell_m_), x1 = Cell(p.x + radius, cell_m_),
         y0 = Cell(p.y - radius, cell_m_), y1 = Cell(p.y + radius, cell_m_);
    auto test = [&](size_t i) {
        auto d = all_[i].position - p;
        if (double(d.x) * d.x + double(d.y) * d.y + double(d.z) * d.z <= double(radius) * radius)
            out.push_back(all_[i].id);
    };
    if (double(x1 - x0 + 1) * double(y1 - y0 + 1) > 100000) {
        for (size_t i = 0; i < all_.size(); ++i)
            test(i);
    } else
        for (auto x = x0; x <= x1; ++x)
            for (auto y = y0; y <= y1; ++y) {
                auto i = cells_.find(Key(x, y));
                if (i != cells_.end())
                    for (auto n : i->second)
                        test(n);
            }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}
} // namespace battle
