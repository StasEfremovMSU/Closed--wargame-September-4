#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattleNotices.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattleNoticesTest, "RegimentBattle.Presentation.BattleNotices",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattleNoticesTest::RunTest(const FString& Parameters)
{
    FRegimentBattleNotices Notices;
    battle::World World;
    auto& Receiver = World.regiments.emplace_back();
    Receiver.id = {1};
    Receiver.team = 1;
    Receiver.name = "Orange 1";
    auto& FirstAttacker = World.regiments.emplace_back();
    FirstAttacker.id = {2};
    FirstAttacker.name = "Blue 1";
    auto& SecondAttacker = World.regiments.emplace_back();
    SecondAttacker.id = {3};
    SecondAttacker.name = "Blue 2";
    for (uint64 Id = 1; Id <= 3; ++Id)
    {
        auto& Unit = World.units.emplace_back();
        Unit.id = {Id};
        Unit.regiment = {Id};
    }
    battle::BattleEvent Hit;
    Hit.kind = battle::EventKind::Hit;
    Hit.amount = 1.f;
    Hit.regiment = {1};
    Hit.tick = 10;
    Hit.source = {2};
    Hit.target = {1};
    const FVector Impact(120, 340, 50);
    Notices.ObserveHit(World, Hit, World.units.front(), Impact);
    Hit.aspect = battle::AttackAspect::Flank;
    Hit.projectile = true;
    Notices.ObserveHit(World, Hit, World.units.front(), Impact);
    TestEqual(TEXT("Frontal hits and projectiles produce no melee callout"), Notices.GetEntries().Num(), 0);
    Hit.projectile = false;
    Notices.ObserveHit(World, Hit, World.units.front(), Impact);
    ++Hit.tick;
    Notices.ObserveHit(World, Hit, World.units.front(), Impact + FVector(50, 0, 0));
    Hit.aspect = battle::AttackAspect::Rear;
    Notices.ObserveHit(World, Hit, World.units.front(), Impact);
    Hit.aspect = battle::AttackAspect::Flank;
    Hit.source = {3};
    Notices.ObserveHit(World, Hit, World.units.front(), Impact);
    if (!TestEqual(TEXT("Distinct attackers remain identifiable while repeat hits still group"), Notices.GetEntries().Num(), 3))
        return false;
    TestEqual(TEXT("The flank icon stays at the first actual hit"), Notices.GetEntries()[0].Location, Impact);
    TestEqual(TEXT("The right callout is used for a rear hit"), Notices.GetEntries()[1].Kind, ERegimentBattleNotice::Rear);
    TestEqual(TEXT("The first attacker is named"), Notices.GetEntries()[0].Actor.Name.ToString(), FString(TEXT("Blue 1")));
    TestEqual(TEXT("Color uses the attacker's own side"), Notices.GetEntries()[0].Actor.Team, 0);
    TestEqual(TEXT("The defender is named"), Notices.GetEntries()[0].Target.Name.ToString(), FString(TEXT("Orange 1")));
    TestEqual(TEXT("The defender retains its opposing side"), Notices.GetEntries()[0].Target.Team, 1);
    TestEqual(TEXT("The second attacker receives a distinct notice"), Notices.GetEntries()[2].Actor.Name.ToString(),
        FString(TEXT("Blue 2")));
    auto& Officer = World.units.emplace_back();
    Officer.id = {4};
    World.officers.emplace_back().unit = Officer.id;
    World.officers.back().id = {1};
    auto& Command = World.command_state.emplace().hierarchy.nodes.emplace_back();
    Command.commander = {1};
    Command.level = battle::command::Level::Army;
    Command.name = "Blue commander";
    Hit.source = Officer.id;
    Notices.ObserveHit(World, Hit, World.units.front(), Impact);
    if (!TestEqual(TEXT("Independent officer attacks do not disappear into a regiment's hits"), Notices.GetEntries().Num(), 4))
        return false;
    TestEqual(TEXT("An independent officer uses the staff title from the hierarchy"),
        Notices.GetEntries()[3].Actor.Name.ToString(), FString(TEXT("Blue commander")));
    Notices.Expire(11 + FRegimentBattleNotices::LifetimeTicks);
    TestEqual(TEXT("Old notices expire"), Notices.GetEntries().Num(), 0);

    battle::RegimentState Regiment;
    Regiment.id = {1};
    Regiment.team = 1;
    Regiment.name = "Orange 1";
    Regiment.units.push_back({1});
    const FVector Center(30, 90, 20);
    uint64 Tick = 120;
    Notices.ObserveRegiment(Regiment, Tick++, Center);
    Regiment.ai_action = battle::OrderType::Withdraw;
    Notices.ObserveRegiment(Regiment, Tick++, Center);
    TestEqual(TEXT("A withdrawal decision without a route is not yet an actual retreat"), Notices.GetEntries().Num(), 0);
    Regiment.route.push_back({{4, 0, 0}, 0});
    Notices.ObserveRegiment(Regiment, Tick++, Center);
    TestEqual(TEXT("A blocked withdrawal with a route is still stationary"), Notices.GetEntries().Num(), 0);
    const FVector Retreated = Center - FVector(12, 0, 0);
    Notices.ObserveRegiment(Regiment, Tick++, Retreated);
    Regiment.route.clear();
    Notices.ObserveRegiment(Regiment, Tick++, Retreated);
    Notices.ObserveRegiment(Regiment, Tick++, Retreated - FVector(12, 0, 0));
    Regiment.panic = true;
    Notices.ObserveRegiment(Regiment, Tick++, Center);
    Regiment.order.emplace();
    Regiment.order->type = battle::OrderType::Rally;
    Regiment.order->status = battle::OrderStatus::Active;
    Notices.ObserveRegiment(Regiment, Tick++, Center);
    Regiment.panic = false;
    Regiment.order->status = battle::OrderStatus::Completed;
    Regiment.ai_action = battle::OrderType::Hold;
    Notices.ObserveRegiment(Regiment, Tick++, Center);
    if (!TestEqual(TEXT("Retreat, rout, rally attempt and recovery emit once each"), Notices.GetEntries().Num(), 4))
        return false;
    TestEqual(TEXT("Organized retreat is distinct from panic"), Notices.GetEntries()[0].Kind, ERegimentBattleNotice::Retreat);
    TestEqual(TEXT("Retreat is called out at the first moving position"), Notices.GetEntries()[0].Location, Retreated);
    TestEqual(TEXT("Panic has its own field notice"), Notices.GetEntries()[1].Kind, ERegimentBattleNotice::Rout);
    TestEqual(TEXT("Rally attempt is tracked during panic"), Notices.GetEntries()[2].Kind, ERegimentBattleNotice::Rally);
    TestEqual(TEXT("Successful recovery has its own field notice"), Notices.GetEntries()[3].Kind, ERegimentBattleNotice::Rallied);
    TestEqual(TEXT("Regiment callout uses its location at the time of the event"), Notices.GetEntries()[3].Location, Center);
    for (const auto& Notice : Notices.GetEntries())
    {
        TestEqual(TEXT("Every discipline notice names the regiment"), Notice.Actor.Name.ToString(), FString(TEXT("Orange 1")));
        TestEqual(TEXT("Every discipline notice retains the regiment's side"), Notice.Actor.Team, 1);
        TestTrue(TEXT("Discipline changes have no attack recipient"), Notice.Target.Name.IsEmpty());
    }
    FRegimentBattleNotices Staff;
    auto& Fallen = World.units.front();
    World.officers.push_back({{2}, Fallen.id, 2});
    const auto LossTick = Tick;
    battle::BattleEvent StaffEvent;
    StaffEvent.kind = battle::EventKind::Died;
    StaffEvent.tick = LossTick;
    StaffEvent.target = Fallen.id;
    Staff.ObserveStaff(World, StaffEvent, Fallen, Center);
    if (!TestEqual(TEXT("A fallen officer generates a callout"), Staff.GetEntries().Num(), 1))
        return false;
    TestEqual(TEXT("Officer loss anchors on the fallen figure"), Staff.GetEntries()[0].Location, Center);
    StaffEvent.tick += 7;
    StaffEvent.kind = battle::EventKind::OfficerSucceeded;
    StaffEvent.source = World.units[1].id;
    Staff.ObserveStaff(World, StaffEvent, Fallen, Center);
    if (!TestEqual(TEXT("Resolved handover replaces its loss callout"), Staff.GetEntries().Num(), 1))
        return false;
    TestEqual(TEXT("Time to handover is the measured tick difference"), Staff.GetEntries()[0].TransferTicks, uint64(7));
    TestFalse(TEXT("New officer is identifiable"), Staff.GetEntries()[0].Target.Name.IsEmpty());
    TestEqual(TEXT("Successor notification carries the regiment"), Staff.GetEntries()[0].Actor.Team, 1);
    World.officers.back().role = 3;
    StaffEvent.tick++;
    StaffEvent.kind = battle::EventKind::Died;
    Staff.ObserveStaff(World, StaffEvent, Fallen, Center);
    TestEqual(TEXT("Standard loss has its own field notification"), Staff.GetEntries().Last().Kind,
        ERegimentBattleNotice::StandardLost);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCavalryNoticeTest, "RegimentBattle.Presentation.CavalryNotice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCavalryNoticeTest::RunTest(const FString& Parameters)
{
    battle::World World;
    World.regiments.emplace_back().id = {1};
    World.regiments.emplace_back().id = {2};
    World.regiments[0].name = "Infantry";
    World.regiments[0].team = 1;
    World.regiments[1].name = "Cavalry";
    for (uint64 Id = 1; Id <= 2; ++Id)
    {
        auto& Unit = World.units.emplace_back();
        Unit.id = {Id};
        Unit.regiment = {Id};
    }
    battle::BattleEvent Impact;
    Impact.kind = battle::EventKind::CavalryImpact;
    Impact.amount = 3.f;
    Impact.source = {2};
    Impact.target = {1};
    Impact.regiment = {1};
    Impact.aspect = battle::AttackAspect::Front;
    Impact.tick = 20;
    const FVector Point(250, 500, 50);
    FRegimentBattleNotices Notices;
    Notices.ObserveHit(World, Impact, World.units.front(), Point);
    ++Impact.tick;
    Impact.aspect = battle::AttackAspect::Flank;
    Notices.ObserveHit(World, Impact, World.units.front(), Point + FVector(200, 0, 0));
    Impact.kind = battle::EventKind::Hit;
    Impact.reason = "cavalry impact: flank";
    Notices.ObserveHit(World, Impact, World.units.front(), Point);
    if (!TestEqual(TEXT("The first confirmed cavalry hit gives one notice for the regiment's impact"),
        Notices.GetEntries().Num(), 1))
        return false;
    const auto& Notice = Notices.GetEntries()[0];
    TestEqual(TEXT("The attack has a distinct notice"), Notice.Kind, ERegimentBattleNotice::Charge);
    TestEqual(TEXT("Cavalry notice anchors on the first impacted figure"), Notice.Location, Point);
    TestEqual(TEXT("Cavalry is named in the field notice"), Notice.Actor.Name.ToString(), FString(TEXT("Cavalry")));
    TestEqual(TEXT("The receiving infantry is named"), Notice.Target.Name.ToString(), FString(TEXT("Infantry")));

    FRegimentBattleNotices RearCharge;
    Impact.kind = battle::EventKind::CavalryImpact;
    Impact.aspect = battle::AttackAspect::Rear;
    RearCharge.ObserveHit(World, Impact, World.units.front(), Point);
    if (TestEqual(TEXT("A rear charge produces an impact notice"), RearCharge.GetEntries().Num(), 1))
        TestEqual(TEXT("The notice preserves the rear impact's direction"),
            RearCharge.GetEntries()[0].Aspect, battle::AttackAspect::Rear);
    return true;
}
#endif
