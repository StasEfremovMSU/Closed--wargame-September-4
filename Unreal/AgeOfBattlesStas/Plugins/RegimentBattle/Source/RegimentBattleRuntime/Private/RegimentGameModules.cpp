#include "RegimentGameModules.h"
#include "RegimentEraCatalog.h"

#include "Algo/Find.h"

#define LOCTEXT_NAMESPACE "RegimentGameModules"

namespace
{
struct FModuleDefinition
{
    const TCHAR* Id;
    FText Name;
    FGuid OpeningBattle;
};

TConstArrayView<FModuleDefinition> ModuleDefinitions()
{
    static const FModuleDefinition Definitions[] = {
        {TEXT("ancient"), LOCTEXT("Ancient", "Античность"), FGuid(0xBD8E7423, 0xCDA541E0, 0xAD6DAF78, 0xD4C9F547)},
        {TEXT("high_medieval"), LOCTEXT("HighMedieval", "Классическое средневековье"), FGuid(0x8E7D530F, 0x47E5433E, 0x92ED12CA, 0x13FD89C8)},
        {TEXT("late_medieval"), LOCTEXT("LateMedieval", "Позднее средневековье"), FGuid(0x594814A7, 0x04264AB5, 0x91F9D5E8, 0xCF0E04E9)},
        {TEXT("renaissance"), LOCTEXT("Renaissance", "Возрождение и раннее Новое время"), FGuid(0x27C8FD9B, 0xAA5B4EA2, 0x8C65042B, 0xBAE21B60)},
        {TEXT("sengoku"), LOCTEXT("Sengoku", "Япония эпохи Сэнгоку"), FGuid(0x7A49D258, 0x5A5F42CE, 0xB72F85D7, 0x0E1600AD)},
        {TEXT("great_northern_war"), LOCTEXT("GreatNorthernWar", "Северная война"), FGuid(0x655310A0, 0x48854A4A, 0xBDE3DE48, 0x9EE8DA84)},
        {TEXT("napoleonic"), LOCTEXT("Napoleonic", "Наполеоновские войны"), FGuid(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33)},
    };
    return Definitions;
}

const FModuleDefinition* FindModule(FStringView Id)
{
    return Algo::FindByPredicate(ModuleDefinitions(), [Id](const FModuleDefinition& Module)
    {
        return Id.Equals(Module.Id);
    });
}

const FRegimentEraDefinition* FindInstalledModule(FStringView Id)
{
    return FindModule(Id) ? RegimentEraCatalog::Find(Id) : nullptr;
}

bool LoadStartingPreset(const FRegimentEraDefinition& Era, FMissionPreset& Preset)
{
    FText Error;
    return RegimentMissionPresets::LoadBuiltIn(Era.MissionId, Preset) &&
        FStringView(Era.Id).Equals(Preset.Era.Id) &&
        RegimentMissionPresets::Validate(Preset, Error);
}
}

TArray<FRegimentGameModule> RegimentGameModules::List()
{
    TArray<FRegimentGameModule> Modules;
    const auto Definitions = ModuleDefinitions();
    Modules.Reserve(Definitions.Num());
    for (const FModuleDefinition& Definition : Definitions)
    {
        const FRegimentEraDefinition* Installed = RegimentEraCatalog::Find(Definition.Id);
        FMissionPreset StartingPreset;
        const bool bAvailable = Installed && LoadStartingPreset(*Installed, StartingPreset);
        Modules.Add({Definition.Id, Definition.Name, bAvailable, Installed ? Installed->Factions.Num() : 0});
    }
    return Modules;
}

FText RegimentGameModules::Name(FStringView Id)
{
    if (Id.IsEmpty())
        return LOCTEXT("PracticeGround", "Полигон");
    if (const auto* Module = FindModule(Id))
        return Module->Name;
    return FText::GetEmpty();
}

bool RegimentGameModules::Open(FStringView Id, FMissionPreset& Out)
{
    FMissionPreset StartingPreset;
    if (Id.IsEmpty())
    {
        if (!RegimentMissionPresets::LoadBuiltIn(FGuid{}, StartingPreset))
            return false;
    }
    else
    {
        const FRegimentEraDefinition* Installed = FindInstalledModule(Id);
        if (!Installed || !LoadStartingPreset(*Installed, StartingPreset))
            return false;
    }
    Out = MoveTemp(StartingPreset);
    return true;
}

TArray<FMissionPresetListItem> RegimentGameModules::Presets(FStringView Id, const FString& Directory)
{
    FMissionPreset StartingPreset;
    if (!Open(Id, StartingPreset))
        return {};

    auto Items = RegimentMissionPresets::BuiltIns();
    Items.RemoveAll([Id](const FMissionPresetListItem& Item)
    {
        return !Item.bSharedAcrossModules && !Id.Equals(Item.EraId);
    });
    Items.Append(RegimentMissionPresets::ListForEra(Directory, Id));
    return Items;
}

TArray<FMissionPresetListItem> RegimentGameModules::PlayablePresets(FStringView Id, const FString& Directory)
{
    auto Items = Presets(Id, Directory);
    Items.RemoveAll([Id](const FMissionPresetListItem& Item)
    {
        return Item.bBuiltIn && !Item.bHistorical && !Item.bPlayableExercise &&
            !(Id.IsEmpty() && !Item.Id.IsValid());
    });
    for (auto& Item : Items)
        if (Item.bBuiltIn && !Item.Id.IsValid())
            Item.Name = LOCTEXT("CustomBattle", "Своя битва").ToString();
    return Items;
}

bool RegimentGameModules::OpenPlayable(FStringView Id, FMissionPreset& Out)
{
    if (Id.IsEmpty())
    {
        if (!Open(Id, Out))
            return false;
        Out.Name = LOCTEXT("CustomBattle", "Своя битва").ToString();
        Out.Terrain = battle::MarchTerrain::OpenField;
        Out.Field = {680, 450};
        Out.FigureTarget = MissionStandardFigureTarget;
        Out.bRegimentTactics = true;
        Out.bAdaptiveBattle = true;
        Out.bDirectDeployment = true;
        for (int32 Index = 0; Index < Out.Roster.Num(); ++Index)
        {
            auto& Regiment = Out.Roster[Index];
            if (!Regiment.bEnabled)
                continue;
            const int32 Slot = Index % MissionRegimentsPerTeam;
            const float Side = Index < MissionRegimentsPerTeam ? -1.f : 1.f;
            Regiment.StartX = Side * (Slot % 2 ? 108.f : 62.f);
            Regiment.StartY = float(Slot / 2 - 1) * 72.f;
        }
        return true;
    }

    const FString Directory = RegimentMissionPresets::Directory();
    const auto Items = PlayablePresets(Id, Directory);
    FText Error;
    if (const auto* Module = FindModule(Id))
        if (const auto* Preferred = Algo::FindBy(Items, Module->OpeningBattle, &FMissionPresetListItem::Id))
            if (Load(Id, Directory, *Preferred, Out, Error))
                return true;
    for (const auto& Item : Items)
        if (Item.bHistorical && Load(Id, Directory, Item, Out, Error))
            return true;
    return false;
}

bool RegimentGameModules::LoadPlayable(FStringView Id, const FString& Directory, const FMissionPresetListItem& Item,
    FMissionPreset& Out, FText& Error)
{
    if (Id.IsEmpty() && Item.bBuiltIn && !Item.Id.IsValid())
        return OpenPlayable(Id, Out);
    return Load(Id, Directory, Item, Out, Error);
}

bool RegimentGameModules::Load(FStringView Id, const FString& Directory, const FMissionPresetListItem& Item,
    FMissionPreset& Out, FText& Error)
{
    const auto* Era = Id.IsEmpty() ? nullptr : FindInstalledModule(Id);
    FMissionPreset Loaded;
    if (!Id.IsEmpty() && (!Era || !LoadStartingPreset(*Era, Loaded)))
    {
        Error = LOCTEXT("UnavailableModule", "Этот модуль недоступен.");
        return false;
    }

    if (Item.bBuiltIn)
    {
        const auto BuiltIns = RegimentMissionPresets::BuiltIns();
        const auto* Registered = Algo::FindBy(BuiltIns, Item.Id, &FMissionPresetListItem::Id);
        if (!Registered || !RegimentMissionPresets::LoadBuiltIn(Registered->Id, Loaded))
        {
            Error = LOCTEXT("MissingBuiltIn", "Встроенная миссия не найдена.");
            return false;
        }
        if (Registered->bSharedAcrossModules)
        {
            if (Era)
                RegimentMissionPresets::ApplyEra(Loaded, *Era);
            else
                Loaded.Era = {};
            if (!RegimentMissionPresets::Validate(Loaded, Error))
                return false;
        }
    }
    else if (!RegimentMissionPresets::Load(Directory, Item.Id, Loaded, Error))
    {
        return false;
    }

    if (!Id.Equals(Loaded.Era.Id))
    {
        Error = LOCTEXT("WrongModule", "Эта миссия относится к другому модулю.");
        return false;
    }
    Out = MoveTemp(Loaded);
    Error = FText::GetEmpty();
    return true;
}

#undef LOCTEXT_NAMESPACE
