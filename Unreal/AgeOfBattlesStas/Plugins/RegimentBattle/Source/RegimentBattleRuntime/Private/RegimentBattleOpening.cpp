#include "RegimentBattleOpening.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Formation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
battle::Pose Between(battle::Pose From, battle::Pose To, float Progress)
{
    const float Ease = Progress * Progress * (3.f - 2.f * Progress);
    const auto Delta = To.position - From.position;
    float Yaw = From.yaw;
    if (battle::Length(Delta) > 0.001f)
    {
        const float Turn = std::remainder(FMath::Atan2(Delta.y, Delta.x) - From.yaw, 2.f * battle::Pi);
        Yaw += Turn * FMath::Square(FMath::Sin(battle::Pi * Progress));
    }
    return { From.position + Delta * Ease, Yaw };
}
}

FRegimentBattleOpening::FRegimentBattleOpening(const battle::World& World)
{
    Paths.SetNum(static_cast<int32>(World.units.size()));
    for (const auto& Regiment : World.regiments)
    {
        const uint32 Columns = Regiment.formation.columns;
        if (Columns < 2 || Columns % 2 != 0 || Regiment.units.size() % Columns != 0)
        {
            Paths.Reset();
            return;
        }

        auto Narrow = Regiment.formation;
        Narrow.columns /= 2;
        const auto Slots = battle::BuildFormationSlots(Narrow, Regiment.pose, static_cast<uint32>(Regiment.units.size()));
        const auto Forward = battle::Forward(Regiment.pose.yaw);
        for (const auto Id : Regiment.units)
        {
            const auto* Unit = battle::FindUnit(World, Id);
            const uint32 Row = Unit->slot_index / Columns;
            const uint32 Lane = Unit->slot_index % Columns;
            // Alternate adjacent lanes into separate ranks before moving them sideways.
            const uint32 NarrowIndex = (2 * Row + Lane % 2) * Narrow.columns + Lane / 2;
            const auto Line = Unit->pose;
            const auto Column = Slots[NarrowIndex];
            const auto Staggered = Line.position + Forward * battle::Dot(Column.position - Line.position, Forward);
            Paths[Unit - World.units.data()] = { Line, { Staggered, Line.yaw }, Column };
        }
    }
}

void FRegimentBattleOpening::Advance(battle::World& World)
{
    if (!IsActive())
        return;
    ++ElapsedTicks;

    for (int32 Index = 0; Index < Paths.Num(); ++Index)
    {
        const auto& Path = Paths[Index];
        auto& Pose = World.units[Index].pose;
        if (ElapsedTicks < MoveTicks)
            Pose = Between(Path.Line, Path.Staggered, float(ElapsedTicks) / MoveTicks);
        else if (ElapsedTicks < ColumnTick)
            Pose = Between(Path.Staggered, Path.Column, float(ElapsedTicks - MoveTicks) / MoveTicks);
        else if (ElapsedTicks <= ColumnTick + ColumnHoldTicks)
            Pose = Path.Column;
        else if (ElapsedTicks < DurationTicks - MoveTicks)
            Pose = Between(Path.Column, Path.Staggered, float(ElapsedTicks - ColumnTick - ColumnHoldTicks) / MoveTicks);
        else if (ElapsedTicks < DurationTicks)
            Pose = Between(Path.Staggered, Path.Line, float(ElapsedTicks - DurationTicks + MoveTicks) / MoveTicks);
        else
            Pose = Path.Line;
    }
}
