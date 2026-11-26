#pragma once

#include "RegimentEraCatalog.h"

inline bool RegimentUsesBayonetProfile(const FRegimentEraFaction* Faction, ERegimentEraRole Role)
{
    return Faction && Faction->Troops[int32(Role)].bBayonetMusket;
}

inline bool RegimentShowsBayonetMusket(const battle::UnitSpec& Unit, const FRegimentEraFaction* Faction)
{
    if (Unit.archer)
        return Unit.ranged_weapon == battle::RangedWeapon::Musket;
    return Unit.troop != battle::UnitTroop::Mounted && RegimentUsesBayonetProfile(Faction,
        Unit.troop == battle::UnitTroop::Spearman ? ERegimentEraRole::Spearmen : ERegimentEraRole::Swords);
}

inline bool RegimentShowsPolearm(const battle::UnitSpec& Unit, bool bMusket, const FRegimentEraTroop* MountedProfile)
{
    if (Unit.archer || bMusket)
        return false;
    if (Unit.troop == battle::UnitTroop::Mounted)
        return !MountedProfile || MountedProfile->MountedWeapon != ERegimentMountedWeapon::Blade;
    return Unit.troop == battle::UnitTroop::Spearman;
}
