#include "BattleTrace.h"
#include "JsonBindings.h"
#include "ReplayIo.h"
#include "battle/Formation.h"
#include "battle/Orders.h"
#include "battle/command/GroupPolicy.h"
#include <memory>
#include <sstream>

namespace battle::io {
namespace {
using Json = nlohmann::json;
constexpr Tick SampleInterval = 20;

template<class T>
Result<T> TraceError(std::string message) {
    return {Status::InvalidInput, {}, {{"battle trace", 0, std::move(message)}}};
}

bool ValidGrid(const NavigationGridGeometry &grid) {
    return std::isfinite(grid.cell_m) && grid.cell_m >= .1f && grid.cell_m <= 100.f &&
           grid.width > 0 && grid.height > 0 && grid.width <= 2048 && grid.height <= 2048 &&
           Finite(grid.origin);
}

bool ExpectsMovement(const World &world, const RegimentState &regiment) {
    if (regiment.units.empty() || regiment.melee_engaged || IsDeploying(world, regiment))
        return false;
    if (regiment.waypoint < regiment.route.size() || regiment.retreat_waypoint)
        return true;
    return regiment.order && regiment.order->status == OrderStatus::Active &&
           (regiment.order->type == OrderType::MoveTo || IsWithdrawing(regiment.order->type));
}

const char *StallReason(const World &world, const RegimentState &regiment) {
    if (regiment.panic || regiment.retreat_waypoint)
        return "retreat_stalled";
    if (regiment.waypoint >= regiment.route.size())
        return "active_order_without_route";
    Pose destination = regiment.route[regiment.waypoint];
    if (const auto &lane = regiment.march_lane; lane && lane->next < lane->detour.size())
        destination = {lane->detour[lane->next], regiment.pose.yaw};
    const bool clear = CanContinueFormationMarch(world, regiment, destination) &&
                       FormationRouteSegmentClear(world, regiment, destination);
    return clear ? "stationary_on_clear_route" : "blocked_route_segment";
}
} // namespace

Result<bool> BattleTraceWriter::Begin(const std::filesystem::path &directory, const World &initial,
                                     const BattleTraceConfig &config) {
    if (ticks_.is_open())
        return TraceError<bool>("a recording is already open");
    if (config.grid && !ValidGrid(*config.grid))
        return TraceError<bool>("invalid navigation grid");
    try {
        const auto saved = SaveSnapshot(directory / "start.json", Capture(initial));
        if (!saved)
            return saved;
        Json header = {{"version", 1}, {"start_tick", initial.tick}, {"seed", initial.seed},
                       {"group_policy", config.group_policy},
                       {"group_order_commitment", config.group_order_commitment}};
        if (config.grid) {
            const auto &grid = *config.grid;
            header["grid"] = {{"cell_m", grid.cell_m}, {"width", grid.width},
                              {"height", grid.height}, {"origin", grid.origin}};
        }
        ticks_.open(directory / "ticks.jsonl", std::ios::binary | std::ios::trunc);
        ticks_ << header.dump() << '\n' << std::flush;
        if (!ticks_)
            return TraceError<bool>("could not write the recording header");
        tick_ = sample_tick_ = initial.tick;
        samples_.clear();
        samples_.reserve(initial.regiments.size());
        for (const auto &regiment : initial.regiments)
            samples_.push_back({regiment.id, regiment.pose, regiment.waypoint, 0});
        return {Status::Ok, true, {}};
    } catch (const std::exception &error) {
        return TraceError<bool>(error.what());
    }
}

Result<bool> BattleTraceWriter::Record(std::span<const CommandEnvelope> commands, const World &after,
                                      std::span<const Order> host_orders,
                                      std::optional<BattleTraceOutcome> mission_result) {
    if (!ticks_.is_open() || after.tick != tick_ + 1)
        return TraceError<bool>("recording expected the next committed tick");
    try {
        Json line = {{"tick", after.tick}, {"commands", commands}};
        if (!host_orders.empty())
            line["host_orders"] = host_orders;
        if (mission_result)
            line["mission_result"] = {{"outcome", uint8_t(mission_result->outcome)},
                                      {"winner", mission_result->winner}};
        const bool sample = after.tick - sample_tick_ >= SampleInterval;
        if (sample) {
            Json stalls = Json::array();
            for (auto &entry : samples_) {
                const auto *regiment = FindRegiment(after, entry.id);
                if (!regiment)
                    continue;
                const bool holding_still = Distance(entry.pose.position, regiment->pose.position) < .15f &&
                    std::abs(std::remainder(regiment->pose.yaw - entry.pose.yaw, 2.f * Pi)) < .08f &&
                    entry.waypoint == regiment->waypoint;
                entry.still_seconds = holding_still && ExpectsMovement(after, *regiment)
                    ? entry.still_seconds + 1 : 0;
                entry.pose = regiment->pose;
                entry.waypoint = regiment->waypoint;
                if (entry.still_seconds < 2)
                    continue;
                Json stall = {{"regiment", regiment->id.value}, {"team", regiment->team},
                              {"position", regiment->pose.position}, {"seconds", entry.still_seconds},
                              {"reason", StallReason(after, *regiment)}, {"waypoint", regiment->waypoint},
                              {"route_points", regiment->route.size()}, {"ai_action", regiment->ai_action}};
                if (regiment->order)
                    stall["order"] = *regiment->order;
                stalls.push_back(std::move(stall));
            }
            if (!stalls.empty())
                line["stalls"] = std::move(stalls);
            line["hash"] = StateHash(after);
            sample_tick_ = after.tick;
        }
        ticks_ << line.dump() << '\n';
        if (sample)
            ticks_.flush();
        if (!ticks_)
            return TraceError<bool>("could not write the recorded tick");
        tick_ = after.tick;
        return {Status::Ok, true, {}};
    } catch (const std::exception &error) {
        return TraceError<bool>(error.what());
    }
}

Result<bool> BattleTraceWriter::Finish(const World &world, std::span<const Order> host_orders) {
    if (!ticks_.is_open() || world.tick != tick_)
        return TraceError<bool>("recording end does not match the last committed tick");
    try {
        Json final = {{"end", world.tick}, {"hash", StateHash(world)}};
        if (!host_orders.empty())
            final["host_orders"] = host_orders;
        ticks_ << final.dump() << '\n';
        ticks_.close();
        return ticks_ ? Result<bool>{Status::Ok, true, {}}
                      : TraceError<bool>("could not finish the battle recording");
    } catch (const std::exception &error) {
        return TraceError<bool>(error.what());
    }
}

Result<BattleTraceSummary> ReplayBattleTrace(const std::filesystem::path &directory) {
    const auto loaded = LoadSnapshot(directory / "start.json");
    if (!loaded)
        return {loaded.status, {}, loaded.diagnostics};
    auto restored = Restore(loaded.value);
    if (!restored)
        return {restored.status, {}, restored.diagnostics};
    auto &world = restored.value;
    try {
        std::ifstream stream(directory / "ticks.jsonl", std::ios::binary);
        std::string text;
        if (!std::getline(stream, text))
            return TraceError<BattleTraceSummary>("missing recording header");
        const auto header = Json::parse(text);
        if (header.at("version") != 1 || header.at("start_tick") != world.tick ||
            header.at("seed") != world.seed)
            return TraceError<BattleTraceSummary>("recording does not match the starting snapshot");

        FlatNavigationService flat;
        std::optional<GridNavigationService> grid;
        if (header.contains("grid")) {
            const auto &value = header.at("grid");
            const NavigationGridGeometry geometry{value.at("cell_m").get<float>(),
                uint32_t(ReadJsonUnsigned(value.at("width"), 2048, "invalid grid width")),
                uint32_t(ReadJsonUnsigned(value.at("height"), 2048, "invalid grid height")),
                value.at("origin").get<Vec3>()};
            if (!ValidGrid(geometry))
                return TraceError<BattleTraceSummary>("invalid navigation grid");
            grid.emplace(world.scenario.obstacles, geometry.cell_m, geometry.width, geometry.height,
                         geometry.origin);
        }
        const auto &nav = grid ? static_cast<const NavigationService &>(*grid)
                               : static_cast<const NavigationService &>(flat);
        const auto policy_text = header.at("group_policy").get<std::string>();
        std::optional<command::GroupPolicy> policy;
        if (!policy_text.empty()) {
            std::istringstream weights(policy_text);
            policy = command::LoadGroupPolicy(weights);
            if (!policy)
                return TraceError<BattleTraceSummary>("invalid recorded group policy");
        }
        const SimulationOptions options{50, 1, policy ? &*policy : nullptr,
                                        header.at("group_order_commitment").get<bool>()};
        BattleTraceSummary summary;
        while (std::getline(stream, text)) {
            // A process interruption may leave a partial last line. Complete rows are newline-terminated.
            if (stream.eof())
                break;
            if (summary.complete)
                return TraceError<BattleTraceSummary>("unexpected input after the final tick");
            const auto line = Json::parse(text);
            const Tick expected = line.contains("end") ? world.tick : world.tick + 1;
            if (line.at(line.contains("end") ? "end" : "tick") != expected)
                return TraceError<BattleTraceSummary>("missing or out-of-order recorded tick");
            if (line.contains("host_orders"))
                for (const auto &order : line.at("host_orders").get<std::vector<Order>>())
                    if (SubmitOrder(world, order) != Status::Ok)
                        return TraceError<BattleTraceSummary>("a recorded host order was rejected");
            if (line.contains("end")) {
                if (line.at("hash") != StateHash(world))
                    return TraceError<BattleTraceSummary>("final state mismatch at tick " + std::to_string(world.tick));
                summary.complete = true;
                continue;
            }
            const auto commands = line.at("commands").get<std::vector<CommandEnvelope>>();
            const auto advanced = Step(world, commands, options, nav);
            if (!advanced)
                return {advanced.status, {}, advanced.diagnostics};
            if (line.contains("mission_result")) {
                const auto &result = line.at("mission_result");
                world.outcome = BattleOutcome(ReadJsonUnsigned(result.at("outcome"),
                    uint8_t(BattleOutcome::Draw), "invalid mission outcome"));
                world.winner_team = result.at("winner").get<int32_t>();
            }
            if (line.contains("hash") && line.at("hash") != StateHash(world))
                return TraceError<BattleTraceSummary>("state mismatch at tick " + std::to_string(world.tick));
            if (line.contains("stalls"))
                summary.stall_samples += line.at("stalls").size();
        }
        if (stream.bad())
            return TraceError<BattleTraceSummary>("could not read recorded ticks");
        summary.tick = world.tick;
        summary.hash = StateHash(world);
        return {Status::Ok, summary, {}};
    } catch (const std::exception &error) {
        return TraceError<BattleTraceSummary>(error.what());
    }
}
} // namespace battle::io
