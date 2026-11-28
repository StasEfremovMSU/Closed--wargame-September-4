#pragma once

#include "Containers/Map.h"
#include "RegimentBattleReport.h"
#include "RegimentMissionPreset.h"
#include "RegimentHistoricalCampaign.h"
#include "RegimentMissionScenario.h"
#include "RegimentMarchBoard.h"
#include "RegimentTacticalCamera.h"

class APlayerController;
class ULocalPlayer;
class SRegimentMissionHud;
class SWidget;

class FRegimentMissionProgress;

struct FRegimentMissionRuntime
{
    FRegimentMissionRuntime(AActor& Owner, bool bSmoke);
    ~FRegimentMissionRuntime();

    void TickMarch(float DeltaSeconds);
    void BeginTactical();
    bool TickCamera(float DeltaSeconds);
    void FollowRegiment(uint64 Regiment, int32 StartingStrength);
    uint64 CameraRegiment() const { return FollowedRegiment; }
    void StopFollowingRegiment() { FollowedRegiment = 0; }
    void StopMapFlyover()
    {
        MapFlight.Stop();
        StopFollowingRegiment();
        CameraVelocity = FVector::ZeroVector;
        CameraZoomTarget.Reset();
    }
    void ReturnToSetup();
    void RemoveHud();
    void RecordCampaignOutcome(battle::BattleOutcome Outcome, int32 Winner);
    void BeginBattleReport(const battle::World* World);
    void ObserveBattleReport(const battle::World& World, const FRegimentMissionProgress* Progress);
    bool IsBattleReportOpen() const { return bDebriefOpen && StatisticsOverlay.IsValid(); }
    const FMissionPreset& GetPreset() const { return Preset; }

    AActor& Owner;
    FVector Origin;
    std::optional<battle::Scenario> TacticalScenario;
    bool bSmoke = false;

private:
    void SelectModule(const FString& Id);
    void ReturnToModules(const FMissionPreset& Draft);
    void StartMission(FMissionPreset Setup);
    void StartCampaignStage(int32 Campaign, int32 Stage);
    void StartTutorial(ERegimentTutorial Lesson);
    void FlushCampaignProgress();
    void FlushPlayerStatistics();
    void ShowPlayerStatistics();
    void OpenStatisticsOverlay(TSharedRef<SWidget> Widget, bool bDebrief);
    void CloseStatisticsOverlay();
    void AttachHud(APlayerController*);
    void HandleMapInput(APlayerController&);
    void PresentMarch();
    void ExerciseSmokeWaypoints();

    FRegimentMarchBoard Board;
    FMissionPreset Preset;
    FString ModuleId;
    TMap<FString, FMissionPreset> ModuleDrafts;
    bool bChoosingModule = true;
    bool bShowCampaigns = false;
    bool bShowTutorials = false;
    bool bCampaignSavePending = false;
    RegimentHistoricalCampaign::FProgress CampaignProgress;
    TOptional<RegimentHistoricalCampaign::FRun> CampaignRun;
    FRegimentPlayerStats PlayerStatistics;
    TOptional<FRegimentBattleReportAccumulator> BattleReport;
    TSharedPtr<SWidget> StatisticsOverlay;
    TWeakObjectPtr<ULocalPlayer> StatisticsPlayer;
    bool bDebriefOpen = false;
    bool bStatisticsSavePending = false;
    std::optional<battle::MarchMission> March;
    std::optional<std::vector<battle::MarchRegiment>> DirectOpening;
    FVector Contact = FVector::ZeroVector;
    TWeakObjectPtr<ACameraActor> TacticalCamera;
    FRegimentMapFlight MapFlight;
    FRegimentFollowCamera FollowCamera;
    uint64 FollowedRegiment = 0;
    FVector CameraVelocity = FVector::ZeroVector;
    TOptional<FVector> CameraZoomTarget;
    TWeakObjectPtr<ULocalPlayer> Player;
    TSharedPtr<SRegimentMissionHud> Hud;
    TSet<uint64> PlayerOrders;
    double Accumulator = 0.0;
    double NextRoutePreview = 0.0;
    TOptional<FVector> LastMarchHoverPoint;
    uint64 Selected = 1;
    bool bMapCameraApplied = false;
    bool bMarchResumed = false;
};
