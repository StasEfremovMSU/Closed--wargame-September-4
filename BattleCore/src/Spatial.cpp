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
    for (size_t i = 0; i < all_.size(); ++i)
        if (Finite(all_[i].position))
            cells_[Key(Cell(all_[i].position.x, cell_m_), Cell(all_[i].position.y, cell_m_))]
                .push_back(i);
}
std::vector<UnitId> SpatialIndex::QueryRadius(Vec3 p, float radius) const {
    std::vector<UnitId> out;
    QueryRadiusInto(p, radius, out);
    return out;
}
void SpatialIndex::QueryRadiusInto(Vec3 p, float radius, std::vector<UnitId> &out) const {
    out.clear();
    if (!Finite(p) || !std::isfinite(radius) || radius < 0)
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
