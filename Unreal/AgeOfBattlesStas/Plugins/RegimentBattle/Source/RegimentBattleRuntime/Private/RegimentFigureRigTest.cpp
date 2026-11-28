#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/SkeletalMesh.h"
#include "RegimentFigureAppearance.h"
#include "RegimentFigureRig.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFigureRigTest, "RegimentBattle.Figures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFigureRigTest::RunTest(const FString& Parameters)
{
    const FString Folder = TEXT("/Game/Fab/Free_LOWPO__Mercenary_Company___Low_Poly_Characters___Weapons/");
    for (const TCHAR* Name : { TEXT("Mercenary_Swordsman"), TEXT("Mercenary_Archer"), TEXT("Mercenary_Captain") })
    {
        const auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Folder + Name + TEXT(".") + Name));
        if (!TestNotNull(Name, Mesh))
            return false;
        const auto& Skeleton = Mesh->GetRefSkeleton();
        const FRegimentFigureRig Rig(Skeleton);
        if (!TestTrue(TEXT("LOWPO character has all driven bones"), Rig.IsValid()))
            return false;

        const FQuat MeshYaw(FVector::UpVector, -Rig.GetMeshForwardYaw());
        TestTrue(TEXT("Mesh forward is corrected to engine X"),
            MeshYaw.RotateVector(Rig.GetMeshForward()).Equals(FVector::ForwardVector, 0.01));
        const bool bArcher = FStringView(Name) == TEXT("Mercenary_Archer");
        TArray<FTransform> Idle;
        TArray<FTransform> Walking;
        TArray<FTransform> Attacking;
        const FTransform IdleGrip = Rig.Evaluate({ 0.f, 0.f, 0.f, bArcher }, Idle);
        Rig.Evaluate({ HALF_PI, 1.f, 0.f, bArcher }, Walking);
        const FTransform AttackGrip = Rig.Evaluate({ 0.f, 0.f, 0.55f, bArcher }, Attacking);
        const int32 Arm = Skeleton.FindBoneIndex(TEXT("upper_arm_R"));
        const int32 Leg = Skeleton.FindBoneIndex(TEXT("thigh_L"));
        TestFalse(TEXT("Idle lowers the arms from rest pose"), Idle[Arm].Equals(Skeleton.GetRefBonePose()[Arm]));
        TestFalse(TEXT("Movement drives the legs"), Walking[Leg].Equals(Idle[Leg]));
        TestFalse(TEXT("Attack animates the weapon grip"), AttackGrip.Equals(IdleGrip, 0.1));
        TestFalse(TEXT("Attack changes the arm pose"), Attacking[Arm].Equals(Idle[Arm]));

        const int32 Pelvis = Skeleton.FindBoneIndex(TEXT("spine"));
        if (!TestTrue(TEXT("The character mesh has a pelvis bone"), Pelvis != INDEX_NONE))
            continue;
        FTransform PelvisPose = Skeleton.GetRefBonePose()[Pelvis];
        for (int32 Parent = Skeleton.GetParentIndex(Pelvis); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
            PelvisPose *= Skeleton.GetRefBonePose()[Parent];
        const FTransform Horse(FRotator(0, 48, 0), FVector(130, 260, 9), FVector(.93));
        FTransform Rider(FRotator(14, 48, -8), FVector::ZeroVector, FVector(1.12));
        Rider.SetTranslation(Rig.SeatMountedRider(Horse, Rider, 140.0));
        const FVector SeatedPelvis = Rider.TransformPosition(PelvisPose.GetTranslation());
        TestTrue(TEXT("The rider's hips stay in the saddle when the figure leans"),
            SeatedPelvis.Equals(Horse.TransformPosition(FVector(0, 0, 140)), .01));

        const int32 StockHead = Skeleton.FindBoneIndex(TEXT("spine_006"));
        TestTrue(TEXT("Every dressed body mesh has a separately hideable stock head"), Rig.HasStockHead());
        FRegimentFigureAnchors Stock, Replacement;
        TArray<FTransform> Visible, Hidden;
        Rig.Evaluate({}, Visible, &Stock);
        FRegimentFigureMotion Costume;
        Costume.bHideStockHead = true;
        Rig.Evaluate(Costume, Hidden, &Replacement);
        if (Visible.IsValidIndex(StockHead) && Hidden.IsValidIndex(StockHead))
        {
            TestFalse(TEXT("The stock cap remains on an unmodified figure"), Visible[StockHead].GetScale3D().IsNearlyZero());
            TestTrue(TEXT("An authored costume removes the stock cap after sampling its attachment"),
                Hidden[StockHead].GetScale3D().IsNearlyZero() &&
                Replacement.At(ERegimentFigureAnchor::Head).Equals(Stock.At(ERegimentFigureAnchor::Head), .001));
            TestTrue(TEXT("The remaining body keeps its original pose"), Hidden[Arm].Equals(Visible[Arm]));
        }

        for (const FRotator& Lean : {FRotator::ZeroRotator, FRotator(7, 0, -11)})
        {
            Rider.SetRotation(Horse.GetRotation() * Lean.Quaternion() * MeshYaw);
            Rider.SetTranslation(Rig.SeatMountedRider(Horse, Rider, FVector(-5, 0, 140)));
            const auto Boots = Rig.MountedBoots(Horse, Rider, FVector(11, 29, 110));
            FRegimentFigureMotion Mounted;
            Mounted.bMounted = true;
            Mounted.MountedBoots = Boots;
            Mounted.bCourier = true;
            TArray<FTransform> MountedPose;
            Rig.Evaluate(Mounted, MountedPose);
            for (int32 Side : {-1, 1})
            {
                const int32 Foot = Skeleton.FindBoneIndex(Side < 0 ? TEXT("foot_L") : TEXT("foot_R"));
                FTransform BootPose = MountedPose[Foot];
                for (int32 Parent = Skeleton.GetParentIndex(Foot); Parent != INDEX_NONE;
                     Parent = Skeleton.GetParentIndex(Parent))
                    BootPose *= MountedPose[Parent];
                TestTrue(FString::Printf(TEXT("%s boot %d meets the stirrup at lean %g"), Name, Side, Lean.Roll),
                    Rider.TransformPosition(BootPose.GetTranslation()).Equals(
                        Horse.TransformPosition(FVector(11, Side * 29, 110)), .2));
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentDressPoseTest, "RegimentBattle.Graphics.HistoricalDressPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentDressPoseTest::RunTest(const FString&)
{
    const FQuat Lean = FRotator(14, 51, -8).Quaternion();
    const FQuat MeshYaw(FVector::UpVector, HALF_PI);
    const FVector Feet(807, 190, 35);
    const FTransform Figure(Lean * MeshYaw.Inverse(), Feet + Lean.RotateVector(FVector(0, 0, 4)), FVector(1.1));
    const auto Dress = RegimentHistoricalDressPose(Figure, HALF_PI, 4.0, 198.0);
    TestTrue(TEXT("The clothing uses the same ground pivot after translation and lean"), Dress.GetTranslation().Equals(Feet));
    TestTrue(TEXT("A tall officer's headwear and armor inherit both the lean and stature"),
        Dress.TransformPosition(FVector(-5, 8, 153)).Equals(Feet + Lean.RotateVector(FVector(-6, 9.6, 183.6)), .01) &&
        Dress.TransformPosition(FVector(0, 0, 107)).Equals(Feet + Lean.RotateVector(FVector(0, 0, 128.4)), .01));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFigureAppearanceTest, "RegimentBattle.Graphics.FigureAppearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFigureAppearanceTest::RunTest(const FString& Parameters)
{
    for (uint64 First : {1ull, 2501ull})
    {
        TSet<FColor> SkinTones;
        TSet<FColor> Coats;
        TSet<FColor> Points;
        float LightestSkin = 0.f;
        float DarkestSkin = 1.f;
        float MinCloth = 1.f;
        float MaxCloth = 1.f;
        float MinLeather = 1.f;
        float MaxLeather = 1.f;
        for (uint64 Id = First; Id < First + 80; ++Id)
        {
            const auto Look = RegimentFigureAppearance(Id);
            const auto Same = RegimentFigureAppearance(Id);
            TestEqual(TEXT("Skin color is stable by unit ID"), Look.Skin, Same.Skin);
            TestEqual(TEXT("Horse coat is stable by unit ID"), Look.Coat.Body, Same.Coat.Body);
            SkinTones.Add(Look.Skin.ToFColorSRGB());
            Coats.Add(Look.Coat.Body.ToFColorSRGB());
            Points.Add(Look.Coat.Points.ToFColorSRGB());
            LightestSkin = FMath::Max(LightestSkin, Look.Skin.GetLuminance());
            DarkestSkin = FMath::Min(DarkestSkin, Look.Skin.GetLuminance());
            MinCloth = FMath::Min(MinCloth, Look.ClothShade);
            MaxCloth = FMath::Max(MaxCloth, Look.ClothShade);
            MinLeather = FMath::Min(MinLeather, Look.LeatherShade);
            MaxLeather = FMath::Max(MaxLeather, Look.LeatherShade);
        }
        TestTrue(TEXT("Both sides contain a substantial range of skin tones"),
            SkinTones.Num() >= 6 && LightestSkin > .45f && DarkestSkin < .09f);
        TestTrue(TEXT("Both sides contain distinct horse coats and points"), Coats.Num() >= 4 && Points.Num() >= 3);
        TestTrue(TEXT("Cloth varies subtly within the team color"),
            MaxCloth - MinCloth > .12f && MinCloth >= .85f && MaxCloth <= 1.15f);
        TestTrue(TEXT("Straps have visibly different shades"), MaxLeather - MinLeather > .2f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAnimatedEquipmentTest, "RegimentBattle.Graphics.AnimatedEquipment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAnimatedEquipmentTest::RunTest(const FString&)
{
    const auto* Mesh = LoadObject<USkeletalMesh>(nullptr,
        TEXT("/Game/Fab/Free_LOWPO__Mercenary_Company___Low_Poly_Characters___Weapons/Mercenary_Swordsman.Mercenary_Swordsman"));
    if (!TestNotNull(TEXT("Swordsman"), Mesh))
        return false;
    const FRegimentFigureRig Rig(Mesh->GetRefSkeleton());
    TArray<FTransform> Local;
    FRegimentFigureAnchors Idle, Strike, Guard;
    Rig.Evaluate({}, Local, &Idle);
    FRegimentFigureMotion Swing;
    Swing.AttackPhase = .55f;
    Rig.Evaluate(Swing, Local, &Strike);
    FRegimentFigureMotion Parry;
    Parry.bParrying = true;
    Rig.Evaluate(Parry, Local, &Guard);
    TestFalse(TEXT("The sword-arm plate rotates with a strike"),
        Strike.At(ERegimentFigureAnchor::RightShoulder).Equals(Idle.At(ERegimentFigureAnchor::RightShoulder), .01));
    TestFalse(TEXT("The shield anchor follows the parrying arm"),
        Guard.At(ERegimentFigureAnchor::LeftForearm).Equals(Idle.At(ERegimentFigureAnchor::LeftForearm), .01));
    const FTransform Rider(FRotator(0, 34, 0), FVector(140, -73, 15), FVector(1.1));
    const FTransform Outfit(FRotator(0, 69, 0), {135, -73, 142}, FVector(1.2));
    const FTransform Relative = Outfit.GetRelativeTransform(Rider);
    const auto Posed = Relative * Guard.At(ERegimentFigureAnchor::LeftShoulder);
    const FTransform Fallen(FRotator(11, 34, -84), {144, -78, 9}, Rider.GetScale3D());
    TestTrue(TEXT("A fallen shoulder plate retains its last pose relative to its wearer"),
        (Posed * Fallen).GetRelativeTransform(Fallen).Equals(Posed, .01));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentRiderBootPoseTest, "RegimentBattle.Graphics.RiderBootPose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentRiderBootPoseTest::RunTest(const FString&)
{
    const FVector Hip(0, 11, 126);
    const FVector Ankle(11, 32, 63);
    const auto Leg = SolveRegimentRiderLeg(Hip, Ankle, 39, 38, FVector::ForwardVector);
    TestTrue(TEXT("A reachable stirrup is met by the ankle"), Leg.Ankle.Equals(Ankle, .01));
    TestTrue(TEXT("The knee leads the ankle while preserving the upper and lower segments"),
        Leg.Knee.X > Ankle.X && FMath::IsNearlyEqual(FVector::Dist(Hip, Leg.Knee), 39.0, .01) &&
        FMath::IsNearlyEqual(FVector::Dist(Leg.Knee, Leg.Ankle), 38.0, .01));
    const auto Extended = SolveRegimentRiderLeg(Hip, {12, 35, -20}, 39, 38, FVector::ForwardVector);
    TestTrue(TEXT("An unreachable stirrup keeps the leg inside the maximum reach"),
        FVector::Dist(Hip, Extended.Ankle) < 77.0 && !Extended.Knee.ContainsNaN());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFigureMotionVariationTest, "RegimentBattle.Graphics.FigureMotionVariation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFigureMotionVariationTest::RunTest(const FString& Parameters)
{
    TSet<int32> PhaseBuckets;
    for (uint64 Id = 1; Id <= 24; ++Id)
    {
        const float Phase = RegimentInitialStridePhase(Id);
        TestEqual(TEXT("Gait starts at a stable phase"), Phase, RegimentInitialStridePhase(Id));
        PhaseBuckets.Add(FMath::FloorToInt(Phase * 12.f / (2.f * UE_PI)));
    }
    TestTrue(TEXT("A rank uses most of the stride cycle"), PhaseBuckets.Num() >= 9);
    const FRotator AtRest = RegimentStandardFlutter(3.0, 4.f, FMath::Square(10000.0));
    TestTrue(TEXT("Standards remain still out of flutter range"), AtRest.IsNearlyZero());
    const FRotator Nearby = RegimentStandardFlutter(3.0, 4.f, 100.0);
    TestTrue(TEXT("Nearby standards move gently"),
        !Nearby.IsNearlyZero() && FMath::Abs(Nearby.Yaw) < 10.0 && FMath::Abs(Nearby.Pitch) < 5.0);
    TestTrue(TEXT("Nearby standards keep moving when the bearer stands"),
        !Nearby.Equals(RegimentStandardFlutter(3.5, 4.f, 100.0)));
    return true;
}
#endif
