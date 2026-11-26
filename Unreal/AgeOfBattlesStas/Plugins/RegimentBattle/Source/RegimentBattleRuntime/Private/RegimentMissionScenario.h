#pragma once

#include "RegimentMissionPreset.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Navigation.h"
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

enum class EMissionDeployment { MarchContact, AuthoredStart };
struct FRegimentEraInfantryProfile;

int32 MissionFigureCount(const FMissionRoster&, bool bArmyStaff = false);
TOptional<int32> FindInvalidScaledDeployment(const FMissionPreset&);
battle::GridNavigationService MakeMissionNavigation(const std::vector<battle::Obstacle>&, battle::MarchField);
std::vector<battle::MarchRegiment> MakeMissionOpening(const FMissionRoster&, battle::MarchTerrain,
    battle::MarchField Field = {}, int32 FigureTarget = 0);
std::vector<battle::MarchRegiment> MakeMissionOpening(const FMissionPreset&);
battle::Scenario BuildMissionBattle(const FMissionRoster&, std::span<const battle::MarchRegiment>,
    battle::MarchTerrain Terrain = battle::MarchTerrain::River,
    EMissionDeployment DeploymentMode = EMissionDeployment::MarchContact,
    bool bRegimentTactics = false, battle::MarchField Field = {}, int32 FigureTarget = 0,
    bool bLinearInfantry = false, bool bArmyStaff = false,
    std::span<const FRegimentEraInfantryProfile* const> InfantryProfiles = {}, bool bReverseFacing = false);
battle::Scenario BuildPresetBattle(const FMissionPreset&, std::span<const battle::MarchRegiment>, EMissionDeployment);
