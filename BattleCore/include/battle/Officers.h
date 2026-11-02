#pragma once
#include "Navigation.h"
#include "Spatial.h"
#include "World.h"
namespace battle {
struct Contact {
    RegimentId id{};
    Vec3 position{};
    float strength{}, distance{};
};
struct Perception {
    RegimentId regiment{};
    Pose pose{};
    Tick tick{};
    float strength{}, preferred_range{1};
    bool panic{};
    std::vector<Contact> enemies;
};
struct OfficerTraits {
    float charisma{}, tactic_mind{}, lead_skill{}, lead_dist{};
};
struct Decision {
    RegimentId regiment{}, target{};
    OrderType action{OrderType::Hold};
    Vec3 point{};
    float score{};
    std::vector<std::pair<OrderType, float>> scores;
    std::string reason;
    PathResult path{};
    bool keep_route{};
};
OfficerId Commander(const World &, RegimentId);
Perception Observe(const World &, OfficerId, const SpatialIndex &);
Decision Evaluate(const Perception &, const OfficerTraits &, const Order *);
void ApplyDecision(World &, OfficerId, const Decision &);
void UpdateOfficers(World &, const NavigationService &);
} // namespace battle
