#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;

float RegimentInitialStridePhase(uint64 UnitId);
FRotator RegimentStandardFlutter(double Seconds, float Phase, double DistanceSquared);

inline constexpr double RegimentFigureReferenceHeight = 165.0;
FTransform RegimentHistoricalDressPose(const FTransform& FigureTransform, double MeshForwardYaw,
    double FeetOffset, double FigureHeight);

enum class ERegimentFigureAnchor : uint8 { Torso, Head, LeftShoulder, RightShoulder, LeftForearm, LeftThigh, RightThigh, Count };

struct FRegimentFigureAnchors
{
    FTransform Deltas[int32(ERegimentFigureAnchor::Count)];
    const FTransform& At(ERegimentFigureAnchor Anchor) const { return Deltas[int32(Anchor)]; }
};

struct FRegimentRiderTack
{
    FVector Seat{-5.0, 0.0, 121.0};
    FVector RightAnkle{11.0, 32.0, 62.0};
};

struct FRegimentRiderLeg
{
    FVector Knee;
    FVector Ankle;
};

FRegimentRiderLeg SolveRegimentRiderLeg(FVector Hip, FVector Target, double Upper, double Lower, FVector Forward);

struct FRegimentMountedBoots
{
    FVector Left;
    FVector Right;
    FQuat FootTurn;
};

struct FRegimentFigureMotion
{
    float StridePhase = 0.f;
    float GaitWeight = 0.f;
    float AttackPhase = 0.f;
    bool bArcher = false;
    bool bMounted = false;
    bool bPolearm = false;
    bool bAirborne = false;
    bool bParrying = false;
    float Windup = 0.f;
    TOptional<FRegimentMountedBoots> MountedBoots;
    bool bCourier = false;
    bool bHideStockHead = false;
};

class FRegimentFigureRig
{
public:
    explicit FRegimentFigureRig(const FReferenceSkeleton& Skeleton);

    bool IsValid() const { return bValid; }
    bool HasStockHead() const { return AnchorBones[int32(ERegimentFigureAnchor::Head)] != INDEX_NONE && bValid; }
    FVector GetMeshForward() const { return Forward; }
    double GetMeshForwardYaw() const { return FMath::Atan2(Forward.Y, Forward.X); }
    FVector SeatMountedRider(const FTransform& HorseTransform, const FTransform& FigureTransform, double SaddleHeight) const;
    FVector SeatMountedRider(const FTransform& HorseTransform, const FTransform& FigureTransform, FVector Saddle) const;
    FRegimentMountedBoots MountedBoots(const FTransform& Horse, const FTransform& Figure, FVector RightAnkle) const;
    FTransform Evaluate(const FRegimentFigureMotion& Motion, TArray<FTransform>& LocalPose,
        FRegimentFigureAnchors* Anchors = nullptr) const;
    FTransform LeftShieldDelta(const FRegimentFigureAnchors& Anchors) const;

private:
    enum EJoint : uint8
    {
        LeftArm, LeftForearm, RightArm, RightForearm, LeftThigh, LeftShin, RightThigh, RightShin, JointCount
    };

    TArray<FTransform> BindPose;
    TArray<FTransform, TInlineAllocator<48>> RestSpace;
    TArray<int32> Parents;
    TArray<int8> JointByBone;
    FVector BoneDirection[JointCount] = {};
    double BoneLength[JointCount] = {};
    int32 AnchorBones[int32(ERegimentFigureAnchor::Count)] = {};
    FVector Forward = FVector::ForwardVector;
    FVector Right = FVector::RightVector;
    TOptional<FVector> Pelvis;
    int32 LeftHand = INDEX_NONE;
    int32 RightHand = INDEX_NONE;
    int32 LeftFoot = INDEX_NONE;
    int32 RightFoot = INDEX_NONE;
    FTransform NeutralLeftForearm = FTransform::Identity;
    bool bValid = false;
};
