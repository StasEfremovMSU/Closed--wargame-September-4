#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
THIRD_PARTY_INCLUDES_END

class UMaterialInterface;
class UStaticMesh;

inline constexpr int32 RegimentGrassCullDistanceCm = 4000;

TArray<FTransform> BuildRegimentGrassTransforms(battle::MarchTerrain Terrain, battle::MarchField Field,
    const battle::TerrainHill* Hill, std::span<const battle::Obstacle> Areas);

UStaticMesh* MakeRegimentGrassMesh(UObject& Owner, UMaterialInterface& Material);
