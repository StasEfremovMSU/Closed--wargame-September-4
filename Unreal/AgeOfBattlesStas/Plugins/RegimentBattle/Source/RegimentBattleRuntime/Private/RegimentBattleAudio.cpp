#include "RegimentBattleAudio.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RegimentAudioSettings.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
using RegimentAudioSettings::EBus;
using RegimentAudioSettings::GetVolume;

constexpr int32 MaximumCombatVoices = 7;
constexpr int32 MaximumVoices = 11;

enum ERange : int32 { Close, Field, Distant, Water };

struct FRange
{
    float Inner;
    float Falloff;
};

constexpr FRange Ranges[] = {{240.f, 2200.f}, {650.f, 9400.f}, {1450.f, 32000.f}, {320.f, 3200.f}};

struct FCue
{
    const TCHAR* Name;
    EBus Bus;
    float Gain;
    uint64 Cooldown;
    int32 VoiceLimit;
    double Separation;
    ERange Range;
    const TCHAR* Assets[3];
};

// Cooldowns use the simulation clock at 20 Hz. The voice budget also applies at accelerated playback.
constexpr FCue Cues[] = {
    {TEXT("steel"), EBus::Combat, .34f, 12, 3, 650., Field,
        {TEXT("Combat/Steel_Clash_01"), TEXT("Combat/Steel_Clash_02"), TEXT("Combat/Steel_Clash_03")}},
    {TEXT("bow_release"), EBus::Combat, .30f, 9, 3, 700., Field,
        {TEXT("Combat/Bow_Release_01"), TEXT("Combat/Bow_Release_02"), TEXT("Combat/Bow_Release_03")}},
    {TEXT("arrow_impact"), EBus::Combat, .27f, 10, 2, 480., Close,
        {TEXT("Combat/Arrow_Target_01"), TEXT("Combat/Arrow_Target_02")}},
    {TEXT("musket"), EBus::Combat, .46f, 18, 3, 1050., Distant,
        {TEXT("Combat/Musket_Shot_01")}},
    {TEXT("cannon"), EBus::Combat, .72f, 21, 3, 1050., Distant,
        {TEXT("Combat/Field_Cannon_01"), TEXT("Combat/Field_Cannon_02")}},
    {TEXT("catapult"), EBus::Combat, .55f, 18, 2, 800., Field,
        {TEXT("Siege/Catapult_Release_01")}},
    {TEXT("ballista"), EBus::Combat, .48f, 15, 2, 800., Field,
        {TEXT("Siege/Ballista_Release_01")}},
    {TEXT("cavalry_impact"), EBus::Combat, .49f, 15, 2, 800., Field,
        {TEXT("Combat/Cavalry_Impact_01")}},
    {TEXT("elephant"), EBus::Combat, .55f, 35, 2, 950., Field,
        {TEXT("Animals/Elephant_Asian_Call_01")}},
    {TEXT("officer_hit"), EBus::Combat, .55f, 2, 2, 0., Close,
        {TEXT("Combat/Officer_Hit_01")}},
    {TEXT("officer_parry"), EBus::Combat, .57f, 2, 2, 0., Close,
        {TEXT("Combat/Officer_Parry_01")}},
    {TEXT("attack_signal"), EBus::Commands, .57f, 34, 2, 1400., Field,
        {TEXT("Signals/Horn_Call_01")}},
    {TEXT("rally"), EBus::Commands, .59f, 45, 2, 1400., Field,
        {TEXT("Signals/Bugle_Phrase_01")}},
    {TEXT("retreat"), EBus::Commands, .49f, 36, 2, 1400., Field,
        {TEXT("Signals/Drum_Roll_01")}},
    {TEXT("regiment_signal"), EBus::Commands, .33f, 40, 1, 1200., Field,
        {TEXT("Signals/Drum_Hit_01")}},
    {TEXT("charge_shout"), EBus::Combat, .28f, 90, 1, 1000., Field,
        {TEXT("Ambience/Charge_Shouts_01")}},
    {TEXT("foot_grass_walk"), EBus::Combat, .33f, 4, 2, 0., Close,
        {TEXT("Movement/Foot_Grass_Walk_02"), TEXT("Movement/Foot_Grass_Walk_03")}},
    {TEXT("foot_grass_run"), EBus::Combat, .34f, 3, 2, 0., Close,
        {TEXT("Movement/Foot_Grass_Run_02"), TEXT("Movement/Foot_Grass_Run_03")}},
    {TEXT("foot_gravel_walk"), EBus::Combat, .32f, 4, 2, 0., Close,
        {TEXT("Movement/Foot_Gravel_Walk_01"), TEXT("Movement/Foot_Gravel_Walk_02")}},
    {TEXT("foot_gravel_run"), EBus::Combat, .32f, 3, 2, 0., Close,
        {TEXT("Movement/Foot_Gravel_Run_01"), TEXT("Movement/Foot_Gravel_Run_02")}},
    {TEXT("foot_mud_walk"), EBus::Combat, .34f, 4, 2, 0., Close,
        {TEXT("Movement/Foot_Mud_Walk_01"), TEXT("Movement/Foot_Mud_Walk_02")}},
    {TEXT("foot_mud_run"), EBus::Combat, .34f, 3, 2, 0., Close,
        {TEXT("Movement/Foot_Mud_Run_01"), TEXT("Movement/Foot_Mud_Run_02")}},
    {TEXT("gear"), EBus::Combat, .20f, 12, 1, 0., Close,
        {TEXT("Movement/Gear_Leather_01"), TEXT("Movement/Gear_Buckle_01")}},
    {TEXT("march"), EBus::Combat, .22f, 40, 2, 850., Field,
        {TEXT("Movement/Regiment_March_Short")}},
    {TEXT("horse_walk"), EBus::Combat, .32f, 32, 2, 850., Field,
        {TEXT("Movement/Horse_Walk_Short")}},
    {TEXT("horse_gallop"), EBus::Combat, .42f, 28, 2, 950., Field,
        {TEXT("Movement/Horse_Gallop_Short")}}
};
static_assert(UE_ARRAY_COUNT(Cues) == int32(ERegimentSound::Count));

constexpr const TCHAR* LoopAssets[] = {
    TEXT("Ambience/Forest_Wind_Loop"), TEXT("Ambience/Brook_Loop"),
    TEXT("Ambience/Distant_Melee_Battle_Loop"), TEXT("Ambience/Distant_BlackPowder_Battle")
};

USoundWave* LoadWave(const TCHAR* Relative)
{
    const FString Asset = FString::Printf(TEXT("/Game/Battle/Audio/%s.%s"), Relative, *FPaths::GetCleanFilename(FString(Relative)));
    return LoadObject<USoundWave>(nullptr, *Asset);
}

bool ShouldProbe()
{
    static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("RegimentAudioProbe"));
    return bProbe;
}
}

FRegimentBattleAudio::FRegimentBattleAudio() = default;

FRegimentBattleAudio::~FRegimentBattleAudio()
{
    for (const FVoice& Voice : Voices)
        if (auto* Component = Voice.Component.Get())
            Component->Stop();
    for (const FLoop* Loop : {&Wind, &River, &DistantMelee, &DistantGunpowder})
        if (auto* Component = Loop->Component.Get())
            Component->Stop();
}

bool FRegimentBattleAudio::CanPlay(UWorld& World) const
{
    return World.AllowAudioPlayback() && World.GetAudioDeviceRaw() && World.GetFirstPlayerController();
}

void FRegimentBattleAudio::Preload(UWorld& World)
{
    if (bLoaded || !CanPlay(World))
        return;

    bLoaded = true;
    Voices.Reserve(MaximumVoices);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cues); ++Index)
        for (const TCHAR* Path : Cues[Index].Assets)
            if (Path)
                if (USoundWave* Wave = LoadWave(Path))
                    Samples[Index].Emplace(Wave);

    FLoop* Loops[] = {&Wind, &River, &DistantMelee, &DistantGunpowder};
    static_assert(UE_ARRAY_COUNT(Loops) == UE_ARRAY_COUNT(LoopAssets));
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Loops); ++Index)
        if (USoundWave* Wave = LoadWave(LoopAssets[Index]))
        {
            Wave->bLooping = true;
            Loops[Index]->Sample.Reset(Wave);
        }
}

USoundAttenuation* FRegimentBattleAudio::GetAttenuation(UWorld& World, int32 Range)
{
    static_assert(UE_ARRAY_COUNT(Ranges) == AttenuationCount);
    auto& Profile = Attenuation[Range];
    if (!Profile.IsValid())
    {
        Profile.Reset(NewObject<USoundAttenuation>(&World));
        auto& Settings = Profile->Attenuation;
        Settings.bAttenuate = true;
        Settings.bSpatialize = true;
        Settings.AttenuationShapeExtents = FVector(Ranges[Range].Inner);
        Settings.FalloffDistance = Ranges[Range].Falloff;
    }
    return Profile.Get();
}

void FRegimentBattleAudio::Play(UWorld& World, ERegimentSound Sound, const FVector& Position, uint64 Source,
    uint64 Tick, bool bPersonal)
{
    if (bPaused || !CanPlay(World))
        return;

    const int32 Index = int32(Sound);
    const FCue& Cue = Cues[Index];
    const float BusGain = GetVolume(Cue.Bus);
    if (BusGain <= 0.f)
        return;
    auto& History = LastPlayed[Index];
    const auto* Previous = History.Find(Source);
    if (Previous && Tick >= Previous->Tick && Tick - Previous->Tick < Cue.Cooldown)
        return;

    FVector Listener;
    FRotator Rotation;
    World.GetFirstPlayerController()->GetPlayerViewPoint(Listener, Rotation);
    const double Distance = FVector::DistSquared(Listener, Position);
    if (Distance >= FMath::Square(Ranges[Cue.Range].Inner + Ranges[Cue.Range].Falloff))
        return;

    Preload(World);
    const auto& Variants = Samples[Index];
    if (Variants.IsEmpty())
        return;

    Voices.RemoveAll([](const FVoice& Voice) { return !Voice.Component.IsValid() || !Voice.Component->IsPlaying(); });
    const bool bPriority = Cue.Bus == EBus::Commands || bPersonal ||
        Sound == ERegimentSound::OfficerHit || Sound == ERegimentSound::OfficerParry;
    int32 CombatVoices = 0;
    int32 SameCueVoices = 0;
    int32 FurthestSame = INDEX_NONE;
    int32 FurthestCombat = INDEX_NONE;
    double SameDistance = Distance;
    double CombatDistance = Distance;
    for (int32 VoiceIndex = 0; VoiceIndex < Voices.Num(); ++VoiceIndex)
    {
        const auto& Voice = Voices[VoiceIndex];
        const double OtherDistance = FVector::DistSquared(Listener, Voice.Position);
        if (!Voice.bPriority)
        {
            ++CombatVoices;
            if (OtherDistance > CombatDistance || (bPriority && FurthestCombat == INDEX_NONE))
            {
                FurthestCombat = VoiceIndex;
                CombatDistance = OtherDistance;
            }
        }
        if (Voice.Sound != Sound)
            continue;

        ++SameCueVoices;
        if (FVector::DistSquared2D(Voice.Position, Position) < FMath::Square(Cue.Separation) &&
            (!bPersonal || Voice.bPriority))
            return;
        const bool bReplaceLowerPriority = bPersonal && !Voice.bPriority && FurthestSame == INDEX_NONE;
        if (bReplaceLowerPriority || ((!Voice.bPriority || Cue.Bus == EBus::Commands) && OtherDistance > SameDistance))
        {
            FurthestSame = VoiceIndex;
            SameDistance = OtherDistance;
        }
    }
    int32 Replace = INDEX_NONE;
    if (SameCueVoices >= Cue.VoiceLimit)
    {
        if (FurthestSame == INDEX_NONE)
            return;
        Replace = FurthestSame;
    }
    else if (Voices.Num() >= MaximumVoices || (!bPriority && CombatVoices >= MaximumCombatVoices))
    {
        if (FurthestCombat == INDEX_NONE)
            return;
        Replace = FurthestCombat;
    }
    if (Replace != INDEX_NONE)
    {
        Voices[Replace].Component->Stop();
        Voices.RemoveAtSwap(Replace, EAllowShrinking::No);
    }

    const uint32 Choice = HashCombineFast(GetTypeHash(Source), GetTypeHash(Tick));
    const int32 Variant = Previous ? (Previous->Variant + 1) % Variants.Num() : int32(Choice % Variants.Num());
    USoundWave* Wave = Variants[Variant].Get();
    const float Pitch = Cue.Bus == EBus::Commands ? 1.f : .97f + float((Choice >> 8) % 7) * .01f;
    const float Gain = Cue.Gain * (bPersonal ? 1.08f : 1.f);
    auto* Audio = UGameplayStatics::SpawnSoundAtLocation(&World, Wave, Position, FRotator::ZeroRotator,
        Gain * BusGain, Pitch, 0.f, GetAttenuation(World, Cue.Range));
    if (!Audio)
        return;

    History.Add(Source, {Tick, Variant});
    Voices.Add({Audio, Sound, Position, Gain, Gain * BusGain, bPriority});
    if (ShouldProbe())
        UE_LOG(LogTemp, Display, TEXT("RegimentAudio cue=%s sample=%s tick=%llu source=%llu xy=(%.0f,%.0f) spatial=1 playing=%d"),
            Cue.Name, *Wave->GetName(), static_cast<unsigned long long>(Tick), static_cast<unsigned long long>(Source),
            Position.X, Position.Y, int32(Audio->IsPlaying()));
}

void FRegimentBattleAudio::SetPaused(bool bInPaused)
{
    if (bPaused == bInPaused)
        return;
    bPaused = bInPaused;
    for (const FVoice& Voice : Voices)
        if (auto* Component = Voice.Component.Get())
            Component->SetPaused(bPaused);
    for (const FLoop* Loop : {&Wind, &River, &DistantMelee, &DistantGunpowder})
        if (auto* Component = Loop->Component.Get())
            Component->SetPaused(bPaused);
}

void FRegimentBattleAudio::UpdateLoop(UWorld& World, FLoop& Loop, float TargetGain, const FVector* Position)
{
    if (!Loop.Sample.IsValid() || bPaused)
        return;
    Loop.Gain = FMath::Lerp(Loop.Gain, TargetGain, .25f);
    auto* Component = Loop.Component.Get();
    if (TargetGain <= 0.f && (!Component || !Component->IsPlaying() || Loop.Gain < .003f))
    {
        if (Component)
            Component->Stop();
        Loop.Component.Reset();
        Loop.Gain = 0.f;
        return;
    }
    if (!Component || !Component->IsPlaying())
    {
        Component = Position ? UGameplayStatics::SpawnSoundAtLocation(&World, Loop.Sample.Get(), *Position,
            FRotator::ZeroRotator, Loop.Gain, 1.f, 0.f, GetAttenuation(World, Water))
            : UGameplayStatics::SpawnSound2D(&World, Loop.Sample.Get(), Loop.Gain);
        Loop.Component = Component;
        if (Component && ShouldProbe())
            UE_LOG(LogTemp, Display, TEXT("RegimentAudio ambience=%s spatial=%d playing=%d"),
                *Loop.Sample->GetName(), int32(Position != nullptr), int32(Component->IsPlaying()));
    }
    else
    {
        Component->SetVolumeMultiplier(Loop.Gain);
        if (Position)
            Component->SetWorldLocation(*Position);
    }
}

void FRegimentBattleAudio::UpdateAmbience(UWorld& World, const TOptional<FVector>& WaterPosition, bool bMelee,
    bool bGunpowder)
{
    if (!CanPlay(World))
        return;
    Preload(World);

    bool bCommand = false;
    for (FVoice& Voice : Voices)
    {
        const FCue& Cue = Cues[int32(Voice.Sound)];
        if (auto* Component = Voice.Component.Get(); Component && Component->IsPlaying())
        {
            const float Volume = GetVolume(Cue.Bus);
            const float Gain = Voice.Gain * Volume;
            if (!FMath::IsNearlyEqual(Voice.AppliedGain, Gain))
            {
                Component->SetVolumeMultiplier(Gain);
                Voice.AppliedGain = Gain;
            }
            bCommand |= Cue.Bus == EBus::Commands && Volume > 0.f && !bPaused;
        }
    }
    const float Gain = GetVolume(EBus::Ambience) * (bCommand ? .48f : 1.f);
    UpdateLoop(World, Wind, .15f * Gain);
    UpdateLoop(World, River, WaterPosition ? .24f * Gain : 0.f, WaterPosition ? &WaterPosition.GetValue() : nullptr);
    UpdateLoop(World, DistantMelee, bMelee && !bGunpowder ? .16f * Gain : 0.f);
    UpdateLoop(World, DistantGunpowder, bGunpowder ? .18f * Gain : 0.f);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattleAudioCatalogTest, "RegimentAudio.RecordedCatalog",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattleAudioCatalogTest::RunTest(const FString&)
{
    for (const FCue& Cue : Cues)
        for (const TCHAR* Path : Cue.Assets)
            if (Path)
                if (const USoundWave* Wave = LoadWave(Path); TestNotNull(Path, Wave))
                {
                    TestEqual(FString(Path) + TEXT(" is mono for spatial playback"), Wave->NumChannels, 1);
                    TestTrue(FString(Path) + TEXT(" has a positive duration"), Wave->GetDuration() > 0.f);
                }
    for (const TCHAR* Path : LoopAssets)
        if (const USoundWave* Wave = LoadWave(Path); TestNotNull(Path, Wave))
            TestTrue(FString(Path) + TEXT(" has enough material for a background loop"), Wave->GetDuration() > 8.f);
    return !HasAnyErrors();
}
#endif
