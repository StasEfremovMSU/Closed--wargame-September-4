#include "RegimentFigureRig.h"

#include "ReferenceSkeleton.h"
#include "RegimentFigureAppearance.h"

float RegimentInitialStridePhase(uint64 UnitId)
{
    const uint32 Variation = RegimentFigureHash(UnitId ^ 0x9e3779b97f4a7c15ull);
    return float(Variation >> 16) * (2.f * UE_PI / 65536.f);
}

FRotator RegimentStandardFlutter(double Seconds, float Phase, double DistanceSquared)
{
    const double Weight = 1.0 - FMath::SmoothStep(FMath::Square(2600.0), FMath::Square(5400.0), DistanceSquared);
    const double Wave = Seconds * 2.8 + Phase;
    return FRotator(2.1 * FMath::Sin(Wave * 1.21) * Weight,
        (5.4 * FMath::Sin(Wave) + 1.6 * FMath::Sin(Wave * 1.7)) * Weight, 0.0);
}

FRegimentFigureRig::FRegimentFigureRig(const FReferenceSkeleton& Skeleton)
    : BindPose(Skeleton.GetRefBonePose())
{
    const int32 NumBones = BindPose.Num();
    Parents.Reserve(NumBones);
    JointByBone.Init(INDEX_NONE, NumBones);
    TArray<FTransform, TInlineAllocator<48>> RestSpace;
    RestSpace.Reserve(NumBones);
    for (int32 Bone = 0; Bone < NumBones; ++Bone)
    {
        const int32 Parent = Parents.Add_GetRef(Bone == 0 ? INDEX_NONE : Skeleton.GetParentIndex(Bone));
        RestSpace.Add(Parent == INDEX_NONE ? BindPose[Bone] : BindPose[Bone] * RestSpace[Parent]);
    }

    const int32 Foot = Skeleton.FindBoneIndex(TEXT("foot_L"));
    const int32 Toe = Skeleton.FindBoneIndex(TEXT("toe_L"));
    LeftHand = Skeleton.FindBoneIndex(TEXT("hand_L"));
    RightHand = Skeleton.FindBoneIndex(TEXT("hand_R"));
    if (Foot == INDEX_NONE || Toe == INDEX_NONE || LeftHand == INDEX_NONE || RightHand == INDEX_NONE)
        return;

    const FVector FootForward = (RestSpace[Toe].GetLocation() - RestSpace[Foot].GetLocation()).GetSafeNormal2D();
    if (FootForward.IsNearlyZero())
        return;
    Forward = FootForward;
    Right = FVector::CrossProduct(FVector::UpVector, Forward);

    static const FName Bones[JointCount] = {
        TEXT("upper_arm_L"), TEXT("forearm_L"), TEXT("upper_arm_R"), TEXT("forearm_R"),
        TEXT("thigh_L"), TEXT("shin_L"), TEXT("thigh_R"), TEXT("shin_R")
    };
    static const FName Children[JointCount] = {
        TEXT("forearm_L"), TEXT("hand_L"), TEXT("forearm_R"), TEXT("hand_R"),
        TEXT("shin_L"), TEXT("foot_L"), TEXT("shin_R"), TEXT("foot_R")
    };
    for (int32 Joint = 0; Joint < JointCount; ++Joint)
    {
        const int32 Bone = Skeleton.FindBoneIndex(Bones[Joint]);
        const int32 Child = Skeleton.FindBoneIndex(Children[Joint]);
        if (Bone == INDEX_NONE || Child == INDEX_NONE || Parents[Child] != Bone)
            return;
        BoneDirection[Joint] = BindPose[Child].GetLocation().GetSafeNormal();
        if (BoneDirection[Joint].IsNearlyZero())
            return;
        JointByBone[Bone] = static_cast<int8>(Joint);
    }
    bValid = true;
}

FTransform FRegimentFigureRig::Evaluate(const FRegimentFigureMotion& Motion, TArray<FTransform>& LocalPose) const
{
    LocalPose = BindPose;
    if (!bValid)
        return FTransform::Identity;

    const FVector Up = FVector::UpVector;
    const float Step = FMath::Sin(Motion.StridePhase) * Motion.GaitWeight;
    // The ranged event arrives at release. The archer starts with the bow up and lowers it during recovery.
    const float Attack = Motion.bArcher && Motion.AttackPhase > 0.f
        ? 1.f - FMath::SmoothStep(0.24f, 1.f, Motion.AttackPhase)
        : FMath::Sin(PI * Motion.AttackPhase);
    FVector Directions[JointCount];
    Directions[LeftThigh] = -Up + Forward * (0.50 * Step) - Right * 0.05;
    Directions[RightThigh] = -Up - Forward * (0.50 * Step) + Right * 0.05;
    Directions[LeftShin] = -Up + Forward * (0.32 * Step - 0.55 * FMath::Max(Step, 0.f));
    Directions[RightShin] = -Up + Forward * (-0.32 * Step - 0.55 * FMath::Max(-Step, 0.f));
    if (Motion.bMounted)
    {
        Directions[LeftThigh] = -Up * .40 + Forward * .72 - Right * .56;
        Directions[RightThigh] = -Up * .40 + Forward * .72 + Right * .56;
        Directions[LeftShin] = -Up - Forward * .40;
        Directions[RightShin] = -Up - Forward * .40;
    }
    else if (Motion.bAirborne)
    {
        Directions[LeftThigh] = -Up * .72 + Forward * .65 - Right * .08;
        Directions[RightThigh] = -Up * .88 + Forward * .42 + Right * .08;
        Directions[LeftShin] = -Up * .42 - Forward * .85;
        Directions[RightShin] = -Up * .65 - Forward * .60;
    }

    Directions[LeftArm] = -Up - Right * 0.22 + Forward * (0.10 - 0.40 * Step);
    Directions[RightArm] = -Up + Right * 0.22 + Forward * (0.10 + 0.40 * Step);
    Directions[LeftForearm] = -Up * 0.65 + Forward * (0.60 - 0.27 * Step);
    Directions[RightForearm] = -Up * 0.65 + Forward * (0.60 + 0.27 * Step);

    if (Motion.bArcher)
    {
        Directions[LeftArm] = FMath::Lerp(Directions[LeftArm], Forward - Right * 0.12 - Up * 0.16, Attack);
        Directions[LeftForearm] = FMath::Lerp(Directions[LeftForearm], Forward + Up * 0.03, Attack);
        Directions[RightArm] = FMath::Lerp(Directions[RightArm], Right - Forward * 0.30 - Up * 0.12, Attack);
        Directions[RightForearm] = FMath::Lerp(Directions[RightForearm], Forward * 0.55 - Right + Up * 0.30, Attack);
    }
    else if (Motion.bPolearm)
    {
        Directions[RightArm] = FMath::Lerp(-Up - Right * .18 + Forward * .35, Forward + Up * .16, Attack);
        Directions[RightForearm] = FMath::Lerp(Forward + Up * .24, Forward + Up * .08, Attack);
    }
    else
    {
        const float Sweep = FMath::Lerp(0.80f, -0.80f, Motion.AttackPhase);
        Directions[RightArm] = FMath::Lerp(Directions[RightArm], Forward + Right * Sweep + Up * 0.16, Attack);
        Directions[RightForearm] = FMath::Lerp(Directions[RightForearm], Forward + Right * Sweep + Up * 0.32, Attack);
    }

    if (Motion.Windup > 0.f)
    {
        const bool bThrust = Motion.bPolearm;
        Directions[RightArm] = FMath::Lerp(Directions[RightArm],
            bThrust ? -Forward * .48 + Right * .43 - Up * .65 : -Forward * .47 + Right * .81 + Up * .50,
            Motion.Windup);
        Directions[RightForearm] = FMath::Lerp(Directions[RightForearm],
            bThrust ? Forward * .58 + Up * .54 : -Forward * .24 + Up * .96, Motion.Windup);
    }
    if (Motion.bParrying)
    {
        Directions[LeftArm] = Forward - Right * .40 - Up * .35;
        Directions[LeftForearm] = Forward * .28 + Up * .85;
        Directions[RightArm] = Forward + Right * .53 - Up * .38;
        Directions[RightForearm] = Forward * .23 - Right * .43 + Up * .79;
    }

    TArray<FTransform, TInlineAllocator<48>> MeshPose;
    MeshPose.SetNumUninitialized(BindPose.Num());
    for (int32 Bone = 0; Bone < BindPose.Num(); ++Bone)
    {
        const int32 Parent = Parents[Bone];
        const FTransform& ParentPose = Parent == INDEX_NONE ? FTransform::Identity : MeshPose[Parent];
        MeshPose[Bone] = BindPose[Bone] * ParentPose;
        const int32 Joint = JointByBone[Bone];
        if (Joint == INDEX_NONE)
            continue;

        const FQuat Rest = MeshPose[Bone].GetRotation();
        const FQuat Turn = FQuat::FindBetweenNormals(Rest.RotateVector(BoneDirection[Joint]),
            Directions[Joint].GetSafeNormal());
        const FQuat Posed = (Turn * Rest).GetNormalized();
        LocalPose[Bone].SetRotation((ParentPose.GetRotation().Inverse() * Posed).GetNormalized());
        MeshPose[Bone].SetRotation(Posed);
    }

    if (Motion.bArcher)
    {
        const FVector BowUp = (Up - Right * (0.44 * (1.0 - Attack))).GetSafeNormal();
        const FQuat BowFacing(FRotationMatrix::MakeFromYZ(-Forward, BowUp));
        return FTransform(BowFacing, MeshPose[LeftHand].GetLocation(), FVector(0.83));
    }

    if (Motion.bPolearm)
    {
        const FVector Shaft = FMath::Lerp(Up + Forward * .27, Forward + Up * .08, Attack).GetSafeNormal();
        return FTransform(FQuat::FindBetweenNormals(Up, Shaft), MeshPose[RightHand].GetLocation());
    }

    const FVector SwingBlade = (Motion.bParrying ? Up * .62 - Right * .75 + Forward * .13
        : Up * (1.0 - 0.88 * Attack) + Forward * (0.58 + 0.90 * Attack) +
            Right * (Attack * FMath::Lerp(1.2, -1.2, Motion.AttackPhase)));
    const FVector Blade = FMath::Lerp(SwingBlade, Up * .82 - Forward * .52 + Right * .40,
        Motion.bParrying ? 0.0 : double(Motion.Windup)).GetSafeNormal();
    return FTransform(FQuat::FindBetweenNormals(Up, Blade), MeshPose[RightHand].GetLocation(), FVector(0.78));
}
