#include "Benchmark.h"
#include "ReplayIo.h"
#include "TestSupport.h"
#include "battle/Presentation.h"
#include "doctest.h"
#include <map>
#include <set>
using namespace battle;
namespace {
struct MockAdapter {
    struct Body {
        UnitId owner{};
        uint32_t generation{};
        bool used{};
    };
    std::vector<Body> pool;
    std::map<uint64_t, size_t> by_id;
    std::set<uint64_t> events;
    Tick tick{};
    void Despawn(UnitId id) {
        auto it = by_id.find(id.value);
        if (it == by_id.end())
            return;
        pool[it->second].used = false;
        pool[it->second].owner = {};
        by_id.erase(it);
    }
    void Consume(const PresentationFrame &frame) {
        if (frame.tick < tick)
            return;
        tick = frame.tick;
        for (const auto &u : frame.units) {
            if (!u.alive) {
                Despawn(u.id);
                continue;
            }
            if (by_id.contains(u.id.value))
                continue;
            size_t slot = 0;
            while (slot < pool.size() && pool[slot].used)
                ++slot;
            if (slot == pool.size())
                pool.push_back({});
            auto &body = pool[slot];
            body.owner = u.id;
            body.used = true;
            ++body.generation;
            by_id[u.id.value] = slot;
        }
        for (const auto &e : frame.events)
            events.insert(e.id.value);
    }
};
} // namespace
TEST_CASE("integration.lod_pooling_events_and_owned_frame") {
    auto world = CreateWorld(io::MakeBenchmarkScenario("melee", 200), 42).value;
    FlatNavigationService nav;
    REQUIRE(Step(world, {}, {}, nav));
    auto frame = BuildPresentation(Capture(world));
    auto hash = StateHash(world);
    REQUIRE(frame.units.size() == 200);
    REQUIRE_FALSE(frame.events.empty());
    MockAdapter adapter;
    adapter.Consume(frame);
    CHECK(adapter.by_id.size() == 200);
    auto event_count = adapter.events.size();
    auto original = adapter.by_id.at(1);
    auto generation = adapter.pool[original].generation;
    adapter.Despawn({1});
    CHECK(GetStats(world).living_units == 200);
    adapter.Consume(frame);
    CHECK(adapter.by_id.size() == 200);
    CHECK(adapter.by_id.at(1) == original);
    CHECK(adapter.pool[original].generation > generation);
    CHECK(adapter.events.size() == event_count);
    CHECK(StateHash(world) == hash);
    auto retained = frame.units[0].pose;
    world.units[0].pose.position.x += 10;
    CHECK(frame.units[0].pose == retained);
    auto newer = frame;
    newer.tick++;
    newer.units[0].alive = false;
    newer.units[0].control = ControlMode::Dead;
    adapter.Consume(newer);
    CHECK(adapter.by_id.size() == 199);
    adapter.Consume(frame);
    CHECK(adapter.by_id.size() == 199);
}
TEST_CASE("integration.ten_thousand_exchange_measurement") {
    auto world = CreateWorld(io::MakeBenchmarkScenario("march", 10000), 42).value;
    using Clock = std::chrono::steady_clock;
    double capture_ms = 0, frame_ms = 0, consume_ms = 0;
    size_t bytes = 0;
    MockAdapter adapter;
    for (int rep = 0; rep < 30; ++rep) {
        auto a = Clock::now();
        auto snapshot = Capture(world);
        auto b = Clock::now();
        auto frame = BuildPresentation(snapshot);
        auto c = Clock::now();
        adapter.Consume(frame);
        auto d = Clock::now();
        capture_ms += std::chrono::duration<double, std::milli>(b - a).count();
        frame_ms += std::chrono::duration<double, std::milli>(c - b).count();
        consume_ms += std::chrono::duration<double, std::milli>(d - c).count();
        bytes = sizeof(frame) + frame.units.size() * sizeof(UnitPresentation) +
                frame.events.size() * sizeof(BattleEvent);
    }
    REQUIRE(adapter.by_id.size() == 10000);
    CHECK(bytes >= 10000 * sizeof(UnitPresentation));
    auto dir = std::filesystem::path(BATTLE_SOURCE_DIR) / "out/artifacts/presentation";
    std::filesystem::create_directories(dir);
    auto write_start = Clock::now();
    REQUIRE(io::WriteReplay(dir / "ten-thousand-replay.json", world.scenario, world.seed, {}));
    auto write_ms = std::chrono::duration<double, std::milli>(Clock::now() - write_start).count();
    std::ofstream f(dir / "exchange.txt");
    f << "10000 units, 30 repetitions, mean milliseconds; no battle events in this fixture\n"
      << "capture_ms=" << capture_ms / 30 << "\nframe_ms=" << frame_ms / 30
      << "\nmock_consume_ms=" << consume_ms / 30 << "\nframe_bytes=" << bytes
      << "\nunit_presentation_size=" << sizeof(UnitPresentation) << "\n"
      << "initial_replay_write_ms=" << write_ms << "\n"
      << "initial_replay_bytes=" << std::filesystem::file_size(dir / "ten-thousand-replay.json")
      << "\n";
}

#include "battle/MarchMission.h"
#include <limits>

TEST_CASE("integration.march_reports_first_contact_and_waits_for_a_valid_player_order") {
    MarchMission mission({{{1}, 0, {{-46, -24, 0}, 0}},
                          {{2}, 0, {{-46, 24, 0}, 0}},
                          {{3}, 1, {{46, -24, 0}, Pi}}});
    CHECK_FALSE(mission.EnemyFound());
    for (int frame = 0; frame < 300 && !mission.EnemyFound(); ++frame)
        mission.Step(.05f);

    REQUIRE(mission.EnemyFound());
    CHECK_FALSE(mission.Encounter());
    const auto stopped = mission.Regiments();
    const auto old_flag = stopped[0].waypoint;
    CHECK_FALSE(mission.SetWaypoint({1}, {0, 12, 0}));
    CHECK_FALSE(mission.SetWaypoint({1}, {61, -24, 0}));
    CHECK_FALSE(mission.SetWaypoint({1}, {std::numeric_limits<float>::infinity(), 0, 0}));
    CHECK_FALSE(mission.SetWaypoint({3}, {10, -24, 0}));
    CHECK(mission.Regiments()[0].waypoint == old_flag);
    mission.Step(1.f);
    CHECK(mission.Regiments()[0].pose == stopped[0].pose);
    CHECK(mission.Regiments()[2].pose == stopped[2].pose);

    const Vec3 new_flag{-12, -24, 0};
    REQUIRE(mission.SetWaypoint({1}, new_flag));
    CHECK(mission.Regiments()[0].waypoint == new_flag);
    CHECK(mission.Regiments()[1].waypoint == stopped[1].waypoint);
    CHECK(mission.Regiments()[1].route == stopped[1].route);
    for (int frame = 0; frame < 300 && !mission.Encounter(); ++frame)
        mission.Step(.05f);

    REQUIRE(mission.Encounter());
    CHECK(mission.Encounter()->first == RegimentId{1});
    CHECK(mission.Encounter()->second == RegimentId{3});
    CHECK(mission.Encounter()->center.x < 0);
    CHECK(mission.Encounter()->center.y == doctest::Approx(-24));
    CHECK_FALSE(mission.SetWaypoint({1}, {-16, -24, 0}));
}

TEST_CASE("integration.march_uses_open_crossings_for_cross_bank_orders") {
    for (const float crossing_y : {-24.f, 0.f, 24.f}) {
        CAPTURE(crossing_y);
        const Vec3 goal{42, crossing_y + 6, 0};
        MarchMission mission({{{1}, 0, {{-42, goal.y, 0}, 0}}});
        REQUIRE(mission.SetWaypoint({1}, goal));

        bool crossed_river = false;
        for (int frame = 0; frame < 400; ++frame) {
            mission.Step(.05f);
            const Vec3 position = mission.Regiments().front().pose.position;
            if (std::abs(position.x) <= 5.f) {
                crossed_river = true;
                CHECK(std::abs(position.y - crossing_y) < 4.f);
            }
        }

        CHECK(crossed_river);
        CHECK(mission.Regiments().front().pose.position == goal);
        CHECK_FALSE(mission.Encounter());
    }
}

TEST_CASE("integration.march_sighting_across_open_water_does_not_start_battle") {
    MarchMission mission({{{1}, 0, {{-6, 12, 0}, 0}, Vec3{-6, 12, 0}},
                          {{2}, 1, {{6, 12, 0}, Pi}, Vec3{6, 12, 0}}});
    mission.Step(.05f);
    REQUIRE(mission.EnemyFound());
    REQUIRE(mission.SetWaypoint({1}, {-6, 12, 0}));
    mission.Step(1.f);
    CHECK_FALSE(mission.Encounter());
}

TEST_CASE("integration.march_opposing_regiments_meet_at_each_crossing") {
    for (const auto &crossing : MarchCrossings) {
        CAPTURE(crossing.y);
        MarchMission mission({{{1}, 0, {{-6, crossing.y, 0}, 0}, Vec3{6, crossing.y, 0}},
                              {{2}, 1, {{6, crossing.y, 0}, Pi}, Vec3{-6, crossing.y, 0}}});
        mission.Step(.05f);
        REQUIRE(mission.EnemyFound());
        REQUIRE(mission.SetWaypoint({1}, {6, crossing.y, 0}));
        mission.Step(1.f);
        REQUIRE(mission.Encounter());
        CHECK(mission.Encounter()->center.y == doctest::Approx(crossing.y));
    }
}
