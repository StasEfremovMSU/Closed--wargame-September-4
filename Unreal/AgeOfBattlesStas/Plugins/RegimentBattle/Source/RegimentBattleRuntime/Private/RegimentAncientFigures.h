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
enum class ERegimentEraShield : uint8;

enum class ERegimentAncientPrimitive : uint8 { Block, BeveledBlock, Ellipsoid, Disc, Taper, Frustum };
enum class ERegimentAncientMaterial : uint8
{
    Paint, Bronze, Wood, Leather, Ivory, Hide, Dark, Skin, Iron, Linen, Scarlet, Green, Navy, Tartan,
    TartanStripe, Ochre, GreyWool, Wicker, Count
};
enum class ERegimentAncientFeature : uint8
{
    None, Driver, TowerCrew, EngineCrew, Ear, Tusk, Horse, Scythe, ThrowingArm, BowArm,
    Quiver, Greatcoat, Kilt, SlingPouch, Kabuto, Jingasa, Sode, Lamellar, Sashimono, Matchlock
};
enum class ERegimentAncientShield : uint8
{
    Aspis, Scutum, Wicker, Oval, Carthaginian, CavalryOval, Macedonian,
    Egyptian, Kite, Pavise, RoundMedieval, AspisLambda, AspisRosette, AspisSun
};

enum class ERegimentHistoricalFigure : uint8
{
    None, Legionary, Auxiliary, RomanCavalry, Numidian, Sarissa, Companion, Hoplite,
    Sparabara, PersianArcher, LooseBow, LooseSling, LooseJavelin,
    Fusilier, Grenadier, Chasseur, OldGuard, YoungGuard, RussianLine, RussianGrenadier, RussianJaeger,
    BritishLine, BritishGrenadier, BritishLight, Highlander,
    SengokuSamurai, SengokuAshigaru, SengokuArcher, SengokuArquebusier, SengokuCavalry,
    Egyptian, Indian, Punic, Gallic, Hellenistic, Steppe, Iranian,
    AncientLightHorse, AncientHeavyHorse, SteppeRider, Cataphract,
    WesternMail, EasternMail, SteppeMedieval, MedievalKnight, MedievalSteppeHorse,
    GothicPlate, MedievalBowman, SwissPike, PaviseInfantry, LateKnight,
    PikeAndShot, PikeAndShotMissile, EasternEarlyModern, WingedHussar, EarlyModernHorse,
    NorthernInfantry, NorthernHorse, NapoleonicCentral, NapoleonicPolish, NapoleonicAustrian,
    NapoleonicIberian, NapoleonicHussar, NapoleonicCuirassier, NapoleonicLancer
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
double RegimentAncientSpearLength(double BaseLength, uint64 Id);
ERegimentAncientShield RegimentPaintedShieldStyle(ERegimentAncientShield Shield, FStringView Faction, uint64 Id);
ERegimentHistoricalFigure RegimentHistoricalProfile(FStringView Profile);
ERegimentHistoricalFigure RegimentHistoricalStyle(ERegimentHistoricalFigure Profile, FStringView Faction,
    const battle::UnitSpec& Unit, FStringView Era);
ERegimentHistoricalFigure RegimentSengokuStyle(const battle::UnitSpec& Unit);
TOptional<ERegimentAncientShield> RegimentHistoricalShield(ERegimentHistoricalFigure Style);
TOptional<ERegimentAncientShield> RegimentHistoricalShield(ERegimentHistoricalFigure Style, ERegimentEraShield FactionShield);
double RegimentHistoricalSpearLength(ERegimentHistoricalFigure Style);
inline constexpr double RegimentHistoricalRiderHeight = 54.0;
FRegimentAncientFigure BuildRegimentHistoricalDress(ERegimentHistoricalFigure Style);
FRegimentAncientFigure BuildRegimentHistoricalProxy(ERegimentHistoricalFigure Style);
FRegimentAncientFigure BuildRegimentHistoricalTack(ERegimentHistoricalFigure Style);
FRegimentAncientFigure BuildRegimentLooseWeapon(battle::RangedWeapon Weapon);
FRegimentAncientFigure BuildRegimentSengokuWeapon(ERegimentHistoricalFigure Style);
FRegimentAncientFigure BuildRegimentSengokuBanner();
UStaticMesh* MakeRegimentAncientMesh(UObject& Owner, const FRegimentAncientFigure& Figure,
    UMaterialInterface* MaterialParent, int32& PaintSlot, int32* HideSlot = nullptr);
