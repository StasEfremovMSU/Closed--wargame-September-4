#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"

inline constexpr int32 RichFigureLimit = 160;

inline double RegimentRichFigureDistance(int32 FigureCount)
{
    return FigureCount <= RichFigureLimit ? 19200.0 : 7350.0;
}

struct FRegimentLodCandidate
{
    uint64 Id;
    double DistanceSquared;
    bool bAlreadyRich;
    bool bControlled;
};

inline void SelectRegimentRichFigures(TArray<FRegimentLodCandidate>& Candidates, int32 Limit, int32 Promotions,
    TSet<uint64>& Selected)
{
    Selected.Reset();
    Candidates.Sort([](const FRegimentLodCandidate& Left, const FRegimentLodCandidate& Right)
    {
        if (Left.bControlled != Right.bControlled)
            return Left.bControlled;
        const double LeftRank = Left.DistanceSquared * (Left.bAlreadyRich ? .82 : 1.0);
        const double RightRank = Right.DistanceSquared * (Right.bAlreadyRich ? .82 : 1.0);
        return LeftRank == RightRank ? Left.Id < Right.Id : LeftRank < RightRank;
    });

    for (const auto& Candidate : Candidates)
    {
        if (Selected.Num() == Limit)
            break;
        if (!Candidate.bAlreadyRich)
        {
            if (Promotions == 0)
                continue;
            --Promotions;
        }
        Selected.Add(Candidate.Id);
    }
}
