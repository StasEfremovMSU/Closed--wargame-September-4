#include "RegimentEraCatalog.h"
#include "RegimentMissionPreset.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/Char.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogRegimentEraCatalog, Log, All);

namespace
{
constexpr int32 EraSchemaVersion = 1;
constexpr int32 MaxManifestSize = 128 * 1024;
constexpr int32 MaxJsonDepth = 32;
constexpr int32 MaxIdentifierLength = 64;
constexpr int32 MaxNameLength = 120;
constexpr int32 MaxDescriptionLength = 400;
constexpr const TCHAR* VisualIds[] = {
    TEXT("classic"), TEXT("plate"), TEXT("pike_shot"), TEXT("eastern"), TEXT("sengoku"),
    TEXT("tricorn"), TEXT("shako"), TEXT("elven"), TEXT("orcish"), TEXT("undead"),
    TEXT("dwarven"), TEXT("giant"), TEXT("arcane")
};
static_assert(UE_ARRAY_COUNT(VisualIds) == int32(ERegimentEraVisual::Count));

using namespace RegimentEraCatalog;

battle::CavalryClass LegacyCavalryClass(FStringView ArchetypeId)
{
    return ArchetypeId.Equals(TEXT("heavy_cavalry")) || ArchetypeId.Equals(TEXT("cataphracts")) ||
        ArchetypeId.Equals(TEXT("knights")) ? battle::CavalryClass::Heavy : battle::CavalryClass::Light;
}

template <size_t N>
int32 ReadClassId(FStringView Id, const TCHAR* const (&Names)[N])
{
    for (int32 Index = 1; Index < int32(N); ++Index)
        if (Id.Equals(Names[Index]))
            return Index;
    return 0;
}
constexpr int32 MaxBriefingLength = 16000;

struct FRoleField
{
    const TCHAR* Name;
    int32 FMissionRosterEntry::* Count;
    int32 MaxCount;
};

constexpr FRoleField RoleFields[] =
{
    {TEXT("swords"), &FMissionRosterEntry::Swords, MissionMaxSwords},
    {TEXT("archers"), &FMissionRosterEntry::Archers, MissionMaxArchers},
    {TEXT("spearmen"), &FMissionRosterEntry::Spearmen, MissionMaxSpecialists},
    {TEXT("mounted"), &FMissionRosterEntry::Mounted, MissionMaxSpecialists},
};
static_assert(UE_ARRAY_COUNT(RoleFields) == int32(ERegimentEraRole::Count));

bool IsIdentifier(FStringView Id)
{
    if (Id.IsEmpty() || Id.Len() > MaxIdentifierLength ||
        Id[0] < TEXT('a') || Id[0] > TEXT('z'))
        return false;

    for (const TCHAR Character : Id)
    {
        if (!((Character >= TEXT('a') && Character <= TEXT('z')) ||
              (Character >= TEXT('0') && Character <= TEXT('9')) ||
              Character == TEXT('_') || Character == TEXT('-')))
            return false;
    }
    return true;
}

bool Fail(FText& Error, const FString& Context, const TCHAR* Message)
{
    Error = FText::FromString(FString::Printf(TEXT("%s: %s"), *Context, Message));
    return false;
}

bool CheckJsonStructure(const FString& Json, FText& Error)
{
    struct FContainer
    {
        bool bObject;
        TSet<FString> Fields;
    };
    TArray<FContainer> Containers;
    const auto Reader = TJsonReaderFactory<>::Create(Json);
    EJsonNotation Notation;
    while (Reader->ReadNext(Notation))
    {
        if (Notation == EJsonNotation::Error)
            return Fail(Error, TEXT("module.json"), *Reader->GetErrorMessage());

        if (Notation == EJsonNotation::ObjectEnd || Notation == EJsonNotation::ArrayEnd)
        {
            Containers.Pop();
            continue;
        }

        if (!Containers.IsEmpty() && Containers.Last().bObject)
        {
            const FString& Field = Reader->GetIdentifier();
            TSet<FString>& Fields = Containers.Last().Fields;
            if (Fields.Contains(Field))
                return Fail(Error, Field, TEXT("duplicate JSON field"));
            Fields.Add(Field);
        }

        if (Notation == EJsonNotation::ObjectStart || Notation == EJsonNotation::ArrayStart)
        {
            if (Containers.Num() == MaxJsonDepth)
                return Fail(Error, TEXT("module.json"), TEXT("JSON is nested too deeply"));
            Containers.Add({Notation == EJsonNotation::ObjectStart, {}});
        }
    }
    return true;
}

struct FManifestObject
{
    const FJsonObject& Json;
    FString Path;
    FText& Error;

    bool Invalid(const TCHAR* Key, const TCHAR* Message) const
    {
        return Fail(Error, Path + TEXT(".") + Key, Message);
    }

    bool Object(const TCHAR* Key, const TSharedPtr<FJsonObject>*& Value) const
    {
        return (Json.TryGetObjectField(Key, Value) && Value->IsValid()) ||
            Invalid(Key, TEXT("expected a JSON object"));
    }

    bool Array(const TCHAR* Key, const TArray<TSharedPtr<FJsonValue>>*& Value) const
    {
        return Json.TryGetArrayField(Key, Value) || Invalid(Key, TEXT("expected a JSON array"));
    }

    bool String(const TCHAR* Key, FString& Value) const
    {
        return (Json.HasTypedField<EJson::String>(Key) && Json.TryGetStringField(Key, Value)) ||
            Invalid(Key, TEXT("expected a string"));
    }

    bool DisplayText(const TCHAR* Key, FString& Value, int32 MaxLength, bool bMultiline = false) const
    {
        if (!String(Key, Value))
            return false;
        if (Value.IsEmpty() || Value.Len() > MaxLength || Value.TrimStartAndEnd().IsEmpty())
            return Invalid(Key, TEXT("expected nonempty display text within the length limit"));
        for (const TCHAR Character : Value)
        {
            if (FChar::IsControl(Character) && !(bMultiline && (Character == TEXT('\n') || Character == TEXT('\r'))))
                return Invalid(Key, TEXT("display text contains a control character"));
        }
        return true;
    }

    bool Id(const TCHAR* Key, FString& Value) const
    {
        return String(Key, Value) && (IsIdentifier(Value) ||
            Invalid(Key, TEXT("expected a lowercase ASCII identifier")));
    }

    bool Number(const TCHAR* Key, double Min, double Max, double& Value) const
    {
        if (!Json.HasTypedField<EJson::Number>(Key) || !Json.TryGetNumberField(Key, Value) ||
            !FMath::IsFinite(Value) || Value < Min || Value > Max)
        {
            const FString Message = FString::Printf(TEXT("expected a number between %g and %g"), Min, Max);
            return Invalid(Key, *Message);
        }
        return true;
    }

    bool Float(const TCHAR* Key, double Min, double Max, float& Value) const
    {
        double Parsed;
        if (!Number(Key, Min, Max, Parsed))
            return false;
        Value = static_cast<float>(Parsed);
        return true;
    }

    bool Integer(const TCHAR* Key, int32 Min, int32 Max, int32& Value) const
    {
        double Parsed;
        if (!Number(Key, Min, Max, Parsed))
            return false;
        if (Parsed != FMath::TruncToDouble(Parsed))
            return Invalid(Key, TEXT("expected an integer"));
        Value = static_cast<int32>(Parsed);
        return true;
    }

    bool Boolean(const TCHAR* Key, bool& Value) const
    {
        return (Json.HasTypedField<EJson::Boolean>(Key) && Json.TryGetBoolField(Key, Value)) ||
            Invalid(Key, TEXT("expected a boolean"));
    }
};

bool ReadRules(const FManifestObject& Root, FRegimentEraDefinition& Era)
{
    const TSharedPtr<FJsonObject>* Object;
    if (!Root.Object(TEXT("rules"), Object))
        return false;

    const FManifestObject Data{**Object, TEXT("rules"), Root.Error};
    auto& Rules = Era.Rules;
    int32 Cohorts;
    if (!Data.Id(TEXT("id"), Era.ProfileId) ||
        !Data.Boolean(TEXT("local_melee"), Rules.local_melee) ||
        !Data.Boolean(TEXT("regiment_tactics"), Rules.regiment_tactics) ||
        (Data.Json.HasField(TEXT("linear_infantry")) &&
            !Data.Boolean(TEXT("linear_infantry"), Rules.linear_infantry)) ||
        !Data.Boolean(TEXT("risky_volley"), Rules.risky_volley) ||
        !Data.Integer(TEXT("ranged_cohorts"), 1, 64, Cohorts) ||
        !Data.Float(TEXT("panic_fraction"), 0, 1, Rules.panic_fraction))
        return false;
    if (Rules.risky_volley && !Rules.regiment_tactics)
        return Data.Invalid(TEXT("risky_volley"), TEXT("requires regiment_tactics"));
    Rules.ranged_cohorts = static_cast<uint32>(Cohorts);
    return true;
}

bool ReadArchetypes(const FManifestObject& Root, TMap<FString, FRegimentEraTroop>& Archetypes)
{
    const TSharedPtr<FJsonObject>* Object;
    if (!Root.Object(TEXT("archetypes"), Object))
        return false;
    if ((*Object)->Values.IsEmpty())
        return Root.Invalid(TEXT("archetypes"), TEXT("expected at least one troop archetype"));

    for (const auto& [Key, Value] : (*Object)->Values)
    {
        const FString Id{FStringView(Key)};
        const FString Path = TEXT("archetypes.") + Id;
        if (!IsIdentifier(Id))
            return Fail(Root.Error, Path, TEXT("expected a lowercase ASCII identifier"));
        if (!Value || Value->Type != EJson::Object || !Value->AsObject())
            return Fail(Root.Error, Path, TEXT("expected a troop object"));
        const FManifestObject Data{*Value->AsObject(), Path, Root.Error};

        FRegimentEraTroop Troop;
        auto& Weapon = Troop.Weapon;
        int32 Cooldown;
        int32 Windup;
        if (!Data.DisplayText(TEXT("name"), Troop.Name, MaxNameLength) ||
            !Data.Float(TEXT("health"), 0.001, 10000, Troop.Health) ||
            !Data.Float(TEXT("speed"), 0, 50, Troop.Speed) ||
            !Data.Float(TEXT("armour"), 0, 10000, Troop.Armour) ||
            !Data.Float(TEXT("damage"), 0, 10000, Weapon.damage) ||
            !Data.Float(TEXT("range"), 0.001, 1000, Weapon.range) ||
            !Data.Integer(TEXT("cooldown_ticks"), 1, 1200, Cooldown) ||
            !Data.Integer(TEXT("windup_ticks"), 0, 1200, Windup) ||
            !Data.Float(TEXT("projectile_speed_mps"), 0.001, 1000, Weapon.projectile_speed_mps) ||
            !Data.Float(TEXT("base_accuracy"), 0, 1, Weapon.base_accuracy))
            return false;
        if (Data.Json.HasField(TEXT("description")) &&
            !Data.DisplayText(TEXT("description"), Troop.Description, MaxDescriptionLength))
            return false;
        if (Data.Json.HasField(TEXT("bayonet_musket")) &&
            !Data.Boolean(TEXT("bayonet_musket"), Troop.bBayonetMusket))
            return false;
        if (Data.Json.HasField(TEXT("platform")))
        {
            FString Platform;
            if (!Data.Id(TEXT("platform"), Platform))
                return false;
            const int32 Index = ReadClassId(Platform, PlatformIds);
            if (!Index)
                return Data.Invalid(TEXT("platform"), TEXT("unknown specialist platform"));
            if (Data.Json.HasField(TEXT("cavalry_class")))
                return Data.Invalid(TEXT("cavalry_class"), TEXT("specialist platforms cannot use cavalry profiles"));
            Troop.Platform = static_cast<battle::BattlePlatform>(Index);
        }
        if (Data.Json.HasField(TEXT("cavalry_class")))
        {
            FString Class;
            if (!Data.Id(TEXT("cavalry_class"), Class))
                return false;
            const int32 Index = ReadClassId(Class, MountedClassIds);
            if (!Index)
                return Data.Invalid(TEXT("cavalry_class"), TEXT("expected light or heavy"));
            Troop.Cavalry = static_cast<battle::CavalryClass>(Index);
        }
        if (Data.Json.HasField(TEXT("mounted_weapon")))
        {
            FString MountWeapon;
            if (!Data.Id(TEXT("mounted_weapon"), MountWeapon))
                return false;
            if (Troop.Cavalry == battle::CavalryClass::None)
                return Data.Invalid(TEXT("mounted_weapon"), TEXT("requires a cavalry profile"));
            if (MountWeapon == TEXT("blade"))
                Troop.MountedWeapon = ERegimentMountedWeapon::Blade;
            else if (MountWeapon == TEXT("lance"))
                Troop.MountedWeapon = ERegimentMountedWeapon::Lance;
            else
                return Data.Invalid(TEXT("mounted_weapon"), TEXT("expected blade or lance"));
        }
        if (battle::IsSiegeEngine(Troop.Platform))
        {
            int32 Stock;
            if (Data.Json.HasField(TEXT("ranged_weapon")))
                return Data.Invalid(TEXT("ranged_weapon"), TEXT("siege platforms use their own projectile type"));
            if (!Data.Integer(TEXT("ammunition"), 1, 1000, Stock))
                return false;
            Troop.Ammunition = static_cast<uint32>(Stock);
        }
        else if (Data.Json.HasField(TEXT("ranged_weapon")))
        {
            FString Class;
            int32 Stock;
            if (!Data.Id(TEXT("ranged_weapon"), Class) || !Data.Integer(TEXT("ammunition"), 1, 1000, Stock))
                return false;
            const int32 Index = ReadClassId(Class, RangedClassIds);
            if (!Index)
                return Data.Invalid(TEXT("ranged_weapon"), TEXT("unknown ranged weapon"));
            Troop.RangedWeapon = static_cast<battle::RangedWeapon>(Index);
            Troop.Ammunition = static_cast<uint32>(Stock);
        }
        else if (Data.Json.HasField(TEXT("ammunition")))
        {
            return Data.Invalid(TEXT("ammunition"), TEXT("requires a ranged weapon"));
        }
        Weapon.cooldown_ticks = static_cast<battle::Tick>(Cooldown);
        Weapon.windup_ticks = static_cast<battle::Tick>(Windup);
        Archetypes.Add(Id, MoveTemp(Troop));
    }
    return true;
}

bool ReadHeraldry(const FManifestObject& Data, FRegimentEraFaction& Faction)
{
    FString Shield;
    if (!Data.String(TEXT("shield"), Shield))
        return false;
    if (Shield.Equals(TEXT("round"), ESearchCase::CaseSensitive))
        Faction.Shield = ERegimentEraShield::Round;
    else if (Shield.Equals(TEXT("rectangular"), ESearchCase::CaseSensitive))
        Faction.Shield = ERegimentEraShield::Rectangular;
    else if (Shield.Equals(TEXT("kite"), ESearchCase::CaseSensitive))
        Faction.Shield = ERegimentEraShield::Kite;
    else
        return Data.Invalid(TEXT("shield"), TEXT("expected round, rectangular or kite"));

    FString Colour;
    if (!Data.String(TEXT("heraldry"), Colour))
        return false;
    if (Colour.Len() != 6)
        return Data.Invalid(TEXT("heraldry"), TEXT("expected a six-digit RGB hexadecimal colour"));
    for (const TCHAR Character : Colour)
    {
        if (!FChar::IsHexDigit(Character))
            return Data.Invalid(TEXT("heraldry"), TEXT("expected a six-digit RGB hexadecimal colour"));
    }
    Faction.Heraldry = FLinearColor::FromSRGBColor(FColor::FromHex(Colour));
    return true;
}

template <uint32 N, typename E>
bool ReadVariants(const FManifestObject& Data, const TCHAR* Key,
    const TMap<FString, FRegimentEraTroop>& Archetypes, const TCHAR* const (&ClassIds)[N],
    E FRegimentEraTroop::* ProfileClass, TStaticArray<TOptional<FRegimentEraTroop>, N>& Out)
{
    if (!Data.Json.HasField(Key))
        return true;
    const TSharedPtr<FJsonObject>* Object;
    if (!Data.Object(Key, Object))
        return false;

    const FManifestObject Variants{**Object, Data.Path + TEXT(".") + Key, Data.Error};
    for (const auto& [Field, Value] : Variants.Json.Values)
    {
        const FString Class{FStringView(Field)};
        const int32 Index = ReadClassId(Class, ClassIds);
        if (!Index)
            return Variants.Invalid(*Class, TEXT("unknown troop variant class"));
        FString Ref;
        if (!Variants.Id(*Class, Ref))
            return false;
        const auto* Troop = Archetypes.Find(Ref);
        if (!Troop || Troop->*ProfileClass != static_cast<E>(Index) ||
            ((Troop->Platform != battle::BattlePlatform::None) != std::is_same_v<E, battle::BattlePlatform>) ||
            (Troop->Cavalry != battle::CavalryClass::None && Troop->RangedWeapon != battle::RangedWeapon::None))
            return Variants.Invalid(*Class, TEXT("archetype does not match the variant class"));
        Out[Index] = *Troop;
    }
    return true;
}

bool ReadInfantryProfiles(const FManifestObject& Data, const TMap<FString, FRegimentEraTroop>& Archetypes,
    FRegimentEraFaction& Faction)
{
    const TCHAR* Key = TEXT("infantry_profiles");
    if (!Data.Json.HasField(Key))
        return true;
    const TSharedPtr<FJsonObject>* Object;
    if (!Data.Object(Key, Object))
        return false;
    for (const auto& [ProfileKey, Value] : (*Object)->Values)
    {
        const FString Id{FStringView(ProfileKey)};
        const FString Path = Data.Path + TEXT(".infantry_profiles.") + Id;
        if (!IsIdentifier(Id) || !Value || Value->Type != EJson::Object || !Value->AsObject())
            return Fail(Data.Error, Path, TEXT("expected an infantry profile object with a lowercase identifier"));
        const FManifestObject Profile{*Value->AsObject(), Path, Data.Error};
        FString Archetype;
        FString Role;
        if (!Profile.Id(TEXT("archetype"), Archetype) || !Profile.Id(TEXT("role"), Role))
            return false;
        FRegimentEraInfantryProfile Entry;
        int32 RoleIndex = 0;
        for (; RoleIndex < int32(ERegimentEraRole::Mounted); ++RoleIndex)
            if (Role == RoleFields[RoleIndex].Name)
                break;
        if (RoleIndex == int32(ERegimentEraRole::Mounted))
            return Profile.Invalid(TEXT("role"), TEXT("expected swords, archers or spearmen"));
        Entry.Role = static_cast<ERegimentEraRole>(RoleIndex);
        const auto* Troop = Archetypes.Find(Archetype);
        const bool bRanged = Entry.Role == ERegimentEraRole::Archers;
        if (!Troop || Troop->Platform != battle::BattlePlatform::None ||
            Troop->Cavalry != battle::CavalryClass::None ||
            (Troop->RangedWeapon != battle::RangedWeapon::None) != bRanged)
            return Profile.Invalid(TEXT("archetype"), TEXT("archetype does not match the infantry role"));
        Entry.Troop = *Troop;
        if (Profile.Json.HasField(TEXT("loose")) &&
            !Profile.Boolean(TEXT("loose"), Entry.bLooseOrder))
            return false;
        if (Entry.bLooseOrder && !bRanged)
            return Profile.Invalid(TEXT("loose"), TEXT("requires a ranged infantry profile"));
        if (Profile.Json.HasField(TEXT("pilum")))
        {
            FString PilumRef;
            if (!Profile.Id(TEXT("pilum"), PilumRef))
                return false;
            const auto* Pilum = Archetypes.Find(PilumRef);
            if (bRanged || !Pilum || Pilum->RangedWeapon != battle::RangedWeapon::Javelin ||
                Pilum->Cavalry != battle::CavalryClass::None || Pilum->Platform != battle::BattlePlatform::None)
                return Profile.Invalid(TEXT("pilum"), TEXT("expected a throwing profile for formed infantry"));
            Entry.Pilum = *Pilum;
        }
        Faction.InfantryProfiles.Add(Id, MoveTemp(Entry));
    }
    return true;
}

bool ReadFactions(const FManifestObject& Root, const TMap<FString, FRegimentEraTroop>& Archetypes,
    FRegimentEraDefinition& Era)
{
    const TArray<TSharedPtr<FJsonValue>>* Factions;
    if (!Root.Array(TEXT("factions"), Factions))
        return false;
    if (Factions->IsEmpty())
        return Root.Invalid(TEXT("factions"), TEXT("expected at least one faction"));

    for (int32 Index = 0; Index < Factions->Num(); ++Index)
    {
        const auto& Value = (*Factions)[Index];
        const FString Path = FString::Printf(TEXT("factions[%d]"), Index);
        if (!Value || Value->Type != EJson::Object || !Value->AsObject())
            return Fail(Root.Error, Path, TEXT("expected a faction object"));
        const FManifestObject Data{*Value->AsObject(), Path, Root.Error};

        FRegimentEraFaction Faction;
        const TSharedPtr<FJsonObject>* TroopObject;
        if (!Data.Id(TEXT("id"), Faction.Id) ||
            !Data.DisplayText(TEXT("name"), Faction.Name, MaxNameLength) ||
            !ReadHeraldry(Data, Faction) || !Data.Object(TEXT("troops"), TroopObject))
            return false;
        if (RegimentEraCatalog::FindFaction(Era, Faction.Id))
            return Data.Invalid(TEXT("id"), TEXT("duplicate faction identifier"));
        if (Data.Json.HasField(TEXT("visual")))
        {
            FString Style;
            if (!Data.Id(TEXT("visual"), Style))
                return false;
            const int32 Profile = ReadClassId(Style, VisualIds);
            if (Profile == 0 && Style != VisualIds[0])
                return Data.Invalid(TEXT("visual"), TEXT("unknown figure style"));
            Faction.Visual = static_cast<ERegimentEraVisual>(Profile);
        }

        const FManifestObject Troops{**TroopObject, Path + TEXT(".troops"), Root.Error};
        for (int32 Role = 0; Role < UE_ARRAY_COUNT(RoleFields); ++Role)
        {
            const TCHAR* Key = RoleFields[Role].Name;
            FString Ref;
            if (!Troops.Id(Key, Ref))
                return false;
            const FRegimentEraTroop* Archetype = Archetypes.Find(Ref);
            if (!Archetype)
                return Troops.Invalid(Key, TEXT("unknown troop archetype"));
            auto& Troop = Faction.Troops[Role];
            Troop = *Archetype;
            if (Role == int32(ERegimentEraRole::Mounted) && Troop.Cavalry == battle::CavalryClass::None)
                Troop.Cavalry = LegacyCavalryClass(Ref);
            if (Role == int32(ERegimentEraRole::Archers) && Troop.RangedWeapon == battle::RangedWeapon::None)
                Troop.RangedWeapon = battle::RangedWeapon::Bow;
            if (Troop.Platform != battle::BattlePlatform::None ||
                (Troop.Cavalry != battle::CavalryClass::None) != (Role == int32(ERegimentEraRole::Mounted)) ||
                (Troop.RangedWeapon != battle::RangedWeapon::None) != (Role == int32(ERegimentEraRole::Archers)))
                return Troops.Invalid(Key, TEXT("archetype does not match its faction role"));
        }
        if (!ReadVariants(Data, TEXT("mounted_variants"), Archetypes, MountedClassIds,
                &FRegimentEraTroop::Cavalry, Faction.MountVariants) ||
            !ReadVariants(Data, TEXT("ranged_variants"), Archetypes, RangedClassIds,
                &FRegimentEraTroop::RangedWeapon, Faction.RangedVariants) ||
            !ReadVariants(Data, TEXT("platforms"), Archetypes, PlatformIds,
                &FRegimentEraTroop::Platform, Faction.Platforms) ||
            !ReadInfantryProfiles(Data, Archetypes, Faction))
            return false;
        Era.Factions.Add(MoveTemp(Faction));
    }
    return true;
}

bool ReadMissionSide(const FManifestObject& Mission, const TCHAR* Key, int32 FirstSlot,
    const FRegimentEraFaction& Faction, FMissionRoster& Roster, TArray<FVector2f>& Positions)
{
    const TArray<TSharedPtr<FJsonValue>>* Regiments;
    if (!Mission.Array(Key, Regiments))
        return false;
    if (Regiments->IsEmpty() || Regiments->Num() > MissionRegimentsPerTeam)
        return Mission.Invalid(Key, TEXT("expected one to six regiments"));

    for (int32 Slot = 0; Slot < Regiments->Num(); ++Slot)
    {
        const auto& Value = (*Regiments)[Slot];
        const FString Path = FString::Printf(TEXT("mission.%s[%d]"), Key, Slot);
        if (!Value || Value->Type != EJson::Object || !Value->AsObject())
            return Fail(Mission.Error, Path, TEXT("expected a regiment object"));
        const FManifestObject Data{*Value->AsObject(), Path, Mission.Error};

        FMissionRosterEntry& Entry = Roster[FirstSlot + Slot];
        if (Data.Json.HasField(TEXT("platform")))
        {
            FString PlatformId;
            if (!Data.Id(TEXT("platform"), PlatformId))
                return false;
            Entry.Platform = static_cast<battle::BattlePlatform>(ReadClassId(PlatformId, PlatformIds));
            if (!PlatformTroop(Faction, Entry.Platform))
                return Data.Invalid(TEXT("platform"), TEXT("specialist platform is unavailable to the faction"));
            if (!Data.Integer(TEXT("platform_count"), 1, MissionMaxPlatforms, Entry.PlatformCount))
                return false;
            for (const auto& Role : RoleFields)
                if (Data.Json.HasField(Role.Name) && !Data.Integer(Role.Name, 0, 0, Entry.*Role.Count))
                    return false;
        }
        else
        {
            if (Data.Json.HasField(TEXT("platform_count")))
                return Data.Invalid(TEXT("platform_count"), TEXT("requires a specialist platform"));
            for (const auto& Role : RoleFields)
                if (!Data.Integer(Role.Name, 0, Role.MaxCount, Entry.*Role.Count))
                    return false;
            const int32 Infantry = Entry.Swords + Entry.Archers + Entry.Spearmen;
            if (Infantry > MissionMaxSpecialists)
                return Fail(Mission.Error, Path, TEXT("a regiment can have at most 100 figures including command staff"));
            if (Infantry > 0 && Entry.Mounted > 0)
                return Fail(Mission.Error, Path, TEXT("mounted regiments cannot include infantry"));
            if (Infantry == 0 && Entry.Mounted == 0)
                return Fail(Mission.Error, Path, TEXT("a regiment needs at least one troop"));
        }

        FVector2f Position;
        if (!Data.Float(TEXT("x"), -MissionStartMaxX, MissionStartMaxX, Position.X) ||
            !Data.Float(TEXT("y"), -MissionStartMaxY, MissionStartMaxY, Position.Y) ||
            !Data.Integer(TEXT("group"), 1, MissionGroupsPerTeam, Entry.Group))
            return false;
        if (Positions.Contains(Position))
            return Fail(Mission.Error, Path, TEXT("duplicate regiment starting position"));
        Positions.Add(Position);

        Entry.StartX = Position.X;
        Entry.StartY = Position.Y;
        Entry.bCavalry = Entry.Mounted > 0;
        Entry.bEnabled = true;
    }
    return true;
}

bool ReadMission(const FManifestObject& Root, FRegimentEraDefinition& Era)
{
    const TSharedPtr<FJsonObject>* Object;
    if (!Root.Object(TEXT("mission"), Object))
        return false;

    const FManifestObject Data{**Object, TEXT("mission"), Root.Error};
    FString Id;
    if (!Data.String(TEXT("id"), Id))
        return false;
    if (!FGuid::Parse(Id, Era.MissionId) || !Era.MissionId.IsValid())
        return Data.Invalid(TEXT("id"), TEXT("expected a nonzero GUID"));

    if (!Data.DisplayText(TEXT("name"), Era.MissionName, MaxNameLength) ||
        !Data.DisplayText(TEXT("briefing"), Era.Briefing, MaxBriefingLength, true) ||
        !Data.Id(TEXT("blue_faction"), Era.BlueFaction) ||
        !Data.Id(TEXT("orange_faction"), Era.OrangeFaction))
        return false;
    if (Data.Json.HasField(TEXT("terrain")))
    {
        FString Map;
        if (!Data.String(TEXT("terrain"), Map))
            return false;
        const auto Terrain = RegimentMissionPresets::FindTerrain(Map);
        if (!Terrain)
            return Data.Invalid(TEXT("terrain"), TEXT("unknown mission terrain"));
        Era.Terrain = *Terrain;
    }
    const auto* Blue = RegimentEraCatalog::FindFaction(Era, Era.BlueFaction);
    if (!Blue)
        return Data.Invalid(TEXT("blue_faction"), TEXT("unknown faction"));
    const auto* Orange = RegimentEraCatalog::FindFaction(Era, Era.OrangeFaction);
    if (!Orange)
        return Data.Invalid(TEXT("orange_faction"), TEXT("unknown faction"));

    for (int32 Index = 0; Index < MissionRegimentCount; ++Index)
    {
        auto& Entry = Era.Roster[Index];
        Entry.Swords = Entry.Archers = Entry.Spearmen = Entry.Mounted = 0;
        Entry.bEnabled = false;
    }
    TArray<FVector2f> Positions;
    return ReadMissionSide(Data, TEXT("blue"), 0, *Blue, Era.Roster, Positions) &&
        ReadMissionSide(Data, TEXT("orange"), MissionRegimentsPerTeam, *Orange, Era.Roster, Positions);
}

TArray<FRegimentEraDefinition> LoadCatalog()
{
    const FString Directory = FPaths::ProjectContentDir() / TEXT("Battle/eras");
    IFileManager& Files = IFileManager::Get();
    TArray<FString> Folders;
    Files.FindFiles(Folders, *(Directory / TEXT("*")), false, true);
    Folders.Sort();

    TArray<FRegimentEraDefinition> Catalog;
    TSet<FString> Ids;
    TSet<FGuid> Missions;
    for (const FString& Folder : Folders)
    {
        const FString Filename = Directory / Folder / TEXT("module.json");
        if (!Files.FileExists(*Filename))
            continue;

        const int64 Size = Files.FileSize(*Filename);
        FString Contents;
        if (Size <= 0 || Size > MaxManifestSize || !FFileHelper::LoadFileToString(Contents, *Filename))
        {
            UE_LOG(LogRegimentEraCatalog, Warning, TEXT("Rejected era manifest %s: unreadable, empty or oversized"), *Filename);
            continue;
        }

        FRegimentEraDefinition Era;
        FText Error;
        if (!RegimentEraCatalog::Parse(Contents, Era, Error))
        {
            UE_LOG(LogRegimentEraCatalog, Warning, TEXT("Rejected era manifest %s: %s"), *Filename, *Error.ToString());
            continue;
        }
        if (Ids.Contains(Era.Id) || Missions.Contains(Era.MissionId))
        {
            UE_LOG(LogRegimentEraCatalog, Warning, TEXT("Rejected era manifest %s: duplicate era ID or mission GUID"), *Filename);
            continue;
        }

        Ids.Add(Era.Id);
        Missions.Add(Era.MissionId);
        Catalog.Add(MoveTemp(Era));
    }
    return Catalog;
}
}

TConstArrayView<FRegimentEraDefinition> RegimentEraCatalog::All()
{
    static const TArray<FRegimentEraDefinition> Catalog = LoadCatalog();
    return Catalog;
}

const FRegimentEraDefinition* RegimentEraCatalog::Find(FStringView Id)
{
    for (const FRegimentEraDefinition& Era : All())
    {
        if (Id.Equals(Era.Id))
            return &Era;
    }
    return nullptr;
}

const FRegimentEraFaction* RegimentEraCatalog::FindFaction(const FRegimentEraDefinition& Era, FStringView Id)
{
    return Era.Factions.FindByPredicate([Id](const FRegimentEraFaction& Faction)
    {
        return Id.Equals(Faction.Id);
    });
}

const FRegimentEraTroop* RegimentEraCatalog::MountedTroop(const FRegimentEraFaction& Faction, ERegimentMountClass Class)
{
    const auto& Default = Faction.Troops[int32(ERegimentEraRole::Mounted)];
    if (Class == ERegimentMountClass::FactionDefault)
        return &Default;
    const int32 Index = int32(Class);
    if (Index >= int32(ERegimentMountClass::Count))
        return nullptr;
    const auto& Variant = Faction.MountVariants[Index];
    return Variant.IsSet() ? &Variant.GetValue() : int32(Default.Cavalry) == Index ? &Default : nullptr;
}

const FRegimentEraTroop* RegimentEraCatalog::RangedTroop(const FRegimentEraFaction& Faction, ERegimentRangedClass Class)
{
    const auto& Default = Faction.Troops[int32(ERegimentEraRole::Archers)];
    if (Class == ERegimentRangedClass::FactionDefault)
        return &Default;
    const int32 Index = int32(Class);
    if (Index >= int32(ERegimentRangedClass::Count))
        return nullptr;
    const auto& Variant = Faction.RangedVariants[Index];
    return Variant.IsSet() ? &Variant.GetValue() : int32(Default.RangedWeapon) == Index ? &Default : nullptr;
}

const FRegimentEraTroop* RegimentEraCatalog::PlatformTroop(const FRegimentEraFaction& Faction,
    battle::BattlePlatform Platform)
{
    const int32 Index = int32(Platform);
    if (Index <= 0 || Index >= MissionPlatformTypeCount)
        return nullptr;
    const auto& Troop = Faction.Platforms[Index];
    return Troop.IsSet() ? &Troop.GetValue() : nullptr;
}

bool RegimentEraCatalog::Parse(FStringView Json, FRegimentEraDefinition& Out, FText& Error)
{
    if (Json.IsEmpty() || Json.Len() > MaxManifestSize)
        return Fail(Error, TEXT("module.json"), TEXT("empty or oversized manifest"));

    const FString Contents(Json);
    if (!CheckJsonStructure(Contents, Error))
        return false;
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Object) || !Object)
        return Fail(Error, TEXT("module.json"), TEXT("expected a valid JSON object"));

    const FManifestObject Root{*Object, TEXT("module"), Error};
    FRegimentEraDefinition Era;
    int32 SchemaVersion;
    TMap<FString, FRegimentEraTroop> Archetypes;
    if (!Root.Integer(TEXT("schema_version"), EraSchemaVersion, EraSchemaVersion, SchemaVersion) ||
        !Root.Id(TEXT("id"), Era.Id) ||
        !Root.DisplayText(TEXT("name"), Era.Name, MaxNameLength) ||
        !Root.Integer(TEXT("version"), 1, MAX_int32, Era.Version) ||
        !ReadRules(Root, Era) ||
        !ReadArchetypes(Root, Archetypes) ||
        !ReadFactions(Root, Archetypes, Era) ||
        !ReadMission(Root, Era))
        return false;

    Out = MoveTemp(Era);
    Error = FText::GetEmpty();
    return true;
}
