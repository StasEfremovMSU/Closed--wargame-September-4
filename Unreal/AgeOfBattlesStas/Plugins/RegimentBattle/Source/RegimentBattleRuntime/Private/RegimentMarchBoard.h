#pragma once

#include "CoreMinimal.h"

class AActor;
class ACameraActor;
class UCameraComponent;
namespace battle { enum class MarchTerrain; struct MarchField; }

void ApplyRegimentBattleCameraExposure(UCameraComponent& Camera);

struct FMarchBoardMarker
{
    uint64 Id = 0;
    int32 Team = 0;
    FVector Position = FVector::ZeroVector;
    double YawDegrees = 0.0;
    TOptional<FVector> Waypoint;
    bool bVisible = true;
    FVector2D SizeScale = FVector2D(1.0, 1.0);
};

struct FMarchBoardRouteSegment
{
    FVector From;
    FVector To;
    bool bReachable;

    bool operator==(const FMarchBoardRouteSegment&) const = default;
};

class FRegimentMarchBoard
{
public:
    static constexpr double TokenLengthCm = 568.0;
    static constexpr double TokenWidthCm = 398.0;
    FRegimentMarchBoard(AActor& Owner, const FVector& Origin);
    ~FRegimentMarchBoard();

    void SetTerrain(battle::MarchTerrain Terrain, const battle::MarchField& Field);
    void Update(TConstArrayView<FMarchBoardMarker> Markers, uint64 Selected);
    void SetRoutePreview(TConstArrayView<FMarchBoardRouteSegment> Segments);
    void ClearMarkers();
    ACameraActor* GetOverviewCamera() const;

private:
    struct FScene;
    TUniquePtr<FScene> Scene;
};
