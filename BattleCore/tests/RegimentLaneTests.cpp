#include "JsonBindings.h"
#include "TestSupport.h"
#include "battle/Formation.h"
#include "doctest.h"

using namespace battle;

TEST_CASE("navigation.two_formations_clear_a_pass_and_rejoin_after_a_blocked_exit") {
    Scenario scenario;
    scenario.rules.local_melee = true;
    scenario.obstacles = {{-1, -30, 1, -1.4f}, {-1, 1.4f, 1, 30}};
    for (unsigned i = 0; i < 2; ++i) {
        RegimentSpec regiment;
        regiment.id = {i + 1};
        regiment.pose.position.x = i == 0 ? -6.f : -13.f;
        regiment.formation = {2, 1.f, .25f};
        regiment.speed = 2.f;
        scenario.regiments.push_back(regiment);
        for (const auto slot : BuildFormationSlots(regiment.formation, regiment.pose, 6)) {
            UnitSpec unit;
            unit.id = {scenario.units.size() + 1};
            unit.regiment = regiment.id;
            unit.pose = slot;
            unit.speed = 3.f;
            scenario.units.push_back(unit);
        }
    }
    const GridNavigationService nav(scenario.obstacles, 1.f, 61, 61, {-30, -30, 0});
    for (const float lead_goal : {6.5f, 23.f}) {
        auto created = CreateWorld(scenario, 16);
        REQUIRE(created);
        auto &world = created.value;
        const Vec3 goals[]{{lead_goal, 0, 0}, {16, 0, 0}};
        for (size_t i = 0; i < world.regiments.size(); ++i) {
            Order move;
            move.recipient = world.regiments[i].id;
            move.type = OrderType::MoveTo;
            move.point = goals[i];
            REQUIRE(SubmitOrder(world, move) == Status::Ok);
        }
        std::optional<World> restored;
        float max_lateral = 0.f;
        float max_slot_gap = 0.f;
        int arrival = -1;
        for (int tick = 0; tick < 410; ++tick) {
            REQUIRE(Step(world, {}, {}, nav));
            if (restored) {
                REQUIRE(Step(*restored, {}, {}, nav));
                CHECK(StateHash(*restored) == StateHash(world));
            }
            const auto &leader = world.regiments[0];
            const auto &follower = world.regiments[1];
            if (!restored && std::abs(follower.pose.position.y) > .4f) {
                const auto saved = nlohmann::json(Capture(world)).dump();
                auto snapshot = nlohmann::json::parse(saved).get<Snapshot>();
                auto loaded = Restore(snapshot);
                REQUIRE(loaded);
                CHECK(StateHash(loaded.value) == StateHash(world));
                restored = std::move(loaded.value);
                auto &lane = snapshot.state.regiments[1].march_lane;
                REQUIRE(lane);
                lane->next = lane->detour.size();
                CHECK_FALSE(Restore(snapshot));
            }
            CHECK_FALSE(RectanglesOverlap(leader.pose, Footprint(leader), follower.pose, Footprint(follower)));
            max_lateral = std::max(max_lateral, std::abs(follower.pose.position.y));
            for (const auto &unit : world.units) {
                const auto position = unit.pose.position;
                CHECK((std::abs(position.x) >= 1.f || std::abs(position.y) < 1.4f));
                max_slot_gap = std::max(max_slot_gap, Distance(position, unit.slot.position));
            }
            if (arrival == -1 && Distance(follower.pose.position, goals[1]) < .01f)
                arrival = tick;
        }
        CAPTURE(lead_goal);
        CAPTURE(max_lateral);
        CAPTURE(max_slot_gap);
        CAPTURE(arrival);
        CAPTURE(world.regiments[1].pose.position.x);
        CHECK(Distance(world.regiments[0].pose.position, goals[0]) < .01f);
        CHECK(arrival >= 0);
        CHECK(max_slot_gap < .45f);
        CHECK(max_lateral < 2.5f);
        REQUIRE(world.regiments[1].order);
        CHECK(world.regiments[1].order->status == OrderStatus::Completed);
        if (lead_goal == 23.f) {
            CHECK(max_lateral < .01f);
            CHECK(arrival <= 325);
        } else {
            CHECK(max_lateral > 1.f);
            CHECK(restored.has_value());
        }
        for (const auto &unit : world.units)
            CHECK(Distance(unit.pose.position, unit.slot.position) < .01f);
    }
}

TEST_CASE("navigation.a_temporarily_blocked_turn_keeps_the_ordered_lane") {
    bool persistent = false;
    SUBCASE("the ally clears before a recovery is necessary") {}
    SUBCASE("a persistently blocked turn finds a second approach after waiting") {
        persistent = true;
    }
    auto scenario = test::Duel(6);
    scenario.units.resize(7);
    scenario.regiments[0].pose = {};
    scenario.regiments[0].formation = {2, 1.f, .25f};
    scenario.regiments[1].pose = {{6, 0, 0}, 0};
    scenario.regiments[1].formation = {1, 1.f, .25f};
    scenario.regiments[1].team = 0;
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    auto &world = created.value;
    auto &marching = world.regiments.front();
    const GridNavigationService navigation({}, 1.f, 33, 33, {-16, -16, 0});
    Order move;
    move.recipient = marching.id;
    move.type = OrderType::MoveTo;
    move.point = {0, 4, 0};
    move.facing = Pi / 2;
    REQUIRE(SubmitOrder(world, move) == Status::Ok);
    REQUIRE(Step(world, {}, {}, navigation));
    const auto route = marching.route;
    REQUIRE(route.size() == 2);
    const auto turning = marching.pose;
    CHECK(turning.yaw > 0.f);
    auto place_ally = [&](float x) {
        world.regiments[1].pose.position.x = x;
        world.units.back().pose.position.x = x;
    };

    place_ally(1.61f);
    for (int tick = 0; tick < 13; ++tick)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(marching.route == route);
    CHECK(marching.pose == turning);
    REQUIRE(marching.order);
    CHECK(marching.order->status == OrderStatus::Active);

    if (!persistent)
        place_ally(6.f);
    for (int tick = 0; tick < 150; ++tick)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(Distance(marching.pose.position, move.point) < .01f);
    CHECK(marching.pose.yaw == doctest::Approx(*move.facing));
    CHECK(marching.order->status == OrderStatus::Completed);
}
