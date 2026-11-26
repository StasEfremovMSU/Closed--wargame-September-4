#include "RegimentBattleField.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/BitArray.h"
#include "ConvexVolume.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Mass/EntityFragments.h"
#include "MassCommandBuffer.h"
#include "MassCommands.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "RegimentBattleOpening.h"
#include "RegimentBattleTrace.h"
#include "RegimentBattleGround.h"
#include "RegimentAncientFigures.h"
#include "RegimentEraCatalog.h"
#include "RegimentHillPresentation.h"
#include "RegimentBattleNotices.h"
#include "RegimentFieldFeedback.h"
#include "RegimentBattleAudio.h"
#include "RegimentSmokePresentation.h"
#include "RegimentMissionRuntime.h"
#include "RegimentMissionScenario.h"
#include "RegimentMissionProgress.h"
#include "RegimentBattlePlayback.h"
#include "RegimentFigureAppearance.h"
#include "RegimentFigureRig.h"
#include "RegimentFigureEquipment.h"
#include "RegimentFigureLod.h"
#include "RegimentFallenPresentation.h"
#include "RegimentArtilleryCameraKick.h"
#include "RegimentInstanceUpdates.h"
#include "RegimentOfficerOrders.h"
#include "RegimentCommanderProfiles.h"
#include "Engine/Texture2D.h"
#include "RegimentTacticalKnowledge.h"
#include "RegimentOfficerSettings.h"
#include "RegimentOfficerView.h"
#include "RegimentPerfCapture.h"
#include "SceneView.h"
#include "SRegimentBattleHud.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Simulation.h"
#include "battle/Snapshot.h"
#include "battle/Terrain.h"
#include "battle/command/GroupPolicy.h"
#include "battle/command/Services.h"
#include <sstream>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogRegimentBattle, Log, All);

#define LOCTEXT_NAMESPACE "RegimentBattle"

namespace
{
constexpr double OfficerDefaultCameraPitch = -13.0;
constexpr double OfficerDefaultCameraDistance = 365.0;

bool ChangesControlledUnit(const battle::CommandEnvelope& Entry)
{
    return Entry.command.kind == battle::CommandKind::AcquireControl ||
           Entry.command.kind == battle::CommandKind::ReleaseControl;
}

FText StrikeDirection(battle::AttackAspect Aspect)
{
    switch (Aspect)
    {
        case battle::AttackAspect::Flank: return LOCTEXT("StrikeFlank", "Удар во фланг");
        case battle::AttackAspect::Rear: return LOCTEXT("StrikeRear", "Удар в тыл");
        default: return LOCTEXT("StrikeFront", "Удар с фронта");
    }
}

std::optional<battle::command::GroupPolicy> RequestedGroupPolicy(std::string& LoadedWeights)
{
    FString Filename;
    if (!FParse::Value(FCommandLine::Get(), TEXT("RegimentGroupPolicy="), Filename))
        return std::nullopt;

    FString Contents;
    if (FFileHelper::LoadFileToString(Contents, *Filename))
    {
        std::string Weights(TCHAR_TO_UTF8(*Contents));
        std::istringstream Stream(Weights);
        if (auto Policy = battle::command::LoadGroupPolicy(Stream))
        {
            LoadedWeights = std::move(Weights);
            UE_LOG(LogRegimentBattle, Display, TEXT("Experimental group officer policy loaded: %s"), *Filename);
            return Policy;
        }
    }
    UE_LOG(LogRegimentBattle, Warning, TEXT("Group officer policy unavailable, using built-in rules: %s"), *Filename);
    return std::nullopt;
}

bool IsOutsideFormation(const battle::UnitState& Unit, const battle::RegimentState& Regiment)
{
    const auto Footprint = battle::Footprint(Regiment);
    const auto Relative = Unit.pose.position - Regiment.pose.position;
    return std::abs(battle::Dot(Relative, battle::Right(Regiment.pose.yaw))) > Footprint.width_m * .5f + .25f ||
           std::abs(battle::Dot(Relative, battle::Forward(Regiment.pose.yaw))) > Footprint.depth_m * .5f + .25f;
}

float FireSectorReach(const battle::World& World, const battle::RegimentState& Regiment)
{
    if (Regiment.panic || battle::IsDeploying(World, Regiment))
        return 0.f;
    float Reach = 0.f;
    for (auto Id : Regiment.units)
        if (const auto* Unit = battle::FindUnit(World, Id); Unit && Unit->alive && Unit->archer &&
            !Unit->dagger_drawn && (!Unit->ammunition || Unit->ammunition_remaining))
            Reach = FMath::Max(Reach, Unit->weapon.range);
    return Reach;
}

constexpr uint64 UnitsPerTeam = 50;
constexpr uint64 CommandLevels = 4;
constexpr uint64 FirstArcher = 40;
constexpr uint64 SmokeAdvanceTick = 100;
constexpr uint64 SmokeEndTick = 600;
constexpr double CentimetersPerMeter = 100.0;
constexpr double AttackPoseTicks = 10.0;
constexpr double HitPoseTicks = 10.0;
constexpr double CollapseTicks = 7.0;
constexpr double ArrowScale = 0.72;

enum ERegimentFigureRole : uint8
{
    Swordsman, Archer, Captain, FigureRoleCount
};

enum class ECommandFigure : uint8
{
    None, Commander, Officer, Standard, Musician, General, Count
};

FText DecisionState(battle::command::FsmState State)
{
    using battle::command::FsmState;
    switch (State)
    {
        case FsmState::Deploying: return LOCTEXT("Deploying", "Развёртывание");
        case FsmState::Holding: return LOCTEXT("Holding", "Удержание позиции");
        case FsmState::Advancing: return LOCTEXT("Advancing", "Выдвижение");
        case FsmState::Attacking: return LOCTEXT("Attacking", "Атака");
        case FsmState::Supporting: return LOCTEXT("Supporting", "Поддержка");
        case FsmState::Regrouping: return LOCTEXT("Regrouping", "Перестроение");
        case FsmState::Withdrawing: return LOCTEXT("Withdrawing", "Отход");
        case FsmState::Rallying: return LOCTEXT("Rallying", "Сбор");
        case FsmState::Reserve: return LOCTEXT("ReserveState", "Резерв");
        case FsmState::Failed: return LOCTEXT("FailedState", "Приказ сорван");
        case FsmState::Complete: return LOCTEXT("CompleteState", "Приказ выполнен");
        default: return LOCTEXT("IdleState", "Ожидание");
    }
}

FText DecisionAction(battle::command::TaskKind Action)
{
    using battle::command::TaskKind;
    switch (Action)
    {
        case TaskKind::MoveToArea: return LOCTEXT("MoveAction", "Выдвинуться к позиции");
        case TaskKind::Attack: return LOCTEXT("AttackAction", "Атаковать");
        case TaskKind::Defend: return LOCTEXT("DefendAction", "Оборонять позицию");
        case TaskKind::Screen: return LOCTEXT("ScreenAction", "Прикрыть");
        case TaskKind::Support: return LOCTEXT("SupportAction", "Поддержать");
        case TaskKind::Flank: return LOCTEXT("FlankAction", "Обойти с фланга");
        case TaskKind::Exploit: return LOCTEXT("ExploitAction", "Развить успех");
        case TaskKind::Reserve: return LOCTEXT("ReserveAction", "Остаться в резерве");
        case TaskKind::Withdraw: return LOCTEXT("WithdrawAction", "Отойти");
        case TaskKind::Rally: return LOCTEXT("RallyAction", "Собраться");
        case TaskKind::Reform: return LOCTEXT("ReformAction", "Перестроиться");
        default: return LOCTEXT("HoldAction", "Удерживать позицию");
    }
}

FText DecisionReason(const std::string& Reason)
{
    static const TPair<std::string_view, const TCHAR*> Translations[] = {
        {"Hold the battle line and protect the assigned sectors.", TEXT("Армия удерживает боевую линию и прикрывает свои участки.")},
        {"Army cannot execute without a commander and living parts.", TEXT("Армия лишилась командования или боеспособных групп.")},
        {"Army mission expired; parts return to a defensive posture.", TEXT("Срок задачи истёк. Группы переходят к обороне.")},
        {"Order an organized army withdrawal to the rally point.", TEXT("Генерал отводит группы к точке сбора.")},
        {"Assigned attack target is no longer visible; hold assigned sectors.", TEXT("Назначенная цель скрылась. Группы удерживают свои участки.")},
        {"Advance the assigned parts toward the visible battle objective.", TEXT("Генерал выдвигает группы к обнаруженной цели.")},
        {"Advance through assigned sectors to establish enemy contact.", TEXT("Генерал выдвигает группы по своим секторам на поиск противника.")},
        {"Hold position because no clear route reaches the search area.", TEXT("Группы удерживают позиции: проход к району поиска перекрыт.")},
        {"Attack the observed opposition in the assigned sectors.", TEXT("Группы атакуют обнаруженного противника каждая на своём участке.")},
        {"Center attacks while the wings take open flanks.", TEXT("Центр атакует, пока фланговые группы идут в обход.")},
        {"Center attacks with wings in support.", TEXT("Центр атакует при поддержке фланговых групп.")},
        {"Center attacks while the wings wait in reserve.", TEXT("Центр атакует. Фланговые группы держат резерв.")},
        {"Move the assigned parts to their army sectors.", TEXT("Генерал направляет группы на их участки.")},
        {"Coordinate support across the army sectors.", TEXT("Генерал координирует поддержку между группами.")},
        {"Keep reserve parts uncommitted behind the battle line.", TEXT("Резерв остаётся за боевой линией.")},
        {"Regroup the army before continuing the mission.", TEXT("Группы перестраиваются перед продолжением боя.")},
        {"Release the army reserve to support a threatened sector.", TEXT("Резерв направляется на помощь группе под угрозой.")},
        {"Hold pending orders.", TEXT("Офицер ждёт новых приказов.")},
        {"Coordinate assigned regiment positions.", TEXT("Командир распределяет позиции между полками.")},
        {"Deploy regiments before joint attack.", TEXT("Полки занимают позиции перед совместной атакой.")},
        {"Lead regiment attacks with coordinated support.", TEXT("Передовой полк атакует. Второй полк его поддерживает.")},
        {"Advance along the assigned flank.", TEXT("Группа выдвигается в назначенный обход.")},
        {"Lead regiment maintains its attack while support engages another observed enemy.",
            TEXT("Передовой полк продолжает атаку. Полк поддержки вступает в бой с другой обнаруженной целью.")},
        {"Lead regiment advances while support takes the open flank.",
            TEXT("Передовой полк выдвигается. Полк поддержки обходит противника с открытого фланга.")},
        {"Keep the supporting position while the flank is closed.",
            TEXT("Фланг закрыт. Полк остаётся на позиции поддержки.")},
        {"Hold the assigned supporting position.", TEXT("Полк удерживает позицию поддержки.")},
        {"Lead regiment pins the enemy. A second regiment takes the open flank.",
            TEXT("Передовой полк связывает противника боем. Второй полк идёт в обход.")},
        {"Move around the engaged line to the assigned flank.",
            TEXT("Полк обходит линию боя к назначенному флангу.")},
        {"Move to the observed enemy flank.", TEXT("Полк выдвигается к обнаруженному флангу противника.")},
        {"Strike the enemy from the assigned flank.", TEXT("Полк атакует противника с назначенного фланга.")},
        {"No passable flank. Keep the supporting position.",
            TEXT("Проходимый обход не найден. Полк держит позицию поддержки.")},
        {"Coordinate group withdrawal.", TEXT("Командир координирует отход обоих полков.")},
        {"Group cannot execute: missing force, commander, target or expired task.",
            TEXT("Группа не может выполнить приказ из-за состояния полков, цели, командира или срока приказа.")},
        {"Maintain committed decision.", TEXT("Офицер продолжает выполнять принятое решение.")},
        {"No live commander.", TEXT("Командир выбыл.")},
        {"Panic overrides ordinary orders.", TEXT("Полк потерял боевой дух и выходит из боя.")},
        {"Occupy assigned position.", TEXT("Полк выдвигается на назначенную позицию.")},
        {"Hold assigned position.", TEXT("Полк достиг назначенной позиции и удерживает её.")},
        {"Attack observed assigned enemy.", TEXT("Назначенная цель обнаружена.")},
        {"Authorized local opportunity.", TEXT("Офицер использует возможность атаковать обнаруженную цель.")},
        {"Move to assigned destination.", TEXT("Полк движется к назначенной позиции.")},
        {"Destination reached.", TEXT("Полк прибыл на указанную позицию.")},
        {"Support recipient unavailable.", TEXT("Полк, которому нужна поддержка, вне поля зрения.")},
        {"Target unavailable in known contacts.", TEXT("Цель не обнаружена.")},
        {"Investigate last known contact.", TEXT("Полк проверяет последнюю известную позицию цели.")},
        {"No target at last known position.", TEXT("На последней известной позиции противника нет.")},
        {"Regroup and watch for the assigned enemy.", TEXT("Полк собирает строй и ищет назначенную цель.")},
        {"Advance toward the last known enemy position.", TEXT("Полк движется к последней известной позиции противника.")},
        {"Contact restored. Resume the attack.", TEXT("Цель снова обнаружена. Полк возобновляет атаку.")},
        {"Assigned enemy is gone. Attack a nearby observed enemy.",
            TEXT("Назначенная цель выбыла. Полк атакует ближайшего обнаруженного противника.")},
        {"Task expired.", TEXT("Срок выполнения приказа истёк.")},
        {"Terminal task awaits replacement.", TEXT("Офицер ждёт следующую задачу.")},
        {"Rally point reached.", TEXT("Полк достиг точки сбора.")},
        {"Ordered withdrawal or rally.", TEXT("Офицер выполняет приказ на отход или сбор.")},
        {"Apply assigned formation.", TEXT("Полк принимает назначенный строй.")},
        {"Formation restored.", TEXT("Полк восстановил строй.")},
        {"Granted casualty limit reached.", TEXT("Потери достигли установленного предела.")},
    };
    for (const auto& Translation : Translations)
        if (Translation.Key == Reason)
            return FText::AsCultureInvariant(Translation.Value);
    return FText::AsCultureInvariant(UTF8_TO_TCHAR(Reason.c_str()));
}

FText BattlePoint(battle::Vec3 Point)
{
    return FText::AsCultureInvariant(FString::Printf(TEXT("%.1f × %.1f м"), Point.x, Point.y));
}

ERegimentBattleStatus BattleStatus(const battle::World& World)
{
    switch (World.outcome)
    {
        case battle::BattleOutcome::Victory:
            return World.winner_team == 0 ? ERegimentBattleStatus::BlueWon : ERegimentBattleStatus::OrangeWon;
        case battle::BattleOutcome::Draw:
            return ERegimentBattleStatus::Draw;
        default:
            return ERegimentBattleStatus::Running;
    }
}

bool IsBuiltEntity(const FMassEntityManager& Mass, FMassEntityHandle Handle)
{
    return Mass.IsEntityValid(Handle) && Mass.IsEntityBuilt(Handle);
}

battle::Scenario MakeBattle()
{
    namespace Command = battle::command;
    battle::Scenario Scenario;
    Scenario.rules.local_melee = true;
    Scenario.command_setup.emplace();
    auto& Setup = *Scenario.command_setup;
    for (uint64 Side = 0; Side < 2; ++Side)
    {
        battle::RegimentSpec Regiment;
        Regiment.id = { Side + 1 };
        Regiment.team = static_cast<int32>(Side);
        Regiment.pose = { { Side == 0 ? -17.f : 17.f, 0.f, 0.f },
                          Side == 0 ? 0.f : FMath::DegreesToRadians(165.f) };
        Regiment.rally_point = Regiment.pose.position;
        Regiment.formation = { 10, 1.1f, 0.25f };
        Regiment.speed = 3.f;
        Regiment.name = Side == 0 ? "Blue Company" : "Orange Company";
        Scenario.regiments.push_back(Regiment);
        for (uint64 Index = 0; Index < UnitsPerTeam; ++Index)
        {
            battle::UnitSpec Unit;
            Unit.id = { Side * UnitsPerTeam + Index + 1 };
            Unit.regiment = Regiment.id;
            Unit.pose = Regiment.pose;
            Unit.health = 18.f;
            Unit.speed = 4.f;
            Unit.archer = Index >= FirstArcher;
            Unit.weapon.damage = Unit.archer ? 2.f : 3.f;
            Unit.weapon.range = Unit.archer ? 16.f : 1.65f;
            Scenario.units.push_back(Unit);
        }

        for (uint64 Rank = 0; Rank < CommandLevels; ++Rank)
        {
            const uint64 Id = Side * CommandLevels + Rank + 1;
            Scenario.officers.push_back({ { Id }, { Side * UnitsPerTeam + Rank + 1 },
                                          Rank == 3 ? 2u : 1u });
            Command::CommandNode Node;
            Node.id = { Id };
            Node.level = static_cast<Command::Level>(Rank);
            Node.team = Regiment.team;
            Node.commander = { Id };
            if (Rank > 0)
                Node.parent = { Id - 1 };
            if (Rank == 3)
                Node.regiment = Regiment.id;
            else
                Node.children.push_back({ Id + 1 });
            Setup.hierarchy.nodes.push_back(Node);
            Command::OfficerProfile Profile;
            Profile.officer = { Id };
            Profile.command_radius_m = 65.f;
            Profile.discipline = 0.8f;
            Profile.tactical_skill = 0.7f;
            Setup.hierarchy.officers.push_back(Profile);
        }

        Command::CommandTask Mission;
        Mission.id = { Side + 1 };
        Mission.recipient = { Side * CommandLevels + 1 };
        // Advance in the authored formation so the angled front makes contact on one flank first.
        Mission.action = Command::TaskKind::MoveToArea;
        Mission.intent.objective_position = { Side == 0 ? 17.f : -17.f, 0.f, 0.f };
        Mission.status = Command::TaskStatus::Active;
        Setup.missions.push_back(Mission);
    }
    RegimentCommanderProfiles::Apply(Scenario, FMissionPreset{});
    return Scenario;
}

struct FHitVisual
{
    uint64 Tick;
    FVector LeanAxis;
};

struct FDeathVisual
{
    uint64 Tick;
    FVector LeanAxis;
    FVector GroundLocation;
};

struct FFigureComponents
{
    TWeakObjectPtr<UPoseableMeshComponent> Figure;
    TWeakObjectPtr<UMeshComponent> Weapon;
    TWeakObjectPtr<UStaticMeshComponent> Dagger;
    TWeakObjectPtr<UStaticMeshComponent> Insignia;
    TWeakObjectPtr<UStaticMeshComponent> Shield;
    TWeakObjectPtr<UInstancedStaticMeshComponent> Mount;

    void DestroyFigure() const
    {
        if (auto* Horse = Mount.Get())
            Horse->DestroyComponent();
        if (auto* Prop = Insignia.Get())
            Prop->DestroyComponent();
        if (auto* Cover = Shield.Get())
            Cover->DestroyComponent();
        if (auto* Blade = Dagger.Get())
            Blade->DestroyComponent();
        if (auto* Props = Weapon.Get())
            Props->DestroyComponent();
        if (auto* Body = Figure.Get())
            Body->DestroyComponent();
    }
};

struct FUnitView : FFigureComponents
{
    FMassEntityHandle Entity;
    battle::Pose LastMassPose{};
    double MeshScale = 1.0;
    double FeetOffset = 0.0;
    FVector LastLocation = FVector::ZeroVector;
    double Facing = 0.0;
    float StridePhase = 0.f;
    float IdlePhase = 0.f;
    FVector StandardHand = FVector::ZeroVector;
    FRegimentFigureAppearance Appearance{};
    bool bPoseInitialized = false;
    bool bPoseWasMoving = false;
    bool bDagger = false;
    bool bPlatformVisible = false;
    TOptional<FHitVisual> Hit;
    TOptional<FDeathVisual> Death;

    void ReleaseRichFigure()
    {
        DestroyFigure();
        static_cast<FFigureComponents&>(*this) = {};
        bPoseInitialized = bPoseWasMoving = bDagger = false;
    }

    double HitWeight(uint64 Tick) const
    {
        return Hit ? FMath::Max(0.0, 1.0 - double(Tick - Hit->Tick) / HitPoseTicks) : 0.0;
    }
};

struct FFallenFigure
{
    FFigureComponents Components;
    FDeathVisual Death;
    double Scale;
    FQuat UprightRotation;
    FQuat MountRotation;
    bool bSettled = false;
};

struct FAttackVisual
{
    uint64 StartTick;
    battle::UnitId TargetId;
    FVector Target;
    bool bProjectile;
    uint64 TelegraphImpactTick = 0;
};

struct FMeleeSmoke
{
    int32 Hits = 0;
    int32 Reactions = 0;
    int32 Deaths = 0;
    uint64 ReactionId = 0;
    uint64 DeathId = 0;
    FMassEntityHandle DeadEntity;
    TWeakObjectPtr<UPoseableMeshComponent> DeadModel;
    FVector DeathLocation = FVector::ZeroVector;
    TOptional<battle::Tick> FirstMeleeTick;
    bool bFirstMeleeOnFlank = true;
    bool bCollapsed = false;
    bool bExpired = false;
};

struct FArrowFlight
{
    FVector Start;
    FVector End;
    uint64 StartTick;
    uint64 EndTick;
    bool bHighArc = false;
    double HillClearanceApex = 0.0;
    battle::BattlePlatform Platform = battle::BattlePlatform::None;
    bool bArcane = false;

    FTransform At(uint64 Tick, double TipOffset) const
    {
        const double Alpha = FMath::Clamp(double(Tick - StartTick) / double(EndTick - StartTick), 0.0, 1.0);
        const FVector Span = End - Start;
        const bool bFieldGun = Platform == battle::BattlePlatform::FieldGun;
        const bool bBolt = Platform == battle::BattlePlatform::Ballista;
        const double Apex = bFieldGun || bBolt ? 0.0 : FMath::Max(HillClearanceApex,
            bHighArc ? FMath::Min(330.0, Span.Size() * 0.32) : FMath::Min(85.0, Span.Size() * 0.14));
        const FVector Location = Start + Span * Alpha + FVector::UpVector * (4.0 * Apex * Alpha * (1.0 - Alpha));
        if (Platform == battle::BattlePlatform::Catapult)
            return FTransform(FQuat::Identity, Location, FVector(.32));
        if (bFieldGun)
            return FTransform(FQuat::Identity, Location, FVector(.19));
        if (bArcane)
            return FTransform(FQuat::Identity, Location, FVector(.23));
        const FVector Direction = Span + FVector::UpVector * (4.0 * Apex * (1.0 - 2.0 * Alpha));
        const FVector Forward = Direction.GetSafeNormal();
        const FVector Scale = bBolt ? FVector(1.35, 2.23, 1.35) * ArrowScale : FVector(ArrowScale);
        return FTransform(FRotationMatrix::MakeFromY(-Forward).ToQuat(),
            Location - Forward * TipOffset * (Scale.Y / ArrowScale), Scale);
    }
};

constexpr int32 RichPromotionsPerFrame = 12;
constexpr int32 RichFallenLimit = 24;

struct FPresentationCamera
{
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    float FieldOfView = 90.f;
    bool bHasFrustum = false;
    FConvexVolume Frustum;

    bool Sees(const FVector& Center, float Radius) const
    {
        return !bHasFrustum || Frustum.IntersectSphere(Center, Radius);
    }
};

FPresentationCamera PlayerPresentationCamera(const APlayerController* Controller, bool bBuildFrustum)
{
    FPresentationCamera Camera;
    if (!Controller)
        return Camera;
    Controller->GetPlayerViewPoint(Camera.Location, Camera.Rotation);
    if (Controller->PlayerCameraManager)
        Camera.FieldOfView = Controller->PlayerCameraManager->GetFOVAngle();
    if (bBuildFrustum)
        if (const auto* Player = Controller->GetLocalPlayer(); Player && Player->ViewportClient && Player->ViewportClient->Viewport)
        {
            FSceneViewProjectionData Projection;
            if (Player->GetProjectionData(Player->ViewportClient->Viewport, Projection))
            {
                Camera.Location = Projection.ViewOrigin;
                GetViewFrustumBounds(Camera.Frustum, Projection.ComputeViewProjectionMatrix(), true);
                Camera.bHasFrustum = true;
            }
        }
    return Camera;
}

struct FInstanceBatch
{
    TArray<FTransform> Transforms;
    TArray<FTransform> Added;
    TArray<int32> Removed;
    TArray<float> PartData;
    TArray<float> PreviousPartData;
    TArray<FTransform> PreviousTransforms;
    TWeakObjectPtr<UInstancedStaticMeshComponent> PreviousMesh;
    FTransform PreviousMeshTransform;

    void AddPartData(const FLinearColor& Color, ERegimentFigureSurface Surface)
    {
        const float Data[] = {Color.R, Color.G, Color.B, float(Surface)};
        PartData.Append(Data, UE_ARRAY_COUNT(Data));
    }

    void PresentPartData(UInstancedStaticMeshComponent& Mesh, int32 Count, int32 Existing)
    {
        constexpr int32 Width = RegimentFigurePartDataCount;
        if (PartData.Num() != Count * Width || Mesh.NumCustomDataFloats != Width)
            return;

        int32 First = 0;
        int32 End = Count;
        if (PreviousMesh.Get() == &Mesh && PreviousPartData.Num() == Existing * Width)
        {
            const int32 Common = FMath::Min(Count, Existing);
            const auto Matches = [&](int32 Index)
            {
                return FMemory::Memcmp(PartData.GetData() + Index * Width,
                    PreviousPartData.GetData() + Index * Width, Width * sizeof(float)) == 0;
            };
            if (Common > 0 && FMemory::Memcmp(PartData.GetData(), PreviousPartData.GetData(),
                Common * Width * sizeof(float)) == 0)
            {
                First = Common;
            }
            else
            {
                while (First < Common && Matches(First))
                    ++First;
                if (Count <= Existing)
                    while (End > First && Matches(End - 1))
                        --End;
            }
        }
        if (First < End)
            Mesh.SetCustomData(First, End - 1,
                MakeArrayView<const float>(PartData.GetData() + First * Width, (End - First) * Width));
        Swap(PreviousPartData, PartData);
    }

    void Present(UInstancedStaticMeshComponent& Mesh)
    {
        const int32 Existing = Mesh.GetInstanceCount();
        const int32 Count = Transforms.Num();
        if (Count == 0)
        {
            if (Existing != 0)
                Mesh.ClearInstances();
            PreviousTransforms.Reset();
            PreviousPartData.Reset();
            return;
        }
        const FTransform MeshTransform = Mesh.GetComponentTransform();
        const FRegimentInstanceUpdateRange Update = PreviousMesh.Get() == &Mesh && PreviousTransforms.Num() == Existing
            && PreviousMeshTransform.Equals(MeshTransform, 0.0)
            ? RegimentChangedInstances(PreviousTransforms, Transforms)
            : FRegimentInstanceUpdateRange{0, FMath::Min(Count, Existing)};
        if (Update.Count != 0)
            Mesh.BatchUpdateInstancesTransforms(Update.First,
                MakeArrayView<const FTransform>(Transforms.GetData() + Update.First, Update.Count), true, false);
        if (Count > Existing)
        {
            Added.Reset();
            Added.Append(Transforms.GetData() + Existing, Count - Existing);
            Mesh.AddInstances(Added, false, true, false);
        }
        else if (Count < Existing)
        {
            Removed.Reset();
            for (int32 Index = Existing - 1; Index >= Count; --Index)
                Removed.Add(Index);
            Mesh.RemoveInstances(Removed, true);
        }
        if (!PartData.IsEmpty())
            PresentPartData(Mesh, Count, Existing);
        PreviousMesh = &Mesh;
        PreviousMeshTransform = MeshTransform;
        Swap(PreviousTransforms, Transforms);
    }
};

double ProxyFigureStature(ECommandFigure Command)
{
    return Command == ECommandFigure::General ? 1.14 : Command == ECommandFigure::Commander ? 1.08 : 1.0;
}

battle::RangedWeapon HistoricalHandProp(bool bAncient, ERegimentHistoricalFigure Style, const battle::UnitSpec& Unit)
{
    using W = battle::RangedWeapon;
    if (!bAncient)
        return W::None;
    if (Style == ERegimentHistoricalFigure::Numidian ||
        (Unit.ranged_attack && Unit.ranged_weapon == W::Javelin))
        return W::Javelin;
    if (!Unit.archer)
        return W::None;
    switch (Unit.ranged_weapon)
    {
    case W::Bow:
    case W::Sling:
    case W::Javelin:
        return Unit.ranged_weapon;
    default:
        return W::None;
    }
}

void AddTrainingTarget(FInstanceBatch& Batch, const FTransform& Transform)
{
    const auto Part = [&](FVector Position, FVector Size, const FLinearColor& Color)
    {
        Batch.Transforms.Emplace(Transform.GetRotation(), Transform.TransformPositionNoScale(Position), Size / 100.0);
        Batch.AddPartData(Color, ERegimentFigureSurface::Wood);
    };
    Part({0, 0, 56}, {12, 12, 112}, FLinearColor(.13f, .059f, .026f));
    Part({1, 0, 118}, {10, 70, 88}, FLinearColor(.44f, .26f, .12f));
    Part({7, 0, 118}, {3, 48, 48}, FLinearColor(.80f, .65f, .40f));
    Part({9, 0, 118}, {3, 25, 25}, FLinearColor(.54f, .078f, .056f));
}

void AddProxyFigure(FInstanceBatch& Batch, const FTransform& Transform, const battle::UnitState& Unit,
    ECommandFigure Command, ERegimentHistoricalFigure Dress, bool bBakedBody, bool bHandProp, bool bMusket,
    double PikeLength, double FigureScale, bool bRefreshedPolearm,
    const FRegimentEraTroop* MountedProfile, const FLinearColor& TeamColor,
    const FRegimentFigureAppearance& Appearance)
{
    const auto Part = [&](FVector Position, FVector Size, const FLinearColor& Color,
        ERegimentFigureSurface Surface, double Pitch = 0.0)
    {
        const FQuat Rotation = Transform.GetRotation() * FRotator(Pitch, 0.0, 0.0).Quaternion();
        Batch.Transforms.Emplace(Rotation, Transform.TransformPositionNoScale(Position * FigureScale),
            Size * (FigureScale / 100.0));
        Batch.AddPartData(Color, Surface);
    };
    constexpr auto Cloth = ERegimentFigureSurface::Cloth;
    constexpr auto Hide = ERegimentFigureSurface::Leather;
    constexpr auto Wood = ERegimentFigureSurface::Wood;
    constexpr auto Metal = ERegimentFigureSurface::Metal;
    const FLinearColor Uniform = TeamColor * Appearance.ClothShade;
    const FLinearColor Timber = FLinearColor(.15f, .078f, .039f) * Appearance.LeatherShade;
    const FLinearColor Steel(.24f, .26f, .28f);
    const bool bMounted = Unit.troop == battle::UnitTroop::Mounted;
    const double Height = bMounted ? (bBakedBody ? RegimentHistoricalRiderHeight : 105.0) : 0.0;
    const double Stature = ProxyFigureStature(Command);
    if (!bBakedBody)
    {
        Part({0, 0, 74 * Stature + Height}, {37, 48, 111 * Stature}, Uniform, Cloth);
        Part({0, 0, 145 * Stature + Height}, {29, 29, 35}, Appearance.Skin, Hide);
    }
    if (bMounted && !bBakedBody)
    {
        Part({0, 0, 72}, {122, 43, 83}, Appearance.Coat.Body, Hide);
        Part({64, 0, 106}, {53, 35, 43}, Appearance.Coat.Body * .74f + Appearance.Coat.Points * .26f,
            Hide, -22.0);
    }
    if (Command == ECommandFigure::Standard)
    {
        Part({-10, 8, 145 + Height}, {10, 10, 131}, Timber, Wood);
        Part({-10, 41, 189 + Height}, {14, 66, 50}, TeamColor, Cloth);
    }
    else if (Command == ECommandFigure::General)
        Part({-10, 38, 189 * Stature + Height}, {14, 66, 50}, TeamColor, Cloth);
    else if (Command == ECommandFigure::Musician)
        Part({27, 0, 100 + Height}, {31, 44, 32}, TeamColor, Cloth);
    else if (bHandProp)
        return;
    else if (Command == ECommandFigure::None && (bMusket || battle::UsesGunpowder(Unit.ranged_weapon)) &&
        !(Dress == ERegimentHistoricalFigure::SengokuArquebusier && Unit.dagger_drawn))
    {
        constexpr double Pitch = 12.0;
        const FVector Grip(12, 28, 96 + Height);
        const FVector Barrel = FRotator(Pitch, 0.0, 0.0).Vector();
        Part(Grip + Barrel * 39.0, {117, 12, 13}, Steel * .62f, Metal, Pitch);
        Part(Grip - Barrel * 21.0 - FVector(0, 0, 6), {38, 15, 18}, Timber, Wood, Pitch + 13.0);
    }
    else if (Command == ECommandFigure::None && RegimentShowsPolearm(Unit, bMusket, MountedProfile))
    {
        if (!bRefreshedPolearm)
        {
            if (bMounted)
            {
                const double Length = bBakedBody ? PikeLength : 214.0;
                Part({28, 33, 106 + Height}, {9, 9, Length}, Timber, Wood, 25.0);
            }
            else
                Part({20, 32, 123 + (PikeLength - 242) * .35}, {12, 12, PikeLength - 15}, Timber, Wood, 15.0);
            return;
        }
        const bool bLeveled = bMounted || PikeLength >= 370.0;
        const double Pitch = bMounted ? 25.0 : bLeveled ? 13.0 : 75.0;
        const FVector Axis = FRotator(Pitch, 0, 0).Vector();
        const FVector Grip(22, 32, 94.0 + Height);
        Part(Grip + Axis * (PikeLength * .24), {PikeLength, 7, 7}, Timber, Wood, Pitch);
        Part(Grip + Axis * (PikeLength * .74 + 9), {19, 11, 11}, Steel, Metal, Pitch);
    }
    else if (Unit.archer && !(Unit.dagger_drawn &&
        (Dress == ERegimentHistoricalFigure::SengokuArcher || Dress == ERegimentHistoricalFigure::SengokuArquebusier)))
        Part({25, 28, 94 + Height}, {14, 19, 83}, Timber, Wood, -29.0);
    else if (!bMounted || Command == ECommandFigure::None)
        Part({31, 26, 68 + Height}, {61, 14, 17}, Steel, Metal, 29.0);
}

template <typename ComponentType>
ComponentType* NewFigureComponent(AActor& Owner, USceneComponent& Parent)
{
    auto* Component = NewObject<ComponentType>(&Owner);
    Owner.AddInstanceComponent(Component);
    Component->SetupAttachment(&Parent);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->SetMobility(EComponentMobility::Movable);
    return Component;
}

struct FAncientMeshBatch
{
    TStrongObjectPtr<UStaticMesh> Mesh;
    int32 PaintSlot = INDEX_NONE;
    int32 CoatSlot = INDEX_NONE;
    FInstanceBatch Batches[2];
    TWeakObjectPtr<UInstancedStaticMeshComponent> Components[2];

    void Create(AActor& Owner, const FRegimentAncientFigure& Figure, UMaterialInterface* BaseMaterial,
        bool bTintHorse = false)
    {
        Mesh.Reset(MakeRegimentAncientMesh(Owner, Figure, BaseMaterial, PaintSlot, bTintHorse ? &CoatSlot : nullptr));
    }

    void Reset()
    {
        for (auto& Batch : Batches)
        {
            Batch.Transforms.Reset();
            Batch.PartData.Reset();
        }
    }

    void Present(AActor& Owner, USceneComponent& Parent, UMaterialInterface* Blue, UMaterialInterface* Orange,
        UMaterialInterface* CoatMaterial = nullptr)
    {
        if (!Mesh)
            return;
        for (int32 Side = 0; Side < 2; ++Side)
        {
            auto& Batch = Batches[Side];
            if (!Components[Side].IsValid() && !Batch.Transforms.IsEmpty())
            {
                auto* Component = NewFigureComponent<UInstancedStaticMeshComponent>(Owner, Parent);
                Component->SetStaticMesh(Mesh.Get());
                Component->SetCastShadow(false);
                if (auto* Paint = Side == 0 ? Blue : Orange; PaintSlot != INDEX_NONE && Paint)
                    Component->SetMaterial(PaintSlot, Paint);
                if (CoatSlot != INDEX_NONE && CoatMaterial)
                {
                    Component->SetNumCustomDataFloats(RegimentFigurePartDataCount);
                    Component->SetMaterial(CoatSlot, CoatMaterial);
                }
                Component->RegisterComponent();
                Component->SetComponentTickEnabled(false);
                Components[Side] = Component;
            }
            if (auto* Component = Components[Side].Get())
                Batch.Present(*Component);
        }
    }

    void Destroy() const
    {
        for (const auto& Component : Components)
            if (auto* Live = Component.Get())
                Live->DestroyComponent();
    }
};

UStaticMeshComponent* NewFigureProp(AActor& Owner, USceneComponent& Parent, UStaticMesh& Mesh,
    UMaterialInterface* Material)
{
    auto* Prop = NewFigureComponent<UStaticMeshComponent>(Owner, Parent);
    Prop->SetStaticMesh(&Mesh);
    if (Material)
        Prop->SetMaterial(0, Material);
    Prop->RegisterComponent();
    Prop->SetComponentTickEnabled(false);
    return Prop;
}

void AddPaintedPart(UInstancedStaticMeshComponent& Mesh, const FTransform& Transform,
    const FLinearColor& Color, ERegimentFigureSurface Surface)
{
    const int32 Index = Mesh.AddInstance(Transform);
    if (Mesh.NumCustomDataFloats == RegimentFigurePartDataCount)
    {
        const float Data[] = {Color.R, Color.G, Color.B, float(Surface)};
        Mesh.SetCustomData(Index, MakeArrayView(Data));
    }
}

UInstancedStaticMeshComponent* NewBayonetMusket(AActor& Owner, USceneComponent& Parent,
    UStaticMesh& Block, UMaterialInterface* PartMaterial, UMaterialInterface* WoodMaterial)
{
    auto* Musket = NewFigureComponent<UInstancedStaticMeshComponent>(Owner, Parent);
    Musket->SetStaticMesh(&Block);
    if (PartMaterial)
    {
        Musket->SetMaterial(0, PartMaterial);
        Musket->SetNumCustomDataFloats(RegimentFigurePartDataCount);
    }
    else if (WoodMaterial)
        Musket->SetMaterial(0, WoodMaterial);
    AddPaintedPart(*Musket, FTransform(FQuat::Identity, FVector(30, 0, 0), FVector(.88, .052, .063)),
        FLinearColor(.13f, .14f, .16f), ERegimentFigureSurface::Metal);
    AddPaintedPart(*Musket, FTransform(FRotator(13, 0, 0), FVector(-21, 0, -6), FVector(.34, .10, .16)),
        FLinearColor(.14f, .063f, .028f), ERegimentFigureSurface::Wood);
    Musket->RegisterComponent();
    Musket->SetComponentTickEnabled(false);
    return Musket;
}

UInstancedStaticMeshComponent* NewMount(AActor& Owner, USceneComponent& Parent,
    UStaticMesh& Block, UMaterialInterface* PartMaterial, UMaterialInterface* FallbackMaterial,
    const FRegimentHorseCoat& Coat)
{
    // Horse-local X is forward. The legs, neck and head remain one rendered component.
    auto* Horse = NewFigureComponent<UInstancedStaticMeshComponent>(Owner, Parent);
    Horse->SetStaticMesh(&Block);
    if (PartMaterial)
    {
        Horse->SetMaterial(0, PartMaterial);
        Horse->SetNumCustomDataFloats(RegimentFigurePartDataCount);
    }
    else if (FallbackMaterial)
        Horse->SetMaterial(0, FallbackMaterial);
    const auto AddPart = [&](FVector Position, FVector Size, double Pitch = 0.0, bool bPoints = false)
    {
        constexpr double LengthScale = .72;
        Position.X *= LengthScale;
        Size.X *= LengthScale;
        AddPaintedPart(*Horse, FTransform(FRotator(Pitch, 0.0, 0.0), Position, Size / 100.0),
            bPoints ? Coat.Points : Coat.Body, ERegimentFigureSurface::Leather);
    };
    AddPart({ -7, 0, 94 }, { 128, 50, 48 });
    AddPart({ -60, 0, 93 }, { 35, 54, 53 });
    AddPart({ 48, 0, 100 }, { 38, 41, 46 });
    AddPart({ 55, 0, 130 }, { 28, 32, 57 }, -25.0);
    AddPart({ 82, 0, 153 }, { 44, 30, 26 }, -12.0);
    AddPart({ 103, 0, 144 }, { 27, 26, 19 }, -12.0, true);
    for (double Side : { -1.0, 1.0 })
        AddPart({ 73, Side * 9.0, 173 }, { 10, 8, 18 }, 12.0);
    for (double X : { -61.0, 50.0 })
    for (double Y : { -17.0, 17.0 })
        AddPart({ X, Y, 39 }, { 14, 13, 80 }, X < 0.0 ? -7.0 : 7.0, true);
    AddPart({ -78, 0, 73 }, { 14, 18, 47 }, 24.0, true);
    Horse->RegisterComponent();
    Horse->SetComponentTickEnabled(false);
    return Horse;
}
}

struct FRegimentBattleSession
{
    explicit FRegimentBattleSession(battle::World&& InWorld, bool bInCampaign, battle::MarchField Field)
        : Simulation(std::move(InWorld)), bCampaign(bInCampaign)
    {
        if (bCampaign)
        {
            auto Grid = MakeMissionNavigation(Simulation.scenario.obstacles, Field);
            TraceGrid = Grid.Geometry();
            Navigation = MakeUnique<battle::GridNavigationService>(std::move(Grid));
        }
        else
        {
            Opening.Emplace(Simulation);
            Navigation = MakeUnique<battle::FlatNavigationService>();
        }
        for (const auto& Officer : Simulation.officers)
        {
            auto Figure = ECommandFigure::Officer;
            if (bCampaign && Simulation.command_state)
            {
                switch (Officer.role)
                {
                    case 1:
                    {
                        const auto& Nodes = Simulation.command_state->hierarchy.nodes;
                        const bool bArmyCommander = std::ranges::any_of(Nodes, [&](const auto& Node)
                        {
                            return Node.level == battle::command::Level::Army && Node.commander == Officer.id;
                        });
                        Figure = bArmyCommander ? ECommandFigure::General : ECommandFigure::Commander;
                        break;
                    }
                    case 3: Figure = ECommandFigure::Standard; break;
                    case 4: Figure = ECommandFigure::Musician; break;
                    default: break;
                }
            }
            OfficerUnits.Add(Officer.unit.value, Figure);
        }
        TMap<uint64, int32> MountedBalance;
        for (const auto& Unit : Simulation.units)
            if (Unit.regiment && !OfficerUnits.Contains(Unit.id.value) && !battle::IsSiegeEngine(Unit.platform) &&
                Unit.platform != battle::BattlePlatform::FieldGun)
            {
                const bool bMounted = Unit.troop == battle::UnitTroop::Mounted ||
                    Unit.platform == battle::BattlePlatform::ScythedChariot;
                MountedBalance.FindOrAdd(Unit.regiment.value) += bMounted ? 1 : -1;
            }
        for (const auto& Entry : MountedBalance)
            if (Entry.Value > 0)
                MountedAudioRegiments.Add(Entry.Key);
        PreviousFormationAudioPositions.Reserve(int32(Simulation.regiments.size()));
    }

    battle::World Simulation;
    const FRegimentEraFaction* Factions[2] = {};
    const FRegimentEraTroop* MountedProfiles[MissionRosterCapacity] = {};
    bool bAncient = false;
    TOptional<FRegimentBattleOpening> Opening;
    TUniquePtr<battle::NavigationService> Navigation;
    TMap<uint64, ECommandFigure> OfficerUnits;
    struct FRecentMelee
    {
        battle::RegimentId Opponent;
        battle::Tick Tick;
    };
    TMap<uint64, FRecentMelee> RecentMelee;
    struct FIncomingStrike
    {
        FRecentMelee Contact;
        battle::AttackAspect Aspect = battle::AttackAspect::Front;
    };
    TMap<uint64, FIncomingStrike> IncomingStrikes;
    TMap<uint64, int32> FriendlyHits;
    TArray<FRegimentDecisionHudEntry> DecisionEntries;
    TWeakObjectPtr<AActor> PickerPreviousView;
    TMap<uint64, RegimentCommanderProfiles::FCommanderCard> CommanderCards;
    RegimentOfficerOrders::FPlayerOrderJournal PlayerOrderJournal;
    FRegimentBattleNotices Notices;
    FRegimentFieldFeedback Feedback;
    FRegimentBattleAudio Audio;
    struct FAudioMotion
    {
        FVector Position = FVector::ZeroVector;
        bool bAirborne = false;
    };
    TMap<uint64, FVector> PreviousFormationAudioPositions;
    TSet<uint64> MountedAudioRegiments;
    FAudioMotion PreviousOfficerAudioMotion;
    uint64 AudioOfficer = 0;
    double OfficerStepDistance = 0.0;
    uint32 OfficerStepCount = 0;
    double NextAmbientUpdate = 0.0;
    TOptional<battle::Tick> LastMeleeAudioTick;
    TOptional<battle::Tick> LastGunpowderAudioTick;
    FRegimentSmokePresentation Smoke;
    TArray<FRegimentFieldLine> FieldLines;
    TArray<FRegimentFieldCard> FieldCards;
    TOptional<ERegimentPlayerOrder> PlotOrder;
    TOptional<battle::Vec3> DragAnchor;
    battle::MovementPace Pace = battle::MovementPace::Walk;
    double NextFieldRefresh = 0.0;
    bool bFireSector = false;
    battle::Tick SectorTick{};
    battle::RegimentId SectorRegiment{};
    std::vector<battle::FireSectorSegment> SectorSamples;
    std::vector<battle::CommandEnvelope> PendingCommands;
    FRegimentBattleTrace Trace;
    std::optional<battle::NavigationGridGeometry> TraceGrid;
    TOptional<FRegimentMissionProgress> MissionProgress;
    std::optional<battle::command::GroupPolicy> GroupPolicy;
    uint64 NextCommandSequence = 1;
    TOptional<battle::Tick> LastDecisionPresentation;
    bool bCampaign = false;
    uint64 SmokeSkirmisherNode = 0;
    TSet<uint64> SmokeFlankGroups;
    bool bSmokeRecalled = false;
    bool bSmokeDeployed = false;
    TMap<uint64, FUnitView> Units;
    TArray<FRegimentLodCandidate> LodCandidates;
    TBitArray<> VisibleUnits;
    TArray<FVector> PresentationLocations;
    TSet<uint64> RichUnitIds;
    TWeakObjectPtr<UInstancedStaticMeshComponent> ProxyMeshes[2];
    FInstanceBatch ProxyBatches[2];
    TStrongObjectPtr<UMaterialInstanceDynamic> FigureWood;
    TStrongObjectPtr<UMaterialInstanceDynamic> FigureSteel;
    FInstanceBatch ArrowBatch;
    FInstanceBatch StoneBatch;
    TWeakObjectPtr<UInstancedStaticMeshComponent> StoneVisuals;
    FInstanceBatch ArcaneBatch;
    TWeakObjectPtr<UInstancedStaticMeshComponent> ArcaneVisuals;
    TMap<battle::BattlePlatform, FAncientMeshBatch> Platforms;
    TMap<ERegimentAncientShield, FAncientMeshBatch> AncientShields;
    TMap<ERegimentEraVisual, FAncientMeshBatch> EraHeadgear;
    TArray<ERegimentHistoricalFigure> HistoricalFigures;
    TMap<ERegimentHistoricalFigure, FAncientMeshBatch> HistoricalDress;
    TMap<ERegimentHistoricalFigure, FAncientMeshBatch> HistoricalProxies;
    TMap<ERegimentHistoricalFigure, FAncientMeshBatch> HistoricalTack;
    TMap<battle::RangedWeapon, FAncientMeshBatch> LooseWeapons;
    TMap<ERegimentHistoricalFigure, FAncientMeshBatch> SengokuWeapons;
    FAncientMeshBatch SengokuBanners;
    FPresentationCamera LastPresentationCamera;
    double LastPresentationTime = 0.0;
    bool bRichPromotionPending = false;
    bool bQueueInitialPossession = false;
    bool bPerfFullDetail = false;
    bool bPerfBaselinePresentation = false;
    bool bPerfOfficerView = false;
    battle::UnitId PerfOfficer;
    TWeakObjectPtr<ACameraActor> PerfCamera;
    FTransform PerfOverview;
    TOptional<FRegimentPerfCapture> Perf;
    TMap<uint64, FMassEntityHandle> InitialHandles;
    TUniquePtr<FRegimentFigureRig> Rigs[FigureRoleCount];
    TMap<uint64, FAttackVisual> Attacks;
    TMap<uint64, FFallenFigure> Fallen;
    FRegimentFallenProxies FallenProxies[2];
    FInstanceBatch FallenBatches[2];
    TWeakObjectPtr<UInstancedStaticMeshComponent> FallenMeshes[2];
    TWeakObjectPtr<URegimentArtilleryCameraKick> ArtilleryKick;
    TOptional<FMeleeSmoke> MeleeSmoke;
    TArray<FArrowFlight> Arrows;
    FRegimentBattlePlayback Playback;
    FVector ReleasedBowGrip = FVector::ZeroVector;
    FVector LastMassOrigin = FVector::ZeroVector;
    int32 PresentedArrowCount = 0;
    double PostBattleTicks = 0.0;

    void QueueCommand(const battle::Command& Command)
    {
        PendingCommands.push_back({Simulation.tick, NextCommandSequence++, Command});
    }

    bool IsOpening() const { return Opening && Opening->IsActive(); }
    uint64 GetDisplayTick() const { return Simulation.tick + (Opening ? Opening->GetElapsedTicks() : 0); }
    ERegimentFigureRole FigureRole(const battle::UnitState& Unit) const
    {
        return OfficerUnits.Contains(Unit.id.value) ? Captain : Unit.archer ? Archer : Swordsman;
    }

    ECommandFigure CommandFigure(battle::UnitId Unit) const
    {
        return OfficerUnits.FindRef(Unit.value);
    }

    TOptional<ERegimentAncientShield> ShieldStyle(int32 Side, ERegimentHistoricalFigure Dress,
        bool bMounted, uint64 Id) const
    {
        const auto* Faction = Factions[Side];
        auto Style = Faction ? RegimentHistoricalShield(Dress, Faction->Shield) : RegimentHistoricalShield(Dress);
        if (!Style && bAncient && Faction && !bMounted && Dress == ERegimentHistoricalFigure::None)
            Style = RegimentAncientShieldStyle(Faction->Id);
        if (Style && bAncient && Faction)
            return RegimentPaintedShieldStyle(*Style, Faction->Id, Id);
        return Style;
    }

    double PikeLength(int32 Side, bool bMounted, ERegimentHistoricalFigure Dress) const
    {
        if (Dress != ERegimentHistoricalFigure::None)
            return RegimentHistoricalSpearLength(Dress);
        if (bMounted)
            return 218.0;
        const auto* Faction = Factions[Side];
        if (Faction && Faction->Visual == ERegimentEraVisual::PikeShot)
            return 310.0;
        return bAncient && Faction ? RegimentAncientPikeLength(Faction->Id) : 242.0;
    }

    const battle::TerrainHill* GetHill() const
    {
        const auto& Hill = Simulation.scenario.hill;
        return Hill ? &*Hill : nullptr;
    }

    FVector WorldLocation(const battle::Vec3& Position, const FVector& Origin) const
    {
        return RegimentTerrainLocation(Position, Origin, GetHill());
    }

    void PresentEffects(AActor& Owner, const FVector& Origin, const FPresentationCamera& Camera, bool bOfficerCamera)
    {
        FRegimentEffectView View{Camera.Location, Camera.bHasFrustum ? &Camera.Frustum : nullptr};
        const bool bOfficerView = IsScriptedPerf() ? PerfCamera.IsValid() && bPerfOfficerView : bOfficerCamera;
        if (bOfficerView)
        {
            const auto Unit = IsScriptedPerf() ? PerfOfficer : Simulation.controlled;
            if (const auto* Officer = battle::FindUnit(Simulation, Unit); Officer && Officer->alive)
                View.Observer = WorldLocation(Officer->pose.position, Origin);
        }
        Smoke.Update(Owner, Origin, Simulation, View, PostBattleTicks);
    }

    double TerrainZAt(const FVector& WorldPosition, const FVector& Origin) const
    {
        const FVector Point = (WorldPosition - Origin) / CentimetersPerMeter;
        return WorldLocation({float(Point.X), float(Point.Y), 0.f}, Origin).Z;
    }

    FVector LimitOfficerCamera(const FVector& Pivot, const FVector& Wanted, const FVector& Origin,
        battle::MarchTerrain Terrain) const
    {
        float Unobstructed = 1.f;
        for (const auto& Obstacle : Simulation.scenario.obstacles)
        {
            using battle::TerrainFeature;
            double Height;
            switch (Obstacle.feature)
            {
            case TerrainFeature::StoneWall:
                Height = Terrain == battle::MarchTerrain::StoneWalls ? 181.0
                    : Terrain == battle::MarchTerrain::Poltava ? 166.0 : 266.0;
                break;
            case TerrainFeature::Hedge: Height = 210.0; break;
            case TerrainFeature::RedoubtBank: Height = 172.0; break;
            case TerrainFeature::Barrier: Height = 360.0; break;
            default: continue;
            }
            const FVector AreaMin = Origin + FVector(Obstacle.min_x, Obstacle.min_y, 0.0) * CentimetersPerMeter;
            const FVector AreaMax = Origin + FVector(Obstacle.max_x, Obstacle.max_y, 0.0) * CentimetersPerMeter;
            const double GroundZ = TerrainZAt((AreaMin + AreaMax) * .5, Origin);
            const FBox Box(FVector(AreaMin.X, AreaMin.Y, GroundZ),
                FVector(AreaMax.X, AreaMax.Y, GroundZ + Height));
            FVector HitLocation, HitNormal;
            float HitTime;
            if (FMath::LineExtentBoxIntersection(Box, Pivot, Wanted, FVector(18.0), HitLocation, HitNormal, HitTime))
                Unobstructed = FMath::Min(Unobstructed, HitTime);
        }
        return FMath::Lerp(Pivot, Wanted, Unobstructed);
    }

    void LogOpeningCheckpoint() const
    {
        const uint32 Tick = Opening->GetElapsedTicks();
        if (Tick != FRegimentBattleOpening::ColumnTick && Tick != FRegimentBattleOpening::DurationTicks)
            return;

        double Width[2] = {};
        for (const auto& Regiment : Simulation.regiments)
        {
            const auto Right = battle::Right(Regiment.pose.yaw);
            auto& HalfWidth = Width[Regiment.id.value - 1];
            for (const auto Id : Regiment.units)
            {
                const auto* Unit = battle::FindUnit(Simulation, Id);
                const auto Offset = Unit->pose.position - Regiment.pose.position;
                HalfWidth = FMath::Max(HalfWidth, FMath::Abs(double(battle::Dot(Offset, Right))));
            }
        }
        UE_LOG(LogRegimentBattle, Display,
            TEXT("RegimentBattle opening: phase=%s coreTick=%llu width1=%.2f width2=%.2f"),
            Tick == FRegimentBattleOpening::ColumnTick ? TEXT("column") : TEXT("ready"),
            static_cast<unsigned long long>(Simulation.tick), 2 * Width[0], 2 * Width[1]);
    }

    void RecordCombatEvents(UWorld& World, const FVector& Origin)
    {
        Notices.Expire(Simulation.tick);
        for (const auto& Event : Simulation.events)
        {
            if (Simulation.scenario.rules.regiment_tactics && Event.kind == battle::EventKind::Hit &&
                Event.amount > 0.f && Event.regiment)
                if (const auto* Source = battle::FindUnit(Simulation, Event.source))
                {
                    if (!Event.projectile && Source->regiment)
                    {
                        auto& Latest = IncomingStrikes.FindOrAdd(Event.regiment.value);
                        if (!Latest.Contact.Opponent || Latest.Contact.Tick != Event.tick || Latest.Aspect <= Event.aspect)
                            Latest = {{Source->regiment, Event.tick}, Event.aspect};
                    }
                    else if (Event.projectile)
                    {
                        const auto* Target = battle::FindUnit(Simulation, Event.target);
                        if (Target && battle::UnitTeam(Simulation, *Source) == battle::UnitTeam(Simulation, *Target))
                            ++FriendlyHits.FindOrAdd(Event.regiment.value);
                    }
                }
            if (Event.kind == battle::EventKind::OfficerSucceeded || Event.kind == battle::EventKind::CommandVacant ||
                (Event.kind == battle::EventKind::Died && OfficerUnits.Contains(Event.target.value)))
                if (const auto* FallenUnit = battle::FindUnit(Simulation, Event.target))
                    Notices.ObserveStaff(Simulation, Event, *FallenUnit, WorldLocation(FallenUnit->pose.position, Origin));
            if (Event.kind == battle::EventKind::CavalryImpact && Event.amount > 0.f)
                if (const auto* Target = battle::FindUnit(Simulation, Event.target))
                    Notices.ObserveHit(Simulation, Event, *Target, WorldLocation(Target->pose.position, Origin));
            const bool bParried = Event.kind == battle::EventKind::Parried;
            if (bParried || (Event.kind == battle::EventKind::Hit && Event.amount > 0.f) ||
                Event.kind == battle::EventKind::Died)
            {
                auto* View = Units.Find(Event.target.value);
                const auto* Target = battle::FindUnit(Simulation, Event.target);
                if (!Target)
                    continue;
                const FVector GroundLocation = WorldLocation(Target->pose.position, Origin);
                if (!bParried)
                    Notices.ObserveHit(Simulation, Event, *Target, GroundLocation);
                if (!View)
                    continue;
                const auto* Source = battle::FindUnit(Simulation, Event.source);
                FVector Away = Source ? (GroundLocation - WorldLocation(Source->pose.position, Origin)).GetSafeNormal2D()
                                      : FVector::ZeroVector;
                if (Away.IsNearlyZero())
                    Away = -FRotator(0, View->Facing, 0).Vector();
                const FVector Axis = FVector::CrossProduct(FVector::UpVector, Away);
                if (Event.kind == battle::EventKind::Died)
                    View->Death = FDeathVisual{ Event.tick, Axis, GroundLocation };
                else
                    View->Hit = FHitVisual{ Event.tick, Axis };
                if (!bParried && MeleeSmoke && Source && !Source->archer)
                {
                    if (Event.kind == battle::EventKind::Died)
                    {
                        ++MeleeSmoke->Deaths;
                        if (MeleeSmoke->DeathId == 0)
                        {
                            MeleeSmoke->DeathId = Event.target.value;
                            MeleeSmoke->DeadEntity = View->Entity;
                            MeleeSmoke->DeadModel = View->Figure;
                            MeleeSmoke->DeathLocation = GroundLocation;
                        }
                    }
                    else
                    {
                        ++MeleeSmoke->Hits;
                        if (Target->alive)
                            MeleeSmoke->ReactionId = Event.target.value;
                    }
                }
                continue;
            }
            if (Event.kind != battle::EventKind::AttackLaunched)
                continue;
            Feedback.ObserveAttack(Event);
            if (MeleeSmoke && !Event.projectile)
            {
                auto& Check = MeleeSmoke.GetValue();
                if (!Check.FirstMeleeTick)
                    Check.FirstMeleeTick = Event.tick;
                if (Event.tick == Check.FirstMeleeTick.GetValue())
                {
                    // Orange's front edge on the negative Y side should reach blue first.
                    const auto* Source = battle::FindUnit(Simulation, Event.source);
                    Check.bFirstMeleeOnFlank &= Source && Source->pose.position.y < -1.f;
                }
            }
            const auto* Target = battle::FindUnit(Simulation, Event.target);
            if (!Target)
                continue;
            if (bCampaign && !Event.projectile && Target->regiment)
                if (const auto* Source = battle::FindUnit(Simulation, Event.source); Source && Source->regiment)
                {
                    RecentMelee.Add(Source->regiment.value, {Target->regiment, Event.tick});
                    RecentMelee.Add(Target->regiment.value, {Source->regiment, Event.tick});
                }
            const FVector TargetPos = WorldLocation(Target->pose.position, Origin);
            const bool bTelegraph = !Event.projectile && Event.target == Simulation.controlled &&
                !Event.reason.starts_with("cavalry impact") && Event.due_tick > Event.tick;
            Attacks.Add(Event.source.value, { Event.tick, Event.target, TargetPos, Event.projectile,
                bTelegraph ? Event.due_tick : 0 });
            if (Event.projectile)
            {
                const auto* Source = battle::FindUnit(Simulation, Event.source);
                if (Source && Source->platform == battle::BattlePlatform::FieldGun)
                    if (const auto* Controller = World.GetFirstPlayerController(); Controller && Controller->PlayerCameraManager)
                    {
                        if (!ArtilleryKick.IsValid())
                            ArtilleryKick = Cast<URegimentArtilleryCameraKick>(
                                Controller->PlayerCameraManager->AddNewCameraModifier(URegimentArtilleryCameraKick::StaticClass()));
                        if (auto* Kick = ArtilleryKick.Get())
                            Kick->KickFrom(WorldLocation(Source->pose.position, Origin));
                    }
                if (Source && !battle::UsesGunpowder(Source->ranged_weapon) &&
                    Event.reason != battle::FieldGunCanisterReason)
                {
                    const FVector SourcePos = WorldLocation(Source->pose.position, Origin);
                    const FVector Aim = (TargetPos - SourcePos).GetSafeNormal2D();
                    const auto* View = Units.Find(Event.source.value);
                    FVector Start = SourcePos + FVector(0, 0, 116);
                    double ForwardOffset = 55.0;
                    if (Source->platform == battle::BattlePlatform::Catapult)
                    {
                        Start.Z = SourcePos.Z + 221.0;
                        ForwardOffset = 49.0;
                    }
                    else if (Source->platform == battle::BattlePlatform::Ballista)
                    {
                        Start.Z = SourcePos.Z + 114.0;
                        ForwardOffset = 125.0;
                    }
                    else if (Source->platform == battle::BattlePlatform::FieldGun)
                    {
                        Start.Z = SourcePos.Z + 82.0;
                        ForwardOffset = 120.0;
                    }
                    else if (View && View->Figure.IsValid() && Rigs[Archer] && Rigs[Archer]->IsValid())
                    {
                        const FQuat Rotation(FVector::UpVector,
                            FMath::Atan2(Aim.Y, Aim.X) - Rigs[Archer]->GetMeshForwardYaw());
                        Start = SourcePos + FVector(0, 0, View->FeetOffset) +
                            Rotation.RotateVector(ReleasedBowGrip * View->MeshScale);
                    }
                    FVector End = TargetPos + FVector(0, 0, 102);
                    if (!Event.hit)
                    {
                        const FVector Across = FVector::CrossProduct(FVector::UpVector, Aim);
                        End = TargetPos + Aim * 70.0 + Across * (Event.id.value % 2 ? -55.0 : 55.0) + FVector(0, 0, 12);
                        if (GetHill())
                            End.Z += TerrainZAt(End, Origin) - TargetPos.Z;
                    }
                    Start += Aim * ForwardOffset;
                    double ClearanceApex = 0.0;
                    if (GetHill())
                        for (double Alpha : {.125, .25, .375, .5, .625, .75, .875})
                        {
                            const FVector Sample = FMath::Lerp(Start, End, Alpha);
                            const double Required = (TerrainZAt(Sample, Origin) + 30.0 - Sample.Z) / (4.0 * Alpha * (1.0 - Alpha));
                            ClearanceApex = FMath::Max(ClearanceApex, Required);
                        }
                    Arrows.Add({ Start, End, Event.tick,
                        FMath::Max(Event.due_tick, Event.tick + 2), Event.high_arc, ClearanceApex, Source->platform,
                        Source->ranged_weapon == battle::RangedWeapon::Arcane });
                }
            }
        }
    }

    ERegimentSound OfficerFootstep(const battle::UnitState& Unit, bool bRunning) const
    {
        using battle::TerrainFeature;
        const auto& At = Unit.pose.position;
        bool bMud = false;
        for (const auto& Area : Simulation.scenario.obstacles)
        {
            if (At.x < Area.min_x || At.x > Area.max_x || At.y < Area.min_y || At.y > Area.max_y)
                continue;
            // Bridge decking and roads take precedence over the underlying river or marsh.
            if (Area.feature == TerrainFeature::Bridge || Area.feature == TerrainFeature::Road)
                return bRunning ? ERegimentSound::GravelRun : ERegimentSound::GravelWalk;
            bMud |= Area.feature == TerrainFeature::Marsh || Area.feature == TerrainFeature::Ford ||
                Area.feature == TerrainFeature::River;
        }
        if (bMud)
            return bRunning ? ERegimentSound::MudRun : ERegimentSound::MudWalk;
        return bRunning ? ERegimentSound::GrassRun : ERegimentSound::GrassWalk;
    }

    void PlayMovementSounds(UWorld& World, const FVector& Origin)
    {
        const auto* Unit = battle::FindUnit(Simulation, Simulation.controlled);
        if (Unit && Unit->alive && Unit->platform == battle::BattlePlatform::None)
        {
            const FVector At = WorldLocation(Unit->pose.position, Origin);
            const bool bAirborne = Unit->jump_started_tick != 0;
            if (AudioOfficer != Unit->id.value)
            {
                AudioOfficer = Unit->id.value;
                OfficerStepDistance = 0.0;
                OfficerStepCount = 0;
                PreviousOfficerAudioMotion = {At, bAirborne};
            }
            const double Travel = FVector::Dist2D(At, PreviousOfficerAudioMotion.Position);
            const bool bLanded = !bAirborne && PreviousOfficerAudioMotion.bAirborne;
            const bool bRunning = battle::Length(Unit->desired_velocity) > Unit->speed * .75f;
            const bool bMounted = Unit->troop == battle::UnitTroop::Mounted;
            const double Stride = bMounted ? (bRunning ? 125.0 : 78.0) : (bRunning ? 91.0 : 63.0);
            PreviousOfficerAudioMotion = {At, bAirborne};
            if (bAirborne || Travel > 450.0 || (Travel < 1.0 && !bLanded))
                OfficerStepDistance = 0.0;
            else
            {
                OfficerStepDistance += Travel;
                if (bLanded || OfficerStepDistance >= Stride)
                {
                    OfficerStepDistance = bLanded ? 0.0 : FMath::Fmod(OfficerStepDistance, Stride);
                    const ERegimentSound Cue = bMounted
                        ? (bRunning ? ERegimentSound::HorseGallop : ERegimentSound::HorseWalk)
                        : OfficerFootstep(*Unit, bRunning);
                    const uint64 Source = (uint64(1) << 63) | Unit->id.value;
                    Audio.Play(World, Cue, At, Source, Simulation.tick, true);
                    if (bLanded || ++OfficerStepCount % 4 == 0)
                        Audio.Play(World, ERegimentSound::Gear, At, Source, Simulation.tick, true);
                }
            }
        }
        else
            AudioOfficer = 0;

        // Sample formations at 4 Hz. No per-soldier scans or audio components for individual troops.
        if (Simulation.tick % 5 != 0)
            return;
        for (const auto& Regiment : Simulation.regiments)
        {
            if (Regiment.units.empty())
                continue;
            const FVector At = WorldLocation(Regiment.pose.position, Origin);
            const auto* Previous = PreviousFormationAudioPositions.Find(Regiment.id.value);
            const double Travel = Previous ? FVector::Dist2D(*Previous, At) : 0.0;
            PreviousFormationAudioPositions.Add(Regiment.id.value, At);
            if (Travel < 11.0 || Travel > 600.0 || Regiment.melee_engaged || battle::IsDeploying(Simulation, Regiment))
                continue;
            const bool bMounted = MountedAudioRegiments.Contains(Regiment.id.value);
            const bool bCharging = Regiment.cavalry_phase == battle::CavalryPhase::Approach &&
                Regiment.charge_runup_m > .30f;
            const ERegimentSound Cue = !bMounted ? ERegimentSound::March
                : (bCharging || Travel > 100.0 ? ERegimentSound::HorseGallop : ERegimentSound::HorseWalk);
            Audio.Play(World, Cue, At, Regiment.id.value, Simulation.tick);
        }
    }

    void UpdateAcoustics(UWorld& World, const FVector& Origin, battle::MarchTerrain Terrain, battle::MarchField Field)
    {
        const double Now = FPlatformTime::Seconds();
        if (Now < NextAmbientUpdate || !Audio.CanPlay(World))
            return;
        NextAmbientUpdate = Now + .12;
        FVector Listener;
        FRotator Rotation;
        World.GetFirstPlayerController()->GetPlayerViewPoint(Listener, Rotation);

        const FVector LocalListener = (Listener - Origin) / CentimetersPerMeter;
        FVector Nearest = FVector::ZeroVector;
        double BestDistance = FMath::Square(34.0);
        bool bNearWater = false;
        const auto TryWater = [&](double X, double Y)
        {
            const FVector At(X, Y, 0.0);
            const double Distance = FVector::DistSquared(LocalListener, At);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                Nearest = At;
                bNearWater = true;
            }
        };
        if (battle::MarchHasWaterCrossings(Terrain))
            TryWater(0.0, FMath::Clamp(LocalListener.Y, -double(Field.HalfHeight()), double(Field.HalfHeight())));
        if (Terrain == battle::MarchTerrain::Cannae)
            TryWater(FMath::Clamp(LocalListener.X, -double(Field.HalfWidth()), double(Field.HalfWidth())), -Field.HalfHeight() - 4.0);
        for (const auto& Area : Simulation.scenario.obstacles)
            if (Area.feature == battle::TerrainFeature::River)
                TryWater(FMath::Clamp(LocalListener.X, double(Area.min_x), double(Area.max_x)),
                    FMath::Clamp(LocalListener.Y, double(Area.min_y), double(Area.max_y)));
        TOptional<FVector> Water;
        if (bNearWater)
            Water = WorldLocation({float(Nearest.X), float(Nearest.Y), 0.f}, Origin);
        const bool bRunning = Simulation.outcome == battle::BattleOutcome::Running;
        const auto HeardRecently = [this, bRunning](const TOptional<battle::Tick>& Played, battle::Tick Window)
        {
            return bRunning && Played && Simulation.tick >= *Played && Simulation.tick - *Played <= Window;
        };
        Audio.UpdateAmbience(World, Water, HeardRecently(LastMeleeAudioTick, 140),
            HeardRecently(LastGunpowderAudioTick, 160));
    }

    void PlayBattleSounds(UWorld& World, const FVector& Origin)
    {
        if (!Audio.CanPlay(World))
            return;
        using battle::EventKind;
        for (const auto& Event : Simulation.events)
        {
            const battle::UnitState* Unit = nullptr;
            const battle::RegimentState* Regiment = nullptr;
            bool bPersonal = Simulation.controlled &&
                (Event.source == Simulation.controlled || Event.target == Simulation.controlled);
            ERegimentSound Sound;
            switch (Event.kind)
            {
            case EventKind::AttackLaunched:
                if (!Event.projectile)
                    continue;
                Unit = battle::FindUnit(Simulation, Event.source);
                if (!Unit)
                    continue;
                switch (Unit->platform)
                {
                case battle::BattlePlatform::FieldGun: Sound = ERegimentSound::Cannon; break;
                case battle::BattlePlatform::Catapult: Sound = ERegimentSound::Catapult; break;
                case battle::BattlePlatform::Ballista: Sound = ERegimentSound::Ballista; break;
                default:
                    if (Unit->ranged_weapon == battle::RangedWeapon::Arcane)
                        continue;
                    Sound = battle::UsesGunpowder(Unit->ranged_weapon) ? ERegimentSound::Musket : ERegimentSound::Bow;
                    break;
                }
                if (Sound == ERegimentSound::Cannon || Sound == ERegimentSound::Musket)
                    LastGunpowderAudioTick = Event.tick;
                break;
            case EventKind::Hit:
            {
                // Charge impacts already have a separate event at this contact.
                if (Event.amount <= 0.f || Event.reason.starts_with("cavalry impact"))
                    continue;
                Unit = battle::FindUnit(Simulation, Event.target);
                const auto* Attacker = battle::FindUnit(Simulation, Event.source);
                if (Event.projectile)
                {
                    if (!Attacker || Attacker->platform != battle::BattlePlatform::None ||
                        (!Attacker->archer && Attacker->ranged_weapon != battle::RangedWeapon::Bow &&
                            Attacker->ranged_weapon != battle::RangedWeapon::Crossbow) ||
                        battle::UsesGunpowder(Attacker->ranged_weapon) || Attacker->ranged_weapon == battle::RangedWeapon::Arcane)
                        continue;
                    Sound = ERegimentSound::ArrowImpact;
                }
                else
                {
                    LastMeleeAudioTick = Event.tick;
                    Sound = Attacker && battle::IsElephant(Attacker->platform)
                        ? ERegimentSound::ElephantImpact : (bPersonal ? ERegimentSound::OfficerHit : ERegimentSound::Steel);
                }
                break;
            }
            case EventKind::Parried:
                Unit = battle::FindUnit(Simulation, Event.source);
                LastMeleeAudioTick = Event.tick;
                Sound = bPersonal ? ERegimentSound::OfficerParry : ERegimentSound::Steel;
                break;
            case EventKind::CavalryImpact:
                Unit = battle::FindUnit(Simulation, Event.source);
                Regiment = battle::FindRegiment(Simulation, Event.regiment);
                LastMeleeAudioTick = Event.tick;
                Sound = Unit && battle::IsElephant(Unit->platform) ? ERegimentSound::ElephantImpact : ERegimentSound::Cavalry;
                if (Unit)
                {
                    const auto* Charging = battle::FindRegiment(Simulation, Unit->regiment);
                    Audio.Play(World, ERegimentSound::ChargeShout,
                        WorldLocation(Charging ? Charging->pose.position : Unit->pose.position, Origin),
                        Charging ? Charging->id.value : (uint64(1) << 63) | Unit->id.value, Event.tick);
                }
                break;
            case EventKind::OfficerSucceeded:
                Unit = battle::FindUnit(Simulation, Event.source);
                [[fallthrough]];
            case EventKind::FormationReformed:
                Regiment = battle::FindRegiment(Simulation, Event.regiment);
                Sound = Regiment && Regiment->order && Regiment->order->type == battle::OrderType::Rally
                    ? ERegimentSound::Rally : ERegimentSound::RegimentSignal;
                break;
            case EventKind::OrderChanged:
                Regiment = battle::FindRegiment(Simulation, Event.regiment);
                if (!Regiment)
                    continue;
                if (Event.reason.starts_with("rallied:"))
                    Sound = ERegimentSound::Rally;
                else if (Event.reason.starts_with("panic:"))
                    Sound = ERegimentSound::Retreat;
                else if (Event.reason == "order delivered" && Event.tick > 0 && Regiment->order)
                {
                    const auto Type = Regiment->order->type;
                    bPersonal = Regiment->order->player_direct;
                    if (Type == battle::OrderType::Rally)
                        Sound = ERegimentSound::Rally;
                    else if (battle::IsWithdrawing(Type))
                        Sound = ERegimentSound::Retreat;
                    else if (battle::IsAttacking(Type))
                        Sound = ERegimentSound::AttackSignal;
                    else if (bPersonal)
                        Sound = ERegimentSound::RegimentSignal;
                    else
                        continue;
                }
                else
                    continue;
                break;
            default:
                continue;
            }
            if (Unit || Regiment)
            {
                const uint64 Source = bPersonal && Unit && Simulation.controlled
                    ? (uint64(1) << 63) | Simulation.controlled.value
                    : Unit && Unit->regiment ? Unit->regiment.value : Regiment ? Regiment->id.value :
                        (uint64(1) << 63) | Unit->id.value;
                Audio.Play(World, Sound, WorldLocation(Unit ? Unit->pose.position : Regiment->pose.position, Origin),
                    Source, Event.tick, bPersonal);
            }
        }
        PlayMovementSounds(World, Origin);
    }

    void AddFallenProxy(AActor& Owner, UStaticMesh& Mesh, UMaterialInterface* Material,
        int32 Side, const battle::UnitState& Unit, const FDeathVisual& Death)
    {
        if (!FallenMeshes[Side].IsValid())
        {
            auto* Component = NewFigureComponent<UInstancedStaticMeshComponent>(Owner, *Owner.GetRootComponent());
            Component->SetStaticMesh(&Mesh);
            if (Material)
                Component->SetMaterial(0, Material);
            Component->SetCastShadow(false);
            Component->RegisterComponent();
            Component->SetComponentTickEnabled(false);
            FallenMeshes[Side] = Component;
        }
        FallenProxies[Side].Add(Death.GroundLocation, Unit.pose.yaw, Unit.troop == battle::UnitTroop::Mounted, Simulation.tick);
    }

    bool HasFallen() const
    {
        return !Fallen.IsEmpty() || !FallenProxies[0].GetBodies().IsEmpty() || !FallenProxies[1].GetBodies().IsEmpty();
    }

    void PresentFallen(const FPresentationCamera* Camera)
    {
        for (auto It = Fallen.CreateIterator(); It; ++It)
        {
            auto& Fall = It.Value();
            const double Age = double(Simulation.tick - Fall.Death.Tick) + PostBattleTicks;
            auto* Body = Fall.Components.Figure.Get();
            if (!Body || Age >= RegimentFallenLifetimeTicks)
            {
                if (MeleeSmoke && MeleeSmoke->DeathId == It.Key() && MeleeSmoke->bCollapsed)
                    MeleeSmoke->bExpired = Age >= RegimentFallenLifetimeTicks;
                Fall.Components.DestroyFigure();
                It.RemoveCurrent();
                continue;
            }
            if (Camera)
            {
                const bool bVisible = Camera->Sees(Fall.Death.GroundLocation + FVector(0, 0, 70), 225.f);
                if (Body->IsVisible() != bVisible)
                    Body->SetVisibility(bVisible, true);
                if (auto* Horse = Fall.Components.Mount.Get(); Horse && Horse->IsVisible() != bVisible)
                    Horse->SetVisibility(bVisible);
                if (!bVisible)
                    continue;
            }
            if (Fall.bSettled)
                continue;
            const double Progress = FMath::Clamp((Age + 1.0) / CollapseTicks, 0.0, 1.0);
            Fall.bSettled = Progress == 1.0;
            const double Angle = FMath::DegreesToRadians(86.0 * FMath::SmoothStep(0.0, 1.0, Progress));
            const FQuat Lean(Fall.Death.LeanAxis, Angle);
            FTransform Transform(Lean * Fall.UprightRotation, Fall.Death.GroundLocation, FVector(Fall.Scale));
            const auto Bounds = Body->GetSkinnedAsset()->GetBounds().TransformBy(Transform);
            Transform.AddToTranslation(FVector(0, 0, Fall.Death.GroundLocation.Z + Bounds.BoxExtent.Z - Bounds.Origin.Z));
            Body->SetWorldTransform(Transform);
            if (auto* Horse = Fall.Components.Mount.Get())
            {
                FTransform MountTransform(Lean * Fall.MountRotation, Fall.Death.GroundLocation);
                const auto MountBounds = Horse->CalcBounds(MountTransform);
                const double Clearance = Fall.Death.GroundLocation.Z + MountBounds.BoxExtent.Z - MountBounds.Origin.Z;
                MountTransform.AddToTranslation(FVector(0, 0, Clearance));
                Horse->SetWorldTransform(MountTransform);
            }
        }
        const double Tick = double(Simulation.tick) + PostBattleTicks;
        for (int32 Side = 0; Side < 2; ++Side)
        {
            auto* Mesh = FallenMeshes[Side].Get();
            if (!Mesh || !FallenProxies[Side].TakeChanges(Tick))
                continue;
            auto& Batch = FallenBatches[Side];
            Batch.Transforms.Reset();
            for (const auto& Body : FallenProxies[Side].GetBodies())
            {
                const FQuat Facing(FVector::UpVector, Body.Yaw);
                const auto Part = [&](const FVector& Offset, const FVector& Size)
                {
                    Batch.Transforms.Emplace(Facing, Body.Location + Facing.RotateVector(Offset), Size * .01);
                };
                Part({-15, 0, 15}, {119, 43, 30});
                Part({58, 0, 15}, {30, 28, 28});
                if (Body.bMounted)
                    Part({-36, -42, 19}, {150, 64, 38});
            }
            Batch.Present(*Mesh);
        }
    }

    void PresentArrows(UInstancedStaticMeshComponent& Visuals, const FPresentationCamera* Camera)
    {
        const UStaticMesh* Mesh = Visuals.GetStaticMesh();
        auto* Stones = StoneVisuals.Get();
        auto* Arcane = ArcaneVisuals.Get();
        if (!Mesh && !Stones && !Arcane)
            return;
        if (Simulation.outcome != battle::BattleOutcome::Running)
        {
            Arrows.Reset();
            if (Visuals.GetInstanceCount() != 0)
                Visuals.ClearInstances();
            if (Stones && Stones->GetInstanceCount() != 0)
                Stones->ClearInstances();
            if (Arcane && Arcane->GetInstanceCount() != 0)
                Arcane->ClearInstances();
            return;
        }
        Arrows.RemoveAllSwap([this](const FArrowFlight& Arrow) { return Arrow.EndTick < Simulation.tick; });
        const auto Bounds = Mesh ? Mesh->GetBounds() : FBoxSphereBounds(ForceInit);
        const double TipOffset = (Bounds.BoxExtent.Y - Bounds.Origin.Y) * ArrowScale;
        if (Camera || Stones || Arcane)
        {
            ArrowBatch.Transforms.Reset();
            StoneBatch.Transforms.Reset();
            ArcaneBatch.Transforms.Reset();
            for (const FArrowFlight& Arrow : Arrows)
            {
                const FTransform Transform = Arrow.At(Simulation.tick, TipOffset);
                const float Radius = Arrow.Platform == battle::BattlePlatform::Ballista ? 180.f : 110.f;
                if (!Camera || Camera->Sees(Transform.GetTranslation(), Radius))
                {
                    if (Arrow.Platform == battle::BattlePlatform::Catapult || Arrow.Platform == battle::BattlePlatform::FieldGun)
                        StoneBatch.Transforms.Add(Transform);
                    else if (Arrow.bArcane)
                        ArcaneBatch.Transforms.Add(Transform);
                    else
                        ArrowBatch.Transforms.Add(Transform);
                }
            }
            if (Mesh)
                ArrowBatch.Present(Visuals);
            if (Stones)
                StoneBatch.Present(*Stones);
            if (Arcane)
                ArcaneBatch.Present(*Arcane);
            return;
        }
        const int32 Existing = Visuals.GetInstanceCount();
        for (int32 Index = 0; Index < Arrows.Num(); ++Index)
        {
            const FTransform Pose = Arrows[Index].At(Simulation.tick, TipOffset);
            if (Index < Existing)
                Visuals.UpdateInstanceTransform(Index, Pose, true);
            else
                Visuals.AddInstance(Pose, true);
        }
        for (int32 Index = Arrows.Num(); Index < PresentedArrowCount; ++Index)
            Visuals.UpdateInstanceTransform(Index, FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), true);
        PresentedArrowCount = Arrows.Num();
    }

    bool IsScriptedPerf() const { return Perf && Perf->IsScripted(); }
    bool IsPerfOfficerView() const { return bPerfOfficerView || bool(Simulation.controlled); }

    void UpdatePerfCamera(UWorld& World, const FVector& Origin, battle::MarchTerrain Terrain)
    {
        auto* Controller = World.GetFirstPlayerController();
        if (!Controller)
            return;
        auto* Camera = PerfCamera.Get();
        if (!Camera)
        {
            const auto* Original = Cast<ACameraActor>(Controller->GetViewTarget());
            if (!Original)
                return;
            PerfOverview = Original->GetActorTransform();
            Camera = World.SpawnActor<ACameraActor>(PerfOverview.GetLocation(), PerfOverview.Rotator());
            if (!Camera)
                return;
            PerfCamera = Camera;
            auto* View = Camera->GetCameraComponent();
            View->SetFieldOfView(bPerfOfficerView ? 76.f : Original->GetCameraComponent()->FieldOfView);
            View->bConstrainAspectRatio = !bPerfOfficerView && Original->GetCameraComponent()->bConstrainAspectRatio;
            const auto& OriginalSettings = Original->GetCameraComponent()->PostProcessSettings;
            View->PostProcessSettings.bOverride_AutoExposureBias = OriginalSettings.bOverride_AutoExposureBias;
            View->PostProcessSettings.AutoExposureBias = OriginalSettings.AutoExposureBias;
            ApplyRegimentBattleCameraExposure(*View);
            View->PostProcessSettings.bOverride_MotionBlurAmount = true;
            View->PostProcessSettings.MotionBlurAmount = 0.0f;
            Controller->SetViewTarget(Camera);
        }
        const double YawOffset = FRegimentPerfCapture::OrbitYaw(Simulation.tick);
        const auto MoveCamera = [&](const FVector& Position, const FRotator& Rotation)
        {
            const FRotator Previous = Camera->GetActorRotation();
            Camera->SetActorLocationAndRotation(Position, Rotation);
            if (Controller->GetViewTarget() == Camera && !Camera->GetActorRotation().Equals(Previous, .01))
                Perf->ObserveCameraMotion(Simulation.tick);
        };
        if (bPerfOfficerView)
        {
            const auto* Officer = battle::FindUnit(Simulation, PerfOfficer);
            if (!Officer || !Officer->alive)
            {
                PerfOfficer = RegimentOfficerOrders::FindOfficer(Simulation, 0);
                Officer = battle::FindUnit(Simulation, PerfOfficer);
            }
            if (!Officer || !Officer->alive)
                return;
            const double Yaw = FMath::RadiansToDegrees(double(Officer->pose.yaw)) + YawOffset;
            FRotator Rotation(OfficerDefaultCameraPitch, Yaw, 0.0);
            const bool bMounted = Officer->troop == battle::UnitTroop::Mounted;
            const FVector Pivot = WorldLocation(Officer->pose.position, Origin) + FVector(0, 0, bMounted ? 207.0 : 143.0);
            const FVector Focus = Pivot + FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y) * 62.0;
            FVector Wanted = Focus - Rotation.Vector() * (OfficerDefaultCameraDistance + (bMounted ? 80.0 : 0.0));
            const double MinimumZ = TerrainZAt(Wanted, Origin) + 85.0;
            if (Wanted.Z < MinimumZ)
            {
                Wanted.Z = MinimumZ;
                Rotation = (Focus - Wanted).Rotation();
            }
            MoveCamera(LimitOfficerCamera(Pivot, Wanted, Origin, Terrain), Rotation);
            return;
        }
        const FVector Position = PerfOverview.GetLocation();
        FRotator Rotation = PerfOverview.Rotator();
        const FVector Forward = Rotation.Vector();
        const double Radius = Forward.Z < -.001 ? (Origin.Z - Position.Z) / Forward.Z : 3000.0;
        const FVector Focus = Position + Forward * Radius;
        Rotation.Yaw += YawOffset;
        MoveCamera(Focus + (Position - Focus).RotateAngleAxis(YawOffset, FVector::UpVector), Rotation);
    }

    void ReportPerf()
    {
        if (Perf && Perf->IsMeasuring())
        {
            TOptional<uint64> StateHash;
            if (Perf->IsScripted())
                StateHash = battle::StateHash(Simulation);
            const FString Summary = Perf->Finish(Simulation.tick, StateHash);
            if (!Summary.IsEmpty())
                UE_LOG(LogRegimentBattle, Display, TEXT("%s"), *Summary);
        }
    }
};

void FRegimentBattleSessionDeleter::operator()(FRegimentBattleSession* Session) const
{
    delete Session;
}

void FRegimentMissionDeleter::operator()(FRegimentMissionRuntime* InMission) const
{
    delete InMission;
}

ARegimentBattleField::ARegimentBattleField()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FieldOrigin")));

    BlueMarkers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BlueMarkers"));
    OrangeMarkers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("OrangeMarkers"));
    for (auto* Markers : { BlueMarkers.Get(), OrangeMarkers.Get() })
    {
        Markers->SetupAttachment(GetRootComponent());
        Markers->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Markers->SetCastShadow(false);
        Markers->SetCanEverAffectNavigation(false);
    }

    ArrowVisuals = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ArrowVisuals"));
    ArrowVisuals->SetupAttachment(GetRootComponent());
    ArrowVisuals->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ArrowVisuals->SetCastShadow(false);
    ArrowVisuals->SetCanEverAffectNavigation(false);

#define MERCENARY_ASSET(Name) TEXT("/Game/Fab/Free_LOWPO__Mercenary_Company___Low_Poly_Characters___Weapons/" Name)
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Sword(MERCENARY_ASSET("Mercenary_Sword"));
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> LongSword(MERCENARY_ASSET("Mercenary_Long_Sword"));
    static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> Bow(MERCENARY_ASSET("Mercenary_Bow"));
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Arrow(MERCENARY_ASSET("Mercenary_Arrow"));
#undef MERCENARY_ASSET
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Blue(TEXT("/Game/Battle/Materials/MI_RegimentFigureBlue"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Orange(TEXT("/Game/Battle/Materials/MI_RegimentFigureOrange"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Weapon(TEXT("/Game/Battle/Materials/MI_RegimentWeapon"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> FigurePart(TEXT("/Game/Battle/Materials/M_RegimentFigureParts"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> BlueBase(TEXT("/Game/Battle/Materials/M_RegimentMarkerBlue"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> OrangeBase(TEXT("/Game/Battle/Materials/M_RegimentMarkerOrange"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Block(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Wood(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    SwordsmanWeapon = Sword.Get();
    CaptainWeapon = LongSword.Get();
    ArcherWeapon = Bow.Get();
    BlueFigureMaterial = Blue.Get();
    OrangeFigureMaterial = Orange.Get();
    WeaponMaterial = Weapon.Get();
    FigurePartMaterial.Reset(FigurePart.Get());
    BlueMarkerMaterial = BlueBase.Get();
    OrangeMarkerMaterial = OrangeBase.Get();
    StaffBlock = Block.Object;
    StaffCylinder = Cylinder.Object;
    SpearPoint = Cone.Object;
    ProjectileSphere = Sphere.Object;
    TimberMaterial = Wood.Get();
    ArrowVisuals->SetStaticMesh(Arrow.Get());
}

ARegimentBattleField::~ARegimentBattleField() = default;

void ARegimentBattleField::BeginPlay()
{
    Super::BeginPlay();
    if (TimberMaterial)
    {
        auto* Timber = UMaterialInstanceDynamic::Create(TimberMaterial, this);
        Timber->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(FColor(116, 69, 38)));
        Timber->SetScalarParameterValue(TEXT("Roughness"), .90f);
        TimberMaterial = Timber;
    }
    bSmokeRun = FParse::Param(FCommandLine::Get(), TEXT("RegimentBattleSmoke"));
    // These components are still serialized in older levels.
    for (auto* LegacyBase : {BlueMarkers.Get(), OrangeMarkers.Get()})
    {
        LegacyBase->ClearInstances();
        LegacyBase->SetHiddenInGame(true);
    }
    if (WeaponMaterial)
        ArrowVisuals->SetMaterial(0, WeaponMaterial);
    if (bSmokeRun || FParse::Param(FCommandLine::Get(), TEXT("RegimentFieldOnly")))
        StartBattle();
    else
        Mission.Reset(new FRegimentMissionRuntime(*this,
            FParse::Param(FCommandLine::Get(), TEXT("RegimentMissionSmoke"))));
}

bool ARegimentBattleField::StartBattle()
{
    const bool bCampaign = Mission && Mission->TacticalScenario.has_value();
    auto Created = battle::CreateWorld(bCampaign ? *Mission->TacticalScenario : MakeBattle(), 42);
    if (!Created)
    {
        UE_LOG(LogRegimentBattle, Error, TEXT("BattleCore rejected the field scenario."));
        if (bSmokeRun || (Mission && Mission->bSmoke))
            FPlatformMisc::RequestExitWithStatus(false, 1);
        HudState.Status = ERegimentBattleStatus::Failed;
        return false;
    }
    const bool bRestarting = Session.IsValid();
    FRegimentBattlePlayback Playback = Session ? Session->Playback : FRegimentBattlePlayback{};
    Playback.SetOfficerSelection(false);
    Playback.ResetClock();
    ClearBattle();
    const auto Field = bCampaign ? Mission->GetPreset().Field : battle::MarchField{};
    Session.Reset(new FRegimentBattleSession(std::move(Created.value), bCampaign, Field));
    Session->Units.Reserve(int32(Session->Simulation.units.size()));
    const TCHAR* CommandLine = FCommandLine::Get();
    if (FParse::Param(CommandLine, TEXT("RegimentPerfReport")))
    {
        int32 PerfTicks = 400;
        FParse::Value(CommandLine, TEXT("RegimentPerfTicks="), PerfTicks);
        FString OutputPath;
        FParse::Value(CommandLine, TEXT("RegimentPerfOutput="), OutputPath);
        FString CameraMode;
        FParse::Value(CommandLine, TEXT("RegimentPerfCamera="), CameraMode);
        Session->bPerfOfficerView = CameraMode.Equals(TEXT("officer"), ESearchCase::IgnoreCase);
        const bool bScripted = Session->bPerfOfficerView || CameraMode.Equals(TEXT("overview"), ESearchCase::IgnoreCase);
        Session->bPerfBaselinePresentation = FParse::Param(CommandLine, TEXT("RegimentPerfBaselinePresentation"));
        Session->bPerfFullDetail = Session->Simulation.units.size() <= 500 &&
            FParse::Param(CommandLine, TEXT("RegimentPerfDisableLod"));
        Session->Perf.Emplace(FMath::Clamp(PerfTicks, 1, 10000), MoveTemp(OutputPath), Session->bPerfFullDetail,
            bScripted);
        Session->bQueueInitialPossession = !bScripted && FParse::Param(CommandLine, TEXT("RegimentPerfOfficer"));
    }
    else if (bCampaign && Mission->GetPreset().FigureTarget >= MissionMaxFigureTarget)
    {
        if (!bRestarting)
            Playback.SetSpeed(ERegimentBattleSpeed::Half);
        Session->bQueueInitialPossession = true;
    }
    if (Mission)
        Mission->BeginBattleReport(Session->Perf || bSmokeRun || Mission->bSmoke ? nullptr : &Session->Simulation);
    std::string PolicyWeights;
    if (bCampaign)
        Session->GroupPolicy = RequestedGroupPolicy(PolicyWeights);
    Session->Trace.Configure(Session->TraceGrid, std::move(PolicyWeights));
    if (bCampaign)
    {
        const auto& Preset = Mission->GetPreset();
        if (Preset.Objective.Goal != EMissionGoal::Eliminate || Preset.Tutorial != ERegimentTutorial::None)
        {
            auto& Progress = Session->MissionProgress.Emplace(Preset, Session->Simulation);
            Session->bQueueInitialPossession |= Progress.StartsPossessed();
        }
    }
    if (bSmokeRun)
        Session->MeleeSmoke.Emplace();
    Session->Playback = Playback;
    Session->Audio.SetPaused(Playback.IsPaused());
    PresentedTick.Reset();
    HudState = {};
    FactionMaterials[0] = FactionMaterials[1] = nullptr;
    AncientFigureMaterials[0] = AncientFigureMaterials[1] = nullptr;
    if (FigurePartMaterial.IsValid())
    {
        const auto MakeSurface = [&](const FLinearColor& Color, ERegimentFigureSurface Surface)
        {
            auto* Material = UMaterialInstanceDynamic::Create(FigurePartMaterial.Get(), this);
            Material->SetVectorParameterValue(TEXT("PartColor"), Color);
            Material->SetScalarParameterValue(TEXT("PartSurface"), float(Surface));
            return Material;
        };
        Session->FigureWood.Reset(MakeSurface(FLinearColor(.15f, .078f, .039f), ERegimentFigureSurface::Wood));
        Session->FigureSteel.Reset(MakeSurface(FLinearColor(.23f, .25f, .27f), ERegimentFigureSurface::Metal));
    }
    auto* BaseMaterial = TimberMaterial ? TimberMaterial->GetMaterial() : nullptr;
    ERegimentHistoricalFigure HistoricalProfiles[MissionRosterCapacity] = {};
    FStringView EraId;
    bool bNapoleonic = false;
    if (bCampaign)
    {
        const auto& Selection = Mission->GetPreset().Era;
        if (const auto* Era = RegimentEraCatalog::Find(Selection.Id))
        {
            EraId = Era->Id;
            Session->bAncient = Era->Id == TEXT("ancient");
            bNapoleonic = Era->Id == TEXT("napoleonic");
            Session->Factions[0] = RegimentEraCatalog::FindFaction(*Era, Selection.BlueFaction);
            Session->Factions[1] = RegimentEraCatalog::FindFaction(*Era, Selection.OrangeFaction);
            const auto& Roster = Mission->GetPreset().Roster;
            for (int32 Slot = 0; Slot < MissionRosterCapacity; ++Slot)
            {
                const auto* Faction = Session->Factions[MissionTeam(Slot)];
                if (Faction && Roster[Slot].bCavalry)
                    Session->MountedProfiles[Slot] = RegimentEraCatalog::MountedTroop(*Faction, Roster[Slot].MountedClass);
                if (Session->bAncient || bNapoleonic)
                    HistoricalProfiles[Slot] = RegimentHistoricalProfile(Roster[Slot].InfantryProfile);
            }
            for (int32 Team = 0; Team < 2; ++Team)
            {
                if (const auto* Faction = Session->Factions[Team])
                {
                    (Team == 0 ? HudState.BlueFaction : HudState.OrangeFaction) = FText::FromString(Faction->Name);
                    if (BaseMaterial)
                    {
                        auto* Paint = UMaterialInstanceDynamic::Create(BaseMaterial, this);
                        Paint->SetVectorParameterValue(TEXT("Color"), Faction->Heraldry);
                        FactionMaterials[Team] = Paint;
                    }
                    if (Faction->Visual != ERegimentEraVisual::Classic && !Session->EraHeadgear.Contains(Faction->Visual))
                        Session->EraHeadgear.Add(Faction->Visual).Create(*this,
                            BuildRegimentEraHeadgear(Faction->Visual), BaseMaterial);
                    if (Faction->Visual == ERegimentEraVisual::Sengoku && !Session->SengokuBanners.Mesh)
                        Session->SengokuBanners.Create(*this, BuildRegimentSengokuBanner(), BaseMaterial);
                    if (Session->bAncient || Faction->Visual != ERegimentEraVisual::Classic)
                    {
                        if (auto* TeamMaterial = Team == 0 ? BlueFigureMaterial.Get() : OrangeFigureMaterial.Get())
                        {
                            auto* Cloth = UMaterialInstanceDynamic::Create(TeamMaterial, this);
                            Cloth->SetVectorParameterValue(TEXT("TeamTint"), Faction->Heraldry);
                            Cloth->SetScalarParameterValue(TEXT("TeamTintStrength"), .49f);
                            AncientFigureMaterials[Team] = Cloth;
                        }
                    }
                }
            }
        }
    }
    bool bArcane = false;
    Session->HistoricalFigures.Reserve(int32(Session->Simulation.units.size()));
    for (const auto& Unit : Session->Simulation.units)
    {
        const int32 Side = battle::UnitTeam(Session->Simulation, Unit) == 0 ? 0 : 1;
        ++(Side == 0 ? HudState.BlueStarting : HudState.OrangeStarting);
        if (Unit.platform != battle::BattlePlatform::None && !Session->Platforms.Contains(Unit.platform))
            Session->Platforms.Add(Unit.platform).Create(*this, BuildRegimentAncientPlatform(Unit.platform), BaseMaterial);
        bArcane |= Unit.ranged_weapon == battle::RangedWeapon::Arcane;
        auto Dress = ERegimentHistoricalFigure::None;
        if (const auto* Faction = Session->Factions[Side]; Faction &&
            Unit.platform == battle::BattlePlatform::None && Session->CommandFigure(Unit.id) == ECommandFigure::None)
        {
            const auto Profile = Unit.regiment && Unit.regiment.value <= MissionRosterCapacity
                ? HistoricalProfiles[Unit.regiment.value - 1] : ERegimentHistoricalFigure::None;
            const bool bSengoku = Faction->Visual == ERegimentEraVisual::Sengoku;
            Dress = bSengoku ? RegimentSengokuStyle(Unit)
                : RegimentHistoricalStyle(Profile, Faction->Id, Unit, EraId);
            const bool bMounted = Unit.troop == battle::UnitTroop::Mounted;
            if (Dress != ERegimentHistoricalFigure::None && !Session->HistoricalDress.Contains(Dress))
            {
                Session->HistoricalDress.Add(Dress).Create(*this, BuildRegimentHistoricalDress(Dress), BaseMaterial);
                Session->HistoricalProxies.Add(Dress).Create(*this, BuildRegimentHistoricalProxy(Dress), BaseMaterial,
                    bMounted && Dress >= ERegimentHistoricalFigure::Egyptian);
                if (bMounted)
                    Session->HistoricalTack.Add(Dress).Create(*this, BuildRegimentHistoricalTack(Dress), BaseMaterial);
                if (bSengoku)
                {
                    const auto Weapon = BuildRegimentSengokuWeapon(Dress);
                    if (!Weapon.Parts.IsEmpty())
                        Session->SengokuWeapons.Add(Dress).Create(*this, Weapon, BaseMaterial);
                }
            }
            if (!Unit.archer)
            {
                const auto Shield = Session->ShieldStyle(Side, Dress, bMounted, Unit.id.value);
                if (Shield && !Session->AncientShields.Contains(*Shield))
                    Session->AncientShields.Add(*Shield).Create(*this, BuildRegimentAncientShield(*Shield), BaseMaterial);
            }
            const auto HandProp = HistoricalHandProp(Session->bAncient, Dress, Unit);
            if (HandProp != battle::RangedWeapon::None && !Session->LooseWeapons.Contains(HandProp))
                Session->LooseWeapons.Add(HandProp).Create(*this, BuildRegimentLooseWeapon(HandProp), BaseMaterial);
        }
        Session->HistoricalFigures.Add(Dress);
    }
    const auto AddSphericalProjectile = [&](FColor Color)
    {
        auto* Mesh = NewFigureComponent<UInstancedStaticMeshComponent>(*this, *GetRootComponent());
        Mesh->SetStaticMesh(ProjectileSphere);
        Mesh->SetCastShadow(false);
        if (BaseMaterial)
        {
            auto* Paint = UMaterialInstanceDynamic::Create(BaseMaterial, Mesh);
            Paint->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(Color));
            Mesh->SetMaterial(0, Paint);
        }
        Mesh->RegisterComponent();
        Mesh->SetComponentTickEnabled(false);
        return Mesh;
    };
    if (ProjectileSphere && (Session->Platforms.Contains(battle::BattlePlatform::Catapult) ||
        Session->Platforms.Contains(battle::BattlePlatform::FieldGun)))
        Session->StoneVisuals = AddSphericalProjectile(FColor(117, 122, 128));
    if (ProjectileSphere && bArcane)
        Session->ArcaneVisuals = AddSphericalProjectile(FColor(89, 183, 254));
    RefreshDecisionEntries();
    return true;
}

FVector ARegimentBattleField::GetBattleOrigin() const
{
    return Mission && Mission->TacticalScenario ? Mission->Origin : GetActorLocation();
}

bool ARegimentBattleField::IsBattlePaused() const
{
    return Session && Session->Playback.IsPaused();
}

ERegimentBattleSpeed ARegimentBattleField::GetBattleSpeed() const
{
    return Session ? Session->Playback.GetSpeed() : ERegimentBattleSpeed::Normal;
}

void ARegimentBattleField::ToggleBattlePause()
{
    if (Session && !IsChoosingOfficer())
    {
        Session->Playback.SetPaused(!IsBattlePaused());
        Session->Audio.SetPaused(IsBattlePaused());
        if (auto* Kick = Session->ArtilleryKick.Get())
            Kick->ResetKick();
        ClearOfficerDirectInput();
    }
}

void ARegimentBattleField::SetBattleSpeed(ERegimentBattleSpeed Speed)
{
    if (Session)
        Session->Playback.SetSpeed(Speed);
}

void ARegimentBattleField::RequestBattleRestart()
{
    bRestartRequested = true;
}

void ARegimentBattleField::RequestMissionSetup()
{
    bSetupRequested = CanReturnToMissionSetup();
}

bool ARegimentBattleField::CanReturnToMissionSetup() const
{
    return Mission && Mission->TacticalScenario.has_value();
}

bool ARegimentBattleField::CanIntroduceReserve() const
{
    return Session && Session->MissionProgress && Session->MissionProgress->CanReleaseReserve(Session->Simulation);
}

void ARegimentBattleField::IntroduceReserve()
{
    if (CanIntroduceReserve())
        Session->MissionProgress->ReleaseReserve(Session->Simulation);
}

FText ARegimentBattleField::GetMissionObjectiveText() const
{
    return Session && Session->MissionProgress ? Session->MissionProgress->Status(Session->Simulation) : FText::GetEmpty();
}

FText ARegimentBattleField::GetMissionReport() const
{
    return Session && Session->MissionProgress ? Session->MissionProgress->Report() : FText::GetEmpty();
}

bool ARegimentBattleField::GetTutorialMarker(FVector& Center, float& Radius) const
{
    if (!Session || !Session->MissionProgress)
        return false;
    const auto Marker = Session->MissionProgress->TutorialMarker();
    if (!Marker)
        return false;
    Center = Session->WorldLocation(Marker->center, GetBattleOrigin());
    Radius = Marker->radius_m * CentimetersPerMeter;
    return true;
}

TConstArrayView<FRegimentDecisionHudEntry> ARegimentBattleField::GetDecisionEntries() const
{
    return Session ? TConstArrayView<FRegimentDecisionHudEntry>(Session->DecisionEntries)
                   : TConstArrayView<FRegimentDecisionHudEntry>{};
}

TConstArrayView<FRegimentBattleNotice> ARegimentBattleField::GetBattleNotices() const
{
    return Session ? Session->Notices.GetEntries() : TConstArrayView<FRegimentBattleNotice>{};
}

uint64 ARegimentBattleField::GetBattleNoticeTick() const
{
    return Session ? Session->Simulation.tick + uint64(Session->PostBattleTicks) : 0;
}

bool ARegimentBattleField::ProjectCommander(uint64 UnitId, FVector2D& Normalized) const
{
    if (!Session || UnitId == 0)
        return false;
    const auto* Unit = battle::FindUnit(Session->Simulation, {UnitId});
    auto* Controller = GetWorld()->GetFirstPlayerController();
    if (!Unit || !Unit->alive || !Controller)
        return false;
    int32 Width = 0, Height = 0;
    Controller->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0)
        return false;
    FVector2D Projected;
    const double LabelHeight = Session->CommandFigure(Unit->id) == ECommandFigure::General ? 320.0
                             : Unit->troop == battle::UnitTroop::Mounted ? 266.0 : 222.0;
    if (!Controller->ProjectWorldLocationToScreen(
            Session->WorldLocation(Unit->pose.position, GetBattleOrigin()) + FVector(0, 0, LabelHeight), Projected))
        return false;
    Normalized = Projected / FVector2D(Width, Height);
    return Normalized.X > .04 && Normalized.X < .96 && Normalized.Y > .06 && Normalized.Y < .96;
}

void ARegimentBattleField::ToggleSkirmishers(uint64 NodeId)
{
    if (!Session || Session->Simulation.outcome != battle::BattleOutcome::Running)
        return;
    const auto* Entry = Session->DecisionEntries.FindByPredicate(
        [NodeId](const auto& Decision) { return Decision.Node == NodeId; });
    if (!Entry || !Entry->bCanDetach || Entry->bSkirmisherOrderQueued)
        return;
    battle::CommandEnvelope Request;
    Request.tick = Session->Simulation.tick;
    Request.sequence = Session->NextCommandSequence++;
    Request.command.kind = battle::CommandKind::SetSkirmishers;
    Request.command.regiment = {Entry->Regiment};
    Request.command.skirmishers_enabled = !Entry->bSkirmishersEnabled;
    Session->PendingCommands.push_back(Request);
    Session->LastDecisionPresentation.Reset();
    RefreshDecisionEntries();
}

void ARegimentBattleField::RallyRegiment(uint64 NodeId)
{
    if (!Session || Session->Simulation.outcome != battle::BattleOutcome::Running)
        return;
    const auto* Entry = Session->DecisionEntries.FindByPredicate(
        [NodeId](const auto& Decision) { return Decision.Node == NodeId; });
    if (!Entry || !Entry->bCanRally)
        return;
    const auto* Regiment = battle::FindRegiment(Session->Simulation, {Entry->Regiment});
    if (!Regiment || Regiment->units.empty() || !Regiment->panic)
        return;
    battle::Order Order;
    Order.recipient = Regiment->id;
    Order.type = battle::OrderType::Rally;
    Order.priority = 120;
    Order.point = Regiment->rally_point;
    Order.created_tick = Session->Simulation.tick;
    Order.delivery_tick = Order.created_tick + FRegimentBattlePlayback::StepsPerSecond;
    if (battle::SubmitOrder(Session->Simulation, Order) == battle::Status::Ok)
    {
        Session->LastDecisionPresentation.Reset();
        RefreshDecisionEntries();
    }
}

void ARegimentBattleField::RefreshDecisionEntries()
{
    auto& Battle = Session->Simulation;
    if (!Battle.command_state ||
        (Session->LastDecisionPresentation && Battle.tick - Session->LastDecisionPresentation.GetValue() < 5))
        return;
    Session->LastDecisionPresentation = Battle.tick;
    Session->PlayerOrderJournal.Observe(Battle);
    RefreshRegimentCards();
    const auto& Command = *Battle.command_state;
    const auto FindNode = [&](battle::command::NodeId Id)
    {
        return std::find_if(Command.hierarchy.nodes.begin(), Command.hierarchy.nodes.end(),
            [&](const auto& Node) { return Node.id == Id; });
    };
    TMap<uint64, int32> EngagedRegiments;
    for (const auto& Leaf : Command.hierarchy.nodes)
        if (const auto* Regiment = battle::FindRegiment(Battle, Leaf.regiment);
            RegimentTacticalKnowledge::ShowsCommandNode(Leaf, 0) && Regiment && Regiment->melee_engaged)
        {
            ++EngagedRegiments.FindOrAdd(Leaf.id.value);
            for (auto Parent = FindNode(Leaf.parent); Parent != Command.hierarchy.nodes.end(); Parent = FindNode(Parent->parent))
                ++EngagedRegiments.FindOrAdd(Parent->id.value);
        }

    auto& Entries = Session->DecisionEntries;
    Entries.Reset(int32(Command.hierarchy.nodes.size()));
    for (const auto& Node : Command.hierarchy.nodes)
    {
        if (!RegimentTacticalKnowledge::ShowsCommandNode(Node, 0))
            continue;
        const auto Runtime = std::find_if(Command.nodes.begin(), Command.nodes.end(),
            [&](const auto& Item) { return Item.node == Node.id; });
        if (Runtime == Command.nodes.end())
            continue;
        auto& Entry = Entries.AddDefaulted_GetRef();
        Entry.Node = Node.id.value;
        Entry.Parent = Node.parent.value;
        Entry.Regiment = Node.regiment.value;
        Entry.Team = Node.team;
        using battle::command::Level;
        Entry.bGeneral = Node.level == Level::Army;
        Entry.CommandTier = Entry.bGeneral ? 3 : Node.level == Level::Regiment ? 1 : 2;
        Entry.Duty = Entry.bGeneral ? LOCTEXT("PickerArmy", "Генерал армии")
            : Node.level == Level::Regiment ? LOCTEXT("PickerRegiment", "Командир полка")
            : Node.level == Level::Part ? LOCTEXT("PickerPart", "Командир соединения")
            : LOCTEXT("PickerGroup", "Командир группы");
        if (Node.commander)
        {
            auto* Card = Session->CommanderCards.Find(Node.commander.value);
            if (!Card)
            {
                const FStringView EraId = Mission ? FStringView(Mission->GetPreset().Era.Id) : FStringView();
                Card = &Session->CommanderCards.Add(Node.commander.value,
                    RegimentCommanderProfiles::Describe(Battle, Node.commander, EraId));
            }
            Entry.CommanderName = FText::FromString(Card->Name);
            Entry.RankIcon = Card->RoleIcon;
            Entry.Portrait = Card->Portrait;
            Entry.Effects = Card->Effects;
            Entry.TacticalSkill = Card->TacticalSkill;
        }
        Entry.MeleeRegiments = EngagedRegiments.FindRef(Entry.Node);
        const auto Officer = std::find_if(Battle.officers.begin(), Battle.officers.end(),
            [&](const auto& Spec) { return Spec.id == Node.commander; });
        const auto* Commander = Officer == Battle.officers.end() ? nullptr : battle::FindUnit(Battle, Officer->unit);
        if (Commander && Commander->alive)
            Entry.Commander = Commander->id.value;

        const auto* Regiment = battle::FindRegiment(Battle, Node.regiment);
        const auto& Name = Node.name.empty() && Regiment ? Regiment->name : Node.name;
        if (!Name.empty())
            Entry.Name = FText::AsCultureInvariant(UTF8_TO_TCHAR(Name.c_str()));
        else
        {
            FText Level;
            switch (Node.level)
            {
                case battle::command::Level::Army: Level = LOCTEXT("ArmyLevel", "Армия"); break;
                case battle::command::Level::Part: Level = LOCTEXT("PartLevel", "Соединение"); break;
                case battle::command::Level::Group: Level = LOCTEXT("GroupLevel", "Группа"); break;
                default: Level = LOCTEXT("RegimentLevel", "Полк"); break;
            }
            Entry.Name = FText::Format(LOCTEXT("UnnamedCommand", "{0} {1}"), Level, Node.id.value);
        }
        const auto& Memory = Runtime->memory;
        Entry.State = Entry.Commander ? DecisionState(Memory.state) : LOCTEXT("CommanderLost", "Командир выбыл");
        using battle::command::FsmState;
        Entry.bPreferOrderState = Memory.state == FsmState::Failed || Memory.state == FsmState::Complete ||
            Memory.state == FsmState::Reserve;
        const auto* LastMelee = Regiment ? Session->RecentMelee.Find(Regiment->id.value) : nullptr;
        if (Entry.MeleeRegiments && Regiment)
        {
            Entry.Contact = LOCTEXT("RegimentMelee", "Ближний бой");
            if (LastMelee && LastMelee->Tick <= Battle.tick &&
                Battle.tick - LastMelee->Tick <= 2 * FRegimentBattlePlayback::StepsPerSecond)
                if (const auto* Opponent = battle::FindRegiment(Battle, LastMelee->Opponent))
                    Entry.Contact = FText::Format(LOCTEXT("RegimentMeleeOpponent", "Ближний бой · {0}"),
                        RegimentName(*Opponent));
        }
        else if (Entry.MeleeRegiments)
            Entry.Contact = FText::Format(LOCTEXT("SubordinateMelee", "Полков в ближнем бою · {0}"),
                Entry.MeleeRegiments);
        if (Entry.Commander)
        {
            using namespace RegimentTacticalKnowledge;
            const auto Latest = FindEnemyContact(Command, *Runtime, {}, Battle.tick);
            const auto* Observed = Latest ? battle::FindRegiment(Battle, Latest->Regiment) : nullptr;
            if (Observed)
            {
                const double Age = double(Battle.tick - Latest->ObservedTick) * FRegimentBattlePlayback::StepSeconds;
                const FText Elapsed = FText::AsCultureInvariant(FString::Printf(TEXT("%.1f с"), Age));
                const auto Reporter = FindNode(Latest->Reporter);
                if (Latest->Reporter && Reporter != Command.hierarchy.nodes.end())
                {
                    const FText Origin = Reporter->name.empty()
                        ? FText::Format(LOCTEXT("ReportNode", "Офицер №{0}"), Reporter->id.value)
                        : FText::AsCultureInvariant(UTF8_TO_TCHAR(Reporter->name.c_str()));
                    Entry.Intel = FText::Format(Latest->Kind == EContactKind::StaleReport
                        ? LOCTEXT("OldEnemyReport", "Устаревший доклад от {0} · {1} · наблюдение {2} назад")
                        : LOCTEXT("EnemyReport", "Доклад от {0} · {1} · наблюдение {2} назад"),
                        Origin, RegimentName(*Observed), Elapsed);
                }
                else
                    Entry.Intel = FText::Format(Latest->Kind == EContactKind::Direct
                        ? LOCTEXT("DirectEnemyContact", "Личное наблюдение · {0} · {1} назад")
                        : LOCTEXT("StaleEnemyContact", "Устаревшие сведения · {0} · наблюдение {1} назад"),
                        RegimentName(*Observed), Elapsed);
            }
            else
                Entry.Intel = LOCTEXT("NoKnownContact", "Нет сведений о противнике");
        }
        Entry.Detail = LOCTEXT("DecisionPending", "Командир ещё не принял решения.");
        if (!Entry.Commander)
            Entry.Detail = LOCTEXT("NoCurrentOrder", "Командир выбыл.");
        else if (!Memory.recent_decisions.empty())
        {
            const auto& Decision = Memory.recent_decisions.back();
            const FText Reason = DecisionReason(Decision.explanation);
            Entry.Action = DecisionAction(Decision.selected.action);
            const auto TargetId = Memory.target ? Memory.target : Decision.selected.target_regiment;
            const bool bTargetKnown = !TargetId ||
                bool(RegimentTacticalKnowledge::FindEnemyContact(Command, *Runtime, TargetId, Battle.tick));
            Entry.Detail = bTargetKnown ? FText::Format(LOCTEXT("DecisionExplanation", "{0}\nТочка назначения  {1}"),
                Reason, BattlePoint(Memory.goal)) : Reason;
            if (const auto* Target = bTargetKnown && TargetId ? battle::FindRegiment(Battle, TargetId) : nullptr)
                Entry.Detail = FText::Format(LOCTEXT("DecisionWithTarget", "{0}\nНазначенная цель  {1}"),
                    Entry.Detail, RegimentName(*Target));
            if (Memory.recent_decisions.size() > 1)
            {
                const auto& Previous = Memory.recent_decisions[Memory.recent_decisions.size() - 2];
                Entry.Detail = FText::Format(LOCTEXT("DecisionWithHistory", "{0}\nДо этого  {1}"),
                    Entry.Detail, DecisionAction(Previous.selected.action));
            }
        }
        if (!Entry.MeleeRegiments && LastMelee && LastMelee->Tick <= Battle.tick)
            if (const auto* Opponent = battle::FindRegiment(Battle, LastMelee->Opponent))
            {
                const double Age = double(Battle.tick - LastMelee->Tick) * FRegimentBattlePlayback::StepSeconds;
                const FText Elapsed = FText::AsCultureInvariant(FString::Printf(TEXT("%.1f с"), Age));
                Entry.Detail = FText::Format(LOCTEXT("LastRegimentMelee", "{0}\nПоследняя рукопашная атака · {1} · {2} назад"),
                    Entry.Detail, RegimentName(*Opponent), Elapsed);
            }
        if (Regiment && Battle.scenario.rules.regiment_tactics)
        {
            const auto IsRally = [Regiment](const battle::Order& Order)
            {
                return Order.recipient == Regiment->id && Order.type == battle::OrderType::Rally;
            };
            Entry.bCanRally = Node.team == 0 && !Regiment->units.empty() && Regiment->panic &&
                Battle.outcome == battle::BattleOutcome::Running &&
                !(Regiment->order && Regiment->order->status == battle::OrderStatus::Active && IsRally(*Regiment->order)) &&
                !std::ranges::any_of(Battle.pending_orders, IsRally);
            const bool bDeploying = battle::IsDeploying(Battle, *Regiment);
            const FText Status = Regiment->units.empty() ? LOCTEXT("RegimentDefeated", "Полк разбит")
                : Regiment->panic ? LOCTEXT("RegimentRouted", "Паника · отход")
                : bDeploying ? LOCTEXT("BattleDeployment", "Колонна развёртывается")
                : Regiment->disordered ? LOCTEXT("RegimentDisordered", "Строй смешался")
                : LOCTEXT("RegimentOrdered", "Полк в строю");
            if (Regiment->panic || bDeploying || Regiment->disordered || Regiment->units.empty())
                Entry.Contact = Status;
            Entry.Detail = FText::Format(LOCTEXT("RegimentTacticalCondition",
                "{0}\n{1}\nДух {2}% · строй {3}% · усталость {4}%"), Entry.Detail, Status,
                FMath::RoundToInt(Regiment->morale * 100.f), FMath::RoundToInt(Regiment->cohesion * 100.f),
                FMath::RoundToInt(Regiment->fatigue * 100.f));
            if (const auto* Incoming = Session->IncomingStrikes.Find(Regiment->id.value))
            {
                const FText Direction = StrikeDirection(Incoming->Aspect);
                if (const auto* Opponent = battle::FindRegiment(Battle, Incoming->Contact.Opponent))
                    Entry.Detail = FText::Format(LOCTEXT("IncomingStrike", "{0}\n{1} · {2}"),
                        Entry.Detail, Direction, RegimentName(*Opponent));
            }
            if (const int32 Hits = Session->FriendlyHits.FindRef(Regiment->id.value); Hits > 0)
                Entry.Detail = FText::Format(LOCTEXT("FriendlyVolleyHits", "{0}\nПопаданий своих стрел · {1}"), Entry.Detail, Hits);
        }
        for (const auto& Task : Runtime->sent_tasks)
        {
            const auto Recipient = FindNode(Task.recipient);
            if (Recipient == Command.hierarchy.nodes.end())
                continue;
            const FText RecipientName = Recipient->name.empty()
                ? FText::Format(LOCTEXT("NumberedSubordinate", "Подчинённый {0}"), Recipient->id.value)
                : FText::AsCultureInvariant(UTF8_TO_TCHAR(Recipient->name.c_str()));
            Entry.Detail = FText::Format(LOCTEXT("AssignedDecision", "{0}\n→ {1} · {2}"),
                Entry.Detail, RecipientName, DecisionAction(Task.action));
            if (Task.intent.flank_waypoint && (!Task.intent.target_regiment ||
                RegimentTacticalKnowledge::FindEnemyContact(Command, *Runtime, Task.intent.target_regiment, Battle.tick)))
                Entry.Detail = FText::Format(LOCTEXT("AssignedFlankPoint", "{0} · обход через {1}"),
                    Entry.Detail, BattlePoint(Task.intent.flank_waypoint->position));
        }
        if (Regiment && Regiment->skirmisher_count > 0)
        {
            Entry.bCanDetach = Session->bCampaign && Node.team == 0 && Entry.Commander != 0 &&
                Battle.outcome == battle::BattleOutcome::Running;
            int32 ScreenOutside = 0;
            for (auto Id : Regiment->units)
                if (const auto* Unit = battle::FindUnit(Battle, Id); Unit && Unit->alive && Unit->skirmisher)
                {
                    ++Entry.Skirmishers;
                    if (IsOutsideFormation(*Unit, *Regiment))
                        ++ScreenOutside;
                }
            if (Entry.Skirmishers > 0)
                Entry.Detail = FText::Format(LOCTEXT("ScreenPosition", "{0}\nЗастрельщики вне строя  {1} / {2}"),
                    Entry.Detail, ScreenOutside, Entry.Skirmishers);
            Entry.bSkirmishersEnabled = Regiment->skirmishers_enabled;
            Entry.bSkirmisherOrderQueued = std::any_of(
                Session->PendingCommands.begin(), Session->PendingCommands.end(),
                [&](const auto& Packet) { return Packet.command.regiment == Node.regiment; });
        }
    }
}

bool ARegimentBattleField::CanPossessOfficer(uint64 NodeId) const
{
    if (!Session || Session->IsOpening() || Session->Simulation.outcome != battle::BattleOutcome::Running)
        return false;
    const auto Candidate = RegimentOfficerOrders::FindOfficer(Session->Simulation, NodeId);
    if (!Candidate || Candidate == Session->Simulation.controlled)
        return false;
    const auto* Officer = battle::FindUnit(Session->Simulation, Candidate);
    return Officer && (!Session->MissionProgress || !Session->MissionProgress->AwaitsReserveRelease(Officer->regiment));
}

bool ARegimentBattleField::IsChoosingOfficer() const
{
    return Session && Session->Playback.IsSelectingOfficer();
}

void ARegimentBattleField::OpenOfficerPicker(uint64 FocusNode)
{
    if (!Session || Session->IsOpening() || IsChoosingOfficer() ||
        Session->Simulation.outcome != battle::BattleOutcome::Running)
        return;
    Session->LastDecisionPresentation.Reset();
    RefreshDecisionEntries();
    if (!Session->DecisionEntries.ContainsByPredicate([this](const auto& Entry)
        { return Entry.Commander && CanPossessOfficer(Entry.Node); }))
        return;
    ++OfficerPickerRevision;
    PreferredOfficerNode = FocusNode;
    if (PreviousBattleView.IsValid() && Session->Simulation.controlled)
    {
        if (auto* Player = GetWorld()->GetFirstPlayerController())
        {
            Session->PickerPreviousView = Player->GetViewTarget();
            Player->SetViewTarget(PreviousBattleView.Get());
        }
    }
    Session->Playback.SetOfficerSelection(true);
    Session->Audio.SetPaused(true);
    ClearOfficerDirectInput();
    SetOfficerMouseCapture(false);
}

void ARegimentBattleField::CloseOfficerPicker()
{
    if (!IsChoosingOfficer())
        return;
    Session->Playback.SetOfficerSelection(false);
    Session->Audio.SetPaused(IsBattlePaused());
    if (Session->PickerPreviousView.IsValid())
        if (auto* Player = GetWorld()->GetFirstPlayerController())
            Player->SetViewTarget(Session->PickerPreviousView.Get());
    Session->PickerPreviousView.Reset();
    PreferredOfficerNode = 0;
    SetOfficerMouseCapture(Session->Simulation.controlled && CanDirectControlOfficer());
    if (FSlateApplication::IsInitialized())
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

void ARegimentBattleField::ChooseOfficer(uint64 NodeId)
{
    if (!IsChoosingOfficer() || !NodeId || !CanPossessOfficer(NodeId))
        return;
    CloseOfficerPicker();
    PossessOfficer(NodeId);
}

void ARegimentBattleField::PossessOfficer(uint64 NodeId)
{
    if (!CanPossessOfficer(NodeId))
        return;
    if (Mission)
        Mission->StopMapFlyover();
    const auto Candidate = RegimentOfficerOrders::FindOfficer(Session->Simulation, NodeId);
    std::erase_if(Session->PendingCommands, ChangesControlledUnit);
    battle::Command Command;
    Command.kind = battle::CommandKind::AcquireControl;
    Command.unit = Candidate;
    Session->QueueCommand(Command);
    ShowOfficerNotice(IsBattlePaused()
        ? LOCTEXT("PossessWhilePaused", "Вселение при продолжении боя.")
        : LOCTEXT("PossessQueued", "Переключаюсь на офицера."));
}

void ARegimentBattleField::ShowOfficerNotice(FText Notice)
{
    OfficerHudState.Notice = MoveTemp(Notice);
    OfficerNoticeSeconds = OfficerHudState.Notice.IsEmpty() ? 0.f : 4.f;
}

void ARegimentBattleField::IssueOfficerOrder(ERegimentPlayerOrder Order, bool bAimAtCursor)
{
    if (!Session || !Session->Simulation.controlled)
        return;
    auto& World = Session->Simulation;
    if (Order == ERegimentPlayerOrder::Release)
    {
        ClearOfficerDirectInput();
        Session->PlotOrder.Reset();
        Session->DragAnchor.Reset();
        Session->FieldLines.Reset();
        OfficerHudState.bPlanning = false;
        std::erase_if(Session->PendingCommands, ChangesControlledUnit);
        battle::Command Command;
        Command.kind = battle::CommandKind::ReleaseControl;
        Command.unit = World.controlled;
        Session->QueueCommand(Command);
        ShowOfficerNotice(IsBattlePaused()
            ? LOCTEXT("ReleaseWhilePaused", "Выход при продолжении боя.")
            : LOCTEXT("ReleaseQueued", "Возвращаюсь к свободной камере."));
        return;
    }
    if (!OfficerHudState.bCanCommand)
        return;
    if (Order == ERegimentPlayerOrder::Pace)
    {
        switch (Session->Pace)
        {
        case battle::MovementPace::Walk: Session->Pace = battle::MovementPace::March; break;
        case battle::MovementPace::March: Session->Pace = battle::MovementPace::Run; break;
        case battle::MovementPace::Run: Session->Pace = battle::MovementPace::Walk; break;
        }
        return;
    }
    if (Order == ERegimentPlayerOrder::FireSector)
    {
        Session->bFireSector = !Session->bFireSector;
        const auto* Regiment = battle::FindRegiment(World, {SelectedOfficerRegiment});
        ShowOfficerNotice(!Session->bFireSector ? FText::GetEmpty()
            : Regiment && FireSectorReach(World, *Regiment) > 0.f
                ? LOCTEXT("FireSectorVisible", "Жёлтый — огонь, синий — свои, фиолетовый — рукопашная, красный — преграда, оранжевый — риск или укрытие.")
                : LOCTEXT("NoReadyArchers", "В полку сейчас нет стрелков, готовых к стрельбе."));
        return;
    }
    if (Order == ERegimentPlayerOrder::NextRegiment)
    {
        const auto Regiments = RegimentOfficerOrders::Regiments(World, World.controlled);
        if (Regiments.empty())
            return;
        auto Selected = std::ranges::find(Regiments, battle::RegimentId{SelectedOfficerRegiment});
        if (Selected == Regiments.end() || ++Selected == Regiments.end())
            Selected = Regiments.begin();
        if (SelectedOfficerRegiment != Selected->value)
            Session->DragAnchor.Reset();
        SelectedOfficerRegiment = Selected->value;
        ShowOfficerNotice(FText::GetEmpty());
        return;
    }
    if (Order == ERegimentPlayerOrder::CommandHold || Order == ERegimentPlayerOrder::WingHold)
    {
        if (OfficerHudState.CommandTier < (Order == ERegimentPlayerOrder::WingHold ? 3 : 2))
            return;
        auto Recipients = RegimentOfficerOrders::HoldRecipients(World, {SelectedOfficerRegiment}, Order == ERegimentPlayerOrder::WingHold);
        if (Session->MissionProgress)
            std::erase_if(Recipients, [&](const auto Id) { return Session->MissionProgress->AwaitsReserveRelease(Id); });
        if (const auto Notice = RegimentOfficerOrders::QueueStaffHold(World, Recipients,
            Session->PendingCommands, Session->NextCommandSequence))
        {
            ShowOfficerNotice(*Notice);
            return;
        }
        int32 Issued = 0;
        for (const auto Id : Recipients)
        {
            const auto Before = Session->NextCommandSequence;
            RegimentOfficerOrders::QueueOrder(World, *Session->Navigation, Id, ERegimentPlayerOrder::Hold, {},
                Session->PendingCommands, Session->NextCommandSequence);
            Issued += Session->NextCommandSequence != Before;
        }
        ShowOfficerNotice(FText::Format(LOCTEXT("HoldRecipients", "Удерживать позицию · приказ передан полкам: {0}"), Issued));
        return;
    }
    if (Session->MissionProgress && Session->MissionProgress->AwaitsReserveRelease({SelectedOfficerRegiment}))
    {
        ShowOfficerNotice(Mission && Mission->GetPreset().Tutorial == ERegimentTutorial::Cavalry
            ? LOCTEXT("AcademyReserveWaits", "Пехота остаётся в резерве до завершения кавалерийского этапа.")
            : LOCTEXT("ReserveWaitsForExerciseOrder", "Нажмите «Ввести резерв» и дождитесь доставки приказа."));
        return;
    }
    if ((Order == ERegimentPlayerOrder::Attack || Order == ERegimentPlayerOrder::Pursue) &&
        Session->MissionProgress && Session->MissionProgress->HidesRegiment({MissionRegimentsPerTeam + 2}))
    {
        ShowOfficerNotice(LOCTEXT("AcademyAttackAfterTargets", "Полк противника появится после стрельбы по мишеням."));
        return;
    }
    const auto* Regiment = battle::FindRegiment(World, {SelectedOfficerRegiment});
    if (Order == ERegimentPlayerOrder::GroupManeuver && !OfficerHudState.bAdditionalRegiments)
    {
        ShowOfficerNotice(LOCTEXT("GroupOfficerRequired", "Возьмите управление командиром группы или армии."));
        return;
    }
    if (Order == ERegimentPlayerOrder::Maneuver || Order == ERegimentPlayerOrder::GroupManeuver ||
        Order == ERegimentPlayerOrder::Cover ||
        (Order == ERegimentPlayerOrder::Withdraw && Regiment && !Regiment->panic && !Regiment->melee_engaged))
    {
        if (Session->PlotOrder == Order)
            Session->PlotOrder.Reset();
        else
            Session->PlotOrder = Order;
        Session->DragAnchor.Reset();
        OfficerHudState.bPlanning = Session->PlotOrder.IsSet();
        if (OfficerHudState.bPlanning)
        {
            OfficerInput.bCommandsOpen = false;
            ClearOfficerDirectInput();
            SetOfficerMouseCapture(false);
        }
        ShowOfficerNotice(!OfficerHudState.bPlanning ? FText::GetEmpty() :
            Order == ERegimentPlayerOrder::Cover ? LOCTEXT("ChooseFortification", "Выберите редут, стену или изгородь.")
            : Order == ERegimentPlayerOrder::Withdraw ? LOCTEXT("ChooseFallback", "Укажите рубеж отхода позади полка.")
            : Order == ERegimentPlayerOrder::GroupManeuver ? LOCTEXT("ChooseGroupMove", "Щелчок задаёт точку. Полки сохранят интервалы и направление.")
            : LOCTEXT("ChooseMove", "Щелчок задаёт точку. Перетягивание задаёт линию фронта."));
        return;
    }
    std::optional<battle::Vec3> Cursor;
    if ((Order == ERegimentPlayerOrder::Attack || Order == ERegimentPlayerOrder::Pursue) && bAimAtCursor)
        if (auto* Controller = GetWorld()->GetFirstPlayerController())
        {
            FVector Origin, Ray;
            bool bHasRay = true;
            if (bOfficerMouseCaptured && OfficerCamera.IsValid())
            {
                FRotator Rotation;
                Controller->GetPlayerViewPoint(Origin, Rotation);
                Ray = Rotation.Vector();
            }
            else
                bHasRay = Controller->DeprojectMousePositionToWorld(Origin, Ray);
            if (bHasRay)
                if (const auto Hit = RegimentGroundPoint(Origin, Ray, GetBattleOrigin(), World.scenario.hill))
                {
                    const FVector Point = (Hit.GetValue() - GetBattleOrigin()) / CentimetersPerMeter;
                    Cursor = battle::Vec3{float(Point.X), float(Point.Y), 0.f};
                }
        }
    ShowOfficerNotice(RegimentOfficerOrders::QueueOrder(World, *Session->Navigation,
        {SelectedOfficerRegiment}, Order, Cursor, Session->PendingCommands, Session->NextCommandSequence));
}

TConstArrayView<FRegimentFieldLine> ARegimentBattleField::GetFieldLines() const
{
    return Session ? TConstArrayView<FRegimentFieldLine>(Session->FieldLines) : TConstArrayView<FRegimentFieldLine>();
}

TConstArrayView<FRegimentFieldCard> ARegimentBattleField::GetRegimentCards() const
{
    return Session ? TConstArrayView<FRegimentFieldCard>(Session->FieldCards) : TConstArrayView<FRegimentFieldCard>();
}

bool ARegimentBattleField::IsCardOfficerQueued(uint64 Id) const
{
    if (!Session)
        return false;
    const auto Officer = RegimentOfficerOrders::PendingOfficer(Session->Simulation, Session->PendingCommands);
    const auto* Unit = Officer ? battle::FindUnit(Session->Simulation, Officer) : nullptr;
    return Unit && Unit->regiment.value == Id;
}

const FRegimentDecisionHudEntry* ARegimentBattleField::GetPendingOfficerPreview() const
{
    if (!Session)
        return nullptr;
    const auto Officer = RegimentOfficerOrders::PendingOfficer(Session->Simulation, Session->PendingCommands);
    return Officer ? Session->DecisionEntries.FindByPredicate([Officer](const auto& Entry)
        { return Entry.Commander == Officer.value; }) : nullptr;
}

void ARegimentBattleField::EnterRegimentCardOfficer(uint64 Id)
{
    if (!Session || Session->IsOpening()) return;
    const auto* Card = Session->FieldCards.FindByPredicate([Id](const auto& Row) { return Row.Id == Id; });
    if (Card && Card->bCanEnterOfficer)
        OpenOfficerPicker(Card->Command);
}

void ARegimentBattleField::SelectRegimentCard(uint64 Id)
{
    if (!Session)
        return;
    const auto* Card = Session->FieldCards.FindByPredicate([Id](const auto& Row) { return Row.Id == Id; });
    if (!Card || Card->Living == 0)
        return;
    const auto& World = Session->Simulation;
    if (!World.controlled)
    {
        EnterRegimentCardOfficer(Id);
        return;
    }
    if (!battle::CanCommandRegiment(World, World.controlled, {Id}))
    {
        ShowOfficerNotice(LOCTEXT("CardRegimentOutsideCommand", "Этот полк не подчиняется выбранному офицеру."));
        return;
    }
    SelectedOfficerRegiment = Id;
    Session->DragAnchor.Reset();
    Session->NextFieldRefresh = 0.0;
}

void ARegimentBattleField::RefreshRegimentCards()
{
    auto& World = Session->Simulation;
    auto& Cards = Session->FieldCards;
    Cards.Reset();
    const FVector Origin = GetBattleOrigin();
    for (const auto& Regiment : World.regiments)
    {
        if (Regiment.team != 0)
            continue;
        auto& Card = Cards.AddDefaulted_GetRef();
        Card.Id = Regiment.id.value;
        Card.Name = RegimentName(Regiment);
        Card.Living = int32(Regiment.units.size());
        Card.bInCommand = World.controlled && battle::CanCommandRegiment(World, World.controlled, Regiment.id);
        Card.Starting = int32(Regiment.initial_count);
        Card.Morale = FMath::RoundToInt(100 * Regiment.morale);
        Card.Fatigue = FMath::RoundToInt(100 * Regiment.fatigue);
        Card.Location = Session->WorldLocation(Regiment.pose.position, Origin) + FVector(0, 0, 330);
        Card.bDisordered = Regiment.disordered || Regiment.panic;
        Card.bContact = Regiment.melee_engaged;
        Card.Activity = RegimentOfficerOrders::DescribeOrder(World, Regiment, {}, false).Activity;
        Card.FlagState = Session->Feedback.FlagState(Regiment, World.tick);
        if (World.command_state)
        {
            const auto& Nodes = World.command_state->hierarchy.nodes;
            const auto Node = std::ranges::find(Nodes, Regiment.id, &battle::command::CommandNode::regiment);
            if (Node != Nodes.end())
            {
                Card.Command = Node->id.value;
                Card.bCanEnterOfficer = CanPossessOfficer(Card.Command) &&
                    bool(FRegimentFieldFeedback::CardOfficer(World, Regiment.id, Node->id.value));
            }
        }
        for (auto Id : Regiment.units)
            if (const auto* Unit = battle::FindUnit(World, Id); Unit && Unit->alive && Unit->ammunition != 0)
            {
                Card.bFiniteAmmo = true;
                Card.Ammo += int32(Unit->ammunition_remaining);
            }
    }
}

void ARegimentBattleField::UpdateManeuverInput(APlayerController& Controller)
{
    auto& World = Session->Simulation;
    if (!OfficerHudState.bCanCommand)
        return;
    const auto* Regiment = battle::FindRegiment(World, {SelectedOfficerRegiment});
    if (!Regiment)
        return;

    if (Controller.WasInputKeyJustPressed(EKeys::Escape))
    {
        Session->PlotOrder.Reset();
        Session->DragAnchor.Reset();
        Session->FieldLines.Reset();
        OfficerHudState.bPlanning = false;
        return;
    }
    const bool bStart = Session->PlotOrder && Controller.WasInputKeyJustPressed(EKeys::LeftMouseButton);
    const bool bRelease = Session->PlotOrder && Session->DragAnchor &&
        Controller.WasInputKeyJustReleased(EKeys::LeftMouseButton);
    const double Now = FPlatformTime::Seconds();
    if (!bStart && !bRelease && Now < Session->NextFieldRefresh)
        return;
    Session->NextFieldRefresh = Now + .12;
    const FVector BattleOrigin = GetBattleOrigin();
    const auto GroundAt = [&](battle::Vec3 Point)
    {
        return Session->WorldLocation(Point, BattleOrigin) + FVector(0, 0, 15);
    };
    const auto MarkConflict = [&](const RegimentOfficerOrders::FManeuverRoute& Route)
    {
        if (Route.TrafficConflict)
        {
            const auto P = Route.TrafficConflict->Position;
            for (float Sign : {-1.f, 1.f})
            {
                const battle::Vec3 Arm{.7f, Sign * .7f, 0.f};
                Session->FieldLines.Add({GroundAt(P - Arm), GroundAt(P + Arm), ERegimentFieldLineKind::Blocked});
            }
        }
    };
    Session->FieldLines.Reset();

    if (Session->PlotOrder)
    {
        FVector RayOrigin, Ray;
        const auto Hit = Controller.DeprojectMousePositionToWorld(RayOrigin, Ray)
            ? RegimentGroundPoint(RayOrigin, Ray, BattleOrigin, World.scenario.hill) : TOptional<FVector>();
        if (!Hit)
        {
            if (bRelease)
                Session->DragAnchor.Reset();
            return;
        }
        const FVector Cursor = (*Hit - BattleOrigin) / CentimetersPerMeter;
        battle::Vec3 Point{float(Cursor.X), float(Cursor.Y), 0.f};
        const auto Mode = *Session->PlotOrder;
        if (Mode == ERegimentPlayerOrder::Cover)
        {
            const battle::Obstacle* Cover = nullptr;
            float Best = 3.f;
            for (const auto& Box : World.scenario.obstacles)
            {
                using T = battle::TerrainFeature;
                if (Box.feature != T::StoneWall && Box.feature != T::Hedge && Box.feature != T::RedoubtInterior)
                    continue;
                const float Distance = std::hypot(Point.x - FMath::Clamp(Point.x, Box.min_x, Box.max_x),
                    Point.y - FMath::Clamp(Point.y, Box.min_y, Box.max_y));
                if (Box.feature == T::RedoubtInterior && Distance == 0.f)
                {
                    Cover = &Box;
                    break;
                }
                if (Distance < Best)
                {
                    Cover = &Box;
                    Best = Distance;
                }
            }
            const auto Size = battle::Footprint(*Regiment);
            const auto Position = Cover ? battle::TerrainCoverPosition(*Cover, {Point, Regiment->pose.yaw},
                Size.depth_m * .5f, Size.width_m * .5f) : std::nullopt;
            if (!Position)
            {
                Session->FieldLines.Add({GroundAt(Regiment->pose.position), GroundAt(Point), ERegimentFieldLineKind::Blocked});
                if (bRelease)
                {
                    Session->DragAnchor.Reset();
                    ShowOfficerNotice(Cover && Cover->feature == battle::TerrainFeature::RedoubtInterior
                        ? LOCTEXT("RedoubtTooSmall", "Полк не помещается в редуте. Сузьте строй или выберите меньший отряд.")
                        : LOCTEXT("NoFortificationHere", "Выберите площадку редута, край стены или изгороди."));
                }
                if (bStart)
                    Session->DragAnchor = Point;
                return;
            }
            Point = Position->position;
            const auto Front = battle::Right(Position->yaw);
            auto Routes = RegimentOfficerOrders::PreviewManeuver(World, *Session->Navigation,
                Regiment->id, Point, {}, false);
            if (!Routes.empty())
            {
                auto& Route = Routes.front();
                Route.Goal = *Position;
                auto Path = battle::FindFormationRoute(*Session->Navigation, World, *Regiment, *Position);
                Route.bReachable = !Regiment->panic && !Regiment->melee_engaged &&
                    !battle::IsDeploying(World, *Regiment) && Path.status == battle::Status::Ok &&
                    (!Session->MissionProgress || !Session->MissionProgress->AwaitsReserveRelease(Regiment->id));
                Route.Path = MoveTemp(Path.waypoints);
                RegimentOfficerOrders::CheckManeuverConflicts(World, *Session->Navigation, Routes,
                    battle::OrderType::MoveTo, Session->Pace, Session->PendingCommands);
                auto Previous = Route.Start.position;
                MarkConflict(Route);
                if (Route.bReachable || Route.TrafficConflict)
                    for (const auto& Waypoint : Route.Path)
                    {
                        Session->FieldLines.Add({GroundAt(Previous), GroundAt(Waypoint.position),
                            ERegimentFieldLineKind::Route});
                        Previous = Waypoint.position;
                    }
                Session->FieldLines.Add({GroundAt(Previous), GroundAt(Point),
                    Route.bReachable ? ERegimentFieldLineKind::Route : ERegimentFieldLineKind::Blocked});
                const auto Side = Front * (Size.width_m * .5f);
                Session->FieldLines.Add({GroundAt(Point - Side), GroundAt(Point + Side),
                    Route.bReachable ? ERegimentFieldLineKind::Front : ERegimentFieldLineKind::Blocked});
                if (bRelease)
                {
                    ShowOfficerNotice(RegimentOfficerOrders::QueueManeuver(World, Routes, battle::OrderType::MoveTo,
                        Session->Pace, Session->PendingCommands, Session->NextCommandSequence, *Session->Navigation));
                    Session->DragAnchor.Reset();
                    if (Route.bReachable)
                        Session->PlotOrder.Reset();
                }
            }
            if (bStart)
                Session->DragAnchor = Point;
            OfficerHudState.bPlanning = Session->PlotOrder.IsSet();
            return;
        }

        if (bStart)
            Session->DragAnchor = Point;
        const auto Begin = Session->DragAnchor.Get(Point);
        auto Routes = RegimentOfficerOrders::PreviewManeuver(World, *Session->Navigation, Regiment->id,
            Mode == ERegimentPlayerOrder::Withdraw ? Point : Begin,
            Session->DragAnchor && Mode != ERegimentPlayerOrder::Withdraw ? std::optional{Point} : std::nullopt,
            Mode == ERegimentPlayerOrder::GroupManeuver, Mode == ERegimentPlayerOrder::Withdraw);
        if (Mode == ERegimentPlayerOrder::Withdraw)
            for (auto& Route : Routes)
                if (battle::Dot(Route.Goal.position - Route.Start.position, battle::Forward(Route.Start.yaw)) >= 0.f)
                    Route.bReachable = false;
        if (Session->MissionProgress)
            for (auto& Route : Routes)
                if (Session->MissionProgress->AwaitsReserveRelease(Route.Id))
                    Route.bReachable = false;
        const auto Type = Mode == ERegimentPlayerOrder::Withdraw ? battle::OrderType::FallBack : battle::OrderType::MoveTo;
        RegimentOfficerOrders::CheckManeuverConflicts(World, *Session->Navigation, Routes, Type,
            Session->Pace, Session->PendingCommands);
        for (const auto& Route : Routes)
        {
            auto Previous = Route.Start.position;
            MarkConflict(Route);
            if (Route.bReachable || Route.TrafficConflict)
                for (const auto& Waypoint : Route.Path)
                {
                    Session->FieldLines.Add({GroundAt(Previous), GroundAt(Waypoint.position),
                        ERegimentFieldLineKind::Route});
                    Previous = Waypoint.position;
                }
            Session->FieldLines.Add({GroundAt(Previous), GroundAt(Route.Goal.position),
                Route.bReachable ? ERegimentFieldLineKind::Route : ERegimentFieldLineKind::Blocked});
            const auto* Member = battle::FindRegiment(World, Route.Id);
            const float Width = Route.Columns ? float(*Route.Columns - 1) * Member->formation.spacing_m +
                2.f * Member->formation.body_radius_m :
                battle::Footprint(*Member).width_m;
            const auto Side = battle::Right(Route.Goal.yaw) * (Width * .5f);
            Session->FieldLines.Add({GroundAt(Route.Goal.position - Side), GroundAt(Route.Goal.position + Side),
                Route.bReachable ? ERegimentFieldLineKind::Front : ERegimentFieldLineKind::Blocked});
            Session->FieldLines.Add({GroundAt(Route.Goal.position),
                GroundAt(Route.Goal.position + battle::Forward(Route.Goal.yaw) * 2.f),
                Route.bReachable ? ERegimentFieldLineKind::Front : ERegimentFieldLineKind::Blocked});
        }
        if (bRelease)
        {
            ShowOfficerNotice(RegimentOfficerOrders::QueueManeuver(World, Routes, Type, Session->Pace,
                Session->PendingCommands, Session->NextCommandSequence, *Session->Navigation));
            Session->DragAnchor.Reset();
            if (!Routes.empty() && std::ranges::all_of(Routes, [](const auto& Route) { return Route.bReachable; }))
                Session->PlotOrder.Reset();
        }
        OfficerHudState.bPlanning = Session->PlotOrder.IsSet();
        return;
    }

    auto Previous = Regiment->pose.position;
    for (size_t Index = Regiment->waypoint; Index < Regiment->route.size(); ++Index)
    {
        const auto Point = Regiment->route[Index].position;
        Session->FieldLines.Add({GroundAt(Previous), GroundAt(Point), ERegimentFieldLineKind::Route});
        Previous = Point;
    }
    for (const auto& Envelope : Session->PendingCommands)
        if (const auto& Command = Envelope.command; Command.kind == battle::CommandKind::IssueOrder &&
            Command.regiment == Regiment->id && (Command.order.type == battle::OrderType::MoveTo ||
                Command.order.type == battle::OrderType::FallBack))
            Session->FieldLines.Add({GroundAt(Previous), GroundAt(Command.order.point), ERegimentFieldLineKind::Front});
    if (Session->bFireSector)
    {
        if (Session->SectorTick != World.tick || Session->SectorRegiment != Regiment->id)
        {
            Session->SectorSamples = battle::SampleFireSector(World, *Regiment);
            Session->SectorTick = World.tick;
            Session->SectorRegiment = Regiment->id;
        }
        for (const auto& Segment : Session->SectorSamples)
        {
            auto Kind = ERegimentFieldLineKind::Fire;
            switch (Segment.mask)
            {
            case battle::FireLaneMask::Friendly: Kind = ERegimentFieldLineKind::FireFriendly; break;
            case battle::FireLaneMask::Melee: Kind = ERegimentFieldLineKind::FireMelee; break;
            case battle::FireLaneMask::Terrain: Kind = ERegimentFieldLineKind::FireBlocked; break;
            case battle::FireLaneMask::Cover:
            case battle::FireLaneMask::Risky: Kind = ERegimentFieldLineKind::FireRisk; break;
            default: break;
            }
            Session->FieldLines.Add({GroundAt(Segment.from), GroundAt(Segment.to), Kind});
        }
    }
}

void ARegimentBattleField::SetOfficerMouseCapture(bool bCapture)
{
    if (bOfficerMouseCaptured == bCapture)
        return;
    auto* Controller = GetWorld()->GetFirstPlayerController();
    const auto* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    if (!Player || !Player->ViewportClient)
        return;

    bOfficerMouseCaptured = bCapture;
    Controller->bShowMouseCursor = !bCapture;
    if (bCapture)
    {
        FInputModeGameOnly Mode;
        Mode.SetConsumeCaptureMouseDown(false);
        Controller->SetInputMode(Mode);
    }
    else
    {
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(Player->ViewportClient->GetGameViewportWidget());
        Mode.SetHideCursorDuringCapture(false);
        Controller->SetInputMode(Mode);
    }
}

void ARegimentBattleField::ToggleOfficerCommands()
{
    if (!Session || !OfficerHudState.bActive)
        return;
    OfficerInput.ToggleCommands(Session->PlotOrder.IsSet());
    ClearOfficerDirectInput();
    SetOfficerMouseCapture(CanDirectControlOfficer());
}

void ARegimentBattleField::ToggleOfficerView()
{
    if (!OfficerHudState.bActive || OfficerHudState.bPlanning)
        return;
    auto& Settings = RegimentOfficerSettings();
    Settings.SetFirstPerson(!Settings.IsFirstPerson());
    SaveRegimentOfficerSettings();
    OfficerCameraPitch = RegimentOfficerView::Pitch(OfficerCameraPitch, Settings.IsFirstPerson());
    ClearOfficerDirectInput();
    bOfficerPresentationDirty = true;
}

bool ARegimentBattleField::CanDirectControlOfficer() const
{
    return !OfficerCommandsOpen() && !Session->PlotOrder && !IsBattlePaused();
}

void ARegimentBattleField::ClearOfficerDirectInput()
{
    OfficerMotionTap = FIntVector(0, 0, 0);
    bOfficerRunTap = false;
    OfficerInput.ClearActions();
}

void ARegimentBattleField::QueueOfficerActions(float AimYaw, bool bDirect)
{
    const auto Actions = OfficerInput.ConsumeActions(bDirect);
    const auto& World = Session->Simulation;
    if (Actions.bJump)
    {
        battle::Command Jump;
        Jump.kind = battle::CommandKind::JumpControlledUnit;
        Jump.unit = World.controlled;
        Session->QueueCommand(Jump);
    }
    if (Actions.bParry)
    {
        battle::Command Parry;
        Parry.kind = battle::CommandKind::ParryControlledUnit;
        Parry.unit = World.controlled;
        Parry.pose.yaw = AimYaw;
        Session->QueueCommand(Parry);
    }
    if (Actions.Attack != FRegimentOfficerInput::EAttack::None)
    {
        if (!OfficerHudState.FieldGunStatus.IsEmpty())
        {
            if (Actions.Attack != FRegimentOfficerInput::EAttack::Strike)
                return;
            const auto* Officer = battle::FindUnit(World, World.controlled);
            const auto* Camera = OfficerCamera.Get();
            if (!Officer || !Camera || !OfficerHudState.bAttackReady)
                return;
            const FVector Ray = Camera->GetActorForwardVector();
            const FVector Eye = Camera->GetActorLocation();
            const FVector Origin = GetBattleOrigin();
            const int32 Team = battle::UnitTeam(World, *Officer);
            battle::UnitId Target;
            double BestSlope = .028; // Aim assistance covers the width of a distant formation, not the whole screen.
            for (const auto& Unit : World.units)
            {
                if (!Unit.alive || battle::UnitTeam(World, Unit) == Team)
                    continue;
                const FVector Delta = Session->WorldLocation(Unit.pose.position, Origin) + FVector(0, 0, 120) - Eye;
                const double Along = FVector::DotProduct(Delta, Ray);
                if (Along <= 0.0)
                    continue;
                const double Slope = (Delta - Ray * Along).Size() / Along;
                if (Slope < BestSlope)
                {
                    BestSlope = Slope;
                    Target = Unit.id;
                }
            }
            if (!Target)
            {
                ShowOfficerNotice(LOCTEXT("FieldGunNeedsTarget", "Наведите прицел на вражеский строй"));
                return;
            }
            battle::Command Fire;
            Fire.kind = battle::CommandKind::FireFieldGun;
            Fire.unit = World.controlled;
            Fire.target = Target;
            Session->QueueCommand(Fire);
            return;
        }
        battle::Command Attack;
        Attack.kind = battle::CommandKind::AttackControlledUnit;
        Attack.unit = World.controlled;
        if (Actions.Attack == FRegimentOfficerInput::EAttack::Strike)
        {
            Attack.target = RegimentOfficerOrders::PersonalTarget(World, AimYaw);
            if (!Attack.target)
            {
                const auto* Officer = battle::FindUnit(World, World.controlled);
                if (Officer && (!Officer->archer || Officer->dagger_drawn) && Officer->parry_until <= World.tick)
                    OfficerInput.ShowMissedSwing();
            }
        }
        Session->QueueCommand(Attack);
    }
}

void ARegimentBattleField::ProcessOfficerInput()
{
    auto* Controller = GetWorld()->GetFirstPlayerController();
    if (!Controller || Session->IsOpening() || Session->Simulation.outcome != battle::BattleOutcome::Running)
        return;

    if (IsChoosingOfficer())
    {
        if (Controller->WasInputKeyJustPressed(EKeys::Escape) || Controller->WasInputKeyJustPressed(EKeys::G))
            CloseOfficerPicker();
        return;
    }
    if (!Session->Simulation.controlled)
    {
        if (Controller->WasInputKeyJustPressed(EKeys::G))
            OpenOfficerPicker();
        return;
    }

    if (Controller->WasInputKeyJustPressed(EKeys::G))
    {
        IssueOfficerOrder(ERegimentPlayerOrder::Release);
        return;
    }
    if (Controller->WasInputKeyJustPressed(EKeys::Tab) ||
        (OfficerCommandsOpen() && Controller->WasInputKeyJustPressed(EKeys::Escape)))
    {
        ToggleOfficerCommands();
        return;
    }
    using InputAction = ERegimentOfficerInputAction;
    const auto& Bindings = RegimentOfficerSettings();
    if (Controller->WasInputKeyJustPressed(Bindings.Key(InputAction::Camera)) && !Session->PlotOrder)
    {
        ToggleOfficerView();
        return;
    }
    const bool bDirectAtStart = CanDirectControlOfficer();
    if (!Session->PlotOrder && Controller->WasInputKeyJustPressed(EKeys::Escape))
        ToggleBattlePause();
    if (bDirectAtStart || Controller->IsInputKeyDown(EKeys::RightMouseButton))
    {
        float X = 0.f, Y = 0.f;
        Controller->GetInputMouseDelta(X, Y);
        const double Mouse = Bindings.Mouse();
        OfficerCameraYaw = FMath::UnwindDegrees(OfficerCameraYaw + X * .19 * Mouse);
        OfficerCameraPitch = RegimentOfficerView::Pitch(OfficerCameraPitch + Y * .16 * Mouse, Bindings.IsFirstPerson());
    }

    const TPair<FKey, ERegimentPlayerOrder> Keys[] = {
        {EKeys::One, ERegimentPlayerOrder::Line}, {EKeys::Two, ERegimentPlayerOrder::Column},
        {EKeys::Three, ERegimentPlayerOrder::Hold}, {EKeys::Four, ERegimentPlayerOrder::Attack},
        {EKeys::Five, ERegimentPlayerOrder::Withdraw}, {EKeys::Six, ERegimentPlayerOrder::Skirmishers},
        {EKeys::Seven, ERegimentPlayerOrder::HoldLine}, {EKeys::Eight, ERegimentPlayerOrder::Brace},
        {EKeys::Nine, ERegimentPlayerOrder::Pursue}, {EKeys::M, ERegimentPlayerOrder::Maneuver},
        {EKeys::H, ERegimentPlayerOrder::GroupManeuver}, {EKeys::T, ERegimentPlayerOrder::Pace},
        {EKeys::B, ERegimentPlayerOrder::Cover}, {EKeys::Y, ERegimentPlayerOrder::FireSector},
        {EKeys::F, ERegimentPlayerOrder::Follow}, {EKeys::R, ERegimentPlayerOrder::NextRegiment},
        {EKeys::J, ERegimentPlayerOrder::CommandHold}, {EKeys::K, ERegimentPlayerOrder::WingHold}
    };
    for (const auto& Key : Keys)
        if (Controller->WasInputKeyJustPressed(Key.Key))
            IssueOfficerOrder(Key.Value);
    if (!OfficerCommandsOpen())
        UpdateManeuverInput(*Controller);

    const bool bDirect = CanDirectControlOfficer();
    SetOfficerMouseCapture(bDirect);
    if (!bDirectAtStart || !bDirect)
    {
        ClearOfficerDirectInput();
        return;
    }

    OfficerInput.RememberActions(Controller->WasInputKeyJustPressed(Bindings.Key(InputAction::Attack)),
        Controller->WasInputKeyJustPressed(Bindings.Key(InputAction::Parry)),
        Controller->WasInputKeyJustPressed(Bindings.Key(InputAction::Jump)));
    const auto RememberTap = [Controller](int32& Axis, const FKey& Positive, const FKey& Negative)
    {
        const int32 Tap = int32(Controller->WasInputKeyJustPressed(Positive)) -
            int32(Controller->WasInputKeyJustPressed(Negative));
        if (Tap)
            Axis = Tap;
    };
    // Retain key taps that fall between two BattleCore ticks.
    RememberTap(OfficerMotionTap.X, Bindings.Key(InputAction::Forward), Bindings.Key(InputAction::Backward));
    RememberTap(OfficerMotionTap.Y, Bindings.Key(InputAction::Right), Bindings.Key(InputAction::Left));
    RememberTap(OfficerMotionTap.Z, Bindings.Key(InputAction::TurnRight), Bindings.Key(InputAction::TurnLeft));
    const FKey& Run = Bindings.Key(InputAction::Run);
    bOfficerRunTap |= Controller->WasInputKeyJustPressed(Run) ||
        (Run == EKeys::LeftShift && Controller->WasInputKeyJustPressed(EKeys::RightShift));

    float Wheel = Controller->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
    if (FMath::IsNearlyZero(Wheel))
        Wheel = float(Controller->WasInputKeyJustPressed(EKeys::MouseScrollUp)) -
            float(Controller->WasInputKeyJustPressed(EKeys::MouseScrollDown));
    if (!Bindings.IsFirstPerson())
        OfficerCameraDistance = FMath::Clamp(OfficerCameraDistance - Wheel * 55.0, 230.0, 620.0);
}

void ARegimentBattleField::QueueOfficerMotion()
{
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto& World = Session->Simulation;
    const auto* Unit = battle::FindUnit(World, World.controlled);
    if (!Controller || !Unit || !Unit->alive || std::ranges::any_of(Session->PendingCommands, ChangesControlledUnit))
        return;

    const bool bMovingAllowed = CanDirectControlOfficer();
    const auto Axis = [Controller, bMovingAllowed](const FKey& Positive, const FKey& Negative, int32 Tap)
    {
        if (!bMovingAllowed)
            return 0;
        const bool bPositive = Controller->IsInputKeyDown(Positive);
        const bool bNegative = Controller->IsInputKeyDown(Negative);
        return bPositive || bNegative ? int32(bPositive) - int32(bNegative) : Tap;
    };
    using InputAction = ERegimentOfficerInputAction;
    const auto& Bindings = RegimentOfficerSettings();
    const int32 Turn = Axis(Bindings.Key(InputAction::TurnRight), Bindings.Key(InputAction::TurnLeft), OfficerMotionTap.Z);
    const bool bFirstPerson = Bindings.IsFirstPerson();
    const double TurnDegrees = double(Turn) * 120.0 * FRegimentBattlePlayback::StepSeconds;
    if (bFirstPerson && Turn)
        OfficerCameraYaw = FMath::UnwindDegrees(OfficerCameraYaw + TurnDegrees);
    const float Yaw = OfficerCamera.IsValid() ? float(FMath::DegreesToRadians(OfficerCameraYaw)) : Unit->pose.yaw;
    QueueOfficerActions(Yaw, bMovingAllowed);
    const auto Direction = battle::Forward(Yaw) * float(Axis(Bindings.Key(InputAction::Forward),
        Bindings.Key(InputAction::Backward), OfficerMotionTap.X)) +
        battle::Right(Yaw) * float(Axis(Bindings.Key(InputAction::Right),
            Bindings.Key(InputAction::Left), OfficerMotionTap.Y));
    OfficerMotionTap = FIntVector(0, 0, 0);
    const FKey& Run = Bindings.Key(InputAction::Run);
    const bool bRunning = bOfficerRunTap || Controller->IsInputKeyDown(Run) ||
        (Run == EKeys::LeftShift && Controller->IsInputKeyDown(EKeys::RightShift));
    bOfficerRunTap = false;
    battle::Command Command;
    Command.unit = Unit->id;
    if (battle::Length(Direction) > .01f)
    {
        float Speed = FRegimentOfficerInput::Speed(Unit->speed, bRunning);
        const auto Follower = std::ranges::find(World.regiments, Unit->id, &battle::RegimentState::following_officer);
        if (!bRunning && Follower != World.regiments.end() && !Follower->panic && !Follower->melee_engaged &&
            !battle::IsDeploying(World, *Follower))
            Speed = FMath::Min(Speed, battle::FormationTravelSpeed(World, *Follower) * .85f);
        Command.kind = battle::CommandKind::MoveControlledUnit;
        Command.velocity = Direction * (Speed / battle::Length(Direction));
    }
    else if (Turn)
    {
        Command.kind = battle::CommandKind::FaceControlledUnit;
        Command.pose.yaw = bFirstPerson ? Yaw : std::remainder(Unit->pose.yaw +
            float(FMath::DegreesToRadians(TurnDegrees)), 2.f * battle::Pi);
    }
    else if (battle::Length(Unit->desired_velocity) > .01f)
    {
        Command.kind = battle::CommandKind::MoveControlledUnit;
    }
    else
        return;
    Session->QueueCommand(Command);
}

void ARegimentBattleField::RestoreOfficerCamera()
{
    if (auto* Camera = OfficerCamera.Get())
    {
        if (auto* Controller = GetWorld()->GetFirstPlayerController(); Controller && Controller->GetViewTarget() == Camera)
            if (auto* Previous = PreviousBattleView.Get())
                Controller->SetViewTarget(Previous);
        Camera->Destroy();
        bOfficerPresentationDirty = true;
    }
    OfficerCamera.Reset();
    OfficerViewBlade.Reset();
    PreviousBattleView.Reset();
    ClearOfficerDirectInput();
    OfficerInput = {};
    SetOfficerMouseCapture(false);
}

void ARegimentBattleField::UpdateOfficerViewBlade(ACameraActor& Camera, double Swing, bool bParrying)
{
    auto* Sword = CaptainWeapon ? CaptainWeapon.Get() : SwordsmanWeapon.Get();
    if (!Sword)
        return;
    if (!OfficerViewBlade.IsValid())
    {
        auto* Blade = NewFigureProp(Camera, *Camera.GetCameraComponent(), *Sword, WeaponMaterial.Get());
        Blade->SetCastShadow(false);
        OfficerViewBlade = Blade;
    }
    auto* Blade = OfficerViewBlade.Get();
    const auto Bounds = Sword->GetBounds();
    const int32 Longest = Bounds.BoxExtent.GetAbsMax() == Bounds.BoxExtent.Z ? 2
        : Bounds.BoxExtent.X >= Bounds.BoxExtent.Y ? 0 : 1;
    FVector MeshAxis = FVector::ZeroVector;
    MeshAxis[Longest] = 1.0;
    const FVector Rest(.12, -.24, .96);
    const FVector Guard(.11, -.96, .26);
    const FVector Attack(.45, -1.0, .15);
    const FVector Direction = (bParrying ? Guard : FMath::Lerp(Rest, Attack, Swing)).GetSafeNormal();
    const FQuat Rotation = FQuat::FindBetweenNormals(MeshAxis, Direction);
    const double Scale = 56.0 / FMath::Max(1.0, 2.0 * Bounds.BoxExtent[Longest]);
    const FVector Center = bParrying ? FVector(54, 8, -14) : FVector(56, 33 - 31 * Swing, -29 + 17 * Swing);
    Blade->SetRelativeTransform(FTransform(Rotation, Center - Rotation.RotateVector(Bounds.Origin * Scale), FVector(Scale)));
    Blade->SetVisibility(true);
}

void ARegimentBattleField::UpdateOfficerCamera(float DeltaSeconds)
{
    auto& World = Session->Simulation;
    OfficerHudState.bCanPossess = CanPossessOfficer(0);
    const auto* Unit = battle::FindUnit(World, World.controlled);
    if (!Unit || !Unit->alive || World.outcome != battle::BattleOutcome::Running)
    {
        if (PossessedOfficer)
        {
            const auto* Previous = battle::FindUnit(World, {PossessedOfficer});
            ShowOfficerNotice(!Previous || !Previous->alive ? LOCTEXT("PossessedOfficerFell", "Офицер выбыл.")
                : World.outcome == battle::BattleOutcome::Running ? LOCTEXT("OfficerReleased", "Свободная камера.")
                                                                : FText::GetEmpty());
            RestoreOfficerCamera();
        }
        PossessedOfficer = SelectedOfficerRegiment = 0;
        Session->FieldLines.Reset();
        Session->DragAnchor.Reset();
        Session->PlotOrder.Reset();
        OfficerHudState.bPlanning = false;
        OfficerHudState.bActive = OfficerHudState.bCanCommand = OfficerHudState.bAdditionalRegiments = false;
        OfficerHudState.bParryActive = false;
        OfficerHudState.HealthFraction.Reset();
        OfficerHudState.FieldGunStatus = FText::GetEmpty();
        OfficerHudState.RegimentCondition = FText::GetEmpty();
        OfficerHudState.Order = {};
        return;
    }

    auto* Controller = GetWorld()->GetFirstPlayerController();
    const bool bFirstPerson = RegimentOfficerSettings().IsFirstPerson();
    const bool bNewOfficer = PossessedOfficer != Unit->id.value;
    if (OfficerHudState.bActive && OfficerHudState.bFirstPerson != bFirstPerson)
    {
        OfficerCameraPitch = RegimentOfficerView::Pitch(OfficerCameraPitch, bFirstPerson);
        bOfficerPresentationDirty = true;
    }
    if (bNewOfficer)
    {
        PossessedOfficer = Unit->id.value;
        Session->PlotOrder.Reset();
        Session->DragAnchor.Reset();
        Session->FieldLines.Reset();
        OfficerHudState.bPlanning = false;
        ClearOfficerDirectInput();
        SelectedOfficerRegiment = Unit->regiment.value;
        OfficerCameraYaw = FMath::RadiansToDegrees(double(Unit->pose.yaw));
        OfficerCameraPitch = bFirstPerson ? 0.0 : OfficerDefaultCameraPitch;
        OfficerCameraDistance = OfficerCameraZoom = OfficerDefaultCameraDistance;
        OfficerInput = {};
        SetOfficerMouseCapture(CanDirectControlOfficer());
        ShowOfficerNotice(FText::GetEmpty());
        if (FSlateApplication::IsInitialized())
            FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
    if (!IsBattlePaused())
        OfficerInput.Advance(DeltaSeconds);
    OfficerHudState.bActive = true;
    OfficerHudState.bFirstPerson = bFirstPerson;
    // BattleCore stores the defining unit specs sorted by id and tracks damage on UnitState.
    const auto Initial = std::ranges::lower_bound(World.scenario.units, Unit->id, {}, &battle::UnitSpec::id);
    if (Initial != World.scenario.units.end() && Initial->id == Unit->id && Initial->health > 0.f)
        OfficerHudState.HealthFraction = FMath::Clamp(Unit->health / Initial->health, 0.f, 1.f);
    else
        OfficerHudState.HealthFraction.Reset();
    OfficerHudState.bParryReady = !Unit->jump_started_tick &&
        World.tick >= Unit->parry_ready_tick;
    OfficerHudState.bParryActive = World.tick < Unit->parry_until;
    OfficerHudState.bAttackReady = World.tick >= Unit->ready_tick && !OfficerHudState.bParryActive;
    OfficerHudState.FieldGunStatus = FText::GetEmpty();
    if (const auto* Battery = battle::FindRegiment(World, Unit->regiment))
    {
        bool bHasGun = false;
        bool bNearbyGun = false;
        bool bLoadedGun = false;
        TOptional<battle::Tick> ReadyTick;
        for (const auto Id : Battery->units)
        {
            const auto* Gun = battle::FindUnit(World, Id);
            if (!Gun || Gun->platform != battle::BattlePlatform::FieldGun)
                continue;
            bHasGun = true;
            if (!Gun->alive || battle::Distance(Unit->pose.position, Gun->pose.position) > battle::FieldGunOfficerReachM)
                continue;
            bNearbyGun = true;
            if (Gun->ammunition && !Gun->ammunition_remaining)
                continue;
            bLoadedGun = true;
            ReadyTick = ReadyTick ? FMath::Min(ReadyTick.GetValue(), Gun->ready_tick) : Gun->ready_tick;
        }
        if (bHasGun)
        {
            OfficerHudState.bAttackReady = bLoadedGun && ReadyTick.GetValue() <= World.tick;
            if (OfficerHudState.bAttackReady)
                OfficerHudState.FieldGunStatus = LOCTEXT("FieldGunReady", "Орудие готово");
            else if (!bNearbyGun)
                OfficerHudState.FieldGunStatus = LOCTEXT("FieldGunDistant", "Подойдите к орудию");
            else if (!bLoadedGun)
                OfficerHudState.FieldGunStatus = LOCTEXT("FieldGunEmpty", "Боезапас исчерпан");
            else
                OfficerHudState.FieldGunStatus = FText::Format(LOCTEXT("FieldGunReload", "Перезарядка · {0} с"),
                    FText::AsNumber(FMath::CeilToInt(float(ReadyTick.GetValue() - World.tick) *
                        FRegimentBattlePlayback::StepSeconds)));
        }
    }
    OfficerHudState.Pace = Session->Pace == battle::MovementPace::Walk ? LOCTEXT("PaceWalk", "Шаг")
        : Session->Pace == battle::MovementPace::March ? LOCTEXT("PaceMarch", "Марш") : LOCTEXT("PaceRun", "Бег");
    const auto* Entry = Session->DecisionEntries.FindByPredicate(
        [Unit](const auto& Decision) { return Decision.Commander == Unit->id.value; });
    OfficerHudState.Officer = Entry ? FText::Format(LOCTEXT("OfficerIdentity", "{0} · {1}"), Entry->Duty, Entry->CommanderName)
        : LOCTEXT("RegimentOfficerCaption", "Командир полка");
    OfficerHudState.ProfileEffects = Entry ? Entry->Effects : FText::GetEmpty();
    OfficerHudState.TacticalSkill = Entry ? Entry->TacticalSkill : FText::GetEmpty();
    OfficerHudState.RankIcon = FText::FromString(Entry ? Entry->RankIcon : TEXT("◆"));
    OfficerHudState.Portrait = Entry ? Entry->Portrait : TWeakObjectPtr<UTexture2D>{};
    OfficerHudState.CommandTier = Entry ? Entry->CommandTier : 1;
    OfficerHudState.CommandTitle = OfficerHudState.CommandTier == 3 ? LOCTEXT("ArmyOrders", "Приказы армии")
        : OfficerHudState.CommandTier == 2 ? LOCTEXT("GroupOrders", "Приказы группе") : LOCTEXT("RegimentOrders", "Приказы полку");
    const auto Regiments = RegimentOfficerOrders::Regiments(World, Unit->id);
    if (std::ranges::find(Regiments, battle::RegimentId{SelectedOfficerRegiment}) == Regiments.end())
    {
        if (SelectedOfficerRegiment)
            ShowOfficerNotice(FText::GetEmpty());
        Session->DragAnchor.Reset();
        SelectedOfficerRegiment = Regiments.empty() ? 0 : Regiments.front().value;
    }
    const auto* Regiment = battle::FindRegiment(World, {SelectedOfficerRegiment});
    OfficerHudState.bCanCommand = Regiment != nullptr;
    OfficerHudState.bAdditionalRegiments = Regiments.size() > 1;
    OfficerHudState.CommandScope = FText::Format(LOCTEXT("OfficerScope", "{0} · полков в подчинении: {1}"),
        Entry ? Entry->Name : OfficerHudState.Officer, int32(Regiments.size()));
    const auto Courier = RegimentOfficerOrders::DescribeCourier(World, {SelectedOfficerRegiment},
        Session->PendingCommands, IsBattlePaused());
    if (!Courier.IsEmpty())
        OfficerHudState.CommandScope = FText::Format(LOCTEXT("OfficerCourierScope", "{0}\n{1}"),
            OfficerHudState.CommandScope, Courier);
    OfficerHudState.Order = Regiment
        ? RegimentOfficerOrders::DescribeOrder(World, *Regiment, Session->PendingCommands, IsBattlePaused(),
            &Session->PlayerOrderJournal)
        : FRegimentOrderPresentation{};
    OfficerHudState.Regiment = Regiment ? RegimentName(*Regiment) : LOCTEXT("NoSubordinateRegiment", "Нет боеспособных полков");
    OfficerHudState.RegimentCondition = Regiment ? FText::Format(LOCTEXT("OfficerRegimentCondition",
        "{0}/{1} в строю  ·  Дух {2}  ·  Усталость {3}"), int32(Regiment->units.size()), int32(Regiment->initial_count),
        FText::AsPercent(Regiment->morale), FText::AsPercent(Regiment->fatigue)) : FText::GetEmpty();

    if (!Controller)
        return;
    const bool bSnapCamera = bNewOfficer || bOfficerPresentationDirty || !OfficerCamera.IsValid();
    if (!OfficerCamera.IsValid())
    {
        PreviousBattleView = Controller->GetViewTarget();
        OfficerCamera = GetWorld()->SpawnActor<ACameraActor>();
        if (auto* Camera = OfficerCamera.Get())
        {
            auto* View = Camera->GetCameraComponent();
            View->bConstrainAspectRatio = false;
            if (Mission)
                ApplyRegimentBattleCameraExposure(*View);
            View->PostProcessSettings.bOverride_MotionBlurAmount = true;
            View->PostProcessSettings.MotionBlurAmount = 0.f;
            Controller->SetViewTarget(Camera);
            bOfficerPresentationDirty = true;
        }
    }
    if (auto* Camera = OfficerCamera.Get())
    {
        const FRotator Rotation(OfficerCameraPitch, OfficerCameraYaw, 0.0);
        const bool bMounted = Unit->troop == battle::UnitTroop::Mounted;
        const FVector Origin = GetBattleOrigin();
        Camera->GetCameraComponent()->SetFieldOfView(RegimentOfficerView::FieldOfView(bFirstPerson));
        const FVector Footing = Session->WorldLocation(Unit->pose.position, Origin);
        if (bFirstPerson)
        {
            Camera->SetActorLocationAndRotation(RegimentOfficerView::Eyes(Footing, bMounted), Rotation);
            double Swing = OfficerInput.SwingPhase();
            if (const auto* Attack = Session->Attacks.Find(Unit->id.value))
            {
                const double Duration = FMath::Max(AttackPoseTicks,
                    double(Attack->TelegraphImpactTick ? Attack->TelegraphImpactTick - Attack->StartTick + 5 : 0));
                const double Progress = double(World.tick - Attack->StartTick) / Duration;
                if (Progress < 1.0)
                    Swing = FMath::Max(Swing, Progress);
            }
            if (Unit->platform == battle::BattlePlatform::None && OfficerHudState.FieldGunStatus.IsEmpty())
                UpdateOfficerViewBlade(*Camera, FMath::Sin(PI * Swing), OfficerHudState.bParryActive);
            else if (auto* Blade = OfficerViewBlade.Get())
                Blade->SetVisibility(false);
            return;
        }
        if (auto* Blade = OfficerViewBlade.Get())
            Blade->SetVisibility(false);
        const FVector Shoulder = FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y) * 62.0;
        const FVector Pivot = Footing + FVector(0, 0, bMounted ? 207.0 : 143.0);
        const double Follow = bSnapCamera ? 1.0 : 1.0 - FMath::Exp(-18.0 * DeltaSeconds);
        OfficerCameraPivot = FMath::Lerp(OfficerCameraPivot, Pivot, Follow);
        OfficerCameraZoom = FMath::Lerp(OfficerCameraZoom, OfficerCameraDistance, Follow);
        const FVector Focus = OfficerCameraPivot + Shoulder;
        FVector Wanted = Focus - Rotation.Vector() * (OfficerCameraZoom + (bMounted ? 80.0 : 0.0));
        FRotator CameraRotation = Rotation;
        const double MinimumZ = Session->TerrainZAt(Wanted, Origin) + 85.0;
        if (Wanted.Z < MinimumZ)
        {
            Wanted.Z = MinimumZ;
            CameraRotation = (Focus - Wanted).Rotation();
        }
        const auto Terrain = Mission ? Mission->GetPreset().Terrain : battle::MarchTerrain::StoneWalls;
        const FVector Position = Session->LimitOfficerCamera(OfficerCameraPivot, Wanted, Origin, Terrain);
        Camera->SetActorLocationAndRotation(Position, CameraRotation);
    }
}

void ARegimentBattleField::AttachHud()
{
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    if (!Player || !Player->ViewportClient || !Player->ViewportClient->GetGameLayerManager())
        return;

    auto Widget = SNew(SRegimentBattleHud).Field(this);
    Player->ViewportClient->AddViewportWidgetForPlayer(Player, Widget, 10);
    BattleHud = Widget;
    HudPlayer = Player;
    FInputModeGameAndUI InputMode;
    InputMode.SetWidgetToFocus(Player->ViewportClient->GetGameViewportWidget());
    InputMode.SetHideCursorDuringCapture(false);
    Controller->SetInputMode(InputMode);
    Controller->bShowMouseCursor = true;
}

void ARegimentBattleField::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (OfficerNoticeSeconds > 0.f)
    {
        OfficerNoticeSeconds -= DeltaSeconds;
        if (OfficerNoticeSeconds <= 0.f)
            ShowOfficerNotice(FText::GetEmpty());
    }
    if (Mission && !Mission->TacticalScenario)
    {
        Mission->TickMarch(DeltaSeconds);
        if (Mission->TacticalScenario)
        {
            if (StartBattle())
            {
                Mission->BeginTactical();
                bCameraApplied = true;
            }
            else
                Mission->ReturnToSetup();
        }
        return;
    }
    if (Mission && (bSetupRequested || (!Mission->IsBattleReportOpen() && !IsChoosingOfficer() && !OfficerHudState.bActive &&
        (!Session || !Session->IsScriptedPerf()) && Mission->TickCamera(DeltaSeconds))))
    {
        if (auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
            Mass && Mass->GetEntityManager().IsProcessing())
            return;
        ClearBattle();
        RemoveHud();
        Mission->ReturnToSetup();
        bRestartRequested = false;
        bSetupRequested = false;
        bCameraApplied = false;
        return;
    }
    if (!bSmokeRun && !(Mission && Mission->bSmoke) && !BattleHud.IsValid())
        AttachHud();

    if (bRestartRequested)
    {
        auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
        if (Mass && Mass->GetEntityManager().IsProcessing())
            return;
        bRestartRequested = false;
        if (!StartBattle())
            return;
        DeltaSeconds = 0.f;
    }
    if (!Session)
        return;
    if (PresentedTick.IsSet() && Session->Perf && Session->Perf->IsMeasuring())
        Session->Perf->RecordGameFrame(Session->IsPerfOfficerView(), FPlatformTime::Seconds(), Session->Simulation.tick);
    if (!Session->IsScriptedPerf() && !bSmokeRun && !(Mission && (Mission->bSmoke || Mission->IsBattleReportOpen())))
        ProcessOfficerInput();

    if (!bCameraApplied && OverviewCamera)
    {
        if (auto* Controller = GetWorld()->GetFirstPlayerController())
        {
            Controller->SetViewTarget(OverviewCamera);
            bCameraApplied = true;
        }
    }

    if (Session->bQueueInitialPossession && GetWorld()->GetFirstPlayerController() && CanPossessOfficer(0))
    {
        PossessOfficer();
        Session->bQueueInitialPossession = false;
    }

    if (!PresentedTick.IsSet())
    {
        if (!PresentFrame())
            return;
        // These records are used only by the headless scene check.
        if (bSmokeRun)
        {
            for (const auto& Entry : Session->Units)
                Session->InitialHandles.Add(Entry.Key, Entry.Value.Entity);
            const auto& Simulation = Session->Simulation;
            const auto Blue = ReadUnitTransform(1);
            const auto Orange = ReadUnitTransform(51);
            UE_LOG(LogRegimentBattle, Display,
                   TEXT("RegimentBattle ready: units=%d entities=%d meshes=%d commands=%d poses=%d x1=%.2f x51=%.2f y1=%.2f y51=%.2f yaw1=%.2f yaw51=%.2f"),
                   int32(Simulation.units.size()), CountValidEntities(), CountFigures(),
                   int32(Simulation.command_state->nodes.size()), CountMatchingPoses(),
                   Blue.GetLocation().X, Orange.GetLocation().X, Blue.GetLocation().Y,
                   Orange.GetLocation().Y, Blue.Rotator().Yaw, Orange.Rotator().Yaw);
        }
        if (Mission && Mission->bSmoke)
        {
            int32 Staff[int32(ECommandFigure::Count)] = {};
            int32 MountedStaff[int32(ECommandFigure::Count)] = {};
            for (const auto& Entry : Session->Units)
            {
                const auto Figure = Session->CommandFigure({Entry.Key});
                const bool bNeedsInsignia = Figure == ECommandFigure::Standard ||
                    Figure == ECommandFigure::Musician || Figure == ECommandFigure::General;
                if (Entry.Value.Figure.IsValid() && Entry.Value.Weapon.IsValid() &&
                    (!bNeedsInsignia || Entry.Value.Insignia.IsValid()))
                {
                    ++Staff[int32(Figure)];
                    if (Entry.Value.Mount.IsValid())
                        ++MountedStaff[int32(Figure)];
                }
            }
            UE_LOG(LogRegimentBattle, Display,
                TEXT("RegimentMission tactical: regiments=%d figures=%d mass=%d blue=%d orange=%d commands=%d"),
                int32(Session->Simulation.regiments.size()), CountFigures(), CountValidEntities(),
                HudState.BlueAlive, HudState.OrangeAlive, int32(Session->Simulation.command_state->nodes.size()));
            UE_LOG(LogRegimentBattle, Display, TEXT("RegimentMission staff: officers=%d banners=%d musicians=%d leaders=%d generals=%d"),
                Staff[int32(ECommandFigure::Officer)], Staff[int32(ECommandFigure::Standard)],
                Staff[int32(ECommandFigure::Musician)], Staff[int32(ECommandFigure::Commander)],
                Staff[int32(ECommandFigure::General)]);
            UE_LOG(LogRegimentBattle, Display, TEXT("RegimentMission mounted staff: officers=%d banners=%d musicians=%d leaders=%d generals=%d"),
                MountedStaff[int32(ECommandFigure::Officer)], MountedStaff[int32(ECommandFigure::Standard)],
                MountedStaff[int32(ECommandFigure::Musician)], MountedStaff[int32(ECommandFigure::Commander)],
                MountedStaff[int32(ECommandFigure::General)]);
            int32 Spears = 0, Mounted = 0;
            for (const auto& Unit : Session->Simulation.units)
                if (const auto* View = Session->Units.Find(Unit.id.value))
                {
                    Spears += Unit.troop == battle::UnitTroop::Spearman &&
                        View->Weapon.IsValid() && View->Insignia.IsValid();
                    Mounted += Unit.troop == battle::UnitTroop::Mounted &&
                        View->Figure.IsValid() && View->Mount.IsValid();
                }
            UE_LOG(LogRegimentBattle, Display, TEXT("RegimentMission troops: spears=%d mounted=%d"), Spears, Mounted);
        }
    }

    auto& Simulation = Session->Simulation;
    const bool bLargeBattle = Simulation.units.size() > 1000;
    const int32 StepsPerFrame = bLargeBattle ? 1 : 8;
    if (Simulation.outcome == battle::BattleOutcome::Running && HudState.Status != ERegimentBattleStatus::Failed)
        Session->Playback.AddTime(DeltaSeconds, bLargeBattle ? 2 : 0);
    for (int32 Steps = 0; Steps < StepsPerFrame && Simulation.outcome == battle::BattleOutcome::Running &&
         HudState.Status != ERegimentBattleStatus::Failed &&
         (!bSmokeRun || Simulation.tick < SmokeEndTick) && Session->Playback.TakeStep(); ++Steps)
    {
        if (Session->IsOpening())
        {
            Session->Opening->Advance(Simulation);
            if (bSmokeRun)
                Session->LogOpeningCheckpoint();
            continue;
        }
        battle::SimulationOptions Options;
        if (Session->GroupPolicy)
            Options.group_policy = &*Session->GroupPolicy;
        if (Simulation.controlled)
            QueueOfficerMotion();
        const bool bMeasureStep = Session->Perf && Session->Perf->IsMeasuring() && !Session->Perf->IsReady();
        Session->Trace.BeforeStep(Simulation);
        const double StepStart = bMeasureStep ? FPlatformTime::Seconds() : 0.0;
        battle::StepProfile StepProfile{};
        const bool bStepped = bool(battle::Step(Simulation, Session->PendingCommands, Options, *Session->Navigation,
            bMeasureStep ? &StepProfile : nullptr));
        if (bMeasureStep && bStepped)
        {
            Session->Perf->RecordStep(Session->IsPerfOfficerView(), (FPlatformTime::Seconds() - StepStart) * 1000.0,
                StepProfile, Simulation.tick);
            Session->Perf->ObserveCombat(Simulation.tick, Simulation.events);
        }
        if (!bStepped)
        {
            UE_LOG(LogRegimentBattle, Error, TEXT("BattleCore rejected a simulation tick."));
            Session->ReportPerf();
            if (bSmokeRun || (Mission && Mission->bSmoke))
                FPlatformMisc::RequestExitWithStatus(false, 1);
            HudState.Status = ERegimentBattleStatus::Failed;
            return;
        }
        for (const auto& Event : Simulation.events)
            if (Event.kind == battle::EventKind::CommandRejected && Event.source == Simulation.controlled &&
                Event.reason == battle::FieldGunRejectedReason)
                ShowOfficerNotice(LOCTEXT("FieldGunBlocked", "Огонь невозможен · проверьте сектор, препятствия и положение орудия"));
        const battle::io::BattleTraceOutcome StepOutcome{Simulation.outcome, Simulation.winner_team};
        if (Session->MissionProgress)
            Session->MissionProgress->Update(Simulation, OfficerCommandsOpen());
        Session->Trace.AfterStep(Simulation, Session->PendingCommands, StepOutcome);
        if (Mission)
        {
            Mission->RecordCampaignOutcome(Simulation.outcome, Simulation.winner_team);
            Mission->ObserveBattleReport(Simulation);
        }
        if (!Session->PendingCommands.empty())
        {
            Session->PendingCommands.clear();
            Session->LastDecisionPresentation.Reset();
        }
        if (Mission && Mission->bSmoke)
        {
            const auto& Command = *Simulation.command_state;
            for (const auto& Node : Command.hierarchy.nodes)
                if (Node.level == battle::command::Level::Army)
                {
                    const auto Runtime = std::find_if(Command.nodes.begin(), Command.nodes.end(),
                        [&](const auto& Item) { return Item.node == Node.id; });
                    if (Runtime == Command.nodes.end())
                        continue;
                    for (const auto& Task : Runtime->sent_tasks)
                        if (Task.action == battle::command::TaskKind::Flank && Task.intent.flank_waypoint)
                            Session->SmokeFlankGroups.Add(Task.recipient.value);
                }
        }
        if (Mission && Mission->bSmoke && Simulation.tick <= 3)
        {
            Session->LastDecisionPresentation.Reset();
            RefreshDecisionEntries();
            if (Simulation.tick == 1)
                if (const auto* Initial = Session->DecisionEntries.FindByPredicate([](const auto& Entry)
                    {
                        // The second regiment starts on the bank, with room to leave its ranks.
                        return Entry.Regiment == 2 && Entry.bCanDetach &&
                            Entry.bSkirmishersEnabled && Entry.Skirmishers > 0;
                    }))
                {
                    Session->SmokeSkirmisherNode = Initial->Node;
                    ToggleSkirmishers(Initial->Node);
                }
            const auto* Screen = Session->DecisionEntries.FindByPredicate([this](const auto& Entry)
            {
                return Entry.Node == Session->SmokeSkirmisherNode;
            });
            if (Screen && Simulation.tick == 2)
            {
                Session->bSmokeRecalled = !Screen->bSkirmishersEnabled && Screen->Skirmishers == 0;
                if (Session->bSmokeRecalled)
                    ToggleSkirmishers(Screen->Node);
            }
            else if (Screen && Simulation.tick == 3)
                Session->bSmokeDeployed = Screen->bSkirmishersEnabled && Screen->Skirmishers > 0;
        }
        const FVector Origin = GetBattleOrigin();
        if (const auto* Controller = GetWorld()->GetFirstPlayerController())
        {
            const FVector CameraLocation = PlayerPresentationCamera(Controller, false).Location;
            Session->Smoke.ObserveStep(Simulation, Origin,
                Mission ? Mission->GetPreset().Terrain : battle::MarchTerrain::StoneWalls, CameraLocation);
        }
        Session->RecordCombatEvents(*GetWorld(), Origin);
        if (Simulation.controlled && std::ranges::any_of(Simulation.events, [&](const auto& Event)
        {
            return Event.kind == battle::EventKind::Parried && Event.source == Simulation.controlled;
        }))
            ShowOfficerNotice(LOCTEXT("OfficerParried", "Удар отражён. Можно контратаковать."));
        if (!bSmokeRun && !(Mission && Mission->bSmoke))
            Session->PlayBattleSounds(*GetWorld(), Origin);
        for (const auto& Regiment : Simulation.regiments)
            Session->Notices.ObserveRegiment(Regiment, Simulation.tick,
                Session->WorldLocation(Regiment.pose.position, Origin));
        if (bSmokeRun && Simulation.tick == SmokeAdvanceTick)
            break;
    }

    if (Session->IsScriptedPerf())
    {
        if (Session->Perf->IsMeasuring())
            Session->UpdatePerfCamera(*GetWorld(), GetBattleOrigin(),
                Mission ? Mission->GetPreset().Terrain : battle::MarchTerrain::StoneWalls);
    }
    else if (!bSmokeRun && !(Mission && (Mission->bSmoke || Mission->IsBattleReportOpen())))
        UpdateOfficerCamera(DeltaSeconds);
    if (!bSmokeRun && !(Mission && Mission->bSmoke))
        Session->UpdateAcoustics(*GetWorld(), GetBattleOrigin(),
            Mission ? Mission->GetPreset().Terrain : battle::MarchTerrain::OpenField,
            Mission ? Mission->GetPreset().Field : battle::MarchField{});
    const bool bSimulationFrame = PresentedTick.GetValue() != Session->GetDisplayTick();
    bool bCameraFrame = false;
    if (!bSimulationFrame && !bSmokeRun && !(Mission && Mission->bSmoke) && !Session->bPerfFullDetail &&
        FPlatformTime::Seconds() - Session->LastPresentationTime >= .08)
    {
        const auto Camera = PlayerPresentationCamera(GetWorld()->GetFirstPlayerController(), false);
        const auto& Previous = Session->LastPresentationCamera;
        bCameraFrame = Session->bRichPromotionPending || !Camera.Location.Equals(Previous.Location, 35.0) ||
            !Camera.Rotation.Equals(Previous.Rotation, .65) || !FMath::IsNearlyEqual(Camera.FieldOfView, Previous.FieldOfView, .2f);
    }
    if ((bSimulationFrame || bCameraFrame || bOfficerPresentationDirty) && !PresentFrame())
        return;
    bOfficerPresentationDirty = false;
    if (Session->Perf && (Session->Perf->IsReady() || Simulation.outcome != battle::BattleOutcome::Running))
        Session->ReportPerf();
    const bool bCheckMission = Mission && Mission->bSmoke && Simulation.tick >= 20;
    const bool bDaggersDrawn = bCheckMission && std::any_of(Simulation.units.begin(), Simulation.units.end(),
        [](const auto& Unit) { return Unit.dagger_drawn; });
    int32 DeployedScreens = 0;
    if (bCheckMission && Session->SmokeSkirmisherNode)
    {
        const auto* Screen = Session->DecisionEntries.FindByPredicate([this](const auto& Entry)
        {
            return Entry.Node == Session->SmokeSkirmisherNode;
        });
        if (const auto* Regiment = Screen ? battle::FindRegiment(Simulation, {Screen->Regiment}) : nullptr)
            for (const auto Id : Regiment->units)
                if (const auto* Unit = battle::FindUnit(Simulation, Id);
                    Unit && Unit->alive && Unit->skirmisher && IsOutsideFormation(*Unit, *Regiment))
                    ++DeployedScreens;
    }
    const bool bMeleeShown = bCheckMission && Session->DecisionEntries.ContainsByPredicate(
        [](const auto& Entry) { return Entry.MeleeRegiments > 0 && !Entry.Contact.IsEmpty(); });
    if (bCheckMission && ((bDaggersDrawn && bMeleeShown && DeployedScreens > 0) || Simulation.tick >= 260 ||
        Simulation.outcome != battle::BattleOutcome::Running))
    {
        int32 Generals = 0, Groups = 0, Leaves = 0, Active = 0, Moved = 0, Screens = 0, Daggers = 0;
        int32 GeneralPlans = 0, GroupOrders = 0, ContactCards = 0, OwnCards = 0, OpposingCards = 0;
        const auto& Command = *Simulation.command_state;
        // Validate the two-army simulation independently of the friendly-only player presentation.
        for (const auto& Node : Command.hierarchy.nodes)
        {
            Groups += Node.level == battle::command::Level::Group;
            Leaves += Node.level == battle::command::Level::Regiment;
            if (Node.level != battle::command::Level::Army)
                continue;
            const auto Officer = std::ranges::find(Simulation.officers, Node.commander, &battle::OfficerSpec::id);
            const auto* Unit = Officer != Simulation.officers.end() ? battle::FindUnit(Simulation, Officer->unit) : nullptr;
            Generals += Unit && Unit->alive;
            const auto Runtime = std::find_if(Command.nodes.begin(), Command.nodes.end(),
                [&](const auto& Item) { return Item.node == Node.id; });
            if (Runtime == Command.nodes.end() || Runtime->sent_tasks.empty())
                continue;
            ++GeneralPlans;
            for (const auto& Task : Runtime->sent_tasks)
            {
                const auto* Recipient = battle::command::FindNode(Command.hierarchy, Task.recipient);
                if (Recipient && Recipient->parent == Node.id)
                    ++GroupOrders;
            }
        }
        for (const auto& Entry : Session->DecisionEntries)
        {
            OwnCards += Entry.Team == 0;
            OpposingCards += Entry.Team != 0;
            Active += !Entry.Action.IsEmpty() && !Entry.Detail.IsEmpty() &&
                (!Entry.Contact.IsEmpty() || !Entry.Intel.IsEmpty());
            ContactCards += Entry.MeleeRegiments > 0 && !Entry.Contact.IsEmpty();
        }
        for (size_t Index = 0; Index < Simulation.units.size(); ++Index)
        {
            const auto& Unit = Simulation.units[Index];
            Moved += !Unit.regiment && Unit.pose.position != Simulation.scenario.units[Index].pose.position;
            Screens += Unit.skirmisher;
            Daggers += Unit.dagger_drawn;
        }
        UE_LOG(LogRegimentBattle, Display,
            TEXT("RegimentMission decisions: generals=%d groups=%d leaves=%d active=%d moved=%d screens=%d daggers=%d contact=%d"),
            Generals, Groups, Leaves, Active, Moved, Screens, Daggers, ContactCards);
        UE_LOG(LogRegimentBattle, Display,
            TEXT("RegimentMission army orders: plans=%d groups=%d flankRoutes=%d"),
            GeneralPlans, GroupOrders, Session->SmokeFlankGroups.Num());
        UE_LOG(LogRegimentBattle, Display, TEXT("RegimentMission decision display: own=%d opposing=%d"), OwnCards, OpposingCards);
        UE_LOG(LogRegimentBattle, Display, TEXT("RegimentMission skirmisher orders: recalled=%d deployed=%d outside=%d"),
            int32(Session->bSmokeRecalled), int32(Session->bSmokeDeployed), DeployedScreens);
        const bool bAction = Simulation.combat_events > 0 ||
            !std::equal(Simulation.regiments.begin(), Simulation.regiments.end(),
                Simulation.scenario.regiments.begin(), [](const auto& Active, const auto& Initial)
                {
                    return Active.pose == Initial.pose;
                });
        const bool bValid = CountMatchingPoses() == CountValidEntities() && OwnCards == 10 && OpposingCards == 0 &&
            Generals == 2 && Groups == 6 && Leaves == 12 && GeneralPlans == 2 && GroupOrders == Groups &&
            Active > Generals && ContactCards > 0 && Moved > 0 && Daggers > 0 && bAction &&
            Session->bSmokeRecalled && Session->bSmokeDeployed && DeployedScreens > 0;
        UE_LOG(LogRegimentBattle, Display, TEXT("RegimentMission advanced: tick=%llu aligned=%d action=%d"),
            static_cast<unsigned long long>(Simulation.tick), int32(bValid), int32(bAction));
        FPlatformMisc::RequestExitWithStatus(false, bValid ? 0 : 1);
        return;
    }
    if (Simulation.outcome != battle::BattleOutcome::Running &&
        (Session->HasFallen() || !GetBattleNotices().IsEmpty()) && !IsBattlePaused())
    {
        const auto Speed = GetBattleSpeed();
        const double Rate = Speed == ERegimentBattleSpeed::Half ? 0.5 : Speed == ERegimentBattleSpeed::Double ? 2.0 : 1.0;
        Session->PostBattleTicks += Rate * DeltaSeconds / FRegimentBattlePlayback::StepSeconds;
        Session->Notices.Expire(GetBattleNoticeTick());
        if (Session->Smoke.HasVisiblePuffs())
            if (const auto* Controller = GetWorld()->GetFirstPlayerController())
                Session->PresentEffects(*this, GetBattleOrigin(), PlayerPresentationCamera(Controller, true), OfficerCamera.IsValid());
        if (!bSimulationFrame && !bCameraFrame && Session->HasFallen())
            Session->PresentFallen(nullptr);
    }

    if (bSmokeRun && Simulation.tick == SmokeAdvanceTick && bSimulationFrame)
    {
        bool bStable = Session->Units.Num() == Session->InitialHandles.Num();
        int32 Equipped = 0;
        int32 Painted = 0;
        int32 Rigs = 0;
        for (const auto& Rig : Session->Rigs)
            Rigs += Rig && Rig->IsValid() ? 1 : 0;
        for (const auto& Entry : Session->Units)
        {
            const auto* First = Session->InitialHandles.Find(Entry.Key);
            bStable &= First && *First == Entry.Value.Entity;
            Equipped += Entry.Value.Weapon.IsValid() ? 1 : 0;
            if (const auto* Figure = Entry.Value.Figure.Get())
            {
                const auto* Team = Entry.Key <= UnitsPerTeam ? BlueFigureMaterial.Get() : OrangeFigureMaterial.Get();
                Painted += Team && Team == Figure->GetMaterial(0) ? 1 : 0;
            }
        }
        UE_LOG(LogRegimentBattle, Display,
               TEXT("RegimentBattle advanced: tick=100 units=%d entities=%d meshes=%d poses=%d stable=%d x1=%.2f x51=%.2f"),
               int32(Session->Simulation.units.size()), CountValidEntities(), CountFigures(),
               CountMatchingPoses(), bStable ? 1 : 0,
               ReadUnitTransform(1).GetLocation().X, ReadUnitTransform(51).GetLocation().X);
        UE_LOG(LogRegimentBattle, Display, TEXT("RegimentBattle figures: rigs=%d equipped=%d painted=%d arrowSlots=%d"),
            Rigs, Equipped, Painted, ArrowVisuals->GetInstanceCount());
    }
    if (bSmokeRun)
    {
        const auto& Check = Session->MeleeSmoke.GetValue();
        const bool bSuccess = Check.Hits > 0 && Check.Reactions > 0 && Check.Deaths > 0 &&
            Check.bCollapsed && Check.bExpired && Simulation.tick >= SmokeAdvanceTick;
        if (!bSuccess && Simulation.tick < SmokeEndTick &&
            (Simulation.outcome == battle::BattleOutcome::Running || !Session->Fallen.IsEmpty()))
            return;
        UE_LOG(LogRegimentBattle, Display,
            TEXT("RegimentBattle combat: meleeHits=%d reactions=%d meleeDeaths=%d collapsed=%d expired=%d aligned=%d firstMeleeOnFlank=%d"),
            Check.Hits, Check.Reactions, Check.Deaths, int32(Check.bCollapsed), int32(Check.bExpired), int32(bSuccess),
            int32(Check.FirstMeleeTick.IsSet() && Check.bFirstMeleeOnFlank));
        SetActorTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
    }
}

bool ARegimentBattleField::PresentFrame()
{
    auto* Subsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (!Subsystem)
        return false;
    auto& Mass = Subsystem->GetMutableEntityManager();
    if (Mass.IsProcessing())
        return false;

    const bool bMeasureFrame = Session->Perf && Session->Perf->IsMeasuring();
    const double StartTime = bMeasureFrame ? FPlatformTime::Seconds() : 0.0;
    double PhaseStart = StartTime;
    FRegimentPresentationProfile Profile;
    const auto MeasurePhase = [&](double& Milliseconds)
    {
        if (bMeasureFrame)
        {
            const double Now = FPlatformTime::Seconds();
            Milliseconds = (Now - PhaseStart) * 1000.0;
            PhaseStart = Now;
        }
    };
    bool bHasLifecycleCommands = false;
    const auto& Simulation = Session->Simulation;
    const FVector Origin = GetBattleOrigin();
    const uint64 DisplayTick = Session->GetDisplayTick();
    const bool bSimulationFrame = !PresentedTick.IsSet() || PresentedTick.GetValue() != DisplayTick;
    const bool bMassOriginChanged = Origin != Session->LastMassOrigin;
    const double FrameSeconds = (DisplayTick - PresentedTick.Get(DisplayTick)) * FRegimentBattlePlayback::StepSeconds;
    const bool bAllRich = Simulation.units.size() <= 500 &&
        (bSmokeRun || (Mission && Mission->bSmoke) || Session->bPerfFullDetail);
    const bool bCacheLocations = !bAllRich && !Session->bPerfBaselinePresentation;
    const bool bTrainingInfantry = Mission && Mission->GetPreset().Tutorial == ERegimentTutorial::Infantry;
    constexpr battle::RegimentId PracticeTargets{MissionRegimentsPerTeam + 1};
    const auto HiddenOfficer = OfficerCamera.IsValid() && OfficerHudState.bFirstPerson
        ? Simulation.controlled : battle::UnitId{};
    if (bCacheLocations)
        Session->PresentationLocations.SetNumUninitialized(int32(Simulation.units.size()));
    for (auto& Entry : Session->Platforms)
        Entry.Value.Reset();
    for (auto& Entry : Session->AncientShields)
        Entry.Value.Reset();
    for (auto& Entry : Session->EraHeadgear)
        Entry.Value.Reset();
    for (auto& Entry : Session->HistoricalDress)
        Entry.Value.Reset();
    for (auto& Entry : Session->HistoricalProxies)
        Entry.Value.Reset();
    for (auto& Entry : Session->HistoricalTack)
        Entry.Value.Reset();
    for (auto& Entry : Session->LooseWeapons)
        Entry.Value.Reset();
    for (auto& Entry : Session->SengokuWeapons)
        Entry.Value.Reset();
    Session->SengokuBanners.Reset();
    if (!bAllRich || bTrainingInfantry)
        for (FInstanceBatch& Batch : Session->ProxyBatches)
        {
            Batch.Transforms.Reset();
            Batch.PartData.Reset();
        }
    if (!bAllRich)
    {
        Session->LastPresentationCamera = PlayerPresentationCamera(GetWorld()->GetFirstPlayerController(), true);
        Session->LastPresentationTime = FPlatformTime::Seconds();
        Session->LodCandidates.Reset();
        Session->VisibleUnits.Init(false, int32(Simulation.units.size()));
        const auto& Camera = Session->LastPresentationCamera;
        const battle::UnitId CameraUnit = Session->IsScriptedPerf() ? Session->PerfOfficer : Simulation.controlled;
        const auto* Officer = battle::FindUnit(Simulation, CameraUnit);
        const bool bOfficerView = Officer && Officer->alive &&
            (Session->IsScriptedPerf() ? Session->PerfCamera.IsValid() : OfficerCamera.IsValid());
        const FVector DetailOrigin = bOfficerView ? Session->WorldLocation(Officer->pose.position, Origin) : Camera.Location;
        const double NearDistance = RegimentRichFigureDistance(int32(Simulation.units.size()));
        for (int32 Index = 0; Index < int32(Simulation.units.size()); ++Index)
        {
            const auto& Unit = Simulation.units[Index];
            if (!Unit.alive)
                continue;
            const bool bControlled = CameraUnit == Unit.id;
            const FVector Ground = Session->WorldLocation(Unit.pose.position, Origin);
            if (bCacheLocations)
                Session->PresentationLocations[Index] = Ground;
            if (Unit.id == HiddenOfficer)
                continue;
            if (const auto* Platform = Session->Platforms.Find(Unit.platform); Platform && Platform->Mesh)
            {
                const auto Bounds = Platform->Mesh->GetBounds();
                const FQuat Facing(FVector::UpVector, Unit.pose.yaw);
                Session->VisibleUnits[Index] = bControlled
                    || Camera.Sees(Ground + Facing.RotateVector(Bounds.Origin), Bounds.SphereRadius);
                continue;
            }
            const FVector Center = Ground + FVector(0, 0, 105);
            const auto* Previous = Session->Units.Find(Unit.id.value);
            const bool bAlreadyRich = Previous && Previous->Figure.IsValid();
            if (!bControlled && !Camera.Sees(Center, bAlreadyRich ? 300.f : 225.f))
                continue;

            Session->VisibleUnits[Index] = true;
            const double DistanceSquared = bOfficerView ? FVector::DistSquaredXY(Center, DetailOrigin)
                : FVector::DistSquared(Center, DetailOrigin);
            const double Limit = bAlreadyRich ? NearDistance * 1.19 : NearDistance;
            if (bControlled || DistanceSquared <= FMath::Square(Limit))
                Session->LodCandidates.Add({Unit.id.value, DistanceSquared, bAlreadyRich, bControlled});
        }
        SelectRegimentRichFigures(Session->LodCandidates, RichFigureLimit, RichPromotionsPerFrame, Session->RichUnitIds);
        Session->bRichPromotionPending = false;
        for (int32 Index = 0; Index < FMath::Min(RichFigureLimit, Session->LodCandidates.Num()); ++Index)
            if (!Session->RichUnitIds.Contains(Session->LodCandidates[Index].Id))
            {
                Session->bRichPromotionPending = true;
                break;
            }
    }
    MeasurePhase(Profile.VisibilityMs);
    int32 BlueAlive = 0;
    int32 OrangeAlive = 0;
    int32 Rich = 0;
    int32 Proxy = 0;
    int32 Culled = 0;
    const auto ProxyTeamColor = [&](int32 Side, const FLinearColor& Default)
    {
        const auto* Faction = Session->Factions[Side];
        return Faction && (Session->bAncient || Faction->Visual != ERegimentEraVisual::Classic) ? Faction->Heraldry : Default;
    };
    const FLinearColor ProxyColors[2] = {
        ProxyTeamColor(0, FLinearColor(.045f, .24f, .78f)), ProxyTeamColor(1, FLinearColor(.76f, .20f, .045f))
    };
    const FVector StandardCameraLocation = bAllRich
        ? PlayerPresentationCamera(GetWorld()->GetFirstPlayerController(), false).Location
        : Session->LastPresentationCamera.Location;
    UMaterialInterface* WoodProp = Session->FigureWood.Get();
    if (!WoodProp)
        WoodProp = TimberMaterial;
    UMaterialInterface* SteelProp = Session->FigureSteel.Get();
    if (!SteelProp)
        SteelProp = WeaponMaterial;

    for (int32 Index = 0; Index < int32(Simulation.units.size()); ++Index)
    {
        const auto& Unit = Simulation.units[Index];
        if (bTrainingInfantry && Session->MissionProgress && Session->MissionProgress->HidesRegiment(Unit.regiment))
            continue;
        const uint64 Id = Unit.id.value;
        const bool bBlue = battle::UnitTeam(Simulation, Unit) == 0;
        const bool bTrainingTarget = bTrainingInfantry && Unit.regiment == PracticeTargets;
        if (!Unit.alive)
        {
            FUnitView Removed;
            if (Session->Units.RemoveAndCopyValue(Id, Removed))
            {
                if (IsBuiltEntity(Mass, Removed.Entity))
                {
                    Mass.Defer().DestroyEntity(Removed.Entity);
                    bHasLifecycleCommands = true;
                }
                if (Removed.Death && Removed.Figure.IsValid() && (bAllRich || Session->Fallen.Num() < RichFallenLimit))
                {
                    const auto& Rig = Session->Rigs[Session->FigureRole(Unit)];
                    const FQuat Upright(FVector::UpVector,
                        FMath::DegreesToRadians(Removed.Facing) - Rig->GetMeshForwardYaw());
                    const FQuat MountRotation(FVector::UpVector, FMath::DegreesToRadians(Removed.Facing));
                    Session->Fallen.Add(Id, FFallenFigure{ Removed, *Removed.Death, Removed.MeshScale, Upright, MountRotation });
                }
                else
                {
                    if (!bTrainingTarget && Removed.Death && Unit.platform == battle::BattlePlatform::None && StaffBlock)
                    {
                        const int32 Side = bBlue ? 0 : 1;
                        auto* Material = FactionMaterials[Side].Get();
                        if (!Material)
                            Material = bBlue ? BlueMarkerMaterial.Get() : OrangeMarkerMaterial.Get();
                        Session->AddFallenProxy(*this, *StaffBlock, Material, Side, Unit, *Removed.Death);
                    }
                    Removed.DestroyFigure();
                }
                Session->Attacks.Remove(Id);
            }
            continue;
        }

        if (bBlue)
            ++BlueAlive;
        else
            ++OrangeAlive;
        FUnitView& View = Session->Units.FindOrAdd(Id);
        const auto& Position = Unit.pose.position;
        const FVector Location = bCacheLocations ? Session->PresentationLocations[Index] :
            Session->WorldLocation(Position, Origin);
        const FQuat Rotation(FVector::UpVector, Unit.pose.yaw);
        const FTransform Transform(Rotation, Location);
        if (IsBuiltEntity(Mass, View.Entity))
        {
            if (bSimulationFrame)
            {
                if (bMassOriginChanged || View.LastMassPose != Unit.pose)
                {
                    Mass.GetFragmentDataChecked<FTransformFragment>(View.Entity).GetMutableTransform() = Transform;
                    View.LastMassPose = Unit.pose;
                    Profile.MassUpdates += bMeasureFrame;
                }
                else
                    Profile.MassUnchanged += bMeasureFrame;
            }
        }
        else
        {
            View.Entity = Mass.ReserveEntity();
            View.LastMassPose = Unit.pose;
            View.Appearance = RegimentFigureAppearance(Id);
            View.IdlePhase = RegimentInitialStridePhase(Id);
            View.StridePhase = View.IdlePhase;
            FRegimentBattleUnitFragment Identity;
            Identity.CoreUnitId = Id;
            Identity.CoreRegimentId = Unit.regiment.value;
            FTransformFragment Pose;
            Pose.GetMutableTransform() = Transform;
            Mass.Defer().PushCommand<FMassCommandBuildEntity>(View.Entity, Identity, Pose);
            bHasLifecycleCommands = true;
        }

        if (Unit.id == HiddenOfficer)
        {
            if (View.Figure.IsValid())
                View.ReleaseRichFigure();
            View.LastLocation = Location;
            ++Culled;
            continue;
        }

        const int32 Side = bBlue ? 0 : 1;
        if (bTrainingTarget && StaffBlock)
        {
            if (bAllRich || Session->VisibleUnits[Index])
                AddTrainingTarget(Session->ProxyBatches[Side], Transform);
            View.LastLocation = Location;
            ++Proxy;
            continue;
        }
        const ECommandFigure CommandFigure = Session->CommandFigure(Unit.id);
        if (auto* Platform = Session->Platforms.Find(Unit.platform); Platform && Platform->Mesh)
        {
            View.bPlatformVisible = bAllRich || Session->VisibleUnits[Index];
            View.LastLocation = Location;
            if (View.bPlatformVisible)
            {
                Platform->Batches[Side].Transforms.Add(Transform);
                ++Proxy;
            }
            else
                ++Culled;
            continue;
        }
        const bool bMounted = Unit.troop == battle::UnitTroop::Mounted;
        const auto* EraFaction = Session->Factions[Side];
        const auto* MountedProfile = bMounted && Unit.regiment && Unit.regiment.value <= MissionRosterCapacity
            ? Session->MountedProfiles[Unit.regiment.value - 1] : nullptr;
        const auto Visual = EraFaction ? EraFaction->Visual : ERegimentEraVisual::Classic;
        const double EraScale = RegimentEraFigureScale(Visual);
        const auto Dress = Session->HistoricalFigures[Index];
        auto* HistoricalDress = Session->HistoricalDress.Find(Dress);
        auto* Headgear = HistoricalDress ? nullptr : Session->EraHeadgear.Find(Visual);
        auto* Tack = bMounted ? Session->HistoricalTack.Find(Dress) : nullptr;
        const auto HandWeapon = HistoricalHandProp(Session->bAncient, Dress, Unit);
        auto* HandProp = Session->LooseWeapons.Find(HandWeapon);
        const bool bHandProp = HandProp && HandProp->Mesh && CommandFigure == ECommandFigure::None;
        const bool bPilum = bHandProp && Unit.ranged_attack && HandWeapon == battle::RangedWeapon::Javelin;
        const bool bHeldLoose = bHandProp && !bPilum && HandWeapon != battle::RangedWeapon::Bow;
        auto* SengokuWeapon = Session->SengokuWeapons.Find(Dress);
        const bool bSengokuWeapon = SengokuWeapon && SengokuWeapon->Mesh;
        const bool bSengokuYumi = bSengokuWeapon && Dress == ERegimentHistoricalFigure::SengokuArcher;
        const bool bSengokuGun = bSengokuWeapon && Dress == ERegimentHistoricalFigure::SengokuArquebusier;
        const bool bSashimono = Visual == ERegimentEraVisual::Sengoku && HistoricalDress && Unit.id.value % 6 == 0;
        const auto AddSashimono = [&](const FQuat& Facing, const FVector& Ground)
        {
            if (bSashimono)
                Session->SengokuBanners.Batches[Side].Transforms.Emplace(Facing,
                    Ground + FVector(0, 0, bMounted ? RegimentHistoricalRiderHeight : 0.0), FVector(EraScale));
        };
        const auto AddHeadgear = [&](const FQuat& Facing, double AboveGround)
        {
            if (Headgear && Headgear->Mesh)
                Headgear->Batches[Side].Transforms.Emplace(Facing, Location + FVector(0, 0, AboveGround), FVector(EraScale));
        };
        const auto AddDress = [&](const FQuat& Facing, const FVector& Ground, double RiderHeight)
        {
            if (HistoricalDress && HistoricalDress->Mesh)
                HistoricalDress->Batches[Side].Transforms.Emplace(
                    Facing, Ground + FVector(0, 0, RiderHeight), FVector(EraScale));
            if (Tack && Tack->Mesh)
                Tack->Batches[Side].Transforms.Emplace(Facing, Ground, FVector(EraScale));
        };
        const bool bMusket = CommandFigure == ECommandFigure::None && RegimentShowsBayonetMusket(Unit, EraFaction);
        const auto ShieldStyle = Session->ShieldStyle(Side, Dress, bMounted, Unit.id.value);
        const bool bFallbackShield = !bMounted &&
            (Dress == ERegimentHistoricalFigure::None || Visual == ERegimentEraVisual::Sengoku);
        const bool bUsesShield = (ShieldStyle.IsSet() || bFallbackShield) && !Unit.archer && !bMusket &&
            CommandFigure == ECommandFigure::None;
        auto* PaintedShield = bUsesShield && ShieldStyle ? Session->AncientShields.Find(*ShieldStyle) : nullptr;
        const FVector ShieldAt = bMounted ? FVector(17, -30, 106 + RegimentHistoricalRiderHeight) : FVector(26, -24, 104);
        const double BasePikeLength = Dress == ERegimentHistoricalFigure::SengokuAshigaru
            ? FMath::Clamp(Unit.weapon.range * 100.0, 245.0, 320.0)
            : Session->PikeLength(Side, bMounted, Dress);
        const double PikeLength = Session->bAncient ? RegimentAncientSpearLength(BasePikeLength, Unit.id.value)
            : BasePikeLength;
        const auto AddProxy = [&]
        {
            auto* HistoricalProxy = Session->HistoricalProxies.Find(Dress);
            const bool bBakedBody = HistoricalProxy && HistoricalProxy->Mesh;
            if (bBakedBody)
            {
                auto& Batch = HistoricalProxy->Batches[Side];
                Batch.Transforms.Emplace(Rotation, Location, FVector(EraScale));
                if (HistoricalProxy->CoatSlot != INDEX_NONE && FigurePartMaterial.IsValid())
                    Batch.AddPartData(View.Appearance.Coat.Body, ERegimentFigureSurface::Leather);
            }
            const bool bShowHandProp = bHandProp && (!bPilum || Unit.ammunition_remaining > 0);
            const bool bShowSengokuWeapon = bSengokuWeapon && !Unit.dagger_drawn;
            AddProxyFigure(Session->ProxyBatches[Side], Transform, Unit, CommandFigure, Dress,
                bBakedBody, bShowHandProp || bShowSengokuWeapon, bMusket, PikeLength, EraScale,
                Session->bAncient || Dress >= ERegimentHistoricalFigure::Egyptian, MountedProfile,
                ProxyColors[Side], View.Appearance);
            AddHeadgear(Rotation, ((bMounted ? 105.0 : 0.0) + 156.0 * ProxyFigureStature(CommandFigure)) * EraScale);
            if (PaintedShield && PaintedShield->Mesh)
                PaintedShield->Batches[Side].Transforms.Emplace(Rotation, Transform.TransformPositionNoScale(ShieldAt));
            if (bShowHandProp)
            {
                const bool bBowProp = HandWeapon == battle::RangedWeapon::Bow;
                const double PropHeight = bMounted ? RegimentHistoricalRiderHeight : 0.0;
                const FVector Grip = Transform.TransformPositionNoScale(FVector(22, bBowProp ? -29 : 29, 92 + PropHeight));
                const FQuat Pitch = FRotator(bBowProp ? 13.0 : -33.0, 0, 0).Quaternion();
                HandProp->Batches[Side].Transforms.Emplace(Rotation * Pitch, Grip, FVector(EraScale));
            }
            if (bShowSengokuWeapon)
            {
                const FVector Grip = Transform.TransformPositionNoScale(FVector(21, bSengokuYumi ? -28 : 28, 94));
                const auto Pitch = FRotator(bSengokuGun ? 8.0 : bSengokuYumi ? -12.0 : -35.0, 0, 0).Quaternion();
                SengokuWeapon->Batches[Side].Transforms.Emplace(Rotation * Pitch, Grip, FVector(EraScale));
            }
            AddSashimono(Rotation, Location);
            ++Proxy;
        };
        if (!bAllRich && !Session->RichUnitIds.Contains(Id))
        {
            if (View.Figure.IsValid())
                View.ReleaseRichFigure();
            if (Session->VisibleUnits[Index])
                AddProxy();
            else
                ++Culled;
            continue;
        }
        const ERegimentFigureRole UnitRole = Session->FigureRole(Unit);
        const bool bDagger = UnitRole == Archer && Unit.dagger_drawn;
        const bool bGunpowder = bMusket || (UnitRole == Archer && battle::UsesGunpowder(Unit.ranged_weapon));
        const bool bArcane = UnitRole == Archer && Unit.ranged_weapon == battle::RangedWeapon::Arcane;
        const double Height = EraScale * (CommandFigure == ECommandFigure::General ? 198.0
            : CommandFigure == ECommandFigure::Commander ? 181.0 : 165.0);
        const bool bPolearm = !bHeldLoose && CommandFigure == ECommandFigure::None && StaffCylinder && SpearPoint &&
            RegimentShowsPolearm(Unit, bMusket, MountedProfile);
        auto& Rig = Session->Rigs[UnitRole];
        const bool bNewRich = !View.Figure.IsValid();
        if (bNewRich)
        {
            auto* Asset = UnitRole == Captain ? CaptainMesh.Get()
                         : UnitRole == Archer ? ArcherMesh.Get() : SwordsmanMesh.Get();
            if (Asset)
            {
                if (!Rig)
                {
                    Rig = MakeUnique<FRegimentFigureRig>(Asset->GetRefSkeleton());
                    if (UnitRole == Archer && Rig->IsValid())
                    {
                        TArray<FTransform> ReleasePose;
                        Session->ReleasedBowGrip = Rig->Evaluate({ 0.f, 0.f, 0.1f, true }, ReleasePose).GetLocation();
                    }
                }
                const auto Bounds = Asset->GetBounds();
                View.MeshScale = FMath::Clamp(Height / FMath::Max(1.0, 2.0 * Bounds.BoxExtent.Z), 0.01, 100.0);
                View.FeetOffset = (Bounds.BoxExtent.Z - Bounds.Origin.Z) * View.MeshScale;
                View.Facing = FMath::RadiansToDegrees(double(Unit.pose.yaw));
                View.LastLocation = Location;
                auto* Figure = NewFigureComponent<UPoseableMeshComponent>(*this, *GetRootComponent());
                Figure->SetSkinnedAssetAndUpdate(Asset);
                auto* Material = AncientFigureMaterials[Side].Get();
                if (!Material)
                    Material = bBlue ? BlueFigureMaterial.Get() : OrangeFigureMaterial.Get();
                if (Material)
                    Figure->SetMaterial(0, Material);
                const auto& Skin = View.Appearance.Skin;
                Figure->SetDefaultCustomPrimitiveDataVector4f(0, FVector4f(Skin.R, Skin.G, Skin.B, 1.f));
                Figure->SetDefaultCustomPrimitiveDataVector4f(4,
                    FVector4f(View.Appearance.ClothShade, View.Appearance.LeatherShade, 0.f, 0.f));
                Figure->RegisterComponent();
                Figure->SetComponentTickEnabled(false);
                View.Figure = Figure;
                if (bMounted && StaffBlock)
                {
                    View.Mount = NewMount(*this, *GetRootComponent(), *StaffBlock, FigurePartMaterial.Get(),
                        TimberMaterial, View.Appearance.Coat);
                    View.FeetOffset += 54.0;
                }
                if (Rig->IsValid())
                {
                    const int32 Team = bBlue ? 0 : 1;
                    auto* Heraldry = FactionMaterials[Team].Get();
                    auto* BannerMaterial = Heraldry ? Heraldry : (bBlue ? BlueFigureMaterial.Get() : OrangeFigureMaterial.Get());
                    if (const auto* Faction = Session->Factions[Team]; Faction && bUsesShield)
                    {
                        const FVector Forward = Rig->GetMeshForward();
                        const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
                        const FVector ShieldPosition = (Forward * ShieldAt.X + Right * ShieldAt.Y +
                            FVector::UpVector * (ShieldAt.Z - View.FeetOffset)) / View.MeshScale;
                        auto* Mesh = StaffBlock.Get();
                        FQuat Orientation = FRotationMatrix::MakeFromXZ(Forward, FVector::UpVector).ToQuat();
                        FVector Scale(.09, .47, .64);
                        if (Faction->Shield == ERegimentEraShield::Round)
                        {
                            Mesh = StaffCylinder.Get();
                            Orientation = FRotationMatrix::MakeFromZX(Forward, FVector::UpVector).ToQuat();
                            Scale = FVector(.56, .56, .09);
                        }
                        else if (Faction->Shield == ERegimentEraShield::Kite)
                        {
                            Mesh = SpearPoint.Get();
                            Orientation = FRotationMatrix::MakeFromZX(-FVector::UpVector, Forward).ToQuat();
                            Scale = FVector(.085, .57, .8);
                        }
                        if (PaintedShield && PaintedShield->Mesh)
                        {
                            auto* Shield = NewFigureProp(*this, *Figure, *PaintedShield->Mesh, nullptr);
                            if (PaintedShield->PaintSlot != INDEX_NONE && Heraldry)
                                Shield->SetMaterial(PaintedShield->PaintSlot, Heraldry);
                            Shield->SetCastShadow(false);
                            Shield->SetRelativeTransform(FTransform(
                                FRotationMatrix::MakeFromXZ(Forward, FVector::UpVector).ToQuat(),
                                ShieldPosition, FVector(1.0 / View.MeshScale)));
                            View.Shield = Shield;
                        }
                        else if (Mesh)
                        {
                            auto* Shield = NewFigureProp(*this, *Figure, *Mesh, BannerMaterial);
                            Shield->SetCastShadow(false);
                            Shield->SetRelativeTransform(FTransform(Orientation, ShieldPosition, Scale / View.MeshScale));
                            View.Shield = Shield;
                        }
                    }
                    if (bSengokuWeapon)
                    {
                        View.Weapon = NewFigureProp(*this, *Figure, *SengokuWeapon->Mesh, nullptr);
                        View.Weapon->SetVisibility(!bDagger);
                    }
                    else if (bGunpowder && StaffBlock)
                    {
                        if (bMusket)
                            View.Weapon = NewBayonetMusket(*this, *Figure, *StaffBlock, FigurePartMaterial.Get(), WoodProp);
                        else
                            View.Weapon = NewFigureProp(*this, *Figure, *StaffBlock, SteelProp);
                        View.Weapon->SetVisibility(bMusket || !bDagger);
                        if (bMusket && SpearPoint)
                            View.Insignia = NewFigureProp(*this, *Figure, *SpearPoint, SteelProp);
                    }
                    else if (bArcane && StaffCylinder && ProjectileSphere)
                    {
                        View.Weapon = NewFigureProp(*this, *Figure, *StaffCylinder, WoodProp);
                        View.Insignia = NewFigureProp(*this, *Figure, *ProjectileSphere, Heraldry);
                        View.Weapon->SetVisibility(!bDagger);
                        View.Insignia->SetVisibility(!bDagger);
                    }
                    else if (bHeldLoose)
                    {
                        View.Weapon = NewFigureProp(*this, *Figure, *HandProp->Mesh, nullptr);
                        View.Weapon->SetVisibility(!bDagger);
                    }
                    else if (UnitRole == Archer && ArcherWeapon)
                    {
                        auto* Bow = NewFigureComponent<USkeletalMeshComponent>(*this, *Figure);
                        Bow->SetSkeletalMeshAsset(ArcherWeapon);
                        if (WeaponMaterial)
                            Bow->SetMaterial(0, WeaponMaterial);
                        Bow->RegisterComponent();
                        Bow->SetComponentTickEnabled(false);
                        View.Weapon = Bow;
                        Bow->SetVisibility(!bDagger);
                    }
                    else if (CommandFigure == ECommandFigure::Standard && StaffCylinder && StaffBlock)
                    {
                        View.Weapon = NewFigureProp(*this, *Figure, *StaffCylinder, WoodProp);
                        View.Insignia = NewFigureProp(*this, *Figure, *StaffBlock, BannerMaterial);
                    }
                    else if (CommandFigure == ECommandFigure::Musician && StaffCylinder)
                    {
                        View.Weapon = NewFigureProp(*this, *Figure, *StaffCylinder,
                            bBlue ? BlueFigureMaterial.Get() : OrangeFigureMaterial.Get());
                        View.Insignia = NewFigureProp(*this, *Figure, *StaffCylinder, SteelProp);
                    }
                    else if (bPolearm)
                    {
                        View.Weapon = NewFigureProp(*this, *Figure, *StaffCylinder, WoodProp);
                        View.Insignia = NewFigureProp(*this, *Figure, *SpearPoint, SteelProp);
                    }
                    else if (auto* Sword = UnitRole == Captain ? CaptainWeapon.Get()
                             : UnitRole == Swordsman ? SwordsmanWeapon.Get() : nullptr)
                    {
                        View.Weapon = NewFigureProp(*this, *Figure, *Sword, WeaponMaterial);
                    }
                    if (bPilum && !bPolearm)
                        View.Insignia = NewFigureProp(*this, *Figure, *HandProp->Mesh, nullptr);
                    if (CommandFigure == ECommandFigure::General && StaffBlock)
                    {
                        auto* Insignia = NewFigureProp(*this, *Figure, *StaffBlock, BannerMaterial);
                        const FVector Forward = Rig->GetMeshForward();
                        const FVector Across = FVector::CrossProduct(FVector::UpVector, Forward);
                        const auto BannerRotation = FRotationMatrix::MakeFromXZ(Across,
                            FVector::UpVector).ToQuat();
                        Insignia->SetRelativeTransform(FTransform(BannerRotation,
                            (-Forward * 45.0 + FVector(0, 0, 192.0)) / View.MeshScale,
                            FVector(.055, .62, .65) / View.MeshScale));
                        View.Insignia = Insignia;
                    }
                }
            }
        }

        if (View.Figure.IsValid())
            ++Rich;
        else if (!bAllRich)
        {
            AddProxy();
            continue;
        }
        if (auto* Figure = View.Figure.Get(); Figure && (bSimulationFrame || bNewRich))
        {
            const auto* Attack = Session->Attacks.Find(Id);
            const bool bRecentAttack = Attack && Simulation.tick - Attack->StartTick < AttackPoseTicks;
            const bool bShowPilum = bPilum && !bPolearm && (bRecentAttack ? Attack->bProjectile : Unit.ammunition_remaining > 0);
            bool bWeaponChanged = bDagger != View.bDagger;
            if (auto* Pilum = bPilum && !bPolearm ? View.Insignia.Get() : nullptr;
                Pilum && Pilum->IsVisible() != bShowPilum)
            {
                Pilum->SetVisibility(bShowPilum);
                bWeaponChanged = true;
            }
            if (auto* Sword = bPilum ? View.Weapon.Get() : nullptr)
                Sword->SetVisibility(!bShowPilum);
            if (bDagger && !bMusket && !View.Dagger.IsValid() && SwordsmanWeapon)
                View.Dagger = NewFigureProp(*this, *Figure, *SwordsmanWeapon, WeaponMaterial);
            if (bWeaponChanged)
            {
                if (auto* Bow = bPilum ? nullptr : View.Weapon.Get())
                    Bow->SetVisibility(bMusket || !bDagger);
                if (bArcane && View.Insignia.IsValid())
                    View.Insignia->SetVisibility(!bDagger);
                if (auto* Dagger = View.Dagger.Get())
                    Dagger->SetVisibility(bDagger);
                View.bDagger = bDagger;
            }
            const double Distance = FVector::Dist2D(Location, View.LastLocation);
            View.LastLocation = Location;
            const bool bControlled = Simulation.controlled == Unit.id;
            const bool bAirborne = Unit.jump_started_tick != 0;
            const bool bParrying = Simulation.tick < Unit.parry_until;
            const double FullGaitSpeed = bControlled ? FMath::Max(170.0, Unit.speed * 90.0) : 170.0;
            const float Gait = FrameSeconds > 0.0 && !bAirborne
                ? FMath::Clamp(float(Distance / FrameSeconds / FullGaitSpeed), 0.f, 1.f) : 0.f;
            View.StridePhase = FMath::Fmod(View.StridePhase + float(Distance * (2.0 * PI / 130.0)), 2.0f * UE_PI);

            float AttackPhase = 0.f;
            float Windup = 0.f;
            double DesiredFacing = FMath::RadiansToDegrees(double(Unit.pose.yaw));
            const bool bBow = UnitRole == Archer && !bDagger;
            const auto* Regiment = battle::FindRegiment(Simulation, Unit.regiment);
            if (Attack && !(Regiment && Regiment->panic) && Attack->bProjectile == (bBow || bShowPilum) &&
                Simulation.outcome == battle::BattleOutcome::Running)
            {
                const double Age = double(Simulation.tick - Attack->StartTick);
                const double WindupTicks = Attack->TelegraphImpactTick
                    ? double(Attack->TelegraphImpactTick - Attack->StartTick) : 0.0;
                const double Duration = WindupTicks > 0.0 ? WindupTicks + 5.0 : AttackPoseTicks;
                if (Age < Duration)
                {
                    if (Age < WindupTicks)
                        Windup = float(FMath::SmoothStep(0.0, WindupTicks * .76, Age));
                    else
                        AttackPhase = WindupTicks > 0.0 ? .5f + float((Age - WindupTicks) * .1)
                            : float(Age / Duration);
                    const auto* Target = battle::FindUnit(Simulation, Attack->TargetId);
                    const FVector AimPosition = Target ? Session->WorldLocation(Target->pose.position, Origin)
                                                       : Attack->Target;
                    const FVector Aim = (AimPosition - Location).GetSafeNormal2D();
                    if (!Aim.IsNearlyZero())
                        DesiredFacing = FMath::RadiansToDegrees(FMath::Atan2(Aim.Y, Aim.X));
                }
            }
            if (bControlled && AttackPhase == 0.f)
            {
                AttackPhase = OfficerInput.SwingPhase();
                if (AttackPhase > 0.f)
                    DesiredFacing = OfficerCameraYaw;
            }
            if (bParrying)
                DesiredFacing = FMath::RadiansToDegrees(double(Unit.parry_yaw));
            View.Facing = FMath::FixedTurn(View.Facing, DesiredFacing, FrameSeconds * 450.0);
            FTransform FigureTransform = Transform;
            const double MotionLift = bMounted ? FMath::Sin(View.StridePhase * 2.0) * Gait * 3.0 : 0.0;
            FigureTransform.AddToTranslation(FVector(0, 0, View.FeetOffset + MotionLift));
            if (auto* Horse = View.Mount.Get())
                Horse->SetWorldTransform(FTransform(FRotator(0.0, View.Facing, 0.0), Location + FVector(0, 0, MotionLift)));
            FigureTransform.SetScale3D(FVector(View.MeshScale));
            FQuat FigureRotation(FVector::UpVector,
                FMath::DegreesToRadians(View.Facing) - Rig->GetMeshForwardYaw());
            const double HitWeight = Simulation.outcome == battle::BattleOutcome::Running ? View.HitWeight(Simulation.tick) : 0.0;
            if (HitWeight > 0.0)
            {
                FigureRotation = FQuat(View.Hit->LeanAxis, FMath::DegreesToRadians(15.0 * HitWeight)) * FigureRotation;
                if (Session->MeleeSmoke && Session->MeleeSmoke->ReactionId == Id)
                {
                    ++Session->MeleeSmoke->Reactions;
                    Session->MeleeSmoke->ReactionId = 0;
                }
            }
            FigureTransform.SetRotation(FigureRotation);
            Figure->SetWorldTransform(FigureTransform);
            const bool bPoseIsMoving = Gait > 0.f || AttackPhase > 0.f || Windup > 0.f || bAirborne || bParrying;
            if ((bPoseIsMoving || View.bPoseWasMoving || !View.bPoseInitialized || bWeaponChanged) &&
                Rig->IsValid() && Figure->BoneSpaceTransforms.Num() > 0)
            {
                const bool bThrust = bPolearm || (bMusket && !bBow) || (bSengokuGun && !bDagger) ||
                    (!bDagger && bHeldLoose && HandWeapon == battle::RangedWeapon::Javelin) || bShowPilum;
                auto Grip = Rig->Evaluate({ View.StridePhase, Gait, AttackPhase, bBow && !bHeldLoose && !bSengokuGun, bMounted, bThrust,
                    bAirborne, bParrying, Windup },
                    Figure->BoneSpaceTransforms);
                Figure->RefreshBoneTransforms();
                if (CommandFigure == ECommandFigure::Standard && View.Insignia.IsValid())
                {
                    const auto Hand = Grip.GetLocation();
                    const double Scale = View.MeshScale;
                    View.StandardHand = Hand;
                    View.Weapon->SetRelativeTransform(FTransform(FQuat::Identity,
                        Hand + FVector(0, 0, 70) / Scale, FVector(.031, .031, 1.85) / Scale));
                }
                else if (CommandFigure == ECommandFigure::Musician && View.Insignia.IsValid())
                {
                    const double Scale = View.MeshScale;
                    const auto Chest = (Rig->GetMeshForward() * 32 + FVector(0, 0, 96)) / Scale;
                    View.Weapon->SetRelativeTransform(FTransform(FQuat::Identity, Chest,
                        FVector(.46, .46, .29) / Scale));
                    View.Insignia->SetRelativeTransform(FTransform(FQuat::Identity,
                        Chest + FVector(0, 0, 15) / Scale, FVector(.48, .48, .045) / Scale));
                }
                else if (bPolearm && View.Weapon.IsValid() && View.Insignia.IsValid())
                {
                    const auto Axis = Grip.GetRotation().GetAxisZ();
                    const double Scale = View.MeshScale;
                    const auto Hand = Grip.GetLocation();
                    const double Length = PikeLength;
                    View.Weapon->SetRelativeTransform(FTransform(Grip.GetRotation(),
                        Hand + Axis * (Length * .24 / Scale), FVector(.048, .048, Length / 100.0) / Scale));
                    View.Insignia->SetRelativeTransform(FTransform(Grip.GetRotation(),
                        Hand + Axis * ((Length * .74 + 10.0) / Scale), FVector(.17, .17, .29) / Scale));
                }
                else if (bSengokuWeapon && !bDagger && View.Weapon.IsValid())
                {
                    if (bSengokuGun || bSengokuYumi)
                    {
                        const auto Up = bSengokuYumi ? Grip.GetRotation().GetAxisZ() : FVector::UpVector;
                        Grip.SetRotation(FRotationMatrix::MakeFromXZ(Rig->GetMeshForward(), Up).ToQuat());
                    }
                    Grip.SetScale3D(FVector(EraScale / View.MeshScale));
                    View.Weapon->SetRelativeTransform(Grip);
                }
                else if (bGunpowder && (bMusket || !bDagger) && View.Weapon.IsValid())
                {
                    const auto Forward = Rig->GetMeshForward();
                    const auto GunRotation = FRotationMatrix::MakeFromXZ(Forward, FVector::UpVector).ToQuat();
                    if (bMusket)
                        View.Weapon->SetRelativeTransform(FTransform(GunRotation, Grip.GetLocation(),
                            FVector(1.0 / View.MeshScale)));
                    else
                        View.Weapon->SetRelativeTransform(FTransform(GunRotation,
                            Grip.GetLocation() + Forward * 30.0 / View.MeshScale,
                            FVector(.88, .052, .063) / View.MeshScale));
                    if (bMusket && View.Insignia.IsValid())
                        View.Insignia->SetRelativeTransform(FTransform(
                            FRotationMatrix::MakeFromZ(Forward).ToQuat(),
                            Grip.GetLocation() + Forward * 83.0 / View.MeshScale,
                            FVector(.035, .035, .24) / View.MeshScale));
                }
                else if (bArcane && !bDagger && View.Weapon.IsValid() && View.Insignia.IsValid())
                {
                    const FVector Hand = Grip.GetLocation();
                    const auto Axis = Grip.GetRotation().GetAxisZ();
                    View.Weapon->SetRelativeTransform(FTransform(Grip.GetRotation(),
                        Hand + Axis * (19.0 / View.MeshScale), FVector(.065, .065, 1.18) / View.MeshScale));
                    View.Insignia->SetRelativeTransform(FTransform(FQuat::Identity,
                        Hand + Axis * (83.0 / View.MeshScale), FVector(.16 / View.MeshScale)));
                }
                else if (bShowPilum && View.Insignia.IsValid())
                {
                    Grip.SetScale3D(FVector(EraScale / View.MeshScale));
                    View.Insignia->SetRelativeTransform(Grip);
                }
                else if (bHeldLoose && !bDagger && View.Weapon.IsValid())
                {
                    Grip.SetScale3D(FVector(EraScale / View.MeshScale));
                    View.Weapon->SetRelativeTransform(Grip);
                }
                else if (auto* Props = bDagger ? static_cast<UMeshComponent*>(View.Dagger.Get()) : View.Weapon.Get())
                {
                    if (bDagger)
                        Grip.SetScale3D(Grip.GetScale3D() * .42);
                    Props->SetRelativeTransform(Grip);
                }
                View.bPoseInitialized = true;
                View.bPoseWasMoving = bPoseIsMoving;
            }
        }
        if (CommandFigure == ECommandFigure::Standard && View.bPoseInitialized && View.Insignia.IsValid())
        {
            const double DistanceSquared = FVector::DistSquared(StandardCameraLocation, Location);
            const double Time = double(DisplayTick) * FRegimentBattlePlayback::StepSeconds;
            const auto Flutter = RegimentStandardFlutter(Time, View.IdlePhase, DistanceSquared).Quaternion();
            const auto Across = FVector::CrossProduct(FVector::UpVector, Rig->GetMeshForward());
            const auto FlagRotation = FRotationMatrix::MakeFromXZ(Across, FVector::UpVector).ToQuat() * Flutter;
            const FVector Center = FVector(0, 0, 131) + FlagRotation.GetAxisX() * 36.0;
            View.Insignia->SetRelativeTransform(FTransform(FlagRotation, View.StandardHand + Center / View.MeshScale,
                FVector(.72, .045, .52) / View.MeshScale));
        }
        const FQuat Facing = FRotator(0, View.Facing, 0).Quaternion();
        AddHeadgear(Facing, Height * .94 + (bMounted ? 54.0 : 0.0));
        if (const auto* Figure = View.Figure.Get())
        {
            const FVector Ground = Figure->GetComponentLocation() - FVector(0, 0, View.FeetOffset);
            AddDress(Facing, Ground, bMounted ? RegimentHistoricalRiderHeight : 0.0);
            AddSashimono(Facing, Ground);
        }
    }
    if (bHasLifecycleCommands)
        Mass.FlushCommands();
    if (bMeasureFrame)
        PhaseStart = FPlatformTime::Seconds();
    if (!bAllRich || bTrainingInfantry)
        for (int32 Side = 0; Side < 2; ++Side)
        {
            auto& Proxies = Session->ProxyBatches[Side];
            if (!Session->ProxyMeshes[Side].IsValid() && StaffBlock && !Proxies.Transforms.IsEmpty())
            {
                auto* Mesh = NewFigureComponent<UInstancedStaticMeshComponent>(*this, *GetRootComponent());
                Mesh->SetStaticMesh(StaffBlock);
                Mesh->SetCastShadow(false);
                const auto* Faction = Session->Factions[Side];
                auto* Material = Session->bAncient || (Faction && Faction->Visual != ERegimentEraVisual::Classic)
                    ? FactionMaterials[Side].Get() : nullptr;
                if (FigurePartMaterial.IsValid())
                {
                    Material = FigurePartMaterial.Get();
                    Mesh->SetNumCustomDataFloats(RegimentFigurePartDataCount);
                }
                else if (!Material)
                    Material = Side == 0 ? BlueMarkerMaterial.Get() : OrangeMarkerMaterial.Get();
                if (Material)
                    Mesh->SetMaterial(0, Material);
                Mesh->RegisterComponent();
                Mesh->SetComponentTickEnabled(false);
                Session->ProxyMeshes[Side] = Mesh;
            }
            if (auto* Mesh = Session->ProxyMeshes[Side].Get())
                Proxies.Present(*Mesh);
        }
    for (auto& Entry : Session->Platforms)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    for (auto& Entry : Session->AncientShields)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    for (auto& Entry : Session->EraHeadgear)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    for (auto& Entry : Session->HistoricalDress)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    for (auto& Entry : Session->HistoricalProxies)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1],
            FigurePartMaterial.Get());
    for (auto& Entry : Session->HistoricalTack)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    for (auto& Entry : Session->LooseWeapons)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    for (auto& Entry : Session->SengokuWeapons)
        Entry.Value.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    Session->SengokuBanners.Present(*this, *GetRootComponent(), FactionMaterials[0], FactionMaterials[1]);
    MeasurePhase(Profile.InstanceSubmitMs);
    if (Simulation.tick > 0)
    {
        const auto* Camera = bAllRich ? nullptr : &Session->LastPresentationCamera;
        Session->PresentArrows(*ArrowVisuals, Camera);
        MeasurePhase(Profile.ProjectilesMs);
        Session->PresentFallen(Camera);
        if (const auto* Controller = GetWorld()->GetFirstPlayerController())
        {
            const auto Fallback = Camera ? FPresentationCamera{} : PlayerPresentationCamera(Controller, true);
            Session->PresentEffects(*this, Origin, Camera ? *Camera : Fallback, OfficerCamera.IsValid());
        }
        if (Session->MeleeSmoke)
        {
            auto& Check = Session->MeleeSmoke.GetValue();
            if (const auto* Fall = Session->Fallen.Find(Check.DeathId))
            {
                const auto* Body = Fall->Components.Figure.Get();
                Check.bCollapsed |= Body && Body == Check.DeadModel.Get()
                    && FVector::Dist2D(Body->GetComponentLocation(), Check.DeathLocation) < 0.02
                    && Body->GetComponentQuat().AngularDistance(Fall->UprightRotation) > 0.35
                    && !IsBuiltEntity(Mass, Check.DeadEntity) && !Session->Units.Contains(Check.DeathId);
            }
        }
    }
    if (bSimulationFrame)
        Session->LastMassOrigin = Origin;
    PresentedTick = DisplayTick;
    HudState.BlueAlive = BlueAlive;
    HudState.OrangeAlive = OrangeAlive;
    HudState.ElapsedSeconds = DisplayTick / FRegimentBattlePlayback::StepsPerSecond;
    if (HudState.Status != ERegimentBattleStatus::Failed)
        HudState.Status = Session->IsOpening() ? ERegimentBattleStatus::FormingUp : BattleStatus(Simulation);
    if (bMeasureFrame)
        PhaseStart = FPlatformTime::Seconds();
    RefreshDecisionEntries();
    MeasurePhase(Profile.HudMs);
    if (bMeasureFrame)
        Session->Perf->RecordFrame(Session->IsPerfOfficerView(), (FPlatformTime::Seconds() - StartTime) * 1000.0,
            {int32(Simulation.units.size()), BlueAlive + OrangeAlive, Rich, Proxy, Culled}, Simulation.tick, Profile);
    return true;
}

int32 ARegimentBattleField::CountFigures() const
{
    int32 Count = 0;
    for (const auto& Entry : Session->Units)
    {
        if (const auto* Figure = Entry.Value.Figure.Get(); Entry.Value.bPlatformVisible || (Figure && Figure->IsRegistered()))
            ++Count;
    }
    return Count;
}

int32 ARegimentBattleField::CountValidEntities() const
{
    const auto& Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager();
    TSet<FMassEntityHandle> Unique;
    for (const auto& Entry : Session->Units)
    {
        if (IsBuiltEntity(Mass, Entry.Value.Entity) &&
            Mass.GetFragmentDataChecked<FRegimentBattleUnitFragment>(Entry.Value.Entity).CoreUnitId == Entry.Key)
            Unique.Add(Entry.Value.Entity);
    }
    return Unique.Num();
}

int32 ARegimentBattleField::CountMatchingPoses() const
{
    const auto& Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager();
    int32 Count = 0;
    for (const auto& Unit : Session->Simulation.units)
    {
        const auto* View = Session->Units.Find(Unit.id.value);
        if (!Unit.alive || !View || !IsBuiltEntity(Mass, View->Entity))
            continue;

        const auto& MapPose = Unit.pose;
        const FVector ExpectedLocation = Session->WorldLocation(MapPose.position, GetBattleOrigin());
        const FQuat CoreRotation(FRotator(0, FMath::RadiansToDegrees(MapPose.yaw), 0));
        const FTransform Actual = ReadUnitTransform(Unit.id.value);
        if (!Actual.GetLocation().Equals(ExpectedLocation, 0.02) || !Actual.GetRotation().Equals(CoreRotation, 0.001))
            continue;
        if (View->bPlatformVisible)
        {
            ++Count;
            continue;
        }
        if (!View->Figure.IsValid())
            continue;
        const FVector FigureForward = View->Figure->GetComponentQuat().RotateVector(
            Session->Rigs[Session->FigureRole(Unit)]->GetMeshForward());
        const FVector ExpectedForward = FRotator(0.0, View->Facing, 0.0).Vector();
        const bool bFacingMatches = View->HitWeight(Session->Simulation.tick) > 0.0
            ? FVector::DotProduct(FigureForward, ExpectedForward) > 0.96
            : FigureForward.Equals(ExpectedForward, 0.001);
        if (View->Figure->GetComponentLocation().Equals(
                Actual.GetLocation() + FVector(0, 0, View->FeetOffset), View->Mount.IsValid() ? 3.1 : 0.02) &&
            bFacingMatches)
            ++Count;
    }
    return Count;
}

FTransform ARegimentBattleField::ReadUnitTransform(uint64 UnitId) const
{
    const auto& Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager();
    const auto* View = Session->Units.Find(UnitId);
    return View && IsBuiltEntity(Mass, View->Entity)
        ? Mass.GetFragmentDataChecked<FTransformFragment>(View->Entity).GetTransform()
        : FTransform::Identity;
}

void ARegimentBattleField::ClearBattle()
{
    RestoreOfficerCamera();
    OfficerHudState = {};
    OfficerNoticeSeconds = 0.f;
    PossessedOfficer = SelectedOfficerRegiment = 0;
    if (Session)
    {
        Session->Trace.Finish(Session->Simulation);
        if (auto* Kick = Session->ArtilleryKick.Get())
        {
            Kick->ResetKick();
            if (const auto* Controller = GetWorld()->GetFirstPlayerController(); Controller && Controller->PlayerCameraManager)
                Controller->PlayerCameraManager->RemoveCameraModifier(Kick);
        }
        if (auto* Camera = Session->PerfCamera.Get())
            Camera->Destroy();
        Session->Smoke.Clear();
        Session->ReportPerf();
        if (auto* Subsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
        {
            auto& Mass = Subsystem->GetMutableEntityManager();
            for (const auto& Entry : Session->Units)
            {
                if (IsBuiltEntity(Mass, Entry.Value.Entity))
                    Mass.Defer().DestroyEntity(Entry.Value.Entity);
            }
            if (!Mass.IsProcessing())
                Mass.FlushCommands();
        }
        for (const auto& Entry : Session->Units)
            Entry.Value.DestroyFigure();
        for (const auto& Entry : Session->Fallen)
            Entry.Value.Components.DestroyFigure();
        for (const auto& Mesh : Session->FallenMeshes)
            if (auto* Component = Mesh.Get())
                Component->DestroyComponent();
        for (const auto& Mesh : Session->ProxyMeshes)
            if (auto* Component = Mesh.Get())
                Component->DestroyComponent();
        for (const auto& Entry : Session->Platforms)
            Entry.Value.Destroy();
        for (const auto& Entry : Session->AncientShields)
            Entry.Value.Destroy();
        for (const auto& Entry : Session->EraHeadgear)
            Entry.Value.Destroy();
        for (const auto& Entry : Session->HistoricalDress)
            Entry.Value.Destroy();
        for (const auto& Entry : Session->HistoricalProxies)
            Entry.Value.Destroy();
        for (const auto& Entry : Session->HistoricalTack)
            Entry.Value.Destroy();
        for (const auto& Entry : Session->LooseWeapons)
            Entry.Value.Destroy();
        for (const auto& Entry : Session->SengokuWeapons)
            Entry.Value.Destroy();
        Session->SengokuBanners.Destroy();
        if (auto* Stones = Session->StoneVisuals.Get())
            Stones->DestroyComponent();
        if (auto* Arcane = Session->ArcaneVisuals.Get())
            Arcane->DestroyComponent();
        ArrowVisuals->ClearInstances();
        Session.Reset();
    }
}

void ARegimentBattleField::RemoveHud()
{
    if (auto* Player = HudPlayer.Get(); Player && Player->ViewportClient && BattleHud.IsValid())
        Player->ViewportClient->RemoveViewportWidgetForPlayer(Player, BattleHud.ToSharedRef());
    BattleHud.Reset();
    HudPlayer.Reset();
}

void ARegimentBattleField::EndPlay(const EEndPlayReason::Type Reason)
{
    RemoveHud();
    ClearBattle();
    Mission.Reset();
    Super::EndPlay(Reason);
}

#undef LOCTEXT_NAMESPACE
