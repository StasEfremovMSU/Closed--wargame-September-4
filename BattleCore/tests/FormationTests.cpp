#include "TestSupport.h"
#include "battle/Formation.h"
#include "battle/Spatial.h"
#include "doctest.h"
#include <set>
using namespace battle;

TEST_CASE("formation.only_a_short_infantry_guard_step_uses_slow_reverse_footwork") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    bool mounted = false;
    float separation = .6f;
    SUBCASE("infantry takes a short defensive backstep") {}
    SUBCASE("infantry turns for a long move back to its rank") { separation = 3.f; }
    SUBCASE("a rider turns for the move back to its rank") { separation = 3.f; mounted = true; }
    auto &spec = scenario.units[0];
    spec.troop = mounted ? UnitTroop::Mounted : UnitTroop::Infantry;
    spec.speed = 3.f;
    auto created = CreateWorld(scenario, 29);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments[0];
    regiment.ai_action = OrderType::HoldLine;
    auto &unit = world.units[0];
    unit.pose.position.x += separation;
    const auto previous = unit.pose.position;
    AdvanceFormations(world, .1f);
    const auto delta = unit.pose.position - previous;
    if (separation < 1.f) {
        CHECK(Length(delta) == doctest::Approx(.15f));
        CHECK(std::abs(unit.pose.yaw) < .001f);
    } else {
        CHECK(Length(delta) == doctest::Approx(.30f));
        CHECK(Dot(delta, Forward(unit.pose.yaw)) == doctest::Approx(Length(delta)));
    }
}

TEST_CASE("formation.rout_breaks_rank_alignment_with_room_between_figures") {
    auto scenario = test::Duel(6);
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].formation = {3, 1.1f, .25f};
    auto created = CreateWorld(scenario, 18);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    regiment.panic = true;
    regiment.ai_action = OrderType::Withdraw;
    regiment.route = {{regiment.pose.position + Vec3{5.f, 0.f, 0.f}, regiment.pose.yaw}};
    for (int i = 0; i < 15; ++i)
        AdvanceFormations(world, .05f);
    const auto formal = BuildFormationSlots(regiment.formation, regiment.pose, uint32_t(regiment.units.size()));
    CHECK(regiment.slots != formal);
    for (size_t i = 0; i < regiment.units.size(); ++i) {
        const auto &a = *FindUnit(world, regiment.units[i]);
        for (size_t j = i + 1; j < regiment.units.size(); ++j) {
            const auto &b = *FindUnit(world, regiment.units[j]);
            CHECK(Distance(a.pose.position, b.pose.position) >= 2.f * regiment.formation.body_radius_m);
        }
    }
}

TEST_CASE("formation.infantry_faces_purposeful_translation_even_with_a_fixed_swept_footprint") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    auto created = CreateWorld(scenario, 19);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    regiment.ai_action = OrderType::MoveTo;
    regiment.route = {{regiment.pose.position + Vec3{-5.f, 0.f, 0.f}, regiment.pose.yaw}};
    const auto before = world.units.front().pose.position;
    AdvanceFormations(world, .1f);
    const auto &unit = world.units.front();
    const auto translation = unit.pose.position - before;
    REQUIRE(Length(translation) > .1f);
    CHECK(Dot(translation, Forward(unit.pose.yaw)) == doctest::Approx(Length(translation)));
    CHECK(regiment.pose.yaw == 0.f);
}
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
TEST_CASE("formation.linear_battalion_frontages_fit_a_battlefield") {
    RegimentState regiment;
    regiment.formation = {16, 1.1f, .25f};
    regiment.units.resize(512);
    const auto line = LinearInfantryFormation(regiment, true);
    const auto column = LinearInfantryFormation(regiment, false);
    CHECK(line.columns <= 64);
    CHECK(column.columns * 2 < line.columns);
    regiment.formation = line;
    CHECK(IsLinearFiringLine(regiment));
    regiment.formation = column;
    CHECK_FALSE(IsLinearFiringLine(regiment));
}

TEST_CASE("formation.spatial_payload_queries_keep_radius_geometry_and_filter_team") {
    const std::vector<UnitPosition> points{
        {{11}, {0.f, 0.f, 0.f}, 0, 8},
        {{21}, {1.f, 0.f, 0.f}, 1, 16},
        {{4}, {-1.f, 0.f, 0.f}, 1, 2},
        {{2}, {0.f, 0.f, 1.5f}, 1, 12},
        {{3}, {.5f, -.5f, 0.f}, 2, 19}
    };
    SpatialIndex index{.5f};
    index.Rebuild(points);
    CHECK(index.QueryRadius({}, 1.f) ==
          std::vector<UnitId>{{3}, {4}, {11}, {21}});
    std::vector<size_t> payloads;
    index.QueryRadiusPayloadsInto({}, 1.f, payloads);
    CHECK(payloads == std::vector<size_t>{2, 8, 16, 19});
    index.QueryRadiusPayloadsInto({}, 1.f, payloads, 0);
    CHECK(payloads == std::vector<size_t>{2, 16, 19});
    index.QueryRadiusPayloadsUnorderedInto({}, 1.f, payloads);
    std::sort(payloads.begin(), payloads.end());
    CHECK(payloads == std::vector<size_t>{2, 8, 16, 19});
}

TEST_CASE("formation.wide_battalion_keeps_its_ranks_during_a_player_ordered_turn") {
    constexpr unsigned Figures = 288;
    auto scenario = test::Duel(Figures);
    scenario.regiments.resize(1);
    scenario.units.resize(Figures);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.rules.adaptive_battle = true;
    scenario.rules.linear_infantry = true;
    scenario.regiments[0].formation = {64, 1.1f, .25f};
    scenario.regiments[0].speed = 3.f;
    for (auto &unit : scenario.units)
        unit.speed = 3.f;
    scenario.officers = {{{1}, {1}, 2}};
    bool move_before_turn = false;
    bool retarget = false;
    SUBCASE("open ground and relocation") {
        move_before_turn = true;
    }
    SUBCASE("plough under one wing") {
        scenario.obstacles.push_back({-100.f, 0.f, 100.f, 100.f, false, TerrainFeature::PloughedField});
    }
    SUBCASE("Waterloo relief") {
        scenario.hill = TerrainHill{280.f, 190.f, 9.f, .35f, TerrainRelief::Waterloo};
    }
    SUBCASE("new facing midway through a turn") {
        retarget = true;
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    const auto start = regiment.pose;
    const auto destination = start.position + Vec3{0.f, move_before_turn ? 8.f : 0.f, 0.f};
    CommandEnvelope request;
    request.sequence = 1;
    request.command.kind = CommandKind::IssueOrder;
    request.command.order.issuer = {1};
    request.command.order.recipient = {1};
    request.command.order.type = OrderType::MoveTo;
    request.command.order.priority = PlayerOrderPriority;
    request.command.order.point = destination;
    request.command.order.facing = Pi * .5f;
    request.command.order.player_direct = true;

    FlatNavigationService navigation;
    REQUIRE(Step(world, std::span{&request, 1}, {}, navigation));
    REQUIRE(regiment.order);
    float max_slot_lag = 0.f;
    float minimum_row_spacing = regiment.formation.spacing_m;
    float minimum_file_spacing = regiment.formation.spacing_m;
    bool stable_slots = true;
    for (int tick = 0; tick < 850; ++tick) {
        std::span<const CommandEnvelope> input;
        if (retarget && tick == 164) {
            request.tick = world.tick;
            request.sequence = 2;
            request.command.order.facing = -Pi * .5f;
            input = std::span{&request, 1};
        }
        REQUIRE(Step(world, input, {}, navigation));
        const auto forward = Forward(regiment.pose.yaw);
        const auto right = Right(regiment.pose.yaw);
        for (size_t index = 0; index < world.units.size(); ++index) {
            const auto &unit = world.units[index];
            max_slot_lag = std::max(max_slot_lag, Distance(unit.pose.position, unit.slot.position));
            stable_slots &= unit.slot_index == index;
            if (index % regiment.formation.columns != 0) {
                const auto offset = unit.pose.position - world.units[index - 1].pose.position;
                minimum_row_spacing = std::min(minimum_row_spacing, Dot(offset, right));
            }
            if (index >= regiment.formation.columns) {
                const auto &ahead = world.units[index - regiment.formation.columns];
                const auto offset = ahead.pose.position - unit.pose.position;
                minimum_file_spacing = std::min(minimum_file_spacing, Dot(offset, forward));
            }
        }
        if (regiment.order->status == OrderStatus::Completed && (!retarget || request.sequence == 2))
            break;
    }
    CHECK(regiment.order->status == OrderStatus::Completed);
    CHECK(Distance(regiment.pose.position, destination) < .02f);
    const float facing = (retarget ? -1.f : 1.f) * Pi * .5f;
    CHECK(std::abs(regiment.pose.yaw - facing) < .01f);
    CHECK(stable_slots);
    CHECK(max_slot_lag < .18f);
    CHECK(minimum_row_spacing >= .95f);
    CHECK(minimum_file_spacing >= .95f);
}

TEST_CASE("formation.mounted_wheel_does_not_freeze_on_a_steep_outside_file") {
    constexpr unsigned Riders = 9;
    auto scenario = test::Duel(Riders);
    scenario.regiments.resize(1);
    scenario.units.resize(Riders);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    scenario.hill = TerrainHill{4.f, 4.f, 6.f, .2f};
    auto &spec = scenario.regiments.front();
    spec.pose = {{4.5f, 0.f, 0.f}, Pi * .5f};
    spec.formation = {Riders, 1.1f, .3f};
    for (auto &unit : scenario.units) {
        unit.pose = spec.pose;
        unit.troop = UnitTroop::Mounted;
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments.front();
    REQUIRE(HillSegmentPassable(*scenario.hill, regiment.pose.position, regiment.pose.position,
                                UnitTroop::Mounted));
    REQUIRE(std::ranges::any_of(world.units, [&](const auto &unit) {
        return !HillSegmentPassable(*scenario.hill, unit.pose.position, unit.pose.position,
                                    UnitTroop::Mounted);
    }));
    regiment.route = {{regiment.pose.position, Pi}};
    for (int tick = 0; tick < 200 && regiment.waypoint == 0; ++tick)
        AdvanceFormations(world, .05f);
    CHECK(regiment.waypoint == 1);
    CHECK(std::abs(regiment.pose.yaw - Pi) < .001f);
}
