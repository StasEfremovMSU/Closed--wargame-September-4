#include "battle/Presentation.h"
namespace battle {
PresentationFrame BuildPresentation(const Snapshot &snapshot) {
    PresentationFrame frame;
    frame.tick = snapshot.state.tick;
    frame.units.reserve(snapshot.state.units.size());
    for (const auto &u : snapshot.state.units)
        frame.units.push_back({u.id, u.regiment, u.pose, u.alive, u.control});
    frame.events = snapshot.state.events;
    return frame;
}
} // namespace battle
