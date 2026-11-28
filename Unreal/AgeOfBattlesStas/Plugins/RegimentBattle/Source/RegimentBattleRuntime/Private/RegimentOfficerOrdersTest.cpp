#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattleField.h"
#include "RegimentOfficerOrders.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Simulation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
battle::World MakeOfficerOrderWorld()
{
    battle::Scenario Scenario;
    const auto AddRegiment = [&](uint64 Id, int32 Team, float X, float Y)
    {
        battle::RegimentSpec Regiment;
        Regiment.id = {Id};
        Regiment.team = Team;
        Regiment.pose.position = {X, Y, 0.f};
        Regiment.formation = {2, 2.f, .3f};
        Regiment.rally_point = {X - 12.f, Y, 0.f};
        if (Team == 0)
            Regiment.skirmisher_count = 1;
        Scenario.regiments.push_back(Regiment);
        for (uint64 Index = 0; Index < 2; ++Index)
        {
            battle::UnitSpec Unit;
            Unit.id = {Id * 10 + Index};
            Unit.regiment = Regiment.id;
            Unit.pose = Regiment.pose;
            Unit.archer = Team == 0 && Index == 1;
            Scenario.units.push_back(Unit);
        }
        Scenario.officers.push_back({{Id}, {Id * 10}, 1});
    };
    Scenario.rules.local_melee = true;
    Scenario.rules.regiment_tactics = true;
    AddRegiment(1, 0, 0.f, 0.f);
    AddRegiment(2, 1, 9.f, 0.f);
    AddRegiment(3, 1, 20.f, 24.f);
    return battle::CreateWorld(Scenario, 42).value;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerOrdersTest, "RegimentBattle.OfficerOrders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerOrderPresentationTest, "RegimentBattle.OfficerOrderPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentManeuverOrderTest, "RegimentBattle.ManeuverOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSpecialFormationsTest, "RegimentBattle.SpecialFormations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentSpecialFormationsTest::RunTest(const FString&)
{
    using namespace RegimentOfficerOrders;
    using battle::FormationShape;
    using battle::UnitTroop;
    using Order = ERegimentPlayerOrder;
    const battle::RegimentId Friendly{1};
    const battle::FlatNavigationService Navigation;
    const auto Check = [&](ERegimentPlayerOrder Action, FormationShape Shape, FFormationContext Context,
        UnitTroop Troop, battle::CavalryClass Cavalry = {}, bool bLinear = false)
    {
        auto Scenario = MakeOfficerOrderWorld().scenario;
        Scenario.rules.linear_infantry = bLinear;
        Scenario.regiments[0].skirmisher_count = 0;
        Scenario.regiments[1].pose.position.x = 70.f;
        auto Unit = Scenario.units.front();
        Unit.troop = Troop;
        Unit.cavalry = Cavalry;
        Unit.ranged_weapon = bLinear ? battle::RangedWeapon::Musket : battle::RangedWeapon::None;
        Unit.archer = bLinear;
        Scenario.units[0] = Unit;
        Unit.id = {11};
        Scenario.units[1] = Unit;
        for (uint64 Index = 0; Index < 24; ++Index)
        {
            Unit.id = {100 + Index};
            Scenario.units.push_back(Unit);
        }
        auto World = battle::CreateWorld(Scenario, 42).value;
        auto* Regiment = battle::FindRegiment(World, Friendly);
        if (!TestNotNull(TEXT("Formation fixture creates a regiment"), Regiment) ||
            !TestTrue(TEXT("Formation fixture possesses its commander"), battle::AcquireControl(World, {10}) == battle::Status::Ok))
            return;
        const auto Mask = SpecialFormations(World, *Regiment, Context);
        const bool bAllowed = Shape != FormationShape::Rectangle;
        TestEqual(TEXT("Officer sees this order only where appropriate"),
            (Mask & RegimentSpecialFormationBit(Action)) != 0, bAllowed);
        std::vector<battle::CommandEnvelope> Pending;
        uint64 Sequence = 1;
        QueueOrder(World, Navigation, Friendly, Action, {}, Pending, Sequence, Context);
        if (!TestEqual(TEXT("Only a permitted special formation is sent"), Pending.size(), bAllowed ? size_t(1) : size_t(0)) || !bAllowed)
            return;
        TestTrue(TEXT("Formation uses the existing reform command"), Pending.front().command.kind == battle::CommandKind::ReformRegiment);
        TestTrue(TEXT("Reform carries the actual new geometry"), Pending.front().command.formation.shape == Shape);
        QueueOrder(World, Navigation, Friendly, Order::Line, {}, Pending, Sequence, Context);
        if (TestEqual(TEXT("The line order can immediately cancel a queued special formation"), Pending.size(), size_t(2)))
            TestTrue(TEXT("Line restores rectangular geometry"), Pending.back().command.formation.shape == FormationShape::Rectangle);
        Pending.resize(1);
        if (TestTrue(TEXT("The simulation consumes the officer's formation command"), bool(battle::Step(World, Pending, {}, Navigation))))
            TestTrue(TEXT("The selected special formation stays active when the officer's AI updates"),
                battle::FindRegiment(World, Friendly)->formation.shape == Shape);
    };
    Check(Order::Wedge, FormationShape::Wedge, {TEXT("high_medieval"), TEXT("livonian_order")}, UnitTroop::Mounted, battle::CavalryClass::Heavy);
    Check(Order::Wedge, FormationShape::Rectangle, {TEXT("ancient"), TEXT("carthage")}, UnitTroop::Mounted, battle::CavalryClass::Light);
    Check(Order::Square, FormationShape::Square, {TEXT("napoleonic"), TEXT("france")}, UnitTroop::Infantry, {}, true);
    Check(Order::Testudo, FormationShape::Testudo, {TEXT("ancient"), TEXT("rome")}, UnitTroop::Infantry);
    Check(Order::Testudo, FormationShape::Rectangle, {TEXT("ancient"), TEXT("rome")}, UnitTroop::Spearman);
    Check(Order::Circle, FormationShape::Circle, {TEXT("high_medieval"), TEXT("rus")}, UnitTroop::Spearman);
    Check(Order::Circle, FormationShape::Rectangle, {TEXT("napoleonic"), TEXT("france")}, UnitTroop::Infantry, {}, true);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentPendingOfficerPreviewTest, "RegimentBattle.PendingOfficerPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentPendingOfficerPreviewTest::RunTest(const FString&)
{
    auto World = MakeOfficerOrderWorld();
    using battle::CommandKind;
    const battle::UnitId First{10};
    const battle::UnitId Second{20};
    battle::Command Command;
    Command.kind = CommandKind::AcquireControl;
    Command.unit = First;
    std::vector<battle::CommandEnvelope> Pending{{World.tick, 1, Command}};
    TestTrue(TEXT("A queued selection is visible before any simulation step"),
        RegimentOfficerOrders::PendingOfficer(World, Pending) == First && !World.controlled);

    Command.unit = Second;
    Pending.push_back({World.tick, 2, Command});
    TestTrue(TEXT("The newer choice replaces the previous portrait"),
        RegimentOfficerOrders::PendingOfficer(World, Pending) == Second);
    auto* Candidate = battle::FindUnit(World, Second);
    if (!TestNotNull(TEXT("The second officer exists"), Candidate))
        return false;
    Candidate->alive = false;
    TestFalse(TEXT("A fallen officer cannot leave a pending portrait"),
        bool(RegimentOfficerOrders::PendingOfficer(World, Pending)));
    Candidate->alive = true;
    Pending.pop_back();

    const battle::FlatNavigationService Navigation;
    if (!TestTrue(TEXT("The queued possession is applied on resume"), bool(battle::Step(World, Pending, {}, Navigation))))
        return false;
    TestTrue(TEXT("The actual officer replaces the preview once acquired"),
        World.controlled == First && !RegimentOfficerOrders::PendingOfficer(World, Pending));
    Pending.clear();
    Command.kind = CommandKind::ReleaseControl;
    Command.unit = First;
    Pending.push_back({World.tick, 3, Command});
    TestFalse(TEXT("Leaving an officer never presents a pending portrait"),
        bool(RegimentOfficerOrders::PendingOfficer(World, Pending)));
    return !HasAnyErrors();
}

bool FRegimentManeuverOrderTest::RunTest(const FString&)
{
    using namespace RegimentOfficerOrders;
    auto World = MakeOfficerOrderWorld();
    TestTrue(TEXT("Fixture possesses the regiment officer"), battle::AcquireControl(World, {10}) == battle::Status::Ok);
    const battle::FlatNavigationService Navigation;
    auto Plan = PreviewManeuver(World, Navigation, {1}, {6, -12, 0}, battle::Vec3{6, -8, 0}, false);
    if (!TestEqual(TEXT("The officer previews exactly his regiment"), Plan.size(), size_t(1)))
        return false;
    TestTrue(TEXT("The dragged front is centered between the two ends"),
        Plan.front().Goal.position == battle::Vec3{6, -10, 0});
    TestEqual(TEXT("The formation faces beyond the front and toward the maneuver"),
        Plan.front().Goal.yaw, 0.f, .001f);
    TestTrue(TEXT("The drag determines a legal width"), Plan.front().Columns == 2);

    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 3;
    const bool Reachable = Plan.front().bReachable;
    TestTrue(TEXT("The preview finds a valid route"), Reachable);
    Plan.front().bReachable = false;
    QueueManeuver(World, Plan, battle::OrderType::MoveTo, battle::MovementPace::Run, Pending, Sequence, Navigation);
    TestTrue(TEXT("An obstructed route queues no commands"), Pending.empty() && Sequence == 3);
    Plan.front().bReachable = Reachable;
    QueueManeuver(World, Plan, battle::OrderType::MoveTo, battle::MovementPace::Run, Pending, Sequence, Navigation);
    if (!TestEqual(TEXT("A valid maneuver queues one delivered order"), Pending.size(), size_t(1)))
        return false;
    const auto& Direct = Pending.front().command.order;
    TestTrue(TEXT("Pace, facing and width survive the command boundary"),
        Direct.pace == battle::MovementPace::Run && Direct.facing == Plan.front().Goal.yaw &&
        Direct.move_columns == Plan.front().Columns && Direct.delivery_tick > World.tick);

    battle::FindRegiment(World, {1})->formation.columns = 1;
    World.scenario.obstacles.push_back({5.8f, -11.4f, 6.2f, -10.8f, true});
    const auto Narrow = PreviewManeuver(World, Navigation, {1}, {6, -10, 0}, {}, false);
    const auto Wide = PreviewManeuver(World, Navigation, {1}, {6, -12, 0}, battle::Vec3{6, -8, 0}, false);
    TestTrue(TEXT("The column itself can reach the destination"), Narrow.size() == 1 && Narrow.front().bReachable);
    TestTrue(TEXT("The preview rejects a frontage that would overlap the wall"), Wide.size() == 1 && !Wide.front().bReachable);
    return !HasAnyErrors();
}

bool FRegimentOfficerOrderPresentationTest::RunTest(const FString&)
{
    using namespace RegimentOfficerOrders;
    auto World = MakeOfficerOrderWorld();
    if (!TestTrue(TEXT("Officer is possessed"), battle::AcquireControl(World, {10}) == battle::Status::Ok))
        return false;

    const battle::FlatNavigationService Navigation;
    auto& Regiment = *battle::FindRegiment(World, {1});
    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 1;
    QueueOrder(World, Navigation, Regiment.id, ERegimentPlayerOrder::Follow, {}, Pending, Sequence);
    const auto QueuedFollow = DescribeOrder(World, Regiment, Pending, true);
    TestTrue(TEXT("Order awaits simulation resume"),
        QueuedFollow.Delivery == ERegimentOrderDelivery::Paused);
    TestTrue(TEXT("Queued order identifies the action and regiment"),
        !QueuedFollow.Action.IsEmpty() && QueuedFollow.Recipient.EqualTo(FText::FromString(TEXT("Полк 1"))));
    TestTrue(TEXT("Order belongs to the selected regiment"),
        DescribeOrder(World, *battle::FindRegiment(World, {2}), Pending, true).Delivery ==
            ERegimentOrderDelivery::None);
    if (!TestTrue(TEXT("BattleCore consumes the follow command"), bool(battle::Step(World, Pending, {}, Navigation))))
        return false;
    Pending.clear();
    const auto Following = DescribeOrder(World, Regiment, Pending, false);
    TestTrue(TEXT("Follow becomes active after the tick"), Following.Follow == ERegimentFollowStatus::Active);
    TestTrue(TEXT("Resuming clears the queued indicator"), Following.Delivery == ERegimentOrderDelivery::None);
    Regiment.melee_engaged = true;
    TestTrue(TEXT("Contact has its own suspended follow status"),
        DescribeOrder(World, Regiment, Pending, false).Follow == ERegimentFollowStatus::Melee);
    Regiment.melee_engaged = false;

    QueueOrder(World, Navigation, Regiment.id, ERegimentPlayerOrder::Hold, {}, Pending, Sequence);
    TestTrue(TEXT("An unprocessed order is queued"),
        DescribeOrder(World, Regiment, Pending, false).Delivery == ERegimentOrderDelivery::Queued);
    if (!TestTrue(TEXT("BattleCore accepts the hold order"), bool(battle::Step(World, Pending, {}, Navigation))))
        return false;
    Pending.clear();
    TestTrue(TEXT("Accepted order is still in transit"),
        DescribeOrder(World, Regiment, Pending, false).Delivery == ERegimentOrderDelivery::InTransit);
    TestTrue(TEXT("Pausing also stops an order in transit"),
        DescribeOrder(World, Regiment, Pending, true).Delivery == ERegimentOrderDelivery::Paused);
    for (int32 Step = 0; !World.pending_orders.empty() && Step <= 25; ++Step)
        if (!TestTrue(TEXT("BattleCore advances toward delivery"), bool(battle::Step(World, {}, {}, Navigation))))
            return false;
    const auto Holding = DescribeOrder(World, Regiment, Pending, false);
    TestTrue(TEXT("Delivered order is distinguished from no player order"),
        Holding.Delivery == ERegimentOrderDelivery::Executing);
    TestTrue(TEXT("The status carries the actual runtime order ID"),
        Regiment.order && Holding.OrderId == Regiment.order->id.value && Holding.OrderId != 0);
    TestTrue(TEXT("The recipient stays the same after delivery"), Holding.Recipient.EqualTo(QueuedFollow.Recipient));
    TestTrue(TEXT("Activity is reported from the live regiment"), !Holding.Activity.IsEmpty());
    if (!TestTrue(TEXT("The delivered order remains available"), Regiment.order.has_value()))
        return false;
    Regiment.deployment_ticks = World.tick + 1;
    const auto Deploying = DescribeOrder(World, Regiment, Pending, false);
    TestTrue(TEXT("An active order waits while the regiment deploys"),
        Deploying.Delivery == ERegimentOrderDelivery::Blocked && !Deploying.OrderDetail.IsEmpty());
    Regiment.deployment_ticks = 0;
    TestTrue(TEXT("The order resumes when the blocker clears"),
        DescribeOrder(World, Regiment, Pending, false).Delivery == ERegimentOrderDelivery::Executing);
    Regiment.order->type = battle::OrderType::MoveTo;
    Regiment.march_lane.emplace();
    Regiment.march_lane->order = Regiment.order->id;
    Regiment.march_lane->stalled_seconds = battle::MarchLane::StallSeconds;
    const auto WaitingForPassage = DescribeOrder(World, Regiment, Pending, false);
    TestTrue(TEXT("Ongoing movement reports a documented route stall"),
        WaitingForPassage.Delivery == ERegimentOrderDelivery::Blocked && !WaitingForPassage.OrderDetail.IsEmpty());
    Regiment.march_lane->order = {};
    TestTrue(TEXT("A previous order's stalled lane does not affect a new order"),
        DescribeOrder(World, Regiment, Pending, false).Delivery == ERegimentOrderDelivery::Executing);
    Regiment.march_lane.reset();
    Regiment.order->status = battle::OrderStatus::Completed;
    TestTrue(TEXT("A completed order has its own status"),
        DescribeOrder(World, Regiment, Pending, false).Delivery == ERegimentOrderDelivery::Complete);
    Regiment.order->status = battle::OrderStatus::Failed;
    TestTrue(TEXT("A failed application is distinguished from completion"),
        DescribeOrder(World, Regiment, Pending, false).Delivery == ERegimentOrderDelivery::Unapplied);

    FPlayerOrderJournal Journal;
    auto Expiring = *Regiment.order;
    Expiring.id = {};
    Expiring.created_tick = World.tick;
    Expiring.delivery_tick = World.tick + 4;
    Expiring.expiry_tick = World.tick + 3;
    const auto ExpiringId = World.next_order_id;
    if (!TestTrue(TEXT("The simulation accepts an order in transit"), battle::SubmitOrder(World, Expiring) == battle::Status::Ok))
        return false;
    Journal.Observe(World);
    for (int32 Step = 0; Step < 5; ++Step)
        if (!TestTrue(TEXT("Simulation advances past expiry"), bool(battle::Step(World, {}, {}, Navigation))))
            return false;
    Journal.Observe(World);
    const auto Lost = DescribeOrder(World, Regiment, Pending, false, &Journal);
    TestTrue(TEXT("Expired in-transit command is reported using its own ID"),
        Lost.Delivery == ERegimentOrderDelivery::Unapplied && Lost.OrderId == ExpiringId);
    TestTrue(TEXT("An expired order includes the reason"), Lost.OrderDetail.ToString().Contains(TEXT("срок")));
    return !HasAnyErrors();
}

bool FRegimentOfficerOrdersTest::RunTest(const FString& Parameters)
{
    using namespace RegimentOfficerOrders;
    auto World = MakeOfficerOrderWorld();
    const battle::RegimentId Friendly{1}, HiddenEnemy{2};
    auto* Regiment = battle::FindRegiment(World, Friendly);
    if (!TestNotNull(TEXT("The fixture has a friendly regiment"), Regiment))
        return false;
    TestEqual(TEXT("Default selection finds the friendly regiment officer"),
        FindOfficer(World, 0).value, uint64(10));
    TestEqual(TEXT("Only the officer's own living regiment is selectable"), Regiments(World, {10}).size(), size_t(1));
    TestTrue(TEXT("The enemy officer has no player regiment selection"), Regiments(World, {20}).empty());

    const battle::FlatNavigationService Navigation;
    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 4;
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Hold, {}, Pending, Sequence);
    TestTrue(TEXT("Unpossessed commands leave the queue untouched"), Pending.empty() && Sequence == 4);
    TestTrue(TEXT("Fixture acquires the officer"), battle::AcquireControl(World, {10}) == battle::Status::Ok);
    QueueOrder(World, Navigation, HiddenEnemy, ERegimentPlayerOrder::Hold, {}, Pending, Sequence);
    TestTrue(TEXT("Enemy orders leave the queue untouched"), Pending.empty() && Sequence == 4);

    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Follow, {}, Pending, Sequence);
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Hold, {}, Pending, Sequence);
    if (TestEqual(TEXT("Hold disables queued follow before delivering the order"), Pending.size(), size_t(3)))
    {
        TestTrue(TEXT("Follow toggle binds to the actual officer"), Pending[0].command.follow_enabled &&
            Pending[0].command.unit == battle::UnitId{10});
        TestTrue(TEXT("Manual order submits follow cancellation first"),
            Pending[1].command.kind == battle::CommandKind::SetRegimentFollowing && !Pending[1].command.follow_enabled);
        const auto& Packet = Pending[2];
        TestTrue(TEXT("Hold uses the controlled officer, current tick and high order priority"),
            Packet.command.kind == battle::CommandKind::IssueOrder && Packet.command.order.type == battle::OrderType::Hold &&
            Packet.command.order.issuer == battle::OfficerId{1} && Packet.command.order.priority >= 120 &&
            Packet.tick == World.tick && Packet.sequence == 6);
        auto Recipients = World;
        TestTrue(TEXT("BattleCore consumes the follow and hold packets"), bool(battle::Step(Recipients, Pending, {}, Navigation)));
        TestFalse(TEXT("Follow stays off as the hold order is delivered"),
            bool(battle::FindRegiment(Recipients, Friendly)->following_officer));
        TestEqual(TEXT("One hold order remains in delivery"), Recipients.pending_orders.size(), size_t(1));
    }

    Pending.clear();
    World.scenario.obstacles.push_back({3.f, -2.f, 4.f, 2.f, true});
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Attack, battle::Vec3{9.f, 0.f, 0.f}, Pending, Sequence);
    if (TestEqual(TEXT("Attack replaces a cursor target occluded from the officer"), Pending.size(), size_t(1)))
        TestTrue(TEXT("The chosen regiment is the visible enemy"), Pending[0].command.order.target_regiment == battle::RegimentId{3});

    Pending.clear();
    Regiment->panic = true;
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Withdraw, {}, Pending, Sequence);
    if (TestEqual(TEXT("Withdrawal under panic issues a rally order"), Pending.size(), size_t(1)))
        TestTrue(TEXT("Rally uses the regiment's rally point"),
            Pending[0].command.order.type == battle::OrderType::Rally &&
            Pending[0].command.order.point == Regiment->rally_point);

    Regiment->panic = false;
    Pending.clear();
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Column, {}, Pending, Sequence);
    if (TestEqual(TEXT("A regiment with space can form a travel column"), Pending.size(), size_t(1)))
        TestTrue(TEXT("Travel column uses one file and the compact spacing"),
            Pending[0].command.formation.columns == 1 &&
            Pending[0].command.formation.spacing_m == 1.6f);
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Line, {}, Pending, Sequence);
    if (TestEqual(TEXT("Another instruction can restore the line before the next tick"), Pending.size(), size_t(2)))
        TestTrue(TEXT("Restoring the line uses the authored formation"),
            Pending[1].command.formation.columns == 2 &&
            Pending[1].command.formation.spacing_m == 2.f);
    Pending.clear();
    Regiment->formation = {1, 1.6f, .3f};
    World.scenario.obstacles.push_back({-.4f, .8f, .4f, 1.2f, true});
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Line, {}, Pending, Sequence);
    TestTrue(TEXT("A column cannot reform into a wall beside it"), Pending.empty());

    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Skirmishers, {}, Pending, Sequence);
    QueueOrder(World, Navigation, Friendly, ERegimentPlayerOrder::Skirmishers, {}, Pending, Sequence);
    if (TestEqual(TEXT("Two skirmisher toggles reverse one another before the next tick"), Pending.size(), size_t(2)))
        TestTrue(TEXT("The initially deployed screen is recalled and then sent back"),
            !Pending[0].command.skirmishers_enabled && Pending[1].command.skirmishers_enabled);

    auto* Officer = battle::FindUnit(World, {10});
    Officer->pose.position = {};
    Officer->weapon.range = 2.f;
    battle::FindUnit(World, {20})->pose.position = {1.f, -1.f, 0.f};
    battle::FindUnit(World, {30})->pose.position = {10.f, -1.f, 0.f};
    battle::FindUnit(World, {31})->pose.position = {-.5f, 0.f, 0.f};
    TestEqual(TEXT("Personal attack chooses an enemy in reach and in front of the camera"),
        PersonalTarget(World, 0.f).value, uint64(20));
    TestEqual(TEXT("Turning the camera changes the attack direction"),
        PersonalTarget(World, battle::Pi).value, uint64(31));
    World.scenario.obstacles.push_back({.4f, -.7f, .6f, -.3f, true});
    TestFalse(TEXT("A blocked target never redirects a slash to the enemy behind the officer"),
        bool(PersonalTarget(World, 0.f)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerFollowSelectionTest, "RegimentBattle.OfficerFollowSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentOfficerFollowSelectionTest::RunTest(const FString& Parameters)
{
    using namespace RegimentOfficerOrders;
    using namespace battle::command;
    auto Scenario = MakeOfficerOrderWorld().scenario;
    Scenario.regiments[2].team = 0;
    battle::UnitSpec GroupOfficer;
    GroupOfficer.id = {40};
    Scenario.units.push_back(GroupOfficer);
    Scenario.officers.push_back({{4}, GroupOfficer.id, 1});
    auto& Hierarchy = Scenario.command_setup.emplace().hierarchy;
    Hierarchy.officers = { {{1}}, {{3}}, {{4}} };
    CommandNode Group;
    Group.id = {100};
    Group.level = Level::Group;
    Group.commander = {4};
    Group.children = {{101}, {103}};
    Hierarchy.nodes.push_back(Group);
    for (auto Id : {uint64(1), uint64(3)})
    {
        CommandNode Leaf;
        Leaf.id = {100 + Id};
        Leaf.parent = Group.id;
        Leaf.regiment = {Id};
        Leaf.commander = {Id};
        Hierarchy.nodes.push_back(Leaf);
    }
    auto Created = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("The fixture has a valid blue group"), bool(Created)))
        return false;
    auto& World = Created.value;
    TestEqual(TEXT("Default possession prefers a regiment officer"), FindOfficer(World, 0).value, uint64(10));
    TestEqual(TEXT("A group card selects the group officer"), FindOfficer(World, 100).value, uint64(40));
    TestEqual(TEXT("The group officer can command both subordinates"), Regiments(World, {40}).size(), size_t(2));
    TestTrue(TEXT("Fixture acquires the group officer"), battle::AcquireControl(World, {40}) == battle::Status::Ok);

    const battle::FlatNavigationService Navigation;
    {
        auto PreviewWorld = World;
        // The two independently plotted orders intersect halfway along each route.
        battle::FindRegiment(PreviewWorld, {1})->pose.position = {-5, 0, 0};
        battle::FindRegiment(PreviewWorld, {3})->pose.position = {0, -5, 0};
        auto Across = PreviewManeuver(PreviewWorld, Navigation, {1}, {5, 0, 0}, {}, false);
        auto Crossing = PreviewManeuver(PreviewWorld, Navigation, {3}, {0, 5, 0}, {}, false);
        std::vector<battle::CommandEnvelope> Commands;
        uint64 Next = 10;
        QueueManeuver(PreviewWorld, Across, battle::OrderType::MoveTo, battle::MovementPace::Walk,
            Commands, Next, Navigation);
        if (!TestEqual(TEXT("First plotted order is accepted"), Commands.size(), size_t(1)))
            return false;
        auto Before = Crossing;
        CheckManeuverConflicts(PreviewWorld, Navigation, Crossing, battle::OrderType::MoveTo,
            battle::MovementPace::Walk, Commands);
        TestTrue(TEXT("A simultaneous crossing is shown as blocked"), !Crossing.empty() &&
            Crossing.front().TrafficConflict && !Crossing.front().bReachable &&
            Crossing.front().TrafficConflict->Other == battle::RegimentId{1} &&
            Crossing.front().TrafficConflict->Seconds > 1.f);
        QueueManeuver(PreviewWorld, Before, battle::OrderType::MoveTo, battle::MovementPace::Walk,
            Commands, Next, Navigation);
        TestTrue(TEXT("Submission rechecks conflicts without consuming a sequence"),
            Commands.size() == 1 && Next == 11);
        battle::FindRegiment(PreviewWorld, {3})->speed = .2f;
        CheckManeuverConflicts(PreviewWorld, Navigation, Before, battle::OrderType::MoveTo,
            battle::MovementPace::Walk, Commands);
        TestTrue(TEXT("The crossing clears when the slower regiment arrives later"),
            !Before.empty() && Before.front().bReachable);
        QueueManeuver(PreviewWorld, Before, battle::OrderType::MoveTo, battle::MovementPace::Walk,
            Commands, Next, Navigation);
        TestEqual(TEXT("Both time-separated orders can be issued"), Commands.size(), size_t(2));
    }
    {
        auto PreviewWorld = World;
        battle::FindRegiment(PreviewWorld, {3})->pose.position = {0.f, 5.f, 0.f};
        const auto Plan = PreviewManeuver(PreviewWorld, Navigation, {1}, {0, 5, 0}, {}, true);
        if (TestEqual(TEXT("The group preview includes both subordinates"), Plan.size(), size_t(2)))
        {
            TestTrue(TEXT("The direct shift can pass through a vacated friendly footprint"),
                Plan[0].bReachable && Plan[1].bReachable && Plan[0].Goal.position == battle::Vec3{0, 5, 0}
                && Plan[1].Goal.position == battle::Vec3{0, 10, 0});
            std::vector<battle::CommandEnvelope> Batch;
            uint64 BatchSequence = 1;
            QueueManeuver(PreviewWorld, Plan, battle::OrderType::MoveTo, battle::MovementPace::Walk, Batch, BatchSequence, Navigation);
            if (TestEqual(TEXT("Both orders are issued"), Batch.size(), size_t(2)))
                TestTrue(TEXT("The accepted maneuver has one group and a common delivery tick"),
                    Batch[0].command.order.maneuver_group && Batch[0].command.order.maneuver_group == Batch[1].command.order.maneuver_group
                    && Batch[0].command.order.delivery_tick == Batch[1].command.order.delivery_tick);
        }
    }
    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 1;
    for (auto Id : {uint64(1), uint64(3), uint64(1)})
        QueueOrder(World, Navigation, {Id}, ERegimentPlayerOrder::Follow, {}, Pending, Sequence);
    TestTrue(TEXT("BattleCore consumes the three group follow packets"), bool(battle::Step(World, Pending, {}, Navigation)));
    TestEqual(TEXT("Switching back to a previously selected subgroup makes it follow again"),
        battle::FindRegiment(World, {1})->following_officer.value, uint64(40));
    TestFalse(TEXT("The other subgroup hands back to AI"), bool(battle::FindRegiment(World, {3})->following_officer));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentRankOrdersTest, "RegimentBattle.Officers.RankOrders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentRankOrdersTest::RunTest(const FString&)
{
    using namespace RegimentOfficerOrders;
    using Order = ERegimentPlayerOrder;
    using battle::RankPriority;
    const battle::FlatNavigationService Navigation;
    const battle::RegimentId Friendly{1};
    auto Setup = MakeOfficerOrderWorld().scenario;
    auto Foot = Setup.units[0];
    Foot.id = {12};
    Setup.units.push_back(Foot);
    Setup.units[1].ranged_weapon = battle::RangedWeapon::Bow;
    Setup.rules.mixed_rank_tactics = true;
    Setup.regiments[0].skirmisher_count = 0;
    Setup.regiments[0].formation.columns = 1;
    Setup.regiments[1].pose.position.x = 70.f;
    auto Created = battle::CreateWorld(Setup, 42);
    if (!TestTrue(TEXT("Mixed company deploys"), bool(Created)))
        return false;
    auto& World = Created.value;
    if (!TestTrue(TEXT("Possesses the company commander"), battle::AcquireControl(World, {10}) == battle::Status::Ok))
        return false;

    const auto State = [&](std::span<const battle::CommandEnvelope> Pending = {})
    {
        return DescribeOrder(World, *battle::FindRegiment(World, Friendly), Pending, false);
    };
    TestTrue(TEXT("The command panel offers rank choices for mixed infantry"), State().bCanSetRanks);
    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 1;
    TestFalse(TEXT("Foreign regiments cannot be given a rank order"),
        QueueOrder(World, Navigation, {2}, Order::RankRanged, {}, Pending, Sequence).IsEmpty());
    TestTrue(TEXT("The officer can put shooters in front"),
        QueueOrder(World, Navigation, Friendly, Order::RankRanged, {}, Pending, Sequence).IsEmpty());
    if (!TestEqual(TEXT("A single authorized order is queued"), Pending.size(), size_t(1)))
        return false;
    TestTrue(TEXT("The queued command specifies ranged front"),
        Pending.front().command.kind == battle::CommandKind::SetRankPriority &&
        Pending.front().command.rank_priority == RankPriority::RangedFront);
    TestTrue(TEXT("The sheet highlights the requested order while it is pending"),
        State(Pending).RankOrder == Order::RankRanged &&
        State(Pending).Delivery == ERegimentOrderDelivery::Queued);
    if (TestTrue(TEXT("The simulation receives the change"), bool(battle::Step(World, Pending, {}, Navigation))))
    {
        TestTrue(TEXT("The order is retained after the simulation step"),
            battle::FindRegiment(World, Friendly)->rank_priority == RankPriority::RangedFront);
        TestTrue(TEXT("The officer can hand rank decisions back to the AI"),
            State().RankOrder == Order::RankRanged);
        Pending.clear();
        QueueOrder(World, Navigation, Friendly, Order::RankAuto, {}, Pending, Sequence);
        TestTrue(TEXT("Automatic mode is queued for the same regiment"),
            Pending.size() == 1 && Pending.front().command.regiment == Friendly &&
            Pending.front().command.rank_priority == RankPriority::Automatic);
    }
    return !HasAnyErrors();
}

#endif
