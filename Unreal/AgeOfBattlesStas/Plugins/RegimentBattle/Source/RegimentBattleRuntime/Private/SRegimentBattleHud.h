#pragma once

#include "Widgets/SCompoundWidget.h"

class ARegimentBattleField;

class SRegimentBattleHud final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentBattleHud) {}
        SLATE_ARGUMENT(ARegimentBattleField*, Field)
        SLATE_ATTRIBUTE(bool, ArtillerySelected)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
