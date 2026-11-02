#include "JsonBindings.h"
#include "ReplayIo.h"
#include "TestSupport.h"
#include "battle/Control.h"
#include "doctest.h"
using namespace battle;
namespace command = battle::command;

namespace {
Scenario CommandScenario() {
    auto scenario = test::Duel(4);
    for (unsigned i = 0; i < 4; ++i)
        scenario.officers.push_back({{i + 1}, {i + 1}, i ? 1u : 2u});
    command::CommandSetup setup;
    for (unsigned i = 0; i < 4; ++i) {
        command::CommandNode node;
        node.id = {i + 1};
        node.level = command::Level(i);
        node.parent = {i};
        node.team = 0;
        node.commander = {4 - i};
        if (i < 3)
            node.children.push_back({i + 2});
        else
            node.regiment = {1};
        setup.hierarchy.nodes.push_back(node);
        command::OfficerProfile profile;
        profile.officer = node.commander;
        profile.tactical_skill = .7f;
        profile.initiative = i == 3 ? 0.f : .7f;
        profile.discipline = .8f;
        profile.aggression = .8f;
        profile.command_radius_m = 60;
        setup.hierarchy.officers.push_back(profile);
    }
    command::CommandTask mission;
    mission.id = {1};
    mission.recipient = {1};
    mission.action = command::TaskKind::Attack;
    mission.intent.target_regiment = {2};
    mission.intent.objective_position = {10, 0, 0};
    mission.status = command::TaskStatus::Active;
    setup.missions.push_back(mission);
    scenario.command_setup = std::move(setup);
    return scenario;
}
} // namespace

TEST_CASE("replay.command_runtime_roundtrips_mid_message_and_hashes_every_part") {
    auto made = CreateWorld(CommandScenario(), 42);
    REQUIRE(made);
    World a = std::move(made.value);
    FlatNavigationService nav;
    REQUIRE(Step(a, {}, {}, nav));
    REQUIRE(a.command_state);
    REQUIRE_FALSE(a.command_state->communications.messages.empty());
    const auto schema = Capture(a);
    CHECK(schema.schema_version == 3);

    auto changed = a;
    changed.command_state->nodes[0].schedule.next_think_tick++;
    CHECK(StateHash(changed) != StateHash(a));
    changed = a;
    changed.command_state->nodes[0].memory.state = command::FsmState::Holding;
    CHECK(StateHash(changed) != StateHash(a));
    changed = a;
    changed.command_state->communications.messages[0].status =
        command::DeliveryStatus::Lost;
    CHECK(StateHash(changed) != StateHash(a));
    changed = a;
    changed.command_state->hierarchy.nodes[0].sector_role = command::SectorRole::Vanguard;
    CHECK(StateHash(changed) != StateHash(a));
    REQUIRE_FALSE(a.command_state->nodes[0].orders.empty());
    changed = a;
    changed.command_state->nodes[0].orders[0].priority++;
    CHECK(StateHash(changed) != StateHash(a));
    changed = a;
    changed.command_state->nodes[0].reports.push_back({});
    CHECK(StateHash(changed) != StateHash(a));
    changed = a;
    changed.command_state->next_message_id.value++;
    CHECK(StateHash(changed) != StateHash(a));

    test::TempDir dir;
    REQUIRE(io::SaveSnapshot(dir.path / "command-save.json", Capture(a)));
    auto loaded = io::LoadSnapshot(dir.path / "command-save.json");
    REQUIRE(loaded);
    REQUIRE(loaded.value.state.command_state);
    World b = std::move(loaded.value.state);
    for (int tick = 0; tick < 40; ++tick) {
        REQUIRE(Step(a, {}, {}, nav));
        REQUIRE(Step(b, {}, {}, nav));
    }
    CHECK(StateHash(a) == StateHash(b));

    Command task_command;
    task_command.kind = CommandKind::IssueCommandTask;
    task_command.command_task.recipient = {1};
    task_command.command_task.action = command::TaskKind::HoldArea;
    std::vector<CommandEnvelope> input{{0, 1, task_command}};
    auto replay_world = CreateWorld(CommandScenario(), 84);
    REQUIRE(replay_world);
    World continuous = std::move(replay_world.value);
    REQUIRE(Step(continuous, input, {}, nav));
    REQUIRE_FALSE(continuous.command_state->communications.messages.empty());
    for (int tick = 0; tick < 30; ++tick)
        REQUIRE(Step(continuous, {}, {}, nav));
    REQUIRE(io::WriteReplay(dir.path / "command-replay.json", CommandScenario(), 84, input));
    auto replay = io::LoadReplay(dir.path / "command-replay.json");
    REQUIRE(replay);
    auto replay_made = CreateWorld(replay.value.scenario, replay.value.seed);
    REQUIRE(replay_made);
    World replayed = std::move(replay_made.value);
    for (int tick = 0; tick < 31; ++tick)
        REQUIRE(Step(replayed, tick == 0 ? std::span<const CommandEnvelope>(replay.value.commands)
                                         : std::span<const CommandEnvelope>{}, {}, nav));
    CHECK(StateHash(continuous) == StateHash(replayed));
}

TEST_CASE("replay.command_snapshot_rejects_corruption_and_migrates_only_plain_v2") {
    auto made = CreateWorld(CommandScenario(), 42);
    REQUIRE(made);
    REQUIRE(Step(made.value, {}, {}, FlatNavigationService{}));
    auto canonical = nlohmann::json(Capture(made.value));
    test::TempDir dir;
    for (const auto &damage : {"timer", "counter", "message-id", "topology", "missing", "enum",
                               "negative"}) {
        auto broken = canonical;
        if (damage == std::string("timer"))
            broken["state"]["command_state"]["nodes"][0]["schedule"]["interval_ticks"] = 2.5;
        else if (damage == std::string("counter"))
            broken["state"]["command_state"]["next_task_id"] = 1;
        else if (damage == std::string("message-id"))
            broken["state"]["command_state"]["communications"]["messages"][0]["id"] = 0;
        else if (damage == std::string("topology"))
            broken["state"]["command_state"]["hierarchy"]["nodes"][0]["children"] =
                nlohmann::json::array();
        else if (damage == std::string("missing"))
            broken["state"]["command_state"]["nodes"][0]["memory"].erase("goal");
        else if (damage == std::string("enum"))
            broken["state"]["command_state"]["hierarchy"]["nodes"][0]["level"] = 99;
        else
            broken["state"]["command_state"]["next_message_id"] = -1;
        test::Write(dir.path / "bad-command-save.json", broken.dump());
        CAPTURE(damage);
        CHECK_FALSE(io::LoadSnapshot(dir.path / "bad-command-save.json"));
    }

    auto old_plain = nlohmann::json(Capture(CreateWorld(test::Duel(), 7).value));
    old_plain["schema_version"] = 2;
    old_plain["state"].erase("command_state");
    old_plain["state"]["scenario"].erase("command_setup");
    for (auto &entry : old_plain["state"]["commands"])
        entry["command"].erase("command_task");
    test::Write(dir.path / "plain-v2.json", old_plain.dump());
    auto migrated = io::LoadSnapshot(dir.path / "plain-v2.json");
    REQUIRE(migrated);
    CHECK(migrated.value.schema_version == 3);

    auto old_command = canonical;
    old_command["schema_version"] = 2;
    test::Write(dir.path / "command-v2.json", old_command.dump());
    CHECK_FALSE(io::LoadSnapshot(dir.path / "command-v2.json"));

    REQUIRE(io::WriteReplay(dir.path / "plain-replay.json", test::Duel(), 7, {}));
    std::ifstream replay_file(dir.path / "plain-replay.json");
    nlohmann::json plain_replay;
    replay_file >> plain_replay;
    plain_replay["schema_version"] = 2;
    plain_replay["scenario"].erase("command_setup");
    test::Write(dir.path / "plain-replay-v2.json", plain_replay.dump());
    auto replay_migrated = io::LoadReplay(dir.path / "plain-replay-v2.json");
    REQUIRE(replay_migrated);
    CHECK(replay_migrated.value.schema_version == 3);

    REQUIRE(io::WriteReplay(dir.path / "command-replay.json", CommandScenario(), 7, {}));
    std::ifstream command_replay_file(dir.path / "command-replay.json");
    nlohmann::json command_replay;
    command_replay_file >> command_replay;
    command_replay["schema_version"] = 2;
    test::Write(dir.path / "command-replay-v2.json", command_replay.dump());
    CHECK_FALSE(io::LoadReplay(dir.path / "command-replay-v2.json"));
}
TEST_CASE("replay.save_resume_same_state") {
    auto s = test::Duel();
    s.units[0].archer = true;
    s.units[0].weapon.range = 30;
    s.units[0].weapon.projectile_speed_mps = .5f;
    auto a = CreateWorld(s, 42).value;
    FlatNavigationService nav;
    Command acquire;
    acquire.kind = CommandKind::AcquireControl;
    acquire.unit = {1};
    Command attack;
    attack.kind = CommandKind::AttackControlledUnit;
    attack.unit = {1};
    attack.target = {2};
    Order o;
    o.recipient = {2};
    o.type = OrderType::Hold;
    o.delivery_tick = 150;
    SubmitOrder(a, o);
    std::vector<CommandEnvelope> commands{{0, 1, acquire}, {0, 2, attack}};
    REQUIRE(Step(a, commands, {}, nav));
    for (int i = 1; i < 100; ++i)
        REQUIRE(Step(a, {}, {}, nav));
    REQUIRE(!a.attacks.empty());
    REQUIRE(!a.pending_orders.empty());
    test::TempDir t;
    REQUIRE(io::SaveSnapshot(t.path / "save.json", Capture(a)));
    auto loaded = io::LoadSnapshot(t.path / "save.json");
    REQUIRE(loaded);
    auto restored = Restore(loaded.value);
    REQUIRE(restored);
    auto b = restored.value;
    for (int i = 0; i < 100; ++i) {
        REQUIRE(Step(a, {}, {}, nav));
        REQUIRE(Step(b, {}, {}, nav));
    }
    CHECK(StateHash(a) == StateHash(b));
}
TEST_CASE("replay.external_future_commands_and_duplicates") {
    auto a = CreateWorld(test::Duel(), 42).value;
    FlatNavigationService nav;
    Command c;
    c.kind = CommandKind::AcquireControl;
    c.unit = {1};
    Command p;
    p.kind = CommandKind::ExternalPoseSample;
    p.unit = {1};
    p.pose.position = {-20, 10, 0};
    p.sample_tick = 5;
    std::vector<CommandEnvelope> input{{0, 1, c}, {5, 2, p}};
    REQUIRE(Step(a, input, {}, nav));
    auto restored = Restore(Capture(a));
    REQUIRE(restored);
    auto b = restored.value;
    REQUIRE(Step(a, input, {}, nav));
    REQUIRE(Step(b, {}, {}, nav));
    for (int i = 0; i < 10; ++i) {
        REQUIRE(Step(a, {}, {}, nav));
        REQUIRE(Step(b, {}, {}, nav));
    }
    CHECK(StateHash(a) == StateHash(b));
    CHECK(a.units[0].pose.position.y == 10);
}
TEST_CASE("replay.corruption_is_rejected") {
    auto w = CreateWorld(test::Duel(), 42).value;
    auto hash = StateHash(w);
    auto s = Capture(w);
    s.schema_version = 99;
    CHECK_FALSE(Restore(s));
    s = Capture(w);
    s.state.units[0].health = std::numeric_limits<float>::infinity();
    CHECK_FALSE(Restore(s));
    s = Capture(w);
    s.state.units[0].regiment = {99};
    CHECK_FALSE(Restore(s));
    s = Capture(w);
    s.state.regiments[0].units.push_back({2});
    CHECK_FALSE(Restore(s));
    CHECK(StateHash(w) == hash);
    test::TempDir t;
    test::Write(t.path / "bad.json", "{\"schema_version\":1,");
    CHECK_FALSE(io::LoadSnapshot(t.path / "bad.json"));
    nlohmann::json j = Capture(w);
    j["state"]["units"][0].erase("alive");
    test::Write(t.path / "missing.json", j.dump());
    CHECK_FALSE(io::LoadSnapshot(t.path / "missing.json"));
}
TEST_CASE("replay.bundle_and_large_ids") {
    auto s = test::Duel();
    s.units[0].id = {0x8000000000000001ull};
    test::TempDir t;
    REQUIRE(io::WriteReplay(t.path / "replay.json", s, 123, {}));
    auto r = io::LoadReplay(t.path / "replay.json");
    REQUIRE(r);
    CHECK(r.value.seed == 123);
    CHECK(r.value.scenario.units[0].id.value == 0x8000000000000001ull);
}
TEST_CASE("replay.canonical_input_and_source_hash") {
    auto s = test::Duel(3);
    s.officers = {{{1}, {1}, 1}, {{2}, {4}, 1}};
    auto a = CreateWorld(s, 42).value;
    std::reverse(s.units.begin(), s.units.end());
    std::reverse(s.regiments.begin(), s.regiments.end());
    std::reverse(s.officers.begin(), s.officers.end());
    auto b = CreateWorld(s, 42).value;
    CHECK(StateHash(a) == StateHash(b));
    b = a;
    b.scenario.units[0].health = 10;
    CHECK(StateHash(a) != StateHash(b));
}
TEST_CASE("replay.rejects_missing_nested_continuation_fields") {
    auto source = test::Duel();
    source.units[0].archer = true;
    source.units[0].weapon.range = 30;
    source.units[0].weapon.projectile_speed_mps = .5f;
    auto w = CreateWorld(source, 42).value;
    FlatNavigationService nav;
    REQUIRE(Step(w, {}, {}, nav));
    REQUIRE_FALSE(w.attacks.empty());
    Order order;
    order.recipient = {1};
    order.delivery_tick = 50;
    REQUIRE(SubmitOrder(w, order) == Status::Ok);
    test::TempDir dir;
    nlohmann::json original = Capture(w);
    std::vector<std::string> keys;
    for (auto it = original["state"]["attacks"][0].begin();
         it != original["state"]["attacks"][0].end(); ++it)
        keys.push_back(it.key());
    for (const auto &key : keys) {
        CAPTURE(key);
        auto damaged = original;
        damaged["state"]["attacks"][0].erase(key);
        test::Write(dir.path / "attack.json", damaged.dump());
        CHECK_FALSE(io::LoadSnapshot(dir.path / "attack.json"));
    }
    for (auto path :
         {"/state/pending_orders/0/delivery_tick", "/state/units/0/weapon/cooldown_ticks",
          "/state/scenario/units/0/weapon/damage"}) {
        auto damaged = original;
        nlohmann::json::json_pointer ptr(path);
        auto parent = ptr.parent_pointer();
        damaged[parent].erase(ptr.back());
        test::Write(dir.path / "nested.json", damaged.dump());
        CHECK_FALSE(io::LoadSnapshot(dir.path / "nested.json"));
    }
}
TEST_CASE("replay.immutable_definitions_match_source") {
    auto source = test::Duel(2);
    source.officers = {{{1}, {1}, 2}};
    auto world = CreateWorld(source, 42).value;
    auto s = Capture(world);
    s.state.regiments[0].team = 9;
    CHECK_FALSE(Restore(s));
    s = Capture(world);
    s.state.regiments[0].id = {99};
    for (auto &u : s.state.units)
        if (u.regiment == RegimentId{1})
            u.regiment = {99};
    CHECK_FALSE(Restore(s));
    s = Capture(world);
    s.state.officers[0].unit = {2};
    CHECK_FALSE(Restore(s));
    s = Capture(world);
    s.state.officers[0].role = 1;
    CHECK_FALSE(Restore(s));
    s = Capture(world);
    s.state.units[0].weapon.damage = 99;
    CHECK_FALSE(Restore(s));
    s = Capture(world);
    s.state.regiments[0].initial_count = 100;
    CHECK_FALSE(Restore(s));
}
TEST_CASE("replay.ranged_mid_cycle_resume") {
    auto source = test::Duel(20);
    source.rules.ranged_cohorts = 4;
    for (auto &u : source.units) {
        u.archer = true;
        u.health = 20;
        u.weapon.range = 100;
        u.weapon.projectile_speed_mps = 5;
    }
    auto a = CreateWorld(source, 123).value;
    FlatNavigationService nav;
    Command acquire;
    acquire.kind = CommandKind::AcquireControl;
    acquire.unit = {1};
    Command external;
    external.kind = CommandKind::ExternalPoseSample;
    external.unit = {1};
    external.pose = {{-12, 1, 0}, 0};
    external.sample_tick = 9;
    Command release;
    release.kind = CommandKind::ReleaseControl;
    release.unit = {1};
    Command hold;
    hold.kind = CommandKind::IssueOrder;
    hold.order.recipient = {1};
    hold.order.type = OrderType::Hold;
    hold.order.delivery_tick = 12;
    std::vector<CommandEnvelope> commands{
        {7, 1, acquire}, {9, 2, external}, {13, 3, release}, {0, 4, hold}};
    REQUIRE(Step(a, commands, {}, nav));
    REQUIRE(Step(a, {}, {}, nav));
    REQUIRE_FALSE(a.attacks.empty());
    REQUIRE_FALSE(a.pending_orders.empty());
    REQUIRE_FALSE(a.commands.empty());
    test::TempDir dir;
    REQUIRE(io::SaveSnapshot(dir.path / "cohorts.json", Capture(a)));
    auto snapshot = io::LoadSnapshot(dir.path / "cohorts.json");
    REQUIRE(snapshot);
    auto restored = Restore(snapshot.value);
    REQUIRE(restored);
    auto b = restored.value;
    for (int tick = 0; tick < 100; ++tick) {
        REQUIRE(Step(a, {}, {}, nav));
        REQUIRE(Step(b, {}, {}, nav));
    }
    CHECK(StateHash(a) == StateHash(b));
    REQUIRE(io::WriteReplay(dir.path / "replay.json", source, 123, commands));
    auto replay = io::LoadReplay(dir.path / "replay.json");
    REQUIRE(replay);
    auto c = CreateWorld(replay.value.scenario, replay.value.seed).value;
    for (int tick = 0; tick < 102; ++tick)
        REQUIRE(Step(c,
                     tick == 0 ? std::span<const CommandEnvelope>(replay.value.commands)
                               : std::span<const CommandEnvelope>{},
                     {}, nav));
    CHECK(StateHash(a) == StateHash(c));
    auto v1 = Capture(a);
    v1.schema_version = 1;
    CHECK_FALSE(Restore(v1));
    nlohmann::json j = v1;
    test::Write(dir.path / "v1-save.json", j.dump());
    auto old = io::LoadSnapshot(dir.path / "v1-save.json");
    CHECK_FALSE(old);
    REQUIRE_FALSE(old.diagnostics.empty());
    CHECK(old.diagnostics[0].message.find("unsupported snapshot version") != std::string::npos);
    std::ifstream f(dir.path / "replay.json");
    nlohmann::json r;
    f >> r;
    r["schema_version"] = 1;
    test::Write(dir.path / "v1-replay.json", r.dump());
    CHECK_FALSE(io::LoadReplay(dir.path / "v1-replay.json"));
}
