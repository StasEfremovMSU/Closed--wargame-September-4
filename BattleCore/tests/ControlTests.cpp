#include "ReplayIo.h"
#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Snapshot.h"
#include "doctest.h"
#include <array>
using namespace battle;

TEST_CASE("control.hill_slows_a_possessed_independent_officer_uphill") {
    auto scenario = test::Duel();
    UnitSpec officer;
    officer.id = {3};
    officer.pose.position = {-20, 0, 0};
    SUBCASE("foot officer") {}
    SUBCASE("mounted officer") {
        officer.troop = UnitTroop::Mounted;
        officer.cavalry = CavalryClass::Light;
    }
    scenario.units.push_back(officer);
    scenario.officers.push_back({{1}, officer.id, 1});
    auto flat = CreateWorld(scenario, 42);
    REQUIRE(flat);
    scenario.hill = TerrainHill{};
    auto hill = CreateWorld(scenario, 42);
    REQUIRE(hill);

    FlatNavigationService navigation;
    Command move;
    move.kind = CommandKind::MoveControlledUnit;
    move.unit = officer.id;
    move.velocity = {2, 0, 0};
    const CommandEnvelope input{0, 1, move};
    for (auto *world : {&flat.value, &hill.value}) {
        REQUIRE(AcquireControl(*world, officer.id) == Status::Ok);
        REQUIRE(Step(*world, std::span{&input, 1}, {}, navigation));
    }
    const auto flat_position = FindUnit(flat.value, officer.id)->pose.position;
    const auto hill_position = FindUnit(hill.value, officer.id)->pose.position;
    CHECK(hill_position.x > officer.pose.position.x);
    CHECK(hill_position.x < flat_position.x);
    CHECK(hill_position.z == officer.pose.position.z);
}

TEST_CASE("control.acquire_switch_preserves_state") {
    auto w = CreateWorld(test::Duel(2), 42).value;
    auto &u = w.units[0];
    u.health = .75f;
    u.ready_tick = 99;
    u.equipment = {1, 2};
    CHECK(AcquireControl(w, {1}) == Status::Ok);
    auto next = w.next_event_id;
    CHECK(AcquireControl(w, {1}) == Status::Ok);
    CHECK(w.next_event_id == next);
    CHECK(AcquireControl(w, {2}) == Status::Ok);
    CHECK(u.control == ControlMode::Returning);
    CHECK(u.health == .75f);
    CHECK(u.ready_tick == 99);
    CHECK(u.equipment.size() == 2);
    CHECK(GetStats(w).living_units == 4);
    CHECK(AcquireControl(w, {999}) == Status::NotFound);
    CHECK(w.controlled.value == 2);
}
TEST_CASE("control.single_movement_authority") {
    auto w = CreateWorld(test::Duel(), 42).value;
    FlatNavigationService nav;
    AcquireControl(w, {1});
    auto start = w.units[0].pose.position;
    w.regiments[0].route = {{{0, 0, 0}, 0}};
    Command c;
    c.kind = CommandKind::MoveControlledUnit;
    c.unit = {1};
    c.velocity = {0, 200, 0};
    CommandEnvelope e{0, 1, c};
    REQUIRE(Step(w, std::span{&e, 1}, {}, nav));
    CHECK(w.units[0].pose.position.x == start.x);
    CHECK(w.units[0].pose.position.y == doctest::Approx(.1f));
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.units[0].pose.position.y == doctest::Approx(.2f));
    auto actual = w.units[0].pose.position;
    CHECK(ReleaseControl(w, {1}) == Status::Ok);
    CHECK(w.units[0].pose.position == actual);
    CHECK(w.units[0].control == ControlMode::Returning);
}
TEST_CASE("control.external_input_atomic_conflict_and_latest_sample") {
    auto w = CreateWorld(test::Duel(), 42).value;
    AcquireControl(w, {1});
    FlatNavigationService nav;
    Command pose;
    pose.kind = CommandKind::ExternalPoseSample;
    pose.unit = {1};
    pose.pose.position = {10, 20, 0};
    pose.sample_tick = 0;
    Command move;
    move.kind = CommandKind::MoveControlledUnit;
    move.unit = {1};
    move.velocity = {1, 0, 0};
    std::vector<CommandEnvelope> bad{{0, 1, pose}, {0, 2, move}};
    auto hash = StateHash(w);
    CHECK(Step(w, bad, {}, nav).status == Status::Conflict);
    CHECK(StateHash(w) == hash);
    auto second = pose;
    second.pose.position = {30, 40, 0};
    std::vector<CommandEnvelope> samples{{0, 1, pose}, {0, 2, second}};
    REQUIRE(Step(w, samples, {}, nav));
    CHECK(w.units[0].pose.position.x == 30);
    CHECK(w.units[0].pose.position.y == 40);
    CHECK(SetExternalPose(w, {1}, {}, 0) == Status::InvalidInput);
}
TEST_CASE("control.death_releases_once") {
    auto w = CreateWorld(test::Duel(), 42).value;
    AcquireControl(w, {1});
    w.events.clear();
    BattleEvent e;
    e.id = {100};
    e.kind = EventKind::AttackLaunched;
    e.source = {2};
    e.target = {1};
    e.amount = 100;
    e.hit = true;
    e.projectile = true;
    e.due_tick = 1;
    CommitCombatEvents(w, std::span{&e, 1});
    CHECK_FALSE(w.controlled);
    CHECK(w.units[0].control == ControlMode::Dead);
    CHECK(AcquireControl(w, {1}) == Status::NotFound);
    CHECK(std::count_if(w.events.begin(), w.events.end(),
                        [](auto &e) { return e.kind == EventKind::ControlChanged; }) == 1);
}
TEST_CASE("control.hundred_switches") {
    auto w = CreateWorld(test::Duel(2), 42).value;
    FlatNavigationService nav;
    for (uint64_t i = 0; i < 100; ++i) {
        Command c;
        c.kind = CommandKind::AcquireControl;
        c.unit = {i % 2 + 1};
        CommandEnvelope e{w.tick, i + 1, c};
        REQUIRE(Step(w, std::span{&e, 1}, {}, nav));
    }
    CHECK(GetStats(w).living_units == 4);
    CHECK(w.controlled.value == 2);
}
TEST_CASE("control.returning_and_reforming_respect_static_walls") {
    auto s = test::Duel();
    s.obstacles = {{-5, -3, -4, 3}};
    auto w = CreateWorld(s, 42).value;
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    REQUIRE(SetExternalPose(w, {1}, {{0, 0, 0}, 0}, w.tick) == Status::Ok);
    REQUIRE(ReleaseControl(w, {1}) == Status::Ok);
    for (int t = 0; t < 120; ++t) {
        AdvanceFormations(w, .05f);
        CHECK(w.units[0].pose.position.x >= -3.7001f);
    }
    CHECK(w.units[0].control == ControlMode::Returning);
    CHECK(w.units[0].pose.position.x < 0);
    w.units[0].control = ControlMode::FormationControlled;
    w.units[0].pose.position = {0, 0, 0};
    for (int t = 0; t < 120; ++t) {
        AdvanceFormations(w, .05f);
        CHECK(w.units[0].pose.position.x >= -3.7001f);
    }
}

namespace {
Scenario OfficerDuel() {
    auto scenario = test::Duel(3);
    scenario.officers.push_back({{10}, {1}, 2});
    scenario.officers.push_back({{20}, {4}, 2});
    scenario.units[0].speed = 1.f;
    scenario.regiments[1].pose.position.x = 110.f;
    for (auto &unit : scenario.units)
        if (unit.regiment == RegimentId{2})
            unit.pose = scenario.regiments[1].pose;
    return scenario;
}
Command Face(UnitId unit, float yaw) {
    Command c;
    c.kind = CommandKind::FaceControlledUnit;
    c.unit = unit;
    c.pose = {{100, 100, 100}, yaw};
    return c;
}
Command Follow(UnitId unit, RegimentId regiment, bool enabled = true) {
    Command c;
    c.kind = CommandKind::SetRegimentFollowing;
    c.unit = unit;
    c.regiment = regiment;
    c.follow_enabled = enabled;
    return c;
}
}

TEST_CASE("control.stationary_facing_updates_yaw_without_external_translation") {
    auto created = CreateWorld(OfficerDuel(), 42);
    REQUIRE(created);
    auto &w = created.value;
    FlatNavigationService nav;
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    auto *officer = FindUnit(w, {1});
    const auto start = officer->pose.position;
    officer->desired_velocity = {1, 0, 0};
    const CommandEnvelope face{w.tick, 1, Face(officer->id, Pi / 2)};
    REQUIRE(Step(w, std::span{&face, 1}, {}, nav));
    CHECK(officer->pose.position == start);
    CHECK(officer->pose.yaw == doctest::Approx(Pi / 2));
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(officer->pose.position == start);
    CHECK(officer->pose.yaw == doctest::Approx(Pi / 2));
}

TEST_CASE("control.regiment_tracks_officer_translation_and_stationary_turn_then_releases") {
    auto created = CreateWorld(OfficerDuel(), 42);
    REQUIRE(created);
    auto &w = created.value;
    GridNavigationService nav;
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    auto &officer = *FindUnit(w, {1});
    auto &regiment = *FindRegiment(w, {1});
    const auto formation = regiment.formation;
    REQUIRE(SetExternalPose(w, officer.id, {{0, 0, 0}, 0}, w.tick) == Status::Ok);
    const CommandEnvelope begin{w.tick, 1, Follow(officer.id, regiment.id)};
    REQUIRE(Step(w, std::span{&begin, 1}, {}, nav));
    CHECK(regiment.pose.position.x < -9.f);
    for (int i = 0; i < 140; ++i)
        REQUIRE(Step(w, {}, {}, nav));
    REQUIRE(regiment.pose.position.x > -6.f);
    CHECK(regiment.pose.position.x < -1.f);
    CHECK(std::abs(regiment.pose.position.y) < .1f);

    Command walk;
    walk.kind = CommandKind::MoveControlledUnit;
    walk.unit = officer.id;
    walk.velocity = {1, 0, 0};
    const CommandEnvelope move{w.tick, 2, walk};
    REQUIRE(Step(w, std::span{&move, 1}, {}, nav));
    for (int i = 0; i < 80; ++i)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(regiment.pose.position.x > -1.f);
    const auto turn_from = regiment.pose.position;
    const CommandEnvelope face{w.tick, 3, Face(officer.id, Pi / 2)};
    REQUIRE(Step(w, std::span{&face, 1}, {}, nav));
    for (int i = 0; i < 140; ++i)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(regiment.formation == formation);
    CHECK(regiment.pose.position.x > turn_from.x + 1.f);
    CHECK(regiment.pose.position.x == doctest::Approx(officer.pose.position.x).epsilon(.08));
    CHECK(regiment.pose.position.y < officer.pose.position.y - 1.f);
    CHECK(regiment.pose.yaw == doctest::Approx(Pi / 2).epsilon(.08));
    const auto stationary = regiment.pose;
    for (int i = 0; i < 30; ++i)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(regiment.pose == stationary);

    const CommandEnvelope stop{w.tick, 4, Follow(officer.id, regiment.id, false)};
    REQUIRE(Step(w, std::span{&stop, 1}, {}, nav));
    REQUIRE(SetExternalPose(w, officer.id, {{15, 0, 0}, 0}, w.tick) == Status::Ok);
    for (int i = 0; i < 40; ++i)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(regiment.pose == stationary);
}

TEST_CASE("control.only_assigned_officers_can_start_regiment_follow") {
    auto created = CreateWorld(OfficerDuel(), 42);
    REQUIRE(created);
    auto &w = created.value;
    FlatNavigationService nav;
    REQUIRE(AcquireControl(w, {2}) == Status::Ok);
    const CommandEnvelope soldier{w.tick, 1, Follow({2}, {1})};
    const auto denied = Step(w, std::span{&soldier, 1}, {}, nav);
    REQUIRE(denied);
    CHECK(std::ranges::any_of(denied.value.events,
        [](const auto &e) { return e.kind == EventKind::CommandRejected; }));
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    const CommandEnvelope outsider{w.tick, 2, Follow({1}, {2})};
    const auto outside = Step(w, std::span{&outsider, 1}, {}, nav);
    REQUIRE(outside);
    CHECK(std::ranges::any_of(outside.value.events,
        [](const auto &e) { return e.kind == EventKind::CommandRejected; }));
    const CommandEnvelope officer{w.tick, 3, Follow({1}, {1})};
    const auto accepted = Step(w, std::span{&officer, 1}, {}, nav);
    REQUIRE(accepted);
    CHECK(std::ranges::none_of(accepted.value.events,
        [](const auto &e) { return e.kind == EventKind::CommandRejected; }));
}

TEST_CASE("control.regiment_follow_does_not_pull_out_of_melee") {
    auto scenario = test::Duel(2);
    scenario.officers.push_back({{10}, {1}, 2});
    scenario.rules.local_melee = true;
    scenario.regiments[1].pose.position = {-8.5f, 0, 0};
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.weapon.damage = 0;
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &w = created.value;
    FlatNavigationService nav;
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    REQUIRE(SetExternalPose(w, {1}, {{-20, 1, 0}, Pi}, w.tick) == Status::Ok);
    const auto start = w.regiments[0].pose;
    const CommandEnvelope begin{w.tick, 1, Follow({1}, {1})};
    REQUIRE(Step(w, std::span{&begin, 1}, {}, nav));
    REQUIRE(w.regiments[0].melee_engaged);
    for (int i = 0; i < 30; ++i) {
        REQUIRE(Step(w, {}, {}, nav));
        CHECK(w.regiments[0].pose == start);
    }
}


TEST_CASE("control.higher_officer_can_follow_one_regiment_in_its_subtree") {
    auto scenario = OfficerDuel();
    auto other = scenario.regiments[0];
    other.id = {3};
    other.pose.position.x = -100.f;
    scenario.regiments.push_back(other);
    auto outsider = other;
    outsider.id = {4};
    outsider.pose.position = {-50, 20, 0};
    scenario.regiments.push_back(outsider);
    for (auto r : {other, outsider}) {
        auto unit = scenario.units[1];
        unit.id = {7 + r.id.value};
        unit.regiment = r.id;
        unit.pose = r.pose;
        scenario.units.push_back(unit);
    }

    UnitSpec general, captain;
    general.id = {16};
    general.pose.position = {-15, 0, 0};
    captain.id = {17};
    captain.pose.position = {-18, 0, 0};
    scenario.units.push_back(general);
    scenario.units.push_back(captain);
    scenario.officers.push_back({{16}, general.id, 1});
    scenario.officers.push_back({{17}, captain.id, 1});

    command::CommandSetup setup;
    command::CommandNode army, group, first, second;
    army.id = {1};
    army.level = command::Level::Army;
    army.commander = {16};
    army.children = {{2}};
    group.id = {2};
    group.parent = army.id;
    group.level = command::Level::Group;
    group.commander = {17};
    group.children = {{3}, {4}};
    first.id = {3};
    first.parent = group.id;
    first.level = command::Level::Regiment;
    first.regiment = {1};
    first.commander = {10};
    second = first;
    second.id = {4};
    second.regiment = other.id;
    second.commander = {};
    setup.hierarchy.nodes = {army, group, first, second};
    for (auto id : {army.commander, group.commander, first.commander}) {
        command::OfficerProfile profile;
        profile.officer = id;
        setup.hierarchy.officers.push_back(profile);
    }
    scenario.command_setup = setup;
    const auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto w = created.value;
    REQUIRE(AcquireControl(w, general.id) == Status::Ok);
    CHECK_FALSE(CanCommandRegiment(w, general.id, {4}));
    CHECK(CanCommandRegiment(w, general.id, {1}));
    CHECK(CanCommandRegiment(w, captain.id, {3}));
    CHECK(ApplyControlCommand(w, Follow(general.id, {4})) == Status::Conflict);
    CHECK(ApplyControlCommand(w, Follow(general.id, {1})) == Status::Ok);
    CHECK(ApplyControlCommand(w, Follow(general.id, {3})) == Status::Ok);
    FlatNavigationService nav;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK_FALSE(w.regiments[0].following_officer);
    CHECK(w.regiments[2].following_officer == general.id);
    CHECK(w.regiments[0].pose.position.x == -10.f);
    CHECK(w.regiments[2].pose.position.x > -100.f);
    REQUIRE(AcquireControl(w, captain.id) == Status::Ok);
    const auto stationary = w.regiments[2].pose;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.regiments[2].pose == stationary);
    CHECK(ApplyControlCommand(w, Follow(captain.id, {1})) == Status::Ok);
}

TEST_CASE("control.release_and_death_immediately_detach_follow") {
    auto created = CreateWorld(OfficerDuel(), 42);
    REQUIRE(created);
    auto &w = created.value;
    FlatNavigationService nav;
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    REQUIRE(SetExternalPose(w, {1}, {{0, 0, 0}, 0}, w.tick) == Status::Ok);
    REQUIRE(ApplyControlCommand(w, Follow({1}, {1})) == Status::Ok);
    REQUIRE(Step(w, {}, {}, nav));
    CHECK_FALSE(w.regiments[0].route.empty());
    REQUIRE(ReleaseControl(w, {1}) == Status::Ok);
    CHECK_FALSE(w.regiments[0].following_officer);
    CHECK(w.regiments[0].route.empty());

    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    REQUIRE(ApplyControlCommand(w, Follow({1}, {1})) == Status::Ok);
    REQUIRE(Step(w, {}, {}, nav));
    REQUIRE_FALSE(w.regiments[0].route.empty());
    BattleEvent attack;
    attack.id = {w.next_event_id++};
    attack.kind = EventKind::AttackLaunched;
    attack.source = {4};
    attack.target = {1};
    attack.amount = 100;
    attack.hit = true;
    attack.projectile = true;
    attack.due_tick = w.tick + 1;
    CommitCombatEvents(w, std::span{&attack, 1});
    CHECK_FALSE(w.controlled);
    CHECK_FALSE(w.regiments[0].following_officer);
    CHECK(w.regiments[0].route.empty());
}


TEST_CASE("control.follow_routes_the_formation_around_static_terrain") {
    auto scenario = OfficerDuel();
    scenario.obstacles = {{-6, -2, -4, 2}};
    const GridNavigationService nav(scenario.obstacles, 1.f, 61, 37, {-30, -18, 0});
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &w = created.value;
    auto &r = w.regiments[0];
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    REQUIRE(SetExternalPose(w, {1}, {{5, 0, 0}, 0}, w.tick) == Status::Ok);
    REQUIRE(ApplyControlCommand(w, Follow({1}, r.id)) == Status::Ok);
    for (int tick = 0; tick < 420; ++tick) {
        REQUIRE(Step(w, {}, {}, nav));
        CHECK_FALSE(RectanglesOverlap(r.pose, Footprint(r), {{-5, 0, 0}, 0}, {4, 2}));
    }
    CHECK(r.pose.position.x > 1.f);
    CHECK(r.pose.position.x < 4.f);
    CHECK(std::abs(r.pose.position.y) < .35f);
}

TEST_CASE("control.follow_yields_to_deployment_and_panic") {
    auto scenario = OfficerDuel();
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].deployment_ticks = 20;
    scenario.regiments[0].rally_point = {-30, 0, 0};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &w = created.value;
    auto &r = w.regiments[0];
    FlatNavigationService nav;
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    REQUIRE(SetExternalPose(w, {1}, {{10, 0, 0}, 0}, w.tick) == Status::Ok);
    REQUIRE(ApplyControlCommand(w, Follow({1}, r.id)) == Status::Ok);
    const auto deployed = r.pose;
    for (int tick = 0; tick < 20; ++tick) {
        REQUIRE(Step(w, {}, {}, nav));
        CHECK(r.pose == deployed);
    }
    for (int tick = 0; tick < 20; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(r.pose.position.x > deployed.position.x + 1.f);

    r.panic = true;
    r.morale = 0.f;
    const float before_panic = r.pose.position.x;
    for (int tick = 0; tick < 30; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(r.ai_action == OrderType::Withdraw);
    CHECK(r.following_officer == UnitId{1});
    CHECK(r.pose.position.x < before_panic - 1.f);
}

TEST_CASE("control.releasing_follow_resumes_a_delivered_move_order") {
    auto created = CreateWorld(OfficerDuel(), 42);
    REQUIRE(created);
    auto &w = created.value;
    auto &r = w.regiments[0];
    FlatNavigationService nav;
    REQUIRE(AcquireControl(w, {1}) == Status::Ok);
    REQUIRE(SetExternalPose(w, {1}, {{5, 0, 0}, 0}, w.tick) == Status::Ok);
    REQUIRE(ApplyControlCommand(w, Follow({1}, r.id)) == Status::Ok);
    Order move;
    move.recipient = r.id;
    move.type = OrderType::MoveTo;
    move.point = {-25, 0, 0};
    REQUIRE(SubmitOrder(w, move) == Status::Ok);
    for (int tick = 0; tick < 20; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    REQUIRE(r.order);
    CHECK(r.order->status == OrderStatus::Active);
    CHECK(r.pose.position.x > -9.f);
    const float halfway = r.pose.position.x;
    REQUIRE(ReleaseControl(w, {1}) == Status::Ok);
    for (int tick = 0; tick < 20; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(r.pose.position.x < halfway - 1.f);
}

TEST_CASE("control.jump_changes_simulation_height_while_preserving_wall_collision") {
    auto scenario = test::Duel();
    scenario.rules.local_melee = true;
    scenario.obstacles.push_back({-9.35f, -2.f, -9.2f, 2.f, true});
    auto result = CreateWorld(scenario, 42);
    REQUIRE(result);
    auto &world = result.value;
    REQUIRE(AcquireControl(world, {1}) == Status::Ok);

    const FlatNavigationService nav;
    Command jump;
    jump.kind = CommandKind::JumpControlledUnit;
    jump.unit = {1};
    auto move = jump;
    move.kind = CommandKind::MoveControlledUnit;
    move.velocity = {2.f, 0.f, 0.f};
    const std::array opening{CommandEnvelope{0, 1, jump}, CommandEnvelope{0, 2, move}};
    REQUIRE(Step(world, opening, {}, nav));
    CHECK(world.units.front().pose.position.z > .1f);
    for (int i = 1; i < 6; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(world.units.front().pose.position.z > .7f);
    const CommandEnvelope repeated{world.tick, 3, jump};
    REQUIRE(Step(world, std::span{&repeated, 1}, {}, nav));
    const test::TempDir temp;
    const auto path = temp.path / "mid-jump.json";
    REQUIRE(io::SaveSnapshot(path, Capture(world)));
    auto restored = io::LoadSnapshot(path);
    REQUIRE(restored);
    for (int i = 7; i < 13; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        REQUIRE(Step(restored.value.state, {}, {}, nav));
    }
    CHECK(StateHash(world) == StateHash(restored.value.state));
    CHECK(world.units.front().pose.position.z == 0.f);
    CHECK(world.units.front().pose.position.x > -10.f);
    CHECK(world.units.front().pose.position.x < -9.5f);
}

TEST_CASE("control.parry_cancels_one_frontal_melee_hit_and_allows_a_counter") {
    auto scenario = test::Duel();
    scenario.regiments[0].pose.position = {};
    scenario.regiments[1].pose.position = {1.1f, 0, 0};
    for (size_t i = 0; i < scenario.units.size(); ++i) {
        scenario.units[i].pose = scenario.regiments[i].pose;
        scenario.units[i].health = 20.f;
    }
    bool rear = false, projectile = false, late = false, archer = false;
    SUBCASE("frontal melee at the end of the window") {}
    SUBCASE("rear attack") { rear = true; }
    SUBCASE("ranged attack") { projectile = true; }
    SUBCASE("strike after parry ends") { late = true; }
    SUBCASE("bow reload survives parry") { archer = true; }
    scenario.units.front().archer = archer;
    auto result = CreateWorld(scenario, 42);
    REQUIRE(result);
    auto &world = result.value;
    REQUIRE(AcquireControl(world, {1}) == Status::Ok);
    world.units.front().ready_tick = 99;
    world.units.front().dagger_ready_tick = 99;
    if (rear)
        world.units.back().pose.position.x = -1.1f;

    BattleEvent attack;
    attack.id = {world.next_event_id};
    attack.kind = EventKind::AttackLaunched;
    attack.source = {2};
    attack.target = {1};
    attack.regiment = {2};
    attack.amount = 4.f;
    attack.hit = true;
    attack.projectile = projectile;
    attack.due_tick = late ? 7 : 6;
    auto second = attack;
    ++second.id.value;
    std::array attacks{attack, second};
    CommitCombatEvents(world, std::span(attacks).first(rear || projectile || late ? 1 : 2));
    world.units.back().ready_tick = 100;

    Command parry;
    parry.kind = CommandKind::ParryControlledUnit;
    parry.unit = {1};
    parry.pose.yaw = 0.f;
    const CommandEnvelope input{0, 1, parry};
    const FlatNavigationService nav;
    REQUIRE(Step(world, std::span{&input, 1}, {}, nav));
    while (world.tick < attack.due_tick)
        REQUIRE(Step(world, {}, {}, nav));
    const auto event = std::ranges::find(world.events, EventKind::Parried, &BattleEvent::kind);
    if (rear || projectile || late) {
        CHECK(event == world.events.end());
        CHECK(world.units.front().ready_tick == 99);
    } else {
        REQUIRE(event != world.events.end());
        CHECK(event->source == UnitId{1});
        CHECK(event->target == UnitId{2});
        CHECK(event->cause == attack.id);
        if (archer) {
            CHECK(world.units.front().ready_tick == 99);
            CHECK(world.units.front().dagger_ready_tick <= world.tick);
        } else {
            CHECK(world.units.front().ready_tick <= world.tick);
        }
        CHECK(std::ranges::count(world.events, EventKind::Parried, &BattleEvent::kind) == 1);
    }
    CHECK(world.units.front().health == doctest::Approx(16.f));
    if (!rear && !projectile && !late && !archer) {
        Command counter;
        counter.kind = CommandKind::AttackControlledUnit;
        counter.unit = {1};
        counter.target = {2};
        const CommandEnvelope input{world.tick, 2, counter};
        REQUIRE(Step(world, std::span{&input, 1}, {}, nav));
        CHECK(std::ranges::any_of(world.events, [](const auto &event) {
            return event.kind == EventKind::AttackLaunched && event.source == UnitId{1} &&
                   event.target == UnitId{2};
        }));
    }
}
