#pragma once

#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"
#include "Math/Color.h"
#include "Misc/Guid.h"
#include "Misc/Optional.h"
#include "RegimentMissionRoster.h"

enum class ERegimentEraRole : uint8 { Swords, Archers, Spearmen, Mounted, Count };
enum class ERegimentEraShield : uint8 { Round, Rectangular, Kite };
enum class ERegimentMountedWeapon : uint8 { Default, Blade, Lance };
enum class ERegimentEraVisual : uint8
{
    Classic, Plate, PikeShot, Eastern, Sengoku, Tricorn, Shako,
    Elven, Orcish, Undead, Dwarven, Giant, Arcane, Count
};

struct FRegimentMountedJavelin
{
    battle::WeaponSpec Weapon;
    uint32 Stock = 0;
};

struct FRegimentEraTroop
{
    FString Name;
    FString Description;
    float Health = 1.f;
    float Speed = 2.f;
    float Armour = 0.f;
    battle::WeaponSpec Weapon;
    battle::CavalryClass Cavalry = battle::CavalryClass::None;
    ERegimentMountedWeapon MountedWeapon = ERegimentMountedWeapon::Default;
    battle::RangedWeapon RangedWeapon = battle::RangedWeapon::None;
    uint32 Ammunition = 0;
    bool bBayonetMusket = false;
    battle::BattlePlatform Platform = battle::BattlePlatform::None;
    TOptional<FRegimentMountedJavelin> MountedJavelin;
};

struct FRegimentEraInfantryProfile
{
    FRegimentEraTroop Troop;
    ERegimentEraRole Role = ERegimentEraRole::Swords;
    bool bLooseOrder = false;
    TOptional<FRegimentEraTroop> Pilum;
};

struct FRegimentEraFaction
{
    FString Id;
    FString Name;
    ERegimentEraShield Shield = ERegimentEraShield::Round;
    ERegimentEraVisual Visual = ERegimentEraVisual::Classic;
    FLinearColor Heraldry = FLinearColor::White;
    TStaticArray<FRegimentEraTroop, int32(ERegimentEraRole::Count)> Troops;
    TStaticArray<TOptional<FRegimentEraTroop>, int32(ERegimentMountClass::Count)> MountVariants;
    TStaticArray<TOptional<FRegimentEraTroop>, int32(ERegimentRangedClass::Count)> RangedVariants;
    TStaticArray<TOptional<FRegimentEraTroop>, MissionPlatformTypeCount> Platforms;
    TMap<FString, FRegimentEraInfantryProfile> InfantryProfiles;
};

struct FRegimentEraDefinition
{
    FString Id;
    FString Name;
    FString ProfileId;
    int32 Version = 0;
    battle::Rules Rules;
    TArray<FRegimentEraFaction> Factions;
    FGuid MissionId;
    FString MissionName;
    FString Briefing;
    FString BlueFaction;
    FString OrangeFaction;
    battle::MarchTerrain Terrain = battle::MarchTerrain::OpenField;
    FMissionRoster Roster;
};

namespace RegimentEraCatalog
{
inline constexpr const TCHAR* MountedClassIds[] = { TEXT("default"), TEXT("light"), TEXT("heavy") };
inline constexpr const TCHAR* RangedClassIds[] = { TEXT("default"), TEXT("bow"), TEXT("crossbow"), TEXT("sling"),
    TEXT("javelin"), TEXT("gunpowder"), TEXT("musket"), TEXT("arcane") };
inline constexpr const TCHAR* PlatformIds[] = {
    TEXT("none"), TEXT("indian_elephant"), TEXT("african_elephant"), TEXT("scythed_chariot"),
    TEXT("catapult"), TEXT("ballista"), TEXT("field_gun") };
static_assert(UE_ARRAY_COUNT(PlatformIds) == MissionPlatformTypeCount);
static_assert(UE_ARRAY_COUNT(MountedClassIds) == int32(ERegimentMountClass::Count));
static_assert(UE_ARRAY_COUNT(RangedClassIds) == int32(ERegimentRangedClass::Count));
static_assert(UE_ARRAY_COUNT(RangedClassIds) == int32(battle::RangedWeapon::Arcane) + 1);
TConstArrayView<FRegimentEraDefinition> All();
const FRegimentEraDefinition* Find(FStringView Id);
const FRegimentEraFaction* FindFaction(const FRegimentEraDefinition& Era, FStringView Id);
const FRegimentEraTroop* MountedTroop(const FRegimentEraFaction& Faction, ERegimentMountClass Class);
const FRegimentEraTroop* RangedTroop(const FRegimentEraFaction& Faction, ERegimentRangedClass Class);
const FRegimentEraTroop* PlatformTroop(const FRegimentEraFaction& Faction, battle::BattlePlatform Platform);
bool Parse(FStringView Json, FRegimentEraDefinition& Out, FText& Error);
}
