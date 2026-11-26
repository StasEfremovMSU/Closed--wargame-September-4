#include "battle/command/GroupPolicy.h"
#include "CommandSupport.h"
#include "doctest.h"
using namespace battle;
namespace c = battle::command;
TEST_CASE("group_fsm.deploys_then_attacks") {
    test::UpperBrainFixture f;
    auto result = c::GroupBrain{}.Think(f.Context());
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    REQUIRE(result.value->next_memory);
    CHECK(result.value->next_memory->state == c::FsmState::Deploying);
    REQUIRE(result.value->subordinate_tasks.size() == 3);
    CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::MoveToArea);
    CHECK(result.value->subordinate_tasks[2].action == c::TaskKind::Reserve);
    for (size_t i = 0; i < 3; ++i)
        f.statuses[i].pose.position = result.value->subordinate_tasks[i].intent.objective_position;
    f.memory = *result.value->next_memory;
    result = c::GroupBrain{}.Think(f.Context(1));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Attacking);
    CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::Attack);
    CHECK(result.value->subordinate_tasks[1].action == c::TaskKind::Support);
    CHECK(result.value->subordinate_tasks[1].intent.target_regiment.value == 100);
}
TEST_CASE("group_fsm.reserve_support_and_withdraw") {
    test::UpperBrainFixture f;
    f.memory.state = c::FsmState::Deploying;
    f.memory.active_task = {1};
    f.memory.task_status = c::TaskStatus::Active;
    f.statuses[0].panic = true;
    auto result = c::GroupBrain{}.Think(f.Context(41));
    REQUIRE(result.status == c::StubStatus::Ready);
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks[2].action != c::TaskKind::Reserve);
    f.tasks[0].id = {2};
    f.tasks[0].action = c::TaskKind::Withdraw;
    result = c::GroupBrain{}.Think(f.Context(42));
    REQUIRE(result.value);
    for (auto &t : result.value->subordinate_tasks)
        CHECK(t.action == c::TaskKind::Withdraw);
    f.tasks[0].intent.target_regiment = {100};
    f.tasks[0].id = {3};
    f.tasks[0].action = c::TaskKind::Attack;
    result = c::GroupBrain{}.Think(f.Context(43));
    REQUIRE(result.value);
    CHECK(result.value->next_memory->state == c::FsmState::Failed);
}
TEST_CASE("group_fsm.empty_and_reversed_children") {
    test::UpperBrainFixture f;
    auto first = c::GroupBrain{}.Think(f.Context());
    REQUIRE(first.value);
    std::reverse(f.children.begin(), f.children.end());
    auto reverse = c::GroupBrain{}.Think(f.Context());
    REQUIRE(reverse.value);
    CHECK(reverse.value->subordinate_tasks == first.value->subordinate_tasks);
    f.children.clear();
    f.statuses.clear();
    f.node.children.clear();
    auto empty = c::GroupBrain{}.Think(f.Context());
    REQUIRE(empty.value);
    CHECK(empty.value->next_memory->state == c::FsmState::Failed);
    CHECK(empty.value->subordinate_tasks.empty());
}
TEST_CASE("group_fsm.commitment_delays_child_attack") {
    test::UpperBrainFixture f;
    f.profile.minimum_commitment_ticks = 100;
    auto deploy = c::GroupBrain{}.Think(f.Context());
    REQUIRE(deploy.value);
    f.memory = *deploy.value->next_memory;
    for (size_t i = 0; i < 3; ++i)
        f.statuses[i].pose.position = deploy.value->subordinate_tasks[i].intent.objective_position;
    auto wait = c::GroupBrain{}.Think(f.Context(1));
    REQUIRE(wait.value);
    CHECK(wait.value->next_memory->state == c::FsmState::Deploying);
    CHECK(wait.value->subordinate_tasks[0].action == c::TaskKind::MoveToArea);
}
TEST_CASE("group_fsm.preserves_assigned_frontage_and_flank") {
    test::UpperBrainFixture f;
    f.tasks[0].intent.objective_position = {30, 20, 0};
    auto plan = c::GroupBrain{}.Think(f.Context());
    REQUIRE(plan.value);
    CHECK(plan.value->next_memory->goal.y > 5);
    f.tasks[0].action = c::TaskKind::Flank;
    f.memory = *plan.value->next_memory;
    plan = c::GroupBrain{}.Think(f.Context(41));
    REQUIRE(plan.value);
    CHECK(plan.value->subordinate_tasks[0].action == c::TaskKind::Flank);
}

TEST_CASE("group_fsm.nearby_enemy_does_not_send_the_lead_back_to_assemble") {
    test::UpperBrainFixture f;
    f.statuses[0].pose.position = {24, 0, 0};
    f.statuses[1].pose.position = {10, 6, 0};
    f.statuses[2].pose.position = {5, 12, 0};
    auto result = c::GroupBrain{}.Think(f.Context());
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::Attack);
}

TEST_CASE("group_fsm.preplanned_flank_starts_without_waiting_for_the_group_to_assemble") {
    test::UpperBrainFixture f;
    f.tasks[0].action = c::TaskKind::Flank;
    f.tasks[0].intent.flank_waypoint = Pose{{30, 15, 0}, -Pi / 2};
    auto result = c::GroupBrain{}.Think(f.Context());
    REQUIRE(result.value);
    const auto &lead = result.value->subordinate_tasks[0];
    CHECK(lead.action == c::TaskKind::Flank);
    CHECK(lead.intent.flank_waypoint == f.tasks[0].intent.flank_waypoint);
}

TEST_CASE("group_fsm.assembly_lanes_stay_fixed_as_the_group_and_enemy_move") {
    test::UpperBrainFixture f;
    auto initial = c::GroupBrain{}.Think(f.Context());
    REQUIRE(initial.value);
    f.memory = *initial.value->next_memory;
    f.situation.own.pose.position.y = 3;
    f.situation.contacts[0].estimated_pose.position.y = -3;
    auto next = c::GroupBrain{}.Think(f.Context(1));
    REQUIRE(next.value);
    REQUIRE(next.value->subordinate_tasks[1].action == c::TaskKind::MoveToArea);
    CHECK(next.value->subordinate_tasks[0].intent.objective_position ==
          initial.value->subordinate_tasks[0].intent.objective_position);
    CHECK(next.value->subordinate_tasks[1].intent.objective_position ==
          initial.value->subordinate_tasks[1].intent.objective_position);
}

TEST_CASE("group_fsm.assigns_support_a_reachable_second_enemy_when_authorized") {
    test::UpperBrainFixture f;
    f.statuses[0].pose.position = {22, 0, 0};
    f.children[0].commander = {1};
    f.children[1].commander = {2};
    auto secondary = f.situation.contacts.front();
    secondary.regiment = {3};
    secondary.estimated_pose.position = {23, 12, 0};
    f.situation.contacts.push_back(secondary);
    f.tasks[0].authority = {c::InitiativePolicy::WithinIntent, true, false, true, 30.f, {}};
    auto context = f.Context();
    context.can_approach = [](c::NodeId mover, const c::KnownContact &target) {
        return mover.value == 101 && target.regiment.value == 3;
    };
    auto initial = c::GroupBrain{}.Think(context);
    REQUIRE(initial.value);
    CHECK(initial.value->subordinate_tasks[0].intent.target_regiment == RegimentId{2});
    const auto &support = initial.value->subordinate_tasks[1];
    CHECK(support.action == c::TaskKind::Attack);
    CHECK(support.intent.target_regiment == RegimentId{3});

    f.memory = *initial.value->next_memory;
    auto next = f.Context(1);
    next.previous_subordinate_tasks = initial.value->subordinate_tasks;
    next.can_approach = [](c::NodeId, const c::KnownContact &) { return false; };
    f.situation.contacts[1].estimated_pose.position.y += 1.f;
    auto repeat = c::GroupBrain{}.Think(next);
    REQUIRE(repeat.value);
    CHECK(repeat.value->subordinate_tasks[1].action == support.action);
    CHECK(repeat.value->subordinate_tasks[1].intent == support.intent);

    f.tasks[0].authority.may_change_target = false;
    auto restricted = c::GroupBrain{}.Think(next);
    REQUIRE(restricted.value);
    CHECK(restricted.value->subordinate_tasks[1].action == c::TaskKind::Support);
}

TEST_CASE("group_fsm.support_can_start_an_open_flank_before_the_lead_is_engaged") {
    test::UpperBrainFixture f;
    f.statuses[0].pose.position = {22, 0, 0};
    f.children[0].commander = {1};
    f.children[1].commander = {2};
    f.tasks[0].authority = {c::InitiativePolicy::WithinIntent, true, false, false, 30.f, {}};
    const Pose waypoint{{30, 10, 0}, -Pi / 2};
    auto context = f.Context();
    context.choose_flank = [&](c::NodeId mover, RegimentId lead, const c::KnownContact &target,
                                const c::CommandTask &) -> std::optional<Pose> {
        if (mover.value == 101 && !lead && target.regiment.value == 2)
            return waypoint;
        return std::nullopt;
    };
    auto result = c::GroupBrain{}.Think(context);
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks[0].action == c::TaskKind::Attack);
    CHECK(result.value->subordinate_tasks[1].action == c::TaskKind::Flank);
    CHECK(result.value->subordinate_tasks[1].intent.flank_waypoint == waypoint);
}

TEST_CASE("group_fsm.a_small_wing_uses_the_flank_only_when_the_lead_pins_the_observed_enemy") {
    bool pinned = false;
    SUBCASE("the open flank has no confirmed pin") {}
    SUBCASE("the lead has engaged the same enemy") { pinned = true; }
    test::UpperBrainFixture fixture;
    fixture.statuses[0].pose.position = {22, 0, 0};
    fixture.statuses[0].melee_engaged = pinned;
    fixture.statuses[0].attack_target = pinned ? RegimentId{2} : RegimentId{};
    fixture.statuses[1].living = 6;
    fixture.children[0].commander = {1};
    fixture.children[1].commander = {2};
    fixture.tasks[0].authority = {c::InitiativePolicy::WithinIntent, true, false, false, 30.f, {}};
    const Pose flank{{30, 10, 0}, -Pi / 2};
    auto context = fixture.Context();
    unsigned probes = 0;
    context.choose_flank = [&](c::NodeId, RegimentId, const c::KnownContact &,
                               const c::CommandTask &) -> std::optional<Pose> {
        ++probes;
        return flank;
    };
    const auto result = c::GroupBrain{}.Think(context);
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks[1].action == (pinned ? c::TaskKind::Flank : c::TaskKind::Support));
    CHECK(probes == (pinned ? 1u : 0u));
}

TEST_CASE("group_fsm.keeps_a_safe_support_assignment_when_the_lead_moves") {
    test::UpperBrainFixture f;
    f.statuses[0].pose.position = {22, 0, 0};
    auto initial = c::GroupBrain{}.Think(f.Context());
    REQUIRE(initial.value);
    const auto &support = initial.value->subordinate_tasks[1];
    CHECK(support.action == c::TaskKind::Support);
    CHECK(support.intent.target_command == f.children[0].id);
    CHECK(support.intent.objective_position == f.statuses[1].pose.position);

    f.statuses[0].pose.position = {26, -2, 0};
    f.statuses[1].pose.position = {2, 6, 0};
    f.situation.contacts[0].estimated_pose.position = {26, -8, 0};
    f.memory = *initial.value->next_memory;
    auto context = f.Context(1);
    context.previous_subordinate_tasks = initial.value->subordinate_tasks;
    auto next = c::GroupBrain{}.Think(context);
    REQUIRE(next.value);
    CHECK(next.value->subordinate_tasks[1].intent == support.intent);
    CHECK(next.value->subordinate_tasks[0].intent == initial.value->subordinate_tasks[0].intent);
}

TEST_CASE("group_fsm.passes_on_an_authorized_preplanned_flank_outside_its_own_sight") {
    test::UpperBrainFixture f;
    f.tasks[0].action = c::TaskKind::Flank;
    f.tasks[0].intent.flank_waypoint = Pose{{30, 15, 0}, -Pi / 2};
    f.tasks[0].authority.may_pursue = true;
    f.situation.contacts.clear();
    auto result = c::GroupBrain{}.Think(f.Context());
    REQUIRE(result.value);
    const auto &lead = result.value->subordinate_tasks[0];
    CHECK(lead.action == c::TaskKind::Flank);
    CHECK(lead.intent.flank_waypoint == f.tasks[0].intent.flank_waypoint);
    CHECK(lead.intent.target_regiment == RegimentId{2});
}

TEST_CASE("group_fsm.wing_support_takes_a_reachable_spaced_staging_point") {
    test::UpperBrainFixture f;
    f.tasks[0].action = c::TaskKind::Flank;
    f.tasks[0].intent.flank_waypoint = Pose{{26, 20, 0}, 0};
    const auto context = [&] {
        auto result = f.Context();
        result.can_stage = [](c::NodeId id, Vec3 point) {
            return id.value == 101 && point.x > 8.f;
        };
        return result;
    }();
    const auto result = c::GroupBrain{}.Think(context);
    REQUIRE(result.value);
    const auto &support = result.value->subordinate_tasks[1];
    CHECK(support.action == c::TaskKind::Support);
    CHECK(support.intent.objective_position.x > 8.f);
    CHECK(support.intent.objective_position.x < 26.f);
    CHECK(Distance(support.intent.objective_position, f.tasks[0].intent.flank_waypoint->position) >
          f.statuses[1].width_m);
}

TEST_CASE("group_fsm.keeps_the_exact_support_goal_for_the_group_lead") {
    test::UpperBrainFixture f;
    f.tasks[0].action = c::TaskKind::Support;
    f.tasks[0].intent.target_command = {200};
    f.tasks[0].intent.objective_position = {24, 0, 0};
    auto context = f.Context();
    context.can_stage = [](c::NodeId node, Vec3 point) {
        return node.value == 101 && point.y >= 6.f;
    };
    const auto result = c::GroupBrain{}.Think(context);
    REQUIRE(result.value);
    const auto &lead = result.value->subordinate_tasks[0];
    CHECK(lead.action == c::TaskKind::Support);
    CHECK(lead.intent.objective_position == f.tasks[0].intent.objective_position);
    const auto &support = result.value->subordinate_tasks[1];
    CHECK(support.action == c::TaskKind::Support);
    CHECK(support.intent.objective_position.y >= 6.f);
    CHECK(support.intent.objective_position.x > 3.f);
    CHECK(Distance(support.intent.objective_position, lead.intent.objective_position) >
          f.statuses[1].width_m);
}

TEST_CASE("group_fsm.keeps_the_army_assigned_center_target_when_another_enemy_appears") {
    test::UpperBrainFixture f;
    f.statuses[0].pose.position = {22, 0, 0};
    f.children[0].commander = {1};
    f.children[1].commander = {2};
    f.tasks[0].authority = {c::InitiativePolicy::WithinIntent, true, false, true, 30.f, {}};
    auto secondary = f.situation.contacts.front();
    secondary.regiment = {3};
    secondary.estimated_pose.position = {23, 12, 0};
    f.situation.contacts.push_back(secondary);
    auto army_context = f.Context();
    army_context.parent_level = c::Level::Army;
    army_context.can_approach = [](c::NodeId node, const c::KnownContact &enemy) {
        return node.value == 101 && enemy.regiment.value == 3;
    };

    auto established = c::GroupBrain{}.Think(army_context);
    REQUIRE(established.value);
    const auto &lead = established.value->subordinate_tasks[0];
    CHECK(lead.action == c::TaskKind::Attack);
    CHECK(lead.intent.target_regiment == RegimentId{2});
    CHECK_FALSE(lead.authority.may_change_target);
    CHECK(established.value->subordinate_tasks[1].intent.target_regiment == RegimentId{3});

    f.memory = *established.value->next_memory;
    const auto assigned = f.situation.contacts.front();
    f.situation.contacts.front().source = c::ObservationSource::Memory;
    auto memory_context = f.Context(1);
    memory_context.parent_level = c::Level::Army;
    auto occluded = c::GroupBrain{}.Think(memory_context);
    REQUIRE(occluded.value);
    CHECK(occluded.value->subordinate_tasks[0].intent.target_regiment == assigned.regiment);

    f.situation.contacts.erase(f.situation.contacts.begin());
    auto missing_context = f.Context(2);
    missing_context.parent_level = c::Level::Army;
    auto missing = c::GroupBrain{}.Think(missing_context);
    REQUIRE(missing.value);
    CHECK(missing.value->next_memory->state == c::FsmState::Regrouping);
    CHECK(missing.value->subordinate_tasks[0].action == c::TaskKind::HoldArea);

    missing_context.parent_level = c::Level::Part;
    auto local = c::GroupBrain{}.Think(missing_context);
    REQUIRE(local.value);
    CHECK(local.value->subordinate_tasks[0].intent.target_regiment == RegimentId{3});
}

TEST_CASE("group_fsm.policy_can_choose_only_a_validated_second_regiment_action") {
    test::UpperBrainFixture f;
    f.children[0].commander = {10};
    f.children[1].commander = {11};
    f.statuses[2].living = 0;
    f.tasks[0].authority.initiative = c::InitiativePolicy::WithinIntent;
    f.tasks[0].authority.may_pursue = true;
    f.tasks[0].authority.may_change_target = true;
    f.statuses[0].melee_engaged = true;
    f.statuses[0].attack_target = {2};
    auto alternate = f.situation.contacts.front();
    alternate.regiment = {3};
    alternate.estimated_pose.position.y = 8;
    f.situation.contacts.push_back(alternate);

    struct PreferFlank final : c::ITacticalEvaluator {
        mutable unsigned calls{};
        c::StubResult<std::vector<c::ActionScore>> Evaluate(const c::EvaluationRequest &r) const override {
            ++calls;
            REQUIRE(r.level == c::Level::Group);
            REQUIRE(r.options.size() >= 2);
            CHECK(r.features.size() % r.options.size() == 0);
            std::vector<c::ActionScore> scores;
            for (uint32_t i = 0; i < r.options.size(); ++i) {
                CHECK(r.options[i].recipient == c::NodeId{101});
                scores.push_back({i, r.options[i].action == c::TaskKind::Flank ? 2.f : 0.f, {}});
            }
            return {c::StubStatus::Ready, std::move(scores), {}};
        }
    } evaluator;

    bool route_open = true;
    SUBCASE("an open flank can outrank the default alternate attack") {}
    SUBCASE("a closed flank cannot be selected") { route_open = false; }
    auto context = f.Context(1);
    context.can_approach = [](auto, const auto &) { return true; };
    context.choose_flank = [route_open](auto, auto, const auto &, const auto &) -> std::optional<Pose> {
        return route_open ? std::optional<Pose>{Pose{{20, 9, 0}, 0}} : std::nullopt;
    };
    const auto baseline = c::GroupBrain{}.Think(context);
    REQUIRE(baseline.value);
    REQUIRE(baseline.value->subordinate_tasks.size() >= 2);
    CHECK(baseline.value->subordinate_tasks[1].action == c::TaskKind::Attack);
    auto policy_context = context;
    policy_context.group_policy = &evaluator;
    const auto chosen = c::GroupBrain{}.Think(policy_context);
    REQUIRE(chosen.value);
    CHECK(evaluator.calls == 1);
    REQUIRE(chosen.value->subordinate_tasks.size() >= 2);
    CHECK(chosen.value->subordinate_tasks[0] == baseline.value->subordinate_tasks[0]);
    CHECK(chosen.value->subordinate_tasks[1].action ==
          (route_open ? c::TaskKind::Flank : c::TaskKind::Attack));
}
TEST_CASE("group_fsm.commitment_preserves_the_exact_live_assignment_until_execution") {
    test::UpperBrainFixture f;
    f.children[0].commander = {10};
    f.children[1].commander = {11};
    f.statuses[2].living = 0;
    f.statuses[0].melee_engaged = true;
    f.statuses[0].attack_target = {2};
    f.tasks[0].authority.initiative = c::InitiativePolicy::WithinIntent;
    f.tasks[0].authority.may_pursue = true;
    f.tasks[0].authority.may_change_target = true;
    auto alternate = f.situation.contacts.front();
    alternate.regiment = {3};
    alternate.estimated_pose.position.y = 8;
    f.situation.contacts.push_back(alternate);
    auto context = f.Context(100);
    context.can_approach = [](auto, const auto &) { return true; };
    context.can_stage = [](auto, auto) { return true; };
    context.choose_flank = [](auto, auto, const auto &, const auto &) -> std::optional<Pose> {
        return Pose{{20, 9, 0}, 0};
    };
    const auto initial = c::GroupBrain{}.Think(context);
    REQUIRE(initial.value);
    CHECK(initial.value->subordinate_tasks[1].action == c::TaskKind::Attack);
    f.memory = *initial.value->next_memory;

    auto previous = initial.value->subordinate_tasks[1];
    previous.id = {9};
    previous.created_tick = 92;
    previous.status = c::TaskStatus::Active;
    previous.action = c::TaskKind::Support;
    previous.intent.target_command = f.children[0].id;
    previous.intent.target_regiment = f.children[0].regiment;
    context.previous_subordinate_tasks = std::span{&previous, 1};

    struct PreferKeep final : c::ITacticalEvaluator {
        mutable unsigned calls{};
        c::StubResult<std::vector<c::ActionScore>> Evaluate(const c::EvaluationRequest &request) const override {
            ++calls;
            CHECK(request.feature_schema == c::GroupPolicyCommitmentFeatureSchema);
            CHECK(request.features.size() ==
                  c::GroupPolicyCommitmentFeatureNames.size() * request.options.size());
            std::vector<c::ActionScore> scores;
            bool found_keep = false;
            for (uint32_t i = 0; i < request.options.size(); ++i) {
                const bool keep = request.options[i].keep_current_order;
                found_keep |= keep;
                scores.push_back({i, keep ? 2.f : 0.f, {}});
            }
            CHECK(found_keep);
            return {c::StubStatus::Ready, std::move(scores), {}};
        }
    } evaluator;

    context.group_order_commitment = true;
    c::ChildOrderObservation observation{true, false, std::nullopt, 0.f};
    context.observe_order = [&](const auto &) { return std::optional{observation}; };
    auto expected = previous;
    unsigned calls = 0;

    SUBCASE("rule alone lets the message arrive") {}
    SUBCASE("the model cannot bypass the initial execution window") {
        context.group_policy = &evaluator;
        observation = {false, true, 98, .1f};
    }
    SUBCASE("the model can select the exact assignment after the window") {
        context.group_policy = &evaluator;
        previous.created_tick = 0;
        observation = {false, true, 5, .2f};
        expected = previous;
        calls = 1;
    }
    SUBCASE("the guard releases an expired or blocked assignment") {
        context.can_stage = [](auto, auto) { return false; };
        auto replacement = c::GroupBrain{}.Think(context);
        REQUIRE(replacement.value);
        CHECK(replacement.value->subordinate_tasks[1].action == c::TaskKind::Attack);
        return;
    }
    SUBCASE("blocked previous support does not seed a new support location") {
        previous.intent.objective_position = {-80, -20, 0};
        context.can_approach = [](auto, const auto &) { return false; };
        context.choose_flank = {};
        context.can_stage = [&](auto, auto goal) { return goal != previous.intent.objective_position; };
        auto replacement = c::GroupBrain{}.Think(context);
        REQUIRE(replacement.value);
        CHECK(replacement.value->subordinate_tasks[1].action == c::TaskKind::Support);
        CHECK(replacement.value->subordinate_tasks[1].intent.objective_position !=
              previous.intent.objective_position);
        return;
    }
    SUBCASE("a new parent assignment supersedes a pending order to the same enemy") {
        f.tasks[0].id = {2};
        f.tasks[0].intent.objective_position.x += 1.f;
        auto replacement = c::GroupBrain{}.Think(context);
        REQUIRE(replacement.value);
        CHECK(replacement.value->subordinate_tasks[1].parent_task == c::TaskId{2});
        CHECK(replacement.value->subordinate_tasks[1].id != previous.id);
        return;
    }
    SUBCASE("panic overrides commitment immediately") {
        f.statuses[1].panic = true;
        auto replacement = c::GroupBrain{}.Think(context);
        REQUIRE(replacement.value);
        CHECK(replacement.value->subordinate_tasks[1].action == c::TaskKind::Rally);
        return;
    }
    const auto result = c::GroupBrain{}.Think(context);
    REQUIRE(result.value);
    CHECK(result.value->subordinate_tasks[1] == expected);
    CHECK(evaluator.calls == calls);
}
