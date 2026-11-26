#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentMissionScenario.h"
#include "RegimentBattleField.h"
#include "RegimentOfficerOrders.h"
#include "battle/Control.h"
#include "battle/Simulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCommandScopeTest, "RegimentBattle.OfficerCommandScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCommandScopeTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    Preset.Roster = MakeDefaultMissionRoster();
    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field);
    auto Created = battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
    if (!TestTrue(TEXT("Mission creates its command hierarchy"), bool(Created) && Created.value.command_state.has_value()))
        return false;
    auto& World = Created.value;
    for (const auto& Node : World.command_state->hierarchy.nodes)
    {
        const auto Unit = RegimentOfficerOrders::FindOfficer(World, Node.id.value);
        if (Node.team != 0)
        {
            TestFalse(TEXT("Enemy commanders cannot be selected"), bool(Unit));
            continue;
        }
        if (!TestTrue(TEXT("Player can possess every friendly command tier"),
            bool(Unit) && battle::AcquireControl(World, Unit) == battle::Status::Ok))
            return false;
        const auto Owned = RegimentOfficerOrders::Regiments(World, Unit);
        if (!TestFalse(TEXT("Command node has living recipients"), Owned.empty()))
            return false;
        const auto All = RegimentOfficerOrders::HoldRecipients(World, Owned.front(), false);
        const auto Wing = RegimentOfficerOrders::HoldRecipients(World, Owned.front(), true);
        using battle::command::Level;
        if (Node.level == Level::Regiment)
            TestTrue(TEXT("A regiment captain has only individual orders"), All.empty() && Wing.empty());
        if (Node.level == Level::Group)
            TestTrue(TEXT("Group order targets its two regiments"), All == Owned && All.size() == 2 && Wing.empty());
        if (Node.level == Level::Army)
        {
            TestTrue(TEXT("General can command the whole army"), All == Owned && All.size() == 6);
            TestTrue(TEXT("General can address one distinct subordinate wing"), Wing.size() == 2 && Wing.front() == Owned.front());
            TestTrue(TEXT("Enemy regiment cannot select an army wing"), RegimentOfficerOrders::HoldRecipients(World, {7}, true).empty());
        }
    }
    const auto& Nodes = World.command_state->hierarchy.nodes;
    const auto Army = std::ranges::find_if(Nodes, [](const auto& Node)
        { return Node.team == 0 && Node.level == battle::command::Level::Army; });
    if (Army == Nodes.end() || battle::AcquireControl(World, RegimentOfficerOrders::FindOfficer(World, Army->id.value)) != battle::Status::Ok)
        return false;
    const auto Issuer = Army->commander;
    const auto Wing = RegimentOfficerOrders::HoldRecipients(World, {1}, true);
    std::vector<battle::CommandEnvelope> Commands;
    uint64 Sequence = 1;
    const battle::FlatNavigationService Navigation;
    for (const auto Id : Wing)
        RegimentOfficerOrders::QueueOrder(World, Navigation, Id, ERegimentPlayerOrder::Hold, {}, Commands, Sequence);
    if (!TestEqual(TEXT("One command per member of selected wing"), Commands.size(), size_t(2)) ||
        !TestTrue(TEXT("General's orders reach the actual simulation"), bool(battle::Step(World, Commands, {}, Navigation))))
        return false;
    for (const auto Id : Wing)
        TestTrue(TEXT("Order is in transit under the general's identity"), std::ranges::any_of(World.pending_orders,
            [&](const auto& Order) { return Order.issuer == Issuer && Order.recipient == Id && Order.type == battle::OrderType::Hold; }));

    const auto Field = battle::MarchTerrain::OpenField;
    const auto Deployed = MakeMissionOpening(Preset.Roster, Field, Preset.Field);
    auto Staffed = battle::CreateWorld(BuildMissionBattle(Preset.Roster, Deployed, Field,
        EMissionDeployment::AuthoredStart, false, Preset.Field, 0, false, true), 42);
    if (!TestTrue(TEXT("Army with mounted staff deploys"), bool(Staffed)))
        return false;
    auto& StaffWorld = Staffed.value;
    const auto General = RegimentOfficerOrders::FindOfficer(StaffWorld, Army->id.value);
    if (!TestTrue(TEXT("The general with staff can be controlled"),
            battle::AcquireControl(StaffWorld, General) == battle::Status::Ok))
        return false;
    const auto StaffWing = RegimentOfficerOrders::HoldRecipients(StaffWorld, {1}, true);
    Commands.clear();
    if (!TestTrue(TEXT("The general prepares one courier dispatch for the selected wing"),
            RegimentOfficerOrders::QueueStaffHold(StaffWorld, StaffWing, Commands, Sequence).has_value()) ||
        !TestEqual(TEXT("The wing receives a single group-level task"), Commands.size(), size_t(1)))
        return false;
    const auto& SelectedTask = Commands.front().command.command_task;
    TestTrue(TEXT("The courier message is visible while playback is paused"),
        !RegimentOfficerOrders::DescribeCourier(StaffWorld, {1}, Commands, true).IsEmpty());
    TestTrue(TEXT("Preparing the dispatch does not advance simulation time"), StaffWorld.tick == 0);
    if (!TestTrue(TEXT("Resuming submits the group dispatch"), bool(battle::Step(StaffWorld, Commands, {}, Navigation))))
        return false;
    using namespace battle::command;
    const auto FindDispatch = [&]() -> const MessageEnvelope*
    {
        const auto& Messages = StaffWorld.command_state->communications.messages;
        const auto Dispatch = std::ranges::find_if(Messages, [&](const auto& Message)
        {
            const auto* Task = std::get_if<CommandTask>(&Message.payload);
            return Message.channel == DeliveryChannel::Courier && Task &&
                Task->recipient == SelectedTask.recipient && Task->priority == battle::PlayerOrderPriority;
        });
        return Dispatch == Messages.end() ? nullptr : &*Dispatch;
    };
    const auto* Dispatch = FindDispatch();
    if (!TestNotNull(TEXT("The wing has a real courier dispatch"), Dispatch))
        return false;
    // Deployment may leave riders a short distance from the reporting position.
    for (int32 Tick = 0; Tick < 40 && Dispatch && Dispatch->status == DeliveryStatus::Queued; ++Tick)
    {
        if (!TestTrue(TEXT("The adjutant can reach the general before departing"),
                bool(battle::Step(StaffWorld, {}, {}, Navigation))))
            return false;
        Dispatch = FindDispatch();
    }
    TestTrue(TEXT("The group order is carried by an actual adjutant"),
        Dispatch && Dispatch->status == DeliveryStatus::InTransit && bool(Dispatch->courier));
    TestFalse(TEXT("Sending a wing order never queues direct regiment orders"),
        std::ranges::any_of(StaffWorld.pending_orders, [](const auto& Order)
        {
            return Order.priority == battle::PlayerOrderPriority;
        }));
    return !HasAnyErrors();
}
#endif
