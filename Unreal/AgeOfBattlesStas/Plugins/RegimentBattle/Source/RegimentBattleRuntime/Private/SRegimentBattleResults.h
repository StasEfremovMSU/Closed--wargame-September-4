#pragma once

#include "CoreMinimal.h"

class SWidget;
struct FRegimentBattleReport;
struct FRegimentPlayerStats;

TSharedRef<SWidget> MakeRegimentBattleDebrief(const FRegimentBattleReport& Report,
    FSimpleDelegate OnRestart, FSimpleDelegate OnLeave);
TSharedRef<SWidget> MakeRegimentPlayerStatistics(const FRegimentPlayerStats& Stats,
    bool bSavePending, FSimpleDelegate OnBack);
