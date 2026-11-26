#pragma once

#include "Containers/Map.h"
#include "RegimentMissionPreset.h"
#include "RegimentHistoricalCampaign.h"
#include "RegimentMissionScenario.h"
#include "RegimentMarchBoard.h"
#include "RegimentTacticalCamera.h"

class APlayerController;
class ULocalPlayer;
class SRegimentMissionHud;

struct FRegimentMissionRuntime
{
    FRegimentMissionRuntime(AActor& Owner, bool bSmoke);
    ~FRegimentMissionRuntime();

    void TickMarch(float DeltaSeconds);
    void BeginTactical();
    bool TickCamera(float DeltaSeconds);
    void StopMapFlyover()
    {
        MapFlight.Stop();
        CameraVelocity = FVector::ZeroVector;
        CameraZoomTarget.Reset();
    }
    void ReturnToSetup();
    void RemoveHud();
    void RecordCampaignOutcome(battle::BattleOutcome Outcome, int32 Winner);
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
    void FlushCampaignProgress();
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
    bool bCampaignSavePending = false;
    RegimentHistoricalCampaign::FProgress CampaignProgress;
    TOptional<RegimentHistoricalCampaign::FRun> CampaignRun;
    std::optional<battle::MarchMission> March;
    std::optional<std::vector<battle::MarchRegiment>> DirectOpening;
    FVector Contact = FVector::ZeroVector;
    TWeakObjectPtr<ACameraActor> TacticalCamera;
    FRegimentMapFlight MapFlight;
    FVector CameraVelocity = FVector::ZeroVector;
    TOptional<FVector> CameraZoomTarget;
    TWeakObjectPtr<ULocalPlayer> Player;
    TSharedPtr<SRegimentMissionHud> Hud;
    TSet<uint64> PlayerOrders;
    double Accumulator = 0.0;
    double NextRoutePreview = 0.0;
    uint64 Selected = 1;
    bool bMapCameraApplied = false;
    bool bMarchResumed = false;
};
