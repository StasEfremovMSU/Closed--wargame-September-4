#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattleGround.h"
#include "RegimentEraCatalog.h"
#include "RegimentHillPresentation.h"
#include "RegimentMissionScenario.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "battle/Simulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHillIntegrationTest, "RegimentBattle.Hill.PlayablePreset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHillIntegrationTest::RunTest(const FString& Parameters)
{
    constexpr FGuid HillId(0xA16D79C4, 0xB2474F66, 0x98D7143E, 0x622FCE91);
    FMissionPreset Preset;
    if (!TestTrue(TEXT("The hill exercise loads from the built-in catalog"),
            RegimentMissionPresets::LoadBuiltIn(HillId, Preset)))
        return false;

    const auto TestPreset = [this](const FMissionPreset& Variant)
    {
        FText Error;
        if (!TestTrue(TEXT("The hill variant is valid"), RegimentMissionPresets::Validate(Variant, Error)))
        {
            AddError(Error.ToString());
            return;
        }
        const auto Opening = MakeMissionOpening(Variant.Roster, Variant.Terrain, Variant.Field, Variant.FigureTarget);
        const auto Scenario = BuildPresetBattle(Variant, Opening, EMissionDeployment::AuthoredStart);
        if (!TestTrue(TEXT("The tactical scenario receives the terrain profile"), Scenario.hill.has_value()))
            return;
        TestEqual(TEXT("The center of the crest is 4 m above the field"), battle::HillHeight(*Scenario.hill, {}), 4.f);
        TestTrue(TEXT("The map starts directly in tactical play"), Variant.StartsInBattle());
        TestTrue(TEXT("The terrain and the chosen era can create a world together"), bool(battle::CreateWorld(Scenario, 31)));
    };

    TestPreset(Preset);
    for (const TCHAR* Id : {TEXT("ancient"), TEXT("high_medieval")})
    {
        const auto* Era = RegimentEraCatalog::Find(Id);
        if (!TestNotNull(FString::Printf(TEXT("The player can select era %s"), Id), Era))
            continue;
        FMissionPreset Variant = Preset;
        RegimentMissionPresets::ApplyEra(Variant, *Era);
        TestPreset(Variant);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentGroundPickTest, "RegimentBattle.Hill.SurfacePicking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentGroundPickTest::RunTest(const FString& Parameters)
{
    const std::optional<battle::TerrainHill> Hill{battle::TerrainHill{}};
    const FVector FieldOrigin(400, 600, 100);
    const FVector RayOrigin = FieldOrigin + FVector(0, 0, 3000);
    const FVector Down(0, 0, -1);
    const auto Flat = RegimentGroundPoint(RayOrigin, Down, FieldOrigin, {});
    if (TestTrue(TEXT("Flat ground still uses the map's base plane"), Flat.IsSet()))
        TestEqual(TEXT("Flat altitude"), Flat->Z, FieldOrigin.Z);
    const auto Crest = RegimentGroundPoint(RayOrigin, Down, FieldOrigin, Hill);
    if (TestTrue(TEXT("A vertical pick hits the top of the hill"), Crest.IsSet()))
        TestEqual(TEXT("Crest altitude"), Crest->Z, FieldOrigin.Z + 400.0);

    const auto Slope = RegimentGroundPoint(FieldOrigin + FVector(-4800, 0, 3000),
        FVector(1, 0, -1), FieldOrigin, Hill);
    if (TestTrue(TEXT("An oblique pick reaches the sloped surface"), Slope.IsSet()))
    {
        const auto Meters = (*Slope - FieldOrigin) / 100;
        TestTrue(TEXT("The pick is above the base surface and below the crest"), Meters.Z > 0 && Meters.Z < 4);
        TestTrue(TEXT("The pick agrees with the authoritative profile"),
            FMath::IsNearlyEqual(Meters.Z, double(battle::HillHeight(*Hill, {float(Meters.X), float(Meters.Y), 0.f})), .005));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentRectangularHillMeshTest, "RegimentBattle.Hill.RectangularSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentRectangularHillMeshTest::RunTest(const FString& Parameters)
{
    constexpr double HalfWidth = 4000.0;
    constexpr double HalfDepth = 3000.0;
    battle::TerrainHill Hill;
    Hill.radius_x_m = 50.f; // A larger hill is still clipped to the field when rendered.
    const auto Mesh = BuildRegimentHillMesh(Hill, HalfWidth, HalfDepth);
    if (!TestTrue(TEXT("Produces a nonempty surface"), Mesh.TriangleCount() > 0))
        return false;

    const auto* Materials = Mesh.Attributes()->GetMaterialID();
    bool bHasSide[4] = {};
    bool bAllBoundaryEdgesOnRectangle = true;
    for (const int32 Edge : Mesh.BoundaryEdgeIndicesItr())
    {
        const auto Triangle = Mesh.GetEdgeT(Edge).A;
        if (Materials->GetValue(Triangle) != 0)
            continue;
        const auto Vertices = Mesh.GetEdgeV(Edge);
        const auto A = Mesh.GetVertex(Vertices.A);
        const auto B = Mesh.GetVertex(Vertices.B);
        const FVector2d Corners[] = {{-HalfWidth, -HalfDepth}, {HalfWidth, HalfDepth}};
        bool bOnRectangle = false;
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const bool bOnX = A.X == Corners[Side].X && B.X == Corners[Side].X;
            const bool bOnY = A.Y == Corners[Side].Y && B.Y == Corners[Side].Y;
            bHasSide[Side] |= bOnX;
            bHasSide[2 + Side] |= bOnY;
            bOnRectangle |= bOnX || bOnY;
        }
        bAllBoundaryEdgesOnRectangle &= bOnRectangle;
    }
    TestTrue(TEXT("All edges of the rectangular ground form an outer border"), bAllBoundaryEdgesOnRectangle);
    for (bool bSide : bHasSide)
        TestTrue(TEXT("The ground reaches each rectangular edge"), bSide);

    bool bTerrainMatchesSimulation = true;
    bool bInsideField = true;
    for (const int32 Triangle : Mesh.TriangleIndicesItr())
    {
        if (Materials->GetValue(Triangle) != 0)
            continue;
        const auto Vertices = Mesh.GetTriangle(Triangle);
        bTerrainMatchesSimulation &= Mesh.GetTriNormal(Triangle).Z > 0.0;
        for (const int32 Vertex : {Vertices.A, Vertices.B, Vertices.C})
        {
            const auto P = Mesh.GetVertex(Vertex);
            bInsideField &= FMath::Abs(P.X) <= HalfWidth && FMath::Abs(P.Y) <= HalfDepth;
            const double Height = battle::HillHeight(Hill, {float(P.X / 100), float(P.Y / 100), 0.f});
            bTerrainMatchesSimulation &= FMath::IsNearlyEqual(P.Z, Height * 100.0, .001);
        }
    }
    TestTrue(TEXT("The terrain faces up and matches the BattleCore height"), bTerrainMatchesSimulation);
    TestTrue(TEXT("No terrain mesh vertex extends outside the field"), bInsideField);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentRectangularDefaultsTest, "RegimentBattle.MissionPresets.RectangularDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentRectangularDefaultsTest::RunTest(const FString& Parameters)
{
    for (const auto& Item : RegimentMissionPresets::BuiltIns())
    {
        FMissionPreset Preset;
        if (TestTrue(Item.Name, RegimentMissionPresets::LoadBuiltIn(Item.Id, Preset)))
            TestTrue(Item.Name + TEXT(" starts on a wider rectangular field"), Preset.Field.width_m > Preset.Field.height_m);
    }
    return !HasAnyErrors();
}
#endif
