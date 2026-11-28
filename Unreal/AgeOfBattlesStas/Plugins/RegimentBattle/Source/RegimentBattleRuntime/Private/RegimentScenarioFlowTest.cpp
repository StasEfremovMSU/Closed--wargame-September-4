#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Layout/Children.h"
#include "RegimentGameModules.h"
#include "RegimentHistoricalCampaign.h"
#include "SRegimentMissionHud.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
bool HasCaption(const TSharedRef<SWidget>& Widget, const FString& Caption, bool bVisibleOnly = false)
{
    if (bVisibleOnly && !Widget->GetVisibility().IsVisible())
        return false;
    if (Widget->GetType() == FName(TEXT("STextBlock")) &&
        StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Caption)
        return true;
    if (auto* Children = Widget->GetChildren())
        for (int32 Index = 0; Index < Children->Num(); ++Index)
            if (HasCaption(Children->GetChildAt(Index), Caption, bVisibleOnly))
                return true;
    return false;
}

TSharedPtr<SSearchBox> FindSearchBox(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetType() == FName(TEXT("SSearchBox")))
        return StaticCastSharedRef<SSearchBox>(Widget);
    if (auto* Children = Widget->GetChildren())
        for (int32 Index = 0; Index < Children->Num(); ++Index)
            if (auto Search = FindSearchBox(Children->GetChildAt(Index)))
                return Search;
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentScenarioFlowTest, "RegimentBattle.Presentation.ScenarioFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentScenarioFlowTest::RunTest(const FString&)
{
    FMissionPreset Draft;
    if (!TestTrue(TEXT("Sengoku opens a historical scenario"), RegimentGameModules::OpenPlayable(TEXT("sengoku"), Draft)))
        return false;

    auto Hud = SNew(SRegimentMissionHud);
    const auto TerrainSelector = Hud->MakeTerrainSelector();
    Hud->SlatePrepass(1.f);
    TestTrue(TEXT("The main menu offers an exit action"), HasCaption(Hud, TEXT("Выйти")));
    Hud->SetModuleContext(TEXT("sengoku"), Draft);
    Hud->SlatePrepass(1.f);
    TerrainSelector->SlatePrepass(1.f);
    using EStep = SRegimentMissionHud::ESetupStep;
    TestTrue(TEXT("Setup starts at scenario selection"), Hud->SetupStep == EStep::Scenario);
    TestTrue(TEXT("Scenario names are shown on the setup screen"), HasCaption(Hud, TEXT("Готовые сценарии")));
    TestTrue(TEXT("The catalogue has a visible era rail"), HasCaption(Hud, TEXT("Эпохи")));
    TestTrue(TEXT("An already constructed terrain list shows the current era's maps"),
        HasCaption(TerrainSelector, TEXT("Каванакадзима"), true));
    TestFalse(TEXT("The terrain list hides another era's additional maps"),
        HasCaption(TerrainSelector, TEXT("Аустерлиц"), true));

    const auto Options = RegimentGameModules::PlayablePresets(TEXT("sengoku"), RegimentMissionPresets::Directory());
    const auto* Alternate = Options.FindByPredicate([&](const FMissionPresetListItem& Item)
    {
        return Item.bBuiltIn && Item.bHistorical && Item.Name != Draft.Name;
    });
    if (!TestNotNull(TEXT("Another Sengoku historical scenario is available"), Alternate))
        return false;
    if (auto Search = FindSearchBox(Hud))
    {
        Search->SetText(FText::FromString(TEXT("   ") + Alternate->Name.ToLower() + TEXT("   ")));
        TestTrue(TEXT("Case-insensitive search preserves the matching row"),
            HasCaption(Hud->ScenarioList.ToSharedRef(), Alternate->Name));
        TestFalse(TEXT("Search filters out other scenarios"),
            HasCaption(Hud->ScenarioList.ToSharedRef(), Draft.Name));
        Search->SetText(FText::FromString(TEXT("несуществующая битва")));
        TestTrue(TEXT("No results has a recovery hint"),
            HasCaption(Hud->ScenarioList.ToSharedRef(), TEXT("Битвы не найдены. Измените поиск или фильтр.")));
        TestEqual(TEXT("Search never rewrites the active draft"), Hud->Draft.Name, Draft.Name);
        Hud->SetModuleContext(TEXT("sengoku"), Draft);
        TestTrue(TEXT("Entering a catalogue clears a previous search"),
            HasCaption(Hud->ScenarioList.ToSharedRef(), Draft.Name));
    }
    else
        AddError(TEXT("The scenario catalogue needs a search field."));
    Hud->PreviewScenario(*Alternate);
    TestEqual(TEXT("Preview preserves the active draft"), Hud->Draft.Name, Draft.Name);
    TestEqual(TEXT("Preview shows the selected scenario"), Hud->PreviewedPreset().Name, Alternate->Name);
    TestTrue(TEXT("Historical previews carry both terrain and opening geometry"),
        Hud->ScenarioBriefingMap.IsSet() && !Hud->ScenarioBriefingMap->Spawns.IsEmpty());
    TestTrue(TEXT("The historical briefing labels both map views"),
        HasCaption(Hud, TEXT("Местность")) && HasCaption(Hud, TEXT("Стартовая расстановка")));
    Hud->SetSetupStep(EStep::Launch);
    TestTrue(TEXT("Navigation cannot accidentally launch the old draft"), Hud->SetupStep == EStep::Scenario);

    Hud->AdvanceSetup();
    TestTrue(TEXT("Applying continues to the battlefield"), Hud->SetupStep == EStep::Battlefield);
    TestEqual(TEXT("Applying uses the selected scenario"), Hud->Draft.Name, Alternate->Name);
    const float InitialWidth = Hud->Draft.Field.width_m;
    Hud->Draft.Field.width_m += 20.f;
    Hud->SetSetupStep(EStep::Scenario);
    TestTrue(TEXT("Returning to the briefing refreshes edited terrain and deployment"),
        Hud->ScenarioBriefingMap && Hud->ScenarioBriefingMap->Field.width_m == InitialWidth + 20.f);
    Hud->Draft.Field.width_m = InitialWidth;
    Hud->SetSetupStep(EStep::Deployment);
    TestFalse(TEXT("Opening the deployment step does not build over thousands of figures"), Hud->DeploymentPreview.IsSet());
    Hud->RefreshDeploymentPreview();
    TestTrue(TEXT("The game deployment step checks and previews the current draft"), Hud->DeploymentPreview.IsSet());
    Hud->Draft.Roster[0].StartX = 5000.f;
    Hud->ClearPresetNotice();
    TestFalse(TEXT("Editing invalidates the old preview snapshot"), Hud->DeploymentPreview.IsSet());
    Hud->RefreshDeploymentPreview();
    TestFalse(TEXT("An invalid new draft does not reuse old map geometry"), Hud->DeploymentPreview.IsSet());
    TestTrue(TEXT("The deployment step names the invalid coordinate"),
        Hud->DeploymentReport.ToString().Contains(TEXT("regiments[0].start_x")));
    Hud->SetPreset(Draft);
    Hud->SetSetupStep(EStep::Launch);
    Hud->SlatePrepass(1.f);
    TestTrue(TEXT("Starting mode is on its own step"), HasCaption(Hud, TEXT("Способ начала")));

    TArray<bool> StartsOnPositions;
    Hud->OnStart = [&StartsOnPositions](const FMissionPreset& Preset)
    {
        StartsOnPositions.Add(Preset.StartsInBattle());
    };
    Hud->SetPreset(Draft);
    Hud->Draft.bDirectDeployment = false;
    Hud->AdvanceSetup();
    Hud->Draft.bDirectDeployment = true;
    Hud->AdvanceSetup();
    TestEqual(TEXT("Both launch modes produce a valid preset"), StartsOnPositions.Num(), 2);
    if (StartsOnPositions.Num() == 2)
    {
        TestFalse(TEXT("March starts the detection phase"), StartsOnPositions[0]);
        TestTrue(TEXT("Direct opening starts on the chosen positions"), StartsOnPositions[1]);
    }

    Hud->SetSetupStep(EStep::Scenario);
    TOptional<FMissionPreset> Archived;
    Hud->OnReturnToModules = [&Hud, &Archived](const FMissionPreset& Preset)
    {
        Archived = Preset;
        Hud->SetStatus(EMissionHudStage::Modules, 0);
    };
    Hud->OnSelectModule = [&Hud](const FString& Id)
    {
        FMissionPreset AnotherEra;
        if (RegimentGameModules::OpenPlayable(Id, AnotherEra))
            Hud->SetModuleContext(Id, AnotherEra);
    };
    Hud->Draft.bDirectDeployment = false;
    Hud->ChangeScenarioSearch(FText::FromString(TEXT("Сэкигахара")));
    Hud->SwitchScenarioEra(TEXT("napoleonic"));
    TerrainSelector->SlatePrepass(1.f);
    TestEqual(TEXT("The atlas rail switches to the chosen era"), Hud->ModuleId, FString(TEXT("napoleonic")));
    TestTrue(TEXT("Era switches archive the former draft before loading"), Archived.IsSet() &&
        Archived->Era.Id == TEXT("sengoku") && !Archived->bDirectDeployment);
    TestTrue(TEXT("The switched catalogue exposes the new era's opening battle"),
        HasCaption(Hud->ScenarioList.ToSharedRef(), Hud->Draft.Name));
    TestFalse(TEXT("The switched catalogue excludes prior-era scenarios"),
        HasCaption(Hud->ScenarioList.ToSharedRef(), Draft.Name));
    TestTrue(TEXT("Era switches update the terrain list in place"),
        HasCaption(TerrainSelector, TEXT("Аустерлиц"), true));
    int32 Launches = 0;
    Hud->OnCampaignUnlocked = [](int32, int32 Stage) { return Stage == 0; };
    Hud->OnStartCampaign = [&Launches](int32, int32) { ++Launches; };
    Hud->ShowCampaignMenu();
    Hud->PreviewCampaignStage(0, 1);
    TestFalse(TEXT("Locked campaign missions cannot be selected"), Hud->CampaignPreview.IsSet());
    Hud->PreviewCampaignStage(0, 0);
    TestTrue(TEXT("Opening a campaign stage presents its briefing first"),
        Hud->CampaignPreview.IsSet() && Hud->CampaignBriefingMap.IsSet());
    TestEqual(TEXT("Viewing the stage never launches the mission"), Launches, 0);
    Hud->LaunchCampaignStage();
    TestEqual(TEXT("The launch action starts the selected campaign stage"), Launches, 1);
    return true;
}
#endif
