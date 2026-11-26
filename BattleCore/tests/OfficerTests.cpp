#include "TestSupport.h"
#include "ReplayIo.h"
#include "battle/Officers.h"
#include "battle/Control.h"
#include "doctest.h"
using namespace battle;

TEST_CASE("officers.held_crossing_faces_the_nearest_visible_threat_in_place") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    const Pose at_crossing{{-6.f, 0.f, 0.f}, Pi * .5f};
    scenario.regiments[0].pose = at_crossing;
    scenario.regiments[1].pose.position.x = 7.f;
    auto concealed = scenario.regiments[1];
    concealed.id = {3};
    concealed.pose.position = {-6.f, 6.f, 0.f};
    scenario.regiments.push_back(concealed);
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    for (uint64_t i = 7; i <= 9; ++i) {
        UnitSpec unit;
        unit.id = {i};
        unit.regiment = concealed.id;
        unit.pose = concealed.pose;
        scenario.units.push_back(unit);
    }
    scenario.obstacles = {{-2.f, -25.f, 2.f, -4.f, false, TerrainFeature::River},
                          {-2.f, 4.f, 2.f, 25.f, false, TerrainFeature::River},
                          {-8.f, 3.f, -4.f, 4.f, true, TerrainFeature::StoneWall}};
    Order order;
    order.recipient = {1};
    order.type = OrderType::HoldLine;
    order.point = at_crossing.position;
    order.player_direct = true;
    bool auto_facing = true;
    bool ai_guard = false;
    bool detour = false;
    SUBCASE("hold with local facing") {}
    SUBCASE("hold position after a move without prescribed facing") {
        order.type = OrderType::MoveTo;
    }
    SUBCASE("a delivered guard mission faces the visible crossing") {
        ai_guard = true;
        namespace c = battle::command;
        auto &setup = scenario.command_setup.emplace();
        scenario.officers = {{{1}, {1}, 2}, {{2}, {2}, 1}};
        for (const auto &officer : scenario.officers) {
            c::OfficerProfile profile;
            profile.officer = officer.id;
            profile.command_radius_m = 35.f;
            setup.hierarchy.officers.push_back(profile);
        }
        c::CommandNode leaf;
        leaf.id = {1};
        leaf.parent = {2};
        leaf.regiment = {1};
        leaf.commander = {1};
        c::CommandNode group;
        group.id = {2};
        group.level = c::Level::Group;
        group.children = {leaf.id};
        group.commander = {2};
        setup.hierarchy.nodes = {leaf, group};
        c::CommandTask guard;
        guard.id = {1};
        guard.recipient = group.id;
        guard.action = c::TaskKind::Defend;
        guard.intent.objective_position = at_crossing.position;
        setup.missions.push_back(guard);
    }
    SUBCASE("honor an explicit standing facing") {
        auto_facing = false;
        order.facing = at_crossing.yaw;
    }
    SUBCASE("honor the facing of a completed move") {
        auto_facing = false;
        order.type = OrderType::MoveTo;
        order.facing = at_crossing.yaw;
    }
    SUBCASE("hold facing when the in-place turn is obstructed") {
        auto_facing = false;
        scenario.obstacles.push_back({-6.4f, .8f, -5.6f, 1.1f, true, TerrainFeature::StoneWall});
    }
    SUBCASE("a blocked explicit pivot cannot send the guard around a wall") {
        auto_facing = false;
        detour = true;
        order.facing = 0.f;
        scenario.obstacles.push_back({-5.2f, .8f, -5.f, 1.f, true, TerrainFeature::StoneWall});
    }
    GridNavigationService navigation(scenario.obstacles, 1.f, 61, 51, {-30.f, -25.f, 0.f});
    struct DetourNavigation final : NavigationService {
        PathResult FindPath(const PathRequest &request) const override {
            const auto turn_location = request.start.position + Vec3{-2.f, 0.f, 0.f};
            const float yaw = request.arrival_yaw.value_or(request.start.yaw);
            return {Status::Ok, {{turn_location, request.start.yaw}, {turn_location, yaw},
                                 {request.goal, yaw}}};
        }
    } detour_navigation;
    const NavigationService &active_navigation = detour ?
        static_cast<const NavigationService &>(detour_navigation) : navigation;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    if (!ai_guard)
        REQUIRE(SubmitOrder(world, order) == Status::Ok);
    for (int tick = 0; tick < 100; ++tick) {
        REQUIRE(Step(world, {}, {}, active_navigation));
        CHECK(world.regiments[0].pose.position == at_crossing.position);
        CHECK_FALSE(world.regiments[0].ai_target);
        if (auto_facing && tick == 0)
            CHECK(world.regiments[0].pose.yaw > 1.f);
    }
    const auto &regiment = world.regiments[0];
    const float expected_yaw = auto_facing ? 0.f : at_crossing.yaw;
    CHECK(std::abs(std::remainder(regiment.pose.yaw - expected_yaw, 2 * Pi)) < .05f);
    CHECK(IsHolding(regiment.ai_action));
}

TEST_CASE("officers.adaptive_execution_keeps_active_orders_until_the_next_phase") {
    auto scenario = test::Duel();
    scenario.rules.adaptive_battle = true;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    Order order;
    order.id = {1};
    order.recipient = regiment.id;
    order.status = OrderStatus::Active;
    order.expiry_tick = 2;
    regiment.order = order;
    FlatNavigationService navigation;
    UpdateOfficers(world, navigation);
    REQUIRE(regiment.decision_tick > 2);
    CHECK(std::ranges::any_of(world.events, [](const auto &event) {
        return event.kind == EventKind::DecisionTrace && event.regiment == RegimentId{1};
    }));

    world.tick = 1;
    world.events.clear();
    UpdateOfficers(world, navigation);
    CHECK(std::ranges::none_of(world.events, [](const auto &event) {
        return event.kind == EventKind::DecisionTrace && event.regiment == RegimentId{1};
    }));
    CHECK(regiment.order->status == OrderStatus::Active);

    world.tick = 2;
    UpdateOfficers(world, navigation);
    CHECK(regiment.order->status == OrderStatus::Cancelled);
}

TEST_CASE("officers.held_line_faces_a_visible_enemy_inside_local_sight") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    scenario.regiments[0].pose.yaw = Pi;
    scenario.regiments[1].pose.position.x = 14.f;
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    Order hold;
    hold.type = OrderType::HoldLine;
    hold.recipient = {1};
    hold.point = scenario.regiments[0].pose.position;
    hold.player_direct = true;
    REQUIRE(SubmitOrder(world, hold) == Status::Ok);
    FlatNavigationService nav;
    for (int tick = 0; tick < 140; ++tick)
        REQUIRE(Step(world, {}, {}, nav));
    const auto &regiment = world.regiments[0];
    CHECK(regiment.pose.position == hold.point);
    CHECK(std::abs(regiment.pose.yaw) < .05f);
    CHECK(regiment.ai_action == OrderType::HoldLine);
}

TEST_CASE("officers.no_enemy_and_role_selection") {
    auto s = test::Duel(3);
    s.regiments.resize(1);
    s.units.resize(3);
    s.officers = {{{1}, {1}, 1}, {{2}, {2}, 2}, {{3}, {3}, 4}};
    auto w = CreateWorld(s, 42).value;
    CHECK(Commander(w, {1}).value == 2);
    SpatialIndex index;
    auto p = Observe(w, {2}, index);
    CHECK(Evaluate(p, {}, nullptr).action == OrderType::Hold);
    FindUnit(w, {2})->alive = false;
    CHECK(Commander(w, {1}).value == 1);
    CHECK(GetStats(w).dead_units == 1);
}
TEST_CASE("officers.delivery_priority_and_no_commander") {
    auto w = CreateWorld(test::Duel(), 42).value;
    FlatNavigationService nav;
    Order a;
    a.id = {1};
    a.recipient = {1};
    a.type = OrderType::MoveTo;
    a.point = {-5, 0, 0};
    a.delivery_tick = 5;
    a.priority = 10;
    CHECK(SubmitOrder(w, a) == Status::Ok);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(Step(w, {}, {}, nav));
    }
    CHECK(w.regiments[0].pose.position.x == -10);
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.regiments[0].pose.position.x > -10);
    Order b = a;
    b.id = {2};
    b.type = OrderType::Hold;
    b.delivery_tick = 5;
    b.priority = 0;
    CHECK(SubmitOrder(w, b) == Status::Ok);
    REQUIRE(Step(w, {}, {}, nav));
    REQUIRE(w.regiments[0].order);
    CHECK(w.regiments[0].order->id.value == 1);
    CHECK_FALSE(Commander(w, {1}));
    CHECK(w.regiments[1].route.empty());
}
TEST_CASE("officers.dead_target_and_no_path") {
    auto w = CreateWorld(test::Duel(), 42).value;
    FindUnit(w, {2})->alive = false;
    Order a;
    a.recipient = {1};
    a.type = OrderType::Attack;
    a.target_unit = {2};
    CHECK(SubmitOrder(w, a) == Status::NotFound);
    REQUIRE(w.regiments[0].order);
    CHECK(w.regiments[0].order->status == OrderStatus::Failed);
    a = {};
    a.recipient = {1};
    a.type = OrderType::MoveTo;
    a.point = {200, 0, 0};
    SubmitOrder(w, a);
    GridNavigationService nav({}, 1, 30, 30, {-15, -15, 0});
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.regiments[0].order->status == OrderStatus::Failed);
}
TEST_CASE("officers.reevaluation_and_single_mover") {
    auto s = test::Duel(3);
    s.officers = {{{1}, {1}, 1}, {{2}, {2}, 3}, {{3}, {3}, 4}};
    s.officers[0].tactic_mind = 1;
    auto w = CreateWorld(s, 42).value;
    FlatNavigationService nav;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.regiments[0].decision_tick == 10);
    CHECK(w.regiments[0].decision_until == 20);
    auto before = w.regiments[0].pose.position;
    CHECK(before.x > -10);
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(Distance(before, w.regiments[0].pose.position) <= .1001f);
    CHECK(w.regiments[0].decision_tick == 10);
    for (auto &u : w.units)
        if (u.regiment == RegimentId{2})
            u.alive = false;
    w.regiments[1].units.clear();
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.regiments[0].ai_action == OrderType::Hold);
}
TEST_CASE("officers.rejected_attack_preserves_active_order") {
    auto w = CreateWorld(test::Duel(), 42).value;
    FlatNavigationService nav;
    Order move;
    move.recipient = {1};
    move.type = OrderType::MoveTo;
    move.priority = 10;
    move.point = {-5, 0, 0};
    REQUIRE(SubmitOrder(w, move) == Status::Ok);
    REQUIRE(Step(w, {}, {}, nav));
    auto id = w.regiments[0].order->id;
    auto route = w.regiments[0].route;
    auto position = w.regiments[0].pose.position;
    Command bad;
    bad.kind = CommandKind::IssueOrder;
    bad.order.recipient = {1};
    bad.order.type = OrderType::Attack;
    bad.order.target_unit = {99999};
    bad.order.priority = 0;
    CommandEnvelope input{w.tick, 1, bad};
    REQUIRE(Step(w, std::span{&input, 1}, {}, nav));
    REQUIRE(w.regiments[0].order);
    CHECK(w.regiments[0].order->id == id);
    CHECK(w.regiments[0].order->status == OrderStatus::Active);
    CHECK(w.regiments[0].route == route);
    CHECK(w.regiments[0].pose.position.x > position.x);
    CHECK(std::any_of(w.events.begin(), w.events.end(),
                      [](auto &e) { return e.kind == EventKind::CommandRejected; }));
}

TEST_CASE("officers.melee_regiment_advances_around_an_allied_formation") {
    auto scenario = test::Duel(4);
    scenario.rules.local_melee = true;
    for (auto &unit : scenario.units) {
        unit.health = 100.f;
        unit.weapon = {.01f, 1.6f, 10, 1};
    }
    auto blocking = scenario.regiments.front();
    blocking.id = {3};
    blocking.pose.position = {-5, 0, 0};
    scenario.regiments.push_back(blocking);
    for (uint64_t i = 0; i < 4; ++i) {
        auto unit = scenario.units[i];
        unit.id = {9 + i};
        unit.regiment = blocking.id;
        unit.pose = blocking.pose;
        scenario.units.push_back(unit);
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService nav({}, 1, 49, 31, {-24, -15, 0});

    Order attack;
    attack.recipient = {1};
    attack.type = OrderType::Attack;
    attack.target_regiment = {2};
    REQUIRE(SubmitOrder(world, attack) == Status::Ok);
    bool passed_ally = false;
    for (unsigned i = 0; i < 450 && !passed_ally; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        passed_ally = world.regiments.front().pose.position.x > 3.f;
    }
    CHECK(passed_ally);
    REQUIRE(world.regiments.front().order);
    CHECK(world.regiments.front().order->status == OrderStatus::Active);
}

TEST_CASE("officers.player_attack_approaches_reported_point_until_recipient_sees_target") {
    auto scenario = test::Duel(2);
    scenario.rules.local_melee = true;
    scenario.regiments[1].pose.position = {60, 0, 0};
    for (auto &unit : scenario.units)
        if (unit.regiment == RegimentId{2})
            unit.pose = scenario.regiments[1].pose;
    scenario.officers.push_back({{10}, {1}, 2});
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto world = std::move(created.value);
    auto &regiment = world.regiments[0];
    FlatNavigationService nav;
    Command acquire;
    acquire.kind = CommandKind::AcquireControl;
    acquire.unit = {1};
    Command attack;
    attack.kind = CommandKind::IssueOrder;
    attack.order.issuer = {10};
    attack.order.recipient = {1};
    attack.order.type = OrderType::Attack;
    attack.order.priority = PlayerOrderPriority;
    attack.order.point = {60, 0, 0};
    attack.order.target_regiment = {2};

    auto autonomous = world;
    REQUIRE(SubmitOrder(autonomous, attack.order) == Status::Ok);
    REQUIRE(Step(autonomous, {}, {}, nav));
    CHECK(autonomous.regiments[0].ai_action == OrderType::Hold);
    CHECK(autonomous.regiments[0].pose == regiment.pose);

    const CommandEnvelope input[]{{0, 1, acquire}, {0, 2, attack}};
    REQUIRE(Step(world, input, {}, nav));
    REQUIRE(regiment.order);
    CHECK(regiment.order->status == OrderStatus::Active);
    REQUIRE_FALSE(regiment.route.empty());
    CHECK(regiment.route.back().position == attack.order.point);
    CHECK_FALSE(regiment.ai_target);
    CHECK(regiment.pose.position.x > -10.f);

    test::TempDir dir;
    const auto path = dir.path / "active-attack.json";
    REQUIRE(io::SaveSnapshot(path, Capture(world)));
    const auto saved = io::LoadSnapshot(path);
    REQUIRE(saved);
    CHECK(StateHash(saved.value.state) == StateHash(world));
    REQUIRE(saved.value.state.regiments[0].order);
    CHECK(saved.value.state.regiments[0].order->player_direct);

    REQUIRE(ReleaseControl(world, {1}) == Status::Ok);
    world.regiments[1].pose.position = {61, 7, 0};
    for (auto &unit : world.units)
        if (unit.regiment == RegimentId{2})
            unit.pose.position = unit.pose.position + Vec3{1, 7, 0};
    for (int i = 0; i < 40; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK_FALSE(regiment.ai_target);
    REQUIRE_FALSE(regiment.route.empty());
    CHECK(regiment.route.back().position == attack.order.point);
    for (int i = 0; i < 150; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.ai_action == OrderType::Attack);
    CHECK(regiment.ai_target == RegimentId{2});
    CHECK(regiment.order->status == OrderStatus::Active);
}
