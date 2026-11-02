#pragma once
#include "battle/Scenario.h"
#include <filesystem>
namespace battle::io {
struct LegacyImportOptions {
    std::filesystem::path map_file;
    float scale_m_per_unit{1};
};
Result<Scenario> ImportLegacy(const std::filesystem::path &, const LegacyImportOptions &);
} // namespace battle::io
