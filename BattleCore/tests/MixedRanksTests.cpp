#include "JsonBindings.h"
#include "TestSupport.h"
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "../src/LocalMelee.h"
#include "doctest.h"

using namespace battle;

namespace {
Scenario MixedRanks(float enemy_x = 15.f) {
    auto scenario = test::Duel(4);
    scenario.rules.regiment_tactics = scenario.rules.local_melee = true;
    scenario.rules.mixed_rank_tactics = true;
    scenario.regiments[0].pose.position = {};
    scenario.regiments[0].formation.columns = 2;
    scenario.regiments[1].pose.position = {enemy_x, -1, 0};
    scenario.regiments[1].formation.columns = 1;
    scenario.units.resize(5);
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.health = 100.f;
    }
    for (auto i : {2u, 3u}) {
        auto &unit = scenario.units[i];
        unit.archer = true;
        unit.ranged_weapon = RangedWeapon::Bow;
        unit.weapon.range = 60.f;
        unit.weapon.damage = 8.f;
    }
    scenario.units[1].troop = UnitTroop::Spearman;
    return scenario;
}

std::optional<BattleEvent> ProjectileFrom(const World &world, UnitId id) {
    std::vector<BattleEvent> events;
    BuildCombatEvents(world, events);
    const auto event = std::ranges::find_if(events, [&](const auto &entry) {
        return entry.projectile && entry.source == id;
    });
    return event == events.end() ? std::nullopt : std::optional(*event);
}

CommandEnvelope RankCommand(const World &world, uint64_t sequence, RankPriority priority) {
    CommandEnvelope envelope{world.tick, sequence, {}};
    envelope.command.kind = CommandKind::SetRankPriority;
    envelope.command.regiment = world.regiments[0].id;
    envelope.command.rank_priority = priority;
    return envelope;
}

void MoveOpponent(World &world, float x) {
    auto &opponent = world.regiments[1];
    opponent.pose.position.x = x;
    auto &unit = *FindUnit(world, opponent.units.front());
    unit.pose = unit.slot = opponent.slots.front() = opponent.pose;
    world.regiments[0].decision_tick = 0;
}
}

TEST_CASE("combat.mixed_regiment_rear_bows_loft_at_midrange_with_reduced_damage") {
    auto created = CreateWorld(MixedRanks(), 2);
    REQUIRE(created);
    auto &world = created.value;
    const auto rear = ProjectileFrom(world, {3});
    REQUIRE(rear);
    CHECK(rear->high_arc);
    CHECK(rear->target == UnitId{5});

    auto &bow = *FindUnit(world, {3});
    auto &sword = *FindUnit(world, {1});
    std::swap(bow.pose, sword.pose);
    std::swap(bow.slot, sword.slot);
    std::swap(bow.slot_index, sword.slot_index);
    const auto direct = ProjectileFrom(world, {3});
    REQUIRE(direct);
    CHECK_FALSE(direct->high_arc);
    CHECK(rear->amount < direct->amount);
}

TEST_CASE("officers.mixed_regiment_places_bows_in_front_at_safe_range") {
    auto created = CreateWorld(MixedRanks(30.f), 2);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    REQUIRE(Step(world, {}, {}, nav));
    const auto &regiment = world.regiments[0];
    CHECK(IsFormationFrontRank(regiment, *FindUnit(world, {3})));
    CHECK(IsFormationFrontRank(regiment, *FindUnit(world, {4})));
    CHECK_FALSE(IsFormationFrontRank(regiment, *FindUnit(world, {1})));
}

TEST_CASE("officers.attached_staff_do_not_make_a_shooter_regiment_mixed") {
    auto scenario = MixedRanks(30.f);
    scenario.officers = {{{1}, {1}, 1}, {{2}, {2}, 3}};
    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    CHECK_FALSE(SupportsRankPriority(world, world.regiments[0]));
    CHECK(SetRegimentRankPriority(world, {1}, RankPriority::MeleeFront) == Status::InvalidInput);
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(world.regiments[0].rank_layout == RankPriority::Automatic);
}

TEST_CASE("officers.mixed_regiment_screens_on_approach_and_waits_for_safe_separation") {
    auto created = CreateWorld(MixedRanks(30.f), 2);
    REQUIRE(created);
    auto &world = created.value;
    auto &regiment = world.regiments[0];
    FlatNavigationService nav;
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.rank_layout == RankPriority::RangedFront);

    MoveOpponent(world, 8.f);
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.rank_layout == RankPriority::MeleeFront);
    CHECK(IsFormationFrontRank(regiment, *FindUnit(world, {1})));
    CHECK(IsFormationFrontRank(regiment, *FindUnit(world, {2})));

    MoveOpponent(world, 13.f);
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.rank_layout == RankPriority::MeleeFront);

    MoveOpponent(world, 24.f);
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.rank_layout == RankPriority::RangedFront);
}

TEST_CASE("officers.following_regiment_applies_queued_rank_orders_after_melee") {
    auto scenario = MixedRanks(30.f);
    UnitSpec officer;
    officer.id = {6};
    officer.regiment = {1};
    officer.pose.position = {4, -8, 0};
    scenario.units.push_back(officer);
    scenario.officers.push_back({{1}, officer.id, 1});
    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    const auto &regiment = world.regiments[0];
    FlatNavigationService nav;
    REQUIRE(AcquireControl(world, officer.id) == Status::Ok);
    REQUIRE(SetRegimentRankPriority(world, regiment.id, RankPriority::MeleeFront) == Status::Ok);
    CommandEnvelope follow{world.tick, 1, {}};
    follow.command.kind = CommandKind::SetRegimentFollowing;
    follow.command.regiment = regiment.id;
    follow.command.unit = officer.id;
    follow.command.follow_enabled = true;
    REQUIRE(Step(world, std::span(&follow, 1), {}, nav));

    MoveOpponent(world, 2.9f);
    REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(regiment.melee_engaged);
    const auto ranged = RankCommand(world, 2, RankPriority::RangedFront);
    REQUIRE(Step(world, std::span(&ranged, 1), {}, nav));
    CHECK(regiment.rank_priority == RankPriority::RangedFront);
    CHECK(regiment.rank_layout == RankPriority::MeleeFront);

    MoveOpponent(world, 30.f);
    REQUIRE(Step(world, {}, {}, nav));
    REQUIRE_FALSE(regiment.melee_engaged);
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.rank_layout == RankPriority::RangedFront);
    CHECK(regiment.following_officer == officer.id);

    const auto melee = RankCommand(world, 3, RankPriority::MeleeFront);
    REQUIRE(Step(world, std::span(&melee, 1), {}, nav));
    REQUIRE(regiment.rank_layout == RankPriority::MeleeFront);
    const auto automatic = RankCommand(world, 4, RankPriority::Automatic);
    REQUIRE(Step(world, std::span(&automatic, 1), {}, nav));
    for (int i = 0; i < 10; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.rank_priority == RankPriority::Automatic);
    CHECK(regiment.rank_layout == RankPriority::RangedFront);
    CHECK(regiment.following_officer == officer.id);
}

TEST_CASE("officers.regiment_chooses_ranks_during_a_shared_route") {
    auto scenario = MixedRanks(30.f);
    RegimentSpec escort;
    escort.id = {3};
    escort.pose.position.y = 7.f;
    scenario.regiments.push_back(escort);
    UnitSpec escort_soldier;
    escort_soldier.id = {6};
    escort_soldier.regiment = escort.id;
    escort_soldier.pose = escort.pose;
    scenario.units.push_back(escort_soldier);
    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    for (const auto id : {RegimentId{1}, escort.id}) {
        Order move;
        move.type = OrderType::MoveTo;
        move.recipient = id;
        move.point = FindRegiment(world, id)->pose.position + Vec3{6.f, 0, 0};
        move.maneuver_group = 7;
        move.facing = 0.f;
        REQUIRE(SubmitOrder(world, move) == Status::Ok);
    }
    for (int i = 0; i < 12; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    REQUIRE(ActiveFormationTranslations(world).size() == 1);
    CHECK(FindRegiment(world, {1})->pose.position.x > .5f);
    CHECK(FindRegiment(world, {1})->rank_layout == RankPriority::RangedFront);
}

TEST_CASE("combat.explicit_rank_order_moves_soldiers_to_posts_and_overrides_automatic_priority") {
    auto scenario = MixedRanks(30.f);
    SUBCASE("two-meter files") {}
    SUBCASE("historical mission files") {
        scenario.regiments[0].formation.spacing_m = 1.1f;
        scenario.regiments[0].formation.body_radius_m = .25f;
    }
    SUBCASE("irregular historical mission files") {
        scenario.regiments[0].formation.spacing_m = 1.1f;
        scenario.regiments[0].formation.body_radius_m = .25f;
        scenario.regiments[0].formation.slot_stagger = .37f;
    }
    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    const auto &regiment = world.regiments[0];
    FlatNavigationService nav;
    const auto ranged = RankCommand(world, 1, RankPriority::RangedFront);
    REQUIRE(Step(world, std::span(&ranged, 1), {}, nav));
    for (int i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(IsAtFormationPost(regiment, *FindUnit(world, {1})));
    CHECK(IsAtFormationPost(regiment, *FindUnit(world, {3})));
    CHECK(FindUnit(world, {3})->pose.position.x > FindUnit(world, {1})->pose.position.x);

    const auto melee = RankCommand(world, 2, RankPriority::MeleeFront);
    REQUIRE(Step(world, std::span(&melee, 1), {}, nav));
    for (int i = 0; i < 60; ++i)
        REQUIRE(Step(world, {}, {}, nav));
    CHECK(regiment.rank_priority == RankPriority::MeleeFront);
    CHECK(regiment.rank_layout == RankPriority::MeleeFront);
    CHECK(IsAtFormationPost(regiment, *FindUnit(world, {1})));
    CHECK(IsAtFormationPost(regiment, *FindUnit(world, {3})));
    CHECK(FindUnit(world, {1})->pose.position.x > FindUnit(world, {3})->pose.position.x);

    const auto automatic = RankCommand(world, 3, RankPriority::Automatic);
    REQUIRE(Step(world, std::span(&automatic, 1), {}, nav));
    CHECK(regiment.rank_priority == RankPriority::Automatic);
    CHECK(regiment.rank_layout == RankPriority::RangedFront);
}

TEST_CASE("combat.protected_bows_keep_loft_penalty_across_open_lanes_and_respect_safety") {
    auto scenario = MixedRanks();
    SUBCASE("gap between the melee files") {
        auto created = CreateWorld(scenario, 2);
        REQUIRE(created);
        auto &world = created.value;
        REQUIRE(SetRegimentRankPriority(world, {1}, RankPriority::MeleeFront) == Status::Ok);
        FindUnit(world, {1})->pose.position.y += .7f;
        const auto attack = ProjectileFrom(world, {3});
        REQUIRE(attack);
        CHECK(attack->high_arc);
        CHECK(attack->amount < 8.f);
    }
    SUBCASE("projectile-blocking cover") {
        scenario.obstacles = {{5, -2, 6, 2, true}};
        auto created = CreateWorld(scenario, 2);
        REQUIRE(created);
        REQUIRE(SetRegimentRankPriority(created.value, {1}, RankPriority::MeleeFront) == Status::Ok);
        CHECK_FALSE(ProjectileFrom(created.value, {3}));
    }
    SUBCASE("muskets behind their own screen") {
        scenario.units[2].ranged_weapon = RangedWeapon::Musket;
        auto created = CreateWorld(scenario, 2);
        REQUIRE(created);
        REQUIRE(SetRegimentRankPriority(created.value, {1}, RankPriority::MeleeFront) == Status::Ok);
        CHECK_FALSE(ProjectileFrom(created.value, {3}));
    }
}

TEST_CASE("combat.detached_screens_exchange_fire_while_their_parent_ranks_are_in_melee") {
    auto scenario = test::Duel(3);
    scenario.rules.local_melee = scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose.position.x = -.4f;
    scenario.regiments[1].pose.position.x = .4f;
    for (auto &unit : scenario.units) {
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
        unit.health = 100.f;
    }
    for (const auto i : {1u, 2u, 4u, 5u}) {
        auto &unit = scenario.units[i];
        unit.archer = true;
        unit.ranged_weapon = RangedWeapon::Bow;
        unit.weapon.range = 20.f;
    }
    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    auto &left = *FindUnit(world, {3});
    auto &right = *FindUnit(world, {6});
    left.skirmisher = right.skirmisher = true;
    left.pose.position = {-4.f, 7.f, 0.f};
    right.pose.position = {4.f, 7.f, 0.f};

    LocalMelee contact(world, .05f);
    contact.Refresh(world);
    REQUIRE(contact.Engaged(world.regiments[0]));
    REQUIRE(contact.Engaged(world.regiments[1]));
    std::vector<BattleEvent> events;
    BuildCombatEvents(world, events, nullptr, &contact);
    CHECK(std::ranges::any_of(events, [](const auto &event) {
        return event.projectile && event.source == UnitId{3} && event.target == UnitId{6};
    }));
    CHECK(std::ranges::any_of(events, [](const auto &event) {
        return event.projectile && event.source == UnitId{6} && event.target == UnitId{3};
    }));
    CHECK_FALSE(std::ranges::any_of(events, [](const auto &event) {
        return event.projectile && (event.source == UnitId{2} || event.source == UnitId{5});
    }));
}

TEST_CASE("combat.empty_bow_ammunition_does_not_prevent_a_dagger_strike_in_contact") {
    auto scenario = test::Duel();
    scenario.rules.local_melee = scenario.rules.regiment_tactics = true;
    scenario.regiments[0].pose.position.x = -.3f;
    scenario.regiments[1].pose.position.x = .3f;
    for (auto &unit : scenario.units)
        unit.pose = scenario.regiments[unit.regiment.value - 1].pose;
    scenario.units[0].archer = true;
    scenario.units[0].ranged_weapon = RangedWeapon::Bow;
    scenario.units[0].ammunition = 2;
    scenario.units[0].weapon.range = 40.f;
    auto created = CreateWorld(scenario, 2);
    REQUIRE(created);
    auto &world = created.value;
    auto &archer = world.units[0];
    archer.ammunition_remaining = 0;
    archer.ready_tick = 100;
    FlatNavigationService nav;
    REQUIRE(Step(world, {}, {}, nav));
    CHECK(archer.dagger_drawn);
    CHECK(archer.dagger_ready_tick > world.tick);
    CHECK(std::ranges::any_of(world.events, [](const auto &event) {
        return event.kind == EventKind::AttackLaunched && event.source == UnitId{1} &&
               event.target == UnitId{2} && !event.projectile;
    }));
}

TEST_CASE("replay.mixed_rank_settings_and_queued_orders_round_trip") {
    auto created = CreateWorld(MixedRanks(30.f), 2);
    REQUIRE(created);
    auto &world = created.value;
    FlatNavigationService nav;
    auto initial = nlohmann::json(Capture(world));
    CHECK_FALSE(initial["state"]["regiments"][0].contains("rank_priority"));
    CHECK_FALSE(initial["state"]["regiments"][0].contains("rank_layout"));
    initial["state"]["scenario"]["rules"].erase("mixed_rank_tactics");
    auto legacy = Restore(initial.get<Snapshot>());
    REQUIRE(legacy);
    CHECK_FALSE(legacy.value.scenario.rules.mixed_rank_tactics);
    MoveOpponent(legacy.value, 15.f);
    CHECK_FALSE(ProjectileFrom(legacy.value, {3}));
    REQUIRE(Step(legacy.value, {}, {}, nav));
    CHECK(legacy.value.regiments[0].rank_layout == RankPriority::Automatic);

    auto commands = std::array{RankCommand(world, 1, RankPriority::MeleeFront),
                               RankCommand(world, 2, RankPriority::RangedFront)};
    commands[1].tick += 2;
    REQUIRE(Step(world, commands, {}, nav));
    auto saved = nlohmann::json::parse(nlohmann::json(Capture(world)).dump()).get<Snapshot>();
    auto restored = Restore(saved);
    REQUIRE(restored);
    CHECK(restored.value.regiments[0].rank_priority == RankPriority::MeleeFront);
    CHECK(restored.value.regiments[0].rank_layout == RankPriority::MeleeFront);
    CHECK(StateHash(restored.value) == StateHash(world));
    REQUIRE(restored.value.commands.size() == 1);
    CHECK(restored.value.commands[0].command.rank_priority == RankPriority::RangedFront);
    for (unsigned i = 0; i < 3; ++i) {
        REQUIRE(Step(world, {}, {}, nav));
        REQUIRE(Step(restored.value, {}, {}, nav));
        CHECK(StateHash(restored.value) == StateHash(world));
    }
    CHECK(restored.value.regiments[0].rank_layout == RankPriority::RangedFront);
}
