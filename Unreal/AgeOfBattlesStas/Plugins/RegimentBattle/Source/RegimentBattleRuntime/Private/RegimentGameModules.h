#pragma once

#include "Containers/StringView.h"
#include "RegimentMissionPreset.h"

struct FRegimentGameModule
{
    FString Id;
    FText Name;
    bool bAvailable = false;
    int32 FactionCount = 0;
};

namespace RegimentGameModules
{
TArray<FRegimentGameModule> List();
FText Name(FStringView Id);
bool Open(FStringView Id, FMissionPreset& Out);
TArray<FMissionPresetListItem> Presets(FStringView Id, const FString& Directory);
TArray<FMissionPresetListItem> PlayablePresets(FStringView Id, const FString& Directory);
bool OpenPlayable(FStringView Id, FMissionPreset& Out);
bool LoadPlayable(FStringView Id, const FString& Directory, const FMissionPresetListItem& Item,
    FMissionPreset& Out, FText& Error);
bool Load(FStringView Id, const FString& Directory, const FMissionPresetListItem& Item,
    FMissionPreset& Out, FText& Error);
}
