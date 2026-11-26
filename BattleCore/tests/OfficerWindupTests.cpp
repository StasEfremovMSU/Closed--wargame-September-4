#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Control.h"
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
