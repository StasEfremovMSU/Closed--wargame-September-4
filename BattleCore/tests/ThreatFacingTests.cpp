#include "JsonBindings.h"
#include "TestSupport.h"
#include "doctest.h"

using namespace battle;

TEST_CASE("officers.stationary_ai_faces_a_lateral_threat_without_leaving_its_post") {
    auto scenario = test::Duel(2);
    scenario.rules.regiment_tactics = true;
    scenario.rules.stationary_threat_facing = true;
    scenario.regiments[0].pose = {{}, 0.f};
    scenario.regiments[1].pose.position = {0.f, 16.f, 0.f};
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    for (int i = 0; i < 80; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments[0].pose.position == Vec3{});
    CHECK(world.regiments[0].pose.yaw == doctest::Approx(Pi * .5f));
}

TEST_CASE("officers.stationary_facing_keeps_a_similarly_close_threat_then_switches") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.rules.stationary_threat_facing = true;
    scenario.regiments[0].pose = {{}, 0.f};
    scenario.regiments[1].pose.position = {15.f, 0.f, 0.f};
    scenario.regiments.push_back(scenario.regiments[1]);
    scenario.regiments.back().id = {3};
    scenario.regiments.back().pose.position = {0.f, 14.f, 0.f};
    scenario.units.push_back(scenario.units[1]);
    scenario.units.back().id = {3};
    scenario.units.back().regiment = {3};
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    for (int i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments[0].pose.yaw == doctest::Approx(0.f));
    auto &closer = world.regiments[2];
    closer.pose.position.y = 8.f;
    FindUnit(world, {3})->pose = closer.pose;
    for (int i = 0; i < 80; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments[0].pose.yaw == doctest::Approx(Pi * .5f));
}

TEST_CASE("officers.disabling_threat_facing_cancels_the_pivot_but_respects_manual_facing") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.rules.stationary_threat_facing = true;
    scenario.regiments[0].pose = {{}, 0.f};
    scenario.regiments[1].pose.position = {0.f, 16.f, 0.f};
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    for (int i = 0; i < 5; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    const auto interrupted = world.regiments[0].pose.yaw;
    CHECK(interrupted > .05f);
    CommandEnvelope disable{world.tick, 1, {}};
    disable.command.kind = CommandKind::SetStationaryThreatFacing;
    disable.command.stationary_threat_facing = false;
    REQUIRE(Step(world, std::span(&disable, 1), {}, navigation));
    CHECK(world.scenario.rules.stationary_threat_facing == std::optional(false));
    const auto packet = nlohmann::json(disable).get<CommandEnvelope>();
    CHECK_FALSE(packet.command.stationary_threat_facing);
    const auto restored = Restore(nlohmann::json(Capture(world)).get<Snapshot>());
    REQUIRE(restored);
    CHECK(restored.value.scenario.rules.stationary_threat_facing == std::optional(false));
    CHECK(StateHash(restored.value) == StateHash(world));
    for (int i = 0; i < 35; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments[0].pose.yaw == doctest::Approx(interrupted));

    Order hold;
    hold.recipient = {1};
    hold.type = OrderType::HoldLine;
    hold.facing = -Pi * .5f;
    REQUIRE(SubmitOrder(world, hold) == Status::Ok);
    for (int i = 0; i < 80; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments[0].pose.yaw == doctest::Approx(-Pi * .5f));
}

TEST_CASE("officers.stationary_threat_setting_preserves_a_march_or_braced_front") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.rules.stationary_threat_facing = true;
    scenario.regiments[0].pose = {{}, 0.f};
    scenario.regiments[1].pose.position = {0.f, 16.f, 0.f};
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    Order order;
    order.recipient = {1};
    SUBCASE("deliberate brace") { order.type = OrderType::Brace; }
    SUBCASE("active march") {
        order.type = OrderType::MoveTo;
        order.point = {20.f, 0.f, 0.f};
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService navigation;
    REQUIRE(SubmitOrder(world, order) == Status::Ok);
    for (int i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments[0].pose.yaw == doctest::Approx(0.f));
    CHECK((world.regiments[0].pose.position.x > 1.f) == (order.type == OrderType::MoveTo));
}
