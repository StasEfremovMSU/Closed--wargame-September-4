#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattlefieldEffects.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

namespace
{
battle::UnitState EffectsShooter(uint64 Id, float Y, battle::BattlePlatform Platform = battle::BattlePlatform::None)
{
    battle::UnitState Unit;
    Unit.id = {Id};
    Unit.regiment = {1};
    Unit.pose.position.y = Y;
    Unit.platform = Platform;
    Unit.ranged_weapon = battle::RangedWeapon::Musket;
    return Unit;
}

battle::BattleEvent EffectsEvent(battle::EventKind Kind, uint64 Shooter)
{
    battle::BattleEvent Event;
    Event.kind = Kind;
    Event.source = {Shooter};
    return Event;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBlackPowderPresentationTest, "RegimentBattle.Effects.BlackPowder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBlackPowderPresentationTest::RunTest(const FString&)
{
    battle::World World;
    World.tick = 1;
    World.units = {EffectsShooter(1, -3.f), EffectsShooter(2, -2.9f), EffectsShooter(3, 3.f),
        EffectsShooter(4, 0.f, battle::BattlePlatform::FieldGun)};
    const FRegimentEffectView View{FVector(-500, 0, 160)};
    constexpr auto Terrain = battle::MarchTerrain::OpenField;
    FRegimentBattlefieldEffects Effects;
    TArray<FRegimentEffectSprite> Sprites;

    World.events = {EffectsEvent(battle::EventKind::AttackLaunched, 1)};
    Effects.ObserveStep(World, FVector::ZeroVector, Terrain, View.Location);
    Effects.Collect(World, FVector::ZeroVector, View, Sprites);
    TestTrue(TEXT("Only confirmed gunpowder emissions create smoke"), Sprites.IsEmpty());

    ++World.tick;
    World.events = {EffectsEvent(battle::EventKind::SmokeCreated, 1),
        EffectsEvent(battle::EventKind::SmokeCreated, 2), EffectsEvent(battle::EventKind::SmokeCreated, 3),
        EffectsEvent(battle::EventKind::SmokeCreated, 4)};
    Effects.ObserveStep(World, FVector::ZeroVector, Terrain, View.Location);
    Effects.ObserveStep(World, FVector::ZeroVector, Terrain, View.Location);
    Effects.Collect(World, FVector::ZeroVector, View, Sprites);
    if (!TestEqual(TEXT("Nearby muskets share a puff and distant sections of the front stay separate"), Sprites.Num(), 3))
        return false;
    const auto* Cannon = Sprites.FindByPredicate([](const auto& Sprite)
    {
        return Sprite.Kind == ERegimentEffectKind::Cannon;
    });
    const auto* Musket = Sprites.FindByPredicate([](const auto& Sprite)
    {
        return Sprite.Kind == ERegimentEffectKind::Musket;
    });
    if (!TestTrue(TEXT("Both gunpowder profiles are present"), Cannon && Musket))
        return false;
    TestTrue(TEXT("Artillery is larger and denser"), Cannon->HalfWidth > Musket->HalfWidth &&
        Cannon->HalfHeight > Musket->HalfHeight && Cannon->Tint.A > Musket->Tint.A);
    TestTrue(TEXT("Infantry smoke is shallow across the front"), Musket->HalfHeight < Musket->HalfWidth * .5f);
    TestEqual(TEXT("Presentation does not create tactical smoke"), World.smoke.size(), size_t(0));

    FRegimentEffectView OfficerView = View;
    OfficerView.Observer = View.Location;
    World.scenario.obstacles.push_back({-2.f, -10.f, -1.f, 10.f, true, battle::TerrainFeature::StoneWall});
    Effects.Collect(World, FVector::ZeroVector, OfficerView, Sprites);
    TestTrue(TEXT("Smoke behind an officer's obscuring wall remains hidden"), Sprites.IsEmpty());
    Effects.Collect(World, FVector::ZeroVector, View, Sprites);
    TestEqual(TEXT("An elevated view can see the same front"), Sprites.Num(), 3);

    World.tick += 33;
    Effects.Collect(World, FVector::ZeroVector, View, Sprites);
    TestTrue(TEXT("The brief dense smoke gives way to faint tactical residue"), !Sprites.IsEmpty() &&
        !Sprites.ContainsByPredicate([](const auto& Sprite) { return Sprite.Tint.A >= .2f; }));
    Effects.Collect(World, FVector::ZeroVector, View, Sprites, 12.0);
    TestTrue(TEXT("The post-battle clock can finish dissipating a cloud"), Sprites.IsEmpty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCavalryDustPresentationTest, "RegimentBattle.Effects.CavalryDust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCavalryDustPresentationTest::RunTest(const FString&)
{
    constexpr auto Terrain = battle::MarchTerrain::OpenField;
    const FVector Origin = FVector::ZeroVector;
    const FRegimentEffectView Near{FVector(0, 0, 280)};
    const FRegimentEffectView Far{FVector(6000, 0, 280)};
    battle::World World;
    battle::RegimentState Cavalry;
    Cavalry.id = {1};
    Cavalry.units = {{1}};
    World.regiments = {Cavalry};
    auto Rider = EffectsShooter(1, 0.f);
    Rider.troop = battle::UnitTroop::Mounted;
    World.units = {Rider};
    FRegimentBattlefieldEffects Effects;
    TArray<FRegimentEffectSprite> Sprites;
    const auto MoveAndCollect = [&](float Travel, battle::MarchTerrain Map, const FRegimentEffectView& View)
    {
        World.tick += 6;
        World.regiments.front().pose.position.x += Travel;
        Effects.ObserveStep(World, Origin, Map, View.Location);
        Effects.Collect(World, Origin, View, Sprites);
    };
    MoveAndCollect(0.f, Terrain, Near);
    MoveAndCollect(.3f, Terrain, Near);
    TestTrue(TEXT("Slow cavalry produces no airborne dust"), Sprites.IsEmpty());
    MoveAndCollect(2.f, Terrain, Far);
    TestTrue(TEXT("Distant cavalry does not consume the effect pool"), Sprites.IsEmpty());
    Effects.Collect(World, Origin, Near, Sprites);
    TestTrue(TEXT("A camera cut does not reveal unrecorded out-of-range dust"), Sprites.IsEmpty());
    MoveAndCollect(2.f, battle::MarchTerrain::Peipus, Near);
    TestTrue(TEXT("Ice and snow cannot produce dry dust"), Sprites.IsEmpty());
    World.scenario.obstacles.push_back({-5.f, -5.f, 20.f, 5.f, false, battle::TerrainFeature::Marsh});
    MoveAndCollect(2.f, Terrain, Near);
    TestTrue(TEXT("Marsh excludes dust regardless of the map climate"), Sprites.IsEmpty());
    World.scenario.obstacles.clear();
    MoveAndCollect(2.f, Terrain, Near);
    if (!TestEqual(TEXT("One trail puff is emitted for the moving formation"), Sprites.Num(), 1))
        return false;
    TestTrue(TEXT("The trail is dust-colored and lies behind the advancing formation"),
        Sprites[0].Kind == ERegimentEffectKind::Dust && Sprites[0].Tint.R > Sprites[0].Tint.B &&
        Sprites[0].Center.X < World.regiments.front().pose.position.x * 100.f);
    const float InitialAlpha = Sprites[0].Tint.A;
    Effects.Collect(World, Origin, Near, Sprites);
    TestEqual(TEXT("A paused simulation does not age the puff"), Sprites[0].Tint.A, InitialAlpha);
    MoveAndCollect(0.f, Terrain, Near);
    if (!TestEqual(TEXT("A stopped formation's trail lingers while dissipating"), Sprites.Num(), 1))
        return false;
    TestTrue(TEXT("The stopped trail is fading"), Sprites[0].Tint.A < InitialAlpha);
    Effects.Reset();
    Effects.Collect(World, Origin, Near, Sprites);
    TestTrue(TEXT("Reset clears the visible trail"), Sprites.IsEmpty());

    World.regiments.clear();
    World.units.clear();
    World.tick = 6;
    for (uint64 Id = 1; Id <= 64; ++Id)
    {
        auto Formation = Cavalry;
        Formation.id = {Id};
        Formation.units = {{Id}};
        Formation.pose.position.y = float(Id % 8);
        World.regiments.push_back(Formation);
        Rider.id = {Id};
        Rider.regiment = {Id};
        World.units.push_back(Rider);
    }
    Effects.ObserveStep(World, Origin, Terrain, Near.Location);
    World.tick += 6;
    for (auto& Formation : World.regiments)
        Formation.pose.position.x += 2.f;
    Effects.ObserveStep(World, Origin, Terrain, Near.Location);
    Effects.Collect(World, Origin, Near, Sprites);
    TestTrue(TEXT("Massed cavalry shares a strict, small trail budget"), Sprites.Num() > 0 && Sprites.Num() <= 12);
    return !HasAnyErrors();
}
#endif
