#include "TestSupport.h"
#include "battle/Formation.h"
#include "doctest.h"
#include <set>
using namespace battle;
TEST_CASE("formation.slots") {
    auto slots = BuildFormationSlots({5, 2, 0.3f}, {}, 25);
    REQUIRE(slots.size() == 25);
    std::set<std::pair<float, float>> unique;
    for (auto p : slots)
        unique.insert({p.position.x, p.position.y});
    CHECK(unique.size() == 25);
    CHECK(slots[0].position.x == 4);
    CHECK(slots[0].position.y == -4);
    CHECK(slots[4].position.y == 4);
    CHECK(slots[5].position.x == 2);
    auto rotated = BuildFormationSlots({5, 2, 0.3f}, {{}, Pi / 2}, 25);
    CHECK(rotated[0].position.x == doctest::Approx(4));
    CHECK(rotated[0].position.y == doctest::Approx(4));
    CHECK(Distance(rotated[0].position, rotated[1].position) == doctest::Approx(2));
    CHECK(BuildFormationSlots({1, 2, 0.3f}, {}, 0).empty());
}
TEST_CASE("formation.speed_and_reform") {
    auto s = test::Duel(3);
    s.regiments.resize(1);
    s.units.resize(3);
    s.regiments[0].pose = {};
    for (auto &u : s.units)
        u.pose = {};
    auto w = CreateWorld(s, 42).value;
    auto &r = w.regiments[0];
    r.route = {{{20, 0, 0}, 0}};
    for (int i = 0; i < 20; ++i)
        AdvanceFormations(w, 0.05f);
    CHECK(r.pose.position.x == doctest::Approx(2).epsilon(0.0001));
    auto id = w.units[2].id;
    auto before = w.units[2].pose;
    w.units[0].alive = false;
    r.units.erase(r.units.begin());
    CHECK(ReformRegiment(w, r.id, {1, 2, 0.3f}) == Status::Ok);
    CHECK(w.units[2].id == id);
    CHECK(w.units[2].pose == before);
    CHECK(r.units.size() == 2);
    CHECK(r.slots.size() == 2);
    CHECK(ReformRegiment(w, r.id, {0, 2, 0.3f}) == Status::InvalidInput);
}
TEST_CASE("formation.contact_order_independence") {
    auto s = test::Duel();
    s.regiments[0].pose.position.x = -1;
    s.regiments[1].pose.position.x = 1;
    for (auto &u : s.units)
        u.pose = s.regiments[u.regiment.value - 1].pose;
    auto a = CreateWorld(s, 42).value, b = a;
    a.regiments[0].route = {{{5, 0, 0}, 0}};
    a.regiments[1].route = {{{-5, 0, 0}, Pi}};
    b = a;
    std::reverse(b.regiments.begin(), b.regiments.end());
    for (int i = 0; i < 100; ++i) {
        AdvanceFormations(a, .05f);
        AdvanceFormations(b, .05f);
    }
    CHECK(Distance(a.regiments[0].pose.position, a.regiments[1].pose.position) >= .599f);
    for (const auto &r : a.regiments) {
        auto it = std::find_if(b.regiments.begin(), b.regiments.end(),
                               [&](auto &x) { return x.id == r.id; });
        REQUIRE(it != b.regiments.end());
        CHECK(r.pose.position.x == doctest::Approx(it->pose.position.x));
    }
}
TEST_CASE("formation.fast_regiments_do_not_tunnel") {
    auto s = test::Duel();
    s.regiments[0].pose.position.x = -1;
    s.regiments[1].pose.position.x = 1;
    for (auto &r : s.regiments)
        r.speed = 100;
    for (auto &u : s.units) {
        u.speed = 100;
        u.pose = s.regiments[u.regiment.value - 1].pose;
    }
    auto w = CreateWorld(s, 42).value;
    w.regiments[0].route = {{{5, 0, 0}, 0}};
    w.regiments[1].route = {{{-5, 0, 0}, Pi}};
    AdvanceFormations(w, .05f);
    CHECK(w.regiments[0].pose.position.x < w.regiments[1].pose.position.x);
}
