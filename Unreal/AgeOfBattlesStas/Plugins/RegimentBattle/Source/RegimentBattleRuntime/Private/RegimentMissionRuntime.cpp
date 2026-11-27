#include "RegimentMissionRuntime.h"
#include "RegimentMissionProgress.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RegimentBattleField.h"
#include "RegimentBattleGround.h"
#include "RegimentBattlePlayback.h"
#include "RegimentGameModules.h"
#include "RegimentMissionTutorial.h"
#include "SRegimentBattleResults.h"
#include "SRegimentMissionHud.h"

DEFINE_LOG_CATEGORY_STATIC(LogRegimentMission, Log, All);

namespace
{
constexpr double Cm = 100.0;
constexpr int32 PlayerTeam = 0;

battle::MarchTerrain RequestedTerrain()
{
    FString Value;
    if (FParse::Value(FCommandLine::Get(), TEXT("RegimentMissionTerrain="), Value))
        if (const auto Choice = RegimentMissionPresets::FindTerrain(Value))
            return *Choice;
    return battle::MarchTerrain::River;
}

FVector PointOnBoard(battle::Vec3 Pos, const FVector& Origin, const FMissionPreset& Preset)
{
    if (const auto Hill = battle::MarchHill(Preset.Terrain, Preset.Field))
        Pos.z += battle::HillHeight(*Hill, Pos);
    return Origin + FVector(Pos.x, Pos.y, Pos.z) * Cm;
}

FVector2D MarkerScale(const battle::MarchRegiment& Regiment, bool bRegimentTactics)
{
    if (!bRegimentTactics || Regiment.ActiveFormation().columns == Regiment.formation.columns)
        return FVector2D(1.0, 1.0);
    return FVector2D(1.34, 0.61);
}
}

FRegimentMissionRuntime::FRegimentMissionRuntime(AActor& InOwner, bool bInSmoke)
    : Owner(InOwner), Origin(InOwner.GetActorLocation() + FVector(0, 32000, 0)), bSmoke(bInSmoke),
      Board(Owner, Origin)
{
    CampaignProgress.Load(RegimentHistoricalCampaign::SavePath());
    if (!bSmoke)
        PlayerStatistics.Load(FRegimentPlayerStats::SavePath());
    Preset.Terrain = RequestedTerrain();
    Board.SetTerrain(Preset.Terrain, Preset.Field);
    FString Workload;
    int32 Tutorial = 0;
    if (!bSmoke && FParse::Value(FCommandLine::Get(), TEXT("RegimentTutorial="), Tutorial) &&
        Tutorial >= int32(ERegimentTutorial::Infantry) && Tutorial <= int32(ERegimentTutorial::Command))
        StartTutorial(ERegimentTutorial(Tutorial));
    else if (FParse::Value(FCommandLine::Get(), TEXT("RegimentPerfPreset="), Workload))
    {
        const FGuid Historic(0xBD8E7423, 0xCDA541E0, 0xAD6DAF78, 0xD4C9F547);
        const FGuid Large(0x39FA5E9A, 0x273F4DCE, 0xB950627B, 0xFA9D8921);
        const FGuid Waterloo(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33);
        const FGuid Sekigahara(0x7A49D258, 0x5A5F42CE, 0xB72F85D7, 0x0E1600AD);
        const FGuid Id = Workload.Equals(TEXT("Cannae10k"), ESearchCase::IgnoreCase) ? Large
            : Workload.Equals(TEXT("Cannae"), ESearchCase::IgnoreCase) ? Historic
            : Workload.Equals(TEXT("Waterloo"), ESearchCase::IgnoreCase) ? Waterloo
            : Workload.Equals(TEXT("Sekigahara"), ESearchCase::IgnoreCase) ? Sekigahara
            : Workload.Equals(TEXT("Smoke"), ESearchCase::IgnoreCase) ? MissionSmokeExerciseId : FGuid{};
        FMissionPreset Benchmark;
        FString FixtureDirectory;
        const bool bExternalFixture = FParse::Value(FCommandLine::Get(), TEXT("RegimentPerfPresetDirectory="),
            FixtureDirectory);
        FText FixtureError;
        if (Id.IsValid() && (bExternalFixture ? RegimentMissionPresets::Load(FixtureDirectory, Id, Benchmark, FixtureError)
            : RegimentMissionPresets::LoadBuiltIn(Id, Benchmark)))
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("RegimentBaselineCore")))
                Benchmark.bAdaptiveBattle = false;
            ModuleId = Benchmark.Era.Id;
            bChoosingModule = false;
            StartMission(MoveTemp(Benchmark));
        }
        else
            UE_LOG(LogRegimentMission, Warning, TEXT("Unknown or missing performance preset: %s %s"),
                *Workload, *FixtureError.ToString());
    }
    else if (bSmoke)
    {
        bChoosingModule = false;
        // The legacy campaign smoke also serves as a compatibility check.
        Preset.bRegimentTactics = false;
        const auto SetStrength = [this](int32 Index, int32 Swords, int32 Archers)
        {
            auto& Entry = Preset.Roster[Index];
            Entry.Swords = Swords;
            Entry.Archers = Archers;
            Entry.Spearmen = 0;
        };
        SetStrength(0, 12, 4);
        SetStrength(1, 8, 2);
        SetStrength(MissionRegimentsPerTeam, 17, 3);
        SetStrength(MissionRegimentsPerTeam + 1, 4, 0);
        StartMission(Preset);
    }
}

FRegimentMissionRuntime::~FRegimentMissionRuntime()
{
    CloseStatisticsOverlay();
    FlushPlayerStatistics();
    RemoveHud();
    if (auto* Camera = TacticalCamera.Get())
        Camera->Destroy();
}

void FRegimentMissionRuntime::AttachHud(APlayerController* Controller)
{
    auto* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
    if (Hud || !Local || !Local->ViewportClient || !Local->ViewportClient->GetGameLayerManager())
        return;

    Hud = SNew(SRegimentMissionHud).World(Controller->GetWorld());
    Hud->OnSelectModule = [this](const FString& Id) { SelectModule(Id); };
    Hud->OnShowStatistics = [this] { ShowPlayerStatistics(); };
    Hud->OnQuit = [this]
    {
        FlushCampaignProgress();
        FlushPlayerStatistics();
        UKismetSystemLibrary::QuitGame(&Owner, nullptr, EQuitPreference::Quit, false);
    };
    Hud->OnReturnToModules = [this](const FMissionPreset& Draft) { ReturnToModules(Draft); };
    Hud->OnTerrainChanged = [this](battle::MarchTerrain Choice, battle::MarchField Field)
    {
        if (March || DirectOpening || TacticalScenario)
            return;
        Preset.Terrain = Choice;
        Preset.Field = Field;
        Board.SetTerrain(Choice, Field);
    };
    Hud->OnCampaignSavePending = [this] { return bCampaignSavePending; };
    Hud->OnCampaignUnlocked = [this](int32 Campaign, int32 Stage)
    { return CampaignProgress.IsUnlocked(Campaign, Stage); };
    Hud->OnCampaignComplete = [this](int32 Campaign, int32 Stage)
    { return CampaignProgress.IsComplete(Campaign, Stage); };
    Hud->OnStartCampaign = [this](int32 Campaign, int32 Stage) { StartCampaignStage(Campaign, Stage); };
    Hud->OnStartTutorial = [this](ERegimentTutorial Lesson) { StartTutorial(Lesson); };
    Hud->OnStart = [this](const FMissionPreset& Setup) { StartMission(Setup); };
    Hud->OnSelectRegiment = [this](uint64 Id)
    {
        if (Id >= 1 && Id <= MissionRosterCapacity && MissionTeam(int32(Id - 1)) == 0 &&
            Preset.Roster[int32(Id - 1)].bEnabled)
        {
            Selected = Id;
            Board.SetRoutePreview({});
            NextRoutePreview = 0.0;
        }
    };
    Hud->OnToggleMarchPosture = [this]
    {
        if (!March || !Preset.bRegimentTactics)
            return;
        const auto& Regiments = March->Regiments();
        const auto It = std::ranges::find(Regiments, battle::RegimentId{Selected}, &battle::MarchRegiment::id);
        if (It == Regiments.end() || It->team != PlayerTeam)
            return;
        const auto Posture = It->posture == battle::MarchPosture::TravelColumn
            ? battle::MarchPosture::BattleLine : battle::MarchPosture::TravelColumn;
        const bool bChanged = March->SetPosture(It->id, Posture);
        if (bChanged && !It->waypoint)
            PlayerOrders.Remove(It->id.value);
        PresentMarch();
        Hud->SetMarchNotice(bChanged ? FText::GetEmpty() :
            NSLOCTEXT("RegimentMission", "PostureTooNarrow", "Здесь мало места для строя."));
    };
    Hud->OnResumeMarch = [this] {
        bMarchResumed = March && !PlayerOrders.IsEmpty();
        if (bMarchResumed) March->ResumeMarch();
    };
    Hud->OnReturnToSetup = [this] { ReturnToSetup(); };
    Local->ViewportClient->AddViewportWidgetForPlayer(Local, Hud.ToSharedRef(), 10);
    Local->ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringRightMouseDown);
    Player = Local;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Controller->SetInputMode(Mode);
    Controller->bShowMouseCursor = true;
    if (bShowTutorials)
        Hud->ShowTutorialMenu();
    else if (bShowCampaigns)
        Hud->ShowCampaignMenu();
    else if (bChoosingModule)
        Hud->ShowModuleMenu();
    else
        Hud->SetModuleContext(ModuleId, Preset);
}

void FRegimentMissionRuntime::RemoveHud()
{
    if (auto* Local = Player.Get(); Hud && Local && Local->ViewportClient)
        Local->ViewportClient->RemoveViewportWidgetForPlayer(Local, Hud.ToSharedRef());
    Hud.Reset();
    Player.Reset();
}

void FRegimentMissionRuntime::SelectModule(const FString& Id)
{
    if (!bChoosingModule)
        return;

    FMissionPreset Starter;
    if (const auto* Draft = ModuleDrafts.Find(Id))
        Starter = *Draft;
    else if (!RegimentGameModules::OpenPlayable(Id, Starter))
        return;

    bShowCampaigns = false;
    bShowTutorials = false;
    ModuleId = Id;
    Preset = MoveTemp(Starter);
    bChoosingModule = false;
    Board.SetTerrain(Preset.Terrain, Preset.Field);
    if (Hud)
        Hud->SetModuleContext(ModuleId, Preset);
}

void FRegimentMissionRuntime::ReturnToModules(const FMissionPreset& Draft)
{
    if (bChoosingModule || March || DirectOpening || TacticalScenario)
        return;
    ModuleDrafts.Add(ModuleId, Draft);
    bChoosingModule = true;
    if (Hud)
        Hud->ShowModuleMenu();
}

void FRegimentMissionRuntime::StartCampaignStage(int32 Campaign, int32 Stage)
{
    using namespace RegimentHistoricalCampaign;
    if (!bChoosingModule || March || DirectOpening || TacticalScenario || !CampaignProgress.IsUnlocked(Campaign, Stage))
        return;

    const auto& Route = Catalog()[Campaign];
    FMissionPreset Setup;
    if (!RegimentMissionPresets::LoadBuiltIn(Route.Stages[Stage].MissionId, Setup) ||
        !Setup.StartsInBattle() || Setup.Era.BlueFaction != Route.PlayerFaction)
    {
        if (Hud)
            Hud->SetCampaignNotice(NSLOCTEXT("RegimentMission", "CampaignStageUnavailable", "Не удалось открыть сценарий кампании."));
        return;
    }

    FlushCampaignProgress();
    bShowTutorials = false;
    ModuleId = Setup.Era.Id;
    bChoosingModule = false;
    StartMission(MoveTemp(Setup));
    if (DirectOpening)
    {
        CampaignRun = FRun{Campaign, Stage};
        bShowCampaigns = true;
    }
    else
    {
        bChoosingModule = true;
        if (Hud)
            Hud->SetCampaignNotice(NSLOCTEXT("RegimentMission", "CampaignStageInvalid", "Сценарий кампании не прошёл проверку."));
    }
}

void FRegimentMissionRuntime::StartTutorial(ERegimentTutorial Lesson)
{
    if (!bChoosingModule || March || DirectOpening || TacticalScenario)
        return;

    FMissionPreset Setup;
    FText Error;
    if (!RegimentMissionTutorial::Load(Lesson, Setup) || !RegimentMissionPresets::Validate(Setup, Error))
    {
        UE_LOG(LogRegimentMission, Warning, TEXT("Could not start tutorial %d: %s"), int32(Lesson), *Error.ToString());
        if (Hud)
            Hud->SetTutorialNotice(NSLOCTEXT("RegimentMission", "TutorialUnavailable", "Не удалось открыть урок."));
        return;
    }

    bShowCampaigns = false;
    bShowTutorials = true;
    ModuleId = Setup.Era.Id;
    bChoosingModule = false;
    StartMission(MoveTemp(Setup));
}

void FRegimentMissionRuntime::RecordCampaignOutcome(battle::BattleOutcome Outcome, int32 Winner)
{
    if (!CampaignRun || !TacticalScenario || Outcome == battle::BattleOutcome::Running)
        return;
    if (CampaignProgress.RecordOutcome(CampaignRun, Outcome, Winner))
        bCampaignSavePending = true;
    FlushCampaignProgress();
}

void FRegimentMissionRuntime::FlushCampaignProgress()
{
    if (!bCampaignSavePending)
        return;

    bCampaignSavePending = !CampaignProgress.Save(RegimentHistoricalCampaign::SavePath());
    if (bCampaignSavePending)
        UE_LOG(LogRegimentMission, Error, TEXT("Could not save historical campaign progress"));
}

void FRegimentMissionRuntime::BeginBattleReport(const battle::World* World)
{
    CloseStatisticsOverlay();
    BattleReport.Reset();
    if (World)
        BattleReport.Emplace(*World);
}

void FRegimentMissionRuntime::ObserveBattleReport(const battle::World& World, const FRegimentMissionProgress* Progress)
{
    if (!BattleReport)
        return;
    if (World.outcome == battle::BattleOutcome::Running)
    {
        BattleReport->Observe(World);
        return;
    }
    const FRegimentBattleDebriefMilestones Milestones = Progress
        ? FRegimentBattleDebriefMilestones{Progress->FirstCrestControlTick(), Progress->FirstReserveContactTick()}
        : FRegimentBattleDebriefMilestones{};
    auto Report = BattleReport->Finish(World, Milestones);
    BattleReport.Reset();
    if (!Report)
        return;
    if (Preset.Tutorial == ERegimentTutorial::None)
    {
        PlayerStatistics.Record(*Report, PlayerTeam);
        bStatisticsSavePending = true;
        FlushPlayerStatistics();
    }

    TWeakObjectPtr<ARegimentBattleField> Field(Cast<ARegimentBattleField>(&Owner));
    const auto* Battle = Field.Get();
    if (!Battle)
        return;
    Report->Mission = FText::FromString(Preset.Name);
    Report->ObjectiveResult = Battle->GetMissionObjectiveText();
    const auto& Status = Battle->GetHudState();
    Report->SideNames[0] = Status.BlueFaction.IsEmpty()
        ? NSLOCTEXT("RegimentMission", "ResultsBlue", "Синяя сторона") : Status.BlueFaction;
    Report->SideNames[1] = Status.OrangeFaction.IsEmpty()
        ? NSLOCTEXT("RegimentMission", "ResultsOrange", "Оранжевая сторона") : Status.OrangeFaction;
    OpenStatisticsOverlay(MakeRegimentBattleDebrief(*Report,
        FSimpleDelegate::CreateLambda([Field] { if (auto* Live = Field.Get()) Live->RequestBattleRestart(); }),
        FSimpleDelegate::CreateLambda([Field] { if (auto* Live = Field.Get()) Live->RequestMissionSetup(); })), true);
}

void FRegimentMissionRuntime::FlushPlayerStatistics()
{
    if (bStatisticsSavePending)
        bStatisticsSavePending = !PlayerStatistics.Save(FRegimentPlayerStats::SavePath());
}

void FRegimentMissionRuntime::ShowPlayerStatistics()
{
    if (!bChoosingModule || StatisticsOverlay)
        return;
    FlushPlayerStatistics();
    OpenStatisticsOverlay(MakeRegimentPlayerStatistics(PlayerStatistics, bStatisticsSavePending,
        FSimpleDelegate::CreateLambda([this] { CloseStatisticsOverlay(); })), false);
}

void FRegimentMissionRuntime::OpenStatisticsOverlay(TSharedRef<SWidget> Widget, bool bDebrief)
{
    auto* Controller = Owner.GetWorld()->GetFirstPlayerController();
    auto* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
    if (!Local || !Local->ViewportClient || !Local->ViewportClient->GetGameLayerManager())
        return;
    CloseStatisticsOverlay();
    StatisticsOverlay = Widget;
    StatisticsPlayer = Local;
    bDebriefOpen = bDebrief;
    Local->ViewportClient->AddViewportWidgetForPlayer(Local, Widget, 30);
    FInputModeGameAndUI Mode;
    Mode.SetWidgetToFocus(Widget);
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Controller->SetInputMode(Mode);
    Controller->bShowMouseCursor = true;
}

void FRegimentMissionRuntime::CloseStatisticsOverlay()
{
    if (auto* Local = StatisticsPlayer.Get(); StatisticsOverlay && Local && Local->ViewportClient)
        Local->ViewportClient->RemoveViewportWidgetForPlayer(Local, StatisticsOverlay.ToSharedRef());
    StatisticsOverlay.Reset();
    StatisticsPlayer.Reset();
    bDebriefOpen = false;
}

void FRegimentMissionRuntime::StartMission(FMissionPreset Setup)
{
    if (bChoosingModule || March || DirectOpening || TacticalScenario ||
        !Setup.Era.Id.Equals(ModuleId, ESearchCase::CaseSensitive))
        return;
    FText Error;
    if (!RegimentMissionPresets::Validate(Setup, Error))
    {
        UE_LOG(LogRegimentMission, Warning, TEXT("%s"), *Error.ToString());
        return;
    }
    CampaignRun.Reset();
    Preset = MoveTemp(Setup);
    Board.SetTerrain(Preset.Terrain, Preset.Field);
    auto Opening = MakeMissionOpening(Preset);
    Accumulator = 0.0;
    Selected = Opening.front().id.value;
    PlayerOrders.Reset();
    bMarchResumed = false;
    if (Preset.StartsInBattle())
    {
        // Start on the next field tick so it can hand the scenario to tactical play.
        DirectOpening = MoveTemp(Opening);
        return;
    }

    March.emplace(MoveTemp(Opening), Preset.Terrain, Preset.bRegimentTactics, Preset.Field);
    PresentMarch();
    if (Hud && FSlateApplication::IsInitialized())
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    if (bSmoke)
    {
        const auto& Regiments = March->Regiments();
        const auto Blue = std::ranges::count(Regiments, PlayerTeam, &battle::MarchRegiment::team);
        const auto Terrain = Preset.Terrain;
        const bool bRiver = battle::MarchHasWaterCrossings(Terrain);
        const auto Crossings = battle::MarchCrossingsFor(Terrain);
        const int32 Fords = bRiver ? int32(std::ranges::count(Crossings, true, &battle::MarchCrossing::ford)) : 0;
        const int32 Bridges = bRiver
            ? int32(Crossings.size()) - Fords : 0;
        const int32 Walls = Terrain == battle::MarchTerrain::StoneWalls ? int32(battle::MarchObstacles(Terrain, Preset.Field).size()) : 0;
        int32 RosterCount = 0;
        for (const auto& Entry : Preset.Roster)
            if (Entry.bEnabled)
                RosterCount += Entry.Total();
        UE_LOG(LogRegimentMission, Display,
            TEXT("RegimentMission setup: regiments=%d blue=%d orange=%d roster=%d bridges=%d fords=%d terrain=%s walls=%d gates=%d"),
            int32(Regiments.size()), int32(Blue), int32(Regiments.size() - Blue), RosterCount,
            Bridges, Fords, RegimentMissionPresets::TerrainKey(Terrain), Walls, FMath::Max(0, Walls - 1));
    }
}

void FRegimentMissionRuntime::ReturnToSetup()
{
    BeginBattleReport(nullptr);
    FlushPlayerStatistics();
    March.reset();
    DirectOpening.reset();
    TacticalScenario.reset();
    Board.ClearMarkers();
    bMapCameraApplied = false;
    PlayerOrders.Reset();
    bMarchResumed = false;
    CampaignRun.Reset();
    FlushCampaignProgress();
    if (bShowCampaigns || bShowTutorials)
        bChoosingModule = true;
    if (Hud)
    {
        if (bShowTutorials)
            Hud->ShowTutorialMenu();
        else if (bShowCampaigns)
            Hud->ShowCampaignMenu();
        else
            Hud->SetStatus(EMissionHudStage::Setup, 0);
    }
    if (auto* Camera = TacticalCamera.Get())
        Camera->Destroy();
    TacticalCamera.Reset();
    StopMapFlyover();
}

void FRegimentMissionRuntime::HandleMapInput(APlayerController& Controller)
{
    const bool bClick = Controller.WasInputKeyJustPressed(EKeys::LeftMouseButton);
    const bool bCrossingOrder = Preset.bRegimentTactics &&
        battle::MarchHasWaterCrossings(Preset.Terrain) &&
        (Controller.IsInputKeyDown(EKeys::LeftControl) || Controller.IsInputKeyDown(EKeys::RightControl));
    if (!March->EnemyFound() && !bCrossingOrder &&
        (Controller.WasInputKeyJustReleased(EKeys::LeftControl) || Controller.WasInputKeyJustReleased(EKeys::RightControl)))
        Board.SetRoutePreview({});
    const bool bShiftChanged = Controller.WasInputKeyJustPressed(EKeys::LeftShift) ||
        Controller.WasInputKeyJustReleased(EKeys::LeftShift) ||
        Controller.WasInputKeyJustPressed(EKeys::RightShift) ||
        Controller.WasInputKeyJustReleased(EKeys::RightShift);
    const double Now = FPlatformTime::Seconds();
    if (!bClick && ((!March->EnemyFound() && !bCrossingOrder) || (!bShiftChanged && Now < NextRoutePreview)))
        return;
    NextRoutePreview = Now + .14;
    FVector RayOrigin;
    FVector RayDirection;
    if (!Controller.DeprojectMousePositionToWorld(RayOrigin, RayDirection))
    {
        Board.SetRoutePreview({});
        return;
    }
    const auto Surface = RegimentGroundPoint(RayOrigin, RayDirection, Origin, battle::MarchHill(Preset.Terrain, Preset.Field));
    if (!Surface)
    {
        Board.SetRoutePreview({});
        return;
    }
    const FVector Point = *Surface;
    for (const auto& Regiment : March->Regiments())
    {
        if (Regiment.team != PlayerTeam || !bClick)
            continue;
        const FRotator Yaw(0, FMath::RadiansToDegrees(Regiment.pose.yaw), 0);
        const FVector Offset = Yaw.UnrotateVector(Point - PointOnBoard(Regiment.pose.position, Origin, Preset));
        const auto Size = MarkerScale(Regiment, Preset.bRegimentTactics);
        if (FMath::Abs(Offset.X) <= FRegimentMarchBoard::TokenLengthCm * Size.X * .5 + 30.0 &&
            FMath::Abs(Offset.Y) <= FRegimentMarchBoard::TokenWidthCm * Size.Y * .5 + 30.0)
        {
            Selected = Regiment.id.value;
            Board.SetRoutePreview({});
            NextRoutePreview = 0.0;
            return;
        }
    }
    if (!March->EnemyFound() && !bCrossingOrder)
        return;
    const auto& Regiments = March->Regiments();
    const auto Anchor = std::ranges::find(Regiments, battle::RegimentId{Selected}, &battle::MarchRegiment::id);
    if (Anchor == Regiments.end())
    {
        Board.SetRoutePreview({});
        return;
    }
    const FVector Meters = (Point - Origin) / Cm;
    const battle::Vec3 Goal{float(Meters.X), float(Meters.Y), 0.f};
    const bool bGroup = !bCrossingOrder &&
        (Controller.IsInputKeyDown(EKeys::LeftShift) || Controller.IsInputKeyDown(EKeys::RightShift));
    const int32 AnchorIndex = int32(Selected - 1);
    const int32 Group = MissionGroup(AnchorIndex, Preset.Roster[AnchorIndex]);
    TArray<TPair<battle::RegimentId, battle::Vec3>, TInlineAllocator<MissionMaxRegimentsPerTeam>> Orders;
    TArray<FMarchBoardRouteSegment, TInlineAllocator<MissionMaxRegimentsPerTeam * 3>> Preview;
    bool bClear = true;
    for (const auto& Regiment : Regiments)
    {
        if (Regiment.team != PlayerTeam)
            continue;
        const int32 Index = int32(Regiment.id.value - 1);
        if (Regiment.id.value != Selected && (!bGroup || MissionGroup(Index, Preset.Roster[Index]) != Group))
            continue;
        const auto Destination = Goal + (Regiment.pose.position - Anchor->pose.position);
        const auto Route = bCrossingOrder ? March->PreviewCrossing(Regiment.id, Destination)
                                          : March->PreviewWaypoint(Regiment.id, Destination);
        auto Previous = PointOnBoard(Regiment.pose.position, Origin, Preset);
        if (!Route)
        {
            bClear = false;
            Preview.Add({Previous, PointOnBoard(Destination, Origin, Preset), false});
            continue;
        }
        for (const auto& Waypoint : *Route)
        {
            const auto Next = PointOnBoard(Waypoint.position, Origin, Preset);
            Preview.Add({Previous, Next, true});
            Previous = Next;
        }
        if (bCrossingOrder)
        {
            const float HalfWidth = (Regiment.formation.columns - 1) * Regiment.formation.spacing_m * .5f
                + Regiment.formation.body_radius_m;
            const battle::Vec3 Side{0.f, HalfWidth, 0.f};
            Preview.Add({PointOnBoard(Destination - Side, Origin, Preset),
                PointOnBoard(Destination + Side, Origin, Preset), true});
        }
        Orders.Emplace(Regiment.id, Destination);
    }
    Board.SetRoutePreview(Preview);
    if (!bClick)
        return;
    if (!bClear)
    {
        if (Hud)
            Hud->SetMarchNotice(bCrossingOrder
                ? NSLOCTEXT("RegimentMission", "CrossingGoalBlocked", "Выберите свободный рубеж за переправой с местом для всего полка.")
                : NSLOCTEXT("RegimentMission", "MarchRouteBlocked", "Строй не проходит к указанной точке."));
        return;
    }
    for (const auto& Order : Orders)
        if (bCrossingOrder ? March->QueueCrossing(Order.Key, Order.Value) : March->SetWaypoint(Order.Key, Order.Value))
            PlayerOrders.Add(Order.Key.value);
    if (Hud)
        Hud->SetMarchNotice(FText::GetEmpty());
}

void FRegimentMissionRuntime::PresentMarch()
{
    TArray<FMarchBoardMarker, TInlineAllocator<MissionRosterCapacity>> Markers;
    for (const auto& Regiment : March->Regiments())
    {
        FMarchBoardMarker Marker;
        Marker.Id = Regiment.id.value;
        Marker.Team = Regiment.team;
        Marker.Position = PointOnBoard(Regiment.pose.position, Origin, Preset);
        Marker.YawDegrees = FMath::RadiansToDegrees(Regiment.pose.yaw);
        Marker.bVisible = Regiment.team == PlayerTeam || March->EnemyFound();
        Marker.SizeScale = MarkerScale(Regiment, Preset.bRegimentTactics);
        if (Hud && Regiment.id.value == Selected && Preset.bRegimentTactics)
            Hud->SetMarchPosture(Regiment.posture == battle::MarchPosture::TravelColumn,
                FMath::CeilToInt(Regiment.deployment_seconds), int32(March->CrossingQueuePosition(Regiment.id)));
        if (Regiment.team == PlayerTeam && PlayerOrders.Contains(Marker.Id) && Regiment.waypoint)
            Marker.Waypoint = PointOnBoard(*Regiment.waypoint, Origin, Preset);
        Markers.Add(Marker);
    }
    Board.Update(Markers, Selected);
    if (Hud)
    {
        Hud->SetCanResume(!PlayerOrders.IsEmpty());
        Hud->SetStatus(!March->EnemyFound() ? EMissionHudStage::March
                         : bMarchResumed ? EMissionHudStage::MarchWithOrders : EMissionHudStage::EnemyFound,
            Selected);
    }
}

void FRegimentMissionRuntime::ExerciseSmokeWaypoints()
{
    for (int32 Index = 0; Index < MissionRegimentsPerTeam; ++Index)
    {
        const uint64 Id = uint64(Index + 1);
        const auto& Crossing = battle::MarchCrossings[Index / MissionDefaultRegimentsPerGroup];
        const float GoalX = Index % MissionDefaultRegimentsPerGroup == 0 ? 24.f : 12.f;
        if (March->SetWaypoint({Id}, {GoalX, Crossing.y, 0.f}))
            PlayerOrders.Add(Id);
    }

    const auto Before = March->Regiments();
    const battle::Vec3 NewFlag{23.f, battle::MarchCrossings.front().y, 0.f};
    const bool bRevised = March->SetWaypoint({1}, NewFlag) &&
        March->Regiments().front().waypoint == NewFlag &&
        Before.front().waypoint != NewFlag;
    int32 Preserved = 0;
    for (int32 Index = 1; Index < MissionRegimentsPerTeam; ++Index)
    {
        const auto& Current = March->Regiments()[Index];
        if (PlayerOrders.Contains(Current.id.value) && Current.waypoint == Before[Index].waypoint &&
            Current.route == Before[Index].route)
            ++Preserved;
    }

    UE_LOG(LogRegimentMission, Display, TEXT("RegimentMission sighted: flags=%d revised=%d preserved=%d"),
        PlayerOrders.Num(), int32(bRevised), Preserved);
    bMarchResumed = PlayerOrders.Num() == MissionRegimentsPerTeam && bRevised && Preserved == MissionRegimentsPerTeam - 1;
    if (!bMarchResumed)
        FPlatformMisc::RequestExitWithStatus(false, 1);
}

void FRegimentMissionRuntime::TickMarch(float DeltaSeconds)
{
    auto* Controller = Owner.GetWorld()->GetFirstPlayerController();
    if (!bSmoke)
        AttachHud(Controller);
    if (DirectOpening)
    {
        TacticalScenario = BuildPresetBattle(Preset, *DirectOpening, EMissionDeployment::AuthoredStart);
        DirectOpening.reset();
        return;
    }
    if (!bMapCameraApplied && Controller && Board.GetOverviewCamera())
    {
        Controller->SetViewTarget(Board.GetOverviewCamera());
        bMapCameraApplied = true;
    }
    if (!March)
        return;
    if (Controller && Controller->WasInputKeyJustPressed(EKeys::Escape))
    {
        ReturnToSetup();
        return;
    }
    if (Controller)
        HandleMapInput(*Controller);

    if (March->EnemyFound() && !bMarchResumed)
        Accumulator = 0.0;
    Accumulator += DeltaSeconds * (bSmoke ? 12.0 : 1.0);
    for (int32 Steps = 0; Steps < 16 && Accumulator >= FRegimentBattlePlayback::StepSeconds &&
         !March->Encounter() && (!March->EnemyFound() || bMarchResumed); ++Steps)
    {
        March->Step(float(FRegimentBattlePlayback::StepSeconds));
        Accumulator -= FRegimentBattlePlayback::StepSeconds;
        if (bSmoke && March->EnemyFound() && !bMarchResumed)
            ExerciseSmokeWaypoints();
    }
    PresentMarch();
    if (const auto& Encounter = March->Encounter())
    {
        Contact = PointOnBoard(Encounter->center, Origin, Preset);
        TacticalScenario = BuildPresetBattle(Preset, March->Regiments(), EMissionDeployment::MarchContact);
        if (bSmoke)
            UE_LOG(LogRegimentMission, Display, TEXT("RegimentMission encounter: first=%llu second=%llu"),
                static_cast<unsigned long long>(Encounter->first.value), static_cast<unsigned long long>(Encounter->second.value));
    }
}

void FRegimentMissionRuntime::BeginTactical()
{
    Board.ClearMarkers();
    RemoveHud();
    FVector Focus = Contact;
    double CameraScale = 1.0;
    double Pitch = -36.0;
    if (Preset.StartsInBattle() && TacticalScenario)
    {
        FBox DeploymentBounds(ForceInit);
        for (const auto& Regiment : TacticalScenario->regiments)
            DeploymentBounds += PointOnBoard(Regiment.pose.position, Origin, Preset);
        Focus = DeploymentBounds.GetCenter();
        const auto Size = DeploymentBounds.GetSize();
        CameraScale = FMath::Max3(1.4, (Size.X + 1400.0) / 2600.0, (Size.Y + 1100.0) / 1300.0);
        Pitch = -38.7;
    }
    const bool bCoastalOverview = Preset.Terrain == battle::MarchTerrain::Thermopylae && Preset.StartsInBattle();
    FVector CameraOffset(0, -2000, 1600);
    if (bCoastalOverview)
    {
        // Open on the passage between the coastline and the mountain face.
        Focus.Y += 20.0 * Preset.Field.HalfHeight();
        CameraOffset = FVector(0, -2700, 1570);
        Pitch = -29.5;
    }
    auto* Camera = Owner.GetWorld()->SpawnActor<ACameraActor>(
        Focus + CameraOffset * CameraScale, FRotator(Pitch, 90, 0));
    if (!Camera)
        return;
    TacticalCamera = Camera;
    auto* View = Camera->GetCameraComponent();
    View->SetFieldOfView(bCoastalOverview ? 70.f : 58.f);
    ApplyRegimentBattleCameraExposure(*View);
    View->PostProcessSettings.bOverride_MotionBlurAmount = true;
    View->PostProcessSettings.MotionBlurAmount = 0.f;
    if (auto* Controller = Owner.GetWorld()->GetFirstPlayerController())
        Controller->SetViewTarget(Camera);
}

void FRegimentMissionRuntime::FollowRegiment(uint64 Regiment, int32 StartingStrength)
{
    const auto* Camera = TacticalCamera.Get();
    if (!Camera || !Regiment || Regiment == FollowedRegiment)
        return;
    StopMapFlyover();
    FollowedRegiment = Regiment;
    FollowCamera.Frame(Camera->GetActorRotation().Yaw, StartingStrength);
}

bool FRegimentMissionRuntime::TickCamera(float DeltaSeconds)
{
    auto* Controller = Owner.GetWorld()->GetFirstPlayerController();
    auto* Camera = TacticalCamera.Get();
    if (!Camera || !Controller)
        return false;
    if (Controller->WasInputKeyJustPressed(EKeys::Escape))
        return true;

    const bool bLooking = Controller->IsInputKeyDown(EKeys::RightMouseButton);
    auto Rotation = Camera->GetActorRotation();
    if (bLooking)
    {
        float X = 0.f, Y = 0.f;
        Controller->GetInputMouseDelta(X, Y);
        Rotation.Yaw = FMath::UnwindDegrees(Rotation.Yaw + X * .18f);
        Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Y * .18f, -88.0, 65.0);
    }
    const auto Axis = [Controller](FKey Positive, FKey Negative)
    {
        return int32(Controller->IsInputKeyDown(Positive)) - int32(Controller->IsInputKeyDown(Negative));
    };
    const FRotator Yaw(0, Rotation.Yaw, 0);
    const auto Move = Yaw.Vector() * Axis(EKeys::W, EKeys::S)
        + FRotationMatrix(Yaw).GetScaledAxis(EAxis::Y) * Axis(EKeys::D, EKeys::A)
        + FVector::UpVector * Axis(EKeys::E, EKeys::Q);
    const auto Hill = battle::MarchHill(Preset.Terrain, Preset.Field);
    const FVector Current = Camera->GetActorLocation();
    const bool bManual = bLooking || !Move.IsNearlyZero() || Controller->WasInputKeyJustPressed(EKeys::G);
    float WheelSteps = Controller->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
    if (FMath::IsNearlyZero(WheelSteps))
        WheelSteps = float(Controller->WasInputKeyJustPressed(EKeys::MouseScrollUp)) -
            float(Controller->WasInputKeyJustPressed(EKeys::MouseScrollDown));

    if (Controller->WasInputKeyJustPressed(EKeys::V))
    {
        if (MapFlight.IsActive())
            MapFlight.Stop();
        else
        {
            StopMapFlyover();
            MapFlight.Start(Current, Rotation, Origin, Preset.Field);
        }
    }
    if (bManual)
    {
        MapFlight.Stop();
        StopFollowingRegiment();
        CameraZoomTarget.Reset();
    }
    if (FollowedRegiment)
    {
        const auto* Battle = Cast<ARegimentBattleField>(&Owner);
        const auto Cards = Battle ? Battle->GetRegimentCards() : TConstArrayView<FRegimentFieldCard>();
        const auto* Card = Cards.FindByPredicate([this](const auto& Row)
        {
            return Row.Id == FollowedRegiment && Row.Living > 0;
        });
        if (Card)
        {
            if (!FMath::IsNearlyZero(WheelSteps))
                FollowCamera.Zoom(WheelSteps);
            const auto Pose = FollowCamera.Advance({Current, Rotation}, Card->Location - FVector(0, 0, 230),
                DeltaSeconds, Origin, Preset.Field, Hill);
            Camera->SetActorLocationAndRotation(Pose.Location, Pose.Rotation);
            return false;
        }
        StopFollowingRegiment();
    }
    if (!bManual && !FMath::IsNearlyZero(WheelSteps))
    {
        MapFlight.Stop();
        CameraVelocity = FVector::ZeroVector;
        const auto PickFieldPoint = [&](const FVector& Start, const FVector& Direction) -> TOptional<FVector>
        {
            const auto Point = RegimentGroundPoint(Start, Direction, Origin, Hill);
            if (Point && FMath::Abs(Point->X - Origin.X) <= Preset.Field.HalfWidth() * Cm &&
                FMath::Abs(Point->Y - Origin.Y) <= Preset.Field.HalfHeight() * Cm)
                return Point;
            return {};
        };
        FVector RayOrigin;
        FVector RayDirection;
        auto Anchor = Controller->DeprojectMousePositionToWorld(RayOrigin, RayDirection)
            ? PickFieldPoint(RayOrigin, RayDirection) : TOptional<FVector>();
        if (!Anchor)
            Anchor = PickFieldPoint(Current, Rotation.Vector());
        if (!Anchor)
            Anchor = RegimentGroundPoint(Current, -FVector::UpVector, Origin, Hill);
        if (Anchor)
            CameraZoomTarget = RegimentCameraZoomTarget(CameraZoomTarget.Get(Current), *Anchor, WheelSteps,
                Origin, Preset.Field, Hill);
    }
    if (MapFlight.IsActive())
    {
        const auto View = MapFlight.Advance(DeltaSeconds);
        Camera->SetActorLocationAndRotation(RegimentFlightCameraLocation(View.Location, Origin, Preset.Field, Hill),
            View.Rotation);
        return false;
    }

    const float MotionSeconds = FMath::Min(DeltaSeconds, .1f);
    if (CameraZoomTarget)
    {
        const auto Location = RegimentFlightCameraLocation(
            FMath::VInterpTo(Current, *CameraZoomTarget, MotionSeconds, 15.f), Origin, Preset.Field, Hill);
        if (Location.Equals(*CameraZoomTarget, .5))
        {
            Camera->SetActorLocation(*CameraZoomTarget);
            CameraZoomTarget.Reset();
        }
        else
            Camera->SetActorLocation(Location);
        return false;
    }
    const bool bBoost = Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift);
    const double Speed = FMath::Clamp(750.0 + .22 * (Current.Z - Origin.Z), 1000.0, 8000.0);
    const FVector DesiredVelocity = Move.GetClampedToMaxSize(1.0) * Speed * (bBoost ? 3.0 : 1.0);
    CameraVelocity = FMath::VInterpTo(CameraVelocity, DesiredVelocity, MotionSeconds, 6.0);
    const auto Location = RegimentFlightCameraLocation(Current + CameraVelocity * MotionSeconds, Origin, Preset.Field, Hill);
    if (!Location.Equals(Current, .01) || !Rotation.Equals(Camera->GetActorRotation(), .01))
        Camera->SetActorLocationAndRotation(Location, Rotation);
    return false;
}
