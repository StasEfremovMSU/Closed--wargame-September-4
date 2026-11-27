#include "TestSupport.h"
#include "ReplayIo.h"
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Snapshot.h"
#include "doctest.h"

using namespace battle;

TEST_CASE("control.enemy_telegraphs_parryable_strike_against_the_player") {
    auto scenario = test::Duel();
    scenario.units[0].pose = scenario.regiments[0].pose = {};
    scenario.units[1].pose = scenario.regiments[1].pose = {{1.1f, 0, 0}, Pi};
    auto result = CreateWorld(scenario, 42);
    REQUIRE(result);
    auto &world = result.value;
    std::vector<BattleEvent> attacks;
    BuildCombatEvents(world, attacks);
    const auto find_strike = [&] {
        return std::ranges::find(attacks, UnitId{2}, &BattleEvent::source);
    };
    REQUIRE(find_strike() != attacks.end());
    CHECK(find_strike()->due_tick - world.tick == world.units[1].weapon.windup_ticks);

    REQUIRE(AcquireControl(world, {1}) == Status::Ok);
    BuildCombatEvents(world, attacks);
    REQUIRE(find_strike() != attacks.end());
    CHECK(find_strike()->due_tick - world.tick >= 9);
}

TEST_CASE("control.arrow_flight_resolves_against_the_original_impact_point") {
    auto scenario = test::Duel();
    scenario.rules.controlled_arrow_aim = true;
    scenario.units[0].health = 20.f;
    scenario.units[1].archer = true;
    scenario.units[1].ranged_weapon = RangedWeapon::Bow;
    Command evade;
    evade.kind = CommandKind::MoveControlledUnit;
    evade.unit = {1};
    bool clears_impact = true;
    SUBCASE("moving sideways clears the fixed impact point") { evade.velocity = {0.f, 4.f, 0.f}; }
    SUBCASE("running toward the launch point clears the fixed impact point") { evade.velocity = {4.f, 0.f, 0.f}; }
    SUBCASE("running away clears the fixed impact point") { evade.velocity = {-4.f, 0.f, 0.f}; }
    SUBCASE("a small movement still leaves the body in the impact volume") {
        evade.velocity = {.5f, 0.f, 0.f};
        clears_impact = false;
    }
    SUBCASE("a normal jump leaves the feet below a waist-high arrow") {
        evade.kind = CommandKind::JumpControlledUnit;
        clears_impact = false;
    }
    SUBCASE("an external pose with feet above the arrow clears its impact point") {
        evade.kind = CommandKind::ExternalPoseSample;
        evade.pose = scenario.units[0].pose;
        evade.pose.position.z += 1.2f;
    }
    SUBCASE("an external pose with the entire body below the arrow also clears its impact point") {
        evade.kind = CommandKind::ExternalPoseSample;
        evade.pose = scenario.units[0].pose;
        evade.pose.position.z -= 1.2f;
    }
    SUBCASE("a legacy scenario keeps resolving against its nominated target") {
        scenario.rules.controlled_arrow_aim = false;
        evade.velocity = {0.f, 4.f, 0.f};
        clears_impact = false;
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    REQUIRE(AcquireControl(world, {1}) == Status::Ok);
    const float initial_health = world.units[0].health;
    BattleEvent launch{EventId{world.next_event_id}, world.tick, EventKind::AttackLaunched,
                       {2}, {1}, {2}, 3.f, "ranged attack"};
    launch.projectile = launch.hit = true;
    launch.due_tick = 8;
    CommitCombatEvents(world, std::span{&launch, 1});
    REQUIRE(world.attacks.size() == 1);
    CHECK(world.attacks.front().officer_arrow.has_value() == scenario.rules.controlled_arrow_aim);

    const test::TempDir temp;
    const auto path = temp.path / "arrow-in-flight.json";
    REQUIRE(io::SaveSnapshot(path, Capture(world)));
    auto restored = io::LoadSnapshot(path);
    REQUIRE(restored);
    CHECK(restored.value.state.scenario.rules.controlled_arrow_aim == scenario.rules.controlled_arrow_aim);
    CHECK(StateHash(world) == StateHash(restored.value.state));

    FlatNavigationService nav;
    const CommandEnvelope command{world.tick, 1, evade};
    REQUIRE(Step(restored.value.state, std::span{&command, 1}, {}, nav));
    while (world.tick < launch.due_tick)
        REQUIRE(Step(world, {}, {}, nav));
    while (restored.value.state.tick < launch.due_tick)
        REQUIRE(Step(restored.value.state, {}, {}, nav));
    CHECK(world.units[0].health == initial_health - 3.f);
    CHECK(restored.value.state.units[0].health == initial_health - (clears_impact ? 0.f : 3.f));
    if (evade.kind == CommandKind::JumpControlledUnit)
        CHECK(restored.value.state.units[0].pose.position.z > .65f);
    CHECK(std::ranges::any_of(restored.value.state.events, [&](const auto &event) {
        return event.kind == EventKind::Miss && event.projectile && event.cause == launch.id &&
               event.target == UnitId{1} && event.reason == OfficerArrowDodgeReason;
    }) == clears_impact);
}
