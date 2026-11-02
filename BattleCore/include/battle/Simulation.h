#pragma once
#include "Navigation.h"
#include "World.h"
namespace battle {
Result<TickResult> Step(World &, std::span<const CommandEnvelope>, const SimulationOptions &,
                        const NavigationService &);
}
