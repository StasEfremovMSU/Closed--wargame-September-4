#include "../src/LocalMelee.h"
#include "JsonBindings.h"
#include "ReplayIo.h"
#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Formation.h"
#include "doctest.h"
#include <set>
using namespace battle;

TEST_CASE("formation.special_layouts_use_real_slots_and_fit_their_footprint") {
    constexpr uint32_t count = 31;
    const Pose centre{{10.f, -4.f, 0.f}, .73f};
    const auto forward = Forward(centre.yaw);
    const auto right = Right(centre.yaw);
    for (auto shape : {FormationShape::Wedge, FormationShape::Square, FormationShape::Testudo,
                       FormationShape::Circle}) {
        const FormationSpec spec{8, 2.f, .3f, shape};
        const auto slots = BuildFormationSlots(spec, centre, count);
        const auto size = Footprint(spec, count);
        REQUIRE(slots.size() == count);
        std::set<std::pair<float, float>> positions;
        for (const auto &slot : slots) {
            positions.emplace(slot.position.x, slot.position.y);
            const Vec3 offset = slot.position - centre.position;
            CHECK(std::abs(Dot(offset, forward)) + spec.body_radius_m <= size.depth_m * .5f + .001f);
            CHECK(std::abs(Dot(offset, right)) + spec.body_radius_m <= size.width_m * .5f + .001f);
            if (shape == FormationShape::Circle || shape == FormationShape::Square) {
                CHECK(Dot(Forward(slot.yaw), offset) > 0.f);
                CHECK(Length(offset) >= spec.spacing_m);
            }
        }
        CHECK(positions.size() == count);
        if (shape == FormationShape::Wedge) {
            CHECK(std::abs(Dot(slots.front().position - centre.position, right)) < .001f);
            CHECK(Dot(slots.front().position - slots[1].position, forward) > 0.f);
            CHECK(Dot(slots[2].position - slots[1].position, right) > 0.f);
        } else if (shape == FormationShape::Testudo) {
            const auto ordinary = Footprint({spec.columns, spec.spacing_m, spec.body_radius_m}, count);
            CHECK(size.width_m < ordinary.width_m);
            CHECK(size.depth_m < ordinary.depth_m);
        }
    }
}

TEST_CASE("formation.reform_command_moves_survivors_into_shape_and_back") {
    auto scenario = test::Duel(20);
    scenario.regiments[0].formation.columns = 5;
    scenario.regiments[1].pose.position.x = 100.f;
    for (auto &unit : scenario.units)
        if (unit.regiment == RegimentId{2})
            unit.pose = scenario.regiments[1].pose;
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments[0];
    const auto rectangle = regiment.formation;
    auto square = rectangle;
    square.shape = FormationShape::Square;
    const float ordinary_speed = FormationTravelSpeed(world, regiment);
    CommandEnvelope envelope;
    envelope.sequence = 1;
    envelope.command.kind = CommandKind::ReformRegiment;
    envelope.command.regiment = regiment.id;
    envelope.command.formation = square;
    FlatNavigationService navigation;
    REQUIRE(Step(world, std::span{&envelope, 1u}, {}, navigation));
    CHECK(regiment.formation.shape == FormationShape::Square);
    CHECK(FormationTravelSpeed(world, regiment) < ordinary_speed);
    CHECK(regiment.slots == BuildFormationSlots(square, regiment.pose, 20));

    auto &fallen = *FindUnit(world, regiment.units.front());
    fallen.alive = false;
    fallen.health = 0.f;
    fallen.control = ControlMode::Dead;
    REQUIRE(ReformRegiment(world, regiment.id, square) == Status::Ok);
    REQUIRE(regiment.slots.size() == 19);
    std::set<uint32_t> claimed;
    for (const auto id : regiment.units) {
        const auto &unit = *FindUnit(world, id);
        REQUIRE(unit.slot_index < regiment.slots.size());
        CHECK(unit.slot == regiment.slots[unit.slot_index]);
        CHECK(claimed.insert(unit.slot_index).second);
    }
    REQUIRE(ReformRegiment(world, regiment.id, rectangle) == Status::Ok);
    CHECK(regiment.slots == BuildFormationSlots(rectangle, regiment.pose, 19));
    auto invalid = rectangle;
    invalid.shape = static_cast<FormationShape>(255);
    CHECK(ReformRegiment(world, regiment.id, invalid) == Status::InvalidInput);
    invalid.shape = FormationShape::Wedge;
    CHECK(ReformRegiment(world, regiment.id, invalid) != Status::Ok);
}

TEST_CASE("replay.formation_shapes_roundtrip_with_legacy_rectangle_defaults") {
    auto scenario = test::Duel(12);
    auto shape = scenario.regiments[0].formation;
    shape.shape = FormationShape::Circle;
    CommandEnvelope envelope;
    envelope.sequence = 1;
    envelope.command.kind = CommandKind::ReformRegiment;
    envelope.command.regiment = {1};
    envelope.command.formation = shape;
    const auto legacy = nlohmann::json{{"columns", 5}, {"spacing_m", 2.f}, {"body_radius_m", .3f}};
    CHECK(legacy.get<FormationSpec>().shape == FormationShape::Rectangle);
    auto malformed = legacy;
    malformed["shape"] = 256;
    CHECK_THROWS(malformed.get<FormationSpec>());
    test::TempDir temp;
    const auto replay = temp.path / "formations-replay.json";
    REQUIRE(io::WriteReplay(replay, scenario, 18, std::span{&envelope, 1u}));
    const auto loaded = io::LoadReplay(replay);
    REQUIRE(loaded);
    CHECK(loaded.value.commands[0].command.formation == shape);
    auto original = CreateWorld(scenario, 18);
    REQUIRE(original);
    FlatNavigationService navigation;
    REQUIRE(Step(original.value, loaded.value.commands, {}, navigation));
    CHECK(original.value.regiments[0].formation == shape);
    const auto snapshot = temp.path / "formations-snapshot.json";
    REQUIRE(io::SaveSnapshot(snapshot, Capture(original.value)));
    const auto restored = io::LoadSnapshot(snapshot);
    REQUIRE(restored);
    CHECK(restored.value.state.regiments[0].formation == shape);
    CHECK(StateHash(restored.value.state) == StateHash(original.value));
}

TEST_CASE("formation.testudo_shields_only_formed_troops_from_front_and_side_missiles") {
    auto scenario = test::Duel(12);
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].formation = {4, 1.5f, .3f, FormationShape::Testudo};
    auto &shooter = scenario.units[12];
    shooter.archer = true;
    shooter.ranged_weapon = RangedWeapon::Bow;
    float shot_bearing = 0.f;
    float expected_damage = .1f;
    bool away_from_slot = false;
    SUBCASE("shields cover the front") {}
    SUBCASE("shields cover the sides") { shot_bearing = Pi / 2; }
    SUBCASE("the rear is exposed") { shot_bearing = Pi; expected_damage = .25f; }
    SUBCASE("soldiers still forming have no cover") { away_from_slot = true; expected_damage = .25f; }
    SUBCASE("musket rounds are not stopped by shields") {
        shooter.ranged_weapon = RangedWeapon::Musket;
        expected_damage = .25f;
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &defender = world.units.front();
    if (away_from_slot)
        defender.pose.position.y += 2.f;
    world.units[12].pose.position = defender.pose.position + Forward(shot_bearing) * 4.f;
    world.attacks.push_back({{1}, world.units[12].id, defender.id, 1, .25f, true, 12.f, true});
    CommitCombatEvents(world, {});
    CHECK(defender.health == doctest::Approx(1.f - expected_damage));
}

TEST_CASE("formation.closed_formations_defend_their_outward_facing_from_any_side") {
    auto scenario = test::Duel(16);
    scenario.rules.regiment_tactics = true;
    auto &attacker = scenario.units[16];
    attacker.weapon.damage = .4f;
    attacker.troop = UnitTroop::Mounted;
    auto shape = FormationShape::Square;
    SUBCASE("infantry square braces against charges") {}
    SUBCASE("circle resists infantry attacks") { shape = FormationShape::Circle; }
    scenario.regiments[0].formation = {4, 1.5f, .3f, shape};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    auto &enemy = world.units[16];
    for (unsigned tick = 0; tick < 15; ++tick) {
        FlatNavigationService navigation;
        REQUIRE(Step(world, {}, {}, navigation));
    }
    CHECK(regiment.braced == (shape == FormationShape::Square));
    for (unsigned side = 0; side < 4; ++side) {
        const auto bearing = Forward(Pi * .5f * float(side));
        auto *defender = &*std::max_element(world.units.begin(), world.units.begin() + 16,
            [&](const auto &a, const auto &b) {
                return Dot(Forward(a.slot.yaw), bearing) < Dot(Forward(b.slot.yaw), bearing);
            });
        const auto outgoing = Forward(defender->slot.yaw);
        enemy.pose.position = defender->pose.position + outgoing * 1.f;
        CHECK(ClassifyAttackAspect(world, enemy, *defender) == AttackAspect::Front);
        const float health = defender->health;
        world.attacks.push_back({{world.next_event_id++}, enemy.id, defender->id, world.tick + 1,
                                 .4f, true, 1.5f, false, shape == FormationShape::Square});
        CommitCombatEvents(world, {});
        const float expected = shape == FormationShape::Square ? .4f * .65f : .4f * .87f;
        CHECK(defender->health == doctest::Approx(health - expected));
        enemy.pose.position = defender->pose.position - outgoing * 1.f;
        CHECK(ClassifyAttackAspect(world, enemy, *defender) == AttackAspect::Rear);
    }
}

TEST_CASE("formation.testudo_holds_javelins_but_can_fight_in_melee") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].formation.shape = FormationShape::Testudo;
    scenario.units[0].ranged_weapon = RangedWeapon::Javelin;
    scenario.units[0].ranged_attack = WeaponSpec{.5f, 12.f};
    scenario.units[0].ammunition = 3;
    auto created = CreateWorld(scenario, 5);
    REQUIRE(created);
    auto &world = created.value;
    world.units[1].pose.position = world.units[0].pose.position + Vec3{1.f, 0.f, 0.f};
    std::vector<BattleEvent> attacks;
    BuildCombatEvents(world, attacks);
    const auto launched = std::ranges::find(attacks, world.units[0].id, &BattleEvent::source);
    REQUIRE(launched != attacks.end());
    CHECK_FALSE(launched->projectile);
    CHECK(world.units[0].ammunition_remaining == 3);
}

TEST_CASE("formation.dense_testudo_still_checks_bodies_during_a_march") {
    auto scenario = test::Duel(2);
    scenario.rules.adaptive_battle = true;
    scenario.regiments[0].formation = {2, 1.f, .29f, FormationShape::Testudo};
    auto created = CreateWorld(scenario, 5);
    REQUIRE(created);
    const auto &world = created.value;
    const auto &leader = world.units[0];
    const auto &neighbor = world.units[1];
    const auto toward_neighbor = neighbor.pose.position - leader.pose.position;
    const auto step = leader.pose.position + toward_neighbor * (.1f / Length(toward_neighbor));
    const LocalMelee contact(world, .05f);
    const auto safe = contact.Constrain(leader, step);
    CHECK(safe != step);
    CHECK(Distance(safe, neighbor.pose.position) >= contact.BodyRadius(leader) + contact.BodyRadius(neighbor));
}
