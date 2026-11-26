#include "RegimentBattleNotices.h"

#include <algorithm>

namespace
{
constexpr uint64 StrikeCooldownTicks = 100;
constexpr int32 MaxNotices = 24;

FText IndependentAttackerName(const battle::World& World, const battle::UnitState& Unit)
{
    if (World.command_state)
    {
        const auto Officer = std::ranges::find(World.officers, Unit.id, &battle::OfficerSpec::unit);
        if (Officer != World.officers.end())
        {
            const auto& Nodes = World.command_state->hierarchy.nodes;
            const auto Node = std::ranges::find(Nodes, Officer->id, &battle::command::CommandNode::commander);
            if (Node != Nodes.end() && !Node->name.empty())
            {
                const FText Name = FText::AsCultureInvariant(UTF8_TO_TCHAR(Node->name.c_str()));
                return Node->level == battle::command::Level::Army ? Name
                    : FText::Format(NSLOCTEXT("RegimentBattle", "NoticeGroupOfficer", "Командир · {0}"), Name);
            }
        }
    }
    if (!Unit.name.empty())
        return FText::AsCultureInvariant(UTF8_TO_TCHAR(Unit.name.c_str()));
    return FText::Format(NSLOCTEXT("RegimentBattle", "NoticeIndependentOfficer", "Офицер №{0}"), Unit.id.value);
}
}

FText RegimentName(const battle::RegimentState& Regiment)
{
    return Regiment.name.empty()
        ? FText::Format(NSLOCTEXT("RegimentBattle", "NumberedRegiment", "Полк {0}"), Regiment.id.value)
        : FText::AsCultureInvariant(UTF8_TO_TCHAR(Regiment.name.c_str()));
}

void FRegimentBattleNotices::ObserveHit(const battle::World& World, const battle::BattleEvent& Hit,
    const battle::UnitState& Target, const FVector& Location)
{
    const bool bCharge = Hit.kind == battle::EventKind::CavalryImpact;
    if (Hit.projectile || Hit.amount <= 0.f || !Hit.regiment ||
        (!bCharge && (Hit.kind != battle::EventKind::Hit || Hit.aspect == battle::AttackAspect::Front ||
                     Hit.reason.starts_with("cavalry impact"))))
        return;

    const auto* Source = battle::FindUnit(World, Hit.source);
    const auto* Defender = battle::FindRegiment(World, Target.regiment);
    if (!Source || !Defender)
        return;
    const auto* Attacker = battle::FindRegiment(World, Source->regiment);
    const int32 AttackerTeam = Attacker ? Attacker->team : Source->independent_team;
    if (AttackerTeam == Defender->team)
        return;

    const FStrikeKey Pair{Attacker ? Attacker->id.value : Source->id.value, Defender->id.value,
        bCharge ? battle::AttackAspect::Front : Hit.aspect, !Attacker, bCharge};
    if (const auto* Last = LastStrikes.Find(Pair); Last && Hit.tick - *Last < StrikeCooldownTicks)
        return;
    LastStrikes.Add(Pair, Hit.tick);

    const auto Kind = bCharge ? ERegimentBattleNotice::Charge
        : Hit.aspect == battle::AttackAspect::Rear ? ERegimentBattleNotice::Rear : ERegimentBattleNotice::Flank;
    const FText AttackerName = Attacker ? RegimentName(*Attacker) : IndependentAttackerName(World, *Source);
    Add({Kind, Location, Hit.tick,
        {AttackerName, AttackerTeam}, {RegimentName(*Defender), Defender->team}, 0, 0, Hit.aspect});
}

void FRegimentBattleNotices::ObserveStaff(const battle::World& World, const battle::BattleEvent& Event,
    const battle::UnitState& Fallen, const FVector& Location)
{
    const auto Officer = std::ranges::find(World.officers, Fallen.id, &battle::OfficerSpec::unit);
    if (Officer == World.officers.end())
        return;
    const auto* Regiment = battle::FindRegiment(World, Fallen.regiment);
    FRegimentNoticeParticipant Command{Regiment ? RegimentName(*Regiment) : IndependentAttackerName(World, Fallen),
        battle::UnitTeam(World, Fallen)};
    if (Event.kind == battle::EventKind::Died)
    {
        if (Officer->role == 3)
            Add({ERegimentBattleNotice::StandardLost, Location, Event.tick, Command, {}});
        else if (Officer->role == 1 || Officer->role == 2)
        {
            OfficerLosses.Add(Fallen.id.value, {Event.tick, Command});
            Add({ERegimentBattleNotice::OfficerLost, Location, Event.tick, Command, {}, Fallen.id.value});
        }
        return;
    }
    const bool bHandover = Event.kind == battle::EventKind::OfficerSucceeded;
    if (!bHandover && Event.kind != battle::EventKind::CommandVacant)
        return;
    FCommandLoss Loss{Event.tick, Command};
    OfficerLosses.RemoveAndCopyValue(Fallen.id.value, Loss);
    Entries.RemoveAll([&](const auto& Entry)
    {
        return Entry.Kind == ERegimentBattleNotice::OfficerLost && Entry.FallenUnit == Fallen.id.value;
    });
    FRegimentNoticeParticipant NewOfficer;
    if (bHandover)
    {
        const auto* Replacement = battle::FindUnit(World, Event.source);
        if (!Replacement)
            return;
        NewOfficer.Name = FText::Format(NSLOCTEXT("RegimentBattle", "NoticeNewOfficerId", "Офицер №{0}"),
            FText::AsCultureInvariant(FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Replacement->id.value))));
        NewOfficer.Team = battle::UnitTeam(World, *Replacement);
    }
    Add({bHandover ? ERegimentBattleNotice::Handover : ERegimentBattleNotice::Vacant, Location,
        Event.tick, Loss.Command, NewOfficer, Fallen.id.value, Event.tick - FMath::Min(uint64(Event.tick), Loss.Tick)});
}

void FRegimentBattleNotices::ObserveRegiment(
    const battle::RegimentState& Regiment, uint64 Tick, const FVector& Location)
{
    if (Regiment.units.empty())
        return;

    auto& State = States.FindOrAdd(Regiment.id.value);
    const bool bMoved = State.LastPosition &&
        FVector::DistSquared2D(Location, State.LastPosition.GetValue()) > FMath::Square(.25);
    const bool bRetreating = !Regiment.panic &&
        (Regiment.ai_action == battle::OrderType::Withdraw || Regiment.ai_action == battle::OrderType::FallBack) &&
        (State.bRetreating || bMoved);
    const bool bRallying = Regiment.order && Regiment.order->type == battle::OrderType::Rally &&
        Regiment.order->status == battle::OrderStatus::Active;
    const auto AddForRegiment = [&](ERegimentBattleNotice Kind)
    {
        Add({Kind, Location, Tick, {RegimentName(Regiment), Regiment.team}, {}});
    };
    if (Regiment.panic && !State.bPanicking)
        AddForRegiment(ERegimentBattleNotice::Rout);
    else if (!Regiment.panic && State.bPanicking)
        AddForRegiment(ERegimentBattleNotice::Rallied);
    else if (bRetreating && !State.bRetreating)
        AddForRegiment(ERegimentBattleNotice::Retreat);
    if (bRallying && !State.bRallying)
        AddForRegiment(ERegimentBattleNotice::Rally);

    State.bRetreating = bRetreating;
    State.bPanicking = Regiment.panic;
    State.bRallying = bRallying;
    State.LastPosition = Location;
}

void FRegimentBattleNotices::Expire(uint64 Tick)
{
    Entries.RemoveAll([Tick](const auto& Notice)
    {
        return Tick - Notice.Tick >= LifetimeTicks;
    });
    for (auto It = LastStrikes.CreateIterator(); It; ++It)
        if (Tick - It.Value() >= StrikeCooldownTicks)
            It.RemoveCurrent();
}

void FRegimentBattleNotices::Add(FRegimentBattleNotice Notice)
{
    if (Entries.Num() == MaxNotices)
        Entries.RemoveAt(0, EAllowShrinking::No);
    Entries.Add(MoveTemp(Notice));
}
