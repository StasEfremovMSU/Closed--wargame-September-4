#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "RegimentBattleAudio.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectIterator.h"

namespace
{
// The session would normally own the cue. Keep it alive for its full two-note phrase here.
class FWaitForRegimentSignal final : public IAutomationLatentCommand
{
public:
    FWaitForRegimentSignal(TSharedRef<FRegimentBattleAudio> InAudio, double Seconds)
        : Audio(MoveTemp(InAudio)), EndTime(FPlatformTime::Seconds() + Seconds) {}
    bool Update() override { return FPlatformTime::Seconds() >= EndTime; }
private:
    TSharedRef<FRegimentBattleAudio> Audio;
    double EndTime;
};

UWorld* FindPlayingWorld()
{
    for (const auto& Context : GEngine->GetWorldContexts())
        if (UWorld* World = Context.World(); World && World->IsGameWorld() && World->GetFirstPlayerController())
            return World;
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSpatialRallyAudioTest, "RegimentAudio.SpatialRallyPlayback",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentSpatialRallyAudioTest::RunTest(const FString&)
{
    UWorld* PlayWorld = FindPlayingWorld();
    if (!TestNotNull(TEXT("Run in a playing map with an audio device"), PlayWorld)) return false;
    FVector Listener;
    FRotator Facing;
    PlayWorld->GetFirstPlayerController()->GetPlayerViewPoint(Listener, Facing);
    const FVector Position = Listener + Facing.Quaternion().GetRightVector() * 327.f;
    auto Audio = MakeShared<FRegimentBattleAudio>();
    Audio->Play(*PlayWorld, ERegimentSound::Rally, Position, 7361, 128);
    for (TObjectIterator<UAudioComponent> Voice; Voice; ++Voice)
    {
        if (Voice->GetWorld() != PlayWorld || !Voice->GetComponentLocation().Equals(Position, .1) || !Voice->IsPlaying())
            continue;
        const auto* Wave = Cast<USoundWave>(Voice->GetSound());
        const auto* Settings = Voice->GetAttenuationSettingsToApply();
        if (!TestNotNull(TEXT("Rally plays a recorded signal at its regiment position"), Wave) ||
            !TestNotNull(TEXT("Rally uses world attenuation"), Settings)) return false;
        TestTrue(TEXT("The signal uses a recorded asset included in the game"), Wave->GetPathName().StartsWith(TEXT("/Game/Battle/Audio/Signals/")));
        TestTrue(TEXT("The emitted voice is spatialized"), Settings->bSpatialize && Settings->bAttenuate);
        TestTrue(TEXT("The signal has both phases of the rally phrase"), Wave->Duration > .4f);
        ADD_LATENT_AUTOMATION_COMMAND(FWaitForRegimentSignal(Audio, Wave->Duration + .2f));
        return !HasAnyErrors();
    }
    AddError(TEXT("No playing audio component at the commanded rally point."));
    return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattleAudioMixTest, "RegimentAudio.CombatMix",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattleAudioMixTest::RunTest(const FString&)
{
    UWorld* PlayWorld = FindPlayingWorld();
    if (!TestNotNull(TEXT("Run in a playing map with an audio device"), PlayWorld)) return false;
    FVector Listener;
    FRotator Facing;
    PlayWorld->GetFirstPlayerController()->GetPlayerViewPoint(Listener, Facing);
    FRegimentBattleAudio Audio;
    const FVector Contact = Listener + FVector(347.0, 193.0, -15.0);
    const FVector SecondContact = Contact + FVector(2360.0, 0.0, 0.0);
    const FVector Rally = Contact + FVector(81.0, 0.0, 0.0);
    for (int32 Regiment = 0; Regiment < 5; ++Regiment)
        Audio.Play(*PlayWorld, ERegimentSound::Steel, Contact + FVector(0, Regiment * 7.0, 0), 8001 + Regiment, 155);
    Audio.Play(*PlayWorld, ERegimentSound::Steel, SecondContact, 8010, 155);
    Audio.Play(*PlayWorld, ERegimentSound::Rally, Rally, 8011, 155);

    int32 LocalCombat = 0;
    bool bSecondContact = false;
    bool bRally = false;
    for (TObjectIterator<UAudioComponent> Voice; Voice; ++Voice)
    {
        if (Voice->GetWorld() != PlayWorld || !Voice->IsPlaying())
            continue;
        const FVector Position = Voice->GetComponentLocation();
        LocalCombat += FVector::DistSquared(Contact, Position) < FMath::Square(36.0) ? 1 : 0;
        bSecondContact |= Position.Equals(SecondContact, .1);
        bRally |= Position.Equals(Rally, .1);
    }
    TestEqual(TEXT("Simultaneous steel strikes at one contact play as one cue"), LocalCombat, 1);
    TestTrue(TEXT("A separate contact still has its own cue"), bSecondContact);
    TestTrue(TEXT("A tactical cue can play alongside contact"), bRally);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentRecordedCombatSeparationTest, "RegimentAudio.RecordedCombatSeparation",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentRecordedCombatSeparationTest::RunTest(const FString&)
{
    UWorld* World = FindPlayingWorld();
    if (!TestNotNull(TEXT("Run in a playing map with an audio device"), World))
        return false;
    FVector Listener;
    FRotator Facing;
    World->GetFirstPlayerController()->GetPlayerViewPoint(Listener, Facing);
    FRegimentBattleAudio Audio;
    const struct
    {
        ERegimentSound Sound;
        const TCHAR* Asset;
        FVector Offset;
    } Examples[] = {
        {ERegimentSound::Musket, TEXT("Musket_Shot"), {200., 120., 0.}},
        {ERegimentSound::Cannon, TEXT("Field_Cannon"), {-440., 100., 0.}},
        {ERegimentSound::Bow, TEXT("Bow_Release"), {350., -170., 0.}},
        {ERegimentSound::ArrowImpact, TEXT("Arrow_Target"), {-220., -210., 0.}},
        {ERegimentSound::Catapult, TEXT("Catapult_Release"), {-90., 520., 0.}},
        {ERegimentSound::Ballista, TEXT("Ballista_Release"), {480., 490., 0.}}
    };
    for (const auto& Example : Examples)
    {
        const FVector Position = Listener + Example.Offset;
        Audio.Play(*World, Example.Sound, Position, 8035, 429);
        const UAudioComponent* Active = nullptr;
        for (TObjectIterator<UAudioComponent> Voice; Voice; ++Voice)
            if (Voice->GetWorld() == World && Voice->GetComponentLocation().Equals(Position, .1) &&
                Voice->IsPlaying() && Voice->GetSound() && Voice->GetSound()->GetName().StartsWith(Example.Asset))
            {
                Active = *Voice;
                break;
            }
        TestNotNull(FString(Example.Asset) + TEXT(" plays a separate positional recording"), Active);
    }

    Audio.UpdateAmbience(*World, Listener + FVector(125., -530., 0.), true, false);
    bool bWater = false;
    bool bMelee = false;
    bool bPowder = false;
    for (TObjectIterator<UAudioComponent> Voice; Voice; ++Voice)
        if (Voice->GetWorld() == World && Voice->IsPlaying() && Voice->GetSound())
        {
            const FString Name = Voice->GetSound()->GetName();
            bWater |= Name == TEXT("Brook_Loop") &&
                Voice->GetComponentLocation().Equals(Listener + FVector(125., -530., 0.), .1);
            bMelee |= Name == TEXT("Distant_Melee_Battle_Loop");
            bPowder |= Name == TEXT("Distant_BlackPowder_Battle");
        }
    TestTrue(TEXT("River is a positional ambience source"), bWater);
    TestTrue(TEXT("Melee plays a distant battle bed"), bMelee);
    TestFalse(TEXT("A battle with no shots plays no powder bed"), bPowder);
    return !HasAnyErrors();
}

#endif
