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

TEST_CASE("navigation.a_player_march_reaches_the_enemy_that_enters_its_destination") {
    auto scenario = test::Duel(6);
    scenario.rules.regiment_tactics = true;
    scenario.rules.adaptive_battle = true;
    scenario.regiments[0].pose = {{-14.f, 0.f, 0.f}, 0.f};
    scenario.regiments[1].pose = {{12.f, 6.f, 0.f}, -Pi / 2.f};
    for (auto &regiment : scenario.regiments) {
        regiment.formation = {3, 1.f, .25f};
        regiment.speed = 3.f;
    }
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.health = 1000.f;
    }

    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    auto &world = created.value;
    const GridNavigationService navigation({}, 1.f, 65, 65, {-32, -32, 0});
    const auto &marching = world.regiments.front();
    const auto &blocker = world.regiments.back();
    Order march;
    march.recipient = marching.id;
    march.type = OrderType::MoveTo;
    march.point = {12.f, 0.f, 0.f};
    march.facing = 0.f;
    march.player_direct = true;
    march.priority = PlayerOrderPriority;
    REQUIRE(SubmitOrder(world, march) == Status::Ok);
    REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE_FALSE(marching.route.empty());

    Order intercept;
    intercept.recipient = blocker.id;
    intercept.type = OrderType::MoveTo;
    intercept.point = march.point;
    REQUIRE(SubmitOrder(world, intercept) == Status::Ok);
    for (int tick = 0; tick < 300; ++tick)
        REQUIRE(Step(world, {}, {}, navigation));

    REQUIRE(marching.order);
    CAPTURE(marching.pose.position.x);
    CHECK(Distance(blocker.pose.position, march.point) < .01f);
    CHECK(marching.pose.position.x > 9.f);
    CHECK(marching.order->status == OrderStatus::Active);
    CHECK_FALSE(RectanglesOverlap(marching.pose, Footprint(marching), blocker.pose, Footprint(blocker)));

    intercept.point.y = -6.f;
    REQUIRE(SubmitOrder(world, intercept) == Status::Ok);
    for (int tick = 0; tick < 140; ++tick)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(Distance(marching.pose.position, march.point) < .01f);
    CHECK(marching.order->status == OrderStatus::Completed);
}

TEST_CASE("navigation.field_march_resolves_a_lane_blocked_by_a_wide_formation") {
    auto scenario = test::Duel(18);
    scenario.rules.regiment_tactics = true;
    scenario.rules.adaptive_battle = true;
    scenario.field_bounds = TerrainFieldBounds{32.f, 32.f};
    scenario.regiments[0].pose.position = {-12.f, 0.f, 0.f};
    scenario.regiments[0].formation = {6, 1.f, .25f};
    scenario.regiments[1].pose = {};
    scenario.regiments[1].formation = {18, 1.f, .25f};
    scenario.regiments[1].team = 0;
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;

    bool passage_available = true;
    SUBCASE("there is room outside the local detour") {}
    SUBCASE("passage is closed by the ally and both walls") {
        passage_available = false;
        scenario.obstacles = {{-32.f, -32.f, 32.f, -9.f}, {-32.f, 9.f, 32.f, 32.f}};
    }

    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    const GridNavigationService navigation(scenario.obstacles, 1.f, 65, 65, {-32, -32, 0});
    const auto &marching = world.regiments.front();
    const auto &blocking = world.regiments.back();
    Order march;
    march.recipient = marching.id;
    march.type = OrderType::MoveTo;
    march.point = {12.f, 0.f, 0.f};
    REQUIRE(SubmitOrder(world, march) == Status::Ok);

    bool diagnosed_block = false;
    float max_lateral_position = 0.f;
    for (int tick = 0; tick < 900; ++tick) {
        REQUIRE(Step(world, {}, {}, navigation));
        CHECK_FALSE(RectanglesOverlap(marching.pose, Footprint(marching), blocking.pose, Footprint(blocking)));
        max_lateral_position = std::max(max_lateral_position, std::abs(marching.pose.position.y));
        for (const auto &event : world.events)
            diagnosed_block |= event.regiment == marching.id && event.kind == EventKind::DecisionTrace &&
                event.reason == "assigned movement route blocked by formations";
        if (marching.order && (marching.order->status == OrderStatus::Completed ||
                              marching.order->status == OrderStatus::Failed))
            break;
    }

    REQUIRE(marching.order);
    CAPTURE(max_lateral_position);
    CAPTURE(marching.pose.position.x);
    if (passage_available) {
        CHECK(max_lateral_position > 8.f);
        CHECK(Distance(marching.pose.position, march.point) < .01f);
        CHECK(marching.order->status == OrderStatus::Completed);
    } else {
        CHECK(marching.pose.position.x < 0.f);
        CHECK(marching.order->status == OrderStatus::Failed);
        CHECK(diagnosed_block);
    }
}
