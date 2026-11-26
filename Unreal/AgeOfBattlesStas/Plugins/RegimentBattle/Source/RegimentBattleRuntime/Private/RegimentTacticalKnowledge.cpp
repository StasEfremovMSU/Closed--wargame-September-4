#include "RegimentTacticalKnowledge.h"
#include "RegimentBattlePlayback.h"

THIRD_PARTY_INCLUDES_START
#include "battle/command/Services.h"
THIRD_PARTY_INCLUDES_END

namespace RegimentTacticalKnowledge
{
bool ShowsCommandNode(const battle::command::CommandNode& Node, int32 Team)
{
    return Node.team == Team;
}

std::optional<FEnemyContact> FindEnemyContact(const battle::command::CommandState& State,
    const battle::command::NodeRuntime& Runtime, battle::RegimentId Regiment, battle::Tick Now)
{
    using namespace battle::command;
    constexpr battle::Tick FreshnessTicks = 5 * FRegimentBattlePlayback::StepsPerSecond;
    const auto* Observer = FindNode(State.hierarchy, Runtime.node);
    if (!Observer)
        return {};
    std::optional<FEnemyContact> Best;
    const auto Consider = [&](const KnownContact& Contact, NodeId Reporter, bool bReceived)
    {
        if (!Contact.regiment || Contact.allegiance != Allegiance::Enemy ||
            (Regiment && Contact.regiment != Regiment) || Contact.observed_tick > Now ||
            Contact.confidence <= 0.f)
            return;
        const bool bStale = Now - Contact.observed_tick > FreshnessTicks ||
            (!bReceived && Contact.source != ObservationSource::Direct);
        const auto Kind = bReceived ? (bStale ? EContactKind::StaleReport : EContactKind::Report)
            : (bStale ? EContactKind::StaleObservation : EContactKind::Direct);
        if (!Best || Contact.observed_tick > Best->ObservedTick ||
            (Contact.observed_tick == Best->ObservedTick && Best->Kind == EContactKind::StaleObservation && bReceived))
            Best = FEnemyContact{Contact.regiment, Reporter, Contact.observed_tick, Kind};
    };
    for (const auto& Contact : Runtime.memory.remembered_contacts)
        Consider(Contact, {}, false);
    // NodeRuntime::reports contains messages that have actually been delivered.
    // An individually observed contact can be older than the report carrying it.
    for (const auto& Report : Runtime.reports)
    {
        const auto* Sender = FindNode(State.hierarchy, Report.sender);
        if (Report.recipient != Runtime.node || !Sender || Sender->parent != Runtime.node ||
            !ShowsCommandNode(*Sender, Observer->team) || Report.created_tick > Now ||
            Report.observed_tick > Report.created_tick)
            continue;
        for (const auto& Contact : Report.situation.contacts)
            if (Contact.observed_tick <= Report.observed_tick)
                Consider(Contact, Report.sender, true);
    }
    return Best;
}
}
