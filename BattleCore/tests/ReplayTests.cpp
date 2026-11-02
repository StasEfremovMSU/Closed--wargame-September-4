#include "JsonBindings.h"
#include "ReplayIo.h"
#include "TestSupport.h"
#include "battle/Control.h"
#include "doctest.h"
using namespace battle;
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
