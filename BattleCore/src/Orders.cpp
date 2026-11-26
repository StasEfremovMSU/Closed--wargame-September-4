#include "battle/Orders.h"
#include "battle/World.h"
namespace battle {
bool ValidOrderShape(const Order &order) {
    return Finite(order.point) && uint8_t(order.type) <= uint8_t(OrderType::Pursue) &&
           uint8_t(order.pace) <= uint8_t(MovementPace::Run) &&
           (!order.facing || std::isfinite(*order.facing)) &&
           (!order.maneuver_group || order.type == OrderType::MoveTo) &&
           (!order.move_columns || (order.type == OrderType::MoveTo && *order.move_columns > 0));
}
Status SubmitOrder(World &w, const Order &input) {
    auto *r = FindRegiment(w, input.recipient);
    if (!r)
        return Status::NotFound;
    if (!ValidOrderShape(input) || input.id.value == UINT64_MAX ||
        (!input.id && w.next_order_id == UINT64_MAX))
        return Status::InvalidInput;
    Order o = input;
    if (!o.id)
        o.id = {w.next_order_id++};
    else
        w.next_order_id = std::max(w.next_order_id, o.id.value + 1);
    if (IsAttacking(o.type)) {
        bool target = true;
        if (o.target_unit) {
            auto *u = FindUnit(w, o.target_unit);
            target = u && u->alive && UnitTeam(w, *u) != r->team;
        } else if (o.target_regiment) {
            auto *t = FindRegiment(w, o.target_regiment);
            target = t && !t->units.empty() && t->team != r->team;
        }
        if (!target) {
            o.status = OrderStatus::Failed;
            if (!r->order || r->order->status != OrderStatus::Active)
                r->order = o;
            return Status::NotFound;
        }
    }
    o.status = OrderStatus::Pending;
    w.pending_orders.push_back(o);
    return Status::Ok;
}
} // namespace battle
