#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Internationalization/Text.h"
#include <span>

THIRD_PARTY_INCLUDES_START
#include "battle/Navigation.h"
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

enum class ERegimentPlayerOrder : uint8;
struct FRegimentOrderPresentation;

namespace RegimentOfficerOrders
{
class FPlayerOrderJournal
{
public:
    void Observe(const battle::World&);
    const battle::Order* Latest(battle::RegimentId Regiment, battle::OfficerId Issuer) const;

private:
    TMap<uint64, TMap<uint64, battle::Order>> Orders;
};

struct FManeuverConflict
{
    battle::RegimentId Other;
    battle::Vec3 Position;
    float Seconds;
};
struct FManeuverRoute
{
    battle::RegimentId Id;
    battle::Pose Start;
    battle::Pose Goal;
    std::optional<uint32_t> Columns;
    std::vector<battle::Pose> Path;
    bool bReachable = false;
    std::optional<FManeuverConflict> TrafficConflict{};
};

std::vector<FManeuverRoute> PreviewManeuver(const battle::World&, const battle::NavigationService&,
    battle::RegimentId, battle::Vec3 Begin, std::optional<battle::Vec3> End, bool bGroup, bool bPreserveFacing = false);
void CheckManeuverConflicts(const battle::World&, const battle::NavigationService&, std::span<FManeuverRoute>,
    battle::OrderType, battle::MovementPace, std::span<const battle::CommandEnvelope>);
FText QueueManeuver(const battle::World&, std::span<const FManeuverRoute>, battle::OrderType,
    battle::MovementPace, std::vector<battle::CommandEnvelope>&, uint64& Sequence, const battle::NavigationService&);
std::optional<FText> QueueStaffHold(const battle::World&, std::span<const battle::RegimentId> Recipients,
    std::vector<battle::CommandEnvelope>&, uint64& Sequence);
FRegimentOrderPresentation DescribeOrder(const battle::World& World, const battle::RegimentState& Regiment,
    std::span<const battle::CommandEnvelope> Pending, bool bPaused, const FPlayerOrderJournal* Journal = nullptr);
battle::UnitId FindOfficer(const battle::World& World, uint64 NodeId);
battle::UnitId PendingOfficer(const battle::World&, std::span<const battle::CommandEnvelope>);
std::vector<battle::RegimentId> HoldRecipients(const battle::World&, battle::RegimentId Selected, bool bWing);
std::vector<battle::RegimentId> Regiments(const battle::World& World, battle::UnitId Officer);
FText QueueOrder(const battle::World& World, const battle::NavigationService& Navigation,
    battle::RegimentId Regiment, ERegimentPlayerOrder Order, std::optional<battle::Vec3> Cursor,
    std::vector<battle::CommandEnvelope>& Pending, uint64& NextSequence);
battle::UnitId PersonalTarget(const battle::World& World, float AimYaw);
}
