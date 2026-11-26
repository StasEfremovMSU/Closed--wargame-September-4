#pragma once

#include "CoreMinimal.h"
#include "RegimentBattlefieldEffects.h"
#include "UObject/StrongObjectPtr.h"

class AActor;
class UMaterialBillboardComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class FRegimentSmokePresentation
{
public:
    ~FRegimentSmokePresentation();
    void ObserveStep(const battle::World& World, const FVector& Origin, battle::MarchTerrain Terrain, const FVector& Camera)
    {
        Effects.ObserveStep(World, Origin, Terrain, Camera);
    }
    void Update(AActor& Owner, const FVector& Origin, const battle::World& World, const FRegimentEffectView& View,
        double PostBattleTicks);
    bool HasVisiblePuffs() const { return !Sprites.IsEmpty(); }
    void Clear();

private:
    struct FPuff
    {
        TWeakObjectPtr<UMaterialBillboardComponent> Billboard;
        TWeakObjectPtr<UMaterialInstanceDynamic> Material;
        FLinearColor Tint = FLinearColor::Transparent;
        bool bVisible = false;
    };

    FRegimentBattlefieldEffects Effects;
    TArray<FRegimentEffectSprite> Sprites;
    TStaticArray<FPuff, FRegimentBattlefieldEffects::MaxPuffs> Pool;
    TStrongObjectPtr<UMaterialInterface> BaseMaterial;
    TStrongObjectPtr<UTexture2D> Texture;
    bool bTriedResources = false;
};
