#include "TestSupport.h"
#include "JsonBindings.h"
#include "battle/Combat.h"
#include "battle/Control.h"
#include "doctest.h"

using namespace battle;

namespace {
Scenario PlatformDuel(BattlePlatform platform) {
    auto scenario = test::Duel();
    scenario.rules.ranged_cohorts = 1;
    scenario.regiments[0].pose = {{0, 0, 0}, 0};
    scenario.regiments[1].pose = {{16, 0, 0}, Pi};
    scenario.regiments[0].formation.columns = 1;
    scenario.regiments[1].formation.columns = 1;
    scenario.units[0].pose = scenario.regiments[0].pose;
    scenario.units[1].pose = scenario.regiments[1].pose;
    auto &unit = scenario.units[0];
    unit.platform = platform;
    unit.archer = platform == BattlePlatform::Ballista || platform == BattlePlatform::Catapult;
    if (unit.archer)
        unit.weapon = {1.f, 60.f, 1, 1, 1000.f, 1.f};
    else
        unit.troop = UnitTroop::Mounted;
    scenario.units[1].health = 100.f;
    return scenario;
}

World MakeWorld(Scenario scenario) {
    auto world = CreateWorld(scenario, 31);
    REQUIRE(world);
    return std::move(world.value);
}

std::optional<BattleEvent> LaunchFrom(const World &world, UnitId source = {1}) {
    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    const auto launch = std::ranges::find(launches, source, &BattleEvent::source);
    return launch == launches.end() ? std::optional<BattleEvent>{} : *launch;
}
} // namespace

TEST_CASE("combat.ancient_artillery_range_and_forty_five_degree_half_sector") {
    auto catapult = MakeWorld(PlatformDuel(BattlePlatform::Catapult));
    auto ballista = MakeWorld(PlatformDuel(BattlePlatform::Ballista));
    FindUnit(catapult, {2})->pose.position = {9.99f, 0, 0};
    FindUnit(ballista, {2})->pose.position = {1.f, 0, 0};
    CHECK_FALSE(LaunchFrom(catapult));
    REQUIRE(LaunchFrom(ballista));
    CHECK_FALSE(LaunchFrom(ballista)->high_arc);
    FindUnit(catapult, {2})->pose.position = {10, 0, 0};
    REQUIRE(LaunchFrom(catapult));
    CHECK(LaunchFrom(catapult)->high_arc);

    for (auto *world : {&catapult, &ballista}) {
        auto &position = FindUnit(*world, {2})->pose.position;
        position = {12, 12, 0};
        CHECK(LaunchFrom(*world));
        position.y = 12.1f;
        CHECK_FALSE(LaunchFrom(*world));
    }
    FindUnit(catapult, {2})->pose.position = {40.1f, 0, 0};
    FindUnit(ballista, {2})->pose.position = {30.1f, 0, 0};
    CHECK_FALSE(LaunchFrom(catapult));
    CHECK_FALSE(LaunchFrom(ballista));
}

TEST_CASE("combat.ancient_catapult_lobs_over_allies_while_ballista_needs_a_clear_lane") {
    const auto battery = [](BattlePlatform platform) {
        auto scenario = PlatformDuel(platform);
        auto infantry = scenario.regiments.front();
        infantry.id = {3};
        infantry.pose.position.x = 4.f;
        scenario.regiments.push_back(infantry);
        UnitSpec ally;
        ally.id = {3};
        ally.regiment = infantry.id;
        ally.pose = infantry.pose;
        scenario.units.push_back(ally);
        return MakeWorld(scenario);
    };
    auto catapult = battery(BattlePlatform::Catapult);
    auto ballista = battery(BattlePlatform::Ballista);
    REQUIRE(LaunchFrom(catapult));
    CHECK(LaunchFrom(catapult)->high_arc);
    CHECK_FALSE(LaunchFrom(ballista));
    FindUnit(ballista, {3})->pose.position.y = 2.f;
    CHECK(LaunchFrom(ballista));
    ballista.scenario.obstacles.push_back({8, -1, 9, 1, true});
    CHECK_FALSE(LaunchFrom(ballista));
}

TEST_CASE("combat.ancient_artillery_consumes_ammunition_and_has_a_slow_reload") {
    for (const auto platform : {BattlePlatform::Catapult, BattlePlatform::Ballista}) {
        auto world = MakeWorld(PlatformDuel(platform));
        auto &shooter = *FindUnit(world, {1});
        const unsigned shots = platform == BattlePlatform::Catapult ? 12 : 14;
        const Tick reload = platform == BattlePlatform::Catapult ? 80 : 60;
        CHECK(shooter.ammunition_remaining == shots);
        for (unsigned i = 0; i < shots; ++i) {
            const auto event = LaunchFrom(world);
            REQUIRE(event);
            REQUIRE(event->projectile);
            CommitCombatEvents(world, std::span(&*event, 1));
            CHECK(shooter.ammunition_remaining == shots - i - 1);
            world.tick += reload - 1;
            CHECK_FALSE(LaunchFrom(world));
            ++world.tick;
        }
        CHECK_FALSE(LaunchFrom(world));
    }
}

TEST_CASE("combat.ancient_siege_projectiles_hit_their_target_after_flight") {
    for (const auto platform : {BattlePlatform::Catapult, BattlePlatform::Ballista}) {
        const bool catapult = platform == BattlePlatform::Catapult;
        CAPTURE(catapult);
        auto scenario = PlatformDuel(platform);
        scenario.rules.regiment_tactics = true;
        scenario.rules.local_melee = true;
        scenario.units[0].weapon = catapult ? WeaponSpec{8.5f, 40.f, 90, 12, 17.f, .5f}
                                            : WeaponSpec{6.5f, 30.f, 65, 10, 36.f, .8f};
        scenario.units[1].health = 4.f;
        scenario.units[1].armour = .25f;
        auto world = MakeWorld(scenario);
        const auto target_pose = FindUnit(world, {2})->pose;
        const FlatNavigationService navigation;

        REQUIRE(Step(world, {}, {}, navigation));
        const auto shot = std::ranges::find(world.events, EventKind::AttackLaunched, &BattleEvent::kind);
        REQUIRE(shot != world.events.end());
        CHECK(shot->source == UnitId{1});
        CHECK(shot->target == UnitId{2});
        CHECK(shot->projectile);
        CHECK(shot->high_arc == catapult);
        REQUIRE(shot->hit);
        const auto flight_id = shot->id;
        const auto arrival = shot->due_tick;
        REQUIRE(arrival > world.tick);
        REQUIRE(world.attacks.size() == 1);
        CHECK(world.attacks.front().id == flight_id);
        CHECK(world.attacks.front().due_tick == arrival);
        CHECK(FindUnit(world, {2})->health == 4.f);

        const nlohmann::json encoded = Capture(world);
        auto restored = Restore(encoded.get<Snapshot>());
        REQUIRE(restored);
        while (world.tick + 1 < arrival) {
            REQUIRE(Step(world, {}, {}, navigation));
            REQUIRE(Step(restored.value, {}, {}, navigation));
            CHECK(FindUnit(world, {2})->health == 4.f);
        }
        REQUIRE(Step(world, {}, {}, navigation));
        REQUIRE(Step(restored.value, {}, {}, navigation));
        CHECK(world.tick == arrival);
        CHECK(StateHash(world) == StateHash(restored.value));
        CHECK(world.attacks.empty());
        const auto hit = std::ranges::find(world.events, EventKind::Hit, &BattleEvent::kind);
        REQUIRE(hit != world.events.end());
        CHECK(hit->cause == flight_id);
        CHECK(hit->source == UnitId{1});
        CHECK(hit->target == UnitId{2});
        CHECK(hit->amount > 4.f);
        CHECK(hit->projectile);
        const auto death = std::ranges::find(world.events, EventKind::Died, &BattleEvent::kind);
        REQUIRE(death != world.events.end());
        CHECK(death->cause == flight_id);
        CHECK(death->target == UnitId{2});
        CHECK_FALSE(FindUnit(world, {2})->alive);
        CHECK(FindUnit(world, {2})->pose == target_pose);
    }
}

TEST_CASE("combat.ancient_artillery_does_not_fire_on_a_tick_that_moves_or_turns_it") {
    auto world = MakeWorld(PlatformDuel(BattlePlatform::Catapult));
    auto &unit = *FindUnit(world, {1});
    REQUIRE(AcquireControl(world, unit.id) == Status::Ok);
    unit.attack_target = {2};
    unit.desired_velocity = {1, 0, 0};
    const FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK(unit.pose.position.x > 0.f);
    CHECK(unit.ammunition_remaining == 12);
    CommandEnvelope turn;
    turn.sequence = 1;
    turn.tick = world.tick;
    turn.command.kind = CommandKind::FaceControlledUnit;
    turn.command.unit = unit.id;
    turn.command.pose.yaw = .1f;
    REQUIRE(Step(world, std::span(&turn, 1), {}, navigation));
    CHECK(unit.ammunition_remaining == 12);
    REQUIRE(Step(world, {}, {}, navigation));
    CHECK(unit.ammunition_remaining == 11);
}

TEST_CASE("combat.ancient_ballista_can_turn_but_cannot_translate") {
    auto world = MakeWorld(PlatformDuel(BattlePlatform::Ballista));
    auto &unit = *FindUnit(world, {1});
    REQUIRE(AcquireControl(world, unit.id) == Status::Ok);
    const auto origin = unit.pose.position;
    CHECK(SetExternalPose(world, unit.id, {origin + Vec3{1.f, 0.f, 0.f}, .1f}, 0) == Status::Conflict);
    CHECK(SetExternalPose(world, unit.id, {origin, .1f}, 0) == Status::Ok);
    Command movement;
    movement.kind = CommandKind::MoveControlledUnit;
    movement.unit = unit.id;
    movement.velocity = {2.f, 0, 0};
    REQUIRE(ApplyControlCommand(world, movement) == Status::Ok);
    AdvanceControlled(world, .05f);
    CHECK(unit.pose.position == origin);
    CHECK(unit.pose.yaw == .1f);
}

TEST_CASE("combat.ancient_ballista_attack_order_turns_the_battery_in_place") {
    auto scenario = PlatformDuel(BattlePlatform::Ballista);
    scenario.regiments[1].pose.position = {0, 16, 0};
    scenario.units[1].pose = scenario.regiments[1].pose;
    auto world = MakeWorld(scenario);
    Order attack;
    attack.type = OrderType::Attack;
    attack.recipient = {1};
    attack.target_regiment = {2};
    REQUIRE(SubmitOrder(world, attack) == Status::Ok);
    const FlatNavigationService navigation;
    for (int i = 0; i < 45; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    const auto &engine = *FindUnit(world, {1});
    CHECK(engine.pose.position == Vec3{});
    CHECK(engine.pose.yaw == doctest::Approx(Pi / 2.f));
    CHECK(engine.ammunition_remaining == 13);
}

namespace {
World PlatformCharge(BattlePlatform platform, float runup) {
    auto scenario = PlatformDuel(platform);
    scenario.rules.regiment_tactics = true;
    scenario.units[0].weapon = {2.f, 1.8f, 20, 1, 20.f, 1.f};
    // Four single-file slots put the front fighter three meters ahead of the anchor.
    scenario.regiments[1].pose.position.x = 4.2f;
    scenario.units[1].pose = scenario.regiments[1].pose;
    for (uint64_t i = 3; i < 6; ++i) {
        UnitSpec staff;
        staff.id = {i};
        staff.regiment = {1};
        staff.pose.position = {-10.f, 2.f * float(i), 0};
        scenario.units.push_back(staff);
    }
    auto world = MakeWorld(scenario);
    auto &regiment = *FindRegiment(world, {1});
    regiment.ai_action = OrderType::Attack;
    regiment.ai_target = {2};
    regiment.decision_tick = 10;
    regiment.cavalry_phase = CavalryPhase::Approach;
    regiment.cavalry_target = {2};
    regiment.charge_runup_m = runup;
    return world;
}

bool Impact(const World &world) {
    return std::ranges::any_of(world.events, [](const auto &event) {
        return event.source == UnitId{1} && event.kind == EventKind::CavalryImpact;
    });
}
} // namespace

TEST_CASE("combat.ancient_elephants_charge_with_infantry_staff_and_distinct_shock") {
    auto indian = PlatformCharge(BattlePlatform::IndianElephant, 3.f);
    auto african = PlatformCharge(BattlePlatform::AfricanElephant, 3.f);
    const FlatNavigationService navigation;
    REQUIRE(Step(indian, {}, {}, navigation));
    REQUIRE(Step(african, {}, {}, navigation));
    REQUIRE(Impact(indian));
    REQUIRE(Impact(african));
    CHECK(FindUnit(indian, {2})->health < FindUnit(african, {2})->health);
    CHECK(FindRegiment(african, {2})->morale < FindRegiment(indian, {2})->morale);
}

TEST_CASE("combat.ancient_scythed_chariot_needs_room_and_clear_ground_to_charge") {
    auto short_run = PlatformCharge(BattlePlatform::ScythedChariot, 3.f);
    auto clear = PlatformCharge(BattlePlatform::ScythedChariot, 6.f);
    auto rough = clear;
    rough.scenario.obstacles.push_back({2.8f, -1.f, 4.5f, 1.f, false, TerrainFeature::PloughedField});
    const FlatNavigationService navigation;
    REQUIRE(Step(short_run, {}, {}, navigation));
    REQUIRE(Step(clear, {}, {}, navigation));
    REQUIRE(Step(rough, {}, {}, navigation));
    CHECK_FALSE(Impact(short_run));
    CHECK(Impact(clear));
    CHECK_FALSE(Impact(rough));
}

TEST_CASE("combat.ancient_melee_can_hit_between_the_edges_of_large_bodies") {
    auto scenario = PlatformDuel(BattlePlatform::ScythedChariot);
    scenario.rules.local_melee = true;
    scenario.regiments[0].formation.body_radius_m = 2.45f;
    scenario.units[0].weapon.range = 2.3f;
    scenario.regiments[1].pose.position.x = 2.9f;
    scenario.units[1].pose = scenario.regiments[1].pose;
    const FlatNavigationService navigation;
    for (bool adaptive : {false, true}) {
        scenario.rules.adaptive_battle = adaptive;
        auto world = MakeWorld(scenario);
        REQUIRE(LaunchFrom(world, {1}));
        REQUIRE(LaunchFrom(world, {2}));
        for (int i = 0; i < 4; ++i)
            REQUIRE(Step(world, {}, {}, navigation));
        CHECK(FindUnit(world, {1})->health < scenario.units[0].health);
        CHECK(FindUnit(world, {2})->health < scenario.units[1].health);
    }
}

TEST_CASE("replay.ancient_platform_and_ammunition_survive_a_snapshot_round_trip") {
    auto world = MakeWorld(PlatformDuel(BattlePlatform::Ballista));
    const FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));
    const nlohmann::json encoded = Capture(world);
    const auto restored = Restore(encoded.get<Snapshot>());
    REQUIRE(restored);
    CHECK(FindUnit(restored.value, {1})->platform == BattlePlatform::Ballista);
    CHECK(FindUnit(restored.value, {1})->ammunition_remaining == 13);
    CHECK(StateHash(restored.value) == StateHash(world));
    FindUnit(world, {1})->platform = BattlePlatform::Catapult;
    CHECK(StateHash(restored.value) != StateHash(world));
}
