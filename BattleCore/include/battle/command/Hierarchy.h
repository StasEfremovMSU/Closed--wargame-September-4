#pragma once
#include "battle/Types.h"
#include <string_view>

namespace battle::command {
using NodeId = Id<struct CommandNodeTag>;
using TaskId = Id<struct CommandTaskTag>;
using MessageId = Id<struct CommandMessageTag>;

// A command position, not a military rank or the legacy OfficerSpec.role.
enum class Level : uint8_t { Army, Part, Group, Regiment };
enum class SectorRole : uint8_t {
    MainBody,
    Vanguard,
    Center,
    LeftFlank,
    RightFlank,
    Reserve,
    Detached
};
enum class SupportDuty : uint8_t { Deputy, Adjutant, StandardBearer, Musician };

enum class StubStatus : uint8_t { NotImplemented, Ready, InvalidInput };
// No implicit success conversion. Inactive code must never manufacture a payload.
template <class T> struct StubResult {
    StubStatus status{StubStatus::NotImplemented};
    std::optional<T> value;
    std::string_view reason; // Static implementation-owned diagnostic text.
};

struct CommandNode {
    bool operator==(const CommandNode &) const = default;
    NodeId id{}, parent{}; // Zero parent denotes an army root.
    Level level{Level::Regiment};
    int32_t team{};
    std::string name;
    SectorRole sector_role{SectorRole::MainBody};
    OfficerId commander{};
    std::vector<OfficerId> succession; // Ordered candidates; life/authority not checked yet.
    std::vector<NodeId> children;
    RegimentId regiment{}; // Only a regiment leaf binds to a physical regiment.
};

// Proposed normalized traits [0,1]; no conversion from legacy values exists yet.
struct OfficerProfile {
    bool operator==(const OfficerProfile &) const = default;
    OfficerId officer{};
    float tactical_skill{}, initiative{}, discipline{}, aggression{}, caution{}, charisma{};
    float leadership{};
    Tick reaction_delay_ticks{}, reevaluation_ticks{}, minimum_commitment_ticks{};
    std::optional<uint32_t> command_capacity;
    std::optional<float> command_radius_m;
};

struct SupportAssignment {
    bool operator==(const SupportAssignment &) const = default;
    OfficerId officer{};
    NodeId command{};
    SupportDuty duty{SupportDuty::Adjutant};
};

struct CommandHierarchy {
    bool operator==(const CommandHierarchy &) const = default;
    std::vector<CommandNode> nodes;
    std::vector<OfficerProfile> officers;
    std::vector<SupportAssignment> support;
};

// External scaffold state: not owned by World or captured by Snapshot.
struct BrainSchedule {
    bool operator==(const BrainSchedule &) const = default;
    NodeId node{};
    Tick next_think_tick{}, interval_ticks{}, commitment_until{};
};
} // namespace battle::command
