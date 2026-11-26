#pragma once

#include "RegimentBattleField.h"
#include "Widgets/SLeafWidget.h"

class SRegimentFieldOverlay final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentFieldOverlay) {}
        SLATE_ARGUMENT(AActor*, Owner)
        SLATE_ARGUMENT(TFunction<TConstArrayView<FRegimentFieldLine>()>, Lines)
        SLATE_ARGUMENT(TFunction<TConstArrayView<FRegimentFieldCard>()>, Cards)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);

private:
    int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32,
        const FWidgetStyle&, bool) const override;
    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

    TWeakObjectPtr<AActor> Owner;
    TFunction<TConstArrayView<FRegimentFieldLine>()> Lines;
    TFunction<TConstArrayView<FRegimentFieldCard>()> Cards;
};
