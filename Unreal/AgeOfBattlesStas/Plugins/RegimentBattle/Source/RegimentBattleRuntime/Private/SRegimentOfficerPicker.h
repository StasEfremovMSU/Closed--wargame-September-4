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
    const FRegimentDecisionHudEntry* SelectedEntry() const;
    void Rebuild(ARegimentBattleField&);
    TWeakObjectPtr<ARegimentBattleField> Field;
    TSharedPtr<SConstraintCanvas> Bubbles;
    TSharedPtr<SScrollBox> Roster;
    TMap<uint64, FVector2D> Positions;
    uint64 SelectedNode = 0;
    uint32 Revision = 0;
};
