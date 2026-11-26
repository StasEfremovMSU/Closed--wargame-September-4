#pragma once

#include "CoreMinimal.h"

class FConfigCacheIni;
class SWidget;
class UWorld;
struct FButtonStyle;

namespace RegimentAudioSettings
{
enum class EBus : uint8 { Combat, Commands, Ambience, Interface, Count };
enum class EMenuSound : uint8 { Click, Confirm, Back, Error, Count };

class FVolumeSettings
{
public:
    FVolumeSettings();

    float GetVolume(EBus Bus) const;
    bool SetVolume(EBus Bus, float Volume);
    void Reset();
    void Load(FConfigCacheIni& Config, const FString& Filename);
    void Save(FConfigCacheIni& Config, const FString& Filename) const;

private:
    float Volumes[int32(EBus::Count)];
};

float GetVolume(EBus Bus);
void PlayMenuSound(TWeakObjectPtr<UWorld> World, EMenuSound Cue);
TSharedRef<SWidget> MakeSettingsButton(TWeakObjectPtr<UWorld> World, const FButtonStyle* Style,
    FLinearColor Tint, bool bOpenAbove = false);
}
