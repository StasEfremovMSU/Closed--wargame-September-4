#include "ArmyStaff.h"
#include "CommandBrainCommon.h"
#include "LocalMelee.h"
#include "battle/Navigation.h"
#include "battle/command/Services.h"

namespace battle::command {
namespace {
constexpr float StaffRadius = .22f;
constexpr float DeliveryReach = 2.f;
constexpr float StationReach = 1.5f;

const OfficerSpec *Officer(const World &world, OfficerId id) {
    const auto found = std::ranges::lower_bound(world.officers, id, {}, &OfficerSpec::id);
    return found != world.officers.end() && found->id == id ? &*found : nullptr;
}

const UnitState *Commander(const World &world, NodeId id) {
    const auto *node = FindNode(world.command_state->hierarchy, id);
    const auto *officer = node ? Officer(world, node->commander) : nullptr;
    const auto *unit = officer ? FindUnit(world, officer->unit) : nullptr;
    return unit && unit->alive ? unit : nullptr;
}

const OfficerSpec *StaffOfficer(const World &world, const SupportAssignment &staff) {
    const auto *officer = Officer(world, staff.officer);
    return officer && officer->role == ArmyStaffOfficerRole ? officer : nullptr;
}

bool Clear(const World &world, Vec3 from, Vec3 to) {
    return UnitTerrainLimit(world, from, to, StaffRadius) >= 1.f &&
           (!world.scenario.hill ||
            HillSegmentPassable(*world.scenario.hill, from, to, UnitTroop::Mounted));
}

Vec3 BoundStaffPosition(const World &world, Vec3 goal) {
    if (const auto &bounds = world.scenario.field_bounds) {
        const float width = std::max(0.f, bounds->half_width_m - StaffRadius);
        const float height = std::max(0.f, bounds->half_height_m - StaffRadius);
        goal.x = std::clamp(goal.x, -width, width);
        goal.y = std::clamp(goal.y, -height, height);
    }
    return goal;
}

Vec3 StaffHome(const World &world, Pose commander, SupportDuty duty, size_t index) {
    const Vec3 offset = ArmyStaffStation(commander, duty, index).position - commander.position;
    for (float fraction : {1.f, .75f, .5f, .25f}) {
        const auto home = BoundStaffPosition(world, commander.position + offset * fraction);
        if (Clear(world, home, home))
            return home;
    }
    return BoundStaffPosition(world, commander.position);
}

bool Ride(World &world, UnitState &rider, Vec3 goal, const NavigationService &navigation) {
    goal = BoundStaffPosition(world, goal);
    const Vec3 from = rider.pose.position;
    if (Clear(world, from, goal)) {
        rider.slot.position = goal;
    } else if (Distance(from, rider.slot.position) <= .04f || !Clear(world, from, rider.slot.position)) {
        PathRequest request{.start = rider.pose, .goal = goal,
                            .footprint = {StaffRadius * 2, StaffRadius * 2}};
        if (world.scenario.hill)
            request.segment_clear = [&](Pose a, Pose b) {
                return HillSegmentPassable(*world.scenario.hill, a.position, b.position, rider.troop);
            };
        const auto path = navigation.FindPath(request);
        if (path.status != Status::Ok)
            return false;
        rider.slot.position = from;
        for (const auto &waypoint : path.waypoints) {
            if (!Finite(waypoint) || !Clear(world, from, waypoint.position))
                break;
            rider.slot.position = waypoint.position;
        }
        if (Distance(from, rider.slot.position) <= .04f)
            return false;
    }

    const auto delta = rider.slot.position - from;
    const float distance = Length(delta);
    if (distance <= .04f)
        return true;
    constexpr float dt = .05f;
    Vec3 travel = delta * (std::min(distance, rider.speed * dt) / distance);
    if (world.scenario.hill)
        travel = travel * HillUphillSpeedMultiplier(*world.scenario.hill, from, from + travel,
                                                     rider.troop, rider.speed);
    rider.pose.position = from + travel;
    const auto turn = std::remainder(std::atan2(delta.y, delta.x) - rider.pose.yaw, 2 * Pi);
    rider.pose.yaw += std::clamp(turn, -4.f * dt, 4.f * dt);
    return true;
}

struct StaffRider {
    UnitState *unit{};
    NodeId army{};
    SupportDuty duty{};
    std::optional<Vec3> home;
    bool carrying{};
};

int Priority(const MessageEnvelope &message) {
    const auto *task = std::get_if<CommandTask>(&message.payload);
    return task ? task->priority : 0;
}
} // namespace

bool UsesStaffCourier(const World &world, NodeId sender, NodeId recipient) {
    if (!world.command_state || !sender)
        return false;
    const auto &hierarchy = world.command_state->hierarchy;
    const auto *source = FindNode(hierarchy, sender);
    const auto *target = FindNode(hierarchy, recipient);
    return source && target && source->level == Level::Army && target->level == Level::Group &&
           target->parent == sender &&
           std::ranges::any_of(hierarchy.support, [&](const auto &staff) {
               return staff.command == sender && staff.duty == SupportDuty::Adjutant &&
                      StaffOfficer(world, staff);
           });
}

bool HasLivingStaffAdjutant(const World &world, NodeId army) {
    return std::ranges::any_of(world.command_state->hierarchy.support, [&](const auto &staff) {
        const auto *record = staff.command == army && staff.duty == SupportDuty::Adjutant
                                 ? StaffOfficer(world, staff) : nullptr;
        const auto *unit = record ? FindUnit(world, record->unit) : nullptr;
        return unit && unit->alive;
    });
}

bool IsArmyStaffTarget(const World &world, const UnitState &unit) {
    if (!world.command_state || unit.regiment)
        return false;
    return std::ranges::any_of(world.command_state->hierarchy.support, [&](const auto &staff) {
        if (staff.duty != SupportDuty::Adjutant && staff.duty != SupportDuty::Bodyguard)
            return false;
        const auto *officer = StaffOfficer(world, staff);
        return officer && (officer->unit == unit.id || Commander(world, staff.command) == &unit);
    });
}

Pose ArmyStaffStation(Pose commander, SupportDuty duty, size_t index) {
    const float side = index % 2 == 0 ? -1.f : 1.f;
    const float row = float(index / 2);
    const float ahead = duty == SupportDuty::Bodyguard ? 1.6f - row * 2.6f : -2.5f - row * 1.8f;
    const float aside = duty == SupportDuty::Bodyguard ? side * 1.3f : side * 1.2f;
    commander.position = commander.position + Forward(commander.yaw) * ahead + Right(commander.yaw) * aside;
    return commander;
}

void AdvanceArmyStaff(World &world, const NavigationService &navigation) {
    auto &state = *world.command_state;
    std::vector<StaffRider> riders;
    for (const auto &assignment : state.hierarchy.support) {
        const auto *record = (assignment.duty == SupportDuty::Adjutant || assignment.duty == SupportDuty::Bodyguard)
                                 ? StaffOfficer(world, assignment) : nullptr;
        auto *unit = record ? FindUnit(world, record->unit) : nullptr;
        if (!unit || !unit->alive || unit->control == ControlMode::PlayerControlled)
            continue;
        const auto *general = Commander(world, assignment.command);
        std::optional<Vec3> home;
        if (general) {
            const auto index = std::ranges::count_if(state.hierarchy.support, [&](const auto &other) {
                return other.command == assignment.command && other.duty == assignment.duty &&
                       other.officer < assignment.officer && StaffOfficer(world, other);
            });
            home = StaffHome(world, general->pose, assignment.duty, size_t(index));
        }
        riders.push_back({unit, assignment.command, assignment.duty, home});
    }

    std::vector<MessageEnvelope *> waiting;
    const auto advance = [&](MessageEnvelope &message, StaffRider &rider) {
        rider.carrying = true;
        const auto *recipient = Commander(world, message.recipient);
        if (!recipient) {
            message.status = DeliveryStatus::RecipientUnavailable;
            message.delivery_tick = world.tick;
            return;
        }
        const auto goal = recipient->pose.position;
        const auto arrived = [&] {
            const auto position = rider.unit->pose.position;
            return Distance(position, goal) <= DeliveryReach && Clear(world, position, goal);
        };
        if (!arrived() && !Ride(world, *rider.unit, goal, navigation)) {
            message.status = DeliveryStatus::Lost;
            message.delivery_tick = world.tick;
            return;
        }
        message.delivery_tick = arrived() ? std::max(world.tick, detail::Later(message.created_tick, 1))
                                          : detail::Later(world.tick, 1);
    };
    for (auto &message : state.communications.messages) {
        if (message.channel != DeliveryChannel::Courier ||
            (message.status != DeliveryStatus::Queued && message.status != DeliveryStatus::InTransit) ||
            !UsesStaffCourier(world, message.sender, message.recipient))
            continue;
        if (message.expiry_tick && *message.expiry_tick <= world.tick) {
            message.status = DeliveryStatus::Expired;
            message.delivery_tick = world.tick;
        } else if (message.status == DeliveryStatus::Queued && !message.courier) {
            if (!Commander(world, message.recipient)) {
                message.status = DeliveryStatus::RecipientUnavailable;
                message.delivery_tick = world.tick;
            } else
                waiting.push_back(&message);
        } else {
            const auto rider = std::ranges::find_if(riders, [&](const auto &candidate) {
                return candidate.unit->id == message.courier && candidate.duty == SupportDuty::Adjutant;
            });
            if (rider == riders.end() || rider->carrying) {
                message.status = DeliveryStatus::Lost;
                message.delivery_tick = world.tick;
            } else
                advance(message, *rider);
        }
    }
    std::ranges::sort(waiting, [](const auto *a, const auto *b) {
        return Priority(*a) != Priority(*b) ? Priority(*a) > Priority(*b) : a->id < b->id;
    });
    for (auto *message : waiting) {
        bool available_army = false;
        for (auto &rider : riders) {
            if (rider.army != message->sender || rider.duty != SupportDuty::Adjutant || !rider.home)
                continue;
            available_army = true;
            if (rider.carrying || Distance(rider.unit->pose.position, *rider.home) > StationReach)
                continue;
            message->courier = rider.unit->id;
            message->status = DeliveryStatus::InTransit;
            rider.unit->slot = rider.unit->pose;
            advance(*message, rider);
            break;
        }
        if (!available_army) {
            message->status = DeliveryStatus::Lost;
            message->delivery_tick = world.tick;
        }
    }
    for (const auto &rider : riders)
        if (!rider.carrying && rider.home)
            Ride(world, *rider.unit, *rider.home, navigation);
}

const UnitState *BodyguardForProjectile(const World &world, const UnitState &attacker,
                                       const UnitState &commander) {
    if (commander.regiment || !world.command_state ||
        UnitTeam(world, attacker) == UnitTeam(world, commander))
        return nullptr;
    const auto &hierarchy = world.command_state->hierarchy;
    const auto origin = commander.pose.position;
    const auto threat = attacker.pose.position - origin;
    const UnitState *guard = nullptr;
    float nearest = 3.4f;
    for (const auto &staff : hierarchy.support) {
        const auto *record = staff.duty == SupportDuty::Bodyguard ? StaffOfficer(world, staff) : nullptr;
        const auto *escort = record ? FindUnit(world, record->unit) : nullptr;
        if (!escort || !escort->alive)
            continue;
        const auto *leader = Commander(world, staff.command);
        const auto offset = escort->pose.position - origin;
        const auto distance = Length(offset);
        if (leader == &commander && distance < nearest && Dot(threat, offset) > 0.f &&
            Clear(world, escort->pose.position, origin)) {
            guard = escort;
            nearest = distance;
        }
    }
    return guard;
}
} // namespace battle::command
