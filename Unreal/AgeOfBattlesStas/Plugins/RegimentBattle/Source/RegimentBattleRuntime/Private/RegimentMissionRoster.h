#pragma once

#include "Containers/StaticArray.h"
#include "Containers/UnrealString.h"
#include "Math/UnrealMathUtility.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
THIRD_PARTY_INCLUDES_END

inline constexpr int32 MissionRegimentsPerTeam = 6;
inline constexpr int32 MissionRegimentCount = 2 * MissionRegimentsPerTeam;
inline constexpr int32 MissionMaxRegimentsPerTeam = 12;
inline constexpr int32 MissionRosterCapacity = 2 * MissionMaxRegimentsPerTeam;
inline constexpr int32 MissionGroupsPerTeam = 3;
inline constexpr int32 MissionDefaultRegimentsPerGroup = MissionRegimentsPerTeam / MissionGroupsPerTeam;
inline constexpr int32 MissionCommandStaffCount = 3;
inline constexpr int32 MissionMaxSwords = 77;
inline constexpr int32 MissionMaxArchers = 20;
inline constexpr int32 MissionMaxRegiment = 100;
inline constexpr int32 MissionMaxSpecialists = MissionMaxRegiment - MissionCommandStaffCount;
inline constexpr float MissionStartInset = 4.f;
inline constexpr int32 MissionMaxPlatforms = 6;
inline constexpr int32 MissionMaxFormationFiles = 100;
inline constexpr int32 MissionPlatformTypeCount = int32(battle::BattlePlatform::FieldGun) + 1;

enum class ERegimentMountClass : uint8 { FactionDefault, Light, Heavy, Count };
enum class ERegimentRangedClass : uint8 { FactionDefault, Bow, Crossbow, Sling, Javelin, Gunpowder, Musket, Arcane, Count };

struct FMissionRosterEntry
{
    int32 Swords = 29;
    int32 Archers = 10;
    int32 Spearmen = 8;
    int32 Mounted = 47;
    bool bCavalry = false;
    bool bEnabled = true;
    int32 Group = 0;
    battle::BattlePlatform Platform = battle::BattlePlatform::None;
    int32 PlatformCount = 0;
    int32 FormationFiles = 0;
    std::optional<float> StartX;
    std::optional<float> StartY;
    ERegimentMountClass MountedClass = ERegimentMountClass::FactionDefault;
    ERegimentRangedClass RangedClass = ERegimentRangedClass::FactionDefault;
    FString InfantryProfile;

    bool operator==(const FMissionRosterEntry&) const = default;

    FMissionRosterEntry() = default;
    FMissionRosterEntry(int32 InSwords, int32 InArchers, int32 InSpearmen = 0)
        : Swords(InSwords), Archers(InArchers), Spearmen(InSpearmen) {}

    FMissionRosterEntry Clamped() const
    {
        auto Entry = *this;
        if (int32(Platform) > 0 && int32(Platform) < MissionPlatformTypeCount)
        {
            Entry.Swords = Entry.Archers = Entry.Spearmen = Entry.Mounted = 0;
            Entry.bCavalry = false;
            Entry.MountedClass = ERegimentMountClass::FactionDefault;
            Entry.RangedClass = ERegimentRangedClass::FactionDefault;
            Entry.InfantryProfile.Reset();
            Entry.PlatformCount = FMath::Clamp(PlatformCount, 1, MissionMaxPlatforms);
            return Entry;
        }
        Entry.Platform = battle::BattlePlatform::None;
        Entry.PlatformCount = 0;
        if (bCavalry)
        {
            Entry.Swords = Entry.Archers = Entry.Spearmen = 0;
            Entry.Mounted = FMath::Clamp(Mounted, 0, MissionMaxSpecialists);
            Entry.InfantryProfile.Reset();
            return Entry;
        }
        Entry.Swords = FMath::Clamp(Swords, 0, MissionMaxSwords);
        Entry.Archers = FMath::Clamp(Archers, 0, MissionMaxArchers);
        Entry.Spearmen = FMath::Clamp(Spearmen, 0, MissionMaxSpecialists - Entry.Swords - Entry.Archers);
        Entry.Mounted = 0;
        return Entry;
    }

    int32 Total() const
    {
        const auto Entry = Clamped();
        return MissionCommandStaffCount + Entry.Swords + Entry.Archers + Entry.Spearmen + Entry.Mounted + Entry.PlatformCount;
    }
};

// The second block adds slots without renumbering either side in legacy presets.
constexpr int32 MissionTeam(int32 Index) { return Index % MissionRegimentCount / MissionRegimentsPerTeam; }
constexpr int32 MissionTeamSlot(int32 Index)
{
    return Index % MissionRegimentsPerTeam + Index / MissionRegimentCount * MissionRegimentsPerTeam;
}
constexpr int32 MissionRosterIndex(int32 Team, int32 TeamSlot)
{
    return Team * MissionRegimentsPerTeam + TeamSlot % MissionRegimentsPerTeam +
        TeamSlot / MissionRegimentsPerTeam * MissionRegimentCount;
}

struct FMissionRoster : TStaticArray<FMissionRosterEntry, MissionRosterCapacity>
{
    FMissionRoster()
    {
        for (int32 Index = MissionRegimentCount; Index < MissionRosterCapacity; ++Index)
            (*this)[Index].bEnabled = false;
    }
};

inline int32 MissionRosterExtent(const FMissionRoster& Roster)
{
    for (int32 Index = MissionRegimentCount; Index < MissionRosterCapacity; ++Index)
        if (Roster[Index].bEnabled)
            return MissionRosterCapacity;
    return MissionRegimentCount;
}

inline int32 MissionSavedRosterExtent(const FMissionRoster& Roster)
{
    FMissionRosterEntry Unused;
    Unused.bEnabled = false;
    for (int32 Index = MissionRegimentCount; Index < MissionRosterCapacity; ++Index)
        if (Roster[Index] != Unused)
            return MissionRosterCapacity;
    return MissionRegimentCount;
}

inline int32 MissionGroup(int32 Index, const FMissionRosterEntry& Entry)
{
    const int32 Slot = Index % MissionRegimentsPerTeam;
    return Entry.Group == 0 ? Slot / MissionDefaultRegimentsPerGroup + 1
                            : FMath::Clamp(Entry.Group, 1, MissionGroupsPerTeam);
}

inline battle::Vec3 MissionStartingPosition(int32 Index, const FMissionRosterEntry& Entry,
    battle::MarchField Field = {})
{
    const int32 Slot = Index % MissionRegimentsPerTeam;
    const float Side = MissionTeam(Index) == 0 ? -1.f : 1.f;
    const float DepthScale = Field.HalfWidth() / battle::MarchHalfWidth;
    const bool bAdditional = Index >= MissionRegimentCount;
    const float DefaultX = Side * (39.f + 13.f * (Slot % MissionDefaultRegimentsPerGroup) + (bAdditional ? 9.f : 0.f)) * DepthScale;
    const float DefaultY = battle::MarchCrossings[Slot / MissionDefaultRegimentsPerGroup].y + (bAdditional ? 10.f : 0.f);
    return {Entry.StartX.value_or(DefaultX), Entry.StartY.value_or(DefaultY), 0.f};
}

inline double MissionCameraScale(battle::MarchField Field)
{
    return FMath::Max(double(Field.HalfWidth() / battle::MarchHalfWidth),
                      double(Field.HalfHeight() / battle::MarchHalfHeight));
}

inline FMissionRoster MakeDefaultMissionRoster()
{
    FMissionRoster Roster;
    Roster[MissionRegimentsPerTeam - 1].bCavalry = true;
    Roster[MissionRegimentCount - 1].bCavalry = true;
    return Roster;
}
