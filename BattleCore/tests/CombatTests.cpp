#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Formation.h"
#include "battle/MarchMission.h"
#include "battle/Officers.h"
#include "doctest.h"
#include <array>
#include <map>
using namespace battle;
TEST_CASE("combat.fire_sector_agrees_with_a_shooters_friendly_lane_and_melee_masks") {
    auto scenario = test::Duel(2);
    scenario.rules.local_melee = scenario.rules.regiment_tactics = true;
    auto world = CreateWorld(scenario, 2).value;
    auto &shooter = world.units[0];
    shooter.pose = {{0, 0, 0}, 0};
    shooter.archer = true;
    shooter.weapon.range = 20;
    world.regiments[0].pose = shooter.pose;
    world.regiments[1].pose.position = {5, 0, 0};
    auto &friend_unit = world.units[1];
    friend_unit.pose.position = {2, 0, 0};
    auto &target = world.units[2];
    target.pose.position = {5, 0, 0};
    world.units[3].alive = false;
    const auto at_target = [&] {
        const auto sector = SampleFireSector(world, world.regiments[0], 0, 20);
        REQUIRE(sector.size() == 20);
        return sector[4].mask;
    };
    const auto launches = [&] {
        std::vector<BattleEvent> events;
        BuildCombatEvents(world, events);
        return std::ranges::any_of(events, [&](const auto &event) {
            return event.source == shooter.id && event.projectile;
        });
    };
    CHECK(at_target() == FireLaneMask::Friendly);
    CHECK_FALSE(launches());
    friend_unit.pose.position.y = 5;
    CHECK(at_target() == FireLaneMask::Clear);
    CHECK(launches());
    // A detached friendly regiment fights at the target, away from the shooting regiment.
    world.regiments.push_back(world.regiments[0]);
    world.regiments.back().id = {3};
    world.regiments.back().units = {friend_unit.id};
    world.regiments[0].units = {shooter.id};
    friend_unit.regiment = {3};
    friend_unit.pose.position = {5, .45f, 0};
    CHECK(at_target() == FireLaneMask::Melee);
    CHECK_FALSE(launches());
    world.scenario.rules.risky_volley = true;
    CHECK(at_target() == FireLaneMask::Risky);
    CHECK(launches());
}
namespace {
World CloseDuel() {
    auto s = test::Duel();
    s.regiments[0].pose.position.x = -.3f;
    s.regiments[1].pose.position.x = .3f;
    for (auto &u : s.units)
        u.pose = s.regiments[u.regiment.value - 1].pose;
    return CreateWorld(s, 42).value;
}
} // namespace
TEST_CASE("combat.windup_cooldown_and_simultaneous_draw") {
    auto w = CloseDuel();
    FlatNavigationService nav;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(GetStats(w).living_units == 2);
    CHECK(w.attacks.size() == 2);
    CHECK(w.units[0].ready_tick == 20);
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(GetStats(w).living_units == 0);
    CHECK(GetStats(w).dead_units == 2);
    CHECK(w.outcome == BattleOutcome::Draw);
    CHECK(std::count_if(w.events.begin(), w.events.end(),
                        [](auto &e) { return e.kind == EventKind::Died; }) == 2);
}
TEST_CASE("combat.target_filters") {
    auto w = CloseDuel();
    w.regiments[1].team = 0;
    std::vector<BattleEvent> e;
    BuildCombatEvents(w, e);
    CHECK(e.empty());
    w.regiments[1].team = 1;
    w.units[1].alive = false;
    BuildCombatEvents(w, e);
    CHECK(e.empty());
    w.units[1].alive = true;
    w.units[1].pose.position.x = 100;
    BuildCombatEvents(w, e);
    CHECK(e.empty());
}
TEST_CASE("combat.tactical_target_priority_is_independent_of_spatial_cell_order") {
    auto scenario = test::Duel(2);
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    scenario.units[0].archer = true;
    scenario.units[0].ranged_weapon = RangedWeapon::Musket;
    scenario.units[0].weapon.range = 30.f;
    scenario.units[2].id = {300};
    scenario.units[3].id = {800};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    world.units[0].pose = {{0, 0, 0}, 0};
    world.units[1].pose.position = {-10, 30, 0};
    world.units[2].pose.position = {16, 4, 0};
    world.units[3].pose.position = {16, -4, 0};
    const auto pick = [&]() {
        std::vector<BattleEvent> events;
        BuildCombatEvents(world, events);
        const auto shot = std::ranges::find_if(events, [](const auto &event) {
            return event.source == UnitId{1} && event.projectile;
        });
        REQUIRE(shot != events.end());
        return shot->target;
    };
    CHECK(pick() == UnitId{300});
    world.scenario.obstacles.push_back({7, 1, 9, 3, true});
    CHECK(pick() == UnitId{800});
}
TEST_CASE("combat.death_names_the_strike_that_killed_its_exact_target") {
    auto s = test::Duel(2);
    auto w = CreateWorld(s, 42).value;
    const auto untouched = FindUnit(w, {3})->pose;
    const std::array<BattleEvent, 2> hits{{
        {{10}, 0, EventKind::AttackLaunched, {1}, {4}, {1}, 1.f, "", 1, true, true},
        {{11}, 0, EventKind::AttackLaunched, {2}, {4}, {1}, 0.f, "", 1, true, true},
    }};
    CommitCombatEvents(w, hits);
    CHECK(FindUnit(w, {3})->health == 1.f);
    CHECK(FindUnit(w, {3})->pose == untouched);
    CHECK_FALSE(FindUnit(w, {4})->alive);
    auto death = std::find_if(w.events.begin(), w.events.end(),
                              [](const auto &ev) { return ev.kind == EventKind::Died; });
    REQUIRE(death != w.events.end());
    CHECK(death->target == UnitId{4});
    CHECK(death->source == UnitId{1});
    CHECK(death->cause == EventId{10});
}
TEST_CASE("combat.local_melee_holds_front_and_blocks_hits_through_allies") {
    auto s = test::Duel(3);
    s.rules.local_melee = true;
    for (auto &r : s.regiments) {
        r.pose.position.x = r.team ? 1.9f : -1.9f;
        r.formation = {1, 1.1f, .25f};
    }
    const float x[] = {-.48f, -.99f, -3.5f, .48f, .99f, 3.5f};
    for (size_t i = 0; i < s.units.size(); ++i) {
        s.units[i].pose.position.x = x[i];
        s.units[i].health = 100;
    }
    auto w = CreateWorld(s, 42).value;
    const auto blue_center = w.regiments[0].pose;
    const auto orange_center = w.regiments[1].pose;
    const auto blue_front = FindUnit(w, {1})->pose.position;
    const auto orange_front = FindUnit(w, {4})->pose.position;
    const auto blue_reserve = FindUnit(w, {3})->pose;
    FlatNavigationService nav;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.regiments[0].pose == blue_center);
    CHECK(w.regiments[1].pose == orange_center);
    CHECK(FindUnit(w, {1})->pose.position == blue_front);
    CHECK(FindUnit(w, {4})->pose.position == orange_front);
    const auto reserve_after = FindUnit(w, {3})->pose.position;
    CHECK(reserve_after.x > blue_reserve.position.x);
    CHECK(reserve_after.y == doctest::Approx(blue_reserve.position.y));
    CHECK(std::abs(FindUnit(w, {2})->pose.position.y) > .08f);
    for (const auto &e : w.events)
        if (e.kind == EventKind::AttackLaunched)
            CHECK((e.source == UnitId{1} || e.source == UnitId{4}));
    CHECK(w.attacks.size() == 2);

    for (int tick = 0; tick < 20; ++tick) {
        const auto previous = w.units;
        REQUIRE(Step(w, {}, {}, nav));
        for (size_t i = 0; i < w.units.size(); ++i)
            for (size_t j = 0; j < i; ++j) {
                const auto start = previous[i].pose.position - previous[j].pose.position;
                const auto end = w.units[i].pose.position - w.units[j].pose.position;
                const auto travel = end - start;
                const float squared = Dot(travel, travel);
                const float t = squared > 0 ? std::clamp(-Dot(start, travel) / squared, 0.f, 1.f) : 0;
                CHECK(Length(start + travel * t) >= .499f);
            }
    }
}
namespace {
World SkewContact() {
    auto s = test::Duel(9);
    s.rules.local_melee = true;
    for (auto &r : s.regiments) {
        r.pose.position.x = r.team ? 1.8f : -1.8f;
        r.formation = {3, 1.1f, .25f};
    }
    const float front_x[3]{-.60f, -1.00f, -2.40f};
    for (size_t i = 0; i < s.units.size(); ++i) {
        auto &u = s.units[i];
        const bool orange = i >= 9;
        const auto lane = (i % 9) % 3;
        const auto row = (i % 9) / 3;
        const float x = orange ? .60f + float(row) * 1.1f : front_x[lane] - float(row) * 1.1f;
        const float y = float(int(lane) - 1) * (orange ? -1.1f : 1.1f);
        u.pose.position = {x, y, 0.f};
        u.health = 100.f;
    }
    return CreateWorld(s, 42).value;
}
} // namespace
TEST_CASE("combat.local_line_closes_the_gap_on_the_open_wing") {
    auto w = SkewContact();
    FlatNavigationService nav;
    const auto front = FindUnit(w, {1})->pose.position;
    const auto wing = FindUnit(w, {3})->pose.position;
    const auto rear = FindUnit(w, {8})->pose;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(FindUnit(w, {1})->pose.position.x > front.x);
    CHECK(FindUnit(w, {3})->pose.position.x > wing.x + .05f);
    CHECK(FindUnit(w, {3})->pose.position.y == doctest::Approx(wing.y));
    const auto rear_after = FindUnit(w, {8})->pose.position;
    CHECK(rear_after.x > rear.position.x);
    CHECK(rear_after.y == doctest::Approx(rear.position.y));
    CHECK(std::any_of(w.events.begin(), w.events.end(), [](const auto &e) {
        return e.kind == EventKind::AttackLaunched && e.source == UnitId{1};
    }));
}
TEST_CASE("combat.local_line_promotes_one_neighbor_into_a_casualty_gap") {
    auto w = SkewContact();
    auto *victim = FindUnit(w, {2});
    const auto casualty = victim->pose.position;
    const auto reserve = FindUnit(w, {8})->pose;
    const BattleEvent fatal{{10}, 0, EventKind::AttackLaunched, {11}, {2}, {2}, 100.f, "",
                            1, true, true};
    CommitCombatEvents(w, std::span{&fatal, 1});
    REQUIRE_FALSE(victim->alive);
    REQUIRE(w.regiments.front().slots.size() == 9);
    FlatNavigationService nav;
    for (int tick = 0; tick < 13; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    const auto replacement = FindUnit(w, {5})->pose.position;
    CHECK(replacement.x >= casualty.x - .20f);
    CHECK(std::abs(replacement.y - casualty.y) < .25f);
    CHECK(FindUnit(w, {8})->pose.position.x > reserve.position.x);
    CHECK(FindUnit(w, {8})->pose.position.x < replacement.x - .55f);
}
TEST_CASE("combat.local_line_sends_one_supporter_around_the_open_edge") {
    auto w = SkewContact();
    const auto start = FindUnit(w, {4})->pose.position;
    const auto rear = FindUnit(w, {7})->pose;
    FlatNavigationService nav;
    for (int tick = 0; tick < 18; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    const auto supporter = FindUnit(w, {4})->pose.position;
    CHECK(supporter.y < start.y - .44f);
    CHECK(supporter.x > start.x + .50f);
    const auto rear_after = FindUnit(w, {7})->pose.position;
    CHECK(rear_after.x > rear.position.x);
    CHECK(rear_after.y == doctest::Approx(rear.position.y));
    CHECK(Distance(rear_after, supporter) >= .50f);
}
TEST_CASE("combat.local_line_keeps_its_place_during_a_swing") {
    auto w = SkewContact();
    auto *lead = FindUnit(w, {1});
    lead->weapon.windup_ticks = 5;
    FlatNavigationService nav;
    REQUIRE(Step(w, {}, {}, nav));
    auto swing = std::find_if(w.attacks.begin(), w.attacks.end(), [](const auto &a) {
        return a.source == UnitId{1};
    });
    REQUIRE(swing != w.attacks.end());
    const UnitId target = swing->target;
    const auto before = lead->pose.position;
    FindUnit(w, target)->pose.position.y -= .30f;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(lead->pose.position == before);
    const auto offset = FindUnit(w, target)->pose.position - lead->pose.position;
    CHECK(lead->pose.yaw == doctest::Approx(std::atan2(offset.y, offset.x)).epsilon(.05));
}
TEST_CASE("combat.local_line_closes_for_short_weapons") {
    auto s = test::Duel();
    s.rules.local_melee = true;
    for (size_t i = 0; i < s.units.size(); ++i) {
        s.regiments[i].pose.position.x = i == 0 ? -.375f : .375f;
        s.units[i].pose = s.regiments[i].pose;
        s.units[i].weapon.range = .65f;
        s.units[i].health = 100;
    }
    auto w = CreateWorld(s, 42).value;
    FlatNavigationService nav;
    for (int tick = 0; tick < 3; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(FindUnit(w, {1})->health < 100.f);
    CHECK(FindUnit(w, {2})->health < 100.f);
    CHECK(Distance(w.units[0].pose.position, w.units[1].pose.position) >= .599f);
}
TEST_CASE("combat.local_line_holds_a_swing_when_contact_breaks") {
    auto w = SkewContact();
    auto *lead = FindUnit(w, {1});
    lead->weapon.windup_ticks = 5;
    FlatNavigationService nav;
    REQUIRE(Step(w, {}, {}, nav));
    const auto before = lead->pose.position;
    for (auto &u : w.units)
        if (u.regiment == RegimentId{2})
            u.pose.position.x += 10.f;
    REQUIRE(Step(w, {}, {}, nav));
    CHECK(lead->pose.position == before);
}
TEST_CASE("combat.local_line_can_reach_an_offset_opponent_in_its_sector") {
    auto s = test::Duel();
    s.rules.local_melee = true;
    for (size_t i = 0; i < s.units.size(); ++i) {
        s.regiments[i].pose.position = i == 0 ? Vec3{-.20f, 0, 0} : Vec3{.20f, .75f, 0};
        s.regiments[i].formation = {1, 1.1f, .25f};
        s.units[i].pose = s.regiments[i].pose;
        s.units[i].weapon.range = .65f;
        s.units[i].health = 100;
    }
    auto w = CreateWorld(s, 42).value;
    FlatNavigationService nav;
    for (int tick = 0; tick < 5; ++tick)
        REQUIRE(Step(w, {}, {}, nav));
    CHECK(w.units[0].pose.position.y > .05f);
    CHECK(w.units[0].health < 100.f);
    CHECK(w.units[1].health < 100.f);
}
TEST_CASE("combat.projectile_timing_and_miss") {
    auto w = CreateWorld(test::Duel(), 42).value;
    w.units[0].pose.position = {0, 0, 0};
    w.units[1].pose.position = {10, 0, 0};
    w.units[0].control = ControlMode::PlayerControlled;
    w.units[1].control = ControlMode::PlayerControlled;
    w.units[0].attack_target = {2};
    w.units[0].archer = true;
    w.units[0].archer_skill = 0;
    w.units[0].weapon.range = 30;
    w.units[0].weapon.projectile_speed_mps = 20;
    std::vector<BattleEvent> e;
    BuildCombatEvents(w, e);
    REQUIRE(e.size() == 1);
    CHECK(e[0].due_tick == 10);
    CHECK_FALSE(e[0].hit);
    CommitCombatEvents(w, e);
    CHECK(w.attacks.size() == 1);
    for (Tick t = 1; t < 9; ++t) {
        w.tick = t;
        CommitCombatEvents(w, {});
    }
    CHECK(w.units[1].health == 1);
    w.tick = 9;
    CommitCombatEvents(w, {});
    CHECK(w.attacks.empty());
    CHECK(w.events.back().kind == EventKind::Miss);
    CHECK(HitProbability(1, 1, 5, 30) > HitProbability(1, 1, 20, 30));
    CHECK(HitProbability(1, 1, 5, 30) > HitProbability(1, .5f, 5, 30));
}
TEST_CASE("combat.armour_and_duplicate_events") {
    auto w = CloseDuel();
    w.units[1].health = 4;
    w.units[1].armour = 2;
    w.units[0].weapon.damage = 3;
    w.units[0].weapon.windup_ticks = 1;
    w.units[1].control = ControlMode::PlayerControlled;
    std::vector<BattleEvent> e;
    BuildCombatEvents(w, e);
    REQUIRE(e.size() == 1);
    CommitCombatEvents(w, e);
    CHECK(w.units[1].health == 3);
    CommitCombatEvents(w, e);
    CHECK(w.units[1].health == 3);
    auto a = e[0];
    a.id = {100};
    a.amount = 50;
    auto b = a;
    b.id = {101};
    std::vector<BattleEvent> lethal{a, b};
    CommitCombatEvents(w, lethal);
    CHECK(w.units[1].health == 0);
    CHECK(GetStats(w).dead_units == 1);
    CHECK(std::count_if(w.events.begin(), w.events.end(), [](auto &v) {
              return v.kind == EventKind::Died && v.target == UnitId{2};
          }) == 1);
}
TEST_CASE("combat.panic_and_rally") {
    auto s = test::Duel(3);
    s.officers = {{{1}, {1}, 1}};
    s.rules.calm_ticks = 3;
    auto w = CreateWorld(s, 42).value;
    for (auto id : {UnitId{2}, UnitId{3}}) {
        auto *u = FindUnit(w, id);
        u->alive = false;
        u->health = 0;
        u->control = ControlMode::Dead;
    }
    w.regiments[0].units = {{1}};
    UpdateMorale(w);
    CHECK(w.regiments[0].panic);
    std::vector<BattleEvent> e;
    BuildCombatEvents(w, e);
    CHECK(std::none_of(e.begin(), e.end(), [](auto &x) { return x.source == UnitId{1}; }));
    for (auto &u : w.units)
        if (u.regiment == RegimentId{2})
            u.pose.position.x = 200;
    w.regiments[0].pose.position = w.regiments[0].rally_point;
    Order order;
    order.type = OrderType::Rally;
    order.status = OrderStatus::Active;
    w.regiments[0].order = order;
    for (int t = 0; t < 3; ++t) {
        UpdateMorale(w);
        ++w.tick;
    }
    CHECK_FALSE(w.regiments[0].panic);
    CHECK(w.regiments[0].ai_action == OrderType::Hold);
    UpdateMorale(w);
    CHECK_FALSE(w.regiments[0].panic);
}
TEST_CASE("combat.casualties_close_ranks_without_teleport") {
    auto s = test::Duel(4);
    s.regiments[0].formation.columns = 2;
    s.regiments[1].formation.columns = 2;
    auto w = CreateWorld(s, 42).value;
    auto old = FindUnit(w, {4})->pose;
    BattleEvent e;
    e.id = {100};
    e.kind = EventKind::AttackLaunched;
    e.source = {5};
    e.target = {1};
    e.amount = 10;
    e.hit = true;
    e.projectile = true;
    e.due_tick = 1;
    CommitCombatEvents(w, std::span{&e, 1});
    CHECK(w.regiments[0].slots.size() == 3);
    CHECK(FindUnit(w, {4})->pose == old);
    CHECK(FindUnit(w, {4})->id.value == 4);
}
TEST_CASE("combat.rally_is_interrupted_and_panic_no_path") {
    auto s = test::Duel(5);
    s.officers = {{{1}, {1}, 1}};
    s.rules.calm_ticks = 3;
    auto w = CreateWorld(s, 42).value;
    for (auto id : {UnitId{2}, UnitId{3}, UnitId{4}}) {
        FindUnit(w, id)->alive = false;
        FindUnit(w, id)->health = 0;
    }
    w.regiments[0].units = {{1}, {5}};
    UpdateMorale(w);
    REQUIRE(w.regiments[0].panic);
    GridNavigationService nav({}, 1, 30, 30, {-15, -15, 0});
    UpdateOfficers(w, nav);
    CHECK(w.regiments[0].route.empty());
    CHECK(w.regiments[0].panic);
    for (auto &u : w.units)
        if (u.regiment == RegimentId{2})
            u.pose.position.x = 200;
    w.regiments[0].pose.position = w.regiments[0].rally_point;
    Order o;
    o.type = OrderType::Rally;
    o.status = OrderStatus::Active;
    w.regiments[0].order = o;
    ++w.tick;
    UpdateMorale(w);
    CHECK(w.regiments[0].calm_counter == 1);
    FindUnit(w, {6})->pose.position = w.regiments[0].rally_point;
    UpdateMorale(w);
    CHECK(w.regiments[0].calm_counter == 0);
    FindUnit(w, {6})->pose.position.x = 200;
    w.regiments[0].last_damage_tick = w.tick;
    UpdateMorale(w);
    CHECK(w.regiments[0].calm_counter == 0);
}
TEST_CASE("combat.indexed_target_matches_full_radius_rule") {
    auto w = CreateWorld(test::Duel(64), 42).value;
    uint64_t random = 123;
    auto sample = [&]() {
        random = random * 6364136223846793005ull + 1;
        return float(int((random >> 32) % 6001) - 3000) / 100.f;
    };
    for (auto &u : w.units) {
        u.pose.position = {sample(), sample(), 0};
        u.archer = true;
        u.weapon.range = 30;
    }
    w.units.back().alive = false;
    w.units.back().health = 0;
    w.units.back().control = ControlMode::Dead;
    std::vector<BattleEvent> events;
    BuildCombatEvents(w, events);
    for (const auto &u : w.units) {
        if (!u.alive)
            continue;
        const UnitState *expected = nullptr;
        float best = u.weapon.range;
        for (const auto &t : w.units) {
            auto delta = t.pose.position - u.pose.position;
            if (!t.alive ||
                FindRegiment(w, t.regiment)->team == FindRegiment(w, u.regiment)->team ||
                double(delta.x) * delta.x + double(delta.y) * delta.y + double(delta.z) * delta.z >
                    double(u.weapon.range) * u.weapon.range)
                continue;
            float d = Length(delta);
            if (!expected || d < best || (d == best && t.id < expected->id)) {
                expected = &t;
                best = d;
            }
        }
        auto found =
            std::find_if(events.begin(), events.end(), [&](auto &e) { return e.source == u.id; });
        REQUIRE((found != events.end()) == bool(expected));
        if (expected)
            CHECK(found->target == expected->id);
    }
    CHECK(std::none_of(events.begin(), events.end(),
                       [&](auto &e) { return e.target == w.units.back().id; }));
}
TEST_CASE("combat.ranged_cohorts_distribute_and_preserve_cadence") {
    auto source = test::Duel(512);
    source.rules.ranged_cohorts = 4;
    for (auto &u : source.units) {
        u.archer = true;
        u.health = 1000;
        u.weapon.range = 2000;
        u.weapon.base_accuracy = 0;
        u.weapon.projectile_speed_mps = 1000000;
    }
    auto w = CreateWorld(source, 42).value;
    FlatNavigationService nav;
    std::map<uint64_t, std::vector<Tick>> fired;
    std::vector<size_t> counts(24);
    for (int tick = 0; tick < 24; ++tick) {
        auto step = Step(w, {}, {}, nav);
        REQUIRE(step);
        for (auto &e : step.value.events)
            if (e.kind == EventKind::AttackLaunched) {
                fired[e.source.value].push_back(e.tick);
                ++counts[tick];
            }
    }
    CHECK(fired.size() == 1024);
    for (int tick = 0; tick < 4; ++tick) {
        CHECK(counts[tick] > 0);
        CHECK(counts[tick] < 400);
        CHECK(counts[tick] == counts[tick + 20]);
    }
    for (auto &[id, ticks] : fired) {
        REQUIRE(ticks.size() == 2);
        CHECK(ticks[1] - ticks[0] == 20);
        CHECK(ticks[0] == RangedPhase({id}, FindUnit(w, {id})->regiment, 4));
    }
    source.rules.ranged_cohorts = 1;
    auto simultaneous = CreateWorld(source, 42).value;
    REQUIRE(Step(simultaneous, {}, {}, nav));
    CHECK(std::count_if(simultaneous.events.begin(), simultaneous.events.end(),
                        [](auto &e) { return e.kind == EventKind::AttackLaunched; }) == 1024);
    auto custom = test::Duel();
    custom.rules.ranged_cohorts = 4;
    custom.units[0].archer = true;
    custom.units[0].weapon.range = 30;
    custom.units[0].weapon.base_accuracy = 0;
    custom.units[0].weapon.cooldown_ticks = 5;
    auto c = CreateWorld(custom, 42).value;
    std::vector<Tick> ticks;
    for (int i = 0; i < 20; ++i) {
        REQUIRE(Step(c, {}, {}, nav));
        for (auto &e : c.events)
            if (e.kind == EventKind::AttackLaunched && e.source == UnitId{1})
                ticks.push_back(e.tick);
    }
    REQUIRE(ticks.size() >= 3);
    for (size_t i = 1; i < ticks.size(); ++i)
        CHECK(ticks[i] - ticks[i - 1] == 5);
}
TEST_CASE("combat.player_ignores_ai_phase_and_control_switch") {
    auto source = test::Duel();
    source.rules.ranged_cohorts = 4;
    UnitId player{100};
    while (RangedPhase(player, {1}, 4) == 0)
        ++player.value;
    source.units[0].id = player;
    source.units[0].archer = true;
    source.units[0].weapon.range = 1000000000;
    source.units[0].weapon.base_accuracy = 1;
    source.units[0].health = 10;
    source.units[1].health = 10;
    auto w = CreateWorld(source, 42).value;
    FlatNavigationService nav;
    Command acquire;
    acquire.kind = CommandKind::AcquireControl;
    acquire.unit = player;
    Command attack;
    attack.kind = CommandKind::AttackControlledUnit;
    attack.unit = player;
    attack.target = {2};
    std::vector<CommandEnvelope> commands{{0, 1, acquire}, {0, 2, attack}};
    REQUIRE(Step(w, commands, {}, nav));
    REQUIRE(w.attacks.size() == 1);
    CHECK(w.attacks[0].source == player);
    CHECK(w.attacks[0].hit);
    auto arrow = w.attacks[0].id;
    Command release;
    release.kind = CommandKind::ReleaseControl;
    release.unit = player;
    CommandEnvelope input{w.tick, 3, release};
    REQUIRE(Step(w, std::span{&input, 1}, {}, nav));
    CHECK_FALSE(w.controlled);
    CHECK(FindUnit(w, player)->health == 10);
    CHECK(w.attacks.size() == 1);
    w.regiments[0].panic = true;
    FindUnit(w, player)->ready_tick = 0;
    std::vector<BattleEvent> blocked;
    BuildCombatEvents(w, blocked);
    CHECK(blocked.empty());
    BattleEvent kill;
    kill.id = {w.next_event_id};
    kill.kind = EventKind::AttackLaunched;
    kill.source = {2};
    kill.target = player;
    kill.due_tick = w.tick + 1;
    kill.hit = true;
    kill.projectile = true;
    kill.amount = 100;
    CommitCombatEvents(w, std::span{&kill, 1});
    REQUIRE_FALSE(FindUnit(w, player)->alive);
    bool resolved = false;
    while (w.tick < 21) {
        REQUIRE(Step(w, {}, {}, nav));
        for (auto &e : w.events)
            if (e.cause == arrow && e.kind == EventKind::Hit)
                resolved = true;
    }
    CHECK(resolved);
    CHECK(FindUnit(w, {2})->health == 9);
    CHECK(w.attacks.empty());
}

TEST_CASE("combat.archers_skip_engaged_regiments_and_draw_daggers") {
    auto s = test::Duel();
    s.rules.local_melee = true;
    for (auto &r : s.regiments)
        r.formation = {1, 1.1f, .25f};
    s.regiments[0].pose.position = {-.5f, 0, 0};
    s.regiments[1].pose.position = {1.22f, 0, 0};
    auto add = [&](uint64_t id, uint64_t regiment, int team, Vec3 position, bool archer) {
        if (regiment > s.regiments.size()) {
            auto r = s.regiments.front();
            r.id = {regiment};
            r.team = team;
            r.pose.position = position;
            s.regiments.push_back(r);
        }
        UnitSpec unit;
        unit.id = {id};
        unit.regiment = {regiment};
        unit.pose.position = position;
        unit.health = 100.f;
        unit.archer = archer;
        unit.weapon.range = archer ? 14.f : 1.4f;
        unit.weapon.damage = 3.f;
        return unit;
    };
    s.units = {add(1, 1, 0, {0, 0, 0}, false),
               add(2, 2, 1, {.72f, 0, 0}, false),
               add(3, 1, 0, {0, 1.f, 0}, true),
               add(4, 2, 1, {.72f, 1.f, 0}, true),
               add(5, 3, 0, {-5.f, 0, 0}, true),
               add(6, 4, 1, {5.f, 0, 0}, true)};
    auto created = CreateWorld(s, 42);
    REQUIRE(created);
    auto &w = created.value;
    std::vector<BattleEvent> events;
    BuildCombatEvents(w, events);
    for (auto id : {UnitId{3}, UnitId{4}}) {
        const auto attack = std::find_if(events.begin(), events.end(),
                                         [&](const auto &e) { return e.source == id; });
        REQUIRE(attack != events.end());
        CHECK_FALSE(attack->projectile);
        CHECK(attack->amount < .5f * FindUnit(w, id)->weapon.damage);
    }
    const auto volley = std::find_if(events.begin(), events.end(),
                                     [](const auto &e) { return e.source == UnitId{5}; });
    REQUIRE(volley != events.end());
    CHECK(volley->projectile);
    CHECK(volley->target == UnitId{6});
}

TEST_CASE("combat.local_line_turns_smoothly_into_an_edge_bypass") {
    auto w = SkewContact();
    FlatNavigationService nav;
    auto *support = FindUnit(w, {4});
    Vec3 previous;
    int movements = 0;
    for (int tick = 0; tick < 20; ++tick) {
        const auto start = support->pose.position;
        REQUIRE(Step(w, {}, {}, nav));
        const auto travel = support->pose.position - start;
        if (Length(travel) < .03f)
            continue;
        if (movements++)
            CHECK(Dot(travel, previous) / (Length(travel) * Length(previous)) > .2f);
        previous = travel;
    }
    CHECK(movements >= 5);
}

TEST_CASE("combat.skirmishers_move_to_flanks_and_rejoin_on_order") {
    auto s = test::Duel(5);
    s.rules.local_melee = true;
    s.regiments[0].formation = {3, 1.1f, .25f};
    s.regiments[0].skirmisher_count = 2;
    s.regiments[1].pose.position.x = 100.f;
    for (auto &u : s.units)
        u.pose = s.regiments[u.regiment.value - 1].pose;
    s.units[3].archer = s.units[4].archer = true;
    s.officers = {{{1}, {1}, 2}};
    auto created = CreateWorld(s, 42);
    REQUIRE(created);
    auto &w = created.value;
    for (int i = 0; i < 80; ++i)
        AdvanceFormations(w, .05f);
    for (auto id : {UnitId{4}, UnitId{5}}) {
        const auto *unit = FindUnit(w, id);
        REQUIRE(unit->skirmisher);
        CHECK(std::abs(unit->pose.position.y - w.regiments[0].pose.position.y) > 1.5f);
        CHECK(Distance(unit->pose.position, unit->slot.position) > 1.5f);
    }
    CommandEnvelope recall;
    recall.sequence = 1;
    recall.command.kind = CommandKind::SetSkirmishers;
    recall.command.regiment = {1};
    FlatNavigationService navigation;
    REQUIRE(Step(w, std::span{&recall, 1}, {}, navigation));
    CHECK_FALSE(w.regiments[0].skirmishers_enabled);
    for (int i = 0; i < 100; ++i)
        AdvanceFormations(w, .05f);
    for (auto id : {UnitId{4}, UnitId{5}}) {
        const auto *unit = FindUnit(w, id);
        CHECK_FALSE(unit->skirmisher);
        CHECK_FALSE(unit->skirmisher_returning);
        CHECK(Distance(unit->pose.position, unit->slot.position) < .06f);
    }
}

TEST_CASE("combat.skirmisher_fire_is_independent_of_its_regiment") {
    auto s = test::Duel(2);
    s.rules.local_melee = true;
    for (auto &r : s.regiments)
        r.formation = {1, 1.1f, .25f};
    const std::array<Vec3, 4> spots{{{0.f, 0.f, 0.f}, {0.f, 5.f, 0.f},
                                     {.80f, 0.f, 0.f}, {.80f, 9.f, 0.f}}};
    for (size_t i = 0; i < s.units.size(); ++i) {
        s.units[i].pose.position = spots[i];
        s.units[i].health = 100;
        s.units[i].archer = i % 2 != 0;
        s.units[i].weapon.range = s.units[i].archer ? 16.f : 1.5f;
    }
    auto created = CreateWorld(s, 42);
    REQUIRE(created);
    auto &w = created.value;
    FindUnit(w, {2})->skirmisher = true;
    FindUnit(w, {4})->skirmisher = true;
    std::vector<BattleEvent> events;
    BuildCombatEvents(w, events);
    const auto ranged = std::find_if(events.begin(), events.end(),
                                     [](const auto &e) { return e.source == UnitId{2}; });
    REQUIRE(ranged != events.end());
    CHECK(ranged->projectile);
    CHECK(ranged->target == UnitId{4});
}

TEST_CASE("combat.skirmishers_follow_their_regiment_across_a_crossing") {
    auto s = test::Duel(7);
    s.rules.local_melee = true;
    s.obstacles = MarchObstacles();
    s.regiments[0].formation = {7, 1.1f, .25f};
    s.regiments[0].pose.position = {-12, 0, 0};
    s.regiments[0].skirmisher_count = 1;
    s.regiments[1].pose.position = {100, 0, 0};
    for (auto &u : s.units)
        u.pose = s.regiments[u.regiment.value - 1].pose;
    s.units[6].archer = true;
    s.officers = {{{1}, {1}, 2}};
    auto created = CreateWorld(s, 42);
    REQUIRE(created);
    auto &w = created.value;
    for (int i = 0; i < 80; ++i)
        AdvanceFormations(w, .05f);
    REQUIRE(FindUnit(w, {7})->pose.position.y > 4.5f);
    w.regiments[0].route = {{{12, 0, 0}, 0.f}};
    for (int i = 0; i < 400; ++i)
        AdvanceFormations(w, .05f);
    REQUIRE(w.regiments[0].pose.position.x > 11.f);
    CHECK(FindUnit(w, {7})->pose.position.x > 6.f);
}

TEST_CASE("combat.only_skirmishers_can_choose_detached_targets") {
    auto s = test::Duel(2);
    s.rules.local_melee = true;
    s.units[0].archer = true;
    s.units[0].weapon.range = 16.f;
    s.units[0].pose.position = {0, 0, 0};
    s.units[1].pose.position = {-8, 0, 0};
    s.units[2].pose.position = {8, 0, 0};
    s.units[3].archer = true;
    s.units[3].pose.position = {4, 0, 0};
    UnitSpec officer;
    officer.id = {5};
    officer.independent_team = 1;
    officer.pose.position = {2, 0, 0};
    s.units.push_back(officer);
    s.officers = {{{1}, officer.id, 1}};
    auto created = CreateWorld(s, 42);
    REQUIRE(created);
    auto &w = created.value;
    FindUnit(w, {4})->skirmisher = true;
    std::vector<BattleEvent> events;
    const auto archer_event = [&]() {
        BuildCombatEvents(w, events);
        return std::find_if(events.begin(), events.end(), [](const auto &e) {
            return e.source == UnitId{1};
        });
    };
    const auto formed = archer_event();
    REQUIRE(formed != events.end());
    CHECK(formed->projectile);
    CHECK(formed->target == UnitId{3});
    FindUnit(w, {1})->skirmisher = true;
    const auto detached = archer_event();
    REQUIRE(detached != events.end());
    CHECK(detached->projectile);
    CHECK(detached->target == UnitId{4});
}

TEST_CASE("combat.full_regiment_skirmishers_advance_to_clear_firing_positions") {
    auto scenario = test::Duel(50);
    scenario.rules.local_melee = true;
    for (auto &regiment : scenario.regiments) {
        regiment.formation = {7, 1.1f, .25f};
        regiment.speed = 3.f;
        regiment.pose.position.x = regiment.team == 0 ? -20.f : 5.f;
    }
    scenario.regiments[0].skirmisher_count = 3;
    scenario.officers = {{{1}, {1}, 2}};
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.speed = 4.f;
        unit.health = 18.f;
        unit.archer = unit.id.value >= 41 && unit.id.value <= 50;
        unit.weapon.range = unit.archer ? 16.f : 1.65f;
    }
    auto created = CreateWorld(scenario, 43);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    regiment.route = {{{-12.f, 0.f, 0.f}, 0.f}};
    for (int step = 0; step < 180; ++step)
        AdvanceFormations(world, .05f);
    REQUIRE(regiment.pose.position.x == doctest::Approx(-12.f));
    REQUIRE_FALSE(regiment.melee_engaged);
    std::vector<BattleEvent> shots;
    BuildCombatEvents(world, shots);
    for (uint64_t id = 48; id <= 50; ++id) {
        const auto &archer = *FindUnit(world, {id});
        REQUIRE(archer.skirmisher);
        const auto offset = archer.pose.position - regiment.pose.position;
        CAPTURE(id);
        CHECK(std::abs(offset.y) > Footprint(regiment).width_m * .5f + .7f);
        CHECK(offset.x > Footprint(regiment).depth_m * .5f + .2f);
        CHECK(std::any_of(shots.begin(), shots.end(), [&](const auto &event) {
            return event.source == archer.id && event.projectile;
        }));
    }
}

TEST_CASE("combat.stone_cover_blocks_archers_and_they_choose_a_visible_alternative") {
    auto scenario = test::Duel();
    scenario.rules.local_melee = true;
    scenario.regiments[0].pose.position = {-5, 0, 0};
    scenario.regiments[1].pose.position = {2, 0, 0};
    scenario.units[0].pose = scenario.regiments[0].pose;
    scenario.units[0].archer = true;
    scenario.units[0].weapon.range = 16.f;
    scenario.units[1].pose = scenario.regiments[1].pose;
    auto exposed = scenario.units[1];
    exposed.id = {3};
    exposed.pose.position = {5, 8, 0};
    scenario.units.push_back(exposed);
    scenario.obstacles.push_back({-.5f, -2.f, .5f, 2.f});
    auto world = CreateWorld(scenario, 42);
    REQUIRE(world);
    std::vector<BattleEvent> launches;
    const auto shot = [&] {
        BuildCombatEvents(world.value, launches);
        return std::find_if(launches.begin(), launches.end(), [](const auto &event) {
            return event.source == UnitId{1} && event.projectile;
        });
    };
    const auto clear = shot();
    REQUIRE(clear != launches.end());
    CHECK(clear->target == UnitId{2});
    world.value.scenario.obstacles.front().blocks_projectiles = true;
    const auto covered = shot();
    REQUIRE(covered != launches.end());
    CHECK(covered->target == exposed.id);
    auto &archer = *FindUnit(world.value, {1});
    archer.control = ControlMode::PlayerControlled;
    archer.attack_target = {2};
    CHECK(shot() == launches.end());
}
