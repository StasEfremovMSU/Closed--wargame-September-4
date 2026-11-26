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
    }
    return true;
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
