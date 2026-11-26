#pragma once

#include "Widgets/SCompoundWidget.h"

class ARegimentBattleField;
class SConstraintCanvas;
class SScrollBox;
struct FRegimentDecisionHudEntry;

class SRegimentOfficerPicker final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentOfficerPicker) {}
        SLATE_ARGUMENT(TWeakObjectPtr<ARegimentBattleField>, Field)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry&, double, float) override;

private:
    TSharedRef<SWidget> Choice(const FRegimentDecisionHudEntry&, bool bCompact);
    void Rebuild(ARegimentBattleField&);
    TWeakObjectPtr<ARegimentBattleField> Field;
    TSharedPtr<SConstraintCanvas> Bubbles;
    TSharedPtr<SScrollBox> Roster;
    TArray<TSharedPtr<FSlateBrush>> Brushes;
    TMap<uint64, FVector2D> Positions;
    uint32 Revision = 0;
};
