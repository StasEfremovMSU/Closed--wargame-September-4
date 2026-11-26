#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UAudioComponent;
class USoundAttenuation;
class USoundWave;
class UWorld;

enum class ERegimentSound : uint8
{
    Steel, Bow, ArrowImpact, Musket, Cannon, Catapult, Ballista,
    Cavalry, ElephantImpact, OfficerHit, OfficerParry,
    AttackSignal, Rally, Retreat, RegimentSignal, ChargeShout,
    GrassWalk, GrassRun, GravelWalk, GravelRun, MudWalk, MudRun, Gear,
    March, HorseWalk, HorseGallop,
    Count
};

class FRegimentBattleAudio
{
public:
    FRegimentBattleAudio();
    ~FRegimentBattleAudio();

    bool CanPlay(UWorld& World) const;
    void Preload(UWorld& World);
    void Play(UWorld& World, ERegimentSound Sound, const FVector& Position, uint64 Source, uint64 Tick,
        bool bPersonal = false);
    void SetPaused(bool bPaused);
    void UpdateAmbience(UWorld& World, const TOptional<FVector>& Water, const TOptional<FVector>& Melee, bool bGunpowder);

private:
    struct FSourcePlayback
    {
        uint64 Tick;
        int32 Variant;
    };

    struct FVoice
    {
        TWeakObjectPtr<UAudioComponent> Component;
        ERegimentSound Sound;
        FVector Position;
        float Gain;
        float AppliedGain;
        bool bPriority;
    };

    struct FLoop
    {
        TStrongObjectPtr<USoundWave> Sample;
        TWeakObjectPtr<UAudioComponent> Component;
        float Gain = 0.f;
    };

    USoundAttenuation* GetAttenuation(UWorld& World, int32 Range);
    void UpdateLoop(UWorld& World, FLoop& Loop, float TargetGain, const FVector* Position = nullptr, int32 Range = 0);

    static constexpr int32 AttenuationCount = 4;
    TArray<TStrongObjectPtr<USoundWave>> Samples[int32(ERegimentSound::Count)];
    TMap<uint64, FSourcePlayback> LastPlayed[int32(ERegimentSound::Count)];
    TArray<FVoice> Voices;
    TStrongObjectPtr<USoundAttenuation> Attenuation[AttenuationCount];
    FLoop Wind, River, DistantMelee, DistantGunpowder;
    bool bLoaded = false;
    bool bPaused = false;
};
