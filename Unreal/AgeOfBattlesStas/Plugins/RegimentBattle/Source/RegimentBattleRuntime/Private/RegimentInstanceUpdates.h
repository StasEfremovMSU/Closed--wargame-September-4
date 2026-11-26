#pragma once

#include "Containers/ArrayView.h"
#include "Math/Transform.h"

struct FRegimentInstanceUpdateRange
{
    int32 First;
    int32 Count;
};

inline FRegimentInstanceUpdateRange RegimentChangedInstances(
    TConstArrayView<FTransform> Previous, TConstArrayView<FTransform> Next)
{
    int32 First = 0;
    int32 End = FMath::Min(Previous.Num(), Next.Num());
    while (First < End && Previous[First].Equals(Next[First], 0.0))
        ++First;
    while (First < End && Previous[End - 1].Equals(Next[End - 1], 0.0))
        --End;
    return {First, End - First};
}
