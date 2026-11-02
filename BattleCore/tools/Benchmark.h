#pragma once
#include "battle/Simulation.h"
#include <string>
namespace battle::io {
struct BenchmarkResult {
    std::string mode;
    uint32_t units{}, workers{}, ticks{}, warmup{}, ranged_cohorts{};
    uint64_t seed{42}, hash{}, neighbor_checks{}, events{}, working_set_bytes{},
        peak_working_set_bytes{};
    uint64_t launches{}, hits{}, misses{}, deaths{}, max_launches_per_tick{};
    double p50_ms{}, p95_ms{}, p99_ms{}, max_ms{};
    double orders_p99_ms{}, movement_p99_ms{}, targeting_p99_ms{}, commit_p99_ms{};
    std::vector<StepProfile> phases;
    std::vector<double> step_ms;
};
Scenario MakeBenchmarkScenario(const std::string &mode, uint32_t units);
BenchmarkResult RunBenchmark(const std::string &mode, uint32_t units, uint32_t ticks,
                             uint32_t warmup, uint32_t workers, uint32_t ranged_cohorts = 0);
} // namespace battle::io
