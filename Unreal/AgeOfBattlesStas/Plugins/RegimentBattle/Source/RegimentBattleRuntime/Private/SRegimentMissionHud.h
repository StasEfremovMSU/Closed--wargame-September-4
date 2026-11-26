#pragma once

#include "RegimentMissionPreset.h"
#include "RegimentMissionAuthoring.h"
#include "Containers/Map.h"
#include "Templates/Function.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SButton;
class SSearchBox;
class SVerticalBox;
class UWorld;
struct FRegimentEraFaction;

enum class EMissionHudStage : uint8
{
    Modules,
    Campaigns,
    Tutorials,
    Setup,
    March,
    EnemyFound,
    MarchWithOrders
};

class SRegimentMissionHud final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentMissionHud) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void ShowModuleMenu();
    void ShowCampaignMenu();
    void ShowTutorialMenu();
    void SetTutorialNotice(FText Notice) { TutorialNotice = MoveTemp(Notice); }
    void SetCampaignNotice(FText Notice) { CampaignNotice = MoveTemp(Notice); }
    void SetModuleContext(const FString& Id, const FMissionPreset& Preset);
    void SetPreset(const FMissionPreset& Preset);
    void SetTerrain(battle::MarchTerrain InTerrain);
    void SetCanResume(bool bReady) { bCanResume = bReady; }
    void SetStatus(EMissionHudStage Stage, uint64 Selected);
    void SetMarchPosture(bool bColumn, int32 DelaySeconds, int32 CrossingPosition = 0);
    void SetMarchNotice(FText Notice) { MarchNotice = MoveTemp(Notice); }

    TFunction<void(const FMissionPreset&)> OnStart;
    TFunction<void(int32, int32)> OnStartCampaign;
    TFunction<void(ERegimentTutorial)> OnStartTutorial;
    TFunction<bool()> OnCampaignSavePending;
    TFunction<bool(int32, int32)> OnCampaignUnlocked;
    TFunction<bool(int32, int32)> OnCampaignComplete;
    TFunction<void(const FString&)> OnSelectModule;
    TFunction<void()> OnShowStatistics;
    TFunction<void()> OnQuit;
    TFunction<void(const FMissionPreset&)> OnReturnToModules;
    TFunction<void(battle::MarchTerrain, battle::MarchField)> OnTerrainChanged;
    TFunction<void(uint64)> OnSelectRegiment;
    TFunction<void()> OnToggleMarchPosture;
    TFunction<void()> OnResumeMarch;
    TFunction<void()> OnReturnToSetup;

private:
    friend class FRegimentScenarioFlowTest;
    enum class ESetupStep : uint8 { Scenario, Battlefield, Armies, Deployment, Launch };
    enum class EScenarioFilter : uint8 { All, Historical, Saved };

    TSharedRef<SWidget> MakeModulePanel();
    TSharedRef<SWidget> MakeCampaignPanel();
    TSharedRef<SWidget> MakeTutorialPanel();
    FText CampaignStageStatus(int32 Campaign, int32 Stage) const;
    FText GetCampaignNotice() const;
    TSharedRef<SWidget> MakeSetupPanel();
    TSharedRef<SWidget> MakePresetControls();
    TSharedRef<SWidget> MakeSetupSteps();
    TSharedRef<SWidget> MakeScenarioStep();
    TSharedRef<SWidget> MakeScenarioEras();
    TSharedRef<SWidget> MakeScenarioFilters();
    TSharedRef<SWidget> MakeBattlefieldStep();
    TSharedRef<SWidget> MakeLaunchStep();
    FReply AdvanceSetup();
    FText NextStepLabel() const;
    FText SetupStatus() const;
    void SetSetupStep(ESetupStep Step);
    TSharedRef<SWidget> MakeFactionTab();
    TSharedRef<SWidget> MakeFactionMenu(bool bBlue);
    TSharedRef<SWidget> MakeTerrainSelector();
    TSharedRef<SWidget> MakeFieldSizeControls();
    TSharedRef<SWidget> MakeObjectiveControls();
    TSharedRef<SWidget> MakeTacticalOptions();
    TSharedRef<SWidget> MakeRosterTable(int32 FirstIndex);
    TSharedRef<SWidget> MakeRosterRow(int32 RegimentIndex);
    TSharedRef<SWidget> MakeRosterProfileMenu(int32 RegimentIndex);
    const FRegimentEraFaction* RosterFaction(int32 RegimentIndex) const;
    TSharedRef<SWidget> MakeDeploymentTable(int32 FirstIndex);
    TSharedRef<SWidget> MakeDeploymentRow(int32 RegimentIndex);
    TSharedRef<SWidget> MakeDeploymentPreview();
    FReply RefreshDeploymentPreview();
    void ResetDeploymentPreview();
    TSharedRef<SWidget> MakeMarchStatus();
    TSharedRef<SWidget> MakeRegimentSelector(uint64 RegimentId);
    TSharedRef<SWidget> MakeMarchHelp();

    void RefreshPresetOptions();
    void RebuildScenarioList();
    void ChangeScenarioSearch(const FText& Query);
    void ResetScenarioPreview();
    void RefreshBriefingSources();
    FReply PreviewScenario(FMissionPresetListItem Option);
    bool IsCurrentScenario(const FMissionPresetListItem& Option) const;
    const FMissionPreset& PreviewedPreset() const;
    FReply SaveActivePreset();
    FReply SavePresetCopy();
    FReply StartMission();
    FReply SelectModule(FString Id);
    FReply SwitchScenarioEra(FString Id);
    void RememberDraftAndShowModules();
    FReply ReturnToModules();
    void ChooseTerrain(battle::MarchTerrain InTerrain);
    void ChooseFieldSize(battle::MarchField Field);
    void ChooseFaction(bool bBlue, FString Id);
    void ClearPresetNotice();

    TWeakObjectPtr<UWorld> World;
    TSharedPtr<SVerticalBox> ScenarioList;
    TSharedPtr<SSearchBox> ScenarioSearch;
    TSharedPtr<SButton> FirstScenarioButton;
    TArray<FMissionPresetListItem> ScenarioOptions;
    FString ScenarioQuery;
    FText ScenarioResultCount;
    EScenarioFilter ScenarioFilter = EScenarioFilter::All;
    TOptional<FMissionPreset> ScenarioPreview;
    TOptional<FMissionAuthoringPreview> DeploymentPreview;
    TSharedPtr<SWidget> DeploymentMap;
    FText DeploymentReport;
    FGuid PreviewId;
    TSharedPtr<SButton> FirstModuleButton;
    TSharedPtr<SEditableTextBox> PresetName;
    TSharedPtr<SVerticalBox> BriefingSources;
    FText PresetStatus;
    FText MarchNotice;
    FText CampaignNotice;
    FText TutorialNotice;
    bool bPresetError = false;
    FString ModuleId;
    TMap<FString, FText> PendingCopyNames;
    FMissionPreset Draft;
    ESetupStep SetupStep = ESetupStep::Scenario;
    EMissionHudStage Stage = EMissionHudStage::Modules;
    uint64 SelectedRegiment = 0;
    bool bCanResume = false;
    bool bTravelColumn = true;
    int32 MarchDeploymentSeconds = 0;
    int32 MarchCrossingPosition = 0;
};
