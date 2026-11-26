#include "RegimentFieldFeedback.h"

#include "RegimentBattlePlayback.h"
#include "RegimentOfficerOrders.h"
#include "battle/Control.h"

#define LOCTEXT_NAMESPACE "RegimentFieldFeedback"

void FRegimentFieldFeedback::ObserveAttack(const battle::BattleEvent& Event)
{
    if (Event.kind == battle::EventKind::AttackLaunched && Event.projectile && Event.regiment)
        LastShot.Add(Event.regiment.value, Event.tick);
}

FText FRegimentFieldFeedback::FlagState(const battle::RegimentState& Regiment, uint64 Tick) const
{
    if (Regiment.panic) return LOCTEXT("Rout", "Бегство");
    if (Regiment.braced) return LOCTEXT("Brace", "Упор");
    switch (Regiment.cavalry_phase)
    {
    case battle::CavalryPhase::Approach: return LOCTEXT("Approach", "Разбег");
    case battle::CavalryPhase::Impact: return LOCTEXT("Impact", "Натиск");
    case battle::CavalryPhase::Reform: return LOCTEXT("Reform", "Сбор");
    default: break;
    }
    if (Regiment.ai_action == battle::OrderType::FallBack) return LOCTEXT("FallBack", "Отход");
    if (Regiment.disordered || Regiment.melee_engaged) return {};
    if (const auto* Last = LastShot.Find(Regiment.id.value);
        Last && Tick >= *Last && Tick - *Last < FRegimentBattlePlayback::StepsPerSecond)
        return LOCTEXT("Firing", "Стреляет");
    const bool bOrderedLine = Regiment.order && Regiment.order->status == battle::OrderStatus::Active &&
        Regiment.order->type == battle::OrderType::HoldLine && battle::IsHolding(Regiment.ai_action);
    if ((bOrderedLine || Regiment.ai_action == battle::OrderType::HoldLine) &&
        Regiment.waypoint >= Regiment.route.size())
        return LOCTEXT("HoldingLine", "Держит линию");
    return {};
}

battle::UnitId FRegimentFieldFeedback::CardOfficer(const battle::World& World, battle::RegimentId Regiment, uint64 Node)
{
    if (!World.command_state || !Regiment || !Node || World.outcome != battle::BattleOutcome::Running ||
        (World.controlled && !battle::CanCommandRegiment(World, World.controlled, Regiment)))
        return {};
    const auto& Nodes = World.command_state->hierarchy.nodes;
    const auto Found = std::ranges::find(Nodes, battle::command::NodeId{Node}, &battle::command::CommandNode::id);
    if (Found == Nodes.end() || Found->regiment != Regiment)
        return {};
    const auto Candidate = RegimentOfficerOrders::FindOfficer(World, Node);
    return Candidate != World.controlled ? Candidate : battle::UnitId{};
}

bool FRegimentFieldFeedback::QueueCardOfficer(const battle::World& World, battle::RegimentId Regiment, uint64 Node,
    std::vector<battle::CommandEnvelope>& Pending, uint64& Sequence)
{
    const auto Candidate = CardOfficer(World, Regiment, Node);
    if (!Candidate) return false;
    std::erase_if(Pending, [](const auto& Entry)
    {
        return Entry.command.kind == battle::CommandKind::AcquireControl ||
            Entry.command.kind == battle::CommandKind::ReleaseControl;
    });
    battle::Command Request;
    Request.kind = battle::CommandKind::AcquireControl;
    Request.unit = Candidate;
    Pending.push_back({World.tick, Sequence++, Request});
    return true;
}

#undef LOCTEXT_NAMESPACE
