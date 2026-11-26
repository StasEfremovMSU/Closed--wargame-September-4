#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentFieldFeedback.h"
#include "RegimentMissionPreset.h"
#include "RegimentMissionScenario.h"
#include "battle/Control.h"
#include "battle/Simulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFieldFeedbackTest, "RegimentBattle.Presentation.CardAndFlag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFieldFeedbackTest::RunTest(const FString&)
{
    FRegimentFieldFeedback Feedback;
    battle::RegimentState Regiment;
    Regiment.id = {1};
    Regiment.ai_action = battle::OrderType::HoldLine;
    const FText Holding = Feedback.FlagState(Regiment, 90);
    TestFalse(TEXT("Holding a line has its own flag state"), Holding.IsEmpty());
    battle::BattleEvent Launch;
    Launch.kind = battle::EventKind::AttackLaunched;
    Launch.regiment = Regiment.id;
    Launch.tick = 91;
    Feedback.ObserveAttack(Launch);
    TestTrue(TEXT("Sword swings do not look like shooting"), Feedback.FlagState(Regiment, 91).EqualTo(Holding));
    Launch.projectile = true;
    Feedback.ObserveAttack(Launch);
    TestFalse(TEXT("An actual shot changes the banner"), Feedback.FlagState(Regiment, 92).EqualTo(Holding));
    TestTrue(TEXT("The flag returns to holding after the last shot"), Feedback.FlagState(Regiment, 400).EqualTo(Holding));
    Regiment.route.push_back({{2, 0, 0}, 0});
    TestTrue(TEXT("A regiment still moving into place is not labeled as holding"), Feedback.FlagState(Regiment, 400).IsEmpty());
    const FText Shooting = Feedback.FlagState(Regiment, 91);
    Regiment.panic = true;
    TestFalse(TEXT("New panic overrides the earlier shot"), Feedback.FlagState(Regiment, 92).EqualTo(Shooting));

    const auto Roster = MakeDefaultMissionRoster();
    const auto Opening = MakeMissionOpening(Roster, battle::MarchTerrain::River);
    auto Created = battle::CreateWorld(BuildMissionBattle(Roster, Opening), 37);
    if (!TestTrue(TEXT("Card test starts with campaign hierarchy"), bool(Created))) return false;
    auto& World = Created.value;
    std::vector<battle::CommandEnvelope> Pending;
    uint64 Sequence = 1;
    TestFalse(TEXT("Cannot transfer into an enemy officer"), Feedback.QueueCardOfficer(World, {7}, 7, Pending, Sequence));
    TestTrue(TEXT("Free camera can request the living regiment officer"), Feedback.QueueCardOfficer(World, {1}, 1, Pending, Sequence));
    TestFalse(TEXT("Pause leaves current camera and control unchanged"), bool(World.controlled));
    if (!TestEqual(TEXT("One possession waits for playback"), int32(Pending.size()), 1)) return false;
    const auto Expected = Pending.front().command.unit;
    auto Nav = MakeMissionNavigation(World.scenario.obstacles, {});
    if (!TestTrue(TEXT("Resume applies the queued command"), bool(battle::Step(World, Pending, {}, Nav)))) return false;
    TestTrue(TEXT("The actual officer on the card is possessed"), World.controlled == Expected);
    Pending.clear();
    TestFalse(TEXT("A regiment officer cannot jump to a peer via its card"),
        Feedback.QueueCardOfficer(World, {2}, 2, Pending, Sequence));
    TestTrue(TEXT("Denied transfer leaves queue empty"), Pending.empty());
    return !HasAnyErrors();
}
#endif
