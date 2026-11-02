#pragma once
#include "Navigation.h"
#include "World.h"
namespace battle {
struct StepProfile {
    double orders_ms{}, movement_ms{}, targeting_ms{}, commit_ms{};
};
Result<TickResult> Step(World &, std::span<const CommandEnvelope>, const SimulationOptions &,
                        const NavigationService &, StepProfile *profile = nullptr);
} // namespace battle
