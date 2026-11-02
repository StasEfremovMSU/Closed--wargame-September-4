#pragma once
#include "Scenario.h"
namespace battle {
enum class OrderType:uint8_t{Hold,MoveTo,Attack,Withdraw,Rally};
enum class OrderStatus:uint8_t{Pending,Active,Completed,Failed,Cancelled};
struct Order{OrderId id{};OfficerId issuer{};RegimentId recipient{};OrderType type{OrderType::Hold};int priority{};Vec3 point{};UnitId target_unit{};RegimentId target_regiment{};Tick created_tick{},delivery_tick{},expiry_tick{};OrderStatus status{OrderStatus::Pending};};
enum class CommandKind:uint8_t{IssueOrder,ReformRegiment,AcquireControl,ReleaseControl,MoveControlledUnit,AttackControlledUnit,ExternalPoseSample};
// A tagged wire record; only fields belonging to kind are consumed.
struct Command{CommandKind kind{CommandKind::IssueOrder};Order order{};RegimentId regiment{};FormationSpec formation{};UnitId unit{},target{};Vec3 velocity{};Pose pose{};Tick sample_tick{};};
struct CommandEnvelope{Tick tick{};uint64_t sequence{};Command command{};};
struct World;
Status SubmitOrder(World&,const Order&);
}
