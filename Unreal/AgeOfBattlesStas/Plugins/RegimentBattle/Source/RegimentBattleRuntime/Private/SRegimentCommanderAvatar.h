#pragma once

#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;

class SRegimentCommanderAvatar final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentCommanderAvatar) : _Size(48.f) {}
        SLATE_ATTRIBUTE(TWeakObjectPtr<UTexture2D>, Portrait)
        SLATE_ATTRIBUTE(FText, Rank)
        SLATE_ARGUMENT(float, Size)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);

private:
    FSlateBrush PortraitBrush;
};
