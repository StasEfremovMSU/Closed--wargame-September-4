#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MassEntityTypes.h"
#include "RegimentOfficerInput.h"
#include "UObject/StrongObjectPtr.h"
#include "RegimentBattleField.generated.h"

class ACameraActor;
class APlayerController;
class SWidget;
class UInstancedStaticMeshComponent;
class ULocalPlayer;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;
class UStaticMeshComponent;
enum class ERegimentBattleSpeed : uint8;
struct FRegimentBattleSession;
struct FRegimentMissionRuntime;
struct FRegimentBattleNotice;

enum class ERegimentBattleStatus : uint8
{
    Preparing, FormingUp, Running, BlueWon, OrangeWon, Draw, Failed
};

struct FRegimentBattleHudState
{
    FText BlueFaction;
    FText OrangeFaction;
    int32 BlueAlive = 0;
    int32 OrangeAlive = 0;
    int32 BlueStarting = 0;
    int32 OrangeStarting = 0;
    uint64 ElapsedSeconds = 0;
    ERegimentBattleStatus Status = ERegimentBattleStatus::Preparing;
};

enum class ERegimentPlayerOrder : uint8
{
    Line, Column, Hold, Attack, Withdraw, Skirmishers, Follow, NextRegiment, Release,
    Maneuver, GroupManeuver, Pace, HoldLine, Brace, Pursue, Cover, FireSector, CommandHold, WingHold,
    Wedge, Square, Testudo, Circle
};

constexpr uint8 RegimentSpecialFormationBit(ERegimentPlayerOrder Order)
{
    switch (Order)
    {
    case ERegimentPlayerOrder::Wedge: return 1;
    case ERegimentPlayerOrder::Square: return 2;
    case ERegimentPlayerOrder::Testudo: return 4;
    case ERegimentPlayerOrder::Circle: return 8;
    default: return 0;
    }
}

enum class ERegimentFieldLineKind : uint8 { Route, Blocked, Front, Fire, FireBlocked, FireFriendly, FireMelee, FireRisk };

struct FRegimentFieldLine
{
    FVector From;
    FVector To;
    ERegimentFieldLineKind Kind;
};

struct FRegimentFieldCard
{
    uint64 Id = 0;
    uint64 Command = 0;
    FText Name;
    FText Activity;
    FText FlagState;
    FVector Location = FVector::ZeroVector;
    int32 Living = 0;
    int32 Starting = 0;
    int32 Morale = 0;
    int32 Fatigue = 0;
    int32 Ammo = 0;
    bool bCanEnterOfficer = false;
    bool bInCommand = false;
    bool bFiniteAmmo = false;
    bool bDisordered = false;
    bool bContact = false;
};

enum class ERegimentFollowStatus : uint8 { Off, Active, Melee, Panic, Deploying };
enum class ERegimentOrderDelivery : uint8
{
    None, Queued, InTransit, Paused, Executing, Complete, Unapplied, AwaitingCourier, Delivered, Blocked
};

struct FRegimentOrderPresentation
{
    ERegimentFollowStatus Follow = ERegimentFollowStatus::Off;
    ERegimentOrderDelivery Delivery = ERegimentOrderDelivery::None;
    uint64 OrderId = 0;
    bool bCourier = false;
    FText Recipient;
    FText Action;
    FText OrderDetail;
    FText Activity;
};

struct FRegimentOfficerHudState
{
    bool bActive = false;
    bool bCanPossess = false;
    bool bCanCommand = false;
    bool bAdditionalRegiments = false;
    bool bPlanning = false;
    bool bFirstPerson = true;
    bool bAttackReady = true;
    bool bParryReady = true;
    bool bParryActive = false;
    TOptional<float> HealthFraction;
    FText Pace;
    FRegimentOrderPresentation Order;
    FText Officer;
    FText CommandTitle;
    FText CommandScope;
    FText ProfileEffects;
    FText TacticalSkill;
    FText FieldGunStatus;
    FText RankIcon;
    TWeakObjectPtr<class UTexture2D> Portrait;
    int32 CommandTier = 1;
    FText Regiment;
    FText RegimentCondition;
    FText Formation;
    uint8 SpecialFormations = 0;
    uint8 ActiveSpecialFormation = 0;
    FText Notice;
};

struct FRegimentDecisionHudEntry
{
    uint64 Node = 0;
    uint64 Parent = 0;
    uint64 Regiment = 0;
    uint64 Commander = 0;
    int32 Team = 0;
    int32 Skirmishers = 0;
    int32 MeleeRegiments = 0;
    bool bGeneral = false;
    bool bPreferOrderState = false;
    bool bCanDetach = false;
    bool bCanRally = false;
    bool bSkirmishersEnabled = false;
    bool bSkirmisherOrderQueued = false;
    FText Name;
    FText CommanderName;
    FText Duty;
    FText Effects;
    FText TacticalSkill;
    FString RankIcon;
    TWeakObjectPtr<class UTexture2D> Portrait;
    int32 CommandTier = 1;
    FText State;
    FText Action;
    FText Contact;
    FText Intel;
    FText Detail;
};

struct FRegimentBattleSessionDeleter
{
    void operator()(FRegimentBattleSession* Session) const;
};
struct FRegimentMissionDeleter
{
    void operator()(FRegimentMissionRuntime* Mission) const;
};

USTRUCT()
struct FRegimentBattleUnitFragment : public FMassFragment
{
    GENERATED_BODY()

    uint64 CoreUnitId = 0;
    uint64 CoreRegimentId = 0;
};

UCLASS()
class REGIMENTBATTLERUNTIME_API ARegimentBattleField : public AActor
{
    GENERATED_BODY()

public:
    ARegimentBattleField();
    virtual ~ARegimentBattleField() override;
    virtual void Tick(float DeltaSeconds) override;

    const FRegimentBattleHudState& GetHudState() const { return HudState; }
    TConstArrayView<FRegimentBattleNotice> GetBattleNotices() const;
    TConstArrayView<FRegimentFieldLine> GetFieldLines() const;
    TConstArrayView<FRegimentFieldCard> GetRegimentCards() const;
    uint64 GetSelectedRegimentCard() const { return SelectedOfficerRegiment; }
    void SelectRegimentCard(uint64 Id);
    void EnterRegimentCardOfficer(uint64 Id);
    bool IsCardOfficerQueued(uint64 Id) const;
    uint64 GetBattleNoticeTick() const;
    bool IsBattlePaused() const;
    ERegimentBattleSpeed GetBattleSpeed() const;
    void ToggleBattlePause();
    void SetBattleSpeed(ERegimentBattleSpeed Speed);
    void RequestBattleRestart();
    void RequestMissionSetup();
    bool CanReturnToMissionSetup() const;
    bool HasMissionReserve() const;
    bool CanIntroduceReserve() const;
    void IntroduceReserve();
    FText GetMissionObjectiveText() const;
    FText GetMissionReport() const;
    bool GetMissionMarker(FVector& Center, TArray<FVector>& Outline, FLinearColor& Tint) const;
    TConstArrayView<FRegimentDecisionHudEntry> GetDecisionEntries() const;
    bool ProjectCommander(uint64 UnitId, FVector2D& Normalized) const;
    void ToggleSkirmishers(uint64 NodeId);
    void RallyRegiment(uint64 NodeId);
    const FRegimentOfficerHudState& GetOfficerControlState() const { return OfficerHudState; }
    const FRegimentDecisionHudEntry* GetPendingOfficerPreview() const;
    bool OfficerCommandsOpen() const { return OfficerInput.bCommandsOpen; }
    void ToggleOfficerCommands();
    void ToggleOfficerView();
    bool CanPossessOfficer(uint64 NodeId) const;
    void PossessOfficer(uint64 NodeId = 0);
    bool IsChoosingOfficer() const;
    void OpenOfficerPicker(uint64 FocusNode = 0);
    uint32 GetOfficerPickerRevision() const { return OfficerPickerRevision; }
    uint64 GetPreferredOfficerNode() const { return PreferredOfficerNode; }
    void CloseOfficerPicker();
    void ChooseOfficer(uint64 NodeId);
    void IssueOfficerOrder(ERegimentPlayerOrder Order, bool bAimAtCursor = true);

    UPROPERTY(EditInstanceOnly, Category = "Battle")
    TObjectPtr<ACameraActor> OverviewCamera;

    UPROPERTY(EditInstanceOnly, Category = "Battle")
    TObjectPtr<USkeletalMesh> SwordsmanMesh;

    UPROPERTY(EditInstanceOnly, Category = "Battle")
    TObjectPtr<USkeletalMesh> ArcherMesh;

    UPROPERTY(EditInstanceOnly, Category = "Battle")
    TObjectPtr<USkeletalMesh> CaptainMesh;

    UPROPERTY(EditAnywhere, Category = "Battle|Figures")
    TObjectPtr<UStaticMesh> SwordsmanWeapon;

    UPROPERTY(EditAnywhere, Category = "Battle|Figures")
    TObjectPtr<UStaticMesh> CaptainWeapon;

    UPROPERTY(EditAnywhere, Category = "Battle|Figures")
    TObjectPtr<USkeletalMesh> ArcherWeapon;

    UPROPERTY(EditAnywhere, Category = "Battle|Figures")
    TObjectPtr<UMaterialInterface> BlueFigureMaterial;

    UPROPERTY(EditAnywhere, Category = "Battle|Figures")
    TObjectPtr<UMaterialInterface> OrangeFigureMaterial;

    UPROPERTY(EditAnywhere, Category = "Battle|Figures")
    TObjectPtr<UMaterialInterface> WeaponMaterial;

    UPROPERTY(VisibleAnywhere, Category = "Battle")
    TObjectPtr<UInstancedStaticMeshComponent> BlueMarkers;

    UPROPERTY(VisibleAnywhere, Category = "Battle")
    TObjectPtr<UInstancedStaticMeshComponent> OrangeMarkers;

    UPROPERTY(VisibleAnywhere, Category = "Battle|Figures")
    TObjectPtr<UInstancedStaticMeshComponent> ArrowVisuals;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    bool StartBattle();
    void ClearBattle();
    void AttachHud();
    void RemoveHud();
    FVector GetBattleOrigin() const;
    bool PresentFrame();
    void RefreshDecisionEntries();
    void ProcessOfficerInput();
    void ShowOfficerNotice(FText Notice);
    void QueueOfficerMotion();
    void UpdateOfficerCamera(float DeltaSeconds);
    void UpdateOfficerViewBlade(ACameraActor& Camera, double Swing, bool bParrying);
    void UpdateManeuverInput(APlayerController& Controller);
    void RefreshRegimentCards();
    void RestoreOfficerCamera();
    void SetOfficerMouseCapture(bool bCapture);
    bool CanDirectControlOfficer() const;
    void ClearOfficerDirectInput();
    void QueueOfficerActions(float AimYaw, bool bDirect);
    int32 CountFigures() const;
    int32 CountValidEntities() const;
    int32 CountMatchingPoses() const;
    FTransform ReadUnitTransform(uint64 UnitId) const;

    TUniquePtr<FRegimentBattleSession, FRegimentBattleSessionDeleter> Session;
    TUniquePtr<FRegimentMissionRuntime, FRegimentMissionDeleter> Mission;
    TOptional<uint64> PresentedTick;
    FRegimentBattleHudState HudState;
    FRegimentOfficerHudState OfficerHudState;
    uint32 OfficerPickerRevision = 0;
    uint64 PreferredOfficerNode = 0;
    uint64 PossessedOfficer = 0;
    uint64 SelectedOfficerRegiment = 0;
    float OfficerNoticeSeconds = 0.f;
    TWeakObjectPtr<ACameraActor> OfficerCamera;
    TWeakObjectPtr<UStaticMeshComponent> OfficerViewBlade;
    TWeakObjectPtr<AActor> PreviousBattleView;
    FIntVector OfficerMotionTap{0, 0, 0};
    FRegimentOfficerInput OfficerInput;
    bool bOfficerMouseCaptured = false;
    bool bOfficerRunTap = false;
    bool bOfficerPresentationDirty = false;
    double OfficerCameraYaw = 0.0;
    double OfficerCameraPitch = -13.0;
    double OfficerCameraDistance = 365.0;
    double OfficerCameraZoom = 365.0;
    FVector OfficerCameraPivot = FVector::ZeroVector;
    TSharedPtr<SWidget> BattleHud;
    TWeakObjectPtr<ULocalPlayer> HudPlayer;
    // Runtime rendering resource, outside the serialized battlefield actor layout.
    TStrongObjectPtr<UMaterialInterface> FigurePartMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> BlueMarkerMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> OrangeMarkerMaterial;
    UPROPERTY()
    TObjectPtr<UStaticMesh> StaffBlock;
    UPROPERTY()
    TObjectPtr<UStaticMesh> StaffCylinder;
    UPROPERTY()
    TObjectPtr<UStaticMesh> SpearPoint;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMesh> ProjectileSphere;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> TimberMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> FactionMaterials[2];

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> AncientFigureMaterials[2];
    bool bRestartRequested = false;
    bool bSetupRequested = false;
    bool bCameraApplied = false;
    bool bSmokeRun = false;
};
