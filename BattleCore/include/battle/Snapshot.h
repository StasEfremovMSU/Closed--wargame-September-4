#pragma once
#include "World.h"
namespace battle {
struct Snapshot {
    uint32_t schema_version{2};
    World state;
};
Snapshot Capture(const World &);
Result<World> Restore(const Snapshot &);
uint64_t StateHash(const World &);
} // namespace battle
