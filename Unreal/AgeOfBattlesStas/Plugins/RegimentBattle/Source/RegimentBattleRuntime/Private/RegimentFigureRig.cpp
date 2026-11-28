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

FTransform RegimentHistoricalDressPose(const FTransform& FigureTransform, double MeshForwardYaw,
    double FeetOffset, double FigureHeight)
{
    // Dress is authored in a 165 cm, X-forward frame on the wearer's feet.
    const FQuat Rotation = FigureTransform.GetRotation() * FQuat(FVector::UpVector, MeshForwardYaw);
    const FVector Feet = FigureTransform.GetTranslation() - Rotation.RotateVector(FVector(0, 0, FeetOffset));
    return FTransform(Rotation, Feet, FVector(FigureHeight / RegimentFigureReferenceHeight));
}

FRegimentRiderLeg SolveRegimentRiderLeg(FVector Hip, FVector Target, double Upper, double Lower, FVector Forward)
{
    const FVector Change = Target - Hip;
    const FVector Axis = Change.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, -FVector::UpVector);
    const double Distance = FMath::Clamp(Change.Size(), FMath::Abs(Upper - Lower) + .001, Upper + Lower - .001);
    const double KneeAlong = (Upper * Upper - Lower * Lower + Distance * Distance) / (2.0 * Distance);
    const double KneeOut = FMath::Sqrt(FMath::Max(0.0, Upper * Upper - KneeAlong * KneeAlong));
    FVector Bend = FVector::VectorPlaneProject(Forward, Axis).GetSafeNormal();
    if (Bend.IsNearlyZero())
    {
        FVector Other;
        Axis.FindBestAxisVectors(Bend, Other);
    }
    return {Hip + Axis * KneeAlong + Bend * KneeOut, Hip + Axis * Distance};
}

FRegimentFigureRig::FRegimentFigureRig(const FReferenceSkeleton& Skeleton)
    : BindPose(Skeleton.GetRefBonePose())
{
    const int32 NumBones = BindPose.Num();
    Parents.Reserve(NumBones);
    JointByBone.Init(INDEX_NONE, NumBones);
    RestSpace.Reserve(NumBones);
    for (int32 Bone = 0; Bone < NumBones; ++Bone)
    {
        const int32 Parent = Parents.Add_GetRef(Bone == 0 ? INDEX_NONE : Skeleton.GetParentIndex(Bone));
        RestSpace.Add(Parent == INDEX_NONE ? BindPose[Bone] : BindPose[Bone] * RestSpace[Parent]);
    }

    // The LOWPO rig names the hips at the base of the spine chain "spine".
    const int32 PelvisBone = Skeleton.FindBoneIndex(TEXT("spine"));
    if (RestSpace.IsValidIndex(PelvisBone))
        Pelvis = RestSpace[PelvisBone].GetTranslation();
    LeftFoot = Skeleton.FindBoneIndex(TEXT("foot_L"));
    RightFoot = Skeleton.FindBoneIndex(TEXT("foot_R"));
    const int32 Toe = Skeleton.FindBoneIndex(TEXT("toe_L"));
    LeftHand = Skeleton.FindBoneIndex(TEXT("hand_L"));
    RightHand = Skeleton.FindBoneIndex(TEXT("hand_R"));
    if (LeftFoot == INDEX_NONE || RightFoot == INDEX_NONE || Toe == INDEX_NONE ||
        LeftHand == INDEX_NONE || RightHand == INDEX_NONE)
        return;

    const FVector FootForward = (RestSpace[Toe].GetLocation() - RestSpace[LeftFoot].GetLocation()).GetSafeNormal2D();
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
        // Stirrup targets and hips are in component centimeters. The imported rigs
        // store joint offsets below a scaled root, so local lengths are not usable here.
        BoneLength[Joint] = FVector::Distance(RestSpace[Bone].GetLocation(), RestSpace[Child].GetLocation());
        if (BoneDirection[Joint].IsNearlyZero())
            return;
        JointByBone[Bone] = static_cast<int8>(Joint);
    }
    const FName AttachmentBones[] = { NAME_None, TEXT("spine_006"), TEXT("upper_arm_L"), TEXT("upper_arm_R"), TEXT("forearm_L"),
        TEXT("thigh_L"), TEXT("thigh_R") };
    static_assert(UE_ARRAY_COUNT(AttachmentBones) == int32(ERegimentFigureAnchor::Count));
    for (int32 Index = 1; Index < UE_ARRAY_COUNT(AttachmentBones); ++Index)
        AnchorBones[Index] = Skeleton.FindBoneIndex(AttachmentBones[Index]);
    bValid = true;
    FRegimentFigureAnchors Neutral;
    TArray<FTransform> Idle;
    Evaluate({}, Idle, &Neutral);
    NeutralLeftForearm = Neutral.At(ERegimentFigureAnchor::LeftForearm);
}

FVector FRegimentFigureRig::SeatMountedRider(const FTransform& HorseTransform,
    const FTransform& FigureTransform, double SaddleHeight) const
{
    return SeatMountedRider(HorseTransform, FigureTransform, FVector(0, 0, SaddleHeight));
}

FVector FRegimentFigureRig::SeatMountedRider(const FTransform& HorseTransform,
    const FTransform& FigureTransform, FVector Saddle) const
{
    if (!Pelvis)
        return FigureTransform.GetTranslation();
    return HorseTransform.TransformPosition(Saddle) - FigureTransform.TransformVector(*Pelvis);
}

FRegimentMountedBoots FRegimentFigureRig::MountedBoots(const FTransform& Horse,
    const FTransform& Figure, FVector RightAnkle) const
{
    FVector LeftAnkle = RightAnkle;
    LeftAnkle.Y = -LeftAnkle.Y;
    return {Figure.InverseTransformPosition(Horse.TransformPosition(LeftAnkle)),
        Figure.InverseTransformPosition(Horse.TransformPosition(RightAnkle)),
        (Figure.GetRotation().Inverse() * Horse.GetRotation() * FQuat(FVector::UpVector, -GetMeshForwardYaw())).GetNormalized()};
}

FTransform FRegimentFigureRig::LeftShieldDelta(const FRegimentFigureAnchors& Anchors) const
{
    return NeutralLeftForearm.Inverse() * Anchors.At(ERegimentFigureAnchor::LeftForearm);
}

FTransform FRegimentFigureRig::Evaluate(const FRegimentFigureMotion& Motion, TArray<FTransform>& LocalPose,
    FRegimentFigureAnchors* Anchors) const
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
        if (Motion.MountedBoots)
        {
            const auto Fit = [&](EJoint Thigh, EJoint Shin, ERegimentFigureAnchor Anchor, FVector Ankle)
            {
                const FVector Hip = RestSpace[AnchorBones[int32(Anchor)]].GetLocation();
                const auto Leg = SolveRegimentRiderLeg(Hip, Ankle, BoneLength[Thigh], BoneLength[Shin],
                    Motion.MountedBoots->FootTurn.RotateVector(Forward));
                Directions[Thigh] = Leg.Knee - Hip;
                Directions[Shin] = Leg.Ankle - Leg.Knee;
            };
            Fit(LeftThigh, LeftShin, ERegimentFigureAnchor::LeftThigh, Motion.MountedBoots->Left);
            Fit(RightThigh, RightShin, ERegimentFigureAnchor::RightThigh, Motion.MountedBoots->Right);
        }
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

    if (Motion.bMounted && Motion.bCourier)
    {
        Directions[LeftArm] = -Up * .64 + Forward * .51 - Right * .16;
        Directions[LeftForearm] = Forward * .93 + Up * .22 + Right * .20;
        Directions[RightArm] = FMath::Lerp(-Up * .78 + Forward * .36 + Right * .23, Directions[RightArm], Attack);
        Directions[RightForearm] = FMath::Lerp(Forward * .79 + Up * .28 - Right * .24, Directions[RightForearm], Attack);
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
        const bool bRidingBoot = Motion.bMounted && Motion.MountedBoots && (Bone == LeftFoot || Bone == RightFoot);
        if (Joint == INDEX_NONE && !bRidingBoot)
            continue;

        const FQuat Rest = MeshPose[Bone].GetRotation();
        const FQuat Posed = bRidingBoot
            ? (Motion.MountedBoots->FootTurn * RestSpace[Bone].GetRotation()).GetNormalized()
            : (FQuat::FindBetweenNormals(Rest.RotateVector(BoneDirection[Joint]),
                Directions[Joint].GetSafeNormal()) * Rest).GetNormalized();
        LocalPose[Bone].SetRotation((ParentPose.GetRotation().Inverse() * Posed).GetNormalized());
        MeshPose[Bone].SetRotation(Posed);
    }
    if (Anchors)
        for (int32 Index = 1; Index < int32(ERegimentFigureAnchor::Count); ++Index)
        {
            const int32 Bone = AnchorBones[Index];
            Anchors->Deltas[Index] = Bone == INDEX_NONE ? FTransform::Identity : RestSpace[Bone].Inverse() * MeshPose[Bone];
        }
    if (Motion.bHideStockHead && HasStockHead())
        // Preserve the sampled pose for the exposed face and headwear before collapsing the stock hood/helmet.
        LocalPose[AnchorBones[int32(ERegimentFigureAnchor::Head)]].SetScale3D(FVector::ZeroVector);

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
