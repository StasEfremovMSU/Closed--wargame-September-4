#include "battle/Simulation.h"
#include "battle/Snapshot.h"
#include "doctest.h"
#include <limits>
using namespace battle;

TEST_CASE("world.empty") {
    auto r = CreateWorld(Scenario{}, 42);
    REQUIRE(r);
    CHECK(GetStats(r.value).living_units == 0);
    CHECK(GetStats(r.value).regiment_count == 0);
}
TEST_CASE("world.stable_ids") {
    Scenario s;
    RegimentSpec reg;
    reg.id = {42};
    s.regiments.push_back(reg);
    UnitSpec a;
    a.id = {17};
    a.regiment = {42};
    s.units.push_back(a);
    UnitSpec b = a;
    b.id = {901};
    s.units.push_back(b);
    auto w = CreateWorld(s, 42);
    REQUIRE(w);
    CHECK(GetUnit(w.value, UnitId{901})->id.value == 901);
    CHECK_FALSE(GetUnit(w.value, UnitId{1}).has_value());
    CHECK(GetStats(w.value).living_units == 2);
    s.units.push_back(a);
    CHECK_FALSE(CreateWorld(s, 42));
}
TEST_CASE("world.fixed_tick") {
    auto w = CreateWorld(Scenario{}, 42).value;
    FlatNavigationService nav;
    CHECK(Step(w, {}, SimulationOptions{}, nav));
    CHECK(w.tick == 1);
    const auto hash = StateHash(w);
    SimulationOptions bad;
    bad.tick_ms = 25;
    CHECK(Step(w, {}, bad, nav).status == Status::InvalidInput);
    CHECK(w.tick == 1);
    CHECK(StateHash(w) == hash);
}
TEST_CASE("world.seed_replay") {
    auto a = CreateWorld(Scenario{}, 42).value, b = a;
    FlatNavigationService nav;
    for (int i = 0; i < 20; ++i) {
        REQUIRE(Step(a, {}, {}, nav));
        REQUIRE(Step(b, {}, {}, nav));
    }
    CHECK(StateHash(a) == StateHash(b));
    CHECK(StateHash(a) != StateHash(CreateWorld(Scenario{}, 43).value));
}
TEST_CASE("world.packet_validation") {
    auto w = CreateWorld(Scenario{}, 42).value;
    FlatNavigationService nav;
    Command c;
    c.kind = CommandKind::ExternalPoseSample;
    c.unit = {99};
    c.pose.position.x = std::numeric_limits<float>::quiet_NaN();
    CommandEnvelope env{0, 1, c};
    auto hash = StateHash(w);
    CHECK(Step(w, std::span{&env, 1}, {}, nav).status == Status::InvalidInput);
    CHECK(StateHash(w) == hash);
    c = {};
    c.kind = CommandKind::AcquireControl;
    c.unit = {99};
    env.command = c;
    auto result = Step(w, std::span{&env, 1}, {}, nav);
    REQUIRE(result);
    CHECK(result.value.events.size() == 1);
    CHECK(result.value.events[0].kind == EventKind::CommandRejected);
    CHECK(w.tick == 1);
}
