#include "CommandSupport.h"
#include "battle/Combat.h"
#include "battle/Formation.h"
#include "battle/Snapshot.h"
#include "battle/command/Brains.h"
#include "doctest.h"
#include <array>
#include <tuple>

using namespace battle;

namespace {
GridNavigationService FieldNavigation() {
    return GridNavigationService({}, .5f, 120, 120, {-30, -30, 0});
}
}

TEST_CASE("combat.finite_ammunition_creates_only_local_gunpowder_smoke") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    auto &shooter = scenario.units[0];
    shooter.archer = true;
    shooter.ranged_weapon = RangedWeapon::Gunpowder;
    shooter.ammunition = 2;
    shooter.weapon = {0.f, 30.f, 2, 1, 100.f, 1.f};
    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    auto &world = created.value;
    auto nav = FieldNavigation();
    int launches = 0, smoke = 0;
    for (int i = 0; i < 90; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        for (const auto &e : world.events) {
            launches += e.kind == EventKind::AttackLaunched;
            smoke += e.kind == EventKind::SmokeCreated;
        }
        for (const auto &cloud : world.smoke)
            CHECK(Distance(cloud.center, shooter.pose.position) < .01f);
    }
    CHECK(launches == 2);
    CHECK(smoke == 2);
    CHECK(world.units[0].ammunition_remaining == 0u);
    CHECK(world.smoke.empty());
    REQUIRE(Restore(Capture(world)));
}

TEST_CASE("combat.era_muskets_and_arcane_bolts_use_finite_stock_with_distinct_smoke") {
    for (const auto kind : {RangedWeapon::Musket, RangedWeapon::Arcane}) {
        auto scenario = test::Duel();
        scenario.rules.regiment_tactics = true;
        auto &shooter = scenario.units[0];
        shooter.archer = true;
        shooter.ranged_weapon = kind;
        shooter.ammunition = 2;
        shooter.weapon = {0.f, 30.f, 2, 1, 100.f, 1.f};
        auto created = CreateWorld(scenario, 1);
        REQUIRE(created);
        auto &world = created.value;
        const auto nav = FieldNavigation();
        int launches = 0, smoke = 0;
        for (int i = 0; i < 60; ++i) {
            REQUIRE(Step(world, {}, {}, nav));
            for (const auto &event : world.events) {
                launches += event.kind == EventKind::AttackLaunched && event.projectile;
                smoke += event.kind == EventKind::SmokeCreated;
            }
        }
        CHECK(launches == 2);
        CHECK(smoke == (kind == RangedWeapon::Musket ? 2 : 0));
        CHECK(world.units[0].ammunition_remaining == 0u);
        const auto restored = Restore(Capture(world));
        REQUIRE(restored);
        CHECK(restored.value.units[0].ranged_weapon == kind);
    }
}

TEST_CASE("combat.era_field_gun_fires_a_straight_smoking_shot_with_finite_stock") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    auto &gun = scenario.units[0];
    gun.archer = true;
    gun.platform = BattlePlatform::FieldGun;
    gun.speed = .9f;
    gun.ammunition = 1;
    gun.weapon = {0.f, 85.f, 170, 4, 80.f, 1.f};
    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    auto &world = created.value;
    const auto nav = FieldNavigation();
    int launches = 0, smoke = 0;
    for (int i = 0; i < 30; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        for (const auto &event : world.events) {
            if (event.kind == EventKind::AttackLaunched && event.projectile) {
                CHECK_FALSE(event.high_arc);
                ++launches;
            }
            smoke += event.kind == EventKind::SmokeCreated;
        }
    }
    CHECK(launches == 1);
    CHECK(smoke == 1);
    CHECK(world.units[0].speed > 0.f);
    CHECK(world.units[0].weapon.range == 85.f);
}

TEST_CASE("combat.mounted_impact_requires_an_approach_and_brace_changes_the_result") {
    auto run = [](bool brace, bool starts_at_contact) {
        auto scenario = test::Duel(2);
        scenario.rules.regiment_tactics = true;
        scenario.rules.local_melee = true;
        if (starts_at_contact) {
            scenario.regiments[0].pose.position.x = 9.f;
            for (size_t i = 0; i < 2; ++i)
                scenario.units[i].pose = scenario.regiments[0].pose;
        }
        scenario.regiments[0].speed = 5.f;
        for (size_t i = 0; i < scenario.units.size(); ++i) {
            auto &u = scenario.units[i];
            u.health = 100.f;
            u.speed = 5.f;
            if (i < 2) {
                u.troop = UnitTroop::Mounted;
                u.cavalry = CavalryClass::Heavy;
            } else {
                u.troop = UnitTroop::Spearman;
            }
            u.weapon = {10.f, 1.5f, 20, 2};
        }
        auto created = CreateWorld(scenario, 5);
        REQUIRE(created);
        auto &world = created.value;
        Order attack;
        attack.recipient = {1};
        attack.target_regiment = {2};
        attack.type = OrderType::Attack;
        REQUIRE(SubmitOrder(world, attack) == Status::Ok);
        Order hold;
        hold.recipient = {2};
        hold.type = brace ? OrderType::Brace : OrderType::HoldLine;
        REQUIRE(SubmitOrder(world, hold) == Status::Ok);
        auto nav = FieldNavigation();
        unsigned impacts = 0;
        float damage = 0.f;
        bool approached = false, reformed = false;
        for (int i = 0; i < 170; ++i) {
            REQUIRE(Step(world, {}, {}, nav));
            const auto phase = world.regiments[0].cavalry_phase;
            approached |= phase == CavalryPhase::Approach;
            reformed |= phase == CavalryPhase::Reform;
            for (const auto &event : world.events) {
                if (event.kind == EventKind::CavalryImpact) {
                    ++impacts;
                    damage += event.amount;
                }
            }
        }
        return std::tuple{approached, reformed, impacts, damage};
    };
    auto [approached, reforming, impacts, damage] = run(false, false);
    auto [braced_approach, braced_reform, braced_impacts, braced_damage] = run(true, false);
    auto [close_approach, close_reform, close_impacts, close_damage] = run(false, true);
    CHECK(approached);
    CHECK(reforming);
    CHECK(impacts > 0);
    CHECK(braced_approach);
    CHECK(braced_reform);
    CHECK(braced_impacts > 0);
    CHECK(braced_damage < damage);
    CHECK(close_impacts == 0);
}

TEST_CASE("formation.field_movement_respects_pace_frontage_and_reverse_facing") {
    auto scenario = test::Duel(4);
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].formation.columns = 2;
    auto nav = FieldNavigation();
    const auto move = [&](MovementPace pace, unsigned ticks) {
        auto created = CreateWorld(scenario, 12);
        REQUIRE(created);
        Order order;
        order.type = OrderType::MoveTo;
        order.recipient = {1};
        order.point = {-2.f, 0.f, 0.f};
        order.pace = pace;
        order.facing = Pi * .5f;
        order.move_columns = 4;
        REQUIRE(SubmitOrder(created.value, order) == Status::Ok);
        for (unsigned i = 0; i < ticks; ++i)
            REQUIRE(Step(created.value, {}, {}, nav));
        return std::move(created.value);
    };
    const auto walk = move(MovementPace::Walk, 45);
    const auto march = move(MovementPace::March, 45);
    const auto run = move(MovementPace::Run, 45);
    CHECK(walk.regiments.front().pose.position.x < march.regiments.front().pose.position.x);
    CHECK(march.regiments.front().pose.position.x < run.regiments.front().pose.position.x);
    auto complete = move(MovementPace::Walk, 130);
    auto &regiment = complete.regiments.front();
    REQUIRE(regiment.order);
    CHECK(regiment.order->status == OrderStatus::Completed);
    CHECK(regiment.formation.columns == 4u);
    CHECK(regiment.pose.yaw == doctest::Approx(Pi * .5f));

    Order reverse;
    reverse.type = OrderType::FallBack;
    reverse.recipient = {1};
    reverse.point = {-2.f, -3.f, 0.f};
    REQUIRE(SubmitOrder(complete, reverse) == Status::Ok);
    for (int i = 0; i < 20; ++i)
        REQUIRE(Step(complete, {}, {}, nav));
    CHECK(regiment.pose.position.y == doctest::Approx(-1.f).epsilon(.05f));
    CHECK(regiment.pose.yaw == doctest::Approx(Pi * .5f));
    REQUIRE(Restore(Capture(complete)));
}

TEST_CASE("formation.group_translation_moves_into_a_sibling's_vacated_footprint") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.field_bounds = TerrainFieldBounds{10.f, 3.f};
    scenario.regiments[1].team = 0;
    scenario.regiments[1].speed = .5f;
    scenario.regiments[0].pose = {{0.f, 0.f, 0.f}, 0.f};
    scenario.regiments[1].pose = {{0.f, 1.f, 0.f}, 0.f};
    scenario.units[0].pose = scenario.regiments[0].pose;
    scenario.units[1].pose = scenario.regiments[1].pose;
    auto blocker = scenario.regiments[1];
    blocker.id = {3};
    blocker.pose.position = {3.f, 2.4f, 0.f};
    scenario.regiments.push_back(blocker);
    auto blocker_unit = scenario.units[1];
    blocker_unit.id = {3};
    blocker_unit.regiment = blocker.id;
    blocker_unit.pose = blocker.pose;
    scenario.units.push_back(blocker_unit);
    auto created = CreateWorld(scenario, 11);
    REQUIRE(created);
    auto &world = created.value;
    auto nav = FieldNavigation();
    const std::array<RegimentId, 2> members{{{1}, {2}}};
    const Vec3 delta{0.f, 1.f, 0.f};
    REQUIRE(CanTranslateFormationGroup(world, members, delta));
    CHECK_FALSE(CanTranslateFormationGroup(world, members, {0.f, 2.f, 0.f}));
    world.scenario.obstacles.push_back({-.1f, .4f, .1f, .5f});
    CHECK_FALSE(CanTranslateFormationGroup(world, members, delta));
    world.scenario.obstacles.clear();
    FindRegiment(world, {1})->panic = true;
    CHECK_FALSE(CanTranslateFormationGroup(world, members, delta));
    FindRegiment(world, {1})->panic = false;
    for (const auto id : members) {
        Order order;
        order.recipient = id;
        order.type = OrderType::MoveTo;
        order.maneuver_group = 1;
        order.point = FindRegiment(world, id)->pose.position + delta;
        order.facing = 0.f;
        REQUIRE(SubmitOrder(world, order) == Status::Ok);
    }
    for (int i = 0; i < 48; ++i) {
        if (i == 10 || i == 15) {
            const float x = i == 10 ? 0.f : 3.f;
            FindRegiment(world, blocker.id)->pose.position.x = x;
            FindUnit(world, blocker_unit.id)->pose.position.x = x;
            CHECK(CanTranslateFormationGroup(world, members, {0.f, .75f, 0.f}) == (i == 15));
        }
        REQUIRE(Step(world, {}, {}, nav));
        const auto a = FindRegiment(world, {1})->pose.position.y;
        const auto b = FindRegiment(world, {2})->pose.position.y;
        CHECK(b - a == doctest::Approx(1.f));
        if (i >= 9 && i < 15)
            CHECK(a == doctest::Approx(.25f));
    }
    CHECK(FindRegiment(world, {1})->pose.position.y == doctest::Approx(1.f));
    CHECK(FindRegiment(world, {2})->pose.position.y == doctest::Approx(2.f));
    REQUIRE(Restore(Capture(world)));
}

TEST_CASE("formation.mounted_march_faces_travel_then_takes_the_prescribed_front") {
    auto scenario = test::Duel(3);
    scenario.rules.regiment_tactics = true;
    scenario.rules.local_melee = true;
    for (size_t i = 0; i < 3; ++i) {
        scenario.units[i].troop = UnitTroop::Mounted;
        scenario.units[i].speed = 4.f;
    }
    scenario.regiments[0].speed = 4.f;
    scenario.regiments[0].rally_point = {-27.f, 0.f, 0.f};
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    auto nav = FieldNavigation();
    Order order;
    order.recipient = {1};
    order.type = OrderType::MoveTo;
    order.point = {-23.f, 0.f, 0.f};
    bool explicit_yaw = false;
    SUBCASE("a plain march faces the direction of the destination") {}
    SUBCASE("an ordered stance is taken at arrival") {
        explicit_yaw = true;
        order.facing = 0.f;
    }
    SUBCASE("mounted disengagement travels forward") {
        order.type = OrderType::FallBack;
    }
    REQUIRE(SubmitOrder(world, order) == Status::Ok);
    auto &regiment = world.regiments[0];
    float travel = 0.f;
    float rearward = 0.f;
    for (int i = 0; i < 300; ++i) {
        const auto previous = regiment.pose.position;
        REQUIRE(Step(world, {}, {}, nav));
        const auto delta = regiment.pose.position - previous;
        travel += Length(delta);
        rearward += std::max(0.f, -Dot(delta, Forward(regiment.pose.yaw)));
        if (regiment.order && regiment.order->status == OrderStatus::Completed)
            break;
    }
    CHECK(travel > 12.5f);
    CHECK(rearward < .05f);
    CHECK(Distance(regiment.pose.position, order.point) < .01f);
    if (explicit_yaw)
        CHECK(std::abs(regiment.pose.yaw) < .01f);
    else if (order.type == OrderType::MoveTo)
        CHECK(std::abs(std::abs(regiment.pose.yaw) - Pi) < .01f);
}

TEST_CASE("combat.authorized_pursuit_catches_a_routing_regiment") {
    auto scenario = test::Duel();
    scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose.position.x = 9.1f;
    scenario.regiments[0].speed = 4.f;
    scenario.regiments[1].rally_point = {25.f, 0.f, 0.f};
    scenario.units[0].pose = scenario.regiments[0].pose;
    scenario.units[0].speed = 4.f;
    scenario.units[0].weapon = {0.f, 1.5f, 1, 1};
    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    world.regiments[1].panic = true;
    Order chase;
    chase.recipient = {1};
    chase.target_regiment = {2};
    chase.type = OrderType::Pursue;
    REQUIRE(SubmitOrder(world, chase) == Status::Ok);

    const auto nav = FieldNavigation();
    bool pursuit_hit = false;
    for (unsigned i = 0; i < 12 && world.units[1].alive; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        for (const auto &event : world.events)
            pursuit_hit |= event.kind == EventKind::Hit && event.reason.starts_with("pursuit");
    }
    CHECK(pursuit_hit);
    CHECK_FALSE(world.units[1].alive);
}

TEST_CASE("combat.rear_rank_replaces_front_loss_then_reassembles_after_separation") {
    auto scenario = test::Duel(4);
    scenario.rules.local_melee = true;
    scenario.rules.regiment_tactics = true;
    for (auto &regiment : scenario.regiments) {
        regiment.formation.columns = 2;
        regiment.pose.position.x = regiment.team ? 1.7f : -1.7f;
    }
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.weapon.damage = 0.f;
        unit.weapon.cooldown_ticks = 6;
    }
    auto created = CreateWorld(scenario, 9);
    REQUIRE(created);
    auto &world = created.value;
    const auto nav = FieldNavigation();
    REQUIRE(Step(world, {}, {}, nav));
    auto &blue = world.regiments[0];
    REQUIRE(blue.melee_engaged);

    auto &fallen = *FindUnit(world, {1});
    auto &replacement = *FindUnit(world, {3});
    const Pose old_line = blue.pose;
    const Vec3 rear_start = replacement.pose.position;
    fallen.alive = false;
    fallen.health = 0.f;
    fallen.control = ControlMode::Dead;
    std::erase(blue.units, fallen.id);
    bool replacement_struck = false;
    for (int i = 0; i < 52; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        CHECK(blue.melee_engaged);
        for (const auto &event : world.events)
            replacement_struck |= event.kind == EventKind::AttackLaunched &&
                                  event.source == replacement.id;
    }
    CHECK(replacement.pose.position.x > rear_start.x + 1.f);
    CHECK(replacement_struck);
    CHECK(blue.pose == old_line);

    auto &enemy = world.regiments[1];
    enemy.pose.position.x += 10.f;
    for (const auto id : enemy.units)
        FindUnit(world, id)->pose.position.x += 10.f;
    bool reformed = false;
    for (int i = 0; i < 50; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        for (const auto &event : world.events)
            reformed |= event.kind == EventKind::FormationReformed && event.regiment == blue.id;
    }
    CHECK_FALSE(blue.melee_engaged);
    CHECK(blue.slots.size() == blue.units.size());
    CHECK(reformed);
}

TEST_CASE("world.field_objective_supplies_a_root_mission_without_replacing_authored_orders") {
    test::CommandFixture fixture;
    auto scenario = fixture.world.scenario;
    auto &setup = scenario.command_setup.emplace();
    setup.hierarchy = fixture.hierarchy;
    scenario.objective = ScenarioObjective{{-4.f, 2.f, 0.f}, 3.f, 0};
    auto created = CreateWorld(scenario, 1);
    REQUIRE(created);
    REQUIRE(created.value.command_state);
    const auto &defense = created.value.command_state->nodes.front().orders;
    REQUIRE(defense.size() == 1u);
    CHECK(defense.front().action == command::TaskKind::Defend);
    CHECK(defense.front().intent.objective_position == scenario.objective->center);
    CHECK(defense.front().intent.objective_radius_m == 3.f);

    command::CommandTask mission;
    mission.recipient = {1};
    mission.action = command::TaskKind::HoldArea;
    setup.missions.push_back(mission);
    created = CreateWorld(scenario, 1);
    REQUIRE(created);
    REQUIRE(created.value.command_state);
    const auto &explicit_order = created.value.command_state->nodes.front().orders;
    REQUIRE(explicit_order.size() == 1u);
    CHECK(explicit_order.front().action == command::TaskKind::HoldArea);
}

TEST_CASE("world.narrow_field_objective_stays_centered_through_the_command_chain") {
    for (const auto level : {command::Level::Army, command::Level::Part, command::Level::Group}) {
        test::UpperBrainFixture fixture(level);
        constexpr Vec3 objective{20.f, 1.f, 0.f};
        for (auto &child : fixture.children)
            child.commander = {child.id.value + 500};
        for (auto &status : fixture.statuses)
            status.width_m = 12.f;
        auto &mission = fixture.tasks.front();
        mission.action = command::TaskKind::MoveToArea;
        mission.intent.target_regiment = {};
        mission.intent.objective_position = objective;
        mission.intent.objective_radius_m = .5f;
        const auto context = fixture.Context();
        const auto result = level == command::Level::Army ? command::ArmyBrain{}.Think(context) :
            level == command::Level::Part ? command::PartBrain{}.Think(context) :
                                            command::GroupBrain{}.Think(context);
        REQUIRE(result.status == command::StubStatus::Ready);
        REQUIRE(result.value);
        const auto &tasks = result.value->subordinate_tasks;
        CHECK(std::ranges::count_if(tasks, [&](const auto &task) {
            return task.action == command::TaskKind::MoveToArea &&
                   task.intent.objective_position == objective;
        }) == 1);
    }
}
