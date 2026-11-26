#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentFigureLod.h"
#include "RegimentFallenPresentation.h"
#include "RegimentInstanceUpdates.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFigureLodBudgetTest, "RegimentBattle.Presentation.FigureBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFigureLodBudgetTest::RunTest(const FString& Parameters)
{
    TArray<FRegimentLodCandidate> Candidates;
    for (uint64 Id = 1; Id <= 10000; ++Id)
        Candidates.Add({Id, double(Id), false, Id == 10000});
    TSet<uint64> Rich;
    SelectRegimentRichFigures(Candidates, 160, 12, Rich);
    TestEqual(TEXT("A busy frame queues only the configured promotions"), Rich.Num(), 12);
    TestTrue(TEXT("The controlled figure takes priority at any distance"), Rich.Contains(10000));
    TestTrue(TEXT("The closest visible fighter is selected"), Rich.Contains(1));
    TestFalse(TEXT("A farther figure waits until a later frame"), Rich.Contains(12));

    for (auto& Candidate : Candidates)
        Candidate.bAlreadyRich = Candidate.Id <= 160;
    SelectRegimentRichFigures(Candidates, 160, 12, Rich);
    TestEqual(TEXT("Existing meshes and promotions share one bounded budget"), Rich.Num(), 160);
    TestFalse(TEXT("The farthest retained figure yields to the controlled one"), Rich.Contains(160));

    Candidates = {{1, 100.0, false, false}, {2, 106.0, true, false}};
    SelectRegimentRichFigures(Candidates, 1, 1, Rich);
    TestTrue(TEXT("A marginally closer new figure does not displace a retained mesh"), Rich.Contains(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentInstanceUpdatesTest, "RegimentBattle.Presentation.InstanceUpdates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentInstanceUpdatesTest::RunTest(const FString& Parameters)
{
    const TArray<FTransform> Previous{
        FTransform(FVector(10, 0, 0)), FTransform(FVector(20, 0, 0)),
        FTransform(FVector(30, 0, 0)), FTransform(FVector(40, 0, 0))};
    TArray<FTransform> Next = Previous;
    TestEqual(TEXT("Stationary figures do not need instance updates"),
        RegimentChangedInstances(Previous, Next).Count, 0);
    Next[1].SetTranslation(FVector(20, 0, .000001));
    auto Range = RegimentChangedInstances(Previous, Next);
    TestEqual(TEXT("Even a small move is sent to the renderer"), Range.First, 1);
    TestEqual(TEXT("The stationary prefix and suffix are skipped"), Range.Count, 1);
    Next[3].SetRotation(FQuat(FVector::UpVector, .5));
    Range = RegimentChangedInstances(Previous, Next);
    TestEqual(TEXT("A later change extends the update to the end"), Range.Count, 3);
    Next = Previous;
    Next.Pop();
    TestEqual(TEXT("Shrinking a batch leaves the common instances clean"),
        RegimentChangedInstances(Previous, Next).Count, 0);
    Next = Previous;
    Next.Add(FTransform(FVector(50, 0, 0)));
    TestEqual(TEXT("New instances are separate from changing the existing instances"),
        RegimentChangedInstances(Previous, Next).Count, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFallenRetentionTest, "RegimentBattle.Presentation.FallenRetention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFallenRetentionTest::RunTest(const FString&)
{
    FRegimentFallenProxies Fallen;
    TestFalse(TEXT("Empty pool never updates the mesh"), Fallen.TakeChanges(0.0));
    const FVector Hit(400, 22, 10);
    Fallen.Add(Hit, 30.0, false, 10);
    TestTrue(TEXT("Adding a body submits presentation once"), Fallen.TakeChanges(10.0));
    if (TestFalse(TEXT("Eight-second lifetime keeps the corpse at seven seconds"), Fallen.TakeChanges(150.0)))
        TestEqual(TEXT("Body is still at the corresponding casualty position"), Fallen.GetBodies().First().Location, Hit);
    TestTrue(TEXT("Body is removed at eight seconds"), Fallen.TakeChanges(170.0));
    TestTrue(TEXT("Expired bodies free render slots"), Fallen.GetBodies().IsEmpty());

    for (int32 Index = 0; Index <= FRegimentFallenProxies::MaxBodies; ++Index)
        Fallen.Add(FVector(Index, 0, 0), 0.0, true, 180);
    TestEqual(TEXT("Burst has a fixed upper bound"), Fallen.GetBodies().Num(), FRegimentFallenProxies::MaxBodies);
    TestEqual(TEXT("Burst replaces the oldest body"), Fallen.GetBodies().First().Location.X, 1.0);
    TestTrue(TEXT("Mounted bodies retain their presentation type"), Fallen.GetBodies().First().bMounted);
    return true;
}

#endif
