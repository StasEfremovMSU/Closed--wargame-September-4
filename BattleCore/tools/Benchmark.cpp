#include "Benchmark.h"
#include "battle/Simulation.h"
#include "battle/Snapshot.h"
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <psapi.h>
#endif
namespace battle::io {
Scenario MakeBenchmarkScenario(const std::string &mode, uint32_t count) {
    if (count < 2 || count > 100000)
        throw std::invalid_argument("benchmark units must be 2..100000");
    if (mode != "march" && mode != "reform" && mode != "bottleneck" && mode != "volley" &&
        mode != "melee")
        throw std::invalid_argument("unknown benchmark mode");
    Scenario s;
    bool battle = mode == "volley" || mode == "melee";
    uint32_t regs = (count + 99) / 100;
    for (uint32_t i = 0; i < regs; ++i) {
        RegimentSpec r;
        r.id = {i + 1};
        r.team = battle ? i % 2 : 0;
        r.formation = {10, 1.2f, .3f};
        r.speed = 4;
        if (battle)
            r.pose = {{i % 2 ? (mode == "melee" ? 5.7f : 12.f) : (mode == "melee" ? -5.7f : -12.f),
                       float(i / 2) * 32, 0},
                      i % 2 ? Pi : 0};
        else
            r.pose = {{-220.f + float(i / 20) * 20, -200.f + float(i % 20) * 20, 0}, 0};
        r.rally_point = r.pose.position + Vec3{r.team ? 100.f : -100.f, 0, 0};
        s.regiments.push_back(r);
        for (uint32_t j = 0; j < 100 && s.units.size() < count; ++j) {
            UnitSpec u;
            u.id = {s.units.size() + 1};
            u.regiment = r.id;
            u.pose = r.pose;
            u.health = 100;
            u.speed = 4;
            u.archer = mode == "volley";
            u.weapon.range = u.archer ? 35 : 1.5f;
            u.weapon.base_accuracy = .8f;
            u.archer_skill = 1;
            s.units.push_back(u);
        }
        if (battle)
            s.officers.push_back({{i + 1}, {i * 100 + 1}, 2, 0, .5f, 0, 0});
    }
    if (mode == "bottleneck") {
        for (int row = 0; row < 21; ++row) {
            float y = -210.f + row * 20;
            s.obstacles.push_back({0, y - 2, 2, y + 2});
        }
    }
    return s;
}
BenchmarkResult RunBenchmark(const std::string &mode, uint32_t units, uint32_t ticks,
                             uint32_t warmup, uint32_t workers) {
    if (!ticks || !workers)
        throw std::invalid_argument("ticks and workers must be positive");
    auto made = CreateWorld(MakeBenchmarkScenario(mode, units), 42);
    if (!made)
        throw std::runtime_error("invalid benchmark scenario");
    auto w = std::move(made.value);
    FlatNavigationService flat;
    GridNavigationService grid(w.scenario.obstacles);
    const NavigationService &nav = w.scenario.obstacles.empty()
                                       ? static_cast<const NavigationService &>(flat)
                                       : static_cast<const NavigationService &>(grid);
    std::vector<CommandEnvelope> initial;
    uint64_t seq = 1;
    if (mode == "march" || mode == "reform" || mode == "bottleneck")
        for (auto &r : w.regiments) {
            Command c;
            c.kind = CommandKind::IssueOrder;
            c.order.recipient = r.id;
            c.order.type = OrderType::MoveTo;
            c.order.point = {mode == "bottleneck" ? 200.f : 1000.f, r.pose.position.y, 0};
            initial.push_back({0, seq++, c});
        }
    BenchmarkResult result;
    result.mode = mode;
    result.units = units;
    result.workers = workers;
    result.ticks = ticks;
    result.warmup = warmup;
    result.step_ms.reserve(ticks);
    uint64_t start_checks = 0;
    for (uint64_t t = 0; t < uint64_t(ticks) + warmup; ++t) {
        std::vector<CommandEnvelope> input = t == 0 ? initial : std::vector<CommandEnvelope>{};
        if (mode == "reform" && t % 100 == 0)
            for (auto &r : w.regiments) {
                Command c;
                c.kind = CommandKind::ReformRegiment;
                c.regiment = r.id;
                c.formation = {t % 200 ? 5u : 10u, 1.2f, .3f};
                input.push_back({w.tick, seq++, c});
            }
        if (t == warmup)
            start_checks = w.neighbor_checks;
        auto before = std::chrono::steady_clock::now();
        auto step = Step(w, input, {50, workers}, nav);
        auto after = std::chrono::steady_clock::now();
        if (!step)
            throw std::runtime_error("benchmark Step failed");
        if (t >= warmup) {
            result.step_ms.push_back(
                std::chrono::duration<double, std::milli>(after - before).count());
            result.events += step.value.events.size();
        }
    }
    result.neighbor_checks = w.neighbor_checks - start_checks;
    result.hash = StateHash(w);
    auto sorted = result.step_ms;
    std::sort(sorted.begin(), sorted.end());
    auto percentile = [&](double q) { return sorted[size_t(std::ceil(q * sorted.size())) - 1]; };
    result.p50_ms = percentile(.5);
    result.p95_ms = percentile(.95);
    result.p99_ms = percentile(.99);
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        result.working_set_bytes = memory.WorkingSetSize;
        result.peak_working_set_bytes = memory.PeakWorkingSetSize;
    }
#endif
    return result;
}
} // namespace battle::io
