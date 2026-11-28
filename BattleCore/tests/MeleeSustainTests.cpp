#include "JsonBindings.h"
#include "TestSupport.h"
#include "battle/Formation.h"
#include "doctest.h"

using namespace battle;

namespace {
World CloseLines() {
    auto scenario = test::Duel(5);
    scenario.rules.local_melee = scenario.rules.regiment_tactics = true;
    scenario.rules.sustained_melee = true;
    for (auto &regiment : scenario.regiments) {
        regiment.pose.position.x = regiment.team ? .6f : -.6f;
        regiment.formation = {5, 1.1f, .25f};
    }
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.weapon.damage = 0.f;
    }
    auto created = CreateWorld(scenario, 7);
    REQUIRE(created);
    return std::move(created.value);
}

void PullOpponentAway(World &world) {
    auto &enemy = world.regiments[1];
    enemy.pose.position.x += 12.f;
    for (auto &slot : enemy.slots)
        slot.position.x += 12.f;
    for (const auto id : enemy.units) {
        auto &unit = *FindUnit(world, id);
        unit.pose.position.x += 12.f;
        unit.slot.position.x += 12.f;
    }
}
}

TEST_CASE("combat.sustained_head_on_melee_opens_the_outer_files") {
    auto world = CloseLines();
    const auto &regiment = world.regiments[0];
    const auto &outer = *FindUnit(world, {5});
    const auto opening = outer.pose.position;
    FlatNavigationService nav;
    Order hold;
    hold.type = OrderType::HoldLine;
    hold.recipient = world.regiments[1].id;
    REQUIRE(SubmitOrder(world, hold) == Status::Ok);
    for (unsigned i = 0; i < 20; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(regiment.melee_engaged);
    CHECK(outer.pose.position.y < opening.y + .25f);

    float widest = opening.y;
    bool turned_the_shoulder = false;
    for (unsigned i = 0; i < 140; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        widest = std::max(widest, outer.pose.position.y);
        turned_the_shoulder |= outer.pose.position.y > opening.y + .45f && outer.pose.position.x > .10f;
    }
    CAPTURE(widest);
    CHECK(turned_the_shoulder);
}

TEST_CASE("combat.genuine_melee_disengagement_pauses_march_and_leaves_fatigue") {
    auto world = CloseLines();
    auto &regiment = world.regiments[0];
    const auto speed = FormationTravelSpeed(world, regiment);
    FlatNavigationService nav;
    for (unsigned i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(regiment.melee_engaged);
    const auto rest_point = regiment.pose;
    PullOpponentAway(world);
    REQUIRE(Step(world, {}, {}, nav));
    REQUIRE_FALSE(regiment.melee_engaged);
    CHECK(IsRecoveringFromMelee(world, regiment));
    CHECK(regiment.fatigue > .1f);
    CHECK(FormationTravelSpeed(world, regiment) < speed * .96f);
    const auto saved = nlohmann::json::parse(nlohmann::json(Capture(world)).dump()).get<Snapshot>();
    const auto restored = Restore(saved);
    REQUIRE(restored);
    CHECK(IsRecoveringFromMelee(restored.value, restored.value.regiments[0]));
    CHECK(StateHash(restored.value) == StateHash(world));

    Order move;
    move.type = OrderType::MoveTo;
    move.recipient = regiment.id;
    move.point = {-8.f, 0.f, 0.f};
    REQUIRE(SubmitOrder(world, move) == Status::Ok);
    for (unsigned i = 0; i < 16; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.pose == rest_point);
    for (unsigned i = 0; i < 120; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK_FALSE(IsRecoveringFromMelee(world, regiment));
    CHECK(regiment.pose.position.x < rest_point.position.x - 1.f);
}

TEST_CASE("combat.brief_formation_contact_does_not_exhaust_the_regiment") {
    auto world = CloseLines();
    auto &regiment = world.regiments[0];
    FlatNavigationService nav;
    for (unsigned i = 0; i < 4; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(regiment.melee_engaged);
    PullOpponentAway(world);
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.fatigue == 0.f);
    CHECK_FALSE(IsRecoveringFromMelee(world, regiment));
}
