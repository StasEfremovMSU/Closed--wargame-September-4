#include "RegimentMissionAuthoring.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Formation.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "RegimentMissionAuthoring"

namespace
{
FString SlotPath(int32 Slot)
{
    return FString::Printf(TEXT("regiments[%d]"), Slot);
}

void AddDiagnostic(FMissionAuthoringPreview& Preview, EMissionAuthoringIssue Kind, int32 Slot,
    const FText& Message, int32 Other = INDEX_NONE)
{
    Preview.Diagnostics.Add({Kind, Slot, Other, FText::Format(LOCTEXT("AtSlot", "{0} · {1}"),
        FText::AsCultureInvariant(SlotPath(Slot)), Message)});
}

FText PositionText(battle::Vec3 Position)
{
    return FText::AsCultureInvariant(FString::Printf(TEXT("(%.1f, %.1f) м"), Position.x, Position.y));
}

void FindDeploymentDiagnostics(FMissionAuthoringPreview& Preview, const FMissionRoster& Roster)
{
    for (int32 Index = 0; Index < Preview.Spawns.Num(); ++Index)
    {
        const auto& Spawn = Preview.Spawns[Index];
        const auto& Entry = Roster[Spawn.Slot];
        const auto& Position = Spawn.Requested.position;
        const auto Limits = battle::TerrainFieldInset({Preview.Field.HalfWidth(), Preview.Field.HalfHeight()},
            Spawn.Requested.yaw, Spawn.Size.depth_m / 2.f, Spawn.Size.width_m / 2.f);
        if (FMath::Abs(Position.x) > Limits.x || FMath::Abs(Position.y) > Limits.y)
            AddDiagnostic(Preview, EMissionAuthoringIssue::OutOfBounds, Spawn.Slot, FText::Format(
                LOCTEXT("FootprintOutside", "Строй в точке {0} выходит за край поля."), PositionText(Position)));

        for (int32 AreaIndex = 0; AreaIndex < Preview.Terrain.Num(); ++AreaIndex)
        {
            const auto& Area = Preview.Terrain[AreaIndex];
            if (battle::TerrainBlocksMovement(Area) && battle::TerrainAreaOverlaps(
                Area, Spawn.Requested, Spawn.Size.depth_m / 2.f, Spawn.Size.width_m / 2.f))
            {
                AddDiagnostic(Preview, EMissionAuthoringIssue::Obstructed, Spawn.Slot, FText::Format(
                    LOCTEXT("FootprintHitsTerrain", "Строй в точке {0} пересекает препятствие №{1}."),
                    PositionText(Position), AreaIndex + 1));
            }
        }
        for (int32 OtherIndex = 0; OtherIndex < Index; ++OtherIndex)
        {
            const auto& Other = Preview.Spawns[OtherIndex];
            if (battle::RectanglesOverlap(Spawn.Requested, Spawn.Size, Other.Requested, Other.Size))
            {
                AddDiagnostic(Preview, EMissionAuthoringIssue::Overlap, Spawn.Slot, FText::Format(
                    LOCTEXT("FootprintsOverlap", "Старт {0} пересекается со строем {1}."),
                    PositionText(Position), FText::AsCultureInvariant(SlotPath(Other.Slot))), Other.Slot);
            }
        }
        if (Spawn.Actual.position != Position || Spawn.Actual.yaw != Spawn.Requested.yaw)
        {
            AddDiagnostic(Preview, EMissionAuthoringIssue::Relocated, Spawn.Slot, FText::Format(
                LOCTEXT("Relocated", "Запрошенный старт {0}, старт в бою {1}, разворот {2}°."),
                PositionText(Position), PositionText(Spawn.Actual.position),
                FText::AsNumber(FMath::RoundToInt(FMath::RadiansToDegrees(Spawn.Actual.yaw)))));
        }
        if (Entry.Total() == MissionCommandStaffCount)
            AddDiagnostic(Preview, EMissionAuthoringIssue::EmptyFormation, Spawn.Slot,
                LOCTEXT("EmptyFormation", "Полк состоит только из командного состава."));
    }
}
}

TOptional<FMissionAuthoringPreview> BuildMissionAuthoringPreview(const FMissionPreset& Preset, FText& OutError)
{
    if (!RegimentMissionPresets::Validate(Preset, OutError))
        return {};

    FMissionAuthoringPreview Preview;
    Preview.Field = Preset.Field;
    Preview.bTactical = Preset.StartsInBattle();
    const auto Opening = MakeMissionOpening(Preset);
    if (Preview.bTactical)
    {
        auto Created = battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
        if (!Created)
        {
            OutError = LOCTEXT("WorldRejected", "Симуляция отклонила расстановку. Проверьте состав полков и проходимость стартов.");
            return {};
        }
        const auto& World = Created.value;
        for (const auto& Area : World.scenario.obstacles)
            Preview.Terrain.Add(Area);
        Preview.Hill = World.scenario.hill;
        Preview.Objective = World.scenario.objective;
        if (Preset.Objective.Goal == EMissionGoal::HillHold && Preview.Hill)
            Preview.Crest = battle::HillObjectiveCrest(*Preview.Hill);
        for (const auto& Company : Opening)
        {
            const auto* Regiment = battle::FindRegiment(World, Company.id);
            if (!Regiment)
                continue;
            const int32 Slot = int32(Company.id.value - 1);
            const auto& Entry = Preset.Roster[Slot];
            const auto& Name = std::ranges::find(World.scenario.regiments, Company.id, &battle::RegimentSpec::id)->name;
            Preview.Spawns.Add({Slot, MissionGroup(Slot, Entry), UTF8_TO_TCHAR(Name.c_str()),
                Company.pose, Regiment->pose, battle::Footprint(*Regiment)});
        }
    }
    else
    {
        for (const auto& Area : battle::MarchTerrainAreas(Preset.Terrain, Preset.Field))
            Preview.Terrain.Add(Area);
        Preview.Hill = battle::MarchHill(Preset.Terrain, Preset.Field);
        for (const auto& Company : Opening)
        {
            const int32 Slot = int32(Company.id.value - 1);
            const auto& Entry = Preset.Roster[Slot];
            const auto Formation = Company.ActiveFormation();
            const uint32 Files = FMath::Min(Formation.columns, Company.figure_count);
            const uint32 Ranks = FMath::DivideAndRoundUp(Company.figure_count, Files);
            const float Padding = 2.f * Formation.body_radius_m;
            Preview.Spawns.Add({Slot, MissionGroup(Slot, Entry), FString(), Company.pose, Company.pose,
                {(Files - 1) * Formation.spacing_m + Padding, (Ranks - 1) * Formation.spacing_m + Padding}});
        }
    }
    FindDeploymentDiagnostics(Preview, Preset.Roster);
    return Preview;
}

#undef LOCTEXT_NAMESPACE
