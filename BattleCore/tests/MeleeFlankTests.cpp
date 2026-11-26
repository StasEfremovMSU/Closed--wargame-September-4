#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Simulation.h"
#include "doctest.h"
using namespace battle;

namespace {
World UnevenFronts() {
    Scenario scenario;
    scenario.rules.local_melee = true;
    for (unsigned side = 0; side < 2; ++side) {
        RegimentSpec regiment;
        regiment.id = {side + 1};
        regiment.team = int32_t(side);
        regiment.pose = {{side ? 1.75f : -.65f, 0, 0}, side ? Pi : 0};
        regiment.formation = {side ? 3u : 7u, 1.1f, .25f};
        scenario.regiments.push_back(regiment);
        const unsigned rows = side ? 3 : 1;
        for (unsigned i = 0; i < regiment.formation.columns * rows; ++i) {
            UnitSpec unit;
            unit.id = {scenario.units.size() + 1};
            unit.regiment = regiment.id;
            unit.pose = regiment.pose;
            unit.speed = 2.5f;
            unit.health = 1000.f;
            unit.weapon.range = 1.45f;
            scenario.units.push_back(unit);
        }
    }
    auto result = CreateWorld(scenario, 202);
    REQUIRE(result);
    return std::move(result.value);
}
} // namespace

TEST_CASE("combat.uncovered_front_columns_turn_the_enemy_wings") {
    auto world = UnevenFronts();
    const auto fixed_center = world.regiments.front().pose;
    FlatNavigationService navigation;
    bool south_hit = false, north_hit = false;
    int first_hit = -1;
    for (int tick = 0; tick < 120; ++tick) {
        REQUIRE(Step(world, {}, {}, navigation));
        CHECK(world.regiments.front().pose == fixed_center);
        for (const auto &event : world.events) {
            if (event.kind != EventKind::Hit || event.projectile || event.source.value > 7)
                continue;
            const auto *source = FindUnit(world, event.source);
            const auto *target = FindUnit(world, event.target);
            const auto along = source->pose.position.x - target->pose.position.x;
            const auto across = source->pose.position.y - target->pose.position.y;
            if (along >= -.30f) {
                south_hit |= (event.source.value <= 2 && across < -.45f);
                north_hit |= (event.source.value >= 6 && across > .45f);
                if (first_hit < 0 && (south_hit || north_hit))
                    first_hit = tick;
            }
        }
    }
    CAPTURE(first_hit);
    CHECK(south_hit);
    CHECK(north_hit);
}


TEST_CASE("combat.terrain_closes_only_the_obstructed_wing") {
    auto world = UnevenFronts();
    world.scenario.obstacles.push_back({-.05f, 1.45f, 4.f, 6.f});
    FlatNavigationService navigation;
    bool open_wing_hit = false;
    for (int tick = 0; tick < 120; ++tick) {
        REQUIRE(Step(world, {}, {}, navigation));
        for (const auto &unit : world.units) {
            const auto p = unit.pose.position;
            CHECK_FALSE((p.x > -.05f && p.x < 4.f && p.y > 1.45f && p.y < 6.f));
        }
        for (const auto &event : world.events) {
            if (event.kind != EventKind::Hit || event.projectile || event.source.value > 7)
                continue;
            const auto *source = FindUnit(world, event.source);
            const auto *target = FindUnit(world, event.target);
            const auto along = source->pose.position.x - target->pose.position.x;
            const auto across = source->pose.position.y - target->pose.position.y;
            if (along >= -.30f) {
                CHECK_FALSE((event.source.value >= 6 && across > .45f));
                open_wing_hit |= event.source.value <= 2 && across < -.45f;
            }
        }
    }
    CHECK(open_wing_hit);
}

TEST_CASE("combat.offset_frontal_hit_is_not_a_regimental_flank") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose = {{.9f, .48f, 0.f}, Pi};
    scenario.regiments[1].pose = {{}, 0.f};
    for (auto &regiment : scenario.regiments)
        regiment.formation = {3, 1.1f, .25f};
    scenario.units[0].pose = {{.40f, .48f, 0.f}, Pi};
    scenario.units[1].pose.position = {3.f, -3.f, 0.f};
    scenario.units[2].pose.position = {3.f, 3.f, 0.f};
    for (size_t i = 3; i < scenario.units.size(); ++i)
        scenario.units[i].pose = {{0.f, (float(i) - 4.f) * 1.1f, 0.f}, 0.f};
    for (auto &unit : scenario.units)
        unit.health = 100.f;

    auto created = CreateWorld(scenario, 17);
    REQUIRE(created);
    auto &world = created.value;
    const auto &attacker = *FindUnit(world, {1});
    const auto &defender = *FindUnit(world, {5});
    CHECK(ClassifyAttackAspect(world, attacker, defender) == AttackAspect::Front);

    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    const auto launch = std::ranges::find(launches, UnitId{1}, &BattleEvent::source);
    REQUIRE(launch != launches.end());
    REQUIRE(launch->target == defender.id);
    CHECK(launch->aspect == AttackAspect::Front);
    CommitCombatEvents(world, std::span(&*launch, 1));
    world.tick = launch->due_tick;
    CommitCombatEvents(world, {});
    const auto hit = std::ranges::find(world.events, EventKind::Hit, &BattleEvent::kind);
    REQUIRE(hit != world.events.end());
    CHECK(hit->aspect == AttackAspect::Front);

    auto &moving = *FindUnit(world, {1});
    world.regiments.front().pose.yaw = -Pi / 2;
    moving.pose = {{.10f, 1.8f, 0.f}, -Pi / 2};
    CHECK(ClassifyAttackAspect(world, moving, *FindUnit(world, {6})) == AttackAspect::Flank);
    moving.pose.position = {-.90f, 0.f, 0.f};
    CHECK(ClassifyAttackAspect(world, moving, defender) == AttackAspect::Rear);
}

TEST_CASE("combat.aligned_frontage_overhang_does_not_turn_frontal_melee_hits_into_flanks") {
    auto world = UnevenFronts();
    world.scenario.rules.local_melee = false;
    world.scenario.rules.regiment_tactics = true;
    world.regiments[0].pose = {{1.f, 0.f, 0.f}, Pi};
    world.regiments[1].pose = {{-1.1f, 0.f, 0.f}, 0.f};
    for (uint64_t i = 0; i < 7; ++i)
        FindUnit(world, {i + 1})->pose = {{.15f, (float(i) - 3.f) * .9f, 0.f}, Pi};
    for (uint64_t i = 0; i < 9; ++i)
        FindUnit(world, {i + 8})->pose = {{-float(i / 3) * 1.1f, (float(i % 3) - 1.f) * 1.1f, 0.f}, 0.f};

    std::vector<BattleEvent> launches;
    BuildCombatEvents(world, launches);
    for (const auto source : {UnitId{2}, UnitId{6}}) {
        const auto launch = std::ranges::find(launches, source, &BattleEvent::source);
        REQUIRE(launch != launches.end());
        CHECK(launch->target == UnitId{source.value == 2 ? 8u : 10u});
        CHECK(launch->aspect == AttackAspect::Front);
    }

    const auto due = launches.front().due_tick;
    CommitCombatEvents(world, launches);
    world.tick = due;
    CommitCombatEvents(world, {});
    for (const auto source : {UnitId{2}, UnitId{6}}) {
        const auto hit = std::ranges::find_if(world.events, [&](const BattleEvent &event) {
            return event.kind == EventKind::Hit && event.source == source;
        });
        REQUIRE(hit != world.events.end());
        CHECK(hit->aspect == AttackAspect::Front);
    }
}

TEST_CASE("combat.an_individual_can_turn_the_shoulder_of_a_head_on_formation") {
    auto world = UnevenFronts();
    world.regiments[0].pose = {{1.f, 0.f, 0.f}, Pi};
    world.regiments[1].pose = {{-1.1f, 0.f, 0.f}, 0.f};
    for (uint64_t i = 0; i < 9; ++i)
        FindUnit(world, {i + 8})->pose = {{-float(i / 3) * 1.1f, (float(i % 3) - 1.f) * 1.1f, 0.f}, 0.f};
    auto &attacker = *FindUnit(world, {6});
    const auto &shoulder = *FindUnit(world, {10});
    const auto &rear = *FindUnit(world, {15});

    attacker.pose = {{-.15f, 1.8f, 0.f}, -Pi / 2};
    CHECK(ClassifyAttackAspect(world, attacker, shoulder) == AttackAspect::Flank);
    attacker.pose = {{-2.9f, 0.f, 0.f}, 0.f};
    CHECK(ClassifyAttackAspect(world, attacker, rear) == AttackAspect::Rear);
}

TEST_CASE("combat.contact_column_advances_into_an_open_diagonal") {
    auto scenario = test::Duel(3);
    scenario.rules.local_melee = true;
    scenario.regiments[0].pose.position.x = -.7f;
    scenario.regiments[1].pose.position.x = .7f;
    for (auto &regiment : scenario.regiments)
        regiment.formation = {3, 1.1f, .25f};
    const Vec3 positions[] = {{-.54f, -1.10f, 0.f}, {-1.60f, 0.f, 0.f}, {-.54f, 1.10f, 0.f},
                              {.54f, 1.26f, 0.f}, {1.7f, -1.26f, 0.f}, {.54f, -1.26f, 0.f}};
    for (size_t i = 0; i < scenario.units.size(); ++i) {
        auto &unit = scenario.units[i];
        unit.pose.position = positions[i];
        unit.health = 100.f;
        unit.weapon.range = 1.4f;
        unit.speed = 2.5f;
    }
    auto created = CreateWorld(scenario, 12);
    REQUIRE(created);
    auto &world = created.value;
    const auto initial = FindUnit(world, {2})->pose.position;
    const auto anchor = world.regiments.front().pose;
    FlatNavigationService navigation;
    for (int tick = 0; tick < 13; ++tick) {
        REQUIRE(Step(world, {}, {}, navigation));
        for (size_t i = 0; i < world.units.size(); ++i)
            for (size_t j = 0; j < i; ++j)
                CHECK(Distance(world.units[i].pose.position, world.units[j].pose.position) >= .498f);
    }
    CHECK(world.regiments.front().pose == anchor);
    CHECK(FindUnit(world, {2})->pose.position.x > initial.x + .42f);
}

TEST_CASE("combat.separated_line_reassigns_crossed_slots") {
    auto scenario = test::Duel(3);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    for (auto &regiment : scenario.regiments)
        regiment.formation = {3, 1.1f, .25f};
    auto created = CreateWorld(scenario, 44);
    REQUIRE(created);
    auto &world = created.value;
    auto &first = *FindUnit(world, {1});
    auto &third = *FindUnit(world, {3});
    const auto first_slot = first.slot_index;
    const auto third_slot = third.slot_index;
    REQUIRE(first_slot != third_slot);
    std::swap(first.pose.position, third.pose.position);
    world.regiments.front().melee_engaged = true;
    FlatNavigationService navigation;

    REQUIRE(Step(world, {}, {}, navigation));
    REQUIRE(world.regiments.front().contact_end_tick == world.tick);
    for (int tick = 0; tick < 20; ++tick)
        REQUIRE(Step(world, {}, {}, navigation));

    CHECK(first.slot_index == third_slot);
    CHECK(third.slot_index == first_slot);
    CHECK(Distance(first.pose.position, first.slot.position) < .08f);
    CHECK(Distance(third.pose.position, third.slot.position) < .08f);
}
