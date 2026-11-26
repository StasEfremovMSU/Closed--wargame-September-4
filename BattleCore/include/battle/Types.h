#pragma once
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace battle {
using Tick = uint64_t;
template <class Tag> struct Id {
    uint64_t value{};
    auto operator<=>(const Id &) const = default;
    explicit operator bool() const {
        return value != 0;
    }
};
using UnitId = Id<struct UnitTag>;
using RegimentId = Id<struct RegimentTag>;
using OfficerId = Id<struct OfficerTag>;
using OrderId = Id<struct OrderTag>;
using EventId = Id<struct EventTag>;
enum class Status : uint8_t { Ok, InvalidInput, NotFound, Conflict, NoPath };
struct Diagnostic {
    std::string source;
    uint32_t line{};
    std::string message;
};
template <class T> struct Result {
    Status status{Status::Ok};
    T value{};
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const {
        return status == Status::Ok;
    }
};
struct Vec3 {
    float x{}, y{}, z{};
    auto operator<=>(const Vec3 &) const = default;
};
struct Pose {
    Vec3 position{};
    float yaw{};
    auto operator<=>(const Pose &) const = default;
};
inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float Length(Vec3 a) {
    return std::sqrt(Dot(a, a));
}
inline float Distance(Vec3 a, Vec3 b) {
    return Length(a - b);
}
inline bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool Finite(Pose p) {
    return Finite(p.position) && std::isfinite(p.yaw);
}
inline Vec3 Forward(float yaw) {
    return {std::cos(yaw), std::sin(yaw), 0};
}
inline Vec3 Right(float yaw) {
    return {-std::sin(yaw), std::cos(yaw), 0};
}
inline constexpr float Pi = 3.14159265358979323846f;
enum class ControlMode : uint8_t { FormationControlled, PlayerControlled, Returning, Dead };
enum class EventKind : uint8_t {
    Spawned,
    AttackLaunched,
    Hit,
    Miss,
    Died,
    OrderChanged,
    ControlChanged,
    CommandRejected,
    DecisionTrace,
    CavalryImpact,
    FormationReformed,
    OfficerSucceeded,
    SmokeCreated,
    CommandVacant,
    Parried
};
enum class AttackAspect : uint8_t { Front, Flank, Rear };
struct BattleEvent {
    EventId id{};
    Tick tick{};
    EventKind kind{};
    UnitId source{}, target{};
    RegimentId regiment{};
    float amount{};
    std::string reason;
    Tick due_tick{};
    bool hit{}, projectile{};
    EventId cause{};
    AttackAspect aspect{AttackAspect::Front};
    bool high_arc{};
};
enum class BattleOutcome : uint8_t { Running, Victory, Draw };
namespace command {
class ITacticalEvaluator;
}
struct SimulationOptions {
    uint32_t tick_ms{50};
    uint32_t worker_count{1};
    const command::ITacticalEvaluator *group_policy{}; // Borrowed for this Step only.
    bool group_order_commitment{};
};
struct TickResult {
    Tick tick{};
    std::vector<BattleEvent> events;
};
} // namespace battle
