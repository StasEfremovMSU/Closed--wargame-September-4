#pragma once
#include "battle/Snapshot.h"
#include <filesystem>
namespace battle::io {
struct ReplayRecord {
    uint32_t schema_version{1};
    Scenario scenario;
    uint64_t seed{};
    std::vector<CommandEnvelope> commands;
};
Result<bool> SaveSnapshot(const std::filesystem::path &, const Snapshot &);
Result<Snapshot> LoadSnapshot(const std::filesystem::path &);
Result<bool> WriteReplay(const std::filesystem::path &, const Scenario &, uint64_t,
                         std::span<const CommandEnvelope>);
Result<ReplayRecord> LoadReplay(const std::filesystem::path &);
} // namespace battle::io
