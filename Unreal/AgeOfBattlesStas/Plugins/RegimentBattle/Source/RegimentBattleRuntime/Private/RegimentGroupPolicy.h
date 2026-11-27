#pragma once

THIRD_PARTY_INCLUDES_START
#include "battle/command/GroupPolicy.h"
THIRD_PARTY_INCLUDES_END

namespace RegimentGroupPolicy
{
inline battle::SimulationOptions SimulationOptions(const battle::command::GroupPolicy* Policy)
{
    battle::SimulationOptions Options;
    Options.group_policy = Policy;
    Options.group_order_commitment = Policy &&
        Policy->FeatureSchema() == battle::command::GroupPolicyCommitmentFeatureSchema;
    return Options;
}
}
