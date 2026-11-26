#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentTacticalKnowledge.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentTacticalKnowledgeTest, "RegimentBattle.TacticalKnowledge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentTacticalKnowledgeTest::RunTest(const FString&)
{
    using namespace battle::command;
    using namespace RegimentTacticalKnowledge;
    CommandState State;
    auto Add = [&State](uint64 Id, uint64 Parent, int32 Team)
    {
        auto& Node = State.hierarchy.nodes.emplace_back();
        Node.id = {Id};
        Node.parent = {Parent};
        Node.team = Team;
    };
    Add(1, 0, 0);
    Add(2, 1, 0);
    Add(3, 0, 1);
    TestTrue(TEXT("Own command appears in the player decision tree"), ShowsCommandNode(State.hierarchy.nodes[0], 0));
    TestFalse(TEXT("A hidden opposing command does not enter the tree"), ShowsCommandNode(State.hierarchy.nodes[2], 0));

    NodeRuntime Runtime;
    Runtime.node = {1};
    TestFalse(TEXT("An opposing regiment absent from the observer's information remains unknown"),
        bool(FindEnemyContact(State, Runtime, {3}, 160)));

    KnownContact Seen;
    Seen.regiment = {7};
    Seen.allegiance = Allegiance::Enemy;
    Seen.source = ObservationSource::Direct;
    Seen.observed_tick = 49;
    Seen.confidence = .9f;
    SituationReport Report;
    Report.sender = {2};
    Report.recipient = {1};
    Report.observed_tick = 140;
    Report.created_tick = 151;
    Report.situation.contacts.push_back(Seen);
    Runtime.reports.push_back(Report);

    const auto Delayed = FindEnemyContact(State, Runtime, {7}, 155);
    if (!TestTrue(TEXT("Delivered report is available to its recipient"), bool(Delayed)))
        return false;
    TestTrue(TEXT("Provenance identifies the reporting subordinate"), Delayed->Reporter == NodeId{2});
    TestEqual(TEXT("Use the contact observation tick rather than report creation or delivery"),
        Delayed->ObservedTick, battle::Tick(49));
    TestTrue(TEXT("A recently delivered report can already be stale"), Delayed->Kind == EContactKind::StaleReport);

    auto Fresh = Seen;
    Fresh.regiment = {8};
    Fresh.observed_tick = 155;
    Runtime.memory.remembered_contacts.push_back(Fresh);
    const auto Latest = FindEnemyContact(State, Runtime, {}, 155);
    if (!TestTrue(TEXT("Selects the newer unrelated contact"), bool(Latest)))
        return false;
    TestTrue(TEXT("The separate sighting is labeled firsthand"), Latest->Kind == EContactKind::Direct);
    const auto StillStale = FindEnemyContact(State, Runtime, {7}, 155);
    TestTrue(TEXT("New firsthand observations do not refresh another enemy's report"),
        StillStale && StillStale->Kind == EContactKind::StaleReport && StillStale->ObservedTick == 49);

    Runtime.reports[0].sender = {3};
    TestFalse(TEXT("A hostile node cannot inject a received own-command report"),
        bool(FindEnemyContact(State, Runtime, {7}, 155)));
    return !HasAnyErrors();
}
#endif
