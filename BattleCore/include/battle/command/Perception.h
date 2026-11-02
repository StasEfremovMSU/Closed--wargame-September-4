#pragma once
#include "Hierarchy.h"

namespace battle::command {
enum class ObservationSource : uint8_t { Direct, Report, Memory };
enum class Allegiance : uint8_t { Unknown, Friendly, Enemy };
enum class RelativeSector : uint8_t { Front, Left, Right, Rear };

// Estimates available to an officer, never a live reference to an enemy World record.
struct KnownContact {
    NodeId command{};
    RegimentId regiment{};
    Allegiance allegiance{Allegiance::Unknown};
    std::optional<int32_t> team;
    Pose estimated_pose{};
    std::optional<float> estimated_strength;
    Tick observed_tick{};
    float confidence{}; // Proposed [0,1]; zero is no confidence.
    ObservationSource source{ObservationSource::Direct};
};

struct OwnForceSummary {
    uint32_t living{}, initial{};
    Pose pose{};
    bool panic{};
    // Missing means unknown/unmodelled, not exhausted or completely supplied.
    std::optional<float> morale, cohesion, fatigue, ammunition_fraction;
};

struct ThreatEstimate {
    RelativeSector sector{RelativeSector::Front};
    std::optional<float> strength;
    float confidence{};
    bool under_ranged_fire{}, cavalry_approaching{};
};

struct TerrainAssessment {
    Vec3 position{};
    std::optional<float> cover, passability, height_advantage;
};

struct LocalSituation {
    NodeId observer{};
    Tick tick{};
    OwnForceSummary own;
    std::vector<KnownContact> contacts;
    std::vector<ThreatEstimate> threats;
    std::vector<TerrainAssessment> terrain;
};
} // namespace battle::command
