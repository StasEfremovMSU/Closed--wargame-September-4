#include "BattleTrace.h"
#include "Benchmark.h"
#include "JsonBindings.h"
#include "LegacyImport.h"
#include "ReplayIo.h"
#include "ScenarioJson.h"
#include "SvgExport.h"
#include "battle/Simulation.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
using namespace battle;
namespace {
std::string CompilerVersion() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#else
    return __VERSION__;
#endif
}
uint64_t Counter(const std::string &token, uint64_t max = std::numeric_limits<uint64_t>::max()) {
    uint64_t value{};
    auto end = token.data() + token.size();
    auto result = std::from_chars(token.data(), end, value);
    if (token.empty() || result.ec != std::errc{} || result.ptr != end || value > max)
        throw std::invalid_argument("invalid unsigned decimal counter: " + token);
    return value;
}
template <class T> bool Report(const Result<T> &r) {
    for (auto &d : r.diagnostics)
        std::cerr << d.source << ":" << d.line << ": " << d.message << "\n";
    return bool(r);
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            std::cout << "battle_cli import-legacy|run|replay|trace-replay; --scenario FILE --ticks N --seed N "
                         "--out DIR\n";
            return 2;
        }
        std::string mode = argv[1];
        std::map<std::string, std::string> args;
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc)
                throw std::invalid_argument("option requires value");
            args[argv[i]] = argv[i + 1];
        }
        auto required = [&](std::string key) {
            if (!args.contains(key))
                throw std::invalid_argument("missing " + key);
            return args.at(key);
        };
        std::map<std::string, uint64_t> counters;
        for (auto key : {"--ticks", "--seed", "--capture-every", "--warmup", "--workers",
                         "--repetitions", "--ranged-cohorts"})
            if (args.contains(key))
                counters[key] = Counter(args.at(key));
        if (args.contains("--resume") && args.contains("--replay-out"))
            throw std::invalid_argument("--resume with --replay-out is unsupported; continue using "
                                        "snapshots or replay the original command log");
        if (mode == "trace-replay") {
            const auto replay = io::ReplayBattleTrace(required("--trace"));
            if (!Report(replay))
                return 2;
            std::cout << "tick=" << replay.value.tick << " hash=" << replay.value.hash
                      << " stall_samples=" << replay.value.stall_samples
                      << " complete=" << replay.value.complete << "\n";
            return 0;
        }
        if (mode == "import-legacy") {
            auto r = io::ImportLegacy(required("--source"), {required("--map"), 1});
            if (!Report(r))
                return 2;
            if (!Report(io::SaveScenario(required("--out"), r.value)))
                return 2;
            auto stats = GetStats(CreateWorld(r.value, 42).value);
            std::cout << "Imported " << stats.regiment_count << " regiments, " << stats.living_units
                      << " units\n";
            return 0;
        }
        if (mode == "bench") {
            auto integer = [&](const std::string &key, unsigned fallback) {
                return args.contains(key)
                           ? unsigned(Counter(args.at(key), std::numeric_limits<unsigned>::max()))
                           : fallback;
            };
            unsigned ticks = integer("--ticks", 1200), warmup = integer("--warmup", 200),
                     workers = integer("--workers", 1), reps = integer("--repetitions", 3),
                     ranged = integer("--ranged-cohorts", Rules{}.ranged_cohorts);
            std::vector<std::string> modes;
            std::istringstream ms(args.contains("--modes")
                                      ? args["--modes"]
                                      : "march,reform,bottleneck,volley,melee");
            for (std::string item; std::getline(ms, item, ',');)
                modes.push_back(item);
            if (ranged == 0 || ranged > 64)
                throw std::invalid_argument("ranged-cohorts must be 1..64");
            if (!ticks || !workers || !reps)
                throw std::invalid_argument("ticks, workers and repetitions must be positive");
            for (const auto &name : modes)
                if (name != "march" && name != "reform" && name != "bottleneck" &&
                    name != "volley" && name != "melee")
                    throw std::invalid_argument("invalid benchmark mode");
            std::istringstream parse_sizes(required("--units"));
            std::vector<unsigned> unit_counts;
            for (std::string token; std::getline(parse_sizes, token, ',');) {
                auto count = Counter(token, 100000);
                if (count < 2)
                    throw std::invalid_argument("benchmark units must be 2..100000");
                unit_counts.push_back(unsigned(count));
            }
            if (unit_counts.empty() || modes.empty())
                throw std::invalid_argument("empty benchmark sizes/modes");
            std::filesystem::path dir = required("--out");
            std::filesystem::create_directories(dir);
            std::ofstream summary(dir / "summary.jsonl"), raw(dir / "ticks.csv");
            if (!summary || !raw)
                throw std::runtime_error("cannot write benchmark");
            raw << "mode,units,workers,rep,tick,step_ms,orders_ms,movement_ms,targeting_ms,commit_"
                   "ms\n";
            for (unsigned size : unit_counts)
                for (auto &mode : modes)
                    for (unsigned rep = 0; rep < reps; ++rep) {
                        auto r = io::RunBenchmark(mode, size, ticks, warmup, workers, ranged);
                        nlohmann::json j = {
                            {"mode", r.mode},
                            {"units", r.units},
                            {"workers_requested", r.workers},
                            {"workers_effective", 1},
                            {"ranged_cohorts", r.ranged_cohorts},
                            {"ticks", r.ticks},
                            {"warmup", r.warmup},
                            {"rep", rep},
                            {"seed", r.seed},
                            {"hash", r.hash},
                            {"p50_ms", r.p50_ms},
                            {"p95_ms", r.p95_ms},
                            {"p99_ms", r.p99_ms},
                            {"max_ms", r.max_ms},
                            {"orders_p99_ms", r.orders_p99_ms},
                            {"movement_p99_ms", r.movement_p99_ms},
                            {"targeting_p99_ms", r.targeting_p99_ms},
                            {"commit_p99_ms", r.commit_p99_ms},
                            {"neighbor_checks", r.neighbor_checks},
                            {"events", r.events},
                            {"launches", r.launches},
                            {"hits", r.hits},
                            {"misses", r.misses},
                            {"deaths", r.deaths},
                            {"max_launches_per_tick", r.max_launches_per_tick},
                            {"working_set_bytes", r.working_set_bytes},
                            {"process_peak_bytes", r.peak_working_set_bytes},
                            {"compiler", CompilerVersion()},
                            {"hardware_threads", std::thread::hardware_concurrency()}};
                        summary << j.dump() << "\n";
                        summary.flush();
                        for (size_t k = 0; k < r.step_ms.size(); ++k)
                            raw << mode << ',' << r.units << ',' << r.workers << ',' << rep << ','
                                << k + warmup << ',' << r.step_ms[k] << ',' << r.phases[k].orders_ms
                                << ',' << r.phases[k].movement_ms << ',' << r.phases[k].targeting_ms
                                << ',' << r.phases[k].commit_ms << "\n";
                        raw.flush();
                        std::cout << mode << " units=" << r.units << " rep=" << rep
                                  << " p95_ms=" << r.p95_ms << std::endl;
                    }
            return 0;
        }
        if (mode == "render") {
            auto loaded = io::LoadSnapshot(required("--snapshot"));
            if (!Report(loaded))
                return 2;
            const auto &scene = loaded.value.state.scenario;
            if (args.contains("--scenario")) {
                auto provided = io::LoadScenario(args["--scenario"]);
                if (!Report(provided))
                    return 2;
                auto canonical = CreateWorld(provided.value, loaded.value.state.seed);
                if (!canonical || nlohmann::json(canonical.value.scenario) != nlohmann::json(scene))
                    throw std::invalid_argument("scenario does not match snapshot");
            }
            std::filesystem::path file = required("--out");
            if (file.has_parent_path())
                std::filesystem::create_directories(file.parent_path());
            std::ofstream f(file);
            if (!f)
                throw std::runtime_error("cannot write SVG");
            f << io::ExportSvg(loaded.value, scene);
            std::cout << file.string() << "\n";
            return f ? 0 : 2;
        }
        if (mode != "run" && mode != "replay")
            throw std::invalid_argument("unknown mode");
        World w;
        std::vector<CommandEnvelope> commands;
        if (mode == "replay") {
            auto r = io::LoadReplay(required("--replay"));
            if (!Report(r))
                return 2;
            auto made = CreateWorld(r.value.scenario, r.value.seed);
            if (!Report(made))
                return 2;
            w = std::move(made.value);
            commands = std::move(r.value.commands);
        } else if (args.contains("--resume")) {
            auto loaded = io::LoadSnapshot(args["--resume"]);
            if (!Report(loaded))
                return 2;
            auto restored = Restore(loaded.value);
            if (!Report(restored))
                return 2;
            w = std::move(restored.value);
        } else {
            auto s = io::LoadScenario(required("--scenario"));
            if (!Report(s))
                return 2;
            auto made = CreateWorld(s.value, args.contains("--seed") ? counters.at("--seed") : 42);
            if (!Report(made))
                return 2;
            w = std::move(made.value);
        }
        if (args.contains("--commands")) {
            std::ifstream f(args["--commands"]);
            if (!f)
                throw std::runtime_error("cannot open commands");
            nlohmann::json j;
            f >> j;
            auto parsed = j.get<std::vector<CommandEnvelope>>();
            if (!j.is_array() || j.size() != parsed.size())
                throw std::invalid_argument("commands must be an array");
            for (size_t i = 0; i < parsed.size(); ++i)
                command::ValidateCommandInputTypes(j[i], nlohmann::json(parsed[i]),
                                                   "commands/" + std::to_string(i));
            commands = std::move(parsed);
        }
        if (args.contains("--replay-out") &&
            !Report(io::WriteReplay(args["--replay-out"], w.scenario, w.seed, commands)))
            return 2;
        FlatNavigationService flat;
        GridNavigationService grid(w.scenario.obstacles);
        const NavigationService &nav = w.scenario.obstacles.empty()
                                           ? static_cast<const NavigationService &>(flat)
                                           : static_cast<const NavigationService &>(grid);
        Tick ticks = args.contains("--ticks") ? counters.at("--ticks") : 100;
        std::filesystem::path out = required("--out");
        std::filesystem::create_directories(out);
        std::ofstream events(out / "events.jsonl");
        if (!events)
            throw std::runtime_error("cannot write events");
        Tick capture = args.contains("--capture-every") ? counters.at("--capture-every") : 0;
        for (Tick i = 0; i < ticks; ++i) {
            if (capture && w.tick % capture == 0 &&
                !Report(io::SaveSnapshot(out / "frames" / (std::to_string(w.tick) + ".json"),
                                         Capture(w))))
                return 2;
            auto r = Step(w,
                          i == 0 ? std::span<const CommandEnvelope>(commands)
                                 : std::span<const CommandEnvelope>{},
                          {}, nav);
            if (!Report(r))
                return 2;
            for (auto &e : r.value.events)
                events << nlohmann::json(e).dump() << "\n";
        }
        auto destination = args.contains("--snapshot-out")
                               ? std::filesystem::path(args["--snapshot-out"])
                               : out / "snapshot.json";
        if (!Report(io::SaveSnapshot(destination, Capture(w))))
            return 2;
        auto stats = GetStats(w);
        std::cout << "tick=" << w.tick << " living=" << stats.living_units
                  << " dead=" << stats.dead_units << " hash=" << StateHash(w)
                  << " outcome=" << unsigned(w.outcome) << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 2;
    }
}
