#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Math/Box.h"
#include "Math/Transform.h"
#include "Misc/Optional.h"

class UObject;
class UMaterialInterface;
class UStaticMesh;

namespace battle { enum class BattlePlatform : uint8; enum class RangedWeapon : uint8; struct UnitSpec; }
enum class ERegimentEraVisual : uint8;

enum class ERegimentAncientPrimitive : uint8 { Block, BeveledBlock, Ellipsoid, Disc, Taper, Frustum };
enum class ERegimentAncientMaterial : uint8
{
    Paint, Bronze, Wood, Leather, Ivory, Hide, Dark, Skin, Iron, Linen, Scarlet, Green, Navy, Tartan,
    TartanStripe, Ochre, GreyWool, Wicker, Count
};
enum class ERegimentAncientFeature : uint8
{
    None, Driver, TowerCrew, EngineCrew, Ear, Tusk, Horse, Scythe, ThrowingArm, BowArm,
    Quiver, Greatcoat, Kilt, SlingPouch
};
enum class ERegimentAncientShield : uint8 { Aspis, Scutum, Wicker, Oval, Carthaginian, CavalryOval, Macedonian };

enum class ERegimentHistoricalFigure : uint8
{
    None, Legionary, Auxiliary, RomanCavalry, Numidian, Sarissa, Companion, Hoplite,
    Sparabara, PersianArcher, LooseBow, LooseSling, LooseJavelin,
    Fusilier, Grenadier, Chasseur, OldGuard, YoungGuard, RussianLine, RussianGrenadier, RussianJaeger,
    BritishLine, BritishGrenadier, BritishLight, Highlander
};

struct FRegimentAncientPart
{
    ERegimentAncientPrimitive Primitive;
    ERegimentAncientMaterial Material;
    FTransform Transform;
    ERegimentAncientFeature Feature;
};

struct FRegimentAncientFigure
{
    TArray<FRegimentAncientPart> Parts;
    FBox Bounds{ForceInit};
    bool bSimplified = false;

    int32 Count(ERegimentAncientFeature Feature) const;
};

// Geometry is in centimeters with +X forward. Paint is a swap-in faction material.
FRegimentAncientFigure BuildRegimentAncientPlatform(battle::BattlePlatform Platform);
FRegimentAncientFigure BuildRegimentAncientShield(ERegimentAncientShield Shield);
FRegimentAncientFigure BuildRegimentEraHeadgear(ERegimentEraVisual Style);
double RegimentEraFigureScale(ERegimentEraVisual Style);
ERegimentAncientShield RegimentAncientShieldStyle(FStringView Faction);
double RegimentAncientPikeLength(FStringView Faction);
ERegimentHistoricalFigure RegimentHistoricalProfile(FStringView Profile);
ERegimentHistoricalFigure RegimentHistoricalStyle(ERegimentHistoricalFigure Profile, FStringView Faction,
    const battle::UnitSpec& Unit, bool bAncient, bool bNapoleonic);
TOptional<ERegimentAncientShield> RegimentHistoricalShield(ERegimentHistoricalFigure Style);
double RegimentHistoricalSpearLength(ERegimentHistoricalFigure Style);
inline constexpr double RegimentHistoricalRiderHeight = 54.0;
FRegimentAncientFigure BuildRegimentHistoricalDress(ERegimentHistoricalFigure Style);
FRegimentAncientFigure BuildRegimentHistoricalProxy(ERegimentHistoricalFigure Style);
FRegimentAncientFigure BuildRegimentHistoricalTack(ERegimentHistoricalFigure Style);
FRegimentAncientFigure BuildRegimentLooseWeapon(battle::RangedWeapon Weapon);
UStaticMesh* MakeRegimentAncientMesh(UObject& Owner, const FRegimentAncientFigure& Figure,
    UMaterialInterface* MaterialParent, int32& PaintSlot);
