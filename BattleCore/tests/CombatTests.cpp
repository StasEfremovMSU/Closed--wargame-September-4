#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Officers.h"
#include "doctest.h"
using namespace battle;
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
