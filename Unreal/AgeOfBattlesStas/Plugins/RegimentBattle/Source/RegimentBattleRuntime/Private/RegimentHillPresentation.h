#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Terrain.h"
THIRD_PARTY_INCLUDES_END

inline FVector RegimentTerrainLocation(const battle::Vec3& Position, const FVector& Origin, const battle::TerrainHill* Hill)
{
    const double GroundZ = Hill ? battle::HillHeight(*Hill, Position) : 0.0;
    return Origin + FVector(100.0 * Position.x, 100.0 * Position.y, 100.0 * (Position.z + GroundZ));
}

enum class ERegimentGroundMaterial : int32
{
    Meadow, Crest, Road, Field, Wood, Water, Ford, Marsh, Ditch, EarthBank, BankTurf
};

UE::Geometry::FDynamicMesh3 BuildRegimentGroundMesh(const battle::TerrainHill* Hill, double HalfWidthCm,
    double HalfDepthCm, std::span<const battle::Obstacle> Areas);

UE::Geometry::FDynamicMesh3 BuildRegimentRoadMesh(const battle::TerrainHill* Hill, double HalfWidthCm,
    double HalfDepthCm, std::span<const battle::Obstacle> Areas);

UE::Geometry::FDynamicMesh3 BuildRegimentHillMesh(const battle::TerrainHill& Hill, double HalfWidthCm,
    double HalfDepthCm, std::span<const battle::Obstacle> Areas = {});
