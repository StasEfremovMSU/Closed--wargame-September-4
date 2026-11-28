#include "RegimentOfficerSettings.h"

#include "Algo/Find.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "RegimentOfficerSettings"

namespace
{
constexpr const TCHAR* Section = TEXT("OfficerControls");
constexpr const TCHAR* ActionNames[] = {
    TEXT("Forward"), TEXT("Backward"), TEXT("Left"), TEXT("Right"), TEXT("TurnLeft"),
    TEXT("TurnRight"), TEXT("Run"), TEXT("Jump"), TEXT("Attack"), TEXT("Parry"), TEXT("Camera")
};
static_assert(UE_ARRAY_COUNT(ActionNames) == FRegimentOfficerSettings::ActionCount);

FString SettingsFile()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), TEXT("RegimentOfficer.ini"));
}

bool IsAllowedPersonalKey(const FKey& Key, ERegimentOfficerInputAction Action)
{
    const FKey Reserved[] = {EKeys::Tab, EKeys::Escape, EKeys::G, EKeys::F, EKeys::R, EKeys::M,
        EKeys::H, EKeys::T, EKeys::B, EKeys::Y, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
        EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::MouseScrollUp,
        EKeys::MouseScrollDown, EKeys::AnyKey};
    return Key.IsValid() && Key.IsDigital() && !Key.IsGamepadKey() && !Key.IsTouch() &&
        !Algo::Find(Reserved, Key) && (Key != EKeys::RightShift || Action == ERegimentOfficerInputAction::Run);
}
}

FRegimentOfficerSettings::FRegimentOfficerSettings()
    : Keys{EKeys::W, EKeys::S, EKeys::A, EKeys::D, EKeys::Q, EKeys::E, EKeys::LeftShift,
           EKeys::SpaceBar, EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::V}
{
}

bool FRegimentOfficerSettings::Bind(ERegimentOfficerInputAction Action, const FKey& NewKey)
{
    const int32 Index = int32(Action);
    if (Index >= ActionCount || !IsAllowedPersonalKey(NewKey, Action))
        return false;
    for (int32 Other = 0; Other < ActionCount; ++Other)
        if (Index != Other && Keys[Other] == NewKey)
            return false;
    Keys[Index] = NewKey;
    return true;
}

void FRegimentOfficerSettings::SetMouse(float Multiplier)
{
    if (FMath::IsFinite(Multiplier))
        MouseMultiplier = FMath::Clamp(Multiplier, MinMouse, MaxMouse);
}

FText FRegimentOfficerSettings::ActionLabel(ERegimentOfficerInputAction Action)
{
    const FText Labels[] = {
        LOCTEXT("Forward", "Вперёд"), LOCTEXT("Backward", "Назад"),
        LOCTEXT("Left", "Шаг влево"), LOCTEXT("Right", "Шаг вправо"),
        LOCTEXT("TurnLeft", "Поворот влево"), LOCTEXT("TurnRight", "Поворот вправо"),
        LOCTEXT("Run", "Бег"), LOCTEXT("Jump", "Прыжок"),
        LOCTEXT("Attack", "Удар"), LOCTEXT("Parry", "Парирование"), LOCTEXT("Camera", "Сменить вид офицера")
    };
    static_assert(UE_ARRAY_COUNT(Labels) == ActionCount);
    return int32(Action) < ActionCount ? Labels[int32(Action)] : FText::GetEmpty();
}

void FRegimentOfficerSettings::Load(const FString& Filename)
{
    FConfigFile File;
    File.Read(Filename);
    FRegimentOfficerSettings Loaded;
    bool bValid = true;
    TSet<FKey> Unique;
    for (int32 Index = 0; Index < ActionCount; ++Index)
    {
        FString Name;
        if (File.GetString(Section, ActionNames[Index], Name))
            Loaded.Keys[Index] = FKey(FName(*Name));
        else if (Index == int32(ERegimentOfficerInputAction::Camera) && Unique.Contains(Loaded.Keys[Index]))
        {
            // Older settings may already use V for a personal action.
            const FKey Alternatives[] = { EKeys::C, EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5,
                EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10 };
            if (const auto* Available = Algo::FindByPredicate(Alternatives, [&Unique](const FKey& Key) { return !Unique.Contains(Key); }))
                Loaded.Keys[Index] = *Available;
        }
        if (!IsAllowedPersonalKey(Loaded.Keys[Index], ERegimentOfficerInputAction(Index)) || Unique.Contains(Loaded.Keys[Index]))
            bValid = false;
        Unique.Add(Loaded.Keys[Index]);
    }
    *this = bValid ? Loaded : FRegimentOfficerSettings{};
    float Multiplier;
    if (File.GetFloat(Section, TEXT("MouseSensitivity"), Multiplier))
        SetMouse(Multiplier);
    File.GetBool(Section, TEXT("FirstPerson"), bFirstPerson);
    File.GetBool(Section, TEXT("StationaryThreatFacing"), bStationaryThreatFacing);
}

void FRegimentOfficerSettings::Save(const FString& Filename) const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FConfigFile File;
    for (int32 Index = 0; Index < ActionCount; ++Index)
        File.SetString(Section, ActionNames[Index], *Keys[Index].GetFName().ToString());
    File.SetFloat(Section, TEXT("MouseSensitivity"), MouseMultiplier);
    File.SetBool(Section, TEXT("FirstPerson"), bFirstPerson);
    File.SetBool(Section, TEXT("StationaryThreatFacing"), bStationaryThreatFacing);
    File.Write(Filename, false);
}

FRegimentOfficerSettings& RegimentOfficerSettings()
{
    static FRegimentOfficerSettings Values = []
    {
        FRegimentOfficerSettings Settings;
        Settings.Load(SettingsFile());
        return Settings;
    }();
    return Values;
}

void SaveRegimentOfficerSettings()
{
    RegimentOfficerSettings().Save(SettingsFile());
}

#undef LOCTEXT_NAMESPACE
