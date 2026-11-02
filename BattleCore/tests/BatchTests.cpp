#include "Benchmark.h"
#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Snapshot.h"
#include "battle/Spatial.h"
#include "doctest.h"
using namespace battle;
TEST_CASE("batch.benchmark_scenarios_and_statistics") {
    for (auto mode : {"march", "reform", "bottleneck", "volley", "melee"}) {
        auto s = io::MakeBenchmarkScenario(mode, 1000);
        auto world = CreateWorld(s, 42);
        REQUIRE(world);
        CHECK(GetStats(world.value).living_units == 1000);
    }
    auto result = io::RunBenchmark("melee", 200, 25, 2, 1);
    CHECK(result.p50_ms >= 0);
    CHECK(result.p99_ms >= result.p50_ms);
    CHECK(result.neighbor_checks > 0);
    CHECK(result.events > 0);
    CHECK_THROWS(io::MakeBenchmarkScenario("bad", 1000));
}

TEST_CASE("batch.workers_input_order_and_mass_deaths") {
    auto s = io::MakeBenchmarkScenario("volley", 200);
    auto reversed = s;
    std::reverse(reversed.units.begin(), reversed.units.end());
    std::reverse(reversed.regiments.begin(), reversed.regiments.end());
    std::reverse(reversed.officers.begin(), reversed.officers.end());
    FlatNavigationService nav;
    auto baseline = CreateWorld(s, 42).value;
    for (unsigned workers : {1, 2, 4}) {
        auto a = CreateWorld(reversed, 42).value;
        auto b = baseline;
        for (int tick = 0; tick < 30; ++tick) {
            REQUIRE(Step(a, {}, {50, workers}, nav));
            REQUIRE(Step(b, {}, {50, 1}, nav));
        }
        CHECK(StateHash(a) == StateHash(b));
        std::vector<BattleEvent> deaths;
        for (auto &u : a.units) {
            BattleEvent e;
            e.id = {a.next_event_id + deaths.size()};
            e.kind = EventKind::AttackLaunched;
            e.source = {u.id.value <= 100 ? 101u : 1u};
            e.target = u.id;
            e.amount = 1000;
            e.hit = true;
            e.projectile = true;
            e.due_tick = a.tick + 1;
            deaths.push_back(e);
        }
        CommitCombatEvents(a, deaths);
        std::reverse(deaths.begin(), deaths.end());
        CommitCombatEvents(b, deaths);
        CHECK(GetStats(a).living_units == 0);
        CHECK(a.outcome == BattleOutcome::Draw);
        CHECK(StateHash(a) == StateHash(b));
    }
}

TEST_CASE("batch.spatial_output_buffer_reuse") {
    SpatialIndex index;
    std::vector<UnitPosition> positions{{{7}, {0, 0, 0}}, {{2}, {1, 0, 0}}, {{11}, {30, 0, 0}}};
    index.Rebuild(positions);
    std::vector<UnitId> results{{999}};
    index.QueryRadiusInto({0, 0, 0}, 2, results);
    REQUIRE(results.size() == 2);
    CHECK(results[0] == UnitId{2});
    CHECK(results[1] == UnitId{7});
    index.QueryRadiusInto({100, 0, 0}, 1, results);
    CHECK(results.empty());
}
