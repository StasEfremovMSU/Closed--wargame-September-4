#pragma once

#include "CoreMinimal.h"

struct FRegimentHorseCoat
{
    FLinearColor Body;
    FLinearColor Points;
};

struct FRegimentFigureAppearance
{
    FLinearColor Skin;
    FRegimentHorseCoat Coat;
    float ClothShade;
    float LeatherShade;
};

inline uint32 RegimentFigureHash(uint64 Id)
{
    // Mix both halves of the ID so adjacent soldiers and alternate ranks get independent looks.
    Id ^= Id >> 33;
    Id *= 0xff51afd7ed558ccdull;
    Id ^= Id >> 33;
    Id *= 0xc4ceb9fe1a85ec53ull;
    Id ^= Id >> 33;
    return uint32(Id);
}

inline FRegimentFigureAppearance RegimentFigureAppearance(uint64 Id)
{
    static const FLinearColor SkinTones[] = {
        FLinearColor::FromSRGBColor(FColor(236, 188, 159)),
        FLinearColor::FromSRGBColor(FColor(215, 161, 124)),
        FLinearColor::FromSRGBColor(FColor(193, 143, 106)),
        FLinearColor::FromSRGBColor(FColor(161, 112, 76)),
        FLinearColor::FromSRGBColor(FColor(133, 88, 60)),
        FLinearColor::FromSRGBColor(FColor(106, 65, 44)),
        FLinearColor::FromSRGBColor(FColor(85, 52, 39))
    };
    static const auto Coat = [](FColor Body, FColor Points)
    {
        return FRegimentHorseCoat{FLinearColor::FromSRGBColor(Body), FLinearColor::FromSRGBColor(Points)};
    };
    static const FRegimentHorseCoat Coats[] = {
        Coat(FColor(124, 70, 39), FColor(43, 32, 31)),    // Bay
        Coat(FColor(157, 84, 48), FColor(119, 59, 38)),   // Chestnut
        Coat(FColor(46, 42, 40), FColor(32, 29, 29)),     // Black
        Coat(FColor(169, 165, 156), FColor(107, 107, 103)), // Gray
        Coat(FColor(181, 147, 91), FColor(59, 44, 35)),   // Dun
        Coat(FColor(93, 55, 43), FColor(45, 32, 30))      // Dark bay
    };
    const uint32 Variation = RegimentFigureHash(Id);
    const float ClothShade = FMath::Lerp(.89f, 1.10f, float((Variation >> 16) & 0xff) / 255.f);
    const float LeatherShade = FMath::Lerp(.84f, 1.16f, float(Variation >> 24) / 255.f);
    return {
        SkinTones[(Variation & 0xff) % UE_ARRAY_COUNT(SkinTones)],
        Coats[((Variation >> 8) & 0xff) % UE_ARRAY_COUNT(Coats)],
        ClothShade, LeatherShade
    };
}

// Matches the surface profile input of M_RegimentFigureParts.
enum class ERegimentFigureSurface : uint8
{
    Cloth, Leather, Wood, Metal
};

inline constexpr int32 RegimentFigurePartDataCount = 4;
