#pragma once
#include "battle/Simulation.h"
#include <filesystem>
#include <fstream>

namespace battle::io {
struct BattleTraceConfig {
    std::optional<NavigationGridGeometry> grid;
    std::string group_policy;
    bool group_order_commitment{};
};

struct BattleTraceOutcome {
    BattleOutcome outcome{};
    int32_t winner{};
};

struct BattleTraceSummary {
    Tick tick{};
    uint64_t hash{};
    size_t stall_samples{};
    bool complete{};
};

class BattleTraceWriter {
  public:
    Result<bool> Begin(const std::filesystem::path &directory, const World &initial,
                       const BattleTraceConfig &);
    Result<bool> Record(std::span<const CommandEnvelope> commands, const World &after,
                        std::span<const Order> host_orders = {},
                        std::optional<BattleTraceOutcome> mission_result = std::nullopt);
    Result<bool> Finish(const World &, std::span<const Order> host_orders = {});

  private:
    struct Sample {
        RegimentId id{};
        Pose pose{};
        size_t waypoint{};
        uint32_t still_seconds{};
    };
    std::ofstream ticks_;
    std::vector<Sample> samples_;
    Tick tick_{}, sample_tick_{};
};

Result<BattleTraceSummary> ReplayBattleTrace(const std::filesystem::path &directory);
} // namespace battle::io
