#include "RegimentMissionTutorial.h"

#include "RegimentBattlePlayback.h"
#include "RegimentEraCatalog.h"
#include "RegimentOfficerSettings.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Combat.h"
#include "battle/Formation.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "RegimentMissionTutorial"

namespace
{
constexpr battle::RegimentId Lead{1}, Reserve{2}, FirstEnemy{MissionRegimentsPerTeam + 1},
    SecondEnemy{MissionRegimentsPerTeam + 2};
constexpr battle::Tick TrainingSafety = std::numeric_limits<battle::Tick>::max();
constexpr float BridgeY = battle::MarchCrossings.back().y;
constexpr float BridgeWidth = battle::MarchCrossings.back().half_width;
constexpr float FarBank = battle::MarchRiverHalfWidth + 2.f;
constexpr float LongShotDistance = 80.f;
constexpr battle::Vec3 BridgeEntry{-12.f, BridgeY, 0.f};
constexpr battle::Vec3 BridgeExit{24.f, BridgeY, 0.f};

void Place(FMissionPreset& Preset, battle::RegimentId Regiment, FMissionRosterEntry Entry, float X, float Y, int32 Group = 2)
{
    Entry.StartX = X;
    Entry.StartY = Y;
    Entry.Group = Group;
    Preset.Roster[int32(Regiment.value - 1)] = Entry.Clamped();
}

void Rename(battle::Scenario& Scenario, battle::RegimentId Id, const FText& Name)
{
    const std::string Label = TCHAR_TO_UTF8(*Name.ToString());
    for (auto& Regiment : Scenario.regiments)
        if (Regiment.id == Id)
            Regiment.name = Label;
    if (Scenario.command_setup)
        for (auto& Node : Scenario.command_setup->hierarchy.nodes)
            if (Node.regiment == Id)
                Node.name = Label;
}

void SetGunSafety(battle::World& World, battle::RegimentId Id, bool bOn)
{
    for (auto& Unit : World.units)
        if (Unit.regiment == Id && (Unit.archer || Unit.ranged_attack))
        {
            if (bOn)
                Unit.ready_tick = TrainingSafety;
            else if (Unit.ready_tick == TrainingSafety)
                Unit.ready_tick = World.tick;
        }
}

const battle::UnitState* ControlledOfficer(const battle::World& World, battle::RegimentId Id)
{
    const auto* Unit = battle::FindUnit(World, World.controlled);
    return Unit && Unit->alive && Unit->regiment == Id ? Unit : nullptr;
}

void HoldExercisePosition(battle::World& World, const battle::RegimentState& Regiment,
    int32 Priority = battle::PlayerOrderPriority - 1)
{
    battle::Order Hold;
    Hold.recipient = Regiment.id;
    Hold.point = Regiment.pose.position;
    Hold.facing = Regiment.pose.yaw;
    Hold.type = battle::OrderType::Hold;
    Hold.priority = Priority;
    Hold.created_tick = Hold.delivery_tick = World.tick;
    Hold.player_direct = true;
    battle::SubmitOrder(World, Hold);
}

bool HasPlayerOrder(const battle::RegimentState& Regiment, battle::Tick Since, battle::OrderType Type)
{
    const auto& Order = Regiment.order;
    return Order && Order->created_tick >= Since && Order->type == Type && Order->issuer &&
        Order->priority == battle::PlayerOrderPriority &&
        (Order->status == battle::OrderStatus::Active || Order->status == battle::OrderStatus::Completed);
}

bool FiredOn(const battle::World& World, const battle::BattleEvent& Event, battle::RegimentId Shooter,
    battle::RegimentId Target, bool bProjectile)
{
    const auto* Attacker = battle::FindUnit(World, Event.source);
    const auto* Defender = battle::FindUnit(World, Event.target);
    return Event.projectile == bProjectile && Attacker && Defender &&
        Attacker->regiment == Shooter && Defender->regiment == Target;
}

void Rout(battle::RegimentState& Regiment)
{
    Regiment.morale = 0.f;
    Regiment.cannot_rally = true;
}

void PlaceRoutedFormationAtExit(battle::World& World, battle::RegimentState& Regiment)
{
    if (!World.scenario.field_bounds)
        return;
    const auto Shape = battle::Footprint(Regiment);
    const auto Limit = battle::TerrainFieldInset(*World.scenario.field_bounds, Regiment.pose.yaw,
        Shape.depth_m * .5f, Shape.width_m * .5f);
    const float ExitY = FMath::Sign(Regiment.pose.position.y) * Limit.y;
    const auto Offset = battle::Vec3{0.f, ExitY - Regiment.pose.position.y, 0.f};
    Regiment.route.clear();
    Regiment.waypoint = 0;
    Regiment.retreat_waypoint.reset();
    Regiment.pose.position = Regiment.pose.position + Offset;
    for (auto& Slot : Regiment.slots)
        Slot.position = Slot.position + Offset;
    for (const auto Id : Regiment.units)
        if (auto* Unit = battle::FindUnit(World, Id))
        {
            Unit->pose.position = Unit->pose.position + Offset;
            Unit->slot.position = Unit->slot.position + Offset;
        }
}

battle::ScenarioObjective Zone(battle::Vec3 Point, float Radius)
{
    return {Point, Radius};
}

FText TutorialKey(const FKey& Key)
{
    if (Key == EKeys::LeftMouseButton)
        return LOCTEXT("LeftMouse", "ЛКМ");
    if (Key == EKeys::RightMouseButton)
        return LOCTEXT("RightMouse", "ПКМ");
    if (Key == EKeys::MiddleMouseButton)
        return LOCTEXT("MiddleMouse", "СКМ");
    return Key.GetDisplayName();
}

const battle::command::CommandNode* ControlledCommand(const battle::World& World, battle::command::Level Level)
{
    if (!World.controlled || !World.command_state)
        return nullptr;
    const auto Officer = std::ranges::find(World.officers, World.controlled, &battle::OfficerSpec::unit);
    if (Officer == World.officers.end())
        return nullptr;
    const auto& Nodes = World.command_state->hierarchy.nodes;
    const auto Node = std::ranges::find_if(Nodes, [&](const auto& Entry)
    { return Entry.team == 0 && Entry.level == Level && Entry.commander == Officer->id; });
    return Node == Nodes.end() ? nullptr : &*Node;
}
}

TConstArrayView<RegimentMissionTutorial::FLesson> RegimentMissionTutorial::Lessons()
{
    static const FLesson Catalogue[] = {
        { ERegimentTutorial::Infantry, LOCTEXT("Infantry", "Пехота и переправа"),
          LOCTEXT("InfantryDescription", "Возьмите полк из 30 человек, проведите его через мост и отработайте атаку и отход.") },
        { ERegimentTutorial::Cavalry, LOCTEXT("Cavalry", "Кавалерия и резерв"),
          LOCTEXT("CavalryDescription", "Возьмите конный полк с карты, обойдите противника и введите резервную пехоту.") },
        { ERegimentTutorial::Artillery, LOCTEXT("Artillery", "Артиллерийская батарея"),
          LOCTEXT("ArtilleryDescription", "Вручную наведите полевую пушку и поразите отряд вдалеке.") },
        { ERegimentTutorial::Command, LOCTEXT("Command", "Группа и армия"),
          LOCTEXT("CommandDescription", "Откройте панели группы и армии и передайте приказы нескольким полкам.") }
    };
    return MakeArrayView(Catalogue);
}

bool RegimentMissionTutorial::Load(ERegimentTutorial Id, FMissionPreset& Out)
{
    const auto* Lesson = Lessons().FindByPredicate([Id](const auto& Entry) { return Entry.Id == Id; });
    const auto* Era = RegimentEraCatalog::Find(TEXT("napoleonic"));
    if (!Lesson || !Era)
        return false;

    FMissionPreset Preset;
    RegimentMissionPresets::ApplyEra(Preset, *Era);
    Preset.Tutorial = Id;
    Preset.Name = Lesson->Name.ToString();
    Preset.Briefing = Lesson->Description.ToString();
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.bDirectDeployment = true;
    for (auto& Entry : Preset.Roster)
        Entry.bEnabled = false;

    switch (Id)
    {
    case ERegimentTutorial::Infantry:
        Preset.Terrain = battle::MarchTerrain::River;
        Preset.Field = {260, 96};
        Place(Preset, Lead, {7, 20}, -22.f, BridgeY);
        Place(Preset, FirstEnemy, {9, 0}, 34.f, BridgeY);
        Place(Preset, SecondEnemy, {37, 0}, 65.f, BridgeY);
        break;
    case ERegimentTutorial::Cavalry:
    {
        Preset.Field = {132, 96};
        FMissionRosterEntry Horse{0, 0};
        Horse.bCavalry = true;
        Horse.Mounted = 21;
        Place(Preset, Lead, Horse, -21.f, -16.f, 1);
        FMissionRosterEntry Infantry{12, 20};
        Infantry.FormationFiles = 5;
        Place(Preset, Reserve, Infantry, -28.f, 0.f, 2);
        Place(Preset, FirstEnemy, {36, 0}, 10.f, 0.f);
        Place(Preset, SecondEnemy, {25, 0}, 33.f, 12.f);
        break;
    }
    case ERegimentTutorial::Artillery:
    {
        Preset.Field = {240, 90};
        FMissionRosterEntry Battery{0, 0};
        Battery.Platform = battle::BattlePlatform::FieldGun;
        Battery.PlatformCount = 1;
        Place(Preset, Lead, Battery, -60.f, 0.f);
        Place(Preset, FirstEnemy, {54, 0}, 50.f, 0.f);
        break;
    }
    case ERegimentTutorial::Command:
        Preset.Field = {216, 150};
        for (int32 Team = 0; Team < 2; ++Team)
            for (int32 Slot = 0; Slot < MissionRegimentsPerTeam; ++Slot)
            {
                const int32 Group = Slot / 2 + 1;
                const float Side = Team == 0 ? -1.f : 1.f;
                const int32 Index = MissionRosterIndex(Team, Slot);
                Place(Preset, {uint64(Index + 1)}, {38, 20}, Side * (63.f + 12.f * (Slot % 2)),
                    float(Group - 2) * 37.f, Group);
            }
        break;
    case ERegimentTutorial::None:
        return false;
    }
    Out = MoveTemp(Preset);
    return true;
}

void RegimentMissionTutorial::PrepareScenario(ERegimentTutorial Id, battle::Scenario& Scenario)
{
    if (Id == ERegimentTutorial::None || Id == ERegimentTutorial::Command)
        return;
    if (Scenario.command_setup)
        Scenario.command_setup->missions.clear();
    if (Id == ERegimentTutorial::Infantry)
    {
        Rename(Scenario, Lead, LOCTEXT("TrainingInfantry", "Учебный пехотный полк"));
        Rename(Scenario, FirstEnemy, LOCTEXT("PracticeTargets", "Мишени · учебный отряд"));
        Rename(Scenario, SecondEnemy, LOCTEXT("OpposingInfantry", "Полк противника"));
    }
    else if (Id == ERegimentTutorial::Cavalry)
    {
        Rename(Scenario, Lead, LOCTEXT("TrainingCavalry", "Учебный кавалерийский полк"));
        Rename(Scenario, Reserve, LOCTEXT("TrainingReserve", "Пехотный резерв"));
    }
    else if (Id == ERegimentTutorial::Artillery)
        Rename(Scenario, Lead, LOCTEXT("TrainingBattery", "Учебная полевая батарея"));
}

FRegimentTutorialProgress::FRegimentTutorialProgress(ERegimentTutorial Id, battle::World& World)
    : Lesson(Id), Step(EStep::InfantryMove)
{
    switch (Lesson)
    {
    case ERegimentTutorial::Infantry:
        SetGunSafety(World, Lead, true);
        break;
    case ERegimentTutorial::Cavalry:
        Step = EStep::CavalryPossess;
        SetGunSafety(World, Reserve, true);
        if (auto* Infantry = battle::FindRegiment(World, Reserve))
            Infantry->manual_frontage = true;
        break;
    case ERegimentTutorial::Artillery:
        Step = EStep::ArtilleryShoot;
        SetGunSafety(World, Lead, true);
        if (const auto* Target = battle::FindRegiment(World, FirstEnemy))
            TargetZone = Zone(Target->pose.position, 4.f);
        break;
    case ERegimentTutorial::Command:
        Step = EStep::GroupPossess;
        return;
    case ERegimentTutorial::None:
        return;
    }
    for (const auto& Regiment : World.regiments)
        HoldExercisePosition(World, Regiment);
}

bool FRegimentTutorialProgress::StartsPossessed() const
{
    return Lesson == ERegimentTutorial::Infantry || Lesson == ERegimentTutorial::Artillery;
}

bool FRegimentTutorialProgress::AwaitsReserveRelease(battle::RegimentId Regiment) const
{
    return Lesson == ERegimentTutorial::Cavalry && Regiment == Reserve && Step < EStep::ReservePossess;
}

bool FRegimentTutorialProgress::HidesRegiment(battle::RegimentId Regiment) const
{
    return Lesson == ERegimentTutorial::Infantry && Regiment == SecondEnemy && Step < EStep::InfantryAttack;
}

void FRegimentTutorialProgress::Advance(EStep Next, const battle::World& World)
{
    Step = Next;
    StepStarted = World.tick;
    TargetZone.reset();
    StartLocation.reset();
}

void FRegimentTutorialProgress::Finish(battle::World& World)
{
    Result = LOCTEXT("Completed", "Урок пройден.");
    TargetZone.reset();
    World.outcome = battle::BattleOutcome::Victory;
    World.winner_team = 0;
}

void FRegimentTutorialProgress::Fail(battle::World& World)
{
    Result = LOCTEXT("Interrupted", "Урок прерван. Нажмите «Сначала», чтобы повторить упражнение.");
    TargetZone.reset();
    World.outcome = battle::BattleOutcome::Victory;
    World.winner_team = 1;
}

void FRegimentTutorialProgress::Update(battle::World& World, bool bOfficerCommandsOpen)
{
    if (!Result.IsEmpty())
        return;
    switch (Lesson)
    {
    case ERegimentTutorial::Infantry: UpdateInfantry(World); break;
    case ERegimentTutorial::Cavalry: UpdateCavalry(World); break;
    case ERegimentTutorial::Artillery: UpdateArtillery(World); break;
    case ERegimentTutorial::Command: UpdateCommand(World, bOfficerCommandsOpen); break;
    case ERegimentTutorial::None: return;
    }
    if (Result.IsEmpty() && World.outcome != battle::BattleOutcome::Running)
        Fail(World);
}

void FRegimentTutorialProgress::UpdateInfantry(battle::World& World)
{
    const auto* Officer = ControlledOfficer(World, Lead);
    const auto* Infantry = battle::FindRegiment(World, Lead);
    auto* Targets = battle::FindRegiment(World, FirstEnemy);
    const auto* Opponent = battle::FindRegiment(World, SecondEnemy);
    if (!Officer || !Infantry || !Targets || !Opponent)
        return;

    const auto PlayerWithdrew = [&]
    {
        return HasPlayerOrder(*Infantry, StepStarted, battle::OrderType::FallBack) ||
            HasPlayerOrder(*Infantry, StepStarted, battle::OrderType::Withdraw);
    };
    switch (Step)
    {
    case EStep::InfantryMove:
        if (!StartLocation)
            StartLocation = Officer->pose.position;
        if (battle::Distance(*StartLocation, Officer->pose.position) >= 2.f)
            Advance(EStep::InfantryFollow, World);
        break;
    case EStep::InfantryFollow:
        if (Infantry->following_officer == Officer->id)
            Advance(EStep::InfantryColumn, World);
        break;
    case EStep::InfantryColumn:
        if (Infantry->manual_frontage && !battle::IsLinearFiringLine(*Infantry))
        {
            Advance(EStep::InfantryCross, World);
            TargetZone = Zone(BridgeEntry, 1.f);
        }
        break;
    case EStep::InfantryCross:
    {
        if (TargetZone->center == BridgeEntry &&
            FMath::Abs(Officer->pose.position.x - BridgeEntry.x) <= 1.f &&
            FMath::Abs(Officer->pose.position.y - BridgeY) <= .45f)
            TargetZone = Zone(BridgeExit, 3.5f);

        int32 Across = 0;
        for (const auto Id : Infantry->units)
        {
            const auto* Soldier = battle::FindUnit(World, Id);
            if (!Soldier || !Soldier->alive || Soldier->control != battle::ControlMode::FormationControlled)
                continue;
            const auto Position = Soldier->pose.position;
            if (Position.x <= -FarBank)
                CrossedBridge.Remove(Id.value);
            else if (FMath::Abs(Position.x) <= .8f && FMath::Abs(Position.y - BridgeY) <= BridgeWidth)
                CrossedBridge.Add(Id.value);
            if (Position.x >= FarBank && CrossedBridge.Contains(Id.value))
                ++Across;
        }
        if (Infantry->pose.position.x >= 13.f && uint64(Across * 2) >= Infantry->initial_count)
            Advance(EStep::InfantryLine, World);
        break;
    }
    case EStep::InfantryLine:
        if (battle::IsLinearFiringLine(*Infantry) && Infantry->manual_frontage && !battle::IsDeploying(World, *Infantry))
        {
            Advance(EStep::InfantryShoot, World);
            TargetZone = Zone(Targets->pose.position, 3.f);
        }
        break;
    case EStep::InfantryShoot:
        if (HasPlayerOrder(*Infantry, StepStarted, battle::OrderType::Hold))
            SetGunSafety(World, Lead, false);
        if (std::ranges::any_of(World.events, [&](const auto& Event)
            { return Event.kind == battle::EventKind::Hit && FiredOn(World, Event, Lead, FirstEnemy, true); }))
        {
            SetGunSafety(World, Lead, true);
            Rout(*Targets);
            PlaceRoutedFormationAtExit(World, *Targets);
            battle::Order Approach;
            Approach.recipient = Opponent->id;
            Approach.type = battle::OrderType::MoveTo;
            Approach.point = {39.f, BridgeY, 0.f};
            Approach.facing = Opponent->pose.yaw;
            Approach.created_tick = Approach.delivery_tick = World.tick;
            Approach.priority = battle::PlayerOrderPriority + 1;
            Approach.player_direct = true;
            Approach.pace = battle::MovementPace::March;
            battle::SubmitOrder(World, Approach);
            Advance(EStep::InfantryEnemyApproaches, World);
        }
        break;
    case EStep::InfantryEnemyApproaches:
        if (battle::Distance(Opponent->pose.position, Infantry->pose.position) < 42.f ||
            World.tick - StepStarted >= 6 * FRegimentBattlePlayback::StepsPerSecond)
        {
            Advance(EStep::InfantryAttack, World);
            TargetZone = Zone(Opponent->pose.position, 4.f);
        }
        break;
    case EStep::InfantryAttack:
        if (Opponent->units.empty())
            return;
        TargetZone = Zone(Opponent->pose.position, 4.f);
        if (HasPlayerOrder(*Infantry, StepStarted, battle::OrderType::Attack) &&
            Infantry->order->target_regiment == SecondEnemy)
        {
            SetGunSafety(World, Lead, false);
            Advance(EStep::InfantryWithdraw, World);
            StartLocation = Infantry->pose.position;
            auto Fallback = *StartLocation - battle::Forward(Infantry->pose.yaw) * 9.f;
            Fallback.x = FMath::Max(10.f, Fallback.x);
            TargetZone = Zone(Fallback, 3.f);
        }
        break;
    case EStep::InfantryWithdraw:
        if (PlayerWithdrew() && StartLocation && battle::Distance(*StartLocation, Infantry->pose.position) >= 2.f)
            Finish(World);
        break;
    default: break;
    }
}

void FRegimentTutorialProgress::UpdateCavalry(battle::World& World)
{
    auto* Horse = battle::FindRegiment(World, Lead);
    const auto* Infantry = battle::FindRegiment(World, Reserve);
    const auto* Opponent = battle::FindRegiment(World, FirstEnemy);
    if (!Horse || !Infantry || !Opponent)
        return;
    if (Horse->units.empty())
    {
        if (Step < EStep::CavalryWithdraw)
        {
            Fail(World);
            return;
        }
        if (Step < EStep::ReservePossess)
            Advance(EStep::ReservePossess, World);
    }

    switch (Step)
    {
    case EStep::CavalryPossess:
        if (ControlledOfficer(World, Lead))
        {
            Advance(EStep::CavalryFlank, World);
            TargetZone = Zone(Opponent->pose.position + battle::Right(Opponent->pose.yaw) * 18.f, 4.f);
        }
        break;
    case EStep::CavalryFlank:
        if (battle::Distance(Horse->pose.position, TargetZone->center) <= TargetZone->radius_m)
        {
            Advance(EStep::CavalryFlankAttack, World);
            TargetZone = Zone(Opponent->pose.position, 4.f);
        }
        break;
    case EStep::CavalryFlankAttack:
    {
        const bool bAttack = HasPlayerOrder(*Horse, StepStarted, battle::OrderType::Attack) &&
            Horse->order->target_regiment == FirstEnemy;
        const auto Bearing = Horse->pose.position - Opponent->pose.position;
        if (!bAttack)
            StartLocation.reset();
        else if (!StartLocation &&
            FMath::Abs(battle::Dot(Bearing, battle::Forward(Opponent->pose.yaw))) <= .5f * battle::Length(Bearing))
            StartLocation = Horse->pose.position;
        if (StartLocation && battle::Distance(*StartLocation, Horse->pose.position) >= 2.5f)
        {
            // End the first run-up before the ranks meet so the next route can peel away.
            HoldExercisePosition(World, *Horse, battle::PlayerOrderPriority);
            Advance(EStep::CavalryRear, World);
            TargetZone = Zone(Opponent->pose.position - battle::Forward(Opponent->pose.yaw) * 15.f, 4.f);
        }
        break;
    }
    case EStep::CavalryRear:
        if (battle::Distance(Horse->pose.position, TargetZone->center) <= TargetZone->radius_m)
        {
            Advance(EStep::CavalryAttack, World);
            TargetZone = Zone(Opponent->pose.position, 4.f);
        }
        break;
    case EStep::CavalryAttack:
    {
        const bool bAttack = HasPlayerOrder(*Horse, StepStarted, battle::OrderType::Attack) &&
            Horse->order->target_regiment == FirstEnemy;
        const auto Bearing = Horse->pose.position - Opponent->pose.position;
        bRearAttackOrdered |= bAttack &&
            battle::Dot(Bearing, battle::Forward(Opponent->pose.yaw)) <= -.45f * battle::Length(Bearing);
        if (bAttack && std::ranges::any_of(World.events, [&](const auto& Event)
        {
            return Event.kind == battle::EventKind::AttackLaunched && FiredOn(World, Event, Lead, FirstEnemy, false) &&
                (bRearAttackOrdered || Event.aspect == battle::AttackAspect::Rear);
        }))
        {
            Advance(EStep::CavalryWithdraw, World);
            StartLocation = Horse->pose.position;
            TargetZone = Zone(*StartLocation - battle::Forward(Horse->pose.yaw) * 12.f, 4.f);
        }
        break;
    }
    case EStep::CavalryWithdraw:
        if ((HasPlayerOrder(*Horse, StepStarted, battle::OrderType::FallBack) ||
             HasPlayerOrder(*Horse, StepStarted, battle::OrderType::Withdraw)) &&
             StartLocation && battle::Distance(*StartLocation, Horse->pose.position) >= 3.f)
        {
            Rout(*Horse);
            Advance(EStep::CavalryLoss, World);
        }
        break;
    case EStep::CavalryLoss:
        // A routed exercise unit normally runs off the field. If its native route
        // stalls, put it at the exit and let BattleCore remove it on the next tick.
        if (Horse->panic && World.tick - StepStarted >= 30 * FRegimentBattlePlayback::StepsPerSecond)
            PlaceRoutedFormationAtExit(World, *Horse);
        break;
    case EStep::ReservePossess:
        if (ControlledOfficer(World, Reserve))
            Advance(EStep::ReserveLine, World);
        break;
    case EStep::ReserveLine:
        if (Infantry->manual_frontage && battle::IsLinearFiringLine(*Infantry) && !battle::IsDeploying(World, *Infantry))
        {
            Advance(EStep::ReserveShoot, World);
            const auto* Target = Opponent->units.empty() ? battle::FindRegiment(World, SecondEnemy) : Opponent;
            if (Target)
                TargetZone = Zone(Target->pose.position, 4.f);
        }
        break;
    case EStep::ReserveShoot:
        if (HasPlayerOrder(*Infantry, StepStarted, battle::OrderType::Hold))
            SetGunSafety(World, Reserve, false);
        if (std::ranges::any_of(World.events, [&](const auto& Event)
            {
                return Event.kind == battle::EventKind::AttackLaunched &&
                    (FiredOn(World, Event, Reserve, FirstEnemy, true) || FiredOn(World, Event, Reserve, SecondEnemy, true));
            }))
            Finish(World);
        break;
    default: break;
    }
}

void FRegimentTutorialProgress::UpdateArtillery(battle::World& World)
{
    const bool bAtBattery = ControlledOfficer(World, Lead) != nullptr;
    SetGunSafety(World, Lead, !bAtBattery);
    for (const auto& Event : World.events)
    {
        if (!FiredOn(World, Event, Lead, FirstEnemy, true) || Event.reason != battle::FieldGunRoundShotReason)
            continue;
        if (Event.kind == battle::EventKind::AttackLaunched && bAtBattery)
        {
            const auto* Gun = battle::FindUnit(World, Event.source);
            const auto* Target = battle::FindUnit(World, Event.target);
            if (Gun->platform == battle::BattlePlatform::FieldGun &&
                battle::Distance(Gun->pose.position, Target->pose.position) >= LongShotDistance)
                ArtilleryLaunches.Add(Event.id.value);
        }
        else if (Event.kind == battle::EventKind::Hit && ArtilleryLaunches.Contains(Event.cause.value))
            Finish(World);
        else if (Event.kind == battle::EventKind::Miss)
            ArtilleryLaunches.Remove(Event.cause.value);
    }
}

void FRegimentTutorialProgress::UpdateCommand(battle::World& World, bool bOfficerCommandsOpen)
{
    using namespace battle::command;
    const auto* Group = ControlledCommand(World, Level::Group);
    const auto* Army = ControlledCommand(World, Level::Army);
    switch (Step)
    {
    case EStep::GroupPossess:
        if (Group)
            Advance(EStep::GroupPanel, World);
        break;
    case EStep::GroupPanel:
        if (Group && bOfficerCommandsOpen)
            Advance(EStep::GroupOrder, World);
        break;
    case EStep::GroupOrder:
        if (Group && std::ranges::any_of(Group->children, [&](const auto Child)
        {
            const auto* Regiment = battle::FindRegiment(World, {Child.value});
            return Regiment && HasPlayerOrder(*Regiment, StepStarted, battle::OrderType::Hold) &&
                Regiment->order->issuer == Group->commander;
        }))
            Advance(EStep::ArmyPossess, World);
        break;
    case EStep::ArmyPossess:
        if (Army)
            Advance(EStep::ArmyPanel, World);
        break;
    case EStep::ArmyPanel:
        if (Army && bOfficerCommandsOpen)
            Advance(EStep::ArmyOrder, World);
        break;
    case EStep::ArmyOrder:
        if (Army && std::ranges::any_of(World.command_state->communications.messages, [&](const auto& Message)
        {
            const auto* Task = std::get_if<CommandTask>(&Message.payload);
            return Task && Task->author == Army->commander && Task->created_tick >= StepStarted &&
                Task->priority == battle::PlayerOrderPriority && Task->action == TaskKind::HoldArea &&
                Message.status == DeliveryStatus::Delivered;
        }))
            Finish(World);
        break;
    default: break;
    }
}

FText FRegimentTutorialProgress::Status() const
{
    const auto* Info = RegimentMissionTutorial::Lessons().FindByPredicate(
        [this](const auto& Entry) { return Entry.Id == Lesson; });
    if (!Info)
        return FText::GetEmpty();
    if (!Result.IsEmpty())
        return FText::Format(LOCTEXT("LessonResult", "{0}\n{1}"), Info->Name, Result);
    const auto Index = int32(Step);
    const int32 First = Lesson == ERegimentTutorial::Infantry ? int32(EStep::InfantryMove) :
        Lesson == ERegimentTutorial::Cavalry ? int32(EStep::CavalryPossess) :
        Lesson == ERegimentTutorial::Artillery ? int32(EStep::ArtilleryShoot) : int32(EStep::GroupPossess);
    const int32 Count = Lesson == ERegimentTutorial::Artillery ? 1 :
        Lesson == ERegimentTutorial::Command ? 6 : Lesson == ERegimentTutorial::Cavalry ? 10 : 9;
    return FText::Format(LOCTEXT("LessonProgress", "{0} · {1}/{2}\n{3}"),
        Info->Name, Index - First + 1, Count, Instruction());
}

FText FRegimentTutorialProgress::Instruction() const
{
    using Action = ERegimentOfficerInputAction;
    const auto& Keys = RegimentOfficerSettings();
    switch (Step)
    {
    case EStep::InfantryMove:
        return FText::Format(LOCTEXT("Walk", "Вы командуете полком из 30 человек. Пройдите несколько шагов клавишами {0}, {1}, {2}, {3}. Мышь управляет взглядом."),
            TutorialKey(Keys.Key(Action::Forward)), TutorialKey(Keys.Key(Action::Left)),
            TutorialKey(Keys.Key(Action::Backward)), TutorialKey(Keys.Key(Action::Right)));
    case EStep::InfantryFollow:
        return LOCTEXT("Follow", "Нажмите F, чтобы полк пошёл за вами.");
    case EStep::InfantryColumn:
        return LOCTEXT("Column", "Нажмите 2, чтобы перестроить полк в колонну перед мостом.");
    case EStep::InfantryCross:
        return TargetZone && TargetZone->center == BridgeEntry
            ? LOCTEXT("BridgeApproach", "Встаньте на золотую метку перед мостом, чтобы вывести колонну точно к началу переправы.")
            : LOCTEXT("Cross", "Идите прямо по мосту к золотой метке на дальнем берегу. Подождите, пока полк переправится.");
    case EStep::InfantryLine:
        return LOCTEXT("Line", "Нажмите 1, чтобы развернуть полк в широкую огневую линию.");
    case EStep::InfantryShoot:
        return LOCTEXT("ShootTargets", "Нажмите 3, чтобы остановить линию и зарядить ружья. Полк откроет огонь по отмеченным мишеням.");
    case EStep::InfantryEnemyApproaches:
        return LOCTEXT("WaitEnemy", "Мишени отходят. К переправе движется полк противника.");
    case EStep::InfantryAttack:
        return LOCTEXT("AttackEnemy", "Наведите прицел на отмеченный полк противника и нажмите 4. Дождитесь доставки приказа.");
    case EStep::InfantryWithdraw:
        return LOCTEXT("WithdrawInfantry", "Нажмите 5 и укажите точку отхода позади строя. Дождитесь, пока полк сдвинется с позиции.");
    case EStep::CavalryPossess:
        return LOCTEXT("ChooseCavalry", "На тактической карте нажмите G, выберите офицера учебного кавалерийского полка и нажмите «Вселиться». Резервная пехота ждёт.");
    case EStep::CavalryFlank:
        return LOCTEXT("CavalryFlank", "Нажмите M и щёлкните по золотой метке сбоку от противника. Доведите до неё конный полк.");
    case EStep::CavalryFlankAttack:
        return LOCTEXT("CavalryFlankAttack", "С фланговой позиции наведите прицел на пехоту и нажмите 4. Дождитесь, пока кавалерия начнёт сближение.");
    case EStep::CavalryRear:
        return LOCTEXT("CavalryRear", "Первый заход остановлен перед строем. Нажмите M и обойдите противника до золотой метки в тылу.");
    case EStep::CavalryAttack:
        return LOCTEXT("CavalryCharge", "Наведите прицел на отмеченную пехоту и нажмите 4. Дождитесь, пока кавалерия вступит в бой.");
    case EStep::CavalryWithdraw:
        return LOCTEXT("CavalryWithdraw", "Нажмите 5 для отхода. Если появится указатель цели, щёлкните позади конного полка. Дождитесь выхода из боя.");
    case EStep::CavalryLoss:
        return LOCTEXT("CavalryLost", "По сценарию упражнения кавалерия выбыла. Полк покидает поле. Приготовьтесь взять резерв.");
    case EStep::ReservePossess:
        return LOCTEXT("ChooseReserve", "Кавалерия вышла из боя. Нажмите G на карте и возьмите офицера пехотного резерва.");
    case EStep::ReserveLine:
        return LOCTEXT("ReserveLine", "Нажмите 1, чтобы развернуть резерв в широкую огневую линию.");
    case EStep::ReserveShoot:
        return LOCTEXT("ReserveShoot", "Нажмите 3, чтобы удерживать рубеж, и дождитесь первого залпа резервной пехоты.");
    case EStep::ArtilleryShoot:
        return FText::Format(LOCTEXT("ArtilleryShoot", "Оставайтесь у своей пушки. Наведите прицел на отмеченный полк вдали и нажмите {0}. Зачёт после попадания ядра."),
            TutorialKey(Keys.Key(Action::Attack)));
    case EStep::GroupPossess:
        return LOCTEXT("ChooseGroup", "Нажмите G и возьмите офицера одной из синих групп. В списке видны его навыки и черты.");
    case EStep::GroupPanel:
        return LOCTEXT("OpenGroup", "Нажмите Tab, чтобы открыть панель группы. Посмотрите, какие полки вам подчиняются и какие приказы они выполняют.");
    case EStep::GroupOrder:
        return LOCTEXT("GroupOrder", "Нажмите J, чтобы приказать группе удерживать позицию. Дождитесь исполнения приказа.");
    case EStep::ArmyPossess:
        return LOCTEXT("ChooseArmy", "Нажмите G, чтобы выйти на карту. Снова нажмите G и возьмите командующего синей армией.");
    case EStep::ArmyPanel:
        return LOCTEXT("OpenArmy", "Нажмите Tab, чтобы открыть панель армии и посмотреть подчинённые группы, навыки и черты командующего.");
    case EStep::ArmyOrder:
        return LOCTEXT("ArmyOrder", "Нажмите K, чтобы приказать крылу армии удерживать позицию. Дождитесь, пока адъютант доставит приказ.");
    }
    return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
