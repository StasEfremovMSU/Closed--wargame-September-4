#pragma once

#include "Widgets/SCompoundWidget.h"

class ARegimentBattleField;
struct FRegimentDecisionHudEntry;

class SRegimentCommandPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentCommandPanel) {}
        SLATE_ARGUMENT(ARegimentBattleField*, Field)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;

private:
    TSharedRef<SWidget> MakeBranch(const FRegimentDecisionHudEntry& Entry, int32 Depth);
    const FRegimentDecisionHudEntry* Decision(uint64 Node) const;

    TWeakObjectPtr<ARegimentBattleField> Field;
    TMap<uint64, FVector2D> VisibleHints;
    uint64 FocusedNode = 0;
};
