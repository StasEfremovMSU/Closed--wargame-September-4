#include "RegimentSmokePresentation.h"

#include "Components/MaterialBillboardComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
UTexture2D* MakeSmokeTexture()
{
    constexpr int32 Size = 64;
    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(Size * Size);
    for (int32 Y = 0; Y < Size; ++Y)
        for (int32 X = 0; X < Size; ++X)
        {
            const FVector2D Point(2.0 * (X + .5) / Size - 1.0, 2.0 * (Y + .5) / Size - 1.0);
            const double Grain = FMath::PerlinNoise2D(Point * 3.75 + FVector2D(24.1, 63.7));
            const double Envelope = FMath::Max(0.0, 1.0 - Point.SizeSquared());
            const double Opacity = FMath::Clamp(Envelope * Envelope * (.64 + .48 * Grain), 0.0, 1.0);
            const uint8 Shade = uint8(FMath::Clamp(223.0 + 22.0 * Grain, 0.0, 255.0));
            Pixels[Y * Size + X] = FColor(Shade, Shade, Shade, uint8(FMath::RoundToInt(255.0 * Opacity)));
        }
    const TConstArrayView64<uint8> Bytes(reinterpret_cast<const uint8*>(Pixels.GetData()), Size * Size * sizeof(FColor));
    auto* Result = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None, Bytes);
    if (Result)
    {
        Result->NeverStream = true;
        Result->UpdateResource();
    }
    return Result;
}
}

FRegimentSmokePresentation::~FRegimentSmokePresentation()
{
    Clear();
}

void FRegimentSmokePresentation::Update(AActor& Owner, const FVector& Origin, const battle::World& World,
    const FRegimentEffectView& View, double PostBattleTicks)
{
    Effects.Collect(World, Origin, View, Sprites, PostBattleTicks);
    if (!bTriedResources && !Sprites.IsEmpty())
    {
        bTriedResources = true;
        BaseMaterial.Reset(LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent")));
        if (BaseMaterial.IsValid())
            Texture.Reset(MakeSmokeTexture());
    }
    if (!BaseMaterial.IsValid() || !Texture.IsValid())
        return;

    TBitArray<TInlineAllocator<2>> Visible(false, FRegimentBattlefieldEffects::MaxPuffs);
    for (const auto& Sprite : Sprites)
    {
        Visible[Sprite.Slot] = true;
        auto& Puff = Pool[Sprite.Slot];
        auto* Billboard = Puff.Billboard.Get();
        auto* Material = Puff.Material.Get();
        if (!Billboard || !Material)
        {
            Billboard = NewObject<UMaterialBillboardComponent>(&Owner, NAME_None, RF_Transient);
            Material = UMaterialInstanceDynamic::Create(BaseMaterial.Get(), Billboard);
            Material->SetTextureParameterValue(TEXT("SlateUI"), Texture.Get());
            Material->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
            Billboard->SetMobility(EComponentMobility::Movable);
            Billboard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Billboard->SetCanEverAffectNavigation(false);
            Billboard->SetCastShadow(false);
            Billboard->SetAbsolute(true, true, true);
            Owner.AddInstanceComponent(Billboard);
            Billboard->SetupAttachment(Owner.GetRootComponent());
            FMaterialSpriteElement Element;
            Element.Material = Material;
            Element.BaseSizeX = Sprite.HalfWidth;
            Element.BaseSizeY = Sprite.HalfHeight;
            Billboard->SetElements({Element});
            Billboard->SetVisibility(false);
            Billboard->RegisterComponent();
            Puff = {Billboard, Material};
        }
        if (Puff.Tint != Sprite.Tint)
        {
            Material->SetVectorParameterValue(TEXT("TintColorAndOpacity"), Sprite.Tint);
            Puff.Tint = Sprite.Tint;
        }
        Billboard->SetWorldLocation(Sprite.Center);
        if (!Puff.bVisible)
            Billboard->SetVisibility(true);
        Puff.bVisible = true;
    }
    for (int32 Slot = 0; Slot < FRegimentBattlefieldEffects::MaxPuffs; ++Slot)
    {
        auto& Puff = Pool[Slot];
        if (Puff.bVisible && !Visible[Slot])
        {
            if (auto* Billboard = Puff.Billboard.Get())
                Billboard->SetVisibility(false);
            Puff.bVisible = false;
        }
    }
}

void FRegimentSmokePresentation::Clear()
{
    for (auto& Puff : Pool)
    {
        if (auto* Billboard = Puff.Billboard.Get())
            Billboard->DestroyComponent();
        Puff = {};
    }
    Effects.Reset();
    Sprites.Reset();
    Texture.Reset();
    BaseMaterial.Reset();
    bTriedResources = false;
}
