#pragma once

#include "CoreTypes.h"

THIRD_PARTY_INCLUDES_START
#include "battle/command/Runtime.h"
THIRD_PARTY_INCLUDES_END

namespace RegimentTacticalKnowledge
{
enum class EContactKind : uint8 { Direct, Report, StaleObservation, StaleReport };

struct FEnemyContact
{
    battle::RegimentId Regiment;
    battle::command::NodeId Reporter;
    battle::Tick ObservedTick;
    EContactKind Kind;
};

bool ShowsCommandNode(const battle::command::CommandNode& Node, int32 Team);
std::optional<FEnemyContact> FindEnemyContact(const battle::command::CommandState& State,
    const battle::command::NodeRuntime& Runtime, battle::RegimentId Regiment, battle::Tick Now);
}
