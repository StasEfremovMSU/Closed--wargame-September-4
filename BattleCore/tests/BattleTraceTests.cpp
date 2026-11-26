#include "BattleTrace.h"
#include "TestSupport.h"
#include "battle/Control.h"
#include "battle/Snapshot.h"
#include "doctest.h"
#include <array>

using namespace battle;

TEST_CASE("replay.battle_trace_replays_actual_command_batches_from_a_snapshot") {
    const test::TempDir temp;
    auto created = CreateWorld(test::Duel(), 315);
    REQUIRE(created);
    auto &world = created.value;
    const GridNavigationService nav({}, 2.f, 128, 80, {-128, -80, 0});
    REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(AcquireControl(world, {1}) == Status::Ok);

    io::BattleTraceWriter trace;
    REQUIRE(trace.Begin(temp.path, world, {nav.Geometry(), {}, true}));
    for (unsigned i = 0; i < 32; ++i) {
        std::vector<CommandEnvelope> commands;
        if (i == 3 || i == 20) {
            Command move;
            move.kind = CommandKind::MoveControlledUnit;
            move.unit = {1};
            move.velocity = i == 3 ? Vec3{1.f, 0, 0} : Vec3{};
            commands.push_back({world.tick, i + 1, move});
        }
        REQUIRE(Step(world, commands, {50, 1, nullptr, true}, nav));
        if (i == 31) {
            world.outcome = BattleOutcome::Victory;
            world.winner_team = 0;
            REQUIRE(trace.Record(commands, world, {}, io::BattleTraceOutcome{world.outcome, world.winner_team}));
        } else {
            REQUIRE(trace.Record(commands, world));
        }
    }
    Order final_host_order;
    final_host_order.id = {world.next_order_id};
    final_host_order.recipient = {1};
    final_host_order.type = OrderType::Rally;
    REQUIRE(SubmitOrder(world, final_host_order) == Status::Ok);
    REQUIRE(trace.Finish(world, std::span(&final_host_order, 1)));
    const auto replay = io::ReplayBattleTrace(temp.path);
    REQUIRE(replay);
    CHECK(replay.value.complete);
    CHECK(replay.value.tick == world.tick);
    CHECK(replay.value.hash == StateHash(world));
}

TEST_CASE("replay.battle_trace_reports_a_stationary_regiment_with_an_active_march") {
    const test::TempDir temp;
    auto scenario = test::Duel();
    scenario.units[0].speed = .01f;
    auto created = CreateWorld(scenario, 178);
    REQUIRE(created);
    auto &world = created.value;
    io::BattleTraceWriter trace;
    REQUIRE(trace.Begin(temp.path, world, {}));

    Command move;
    move.kind = CommandKind::IssueOrder;
    move.order = {.recipient = {1}, .type = OrderType::MoveTo, .point = {-2, 0, 0}};
    const auto input = std::array{CommandEnvelope{world.tick, 1, move}};
    FlatNavigationService nav;
    for (unsigned i = 0; i < 62; ++i) {
        const auto commands = i == 0 ? std::span<const CommandEnvelope>(input)
                                     : std::span<const CommandEnvelope>{};
        REQUIRE(Step(world, commands, {}, nav));
        REQUIRE(trace.Record(commands, world));
    }

    const auto partial = io::ReplayBattleTrace(temp.path);
    REQUIRE(partial);
    CHECK_FALSE(partial.value.complete);
    CHECK(partial.value.stall_samples > 0);
    REQUIRE(trace.Finish(world));
}

TEST_CASE("replay.battle_trace_rejects_a_missing_tick") {
    const test::TempDir temp;
    const auto created = CreateWorld(test::Duel(), 315);
    REQUIRE(created);
    io::BattleTraceWriter trace;
    REQUIRE(trace.Begin(temp.path, created.value, {}));
    REQUIRE(trace.Finish(created.value));
    const auto ticks = temp.path / "ticks.jsonl";
    std::ifstream input(ticks);
    std::string header;
    REQUIRE(std::getline(input, header));
    input.close();
    test::Write(ticks, header + "\n{\"tick\":2,\"commands\":[]}\n");
    const auto replay = io::ReplayBattleTrace(temp.path);
    REQUIRE_FALSE(replay);
    REQUIRE(replay.diagnostics.size() == 1);
    CHECK(replay.diagnostics.front().message == "missing or out-of-order recorded tick");
}

TEST_CASE("replay.battle_trace_distinguishes_a_segment_obstructed_by_stone") {
    const test::TempDir temp;
    auto scenario = test::Duel();
    scenario.obstacles.push_back({-8.f, -2.f, -7.f, 2.f, true});
    auto created = CreateWorld(scenario, 315);
    REQUIRE(created);
    auto &world = created.value;
    world.regiments.front().route = {{{-5.f, 0.f, 0.f}, 0.f}};
    io::BattleTraceWriter trace;
    REQUIRE(trace.Begin(temp.path, world, {}));
    for (unsigned i = 0; i < 40; ++i) {
        ++world.tick;
        REQUIRE(trace.Record({}, world));
    }
    REQUIRE(trace.Finish(world));
    std::ifstream rows(temp.path / "ticks.jsonl");
    const std::string contents((std::istreambuf_iterator<char>(rows)), {});
    CHECK(contents.find("blocked_route_segment") != std::string::npos);
    CHECK(contents.find("stationary_on_clear_route") == std::string::npos);
}
