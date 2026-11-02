#include "ScenarioJson.h"
#include "JsonBindings.h"
#include <fstream>
namespace battle::io {
Result<Scenario> LoadScenario(const std::filesystem::path &p) {
    try {
        std::ifstream f(p);
        if (!f)
            return {Status::InvalidInput, {}, {{p.string(), 0, "cannot open scenario"}}};
        nlohmann::json j;
        f >> j;
        if (!j.is_object() || !j.contains("schema_version") || !j.contains("units") ||
            !j.contains("regiments"))
            throw std::invalid_argument("scenario requires schema_version, units and regiments");
        auto input = j.get<Scenario>();
        if (j.contains("command_setup") && !j.at("command_setup").is_null())
            command::ValidateCommandInputTypes(j.at("command_setup"),
                                               nlohmann::json(*input.command_setup),
                                               "scenario/command_setup");
        auto r = ValidateScenario(std::move(input));
        for (auto &d : r.diagnostics)
            d.source = p.string();
        return r;
    } catch (const std::exception &e) {
        return {Status::InvalidInput, {}, {{p.string(), 0, e.what()}}};
    }
}
Result<bool> SaveScenario(const std::filesystem::path &p, const Scenario &s) {
    try {
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());
        std::ofstream f(p);
        if (!f)
            throw std::runtime_error("cannot write scenario");
        f << nlohmann::json(s).dump(2);
        if (!f)
            throw std::runtime_error("scenario write failed");
        return {Status::Ok, true, {}};
    } catch (const std::exception &e) {
        return {Status::InvalidInput, false, {{p.string(), 0, e.what()}}};
    }
}
} // namespace battle::io
