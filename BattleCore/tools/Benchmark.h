#pragma once
#include "battle/Scenario.h"
#include <string>
namespace battle::io {
struct BenchmarkResult {
    std::string mode;
    uint32_t units{}, workers{}, ticks{}, warmup{};
    uint64_t seed{42}, hash{}, neighbor_checks{}, events{}, working_set_bytes{},
        peak_working_set_bytes{};
    double p50_ms{}, p95_ms{}, p99_ms{};
    std::vector<double> step_ms;
};
Scenario MakeBenchmarkScenario(const std::string &mode, uint32_t units);
BenchmarkResult RunBenchmark(const std::string &mode, uint32_t units, uint32_t ticks,
                             uint32_t warmup, uint32_t workers);
} // namespace battle::io
