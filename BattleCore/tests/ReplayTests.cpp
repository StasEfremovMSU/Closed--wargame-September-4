#include "JsonBindings.h"
#include "ReplayIo.h"
#include "TestSupport.h"
#include "battle/Control.h"
#include "battle/command/Services.h"
#include "doctest.h"
using namespace battle;

TEST_CASE("replay.hill_roundtrips_without_changing_flat_scenario_data") {
    auto scenario = test::Duel();
    const nlohmann::json flat_json = scenario;
    CHECK_FALSE(flat_json.contains("hill"));
    CHECK_FALSE(flat_json.get<Scenario>().hill);
    const auto flat = CreateWorld(scenario, 42);
    REQUIRE(flat);
    scenario.hill = TerrainHill{31.f, 25.f, 9.f, .4f};
    const auto hill = CreateWorld(scenario, 42);
    REQUIRE(hill);
    CHECK(StateHash(hill.value) != StateHash(flat.value));
    test::TempDir temp;
    const auto path = temp.path / "hill.json";
    REQUIRE(io::SaveSnapshot(path, Capture(hill.value)));
    const auto restored = io::LoadSnapshot(path);
    REQUIRE(restored);
    CHECK(restored.value.state.scenario.hill == scenario.hill);
    CHECK(StateHash(hill.value) == StateHash(restored.value.state));
    scenario.hill->crest_fraction = 1.f;
    CHECK_FALSE(CreateWorld(scenario, 42));
}

TEST_CASE("replay.adaptive_battle_roundtrips_and_legacy_stays_disabled") {
    auto scenario = test::Duel();
    nlohmann::json legacy = scenario.rules;
    CHECK_FALSE(legacy.contains("adaptive_battle"));
    CHECK_FALSE(legacy.get<Rules>().adaptive_battle);
    const auto baseline = CreateWorld(scenario, 42);
    REQUIRE(baseline);

    scenario.rules.adaptive_battle = true;
    const auto adaptive = CreateWorld(scenario, 42);
    REQUIRE(adaptive);
    CHECK(StateHash(adaptive.value) != StateHash(baseline.value));
    test::TempDir temp;
    const auto file = temp.path / "adaptive.json";
    REQUIRE(io::SaveSnapshot(file, Capture(adaptive.value)));
    const auto reloaded = io::LoadSnapshot(file);
    REQUIRE(reloaded);
    CHECK(reloaded.value.state.scenario.rules.adaptive_battle);
    CHECK(StateHash(adaptive.value) == StateHash(reloaded.value.state));
}

TEST_CASE("replay.troop_kinds_roundtrip_and_legacy_units_default_to_infantry") {
    auto scenario = test::Duel();
    scenario.units[0].troop = UnitTroop::Spearman;
    scenario.units[1].troop = UnitTroop::Mounted;
    const auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    const auto before = StateHash(created.value);
    test::TempDir temp;
    const auto path = temp.path / "troops.json";
    REQUIRE(io::SaveSnapshot(path, Capture(created.value)));
    const auto loaded = io::LoadSnapshot(path);
    REQUIRE(loaded);
    CHECK(StateHash(loaded.value.state) == before);
    CHECK(loaded.value.state.units[0].troop == UnitTroop::Spearman);
    CHECK(loaded.value.state.units[1].troop == UnitTroop::Mounted);

    nlohmann::json legacy = scenario.units[0];
    legacy.erase("troop");
    CHECK(legacy.get<UnitSpec>().troop == UnitTroop::Infantry);
    legacy["troop"] = 256;
    CHECK_THROWS(legacy.get<UnitSpec>());
    scenario.units[0].troop = static_cast<UnitTroop>(127);
    CHECK_FALSE(CreateWorld(scenario, 42));
}

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

TEST_CASE("replay.ongoing_fire_pressure_survives_json_roundtrip") {
    auto scenario = CommandScenario();
    scenario.rules.regiment_tactics = true;
    scenario.units[4].archer = true;
    scenario.units[4].weapon.range = 40.f;
    auto made = CreateWorld(scenario, 42);
    REQUIRE(made);
    auto &world = made.value;
    const FlatNavigationService nav;
    REQUIRE(Step(world, {}, {}, nav));
    const auto &hierarchy = world.command_state->hierarchy;
    const auto observed = command::BuildSituation(world, hierarchy, {4}, world.tick);
    REQUIRE(observed.value);
    REQUIRE(observed.value->own.under_ranged_fire);
    const nlohmann::json report = *observed.value;
    CHECK(report.get<command::LocalSituation>() == *observed.value);
    const nlohmann::json legacy = command::OwnForceSummary{};
    CHECK_FALSE(legacy.contains("under_ranged_fire"));

    test::TempDir temp;
    const auto path = temp.path / "incoming-fire.json";
    REQUIRE(io::SaveSnapshot(path, Capture(world)));
    auto loaded = io::LoadSnapshot(path);
    REQUIRE(loaded);
    CHECK(StateHash(loaded.value.state) == StateHash(world));
    REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(Step(loaded.value.state, {}, {}, nav));
    CHECK(StateHash(loaded.value.state) == StateHash(world));
}

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
    for (const auto &damage : {"timer", "timer-range", "counter", "message-id", "topology",
                               "missing", "enum", "negative", "payload-identity"}) {
        auto broken = canonical;
        if (damage == std::string("timer"))
            broken["state"]["command_state"]["nodes"][0]["schedule"]["interval_ticks"] = 2.5;
        else if (damage == std::string("timer-range"))
            broken["state"]["command_state"]["nodes"][0]["schedule"]["interval_ticks"] =
                1000001;
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
        else if (damage == std::string("payload-identity"))
            broken["state"]["command_state"]["communications"]["messages"][0]["recipient"] = 4;
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

TEST_CASE("replay.follow_and_facing_roundtrip_with_a_queued_stop_and_active_route") {
    auto scenario = test::Duel(2);
    scenario.officers.push_back({{10}, {1}, 2});
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    World &world = created.value;
    FlatNavigationService nav;
    Command acquire;
    acquire.kind = CommandKind::AcquireControl;
    acquire.unit = {1};
    Command begin;
    begin.kind = CommandKind::SetRegimentFollowing;
    begin.unit = acquire.unit;
    begin.regiment = {1};
    begin.follow_enabled = true;
    Command face;
    face.kind = CommandKind::FaceControlledUnit;
    face.unit = acquire.unit;
    face.pose.yaw = Pi / 2;
    auto stop = begin;
    stop.follow_enabled = false;
    std::vector<CommandEnvelope> commands{{0, 1, acquire}, {0, 2, begin}, {0, 3, face},
                                           {16, 4, stop}};
    test::TempDir dir;
    REQUIRE(io::WriteReplay(dir.path / "follow-replay.json", scenario, 42, commands));
    const auto replay = io::LoadReplay(dir.path / "follow-replay.json");
    REQUIRE(replay);
    REQUIRE(Step(world, commands, {}, nav));
    CHECK(FindUnit(world, {1})->pose.yaw == doctest::Approx(Pi / 2));
    CHECK_FALSE(FindRegiment(world, {1})->route.empty());
    REQUIRE(io::SaveSnapshot(dir.path / "follow-save.json", Capture(world)));
    auto loaded = io::LoadSnapshot(dir.path / "follow-save.json");
    REQUIRE(loaded);
    CHECK(StateHash(loaded.value.state) == StateHash(world));
    auto replayed = CreateWorld(scenario, 42);
    REQUIRE(replayed);
    REQUIRE(Step(replayed.value, replay.value.commands, {}, nav));
    for (int i = 0; i < 32; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        REQUIRE(Step(loaded.value.state, {}, {}, nav));
        REQUIRE(Step(replayed.value, {}, {}, nav));
    }
    CHECK(StateHash(loaded.value.state) == StateHash(world));
    CHECK(StateHash(replayed.value) == StateHash(world));
}

TEST_CASE("replay.field_order_roundtrip_and_legacy_defaults") {
    auto world = CreateWorld(test::Duel(), 42);
    REQUIRE(world);
    Order order;
    order.recipient = {1};
    order.type = OrderType::MoveTo;
    order.point = {3.f, 8.f, 0.f};
    order.pace = MovementPace::Run;
    order.facing = 0.f;
    order.move_columns = 3;
    order.maneuver_group = 1234;
    REQUIRE(SubmitOrder(world.value, order) == Status::Ok);
    test::TempDir dir;
    const auto path = dir.path / "field-order.json";
    REQUIRE(io::SaveSnapshot(path, Capture(world.value)));
    const auto restored = io::LoadSnapshot(path);
    REQUIRE(restored);
    REQUIRE(restored.value.state.pending_orders.size() == 1);
    const auto& loaded = restored.value.state.pending_orders.front();
    CHECK(loaded.pace == MovementPace::Run);
    CHECK(loaded.facing == 0.f);
    CHECK(loaded.move_columns == 3u);
    CHECK(loaded.maneuver_group == 1234u);
    CHECK(StateHash(restored.value.state) == StateHash(world.value));

    const nlohmann::json legacy = Order{};
    CHECK_FALSE(legacy.contains("pace"));
    CHECK_FALSE(legacy.contains("facing"));
    CHECK_FALSE(legacy.contains("move_columns"));
    CHECK_FALSE(legacy.contains("maneuver_group"));
    CHECK(legacy.get<Order>().pace == MovementPace::Walk);
    CHECK_FALSE(legacy.get<Order>().facing);
    CHECK_FALSE(legacy.get<Order>().move_columns);
    CHECK(legacy.get<Order>().maneuver_group == 0u);

    for (const auto &bad : {nlohmann::json(-1), nlohmann::json(4294967296ull), nlohmann::json(1.5),
                            nlohmann::json("1")}) {
        auto invalid = nlohmann::json(order);
        invalid["pace"] = bad;
        CHECK_THROWS(invalid.get<Order>());
        invalid = order;
        invalid["move_columns"] = bad;
        CHECK_THROWS(invalid.get<Order>());
    }
    auto invalid = nlohmann::json(order);
    invalid["move_columns"] = 0;
    CHECK_THROWS(invalid.get<Order>());
    invalid["move_columns"] = 3;
    invalid["type"] = OrderType::Hold;
    CHECK_THROWS(invalid.get<Order>());
    invalid = order;
    invalid["facing"] = "0";
    CHECK_THROWS(invalid.get<Order>());
}

TEST_CASE("replay.field_battle_preserves_pending_effects_and_discipline") {
    auto source = test::Duel();
    source.units[0].archer = true;
    source.units[0].ranged_weapon = RangedWeapon::Gunpowder;
    source.units[0].ammunition = 8;
    source.units[1].troop = UnitTroop::Mounted;
    source.units[1].cavalry = CavalryClass::Heavy;
    auto world = CreateWorld(source, 42);
    REQUIRE(world);
    auto &state = world.value;
    state.units[0].ammunition_remaining = 0;
    auto &mounted = state.regiments[1];
    mounted.cavalry_phase = CavalryPhase::Reform;
    mounted.cavalry_target = {1};
    mounted.charge_runup_m = 4.f;
    mounted.cavalry_phase_until = 12;
    mounted.pursuit_target = {1};
    mounted.melee_opponent = {1};
    mounted.rout_direction = {-1.f, 0.f, 0.f};
    mounted.discipline_window_until = 10;
    mounted.melee_losses = 1;
    mounted.rear_checked = true;
    state.last_launch_id = state.next_event_id++;
    state.attacks.push_back({{state.last_launch_id}, {2}, {1}, 8, 3.f, true, 1.5f, false, true});
    state.smoke.push_back({{2.f, 3.f, 0.f}, 4.f, 9});
    test::TempDir dir;
    const auto path = dir.path / "field-battle.json";
    REQUIRE(io::SaveSnapshot(path, Capture(state)));
    const auto loaded = io::LoadSnapshot(path);
    REQUIRE(loaded);
    CHECK(StateHash(state) == StateHash(loaded.value.state));
    CHECK(loaded.value.state.units[0].ammunition == 8u);
    CHECK(loaded.value.state.units[0].ammunition_remaining == 0u);
    CHECK(loaded.value.state.regiments[1].cavalry_phase == CavalryPhase::Reform);
    CHECK(loaded.value.state.regiments[1].rout_direction == mounted.rout_direction);
    REQUIRE(loaded.value.state.attacks.size() == 1);
    CHECK(loaded.value.state.attacks.front().charge);
    REQUIRE(loaded.value.state.smoke.size() == 1);
    CHECK(loaded.value.state.smoke.front().expires_tick == 9u);
}
