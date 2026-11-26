#include "SRegimentCommanderAvatar.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SRegimentCommanderAvatar::Construct(const FArguments& Args)
{
    static const FSlateRoundedBoxBrush Background(FLinearColor(FColor::FromHex(TEXT("17242D"))), 4.f);
    const auto Portrait = Args._Portrait;
    ChildSlot
    [
        SNew(SBox).WidthOverride(Args._Size).HeightOverride(Args._Size)
        [
            SNew(SBorder).BorderImage(&Background).Padding(1.f)
            [
                SNew(SOverlay)
                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(Args._Rank)
                    .Visibility_Lambda([Portrait]
                    {
                        return Portrait.Get().IsValid() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                    })
                    .ColorAndOpacity(FLinearColor(FColor::FromHex(TEXT("F0C98E"))))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", FMath::RoundToInt(Args._Size * .49f)))
                ]
                + SOverlay::Slot()
                [
                    SNew(SImage)
                    .Visibility_Lambda([Portrait]
                    {
                        return Portrait.Get().IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    .Image_Lambda([this, Portrait]
                    {
                        if (auto* Texture = Portrait.Get().Get(); PortraitBrushes[ActiveBrush].GetResourceObject() != Texture)
                        {
                            // Return a different brush when the texture changes so Slate invalidates the image.
                            ActiveBrush = 1 - ActiveBrush;
                            PortraitBrushes[ActiveBrush].SetResourceObject(Texture);
                        }
                        return &PortraitBrushes[ActiveBrush];
                    })
                ]
            ]
        ]
    ];
}
