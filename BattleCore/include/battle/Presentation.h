#pragma once
#include "Snapshot.h"
namespace battle {
// Owned boundary data. Consumers can retain this across world mutations.
struct UnitPresentation {
    UnitId id{};
    RegimentId regiment{};
    Pose pose{};
    bool alive{};
    ControlMode control{};
};
struct PresentationFrame {
    Tick tick{};
    std::vector<UnitPresentation> units;
    std::vector<BattleEvent> events;
};
PresentationFrame BuildPresentation(const Snapshot &);
} // namespace battle
