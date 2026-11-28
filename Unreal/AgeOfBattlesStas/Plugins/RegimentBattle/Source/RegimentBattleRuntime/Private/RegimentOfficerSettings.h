#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

enum class ERegimentOfficerInputAction : uint8
{
    Forward, Backward, Left, Right, TurnLeft, TurnRight, Run, Jump, Attack, Parry, Camera, Count
};

class FRegimentOfficerSettings
{
public:
    static constexpr float MinMouse = .25f;
    static constexpr float MaxMouse = 2.5f;
    static constexpr int32 ActionCount = int32(ERegimentOfficerInputAction::Count);

    FRegimentOfficerSettings();
    const FKey& Key(ERegimentOfficerInputAction Action) const { return Keys[int32(Action)]; }
    bool Bind(ERegimentOfficerInputAction Action, const FKey& Key);
    float Mouse() const { return MouseMultiplier; }
    void SetMouse(float Multiplier);
    bool IsFirstPerson() const { return bFirstPerson; }
    void SetFirstPerson(bool bEnabled) { bFirstPerson = bEnabled; }
    bool StationaryThreatFacing() const { return bStationaryThreatFacing; }
    void SetStationaryThreatFacing(bool bEnabled) { bStationaryThreatFacing = bEnabled; }
    static FText ActionLabel(ERegimentOfficerInputAction Action);
    void Load(const FString& Filename);
    void Save(const FString& Filename) const;

private:
    FKey Keys[ActionCount];
    float MouseMultiplier = 1.f;
    bool bFirstPerson = true;
    bool bStationaryThreatFacing = true;
};

FRegimentOfficerSettings& RegimentOfficerSettings();
void SaveRegimentOfficerSettings();
