#pragma once

#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"
THIRD_PARTY_INCLUDES_START
#include "battle/Types.h"
THIRD_PARTY_INCLUDES_END

class UTexture2D;
struct FMissionPreset;
namespace battle { struct Scenario; struct World; }

namespace RegimentCommanderProfiles
{
struct FCommanderCard
{
    FString Name;
    FString RoleIcon;
    FText Effects;
    FText TacticalSkill;
    UTexture2D* Portrait = nullptr;
};

void Apply(battle::Scenario& Scenario, const FMissionPreset& Preset);
FCommanderCard Describe(const battle::World& World, battle::OfficerId OfficerId, FStringView EraId = {});
bool HasNamePool(const FString& Era, const FString& Faction);
}
