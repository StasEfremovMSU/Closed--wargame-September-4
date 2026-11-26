#include "battle/MarchMission.h"
#include "battle/Simulation.h"
#include "battle/Snapshot.h"
#include "battle/command/GroupPolicy.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace battle;
namespace c = battle::command;
namespace {
constexpr c::NodeId Group{10};
constexpr c::NodeId Wing{12};
constexpr RegimentId LeadRegiment{1};
constexpr RegimentId Primary{2};
constexpr RegimentId WingRegiment{3};
constexpr RegimentId Alternate{4};
constexpr Tick EpisodeTicks = 300;
constexpr Tick RolloutTicks = 150;
constexpr Tick InterventionTicks = 48;
constexpr unsigned SamplesPerEpisode = 3;
constexpr Tick SampleSpacing = 35;

enum class Doctrine { Prototype, Ancient, Napoleonic };

struct Family {
    std::string_view name;
    Pose lead, primary, wing, alternate;
    std::vector<Obstacle> obstacles;
    bool second_enemy{}, ranged{};
    Doctrine doctrine{};
    std::optional<TerrainHill> hill{};
};
const std::array TrainFamilies{
    Family{"open_duel", {{-3, 0, 0}, 0}, {{4, 0, 0}, Pi}, {{-5, 8, 0}, 0}, {}, {}, false, false},
    Family{"field_reserve",
           {{-3, 0, 0}, 0},
           {{4, 0, 0}, Pi},
           {{-5, 8, 0}, 0},
           {{2, 14, 0}, Pi},
           {},
           true,
           false},
    Family{"bridge_bank",
           {{9.3f, 0, 0}, Pi},
           {{5, 0, 0}, 0},
           {{15, -8, 0}, Pi},
           {{17, -13, 0}, Pi},
           {{-5, 4, 5, 18}, {-5, -18, 5, -4}},
           true,
           false},
    Family{"closed_bank",
           {{9.3f, 0, 0}, Pi},
           {{5, 0, 0}, 0},
           {{16, -8, 0}, Pi},
           {{19, -14, 0}, Pi},
           {{-5, 4, 12, 18}, {-5, -18, 12, -4}},
           true,
           false},
};
const std::array HoldoutFamilies{
    Family{"split_crossing",
           {{-10.2f, 9, 0}, 0},
           {{-3.8f, 9, 0}, Pi},
           {{-8, 17, 0}, 0},
           {{-8, 2.5f, 0}, Pi},
           {{-2, -20, 3, -7}, {-2, 0, 3, 5}, {-2, 13, 3, 20}},
           true,
           false},
    Family{"stone_yard",
           {{-3.2f, 0, 0}, 0},
           {{3.4f, 0, 0}, Pi},
           {{-4.5f, -8, 0}, 0},
           {{2, -11, 0}, Pi},
           {{-2, 6, 1, 10}, {7, -7, 10, -2}},
           true,
           false},
    Family{"ridge_archers",
           {{-3.5f, 0, 0}, 0},
           {{4.5f, 0, 0}, Pi},
           {{-7.5f, 9, 0}, 0},
           {{-2, 16, 0}, Pi},
           {{7, -10, 10, -4, true}, {4, 8, 7, 11, true}},
           true,
           true},
};

const std::array HistoricalTrain{
    Family{"ancient_field_screen",
           {{-9, -2, 0}, 0}, {{4, 0, 0}, Pi}, {{-12, 8, 0}, 0}, {{6, 12, 0}, Pi},
           {}, true, true, Doctrine::Ancient},
    Family{"ancient_low_ridge",
           {{-13, -3, 0}, 0}, {{2, 0, 0}, Pi}, {{-15, 8, 0}, 0}, {{4, 10, 0}, Pi},
           {}, true, true, Doctrine::Ancient, TerrainHill{17, 15, 2.5f, .35f}},
    Family{"ancient_ford",
           {{-8, -1, 0}, 0}, {{6, 0, 0}, Pi}, {{-11, 8, 0}, 0}, {{-6, 13, 0}, Pi},
           {{-2, -36, 2, -5, false, TerrainFeature::River},
            {-2, 5, 2, 36, false, TerrainFeature::River}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_line",
           {{-16, -4, 0}, 0}, {{5, -1, 0}, Pi}, {{-19, 7, 0}, 0}, {{5, 12, 0}, Pi},
           {}, true, true, Doctrine::Napoleonic},
    Family{"napoleonic_low_ridge",
           {{-14, -4, 0}, 0}, {{1, 0, 0}, Pi}, {{-16, 10, 0}, 0}, {{2, 13, 0}, Pi},
           {}, true, true, Doctrine::Napoleonic, TerrainHill{19, 17, 2.3f, .4f}},
    Family{"napoleonic_ford",
           {{-11, 0, 0}, 0}, {{5, 0, 0}, Pi}, {{-14, 9, 0}, 0}, {{-6, 15, 0}, Pi},
           {{-1, -36, 2, -5, false, TerrainFeature::River},
            {-1, 5, 2, 36, false, TerrainFeature::River}},
           true, true, Doctrine::Napoleonic},
};
const std::array HistoricalValidation{
    Family{"ancient_broken_wall",
           {{-10, 3, 0}, 0}, {{6, 4, 0}, Pi}, {{-13, -8, 0}, 0}, {{5, -11, 0}, Pi},
           {{-2, -4, 0, -0.5f, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_walled_lane",
           {{-16, -4, 0}, 0}, {{3, -5, 0}, Pi}, {{-17, 7, 0}, 0}, {{4, 13, 0}, Pi},
           {{-7, 1, 1, 2, true, TerrainFeature::StoneWall},
            {2, -11, 3, -9, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Napoleonic},
};
const std::array HistoricalHoldout{
    Family{"ancient_diagonal_ridge",
           {{-12, 3, 0}, -.2f}, {{2, -1, 0}, Pi - .2f}, {{-10, 13, 0}, -.2f}, {{-1, 8, 0}, Pi},
           {}, true, true, Doctrine::Ancient, TerrainHill{20, 11, 3.f, .42f}},
    Family{"ancient_split_crossing",
           {{-8, 7, 0}, 0}, {{5, 8, 0}, Pi}, {{-11, -8, 0}, 0}, {{-6, -9, 0}, Pi},
           {{-2, -36, 3, -13, false, TerrainFeature::River},
            {-2, -4, 3, 3, false, TerrainFeature::River},
            {-2, 13, 3, 36, false, TerrainFeature::River}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_ridge_line",
           {{-17, 4, 0}, 0}, {{-1, 3, 0}, Pi}, {{-19, -6, 0}, 0}, {{1, -8, 0}, Pi},
           {}, true, true, Doctrine::Napoleonic, TerrainHill{22, 15, 3.f, .43f}},
    Family{"napoleonic_offset_redoubt",
           {{-16, -6, 0}, 0}, {{5, -3, 0}, Pi}, {{-18, 7, 0}, 0}, {{3, 12, 0}, Pi},
           {{-4, -1, 2, 1, true, TerrainFeature::StoneWall},
            {0, -14, 1, -10, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Napoleonic},
};

const std::array SecondRoundValidation{
    Family{"ancient_field_diagonal",
           {{-10, 2, 0}, -.3f}, {{5, -3, 0}, Pi - .3f}, {{-13, -9, 0}, 0}, {{6, -12, 0}, Pi},
           {}, true, true, Doctrine::Ancient},
    Family{"ancient_walled_pocket",
           {{-10, 5, 0}, 0}, {{5, 4, 0}, Pi}, {{-14, -6, 0}, 0}, {{2, -8, 0}, Pi},
           {{-4, -3, 1, -1, true, TerrainFeature::StoneWall},
            {1, 8, 2, 13, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_slope_crossfire",
           {{-14, 3, 0}, 0}, {{3, 1, 0}, Pi}, {{-17, -8, 0}, 0}, {{3, -10, 0}, Pi},
           {}, true, true, Doctrine::Napoleonic, TerrainHill{17, 18, 1.8f, .3f}},
    Family{"napoleonic_wide_bridgehead",
           {{-11, -3, 0}, 0}, {{7, -3, 0}, Pi}, {{-13, 10, 0}, 0}, {{-5, 14, 0}, Pi},
           {{-2, -36, 3, -9, false, TerrainFeature::River},
            {-2, 4, 3, 36, false, TerrainFeature::River}},
           true, true, Doctrine::Napoleonic},
};
const std::array SecondRoundHoldout{
    Family{"ancient_terrace_pass",
           {{-14, -2, 0}, 0}, {{4, -4, 0}, Pi}, {{-13, 9, 0}, 0}, {{5, 10, 0}, Pi},
           {{-3, 2, 0, 4, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Ancient, TerrainHill{24, 16, 2.f, .38f}},
    Family{"ancient_south_bank",
           {{-10, -6, 0}, 0}, {{8, -6, 0}, Pi}, {{-12, 7, 0}, 0}, {{-6, 14, 0}, Pi},
           {{-3, -36, 1, -12, false, TerrainFeature::River},
            {-3, -1, 1, 7, false, TerrainFeature::River},
            {-3, 17, 1, 36, false, TerrainFeature::River}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_saddle",
           {{-18, -2, 0}, .1f}, {{-2, 0, 0}, Pi}, {{-20, 10, 0}, 0}, {{3, 13, 0}, Pi},
           {{-6, 5, 0, 6, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Napoleonic, TerrainHill{26, 11, 2.6f, .45f}},
    Family{"napoleonic_bent_wall",
           {{-13, 5, 0}, 0}, {{6, 4, 0}, Pi}, {{-16, -5, 0}, 0}, {{3, -10, 0}, Pi},
           {{-6, -2, -4, 3, true, TerrainFeature::StoneWall},
            {-4, 1, 2, 3, true, TerrainFeature::StoneWall},
            {4, 10, 7, 11, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Napoleonic},
};

const std::array ThirdRoundValidation{
    Family{"ancient_courtyard_turn",
           {{-12, -2, 0}, .18f}, {{4, 1, 0}, Pi}, {{-14, 8, 0}, 0}, {{3, 11, 0}, Pi},
           {{-7, 3, -3, 4, true, TerrainFeature::StoneWall},
            {-3, 4, -2, 10, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Ancient},
    Family{"ancient_forked_ford",
           {{-11, 1, 0}, 0}, {{5, 0, 0}, Pi}, {{-12, -10, 0}, 0}, {{-5, -14, 0}, Pi},
           {{-2, -36, 3, -15, false, TerrainFeature::River},
            {-2, -7, 3, -5, false, TerrainFeature::River},
            {-2, 5, 3, 36, false, TerrainFeature::River}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_reverse_slope",
           {{-15, 1, 0}, -.3f}, {{2, -5, 0}, Pi - .3f}, {{-18, 9, 0}, -.2f}, {{5, 8, 0}, Pi},
           {}, true, true, Doctrine::Napoleonic, TerrainHill{24, 22, 2.6f, .52f}},
    Family{"napoleonic_diagonal_enclosure",
           {{-17, -6, 0}, .25f}, {{1, -1, 0}, Pi}, {{-15, 8, 0}, 0}, {{3, 10, 0}, Pi},
           {{-6, 0, -1, 2, true, TerrainFeature::StoneWall},
            {-1, 2, 1, 6, true, TerrainFeature::StoneWall},
            {7, -12, 9, -4, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Napoleonic},
};
const std::array ThirdRoundHoldout{
    Family{"ancient_raised_ascent",
           {{-15, -4, 0}, .23f}, {{3, 1, 0}, Pi}, {{-17, -12, 0}, .2f}, {{5, -9, 0}, Pi},
           {{-3, -8, 1, -7, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Ancient, TerrainHill{24, 16, 2.1f, .56f}},
    Family{"ancient_chicane_crossing",
           {{-10, 4, 0}, -.1f}, {{6, 1, 0}, Pi}, {{-16, -9, 0}, .15f}, {{-6, -11, 0}, Pi},
           {{-1, -36, 3, -16, false, TerrainFeature::River},
            {-1, -8, 3, -4, false, TerrainFeature::River},
            {-1, 5, 3, 36, false, TerrainFeature::River},
            {-8, -6, -6, 2, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_orchard_gap",
           {{-15, 1, 0}, 0}, {{4, 1, 0}, Pi}, {{-16, 11, 0}, 0}, {{2, 13, 0}, Pi},
           {{-5, 4, -3, 9, true, TerrainFeature::StoneWall},
            {-5, -10, -3, -3, true, TerrainFeature::StoneWall},
            {6, 8, 7, 16, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Napoleonic},
    Family{"napoleonic_ridge_road",
           {{-16, -3, 0}, -.2f}, {{4, -6, 0}, Pi}, {{-12, 5, 0}, 0}, {{5, 11, 0}, Pi},
           {}, true, true, Doctrine::Napoleonic, TerrainHill{26, 13, 3.1f, .28f}},
};
const std::array FourthRoundHoldout{
    Family{"ancient_crossing_island",
           {{-15, -4, 0}, .1f}, {{8, -3, 0}, Pi}, {{-18, 9, 0}, 0}, {{5, 14, 0}, Pi},
           {{-5, -36, -2, -10, false, TerrainFeature::River},
            {-5, 4, -2, 36, false, TerrainFeature::River},
            {0, -36, 3, -14, false, TerrainFeature::River},
            {0, 0, 3, 36, false, TerrainFeature::River}},
           true, true, Doctrine::Ancient},
    Family{"ancient_oblique_palisades",
           {{-13, 5, 0}, -.24f}, {{7, -2, 0}, Pi - .24f}, {{-16, -10, 0}, 0}, {{5, -12, 0}, Pi},
           {{-7, -6, -5, 1, true, TerrainFeature::StoneWall},
            {-1, 2, 5, 4, true, TerrainFeature::StoneWall},
            {6, -15, 8, -9, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Ancient},
    Family{"napoleonic_crest_enclosure",
           {{-18, 5, 0}, -.3f}, {{2, 2, 0}, Pi}, {{-14, -12, 0}, .1f}, {{2, -13, 0}, Pi},
           {{-7, -6, -5, -2, true, TerrainFeature::StoneWall},
            {5, 5, 8, 7, true, TerrainFeature::StoneWall}},
           true, true, Doctrine::Napoleonic, TerrainHill{25, 14, 3.f, .39f}},
    Family{"napoleonic_lane_diversion",
           {{-16, -4, 0}, .12f}, {{6, -5, 0}, Pi}, {{-17, 6, 0}, -.16f}, {{4, 12, 0}, Pi},
           {{-8, 1, -3, 2, true, TerrainFeature::Hedge},
            {-3, 2, -2, 11, true, TerrainFeature::Hedge},
            {0, -12, 2, -7, true, TerrainFeature::StoneWall},
            {5, 5, 9, 6, true, TerrainFeature::Hedge}},
           true, true, Doctrine::Napoleonic},
};

Family BattlefieldSector(std::string_view name, MarchTerrain terrain, Doctrine doctrine,
                         Vec3 lead, Vec3 primary, Vec3 wing, Vec3 alternate) {
    constexpr MarchField field{320, 210};
    return {name, {lead, 0.f}, {primary, Pi}, {wing, 0.f}, {alternate, Pi},
            MarchTerrainAreas(terrain, field), true, true, doctrine, MarchHill(terrain, field)};
}

// Compact two-regiment-per-side experiments use game terrain geometry and ground rules.
const std::array FifthRoundTrain{
    BattlefieldSector("granicus_center", MarchTerrain::Granicus, Doctrine::Ancient,
        {-18, 1, 0}, {11, 1, 0}, {-22, 16, 0}, {13, 20, 0}),
    BattlefieldSector("senlac_approach", MarchTerrain::Hastings, Doctrine::Ancient,
        {-17, -2, 0}, {13, 0, 0}, {-18, 12, 0}, {16, 15, 0}),
    BattlefieldSector("waterloo_middle", MarchTerrain::Waterloo, Doctrine::Napoleonic,
        {-17, -3, 0}, {14, -2, 0}, {-21, 12, 0}, {15, 15, 0}),
};
const std::array FifthRoundValidation{
    BattlefieldSector("bouvines_plateau", MarchTerrain::Bouvines, Doctrine::Ancient,
        {-17, 0, 0}, {12, 1, 0}, {-20, 14, 0}, {15, 18, 0}),
    BattlefieldSector("kliszow_east", MarchTerrain::Kliszow, Doctrine::Napoleonic,
        {-16, 10, 0}, {16, 10, 0}, {-19, 23, 0}, {15, 27, 0}),
    BattlefieldSector("marengo_approach", MarchTerrain::Marengo, Doctrine::Napoleonic,
        {-18, -2, 0}, {12, -1, 0}, {-21, 13, 0}, {15, 16, 0}),
};
const std::array FifthRoundHoldout{
    BattlefieldSector("issus_fords", MarchTerrain::Issus, Doctrine::Ancient,
        {-19, 1, 0}, {12, 2, 0}, {-21, 14, 0}, {13, 18, 0}),
    BattlefieldSector("patay_hedges", MarchTerrain::Patay, Doctrine::Ancient,
        {-18, 4, 0}, {13, 5, 0}, {-20, 18, 0}, {14, 22, 0}),
    BattlefieldSector("borodino_outer_line", MarchTerrain::Borodino, Doctrine::Napoleonic,
        {-18, -11, 0}, {14, -10, 0}, {-20, 9, 0}, {16, 12, 0}),
    BattlefieldSector("wachau_ridge", MarchTerrain::Wachau, Doctrine::Napoleonic,
        {-20, -4, 0}, {14, -2, 0}, {-20, 14, 0}, {16, 17, 0}),
};

struct Job {
    const Family *family;
    uint64_t seed;
    std::string_view split;
};
Scenario Setup(const Job &job) {
    const auto &family = *job.family;
    // The seed defines geometry, starting strengths, and armament, before simulation starts.
    auto draw = [seed = job.seed](uint32_t modulus) mutable {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return uint32_t(seed >> 32) % modulus;
    };
    Scenario scenario;
    scenario.obstacles = family.obstacles;
    scenario.hill = family.hill;
    scenario.rules.local_melee = true;
    const bool historical = family.doctrine != Doctrine::Prototype;
    const bool napoleonic = family.doctrine == Doctrine::Napoleonic;
    scenario.rules.regiment_tactics = historical;
    scenario.rules.linear_infantry = napoleonic;
    auto add = [&](RegimentId id, int team, Pose pose, unsigned min_count, bool ranged) {
        pose.position.x += float(int(draw(9)) - 4) * .15f;
        pose.position.y += float(int(draw(9)) - 4) * .15f;
        RegimentSpec regiment;
        regiment.id = id;
        regiment.team = team;
        regiment.pose = pose;
        regiment.formation = {4, 1.1f, .25f};
        regiment.rally_point = pose.position - Forward(pose.yaw) * 18.f;
        regiment.speed = 3.2f;
        scenario.regiments.push_back(regiment);
        const unsigned count = min_count + draw(8);
        if (napoleonic && ranged)
            scenario.regiments.back().formation.columns = (count + 1) / 2;
        const float damage = float(4 + draw(8));
        for (unsigned i = 1; i <= count; ++i) {
            UnitSpec unit;
            unit.id = {id.value * 100 + i};
            unit.regiment = id;
            unit.pose = pose;
            unit.health = 44.f + float(draw(9));
            unit.speed = 3.6f;
            unit.archer = ranged;
            unit.weapon = {ranged ? damage * .9f : damage, ranged ? 25.f : 1.7f, 9 + draw(5), 1};
            if (historical) {
                // Keep dispatchers, fighters, and marksmen in the same mixed regiments as the game.
                const bool initial_staff = (id == LeadRegiment && i <= 2) ||
                                           (id == WingRegiment && i == 1);
                const auto dedicated_marksman = ranged && i > 1 && i % 5 != 0;
                const auto mixed_marksman = !ranged && i % 5 == (job.seed % 3) + 1;
                unit.archer = !initial_staff && (dedicated_marksman || mixed_marksman);
                unit.health = (napoleonic ? 20.f : 22.f) + float(draw(3));
                unit.speed = 3.8f;
                unit.armour = napoleonic ? 0.f : .65f;
                unit.weapon = napoleonic ? WeaponSpec{3.9f, 1.97f, 21, 4, 20.f, .8f}
                                         : WeaponSpec{3.4f, 1.65f, 20, 3, 20.f, .8f};
                if (unit.archer) {
                    unit.ranged_weapon = napoleonic ? RangedWeapon::Musket : RangedWeapon::Bow;
                    unit.armour = 0.f;
                    unit.ammunition = napoleonic ? 8 : 12;
                    unit.weapon = napoleonic ? WeaponSpec{12.3f, 81.f, 302, 10, 408.f, .56f}
                                             : WeaponSpec{3.1f, 25.f, 26, 3, 30.f, .78f};
                }
            }
            scenario.units.push_back(unit);
        }
    };
    add(LeadRegiment, 0, family.lead, 10, false);
    add(Primary, 1, family.primary, 12, false);
    add(WingRegiment, 0, family.wing, 10, false);
    if (family.second_enemy)
        add(Alternate, 1, family.alternate, 7, family.ranged);
    scenario.officers = {{{1}, {101}, 2}, {{2}, {301}, 2}, {{3}, {102}, 1}};

    auto &setup = scenario.command_setup.emplace();
    c::CommandNode group;
    group.id = Group;
    group.level = c::Level::Group;
    group.commander = {3};
    group.children = {{11}, Wing};
    setup.hierarchy.nodes.push_back(group);
    for (const auto &[node_id, regiment, commander] :
         {std::tuple{c::NodeId{11}, LeadRegiment, OfficerId{1}},
          std::tuple{Wing, WingRegiment, OfficerId{2}}}) {
        c::CommandNode child;
        child.id = node_id;
        child.parent = Group;
        child.regiment = regiment;
        child.commander = commander;
        setup.hierarchy.nodes.push_back(child);
    }
    for (unsigned i = 1; i <= 3; ++i) {
        c::OfficerProfile profile;
        profile.officer = {i};
        profile.command_radius_m = 60;
        profile.reevaluation_ticks = historical ? (i == 3 ? 40 : 20) : (i == 3 ? 12 : 4);
        profile.minimum_commitment_ticks = historical ? 20 : 8;
        setup.hierarchy.officers.push_back(profile);
    }
    c::CommandTask task;
    task.id = {1};
    task.recipient = Group;
    task.action = c::TaskKind::Attack;
    task.status = c::TaskStatus::Active;
    task.intent.target_regiment = Primary;
    task.intent.objective_position = scenario.regiments[1].pose.position;
    task.intent.objective_radius_m = 6.f;
    task.authority.initiative = c::InitiativePolicy::WithinIntent;
    task.authority.may_pursue = true;
    task.authority.may_change_target = true;
    task.authority.pursuit_limit_m = 34.f;
    setup.missions.push_back(task);
    return scenario;
}

std::string_view ActionName(c::TaskKind kind) {
    switch (kind) {
    case c::TaskKind::Support:
        return "support";
    case c::TaskKind::Attack:
        return "alternate";
    case c::TaskKind::Flank:
        return "flank";
    default:
        return "other";
    }
}
struct Decision {
    Tick tick{};
    uint32_t baseline{};
    std::vector<c::TacticalOption> options;
    std::vector<float> features;
};
struct Probe final : c::ITacticalEvaluator {
    mutable std::optional<Decision> decision;
    bool capture{};
    std::optional<c::TaskKind> force;
    Tick until{};
    const c::GroupPolicy *model{};

    c::StubResult<std::vector<c::ActionScore>>
    Evaluate(const c::EvaluationRequest &request) const override {
        if (request.node != Group || request.feature_schema != c::GroupPolicyFeatureSchema)
            return {};
        if (capture)
            decision = {request.tick,
                        request.baseline_option,
                        {request.options.begin(), request.options.end()},
                        {request.features.begin(), request.features.end()}};
        if (model)
            return model->Evaluate(request);
        if (force && request.tick < until) {
            std::vector<c::ActionScore> scores;
            for (uint32_t i = 0; i < request.options.size(); ++i)
                scores.push_back({i, request.options[i].action == force ? 1.f : 0.f, {}});
            return {c::StubStatus::Ready, std::move(scores), {}};
        }
        return {};
    }
};
const c::CommandTask *WingTask(const World &world) {
    const auto &tasks = world.command_state->nodes.front().sent_tasks;
    auto task = std::find_if(tasks.begin(), tasks.end(),
                             [](const auto &candidate) { return candidate.recipient == Wing; });
    return task == tasks.end() ? nullptr : &*task;
}
struct Orders {
    c::TaskId last{};
    unsigned issued{}, changes{};
    void Observe(const World &world) {
        if (const auto *task = WingTask(world); task && task->id != last) {
            changes += bool(last);
            ++issued;
            last = task->id;
        }
    }
};
void Advance(World &world, const NavigationService &nav, const c::ITacticalEvaluator *policy,
             bool commitment = false) {
    if (!Step(world, {}, {50, 1, policy, commitment}, nav))
        throw std::runtime_error("BattleCore rejected a simulated step.");
}
struct Outcome {
    unsigned primary_lost{}, blue_lost{}, wing_lost{}, red_lost{};
    bool primary_cleared{}, objective_reached{}, preserved{}, victory{};
    float utility{};
};
unsigned Count(const World &world, RegimentId id) {
    const auto *regiment = FindRegiment(world, id);
    return regiment ? unsigned(regiment->units.size()) : 0;
}
Outcome Measure(const World &before, const World &after, const Orders &orders) {
    const auto lost = [&](RegimentId id) { return Count(before, id) - Count(after, id); };
    const unsigned primary = lost(Primary), wing = lost(WingRegiment);
    const unsigned blue = lost(LeadRegiment) + wing;
    const unsigned red = primary + lost(Alternate);
    const auto &task = after.scenario.command_setup->missions.front();
    const auto *enemy = FindRegiment(after, Primary);
    const Vec3 objective = task.intent.objective_position;
    const auto distance = [&](const World &world) {
        float nearest = 40.f;
        for (auto id : {LeadRegiment, WingRegiment}) {
            const auto *regiment = FindRegiment(world, id);
            if (regiment && !regiment->units.empty() && !regiment->panic)
                nearest = std::min(nearest, Distance(regiment->pose.position, objective));
        }
        return nearest;
    };
    const unsigned start_blue = Count(before, LeadRegiment) + Count(before, WingRegiment);
    const unsigned start_red = Count(before, Primary) + Count(before, Alternate);
    const bool cleared = enemy->units.empty() || enemy->panic;
    const bool reached = distance(after) <= task.intent.objective_radius_m;
    const float progress = std::clamp((distance(before) - distance(after)) / 18.f, -1.f, 1.f);
    const float utility = .55f * float(primary) / float(std::max(1u, Count(before, Primary))) +
                          .20f * float(red) / float(std::max(1u, start_red)) +
                          .15f * float(cleared) + .12f * progress -
                          .70f * float(blue) / float(std::max(1u, start_blue)) -
                          .005f * float(orders.changes);
    return {primary,
            blue,
            wing,
            red,
            cleared,
            reached,
            blue * 4 <= start_blue,
            after.outcome == BattleOutcome::Victory && after.winner_team == 0,
            utility};
}
void Output(std::ostream &out, const Outcome &outcome, const Orders &orders) {
    out << ',' << outcome.utility << ',' << outcome.primary_lost << ',' << outcome.blue_lost << ','
        << outcome.wing_lost << ',' << outcome.red_lost << ',' << outcome.primary_cleared << ','
        << outcome.objective_reached << ',' << outcome.preserved << ',' << outcome.victory << ','
        << orders.issued << ',' << orders.changes;
}
constexpr std::string_view OutcomeHeader =
    ",utility,primary_lost,blue_lost,wing_lost,red_lost,primary_cleared,objective_reached,"
    "preserved,victory,orders_issued,order_changes";
void LocalFeatures(std::ostream &out, const c::TacticalOption &option,
                   std::span<const float> features) {
    out << ',' << option.target_regiment.value << ',' << option.target_command.value << ','
        << option.destination.x << ',' << option.destination.y << ',' << option.destination.z;
    for (auto feature : features)
        out << ',' << feature;
}
void LocalHeader(std::ostream &out, bool commitment = false) {
    out << ",observed_target,supported_node,option_x,option_y,option_z";
    const auto features = commitment ? std::span<const std::string_view>{c::GroupPolicyCommitmentFeatureNames}
                                     : std::span<const std::string_view>{c::GroupPolicyFeatureNames};
    for (auto feature : features)
        out << ',' << feature;
}
std::span<const float> Row(const Decision &decision, size_t index,
                           size_t count = c::GroupPolicyFeatureNames.size()) {
    return std::span{decision.features}.subspan(index * count, count);
}

struct JobOutput {
    std::string data, trace;
};
JobOutput Simulate(const Job &job, const c::GroupPolicy *model, bool evaluate) {
    constexpr unsigned precision = 9;
    const auto scenario = Setup(job);
    auto created = CreateWorld(scenario, job.seed);
    if (!created)
        throw std::runtime_error("BattleCore rejected scenario: " + std::string(job.family->name));
    const auto &initial = created.value;
    const bool historical = job.family->doctrine != Doctrine::Prototype;
    const Tick episode_ticks = historical ? 600 : EpisodeTicks;
    const Tick rollout_ticks = historical ? 300 : RolloutTicks;
    const GridNavigationService grid(scenario.obstacles, 1.5f, 49, 49, {-36, -36, 0});
    const FlatNavigationService flat;
    const auto &nav = scenario.obstacles.empty() ? static_cast<const NavigationService &>(flat)
                                                 : static_cast<const NavigationService &>(grid);
    std::ostringstream rows, trace;
    rows << std::setprecision(precision);
    trace << std::setprecision(precision);
    const auto identity = [&](std::ostream &out) {
        out << job.split << ',' << job.family->name << ',' << job.seed;
    };
    if (!evaluate) {
        auto world = initial;
        unsigned samples = 0;
        Tick next_sample = 0;
        while (world.tick < episode_ticks && world.outcome == BattleOutcome::Running &&
               samples < SamplesPerEpisode) {
            // An event can wake the officer before its scheduled reevaluation.
            if (world.tick < next_sample ||
                (!historical && world.command_state->nodes.front().schedule.next_think_tick > world.tick)) {
                Advance(world, nav, nullptr);
                continue;
            }
            const auto before = world;
            Probe recorder;
            recorder.capture = true;
            Advance(world, nav, &recorder);
            if (!recorder.decision)
                continue;
            const auto &decision = *recorder.decision;
            for (size_t index = 0; index < decision.options.size(); ++index) {
                const auto &option = decision.options[index];
                auto rollout = before;
                Probe intervention;
                if (index != decision.baseline)
                    intervention.force = option.action;
                intervention.until = before.tick + InterventionTicks;
                Orders orders;
                orders.last = WingTask(before) ? WingTask(before)->id : c::TaskId{};
                const auto end = historical ? std::min(episode_ticks, before.tick + rollout_ticks)
                                            : before.tick + rollout_ticks;
                while (rollout.tick < end && rollout.outcome == BattleOutcome::Running) {
                    Advance(rollout, nav,
                            intervention.force && rollout.tick < intervention.until ? &intervention
                                                                                    : nullptr);
                    if (rollout.tick == before.tick + 1) {
                        const auto *executed = WingTask(rollout);
                        if (!executed || executed->action != option.action)
                            throw std::runtime_error(
                                "Counterfactual did not execute its first assignment.");
                    }
                    orders.Observe(rollout);
                }
                identity(rows);
                rows << ',' << decision.tick << ',' << Group.value << ',' << Wing.value << ','
                     << ActionName(decision.options[decision.baseline].action) << ','
                     << ActionName(option.action) << ',' << decision.options.size();
                LocalFeatures(rows, option, Row(decision, index));
                Output(rows, Measure(before, rollout, orders), orders);
                rows << '\n';
            }
            ++samples;
            next_sample = world.tick + SampleSpacing;
        }
    } else {
        for (const auto &entry : {std::pair{"fsm", static_cast<const c::GroupPolicy *>(nullptr)},
                                  std::pair{"trained", model}}) {
            auto world = initial;
            Orders orders;
            Probe probe;
            probe.capture = true;
            probe.model = entry.second;
            unsigned decisions = 0, changes = 0;
            while (world.tick < episode_ticks && world.outcome == BattleOutcome::Running) {
                probe.decision.reset();
                Advance(world, nav, &probe);
                orders.Observe(world);
                if (probe.decision) {
                    const auto &decision = *probe.decision;
                    const auto *task = WingTask(world);
                    if (!task)
                        throw std::runtime_error("Missing selected second-regiment task.");
                    const auto baseline = decision.options[decision.baseline].action;
                    ++decisions;
                    changes += task->action != baseline;
                    for (size_t index = 0; index < decision.options.size(); ++index) {
                        const auto &option = decision.options[index];
                        identity(trace);
                        trace << ',' << entry.first << ',' << decision.tick << ',' << Group.value
                              << ',' << Wing.value << ',' << ActionName(baseline) << ','
                              << ActionName(task->action) << ',' << ActionName(option.action) << ','
                              << (task->action == option.action);
                        LocalFeatures(trace, option, Row(decision, index));
                        trace << '\n';
                    }
                }
            }
            if (!entry.second) {
                auto uninstrumented = initial;
                while (uninstrumented.tick < episode_ticks &&
                       uninstrumented.outcome == BattleOutcome::Running)
                    Advance(uninstrumented, nav, nullptr);
                if (StateHash(uninstrumented) != StateHash(world))
                    throw std::runtime_error("The FSM recorder changed baseline simulation state.");
            }
            identity(rows);
            rows << ',' << entry.first << ',' << world.tick << ',' << StateHash(world) << ','
                 << decisions << ',' << changes;
            Output(rows, Measure(initial, world, orders), orders);
            rows << '\n';
        }
    }
    return {rows.str(), trace.str()};
}

constexpr Tick CommitmentEpisodeTicks = 600;
constexpr Tick CommitmentRolloutTicks = 300;
constexpr Tick CounterfactualKeepTicks = 60;

std::string_view ActionName(const c::TacticalOption &option) {
    return option.keep_current_order ? "keep" : ActionName(option.action);
}

struct CommitmentProbe final : c::ITacticalEvaluator {
    const c::GroupPolicy *model{};
    const Decision *forced_decision{};
    size_t forced_index{};
    Tick retain_until{};
    bool capture{true};
    mutable size_t chosen{};
    mutable std::optional<Decision> decision;

    c::StubResult<std::vector<c::ActionScore>>
    Evaluate(const c::EvaluationRequest &request) const override {
        if (request.node != Group ||
            (request.feature_schema != c::GroupPolicyFeatureSchema &&
             request.feature_schema != c::GroupPolicyCommitmentFeatureSchema))
            return {};
        if (capture)
            decision = {request.tick, request.baseline_option,
                        {request.options.begin(), request.options.end()},
                        {request.features.begin(), request.features.end()}};
        chosen = request.baseline_option;
        if (forced_decision && request.tick == forced_decision->tick) {
            if (!std::ranges::equal(request.options, forced_decision->options) ||
                !std::ranges::equal(request.features, forced_decision->features) ||
                request.baseline_option != forced_decision->baseline)
                throw std::runtime_error("Counterfactual options changed after copying the same world.");
            chosen = forced_index;
        } else if (request.tick < retain_until) {
            // Keep receives a score only when the runtime still presents it as feasible.
            const auto keep = std::ranges::find(request.options, true,
                                                &c::TacticalOption::keep_current_order);
            if (keep == request.options.end())
                return {};
            chosen = static_cast<size_t>(keep - request.options.begin());
        } else if (model) {
            auto scores = model->Evaluate(request);
            if (scores.status != c::StubStatus::Ready || !scores.value ||
                scores.value->size() != request.options.size())
                throw std::runtime_error("V3 model rejected a valid commander observation.");
            const auto baseline = std::ranges::find(*scores.value, request.baseline_option,
                                                      &c::ActionScore::option_index);
            auto best = baseline->utility;
            for (const auto &score : *scores.value)
                if (score.utility > best) {
                    chosen = score.option_index;
                    best = score.utility;
                }
            return scores;
        } else {
            return {};
        }
        std::vector<c::ActionScore> scores;
        for (uint32_t i = 0; i < request.options.size(); ++i)
            scores.push_back({i, i == chosen ? 1.f : 0.f, {}});
        return {c::StubStatus::Ready, std::move(scores), {}};
    }
};

struct WingOrderMetrics {
    Orders counts;
    unsigned meaningful_changes{}, early_changes{}, emergency_changes{};
    std::optional<c::CommandTask> assignment;
    bool began{};

    void Observe(const World &world) {
        const auto *task = WingTask(world);
        const auto *regiment = FindRegiment(world, WingRegiment);
        const auto &nodes = world.command_state->nodes;
        const auto receiver = std::ranges::find(nodes, Wing, &c::NodeRuntime::node);
        if (assignment && regiment && receiver != nodes.end()) {
            const auto delivered = std::ranges::find(receiver->orders, assignment->id,
                                                      &c::CommandTask::id);
            if (delivered != receiver->orders.end() && delivered->received_tick &&
                receiver->memory.active_task == assignment->id && regiment->order &&
                regiment->order->id == receiver->last_order_id &&
                regiment->order->created_tick >= *delivered->received_tick &&
                (regiment->order->status == OrderStatus::Active ||
                 regiment->order->status == OrderStatus::Completed))
                began = true;
            if (task && task->id != assignment->id) {
                meaningful_changes += task->action != assignment->action ||
                    task->intent.target_regiment != assignment->intent.target_regiment ||
                    task->intent.target_command != assignment->intent.target_command;
                early_changes += !began;
                emergency_changes += regiment->panic || regiment->units.empty() ||
                    task->action == c::TaskKind::Withdraw || task->action == c::TaskKind::Rally ||
                    (delivered != receiver->orders.end() && delivered->status == c::TaskStatus::Failed);
            }
        }
        counts.Observe(world);
        if (task && (!assignment || task->id != assignment->id)) {
            assignment = *task;
            began = false;
        }
    }
};

constexpr std::string_view CommitmentOutcomeHeader =
    ",meaningful_order_changes,early_order_changes,emergency_order_changes";

void OutputCommitment(std::ostream &out, const World &before, const World &after,
                      const WingOrderMetrics &orders) {
    Output(out, Measure(before, after, orders.counts), orders.counts);
    out << ',' << orders.meaningful_changes << ',' << orders.early_changes << ','
        << orders.emergency_changes << '\n';
}

c::TaskId PreviousWingTask(const World &world) {
    if (const auto *previous = WingTask(world))
        return previous->id;
    return {};
}

void VerifyChoice(const World &world, const c::TacticalOption &option, c::TaskId previous) {
    const auto *task = WingTask(world);
    if (!task || (option.keep_current_order
                      ? !previous || task->id != previous
                      : task->action != option.action ||
                            (option.target_regiment &&
                             task->intent.target_regiment != option.target_regiment) ||
                            (option.target_command &&
                             task->intent.target_command != option.target_command)))
        throw std::runtime_error("Selected commander option did not produce its expected assignment.");
}

JobOutput SimulateCommitment(const Job &job, const c::GroupPolicy *model, bool evaluate) {
    const auto scenario = Setup(job);
    auto created = CreateWorld(scenario, job.seed);
    if (!created)
        throw std::runtime_error("BattleCore rejected scenario: " + std::string(job.family->name));
    const auto &initial = created.value;
    const GridNavigationService grid(scenario.obstacles, 1.5f, 49, 49, {-36, -36, 0});
    const FlatNavigationService flat;
    const auto &nav = scenario.obstacles.empty() ? static_cast<const NavigationService &>(flat)
                                                 : static_cast<const NavigationService &>(grid);
    const auto identity = [&](std::ostream &out) {
        out << job.split << ',' << job.family->name << ',' << job.seed;
    };
    std::ostringstream rows, trace;
    rows << std::setprecision(9);
    trace << std::setprecision(9);
    if (!evaluate) {
        auto world = initial;
        unsigned samples = 0;
        Tick next_sample = 0;
        while (world.tick + CounterfactualKeepTicks < CommitmentEpisodeTicks &&
               world.outcome == BattleOutcome::Running && samples < SamplesPerEpisode) {
            if (world.tick < next_sample) {
                Advance(world, nav, model, true);
                continue;
            }
            const auto before = world;
            CommitmentProbe recorder;
            recorder.model = model;
            Advance(world, nav, &recorder, true);
            if (!recorder.decision)
                continue;
            const auto &decision = *recorder.decision;
            const auto previous = PreviousWingTask(before);
            VerifyChoice(world, decision.options[recorder.chosen], previous);
            for (size_t index = 0; index < decision.options.size(); ++index) {
                const auto &option = decision.options[index];
                auto rollout = before;
                CommitmentProbe intervention;
                intervention.capture = false;
                intervention.forced_decision = &decision;
                intervention.forced_index = index;
                intervention.retain_until = before.tick + CounterfactualKeepTicks;
                WingOrderMetrics orders;
                orders.Observe(rollout);
                orders.counts.issued = 0;
                const auto end = std::min(CommitmentEpisodeTicks, before.tick + CommitmentRolloutTicks);
                const auto &before_messages = before.command_state->communications.messages;
                const auto prior_message_count = std::ranges::count_if(before_messages, [&](const auto &msg) {
                    const auto *task = std::get_if<c::CommandTask>(&msg.payload);
                    return task && task->recipient == Wing && task->id == previous;
                });
                Advance(rollout, nav, &intervention, true);
                VerifyChoice(rollout, option, previous);
                if (option.keep_current_order) {
                    const auto &messages = rollout.command_state->communications.messages;
                    const auto message_count = std::ranges::count_if(messages, [&](const auto &msg) {
                        const auto *task = std::get_if<c::CommandTask>(&msg.payload);
                        return task && task->recipient == Wing && task->id == previous;
                    });
                    if (message_count > prior_message_count)
                        throw std::runtime_error("Keep resent the current assignment.");
                }
                orders.Observe(rollout);
                while (rollout.tick < end && rollout.outcome == BattleOutcome::Running) {
                    Advance(rollout, nav, rollout.tick < intervention.retain_until ? &intervention : nullptr,
                            true);
                    orders.Observe(rollout);
                }
                identity(rows);
                rows << ',' << decision.tick << ',' << Group.value << ',' << Wing.value << ','
                     << ActionName(decision.options[decision.baseline]) << ',' << ActionName(option) << ','
                     << decision.options.size() << ',' << (model ? "trained" : "held");
                LocalFeatures(rows, option, Row(decision, index, c::GroupPolicyCommitmentFeatureNames.size()));
                OutputCommitment(rows, before, rollout, orders);
            }
            ++samples;
            next_sample = world.tick + SampleSpacing;
        }
        return {rows.str(), {}};
    }

    for (const auto policy : {"fsm", "held", "trained"}) {
        const bool committed = std::string_view(policy) != "fsm";
        auto world = initial;
        WingOrderMetrics orders;
        CommitmentProbe probe;
        if (std::string_view(policy) == "trained")
            probe.model = model;
        const auto feature_count = committed ? c::GroupPolicyCommitmentFeatureNames.size()
                                             : c::GroupPolicyFeatureNames.size();
        unsigned decisions = 0, overrides = 0;
        while (world.tick < CommitmentEpisodeTicks && world.outcome == BattleOutcome::Running) {
            probe.decision.reset();
            const auto previous = PreviousWingTask(world);
            Advance(world, nav, &probe, committed);
            orders.Observe(world);
            if (!probe.decision)
                continue;
            const auto &decision = *probe.decision;
            VerifyChoice(world, decision.options[probe.chosen], previous);
            ++decisions;
            overrides += probe.chosen != decision.baseline;
            for (size_t index = 0; index < decision.options.size(); ++index) {
                const auto &option = decision.options[index];
                identity(trace);
                trace << ',' << policy << ',' << decision.tick << ',' << Group.value << ','
                      << Wing.value << ',' << ActionName(decision.options[decision.baseline]) << ','
                      << ActionName(decision.options[probe.chosen]) << ',' << ActionName(option) << ','
                      << (probe.chosen == index);
                LocalFeatures(trace, option, Row(decision, index, feature_count));
                for (auto column = feature_count; column < c::GroupPolicyCommitmentFeatureNames.size(); ++column)
                    trace << ",0";
                trace << ',' << previous.value << ',' << PreviousWingTask(world).value << '\n';
            }
        }
        auto uninstrumented = initial;
        while (uninstrumented.tick < CommitmentEpisodeTicks &&
               uninstrumented.outcome == BattleOutcome::Running)
            Advance(uninstrumented, nav, probe.model, committed);
        const auto hash = StateHash(world);
        if (StateHash(uninstrumented) != hash)
            throw std::runtime_error("V3 recording changed simulation state.");
        identity(rows);
        rows << ',' << policy << ',' << world.tick << ',' << hash << ',' << decisions << ',' << overrides;
        OutputCommitment(rows, initial, world, orders);
    }
    return {rows.str(), trace.str()};
}

bool IsCommitmentModel(const c::GroupPolicy &model) {
    std::array<float, c::GroupPolicyCommitmentFeatureNames.size()> features{};
    const std::array options{c::TacticalOption{}};
    return model.Evaluate({c::Level::Group, Group, 0, c::GroupPolicyCommitmentFeatureSchema,
                           features, options, 0}).status == c::StubStatus::Ready;
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto mode = argc > 1 ? std::string_view(argv[1]) : "";
        const bool collect_third_round = mode == "collect-historical-v3";
        const bool validate_third_round = mode == "validate-historical-v3";
        const bool evaluate_third_round = mode == "evaluate-historical-v3";
        const bool collect_fifth_round = mode == "collect-historical-v5";
        const bool validate_fifth_round = mode == "validate-historical-v5";
        const bool evaluate_fifth_round = mode == "evaluate-historical-v5";
        const bool fifth_round = collect_fifth_round || validate_fifth_round || evaluate_fifth_round;
        const bool collect_fourth_round = mode == "collect-historical-v4";
        const bool validate_fourth_round = mode == "validate-historical-v4";
        const bool evaluate_fourth_round = mode == "evaluate-historical-v4";
        const bool commitment_round = collect_third_round || validate_third_round || evaluate_third_round ||
                                      collect_fourth_round || validate_fourth_round || evaluate_fourth_round || fifth_round;
        const bool validate_second_round = mode == "validate-historical-v2";
        const bool second_round = validate_second_round || mode == "collect-historical-v2" ||
                                  mode == "evaluate-historical-v2";
        const bool evaluate = validate_fifth_round || evaluate_fifth_round || validate_third_round || evaluate_third_round ||
                              validate_fourth_round || evaluate_fourth_round ||
                              mode == "evaluate" || mode == "evaluate-historical" ||
                              mode == "evaluate-historical-v2" || validate_second_round;
        const bool historical = second_round || mode == "collect-historical" ||
                                mode == "evaluate-historical";
        constexpr std::string_view BlockArgument = "--seed-block=";
        const std::string_view tail = argc > 5 ? argv[argc - 1] : "";
        const bool has_block = fifth_round && tail.starts_with(BlockArgument);
        const char *block = has_block ? argv[--argc] + BlockArgument.size() : nullptr;
        const bool use_weights = evaluate ||
            ((collect_third_round || collect_fourth_round || collect_fifth_round) && argc == 6);
        if ((mode != "collect" && !historical && !commitment_round && !evaluate) ||
            argc != (use_weights ? 6 : 5)) {
            std::cerr << "battle_officer_training collect output_dir seeds_per_family workers\n"
                      << "battle_officer_training evaluate output_dir seeds_per_family workers "
                         "model_file\n"
                      << "Use collect-historical and evaluate-historical for the historical pilot.\n"
                      << "Use collect-historical-vN, validate-historical-vN, or evaluate-historical-vN "
                         "for historical rounds 2 to 5. Round 5 accepts --seed-block=N.\n";
            return 2;
        }
        auto parse_count = [](const char *arg, unsigned max) {
            size_t consumed = 0;
            const auto value = std::stoul(arg, &consumed);
            if (consumed != std::string_view(arg).size() || !value || value > max)
                throw std::invalid_argument("Invalid batch count.");
            return unsigned(value);
        };
        const auto seeds = parse_count(argv[3], 500);
        const auto workers = parse_count(argv[4], 32);
        const uint64_t offset = fifth_round ? 1'000'000ULL * (block ? parse_count(block, 1000) : 1) : 0;
        std::optional<c::GroupPolicy> model;
        if (use_weights) {
            std::ifstream weights(argv[5]);
            model = c::LoadGroupPolicy(weights);
            if (!model)
                throw std::runtime_error("Invalid or missing group policy weights.");
            if (commitment_round && !IsCommitmentModel(*model))
                throw std::runtime_error("Commitment commands require V3 group policy weights.");
            if (!commitment_round && IsCommitmentModel(*model))
                throw std::runtime_error("Legacy commands require V2 group policy weights.");
        }
        const std::filesystem::path destination(argv[2]);
        std::filesystem::create_directories(destination);
        std::vector<Job> jobs;
        const auto add_families = [&](std::span<const Family> families, uint64_t start_seed,
                                      std::string_view split) {
            for (size_t family = 0; family < families.size(); ++family)
                for (unsigned seed = 0; seed < seeds; ++seed)
                    jobs.push_back({&families[family], offset + start_seed + family * 1000 + seed,
                                    split.empty() ? (seed % 5 == 4 ? "validation" : "train") : split});
        };
        if (validate_fifth_round)
            add_families(FifthRoundValidation, 500000, "validation");
        else if (evaluate_fifth_round)
            add_families(FifthRoundHoldout, 700000, "holdout");
        else if (collect_fifth_round) {
            const uint64_t start = model ? 300000 : 100000;
            add_families(HistoricalTrain, start, "train");
            add_families(HistoricalValidation, start + 10000, "train");
            add_families(HistoricalHoldout, start + 20000, "train");
            add_families(SecondRoundValidation, start + 30000, "train");
            add_families(SecondRoundHoldout, start + 40000, "train");
            add_families(ThirdRoundHoldout, start + 50000, "train");
            add_families(FifthRoundTrain, start + 60000, "train");
            if (!model)
                add_families(FifthRoundValidation, 490000, "validation");
        } else if (validate_fourth_round)
            add_families(ThirdRoundValidation, 855000, "validation");
        else if (evaluate_fourth_round)
            add_families(FourthRoundHoldout, 880000, "holdout");
        else if (validate_third_round)
            add_families(ThirdRoundValidation, 650000, "validation");
        else if (evaluate_third_round)
            add_families(ThirdRoundHoldout, 700000, "holdout");
        else if (collect_third_round || collect_fourth_round) {
            const uint64_t start = collect_fourth_round ? (model ? 900000 : 800000) : (model ? 750000 : 600000);
            add_families(HistoricalTrain, start, "train");
            add_families(HistoricalValidation, start + 10000, "train");
            add_families(HistoricalHoldout, start + 20000, "train");
            add_families(SecondRoundValidation, start + 30000, "train");
            add_families(SecondRoundHoldout, start + 40000, "train");
            if (collect_fourth_round)
                add_families(ThirdRoundHoldout, start + 50000, "train");
            if (!model)
                add_families(ThirdRoundValidation, collect_fourth_round ? 855000 : 650000, "validation");
        } else if (validate_second_round)
            add_families(SecondRoundValidation, 450000, "validation");
        else if (second_round && evaluate)
            add_families(SecondRoundHoldout, 500000, "holdout");
        else if (second_round) {
            add_families(HistoricalTrain, 400000, "train");
            add_families(HistoricalValidation, 410000, "train");
            add_families(HistoricalHoldout, 420000, "train");
            add_families(SecondRoundValidation, 450000, "validation");
        } else if (historical && evaluate)
            add_families(HistoricalHoldout, 300000, "holdout");
        else if (historical) {
            add_families(HistoricalTrain, 200000, "train");
            add_families(HistoricalValidation, 250000, "validation");
        } else if (evaluate)
            add_families(HoldoutFamilies, 100000, "holdout");
        else
            add_families(TrainFamilies, 10000, {});
        std::vector<JobOutput> results(jobs.size());
        std::vector<std::exception_ptr> errors(jobs.size());
        std::atomic_size_t next{0};
        {
            std::vector<std::future<void>> pool;
            for (unsigned worker = 0; worker < std::min(size_t(workers), jobs.size()); ++worker)
                pool.emplace_back(std::async(std::launch::async, [&] {
                    for (auto index = next.fetch_add(1); index < jobs.size();
                         index = next.fetch_add(1)) {
                        try {
                            results[index] = commitment_round
                                ? SimulateCommitment(jobs[index], model ? &*model : nullptr, evaluate)
                                : Simulate(jobs[index], model ? &*model : nullptr, evaluate);
                        } catch (...) {
                            errors[index] = std::current_exception();
                        }
                    }
                }));
            for (auto &worker : pool)
                worker.get();
        }
        for (const auto &error : errors)
            if (error)
                std::rethrow_exception(error);

        std::ofstream data(destination / (evaluate ? "evaluation.csv" : "rollouts.csv"),
                           std::ios::binary);
        data << "split,family,seed";
        if (evaluate)
            data << ",policy,ticks,state_hash,decisions,policy_overrides";
        else {
            data << ",tick,group,wing,baseline,action,available_count";
            if (commitment_round)
                data << ",behavior";
            LocalHeader(data, commitment_round);
        }
        data << OutcomeHeader;
        if (commitment_round)
            data << CommitmentOutcomeHeader;
        data << '\n';
        std::ofstream trace;
        if (evaluate) {
            trace.open(destination / "evaluation-decisions.csv", std::ios::binary);
            trace << "split,family,seed,policy,tick,group,wing,baseline,chosen,option,selected";
            LocalHeader(trace, commitment_round);
            if (commitment_round)
                trace << ",previous_task_id,current_task_id";
            trace << '\n';
        }
        size_t rows = 0;
        for (const auto &result : results) {
            data << result.data;
            rows += std::count(result.data.begin(), result.data.end(), '\n');
            if (evaluate)
                trace << result.trace;
        }
        data.close();
        if (evaluate)
            trace.close();
        if (!data || (evaluate && !trace) || !rows)
            throw std::runtime_error("Could not write populated result files.");
        std::cout << (evaluate ? "Paired scenarios: " : "Collection scenarios: ") << jobs.size()
                  << ", result rows: " << rows << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
