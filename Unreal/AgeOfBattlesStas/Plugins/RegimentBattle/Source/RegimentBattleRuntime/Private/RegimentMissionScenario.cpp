#include "RegimentMissionScenario.h"
#include "RegimentCommanderProfiles.h"
#include "RegimentMissionTutorial.h"
#include "Internationalization/Internationalization.h"
#include "RegimentEraCatalog.h"
#include "RegimentBattlePlayback.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Formation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
constexpr float FigureRadius = .25f;
constexpr float Spacing = 1.1f;
constexpr int32 TeamCount = MissionRegimentCount / MissionRegimentsPerTeam;
constexpr int32 GroupCount = TeamCount * MissionGroupsPerTeam;
constexpr int32 ArmyBodyguards = 3;
constexpr int32 ArmyAdjutants = 2;
constexpr int32 ArmyStaffCount = ArmyBodyguards + ArmyAdjutants;
constexpr uint64 MinimumUnitIdStride = 128;

static_assert(MissionGroupsPerTeam == battle::MarchCrossings.size());

battle::Vec3 RallyStation(int32 Index, int32 Team, battle::FormationFootprint Size, battle::MarchField Field)
{
    const int32 Position = Index % MissionRegimentsPerTeam;
    const int32 Rank = Position % MissionDefaultRegimentsPerGroup;
    const auto& Crossing = battle::MarchCrossings[Position / MissionDefaultRegimentsPerGroup];
    // The second block uses a different station along the same lane.
    const float RearInset = (Rank == 0 ? 21.f : 8.f) + (Index >= MissionRegimentCount ? 26.f : 0.f);
    const float RearLimit = FMath::Max(0.f, std::floor(Field.HalfWidth() - std::hypot(Size.width_m, Size.depth_m) * .5f));
    const float SideLimit = FMath::Max(0.f, std::floor(Field.HalfHeight() - std::max(Size.width_m, Size.depth_m) * .5f));
    const float LateralOffset = Crossing.half_width + (Crossing.ford ? 4.f : 2.f);
    const float Rear = std::clamp(Field.HalfWidth() - RearInset, 0.f, RearLimit);
    const float Side = std::clamp(Crossing.y + (Rank == 0 ? LateralOffset : -LateralOffset),
        -SideLimit, SideLimit);
    return {(Team == 0 ? -1.f : 1.f) * Rear, Side, 0.f};
}

int32 NavigationSpacing(battle::MarchField Field)
{
    if (Field.width_m >= 720 && Field.height_m >= 480)
        return 3;
    return Field.width_m >= 400 && Field.height_m >= 250 ? 2 : 1;
}

float SnapToNavigationCell(float Coordinate, int32 Extent, float CellSpacing)
{
    const float Origin = -Extent * .5f;
    return std::round((Coordinate - Origin) / CellSpacing) * CellSpacing + Origin;
}

struct FDeployment
{
    battle::Pose Pose;
    battle::FormationFootprint Size;
};

std::optional<battle::Pose> FindDeployment(battle::Pose Anchor, battle::FormationFootprint Size,
    const battle::GridNavigationService& Navigation, std::span<const FDeployment> Deployed,
    battle::MarchTerrain Terrain, battle::MarchField Field)
{
    const bool bHasRiver = battle::MarchHasWaterCrossings(Terrain);
    const auto GridHeading = [](float Yaw)
    {
        constexpr float QuarterTurn = battle::Pi * .5f;
        return std::round(Yaw / QuarterTurn) * QuarterTurn;
    };
    const auto Fits = [&](battle::Pose Pose)
    {
        if (bHasRiver && std::abs(Anchor.position.x) >= battle::MarchRiverHalfWidth &&
            (Pose.position.x * (Anchor.position.x < 0.f ? -1.f : 1.f)) < battle::MarchRiverHalfWidth)
            return false;
        if (std::abs(Pose.position.x) > Field.HalfWidth() ||
            std::abs(Pose.position.y) > Field.HalfHeight())
            return false;
        const battle::Pose GridTurn{Pose.position, GridHeading(Pose.yaw)};
        const battle::Pose GridCell{{
            SnapToNavigationCell(Pose.position.x, Field.width_m, NavigationSpacing(Field)),
            SnapToNavigationCell(Pose.position.y, Field.height_m, NavigationSpacing(Field)),
            Pose.position.z}, GridTurn.yaw};
        if (!Navigation.CanTraverse(Pose, GridTurn, Size) || !Navigation.CanTraverse(GridTurn, GridCell, Size))
            return false;
        return std::none_of(Deployed.begin(), Deployed.end(), [&](const FDeployment& Other)
        {
            return battle::RectanglesOverlap(Pose, Size, Other.Pose, Other.Size);
        });
    };

    if (Fits(Anchor))
        return Anchor;
    auto Aligned = Anchor;
    Aligned.yaw = GridHeading(Anchor.yaw);
    if (Fits(Aligned))
        return Aligned;
    const bool bQuarterTurn = std::abs(std::sin(Aligned.yaw)) > .5f;
    const float FieldWidth = bQuarterTurn ? Field.height_m : Field.width_m;
    const float FieldDepth = bQuarterTurn ? Field.width_m : Field.height_m;
    if (Size.depth_m > FieldWidth + 1.f || Size.width_m > FieldDepth + 1.f)
        return std::nullopt;

    const int32 MaxSearchRadius = FMath::Max(Field.width_m, Field.height_m);
    for (int32 Radius = 1; Radius <= MaxSearchRadius; ++Radius)
    {
        for (int32 X = -Radius; X <= Radius; ++X)
        {
            // Test only the perimeter of this search ring.
            const int32 StepY = FMath::Abs(X) == Radius ? 1 : 2 * Radius;
            for (int32 Y = -Radius; Y <= Radius; Y += StepY)
            {
                auto Candidate = Anchor;
                Candidate.position = Anchor.position + battle::Vec3{float(X), float(Y), 0.f};
                if (Fits(Candidate))
                    return Candidate;
            }
        }
    }
    return std::nullopt;
}

const battle::MarchRegiment* ClosestEnemy(const battle::MarchRegiment& Regiment,
    const std::array<const battle::MarchRegiment*, MissionRosterCapacity>& ById)
{
    const battle::MarchRegiment* Result = nullptr;
    float Closest = TNumericLimits<float>::Max();
    for (const auto* Other : ById)
    {
        if (!Other || Other->team == Regiment.team)
            continue;
        const float Distance = battle::Distance(Other->pose.position, Regiment.pose.position);
        if (Distance < Closest)
        {
            Result = Other;
            Closest = Distance;
        }
    }
    return Result;
}

battle::command::OfficerProfile CommanderProfile(battle::OfficerId Id)
{
    battle::command::OfficerProfile Profile;
    Profile.officer = Id;
    Profile.command_radius_m = 75.f;
    Profile.discipline = .8f;
    Profile.tactical_skill = .7f;
    Profile.initiative = .7f;
    Profile.aggression = .65f;
    Profile.leadership = .65f;
    return Profile;
}

battle::command::NodeId ArmyId(int32 Team, int32 RegimentSlots)
{
    return {uint64(RegimentSlots + GroupCount + Team + 1)};
}

constexpr int32 FMissionRosterEntry::* TroopCounts[] = {
    &FMissionRosterEntry::Swords, &FMissionRosterEntry::Archers, &FMissionRosterEntry::Spearmen,
    &FMissionRosterEntry::Mounted
};

int32 TroopCount(const FMissionRosterEntry& Entry)
{
    return Entry.Swords + Entry.Archers + Entry.Spearmen + Entry.Mounted + Entry.PlatformCount;
}

struct FMissionStrength
{
    int32 Troops = 0;
    int32 Staff = 0;
    int32 Platforms = 0;
};

FMissionStrength CountMissionStrength(const FMissionRoster& Roster, bool bArmyStaff = false)
{
    FMissionStrength Count;
    std::array<bool, TeamCount> Teams{};
    std::array<bool, GroupCount> Groups{};
    for (int32 Index = 0; Index < MissionRosterCapacity; ++Index)
    {
        const auto& Entry = Roster[Index];
        if (!Entry.bEnabled)
            continue;
        const auto Resolved = Entry.Clamped();
        Count.Platforms += Resolved.PlatformCount;
        Count.Troops += TroopCount(Resolved) - Resolved.PlatformCount;
        Count.Staff += MissionCommandStaffCount;
        const int32 Team = MissionTeam(Index);
        Teams[Team] = true;
        Groups[Team * MissionGroupsPerTeam + MissionGroup(Index, Entry) - 1] = true;
    }
    Count.Staff += int32(std::ranges::count(Teams, true) + std::ranges::count(Groups, true));
    if (bArmyStaff)
        Count.Staff += int32(std::ranges::count(Teams, true)) * ArmyStaffCount;
    return Count;
}

FMissionRoster ResolveMissionRoster(const FMissionRoster& Roster, int32 FigureTarget, bool bArmyStaff = false)
{
    auto Resolved = Roster;
    for (auto& Entry : Resolved)
        Entry = Entry.Clamped();
    if (!FigureTarget)
        return Resolved;

    const auto Original = CountMissionStrength(Roster, bArmyStaff);
    const int32 TroopTarget = FigureTarget - Original.Staff - Original.Platforms;
    if (Original.Troops == 0 || TroopTarget <= Original.Troops)
        return Resolved;

    struct FAllocation
    {
        int32 Slot;
        int32 FMissionRosterEntry::* Role;
        int32 Remainder;
    };
    TArray<FAllocation, TInlineAllocator<MissionRosterCapacity * UE_ARRAY_COUNT(TroopCounts)>> Remainders;
    int32 Allocated = 0;
    for (int32 Slot = 0; Slot < MissionRosterCapacity; ++Slot)
    {
        auto& Entry = Resolved[Slot];
        if (!Entry.bEnabled)
            continue;
        for (auto Role : TroopCounts)
        {
            auto& Count = Entry.*Role;
            if (!Count)
                continue;
            const int64 WeightedCount = int64(Count) * TroopTarget;
            Count = int32(WeightedCount / Original.Troops);
            Allocated += Count;
            Remainders.Add({Slot, Role, int32(WeightedCount % Original.Troops)});
        }
    }
    // Largest remainders get the spare places, with ties resolved by roster and troop order.
    Remainders.StableSort([](const FAllocation& A, const FAllocation& B) { return A.Remainder > B.Remainder; });
    for (int32 Index = 0; Index < TroopTarget - Allocated; ++Index)
    {
        const auto& Item = Remainders[Index];
        ++(Resolved[Item.Slot].*Item.Role);
    }
    return Resolved;
}

battle::FormationSpec MissionFormation(const FMissionRosterEntry& Entry, bool bLinearInfantry = false,
    const FRegimentEraInfantryProfile* Infantry = nullptr)
{
    // Keep the full model clear of the adjacent rank, including its crew and harness.
    using battle::BattlePlatform;
    switch (Entry.Platform)
    {
    case BattlePlatform::IndianElephant: return {2, 3.8f, 1.75f};
    case BattlePlatform::AfricanElephant: return {2, 4.5f, 2.05f};
    case BattlePlatform::ScythedChariot: return {2, 5.2f, 2.45f};
    case BattlePlatform::Catapult:
    case BattlePlatform::Ballista:
        return {uint32(Entry.PlatformCount + MissionCommandStaffCount), 3.1f, 1.4f};
    case BattlePlatform::FieldGun:
        return {uint32(Entry.PlatformCount + MissionCommandStaffCount), 3.8f, 1.7f};
    default: break;
    }
    const int32 Count = TroopCount(Entry) + MissionCommandStaffCount;
    const int32 SquareColumns = FMath::RoundToInt(FMath::Sqrt(float(Count)));
    const int32 DefaultColumns = Count <= MissionMaxRegiment ? FMath::Clamp(SquareColumns, 3, 7) : SquareColumns;
    const int32 Columns = Entry.FormationFiles > 0 ? FMath::Min(Entry.FormationFiles, Count) : DefaultColumns;
    battle::FormationSpec Formation{uint32(Columns), Entry.bCavalry ? 1.2f : Spacing,
        Entry.bCavalry ? .32f : FigureRadius};
    if (Infantry && Infantry->bLooseOrder)
        battle::OpenSkirmishSpacing(Formation);
    else if (bLinearInfantry && !Entry.bCavalry && Entry.FormationFiles == 0)
    {
        const bool bRangedMajority = Infantry ? Infantry->Role == ERegimentEraRole::Archers : Entry.Archers > Count / 2;
        return battle::LinearInfantryFormation(Formation, uint32(Count), bRangedMajority);
    }
    return Formation;
}

const FRegimentEraInfantryProfile* FindInfantryProfile(const FRegimentEraFaction* Faction, const FMissionRosterEntry& Entry)
{
    if (!Faction || Entry.InfantryProfile.IsEmpty() || Entry.bCavalry || Entry.Platform != battle::BattlePlatform::None)
        return nullptr;
    return Faction->InfantryProfiles.Find(Entry.InfantryProfile);
}

auto MissionInfantryProfiles(const FMissionRoster& Roster, const FRegimentEraFaction* Blue,
    const FRegimentEraFaction* Orange)
{
    std::array<const FRegimentEraInfantryProfile*, MissionRosterCapacity> Profiles{};
    for (int32 Index = 0; Index < MissionRosterCapacity; ++Index)
        Profiles[Index] = FindInfantryProfile(MissionTeam(Index) == 0 ? Blue : Orange, Roster[Index]);
    return Profiles;
}

bool IncludesArmyStaff(const FMissionPreset& Preset, const FRegimentEraDefinition* Era)
{
    return Era && (!Preset.FigureTarget || Preset.FigureTarget >= MissionFigureCount(Preset.Roster, true));
}

battle::FormationFootprint FormationSize(battle::FormationSpec Formation, int32 Count)
{
    const uint32 Columns = FMath::Min(Formation.columns, uint32(Count));
    const uint32 Rows = (uint32(Count) + Columns - 1) / Columns;
    return {(Columns - 1) * Formation.spacing_m + 2.f * Formation.body_radius_m,
            (Rows - 1) * Formation.spacing_m + 2.f * Formation.body_radius_m};
}
}

int32 MissionFigureCount(const FMissionRoster& Roster, bool bArmyStaff)
{
    const auto Count = CountMissionStrength(Roster, bArmyStaff);
    return Count.Troops + Count.Staff + Count.Platforms;
}

battle::GridNavigationService MakeMissionNavigation(const std::vector<battle::Obstacle>& Obstacles,
    battle::MarchField Field)
{
    const int32 Cell = NavigationSpacing(Field);
    return battle::GridNavigationService(Obstacles, float(Cell),
        uint32(FMath::DivideAndRoundUp(Field.width_m, Cell) + 1),
        uint32(FMath::DivideAndRoundUp(Field.height_m, Cell) + 1),
        {-Field.HalfWidth(), -Field.HalfHeight(), 0.f});
}

TOptional<int32> FindInvalidScaledDeployment(const FMissionPreset& Preset)
{
    const auto* Era = Preset.Era.IsSet() ? RegimentEraCatalog::Find(Preset.Era.Id) : nullptr;
    const auto Roster = ResolveMissionRoster(Preset.Roster, Preset.FigureTarget, IncludesArmyStaff(Preset, Era));
    const auto Obstacles = battle::MarchObstacles(Preset.Terrain, Preset.Field);
    const auto Navigation = MakeMissionNavigation(Obstacles, Preset.Field);
    const bool bLinearInfantry = Preset.bRegimentTactics && Era && Era->Rules.linear_infantry;
    const auto* Blue = Era ? RegimentEraCatalog::FindFaction(*Era, Preset.Era.BlueFaction) : nullptr;
    const auto* Orange = Era ? RegimentEraCatalog::FindFaction(*Era, Preset.Era.OrangeFaction) : nullptr;
    std::vector<FDeployment> Deployed;
    for (int32 Index = 0; Index < MissionRosterCapacity; ++Index)
    {
        const auto& Entry = Roster[Index];
        if (!Entry.bEnabled)
            continue;
        const auto* Infantry = FindInfantryProfile(MissionTeam(Index) == 0 ? Blue : Orange, Entry);
        const auto Size = FormationSize(MissionFormation(Entry, bLinearInfantry, Infantry),
            TroopCount(Entry) + MissionCommandStaffCount);
        if (Size.depth_m > Preset.Field.width_m || Size.width_m > Preset.Field.height_m)
            return Index;
        if (!Preset.StartsInBattle())
            continue;

        const auto Start = MissionStartingPosition(Index, Entry, Preset.Field);
        const battle::Pose Anchor{Start, MissionTeam(Index) == 0 ? 0.f : battle::Pi};
        const auto Pose = FindDeployment(Anchor, Size, Navigation, Deployed, Preset.Terrain, Preset.Field);
        if (!Pose)
            return Index;
        Deployed.push_back({*Pose, Size});
    }
    return {};
}

namespace
{
std::vector<battle::MarchRegiment> MakeOpening(const FMissionRoster& Roster, battle::MarchTerrain Terrain,
    battle::MarchField Field, int32 FigureTarget, bool bArmyStaff,
    std::span<const FRegimentEraInfantryProfile* const> InfantryProfiles)
{
    const auto ResolvedRoster = ResolveMissionRoster(Roster, FigureTarget, bArmyStaff);
    std::vector<battle::MarchRegiment> Regiments;
    Regiments.reserve(MissionRosterExtent(Roster));
    auto Lanes = battle::MarchCrossingsFor(Terrain);
    if (Lanes.empty())
        Lanes = battle::MarchCrossings;

    for (int32 Index = 0; Index < MissionRosterCapacity; ++Index)
    {
        const auto& Entry = Roster[Index];
        if (!Entry.bEnabled)
            continue;
        const auto Start = MissionStartingPosition(Index, Entry, Field);
        const int32 Team = MissionTeam(Index);
        const int32 Rank = Index % MissionDefaultRegimentsPerGroup;
        const auto Lane = std::ranges::min_element(Lanes, {},
            [Start](const auto& Crossing) { return std::abs(Crossing.y - Start.y); });
        const float WaypointX = -17.f + (Team == 0 ? -12.f : 12.f) * Rank;
        Regiments.push_back({
            {uint64(Index + 1)}, Team, {Start, Team == 0 ? 0.f : battle::Pi},
            battle::Vec3{WaypointX, Lane->y, 0.f}});
        const auto* Infantry = Index < int32(InfantryProfiles.size()) ? InfantryProfiles[Index] : nullptr;
        Regiments.back().formation = MissionFormation(ResolvedRoster[Index], false, Infantry);
        Regiments.back().figure_count = TroopCount(ResolvedRoster[Index]) + MissionCommandStaffCount;
        Regiments.back().troop = Entry.bCavalry ? battle::UnitTroop::Mounted : battle::UnitTroop::Infantry;
        Regiments.back().fixed_frontage = battle::IsSiegeEngine(Entry.Platform);
        if (Regiments.back().fixed_frontage)
            Regiments.back().posture = battle::MarchPosture::BattleLine;
    }

    // Keep the original staging lanes whenever a pair can make contact. With
    // sparse armies, have one orange regiment approach the nearest blue lane.
    battle::MarchRegiment* Lead = nullptr;
    float StagingY = 0.f;
    float LaneDistance = TNumericLimits<float>::Max();
    std::ranges::stable_sort(Regiments, {}, &battle::MarchRegiment::team);
    const auto Orange = std::ranges::find(Regiments, 1, &battle::MarchRegiment::team);
    for (auto Blue = Regiments.begin(); Blue != Orange; ++Blue)
    for (auto Enemy = Orange; Enemy != Regiments.end(); ++Enemy)
    {
        const float Distance = std::abs(Blue->waypoint->y - Enemy->waypoint->y);
        if (Distance < LaneDistance)
        {
            Lead = &*Enemy;
            StagingY = Blue->waypoint->y;
            LaneDistance = Distance;
        }
    }
    if (Lead)
        Lead->waypoint->y = StagingY;
    return Regiments;
}
}

std::vector<battle::MarchRegiment> MakeMissionOpening(const FMissionRoster& Roster, battle::MarchTerrain Terrain,
    battle::MarchField Field, int32 FigureTarget)
{
    return MakeOpening(Roster, Terrain, Field, FigureTarget, false, {});
}

std::vector<battle::MarchRegiment> MakeMissionOpening(const FMissionPreset& Preset)
{
    const auto* Era = Preset.Era.IsSet() ? RegimentEraCatalog::Find(Preset.Era.Id) : nullptr;
    const auto* Blue = Era ? RegimentEraCatalog::FindFaction(*Era, Preset.Era.BlueFaction) : nullptr;
    const auto* Orange = Era ? RegimentEraCatalog::FindFaction(*Era, Preset.Era.OrangeFaction) : nullptr;
    auto Opening = MakeOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget, IncludesArmyStaff(Preset, Era),
        MissionInfantryProfiles(Preset.Roster, Blue, Orange));
    if (Preset.Objective.Goal == EMissionGoal::HillDefense)
    {
        double SumX[2] = {};
        int32 Counts[2] = {};
        for (const auto& Regiment : Opening)
        {
            SumX[Regiment.team] += Regiment.pose.position.x;
            ++Counts[Regiment.team];
        }
        if (Counts[0] && Counts[1] && SumX[0] / Counts[0] > SumX[1] / Counts[1])
            for (auto& Regiment : Opening)
                Regiment.pose.yaw = Regiment.team == 0 ? battle::Pi : 0.f;
    }
    return Opening;
}

battle::Scenario BuildMissionBattle(const FMissionRoster& Roster, std::span<const battle::MarchRegiment> March,
    battle::MarchTerrain Terrain, EMissionDeployment DeploymentMode, bool bRegimentTactics, battle::MarchField Field,
    int32 FigureTarget, bool bLinearInfantry, bool bArmyStaff,
    std::span<const FRegimentEraInfantryProfile* const> InfantryProfiles, bool bReverseFacing)
{
    namespace Command = battle::command;
    const auto FacesEast = [bReverseFacing](int32 Team) { return (Team == 0) != bReverseFacing; };
    const int32 RegimentSlots = MissionRosterExtent(Roster);
    const auto ResolvedRoster = ResolveMissionRoster(Roster, FigureTarget, bArmyStaff);
    uint64 UnitIdStride = MinimumUnitIdStride;
    for (const auto& Entry : ResolvedRoster)
        if (Entry.bEnabled)
            UnitIdStride = FMath::Max(UnitIdStride, uint64(TroopCount(Entry) + MissionCommandStaffCount + 1));

    battle::Scenario Scenario;
    if (FigureTarget > 0)
        Scenario.units.reserve(FigureTarget);
    Scenario.rules.local_melee = true;
    Scenario.rules.regiment_tactics = bRegimentTactics;
    Scenario.rules.mixed_rank_tactics = bRegimentTactics;
    Scenario.rules.sustained_melee = bRegimentTactics;
    Scenario.rules.controlled_arrow_aim = true;
    Scenario.obstacles = battle::MarchTerrainAreas(Terrain, Field);
    Scenario.hill = battle::MarchHill(Terrain, Field);
    Scenario.field_bounds = battle::TerrainFieldBounds{Field.HalfWidth(), Field.HalfHeight()};
    auto& Setup = Scenario.command_setup.emplace();
    const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Field);
    std::vector<FDeployment> Deployed;
    std::array<bool, GroupCount> GroupHasCavalry{};
    std::array<std::optional<battle::Pose>, TeamCount> ArmyRear{};
    std::array<const battle::MarchRegiment*, MissionRosterCapacity> ById{};
    for (const auto& Regiment : March)
    {
        if (Regiment.id.value < 1 || Regiment.id.value > uint64(RegimentSlots))
            continue;
        const int32 Index = int32(Regiment.id.value - 1);
        if (Roster[Index].bEnabled && Regiment.team == MissionTeam(Index))
            ById[Index] = &Regiment;
    }

    for (int32 Index = 0; Index < RegimentSlots; ++Index)
    {
        const auto* Company = ById[Index];
        if (!Company)
            continue;
        const auto& Entry = ResolvedRoster[Index];
        const int32 Count = TroopCount(Entry) + MissionCommandStaffCount;
        const bool bMountedRegiment = Entry.bCavalry;
        const int32 GroupIndex = Company->team * MissionGroupsPerTeam + MissionGroup(Index, Entry) - 1;
        GroupHasCavalry[GroupIndex] |= bMountedRegiment;
        const int32 StaffStart = Entry.Mounted + Entry.Spearmen + Entry.Swords + Entry.PlatformCount;
        auto Anchor = Company->pose;
        if (DeploymentMode == EMissionDeployment::MarchContact)
        {
            if (const auto* Enemy = ClosestEnemy(*Company, ById))
            {
                const auto Direction = Enemy->pose.position - Anchor.position;
                Anchor.yaw = std::atan2(Direction.y, Direction.x);
            }
            // Match the tactical grid so a first path does not snap into a neighbor.
            Anchor.position.x = SnapToNavigationCell(Anchor.position.x, Field.width_m, NavigationSpacing(Field));
            Anchor.position.y = SnapToNavigationCell(Anchor.position.y, Field.height_m, NavigationSpacing(Field));
        }
        battle::RegimentSpec Regiment;
        Regiment.id = Company->id;
        Regiment.team = Company->team;
        Regiment.name = TCHAR_TO_UTF8(*FString::Printf(TEXT("%s %d%s"),
            Company->team == 0 ? TEXT("Синий") : TEXT("Оранжевый"),
            MissionTeamSlot(Index) + 1, bMountedRegiment ? TEXT(" · конный") : TEXT("")));
        const auto* Infantry = Index < int32(InfantryProfiles.size()) ? InfantryProfiles[Index] : nullptr;
        Regiment.loose_skirmish = Infantry && Infantry->bLooseOrder;
        Regiment.formation = MissionFormation(Entry, bLinearInfantry, Infantry);
        const auto Size = FormationSize(Regiment.formation, Count);
        auto DeploymentSize = Size;
        if (bRegimentTactics && DeploymentMode == EMissionDeployment::MarchContact &&
            !battle::IsSiegeEngine(Entry.Platform))
        {
            Regiment.deployment_ticks = battle::Tick(FMath::CeilToInt(
                Company->DeploymentSeconds() * FRegimentBattlePlayback::StepsPerSecond));
            Regiment.initial_fatigue = Company->fatigue;
            if (Regiment.deployment_ticks > 0)
            {
                const auto ColumnSize = FormationSize(battle::TravelColumnFormation(Regiment.formation), Count);
                DeploymentSize = {FMath::Max(Size.width_m, ColumnSize.width_m),
                                  FMath::Max(Size.depth_m, ColumnSize.depth_m)};
            }
        }
        Regiment.pose = FindDeployment(Anchor, DeploymentSize, Navigation, Deployed, Terrain, Field).value_or(Anchor);
        Regiment.rally_point = RallyStation(Index, Regiment.team, Size, Field);
        Regiment.speed = bMountedRegiment ? 5.2f : 3.f;
        Regiment.skirmisher_count = Infantry ? 0u : uint32(FMath::Min(3, Entry.Archers / 2));
        Scenario.regiments.push_back(Regiment);
        Deployed.push_back({Regiment.pose, DeploymentSize});

        const uint64 Base = uint64(Index) * UnitIdStride;
        for (int32 Slot = 0; Slot < Count; ++Slot)
        {
            battle::UnitSpec Unit;
            Unit.id = {Base + uint64(Slot) + 1};
            Unit.regiment = Regiment.id;
            Unit.pose = Regiment.pose;
            const bool bMounted = bMountedRegiment;
            const bool bSpearman = Slot < Entry.Spearmen;
            Unit.troop = bMounted ? battle::UnitTroop::Mounted
                       : bSpearman ? battle::UnitTroop::Spearman : battle::UnitTroop::Infantry;
            Unit.health = bMounted ? 24.f : 18.f;
            Unit.speed = bMounted ? 6.4f : 4.f;
            Unit.archer = Slot >= StaffStart + MissionCommandStaffCount;
            Unit.weapon.damage = Unit.archer ? 2.f : bMounted ? 3.4f : bSpearman ? 2.8f : 3.f;
            Unit.weapon.range = Unit.archer ? 16.f : bSpearman ? 2.65f : bMounted ? 2.15f : 1.65f;
            if (bSpearman)
            {
                Unit.weapon.windup_ticks = 4;
                Unit.weapon.cooldown_ticks = 22;
            }
            Scenario.units.push_back(Unit);
        }

        const uint64 StaffId = uint64(Index * MissionCommandStaffCount + 1);
        const Command::NodeId LeafId{Regiment.id.value};
        Scenario.officers.push_back({{StaffId}, {Base + uint64(StaffStart + 1)}, 2, 1.f, .7f, .6f, 12.f});
        Scenario.officers.push_back({{StaffId + 1}, {Base + uint64(StaffStart + 2)}, 3, 1.f, 0.f, 0.f, 12.f});
        Scenario.officers.push_back({{StaffId + 2}, {Base + uint64(StaffStart + 3)}, 4, 0.f, 0.f, 1.f, 12.f});
        Setup.hierarchy.support.push_back({{StaffId + 1}, LeafId, Command::SupportDuty::StandardBearer});
        Setup.hierarchy.support.push_back({{StaffId + 2}, LeafId, Command::SupportDuty::Musician});
        Setup.hierarchy.officers.push_back(CommanderProfile({StaffId}));

        Command::CommandNode Leaf;
        Leaf.id = LeafId;
        Leaf.parent = {uint64(RegimentSlots + GroupIndex + 1)};
        Leaf.level = Command::Level::Regiment;
        Leaf.team = Regiment.team;
        Leaf.name = Regiment.name;
        Leaf.commander = {StaffId};
        Leaf.regiment = Regiment.id;
        Setup.hierarchy.nodes.push_back(std::move(Leaf));
    }

    for (int32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
    {
        Command::CommandNode Group;
        Group.id = {uint64(RegimentSlots + GroupIndex + 1)};
        Group.level = Command::Level::Group;
        std::optional<battle::Pose> Rear;
        for (const auto& Leaf : Setup.hierarchy.nodes)
        {
            if (Leaf.parent != Group.id)
                continue;
            Group.children.push_back(Leaf.id);
            Group.team = Leaf.team;
            const auto Regiment = std::find_if(Scenario.regiments.begin(), Scenario.regiments.end(),
                [&](const auto& Candidate) { return Candidate.id == Leaf.regiment; });
            const auto& Deployment = Deployed[Regiment - Scenario.regiments.begin()];
            auto Position = Regiment->pose;
            Position.yaw = FacesEast(Group.team) ? 0.f : battle::Pi;
            Position.position = Position.position -
                battle::Forward(Position.yaw) * (Deployment.Size.depth_m * .5f + 3.f);
            if (!Rear || (FacesEast(Group.team) ? Position.position.x < Rear->position.x
                                         : Position.position.x > Rear->position.x))
                Rear = Position;
        }
        if (!Rear)
            continue;

        Group.parent = ArmyId(Group.team, RegimentSlots);
        const int32 Lane = GroupIndex % MissionGroupsPerTeam;
        const int32 Wing = FacesEast(Group.team) ? Lane : MissionGroupsPerTeam - Lane - 1;
        constexpr std::array Roles = {Command::SectorRole::LeftFlank, Command::SectorRole::Center,
            Command::SectorRole::RightFlank};
        const TCHAR* const Names[] = {TEXT("Левый фланг"), TEXT("Центр"), TEXT("Правый фланг")};
        Group.sector_role = Roles[Wing];
        Group.name = TCHAR_TO_UTF8(Names[Wing]);
        Group.commander = {uint64(RegimentSlots * MissionCommandStaffCount + GroupIndex + 1)};

        battle::UnitSpec Commander;
        const bool bMountedCommander = GroupHasCavalry[GroupIndex];
        Commander.id = {RegimentSlots * UnitIdStride + uint64(GroupIndex + 1)};
        Commander.independent_team = Group.team;
        const battle::FormationFootprint CommanderSize = bMountedCommander
            ? battle::FormationFootprint{.75f, 1.65f} : battle::FormationFootprint{.6f, .6f};
        Rear->position.x = FMath::Clamp(Rear->position.x, -Field.HalfWidth() + 1.f, Field.HalfWidth() - 1.f);
        Commander.pose = FindDeployment(*Rear, CommanderSize, Navigation, Deployed, Terrain, Field).value_or(*Rear);
        auto& Rearmost = ArmyRear[Group.team];
        if (!Rearmost || (FacesEast(Group.team) ? Commander.pose.position.x < Rearmost->position.x
                                         : Commander.pose.position.x > Rearmost->position.x))
            Rearmost = Commander.pose;
        Commander.troop = bMountedCommander ? battle::UnitTroop::Mounted : battle::UnitTroop::Infantry;
        Commander.health = 22.f;
        Commander.speed = bMountedCommander ? 6.4f : 4.3f;
        Commander.weapon.damage = 3.f;
        Scenario.units.push_back(Commander);
        Deployed.push_back({Commander.pose, CommanderSize});
        Scenario.officers.push_back({Group.commander, Commander.id, 1, .8f, .85f, .8f, 15.f});
        auto Profile = CommanderProfile(Group.commander);
        Profile.command_capacity = uint32(Group.children.size());
        Setup.hierarchy.officers.push_back(Profile);
        Setup.hierarchy.nodes.push_back(std::move(Group));
    }

    for (int32 Team = 0; Team < TeamCount; ++Team)
    {
        if (!ArmyRear[Team])
            continue;
        Command::CommandNode Army;
        Army.id = ArmyId(Team, RegimentSlots);
        Army.level = Command::Level::Army;
        Army.team = Team;
        const TCHAR* ArmyName = Team == 0 ? TEXT("Генерал синих") : TEXT("Генерал оранжевых");
        Army.name = TCHAR_TO_UTF8(ArmyName);
        Army.commander = {uint64(RegimentSlots * MissionCommandStaffCount + GroupCount + Team + 1)};
        const battle::MarchRegiment* Lead = nullptr;
        for (const auto& Group : Setup.hierarchy.nodes)
            if (Group.parent == Army.id)
            {
                Army.children.push_back(Group.id);
                if (!Lead || Group.sector_role == Command::SectorRole::Center)
                    Lead = ById[Group.children.front().value - 1];
            }

        battle::UnitSpec General;
        General.id = {RegimentSlots * UnitIdStride + uint64(GroupCount + Team + 1)};
        General.independent_team = Team;
        General.troop = battle::UnitTroop::Mounted;
        General.health = 26.f;
        General.speed = 6.4f;
        General.weapon.damage = 3.f;
        auto Rear = *ArmyRear[Team];
        const float Direction = FacesEast(Team) ? 1.f : -1.f;
        Rear.yaw = FacesEast(Team) ? 0.f : battle::Pi;
        Rear.position.x = -Direction * std::clamp(-Direction * Rear.position.x + 6.f,
            battle::MarchRiverHalfWidth + 2.f, Field.HalfWidth() - 2.f);
        Rear.position.y = 0.f;
        constexpr battle::FormationFootprint GeneralSize{.75f, 1.65f};
        General.pose = FindDeployment(Rear, GeneralSize, Navigation, Deployed, Terrain, Field).value_or(Rear);
        Scenario.units.push_back(General);
        Deployed.push_back({General.pose, GeneralSize});
        Scenario.officers.push_back({Army.commander, General.id, 1, .8f, .9f, .8f, 18.f});
        auto Profile = CommanderProfile(Army.commander);
        Profile.command_capacity = uint32(Army.children.size());
        Profile.command_radius_m = 90.f;
        Setup.hierarchy.officers.push_back(Profile);

        if (bArmyStaff)
            for (int32 Index = 0; Index < ArmyStaffCount; ++Index)
            {
                const bool bBodyguard = Index < ArmyBodyguards;
                const auto Duty = bBodyguard ? Command::SupportDuty::Bodyguard : Command::SupportDuty::Adjutant;
                const int32 DutyIndex = bBodyguard ? Index : Index - ArmyBodyguards;
                const uint64 Offset = uint64(GroupCount + TeamCount + Team * ArmyStaffCount + Index + 1);
                const battle::OfficerId StaffOfficer{RegimentSlots * MissionCommandStaffCount + Offset};
                auto Staff = General;
                Staff.id = {RegimentSlots * UnitIdStride + Offset};
                Staff.name = TCHAR_TO_UTF8(bBodyguard ? TEXT("Телохранитель") : TEXT("Адъютант"));
                Staff.health = bBodyguard ? 18.f : 14.f;
                Staff.speed = bBodyguard ? 7.2f : 8.5f;
                Staff.weapon.damage = bBodyguard ? 2.f : 0.f;
                const auto Station = Command::ArmyStaffStation(General.pose, Duty, size_t(DutyIndex));
                Staff.pose = FindDeployment(Station, GeneralSize, Navigation, Deployed, Terrain, Field).value_or(Station);
                Scenario.units.push_back(Staff);
                Deployed.push_back({Staff.pose, GeneralSize});
                Scenario.officers.push_back({StaffOfficer, Staff.id, Command::ArmyStaffOfficerRole});
                Setup.hierarchy.support.push_back({StaffOfficer, Army.id, Duty});
            }

        if (const auto* Enemy = Lead ? ClosestEnemy(*Lead, ById) : nullptr)
        {
            Command::CommandTask Task;
            Task.id = {uint64(Team + 1)};
            Task.recipient = Army.id;
            Task.action = Command::TaskKind::Attack;
            Task.intent.target_regiment = Enemy->id;
            Task.intent.objective_position = Enemy->pose.position;
            Task.authority.initiative = Command::InitiativePolicy::WithinIntent;
            Task.authority.may_change_target = true;
            Task.authority.may_pursue = true;
            Task.authority.pursuit_limit_m = 30.f;
            Task.status = Command::TaskStatus::Active;
            Setup.missions.push_back(Task);
        }
        Setup.hierarchy.nodes.push_back(std::move(Army));
    }
    return Scenario;
}

battle::Scenario BuildPresetBattle(const FMissionPreset& Preset, std::span<const battle::MarchRegiment> Opening,
    EMissionDeployment Deployment)
{
    const FRegimentEraDefinition* Era = nullptr;
    const FRegimentEraFaction* Factions[TeamCount] = {};
    if (Preset.Era.IsSet())
    {
        Era = RegimentEraCatalog::Find(Preset.Era.Id);
        if (!Era || Era->Version != Preset.Era.Version)
            return {};
        Factions[0] = RegimentEraCatalog::FindFaction(*Era, Preset.Era.BlueFaction);
        Factions[1] = RegimentEraCatalog::FindFaction(*Era, Preset.Era.OrangeFaction);
        if (!Factions[0] || !Factions[1])
            return {};
    }
    else
    {
        for (const auto& Entry : Preset.Roster)
            if (Entry.bEnabled && Entry.Platform != battle::BattlePlatform::None)
                return {};
    }

    const auto InfantryProfiles = MissionInfantryProfiles(Preset.Roster, Factions[0], Factions[1]);
    const bool bDefendingCrest = Preset.Objective.Goal == EMissionGoal::HillDefense;
    const auto BlueLead = std::ranges::find(Opening, 0, &battle::MarchRegiment::team);
    const bool bReverseFacing = bDefendingCrest && BlueLead != Opening.end() && battle::Forward(BlueLead->pose.yaw).x < 0.f;
    auto Scenario = BuildMissionBattle(Preset.Roster, Opening, Preset.Terrain, Deployment,
        Preset.bRegimentTactics, Preset.Field, Preset.FigureTarget,
        Preset.bRegimentTactics && Era && Era->Rules.linear_infantry, IncludesArmyStaff(Preset, Era), InfantryProfiles, bReverseFacing);
    if (Preset.Objective.Goal == EMissionGoal::Eliminate && Scenario.command_setup)
    {
        // This is a terrain checkpoint. Enemy identity and position come from contact reports in battle.
        const float SearchDepth = FMath::Clamp(.20f * Preset.Field.HalfWidth(), 12.f, 24.f);
        for (auto& Task : Scenario.command_setup->missions)
        {
            Task.intent.target_regiment = {};
            Task.intent.objective_position = {Task.recipient == ArmyId(0, MissionRosterExtent(Preset.Roster))
                ? SearchDepth : -SearchDepth, 0.f, 0.f};
            Task.authority.pursuit_limit_m.reset();
        }
    }
    if (IsCrestObjective(Preset.Objective.Goal) && Scenario.hill)
        if (const auto Crest = battle::HillObjectiveCrest(*Scenario.hill))
            Scenario.objective = battle::ScenarioObjective{
                {Crest->center.x, Crest->center.y, 0.f}, FMath::Min(Crest->radius_x_m, Crest->radius_y_m), bDefendingCrest ? 0 : 1};
    if (Preset.Objective.Goal == EMissionGoal::Breakthrough && Preset.Objective.Breakthrough)
    {
        const auto& Zone = *Preset.Objective.Breakthrough;
        Scenario.objective = battle::ScenarioObjective{{Zone.X, Zone.Y, 0.f}, Zone.Radius, 1};
    }
    if (Preset.Objective.Goal == EMissionGoal::Crossing)
    {
        const auto Crossings = battle::MarchCrossingsFor(Preset.Terrain);
        const int32 Index = Preset.Terrain == battle::MarchTerrain::StirlingBridge ? 0 : Preset.Objective.Crossing;
        if (Index >= 0 && size_t(Index) < Crossings.size())
        {
            const auto& Crossing = Crossings[Index];
            const float Bank = (Preset.Objective.CrossingTeam == 0 ? 1.f : -1.f) * (battle::MarchRiverHalfWidth + 2.f);
            Scenario.objective = battle::ScenarioObjective{{Bank, Crossing.y, 0.f},
                Crossing.half_width, 1 - Preset.Objective.CrossingTeam};
        }
    }
    // The breakthrough line is a victory condition behind a contested front. Keep the
    // opening contact assignments so the defenders do not abandon their own center.
    if (Scenario.objective && Scenario.command_setup && Preset.Objective.Goal != EMissionGoal::Breakthrough)
    {
        using namespace battle::command;
        const auto& Objective = *Scenario.objective;
        auto& Setup = *Scenario.command_setup;
        for (auto& Task : Setup.missions)
        {
            const auto Recipient = std::ranges::find(Setup.hierarchy.nodes, Task.recipient, &CommandNode::id);
            if (Recipient == Setup.hierarchy.nodes.end() || Recipient->level != Level::Army)
                continue;
            const bool bDefending = Recipient->team == Objective.defending_team;
            Task.action = bDefending ? TaskKind::Defend : TaskKind::MoveToArea;
            Task.intent.target_regiment = {};
            Task.intent.objective_position = Objective.center;
            if (!bDefending && Preset.Terrain == battle::MarchTerrain::Waterloo)
                Task.intent.objective_position.y += .55f * Objective.radius_m;
            Task.intent.objective_radius_m = Objective.radius_m;
            Task.authority.may_pursue = false;
            Task.authority.pursuit_limit_m.reset();
        }
    }
    if (Era)
    {
        Scenario.rules = Era->Rules;
        Scenario.rules.regiment_tactics = Preset.bRegimentTactics;
        Scenario.rules.mixed_rank_tactics = Preset.bRegimentTactics;
        Scenario.rules.sustained_melee = Preset.bRegimentTactics;
        Scenario.rules.controlled_arrow_aim = true;
        struct FSlotTroops
        {
            const FRegimentEraFaction* Faction{};
            const FRegimentEraTroop* Mounted{};
            const FRegimentEraTroop* Ranged{};
            const FRegimentEraTroop* Platform{};
            const FRegimentEraInfantryProfile* Infantry{};
        };
        std::array<FSlotTroops, MissionRosterCapacity> TroopsBySlot{};
        for (auto& Regiment : Scenario.regiments)
        {
            const int32 Slot = int32(Regiment.id.value - 1);
            const auto Entry = Preset.Roster[Slot].Clamped();
            const auto& Faction = *Factions[Regiment.team];
            auto& Troops = TroopsBySlot[Slot];
            Troops = {&Faction, RegimentEraCatalog::MountedTroop(Faction, Entry.MountedClass),
                RegimentEraCatalog::RangedTroop(Faction, Entry.RangedClass),
                RegimentEraCatalog::PlatformTroop(Faction, Entry.Platform),
                InfantryProfiles[Slot]};
            if (!Troops.Mounted || !Troops.Ranged)
                return {};
            if (!Entry.InfantryProfile.IsEmpty() && !Troops.Infantry)
                return {};
            if (Entry.Platform != battle::BattlePlatform::None && !Troops.Platform)
                return {};
            ERegimentEraRole Role = ERegimentEraRole::Swords;
            if (Entry.bCavalry)
                Role = ERegimentEraRole::Mounted;
            else if (Entry.Spearmen >= Entry.Swords && Entry.Spearmen >= Entry.Archers)
                Role = ERegimentEraRole::Spearmen;
            else if (Entry.Archers > Entry.Swords)
                Role = ERegimentEraRole::Archers;
            const auto& Troop = Troops.Infantry ? Troops.Infantry->Troop : Troops.Platform ? *Troops.Platform
                : Role == ERegimentEraRole::Mounted ? *Troops.Mounted
                : Role == ERegimentEraRole::Archers ? *Troops.Ranged : Faction.Troops[int32(Role)];
            if (Troops.Platform)
                Regiment.speed = Troop.Speed;
            else if (Entry.bCavalry)
                Regiment.speed = Troop.Speed * (5.2f / 6.4f);
            else if (Troops.Infantry)
                Regiment.speed = Troop.Speed;
            Regiment.name = TCHAR_TO_UTF8(*FString::Printf(TEXT("%s · %s %d"),
                *Faction.Name, *Troop.Name, MissionTeamSlot(Slot) + 1));
            for (auto& Node : Scenario.command_setup->hierarchy.nodes)
                if (Node.regiment == Regiment.id)
                    Node.name = Regiment.name;
        }
        for (auto& Node : Scenario.command_setup->hierarchy.nodes)
        {
            if (Node.level == battle::command::Level::Army)
                Node.name = TCHAR_TO_UTF8(*FString::Printf(TEXT("Командующий · %s"), *Factions[Node.team]->Name));
        }

        TSet<uint64> CommandFigures;
        for (const auto& Officer : Scenario.officers)
            CommandFigures.Add(Officer.unit.value);
        for (auto& Unit : Scenario.units)
        {
            if (!Unit.regiment)
                continue;
            const int32 Slot = int32(Unit.regiment.value - 1);
            const auto& Troops = TroopsBySlot[Slot];
            const bool bStaff = CommandFigures.Contains(Unit.id.value);
            const bool bPlatform = Troops.Platform && !bStaff;
            const auto* Infantry = bStaff ? nullptr : Troops.Infantry;
            if (bPlatform)
            {
                Unit.platform = Preset.Roster[Slot].Platform;
                const bool bEngine = battle::IsSiegeEngine(Unit.platform);
                Unit.troop = bEngine ? battle::UnitTroop::Infantry : battle::UnitTroop::Mounted;
                Unit.archer = bEngine || Troops.Platform->RangedWeapon != battle::RangedWeapon::None;
            }
            if (Infantry)
            {
                Unit.archer = Infantry->Role == ERegimentEraRole::Archers;
                Unit.troop = Infantry->Role == ERegimentEraRole::Spearmen
                    ? battle::UnitTroop::Spearman : battle::UnitTroop::Infantry;
            }
            const auto Role = Unit.troop == battle::UnitTroop::Spearman ? ERegimentEraRole::Spearmen : ERegimentEraRole::Swords;
            const auto& Troop = Infantry ? Infantry->Troop : bPlatform ? *Troops.Platform : Unit.troop == battle::UnitTroop::Mounted ? *Troops.Mounted
                : Unit.archer ? *Troops.Ranged : Troops.Faction->Troops[int32(Role)];
            Unit.cavalry = Troop.Cavalry;
            if (bStaff)
                continue;
            Unit.ranged_weapon = Troop.RangedWeapon;
            Unit.ammunition = Troop.Ammunition;
            Unit.name = TCHAR_TO_UTF8(*Troop.Name);
            Unit.health = Troop.Health;
            Unit.speed = Troop.Speed;
            Unit.armour = Troop.Armour;
            Unit.weapon = Troop.Weapon;
            if (Infantry && Infantry->Pilum)
            {
                Unit.ranged_weapon = Infantry->Pilum->RangedWeapon;
                Unit.ammunition = Infantry->Pilum->Ammunition;
                Unit.ranged_attack = Infantry->Pilum->Weapon;
            }
            if (Troop.MountedJavelin)
            {
                Unit.ranged_weapon = battle::RangedWeapon::Javelin;
                Unit.ammunition = Troop.MountedJavelin->Stock;
                Unit.ranged_attack = Troop.MountedJavelin->Weapon;
            }
        }
    }
    if (Preset.bSmokeExercise && !Era)
    {
        for (auto& Unit : Scenario.units)
            if (Unit.archer)
            {
                Unit.ranged_weapon = battle::RangedWeapon::Gunpowder;
                Unit.ammunition = 6;
                Unit.weapon.range = 30.f;
                Unit.weapon.cooldown_ticks = 36;
            }
        for (auto& Regiment : Scenario.regiments)
            if (Preset.Roster[Regiment.id.value - 1].Archers > 0)
            {
                Regiment.name = TCHAR_TO_UTF8(*NSLOCTEXT("RegimentMission", "SmokeShooters", "Пороховые стрелки").ToString());
                for (auto& Node : Scenario.command_setup->hierarchy.nodes)
                    if (Node.regiment == Regiment.id)
                        Node.name = Regiment.name;
            }
    }
    if (Preset.Tutorial == ERegimentTutorial::None && !Preset.bSmokeExercise)
        for (auto& Regiment : Scenario.regiments)
        {
            const auto Entry = Preset.Roster[Regiment.id.value - 1].Clamped();
            if (Entry.Platform != battle::BattlePlatform::None)
                continue;
            if (Entry.bCavalry)
            {
                Regiment.formation.slot_stagger = .88f;
                continue;
            }
            const auto* Faction = Factions[Regiment.team];
            const bool bSpearMajority = Entry.Spearmen >= FMath::Max(Entry.Swords, Entry.Archers);
            const bool bSwordMajority = Entry.Swords > Entry.Spearmen && Entry.Swords >= Entry.Archers;
            const bool bAncientDrill = Era && Era->Id == TEXT("ancient") &&
                (Entry.InfantryProfile == TEXT("legionaries") || Entry.InfantryProfile == TEXT("hoplites") ||
                    Entry.InfantryProfile == TEXT("sarissa_phalanx") ||
                    (Entry.InfantryProfile.IsEmpty() && Faction &&
                        ((bSwordMajority && Faction->Id == TEXT("rome")) ||
                            (bSpearMajority && (Faction->Id == TEXT("greece") || Faction->Id == TEXT("sparta") ||
                                Faction->Id == TEXT("macedon"))))));
            const bool bLinearDrill = Era && (Era->Rules.linear_infantry || Era->Id == TEXT("great_northern_war"));
            Regiment.formation.slot_stagger = bAncientDrill || bLinearDrill ? 0.f : .37f;
        }
    Scenario.rules.risky_volley = Preset.bRiskyVolley;
    Scenario.rules.adaptive_battle = Preset.bAdaptiveBattle;
    RegimentCommanderProfiles::Apply(Scenario, Preset);
    RegimentMissionTutorial::PrepareScenario(Preset.Tutorial, Scenario);
    return Scenario;
}
