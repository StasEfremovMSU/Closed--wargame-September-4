#pragma once
#include "World.h"
#include "Navigation.h"
namespace battle {Result<TickResult> Step(World&,std::span<const CommandEnvelope>,const SimulationOptions&,const NavigationService&);}
