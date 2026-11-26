#include "CommandSupport.h"
#include "battle/Snapshot.h"
#include "doctest.h"
#include <array>
using namespace battle;
namespace c = battle::command;

TEST_CASE("army_fsm.mission_balance_and_lost_target") {
    test::UpperBrainFixture f(c::Level::Army);

    f.tasks[0].action = c::TaskKind::HoldArea;
    auto result = c::ArmyBrain{}.Think(f.Context());
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks.size() == 3);
    for (const auto &task : result.value->subordinate_tasks)
        CHECK((task.action == c::TaskKind::HoldArea || task.action == c::TaskKind::Reserve));

    f.tasks[0].action = c::TaskKind::Attack;
    f.tasks[0].intent.target_regiment = {2};
    f.tasks[0].intent.objective_position = {30, 0, 0};
    f.tasks[0].id = {2};
    result = c::ArmyBrain{}.Think(f.Context(1));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Advancing);

    f.situation.contacts.clear();
    result = c::ArmyBrain{}.Think(f.Context(2));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Failed);
    for (const auto &task : result.value->subordinate_tasks)
        CHECK(task.action == c::TaskKind::HoldArea);
}

TEST_CASE("army_fsm.defend_reserve_release_and_safe_empty") {
    test::UpperBrainFixture f(c::Level::Army);
    f.tasks.clear();
    f.situation.contacts.clear();
    auto result = c::ArmyBrain{}.Think(f.Context());
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Holding);

    c::KnownContact contact;
    contact.regiment = {2};
    contact.team = 1;
    contact.allegiance = c::Allegiance::Enemy;
    contact.estimated_pose.position = {30, 0, 0};
    contact.estimated_strength = 30;
    contact.confidence = 1;
    f.situation.contacts.push_back(contact);
    result = c::ArmyBrain{}.Think(f.Context(1));
    REQUIRE(result.value);
    CHECK((result.value->next_memory->state == c::FsmState::Attacking ||
           result.value->next_memory->state == c::FsmState::Advancing));
    REQUIRE(result.value->subordinate_tasks.size() == 3);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Reserve);

    f.memory = *result.value->next_memory;
    f.situation.own.living = 75;
    f.situation.own.initial = 100;
    result = c::ArmyBrain{}.Think(f.Context(2));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Supporting);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Support);

    test::UpperBrainFixture panic(c::Level::Army);
    panic.tasks.clear();
    panic.statuses[0].panic = true;
    result = c::ArmyBrain{}.Think(panic.Context());
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Supporting);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Support);

    f.children.clear();
    f.statuses.clear();
    f.node.children.clear();
    result = c::ArmyBrain{}.Think(f.Context(3));
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks.empty());
    CHECK(c::ArmyBrain{}.Think({}).status == c::StubStatus::InvalidInput);
}

TEST_CASE("army_fsm.a_wing_request_sends_reserve_to_its_own_sector") {
    test::UpperBrainFixture f(c::Level::Army);
    f.children[0].sector_role = c::SectorRole::Center;
    f.children[1].sector_role = c::SectorRole::RightFlank;
    for (auto &child : f.children) {
        child.level = c::Level::Group;
        child.commander = {child.id.value};
    }
    const auto center = f.children[0].id;
    const auto wing = f.children[1].id;
    const auto reserve = f.children[2].id;
    c::SituationReport request;
    request.kind = c::ReportKind::NeedSupport;
    request.sender = wing;
    request.recipient = f.node.id;
    request.observed_tick = 8;
    request.created_tick = 8;
    const Vec3 staging{-6, 12, 0};
    Vec3 expected_goal = staging;
    SUBCASE("respond while attacking") {}
    SUBCASE("respond while holding") {
        f.tasks[0].action = c::TaskKind::HoldArea;
        f.tasks[0].intent.target_regiment = {};
        expected_goal = f.statuses[1].pose.position;
    }
    auto context = f.Context(10);
    context.reports = std::span{&request, 1};
    context.choose_group_support = [&](c::NodeId mover, c::NodeId assisted, const c::KnownContact &)
        -> std::optional<Vec3> {
        if (mover != reserve)
            return {};
        return assisted == wing ? staging : Vec3{-6, 0, 0};
    };
    const auto result = c::ArmyBrain{}.Think(context);
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Supporting);
    const auto &tasks = result.value->subordinate_tasks;
    const auto dispatched = std::ranges::find(tasks, reserve, &c::CommandTask::recipient);
    REQUIRE(dispatched != tasks.end());
    CHECK(dispatched->action == c::TaskKind::Support);
    CHECK(dispatched->intent.target_command == wing);
    CHECK(dispatched->intent.objective_position == expected_goal);
    const auto center_task = std::ranges::find(tasks, center, &c::CommandTask::recipient);
    REQUIRE(center_task != tasks.end());
    CHECK(center_task->action == f.tasks[0].action);
}

TEST_CASE("army_fsm.an_authorized_area_attack_searches_assigned_lanes_for_contact") {
    test::UpperBrainFixture f(c::Level::Army);
    f.situation.contacts.clear();
    f.children[0].sector_role = c::SectorRole::Center;
    f.children[1].sector_role = c::SectorRole::RightFlank;
    for (auto &child : f.children) {
        child.level = c::Level::Group;
        child.commander = {child.id.value};
    }
    auto &mission = f.tasks.front();
    mission.intent.target_regiment = {};
    mission.intent.objective_position = {20, 0, 0};
    mission.authority.initiative = c::InitiativePolicy::WithinIntent;
    mission.authority.may_change_target = true;
    mission.authority.may_pursue = true;
    bool searches = true;
    SUBCASE("area attack without contact") {}
    SUBCASE("an explicit hold stays a hold") {
        mission.action = c::TaskKind::HoldArea;
        searches = false;
    }
    SUBCASE("an explicit attack waits for its missing target") {
        mission.intent.target_regiment = {2};
        searches = false;
    }
    bool farther_station_open = false;
    auto context = f.Context();
    context.can_stage = [&](c::NodeId child, Vec3 goal) {
        return farther_station_open || child == f.children[0].id ||
               goal.x < mission.intent.objective_position.x;
    };
    const auto first = c::ArmyBrain{}.Think(context);
    REQUIRE(first.value);
    REQUIRE(first.value->subordinate_tasks.size() == 3);
    const auto &center = first.value->subordinate_tasks[0];
    const auto &wing = first.value->subordinate_tasks[1];
    CHECK(center.action == (searches ? c::TaskKind::MoveToArea : c::TaskKind::HoldArea));
    CHECK(wing.action == center.action);
    CHECK_FALSE(center.intent.target_regiment);
    CHECK_FALSE(wing.intent.target_regiment);
    if (!searches)
        return;
    CHECK(center.intent.objective_position.x == mission.intent.objective_position.x);
    CHECK(center.intent.objective_position.y == f.statuses[0].pose.position.y);
    CHECK(wing.intent.objective_position.x > f.statuses[1].pose.position.x);
    CHECK(wing.intent.objective_position.x < center.intent.objective_position.x);
    CHECK(wing.intent.objective_position.y == f.statuses[1].pose.position.y);
    CHECK(first.value->subordinate_tasks.back().action == c::TaskKind::Reserve);

    f.statuses[1].pose.position.y += 3.f;
    f.memory = *first.value->next_memory;
    context.previous_subordinate_tasks = first.value->subordinate_tasks;
    auto repeated = c::ArmyBrain{}.Think(context);
    REQUIRE(repeated.value);
    CHECK(repeated.value->subordinate_tasks[1].intent.objective_position == wing.intent.objective_position);

    f.statuses[1].pose.position.x = wing.intent.objective_position.x;
    farther_station_open = true;
    const auto advances = c::ArmyBrain{}.Think(context);
    REQUIRE(advances.value);
    CHECK(advances.value->subordinate_tasks[1].intent.objective_position.x ==
          mission.intent.objective_position.x);
    CHECK(advances.value->subordinate_tasks[1].intent.objective_position.y ==
          wing.intent.objective_position.y);

    c::KnownContact enemy;
    enemy.regiment = {2};
    enemy.team = 1;
    enemy.allegiance = c::Allegiance::Enemy;
    enemy.estimated_pose.position = {12, 0, 0};
    enemy.estimated_strength = 6.f;
    enemy.confidence = 1.f;
    f.situation.contacts.push_back(enemy);
    const auto contact = c::ArmyBrain{}.Think(context);
    REQUIRE(contact.value);
    CHECK(contact.value->subordinate_tasks.front().action == c::TaskKind::Attack);
    CHECK(contact.value->subordinate_tasks.front().intent.target_regiment == enemy.regiment);
}

TEST_CASE("army_fsm.an_unassigned_attack_respects_its_area_and_target_authority") {
    test::UpperBrainFixture f(c::Level::Army);
    f.children[0].sector_role = c::SectorRole::Center;
    f.children[1].sector_role = c::SectorRole::RightFlank;
    for (auto &child : f.children) {
        child.level = c::Level::Group;
        child.commander = {child.id.value};
    }
    auto &mission = f.tasks.front();
    mission.intent.target_regiment = {};
    mission.intent.objective_position = {20, 0, 0};
    mission.authority.may_pursue = true;
    SUBCASE("without target authority even a visible enemy is unavailable") {
        const auto result = c::ArmyBrain{}.Think(f.Context());
        REQUIRE(result.value);
        CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::HoldArea);
        CHECK_FALSE(result.value->subordinate_tasks[0].intent.target_regiment);
    }
    SUBCASE("a search stays inside its limited area") {
        f.situation.contacts.clear();
        mission.authority.may_change_target = true;
        mission.authority.pursuit_limit_m = 4.f;
        const auto result = c::ArmyBrain{}.Think(f.Context());
        REQUIRE(result.value);
        REQUIRE(result.value->subordinate_tasks.size() == 3);
        CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::MoveToArea);
        CHECK(result.value->subordinate_tasks[1].action == c::TaskKind::HoldArea);
        CHECK(result.value->subordinate_tasks[1].intent.objective_position == f.statuses[1].pose.position);
    }
}

TEST_CASE("army_fsm.a_blocked_search_holds_by_the_formation_instead_of_the_officer") {
    test::UpperBrainFixture f(c::Level::Army);
    f.situation.contacts.clear();
    for (auto &child : f.children) {
        child.level = c::Level::Group;
        child.commander = {child.id.value};
    }
    auto &mission = f.tasks.front();
    mission.intent.target_regiment = {};
    mission.intent.objective_position = {20, 0, 0};
    mission.authority.may_pursue = mission.authority.may_change_target = true;
    const Vec3 formation{4, 0, 0};
    f.statuses[0].formation_center = formation;
    f.statuses[0].pose.position = {-4, 0, 0};
    auto context = f.Context();
    context.can_stage = [](c::NodeId, Vec3) { return false; };
    const auto first = c::ArmyBrain{}.Think(context);
    REQUIRE(first.value);
    const auto &order = first.value->subordinate_tasks.front();
    CHECK(order.action == c::TaskKind::HoldArea);
    CHECK(order.intent.objective_position == formation);
    CHECK_FALSE(order.intent.target_regiment);

    f.memory = *first.value->next_memory;
    f.statuses[0].pose.position.x -= 4.f;
    context.previous_subordinate_tasks = first.value->subordinate_tasks;
    const auto next = c::ArmyBrain{}.Think(context);
    REQUIRE(next.value);
    CHECK(next.value->subordinate_tasks.front().intent.objective_position == formation);
}

TEST_CASE("army_fsm.waiting_wing_takes_support_when_a_route_opens") {
    test::UpperBrainFixture f(c::Level::Army);
    f.children[0].sector_role = c::SectorRole::Center;
    f.children[1].sector_role = c::SectorRole::LeftFlank;
    for (auto &child : f.children) {
        child.level = c::Level::Group;
        child.commander = {child.id.value};
    }
    std::optional<Vec3> reachable;
    const auto think = [&](Tick tick, std::span<const c::CommandTask> previous = {}) {
        auto context = f.Context(tick);
        context.previous_subordinate_tasks = previous;
        context.choose_group_support = [&](c::NodeId, c::NodeId, const c::KnownContact &) {
            return reachable;
        };
        return c::ArmyBrain{}.Think(context);
    };
    const auto waiting = think(0);
    REQUIRE(waiting.value);
    REQUIRE(waiting.value->subordinate_tasks.size() == 3);
    CHECK(waiting.value->subordinate_tasks[1].action == c::TaskKind::Reserve);

    const Vec3 staging{15, 6, 0};
    reachable = staging;
    f.memory = *waiting.value->next_memory;
    const auto supporting = think(1, waiting.value->subordinate_tasks);
    REQUIRE(supporting.value);
    REQUIRE(supporting.value->subordinate_tasks.size() == 3);
    CHECK(supporting.value->subordinate_tasks[1].action == c::TaskKind::Support);
    CHECK(supporting.value->subordinate_tasks[1].intent.objective_position == staging);

    reachable = Vec3{19, 6, 0};
    f.memory = *supporting.value->next_memory;
    const auto committed = think(2, supporting.value->subordinate_tasks);
    REQUIRE(committed.value);
    REQUIRE(committed.value->subordinate_tasks.size() == 3);
    CHECK(committed.value->subordinate_tasks[1].action == c::TaskKind::Support);
    CHECK(committed.value->subordinate_tasks[1].intent.objective_position == staging);
}

namespace {
Scenario ThreeGroups(bool blocked_right) {
    Scenario scenario;
    scenario.rules.local_melee = true;
    if (blocked_right)
        scenario.obstacles.push_back({-3, 3, 26, 24});
    const auto add_regiment = [&](uint64_t id, int team, Vec3 position) {
        RegimentSpec spec;
        spec.id = {id};
        spec.team = team;
        spec.pose = {position, team ? Pi : 0.f};
        spec.formation = {3, 1.1f, .25f};
        spec.speed = 3.f;
        spec.rally_point = {team ? 26.f : -26.f, 0, 0};
        scenario.regiments.push_back(spec);
        for (uint64_t i = 1; i <= 6; ++i) {
            UnitSpec unit;
            unit.id = {id * 100 + i};
            unit.regiment = spec.id;
            unit.pose = spec.pose;
            unit.health = 100.f;
            unit.speed = 3.5f;
            unit.weapon = {.01f, 1.65f, 10, 1};
            scenario.units.push_back(unit);
        }
    };
    add_regiment(1, 0, {-8, 0, 0});
    add_regiment(2, 0, {-12, -12, 0});
    add_regiment(3, 0, {-12, 12, 0});
    add_regiment(4, 1, {8, 0, 0});

    auto &setup = scenario.command_setup.emplace();
    const auto add_officer = [&](uint64_t id, UnitId unit, uint32_t role) {
        scenario.officers.push_back({{id}, unit, role});
        c::OfficerProfile profile;
        profile.officer = {id};
        profile.command_radius_m = 60;
        setup.hierarchy.officers.push_back(profile);
    };
    add_officer(1, {103}, 1);
    c::CommandNode army;
    army.id = {19};
    army.level = c::Level::Army;
    army.commander = {1};
    army.children = {{13}, {14}, {15}};
    setup.hierarchy.nodes.push_back(army);
    const std::array roles{c::SectorRole::Center, c::SectorRole::LeftFlank, c::SectorRole::RightFlank};
    for (uint64_t i = 1; i <= roles.size(); ++i) {
        c::CommandNode group;
        group.id = {i + 12};
        group.parent = army.id;
        group.level = c::Level::Group;
        group.sector_role = roles[i - 1];
        group.commander = {i + 1};
        group.children = {{i}};
        setup.hierarchy.nodes.push_back(group);
        add_officer(i + 1, {i * 100 + 2}, 1);

        c::CommandNode leaf;
        leaf.id = group.children.front();
        leaf.parent = group.id;
        leaf.level = c::Level::Regiment;
        leaf.regiment = {i};
        leaf.commander = {i + 4};
        setup.hierarchy.nodes.push_back(leaf);
        add_officer(i + 4, {i * 100 + 1}, 2);
    }
    c::CommandTask mission;
    mission.id = {1};
    mission.recipient = army.id;
    mission.action = c::TaskKind::Attack;
    mission.intent.target_regiment = {4};
    mission.intent.objective_position = {8, 0, 0};
    mission.authority.initiative = c::InitiativePolicy::WithinIntent;
    mission.authority.may_pursue = true;
    mission.authority.pursuit_limit_m = 30.f;
    setup.missions.push_back(mission);
    return scenario;
}

const c::CommandTask *Assignment(const std::vector<c::CommandTask> &tasks, c::NodeId recipient) {
    const auto found = std::find_if(tasks.begin(), tasks.end(), [&](const auto &task) {
        return task.recipient == recipient;
    });
    return found == tasks.end() ? nullptr : &*found;
}
} // namespace

TEST_CASE("army_fsm.general_pins_with_center_and_delivers_distinct_flank_routes") {
    auto created = CreateWorld(ThreeGroups(false), 42);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService navigation({}, 1.f, 65, 57, {-32, -28, 0});
    REQUIRE(Step(world, {}, {}, navigation));

    const auto &army = world.command_state->nodes.back();
    const auto *center = Assignment(army.sent_tasks, {13});
    const auto *left = Assignment(army.sent_tasks, {14});
    const auto *right = Assignment(army.sent_tasks, {15});
    REQUIRE(center);
    REQUIRE(left);
    REQUIRE(right);
    CHECK(center->action == c::TaskKind::Attack);
    CHECK(center->intent.target_regiment == RegimentId{4});
    CHECK(left->action == c::TaskKind::Flank);
    CHECK(right->action == c::TaskKind::Flank);
    REQUIRE(left->intent.flank_waypoint);
    REQUIRE(right->intent.flank_waypoint);
    CHECK(left->intent.flank_waypoint->position.y < -2.f);
    CHECK(right->intent.flank_waypoint->position.y > 2.f);
    const auto left_id = left->id;
    const auto right_id = right->id;

    bool center_engaged = false, left_engaged = false, right_engaged = false;
    for (int i = 0; i < 260; ++i) {
        REQUIRE(Step(world, {}, {}, navigation));
        center_engaged |= world.regiments[0].melee_engaged &&
                          world.regiments[0].ai_target == RegimentId{4};
        left_engaged |= world.regiments[1].melee_engaged &&
                        world.regiments[1].ai_action == OrderType::Attack &&
                        world.regiments[1].ai_target == RegimentId{4} &&
                        world.regiments[1].pose.position.y < -2.f;
        right_engaged |= world.regiments[2].melee_engaged &&
                         world.regiments[2].ai_action == OrderType::Attack &&
                         world.regiments[2].ai_target == RegimentId{4} &&
                         world.regiments[2].pose.position.y > 2.f;
    }
    CHECK(center_engaged);
    CHECK(left_engaged);
    CHECK(right_engaged);
    REQUIRE(Assignment(army.sent_tasks, {14}));
    REQUIRE(Assignment(army.sent_tasks, {15}));
    CHECK(Assignment(army.sent_tasks, {14})->id.value == left_id.value);
    CHECK(Assignment(army.sent_tasks, {15})->id.value == right_id.value);
    CHECK(Restore(Capture(world)));
}

TEST_CASE("army_fsm.general_keeps_blocked_wing_in_support_and_uses_open_wing") {
    const auto scenario = ThreeGroups(true);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService navigation(scenario.obstacles, 1.f, 65, 57, {-32, -28, 0});
    REQUIRE(Step(world, {}, {}, navigation));
    const auto &army = world.command_state->nodes.back();
    REQUIRE(Assignment(army.sent_tasks, {13}));
    CHECK(Assignment(army.sent_tasks, {13})->action == c::TaskKind::Attack);
    REQUIRE(Assignment(army.sent_tasks, {14}));
    CHECK(Assignment(army.sent_tasks, {14})->action == c::TaskKind::Flank);
    const auto *right = Assignment(army.sent_tasks, {15});
    REQUIRE(right);
    CHECK(right->action == c::TaskKind::Support);
    CHECK(right->intent.target_command == c::NodeId{13});
    CHECK_FALSE(right->intent.flank_waypoint);
    for (int i = 0; i < 120; ++i)
        REQUIRE(Step(world, {}, {}, navigation));
    CHECK(world.regiments[2].pose.position.y < 10.f);
}

TEST_CASE("army_fsm.six_regiments_per_side_contest_all_three_fronts") {
    Scenario scenario;
    scenario.rules.local_melee = true;
    bool local_attacks = true;
    SUBCASE("the mission authorizes engagement in all three sectors") {}
    SUBCASE("wings hold sectors outside the center pursuit authority") {
        local_attacks = false;
    }
    auto &setup = scenario.command_setup.emplace();
    for (const auto &[low, high] : std::array<std::pair<float, float>, 4>{{
             {-36.f, -26.f}, {-7.f, -6.f}, {5.f, 6.f}, {26.f, 36.f}}})
        scenario.obstacles.push_back({-2.f, low, 2.f, high, false, TerrainFeature::River});
    const std::array<float, 6> frontage{-22.f, -17.f, -3.f, 3.f, 17.f, 22.f};
    const std::array roles{c::SectorRole::LeftFlank, c::SectorRole::Center, c::SectorRole::RightFlank};
    const auto add_officer = [&](uint64_t id, UnitId unit, uint32_t role) {
        scenario.officers.push_back({{id}, unit, role});
        c::OfficerProfile profile;
        profile.officer = {id};
        profile.command_radius_m = 75.f;
        setup.hierarchy.officers.push_back(profile);
    };
    for (uint64_t team = 0; team < 2; ++team) {
        const uint64_t first = 1 + team * frontage.size();
        const uint64_t first_group = 21 + team * roles.size();
        c::CommandNode army;
        army.id = {31 + team};
        army.level = c::Level::Army;
        army.team = int(team);
        army.commander = {army.id.value};
        add_officer(army.id.value, {(first + 2) * 100 + 3}, 1);
        for (size_t lane = 0; lane < roles.size(); ++lane) {
            c::CommandNode group;
            group.id = {first_group + lane};
            group.level = c::Level::Group;
            group.team = army.team;
            group.parent = army.id;
            group.sector_role = roles[team ? roles.size() - 1 - lane : lane];
            group.commander = {group.id.value};
            army.children.push_back(group.id);
            add_officer(group.id.value, {(first + lane * 2) * 100 + 2}, 1);
            for (size_t rank = 0; rank < 2; ++rank) {
                const size_t slot = lane * 2 + rank;
                const RegimentId id{first + slot};
                RegimentSpec regiment;
                regiment.id = id;
                regiment.team = army.team;
                regiment.pose = {{team ? 15.f : -15.f, frontage[slot], 0}, team ? Pi : 0.f};
                regiment.formation = {4, 1.f, .25f};
                regiment.rally_point = regiment.pose.position;
                scenario.regiments.push_back(regiment);
                for (uint64_t i = 1; i <= 8; ++i) {
                    UnitSpec unit;
                    unit.id = {id.value * 100 + i};
                    unit.regiment = id;
                    unit.pose = regiment.pose;
                    unit.health = 100.f;
                    unit.weapon = {.01f, 1.65f, 10, 1};
                    scenario.units.push_back(unit);
                }
                c::CommandNode leaf;
                leaf.id = {id.value};
                leaf.parent = group.id;
                leaf.level = c::Level::Regiment;
                leaf.team = group.team;
                leaf.regiment = id;
                leaf.commander = {id.value};
                add_officer(id.value, {id.value * 100 + 1}, 2);
                group.children.push_back(leaf.id);
                setup.hierarchy.nodes.push_back(leaf);
            }
            setup.hierarchy.nodes.push_back(group);
        }
        c::CommandTask mission;
        mission.id = {1 + team};
        mission.recipient = army.id;
        mission.action = c::TaskKind::Attack;
        mission.intent.target_regiment = {team ? 3u : 9u};
        mission.intent.objective_position = {team ? -15.f : 15.f, frontage[2], 0};
        mission.authority.initiative = c::InitiativePolicy::WithinIntent;
        mission.authority.may_change_target = true;
        mission.authority.may_pursue = true;
        mission.authority.pursuit_limit_m = local_attacks ? 30.f : 12.f;
        setup.missions.push_back(mission);
        setup.hierarchy.nodes.push_back(army);
    }
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    GridNavigationService navigation(scenario.obstacles, 1.f, 81, 73, {-40.f, -36.f, 0});
    REQUIRE(Step(world, {}, {}, navigation));

    const auto runtime = [&](c::NodeId node) {
        return std::ranges::find(world.command_state->nodes, node, &c::NodeRuntime::node);
    };
    for (uint64_t team = 0; team < 2; ++team) {
        const auto army = runtime({31 + team});
        REQUIRE(army != world.command_state->nodes.end());
        REQUIRE(army->sent_tasks.size() == 3);
        for (size_t lane = 0; lane < roles.size(); ++lane) {
            const auto *order = Assignment(army->sent_tasks, {21 + team * roles.size() + lane});
            REQUIRE(order);
            CAPTURE(team);
            CAPTURE(lane);
            if (!local_attacks && lane != 1) {
                CHECK(order->action == c::TaskKind::HoldArea);
                CHECK_FALSE(order->intent.target_regiment);
                continue;
            }
            CHECK(order->action == c::TaskKind::Attack);
            const uint64_t first_enemy = 1 + (1 - team) * frontage.size() + lane * 2;
            CHECK(order->intent.target_regiment.value >= first_enemy);
            CHECK(order->intent.target_regiment.value < first_enemy + 2);
            if (lane == 1)
                CHECK(order->intent.target_regiment == setup.missions[team].intent.target_regiment);
        }
    }
    for (int tick = 0; tick < 70; ++tick)
        REQUIRE(Step(world, {}, {}, navigation));
    for (uint64_t team = 0; team < 2; ++team)
        for (size_t lane = 0; lane < roles.size(); ++lane) {
            const auto group = runtime({21 + team * roles.size() + lane});
            REQUIRE(group != world.command_state->nodes.end());
            REQUIRE(group->sent_tasks.size() == 2);
            const uint64_t first_enemy = 1 + (1 - team) * frontage.size() + lane * 2;
            if (local_attacks || lane == 1) {
                CHECK(std::ranges::any_of(group->sent_tasks, [&](const auto &order) {
                    return order.action == c::TaskKind::Attack &&
                           order.intent.target_regiment.value >= first_enemy &&
                           order.intent.target_regiment.value < first_enemy + 2;
                }));
            } else {
                CHECK(std::ranges::all_of(group->sent_tasks, [](const auto &order) {
                    return order.action == c::TaskKind::HoldArea && !order.intent.target_regiment;
                }));
            }
            const float center_y = (frontage[lane * 2] + frontage[lane * 2 + 1]) * .5f;
            for (const auto &order : group->sent_tasks) {
                CAPTURE(team);
                CAPTURE(lane);
                CHECK(std::abs(order.intent.objective_position.y - center_y) < 10.f);
            }
        }
}
