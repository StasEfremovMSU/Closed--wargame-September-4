#include "RegimentOfficerOrders.h"

#include "RegimentBattleField.h"
#include "RegimentBattleNotices.h"
#include "RegimentBattlePlayback.h"
#include <map>
#include <tuple>

THIRD_PARTY_INCLUDES_START
#include "battle/Combat.h"
#include "battle/Control.h"
#include "battle/Formation.h"
#include "battle/Officers.h"
#include "battle/Terrain.h"
#include "battle/command/Services.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "RegimentOfficerOrders"

namespace RegimentOfficerOrders
{
namespace
{
constexpr float DefaultSightRadius = 60.f;
constexpr float CursorRegimentReach = 2.5f;

const battle::OfficerSpec* OfficerRecord(const battle::World& World, battle::UnitId Unit)
{
    const auto Record = std::ranges::find(World.officers, Unit, &battle::OfficerSpec::unit);
    return Record == World.officers.end() ? nullptr : &*Record;
}

const battle::UnitState* LivingBlueUnit(const battle::World& World, battle::UnitId Id)
{
    const auto* Unit = battle::FindUnit(World, Id);
    return Unit && Unit->alive && battle::UnitTeam(World, *Unit) == 0 ? Unit : nullptr;
}

bool HasLivingUnit(const battle::World& World, const battle::RegimentState& Regiment)
{
    return std::ranges::any_of(Regiment.units, [&](battle::UnitId Id)
    {
        const auto* Unit = battle::FindUnit(World, Id);
        return Unit && Unit->alive;
    });
}

battle::UnitId OfficerForNode(const battle::World& World, const battle::command::CommandNode& Node)
{
    if (Node.team != 0 || !Node.commander)
        return {};

    const auto Record = std::ranges::find(World.officers, Node.commander, &battle::OfficerSpec::id);
    if (Record == World.officers.end() || Regiments(World, Record->unit).empty())
        return {};
    return Record->unit;
}

const battle::Command* MostRecentCommand(const std::vector<battle::CommandEnvelope>& Pending, battle::Tick Tick,
    battle::RegimentId Regiment, battle::CommandKind Kind)
{
    const auto Packet = std::find_if(Pending.rbegin(), Pending.rend(), [&](const auto& Entry)
    {
        return Entry.tick == Tick && Entry.command.kind == Kind && Entry.command.regiment == Regiment;
    });
    return Packet == Pending.rend() ? nullptr : &Packet->command;
}

bool WillFollow(const battle::World& World, const battle::RegimentState& Regiment, battle::UnitId Officer,
    const std::vector<battle::CommandEnvelope>& Pending)
{
    // Enabling follow on another regiment also stops this one.
    const auto Packet = std::find_if(Pending.rbegin(), Pending.rend(), [&](const auto& Entry)
    {
        return Entry.tick == World.tick && Entry.command.kind == battle::CommandKind::SetRegimentFollowing &&
            Entry.command.unit == Officer && (Entry.command.regiment == Regiment.id || Entry.command.follow_enabled);
    });
    if (Packet != Pending.rend())
        return Packet->command.regiment == Regiment.id && Packet->command.follow_enabled;
    return Regiment.following_officer == Officer;
}

float DistanceToFormation(battle::Vec3 Point, const battle::RegimentState& Regiment)
{
    const auto Size = battle::Footprint(Regiment);
    const auto Offset = Point - Regiment.pose.position;
    const float Ahead = std::abs(battle::Dot(Offset, battle::Forward(Regiment.pose.yaw)));
    const float Aside = std::abs(battle::Dot(Offset, battle::Right(Regiment.pose.yaw)));
    return std::hypot(std::max(0.f, Ahead - Size.depth_m * .5f), std::max(0.f, Aside - Size.width_m * .5f));
}

const battle::RegimentState* VisibleEnemy(const battle::World& World, const battle::UnitState& Officer,
    battle::OfficerId OfficerId, std::optional<battle::Vec3> Cursor, bool bFleeingOnly = false)
{
    float SightRadius = DefaultSightRadius;
    if (World.command_state)
        if (const auto* Profile = battle::command::FindProfile(World.command_state->hierarchy, OfficerId))
            SightRadius = Profile->command_radius_m.value_or(DefaultSightRadius);

    const battle::RegimentState* Nearest = nullptr;
    const battle::RegimentState* Pointed = nullptr;
    float NearestDistance = SightRadius;
    float PointedDistance = CursorRegimentReach;
    for (const auto& Candidate : World.regiments)
    {
        const float Distance = battle::Distance(Officer.pose.position, Candidate.pose.position);
        if (Candidate.team == 0 || (bFleeingOnly && !Candidate.panic) || Distance > SightRadius || !HasLivingUnit(World, Candidate) ||
            battle::SmokeObscures(World, Officer.pose.position, Candidate.pose.position) ||
            (World.scenario.hill && !battle::HillHasLineOfSight(*World.scenario.hill, Officer.pose.position, Candidate.pose.position)) ||
            !battle::command::HasLineOfSight(Officer.pose.position, Candidate.pose.position, World.scenario.obstacles))
            continue;
        if (!Nearest || Distance < NearestDistance)
        {
            Nearest = &Candidate;
            NearestDistance = Distance;
        }
        if (Cursor)
        {
            const float FromCursor = DistanceToFormation(*Cursor, Candidate);
            if (FromCursor <= CursorRegimentReach &&
                (!Pointed || FromCursor < PointedDistance ||
                    (FromCursor == PointedDistance && Distance < battle::Distance(Officer.pose.position, Pointed->pose.position))))
            {
                Pointed = &Candidate;
                PointedDistance = FromCursor;
            }
        }
    }
    return Pointed ? Pointed : Nearest;
}

bool CanReformHere(const battle::World& World, const battle::RegimentState& Regiment, battle::FormationSpec Formation)
{
    battle::RegimentState Candidate;
    Candidate.id = Regiment.id;
    Candidate.pose = Regiment.pose;
    Candidate.formation = Formation;
    for (auto Id : Regiment.units)
        if (const auto* Unit = battle::FindUnit(World, Id); Unit && Unit->alive)
            Candidate.units.push_back(Id);

    // Keep the exact current heading. Pathfinding may round it while looking for a route.
    const auto Size = battle::Footprint(Candidate);
    for (const auto& Box : World.scenario.obstacles)
    {
        if (!battle::TerrainBlocksMovement(Box))
            continue;
        const battle::Pose Obstacle{{(Box.min_x + Box.max_x) * .5f, (Box.min_y + Box.max_y) * .5f, 0.f}, 0.f};
        if (battle::RectanglesOverlap(Candidate.pose, Size, Obstacle,
                {Box.max_y - Box.min_y, Box.max_x - Box.min_x}))
            return false;
    }
    return battle::FormationRouteSegmentClear(World, Candidate, Candidate.pose);
}

bool IsDirectPlayerOrder(const battle::Order& Order)
{
    // Direct orders have a delivery delay. The immediate holds issued by a delivered group task
    // are its effects on the regiments and should leave the group's delivery status visible.
    return Order.issuer && Order.priority == battle::PlayerOrderPriority && Order.delivery_tick > Order.created_tick;
}

FText FormationOrderBlocker(const battle::World& World, const battle::RegimentState& Regiment)
{
    if (Regiment.panic)
        return LOCTEXT("FormationOrderPanic", "Полк паникует. Сначала соберите его.");
    if (battle::IsDeploying(World, Regiment))
        return LOCTEXT("FormationOrderDeploying", "Полк ещё развёртывается.");
    if (Regiment.melee_engaged)
        return LOCTEXT("FormationOrderMelee", "Полк в рукопашной.");
    return {};
}

FText OrderAction(battle::OrderType Type)
{
    using battle::OrderType;
    switch (Type)
    {
    case OrderType::Hold: return LOCTEXT("OrderHoldAction", "Удерживать позицию");
    case OrderType::MoveTo: return LOCTEXT("OrderMoveAction", "Манёвр");
    case OrderType::Attack: return LOCTEXT("OrderAttackAction", "Атака");
    case OrderType::Withdraw:
    case OrderType::FallBack: return LOCTEXT("OrderWithdrawAction", "Отход");
    case OrderType::Rally: return LOCTEXT("OrderRallyAction", "Сбор");
    case OrderType::HoldLine: return LOCTEXT("OrderHoldLineAction", "Держать строй");
    case OrderType::Brace: return LOCTEXT("OrderBraceAction", "Упор");
    case OrderType::Pursue: return LOCTEXT("OrderPursueAction", "Погоня");
    }
    return {};
}

FText PendingRegimentAction(const battle::Command& Command, battle::UnitId Officer,
    battle::OfficerId Issuer, battle::RegimentId Regiment)
{
    if (Command.unit != Officer)
        return {};
    if (Command.kind == battle::CommandKind::IssueOrder)
        return IsDirectPlayerOrder(Command.order) && Command.order.recipient == Regiment && Command.order.issuer == Issuer
            ? OrderAction(Command.order.type) : FText{};
    if (Command.regiment != Regiment)
        return {};
    switch (Command.kind)
    {
    case battle::CommandKind::ReformRegiment: return LOCTEXT("OrderReformAction", "Перестроение");
    case battle::CommandKind::SetSkirmishers:
        return Command.skirmishers_enabled ? LOCTEXT("OrderSkirmisherAction", "Выслать застрельщиков")
                                          : LOCTEXT("OrderRecallAction", "Отозвать застрельщиков");
    case battle::CommandKind::SetRegimentFollowing:
        return Command.follow_enabled ? LOCTEXT("OrderFollowAction", "За мной")
                                      : LOCTEXT("OrderStopFollowAction", "Прекратить следование");
    default: return {};
    }
}

struct FCourierAddress
{
    battle::command::NodeId Army, Group;
    battle::OfficerId General;
};

std::optional<FCourierAddress> CourierAddress(const battle::World& World, battle::RegimentId Regiment)
{
    const auto* General = OfficerRecord(World, World.controlled);
    if (!World.command_state || !General || !Regiment || !LivingBlueUnit(World, General->unit))
        return {};
    const auto& Hierarchy = World.command_state->hierarchy;
    const auto Leaf = std::ranges::find(Hierarchy.nodes, Regiment, &battle::command::CommandNode::regiment);
    const auto Army = std::ranges::find(Hierarchy.nodes, General->id, &battle::command::CommandNode::commander);
    if (Leaf == Hierarchy.nodes.end() || Army == Hierarchy.nodes.end() ||
        !battle::command::UsesStaffCourier(World, Army->id, Leaf->parent))
        return {};
    return FCourierAddress{Army->id, Leaf->parent, General->id};
}

bool IsPlayerDispatch(const battle::command::CommandTask& Task, const FCourierAddress& Address)
{
    return Task.author == Address.General && Task.issuer == Address.Army && Task.recipient == Address.Group &&
        Task.priority == battle::PlayerOrderPriority && Task.action == battle::command::TaskKind::HoldArea;
}

void SetCourierRecipient(FRegimentOrderPresentation& Result, const battle::World& World, const FCourierAddress& Address)
{
    const auto* Group = battle::command::FindNode(World.command_state->hierarchy, Address.Group);
    Result.bCourier = true;
    Result.Recipient = Group && !Group->name.empty() ? FText::AsCultureInvariant(UTF8_TO_TCHAR(Group->name.c_str()))
        : FText::Format(LOCTEXT("UnnamedCourierGroup", "Группа {0}"), Address.Group.value);
    Result.Action = OrderAction(battle::OrderType::Hold);
}

const battle::command::MessageEnvelope* LatestDispatch(const battle::World& World, const FCourierAddress& Address)
{
    const battle::command::MessageEnvelope* Latest = nullptr;
    for (const auto& Message : World.command_state->communications.messages)
    {
        const auto* Task = std::get_if<battle::command::CommandTask>(&Message.payload);
        if (Message.channel == battle::command::DeliveryChannel::Courier && Task && IsPlayerDispatch(*Task, Address) &&
            (!Latest || Message.id.value > Latest->id.value))
            Latest = &Message;
    }
    return Latest;
}

void DescribeDispatchPhase(FRegimentOrderPresentation& Result, battle::command::DeliveryStatus Status, bool bPaused)
{
    using battle::command::DeliveryStatus;
    using Delivery = ERegimentOrderDelivery;
    switch (Status)
    {
    case DeliveryStatus::Queued: Result.Delivery = Delivery::AwaitingCourier; break;
    case DeliveryStatus::InTransit: Result.Delivery = bPaused ? Delivery::Paused : Delivery::InTransit; break;
    case DeliveryStatus::Delivered: Result.Delivery = Delivery::Delivered; break;
    case DeliveryStatus::Expired:
        Result.Delivery = Delivery::Unapplied;
        Result.OrderDetail = LOCTEXT("CourierExpired", "Срок доставки истёк");
        break;
    case DeliveryStatus::Lost:
        Result.Delivery = Delivery::Unapplied;
        Result.OrderDetail = LOCTEXT("CourierLost", "Адъютант не доставил приказ");
        break;
    case DeliveryStatus::RecipientUnavailable:
        Result.Delivery = Delivery::Unapplied;
        Result.OrderDetail = LOCTEXT("CourierNoRecipient", "Командир группы недоступен");
        break;
    }
}

void QueueCourierTask(const battle::World& World, const FCourierAddress& Address,
    battle::command::TaskKind Action, battle::Vec3 Objective,
    std::vector<battle::CommandEnvelope>& Pending, uint64& Sequence)
{
    battle::Command Command;
    Command.kind = battle::CommandKind::IssueCommandTask;
    Command.unit = World.controlled;
    auto& Task = Command.command_task;
    Task.issuer = Address.Army;
    Task.recipient = Address.Group;
    Task.author = Address.General;
    Task.action = Action;
    Task.priority = battle::PlayerOrderPriority;
    Task.intent.objective_position = Objective;
    Pending.push_back({World.tick, Sequence++, Command});
}
}

std::optional<FText> QueueStaffHold(const battle::World& World, std::span<const battle::RegimentId> Recipients,
    std::vector<battle::CommandEnvelope>& Pending, uint64& Sequence)
{
    struct FGroup
    {
        FCourierAddress Address;
        battle::Vec3 Total;
        int32 Count = 0;
    };
    std::map<battle::command::NodeId, FGroup> Groups;
    for (const auto Id : Recipients)
    {
        const auto Address = CourierAddress(World, Id);
        const auto* Regiment = battle::FindRegiment(World, Id);
        if (!Address || !Regiment)
            return {};
        auto& Group = Groups.try_emplace(Address->Group, FGroup{*Address, {}, 0}).first->second;
        Group.Total = Group.Total + Regiment->pose.position;
        ++Group.Count;
    }
    if (Groups.empty())
        return {};
    for (const auto& [Id, Group] : Groups)
        QueueCourierTask(World, Group.Address, battle::command::TaskKind::HoldArea,
            Group.Total * (1.f / float(Group.Count)), Pending, Sequence);
    return FText::Format(LOCTEXT("HoldByCourier", "Удерживать позицию · приказ подготовлен для групп: {0}"),
        int32(Groups.size()));
}

void FPlayerOrderJournal::Observe(const battle::World& World)
{
    const auto Record = [this](const battle::Order& Order)
    {
        if (!Order.id || !IsDirectPlayerOrder(Order))
            return;
        auto& Latest = Orders.FindOrAdd(Order.issuer.value).FindOrAdd(Order.recipient.value);
        if (Latest.id.value <= Order.id.value)
            Latest = Order;
    };
    for (const auto& Order : World.pending_orders)
        Record(Order);
    for (const auto& Regiment : World.regiments)
        if (Regiment.team == 0 && Regiment.order)
            Record(*Regiment.order);
}

const battle::Order* FPlayerOrderJournal::Latest(battle::RegimentId Regiment, battle::OfficerId Issuer) const
{
    const auto* ByRecipient = Orders.Find(Issuer.value);
    return ByRecipient ? ByRecipient->Find(Regiment.value) : nullptr;
}

FRegimentOrderPresentation DescribeOrder(const battle::World& World, const battle::RegimentState& Regiment,
    std::span<const battle::CommandEnvelope> Pending, bool bPaused, const FPlayerOrderJournal* Journal)
{
    FRegimentOrderPresentation Result;
    const auto* Record = OfficerRecord(World, World.controlled);
    const bool bDeploying = battle::IsDeploying(World, Regiment);
    const auto Address = Regiment.team == 0 ? CourierAddress(World, Regiment.id) : std::nullopt;
    using Delivery = ERegimentOrderDelivery;
    if (Record)
    {
        for (auto It = Pending.rbegin(); It != Pending.rend(); ++It)
        {
            const auto& Command = It->command;
            if (Address && Command.kind == battle::CommandKind::IssueCommandTask && IsPlayerDispatch(Command.command_task, *Address))
                SetCourierRecipient(Result, World, *Address);
            else
                Result.Action = PendingRegimentAction(Command, World.controlled, Record->id, Regiment.id);
            if (!Result.Action.IsEmpty())
            {
                Result.Delivery = bPaused ? Delivery::Paused : Delivery::Queued;
                break;
            }
        }
    }

    const battle::Order* Latest = nullptr;
    if (Record && Result.Delivery == Delivery::None)
    {
        const auto Newest = [&](const battle::Order& Order)
        {
            if (IsDirectPlayerOrder(Order) && Order.recipient == Regiment.id && Order.issuer == Record->id &&
                (!Latest || Order.id.value > Latest->id.value))
                Latest = &Order;
        };
        if (Regiment.order)
            Newest(*Regiment.order);
        for (const auto& Order : World.pending_orders)
            Newest(Order);
    }
    const auto* Previous = Journal && Record && Result.Delivery == Delivery::None
        ? Journal->Latest(Regiment.id, Record->id) : nullptr;
    const bool bDisappeared = Previous && (!Latest || Previous->id.value > Latest->id.value);
    if (bDisappeared)
        Latest = Previous;
    const auto* Dispatch = Address && Result.Delivery == Delivery::None ? LatestDispatch(World, *Address) : nullptr;
    if (Dispatch && (!Latest || Dispatch->created_tick > Latest->created_tick))
    {
        SetCourierRecipient(Result, World, *Address);
        DescribeDispatchPhase(Result, Dispatch->status, bPaused);
    }
    else if (Latest)
    {
        using battle::OrderStatus;
        Result.OrderId = Latest->id.value;
        Result.Action = OrderAction(Latest->type);
        if (!bDisappeared && Latest->status == OrderStatus::Pending)
            Result.Delivery = bPaused ? Delivery::Paused : Delivery::InTransit;
        else if (Latest->status == OrderStatus::Completed)
            Result.Delivery = Delivery::Complete;
        else if (!bDisappeared && Latest->status == OrderStatus::Active)
        {
            Result.Delivery = Delivery::Executing;
            if (Regiment.panic && Latest->type != battle::OrderType::Rally && Latest->type != battle::OrderType::Withdraw)
            {
                Result.Delivery = Delivery::Blocked;
                Result.OrderDetail = LOCTEXT("OrderPausedByPanic", "Полк паникует");
            }
            else if (bDeploying && !Regiment.panic)
            {
                Result.Delivery = Delivery::Blocked;
                Result.OrderDetail = LOCTEXT("OrderPausedByDeployment", "Полк развёртывается");
            }
            else if (!Regiment.melee_engaged && Regiment.march_lane && Regiment.march_lane->order == Latest->id &&
                Regiment.march_lane->detour.empty() && Regiment.march_lane->stalled_seconds >= battle::MarchLane::StallSeconds)
            {
                Result.Delivery = Delivery::Blocked;
                Result.OrderDetail = LOCTEXT("OrderPausedByTraffic", "Полк ждёт, когда освободится проход");
            }
        }
        else
        {
            Result.Delivery = Delivery::Unapplied;
            if (Latest->expiry_tick && Latest->expiry_tick <= World.tick &&
                (Latest->status == OrderStatus::Cancelled || (bDisappeared && Latest->status == OrderStatus::Pending &&
                    Latest->expiry_tick <= Latest->delivery_tick)))
                Result.OrderDetail = LOCTEXT("PlayerOrderExpired", "Истёк срок действия.");
            else if (Latest->status == OrderStatus::Failed)
                Result.OrderDetail = LOCTEXT("PlayerOrderFailed", "Полк не смог выполнить приказ.");
            else if (Latest->status == OrderStatus::Cancelled)
                Result.OrderDetail = LOCTEXT("PlayerOrderCancelled", "Приказ отменён.");
            else
                Result.OrderDetail = LOCTEXT("PlayerOrderUnconfirmed", "Подтверждение от полка не получено.");
        }
    }
    if (!Result.bCourier && Result.Delivery != Delivery::None)
        Result.Recipient = RegimentName(Regiment);

    const bool bFollowing = World.controlled && Regiment.following_officer == World.controlled;
    if (Regiment.panic)
    {
        Result.Follow = bFollowing ? ERegimentFollowStatus::Panic : ERegimentFollowStatus::Off;
        Result.Activity = bFollowing ? LOCTEXT("FollowPanic", "Следование приостановлено · паника")
                                    : LOCTEXT("ActivityPanic", "Паника · отход");
    }
    else if (bDeploying)
    {
        Result.Follow = bFollowing ? ERegimentFollowStatus::Deploying : ERegimentFollowStatus::Off;
        Result.Activity = bFollowing ? LOCTEXT("FollowDeployment", "Следование приостановлено · развёртывание")
                                    : LOCTEXT("ActivityDeploy", "Развёртывается");
    }
    else if (Regiment.melee_engaged)
    {
        Result.Follow = bFollowing ? ERegimentFollowStatus::Melee : ERegimentFollowStatus::Off;
        Result.Activity = bFollowing ? LOCTEXT("FollowMelee", "Следование приостановлено · рукопашная")
                                    : LOCTEXT("ActivityMelee", "В рукопашной");
    }
    else if (bFollowing)
    {
        Result.Follow = ERegimentFollowStatus::Active;
        Result.Activity = LOCTEXT("ActivityFollow", "Следует за вами");
    }
    else
    {
        switch (Regiment.ai_action)
        {
        case battle::OrderType::Attack:
            if (battle::UsesLinearInfantry(World, Regiment))
                Result.Activity = !battle::IsLinearFiringLine(Regiment)
                    ? LOCTEXT("ActivityBayonetColumn", "Наступает колонной")
                    : Regiment.waypoint < Regiment.route.size()
                        ? LOCTEXT("ActivityFiringLineAdvances", "Линия выходит на огневую позицию")
                        : LOCTEXT("ActivityFiringLineReady", "Линия на огневой позиции");
            else
                Result.Activity = LOCTEXT("ActivityAttack", "Атакует");
            break;
        case battle::OrderType::MoveTo: Result.Activity = LOCTEXT("ActivityMove", "Движется к позиции"); break;
        case battle::OrderType::Withdraw: Result.Activity = LOCTEXT("ActivityWithdraw", "Отходит"); break;
        case battle::OrderType::Rally: Result.Activity = LOCTEXT("ActivityRally", "Собирается"); break;
        case battle::OrderType::Hold: Result.Activity = LOCTEXT("ActivityHold", "Удерживает позицию"); break;
        case battle::OrderType::HoldLine: Result.Activity = LOCTEXT("ActivityLine", "Держит линию"); break;
        case battle::OrderType::FallBack: Result.Activity = LOCTEXT("ActivityFallback", "Отходит"); break;
        case battle::OrderType::Brace: Result.Activity = LOCTEXT("ActivityBrace", "Готов против конницы"); break;
        case battle::OrderType::Pursue: Result.Activity = LOCTEXT("ActivityPursuit", "Преследует"); break;
        }
    }
    return Result;
}

battle::UnitId PendingOfficer(const battle::World& World, std::span<const battle::CommandEnvelope> Pending)
{
    if (World.outcome != battle::BattleOutcome::Running)
        return {};
    for (auto It = Pending.rbegin(); It != Pending.rend(); ++It)
    {
        const auto& Command = It->command;
        if (Command.kind == battle::CommandKind::ReleaseControl)
            return {};
        if (Command.kind == battle::CommandKind::AcquireControl)
        {
            const auto* Candidate = battle::FindUnit(World, Command.unit);
            return Candidate && Candidate->alive && Candidate->id != World.controlled ? Candidate->id : battle::UnitId{};
        }
    }
    return {};
}

battle::UnitId FindOfficer(const battle::World& World, uint64 NodeId)
{
    if (World.command_state)
    {
        const auto& Hierarchy = World.command_state->hierarchy;
        if (NodeId)
        {
            const auto* Node = battle::command::FindNode(Hierarchy, {NodeId});
            return Node ? OfficerForNode(World, *Node) : battle::UnitId{};
        }
        for (auto Level : {battle::command::Level::Regiment, battle::command::Level::Group,
                battle::command::Level::Part, battle::command::Level::Army})
            for (const auto& Node : Hierarchy.nodes)
                if (Node.level == Level)
                    if (const auto Officer = OfficerForNode(World, Node))
                        return Officer;
        return {};
    }
    if (!NodeId)
        for (const auto& Record : World.officers)
            if (!Regiments(World, Record.unit).empty())
                return Record.unit;
    return {};
}

std::vector<battle::RegimentId> Regiments(const battle::World& World, battle::UnitId Officer)
{
    std::vector<battle::RegimentId> Result;
    if (!LivingBlueUnit(World, Officer))
        return Result;
    for (const auto& Regiment : World.regiments)
        if (Regiment.team == 0 && HasLivingUnit(World, Regiment) &&
            battle::CanCommandRegiment(World, Officer, Regiment.id))
            Result.push_back(Regiment.id);
    return Result;
}

std::vector<battle::RegimentId> HoldRecipients(const battle::World& World, battle::RegimentId Selected, bool bWing)
{
    if (!World.command_state)
        return {};
    const auto* Officer = OfficerRecord(World, World.controlled);
    if (!Officer)
        return {};
    const auto& Nodes = World.command_state->hierarchy.nodes;
    const auto Leader = std::ranges::find(Nodes, Officer->id, &battle::command::CommandNode::commander);
    using battle::command::Level;
    if (Leader == Nodes.end() || Leader->level == Level::Regiment || (bWing && Leader->level != Level::Army))
        return {};
    auto Owned = Regiments(World, World.controlled);
    if (!bWing)
        return Owned;
    if (std::ranges::find(Owned, Selected) == Owned.end())
        return {};
    // The branch directly beneath the army may contain intermediate command levels.
    const auto Branch = [&](battle::RegimentId Id)
    {
        const auto Leaf = std::ranges::find(Nodes, Id, &battle::command::CommandNode::regiment);
        const auto* Node = Leaf != Nodes.end() ? &*Leaf : nullptr;
        for (size_t Depth = 0; Node && Depth < Nodes.size(); ++Depth)
        {
            if (Node->parent == Leader->id)
                return Node->id;
            Node = battle::command::FindNode(World.command_state->hierarchy, Node->parent);
        }
        return battle::command::NodeId{};
    };
    const auto Wing = Branch(Selected);
    std::erase_if(Owned, [&](battle::RegimentId Id) { return !Wing || Branch(Id) != Wing; });
    return Owned;
}

FText QueueOrder(const battle::World& World, const battle::NavigationService&,
    battle::RegimentId RegimentId, ERegimentPlayerOrder Order, std::optional<battle::Vec3> Cursor,
    std::vector<battle::CommandEnvelope>& Pending, uint64& NextSequence)
{
    if (World.outcome != battle::BattleOutcome::Running)
        return LOCTEXT("BattleFinished", "Бой завершён.");
    const auto* Officer = LivingBlueUnit(World, World.controlled);
    const auto* Record = Officer ? OfficerRecord(World, Officer->id) : nullptr;
    if (!Record)
        return LOCTEXT("NeedControlledOfficer", "Возьмите управление офицером.");
    const auto* Regiment = battle::FindRegiment(World, RegimentId);
    if (!Regiment || !HasLivingUnit(World, *Regiment))
        return LOCTEXT("NeedLivingRegiment", "Выберите полк, в котором остались бойцы.");
    if (Regiment->team != 0 || !battle::CanCommandRegiment(World, Officer->id, RegimentId))
        return LOCTEXT("RegimentOutsideCommand", "Этот полк не подчиняется выбранному офицеру.");

    if (Cursor && !battle::Finite(*Cursor))
        Cursor.reset();

    const auto Queue = [&](battle::Command Command)
    {
        Command.unit = Officer->id;
        Command.regiment = RegimentId;
        Pending.push_back({World.tick, NextSequence++, Command});
    };
    const auto SetFollow = [&](bool bEnable)
    {
        battle::Command Command;
        Command.kind = battle::CommandKind::SetRegimentFollowing;
        Command.follow_enabled = bEnable;
        Queue(Command);
    };
    const bool bFollowing = WillFollow(World, *Regiment, Officer->id, Pending);
    const bool bDeploying = battle::IsDeploying(World, *Regiment);

    switch (Order)
    {
    case ERegimentPlayerOrder::Line:
    case ERegimentPlayerOrder::Column:
    {
        if (Regiment->panic)
            return LOCTEXT("ReformDuringPanic", "Сначала соберите паникующий полк.");
        if (Regiment->melee_engaged)
            return LOCTEXT("ReformDuringMelee", "Полк в рукопашной. Перестроение невозможно.");
        if (bDeploying)
            return LOCTEXT("ReformDuringDeployment", "Дождитесь окончания развёртывания.");

        const auto Authored = std::ranges::find(World.scenario.regiments, RegimentId, &battle::RegimentSpec::id);
        if (Authored == World.scenario.regiments.end())
            return LOCTEXT("NoAuthoredFormation", "Для этого полка не задан строй.");
        const bool bLine = Order == ERegimentPlayerOrder::Line;
        auto Formation = bLine ? Authored->formation : battle::TravelColumnFormation(Authored->formation);
        const auto Living = std::ranges::count_if(Regiment->units, [&](battle::UnitId Id)
        {
            const auto* Unit = battle::FindUnit(World, Id);
            return Unit && Unit->alive;
        });
        Formation.columns = std::min(Formation.columns, uint32(Living));
        if (battle::UsesLinearInfantry(World, *Regiment))
            Formation = battle::LinearInfantryFormation(*Regiment, bLine);
        const auto* PendingReform = MostRecentCommand(Pending, World.tick, RegimentId, battle::CommandKind::ReformRegiment);
        if (Formation == (PendingReform ? PendingReform->formation : Regiment->formation))
            return bLine ? LOCTEXT("LineAlreadySet", "Полк уже в линии.")
                         : LOCTEXT("ColumnAlreadySet", "Полк уже в колонне.");
        if (!CanReformHere(World, *Regiment, Formation))
            return LOCTEXT("FormationObstructed", "Здесь недостаточно места для выбранного строя.");

        battle::Command Command;
        Command.kind = battle::CommandKind::ReformRegiment;
        Command.formation = Formation;
        Queue(Command);
        return bLine ? LOCTEXT("LineOrdered", "Приказано развернуться в линию.")
                     : LOCTEXT("ColumnOrdered", "Приказано построиться в колонну.");
    }
    case ERegimentPlayerOrder::Follow:
        if (!bFollowing && (Regiment->panic || Regiment->melee_engaged || bDeploying))
            return FormationOrderBlocker(World, *Regiment);
        SetFollow(!bFollowing);
        return bFollowing ? LOCTEXT("FollowStopped", "Следование отменено.")
                          : LOCTEXT("FollowStarted", "Приказано следовать за офицером.");

    case ERegimentPlayerOrder::Skirmishers:
    {
        if (!World.scenario.rules.local_melee || Regiment->skirmisher_count == 0)
            return LOCTEXT("NoSkirmishers", "В этом полку нет отряда застрельщиков.");
        const auto* PendingScreen = MostRecentCommand(Pending, World.tick, RegimentId, battle::CommandKind::SetSkirmishers);
        const bool bEnabled = PendingScreen ? PendingScreen->skirmishers_enabled : Regiment->skirmishers_enabled;
        if (!bEnabled)
        {
            if (Regiment->panic || bDeploying || Regiment->melee_engaged || !battle::Commander(World, RegimentId))
                return LOCTEXT("SkirmishersUnavailable", "Полк сейчас не может выдвинуть застрельщиков.");
            if (!std::ranges::any_of(Regiment->units, [&](battle::UnitId Id)
                {
                    const auto* Unit = battle::FindUnit(World, Id);
                    return Unit && Unit->alive && Unit->archer && Unit->control != battle::ControlMode::PlayerControlled;
                }))
                return LOCTEXT("NoLivingArchers", "В полку не осталось стрелков для отряда застрельщиков.");
        }
        battle::Command Command;
        Command.kind = battle::CommandKind::SetSkirmishers;
        Command.skirmishers_enabled = !bEnabled;
        Queue(Command);
        return bEnabled ? LOCTEXT("SkirmishersRecalled", "Приказано вернуть застрельщиков в строй.")
                        : LOCTEXT("SkirmishersSent", "Приказано выдвинуть застрельщиков.");
    }
    case ERegimentPlayerOrder::Hold:
    case ERegimentPlayerOrder::HoldLine:
    case ERegimentPlayerOrder::Brace:
    case ERegimentPlayerOrder::Attack:
    case ERegimentPlayerOrder::Pursue:
    case ERegimentPlayerOrder::Withdraw:
        break;
    case ERegimentPlayerOrder::NextRegiment:
    case ERegimentPlayerOrder::Release:
    case ERegimentPlayerOrder::Maneuver:
    case ERegimentPlayerOrder::GroupManeuver:
    case ERegimentPlayerOrder::Pace:
    case ERegimentPlayerOrder::Cover:
    case ERegimentPlayerOrder::FireSector:
        return FText::GetEmpty();
    }

    if (Regiment->panic && Order != ERegimentPlayerOrder::Withdraw)
        return LOCTEXT("OrderDuringPanic", "Полк паникует. Отдайте приказ на сбор.");

    battle::Command Command;
    Command.kind = battle::CommandKind::IssueOrder;
    auto& Direct = Command.order;
    Direct.issuer = Record->id;
    Direct.recipient = RegimentId;
    Direct.priority = battle::PlayerOrderPriority;
    Direct.created_tick = World.tick;
    Direct.delivery_tick = World.tick + FRegimentBattlePlayback::StepsPerSecond;
    if (Order == ERegimentPlayerOrder::Attack || Order == ERegimentPlayerOrder::Pursue)
    {
        const auto* Enemy = VisibleEnemy(World, *Officer, Record->id, Cursor, Order == ERegimentPlayerOrder::Pursue);
        if (!Enemy)
            return Order == ERegimentPlayerOrder::Pursue
                ? LOCTEXT("NoVisibleFleeingEnemy", "Офицер не видит отступающего вражеского полка.")
                : LOCTEXT("NoVisibleEnemy", "Офицер не видит ни одного вражеского полка.");
        Direct.type = Order == ERegimentPlayerOrder::Pursue ? battle::OrderType::Pursue : battle::OrderType::Attack;
        Direct.target_regiment = Enemy->id;
        Direct.point = Enemy->pose.position;
    }
    else if (Order == ERegimentPlayerOrder::Withdraw)
    {
        Direct.type = Regiment->panic ? battle::OrderType::Rally : battle::OrderType::Withdraw;
        Direct.point = Regiment->rally_point;
    }
    else
    {
        Direct.type = Order == ERegimentPlayerOrder::Brace ? battle::OrderType::Brace :
            Order == ERegimentPlayerOrder::HoldLine ? battle::OrderType::HoldLine : battle::OrderType::Hold;
        Direct.point = Regiment->pose.position;
    }
    if (bFollowing)
        SetFollow(false);
    Queue(Command);
    return {};
}

std::vector<FManeuverRoute> PreviewManeuver(const battle::World& World, const battle::NavigationService& Navigation,
    battle::RegimentId Reference, battle::Vec3 Begin, std::optional<battle::Vec3> End, bool bGroup, bool bPreserveFacing)
{
    const auto* Anchor = battle::FindRegiment(World, Reference);
    const auto Owned = Regiments(World, World.controlled);
    if (!Anchor || std::ranges::find(Owned, Reference) == Owned.end() ||
        !battle::Finite(Begin) || (End && !battle::Finite(*End)))
        return {};

    battle::Pose Destination{Begin, Anchor->pose.yaw};
    float Width = 0.f;
    if (End && !bGroup)
    {
        const auto Edge = *End - Begin;
        Width = std::hypot(Edge.x, Edge.y);
        if (Width >= 1.f)
        {
            Destination.position = (Begin + *End) * .5f;
            Destination.yaw = std::atan2(Edge.y, Edge.x) - battle::Pi * .5f;
            if (battle::Dot(battle::Forward(Destination.yaw), Destination.position - Anchor->pose.position) < 0)
                Destination.yaw = std::remainder(Destination.yaw + battle::Pi, 2.f * battle::Pi);
        }
    }

    const auto Parent = [&](battle::RegimentId Id) -> battle::command::NodeId
    {
        if (!World.command_state)
            return {};
        const auto& Nodes = World.command_state->hierarchy.nodes;
        const auto Node = std::ranges::find(Nodes, Id, &battle::command::CommandNode::regiment);
        return Node == Nodes.end() ? battle::command::NodeId{} : Node->parent;
    };
    const auto Group = Parent(Reference);
    std::vector<FManeuverRoute> Routes;
    std::vector<battle::RegimentId> Members;
    const auto Translation = Destination.position - Anchor->pose.position;
    for (auto Id : Owned)
    {
        if (Id != Reference && (!bGroup || !Group || Parent(Id) != Group))
            continue;
        Members.push_back(Id);
        const auto* Regiment = battle::FindRegiment(World, Id);
        const battle::Pose Goal{Regiment->pose.position + Translation, bGroup ? Regiment->pose.yaw : Destination.yaw};
        auto& Route = Routes.emplace_back(FManeuverRoute{Id, Regiment->pose, Goal, {}, {}, false});
        if (!bGroup && Width >= 1.f)
        {
            const auto Slots = (Width - 2.f * Regiment->formation.body_radius_m) / Regiment->formation.spacing_m;
            const auto Count = FMath::Clamp(FMath::FloorToInt(Slots) + 1, 1,
                static_cast<int32>(Regiment->units.size()));
            Route.Columns = uint32(Count);
        }
        if (Regiment->panic || Regiment->melee_engaged || battle::IsDeploying(World, *Regiment))
            continue;
        if (bGroup)
        {
            Route.bReachable = true;
            Route.Path = {Goal};
            continue;
        }
        const bool bShortWithdrawal = bPreserveFacing &&
            battle::PreserveFallbackFacing(World, *Regiment, Goal.position);
        auto Path = battle::FindFormationRoute(Navigation, World, *Regiment, Goal, bShortWithdrawal);
        if (Path.status != battle::Status::Ok)
            continue;
        battle::RegimentState AtGoal;
        AtGoal.id = Regiment->id;
        AtGoal.pose = Goal;
        AtGoal.formation = Regiment->formation;
        AtGoal.formation.columns = Route.Columns.value_or(AtGoal.formation.columns);
        AtGoal.units = Regiment->units;
        if (Route.Columns && (battle::FindFormationRoute(Navigation, World, AtGoal, Goal).status != battle::Status::Ok ||
            !battle::FormationRouteSegmentClear(World, AtGoal, Goal)))
            continue;
        Route.Path = std::move(Path.waypoints);
        Route.bReachable = true;
    }
    if (bGroup && Members.size() == 1)
        return PreviewManeuver(World, Navigation, Reference, Begin, {}, false, bPreserveFacing);
    if (bGroup && !Members.empty())
    {
        const auto Shared = battle::FindFormationGroupRoute(Navigation, World, Members, Translation);
        for (auto& Route : Routes)
        {
            Route.Path.clear();
            Route.bReachable = Shared.status == battle::Status::Ok;
            const auto Offset = Route.Start.position - Routes.front().Start.position;
            for (const auto& Point : Shared.waypoints)
                Route.Path.push_back({Point.position + Offset, Route.Start.yaw});
        }
    }
    return Routes;
}

namespace
{
using FTimePath = std::vector<battle::TimedFormationPose>;

bool ValidPlottedRoute(const battle::World& World, const FManeuverRoute& R)
{
    const auto* Regiment = battle::FindRegiment(World, R.Id);
    return Regiment && !Regiment->units.empty() && !Regiment->panic && !Regiment->melee_engaged &&
        !battle::IsDeploying(World, *Regiment) && R.Start == Regiment->pose && battle::Finite(R.Goal) &&
        !R.Path.empty() && battle::Distance(R.Path.back().position, R.Goal.position) < .001f &&
        std::ranges::all_of(R.Path, [](const auto& P) { return battle::Finite(P); }) &&
        (!R.Columns || (*R.Columns > 0 && *R.Columns <= Regiment->units.size()));
}

std::vector<FTimePath> TimePaths(const battle::World& World, std::span<const FManeuverRoute> Routes,
    battle::OrderType Type, battle::MovementPace Pace, float Delay)
{
    std::vector<FTimePath> Paths(Routes.size());
    std::vector<float> Speeds;
    for (size_t I = 0; I < Routes.size(); ++I)
    {
        const auto& Regiment = *battle::FindRegiment(World, Routes[I].Id);
        Speeds.push_back(battle::FormationTravelSpeed(World, Regiment, Type, Pace));
        auto& Path = Paths[I];
        Path.push_back({0.f, Routes[I].Start, battle::Footprint(Regiment)});
        if (Delay > 0.f)
            Path.push_back({Delay, Routes[I].Start, Path.front().footprint});
    }
    // Each group member has the same waypoint count and translates at the slowest member's pace.
    for (size_t K = 0; K < Routes.front().Path.size(); ++K)
    {
        const auto From = Paths.front().back().pose;
        const auto To = Routes.front().Path[K];
        const float Turn = std::remainder(To.yaw - From.yaw, 2.f * battle::Pi);
        if (std::abs(Turn) > .0001f)
        {
            const auto& Regiment = *battle::FindRegiment(World, Routes.front().Id);
            const bool bTactics = World.scenario.rules.regiment_tactics;
            const float Pressure = 1.f - .35f * Regiment.artillery_pressure;
            float Rate = Regiment.turn_rate * Pressure * (bTactics
                ? (1.f - .35f * Regiment.fatigue) * (Regiment.disordered ? .65f : 1.f) : 1.f);
            if (bTactics && !Regiment.panic && Speeds.front() > 0.f)
            {
                // Estimate the same outer-file pace as the simulation on level ground.
                const auto Footprint = Paths.front().back().footprint;
                const float Body = Regiment.formation.body_radius_m;
                const float Radius = std::hypot(Footprint.width_m * .5f - Body, Footprint.depth_m * .5f - Body);
                if (Radius > 0.f)
                    Rate = FMath::Min(Rate, Speeds.front() * Pressure / Radius);
            }
            if (Rate <= 0.f)
                return Paths;
            const int Steps = FMath::CeilToInt(std::abs(Turn) / .08f);
            for (int Step = 1; Step <= Steps; ++Step)
            {
                const auto Last = Paths.front().back();
                Paths.front().push_back({Last.seconds + std::abs(Turn) / (Rate * Steps),
                    {From.position, From.yaw + Turn * float(Step) / Steps}, Last.footprint});
            }
        }
        const float Distance = battle::Distance(From.position, To.position);
        const int Steps = FMath::CeilToInt(Distance); // Resolve local terrain speed along long straight legs.
        for (int Step = 1; Step <= Steps; ++Step)
        {
            float Duration = 0.f;
            std::vector<battle::Pose> Ends;
            for (size_t I = 0; I < Routes.size(); ++I)
            {
                const auto& Regiment = *battle::FindRegiment(World, Routes[I].Id);
                const auto Start = Paths[I].back().pose;
                const auto Goal = Routes[I].Path[K];
                const auto End = Step == Steps ? Goal.position :
                    Start.position + (Goal.position - Start.position) * (1.f / (Steps - Step + 1));
                const auto Troop = battle::FindUnit(World, Regiment.units.front())->troop;
                float Speed = Speeds[I] * battle::TerrainSpeedMultiplier(Start.position, End,
                    World.scenario.obstacles, Troop, Speeds[I]);
                if (World.scenario.hill)
                    Speed *= battle::HillUphillSpeedMultiplier(*World.scenario.hill, Start.position, End, Troop, Speeds[I]);
                if (Speed <= 0.f)
                    return Paths;
                Duration = std::max(Duration, battle::Distance(Start.position, End) / Speed);
                Ends.push_back({End, Goal.yaw});
            }
            if (Duration > 0.f)
                for (size_t I = 0; I < Routes.size(); ++I)
                {
                    const auto Last = Paths[I].back();
                    Paths[I].push_back({Last.seconds + Duration, Ends[I], Last.footprint});
                }
        }
    }
    for (size_t I = 0; I < Routes.size(); ++I)
        if (Routes[I].Columns)
        {
            const auto& Regiment = *battle::FindRegiment(World, Routes[I].Id);
            const auto Columns = std::clamp<size_t>(*Routes[I].Columns, 1, Regiment.units.size());
            const auto Rows = (Regiment.units.size() + Columns - 1) / Columns;
            const auto& Form = Regiment.formation;
            Paths[I].back().footprint = {float(Columns - 1) * Form.spacing_m + 2.f * Form.body_radius_m,
                float(Rows - 1) * Form.spacing_m + 2.f * Form.body_radius_m};
        }
    return Paths;
}
}

void CheckManeuverConflicts(const battle::World& World, const battle::NavigationService& Navigation,
    std::span<FManeuverRoute> Routes, battle::OrderType Type, battle::MovementPace Pace,
    std::span<const battle::CommandEnvelope> Pending)
{
    if (Routes.empty() || (Pending.empty() && World.pending_orders.empty()) ||
        std::ranges::any_of(Routes, [&](const auto& R) { return !R.bReachable ||
            R.Path.size() != Routes.front().Path.size() || !ValidPlottedRoute(World, R); }))
        return;
    std::map<battle::RegimentId, const battle::Order*> Latest;
    const auto Remember = [&](const battle::Order& Order)
    {
        const auto* Regiment = battle::FindRegiment(World, Order.recipient);
        if (Regiment && Regiment->team == 0 && !Regiment->units.empty() &&
            std::ranges::find(Routes, Order.recipient, &FManeuverRoute::Id) == Routes.end())
            Latest[Order.recipient] = &Order;
    };
    for (const auto& Order : World.pending_orders)
        Remember(Order);
    for (const auto& Envelope : Pending)
        if (Envelope.command.kind == battle::CommandKind::IssueOrder)
            Remember(Envelope.command.order);
    using FGroupKey = std::tuple<battle::OfficerId, battle::Tick, uint64, battle::RegimentId>;
    std::map<FGroupKey, std::vector<battle::RegimentId>> Groups;
    for (const auto& [Id, Order] : Latest)
        if (Order->type == battle::OrderType::MoveTo || Order->type == battle::OrderType::FallBack)
            Groups[{Order->issuer, Order->delivery_tick, Order->maneuver_group,
                Order->maneuver_group ? battle::RegimentId{} : Id}].push_back(Id);
    if (Groups.empty())
        return;
    const auto Candidate = TimePaths(World, Routes, Type, Pace, 1.f);
    for (const auto& [Key, Members] : Groups)
    {
        const auto& First = *Latest.at(Members.front());
        const auto& Lead = *battle::FindRegiment(World, Members.front());
        const auto Shared = Members.size() > 1 ? battle::FindFormationGroupRoute(Navigation, World, Members,
            First.point - Lead.pose.position) : battle::PathResult{};
        std::vector<FManeuverRoute> Other;
        for (const auto Id : Members)
        {
            const auto& Order = *Latest.at(Id);
            const auto& Regiment = *battle::FindRegiment(World, Id);
            const battle::Pose Goal{Order.point, Order.facing.value_or(Regiment.pose.yaw)};
            auto& R = Other.emplace_back(FManeuverRoute{Id, Regiment.pose, Goal, Order.move_columns, {}, true});
            if (Members.size() == 1)
                R.Path = battle::FindFormationRoute(Navigation, World, Regiment, Goal,
                    Order.type == battle::OrderType::FallBack &&
                    battle::PreserveFallbackFacing(World, Regiment, Goal.position)).waypoints;
            else
                for (const auto& P : Shared.waypoints)
                    R.Path.push_back({P.position + (Regiment.pose.position - Lead.pose.position), Regiment.pose.yaw});
        }
        const auto DelayTicks = First.delivery_tick - std::min(First.delivery_tick, World.tick);
        const auto Occupied = TimePaths(World, Other, First.type, First.pace,
            float(DelayTicks) / FRegimentBattlePlayback::StepsPerSecond);
        for (size_t I = 0; I < Routes.size(); ++I)
            for (size_t J = 0; J < Occupied.size(); ++J)
            {
                const auto Hit = battle::FirstFormationRouteConflict(Candidate[I], Occupied[J]);
                if (Hit && (!Routes[I].TrafficConflict || Hit->seconds < Routes[I].TrafficConflict->Seconds))
                {
                    Routes[I].TrafficConflict = FManeuverConflict{Members[J], Hit->position, Hit->seconds};
                    Routes[I].bReachable = false;
                }
            }
    }
}

FText QueueManeuver(const battle::World& World, std::span<const FManeuverRoute> Routes, battle::OrderType Type,
    battle::MovementPace Pace, std::vector<battle::CommandEnvelope>& Pending, uint64& Sequence,
    const battle::NavigationService& Navigation)
{
    std::vector<FManeuverRoute> Checked(Routes.begin(), Routes.end());
    CheckManeuverConflicts(World, Navigation, Checked, Type, Pace, Pending);
    const FManeuverConflict* First = nullptr;
    for (const auto& R : Checked)
        if (R.TrafficConflict && (!First || R.TrafficConflict->Seconds < First->Seconds))
            First = &*R.TrafficConflict;
    if (First)
        return FText::Format(LOCTEXT("ManeuverTraffic", "Через {0} с путь займёт полк №{1}."),
            FMath::RoundToInt(First->Seconds), First->Other.value);
    if (Routes.empty() || (Type != battle::OrderType::MoveTo && Type != battle::OrderType::FallBack) ||
        (Routes.size() > 1 && Type != battle::OrderType::MoveTo) ||
        std::ranges::any_of(Routes, [&](const auto& R) { return !R.bReachable ||
            R.Path.size() != Routes.front().Path.size() || !ValidPlottedRoute(World, R); }))
    {
        for (const auto& Route : Routes)
            if (const auto* Regiment = battle::FindRegiment(World, Route.Id))
            {
                const auto Reason = FormationOrderBlocker(World, *Regiment);
                if (!Reason.IsEmpty())
                    return FText::Format(LOCTEXT("ManeuverBlocker", "{0} · {1}"), RegimentName(*Regiment), Reason);
            }
        return LOCTEXT("ManeuverBlocked", "Для выбранного строя нет прохода.");
    }
    const auto* Officer = LivingBlueUnit(World, World.controlled);
    const auto* Record = Officer ? OfficerRecord(World, Officer->id) : nullptr;
    if (!Record || World.outcome != battle::BattleOutcome::Running)
        return LOCTEXT("NeedControlledOfficer", "Возьмите управление офицером.");
    const auto Owned = Regiments(World, Officer->id);
    if (std::ranges::any_of(Routes, [&](const auto& Route) { return std::ranges::find(Owned, Route.Id) == Owned.end(); }))
        return LOCTEXT("RegimentOutsideCommand", "Этот полк не подчиняется выбранному офицеру.");

    const uint64 ManeuverGroup = Routes.size() > 1 ? Sequence + 1 : 0;
    for (const auto& Route : Routes)
    {
        if (const auto* Regiment = battle::FindRegiment(World, Route.Id); WillFollow(World, *Regiment, Officer->id, Pending))
        {
            battle::Command Release;
            Release.kind = battle::CommandKind::SetRegimentFollowing;
            Release.unit = Officer->id;
            Release.regiment = Route.Id;
            Pending.push_back({World.tick, Sequence++, Release});
        }
        battle::Command Command;
        Command.kind = battle::CommandKind::IssueOrder;
        Command.unit = Officer->id;
        Command.regiment = Route.Id;
        auto& Order = Command.order;
        Order.type = Type;
        Order.issuer = Record->id;
        Order.recipient = Route.Id;
        Order.point = Route.Goal.position;
        Order.facing = Route.Goal.yaw;
        Order.move_columns = Route.Columns;
        Order.pace = Pace;
        Order.maneuver_group = ManeuverGroup;
        Order.priority = battle::PlayerOrderPriority;
        Order.created_tick = World.tick;
        Order.delivery_tick = World.tick + FRegimentBattlePlayback::StepsPerSecond;
        Pending.push_back({World.tick, Sequence++, Command});
    }
    return FText::Format(LOCTEXT("ManeuverQueued", "Манёвр передаётся. Полков {0}."), Routes.size());
}

battle::UnitId PersonalTarget(const battle::World& World, float AimYaw)
{
    const auto* Officer = LivingBlueUnit(World, World.controlled);
    if (!Officer || !OfficerRecord(World, Officer->id) || !std::isfinite(AimYaw))
        return {};
    const float Range = Officer->weapon.range;
    if (Range <= 0.f)
        return {};
    const auto Forward = battle::Forward(AimYaw);
    battle::UnitId Target;
    float BestScore = 1.f;
    for (const auto& Candidate : World.units)
    {
        if (!Candidate.alive)
            continue;
        const auto Offset = Candidate.pose.position - Officer->pose.position;
        const float Distance = battle::Length(Offset);
        if (Distance > Range)
            continue;
        const float Horizontal = std::hypot(Offset.x, Offset.y);
        const float Alignment = Horizontal > .001f ? battle::Dot(Offset, Forward) / Horizontal : 1.f;
        if (Alignment < .5f)
            continue;
        const float Score = 1.f - Alignment + .35f * Distance / Range;
        if (Score >= BestScore || battle::UnitTeam(World, Candidate) == 0 ||
            !battle::command::HasLineOfSight(Officer->pose.position, Candidate.pose.position, World.scenario.obstacles))
            continue;
        Target = Candidate.id;
        BestScore = Score;
    }
    return Target;
}
}

#undef LOCTEXT_NAMESPACE
