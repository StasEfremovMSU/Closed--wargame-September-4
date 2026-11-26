#include "TestSupport.h"
#include "battle/Formation.h"
#include "battle/Snapshot.h"
#include "battle/command/Services.h"
#include "doctest.h"
using namespace battle;
namespace c = battle::command;
namespace {
Scenario BridgeheadFlank(bool blocked, bool full_regiment = false) {
    Scenario scenario;
    scenario.rules.local_melee = true;
    // The main lines meet on the approach to the bridge. Its impassable sides are
    // behind the enemy; the other blue regiment is on the open right bank.
    const float east_edge = blocked ? 12.f : 5.f;
    scenario.obstacles = {{-5.f, 4.f, east_edge, 18.f}, {-5.f, -18.f, east_edge, -4.f}};
    if (full_regiment)
        scenario.obstacles = {{-5.f, -38.f, 5.f, -28.f}, {-5.f, -20.f, 5.f, -4.f},
                              {-5.f, 4.f, 5.f, 20.f}, {-5.f, 28.f, 5.f, 38.f}};
    auto regiment = [&](uint64_t id, int team, Pose pose) {
        RegimentSpec spec;
        spec.id = {id};
        spec.team = team;
        spec.pose = pose;
        spec.formation = {full_regiment ? 7u : 4u, 1.1f, .25f};
        spec.rally_point = team == 0 ? Vec3{24, 0, 0} : Vec3{-24, 0, 0};
        spec.speed = 3.f;
        scenario.regiments.push_back(spec);
        const uint64_t count = full_regiment ? 50 : 12;
        for (uint64_t j = 1; j <= count; ++j) {
            UnitSpec unit;
            unit.id = {id * 100 + j};
            unit.regiment = spec.id;
            unit.pose = pose;
            unit.health = 100.f;
            unit.speed = 3.5f;
            unit.weapon = {id == 3 ? 4.f : .02f, 1.65f, 10, 1};
            scenario.units.push_back(unit);
        }
    };
    if (full_regiment) {
        regiment(1, 0, {{-13.5f, 24.f, 0}, 0});
        regiment(2, 1, {{-4.2f, 24.f, 0}, Pi});
        regiment(3, 0, {{-16.5f, 12.f, 0}, 0});
    } else {
        regiment(1, 0, {{9.3f, 0, 0}, Pi});
        regiment(2, 1, {{5.f, 0, 0}, 0});
        regiment(3, 0, {{15.f, -8.f, 0}, Pi});
    }

    auto &setup = scenario.command_setup.emplace();
    scenario.officers = {{{1}, {101}, 2}, {{2}, {301}, 2}, {{3}, {102}, 1}};
    c::CommandNode group;
    group.id = {10};
    group.level = c::Level::Group;
    group.commander = {3};
    group.children = {{11}, {12}};
    setup.hierarchy.nodes.push_back(group);
    for (uint64_t i = 1; i <= 2; ++i) {
        c::CommandNode child;
        child.id = {10 + i};
        child.parent = group.id;
        child.level = c::Level::Regiment;
        child.commander = {i};
        child.regiment = {i == 1 ? 1u : 3u};
        setup.hierarchy.nodes.push_back(child);
    }
    for (uint64_t i = 1; i <= 3; ++i) {
        c::OfficerProfile profile;
        profile.officer = {i};
        profile.command_radius_m = 60;
        setup.hierarchy.officers.push_back(profile);
    }
    c::CommandTask task;
    task.id = {1};
    task.recipient = group.id;
    task.action = c::TaskKind::Attack;
    task.intent.target_regiment = {2};
    task.intent.objective_position = scenario.regiments[1].pose.position;
    task.authority.initiative = c::InitiativePolicy::WithinIntent;
    task.authority.may_pursue = true;
    task.authority.pursuit_limit_m = 30.f;
    task.status = c::TaskStatus::Active;
    setup.missions.push_back(task);
    return scenario;
}
} // namespace

TEST_CASE("runtime.bridgehead_uses_an_open_regiment_flank_and_commits_to_the_attack") {
    bool full_regiment = false;
    SUBCASE("small formations on the east bank") {}
    SUBCASE("full regiments at the north bridge") { full_regiment = true; }
    const auto scenario = BridgeheadFlank(false, full_regiment);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    const GridNavigationService nav(scenario.obstacles, 1.f, 81, 77, {-40, -38, 0});
    bool lead_pins = false, flank_task = false, flank_hit = false;
    for (int i = 0; i < 290 && !flank_hit; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        const auto &lead = world.regiments[0];
        const auto &flanker = world.regiments[2];
        lead_pins |= lead.melee_engaged && lead.ai_target == RegimentId{2};
        const auto &group_orders = world.command_state->nodes.front().sent_tasks;
        flank_task |= std::any_of(group_orders.begin(), group_orders.end(), [](const auto &task) {
            return task.recipient.value == 12 && task.action == c::TaskKind::Flank &&
                   task.intent.target_regiment == RegimentId{2};
        });
        CHECK_FALSE(RectanglesOverlap(lead.pose, Footprint(lead), flanker.pose, Footprint(flanker)));
        for (const auto &event : world.events)
            if (event.kind == EventKind::Hit && !event.projectile &&
                event.source.value >= 301 && event.source.value < 400 &&
                event.target.value >= 201 && event.target.value < 300)
                flank_hit |= flanker.pose.position.y < (full_regiment ? 21.5f : -2.5f) &&
                             flanker.ai_action == OrderType::Attack &&
                             flanker.ai_target == RegimentId{2} &&
                             event.aspect != AttackAspect::Front;
    }
    CHECK(lead_pins);
    CHECK(flank_task);
    CHECK(flank_hit);
    CHECK(Restore(Capture(world)));
}

TEST_CASE("runtime.bridgehead_keeps_support_when_flanks_are_closed_to_the_formation") {
    const auto scenario = BridgeheadFlank(true);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    const GridNavigationService nav(scenario.obstacles, 1.f, 61, 35, {-30, -17, 0});
    bool supports = false, lead_pins = false;
    for (int i = 0; i < 110; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        lead_pins |= world.regiments[0].melee_engaged &&
                     world.regiments[0].ai_target == RegimentId{2};
        for (const auto &task : world.command_state->nodes.front().sent_tasks)
            if (task.recipient.value == 12) {
                CHECK(task.action != c::TaskKind::Flank);
                supports |= task.action == c::TaskKind::Support &&
                            task.intent.target_regiment == RegimentId{1};
            }
    }
    CHECK(lead_pins);
    CHECK(supports);
}

TEST_CASE("runtime.a_lost_flank_order_does_not_hold_the_wing_on_a_nonexistent_delivery") {
    const auto scenario = BridgeheadFlank(false);
    auto created = CreateWorld(scenario, 42);
    REQUIRE(created);
    auto &world = created.value;
    const GridNavigationService navigation(scenario.obstacles, 1.f, 81, 77, {-40, -38, 0});
    auto &commands = *world.command_state;
    auto &group = commands.nodes.front();
    const auto wing_assignment = [&] {
        return std::ranges::find(group.sent_tasks, c::NodeId{12}, &c::CommandTask::recipient);
    };
    for (int tick = 0; tick < 160; ++tick) {
        REQUIRE(Step(world, {}, {}, navigation));
        const auto order = wing_assignment();
        if (order == group.sent_tasks.end() || order->action != c::TaskKind::Flank)
            continue;
        const auto lost_id = order->id;
        auto message = std::ranges::find_if(commands.communications.messages, [&](const auto &envelope) {
            const auto *task = std::get_if<c::CommandTask>(&envelope.payload);
            return task && task->id == lost_id &&
                (envelope.status == c::DeliveryStatus::Queued || envelope.status == c::DeliveryStatus::InTransit);
        });
        REQUIRE(message != commands.communications.messages.end());
        message->status = c::DeliveryStatus::Lost;
        group.schedule.next_think_tick = world.tick;
        REQUIRE(Step(world, {}, {}, navigation));
        const auto replacement = wing_assignment();
        REQUIRE(replacement != group.sent_tasks.end());
        CHECK(replacement->id != lost_id);
        const auto &wing = commands.nodes.back();
        CHECK(wing.memory.active_task != lost_id);
        return;
    }
    FAIL("the open bridgehead did not send a flank order");
}
