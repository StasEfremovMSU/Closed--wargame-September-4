#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

struct FConvexVolume;
namespace battle
{
enum class MarchTerrain;
struct World;
}

enum class ERegimentEffectKind : uint8 { Musket, Cannon, Dust };

struct FRegimentEffectView
{
    FVector Location;
    const FConvexVolume* Frustum = nullptr;
    TOptional<FVector> Observer;
};

struct FRegimentEffectSprite
{
    int32 Slot;
    ERegimentEffectKind Kind;
    FVector Center;
    float HalfWidth;
    float HalfHeight;
    FLinearColor Tint;
};

class FRegimentBattlefieldEffects
{
public:
    static constexpr int32 MusketLimit = 36;
    static constexpr int32 CannonLimit = 10;
    static constexpr int32 DustLimit = 12;
    static constexpr int32 MaxPuffs = MusketLimit + CannonLimit + DustLimit;

    void ObserveStep(const battle::World& World, const FVector& Origin, battle::MarchTerrain Terrain, const FVector& Camera);
    void Collect(const battle::World& World, const FVector& Origin, const FRegimentEffectView& View,
        TArray<FRegimentEffectSprite>& Sprites, double PostBattleTicks = 0.0) const;
    void Reset();

private:
    struct FBurst
    {
        FVector Location = FVector::ZeroVector;
        FVector Travel = FVector::ZeroVector;
        uint64 Born = 0;
        uint64 Regiment = 0;
        bool bActive = false;
    };

    void AddBurst(ERegimentEffectKind Kind, FBurst Burst, const FVector& Camera);
    void FindCavalry(const battle::World& World);
    void ObserveCavalry(const battle::World& World, const FVector& Origin, battle::MarchTerrain Terrain, const FVector& Camera);

    TStaticArray<FBurst, MaxPuffs> Bursts;
    TMap<uint64, FVector2D> PreviousCavalryCenters;
    TOptional<uint64> LastStep;
    TOptional<uint64> LastCavalrySample;
};
