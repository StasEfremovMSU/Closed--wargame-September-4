#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentGroupPolicy.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Simulation.h"
#include <sstream>
THIRD_PARTY_INCLUDES_END

namespace
{
namespace Command = battle::command;
constexpr Command::NodeId GroupId{10};
constexpr Command::NodeId SecondRegimentId{12};

battle::Scenario MakePolicyBattle()
{
    battle::Scenario Scenario;
    Scenario.rules.local_melee = true;
    const auto AddRegiment = [&](uint64 Id, int32 Team, battle::Vec3 Position, uint64 Strength)
    {
        battle::RegimentSpec Regiment;
        Regiment.id = {Id};
        Regiment.team = Team;
        Regiment.pose = {Position, Team == 0 ? 0.f : battle::Pi};
        Regiment.formation = {4, 1.1f, .25f};
        Regiment.rally_point = Position - battle::Forward(Regiment.pose.yaw) * 20.f;
        Scenario.regiments.push_back(Regiment);
        for (uint64 Soldier = 1; Soldier <= Strength; ++Soldier)
        {
            battle::UnitSpec Unit;
            Unit.id = {Id * 100 + Soldier};
            Unit.regiment = Regiment.id;
            Unit.pose = Regiment.pose;
            Unit.health = 50.f;
            Scenario.units.push_back(Unit);
        }
    };
    AddRegiment(1, 0, {-3, 0, 0}, 12);
    AddRegiment(2, 1, {4, 0, 0}, 14);
    AddRegiment(3, 0, {-5, 8, 0}, 12);
    AddRegiment(4, 1, {2, 14, 0}, 8);
    Scenario.officers = {{{1}, {101}, 2}, {{2}, {301}, 2}, {{3}, {102}, 1}};

    auto& Setup = Scenario.command_setup.emplace();
    Command::CommandNode Group;
    Group.id = GroupId;
    Group.level = Command::Level::Group;
    Group.commander = {3};
    Group.children = {{11}, SecondRegimentId};
    Setup.hierarchy.nodes.push_back(Group);
    Command::CommandNode Lead;
    Lead.id = {11};
    Lead.parent = GroupId;
    Lead.regiment = {1};
    Lead.commander = {1};
    Setup.hierarchy.nodes.push_back(Lead);
    Command::CommandNode Wing = Lead;
    Wing.id = SecondRegimentId;
    Wing.regiment = {3};
    Wing.commander = {2};
    Setup.hierarchy.nodes.push_back(Wing);
    for (uint64 Id = 1; Id <= 3; ++Id)
    {
        Command::OfficerProfile Profile;
        Profile.officer = {Id};
        Profile.command_radius_m = 60.f;
        Profile.reevaluation_ticks = 8;
        Setup.hierarchy.officers.push_back(Profile);
    }
    Command::CommandTask Mission;
    Mission.id = {1};
    Mission.recipient = GroupId;
    Mission.action = Command::TaskKind::Attack;
    Mission.status = Command::TaskStatus::Active;
    Mission.intent.target_regiment = {2};
    Mission.intent.objective_position = {4, 0, 0};
    Mission.intent.objective_radius_m = 6.f;
    Mission.authority.initiative = Command::InitiativePolicy::WithinIntent;
    Mission.authority.may_pursue = true;
    Mission.authority.may_change_target = true;
    Setup.missions.push_back(Mission);
    return Scenario;
}

std::optional<Command::GroupPolicy> MakeSupportPolicy(uint32 Schema, size_t Inputs)
{
    std::ostringstream Weights;
    Weights << "BATTLE_GROUP_MLP " << Schema << ' ' << Inputs << " 24 0\n";
    // The first hidden neuron reads choose_support. Its output weight is 1.
    for (size_t Index = 0; Index < Inputs * 24; ++Index)
        Weights << (Index == 0 ? "1 " : "0 ");
    for (size_t Index = 0; Index < 24; ++Index)
        Weights << "0 ";
    for (size_t Index = 0; Index < 24; ++Index)
        Weights << (Index == 0 ? "1 " : "0 ");
    Weights << '0';
    std::istringstream Input(Weights.str());
    return Command::LoadGroupPolicy(Input);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentGroupPolicyTest, "RegimentBattle.Officers.GroupPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentGroupPolicyTest::RunTest(const FString&)
{
    const auto V2 = MakeSupportPolicy(2, 28);
    const auto V3 = MakeSupportPolicy(3, 33);
    if (!TestTrue(TEXT("Both portable model formats load"), V2.has_value() && V3.has_value()))
        return false;

    const auto Scenario = MakePolicyBattle();
    const battle::FlatNavigationService Navigation;
    const auto Run = [&](const Command::GroupPolicy* Policy, Command::TaskKind Expected, const TCHAR* Label)
    {
        auto Created = battle::CreateWorld(Scenario, 42);
        if (!TestTrue(TEXT("The game can start the two-regiment group"), bool(Created) && Created.value.command_state.has_value()))
            return false;
        const auto Options = RegimentGroupPolicy::SimulationOptions(Policy);
        auto& World = Created.value;
        for (int32 Tick = 0; Tick < 40; ++Tick)
        {
            if (!TestTrue(TEXT("The game can advance the group decision"), bool(battle::Step(World, {}, Options, Navigation))))
                return false;
            const auto& Nodes = World.command_state->nodes;
            const auto Group = std::ranges::find(Nodes, GroupId, &Command::NodeRuntime::node);
            if (!TestTrue(TEXT("The game created the group commander"), Group != Nodes.end()))
                return false;
            const auto Task = std::ranges::find(Group->sent_tasks, SecondRegimentId, &Command::CommandTask::recipient);
            if (Task != Group->sent_tasks.end())
                return TestTrue(Label, Task->action == Expected);
        }
        AddError(TEXT("The second regiment did not receive an assignment."));
        return false;
    };

    const bool bFsm = Run(nullptr, Command::TaskKind::Attack, TEXT("FSM orders the second regiment to attack"));
    const bool bLegacy = Run(&*V2, Command::TaskKind::Support, TEXT("V2 changes the second regiment's assignment to support"));
    const bool bCommitment = Run(&*V3, Command::TaskKind::Support, TEXT("V3 changes the second regiment's assignment to support"));
    return bFsm && bLegacy && bCommitment;
}
#endif
