#include "TestSupport.h"
#include "ReplayIo.h"
#include "battle/Combat.h"
#include "battle/Formation.h"
#include "../src/LocalMelee.h"
#include "../src/RegimentDiscipline.h"
#include "doctest.h"

using namespace battle;

namespace {
World GunFixture(float distance, unsigned troops, unsigned columns = 0,
                 UnitTroop troop = UnitTroop::Infantry, float health = 10.f) {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    scenario.regiments[0].pose = {{0, 0, 0}, 0};
    scenario.regiments[1].pose = {{distance, 0, 0}, Pi};
    scenario.regiments[1].formation = {columns ? columns : troops, 1.4f, .3f};
    auto &gun = scenario.units[0];
    gun.platform = BattlePlatform::FieldGun;
    gun.archer = true;
    gun.weapon = {26.f, 210.f, 160, 1, 84.f, 1.f};
    gun.pose = scenario.regiments[0].pose;
    scenario.units[1].health = health;
    scenario.units[1].troop = troop;
    scenario.units[1].pose = scenario.regiments[1].pose;
    for (unsigned i = 1; i < troops; ++i) {
        UnitSpec unit = scenario.units[1];
        unit.id = {i + 2u};
        scenario.units.push_back(unit);
    }
    auto result = CreateWorld(scenario, 31);
    REQUIRE(result);
    return std::move(result.value);
}
BattleEvent LaunchGun(World &world) {
    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    REQUIRE(launches.size() == 1);
    REQUIRE(launches.front().source == UnitId{1});
    CommitCombatEvents(world, launches);
    return launches.front();
}

void ResolveGun(World &world, const BattleEvent &shot) {
    world.tick = shot.due_tick - 1;
    world.events.clear();
    CommitCombatEvents(world, {});
}

int HitCount(const World &world, EventId shot) {
    return int(std::ranges::count_if(world.events, [shot](const auto &event) {
        return event.kind == EventKind::Hit && event.cause == shot;
    }));
}
} // namespace

TEST_CASE("formation.artillery_keeps_all_guns_on_one_frontage") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].formation = {2, 3.8f, 1.7f};
    scenario.units[0].pose = scenario.regiments[0].pose;
    scenario.units[0].platform = BattlePlatform::FieldGun;
    scenario.units[0].archer = true;
    scenario.units[0].weapon.range = 210.f;
    for (uint64_t i = 3; i <= 6; ++i) {
        UnitSpec unit = scenario.units[0];
        unit.id = {i};
        unit.pose.position.x -= 4.f;
        if (i == 6) {
            unit.platform = BattlePlatform::None;
            unit.archer = false;
        }
        scenario.units.push_back(unit);
    }
    auto created = CreateWorld(scenario, 31);
    REQUIRE(created);
    auto &world = created.value;
    const auto *battery = FindRegiment(world, {1});
    REQUIRE(battery);
    CHECK(battery->formation.columns == 5);
    for (auto id : battery->units)
        CHECK(FindUnit(world, id)->pose.position.x == doctest::Approx(battery->pose.position.x));
    REQUIRE(ReformRegiment(world, battery->id, {1, 3.8f, 1.7f}) == Status::Ok);
    CHECK(battery->formation.columns == 5);
    for (const auto &slot : battery->slots)
        CHECK(slot.position.x == doctest::Approx(battery->pose.position.x));
}

TEST_CASE("combat.field_gun_can_hit_an_exposed_ranged_target_on_a_large_battlefield") {
    auto world = GunFixture(160.f, 1);
    world.scenario.rules.adaptive_battle = true;
    const auto *gun = FindUnit(world, {1});
    REQUIRE(gun);
    CHECK(gun->weapon.range == 210.f);
    const FlatNavigationService navigation;
    EventId shot{};
    unsigned hits = 0, casualties = 0;
    for (int i = 0; i < 60 && world.outcome == BattleOutcome::Running; ++i) {
        REQUIRE(Step(world, {}, {}, navigation));
        for (const auto &event : world.events) {
            if (event.source != gun->id)
                continue;
            if (event.kind == EventKind::AttackLaunched) {
                shot = event.id;
                CHECK(event.reason == "field gun round shot");
            }
            hits += event.kind == EventKind::Hit && event.cause == shot;
            casualties += event.kind == EventKind::Died && event.cause == shot;
        }
    }
    CHECK(shot);
    CHECK(hits == 1);
    CHECK(casualties == 1);
}

TEST_CASE("combat.field_gun_canister_uses_one_shot_for_a_small_spread_of_enemies") {
    auto world = GunFixture(18.f, 7);
    const FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));

    const auto shot = std::ranges::find_if(world.events, [](const auto &event) {
        return event.kind == EventKind::AttackLaunched && event.source == UnitId{1};
    });
    REQUIRE(shot != world.events.end());
    CHECK(shot->reason == "field gun canister");
    const auto hits = std::ranges::count_if(world.events, [&](const auto &event) {
        return event.kind == EventKind::Hit && event.cause == shot->id &&
               event.reason == "field gun canister";
    });
    const auto casualties = std::ranges::count_if(world.events, [&](const auto &event) {
        return event.kind == EventKind::Died && event.cause == shot->id;
    });
    CHECK(hits == 7);
    CHECK(casualties == 7);
    CHECK(FindUnit(world, {1})->ammunition_remaining == 9);
    CHECK(FindUnit(world, {1})->ready_tick == 160);
}

TEST_CASE("combat.field_gun_cannot_fire_through_a_wall") {
    auto world = GunFixture(18.f, 5);
    world.scenario.obstacles.push_back({5, -6, 6, 6, true});
    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    CHECK(launches.empty());
    CHECK(FindUnit(world, {1})->ammunition_remaining == 10);
}

TEST_CASE("combat.field_gun_canister_cannot_hit_secondary_targets_behind_cover") {
    auto world = GunFixture(18.f, 7);
    world.scenario.obstacles.push_back({8, .25f, 9, 5, true});
    const FlatNavigationService navigation;
    REQUIRE(Step(world, {}, {}, navigation));

    const auto shot = std::ranges::find_if(world.events, [](const auto &event) {
        return event.kind == EventKind::AttackLaunched && event.source == UnitId{1};
    });
    REQUIRE(shot != world.events.end());
    CHECK(shot->reason == FieldGunCanisterReason);
    const auto hits = std::ranges::count_if(world.events, [&](const auto &event) {
        return event.kind == EventKind::Hit && event.cause == shot->id;
    });
    CHECK(hits == 4);
    for (const auto &unit : world.units)
        if (unit.regiment == RegimentId{2} && unit.pose.position.y > 0.f)
            CHECK(unit.alive);
}

TEST_CASE("combat.field_gun_round_shot_penetrates_a_column_but_not_a_line_or_loose_screen") {
    auto line = GunFixture(100.f, 9, 9);
    auto column = GunFixture(100.f, 9, 1);
    auto screen = GunFixture(100.f, 9, 1);
    auto cavalry = GunFixture(100.f, 9, 1, UnitTroop::Mounted);
    for (auto &unit : screen.units)
        if (unit.regiment == RegimentId{2})
            unit.skirmisher = true;

    for (auto *world : {&line, &column, &screen, &cavalry}) {
        const auto shot = LaunchGun(*world);
        CHECK(shot.hit);
        ResolveGun(*world, shot);
        CHECK(FindUnit(*world, {1})->ammunition_remaining == 9);
        CHECK(FindUnit(*world, {1})->ready_tick == 160);
    }
    const auto survivors = [](const World &world) { return world.regiments[1].units.size(); };
    CHECK(survivors(line) == 8);
    CHECK(survivors(column) == 5);
    CHECK(survivors(screen) == 8);
    CHECK(survivors(cavalry) == 6);
}

TEST_CASE("combat.field_gun_round_shot_keeps_its_launch_lane_and_stops_at_cover") {
    auto world = GunFixture(100.f, 7, 1);
    const auto shot = LaunchGun(world);
    REQUIRE(shot.hit);
    auto *aim = FindUnit(world, shot.target);
    const float impact_x = aim->pose.position.x;
    aim->pose.position.y += 4.f;
    // The first figure on the original lane is now a rank behind the aiming point.
    world.scenario.obstacles.push_back({impact_x + 2.f, -1.f, impact_x + 2.2f, 1.f, true});
    ResolveGun(world, shot);

    CHECK(aim->alive);
    CHECK(HitCount(world, shot.id) == 1);
    for (const auto &event : world.events)
        if (event.kind == EventKind::Hit && event.cause == shot.id)
            CHECK(FindUnit(world, event.target)->pose.position.x < impact_x + 2.f);
}

TEST_CASE("combat.field_gun_casualties_leave_holes_in_the_hit_file_until_reform") {
    auto world = GunFixture(100.f, 9, 3);
    const auto before = world.units;
    const auto original_slots = world.regiments[1].slots;
    const auto original_footprint = Footprint(world.regiments[1]);
    const auto shot = LaunchGun(world);
    REQUIRE(shot.hit);
    ResolveGun(world, shot);
    ++world.tick;

    CHECK(HitCount(world, shot.id) == 3);
    auto &regiment = world.regiments[1];
    CHECK(regiment.units.size() == 6);
    for (const auto &old : before) {
        if (old.regiment != regiment.id)
            continue;
        const auto &unit = *FindUnit(world, old.id);
        const bool hit_file = std::abs(old.pose.position.y) < .01f;
        CHECK(unit.alive == !hit_file);
        CHECK(unit.pose == old.pose);
        if (hit_file) {
            CHECK(std::ranges::any_of(world.events, [&](const auto &event) {
                return event.kind == EventKind::Died && event.target == old.id && event.cause == shot.id;
            }));
        } else {
            CHECK(unit.slot_index == old.slot_index);
            CHECK(unit.slot == old.slot);
        }
    }
    CHECK(regiment.slots == original_slots);
    CHECK(Footprint(regiment).width_m == original_footprint.width_m);
    CHECK(Footprint(regiment).depth_m == original_footprint.depth_m);
    CHECK_FALSE(IsLinearFiringLine(regiment));

    const LocalMelee contact(world, .05f);
    AdvanceFormations(world, .05f, &contact);
    CHECK(regiment.slots == original_slots);
    auto restored = Restore(Capture(world));
    REQUIRE(restored);
    CHECK(StateHash(restored.value) == StateHash(world));

    REQUIRE(ReformRegiment(world, regiment.id, regiment.formation) == Status::Ok);
    CHECK(regiment.slots.size() == regiment.units.size());
    CHECK(IsLinearFiringLine(regiment));
    for (const auto id : regiment.units)
        CHECK(FindUnit(world, id)->pose == before[id.value - 1].pose);
}

TEST_CASE("combat.field_gun_canister_weakens_at_the_edges_and_caps_the_number_hit") {
    auto world = GunFixture(24.f, 17, 17, UnitTroop::Infantry, 20.f);
    // Match the spacing of a battalion fielded in a large mission.
    for (auto &unit : world.units)
        if (unit.regiment == RegimentId{2})
            unit.pose.position.y *= .7f;
    const auto original_slots = world.regiments[1].slots;
    const auto shot = LaunchGun(world);

    CHECK(HitCount(world, shot.id) == 10);
    CHECK_FALSE(FindUnit(world, shot.target)->alive);
    CHECK(world.regiments[1].slots == original_slots);
    for (const auto id : world.regiments[1].units)
        CHECK(world.regiments[1].slots[FindUnit(world, id)->slot_index] ==
              original_slots[id.value - 2]);
    CHECK(std::ranges::any_of(world.units, [](const UnitState &unit) {
        return unit.regiment == RegimentId{2} && unit.alive && unit.health < 20.f;
    }));
}

TEST_CASE("combat.field_gun_fire_disrupts_ranks_by_shell_and_wears_off") {
    auto single = GunFixture(100.f, 100, 10);
    auto battery = single;
    const auto feed_hits = [](World &world, bool separate_shots) {
        for (uint64_t i = 0; i < 5; ++i) {
            BattleEvent event;
            event.id = {10 + i};
            event.kind = EventKind::Hit;
            event.source = {1};
            event.target = {2 + i};
            event.regiment = {2};
            event.cause = {separate_shots ? 100 + i : 100};
            event.projectile = true;
            event.amount = 2.f;
            world.events.push_back(event);
        }
        world.regiments[1].last_damage_tick = world.tick + 1;
        UpdateRegimentDiscipline(world);
    };
    const auto refill_distance = [](World &world) {
        auto &troop = *FindUnit(world, world.regiments[1].units.front());
        troop.pose.position.x = troop.slot.position.x - 1.f;
        const auto before = troop.pose.position;
        AdvanceFormations(world, .05f);
        return Distance(before, troop.pose.position);
    };
    auto quiet = single;
    const auto normal_refill = refill_distance(quiet);
    feed_hits(single, false);
    feed_hits(battery, true);

    CHECK(single.regiments[1].cohesion < 1.f);
    CHECK(battery.regiments[1].cohesion + .1f < single.regiments[1].cohesion);
    CHECK(refill_distance(battery) < refill_distance(single));
    CHECK_FALSE(battery.regiments[1].panic);

    battery.events.clear();
    for (int i = 0; i < 120; ++i) {
        ++battery.tick;
        UpdateRegimentDiscipline(battery);
    }
    CHECK(battery.regiments[1].cohesion == doctest::Approx(1.f));
    CHECK(refill_distance(battery) == doctest::Approx(normal_refill));
}

TEST_CASE("combat.field_gun_flight_and_artillery_pressure_survive_a_saved_battle") {
    auto world = GunFixture(100.f, 8, 1);
    const auto shot = LaunchGun(world);
    ++world.tick;
    REQUIRE(world.attacks.size() == 1);
    REQUIRE(world.attacks.front().round_shot);
    world.regiments[1].artillery_pressure = .4f;
    test::TempDir directory;
    const auto path = directory.path / "artillery.json";
    REQUIRE(io::SaveSnapshot(path, Capture(world)));
    auto loaded = io::LoadSnapshot(path);
    REQUIRE(loaded);
    auto &restored = loaded.value.state;
    CHECK(StateHash(world) == StateHash(restored));
    REQUIRE(restored.attacks.front().round_shot);
    CHECK(restored.regiments[1].artillery_pressure == doctest::Approx(.4f));
    for (auto *battle : {&world, &restored}) {
        FindUnit(*battle, shot.target)->pose.position.y += 4.f;
        ResolveGun(*battle, shot);
        CHECK(FindUnit(*battle, shot.target)->alive);
        CHECK(HitCount(*battle, shot.id) == 4);
    }
    CHECK(StateHash(world) == StateHash(restored));
}

TEST_CASE("combat.field_gun_canister_stops_at_a_stonewall_adjacent_to_the_targets") {
    auto world = GunFixture(18.f, 7);
    world.scenario.obstacles.push_back({16.8f, -9.f, 17.f, 9.f, true, TerrainFeature::StoneWall});
    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    CHECK(launches.empty());
}

TEST_CASE("combat.field_gun_round_shot_uses_each_intersected_figures_own_armour") {
    auto world = GunFixture(100.f, 7, 1, UnitTroop::Infantry, 40.f);
    auto *armoured = FindUnit(world, {2});
    armoured->armour = 100.f;
    const auto shot = LaunchGun(world);
    REQUIRE(shot.target == armoured->id);
    CHECK(shot.amount == 0.f);
    armoured->pose.position.y += 4.f;
    ResolveGun(world, shot);

    CHECK(HitCount(world, shot.id) == 1);
    const auto *first = FindUnit(world, {3});
    CHECK(first->alive);
    CHECK(first->health == doctest::Approx(14.f));
}

TEST_CASE("combat.field_gun_aimed_at_a_back_rank_still_hits_the_front_rank_first") {
    auto world = GunFixture(100.f, 7, 1);
    auto *gun = FindUnit(world, {1});
    gun->control = ControlMode::PlayerControlled;
    gun->attack_target = {6};
    world.controlled = gun->id;
    const auto shot = LaunchGun(world);
    CHECK(shot.target == UnitId{6});
    ResolveGun(world, shot);

    CHECK(HitCount(world, shot.id) == 4);
    CHECK_FALSE(FindUnit(world, {2})->alive);
    CHECK(FindUnit(world, {6})->alive);
}
