#include "ReplayIo.h"
#include "JsonBindings.h"
#include "battle/Simulation.h"
#include <chrono>
#include <fstream>
namespace battle::io {
namespace {
using Json = nlohmann::json;
void Keys(const Json &j, std::initializer_list<const char *> keys) {
    if (!j.is_object())
        throw std::invalid_argument("expected object");
    for (auto key : keys)
        if (!j.contains(key))
            throw std::invalid_argument(std::string("missing snapshot field: ") + key);
}
// Scenario authoring permits defaults; continuation snapshots must be complete.
void CompleteFields(const Json &actual, const Json &persisted, const std::string &path) {
    if (persisted.is_object()) {
        if (!actual.is_object())
            throw std::invalid_argument("expected snapshot object: " + path);
        if (actual.size() != persisted.size())
            throw std::invalid_argument("unexpected snapshot fields: " + path);
        for (auto it = persisted.begin(); it != persisted.end(); ++it) {
            if (!actual.contains(it.key()))
                throw std::invalid_argument("missing snapshot field: " + path + "/" + it.key());
            CompleteFields(actual.at(it.key()), it.value(), path + "/" + it.key());
        }
    } else if (persisted.is_array()) {
        if (!actual.is_array() || actual.size() != persisted.size())
            throw std::invalid_argument("invalid snapshot array: " + path);
        for (size_t i = 0; i < persisted.size(); ++i)
            CompleteFields(actual[i], persisted[i], path + "/" + std::to_string(i));
    } else if (persisted.is_number_unsigned()) {
        if (!actual.is_number_unsigned())
            throw std::invalid_argument("expected unsigned integer: " + path);
    } else if (persisted.is_number_integer()) {
        if (!actual.is_number_integer())
            throw std::invalid_argument("expected integer: " + path);
    } else if (persisted.is_number_float()) {
        if (!actual.is_number())
            throw std::invalid_argument("expected number: " + path);
    } else if (persisted.is_boolean() && !actual.is_boolean()) {
        throw std::invalid_argument("expected boolean: " + path);
    } else if (persisted.is_string() && !actual.is_string()) {
        throw std::invalid_argument("expected string: " + path);
    }
}
Json Version2SnapshotShape(const Snapshot &s) {
    Json old = s;
    old["schema_version"] = 2;
    old["state"].erase("command_state");
    old["state"]["scenario"].erase("command_setup");
    for (auto &entry : old["state"]["commands"])
        entry["command"].erase("command_task");
    return old;
}
Json Version2ReplayShape(const ReplayRecord &r) {
    Json old{{"schema_version", 2}, {"scenario", r.scenario}, {"seed", r.seed},
             {"commands", r.commands}};
    old["scenario"].erase("command_setup");
    for (auto &entry : old["commands"])
        entry["command"].erase("command_task");
    return old;
}
Result<bool> Write(const std::filesystem::path &p, const Json &j) {
    std::filesystem::path temp = p;
    temp += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());
        {
            std::ofstream f(temp);
            if (!f)
                throw std::runtime_error("cannot open output");
            f << j.dump(2);
            f.close();
            if (!f)
                throw std::runtime_error("write failed");
        }
        std::filesystem::rename(temp, p);
        return {Status::Ok, true, {}};
    } catch (const std::exception &e) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return {Status::InvalidInput, false, {{p.string(), 0, e.what()}}};
    }
}
Json Read(const std::filesystem::path &p) {
    std::ifstream f(p);
    if (!f)
        throw std::runtime_error("cannot open input");
    Json j;
    f >> j;
    return j;
}
} // namespace
Result<bool> SaveSnapshot(const std::filesystem::path &p, const Snapshot &s) {
    auto r = Restore(s);
    if (!r)
        return {r.status, false, r.diagnostics};
    return Write(p, Json(Snapshot{3, std::move(r.value)}));
}
Result<Snapshot> LoadSnapshot(const std::filesystem::path &p) {
    try {
        auto j = Read(p);
        Keys(j, {"schema_version", "state"});
        const auto version = j.at("schema_version").get<uint32_t>();
        if (version != 2 && version != 3)
            throw std::invalid_argument(
                "unsupported snapshot version; this build requires version 3");
        const auto &w = j.at("state");
        if (version == 2)
            Keys(w, {"tick", "seed", "next_event_id", "next_order_id", "last_launch_id", "scenario",
                     "units", "regiments", "officers", "attacks", "pending_orders", "commands",
                     "accepted_sequences", "controlled", "outcome", "winner_team"});
        else
            Keys(w, {"tick", "seed", "next_event_id", "next_order_id", "last_launch_id", "scenario",
                     "units", "regiments", "officers", "attacks", "pending_orders", "commands",
                     "accepted_sequences", "controlled", "outcome", "winner_team", "command_state"});
        for (const auto &u : w.at("units"))
            Keys(u, {"id", "regiment", "pose", "health", "alive", "control", "slot", "slot_index",
                     "ready_tick", "desired_velocity", "attack_target", "speed", "armour",
                     "archer_skill", "archer", "equipment", "weapon"});
        for (const auto &r : w.at("regiments"))
            Keys(r, {"id",
                     "team",
                     "pose",
                     "formation",
                     "rally_point",
                     "speed",
                     "turn_rate",
                     "units",
                     "slots",
                     "route",
                     "waypoint",
                     "order",
                     "initial_count",
                     "panic",
                     "calm_counter",
                     "last_damage_tick",
                     "panic_loss_count",
                     "decision_tick",
                     "decision_until",
                     "ai_action",
                     "ai_target"});
        Snapshot s = j.get<Snapshot>();
        CompleteFields(j, version == 2 ? Version2SnapshotShape(s) : Json(s), "snapshot");
        auto r = Restore(s);
        if (!r)
            return {r.status, {}, r.diagnostics};
        s.state = std::move(r.value);
        s.schema_version = 3;
        return {Status::Ok, std::move(s), {}};
    } catch (const std::exception &e) {
        return {Status::InvalidInput, {}, {{p.string(), 0, e.what()}}};
    }
}
Result<bool> WriteReplay(const std::filesystem::path &p, const Scenario &s, uint64_t seed,
                         std::span<const CommandEnvelope> commands) {
    auto valid = ValidateScenario(s);
    if (!valid)
        return {valid.status, false, valid.diagnostics};
    Json j{{"schema_version", 3},
           {"scenario", s},
           {"seed", seed},
           {"commands", std::vector<CommandEnvelope>(commands.begin(), commands.end())}};
    return Write(p, j);
}
Result<ReplayRecord> LoadReplay(const std::filesystem::path &p) {
    try {
        auto j = Read(p);
        Keys(j, {"schema_version", "scenario", "seed", "commands"});
        const auto version = j.at("schema_version").get<uint32_t>();
        if (version != 2 && version != 3)
            throw std::invalid_argument("unsupported replay version");
        ReplayRecord r;
        r.scenario = j.at("scenario").get<Scenario>();
        r.seed = j.at("seed");
        r.commands = j.at("commands").get<std::vector<CommandEnvelope>>();
        if (version == 2 &&
            (r.scenario.command_setup || std::any_of(r.commands.begin(), r.commands.end(),
                [](const auto &e) { return e.command.kind == CommandKind::IssueCommandTask; })))
            throw std::invalid_argument("version 2 replay cannot contain command hierarchy state");
        r.schema_version = 3;
        CompleteFields(j, version == 2 ? Version2ReplayShape(r) :
                       Json{{"schema_version", 3}, {"scenario", r.scenario}, {"seed", r.seed},
                            {"commands", r.commands}}, "replay");
        auto world = CreateWorld(r.scenario, r.seed);
        if (!world)
            return {world.status, {}, world.diagnostics};
        FlatNavigationService nav;
        auto checked = Step(world.value, r.commands, {}, nav);
        if (!checked)
            return {checked.status, {}, checked.diagnostics};
        return {Status::Ok, std::move(r), {}};
    } catch (const std::exception &e) {
        return {Status::InvalidInput, {}, {{p.string(), 0, e.what()}}};
    }
}
} // namespace battle::io
