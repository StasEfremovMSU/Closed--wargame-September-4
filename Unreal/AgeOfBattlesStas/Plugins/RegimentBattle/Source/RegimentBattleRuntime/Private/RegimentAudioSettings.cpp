#include "RegimentAudioSettings.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundWave.h"
#include "Styling/CoreStyle.h"
#include "UObject/SoftObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RegimentAudioSettings"

namespace RegimentAudioSettings
{
namespace Private
{
constexpr const TCHAR* Section = TEXT("RegimentAudio");
constexpr const TCHAR* VolumeKeys[] = {TEXT("Combat"), TEXT("Commands"), TEXT("Ambience"), TEXT("Interface")};
constexpr int32 BusCount = int32(EBus::Count);
static_assert(UE_ARRAY_COUNT(VolumeKeys) == BusCount);

constexpr float MenuGain = .55f;
const FLinearColor Ink(FColor::FromHex(TEXT("F7F3EB")));
const FLinearColor Muted(FColor::FromHex(TEXT("BDC8D0")));
const FLinearColor Brass(FColor::FromHex(TEXT("F0C98E")));
const FSlateRoundedBoxBrush MenuPanel(FLinearColor(FColor::FromHex(TEXT("121B24"))), 8.f,
    FLinearColor(FColor::FromHex(TEXT("475562"))), 1.f);

class SVolumeMenuBox final : public SBox
{
public:
    bool SupportsKeyboardFocus() const override { return true; }
};

FVolumeSettings& Settings()
{
    static FVolumeSettings Values = []
    {
        FVolumeSettings Loaded;
        if (GConfig && !GGameUserSettingsIni.IsEmpty())
            Loaded.Load(*GConfig, GGameUserSettingsIni);
        return Loaded;
    }();
    return Values;
}

void SaveSettings()
{
    if (GConfig && !GGameUserSettingsIni.IsEmpty())
        Settings().Save(*GConfig, GGameUserSettingsIni);
}

TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UAudioComponent>>& MenuVoices()
{
    static TMap<TWeakObjectPtr<UWorld>, TWeakObjectPtr<UAudioComponent>> Voices;
    return Voices;
}

USoundWave* MenuWave(EMenuSound Cue)
{
    using FWaveRef = TSoftObjectPtr<USoundWave>;
    static const FWaveRef Waves[] = {
        FWaveRef(FSoftObjectPath(TEXT("/Game/Battle/Audio/UI/Menu_Click.Menu_Click"))),
        FWaveRef(FSoftObjectPath(TEXT("/Game/Battle/Audio/UI/Menu_Confirm.Menu_Confirm"))),
        FWaveRef(FSoftObjectPath(TEXT("/Game/Battle/Audio/UI/Menu_Back.Menu_Back"))),
        FWaveRef(FSoftObjectPath(TEXT("/Game/Battle/Audio/UI/Menu_Error.Menu_Error")))
    };
    static_assert(UE_ARRAY_COUNT(Waves) == int32(EMenuSound::Count));
    const int32 Index = int32(Cue);
    return Index < UE_ARRAY_COUNT(Waves) ? Waves[Index].LoadSynchronous() : nullptr;
}

TSharedRef<SWidget> MakeVolumeRow(EBus Bus, FText Label, TWeakObjectPtr<UWorld> World)
{
    const auto Dragging = MakeShared<bool>(false);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)
            [SNew(STextBlock).Text(Label).ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))]
            + SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right)
            [
                SNew(STextBlock)
                .Text_Lambda([Bus]
                {
                    static const FNumberFormattingOptions Percent = FNumberFormattingOptions()
                        .SetUseGrouping(false).SetMaximumFractionalDigits(0);
                    return FText::AsPercent(GetVolume(Bus), &Percent);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                .ColorAndOpacity(Brass)
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
        [
            SNew(SSlider)
            .ToolTipText(Label)
            .AccessibleText(Label)
            .RequiresControllerLock(false)
            .MouseUsesStep(true)
            .StepSize(.01f)
            .SliderBarColor(Muted)
            .SliderHandleColor(Brass)
            .Value_Lambda([Bus] { return GetVolume(Bus); })
            .OnMouseCaptureBegin_Lambda([Dragging] { *Dragging = true; })
            .OnMouseCaptureEnd_Lambda([Bus, Dragging, World]
            {
                *Dragging = false;
                SaveSettings();
                if (Bus == EBus::Interface)
                    PlayMenuSound(World, EMenuSound::Click);
            })
            .OnValueChanged_Lambda([Bus, Dragging, World](float Volume)
            {
                if (!Settings().SetVolume(Bus, Volume))
                    return;
                if (Bus == EBus::Interface)
                {
                    const auto* Voice = MenuVoices().Find(World);
                    if (auto* Component = Voice ? Voice->Get() : nullptr)
                        Component->SetVolumeMultiplier(MenuGain * GetVolume(Bus));
                }
                if (!*Dragging)
                    SaveSettings();
            })
        ];
}

TSharedRef<SWidget> MakeVolumeMenu(TWeakObjectPtr<UWorld> World, const FButtonStyle* Style, FLinearColor Tint)
{
    return SNew(SVolumeMenuBox).WidthOverride(280.f).Padding(FMargin(12, 10))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
            [
                SNew(STextBlock).Text(LOCTEXT("SoundHeading", "Звук"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 17)).ColorAndOpacity(Brass)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [MakeVolumeRow(EBus::Combat, LOCTEXT("Combat", "Бой"), World)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [MakeVolumeRow(EBus::Commands, LOCTEXT("Commands", "Команды"), World)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [MakeVolumeRow(EBus::Ambience, LOCTEXT("Ambience", "Окружение"), World)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [MakeVolumeRow(EBus::Interface, LOCTEXT("Interface", "Интерфейс"), World)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
            [
                SNew(SButton)
                .ButtonStyle(Style)
                .ButtonColorAndOpacity(Tint)
                .ContentPadding(FMargin(9, 5))
                .OnClicked_Lambda([World]
                {
                    Settings().Reset();
                    SaveSettings();
                    PlayMenuSound(World, EMenuSound::Confirm);
                    return FReply::Handled();
                })
                [
                    SNew(STextBlock).Text(LOCTEXT("ResetVolume", "Сбросить громкость"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14)).ColorAndOpacity(Ink)
                ]
            ]
        ];
}
}

FVolumeSettings::FVolumeSettings()
{
    Reset();
}

float FVolumeSettings::GetVolume(EBus Bus) const
{
    const int32 Index = int32(Bus);
    return Index < Private::BusCount ? Volumes[Index] : 1.f;
}

bool FVolumeSettings::SetVolume(EBus Bus, float Volume)
{
    const int32 Index = int32(Bus);
    if (Index >= Private::BusCount || !FMath::IsFinite(Volume))
        return false;
    const float Clamped = FMath::Clamp(Volume, 0.f, 1.f);
    if (Volumes[Index] == Clamped)
        return false;
    Volumes[Index] = Clamped;
    return true;
}

void FVolumeSettings::Reset()
{
    for (float& Volume : Volumes)
        Volume = 1.f;
}

void FVolumeSettings::Load(FConfigCacheIni& Config, const FString& Filename)
{
    Reset();
    for (int32 Index = 0; Index < Private::BusCount; ++Index)
    {
        FString Value;
        float Volume;
        if (Config.GetString(Private::Section, Private::VolumeKeys[Index], Value, Filename) && LexTryParseString(Volume, *Value))
            SetVolume(EBus(Index), Volume);
    }
}

void FVolumeSettings::Save(FConfigCacheIni& Config, const FString& Filename) const
{
    for (int32 Index = 0; Index < Private::BusCount; ++Index)
        Config.SetFloat(Private::Section, Private::VolumeKeys[Index], Volumes[Index], Filename);
    Config.Flush(false, Filename);
}

float GetVolume(EBus Bus)
{
    return Private::Settings().GetVolume(Bus);
}

void PlayMenuSound(TWeakObjectPtr<UWorld> World, EMenuSound Cue)
{
    const float Volume = Private::MenuGain * GetVolume(EBus::Interface);
    auto* Context = World.Get();
    if (!Context || Volume <= 0.f)
        return;
    auto* Wave = Private::MenuWave(Cue);
    if (!Wave)
        return;

    auto& Voices = Private::MenuVoices();
    for (auto It = Voices.CreateIterator(); It; ++It)
        if (!It.Key().IsValid())
            It.RemoveCurrent();
    auto& Voice = Voices.FindOrAdd(World);
    if (auto* Previous = Voice.Get())
        Previous->Stop();
    Voice = UGameplayStatics::SpawnSound2D(Context, Wave, Volume);
}

TSharedRef<SWidget> MakeSettingsButton(TWeakObjectPtr<UWorld> World, const FButtonStyle* Style,
    FLinearColor Tint, bool bOpenAbove)
{
    static const FComboButtonStyle ComboStyle = FComboButtonStyle(FCoreStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton"))
        .SetMenuBorderBrush(Private::MenuPanel).SetMenuBorderPadding(FMargin(0));
    return SNew(SComboButton)
        .ComboButtonStyle(&ComboStyle)
        .ButtonStyle(Style)
        .ButtonColorAndOpacity(Tint)
        .ForegroundColor(FSlateColor(Private::Ink))
        .ContentPadding(FMargin(9, 5))
        .MenuPlacement(bOpenAbove ? MenuPlacement_AboveAnchor : MenuPlacement_ComboBox)
        .ToolTipText(LOCTEXT("AudioSettingsHelp", "Настроить громкость звука"))
        .OnMenuOpenChanged_Lambda([World](bool bOpen)
        {
            if (bOpen)
                PlayMenuSound(World, EMenuSound::Click);
        })
        .OnGetMenuContent_Lambda([World, Style, Tint] { return Private::MakeVolumeMenu(World, Style, Tint); })
        .ButtonContent()
        [
            SNew(STextBlock).Text(LOCTEXT("SettingsButton", "Настройки"))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14)).ColorAndOpacity(Private::Ink)
        ];
}
}

#undef LOCTEXT_NAMESPACE
